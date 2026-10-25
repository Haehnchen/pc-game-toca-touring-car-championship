#!/usr/bin/env python3
"""Donington's shared barn: actual roof/frames and original UV attachments."""

import copy
import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import mega_barn as barn
import vehicle_model as model


def source(short=False):
    prefix, archive_crc, member_crc, mesh_index = ('dons', 'f07db8b7', 'f60fd8a0', 12) if short else \
        ('donl', '46885a20', '5f670bb9', 13)
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


def face_vertices(mesh, face):
    return [mesh.vertices[index] for index in struct.unpack_from('<3H', face, 2)]


def bounds(mesh):
    return [(min(vertex[axis] for vertex in mesh.vertices), max(vertex[axis] for vertex in mesh.vertices)) for axis in range(3)]


def surfaces(original, names):
    specifications = ([(indices, 'DA50BAR1') for indices in barn.SIDES] +
                      [(barn.FRONT, 'DA50BAR3'), (barn.REAR, 'DA50BAR2')] +
                      [(indices, 'DA50BAR4') for indices in barn.ROOFS])
    return [barn.Surface.source(original, indices, names.index(name))
            for indices, name in specifications]


class MegaBarnTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, _ = source()
        cls.generated, cls.stats = barn.enhance(cls.original, model.Mesh, cls.names, surface_index=len(cls.names))

    def test_gp_short_share_geometry_and_source_surfaces_stay_exact(self):
        shorter, names, _ = source(True)
        generated, stats = barn.enhance(shorter, model.Mesh, names)
        self.assertTrue(barn.matches(shorter, names))
        self.assertEqual(self.stats, stats)
        self.assertEqual(self.generated.vertices, generated.vertices)
        for gp, short in zip(self.generated.faces, generated.faces):
            self.assertEqual(gp[:8] + gp[10:], short[:8] + short[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', gp, 8)[0]], names[struct.unpack_from('<h', short, 8)[0]])
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:30], self.original.vertices)
        self.assertEqual(self.generated.faces[:14], self.original.faces)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual((self.stats['barn_roof_ribs'], self.stats['barn_window_frames'], self.stats['barn_panel_frames'],
                          self.stats['barn_door_reveals'], self.stats['barn_corner_strips']), (0, 6, 2, 1, 8))

    def test_new_geometry_has_valid_winding_finite_normalized_corners_and_source_materials(self):
        for face in self.generated.faces[14:]:
            self.assertIn(self.names[struct.unpack_from('<h', face, 8)[0]], barn.MATERIALS)
            a, b, c = face_vertices(self.generated, face)
            normal = model.cross(model.sub(b[:3], a[:3]), model.sub(c[:3], a[:3]))
            self.assertGreater(model.dot(normal, normal), 1e-14)
            average = tuple(sum(vertex[axis] for vertex in (a, b, c)) for axis in (5, 6, 7))
            self.assertGreater(model.dot(normal, average), 0.)
            for vertex in (a, b, c):
                self.assertTrue(all(math.isfinite(value) for value in vertex))
                self.assertTrue(.02 - 1e-12 <= vertex[3] <= .99 + 1e-12)
                self.assertTrue(-.98 - 1e-12 <= vertex[4] <= -.01 + 1e-12)
                self.assertAlmostEqual(model.dot(vertex[5:], vertex[5:]), 1., places=10)
        encoded = self.generated.encode()
        self.assertEqual(struct.unpack_from('<HH', encoded, 16), (len(self.generated.vertices), len(self.generated.faces)))

    def test_envelope_preserves_original_foot_plane_and_clears_real_roof_and_wall_depth(self):
        before, after = bounds(self.original), bounds(self.generated)
        for axis in range(3):
            self.assertGreaterEqual(after[axis][0], before[axis][0] - .13)
            self.assertLessEqual(after[axis][1], before[axis][1] + .13)
        self.assertAlmostEqual(after[2][1], before[2][1], places=12)
        self.assertAlmostEqual(before[2][0] - after[2][0], .08, places=10)
        planes = surfaces(self.original, self.names)
        depths = []
        for vertex in self.generated.vertices[30:]:
            depths.append(min(abs(model.dot(model.sub(vertex[:3], surface.vertices[0][:3]), surface.normal)) for surface in planes))
        self.assertGreater(sum(depth > .01 for depth in depths), 200)
        self.assertGreater(max(depths), .04)

    def test_sampler_matches_source_triangle_interpolation_including_unequal_roof_uvs(self):
        for surface in surfaces(self.original, self.names):
            for s, t, weights in ((.2, .3, (.5, .2, 0., .3)), (.8, .7, (0., .3, .5, .2))):
                point, uv = surface.sample(s, t)
                expected_point = tuple(sum(weight * vertex[axis] for weight, vertex in zip(weights, surface.vertices)) for axis in range(3))
                source_uvs = [(max(.02, min(.99, vertex[3])), max(.02, min(.99, vertex[4] + 1)) - 1) for vertex in surface.vertices]
                expected_uv = tuple(sum(weight * value[axis] for weight, value in zip(weights, source_uvs)) for axis in range(2))
                for value, expected in zip((*point, *uv), (*expected_point, *expected_uv)):
                    self.assertAlmostEqual(value, expected, places=12)
        # Insetting must occur before interpolation: raw U0..1 would displace
        # the narrow original window frames and the source roof edge stripes.
        right_wall = surfaces(self.original, self.names)[0]
        point, uv = right_wall.image(6, 5)
        self.assertAlmostEqual(uv[0], 6 / 64, places=12)
        self.assertAlmostEqual(uv[1] + 1, 5 / 32, places=12)
        self.assertGreater(math.dist(point, right_wall.sample(6 / 64, 5 / 32)[0]), .05)

    def test_wall_detail_projects_to_original_paint_and_leaves_window_interiors_visible(self):
        walls = surfaces(self.original, self.names)[:4]
        for face in self.generated.faces[14:]:
            slot = struct.unpack_from('<h', face, 8)[0]
            if self.names[slot] == 'DA50BAR4':
                continue
            vertices = face_vertices(self.generated, face)
            for vertex in vertices:
                candidates = []
                for surface in walls:
                    if surface.texture != slot:
                        continue
                    projected, _ = surface.image(vertex[3] * 64, (vertex[4] + 1) * 32)
                    offset = model.sub(vertex[:3], projected)
                    height = model.dot(offset, surface.normal)
                    error = math.dist(offset, model.mul(surface.normal, height))
                    candidates.append((error, height))
                error, height = min(candidates)
                self.assertLess(error, 3e-5)
                self.assertGreaterEqual(height, .0019)
                self.assertLessEqual(height, .12001)
            if self.names[slot] == 'DA50BAR1':
                # A raised full-size pane would hide the source dark window
                # paint. All front-facing frame triangles must stay on its rim.
                surface = min(walls[:2], key=lambda candidate: abs(model.dot(model.sub(vertices[0][:3], candidate.vertices[0][:3]), candidate.normal)))
                heights = [model.dot(model.sub(vertex[:3], surface.vertices[0][:3]), surface.normal) for vertex in vertices]
                if all(abs(height - .065) < 3e-5 for height in heights):
                    x, y = (sum(vertex[3] for vertex in vertices) / 3 * 64,
                            (sum(vertex[4] for vertex in vertices) / 3 + 1) * 32)
                    for x0, y0, x1, y1 in barn.SIDE_WINDOWS:
                        self.assertFalse(x0 + .8 < x < x1 - .8 and y0 + .7 < y < y1 - .7)

    def test_plain_roof_keeps_gable_planes_and_closed_edge_sections(self):
        # Corrugation lives in DA50BAR4. Only the eaves, closed flashing and
        # ridge add geometry outside the two exact source roof planes.
        mesh = model.Mesh(bytes(48), [], [])
        roofs = surfaces(self.original, self.names)[4:]
        barn._roof_edges(mesh, roofs)
        self.assertEqual(len(mesh.faces), 78)
        self.assertEqual(self.stats['barn_roof_ribs'], 0)
        self.assertEqual(self.generated.faces[6:10], self.original.faces[6:10])
        self.assertTrue(any(abs(vertex[7]) > .8 for vertex in mesh.vertices))

    def test_exact_source_identity(self):
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        renamed = list(self.names)
        renamed[38] = 'UNRELATED'
        rebound = copy.deepcopy(self.original)
        rebound.faces[0] = rebound.faces[0][:8] + struct.pack('<h', 41) + rebound.faces[0][10:]
        for mesh, names in ((altered, self.names), (self.original, renamed), (rebound, self.names)):
            result, counts = barn.enhance(mesh, model.Mesh, names)
            self.assertIs(result, mesh)
            self.assertEqual(counts['added_triangles'], 0)


if __name__ == '__main__':
    unittest.main()
