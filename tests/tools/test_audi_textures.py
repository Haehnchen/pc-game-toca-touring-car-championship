#!/usr/bin/env python3
"""Audi driver-row coverage, exact payload embedding and source-bound opacity."""

from pathlib import Path
import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import audi_textures as audi
import vehicle_textures as images
from vehicle_manifest import load_definition
from vehicle_model import decode, read_source

PACKAGE = PROJECT / "assets/models/vehicles/audi/textures"
CONVERTER = shutil.which("magick") or shutil.which("convert")


def record(name, payload):
    return name.encode().ljust(12, b"\0") + struct.pack("<I", len(payload)) + payload


def rgba(path):
    return subprocess.run([CONVERTER, str(path), "-depth", "8", "rgba:-"], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


class AudiTextureTests(unittest.TestCase):
    def test_exact_black_key_does_not_erase_near_black_glass(self):
        original = images.rgb_bmp(2, 2, [(0, 0, 0), (0, 8, 8), (0, 0, 8), (255, 255, 255)])
        result = audi.prepare_pixels("WSCRN", original, 2, 2, bytes((0, 0, 0, 255)) * 4)
        self.assertEqual(list(result[3::4]), [0, 255, 255, 255])
        self.assertEqual(result[4:8], bytes((8, 8, 8, 255)))

    def test_opacity_release_is_confined_to_actual_body_footprints(self):
        original = images.rgb_bmp(2, 2, [(0, 0, 0)] * 4)
        pixels = bytes((255, 255, 255, 255)) * 256
        footprint = [[(.25, .25), (.75, .25), (.25, .75)]]
        body = audi.prepare_pixels("AUDI", original, 16, 16, pixels, footprint)
        self.assertEqual(body[(6 * 16 + 5) * 4 + 3], 255)
        self.assertEqual(body[(12 * 16 + 12) * 4 + 3], 0)
        glass = audi.prepare_pixels("SIDEG", original, 16, 16, pixels, footprint)
        self.assertFalse(any(glass[3::4]))

    def test_single_digit_keeps_opaque_ink_outline_and_source_antialiasing(self):
        width, height = 12, 8
        pixels = bytearray(width * height * 4)
        for y in range(1, 4):
            for x in range(1, 4):
                pixels[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes((0, 0, 0, 254))
        edge, noise = (2 * width + 4) * 4, (6 * width + 9) * 4
        pixels[edge:edge + 4] = bytes((255, 255, 255, 64))
        pixels[noise:noise + 4] = bytes((255, 255, 255, 200))
        for name in ("Num1", "Num7"):
            result = audi.prepare_pixels(name, b"", width, height, pixels)
            self.assertEqual(result[(2 * width + 2) * 4:(2 * width + 2) * 4 + 4], bytes((8, 8, 8, 255)))
            self.assertEqual(result[edge:edge + 4], bytes((255, 255, 255, 64)))
            self.assertEqual(result[noise:noise + 4], bytes(4))

    def test_both_swappable_name_slots_fit_the_second_row_without_geometry_changes(self):
        original = images.rgb_bmp(2, 2, [(255, 255, 255)] * 4)
        section = record("ListA", original) + record("ListB", original)
        for slot, name in enumerate(("ListA", "ListB")):
            mesh = SimpleNamespace(vertices=[(1., 2., 3., 0., -.875, 0., 1., 0.), (2., 2., 3., 1., -.75, 0., 1., 0.)],
                                   faces=[struct.pack("<H3Hh4B10x", 0, 0, 1, 0, slot, 255, 255, 255, 255)])
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / (name + ".bmp")).write_bytes(original)
                (root / "metadata.json").write_text(json.dumps({"name_uv_ranges": {"ListA": [.13, .26], "ListB": [.13, .26]}}))
                audi.fit_uvs([mesh], section, 2, root)
                self.assertEqual(mesh.vertices[0][:4], (1., 2., 3., 0.))
                self.assertAlmostEqual(mesh.vertices[0][4] + 1, .13)
                self.assertAlmostEqual(mesh.vertices[1][4] + 1, .26)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_final_webp_bytes_bypass_preparation_and_lossy_encoding(self):
        original = images.rgb_bmp(2, 2, [(255, 255, 255)] * 4)
        final = images.webp_texture(original)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "AUDI.webp").write_bytes(final)
            (root / "metadata.json").write_text("{}")
            with patch.object(audi, "prepare_artwork", side_effect=AssertionError("Final payload re-encoded")):
                replaced = audi.replace_textures(record("AUDI", original), 1, root)
            self.assertEqual(images.texture_records(replaced, 1)[0][2], final)


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_actual_quantized_name_faces_cover_both_full_names_and_no_neighbor_row(self):
        from audi_geometry import generate
        entry = audi._entry()
        ink = {}
        for name, expected in (("ListA", [(5, 61), (70, 126), (135, 191)]), ("ListB", [(8, 68), (75, 132), (142, 198)])):
            rgb = subprocess.run([CONVERTER, str(PACKAGE / "work" / (name + ".webp")), "-filter", "Lanczos", "-resize", "512x512!", "-depth", "8", "rgb:-"],
                                 check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
            bands, previous = [], -2
            for y in range(199):
                if max(rgb[y * 512 * 3:(y + 1) * 512 * 3]) < 128:
                    continue
                if y != previous + 1:
                    bands.append([y, y])
                else:
                    bands[-1][1] = y
                previous = y
            self.assertEqual([tuple(b) for b in bands[:3]], expected)
            ink[name] = (bands[0][1], bands[1][0], bands[1][1], bands[2][0])
        data, _ = generate(read_source(entry["source"]), load_definition(entry), PACKAGE)
        _, _, meshes, _ = decode(data)
        tested = set()
        for index in (26, 27, 29, 30, 37, 38):
            values = [meshes[index].vertices[i][4] + 1 for face in meshes[index].faces if struct.unpack_from("<h", face, 8)[0] in (5, 6)
                      for i in struct.unpack_from("<3H", face, 2)]
            self.assertTrue(values)
            for target, (previous_end, first, last, following) in ink.items():
                with self.subTest(mesh=index, target=target):
                    self.assertGreaterEqual(min(values) * 512, previous_end + 1.5)
                    self.assertLessEqual(min(values) * 512, first + .5)
                    self.assertGreaterEqual(max(values) * 512, last + .5)
                    self.assertLessEqual(max(values) * 512, following - .5)
                tested.add((index, target))
        self.assertEqual(len(tested), 12)

    def test_glass_damage_regions_keep_original_opaque_near_black_behavior(self):
        header, _, _, section = decode(read_source(audi._entry()["source"]))
        records = {n: p for n, _, p in images.texture_records(section, struct.unpack_from("<H", header, 16)[0])}
        for name in ("SIDEG", "WSCRN"):
            self.assertNotIn((0, 0, 0), images.bmp_pixels(records[name])[2])
            result = audi.prepare_pixels(name, records[name], 2, 2, bytes((0, 0, 0, 255)) * 4)
            self.assertEqual(list(result[3::4]), [255] * 4)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_bonnet_registration_avoids_black_canvas_and_preserves_all_other_charts(self):
        header, _, meshes, section = decode(read_source(audi._entry()["source"]))
        before = [[tuple(v) for v in mesh.vertices] for mesh in meshes]
        audi._fit_paint_edges(meshes, json.loads((PACKAGE / "metadata.json").read_text()))
        for index, mesh in enumerate(meshes):
            if index not in (12, 13):
                self.assertEqual(mesh.vertices, before[index])
            for original, fitted in zip(before[index], mesh.vertices):
                self.assertEqual(original[:3], fitted[:3])
                self.assertEqual(original[5:], fitted[5:])
        data = rgba(PACKAGE / "AUDI.webp")
        width = height = 1024
        for index, start, end in ((12, 6, 4), (13, 6, 5), (13, 5, 4)):
            endpoints = [(round(meshes[index].vertices[i][3] * 65536) / 65536,
                          round(meshes[index].vertices[i][4] * 65536) / 65536 + 1) for i in (start, end)]
            for step in range(129):
                t = step / 128
                u, v = [endpoints[0][k] * (1 - t) + endpoints[1][k] * t for k in range(2)]
                x, y = u * width - .5, v * height - .5
                ix, iy = math.floor(x), math.floor(y)
                channels = [0.] * 4
                for dx, dy, weight in ((0, 0, (1 - x + ix) * (1 - y + iy)), (1, 0, (x - ix) * (1 - y + iy)),
                                       (0, 1, (1 - x + ix) * (y - iy)), (1, 1, (x - ix) * (y - iy))):
                    offset = (((iy + dy) % height) * width + (ix + dx) % width) * 4
                    for c in range(4):
                        channels[c] += data[offset + c] * weight
                with self.subTest(mesh=index, step=step):
                    self.assertGreater(max(channels[:3]), 110)
                    self.assertGreater(channels[3], 250)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_registered_motif_backgrounds_exclude_canvas_and_keep_complete_word_ink(self):
        metadata = json.loads((PACKAGE / "metadata.json").read_text())
        pixels = rgba(PACKAGE / "work/AUDI.webp")
        regions = metadata["paint_motif_uvs"]
        self.assertEqual(regions[1]["meshes"], [4, 5, 6])
        quad = regions[1]["quad"]
        final = rgba(PACKAGE / "AUDI.webp")
        tested = 0
        for y in range(math.floor(min(p[1] for p in quad)), math.ceil(max(p[1] for p in quad)) + 1):
            for x in range(math.floor(min(p[0] for p in quad)), math.ceil(max(p[0] for p in quad)) + 1):
                if any((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]) < -1e-8 for a, b in zip(quad, quad[1:] + quad[:1])):
                    continue
                offset = (y * 1254 + x) * 4
                self.assertGreaterEqual(max(pixels[offset:offset + 3]), 150, (x, y))
                target = (int(y * 1024 / 1254) * 1024 + int(x * 1024 / 1254)) * 4
                self.assertGreater(max(final[target:target + 3]), 100, (x, y))
                self.assertEqual(final[target + 3], 255, (x, y))
                tested += 1
        self.assertEqual(tested, 4787)
        # Measured complete red DUNLOP glyphs, including the sloping end letters.
        for y in range(108, 166):
            for x in range(984, 1150):
                offset = (y * 1254 + x) * 4
                red, green, blue = pixels[offset:offset + 3]
                if red > 150 and green < 110 and blue < 110:
                    self.assertTrue(all((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]) >= -1e-8
                                        for a, b in zip(quad, quad[1:] + quad[:1])), (x, y))
        # Black Finance lettering is legitimate; a dark border would be canvas.
        for region in (regions[0], regions[2]):
            x0, y0, x1, y1 = region["bounds"]
            for y in range(y0, y1):
                for x in range(x0, x1):
                    if x0 + 4 <= x < x1 - 4 and y0 + 4 <= y < y1 - 4:
                        continue
                    offset = (y * 1254 + x) * 4
                    self.assertGreater(max(pixels[offset:offset + 3]), 100, (region["bounds"], x, y))

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_final_number_glyphs_have_opaque_black_ink_and_empty_free_canvas(self):
        for name in ("Num1", "Num7"):
            data = rgba(PACKAGE / (name + ".webp"))
            self.assertEqual(len(data), 256 * 256 * 4)
            self.assertTrue(any(data[i + 3] == 255 and max(data[i:i + 3]) < 32 for i in range(0, len(data), 4)))
            self.assertTrue(any(data[i + 3] == 255 and min(data[i:i + 3]) > 220 for i in range(0, len(data), 4)))
            for y in range(256):
                for x in range(256):
                    if x < 50 or x > 205 or y < 35 or y > 225:
                        self.assertEqual(data[(y * 256 + x) * 4 + 3], 0, (name, x, y))


if __name__ == "__main__":
    unittest.main()
