#!/usr/bin/env python3
"""Manually publish finished garage WebPs from the recorded source layers.

Normal builds copy the finished files in assets/ui/mega/garage and never invoke this tool.
"""

import argparse
from io import BytesIO
import json
from pathlib import Path
import re
import tempfile

from PIL import Image

PROJECT = Path(__file__).resolve().parent.parent
CANVAS = [1146, 714]
MANAGED = re.compile(r"gar[1-4]-car[0-9]\.webp")


def write_changed(path, data):
    if path.is_file() and path.read_bytes() == data:
        return False
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".garage-", delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
            stream.close()
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    return True


def layer(source, entry, kind, canvas=CANVAS):
    name = entry.get(kind)
    if not isinstance(name, str) or not name or Path(name).is_absolute():
        raise ValueError(f"{kind} must name a relative WebP layer")
    path = (source / name).resolve()
    if not path.is_relative_to(source) or path.suffix.lower() != ".webp":
        raise ValueError(f"{kind} layer must remain under source-dir and use WebP")
    if not path.is_file():
        raise ValueError(f"Missing {kind} layer: {name}")
    encoded = path.read_bytes()
    with Image.open(BytesIO(encoded)) as image:
        if image.format != "WEBP" or list(image.size) != canvas:
            raise ValueError(f"{kind} must be a {canvas[0]}x{canvas[1]} WebP canvas")
        rgba = image.convert("RGBA")
    minimum, maximum = rgba.getchannel("A").getextrema()
    if kind == "background" and (minimum, maximum) != (255, 255):
        raise ValueError("background must be opaque")
    if kind == "foreground":
        if entry.get("foreground_empty", False):
            if (minimum, maximum) != (0, 0):
                raise ValueError("foreground_empty requires an entirely transparent foreground")
        elif minimum != 0 or maximum == 0:
            raise ValueError("foreground requires visible artwork and a transparent background")
    if kind == "shadow" and (minimum != 0 or not 0 < maximum < 255):
        raise ValueError("shadow requires a transparent background and translucent artwork")
    return rgba


def contact_shadows(stamp, index, entry):
    """Place the shared footprint using the published renders and panorama crop."""
    result = Image.new("RGBA", tuple(CANVAS))
    record = index["entries"][entry["key"]]
    if entry.get("foreground_empty", False):
        return result
    crop_x, crop_y = index["crop"][:2]
    placements = [(f'car-{vehicle["car_id"]}', vehicle["x_shift"], False)
                  for vehicle in record["vehicles"]]
    for name, car in (("audi", 1), ("renault", 7)):
        shift = (index["panorama_starts"][car] - index["panorama_starts"][entry["car_id"]]) * index["strip_width"]
        placements.append(("interior-" + name, shift, True))
    for name, shift, interior in placements:
        left, top, right, bottom = index["renders"][name]["alpha_bbox_xyxy"]
        width, height = right - left, bottom - top
        # Expose a short rim beside the tires/front, including the original's
        # slight rightward spread; a footprint entirely under the car disappears.
        depth = round(height * (0.16 if interior else 0.50))
        margin = max(4, round(width * 0.04))
        size = (width + margin * 4, depth + margin * 2)
        position = (left + shift - crop_x - margin * 2 + round(width * 0.06),
                    bottom - crop_y - depth - margin + round(width * 0.025))
        if position[0] >= CANVAS[0] or position[0] + size[0] <= 0:
            continue
        result.alpha_composite(stamp.resize(size, Image.Resampling.LANCZOS), position)
    return result


def compose(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    references = (PROJECT / "references").resolve()
    if source.is_relative_to(references) or output.is_relative_to(references) or references.is_relative_to(output):
        raise ValueError("Garage composition must not read from or write into references")
    if source != output and (source.is_relative_to(output) or output.is_relative_to(source)):
        raise ValueError("output-dir must be source-dir itself or a separate directory")
    manifest = json.loads((source / "generation.json").read_text())
    if type(manifest.get("schema_version")) is not int or manifest["schema_version"] != 1 or manifest.get("canvas") != CANVAS:
        raise ValueError("Expected garage schema_version 1 and canvas [1146, 714]")
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        raise ValueError("Garage manifest entries must be a list")
    shadow = manifest.get("contact_shadow")
    if shadow is not None:
        stamp = layer(source, shadow, "shadow", [512, 256])
        index_path = (source / manifest["local_render"]["index"]).resolve()
        if not index_path.is_relative_to(source):
            raise ValueError("Shadow vehicle index must remain under source-dir")
        vehicle_index = json.loads(index_path.read_text())
    prepared = {}
    # Validate every activated pair before changing any output or removing stale artwork.
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("Garage entry must be an object")
        variant, car = entry.get("variant"), entry.get("car_id")
        if type(variant) is not int or type(car) is not int or variant not in range(1, 5) or car not in range(10) or (variant != 1 and car < 7):
            raise ValueError("Garage variant/car_id mapping is outside the original roster")
        key = entry.get("key")
        if key != f"gar{variant}-car{car}" or key in prepared:
            raise ValueError("Garage key must match its unique variant/car_id mapping")
        empty = entry.get("foreground_empty", False)
        if type(empty) is not bool or (empty and (variant, car) not in ((1, 8), (3, 8), (1, 9), (2, 9))):
            raise ValueError("foreground_empty is allowed only for explicitly locked bonus-car mappings")
        for kind in ("background", "foreground"):
            if isinstance(entry.get(kind), str):
                path = (source / entry[kind]).resolve()
                if path.parent == output and MANAGED.fullmatch(path.name):
                    raise ValueError("source layers must not use managed output filenames")
        background, foreground = layer(source, entry, "background"), layer(source, entry, "foreground")
        if shadow is not None:
            background = Image.alpha_composite(background, contact_shadows(stamp, vehicle_index, entry))
        prepared[key] = (background, foreground)
    encoded_outputs = {}
    for key, (background, foreground) in sorted(prepared.items()):
        filename = key + ".webp"
        composite = Image.alpha_composite(background, foreground).convert("RGB")
        stream = BytesIO()
        composite.save(stream, "WEBP", quality=70, method=6)
        encoded = stream.getvalue()
        encoded_outputs[filename] = encoded
    output.mkdir(parents=True, exist_ok=True)
    written = sum(write_changed(output / name, data) for name, data in encoded_outputs.items())
    removed = 0
    for path in output.iterdir():
        if MANAGED.fullmatch(path.name) and path.name not in encoded_outputs and (path.is_file() or path.is_symlink()):
            path.unlink()
            removed += 1
    return {"active": len(encoded_outputs), "written": written, "removed": removed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=PROJECT / "assets/ui/mega/garage")
    parser.add_argument("--output-dir", type=Path, help="defaults to source-dir; only finished garN-carN.webp files are managed")
    args = parser.parse_args()
    result = compose(args.source_dir, args.output_dir or args.source_dir)
    print(f'Garage composites: {result["active"]} active, {result["written"]} written, {result["removed"]} removed')


if __name__ == "__main__":
    main()
