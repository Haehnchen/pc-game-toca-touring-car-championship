#!/usr/bin/env python3
"""Bounded Croft/Snet woodland volumes, source bindings and alpha profiles."""

from collections import Counter
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import configured_data_root
import checksum
import mega_woodland as woodland
import vehicle_model as geometry


def position(vertex):
    return tuple(round(value, 7) for value in vertex[:3])


def snet_source_strip():
    """Independent nine-vertex full panel followed by an upper end triangle."""
    mesh = geometry.Mesh(b"TREES_20\0".ljust(48, b"\0"), [], [])
    c, b, a_top = (-10., .2, 5.), (0., .4, 0.), (15., 4.1, 0.)
    up = (0., 4., 0.)
    full_normal, end_normal = woodland.unit((-5., 0., -10.)), (0., 0., -1.)
    for point, uv, normal in (
        (a_top, (1., -1.), end_normal), (woodland.add(b, up), (0., -1.), end_normal),
        (b, (0., 0.), end_normal), (woodland.add(b, up), (1., -1.), full_normal),
        (woodland.add(c, up), (0., -1.), full_normal), (c, (0., 0.), full_normal),
        (c, (0., 0.), full_normal), (b, (1., 0.), full_normal),
        (woodland.add(b, up), (1., -1.), full_normal),
    ):
        mesh.vertex(point, normal, uv)
    for indices in ((2, 1, 0), (5, 4, 3), (8, 7, 6)):
        mesh.triangle(indices, (167, 167, 167, 255), 1)
    return mesh


def shell_measurements(mesh):
    edges = Counter()
    signed_volume = 0.
    for face in mesh.faces:
        indices = struct.unpack_from("<3H", face, 2)
        points = [position(mesh.vertices[index]) for index in indices]
        normal = woodland.cross(woodland.sub(points[1], points[0]), woodland.sub(points[2], points[0]))
        if woodland.dot(normal, normal) <= 1e-16:
            raise AssertionError("Woodland emitted a degenerate triangle")
        signed_volume += woodland.dot(points[0], woodland.cross(points[1], points[2])) / 6
        for a, b in zip(points, points[1:] + points[:1]):
            edges[(a, b)] += 1
    return edges, signed_volume


class SnetWoodlandGeometryTests(unittest.TestCase):
    def test_upper_triangle_and_sloped_foot_keep_their_source_domain(self):
        source = snet_source_strip()
        before = source.encode()
        result = woodland._rebuild_snet(source, geometry.Mesh)
        count = 6
        for panel_index, panel in enumerate(woodland._snet_panels(source)):
            for side in range(2):
                for column in range(2):
                    u = float(column)
                    lower = u if panel.rising_base else 0.
                    base = woodland.interpolate(panel.left, panel.right, u)
                    up = woodland.interpolate(panel.up_left, panel.up_right, u)
                    for row in range(3):
                        vertex = result.vertices[panel_index * count * 2 + side * count + column * 3 + row]
                        self.assertAlmostEqual(vertex[3], u)
                        self.assertGreaterEqual(-vertex[4], lower)
                        self.assertLessEqual(-vertex[4], 1.)
                        center = woodland.add(base, woodland.scale(up, -vertex[4]))
                        self.assertAlmostEqual(vertex[1], center[1], places=10)
                        distance = math.dist(vertex[:3], center)
                        self.assertLessEqual(distance, woodland.SNET_DEPTH * .5 * (1 - lower) + 1e-10)
                        if row in (0, 2):
                            self.assertLess(distance, 1e-10)
                        elif lower < 1:
                            self.assertGreater(distance, .01 * (1 - lower))
                        if panel.rising_base and column == 1:
                            self.assertEqual(position(vertex), position(source.vertices[0]))
        self.assertEqual(source.encode(), before)
        self.assertEqual(result.header, source.header)
        for face in result.faces:
            self.assertEqual(struct.unpack_from("<h", face, 8)[0], 1)
            self.assertEqual(tuple(face[10:14]), (167, 167, 167, 255))
        for vertex in result.vertices:
            self.assertTrue(all(math.isfinite(value) for value in vertex))
            self.assertAlmostEqual(math.sqrt(woodland.dot(vertex[5:], vertex[5:])), 1., places=8)

    def test_simple_body_is_closed_and_reproducible(self):
        source = snet_source_strip()
        result = woodland._rebuild_snet(source, geometry.Mesh)
        self.assertEqual(result.encode(), woodland._rebuild_snet(source, geometry.Mesh).encode())
        self.assertEqual(len(result.faces), 14)
        edges, volume = shell_measurements(result)
        self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
        self.assertGreater(volume, 20.)
        self.assertLess(volume, 80.)


class WoodlandArtworkTests(unittest.TestCase):
    def test_oulton_reuses_accepted_dense_llrts1(self):
        self.assertEqual(checksum.crc32_file(PROJECT / "assets/models/mega/textures/hedge-a.webp"), woodland.OULTON_ARTWORK_CRC32)

    def test_croft_reuses_the_accepted_sap1_artwork(self):
        path = PROJECT / "assets/models/mega/textures/woodland-crof-sap1.webp"
        self.assertEqual(checksum.crc32_file(path), woodland.PILOT_ARTWORK_CRC32)

    def test_snet_reuses_accepted_low_broadleaf_artwork(self):
        self.assertEqual(checksum.crc32_file(PROJECT / "assets/models/mega/textures/woodland-snet-llrts2.webp"), woodland.SNET_ARTWORK_CRC32)

    def test_llrtsx_end_uses_covered_foliage_with_unchanged_image(self):
        path = PROJECT / "assets/models/mega/textures/woodland-snet-llrtsx.webp"
        self.assertEqual(checksum.crc32_file(path), woodland.LLRTSX_ARTWORK_CRC32)
        converter = shutil.which("magick") or shutil.which("convert")
        self.assertIsNotNone(converter, "Mega asset authoring requires ImageMagick")
        rgba = subprocess.run([converter, str(path), "-depth", "8", "RGBA:-"], check=True, capture_output=True).stdout
        self.assertEqual(len(rgba), 512 * 128 * 4)
        # The skin retains the source's transparent U0/U1 framing. The end
        # remaps into real interior foliage instead of that empty margin.
        self.assertTrue(all(rgba[(y * 512 + x) * 4 + 3] < 128 for x in (0, 511) for y in range(128)))
        samples = []
        for x in range(448, 480):
            for y in range(64, 112):
                samples.append(rgba[(y * 512 + x) * 4 + 3] >= 128)
        self.assertGreater(sum(samples), len(samples) / 4)


class CanonicalWoodlandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = configured_data_root() / "data/croffree.bfp"
        archive = path.read_bytes()
        if checksum.crc32_file(path) != "e4399113":
            raise AssertionError("Unexpected canonical Croft FREE CRC32")
        count, table_size = struct.unpack_from("<II", archive)
        if table_size != count * 24:
            raise AssertionError("Invalid canonical BFP directory")
        name, offset, length = struct.unpack_from("<16sII", archive, 12 + 2 * 24)
        # This directory has a residual T after the first NUL; it is padding,
        # not part of TREES.C3D's original member name.
        if name.split(b"\0", 1)[0] != b"TREES.C3D":
            raise AssertionError("Unexpected canonical woodland member")
        start = 12 + table_size + offset
        cls.raw = archive[start:start + length]
        if geometry.crc32(cls.raw) != "ae8126bd":
            raise AssertionError("Unexpected canonical TREES CRC32")
        cls.header, cls.nodes, cls.meshes, cls.textures = geometry.decode(cls.raw)

    def combined(self, meshes, quantized=False):
        combined = geometry.Mesh(self.meshes[23].header, [], [])
        for part in (23, 24):
            mesh = meshes[part]
            if quantized:
                encoded = mesh.encode()
                count = len(mesh.vertices)
                vertices = [tuple(x / 65536 for x in struct.unpack_from("<8i", encoded, 48 + 32 * i)) for i in range(count)]
            else:
                vertices = mesh.vertices
            offset = len(combined.vertices)
            combined.vertices.extend((v[0] + (64. if part == 24 else 0.), *v[1:]) for v in vertices)
            for face in mesh.faces:
                record = bytearray(face)
                struct.pack_into("<3H", record, 2, *(i + offset for i in struct.unpack_from("<3H", face, 2)))
                combined.faces.append(bytes(record))
        return combined

    def test_two_source_owners_form_one_closed_body(self):
        before = [mesh.encode() for mesh in self.meshes]
        result, stats = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "SAP1"])
        self.assertEqual([i for i, mesh in enumerate(result) if mesh.encode() != before[i]], [23, 24])
        self.assertEqual(stats["woodland_strips"], 1)
        self.assertEqual(stats["added_triangles"], sum(len(result[i].faces) - len(self.meshes[i].faces) for i in (23, 24)))
        for index in (23, 24):
            mesh = result[index]
            self.assertEqual(mesh.header, self.meshes[index].header)
            self.assertGreater(len(mesh.faces), len(self.meshes[index].faces))
        for quantized in (False, True):
            edges, volume = shell_measurements(self.combined(result, quantized))
            self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
            self.assertGreater(volume, 80.)
            self.assertLess(volume, 82.)
        packed = geometry.pack(self.header, self.nodes, result, self.textures)
        header, nodes, _, textures = geometry.decode(packed)
        self.assertEqual((nodes, textures, header[:20]), (self.nodes, self.textures, self.header[:20]))
        self.assertEqual([mesh.encode() for mesh in self.meshes], before)

    def test_original_uv_diagonal_ownership_and_unpinched_contact(self):
        result, _ = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "SAP1"])
        bottoms, tops, _ = woodland._croft_panels([self.meshes[23], self.meshes[24]])
        seam = []
        counts = []
        for owner, index in enumerate((23, 24)):
            mesh = result[index]
            count = 0
            diagonal = set()
            for face in mesh.faces:
                vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                world = [(v[0] + 64. * owner, *v[1:3]) for v in vertices]
                centers = [woodland.interpolate(woodland.interpolate(bottoms[1], bottoms[2], v[3]),
                                                woodland.interpolate(tops[1], tops[2], v[3]), -v[4]) for v in vertices]
                if not all(math.dist(point, center) <= .450001 for point, center in zip(world, centers)):
                    continue
                count += 1
                for vertex, point in zip(vertices, world):
                    delta = -vertex[4] - vertex[3]
                    self.assertLessEqual(delta if owner == 0 else -delta, 1e-10)
                    if abs(delta) < 1e-10:
                        diagonal.add(tuple(round(x, 8) for x in point))
                self.assertTrue(any(abs(-v[4] - v[3]) > 1e-10 for v in vertices), "No internal diagonal cap")
            counts.append(count)
            seam.append(diagonal)
        self.assertTrue(all(count > 0 for count in counts))
        self.assertEqual(seam[0], seam[1])
        middle = [point for point in seam[0] if abs(point[1] - (bottoms[1][1] + bottoms[2][1] + tops[1][1] + tops[2][1]) / 4) < 1e-7]
        self.assertEqual(len(middle), 2)
        self.assertGreater(math.dist(*middle), .7)
        self.assertLessEqual(math.dist(*middle), .9)

    def test_ground_top_full_uv_and_source_materials_survive(self):
        result, _ = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "SAP1"])
        body = self.combined(result)
        bottoms, tops, _ = woodland._croft_panels([self.meshes[23], self.meshes[24]])
        points = {position(vertex) for vertex in body.vertices}
        for point in (*bottoms, *tops):
            self.assertIn(tuple(round(x, 7) for x in point), points)
        # The source roots disagree by precisely one fixed-point X unit at
        # A-bottom and B-top; only those shared contacts are normalized.
        for upper_i, lower_i in ((3, 0), (4, 2)):
            upper = woodland.add(self.meshes[24].vertices[upper_i][:3], (64., 0., 0.))
            self.assertEqual(woodland.sub(upper, self.meshes[23].vertices[lower_i][:3]), (1 / 65536, 0., 0.))
        for face in body.faces:
            self.assertEqual((struct.unpack_from("<h", face, 8)[0], tuple(face[10:14])), (1, (167, 167, 167, 255)))
        for vertex in body.vertices:
            self.assertTrue(all(math.isfinite(x) for x in vertex))
            self.assertTrue(0 <= vertex[3] <= 1 and -1 <= vertex[4] <= 0)
            self.assertAlmostEqual(math.sqrt(woodland.dot(vertex[5:], vertex[5:])), 1.)
        # D/E meet untouched neighboring source strips, not free box ends.
        self.assertEqual(result[28].encode(), self.meshes[28].encode())
        self.assertEqual(result[29].encode(), self.meshes[29].encode())

    def test_pair_budget_and_source_selection_are_atomic(self):
        pair = [self.meshes[23], self.meshes[24]]
        names = [b"LLRTS1\0", b"SAP1\0"]
        for sources in (pair[:1], pair[1:], pair + [pair[0]]):
            self.assertIs(woodland.enhance_connected(sources, geometry.Mesh, names)[0], sources)
        mutated = geometry.Mesh(pair[1].header, list(pair[1].vertices), list(pair[1].faces))
        mutated.vertices[0] = (mutated.vertices[0][0] + 1 / 65536, *mutated.vertices[0][1:])
        sources = [pair[0], mutated]
        self.assertIs(woodland.enhance_connected(sources, geometry.Mesh, names)[0], sources)
        rebuilt, stats = woodland.enhance_connected(pair, geometry.Mesh, names)
        cost = stats["added_triangles"]
        per_mesh = max(len(a.faces) - len(b.faces) for a, b in zip(rebuilt, pair))
        self.assertGreater(cost, 0)
        for total, per in ((cost - 1, 2048), (48000, per_mesh - 1)):
            self.assertIs(woodland.enhance_connected(pair, geometry.Mesh, names, total, per)[0], pair)
        result, stats = woodland.enhance_connected(pair, geometry.Mesh, names, cost, per_mesh)
        self.assertTrue(all(a is not b for a, b in zip(result, pair)))
        self.assertEqual(stats["added_triangles"], cost)


class CanonicalSnetWoodlandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = configured_data_root() / "data/snetfree.bfp"
        if checksum.crc32_file(path) != "4111866a":
            raise AssertionError("Unexpected canonical Snet FREE CRC32")
        archive = path.read_bytes()
        count, table_size = struct.unpack_from("<II", archive)
        if table_size != count * 24:
            raise AssertionError("Invalid canonical BFP directory")
        name, offset, length = struct.unpack_from("<16sII", archive, 12 + 2 * 24)
        if name.split(b"\0", 1)[0] != b"TREES.C3D":
            raise AssertionError("Unexpected canonical woodland member")
        start = 12 + table_size + offset
        raw = archive[start:start + length]
        if geometry.crc32(raw) != "362bcf3f":
            raise AssertionError("Unexpected canonical Snet TREES CRC32")
        cls.header, cls.nodes, cls.meshes, cls.textures = geometry.decode(raw)

    def test_exact_mesh19_node60_replacement_and_preserved_other_types(self):
        names = ["LLRTS1", "LLRTS2", "LLRTSX"]
        before = [mesh.encode() for mesh in self.meshes]
        self.assertEqual(len(before), 32)
        results = [woodland.enhance(mesh, geometry.Mesh, names) for mesh in self.meshes]
        changed = [i for i, ((mesh, _), original) in enumerate(zip(results, before)) if mesh.encode() != original]
        self.assertEqual(changed, [0, 19])
        mesh, stats = results[19]
        self.assertEqual(geometry.crc32(before[19]), woodland.SNET_MESH_CRC32)
        self.assertEqual(stats["woodland_strips"], 1)
        self.assertEqual(stats["added_triangles"], len(mesh.faces) - len(self.meshes[19].faces))
        self.assertGreater(len(mesh.faces), len(self.meshes[19].faces))
        edges, volume = shell_measurements(mesh)
        self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
        self.assertGreater(volume, 285.)
        self.assertLess(volume, 290.)
        bindings, offset = [], 0
        while offset < len(self.nodes):
            size = struct.unpack_from("<I", self.nodes, offset)[0]
            count = self.nodes[offset + 76]
            slots = struct.unpack_from("<" + "H" * count, self.nodes, offset + 80) if count else ()
            bindings.append(slots)
            offset += size
        self.assertEqual([i for i, slots in enumerate(bindings) if 19 in slots], [60])
        packed = geometry.pack(self.header, self.nodes, [mesh for mesh, _ in results], self.textures)
        header, nodes, decoded, textures = geometry.decode(packed)
        self.assertEqual(nodes, self.nodes)
        self.assertEqual(textures, self.textures)
        self.assertEqual(header[:20], self.header[:20])
        fixed_edges, fixed_volume = shell_measurements(decoded[19])
        self.assertTrue(all(count == 1 and fixed_edges[(b, a)] == 1 for (a, b), count in fixed_edges.items()))
        self.assertGreater(fixed_volume, 285.)
        self.assertEqual([mesh.encode() for mesh in self.meshes], before)

    def test_single_mesh_selection_and_budget(self):
        for index, names in ((19, ["LLRTS1", "LLRTS2"]), (0, ["LLRTS1", "LLRTS2", "LLRTSX"])):
            source = self.meshes[index]
            rebuilt, stats = woodland.enhance(source, geometry.Mesh, names)
            cost = stats["added_triangles"]
            self.assertGreater(cost, 0)
            self.assertIs(woodland.enhance(source, geometry.Mesh, names, cost - 1)[0], source)
            self.assertEqual(woodland.enhance(source, geometry.Mesh, names, cost)[0].encode(), rebuilt.encode())
            altered = geometry.Mesh(source.header, list(source.vertices), list(source.faces))
            altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
            self.assertIs(woodland.enhance(altered, geometry.Mesh, names)[0], altered)

    def test_llrtsx_single_triangle_body_preserves_source_domain_and_binding(self):
        source = self.meshes[0]
        before = source.encode()
        self.assertEqual(geometry.crc32(before), woodland.LLRTSX_MESH_CRC32)
        mesh, stats = woodland.enhance(source, geometry.Mesh, ["LLRTS1", "LLRTS2", "LLRTSX"])
        self.assertEqual(stats["woodland_strips"], 1)
        self.assertEqual(stats["added_triangles"], len(mesh.faces) - len(source.faces))
        self.assertGreater(len(mesh.faces), len(source.faces))
        for quantized in (False, True):
            checked = mesh
            if quantized:
                data = mesh.encode()
                checked = geometry.Mesh(mesh.header,
                    [tuple(value / 65536 for value in struct.unpack_from("<8i", data, 48 + 32 * i)) for i in range(len(mesh.vertices))], mesh.faces)
            edges, volume = shell_measurements(checked)
            self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
            self.assertGreater(volume, 161.)
            self.assertLess(volume, 162.)
        left, right, top = (v[:3] for v in source.vertices)
        points = {position(vertex) for vertex in mesh.vertices}
        self.assertEqual(len(points), 5)
        self.assertTrue({position(v) for v in source.vertices} <= points)
        up = woodland.sub(top, right)
        for vertex in mesh.vertices[:8]:
            u, t = vertex[3], -vertex[4]
            self.assertTrue(0 <= t <= u <= 1)
            center = woodland.add(woodland.interpolate(left, right, u), woodland.scale(up, t))
            distance = math.dist(vertex[:3], center)
            self.assertAlmostEqual(vertex[1], center[1], places=10)
            self.assertLessEqual(distance, woodland.LLRTSX_DEPTH * .5 * u + 1e-10)
            if t == 0 or t == u:
                self.assertLess(distance, 1e-10)
        self.assertAlmostEqual(math.dist(mesh.vertices[3][:3], mesh.vertices[7][:3]), 1.4)
        # Original lower and rising upper edges remain shared by front/rear;
        # the only closure is on the full-height right section.
        self.assertEqual(mesh.vertices[0][:3], mesh.vertices[4][:3])
        self.assertEqual(mesh.vertices[1][:3], mesh.vertices[5][:3])
        self.assertEqual(mesh.vertices[2][:3], mesh.vertices[6][:3])
        self.assertEqual([v[:3] for v in mesh.vertices[8:]],
                         [mesh.vertices[i][:3] for i in (1, 3, 2, 7)])
        for vertex in mesh.vertices:
            self.assertTrue(0 <= -vertex[4] <= vertex[3] <= 1)
        for face in mesh.faces:
            self.assertEqual(struct.unpack_from("<h", face, 8)[0], 2)
            self.assertEqual(tuple(face[10:14]), (167, 167, 167, 255))
        for vertex in mesh.vertices:
            self.assertTrue(all(math.isfinite(value) for value in vertex))
            self.assertAlmostEqual(math.sqrt(woodland.dot(vertex[5:], vertex[5:])), 1., places=8)
        offset, index, binding = 0, 0, []
        while offset < len(self.nodes):
            size = struct.unpack_from("<I", self.nodes, offset)[0]
            count = self.nodes[offset + 76]
            slots = struct.unpack_from("<" + "H" * count, self.nodes, offset + 80) if count else ()
            if 0 in slots:
                binding.append(index)
                self.assertEqual(struct.unpack_from("<h", self.nodes, offset + 4)[0], -1)
                self.assertEqual(struct.unpack_from("<I", self.nodes, offset + 72)[0], 0x21000010)
            offset += size
            index += 1
        self.assertEqual(binding, [6])
        self.assertEqual(source.encode(), before)


class CanonicalOultonWoodlandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = configured_data_root() / "data/oultfree.bfp"
        if checksum.crc32_file(path) != "e6e258cb":
            raise AssertionError("Unexpected canonical Oulton FREE CRC32")
        archive = path.read_bytes()
        count, table_size = struct.unpack_from("<II", archive)
        if table_size != count * 24:
            raise AssertionError("Invalid canonical BFP directory")
        name, offset, length = struct.unpack_from("<16sII", archive, 12 + 2 * 24)
        start = 12 + table_size + offset
        raw = archive[start:start + length]
        if name.split(b"\0", 1)[0] != b"TREES.C3D" or geometry.crc32(raw) != "cf058247":
            raise AssertionError("Unexpected canonical Oulton TREES member")
        cls.header, cls.nodes, cls.meshes, cls.textures = geometry.decode(raw)

    def combined_body(self, meshes):
        body = geometry.Mesh(meshes[100].header, [], [])
        for side, index in enumerate((100, 101)):
            mesh = meshes[index]
            offset = len(body.vertices)
            # The exact source roots differ by64m on X and have no rotation.
            origin = (508.54052734375 + side * 64, 0., -242.149200439453125)
            body.vertices.extend((*woodland.add(v[:3], origin), *v[3:]) for v in mesh.vertices)
            for face in mesh.faces:
                if struct.unpack_from("<h", face, 8)[0] != 0:
                    continue
                record = bytearray(face)
                struct.pack_into("<3H", record, 2, *(i + offset for i in struct.unpack_from("<3H", face, 2)))
                body.faces.append(bytes(record))
        return body

    def test_two_exact_components_form_one_oriented_closed_body(self):
        before = [mesh.encode() for mesh in self.meshes]
        result, stats = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "LLRTS2"])
        self.assertEqual([i for i, mesh in enumerate(result) if mesh.encode() != before[i]], [14, 15, 100, 101])
        self.assertEqual(stats["woodland_strips"], 2)
        self.assertEqual(stats["added_triangles"], sum(len(result[i].faces) - len(self.meshes[i].faces) for i in (14, 15, 100, 101)))
        for index in (14, 15, 100, 101):
            self.assertGreater(len(result[index].faces), len(self.meshes[index].faces))
        body = self.combined_body(result)
        edges, volume = shell_measurements(body)
        self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
        self.assertGreater(volume, 238.)
        self.assertLess(volume, 240.)
        for face in body.faces:
            a, b, c = (body.vertices[index] for index in struct.unpack_from("<3H", face, 2))
            normal = woodland.cross(woodland.sub(b[:3], a[:3]), woodland.sub(c[:3], a[:3]))
            shading = woodland.add(woodland.add(a[5:], b[5:]), c[5:])
            self.assertGreater(woodland.dot(normal, shading), 0.)
            self.assertEqual(tuple(face[10:14]), (167, 167, 167, 255))
        self.assertEqual([mesh.encode() for mesh in self.meshes], before)

    def test_exact_llrts2_attributes_nodes_and_single_mesh_path_are_preserved(self):
        result, _ = woodland.enhance_connected(self.meshes, geometry.Mesh, [b"LLRTS1\0", b"LLRTS2\0"])
        self.assertEqual(result[100].vertices[:13], self.meshes[100].vertices)
        self.assertEqual(result[100].faces[:3], self.meshes[100].faces[2:])
        self.assertEqual([f for f in result[100].faces if struct.unpack_from("<h", f, 8)[0] == 1], self.meshes[100].faces[2:])
        for index in (100, 101):
            unchanged, stats = woodland.enhance(self.meshes[index], geometry.Mesh, ["LLRTS1", "LLRTS2"])
            self.assertIs(unchanged, self.meshes[index])
            self.assertEqual(stats, {"added_triangles": 0})
        packed = geometry.pack(self.header, self.nodes, result, self.textures)
        header, nodes, decoded, textures = geometry.decode(packed)
        self.assertEqual(header[:20], self.header[:20])
        self.assertEqual(nodes, self.nodes)
        self.assertEqual(textures, self.textures)
        # Closure must also survive the actual16.16 asset representation.
        edges, _ = shell_measurements(self.combined_body(decoded))
        self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
        offset, index, bindings = 0, 0, {}
        while offset < len(nodes):
            size = struct.unpack_from("<I", nodes, offset)[0]
            count = nodes[offset + 76]
            slots = struct.unpack_from("<" + "H" * count, nodes, offset + 80) if count else ()
            for target in (100, 101):
                if target in slots:
                    bindings[target] = index
                    self.assertEqual(struct.unpack_from("<h", nodes, offset + 4)[0], -1)
                    self.assertEqual(struct.unpack_from("<I", nodes, offset + 72)[0], 0x21000010)
            offset += size
            index += 1
        self.assertEqual(bindings, {100: 260, 101: 261})

    def test_ground_top_uvs_depth_and_common_uncapped_miter(self):
        result, _ = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "LLRTS2"])
        joins = []
        for side, index in enumerate((100, 101)):
            mesh, source = result[index], self.meshes[index]
            panel = woodland._oulton_panel(source, side)
            offset = len(source.vertices)
            grid_size = 6
            # Independently pin the four actual source corners, not only the
            # helper's inferred panel direction: U must not mirror at B.
            source_corners = ((3, 0, 0), (2, 0, 2), (0, 1, 0), (1, 1, 2)) if side == 0 else ((0, 0, 0), (3, 0, 2), (1, 1, 0), (2, 1, 2))
            for source_index, column, row in source_corners:
                expected_uv = (float(column), -row / 2)
                self.assertEqual(source.vertices[source_index][3:5], expected_uv)
                for skin in range(2):
                    actual = mesh.vertices[offset + skin * grid_size + column * 3 + row]
                    self.assertEqual(actual[:5], (*source.vertices[source_index][:3], *expected_uv))
            for sign in range(2):
                for column in range(2):
                    u = float(column)
                    base = woodland.interpolate(panel.left, panel.right, u)
                    up = woodland.interpolate(panel.up_left, panel.up_right, u)
                    for row in range(3):
                        vertex = mesh.vertices[offset + sign * grid_size + column * 3 + row]
                        self.assertEqual(vertex[3], u)
                        self.assertTrue(-1 <= vertex[4] <= 0)
                        center = woodland.add(base, woodland.scale(up, -vertex[4]))
                        self.assertAlmostEqual(vertex[1], center[1], places=10)
                        self.assertLessEqual(math.dist(vertex[:3], center), .7 + 1e-10)
                        if row in (0, 2):
                            self.assertLess(math.dist(vertex[:3], center), 1e-10)
                        elif column == (0 if side == 0 else 1):
                            self.assertLessEqual(math.dist(vertex[:3], center), .05 + 1e-10)
            column = 1 if side == 0 else 0
            origin = (508.54052734375 + side * 64, 0., -242.149200439453125)
            joint = [(*woodland.add(mesh.vertices[offset + sign * grid_size + column * 3 + row][:3], origin),
                      *mesh.vertices[offset + sign * grid_size + column * 3 + row][5:]) for sign in range(2) for row in range(3)]
            joins.append(joint)
            join_points = {position(v) for v in joint}
            # No face may span only the shared B cross-section. The pair's
            # manifold closure comes from matching skins, not internal caps.
            for face in mesh.faces:
                points = [position(woodland.add(mesh.vertices[i][:3], origin)) for i in struct.unpack_from("<3H", face, 2)]
                self.assertFalse(all(point in join_points for point in points))
        for a, b in zip(*joins):
            for x, y in zip(a, b):
                self.assertAlmostEqual(x, y, places=10)
        self.assertAlmostEqual(math.dist(joins[0][1][:3], joins[0][4][:3]), 1.4, places=10)

    def test_pair_selection_and_budget_fallback_are_atomic(self):
        names = ["LLRTS1", "LLRTS2"]
        pair_only = [self.meshes[100], self.meshes[101]]
        altered = geometry.Mesh(self.meshes[100].header, list(self.meshes[100].vertices), list(self.meshes[100].faces))
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        modified_family = [altered, self.meshes[101]]
        for sources in (pair_only[:1], pair_only[1:], pair_only + [pair_only[0]], modified_family):
            result, stats = woodland.enhance_connected(sources, geometry.Mesh, names)
            self.assertIs(result, sources)
            self.assertEqual(stats, {"added_triangles": 0})
        for wrong_names in (["LLRTS2", "LLRTS1"], ["LLRTS1", "SAP1"]):
            self.assertIs(woodland.enhance_connected(self.meshes, geometry.Mesh, wrong_names)[0], self.meshes)
        rebuilt, baseline = woodland.enhance_connected(pair_only, geometry.Mesh, names)
        cost = baseline["added_triangles"]
        per_mesh_cost = max(len(a.faces) - len(b.faces) for a, b in zip(rebuilt, pair_only))
        self.assertGreater(cost, 0)
        for total, per_mesh in ((cost - 1, 2048), (48000, per_mesh_cost - 1)):
            self.assertIs(woodland.enhance_connected(pair_only, geometry.Mesh, names, total, per_mesh)[0], pair_only)
        for total, per_mesh in ((cost, per_mesh_cost), (48000, 2048)):
            result, stats = woodland.enhance_connected(pair_only, geometry.Mesh, names, total, per_mesh)
            self.assertLessEqual(stats["added_triangles"], total)
            self.assertEqual(len(result[0].faces) - len(pair_only[0].faces), len(result[1].faces) - len(pair_only[1].faces))
            self.assertLessEqual(len(result[0].faces) - len(pair_only[0].faces), per_mesh)
            edges, volume = shell_measurements(self.combined_body(self.meshes[:100] + result + self.meshes[102:]))
            self.assertGreater(volume, 238.)
            self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
            repeated, _ = woodland.enhance_connected(pair_only, geometry.Mesh, ["LLRTS1", "LLRTS2"], total, per_mesh)
            self.assertEqual([m.encode() for m in result], [m.encode() for m in repeated])


class CanonicalOultonWedgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        CanonicalOultonWoodlandTests.setUpClass()
        for key in ("header", "nodes", "meshes", "textures"):
            setattr(cls, key, getattr(CanonicalOultonWoodlandTests, key))

    def combined_body(self, meshes):
        body = geometry.Mesh(meshes[14].header, [], [])
        for part, index in enumerate((14, 15)):
            mesh = meshes[index]
            offset = len(body.vertices)
            origin = (188.54052734375 + part * 64, 0., 269.850799560546875)
            body.vertices.extend((*woodland.add(v[:3], origin), *v[3:]) for v in mesh.vertices)
            for face in mesh.faces:
                record = bytearray(face)
                struct.pack_into("<3H", record, 2, *(i + offset for i in struct.unpack_from("<3H", face, 2)))
                body.faces.append(bytes(record))
        return body

    def test_three_panels_preserve_neighbors_and_source_pairs(self):
        before = [m.encode() for m in self.meshes]
        result, _ = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "LLRTS2"])
        for index in (14, 15, 100, 101):
            self.assertGreater(len(result[index].faces), len(self.meshes[index].faces))
        self.assertEqual([i for i, m in enumerate(result) if m.encode() != before[i]], [14, 15, 100, 101])
        for index in (5, 6, 7, 16):
            self.assertIs(result[index], self.meshes[index])
        for index in (14, 15):
            self.assertEqual(woodland.enhance(self.meshes[index], geometry.Mesh, ["LLRTS1", "LLRTS2"])[1], {"added_triangles": 0})
        packed = geometry.pack(self.header, self.nodes, result, self.textures)
        header, nodes, _, textures = geometry.decode(packed)
        self.assertEqual(header[:20], self.header[:20])
        self.assertEqual(nodes, self.nodes)
        self.assertEqual(textures, self.textures)
        self.assertEqual([m.encode() for m in self.meshes], before)

    def test_joint_body_is_manifold_in_float_and_fixed_asset_coordinates(self):
        result, _ = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "LLRTS2"])
        decoded = geometry.decode(geometry.pack(self.header, self.nodes, result, self.textures))[2]
        for meshes in (result, decoded):
            body = self.combined_body(meshes)
            edges, volume = shell_measurements(body)
            self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
            self.assertGreater(volume, 460.)
            self.assertLess(volume, 464.)
            for face in body.faces:
                self.assertEqual(struct.unpack_from("<h", face, 8)[0], 0)
                self.assertEqual(tuple(face[10:14]), (167, 167, 167, 255))
                a, b, c = (body.vertices[i] for i in struct.unpack_from("<3H", face, 2))
                normal = woodland.cross(woodland.sub(b[:3], a[:3]), woodland.sub(c[:3], a[:3]))
                self.assertGreater(woodland.dot(normal, woodland.add(woodland.add(a[5:], b[5:]), c[5:])), 0.)

    def test_original_uv_corners_ground_and_two_uncapped_miters(self):
        result, _ = woodland.enhance_connected(self.meshes, geometry.Mesh, ["LLRTS1", "LLRTS2"])
        # Actual source topology: the second panel of14starts after its first
        # panel and the four outer-A leaf-cap vertices;15has a separate node.
        specs = ((14, 0, (0, 1, 3, 2)), (14, 16, (4, 5, 8, 6)), (15, 0, (0, 1, 3, 2)))
        sections = []
        for mesh_index, start, (li, ri, lti, rti) in specs:
            mesh, original = result[mesh_index], self.meshes[mesh_index]
            left, right, ltop, rtop = (original.vertices[i][:3] for i in (li, ri, lti, rti))
            for si, col, row in ((li, 0, 0), (ri, 1, 0), (lti, 0, 2), (rti, 1, 2)):
                uv = (float(col), -row / 2)
                self.assertEqual(original.vertices[si][3:5], uv)
                for skin in range(2):
                    self.assertEqual(mesh.vertices[start + skin * 6 + col * 3 + row][:5], (*original.vertices[si][:3], *uv))
            for skin in range(2):
                for column in range(2):
                    u = float(column)
                    base = woodland.interpolate(left, right, u)
                    up = woodland.interpolate(woodland.sub(ltop, left), woodland.sub(rtop, right), u)
                    for row in range(3):
                        v = mesh.vertices[start + skin * 6 + column * 3 + row]
                        center = woodland.add(base, woodland.scale(up, -v[4]))
                        self.assertAlmostEqual(v[1], center[1], places=10)
                        self.assertTrue(0 <= v[3] <= 1 and -1 <= v[4] <= 0)
                        self.assertLessEqual(woodland.math.dist(v[:3], center), .7 + 1e-10)
                        if row in (0, 2):
                            self.assertLess(woodland.math.dist(v[:3], center), 1e-10)
            origin = (188.54052734375 + (64 if mesh_index == 15 else 0), 0., 269.850799560546875)
            endpoints = []
            for column in (0, 1):
                endpoints.append([(*woodland.add(mesh.vertices[start + skin * 6 + column * 3 + row][:3], origin),
                                   *mesh.vertices[start + skin * 6 + column * 3 + row][5:]) for skin in range(2) for row in range(3)])
            sections.append(endpoints)
        for seam_a, seam_b in ((sections[0][1], sections[1][0]), (sections[1][1], sections[2][0])):
            for a, b in zip(seam_a, seam_b):
                for x, y in zip(a, b):
                    self.assertAlmostEqual(x, y, places=10)
            self.assertAlmostEqual(math.dist(seam_a[1][:3], seam_a[4][:3]), 1.4, places=10)
            seam_points = {position(v) for v in seam_a}
            body = self.combined_body(result)
            for face in body.faces:
                self.assertFalse(all(position(body.vertices[i]) in seam_points for i in struct.unpack_from("<3H", face, 2)))
        for outer in (sections[0][0], sections[2][1]):
            self.assertAlmostEqual(math.dist(outer[1][:3], outer[4][:3]), .1, places=10)

    def test_each_pair_has_an_atomic_share_of_the_family_budget(self):
        pair = [self.meshes[14], self.meshes[15]]
        names = ["LLRTS1", "LLRTS2"]
        for sources in (pair[:1], pair[1:], pair + [pair[0]]):
            self.assertIs(woodland.enhance_connected(sources, geometry.Mesh, names)[0], sources)
        pair_result, pair_stats = woodland.enhance_connected(pair, geometry.Mesh, names)
        pair_cost = pair_stats["added_triangles"]
        per_mesh_cost = max(len(a.faces) - len(b.faces) for a, b in zip(pair_result, pair))
        self.assertGreater(pair_cost, 0)
        for total, per_mesh in ((pair_cost - 1, 2048), (48000, per_mesh_cost - 1)):
            self.assertIs(woodland.enhance_connected(pair, geometry.Mesh, names, total, per_mesh)[0], pair)
        for total, per_mesh in ((pair_cost, per_mesh_cost), (48000, 2048)):
            rebuilt, stats = woodland.enhance_connected(pair, geometry.Mesh, names, total, per_mesh)
            self.assertLessEqual(stats["added_triangles"], total)
            self.assertTrue(all(len(a.faces) - len(b.faces) <= per_mesh for a, b in zip(rebuilt, pair)))
            family = list(self.meshes)
            family[14:16] = rebuilt
            edges, volume = shell_measurements(self.combined_body(family))
            self.assertGreater(volume, 460.)
            self.assertTrue(all(count == 1 and edges[(b, a)] == 1 for (a, b), count in edges.items()))
        complete, complete_stats = woodland.enhance_connected(self.meshes, geometry.Mesh, names)
        first_cost = sum(len(complete[i].faces) - len(self.meshes[i].faces) for i in (100, 101))
        second_cost = sum(len(complete[i].faces) - len(self.meshes[i].faces) for i in (14, 15))
        self.assertEqual(complete_stats["added_triangles"], first_cost + second_cost)
        for total in (first_cost - 1, first_cost, first_cost + second_cost - 1, first_cost + second_cost):
            result, stats = woodland.enhance_connected(self.meshes, geometry.Mesh, names, total)
            self.assertLessEqual(stats["added_triangles"], total)
            for a, b in ((14, 15), (100, 101)):
                self.assertEqual(result[a] is self.meshes[a], result[b] is self.meshes[b])
            if total == first_cost:
                self.assertIs(result[14], self.meshes[14])
                self.assertIsNot(result[100], self.meshes[100])


if __name__ == "__main__":
    unittest.main(verbosity=2)
