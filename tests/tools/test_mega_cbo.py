#!/usr/bin/env python3
"""Donington CBO1–6: shared source setup and complete per-type contracts."""

from collections import Counter
import copy
from functools import lru_cache
import importlib
import math
from pathlib import Path
import struct
import sys
import unittest
from PIL import Image

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import mega_cbo4 as shared
import vehicle_model as model
from vehicle_textures import texture_records


@lru_cache(maxsize=2)
def object_data(short):
    prefix, archive_crc, member_crc = ('dons', 'f07db8b7', 'f60fd8a0') if short else ('donl', '46885a20', '5f670bb9')
    return model.read_source(dict(archive=f'data/{prefix}free.bfp', archive_crc32=archive_crc,
                                  member=5, member_name='OBJECTS.C3D', member_crc32=member_crc))


def source(number, short=False):
    header, node_bytes, meshes, textures = model.decode(object_data(short))
    # Counted scenery families carry the original exporter terminator.
    if textures[-1:] != b'\0':
        raise ValueError('Expected canonical OBJECTS texture terminator')
    records = texture_records(textures[:-1], struct.unpack_from('<H', header, 16)[0])
    names = [name for name, _, _ in records]
    nodes, offset = [], 0
    while offset < len(node_bytes):
        size = struct.unpack_from('<I', node_bytes, offset)[0]
        nodes.append(node_bytes[offset:offset + size])
        offset += size
    indices = (50, 54, 53, 9, 10, 11) if short else (48, 52, 51, 10, 11, 12)
    original = meshes[indices[number - 1]]
    if number in (2, 4):
        cbo = importlib.import_module(f'mega_cbo{number}')
        images = {name: bmp for name, _, bmp in records if name in cbo.MATERIALS}
        return (original, names, images, meshes) if number == 2 else (original, names, images, nodes, meshes)
    node = ({1: 347, 3: 379, 5: 37, 6: 39} if short else {1: 307, 3: 339, 5: 35, 6: 37})[number]
    return original, names, nodes, meshes, node



def bounds(mesh):
    return [(min(vertex[axis] for vertex in mesh.vertices), max(vertex[axis] for vertex in mesh.vertices)) for axis in range(3)]


def material_names(mesh, names):
    return [names[struct.unpack_from('<h', face, 8)[0]] for face in mesh.faces]


def edge_counts(mesh, source_faces=()):
    edges = Counter()
    faces = [source_faces] if source_faces else []
    for group in faces + [mesh.faces]:
        for face in group:
            points = [tuple(round(value, 6) for value in mesh.vertices[index][:3])
                      for index in struct.unpack_from('<3H', face, 2)]
            for a, b in zip(points, points[1:] + points[:1]):
                edges[tuple(sorted((a, b)))] += 1
    return edges


def rgb_rows(bmp):
    pixel_start, header_size = struct.unpack_from('<II', bmp, 10)
    width, height, planes, bits, compression = struct.unpack_from('<iiHHI', bmp, 18)
    if bmp[:2] != b'BM' or (planes, bits, compression) != (1, 8, 0):
        raise AssertionError('Expected canonical uncompressed8-bit palette BMP')
    start = 14 + header_size
    palette = [(bmp[start + i * 4 + 2], bmp[start + i * 4 + 1], bmp[start + i * 4]) for i in range(256)]
    stride = (width + 3) // 4 * 4
    return [[palette[bmp[pixel_start + (height - 1 - y) * stride + x]] for x in range(width)] for y in range(height)]


def vertices_of(mesh, face):
    return [mesh.vertices[index] for index in struct.unpack_from('<3H', face, 2)]


def source_point(original, face_index, uv):
    samples, normal, _ = shared._triangle(original, face_index)
    a, b, c = [sample[1] for sample in samples]
    edge, other, query = model.sub(b, a), model.sub(c, a), model.sub(uv, a)
    determinant = edge[0] * other[1] - edge[1] * other[0]
    beta = (query[0] * other[1] - query[1] * other[0]) / determinant
    gamma = (edge[0] * query[1] - edge[1] * query[0]) / determinant
    weights = (1 - beta - gamma, beta, gamma)
    if min(weights) < -1e-7:
        raise ValueError('UV outside source triangle')
    return tuple(sum(weight * sample[0][axis] for weight, sample in zip(weights, samples)) for axis in range(3)), normal


class MegaCbo1Tests(unittest.TestCase):

    cbo = importlib.import_module("mega_cbo1")

    source = staticmethod(lambda short=False: source(1, short))

    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, cls.nodes, cls.meshes, cls.node = cls.source()
        cls.generated, cls.stats = cls.cbo.enhance(cls.original, model.Mesh, cls.names)

    def test_gp_short_share_one_type_with_exact_original_prefix_and_placements(self):
        short, names, nodes, _, node = self.source(True)
        generated, stats = self.cbo.enhance(short, model.Mesh, names)
        self.assertTrue(self.cbo.matches(self.original, self.names) and self.cbo.matches(short, names))
        self.assertEqual(stats, self.stats)
        self.assertEqual(self.generated.vertices, generated.vertices)
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:84], self.original.vertices)
        self.assertEqual(self.generated.faces[:54], self.original.faces)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual((self.stats['cbo1_window_patches'], self.stats['cbo1_roof_edges'], self.stats['cbo1_sign_backings']), (26, 13, 1))
        self.assertEqual(self.nodes[self.node - 1], nodes[node - 1])
        self.assertEqual(model.crc32(self.nodes[self.node - 1]), 'dad33547')
        self.assertEqual(struct.unpack_from('<h', self.nodes[self.node], 4)[0], self.node - 1)
        self.assertEqual(struct.unpack_from('<h', nodes[node], 4)[0], node - 1)
        for a, b in zip(self.generated.faces, generated.faces):
            self.assertEqual(a[:8] + a[10:], b[:8] + b[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', a, 8)[0]], names[struct.unpack_from('<h', b, 8)[0]])

    def test_original_material_roles_are_preserved(self):
        expected = [name for name, count in zip(self.cbo.MATERIALS, self.cbo.MATERIAL_COUNTS) for _ in range(count)]
        self.assertEqual([self.names[struct.unpack_from('<h', face, 8)[0]] for face in self.original.faces], expected)
        self.assertTrue(all(self.names[struct.unpack_from('<h', face, 8)[0]] in
                            ('DA28COM', 'DA28COM6', 'DA28COM7', 'DA28PIT2', 'DA28COM8', 'DA28COM9')
                            for face in self.generated.faces[54:]))

    def test_real_lit_geometry_stays_attached_within_the_source_envelope(self):
        old, new = bounds(self.original), bounds(self.generated)
        for axis in range(3):
            self.assertGreaterEqual(new[axis][0], old[axis][0] - .1)
            self.assertLessEqual(new[axis][1], old[axis][1] + .1)
        self.assertEqual(new[2][1], old[2][1])
        self.assertLess(len(self.generated.vertices), 65536)
        for face in self.generated.faces[54:]:
            vertices = [self.generated.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
            normal = model.cross(model.sub(vertices[1][:3], vertices[0][:3]), model.sub(vertices[2][:3], vertices[0][:3]))
            lighting = tuple(sum(vertex[axis] for vertex in vertices) for axis in (5, 6, 7))
            self.assertGreater(model.dot(normal, normal), 1e-14)
            self.assertGreater(model.dot(normal, lighting), 0)
            for vertex in vertices:
                self.assertTrue(all(math.isfinite(value) for value in vertex))
                self.assertTrue(.02 - 1e-12 <= vertex[3] <= .99 + 1e-12)
                self.assertTrue(-.985 - 1e-12 <= vertex[4] <= -.01 + 1e-12)

    def test_window_bands_project_to_each_original_tapered_panel(self):
        count = 0
        for faces, width, height, rectangles in self.cbo.WINDOW_GROUPS:
            for face_index in faces:
                samples, normal, _ = shared._triangle(self.original, face_index)
                for x0, y0, x1, y1 in rectangles:
                    mesh = model.Mesh(bytes(48), [], [])
                    count += shared._window_panel(mesh, self.original, (face_index,),
                                           (x0 / width, y0 / height - 1, x1 / width, y1 / height - 1))
                    for vertex in mesh.vertices:
                        distance = model.dot(model.sub(vertex[:3], samples[0][0]), normal)
                        point = model.sub(vertex[:3], model.mul(normal, distance))
                        shared._project(self.original, (face_index,), point)
                        self.assertTrue(abs(distance) < 1e-8 or abs(distance - .065) < 1e-8)
        self.assertEqual(count, 26)

    def test_flat_terrace_cabin_and_mast_roofs_keep_their_actual_perimeters(self):
        self.assertEqual([len(shared._boundary(self.original, faces)) for faces in self.cbo.ROOFS], [4, 5, 4])
        for faces in self.cbo.ROOFS:
            for face_index in faces:
                _, normal, _ = shared._triangle(self.original, face_index)
                self.assertLess(normal[2], -.999)

    def test_four_triangle_sign_front_gets_one_unlettered_reverse(self):
        mesh = model.Mesh(bytes(48), [], [])
        self.cbo._sign_backing(mesh, self.original)
        self.assertEqual({vertex[3:5] for vertex in mesh.vertices}, {self.cbo.SIGN_BLANK_UV})
        self.assertEqual(len(mesh.faces), 16)
        self.assertEqual(len(shared._boundary(self.original, self.cbo.SIGN_FACES)), 6)
        for index in self.cbo.SIGN_FACES:
            _, normal, _ = shared._triangle(self.original, index)
            self.assertLess(normal[1], -.999)
        for index in (39, 40):
            _, normal, _ = shared._triangle(self.original, index)
            self.assertLess(normal[0], -.999)

    def test_exact_identity_excludes_unrelated_buildings(self):
        for mesh in self.meshes:
            if mesh is not self.original:
                self.assertFalse(self.cbo.matches(mesh, self.names))
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        self.assertFalse(self.cbo.matches(altered, self.names))


class MegaCbo2Tests(unittest.TestCase):

    cbo = importlib.import_module("mega_cbo2")

    source = staticmethod(lambda short=False: source(2, short))

    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, cls.images, cls.meshes = cls.source()
        cls.generated, cls.stats = cls.cbo.enhance(cls.original, model.Mesh, cls.names)

    def test_exact_gp_short_identity_preserves_source_prefix(self):
        short, names, images, _ = self.source(True)
        generated, stats = self.cbo.enhance(short, model.Mesh, names)
        self.assertTrue(self.cbo.matches(self.original, self.names) and self.cbo.matches(short, names))
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:len(self.original.vertices)], self.original.vertices)
        self.assertEqual(self.generated.faces[:len(self.original.faces)], self.original.faces)
        self.assertEqual(self.generated.vertices, generated.vertices)
        self.assertEqual(self.stats, stats)
        self.assertEqual(self.images, images)
        for a, b in zip(self.generated.faces, generated.faces):
            self.assertEqual(a[:8] + a[10:], b[:8] + b[10:])
        self.assertEqual(material_names(self.generated, self.names), material_names(generated, names))

    def test_distinct_comb_material_and_original_uv_roles_are_retained(self):
        self.assertEqual(material_names(self.original, self.names),
                         ['DA28PIT3'] * 4 + ['DA28COM9'] * 10 + ['DA28COMA'] * 8 +
                         ['DA28COMB'] * 8 + ['DA02GSD9'] * 2)
        self.assertTrue(set(material_names(self.generated, self.names)[len(self.original.faces):]) <=
                        {'DA28PIT3', 'DA28COM9'})

    def test_additions_stay_in_envelope_with_finite_uvs_and_outward_winding(self):
        self.assertEqual(bounds(self.generated), bounds(self.original))
        self.assertGreater(self.stats['added_triangles'], 0)
        for face in self.generated.faces[len(self.original.faces):]:
            vertices = [self.generated.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
            normal = model.cross(model.sub(vertices[1][:3], vertices[0][:3]), model.sub(vertices[2][:3], vertices[0][:3]))
            lighting = tuple(sum(vertex[axis] for vertex in vertices) for axis in (5, 6, 7))
            self.assertGreater(model.dot(normal, normal), 1e-14)
            self.assertGreater(model.dot(normal, lighting), 0)
            for vertex in vertices:
                self.assertTrue(all(math.isfinite(value) for value in vertex))
                self.assertTrue(.02 - 1e-12 <= vertex[3] <= .99 + 1e-12)
                self.assertTrue(-.98 - 1e-12 <= vertex[4] <= -.01 + 1e-12)

    def test_roof_is_closed_and_underside_retains_source_uv_mapping(self):
        mesh = model.Mesh(self.original.header, list(self.original.vertices), [])
        self.cbo._roof_shell(mesh, self.original)
        self.assertTrue(all(count == 2 for count in edge_counts(
            mesh, [self.original.faces[i] for i in self.cbo.ROOF_FACES]).values()))
        offset = len(self.original.vertices)
        for face_index in self.cbo.ROOF_FACES:
            samples, normal, _ = shared._triangle(self.original, face_index)
            # Every underside is inside its source plane with meaningful
            # separation, including the almost vertical rear return.
            self.assertLess(model.dot(normal, self.cbo.ROOF_OFFSET), -.02)
            for point, uv in samples:
                vertex = mesh.vertices[offset]
                self.assertEqual(vertex[:3], model.add(point, self.cbo.ROOF_OFFSET))
                self.assertEqual(vertex[3:5], uv)
                offset += 1

    def test_sign_has_unlettered_reverse_and_closed_perimeter(self):
        mesh = model.Mesh(self.original.header, list(self.original.vertices), [])
        self.cbo._sign_backing(mesh, self.original)
        self.assertEqual({vertex[3:5] for vertex in mesh.vertices[len(self.original.vertices):]},
                         {shared.SIGN_BLANK_UV})
        self.assertTrue(all(count == 2 for count in edge_counts(
            mesh, [self.original.faces[i] for i in self.cbo.SIGN_FACES]).values()))

    def test_identity_and_budget_fail_without_changing_input(self):
        before = copy.deepcopy(self.original)
        unchanged, stats = self.cbo.enhance(self.original, model.Mesh, self.names,
                                       triangle_budget=self.stats['added_triangles'] - 1)
        self.assertIs(unchanged, self.original)
        self.assertFalse(any(stats.values()))
        self.assertEqual(self.original, before)
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        self.assertFalse(self.cbo.matches(altered, self.names))
        changed_names = list(self.names)
        changed_names[changed_names.index('DA28COMB')] = 'DA28COM4'
        self.assertFalse(self.cbo.matches(self.original, changed_names))
        for mesh in self.meshes:
            if mesh is not self.original and mesh.header[:7] == b'DA28CBO':
                self.assertFalse(self.cbo.matches(mesh, self.names))


class MegaCbo3Tests(unittest.TestCase):

    cbo = importlib.import_module("mega_cbo3")

    source = staticmethod(lambda short=False: source(3, short))

    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, cls.nodes, cls.meshes, cls.node = cls.source()
        cls.generated, cls.stats = cls.cbo.enhance(cls.original, model.Mesh, cls.names)

    def test_gp_short_are_one_exact_type_with_original_prefix_and_placements(self):
        short, names, nodes, _, node = self.source(True)
        generated, stats = self.cbo.enhance(short, model.Mesh, names)
        self.assertTrue(self.cbo.matches(self.original, self.names) and self.cbo.matches(short, names))
        self.assertEqual(stats, self.stats)
        self.assertEqual(self.generated.vertices, generated.vertices)
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:64], self.original.vertices)
        self.assertEqual(self.generated.faces[:32], self.original.faces)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual((self.stats['cbo3_roof_edges'], self.stats['cbo3_ridge_sections'],
                          self.stats['cbo3_window_frames'], self.stats['cbo3_sign_backings']), (8, 2, 6, 1))
        self.assertEqual(self.nodes[self.node - 1], nodes[node - 1])
        self.assertEqual(model.crc32(self.nodes[self.node - 1]), 'e6ed0399')
        self.assertEqual(struct.unpack_from('<h', self.nodes[self.node], 4)[0], self.node - 1)
        self.assertEqual(struct.unpack_from('<h', nodes[node], 4)[0], node - 1)
        for a, b in zip(self.generated.faces, generated.faces):
            self.assertEqual(a[:8] + a[10:], b[:8] + b[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', a, 8)[0]], names[struct.unpack_from('<h', b, 8)[0]])

    def test_source_material_roles_are_preserved(self):
        self.assertEqual([self.names[struct.unpack_from('<h', face, 8)[0]] for face in self.original.faces],
                         ['DA28COMA'] * 8 + ['DA28PIT3'] * 4 + ['DA28COM4'] * 10 + ['DA28COM9'] * 8 + ['DA02GSD9'] * 2)
        self.assertTrue(all(self.names[struct.unpack_from('<h', face, 8)[0]] in ('DA28COM9', 'DA28COM4', 'DA28PIT3')
                            for face in self.generated.faces[32:]))

    def test_pit_banner_keeps_source_repeat_phase_and_label_positions(self):
        encoded = Image.open(PROJECT / 'assets/models/mega/textures/donington-cbo4-da28pit3.webp').convert('RGB')
        self.assertEqual(encoded.size, (512, 64))
        # Static scenery samples U0..1 through the original .02..99 inset.
        # Compare the effective panel domain, including its physical wrap.
        a, b = (struct.unpack('<f', struct.pack('<f', value))[0] for value in (.02, .99))
        period = (b - a) / 2

        def sample(u, y):
            x = u * encoded.width - .5
            index = math.floor(x)
            fraction = x - index
            left = encoded.getpixel((index % encoded.width, y))
            right = encoded.getpixel(((index + 1) % encoded.width, y))
            return [(1 - fraction) * x + fraction * y for x, y in zip(left, right)]

        errors, joins = [], []
        for y in range(encoded.height):
            for index in range(1024):
                u = a + period * (index + .5) / 1024
                errors.extend(abs(x - z) for x, z in zip(sample(u, y), sample(u + period, y)))
            for index in range(33):
                distance = 3 / encoded.width * index / 32
                joins.extend(abs(x - z) for x, z in zip(sample(b - distance, y), sample(a + period - distance, y)))
                joins.extend(abs(x - z) for x, z in zip(sample(a + distance, y), sample(a + period + distance, y)))
        # Fractional texel positions and lossy WebP permit small differences.
        # The rejected image measures MAE53.3 for repeats and48.8 for joins.
        self.assertLessEqual(sum(errors) / len(errors), 6, 'Both source half-repeats must keep the same artwork phase')
        self.assertLessEqual(math.sqrt(sum(value * value for value in errors) / len(errors)), 12)
        self.assertLessEqual(sum(joins) / len(joins), 3, 'Physical panel join must match the internal repeat')
        self.assertLessEqual(math.sqrt(sum(value * value for value in joins) / len(joins)), 5)
        artwork = encoded.transform((512, 64), Image.Transform.EXTENT,
                                    (a * 512, 0, b * 512, 64), Image.Resampling.BILINEAR)
        # The source's clear label row has white fields at x[7,23),[39,55)
        # in its64px width. At512px this fixes their centers at120 and376.
        spans, start = [], None
        for x in range(513):
            rgb = artwork.getpixel((x, 9)) if x < 512 else (0, 0, 0)
            white = min(rgb) > 175 and max(rgb) - min(rgb) < 50
            if white and start is None:
                start = x
            elif not white and start is not None:
                if x - start > 40:
                    spans.append((start, x))
                start = None
        self.assertEqual(len(spans), 2)
        centers = [(a + b) / 2 for a, b in spans]
        self.assertAlmostEqual(centers[1] - centers[0], 256, delta=2)
        for center, expected in zip(centers, (120, 376)):
            self.assertAlmostEqual(center, expected, delta=12)
        # Reverse-side samples stay in the unlettered top red margin.
        for x in (20, 276):
            red, green, blue = encoded.getpixel((x, 6))
            self.assertGreater(red, green * 1.7)
            self.assertGreater(red, blue * 1.7)

    def test_new_faces_are_real_finite_lit_geometry_within_original_envelope(self):
        old, new = bounds(self.original), bounds(self.generated)
        for axis, tolerance in ((0, .05), (1, .08), (2, .09)):
            self.assertGreaterEqual(new[axis][0], old[axis][0] - tolerance)
            self.assertLessEqual(new[axis][1], old[axis][1] + tolerance)
        self.assertEqual(new[2][1], old[2][1])
        self.assertLess(len(self.generated.vertices), 65536)
        for face in self.generated.faces[32:]:
            vertices = [self.generated.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
            normal = model.cross(model.sub(vertices[1][:3], vertices[0][:3]), model.sub(vertices[2][:3], vertices[0][:3]))
            lighting = tuple(sum(vertex[axis] for vertex in vertices) for axis in (5, 6, 7))
            self.assertGreater(model.dot(normal, normal), 1e-14)
            self.assertGreater(model.dot(normal, lighting), 0)
            for vertex in vertices:
                self.assertTrue(all(math.isfinite(value) for value in vertex))
                self.assertTrue(.02 - 1e-12 <= vertex[3] <= .99 + 1e-12)
                self.assertTrue(-.98 - 1e-12 <= vertex[4] <= -.01 + 1e-12)

    def test_ridge_anchors_and_window_frames_follow_the_source(self):
        ridge = model.Mesh(bytes(48), [], [])
        self.cbo._ridge(ridge, self.original)
        self.assertEqual(len(ridge.faces), 12)
        points = {vertex[:3] for vertex in ridge.vertices}
        for index in (53, 45, 49, 47):
            self.assertIn(model.add(self.original.vertices[index][:3], (0., 0., -.08)), points)
        count = 0
        for face_index in self.cbo.WINDOW_FACES:
            for x0, y0, x1, y1 in shared.WINDOWS:
                mesh = model.Mesh(bytes(48), [], [])
                count += shared._frame(mesh, self.original, (face_index,),
                                       (x0 / 64, y0 / 16 - 1, x1 / 64, y1 / 16 - 1), .35 / 64, .35 / 16, .065)
                samples, normal, _ = shared._triangle(self.original, face_index)
                for vertex in mesh.vertices:
                    distance = model.dot(model.sub(vertex[:3], samples[0][0]), normal)
                    point = model.sub(vertex[:3], model.mul(normal, distance))
                    shared._project(self.original, (face_index,), point)
                    self.assertTrue(abs(distance) < 1e-8 or abs(distance - .065) < 1e-8)
        self.assertEqual(count, 32)

    def test_thin_end_and_rear_fascia_do_not_receive_facade_window_frames(self):
        self.assertEqual(self.cbo.WINDOW_FACES, (16, 17, 19, 20))
        for index in self.cbo.WINDOW_FACES:
            _, normal, _ = shared._triangle(self.original, index)
            self.assertLess(normal[1], -.999)
        for index in (12, 13, 14, 15, 18, 21):
            samples, normal, slot = shared._triangle(self.original, index)
            self.assertEqual(self.names[slot], 'DA28COM4')
            self.assertTrue(normal[0] > .999 or normal[1] > .999)
        for face in self.generated.faces[32:]:
            if self.names[struct.unpack_from('<h', face, 8)[0]] == 'DA28COM4':
                for index in struct.unpack_from('<3H', face, 2):
                    self.assertLess(abs(self.generated.vertices[index][1] + 1.98195), .067)

    def test_four_face_sign_has_one_closed_unlettered_reverse(self):
        mesh = model.Mesh(bytes(48), [], [])
        self.cbo._sign_backing(mesh, self.original)
        self.assertEqual({vertex[3:5] for vertex in mesh.vertices}, {shared.SIGN_BLANK_UV})
        self.assertEqual(len(mesh.faces), 16)
        self.assertGreater(max(vertex[1] for vertex in mesh.vertices) - min(vertex[1] for vertex in mesh.vertices), .069)
        self.assertEqual(len(shared._boundary(self.original, self.cbo.SIGN_FACES)), 6)

    def test_identity_excludes_other_cbo_variants(self):
        for mesh in self.meshes:
            if mesh is not self.original and mesh.header[:7] == b'DA28CBO':
                self.assertFalse(self.cbo.matches(mesh, self.names))
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        self.assertFalse(self.cbo.matches(altered, self.names))


class MegaCbo4Tests(unittest.TestCase):

    cbo = importlib.import_module("mega_cbo4")

    source = staticmethod(lambda short=False: source(4, short))

    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, cls.images, cls.nodes, cls.meshes = cls.source()
        cls.generated, cls.stats = cls.cbo.enhance(cls.original, model.Mesh, cls.names, surface_index=len(cls.names))

    def test_source_identity_gp_short_reuse_and_unchanged_broad_surfaces(self):
        short, names, _, nodes, _ = self.source(True)
        generated, stats = self.cbo.enhance(short, model.Mesh, names)
        self.assertEqual(self.stats, stats)
        self.assertEqual(self.generated.vertices, generated.vertices)
        for gp_face, short_face in zip(self.generated.faces, generated.faces):
            self.assertEqual(gp_face[:8] + gp_face[10:], short_face[:8] + short_face[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', gp_face, 8)[0]], names[struct.unpack_from('<h', short_face, 8)[0]])
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:59], self.original.vertices)
        self.assertEqual(self.generated.faces[:30], self.original.faces)
        self.assertEqual(self.nodes[32], nodes[34])
        self.assertEqual(model.crc32(self.nodes[32]), '641c448e')
        self.assertEqual(struct.unpack_from('<h', self.nodes[33], 4)[0], 32)
        self.assertEqual(struct.unpack_from('<h', nodes[35], 4)[0], 34)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual((self.stats['cbo4_roof_edges'],
                          self.stats['cbo4_ridge_sections'], self.stats['cbo4_window_frames'], self.stats['cbo4_sign_backings']),
                         (8, 2, 6, 1))

    def test_real_geometry_is_finite_nondegenerate_outward_and_uses_only_known_images(self):
        for face in self.generated.faces[30:]:
            self.assertIn(self.names[struct.unpack_from('<h', face, 8)[0]], ('DA28COM9', 'DA28COM4', 'DA28PIT3'))
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

    def test_source_ground_skew_and_tight_silhouette_envelope_are_preserved(self):
        old, new = bounds(self.original), bounds(self.generated)
        for axis, tolerance in ((0, .05), (1, .08), (2, .09)):
            self.assertGreaterEqual(new[axis][0], old[axis][0] - tolerance)
            self.assertLessEqual(new[axis][1], old[axis][1] + tolerance)
        self.assertEqual(new[2][1], old[2][1])
        self.assertGreater(old[2][1], 1.4, 'Original CBO4 feet are not a flat zero plane')
        self.assertAlmostEqual(old[2][0] - new[2][0], .08, places=9)
        self.assertGreater(len({round(vertex[1], 4) for vertex in self.generated.vertices[59:]}), 30)
        # Ambiguous facade art and the shared rear-wall atlas remain solely
        # their original source surfaces; this package invents no new objects.
        self.assertEqual(self.generated.faces[8:16], self.original.faces[8:16])
        self.assertEqual(self.generated.faces[28:30], self.original.faces[28:30])

    def test_roof_and_window_landmarks_are_measured_in_canonical_pixel_data(self):
        facade = rgb_rows(self.images['DA28COM4'])
        pale = []
        for x in range(64):
            color = tuple((facade[7][x][channel] + facade[8][x][channel]) / 2 for channel in range(3))
            if max(color) - min(color) < 35 and sum(color) > 200:
                pale.append(x)
        groups = []
        for x in pale:
            if not groups or x != groups[-1][-1] + 1:
                groups.append([])
            groups[-1].append(x)
        self.assertEqual([(group[0], group[-1] + 1) for group in groups], [(int(a), int(c)) for a, _, c, _ in self.cbo.WINDOWS])

    def assert_attachment(self, mesh, face_index, height):
        for vertex in mesh.vertices:
            point, normal = source_point(self.original, face_index, vertex[3:5])
            offset = model.sub(vertex[:3], point)
            distance = model.dot(offset, normal)
            self.assertLess(math.dist(offset, model.mul(normal, distance)), 1e-8)
            self.assertTrue(abs(distance) < 1e-8 or abs(distance - height) < 1e-8)

    def test_window_panels_follow_both_mirrored_source_maps(self):
        for face_index in self.cbo.FRONT_WINDOW_FACES:
            for x0, y0, x1, y1 in self.cbo.WINDOWS:
                mesh = model.Mesh(bytes(48), [], [])
                self.cbo._window_panel(mesh, self.original, (face_index,), (x0 / 64, y0 / 16 - 1, x1 / 64, y1 / 16 - 1))
                self.assert_attachment(mesh, face_index, .065)
        # Source COM4 right front is genuinely mirrored relative to the left.
        self.assertGreater(self.original.vertices[37][3], self.original.vertices[36][3])
        self.assertGreater(self.original.vertices[37][3], self.original.vertices[40][3])

    def test_roof_perimeter_and_ridge_keep_source_silhouette(self):
        self.assertEqual(len(self.cbo._boundary(self.original, range(8))), 8)
        mesh = model.Mesh(bytes(48), [], [])
        self.cbo._ridge(mesh, self.original)
        self.assertEqual(len(mesh.faces), 12)
        self.assertEqual({struct.unpack_from('<h', face, 8)[0] for face in mesh.faces},
                         {struct.unpack_from('<h', self.original.faces[0], 8)[0]})

    def test_sign_is_closed_with_solid_depth_and_unlettered_red_reverse(self):
        mesh = model.Mesh(bytes(48), [], [])
        self.cbo._sign_backing(mesh, self.original)
        self.assertEqual(len(mesh.faces), 10)
        self.assertEqual({vertex[3:5] for vertex in mesh.vertices}, {self.cbo.SIGN_BLANK_UV})
        self.assertTrue(any(vertex[6] > .99 for vertex in mesh.vertices))
        self.assertGreater(max(vertex[1] for vertex in mesh.vertices) - min(vertex[1] for vertex in mesh.vertices), .069)
        pit = rgb_rows(self.images['DA28PIT3'])
        # All four source texels around the reverse's bilinear sample are red
        # blank margin, so the rear cannot acquire duplicated lettering.
        for y in (0, 1):
            for x in (2, 3):
                red, green, blue = pit[y][x]
                self.assertGreater(red, green * 1.7)
                self.assertGreater(red, blue * 1.7)

    def test_type_selection_excludes_other_shared_material_consumers(self):
        other_cbos = [mesh for mesh in self.meshes if mesh.header[:7] == b'DA28CBO' and mesh is not self.original]
        self.assertEqual(len(other_cbos), 5)
        for mesh in other_cbos:
            self.assertFalse(self.cbo.matches(mesh, self.names))
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        rebound = copy.deepcopy(self.original)
        rebound.faces[0] = rebound.faces[0][:8] + struct.pack('<h', 35) + rebound.faces[0][10:]
        renamed = list(self.names)
        renamed[31] = 'UNRELATED'
        for mesh, names in ((altered, self.names), (rebound, self.names), (self.original, renamed)):
            result, counts = self.cbo.enhance(mesh, model.Mesh, names)
            self.assertIs(result, mesh)
            self.assertEqual(counts['added_triangles'], 0)


class MegaCbo5Tests(unittest.TestCase):

    cbo = importlib.import_module("mega_cbo5")

    source = staticmethod(lambda short=False: source(5, short))

    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, cls.nodes, cls.meshes, cls.node = cls.source()
        cls.generated, cls.stats = cls.cbo.enhance(cls.original, model.Mesh, cls.names)

    def test_gp_short_are_one_exact_type_with_original_prefix_and_placements(self):
        short, names, nodes, _, node = self.source(True)
        generated, stats = self.cbo.enhance(short, model.Mesh, names)
        self.assertTrue(self.cbo.matches(self.original, self.names) and self.cbo.matches(short, names))
        self.assertEqual(stats, self.stats)
        self.assertEqual(self.generated.vertices, generated.vertices)
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:60], self.original.vertices)
        self.assertEqual(self.generated.faces[:32], self.original.faces)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual((self.stats['cbo5_roof_edges'],
                          self.stats['cbo5_ridge_sections'], self.stats['cbo5_window_frames'],
                          self.stats['cbo5_sign_backings']), (8, 2, 6, 1))
        self.assertEqual(self.nodes[self.node - 1], nodes[node - 1])
        self.assertEqual(model.crc32(self.nodes[self.node - 1]), 'c4a6a8b7')
        self.assertEqual(struct.unpack_from('<h', self.nodes[self.node], 4)[0], self.node - 1)
        self.assertEqual(struct.unpack_from('<h', nodes[node], 4)[0], node - 1)
        for a, b in zip(self.generated.faces, generated.faces):
            self.assertEqual(a[:8] + a[10:], b[:8] + b[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', a, 8)[0]], names[struct.unpack_from('<h', b, 8)[0]])

    def test_source_material_roles_are_preserved(self):
        self.assertEqual([self.names[struct.unpack_from('<h', face, 8)[0]] for face in self.original.faces],
                         ['DA28COMA'] * 8 + ['DA28COM9'] * 8 + ['DA28COM4'] * 10 + ['DA28PIT3'] * 4 + ['DA02GSD9'] * 2)
        self.assertTrue(all(self.names[struct.unpack_from('<h', face, 8)[0]] in ('DA28COM9', 'DA28COM4', 'DA28PIT3')
                            for face in self.generated.faces[32:]))

    def test_new_faces_are_real_finite_lit_geometry_within_original_envelope(self):
        old, new = bounds(self.original), bounds(self.generated)
        for axis, tolerance in ((0, .05), (1, .08), (2, .09)):
            self.assertGreaterEqual(new[axis][0], old[axis][0] - tolerance)
            self.assertLessEqual(new[axis][1], old[axis][1] + tolerance)
        self.assertEqual(new[2][1], old[2][1])
        self.assertLess(len(self.generated.vertices), 65536)
        for face in self.generated.faces[32:]:
            vertices = [self.generated.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
            normal = model.cross(model.sub(vertices[1][:3], vertices[0][:3]), model.sub(vertices[2][:3], vertices[0][:3]))
            lighting = tuple(sum(vertex[axis] for vertex in vertices) for axis in (5, 6, 7))
            self.assertGreater(model.dot(normal, normal), 1e-14)
            self.assertGreater(model.dot(normal, lighting), 0)
            for vertex in vertices:
                self.assertTrue(all(math.isfinite(value) for value in vertex))
                self.assertTrue(.02 - 1e-12 <= vertex[3] <= .99 + 1e-12)
                self.assertTrue(-.98 - 1e-12 <= vertex[4] <= -.01 + 1e-12)

    def test_window_panels_project_to_their_source_triangles(self):
        count = 0
        for face_index in self.cbo.WINDOW_FACES:
            for x0, y0, x1, y1 in shared.WINDOWS:
                mesh = model.Mesh(bytes(48), [], [])
                count += shared._window_panel(mesh, self.original, (face_index,),
                                       (x0 / 64, y0 / 16 - 1, x1 / 64, y1 / 16 - 1))
                samples, normal, _ = shared._triangle(self.original, face_index)
                for vertex in mesh.vertices:
                    distance = model.dot(model.sub(vertex[:3], samples[0][0]), normal)
                    point = model.sub(vertex[:3], model.mul(normal, distance))
                    shared._project(self.original, (face_index,), point)
                    self.assertTrue(abs(distance) < 1e-8 or abs(distance - .065) < 1e-8)
        self.assertEqual(count, 10)

    def test_four_face_sign_has_one_closed_unlettered_reverse(self):
        mesh = model.Mesh(bytes(48), [], [])
        self.cbo._sign_backing(mesh, self.original)
        self.assertEqual({vertex[3:5] for vertex in mesh.vertices}, {shared.SIGN_BLANK_UV})
        self.assertEqual(len(mesh.faces), 16)
        self.assertGreater(max(vertex[1] for vertex in mesh.vertices) - min(vertex[1] for vertex in mesh.vertices), .069)
        self.assertEqual(len(shared._boundary(self.original, self.cbo.SIGN_FACES)), 6)

    def test_identity_excludes_other_cbo_variants(self):
        for mesh in self.meshes:
            if mesh is not self.original and mesh.header[:7] == b'DA28CBO':
                self.assertFalse(self.cbo.matches(mesh, self.names))
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        self.assertFalse(self.cbo.matches(altered, self.names))


class MegaCbo6Tests(unittest.TestCase):

    cbo = importlib.import_module("mega_cbo6")

    source = staticmethod(lambda short=False: source(6, short))

    @classmethod
    def setUpClass(cls):
        cls.original, cls.names, cls.nodes, cls.meshes, cls.node = cls.source()
        cls.generated, cls.stats = cls.cbo.enhance(cls.original, model.Mesh, cls.names)

    def test_gp_short_are_one_exact_type_with_original_prefix_and_placements(self):
        short, names, nodes, _, node = self.source(True)
        generated, stats = self.cbo.enhance(short, model.Mesh, names)
        self.assertTrue(self.cbo.matches(self.original, self.names) and self.cbo.matches(short, names))
        self.assertEqual(stats, self.stats)
        self.assertEqual(self.generated.vertices, generated.vertices)
        self.assertEqual(self.generated.header, self.original.header)
        self.assertEqual(self.generated.vertices[:63], self.original.vertices)
        self.assertEqual(self.generated.faces[:34], self.original.faces)
        self.assertGreater(self.stats['added_triangles'], 0)
        self.assertEqual((self.stats['cbo6_roof_edges'],
                          self.stats['cbo6_ridge_sections'], self.stats['cbo6_window_frames'],
                          self.stats['cbo6_sign_backings']), (8, 2, 6, 1))
        self.assertEqual(self.nodes[self.node - 1], nodes[node - 1])
        self.assertEqual(model.crc32(self.nodes[self.node - 1]), '9ae82b9e')
        self.assertEqual(struct.unpack_from('<h', self.nodes[self.node], 4)[0], self.node - 1)
        self.assertEqual(struct.unpack_from('<h', nodes[node], 4)[0], node - 1)
        for a, b in zip(self.generated.faces, generated.faces):
            self.assertEqual(a[:8] + a[10:], b[:8] + b[10:])
            self.assertEqual(self.names[struct.unpack_from('<h', a, 8)[0]], names[struct.unpack_from('<h', b, 8)[0]])

    def test_source_material_roles_are_preserved(self):
        self.assertEqual([self.names[struct.unpack_from('<h', face, 8)[0]] for face in self.original.faces],
                         ['DA28COM9'] * 8 + ['DA28COMA'] * 8 + ['DA28PIT3'] * 6 + ['DA28COM4'] * 4 + ['DA02GSD9'] * 8)
        self.assertTrue(all(self.names[struct.unpack_from('<h', face, 8)[0]] in ('DA28COM9', 'DA28COM4', 'DA28PIT3')
                            for face in self.generated.faces[34:]))

    def test_new_faces_are_real_finite_lit_geometry_within_original_envelope(self):
        old, new = bounds(self.original), bounds(self.generated)
        for axis, tolerance in ((0, .05), (1, .08), (2, .09)):
            self.assertGreaterEqual(new[axis][0], old[axis][0] - tolerance)
            self.assertLessEqual(new[axis][1], old[axis][1] + tolerance)
        self.assertEqual(new[2][1], old[2][1])
        self.assertLess(len(self.generated.vertices), 65536)
        for face in self.generated.faces[34:]:
            vertices = [self.generated.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
            normal = model.cross(model.sub(vertices[1][:3], vertices[0][:3]), model.sub(vertices[2][:3], vertices[0][:3]))
            lighting = tuple(sum(vertex[axis] for vertex in vertices) for axis in (5, 6, 7))
            self.assertGreater(model.dot(normal, normal), 1e-14)
            self.assertGreater(model.dot(normal, lighting), 0)
            for vertex in vertices:
                self.assertTrue(all(math.isfinite(value) for value in vertex))
                self.assertTrue(.02 - 1e-12 <= vertex[3] <= .99 + 1e-12)
                self.assertTrue(-.98 - 1e-12 <= vertex[4] <= -.01 + 1e-12)

    def test_window_panels_project_to_their_source_triangles(self):
        count = 0
        for face_index in self.cbo.WINDOW_FACES:
            for x0, y0, x1, y1 in shared.WINDOWS:
                mesh = model.Mesh(bytes(48), [], [])
                count += shared._window_panel(mesh, self.original, (face_index,),
                                       (x0 / 64, y0 / 16 - 1, x1 / 64, y1 / 16 - 1))
                samples, normal, _ = shared._triangle(self.original, face_index)
                for vertex in mesh.vertices:
                    distance = model.dot(model.sub(vertex[:3], samples[0][0]), normal)
                    point = model.sub(vertex[:3], model.mul(normal, distance))
                    shared._project(self.original, (face_index,), point)
                    self.assertTrue(abs(distance) < 1e-8 or abs(distance - .065) < 1e-8)
        self.assertEqual(count, 10)

    def test_six_face_sign_preserves_four_triangle_front_and_end_return(self):
        front = (16, 17, 20, 21)
        for index in front:
            corners = struct.unpack_from('<3H', self.original.faces[index], 2)
            self.assertTrue(all(abs(self.original.vertices[corner][1] + 5.198) < .001 for corner in corners))
        for index in (18, 19):
            corners = struct.unpack_from('<3H', self.original.faces[index], 2)
            self.assertTrue(all(self.original.vertices[corner][0] > 14.9 for corner in corners))
        mesh = model.Mesh(bytes(48), [], [])
        self.cbo._sign_backing(mesh, self.original)
        self.assertEqual({vertex[3:5] for vertex in mesh.vertices}, {shared.SIGN_BLANK_UV})
        self.assertEqual(len(mesh.faces), 16)
        self.assertGreater(max(vertex[1] for vertex in mesh.vertices) - min(vertex[1] for vertex in mesh.vertices), .069)
        self.assertEqual(len(shared._boundary(self.original, self.cbo.SIGN_FACES)), 6)

    def test_identity_excludes_other_cbo_variants(self):
        for mesh in self.meshes:
            if mesh is not self.original and mesh.header[:7] == b'DA28CBO':
                self.assertFalse(self.cbo.matches(mesh, self.names))
        altered = copy.deepcopy(self.original)
        altered.vertices[0] = (altered.vertices[0][0] + .01, *altered.vertices[0][1:])
        self.assertFalse(self.cbo.matches(altered, self.names))


if __name__ == '__main__':
    unittest.main()
