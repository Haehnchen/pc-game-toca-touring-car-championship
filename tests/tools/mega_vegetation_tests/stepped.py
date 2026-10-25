"""Vegetation checks for stepped forms."""

import math
import struct

from .environment import PROJECT, vegetation
from .geometry import Mesh, bounds
from .alpha import crown_alpha_mask
from .source_forms import source_tree1
from .fixtures import VegetationCase


class SteppedVegetationTests(VegetationCase):
    source_factory = staticmethod(source_tree1)
    texture_name = 'LHRTS1'
    foliage_slot = 6

    def test_visible_alpha_envelope_replaces_the_padded_carrier(self):
        foot, extent = vegetation.tree1_envelope(self.source)
        self.assertEqual(self.result.header, self.source.header)
        self.assertEqual(self.stats["near_trees1"], 1)
        self.assertAlmostEqual(foot[2], .0175933837890625, places=10)
        self.assertAlmostEqual(extent[2][1] - extent[2][0], 12.970926422433756, places=8)
        self.assertAlmostEqual(extent[0][1] - extent[0][0], 3.3741596687881916, places=8)
        self.assertAlmostEqual(extent[1][1] - extent[1][0], 3.389189232588507, places=8)
        for axis, (source, result) in enumerate(zip(extent, bounds(self.result))):
            self.assertGreaterEqual(result[0], source[0] - 1e-8)
            self.assertLessEqual(result[1], source[1] + 1e-8)
            self.assertGreater(result[1] - result[0], .95 * (source[1] - source[0]))
        self.assertAlmostEqual(bounds(self.result)[2][1], foot[2], places=9)

    def test_long_stem_supports_real_branches_and_stepped_upper_middle_crown(self):
        foot, extent = vegetation.tree1_envelope(self.source)
        height = extent[2][1] - extent[2][0]
        self.assertEqual((self.stats["trunks"], self.stats["branches"], self.stats["leaf_cards"]), (1, 4, 40))
        foliage_indices = {i for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 6
                           for i in struct.unpack_from("<3H", face, 2)}
        self.assertEqual(len(foliage_indices), self.stats["leaf_cards"] * 9)
        self.assertEqual(sum(struct.unpack_from("<h", face, 8)[0] == 6 for face in self.result.faces),
                         self.stats["leaf_cards"] * 8)
        foliage = [face for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 6]
        source_crown_floor = (.99 - 48 / 64) / (.99 - 6 / 64)
        for card in range(self.stats["leaf_cards"]):
            indices = sorted({i for face in foliage[card * 8:(card + 1) * 8]
                              for i in struct.unpack_from("<3H", face, 2)})
            vertices = [self.result.vertices[i] for i in indices]
            if 16 <= card < 20:
                # The lowest four upright core cards retain their whole chart.
                # Its below-half-alpha last65 rows may extend below the visible
                # source foliage; the actual alpha edge must keep the free stem.
                for column in range(3):
                    fraction = 2 * (447 / 512) - 1
                    alpha_edge_z = (1 - fraction) * vertices[3 + column][2] + fraction * vertices[6 + column][2]
                    self.assertGreaterEqual((foot[2] - alpha_edge_z) / height, source_crown_floor - 1e-8)
            else:
                self.assertGreaterEqual(min((foot[2] - vertex[2]) / height for vertex in vertices), .27 - 1e-8)
        bark_indices = {i for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 1
                        for i in struct.unpack_from("<3H", face, 2)}
        crown_radius = min(high - low for low, high in extent[:2]) * .5
        fork_quadrants = set()
        for index in bark_indices:
            x, y, z, *_ = self.result.vertices[index]
            h = (foot[2] - z) / height
            dx, dy = x - foot[0], y - foot[1]
            if .23 <= h <= .32 and math.hypot(dx, dy) > crown_radius * .09:
                fork_quadrants.add(int((math.atan2(dy, dx) % math.tau) / (math.tau / 4)))
        self.assertEqual(fork_quadrants, {0, 1, 2, 3})
        # The widest original tier is above the crown's middle; the stem and
        # pointed upper part must remain much narrower than that tier.
        self.assertLess(vegetation.tree1_radius(.20), vegetation.tree1_radius(.68) * .25)
        self.assertGreater(vegetation.tree1_radius(.40), vegetation.tree1_radius(.68) * .75)
        # Native alpha rows38/39 contain the second broad tier; sparse sampling
        # of every fourth row misses this lobe and creates a false waist.
        self.assertGreater(vegetation.tree1_radius(.434), vegetation.tree1_radius(.68) * .90)
        self.assertLess(vegetation.tree1_radius(.85), vegetation.tree1_radius(.68) * .65)

    def test_whole_square_sprays_surround_the_crown_with_valid_surface_normals(self):
        foliage = [face for face in self.result.faces if struct.unpack_from("<h", face, 8)[0] == 6]
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in self.result.faces}, {1, 6})
        for start in range(0, len(foliage), 8):
            indices = sorted({i for f in foliage[start:start + 8] for i in struct.unpack_from("<3H", f, 2)})
            vertices = [self.result.vertices[i] for i in indices]
            self.assertEqual({v[3:5] for v in vertices if v[3] in (0., 1.) and v[4] in (-1., 0.)},
                             {(0., -1.), (1., -1.), (0., 0.), (1., 0.)})
            self.assertAlmostEqual(math.dist(vertices[0][:3], vertices[2][:3]), math.dist(vertices[0][:3], vertices[6][:3]), places=7)
        area = [0.] * 8
        for face in self.result.faces:
            a, b, c = [self.result.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            normal = vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))
            summed = vegetation.add(vegetation.add(a[5:], b[5:]), c[5:])
            self.assertGreater(sum(x * y for x, y in zip(normal, summed)), 0)
            for direction in range(8):
                angle = direction * math.tau / 8
                area[direction] += max(0., normal[0] * math.cos(angle) + normal[1] * math.sin(angle)) * .5
        self.assertTrue(all(value > 10 for value in area))
        for vertex in self.result.vertices:
            self.assertTrue(all(math.isfinite(value) for value in vertex))
            self.assertAlmostEqual(sum(value * value for value in vertex[5:]), 1., places=7)

    def test_dispatcher_preserves_source_and_generated_tree(self):
        import mega_geometry

        before = (list(self.source.vertices), list(self.source.faces))
        result, stats = mega_geometry.scenery(self.source, Mesh, [b"LHRTS1\0"], 2, "objects", vegetation_index=1, foliage_index=6)
        self.assertEqual((result.vertices, result.faces, stats), (self.result.vertices, self.result.faces, self.stats))
        self.assertEqual((self.source.vertices, self.source.faces), before)
        self.assertEqual(stats["added_triangles"], len(result.faces) - len(self.source.faces))

    def test_actual_leaf1_alpha_connects_the_thin_crown_in_eight_views(self):
        sprite = PROJECT / "assets/models/mega/textures/vegetation-leaf1-croft.webp"
        if not sprite.is_file():
            self.skipTest("The authored LHRTS1 sprite is unavailable")
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is required to inspect the authored WebP alpha")
        with Image.open(sprite) as image:
            alpha = image.convert("RGBA").getchannel("A")
            self.assertEqual(alpha.point(lambda value: 255 if value >= 128 else 0).getbbox()[3], 447,
                             "The lower core fit must match the actual half-alpha edge")
            texture = (image.width, image.height, alpha.tobytes())
        _, extent = vegetation.tree1_envelope(self.source)
        cx, cy = [(low + high) * .5 for low, high in extent[:2]]
        # Frame the true visible alpha hull, rather than the much wider padded
        # source carrier. Its crossed axes also retain the source oblique frame.
        frame = Mesh(b"", [(x, y, z, 0., 0., 0., 0., 0.) for x, y, z in
                            ((extent[0][0], cy, extent[2][0]), (extent[0][1], cy, extent[2][1]),
                             (cx, extent[1][0], extent[2][0]), (cx, extent[1][1], extent[2][1]))], [])
        for direction in range(8):
            mask, width, height = crown_alpha_mask(self.result, {6: texture}, direction * math.tau / 8, frame)
            core = [mask[y * width + x] for y in range(int(height * .12), int(height * .67))
                    for x in range(int(width * .30), int(width * .70))]
            self.assertGreater(sum(core) / len(core), .93, direction)
