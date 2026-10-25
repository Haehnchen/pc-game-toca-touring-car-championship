#!/usr/bin/env python3
"""Reproduce the Tank's source-masked WebP70 from its lossless WebP Imagegen master.

The single TANK slot keeps the canonical 256x256 layout. The 1254x1254 Imagegen
output is reduced with Lanczos to 1024x1024. The canonical exact-black key is
registered as real alpha before WebP70 encoding; generated dark ink stays opaque.
"""

import argparse
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import vehicle_textures as images
from vehicle_model import PROJECT, crc32, decode, read_source

PACKAGE = PROJECT / 'assets/models/vehicles/tank/textures'
SLOT = 'TANK'
FINAL_SIZE = 1024
SOURCE = dict(archive='data/allcars.bfp', archive_crc32='92273ec9', member=28,
              member_name='cars\\tank\\high\\t', member_crc32='3d3ea7c9')


def canonical_atlas():
    (_, _, original), = images.texture_records(decode(read_source(SOURCE))[3], 1)
    return original


def source_key_alpha(original, width, height):
    """Keep the entire canonical mask, including unused texels/filtering borders."""
    source_width, source_height, pixels = images.bmp_pixels(original)
    return bytes(0 if pixels[(y * source_height // height) * source_width + x * source_width // width] == (0, 0, 0)
                 else 255 for y in range(height) for x in range(width))


def triangle_texel_overlap(triangle, x, y):
    """Test positive-area overlap, including slivers missed by center/lattice probes.

    Triangle coordinates and the unit texel square use source-pixel units.
    Separating axes are the square's axes and the triangle's three edge normals.
    """
    a, b, c = triangle
    if abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) < 1e-12:
        return False
    axes = [(1, 0), (0, 1)] + [(p[1] - q[1], q[0] - p[0]) for p, q in ((a, b), (b, c), (c, a))]
    for dx, dy in axes:
        projections = [u * dx + v * dy for u, v in triangle]
        center = (x + .5) * dx + (y + .5) * dy
        radius = .5 * (abs(dx) + abs(dy))
        if min(max(projections), center + radius) <= max(min(projections), center - radius) + 1e-10:
            return False
    return True


def sampled_key_texels(original, meshes):
    """Return exact-black source texels touched by actual slot-0 triangle areas."""
    width, height, pixels = images.bmp_pixels(original)
    keys = [(i % width, i // width) for i, rgb in enumerate(pixels) if rgb == (0, 0, 0)]
    sampled = set()
    for mesh in meshes:
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != 0:
                continue
            triangle = [(mesh.vertices[i][3] * width, (mesh.vertices[i][4] + 1) * height)
                        for i in struct.unpack_from('<3H', face, 2)]
            if any(not math.isfinite(value) for point in triangle for value in point):
                raise ValueError('Tank UVs must be finite')
            if any(not 0 <= point[0] <= width or not 0 <= point[1] <= height for point in triangle):
                raise ValueError('Tank atlas triangles must stay within the canonical chart')
            left, top = (min(point[axis] for point in triangle) for axis in (0, 1))
            right, bottom = (max(point[axis] for point in triangle) for axis in (0, 1))
            for x, y in keys:
                if (x, y) not in sampled and x + 1 > left and x < right and y + 1 > top and y < bottom:
                    if triangle_texel_overlap(triangle, x, y):
                        sampled.add((x, y))
    return sampled


def converter():
    command = shutil.which('magick') or shutil.which('convert')
    if not command:
        raise RuntimeError('ImageMagick with WebP support is required')
    return command


def encode(source, original=None, *, quality=70):
    """Keep generated RGB and register source alpha after the only resize."""
    images.lossless_webp_dimensions(Path(source).read_bytes())
    rgba = bytearray(subprocess.run(
        [converter(), str(source), '-filter', 'Lanczos', '-resize', f'{FINAL_SIZE}x{FINAL_SIZE}',
         '-alpha', 'on', '-depth', '8', 'rgba:-'],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout)
    if len(rgba) != FINAL_SIZE * FINAL_SIZE * 4:
        raise ValueError('Tank master must produce the registered square atlas')
    rgba[3::4] = source_key_alpha(canonical_atlas() if original is None else original, FINAL_SIZE, FINAL_SIZE)
    return subprocess.run(
        [converter(), '-size', f'{FINAL_SIZE}x{FINAL_SIZE}', '-depth', '8', 'rgba:-', '-strip',
         '-define', 'webp:lossless=false', '-define', 'webp:method=6', '-define', 'webp:alpha-quality=100',
         '-quality', str(quality), 'webp:-'],
        input=bytes(rgba), check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('finalize',))
    parser.add_argument('--verify', action='store_true', help='Compare with the committed WebP instead of writing')
    args = parser.parse_args()
    provenance_path = PACKAGE / 'provenance.json'
    provenance = json.loads(provenance_path.read_text())
    quality = provenance['finalization']['quality'] if args.verify else 70
    master, = provenance['assets']
    source = PACKAGE / master['file']
    if crc32(source.read_bytes()) != master['crc32']:
        raise ValueError('Tank artwork work-source CRC32 mismatch')
    data = encode(source, quality=quality)
    target = PACKAGE / f'{SLOT}.webp'
    if args.verify:
        if target.read_bytes() != data:
            sys.exit(f'{target} differs from the reproduced WebP{quality} output')
        print(json.dumps({'file': target.name, 'bytes': len(data), 'crc32': crc32(data)}))
        return
    target.write_bytes(data)
    provenance['finalization'] = {'quality': quality}
    provenance_path.write_text(json.dumps(provenance, indent=2) + '\n')
    print(json.dumps({'file': target.name, 'bytes': len(data), 'crc32': crc32(data)}))


if __name__ == '__main__':
    main()
