#!/usr/bin/env python3
"""Per-car glove artwork follows canonical1.1a slots and material identity."""

from io import BytesIO
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from PIL import Image

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import cockpit_hand_textures as textures
from git_game_data import configured_data_root
from vehicle_model import decode, read_source
from vehicle_textures import bmp_dimensions, crc32, texture_records, webp_dimensions

PACKAGE = PROJECT / "assets/models/cockpits/hands"


class HandTextureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generation = json.loads((PACKAGE / "generation.json").read_text())
        cls.data_root = configured_data_root()

    def test_nine_authored_sets_match_original_members_and_output_provenance(self):
        document = self.generation
        self.assertEqual(document["webp_quality"], 80)
        self.assertEqual([car["car_id"] for car in document["cars"]], [0, 1, 2, 3, 4, 5, 6, 7, 9])
        self.assertEqual(len(document["sources"]), 9)
        sources = {source["id"]: source for source in document["sources"]}
        sections = []
        for car in document["cars"]:
            original_sections = []
            for member in car["members"]:
                raw = read_source({**car, **member}, self.data_root)
                header, _, _, section = decode(raw)
                self.assertEqual(crc32(section), member["texture_section_crc32"])
                original_sections.append(section)
                count = struct.unpack_from("<H", header, 16)[0]
                self.assertEqual(textures.replace_textures(section, count), section)
                replaced = textures.replace_textures(section, count, PACKAGE / car["key"] / "textures")
                for original, authored in zip(texture_records(section, count), texture_records(replaced, count)):
                    self.assertEqual(original[:2], authored[:2])
                    self.assertEqual(authored[2], (PACKAGE / car["key"] / "textures" / (original[0] + ".webp")).read_bytes())
            self.assertEqual(original_sections[0], original_sections[1])
            sections.append(original_sections[0])
            originals = texture_records(original_sections[0], 4)
            self.assertEqual(tuple(item[0] for item in originals), textures.SLOTS)
            self.assertEqual(len(car["outputs"]), 4)
            for original, output in zip(originals, car["outputs"]):
                payload = (PACKAGE / output["image"]).read_bytes()
                self.assertEqual(crc32(original[2]), output["original_crc32"])
                self.assertEqual(list(webp_dimensions(payload)), output["size"])
                self.assertLessEqual(max(output["size"]), textures.MAX_EDGE)
                ow, oh, _ = bmp_dimensions(original[2])
                self.assertEqual(output["size"][0] * oh, output["size"][1] * ow)
                source = sources[output["source"]]
                self.assertRegex(source["crc32"], r"^[0-9a-f]{8}$")
                self.assertGreaterEqual(output["crop"][0], output["size"][0])
                self.assertGreaterEqual(output["crop"][1], output["size"][1])
        self.assertEqual(len(set(sections)), 9)

    def test_exact_complete_slot_set_and_directory_boundaries(self):
        car = self.generation["cars"][0]
        _, _, _, section = decode(read_source({**car, **car["members"][0]}, self.data_root))
        original = PACKAGE / car["key"] / "textures"
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for slot in textures.SLOTS:
                (directory / (slot + ".webp")).write_bytes((original / (slot + ".webp")).read_bytes())
            self.assertTrue(textures.replace_textures(section, 4, directory))
            (directory / "extra.webp").write_bytes(b"invalid")
            with self.assertRaisesRegex(ValueError, "exactly four"):
                textures.replace_textures(section, 4, directory)
            (directory / "extra.webp").unlink()
            (directory / "ARM.webp").unlink()
            with self.assertRaisesRegex(ValueError, "exactly four"):
                textures.replace_textures(section, 4, directory)
            (directory / "ARM.webp").symlink_to(original / "ARM.webp")
            with self.assertRaisesRegex(ValueError, "escapes"):
                textures.replace_textures(section, 4, directory)

    def test_flex_square_swatches_cannot_replace_another_cars_tall_palm(self):
        car = self.generation["cars"][0]
        _, _, _, section = decode(read_source({**car, **car["members"][0]}, self.data_root))
        with self.assertRaisesRegex(ValueError, "UV aspect"):
            textures.replace_textures(section, 4, PACKAGE / "flexmobile" / "textures")

    def test_committed_flex_palm_keeps_original_exact_black_alpha_mask(self):
        car = next(car for car in self.generation["cars"] if car["car_id"] == 9)
        _, _, _, section = decode(read_source({**car, **car["members"][0]}, self.data_root))
        originals = {name: payload for name, _, payload in texture_records(section, 4)}
        output = next(output for output in car["outputs"] if output["slot"] == "GLOVE01")
        with Image.open(BytesIO(originals["GLOVE01"])) as bitmap:
            source = bitmap.convert("RGB")
        mask = Image.new("L", source.size)
        mask.putdata([0 if pixel == (0, 0, 0) else 255 for pixel in source.get_flattened_data()])
        expected = mask.resize(tuple(output["size"]), Image.Resampling.NEAREST)
        with Image.open(PACKAGE / output["image"]) as artwork:
            alpha = artwork.convert("RGBA").getchannel("A")
        self.assertEqual(alpha.getextrema(), (0, 255))
        self.assertEqual(alpha.tobytes(), expected.tobytes())

    def test_import_rejects_changed_generated_source_before_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "honda").mkdir()
            (root / "honda/generated.png").write_bytes(b"changed raw generated source")
            output = root / "output"
            with self.assertRaisesRegex(ValueError, "source CRC32 differs"):
                textures.import_generation(PACKAGE / "generation.json", root, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
