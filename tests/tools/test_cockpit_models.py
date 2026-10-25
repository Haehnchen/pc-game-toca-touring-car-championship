#!/usr/bin/env python3
"""Shared cockpit solids retain canonical pivots, bindings and silhouette."""

import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
from git_game_data import configured_data_root
import cockpit_geometry as geometry
from vehicle_model import add, crc32, cross, decode, dot, sub
from vehicle_textures import texture_records


class SharedCockpitGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        archive = (configured_data_root() / 'data/honddasx.bfp').read_bytes()
        _, table_size = struct.unpack_from('<II', archive)
        cls.sources, cls.outputs, cls.models, cls.stats = {}, {}, {}, {}
        for role, member in (('wiper', 3), ('wheel', 4)):
            _, offset, size = struct.unpack_from('<16sII', archive, 12 + member * 24)
            source = archive[12 + table_size + offset:12 + table_size + offset + size]
            cls.sources[role] = source
            cls.outputs[role], cls.stats[role] = geometry.generate(source, role)
            cls.models[role] = decode(cls.outputs[role])

    def test_original_hierarchy_pivots_and_material_prefix_are_exact(self):
        for role in self.sources:
            original = decode(self.sources[role])
            generated = self.models[role]
            self.assertEqual(generated[0][:20], original[0][:20])
            self.assertEqual(generated[1], original[1])
            self.assertEqual(generated[3], original[3])
            self.assertEqual([m.header[:12] for m in generated[2]], [m.header[:12] for m in original[2]])
            self.assertEqual(geometry.generate(self.sources[role], role)[0], self.outputs[role])
            names = [name for name, _, _ in texture_records(generated[3], 1)]
            self.assertEqual(names, ['wheel01' if role == 'wheel' else 'wiper'])
            self.assertEqual(self.stats[role]['source_model_crc32'], crc32(self.sources[role]))

    def test_quantized_details_have_real_area_normals_and_original_texture_binding(self):
        for role, (_, _, meshes, _) in self.models.items():
            for mesh in meshes:
                self.assertTrue(all(math.isfinite(value) for vertex in mesh.vertices for value in vertex))
                for face in mesh.faces:
                    indices = struct.unpack_from('<3H', face, 2)
                    self.assertLess(max(indices), len(mesh.vertices))
                    a, b, c = [mesh.vertices[i] for i in indices]
                    normal = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
                    self.assertGreater(dot(normal, normal), 1e-16, role)
                    self.assertGreater(dot(normal, add(add(a[5:], b[5:]), c[5:])), 0, role)
                    self.assertEqual(struct.unpack_from('<h', face, 8)[0], 0)
                    self.assertEqual(face[10:14], b'\xff' * 4)
                self.assertTrue(all(0 <= v[3] <= 1 and -1 <= v[4] <= 0 for v in mesh.vertices))

    def test_final_artwork_binds_every_physical_surface_opaquely(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow artwork inspection unavailable')
        directory = PROJECT / 'assets/models/cockpits/shared/textures'
        if not all((directory / f'{name}.webp').exists() for name in ('wheel01', 'wiper')):
            self.skipTest('Final shared cockpit artwork unavailable')
        for role, name in (('wheel', 'wheel01'), ('wiper', 'wiper')):
            output, _ = geometry.generate(self.sources[role], role, directory)
            header, _, meshes, textures = decode(output)
            records = texture_records(textures, struct.unpack_from('<H', header, 16)[0])
            self.assertEqual([(n, data) for n, _, data in records], [(name, (directory / f'{name}.webp').read_bytes())])
            with Image.open(directory / f'{name}.webp') as source:
                image = source.convert('RGBA')
            width, height = image.size
            # Holes have no faces. Every remaining physical face must sample
            # visible material, especially drilled walls and wiper edge rails.
            for face in meshes[0].faces:
                vertices = [meshes[0].vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                u = sum(v[3] for v in vertices) / 3
                v = sum(v[4] for v in vertices) / 3 + 1
                pixel = (round(u * (width - 1)), round(v * (height - 1)))
                self.assertGreaterEqual(image.getpixel(pixel)[3], 128, (role, pixel))

    def test_wheel_has_rounded_rim_and_six_physical_spoke_holes(self):
        mesh = self.models['wheel'][2][0]
        self.assertGreater(len(mesh.faces), 2500)
        self.assertLess(len(mesh.faces), 4000)
        # The first64x12 tube cells surround the same original pivot; each
        # section has a curved26mm cross-section rather than a flat decal.
        section = mesh.vertices[:13]
        self.assertGreater(max(v[0] for v in section) - min(v[0] for v in section), .050)
        self.assertGreater(max(v[1] for v in section) - min(v[1] for v in section), .049)
        original = decode(self.sources['wheel'])[2][0]
        for axis in range(3):
            self.assertGreaterEqual(min(v[axis] for v in mesh.vertices), min(v[axis] for v in original.vertices) - .028)
            self.assertLessEqual(max(v[axis] for v in mesh.vertices), max(v[axis] for v in original.vertices) + .028)
        # Probe holes in the three source spokes against every front-facing
        # triangle: real holes remain open even without texture alpha.
        center_y = sum(v[1] for v in original.vertices) / 4
        vertical = (original.vertices[0][1] - original.vertices[3][1]) / (2 * original.vertices[1][2])
        def covered(q, z):
            y = center_y + q * vertical
            for face in mesh.faces:
                a, b, c = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                den = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1])
                if abs(den) < 1e-12:
                    continue
                u = ((y - a[1]) * (c[2] - a[2]) - (z - a[2]) * (c[1] - a[1])) / den
                v = ((b[1] - a[1]) * (z - a[2]) - (b[2] - a[2]) * (y - a[1])) / den
                if min(u, v, 1 - u - v) >= -1e-7:
                    return True
            return False
        for angle in (math.pi, math.pi / 2, math.pi * 1.5):
            for radius in (.103, .168):
                self.assertFalse(covered(radius * math.cos(angle), radius * math.sin(angle)))
        self.assertTrue(covered(-.16, .025))
        self.assertTrue(covered(0, 0))

    def test_wiper_frame_windows_and_original_envelope(self):
        original = decode(self.sources['wiper'])[2][0]
        mesh = self.models['wiper'][2][0]
        self.assertGreater(len(mesh.faces), 150)
        self.assertLess(len(mesh.faces), 250)
        for axis in range(3):
            self.assertGreaterEqual(min(v[axis] for v in mesh.vertices), min(v[axis] for v in original.vertices) - 1 / 65536)
            self.assertLessEqual(max(v[axis] for v in mesh.vertices), max(v[axis] for v in original.vertices) + 1 / 65536)
        # Every new frame corner lies on an original front/back station cage.
        # Windows are geometric: no remaining face spans their central strip.
        upper = [original.vertices[i] for i in (5, 6, 9, 2)]
        lower = [original.vertices[i] for i in (8, 7, 10, 11)]
        for station in range(3):
            for s in (.25, .75):
                high = tuple(a + (b - a) * s for a, b in zip(upper[station], upper[station + 1]))
                low = tuple(a + (b - a) * s for a, b in zip(lower[station], lower[station + 1]))
                target = tuple((a + b) / 2 for a, b in zip(high[:3], low[:3]))
                for face in mesh.faces:
                    vertices = [mesh.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
                    # A barycentric3D point check avoids mistaking the far side
                    # of the framed original hull for a filled front window.
                    a, b, c = vertices
                    ab, ac, ap = sub(b, a), sub(c, a), sub(target, a)
                    normal = cross(ab, ac)
                    if abs(dot(ap, normal)) > 2e-6 * math.sqrt(dot(normal, normal)):
                        continue
                    den = dot(ab, ab) * dot(ac, ac) - dot(ab, ac) ** 2
                    if den <= 1e-16:
                        continue
                    u = (dot(ap, ab) * dot(ac, ac) - dot(ap, ac) * dot(ab, ac)) / den
                    v = (dot(ap, ac) * dot(ab, ab) - dot(ap, ab) * dot(ab, ac)) / den
                    self.assertLess(min(u, v, 1 - u - v), -1e-5)


if __name__ == '__main__':
    unittest.main()
