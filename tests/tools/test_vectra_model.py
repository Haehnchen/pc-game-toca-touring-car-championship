#!/usr/bin/env python3
"""Source support, fixed-point topology and separate Vectra/V10 contracts."""

import collections
import json
import math
from pathlib import Path
import runpy
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import vectra_geometry as geometry
import vehicle_geometry as panels
from git_game_data import configured_data_root
from vehicle_model import Mesh, read_source, decode, pack, cross, sub, dot, add, crc32


class VectraGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data_root = configured_data_root()
        inventory = runpy.run_path(str(PROJECT / 'tools/inventory-vehicles.py'))['inventory'](cls.data_root)
        cls.entry = next(v for v in inventory['vehicles'] if v['key'] == 'vectra')
        cls.v10 = next(v for v in inventory['vehicles'] if v['key'] == 'vectra2')
        cls.definition = json.loads((PROJECT / 'assets/models/vehicles/vectra/definition.json').read_text())
        cls.source = read_source(cls.entry['models']['high']['source'], cls.data_root)
        cls.original = decode(cls.source)
        cls.output, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.generated = decode(cls.output)

    def assert_faces(self, meshes):
        for index, mesh in enumerate(meshes):
            self.assertLess(max(len(mesh.vertices), len(mesh.faces)), 65536, index)
            self.assertTrue(all(math.isfinite(v) for p in mesh.vertices for v in p), index)
            for face in mesh.faces:
                self.assertLess(max(struct.unpack_from('<3H', face, 2)), len(mesh.vertices), index)
                a, b, c = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                normal = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
                self.assertGreater(dot(normal, normal), 1e-16, index)
                self.assertGreater(dot(normal, add(add(a[5:], b[5:]), c[5:])), 0, index)

    @staticmethod
    def edge_counts(mesh):
        edges = collections.Counter()
        for face in mesh.faces:
            vertices = [tuple(round(v * 65536) for v in mesh.vertices[i][:3]) for i in struct.unpack_from('<3H', face, 2)]
            for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                edges[tuple(sorted((a, b)))] += 1
        return edges

    def test_original_members_hierarchy_slots_and_damage_are_preserved(self):
        self.assertEqual(self.stats['source_model_crc32'], '943e9422')
        self.assertEqual(self.stats['original_visible_triangles'], 315)
        self.assertEqual(self.generated[0][:20], self.original[0][:20])
        self.assertEqual(self.generated[1], self.original[1])
        self.assertEqual(self.generated[3], self.original[3])
        self.assertEqual([m.header[:12] for m in self.generated[2]], [m.header[:12] for m in self.original[2]])
        for level, member in (('medium', 8), ('low', 9)):
            source = self.entry['models'][level]['source']
            self.assertEqual(source['member'], member)
            self.assertEqual(crc32(read_source(source, self.data_root)), source['member_crc32'])
        for index in (3, 7, 17, 23, 25, 29, 33, 41, 43, 45, 47, 48):
            self.assertEqual(self.generated[2][index].vertices, self.original[2][index].vertices, index)
            self.assertEqual(self.generated[2][index].faces, self.original[2][index].faces, index)
        self.assertEqual(geometry.generate(self.source, self.definition)[0], self.output)
        self.assertGreater(self.stats['visible_triangles'], 8000)
        self.assertLess(self.stats['visible_triangles'], 15000)

    def test_quantized_new_faces_have_outward_normals_and_no_degenerates(self):
        self.assert_faces(self.generated[2])

    def test_v10_shares_topology_but_not_source_positions_pivots_or_artwork(self):
        data = read_source(self.v10['models']['high']['source'], self.data_root)
        other = decode(data)
        self.assertEqual(crc32(data), 'e0ce56fe')
        self.assertNotEqual(other[1], self.original[1])
        self.assertNotEqual(other[3], self.original[3])
        special = []
        for index, (a, b) in enumerate(zip(self.original[2], other[2])):
            self.assertEqual(a.faces, b.faces, index)
            self.assertEqual([v[3:] for v in a.vertices], [v[3:] for v in b.vertices], index)
            if index == 49:
                self.assertEqual(a.vertices, b.vertices)
                continue
            for i, (p, q) in enumerate(zip(a.vertices, b.vertices)):
                if abs(p[0] - q[0]) > .0001 or abs(p[2] - q[2]) > .0001:
                    special.append((index, i))
                    self.assertIn(index, (8, 9, 11, 12))
                else:
                    self.assertAlmostEqual(q[1] - p[1], .055, delta=.00003)
        self.assertEqual(len(special), 8)
        with self.assertRaisesRegex(ValueError, 'canonical HIGH member7'):
            geometry.generate(data, self.definition)

    def test_fifteen_physical_spokes_and_round_source_size_tire(self):
        mesh = self.generated[2][49]
        self.assertEqual(self.definition['wheel_spokes'], 15)
        self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] < 0 for f in mesh.faces))
        self.assertEqual(sum(tuple(f[10:14]) == geometry.BRIGHT for f in mesh.faces), 15 * 2 * 2)
        for row, (z, radius) in enumerate(self.definition['wheel_profile']):
            for p in mesh.vertices[row * 40:(row + 1) * 40]:
                self.assertAlmostEqual(p[2], z, delta=.00002)
                self.assertAlmostEqual(math.hypot(p[0], p[1]), radius, delta=.00002)

    def test_mirror_casing_and_source_wing_are_closed(self):
        for index in (22, 24):
            mesh = self.generated[2][index]
            self.assertTrue(all(count == 2 for count in self.edge_counts(mesh).values()), index)
            self.assertTrue(any(tuple(f[10:14]) == geometry.GLASS for f in mesh.faces))
            self.assertTrue(any(tuple(f[10:14]) == geometry.RED for f in mesh.faces))
        for index in (20, 21):
            mesh = Mesh(self.original[2][index].header, [], [])
            geometry.solid_wing(mesh, self.original[2][index])
            quantized = decode(pack(self.original[0], self.original[1], [mesh] + self.original[2][1:], self.original[3]))[2][0]
            self.assertTrue(all(count == 2 for count in self.edge_counts(quantized).values()), index)
            self.assertTrue(any(struct.unpack_from('<h', f, 8)[0] == geometry.PAINT for f in mesh.faces))

    def test_source_panel_bounds_and_actual_damage_deformation(self):
        for index in self.definition['curved_meshes']:
            original, mesh = self.original[2][index], self.generated[2][index]
            for axis in range(3):
                self.assertGreaterEqual(min(p[axis] for p in mesh.vertices), min(p[axis] for p in original.vertices) - .035, index)
                self.assertLessEqual(max(p[axis] for p in mesh.vertices), max(p[axis] for p in original.vertices) + .035, index)
        for healthy, damaged in ((0, 2), (4, 6), (8, 10), (11, 13), (15, 16), (18, 19), (20, 21), (26, 28), (30, 32)):
            self.assertNotEqual(self.generated[2][healthy].vertices, self.generated[2][damaged].vertices)

    def test_complete_lamp_charts_and_source_fold_boundaries_are_retained(self):
        for index in geometry.LAMP_SLOTS:
            original, mesh = self.original[2][index], self.generated[2][index]
            for point in original.vertices:
                self.assertTrue(any(math.dist(p[:3], point[:3]) < .00003 and math.dist(p[3:5], point[3:5]) < .00003 for p in mesh.vertices))
            for axis in (3, 4):
                self.assertAlmostEqual(min(p[axis] for p in mesh.vertices), min(p[axis] for p in original.vertices), delta=.00002)
                self.assertAlmostEqual(max(p[axis] for p in mesh.vertices), max(p[axis] for p in original.vertices), delta=.00002)

    def test_shared_pn_edge_controls_produce_equal_fixed_point_seams(self):
        originals = self.original[2]
        selected = set(self.definition['curved_meshes'])
        controls = panels.shared_edge_controls(originals, selected, paint=0, excluded_meshes=(49,))
        edges = collections.defaultdict(set)
        for index in selected:
            for face in originals[index].faces:
                if struct.unpack_from('<h', face, 8)[0] != 0:
                    continue
                points = [originals[index].vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                for a, b in zip(points, points[1:] + points[:1]):
                    edges[panels.edge_key(a, b)].add(index)
        seam_count = 0
        for (a, b), indices in edges.items():
            if len(indices) < 2:
                continue
            c, d = controls[(a, b)]
            for t in (1 / 3, 2 / 3):
                p = tuple((1 - t) ** 3 * a[k] + 3 * (1 - t) ** 2 * t * c[k] + 3 * (1 - t) * t ** 2 * d[k] + t ** 3 * b[k] for k in range(3))
                linear = tuple((1 - t) * a[k] + t * b[k] for k in range(3))
                displacement = sub(p, linear)
                length = math.sqrt(dot(displacement, displacement))
                p = tuple(linear[k] + displacement[k] * min(1, .016 / length) for k in range(3)) if length else linear
                position = tuple(round(v * 65536) for v in p)
                # This low-level seam test excludes clipping/details and proves
                # both panels use the same position before final16.16 storage.
                for index in indices:
                    mesh = geometry.curved_mesh(originals[index], 3, .016, controls)
                    self.assertTrue(any(tuple(round(v * 65536) for v in q[:3]) == position for q in mesh.vertices))
                seam_count += 1
        self.assertGreater(seam_count, 8)

    def test_local_arch_footprints_keep_the_measured_lower_widths(self):
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        footprints = geometry.wheel_arch_opaque_uvs(meshes)
        self.assertTrue(footprints)
        for index in self.definition['arch_meshes']:
            mesh = meshes[index]
            for f in mesh.wheel_arch_faces:
                tri = [mesh.vertices[i] for i in struct.unpack_from('<3H', mesh.faces[f], 2)]
                side = 1 if sum(p[2] for p in tri) > 0 else -1
                points = [geometry.arch_coordinates(p, side) for p in tri]
                self.assertGreaterEqual(min(p[1] for p in points), 8 - .00001)
                self.assertTrue(any(all(cx - radius - 2 <= p[0] <= cx + radius + 2 and p[1] <= cy + radius + 2 for p in points)
                                    for cx, cy, radius, _, _ in self.definition['wheel_arches']))
        # These are actual source-triangle intersections at atlas Y8, rather
        # than the wider source color-key holes (18.5..64.5 /181.5..225.5).
        self.assertEqual(self.definition['wheel_arches'][0][4][0], [8, 20.976, 62.964])
        self.assertEqual(self.definition['wheel_arches'][1][4][0], [8, 182.967, 224.906])
        for mesh in meshes:
            for triangle in getattr(mesh, 'lower_arch_opaque_uvs', ()):
                side = 1 if sum(v[1] for v in triangle) > 1.5 else -1
                for u, v in triangle:
                    x, y = (u * 256 - 2.248, 255.99 - v * 256) if side > 0 else (u * 256, v * 256)
                    self.assertGreaterEqual(y, -1e-7)
                    self.assertLessEqual(y, 12 + 1e-7)
                    self.assertTrue(any(abs(x - edge) <= 3 + 1e-7
                                        for arch in self.definition['wheel_arches'] for edge in arch[4][0][1:]))

    def test_deck_key_release_is_clipped_to_existing_narrow_source_edge_strips(self):
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        positions = [[p[:3] for p in mesh.vertices] for mesh in meshes]
        footprints = geometry.deck_seam_opaque_uvs(meshes)
        self.assertTrue(footprints)
        for triangle in footprints:
            for u, v in triangle:
                x, y = u * 256, v * 256
                self.assertGreaterEqual(y, 84.102 - 1e-7)
                self.assertLessEqual(y, 109.703 + 1e-7)
                t = (y - 84.102) / (109.703 - 84.102)
                left, right = 123.113 + t * (126.750 - 123.113), 188.754 + t * (185.117 - 188.754)
                self.assertLessEqual(min(abs(x - left), abs(x - right)), 1.25 + 1e-7)
            center = tuple(sum(p[k] for p in triangle) / 3 - (1 if k else 0) for k in (0, 1))
            self.assertIsNotNone(geometry.uv_vertex(meshes[20], center))
        self.assertEqual(positions, [[p[:3] for p in mesh.vertices] for mesh in meshes])

    @unittest.skipUnless((PROJECT / 'assets/models/vehicles/vectra/textures/metadata.json').exists(), 'Vectra artwork not installed')
    def test_lower_paint_registration_preserves_panel_area_positions_and_normals(self):
        directory = PROJECT / 'assets/models/vehicles/vectra/textures'
        if not json.loads((directory / 'metadata.json').read_text()).get('lower_arch_paint_uvs'):
            self.skipTest('Lower arch paint measurements pending')
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        records = [list(mesh.vertices) for mesh in meshes]
        def area(mesh):
            total = 0
            for face in mesh.faces:
                a, b, c = [mesh.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
                n = cross(sub(b, a), sub(c, a))
                total += math.sqrt(dot(n, n)) / 2
            return total
        areas = [area(mesh) for mesh in meshes]
        geometry.register_lower_arch_paint(meshes, self.definition, directory)
        marked = 0
        for index, mesh in enumerate(meshes):
            self.assertEqual(mesh.vertices[:len(records[index])], records[index])
            self.assertAlmostEqual(area(mesh), areas[index], delta=.00002)
            for f in getattr(mesh, 'lower_arch_faces', ()):
                marked += 1
                face = mesh.faces[f]
                self.assertEqual(struct.unpack_from('<h', face, 8)[0], 0)
                points = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                side = 1 if sum(p[2] for p in points) > 0 else -1
                for p in points:
                    _, y = geometry.arch_coordinates(p, side)
                    self.assertGreaterEqual(y, 8 - 1e-7)
                    self.assertLessEqual(y, 26 + 1e-7)
        self.assertGreater(marked, 0)

    def test_synthetic_reflector_cavity_has_real_quantized_depth(self):
        mesh = Mesh(bytes(48), [], [])
        for y, z in ((0, 0), (1, 0), (1, 1), (0, 1)):
            mesh.vertex((1, y, z), (1, 0, 0), (y, z - 1))
        mesh.triangle((0, 1, 2), (255, 255, 255, 255), 0)
        mesh.triangle((0, 2, 3), (255, 255, 255, 255), 0)
        geometry.recess_uv(mesh, [(.2, -.8), (.8, -.8), (.8, -.2), (.2, -.2)], .012, textured_walls=True)
        self.assertAlmostEqual(min(p[0] for p in mesh.vertices), .988)
        self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] == 0 for f in mesh.faces))

    @unittest.skipUnless((PROJECT / 'assets/models/vehicles/vectra/textures/metadata.json').exists(), 'Final Vectra artwork not installed')
    def test_final_registered_optics_have_source_support_and_quantized_depth(self):
        import vectra_textures
        directory = PROJECT / 'assets/models/vehicles/vectra/textures'
        metadata = json.loads((directory / 'metadata.json').read_text())
        regions = metadata.get('optics_recess_uvs', {})
        if not regions:
            self.skipTest('Final Vectra reflector measurements pending')
        output, _ = geometry.generate(self.source, self.definition, directory)
        meshes = decode(output)[2]
        self.assert_faces(meshes)
        _, _, before, section = geometry.prepare(self.source, self.definition)
        vectra_textures.fit_uvs(before, section, 7, directory)
        for index, contours in regions.items():
            for region in contours:
                uv = tuple(sum(p[k] for p in region['outline']) / len(region['outline']) - (1 if k else 0) for k in (0, 1))
                a, b = geometry.uv_vertex(before[int(index)], uv), geometry.uv_vertex(meshes[int(index)], uv)
                self.assertAlmostEqual(math.dist(a[:3], b[:3]), region['depth'], delta=.001)


if __name__ == '__main__':
    unittest.main()
