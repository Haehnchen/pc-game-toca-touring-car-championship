"""Canonical scenery geometry checks for aircraft."""

import struct

from .environment import MODEL, detail
from .fixtures import SceneryCase


class StaticAircraftTests(SceneryCase):
    def test_static_aircraft_close_source_wing_planforms_and_replace_flat_gear(self):
        from collections import Counter
        import mega_static_aircraft

        def closed(mesh, first, end):
            edges = Counter()
            for face in mesh.faces[first:end]:
                points = [tuple(round(x, 8) for x in mesh.vertices[i][:3])
                          for i in struct.unpack_from('<3H', face, 2)]
                for a, b in zip(points, points[1:] + points[:1]):
                    edges[tuple(sorted((a, b)))] += 1
            self.assertTrue(edges and set(edges.values()) == {2})

        for member, meshes, names in self.static_aircraft_sources():
            selected = [mesh for mesh in meshes if mega_static_aircraft.matches(mesh, names)]
            self.assertEqual({mesh.header[:12].rstrip(b'\0').decode() for mesh in selected},
                             {'TH36APLN_1', 'TH37APLN_1'})
            for source in selected:
                original_bytes = source.encode()
                result, stats = detail.scenery(source, MODEL.Mesh, names, len(names), 'objects')
                self.assertEqual(stats['rebuilt_static_aircraft'], 1)
                self.assertTrue(all(isinstance(value, int) for value in stats.values()))
                self.assertEqual((stats['closed_wing_parts'], stats['aircraft_wheels'], stats['aircraft_legs']), (4, 3, 3))
                accumulated = Counter()
                accumulated.update(stats)
                accumulated.update(stats)
                self.assertEqual(accumulated['rebuilt_static_aircraft'], 2)
                description = mega_static_aircraft.describe(source, MODEL.Mesh, names)
                self.assertEqual(result.header, source.header)
                self.assertEqual(result.vertices[:len(source.vertices)], source.vertices)
                kept = [source.faces[i] for i in description['preserved_faces']]
                self.assertEqual(result.faces[:len(kept)], kept)
                self.assertEqual(source.encode(), original_bytes)
                repeated, _ = mega_static_aircraft.enhance(source, MODEL.Mesh, names)
                self.assertEqual(result.encode(), repeated.encode())
                name = source.header[:12].rstrip(b'\0').decode()
                patches = mega_static_aircraft.CONTRACTS[name]['patches']
                self.assertEqual(len(description['closed_wing_parts']), 4)
                repaired_source_normals = 0
                for part, (_, _, upper, _, _) in zip(description['closed_wing_parts'], patches):
                    closed(result, *part['output_face_range'])
                    diagonal_normals = {}
                    underside_normals = {}
                    shared_top = shared_bottom = 0
                    # The two original painted upper triangles retain their
                    # exact positions, UV anchors, smooth source normals and
                    # material slot. Shared diagonal points remain smooth.
                    for offset, source_id in enumerate(upper):
                        generated = result.faces[part['output_face_range'][0] + offset * 2]
                        original = source.faces[source_id]
                        records = lambda mesh, face: sorted(mesh.vertices[i][:5] for i in struct.unpack_from('<3H', face, 2))
                        self.assertEqual(records(result, generated), records(source, original))
                        self.assertEqual(generated[8:14], original[8:14])
                        # Independent of the contract's face selection: these
                        # upper skins must point upward in local -Z-up space.
                        generated_ids = struct.unpack_from('<3H', generated, 2)
                        self.assertLess(sum(result.vertices[i][7] for i in generated_ids), 0.)
                        original_normals = {source.vertices[i][:3]: MODEL.unit(source.vertices[i][5:])
                                            for i in struct.unpack_from('<3H', original, 2)}
                        for i in generated_ids:
                            vertex = result.vertices[i]
                            self.assertLessEqual(vertex[7], 0.)
                            if original_normals[vertex[:3]][2] <= 0:
                                self.assertEqual(vertex[5:], original_normals[vertex[:3]])
                            else:
                                # Source TH36 tail roots and TH37 right-wing
                                # root share normals across opposite skins.
                                # They must be repaired, never exempted from
                                # the outward normal/continuity assertions.
                                self.assertLess(vertex[7], 0.)
                                repaired_source_normals += 1
                            if vertex[:3] in diagonal_normals:
                                shared_top += 1
                                self.assertEqual(vertex[5:], diagonal_normals[vertex[:3]])
                            diagonal_normals[vertex[:3]] = vertex[5:]
                        underside = result.faces[part['output_face_range'][0] + offset * 2 + 1]
                        for i in struct.unpack_from('<3H', underside, 2):
                            vertex = result.vertices[i]
                            self.assertAlmostEqual(MODEL.dot(vertex[5:], vertex[5:]), 1.)
                            self.assertGreaterEqual(vertex[7], 0.)
                            if vertex[:3] in underside_normals:
                                shared_bottom += 1
                                self.assertEqual(vertex[5:], underside_normals[vertex[:3]])
                            underside_normals[vertex[:3]] = vertex[5:]
                    self.assertEqual((shared_top, shared_bottom), (2, 2))
                    if name == 'TH36APLN_1' and part['name'] == 'main-right':
                        # Regress the observed coincident48/49 side swap:
                        # upper49 has different UVs from lower48.
                        anchor = result.faces[part['output_face_range'][0]]
                        self.assertEqual(records(result, anchor), records(source, source.faces[49]))
                        self.assertNotEqual(records(result, anchor), records(source, source.faces[48]))
                self.assertGreater(repaired_source_normals, 0)
                self.assertEqual(len(description['landing_gear']), 3)
                for gear in description['landing_gear']:
                    closed(result, *gear['wheel_face_range'])
                    closed(result, *gear['leg_face_range'])
                    first, end = gear['wheel_face_range']
                    slots = {struct.unpack_from('<h', f, 8)[0] for f in result.faces[first:end]}
                    self.assertEqual(slots, {-1, names.index('TH36AI1')})
                prefix = MODEL.Mesh(source.header, source.vertices, kept)
                self.assert_valid_added_faces(prefix, result)
                for face in result.faces[len(kept):]:
                    slot, = struct.unpack_from('<h', face, 8)
                    self.assertTrue(slot == -1 or 0 <= slot < len(names))
                for axis in range(3):
                    # All added thickness stays within the original aircraft
                    # envelope; no placement or scale change is introduced.
                    self.assertGreaterEqual(min(v[axis] for v in result.vertices), min(v[axis] for v in source.vertices) - 1e-6)
                    self.assertLessEqual(max(v[axis] for v in result.vertices), max(v[axis] for v in source.vertices) + 1e-6)

    def test_static_aircraft_source_gate_and_atomic_budget_do_not_change_other_meshes(self):
        import mega_static_aircraft
        for _, meshes, names in self.static_aircraft_sources():
            for source in meshes:
                if not mega_static_aircraft.matches(source, names):
                    result, stats = mega_static_aircraft.enhance(source, MODEL.Mesh, names)
                    self.assertEqual(result.encode(), source.encode())
                    self.assertEqual(stats['rebuilt_static_aircraft'], 0)
            source = next(mesh for mesh in meshes if mega_static_aircraft.matches(mesh, names))
            changed = MODEL.Mesh(source.header, list(source.vertices), list(source.faces))
            changed.vertices[0] = tuple(x + 1 / 65536 if i == 0 else x for i, x in enumerate(changed.vertices[0]))
            self.assertFalse(mega_static_aircraft.matches(changed, names))
            changed_names = ['unrelated' if name == 'TH36AIR7' else name for name in names]
            self.assertFalse(mega_static_aircraft.matches(source, changed_names))
            result, stats = mega_static_aircraft.enhance(source, MODEL.Mesh, names, triangle_budget=0)
            self.assertEqual(result.encode(), source.encode())
            self.assertEqual(stats['added_triangles'], 0)
