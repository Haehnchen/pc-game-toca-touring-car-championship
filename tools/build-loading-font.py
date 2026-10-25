#!/usr/bin/env python3
"""Manually author the fourfold LOADHIGH font; normal builds only copy it."""

import argparse
from pathlib import Path
import runpy
import struct
import zlib

from git_game_data import configured_data_root
from loading_font_geometry import (LOADING_ADVANCE, LOADING_ATLAS_SIZE,
                                   LOADING_GLYPHS, LOADING_LINE_HEIGHT,
                                   LOADING_REFERENCE_CRC32,
                                   LOADING_SPACE_WIDTH, loading_font_atlas)

PROJECT = Path(__file__).resolve().parents[1]
ARCHIVE_CRC32 = "af7b3a1d"


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def validate_source(path):
    archive = path.read_bytes()
    if crc32(archive) != ARCHIVE_CRC32:
        raise ValueError("Expected unchanged canonical1.1a LOADHIGH archive")
    read_bfp = runpy.run_path(str(PROJECT / "tools/extract-assets.py"))["read_bfp"]
    _, _, offset, size = read_bfp(archive)[0]
    source = archive[offset:offset + size]
    if crc32(source) != LOADING_REFERENCE_CRC32:
        raise ValueError("Unexpected LOADHIGH font member")
    width, height = struct.unpack_from("<II", source, 18)
    appendix = 0x436 + ((width + 3) & ~3) * height
    metrics = struct.unpack_from("<iii", source, appendix + 4)
    if (width, height) != LOADING_ATLAS_SIZE or metrics != (LOADING_LINE_HEIGHT, LOADING_SPACE_WIDTH, LOADING_ADVANCE):
        raise ValueError("Authored font must retain original atlas dimensions and metrics")
    count = source[appendix + 1]
    cells = {}
    for index in range(count):
        code, x, y, w, h = struct.unpack_from("<BxHHHH", source, appendix + 24 + index * 16)
        cells[chr(code)] = (x, y, w, h)
    if cells != LOADING_GLYPHS:
        raise ValueError("Authored font must retain every original glyph code and cell")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=PROJECT / "assets/ui/mega/loading")
    parser.add_argument("--data-root", type=Path, help="Unpacked Git game-data root; defaults to TOCA_DATA_ROOT")
    args = parser.parse_args()
    validate_source((args.data_root or configured_data_root()) / "data/loadhigh.bfp")
    image = loading_font_atlas()
    args.output.mkdir(parents=True, exist_ok=True)
    path = args.output / "font.webp"
    image.save(path, format="WEBP", quality=70, method=6, alpha_quality=100, exact=True)
    data = path.read_bytes()
    print(f"{path}: {image.width}x{image.height}, {len(data)}bytes, CRC32{crc32(data)}")


if __name__ == "__main__":
    main()
