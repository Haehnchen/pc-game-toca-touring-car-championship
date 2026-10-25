"""Canonical source readers and independent mesh/UV assertions."""

import math
import struct
import unittest

from .environment import MODEL


FLEET_TRACKS = (
    (3, 'bran', 'ecc577c7', ('90ddbd12', '09163ac2', 'd438957e')),
    (6, 'crof', 'e4399113', ('93406c70', '88589e3a', '4b8aa61f')),
    (7, 'knoc', '7e00bade', ('24a4dac8', '10de37e5', '7dbcd0be')),
    (8, 'snet', '4111866a', ('d5bdef63', '19d8ccf4', '6023011d')),
)


def source_models(prefix, archive_crc, member, member_crc):
    """Return fresh meshes and exact authored slot names for one CRC-bound member."""
    label = {5: 'OBJECTS.C3D', 6: 'LOWOBJ.C3D', 7: 'ZEROBJ.C3D'}[member]
    raw = MODEL.read_source(dict(archive=f'data/{prefix}free.bfp', archive_crc32=archive_crc,
                                member=member, member_name=label, member_crc32=member_crc))
    header, _, meshes, textures = MODEL.decode(raw)
    names, offset = [], 0
    for _ in range(struct.unpack_from('<H', header, 16)[0]):
        name, length = struct.unpack_from('<12sI', textures, offset)
        names.append(name.rstrip(b'\0').decode('ascii'))
        offset += 16 + length
    return meshes, names


class SceneryCase(unittest.TestCase):
    def static_fleet_sources(self):
        for track, prefix, archive_crc, members in FLEET_TRACKS:
            for member, crc in zip((5, 6, 7), members):
                yield track, member, *source_models(prefix, archive_crc, member, crc)

    def static_vectra_sources(self):
        for track, prefix, archive_crc, members in FLEET_TRACKS:
            for member, crc in zip((5, 6), members):
                yield track, member, *source_models(prefix, archive_crc, member, crc)

    def static_platform_sources(self):
        for track, prefix, archive_crc, high_crc, low_crc in (
                (0, 'donl', '46885a20', '5f670bb9', '115e5886'),
                (2, 'thru', '473c8fe8', 'a17b2723', 'b816e715'),
                (4, 'oult', 'e6e258cb', '5ad0e21a', 'd19a42f6'),
                (5, 'dons', 'f07db8b7', 'f60fd8a0', 'f1caf737')):
            for member, crc in ((5, high_crc), (6, low_crc)):
                yield track, member, *source_models(prefix, archive_crc, member, crc)

    def static_aircraft_sources(self):
        for member, crc in ((5, 'a17b2723'), (6, 'b816e715')):
            yield member, *source_models('thru', '473c8fe8', member, crc)


    def remap_face_slots(self, faces, source_names, target_names):
        result = []
        for face in faces:
            slot, = struct.unpack_from('<h', face, 8)
            if slot >= 0:
                face = face[:8] + struct.pack('<h', target_names.index(source_names[slot])) + face[10:]
            result.append(face)
        return result

    def assert_relief_source_uv(self, source, source_faces, vertex, side):
        point = (vertex[0] - side * .03, *vertex[1:3])
        candidates = []
        for source_id in source_faces:
            records = [source.vertices[i] for i in struct.unpack_from('<3H', source.faces[source_id], 2)]
            a, b, c = [v[:3] for v in records]
            ab, ac, ap = MODEL.sub(b, a), MODEL.sub(c, a), MODEL.sub(point, a)
            aa, cc, bc = MODEL.dot(ab, ab), MODEL.dot(ac, ac), MODEL.dot(ab, ac)
            denominator = aa * cc - bc * bc
            d, e = MODEL.dot(ap, ab), MODEL.dot(ap, ac)
            u, v = (cc * d - bc * e) / denominator, (aa * e - bc * d) / denominator
            weights = (1 - u - v, u, v)
            records_at_point = [sum(weights[j] * records[j][axis] for j in range(3)) for axis in range(5)]
            candidates.append((sum(max(0., -w) for w in weights), records_at_point))
        # A real source seam can assign different UVs to the same XYZ edge
        # (Caravan15/29). Tiny barycentric roundoff cannot select one side's
        # phase for the other. Require an actually containing source triangle
        # and its complete XYZ+UV correspondence, keeping the exact tolerance.
        containing = [c for c in candidates if c[0] <= 1e-7]
        self.assertTrue(containing, 'Relief point lies outside every original source triangle')
        actual = (*point, *vertex[3:5])
        _, expected = min(containing, key=lambda c: max(abs(a - b) for a, b in zip(actual, c[1])))
        for actual, reference in zip((*point, *vertex[3:5]), expected):
            self.assertAlmostEqual(actual, reference, places=7)

    def assert_preserved(self, before, after):
        self.assertEqual(before.header, after.header)
        self.assertEqual(before.vertices, after.vertices[:len(before.vertices)])
        self.assertEqual(before.faces, after.faces[:len(before.faces)])

    def assert_valid_added_faces(self, before, after):
        for face in after.faces[len(before.faces):]:
            indices = struct.unpack_from('<3H', face, 2)
            self.assertLess(max(indices), len(after.vertices))
            a, b, c = (after.vertices[index] for index in indices)
            normal = MODEL.cross(MODEL.sub(b[:3], a[:3]), MODEL.sub(c[:3], a[:3]))
            self.assertGreater(MODEL.dot(normal, normal), 1e-14)
            averaged = MODEL.add(MODEL.add(a[5:], b[5:]), c[5:])
            self.assertGreater(MODEL.dot(normal, averaged), 0)
        self.assertTrue(all(math.isfinite(component) for v in after.vertices for component in v))
