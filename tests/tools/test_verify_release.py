#!/usr/bin/env python3
"""Release archive integrity and runtime-only payload checks with synthetic ZIPs."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import warnings
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("verify_release", ROOT / "tools/verify-release.py")
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


def executable(platform: str) -> bytes:
    header = bytearray(70)
    if platform == "Linux":
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = b"\x3e\x00"
    else:
        header[:2] = b"MZ"
        struct.pack_into("<I", header, 60, 64)
        header[64:] = b"PE\0\0\x64\x86"
    return bytes(header)


class ReleaseChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def archive(self, platform="Linux", extra=(), omit=()):
        path = self.directory / f"toca-{platform}-x86_64.zip"
        members = {
            "toca.exe" if platform == "Windows" else "toca": executable(platform),
            "README.md": b"Synthetic release fixture",
            "assets/music/track02.ogg": b"audio-payload-for-crc32-check",
            "game-data/data/synthetic.bfp": b"fixture archive",
            "shaders/legacy.vert.spv": b"fixture shader",
            "SDL3.dll" if platform == "Windows" else "lib/libSDL3.so.0": b"fixture library",
        }
        if platform == "Windows":
            members.update({"toca-vulkan.bat": b"fixture Vulkan launcher",
                            "toca-direct3d12.bat": b"fixture Direct3D12 launcher"})
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
            for name, content in list(members.items()) + list(extra):
                if name not in omit:
                    archive.writestr(name, content)
        return path

    def test_two_platform_release_set(self):
        self.archive()
        self.archive("Windows")
        VERIFY.verify_release(self.directory, ["Linux", "Windows"])

    def test_missing_or_extra_archive_fails(self):
        self.archive()
        with self.assertRaisesRegex(ValueError, "archive set differs"):
            VERIFY.verify_release(self.directory, ["Linux", "Windows"])
        self.archive("Windows")
        with self.assertRaisesRegex(ValueError, "archive set differs"):
            VERIFY.verify_release(self.directory, ["Linux"])

    def test_required_runtime_groups(self):
        for name in ("toca", "README.md", "assets/music/track02.ogg", "game-data/data/synthetic.bfp",
                     "shaders/legacy.vert.spv", "lib/libSDL3.so.0"):
            with self.subTest(member=name):
                path = self.archive(omit=(name,))
                with self.assertRaisesRegex(ValueError, "missing"):
                    VERIFY.verify_archive(path, "Linux")

    def test_authoring_tools_and_reference_files_fail(self):
        for name in ("references/tourcars.exe", "assets/source.py", "shaders/world.vert.hlsl",
                     "lib/libSDL3.a", "THIRD-PARTY-NOTICES.txt", "references/"):
            with self.subTest(member=name):
                path = self.archive(extra=((name, b"unexpected"),))
                with self.assertRaisesRegex(ValueError, "non-runtime"):
                    VERIFY.verify_archive(path, "Linux")
        for name in ("shadercross.exe", "dxcompiler.dll", "dxil.dll", "SDL3_shadercross.dll",
                     "libspirv-cross-c-shared.dll"):
            with self.subTest(member=name):
                path = self.archive("Windows", extra=((name, b"unexpected"),))
                with self.assertRaisesRegex(ValueError, "non-runtime"):
                    VERIFY.verify_archive(path, "Windows")

    def test_windows_case_collisions_and_missing_sdl_fail(self):
        path = self.archive("Windows", extra=(("sdl3.DLL", b"duplicate"),))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            VERIFY.verify_archive(path, "Windows")
        path = self.archive("Windows", omit=("SDL3.dll",))
        with self.assertRaisesRegex(ValueError, "missing SDL3.dll"):
            VERIFY.verify_archive(path, "Windows")

    def test_windows_requires_both_backend_launchers(self):
        for omitted in (("toca-vulkan.bat",), ("toca-direct3d12.bat",),
                        ("toca-vulkan.bat", "toca-direct3d12.bat")):
            with self.subTest(omitted=omitted):
                path = self.archive("Windows", omit=omitted)
                with self.assertRaisesRegex(ValueError, "missing Windows backend launchers"):
                    VERIFY.verify_archive(path, "Windows")

    def test_launchers_do_not_allow_unrelated_scripts_or_linux_batch_files(self):
        cases = (("Windows", "run.bat"), ("Windows", "toca-opengl.bat"),
                 ("Windows", "assets/toca-vulkan.bat"), ("Windows", "launchers/toca-direct3d12.bat"),
                 ("Linux", "toca-vulkan.bat"), ("Linux", "toca-direct3d12.bat"))
        for platform, name in cases:
            with self.subTest(platform=platform, member=name):
                path = self.archive(platform, extra=((name, b"unexpected script"),))
                with self.assertRaisesRegex(ValueError, "non-runtime"):
                    VERIFY.verify_archive(path, platform)

    def test_duplicate_path_fails(self):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            path = self.archive(extra=(("README.md", b"duplicate"),))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            VERIFY.verify_archive(path, "Linux")

    def test_unsafe_paths_fail(self):
        for name in ("../escape", "/absolute", "assets/../../escape", "assets\\escape", "C:/escape"):
            with self.subTest(member=name):
                path = self.archive(extra=((name, b"unexpected"),))
                with self.assertRaisesRegex(ValueError, "unsafe"):
                    VERIFY.verify_archive(path, "Linux")

    def test_corrupt_payload_fails_crc32(self):
        path = self.archive()
        path.write_bytes(path.read_bytes().replace(b"audio-payload-for-crc32-check", b"audio-payload-for-crc32-CHECK"))
        with self.assertRaisesRegex(ValueError, "CRC32"):
            VERIFY.verify_archive(path, "Linux")

    def test_wrong_executable_architecture_fails(self):
        for platform, name in (("Linux", "toca"), ("Windows", "toca.exe")):
            with self.subTest(platform=platform):
                path = self.archive(platform, omit=(name,))
                with zipfile.ZipFile(path, "a") as archive:
                    archive.writestr(name, b"not an executable")
                with self.assertRaisesRegex(ValueError, "executable is not"):
                    VERIFY.verify_archive(path, platform)


if __name__ == "__main__":
    unittest.main()
