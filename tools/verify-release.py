#!/usr/bin/env python3
"""Check release ZIP integrity and runtime-only contents without launching the game."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import sys
import zipfile


PLATFORMS = ("Linux", "Windows")
WINDOWS_LAUNCHERS = {"toca-vulkan.bat", "toca-direct3d12.bat"}
BUILD_DLL = re.compile(r"(?:dxcompiler|dxil|SDL3_shadercross|(?:lib)?spirv-cross[^/]*)\.dll", re.IGNORECASE)


def require(condition: object, message: str) -> None:
    if not condition:
        raise ValueError(message)


def verify_archive(path: Path, platform: str) -> None:
    executable = "toca.exe" if platform == "Windows" else "toca"
    with zipfile.ZipFile(path) as archive:
        names: set[str] = set()
        files: set[str] = set()
        for entry in archive.infolist():
            name = entry.filename
            parts = PurePosixPath(name).parts
            require(name and not name.startswith("/") and "\\" not in name and ":" not in name
                    and all(part not in {"", ".", ".."} for part in name.rstrip("/").split("/")),
                    f"unsafe archive path: {name!r}")
            require(not stat.S_ISLNK(entry.external_attr >> 16), f"archive contains a symlink: {name}")
            identity = name.casefold() if platform == "Windows" else name
            require(identity not in names, f"duplicate archive path: {name}")
            names.add(identity)
            if entry.is_dir():
                require(parts[0] in ({"assets", "game-data", "shaders", "lib"} if platform == "Linux"
                                     else {"assets", "game-data", "shaders"}), f"non-runtime archive directory: {name}")
                continue
            files.add(name)
            suffix = PurePosixPath(name).suffix
            allowed = name in {executable, "README.md"} or (platform == "Windows" and name in WINDOWS_LAUNCHERS)
            if parts[0] == "assets":
                allowed = suffix in {".ogg", ".webp", ".glb"}
            elif parts[0] == "game-data":
                allowed = suffix in {".bfp", ".ogg"}
            elif parts[0] == "shaders":
                allowed = suffix in {".spv", ".dxil", ".msl"}
            elif platform == "Linux" and len(parts) == 2 and parts[0] == "lib":
                allowed = re.fullmatch(r"[^/]+\.so(?:\.\d+)*", parts[1]) is not None
            elif platform == "Windows" and len(parts) == 1 and suffix.lower() == ".dll":
                allowed = BUILD_DLL.fullmatch(name) is None
            require(allowed, f"non-runtime archive member: {name}")

        require({executable, "README.md"} <= files, f"missing executable or README in {path.name}")
        for directory in ("assets", "game-data", "shaders"):
            require(any(name.startswith(f"{directory}/") for name in files), f"missing {directory} payload")
        require(any(name.startswith("game-data/") and name.endswith(".bfp") for name in files), "missing BFP data")
        if platform == "Windows":
            require("sdl3.dll" in {name.casefold() for name in files}, "missing SDL3.dll")
            require(WINDOWS_LAUNCHERS <= files, f"missing Windows backend launchers: {', '.join(sorted(WINDOWS_LAUNCHERS - files))}")
        else:
            require(any(name.startswith("lib/") for name in files), "missing bundled shared libraries")
        require(archive.testzip() is None, f"ZIP CRC32 check failed: {path.name}")
        with archive.open(executable) as binary:
            header = binary.read(64)
            if platform == "Linux":
                require(header.startswith(b"\x7fELF\x02\x01") and header[18:20] == b"\x3e\x00",
                        "executable is not Linux x86_64 ELF")
            else:
                require(len(header) == 64 and header[:2] == b"MZ", "executable is not Windows PE")
                binary.seek(struct.unpack_from("<I", header, 60)[0])
                require(binary.read(6) == b"PE\0\0\x64\x86", "executable is not Windows x86_64 PE")


def verify_release(directory: Path, platforms: list[str]) -> None:
    expected = {f"toca-{platform}-x86_64.zip" for platform in platforms}
    actual = {path.name for path in directory.glob("*.zip")}
    require(actual == expected, f"release archive set differs: expected {sorted(expected)}, found {sorted(actual)}")
    for platform in platforms:
        verify_archive(directory / f"toca-{platform}-x86_64.zip", platform)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parents[1] / "dist")
    parser.add_argument("--platform", choices=PLATFORMS, action="append", required=True)
    args = parser.parse_args()
    try:
        verify_release(args.output_dir, args.platform)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
