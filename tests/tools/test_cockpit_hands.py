#!/usr/bin/env python3
"""Canonical hand bindings, rounded sleeves and genuine curled-finger gaps."""

from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
from git_game_data import configured_data_root
import cockpit_hand_geometry as geometry
from vehicle_model import add, crc32, cross, decode, dot, scale, sub, unit
from vehicle_textures import texture_records

CARS = ('hond', 'audi', 'vect', 'volv', 'ford', 'niss', 'p406', 'ren', 'flex')
CAR_IDS = dict(zip(CARS, (0, 1, 2, 3, 4, 5, 6, 7, 9)))


def section(data):
    start, end = struct.unpack_from('<II', data, 24)
    return data[start:end]


def z_hits(meshes, x, y):
    """Independent orthographic ray test, used to probe actual finger gaps."""
    hits = []
    for mesh in meshes:
        for face in mesh.faces:
            a, b, c = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            den = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if abs(den) < 1e-12:
                continue
            u = ((x - a[0]) * (c[1] - a[1]) - (y - a[1]) * (c[0] - a[0])) / den
            v = ((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])) / den
            if min(u, v, 1 - u - v) >= -1e-7:
                hits.append(a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]))
    return hits


def visible_thumb_faces(meshes, matrix, eye, wheel_distance):
    """Independent driver-eye visibility test of the exposed thumb triangles.

    Project the generated hand, retaining reciprocal depth for correct
    perspective interpolation. A small screen-space index makes this a ray
    test against actual triangles, including the palm that can hide a thumb.
    """
    triangles, candidates, bins = [], [], {}
    cell = .01
    for mesh_index, mesh in enumerate(meshes):
        points = [tuple(sum(matrix[4 * j + i] * vertex[j] for j in range(3)) + matrix[12 + i]
                        for i in range(3)) for vertex in mesh.vertices]
        thumb_start = len(mesh.faces) - 648 if mesh_index == 1 else len(mesh.faces)
        for face_index, face in enumerate(mesh.faces):
            world = [points[i] for i in struct.unpack_from('<3H', face, 2)]
            projected = [((p[2] - eye[2]) / p[0], (p[1] - eye[1]) / p[0], 1 / p[0]) for p in world]
            a, b, c = projected
            determinant = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if abs(determinant) < 1e-12:
                continue
            index = len(triangles)
            triangles.append((a, b, c, determinant))
            for u in range(math.floor(min(p[0] for p in projected) / cell), math.floor(max(p[0] for p in projected) / cell) + 1):
                for v in range(math.floor(min(p[1] for p in projected) / cell), math.floor(max(p[1] for p in projected) / cell) + 1):
                    bins.setdefault((u, v), []).append(index)
            # Check the visible shaft, excluding its buried root and tucked
            # distal pad. A wrapped tip should be hidden by the rim.
            if mesh_index == 1 and 144 <= face_index - thumb_start < 432:
                candidates.append((tuple(sum(p[i] for p in world) / 3 for i in range(3)),
                                   (face_index - thumb_start) // 24))
    visible, stations = 0, Counter()
    for point, station in candidates:
        u, v = (point[2] - eye[2]) / point[0], (point[1] - eye[1]) / point[0]
        occluded = False
        for index in bins.get((math.floor(u / cell), math.floor(v / cell)), []):
            a, b, c, determinant = triangles[index]
            first = ((u - a[0]) * (c[1] - a[1]) - (v - a[1]) * (c[0] - a[0])) / determinant
            second = ((b[0] - a[0]) * (v - a[1]) - (b[1] - a[1]) * (u - a[0])) / determinant
            if min(first, second, 1 - first - second) >= -1e-7:
                depth = 1 / (a[2] * (1 - first - second) + b[2] * first + c[2] * second)
                if depth < point[0] - .0002:
                    occluded = True
                    break
        if not occluded:
            for i in range(160, 256):
                t = i / 256
                ray = tuple(eye[a] + (point[a] - eye[a]) * t for a in range(3))
                if wheel_distance(ray) < .026:
                    occluded = True
                    break
        visible += not occluded
        if not occluded and station < 24:
            stations[station] += 1
    return visible, stations


class CockpitHandGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources, cls.outputs, cls.metrics, cls.models = {}, {}, {}, {}
        cls.archive_checksums = {}
        for car in CARS:
            path = configured_data_root() / f'data/{car}dasx.bfp'
            archive = path.read_bytes()
            cls.archive_checksums[path] = crc32(archive)
            table_size = struct.unpack_from('<I', archive, 4)[0]
            for role in (0, 1):
                _, offset, size = struct.unpack_from('<16sII', archive, 12 + role * 24)
                source = archive[12 + table_size + offset:12 + table_size + offset + size]
                key = car, role
                cls.sources[key] = source
                cls.outputs[key], cls.metrics[key] = geometry.generate(source, role, car_id=CAR_IDS[car])
                if car in ('hond', 'audi', 'flex'):
                    cls.models[key] = decode(cls.outputs[key])

    def test_all_nine_cars_keep_exact_hierarchy_pivots_slots_and_original_materials(self):
        for key, output in self.outputs.items():
            source = decode(self.sources[key])
            generated = decode(output)
            self.assertEqual(generated[0][:20], source[0][:20], key)
            self.assertEqual(generated[1], source[1], key)
            self.assertEqual(generated[3], source[3], key)
            self.assertEqual([m.header[:12] for m in generated[2]], [m.header[:12] for m in source[2]], key)
            self.assertTrue(all(m.faces for m in generated[2]), key)
            self.assertEqual([n for n, _, _ in texture_records(generated[3], 4)],
                             ['ARM', 'GLOVE03', 'GLOVE01', 'GLOVE02'])
            self.assertEqual(self.metrics[key]['original_visible_triangles'], 138)
            self.assertEqual(geometry.generate(self.sources[key], key[1], car_id=CAR_IDS[key[0]])[0], output)
        for path, checksum in self.archive_checksums.items():
            self.assertEqual(crc32(path.read_bytes()), checksum)

    def test_proven_source_reuse_has_explicit_runtime_grip_groups_and_original_audi_forearm(self):
        for role in (0, 1):
            reference = section(self.outputs['hond', role])
            group = set(CARS) - {'audi'} if role == 0 else {'hond', 'volv', 'niss', 'p406'}
            for car in group:
                self.assertEqual(section(self.outputs[car, role]), reference)
            if role == 1:
                for car in ('ford', 'ren'):
                    self.assertEqual(section(self.outputs[car, role]), section(self.outputs['vect', role]))
                self.assertNotEqual(section(self.outputs['vect', role]), reference)
                self.assertNotEqual(section(self.outputs['flex', role]), reference)
            self.assertNotEqual(section(self.outputs['audi', role]), reference)
            honda = self.models['hond', role][2]
            audi = self.models['audi', role][2]
            for mesh_index, (original, exception) in enumerate(zip(honda, audi)):
                self.assertEqual(len(original.vertices), len(exception.vertices))
                thumb_vertices = ({i for face in original.faces[-648:] for i in struct.unpack_from('<3H', face, 2)}
                                  if mesh_index == 1 else set())
                for vertex_index, (a, b) in enumerate(zip(original.vertices, exception.vertices)):
                    if role == 0 and vertex_index in thumb_vertices:
                        continue  # Only the authored Audi thumb closes its source offset.
                    if role == 1 and a[0] >= -.17:
                        continue  # Distal geometry fits each real right-hand basis.
                    self.assertAlmostEqual(a[0] - b[0], .01, delta=2 / 65536)
                    for axis in (1, 2):
                        self.assertAlmostEqual(a[axis], b[axis], delta=3 / 65536)
                    # Audi's independently quantized cage retains its own UV
                    # projections. Its fitted taper has the same outward basis.
                    self.assertGreater(dot(a[5:], b[5:]), .9999)

    def test_every_quantized_triangle_has_area_outward_normals_and_source_chart_uvs(self):
        for key, (_, _, meshes, _) in self.models.items():
            source = decode(self.sources[key])[2]
            definition = json.loads(geometry.DEFINITION.read_text())
            for index, (original, mesh) in enumerate(zip(source, meshes)):
                slots = {struct.unpack_from('<h', face, 8)[0] for face in original.faces}
                lo = [min(v[a] for v in original.vertices) for a in (3, 4)]
                hi = [max(v[a] for v in original.vertices) for a in (3, 4)]
                if index == 2:
                    lo = [definition['little_tip_chart'][a][0] for a in ('u', 'v')]
                    hi = [definition['little_tip_chart'][a][1] for a in ('u', 'v')]
                for vertex in mesh.vertices:
                    self.assertTrue(all(math.isfinite(v) for v in vertex))
                    self.assertAlmostEqual(dot(vertex[5:], vertex[5:]), 1, delta=.00006)
                    for axis in (0, 1):
                        self.assertGreaterEqual(vertex[3 + axis], lo[axis] - 1 / 65536)
                        self.assertLessEqual(vertex[3 + axis], hi[axis] + 1 / 65536)
                for face in mesh.faces:
                    indices = struct.unpack_from('<3H', face, 2)
                    self.assertLess(max(indices), len(mesh.vertices))
                    a, b, c = [mesh.vertices[i] for i in indices]
                    normal = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
                    self.assertGreater(dot(normal, normal), 0, key)
                    self.assertGreater(dot(normal, add(add(a[5:], b[5:]), c[5:])), 0, key)
                    self.assertIn(struct.unpack_from('<h', face, 8)[0], slots)

    def test_adjacent_faces_share_continuous_glove_charts_and_back_logo_projection(self):
        for key, (_, _, meshes, _) in self.models.items():
            for index, mesh in enumerate(meshes):
                bindings = {}
                for vertex in mesh.vertices:
                    previous = bindings.setdefault(vertex[:3], vertex[3:5])
                    self.assertEqual(vertex[3:5], previous, (key, index, vertex[:3]))
                if index == 0:
                    continue  # Original left ARM includes a cylindrical seam.
                # A sewn panel has one smooth projected chart. A small edge
                # cannot jump across the atlas or borrow an adjacent panel.
                for face in mesh.faces:
                    vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                    for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                        distance = math.sqrt(dot(sub(a[:3], b[:3]), sub(a[:3], b[:3])))
                        jump = math.sqrt(dot(sub(a[3:5], b[3:5]), sub(a[3:5], b[3:5])))
                        self.assertLessEqual(jump, 25 * distance + 3 / 65536, (key, index))
            original_back = decode(self.sources[key])[2][3]
            projection = geometry._projection(original_back, 17)
            # The original broad logo chart, independent of the tip chart,
            # defines both orientation and location on the new rounded back.
            for vertex in original_back.vertices[:17]:
                for axis, coefficients in enumerate(projection):
                    self.assertAlmostEqual(dot((*vertex[:3], 1.), coefficients), vertex[3 + axis], delta=.04)

    def test_original_open_shoulder_corners_and_sleeve_envelope_survive(self):
        for key, (_, _, meshes, _) in self.models.items():
            original = decode(self.sources[key])[2]
            arm = original[0]
            edges = Counter(tuple(sorted((a, b))) for face in arm.faces
                            for vertices in [[arm.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]]
                            for a, b in zip(vertices, vertices[1:] + vertices[:1]))
            boundary = {point for (a, b), count in edges.items() if count == 1 and max(a[0], b[0]) < -.42
                        for point in (a, b)}
            positions = {v[:3] for mesh in meshes for v in mesh.vertices}
            self.assertEqual(len(boundary), 8)
            self.assertTrue(boundary <= positions, key)
            for axis in range(3):
                old = [v[axis] for m in (original if key[1] == 0 else original[:1]) for v in m.vertices]
                new = [v[axis] for m in meshes for v in m.vertices if key[1] == 0 or v[0] < -.17]
                self.assertGreaterEqual(min(new), min(old) - .012, key)
                self.assertLessEqual(max(new), max(old) + .012, key)

    def test_four_separate_curled_fingers_replace_the_solid_source_mitten(self):
        definition = json.loads(geometry.DEFINITION.read_text())
        for car in ('hond', 'audi'):
            meshes = self.models[car, 0][2]
            source = decode(self.sources[car, 0])[2]
            offset = source[2].vertices[0][0] - definition['frame_anchor'][0]
            for probe, finger in zip((-.030, .005, .015, .012), definition['fingers']):
                # Both front and back of each curved solid are present.
                hits = z_hits(meshes, probe + offset, finger['y'])
                self.assertTrue(hits)
                self.assertGreater(max(hits) - min(hits), .003)
            for probe, (a, b) in zip((-.005, .008, .010), zip(definition['fingers'], definition['fingers'][1:])):
                y = (a['y'] + b['y']) / 2
                self.assertTrue(any(z > .02 for z in z_hits(source, probe + offset, y)))
                self.assertFalse(any(z > .02 for z in z_hits(meshes, probe + offset, y)), (car, y))
            stats = self.metrics[car, 0]
            self.assertEqual(stats['fingers'], 4)
            self.assertEqual(stats['thumbs'], 1)
            self.assertTrue(all(stats['parts'][f'finger-{i}'] > 0 for i in range(1, 5)))

    def test_distinct_anatomical_lengths_knuckle_arc_and_tapered_pads(self):
        definition = json.loads(geometry.DEFINITION.read_text())
        lengths = {}
        for finger in definition['fingers']:
            path = [(x, finger['y'] * spread + drift, z) for (x, drift, z), spread in
                    zip(finger['path'], definition['finger_spread'])]
            samples = [geometry._catmull(path, i * (len(path) - 1) / 120) for i in range(121)]
            lengths[finger['name']] = sum(math.sqrt(dot(sub(b, a), sub(b, a))) for a, b in zip(samples, samples[1:]))
            self.assertLess(finger['radii'][-1], finger['radii'][1] * .7)
            self.assertLess(finger['path'][-1][1], -.01)  # real 3D curl, not parallel identical rows
        self.assertGreater(lengths['middle'], lengths['ring'])
        self.assertGreater(lengths['ring'], lengths['index'])
        self.assertLess(lengths['little'], lengths['index'] * .90)
        knuckles = [f['path'][1][0] for f in definition['fingers']]
        self.assertEqual(len(set(knuckles)), 4)
        self.assertLess(knuckles[0], min(knuckles[1:]) - .01)

    def test_exposed_finger_and_thumb_bends_have_room_for_the_full_padding_radius(self):
        definition = json.loads(geometry.DEFINITION.read_text())
        # Include both sides of each Catmull station: it is C1, so curvature
        # can jump there. The first half-station is buried in the solid palm.
        digits = []
        for finger in definition['fingers']:
            path = [(x, finger['y'] * spread + drift, z) for (x, drift, z), spread in
                    zip(finger['path'], definition['finger_spread'])]
            digits.append((finger['name'], path, finger['radii']))
        digits.append(('thumb', definition['thumb_path'], definition['thumb_radii']))
        audi_anchor = decode(self.sources['audi', 0])[2][2].vertices[0][:3]
        digits.append(('Audi thumb', geometry.thumb_path_for_source(0, audi_anchor, definition), definition['thumb_radii']))
        for name, path, radii in digits:
            samples = [i / 100 for i in range(50, (len(path) - 1) * 100 - 4)]
            samples += [i + side * .0002 for i in range(1, len(path) - 1) for side in (-1, 1)]
            for t in samples:
                step = .0001
                a, b, c = [geometry._catmull(path, q) for q in (t - step, t, t + step)]
                velocity = scale(sub(c, a), .5 / step)
                acceleration = tuple((c[i] - 2 * b[i] + a[i]) / (step * step) for i in range(3))
                perpendicular = cross(velocity, acceleration)
                curvature = math.sqrt(dot(perpendicular, perpendicular)) / dot(velocity, velocity) ** 1.5
                radius = geometry._catmull([(r,) for r in radii], t)[0]
                self.assertLess(curvature * radius, 1 / 1.02,
                                (name, t, 'inner sweep must not fold into itself'))

    def test_thumb_is_compact_without_the_former_long_handle_arch(self):
        definition = json.loads(geometry.DEFINITION.read_text())
        path = definition['thumb_path']
        samples = [geometry._catmull(path, i * (len(path) - 1) / 200) for i in range(201)]
        length = sum(math.sqrt(dot(sub(b, a), sub(b, a))) for a, b in zip(samples, samples[1:]))
        # Keep a short soft taper, rather than the former long handle or the
        # pinched shaft followed by a crosswise bulb. The paintover defines
        # the outline; a prescribed wheel-section angle does not.
        self.assertGreater(length, .035)
        self.assertLess(length, .120)
        self.assertLess(max(p[1] for p in samples), .050)
        self.assertTrue(all(b <= a for a, b in zip(definition['thumb_radii'], definition['thumb_radii'][1:])))
        self.assertLess(definition['thumb_radii'][-1], definition['thumb_radii'][1] * .6)

    def test_audi_thumb_source_fit_keeps_the_root_and_closes_the_distal_offset(self):
        definition = json.loads(geometry.DEFINITION.read_text())
        anchor = decode(self.sources['audi', 0])[2][2].vertices[0][:3]
        fitted = geometry.thumb_path_for_source(0, anchor, definition)
        original = definition['thumb_path']
        self.assertEqual(fitted[0], tuple(original[0]))
        offset_x = anchor[0] - definition['frame_anchor'][0]
        self.assertLess(offset_x, -.009)
        for i, (before, after) in enumerate(zip(original, fitted)):
            self.assertEqual(after[1:], tuple(before[1:]))
            if i >= 2:
                self.assertAlmostEqual(after[0] + offset_x, before[0])
        self.assertEqual(geometry.thumb_path_for_source(1, anchor, definition), [tuple(p) for p in original])

    def test_little_pad_samples_existing_cloth_instead_of_black_atlas_void(self):
        from PIL import Image
        names = dict(zip(CARS, ('honda', 'audi', 'vectra', 'volvo', 'ford', 'nissan', 'peugeot', 'renault', 'flexmobile')))
        for key, data in self.outputs.items():
            mesh = decode(data)[2][2]
            path = PROJECT / f'assets/models/cockpits/hands/{names[key[0]]}/textures/GLOVE01.webp'
            with Image.open(path) as image:
                image = image.convert('RGBA')
                for vertex in mesh.vertices:
                    x, y = round(vertex[3] * (image.width - 1)), round((vertex[4] + 1) * (image.height - 1))
                    pixel = image.getpixel((x, y))
                    self.assertEqual(pixel[3], 255)
                    self.assertNotEqual(pixel[:3], (0, 0, 0), (key, x, y, 'cloth patch must not sample atlas black'))

    def test_terminal_pads_move_down_and_away_and_are_occluded_by_actual_rim(self):
        definition = json.loads(geometry.DEFINITION.read_text())

        def matrix_point(matrix, point):
            return tuple(sum(matrix[4 * j + i] * point[j] for j in range(3)) + matrix[12 + i] for i in range(3))

        def wheel_section(point, pivot_z):
            # Exact tilted circle/chart of canonical WHEEL1, also used by the
            # accepted shared-wheel generator; the depth axis remains bodyX.
            y = point[1] + .049987793 + .0005645751953125
            q = y / .984821365692893
            radius = math.hypot(q, point[2] - pivot_z) - .287
            depth = point[0] - (.799987793 + .0013275146484375 - .176321 * y)
            return radius, depth

        def wheel_distance(point, pivot_z):
            return math.hypot(*wheel_section(point, pivot_z))

        for key, (_, _, meshes, _) in self.models.items():
            if key == ('flex', 0):
                continue  # The original Flex left hand remains parked at the lever.
            original = decode(self.sources[key])[2]
            car_id, role = CAR_IDS[key[0]], key[1]
            group = geometry._fit_group('left-hand' if role == 0 else 'right-hand', car_id)
            matrix = list(definition['grip_fits']['reference' if role == 0 else group]['hand_matrix'])
            pivot_z = -.399993896 if car_id in (0, 3, 5, 6, 9) else .399993896
            if role == 0 and car_id == 1:
                matrix[14] += .799987792
            eye = (0., .4, pivot_z)
            anchor = original[2].vertices[0][:3]
            sign = 1 if role == 0 else -1
            offset = sub(anchor, (definition['frame_anchor'][0], definition['frame_anchor'][1], sign * definition['frame_anchor'][2]))

            def source_point(point):
                fitted = geometry.fit_point(point, role, car_id, anchor, definition)
                return add((fitted[0], fitted[1], sign * fitted[2]), offset)

            positions = {v[:3] for m in meshes for v in m.vertices}
            for finger in definition['fingers']:
                path = [(x, finger['y'] * spread + drift, z) for (x, drift, z), spread in
                        zip(finger['path'], definition['finger_spread'])]
                tangent = unit(sub(geometry._catmull(path, len(path) - 1), geometry._catmull(path, len(path) - 1 - .01)))
                pole = add(path[-1], scale(tangent, finger['radii'][-1] * definition['fingertip_length_scale']))
                pole_source = source_point(pole)
                self.assertLess(min(math.sqrt(dot(sub(v, pole_source), sub(v, pole_source))) for v in positions), .0001)
                tip, joint = matrix_point(matrix, pole_source), matrix_point(matrix, source_point(path[-2]))
                self.assertGreater(tip[0], joint[0], (key, finger['name'], 'terminal must curl away'))
                self.assertLess(tip[1], joint[1], (key, finger['name'], 'native +Y is up'))
                self.assertGreater(1 - (tip[1] - eye[1]) / tip[0], 1 - (joint[1] - eye[1]) / joint[0])
                samples = [tuple(eye[a] + (tip[a] - eye[a]) * i / 255 for a in range(3)) for i in range(256)]
                self.assertLess(min(wheel_distance(p, pivot_z) for p in samples), .026,
                                (key, finger['name'], 'actual rim must hide rounded tip'))
                self.assertLess(abs(wheel_distance(tip, pivot_z) - .026), .025,
                                (key, finger['name'], 'tip pad must remain near rim surface'))
            thumb = geometry.thumb_path_for_source(role, anchor, definition)
            tangent = unit(sub(geometry._catmull(thumb, len(thumb) - 1),
                               geometry._catmull(thumb, len(thumb) - 1 - .01)))
            pole = add(thumb[-1], scale(tangent, definition['thumb_radii'][-1] * definition['fingertip_length_scale']))
            pole_source = source_point(pole)
            self.assertLess(min(math.sqrt(dot(sub(v, pole_source), sub(v, pole_source))) for v in positions), .0001)
            # Keep the approved visible shaft while tucking the distal pad
            # around the far side. Do not prescribe an angular hook: that
            # forced the rejected elbow/T-shaped outline.
            center = matrix_point(matrix, source_point(thumb[-1]))
            gap = wheel_distance(center, pivot_z) - .026
            padding = definition['thumb_radii'][-1] * 1.5
            self.assertLess(gap, padding + .004, (key, 'thumb tip must remain close to the rim'))
            self.assertGreater(gap, -padding, (key, 'thumb must not disappear into the rim'))
            route = [matrix_point(matrix, source_point(geometry._catmull(thumb, t)))
                     for t in [2 + i * (len(thumb) - 3) / 80 for i in range(81)]]
            self.assertGreater(min(wheel_distance(point, pivot_z) for point in route), .026 - .0001,
                               (key, 'distal centerline must go around the tube rather than through it'))
            near_depth = wheel_section(route[0], pivot_z)[1]
            far_depth = wheel_section(route[-1], pivot_z)[1]
            self.assertLess(near_depth, 0, (key, 'shaft must approach the near side'))
            self.assertGreater(far_depth, 0, (key, 'pad must reach the far side'))
            ray = [tuple(eye[a] + (center[a] - eye[a]) * i / 255 for a in range(3)) for i in range(256)]
            self.assertLess(min(wheel_distance(point, pivot_z) for point in ray[:-1]), .026,
                            (key, 'actual rim must hide the far-side pad'))
            visible, stations = visible_thumb_faces(meshes, matrix, eye, lambda p: wheel_distance(p, pivot_z))
            self.assertGreaterEqual(visible, 12, (key, 'driver must see a distinct fifth digit'))
            self.assertGreaterEqual(sum(count >= 2 for count in stations.values()), 4,
                                    (key, 'show a continuous thumb segment rather than only a pad nub'))
            # The sleeve and palm use one continuous loft, with no independent
            # cuff transform, and rounded surface normals span both old sides.
            cuff = [v for m in meshes for v in m.vertices if -.17 < v[0] < -.13]
            self.assertTrue(any(v[6] > .9 for v in cuff))
            self.assertTrue(any(v[6] < -.9 for v in cuff))

    def test_palm_closes_behind_fingers_and_index_curls_inside_the_grip(self):
        definition = json.loads(geometry.DEFINITION.read_text())
        for key, (_, _, meshes, _) in self.models.items():
            source = decode(self.sources[key])[2]
            offset = source[2].vertices[0][0] - definition['frame_anchor'][0]
            # Retain a thin dorsal web beneath the knuckles, while the palmar
            # bulk of the former localX=-.025 disk stays recessed.
            sign = 1 if key[1] == 0 else -1
            z_offset = source[2].vertices[0][2] - sign * definition['frame_anchor'][2]
            for vertex in meshes[4].vertices:
                if key[1] == 0 and vertex[0] - offset > -.03:
                    self.assertGreater(sign * (vertex[2] - z_offset), .004)
            for mesh in meshes:
                for face in mesh.faces:
                    vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                    spans = [max(v[a] for v in vertices) - min(v[a] for v in vertices) for a in range(3)]
                    if min(v[0] - offset for v in vertices) > -.07:
                        self.assertFalse(spans[0] <= 2 / 65536 and spans[1] > .035 and spans[2] > .015, key)
            if key[1] == 0:
                for a, b in zip(definition['fingers'], definition['fingers'][1:]):
                    # The palm supports the spaces between proximal roots;
                    # visible finger separation starts farther along the curl.
                    hits = z_hits(meshes, -.025 + offset, (a['y'] + b['y']) / 2)
                    self.assertTrue(any(z > .025 for z in hits), (key, 'dorsal web'))
                # The thumb-side index used to reach this outer probe with the
                # same loose curl as the middle fingers; it now wraps inward.
                self.assertFalse(z_hits(meshes, .049 + offset, .032), key)


if __name__ == '__main__':
    unittest.main()
