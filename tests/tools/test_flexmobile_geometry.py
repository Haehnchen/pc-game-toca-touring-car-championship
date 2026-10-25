#!/usr/bin/env python3
"""Source-bound Flexmobile curved skin, continuous mouths and tire-safe housings."""

from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import flexmobile_geometry as geometry
import vehicle_manifest as manifest
from vehicle_model import Mesh, add, cross, crc32, decode, dot, read_source, sub

ENTRY = next(entry for entry in manifest.load_manifest()['vehicles'] if entry['key'] == 'flexmobile')


class FlexmobileGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.definition = manifest.load_definition(ENTRY)
        cls.source = read_source(ENTRY['source'])
        cls.result, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.old = decode(cls.source)
        cls.new = decode(cls.result)
        cls.prepared = geometry.prepare(cls.source, cls.definition)
        cls.report = geometry.geometry_report(cls.source, cls.definition)

    def test_registration_matches_the_canonical_bonus_car(self):
        self.assertEqual((ENTRY['render_id'], ENTRY['physical_id'], ENTRY['output']), (9, 9, 'assets/models/mega/car-9.glb'))
        self.assertEqual(ENTRY['source']['member'], 31)
        self.assertEqual(self.stats['source_model_crc32'], '3234277d')

    def test_hierarchy_pivots_source_corners_and_materials_are_preserved(self):
        # Bytes 20..32 are the section offsets, which follow the rebuilt wheel's size.
        self.assertEqual(self.old[0][:20], self.new[0][:20])
        self.assertEqual(struct.unpack_from('<III', self.new[0], 20), (32, 32 + len(self.new[1]), 32 + len(self.new[1]) + sum(len(m.encode()) for m in self.new[2])))
        self.assertEqual(self.old[1], self.new[1])
        self.assertEqual(self.old[3], self.new[3])
        old_body = self.old[2][geometry.BODY_MESH]
        new_body = self.new[2][geometry.BODY_MESH]
        changed_corners = 0
        for a, b in zip(old_body.vertices, new_body.vertices):
            self.assertEqual(a[3:5], b[3:5])
            if a != b:
                self.assertGreater(a[0], 2.5)
                self.assertGreater(a[1], .075)
                self.assertGreater(abs(a[2]), .64)
                self.assertLess(math.dist(a[:3], b[:3]), .18)
                changed_corners += 1
        self.assertGreater(changed_corners, 0)
        self.assertNotEqual(old_body.encode(), new_body.encode())
        self.assertEqual({struct.unpack_from('<h', f, 8)[0] for f in new_body.faces}, {-1, 0, 1})
        self.assertEqual(len(self.old[2]), len(self.new[2]))
        # Original fins, lower chrome folds and rear lamp outlines remain in
        # their exact source coordinates/UVs. The upper front lamps now join
        # their rounded fender/support patches; lower fog lamps stay original.
        output_faces = {tuple(sorted(self.new[2][0].vertices[i][:5] for i in struct.unpack_from('<3H', f, 2)))
                        for f in new_body.faces[:self.stats['wheel_well_first_face']]}
        for index in (*range(10, 18), *range(42, 52), *range(77, 89), *range(91, 103), *range(161, 186), *range(280, 305)):
            source_face = old_body.faces[index]
            vertices = tuple(sorted(old_body.vertices[i][:5] for i in struct.unpack_from('<3H', source_face, 2)))
            self.assertTrue(vertices in output_faces, f'Held source face {index} lost its position/UV identity')

    def test_curved_panels_preserve_source_uv_fields_and_limit_displacement(self):
        old, new = self.old[2][0], self.prepared[2][0]
        registered = Mesh(old.header, list(old.vertices), list(old.faces))
        geometry.register_front_lamp_chart(registered, old)
        source_triangles = [[registered.vertices[i] for i in struct.unpack_from('<3H', old.faces[f], 2)]
                            for f in range(len(old.faces))]
        curved = geometry.curved_body(old, {**self.definition, 'front_lamp_corner_radius': 0., 'front_lamp_support_depth': 0.}, lenses=False)
        moved = 0
        for vertex in curved.vertices[len(old.vertices):]:
            candidates = []
            u, v = vertex[3:5]
            for a, b, c in source_triangles:
                denominator = (b[4] - c[4]) * (a[3] - c[3]) + (c[3] - b[3]) * (a[4] - c[4])
                if abs(denominator) < 1e-10:
                    continue
                wa = ((b[4] - c[4]) * (u - c[3]) + (c[3] - b[3]) * (v - c[4])) / denominator
                wb = ((c[4] - a[4]) * (u - c[3]) + (a[3] - c[3]) * (v - c[4])) / denominator
                weights = wa, wb, 1 - wa - wb
                if min(weights) < -1e-6:
                    continue
                point = tuple(sum(w * p[k] for w, p in zip(weights, (a, b, c))) for k in range(3))
                candidates.append(math.dist(vertex[:3], point))
            self.assertTrue(candidates)
            self.assertLessEqual(min(candidates), self.definition['body_max_curve'] + 1e-6)
            moved += min(candidates) > .001
        self.assertGreater(moved, 100)

    def test_upper_lamp_support_curves_are_shared_across_the_atlas_seams(self):
        old = self.old[2][0]
        curved = geometry.curved_body(old, self.definition, lenses=False)
        for first, last in ((2, 4), (4, 6), (9, 11), (11, 13)):
            a, b = old.vertices[first], old.vertices[last]
            du, dv = b[3] - a[3], b[4] - a[4]
            length = du * du + dv * dv
            samples = []
            for p in curved.vertices[len(old.vertices):]:
                t = ((p[3] - a[3]) * du + (p[4] - a[4]) * dv) / length
                if not 1e-5 < t < 1 - 1e-5:
                    continue
                if abs(du * (p[4] - a[4]) - dv * (p[3] - a[3])) > 1e-10:
                    continue
                linear = tuple(a[k] + t * (b[k] - a[k]) for k in range(3))
                samples.append(math.dist(p[:3], linear))
                self.assertTrue(any(math.dist(p[:3], q[:3]) < 2 / 65536 and
                                    math.dist(p[3:5], q[3:5]) > .01
                                    for q in curved.vertices),
                                f'Lamp/fender seam at {first}/{last} has no joined UV copy')
            self.assertGreaterEqual(len(samples), 2)
            self.assertGreater(max(samples), .001)

    def test_rounded_bays_keep_conforming_fender_grille_and_bonnet_joins(self):
        old = self.old[2][0]
        curved = geometry.curved_body(old, self.definition, lenses=False)
        def physical_edge(p, q):
            return tuple(sorted(tuple(round(v * 65536) for v in point[:3]) for point in (p, q)))
        all_edges = Counter()
        for face in curved.faces:
            tri = [curved.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            all_edges.update(physical_edge(p, q) for p, q in zip(tri, tri[1:] + tri[:1]))
        # Both fender returns; both halves of the bonnet/grille top; both
        # vertical inner lamp/grille returns, including their lower endpoints.
        for first, last in ((158, 159), (264, 266), (106, 108), (108, 120),
                            (0, 1), (7, 8), (5, 6), (12, 13)):
            a, b = old.vertices[first], old.vertices[last]
            du, dv = b[3] - a[3], b[4] - a[4]
            length = du * du + dv * dv
            def on_edge(p):
                t = ((p[3] - a[3]) * du + (p[4] - a[4]) * dv) / length
                return (-1e-9 <= t <= 1 + 1e-9 and
                        abs(du * (p[4] - a[4]) - dv * (p[3] - a[3])) < 1e-10)
            edges = set()
            for face in curved.faces:
                tri = [curved.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                for p, q in zip(tri, tri[1:] + tri[:1]):
                    if on_edge(p) and on_edge(q):
                        edges.add(physical_edge(p, q))
            self.assertGreaterEqual(len(edges), 3)
            self.assertTrue(all(all_edges[edge] == 2 for edge in edges),
                            f'Rounded bay seam {first}/{last} has an unmatched edge')

    def test_full_optical_bays_have_visible_roll_while_lower_pedestals_stay_original(self):
        old = self.old[2][0]
        shaped = geometry.curved_body(old, self.definition, lenses=False)
        flat = geometry.curved_body(old, {**self.definition, 'front_lamp_corner_radius': 0.,
                                         'front_lamp_support_depth': 0.}, lenses=False)
        forward, shoulder = 0., 0.
        self.assertEqual(shaped.faces, flat.faces)
        for p, q in zip(shaped.vertices, flat.vertices):
            self.assertEqual(p[3:5], q[3:5])
            if q[1] <= .075:
                self.assertEqual(p[:3], q[:3])
            forward = max(forward, p[0] - q[0])
            shoulder = max(shoulder, q[1] - p[1])
        self.assertGreater(forward, .025)
        self.assertGreater(shoulder, .01)

    def test_four_upper_lenses_remain_round_and_form_closed_rolled_shells(self):
        old = self.old[2][0]
        support = geometry.curved_body(old, self.definition, lenses=False)
        raised = geometry.curved_body(old, self.definition)
        self.assertEqual(raised.faces[:len(support.faces)], support.faces)
        self.assertEqual(raised.vertices[:len(support.vertices)], support.vertices)
        triangles = [struct.unpack_from('<3H', f, 2) for f in raised.faces[len(support.faces):]]
        edges = Counter(tuple(sorted(edge)) for tri in triangles
                        for edge in zip(tri, (*tri[1:], tri[0])))
        self.assertTrue(all(n == 2 for n in edges.values()), 'Each optical shell must be closed')
        directed = Counter(edge for tri in triangles for edge in zip(tri, (*tri[1:], tri[0])))
        self.assertTrue(all(n == directed[b, a] for (a, b), n in directed.items()),
                        'Closed optical shells need consistent outward winding')
        original_front = Mesh(old.header, old.vertices, old.faces[:10])
        centers = [geometry.panels.uv_vertex(original_front, (x / 256, y / 256 - 1), paint=0)
                   for x, y in geometry.FRONT_LENS_CENTERS]
        extents = [[] for _ in centers]
        for p in raised.vertices[len(support.vertices):]:
            distances = [math.hypot(p[3] * 256 - x, (p[4] + 1) * 256 - y)
                         for x, y in geometry.FRONT_LENS_CENTERS]
            lamp = min(range(len(distances)), key=distances.__getitem__)
            self.assertLessEqual(distances[lamp], self.definition['front_lens_radius_pixels'] + 1e-9)
            q = centers[lamp]
            radius = math.hypot(p[1] - q[1], p[2] - q[2])
            self.assertAlmostEqual(radius, distances[lamp] * 5.78 / 256, delta=.0005,
                                   msg='Optics must stay circular even when the support is rolled')
        for tri in triangles:
            ps = [raised.vertices[i] for i in tri]
            area = (ps[1][3] - ps[0][3]) * (ps[2][4] - ps[0][4]) - (ps[1][4] - ps[0][4]) * (ps[2][3] - ps[0][3])
            if area > 1e-12:
                self.assertGreater(cross(sub(ps[1][:3], ps[0][:3]), sub(ps[2][:3], ps[0][:3]))[0], 0.)
                for p in ps:
                    lamp = min(range(len(centers)), key=lambda i: math.hypot(p[3] - centers[i][3], p[4] - centers[i][4]))
                    # Remove the canonical front plane's seven-degree tilt.
                    extents[lamp].append(p[0] - .12857 * p[1])
        self.assertTrue(all(.005 < max(vs) - min(vs) < .025 for vs in extents))

    def test_quantized_lens_fronts_stay_ahead_of_the_actual_shaped_support(self):
        old = self.old[2][0]
        support = geometry.curved_body(old, self.definition, lenses=False)
        raised = geometry.curved_body(old, self.definition)
        support.vertices = [tuple(round(v * 65536) / 65536 for v in p) for p in support.vertices]
        samples, clearance = 0, float('inf')
        for face in raised.faces[len(support.faces):]:
            tri = [raised.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            area = (tri[1][3] - tri[0][3]) * (tri[2][4] - tri[0][4]) - (tri[1][4] - tri[0][4]) * (tri[2][3] - tri[0][3])
            if area < 1e-12:
                continue
            quantized = [tuple(round(v * 65536) / 65536 for v in p) for p in tri]
            y0, y1 = min(p[1] for p in tri), max(p[1] for p in tri)
            z0, z1 = min(p[2] for p in tri), max(p[2] for p in tri)
            near = Mesh(support.header, support.vertices, [f for f in support.faces
                        if (lambda vs: min(p[0] for p in vs) > 2.7 and max(p[1] for p in vs) >= y0 and
                         min(p[1] for p in vs) <= y1 and max(p[2] for p in vs) >= z0 and min(p[2] for p in vs) <= z1)
                        ([support.vertices[k] for k in struct.unpack_from('<3H', f, 2)])])
            for i in range(5):
                for j in range(5 - i):
                    weights = (i / 4, j / 4, 1 - (i + j) / 4)
                    p = tuple(sum(w * q[k] for w, q in zip(weights, quantized)) for k in range(3))
                    q, _ = geometry.panels.surface(near, (0., p[1], p[2]), 0, 1)
                    clearance = min(clearance, p[0] - q[0])
                    samples += 1
        self.assertGreater(samples, 1000)
        self.assertGreater(clearance, .001)

    def test_all_front_contour_vertices_follow_the_physical_smooth_boundary(self):
        body = self.prepared[2][0]
        centers = geometry.wheel_centers(self.prepared[1])
        for wheel, well in enumerate(body.well_geometry):
            boundary = geometry.arch_boundary(centers[wheel], self.definition, wheel < 2)
            def segment_distance(point, a, b):
                edge = sub(b, a)
                t = max(0., min(1., dot(sub(point, a), edge) / dot(edge, edge)))
                return math.dist(point, add(a, tuple(t * v for v in edge)))
            for point in well['mouth']:
                distance = min(segment_distance(point[:2], a, b)
                               for a, b in zip(boundary[:-1], boundary[1:]))
                self.assertLessEqual(distance, 2 / 65536)
            if wheel < 2:
                crown = max(p[1] for p in well['mouth'])
                self.assertAlmostEqual(crown, centers[wheel][1] + self.definition['front_arch_radius'], delta=.012)
            else:
                self.assertLess(max(p[1] for p in well['mouth']), -.22)

    def test_four_housings_are_closed_behind_one_continuous_body_join(self):
        body = self.prepared[2][0]
        self.assertEqual(self.stats['ghost_hidden_starts'], {0: body.well_first_face})
        self.assertEqual(len(body.well_geometry), 4)
        skin_edges = []
        for face in body.faces[:body.well_first_face]:
            points = [body.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
            skin_edges.extend(zip(points, points[1:] + points[:1]))
        for well in body.well_geometry:
            edges = Counter()
            lower_lip = min(p[1] for p in well['mouth'])
            for face in body.faces[well['first_face']:well['first_face'] + well['face_count']]:
                points = [tuple(round(v * 65536) for v in body.vertices[i][:3])
                          for i in struct.unpack_from('<3H', face, 2)]
                self.assertGreaterEqual(min(p[1] / 65536 for p in points), lower_lip - 1 / 65536)
                for a, b in zip(points, points[1:] + points[:1]):
                    edges[tuple(sorted((a, b)))] += 1
            self.assertEqual(set(edges.values()), {1, 2})
            boundary = [edge for edge, count in edges.items() if count == 1]
            degree = Counter(p for edge in boundary for p in edge)
            self.assertEqual(set(degree.values()), {2})
            neighbors = {p: set() for p in degree}
            for a, b in boundary:
                neighbors[a].add(b)
                neighbors[b].add(a)
            seen, pending = set(), [next(iter(neighbors))]
            while pending:
                point = pending.pop()
                if point not in seen:
                    seen.add(point)
                    pending.extend(neighbors[point] - seen)
            self.assertEqual(seen, set(neighbors))  # One intended open mouth.
            for point in well['mouth']:
                def distance(edge):
                    a, b = edge
                    delta = sub(b, a)
                    if not dot(delta, delta):
                        return math.dist(point, a)
                    t = min(1., max(0., dot(sub(point, a), delta) / dot(delta, delta)))
                    return math.dist(point, add(a, tuple(t * c for c in delta)))
                self.assertLessEqual(min(map(distance, skin_edges)), 4 / 65536)

    def test_ghost_housings_keep_exact_mouths_and_one_fifth_strip_depth(self):
        body = self.prepared[2][geometry.BODY_MESH]
        positions = self.stats['ghost_positions'][geometry.BODY_MESH]
        quantize = lambda p: tuple(round(v * 65536) / 65536 for v in p)
        expected = {}
        for well in body.well_geometry:
            for row in ('mouth', 'returned', 'throat', 'inner'):
                for point, lip in zip(well[row], well['mouth']):
                    ghost = (lip[0], lip[1], lip[2] + .2 * (point[2] - lip[2]))
                    expected.setdefault(quantize(point), set()).add(quantize(ghost))
            center = tuple(sum(p[k] for p in well['inner']) / len(well['inner']) for k in range(3))
            anchor = sum(p[2] for p in well['mouth']) / len(well['mouth'])
            ghost_center = (*(sum(p[k] for p in well['mouth']) / len(well['mouth']) for k in range(2)),
                            anchor + .2 * (center[2] - anchor))
            expected[quantize(center)] = {quantize(ghost_center)}
        referenced = {index for face in body.faces[body.well_first_face:] for index in struct.unpack_from('<3H', face, 2)}
        self.assertEqual(set(positions), referenced)
        for index, ghost in positions.items():
            self.assertIn(ghost, expected[quantize(body.vertices[index][:3])])
        for well in body.well_geometry:
            for lip in well['mouth']:
                self.assertEqual(expected[quantize(lip)], {quantize(lip)})

    def test_quantized_skin_and_housings_have_nine_angle_arbitrary_spin_clearance(self):
        bounds = self.report['triangle_clearance_lower_bounds_mm']
        self.assertEqual([len(row) for row in bounds], [9, 9, 1, 1])
        for row in bounds:
            self.assertGreaterEqual(min(row), .01 * 1000)
        # Vertices alone are insufficient: this large triangle crosses the
        # wheel interior even though all three vertices lie beyond the tread.
        tri = [(1., 0., 0.), (-1., 1., 0.), (-1., -1., 0.)]
        self.assertEqual(geometry.tire_triangle_clearance(tri, (0, 0, 0), 0, self.definition), 0)


    def test_opacity_release_uses_only_final_front_skin_uvs(self):
        triangles = geometry.wheel_arch_opaque_uvs(self.prepared[2])
        self.assertTrue(triangles)
        body = self.prepared[2][0]
        for face_index in body.wheel_arch_faces:
            self.assertLess(face_index, body.well_first_face)
            points = [body.vertices[i] for i in struct.unpack_from('<3H', body.faces[face_index], 2)]
            self.assertGreater(max(p[0] for p in points), 1.4)
            self.assertEqual(struct.unpack_from('<h', body.faces[face_index], 8)[0], 0)

    def test_wheel_keeps_source_radius_width_and_material_slots(self):
        old, new = self.old[2][geometry.WHEEL_MESH], self.new[2][geometry.WHEEL_MESH]
        old_tread = {i for f in old.faces if struct.unpack_from('<h', f, 8)[0] == -1
                     for i in struct.unpack_from('<3H', f, 2)}
        # The rebuilt round tyre must stay inside the source seven-gon ring's
        # radial span and keep its width and material slots.
        radii = [math.hypot(old.vertices[i][0], old.vertices[i][1]) for i in old_tread]
        new_radius = max(math.hypot(v[0], v[1]) for v in new.vertices)
        self.assertGreaterEqual(new_radius, min(radii) - 2 / 65536)
        self.assertLessEqual(new_radius, max(radii) + 2 / 65536)
        self.assertAlmostEqual(max(abs(v[2]) for v in old.vertices), max(abs(v[2]) for v in new.vertices), delta=2 / 65536)
        self.assertEqual({struct.unpack_from('<h', f, 8)[0] for f in new.faces}, {-1, 0})
        # Bytes 12..20 hold the mesh size and counts; name and the rest stay original.
        self.assertEqual(new.header[:12], old.header[:12])
        self.assertEqual(new.header[20:], old.header[20:])

    def test_wheel_is_closed_round_and_outward_wound(self):
        mesh = self.new[2][geometry.WHEEL_MESH]
        segments = self.definition['wheel_segments']
        self.assertEqual(len(mesh.faces), 2 * 3 * segments + 2 * segments)
        for face in mesh.faces:
            a, b, c = (mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2))
            normal = add(add(a[5:], b[5:]), c[5:])
            area = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
            self.assertGreater(dot(area, area), 1e-12)
            self.assertGreaterEqual(dot(area, normal), 0)

    def test_hub_disc_samples_only_the_source_wheel_picture(self):
        mesh = self.new[2][geometry.WHEEL_MESH]
        centre_u, centre_v = self.definition['wheel_patch_center']
        half = self.definition['wheel_patch_half']
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != 0:
                continue
            for index in struct.unpack_from('<3H', face, 2):
                u, v = mesh.vertices[index][3:5]
                self.assertLessEqual(abs(u - centre_u), half + 1e-6)
                self.assertLessEqual(abs(v - centre_v), half + 1e-6)

    def test_wrong_source_or_invalid_artwork_directory_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'canonical HIGH member31'):
            geometry.generate(self.source[:-1] + bytes([self.source[-1] ^ 1]), self.definition)
        with self.assertRaisesRegex(FileNotFoundError, 'flexmobile-missing-directory'):
            geometry.generate(self.source, self.definition, PROJECT / 'build/flexmobile-missing-directory')

    def test_definition_binds_to_the_flexmobile_only(self):
        with self.assertRaisesRegex(ValueError, 'Flexmobile geometry requires the canonical Flexmobile source'):
            manifest.load_definition({**ENTRY, 'render_id': 4})


if __name__ == '__main__':
    unittest.main()
