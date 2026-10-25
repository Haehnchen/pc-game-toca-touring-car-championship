"""Vegetation checks for groves forms."""

import math
import struct

from .environment import vegetation
from .geometry import Mesh, bounds
from .source_forms import source_grove, source_grove_lm2, source_grove_lm4
from .fixtures import VegetationCase


class SplitGroveVegetationTests(VegetationCase):
    source_factory = staticmethod(source_grove_lm2)
    texture_name = 'LMRTS2'
    foliage_slot = 9

    def test_two_islands_keep_source_dimensions_and_three_grounded_stems(self):
        old, new = bounds(self.source), bounds(self.result)
        for axis in (0, 2):
            for a, b in zip(old[axis], new[axis]):
                self.assertAlmostEqual(a, b, places=8)
        self.assertGreater(new[1][1] - new[1][0], 3.)
        self.assertLessEqual(new[1][1] - new[1][0], 3.600001)
        self.assertEqual((self.stats["grove_groups_lm2"], self.stats["grove_islands"], self.stats["trunks"]), (1, 2, 3))
        self.assertEqual(self.stats["removed_tree_triangles"], 4)
        self.assertEqual(self.stats["added_triangles"], len(self.result.faces) - 4)
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in self.result.faces}, {0, 1, 9})

    def test_no_body_branch_or_leaf_geometry_crosses_the_full_height_source_gap(self):
        xy, _, _, _, columns = vegetation.grove_frame(self.source)
        islands = vegetation.grove_lm2_islands(columns)
        left_edge, right_edge = xy(islands[0][1])[0], xy(islands[1][0])[0]
        self.assertGreater(right_edge - left_edge, 1.5)
        for face in self.result.faces:
            xs = [self.result.vertices[i][0] for i in struct.unpack_from("<3H", face, 2)]
            self.assertTrue(max(xs) <= left_edge + 1e-8 or min(xs) >= right_edge - 1e-8)

    def test_original_uv_interpolation_and_pure_foot_removal_support_front_and_rear(self):
        xy, uv_at, bottom, height, columns = vegetation.grove_frame(self.source)
        uv_area, areas = 0., [0.] * 8
        for face in self.result.faces:
            vertices = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            a, b, c = vertices
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
            self.assertGreater(sum(v * v for v in normal), 1e-14)
            self.assertGreater(sum(x * y for x, y in zip(normal, summed)), 0.)
            for i in range(8):
                angle = i * math.tau / 8
                areas[i] += max(0., normal[0] * math.cos(angle) + normal[1] * math.sin(angle)) * .5
            if struct.unpack_from("<h", face, 8)[0] != 0:
                continue
            uv_area += abs((b[3] - a[3]) * (c[4] - a[4]) - (b[4] - a[4]) * (c[3] - a[3])) * .5
            for x, _, z, u, v, *_ in vertices:
                u0, p0, u1, p1 = next((u0, p0, u1, p1) for (u0, p0), (u1, p1) in zip(columns, columns[1:])
                                      if p0[0] - 1e-8 <= x <= p1[0] + 1e-8)
                parameter = u0 + (u1 - u0) * (x - p0[0]) / (p1[0] - p0[0])
                expected_u, expected_v = uv_at(parameter, (bottom - z) / height)
                self.assertAlmostEqual(u, expected_u, places=8)
                self.assertAlmostEqual(v, expected_v, places=8)
                self.assertLessEqual(v + 1, 29 / 32 + 1e-8)
        self.assertAlmostEqual(uv_area, 2 * (.97 - 4 / 64) * (29 / 32 - .02), places=7)
        self.assertTrue(all(area > 10 for area in areas))

    def test_dispatcher_and_whole_square_leaf_charts(self):
        import mega_geometry

        result, stats = mega_geometry.scenery(self.source, Mesh, ["LMRTS2"], 2, "objects", vegetation_index=1, foliage_index=9)
        self.assertEqual(stats["grove_groups_lm2"], 1)
        self.assertEqual((result.vertices, result.faces), (self.result.vertices, self.result.faces))
        leaf_indices = sorted({i for f in result.faces if struct.unpack_from("<h", f, 8)[0] == 9
                               for i in struct.unpack_from("<3H", f, 2)})
        self.assertEqual(len(leaf_indices), stats["leaf_cards"] * 9)
        for offset in range(0, len(leaf_indices), 9):
            patch = [result.vertices[i] for i in leaf_indices[offset:offset + 9]]
            self.assertEqual((min(v[3] for v in patch), max(v[3] for v in patch)), (0., 1.))
            self.assertEqual((min(v[4] for v in patch), max(v[4] for v in patch)), (-1., 0.))
            self.assertAlmostEqual(math.dist(patch[0][:3], patch[2][:3]), math.dist(patch[0][:3], patch[6][:3]), places=7)


class ShortStemGroveVegetationTests(VegetationCase):
    source_factory = staticmethod(source_grove_lm4)
    texture_name = 'LMRTS4'
    foliage_slot = 8

    def test_source_width_foot_and_height_survive_with_nine_real_feet(self):
        old, new = bounds(self.source), bounds(self.result)
        for axis in (0, 2):
            for a, b in zip(old[axis], new[axis]):
                self.assertAlmostEqual(a, b, places=8)
        self.assertGreater(new[1][1] - new[1][0], 3.)
        self.assertLessEqual(new[1][1] - new[1][0], 3.600001)
        self.assertEqual((self.stats["grove_groups_lm4"], self.stats["trunks"], self.stats["grove_lobes"]), (1, 9, 3))
        self.assertEqual(self.stats["removed_tree_triangles"], 8)
        self.assertEqual(self.stats["added_triangles"], len(self.result.faces) - 8)
        feet = [v for v in self.result.vertices if abs(v[2] - old[2][1]) < 1e-8]
        self.assertGreaterEqual(len(feet), 9 * 4)
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in self.result.faces}, {0, 1, 8})

    def test_mirrored_complete_uv_charts_and_outward_winding_on_both_sides(self):
        xy, uv_at, bottom, height, columns = vegetation.grove_frame(self.source)
        mask = [f for f in self.result.faces if struct.unpack_from("<h", f, 8)[0] == 0]
        self.assertEqual(len(mask), self.stats["source_mask_triangles"])
        uv_area, areas = 0., [0.] * 8
        for face in self.result.faces:
            vertices = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            a, b, c = vertices
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
            self.assertGreater(sum(v * v for v in normal), 1e-14)
            self.assertGreater(sum(x * y for x, y in zip(normal, summed)), 0.)
            for i in range(8):
                angle = i * math.tau / 8
                areas[i] += max(0., normal[0] * math.cos(angle) + normal[1] * math.sin(angle)) * .5
            if struct.unpack_from("<h", face, 8)[0] != 0:
                continue
            uv_area += abs((b[3] - a[3]) * (c[4] - a[4]) - (b[4] - a[4]) * (c[3] - a[3])) * .5
            for x, y, z, u, v, *_ in vertices:
                segment = next(((u0, p0, u1, p1) for (u0, p0), (u1, p1) in zip(columns, columns[1:])
                                if p1[0] - 1e-8 <= x <= p0[0] + 1e-8))
                u0, p0, u1, p1 = segment
                parameter = u0 + (u1 - u0) * (x - p0[0]) / (p1[0] - p0[0])
                h = (bottom - z) / height
                expected_u, expected_v = uv_at(parameter, h)
                self.assertAlmostEqual(u, expected_u, places=8)
                self.assertAlmostEqual(v, expected_v, places=8)
                # The canopy ends exactly before pure source stem rows29..31;
                # real bark supplies that part once, without painted doubles.
                self.assertLessEqual(v + 1, 29 / 32 + 1e-8)
        self.assertAlmostEqual(uv_area, 2 * .97 * (29 / 32 - .02), places=7)
        self.assertTrue(all(area > 10 for area in areas))

    def test_root_charts_match_nine_native_two_pixel_strokes(self):
        _, uv_at, _, _, columns = vegetation.grove_frame(self.source)
        roots = vegetation.grove_lm4_roots(columns)
        self.assertEqual(len(roots), 9)
        for root, pixel in zip(roots, (14., 20., 26., 29., 34., 41., 46., 51., 55.)):
            self.assertAlmostEqual(uv_at(root, 0.)[0] * 64, pixel, places=8)

    def test_real_feet_match_source_two_texel_width_without_painted_duplicates(self):
        xy, uv_at, bottom, height, columns = vegetation.grove_frame(self.source)
        roots = vegetation.grove_lm4_roots(columns)
        bark_indices = {i for f in self.result.faces if struct.unpack_from("<h", f, 8)[0] == 1
                        for i in struct.unpack_from("<3H", f, 2)}
        feet = [self.result.vertices[i] for i in bark_indices if abs(self.result.vertices[i][2] - bottom) < 1e-8]
        for root in roots:
            center_x = xy(root)[0]
            ring = [v for v in feet if abs(v[0] - center_x) < .4]
            self.assertEqual(len(ring), 5)
            u0, p0, u1, p1 = next((u0, p0, u1, p1) for (u0, p0), (u1, p1) in zip(columns, columns[1:]) if root <= u1)
            material_span = uv_at(u1, 0.)[0] - uv_at(u0, 0.)[0]
            two_texel_width = 2 / 64 * abs(p1[0] - p0[0]) / material_span
            actual_width = max(v[0] for v in ring) - min(v[0] for v in ring)
            self.assertAlmostEqual(actual_width / two_texel_width, .97, places=7)
        # Keep an uninflated perimeter strip for the almost edge-on original
        # silhouette; the middle remains a substantial spatial crown body.
        for u in (.08, .92):
            self.assertLess(vegetation.grove_lm4_depth(u, .5), .18)
        self.assertGreater(vegetation.grove_lm4_depth(.5, .5), 1.5)

    def test_dispatcher_preserves_full_group_and_whole_sprites(self):
        import mega_geometry

        result, stats = mega_geometry.scenery(self.source, Mesh, ["LMRTS4"], 2, "objects", vegetation_index=1, foliage_index=8)
        self.assertEqual(stats["grove_groups_lm4"], 1)
        self.assertEqual((result.vertices, result.faces), (self.result.vertices, self.result.faces))
        leaf_indices = sorted({i for f in result.faces if struct.unpack_from("<h", f, 8)[0] == 8
                               for i in struct.unpack_from("<3H", f, 2)})
        self.assertEqual(len(leaf_indices), stats["leaf_cards"] * 9)
        for offset in range(0, len(leaf_indices), 9):
            patch = [result.vertices[i] for i in leaf_indices[offset:offset + 9]]
            self.assertEqual((min(v[3] for v in patch), max(v[3] for v in patch)), (0., 1.))
            self.assertEqual((min(v[4] for v in patch), max(v[4] for v in patch)), (-1., 0.))
            self.assertAlmostEqual(math.dist(patch[0][:3], patch[2][:3]), math.dist(patch[0][:3], patch[6][:3]), places=7)


class GroundGroveVegetationTests(VegetationCase):
    source_factory = staticmethod(source_grove)
    texture_name = 'LHRTWS4'
    foliage_slot = 7
    source_name = "TRGS18_1\0"

    def test_closed_group_keeps_foot_width_height_and_moderate_depth(self):
        self.assertEqual(self.result.header, self.source.header)
        old, new = bounds(self.source), bounds(self.result)
        for axis in (0, 2):
            for a, b in zip(old[axis], new[axis]):
                self.assertAlmostEqual(a, b, places=8)
        self.assertGreater(new[1][1] - new[1][0], 3.)
        self.assertLessEqual(new[1][1] - new[1][0], 3.600001)
        self.assertEqual((self.stats["grove_groups4"], self.stats["grove_lobes"], self.stats["trunks"]), (1, 3, 3))
        self.assertEqual(self.stats["removed_tree_triangles"], 4)
        self.assertEqual(self.stats["added_triangles"], len(self.result.faces) - 4)
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in self.result.faces}, {0, 1, 7})

    def test_both_curved_sides_keep_complete_original_motif_without_a_lower_band(self):
        xy, uv_at, bottom, height, _ = vegetation.grove_frame(self.source)
        mask = [f for f in self.result.faces if struct.unpack_from("<h", f, 8)[0] == 0]
        self.assertEqual(len(mask), self.stats["source_mask_triangles"])
        area = 0.
        front, rear = set(), set()
        seam = 32509 / 65536
        extent = bounds(self.source)
        for face in mask:
            vertices = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            a, b, c = [v[3:5] for v in vertices]
            area += abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) * .5
            for x, y, z, u, v, *_ in vertices:
                h = (bottom - z) / height
                native_u = ((x - extent[0][0]) / (2499 / 65536 - extent[0][0]) * seam if x <= 2499 / 65536
                            else seam + (x - 2499 / 65536) / (extent[0][1] - 2499 / 65536) * (1 - seam))
                expected_u, expected_v = uv_at(native_u, h)
                self.assertAlmostEqual(u, expected_u, places=8)
                self.assertAlmostEqual(v, expected_v, places=8)
                if abs(y - xy(native_u)[1]) > .01:
                    (front if y < xy(native_u)[1] else rear).add(round(h, 6))
        self.assertAlmostEqual(area, 2 * .97 * .97, places=7)
        self.assertEqual(front & rear, {.5})

    def test_full_square_leaf_uvs_and_real_front_back_side_winding(self):
        areas = [0.] * 8
        leaf_indices = set()
        for face in self.result.faces:
            indices = struct.unpack_from("<3H", face, 2)
            a, b, c = [self.result.vertices[i] for i in indices]
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
            self.assertGreater(sum(v * v for v in normal), 1e-14)
            self.assertGreater(sum(x * y for x, y in zip(normal, summed)), 0.)
            for i in range(8):
                angle = i * math.tau / 8
                areas[i] += max(0., normal[0] * math.cos(angle) + normal[1] * math.sin(angle)) * .5
            if struct.unpack_from("<h", face, 8)[0] == 7:
                leaf_indices.update(indices)
        self.assertTrue(all(area > 10 for area in areas))
        ordered = sorted(leaf_indices)
        self.assertEqual(len(ordered), self.stats["leaf_cards"] * 9)
        for offset in range(0, len(ordered), 9):
            # Every patch retains its whole square pixel chart after fitting.
            patch = [self.result.vertices[i] for i in ordered[offset:offset + 9]]
            self.assertEqual({round(v[3], 7) for v in patch}, {0., .5, 1.})
            self.assertEqual({round(v[4], 7) for v in patch}, {-1., -.5, 0.})
            self.assertAlmostEqual(math.dist(patch[0][:3], patch[2][:3]), math.dist(patch[0][:3], patch[6][:3]), places=7)

    def test_dispatcher_keeps_three_connected_masses(self):
        import mega_geometry

        result, stats = mega_geometry.scenery(self.source, Mesh, ["LHRTWS4"], 2, "objects",
                                               vegetation_index=1, foliage_index=7)
        self.assertEqual(stats["grove_groups4"], 1)
        self.assertEqual((result.vertices, result.faces), (self.result.vertices, self.result.faces))

    def test_depth_returns_to_source_perimeter_before_the_dense_outer_alpha(self):
        # Both LHRTWS4 masks remain dense near their lateral edges. Limiting
        # the edge bulge retains those source pixels from grazing front views.
        middle = vegetation.grove4_depth(.5, .5)
        self.assertGreater(middle, 1.4)
        for u in (.08, .92):
            self.assertLess(vegetation.grove4_depth(u, .5), middle * .56)
        for u in (0., .23, .51, .79, 1.):
            self.assertAlmostEqual(vegetation.grove4_depth(u, 0.), 0., places=7)
        self.assertAlmostEqual(vegetation.grove4_depth(0., .5), 0., places=7)
        self.assertAlmostEqual(vegetation.grove4_depth(1., .5), 0., places=6)


class GroveVegetationTests(VegetationCase):
    source_factory = staticmethod(source_grove)
    texture_name = 'LHRTWS3'
    foliage_slot = 4

    def test_group_keeps_source_width_height_while_gaining_moderate_depth(self):
        self.assertEqual(self.result.header, self.source.header)
        old, new = bounds(self.source), bounds(self.result)
        for axis in (0, 2):
            for source, target in zip(old[axis], new[axis]):
                self.assertAlmostEqual(source, target, places=7)
        self.assertGreaterEqual(new[1][1] - new[1][0], 3.)
        self.assertLessEqual(new[1][1] - new[1][0], 4.)
        self.assertEqual(self.stats["grove_groups"], 1)
        self.assertEqual(self.stats["removed_tree_triangles"], 4)
        self.assertGreater(len(self.result.faces), len(self.source.faces))

    def test_shared_scenery_dispatcher_returns_the_enhanced_grove_geometry(self):
        import mega_geometry

        model, stats = mega_geometry.scenery(self.source, Mesh, ["LHRTWS3"], 2, "objects",
                                             vegetation_index=1, foliage_index=4)
        self.assertEqual(stats["grove_groups"], 1)
        self.assertEqual(stats["added_triangles"], len(model.faces) - len(self.source.faces))
        self.assertEqual((model.vertices, model.faces), (self.result.vertices, self.result.faces))
        self.assertEqual({struct.unpack_from("<h", face, 8)[0] for face in model.faces}, {0, 1, 4})
        self.assertGreater(bounds(model)[1][1] - bounds(model)[1][0], 3.)

    def test_continuous_group_surfaces_preserve_original_full_motif_and_stem_gaps(self):
        source, result = self.source, self.result
        extent = bounds(source)
        bottom, height = extent[2][1], extent[2][1] - extent[2][0]
        seam_u, seam_x = 32509 / 65536, 2499 / 65536
        mask = [face for face in result.faces if struct.unpack_from("<h", face, 8)[0] == 0]
        self.assertEqual(len(mask), self.stats["source_mask_triangles"])
        self.assertGreater(len(mask), len(source.faces))
        uv_area = 0.
        heights = set()
        for face in mask:
            vertices = [result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            a, b, c = [vertex[3:5] for vertex in vertices]
            uv_area += abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) * .5
            for index in struct.unpack_from("<3H", face, 2):
                x, _, z, u, v, *_ = result.vertices[index]
                h = (bottom - z) / height
                heights.add(round(h, 7))
                self.assertLessEqual(h, 1. + 1e-9)
                self.assertAlmostEqual(v + 1, .99 - .97 * h, places=7)
                if x <= seam_x:
                    expected_u = .02 + (seam_u - .02) * (x - extent[0][0]) / (seam_x - extent[0][0])
                else:
                    expected_u = seam_u + (.99 - seam_u) * (x - seam_x) / (extent[0][1] - seam_x)
                self.assertAlmostEqual(u, expected_u, places=7)
        self.assertEqual((min(heights), max(heights)), (0., 1.))
        self.assertEqual(heights, {0., .5, 1.})
        # Each side covers the complete inset source rectangle exactly once.
        # The original alpha controls openings throughout the group motif.
        self.assertAlmostEqual(uv_area, 2 * .97 * .97, places=7)
        for face in result.faces:
            if struct.unpack_from("<h", face, 8)[0] == 4:
                for index in struct.unpack_from("<3H", face, 2):
                    self.assertGreater((bottom - result.vertices[index][2]) / height, .115)

    def test_valid_winding_supports_both_sides_and_nearly_edge_on_views(self):
        areas = [0.] * 8
        for face in self.result.faces:
            a, b, c = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
            self.assertGreater(sum(x * x for x in normal), 1e-14)
            self.assertGreater(sum(x * y for x, y in zip(normal, summed)), 0)
            for direction in range(8):
                angle = direction * math.tau / 8
                areas[direction] += max(0., normal[0] * math.cos(angle) + normal[1] * math.sin(angle)) * .5
        self.assertTrue(all(area > 10 for area in areas))
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in self.result.faces}, {0, 1, 4})

    def test_family_budget_keeps_a_complete_simple_group(self):
        cost = self.stats["added_triangles"]
        for budget in (cost - 1, cost, 1500):
            result, stats = vegetation.enhance(self.source, Mesh, ["LHRTWS3"], 1, triangle_budget=budget, foliage_index=4)
            if budget < cost:
                self.assertIs(result, self.source)
                self.assertEqual(stats, {"added_triangles": 0})
            else:
                self.assertEqual((result.vertices, result.faces, stats), (self.result.vertices, self.result.faces, self.stats))

    def test_root_positions_invert_the_original_static_uv_inset(self):
        _, material_uv, _, _, columns = vegetation.grove_frame(self.source)
        roots = vegetation.grove_root_parameters(columns)
        for root, pixel in zip(roots, (14.5, 44.5, 77.5, 97.5, 114.5)):
            self.assertAlmostEqual(material_uv(root, 0.)[0] * 128, pixel, places=8)
