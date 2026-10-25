#!/usr/bin/env python3
"""Tank artwork: provenance, reproducible WebP80 and exact source-key coverage."""

import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import tank_geometry as geometry
import tank_textures as tank
import vehicle_manifest as manifest
import vehicle_textures as images
from vehicle_model import crc32, decode, read_source

PACKAGE = tank.PACKAGE
ENTRY = next(entry for entry in manifest.load_manifest()['vehicles'] if entry['key'] == 'tank')


class TankTextureTests(unittest.TestCase):

    def test_final_webp_is_square_bounded_and_reproducible(self):
        data = (PACKAGE / 'TANK.webp').read_bytes()
        self.assertEqual(images.webp_dimensions(data), (tank.FINAL_SIZE, tank.FINAL_SIZE))
        provenance = json.loads((PACKAGE / 'provenance.json').read_text())
        self.assertEqual(tank.encode(PACKAGE / 'work/TANK.webp', quality=provenance['finalization']['quality']), data)

    def test_artwork_keeps_the_single_slot_and_aspect(self):
        source = read_source(ENTRY['source'])
        header, _, _, textures = decode(source)
        count = struct.unpack_from('<H', header, 16)[0]
        self.assertEqual(count, 1)
        replaced = images.replace_textures(textures, count, PACKAGE)
        (name, _, payload), = images.texture_records(replaced, count)
        self.assertEqual(name, 'TANK')
        self.assertEqual(payload, (PACKAGE / 'TANK.webp').read_bytes())

    def test_triangle_footprints_include_slivers_but_exclude_edge_contact(self):
        sliver = [(0.99, 0), (1.01, 0), (1.01, 1)]
        self.assertTrue(tank.triangle_texel_overlap(sliver, 0, 0))
        self.assertTrue(tank.triangle_texel_overlap(list(reversed(sliver)), 0, 0))
        self.assertFalse(tank.triangle_texel_overlap([(1, 0), (2, 0), (1, 1)], 0, 0))
        self.assertFalse(tank.triangle_texel_overlap([(0, 0), (.5, .5), (1, 1)], 0, 0))

    def test_encoded_alpha_preserves_complete_source_mask_and_sampled_boundary_texels(self):
        """Check decoded WebP alpha, including subtexel original/final triangle edges."""
        source = read_source(ENTRY['source'])
        _, _, original_meshes, textures = decode(source)
        (_, _, original), = images.texture_records(textures, 1)
        width, height, pixels = images.bmp_pixels(original)
        rgba = subprocess.run([tank.converter(), str(PACKAGE / 'TANK.webp'), '-depth', '8', 'rgba:-'],
                              check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        self.assertEqual(len(rgba), tank.FINAL_SIZE * tank.FINAL_SIZE * 4)
        alpha = rgba[3::4]
        expected = bytes(0 if pixels[(y * height // tank.FINAL_SIZE) * width + x * width // tank.FINAL_SIZE] == (0, 0, 0)
                         else 255 for y in range(tank.FINAL_SIZE) for x in range(tank.FINAL_SIZE))
        self.assertEqual(alpha, expected)
        # The old 7-step barycentric probes missed these source-body edge slivers.
        original_keys = tank.sampled_key_texels(original, [original_meshes[2]])
        self.assertTrue({(71, 228), (72, 233), (73, 238)} <= original_keys)
        # Read back quantized CM3D UVs, exactly as the GLB producer consumes them.
        generated, _ = geometry.generate(source, manifest.load_definition(ENTRY))
        final_keys = tank.sampled_key_texels(original, decode(generated)[2])
        for x, y in original_keys | final_keys:
            for yy in range(y * tank.FINAL_SIZE // height, (y + 1) * tank.FINAL_SIZE // height):
                row = alpha[yy * tank.FINAL_SIZE + x * tank.FINAL_SIZE // width:
                            yy * tank.FINAL_SIZE + (x + 1) * tank.FINAL_SIZE // width]
                self.assertEqual(set(row), {0}, f'Canonical black key ({x}, {y}) lost alpha')


if __name__ == '__main__':
    unittest.main()
