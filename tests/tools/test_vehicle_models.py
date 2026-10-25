#!/usr/bin/env python3
"""Manifest identities, canonical sources and both shared vehicle build modes."""

import copy
import importlib.util
import json
import math
import shutil
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import configured_data_root
import vehicle_manifest as manifest
import vehicle_model as model
import vehicle_wheel_wells as wells
import vehicle_wheel_stance as stance
import vehicle_textures as textures

SPEC = importlib.util.spec_from_file_location("vehicle_builder", PROJECT / "tools/build-vehicle-models.py")
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


def ray_hit(origin, direction, triangle):
    """One-sided renderer visibility, independent of the liner construction."""
    a, b, c = triangle
    ax, ay, az = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    bx, by, bz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    dx, dy, dz = direction
    px, py, pz = dy * bz - dz * by, dz * bx - dx * bz, dx * by - dy * bx
    determinant = ax * px + ay * py + az * pz
    if determinant <= 1e-12:
        return None
    rx, ry, rz = origin[0] - a[0], origin[1] - a[1], origin[2] - a[2]
    u = (rx * px + ry * py + rz * pz) / determinant
    if u < -1e-7 or u > 1 + 1e-7:
        return None
    qx, qy, qz = ry * az - rz * ay, rz * ax - rx * az, rx * ay - ry * ax
    v = (dx * qx + dy * qy + dz * qz) / determinant
    distance = (bx * qx + by * qy + bz * qz) / determinant
    if u >= -1e-7 and v >= -1e-7 and u + v <= 1 + 1e-7 and distance >= -1e-7:
        return distance
    return None


def skin_lip(triangles, center, angle):
    """First actual painted triangle edge along a radial aperture probe."""
    dx, dy = math.cos(angle), math.sin(angle)
    hits = []
    for triangle in triangles:
        for a, b in zip(triangle, triangle[1:] + triangle[:1]):
            ex, ey = b[0] - a[0], b[1] - a[1]
            determinant = dx * ey - dy * ex
            if abs(determinant) < 1e-12:
                continue
            ox, oy = a[0] - center[0], a[1] - center[1]
            radius = (ox * ey - oy * ex) / determinant
            fraction = (ox * dy - oy * dx) / determinant
            if .15 < radius < .7 and -1e-7 <= fraction <= 1 + 1e-7:
                hits.append((radius, a[2] + fraction * (b[2] - a[2])))
    if not hits:
        return None
    radius, z = min(hits)
    return center[0] + radius * dx, center[1] + radius * dy, z


def healthy_skin(nodes, meshes, floor, wheel, paint):
    """Active healthy painted side panels in the invariant floor frame."""
    records, offset = [], 0
    while offset < len(nodes):
        size, parent = struct.unpack_from('<Ih', nodes, offset)
        origin = tuple(v / 65536 for v in struct.unpack_from('<3i', nodes, offset + 56))
        if parent >= 0:
            origin = model.add(origin, records[parent][0])
        mesh = struct.unpack_from('<H', nodes, offset + 80)[0] if nodes[offset + 76] else 65535
        records.append((origin, mesh))
        offset += size
    floor_origin = next(origin for origin, mesh in records if mesh == floor)
    triangles = []
    for origin, mesh in records:
        if mesh in (65535, floor, wheel):
            continue
        offset = model.sub(origin, floor_origin)
        for face in meshes[mesh].faces:
            if struct.unpack_from('<h', face, 8)[0] == paint:
                triangles.append([(*model.add(meshes[mesh].vertices[i][:3], offset), *meshes[mesh].vertices[i][3:5])
                                  for i in struct.unpack_from('<3H', face, 2)])
    return triangles


def skin_at(triangles, x, y, side):
    hits = []
    for a, b, c in triangles:
        if ((x < a[0] and x < b[0] and x < c[0]) or (x > a[0] and x > b[0] and x > c[0]) or
                (y < a[1] and y < b[1] and y < c[1]) or (y > a[1] and y > b[1] and y > c[1])):
            continue
        determinant = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(determinant) < 1e-12:
            continue
        wa = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / determinant
        wb = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / determinant
        weights = wa, wb, 1 - wa - wb
        if min(weights) >= -1e-7:
            point = tuple(sum(weight * vertex[k] for weight, vertex in zip(weights, (a, b, c))) for k in range(5))
            hits.append(point)
    return max(hits, key=lambda point: side * point[2]) if hits else None


def housing_faces(faces, vertices, centers, center):
    result = []
    for face in faces:
        indices = struct.unpack_from('<3H', face, 2)
        centroid = tuple(sum(vertices[i][axis] for i in indices) / 3 for axis in (0, 2))
        nearest = min(centers, key=lambda pivot: abs(centroid[0] - pivot[0]) + abs(centroid[1] - pivot[2]))
        if nearest == center:
            result.append(face)
    return result


class VehicleSourceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.payload = b"canonical member fixture"
        name = b"FIXTURE.C3D".ljust(16, b"\0")
        self.archive = struct.pack("<III16sII", 1, 24, len(self.payload), name, 0, len(self.payload)) + self.payload
        self.path = self.root / "data/allcars.bfp"
        self.path.parent.mkdir()
        self.path.write_bytes(self.archive)
        self.source = dict(archive="data/allcars.bfp", archive_crc32=model.crc32(self.archive),
                           member=0, member_name="FIXTURE.C3D", member_crc32=model.crc32(self.payload))

    def test_relocated_root_retains_provenance_and_archive_member_checks(self):
        with mock.patch.object(model, "PROJECT", self.root / "no-reference"):
            self.assertEqual(model.read_source(self.source, self.root), self.payload)
        self.assertEqual(self.source["archive"], "data/allcars.bfp")
        for field, value in (("archive_crc32", "00000000"), ("member_crc32", "00000000"), ("member_name", "wrong")):
            with self.subTest(field=field), self.assertRaises(ValueError):
                model.read_source({**self.source, field: value}, self.root)
        self.path.write_bytes(self.archive[:-1] + b"!")
        with self.assertRaisesRegex(ValueError, "archive CRC32"):
            model.read_source(self.source, self.root)

    def test_default_source_root_uses_the_configured_bundle_without_reference_fallback(self):
        with mock.patch.dict("os.environ", {"TOCA_DATA_ROOT": str(self.root)}):
            with mock.patch.object(model, "PROJECT", self.root / "no-reference"):
                self.assertEqual(model.read_source(self.source), self.payload)
        with mock.patch.dict("os.environ", {}, clear=True):
            with self.assertRaisesRegex(ValueError, "TOCA_DATA_ROOT"):
                model.read_source(self.source)

    def test_relocated_sources_reject_other_versions_and_escaping_identifiers(self):
        for archive in ("other-version/data/allcars.bfp", "/data/allcars.bfp",
                        "data/../allcars.bfp", "data/./allcars.bfp",
                        "data//allcars.bfp", "data\\allcars.bfp",
                        "data/C:/allcars.bfp"):
            with self.subTest(archive=archive), self.assertRaises(ValueError):
                model.read_source({**self.source, "archive": archive}, self.root)

    @unittest.skipIf(sys.platform == "win32", "Symlink creation needs Windows privileges")
    def test_relocated_archive_symlink_cannot_escape_root(self):
        outside = self.root.parent / (self.root.name + "-outside.bfp")
        outside.write_bytes(self.archive)
        self.addCleanup(outside.unlink)
        self.path.unlink()
        self.path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes"):
            model.read_source(self.source, self.root)


class VehicleManifestTests(unittest.TestCase):
    def setUp(self):
        self.document = manifest.load_manifest()

    def invalid(self, mutate):
        document = copy.deepcopy(self.document)
        mutate(document)
        with self.assertRaises(ValueError):
            manifest.validate_manifest(document)

    def test_registered_runtime_identities(self):
        runtime = [entry for entry in self.document["vehicles"] if entry["mode"] == "mega"]
        self.assertEqual([entry["key"] for entry in runtime], ["renault", "honda", "audi", "vectra", "vectra2", "volvo", "ford", "nissan", "peugeot", "tank", "flexmobile"])
        self.assertEqual(runtime[0]["output"], "assets/models/mega/car-7.glb")
        honda = next(entry for entry in self.document["vehicles"] if entry["key"] == "honda")
        self.assertEqual(honda["mode"], "mega")
        self.assertEqual(manifest.load_definition(honda)["geometry"], "honda")
        warwick = next(entry for entry in runtime if entry["key"] == "vectra2")
        self.assertEqual((warwick["render_id"], warwick["physical_id"]), (10, 2))
        self.assertEqual(manifest.load_definition(warwick)["geometry"], "vectra")
        volvo = next(entry for entry in runtime if entry["key"] == "volvo")
        self.assertEqual((volvo["render_id"], volvo["physical_id"]), (3, 3))
        self.assertEqual(manifest.load_definition(volvo)["geometry"], "volvo")
        ford = next(entry for entry in runtime if entry["key"] == "ford")
        self.assertEqual((ford["render_id"], ford["physical_id"]), (4, 4))
        self.assertEqual(manifest.load_definition(ford)["geometry"], "ford")
        nissan = next(entry for entry in runtime if entry["key"] == "nissan")
        self.assertEqual((nissan["render_id"], nissan["physical_id"]), (5, 5))
        self.assertEqual(manifest.load_definition(nissan)["geometry"], "nissan")
        peugeot = next(entry for entry in runtime if entry["key"] == "peugeot")
        self.assertEqual((peugeot["render_id"], peugeot["physical_id"]), (6, 6))
        self.assertEqual(manifest.load_definition(peugeot)["geometry"], "peugeot")
        for entry in self.document["vehicles"]:
            manifest.load_definition(entry)

    def test_conflicting_keys_ids_outputs_and_source_identity_rejected(self):
        for field, value in (("key", "renault"), ("render_id", 7), ("physical_id", 2),
                             ("output", "assets/models/mega/car-7.glb")):
            with self.subTest(field=field):
                self.invalid(lambda d: d["vehicles"][1].update({field: value}))
        for field, value in (("member", 4), ("archive", "data/champfin.bfp"),
                             ("archive_crc32", "00000000"), ("member_crc32", "00000000")):
            with self.subTest(source_field=field):
                self.invalid(lambda d: d["vehicles"][0]["source"].update({field: value}))
        self.invalid(lambda d: d["vehicles"][0].update(render_id=True))

    def test_paths_cannot_escape_or_override_another_package(self):
        for field, value in (("definition", "/tmp/definition.json"),
                             ("definition", "assets/models/vehicles/renault/../honda/definition.json"),
                             ("texture_directory", "assets/models/vehicles/honda/textures"),
                             ("output", "assets/models/mega/scene/car-7.glb"),
                             ("output", "assets/models/mega/../car-7.glb")):
            with self.subTest(field=field, value=value):
                self.invalid(lambda d: d["vehicles"][0].update({field: value}))
        with tempfile.TemporaryDirectory(dir=PROJECT / "build") as directory:
            link = Path(directory) / "outside"
            link.symlink_to("/tmp", target_is_directory=True)
            with self.assertRaises(ValueError):
                builder.write(link / "car-7.glb", b"not written")


    def test_registered_canonical_sources_round_trip_with_original_nodes_and_media(self):
        for entry in self.document["vehicles"]:
            with self.subTest(key=entry["key"]):
                source = model.read_source(entry["source"])
                self.assertEqual(model.crc32(source), entry["source"]["member_crc32"])
                self.assertEqual(model.pack(*model.decode(source)), source)
                wrong_name = {**entry["source"], "member_name": "cars"}
                with self.assertRaisesRegex(ValueError, "member name"):
                    model.read_source(wrong_name)

    def test_honda_reference_build_preserves_source_contract_without_detail_growth(self):
        entry = self.document["vehicles"][1]
        source = model.read_source(entry["source"])
        header, nodes, meshes, textures = model.decode(source)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "data").mkdir()
            shutil.copyfile(configured_data_root() / "data/allcars.bfp", root / "data/allcars.bfp")
            with mock.patch.object(model, "PROJECT", root / "unavailable-reference"):
                data, stats = builder.build(entry, {"geometry": "source"}, data_root=root)
        size, = struct.unpack_from("<I", data, 12)
        glb = json.loads(data[20:20 + size])
        self.assertEqual(len(glb["nodes"]), struct.unpack_from("<H", header, 12)[0])
        self.assertEqual(len(glb["meshes"]), len(meshes))
        self.assertEqual(len(glb["images"]), struct.unpack_from("<H", header, 16)[0])
        self.assertEqual(stats["visible_triangles"], stats["original_visible_triangles"])
        self.assertTrue(stats["reference"])
        self.assertEqual(stats["source_model_crc32"], "3562e854")
        offset = 0
        for node in glb["nodes"]:
            count = nodes[offset + 76]
            self.assertEqual(node["extras"]["toca_variants"], list(struct.unpack_from(f"<{count}H", nodes, offset + 80)))
            self.assertEqual(node["extras"]["toca_id_flags"], struct.unpack_from("<I", nodes, offset + 72)[0])
            offset += struct.unpack_from("<I", nodes, offset)[0]


class VehicleWheelWellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cars = []
        cls.canonical_models = {}
        cls.stance_reports = {}
        for entry in manifest.load_manifest()['vehicles']:
            if entry['mode'] != 'mega' or entry['key'] in ('tank', 'flexmobile'):
                # The tracked-style Tank and the pink Cadillac-style Flexmobile
                # have their own source-backed wheel geometry (see
                # test_tank_geometry.py and test_flexmobile_geometry.py), not
                # the touring-car presentation wheel geometry.
                continue
            definition = manifest.load_definition(entry)
            module = __import__(definition['geometry'] + '_geometry')
            source = model.read_source(entry['source'])
            canonical, stats = module.generate(source, definition)
            cls.canonical_models[entry['key']] = canonical
            generated, cls.stance_reports[entry['key']] = stance.apply(canonical, definition)
            result, details = wells.apply(generated, definition)
            cls.cars.append((entry, definition, module, generated, stats, result, details))

    def test_visual_outsets_change_only_four_lateral_node_fields(self):
        for entry, definition, module, generated, stats, result, details in self.cars:
            canonical = self.canonical_models[entry['key']]
            offsets = definition.get('wheel_outset', [0, 0])
            if not any(offsets):
                self.assertEqual(generated, canonical)
            expected = bytearray(canonical)
            offset, end = struct.unpack_from('<2I', canonical, 20)
            changed = []
            while offset < end:
                node_id = struct.unpack_from('<I', canonical, offset + 72)[0] & 0xffffff
                if node_id in (1, 2, 3, 4):
                    original = struct.unpack_from('<i', canonical, offset + 64)[0]
                    outset = round(offsets[0 if node_id <= 2 else 1] * 65536)
                    struct.pack_into('<i', expected, offset + 64, original + (outset if node_id in (1, 3) else -outset))
                    changed.append(node_id)
                offset += struct.unpack_from('<I', canonical, offset)[0]
            self.assertEqual(changed, [1, 2, 3, 4])
            self.assertEqual(generated, bytes(expected))
            old = model.decode(canonical)
            new = model.decode(generated)
            self.assertEqual(old[0], new[0])
            self.assertEqual(old[3], new[3])
            self.assertEqual([mesh.encode() for mesh in old[2]], [mesh.encode() for mesh in new[2]])
            old_centers = wells.anchors(old[1])[2]
            new_centers = wells.anchors(new[1])[2]
            for before, after in zip(old_centers, new_centers):
                self.assertEqual(before[:2], after[:2])
                self.assertEqual(wells.aperture(old[2], old[1], before, definition),
                                 wells.aperture(new[2], new[1], after, definition))

    def test_visual_outset_rejects_unbounded_or_invalid_axle_configuration(self):
        canonical = self.canonical_models['renault']
        definition = self.cars[0][1]
        for value in ([.101, .03], [-.01, .03], [True, .03], [float('nan'), .03], [.055], '0.055'):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'Wheel outset'):
                stance.apply(canonical, {**definition, 'wheel_outset': value})


    def test_renault_adjusted_wheels_clear_the_body_attached_liner_faces(self):
        entry, definition, module, generated, stats, result, details = self.cars[0]
        self.assertEqual(entry['key'], 'renault')
        before, after = model.decode(generated), model.decode(result)
        floor, wheel, centers = wells.anchors(after[1])
        mesh = after[2][floor]
        faces = mesh.faces[len(before[2][floor].faces):]
        minimum = [math.inf] * 4
        weights = [(i / 10, j / 10, 1 - (i + j) / 10) for i in range(11) for j in range(11 - i)]
        # Check the actual flare triangles, not just their vertices or the
        # generic barrel radius. Moving wheels under an unchanged old flare
        # intersects the rear tires even though every corner can stay clear.
        for face in faces[::2]:
            triangle = [mesh.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
            centroid = tuple(sum(p[k] for p in triangle) / 3 for k in range(3))
            well = min(range(4), key=lambda i: abs(centroid[0] - centers[i][0]) + abs(centroid[2] - centers[i][2]))
            center = centers[well]
            for barycentric in weights:
                point = tuple(sum(weight * p[k] for weight, p in zip(barycentric, triangle)) for k in range(3))
                depth = abs(point[2] - center[2])
                for (za, ra), (zb, rb) in zip(definition['wheel_profile'], definition['wheel_profile'][1:]):
                    if za <= depth <= zb:
                        tire_radius = ra + (rb - ra) * (depth - za) / (zb - za)
                        clearance = math.hypot(point[0] - center[0], point[1] - center[1]) - tire_radius
                        minimum[well] = min(minimum[well], clearance)
                        break
        for well, clearance in enumerate(minimum):
            with self.subTest(well=well):
                self.assertTrue(math.isfinite(clearance))
                self.assertGreater(clearance, .010)

    def test_four_housings_use_invariant_floor_and_preserve_every_other_record(self):
        for entry, definition, module, generated, stats, result, details in self.cars:
            with self.subTest(key=entry['key']):
                before, after = model.decode(generated), model.decode(result)
                self.assertEqual(before[0][:20], after[0][:20])
                self.assertEqual(before[1], after[1])
                self.assertEqual(before[3], after[3])
                floor, wheel, centers = wells.anchors(before[1])
                self.assertEqual(details['wheel_well_count'], 4)
                added = len(after[2][floor].faces) - len(before[2][floor].faces)
                self.assertGreater(added, 0)
                self.assertEqual(details['wheel_well_triangles'], added)
                self.assertEqual(details['wheel_well_first_face'], len(before[2][floor].faces))
                self.assertEqual(details['visible_triangles'] - stats['visible_triangles'], added)
                self.assertEqual(details['stored_triangles'], sum(len(mesh.faces) for mesh in after[2]))
                materials = {(struct.unpack_from('<h', f, 8)[0], f[10:14]) for mesh in before[2] for f in mesh.faces}
                for i, (original, changed) in enumerate(zip(before[2], after[2])):
                    if i != floor:
                        self.assertEqual(original.encode(), changed.encode())
                    else:
                        self.assertEqual(changed.faces[:len(original.faces)], original.faces)
                        self.assertEqual(changed.vertices[:len(original.vertices)], original.vertices)
                        for face in changed.faces[len(original.faces):]:
                            self.assertIn((struct.unpack_from('<h', face, 8)[0], face[10:14]), materials)

    def test_quantized_surfaces_face_the_cavity_and_clear_tire_steering_and_ground(self):
        for entry, definition, module, generated, stats, result, details in self.cars:
            with self.subTest(key=entry['key']):
                original, output = model.decode(generated), model.decode(result)
                floor, wheel, centers = wells.anchors(output[1])
                faces = output[2][floor].faces[len(original[2][floor].faces):]
                vertices = output[2][floor].vertices
                radius = max(row[1] for row in definition['wheel_profile'])
                for well, center in enumerate(centers):
                    side = 1 if center[2] > 0 else -1
                    well_faces = housing_faces(faces, vertices, centers, center)
                    self.assertTrue(well_faces)
                    for face in well_faces:
                        points = [vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                        normal = model.cross(model.sub(points[1][:3], points[0][:3]), model.sub(points[2][:3], points[0][:3]))
                        fixed = [tuple(round(value * 65536) for value in point[:3]) for point in points]
                        fixed_normal = model.cross(model.sub(fixed[1], fixed[0]), model.sub(fixed[2], fixed[0]))
                        self.assertGreater(model.dot(fixed_normal, fixed_normal), 0)
                        self.assertGreater(model.dot(normal, tuple(sum(p[k] for p in points) for k in range(5, 8))), 0)
                        for point in points:
                            self.assertGreater(point[1], center[1] - radius)
                    # The barrel has55mm radial room. At the native maximum
                    # 398/4096turn steering, every spun tire/rim vertex also
                    # remains outside the deep backing and inside the barrel.
                    angle = 398 * math.tau / 4096
                    for vertex in output[2][wheel].vertices:
                        for steering in (-angle, 0, angle):
                            for spin in (0, math.pi / 4, math.pi / 2):
                                x = vertex[0] * math.cos(spin) - vertex[1] * math.sin(spin)
                                y = vertex[0] * math.sin(spin) + vertex[1] * math.cos(spin)
                                z = vertex[2]
                                steered_x = x * math.cos(steering) + z * math.sin(steering)
                                steered_z = -x * math.sin(steering) + z * math.cos(steering)
                                self.assertLess(math.hypot(steered_x, y), radius + wells.RADIAL_CLEARANCE - .005)
                                self.assertGreater(side * steered_z, wells.INNER_DEPTH + .005)
                    # A side ray through either visible upper crescent meets
                    # the deep D backing, while the wheel mouth stays open.
                    backing = [f for f in well_faces if all(abs(vertices[i][2] - (center[2] + side * wells.INNER_DEPTH)) < 1e-5
                               for i in struct.unpack_from('<3H', f, 2))]
                    for dx in (-.375, .375):
                        point = (center[0] + dx, center[1] + .10)
                        def inside(face):
                            triangle = [vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                            signs = [(b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0])
                                     for a, b in zip(triangle, triangle[1:] + triangle[:1])]
                            return min(signs) >= -1e-9 or max(signs) <= 1e-9
                        self.assertTrue(any(inside(face) for face in backing))

    def test_oblique_rays_cannot_escape_between_actual_body_lip_and_liner(self):
        for entry, definition, module, generated, stats, result, details in self.cars:
            original, output = model.decode(generated), model.decode(result)
            floor, wheel, centers = wells.anchors(output[1])
            mesh = output[2][floor]
            added = mesh.faces[len(original[2][floor].faces):]
            skin = healthy_skin(original[1], original[2], floor, wheel, getattr(module, 'PAINT', 0))
            for well, center in enumerate(centers):
                side = 1 if center[2] > 0 else -1
                panels = [triangle for triangle in skin if min(side * vertex[2] for vertex in triangle) > .55]
                liners = [[mesh.vertices[index][:3] for index in struct.unpack_from('<3H', face, 2)]
                          for face in housing_faces(added, mesh.vertices, centers, center)]
                tire = [[model.add(output[2][wheel].vertices[index][:3], center)
                         for index in struct.unpack_from('<3H', face, 2)] for face in output[2][wheel].faces]
                probes = 0
                for degrees in (-20, 0, 30, 60, 90, 120, 150, 180, 200):
                    angle = math.radians(degrees)
                    lip = skin_lip(panels, center, angle)
                    if lip is None:
                        continue  # Some rear sills end before the200-degree ray.
                    with self.subTest(car=entry['key'], well=well, angle=degrees):
                        radial = math.cos(angle), math.sin(angle)
                        origin = (lip[0] - .002 * radial[0], lip[1] - .002 * radial[1], lip[2] - side * .001)
                        # Grazing rays through the actual aperture edge escape
                        # an inset generic barrel, even when side-on rays hit
                        # its deep backing. Test the drawn front face as well.
                        camera = (7 * math.cos(math.radians(20)), 1., side * 7 * math.sin(math.radians(20)))
                        for view, direction in (('grazing', (4 * radial[0], 4 * radial[1], -side)),
                                                ('garage20', model.sub(origin, camera))):
                            surfaces = liners + tire if view == 'garage20' else liners
                            hits = [hit for triangle in surfaces if (hit := ray_hit(origin, direction, triangle)) is not None]
                            if not hits and view == 'garage20':
                                floor_y = min(vertex[1] for vertex in original[2][floor].vertices)
                                backing_z = center[2] + side * wells.INNER_DEPTH
                                end = model.add(origin, model.scale(direction, (backing_z - origin[2]) / direction[2]))
                                if end[1] < floor_y:
                                    continue  # The original underside stays open below its floor.
                            self.assertTrue(hits, f'Daylight between painted body lip and inner housing ({view})')
                    probes += 1
                self.assertGreaterEqual(probes, 8)

    def test_housing_surfaces_stay_behind_opaque_fender_skin_at_oblique_views(self):
        for entry, definition, module, generated, stats, result, details in self.cars:
            original, output = model.decode(generated), model.decode(result)
            floor, wheel, centers = wells.anchors(output[1])
            mesh = output[2][floor]
            added = mesh.faces[len(original[2][floor].faces):]
            paint = getattr(module, 'PAINT', 0)
            records = textures.texture_records(original[3], struct.unpack_from('<H', original[0], 16)[0])
            width, height, pixels = textures.bmp_pixels(records[paint][2])
            skin = healthy_skin(original[1], original[2], floor, wheel, paint)
            def opaque(point):
                u, v = point[3:5]
                return pixels[int((v + 1) * height) % height * width + int(u * width) % width] != (0, 0, 0)

            samples = 0
            for well, center in enumerate(centers):
                side = 1 if center[2] > 0 else -1
                panels = [triangle for triangle in skin if min(side * vertex[2] for vertex in triangle) > .55]
                liners = [[mesh.vertices[index][:3] for index in struct.unpack_from('<3H', face, 2)]
                          for face in housing_faces(added, mesh.vertices, centers, center)]
                liners += [[details['ghost_positions'][floor][index] for index in struct.unpack_from('<3H', face, 2)]
                           for face in housing_faces(added, mesh.vertices, centers, center)]
                camera = (7 * math.cos(math.radians(20)), 1., side * 7 * math.sin(math.radians(20)))
                for degrees in range(0, 181, 15):
                    angle = math.radians(degrees)
                    lip = skin_lip(panels, center, angle)
                    self.assertIsNotNone(lip)
                    for margin in (.005, .03, .10):
                        point = skin_at(panels, lip[0] + margin * math.cos(angle), lip[1] + margin * math.sin(angle), side)
                        if point is None:
                            continue
                        if not opaque(point):
                            continue
                        direction = model.sub(point[:3], camera)
                        if not any(abs(hit - 1) < 1e-6 and (front := skin_at([triangle], point[0], point[1], side)) is not None and opaque(front)
                                   for triangle in panels if (hit := ray_hit(camera, direction, triangle)) is not None):
                            continue  # Back-facing paint was never visible.
                        # Paint hidden by another original panel is not a
                        # visible livery sample and cannot diagnose intrusion.
                        hidden = False
                        for triangle in panels:
                            hit = ray_hit(camera, direction, triangle)
                            if hit is not None and hit < 1 - .0002:
                                position = model.add(camera, model.scale(direction, hit))
                                front = skin_at([triangle], position[0], position[1], side)
                                if front is not None and opaque(front):
                                    hidden = True
                                    break
                        if hidden:
                            continue
                        protruding = []
                        for triangle in liners:
                            hit = ray_hit(camera, direction, triangle)
                            if hit is None or hit >= 1 - .0002:
                                continue
                            position = model.add(camera, model.scale(direction, hit))
                            covering_skin = skin_at(panels, position[0], position[1], side)
                            # Paint formerly visible through the empty opening
                            # can correctly disappear behind a new inner wall.
                            # A wall outside the actual opaque skin is a blade.
                            if covering_skin is not None and opaque(covering_skin) and side * (position[2] - covering_skin[2]) > .0003:
                                protruding.append(hit)
                        with self.subTest(car=entry['key'], well=well, angle=degrees, margin=margin):
                            self.assertFalse(protruding, 'Liner protrudes outside original opaque fender paint')
                        samples += 1
            self.assertGreater(samples, 50)

    def test_builder_includes_liners_and_reports_final_geometry_counts(self):
        for entry, definition, module, generated, original_stats, result, details in self.cars:
            with self.subTest(key=entry['key']):
                with mock.patch.object(module, 'generate', return_value=(self.canonical_models[entry['key']], dict(original_stats))), \
                        mock.patch.object(builder.vehicle_glb, 'encode', return_value=(b'glb', {})) as encode:
                    data, stats = builder.build(entry, definition)
                self.assertEqual(data, b'glb')
                self.assertEqual(model.pack(*encode.call_args.args), result)
                self.assertEqual(encode.call_args.kwargs['ghost_hidden_starts'], {details['wheel_well_mesh']: details['wheel_well_first_face']})
                self.assertEqual(encode.call_args.kwargs['ghost_positions'], details['ghost_positions'])
                self.assertNotIn('ghost_positions', stats)
                self.assertEqual(stats['wheel_well_triangles'], details['wheel_well_triangles'])
                self.assertEqual(stats['uncompressed_bytes'], len(result))
                self.assertEqual(stats['visible_triangles'], details['visible_triangles'])
                self.assertEqual(stats['wheel_outset'], self.stance_reports[entry['key']]['wheel_outset'])

    def test_ghost_housings_pin_every_authored_mouth_and_only_change_depth(self):
        for entry, definition, module, generated, stats, result, details in self.cars:
            with self.subTest(key=entry['key']):
                before = model.decode(generated)
                floor, _, centers = wells.anchors(before[1])
                mesh = model.decode(result)[2][floor]
                positions = details['ghost_positions'][floor]
                ordinary = {index for face in mesh.faces[:details['wheel_well_first_face']]
                            for index in struct.unpack_from('<3H', face, 2)}
                housing = {index for face in mesh.faces[details['wheel_well_first_face']:]
                           for index in struct.unpack_from('<3H', face, 2)}
                self.assertEqual(set(positions), housing)
                self.assertFalse(ordinary & positions.keys())
                by_position = {}
                for index, ghost in positions.items():
                    point = mesh.vertices[index][:3]
                    self.assertTrue(all(v * 65536 == round(v * 65536) for v in ghost))
                    by_position.setdefault(point, set()).add(ghost)
                anchors = {}
                for center in centers:
                    mouth = wells.aperture(before[2], before[1], center, definition)
                    side = 1 if center[2] > 0 else -1
                    for point in mouth:
                        self.assertEqual(by_position[point], {point})
                        anchors[(side, *point[:2])] = point[2]
                    center_point = (*center[:2], center[2] + side * wells.INNER_DEPTH)
                    center_point = tuple(round(v * 65536) / 65536 for v in center_point)
                    anchor = sum(p[2] for p in mouth) / len(mouth)
                    anchors[(side, *center_point[:2])] = anchor
                    expected = (*center_point[:2], round((anchor + .2 * (center_point[2] - anchor)) * 65536) / 65536)
                    self.assertEqual(len(by_position[center_point]), 1)
                    self.assertLessEqual(math.dist(next(iter(by_position[center_point])), expected), wells.FIXED_UNIT)
                for index, ghost in positions.items():
                    original = mesh.vertices[index][:3]
                    side = 1 if original[2] > 0 else -1
                    anchor = anchors[(side, *ghost[:2])]
                    self.assertAlmostEqual(ghost[2], anchor + .2 * (original[2] - anchor), delta=wells.FIXED_UNIT)

    def test_anchors_resolve_different_body_and_wheel_parent_frames(self):
        nodes = bytearray(model.decode(self.cars[0][3])[1])
        # Body node300 is translated independently of the wheel siblings.
        struct.pack_into('<i', nodes, 80 + 56, 32768)
        _, _, centers = wells.anchors(nodes)
        expected = wells.anchors(model.decode(self.cars[0][3])[1])[2]
        for before, after in zip(expected, centers):
            self.assertAlmostEqual(before[0] - .5, after[0])
            self.assertEqual(before[1:], after[1:])


if __name__ == "__main__":
    unittest.main()
