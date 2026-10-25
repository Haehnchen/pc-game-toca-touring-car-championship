#!/usr/bin/env python3
"""Canonical Audi A4 geometry, source support and fixed-point topology."""

import collections
import json
import math
from pathlib import Path
import runpy
import struct
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import audi_geometry as geometry
from git_game_data import configured_data_root
from vehicle_model import Mesh, read_source, decode, pack, cross, sub, dot, add, scale, crc32


class AudiGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data_root = configured_data_root()
        inventory = runpy.run_path(str(PROJECT / 'tools/inventory-vehicles.py'))['inventory'](cls.data_root)
        cls.entry = next(v for v in inventory['vehicles'] if v['key'] == 'audi')
        cls.definition = json.loads((PROJECT / 'assets/models/vehicles/audi/definition.json').read_text())
        cls.source = read_source(cls.entry['models']['high']['source'], cls.data_root)
        cls.original = decode(cls.source)
        cls.output, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.generated = decode(cls.output)

    def assert_faces(self, meshes, originals=None):
        for index, mesh in enumerate(meshes):
            self.assertLess(max(len(mesh.vertices), len(mesh.faces)), 65536)
            self.assertTrue(all(math.isfinite(v) for p in mesh.vertices for v in p))
            for face in mesh.faces:
                self.assertLess(max(struct.unpack_from('<3H', face, 2)), len(mesh.vertices))
                a, b, c = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                normal = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
                if dot(normal, normal) < 1e-16:
                    # Canonical removed rear bumper22 contains six zero-area
                    # faces. Preserve this original damage payload byte-for-byte.
                    self.assertIsNotNone(originals, index)
                    self.assertEqual(index, 22)
                    self.assertIn(face, originals[index].faces)
                    self.assertEqual(mesh.vertices, originals[index].vertices)
                    continue
                self.assertGreater(dot(normal, add(add(a[5:], b[5:]), c[5:])), 0, index)

    def test_original_members_structure_variants_and_untouched_damage(self):
        self.assertEqual(self.stats['source_model_crc32'], '1fd3403d')
        self.assertEqual(self.stats['original_visible_triangles'], 318)
        for level, member in (('medium', 5), ('low', 6)):
            source = self.entry['models'][level]['source']
            self.assertEqual(source['member'], member)
            self.assertEqual(crc32(read_source(source, self.data_root)), source['member_crc32'])
        self.assertEqual(self.generated[0][:20], self.original[0][:20])
        self.assertEqual(self.generated[1], self.original[1])
        self.assertEqual(self.generated[3], self.original[3])
        self.assertEqual([m.header[:12] for m in self.generated[2]], [m.header[:12] for m in self.original[2]])
        self.assertEqual(geometry.generate(self.source, self.definition)[0], self.output)
        for index in (3, 7, 11, 14, 16, 18, 22, 25, 41, 43, 45, 47, 48):
            self.assertEqual(self.generated[2][index].vertices, self.original[2][index].vertices, index)
            self.assertEqual(self.generated[2][index].faces, self.original[2][index].faces, index)
        self.assertGreater(self.stats['visible_triangles'], 7000)
        self.assertLess(self.stats['visible_triangles'], 13000)

    def test_new_fixed_point_faces_are_finite_outward_and_nonzero(self):
        self.assert_faces(self.generated[2], self.original[2])

    def test_source_panel_bounds_and_damage_deformation_support(self):
        # Added panel details are local. The wheel's own coordinate system and
        # removed/broken source panels are covered independently below.
        for index in self.definition['curved_meshes']:
            original, mesh = self.original[2][index], self.generated[2][index]
            for axis in range(3):
                self.assertGreaterEqual(min(v[axis] for v in mesh.vertices), min(v[axis] for v in original.vertices) - .04, index)
                self.assertLessEqual(max(v[axis] for v in mesh.vertices), max(v[axis] for v in original.vertices) + .04, index)
        for healthy, damaged in ((0, 2), (4, 6), (8, 10), (19, 21), (26, 28), (29, 31), (35, 36)):
            self.assertNotEqual(self.generated[2][healthy].vertices, self.generated[2][damaged].vertices)

    def test_single_sixteen_spoke_rim_round_tire_and_source_pivots(self):
        wheel = self.generated[2][49]
        self.assertEqual(self.definition['wheel_spokes'], 16)
        self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] < 0 for f in wheel.faces), 'No painted second spoke set')
        self.assertEqual(sum(tuple(f[10:14]) == geometry.RIM for f in wheel.faces), 2 * (32 * 2 + 16 * 2))
        for row, (z, radius) in enumerate(self.definition['wheel_profile']):
            for vertex in wheel.vertices[row * 32:(row + 1) * 32]:
                self.assertAlmostEqual(vertex[2], z, delta=.00002)
                self.assertAlmostEqual(math.hypot(vertex[0], vertex[1]), radius, delta=.00002)

    def assert_closed(self, mesh):
        edges = collections.defaultdict(list)
        for face in mesh.faces:
            vertices = [mesh.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
            for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                self.assertNotEqual(a, b)
                edges[tuple(sorted((a, b)))].append(a < b)
        self.assertTrue(edges)
        for directions in edges.values():
            self.assertEqual(sorted(directions), [False, True])

    def test_silver_mirrors_are_closed_and_keep_source_glass_support(self):
        for index in (40, 42):
            mesh, source = self.generated[2][index], self.original[2][index]
            self.assert_closed(mesh)
            self.assertTrue(any(tuple(f[10:14]) == geometry.GLASS for f in mesh.faces))
            self.assertTrue(any(tuple(f[10:14]) == geometry.SILVER for f in mesh.faces))
            for axis in range(3):
                self.assertGreaterEqual(min(v[axis] for v in mesh.vertices), min(v[axis] for v in source.vertices) - .02)
                self.assertLessEqual(max(v[axis] for v in mesh.vertices), max(v[axis] for v in source.vertices) + .02)

    def test_real_textured_wing_thickness_has_no_duplicate_reverse_skin(self):
        for index in (23, 24):
            original = self.original[2][index]
            wing = Mesh(original.header, list(original.vertices), original.faces[:16])
            wing = geometry.curved_mesh(wing, 3, .018)
            geometry.solid_wing(wing, original, 3)
            wing = decode(pack(self.original[0], self.original[1], [wing] + self.original[2][1:], self.original[3]))[2][0]
            self.assert_closed(wing)
            self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] == 0 for f in wing.faces))
            self.assertGreater(len(wing.faces), 16)
            self.assertLess(len(wing.faces), 100)

    def test_lamp_contours_follow_all_source_islands_without_black_frames(self):
        for index in (15, 17, 44, 46):
            old, mesh = self.original[2][index], self.generated[2][index]
            positions = {v[:3] for v in mesh.vertices}
            for a, b in geometry.source_boundary(old):
                self.assertIn(a[:3], positions)
                self.assertIn(b[:3], positions)
            self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] == 0 for f in mesh.faces))
            for vertex in mesh.vertices:
                for axis in (3, 4):
                    self.assertGreaterEqual(vertex[axis], min(v[axis] for v in old.vertices) - .00002)
                    self.assertLessEqual(vertex[axis], max(v[axis] for v in old.vertices) + .00002)
        footprints = geometry.lamps_opaque_uvs(self.generated[2])
        self.assertGreater(len(footprints), 400)
        self.assertTrue(all(0 <= c <= 1 for tri in footprints for p in tri for c in p))

    def test_registered_reflector_depth_uses_the_lamp_atlas_on_walls(self):
        mesh = geometry.subdivide_lens(self.original[2][44])
        meshes = list(self.original[2]); meshes[44] = mesh
        before = geometry.uv_vertex(mesh, (.945, -.549))
        region = {'optics_recess_uvs': {'44': [{'outline': [[.94, .446], [.95, .446], [.95, .456], [.94, .456]], 'depth': .012}]}}
        with tempfile.TemporaryDirectory(dir=PROJECT / 'build') as directory:
            (Path(directory) / 'metadata.json').write_text(json.dumps(region))
            geometry.detail_optics(meshes, directory)
        after = geometry.uv_vertex(mesh, (.945, -.549))
        self.assertAlmostEqual(math.dist(before[:3], after[:3]), .012, delta=.0001)
        for a, b in zip(before[3:5], after[3:5]):
            self.assertAlmostEqual(a, b)
        self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] == 0 for f in mesh.faces))

    def test_shared_panel_edges_are_identical_after_fixed_point_subdivision(self):
        originals = self.original[2]; selected = self.definition['curved_meshes']
        shared = geometry.shared_edge_controls(originals, set(selected)); checked = 0
        for index in selected:
            mesh = geometry.curved_mesh(originals[index], 3, .018, shared)
            positions = {tuple(round(v * 65536) for v in p[:3]) for p in mesh.vertices}
            for face in originals[index].faces:
                if struct.unpack_from('<h', face, 8)[0] not in (-1, 0):
                    continue
                vertices = [originals[index].vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                    p, q = geometry.edge_key(a, b); c, d = shared[geometry.edge_key(a, b)]
                    for t in (1 / 3, 2 / 3):
                        linear = tuple((1 - t) * p[k] + t * q[k] for k in range(3))
                        point = tuple((1 - t) ** 3 * p[k] + 3 * (1 - t) ** 2 * t * c[k] + 3 * (1 - t) * t * t * d[k] + t ** 3 * q[k] for k in range(3))
                        delta = sub(point, linear); length = math.sqrt(dot(delta, delta))
                        point = add(linear, scale(delta, min(1., .018 / length))) if length else linear
                        self.assertIn(tuple(round(v * 65536) for v in point), positions)
                        checked += 1
        self.assertGreater(checked, 2000)

    def test_upper_arch_mask_release_is_local_and_keeps_lower_openings(self):
        _, _, before, _ = decode(self.source)
        shared = geometry.shared_edge_controls(before, set(self.definition['curved_meshes']))
        before = [geometry.curved_mesh(m, 3, .018, shared) if i in self.definition['curved_meshes'] else m for i, m in enumerate(before)]
        after = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in before]
        geometry.smooth_wheel_arches(after, self.definition)
        def covered(meshes, parts, side, point):
            for index in parts:
                mesh = meshes[index]
                for face in mesh.faces:
                    if struct.unpack_from('<h', face, 8)[0] != 0:
                        continue
                    tri = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                    if min(side * p[2] for p in tri) < .55:
                        continue
                    a, b, c = [geometry.arch_coordinates(p, side) for p in tri]
                    den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                    if abs(den) < 1e-10:
                        continue
                    wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / den
                    wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / den
                    if min(wa, wb, 1 - wa - wb) >= -.000001:
                        return True
            return False
        for parts, side, arch in (((4, 8), -1, 0), ((0, 8), 1, 0), ((26, 19), -1, 1), ((29, 19), 1, 1)):
            cx, cy, radius = self.definition['wheel_arches'][arch][:3]
            for y in (3, 6, 10, 13):
                for x in range(math.floor(cx - radius - 2), math.ceil(cx + radius + 2)):
                    self.assertEqual(covered(before, parts, side, (x + .25, y)), covered(after, parts, side, (x + .25, y)))
            for degrees in range(75, 106, 5):
                angle = math.radians(degrees)
                inside = cx + (radius - .35) * math.cos(angle), cy + (radius - .35) * math.sin(angle)
                outside = cx + (radius + .35) * math.cos(angle), cy + (radius + .35) * math.sin(angle)
                self.assertFalse(covered(after, parts, side, inside), (parts, degrees))
                self.assertTrue(covered(after, parts, side, outside), (parts, degrees))
        footprints = geometry.wheel_arch_opaque_uvs(after)
        self.assertGreater(len(footprints), 500)
        self.assertTrue(all(0 <= c <= 1 for tri in footprints for p in tri for c in p))
        for tri in footprints:
            # These are only the local, upper original side-atlas bands.
            self.assertTrue(all(p[1] < .2 for p in tri) or all(p[1] > .8 for p in tri))

    def test_local_paint_reflection_preserves_surface_and_outside_uvs(self):
        meshes = [Mesh(bytes(48), [], []) for _ in range(50)]
        mesh = meshes[26]
        for y, z in ((0, 0), (1, 0), (1, 1), (0, 1)):
            mesh.vertex((1, y, z), (1, 0, 0), ((720 + 190 * y) / 1254, (100 + 80 * z) / 1254 - 1))
        mesh.triangle((0, 1, 2), (255, 255, 255, 255), 0)
        mesh.triangle((0, 2, 3), (255, 255, 255, 255), 0)
        geometry.register_paint_motifs(meshes, [{'bounds': [742, 116, 887, 164], 'meshes': [26, 27, 28]}])
        area, mirrored, unchanged = 0, 0, 0
        for face in mesh.faces:
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            a, b, c = vertices
            area += math.sqrt(dot(cross(sub(b[:3], a[:3]), sub(c[:3], a[:3])), cross(sub(b[:3], a[:3]), sub(c[:3], a[:3])))) / 2
            original_center = (720 + 190 * sum(v[1] for v in vertices) / 3, 100 + 80 * sum(v[2] for v in vertices) / 3)
            inside = 742 < original_center[0] < 887 and 116 < original_center[1] < 164
            mirrored += inside
            unchanged += not inside
            for vertex in vertices:
                self.assertEqual(vertex[0], 1)
                self.assertEqual(vertex[5:], (1, 0, 0))
                original_u = 720 + 190 * vertex[1]
                self.assertAlmostEqual(vertex[3] * 1254, 742 + 887 - original_u if inside else original_u)
                self.assertAlmostEqual((vertex[4] + 1) * 1254, 100 + 80 * vertex[2])
        self.assertAlmostEqual(area, 1)
        self.assertGreater(mirrored, 0)
        self.assertGreater(unchanged, 0)

    def test_slanted_motif_reflection_stays_inside_its_silver_island(self):
        meshes = [Mesh(bytes(48), [], []) for _ in range(50)]
        mesh = meshes[4]
        for y, z in ((0, 0), (1, 0), (1, 1), (0, 1)):
            mesh.vertex((1, y, z), (1, 0, 0), ((960 + 210 * y) / 1254, (90 + 100 * z) / 1254 - 1))
        mesh.triangle((0, 1, 2), (255, 255, 255, 255), 0)
        mesh.triangle((0, 2, 3), (255, 255, 255, 255), 0)
        geometry.register_paint_motifs(meshes, [{'quad': [[984, 142], [1148, 109], [1148, 138], [984, 171]], 'meshes': [4, 5, 6]}])
        slope, area, reflected = -33 / 164, 0, 0
        for face in mesh.faces:
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            a, b, c = vertices
            normal = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
            area += math.sqrt(dot(normal, normal)) / 2
            center_x = 960 + 210 * sum(p[1] for p in vertices) / 3
            center_y = 90 + 100 * sum(p[2] for p in vertices) / 3
            inside = 984 < center_x < 1148 and 142 < center_y - slope * (center_x - 984) < 171
            reflected += inside
            for p in vertices:
                x, y = 960 + 210 * p[1], 90 + 100 * p[2]
                self.assertEqual(p[0], 1)
                self.assertEqual(p[5:], (1, 0, 0))
                self.assertAlmostEqual(p[3] * 1254, 984 + 1148 - x if inside else x)
                self.assertAlmostEqual((p[4] + 1) * 1254, y + slope * (984 + 1148 - 2 * x) if inside else y)
                if inside:
                    target_x, target_y = p[3] * 1254, (p[4] + 1) * 1254
                    self.assertGreaterEqual(target_y - slope * (target_x - 984), 142 - 1e-7)
                    self.assertLessEqual(target_y - slope * (target_x - 984), 171 + 1e-7)
        self.assertGreater(reflected, 0)
        self.assertAlmostEqual(area, 1)

    def test_registered_full_front_frames_and_source_rear_charts_preserve_damage(self):
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        originals = self.original[2]
        before = [[tuple(v) for v in mesh.vertices] for mesh in meshes]
        geometry.fit_lamp_uvs(meshes, originals)
        for index in (16, 18, 41, 43, 45, 47, 48):
            self.assertEqual(meshes[index].vertices, originals[index].vertices, index)
            self.assertEqual(meshes[index].faces, originals[index].faces, index)
        for index in (0, 1, 2, 4, 5, 6, 8, 9, 10, 15, 17, 44, 46):
            # Chart splitting creates records without moving source geometry.
            self.assertEqual([v[:3] for v in meshes[index].vertices[:len(before[index])]],
                             [v[:3] for v in before[index]])
        def referenced(index):
            mesh = meshes[index]
            return [mesh.vertices[i] for face in mesh.faces for i in struct.unpack_from('<3H', face, 2)]
        def registered(index):
            mesh = meshes[index]
            return [mesh.vertices[i] for f in mesh.registered_lamp_faces for i in struct.unpack_from('<3H', mesh.faces[f], 2)]
        def records(vertices):
            out = collections.defaultdict(set)
            for p in vertices:
                out[tuple(round(v * 65536) for v in p[:3])].add(tuple(round(v * 65536) for v in p[3:5]))
            return out
        for lamp, fender in ((44, 0), (46, 4)):
            a, b = records(registered(lamp)), records(registered(fender))
            shared = set(a) & set(b)
            self.assertGreaterEqual(len(shared), 4)
            for position in shared:
                # At the same position, painted fender and glass may retain
                # separate UVs; the true glass fold must share its UV exactly.
                self.assertTrue(a[position] & b[position])
            rows = [(p[4] + 1) * 1254 for p in registered(lamp)]
            self.assertLess(min(rows), 542, 'Retain the source upper silver frame')
            self.assertGreater(max(rows), 595, 'Retain the source lower paint border')
            self.assertTrue(all(y <= 582 + 1e-7 or y >= 594 - 1e-7 for y in rows),
                            'Skip green artwork bleed inside the full chart')
            for p in registered(fender):
                x, y = p[3] * 1254, (p[4] + 1) * 1254
                if not 500 < y < 600:
                    self.assertTrue(76 - 1e-7 <= x <= 77.52 + 1e-7 if fender == 0 else
                                    1174.44 - 1e-7 <= x <= 1176 + 1e-7,
                                    'Fender paint must sample its own adjacent silver, never the second oval')
        for index in (8, 9, 10):
            for p in referenced(index):
                x, y = p[3] * 1254, (p[4] + 1) * 1254
                if 830 <= x <= 1230 and 588 <= y < 596:
                    self.assertGreaterEqual(y, 594 - 1e-7, 'Shared bumper rim must also skip green bleed')
        # The different rear white/amber layouts are canonical. Preserve all
        # UVs including their full silver/paint frame, without band realignment.
        for index in (15, 17):
            source_records = records(before[index])
            for position, uvs in records(referenced(index)).items():
                for uv in uvs:
                    self.assertLessEqual(min(max(abs(a - b) for a, b in zip(uv, old))
                                             for old in source_records[position]), 1,
                                         'Identity interpolation may round by at most one16.16 UV tick')

    @unittest.skipUnless((PROJECT / 'assets/models/vehicles/audi/textures/metadata.json').exists(), 'Audi artwork registration not installed')
    def test_bonnet_opacity_release_only_covers_fitted_actual_paint_strip(self):
        import audi_textures
        _, _, meshes, section = geometry.prepare(self.source, self.definition)
        directory = PROJECT / 'assets/models/vehicles/audi/textures'
        audi_textures.fit_uvs(meshes, section, 9, directory)
        positions = [[v[:3] for v in mesh.vertices] for mesh in meshes]
        footprints = geometry.bonnet_opaque_uvs(meshes)
        self.assertTrue(footprints)
        for triangle in footprints:
            for u, v in triangle:
                self.assertGreaterEqual(u, 1220 / 1254 - 1e-12)
                self.assertLessEqual(u, 1240 / 1254 + 1e-12)
                self.assertGreaterEqual(v, 180 / 1254 - 1e-12)
                self.assertLessEqual(v, 550 / 1254 + 1e-12)
            centroid = tuple(sum(p[k] for p in triangle) / 3 for k in (0, 1))
            # Every released texel belongs to an actual fitted paint triangle.
            self.assertIsNotNone(geometry.uv_vertex(meshes[12], (centroid[0], centroid[1] - 1)))
        self.assertEqual(positions, [[v[:3] for v in mesh.vertices] for mesh in meshes])
        # A textureless mesh cannot request opacity for that rectangle alone.
        empty = [Mesh(m.header, list(m.vertices), []) for m in meshes]
        self.assertEqual(geometry.bonnet_opaque_uvs(empty), [])

    @unittest.skipUnless((PROJECT / 'assets/models/vehicles/audi/textures/AUDI.webp').exists(), 'Final Audi artwork not installed')
    def test_actual_registered_artwork_reflectors_are_quantized_and_outward(self):
        import audi_textures
        directory = PROJECT / 'assets/models/vehicles/audi/textures'
        metadata = json.loads((directory / 'metadata.json').read_text())
        regions = metadata.get('optics_recess_uvs', {})
        self.assertTrue(regions)
        output, _ = geometry.generate(self.source, self.definition, directory)
        meshes = decode(output)[2]
        self.assert_faces(meshes, self.original[2])
        _, _, before, section = geometry.prepare(self.source, self.definition)
        audi_textures.fit_uvs(before, section, 9, directory)
        geometry.fit_lamp_uvs(before, self.original[2])
        for index, contours in regions.items():
            for region in contours:
                uv = tuple(sum(p[k] for p in region['outline']) / len(region['outline']) - (1 if k else 0) for k in (0, 1))
                a = geometry.uv_vertex(before[int(index)], uv)
                b = geometry.uv_vertex(meshes[int(index)], uv)
                self.assertAlmostEqual(math.dist(a[:3], b[:3]), region['depth'], delta=.001)


if __name__ == '__main__':
    unittest.main()
