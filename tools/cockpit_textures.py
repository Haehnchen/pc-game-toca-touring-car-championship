#!/usr/bin/env python3
"""Shared cockpit material import and exact final WebP slot replacement."""

import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess

from vehicle_textures import crc32, bmp_dimensions, texture_records, webp_dimensions


SLOTS = {"wheel01": (128, 128), "wiper": (256, 16)}
MAX_EDGE = 512
MAX_BYTES = 256 * 1024


def replace_textures(section, texture_count, directory=None):
    """Preserve canonical names/order and embed authored WebP bytes unchanged.

    Both common materials live in the same directory; each source model keeps
    its own original slot. Final alpha is never refitted to the old bitmap.
    """
    if directory is None:
        return section
    records = texture_records(section, texture_count)
    directory = Path(directory).resolve(strict=True)
    if not directory.is_dir():
        raise ValueError("Cockpit artwork requires a directory")
    sources = {}
    for path in directory.iterdir():
        if path.suffix.lower() not in (".bmp", ".png", ".webp"):
            continue
        if path.name not in {name + ".webp" for name in SLOTS}:
            raise ValueError("Cockpit artwork requires known final WebP slot names")
        resolved = path.resolve(strict=True)
        if not resolved.is_relative_to(directory) or not resolved.is_file():
            raise ValueError("Cockpit artwork escapes its directory")
        if resolved.stat().st_size > MAX_BYTES:
            raise ValueError("Cockpit artwork exceeds256KiB")
        payload = resolved.read_bytes()
        width, height = webp_dimensions(payload)
        original_width, original_height = SLOTS[path.stem]
        if max(width, height) > MAX_EDGE or width * original_height != height * original_width:
            raise ValueError("Cockpit artwork must preserve its UV aspect within512px")
        sources[path.stem] = payload
    result = bytearray()
    for name, raw_name, original in records:
        if name not in SLOTS or bmp_dimensions(original)[:2] != SLOTS[name]:
            raise ValueError("Unexpected canonical shared cockpit material")
        payload = sources.get(name, original)
        result.extend(raw_name + struct.pack("<I", len(payload)) + payload)
    return bytes(result)


apply_textures = replace_textures


def import_artwork(source, destination, crop, size, *, quality=70):
    """Crop/downsample an actual generated source and retain its alpha at WebP70."""
    converter = shutil.which("magick") or shutil.which("convert")
    if converter is None:
        raise ValueError("ImageMagick with WebP support is required")
    if (not isinstance(crop, list) or len(crop) != 4 or
            not isinstance(size, list) or len(size) != 2 or
            any(type(value) is not int for value in crop + size)):
        raise ValueError("Artwork crop/size require integer pixel coordinates")
    width, height, left, top = crop
    out_width, out_height = size
    dimensions = subprocess.run([converter, str(source), "-format", "%w %h", "info:"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
    source_width, source_height = map(int, dimensions.split())
    if (min(width, height, out_width, out_height) < 1 or min(left, top) < 0 or
            max(size) > MAX_EDGE or out_width > width or out_height > height or
            width * out_height != height * out_width or
            left + width > source_width or top + height > source_height):
        raise ValueError("Cockpit import permits bounded crop/downsampling with unchanged aspect only")
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([converter, str(source), "-crop", f"{width}x{height}+{left}+{top}",
                    "+repage", "-filter", "Lanczos", "-resize", f"{out_width}x{out_height}!",
                    "-strip", "-define", "webp:lossless=false", "-define", "webp:alpha-quality=100",
                    "-define", "webp:method=6", "-quality", str(quality), str(destination)],
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    payload = destination.read_bytes()
    if webp_dimensions(payload) != tuple(size) or len(payload) > MAX_BYTES:
        raise ValueError("Imported cockpit artwork exceeds its size budget")
    return {"crc32": crc32(payload), "bytes": len(payload), "size": size}


def import_generation(manifest, source_root=None):
    manifest = Path(manifest)
    document = json.loads(manifest.read_text())
    quality = document.get("webp_quality", 70)
    sources = {entry["id"]: entry for entry in document["sources"]}
    for entry in document["outputs"]:
        source = sources[entry["source"]]
        path = Path(source["generated_file"])
        if source_root is not None:
            path = Path(source_root) / path.name
        if crc32(path.read_bytes()) != source["crc32"]:
            raise ValueError("Generated cockpit source CRC32 differs")
        destination = manifest.parent / entry["image"]
        if not destination.resolve().is_relative_to(manifest.parent.resolve()):
            raise ValueError("Cockpit output escapes its package")
        import_artwork(path, destination, entry["crop"], entry["size"], quality=quality)
    return document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    document = import_generation(args.manifest, args.source_root)
    print(f"Imported {len(document['outputs'])} shared cockpit WebP{document['webp_quality']} materials")


if __name__ == "__main__":
    main()
