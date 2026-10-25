#!/usr/bin/env python3
"""Volvo source bindings, fixed-point topology and actual wheel-sweep clearance."""

import collections
import json
import math
from pathlib import Path
import runpy
import struct
import shutil
import subprocess
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import volvo_geometry as geometry
import vehicle_wheel_stance as stance
import vehicle_wheel_wells as wells
from git_game_data import configured_data_root
from vehicle_model import Mesh, add, crc32, cross, decode, dot, pack, read_source, sub


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


def final_frame_report(source, definition, directory):
    """Sample decoded final WebP on the actual quantized painted triangles."""
    import vehicle_textures
    directory = Path(directory)
    converter = shutil.which('magick') or shutil.which('convert')
    if not converter:
        raise unittest.SkipTest('ImageMagick required for final WebP sampling')
    texture = directory / 'S40.webp'
    width, height = vehicle_textures.webp_dimensions(texture.read_bytes())
    rgba = subprocess.run([converter, str(texture), '-depth', '8', 'rgba:-'],
                          check=True, stdout=subprocess.PIPE).stdout
    originals = decode(source)[2]
    before = decode(pack(*geometry.prepare(source, definition)))[2]
    after = decode(geometry.generate(source, definition, directory)[0])[2]

    def color(uv):
        px, py = uv[0] * width - .5, (uv[1] + 1) * height - .5
        x, y = math.floor(px), math.floor(py)
        fx, fy = px - x, py - y
        return tuple(sum(rgba[(max(0, min(height - 1, y + dy)) * width + max(0, min(width - 1, x + dx))) * 4 + channel] * weight
                         for dx, dy, weight in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                                                (0, 1, (1 - fx) * fy), (1, 1, fx * fy)))
                     for channel in range(4))

    panel_cache = {}
    def panel_uv(mesh, point, chart=None):
        key = id(mesh), chart
        best = None
        if key not in panel_cache:
            panel_cache[key] = []
            for face in mesh.faces:
                if struct.unpack_from('<h', face, 8)[0] != 0:
                    continue
                triangle = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                if chart == 'bumper-front' and any(v[3] > .15 for v in triangle):
                    continue
                if chart == 'bumper-side' and any(v[3] < .85 for v in triangle):
                    continue
                if chart == 'rear-side' and any(v[3] > .05 for v in triangle):
                    continue
                if chart == 'rear-main' and any(v[3] < .5 for v in triangle):
                    continue
                a, b, c = triangle
                ab, ac = sub(b[:3], a[:3]), sub(c[:3], a[:3])
                aa, cc, bc = dot(ab, ab), dot(ac, ac), dot(ab, ac)
                denominator = aa * cc - bc * bc
                if denominator >= 1e-18:
                    panel_cache[key].append((triangle, ab, ac, aa, cc, bc, denominator))
        for triangle, ab, ac, aa, cc, bc, denominator in panel_cache[key]:
            ap = sub(point, triangle[0][:3])
            wb = (cc * dot(ap, ab) - bc * dot(ap, ac)) / denominator
            wc = (aa * dot(ap, ac) - bc * dot(ap, ab)) / denominator
            weights = 1 - wb - wc, wb, wc
            # Clamp only the query to the physical triangle. Quantized split
            # edges can lie a few micrometres away from the analytic source
            # line; the strict four-fixed-unit distance check below still
            # rejects points outside the actual surface.
            if min(weights) < 0:
                weights = tuple(max(0., w) for w in weights)
                total = sum(weights)
                weights = tuple(w / total for w in weights)
            projected = tuple(sum(w * v[k] for w, v in zip(weights, triangle)) for k in range(3))
            uv = tuple(sum(w * v[k] for w, v in zip(weights, triangle)) for k in (3, 4))
            candidate = math.dist(point, projected), -min(weights), uv
            if best is None or candidate < best:
                best = candidate
        if best is None or best[0] > 4 / 65536:
            raise AssertionError(f'Fold point {point} escaped its actual {chart} panel: {best}')
        return best[2]

    def measure(meshes, a, b, slots, charts, region=None, path=None):
        path = path or [a, b]
        spans = [math.dist(p, q) for p, q in zip(path, path[1:])]
        length = sum(spans)
        dark = [[], []]
        for step in range(401):
            t = step / 400
            if region is not None and not region[0] <= t <= region[1]:
                continue
            distance = length * t
            for p, q, span in zip(path, path[1:], spans):
                if distance <= span + 1e-10:
                    fraction = distance / span
                    point = tuple(x * (1 - fraction) + y * fraction for x, y in zip(p, q))
                    break
                distance -= span
            for side, (slot, chart) in enumerate(zip(slots, charts)):
                rgba = color(panel_uv(meshes[slot], point, chart))
                if rgba[3] < 254.9:
                    raise AssertionError(f'Fold must remain opaque: {slot}, {chart}, {point}')
                if max(rgba[:3]) < 180 and (region is None or region[0] <= t <= region[1]):
                    dark[side].append(t)
        if not all(dark):
            raise AssertionError(f'Missing dark outline: {slots}, {charts}')
        connected = []
        for values in dark:
            if values[0] > 0:
                connected.append(None)
                continue
            end = values[0]
            for value in values[1:]:
                if value - end > 1.5 / 400:
                    break
                end = value
            connected.append(end)
        return {'length_m': length, 'first_dark_fraction': [min(v) for v in dark],
                'last_dark_fraction': [max(v) for v in dark],
                'initial_dark_end_fraction': connected,
                'first_mismatch_m': abs(min(dark[0]) - min(dark[1])) * length,
                'last_mismatch_m': abs(max(dark[0]) - max(dark[1])) * length,
                'initial_dark_end_mismatch_m': (abs(connected[0] - connected[1]) * length
                                                if all(v is not None for v in connected) else None)}

    report = {'texture_crc32': crc32(texture.read_bytes()), 'sample_intervals': 400, 'front': {}, 'rear': {}}
    for name, side, lamp in (('positive_z', 0, 45), ('negative_z', 4, 47)):
        shared = {v[:3] for v in originals[side].vertices} & {v[:3] for v in originals[lamp].vertices}
        if len(shared) != 2:
            raise AssertionError('Front source fold must contain two corners')
        a, b = sorted(shared, key=lambda p: p[1], reverse=True)
        record = {label: measure(meshes, a, b, (side, lamp), (None, None))
                  for label, meshes in (('before', before), ('after', after))}
        # Continue below the lamp through the bumper's two physical copies.
        corners = (14, 15) if side == 0 else (18, 20)
        a, b = [originals[9].vertices[i][:3] for i in corners]
        record['bumper'] = {}
        for label, meshes in (('before', before), ('after', after)):
            positions = [set(), set()]
            mesh = meshes[9]
            for face in mesh.faces:
                if struct.unpack_from('<h', face, 8)[0] != 0:
                    continue
                triangle = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                if all(p[3] > .85 for p in triangle):
                    positions[0].update(p[:3] for p in triangle)
                if all(p[3] < .15 for p in triangle):
                    positions[1].update(p[:3] for p in triangle)
            direction = sub(b, a)
            squared = dot(direction, direction)
            def along(point):
                fraction = dot(sub(point, a), direction) / squared
                projected = tuple(x + fraction * d for x, d in zip(a, direction))
                return fraction, math.dist(point, projected)
            path = sorted((p for p in positions[0] & positions[1]
                           if -.0001 <= along(p)[0] <= 1.0001 and along(p)[1] < .015),
                          key=lambda p: along(p)[0])
            if len(path) < 3 or math.dist(path[0], a) > .0001 or math.dist(path[-1], b) > .0001:
                raise AssertionError('Bumper fold must follow the actual shared curved edge')
            record['bumper'][label] = measure(meshes, a, b, (9, 9), ('bumper-side', 'bumper-front'), path=path)
        report['front'][name] = record
    for name, lamp, a, b in (('positive_z', 16, 6, 5), ('negative_z', 18, 6, 4)):
        a, b = originals[lamp].vertices[a][:3], originals[lamp].vertices[b][:3]
        report['rear'][name] = {label: measure(meshes, a, b, (lamp, lamp), ('rear-side', 'rear-main'), (.5, 1))
                               for label, meshes in (('before', before), ('after', after))}
    return report


class VolvoGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data_root = configured_data_root()
        inventory = runpy.run_path(str(PROJECT / 'tools/inventory-vehicles.py'))['inventory'](cls.data_root)
        cls.entry = next(v for v in inventory['vehicles'] if v['key'] == 'volvo')
        cls.definition = json.loads((PROJECT / 'assets/models/vehicles/volvo/definition.json').read_text())
        cls.source = read_source(cls.entry['models']['high']['source'], cls.data_root)
        cls.original = decode(cls.source)
        cls.model, cls.stats = geometry.generate(cls.source, cls.definition)
        cls.generated = decode(cls.model)
        cls.adjusted, cls.stance = stance.apply(cls.model, cls.definition)
        cls.lined, cls.well_stats = wells.apply(cls.adjusted, cls.definition)

    def test_exact_source_slots_hierarchy_and_damage_bindings(self):
        self.assertEqual(crc32(self.source), 'c577ebac')
        self.assertEqual(struct.unpack_from('<3H', self.generated[0], 12), (25, 50, 10))
        self.assertEqual(self.generated[1], self.original[1])
        self.assertEqual(self.generated[3], self.original[3])
        self.assertEqual([m.header[:12] for m in self.generated[2]], [m.header[:12] for m in self.original[2]])
        for level, member in (('medium', 14), ('low', 15)):
            source = self.entry['models'][level]['source']
            self.assertEqual(source['member'], member)
            self.assertEqual(crc32(read_source(source, self.data_root)), source['member_crc32'])
        self.assertEqual(self.model, geometry.generate(self.source, self.definition)[0])
        with self.assertRaisesRegex(ValueError, 'canonical HIGH member13'):
            geometry.generate(self.source, dict(self.definition, source_model_crc32='943e9422'))
        for index in (3, 7, 8, 12, 15, 17, 19, 26, 42, 44, 46, 48):
            self.assertEqual(self.generated[2][index].encode(), self.original[2][index].encode(), index)
        for healthy, damaged in ((0, 2), (4, 6), (9, 11), (13, 14), (20, 22), (24, 25), (27, 29), (30, 32), (36, 37)):
            self.assertNotEqual(self.generated[2][healthy].vertices, self.generated[2][damaged].vertices)
        self.assertEqual(self.stats['original_visible_triangles'], 322)
        self.assertLess(self.stats['visible_triangles'], 16000)

    def test_quantized_faces_are_finite_outward_and_nondegenerate(self):
        for index, mesh in enumerate(decode(self.lined)[2]):
            self.assertLess(max(len(mesh.vertices), len(mesh.faces)), 65536)
            self.assertTrue(all(math.isfinite(v) for p in mesh.vertices for v in p), index)
            for face in mesh.faces:
                a, b, c = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                n = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
                self.assertGreater(dot(n, n), 1e-16, index)
                self.assertGreater(dot(n, add(add(a[5:], b[5:]), c[5:])), 0, index)

    def test_fifteen_spoke_wheel_uses_original_radius_width_and_pivots(self):
        mesh = self.generated[2][49]
        self.assertGreaterEqual(self.definition['wheel_segments'], 48)
        self.assertEqual(self.definition['wheel_spokes'], 15)
        self.assertEqual(sum(tuple(f[10:14]) == geometry.BRIGHT for f in mesh.faces), 15 * 2 * 2)
        self.assertTrue(all(struct.unpack_from('<h', f, 8)[0] == -1 for f in mesh.faces))
        for row, (z, radius) in enumerate(self.definition['wheel_profile']):
            for p in mesh.vertices[row * 48:(row + 1) * 48]:
                self.assertAlmostEqual(p[2], z, delta=.00002)
                self.assertAlmostEqual(math.hypot(p[0], p[1]), radius, delta=.00002)
        for old, new in zip(wells.node_records(self.original[1]), wells.node_records(decode(self.adjusted)[1])):
            self.assertEqual(old['id'], new['id'])
            self.assertEqual(old['variants'], new['variants'])
            if old['id'] in (1, 2, 3, 4):
                self.assertEqual(old['origin'][:2], new['origin'][:2])
            else:
                self.assertEqual(old['origin'], new['origin'])

    @staticmethod
    def edge_counts(mesh):
        edges = collections.Counter()
        for face in mesh.faces:
            vertices = [tuple(round(v * 65536) for v in mesh.vertices[i][:3]) for i in struct.unpack_from('<3H', face, 2)]
            for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                edges[tuple(sorted((a, b)))] += 1
        return edges

    def test_mirrors_and_actual_wing_are_closed(self):
        for index in (41, 43):
            mesh = self.generated[2][index]
            self.assertTrue(all(count == 2 for count in self.edge_counts(mesh).values()), index)
            self.assertTrue(any(tuple(f[10:14]) == geometry.GLASS for f in mesh.faces))
        for index in (24, 25):
            mesh = Mesh(self.original[2][index].header, [], [])
            geometry.solid_wing(mesh, self.original[2][index])
            quantized = decode(pack(self.original[0], self.original[1], [mesh] + self.original[2][1:], self.original[3]))[2][0]
            self.assertTrue(all(count == 2 for count in self.edge_counts(quantized).values()), index)
            self.assertTrue(any(struct.unpack_from('<h', f, 8)[0] == 0 for f in mesh.faces))

    def test_lenses_keep_source_corner_positions_and_uv_charts(self):
        for index in geometry.LAMP_SLOTS:
            original, mesh = self.original[2][index], self.generated[2][index]
            for point in original.vertices:
                self.assertTrue(any(math.dist(p[:3], point[:3]) < .00003 and math.dist(p[3:5], point[3:5]) < .00003 for p in mesh.vertices))
            for axis in (3, 4):
                self.assertAlmostEqual(min(p[axis] for p in mesh.vertices), min(p[axis] for p in original.vertices), delta=.00002)
                self.assertAlmostEqual(max(p[axis] for p in mesh.vertices), max(p[axis] for p in original.vertices), delta=.00002)

    def test_verified_front_fold_repairs_are_local_and_preserve_round_side_lenses(self):
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        before = [(list(m.vertices), list(m.faces)) for m in meshes]
        geometry.register_front_lamp_frames(meshes, self.original[2])
        changed = set()
        for index, mesh in enumerate(meshes):
            vertices, faces = before[index]
            self.assertEqual(mesh.faces, faces)
            self.assertEqual([p[:3] + p[5:] for p in mesh.vertices], [p[:3] + p[5:] for p in vertices])
            if mesh.vertices != vertices:
                changed.add(index)
                self.assertIn(index, (4, 5, 9, 10, 45, 47))
                for old, new in zip(vertices, mesh.vertices):
                    if index in (45, 47):
                        self.assertEqual(old[4], new[4])
                        self.assertLessEqual(abs(new[3] - old[3]), 1 / 256)
                    else:
                        self.assertEqual(old[3], new[3])
                        self.assertLessEqual(abs(new[4] - old[4]), 1 / 256)
        self.assertEqual(changed, {4, 5, 9, 10, 45, 47})
        for lamp, top in ((45, 1), (47, 0)):
            point = self.original[2][lamp].vertices[top]
            corners = [p for p in meshes[lamp].vertices if math.dist(p[:3], point[:3]) < .00001]
            self.assertTrue(corners)
            self.assertTrue(all(abs(p[3] * 256 - 34.7) < .0001 for p in corners))
        # The side fender and its neighboring bumper share one corrected fold.
        for index, original_corner in ((4, 9), (5, 9), (9, 18), (10, 18)):
            old = self.original[2][index].vertices[original_corner]
            corners = [p for p in meshes[index].vertices if math.dist(p[3:5], (old[3], 229.85 / 256 - 1)) < .00001]
            self.assertTrue(corners, index)

    def test_rear_return_registration_preserves_actual_panels_and_upper_clear_band(self):
        _, _, meshes, _ = geometry.prepare(self.source, self.definition)
        before = [(list(m.vertices), list(m.faces)) for m in meshes]
        geometry.register_rear_lamp_returns(meshes)
        selected = {16, 18, 27, 28, 30, 31}
        def area(vertices, faces):
            total = 0.
            for face in faces:
                a, b, c = [vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
                n = cross(sub(b, a), sub(c, a))
                total += math.sqrt(dot(n, n)) / 2
            return total
        for index, mesh in enumerate(meshes):
            vertices, faces = before[index]
            if index not in selected:
                self.assertEqual(mesh.vertices, vertices, index)
                self.assertEqual(mesh.faces, faces, index)
            else:
                self.assertEqual(mesh.vertices[:len(vertices)], vertices, index)
                self.assertAlmostEqual(area(mesh.vertices, mesh.faces), area(vertices, faces), delta=1e-8)
        for index, top, bottom, target, sampled in (
                (16, 48.8046875, 29.640625, .6215, .5915),
                (18, 207.12890625, 226.296875, .6182, .5859)):
            old, mesh = self.original[2][index], meshes[index]
            # The original outer fold goes from its top to its lower corner.
            a, b = old.vertices[6], old.vertices[5 if index == 16 else 4]
            point = tuple(a[k] + target * (b[k] - a[k]) for k in range(3))
            hits = [p for p in mesh.vertices if math.dist(p[:3], point) < .00001]
            self.assertTrue(hits, index)
            self.assertTrue(any(abs((p[4] + 1) * 256 - (top + sampled * (bottom - top))) < .0001 for p in hits), index)

    def test_final_webp_lamp_outlines_join_on_actual_quantized_folds_and_bumper(self):
        report = final_frame_report(self.source, self.definition,
                                    PROJECT / 'assets/models/vehicles/volvo/textures')
        for side, record in report['front'].items():
            with self.subTest(surface='front', side=side):
                self.assertGreater(record['before']['first_mismatch_m'], .007)
                self.assertLess(record['after']['first_mismatch_m'], .003)
                self.assertLess(record['after']['last_mismatch_m'], .003)
                bumper = record['bumper']['after']
                self.assertEqual(bumper['first_dark_fraction'], [0., 0.])
                self.assertIsNotNone(bumper['initial_dark_end_mismatch_m'])
                self.assertLess(bumper['initial_dark_end_mismatch_m'], .003)
                # The source's separate lower indicator remains farther along
                # the side fold. It must not be mistaken for the lamp frame.
                self.assertGreater(bumper['last_dark_fraction'][0], .9)
                self.assertLess(bumper['initial_dark_end_fraction'][0], .1)
        for side, record in report['rear'].items():
            with self.subTest(surface='rear', side=side):
                self.assertGreater(record['before']['last_mismatch_m'], .007)
                self.assertLess(record['after']['last_mismatch_m'], .003)

    def test_actual_liner_and_paint_faces_clear_tire_at_both_maximum_turns(self):
        reports = clearance_reports(decode(self.adjusted), decode(self.lined), self.definition)
        for surface, wheels in reports.items():
            for wheel, states in enumerate(wheels):
                for state, clearance in enumerate(states):
                    with self.subTest(surface=surface, wheel=wheel, steering=state):
                        self.assertTrue(math.isfinite(clearance))
                        self.assertGreater(clearance, .010)

    def test_liner_mouths_trace_all_four_actual_skin_openings_to_sill(self):
        before, after = decode(self.adjusted), decode(self.lined)
        self.assertEqual(self.well_stats['wheel_well_mesh'], 26)
        self.assertEqual(self.well_stats['wheel_well_count'], 4)
        floor, _, centers = wells.anchors(before[1])
        liner_positions = {p[:3] for p in after[2][floor].vertices[len(before[2][floor].vertices):]}
        for center in centers:
            contour = wells.aperture(before[2], before[1], center, self.definition)
            self.assertGreater(len(contour), 40)
            self.assertLess(min(p[1] for p in contour), -.43)
            for point in contour:
                self.assertLess(min(math.dist(p, point) for p in liner_positions), .005)
        for index, (a, b) in enumerate(zip(before[2], after[2])):
            if index == floor:
                self.assertEqual(b.faces[:len(a.faces)], a.faces)
                self.assertEqual(b.vertices[:len(a.vertices)], a.vertices)
            else:
                self.assertEqual(a.encode(), b.encode())


if __name__ == '__main__':
    unittest.main()
