"""Vegetation checks for canonical forms."""

import math
import struct

from .environment import archive_bytes, vegetation
from .geometry import Mesh, assert_contained_tall_tree, bounds, canonical_mesh
from .fixtures import CanonicalDataCase


class CanonicalVegetationTests(CanonicalDataCase):

    def test_split_grove_has_one_sided_source_twenty_four_bindings_and_source_fitted_feet(self):
        import io
        from PIL import Image

        inventory = self.inventory
        raw = inventory.archive_members(archive_bytes(self.data_root, "data/branfree.bfp"))[5][1]
        nodes, meshes, materials = inventory.parse_model(raw)
        source = meshes[40]
        self.assertEqual((source["name"], inventory.crc32(source["raw"])), ("TRGS20_1", "13e0d1fa"))
        self.assertEqual((len(source["vertices"]), len(source["faces"])), (6, 4))
        self.assertEqual((materials[136]["name"], materials[136]["crc32"], materials[136]["size"]),
                         ("LMRTS2", "2bb5e0c1", [64, 32]))
        bindings = [i for i, node in enumerate(nodes) if 40 in node["variants"]]
        self.assertEqual(bindings, [277, 279, 281, 283, 285, 287, *range(291, 320, 2), 457, 459, 501])
        self.assertTrue(all(nodes[nodes[i]["root"]]["flags"] & 0x20000000 for i in bindings))
        original = canonical_mesh(source)
        for face in original.faces:
            a, b, c = [original.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            self.assertLess(vegetation.cross(vegetation.sub(b[:3], a[:3]), vegetation.sub(c[:3], a[:3]))[1], 0.)
        result, stats = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                           foliage_index=len(materials) + 9)
        self.assertEqual(stats["grove_groups_lm2"], 1)
        offset = struct.unpack_from("<I", raw, 28)[0]
        for _ in range(137):
            size = struct.unpack_from("<I", raw, offset + 12)[0]
            bmp = raw[offset + 16:offset + 16 + size]
            offset += 16 + size
        image = Image.open(io.BytesIO(bmp)).convert("RGB")
        _, uv_at, bottom, height, columns = vegetation.grove_frame(original)
        bark_indices = {i for f in result.faces if struct.unpack_from("<h", f, 8)[0] == len(materials) + 1
                        for i in struct.unpack_from("<3H", f, 2)}
        ground = 0
        for index in bark_indices:
            x, _, z, *_ = result.vertices[index]
            h = (bottom - z) / height
            if h > (.99 - 29 / 32) / .97:
                continue
            u0, p0, u1, p1 = next((u0, p0, u1, p1) for (u0, p0), (u1, p1) in zip(columns, columns[1:])
                                  if p0[0] <= x <= p1[0])
            parameter = u0 + (u1 - u0) * (x - p0[0]) / (p1[0] - p0[0])
            u, v = uv_at(parameter, h)
            px, py = u * 64 - .5, (v + 1) * 32 - .5
            alpha = 0.
            for ix in (math.floor(px), math.floor(px) + 1):
                for iy in (math.floor(py), math.floor(py) + 1):
                    alpha += (1 - abs(ix - px)) * (1 - abs(iy - py)) * (image.getpixel((min(63, max(0, ix)), min(31, max(0, iy)))) != (0, 0, 0))
            self.assertGreaterEqual(alpha, .5)
            ground += abs(h) < 1e-8
        self.assertEqual(ground, 3 * 5)

    def test_short_stem_grove_preserves_twenty_nine_bindings_and_original_foot_gaps(self):
        import io
        from PIL import Image

        inventory = self.inventory
        raw = inventory.archive_members(archive_bytes(self.data_root, "data/branfree.bfp"))[5][1]
        nodes, meshes, materials = inventory.parse_model(raw)
        source = meshes[38]
        self.assertEqual((source["name"], inventory.crc32(source["raw"])), ("TRGS22_1", "9aa00e17"))
        self.assertEqual((len(source["vertices"]), len(source["faces"])), (6, 8))
        self.assertEqual((materials[135]["name"], materials[135]["crc32"], materials[135]["size"]),
                         ("LMRTS4", "fdd0802f", [64, 32]))
        bindings = [i for i, node in enumerate(nodes) if 38 in node["variants"]]
        self.assertEqual(bindings, [137, 145, *range(225, 276, 2), 289])
        self.assertTrue(all(nodes[nodes[i]["root"]]["flags"] & 0x20000000 for i in bindings))
        original = canonical_mesh(source)
        result, stats = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                           foliage_index=len(materials) + 8)
        self.assertEqual(stats["grove_groups_lm4"], 1)
        offset = struct.unpack_from("<I", raw, 28)[0]
        for _ in range(136):
            size = struct.unpack_from("<I", raw, offset + 12)[0]
            bmp = raw[offset + 16:offset + 16 + size]
            offset += 16 + size
        image = Image.open(io.BytesIO(bmp)).convert("RGB")
        _, uv_at, bottom, height, columns = vegetation.grove_frame(original)
        bark_indices = {i for f in result.faces if struct.unpack_from("<h", f, 8)[0] == len(materials) + 1
                        for i in struct.unpack_from("<3H", f, 2)}
        checked, ground_checked = 0, 0
        for index in bark_indices:
            x, _, z, *_ = result.vertices[index]
            h = (bottom - z) / height
            if h > .0700001:
                continue
            u0, p0, u1, p1 = next((u0, p0, u1, p1) for (u0, p0), (u1, p1) in zip(columns, columns[1:])
                                  if p1[0] <= x <= p0[0])
            parameter = u0 + (u1 - u0) * (x - p0[0]) / (p1[0] - p0[0])
            u, v = uv_at(parameter, h)
            # Match the actual two-texel alpha span, including its filtered
            # edge; requiring both neighbors opaque would halve each foot.
            row = min(31, math.floor((v + 1) * 32))
            self.assertNotEqual(image.getpixel((math.floor(u * 64), row)), (0, 0, 0))
            first = math.floor(u * 64 - .5)
            fraction = u * 64 - .5 - first
            alpha = sum(weight * (image.getpixel((pixel_x, row)) != (0, 0, 0))
                        for pixel_x, weight in ((first, 1 - fraction), (first + 1, fraction)))
            self.assertGreaterEqual(alpha, .5)
            checked += 1
            ground_checked += abs(h) < 1e-8
        self.assertEqual(ground_checked, 9 * 5)
        self.assertGreaterEqual(checked, ground_checked)

    def test_ground_grove_has_two_exact_palettes_and_eleven_static_bindings(self):
        inventory = self.inventory
        expected = {3: (41, "5bd1ebb3", 137, "cd66b801", [321, 323, 325, 327, 329, 471, 473]),
                    4: (30, "dd00939b", 91, "d3163486", [145, 147, 429, 431])}
        found = []
        for track, prefix in enumerate(inventory.PREFIXES):
            raw = inventory.archive_members(archive_bytes(self.data_root, f"data/{prefix}free.bfp"))[5][1]
            nodes, meshes, materials = inventory.parse_model(raw)
            for index, source in enumerate(meshes):
                if source["name"] != "TRGS18_1":
                    continue
                found.append(track)
                mesh_index, crc, slot, bmp_crc, bindings = expected[track]
                self.assertEqual((index, inventory.crc32(source["raw"])), (mesh_index, crc))
                self.assertEqual((len(source["vertices"]), len(source["faces"])), (6, 4))
                self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in source["faces"]}, {slot})
                self.assertEqual((materials[slot]["name"], materials[slot]["crc32"], materials[slot]["size"]),
                                 ("LHRTWS4", bmp_crc, [128, 64]))
                actual = [i for i, node in enumerate(nodes) if index in node["variants"]]
                self.assertEqual(actual, bindings)
                self.assertTrue(all(nodes[nodes[i]["root"]]["flags"] & 0x20000000 for i in actual))
                original = canonical_mesh(source)
                result, stats = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                                   foliage_index=len(materials) + 7)
                self.assertEqual(stats["grove_groups4"], 1)
                self.assertEqual(stats["added_triangles"], len(result.faces) - 4)
                self.assertLessEqual(stats["added_triangles"], 2048)
                self.assertEqual(len(original.vertices), 6)
        self.assertEqual(found, [3, 4])

    def test_croft_stepped_tree_has_one_exact_palette_and_sixteen_static_bindings(self):
        inventory = self.inventory
        archive = archive_bytes(self.data_root, "data/croffree.bfp")
        self.assertEqual(inventory.crc32(archive), "e4399113")
        raw = inventory.archive_members(archive)[5][1]
        self.assertEqual(inventory.crc32(raw), "93406c70")
        nodes, meshes, materials = inventory.parse_model(raw)
        index, source = next((i, m) for i, m in enumerate(meshes) if m["name"] == "TRIE01_1")
        self.assertEqual((index, inventory.crc32(source["raw"])), (23, "2043ce10"))
        self.assertEqual((len(source["vertices"]), len(source["faces"])), (20, 16))
        slot = struct.unpack_from("<h", source["faces"][0], 8)[0]
        self.assertEqual((slot, materials[slot]["name"], materials[slot]["crc32"], materials[slot]["size"]),
                         (70, "LHRTS1", "065f127e", [64, 64]))
        original = canonical_mesh(source)
        result, counts = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                            foliage_index=len(materials) + 6)
        self.assertEqual(counts["near_trees1"], 1)
        self.assertEqual(result.header, original.header)
        self.assertEqual(counts["added_triangles"], len(result.faces) - 16)
        self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in result.faces}, {len(materials) + 1, len(materials) + 6})
        matched = [(i, node) for i, node in enumerate(nodes) if index in node["variants"]]
        self.assertEqual([i for i, _ in matched], list(range(453, 477, 2)) + [479, 481, 483, 485])
        self.assertTrue(all(nodes[node["root"]]["flags"] & 0x20000000 for _, node in matched))

    def test_tall_source_has_one_palette_and_thirty_static_bindings(self):
        inventory = self.inventory
        seen, bindings = set(), 0
        for prefix in ("donl", "dons"):
            raw = inventory.archive_members(archive_bytes(self.data_root, f"data/{prefix}free.bfp"))[5][1]
            nodes, meshes, materials = inventory.parse_model(raw)
            index, source = next((i, mesh) for i, mesh in enumerate(meshes) if mesh["name"] == "TRIS02B_1")
            self.assertEqual((index, inventory.crc32(source["raw"])), (35, "5938e222"))
            original = canonical_mesh(source)
            result, stats = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                               foliage_index=len(materials) + 5)
            self.assertEqual(stats["near_trees2b"], 1)
            self.assertEqual(result.header, original.header)
            self.assertEqual(stats["added_triangles"], len(result.faces) - 16)
            self.assertLessEqual(stats["added_triangles"], 2048)
            slot = struct.unpack_from("<h", source["faces"][0], 8)[0]
            self.assertEqual((slot, materials[slot]["size"]), (74, [32, 64]))
            seen.add(materials[slot]["crc32"])
            self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in result.faces}, {slot, len(materials) + 1, len(materials) + 5})
            assert_contained_tall_tree(self, original, result)
            matched = [node for node in nodes if index in node["variants"]]
            self.assertEqual(len(matched), 15)
            bindings += len(matched)
            self.assertTrue(all(nodes[node["root"]]["flags"] & 0x20000000 for node in matched))
        self.assertEqual((seen, bindings), ({"fdd155e8"}, 30))

    def test_broad_variant_uses_two_existing_palettes_and_fifty_one_static_bindings(self):
        inventory = self.inventory
        seen, bindings = set(), 0
        for prefix, source_crc, count in (("donl", "c5511020", 13), ("bran", "fbcee867", 25), ("dons", "c5511020", 13)):
            archive = inventory.archive_members(archive_bytes(self.data_root, f"data/{prefix}free.bfp"))
            nodes, meshes, materials = inventory.parse_model(archive[5][1])
            index, source = next((i, mesh) for i, mesh in enumerate(meshes) if mesh["name"] == "TRIS04B_1")
            self.assertEqual(inventory.crc32(source["raw"]), source_crc)
            original = canonical_mesh(source)
            result, stats = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                               foliage_index=len(materials) + 2)
            self.assertEqual(stats["near_trees4b"], 1)
            self.assertEqual(result.header, original.header)
            self.assertEqual((len(original.vertices), len(original.faces)), (20, 16))
            self.assertEqual(stats["added_triangles"], len(result.faces) - 16)
            self.assertLessEqual(stats["added_triangles"], 2048)
            self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in result.faces}, {len(materials) + 1, len(materials) + 2})
            slot = struct.unpack_from("<h", original.faces[0], 8)[0]
            seen.add(materials[slot]["crc32"])
            for old, new in zip(bounds(original), bounds(result)):
                for a, b in zip(old, new):
                    self.assertAlmostEqual(a, b, places=8)
            source_bindings = sum(node["variants"].count(index) for node in nodes)
            self.assertEqual(source_bindings, count)
            bindings += source_bindings
            for node in nodes:
                if index in node["variants"]:
                    self.assertTrue(nodes[node["root"]]["flags"] & 0x20000000)
        self.assertEqual((seen, bindings), ({"4cff410e", "9382b6a6"}, 51))

    def test_all_eight_material_variants_keep_original_slots_and_bounds(self):
        inventory = self.inventory
        seen = set()
        for prefix in inventory.PREFIXES[:-1]:
            archive = inventory.archive_members(archive_bytes(self.data_root, f"data/{prefix}free.bfp"))
            _, meshes, materials = inventory.parse_model(archive[5][1])
            source = next(mesh for mesh in meshes if mesh["name"] == "TRIS04_1")
            original = canonical_mesh(source)
            names = [m["name"] for m in materials]
            result, stats = vegetation.enhance(original, Mesh, names, len(materials), foliage_index=len(materials) + 1)
            self.assertEqual(stats["near_trees"], 1)
            self.assertEqual(result.header, original.header)
            slot = struct.unpack_from("<h", original.faces[0], 8)[0]
            self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in result.faces}, {len(materials), len(materials) + 1})
            seen.add(materials[slot]["crc32"])
            for old, new in zip(bounds(original), bounds(result)):
                for a, b in zip(old, new):
                    self.assertAlmostEqual(a, b, places=8)
        self.assertEqual(len(seen), 8)

    def test_two_slender_source_variants_cover_sixteen_static_bindings(self):
        inventory = self.inventory
        seen, bindings = set(), 0
        for prefix in ("donl", "bran", "dons"):
            archive = inventory.archive_members(archive_bytes(self.data_root, f"data/{prefix}free.bfp"))
            nodes, meshes, materials = inventory.parse_model(archive[5][1])
            index, source = next((i, mesh) for i, mesh in enumerate(meshes) if mesh["name"] == "TRIS07_1")
            original = canonical_mesh(source)
            names = [m["name"] for m in materials]
            result, stats = vegetation.enhance(original, Mesh, names, len(materials) + 1, foliage_index=len(materials) + 3)
            self.assertEqual(stats["near_trees7"], 1)
            self.assertEqual(result.header, original.header)
            slot = struct.unpack_from("<h", original.faces[0], 8)[0]
            seen.add(materials[slot]["crc32"])
            for node in nodes:
                for mesh in node["variants"]:
                    if mesh == index:
                        bindings += 1
                        self.assertTrue(nodes[node["root"]]["flags"] & 0x20000000)
        self.assertEqual((seen, bindings), ({"b5c67d09", "a7ffa46d"}, 16))

    def test_five_grove_palettes_retain_six_source_members_and_one_hundred_bindings(self):
        inventory = self.inventory
        seen, bindings, members = set(), 0, 0
        for prefix in ("donl", "thru", "bran", "dons", "crof", "snet"):
            archive = inventory.archive_members(archive_bytes(self.data_root, f"data/{prefix}free.bfp"))
            nodes, meshes, materials = inventory.parse_model(archive[5][1])
            index, source = next((i, mesh) for i, mesh in enumerate(meshes) if mesh["name"] == "TRGE17_1")
            original = canonical_mesh(source)
            result, stats = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                               foliage_index=len(materials) + 4)
            self.assertEqual(stats["grove_groups"], 1)
            self.assertEqual(result.header, original.header)
            slot = struct.unpack_from("<h", original.faces[0], 8)[0]
            seen.add(materials[slot]["crc32"])
            self.assertEqual({struct.unpack_from("<h", f, 8)[0] for f in result.faces},
                             {slot, len(materials) + 1, len(materials) + 4})
            members += 1
            for node in nodes:
                for mesh in node["variants"]:
                    if mesh == index:
                        bindings += 1
                        self.assertTrue(nodes[node["root"]]["flags"] & 0x20000000)
        self.assertEqual((len(seen), members, bindings), (5, 6, 100))

    def test_complete_grove_bark_feet_sample_only_opaque_original_alpha_texels(self):
        inventory = self.inventory
        for prefix in ("donl", "thru", "bran", "crof", "snet"):
            raw = inventory.archive_members(archive_bytes(self.data_root, f"data/{prefix}free.bfp"))[5][1]
            _, meshes, materials = inventory.parse_model(raw)
            source = next(mesh for mesh in meshes if mesh["name"] == "TRGE17_1")
            original = canonical_mesh(source)
            slot = struct.unpack_from("<h", source["faces"][0], 8)[0]
            offset = struct.unpack_from("<I", raw, 28)[0]
            for _ in range(slot + 1):
                size = struct.unpack_from("<I", raw, offset + 12)[0]
                bmp = raw[offset + 16:offset + 16 + size]
                offset += 16 + size
            width, bitmap_height = struct.unpack_from("<ii", bmp, 18)
            self.assertEqual(struct.unpack_from("<H", bmp, 28)[0], 8)
            pixels = struct.unpack_from("<I", bmp, 10)[0]
            palette = 14 + struct.unpack_from("<I", bmp, 14)[0]
            model, _ = vegetation.enhance(original, Mesh, [m["name"] for m in materials], len(materials) + 1,
                                          foliage_index=len(materials) + 4)
            _, uv_at, bottom, height, columns = vegetation.grove_frame(original)
            foot_indices = {i for face in model.faces if struct.unpack_from("<h", face, 8)[0] == len(materials) + 1
                            for i in struct.unpack_from("<3H", face, 2) if bottom - model.vertices[i][2] < height * .02}
            groups = []
            for vertex in sorted((model.vertices[i] for i in foot_indices), key=lambda v: v[0]):
                if not groups or vertex[0] - groups[-1][-1][0] > .7:
                    groups.append([])
                groups[-1].append(vertex)
            self.assertEqual(len(groups), 5)
            for group in groups:
                uv = []
                for x, _, z, *_ in group:
                    for (u0, p0), (u1, p1) in zip(columns, columns[1:]):
                        if x <= p1[0]:
                            u = u0 + (u1 - u0) * (x - p0[0]) / (p1[0] - p0[0])
                            break
                    a, b = uv_at(u, (bottom - z) / height)
                    uv.append((a * width - .5, (b + 1) * bitmap_height - .5))
                # Include every bilinear filter contributor across the complete
                # polygon foot, not just its center or its nearest source texel.
                for x in range(math.floor(min(p[0] for p in uv)), math.ceil(max(p[0] for p in uv)) + 1):
                    for y in range(math.floor(min(p[1] for p in uv)), math.ceil(max(p[1] for p in uv)) + 1):
                        index = bmp[pixels + (bitmap_height - 1 - y) * width + x]
                        self.assertNotEqual(bmp[palette + index * 4:palette + index * 4 + 3], b"\0\0\0", prefix)
