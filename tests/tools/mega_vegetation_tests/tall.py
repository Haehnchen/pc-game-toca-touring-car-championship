"""Vegetation checks for tall forms."""

import math
import struct

from .environment import PROJECT, vegetation
from .geometry import Mesh, assert_contained_tall_tree, bounds
from .alpha import crown_alpha_mask
from .source_forms import source_tree2b, source_tree7
from .fixtures import VegetationCase


class TallVegetationTests(VegetationCase):
    source_factory = staticmethod(source_tree2b)
    texture_name = 'LHRTS2'
    foliage_slot = 5

    def test_dense_tall_crown_preserves_source_bounds_foot_and_appended_materials(self):
        self.assertEqual(self.result.header, self.source.header)
        assert_contained_tall_tree(self, self.source, self.result)
        self.assertEqual(self.stats["near_trees2b"], 1)
        self.assertEqual((self.stats["trunks"], self.stats["branches"], self.stats["interior_clusters"]), (1, 0, 0))
        self.assertEqual(self.stats["leaf_cards"], 20)
        self.assertEqual(self.stats["source_mask_triangles"], 120)
        self.assertEqual({struct.unpack_from("<h", face, 8)[0] for face in self.result.faces}, {0, 1, 5})
        for vertex in self.result.vertices:
            self.assertTrue(all(math.isfinite(x) for x in vertex))
            self.assertAlmostEqual(sum(x * x for x in vertex[5:]), 1., places=7)
        for face in self.result.faces:
            a, b, c = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
            self.assertGreater(sum(x * y for x, y in zip(normal, summed)), 0)

    def test_curved_full_sprites_preserve_square_proportions(self):
        foliage = [f for f in self.result.faces if struct.unpack_from("<h", f, 8)[0] == 5]
        for start in range(0, len(foliage), 8):
            indices = sorted({i for face in foliage[start:start + 8] for i in struct.unpack_from("<3H", face, 2)})
            vertices = [self.result.vertices[i] for i in indices]
            self.assertEqual({v[3:5] for v in vertices if v[3] in (0., 1.) and v[4] in (-1., 0.)},
                             {(0., -1.), (1., -1.), (0., 0.), (1., 0.)})
            width = math.dist(vertices[0][:3], vertices[2][:3])
            card_height = math.dist(vertices[0][:3], vertices[6][:3])
            self.assertGreater(width / card_height, .85)
            self.assertLess(width / card_height, 1.25)

    def test_actual_image_alpha_closes_the_projected_crown_core_from_eight_views(self):
        paths = {0: PROJECT / "assets/models/mega/textures/vegetation-lhrts2-donington.webp",
                 5: PROJECT / "assets/models/mega/textures/vegetation-leaf2-donington.webp"}
        if not all(path.is_file() for path in paths.values()):
            self.skipTest("The authored LHRTS2 images are unavailable")
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is required to inspect the authored WebP alpha")
        textures = {}
        for slot, path in paths.items():
            with Image.open(path) as image:
                textures[slot] = (image.width, image.height, image.convert("RGBA").getchannel("A").tobytes())
        # Horizontal and vertical coverage must coincide in the rendered alpha.
        # Merely overlapping card-height intervals misses vertical sky columns.
        for direction in range(8):
            mask, width, height = crown_alpha_mask(self.result, textures, direction * math.tau / 8, self.source)
            core = [mask[y * width + x] for y in range(int(height * .15), int(height * .75))
                    for x in range(int(width * .32), int(width * .68))]
            self.assertGreater(sum(core) / len(core), .92, direction)
        # Opaque sprite samples must lie outside the filler across the body,
        # not merely hide inside the solid core or collect into a spiky top.
        source_bounds = bounds(self.source)
        cx, cy = [(low + high) * .5 for low, high in source_bounds[:2]]
        rx, ry = [(high - low) * .5 for low, high in source_bounds[:2]]
        bottom = source_bounds[2][1]
        tree_height = bottom - source_bounds[2][0]
        tw, th, alpha = textures[5]
        indices = {i for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 5
                   for i in struct.unpack_from("<3H", face, 2)}
        exterior_bands = set()
        for index in indices:
            x, y, z, u, v, *_ = self.result.vertices[index]
            tx, ty = min(tw - 1, int(u * tw)), min(th - 1, int((v + 1) * th))
            h = (bottom - z) / tree_height
            radius = math.hypot((x - cx) / rx, (y - cy) / ry)
            if alpha[ty * tw + tx] >= 128 and radius > vegetation.tree2b_radius(h) * .87:
                if .20 <= h < .80:
                    exterior_bands.add(int((h - .20) / .20))
        self.assertEqual(exterior_bands, {0, 1, 2})

    def test_short_stem_joins_a_broad_middle_and_gradually_narrowing_upper_crown(self):
        bottom = bounds(self.source)[2][1]
        height = bottom - bounds(self.source)[2][0]

        def section_widths(h):
            z = bottom - height * h
            points = []
            for face in self.result.faces:
                vertices = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                    if min(a[2], b[2]) <= z <= max(a[2], b[2]) and a[2] != b[2]:
                        t = (z - a[2]) / (b[2] - a[2])
                        points.append(tuple(a[k] + t * (b[k] - a[k]) for k in range(3)))
            return [max(p[k] for p in points) - min(p[k] for p in points) for k in (0, 1)]

        stem, lower, middle = [section_widths(h) for h in (.05, .25, .55)]
        for axis in (0, 1):
            self.assertLess(stem[axis], middle[axis] * .10)
            self.assertGreater(lower[axis], middle[axis] * .80)
        # Newly modeled crown surfaces use only the motif's dense central 70 %.
        # The original transparent picture margins must not open their sides.
        source_indices = {i for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 0
                          for i in struct.unpack_from("<3H", face, 2)}
        for index in source_indices:
            vertex = self.result.vertices[index]
            _, _, z, u, v, *_ = vertex
            self.assertGreaterEqual(u, .15)
            self.assertLessEqual(u, .85)
            self.assertAlmostEqual(v + 1, .99 - .97 * (bottom - z) / height, places=7)

    def test_crown_texture_spreads_evenly_around_curved_sides(self):
        indices = {i for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 0
                   for i in struct.unpack_from("<3H", face, 2)}
        rings = {}
        for index in indices:
            vertex = self.result.vertices[index]
            rings.setdefault(round(vertex[2], 7), set()).add(round(vertex[3], 12))
        for values in rings.values():
            values = sorted(values)
            steps = [second - first for first, second in zip(values, values[1:])]
            # Equal angular intervals need equal texture intervals, including
            # side poles. An X projection collapses the UV step near those poles.
            self.assertGreaterEqual(len(steps), 4)
            for step in steps:
                self.assertAlmostEqual(step, sum(steps) / len(steps), places=10)

    def test_rounded_skin_tapers_both_axes_without_repeating_full_uv_per_ring(self):
        indices = {i for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 0
                   for i in struct.unpack_from("<3H", face, 2)}
        rings = {}
        for index in indices:
            vertex = self.result.vertices[index]
            rings.setdefault(round(vertex[2], 7), []).append(vertex)
        widths = []
        for vertices in rings.values():
            extent = [(min(v[a] for v in vertices), max(v[a] for v in vertices)) for a in (0, 1)]
            widths.append((vertices[0][2], extent[0][1] - extent[0][0], extent[1][1] - extent[1][0],
                           max(v[3] for v in vertices) - min(v[3] for v in vertices)))
        widths.sort()
        # The top, upper shoulder and round lower attachment must physically
        # taper in X as well as Y. A full-width X slab exposes transparent image
        # borders to side views, producing open halves and horizontal strips.
        top, shoulder, middle, lower = widths[0], widths[1], widths[3], widths[-2]
        for axis in (1, 2):
            self.assertLess(top[axis], middle[axis] * .10)
            self.assertLess(shoulder[axis], middle[axis] * .85)
            self.assertGreater(shoulder[axis], middle[axis] * .60)
            self.assertLess(lower[axis], middle[axis])
        self.assertLess(top[3], .10)
        self.assertLess(shoulder[3], .85)
        self.assertGreater(middle[3], .65)
        self.assertLessEqual(middle[3], .70 + 1e-8)

    def test_dispatcher_keeps_the_complete_tree(self):
        import mega_geometry

        result, stats = mega_geometry.scenery(self.source, Mesh, ["LHRTS2"], 2, "objects",
                                              vegetation_index=1, foliage_index=5)
        self.assertEqual((result.vertices, result.faces, stats), (self.result.vertices, self.result.faces, self.stats))
        self.assertEqual(stats["added_triangles"], len(result.faces) - len(self.source.faces))


class SlenderVegetationTests(VegetationCase):
    source_factory = staticmethod(source_tree7)
    texture_name = 'LHRTS7'
    foliage_slot = 3

    def test_source_alpha_envelope_sets_visible_height_and_lower_rounded_crown(self):
        old, new = bounds(self.source), bounds(self.result)
        foot, radius, height = vegetation.tree7_envelope(self.source)
        self.assertEqual(self.result.header, self.source.header)
        self.assertAlmostEqual(height, 10.935811584472656, places=7)
        self.assertAlmostEqual(new[2][0], foot[2] - height, places=7)
        self.assertAlmostEqual(new[2][1], old[2][1], places=7)
        for source, target in zip(old, new):
            self.assertGreaterEqual(target[0], source[0])
            self.assertLessEqual(target[1], source[1])
        for source, target in zip(old[:2], new[:2]):
            self.assertLess(target[1] - target[0], (source[1] - source[0]) * .55)
        lower, upper = [], []
        for vertex in self.result.vertices:
            h = (foot[2] - vertex[2]) / height
            r = math.hypot(vertex[0] - foot[0], vertex[1] - foot[1])
            if .18 <= h <= .42:
                lower.append(r)
            if .75 <= h <= .97:
                upper.append(r)
        self.assertGreater(max(lower), max(upper) * 1.45)
        for axis in (0, 1):
            self.assertAlmostEqual(sum(v[axis] for v in self.result.vertices[:4]) / 4, foot[axis], places=7)

    def test_complete_sprite_charts_and_ascending_branches_are_valid_and_repeatable(self):
        repeated, stats = vegetation.enhance(self.source, Mesh, [b"LHRTS7\0"], 1, foliage_index=3)
        self.assertEqual((repeated.vertices, repeated.faces, stats), (self.result.vertices, self.result.faces, self.stats))
        self.assertEqual(self.stats["near_trees7"], 1)
        self.assertEqual(self.stats["branches"], 0)
        self.assertEqual(self.stats["leaf_cards"], 2 * self.stats["crown_clusters"])
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in self.result.faces}, {1, 3})
        foliage = [face for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 3]
        centers = []
        for start in range(0, len(foliage), 16):
            indices = {i for face in foliage[start:start + 16] for i in struct.unpack_from("<3H", face, 2)}
            vertices = [self.result.vertices[i] for i in indices]
            centers.append(sum(v[2] for v in vertices) / len(vertices))
            self.assertEqual({v[3] for v in vertices}, {0., .5, 1.})
            self.assertEqual({v[4] for v in vertices}, {-1., -.5, 0.})
        outer = centers[:self.stats["crown_clusters"] - self.stats["interior_clusters"]]
        self.assertTrue(all(first > second for first, second in zip(outer, outer[1:])))
        for face in self.result.faces:
            a, b, c = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
            self.assertGreater(sum(x * x for x in normal), 1e-14)
            self.assertGreater(sum(x * y for x, y in zip(normal, summed)), 0)

    def test_small_upper_foliage_regions_overlap_to_the_pointed_tip(self):
        foot, _, height = vegetation.tree7_envelope(self.source)
        foliage = [face for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 3]
        intervals = []
        for start in range(0, len(foliage), 8):
            indices = {i for face in foliage[start:start + 8] for i in struct.unpack_from("<3H", face, 2)}
            vertices = [self.result.vertices[i] for i in indices]
            low, high = min(v[2] for v in vertices), max(v[2] for v in vertices)
            center = (low + high) * .5
            # Both actual Leaf7 alpha bounds occupy 460 of 512 rows. Use only
            # their middle 70 percent here, leaving room for natural cutouts.
            intervals.append((foot[2] - center - (high - low) * .35,
                              foot[2] - center + (high - low) * .35))
        covered = height * .65
        for low, high in sorted(intervals):
            if high < covered:
                continue
            if low > height * .95:
                break
            self.assertLessEqual(low - covered, height * .008)
            covered = max(covered, high)
        self.assertGreaterEqual(covered, height * .95)

    def test_inner_foliage_closes_lower_middle_crown_from_eight_directions(self):
        foot, radius, height = vegetation.tree7_envelope(self.source)
        foliage = [face for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 3]
        inside_faces = foliage[-self.stats["interior_clusters"] * 16:]
        cards = []
        for start in range(0, len(inside_faces), 8):
            indices = {i for face in inside_faces[start:start + 8] for i in struct.unpack_from("<3H", face, 2)}
            vertices = [self.result.vertices[i] for i in indices]
            center = tuple(sum(v[a] for v in vertices) / len(vertices) for a in range(3))
            self.assertLess(math.hypot(center[0] - foot[0], center[1] - foot[1]), radius * .25)
            self.assertTrue(.20 <= (foot[2] - center[2]) / height <= .55)
            low, high = min(v[2] for v in vertices), max(v[2] for v in vertices)
            middle = (low + high) * .5
            interval = (foot[2] - middle - (high - low) * .40,
                        foot[2] - middle + (high - low) * .40)
            normal = vegetation.unit(tuple(sum(v[a] for v in vertices) for a in (5, 6, 7)))
            cards.append((interval, normal))
        for direction in range(8):
            angle = direction * math.tau / 8
            intervals = sorted(interval for interval, n in cards if abs(n[0] * math.cos(angle) + n[1] * math.sin(angle)) > .35)
            covered = height * .20
            for low, high in intervals:
                if high < covered:
                    continue
                self.assertLessEqual(low - covered, height * .01)
                covered = max(covered, high)
            self.assertGreaterEqual(covered, height * .55)
