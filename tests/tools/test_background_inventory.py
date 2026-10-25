"""Check exact static HOR identities, valid short palettes and shared consumers."""

import json
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import configured_data_root
import background_inventory as backgrounds
from vehicle_textures import bmp_pixels


class BackgroundInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = backgrounds.inventory(configured_data_root())
        cls.images = {i["material"]: i for i in cls.document["images"]}
        cls.rows = backgrounds.source_rows(configured_data_root())[1]

    def test_complete_canonical_identity(self):
        self.assertEqual(self.document["counts"], dict(images=51, unique_members=43, archives=10, material_occurrences=80))
        self.assertEqual(len({i["bmp_crc32"] for i in self.images.values()}), 51)
        pilot = self.images["DO02"]
        self.assertEqual(pilot["bmp_crc32"], "87048995")
        self.assertEqual(pilot["palette"]["crc32"], "2d92689f")
        self.assertEqual(pilot["occurrences"][0]["member_crc32"], "fd1149f7")

    def test_short_palettes_decode_all_referenced_indices(self):
        for name, entries in (("BH04", 149), ("CR04", 212), ("CARTOON", 43)):
            row, bmp, _ = next(r for r in self.rows if r[0]["material"] == name)
            self.assertEqual(self.images[name]["palette"]["entries"], entries)
            offset = struct.unpack_from("<I", bmp, 10)[0]
            self.assertEqual(offset, 54 + entries * 4)
            width, height, pixels = bmp_pixels(bmp)
            self.assertEqual((width, height, len(pixels)), (256, 256, 65536))
            # First output pixel uses the actual short embedded bank, never PSP1.
            index = bmp[offset + 255 * 256]
            self.assertLess(index, entries)
            self.assertEqual(pixels[0], tuple(reversed(bmp[54 + index * 4:57 + index * 4])))

    def test_shared_donington_extras_and_inactive_occurrences(self):
        self.assertEqual({i["material"] for i in self.images.values() if not i["active"]}, {"TH02", "BH03", "SN02"})
        for material in ("SILLYLAN", "CARTOON"):
            self.assertEqual(len(self.images[material]["occurrences"]), 10)
            self.assertEqual(sum(r["active"] for r in self.images[material]["occurrences"]), 9)
        do03 = self.images["DO03"]["occurrences"]
        self.assertEqual([r["track_id"] for r in do03 if r["active"]], [5])
        self.assertEqual({r["track_id"] for r in do03}, {0, 5, 9})
        self.assertEqual(self.images["CR02"]["occurrences"][0]["consumers"], ["championship-meeting-6-cloudy"])
        self.assertEqual(self.images["LAVABAK"]["occurrences"][0]["consumers"], ["lavaland-all-weather"])

    def test_uv_regions_and_storm_node_slots(self):
        pilot = self.images["DO02"]["occurrences"][0]["meshes"][0]
        self.assertEqual(pilot["uv_source_16_16"], [[0, 65536], [-65536, 0]])
        self.assertEqual(pilot["uv_runtime"], [[0, 1], [0, 1]])
        self.assertEqual((pilot["triangles"], pilot["cap_triangles"]), (24, 8))
        self.assertTrue(all(-65536 < v < 0 for v in pilot["cap_uv_source_16_16"][1]))
        for suffix, slot, node, triangles in (("05", 0, 1, 24), ("05L", 1, 2, 4)):
            row = self.images["DO" + suffix]["occurrences"][0]
            self.assertEqual((row["member"], row["material_slot"]), (6, slot))
            self.assertEqual(row["meshes"][0]["node_indices"], [node])
            self.assertEqual(row["meshes"][0]["triangles"], triangles)
        self.assertEqual(self.images["DO05"]["occurrences"][0]["meshes"][1]["triangles"], 20)
        self.assertEqual(len(self.images["DO05"]["occurrences"][0]["storm_ordinary_uv_deltas"]), 1)
        self.assertEqual(self.images["BH05"]["occurrences"][0]["storm_ordinary_uv_deltas"], [])

    def test_generated_images_keep_exact_source_and_final_payload(self):
        generated_images = []
        for path in (backgrounds.GENERATION, backgrounds.ATMOSPHERE_GENERATION):
            generated_images.extend(json.loads(path.read_text())["images"])
        for generated in generated_images:
            with self.subTest(material=generated["material"]):
                atmosphere = {i["material"]: i for i in self.document["other_atmosphere"]["sun_lens_glow"]["images"]}
                image = self.images.get(generated["material"], atmosphere.get(generated["material"]))
                self.assertEqual(image["bmp_crc32"], generated["source_bmp_crc32"])
                output = generated["output"]
                path = PROJECT / output["path"]
                self.assertEqual(backgrounds.crc32_file(path), output["crc32"])
                self.assertEqual(path.stat().st_size, output["bytes"])
                self.assertLessEqual(path.stat().st_size, 256 * 1024)
                from vehicle_textures import webp_dimensions
                self.assertEqual(webp_dimensions(path.read_bytes()), tuple(image["target"]["size"]))

    def test_atmosphere_visible_consumers_and_suppressed_sun(self):
        images = self.document["other_atmosphere"]["sun_lens_glow"]["images"]
        visible = [i for i in images if i["background_generation"]]
        self.assertEqual([i["material"] for i in visible], [f"LENS{i}320" for i in range(1, 8)])
        self.assertEqual([i["target"]["size"][0] for i in visible], [64, 64, 128, 32, 128, 128, 256])
        for image in images:
            self.assertEqual(len(image["sources"]), 1)
            self.assertEqual(image["sources"][0]["archive"], "data/image640.bfp")
            if image["material"] in ("SUNMASK", "SUN", "WHITE2", "BGLOW", "HGLOW"):
                self.assertIsNone(image["target"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
