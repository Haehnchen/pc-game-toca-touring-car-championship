"""Vegetation checks for broad forms."""

import math
import struct

from .environment import PROJECT, vegetation
from .geometry import Mesh, bounds
from .source_forms import source_tree, source_tree4b
from .fixtures import VegetationCase


class VegetationTests(VegetationCase):
    source_factory = staticmethod(source_tree)
    texture_name = 'LHRTS4'
    foliage_slot = 2

    def test_spatial_crown_replaces_tall_crosses_and_keeps_footprint(self):
        self.assertEqual(self.result.header, self.source.header)
        for old, new in zip(bounds(self.source), bounds(self.result)):
            for a, b in zip(old, new):
                self.assertAlmostEqual(a, b, places=8)
        self.assertEqual(self.stats["near_trees"], 1)
        self.assertEqual(self.stats["trunks"], 1)
        self.assertEqual(self.stats["removed_tree_triangles"], len(self.source.faces))
        self.assertGreater(len(self.result.faces), len(self.source.faces))
        height = bounds(self.source)[2][1] - bounds(self.source)[2][0]
        angular_sectors = set()
        for face in self.result.faces:
            vertices = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            # A former whole-tree billboard would span nearly the full height.
            self.assertLess(max(v[2] for v in vertices) - min(v[2] for v in vertices), height * .5)
            if struct.unpack_from("<h", face, 8)[0] == 2:
                x, y = [sum(v[a] for v in vertices) / 3 for a in (0, 1)]
                if math.hypot(x, y) > 2.:
                    angular_sectors.add(round(math.atan2(y, x) * 8 / math.tau) % 8)
        # Foliage surrounds a volume through eight viewpoints.
        self.assertEqual(angular_sectors, set(range(8)))

    def test_isolated_foliage_and_bark_use_complete_appended_slots(self):
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in self.result.faces}, {1, 2})
        material_uv = {1: [], 2: []}
        for face in self.result.faces:
            slot = struct.unpack_from("<h", face, 8)[0]
            for index in struct.unpack_from("<3H", face, 2):
                u, v = self.result.vertices[index][3:5]
                self.assertTrue(0 <= u <= 1 and -1 <= v <= 0)
                material_uv[slot].append((u, v + 1))
        for uv in material_uv.values():
            self.assertEqual(min(u for u, _ in uv), 0)
            self.assertEqual(max(u for u, _ in uv), 1)
            self.assertEqual(min(v for _, v in uv), 0)
            self.assertEqual(max(v for _, v in uv), 1)

    def test_large_curved_foliage_patch_encloses_entire_upright_sprite(self):
        mesh = Mesh(b"patch", [], [])
        vegetation.leaf_spray(mesh, (0., 0., 0.), 0., 3.5, 3.7, 0, 0.)
        self.assertEqual((len(mesh.vertices), len(mesh.faces)), (16, 18))
        for first in mesh.vertices:
            for second in mesh.vertices:
                if first[2] != second[2]:
                    self.assertGreater((first[2] - second[2]) * (first[4] - second[4]), 0)
        self.assertEqual({mesh.vertices[i][3:5] for i in (0, 3, 12, 15)}, {(0., -1.), (1., -1.), (0., 0.), (1., 0.)})
        self.assertGreater(max(v[0] for v in mesh.vertices), .1)

    def test_visible_foliage_regions_overlap_through_middle_crown_from_eight_views(self):
        height = bounds(self.source)[2][1] - bounds(self.source)[2][0]
        bottom = bounds(self.source)[2][1]
        foliage = [face for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 2]
        cards = []
        for start in range(0, len(foliage), 8):
            indices = {i for face in foliage[start:start + 8] for i in struct.unpack_from("<3H", face, 2)}
            vertices = [self.result.vertices[i] for i in indices]
            low, high = min(v[2] for v in vertices), max(v[2] for v in vertices)
            center = (low + high) * .5
            # Restrict to the central 60 percent actually occupied by foliage;
            # the transparent sprite border cannot close a canopy opening.
            interval = (bottom - center - (high - low) * .3, bottom - center + (high - low) * .3)
            normal = vegetation.unit(tuple(sum(v[a] for v in vertices) for a in (5, 6, 7)))
            cards.append((interval, normal))
        for direction in range(8):
            angle = direction * math.tau / 8
            intervals = sorted(interval for interval, n in cards if abs(n[0] * math.cos(angle) + n[1] * math.sin(angle)) > .35)
            covered = height * .20
            for low, high in intervals:
                if high < covered:
                    continue
                if low > height * .80:
                    break
                self.assertLessEqual(low - covered, height * .015)
                covered = max(covered, high)
            self.assertGreaterEqual(covered, height * .80)

    def test_central_crown_is_alpha_covered_from_eight_directions(self):
        from PIL import Image
        image = Image.open(PROJECT / "assets/models/mega/textures/vegetation-leaf-donington.webp").convert("RGBA")
        width, pixels_high = image.size
        bottom = bounds(self.source)[2][1]
        height = bottom - bounds(self.source)[2][0]
        foliage = [[self.result.vertices[i] for i in struct.unpack_from("<3H", f, 2)]
                   for f in self.result.faces if struct.unpack_from("<h", f, 8)[0] == 2]
        for direction in range(8):
            angle = direction * math.tau / 8
            across = (-math.sin(angle), math.cos(angle))
            for h in (.40, .50, .60, .70):
                for horizontal in (-.3, 0., .3):
                    covered = False
                    for triangle in foliage:
                        a, b, c = [(v[0] * across[0] + v[1] * across[1], v[2]) for v in triangle]
                        px, py = horizontal, bottom - height * h
                        determinant = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
                        if abs(determinant) < 1e-10:
                            continue
                        q = ((px - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (py - a[1])) / determinant
                        r = ((b[0] - a[0]) * (py - a[1]) - (px - a[0]) * (b[1] - a[1])) / determinant
                        if min(1 - q - r, q, r) < -1e-8:
                            continue
                        u, v = [sum(weight * vertex[axis] for weight, vertex in zip((1 - q - r, q, r), triangle)) for axis in (3, 4)]
                        x, y = min(width - 1, max(0, round(u * (width - 1)))), min(pixels_high - 1, max(0, round((v + 1) * (pixels_high - 1))))
                        if image.getpixel((x, y))[3] >= 128:
                            covered = True
                            break
                    self.assertTrue(covered, f"Central canopy hole at direction{direction}, height{h}, offset{horizontal}")

    def test_triangles_have_valid_normals_and_source_is_not_modified(self):
        before = (list(self.source.vertices), list(self.source.faces))
        repeated, stats = vegetation.enhance(self.source, Mesh, [b"LHRTS4\0"], 1, foliage_index=2)
        self.assertEqual((self.source.vertices, self.source.faces), before)
        self.assertEqual((repeated.vertices, repeated.faces, stats), (self.result.vertices, self.result.faces, self.stats))
        for vertex in repeated.vertices:
            self.assertTrue(all(math.isfinite(x) for x in vertex))
            self.assertAlmostEqual(sum(x * x for x in vertex[5:]), 1., places=7)
        for face in repeated.faces:
            a, b, c = [repeated.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            self.assertGreater(sum(x * x for x in normal), 1e-14)
            self.assertGreater(sum(x * y for x, y in zip(normal, vegetation.add(vegetation.add(a[5:], b[5:]), c[5:]))), 0)

    def test_family_budget_selects_the_complete_simple_tree(self):
        cost = self.stats["added_triangles"]
        for budget in (cost - 1, cost, 900):
            model, stats = vegetation.enhance(self.source, Mesh, ["LHRTS4"], 1, triangle_budget=budget, foliage_index=2)
            if budget < cost:
                self.assertIs(model, self.source)
                self.assertEqual(stats, {"added_triangles": 0})
            else:
                self.assertEqual((model.vertices, model.faces, stats), (self.result.vertices, self.result.faces, self.stats))


class BroadVariantVegetationTests(VegetationCase):
    source_factory = staticmethod(source_tree4b)
    texture_name = 'LHRTS4'
    foliage_slot = 2

    def test_broad_variant_keeps_its_own_asymmetric_bounds_and_lower_foot(self):
        self.assertEqual(self.result.header, self.source.header)
        for old, new in zip(bounds(self.source), bounds(self.result)):
            for a, b in zip(old, new):
                self.assertAlmostEqual(a, b, places=8)
        self.assertEqual(self.stats["near_trees4b"], 1)
        self.assertEqual(self.stats["near_trees"], 1)
        self.assertEqual((self.stats["trunks"], self.stats["branches"], self.stats["leaf_cards"]), (1, 0, 28))
        self.assertEqual({struct.unpack_from("<h", face, 8)[0] for face in self.result.faces}, {1, 2})
        self.assertLess(bounds(self.result)[2][1], -.09)

    def test_dispatcher_emits_the_variant(self):
        import mega_geometry

        result, stats = mega_geometry.scenery(self.source, Mesh, [b"LHRTS4\0"], 3, "objects",
                                              vegetation_index=1, foliage_index=2)
        self.assertEqual((result.vertices, result.faces, stats), (self.result.vertices, self.result.faces, self.stats))
        self.assertEqual(stats["added_triangles"], len(result.faces) - len(self.source.faces))
