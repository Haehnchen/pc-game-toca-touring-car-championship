#!/usr/bin/env python3
"""Shared Donington grandstand: source UV framing, true profiles and budget."""

import copy
import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import mega_grandstand as grandstand
import vehicle_model as model


def source(short=False):
    prefix, archive_crc, member_crc, mesh_index = ('dons', 'f07db8b7', 'f60fd8a0', 37) if short else \
        ('donl', '46885a20', '5f670bb9', 8)
    archive = f'data/{prefix}free.bfp'
    raw = model.read_source(dict(archive=archive, archive_crc32=archive_crc, member=5,
                                 member_name='OBJECTS.C3D', member_crc32=member_crc))
    header, nodes, meshes, textures = model.decode(raw)
    names, images, offset = [], {}, 0
    for _ in range(struct.unpack_from('<H', header, 16)[0]):
        name, length = struct.unpack_from('<12sI', textures, offset)
        names.append(name.split(b'\0')[0].decode('ascii'))
        if names[-1].upper() in ('DA02GSD6', 'DA28COM9'):
            images[names[-1].upper()] = textures[offset + 16:offset + 16 + length]
        offset += 16 + length
    return meshes[mesh_index], names, nodes, images


def rgb_rows(bmp):
    """Read the canonical uncompressed8-bit palette BMP without image packages."""
    pixel_start, header_size = struct.unpack_from('<II', bmp, 10)
    width, height, planes, bits, compression = struct.unpack_from('<iiHHI', bmp, 18)
    if bmp[:2] != b'BM' or (planes, bits, compression) != (1, 8, 0):
        raise AssertionError('Source material must be the canonical indexed BMP')
    palette_start = 14 + header_size
    palette = [tuple(bmp[palette_start + index * 4:palette_start + index * 4 + 3]) for index in range(256)]
    stride = (width + 3) // 4 * 4
    return [[palette[bmp[pixel_start + (height - 1 - y) * stride + x]] for x in range(width)] for y in range(height)]


def vertices_of(mesh, face):
    return [mesh.vertices[index] for index in struct.unpack_from('<3H', face, 2)]


def bounds(mesh):
    return [(min(vertex[axis] for vertex in mesh.vertices), max(vertex[axis] for vertex in mesh.vertices)) for axis in range(3)]


class MegaGrandstandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, _, cls.images = source()
        cls.generated, cls.stats = grandstand.enhance(cls.original, model.Mesh, cls.names, surface_index=len(cls.names))

    def test_gp_short_share_geometry_despite_scattered_material_slots(self):
        shorter, names, _, _ = source(True)
        generated, stats = grandstand.enhance(shorter, model.Mesh, names)
        self.assertTrue(grandstand.matches(shorter, names))
        self.assertEqual(self.stats, stats)
        self.assertEqual(self.generated.vertices, generated.vertices)
        for gp, short in zip(self.generated.faces, generated.faces):
            self.assertEqual(gp[:8] + gp[10:], short[:8] + short[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', gp, 8)[0]].upper(), names[struct.unpack_from('<h', short, 8)[0]].upper())
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:136], self.original.vertices)
        self.assertEqual(self.generated.faces[:80], self.original.faces)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual((self.stats['grandstand_beam_patches'], self.stats['grandstand_roof_course_patches'],
                          self.stats['grandstand_fascia_frames'], self.stats['grandstand_stair_nosings'],
                          self.stats['grandstand_roof_edges']), (20, 0, 0, 11, 9))

    def test_beam_coordinates_are_measured_source_pixel_features(self):
        beams = self.images['DA02GSD6']
        rows = rgb_rows(beams)
        means = [sum(sum(rows[y][x]) for y in range(4, 60)) / (56 * 3) for x in range(64)]
        bright = [x for x in range(1, 63) if means[x] > 75]
        groups = []
        for x in bright:
            if not groups or x != groups[-1][-1] + 1:
                groups.append([])
            groups[-1].append(x)
        self.assertEqual(tuple(sum(group) / len(group) for group in groups), grandstand.BEAM_U_PIXELS)
        # Roof courses and logo borders remain entirely in their original
        # images and exact broad source surfaces.
        self.assertEqual(self.generated.faces[:10], self.original.faces[:10])
        self.assertEqual(self.generated.faces[73:80], self.original.faces[73:80])

    def test_all_added_triangles_are_finite_nondegenerate_and_correctly_wound(self):
        for face in self.generated.faces[80:]:
            name = self.names[struct.unpack_from('<h', face, 8)[0]].upper()
            self.assertIn(name, grandstand.MATERIALS)
            self.assertNotIn(name, ('DA02GSD2', 'DA02GSD4'))
            a, b, c = vertices_of(self.generated, face)
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

    def test_tight_envelope_and_original_ground_and_crowd_surfaces(self):
        before, after = bounds(self.original), bounds(self.generated)
        for axis, tolerance in ((0, .03), (1, .15), (2, .05)):
            self.assertGreaterEqual(after[axis][0], before[axis][0] - tolerance)
            self.assertLessEqual(after[axis][1], before[axis][1] + tolerance)
        self.assertEqual(after[2][1], before[2][1])
        self.assertGreater(before[2][0] - after[2][0], .03)
        self.assertGreater(before[1][0] - after[1][0], .13)
        # Faces10..29 are the source spectators and the front/side crowd edge;
        # no step grid, new railing or support is added through that artwork.
        self.assertEqual(self.generated.faces[10:30], self.original.faces[10:30])
        old_y = {round(vertex[1], 4) for vertex in self.original.vertices}
        new_y = {round(vertex[1], 4) for vertex in self.generated.vertices[136:]}
        self.assertGreater(len(new_y - old_y), 20)

    def assert_attached(self, mesh, face_index, height):
        _, normal, _ = grandstand._triangle(self.original, face_index)
        for vertex in mesh.vertices:
            projected, _, _ = grandstand._sample(self.original, (face_index,), vertex[3:5])
            offset = model.sub(vertex[:3], projected[0])
            depth = model.dot(offset, normal)
            error = math.dist(offset, model.mul(normal, depth))
            self.assertLess(error, 1e-9, 'Additional paint must project to the actual source triangle UV')
            self.assertTrue(abs(depth) < 1e-9 or abs(depth - height) < 1e-9)

    def test_structural_roof_framing_follows_exact_inset_source_paint(self):
        patches = 0
        for face_index in range(30, 47):
            mesh = model.Mesh(bytes(48), [], [])
            patches += grandstand._bands(mesh, self.original, (face_index,), 0,
                                          [value / 64 for value in grandstand.BEAM_U_PIXELS], .7 / 64, .08)
            self.assert_attached(mesh, face_index, .08)
        self.assertEqual(patches, 20)

    def test_source_stairs_have_real_treads_and_risers_and_leave_the_sign_field_clear(self):
        mesh = model.Mesh(bytes(48), [], [])
        self.assertEqual(grandstand._stairs(mesh, self.original), 11)
        self.assertEqual(len(mesh.faces), 110)
        self.assertGreater(len({round(vertex[2], 4) for vertex in mesh.vertices}), 10)
        self.assertTrue(any(abs(vertex[7]) > .99 for vertex in mesh.vertices), 'Stair treads require actual horizontal geometry')
        self.assertTrue(any(vertex[6] < -.99 for vertex in mesh.vertices), 'Stair risers require actual vertical geometry')
        self.assertFalse(any(.01 < vertex[0] < 4.99 for vertex in mesh.vertices), 'The source pale notice panel must stay clear')
        for vertex in mesh.vertices:
            candidates = []
            for faces, _, _, _ in grandstand.STAIRS:
                try:
                    sample, normal, _ = grandstand._sample(self.original, faces, vertex[3:5])
                except ValueError:
                    continue
                offset = model.sub(vertex[:3], sample[0])
                depth = model.dot(offset, normal)
                candidates.append((math.dist(offset, model.mul(normal, depth)), depth))
            error, depth = min(candidates)
            self.assertLess(error, 1e-8)
            self.assertTrue(abs(depth) < 1e-8 or abs(depth - .14) < 1e-8)

    def test_canonical_geometry_material_identity(self):
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        rebound = copy.deepcopy(self.original)
        rebound.faces[0] = rebound.faces[0][:8] + struct.pack('<h', 31) + rebound.faces[0][10:]
        renamed = list(self.names)
        renamed[26] = 'UNRELATED'
        for mesh, names in ((altered, self.names), (rebound, self.names), (self.original, renamed)):
            result, counts = grandstand.enhance(mesh, model.Mesh, names)
            self.assertIs(result, mesh)
            self.assertEqual(counts['added_triangles'], 0)


if __name__ == '__main__':
    unittest.main()
