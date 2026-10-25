#!/usr/bin/env python3
"""Prop pivots, face orientation and compact WebP import contracts."""

import importlib.util
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import mega_geometry as geometry
import vehicle_textures
import vehicle_model as MODEL
from git_game_data import archive_bytes, configured_data_root

SPEC = importlib.util.spec_from_file_location("mega_assets", PROJECT / "tools/build-mega-assets.py")
ASSETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ASSETS)
CONVERTER = shutil.which("magick") or shutil.which("convert")


def original_box(center, half):
    mesh = MODEL.Mesh(b"prop-fixture".ljust(48, b"\0"), [], [])
    geometry.geometry.box(mesh, center, half, (0, 0, 0, 255))
    mesh.faces = [face[:8] + struct.pack("<h", 2) + face[10:] for face in mesh.faces]
    return mesh


def write_scene_manifest(source, inputs=None):
    report = {'models': [], 'textures': []}
    for path in sorted(source.rglob('*')):
        if not path.is_file():
            continue
        row = {'bytes': path.stat().st_size, 'output_crc32': ASSETS.crc32(path.read_bytes())}
        if path.suffix == '.glb':
            row.update(track=int(path.parent.name.removeprefix('track-')), family=path.stem)
            report['models'].append(row)
        elif path.suffix == '.webp':
            row['source_crc32'] = path.stem
            report['textures'].append(row)
    if inputs is not None:
        report['build_inputs'] = inputs
    (source.parent / 'scene-manifest.json').write_text(json.dumps(report))
    return report


class SceneGenerationTests(unittest.TestCase):
    def test_incomplete_or_corrupt_generation_preserves_entire_bundle(self):
        for defect in ('missing model', 'corrupt texture', 'missing manifest'):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / 'generated/scene'
                destination = Path(directory) / 'bundle'
                (source / 'track-0').mkdir(parents=True)
                (source / 'track-0/cones.glb').write_bytes(b'live model')
                (source / '12345678.webp').write_bytes(b'live texture')
                write_scene_manifest(source)
                destination.mkdir()
                (destination / 'deadbeef.webp').write_bytes(b'old but valid bundle')
                if defect == 'missing model':
                    (source / 'track-0/cones.glb').unlink()
                elif defect == 'corrupt texture':
                    (source / '12345678.webp').write_bytes(b'bad texture!')
                else:
                    (source.parent / 'scene-manifest.json').unlink()
                with self.assertRaises((ValueError, OSError)):
                    ASSETS.sync_scene(source, destination)
                self.assertEqual(list(destination.iterdir()), [destination / 'deadbeef.webp'])
                self.assertEqual((destination / 'deadbeef.webp').read_bytes(), b'old but valid bundle')

    def test_incremental_update_repairs_outputs_and_tracks_all_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'generated'
            inputs = [root / name for name in ('source.bfp', 'builder.py', 'encoder', 'artwork.webp')]
            for path in inputs:
                path.write_bytes(path.name.encode())
            calls = []
            def generate(*args):
                calls.append(True)
                scene = output / 'scene'
                (scene / 'track-0').mkdir(parents=True, exist_ok=True)
                ASSETS.write(scene / 'track-0/cones.glb', b'generated model')
                ASSETS.write(scene / '12345678.webp', b'generated texture')
                return write_scene_manifest(scene)
            def update():
                return ASSETS.update(root, output, inputs[3], inputs[2], inputs[1], inputs)
            with patch.object(ASSETS, 'build', side_effect=generate):
                update()
                model = output / 'scene/track-0/cones.glb'
                texture = output / 'scene/12345678.webp'
                timestamps = model.stat().st_mtime_ns, texture.stat().st_mtime_ns
                update()
                self.assertEqual(len(calls), 1)
                self.assertEqual((model.stat().st_mtime_ns, texture.stat().st_mtime_ns), timestamps)
                for path in (model, texture):
                    path.unlink()
                    update()
                    self.assertTrue(path.is_file())
                model.write_bytes(b'corrupt model!!')
                update()
                self.assertEqual(model.read_bytes(), b'generated model')
                for path in inputs:
                    path.write_bytes(path.read_bytes() + b'changed')
                    update()
                self.assertEqual(len(calls), 8)
                (output / 'scene-manifest.json').unlink()
                update()
                self.assertEqual(len(calls), 9)
                for count, malformed in enumerate((None, [], 42, 'invalid report', True), start=10):
                    with self.subTest(manifest=malformed):
                        (output / 'scene-manifest.json').write_text(json.dumps(malformed))
                        report = update()
                        self.assertIsInstance(report, dict)
                        self.assertIn('build_inputs', report)
                        self.assertEqual(len(calls), count)

    def test_failed_generation_never_publishes_current_input_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'generated'
            source = output / 'scene'
            source.mkdir(parents=True)
            dependency = root / 'input'
            dependency.write_bytes(b'first revision')
            def generate(*args):
                ASSETS.write(source / '12345678.webp', dependency.read_bytes())
                return write_scene_manifest(source)
            def update():
                return ASSETS.update(root, output, dependency, dependency, dependency, [dependency])
            with patch.object(ASSETS, 'build', side_effect=generate):
                previous_inputs = update()['build_inputs']
            dependency.write_bytes(b'new revision')
            def incomplete(*args):
                # A generation may publish its ordinary output report before a
                # later validation/IO failure. It must not gain fresh inputs.
                generate()
                raise OSError('incomplete generation')
            with patch.object(ASSETS, 'build', side_effect=incomplete) as failed:
                for _ in range(2):
                    with self.assertRaisesRegex(OSError, 'incomplete generation'):
                        update()
                self.assertEqual(failed.call_count, 2)
                report = json.loads((output / 'scene-manifest.json').read_text())
                self.assertNotIn('build_inputs', report)
            with patch.object(ASSETS, 'build', side_effect=generate) as repaired:
                current_inputs = update()['build_inputs']
                self.assertEqual(repaired.call_count, 1)
                self.assertNotEqual(current_inputs, previous_inputs)
                update()
                self.assertEqual(repaired.call_count, 1)

    def test_track_validator_uses_shared_static_structure_and_track_budget(self):
        def container(*chunks):
            payload = b'WEBP' + b''.join(b'VP8 ' + struct.pack('<I', len(chunk)) + chunk for chunk in chunks)
            return b'RIFF' + struct.pack('<I', len(payload)) + payload
        frame = b'\0\0\0\x9d\x01\x2a' + struct.pack('<HH', 16, 8)
        self.assertEqual(ASSETS.webp_size(container(frame)), (16, 8))
        for payload in (container(frame, frame), container(b'\1' + frame[1:]),
                        container(frame[:6] + struct.pack('<HH', 1024, 8))):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                ASSETS.webp_size(payload)


class MegaGeometryTests(unittest.TestCase):
    def assert_valid_triangles(self, mesh, start=0):
        for face in mesh.faces[start:]:
            indices = struct.unpack_from("<3H", face, 2)
            self.assertLess(max(indices), len(mesh.vertices))
            a, b, c = (mesh.vertices[index] for index in indices)
            normal = MODEL.cross(MODEL.sub(b[:3], a[:3]), MODEL.sub(c[:3], a[:3]))
            self.assertGreater(MODEL.dot(normal, normal), 1e-14)
            self.assertGreater(MODEL.dot(normal, MODEL.add(MODEL.add(a[5:], b[5:]), c[5:])), 0)
        for vertex in mesh.vertices:
            self.assertTrue(all(math.isfinite(component) for component in vertex))
            self.assertAlmostEqual(sum(component ** 2 for component in vertex[5:]), 1., places=6)

    def test_knockhill_sheep_keep_cards_and_gain_closed_source_projected_bodies(self):
        from collections import Counter
        import mega_sheep
        archive = archive_bytes(configured_data_root(), 'data/knocfree.bfp')
        self.assertEqual(ASSETS.crc32(archive), '7e00bade')
        raw = ASSETS.members(archive)[5]
        self.assertEqual(ASSETS.crc32(raw), '24a4dac8')
        _, _, meshes, _ = MODEL.decode(raw)
        names = [name.split(b'\0', 1)[0].decode('ascii') for name, _ in ASSETS.textures(raw)]
        self.assertEqual([i for i, source in enumerate(meshes) if mega_sheep.matches(source, names)], [13, 14, 15, 16])
        for index in (13, 14, 15, 16):
            source = meshes[index]
            before = source.encode()
            enhanced, stats = mega_sheep.enhance(source, MODEL.Mesh, names, len(names), 2048)
            self.assertEqual(source.encode(), before)
            self.assertEqual(enhanced.header, source.header)
            self.assertEqual(enhanced.vertices[:4], source.vertices)
            self.assertEqual(enhanced.faces[:2], source.faces)
            self.assertEqual(stats['knockhill_sheep'], 1)
            self.assert_valid_triangles(enhanced, start=len(source.faces))
            edges = Counter()
            for face in enhanced.faces[2:]:
                self.assertEqual(struct.unpack_from('<h', face, 8)[0], struct.unpack_from('<h', source.faces[0], 8)[0])
                triangle = [enhanced.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                points = [tuple(round(c, 6) for c in vertex[:3]) for vertex in triangle]
                for a, b in zip(points, points[1:] + points[:1]):
                    edges[tuple(sorted((a, b)))] += 1
                for vertex in triangle:
                    self.assertGreaterEqual(vertex[1], mega_sheep.FRONT_INSET - 1e-8)
                    self.assertLessEqual(vertex[1], mega_sheep.DEPTH + 1e-8)
                    self.assertTrue(-.65 <= vertex[0] <= .65 and -1 <= vertex[2] <= 0)
                    expected = mega_sheep._project(source, vertex[3], vertex[4] + 1)
                    self.assertAlmostEqual(vertex[0], expected[0])
                    self.assertAlmostEqual(vertex[2], expected[1])
            self.assertTrue(edges and set(edges.values()) == {2})
            # Independently find each closed added solid by mesh connectivity.
            # A leg must enter an actual faceted torso volume, not merely touch
            # its projected image or overlap an analytic ellipsoid's bounds.
            neighbors = {}
            for face in enhanced.faces[len(source.faces):]:
                triangle = struct.unpack_from('<3H', face, 2)
                for vertex in triangle:
                    neighbors.setdefault(vertex, set()).update(triangle)
            components = []
            unseen = set(neighbors)
            while unseen:
                pending = [next(iter(unseen))]
                connected = set()
                while pending:
                    vertex = pending.pop()
                    if vertex in connected:
                        continue
                    connected.add(vertex)
                    pending.extend(neighbors[vertex] - connected)
                unseen.difference_update(connected)
                faces = [face for face in enhanced.faces[len(source.faces):]
                         if struct.unpack_from('<H', face, 2)[0] in connected]
                components.append((connected, faces))
            legs = [component for component in components
                    if max(enhanced.vertices[v][2] for v in component[0]) > -.1]
            bodies = [component for component in components if component not in legs]
            self.assertEqual(len(legs), 4)

            def inside_body(point, faces):
                # Each coarse body is convex, with outward triangle winding.
                for face in faces:
                    a, b, c = (enhanced.vertices[v][:3]
                               for v in struct.unpack_from('<3H', face, 2))
                    normal = MODEL.cross(MODEL.sub(b, a), MODEL.sub(c, a))
                    if MODEL.dot(normal, MODEL.sub(point, a)) >= -1e-10:
                        return False
                return True

            for vertices, _ in legs:
                self.assertTrue(any(inside_body(enhanced.vertices[v][:3], faces)
                                    for v in vertices for _, faces in bodies),
                                f'{index}: every leg must overlap a closed body')
            # Heads and necks need a volume-overlap path to a torso. The two
            # sheep in a pair may retain independent torso groups; a floating
            # head/neck group must not pass merely because its own parts join.
            torsos = {n for n, (vertices, _) in enumerate(bodies)
                      if max(enhanced.vertices[v][2] for v in vertices) > -.4}
            self.assertTrue(torsos)
            joins = {n: set() for n in range(len(bodies))}
            for left, (left_vertices, left_faces) in enumerate(bodies):
                for right in range(left + 1, len(bodies)):
                    right_vertices, right_faces = bodies[right]
                    overlap = [enhanced.vertices[v][:3] for v in left_vertices
                               if inside_body(enhanced.vertices[v][:3], right_faces)]
                    overlap.extend(enhanced.vertices[v][:3] for v in right_vertices
                                   if inside_body(enhanced.vertices[v][:3], left_faces))
                    if index in (13, 15) and ((left in torsos) != (right in torsos)):
                        # The central pair neck must reach a rump's middle,
                        # rather than clipping only its upper/front corner:
                        # that shallow join leaves the rear-view gap below it.
                        torso = left_vertices if left in torsos else right_vertices
                        mid_height = (min(enhanced.vertices[v][2] for v in torso)
                                      + max(enhanced.vertices[v][2] for v in torso)) / 2
                        overlap = [point for point in overlap if point[2] >= mid_height]
                    if overlap:
                        joins[left].add(right)
                        joins[right].add(left)
            reached = set(torsos)
            pending = list(torsos)
            while pending:
                current = pending.pop()
                connected = joins[current] - reached
                reached.update(connected)
                pending.extend(connected)
            self.assertEqual(reached, set(joins),
                             f'{index}: every head/neck must reach a torso by volume overlap')
            rejected, stats = mega_sheep.enhance(source, MODEL.Mesh, names, len(names), 0)
            self.assertIs(rejected, source)
            self.assertEqual(stats['added_triangles'], 0)
            wrong = list(names)
            wrong[struct.unpack_from('<h', source.faces[0], 8)[0]] = 'SHEEP99'
            self.assertFalse(mega_sheep.matches(source, wrong))
            changed = MODEL.Mesh(source.header, list(source.vertices), list(source.faces))
            changed.vertices[0] = (source.vertices[0][0] + .01, *source.vertices[0][1:])
            self.assertFalse(mega_sheep.matches(changed, names))

    def test_cone_keeps_actor_pivot_bounds_and_adds_round_taper(self):
        original = original_box((1., .1, -2.), (.35, .45, .35))
        before = list(original.vertices), list(original.faces)
        cone = geometry.cone(original, MODEL.Mesh, 3)
        self.assertEqual(cone.header, original.header)
        self.assertEqual(geometry.bounds(cone), geometry.bounds(original))
        self.assertEqual((original.vertices, original.faces), before)
        self.assertEqual({struct.unpack_from("<h", face, 8)[0] for face in cone.faces}, {3})
        self.assert_valid_triangles(cone)
        # Several authored heights and many radial directions replace the
        # original angular body while retaining its resting footprint.
        self.assertGreaterEqual(len({round(vertex[1], 6) for vertex in cone.vertices}), 6)
        self.assertTrue(all(.008 - 1e-9 <= v[3] <= .992 + 1e-9 and -.992 - 1e-9 <= v[4] <= -.008 + 1e-9
                            for v in cone.vertices))

    def test_lavaland_gargoyle_dispatch_retains_source_shape_and_both_materials(self):
        records = ASSETS.members((configured_data_root() / 'data/landfree.bfp').read_bytes())
        raw = records[8]
        self.assertEqual(ASSETS.crc32(raw), 'ca267324')
        _, _, meshes, _ = MODEL.decode(raw)
        names = [name.split(b'\0', 1)[0].decode('ascii') for name, _ in ASSETS.textures(raw)]
        self.assertEqual(names, ['GARG01', 'GARG02'])
        self.assertEqual(len(meshes), 1)
        original = meshes[0]
        before = list(original.vertices), list(original.faces)
        self.assertEqual(geometry.bounds(original)[1], (-9356 / 65536, 50379 / 65536))
        with patch.object(geometry, 'cone', side_effect=AssertionError('Gargoyles must retain their source shape')):
            result = ASSETS.prop_meshes(9, 8, raw, meshes, names)
        self.assertEqual((original.vertices, original.faces), before)
        self.assertEqual(geometry.bounds(result[0]), geometry.bounds(original))
        self.assertGreater(len(result[0].faces), len(original.faces))
        self.assertNotEqual(result[0].vertices, original.vertices)
        self.assertEqual({struct.unpack_from('<h', face, 8)[0] for face in result[0].faces}, {0, 1})
        # Neither another track nor changed source bytes may adopt this shape
        # exception; their existing generic cone dispatch stays active.
        generated = original_box((0, 0, 0), (.35, .45, .35))
        for track, source in ((2, raw), (9, raw + b'\0')):
            with self.subTest(track=track, checksum=ASSETS.crc32(source)):
                with patch.object(geometry, 'cone', return_value=generated) as make:
                    self.assertEqual(ASSETS.prop_meshes(track, 8, source, meshes, names), [generated])
                make.assert_called_once_with(original, MODEL.Mesh, 2)

    def test_accepted_source_props_keep_their_original_shapes_and_uvs(self):
        for track, prefix in ((0, 'donl'), (1, 'silv'), (2, 'thru'), (3, 'bran'), (4, 'oult'), (5, 'dons'), (6, 'crof')):
            records = ASSETS.members((configured_data_root() / f'data/{prefix}free.bfp').read_bytes())
            for index in ((10,) if track == 4 else (8, 9, 10)):
                with self.subTest(track=track, member=index):
                    _, _, meshes, _ = MODEL.decode(records[index])
                    names = [name.split(b'\0', 1)[0].decode('ascii') for name, _ in ASSETS.textures(records[index])]
                    self.assertIs(ASSETS.prop_meshes(track, index, records[index], meshes, names), meshes)

    @unittest.skipUnless(CONVERTER, "ImageMagick is unavailable")
    def test_oulton_dhl_arrow_surrounds_sample_opaque_white_paint(self):
        raw = ASSETS.members(archive_bytes(configured_data_root(), 'data/oultfree.bfp'))[10]
        _, _, meshes, _ = MODEL.decode(raw)
        name, bmp = ASSETS.textures(raw)[0]
        self.assertEqual((name.rstrip(b'\0'), ASSETS.crc32(bmp)), (b'LDHL', '7f08be18'))
        key = ASSETS.crc32(bmp)
        mapping = PROJECT / 'assets/models/mega/texture-replacements.json'
        matches = [entry for entry in json.loads(mapping.read_text())['replacements']
                   if 'LDHL' in entry['names'] and key in entry.get('source_crc32', [])]
        self.assertEqual(len(matches), 1)
        artwork = (mapping.parent / matches[0]['image']).read_bytes()
        width, height = ASSETS.webp_size(artwork)
        pixels = subprocess.run([CONVERTER, 'webp:-', '-depth', '8', 'rgba:-'], input=artwork,
                                capture_output=True, check=True).stdout
        self.assertEqual(len(pixels), width * height * 4)
        # The original boards reuse the upper-right DHL field for their frame,
        # reverse and arrow surrounds. A wider logo put red in the lower-left
        # arrow corner, even though the arrow material itself was correct.
        for mesh in meshes:
            paint = []
            for face in mesh.faces:
                if struct.unpack_from('<h', face, 8)[0] != 0:
                    continue
                corners = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                if all(vertex[3] >= .7 for vertex in corners):
                    paint.extend((vertex[3], vertex[4] + 1) for vertex in corners)
            self.assertTrue(paint)
            x0 = max(0, math.floor(min(u for u, v in paint) * width) - 2)
            x1 = min(width, math.ceil(max(u for u, v in paint) * width) + 2)
            y0 = max(0, math.floor(min(v for u, v in paint) * height) - 2)
            y1 = min(height, math.ceil(max(v for u, v in paint) * height) + 2)
            for y in range(y0, y1):
                for x in range(x0, x1):
                    r, g, b, a = pixels[(y * width + x) * 4:(y * width + x + 1) * 4]
                    self.assertTrue(min(r, g, b) >= 245 and a == 255,
                                    f'{mesh.header[:12]!r} structural paint at {x},{y}: {(r, g, b, a)}')

    def test_brands_bridge_local_material_preserves_native_helicopter_scalar(self):
        raw = ASSETS.members(archive_bytes(configured_data_root(), 'data/branfree.bfp'))[5]
        self.assertEqual(ASSETS.crc32(raw), '90ddbd12')
        header, _, meshes, _ = MODEL.decode(raw)
        source_records = ASSETS.textures(raw)
        self.assertEqual(ASSETS.crc32(source_records[127][1]), '1c07ca8e')
        original_meshes = [mesh.encode() for mesh in meshes]
        # Existing appended materials must keep their indices as well as the
        # original prefix; the bridge chart is appended strictly after them.
        records = source_records + [(b'AlreadyAdded', b'existing authored material')]
        prepared_header = bytearray(header)
        struct.pack_into('<H', prepared_header, 16, len(records))
        prepared_header = bytes(prepared_header)
        output_header, output_meshes, output_records, added = ASSETS.brands_bridge_material(
            3, 5, raw, prepared_header, meshes, records, b'authored bridge BMP identity carrier')
        self.assertTrue(added)
        self.assertEqual(output_records[:-1], records)
        self.assertEqual(output_records[-1], (b'MegaBridgeX\0', b'authored bridge BMP identity carrier'))
        self.assertEqual(struct.unpack_from('<H', output_header, 16)[0], len(records) + 1)
        self.assertEqual(output_header[:16] + output_header[18:], prepared_header[:16] + prepared_header[18:])
        self.assertEqual([mesh.encode() for mesh in meshes], original_meshes)
        self.assertEqual(output_meshes[33].header, meshes[33].header)
        self.assertEqual(output_meshes[33].vertices, meshes[33].vertices)
        changed = []
        for index, (before, after) in enumerate(zip(meshes[33].faces, output_meshes[33].faces)):
            if before != after:
                changed.append(index)
                self.assertEqual(struct.unpack_from('<h', before, 8)[0], 127)
                self.assertEqual(struct.unpack_from('<h', after, 8)[0], len(records))
                self.assertEqual(before[:8] + before[10:], after[:8] + after[10:])
        self.assertEqual(len(changed), 12)
        for index, mesh in enumerate(output_meshes):
            if index != 33:
                self.assertEqual(mesh.encode(), original_meshes[index])
        self.assertEqual(output_meshes[34].faces[69], meshes[34].faces[69])
        self.assertEqual(struct.unpack_from('<h', output_meshes[34].faces[69], 8)[0], 127)
        self.assertEqual(output_records[127], source_records[127])
        for track, member, source in ((2, 5, raw), (3, 3, raw), (3, 5, raw + b'\0')):
            self.assertEqual(ASSETS.brands_bridge_material(track, member, source, prepared_header,
                             meshes, records, b'authored bridge BMP identity carrier'),
                             (prepared_header, meshes, records, False))

    def test_post_keeps_footprint_height_and_authored_marking_slot(self):
        original = original_box((.1, .5, -.2), (.05, .5, .05))
        before = list(original.vertices), list(original.faces)
        post = geometry.post(original, MODEL.Mesh, 3)
        self.assertEqual(post.header, original.header)
        self.assertEqual(geometry.bounds(post), geometry.bounds(original))
        self.assertEqual((original.vertices, original.faces), before)
        self.assertEqual({struct.unpack_from("<h", face, 8)[0] for face in post.faces}, {2, 3})
        self.assertGreater(len(post.faces), len(original.faces))
        self.assert_valid_triangles(post)
        # An octagonal side silhouette and an inset top distinguish actual
        # beveled geometry from merely subdividing the original box.
        bottom = [v for v in post.vertices if v[1] == 0.]
        self.assertEqual(len({v[:3] for v in bottom}), 8)
        top = [v for v in post.vertices if v[1] == 1.]
        self.assertTrue(all(.05 < v[0] < .15 and -.25 < v[2] < -.15 for v in top))

    def test_sign_retains_all_number_arrow_faces_and_simple_body_edges(self):
        original = original_box((.2, -.02, -1.), (.9, .12, .9))
        before = list(original.vertices), list(original.faces)
        sign = geometry.sign(original, MODEL.Mesh, 3)
        self.assertEqual(sign.header, original.header)
        self.assertEqual(sign.vertices[:len(original.vertices)], original.vertices)
        self.assertEqual(sign.faces[:len(original.faces)], original.faces)
        self.assertEqual((original.vertices, original.faces), before)
        self.assertGreater(len(sign.faces), len(original.faces) + 40)
        added = sign.vertices[len(original.vertices):]
        extent = geometry.bounds(original)
        self.assertTrue(all(extent[0][0] <= v[0] <= extent[0][1] and extent[2][0] <= v[2] <= extent[2][1] for v in added))
        self.assertAlmostEqual(min(v[1] for v in added), extent[1][0] - .006)
        self.assertEqual({struct.unpack_from("<h", face, 8)[0] for face in sign.faces[len(original.faces):]}, {3})
        self.assert_valid_triangles(sign, len(original.faces))


class MegaTextureTests(unittest.TestCase):
    def test_flag_unresolved_parent_retains_actor_and_three_cloth_variants(self):
        def node(parent, flags, variants):
            record = bytearray((80 + 2 * len(variants) + 3) & ~3)
            struct.pack_into('<Ih', record, 0, len(record), parent)
            struct.pack_into('<I', record, 72, flags)
            record[76] = len(variants)
            struct.pack_into(f'<{len(variants)}H', record, 80, *variants)
            return bytes(record)

        root = node(-1, 0x04000042, [])
        cloth = node(1, 1, [0, 1, 2])
        pole = node(0, 5, [3])
        header = bytearray(32)
        struct.pack_into('<H', header, 12, 3)
        struct.pack_into('<III', header, 20, 32, 32 + len(root + cloth + pole), 1024)
        result_header, result = ASSETS.flag_nodes(header, root + cloth + pole)
        self.assertEqual(struct.unpack_from('<H', result_header, 12)[0], 2)
        self.assertEqual(struct.unpack_from('<h', result, 4)[0], -1)
        self.assertEqual(struct.unpack_from('<I', result, 72)[0], 0x04000001)
        self.assertEqual(struct.unpack_from('<3H', result, 80), (0, 1, 2))
        self.assertEqual(result[len(cloth):], pole)
        self.assertEqual(struct.unpack_from('<I', result_header, 28)[0], 1024 - len(root))

    def test_flag_family_selection_preserves_empty_and_nonempty_source_tracks(self):
        for track, prefix, expected in ((0, 'donl', True), (1, 'silv', False), (9, 'land', True)):
            with self.subTest(track=track):
                records = ASSETS.members((configured_data_root() / f'data/{prefix}free.bfp').read_bytes())
                selected = dict(ASSETS.active_families(track, records))
                self.assertEqual(selected.get(11), 'flags' if expected else None)
                _, nodes, meshes, _ = MODEL.decode(records[11])
                self.assertEqual(bool(nodes and meshes), expected)
                if expected:
                    # The cloth is a three-variant source mesh family. No
                    # generated pole, new controller or reduced phase list.
                    self.assertEqual(len(meshes) % 4, 0)
                    self.assertEqual(sum(len(mesh.faces) for mesh in meshes), len(meshes) // 4 * 18)
                if track == 9:
                    self.assertTrue({2, 9, 10}.isdisjoint(selected))

    def test_unsupported_canonical_pixels_retain_their_native_dimensions(self):
        for source, expected in (((1, 1), (1, 1)), ((24, 12), (24, 12)), ((64, 128), (64, 128)),
                                 ((256, 128), (256, 128)), ((2048, 4), (2048, 4)), ((32, -16), (32, 16))):
            with self.subTest(source=source):
                header = bytearray(54)
                struct.pack_into("<ii", header, 18, *source)
                self.assertEqual(ASSETS.texture_size(header), expected)

    @unittest.skipUnless(CONVERTER, "ImageMagick is unavailable")
    def test_native_embedding_retains_pixel_size_cutouts_and_webp70(self):
        pixels = [(0, 0, 0), (8, 8, 8), (255, 255, 255), (255, 80, 20)] * 4
        source = vehicle_textures.rgb_bmp(4, 4, pixels)
        run = subprocess.run
        with patch.object(ASSETS.subprocess, "run", wraps=run) as observed:
            encoded = ASSETS.native_texture(source)
        command = observed.call_args.args[0]
        quality = command.index("-quality")
        self.assertEqual(command[quality + 1], "70")
        self.assertIn("webp:lossless=false", command)
        self.assertIn("webp:alpha-quality=100", command)
        self.assertNotIn("-resize", command)
        self.assertLess(len(encoded), ASSETS.MAX_TEXTURE_BYTES)
        self.assertEqual(encoded[:4], b"RIFF")
        self.assertEqual(encoded[8:12], b"WEBP")
        decoded = run([CONVERTER, "webp:-", "-depth", "8", "rgba:-"], input=encoded,
                      capture_output=True, check=True).stdout
        self.assertEqual(len(decoded), 4 * 4 * 4)
        self.assertEqual(list(decoded[3::4]), [0, 255, 255, 255] * 4)

    @unittest.skipUnless(CONVERTER, "ImageMagick is unavailable")
    def test_generated_mapping_covers_palette_variants_by_exact_name_and_aspect(self):
        image = ASSETS.native_texture(vehicle_textures.rgb_bmp(16, 8, [(255, 240, 100)] * 128))
        self.assertEqual(ASSETS.webp_size(image), (16, 8))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "board.webp").write_bytes(image)
            mapping = root / "texture-replacements.json"
            entry = {"names": ["BOARD"], "source_size": [4, 2], "image": "board.webp"}
            document = {"version": 1, "webp_quality": 70, "replacements": [entry]}
            mapping.write_text(json.dumps(document))
            sources = {
                "00000001": {"names": {"BOARD"}, "size": (4, 2)},
                "00000002": {"names": {"BOARD"}, "size": (4, 2)},
                "00000003": {"names": {"BOARD"}, "size": (2, 4)},
                "00000004": {"names": {"OTHER"}, "size": (4, 2)},
            }
            encoded, report = ASSETS.replacements(mapping, sources)
            self.assertEqual(encoded, {"00000001": image, "00000002": image})
            self.assertEqual({row["kind"] for row in report}, {"generated"})
            self.assertEqual({tuple(row["size"]) for row in report}, {(16, 8)})
            entry["source_crc32"] = ["00000002"]
            mapping.write_text(json.dumps(document))
            encoded, _ = ASSETS.replacements(mapping, sources)
            self.assertEqual(set(encoded), {"00000002"})

            entry["image"] = "absent.webp"
            mapping.write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, "Missing or invalid generated"):
                ASSETS.replacements(mapping, sources)
            entry["image"], entry["source_size"] = "board.webp", [2, 4]
            entry.pop("source_crc32")
            mapping.write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, "aspect"):
                ASSETS.replacements(mapping, sources)
            entry["source_size"], entry["names"] = [4, 2], ["MISSPELLED"]
            mapping.write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, "does not match canonical"):
                ASSETS.replacements(mapping, sources)

    def test_shared_cache_contains_only_explicit_generated_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "deadbeef.webp").write_bytes(b"stale generated output")
            (root / "notes.webp").write_bytes(b"unrelated authored file")
            ASSETS.reconcile_texture_cache(root, {"12345678": b"generated asset"})
            self.assertFalse((root / "deadbeef.webp").exists())
            self.assertEqual((root / "12345678.webp").read_bytes(), b"generated asset")
            self.assertTrue((root / "notes.webp").exists())

    @unittest.skipUnless(CONVERTER, "ImageMagick is unavailable")
    def test_leaf_sprites_keep_their_exact_source_palette_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outputs, sources, images = [], {}, {}
            for key, color, source_name, material, source_size in (
                    ("12345678", (40, 60, 20), "LHRTS4", "MegaTreeLeaf", (64, 64)),
                    ("87654321", (25, 35, 15), "LHRTS7", "MegaTreeLf7", (64, 64)),
                    ("11223344", (20, 30, 12), "LHRTWS3", "MegaGroveLf", (128, 64)),
                    ("55667788", (30, 40, 10), "LHRTS2", "MegaTreeLf2", (32, 64)),
                    ("99001122", (30, 45, 20), "LHRTS1", "MegaTreeLf1", (64, 64)),
                    ("33445566", (32, 37, 16), "LHRTWS4", "MegaGrovLf4", (128, 64)),
                    ("44556677", (24, 24, 11), "LMRTS4", "MegaGrovLm4", (64, 32)),
                    ("66778899", (38, 46, 14), "LMRTS2", "MegaGrovLm2", (64, 32))):
                data = ASSETS.native_texture(vehicle_textures.rgb_bmp(16, 16, [color] * 256))
                name = key + ".webp"
                (root / name).write_bytes(data)
                images[key] = data
                sources[key] = {"names": {source_name}, "size": source_size}
                outputs.append({"image": name, "crc32": ASSETS.crc32(data),
                                "mapping": {"append_material": material, "source_crc32": [key]}})
            recipe = root / "vegetation-generation.json"
            recipe.write_text(json.dumps({"outputs": outputs}))
            resolved, report = ASSETS.vegetation_materials(recipe, sources)
            self.assertEqual({key: data for key, (_, data) in resolved.items()}, images)
            self.assertNotEqual(resolved["12345678"][0], resolved["87654321"][0])
            self.assertEqual({tuple(row["source_crc32"]) for row in report},
                             {("12345678",), ("87654321",), ("11223344",), ("55667788",), ("99001122",), ("33445566",), ("44556677",), ("66778899",)})
            self.assertEqual({tuple(row["size"]) for row in report}, {(16, 16)})
            self.assertEqual({(row["source_name"], row["material"]) for row in report},
                             {("LHRTS4", "MegaTreeLeaf"), ("LHRTS7", "MegaTreeLf7"), ("LHRTWS3", "MegaGroveLf"),
                              ("LHRTS2", "MegaTreeLf2"), ("LHRTS1", "MegaTreeLf1"), ("LHRTWS4", "MegaGrovLf4"), ("LMRTS4", "MegaGrovLm4"), ("LMRTS2", "MegaGrovLm2")})
            # Canonical LHRTS7 precedes LHRTS4 in Donington. The appended M01a
            # leaf slot must stay first while each mesh receives its own sprite.
            first, second, third, fourth = b"canonical first palette", b"canonical second palette", b"canonical group palette", b"canonical narrow palette"
            fifth, sixth = b"canonical stepped palette", b"canonical dense group palette"
            seventh = b"canonical short-stem group palette"
            eighth = b"canonical separated group palette"
            records = [(b"LMRTS2", eighth), (b"LMRTS4", seventh), (b"LHRTWS4", sixth), (b"LHRTS1", fifth), (b"LHRTS2", fourth), (b"LHRTS7", second), (b"LHRTWS3", third), (b"LHRTS4", first)]
            foliage = {ASSETS.crc32(first): resolved["12345678"], ASSETS.crc32(second): resolved["87654321"], ASSETS.crc32(third): resolved["11223344"],
                       ASSETS.crc32(fourth): resolved["55667788"], ASSETS.crc32(fifth): resolved["99001122"], ASSETS.crc32(sixth): resolved["33445566"],
                       ASSETS.crc32(seventh): resolved["44556677"], ASSETS.crc32(eighth): resolved["66778899"]}
            slots = ASSETS.vegetation_slots(records, foliage, 5)
            self.assertEqual(slots[7], (5, b"MegaTreeLeaf", resolved["12345678"][0]))
            self.assertEqual(slots[5], (6, b"MegaTreeLf7\0", resolved["87654321"][0]))
            self.assertEqual(slots[6], (7, b"MegaGroveLf\0", resolved["11223344"][0]))
            self.assertEqual(slots[4], (8, b"MegaTreeLf2\0", resolved["55667788"][0]))
            self.assertEqual(slots[3], (9, b"MegaTreeLf1\0", resolved["99001122"][0]))
            self.assertEqual(slots[2], (10, b"MegaGrovLf4\0", resolved["33445566"][0]))
            self.assertEqual(slots[1], (11, b"MegaGrovLm4\0", resolved["44556677"][0]))
            self.assertEqual(slots[0], (12, b"MegaGrovLm2\0", resolved["66778899"][0]))

    def test_scene_sync_removes_stale_owned_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            source, destination = Path(directory) / "generated", Path(directory) / "bundle"
            (source / "track-0").mkdir(parents=True)
            destination.mkdir()
            (source / "12345678.webp").write_bytes(b"AI-authored replacement")
            (source / "track-0/cones.glb").write_bytes(b"generated geometry")
            (source / "track-0/moving.glb").write_bytes(b"generated aircraft")
            (destination / "track-9").mkdir()
            (destination / "track-9/rotorcraft.glb").write_bytes(b"inactive horizon")
            (destination / "deadbeef.webp").write_bytes(b"stale generated texture")
            (destination / "notes.txt").write_text("unrelated")
            write_scene_manifest(source)
            ASSETS.sync_scene(source, destination)
            self.assertFalse((destination / "deadbeef.webp").exists())
            self.assertEqual((destination / "12345678.webp").read_bytes(), b"AI-authored replacement")
            self.assertEqual((destination / "track-0/cones.glb").read_bytes(), b"generated geometry")
            self.assertEqual((destination / "track-0/moving.glb").read_bytes(), b"generated aircraft")
            self.assertFalse((destination / "track-9/rotorcraft.glb").exists())
            self.assertTrue((destination / "notes.txt").exists())


class MegaHorizonTests(unittest.TestCase):
    def test_psp_uv_skin_appends_without_replacing_original_materials(self):
        header = bytearray(range(32))
        struct.pack_into("<H", header, 16, 1)
        original = bytes(header)
        records = [(b"PSP1".ljust(12, b"\0"), b"original AI PSP1 material")]
        psp = MODEL.Mesh(b"PSP1_1".ljust(48, b"\0"), [], [])
        updated, appended, added = ASSETS.psp_material(original, [psp], records, b"opaque authored UV skin")
        self.assertTrue(added)
        self.assertEqual(appended[:-1], records)
        self.assertEqual(appended[-1], (b"MegaPSPSkin\0", b"opaque authored UV skin"))
        self.assertEqual(struct.unpack_from("<H", updated, 16)[0], 2)
        self.assertEqual(updated[:16] + updated[18:], original[:16] + original[18:])
        self.assertEqual(bytes(header), original)
        self.assertEqual(len(records), 1)
        other = MODEL.Mesh(b"AIRMARK_1".ljust(48, b"\0"), [], [])
        self.assertEqual(ASSETS.psp_material(original, [other], records, b"skin"), (original, records, False))
        with self.assertRaisesRegex(ValueError, "prefix"):
            ASSETS.psp_material(updated, [psp], appended, b"skin")

    def test_partial_original_palette_is_completed_without_changing_pixels(self):
        palette = bytes(range(256)) * 3 + bytes(range(252)) + b"\xff\xff\xff\0"
        source = bytearray(1078)
        source[:2] = b"BM"
        struct.pack_into("<I", source, 2, len(source))
        struct.pack_into("<I", source, 10, 1074)
        struct.pack_into("<IiiHH", source, 14, 40, 1, 1, 1, 8)
        struct.pack_into("<I", source, 46, 255)
        source[54:1074] = palette[:1020]
        source[1074:] = bytes((255, 17, 0, 0))
        before = bytes(source)
        restored = ASSETS.normalize_horizon_bmp(source, palette)
        self.assertEqual(bytes(source), before)
        self.assertEqual(len(restored), len(source) + 4)
        self.assertEqual(struct.unpack_from("<I", restored, 2)[0], len(restored))
        self.assertEqual(struct.unpack_from("<I", restored, 10)[0], 1078)
        self.assertEqual(struct.unpack_from("<I", restored, 46)[0], 256)
        self.assertEqual(restored[54:1078], palette)
        self.assertEqual(restored[1078:], before[1074:])
        self.assertEqual(ASSETS.normalize_horizon_bmp(restored, palette), restored)
        self.assertNotEqual(ASSETS.crc32(restored), ASSETS.crc32(before))

    def test_original_actor_nodes_match_runtime_ids_parent_and_animation_slots(self):
        header = bytearray(32)
        struct.pack_into("<H", header, 12, 2)
        root, child = bytearray(84), bytearray(88)
        struct.pack_into("<Ih", root, 0, len(root), -1)
        struct.pack_into("<Ih", child, 0, len(child), 0)
        for node in (root, child):
            for index in (0, 5, 10, 15):
                struct.pack_into("<i", node, 8 + index * 4, 65536)
        struct.pack_into("<I", root, 72, 0xa0000007)
        root[76] = 1
        struct.pack_into("<H", root, 80, 4)
        struct.pack_into("<I", child, 72, 17)
        child[76] = 4
        struct.pack_into("<4H", child, 80, 0, 1, 2, 3)
        original = bytes(root + child)
        moving_header, moving = ASSETS.horizon_nodes(header, original, "moving")
        self.assertEqual(moving_header, bytes(header))
        self.assertEqual(struct.unpack_from("<I", moving, 72)[0], 0xa00002f4)
        self.assertEqual(moving[84:], bytes(child))
        self.assertEqual(moving[80:84], original[80:84])
        heli_header, helicopter = ASSETS.horizon_nodes(header, original, "rotorcraft")
        self.assertEqual(struct.unpack_from("<H", heli_header, 12)[0], 3)
        self.assertEqual(struct.unpack_from("<Ih", helicopter, 0), (80, -1))
        self.assertEqual(struct.unpack_from("<16i", helicopter, 8), tuple(65536 if i in (0, 5, 10, 15) else 0 for i in range(16)))
        self.assertEqual(struct.unpack_from("<h", helicopter, 84)[0], 0)
        self.assertEqual(struct.unpack_from("<I", helicopter, 152)[0], 0xa00002f4)
        self.assertEqual(struct.unpack_from("<h", helicopter, 80 + 84 + 4)[0], 1)
        self.assertEqual(helicopter[-12:], original[-12:])
        self.assertEqual(original, bytes(root + child))

    def test_only_originally_active_track_horizon_groups_are_built(self):
        self.assertEqual([ASSETS.horizon_members(track) for track in range(10)],
                         [(0,), (0, 1), (0,), (1,), (1,), (0,), (0,), (1,), (0, 1), ()])

    def test_inactive_horizon_artifacts_are_removed_before_bundling(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'track-0').mkdir()
            (root / 'track-9').mkdir()
            (root / 'track-0/moving.glb').write_bytes(b'active')
            (root / 'track-9/moving.glb').write_bytes(b'inactive')
            (root / 'track-9/custom.glb').write_bytes(b'unrelated')
            ASSETS.reconcile_models(root, [{'track': 0, 'family': 'moving'}])
            self.assertTrue((root / 'track-0/moving.glb').exists())
            self.assertFalse((root / 'track-9/moving.glb').exists())
            self.assertTrue((root / 'track-9/custom.glb').exists())


if __name__ == "__main__":
    unittest.main()
