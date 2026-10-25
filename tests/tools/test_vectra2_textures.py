#!/usr/bin/env python3
"""Warwick source identity, two-digit alpha and reproducible V10 artwork."""

from pathlib import Path
import json
import math
import shutil
import struct
import subprocess
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import vectra_textures as vectra
import vehicle_textures as images
from vehicle_manifest import load_definition
from vehicle_model import decode, read_source

PACKAGE = PROJECT / "assets/models/vehicles/vectra2/textures"
CONVERTER = shutil.which("magick") or shutil.which("convert")


class Vectra2TextureTests(unittest.TestCase):
    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_front_and_side_headlamp_frames_join_at_physical_fold(self):
        from vectra_geometry import generate
        from vehicle_model import dot, sub

        entry = vectra._entry("vectra2")
        source = read_source(entry["source"])
        _, _, originals, _ = decode(source)
        data, _ = generate(source, load_definition(entry), PACKAGE)
        _, _, meshes, _ = decode(data)
        rgba = subprocess.run([CONVERTER, str(PACKAGE / "VECTRA2.webp"), "-depth", "8", "rgba:-"], check=True, stdout=subprocess.PIPE).stdout

        def texel(uv):
            px, py = uv[0] * 1024 - .5, (uv[1] + 1) * 1024 - .5
            x, y = math.floor(px), math.floor(py)
            fx, fy = px - x, py - y
            return tuple(sum(rgba[((y + dy) * 1024 + x + dx) * 4 + channel] * weight
                             for dx, dy, weight in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                                                    (0, 1, (1 - fx) * fy), (1, 1, fx * fy)))
                         for channel in range(4))

        def panel_uv(mesh, point, chart=None):
            candidates = []
            for face in mesh.faces:
                if struct.unpack_from("<h", face, 8)[0] != 0:
                    continue
                triangle = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                if chart == "side" and any(v[3] > .2 for v in triangle):
                    continue
                if chart == "front" and any(v[3] < .3 for v in triangle):
                    continue
                a, b, c = triangle
                ab, ac, ap = sub(b[:3], a[:3]), sub(c[:3], a[:3]), sub(point, a[:3])
                aa, cc, bc = dot(ab, ab), dot(ac, ac), dot(ab, ac)
                denominator = aa * cc - bc * bc
                if denominator < 1e-18:
                    continue
                wa = (cc * dot(ap, ab) - bc * dot(ap, ac)) / denominator
                wb = (aa * dot(ap, ac) - bc * dot(ap, ab)) / denominator
                weights = 1 - wa - wb, wa, wb
                if min(weights) < -.0002:
                    continue
                projected = tuple(sum(w * v[k] for w, v in zip(weights, triangle)) for k in range(3))
                error = math.dist(point, projected)
                uv = tuple(sum(w * v[k] for w, v in zip(weights, triangle)) for k in (3, 4))
                candidates.append((error, -min(weights), uv))
            self.assertTrue(candidates, point)
            error, _, uv = min(candidates)
            self.assertLess(error, 2 / 65536, point)
            return uv

        for side, front in ((4, 44), (0, 46)):
            # Derive the physical seam from the canonical panels, then sample
            # actual quantized generated triangles on either side of that edge.
            shared = sorted({v[:3] for v in originals[side].vertices} & {v[:3] for v in originals[front].vertices})
            self.assertEqual(len(shared), 2)
            a, b = sorted(shared, key=lambda p: p[1], reverse=True)
            dark = [[], []]
            for step in range(201):
                t = step / 200
                point = tuple(x * (1 - t) + y * t for x, y in zip(a, b))
                left, right = (texel(panel_uv(meshes[index], point)) for index in (side, front))
                self.assertGreater(min(left[3], right[3]), 254.9)
                for values, color in zip(dark, (left, right)):
                    if max(color[:3]) < 180:
                        values.append(t)
            self.assertTrue(all(dark))
            # Independently drawn glass reflections can differ. The visible
            # dark frame must start and end at the same physical fold heights.
            # This catches the inherited white top step without forcing the
            # side oval into a stretched copy of the front reflector.
            for end in (min, max):
                mismatch = abs(end(dark[0]) - end(dark[1])) * math.dist(a, b)
                self.assertLess(mismatch, .003, (side, front, end.__name__, dark[0][0], dark[1][0], dark[0][-1], dark[1][-1]))
            # The front lens continues onto the bumper below the separate
            # lamp mesh. Inspect both UV copies of that actual shared edge;
            # stopping at the lamp endpoint hides a second lower black rim.
            side_ids, front_ids = ((4, 1), (18, 12)) if side == 4 else ((9, 5), (30, 24))
            bumper = originals[26]
            edge = [bumper.vertices[i][:3] for i in side_ids]
            self.assertEqual(edge, [bumper.vertices[i][:3] for i in front_ids])
            self.assertEqual(edge[0], b)
            side_positions = {v[:3] for v in meshes[26].vertices if v[3] < .2}
            front_positions = {v[:3] for v in meshes[26].vertices if v[3] > .3}
            direction = sub(edge[1], edge[0])
            length2 = dot(direction, direction)

            def on_fold(point):
                t = dot(sub(point, edge[0]), direction) / length2
                projected = tuple(a + t * d for a, d in zip(edge[0], direction))
                return -1e-5 <= t <= 1.00001 and math.dist(point, projected) < .005

            path = sorted((p for p in side_positions & front_positions if on_fold(p)), key=lambda p: p[1], reverse=True)
            self.assertEqual((path[0], path[-1]), tuple(edge))
            lengths = [math.dist(a, b) for a, b in zip(path, path[1:])]
            length = sum(lengths)
            last_dark = [0., 0.]
            for step in range(201):
                distance = length * step / 200
                for a, b, span in zip(path, path[1:], lengths):
                    if distance <= span + 1e-10:
                        t = distance / span
                        point = tuple(x * (1 - t) + y * t for x, y in zip(a, b))
                        break
                    distance -= span
                for index, chart in enumerate(("side", "front")):
                    color = texel(panel_uv(meshes[26], point, chart))
                    self.assertGreater(color[3], 254.9)
                    if max(color[:3]) < 180:
                        last_dark[index] = length * step / 200
            mismatch = abs(last_dark[0] - last_dark[1])
            self.assertLess(mismatch, .003, (side, front, "bumper lower frame", last_dark))

    @unittest.skipUnless(CONVERTER, "ImageMagick required")
    def test_lower_arch_paint_lanes_remain_opaque_after_bilinear_sampling(self):
        metadata = json.loads((PACKAGE / "metadata.json").read_text())
        rgba = subprocess.run([CONVERTER, str(PACKAGE / "VECTRA2.webp"), "-depth", "8", "rgba:-"], check=True, stdout=subprocess.PIPE).stdout
        samples = 0
        for region in metadata["lower_arch_paint_uvs"]:
            for step in range(113):
                y = 8 + step / 8
                for a, b in zip(region["rows"], region["rows"][1:]):
                    if a[0] <= y <= b[0]:
                        t = (y - a[0]) / (b[0] - a[0])
                        edges = [a[k] * (1 - t) + b[k] * t for k in (1, 2)]
                        break
                for x in edges:
                    u, v = (x, y) if region["side"] < 0 else (x + 2.248, 255.99 - y)
                    px, py = u * 4 - .5, v * 4 - .5
                    ix, iy = math.floor(px), math.floor(py)
                    fx, fy = px - ix, py - iy
                    alpha = sum(rgba[((iy + dy) * 1024 + ix + dx) * 4 + 3] * weight
                                for dx, dy, weight in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                                                       (0, 1, (1 - fx) * fy), (1, 1, fx * fy)))
                    self.assertGreater(alpha, 254.9, (region["side"], region["arch"], x, y))
                    samples += 1
        self.assertEqual(samples, 904)

    def test_two_separate_digit_bodies_survive_noise_cleanup(self):
        width, height = 20, 10
        pixels = bytearray(width * height * 4)
        for x0 in (2, 11):
            for y in range(2, 7):
                for x in range(x0, x0 + 5):
                    offset = (y * width + x) * 4
                    pixels[offset:offset + 4] = bytes((0, 0, 0, 254))
        pixels[-4:] = bytes((255, 255, 255, 255))
        result = vectra.prepare_pixels("Num88", b"", width, height, pixels)
        for x in (4, 13):
            self.assertEqual(result[(4 * width + x) * 4:(4 * width + x) * 4 + 4], bytes((8, 8, 8, 255)))
        self.assertEqual(result[-4:], bytes(4))


    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_source_name_row_and_both_number_glyphs(self):
        from vectra_geometry import generate
        entry = vectra._entry("vectra2")
        data, _ = generate(read_source(entry["source"]), load_definition(entry), PACKAGE)
        _, _, meshes, section = decode(data)
        records = images.texture_records(section, 7)
        self.assertEqual(tuple(n for n, _, _ in records), vectra.VECTRA2_SLOTS)
        for name, _, payload in records:
            if (PACKAGE / (name + ".webp")).is_file():
                self.assertEqual(payload, (PACKAGE / (name + ".webp")).read_bytes())
        for index in (8, 9, 11, 12, 37, 38):
            values = [meshes[index].vertices[i][4] + 1 for f in meshes[index].faces if struct.unpack_from("<h", f, 8)[0] == 5 for i in struct.unpack_from("<3H", f, 2)]
            self.assertGreaterEqual(min(values) * 512, 127)
            self.assertLessEqual(min(values) * 512, 136)
            self.assertGreaterEqual(max(values) * 512, 191)
            self.assertLessEqual(max(values) * 512, 199)
        rgba = subprocess.run([CONVERTER, str(PACKAGE / "Num88.webp"), "-depth", "8", "rgba:-"], check=True, stdout=subprocess.PIPE).stdout
        for x0, x1 in ((0, 128), (128, 256)):
            ink = sum(rgba[(y * 256 + x) * 4 + 3] == 255 and max(rgba[(y * 256 + x) * 4:(y * 256 + x) * 4 + 3]) < 32 for y in range(256) for x in range(x0, x1))
            self.assertGreater(ink, 3000)
        for x, y in ((5, 5), (250, 5), (5, 250), (250, 250), (90, 90), (198, 90), (60, 160), (170, 160)):
            self.assertEqual(rgba[(y * 256 + x) * 4 + 3], 0, (x, y))

    @unittest.skipUnless(CONVERTER, "ImageMagick is required")
    def test_finalization_reproduces_every_embedded_webp_byte(self):
        self.assertTrue(vectra.finalize(True, "vectra2")["verified"])


if __name__ == "__main__":
    unittest.main()
