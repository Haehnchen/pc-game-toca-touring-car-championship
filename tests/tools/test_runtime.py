"""Check storage, environment and owned-process cleanup of the native test runner."""

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

RUNNER = Path(__file__).resolve().parents[2] / "tools/run-isolated.py"


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.artifacts = Path(self.temporary.name)

    def command(self, code, *options):
        return [sys.executable, str(RUNNER), "--name", "probe", "--artifacts",
                str(self.artifacts), *options, "--", sys.executable, "-c", code]

    def test_private_storage_environment_and_parallel_cases(self):
        code = ("import json,os,pathlib; "
                "pathlib.Path('environment.json').write_text(json.dumps(dict(os.environ))); "
                "pathlib.Path('current-directory').write_text(os.getcwd())")
        environment = dict(os.environ, DISPLAY=":0", WAYLAND_DISPLAY="host-wayland",
                           XAUTHORITY="host-auth", DBUS_SESSION_BUS_ADDRESS="host-bus",
                           SDL_AUDIODRIVER="host-audio", PULSE_SERVER="host-pulse",
                           ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=0")
        environment.pop("LP_NUM_THREADS", None)
        commands = [subprocess.Popen(self.command(code, "--env", "CASE_INPUT=expected"),
                                    env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True) for _ in range(2)]
        for process in commands:
            output, _ = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, output)
        cases = list(self.artifacts.iterdir())
        self.assertEqual(len(cases), 2)
        for case in cases:
            captured = json.loads((case / "work/environment.json").read_text())
            for key in ("DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY"):
                self.assertNotIn(key, captured)
            for key in ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME",
                        "XDG_STATE_HOME", "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"):
                self.assertTrue(Path(captured[key]).is_relative_to(case), key)
            self.assertEqual(captured["SDL_VIDEODRIVER"], "x11")
            self.assertEqual(captured["SDL_AUDIODRIVER"], "dummy")
            self.assertEqual(captured["CASE_INPUT"], "expected")
            self.assertEqual(captured["ASAN_OPTIONS"], "detect_leaks=1")
            self.assertEqual(captured["UBSAN_OPTIONS"], "halt_on_error=1:print_stacktrace=1")
            self.assertEqual(captured["LP_NUM_THREADS"], "2")
            self.assertEqual(Path((case / "work/current-directory").read_text()), case / "work")

    def test_software_renderer_workers_preserve_inherited_and_explicit_settings(self):
        code = "import os,pathlib; pathlib.Path('workers').write_text(os.environ['LP_NUM_THREADS'])"
        environment = dict(os.environ, LP_NUM_THREADS="3")
        for options, expected in (((), "3"), (("--env", "LP_NUM_THREADS=4"), "4")):
            with self.subTest(options=options):
                before = set(self.artifacts.iterdir())
                result = subprocess.run(self.command(code, *options), env=environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                case, = set(self.artifacts.iterdir()) - before
                self.assertEqual((case / "work/workers").read_text(), expected)

    def test_timeout_cleans_descendant_that_ignores_term(self):
        child = ("import pathlib,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
                 "pathlib.Path('child-ready').write_text('yes'); time.sleep(60)")
        code = ("import pathlib,subprocess,sys,time; "
                f"child=subprocess.Popen([sys.executable,'-c',{child!r}]); "
                "pathlib.Path('child-pid').write_text(str(child.pid)); time.sleep(60)")
        start = time.monotonic()
        result = subprocess.run(self.command(code, "--timeout", "0.5"),
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 124, result.stdout + result.stderr)
        self.assertLess(time.monotonic() - start, 8)
        case = next(self.artifacts.iterdir())
        self.assertTrue((case / "work/child-ready").exists())
        pid = int((case / "work/child-pid").read_text())
        # A child reparented to the container init may remain briefly as a zombie.
        status = Path(f"/proc/{pid}/stat")
        if status.exists():
            self.assertEqual(status.read_text().split(") ", 1)[1][0], "Z")

    def test_cleanup_kills_descendant_after_group_leader_exits(self):
        child = ("import pathlib,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
                 "pathlib.Path('child-ready').write_text('yes'); time.sleep(60)")
        code = ("import pathlib,subprocess,sys,time; "
                f"child=subprocess.Popen([sys.executable,'-c',{child!r}]); "
                "pathlib.Path('child-pid').write_text(str(child.pid)); "
                "exec(\"while not pathlib.Path('child-ready').exists(): time.sleep(0.01)\")")
        result = subprocess.run(self.command(code), capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        case = next(self.artifacts.iterdir())
        pid = int((case / "work/child-pid").read_text())
        status = Path(f"/proc/{pid}/stat")
        deadline = time.monotonic() + 1
        while status.exists() and status.read_text().split(") ", 1)[1][0] != "Z" and time.monotonic() < deadline:
            time.sleep(0.01)
        if status.exists():
            self.assertEqual(status.read_text().split(") ", 1)[1][0], "Z")

    @unittest.skipUnless(shutil.which("Xvfb") and shutil.which("xdpyinfo"),
                         "Xvfb/xdpyinfo are unavailable; private-display coverage skipped")
    def test_private_display_is_ready_and_removed_after_exit(self):
        code = ("import os,pathlib,subprocess; "
                "subprocess.run(['xdpyinfo'],check=True,stdout=subprocess.DEVNULL); "
                "pathlib.Path('display').write_text(os.environ['DISPLAY'])")
        result = subprocess.run(self.command(code, "--display"), capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        case = next(self.artifacts.iterdir())
        display = (case / "work/display").read_text()
        self.assertRegex(display, r"^:[0-9]+$")
        probe = subprocess.run(["xdpyinfo", "-display", display], capture_output=True, timeout=2)
        self.assertNotEqual(probe.returncode, 0, "Owned X server remained alive after exit")


if __name__ == "__main__":
    unittest.main(verbosity=2)
