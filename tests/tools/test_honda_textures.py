#!/usr/bin/env python3
"""Honda-specific opacity, driver-name coverage and registered lamp artwork."""

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
import honda_textures as honda
import vehicle_textures as images
from vehicle_manifest import load_definition, load_manifest
from vehicle_model import decode, read_source

PACKAGE = PROJECT / "assets/models/vehicles/honda/textures"
CONVERTER = shutil.which("magick") or shutil.which("convert")


def record(name, payload):
    return name.encode().ljust(12, b"\0") + struct.pack("<I", len(payload)) + payload


class HondaTextureTests(unittest.TestCase):
    def test_canonical_black_holes_and_near_black_opaque_glass_are_distinct(self):
        original = images.rgb_bmp(2, 2, [(0, 0, 0), (0, 8, 8), (0, 0, 8), (255, 255, 255)])
        result = honda.prepare_pixels("WSCRN", original, 2, 2, bytes((0, 0, 0, 255)) * 4)
        self.assertEqual(list(result[3::4]), [0, 255, 255, 255])
        self.assertEqual(result[4:8], bytes((8, 8, 8, 255)))

    def test_arch_opacity_is_local_to_surviving_body_faces(self):
        original = images.rgb_bmp(2, 2, [(0, 0, 0)] * 4)
        rgba = bytes((255, 255, 255, 255)) * (16 * 16)
        footprint = [[(.25, .25), (.75, .25), (.25, .75)]]
        body = honda.prepare_pixels("HONDA", original, 16, 16, rgba, footprint)
        self.assertEqual(body[(6 * 16 + 5) * 4 + 3], 255)
        self.assertEqual(body[(12 * 16 + 12) * 4 + 3], 0)
        glass = honda.prepare_pixels("SIDEG", original, 16, 16, rgba, footprint)
        self.assertFalse(any(glass[3::4]))

    def test_number_mask_keeps_black_strokes_outline_and_edge_alpha_only(self):
        width, height = 12, 8
        rgba = bytearray(width * height * 4)
        for left in (1, 8):
            for y in range(1, 4):
                for x in range(left, left + 3):
                    rgba[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes((0, 0, 0, 255))
        edge = (2 * width + 4) * 4
        noise = (6 * width + 6) * 4
        rgba[edge:edge + 4] = bytes((255, 255, 255, 64))
        rgba[noise:noise + 4] = bytes((255, 255, 255, 3))
        result = honda.prepare_pixels("Num10", b"", width, height, rgba)
        self.assertEqual(result[(2 * width + 2) * 4:(2 * width + 2) * 4 + 4], bytes((8, 8, 8, 255)))
        self.assertEqual(result[edge:edge + 4], bytes((255, 255, 255, 64)))
        self.assertEqual(result[noise:noise + 4], bytes(4))

    def test_both_name_slots_share_a_fit_without_changing_geometry(self):
        original = images.rgb_bmp(2, 2, [(255, 255, 255)] * 4)
        section = record("ListA", original) + record("ListB", original)
        mesh = SimpleNamespace(vertices=[(1., 2., 3., 0., -1., 0., 1., 0.), (2., 2., 3., 1., -.875, 0., 1., 0.)],
                               faces=[struct.pack("<H3Hh4B10x", 0, 0, 1, 0, 0, 255, 255, 255, 255)])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "ListA.bmp").write_bytes(original)
            (root / "metadata.json").write_text(json.dumps({"name_uv_ranges": {"ListA": [.01, .14], "ListB": [.01, .14]}}))
            honda.fit_uvs([mesh], section, 2, root)
            self.assertEqual(mesh.vertices[0][:4], (1., 2., 3., 0.))
            self.assertAlmostEqual(mesh.vertices[0][4] + 1, .01)
            self.assertAlmostEqual(mesh.vertices[1][4] + 1, .14)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_final_webp_bypasses_honda_preparation(self):
        original = images.rgb_bmp(2, 2, [(255, 255, 255)] * 4)
        final = images.webp_texture(original)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "HONDA.webp").write_bytes(final)
            (root / "metadata.json").write_text("{}")
            with patch.object(honda, "prepare_artwork", side_effect=AssertionError("Unexpected final re-encode")):
                result = honda.replace_textures(record("HONDA", original), 1, root)
            self.assertEqual(images.texture_records(result, 1)[0][2], final)


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_generated_uvs_cover_both_complete_first_names_without_next_line(self):
        from honda_geometry import generate
        entry = next(v for v in load_manifest()["vehicles"] if v["key"] == "honda")
        metadata = json.loads((PACKAGE / "metadata.json").read_text())
        self.assertEqual(metadata["name_uv_ranges"]["ListA"], metadata["name_uv_ranges"]["ListB"])
        ink = {}
        for name, expected in (("ListA", [(6, 62), (72, 127)]), ("ListB", [(5, 69), (73, 130)])):
            rgb = subprocess.run([CONVERTER, str(PACKAGE / "work" / (name + ".webp")), "-filter", "Lanczos", "-resize", "512x512!",
                                  "-depth", "8", "rgb:-"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
            bands, previous = [], -2
            for y in range(134):
                if max(rgb[y * 512 * 3:(y + 1) * 512 * 3]) < 128:
                    continue
                if y != previous + 1:
                    bands.append([y, y])
                else:
                    bands[-1][1] = y
                previous = y
            self.assertEqual([tuple(b) for b in bands[:2]], expected)
            ink[name] = (bands[0][0], bands[0][1], bands[1][0])
        generated, _ = generate(read_source(entry["source"]), load_definition(entry), PACKAGE)
        header, nodes, meshes, section = decode(generated)
        names = [r[0] for r in images.texture_records(section, struct.unpack_from("<H", header, 16)[0])]
        tested = set()
        for mesh_index in (21, 22, 24, 25, 32, 33):
            mesh = meshes[mesh_index]
            vs = [mesh.vertices[i][4] + 1 for face in mesh.faces if struct.unpack_from("<h", face, 8)[0] in (5, 6)
                  for i in struct.unpack_from("<3H", face, 2)]
            for target, (first, last, next_start) in ink.items():
                with self.subTest(mesh=mesh_index, target=target):
                    self.assertLessEqual(min(vs) * 512 + .5, first)
                    self.assertGreaterEqual(max(vs) * 512 - 1, last + 1)
                    self.assertLess(max(vs) * 512 + .5, next_start)
                tested.add((mesh_index, target))
        self.assertEqual(len(tested), 12)

    def test_lamp_fit_preserves_silver_motifs_and_original_fender_turn_signal_binding(self):
        entry = next(v for v in load_manifest()["vehicles"] if v["key"] == "honda")
        header, _, meshes, section = decode(read_source(entry["source"]))
        before = {i: list(meshes[i].vertices) for i in (0, 1, 4, 5, 39, 40, 41, 42)}
        honda.fit_uvs(meshes, section, struct.unpack_from("<H", header, 16)[0], PACKAGE)
        for index in (0, 1, 4, 5, 40, 42):
            self.assertEqual(meshes[index].vertices, before[index])
        # Measured silver motifs occupy619..704 and319..402 respectively.
        # The amber ends715..738/294..313 belong to the adjacent Fender chart.
        for index, silver_rows, turn_signal_rows in ((39, (619, 704), (715, 738)), (41, (319, 402), (294, 313))):
            pixels = [(v[3] * 1254, (v[4] + 1) * 1254) for v in meshes[index].vertices]
            first, last = min(y for x, y in pixels), max(y for x, y in pixels)
            self.assertLess(first, silver_rows[0])
            self.assertGreater(last, silver_rows[1])
            self.assertTrue(last < turn_signal_rows[0] or first > turn_signal_rows[1])
            for original, fitted in zip(before[index], meshes[index].vertices):
                self.assertEqual(original[:3], fitted[:3])
                self.assertEqual(original[5:], fitted[5:])

    def test_original_glass_has_no_exact_black_key_pixels(self):
        entry = next(v for v in load_manifest()["vehicles"] if v["key"] == "honda")
        header, _, _, section = decode(read_source(entry["source"]))
        records = {n: p for n, _, p in images.texture_records(section, struct.unpack_from("<H", header, 16)[0])}
        for name in ("SIDEG", "WSCRN"):
            width, height, pixels = images.bmp_pixels(records[name])
            self.assertNotIn((0, 0, 0), pixels)
            result = honda.prepare_pixels(name, records[name], 2, 2, bytes((0, 0, 0, 255)) * 4)
            self.assertEqual(list(result[3::4]), [255] * 4)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_registered_hood_and_boot_edges_sample_paint_instead_of_dark_atlas_borders(self):
        entry = next(v for v in load_manifest()["vehicles"] if v["key"] == "honda")
        header, _, meshes, section = decode(read_source(entry["source"]))
        before = [[tuple(v) for v in mesh.vertices] for mesh in meshes]
        honda._fit_paint_edges(meshes, json.loads((PACKAGE / "metadata.json").read_text()))
        for index, mesh in enumerate(meshes):
            if index not in (13, 14, 47, 48):
                self.assertEqual(mesh.vertices, before[index])
            for original, fitted in zip(before[index], mesh.vertices):
                self.assertEqual(original[:3], fitted[:3])
                self.assertEqual(original[5:], fitted[5:])
        payload = (PACKAGE / "HONDA.webp").read_bytes()
        width, height = images.webp_dimensions(payload)
        rgba = subprocess.run([CONVERTER, "webp:-", "-depth", "8", "rgba:-"], input=payload,
                              check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

        def filtered_pixel(u, v):
            x, y = u * width - .5, v * height - .5
            ix, iy = math.floor(x), math.floor(y)
            result = [0.] * 4
            for ox, oy, weight in ((0, 0, (1 - (x - ix)) * (1 - (y - iy))), (1, 0, (x - ix) * (1 - (y - iy))),
                                   (0, 1, (1 - (x - ix)) * (y - iy)), (1, 1, (x - ix) * (y - iy))):
                offset = (((iy + oy) % height) * width + (ix + ox) % width) * 4
                for channel in range(4):
                    result[channel] += rgba[offset + channel] * weight
            return result

        # These are the actual canonical chart edges that produced the native
        # texture-only artifacts. Sample the final WebP with the renderer's
        # linear/repeat behavior, after the model's16.16 UV quantization.
        for index, a, b in ((13, 7, 8), (14, 3, 4), (47, 7, 4), (48, 7, 4)):
            endpoints = [(round(meshes[index].vertices[i][3] * 65536) / 65536,
                          round(meshes[index].vertices[i][4] * 65536) / 65536 + 1) for i in (a, b)]
            for step in range(129):
                t = step / 128
                uv = tuple(endpoints[0][k] + (endpoints[1][k] - endpoints[0][k]) * t for k in range(2))
                pixel = filtered_pixel(*uv)
                with self.subTest(mesh=index, step=step):
                    self.assertGreater(max(pixel[:3]), 150)
                    self.assertGreater(pixel[3], 250)


if __name__ == "__main__":
    unittest.main()
