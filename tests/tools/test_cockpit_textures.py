#!/usr/bin/env python3
"""Authored shared cockpit materials retain their bindings and transparency."""

import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import cockpit_textures as textures
from vehicle_textures import rgb_bmp, texture_records, webp_dimensions

PACKAGE = PROJECT / "assets/models/cockpits/shared"
CONVERTER = shutil.which("magick") or shutil.which("convert")


def record(name, payload):
    return name.encode().ljust(12, b"\0") + struct.pack("<I", len(payload)) + payload


class CockpitTextureTests(unittest.TestCase):
    def test_authored_package_matches_provenance_and_uv_layout(self):
        generation = json.loads((PACKAGE / "generation.json").read_text())
        self.assertEqual(generation["webp_quality"], 80)
        sources = {source["id"]: source for source in generation["sources"]}
        self.assertEqual(len(sources), 2)
        for output in generation["outputs"]:
            payload = (PACKAGE / output["image"]).read_bytes()
            self.assertEqual(list(webp_dimensions(payload)), output["size"])
            self.assertLessEqual(max(output["size"]), 512)
            self.assertLessEqual(len(payload), textures.MAX_BYTES)
            source = sources[output["source"]]
            self.assertRegex(source["crc32"], r"^[0-9a-f]{8}$")
            self.assertGreaterEqual(output["crop"][0], output["size"][0])
            self.assertGreaterEqual(output["crop"][1], output["size"][1])

    def test_shared_directory_embeds_only_the_models_original_slot_verbatim(self):
        for name, size in textures.SLOTS.items():
            original = record(name, rgb_bmp(*size, [(20, 20, 20)] * (size[0] * size[1])))
            self.assertEqual(textures.replace_textures(original, 1), original)
            replaced = textures.replace_textures(original, 1, PACKAGE / "textures")
            records = texture_records(replaced, 1)
            self.assertEqual(records[0][0], name)
            self.assertEqual(records[0][1], original[:12])
            self.assertEqual(records[0][2], (PACKAGE / "textures" / (name + ".webp")).read_bytes())

    def test_import_rejects_unknown_slots_and_escaping_files(self):
        original = record("wheel01", rgb_bmp(128, 128, [(20, 20, 20)] * 128 * 128))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "unknown.webp").write_bytes(b"not artwork")
            with self.assertRaises(ValueError):
                textures.replace_textures(original, 1, directory)
            (directory / "unknown.webp").unlink()
            (directory / "wheel01.webp").symlink_to(PACKAGE / "textures/wheel01.webp")
            with self.assertRaises(ValueError):
                textures.replace_textures(original, 1, directory)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_final_wheel_has_visible_material_and_clear_structural_openings(self):
        payload = subprocess.run([CONVERTER, str(PACKAGE / "textures/wheel01.webp"),
                                  "-depth", "8", "rgba:-"], check=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        self.assertEqual(len(payload), 512 * 512 * 4)
        pixel = lambda x, y: tuple(payload[(y * 512 + x) * 4:(y * 512 + x + 1) * 4])
        self.assertEqual(pixel(0, 0)[3], 0)
        self.assertEqual(pixel(170, 256)[3], 0)
        self.assertGreaterEqual(pixel(256, 256)[3], 250)
        self.assertGreaterEqual(pixel(256, 20)[3], 250)
        rim = pixel(256, 20)
        self.assertGreater(rim[0], rim[1])
        self.assertGreater(rim[2], rim[1])

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_reimport_downsamples_and_keeps_generated_alpha(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "generated.png"
            subprocess.run([CONVERTER, "-size", "32x32", "xc:none", "-fill", "#6e235b",
                            "-draw", "rectangle 8,8 23,23", str(source)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            output = root / "final.webp"
            stats = textures.import_artwork(source, output, [32, 32, 0, 0], [16, 16])
            self.assertEqual(stats["crc32"], textures.crc32(output.read_bytes()))
            rgba = subprocess.run([CONVERTER, str(output), "-depth", "8", "rgba:-"], check=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
            self.assertEqual(rgba[3], 0)
            self.assertEqual(rgba[(8 * 16 + 8) * 4 + 3], 255)
            with self.assertRaises(ValueError):
                textures.import_artwork(source, output, [32, 32, 0, 0], [64, 64])
            # Both target edges shrink, but unequal scale would distort the UV chart.
            with self.assertRaisesRegex(ValueError, "unchanged aspect"):
                textures.import_artwork(source, output, [32, 32, 0, 0], [16, 8])
            self.assertEqual(textures.crc32(output.read_bytes()), stats["crc32"])


if __name__ == "__main__":
    unittest.main()
