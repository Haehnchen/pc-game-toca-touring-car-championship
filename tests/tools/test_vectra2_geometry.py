#!/usr/bin/env python3
"""Vectra2 source identity, render bindings and actual arch clearance."""

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
import vehicle_wheel_stance as stance
import vehicle_wheel_wells as wells
from git_game_data import configured_data_root
from vehicle_model import add, crc32, cross, decode, dot, read_source, sub


def clip_axial(polygon, bound, sign):
    result = []
    if not polygon:
        return result
    a, da = polygon[-1], sign * (polygon[-1][2] - bound)
    for b in polygon:
        db = sign * (b[2] - bound)
        if (da >= 0) != (db >= 0):
            result.append(tuple(a[k] + da / (da - db) * (b[k] - a[k]) for k in range(3)))
        if db >= 0:
            result.append(b)
        a, da = b, db
    return result


def nearest_radial(polygon):
    """Exact distance from the axle to a convex triangle/slab intersection."""
    minimum, turns = math.inf, []
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = dx * dx + dy * dy
        t = max(0., min(1., -(a[0] * dx + a[1] * dy) / length)) if length else 0.
        minimum = min(minimum, math.hypot(a[0] + t * dx, a[1] + t * dy))
        turns.append(a[0] * b[1] - a[1] * b[0])
    return 0. if min(turns) > 1e-12 or max(turns) < -1e-12 else minimum


def triangle_clearance(triangles, centers, profile):
    """Conservative full-face radial clearance, including arbitrary wheel spin.

    Intersect each actual triangle with thin axial slabs of the continuous
    tire shoulder. The exact nearest radial point is compared with the slab's
    greatest tire radius, so interiors and edges participate, not only vertices.
    """
    slabs = []
    for (za, ra), (zb, rb) in zip(profile, profile[1:]):
        for step in range(16):
            low, high = step / 16, (step + 1) / 16
            slabs.append((za + (zb - za) * low, za + (zb - za) * high,
                          max(ra + (rb - ra) * low, ra + (rb - ra) * high)))
    minimum = [[math.inf] * 3 for _ in centers]
    maximum = 398 * math.tau / 4096
    for triangle in triangles:
        centroid = tuple(sum(p[k] for p in triangle) / 3 for k in range(3))
        wheel = min(range(4), key=lambda i: abs(centroid[0] - centers[i][0]) + abs(centroid[2] - centers[i][2]))
        center = centers[wheel]
        for state, angle in enumerate((-maximum, 0., maximum)):
            if wheel >= 2:
                angle = 0.
            c, s = math.cos(angle), math.sin(angle)
            points = [(c * (p[0] - center[0]) - s * (p[2] - center[2]), p[1] - center[1],
                       s * (p[0] - center[0]) + c * (p[2] - center[2])) for p in triangle]
            low, high = min(p[2] for p in points), max(p[2] for p in points)
            for za, zb, radius in slabs:
                if low > zb or high < za:
                    continue
                polygon = clip_axial(clip_axial(points, za, 1), zb, -1)
                if len(polygon) >= 3:
                    minimum[wheel][state] = min(minimum[wheel][state], nearest_radial(polygon) - radius)
    return minimum


def clearance_reports(before, after, definition):
    floor, _, centers = wells.anchors(after[1])
    mesh = after[2][floor]
    liners = [[mesh.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
              for face in mesh.faces[len(before[2][floor].faces)::2]]
    records = wells.node_records(before[1])
    origin = next(r['origin'] for r in records if r['id'] == 14)
    paint = []
    for node in records:
        if not node['variants'] or node['id'] in (1, 2, 3, 4, 14):
            continue
        mesh = before[2][node['variants'][0]]
        offset = sub(node['origin'], origin)
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != 0:
                continue
            triangle = [add(mesh.vertices[i][:3], offset) for i in struct.unpack_from('<3H', face, 2)]
            if min(p[2] for p in triangle) > .55 or max(p[2] for p in triangle) < -.55:
                paint.append(triangle)
    return {'liner': triangle_clearance(liners, centers, definition['wheel_profile']),
            'paint': triangle_clearance(paint, centers, definition['wheel_profile'])}


class Vectra2GeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data_root = configured_data_root()
        inventory = runpy.run_path(str(PROJECT / 'tools/inventory-vehicles.py'))['inventory'](cls.data_root)
        cls.entry = next(v for v in inventory['vehicles'] if v['key'] == 'vectra2')
        cls.definition = json.loads((PROJECT / 'assets/models/vehicles/vectra2/definition.json').read_text())
        cls.source = read_source(cls.entry['models']['high']['source'], cls.data_root)
        cls.original = decode(cls.source)
        cls.model, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.generated = decode(cls.model)
        cls.adjusted, cls.stance = stance.apply(cls.model, cls.definition)
        cls.lined, cls.well_stats = wells.apply(cls.adjusted, cls.definition)

    def test_source_members_and_exact_damage_node_contract(self):
        self.assertEqual(crc32(self.source), 'e0ce56fe')
        self.assertEqual(struct.unpack_from('<3H', self.generated[0], 12), (25, 50, 7))
        self.assertEqual(self.generated[1], self.original[1])
        self.assertEqual(self.generated[3], self.original[3])
        self.assertEqual([m.header[:12] for m in self.generated[2]], [m.header[:12] for m in self.original[2]])
        for level, member, checksum in (('high', 10, 'e0ce56fe'), ('medium', 11, '64a71c41'), ('low', 12, '03dee6e7')):
            source = self.entry['models'][level]['source']
            self.assertEqual(source['member'], member)
            self.assertEqual(crc32(read_source(source, self.data_root)), checksum)
        self.assertEqual(self.model, geometry.generate(self.source, self.definition)[0])

    def test_source_bound_reuse_retains_warwick_positions_and_numeric_uvs(self):
        wrong = dict(self.definition, source_model_crc32='943e9422')
        with self.assertRaisesRegex(ValueError, 'canonical HIGH member7'):
            geometry.generate(self.source, wrong)
        for index in (3, 7, 17, 23, 25, 29, 33, 41, 43, 45, 47, 48):
            self.assertEqual(self.generated[2][index].encode(), self.original[2][index].encode(), index)
        for healthy, damaged in ((0, 2), (4, 6), (8, 10), (11, 13), (15, 16), (18, 19), (20, 21), (26, 28), (30, 32)):
            self.assertNotEqual(self.generated[2][healthy].vertices, self.generated[2][damaged].vertices)
        for index in (8, 9, 11, 12):
            original = self.original[2][index]
            uv = {p[3:5] for face in original.faces if struct.unpack_from('<h', face, 8)[0] == 5
                  for p in [original.vertices[i] for i in struct.unpack_from('<3H', face, 2)]}
            kept = {p[3:5] for face in self.generated[2][index].faces if struct.unpack_from('<h', face, 8)[0] == 5
                    for p in [self.generated[2][index].vertices[i] for i in struct.unpack_from('<3H', face, 2)]}
            self.assertEqual(kept, uv)

    def test_quantized_faces_have_finite_outward_normals_and_no_degenerates(self):
        for index, mesh in enumerate(decode(self.lined)[2]):
            self.assertLess(max(len(mesh.vertices), len(mesh.faces)), 65536)
            self.assertTrue(all(math.isfinite(v) for p in mesh.vertices for v in p), index)
            for face in mesh.faces:
                a, b, c = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                n = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
                self.assertGreater(dot(n, n), 1e-16, index)
                self.assertGreater(dot(n, add(add(a[5:], b[5:]), c[5:])), 0, index)

    def test_touring_standard_and_liners_preserve_the_source_matrices(self):
        self.assertEqual(self.model, self.adjusted)
        before, after = decode(self.adjusted), decode(self.lined)
        self.assertEqual(before[1], after[1])
        self.assertEqual(before[3], after[3])
        floor = self.well_stats['wheel_well_mesh']
        self.assertEqual(self.well_stats['wheel_well_count'], 4)
        for index, (a, b) in enumerate(zip(before[2], after[2])):
            if index == floor:
                self.assertEqual(b.faces[:len(a.faces)], a.faces)
                self.assertEqual(b.vertices[:len(a.vertices)], a.vertices)
            else:
                self.assertEqual(a.encode(), b.encode())

    def test_actual_liner_and_paint_triangles_clear_spun_tire_at_both_maximum_steering_angles(self):
        reports = clearance_reports(decode(self.adjusted), decode(self.lined), self.definition)
        for surface, wheels in reports.items():
            for wheel, states in enumerate(wheels):
                for state, clearance in enumerate(states):
                    with self.subTest(surface=surface, wheel=wheel, steering=state):
                        self.assertTrue(math.isfinite(clearance))
                        self.assertGreater(clearance, .010)

    def test_front_lamp_registration_preserves_complete_side_silhouette_and_geometry(self):
        _, nodes, meshes, _ = geometry.prepare(self.source, self.definition)
        before = [(list(m.vertices), list(m.faces)) for m in meshes]
        geometry.register_front_lamp_frames(meshes, self.original[2])
        for index, mesh in enumerate(meshes):
            vertices, faces = before[index]
            with self.subTest(mesh=index):
                self.assertEqual(mesh.faces, faces)
                self.assertEqual([p[:3] + p[5:] for p in mesh.vertices],
                                 [p[:3] + p[5:] for p in vertices])
                if index not in (26, 27, 44, 46):
                    self.assertEqual(mesh.vertices, vertices)
                else:
                    self.assertNotEqual(mesh.vertices, vertices)
                    self.assertEqual([p[3] for p in mesh.vertices], [p[3] for p in vertices])
                    for old, new in zip(vertices, mesh.vertices):
                        if old != new:
                            # The complete front-frame motif continues below
                            # the original lens boundary onto the same bumper.
                            self.assertTrue(.31 < new[3] < .66)
                            self.assertTrue(47 / 256 - 1 < new[4] < 61 / 256 - 1)
        # The lower corner is duplicated in the lens and in the front-facing
        # bumper chart. It must receive one registration in both records.
        for lamp, corner in ((44, 18), (46, 30)):
            source = self.original[2][26].vertices[corner]
            uv = []
            for index in (lamp, 26, 27):
                matches = [p for p in meshes[index].vertices if p[3] > .3 and math.dist(p[:3], source[:3]) < 1e-8]
                self.assertTrue(matches, index)
                uv.append(matches[0][3:5])
            self.assertEqual(uv[0], uv[1])
            self.assertEqual(uv[0], uv[2])
        self.assertEqual(nodes, self.original[1])


if __name__ == '__main__':
    unittest.main()
