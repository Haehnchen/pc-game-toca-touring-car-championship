#!/usr/bin/env python3
"""Import reviewed Imagegen outputs; crop/encode only, never upscale game art."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_dir', type=Path, help='Built-in Imagegen output directory')
    parser.add_argument('--recipe', type=Path, help='Source/crop recipe (defaults to scenery textures)')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent / 'assets/models/mega'
    recipe_path = args.recipe or root / 'texture-generation.json'
    recipe = json.loads(recipe_path.read_text())
    converter = shutil.which('magick') or shutil.which('convert')
    if converter is None:
        parser.error('ImageMagick with WebP support is required')
    for source in recipe['sources']:
        path = args.source_dir / source['file']
        checksum = f'{zlib.crc32(path.read_bytes()):08x}'
        if source.get('crc32', checksum) != checksum:
            raise ValueError(f'Generated source CRC32 differs: {path.name}')
        source['crc32'] = checksum
    sources = {entry['id']: args.source_dir / entry['file'] for entry in recipe['sources']}
    for entry in recipe['outputs']:
        quality = entry.get('quality', recipe.get('webp_quality', recipe.get('quality', 70)))
        destination = root / entry['image']
        destination.parent.mkdir(parents=True, exist_ok=True)
        out_width, out_height = entry['size']
        rows = entry.get('rows', [{'crop': entry.get('crop'), 'size': entry['size']}])
        if max(out_width, out_height) > 512 or sum(row['size'][1] for row in rows) != out_height:
            raise ValueError('Generated material must be bounded at512px with matching atlas rows')
        command = [converter]
        roll = entry.get('roll', [0, 0])
        if not isinstance(roll, list) or len(roll) != 2 or any(type(value) is not int for value in roll):
            raise ValueError('Generated UV offset requires two integer pixel coordinates')
        for row in rows:
            width, height, left, top = row['crop']
            row_width, row_height = row['size']
            if row_width != out_width or row_width > width or row_height > height:
                raise ValueError('Generated material rows must only be downsampled')
            command.extend(['(', str(sources[entry['source']])])
            if any(roll):
                command.extend(['-roll', f'{roll[0]:+d}{roll[1]:+d}'])
            command.extend(['-crop',
                            f'{width}x{height}+{left}+{top}', '+repage', '-resize',
                            f'{row_width}x{row_height}!', ')'])
        alpha = entry.get('alpha', 'preserve')
        if alpha not in ('preserve', 'opaque'):
            raise ValueError('Generated material alpha must be preserve or opaque')
        command += ['-append']
        if alpha == 'opaque':
            command += ['-alpha', 'off']
        subprocess.run(command + ['-strip', '-define',
                        'webp:lossless=false', '-define', 'webp:alpha-quality=100',
                        '-define', 'webp:method=6', '-quality', str(quality), str(destination)], check=True)
        data = destination.read_bytes()
        if len(data) > 256 * 1024:
            raise ValueError(f'Generated material exceeds256KiB: {destination.name}')
        entry['crc32'] = f'{zlib.crc32(data):08x}'
        entry['bytes'] = len(data)
        entry['quality'] = quality
    recipe_path.write_text(json.dumps(recipe, indent=2) + '\n')
    print(f"Imported {len(recipe['outputs'])} authored WebP textures")


if __name__ == '__main__':
    main()
