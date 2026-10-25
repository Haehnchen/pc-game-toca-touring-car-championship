#!/usr/bin/env python3
"""Canonical GP/Short bridge shoulders, tread and passage contracts."""

import copy
import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import mega_dunlop as dunlop
import vehicle_model as model


def source(track):
    prefix, archive_crc, member_crc, mesh_index = {
        'gp': ('donl', '46885a20', '5f670bb9', 22),
        'short': ('dons', 'f07db8b7', 'f60fd8a0', 21),
    }[track]
    archive = f'data/{prefix}free.bfp'
    raw = model.read_source(dict(archive=archive, archive_crc32=archive_crc, member=5,
                                 member_name='OBJECTS.C3D', member_crc32=member_crc))
    header, nodes, meshes, textures = model.decode(raw)
    names, offset = [], 0
    for _ in range(struct.unpack_from('<H', header, 16)[0]):
        name, length = struct.unpack_from('<12sI', textures, offset)
        names.append(name.split(b'\0')[0].decode('ascii'))
        offset += 16 + length
    return meshes[mesh_index], names, nodes


def vertices_of(mesh, face):
    return [mesh.vertices[index] for index in struct.unpack_from('<3H', face, 2)]


def bounds(mesh):
    return [(min(vertex[axis] for vertex in mesh.vertices), max(vertex[axis] for vertex in mesh.vertices)) for axis in range(3)]


def intersects_rectangle(points, rectangle):
    """SAT catches triangles spanning a clear opening despite exterior corners."""
    x0, x1, z0, z1 = rectangle
    box = ((x0, z0), (x1, z0), (x1, z1), (x0, z1))
    axes = [(1., 0.), (0., 1.)]
    for a, b in zip(points, points[1:] + points[:1]):
        if math.dist(a, b) > 1e-12:
            axes.append((b[1] - a[1], a[0] - b[0]))
    for axis in axes:
        triangle = [model.dot(point, axis) for point in points]
        square = [model.dot(point, axis) for point in box]
        if max(triangle) < min(square) - 1e-9 or max(square) < min(triangle) - 1e-9:
            return False
    return True


class MegaDunlopTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, cls.nodes = source('gp')
        cls.generated, cls.stats = dunlop.enhance(cls.original, model.Mesh, cls.names, surface_index=len(cls.names))

    def test_exact_shared_source_material_binding(self):
        short, names, _ = source('short')
        generated, stats = dunlop.enhance(short, model.Mesh, names)
        self.assertTrue(dunlop.matches(short, names))
        self.assertEqual(self.stats, stats)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual(self.stats['dunlop_bridges'], 1)
        self.assertEqual(self.stats['dunlop_shoulders'], 4)
        self.assertEqual(self.stats['dunlop_tread_lugs'], 0)
        self.assertEqual(self.stats['dunlop_support_ends'], 2)
        self.assertEqual(self.generated.vertices, generated.vertices)
        for gp, shorter in zip(self.generated.faces, generated.faces):
            self.assertEqual(gp[:8] + gp[10:], shorter[:8] + shorter[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', gp, 8)[0]], names[struct.unpack_from('<h', shorter, 8)[0]])
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:156], self.original.vertices)
        self.assertEqual(self.generated.faces[:116], self.original.faces)

    def test_geometry_is_real_finite_outward_and_uses_only_existing_images(self):
        added_vertices = self.generated.vertices[156:]
        self.assertTrue(all(math.isfinite(value) for vertex in added_vertices for value in vertex))
        self.assertGreaterEqual(len({round(vertex[1], 4) for vertex in added_vertices}), 6)
        for face in self.generated.faces[116:]:
            slot = struct.unpack_from('<h', face, 8)[0]
            self.assertIn(self.names[slot], ('DA16DUN0', 'DA16DUN4', 'DA16DUN9'))
            a, b, c = vertices_of(self.generated, face)
            cross = model.cross(model.sub(b[:3], a[:3]), model.sub(c[:3], a[:3]))
            self.assertGreater(model.dot(cross, cross), 1e-14)
            self.assertGreater(model.dot(cross, tuple(sum(vertex[axis] for vertex in (a, b, c)) for axis in (5, 6, 7))), 0.)
            for vertex in (a, b, c):
                self.assertTrue(0. <= vertex[3] <= 1.)
                self.assertTrue(-1. <= vertex[4] <= 0.)
                self.assertAlmostEqual(model.dot(vertex[5:], vertex[5:]), 1., places=10)
        encoded = self.generated.encode()
        self.assertEqual(struct.unpack_from('<HH', encoded, 16), (len(self.generated.vertices), len(self.generated.faces)))

    def test_silhouette_feet_and_complete_central_passage_stay_in_source_envelope(self):
        before, after = bounds(self.original), bounds(self.generated)
        for axis, limit in ((0, .02), (1, .05), (2, .10)):
            self.assertGreaterEqual(after[axis][0], before[axis][0] - limit)
            self.assertLessEqual(after[axis][1], before[axis][1] + limit)
        self.assertEqual(after[2][0], before[2][0])
        self.assertGreater(after[2][1], before[2][1] + .05)
        # Full triangle/rectangle overlap, rather than just inspecting vertices:
        # even a large accidental closing face cannot bridge this passage.
        rectangle = (-11.9, 11.9, .05, 5.95)
        for mesh in (self.original, self.generated):
            for face in mesh.faces:
                points = [(vertex[0], vertex[2]) for vertex in vertices_of(mesh, face)]
                self.assertFalse(intersects_rectangle(points, rectangle), 'A triangle obstructs the canonical drive-through opening')

    def test_rounding_changes_the_profile_between_source_corners(self):
        outline = dunlop._outline(self.original, dunlop.OUTER)
        curved = 0
        for segment in range(len(dunlop.OUTER) - 1):
            a = self.original.vertices[dunlop.OUTER[segment]]
            b = self.original.vertices[dunlop.OUTER[segment + 1]]
            midpoint = outline[segment * 2 + 1][0]
            line_midpoint = ((a[0] + b[0]) / 2, (a[2] + b[2]) / 2)
            self.assertGreaterEqual(outline[segment * 2][0][1], self.original.vertices[0][2])
            if math.dist(midpoint, line_midpoint) > .04:
                curved += 1
        self.assertGreaterEqual(curved, 6, 'Additional subdivisions alone must not count as a rounded shoulder')
        # The new inner curve lies on the solid side of every original chord.
        outline = dunlop._outline(self.original, dunlop.INNER)
        for segment in range(len(dunlop.INNER) - 1):
            a = self.original.vertices[dunlop.INNER[segment]]
            b = self.original.vertices[dunlop.INNER[segment + 1]]
            normal = model.unit((a[2] - b[2], b[0] - a[0]))
            point = outline[segment * 2 + 1][0]
            self.assertGreaterEqual(model.dot(model.sub(point, (a[0], a[2])), normal), -1e-10)

    def test_source_sidewall_projection_keeps_the_continuous_artwork_map(self):
        # The new profile's inner border overlays rubber on the planar source.
        # Check actual source triangle interiors on both sides/material halves.
        for face in self.original.faces:
            slot = struct.unpack_from('<h', face, 8)[0]
            if self.names[slot] not in ('DA16DUN0', 'DA16DUN9'):
                continue
            vertices = vertices_of(self.original, face)
            if not all(abs(abs(vertex[1]) - dunlop.DEPTH) < 1e-8 for vertex in vertices) or len({vertex[1] for vertex in vertices}) != 1:
                continue
            point = tuple(sum(vertex[axis] for vertex in vertices) / 3 for axis in (0, 2))
            actual = dunlop._source_uv(self.original, slot, 1 if vertices[0][1] > 0 else -1, point)
            expected = (sum(max(.02, min(.99, vertex[3])) for vertex in vertices) / 3,
                        sum(max(.02, min(.99, vertex[4] + 1)) - 1 for vertex in vertices) / 3)
            for a, b in zip(actual, expected):
                self.assertAlmostEqual(a, b, places=10)

    def test_simple_barrel_and_support_caps_are_complete_without_tread_blocks(self):
        mesh = model.Mesh(bytes(48), [], [])
        outline = dunlop._outline(self.original, dunlop.OUTER)
        dunlop._barrel(mesh, outline, self.names.index('DA16DUN9'))
        self.assertEqual(len(mesh.faces), 40)
        self.assertEqual({round(vertex[1], 8) for vertex in mesh.vertices},
                         {round(-dunlop.DEPTH + .34, 8), round(dunlop.DEPTH - .34, 8)})
        mesh = model.Mesh(bytes(48), [], [])
        dunlop._supports(mesh, self.original, self.names.index('DA16DUN4'))
        self.assertEqual((len(mesh.vertices), len(mesh.faces)), (8, 4))
        for offset in (0, 4):
            self.assertEqual(len({vertex[0] for vertex in mesh.vertices[offset:offset + 4]}), 1)

    def test_canonical_identity(self):
        mutated = copy.deepcopy(self.original)
        mutated.vertices[0] = (mutated.vertices[0][0] + .01, *mutated.vertices[0][1:])
        renamed = list(self.names)
        renamed[49] = 'UNRELATED'
        bad_binding = copy.deepcopy(self.original)
        bad_binding.faces[0] = bad_binding.faces[0][:8] + struct.pack('<h', 53) + bad_binding.faces[0][10:]
        for mesh, names in ((mutated, self.names), (self.original, renamed), (bad_binding, self.names)):
            result, counts = dunlop.enhance(mesh, model.Mesh, names)
            self.assertIs(result, mesh)
            self.assertEqual(counts['added_triangles'], 0)


if __name__ == '__main__':
    unittest.main()
