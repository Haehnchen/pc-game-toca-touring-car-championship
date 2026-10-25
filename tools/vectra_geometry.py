"""Vauxhall Vectra exteriors from canonical ALLCARS HIGH members7 and10.

All component positions, UV islands and variant slots come from this Vectra.
V10 shares the algorithms, retaining its own body positions, pivots and artwork.
Each definition is bound to its exact source rather than substituting member7.
"""

from functools import partial
import json
import math
from pathlib import Path
import struct

from vehicle_model import (Mesh, add, sub, scale, dot, cross, unit, interpolate,
                           polygon, cylinder, decode, pack, crc32, visible_triangles)
import vehicle_geometry as panels

PAINT = 0
RUBBER = (8, 8, 8, 255)
RED = (255, 0, 0, 255)  # Original mirror shell.
GLASS = (120, 197, 238, 255)
SILVER = (190, 206, 219, 255)  # Original VECTRA wheel palette.
BRIGHT = (222, 239, 255, 255)
RECESS = (49, 49, 49, 255)
LAMP_SLOTS = (40, 42, 44, 46)

uv_vertex = partial(panels.uv_vertex, paint=PAINT)
recess_uv = partial(panels.recess_uv, paint=PAINT, wall_color=RUBBER)
curved_mesh = partial(panels.curved_mesh, paint=PAINT)


def wheel_mesh(header, definition):
    """One fifteen-spoke silver rim, with no painted spoke layer behind it."""
    mesh = Mesh(header, [], [])
    segments, profile = definition['wheel_segments'], definition['wheel_profile']
    rings = []
    for row, (z, radius) in enumerate(profile):
        a, b = profile[max(0, row - 1)], profile[min(len(profile) - 1, row + 1)]
        dz, dr = b[0] - a[0], b[1] - a[1]
        rings.append([mesh.vertex((radius * math.cos(math.tau * i / segments), radius * math.sin(math.tau * i / segments), z),
                                  (dz * math.cos(math.tau * i / segments), dz * math.sin(math.tau * i / segments), -dr))
                      for i in range(segments)])
    for a, b in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            mesh.quad((a[i], a[j], b[j], b[i]), RUBBER)
    radius, spoke, hub = (definition[k] for k in ('wheel_rim_radius', 'wheel_spoke_radius', 'wheel_hub_radius'))
    for side in (-1, 1):
        center = mesh.vertex((0, 0, side * .096), (0, 0, side))
        disk, lip = [], []
        for i in range(segments):
            c, s = math.cos(math.tau * i / segments), math.sin(math.tau * i / segments)
            disk.append(mesh.vertex((spoke * c, spoke * s, side * .096), (0, 0, side)))
            lip.append(mesh.vertex((radius * c, radius * s, side * .125), (0, 0, side)))
        for i in range(segments):
            j = (i + 1) % segments
            mesh.triangle((center, disk[i], disk[j]), RECESS)
            mesh.quad((disk[i], disk[j], lip[j], lip[i]), SILVER)
        for i in range(definition['wheel_spokes']):
            angle = math.tau * i / definition['wheel_spokes']
            radial, tangent = (math.cos(angle), math.sin(angle)), (-math.sin(angle), math.cos(angle))
            outline = [(radial[0] * r + tangent[0] * w, radial[1] * r + tangent[1] * w, side * .102)
                       for r, w in ((hub, -.009), (spoke, -.007), (spoke, .007), (hub, .009))]
            top = [(p[0], p[1], side * (.134 if j in (0, 3) else .123)) for j, p in enumerate(outline)]
            polygon(mesh, top, BRIGHT, (0, 0, side))
            polygon(mesh, list(reversed(outline)), SILVER, (0, 0, -side))
            for j, p in enumerate(outline):
                k = (j + 1) % 4
                polygon(mesh, (p, outline[k], top[k], top[j]), SILVER)
        cylinder(mesh, (0, 0, side * .098), (0, 0, side * .141), hub, SILVER, segments=20)
        for i in range(5):
            angle = math.tau * i / 5
            point = (.033 * math.cos(angle), .033 * math.sin(angle), side * .142)
            cylinder(mesh, point, add(point, (0, 0, side * .002)), .0045, RECESS, segments=6)
    return mesh


def mirror(original):
    """Round the source glass perimeter and close its red Vectra casing."""
    mesh = Mesh(original.header, [], [])
    glass = Mesh(original.header, list(original.vertices), [f for f in original.faces if tuple(f[10:14]) == GLASS])
    edges = panels.source_boundary(glass)
    if len(edges) != 4:
        raise ValueError('Vectra mirror must retain its four source glass edges')
    outline = [edges[0][0][:3], edges[0][1][:3]]
    while len(outline) < 4:
        candidates = [(a, b) for a, b in edges if a[:3] == outline[-1] and b[:3] not in outline]
        if not candidates:
            candidates = [(b, a) for a, b in edges if b[:3] == outline[-1] and a[:3] not in outline]
        if len(candidates) != 1:
            raise ValueError('Vectra mirror glass must have one closed boundary')
        outline.append(candidates[0][1][:3])
    if cross(sub(outline[1], outline[0]), sub(outline[2], outline[0]))[0] > 0:
        outline.reverse()
    perimeter = []
    for i, b in enumerate(outline):
        a, c = outline[i - 1], outline[(i + 1) % 4]
        start, end = interpolate(a, b, .84), interpolate(b, c, .16)
        for j in range(5):
            t = j / 4
            perimeter.append(add(add(scale(start, (1 - t) ** 2), scale(b, 2 * t * (1 - t))), scale(end, t * t)))
    center = tuple(sum(p[k] for p in perimeter) / len(perimeter) for k in range(3))
    front_x = max(p[0] for p in outline) + .080
    front = [(front_x, center[1] + (p[1] - center[1]) * .87, center[2] + (p[2] - center[2]) * .88) for p in perimeter]
    inner = [(p[0] + .003, center[1] + (p[1] - center[1]) * .88, center[2] + (p[2] - center[2]) * .88) for p in perimeter]
    front_center, glass_center = (front_x + .004, center[1], center[2]), add(center, (.003, 0, 0))
    for i, p in enumerate(perimeter):
        j = (i + 1) % len(perimeter)
        polygon(mesh, (p, front[i], front[j], perimeter[j]), RED)
        polygon(mesh, (p, perimeter[j], inner[j], inner[i]), RUBBER)
        polygon(mesh, (glass_center, inner[i], inner[j]), GLASS)
        polygon(mesh, (front_center, front[j], front[i]), RED)
    attachment = max(original.vertices, key=lambda v: v[0])[:3]
    cylinder(mesh, front_center, attachment, .014, RED, segments=10)
    return mesh


def wing_faces(source):
    """Select only the source wing and endplates, excluding the deck/lamp panel."""
    result = []
    for face in source.faces:
        points = [source.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
        if min(p[1] for p in points) >= .20 and max(p[0] for p in points) < -1.75 and max(abs(p[2]) for p in points) >= .69:
            result.append(face)
    return result


def solid_wing(mesh, source):
    """Close the actual top and paired endplates with a four-millimetre shell."""
    unique, normals, edges = {}, {}, {}
    for face in wing_faces(source):
        vertices = [source.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        key = tuple(sorted(p[:3] for p in vertices))
        if key in unique:
            continue
        normal = unit(cross(sub(vertices[1][:3], vertices[0][:3]), sub(vertices[2][:3], vertices[0][:3])))
        # Source wing includes both faces of the zero-thickness plates.
        if (abs(normal[2]) > .5 and normal[2] * sum(p[2] for p in vertices) < 0) or (abs(normal[1]) > .5 and normal[1] < 0):
            vertices.reverse()
            normal = scale(normal, -1)
        unique[key] = vertices, normal, face
        for p in vertices:
            normals[p[:3]] = add(normals.get(p[:3], (0, 0, 0)), normal)
        for a, b in zip(vertices, vertices[1:] + vertices[:1]):
            edges.setdefault(panels.edge_key(a, b), []).append((a, b))
    back = {p: sub(p, scale(unit(n), .004)) for p, n in normals.items()}
    for vertices, normal, face in unique.values():
        texture, color = struct.unpack_from('<h', face, 8)[0], tuple(face[10:14])
        polygon(mesh, [p[:3] for p in vertices], color, normal, [p[3:5] for p in vertices], texture)
        polygon(mesh, [back[p[:3]] for p in reversed(vertices)], RUBBER, scale(normal, -1))
    for uses in edges.values():
        if len(uses) == 1:
            a, b = uses[0]
            polygon(mesh, (a[:3], back[a[:3]], back[b[:3]], b[:3]), RUBBER)


def subdivide_lens(original, steps=7, depth=.0016):
    """Subdivide the complete source chart, preserving paint/rim/glass proportions."""
    mesh = Mesh(original.header, [], [])
    for face in original.faces:
        vertices = [original.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        grid = {}
        for i in range(steps + 1):
            for j in range(steps + 1 - i):
                weights = i / steps, j / steps, 1 - (i + j) / steps
                point = tuple(sum(weights[a] * vertices[a][k] for a in range(3)) for k in range(8))
                normal = unit(point[5:])
                position = add(point[:3], scale(normal, depth * 27 * math.prod(weights)))
                grid[i, j] = mesh.vertex(position, normal, point[3:5])
        for i in range(steps):
            for j in range(steps - i):
                mesh.triangle((grid[i, j], grid[i + 1, j], grid[i, j + 1]), (255, 255, 255, 255), PAINT)
                if i + j < steps - 1:
                    mesh.triangle((grid[i + 1, j], grid[i + 1, j + 1], grid[i, j + 1]), (255, 255, 255, 255), PAINT)
    for a, b in panels.source_boundary(original):
        normal = unit(add(a[5:], b[5:]))
        polygon(mesh, (a[:3], b[:3], sub(b[:3], scale(normal, .003)), sub(a[:3], scale(normal, .003))),
                (255, 255, 255, 255), uvs=(a[3:5], b[3:5], b[3:5], a[3:5]), texture=PAINT)
    return mesh


def arch_coordinates(vertex, side):
    u, v = vertex[3] * 256, (vertex[4] + 1) * 256
    return (u - 2.248, 255.99 - v) if side > 0 else (u, v)


def smooth_wheel_arches(meshes, definition):
    panels.smooth_wheel_arches(meshes, definition, paint=PAINT,
                              atlas_coordinates=arch_coordinates, minimum_side=.55)
    # The canonical texture cutout extends beyond the real lower mesh edge.
    # Release only the actual surviving paint beside those four lower joins;
    # an alpha staircase must not cut holes through the continuous geometry.
    for index in definition['arch_meshes']:
        mesh = meshes[index]
        mesh.lower_arch_opaque_uvs = []
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != PAINT:
                continue
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            side = 1 if sum(p[2] for p in vertices) > 0 else -1
            if min(side * p[2] for p in vertices) < .55:
                continue
            for _, _, _, _, widths in definition['wheel_arches']:
                bottom, left, right = widths[0]
                for edge in (left, right):
                    x0, x1, y0, y1 = edge - 3, edge + 3, 0, bottom + 4
                    if side > 0:
                        x0, x1, y0, y1 = x0 + 2.248, x1 + 2.248, 255.99 - y1, 255.99 - y0
                    clipped = vertices
                    for axis, bound, sign in ((3, x0 / 256, 1), (3, x1 / 256, -1),
                                              (4, y0 / 256 - 1, 1), (4, y1 / 256 - 1, -1)):
                        clipped = panels.clip(clipped, axis, bound, sign)
                    mesh.lower_arch_opaque_uvs.extend(_uv_triangles(clipped))


def _uv_triangles(vertices):
    return [tuple((p[3], p[4] + 1) for p in (vertices[0], vertices[i], vertices[i + 1]))
            for i in range(1, len(vertices) - 1)
            if abs((vertices[i][3] - vertices[0][3]) * (vertices[i + 1][4] - vertices[0][4]) -
                   (vertices[i][4] - vertices[0][4]) * (vertices[i + 1][3] - vertices[0][3])) > 1e-12]


def wheel_arch_opaque_uvs(meshes):
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', mesh.faces[f], 2))
            for mesh in meshes for f in sorted(getattr(mesh, 'wheel_arch_faces', ()))
            if struct.unpack_from('<h', mesh.faces[f], 8)[0] == PAINT] + [
                triangle for mesh in meshes if not hasattr(mesh, 'lower_arch_faces')
                for triangle in getattr(mesh, 'lower_arch_opaque_uvs', ())] + [
                triangle for mesh in meshes for f in sorted(getattr(mesh, 'lower_arch_faces', ()))
                for triangle in _uv_triangles([mesh.vertices[i] for i in struct.unpack_from('<3H', mesh.faces[f], 2)])]


def _emit_uv_polygon(mesh, vertices, template, mapping=None):
    """Split only the existing panel, copying its position and normal field."""
    if len(vertices) < 3:
        return
    indices = []
    for p in vertices:
        uv = mapping(p[3:5]) if mapping else p[3:5]
        indices.append(len(mesh.vertices))
        mesh.vertices.append((*p[:3], *uv, *p[5:]))
    for i in range(1, len(indices) - 1):
        face = bytearray(template)
        struct.pack_into('<3H', face, 2, indices[0], indices[i], indices[i + 1])
        mesh.faces.append(bytes(face))


def register_lower_arch_paint(meshes, definition, directory):
    """Keep real lower arch paint out of the redraw's nearby black openings.

    Only existing source-supported triangles beside the lower opening edges
    are split. Positions, normals, opening widths and every upper/body UV stay
    unchanged. Safe paint columns are measured in the accepted Vectra artwork.
    """
    metadata = json.loads((Path(directory) / 'metadata.json').read_text(encoding='utf-8'))
    regions = metadata.get('lower_arch_paint_uvs', [])
    if not regions:
        return
    if len(regions) != 4 or {(r.get('side'), r.get('arch')) for r in regions} != {(-1, 0), (-1, 1), (1, 0), (1, 1)}:
        raise ValueError('Vectra lower paint requires its four existing arch charts')
    for record in regions:
        side, arch, rows = record['side'], record['arch'], record.get('rows')
        if (not isinstance(rows, list) or len(rows) != 5 or [r[0] for r in rows] != [8, 12, 16, 20, 22] or
                any(len(r) != 3 or any(type(v) not in (int, float) or not math.isfinite(v) for v in r) or
                    not 0 <= r[1] < r[2] <= 256 for r in rows)):
            raise ValueError('Vectra lower paint measurements must be bounded source256 rows')
        for edge in (0, 1):
            old_edge = definition['wheel_arches'][arch][4][0][edge + 1]
            x0, x1, y0, y1 = old_edge - 7, old_edge + 7, 0, 26
            if side > 0:
                x0, x1, y0, y1 = x0 + 2.248, x1 + 2.248, 255.99 - y1, 255.99 - y0
            limits = ((3, x0 / 256, 1), (3, x1 / 256, -1),
                      (4, y0 / 256 - 1, 1), (4, y1 / 256 - 1, -1))

            def mapping(uv):
                x, y = uv[0] * 256, (uv[1] + 1) * 256
                if side > 0:
                    x, y = x - 2.248, 255.99 - y
                sample_y = max(8, y)
                safe = rows[-1][edge + 1]
                for a, b in zip(rows, rows[1:]):
                    if a[0] - 1e-7 <= sample_y <= b[0] + 1e-7:
                        safe = a[edge + 1] + (sample_y - a[0]) / (b[0] - a[0]) * (b[edge + 1] - a[edge + 1])
                        break
                registered_x = min(x, safe) if edge == 0 else max(x, safe)
                # Join the unchanged upper chart without a hard UV boundary.
                t = max(0.0, min(1.0, (y - 22) / 4))
                weight = 1 - t * t * (3 - 2 * t)
                x += weight * (registered_x - x)
                if side > 0:
                    x, sample_y = x + 2.248, 255.99 - sample_y
                return x / 256, sample_y / 256 - 1

            for index in definition['arch_meshes']:
                mesh = meshes[index]
                old_faces = mesh.faces
                prior_arch = getattr(mesh, 'wheel_arch_faces', ())
                prior_lower = getattr(mesh, 'lower_arch_faces', ())
                mesh.faces, arch_marks, lower_marks = [], set(), set()
                for face_index, face in enumerate(old_faces):
                    start = len(mesh.faces)
                    vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                    inside, outside = vertices, []
                    if struct.unpack_from('<h', face, 8)[0] == PAINT and min(side * p[2] for p in vertices) >= .55:
                        for axis, bound, sign in limits:
                            outside.append(panels.clip(inside, axis, bound, -sign))
                            inside = panels.clip(inside, axis, bound, sign)
                    else:
                        inside = []
                    if not _uv_triangles(inside):
                        mesh.faces.append(face)
                    else:
                        for part in outside:
                            _emit_uv_polygon(mesh, part, face)
                        first = len(mesh.faces)
                        for low, high in zip((0, 8, 12, 16, 20, 22, 23, 24, 25), (8, 12, 16, 20, 22, 23, 24, 25, 26)):
                            a, b = (255.99 - high, 255.99 - low) if side > 0 else (low, high)
                            part = panels.clip(panels.clip(inside, 4, a / 256 - 1, 1), 4, b / 256 - 1, -1)
                            _emit_uv_polygon(mesh, part, face, mapping)
                        lower_marks.update(range(first, len(mesh.faces)))
                    if face_index in prior_arch:
                        arch_marks.update(range(start, len(mesh.faces)))
                    if face_index in prior_lower:
                        lower_marks.update(range(start, len(mesh.faces)))
                mesh.wheel_arch_faces, mesh.lower_arch_faces = arch_marks, lower_marks
    remove_quantized_slivers(meshes, definition['arch_meshes'])


def deck_seam_opaque_uvs(meshes):
    """Release old key pixels only on the two exact existing deck-edge strips.

    Source deck20/21 and side8/11 have identical physical endpoints and shared
    PN controls. Their tiny source-era see-through seam comes from key pixels
    touching the boundary of the deck UV island, not an absent body panel.
    """
    result = []
    for index in (20, 21):
        mesh = meshes[index]
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != PAINT:
                continue
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            for a, b in (((123.113, 84.102), (126.750, 109.703)),
                         ((188.754, 84.102), (185.117, 109.703))):
                outline = [(a[0] - 1.25, a[1]), (b[0] - 1.25, b[1]),
                           (b[0] + 1.25, b[1]), (a[0] + 1.25, a[1])]
                # This ordering is clockwise in UV; clip toward its right side.
                clipped = vertices
                for p, q in zip(outline, outline[1:] + outline[:1]):
                    p, q = (p[0] / 256, p[1] / 256 - 1), (q[0] / 256, q[1] / 256 - 1)
                    clipped = _clip_uv_line(clipped, p, q, -1)
                result.extend(_uv_triangles(clipped))
    return result


def _clip_uv_line(vertices, a, b, sign):
    def distance(p):
        return sign * ((b[0] - a[0]) * (p[4] - a[1]) - (b[1] - a[1]) * (p[3] - a[0]))
    result = []
    if not vertices:
        return result
    previous, dp = vertices[-1], distance(vertices[-1])
    for current in vertices:
        dc = distance(current)
        if (dp >= 0) != (dc >= 0):
            result.append(interpolate(previous, current, dp / (dp - dc)))
        if dc >= 0:
            result.append(current)
        previous, dp = current, dc
    return result


def lamps_opaque_uvs(meshes):
    return [tuple((meshes[index].vertices[i][3], meshes[index].vertices[i][4] + 1) for i in struct.unpack_from('<3H', face, 2))
            for index in LAMP_SLOTS for face in meshes[index].faces
            if struct.unpack_from('<h', face, 8)[0] == PAINT]


def register_front_lamp_frames(meshes, originals):
    """Register V10's front frame to the complete original side return.

    Source corner UVs crop the new front frame before its lower trim and
    sample white paint above its upper trim. Correct only the two fold
    vertices on each front lens and their existing lower bumper continuation;
    the rounded side atlas retains its original UVs and complete silhouette.
    """
    def barycentric(point, triangle):
        a, b, c = triangle
        denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / denominator
        wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / denominator
        return wa, wb, 1 - wa - wb

    # Measured from the final1024px WebP: the two independently redrawn side
    # returns have slightly different antialiasing at the lower cutoff.
    bottom_rows = {44: 57.1681, 46: 57.2224}
    for lamp, top in ((44, 1), (46, 0)):
        source = originals[lamp]
        targets = [p[3:5] for p in source.vertices]
        targets[top] = (targets[top][0], 48.2 / 256 - 1)
        targets[2] = (targets[2][0], bottom_rows[lamp] / 256 - 1)
        charts = [struct.unpack_from('<3H', face, 2) for face in source.faces]
        for index, p in enumerate(meshes[lamp].vertices):
            for indices in charts:
                weights = barycentric(p[3:5], [source.vertices[i][3:5] for i in indices])
                if min(weights) >= -1e-7:
                    v = sum(w * targets[i][1] for w, i in zip(weights, indices))
                    meshes[lamp].vertices[index] = (*p[:4], v, *p[5:])
                    break
            else:
                raise ValueError('Vectra2 front lens escaped its original chart')

    # The original lens ends above its complete lower black trim. The bumper
    # repeats that same atlas below the shared corner; leaving its UV unchanged
    # would draw a second lower band beneath the newly registered lens.
    bumper = originals[26]
    charts = []
    for corner, lamp in ((18, 44), (30, 46)):
        delta = bottom_rows[lamp] / 256 - 1 - bumper.vertices[corner][4]
        for face in bumper.faces:
            indices = struct.unpack_from('<3H', face, 2)
            if corner in indices:
                charts.append(([bumper.vertices[i][3:5] for i in indices], indices.index(corner), delta))
    # Only levels0/1 retain the intact front lens. The broken-lens variants
    # and their bumper charts retain the original damage artwork.
    for index in (26, 27):
        for vertex, p in enumerate(meshes[index].vertices):
            for triangle, corner, delta in charts:
                weights = barycentric(p[3:5], triangle)
                if min(weights) >= -1e-7:
                    meshes[index].vertices[vertex] = (*p[:4], p[4] + weights[corner] * delta, *p[5:])
                    break


def seals(mesh, original, texture):
    return panels.window_seals(mesh, original, texture, color=RUBBER)


def grille(mesh):
    """Recess the two original dark grille strips, leaving the Vauxhall badge."""
    source = Mesh(mesh.header, list(mesh.vertices), list(mesh.faces))
    for x0, x1 in ((105.5, 117), (129, 140)):
        outline = [(u / 256, v / 256 - 1) for u, v in ((x0, 52), (x1, 52), (x1, 56), (x0, 56))]
        recess_uv(mesh, outline, .024, textured_walls=True)
        for y in (53, 55):
            a, b = uv_vertex(source, ((x0 + .5) / 256, y / 256 - 1)), uv_vertex(source, ((x1 - .5) / 256, y / 256 - 1))
            cylinder(mesh, sub(a[:3], scale(unit(a[5:]), .019)), sub(b[:3], scale(unit(b[5:]), .019)), .0018, RECESS, segments=6)


def detail_optics(meshes, directory):
    metadata = json.loads((Path(directory) / 'metadata.json').read_text(encoding='utf-8'))
    for index, regions in metadata.get('optics_recess_uvs', {}).items():
        if index not in tuple(str(i) for i in LAMP_SLOTS) or not isinstance(regions, list):
            raise ValueError('Vectra optics must address existing intact lamp charts')
        for region in regions:
            outline, depth = region.get('outline'), region.get('depth')
            if (not isinstance(outline, list) or not 3 <= len(outline) <= 64 or
                    type(depth) not in (int, float) or not math.isfinite(depth) or not 0 < depth <= .018 or
                    any(not isinstance(p, list) or len(p) != 2 or
                        any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in p) for p in outline)):
                raise ValueError('Vectra reflector contours/depth must remain bounded')
            turns = [((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]))
                     for a, b, c in zip(outline, outline[1:] + outline[:1], outline[2:] + outline[:2])]
            if min(turns) < -1e-12 or max(turns) <= 1e-12:
                raise ValueError('Vectra reflector contours must be convex and counterclockwise')
            target = [(u, v - 1) for u, v in outline]
            for point in target:
                uv_vertex(meshes[int(index)], point)
            recess_uv(meshes[int(index)], target, depth, textured_walls=True)


def remove_quantized_slivers(meshes, selected):
    for index in selected:
        mesh = meshes[index]
        faces, marks, lower = [], set(), set()
        old_marks = getattr(mesh, 'wheel_arch_faces', ())
        old_lower = getattr(mesh, 'lower_arch_faces', ())
        for face_index, face in enumerate(mesh.faces):
            a, b, c = [tuple(round(v * 65536) / 65536 for v in mesh.vertices[i][:3])
                       for i in struct.unpack_from('<3H', face, 2)]
            normal = cross(sub(b, a), sub(c, a))
            if dot(normal, normal) < 1e-16:
                continue
            if face_index in old_marks:
                marks.add(len(faces))
            if face_index in old_lower:
                lower.add(len(faces))
            faces.append(face)
        mesh.faces = faces
        if hasattr(mesh, 'wheel_arch_faces'):
            mesh.wheel_arch_faces = marks
        if hasattr(mesh, 'lower_arch_faces'):
            mesh.lower_arch_faces = lower


def relieve_wheel_arches(meshes, nodes, definition):
    """Relieve V10's folded opening locally without moving its UV charts.

    The source arch is narrower than the swept tire at full steering. Move
    its edge radially in the side plane, keeping the lateral paint position,
    then fade the displacement over the adjacent panel. Shared physical
    points receive the same displacement on every damage mesh and UV seam.
    """
    clearance = definition.get('arch_tire_clearance')
    if clearance is None:
        return
    if (definition['source_model_crc32'] != 'e0ce56fe' or
            type(clearance) not in (int, float) or not .015 <= clearance <= .030):
        raise ValueError('Vectra2 arch relief requires its canonical source and 15..30mm clearance')
    import vehicle_wheel_wells as wells
    _, _, centers = wells.anchors(nodes)
    contours = [wells.aperture(meshes, nodes, center, definition) for center in centers]
    profile = definition['wheel_profile']
    # A sphere bounds the entire spun front tire at every steering angle.
    # Rear wheels do not steer and retain their actual shoulder profile.
    sphere = max(math.hypot(z, r) for z, r in profile)

    def tire_radius(depth, front):
        if front:
            return math.sqrt(max(0., sphere * sphere - depth * depth)) if abs(depth) < sphere else 0.
        for (za, ra), (zb, rb) in zip(profile, profile[1:]):
            if za <= depth <= zb:
                return ra + (rb - ra) * (depth - za) / (zb - za)
        return 0.

    def edge(contour, center, dx, dy):
        hits = []
        for a, b in zip(contour, contour[1:]):
            ex, ey = b[0] - a[0], b[1] - a[1]
            denominator = dx * ey - dy * ex
            if abs(denominator) < 1e-12:
                continue
            ax, ay = a[0] - center[0], a[1] - center[1]
            radius = (ax * ey - ay * ex) / denominator
            t = (ax * dy - ay * dx) / denominator
            if radius > 0 and -1e-7 <= t <= 1 + 1e-7:
                hits.append((radius, a[2] + t * (b[2] - a[2])))
        return min(hits) if hits else None

    cache = {}
    def relief(position):
        if position in cache:
            return cache[position]
        x, y, z = position
        wheel = min(range(4), key=lambda i: abs(x - centers[i][0]) + abs(z - centers[i][2]))
        center, contour = centers[wheel], contours[wheel]
        dx, dy = x - center[0], y - center[1]
        distance = math.hypot(dx, dy)
        if abs(z) < .55 or not .18 < distance < .75:
            return position
        dx, dy = dx / distance, dy / distance
        lip = edge(contour, center, dx, dy)
        if lip is None:
            return position
        radius, lip_z = lip
        side = 1 if center[2] > 0 else -1
        # The return sits below the paint. Include the entire straight flare
        # to the barrel so safe endpoints cannot hide an intersecting triangle.
        depth = side * (lip_z - center[2]) - .004
        required = radius
        for step in range(40):
            t = step / 40
            target = tire_radius(depth + t * (wells.THROAT_DEPTH - depth), wheel < 2) + clearance
            required = max(required, (target - t * (.35 + wells.RADIAL_CLEARANCE)) / (1 - t))
        delta = max(0., required - radius)
        # A broad smooth fade keeps the mapping monotonic even on the tight
        # upper source opening, while changing no vertices beyond the fender.
        weight = max(0., 1 - abs(distance - radius) / .20)
        weight = weight * weight * (3 - 2 * weight)
        result = (x + dx * delta * weight, y + dy * delta * weight, z)
        cache[position] = result
        return result

    for index in definition['arch_meshes']:
        mesh = meshes[index]
        vertices = []
        for p in mesh.vertices:
            position = relief(p[:3])
            normal = p[5:]
            if position != p[:3]:
                # Carry the source smooth normal through the same deformation.
                # Cofactors implement inverse-transpose without an external
                # matrix dependency, including the varying lateral shoulder.
                columns, epsilon = [], .0001
                for axis in range(3):
                    low, high = list(p[:3]), list(p[:3])
                    low[axis] -= epsilon
                    high[axis] += epsilon
                    columns.append(scale(sub(relief(tuple(high)), relief(tuple(low))), .5 / epsilon))
                a, b, c = columns
                normal = unit(add(add(scale(cross(b, c), normal[0]), scale(cross(c, a), normal[1])),
                                  scale(cross(a, b), normal[2])))
            vertices.append((*position, *p[3:5], *normal))
        mesh.vertices = vertices
        for face_index, original in enumerate(mesh.faces):
            indices = struct.unpack_from('<3H', original, 2)
            points = [mesh.vertices[i] for i in indices]
            a, b, c = [tuple(round(value * 65536) / 65536 for value in p[:3]) for p in points]
            facing = cross(sub(b, a), sub(c, a))
            if dot(facing, add(add(points[0][5:], points[1][5:]), points[2][5:])) < 0:
                # The atlas clips include very narrow triangles. As with
                # Mesh.triangle, select their actual stored outward winding
                # after deformation rather than carrying the old winding.
                face = bytearray(original)
                struct.pack_into('<3H', face, 2, indices[0], indices[2], indices[1])
                mesh.faces[face_index] = bytes(face)


def prepare(source, definition):
    sources = {'943e9422': 7, 'e0ce56fe': 10}
    expected = definition['source_model_crc32']
    if expected not in sources or crc32(source) != expected:
        member = sources.get(expected, '7 or10')
        raise ValueError(f'Vectra geometry requires canonical HIGH member{member}')
    header, nodes, originals, textures = decode(source)
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    # The wing's duplicate zero-thickness plates are replaced separately.
    for index in (20, 21):
        selected = set(wing_faces(originals[index]))
        meshes[index].faces = [face for face in meshes[index].faces if face not in selected]
    shared = panels.shared_edge_controls(meshes, set(definition['curved_meshes']), paint=PAINT, excluded_meshes=(49,))
    for index in definition['curved_meshes']:
        meshes[index] = curved_mesh(meshes[index], definition['body_subdivisions'], definition['body_max_curve'], shared)
    meshes[49] = wheel_mesh(originals[49].header, definition)
    for index in (8, 9, 10, 11, 12, 13, 15, 16):
        panels.conform_numeric_decals(meshes[index], paint=PAINT, decal_slots=(6,))
    smooth_wheel_arches(meshes, definition)
    for index in (22, 24):
        meshes[index] = mirror(originals[index])
    for index in LAMP_SLOTS:
        meshes[index] = subdivide_lens(originals[index])
    for index in (20, 21):
        solid_wing(meshes[index], originals[index])
    grille(meshes[14])
    for index in (8, 9, 10, 11, 12, 13):
        seals(meshes[index], originals[index], 4)
        side = 1 if index < 11 else -1
        for x in (-.22, -1.02):
            p, n = panels.surface(originals[index], (x, .12, side), 2, side)
            if abs(p[2]) > .72:
                p = add(p, scale(n, .004))
                cylinder(meshes[index], add(p, (-.042, 0, 0)), add(p, (.042, 0, 0)), .005, RUBBER, segments=8)
    for index in (34, 35, 36, 37, 38, 39):
        seals(meshes[index], originals[index], 3)
    for index in (34, 35, 36):
        for z in (-.30, .30):
            points = []
            for step in range(9):
                t = step / 8
                p, n = panels.surface(originals[index], (1.03 - .54 * t, .3, z - .13 * t), 1, 1)
                points.append(add(p, scale(n, .006)))
            for a, b in zip(points, points[1:]):
                cylinder(meshes[index], a, b, .0032, RUBBER, segments=6)
    for index in (15, 16):
        for z in (-.55, .55):
            p, n = panels.surface(originals[index], (1.88, 1, z), 1, 1)
            cylinder(meshes[index], p, add(p, scale(n, .004)), .007, SILVER, segments=10)
    relieve_wheel_arches(meshes, nodes, definition)
    remove_quantized_slivers(meshes, definition['curved_meshes'])
    return header, nodes, meshes, textures


def generate(source, definition, texture_directory=None, *, quality=70):
    header, nodes, meshes, textures = prepare(source, definition)
    if texture_directory is not None:
        import vectra_textures
        count = struct.unpack_from('<H', header, 16)[0]
        vectra_textures.fit_uvs(meshes, textures, count, texture_directory)
        if definition['source_model_crc32'] == 'e0ce56fe':
            register_front_lamp_frames(meshes, decode(source)[2])
        register_lower_arch_paint(meshes, definition, texture_directory)
        detail_optics(meshes, texture_directory)
        remove_quantized_slivers(meshes, LAMP_SLOTS)
        textures = vectra_textures.replace_textures(textures, count, texture_directory,
                                                   wheel_arch_opaque_uvs(meshes) + lamps_opaque_uvs(meshes) + deck_seam_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, decode(source)[2]),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'wheel_triangles': len(meshes[49].faces), 'wheel_vertices': len(meshes[49].vertices),
                    'curved_meshes': len(definition['curved_meshes'])}
