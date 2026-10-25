#!/usr/bin/env python3
"""Manually import the 19 approved canonical garage views; never run by builds."""

import argparse
from io import BytesIO
import json
from pathlib import Path
import runpy
import zlib

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
INVENTORY = runpy.run_path(str(ROOT / "tools/inventory-garages.py"))
READ_BFP = INVENTORY["READ_BFP"]
BITMAP = INVENTORY["bitmap"]
STARTS = INVENTORY["STARTS"]
ARCHIVE_CRCS = INVENTORY["ARCHIVE_CRCS"]
EXPECTED = {f"gar1-car{car}" for car in range(10)} | {
    f"gar{variant}-car{car}" for variant in (2, 3, 4) for car in (7, 8, 9)}


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def checked(path, checksum):
    data = path.read_bytes()
    if crc32(data) != checksum:
        raise ValueError(f"Canonical CRC32 differs: {path}")
    return data


def member(data, index):
    _, _, offset, size = READ_BFP(data)[index]
    return data[offset:offset + size]


def original_view(strips, palette):
    if len(strips) != 9 or len(palette) != 768:
        raise ValueError("Original garage requires nine strips and the active RGB256 palette")
    indices = [BITMAP(strip)[1] for strip in strips]
    # Raw strip indices are bottom-up. Never convert through their stored palettes:
    # the original frontend displays every strip with its active Audi palette.
    pixels = b"".join(strip[row * 64:row * 64 + 64] for row in range(359, -1, -1) for strip in indices)
    canvas = Image.frombytes("P", (576, 360), pixels)
    canvas.putpalette(palette)
    return canvas.crop((1, 0, 574, 357)).convert("RGB")


def import_garages(reference, output, inventory_path):
    reference, output = Path(reference).resolve(), Path(output).resolve()
    canonical = (ROOT / "references").resolve()
    if (output.is_relative_to(reference) or reference.is_relative_to(output)
            or output.is_relative_to(canonical) or canonical.is_relative_to(output)):
        raise ValueError("Original garage output must remain outside reference directories")
    inventory_bytes = Path(inventory_path).read_bytes()
    inventory = json.loads(inventory_bytes)
    views = inventory["views"]
    if len(views) != 19 or {view["key"] for view in views} != EXPECTED:
        raise ValueError("Original garage inventory must cover all 19 unique mappings")
    frontend = checked(reference / "data/frontend.bfp", "a014e5af")
    active = member(frontend, 29)
    palette = bytes(channel for index in range(256) for channel in active[54 + index * 4:57 + index * 4][::-1])
    archives = {variant: checked(reference / f"fe/images/gar{variant}.bfp", checksum)
                for variant, checksum in enumerate(ARCHIVE_CRCS, 1)}
    encoded = {}
    for view in views:
        variant, car = view["archive_variant"], view["car_id"]
        if view["key"] != f"gar{variant}-car{car}" or view["key"] not in EXPECTED:
            raise ValueError("Original garage key differs from its variant/car mapping")
        first = STARTS[car] - (46 if variant != 1 else 0)
        indices = list(range(first, first + 9))
        if view["member_indices"] != indices:
            raise ValueError("Original garage inventory has changed strip indices")
        image = original_view([member(archives[variant], index) for index in indices], palette)
        stream = BytesIO()
        image.save(stream, "WEBP", quality=70, method=6)
        payload = stream.getvalue()
        filename = view["key"] + ".webp"
        encoded[filename] = payload
    # Validate and encode all views before publishing; only these named outputs change.
    for name in encoded:
        destination = output / name
        if destination.is_symlink() or (destination.exists() and not destination.is_file()):
            raise ValueError(f"Original garage output must be a regular file: {destination}")
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in encoded.items():
        (output / name).write_bytes(payload)
    return list(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-dir", type=Path, default=ROOT / "references/toca-1.1a")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets/ui/original/garage")
    parser.add_argument("--inventory", type=Path, default=ROOT / "assets/ui/mega/garage/inventory.json")
    args = parser.parse_args()
    files = import_garages(args.reference_dir, args.output_dir, args.inventory)
    print(f'Original garages: {len(files)} opaque 573x357 WebP70/method6 files')


if __name__ == "__main__":
    main()
