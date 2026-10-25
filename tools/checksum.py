#!/usr/bin/env python3
"""Compute or verify eight-digit CRC32 checksums using Python's standard library."""

import argparse
from pathlib import Path
import re
import sys
import zlib


def crc32_file(path):
    checksum = 0
    with Path(path).open("rb") as source:
        while chunk := source.read(1024 * 1024):
            checksum = zlib.crc32(chunk, checksum)
    return f"{checksum:08x}"


def check_manifest(path, quiet=False):
    failed = False
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    if not lines:
        raise ValueError("Empty CRC32 manifest")
    for number, line in enumerate(lines, 1):
        match = re.fullmatch(r"([0-9a-fA-F]{8})  (.+)", line)
        if not match:
            raise ValueError(f"Invalid CRC32 entry at {path}:{number}")
        expected, filename = match.groups()
        try:
            actual = crc32_file(filename)
            if actual != expected.lower():
                raise ValueError(f"CRC32 {actual}; expected {expected.lower()}")
            if not quiet:
                print(f"{filename}: OK")
        except (OSError, ValueError) as error:
            print(f"{filename}: {error}", file=sys.stderr)
            failed = True
    return not failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", nargs="?", type=Path)
    parser.add_argument("--check", type=Path, help="manifest with paths relative to the current directory")
    parser.add_argument("--quiet", action="store_true", help="omit successful manifest entries")
    args = parser.parse_args()
    if (args.file is None) == (args.check is None) or (args.quiet and args.check is None):
        parser.error("choose a file or --check MANIFEST; --quiet requires --check")
    try:
        if args.check is not None:
            return 0 if check_manifest(args.check, args.quiet) else 1
        print(crc32_file(args.file))
        return 0
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
