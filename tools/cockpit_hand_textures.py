"""Strict per-car Mega glove/forearm WebP slot replacement.

The original names, order and UV aspects belong to each source CM3D. Flex has
two square leather swatches where the other cars use a tall palm chart.
"""

import argparse
import io
import json
from pathlib import Path
import shutil
import struct
import tempfile

from cockpit_textures import import_artwork
from vehicle_model import PROJECT, decode, read_source
from vehicle_textures import bmp_dimensions, crc32, texture_records, webp_dimensions


SLOTS = ("ARM", "GLOVE03", "GLOVE01", "GLOVE02")
MAX_EDGE = 512
MAX_BYTES = 256 * 1024


def replace_textures(section, texture_count, directory=None):
    """Embed exactly four authored WebPs unchanged, including their alpha.

    A supplied directory is a complete per-car set. No partial replacement can
    silently mix another car's materials into this car's source charts.
    """
    if directory is None:
        return section
    records = texture_records(section, texture_count)
    if tuple(record[0] for record in records) != SLOTS:
        raise ValueError("Unexpected canonical hand texture slots/order")
    directory = Path(directory).resolve(strict=True)
    if not directory.is_dir():
        raise ValueError("Hand artwork requires a directory")
    if {path.name for path in directory.iterdir()} != {name + ".webp" for name in SLOTS}:
        raise ValueError("Hand artwork requires exactly four known final WebP slots")
    result = bytearray()
    for name, raw_name, original in records:
        path = (directory / (name + ".webp")).resolve(strict=True)
        if not path.is_relative_to(directory) or not path.is_file():
            raise ValueError("Hand artwork escapes its directory")
        if path.stat().st_size > MAX_BYTES:
            raise ValueError("Hand artwork exceeds256KiB")
        payload = path.read_bytes()
        width, height = webp_dimensions(payload)
        original_width, original_height, _ = bmp_dimensions(original)
        if max(width, height) > MAX_EDGE or width * original_height != height * original_width:
            raise ValueError("Hand artwork must preserve its original UV aspect within512px")
        result.extend(raw_name + struct.pack("<I", len(payload)) + payload)
    return bytes(result)


apply_textures = replace_textures


def _canonical_textures(car):
    """Verify both recorded members before using their common texture section."""
    sections = []
    for member in car["members"]:
        header, _, _, section = decode(read_source({**car, **member}))
        if crc32(section) != member["texture_section_crc32"]:
            raise ValueError("Canonical hand texture section CRC32 differs")
        if struct.unpack_from("<H", header, 16)[0] != len(SLOTS):
            raise ValueError("Unexpected canonical hand texture count")
        sections.append(section)
    if len(sections) != 2 or sections[0] != sections[1]:
        raise ValueError("Canonical left/right hand textures must match")
    records = texture_records(sections[0], len(SLOTS))
    if tuple(record[0] for record in records) != SLOTS:
        raise ValueError("Unexpected canonical hand texture slots/order")
    return {name: payload for name, _, payload in records}


def _import_colorkey_artwork(source, destination, crop, size, original, working, *, quality=70):
    """Repeat the accepted Flex crop/filter and original exact-black UV mask.

    Pillow's Lanczos RGB downsample precedes the nearest-sampled canonical mask.
    The shared ImageMagick encoder uses the generation's recorded WebP quality.
    Pillow is only needed by the artwork import command, never model builds.
    """
    from PIL import Image

    width, height, left, top = crop
    with Image.open(source) as generated:
        if (min(width, height, *size) < 1 or min(left, top) < 0 or
                left + width > generated.width or top + height > generated.height or
                size[0] > width or size[1] > height or max(size) > MAX_EDGE or
                width * size[1] != height * size[0]):
            raise ValueError("Hand colorkey import requires bounded proportional downsampling")
        image = generated.crop((left, top, left + width, top + height))
        image = image.resize(tuple(size), Image.Resampling.LANCZOS).convert("RGBA")
    with Image.open(io.BytesIO(original)) as bitmap:
        bitmap = bitmap.convert("RGB")
        mask = Image.new("L", bitmap.size)
        mask.putdata([0 if pixel == (0, 0, 0) else 255 for pixel in bitmap.get_flattened_data()])
    image.putalpha(mask.resize(image.size, Image.Resampling.NEAREST))
    prepared = working / "colorkey.png"
    image.save(prepared)
    return import_artwork(prepared, destination, [*size, 0, 0], size, quality=quality)


def import_generation(manifest, source_root=None, output_directory=None):
    """Import recorded crops and alpha operations from <car>/generated.png."""
    manifest = Path(manifest).resolve(strict=True)
    document = json.loads(manifest.read_text())
    quality = document.get("webp_quality", 70)
    if document["version"] != 1 or quality not in (70, 80):
        raise ValueError("Unknown hand artwork generation format")
    sources = {}
    for source in document["sources"]:
        recorded = Path(source["generated_file"])
        if recorded.is_absolute() or ".." in recorded.parts:
            raise ValueError("Generated hand source must be project-relative")
        path = PROJECT / recorded if source_root is None else Path(source_root) / recorded.parent.name / recorded.name
        if crc32(path.read_bytes()) != source["crc32"]:
            raise ValueError(f"Generated hand source CRC32 differs: {source['id']}")
        if source["id"] in sources:
            raise ValueError("Duplicate generated hand source identity")
        sources[source["id"]] = path
    destination_root = (manifest.parent if output_directory is None else Path(output_directory)).resolve()
    staged_outputs = []
    (PROJECT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cockpit-hand-import-", dir=PROJECT / "build") as temporary:
        working = Path(temporary)
        for car in document["cars"]:
            originals = _canonical_textures(car)
            if tuple(output["slot"] for output in car["outputs"]) != SLOTS:
                raise ValueError("Hand artwork import requires all four ordered slots")
            for output in car["outputs"]:
                original = originals[output["slot"]]
                if crc32(original) != output["original_crc32"]:
                    raise ValueError("Canonical hand bitmap CRC32 differs")
                shared = output.get("shared_original_slot")
                if shared is not None and originals[shared] != original:
                    raise ValueError("Shared hand artwork requires byte-identical originals")
                destination = destination_root / output["image"]
                if not destination.resolve().is_relative_to(destination_root):
                    raise ValueError("Hand artwork output escapes its package")
                staged = working / output["image"]
                source = sources[output["source"]]
                if "alpha" in output:
                    if (car["car_id"], output["slot"]) != (9, "GLOVE01"):
                        raise ValueError("Unexpected canonical hand colorkey operation")
                    _import_colorkey_artwork(source, staged, output["crop"], output["size"], original, working, quality=quality)
                else:
                    import_artwork(source, staged, output["crop"], output["size"], quality=quality)
                staged_outputs.append((staged, destination))
        for staged, destination in staged_outputs:
            if destination.exists() and destination.read_bytes() == staged.read_bytes():
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(staged, destination)
    return document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--source-root", type=Path, help="raw source root containing <car>/generated.png")
    parser.add_argument("--output-dir", type=Path, help="alternate package root for verification")
    args = parser.parse_args()
    document = import_generation(args.manifest, args.source_root, args.output_dir)
    print(f"Verified/imported {sum(len(car['outputs']) for car in document['cars'])} hand WebP{document.get('webp_quality', 70)} materials")


if __name__ == "__main__":
    main()
