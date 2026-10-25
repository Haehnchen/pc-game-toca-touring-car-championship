#!/usr/bin/env python3
"""Exact GLB face-corner preservation when compacting unused/duplicate vertices."""

from collections import OrderedDict
import argparse
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import vehicle_glb

MESHOPT_ENCODER = None


def face(indices, color):
    return struct.pack("<H3Hh4B10x", 0, *indices, -1, *color)


def output_record(vertex):
    x, y, z, u, v, nx, ny, nz = vertex
    return struct.pack("<8f", x, y, z, nx, ny, nz, u - 1 if u > 1 else u, v + 1)


class RenaultGlbTests(unittest.TestCase):
    @staticmethod
    def unpack(encoded):
        length, = struct.unpack_from("<I", encoded, 12)
        document = json.loads(encoded[20:20 + length])
        return document, encoded[28 + length:]

    def test_compaction_preserves_every_face_corner_and_material_group(self):
        a = (0., 0., 0., .25, -.5, 0., 0., 1.)
        b = (1., 0., 0., .5, -.5, 0., 0., 1.)
        c = (0., 1., 0., .25, -.25, 0., 0., 1.)
        normal_seam = (*a[:5], 0., 1., 0.)
        uv_seam = (*a[:3], .75, -.5, *a[5:])
        signed_zero = (*a[:5], -0., 0., 1.)
        red, blue, green = (255, 20, 30, 255), (20, 40, 255, 255), (10, 200, 20, 255)
        mesh = SimpleNamespace(
            header=b"fixture".ljust(48, b"\0"),
            vertices=[(99., 99., 99., 0., 0., 1., 0., 0.), a, b, c, a, normal_seam, uv_seam, signed_zero],
            faces=[face((1, 2, 3), red), face((4, 3, 2), red), face((5, 2, 3), blue), face((6, 2, 3), red), face((7, 2, 3), green)],
        )
        header = bytearray(32)
        struct.pack_into("<H", header, 16, 0)
        encoded, stats = vehicle_glb.encode(header, b"", [mesh], b"")
        self.assertEqual(struct.unpack_from("<III", encoded), (0x46546c67, 2, len(encoded)))
        length, kind = struct.unpack_from("<II", encoded, 12)
        self.assertEqual(kind, 0x4e4f534a)
        document = json.loads(encoded[20:20 + length])
        binary_length, binary_kind = struct.unpack_from("<II", encoded, 20 + length)
        self.assertEqual(binary_kind, 0x004e4942)
        binary = encoded[28 + length:28 + length + binary_length]

        expected = OrderedDict()
        for source_face in mesh.faces:
            color = tuple(source_face[10:14])
            expected.setdefault(color, []).extend(output_record(mesh.vertices[i]) for i in struct.unpack_from("<3H", source_face, 2))
        primitives = document["meshes"][0]["primitives"]
        self.assertEqual(len(primitives), len(expected))
        common_attributes = primitives[0]["attributes"]
        for primitive, (color, corners) in zip(primitives, expected.items()):
            self.assertEqual(primitive["attributes"], common_attributes)
            self.assertEqual(document["materials"][primitive["material"]]["pbrMetallicRoughness"]["baseColorFactor"], [value / 255 for value in color])
            vertices = document["accessors"][primitive["attributes"]["POSITION"]]
            vertex_view = document["bufferViews"][vertices["bufferView"]]
            indices = document["accessors"][primitive["indices"]]
            index_view = document["bufferViews"][indices["bufferView"]]
            ids = struct.unpack_from(f"<{indices['count']}H", binary, index_view["byteOffset"])
            actual = [binary[vertex_view["byteOffset"] + i * 32:vertex_view["byteOffset"] + (i + 1) * 32] for i in ids]
            self.assertEqual(actual, corners)
            self.assertEqual(vertices["count"], 6)
            self.assertEqual(vertices["min"], [0., 0., 0.])
            self.assertEqual(vertices["max"], [1., 1., 0.])
        self.assertEqual(stats["source_mesh_vertices"], 8)
        self.assertEqual(stats["removed_unused_vertices"], 1)
        self.assertEqual(stats["merged_duplicate_vertices"], 1)
        self.assertEqual(stats["packed_mesh_vertices"], 6)
        self.assertEqual(stats["vertex_bytes_saved"], 64)
        self.assertEqual(vehicle_glb.encode(header, b"", [mesh], b"")[0], encoded)

    def test_identical_mesh_payloads_share_buffer_views(self):
        mesh = SimpleNamespace(header=b"fixture".ljust(48, b"\0"),
                               vertices=[(0., 0., 0., 0., 0., 0., 0., 1.),
                                         (1., 0., 0., 0., 0., 0., 0., 1.),
                                         (0., 1., 0., 0., 0., 0., 0., 1.)],
                               faces=[face((0, 1, 2), (255, 255, 255, 255))])
        encoded, stats = vehicle_glb.encode(bytes(32), b"", [mesh, mesh], b"")
        document, _ = self.unpack(encoded)
        self.assertEqual(len(document["bufferViews"]), 2)
        self.assertEqual(stats["duplicate_buffer_bytes_saved"], 102)
        first, second = [entry["primitives"][0] for entry in document["meshes"]]
        for attribute in ("POSITION", "NORMAL", "TEXCOORD_0"):
            self.assertEqual(document["accessors"][first["attributes"][attribute]]["bufferView"],
                             document["accessors"][second["attributes"][attribute]]["bufferView"])

    def test_generated_housing_faces_receive_ghost_only_primitive_metadata(self):
        mesh = SimpleNamespace(header=b"floor".ljust(48, b"\0"),
                               vertices=[(0., 0., 0., 0., 0., 0., 0., 1.),
                                         (1., 0., 0., 0., 0., 0., 0., 1.),
                                         (0., 1., 0., 0., 0., 0., 0., 1.)],
                               faces=[face((0, 1, 2), (30, 30, 30, 255)),
                                      face((0, 2, 1), (30, 30, 30, 255))])
        ghost = {0: (0., 0., .125), 1: (1., 0., .125), 2: (0., 1., .125)}
        encoded, _ = vehicle_glb.encode(bytes(32), b"", [mesh], b"", ghost_hidden_starts={0: 1}, ghost_positions={0: ghost})
        document, binary = self.unpack(encoded)
        primitives = document["meshes"][0]["primitives"]
        self.assertEqual(len(primitives), 2)
        self.assertNotIn("extras", primitives[0])
        self.assertEqual(primitives[1]["extras"], {"toca_ghost_hidden": True})
        self.assertEqual([document["accessors"][part["indices"]]["count"] for part in primitives], [3, 3])
        accessor_ids = [part['attributes']['_TOCA_GHOST_POSITION'] for part in primitives]
        self.assertEqual(accessor_ids[0], accessor_ids[1])
        accessor = document['accessors'][accessor_ids[0]]
        self.assertEqual((accessor['count'], accessor['type'], accessor['componentType']), (3, 'VEC3', 5126))
        view = document['bufferViews'][accessor['bufferView']]
        values = [struct.unpack_from('<3f', binary, view['byteOffset'] + 12 * index) for index in range(3)]
        self.assertEqual(values, list(ghost.values()))

    def test_ghost_position_participates_in_vertex_compaction(self):
        vertex = (0., 0., 0., 0., 0., 0., 0., 1.)
        mesh = SimpleNamespace(header=b'floor'.ljust(48, b'\0'), vertices=[vertex] * 3,
                               faces=[face((0, 1, 2), (30, 30, 30, 255))])
        ghost = {0: (0., 0., 0.), 1: (0., 0., .125), 2: (0., 0., .125)}
        encoded, stats = vehicle_glb.encode(bytes(32), b'', [mesh], b'', ghost_hidden_starts={0: 0}, ghost_positions={0: ghost})
        document, _ = self.unpack(encoded)
        primitive = document['meshes'][0]['primitives'][0]
        self.assertEqual(stats['packed_mesh_vertices'], 2)
        self.assertEqual(document['accessors'][primitive['attributes']['POSITION']]['count'], 2)
        self.assertEqual(document['accessors'][primitive['attributes']['_TOCA_GHOST_POSITION']]['count'], 2)
        with self.assertRaisesRegex(ValueError, 'explicit positions'):
            vehicle_glb.encode(bytes(32), b'', [mesh], b'', ghost_hidden_starts={0: 0})
        with self.assertRaisesRegex(ValueError, 'cover all'):
            vehicle_glb.encode(bytes(32), b'', [mesh], b'', ghost_hidden_starts={0: 0}, ghost_positions={0: {0: ghost[0]}})

    def test_meshopt_batch_compresses_geometry_without_storing_raw_fallback(self):
        if MESHOPT_ENCODER is None:
            self.skipTest("pass --meshopt-encoder to exercise native compression")
        mesh = SimpleNamespace(
            header=b"grid".ljust(48, b"\0"),
            vertices=[(float(i % 32), float(i // 32), 0., (i % 32) / 32, -(i // 32) / 32, 0., 0., 1.) for i in range(1024)],
            faces=[face((i, i + 1, i + 32), (255, 255, 255, 255)) for i in range(991) if i % 32 != 31])
        raw, _ = vehicle_glb.encode(bytes(32), b"", [mesh], b"")
        encoded, stats = vehicle_glb.encode(bytes(32), b"", [mesh], b"", meshopt_encoder=MESHOPT_ENCODER)
        document, binary = self.unpack(encoded)
        self.assertLess(len(encoded), len(raw) // 2)
        self.assertIn("EXT_meshopt_compression", document["extensionsRequired"])
        self.assertEqual(stats["meshopt_buffer_count"], 2)
        self.assertGreater(stats["meshopt_bytes_saved"], 20000)
        self.assertEqual(len(document["buffers"]), 2)
        self.assertLessEqual(len(binary) - document["buffers"][0]["byteLength"], 3)
        self.assertGreater(document["buffers"][1]["byteLength"], len(binary))
        self.assertTrue(document["buffers"][1]["extensions"]["EXT_meshopt_compression"]["fallback"])
        self.assertEqual([view["extensions"]["EXT_meshopt_compression"]["mode"] for view in document["bufferViews"]],
                         ["ATTRIBUTES", "INDICES"])
        self.assertEqual(vehicle_glb.encode(bytes(32), b"", [mesh], b"", meshopt_encoder=MESHOPT_ENCODER)[0], encoded)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--meshopt-encoder", type=Path)
    args, remaining = parser.parse_known_args()
    MESHOPT_ENCODER = args.meshopt_encoder
    sys.argv = [sys.argv[0], *remaining]
    unittest.main()
