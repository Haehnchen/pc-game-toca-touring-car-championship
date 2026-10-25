"""Check selected-target builds and real CTest filtering without compiling game targets."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("toca_check", PROJECT / "tools/check.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def selected(*names):
    return subprocess.CompletedProcess([], 0, json.dumps({"tests": [{"name": name} for name in names]}), "")


def complete(code=0):
    return subprocess.CompletedProcess([], code)


class CheckTests(unittest.TestCase):
    def test_ctest_selection_builds_only_deduplicated_mapped_targets(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "test-targets.json").write_text(json.dumps({
                "first": ["shared"], "second": ["shared", "extra"], "no-build-target": []}), encoding="utf-8")
            filters = ["-R", "first|second", "-L", "unit", "-E", "slow"]
            with patch.object(CHECK.subprocess, "run", side_effect=[selected("first", "second", "no-build-target"), complete(), complete(8)]) as run:
                with contextlib.redirect_stdout(io.StringIO()):
                    code = CHECK.check(build, "Debug", "custom-cmake", "custom-ctest", 3, 2, None, filters)
            self.assertEqual(code, 8, "CTest failures must retain their original exit code")
            selection, compilation, execution = [call.args[0] for call in run.call_args_list]
            self.assertEqual(selection, [*execution, "--show-only=json-v1"])
            self.assertEqual(execution[-len(filters):], filters)
            self.assertEqual(compilation, ["custom-cmake", "--build", str(build), "--config", "Debug",
                                           "--parallel", "3", "--target", "shared", "extra"])

    def test_listing_and_explicit_target_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)  # Deliberately no automatic target manifest.
            for listing in (["-N"], ["--show-only=json-v1"]):
                with self.subTest(listing=listing):
                    with patch.object(CHECK.subprocess, "run", side_effect=[selected("case"), complete()]) as run:
                        self.assertEqual(CHECK.check(build, "Debug", "cmake", "ctest", 4, 2, None, listing), 0)
                    self.assertEqual(run.call_count, 2)
                    self.assertTrue(all(call.args[0][0] == "ctest" for call in run.call_args_list))
            with patch.object(CHECK.subprocess, "run", side_effect=[selected("case"), complete(7)]) as run:
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(CHECK.check(build, "Release", "cmake", "ctest", 4, 2, ["unknown-user-target"], []), 7)
            self.assertEqual(run.call_count, 2, "A failed explicit target build must prevent test execution")
            self.assertEqual(run.call_args.args[0][-2:], ["--target", "unknown-user-target"])

    def test_empty_or_unmapped_selection_fails_before_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "test-targets.json").write_text("{}", encoding="utf-8")
            for result, message in ((selected(), "selected no tests"), (selected("unregistered"), "no build-target mapping")):
                with self.subTest(message=message):
                    with patch.object(CHECK.subprocess, "run", return_value=result) as run:
                        with self.assertRaisesRegex(ValueError, message):
                            CHECK.check(build, "Debug", "cmake", "ctest", 4, 2, None, [])
                    self.assertEqual(run.call_count, 1)
            failure = subprocess.CompletedProcess([], 4, "", "invalid CTest option\n")
            with patch.object(CHECK.subprocess, "run", return_value=failure) as run:
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(CHECK.check(build, "Debug", "cmake", "ctest", 4, 2, None, []), 4)
            self.assertEqual(run.call_count, 1)

    def test_disabled_cache_rejects_stale_manifest_before_any_subprocess(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "test-targets.json").write_text(json.dumps({"old-case": ["old-target"]}), encoding="utf-8")
            for value in ("OFF", "0", "False", "NO", "N", "IGNORE", "NOTFOUND", "", "old-NOTFOUND"):
                for override in (None, ["old-target"]):
                    with self.subTest(value=value, override=override):
                        (build / "CMakeCache.txt").write_text(f"BUILD_TESTING:BOOL={value}\n", encoding="utf-8")
                        with patch.object(CHECK.subprocess, "run") as run:
                            with self.assertRaisesRegex(ValueError, "-DBUILD_TESTING=ON"):
                                CHECK.check(build, "Debug", "cmake", "ctest", 4, 2, override, [])
                        run.assert_not_called()

    def test_real_ctest_combines_regex_labels_and_exclusions(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            marker = build / "selected.txt"
            (build / "case.py").write_text(
                "from pathlib import Path\nimport sys\n"
                "assert sys.argv[1] == 'selected', 'An excluded test ran'\n"
                f"Path({str(marker)!r}).write_text('passed')\n", encoding="utf-8")
            cases = [("audio-fast", "unit;audio", "selected"),
                     ("audio-slow", "unit;audio", "excluded"),
                     ("video-fast", "unit;video", "excluded"),
                     ("audio-fixture", "audio;fixture", "excluded")]
            declarations = []
            for name, labels, argument in cases:
                declarations.append(f'add_test("{name}" "{Path(sys.executable).as_posix()}" "{(build / "case.py").as_posix()}" "{argument}")')
                declarations.append(f'set_tests_properties("{name}" PROPERTIES LABELS "{labels}")')
            (build / "CTestTestfile.cmake").write_text("\n".join(declarations) + "\n", encoding="utf-8")
            (build / "test-targets.json").write_text(json.dumps({name: [] for name, _, _ in cases}), encoding="utf-8")
            code = CHECK.main(["--build-dir", str(build), "--", "-R", "^audio-", "-L", "unit", "-E", "slow"])
            self.assertEqual(code, 0)
            self.assertEqual(marker.read_text(), "passed")

    def test_real_ctest_profiles_preserve_explicit_selection_and_label_exclusions(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "case.py").write_text(
                "from pathlib import Path\nimport sys\nPath(sys.argv[1]).touch()\n", encoding="utf-8")
            cases = {"unit-fast": "unit;audio", "unit-slow": "unit;audio;slow",
                     "unit-both": "unit;slow;other", "native-short": "integration",
                     "native-slow": "integration;slow", "extended-parser": "parser;extended"}
            declarations = []
            for name, labels in cases.items():
                marker = build / f"{name}.ran"
                declarations.append(f'add_test("{name}" "{Path(sys.executable).as_posix()}" "{(build / "case.py").as_posix()}" "{marker.as_posix()}")')
                declarations.append(f'set_tests_properties("{name}" PROPERTIES LABELS "{labels}")')
            (build / "CTestTestfile.cmake").write_text("\n".join(declarations) + "\n", encoding="utf-8")
            (build / "test-targets.json").write_text(json.dumps({name: [] for name in cases}), encoding="utf-8")
            scenarios = [([], [], {"unit-fast", "unit-slow", "unit-both"}, "quick"),
                         (["--profile", "full"], [], set(cases), "full"),
                         ([], ["--tests-regex=^native-short$"], {"native-short"}, "full"),
                         ([], ["--label-regex", "integration"], {"native-short", "native-slow"}, "full"),
                         ([], ["-E", "^unit-"], {"native-short", "native-slow", "extended-parser"}, "full"),
                         (["--profile", "quick"], ["--label-exclude=slow"], {"unit-fast"}, "quick"),
                         (["--profile", "quick"], ["-LE", "slow", "--label-exclude", "other"],
                          {"unit-fast", "unit-slow"}, "quick"),
                         (["--profile", "quick"], ["-R", "^native-"], set(), "quick"),
                         (["--profile", "quick"], ["-I", "4,4"], set(), "quick"),
                         ([], ["--rerun-failed"], {"extended-parser"}, "full"),
                         ([], ["-U", "1", "-I", "6,6", "-R", "^unit-fast$"],
                          {"unit-fast", "extended-parser"}, "full")]
            for options, filters, expected, profile in scenarios:
                with self.subTest(options=options, filters=filters):
                    for marker in build.glob("*.ran"):
                        marker.unlink()
                    failed = build / "Testing/Temporary/LastTestsFailed.log"
                    failed.parent.mkdir(parents=True, exist_ok=True)
                    failed.write_text("6:extended-parser\n", encoding="utf-8")
                    output, errors = io.StringIO(), io.StringIO()
                    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                        code = CHECK.main(["--build-dir", str(build), *options, "--", *filters])
                    self.assertEqual(code, 0 if expected else 2, errors.getvalue())
                    self.assertEqual({marker.stem for marker in build.glob("*.ran")}, expected)
                    self.assertIn(f"Test profile: {profile} ({len(expected)} selected)", output.getvalue())
                    if not expected:
                        self.assertIn("selected no tests", errors.getvalue())

    def test_forced_quick_rejects_modes_that_bypass_label_filters_before_subprocess(self):
        with tempfile.TemporaryDirectory() as temporary:
            for filters in (["--rerun-failed"], ["-U", "1"], ["--union", "true"]):
                with self.subTest(filters=filters):
                    with patch.object(CHECK.subprocess, "run") as run:
                        with contextlib.redirect_stderr(io.StringIO()) as errors:
                            code = CHECK.main(["--build-dir", temporary, "--profile", "quick", "--", *filters])
                    self.assertEqual(code, 2)
                    self.assertIn("CTest bypasses label filters", errors.getvalue())
                    self.assertIn("use --profile auto or --profile full", errors.getvalue())
                    run.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
