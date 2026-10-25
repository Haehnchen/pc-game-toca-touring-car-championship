"""Reusable catalog, WebP sprite exports and contact sheet for generated HUD art.

Inputs are the newly generated target-resolution atlases. This module never
reads original game bitmaps and does not alter runtime layout or game values.
"""

from collections import defaultdict
import json
from pathlib import Path
import zlib

from PIL import Image, ImageDraw, ImageFont


RPM_POSITIONS = [(453, 457), (457, 449), (465, 443), (472, 438), (482, 435), (492, 433),
                 (501, 431), (511, 429), (522, 428), (532, 427), (542, 427), (551, 427)]


def sprite_catalog(large, small_x, small_width, rpm_rects, factor):
    """Return named source rectangles in output pixels and logical sprite sizes.

    The inventory is the glyphs/sprites actually authored by build-hud-assets.py,
    including original punctuation. It does not invent an alphabet or substitute
    a font. All positions retain the canonical atlas coordinate system.
    """
    result = {}
    def add(name, atlas, rectangle, group, destination=None):
        x, y, width, height = rectangle
        item = {"atlas": atlas, "source": [round(value * factor) for value in rectangle],
                "logical_size": [width, height], "group": group}
        if destination is not None:
            item["destination"] = list(destination)
        result[name] = item
    for color, atlas, characters, offset, group in [
        ("cyan", "primary-km.webp", "0123456789R", 0, "Large cyan numerals"),
        ("red", "secondary.webp", "0123456789R", 0, "Large red numerals"),
        ("yellow", "secondary.webp", "0123456789RN", 106, "Large yellow numerals and gears"),
    ]:
        for index, character in enumerate(characters):
            x, y, width, height = large[index]
            add(f"large-{color}-{character}", atlas, (x, y + offset, width, height), group)
    characters = [(str(i), str(i)) for i in range(10)] + [(":", "colon"), ("-", "dash"), ("+", "plus"), (".", "dot")]
    for color, y in [("red", 106), ("yellow", 118)]:
        for index, (_, name) in enumerate(characters):
            add(f"small-{color}-{name}", "primary-km.webp", (small_x[index], y, small_width[index], 12), f"Small {color} numerals and punctuation")
    for name, atlas, rectangle in [
        ("TIME", "primary-km.webp", (178, 186, 72, 12)),
        ("BEST", "primary-km.webp", (178, 200, 72, 11)),
        ("SPLIT", "primary-km.webp", (1, 132, 75, 11)),
        ("LAP", "primary-km.webp", (128, 212, 58, 15)),
        ("POS", "primary-km.webp", (2, 243, 59, 12)),
        ("Gear", "primary-km.webp", (48, 191, 48, 9)),
        ("KpH", "primary-km.webp", (48, 201, 48, 9)),
        ("MpH", "primary-miles.webp", (48, 201, 48, 9)),
        ("lap-separator", "primary-km.webp", (128, 227, 9, 13)),
    ]:
        add(f"label-{name}", atlas, rectangle, "Original labels and units")
    for index, (rectangle, position) in enumerate(zip(rpm_rects, RPM_POSITIONS)):
        add(f"rpm-{index + 1:02d}", "primary-km.webp", rectangle, "Tachometer active sectors", position)
    add("rpm-background", "primary-km.webp", (123, 144, 116, 41), "HUD backing sprites", (452, 427))
    for name, atlas, rectangle in [
        ("speed-km", "primary-km.webp", (15, 190, 150, 20)),
        ("speed-miles", "primary-miles.webp", (15, 190, 150, 20)),
        ("lap", "primary-km.webp", (0, 212, 186, 28)),
        ("position", "primary-km.webp", (0, 241, 187, 15)),
        ("timing-labels", "primary-km.webp", (178, 186, 72, 25)),
    ]:
        add(f"panel-{name}", atlas, rectangle, "HUD backing sprites")
    return result


def _crop(images, name, entry):
    image = images[entry["atlas"]]
    x, y, width, height = entry["source"]
    if min(x, y) < 0 or min(width, height) <= 0 or x + width > image.width or y + height > image.height:
        raise ValueError(f"Generated sprite {name} lies outside {entry['atlas']}")
    return image.crop((x, y, x + width, y + height)).convert("RGBA")


def export_sprites(images_by_filename, catalog, output_dir):
    """Export target-resolution WebP70 sprites and a CRC32/source manifest."""
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=True)
    records = {}
    for name, entry in catalog.items():
        if entry["atlas"] not in images_by_filename:
            continue
        sprite = _crop(images_by_filename, name, entry)
        filename = f"{name}.webp"
        path = output / filename
        temporary = path.with_suffix(".webp.tmp")
        sprite.save(temporary, format="WEBP", quality=70, alpha_quality=100, method=6, exact=True)
        temporary.replace(path)
        data = path.read_bytes()
        records[name] = {**entry, "file": filename, "size": list(sprite.size), "bytes": len(data), "crc32": f"{zlib.crc32(data):08x}"}
    manifest = {"schema": 1, "encoding": "WebP quality70 with alpha quality100", "coordinate_space": "output atlas pixels", "sprites": records}
    temporary = output / "manifest.json.tmp"
    temporary.write_text(json.dumps(manifest, indent=2) + "\n")
    temporary.replace(output / "manifest.json")
    return records


def _checker(size):
    result = Image.new("RGB", size, (38, 42, 48))
    draw = ImageDraw.Draw(result)
    for y in range(0, size[1], 12):
        for x in range(0, size[0], 12):
            if (x // 12 + y // 12) % 2:
                draw.rectangle((x, y, x + 11, y + 11), fill=(48, 52, 58))
    return result


def _rpm_preview(images, catalog, count):
    background = catalog["rpm-background"]
    result = _crop(images, "rpm-background", background)
    scale = result.width / background["logical_size"][0]
    x, y = background["destination"]
    for index in range(count):
        name = f"rpm-{index + 1:02d}"
        entry = catalog[name]
        sprite = _crop(images, name, entry)
        dx, dy = entry["destination"]
        result.alpha_composite(sprite, (round((dx - x) * scale), round((dy - y) * scale)))
    return result


def save_preview(images_by_filename, catalog, output_path):
    """Save a labelled inventory plus idle/partial/full exact-sprite assemblies.

    Pass decoded WebP images to show the actual encoded game assets. Thumbnails
    preserve aspect ratio; the three tachometer assemblies use their native4x
    size rather than recomputed presentation positions.
    """
    groups = defaultdict(list)
    for name, entry in catalog.items():
        if entry["atlas"] in images_by_filename:
            groups[entry["group"]].append((name, entry))
    columns, tile_width, tile_height = 6, 184, 136
    width = columns * tile_width + 32
    height = 70 + sum(34 + ((len(entries) + columns - 1) // columns) * tile_height + 16 for entries in groups.values())
    gauges = "rpm-background" in catalog and catalog["rpm-background"]["atlas"] in images_by_filename
    if gauges:
        height += 34 + 3 * 194
    result = Image.new("RGB", (width, height), (23, 28, 34))
    draw = ImageDraw.Draw(result)
    font = ImageFont.load_default(size=14)
    title = ImageFont.load_default(size=18)
    draw.text((16, 14), "Generated HUD artwork - original layout and complete authored sprite inventory", fill=(245, 245, 245), font=title)
    draw.text((16, 42), "All source rectangles are target-resolution atlas pixels. Checkerboard shows transparency.", fill=(175, 185, 195), font=font)
    top = 70
    for group, entries in groups.items():
        draw.text((16, top), group, fill=(245, 245, 245), font=title)
        top += 34
        for index, (name, entry) in enumerate(entries):
            left = 16 + (index % columns) * tile_width
            y = top + (index // columns) * tile_height
            card = _checker((tile_width - 10, 94))
            sprite = _crop(images_by_filename, name, entry)
            sprite.thumbnail((tile_width - 20, 84), Image.Resampling.LANCZOS)
            card.paste(sprite, ((card.width - sprite.width) // 2, (card.height - sprite.height) // 2), sprite)
            result.paste(card, (left, y))
            draw.text((left, y + 98), name, fill=(230, 235, 240), font=font)
            width_text, height_text = entry["logical_size"]
            draw.text((left, y + 117), f"{width_text} x {height_text} logical pixels", fill=(145, 160, 175), font=font)
        top += ((len(entries) + columns - 1) // columns) * tile_height + 16
    if gauges:
        draw.text((16, top), "Tachometer assembly from the exact original sprite destinations", fill=(245, 245, 245), font=title)
        top += 34
        for count, label in [(0, "Idle - 0 illuminated"), (6, "Partial - 6 illuminated"), (12, "Full - 12 illuminated")]:
            sprite = _rpm_preview(images_by_filename, catalog, count)
            backdrop = Image.new("RGB", sprite.size, (105, 75, 48))
            backdrop.paste(sprite, (0, 0), sprite)
            result.paste(backdrop, (250, top))
            draw.text((16, top + 18), label, fill=(230, 235, 240), font=font)
            top += 194
    path = Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() == ".webp":
        result.save(path, format="WEBP", quality=70, method=6)
    else:
        result.save(path, format="PNG")
    return {"file": str(path), "size": list(result.size), "bytes": path.stat().st_size,
            "crc32": f"{zlib.crc32(path.read_bytes()):08x}", "sprites": sum(len(entries) for entries in groups.values())}
