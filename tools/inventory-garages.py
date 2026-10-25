#!/usr/bin/env python3
"""Inventory canonical 1.1a garage strips; keep original preview pixels in build/."""

import argparse
from io import BytesIO
import json
from pathlib import Path
import runpy
import struct
import zlib

PROJECT = Path(__file__).resolve().parent.parent
REFERENCE = PROJECT / "references/toca-1.1a"
OUTPUT = PROJECT / "assets/ui/mega/garage/inventory.json"
STARTS = (0, 7, 14, 20, 27, 33, 40, 46, 53, 59)
ARCHIVE_CRCS = ("a46a2742", "1c11b91c", "2037bf45", "ed804971")
READ_BFP = runpy.run_path(str(PROJECT / "tools/extract-assets.py"))["read_bfp"]


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def bitmap(payload):
    if payload[:2] != b"BM" or len(payload) < 1078:
        raise ValueError("Expected complete indexed BMP garage strip")
    pixel_start, header = struct.unpack_from("<II", payload, 10)
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", payload, 18)
    if (header, width, height, planes, bits, compression) != (40, 64, 360, 1, 8, 0):
        raise ValueError("Expected uncompressed bottom-up 64x360 INDEX8 garage strip")
    palette_bgra = payload[54:1078]
    palette_rgb = bytes(channel for index in range(256)
                        for channel in palette_bgra[index * 4:index * 4 + 3][::-1])
    indices = payload[pixel_start:pixel_start + 64 * 360]
    if len(indices) != 64 * 360:
        raise ValueError("Truncated garage pixel array")
    return palette_rgb, indices


def inventory():
    views, payloads = [], {}
    for variant, expected in enumerate(ARCHIVE_CRCS, 1):
        data = (REFERENCE / f"fe/images/gar{variant}.bfp").read_bytes()
        if crc32(data) != expected:
            raise ValueError("Canonical garage archive differs")
        entries = READ_BFP(data)
        for car in range(10) if variant == 1 else range(7, 10):
            first = STARTS[car] - (46 if variant != 1 else 0)
            indices = list(range(first, first + 9))
            for index in indices:
                _, _, offset, size = entries[index]
                payload = data[offset:offset + size]
                bitmap(payload)
                payloads[variant, index] = payload
            views.append({"key": f"gar{variant}-car{car}", "archive_variant": variant,
                          "car_id": car, "member_indices": indices})
    return {"schema_version": 1, "views": views}, payloads


def encode(result):
    return json.dumps(result, indent=2) + "\n"


def preview(directory, result, payloads):
    from PIL import Image, ImageDraw

    directory = directory.resolve()
    if not directory.is_relative_to((PROJECT / "build").resolve()):
        raise ValueError("Original previews must remain under ignored build/")
    directory.mkdir(parents=True, exist_ok=True)
    frontend = (REFERENCE / "data/frontend.bfp").read_bytes()
    _, _, offset, size = READ_BFP(frontend)[29]
    palette = Image.open(BytesIO(frontend[offset:offset + size])).getpalette()[:768]
    sheet = Image.new("RGB", (576 * 3, 388 * ((len(result["views"]) + 2) // 3)), (32, 32, 32))
    draw = ImageDraw.Draw(sheet)
    for number, view in enumerate(result["views"]):
        canvas = Image.new("P", (576, 360))
        canvas.putpalette(palette)
        for strip, member in enumerate(view["member_indices"]):
            image = Image.open(BytesIO(payloads[view["archive_variant"], member]))
            canvas.paste(image, (strip * 64, 0))
        rgb = canvas.convert("RGB")
        rgb.save(directory / (view["key"] + ".png"))
        x, y = number % 3 * 576, number // 3 * 388
        draw.text((x + 5, y + 7), view["key"], fill="white")
        sheet.paste(rgb, (x, y + 28))
    sheet.save(directory / "contact-sheet.png")
    print(f"Original previews: {directory.relative_to(PROJECT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify the tracked inventory against canonical data")
    parser.add_argument("--preview-dir", type=Path, help="Export 19 opaque original views/contact sheet under build/")
    args = parser.parse_args()
    result, payloads = inventory()
    encoded = encode(result)
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text() != encoded:
            raise SystemExit("Garage inventory differs; regenerate with tools/inventory-garages.py")
        print(f'Garage views: {len(result["views"])}')
    else:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(encoded)
        print(f"Wrote {OUTPUT.relative_to(PROJECT)}")
    if args.preview_dir:
        preview(args.preview_dir, result, payloads)


if __name__ == "__main__":
    main()
