#!/usr/bin/env python3
"""Export selected canonical Ground references into ignored build/ for artwork review."""

import argparse
import csv
import importlib.util
import io
import json
from pathlib import Path
import re
import struct

from PIL import Image, ImageDraw

from git_game_data import archive_bytes, configured_data_root


PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ground_reader', PROJECT / 'tools/inventory-scenery.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def export(track, rows, destination):
    selected = [row for row in rows if int(row['track_id']) == track]
    archive = archive_bytes(configured_data_root(), selected[0]['archive'])
    name, raw = reader.archive_members(archive)[1]
    assert name == 'GROUND.C3D'
    assert reader.crc32(archive) == selected[0]['archive_crc32']
    assert reader.crc32(raw) == selected[0]['member_crc32']
    _, _, materials = reader.parse_model(raw)
    slug = re.sub(r'[^a-z0-9]+', '-', selected[0]['track'].lower()).strip('-')
    output = destination / f'{track:02}-{slug}'
    output.mkdir(parents=True, exist_ok=True)
    offset = struct.unpack_from('<I', raw, 28)[0]
    records = []
    sheet = Image.new('RGB', (4 * 280, ((len(materials) + 3) // 4) * 220), '#303030')
    draw = ImageDraw.Draw(sheet)
    for slot, material in enumerate(materials):
        row = selected[slot]
        size = struct.unpack_from('<I', raw, offset + 12)[0]
        bmp = raw[offset + 16:offset + 16 + size]
        offset += 16 + size
        assert int(row['slot']) == slot and row['name'] == material['name']
        assert reader.crc32(bmp) == row['bmp_crc32'] == material['crc32']
        safe = re.sub(r'[^a-z0-9]+', '-', material['name'].lower()).strip('-')
        stem = f'{slot:02}-{safe}-{material["crc32"]}'
        bitmap = output / (stem + '.bmp')
        preview = output / (stem + '.png')
        layout = output / (stem + '-layout.png')
        bitmap.write_bytes(bmp)
        image = Image.open(io.BytesIO(bmp)).convert('RGBA')
        decoded = image.tobytes()
        pixels = [tuple(decoded[index:index + 4]) for index in range(0, len(decoded), 4)]
        black = sum(pixel[:3] == (0, 0, 0) for pixel in pixels)
        if black:
            image.putdata([(*pixel[:3], 0 if pixel[:3] == (0, 0, 0) else 255) for pixel in pixels])
        image.save(preview)
        scale = max(1, 512 // max(image.size))
        image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST).save(layout)
        record = dict(row, source_size=material['size'], black_key_pixels=black,
                      opaque=black == 0, mean_rgb=[round(sum(p[c] for p in pixels) / len(pixels), 3) for c in range(3)],
                      bmp_path=str(bitmap.relative_to(PROJECT)), preview_path=str(preview.relative_to(PROJECT)),
                      preview_crc32=reader.crc32(preview.read_bytes()), layout_path=str(layout.relative_to(PROJECT)),
                      layout_crc32=reader.crc32(layout.read_bytes()))
        records.append(record)
        thumb = image.copy()
        thumb.thumbnail((256, 170), Image.Resampling.NEAREST)
        x, y = (slot % 4) * 280 + 12, (slot // 4) * 220 + 12
        sheet.paste(thumb, (x, y), thumb)
        draw.text((x, y + 174), f'{slot}: {material["name"]} {row["source_size"]} -> {row["planned_size"]}', fill='white')
        draw.text((x, y + 189), f'{material["crc32"]} / alpha: {black}', fill='white')
    sheet.save(output / 'contact-sheet.png')
    (output / 'manifest.json').write_text(json.dumps(dict(track_id=track, track=selected[0]['track'],
        archive=selected[0]['archive'], archive_crc32=selected[0]['archive_crc32'], member_index=1,
        member_name=name, member_crc32=selected[0]['member_crc32'], materials=records), indent=2) + '\n')
    print(f'{track}: {len(records)} materials, {sum(r["black_key_pixels"] > 0 for r in records)} keyed; {output.relative_to(PROJECT)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tracks', nargs='+', type=int, choices=range(10), default=list(range(10)))
    args = parser.parse_args()
    with (PROJECT / 'docs/mega-ground-textures.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    for track in args.tracks:
        export(track, rows, PROJECT / 'build/ground-texture-rollout/reference')


if __name__ == '__main__':
    main()
