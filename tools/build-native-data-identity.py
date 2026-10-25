#!/usr/bin/env python3
"""Fingerprint the versioned native data sources for network content matching."""

import argparse
from pathlib import Path, PurePosixPath
import struct
import tempfile
import zlib


def source_manifest(root, files):
    root = Path(root).resolve()
    names = sorted(PurePosixPath(name).as_posix() for name in files)
    if not names or len(names) != len(set(names)):
        raise ValueError("Native data sources must be a nonempty, unique file list")
    manifest = bytearray(b"TOCA-NATIVE-DATA\0")
    for name in names:
        relative = PurePosixPath(name)
        path = root / name
        if relative.is_absolute() or ".." in relative.parts or not path.resolve().is_relative_to(root):
            raise ValueError(f"Native data source is outside the project: {name}")
        # Git checkouts on Windows and Unix must identify the same native data.
        data = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        manifest.extend(name.encode("utf-8") + b"\0")
        manifest.extend(struct.pack("<II", len(data), zlib.crc32(data)))
    return bytes(manifest)


def generate(root, files, output):
    manifest = source_manifest(root, files)
    content = (
        "// Generated from versioned native data; no original game executable is used.\n"
        "#pragma once\n#include <cstdint>\n"
        "namespace toca::network::native_data {\n"
        f"inline constexpr std::uint32_t manifest_size = {len(manifest)};\n"
        f"inline constexpr std::uint32_t manifest_crc32 = 0x{zlib.crc32(manifest):08x};\n"
        "} // namespace toca::network::native_data\n"
    )
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_file() and output.read_text(encoding="utf-8") == content:
        return
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output.parent,
                                     prefix=".native-data-", delete=False) as temporary:
        temporary.write(content)
        pending = Path(temporary.name)
    try:
        pending.replace(output)
    finally:
        pending.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()
    generate(args.source_root, args.files, args.output)


if __name__ == "__main__":
    main()
