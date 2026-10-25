"""Probe the public CTest helper without configuring or compiling dependencies."""

from pathlib import Path
import json
import subprocess
import tempfile
import unittest

HELPER = Path(__file__).resolve().parents[2] / "cmake/Testing.cmake"


class CMakeTestHelperTests(unittest.TestCase):
    def configure(self, declaration):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            (source / "case.py").write_text("print('case')\n")
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\nproject(helper-probe NONE)\n"
                f'include("{HELPER.as_posix()}")\nenable_testing()\n{declaration}\n')
            result = subprocess.run(["cmake", "-S", str(source), "-B", str(source / "build")],
                                    capture_output=True, text=True, timeout=15)
            tests = source / "build/CTestTestfile.cmake"
            mapping = source / "build/test-targets.json"
            return result, tests.read_text() if tests.exists() else "", json.loads(mapping.read_text()) if mapping.exists() else {}

    def test_rejects_unknown_missing_and_invalid_arguments(self):
        for declaration, diagnostic in (
            ("toca_add_test(TARGET case NAME probe SOURCE case.py UNKNOWN value)", "unknown arguments"),
            ("toca_add_test(NAME probe SOURCE case.py)", "TARGET is required"),
            ("toca_add_test(TARGET case SOURCE case.py)", "NAME is required"),
            ("toca_add_test(TARGET case NAME probe)", "SOURCE is required"),
            ("toca_add_test(TARGET case NAME probe SOURCE)", "missing values"),
            ("toca_add_test(TARGET case NAME probe SOURCE absent.py)", "SOURCE does not exist"),
            ("toca_add_test(TARGET case NAME probe SOURCE case.py TIMEOUT 0)", "positive integer"),
            ("toca_add_test(TARGET case NAME probe SOURCE case.py ENVIRONMENT invalid)", "NAME=VALUE"),
        ):
            with self.subTest(declaration=declaration):
                result, _, _ = self.configure(declaration)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stdout + result.stderr)
        for keyword in ("TARGET", "NAME", "SOURCE", "LIBRARIES", "LABELS", "ARGS", "ENVIRONMENT", "TIMEOUT"):
            with self.subTest(missing_value=keyword):
                result, _, _ = self.configure(f"toca_add_test({keyword})")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("missing values", result.stdout + result.stderr)

    def test_preserves_target_reuse_and_declared_case_options(self):
        result, tests, targets = self.configure(
            'set(original_data_directory "${CMAKE_CURRENT_SOURCE_DIR}/unpacked Git inputs")\n'
            'add_custom_target(toca-original-data)\n'
            'toca_add_test(TARGET case NAME first SOURCE case.py LABELS unit parser '
            'ARGS "argument with spaces" ENVIRONMENT "INPUT=some value" TIMEOUT 7)\n'
            'toca_add_test(TARGET case NAME second DISPLAY ARGS other TIMEOUT 9)\n'
            'get_property(display_processors TEST second PROPERTY PROCESSORS)\n'
            'if(NOT display_processors EQUAL 2)\n'
            '  message(FATAL_ERROR "Display checks must reserve two process slots")\n'
            'endif()\n'
            'get_property(ordinary_processors TEST first PROPERTY PROCESSORS SET)\n'
            'if(ordinary_processors)\n'
            '  message(FATAL_ERROR "Ordinary checks must retain the default process slots")\n'
            'endif()\n'
            'set_tests_properties(second PROPERTIES PROCESSORS 8)\n'
            'get_property(override_processors TEST second PROPERTY PROCESSORS)\n'
            'if(NOT override_processors EQUAL 8)\n'
            '  message(FATAL_ERROR "Case process reservations must override the display default")\n'
            'endif()\n'
            'get_property(dependencies TARGET case PROPERTY MANUALLY_ADDED_DEPENDENCIES)\n'
            'if(NOT "toca-original-data" IN_LIST dependencies)\n'
            '  message(FATAL_ERROR "Python checks require the bundled game-data target")\n'
            'endif()')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for value in ("first", "second", "argument with spaces", "INPUT=some value", '"unit;parser"', '"27"', '"29"'):
            self.assertIn(value, tests)
        self.assertEqual(tests.count("TOCA_DATA_ROOT="), 2)
        self.assertIn("/unpacked Git inputs", tests)
        self.assertEqual(targets, {"first": ["case"], "second": ["case"]})


if __name__ == "__main__":
    unittest.main(verbosity=2)
