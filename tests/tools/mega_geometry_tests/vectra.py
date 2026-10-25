"""Canonical scenery geometry checks for vectra."""

import struct

from .environment import MODEL, detail
from .fixtures import SceneryCase


class StaticVectraTests(SceneryCase):
    def test_static_vectra_truck_closes_actual_body_and_three_source_photo_wheels(self):
        from collections import Counter
        import mega_static_vectra
        reviewed_uv_gates = set()
        for track, member, meshes, names in self.static_vectra_sources():
            selected = [m for m in meshes if mega_static_vectra.matches(m, names)]
            self.assertEqual(len(selected), 0 if (track, member) == (6, 6) else 1)
            if not selected:
                continue
            source = selected[0]
            gate = MODEL.crc32(b''.join(struct.pack('<8i', *(round(x * 65536) for x in v)) for v in source.vertices))
            if gate in reviewed_uv_gates:
                continue
            reviewed_uv_gates.add(gate)
            original_bytes = source.encode()
            result, stats, description = mega_static_vectra._build(source, MODEL.Mesh, names, 2048)
            if len(reviewed_uv_gates) == 1:
                dispatched, counters = detail.scenery(source, MODEL.Mesh, names, len(names), 'objects')
                self.assertEqual(dispatched.encode(), result.encode())
                self.assertEqual(counters, stats)
            self.assertEqual((stats['vectra_truck_wheels'], stats['closed_vectra_truck_bodies']), (3, 2))
            accumulated = Counter()
            accumulated.update(stats)
            accumulated.update(stats)
            self.assertEqual(accumulated['rebuilt_static_vectra_truck'], 2)
            self.assertEqual(source.encode(), original_bytes)
            self.assertEqual(result.header, source.header)
            self.assertEqual(result.vertices[:len(source.vertices)], source.vertices)
            kept = [i for i in range(len(source.faces)) if i not in description['replaced_source_faces']]
            preserved = {kept.index(i) for i in description['preserved_source_faces']}
            for position in preserved:
                self.assertEqual(result.faces[position], source.faces[kept[position]])
            # Keep the broad plain right side and existing alpha-keyed
            # underbody: no invented right-body wheel or body reproportioning.
            for source_id in (8, 9, *range(29, 43)):
                self.assertIn(source_id, description['preserved_source_faces'])
            generated = MODEL.Mesh(result.header, result.vertices,
                                   [f for i, f in enumerate(result.faces) if i not in preserved])
            self.assert_valid_added_faces(MODEL.Mesh(source.header, source.vertices, []), generated)
            for name, faces in description['body_components'].items():
                edges = Counter()
                for fi in faces:
                    points = [tuple(round(x * 65536) for x in result.vertices[i][:3])
                              for i in struct.unpack_from('<3H', result.faces[fi], 2)]
                    for a, b in zip(points, points[1:] + points[:1]):
                        edges[tuple(sorted((a, b)))] += 1
                self.assertTrue(edges and set(edges.values()) == {2}, name)
            dark_faces = [i for faces in description['underside_faces'].values() for i in faces]
            self.assertEqual({w['name'] for w in description['wheels']}, {'cab-left', 'cab-right', 'body-photo-left'})
            for wheel in description['wheels']:
                dark_faces.extend(wheel['ring_faces'] + wheel['tread_faces'])
                self.assertAlmostEqual(wheel['outward_relief'], .03)
                photo_slot = names.index('LVECTR1' if wheel['body'] == 'cab' else 'LVECTR2')
                all_points = []
                for fi in wheel['cap_faces']:
                    self.assertEqual(struct.unpack_from('<h', result.faces[fi], 8)[0], photo_slot)
                    for vi in struct.unpack_from('<3H', result.faces[fi], 2):
                        vertex = result.vertices[vi]
                        self.assertGreater(vertex[5] * wheel['side'], .999)
                        self.assert_relief_source_uv(source, wheel['source_faces'], vertex, wheel['side'])
                for fi in wheel['cap_faces'] + wheel['ring_faces'] + wheel['tread_faces']:
                    all_points.extend(result.vertices[i] for i in struct.unpack_from('<3H', result.faces[fi], 2))
                if wheel['name'] == 'body-photo-left':
                    # Independent source-pixel regression: the opaque tyre
                    # and hub are small, unlike the wide transparent bay.
                    cx, cy, cz = wheel['center']
                    self.assert_relief_source_uv(source, wheel['source_faces'],
                                                 (cx - .03, cy, cz, 113 / 128, -1 + 28 / 32), -1)
                    self.assertLess(wheel['radius_y'], .51)
                    self.assertLess(wheel['radius_z'], .63)
                    for fi in wheel['cap_faces']:
                        for vi in struct.unpack_from('<3H', result.faces[fi], 2):
                            u, v = result.vertices[vi][3:5]
                            self.assertGreaterEqual(u, 108 / 128 - 1e-8)
                            self.assertLessEqual(u, 118 / 128 + 1e-8)
                            self.assertGreaterEqual(v, -1 + 23 / 32 - 1e-8)
                            self.assertLessEqual(v, 1e-8)
                # Derive the actual source lower edge independently. One cab
                # edge slopes, so its center height is not the maximum contact.
                source_edges = Counter()
                for source_id in wheel['source_faces']:
                    points = [source.vertices[i][:3] for i in struct.unpack_from('<3H', source.faces[source_id], 2)]
                    for a, b in zip(points, points[1:] + points[:1]):
                        source_edges[tuple(sorted((a, b)))] += 1
                a, b = max((edge for edge, count in source_edges.items() if count == 1 and abs(edge[0][1] - edge[1][1]) > 1e-8),
                           key=lambda edge: edge[0][2] + edge[1][2])
                deviations = [v[2] - (a[2] + (b[2] - a[2]) * (v[1] - a[1]) / (b[1] - a[1])) for v in all_points]
                self.assertLessEqual(max(deviations), 1e-7)
                self.assertGreaterEqual(sum(abs(d) < 1e-7 for d in deviations), 2)
            for fi in dark_faces:
                self.assertEqual(struct.unpack_from('<h', result.faces[fi], 8)[0], names.index('LVOLVO4'))
                for vi in struct.unpack_from('<3H', result.faces[fi], 2):
                    self.assertEqual(result.vertices[vi][3:5], (.953125, -.484375))
            for axis in range(3):
                lower = min(v[axis] for v in result.vertices) - min(v[axis] for v in source.vertices)
                upper = max(v[axis] for v in result.vertices) - max(v[axis] for v in source.vertices)
                if axis == 0:
                    self.assertGreaterEqual(lower, -.030001)
                    self.assertLessEqual(upper, .030001)
                else:
                    self.assertAlmostEqual(lower, 0.)
                    self.assertAlmostEqual(upper, 0.)
        self.assertEqual(reviewed_uv_gates, {'64792485', '01ef377d'})

    def test_static_vectra_truck_exact_uv_gates_preserve_shared_consumers_and_budget(self):
        import mega_static_vectra
        for _, _, meshes, names in self.static_vectra_sources():
            for source in meshes:
                if mega_static_vectra.matches(source, names):
                    continue
                result, stats = mega_static_vectra.enhance(source, MODEL.Mesh, names)
                self.assertEqual(result.encode(), source.encode())
                self.assertEqual(stats['rebuilt_static_vectra_truck'], 0)
        _, _, meshes, names = next(self.static_vectra_sources())
        source = next(m for m in meshes if mega_static_vectra.matches(m, names))
        changed = MODEL.Mesh(source.header, list(source.vertices), list(source.faces))
        changed.vertices[0] = tuple(x + 1 / 65536 if i == 4 else x for i, x in enumerate(changed.vertices[0]))
        self.assertFalse(mega_static_vectra.matches(changed, names))
        self.assertFalse(mega_static_vectra.matches(source, ['unrelated' if n == 'LVECTR1' else n for n in names]))
        result, stats = mega_static_vectra.enhance(source, MODEL.Mesh, names, triangle_budget=0)
        self.assertEqual(result.encode(), source.encode())
        self.assertEqual(stats['added_triangles'], 0)
