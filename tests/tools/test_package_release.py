"""Check real ELF relocation and Windows PE import closure and ZIP contents."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("toca_package_release", ROOT / "tools/package_release.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)

RUNTIME_SHADERS = {"legacy.vert.spv", "legacy.frag.spv", "world.vert.spv"}
RUNTIME_STARTUP = {"ui/original/title.webp", "ui/mega/title-wide.webp"}
RUNTIME_GARAGES = {"ui/original/garage/gar1-car0.webp", "ui/mega/garage/gar1-car0.webp"}
RUNTIME_LOADING = {f"ui/mega/loading/{name}.webp" for name in (
    "donington", "silverstone", "thruxton", "brands-hatch", "oulton-park",
    "donington-short", "croft", "knockhill", "snetterton", "lavaland", "font", "sides")}


def bfp_bytes(members):
    payloads = [payload for _, payload in members]
    result = bytearray(struct.pack("<III", len(members), 24 * len(members), max(map(len, payloads))))
    offset = 0
    for name, payload in members:
        result += struct.pack("<16sII", name.encode().ljust(16, b"\0"), offset, len(payload))
        offset += len(payload)
    return bytes(result) + b"".join(payloads)


def write(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)
    return path


@unittest.skipUnless(sys.platform.startswith("linux") and all(shutil.which(tool) for tool in ("cc", "strip", "readelf", "ldd")),
                     "Linux ELF packaging tools are required")
class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="toca-package-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source project"
        self.build = self.root / "build tree"
        self.output = self.root / "distribution"
        self.source.mkdir()
        self.build.mkdir()
        self.bundled_files = ("data/config.bfp", "data/allcars.bfp", "data/donlfree.bfp")
        self.bundled_data = self.build / "generated/original-data"
        for name in self.bundled_files:
            write(self.bundled_data / name, b"bundled fixture " + name.encode())
        self.audio_directory = self.build / "generated/audio"
        self.audio_files = ("fe/sounds/updown.ogg", "data/sounddry.bfp", "data/soundwet.bfp",
                            "speech/pc/english/extras/final.ogg", "speech/pc/french/extras/final.ogg",
                            "speech/pc/german/extras/final.ogg")
        for name in self.audio_files:
            payload = (bfp_bytes([("ENGINE.WAV", b"OggS fixture effect")]) if name.endswith(".bfp")
                       else b"OggS fixture " + name.encode())
            write(self.audio_directory / name, payload)
        write(self.source / "assets/audio/frontend.tar.zst", b"committed audio source")
        # Leftovers must never enter the positive runtime allowlist.
        write(self.audio_directory / "fe/sounds/updown.wav", b"unused source PCM")
        write(self.audio_directory / "speech/pc/english/extras/final.raw", b"unused source PCM")
        write(self.audio_directory / ".cache/audio.ogg", b"unused conversion cache")
        write(self.audio_directory / ".cache/audio.bfp", b"unused conversion cache")
        write(self.audio_directory / "authoring/preview.ogg", b"unused authoring audio")
        write(self.audio_directory / "data/preview.ogg", b"unused authoring audio")
        write(self.build / "assets/original-data/data/sounddry.bfp", b"duplicate bundled audio")
        for track in range(2, 7):
            write(self.build / f"assets/music/track{track:02}.ogg", b"runtime music")
        for name in RUNTIME_STARTUP:
            write(self.build / "assets" / name, b"committed startup artwork")
        for name in RUNTIME_GARAGES:
            write(self.build / "assets" / name, b"committed garage artwork")
        for name in RUNTIME_LOADING:
            write(self.build / "assets" / name, b"committed loading artwork " + name.encode())
        write(self.build / "assets/ui/mega/loading/generation.json", b"authoring provenance")
        write(self.build / "assets/ui/mega/loading/source.png", b"authoring image")
        write(self.build / "assets/ui/mega/loading/sources/header.webp", b"authoring image")
        write(self.build / "assets/ui/mega/garage/backgrounds/honda.webp", b"authoring background")
        write(self.build / "assets/ui/mega/garage/cars/gar1-car0.webp", b"authoring vehicle layer")
        write(self.build / "assets/models/mega/car-0.glb", b"runtime model")
        write(self.build / "assets/models/mega/scene/a.webp", b"runtime texture")
        write(self.build / "assets/models/mega/qa.json", b"authoring report")
        for name in RUNTIME_SHADERS:
            write(self.build / "shaders" / name, b"compiled shader")
        leaf_source = write(self.build / "fixture_leaf.c", b"int fixture_leaf(void) { return 40; }\n")
        library_source = write(self.build / "fixture_library.c", b"int fixture_leaf(void); int fixture_value(void) { return fixture_leaf() + 2; }\n")
        executable_source = write(self.build / "fixture_main.c", b"int fixture_value(void); int main(void) { return fixture_value() != 42; }\n")
        self.library_dir = self.build / "libraries"
        self.library_dir.mkdir()
        self.leaf = self.library_dir / "libtoca-package-leaf.so"
        subprocess.run(["cc", "-g", "-shared", "-fPIC", "-Wl,-soname,libtoca-package-leaf.so",
                        str(leaf_source), "-o", str(self.leaf)], check=True, capture_output=True, timeout=15)
        self.library = self.library_dir / "libtoca-package-fixture.so"
        subprocess.run(["cc", "-g", "-shared", "-fPIC", "-Wl,-soname,libtoca-package-fixture.so", "-Wl,-rpath,$ORIGIN",
                        str(library_source), "-L", str(self.library_dir), "-ltoca-package-leaf", "-o", str(self.library)], check=True, capture_output=True, timeout=15)
        executable = self.build / "toca-game"
        subprocess.run(["cc", "-g", str(executable_source), "-L", str(self.library_dir), "-ltoca-package-fixture",
                        "-Wl,-rpath,$ORIGIN/lib", "-Wl,-rpath-link," + str(self.library_dir), "-o", str(executable)], check=True, capture_output=True, timeout=15)
        self.metadata = {
            "schema_version": 1, "executable": str(executable),
            "asset_directory": str(self.build / "assets"), "shader_directory": str(self.build / "shaders"),
            "shader_files": [str(path) for path in sorted((self.build / "shaders").iterdir())],
            "source_directory": str(self.source),
            "audio_directory": str(self.build / "generated/audio"),
            "original_data_directory": str(self.bundled_data),
            "library_directories": [str(self.library_dir)], "strip": shutil.which("strip"), "readelf": shutil.which("readelf"),
            "system": "Linux", "processor": os.uname().machine, "configuration": "Release",
        }
        write(self.source / "README.md", (ROOT / "README.md").read_bytes())
        self.cache = {
            "TOCA_DISTRIBUTION": "ON", "CMAKE_BUILD_TYPE": "Release", "TOCA_SANITIZERS": "OFF",
            "BUILD_TESTING": "OFF", "TOCA_USE_SYSTEM_SDL3": "OFF", "CMAKE_STRIP": shutil.which("strip"),
            "CMAKE_READELF": shutil.which("readelf"), "CMAKE_HOME_DIRECTORY": str(self.source),
        }
        self.save_inputs()

    def save_inputs(self):
        (self.build / "toca-package.json").write_text(json.dumps(self.metadata))
        (self.build / "CMakeCache.txt").write_text("".join(f"{name}:STRING={value}\n" for name, value in self.cache.items()))

    def package(self):
        with mock.patch.object(PACKAGE, "ROOT", self.source):
            return PACKAGE.package(self.build, self.output)

    def test_zip_is_stripped_complete_and_runs_after_relocation(self):
        archive = self.package()
        with zipfile.ZipFile(archive) as package:
            self.assertIsNone(package.testzip())
            names = set(package.namelist())
            expected = {"README.md", "toca", "lib/libtoca-package-fixture.so", "lib/libtoca-package-leaf.so"}
            expected.update("game-data/" + name for name in self.bundled_files + self.audio_files)
            expected.update("assets/music/track%02d.ogg" % track for track in range(2, 7))
            expected.update("assets/" + name for name in RUNTIME_STARTUP | RUNTIME_GARAGES | RUNTIME_LOADING)
            expected.update({"assets/models/mega/car-0.glb", "assets/models/mega/scene/a.webp"})
            expected.update("shaders/" + name for name in RUNTIME_SHADERS)
            self.assertEqual(names, expected)
            self.assertFalse(any(name.endswith(".bat") for name in names))
            self.assertEqual(package.read("README.md"), (self.source / "README.md").read_bytes())
            for name in RUNTIME_LOADING:
                self.assertEqual(package.read("assets/" + name), (self.build / "assets" / name).read_bytes())
            self.assertEqual(package.getinfo("toca").external_attr >> 16 & 0o111, 0o111)
            for name in self.bundled_files:
                self.assertEqual(package.read("game-data/" + name), (self.bundled_data / name).read_bytes())
            for name in self.audio_files:
                self.assertEqual(package.read("game-data/" + name), (self.audio_directory / name).read_bytes())
            relocated = self.root / "relocated package"
            package.extractall(relocated)
        staged = relocated
        (staged / "toca").chmod(0o755)
        original_sections = subprocess.run(["readelf", "-S", str(self.build / "toca-game")], capture_output=True, text=True, check=True).stdout
        self.assertIn(".debug_info", original_sections, "Packaging must strip the copy only")
        self.source.rename(self.root / "unavailable source")
        self.build.rename(self.root / "unavailable build")
        clean_environment = {name: value for name, value in os.environ.items() if not name.startswith(("LD_", "DYLD_"))}
        result = subprocess.run([str(staged / "toca")], cwd=self.root, env=clean_environment,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        for binary in (staged / "toca", staged / "lib/libtoca-package-fixture.so", staged / "lib/libtoca-package-leaf.so"):
            sections = subprocess.run(["readelf", "-S", str(binary)], capture_output=True, text=True, check=True).stdout
            self.assertNotIn(".debug_info", sections)
            self.assertNotIn(".symtab", sections)

    def test_available_artwork_and_shader_subset_are_packaged(self):
        omitted_artwork = ("ui/original/title.webp", "ui/original/garage/gar1-car0.webp")
        for name in omitted_artwork:
            (self.build / "assets" / name).unlink()
        self.metadata["shader_files"] = [str(self.build / "shaders/world.vert.spv")]
        self.save_inputs()
        with zipfile.ZipFile(self.package()) as archive:
            names = set(archive.namelist())
            for name in omitted_artwork:
                self.assertNotIn("assets/" + name, names)
            self.assertIn("assets/ui/mega/title-wide.webp", names)
            self.assertIn("assets/ui/mega/garage/gar1-car0.webp", names)
            self.assertEqual({name for name in names if name.startswith("shaders/")}, {"shaders/world.vert.spv"})

    def test_custom_build_settings_do_not_block_packaging(self):
        self.cache.update({"TOCA_DISTRIBUTION": "OFF", "BUILD_TESTING": "ON",
                           "TOCA_SANITIZERS": "ON", "CMAKE_BUILD_TYPE": "Debug", "TOCA_USE_SYSTEM_SDL3": "ON"})
        self.metadata["configuration"] = "Debug"
        self.save_inputs()
        with zipfile.ZipFile(self.package()) as archive:
            self.assertIn("toca", archive.namelist())

    def test_missing_referenced_shader_preserves_existing_package(self):
        archive = self.package()
        before = archive.read_bytes()
        (self.build / "shaders/world.vert.spv").unlink()
        with self.assertRaisesRegex(PACKAGE.PackagingError, "missing regular input file"):
            self.package()
        self.assertEqual(archive.read_bytes(), before)

    def test_output_cannot_overwrite_originals_or_source_assets(self):
        source = self.source / "assets/audio/frontend.tar.zst"
        before = source.read_bytes()
        for output in (self.source / "assets/data", self.source / "assets/packages", self.source / "assets"):
            with self.subTest(output=output), mock.patch.object(PACKAGE, "ROOT", self.source):
                with self.assertRaises(PACKAGE.PackagingError):
                    PACKAGE.package(self.build, output)
        self.assertEqual(source.read_bytes(), before)


class WindowsReleasePackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="toca windows package ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.build = self.root / "build tree"
        self.output = self.root / "distribution"
        self.executable = write(self.build / "toca-game.exe", b"fixture executable debug-info")
        self.sdl_directory = self.build / "SDL library"
        self.runtime_directory = self.root / "compiler runtime"
        self.sdl = write(self.sdl_directory / "SDL3.dll", b"fixture SDL debug-info")
        self.shared = write(self.sdl_directory / "shared.dll", b"fixture transitive DLL debug-info")
        self.gcc = write(self.runtime_directory / "LIBSTDC++-6.DLL", b"fixture GCC debug-info")
        self.originals = write(self.build / "original-data/data/config.bfp", b"original fixture")
        self.audio = write(self.build / "audio/speech/pc/english/test.ogg", b"OggS fixture")
        self.asset = write(self.build / "assets/ui/original/title.webp", b"WebP fixture")
        self.shader = write(self.build / "shaders/world.vert.dxil", b"shader fixture")
        write(self.source / "README.md", b"player instructions")
        write(self.build / "assets/ui/mega/loading/generation.json", b"authoring provenance")
        self.system_root = self.root / "Windows"
        write(self.system_root / "System32/vendor-system.dll", b"host OS DLL")
        # OS libraries beside the game must still stay out of the archive.
        write(self.build / "KERNEL32.dll", b"never package OS DLLs")
        self.metadata = {
            "executable": str(self.executable), "system": "Windows", "processor": "AMD64",
            "asset_directory": str(self.build / "assets"), "shader_directory": str(self.shader.parent),
            "shader_files": [str(self.shader)], "original_data_directory": str(self.build / "original-data"),
            "audio_directory": str(self.build / "audio"), "strip": "fixture-strip", "objdump": "fixture-objdump",
            "library_directories": [str(self.sdl_directory)],
        }
        write(self.build / "toca-package.json", json.dumps(self.metadata).encode())
        write(self.build / "CMakeCache.txt", b"CMAKE_STRIP:FILEPATH=fixture-strip\nCMAKE_OBJDUMP:FILEPATH=fixture-objdump\n")
        self.imports = {
            self.executable: ["SDL3.dll", "libstdc++-6.dll", "KERNEL32.dll", "api-ms-win-crt-runtime-l1-1-0.dll",
                              "ext-ms-win-test.dll", "vendor-system.dll"],
            self.sdl: ["Shared.DLL", "USER32.dll"],
            self.shared: ["SDL3.dll", "ucrtbase.dll"],
            self.gcc: ["KERNEL32.dll"],
        }
        self.inspected = []
        self.stripped = []

    def run_tool(self, command, library_directories=None):
        tool, option, filename = command
        binary = Path(filename)
        if tool == "fixture-objdump":
            self.assertEqual(option, "-p")
            self.inspected.append(binary)
            return "PE import table\n" + "".join(f"    DLL Name: {name}\n" for name in self.imports[binary])
        self.assertEqual((tool, option), ("fixture-strip", "--strip-unneeded"))
        self.assertTrue(binary.is_relative_to(self.output), "Strip must only touch staged copies")
        self.stripped.append(binary.name)
        binary.write_bytes(binary.read_bytes().replace(b" debug-info", b""))
        return ""

    def package(self, host="Windows"):
        with mock.patch.object(PACKAGE, "ROOT", self.source), \
                mock.patch.object(PACKAGE.platform, "system", return_value=host), \
                mock.patch.dict(os.environ, {"PATH": str(self.runtime_directory), "SystemRoot": str(self.system_root)}), \
                mock.patch.object(PACKAGE, "run", side_effect=self.run_tool):
            return PACKAGE.package(self.build, self.output)

    def test_zip_contains_recursive_dll_closure_and_shared_runtime_payload(self):
        for host in ("Windows", "Linux"):
            with self.subTest(host=host):
                self.inspected.clear()
                self.stripped.clear()
                inputs = {path: path.read_bytes() for path in self.imports}
                archive = self.package(host=host)
                self.assertEqual(archive.name, "toca-Windows-x86_64.zip")
                self.assertEqual(list(self.output.glob("*.zip")), [archive])
                expected = {"toca.exe", "SDL3.dll", "Shared.DLL", "libstdc++-6.dll", "README.md",
                            "toca-vulkan.bat", "toca-direct3d12.bat",
                            "game-data/data/config.bfp", "game-data/speech/pc/english/test.ogg",
                            "assets/ui/original/title.webp", "shaders/world.vert.dxil"}
                with zipfile.ZipFile(archive) as package:
                    self.assertIsNone(package.testzip())
                    self.assertEqual(set(package.namelist()), expected)
                    self.assertEqual(package.read("toca.exe"), b"fixture executable")
                    self.assertEqual(package.read("Shared.DLL"), b"fixture transitive DLL")
                    self.assertEqual(package.read("game-data/data/config.bfp"), self.originals.read_bytes())
                    self.assertEqual(package.read("game-data/speech/pc/english/test.ogg"), self.audio.read_bytes())
                    self.assertEqual(package.read("assets/ui/original/title.webp"), self.asset.read_bytes())
                    self.assertEqual(package.read("shaders/world.vert.dxil"), self.shader.read_bytes())
                    readme = package.read("README.md").decode("utf-8")
                    self.assertTrue(readme.startswith("player instructions"))
                    self.assertIn("`toca.exe` to use Vulkan by default on Windows", readme)
                    self.assertIn("`toca.exe --gpu-driver direct3d12`", readme)
                    self.assertIn("`toca.exe --gpu-driver vulkan`", readme)
                    self.assertIn("The command-line option overrides\nthe `SDL_GPU_DRIVER` environment variable", readme)
                    self.assertIn("`toca-vulkan.bat` to use Vulkan", readme)
                    self.assertIn("`toca-direct3d12.bat` to use Direct3D 12", readme)
                    for driver in ("vulkan", "direct3d12"):
                        launcher = package.read(f"toca-{driver}.bat")
                        self.assertEqual(launcher.decode("ascii").split("\r\n"), [
                            "@echo off", "setlocal", f'"%~dp0toca.exe" --gpu-driver {driver} %*',
                            "exit /b %errorlevel%", ""])
                self.assertCountEqual(self.inspected, self.imports)
                self.assertCountEqual(self.stripped, ["toca.exe", "SDL3.dll", "Shared.DLL", "libstdc++-6.dll"])
                for path, contents in inputs.items():
                    self.assertEqual(path.read_bytes(), contents, "Packaging must preserve original binaries")

    def test_missing_transitive_dll_preserves_existing_archive(self):
        archive = self.package()
        before = archive.read_bytes()
        self.imports[self.shared].append("missing-runtime.dll")
        with self.assertRaisesRegex(PACKAGE.PackagingError, "missing DLL 'missing-runtime.dll'"):
            self.package()
        self.assertEqual(archive.read_bytes(), before)

    def test_conflicting_importer_local_dlls_are_rejected(self):
        local = write(self.build / "shared.dll", b"different local DLL")
        self.imports[local] = []
        self.imports[self.gcc] = ["shared.dll"]
        with self.assertRaisesRegex(PACKAGE.PackagingError, "conflicting paths for DLL"):
            self.package()

    def test_import_paths_cannot_escape_the_archive(self):
        for invalid in ("../escape.dll", "C:escape.dll", "sub\\escape.dll", "", "not-a-library.exe"):
            with self.subTest(invalid=invalid):
                self.imports[self.executable] = [invalid]
                with self.assertRaisesRegex(PACKAGE.PackagingError, "invalid DLL import name"):
                    self.package()

    def test_tool_failure_preserves_existing_archive(self):
        archive = self.package()
        before = archive.read_bytes()
        run_tool = self.run_tool

        def fail_strip(command, library_directories=None):
            if command[0] == "fixture-strip":
                raise PACKAGE.PackagingError("strip failed")
            return run_tool(command, library_directories)

        with mock.patch.object(self, "run_tool", side_effect=fail_strip):
            with self.assertRaisesRegex(PACKAGE.PackagingError, "strip failed"):
                self.package()
        self.assertEqual(archive.read_bytes(), before)

    def test_requires_build_metadata_for_native_host(self):
        self.metadata["system"] = "Linux"
        write(self.build / "toca-package.json", json.dumps(self.metadata).encode())
        with self.assertRaisesRegex(PACKAGE.PackagingError, "requires a native Linux or Windows build"):
            self.package()


class MakeReleaseTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("make"), "GNU Make is required")
    def test_make_release_builds_game_and_keeps_run_build_separate(self):
        result = subprocess.run(["make", "--no-print-directory", "-n", "release", "DIST_BUILD_DIR=build/package-fixture",
                                 "RELEASE_OUTPUT_DIR=dist-fixture", "BUILD_TARGETS=unrelated-test"], cwd=ROOT,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        for expected in ("-DCMAKE_BUILD_TYPE=\"Release\"", "-DTOCA_DISTRIBUTION=ON", "-DBUILD_TESTING=OFF",
                         "-DTOCA_SANITIZERS=OFF", "--target toca-game", '--build-dir "build/package-fixture"',
                         '--output-dir "dist-fixture"'):
            self.assertIn(expected, result.stdout)
        self.assertNotIn("--target unrelated-test", result.stdout)
        self.assertNotIn("build/release", result.stdout)


if __name__ == "__main__":
    unittest.main()
