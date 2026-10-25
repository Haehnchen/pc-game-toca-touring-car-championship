#!/usr/bin/env python3
"""Geometry and canonical-media contracts for the local Renault remodel."""

import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import renault_geometry as MODEL
import renault_textures
from vehicle_model import read_source

DEFAULT_DEFINITION = PROJECT / "assets/models/vehicles/renault/definition.json"

def renault_source():
    from vehicle_manifest import load_manifest
    return read_source(load_manifest()["vehicles"][0]["source"])


class RenaultGeometryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.definition = json.loads(DEFAULT_DEFINITION.read_text())

    def test_wheel_is_round_closed_profile_with_real_recess_and_spokes(self):
        mesh = MODEL.wheel_mesh(bytes(48), self.definition)
        segments = self.definition["wheel_segments"]
        self.assertEqual(segments, 32)
        self.assertGreater(len(mesh.faces), 500)
        self.assertLess(len(mesh.faces), 700)
        for row, (z, radius) in enumerate(self.definition["wheel_profile"]):
            for vertex in mesh.vertices[row * segments:(row + 1) * segments]:
                self.assertAlmostEqual(vertex[2], z)
                self.assertLessEqual(abs((vertex[0] ** 2 + vertex[1] ** 2) ** .5 - radius), .00151)
        # Rim disk, raised spoke ridge and central hub are separate depths.
        depths = {round(abs(v[2]), 3) for v in mesh.vertices}
        self.assertTrue({.103, .106, .119, .130, .139}.issubset(depths))
        for face in mesh.faces:
            vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            normal = MODEL.cross(MODEL.sub(vertices[1][:3], vertices[0][:3]), MODEL.sub(vertices[2][:3], vertices[0][:3]))
            self.assertGreater(MODEL.dot(normal, normal), 1e-14)
            self.assertGreater(MODEL.dot(normal, MODEL.add(MODEL.add(vertices[0][5:], vertices[1][5:]), vertices[2][5:])), 0)

    def test_tire_retains_source_bounds_and_broad_tread_at_unchanged_axle(self):
        original = MODEL.decode(renault_source())[2][49]
        wheel = MODEL.wheel_mesh(original.header, self.definition)
        tire_indices = {index for face in wheel.faces if tuple(face[10:14]) == tuple(self.definition["tire_color"])
                        for index in struct.unpack_from("<3H", face, 2)}
        tire = [wheel.vertices[index] for index in tire_indices]
        # The original tire is cylindrical across its250mm width. A single
        # maximum-radius ring instead makes a narrow crown that recedes behind
        # the fender; retain a broad tread without moving or scaling the axle.
        for axis in range(3):
            for bound in (min, max):
                self.assertAlmostEqual(bound(v[axis] for v in tire),
                                       bound(v[axis] for v in original.vertices), delta=.00002)
        source_tire = {index for face in original.faces if struct.unpack_from("<h", face, 8)[0] == -1
                       for index in struct.unpack_from("<3H", face, 2)}
        radius = max(math.hypot(*original.vertices[index][:2]) for index in source_tire)
        tread = [vertex for vertex in tire if math.hypot(*vertex[:2]) >= radius - .00002]
        width = max(v[2] for v in original.vertices) - min(v[2] for v in original.vertices)
        self.assertGreaterEqual(max(v[2] for v in tread) - min(v[2] for v in tread), width * .6)
        self.assertAlmostEqual(min(v[2] for v in tread), -max(v[2] for v in tread))

    def test_mirrors_close_glass_and_recess_with_consistent_outward_winding(self):
        geometry = MODEL
        for side in (-1, 1):
            mesh = MODEL.Mesh(bytes(48), [], [])
            geometry.mirror(mesh, side)
            # Audit welded16.16 positions, including duplicated hard-edge
            # normals. Every solid edge must be shared in opposite directions.
            positions = [tuple(round(value * 65536) / 65536 for value in vertex[:3]) for vertex in mesh.vertices]
            edges = {}
            for face in mesh.faces:
                indices = struct.unpack_from("<3H", face, 2)
                for a, b in zip(indices, indices[1:] + indices[:1]):
                    start, end = positions[a], positions[b]
                    self.assertNotEqual(start, end)
                    edges.setdefault(tuple(sorted((start, end))), []).append(start < end)
            for uses in edges.values():
                self.assertEqual(sorted(uses), [False, True])

            def visible_material(y, z):
                hits = []
                for face in mesh.faces:
                    a, b, c = [positions[index] for index in struct.unpack_from("<3H", face, 2)]
                    normal = MODEL.cross(MODEL.sub(b, a), MODEL.sub(c, a))
                    # Actual one-sided visibility from behind the glass (+X
                    # viewing ray); back-facing surfaces cannot cover a hole.
                    if normal[0] >= -1e-12:
                        continue
                    denominator = (b[2] - c[2]) * (a[1] - c[1]) + (c[1] - b[1]) * (a[2] - c[2])
                    wa = ((b[2] - c[2]) * (y - c[1]) + (c[1] - b[1]) * (z - c[2])) / denominator
                    wb = ((c[2] - a[2]) * (y - c[1]) + (a[1] - c[1]) * (z - c[2])) / denominator
                    weights = wa, wb, 1 - wa - wb
                    if min(weights) >= -1e-7:
                        x = sum(weight * point[0] for weight, point in zip(weights, (a, b, c)))
                        hits.append((x, tuple(face[10:14])))
                self.assertTrue(hits, "Culling must never expose the mirror's open interior")
                return min(hits)[1]

            for i in range(20):
                angle = math.tau * (i + .37) / 20
                cy, cz = math.cos(angle), math.sin(angle)
                for fraction in (.1, .5, .9):
                    y = .300 + (.050 + fraction * (.063 * .94 - .050)) * cy
                    z = side * .895 + (.067 + fraction * (.083 * .94 - .067)) * cz
                    self.assertEqual(visible_material(y, z), geometry.RUBBER)
                # The view through the aperture must still hit actual glass.
                for fraction in (.2, .85):
                    self.assertEqual(visible_material(.300 + .050 * fraction * cy, side * .895 + .067 * fraction * cz), geometry.GLASS)

    def test_curved_surface_changes_shape_with_bounded_displacement(self):
        # This is a curved panel, with corner normals pointing along its crown.
        vertices = [(0., 0., 0., 0., -1., -.3, 1., 0.), (1., 0., 0., 1., -1., .3, 1., 0.), (0., 0., 1., 0., 0., -.3, 1., 0.)]
        face = struct.pack("<H3Hh4B10x", 0, 0, 2, 1, 0, 255, 255, 255, 255)
        mesh = MODEL.curved_mesh(MODEL.Mesh(bytes(48), vertices, [face]), 2, .025)
        self.assertEqual(len(mesh.faces), 4)
        self.assertTrue(any(v[1] > .001 for v in mesh.vertices))
        self.assertTrue(all(abs(v[1]) <= .025001 for v in mesh.vertices))

    def test_canonical_structure_media_and_damage_slots_survive(self):
        original = renault_source()
        output, stats = MODEL.generate(original, self.definition)
        self.assertEqual(MODEL.generate(original, self.definition)[0], output)
        original_header, original_nodes, original_meshes, original_textures = MODEL.decode(original)
        header, nodes, meshes, textures = MODEL.decode(output)
        self.assertEqual(header[:20], original_header[:20])
        self.assertEqual(nodes, original_nodes)
        self.assertEqual(textures, original_textures)
        self.assertEqual([m.header[:12] for m in meshes], [m.header[:12] for m in original_meshes])
        self.assertEqual(stats["original_visible_triangles"], 327)
        self.assertGreater(stats["visible_triangles"], 5000)
        # Physical wheel-arch bands add bounded geometry to the earlier6500
        # source budget. The final generated-atlas UV pass is checked below.
        self.assertLess(stats["visible_triangles"], 7000)
        self.assertGreater(stats["visible_body_triangles"], 2800)
        self.assertGreater(stats["visible_body_detail_triangles"], 2000)
        for mesh in meshes:
            for face in mesh.faces:
                self.assertLess(max(struct.unpack_from("<3H", face, 2)), len(mesh.vertices))
                vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                normal = MODEL.cross(MODEL.sub(vertices[1][:3], vertices[0][:3]), MODEL.sub(vertices[2][:3], vertices[0][:3]))
                self.assertGreater(MODEL.dot(normal, normal), 1e-16)
                self.assertGreater(MODEL.dot(normal, MODEL.add(MODEL.add(vertices[0][5:], vertices[1][5:]), vertices[2][5:])), 0)
        # The grille is a cut opening with a back wall behind the painted skin.
        geometry = MODEL
        skin, _ = geometry.surface(original_meshes[9], (2.2, -.438, .1), 0, 1)
        recess, _ = geometry.surface(meshes[9], (2.2, -.438, .1), 0, 1)
        self.assertGreater(skin[0] - recess[0], .04)
        self.assertLess(skin[0] - recess[0], .065)
        # Only the obsolete side-sprite tip samples a fixed plain-yellow point;
        # hood logos/white sponsor areas and bumper UVs are excluded entirely.
        for index in (0, 1, 2, 4, 5, 6):
            mesh = meshes[index]
            paint_uv = geometry.LAMP_SIDE_PAINT_UVS[1 if index < 4 else -1]
            painted = [mesh.vertices[i] for face in mesh.faces for i in struct.unpack_from("<3H", face, 2)
                       if math.dist(mesh.vertices[i][3:5], paint_uv) < .00002]
            self.assertTrue(painted)
            for vertex in painted:
                self.assertTrue(1.83999 <= vertex[0] <= 1.91001)
                self.assertTrue(-.08501 <= vertex[1] <= .05501)
                self.assertGreaterEqual(abs(vertex[2]), .72999)
        # Lost mirrors retain their original stumps; damaged lamps lose details.
        self.assertEqual(len(meshes[42].faces), len(original_meshes[42].faces))
        self.assertEqual(len(meshes[44].faces), len(original_meshes[44].faces))
        self.assertLess(len(meshes[46].faces), len(meshes[45].faces))
        self.assertLess(len(meshes[48].faces), len(meshes[47].faces))
        # Rear lenses follow the authored contour without added dark frames or
        # protruding separator bars, including both damaged lens variants.
        for index in (16, 17, 18, 19):
            original_lamp, lamp = original_meshes[index], meshes[index]
            self.assertTrue(all(struct.unpack_from("<h", face, 8)[0] == 0 for face in lamp.faces))
            lens_vertices = {i for face in lamp.faces[3:] for i in struct.unpack_from("<3H", face, 2)}
            lens_positions = {lamp.vertices[i][:3] for i in lens_vertices}
            for corner in geometry.boundary_loop(original_lamp, (0, 1)):
                self.assertIn(original_lamp.vertices[corner][:3], lens_positions)
            displacements = []
            for vertex_index in lens_vertices:
                vertex = lamp.vertices[vertex_index]
                matches = []
                for face in original_lamp.faces[:2]:
                    a, b, c = [original_lamp.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                    denominator = (b[4] - c[4]) * (a[3] - c[3]) + (c[3] - b[3]) * (a[4] - c[4])
                    wa = ((b[4] - c[4]) * (vertex[3] - c[3]) + (c[3] - b[3]) * (vertex[4] - c[4])) / denominator
                    wb = ((c[4] - a[4]) * (vertex[3] - c[3]) + (a[3] - c[3]) * (vertex[4] - c[4])) / denominator
                    weights = wa, wb, 1 - wa - wb
                    if min(weights) >= -.0002:
                        source_point = tuple(sum(weights[j] * point[k] for j, point in enumerate((a, b, c))) for k in range(3))
                        matches.append(math.dist(vertex[:3], source_point))
                self.assertTrue(matches)
                displacements.append(min(matches))
            self.assertLess(max(displacements), .00085)
            self.assertGreater(max(displacements), .0006)
        # Lamp panels also include paint and grille: no added black panel frame,
        # no inset/shrunken exterior and no solid-gray inserts over painted optics.
        # Actual textured cavities may only recess the atlas's circular optics.
        for index, side in ((45, 1), (47, -1)):
            front = MODEL.Mesh(original_meshes[index].header, original_meshes[index].vertices, original_meshes[index].faces[:3])
            lamp = meshes[index]
            gasket = [face for face in lamp.faces if struct.unpack_from("<h", face, 8)[0] == -1]
            self.assertEqual(len(gasket), 2)
            for face in gasket:
                self.assertEqual(tuple(face[10:14]), geometry.RUBBER)
                for vertex_index in struct.unpack_from("<3H", face, 2):
                    point = lamp.vertices[vertex_index]
                    self.assertTrue(1.903 < point[0] < 1.905)
                    self.assertTrue(-.071 < point[1] < .046)
                    skin, _ = geometry.surface(original_meshes[index], point[:3], 2, side)
                    self.assertAlmostEqual(point[2], skin[2], delta=.00002)
            textured = [face for face in lamp.faces if struct.unpack_from("<h", face, 8)[0] == 0]
            self.assertEqual(len(textured) + len(gasket), len(lamp.faces))
            self.assertTrue(all(face in original_meshes[index].faces or tuple(face[10:14]) == (255, 255, 255, 255) for face in textured))
            used = {i for face in textured for i in struct.unpack_from("<3H", face, 2)}
            for corner in geometry.boundary_loop(front, (0, 1, 2)):
                source_vertex = front.vertices[corner]
                self.assertTrue(any(vertex[:5] == source_vertex[:5] for vertex in (lamp.vertices[i] for i in used)))

            def source_at_uv(u, v):
                for face in front.faces:
                    a, b, c = [front.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                    denominator = (b[4] - c[4]) * (a[3] - c[3]) + (c[3] - b[3]) * (a[4] - c[4])
                    wa = ((b[4] - c[4]) * (u - c[3]) + (c[3] - b[3]) * (v - c[4])) / denominator
                    wb = ((c[4] - a[4]) * (u - c[3]) + (a[3] - c[3]) * (v - c[4])) / denominator
                    weights = wa, wb, 1 - wa - wb
                    if min(weights) >= -.0002:
                        return tuple(sum(weights[j] * point[k] for j, point in enumerate((a, b, c))) for k in range(8))
                return None

            recessed = 0
            for vertex_index in used:
                vertex = lamp.vertices[vertex_index]
                skin = source_at_uv(*vertex[3:5])
                if skin is None:  # Original side-wrapping indicator faces.
                    continue
                self.assertAlmostEqual(vertex[1], skin[1], delta=.00005)
                self.assertAlmostEqual(vertex[2], skin[2], delta=.00005)
                depth = skin[0] - vertex[0]
                radius = min(math.dist((vertex[3], vertex[4] + 1), uv) for uv in geometry.HEADLAMP_UV_CENTERS[side])
                self.assertGreater(depth, -.00005)
                self.assertLess(depth, .01405)
                if depth > .0001:
                    recessed += 1
                    self.assertLess(radius, geometry.HEADLAMP_UV_RADIUS + .0001)
                elif radius > geometry.HEADLAMP_UV_RADIUS + .0001:
                    self.assertLess(math.dist(vertex[5:], skin[5:]), .00015)
            self.assertGreater(recessed, 50)
            for u, v in geometry.HEADLAMP_UV_CENTERS[side]:
                skin = source_at_uv(u, v - 1)
                cavity, _ = geometry.surface(lamp, skin, 0, 1)
                self.assertAlmostEqual(skin[0] - cavity[0], .014, delta=.00005)
        for index in (46, 48):
            self.assertEqual(meshes[index].vertices, original_meshes[index].vertices)
            self.assertEqual(meshes[index].faces, original_meshes[index].faces)
        # The generated atlas samples one continuous lens across the front/side
        # corner. Only the side UVs change, with no geometry/normal/face changes.
        before_uv = [MODEL.Mesh(mesh.header, list(mesh.vertices), list(mesh.faces)) for mesh in meshes]
        texture_tools = renault_textures
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "metadata.json").write_text(json.dumps({"front_lamp_uvs": {"wrap_width": .012},
                                                           "opaque_lamp_meshes": [45, 47]}))
            texture_tools.fit_front_lamp_uvs(meshes, path)
            self.assertEqual(meshes, before_uv)  # Original atlas has no override.
            (path / "REN.bmp").write_bytes(b"generated texture presence")
            texture_tools.fit_front_lamp_uvs(meshes, path)
            # The new corner UVs cover an originally transparent atlas texel.
            # Its real glass must be opaque; unrelated original mask stays exact.
            opaque_uvs = texture_tools.lamp_opaque_uvs(meshes, path)
            original_bmp = texture_tools.texture_records(original_textures, 9)[0][2]
            white = texture_tools.rgb_bmp(256, 256, [(255, 255, 255)] * 65536)
            fitted = texture_tools.bmp_pixels(texture_tools.native_colorkey("REN", original_bmp, white, opaque_uvs))[2]
            self.assertEqual(texture_tools.bmp_pixels(original_bmp)[2][116 * 256 + 251], (0, 0, 0))
            self.assertEqual(fitted[116 * 256 + 251], (255, 255, 255))
            self.assertEqual(fitted[0], (0, 0, 0))
            for invalid in (-.01, .5, float("nan")):
                (path / "metadata.json").write_text(json.dumps({"front_lamp_uvs": {"wrap_width": invalid}}))
                with self.assertRaises(ValueError):
                    texture_tools.fit_front_lamp_uvs(meshes, path)
        for index, (before, after) in enumerate(zip(before_uv, meshes)):
            self.assertEqual(before.faces, after.faces)
            if index not in (45, 47):
                self.assertEqual(before.vertices, after.vertices)
                continue
            self.assertNotEqual(before.vertices, after.vertices)
            for vertex_index, (a, b) in enumerate(zip(before.vertices, after.vertices)):
                self.assertEqual(a[:3], b[:3])
                self.assertEqual(a[5:], b[5:])
                if vertex_index not in (5, 6, 7, 8):
                    self.assertEqual(a[3:5], b[3:5])
            top_side = 6 if index == 45 else 5
            self.assertEqual(after.vertices[top_side][3:5], after.vertices[3][3:5])
            self.assertEqual(after.vertices[7][3:5], after.vertices[4][3:5])
            self.assertTrue(.489 < after.vertices[8][4] + 1 < .491)
            for t in (0., .2, .5, .8, 1.):
                for attribute in range(5):
                    front = (1 - t) * after.vertices[3][attribute] + t * after.vertices[4][attribute]
                    side = (1 - t) * after.vertices[top_side][attribute] + t * after.vertices[7][attribute]
                    self.assertAlmostEqual(front, side)
        parked = MODEL.Mesh(original_meshes[33].header, [], [])
        geometry.wipers(parked, original_meshes[33])
        self.assertTrue(all(.93 < vertex[0] < 1.08 for vertex in parked.vertices))
        # Numbers must follow the curved paint in every damage variant. Check
        # interior points, because lifting only quad corners leaves occlusion.
        numeric_samples = 0
        for index, original_mesh in enumerate(original_meshes):
            faces = [face for face in original_mesh.faces if struct.unpack_from("<h", face, 8)[0] in (7, 8)]
            if not faces:
                continue
            body = MODEL.Mesh(meshes[index].header, meshes[index].vertices, [face for face in meshes[index].faces if struct.unpack_from("<h", face, 8)[0] == 0])
            decal = MODEL.Mesh(meshes[index].header, meshes[index].vertices, [face for face in meshes[index].faces if struct.unpack_from("<h", face, 8)[0] in (7, 8)])
            for face in faces:
                vertices = [original_mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                normal = tuple(sum(vertex[k] for vertex in vertices) for k in (5, 6, 7))
                axis = max(range(3), key=lambda i: abs(normal[i]))
                sign = 1 if normal[axis] > 0 else -1
                for i in range(1, 8):
                    for j in range(1, 8 - i):
                        weights = i / 8, j / 8, 1 - (i + j) / 8
                        point = tuple(sum(weights[a] * vertices[a][k] for a in range(3)) for k in range(3))
                        skin, _ = geometry.surface(body, point, axis, sign)
                        number, _ = geometry.surface(decal, point, axis, sign)
                        self.assertAlmostEqual((number[axis] - skin[axis]) * sign, .0025, delta=.00005)
                        numeric_samples += 1
        self.assertGreater(numeric_samples, 250)
        # Actual texture metadata can add triangles when fitting complete lamp
        # artwork. Exercise those real UV passes without re-encoding images.
        texture_directory = PROJECT / "assets/models/vehicles/renault/textures"
        texture_tools.fit_name_uvs(meshes, original_textures, 9, texture_directory)
        texture_tools.fit_rear_lamp_uvs(meshes, texture_directory)
        texture_tools.fit_front_lamp_uvs(meshes, texture_directory)
        self.assertLess(MODEL.visible_triangles(nodes, meshes), 8000)

        # The smooth upper arch closes the original triangular notch, while
        # lower opening widths retain exactly the original geometry coverage.
        smooth_arches = geometry.smooth_wheel_arches
        geometry.smooth_wheel_arches = lambda _: None
        try:
            before_arches = MODEL.decode(MODEL.generate(original, self.definition)[0])[2]
        finally:
            geometry.smooth_wheel_arches = smooth_arches

        def profile_triangles(parts, side, source_meshes):
            result = []
            for index in parts:
                mesh = source_meshes[index]
                for face in mesh.faces:
                    if struct.unpack_from("<h", face, 8)[0] != 0:
                        continue
                    vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                    if min(side * vertex[2] for vertex in vertices) >= .55:
                        result.append([(v[3] * 256, ((v[4] + 1) if side > 0 else -v[4]) * 256) for v in vertices])
            return result

        def covers(point, triangles):
            for a, b, c in triangles:
                den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                if abs(den) < 1e-8:
                    continue
                wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / den
                wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / den
                if min(wa, wb, 1 - wa - wb) >= -1e-5:
                    return True
            return False

        for parts, side, cx, cy, radius in (((0, 9), 1, 213.0308, 7.8108, 24.8), ((4, 9), -1, 213.0308, 7.8108, 24.8),
                                            ((30, 20), 1, 53.186, .9872, 27.8), ((27, 20), -1, 53.186, .9872, 27.8)):
            before = profile_triangles(parts, side, before_arches)
            after = profile_triangles(parts, side, meshes)
            for height in (3., 6., 10., 13.):
                for x in range(math.floor(cx - radius - 2), math.ceil(cx + radius + 2)):
                    self.assertEqual(covers((x + .25, height), before), covers((x + .25, height), after))
            for degrees in range(60, 121, 5):
                angle = math.radians(degrees)
                self.assertTrue(covers((cx + (radius + .15) * math.cos(angle), cy + (radius + .15) * math.sin(angle)), after))
                self.assertFalse(covers((cx + (radius - .15) * math.cos(angle), cy + (radius - .15) * math.sin(angle)), after))
        # The curvature pass precedes intentional remodeling of lights/wing/vents.
        # Audit its panel-join contract separately from those replaced surfaces.
        base, _ = MODEL.generate(original, {**self.definition, "body_details": False})
        meshes = MODEL.decode(base)[2]
        # At least one real surface position changes in every selected panel.
        for index in self.definition["curved_meshes"]:
            before = {tuple(v[:3]) for v in original_meshes[index].vertices}
            self.assertTrue(any(tuple(v[:3]) not in before for v in meshes[index].vertices))
        # Audit actual emitted geometry, including seams between damage variants,
        # duplicated UV vertices and neighboring unmodified glass/light panels.
        edges = {}
        changed_edges = 0
        for index, mesh in enumerate(original_meshes):
            if index == 49:
                continue
            cursor = 0
            for face in mesh.faces:
                vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                curved = index in self.definition["curved_meshes"] and struct.unpack_from("<h", face, 8)[0] in (-1, 0)
                pairs = ((0, 2), (0, 1), (1, 2))
                if curved:
                    points = [meshes[index].vertices[i][:3] for i in struct.unpack_from("<3H", meshes[index].faces[cursor + 1], 2)]
                    cursor += 4
                else:
                    points = [tuple(round((vertices[a][k] + vertices[b][k]) * 32768) / 65536 for k in range(3)) for a, b in pairs]
                    cursor += 1
                for (a, b), point in zip(pairs, points):
                    key = MODEL.edge_key(vertices[a], vertices[b])
                    if key in edges:
                        self.assertEqual(point, edges[key])
                    edges[key] = point
                    midpoint = MODEL.scale(MODEL.add(vertices[a][:3], vertices[b][:3]), .5)
                    distance = math.dist(point, midpoint)
                    self.assertLessEqual(distance, self.definition["body_max_curve"] + .00002)
                    changed_edges += distance > .001
        self.assertGreater(changed_edges, 100)


if __name__ == "__main__":
    unittest.main()
