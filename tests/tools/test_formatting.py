"""Exercise optional formatter discovery and the explicit formatting build target."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(__file__).resolve().parents[2] / "cmake/Formatting.cmake"
CMAKE = shutil.which("cmake")
NINJA = shutil.which("ninja")


@unittest.skipUnless(CMAKE and NINJA, "CMake and Ninja are required")
class FormattingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="toca formatting ")
        self.addCleanup(temporary.cleanup)
        self.source = Path(temporary.name)
        self.build = self.source / "build"
        self.log = self.source / "formatter.jsonl"
        (self.source / "src").mkdir()
        self.sources = []
        for index in range(7):
            source = self.source / "src" / f"sample {index}.cpp"
            source.write_text(f"int value_{index};\n", encoding="utf-8")
            self.sources.append(source)
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\nproject(formatting-probe NONE)\n"
            f'include("{HELPER.as_posix()}")\ntoca_setup_formatting()\n'
            'add_custom_target(ordinary-build ALL COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/built")\n',
            encoding="utf-8")

    def run_command(self, *command):
        return subprocess.run(command, capture_output=True, text=True, timeout=30)

    def formatter(self, version):
        script = self.source / "formatter.py"
        script.write_text(
            "import json\nfrom pathlib import Path\nimport sys\n"
            f"with Path({str(self.log)!r}).open('a', encoding='utf-8') as log:\n"
            "    log.write(json.dumps(sys.argv[1:]) + '\\n')\n"
            "if sys.argv[1:] == ['--version']:\n"
            f"    print('clang-format version {version}')\n"
            "else:\n"
            "    assert sys.argv[1:3] == ['--dry-run', '--Werror']\n"
            "    sys.exit(any('BAD' in Path(name).read_text() for name in sys.argv[3:]))\n",
            encoding="utf-8")
        if sys.platform == "win32":
            executable = self.source / "clang-format.cmd"
            executable.write_text(f'@"{sys.executable}" "{script}" %*\n', encoding="utf-8")
        else:
            executable = self.source / "clang-format"
            executable.write_text(f"#!{sys.executable}\n" + script.read_text(encoding="utf-8"), encoding="utf-8")
            executable.chmod(0o755)
        return executable

    def configure_and_build(self, executable):
        result = self.run_command(CMAKE, "-S", str(self.source), "-B", str(self.build), "-G", "Ninja",
                                  f"-DCMAKE_MAKE_PROGRAM={NINJA}", f"-DCLANG_FORMAT_EXECUTABLE={executable}",
                                  "-DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF", "-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF",
                                  "-DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF", "-DCMAKE_FIND_USE_CMAKE_PATH=OFF")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.run_command(CMAKE, "--build", str(self.build))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.build / "built").is_file())
        self.assertFalse(self.log.exists(), "Ordinary configure/build must never run the formatter")

    def format_check(self):
        return self.run_command(CMAKE, "--build", str(self.build), "--target", "format-check", "--parallel", "6")

    def calls(self):
        return [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()]

    def test_missing_formatter_only_fails_explicit_check(self):
        for executable in ("CLANG_FORMAT_EXECUTABLE-NOTFOUND", str(self.source / "absent-formatter")):
            with self.subTest(executable=executable):
                self.configure_and_build(executable)
                result = self.format_check()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Install clang-format 21.1.8", " ".join((result.stdout + result.stderr).split()))
                self.assertIn("-DCLANG_FORMAT_EXECUTABLE=", result.stdout + result.stderr)

    def test_wrong_version_only_fails_explicit_check(self):
        self.configure_and_build(self.formatter("21.1.80"))
        result = self.format_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("21.1.80", result.stdout + result.stderr)
        self.assertIn("-DCLANG_FORMAT_EXECUTABLE=", result.stdout + result.stderr)
        self.assertEqual(self.calls(), [["--version"]], "Version rejection must prevent all source checks")

    def test_valid_formatter_checks_all_sources_once(self):
        self.configure_and_build(self.formatter("21.1.8"))
        result = self.format_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = self.calls()
        self.assertEqual(calls.count(["--version"]), 1)
        self.assertEqual(len(calls), 7, "The validated formatter must run six source batches")
        checked = [Path(name) for call in calls if call != ["--version"] for name in call[2:]]
        self.assertCountEqual(checked, self.sources)

    def test_formatter_failure_reaches_build_result(self):
        self.configure_and_build(self.formatter("21.1.8"))
        self.sources[0].write_text("BAD\n", encoding="utf-8")
        result = self.format_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls().count(["--version"]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
