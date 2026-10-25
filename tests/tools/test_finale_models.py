#!/usr/bin/env python3
"""Canonical Finale actors keep their own wheels, atlas charts and animation."""

from collections import Counter
import copy
import importlib.util
import json
import shutil
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'tools'))
from git_game_data import configured_data_root
import mega_finale as finale
import vehicle_model as model

SPEC = importlib.util.spec_from_file_location('finale_builder', PROJECT / 'tools/build-finale-models.py')
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


class FinaleBundleTests(unittest.TestCase):
    def test_registry_rejects_other_sources_and_escaping_artwork(self):
        document = {'version': 1, 'models': [
            {'role': role, 'source': {'archive': 'data/champfin.bfp',
             'archive_crc32': '28f9b97c', 'member': member, 'member_crc32': checksum},
             'output': f'finale/{role}.glb', 'textures': [
                 {'name': 'TEST', 'image': 'assets/models/finale/textures/test.webp'}]}
            for role, (member, checksum) in builder.SOURCES.items()]}
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'registry.json'
            path.write_text(json.dumps(document))
            self.assertEqual(builder.load_manifest(path), document)
            for field, bad in (('archive_crc32', '00000000'), ('member', 9)):
                changed = copy.deepcopy(document)
                changed['models'][0]['source'][field] = bad
                path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):
                    builder.load_manifest(path)
            changed = copy.deepcopy(document)
            changed['models'][0]['textures'][0]['image'] = 'assets/models/finale/textures/../../escape.webp'
            path.write_text(json.dumps(changed))
            with self.assertRaises(ValueError):
                builder.load_manifest(path)

    def test_bundle_sync_and_disabled_cleanup_preserve_other_models(self):
        (PROJECT / 'build').mkdir(exist_ok=True)
        document = {'models': [{'output': f'finale/{role}.glb'} for role in builder.SOURCES]}
        with tempfile.TemporaryDirectory(dir=PROJECT / 'build') as temporary:
            generated, bundled = (Path(temporary) / name for name in ('generated', 'bundle'))
            for root in (generated, bundled):
                (root / 'finale').mkdir(parents=True)
                (root / 'finale/driver.glb').write_bytes(b'foreign driver')
            for role in builder.SOURCES:
                (generated / f'finale/{role}.glb').write_bytes(role.encode())
            (generated / 'finale-build.json').write_bytes(b'completion marker')
            builder.sync(document, generated, bundled)
            for role in builder.SOURCES:
                self.assertEqual((bundled / f'finale/{role}.glb').read_bytes(), role.encode())
            builder.sync(document, generated, bundled, enabled=False)
            self.assertFalse((generated / 'finale-build.json').exists())
            for root in (generated, bundled):
                self.assertEqual(list((root / 'finale').iterdir()), [root / 'finale/driver.glb'])
                self.assertEqual((root / 'finale/driver.glb').read_bytes(), b'foreign driver')


class FinaleModelsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = {}
        for role, member, name, checksum in (
                ('pace-car', 0, 'cars\\pace\\high\\p', '7076ff0e'),
                ('transporter', 1, 'cars\\tocatrk\\toc', '94b4e25b')):
            raw = model.read_source({'archive': 'data/champfin.bfp',
                                     'archive_crc32': '28f9b97c', 'member': member,
                                     'member_name': name, 'member_crc32': checksum})
            output, report = finale.generate(raw, role)
            cls.cases[role] = (raw, output, report)

    def test_real_exports_record_authored_quality_and_share_duplicate_pace_image(self):
        document = builder.load_manifest()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'data').mkdir()
            shutil.copyfile(configured_data_root() / 'data/champfin.bfp', root / 'data/champfin.bfp')
            with mock.patch.object(model, 'PROJECT', root / 'unavailable-reference'):
                results = builder.build(document, None, data_root=root)
        for entry, (_, data, report) in zip(document['models'], results):
            size = struct.unpack_from('<I', data, 12)[0]
            gltf = json.loads(data[20:20 + size])
            self.assertEqual(gltf['extras'], {'toca_model_version': 1, 'authored_webp_quality': 70})
            self.assertEqual(report['authored_webp_quality'], 70)
            for image in entry['textures']:
                self.assertIn((PROJECT / image['image']).read_bytes(), data)
            if entry['role'] == 'pace-car':
                self.assertEqual(len(gltf['images']), 5)
                self.assertEqual(gltf['images'][0]['bufferView'], gltf['images'][4]['bufferView'])

    def test_canonical_gates_and_source_slots_hierarchy_variants_preserved(self):
        for role, (raw, output, report) in self.cases.items():
            source_header, source_nodes, source_meshes, source_textures = model.decode(raw)
            header, nodes, meshes, textures = model.decode(output)
            self.assertEqual(source_header[:20], header[:20])
            self.assertEqual(source_nodes, nodes)
            self.assertEqual(source_textures, textures)
            records = builder.source_textures(source_textures, struct.unpack_from('<H', source_header, 16)[0])
            self.assertEqual([name for name, _ in records],
                             ['PACE', 'SIDEG', 'WSCRN', 'ENGFLO', 'PACE'] if role == 'pace-car' else ['TRUCK'])
            if role == 'pace-car':
                self.assertEqual(records[0], records[4])
            self.assertEqual(len(source_meshes), len(meshes))
            self.assertEqual(meshes[0].vertices[:len(source_meshes[0].vertices)], source_meshes[0].vertices)
            self.assertEqual(meshes[0].faces[:len(source_meshes[0].faces)], source_meshes[0].faces)
            for i in range(1, len(meshes)):
                if i != report['wheel_mesh']:
                    self.assertEqual(meshes[i].encode(), source_meshes[i].encode())
            mutated = bytearray(raw)
            mutated[-5] ^= 1
            with self.assertRaises(ValueError):
                finale.generate(bytes(mutated), role)
            with self.assertRaises(ValueError):
                finale.generate(raw, 'transporter' if role == 'pace-car' else 'pace-car')
        with self.assertRaises(ValueError):
            finale.generate(self.cases['pace-car'][0], 'ALLCARS')

    def test_wheels_closed_winding_normals_and_actual_source_dimensions(self):
        for role, (_, output, report) in self.cases.items():
            _, nodes, meshes, _ = model.decode(output)
            wheel = meshes[report['wheel_mesh']]
            edges = Counter()
            for face in wheel.faces:
                vertices = [wheel.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                points = [tuple(round(c * 65536) for c in v[:3]) for v in vertices]
                edges.update(tuple(sorted((a, b))) for a, b in zip(points, points[1:] + points[:1]))
                normal = model.cross(model.sub(vertices[1][:3], vertices[0][:3]), model.sub(vertices[2][:3], vertices[0][:3]))
                self.assertGreater(model.dot(normal, normal), 1e-14)
                self.assertGreater(model.dot(normal, tuple(sum(v[k] for v in vertices) for k in (5, 6, 7))), 0)
                self.assertGreaterEqual(struct.unpack_from('<h', face, 8)[0], 0)
            self.assertEqual(set(edges.values()), {2}, role)
            expected_radius, expected_width, wheelbase, track = ((22937 / 65536, .25, 171048 / 65536, 91750 / 65536)
                                                               if role == 'pace-car' else
                                                               (30277 / 65536, 21626 / 65536, 291110 / 65536, 138412 / 65536))
            self.assertAlmostEqual(report['wheel']['radius'], expected_radius)
            self.assertAlmostEqual(report['wheel']['axial_width'], expected_width)
            pivots = [n['origin'] for n in finale.node_records(nodes) if n['id'] in (1, 2, 3, 4)]
            self.assertAlmostEqual(abs(pivots[0][0] - pivots[2][0]), wheelbase)
            self.assertAlmostEqual(abs(pivots[0][2] - pivots[1][2]), track)
            self.assertAlmostEqual(max(v[1] for v in wheel.vertices), expected_radius)
            self.assertAlmostEqual(min(v[1] for v in wheel.vertices), -expected_radius)

    def test_photo_uvs_and_arch_back_faces_follow_source_not_global_boxes(self):
        for _, (raw, output, report) in self.cases.items():
            _, _, originals, _ = model.decode(raw)
            _, _, meshes, _ = model.decode(output)
            source_wheel = originals[report['wheel_mesh']]
            wheel = meshes[report['wheel_mesh']]
            # Independently fit U(X), V(Y) from the original rectangular chart.
            for face in wheel.faces:
                vertices = [wheel.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                if abs(vertices[0][7]) < .999:
                    continue
                side = 1 if vertices[0][2] > 0 else -1
                chart = [v for v in source_wheel.vertices[:8] if v[2] * side > 0]
                x0, x1 = min(v[0] for v in chart), max(v[0] for v in chart)
                y0, y1 = min(v[1] for v in chart), max(v[1] for v in chart)
                a = next(v for v in chart if v[0] == x0 and v[1] == y0)
                b = next(v for v in chart if v[0] == x1 and v[1] == y0)
                c = next(v for v in chart if v[0] == x0 and v[1] == y1)
                for v in vertices:
                    self.assertAlmostEqual(v[3], a[3] + (v[0] - x0) * (b[3] - a[3]) / (x1 - x0), delta=2 / 65536)
                    self.assertAlmostEqual(v[4], a[4] + (v[1] - y0) * (c[4] - a[4]) / (y1 - y0), delta=2 / 65536)
            self.assertEqual(len(report['housings']), 4)
            original_points = {v[:3] for v in originals[0].vertices}
            for housing in report['housings']:
                self.assertTrue(set(housing['source_arch']) <= original_points)
                side = 1 if housing['center'][2] > 0 else -1
                self.assertLess(housing['inner_z'] * side,
                                housing['center'][2] * side - report['wheel']['axial_width'] / 2)
                faces = meshes[0].faces[housing['first_face']:housing['first_face'] + housing['faces']]
                for face in faces:
                    self.assertEqual(struct.unpack_from('<h', face, 8)[0], 0)
                    vertices = [meshes[0].vertices[vi] for vi in struct.unpack_from('<3H', face, 2)]
                    geometric = model.cross(model.sub(vertices[1][:3], vertices[0][:3]),
                                            model.sub(vertices[2][:3], vertices[0][:3]))
                    self.assertGreater(model.dot(geometric, geometric), 1e-14)
                    self.assertGreater(model.dot(geometric, tuple(sum(v[k] for v in vertices) for k in (5, 6, 7))), 0)
                    for vi in struct.unpack_from('<3H', face, 2):
                        vertex = meshes[0].vertices[vi]
                        self.assertAlmostEqual(vertex[3], report['dark_sampler']['raw_uv'][0], delta=1 / 65536)
                        self.assertAlmostEqual(vertex[4], report['dark_sampler']['raw_uv'][1], delta=1 / 65536)
                        self.assertLessEqual(abs(vertex[2]), max(abs(v[2]) for v in originals[0].vertices))

    def test_new_dark_surfaces_sample_real_opaque_source_palette_patches(self):
        for role, (raw, _, report) in self.cases.items():
            _, _, _, texture_section = model.decode(raw)
            size = struct.unpack_from('<I', texture_section, 12)[0]
            bmp = texture_section[16:16 + size]
            width, height = struct.unpack_from('<ii', bmp, 18)
            self.assertEqual(struct.unpack_from('<H', bmp, 28)[0], 8)
            pixels = struct.unpack_from('<I', bmp, 10)[0]
            palette = 14 + struct.unpack_from('<I', bmp, 14)[0]
            x, y, expected = ((54, 44, (0, 8, 8)) if role == 'pace-car' else (223, 102, (8, 8, 8)))
            self.assertEqual(tuple(report['dark_sampler']['pixel']), (x, y))
            for yy in range(y - 2, y + 3):
                for xx in range(x - 2, x + 3):
                    index = bmp[pixels + (height - 1 - yy) * ((width + 3) & ~3) + xx]
                    b, g, r = struct.unpack_from('<3B', bmp, palette + index * 4)
                    self.assertEqual((r, g, b), expected)
                    self.assertNotEqual((r, g, b), (0, 0, 0))


if __name__ == '__main__':
    unittest.main()
