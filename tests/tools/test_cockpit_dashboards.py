#!/usr/bin/env python3
"""Honda dashboard backing keeps the original front, UVs and static actor."""

from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
import cockpit_dashboard_geometry as geometry
import vehicle_model as model
from vehicle_textures import texture_records


class HondaDashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.definition = json.loads(geometry.DEFINITION.read_text())
        # Honda's stored name has nonzero bytes after NUL. This exact logical
        # member lookup is the source-padding regression, not a direct slice.
        cls.source = model.read_source(cls.definition['source'])
        cls.output, cls.report = geometry.generate(cls.source)
        cls.original = model.decode(cls.source)
        cls.generated = model.decode(cls.output)

    def test_logical_source_gate_and_original_hierarchy_materials_front_skin(self):
        self.assertEqual(self.definition['source']['member_name'], 'cabin\\dash.c3d')
        self.assertEqual(model.crc32(self.source), '68206187')
        source_header, source_nodes, originals, source_textures = self.original
        header, nodes, meshes, textures = self.generated
        self.assertEqual(header[:20], source_header[:20])
        self.assertEqual(nodes, source_nodes)
        self.assertEqual(textures, source_textures)
        self.assertEqual(len(meshes), 4)
        for original, generated in zip(originals, meshes):
            self.assertEqual(generated.vertices[:len(original.vertices)], original.vertices)
            self.assertEqual(generated.faces[:len(original.faces)], original.faces)
        changed = bytearray(self.source)
        changed[-1] ^= 1
        with self.assertRaises(ValueError):
            geometry.generate(bytes(changed))
        with self.assertRaises(ValueError):
            geometry.generate(self.source, car_id=1)

    def test_closed_group_independent_winding_and_source_bound_thickness(self):
        originals, meshes = self.original[2], self.generated[2]
        edges = Counter()
        volume = 0
        source_points = {v[:3] for m in originals for v in m.vertices}
        for original, mesh in zip(originals, meshes):
            for face in mesh.faces:
                vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                a, b, c = vertices
                normal = model.cross(model.sub(b[:3], a[:3]), model.sub(c[:3], a[:3]))
                self.assertGreater(model.dot(normal, normal), 1e-16)
                self.assertGreater(model.dot(normal, tuple(sum(v[k] for v in vertices) for k in (5, 6, 7))), 0)
                self.assertIn(struct.unpack_from('<h', face, 8)[0], range(4))
                points = [tuple(round(x * 65536) for x in v[:3]) for v in vertices]
                edges.update(tuple(sorted((x, y))) for x, y in zip(points, points[1:] + points[:1]))
                volume += model.dot(a[:3], model.cross(b[:3], c[:3])) / 6
            for vertex in mesh.vertices[len(original.vertices):]:
                self.assertTrue(all(math.isfinite(x) for x in vertex))
                self.assertLessEqual(min(math.dist(vertex[:3], p) for p in source_points), .008 + 2 / 65536)
        self.assertEqual(set(edges.values()), {2})
        self.assertGreater(volume, 0)
        self.assertAlmostEqual(self.report['backing_thickness_m'], .008)
        # Two original point-only joins must not become four-use extrusion
        # edges. Actual source edge-connected material seams still share backs.
        self.assertEqual({tuple(p['source_position_16_16']) for p in self.report['source_point_fans']},
                         {(67098, -59852, -12942), (35820, -46289, -35820)})
        for point in self.report['source_point_fans']:
            self.assertEqual(len(point['fans']), 2)
            self.assertEqual(len({tuple(f['back_position']) for f in point['fans']}), 2)

    def test_backing_and_edge_charts_use_only_corresponding_source_uvs(self):
        for original, generated, detail in zip(self.original[2], self.generated[2], self.report['mesh_details']):
            lo, hi = detail['back_face_range']
            for source_face, face in zip(original.faces, generated.faces[lo:hi]):
                source_uvs = Counter(original.vertices[i][3:5] for i in struct.unpack_from('<3H', source_face, 2))
                self.assertEqual(Counter(generated.vertices[i][3:5] for i in struct.unpack_from('<3H', face, 2)), source_uvs)
                self.assertEqual(struct.unpack_from('<h', face, 8)[0], struct.unpack_from('<h', source_face, 8)[0])
            used_uvs = {v[3:5] for v in original.vertices}
            for fi in detail['wall_faces']:
                self.assertTrue(all(generated.vertices[i][3:5] in used_uvs for i in struct.unpack_from('<3H', generated.faces[fi], 2)))

    def test_authored_dashboard_images_embed_verbatim_and_remain_opaque(self):
        directory = PROJECT / 'assets/models/cockpits/honda/textures'
        names = ['dashc', 'dash', 'dasha', 'dashb']
        if not all((directory / f'{name}.webp').is_file() for name in names):
            self.skipTest('Final Honda dashboard artwork unavailable')
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow artwork inspection unavailable')
        output, _ = geometry.generate(self.source, texture_directory=directory)
        header, nodes, meshes, section = model.decode(output)
        self.assertEqual(nodes, self.generated[1])
        self.assertEqual([m.encode() for m in meshes], [m.encode() for m in self.generated[2]])
        records = texture_records(section, struct.unpack_from('<H', header, 16)[0])
        self.assertEqual([(name, payload) for name, _, payload in records], [(name, (directory / f'{name}.webp').read_bytes()) for name in names])
        for name in names:
            with Image.open(directory / f'{name}.webp') as image:
                self.assertEqual(image.convert('RGBA').getchannel('A').getextrema(), (255, 255))


if __name__ == '__main__':
    unittest.main()
