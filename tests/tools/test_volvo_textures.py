#!/usr/bin/env python3
"""Volvo V03 source identity, decal/glass alpha and reproducible artwork."""

from pathlib import Path
import json
import shutil
import struct
import subprocess
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import volvo_textures as volvo
import vehicle_textures as images
from vehicle_model import decode, read_source

PACKAGE = PROJECT / 'assets/models/vehicles/volvo/textures'
CONVERTER = shutil.which('magick') or shutil.which('convert')


def rgba(name):
    data = (PACKAGE / (name + '.webp')).read_bytes()
    width, height = images.webp_dimensions(data)
    pixels = subprocess.run([CONVERTER, str(PACKAGE / (name + '.webp')), '-depth', '8', 'rgba:-'],
                            check=True, stdout=subprocess.PIPE).stdout
    return width, height, pixels


class VolvoTextureTests(unittest.TestCase):

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_original_slots_duplicate_wheel_and_exact_glass_key_masks(self):
        header, _, _, section = decode(read_source(volvo.SOURCE))
        records = images.texture_records(section, struct.unpack_from('<H', header, 16)[0])
        self.assertEqual(tuple(n for n, _, _ in records), volvo.SLOTS)
        self.assertEqual(records[0][2], records[9][2])
        for name in ('WSCRN', 'SIDEG'):
            original = next(payload for n, _, payload in records if n == name)
            old_width, old_height, pixels = images.bmp_pixels(original)
            width, height, output = rgba(name)
            for y in range(height):
                for x in range(width):
                    keyed = pixels[(y * old_height // height) * old_width + x * old_width // width] == (0, 0, 0)
                    self.assertEqual(output[(y * width + x) * 4 + 3], 0 if keyed else 255, (name, x, y))

        for name in ('NUM3', 'Num11'):
            original = next(payload for n, _, payload in records if n == name)
            old_width, old_height, pixels = images.bmp_pixels(original)
            points = [(i % old_width, i // old_width) for i, c in enumerate(pixels) if c != (0, 0, 0)]
            width, height, output = rgba(name)
            actual = [(i % width, i // width) for i, a in enumerate(output[3::4]) if a]
            expected = (round(min(x for x,y in points)*width/old_width),
                        round(min(y for x,y in points)*height/old_height),
                        round((max(x for x,y in points)+1)*width/old_width),
                        round((max(y for x,y in points)+1)*height/old_height))
            bounds = (min(x for x,y in actual), min(y for x,y in actual),
                      max(x for x,y in actual)+1, max(y for x,y in actual)+1)
            self.assertEqual(bounds, expected, name)

    @unittest.skipUnless(CONVERTER, 'ImageMagick required')
    def test_number_ink_explicit_alpha_and_both_driver_name_bands(self):
        for name, components in (('NUM3', 1), ('Num11', 2)):
            width, height, pixels = rgba(name)
            self.assertEqual((width, height), (256, 256))
            self.assertGreater(sum(a == 255 for a in pixels[3::4]), 4000)
            for x, y in ((0, 0), (255, 0), (0, 255), (255, 255)):
                self.assertEqual(pixels[(y * width + x) * 4 + 3], 0)
            # Every retained alpha island belongs to one real digit; stray
            # generator residues must not survive on transparent number cards.
            visited, bodies = set(), []
            for i, alpha in enumerate(pixels[3::4]):
                if alpha < 128 or i in visited:
                    continue
                todo, size = [i], 0
                visited.add(i)
                while todo:
                    current = todo.pop()
                    size += 1
                    x, y = current % width, current // width
                    for xx, yy in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                        j = yy * width + xx
                        if 0 <= xx < width and 0 <= yy < height and j not in visited and pixels[j*4+3] >= 128:
                            visited.add(j)
                            todo.append(j)
                if size > 100:
                    bodies.append(size)
            self.assertEqual(len(bodies), components, (name, bodies))
        metadata = json.loads((PACKAGE / 'metadata.json').read_text())
        self.assertEqual(metadata['name_source_uv_range'], [.375, .5])
        self.assertEqual(metadata['name_uv_ranges']['ListA'], metadata['name_uv_ranges']['ListB'])
        for name in ('ListA', 'ListB'):
            width, height, pixels = rgba(name)
            low, high = metadata['name_uv_ranges'][name]
            self.assertGreater(sum(pixels[(y * width + x) * 4 + 3] == 255
                                   for y in range(int(low*height), int(high*height)) for x in range(width)), 2500)
            self.assertTrue(all(pixels[(y*width+x)*4+3] == 0
                                for y in (int(low*height), int(high*height)-1) for x in range(width)))

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_finalization_reproduces_every_embedded_webp_byte(self):
        self.assertTrue(volvo.finalize(True)['verified'])


if __name__ == '__main__':
    unittest.main()
