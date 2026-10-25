#!/usr/bin/env python3
"""Generated menu cells retain alpha, explicit crops and reference cells."""

import importlib.util
from io import BytesIO
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from PIL import Image, ImageDraw

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
SPEC = importlib.util.spec_from_file_location('menu_icon_assets', PROJECT / 'tools/menu_icon_assets.py')
ASSETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ASSETS)


def local_reference_archive(path):
    """An indexed fixture records source provenance without game media."""
    image = Image.new('P', (267, 457), 2)
    palette = [0] * 768
    palette[6:9] = [80, 90, 100]
    image.putpalette(palette)
    stream = BytesIO()
    image.save(stream, 'BMP')
    member = stream.getvalue()
    entries = [struct.pack('<16sII', b'local-reference', index * len(member), len(member)) for index in range(30)]
    path.write_bytes(struct.pack('<III', 30, 30 * 24, len(member)) + b''.join(entries) + member * 30)


class MenuIconImportTests(unittest.TestCase):
    def test_fit_cell_preserves_aspect_and_centers_transparently(self):
        square = Image.new('RGBA', (20, 20), (230, 20, 20, 255))
        fitted = ASSETS.fit_cell(square, (40, 20))
        self.assertEqual(fitted.size, (40, 20))
        self.assertEqual(fitted.getchannel('A').getbbox(), (10, 0, 30, 20))

        wide = Image.new('RGBA', (40, 20), (20, 230, 20, 255))
        fitted = ASSETS.fit_cell(wide, (20, 40))
        self.assertEqual(fitted.getchannel('A').getbbox(), (0, 15, 20, 25))

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        archive = self.root / 'local-reference.bfp'
        local_reference_archive(archive)
        canonical = ASSETS.canonical
        for key, value in (('ARCHIVE', archive), ('canonical', lambda: canonical(archive)),
                           ('OUTPUT', self.root / 'output'), ('BATCHES', ((1,),))):
            context = patch.object(ASSETS, key, value)
            context.start()
            self.addCleanup(context.stop)
        self.sheet = self.root / 'generated.png'
        image = Image.new('RGBA', (20, 8))
        draw = ImageDraw.Draw(image)
        draw.rectangle((3, 2, 6, 5), fill=(230, 20, 20, 255))
        draw.rectangle((12, 2, 15, 5), fill=(20, 230, 20, 255))
        image.save(self.sheet)
        self.cells = self.root / 'cells.json'
        self.cells.write_text(json.dumps([[2, 1, 6, 6], [11, 1, 6, 6]]))
        self.args = SimpleNamespace(input=self.sheet, batch=0, aux_spec=None, cells=self.cells)

    def test_explicit_cells_preserve_artwork_alpha(self):
        ASSETS.import_artwork(self.args)
        for state, green, rect in (('normal', False, [2, 1, 8, 7]), ('selected', True, [11, 1, 17, 7])):
            path = ASSETS.OUTPUT / f'icon-1-{state}.webp'
            image = Image.open(path)
            self.assertEqual(image.size, (352, 224))
            self.assertEqual(image.mode, 'RGBA')
            self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
            red, actual_green, _, alpha = image.getpixel((176, 112))
            self.assertGreater(actual_green if green else red, 200)
            self.assertGreater(alpha, 200)
        first = {path.name: path.read_bytes() for path in ASSETS.OUTPUT.glob('*.webp')}
        ASSETS.import_artwork(self.args)
        self.assertEqual({path.name: path.read_bytes() for path in ASSETS.OUTPUT.glob('*.webp')}, first)

    def test_invalid_cells_do_not_write_partial_packages(self):
        for rectangles, message in (([[0, 0, 1, 1]], 'count'),
                                    ([[0, 0, 1, 1], [11, 1, 6, 6]], 'visible pixels'),
                                    ([[2, 1, 6, 6], [19, 1, 6, 6]], 'bounds')):
            with self.subTest(rectangles=rectangles):
                self.cells.write_text(json.dumps(rectangles))
                with self.assertRaisesRegex(ValueError, message):
                    ASSETS.import_artwork(self.args)
                self.assertFalse(ASSETS.OUTPUT.exists())

    def test_opaque_and_empty_sources_are_rejected(self):
        for mode, color in (('RGB', (10, 20, 30)), ('RGBA', (0, 0, 0, 0))):
            with self.subTest(mode=mode):
                Image.new(mode, (20, 8), color).save(self.sheet)
                with self.assertRaisesRegex(ValueError, 'transparent background'):
                    ASSETS.import_artwork(self.args)
                self.assertFalse(ASSETS.OUTPUT.exists())


if __name__ == '__main__':
    unittest.main()
