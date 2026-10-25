#!/usr/bin/env python3
"""Flexmobile artwork: canonical key mask, slot identity and byte reproduction."""

import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import flexmobile_textures as artwork
import vehicle_manifest as manifest
import vehicle_textures as images
from vehicle_model import decode, read_source

ENTRY = next(entry for entry in manifest.load_manifest()['vehicles'] if entry['key'] == 'flexmobile')


def webp_rgba(path):
    data = Path(path).read_bytes()
    width, height = images.webp_dimensions(data)
    converter = images.shutil.which('magick') or images.shutil.which('convert')
    rgba = subprocess.run([converter, str(path), '-alpha', 'on', '-depth', '8', 'rgba:-'],
                          capture_output=True, check=True).stdout
    return width, height, rgba


class FlexmobileTextureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.definition = manifest.load_definition(ENTRY)
        cls.package = PROJECT / ENTRY['texture_directory']
        cls.provenance = json.loads((cls.package / 'provenance.json').read_text())
        cls.source = read_source(ENTRY['source'])


    def test_canonical_key_mask_and_opaque_ink(self):
        import flexmobile_geometry as geometry
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        arch_uvs = geometry.wheel_arch_opaque_uvs(meshes)
        header, _, _, section = decode(self.source)
        originals = {name: payload for name, _, payload in images.texture_records(section, struct.unpack_from('<H', header, 16)[0])}
        for name in artwork.SLOTS:
            old_width, old_height, old_pixels = images.bmp_pixels(originals[name])
            width, height, rgba = webp_rgba(self.package / (name + '.webp'))
            coverage = artwork.arch_coverage(width, height, arch_uvs) if name == 'FLEX' else bytearray(width * height)
            key = repaired = chrome = pink = 0
            for y in range(height):
                for x in range(width):
                    source = old_pixels[(y * old_height // height) * old_width + x * old_width // width]
                    alpha = rgba[(y * width + x) * 4 + 3]
                    if source == (0, 0, 0) and coverage[y * width + x]:
                        repaired += 1
                        self.assertEqual(alpha, 255, 'Retained physical arch surface must be opaque')
                        r, g, b = rgba[(y * width + x) * 4:(y * width + x) * 4 + 3]
                        self.assertGreater(max(r, g, b), 55, 'Arch repair must extend artwork, not black atlas remnants')
                        if r > g + 20:
                            pink += 1
                        if b >= r and g > 60:
                            chrome += 1
                    elif source == (0, 0, 0):
                        key += 1
                        self.assertEqual(alpha, 0, f'{name} source key texel must stay transparent')
                    else:
                        self.assertEqual(alpha, 255, f'{name} source ink texel must stay opaque')
            self.assertGreater(key, 0)
            self.assertEqual(len(rgba), width * height * 4)
            if name == 'FLEX':
                self.assertGreater(repaired, 0)
                self.assertLess(repaired, width * height // 50, 'Mask changes must stay local to front arches')
                self.assertGreater(pink, 20, 'Upper arch repairs must continue pink body paint')
                self.assertGreater(chrome, 20, 'Lower arch cheeks must continue horizontal chrome bands')
            else:
                self.assertEqual(repaired, 0)
                self.assertEqual(images.crc32((self.package / 'ENGFLO.webp').read_bytes()), '5807d910')

    def test_finished_plate_lettering_is_opaque(self):
        rectangles = ((223, 475, 296, 502), (204, 983, 277, 1010))
        width, height, final = webp_rgba(self.package / 'FLEX.webp')
        for x0, y0, x1, y1 in rectangles:
            yellow = 0
            for y in range(y0 * height // 1254, (y1 * height + 1253) // 1254):
                for x in range(x0 * width // 1254, (x1 * width + 1253) // 1254):
                    r, g, b, a = final[(y * width + x) * 4:(y * width + x) * 4 + 4]
                    self.assertEqual(a, 255, 'Plate lettering must not punch transparency holes')
                    if r > 120 and g > 90 and b < g * .7:
                        yellow += 1
            self.assertGreater(yellow, 25, 'Both encoded plate faces must contain gold lettering')

    def test_geometry_embeds_the_final_webp_payloads(self):
        import flexmobile_geometry as geometry
        generated, stats = geometry.generate(self.source, self.definition, self.package)
        header, _, _, section = decode(generated)
        embedded = {name: payload for name, _, payload in images.texture_records(section, struct.unpack_from('<H', header, 16)[0])}
        self.assertEqual(set(embedded), set(artwork.SLOTS))
        for name in artwork.SLOTS:
            final = (self.package / (name + '.webp')).read_bytes()
            self.assertEqual(embedded[name], final, f'{name} must be embedded byte for byte')

    def test_finished_body_pixels_preserve_accepted_alpha_and_shoulder_colors(self):
        master = self.package / 'work/FLEX.webp'
        self.assertEqual(images.lossless_webp_dimensions(master.read_bytes()), (1024, 1024))
        rgba = subprocess.run([artwork._converter(), str(master), '-depth', '8', 'rgba:-'],
                              capture_output=True, check=True).stdout
        self.assertEqual(images.crc32(rgba), '7c7a09b2', 'Finished pixels must match the accepted pre-WebP input')
        width, height, encoded = webp_rgba(self.package / 'FLEX.webp')
        self.assertEqual(rgba[3::4], encoded[3::4], 'Stored body source must already contain its final alpha mask')
        for u, v in ((253., 34.), (254., 33.), (254.5, 33.)):
            r, g, b, alpha = encoded[(int(v * height / 256) * width + int(u * width / 256)) * 4:][:4]
            self.assertGreater(r, 180)
            self.assertGreater(g, 70)
            self.assertGreater(b, g)
            self.assertEqual(alpha, 255)
        self.assertEqual(images.crc32((self.package / 'ENGFLO.webp').read_bytes()), '5807d910')

    def test_body_export_requires_the_finished_source_dimensions_format_and_crc(self):
        metadata = json.loads((self.package / 'metadata.json').read_text())
        header, _, _, section = decode(self.source)
        original = next(payload for name, _, payload in images.texture_records(section, struct.unpack_from('<H', header, 16)[0]) if name == 'FLEX')
        master = self.package / 'work/FLEX.webp'
        for changed in ({**metadata, 'prepared_body_crc32': '00000000'},
                        {**metadata, 'final_dimensions': {**metadata['final_dimensions'], 'FLEX': [512, 512]}}):
            with self.subTest(metadata=changed), self.assertRaisesRegex(ValueError, 'finished lossless WebP'):
                artwork.prepare_artwork('FLEX', original, master, changed)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'FLEX.webp'
            path.write_bytes((self.package / 'FLEX.webp').read_bytes())
            with self.assertRaises(ValueError):
                artwork.prepare_artwork('FLEX', original, path, {**metadata, 'prepared_body_crc32': images.crc32(path.read_bytes())})

    def test_finalization_reproduces_every_webp_byte(self):
        with tempfile.TemporaryDirectory(prefix='toca-flexmobile-texture-') as temporary:
            report = json.loads(subprocess.run(
                [sys.executable, str(PROJECT / 'tools/flexmobile_textures.py'), 'finalize', '--verify'],
                capture_output=True, text=True, check=True, cwd=PROJECT).stdout)
        self.assertTrue(report['verified'])
        self.assertEqual([asset['file'] for asset in report['assets']],
                         ['FLEX.webp', 'ENGFLO.webp'])

    def test_finished_lamp_walls_contain_continuous_pink_and_chrome(self):
        width, height, encoded = webp_rgba(self.package / 'FLEX.webp')
        def sample(u, v):
            offset = (int(v * height / 256) * width + int(u * width / 256)) * 4
            return encoded[offset:offset + 4]
        # Both former source-pink and source-chrome black spill now match the
        # neighboring bands; the much smaller mirrored wall spill is removed.
        pink = sample(254., 24.)
        self.assertGreater(pink[0], 180)
        self.assertGreater(pink[0], pink[2] + 50)
        chrome = sample(254.5, 28.)
        self.assertGreater(chrome[1], 100)
        self.assertGreater(chrome[2], chrome[0])
        self.assertGreater(max(sample(254.5, 225.)[:3]), 80)
        self.assertEqual(pink[3], 255)
        self.assertEqual(chrome[3], 255)


if __name__ == '__main__':
    unittest.main()
