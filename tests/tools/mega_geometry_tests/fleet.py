"""Canonical scenery geometry checks for fleet."""

import math
import struct

from .environment import MODEL
from .fixtures import SceneryCase


class StaticFleetTests(SceneryCase):
    def test_static_fleet_source_gates_keep_lods_billboard_and_unrelated_models(self):
        import mega_static_fleet as fleet
        gates = {}
        count = 0
        for _, _, meshes, names in self.static_fleet_sources():
            for source in meshes:
                name = source.header[:12].rstrip(b'\0').decode('ascii')
                match = fleet.matches(source, names)
                self.assertEqual(match, name in fleet.CONTRACTS)
                if not match:
                    result, stats = fleet.enhance(source, MODEL.Mesh, names)
                    self.assertEqual(result.encode(), source.encode())
                    self.assertEqual(stats['rebuilt_static_fleet'], 0)
                    continue
                count += 1
                vertex_crc = MODEL.crc32(b''.join(struct.pack('<8i', *(round(x * 65536) for x in v)) for v in source.vertices))
                gates.setdefault(name, set()).add(vertex_crc)
                if name == 'LCAR1_1':
                    result, stats = fleet.enhance(source, MODEL.Mesh, names)
                    self.assertEqual(result.encode(), source.encode())
                    self.assertEqual((len(result.vertices), len(result.faces)), (4, 4))
                    self.assertEqual(stats['fleet_wheels'], 0)
                changed = MODEL.Mesh(source.header, list(source.vertices), list(source.faces))
                changed.vertices[0] = tuple(x + 1 / 65536 if i == 3 else x for i, x in enumerate(changed.vertices[0]))
                self.assertFalse(fleet.matches(changed, names))
                bad_names = ['other' if n == names[struct.unpack_from('<h', source.faces[0], 8)[0]] else n for n in names]
                self.assertFalse(fleet.matches(source, bad_names))
                result, stats = fleet.enhance(source, MODEL.Mesh, names, triangle_budget=0)
                self.assertEqual(result.encode(), source.encode())
                self.assertEqual(stats['added_triangles'], 0)
        self.assertEqual(count, 27)
        self.assertEqual({name: len(values) for name, values in gates.items()},
                         {'LVOLVO_1': 2, 'LAMBULAN_1': 2, 'LFIRETRK_1': 1, 'LRACJEEP_1': 1,
                          'LCAR1_1': 1, 'CR11CVAN_1': 1, 'CR03BUS2_1': 1})

    def test_static_fleet_photo_reliefs_undersides_winding_uv_contacts_and_bounds(self):
        from collections import Counter
        import mega_static_fleet as fleet
        reviewed = set()
        # Deep-check each actual form once, without rebuilding palette/LOD
        # duplicates. The separate gate test covers all27 canonical sources.
        for _, member, meshes, names in self.static_fleet_sources():
            if member != 5:
                continue
            for source in meshes:
                name = source.header[:12].rstrip(b'\0').decode('ascii')
                if name == 'LCAR1_1' or name in reviewed or not fleet.matches(source, names):
                    continue
                reviewed.add(name)
                before = source.encode()
                result, stats, description = fleet._build(source, MODEL.Mesh, names, 2048)
                self.assertEqual(stats['rebuilt_static_fleet'], 1)
                self.assertEqual(stats['fleet_wheels'], 2 if name == 'CR11CVAN_1' else 4)
                self.assertEqual(source.encode(), before)
                self.assertEqual(result.header, source.header)
                self.assertEqual(result.vertices[:len(source.vertices)], source.vertices)
                accumulated = Counter()
                accumulated.update(stats)
                accumulated.update(stats)
                self.assertEqual(accumulated['rebuilt_static_fleet'], 2)
                self.assert_valid_added_faces(MODEL.Mesh(source.header, source.vertices, []), result)
                cleanup = description['fragment_cleanup']
                self.assertTrue(all('fallback_source_face' not in c for c in cleanup))
                if name == 'LFIRETRK_1':
                    prebounded = {fi for c in cleanup for fi in c['prebounded_source_faces']}
                    self.assertTrue({22, 25} <= prebounded)
                rotate = lambda v: (-v[1], v[0], v[2], v[3], v[4], -v[6], v[5], v[7])
                canonical = MODEL.Mesh(source.header, [rotate(v) for v in source.vertices], source.faces) if name != 'LVOLVO_1' else source
                dark = description['dark_sampler']
                dark_faces = [i for faces in description['underside_faces'].values() for i in faces]
                closed = [faces for group, faces in description['body_components'].items() if group != 'chassis']
                if description['separate_floor']:
                    closed.append(description['separate_floor']['faces'])
                    if name == 'LRACJEEP_1':
                        # Independent native-regression check: the new inner
                        # floor must stay behind BOTH original sloping front
                        # triangles, rather than the global ground-level AABB.
                        for source_face in (34, 35):
                            a, b, c = [source.vertices[i][:3] for i in struct.unpack_from('<3H', source.faces[source_face], 2)]
                            normal = MODEL.unit(MODEL.cross(MODEL.sub(b, a), MODEL.sub(c, a)))
                            for fi in description['separate_floor']['faces']:
                                for vi in struct.unpack_from('<3H', result.faces[fi], 2):
                                    self.assertLess(MODEL.dot(normal, MODEL.sub(result.vertices[vi][:3], a)), -1e-7)
                for faces in closed:
                    edges = Counter()
                    for fi in faces:
                        points = [tuple(round(c * 65536) for c in result.vertices[i][:3]) for i in struct.unpack_from('<3H', result.faces[fi], 2)]
                        edges.update(tuple(sorted((a, b))) for a, b in zip(points, points[1:] + points[:1]))
                    self.assertTrue(edges and set(edges.values()) == {2}, name)
                for wheel in description['wheels']:
                    dark_faces.extend(wheel['ring_faces'] + wheel['tread_faces'])
                    self.assertAlmostEqual(wheel['outward_relief'], .03)
                    # Source photograph landmarks, independently transcribed
                    # from the registered originals (not the enhancer table).
                    second = wheel['name'].endswith('-1')
                    if name == 'LVOLVO_1':
                        x, y, rx, ry, width, height = (15.5, 57.5, 8.5, 8.5, 32, 64) if wheel['name'].startswith('cab') else (47.5, 28.5, 9.5, 4.5, 128, 32)
                    elif name == 'LAMBULAN_1':
                        x, y, rx, ry, width, height = (54.5 if second else 8.5), 28, 5.5, 5.5, 64, 32
                    elif name == 'LFIRETRK_1':
                        x, y, rx, ry, width, height = (56.5 if second else 18), 25, 7.5, 7.5, 64, 32
                    elif name == 'LRACJEEP_1':
                        x, y, rx, ry, width, height = (56.5 if second else 17), 25, 7.5, 7.5, 64, 32
                    elif name == 'CR11CVAN_1':
                        x, y, rx, ry, width, height = (30.5 if wheel['name'].startswith('side-a') else 34.5), 29.5, 5, 3.5, 64, 32
                        self.assertEqual(wheel['source_hub_pixel'], (x, 28))
                        self.assertEqual(wheel['vertical_radius_mode'], 'original-ground-contact')
                    else:
                        x, y, rx, ry, width, height = (101 if second else 29.5), 57, 7.5, 7.5, 128, 64
                    cx, cy, cz = wheel['center']
                    self.assert_relief_source_uv(canonical, wheel['source_faces'],
                                                 (cx + wheel['side'] * .03, cy, cz, x / width, -1 + y / height), wheel['side'])
                    points = []
                    for fi in wheel['cap_faces']:
                        source_slots = {struct.unpack_from('<h', source.faces[i], 8)[0] for i in wheel['source_faces']}
                        self.assertIn(struct.unpack_from('<h', result.faces[fi], 8)[0], source_slots)
                        cap_vertices = []
                        for vi in struct.unpack_from('<3H', result.faces[fi], 2):
                            vertex = rotate(result.vertices[vi]) if name != 'LVOLVO_1' else result.vertices[vi]
                            cap_vertices.append(vertex)
                            self.assertGreater(vertex[5] * wheel['side'], .999)
                            self.assert_relief_source_uv(canonical, wheel['source_faces'], vertex, wheel['side'])
                            # Caravan's lower wheel patch has a different U
                            # phase from its upper body (the15/29 seam). Keep
                            # the original tyre texel columns, rather than
                            # assuming one affine UV radius across both crops.
                            left = math.floor(x - rx) if name == 'CR11CVAN_1' else x - rx
                            right = math.ceil(x + rx) if name == 'CR11CVAN_1' else x + rx
                            self.assertGreaterEqual(vertex[3], left / width - 1e-8)
                            self.assertLessEqual(vertex[3], right / width + 1e-8)
                            self.assertGreaterEqual(vertex[4], -1 + (y - ry) / height - 1e-8)
                            self.assertLessEqual(vertex[4], 1e-8 if name == 'CR11CVAN_1' else -1 + (y + ry) / height + 1e-8)
                        # A triangle cannot mix the two valid seam phases at
                        # its corners: its interior must also match the source.
                        centroid = tuple(sum(v[i] for v in cap_vertices) / 3 for i in range(8))
                        self.assert_relief_source_uv(canonical, wheel['source_faces'], centroid, wheel['side'])
                    for fi in wheel['cap_faces'] + wheel['ring_faces'] + wheel['tread_faces']:
                        points.extend(rotate(result.vertices[i]) if name != 'LVOLVO_1' else result.vertices[i]
                                      for i in struct.unpack_from('<3H', result.faces[fi], 2))
                    boundary = Counter()
                    for fi in wheel['source_faces']:
                        corners = [canonical.vertices[i][:3] for i in struct.unpack_from('<3H', source.faces[fi], 2)]
                        boundary.update(tuple(sorted((a, b))) for a, b in zip(corners, corners[1:] + corners[:1]))
                    a, b = max((edge for edge, count in boundary.items() if count == 1 and abs(edge[0][1] - edge[1][1]) > 1e-8),
                               key=lambda edge: edge[0][2] + edge[1][2])
                    deviations = [v[2] - (a[2] + (b[2] - a[2]) * (v[1] - a[1]) / (b[1] - a[1])) for v in points]
                    self.assertLessEqual(max(deviations), 1e-7)
                    self.assertTrue(any(abs(d) < 1e-7 for d in deviations), name)
                for fi in dark_faces:
                    self.assertEqual(struct.unpack_from('<h', result.faces[fi], 8)[0], dark['slot'])
                    for vi in struct.unpack_from('<3H', result.faces[fi], 2):
                        self.assertEqual(result.vertices[vi][3:5], dark['uv'])
                for axis in range(3):
                    lower = min(v[axis] for v in result.vertices) - min(v[axis] for v in source.vertices)
                    upper = max(v[axis] for v in result.vertices) - max(v[axis] for v in source.vertices)
                    if axis == (0 if name == 'LVOLVO_1' else 1):
                        self.assertGreaterEqual(lower, -.030001)
                        self.assertLessEqual(upper, .030001)
                    else:
                        self.assertAlmostEqual(lower, 0.)
                        self.assertAlmostEqual(upper, 0.)
        self.assertEqual(reviewed, {'LVOLVO_1', 'LAMBULAN_1', 'LFIRETRK_1', 'LRACJEEP_1', 'CR11CVAN_1', 'CR03BUS2_1'})
