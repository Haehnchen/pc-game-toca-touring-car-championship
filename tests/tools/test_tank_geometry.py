#!/usr/bin/env python3
"""Tank source fidelity, real armor thickness and continuous-spin clearance."""

import json
import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import tank_geometry as geometry
import vehicle_manifest as manifest
from vehicle_model import add, cross, crc32, decode, dot, read_source, sub

ENTRY = next(entry for entry in manifest.load_manifest()['vehicles'] if entry['key'] == 'tank')


class TankGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.definition = manifest.load_definition(ENTRY)
        cls.source = read_source(ENTRY['source'])
        cls.result, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.old = decode(cls.source)
        cls.new = decode(cls.result)
        cls.report = geometry.geometry_report(cls.source, cls.definition)

    def test_registration_matches_the_canonical_bonus_car(self):
        self.assertEqual((ENTRY['render_id'], ENTRY['physical_id'], ENTRY['output']), (8, 8, 'assets/models/mega/car-8.glb'))
        self.assertEqual(ENTRY['source']['member'], 28)
        self.assertEqual(self.stats['source_model_crc32'], '3d3ea7c9')

    def test_hierarchy_guns_damage_bindings_and_material_are_byte_identical(self):
        # Bytes 20..32 are the section offsets, which follow the rebuilt wheel's size.
        self.assertEqual(self.old[0][:20], self.new[0][:20])
        self.assertEqual(struct.unpack_from('<III', self.new[0], 20), (32, 32 + len(self.new[1]), 32 + len(self.new[1]) + sum(len(m.encode()) for m in self.new[2])))
        self.assertEqual(self.old[1], self.new[1])
        self.assertEqual(self.old[3], self.new[3])
        for index in range(2):
            self.assertEqual(self.old[2][index].encode(), self.new[2][index].encode())
        self.assertEqual(len(self.old[2]), len(self.new[2]))

    def test_planar_armor_and_its_motif_uv_charts_stay_canonical(self):
        # The untouched turret side/bird charts and roof/hatch charts must
        # remain the actual source faces, including all UV/color/slot records.
        original, new = self.old[2][geometry.BODY_MESH], self.new[2][geometry.BODY_MESH]
        for i in (*range(161, 173), *range(263, 269)):
            self.assertIn(original.faces[i], new.faces)
            for vertex in geometry.face_vertices(original, original.faces[i]):
                self.assertIn(vertex, new.vertices)

    def test_original_circular_profiles_gain_shared_arcs_without_moving_corners(self):
        original = self.old[2][geometry.BODY_MESH]
        edges = geometry.circular_edges(original, self.definition['fitting_edge_segments'])
        self.assertEqual(len(edges), 6 * 7)
        for (a, b), samples in edges.items():
            self.assertEqual((samples[0], samples[-1]), (a, b))
            self.assertEqual(len(samples), 5)
            # Curvature must add a real arc, while preserving its canonical
            # source endpoints and staying within a modest source sagitta.
            middle = samples[2]
            chord = tuple((x + y) / 2 for x, y in zip(a, b))
            self.assertGreater(math.dist(middle, chord), .005)
            self.assertLess(math.dist(middle, chord), .060)
        used = {tuple(round(x * 65536) for x in vertex[:3])
                for face in self.new[2][geometry.BODY_MESH].faces
                for vertex in geometry.face_vertices(self.new[2][geometry.BODY_MESH], face)}
        for samples in edges.values():
            # Lower vent sectors may be locally trimmed by the tire envelope;
            # the rear hatch and the cannon mouth are away from every wheel.
            if samples[0][0] < -2 or .82 < samples[0][0] < .84:
                for p in samples:
                    self.assertIn(tuple(round(x * 65536) for x in p), used)

    def test_added_vent_arc_uvs_keep_the_original_planar_atlas_projection(self):
        original = self.old[2][geometry.BODY_MESH]
        rounded = geometry.rounded_fittings(original, self.definition)
        groups = geometry.circular_profiles(original)
        edges = geometry.circular_edges(original, self.definition['fitting_edge_segments'])
        for group in groups[1:3]:
            # An independent three-source-point planar projection checks the
            # rendered chart, rather than repeating the generator's ring fit.
            anchors = [max(group, key=lambda p: p[1]), max(group, key=lambda p: p[2]), min(group, key=lambda p: p[2])]
            anchors = [next(v for v in original.vertices if v[:3] == p) for p in anchors]
            a0, a1, a2 = anchors
            by, bz, cy, cz = a1[1] - a0[1], a1[2] - a0[2], a2[1] - a0[1], a2[2] - a0[2]
            determinant = by * cz - bz * cy
            for (a, b), samples in edges.items():
                if a not in group or b not in group:
                    continue
                va = next(v for v in original.vertices if v[:3] == a)
                vb = next(v for v in original.vertices if v[:3] == b)
                midpoint = samples[2]
                linear = tuple((x + y) / 2 for x, y in zip(va, vb))
                dy, dz = midpoint[1] - a0[1], midpoint[2] - a0[2]
                wb, wc = (dy * cz - dz * cy) / determinant, (by * dz - bz * dy) / determinant
                uv = tuple(a0[k] + wb * (a1[k] - a0[k]) + wc * (a2[k] - a0[k]) for k in (3, 4))
                matches = [v for v in rounded.vertices if math.dist(v[:3], midpoint) < 1e-10]
                self.assertTrue(matches)
                self.assertTrue(any(math.dist(v[3:5], uv) < .0007 for v in matches))
                self.assertGreater(math.dist(uv, linear[3:5]), .0005)

    def test_plate_returns_and_recessed_aperture_have_real_depth_and_ghost_scope(self):
        body = self.new[2][geometry.BODY_MESH]
        first = self.stats['ghost_hidden_starts'][geometry.BODY_MESH]
        self.assertEqual(first, self.report['cavity_first_face'])
        self.assertGreater(self.report['armor_return_triangles'], 0)
        self.assertGreater(self.report['cavity_triangles'], 0)
        self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] == -1 for f in body.faces[first:]))
        self.assertTrue(any(struct.unpack_from('<h', f, 8)[0] == 0 for f in body.faces[:first]))
        # The intentional opening stays at its source plane, with a dark
        # recessed back180mm inside; no dark polygon plugs its front plane.
        aperture_x = .8282470703125
        cap_x = aperture_x - self.definition['hatch_depth']
        caps = [geometry.face_vertices(body, f) for f in body.faces[first:]
                if all(abs(v[0] - cap_x) < 2 / 65536 for v in geometry.face_vertices(body, f))]
        self.assertGreater(len(caps), 0)
        self.assertFalse(any(all(abs(v[0] - aperture_x) < 1 / 65536 and v[1] > .5
                                 for v in geometry.face_vertices(body, f)) for f in body.faces[first:]))
        source_min_y = min(v[1] for v in self.old[2][geometry.BODY_MESH].vertices)
        used = [v for f in body.faces for v in geometry.face_vertices(body, f)]
        self.assertGreaterEqual(min(v[1] for v in used), source_min_y - self.definition['armor_thickness'] - 2 / 65536)

    def test_ghost_armor_and_hatch_depth_contract_from_their_actual_outer_skin(self):
        body = self.new[2][geometry.BODY_MESH]
        positions = self.stats['ghost_positions'][geometry.BODY_MESH]
        first = self.stats['ghost_hidden_starts'][geometry.BODY_MESH]
        referenced = {index for face in body.faces[first:] for index in struct.unpack_from('<3H', face, 2)}
        self.assertEqual(set(positions), referenced)
        skin_count = self.report['skin_triangles']
        for outer_face, inner_face in zip(body.faces[:skin_count], body.faces[first:first + skin_count]):
            outer = [body.vertices[i][:3] for i in struct.unpack_from('<3H', outer_face, 2)]
            for index in struct.unpack_from('<3H', inner_face, 2):
                inner = body.vertices[index][:3]
                candidates = [tuple(a + .2 * (b - a) for a, b in zip(anchor, inner)) for anchor in outer]
                self.assertLessEqual(min(math.dist(positions[index], expected) for expected in candidates), 2 / 65536)
        aperture_x = .8282470703125
        ghost_cap_x = aperture_x - .2 * self.definition['hatch_depth']
        mouth_vertices = cap_vertices = 0
        for face in body.faces[first + skin_count:]:
            for index in struct.unpack_from('<3H', face, 2):
                point, ghost = body.vertices[index][:3], positions[index]
                self.assertEqual(point[1:], ghost[1:])
                if abs(point[0] - aperture_x) < 1 / 65536:
                    self.assertEqual(point, ghost)
                    mouth_vertices += 1
                else:
                    self.assertAlmostEqual(ghost[0], ghost_cap_x, delta=1 / 65536)
                    cap_vertices += 1
        self.assertGreater(mouth_vertices, 0)
        self.assertGreater(cap_vertices, 0)

    def test_wheel_keeps_source_radius_width_and_material_slots(self):
        old, new = self.old[2][geometry.WHEEL_MESH], self.new[2][geometry.WHEEL_MESH]
        tread = lambda mesh: {i for f in mesh.faces if struct.unpack_from('<h', f, 8)[0] == -1
                              for i in struct.unpack_from('<3H', f, 2)}
        old_radius = max(math.hypot(old.vertices[i][0], old.vertices[i][1]) for i in tread(old))
        new_radius = max(math.hypot(new.vertices[i][0], new.vertices[i][1]) for i in tread(new))
        self.assertAlmostEqual(new_radius, old_radius, delta=2 / 65536)
        for mesh in (old, new):
            self.assertAlmostEqual(max(abs(v[2]) for v in mesh.vertices), .16949462890625, delta=2 / 65536)
        self.assertEqual({struct.unpack_from('<h', f, 8)[0] for f in new.faces}, {-1, 0})
        # Bytes 12..20 hold the mesh size and counts; name and the rest stay original.
        self.assertEqual(new.header[:12], old.header[:12])
        self.assertEqual(new.header[20:], old.header[20:])

    def test_wheel_is_closed_round_and_outward_wound(self):
        mesh = self.new[2][geometry.WHEEL_MESH]
        segments = self.definition['wheel_segments']
        self.assertEqual(len(mesh.faces), 2 * segments * 3 + 2 * segments)
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


    def test_real_quantized_armor_and_cavity_triangles_clear_every_wheel_spin(self):
        self.assertGreaterEqual(self.report['minimum_clearance_mm'], 10.1)
        self.assertLess(self.report['runtime_rotation_basis_error_bound_mm'], .052)
        self.assertEqual(self.report['runtime_rotation_basis_allowance_mm'], .1)
        self.assertAlmostEqual(self.report['effective_minimum_clearance_mm'],
                               self.report['minimum_clearance_mm'] - .1)
        self.assertGreaterEqual(self.report['effective_minimum_clearance_mm'], 10)
        self.assertEqual([len(w['steering_degrees']) for w in self.report['wheels']], [9, 9, 1, 1])
        for wheel in self.report['wheels']:
            self.assertGreater(wheel['checked_triangles'], 0)
            for gap in wheel['clearance_lower_bounds_mm']:
                self.assertGreaterEqual(gap, 10.1)

    def test_wrong_source_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'canonical HIGH member28'):
            geometry.generate(self.source[:-1] + bytes([self.source[-1] ^ 1]), self.definition)

    def test_definition_binds_to_the_tank_only(self):
        with self.assertRaisesRegex(ValueError, 'Tank geometry requires the canonical Tank source'):
            manifest.load_definition({**ENTRY, 'render_id': 4})


if __name__ == '__main__':
    unittest.main()
