#!/usr/bin/env python3
"""Renault-specific UV/mask fitting and reproducible final artwork preparation."""

import argparse
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


from git_game_data import configured_data_root
import vehicle_textures
from vehicle_textures import (artwork_files, bmp_dimensions, bmp_pixels, crc32,
                              generated_bmp, rgb_bmp, texture_records)

PROJECT = Path(__file__).resolve().parent.parent
SLOTS = ("REN", "ListA", "ListB", "Num2", "Num22", "WSCRN", "SIDEG")


def lamp_opaque_uvs(meshes, texture_directory=None):
    """Return actual closed-lamp UV triangles that supersede the old atlas mask."""
    if texture_directory is None:
        return []
    directory = Path(texture_directory).resolve(strict=True)
    metadata = directory / "metadata.json"
    if not metadata.exists():
        return []
    if not metadata.resolve(strict=True).is_relative_to(directory) or metadata.stat().st_size > 8192:
        raise ValueError("Invalid Renault texture metadata path/size")
    selected = json.loads(metadata.read_text(encoding="utf-8")).get("opaque_lamp_meshes", [])
    if (not isinstance(selected, list) or
            any(type(index) is not int or index not in (16, 18, 45, 47) for index in selected) or
            len(set(selected)) != len(selected)):
        raise ValueError("Opaque lamp UV metadata only supports distinct healthy lamp meshes")
    triangles = []
    for index in selected:
        if index >= len(meshes):
            raise ValueError("Opaque lamp UV metadata exceeds generated mesh bounds")
        for face in meshes[index].faces:
            material = struct.unpack_from("<h", face, 8)[0]
            if material == -1:  # Actual gasket geometry has no atlas footprint.
                continue
            if material != 0:
                raise ValueError("Opaque lamp UV fitting requires the REN material")
            triangle = [meshes[index].vertices[i][3:5] for i in struct.unpack_from("<3H", face, 2)]
            triangle = [(u, v + 1) for u, v in triangle]
            if any(not math.isfinite(value) or not 0 <= value <= 1 for point in triangle for value in point):
                raise ValueError("Opaque lamp UV triangles must remain within [0,1]")
            triangles.append(triangle)
    return triangles


def wheel_arch_opaque_uvs(meshes):
    """Release the old coarse mask only on geometrically cut wheel-arch faces.

    The geometry helper marks surviving REN face indices after cutting the
    actual opening. Unmarked faces, including independent damage variants,
    retain their canonical transparency. There is no footprint over the hole.
    """
    triangles = []
    for mesh in meshes:
        selected = getattr(mesh, "wheel_arch_faces", set())
        if (not isinstance(selected, (set, frozenset)) or
                any(type(index) is not int or not 0 <= index < len(mesh.faces) for index in selected)):
            raise ValueError("Invalid geometrically cut wheel-arch face indices")
        for index in sorted(selected):
            face = mesh.faces[index]
            if len(face) != 24 or struct.unpack_from("<h", face, 8)[0] != 0:
                raise ValueError("Wheel-arch opacity requires a complete REN face")
            indices = struct.unpack_from("<3H", face, 2)
            if any(index >= len(mesh.vertices) for index in indices):
                raise ValueError("Wheel-arch opacity exceeds generated vertex bounds")
            triangle = [(mesh.vertices[index][3], mesh.vertices[index][4] + 1) for index in indices]
            if any(not math.isfinite(value) or not 0 <= value <= 1 for point in triangle for value in point):
                raise ValueError("Wheel-arch UV triangles must remain within [0,1]")
            triangles.append(triangle)
    return triangles


def _opaque_lamp_pixels(width, height, triangles):
    mask = bytearray(width * height)
    for triangle in triangles:
        points = [(u * width, v * height) for u, v in triangle]
        a, b, c = points
        area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(area) < 1e-10:
            continue
        sign = 1 if area > 0 else -1
        edges = [(a, b), (b, c), (c, a)]
        # Include one filtering texel around the surface's exact UV footprint.
        edges = [(p, q[0] - p[0], q[1] - p[1], math.dist(p, q)) for p, q in edges]
        for y in range(max(0, math.floor(min(p[1] for p in points) - 1)), min(height, math.ceil(max(p[1] for p in points) + 1))):
            for x in range(max(0, math.floor(min(p[0] for p in points) - 1)), min(width, math.ceil(max(p[0] for p in points) + 1))):
                if all(sign * (dx * (y + .5 - p[1]) - dy * (x + .5 - p[0])) >= -length
                       for p, dx, dy, length in edges):
                    mask[y * width + x] = 1
    return mask


def native_colorkey(name, original, replacement, opaque_uvs=()):
    """Fit generated colors to the renderer's exact RGB-black transparency."""
    if name not in ("REN", "WSCRN", "SIDEG", "ListA", "ListB", "Num2", "Num22"):
        return replacement
    width, height, pixels = bmp_pixels(replacement)
    if name in ("Num2", "Num22"):
        pixels = [(8, 8, 8) if max(pixel) <= 32 else pixel for pixel in pixels]
    elif name in ("ListA", "ListB"):
        pixels = [(0, 0, 0) if max(pixel) <= 32 else pixel for pixel in pixels]
    else:
        old_width, old_height, old_pixels = bmp_pixels(original)
        opaque = _opaque_lamp_pixels(width, height, opaque_uvs) if name == "REN" and opaque_uvs else None
        for y in range(height):
            for x in range(width):
                index = y * width + x
                source = old_pixels[(y * old_height // height) * old_width + x * old_width // width]
                if source == (0, 0, 0) and not (opaque and opaque[index]):
                    pixels[index] = (0, 0, 0)
                elif pixels[index] == (0, 0, 0):
                    pixels[index] = (8, 8, 8)
    return rgb_bmp(width, height, pixels)


def replace_textures(section, texture_count, directory=None, opaque_uvs=()):
    """Apply Renault masks to working BMP/PNG; final WebP is already prepared."""
    return vehicle_textures.replace_textures(
        section, texture_count, directory,
        prepare=lambda name, original, source: native_colorkey(name, original, generated_bmp(source), opaque_uvs))


def fit_name_uvs(meshes, texture_section, texture_count, texture_directory=None):
    """Fit final-row name decals using optional generated-asset metadata.

    metadata.json: {"name_uv_ranges": {"ListA": [0.895, 0.999],
                                       "ListB": [0.900, 0.999]}}
    The canonical final-row interval [0.875,1] maps into the declared interval;
    U, geometry, other materials and original no-replacement models stay exact.
    """
    if texture_directory is None:
        return
    directory = Path(texture_directory).resolve(strict=True)
    metadata = directory / "metadata.json"
    if not metadata.exists():
        return
    if not metadata.resolve(strict=True).is_relative_to(directory) or metadata.stat().st_size > 8192:
        raise ValueError("Invalid Renault texture metadata path/size")
    ranges = json.loads(metadata.read_text(encoding="utf-8")).get("name_uv_ranges", {})
    names = [name for name, _, _ in texture_records(texture_section, texture_count)]
    for name, interval in ranges.items():
        if name not in ("ListA", "ListB") or not isinstance(interval, list) or len(interval) != 2:
            raise ValueError("Name UV metadata requires ListA/ListB intervals")
        low, high = interval
        if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in interval) or not 0 <= low < high <= 1:
            raise ValueError("Name UV interval must remain within [0,1]")
    sources = artwork_files(directory, names)
    for mesh in meshes:
        selected = {}
        for face in mesh.faces:
            texture = struct.unpack_from("<h", face, 8)[0]
            name = names[texture] if texture >= 0 else None
            if name not in ranges or name not in sources:
                continue
            for index in struct.unpack_from("<3H", face, 2):
                if index in selected and selected[index] != name:
                    raise ValueError("Name UV fitting encountered shared material vertices")
                selected[index] = name
        for index, name in selected.items():
            vertex = mesh.vertices[index]
            v = vertex[4] + 1
            if not .875 <= v <= 1.00001:
                raise ValueError("Name UV fitting requires the original final-row interval")
            low, high = ranges[name]
            target = low + ((v - .875) / .125) * (high - low)
            mesh.vertices[index] = (*vertex[:4], target - 1, *vertex[5:])


def fit_rear_lamp_uvs(meshes, texture_directory=None):
    """Unfold each healthy lens into one complete existing atlas motif.

    A shared intrinsic chart crosses the rear/side fold. Only the measured
    convex glass outline samples the complete lens sprite; the remaining paint,
    upper white band and boot artwork retain their original texture coordinates.
    Splits interpolate positions/normals on the existing triangles exactly.
    """
    if texture_directory is None:
        return
    directory = Path(texture_directory).resolve(strict=True)
    metadata = directory / "metadata.json"
    if not metadata.exists():
        return
    if not metadata.resolve(strict=True).is_relative_to(directory) or metadata.stat().st_size > 8192:
        raise ValueError("Invalid Renault texture metadata path/size")
    entries = json.loads(metadata.read_text(encoding="utf-8")).get("rear_lamp_uvs", {})
    if not isinstance(entries, dict):
        raise ValueError("Rear lamp UV metadata requires a mesh mapping")
    if "REN" not in artwork_files(directory, SLOTS):
        return

    def cross(a, b, p):
        return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])

    def clip(polygon, a, b, sign):
        result = []
        if not polygon:
            return result
        previous = polygon[-1]
        before = sign * cross(a, b, previous[8:10])
        for current in polygon:
            distance = sign * cross(a, b, current[8:10])
            if (before >= -1e-12) != (distance >= -1e-12):
                factor = before / (before - distance)
                result.append(tuple(x + factor * (y - x) for x, y in zip(previous, current)))
            if distance >= -1e-12:
                result.append(current)
            previous, before = current, distance
        return [point for i, point in enumerate(result)
                if math.dist(point[:3], result[i - 1][:3]) > 1e-10]

    def barycentric(p, triangle):
        a, b, c = triangle
        area = cross(a, b, c)
        if abs(area) < 1e-12:
            raise ValueError("Degenerate rear lamp UV chart")
        wb = cross(a, p, c) / area
        wc = cross(a, b, p) / area
        return 1 - wb - wc, wb, wc

    for key, entry in entries.items():
        if key not in ("16", "18") or not isinstance(entry, dict):
            raise ValueError("Rear lamp projection supports healthy meshes16/18 only")
        index = int(key)
        if index >= len(meshes):
            raise ValueError("Rear lamp projection exceeds generated mesh bounds")
        mesh = meshes[index]
        paths = entry.get("paths")
        charts = entry.get("charts")
        contour = entry.get("target_outline")
        outline = entry.get("sprite_outline")
        if (not isinstance(paths, list) or len(paths) != 2 or
                not isinstance(charts, list) or len(charts) != 5 or
                not isinstance(contour, list) or not 4 <= len(contour) <= 48 or
                not isinstance(outline, list) or not 4 <= len(outline) <= 48):
            raise ValueError("Rear lamp requires paths, five charts and a measured lens contour")
        if any(not isinstance(pair, list) or len(pair) != 2 or
               any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in pair)
               for pair in contour + outline):
            raise ValueError("Rear lamp projection coordinates must remain within [0,1]")
        if any(not isinstance(path, list) or not 3 <= len(path) <= 4 or
               any(type(i) is not int or not 0 <= i < len(mesh.vertices) for i in path) for path in paths):
            raise ValueError("Invalid rear lamp unfolding path")
        if any(not isinstance(chart, list) or len(chart) != 3 or
               any(type(i) is not int or not 0 <= i < len(mesh.vertices) for i in chart) for chart in charts):
            raise ValueError("Invalid rear lamp chart indices")
        coordinates = {}
        for path in paths:
            points = [mesh.vertices[i][:3] for i in path]
            lengths = [math.dist(a, b) for a, b in zip(points, points[1:])]
            total = sum(lengths)
            if total < .01:
                raise ValueError("Degenerate rear lamp unfolding path")
            distance = 0
            for i, point in enumerate(points):
                coordinates[tuple(point)] = 1 - distance / total
                if i < len(lengths):
                    distance += lengths[i]
        top, bottom = (mesh.vertices[path[0]] for path in paths)
        if abs(top[1] - bottom[1]) < .01:
            raise ValueError("Rear lamp projection requires distinct panel heights")
        slope = (top[4] - bottom[4]) / (top[1] - bottom[1])
        intercept = top[4] + 1 - slope * top[1]
        prepared = []
        for chart in charts:
            source = [mesh.vertices[i] for i in chart]
            target = [(coordinates[tuple(v[:3])], slope * v[1] + intercept) for v in source]
            prepared.append(([(v[3], v[4] + 1) for v in source], target))
        contour = [tuple(point) for point in contour]
        if any(cross(a, b, c) < -1e-10 for a, b, c in zip(contour, contour[1:] + contour[:1], contour[2:] + contour[:2])):
            raise ValueError("Rear lamp target contour must be convex")

        def perimeter(points):
            low = [min(point[i] for point in points) for i in (0, 1)]
            span = [max(point[i] for point in points) - low[i] for i in (0, 1)]
            if min(span) < 1e-5:
                raise ValueError("Degenerate rear lamp contour")
            lengths = [math.hypot((b[0] - a[0]) / span[0], (b[1] - a[1]) / span[1])
                       for a, b in zip(points, points[1:] + points[:1])]
            total = sum(lengths)
            fractions = [0.]
            for length in lengths:
                fractions.append(fractions[-1] + length / total)
            return fractions

        target_steps, source_steps = perimeter(contour), perimeter(outline)
        steps = sorted({round(step, 12) for step in target_steps[:-1] + source_steps[:-1]})

        def at(points, fractions, value):
            for i in range(len(points)):
                if fractions[i] - 1e-11 <= value <= fractions[i + 1] + 1e-11:
                    weight = (value - fractions[i]) / (fractions[i + 1] - fractions[i])
                    return tuple(points[i][k] + weight * (points[(i + 1) % len(points)][k] - points[i][k]) for k in (0, 1))
            raise ValueError("Invalid rear lamp perimeter correspondence")

        target_ring = [at(contour, target_steps, step) for step in steps]
        source_ring = [at(outline, source_steps, step) for step in steps]
        target_center = tuple(sum(point[i] for point in contour) / len(contour) for i in (0, 1))
        source_center = tuple(sum(point[i] for point in outline) / len(outline) for i in (0, 1))
        fans = [(target_center, target_ring[i], target_ring[(i + 1) % len(steps)],
                 source_center, source_ring[i], source_ring[(i + 1) % len(steps)]) for i in range(len(steps))]
        original_vertices = list(mesh.vertices)
        output = []

        def emit(polygon, face, mapping=None):
            if len(polygon) < 3:
                return
            ids = []
            for point in polygon:
                u, v = point[3], point[4]
                if mapping:
                    weights = barycentric(point[8:10], mapping[:3])
                    u, sample_v = (sum(weight * sample[i] for weight, sample in zip(weights, mapping[3:])) for i in (0, 1))
                    v = sample_v - 1
                ids.append(len(mesh.vertices))
                mesh.vertices.append((*point[:3], u, v, *point[5:8]))
            for i in range(1, len(ids) - 1):
                a, b, c = [tuple(round(value * 65536) for value in mesh.vertices[j][:3]) for j in (ids[0], ids[i], ids[i + 1])]
                ab, ac = [tuple(b[k] - a[k] for k in range(3)), tuple(c[k] - a[k] for k in range(3))]
                area = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
                if any(area):
                    encoded = bytearray(face)
                    struct.pack_into("<3H", encoded, 2, ids[0], ids[i], ids[i + 1])
                    output.append(bytes(encoded))

        for face in mesh.faces:
            if struct.unpack_from("<h", face, 8)[0] != 0:
                raise ValueError("Rear lamp projection requires the REN material")
            vertices = [original_vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            center = tuple(sum(v[i] + (1 if i == 4 else 0) for v in vertices) / 3 for i in (3, 4))
            candidates = [(min(barycentric(center, source)), source, target) for source, target in prepared]
            score, source, target = max(candidates, key=lambda c: c[0])
            if score < -.0001:
                raise ValueError("Rear lamp face lies outside its source charts")
            polygon = []
            for vertex in vertices:
                weights = barycentric((vertex[3], vertex[4] + 1), source)
                q = tuple(sum(weight * point[i] for weight, point in zip(weights, target)) for i in (0, 1))
                polygon.append((*vertex, *q))
            for a, b in zip(contour, contour[1:] + contour[:1]):
                emit(clip(polygon, a, b, -1), face)
                polygon = clip(polygon, a, b, 1)
            for fan in fans:
                part = polygon
                triangle = fan[:3]
                for a, b in zip(triangle, triangle[1:] + triangle[:1]):
                    part = clip(part, a, b, 1)
                emit(part, face, fan)
        mesh.faces = output


def fit_front_lamp_uvs(meshes, texture_directory=None):
    """Continue healthy front glass around its corner in the same atlas island.

    The separate AI side-profile artwork cannot meet all front-lens contours.
    Keep front UVs exact; fold the short side surface into a narrow strip of
    that same lens. Shared physical seam points then sample identical pixels.
    Original geometry, normals, faces and independent damage islands stay exact.
    """
    if texture_directory is None:
        return
    directory = Path(texture_directory).resolve(strict=True)
    metadata = directory / "metadata.json"
    if not metadata.exists():
        return
    if not metadata.resolve(strict=True).is_relative_to(directory) or metadata.stat().st_size > 8192:
        raise ValueError("Invalid Renault texture metadata path/size")
    config = json.loads(metadata.read_text(encoding="utf-8")).get("front_lamp_uvs")
    if config is None:
        return
    if not isinstance(config, dict) or set(config) != {"wrap_width"}:
        raise ValueError("Front lamp UV metadata requires only wrap_width")
    width = config["wrap_width"]
    if type(width) not in (int, float) or not math.isfinite(width) or not .002 <= width <= .02:
        raise ValueError("Front lamp UV wrap width must be within [0.002,0.02]")
    if "REN" not in artwork_files(directory, SLOTS):
        return
    for index, direction, side_seam in ((45, 1, (6, 7)), (47, -1, (5, 7))):
        if index >= len(meshes) or len(meshes[index].vertices) < 9 or len(meshes[index].faces) < 2:
            raise ValueError("Front lamp UV fitting requires complete healthy lamp meshes")
        mesh = meshes[index]
        top, bottom = (mesh.vertices[i] for i in (3, 4))
        if any(mesh.vertices[a][:3] != mesh.vertices[b][:3] for a, b in zip(side_seam, (3, 4))):
            raise ValueError("Front lamp UV fitting requires the original shared corner seam")
        selected = {i for face in mesh.faces[:2] for i in struct.unpack_from("<3H", face, 2)}
        if selected != {5, 6, 7, 8} or any(struct.unpack_from("<h", face, 8)[0] != 0 for face in mesh.faces[:2]):
            raise ValueError("Front lamp UV fitting requires the original side faces")
        span = bottom[1] - top[1]
        coordinates = {}
        for i in selected:
            vertex = mesh.vertices[i]
            t = (vertex[1] - top[1]) / span
            seam_z = top[2] + t * (bottom[2] - top[2])
            distance = direction * (vertex[2] - seam_z)
            if not -1e-8 <= t <= 1.00000001 or distance < -1e-8:
                raise ValueError("Front lamp side surface exceeds the original seam bounds")
            coordinates[i] = (t, max(0., distance))
        depth = max(distance for _, distance in coordinates.values())
        if depth <= 1e-5:
            raise ValueError("Front lamp side surface has no wrap depth")
        for i, (t, distance) in coordinates.items():
            vertex = mesh.vertices[i]
            u = top[3] + t * (bottom[3] - top[3]) - direction * width * distance / depth
            v = top[4] + t * (bottom[4] - top[4])
            # At the outer corner the original front projection reaches the
            # white sponsor band. Keep this glass/paint edge in adjacent yellow;
            # fade to zero at the shared front seam and at the upper glass edge.
            v += t * distance / depth * (.490 - (bottom[4] + 1))
            if not 0 <= u <= 1 or not -1 <= v <= 0:
                raise ValueError("Front lamp UV projection exceeds texture bounds")
            mesh.vertices[i] = (*vertex[:3], u, v, *vertex[5:])


def extract(source, output):
    """Extract member25's original BMP bytes only below ignored build/."""
    output = Path(output).resolve()
    if not output.is_relative_to(PROJECT / "build"):
        raise ValueError("Original texture extraction must remain under ignored build/")
    data = Path(source).read_bytes()
    if crc32(data) != "92273ec9":
        raise ValueError("Expected unchanged canonical1.1a allcars.bfp (CRC32 92273ec9)")
    count, table_size = struct.unpack_from("<II", data)
    if count != 34 or table_size != count * 24:
        raise ValueError("Unexpected canonical car archive directory")
    offset, size = struct.unpack_from("<II", data, 12 + 25 * 24 + 16)
    model = data[12 + table_size + offset:12 + table_size + offset + size]
    if crc32(model) != "e0fffe5d":
        raise ValueError("Unexpected Renault high member CRC32")
    texture_start = struct.unpack_from("<I", model, 28)[0]
    records = texture_records(model[texture_start:], struct.unpack_from("<H", model, 16)[0])
    output.mkdir(parents=True, exist_ok=True)
    report = {"textures": [], "meshes": []}
    for index, (name, _, bmp) in enumerate(records):
        path = output / (name + ".bmp")
        if path.is_symlink():
            raise ValueError("Refusing to extract through a symbolic link")
        path.write_bytes(bmp)
        width, height, bits = bmp_dimensions(bmp)
        report["textures"].append(dict(index=index, name=name, width=width, height=height, bits=bits, crc32=crc32(bmp), path=str(path)))
    offset = struct.unpack_from("<I", model, 24)[0]
    for index in range(struct.unpack_from("<H", model, 14)[0]):
        name = model[offset:offset + 12].split(b"\0", 1)[0].decode("ascii")
        size, vertices, faces = struct.unpack_from("<IHH", model, offset + 12)
        materials = {}
        for face in range(faces):
            texture = struct.unpack_from("<h", model, offset + 48 + vertices * 32 + face * 24 + 8)[0]
            material = records[texture][0] if texture >= 0 else "flat-color"
            materials[material] = materials.get(material, 0) + 1
        report["meshes"].append(dict(index=index, name=name, materials=materials))
        offset += size
    path = output / "materials.json"
    if path.is_symlink():
        raise ValueError("Refusing to extract through a symbolic link")
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def finalize(verify=False):
    """Prepare the accepted lossless WebP work sources once, before final WebP70 encoding.

    Temporary BMP/CM3D bytes stay local. Normal builds consume the resulting
    WebP files directly and never apply masks or lossy encoding to them again.
    """
    from vehicle_manifest import load_definition, load_manifest
    from vehicle_model import decode, read_source
    from renault_geometry import generate

    entry = next(entry for entry in load_manifest()["vehicles"] if entry["key"] == "renault")
    package = PROJECT / entry["texture_directory"]
    provenance_path = package / "provenance.json"
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    quality = provenance.get("finalization", {}).get("quality", 70) if verify else 70
    sources = provenance["assets"]
    metadata = (package / "metadata.json").read_bytes()
    with tempfile.TemporaryDirectory(prefix="toca-renault-finalize-") as temporary:
        working = Path(temporary)
        (working / "metadata.json").write_bytes(metadata)
        for asset in sources:
            source = (package / asset["file"]).resolve(strict=True)
            if not source.is_relative_to(package / "work") or source.suffix != ".webp":
                raise ValueError("Renault artwork work sources must be package-local lossless WebP files")
            if crc32(source.read_bytes()) != asset["crc32"]:
                raise ValueError(f"Artwork work source CRC32 mismatch: {asset['file']}")
            vehicle_textures.lossless_webp_dimensions(source.read_bytes())
            (working / "work").mkdir(exist_ok=True)
            shutil.copyfile(source, working / "work" / source.name)
        prepared, _ = generate(read_source(entry["source"]), load_definition(entry), working)
        header, _, _, section = decode(prepared)
        prepared_records = {name: payload for name, _, payload in
                            texture_records(section, struct.unpack_from("<H", header, 16)[0])}
        outputs = []
        for asset in sources:
            name = Path(asset["file"]).stem
            payload = prepared_records[name]
            final = vehicle_textures.webp_texture(payload, quality=quality)
            output = package / (name + ".webp")
            if verify:
                if not output.is_file() or output.read_bytes() != final:
                    raise ValueError(f"Final artwork is not reproducible: {output.name}")
            else:
                output.write_bytes(final)
            outputs.append(dict(file=output.name, source=asset["file"], source_crc32=asset["crc32"],
                                prepared_bmp_crc32=crc32(payload), dimensions=list(vehicle_textures.webp_dimensions(final)),
                                bytes=len(final), crc32=crc32(final)))
    if not verify:
        provenance["finalization"] = {"quality": quality}
        provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    return dict(verified=verify, textures=len(outputs), bytes=sum(output["bytes"] for output in outputs), assets=outputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    extraction = commands.add_parser("extract")
    extraction.add_argument("--source", type=Path, help="ALLCARS archive; defaults to TOCA_DATA_ROOT/data/allcars.bfp")
    extraction.add_argument("--output", type=Path, default=PROJECT / "build/renault-texture-source")
    conversion = commands.add_parser("convert")
    conversion.add_argument("--source", type=Path, required=True)
    conversion.add_argument("--output", type=Path, required=True)
    finalization = commands.add_parser("finalize", help="Prepare accepted lossless WebP work sources into final package WebP70")
    finalization.add_argument("--verify", action="store_true", help="Reproduce and compare final artwork without writing")
    args = parser.parse_args()
    if args.command == "extract":
        source = args.source if args.source is not None else configured_data_root() / "data/allcars.bfp"
        print(json.dumps(extract(source, args.output), indent=2))
    elif args.command == "finalize":
        print(json.dumps(finalize(args.verify), indent=2))
    else:
        if args.source.suffix.lower() not in (".png", ".webp") or args.output.suffix.lower() != ".bmp":
            parser.error("convert requires a PNG/lossless WebP source and BMP output")
        bmp = generated_bmp(args.source.resolve(strict=True))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(bmp)
        print(json.dumps(dict(output=str(args.output), crc32=crc32(bmp), dimensions=bmp_dimensions(bmp))))


if __name__ == "__main__":
    main()
