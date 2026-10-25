#!/usr/bin/env python3
"""WIN3 vector artwork, exact double-resolution cells and source cells."""

from io import BytesIO
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
import zlib

from PIL import Image, ImageChops, ImageDraw


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import configured_data_root
import car_name_font_geometry as FONT


class CarNameFontTests(unittest.TestCase):
    def test_vector_raster_is_white_antialiased_and_exactly_double_resolution(self):
        # Geometry generation must work without any bitmap/reference file IO.
        with patch.object(Image, "open", side_effect=AssertionError("Vector authoring must not read bitmap inputs")):
            image = FONT.car_name_atlas()
        self.assertEqual(image.size, (512, 512))
        self.assertEqual(image.mode, "RGBA")
        self.assertEqual(set(FONT.CAR_NAME_GLYPHS), set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'."))
        self.assertEqual(len(FONT.CAR_NAME_GLYPHS), 38)
        self.assertEqual((FONT.CAR_NAME_LINE_HEIGHT, FONT.CAR_NAME_SPACE_WIDTH, FONT.CAR_NAME_ADVANCE), (14, 8, -1))
        self.assertTrue(all(pixel[:3] == (255, 255, 255) for pixel in image.getdata()))
        alpha = image.getchannel("A")
        self.assertGreater(sum(0 < value < 255 for value in alpha.getdata()), 500)
        permitted = Image.new("L", image.size)
        draw = ImageDraw.Draw(permitted)
        for character, (x, y, width, height) in FONT.CAR_NAME_GLYPHS.items():
            cell = alpha.crop((x * 2, y * 2, (x + width) * 2, (y + height) * 2))
            expected = tuple(value * 2 for value in FONT.CAR_NAME_INK_BOUNDS[character])
            self.assertEqual(cell.getbbox(), expected, character)
            draw.rectangle((x * 2, y * 2, (x + width) * 2 - 1, (y + height) * 2 - 1), fill=255)
        self.assertIsNone(ImageChops.subtract(alpha, permitted).getbbox(), "Unassigned original cells must remain empty")
        for scale in (1, 3, 4, 2.0, True):
            with self.subTest(scale=scale), self.assertRaises(ValueError):
                FONT.car_name_atlas(scale)

    def test_committed_webp_matches_vector_recipe(self):
        directory = PROJECT / "assets/ui/mega/hud"
        data = (directory / "car-name.webp").read_bytes()
        encoded = Image.open(BytesIO(data)).convert("RGBA")
        self.assertEqual(encoded.size, (512, 512))
        self.assertEqual(set(encoded.convert("RGB").getdata()), {(255, 255, 255)})
        authored = FONT.car_name_atlas()
        self.assertEqual(encoded.getchannel("A").tobytes(), authored.getchannel("A").tobytes())
    def test_canonical_cells_and_ink_bounds_are_preserved_without_bitmap_enlargement(self):
        archive = (configured_data_root() / "data/allcars.bfp").read_bytes()
        self.assertEqual(f"{zlib.crc32(archive):08x}", "92273ec9")
        count, table_size = struct.unpack_from("<II", archive)
        self.assertEqual((count, table_size), (34, 816))
        offset, size = struct.unpack_from("<II", archive, 28)
        font = archive[12 + table_size + offset:12 + table_size + offset + size]
        self.assertEqual(f"{zlib.crc32(font):08x}", FONT.CAR_NAME_REFERENCE_CRC32)
        width, height = struct.unpack_from("<II", font, 18)
        self.assertEqual((width, height), FONT.CAR_NAME_ATLAS_SIZE)
        appendix = 0x436 + ((width + 3) & ~3) * height
        self.assertEqual(font[appendix + 1], 38)
        reference = Image.open(BytesIO(font)).convert("L")
        generated = FONT.car_name_atlas().getchannel("A")
        for index in range(font[appendix + 1]):
            character, x, y, width, height = struct.unpack_from("<BxHHHH", font, appendix + 24 + index * 16)
            character = chr(character)
            self.assertEqual(FONT.CAR_NAME_GLYPHS[character], (x, y, width, height))
            original = reference.crop((x, y, x + width, y + height))
            self.assertEqual(original.getbbox(), FONT.CAR_NAME_INK_BOUNDS[character])
        # Diagonal letters have fresh continuous edges; neither nearest-neighbor
        # doubling nor resampling an existing source glyph produces this raster.
        for character in "AKNVWXZ":
            x, y, width, height = FONT.CAR_NAME_GLYPHS[character]
            original = reference.crop((x, y, x + width, y + height))
            cell = generated.crop((x * 2, y * 2, (x + width) * 2, (y + height) * 2))
            for resampler in (Image.Resampling.NEAREST, Image.Resampling.BILINEAR, Image.Resampling.LANCZOS):
                self.assertNotEqual(cell.tobytes(), original.resize(cell.size, resampler).tobytes())


if __name__ == "__main__":
    unittest.main()
