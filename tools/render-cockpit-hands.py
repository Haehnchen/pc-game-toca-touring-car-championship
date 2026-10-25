#!/usr/bin/env python3
"""Inspect native --export-grip meshes from true front/side/back viewpoints.

python3 tools/render-cockpit-hands.py build/cockpit-hands/runtime-grip-before \
  --output build/cockpit-hands/diagnostic-before

The native fixture supplies actual Cockpit::frame and node transforms. This
tool never poses or modifies a game hand. Blender runs background-only, with
clay materials by default; --material textured adds the existing artwork.
All reports, native image copies and final WebP70 views stay under build/.
"""

import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import zlib

PROJECT = Path(__file__).resolve().parents[1]


def crc32(path):
    return f"{zlib.crc32(Path(path).read_bytes()):08x}"


def transform(matrix, point, vector=False):
    return [sum(matrix[column * 4 + row] * point[column] for column in range(3)) +
            (0 if vector else matrix[12 + row]) for row in range(3)]


def inverse(matrix):
    rows = [[matrix[column * 4 + row] for column in range(4)] +
            [float(row == column) for column in range(4)] for row in range(4)]
    for column in range(4):
        pivot = max(range(column, 4), key=lambda row: abs(rows[row][column]))
        rows[column], rows[pivot] = rows[pivot], rows[column]
        denominator = rows[column][column]
        rows[column] = [value / denominator for value in rows[column]]
        for row in range(4):
            if row != column:
                amount = rows[row][column]
                rows[row] = [a - amount * b for a, b in zip(rows[row], rows[column])]
    return [rows[row][column + 4] for column in range(4) for row in range(4)]


def squared_distance(first, second):
    return sum((a - b) ** 2 for a, b in zip(first, second))


def wheel_occlusion(eye, point, triangles, all_hits=False):
    """Segment/actual-wheel triangle intersections; holes stay real openings."""
    def subtract(a, b):
        return [x - y for x, y in zip(a, b)]

    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    def cross(a, b):
        return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]

    direction = subtract(point, eye)
    hits = []
    for a, b, c in triangles:
        edge1, edge2 = subtract(b, a), subtract(c, a)
        p = cross(direction, edge2)
        determinant = dot(edge1, p)
        if abs(determinant) < 1e-10:
            continue
        offset = subtract(eye, a)
        u = dot(offset, p) / determinant
        if not 0 <= u <= 1:
            continue
        q = cross(offset, edge1)
        v = dot(direction, q) / determinant
        if v < 0 or u + v > 1:
            continue
        distance = dot(edge2, q) / determinant
        if 0 < distance < 1:
            hits.append(distance)
    return sorted(hits) if all_hits else min(hits) if hits else None


def canonical_functions(role, definition):
    raw_from_body = inverse(role["matrix"])
    sign = 1 if role["role"] == 0 else -1
    reference = definition["frame_anchor"]
    offset = [role["source_anchor"][axis] - reference[axis] * (sign if axis == 2 else 1) for axis in range(3)]

    def from_body(point, vector=False):
        raw = transform(raw_from_body, point, vector)
        return [(raw[axis] - (0 if vector else offset[axis])) * (sign if axis == 2 else 1) for axis in range(3)]

    def to_body(point):
        return transform(role["matrix"], [point[axis] * (sign if axis == 2 else 1) + offset[axis] for axis in range(3)])

    return from_body, to_body


def grip_report(export_root, files, output, definition, definition_path):
    """Sample the recovered tilted torus in each actual canonical hand frame."""
    if str(PROJECT / "tools") not in sys.path:
        sys.path.insert(0, str(PROJECT / "tools"))
    import cockpit_hand_geometry as hand_geometry

    shared = json.loads((PROJECT / "assets/models/cockpits/shared/definition.json").read_text())["wheel"]
    report = {"coordinates": "body: +X forward/away from driver, +Y up; canonical hand frame removes original Audi translation/right-hand reflection",
              "method": "Native runtime/node matrices; nominal rim parameters from accepted shared definition and canonical billboard. Actual exported wheel vertices included for independent verification.",
              "hand_definition_snapshot": str(definition_path), "hand_definition_crc32": crc32(definition_path),
              "tip_metrics": "Rounded pad center and surface pole use the generator's Catmull tangent and public fit_point; nearest exported-vertex distance checks the pole. Older flat caps use the terminal center. Rim rays use actual wheel triangles.", "poses": []}
    for path in files:
        pose = json.loads(path.read_text())
        if pose["profile"] != "mega":
            continue
        original = json.loads((export_root / path.name.replace("-mega.json", "-original.json")).read_text())
        original_wheel = next(role for role in original["roles"] if role["role"] == 4)["meshes"][0]
        local_from_body = inverse(original_wheel["matrix"])
        vertices = [transform(local_from_body, vertex[:3]) for vertex in original_wheel["vertices"]]
        top, bottom = vertices[0], vertices[3]
        cy, cx = (top[1] + bottom[1]) / 2, (top[0] + bottom[0]) / 2
        slope = (top[0] - bottom[0]) / (top[1] - bottom[1])
        vertical = (top[1] - bottom[1]) / (2 * max(abs(vertex[2]) for vertex in vertices))
        wheel = next(role for role in pose["roles"] if role["role"] == 4)
        wheel_matrix = wheel["meshes"][0]["matrix"]
        wheel_vertices = [vertex[:3] for mesh in wheel["meshes"] for vertex in mesh["vertices"]]
        triangles = [[mesh["vertices"][index][:3] for index in face[:3]] for mesh in wheel["meshes"] for face in mesh["faces"]]
        # Exports made before driver_world_to_view was added retain the same
        # neutral head offset from FollowCamera's constructor (Q16 0x6666).
        eye = transform(inverse(pose["driver_world_to_view"]), [0, 0, 0]) if "driver_world_to_view" in pose else [0, 0x6666 / 65536, (1 if pose["car"] == 1 else -1) * 0x6666 / 65536]

        def rim_point(angle, turn=None):
            radius = shared["rim_radius"] + (0 if turn is None else shared["rim_tube_radius"] * math.cos(turn))
            q, z = radius * math.cos(angle), radius * math.sin(angle)
            depth = 0 if turn is None else shared["rim_tube_radius"] * math.sin(turn)
            return transform(wheel_matrix, [cx + slope * vertical * q + depth, cy + vertical * q, z])

        entry = {"car": pose["car"], "pose": pose["pose"], "export": str(path), "export_crc32": crc32(path),
                 "driver_eye_body": eye, "camera_source": "native public FollowCamera::world_to_view" if "driver_world_to_view" in pose else "Original neutral FollowCamera head offset (older export)", "hands": []}
        for role in pose["roles"]:
            if role["role"] >= 2:
                continue
            from_body, to_body = canonical_functions(role, definition)
            driver = from_body([-1, 0, 0], vector=True)
            length = math.sqrt(sum(value * value for value in driver))
            driver = [value / length for value in driver]
            down = from_body([0, -1, 0], vector=True)
            down_length = math.sqrt(sum(value * value for value in down))
            hand = {"role": role["role"], "parked_at_lever": pose["car"] == 9 and role["role"] == 0,
                    "toward_driver_unit": driver, "away_from_driver_unit": [-v for v in driver],
                    "down_unit": [value / down_length for value in down], "fingers": []}
            for index, finger in enumerate(definition["fingers"]):
                path_points = finger["path"] if "path" in finger else definition["finger_path"]
                spread = finger.get("spread", definition.get("finger_spread", [1.] * len(path_points)))
                centers = [[x, finger["y"] * width + drift, z] for (x, drift, z), width in zip(path_points, spread)]
                radii = finger.get("radii", definition.get("finger_radii"))
                base = centers[-1]
                pole = list(base)
                if "fingertip_steps" in definition:
                    previous = hand_geometry._catmull(centers, len(centers) - 1.01)
                    tangent = [a - b for a, b in zip(base, previous)]
                    tangent_length = math.sqrt(sum(value * value for value in tangent))
                    tip_length = radii[-1] * finger["radius_scale"] * definition["fingertip_length_scale"]
                    pole = [a + b / tangent_length * tip_length for a, b in zip(base, tangent)]
                fit = lambda point: hand_geometry.fit_point(point, role["role"], pose["car"], role["source_anchor"], definition)
                tip = fit(pole)
                pad_center = fit(base)
                hand_vertices = [vertex[:3] for mesh in role["meshes"] for vertex in mesh["vertices"]]
                tip_vertex_error = math.sqrt(min(squared_distance(to_body(tip), point) for point in hand_vertices))
                # Restrict to the local grip neighborhood, then choose the rim
                # centerline nearest the finger's transverse row at the origin.
                target = fit([0, finger["y"], 0])
                samples = [(squared_distance(from_body(rim_point(i * math.tau / 8192)), target), i * math.tau / 8192) for i in range(8192)]
                _, angle = min(samples)
                center = from_body(rim_point(angle))
                section = [from_body(rim_point(angle, i * math.tau / 24)) for i in range(24)]
                actual = sorted(wheel_vertices, key=lambda point: squared_distance(from_body(point), center))[:24]
                tip_rim_distance = min(math.sqrt(squared_distance(to_body(tip), vertex)) for vertex in wheel_vertices)
                occlusion = wheel_occlusion(eye, to_body(tip), triangles)
                pad_center_occlusion = wheel_occlusion(eye, to_body(pad_center), triangles)
                # A diagnostic contact proposal: follow the eye-to-centerline
                # ray through the actual rim's far exit, then clear the pad.
                # This is explicitly separate from authored/exported geometry.
                center_body = rim_point(angle)
                ray = [a - b for a, b in zip(center_body, eye)]
                ray_length = math.sqrt(sum(value * value for value in ray))
                ray_end = [a + 2 * b for a, b in zip(eye, ray)]
                hits = wheel_occlusion(eye, ray_end, triangles, all_hits=True)
                model_scale = math.sqrt(sum(value * value for value in role["matrix"][:3]))
                clearance = radii[-1] * finger["radius_scale"] * model_scale + .002
                proposed = [a + 2 * b * max(hits) + b / ray_length * clearance for a, b in zip(eye, ray)] if hits else None
                hand["fingers"].append({"index": index + 1, "name": finger.get("name", ("little", "ring", "middle", "index")[index]),
                    "configured_pre_fit_path_centers": centers, "fitted_path_centers": [fit(point) for point in centers],
                    "rounded_pad_center": pad_center, "tip_surface_pole": tip, "pole_to_nearest_exported_vertex_body_units": tip_vertex_error,
                    "definition_matches_exported_tip": tip_vertex_error < .0001,
                    "tip_point_kind": "Rounded surface pole" if "fingertip_steps" in definition else "Original flat terminal fan center",
                    "rim_center": center, "rim_section": section,
                    "actual_nearest_wheel_vertices": [from_body(point) for point in actual],
                    "tip_center_minus_rim_center": [a - b for a, b in zip(tip, center)],
                    "tip_body": to_body(tip), "rim_center_body": rim_point(angle),
                    "tip_body_minus_rim_center_body": [a - b for a, b in zip(to_body(tip), rim_point(angle))],
                    "actual_wheel_occludes_pad_center_from_driver": pad_center_occlusion is not None,
                    "actual_wheel_occludes_tip_pole_from_driver": occlusion is not None, "occluding_triangle_ray_fraction": occlusion,
                    "diagnostic_far_contact_proposal_canonical": from_body(proposed) if proposed else None,
                    "diagnostic_far_contact_proposal_body": proposed,
                    "tip_toward_driver_offset": sum((a - b) * direction for a, b, direction in zip(tip, center, driver)),
                    "tip_center_to_nearest_actual_wheel_vertex_body_units": tip_rim_distance})
            entry["hands"].append(hand)
        entry["definition_matches_exported_tips"] = all(finger["definition_matches_exported_tip"] for hand in entry["hands"] for finger in hand["fingers"])
        report["poses"].append(entry)
    (output / "grip-analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "hand-definition-snapshot.json").write_text(json.dumps(definition, indent=2) + "\n")
    return report


def blender_render(files, output, material, hand_choice, definition, framing_root=None):
    import bpy
    from mathutils import Matrix, Vector

    converter = shutil.which("magick") or shutil.which("convert")
    if converter is None:
        raise ValueError("ImageMagick with WebP support is required")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH" if material == "clay" else "CYCLES"
    scene.render.resolution_x, scene.render.resolution_y = 1024, 768
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.world = bpy.data.worlds.new("DiagnosticWorld")
    scene.world.color = (.08, .08, .08)
    if material == "clay":
        scene.display.shading.light = "STUDIO"
        scene.display.shading.studiolight_rotate_z = .45
        scene.display.shading.color_type = "MATERIAL"
        scene.display.shading.show_shadows = True
        scene.display.shading.show_cavity = True
        scene.display.shading.cavity_type = "BOTH"
        scene.display.shading.background_type = "WORLD"
    else:
        scene.cycles.samples = 16
        scene.world.use_nodes = True
        scene.world.node_tree.nodes["Background"].inputs[0].default_value = (.16, .16, .16, 1)
    camera_data = bpy.data.cameras.new("DiagnosticCamera")
    camera = bpy.data.objects.new("DiagnosticCamera", camera_data)
    scene.collection.objects.link(camera)
    camera_data.type = "ORTHO"
    camera_data.clip_start, camera_data.clip_end = .001, 20
    scene.camera = camera
    png_root = output / "intermediate"
    png_root.mkdir(exist_ok=True)
    results = []
    for path in files:
        document = json.loads(path.read_text())
        mega_document = json.loads(path.with_name(path.name.replace("-original.json", "-mega.json")).read_text())
        framing_path = (framing_root or path.parent) / path.name.replace("-original.json", "-mega.json")
        framing_document = json.loads(framing_path.read_text())
        # The original wheel is an alpha billboard. For shape comparison use
        # the same actual Mega rim with both hand profiles, never an opaque quad.
        if document["profile"] == "original":
            document["roles"] = [role for role in document["roles"] if role["role"] != 4] + [role for role in mega_document["roles"] if role["role"] == 4]
        for hand_index in ([0, 1] if hand_choice == "both" else [int(hand_choice)]):
            for obj in list(bpy.data.objects):
                if obj != camera:
                    bpy.data.objects.remove(obj, do_unlink=True)
            hand_role = next(role for role in document["roles"] if role["role"] == hand_index)
            from_body, _ = canonical_functions(hand_role, definition)
            hand_points = []
            for role in document["roles"]:
                if role["role"] not in (hand_index, 4):
                    continue
                materials = []
                for slot, texture in enumerate(role["textures"]):
                    mat = bpy.data.materials.new(f"role{role['role']}-{slot}")
                    mat.diffuse_color = (.61, .67, .73, 1) if role["role"] < 2 else (.28, .31, .34, 1)
                    if material == "textured":
                        source = path.parent / texture["file"]
                        converted = png_root / (source.stem + '-' + crc32(source) + ".png")
                        if not converted.exists():
                            subprocess.run([converter, str(source), str(converted)], check=True, capture_output=True)
                        mat.use_nodes = True
                        shader = mat.node_tree.nodes.get("Principled BSDF")
                        shader.inputs["Roughness"].default_value = .7
                        image = mat.node_tree.nodes.new("ShaderNodeTexImage")
                        image.image = bpy.data.images.load(str(converted), check_existing=True)
                        mat.node_tree.links.new(image.outputs["Color"], shader.inputs["Base Color"])
                        mat.node_tree.links.new(image.outputs["Alpha"], shader.inputs["Alpha"])
                    materials.append(mat)
                for mesh in role["meshes"]:
                    vertices = [vertex[:3] for vertex in mesh["vertices"]]
                    data = bpy.data.meshes.new(mesh["name"])
                    data.from_pydata(vertices, [], [face[:3] for face in mesh["faces"]])
                    data.update()
                    obj = bpy.data.objects.new(f"role{role['role']}-{mesh['name']}", data)
                    scene.collection.objects.link(obj)
                    for mat in materials:
                        data.materials.append(mat)
                    uv_layer = data.uv_layers.new()
                    for polygon, face in zip(data.polygons, mesh["faces"]):
                        polygon.material_index = max(face[3], 0)
                        polygon.use_smooth = document["profile"] == "mega" or len(mesh["vertices"][0]) >= 8
                        for loop in polygon.loop_indices:
                            vertex = mesh["vertices"][data.loops[loop].vertex_index]
                            # Native images are raster-down; Blender V is up.
                            # GLB v=CM3D_v+1. Repeat addressing means 1-v works
                            # for both positive GLB and negative canonical V.
                            uv_layer.data[loop].uv = (vertex[3], 1 - vertex[4])
                    if mesh["vertices"] and len(mesh["vertices"][0]) >= 8:
                        # CM3D intentionally duplicates chart vertices. Preserve
                        # its analytic normals rather than smoothing triangles
                        # according to Blender's unrelated vertex adjacency.
                        data.normals_split_custom_set_from_vertices([vertex[5:8] for vertex in mesh["vertices"]])
                    if role["role"] < 2:
                        hand_points.extend(Vector(point) for point in vertices if from_body(point)[0] > -.13)
            # Both original/Mega use identical cameras and the same rim, with
            # framing taken from the actual Mega hand rather than separate fits.
            framing = next(role for role in framing_document["roles"] if role["role"] == hand_index)
            hand_points = [Vector(vertex[:3]) for mesh in framing["meshes"] for vertex in mesh["vertices"] if from_body(vertex[:3])[0] > -.13]
            minimum = Vector(tuple(min(point[axis] for point in hand_points) for axis in range(3)))
            maximum = Vector(tuple(max(point[axis] for point in hand_points) for axis in range(3)))
            target = (minimum + maximum) / 2
            scale = max(maximum - minimum) * 1.6
            camera_data.ortho_scale = scale
            if material == "textured":
                for location, energy, size in [((-.8, -1.2, .8), 90, 1.0), ((1.2, -.4, -.8), 60, .8)]:
                    light_data = bpy.data.lights.new("DiagnosticSoftbox", "AREA")
                    light_data.energy, light_data.shape, light_data.size = energy, "DISK", size
                    light = bpy.data.objects.new("DiagnosticSoftbox", light_data)
                    scene.collection.objects.link(light)
                    light.location = target + Vector(location)
                    light.rotation_euler = (target - light.location).to_track_quat("-Z", "Y").to_euler()
            for name, offset in [("front-driver", (-1, 0, 0)), ("side", (0, 0, 1 if hand_index == 0 else -1)), ("back-far", (1, 0, 0))]:
                if name == "front-driver":
                    eye = transform(inverse(document["driver_world_to_view"]), [0, 0, 0]) if "driver_world_to_view" in document else [0, 0x6666 / 65536, (1 if document["car"] == 1 else -1) * 0x6666 / 65536]
                    camera.location = eye
                    camera_data.type = "PERSP"
                    camera_data.lens = 36 * (target - camera.location).length / (scale * 4 / 3)
                else:
                    camera_data.type = "ORTHO"
                    camera.location = target + Vector(offset) * 2
                direction = (target - camera.location).normalized()
                right = direction.cross(Vector((0, 1, 0))).normalized()
                up = right.cross(direction).normalized()
                rotation = Matrix((right, up, -direction)).transposed()
                camera.rotation_euler = rotation.to_euler()
                stem = f"{path.stem}-hand{hand_index}-{name}-{material}"
                intermediate = png_root / (stem + ".png")
                scene.render.filepath = str(intermediate)
                bpy.ops.render.render(write_still=True)
                final = output / (stem + ".webp")
                subprocess.run([converter, str(intermediate), "-strip", "-define", "webp:lossless=false", "-define", "webp:method=6",
                                "-quality", "70", str(final)], check=True, capture_output=True)
                results.append({"image": final.name, "crc32": crc32(final), "source": str(path), "source_crc32": crc32(path),
                                "car": document["car"], "pose": document["pose"], "profile": document["profile"], "hand": hand_index,
                                "view": name, "material": material, "wheel_profile": "mega",
                                "framing_source": str(framing_path), "framing_crc32": crc32(framing_path),
                                "comparison": "Original hands with the same Mega wheel" if document["profile"] == "original" else "Mega hands and Mega wheel",
                                "normals": "Native transformed vertex normals" if len(framing["meshes"][0]["vertices"][0]) >= 8 else "Blender reconstructed normals (older native export)",
                                "camera_body": list(camera.location), "target_body": list(target)})
    (output / ("renders-" + material + ".json")).write_text(json.dumps({"webp_quality": 70, "renders": results}, indent=2) + "\n")


def main():
    worker = "bpy" in sys.modules
    argv = sys.argv[sys.argv.index("--") + 1:] if worker else sys.argv[1:]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--poses", default="normal")
    parser.add_argument("--cars", default="0,1,9")
    parser.add_argument("--profiles", default="original,mega")
    parser.add_argument("--hand", choices=("0", "1", "both"), default="both")
    parser.add_argument("--material", choices=("clay", "textured"), default="clay")
    parser.add_argument("--definition", type=Path, default=PROJECT / "assets/models/cockpits/hands/definition.json", help="definition snapshot matching exported meshes")
    parser.add_argument("--framing-frames", type=Path, help="use this export directory's Mega geometry for identical before/after cameras")
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args(argv)
    frames, output = args.frames.resolve(strict=True), args.output.resolve()
    if not frames.is_relative_to(PROJECT / "build") or not output.is_relative_to(PROJECT / "build"):
        raise ValueError("Grip diagnostics must remain under ignored project build/")
    output.mkdir(parents=True, exist_ok=True)
    files = sorted(path for path in frames.glob("car*-*-*.json") if
                   (row := json.loads(path.read_text()))["pose"] in args.poses.split(",") and
                   str(row["car"]) in args.cars.split(",") and row["profile"] in args.profiles.split(","))
    if not files:
        raise ValueError("No matching native grip exports")
    definition_path = args.definition.resolve(strict=True)
    definition = json.loads(definition_path.read_text())
    framing_root = args.framing_frames.resolve(strict=True) if args.framing_frames is not None else None
    if framing_root is not None and not framing_root.is_relative_to(PROJECT / "build"):
        raise ValueError("Grip framing exports must remain under ignored project build/")
    if worker:
        blender_render(files, output, args.material, args.hand, definition, framing_root)
    else:
        report = grip_report(frames, files, output, definition, definition_path)
        print(f"Reported {len(report['poses'])} runtime grip poses in {output / 'grip-analysis.json'}", flush=True)
        if not args.report_only:
            blender = shutil.which("blender")
            if blender is None:
                raise ValueError("Blender is required; --report-only needs only Python")
            subprocess.run([blender, "--background", "--factory-startup", "--threads", "6", "--python", str(Path(__file__).resolve()), "--", *sys.argv[1:]], check=True)


if __name__ == "__main__":
    main()
