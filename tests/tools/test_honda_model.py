#!/usr/bin/env python3
"""Canonical Accord structure, panel joins, curved optics and closed mirrors."""

import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import honda_geometry as geometry
from vehicle_manifest import load_manifest, load_definition
from vehicle_model import Mesh, read_source, decode, cross, sub, dot, add, scale


class HondaGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entry = next(e for e in load_manifest()['vehicles'] if e['key'] == 'honda')
        cls.definition = load_definition(cls.entry)
        cls.source = read_source(cls.entry['source'])
        cls.original = decode(cls.source)
        cls.output, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.generated = decode(cls.output)

    def test_canonical_structure_and_original_media_survive(self):
        header, nodes, meshes, textures = self.generated
        old_header, old_nodes, old_meshes, old_textures = self.original
        self.assertEqual(header[:20], old_header[:20])
        self.assertEqual(nodes, old_nodes)
        self.assertEqual(textures, old_textures)
        self.assertEqual([m.header[:12] for m in meshes], [m.header[:12] for m in old_meshes])
        self.assertEqual(geometry.generate(self.source, self.definition)[0], self.output)
        self.assertEqual(self.stats['original_visible_triangles'], 317)
        self.assertGreater(self.stats['visible_triangles'], 6000)
        self.assertLess(self.stats['visible_triangles'], 12000)
        self.assertEqual(self.stats['source_model_crc32'], '3562e854')
        # Fully removed panels, lost mirrors and broken optics must never gain
        # intact skins, lenses or a replacement mirror through a shared slot.
        for index in (3, 7, 12, 15, 19, 36, 38, 40, 42, 44, 46):
            self.assertEqual(meshes[index].vertices, old_meshes[index].vertices, index)
            self.assertEqual(meshes[index].faces, old_meshes[index].faces, index)

    def assert_quantized_faces(self, meshes):
        for index, mesh in enumerate(meshes):
            self.assertLess(max(len(mesh.vertices), len(mesh.faces)), 65536)
            self.assertTrue(all(math.isfinite(v) for p in mesh.vertices for v in p))
            for face in mesh.faces:
                indices = struct.unpack_from('<3H', face, 2)
                self.assertLess(max(indices), len(mesh.vertices))
                a, b, c = [mesh.vertices[i] for i in indices]
                normal = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
                self.assertGreater(dot(normal, normal), 1e-16, index)
                self.assertGreater(dot(normal, add(add(a[5:], b[5:]), c[5:])), 0, index)

    def test_generated_fixed_point_faces_are_finite_and_outward(self):
        self.assert_quantized_faces(self.generated[2])

    @unittest.skipUnless((PROJECT / 'assets/models/vehicles/honda/textures/HONDA.webp').exists(), 'Final Honda artwork not installed')
    def test_final_artwork_optics_after_uv_registration_and_quantization(self):
        import honda_textures
        directory = PROJECT / self.entry['texture_directory']
        metadata = json.loads((directory / 'metadata.json').read_text())
        regions = metadata.get('optics_recess_uvs', {})
        self.assertTrue(regions, 'Accepted artwork must supply its registered reflector regions')
        output, _ = geometry.generate(self.source, self.definition, directory)
        header, nodes, meshes, _ = decode(output)
        self.assertEqual(nodes, self.original[1])
        self.assertEqual(header[:20], self.original[0][:20])
        self.assert_quantized_faces(meshes)
        # Compare actual registered source glass against the packed final bowls,
        # not a synthetic contour, including both independent UV islands.
        _, _, before, section = geometry.prepare(self.source, self.definition)
        honda_textures.fit_uvs(before, section, 9, directory)
        for index, contours in regions.items():
            for region in contours:
                point = tuple(sum(p[k] for p in region['outline']) / len(region['outline']) for k in (0, 1))
                uv = point[0], point[1] - 1
                start = geometry.uv_vertex(before[int(index)], uv)
                end = geometry.uv_vertex(meshes[int(index)], uv)
                self.assertAlmostEqual(math.dist(start[:3], end[:3]), region['depth'], delta=.001)

    def test_honda_wheel_profile_and_single_spoke_set_preserve_body_pivots(self):
        wheel = self.generated[2][49]
        self.assertEqual(self.definition['wheel_spokes'], 14)
        self.assertEqual(self.definition['wheel_segments'], 32)
        self.assertGreater(len(wheel.faces), 900)
        for row, (z, radius) in enumerate(self.definition['wheel_profile']):
            for vertex in wheel.vertices[row * 32:(row + 1) * 32]:
                self.assertAlmostEqual(vertex[2], z, delta=.00002)
                self.assertAlmostEqual(math.hypot(vertex[0], vertex[1]), radius, delta=.00002)
        # The recessed disc must not repeat the painted spokes below the 14
        # physical ones. Its neutral gray is in the original Honda wheel palette.
        self.assertTrue(all(struct.unpack_from('<h', face, 8)[0] < 0 for face in wheel.faces))
        white_faces = [face for face in wheel.faces if tuple(face[10:14]) == geometry.WHITE]
        self.assertEqual(len(white_faces), 14 * 2 * 2)  # 14 spokes, two sides, two top triangles.
        backplate = tuple(self.definition['wheel_backplate_color'])
        disc_faces = [face for face in wheel.faces if tuple(face[10:14]) == backplate]
        self.assertEqual(len(disc_faces), 32 * 2)
        from vehicle_textures import texture_records, bmp_pixels
        source_atlas = texture_records(self.original[3], 9)[1][2]
        self.assertIn(backplate[:3], bmp_pixels(source_atlas)[2])
        self.assertEqual(self.generated[1], self.original[1])

    def test_both_mirrors_are_closed_after_quantization(self):
        for index in (35, 37):
            mesh = self.generated[2][index]
            positions = [p[:3] for p in mesh.vertices]
            edges = {}
            for face in mesh.faces:
                points = [positions[i] for i in struct.unpack_from('<3H', face, 2)]
                for a, b in zip(points, points[1:] + points[:1]):
                    self.assertNotEqual(a, b)
                    edges.setdefault(tuple(sorted((a, b))), []).append(a < b)
            for directions in edges.values():
                self.assertEqual(sorted(directions), [False, True], index)
            self.assertTrue(any(tuple(face[10:14]) == geometry.GLASS for face in mesh.faces))
            source = self.original[2][index]
            for axis in range(3):
                self.assertGreaterEqual(min(p[axis] for p in positions), min(p[axis] for p in source.vertices) - .022)
                self.assertLessEqual(max(p[axis] for p in positions), max(p[axis] for p in source.vertices) + .022)

    def test_optics_retain_each_original_contour_and_uv_chart(self):
        for index in (39, 41, 43, 45):
            original, generated = self.original[2][index], self.generated[2][index]
            self.assertGreater(len(generated.faces), 30 * len(original.faces))
            positions = {p[:3] for p in generated.vertices}
            for point in original.vertices:
                self.assertIn(point[:3], positions)
            for face in generated.faces:
                texture = struct.unpack_from('<h', face, 8)[0]
                self.assertIn(texture, (-1, 1))
                for i in struct.unpack_from('<3H', face, 2):
                    point = generated.vertices[i]
                    if texture < 0:
                        continue
                    self.assertGreaterEqual(point[3], min(p[3] for p in original.vertices) - .00002)
                    self.assertLessEqual(point[3], max(p[3] for p in original.vertices) + .00002)
                    self.assertGreaterEqual(point[4], min(p[4] for p in original.vertices) - .00002)
                    self.assertLessEqual(point[4], max(p[4] for p in original.vertices) + .00002)

    def test_registered_reflector_and_radiator_have_real_recess_depth(self):
        original = self.original[2][39]
        lens = geometry.subdivide_lens(original, 6, .0018)
        meshes = list(self.original[2])
        meshes[39] = lens
        before = geometry.uv_vertex(lens, (.115, -.47))
        dark_frames = sum(struct.unpack_from('<h', f, 8)[0] < 0 for f in lens.faces)
        region = {'optics_recess_uvs': {'39': [{'outline': [[.11, .52], [.12, .52], [.12, .54], [.11, .54]], 'depth': .012}]}}
        with tempfile.TemporaryDirectory(dir=PROJECT / 'build') as directory:
            (Path(directory) / 'metadata.json').write_text(json.dumps(region))
            geometry.detail_optics(meshes, directory)
        after = geometry.uv_vertex(lens, (.115, -.47))
        self.assertAlmostEqual(math.dist(before[:3], after[:3]), .012, delta=.0001)
        self.assertEqual(before[3:5], after[3:5])
        self.assertGreater(dark_frames, 0)
        self.assertEqual(sum(struct.unpack_from('<h', f, 8)[0] < 0 for f in lens.faces), dark_frames,
                         'Reflector pockets must inherit their lens atlas, never add black outline walls')
        self.assertEqual(meshes[40], self.original[2][40])
        # The same primitive cuts only the source's dark lower radiator chart.
        intact, detailed = self.original[2][9], self.generated[2][9]
        front, _ = geometry.surface(intact, (3, -.35, 0), 0, 1)
        recessed, _ = geometry.surface(detailed, (3, -.35, 0), 0, 1)
        self.assertGreater(front[0] - recessed[0], .014)
        self.assertLess(front[0] - recessed[0], .065)

    def test_upper_arch_geometry_retains_lower_widths_and_local_mask_footprints(self):
        originals = self.original[2]
        before = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
        shared = geometry.shared_edge_controls(originals, set(self.definition['curved_meshes']))
        for index in self.definition['curved_meshes']:
            before[index] = geometry.curved_mesh(originals[index], 3, .018, shared)
        after = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in before]
        geometry.smooth_wheel_arches(after, self.definition)
        footprints = geometry.wheel_arch_opaque_uvs(after)
        self.assertGreater(len(footprints), 100)
        self.assertTrue(all(0 <= c <= 1 for t in footprints for p in t for c in p))
        # Face-replacing details such as the radiator run before arch markers.
        # The final generator footprints must still refer only to those bands.
        completed = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in before]
        geometry.detail_body(completed, originals, self.definition)
        final_footprints = geometry.wheel_arch_opaque_uvs(completed)
        self.assertTrue(final_footprints)
        for triangle in final_footprints:
            points = [(u * 256, min(v, 1 - v) * 256) for u, v in triangle]
            self.assertTrue(any(all(cx - radius - 2 <= x <= cx + radius + 2 and 13.99 <= y <= cy + radius + 2 for x, y in points)
                                for cx, cy, radius, *_ in self.definition['wheel_arches']))

        def triangles(meshes, parts, side):
            result = []
            for index in parts:
                mesh = meshes[index]
                for face in mesh.faces:
                    if struct.unpack_from('<h', face, 8)[0] != 1:
                        continue
                    vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                    if min(side * p[2] for p in vertices) >= .55:
                        result.append([(p[3] * 256, ((p[4] + 1) if side > 0 else -p[4]) * 256) for p in vertices])
            return result

        def covers(point, faces):
            for a, b, c in faces:
                denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                if abs(denominator) < 1e-10:
                    continue
                wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / denominator
                wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / denominator
                if min(wa, wb, 1 - wa - wb) >= -.000001:
                    return True
            return False

        for parts, side, arch in (((0, 9), 1, 0), ((4, 9), -1, 0), ((24, 16), 1, 1), ((21, 16), -1, 1)):
            a, b = triangles(before, parts, side), triangles(after, parts, side)
            cx, cy, radius = self.definition['wheel_arches'][arch][:3]
            for y in (3, 6, 10, 13):
                for x in range(math.floor(cx - radius - 2), math.ceil(cx + radius + 2)):
                    self.assertEqual(covers((x + .25, y), a), covers((x + .25, y), b))
            for degrees in range(65, 116, 5):
                angle = math.radians(degrees)
                self.assertFalse(covers((cx + (radius - .25) * math.cos(angle), cy + (radius - .25) * math.sin(angle)), b))
                self.assertTrue(covers((cx + (radius + .25) * math.cos(angle), cy + (radius + .25) * math.sin(angle)), b))

    def test_subdivision_joins_use_one_quantized_edge_across_uv_seams(self):
        originals = self.original[2]
        selected = set(self.definition['curved_meshes'])
        shared = geometry.shared_edge_controls(originals, selected)
        checked, changed = 0, 0
        for index in selected:
            mesh = geometry.curved_mesh(originals[index], 3, self.definition['body_max_curve'], shared)
            positions = {tuple(round(x * 65536) for x in p[:3]) for p in mesh.vertices}
            for face in originals[index].faces:
                if struct.unpack_from('<h', face, 8)[0] not in (-1, 1):
                    continue
                vertices = [originals[index].vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                    key = geometry.edge_key(a, b)
                    p, q = key
                    c, d = shared[key]
                    for t in (1 / 3, 2 / 3):
                        linear = tuple((1 - t) * p[k] + t * q[k] for k in range(3))
                        point = tuple((1 - t) ** 3 * p[k] + 3 * (1 - t) ** 2 * t * c[k] + 3 * (1 - t) * t * t * d[k] + t ** 3 * q[k] for k in range(3))
                        delta = sub(point, linear)
                        length = math.sqrt(dot(delta, delta))
                        point = add(linear, scale(delta, min(1, self.definition['body_max_curve'] / length))) if length else linear
                        self.assertIn(tuple(round(x * 65536) for x in point), positions)
                        self.assertLessEqual(math.dist(point, linear), .018001)
                        checked += 1
                        changed += math.dist(point, linear) > .001
        self.assertGreater(checked, 1000)
        self.assertGreater(changed, 100)


if __name__ == '__main__':
    unittest.main()
