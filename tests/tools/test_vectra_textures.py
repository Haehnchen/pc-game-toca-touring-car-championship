#!/usr/bin/env python3
"""Vectra driver-row coverage, exact payload embedding and source-bound opacity."""

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
import vectra_textures as vectra
import vehicle_textures as images
from vehicle_manifest import load_definition
from vehicle_model import decode, read_source

PACKAGE = PROJECT / "assets/models/vehicles/vectra/textures"
CONVERTER = shutil.which("magick") or shutil.which("convert")


def record(name, payload):
    return name.encode().ljust(12, b"\0") + struct.pack("<I", len(payload)) + payload


def rgba(path):
    return subprocess.run([CONVERTER, str(path), "-depth", "8", "rgba:-"], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


class VectraTextureTests(unittest.TestCase):
    def test_exact_black_key_does_not_erase_near_black_glass(self):
        original = images.rgb_bmp(2, 2, [(0, 0, 0), (0, 8, 8), (0, 0, 8), (255, 255, 255)])
        result = vectra.prepare_pixels("WSCRN", original, 2, 2, bytes((0, 0, 0, 255)) * 4)
        self.assertEqual(list(result[3::4]), [0, 255, 255, 255])
        self.assertEqual(result[4:8], bytes((8, 8, 8, 255)))

    def test_opacity_release_is_confined_to_actual_body_footprints(self):
        original = images.rgb_bmp(2, 2, [(0, 0, 0)] * 4)
        pixels = bytes((255, 255, 255, 255)) * 256
        footprint = [[(.25, .25), (.75, .25), (.25, .75)]]
        body = vectra.prepare_pixels("VECTRA", original, 16, 16, pixels, footprint)
        self.assertEqual(body[(6 * 16 + 5) * 4 + 3], 255)
        self.assertEqual(body[(12 * 16 + 12) * 4 + 3], 0)
        glass = vectra.prepare_pixels("SIDEG", original, 16, 16, pixels, footprint)
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
        for name in ("Num8",):
            result = vectra.prepare_pixels(name, b"", width, height, pixels)
            self.assertEqual(result[(2 * width + 2) * 4:(2 * width + 2) * 4 + 4], bytes((8, 8, 8, 255)))
            self.assertEqual(result[edge:edge + 4], bytes((255, 255, 255, 64)))
            self.assertEqual(result[noise:noise + 4], bytes(4))

    def test_single_cleland_slot_fits_the_third_row(self):
        original = images.rgb_bmp(2, 2, [(255, 255, 255)] * 4)
        mesh = SimpleNamespace(vertices=[(1., 2., 3., 0., -.75, 0., 1., 0.), (2., 2., 3., 1., -.625, 0., 1., 0.)],
                               faces=[struct.pack("<H3Hh4B10x", 0, 0, 1, 0, 0, 255, 255, 255, 255)])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "ListA.bmp").write_bytes(original)
            (root / "metadata.json").write_text(json.dumps({"name_uv_ranges": {"ListA": [.26, .39]}}))
            vectra.fit_uvs([mesh], record("ListA", original), 1, root)
            self.assertEqual(mesh.vertices[0][:4], (1., 2., 3., 0.))
            self.assertAlmostEqual(mesh.vertices[0][4] + 1, .26)
            self.assertAlmostEqual(mesh.vertices[1][4] + 1, .39)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_final_webp_bytes_bypass_preparation_and_lossy_encoding(self):
        original = images.rgb_bmp(2, 2, [(255, 255, 255)] * 4)
        final = images.webp_texture(original)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "VECTRA.webp").write_bytes(final)
            (root / "metadata.json").write_text("{}")
            with patch.object(vectra, "prepare_artwork", side_effect=AssertionError("Final payload re-encoded")):
                replaced = vectra.replace_textures(record("VECTRA", original), 1, root)
            self.assertEqual(images.texture_records(replaced, 1)[0][2], final)


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_actual_quantized_name_faces_cover_cleland_and_no_neighbor_row(self):
        from vectra_geometry import generate
        entry = vectra._entry()
        rgb = subprocess.run([CONVERTER, str(PACKAGE / "work/ListA.webp"), "-filter", "Lanczos", "-resize", "512x512!", "-depth", "8", "rgb:-"],
                             check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        bands, previous = [], -2
        for y in range(260):
            if max(rgb[y * 512 * 3:(y + 1) * 512 * 3]) < 128:
                continue
            if y != previous + 1:
                bands.append([y, y])
            else:
                bands[-1][1] = y
            previous = y
        self.assertEqual(bands, [[7, 63], [72, 128], [139, 193], [202, 257]])
        data, _ = generate(read_source(entry["source"]), load_definition(entry), PACKAGE)
        _, _, meshes, _ = decode(data)
        for index in (8, 9, 11, 12, 37, 38):
            values = [meshes[index].vertices[i][4] + 1 for face in meshes[index].faces if struct.unpack_from("<h", face, 8)[0] == 5
                      for i in struct.unpack_from("<3H", face, 2)]
            self.assertTrue(values)
            with self.subTest(mesh=index):
                self.assertGreaterEqual(min(values) * 512, 129.5)
                self.assertLessEqual(min(values) * 512, 139.5)
                self.assertGreaterEqual(max(values) * 512, 193.5)
                self.assertLessEqual(max(values) * 512, 201.5)

    def test_glass_damage_regions_keep_original_opaque_near_black_behavior(self):
        header, _, _, section = decode(read_source(vectra._entry()["source"]))
        records = {n: p for n, _, p in images.texture_records(section, struct.unpack_from("<H", header, 16)[0])}
        for name in ("SIDEG", "WSCRN"):
            self.assertNotIn((0, 0, 0), images.bmp_pixels(records[name])[2])
            result = vectra.prepare_pixels(name, records[name], 2, 2, bytes((0, 0, 0, 255)) * 4)
            self.assertEqual(list(result[3::4]), [255] * 4)

    def test_name_fit_preserves_full_paint_and_framed_lamp_charts(self):
        header, _, meshes, section = decode(read_source(vectra._entry()["source"]))
        before = [[tuple(v) for v in mesh.vertices] for mesh in meshes]
        vectra.fit_uvs(meshes, section, struct.unpack_from("<H", header, 16)[0], PACKAGE)
        count = 0
        for index, mesh in enumerate(meshes):
            for original, fitted in zip(before[index], mesh.vertices):
                self.assertEqual(original[:4], fitted[:4])
                self.assertEqual(original[5:], fitted[5:])
                if original[4] != fitted[4]:
                    self.assertIn(index, (8, 9, 11, 12, 37, 38))
                    count += 1
            if index in (14, 40, 41, 42, 43, 44, 45, 46, 47):
                self.assertEqual(before[index], mesh.vertices)
        self.assertGreater(count, 0)

    def test_hidden_slots_remain_canonical_and_v10_media_is_distinct(self):
        from vectra_geometry import generate
        entry = vectra._entry()
        source = read_source(entry["source"])
        _, _, _, original_section = decode(source)
        generated, _ = generate(source, load_definition(entry), PACKAGE)
        _, _, _, final_section = decode(generated)
        original = images.texture_records(original_section, 7)
        final = images.texture_records(final_section, 7)
        self.assertEqual(tuple(n for n, _, _ in original), vectra.SLOTS)
        for slot in (1, 2):
            self.assertEqual(final[slot], original[slot])

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_final_number_glyphs_have_opaque_black_ink_and_empty_free_canvas(self):
        for name in ("Num8",):
            data = rgba(PACKAGE / (name + ".webp"))
            self.assertEqual(len(data), 256 * 256 * 4)
            self.assertTrue(any(data[i + 3] == 255 and max(data[i:i + 3]) < 32 for i in range(0, len(data), 4)))
            self.assertTrue(any(data[i + 3] == 255 and min(data[i:i + 3]) > 220 for i in range(0, len(data), 4)))
            # Measured free corners and the two genuine counters of the slanted8.
            for x, y in ((5, 5), (250, 5), (5, 250), (250, 250), (156, 78), (116, 169)):
                self.assertEqual(data[(y * 256 + x) * 4 + 3], 0, (x, y))
            alpha = data[3::4]
            occupied = [i for i, value in enumerate(alpha) if value >= 128]
            self.assertGreater(len(occupied), 10000)
            self.assertTrue(all(25 <= i % 256 <= 221 and 25 <= i // 256 <= 222 for i in occupied))

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_mobil1_ink_order_matches_both_canonical_side_orientations(self):
        header, _, _, section = decode(read_source(vectra._entry()["source"]))
        original = images.texture_records(section, struct.unpack_from("<H", header, 16)[0])[0][2]
        width, _, colors = images.bmp_pixels(original)
        source = bytes(channel for color in colors for channel in color)
        raw = subprocess.run([CONVERTER, str(PACKAGE / "work/VECTRA.webp"), "-depth", "8", "rgb:-"],
                             check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        final = subprocess.run([CONVERTER, str(PACKAGE / "VECTRA.webp"), "-depth", "8", "rgb:-"],
                               check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        # Blue-letter and dark1 ink order is directly measurable even in256px source.
        # The upper negative-Z chart is180degrees; lower positive-Z chart is upright.
        for data, size, upper, lower in ((source, width, (26, 29, 58, 40), (25, 215, 58, 226)),
                                         (raw, 1254, (127, 147, 281, 198), (122, 1057, 279, 1113)),
                                         (final, 1024, (104, 120, 229, 162), (100, 863, 228, 909))):
            for rotated, bounds in ((True, upper), (False, lower)):
                x0, y0, x1, y1 = bounds
                blue, dark = [], []
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        red, green, color_blue = data[(y * size + x) * 3:(y * size + x) * 3 + 3]
                        if color_blue > red + 15 and color_blue > green + 10:
                            blue.append((x - x0) / (x1 - x0))
                        if max(red, green, color_blue) < 50:
                            dark.append((x - x0) / (x1 - x0))
                self.assertTrue(blue and dark)
                blue_center, dark_center = sum(blue) / len(blue), sum(dark) / len(dark)
                with self.subTest(size=size, upper=rotated):
                    if rotated:
                        self.assertGreater(blue_center, .5)
                        self.assertLess(dark_center, blue_center)
                    else:
                        self.assertLess(blue_center, .5)
                        self.assertGreater(dark_center, blue_center)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_measured_lower_arch_sampling_lanes_avoid_canvas_in_raw_and_final(self):
        regions = json.loads((PACKAGE / "metadata.json").read_text())["lower_arch_paint_uvs"]
        self.assertEqual({(r["side"], r["arch"]) for r in regions}, {(-1, 0), (-1, 1), (1, 0), (1, 1)})
        for name, width, minimum in (("work/VECTRA.webp", 1254, 70), ("VECTRA.webp", 1024, 60)):
            data = rgba(PACKAGE / name)
            tested = 0
            for region in regions:
                rows = region["rows"]
                for step in range(113):
                    y = 8 + step / 8
                    for a, b in zip(rows, rows[1:]):
                        if a[0] <= y <= b[0]:
                            t = (y - a[0]) / (b[0] - a[0])
                            edges = [a[k] * (1 - t) + b[k] * t for k in (1, 2)]
                            break
                    for x in edges:
                        u, v = (x, y) if region["side"] < 0 else (x + 2.248, 255.99 - y)
                        px, py = u * width / 256 - .5, v * width / 256 - .5
                        ix, iy = math.floor(px), math.floor(py)
                        fx, fy = px - ix, py - iy
                        channels = [0.] * 4
                        for dx, dy, weight in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                                               (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
                            offset = ((iy + dy) * width + ix + dx) * 4
                            for channel in range(4):
                                channels[channel] += data[offset + channel] * weight
                        with self.subTest(file=name, side=region["side"], arch=region["arch"], y=y, x=x):
                            self.assertGreater(max(channels[:3]), minimum)
                            self.assertGreater(channels[3], 254.9)
                        tested += 1
            self.assertEqual(tested, 904)

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_actual_lower_arch_fade_faces_keep_alpha_through_row26(self):
        import vectra_geometry as geometry
        entry = vectra._entry()
        definition = load_definition(entry)
        _, _, meshes, _ = geometry.prepare(read_source(entry["source"]), definition)
        geometry.register_lower_arch_paint(meshes, definition, PACKAGE)
        data = rgba(PACKAGE / "VECTRA.webp")
        covered, samples = {}, 0
        self.fade_pixel_audit = []
        weights = ((1., 0., 0.), (0., 1., 0.), (0., 0., 1.), (1 / 3, 1 / 3, 1 / 3),
                   (.5, .5, 0.), (.5, 0., .5), (0., .5, .5))
        for mesh in meshes:
            for index in getattr(mesh, "lower_arch_faces", ()):
                vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", mesh.faces[index], 2)]
                for weight in weights:
                    u, v = (sum(weight[j] * vertices[j][k] for j in range(3)) for k in (3, 4))
                    side = 1 if sum(p[2] for p in vertices) > 0 else -1
                    x, y = (u * 256, (v + 1) * 256) if side < 0 else (u * 256 - 2.248, 255.99 - (v + 1) * 256)
                    if not 22 - 1e-5 <= y <= 26 + 1e-5:
                        continue
                    px, py = u * 1024 - .5, (v + 1) * 1024 - .5
                    ix, iy = math.floor(px), math.floor(py)
                    fx, fy = px - ix, py - iy
                    channels = [0.] * 4
                    for dx, dy, sample_weight in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                                                  (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
                        offset = ((iy + dy) * 1024 + ix + dx) * 4
                        for channel in range(4):
                            channels[channel] += data[offset + channel] * sample_weight
                    arch = 0 if x < 128 else 1
                    edge = 0 if x < definition["wheel_arches"][arch][0] else 1
                    with self.subTest(side=side, arch=arch, edge=edge, y=y, x=x):
                        # The fade returns to original UVs at26, including the existing dark rim.
                        # Bilinear rim samples retain at least95% coverage; native QA owns the visible rim shape.
                        self.assertGreater(channels[3], .95 * 255)
                    self.fade_pixel_audit.append({"side": side, "arch": arch, "edge": edge, "x": x, "y": y,
                                                  "max_rgb": max(channels[:3]), "alpha": channels[3]})
                    span = covered.setdefault((side, arch, edge), [y, y])
                    span[0], span[1] = min(span[0], y), max(span[1], y)
                    samples += 1
        self.assertEqual(set(covered), {(side, arch, edge) for side in (-1, 1) for arch in (0, 1) for edge in (0, 1)})
        for span in covered.values():
            self.assertLessEqual(span[0], 22.01)
            self.assertGreaterEqual(span[1], 25.99)
        self.assertGreater(samples, 100)


if __name__ == "__main__":
    unittest.main()
