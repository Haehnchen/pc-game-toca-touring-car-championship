"""Canonical scenery geometry checks for props."""

import math
import struct

from .environment import MODEL, detail
from .fixtures import SceneryCase
from .panels import panel


class SceneryPropTests(SceneryCase):
    def test_donington_spectator_batch_preserves_fronts_and_closes_shallow_volumes(self):
        from collections import Counter
        import mega_spectators
        for prefix, archive_crc, member_crc, selected in (
                ('donl', '46885a20', '5f670bb9', (36, 37, 62)),
                ('dons', 'f07db8b7', 'f60fd8a0', (36, 38, 39))):
            raw = MODEL.read_source(dict(archive=f'data/{prefix}free.bfp',
                                        archive_crc32=archive_crc, member=5,
                                        member_name='OBJECTS.C3D', member_crc32=member_crc))
            header, _, meshes, textures = MODEL.decode(raw)
            names, offset = [], 0
            for _ in range(struct.unpack_from('<H', header, 16)[0]):
                name, length = struct.unpack_from('<12sI', textures, offset)
                names.append(name.rstrip(b'\0').decode('ascii'))
                offset += 16 + length
            self.assertEqual([i for i, mesh in enumerate(meshes) if mega_spectators.matches(mesh, names)], sorted(selected))
            for index in selected:
                source = meshes[index]
                enhanced, stats = detail.scenery(source, MODEL.Mesh, names, len(names), 'objects')
                self.assertGreater(stats['added_triangles'], 0)
                self.assert_preserved(source, enhanced)
                self.assert_valid_added_faces(source, enhanced)
                edges = Counter()
                for face in enhanced.faces[len(source.faces):]:
                    self.assertEqual(struct.unpack_from('<h', face, 8)[0], struct.unpack_from('<h', source.faces[0], 8)[0])
                    points = [tuple(round(c, 6) for c in enhanced.vertices[i][:3]) for i in struct.unpack_from('<3H', face, 2)]
                    for a, b in zip(points, points[1:] + points[:1]):
                        edges[tuple(sorted((a, b)))] += 1
                self.assertTrue(edges and all(count == 2 for count in edges.values()))
                for vertex in enhanced.vertices[len(source.vertices):]:
                    for axis in (0, 2):
                        self.assertTrue(min(v[axis] for v in source.vertices) <= vertex[axis] <= max(v[axis] for v in source.vertices))
                    self.assertTrue(source.vertices[0][1] < vertex[1] <= source.vertices[0][1] + mega_spectators.DEPTH + 1e-6)
                unchanged, limited = detail.scenery(source, MODEL.Mesh, names, len(names), 'objects', triangle_budget=0)
                self.assertEqual(unchanged.vertices, source.vertices)
                self.assertEqual(unchanged.faces, source.faces)
                self.assertEqual(limited['added_triangles'], 0)

    def test_blank_sign_ldhl_uvs_are_private_and_logo_and_numbers_stay_exact(self):
        vertices = [(-.9, 0., -.9, .72, -.98, 0., -1., 0.),
                    (.9, 0., -.9, .96, -.98, 0., -1., 0.),
                    (-.9, 0., .9, .72, -.55, 0., -1., 0.),
                    (.9, 0., .9, .02, -.1, 0., -1., 0.),
                    (0., 0., 0., .4, -.2, 0., -1., 0.)]
        make = lambda indices, slot: struct.pack('<H3Hh4B10x', 23, *indices, slot, 10, 20, 30, 255)
        # The logo shares vertex0 with the blank face but extends outside the
        # blank UV rectangle. Number paint uses identical UVs on another slot.
        original = MODEL.Mesh(bytes(48), vertices, [make((0, 1, 2), 0), make((0, 3, 4), 0), make((0, 1, 2), 1)])
        detailed = detail.sign(original, MODEL.Mesh, 2, texture_names=['LDHL', '200'])
        self.assertEqual(detailed.vertices[:len(vertices)], vertices)
        self.assertEqual(detailed.faces[1:3], original.faces[1:3])
        remapped = detailed.faces[0]
        self.assertEqual(remapped[:2], original.faces[0][:2])
        self.assertEqual(remapped[8:], original.faces[0][8:])
        for copied, source in zip(struct.unpack_from('<3H', remapped, 2), (0, 1, 2)):
            self.assertGreaterEqual(copied, len(vertices))
            self.assertEqual(detailed.vertices[copied][:3], vertices[source][:3])
            self.assertEqual(detailed.vertices[copied][3:5], (.99, -.99))
            self.assertEqual(detailed.vertices[copied][5:], vertices[source][5:])
        self.assertEqual(original.faces[0], make((0, 1, 2), 0))

    def test_advert_board_has_solid_backing_and_preserves_authored_front(self):
        original = panel(False)
        detailed, stats = detail.scenery(original, MODEL.Mesh, ['CASTROL'], 1, 'objects')
        self.assert_preserved(original, detailed)
        self.assert_valid_added_faces(original, detailed)
        self.assertEqual(stats['boards'], 1)
        added = detailed.vertices[len(original.vertices):]
        self.assertGreater(max(v[1] for v in added), 0.)
        self.assertTrue(all(v[1] >= 0. for v in added))
        self.assertEqual({struct.unpack_from('<h', f, 8)[0] for f in detailed.faces[len(original.faces):]}, {1})
        self.assertTrue(all(-5 <= v[0] <= 5 and -1.5 <= v[2] <= 0 for v in added))
        # Source artwork supplies the only front; added faces close the sides
        # and rear without overlapping the sponsor field.
        rear = False
        for face in detailed.faces[len(original.faces):]:
            vertices = [detailed.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
            normal_y = sum(vertex[6] for vertex in vertices)
            self.assertGreaterEqual(normal_y, 0., 'Generated front-facing metal overlaps sponsor lettering')
            rear |= normal_y > 1.5 and all(vertex[1] > 0. for vertex in vertices)
        self.assertTrue(rear, 'Backing must include an outward-facing rear surface')

    def test_opposing_silverstone_boards_leave_artwork_clear(self):
        raw = MODEL.read_source(dict(archive='data/silvfree.bfp', archive_crc32='960a335c',
                                    member=5, member_name='OBJECTS.C3D', member_crc32='a92b3b70'))
        header, nodes, meshes, textures = MODEL.decode(raw)
        names, offset = [], 0
        for _ in range(struct.unpack_from('<H', header, 16)[0]):
            name, length = struct.unpack_from('<12sI', textures, offset)
            names.append(name.rstrip(b'\0').decode('ascii'))
            offset += 16 + length
        records, offset = [], 0
        while offset < len(nodes):
            size = struct.unpack_from('<I', nodes, offset)[0]
            records.append(nodes[offset:offset + size])
            offset += size

        def world(node, point):
            record = records[node]
            matrix = [value / 65536 for value in struct.unpack_from('<16i', record, 8)]
            point = tuple(sum(matrix[axis + 4 * column] * point[column] for column in range(3)) + matrix[12 + axis]
                          for axis in range(3))
            parent = struct.unpack_from('<h', record, 4)[0]
            return world(parent, point) if parent >= 0 else point

        for pair in ((343, 351), (345, 357), (349, 353), (355, 369), (359, 361)):
            for front, back in (pair, pair[::-1]):
                with self.subTest(front=front, back=back):
                    source_front = meshes[struct.unpack_from('<H', records[front], 80)[0]]
                    source_back = meshes[struct.unpack_from('<H', records[back], 80)[0]]
                    corners = [world(front, source_front.vertices[index][:3])
                               for index in struct.unpack_from('<3H', source_front.faces[0], 2)]
                    # Source matrices are not perfectly orthogonal: use the
                    # actual geometric plane rather than lighting normals.
                    normal = MODEL.unit(MODEL.cross(MODEL.sub(corners[1], corners[0]), MODEL.sub(corners[2], corners[0])))
                    solid, stats = detail.scenery(source_back, MODEL.Mesh, names, len(names), 'objects')
                    self.assertEqual(stats['boards'], 1)
                    distances = [MODEL.dot(MODEL.sub(world(back, vertex[:3]), corners[0]), normal)
                                 for vertex in solid.vertices[len(source_back.vertices):]]
                    self.assertLess(max(distances), -.001, 'Generated solid crosses opposing original artwork')

    def test_tyre_panels_gain_hollow_round_stacks_on_the_actual_ground_slope(self):
        original = panel()
        detailed, stats = detail.scenery(original, MODEL.Mesh, ['TYRES'], 1, 'fence')
        self.assert_preserved(original, detailed)
        self.assert_valid_added_faces(original, detailed)
        self.assertEqual(stats['tyre_stacks'], 2)
        self.assertGreaterEqual(stats['tyres'], 4)
        self.assertLessEqual(stats['added_triangles'], 2048)
        added = detailed.vertices[len(original.vertices):]
        self.assertLess(min(v[2] for v in added), -.3)
        # The inner opening and shoulder heights are
        # physical mesh coordinates rather than a subdivided flat front.
        single = MODEL.Mesh(bytes(48), [], [])
        detail.tyre(single, (0., 0., 0.), .36, .32, 0)
        radial = [math.hypot(v[0], v[2]) for v in single.vertices]
        self.assertAlmostEqual(min(radial), .36 * .46)
        self.assertAlmostEqual(max(radial), .36)
        self.assertGreaterEqual(len({round(v[1], 6) for v in single.vertices}), 4)
        self.assertTrue(any(v[0] * v[5] + v[2] * v[7] < 0 for v in single.vertices))
        self.assertGreater(min(v[1] for v in added if v[0] > 4), min(v[1] for v in added if v[0] < 2))

    def test_steel_rail_has_folded_cross_section_and_closed_sector_ends(self):
        original = panel()
        detailed, stats = detail.scenery(original, MODEL.Mesh, ['BCBLO'], 1, 'fence')
        self.assert_preserved(original, detailed)
        self.assert_valid_added_faces(original, detailed)
        self.assertEqual(stats['rail_panels'], 1)
        self.assertEqual(stats['tyres'], 0)
        depths = {round(v[2], 6) for v in detailed.vertices[len(original.vertices):]}
        self.assertTrue({-.09, -.025, -.018, .012}.issubset(depths))
        self.assertEqual({struct.unpack_from('<h', f, 8)[0] for f in detailed.faces[len(original.faces):]}, {0})

    def test_semantics_and_triangle_budget_prevent_unbounded_arbitrary_extrusions(self):
        original = panel()
        for names, family in ((['BARBLUE2'], 'fence'), (['TYRES'], 'trees'), (['BUILDING'], 'objects')):
            mesh, stats = detail.scenery(original, MODEL.Mesh, names, 1, family)
            self.assertEqual(mesh.vertices, original.vertices)
            self.assertEqual(mesh.faces, original.faces)
            self.assertEqual(stats['added_triangles'], 0)
        detailed, stats = detail.scenery(original, MODEL.Mesh, ['TYRES'], 1, 'fence', triangle_budget=500)
        self.assertEqual(stats['tyre_stacks'], 1)
        self.assertLessEqual(stats['added_triangles'], 500)
        self.assert_preserved(original, detailed)
        original = panel(False)
        detailed, stats = detail.scenery(original, MODEL.Mesh, ['CASTROL'], 1, 'objects', triangle_budget=0)
        self.assertEqual(stats['added_triangles'], 0)
        self.assertEqual((detailed.vertices, detailed.faces), (original.vertices, original.faces))
