#!/usr/bin/env python3
"""Generated Renault texture binding, bounded BMP input and format conversion."""

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
import zlib


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import renault_textures as textures
import vehicle_textures


def bmp(width=2, height=2, color=(0, 220, 255)):
    row = bytes(color) * width + bytes((-width * 3) % 4)
    data = bytearray(54 + len(row) * height)
    data[:2] = b"BM"
    struct.pack_into("<I4xIIiiHHI", data, 2, len(data), 54, 40, width, height, 1, 24, 0)
    data[54:] = row * height
    return bytes(data)


def record(name, data):
    return name.encode().ljust(12, b"\0") + struct.pack("<I", len(data)) + data


class RenaultTextureTests(unittest.TestCase):

    def test_named_replacement_preserves_order_names_and_other_payloads(self):
        original = record("REN", bmp()) + record("Num2", bmp())
        self.assertEqual(textures.replace_textures(original, 2), original)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            self.assertEqual(textures.replace_textures(original, 2, path), original)
            replacement = bmp(4, 4)
            (path / "REN.bmp").write_bytes(replacement)
            output = textures.texture_records(textures.replace_textures(original, 2, path), 2)
            self.assertEqual([r[0] for r in output], ["REN", "Num2"])
            self.assertEqual(output[0][2], replacement)
            self.assertEqual(output[1][2], bmp())

    def test_replacements_validate_dimensions_format_and_directory(self):
        original = record("REN", bmp())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            for invalid in (b"BM", bmp(3, 2), bmp(2049, 1)):
                (path / "REN.bmp").write_bytes(invalid)
                with self.assertRaises(ValueError):
                    textures.replace_textures(original, 1, path)
            (path / "REN.bmp").write_bytes(bmp())
            (path / "OTHER.bmp").write_bytes(bmp())
            with self.assertRaises(ValueError):
                textures.replace_textures(original, 1, path)
            (path / "OTHER.bmp").unlink()
            (path / "REN.bmp").unlink()
            outside = path.parent / (path.name + "-outside.bmp")
            outside.write_bytes(bmp())
            try:
                (path / "REN.bmp").symlink_to(outside)
                with self.assertRaises(ValueError):
                    textures.replace_textures(original, 1, path)
            finally:
                outside.unlink()

    @unittest.skipUnless(shutil.which("magick") or shutil.which("convert"), "ImageMagick is unavailable")
    def test_webp_encoding_preserves_native_mask_and_opaque_number_ink(self):
        pixels = [(0, 0, 0), (8, 8, 8), (255, 255, 255), (255, 220, 0)] * 4
        encoded = vehicle_textures.webp_texture(textures.rgb_bmp(4, 4, pixels))
        converter = shutil.which("magick") or shutil.which("convert")
        decoded = subprocess.run([converter, "webp:-", "-depth", "8", "rgba:-"], input=encoded,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
        self.assertEqual(len(decoded), 4 * 4 * 4)
        self.assertEqual(list(decoded[3::4]), [0, 255, 255, 255] * 4)

    @unittest.skipUnless(shutil.which("magick") or shutil.which("convert"), "ImageMagick is unavailable")
    def test_png_conversion_preserves_pixels_as_native_bmp24(self):
        def chunk(name, data):
            return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress((b"\0" + bytes([255, 220, 0]) * 2) * 2)) + chunk(b"IEND", b"")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "REN.png"
            source.write_bytes(png)
            result = textures.generated_bmp(source)
            self.assertEqual(textures.bmp_dimensions(result, replacement=True), (2, 2, 24))
            offset = struct.unpack_from("<I", result, 10)[0]
            self.assertEqual(result[offset:offset + 6], bytes([0, 220, 255]) * 2)

    def test_native_number_ink_and_name_background_use_distinct_colorkeys(self):
        image = textures.rgb_bmp(2, 2, [(0, 0, 0), (20, 18, 15), (255, 255, 255), (40, 41, 42)])
        for name in ("Num2", "Num22"):
            pixels = textures.bmp_pixels(textures.native_colorkey(name, image, image))[2]
            self.assertEqual(pixels, [(8, 8, 8), (8, 8, 8), (255, 255, 255), (40, 41, 42)])
        for name in ("ListA", "ListB"):
            pixels = textures.bmp_pixels(textures.native_colorkey(name, image, image))[2]
            self.assertEqual(pixels, [(0, 0, 0), (0, 0, 0), (255, 255, 255), (40, 41, 42)])

    def test_native_canonical_mask_scales_without_spurious_opaque_holes(self):
        original = textures.rgb_bmp(2, 2, [(0, 0, 0), (10, 20, 30), (40, 50, 60), (0, 0, 0)])
        generated = [(0, 0, 0)] * 16
        generated[0] = (255, 255, 255)
        generated[3] = (120, 80, 50)
        replacement = textures.rgb_bmp(4, 4, generated)
        expected = [(0, 0, 0), (0, 0, 0), (8, 8, 8), (120, 80, 50),
                    (0, 0, 0), (0, 0, 0), (8, 8, 8), (8, 8, 8),
                    (8, 8, 8), (8, 8, 8), (0, 0, 0), (0, 0, 0),
                    (8, 8, 8), (8, 8, 8), (0, 0, 0), (0, 0, 0)]
        for name in ("REN", "WSCRN", "SIDEG"):
            self.assertEqual(textures.bmp_pixels(textures.native_colorkey(name, original, replacement))[2], expected)

    def test_wheel_arch_releases_only_cut_face_footprints_and_keeps_hole_clear(self):
        vertices = [(0., 0., 0., .25, -.75), (1., 0., 0., .75, -.75), (0., 1., 0., .25, -.25)]
        face = struct.pack("<H3Hh4B10x", 0, 0, 1, 2, 0, 255, 255, 255, 255)
        cut = SimpleNamespace(vertices=vertices, faces=[face], wheel_arch_faces={0})
        unmarked = SimpleNamespace(vertices=vertices, faces=[face])
        footprints = textures.wheel_arch_opaque_uvs([cut, unmarked])
        self.assertEqual(footprints, [[(.25, .25), (.75, .25), (.25, .75)]])
        original = textures.rgb_bmp(2, 2, [(0, 0, 0)] * 4)
        pixels = [(255, 225, 2)] * 256
        pixels[6 * 16 + 6] = (0, 0, 0)
        generated = textures.rgb_bmp(16, 16, pixels)
        imported = textures.bmp_pixels(textures.native_colorkey("REN", original, generated, footprints))[2]
        self.assertEqual(imported[6 * 16 + 5], (255, 225, 2))
        self.assertEqual(imported[6 * 16 + 6], (8, 8, 8))
        self.assertEqual(imported[12 * 16 + 12], (0, 0, 0))  # Geometry opening, outside surviving face.
        self.assertEqual(imported[0], (0, 0, 0))
        self.assertEqual(textures.bmp_pixels(textures.native_colorkey("SIDEG", original, generated, footprints))[2],
                         [(0, 0, 0)] * 256)
        self.assertEqual(textures.wheel_arch_opaque_uvs([unmarked]), [])

    def test_wheel_arch_footprints_reject_invalid_markers_materials_and_uvs(self):
        vertices = [(0., 0., 0., .25, -.75), (1., 0., 0., .75, -.75), (0., 1., 0., .25, -.25)]
        face = struct.pack("<H3Hh4B10x", 0, 0, 1, 2, 0, 255, 255, 255, 255)
        for selected in ([0], {True}, {-1}, {1}):
            with self.assertRaises(ValueError):
                textures.wheel_arch_opaque_uvs([SimpleNamespace(vertices=vertices, faces=[face], wheel_arch_faces=selected)])
        for broken_face in (face[:10], face[:8] + b"\xff\xff" + face[10:], face[:2] + b"\x09\x00" + face[4:]):
            with self.assertRaises(ValueError):
                textures.wheel_arch_opaque_uvs([SimpleNamespace(vertices=vertices, faces=[broken_face], wheel_arch_faces={0})])
        for coordinate in (float("nan"), -.01, 1.01):
            bad = [(*vertices[0][:3], coordinate, -.75), *vertices[1:]]
            with self.assertRaises(ValueError):
                textures.wheel_arch_opaque_uvs([SimpleNamespace(vertices=bad, faces=[face], wheel_arch_faces={0})])

    def test_name_uv_metadata_fits_last_row_without_changing_geometry(self):
        section = record("ListA", bmp()) + record("Num2", bmp())
        vertices = [(1., 2., 3., .25, -.125, 0., 1., 0.), (4., 5., 6., .75, 0., 0., 1., 0.),
                    (7., 8., 9., .25, -.125, 0., 1., 0.)]
        face = struct.pack("<H3Hh4B10x", 0, 0, 1, 2, 0, 255, 255, 255, 255)
        mesh = SimpleNamespace(vertices=list(vertices), faces=[face])
        textures.fit_name_uvs([mesh], section, 2)
        self.assertEqual(mesh.vertices, vertices)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "ListA.bmp").write_bytes(bmp())
            (path / "metadata.json").write_text(json.dumps({"name_uv_ranges": {"ListA": [.895, .999]}}))
            textures.fit_name_uvs([mesh], section, 2, path)
            self.assertAlmostEqual(mesh.vertices[0][4] + 1, .895)
            self.assertAlmostEqual(mesh.vertices[1][4] + 1, .999)
            self.assertEqual(mesh.faces, [face])
            for before, after in zip(vertices, mesh.vertices):
                self.assertEqual(before[:4], after[:4])
                self.assertEqual(before[5:], after[5:])

    @unittest.skipUnless(shutil.which("magick") or shutil.which("convert"), "ImageMagick is required")
    def test_both_driver_name_slots_cover_complete_source_ink_on_actual_generated_uvs(self):
        from vehicle_manifest import load_definition, load_manifest
        from vehicle_model import decode, read_source
        from renault_geometry import generate

        entry = next(v for v in load_manifest()["vehicles"] if v["key"] == "renault")
        root = PROJECT / entry["texture_directory"]
        ranges = json.loads((root / "metadata.json").read_text())["name_uv_ranges"]
        self.assertEqual(ranges["ListA"], ranges["ListB"], "Swappable driver names require the same UV target interval")
        converter = shutil.which("magick") or shutil.which("convert")
        # Measure the final contiguous name line independently from its UV
        # metadata. Other names above it must not influence the coverage check.
        ink = {}
        for name, expected_rows in (("ListA", (1127, 1246)), ("ListB", (1133, 1247))):
            source = root / "work" / (name + ".webp")
            rgb = subprocess.run([converter, str(source), "-depth", "8", "rgb:-"], check=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
            width = height = 1254
            self.assertEqual(len(rgb), width * height * 3)
            bands, previous = [], -2
            for y in range(height * 3 // 4, height):
                row = rgb[y * width * 3:(y + 1) * width * 3]
                if max(row) < 128:
                    continue
                if y != previous + 1:
                    bands.append([y, y])
                else:
                    bands[-1][1] = y
                previous = y
            self.assertEqual(tuple(bands[-1]), expected_rows)
            ink[name] = (*bands[-1], height)

        generated, _ = generate(read_source(entry["source"]), load_definition(entry), root)
        header, node_bytes, meshes, section = decode(generated)
        names = [name for name, _, _ in textures.texture_records(section, struct.unpack_from("<H", header, 16)[0])]
        covered = set()
        offset = 0
        while offset < len(node_bytes):
            size = struct.unpack_from("<I", node_bytes, offset)[0]
            node_id = struct.unpack_from("<I", node_bytes, offset + 72)[0] & 0xffffff
            variants = struct.unpack_from(f"<{node_bytes[offset + 76]}H", node_bytes, offset + 80)
            if node_id in (15, 16, 21):
                for mesh_index in set(variants) - {65535}:
                    mesh = meshes[mesh_index]
                    vs = [mesh.vertices[index][4] + 1 for face in mesh.faces
                          if struct.unpack_from("<h", face, 8)[0] >= 0 and
                          names[struct.unpack_from("<h", face, 8)[0]] in ("ListA", "ListB")
                          for index in struct.unpack_from("<3H", face, 2)]
                    if not vs:
                        continue
                    # Recovered49a780 gives every name-bearing panel ListA
                    # for the first driver and ListB for the second driver.
                    # Test both destinations against the actual quantized UVs,
                    # including the damaged variants that still carry names.
                    for target in ("ListA", "ListB"):
                        first, last, height = ink[target]
                        with self.subTest(node=node_id, mesh=mesh_index, target=target):
                            self.assertLessEqual(min(vs) * height + 1, first)
                            self.assertGreaterEqual(max(vs) * height - 1, last + 1)
                        covered.add((node_id, target))
            offset += size
        self.assertEqual(covered, {(node, target) for node in (15, 16, 21) for target in ("ListA", "ListB")})

    def test_rear_lamp_uses_one_contour_across_fold_and_preserves_surrounding_paint(self):
        # A rear plane and a perpendicular side plane, with deliberately
        # disconnected old atlas islands but a single physical lamp fold.
        points = [(1., 0., 0.), (0., 0., 0.), (0., 1., 0.), (1., 1., 0.),
                  (1., 0., .5), (1., 1., 0.), (1., 1., 1.), (1., 0., 0.), (1., 0., 1.)]
        vertices = []
        for i, (x, y, z) in enumerate(points):
            u = .1 + .2 * x if i < 4 else .6 + .2 * z
            vertices.append((x, y, z, u, -.6 - .2 * y, x + 1, y + 1, z + 1))
        charts = [[2, 1, 0], [3, 2, 0], [6, 5, 4], [7, 4, 5], [8, 6, 4]]
        faces = [struct.pack("<H3Hh4B10x", 0, *ids, 0, 255, 240, 230, 255) for ids in charts]
        meshes = [SimpleNamespace(vertices=list(vertices), faces=list(faces)) for _ in range(20)]
        entry = {"paths": [[2, 3, 6], [1, 0, 4, 8]], "charts": charts,
                 "target_outline": [[.15, .21], [.85, .21], [.95, .23], [.95, .37],
                                    [.85, .39], [.15, .39], [.05, .37], [.05, .23]],
                 "sprite_outline": [[.42, .5], [.53, .5], [.55, .52], [.55, .63],
                                    [.53, .65], [.42, .65], [.4, .63], [.4, .52]]}
        opposite = {**entry, "sprite_outline": [[.95 - u, v] for u, v in entry["sprite_outline"]]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "metadata.json").write_text(json.dumps({"rear_lamp_uvs": {"16": entry, "18": opposite}}))
            textures.fit_rear_lamp_uvs(meshes, path)
            self.assertEqual(meshes[16].faces, faces)
            (path / "REN.bmp").write_bytes(bmp())
            textures.fit_rear_lamp_uvs(meshes, path)
        for index, mesh in enumerate(meshes):
            if index not in (16, 18):
                self.assertEqual(mesh.vertices, vertices)
                self.assertEqual(mesh.faces, faces)
        mesh = meshes[16]
        self.assertGreater(len(mesh.faces), len(faces))
        area = 0
        seam = {}
        glass_vertices = paint_vertices = 0
        for face in mesh.faces:
            self.assertEqual(face[:2] + face[8:], faces[0][:2] + faces[0][8:])
            a, b, c = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            ab, ac = [tuple(b[k] - a[k] for k in range(3)), tuple(c[k] - a[k] for k in range(3))]
            normal = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
            area += math.sqrt(sum(v * v for v in normal)) / 2
            for vertex in (a, b, c):
                x, y, z, u, v, nx, ny, nz = vertex
                self.assertAlmostEqual(nx, x + 1)
                self.assertAlmostEqual(ny, y + 1)
                self.assertAlmostEqual(nz, z + 1)
                self.assertTrue(abs(z) < 1e-9 or abs(x - 1) < 1e-9)
                if .4 - 1e-9 <= u <= .55 + 1e-9 and .5 - 1e-9 <= v + 1 <= .65 + 1e-9:
                    glass_vertices += 1
                    if abs(x - 1) < 1e-9 and abs(z) < 1e-9:
                        seam.setdefault(round(y, 10), set()).add((round(u, 10), round(v, 10)))
                else:
                    paint_vertices += 1
                    if abs(y - 1) < 1e-9:
                        self.assertAlmostEqual(v + 1, .2)
        self.assertAlmostEqual(area, 2)
        self.assertGreater(glass_vertices, 20)
        self.assertGreater(paint_vertices, 20)
        self.assertGreater(len(seam), 1)
        self.assertTrue(all(len(uvs) == 1 for uvs in seam.values()))
        for a, b in zip(meshes[16].vertices, meshes[18].vertices):
            self.assertEqual(a[:3], b[:3])
            if .4 - 1e-9 <= a[3] <= .55 + 1e-9 and .5 - 1e-9 <= a[4] + 1 <= .65 + 1e-9:
                self.assertAlmostEqual(a[3] + b[3], .95)
                self.assertAlmostEqual(a[4], b[4])

    def test_rear_lamp_rejects_invalid_contours_paths_and_damage_islands(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "REN.bmp").write_bytes(bmp())
            for entry in ({}, {"paths": [[], []]}, {"sprite_outline": [[float("nan"), 0]]}):
                (path / "metadata.json").write_text(json.dumps({"rear_lamp_uvs": {"16": entry}}))
                with self.assertRaises(ValueError):
                    textures.fit_rear_lamp_uvs([], path)
            (path / "metadata.json").write_text(json.dumps({"rear_lamp_uvs": {"17": {}}}))
            with self.assertRaises(ValueError):
                textures.fit_rear_lamp_uvs([], path)

if __name__ == "__main__":
    unittest.main()
