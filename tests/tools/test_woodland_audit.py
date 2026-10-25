#!/usr/bin/env python3
"""Physical proximity witnesses and conservative M02 review coverage."""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import configured_data_root
spec = importlib.util.spec_from_file_location("woodland_audit", PROJECT / "tools/audit-woodland.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class PhysicalDistanceTests(unittest.TestCase):
    def test_filled_face_and_edge_proximity_cannot_use_object_center(self):
        triangle = ((0., 0., 0.), (1000., 0., 0.), (0., 1000., 0.))
        distance, face, route = audit.closest_triangle_segment(triangle, (1., 1., 3.), (2., 1., 3.))
        self.assertAlmostEqual(distance, 3.)
        self.assertEqual(face, (1., 1., 0.))
        self.assertEqual(route, (1., 1., 3.))
        distance, face, route = audit.closest_triangle_segment(triangle, (1., 1., -3.), (1., 1., 3.))
        self.assertAlmostEqual(distance, 0.)
        self.assertEqual(face, route)

    def test_finite_skew_parallel_and_degenerate_segments(self):
        for a, b, c, d, expected in (
            ((0, 0, 0), (10, 0, 0), (5, -2, 3), (5, 2, 3), 3),
            ((0, 0, 0), (10, 0, 0), (2, 2, 0), (8, 2, 0), 2),
            ((0, 0, 0), (0, 0, 0), (3, 4, 0), (3, 4, 0), 5),
            ((0, 0, 0), (1, 0, 0), (2, 1, 0), (2, 2, 0), 2 ** .5),
        ):
            first, second = audit.closest_segments(a, b, c, d)
            self.assertAlmostEqual(audit.math.dist(first, second), expected)

    def test_real_boundary_welds_duplicate_vertices_and_ignores_diagonal(self):
        vertices = [(0., 0., 0.), (10., 0., 0.), (10., 4., 0.), (0., 0., 0.), (10., 4., 0.), (0., 4., 0.)]
        faces = [struct.pack("<H3Hh4B10x", 0, *indices, 0, 167, 167, 167, 255) for indices in ((0, 1, 2), (3, 4, 5))]
        result = audit.measure(vertices, faces, [(5., 0., 3.), (6., 0., 3.), (5., 0., 4.)], [2., 2., 2.])
        self.assertEqual(result["geometric_boundary_edges"], 4)
        self.assertEqual(result["vertical_end_edges"], 2)
        self.assertAlmostEqual(result["face"]["distance_m"], 3.)
        self.assertAlmostEqual(result["corridor_lower_bound_m"], 1.)
        self.assertGreater(result["exposed_end"]["distance_m"], 3.)

    def test_original_transform_shifts_each_signed_product_before_addition(self):
        matrix = [0] * 16
        matrix[0], matrix[4], matrix[8], matrix[12] = 32768, 32768, -32768, 65536
        self.assertEqual(audit.transform((1, 1, 1), matrix), ((65536 - 1) / 65536, 0., 0.))

    def test_spatial_groups_are_seed_bounded_not_transitive_chains(self):
        rows = [dict(mesh=i, source_triangles=1, proximity=dict(face=dict(distance_m=i + 1, route_point=[i * 70., 0., 0.]))) for i in range(3)]
        groups = audit.spatial_groups(rows)
        self.assertEqual([group["meshes"] for group in groups], [[0, 1], [2]])


class AuditReportTests(unittest.TestCase):
    def test_exact_scope_pilots_and_donington_identity_are_explicit(self):
        report = audit.audit(configured_data_root())
        self.assertEqual((report["summary"]["tracks"], report["summary"]["instances"], report["summary"]["types"]), (9, 501, 440))
        self.assertEqual(report["summary"]["source_triangles"], 1926)
        pilots = {(row["track"], row["mesh"]) for row in report["instances"] if row["geometry"] == "accepted-pilot"}
        self.assertEqual(pilots, {(6, 23), (6, 24), (8, 19), (8, 0), (4, 100), (4, 101), (4, 14), (4, 15)})
        reuse = report["donington_reuse"]
        self.assertTrue(reuse["source_member_byteidentical"])
        self.assertTrue(reuse["materials_and_images_identical"])
        self.assertFalse(reuse["route_identical"])
        self.assertEqual(reuse["source_member_crc32"], "0e4e8cd8")
        self.assertEqual(reuse["placement_differences"], [])
        self.assertEqual(reuse["geometry_differences"], [])
        self.assertEqual(set(report["dependencies"]), {"woodland_geometry_crc32", "material_mapping_crc32"})
        for track in report["tracks"]:
            rows = [row for row in report["instances"] if row["track"] == track["track"]]
            grouped = [index for group in track["spatial_groups"] for index in group["meshes"]]
            self.assertEqual(sorted(grouped), sorted(row["mesh"] for row in rows))
            near = [row["mesh"] for row in rows if row["risk"] == "near-review"]
            required = [index for group in track["near_review_groups"] for index in group["required_meshes"]]
            self.assertEqual(sorted(required), sorted(near))
            for group in track["near_review_groups"]:
                self.assertEqual(group["status"], "native-review-required")
            for row in rows:
                if row["geometry"] == "original":
                    self.assertEqual(row["source_crc32"], row["output_mesh_crc32"])
                else:
                    self.assertNotEqual(row["source_crc32"], row["output_mesh_crc32"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
