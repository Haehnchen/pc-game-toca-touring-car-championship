#!/usr/bin/env python3
"""Flexmobile extraction and source-faithful colorkey WebP70 preparation."""

import argparse
import json
import math
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import vehicle_textures as images
from vehicle_manifest import load_manifest
from vehicle_model import decode, read_source

PROJECT = Path(__file__).resolve().parents[1]
PACKAGE = PROJECT / "assets/models/vehicles/flexmobile/textures"
SLOTS = ("FLEX", "ENGFLO")
SOURCE = dict(archive="data/allcars.bfp", archive_crc32="92273ec9", member=31,
              member_name="cars\\flex\\high\\f", member_crc32="3234277d")


def _entry():
    return next(entry for entry in load_manifest()["vehicles"] if entry["key"] == "flexmobile")


def _converter():
    converter = shutil.which("magick") or shutil.which("convert")
    if not converter:
        raise ValueError("ImageMagick with WebP support is required for Flexmobile artwork")
    return converter


def _metadata(directory):
    directory = Path(directory).resolve(strict=True)
    path = directory / "metadata.json"
    if not path.resolve(strict=True).is_relative_to(directory) or path.stat().st_size > 16384:
        raise ValueError("Invalid Flexmobile artwork metadata path/size")
    return json.loads(path.read_text(encoding="utf-8"))


def fit_uvs(meshes, section, count, directory=None):
    """The Flexmobile keeps its canonical UVs; body art replaces slots only."""
    return


def arch_coverage(width, height, triangles):
    """Rasterize surviving front-arch surfaces for finished-alpha validation.

    Coordinates are top-origin UVs from the actual repaired body faces. A
    triangle excludes the atlas background, wheel picture, and rear skirt.
    """
    result = bytearray(width * height)
    for triangle in triangles or ():
        if len(triangle) != 3 or any(len(p) != 2 or not all(math.isfinite(v) for v in p) for p in triangle):
            raise ValueError("Invalid Flexmobile front-arch UV triangle")
        points = [(u * width, v * height) for u, v in triangle]
        (ax, ay), (bx, by), (cx, cy) = points
        area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        if abs(area) < 1e-9:
            continue
        for y in range(max(0, math.floor(min(ay, by, cy))), min(height, math.ceil(max(ay, by, cy)))):
            sy = (y + .5) * 256 / height
            if not (sy < 34 or sy >= 225):
                continue
            for x in range(max(0, math.floor(min(ax, bx, cx)), math.ceil(186 * width / 256)),
                           min(width, math.ceil(max(ax, bx, cx)), math.ceil(244 * width / 256))):
                px, py = x + .5, y + .5
                a = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area
                b = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area
                if a >= -1e-8 and b >= -1e-8 and a + b <= 1 + 1e-8:
                    result[y * width + x] = 1
    return result


def prepare_pixels(name, original, width, height, rgba):
    """Apply the engine-floor key mask; body pixels are already finished."""
    if name != "ENGFLO" or len(rgba) != width * height * 4:
        raise ValueError("Invalid Flexmobile engine-floor pixel bounds")
    old_width, old_height, old_pixels = images.bmp_pixels(original)
    result = bytearray(rgba)
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 4
            source = old_pixels[(y * old_height // height) * old_width + x * old_width // width]
            if source == (0, 0, 0):
                result[offset:offset + 4] = b"\0\0\0\0"
            else:
                result[offset + 3] = 255
                if result[offset:offset + 3] == b"\0\0\0":
                    result[offset:offset + 3] = b"\x08\x08\x08"
    return bytes(result)


def prepare_artwork(name, original, source, metadata, arch_uvs=None, *, quality=70):
    """Encode the finished body directly; resize and key only the engine floor."""
    dimensions = metadata.get("final_dimensions", {}).get(name)
    if (not isinstance(dimensions, list) or len(dimensions) != 2 or
            any(type(v) is not int or not 1 <= v <= images.MAX_DIMENSION for v in dimensions)):
        raise ValueError(f"Invalid final Flexmobile dimensions for {name}")
    width, height = dimensions
    old_width, old_height, _ = images.bmp_dimensions(original)
    if width * old_height != height * old_width:
        raise ValueError("Flexmobile final dimensions must preserve the canonical aspect ratio")
    if name == "FLEX":
        data = Path(source).read_bytes()
        if (images.lossless_webp_dimensions(data) != (width, height)
                or images.crc32(data) != metadata.get("prepared_body_crc32")):
            raise ValueError("Flexmobile body requires its CRC-checked finished lossless WebP at final dimensions")
        rgba = subprocess.run([_converter(), str(source), "-depth", "8", "rgba:-"],
                              check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    elif name == "ENGFLO":
        rgba = subprocess.run([_converter(), str(source), "-alpha", "on", "-filter", "Lanczos",
                               "-resize", f"{width}x{height}!", "-depth", "8", "rgba:-"],
                              check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        rgba = prepare_pixels(name, original, width, height, rgba)
    else:
        raise ValueError("Unknown Flexmobile artwork slot")
    if len(rgba) != width * height * 4:
        raise ValueError("Flexmobile artwork lost its pixel bounds")
    if name == "FLEX" and arch_uvs:
        coverage = arch_coverage(width, height, arch_uvs)
        if any(covered and rgba[index * 4 + 3] != 255 for index, covered in enumerate(coverage)):
            raise ValueError("Finished Flexmobile body must cover its surviving front-arch faces")
    encoded = subprocess.run([_converter(), "-size", f"{width}x{height}", "-depth", "8", "rgba:-", "-strip",
                              "-define", "webp:lossless=false", "-define", "webp:alpha-quality=100", "-define", "webp:method=6",
                              "-quality", str(quality), "webp:-"], input=rgba, check=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if images.webp_dimensions(encoded) != (width, height):
        raise ValueError("Flexmobile final encoder changed texture dimensions")
    return encoded


def replace_textures(section, count, directory=None, arch_uvs=None, *, quality=70):
    if directory is None:
        return section
    metadata = _metadata(directory)
    return images.replace_textures(section, count, directory,
                                   prepare=lambda name, original, source: prepare_artwork(name, original, source, metadata, arch_uvs, quality=quality))


def extract(output):
    output = Path(output).resolve()
    if output != (PROJECT / "build/flexmobile-source-v09").resolve():
        raise ValueError("Original Flexmobile extraction must remain in ignored build/flexmobile-source-v09/")
    header, _, _, section = decode(read_source(SOURCE))
    output.mkdir(parents=True, exist_ok=True)
    records = images.texture_records(section, struct.unpack_from("<H", header, 16)[0])
    report = dict(source=SOURCE, textures=[])
    for slot, (name, _, payload) in enumerate(records):
        if name not in SLOTS:
            raise ValueError("Unexpected Flexmobile texture slot")
        bmp = output / (name + ".bmp")
        png = output / (name + ".png")
        if bmp.is_symlink() or png.is_symlink():
            raise ValueError("Refusing original Flexmobile extraction through symbolic links")
        bmp.write_bytes(payload)
        subprocess.run([_converter(), str(bmp), str(png)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        width, height, bits = images.bmp_dimensions(payload)
        report["textures"].append(dict(slot=slot, name=name, bmp_crc32=images.crc32(payload), png_crc32=images.crc32(png.read_bytes()),
                                       width=width, height=height, bits=bits))
    (output / "source.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def finalize(verify=False):
    from flexmobile_geometry import generate
    return images.finalize_artwork(_entry(), generate, verify=verify)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    extraction = commands.add_parser("extract")
    extraction.add_argument("--output", type=Path)
    finalization = commands.add_parser("finalize")
    finalization.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    output = args.output if args.command == "extract" else None
    if args.command == "extract" and output is None:
        output = PROJECT / "build/flexmobile-source-v09"
    print(json.dumps(extract(output) if args.command == "extract" else finalize(args.verify), indent=2))


if __name__ == "__main__":
    main()
