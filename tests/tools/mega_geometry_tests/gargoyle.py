"""Canonical scenery geometry checks for gargoyle."""

import struct
import unittest

from .environment import MODEL, detail


class MegaGargoyleGeometryTests(unittest.TestCase):
    def source(self):
        raw = MODEL.read_source(dict(archive='data/landfree.bfp',
                                    archive_crc32='dba52c51', member=8,
                                    member_name='CONES.C3D', member_crc32='ca267324'))
        return MODEL.decode(raw)[2][0]

    @staticmethod
    def records(mesh, face):
        return [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]

    @staticmethod
    def weights(triangle, point, axes):
        a, b, c = triangle
        x, y = axes
        denominator = (b[x] - a[x]) * (c[y] - a[y]) - (b[y] - a[y]) * (c[x] - a[x])
        if abs(denominator) < 1e-12:
            return None
        u = ((point[x] - a[x]) * (c[y] - a[y]) - (point[y] - a[y]) * (c[x] - a[x])) / denominator
        v = ((b[x] - a[x]) * (point[y] - a[y]) - (b[y] - a[y]) * (point[x] - a[x])) / denominator
        return 1 - u - v, u, v

    @staticmethod
    def interpolate(triangle, factors):
        return tuple(sum(w * vertex[axis] for w, vertex in zip(factors, triangle)) for axis in range(8))

    def test_relief_is_closed_and_keeps_source_uvs_pivot_and_closure(self):
        from collections import Counter
        import mega_gargoyle
        source = self.source()
        before = source.encode()
        result = mega_gargoyle.enhance(source, MODEL.Mesh, ['GARG01', 'GARG02'])
        self.assertEqual(source.encode(), before)
        self.assertEqual(result.header, source.header)
        self.assertEqual(result.vertices[:len(source.vertices)], source.vertices)
        self.assertEqual(detail.bounds(result), detail.bounds(source))
        self.assertEqual(sum(detail.bounds(result)[1]) / 2, 41023 / 131072)
        fronts = [self.records(source, face) for face in source.faces[:3]]
        protected = [self.records(source, face) for face in source.faces[3:]]
        edges, displacement = Counter(), []
        for face in result.faces:
            triangle = self.records(result, face)
            normal = MODEL.cross(MODEL.sub(triangle[1][:3], triangle[0][:3]), MODEL.sub(triangle[2][:3], triangle[0][:3]))
            self.assertGreater(MODEL.dot(normal, normal), 1e-14)
            self.assertGreater(MODEL.dot(normal, MODEL.add(MODEL.add(triangle[0][5:], triangle[1][5:]), triangle[2][5:])), 0)
            points = [tuple(round(x, 8) for x in vertex[:3]) for vertex in triangle]
            for a, b in zip(points, points[1:] + points[:1]):
                edges[tuple(sorted((a, b)))] += 1
            slot, = struct.unpack_from('<h', face, 8)
            self.assertIn(slot, (0, 1))
            if slot == 0:
                for vertex in triangle:
                    candidates = [(original, factors) for original in fronts
                                  if (factors := self.weights(original, vertex, (3, 4))) is not None and min(factors) >= -1e-8]
                    self.assertTrue(candidates)
                    original = self.interpolate(*candidates[0])
                    for axis in (0, 1, 3, 4):
                        self.assertAlmostEqual(vertex[axis], original[axis], places=8)
                    displacement.append(vertex[2] - original[2])
                    self.assertAlmostEqual(MODEL.dot(vertex[5:], vertex[5:]), 1., places=6)
            else:
                # A closure triangle stays on one original source plane, with
                # the original affine UVs and separate flat lighting normal.
                matches = []
                for original in protected:
                    for axes in ((0, 1), (0, 2), (1, 2)):
                        projected = [self.weights(original, vertex, axes) for vertex in triangle]
                        if any(factors is None or min(factors) < -1e-8 for factors in projected):
                            continue
                        expected = [self.interpolate(original, factors) for factors in projected]
                        if all(max(abs(vertex[axis] - point[axis]) for axis in range(8)) < 1e-8 for vertex, point in zip(triangle, expected)):
                            matches.append(original)
                self.assertTrue(matches, 'GARG02 closure must retain a source surface and its UVs/normals')
        self.assertEqual(set(edges.values()), {2}, 'The complete head must stay closed without T junctions')
        self.assertLess(min(displacement), -.01)
        self.assertGreater(max(displacement), .002)

    def test_rejects_other_shapes_materials_and_changed_source(self):
        import mega_gargoyle
        source = self.source()
        self.assertTrue(mega_gargoyle.matches(source, ['GARG01', 'GARG02']))
        changed = MODEL.Mesh(source.header, list(source.vertices), list(source.faces))
        vertex = changed.vertices[0]
        changed.vertices[0] = (vertex[0] + .01, *vertex[1:])
        renamed = MODEL.Mesh(b'LRED_1'.ljust(12, b'\0') + source.header[12:], list(source.vertices), list(source.faces))
        for original, names in ((source, ['GARG02', 'GARG01']), (changed, ['GARG01', 'GARG02']), (renamed, ['GARG01', 'GARG02'])):
            self.assertFalse(mega_gargoyle.matches(original, names))
            result = mega_gargoyle.enhance(original, MODEL.Mesh, names)
            self.assertEqual((result.vertices, result.faces), (original.vertices, original.faces))
