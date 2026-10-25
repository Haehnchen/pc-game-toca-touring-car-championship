"""Actual tiny CMake/Ninja projects prove fresh test metadata on the fast path."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[2] / "tools/configure.py"
CMAKE = shutil.which("cmake")


@unittest.skipUnless(CMAKE and shutil.which("ninja"), "CMake and Ninja are required")
class ConfigureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.build = self.root / "build"
        self.source.mkdir()
        (self.source / "inputs").mkdir()
        (self.source / "inputs/a.txt").write_text("first\n")
        (self.source / "extra.cmake").write_text('set(CASE_NAME first)\n')
        (self.source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.25)
project(configure_fixture NONE)
include(CTest)
include(extra.cmake)
file(GLOB inputs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/inputs/*.txt")
file(APPEND "${CMAKE_BINARY_DIR}/configure-count.txt" "configure\\n")
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${VALUE}|${inputs}")
add_test(NAME "${CASE_NAME}" COMMAND "${CMAKE_COMMAND}" -E true)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/test-targets.json" CONTENT "{\\"${CASE_NAME}\\":[]}")
''')

    def run_configure(self, *flags, generator="Ninja", build=None, success=True):
        build = self.build if build is None else build
        command = [sys.executable, str(TOOL), "--", CMAKE, "-S", str(self.source), "-B", str(build), "-G", generator, *flags]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def count(self, build=None):
        return len(((build or self.build) / "configure-count.txt").read_text().splitlines())

    def test_noop_preserves_exact_argv_and_does_not_configure(self):
        value = "spaces;literal$(must_not_execute)"
        self.run_configure(f"-DVALUE:STRING={value}")
        result = self.run_configure(f"-DVALUE:STRING={value}")
        self.assertIn("refresh Ninja metadata", result.stdout)
        self.assertEqual(self.count(), 1)
        self.assertTrue((self.build / "selected.txt").read_text().startswith(value + "|"))

    def test_include_and_glob_changes_regenerate_before_selection(self):
        self.run_configure()
        (self.source / "extra.cmake").write_text("set(CASE_NAME changed_case)\n")
        self.run_configure()
        self.assertEqual(self.count(), 2)
        self.assertEqual(json.loads((self.build / "test-targets.json").read_text()), {"changed_case": []})
        self.assertIn("changed_case", (self.build / "CTestTestfile.cmake").read_text())
        (self.source / "inputs/new.txt").write_text("new source\n")
        self.run_configure()
        self.assertEqual(self.count(), 3)
        self.assertIn("new.txt", (self.build / "selected.txt").read_text())

    def test_flag_cache_and_deleted_mapping_force_full_configure(self):
        self.run_configure("-DVALUE:STRING=one")
        self.assertIn("full configure", self.run_configure("-DVALUE:STRING=two").stdout)
        cache = self.build / "CMakeCache.txt"
        cache.write_text(cache.read_text() + "\nEXTERNAL_EDIT:STRING=present\n")
        self.assertIn("full configure", self.run_configure("-DVALUE:STRING=two").stdout)
        (self.build / "test-targets.json").unlink()
        self.assertIn("full configure", self.run_configure("-DVALUE:STRING=two").stdout)
        self.assertTrue((self.build / "test-targets.json").is_file())
        self.assertEqual(self.count(), 4)

    def test_changed_build_metadata_and_invalid_source_are_not_skipped(self):
        self.run_configure()
        ninja = self.build / "build.ninja"
        ninja.write_text(ninja.read_text() + "\n# external change\n")
        self.assertIn("full configure", self.run_configure().stdout)
        self.assertEqual(self.count(), 2)
        (self.source / "extra.cmake").write_text("not_a_real_cmake_command()\n")
        self.run_configure(success=False)

    def test_other_generator_uses_full_configure(self):
        self.run_configure(generator="Ninja Multi-Config")
        self.run_configure(generator="Ninja Multi-Config")
        self.assertEqual(self.count(), 2)

    def test_fresh_and_changed_source_are_never_skipped(self):
        self.run_configure("--fresh")
        self.run_configure("--fresh")
        self.assertEqual(self.count(), 2)
        self.run_configure()
        other_source = self.root / "other-source"
        shutil.copytree(self.source, other_source)
        self.source = other_source
        result = self.run_configure(success=False)
        self.assertIn("full configure", result.stdout)


if __name__ == "__main__":
    unittest.main()
