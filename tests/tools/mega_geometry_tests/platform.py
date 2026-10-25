"""Canonical scenery geometry checks for platform."""

import math
import struct

from .environment import MODEL, detail
from .fixtures import SceneryCase


class StaticPlatformTests(SceneryCase):
    def test_static_platform_wheels_close_bodies_preserve_uvs_and_limit_only_side_relief(self):
        from collections import Counter
        import mega_static_platform
        frozen_outputs = {(0, 5): 'e8896081', (0, 6): 'f5f46df5',
                          (2, 5): '348ead21', (2, 6): 'ce05473c',
                          (4, 5): 'da998faf', (4, 6): '7458ed6d',
                          (5, 5): '5696d9ee', (5, 6): '6e7aa344'}
        template = None
        for track, member, meshes, names in self.static_platform_sources():
            selected = [mesh for mesh in meshes if mega_static_platform.matches(mesh, names)]
            self.assertEqual(len(selected), 1)
            source = selected[0]
            if template is not None:
                original, shared, original_names = template
                # All eight sources have exactly the same geometry/UV/normal
                # records. Palette/header/slot differences require no second
                # geometry construction, but every accepted output stays fixed.
                self.assertEqual(source.vertices, original.vertices)
                self.assertEqual(source.faces, self.remap_face_slots(original.faces, original_names, names))
                remapped = MODEL.Mesh(source.header, shared.vertices,
                                      self.remap_face_slots(shared.faces, original_names, names))
                self.assertEqual(MODEL.crc32(remapped.encode()), frozen_outputs[track, member])
                continue
            before = source.encode()
            result, stats, description = mega_static_platform._build(source, MODEL.Mesh, names, 2048)
            dispatched, counters = detail.scenery(source, MODEL.Mesh, names, len(names), 'objects')
            self.assertEqual(dispatched.encode(), result.encode())
            self.assertEqual(counters, stats)
            self.assertEqual(MODEL.crc32(result.encode()), frozen_outputs[track, member])
            template = source, result, names
            self.assertEqual((stats['platform_wheels'], stats['closed_platform_bodies']), (6, 2))
            accumulated = Counter()
            accumulated.update(stats)
            accumulated.update(stats)
            self.assertEqual(accumulated['rebuilt_static_platform'], 2)
            self.assertEqual(source.encode(), before)
            self.assertEqual(result.header, source.header)
            self.assertEqual(result.vertices[:len(source.vertices)], source.vertices)
            kept = [i for i in range(len(source.faces)) if i not in description['replaced_source_faces']]
            preserved_positions = {kept.index(i) for i in description['preserved_source_faces']}
            for position in preserved_positions:
                self.assertEqual(result.faces[position], source.faces[kept[position]])
            # All boom and basket faces remain exact, including the open top.
            for source_id in (*range(8, 16), *range(38, 64)):
                self.assertIn(source_id, description['preserved_source_faces'])
            generated = MODEL.Mesh(result.header, result.vertices,
                                   [f for i, f in enumerate(result.faces) if i not in preserved_positions])
            self.assert_valid_added_faces(MODEL.Mesh(source.header, source.vertices, []), generated)
            for name, faces in description['body_components'].items():
                edges = Counter()
                for fi in faces:
                    points = [tuple(round(x * 65536) for x in result.vertices[i][:3])
                              for i in struct.unpack_from('<3H', result.faces[fi], 2)]
                    for a, b in zip(points, points[1:] + points[:1]):
                        edges[tuple(sorted((a, b)))] += 1
                self.assertTrue(edges and set(edges.values()) == {2}, name)
                for fi in description['underside_faces'][name]:
                    self.assertEqual(struct.unpack_from('<h', result.faces[fi], 8)[0], names.index('CRANE4B'))
                    for i in struct.unpack_from('<3H', result.faces[fi], 2):
                        self.assertEqual(result.vertices[i][5:], (0., 0., 1.))
                        self.assertEqual(result.vertices[i][3:5], (.12890625, -.546875))
            for wheel in description['wheels']:
                self.assertAlmostEqual(wheel['outward_relief'], .03)
                self.assertAlmostEqual(wheel['center'][2] + wheel['radius_z'], wheel['original_ground_z'])
                cap_points = []
                for fi in wheel['cap_faces']:
                    face = result.faces[fi]
                    self.assertEqual(struct.unpack_from('<h', face, 8)[0], names.index('CRANE4B'))
                    for vi in struct.unpack_from('<3H', face, 2):
                        vertex = result.vertices[vi]
                        cap_points.append(vertex)
                        self.assertGreater(vertex[5] * wheel['side'], .999)
                        # Recover the exact source surface point by removing
                        # only the authorized outward offset. Source triangle
                        # barycentric XYZ/UV agreement is independent of the
                        # implementation's polygon partitioning.
                        point = (vertex[0] - wheel['side'] * .03, *vertex[1:3])
                        candidates = []
                        for source_id in wheel['source_faces']:
                            records = [source.vertices[i] for i in struct.unpack_from('<3H', source.faces[source_id], 2)]
                            a, b, c = [v[:3] for v in records]
                            ab, ac, ap = MODEL.sub(b, a), MODEL.sub(c, a), MODEL.sub(point, a)
                            aa, cc, bc = MODEL.dot(ab, ab), MODEL.dot(ac, ac), MODEL.dot(ab, ac)
                            denominator = aa * cc - bc * bc
                            d, e = MODEL.dot(ap, ab), MODEL.dot(ap, ac)
                            u, v = (cc * d - bc * e) / denominator, (aa * e - bc * d) / denominator
                            weights = (1 - u - v, u, v)
                            reconstructed = [sum(weights[j] * records[j][axis] for j in range(3)) for axis in range(5)]
                            candidates.append((sum(max(0., -w) for w in weights), reconstructed))
                        _, reconstructed = min(candidates, key=lambda c: c[0])
                        for actual, expected in zip((*point, *vertex[3:5]), reconstructed):
                            self.assertAlmostEqual(actual, expected, places=7)
                ring_points = []
                for fi in wheel['ring_faces']:
                    face = result.faces[fi]
                    self.assertEqual(struct.unpack_from('<h', face, 8)[0], names.index('CRANE4B'))
                    self.assertEqual(tuple(face[10:14]), (35, 36, 35, 255))
                    for vi in struct.unpack_from('<3H', face, 2):
                        vertex = result.vertices[vi]
                        ring_points.append(vertex)
                        self.assertEqual(vertex[3:5], (.12890625, -.546875))
                        self.assertGreater(vertex[5] * wheel['side'], .999)
                        radius = math.hypot((vertex[1] - wheel['center'][1]) / wheel['radius_y'],
                                            (vertex[2] - wheel['center'][2]) / wheel['radius_z'])
                        self.assertGreaterEqual(radius, .84)
                        self.assertLessEqual(radius, 1.000001)
                self.assertTrue(ring_points)
                for fi in wheel['tread_faces']:
                    self.assertEqual(struct.unpack_from('<h', result.faces[fi], 8)[0], names.index('CRANE4B'))
                    for vi in struct.unpack_from('<3H', result.faces[fi], 2):
                        self.assertEqual(result.vertices[vi][3:5], (.12890625, -.546875))
                for vertex in cap_points:
                    radius = math.hypot((vertex[1] - wheel['center'][1]) / wheel['radius_y'],
                                        (vertex[2] - wheel['center'][2]) / wheel['radius_z'])
                    self.assertLessEqual(radius, .880001)
                self.assertAlmostEqual(max(v[2] for v in ring_points), wheel['original_ground_z'], delta=1 / 65536)
            for axis in range(3):
                lower = min(v[axis] for v in result.vertices) - min(v[axis] for v in source.vertices)
                upper = max(v[axis] for v in result.vertices) - max(v[axis] for v in source.vertices)
                if axis == 0:
                    self.assertGreaterEqual(lower, -.030001)
                    self.assertLessEqual(upper, .030001)
                else:
                    self.assertAlmostEqual(lower, 0.)
                    self.assertAlmostEqual(upper, 0.)

    def test_static_platform_gate_and_budget_preserve_other_source_shapes(self):
        import mega_static_platform
        for _, _, meshes, names in self.static_platform_sources():
            for source in meshes:
                if mega_static_platform.matches(source, names):
                    continue
                result, stats = mega_static_platform.enhance(source, MODEL.Mesh, names)
                self.assertEqual(result.encode(), source.encode())
                self.assertEqual(stats['rebuilt_static_platform'], 0)
        _, _, meshes, names = next(self.static_platform_sources())
        source = next(m for m in meshes if mega_static_platform.matches(m, names))
        changed = MODEL.Mesh(source.header, list(source.vertices), list(source.faces))
        changed.vertices[0] = tuple(x + 1 / 65536 if i == 0 else x for i, x in enumerate(changed.vertices[0]))
        self.assertFalse(mega_static_platform.matches(changed, names))
        self.assertFalse(mega_static_platform.matches(source, ['unrelated' if n == 'CRANE4B' else n for n in names]))
        result, stats = mega_static_platform.enhance(source, MODEL.Mesh, names, triangle_budget=0)
        self.assertEqual(result.encode(), source.encode())
        self.assertEqual(stats['added_triangles'], 0)
