#!/usr/bin/env python3
"""HOR mesh identity, volumetric shape, source paint and animation contracts."""

import importlib.util
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
from unittest import mock

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
from git_game_data import configured_data_root
import mega_aircraft as aircraft
import vehicle_model as geometry

import vehicle_model as MODEL


def plane(name, helicopter=False):
    mesh = MODEL.Mesh(name.encode().ljust(48, b'\0'), [], [])
    points = ((-3., -3., -2.3), (3., -3., -2.3), (3., 3., -2.3), (-3., 3., -2.3)) if helicopter \
        else ((0., -3., -12.), (0., 6., -12.), (0., 6., -8.), (0., -3., -8.))
    normal = (0., 0., -1.) if helicopter else (-1., 0., 0.)
    geometry.polygon(mesh, points, (255, 255, 255, 255), normal,
                     ((0., -1.), (1., -1.), (1., 0.), (0., 0.)), 0)
    return mesh


def validate(test, mesh, slots):
    test.assertLessEqual(max(len(mesh.vertices), len(mesh.faces)), 65535)
    test.assertLessEqual(len(mesh.faces), 5000)
    for vertex in mesh.vertices:
        test.assertTrue(all(math.isfinite(component) for component in vertex))
        test.assertAlmostEqual(sum(component ** 2 for component in vertex[5:]), 1., places=5)
        test.assertGreaterEqual(vertex[3], 0.)
        test.assertLessEqual(vertex[3], 1. + 1 / 65536)
        test.assertGreaterEqual(vertex[4], -1.)
        test.assertLessEqual(vertex[4], 1 / 65536)
    for face in mesh.faces:
        texture = struct.unpack_from('<h', face, 8)[0]
        test.assertTrue(texture == -1 or 0 <= texture < slots)
        indices = struct.unpack_from('<3H', face, 2)
        test.assertLess(max(indices), len(mesh.vertices))
        a, b, c = (mesh.vertices[i] for i in indices)
        normal = geometry.cross(geometry.sub(b[:3], a[:3]), geometry.sub(c[:3], a[:3]))
        test.assertGreater(geometry.dot(normal, normal), 1e-18)
        averaged = geometry.add(geometry.add(a[5:], b[5:]), c[5:])
        test.assertGreater(geometry.dot(normal, averaged), 0.)


def emitted_triangles(mesh, start=0):
    return [tuple(mesh.vertices[index][:3] for index in struct.unpack_from('<3H', face, 2))
            for face in mesh.faces[start:]]


def contains_point(triangles, point):
    """Odd/even solid containment from the actual mesh, casting along +X."""
    hits = []
    for a, b, c in triangles:
        ab, ac = geometry.sub(b, a), geometry.sub(c, a)
        determinant = ab[1] * ac[2] - ab[2] * ac[1]
        if abs(determinant) < 1e-12:
            continue
        y, z = point[1] - a[1], point[2] - a[2]
        u = (y * ac[2] - z * ac[1]) / determinant
        v = (ab[1] * z - ab[2] * y) / determinant
        if min(u, v, 1 - u - v) < -1e-9:
            continue
        x = a[0] + u * ab[0] + v * ac[0]
        if x > point[0] + 1e-9 and not any(abs(x - hit) < 1e-8 for hit in hits):
            hits.append(x)
    return len(hits) % 2 == 1


def inboard_cap_samples(triangles, center_x):
    # End caps face laterally; the airfoil skins face predominantly up/down.
    caps = []
    for triangle in triangles:
        a, b, c = triangle
        normal = geometry.cross(geometry.sub(b, a), geometry.sub(c, a))
        if normal[0] ** 2 > .9 * geometry.dot(normal, normal):
            distance = abs(sum(p[0] for p in triangle) / 3 - center_x)
            caps.append((distance, triangle))
    assert caps, 'The wing must have a closed inboard airfoil cap'
    boundary = (min(distance for distance, _ in caps) + max(distance for distance, _ in caps)) / 2
    cap = [triangle for distance, triangle in caps if distance < boundary]
    points = [point for triangle in cap for point in triangle]
    start, end = min(p[1] for p in points), max(p[1] for p in points)
    for fraction in (.2, .5, .8):
        y = start + fraction * (end - start)
        crossings = []
        for triangle in cap:
            for a, b in zip(triangle, triangle[1:] + triangle[:1]):
                if (a[1] <= y < b[1]) or (b[1] <= y < a[1]):
                    crossings.append(geometry.interpolate(a, b, (y - a[1]) / (b[1] - a[1])))
        assert crossings, 'The inboard cap must span the airfoil chord'
        # Sample halfway between its actual upper and lower airfoil surfaces.
        yield (sum(p[0] for p in crossings) / len(crossings), y,
               (min(p[2] for p in crossings) + max(p[2] for p in crossings)) / 2)


class AircraftTests(unittest.TestCase):
    def test_flat_jet_becomes_volume_with_wings_engines_and_preserved_material_prefix(self):
        original = plane('PSP1_1')
        original_vertices, original_faces = list(original.vertices), list(original.faces)
        mesh, stats = aircraft.enhance(original, MODEL.Mesh, ['PSP1', 'MegaPSPSkin'])
        self.assertEqual(mesh.header, original.header)
        self.assertEqual(original.vertices, original_vertices)
        self.assertEqual(original.faces, original_faces)
        self.assertEqual(stats['rebuilt_meshes'], 1)
        self.assertGreater(len(mesh.faces), 1000)
        self.assertGreater(aircraft.extent(mesh)[0][1] - aircraft.extent(mesh)[0][0], 5.)
        self.assertTrue(any(abs(v[5]) > .5 for v in mesh.vertices))
        self.assertTrue(any(abs(v[7]) > .5 for v in mesh.vertices))
        self.assertEqual({struct.unpack_from('<h', face, 8)[0] for face in mesh.faces}, {1})
        validate(self, mesh, 2)
        # Cached builds must be byte-identical without sharing mutable lists.
        again, _ = aircraft.enhance(original, MODEL.Mesh, ['PSP1', 'MegaPSPSkin'])
        self.assertEqual(mesh.vertices, again.vertices)
        self.assertEqual(mesh.faces, again.faces)
        self.assertIsNot(mesh.vertices, again.vertices)

    def test_psp_atlas_uses_full_fuselage_wing_and_engine_regions_without_geometry_changes(self):
        original = plane('PSP1_1')
        relocated, _ = aircraft.enhance(original, MODEL.Mesh, ['PSP1', 'unused-material', 'MegaPSPSkin'])
        mesh, _ = aircraft.enhance(original, MODEL.Mesh, ['PSP1', 'MegaPSPSkin'])
        self.assertEqual(mesh.header, original.header)
        self.assertEqual(len(mesh.faces), len(relocated.faces))
        self.assertEqual([v[:3] + v[5:] for v in mesh.vertices], [v[:3] + v[5:] for v in relocated.vertices])
        self.assertEqual({struct.unpack_from('<h', f, 8)[0] for f in mesh.faces}, {1})
        body = [v for v in mesh.vertices if v[4] < -.5]
        wings = [v for v in mesh.vertices if v[4] > -.5 and v[3] < .5]
        engines = [v for v in mesh.vertices if v[4] > -.5 and v[3] > .5]
        self.assertGreater(len(body), 1000)
        self.assertGreater(len(wings), 400)
        self.assertGreater(len(engines), 200)
        self.assertGreater(max(v[3] for v in body) - min(v[3] for v in body), .99)
        self.assertGreater(max(v[4] for v in body) - min(v[4] for v in body), .45)
        sections = ((-2.393, -.038, -11., .2, .2), (5.607, -.038, -10., .2, .2))
        paint = aircraft.PSPAtlas(['PSP1', 'MegaPSPSkin'], 'body', sections)
        self.assertEqual(paint.uv((-.038, -2.393, -11.), (1., 0., 0.))[0], 1.)
        self.assertEqual(paint.uv((-.038, 5.607, -10.), (1., 0., 0.))[0], 0.)
        validate(self, mesh, 2)

    @unittest.skipUnless(shutil.which('magick') or shutil.which('convert'), 'ImageMagick is unavailable')
    def test_psp_full_3d_material_atlas_is_opaque(self):
        image = PROJECT / 'assets/models/mega/aircraft-skin.webp'
        if not image.exists():
            self.skipTest('Authored PSP3D material atlas not installed')
        converter = shutil.which('magick') or shutil.which('convert')
        pixels = subprocess.run([converter, str(image), '-depth', '8', 'rgba:-'], check=True, capture_output=True).stdout
        self.assertEqual(len(pixels), 512 * 256 * 4)
        self.assertTrue(all(alpha == 255 for alpha in pixels[3::4]), 'The full3D jet atlas must be opaque')

    def test_mark_lettering_boundary_triangle_is_split_without_uv_warp_or_lost_area(self):
        # This single source triangle crosses all four logo-rectangle borders.
        # World Y/Z equal its original U/V, so the exact expected UV transform
        # of every new corner is independently observable from its position.
        points = ((1., .2, -.65), (1., .85, -.35), (1., .45, -.05))
        source = MODEL.Mesh(bytes(48), [], [])
        geometry.polygon(source, points, (20, 40, 60, 255), (1., 0., 0.), [(p[1], p[2]) for p in points], 0)
        paint = aircraft.Paint(source, ['MARKAM'], readable_mark=True)
        mesh = MODEL.Mesh(source.header, [], [])
        paint.triangle(mesh, points, [(1., 0., 0.)] * 3)
        self.assertGreater(len(mesh.faces), 4)
        u0, u1, v0, v1 = aircraft.MARK_ISLAND
        area, mirrored, original = 0., 0, 0
        for face in mesh.faces:
            vertices = [mesh.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
            center_u = sum(vertex[1] for vertex in vertices) / 3
            center_v = sum(vertex[2] for vertex in vertices) / 3
            inside = u0 < center_u < u1 and v0 < center_v < v1
            mirrored += inside
            original += not inside
            self.assertEqual(tuple(face[10:14]), (20, 40, 60, 255))
            for vertex in vertices:
                expected_u = u0 + u1 - vertex[1] if inside else vertex[1]
                self.assertAlmostEqual(vertex[3], expected_u)
                self.assertAlmostEqual(vertex[4], vertex[2])
                self.assertAlmostEqual(vertex[0], 1.)
                if inside:
                    self.assertGreaterEqual(vertex[1] + 1e-12, u0)
                    self.assertLessEqual(vertex[1] - 1e-12, u1)
                    self.assertGreaterEqual(vertex[2] + 1e-12, v0)
                    self.assertLessEqual(vertex[2] - 1e-12, v1)
            a, b, c = [vertex[:3] for vertex in vertices]
            normal = geometry.cross(geometry.sub(b, a), geometry.sub(c, a))
            area += math.sqrt(geometry.dot(normal, normal)) / 2
        self.assertGreater(mirrored, 0)
        self.assertGreater(original, 0)
        normal = geometry.cross(geometry.sub(points[1], points[0]), geometry.sub(points[2], points[0]))
        self.assertAlmostEqual(area, math.sqrt(geometry.dot(normal, normal)) / 2)
        # Opposite fuselage side keeps the complete original projection.
        reverse = MODEL.Mesh(source.header, [], [])
        paint.triangle(reverse, points, [(-1., 0., 0.)] * 3)
        self.assertEqual(len(reverse.faces), 1)
        self.assertTrue(all(abs(v[3] - v[1]) < 1e-12 and abs(v[4] - v[2]) < 1e-12 for v in reverse.vertices))

    def test_existing_four_rotor_slots_are_distinct_solid_animation_phases(self):
        phases = []
        for phase in range(1, 5):
            original = plane(f'HELI_{phase}', helicopter=True)
            mesh, stats = aircraft.enhance(original, MODEL.Mesh, ['HELI1'], 'rotorcraft')
            self.assertEqual(mesh.header, original.header)
            self.assertEqual(stats['rebuilt_meshes'], 1)
            self.assertGreater(len(mesh.faces), 50)
            self.assertGreater(aircraft.extent(mesh)[2][1] - aircraft.extent(mesh)[2][0], .05)
            validate(self, mesh, 1)
            phases.append(tuple(mesh.vertices))
        self.assertEqual(len(set(phases)), 4)

    def test_original_uv_triangle_interpolation_retains_material_and_ink_layout(self):
        source = plane('paint-only')
        paint = aircraft.Paint(source, ['source-paint'])
        mesh = MODEL.Mesh(source.header, [], [])
        points = ((.05, 0., -11.), (.05, 2., -11.), (.05, 0., -10.))
        paint.triangle(mesh, points, [(-1., 0., 0.)] * 3)
        self.assertEqual(struct.unpack_from('<h', mesh.faces[0], 8)[0], 0)
        for vertex in mesh.vertices:
            self.assertAlmostEqual(vertex[3], (vertex[1] + 3.) / 9.)
            self.assertAlmostEqual(vertex[4], (vertex[2] + 8.) / 4.)

    def test_canonical_fixed_wing_roots_overlap_actual_fuselage_or_central_fin(self):
        source_spec = importlib.util.spec_from_file_location('aircraft_attachment_source', PROJECT / 'tools/build-mega-assets.py')
        sources = importlib.util.module_from_spec(source_spec)
        source_spec.loader.exec_module(sources)
        seen = set()
        for prefix in ('donl', 'thru'):
            raw = sources.members((configured_data_root() / f'data/{prefix}hor.bfp').read_bytes())[0]
            names = [name.split(b'\0', 1)[0].decode('ascii') for name, _ in sources.textures(raw)]
            for original in MODEL.decode(raw)[2]:
                name = original.header[:12].split(b'\0', 1)[0].decode('ascii')
                if name not in ('AIRMARK_1', 'PSP1_1', 'PLANE1R_5', 'PLANE2R_5'):
                    continue
                seen.add(name)
                parts = {kind: [] for kind in ('loft', 'wing', 'fin')}

                def capture(kind, build):
                    def record(mesh, *args, **kwargs):
                        start = len(mesh.faces)
                        build(mesh, *args, **kwargs)
                        parts[kind].append(emitted_triangles(mesh, start))
                    return record

                # Capture only emitted geometry; root and body dimensions come
                # from triangles rather than copying the generator's sections.
                with mock.patch.object(aircraft, '_CACHE', {}), \
                        mock.patch.object(aircraft, 'loft', capture('loft', aircraft.loft)), \
                        mock.patch.object(aircraft, 'wing', capture('wing', aircraft.wing)), \
                        mock.patch.object(aircraft, 'fin', capture('fin', aircraft.fin)):
                    material_names = names + ['MegaPSPSkin'] if name == 'PSP1_1' else names
                    aircraft.enhance(original, MODEL.Mesh, material_names)
                self.assertEqual(len(parts['wing']), 4)
                self.assertEqual(len(parts['fin']), 1)
                body = parts['loft'][0]
                xs = [point[0] for triangle in body for point in triangle]
                center_x = (min(xs) + max(xs)) / 2
                for index, wing in enumerate(parts['wing']):
                    for sample in inboard_cap_samples(wing, center_x):
                        with self.subTest(name=name, wing=index, root=sample):
                            self.assertTrue(contains_point(body, sample) or contains_point(parts['fin'][0], sample),
                                            'The main wing or horizontal tail root must enter the fuselage or central fin')
        self.assertEqual(seen, {'AIRMARK_1', 'PSP1_1', 'PLANE1R_5', 'PLANE2R_5'})

    def test_canonical_aircraft_balloons_and_propeller_families_keep_identity_and_budget(self):
        source_spec = importlib.util.spec_from_file_location('aircraft_source', PROJECT / 'tools/build-mega-assets.py')
        sources = importlib.util.module_from_spec(source_spec)
        source_spec.loader.exec_module(sources)
        seen = set()
        for prefix, member in (('donl', 0), ('thru', 0), ('silv', 1), ('silv', 0), ('crof', 0)):
            archive = configured_data_root() / f'data/{prefix}hor.bfp'
            raw = sources.members(archive.read_bytes())[member]
            meshes = MODEL.decode(raw)[2]
            names = [name.split(b'\0', 1)[0].decode('ascii') for name, _ in sources.textures(raw)]
            for original in meshes:
                name = original.header[:12].split(b'\0', 1)[0].decode('ascii')
                if name in seen:
                    continue
                seen.add(name)
                with self.subTest(name=name):
                    material_names = names + ['MegaPSPSkin'] if name == 'PSP1_1' else names
                    mesh, stats = aircraft.enhance(original, MODEL.Mesh, material_names)
                    self.assertEqual(mesh.header, original.header)
                    self.assertEqual(stats['rebuilt_meshes'], 1)
                    self.assertGreater(len(mesh.faces), len(original.faces))
                    validate(self, mesh, len(material_names))
                    if name == 'HELI_5':
                        tip = [v for v in mesh.vertices if abs(v[1] - 3.719) < 1e-8 and abs(v[2] + .874) < 1e-8]
                        self.assertTrue(tip)
                        u0, v0, u1, v1 = aircraft.HELI_TAIL_SKIN
                        self.assertTrue(all(u0 <= v[3] <= u1 and v0 <= v[4] <= v1 for v in tip),
                                        'Tail fin must sample helicopter paint, excluding the photographed green/grey background')
                    if name in ('CR05BALL_1', 'CR06BALL_1', 'CR13AUDB_1'):
                        old, new = aircraft.extent(original), aircraft.extent(mesh)
                        for axis in range(3):
                            self.assertLess(abs(old[axis][0] - new[axis][0]), .05)
                            self.assertLess(abs(old[axis][1] - new[axis][1]), .05)
        self.assertTrue({'AIRMARK_1', 'PSP1_1', 'PLANE1R_5', 'PLANE2R_5', 'HELI_5',
                         'CR05BALL_1', 'CR06BALL_1', 'CR13AUDB_1'}.issubset(seen))


if __name__ == '__main__':
    unittest.main()
