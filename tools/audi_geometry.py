"""Audi A4 exterior measured from canonical ALLCARS HIGH member 4.

+X is forward, +Y up, Z the axle. Existing mesh slots retain their original
hierarchy, damage bindings and separate material islands.
"""

from functools import partial
import json
import math
from pathlib import Path
import struct

from vehicle_model import (Mesh, add, sub, scale, dot, cross, unit, interpolate,
                           polygon, cylinder, decode, pack, crc32, visible_triangles)
import vehicle_geometry as panels
from vehicle_geometry import edge_key, edge_controls, surface, source_boundary

PAINT = 0
RUBBER = (0, 0, 8, 255)  # Canonical Audi tire color.
SILVER = (176, 184, 196, 255)  # Canonical Audi mirror casing.
GLASS = (120, 197, 238, 255)
RIM = (190, 196, 205, 255)
RECESS = (45, 49, 57, 255)

shared_edge_controls = partial(panels.shared_edge_controls, paint=PAINT, excluded_meshes=(49,))
curved_mesh = partial(panels.curved_mesh, paint=PAINT)
conform_numeric_decals = partial(panels.conform_numeric_decals, paint=PAINT, decal_slots=(7, 8))
uv_vertex = partial(panels.uv_vertex, paint=PAINT)
recess_uv = partial(panels.recess_uv, paint=PAINT, wall_color=RUBBER)


def arch_coordinates(vertex, side):
    """Register both rotated side charts to the measured negative-Z mask."""
    u, v = vertex[3] * 256, (vertex[4] + 1) * 256
    return (255.57 - u, 255.63 - v) if side > 0 else (u, v)


def smooth_wheel_arches(meshes, definition):
    return panels.smooth_wheel_arches(meshes, definition, paint=PAINT,
                                    atlas_coordinates=arch_coordinates, minimum_side=.55)


def wheel_mesh(header, definition):
    """Rounded original-size tire and one physical sixteen-spoke silver rim."""
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
    for side in (-1, 1):
        # A plain recessed surface avoids duplicating the source's painted spokes.
        center = mesh.vertex((0, 0, side * .098), (0, 0, side))
        disk, lip = [], []
        for i in range(segments):
            c, s = math.cos(math.tau * i / segments), math.sin(math.tau * i / segments)
            disk.append(mesh.vertex((.241 * c, .241 * s, side * .098), (0, 0, side)))
            lip.append(mesh.vertex((.258 * c, .258 * s, side * .125), (0, 0, side)))
        for i in range(segments):
            j = (i + 1) % segments
            mesh.triangle((center, disk[i], disk[j]), RECESS)
            mesh.quad((disk[i], disk[j], lip[j], lip[i]), RIM)
        for i in range(definition['wheel_spokes']):
            angle = math.tau * i / definition['wheel_spokes']
            radial, tangent = (math.cos(angle), math.sin(angle)), (-math.sin(angle), math.cos(angle))
            outline = [(radial[0] * r + tangent[0] * width, radial[1] * r + tangent[1] * width, side * .103)
                       for r, width in ((.055, -.009), (.242, -.008), (.242, .008), (.055, .009))]
            top = [(p[0], p[1], side * (.132 if j in (0, 3) else .122)) for j, p in enumerate(outline)]
            polygon(mesh, top, RIM, (0, 0, side))
            polygon(mesh, list(reversed(outline)), SILVER, (0, 0, -side))
            for j, p in enumerate(outline):
                k = (j + 1) % 4
                polygon(mesh, (p, outline[k], top[k], top[j]), SILVER)
        cylinder(mesh, (0, 0, side * .101), (0, 0, side * .139), .055, SILVER, segments=16)
        # Five recessed lug heads belong to this rim, and rotate with mesh49.
        for i in range(5):
            angle = math.tau * i / 5
            point = (.038 * math.cos(angle), .038 * math.sin(angle), side * .140)
            cylinder(mesh, point, add(point, (0, 0, side * .002)), .005, RECESS, segments=6)
    return mesh


def mirror(original):
    """Round and close the original silver Audi shell around its blue glass."""
    mesh = Mesh(original.header, [], [])
    glass = Mesh(original.header, list(original.vertices),
                 [f for f in original.faces if tuple(f[10:14]) == GLASS])
    edges = source_boundary(glass)
    if len(edges) != 4:
        raise ValueError('Audi mirror must retain its four original glass sides')
    outline = [edges[0][0][:3], edges[0][1][:3]]
    while len(outline) < 4:
        next_edges = [(a, b) for a, b in edges if a[:3] == outline[-1] and b[:3] not in outline]
        if not next_edges:
            next_edges = [(b, a) for a, b in edges if b[:3] == outline[-1] and a[:3] not in outline]
        if len(next_edges) != 1:
            raise ValueError('Audi mirror glass boundary must form one loop')
        outline.append(next_edges[0][1][:3])
    # Orient the loop so its visible glass faces rearwards (-X).
    if cross(sub(outline[1], outline[0]), sub(outline[2], outline[0]))[0] > 0:
        outline.reverse()
    perimeter = []
    for i, b in enumerate(outline):
        a, c = outline[i - 1], outline[(i + 1) % 4]
        start, end = interpolate(a, b, .86), interpolate(b, c, .14)
        for j in range(5):
            t = j / 4
            perimeter.append(add(add(scale(start, (1 - t) ** 2), scale(b, 2 * t * (1 - t))), scale(end, t * t)))
    center = tuple(sum(p[k] for p in perimeter) / len(perimeter) for k in range(3))
    front_x = max(p[0] for p in original.vertices) - .045
    front = [(front_x, center[1] + (p[1] - center[1]) * .78, center[2] + (p[2] - center[2]) * .84) for p in perimeter]
    inner = [(p[0] + .004, center[1] + (p[1] - center[1]) * .86, center[2] + (p[2] - center[2]) * .86) for p in perimeter]
    front_center = (front_x + .004, center[1], center[2])
    glass_center = add(center, (.004, 0, 0))
    for i, p in enumerate(perimeter):
        j = (i + 1) % len(perimeter)
        polygon(mesh, (p, front[i], front[j], perimeter[j]), SILVER)
        polygon(mesh, (p, perimeter[j], inner[j], inner[i]), RECESS)
        polygon(mesh, (glass_center, inner[i], inner[j]), GLASS)
        polygon(mesh, (front_center, front[j], front[i]), SILVER)
    # The source's forward-most corner is the mounting attachment, not glass.
    tip = max(original.vertices, key=lambda v: v[0])[:3]
    mount = add(front_center, (-.01, -.03, 0))
    cylinder(mesh, mount, tip, .018, SILVER, segments=8)
    return mesh


def subdivide_lens(original, steps=6, depth=.0018):
    """Curve the original glass only, including each independent rear UV chart."""
    mesh = Mesh(original.header, [], [])
    for face in original.faces:
        vertices = [original.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        grid = {}
        for i in range(steps + 1):
            for j in range(steps + 1 - i):
                weights = (i / steps, j / steps, 1 - (i + j) / steps)
                point = tuple(sum(weights[a] * vertices[a][k] for a in range(3)) for k in range(8))
                normal = unit(point[5:])
                position = add(point[:3], scale(normal, depth * 27 * math.prod(weights)))
                grid[i, j] = mesh.vertex(position, normal, point[3:5])
        for i in range(steps):
            for j in range(steps - i):
                mesh.triangle((grid[i, j], grid[i + 1, j], grid[i, j + 1]), (255, 255, 255, 255), PAINT)
                if i + j < steps - 1:
                    mesh.triangle((grid[i + 1, j], grid[i + 1, j + 1], grid[i, j + 1]), (255, 255, 255, 255), PAINT)
    # The 3mm glass edge follows the source folds, with the actual lens colors.
    for a, b in source_boundary(original):
        normal = unit(add(a[5:], b[5:]))
        polygon(mesh, (a[:3], b[:3], sub(b[:3], scale(normal, .003)), sub(a[:3], scale(normal, .003))),
                (255, 255, 255, 255), uvs=(a[3:5], b[3:5], b[3:5], a[3:5]), texture=PAINT)
    return mesh


def solid_wing(mesh, source, subdivisions):
    """Give the source's double-sided Audi wing and end plates real thickness."""
    # Source faces0..15 are exclusively wing top/end plates, in opposite pairs.
    mesh.faces = mesh.faces[16 * subdivisions * subdivisions:]
    unique, normals, edges = {}, {}, {}
    for face in source.faces[:16]:
        vertices = [source.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        key = tuple(sorted(p[:3] for p in vertices))
        if key in unique:
            continue
        normal = unit(cross(sub(vertices[1][:3], vertices[0][:3]), sub(vertices[2][:3], vertices[0][:3])))
        # The first source end-plate face is the inward member of its
        # double-sided pair; give the solid shell a consistent outer skin.
        if abs(normal[2]) > .5 and normal[2] * sum(p[2] for p in vertices) < 0:
            vertices.reverse()
            normal = scale(normal, -1)
        unique[key] = vertices, normal
        for p in vertices:
            normals[p[:3]] = add(normals.get(p[:3], (0, 0, 0)), normal)
        for a, b in zip(vertices, vertices[1:] + vertices[:1]):
            edges.setdefault(edge_key(a, b), []).append((a, b))
    back = {p: sub(p, scale(unit(n), .004)) for p, n in normals.items()}
    for vertices, normal in unique.values():
        polygon(mesh, [p[:3] for p in vertices], (255, 255, 255, 255), normal,
                [p[3:5] for p in vertices], PAINT)
        polygon(mesh, [back[p[:3]] for p in reversed(vertices)], (255, 255, 255, 255), scale(normal, -1),
                [p[3:5] for p in reversed(vertices)], PAINT)
    for uses in edges.values():
        if len(uses) == 1:
            a, b = uses[0]
            polygon(mesh, (a[:3], back[a[:3]], back[b[:3]], b[:3]), (255, 255, 255, 255),
                    uvs=(a[3:5], a[3:5], b[3:5], b[3:5]), texture=PAINT)


def grille(mesh):
    """Recess just the black source grille chart; leave rings and paint in place."""
    source = Mesh(mesh.header, list(mesh.vertices), list(mesh.faces))
    outline = [(u / 256, v / 256 - 1) for u, v in ((197, 114), (224, 114), (224, 120), (197, 120))]
    recess_uv(mesh, outline, .026, textured_walls=True)
    for v in (115, 117, 119):
        a, b = uv_vertex(source, (198 / 256, v / 256 - 1)), uv_vertex(source, (223 / 256, v / 256 - 1))
        start, end = sub(a[:3], scale(unit(a[5:]), .021)), sub(b[:3], scale(unit(b[5:]), .021))
        cylinder(mesh, start, end, .002, RECESS, segments=6)


def seals(mesh, original, textures):
    for a, b in source_boundary(original, textures):
        if math.dist(a[:3], b[:3]) < .02:
            continue
        normal = unit(add(a[5:], b[5:]))
        cylinder(mesh, add(a[:3], scale(normal, .001)), add(b[:3], scale(normal, .001)), .0026, RUBBER, segments=6)


def detail_body(meshes, originals, definition):
    for mesh in meshes:
        conform_numeric_decals(mesh)
    for index in (12, 13):
        grille(meshes[index])
    smooth_wheel_arches(meshes, definition)
    for index in (40, 42):
        meshes[index] = mirror(originals[index])
    for index in (15, 17, 44, 46):
        meshes[index] = subdivide_lens(originals[index])
    for index in (26, 27, 28, 29, 30, 31):
        seals(meshes[index], originals[index], {1})
        side = -1 if index < 29 else 1
        for x in (-.22, -1.05):
            p, n = surface(originals[index], (x, .155, side), 2, side)
            if abs(p[2]) > .7:
                p = add(p, scale(n, .004))
                cylinder(meshes[index], add(p, (-.044, 0, 0)), add(p, (.044, 0, 0)), .005, RECESS, segments=8)
    for index in (32, 33, 34, 37, 38, 39):
        seals(meshes[index], originals[index], {2})
    for index in (32, 33, 34):
        for z in (-.27, .27):
            a, na = surface(originals[index], (1.03, .33, z), 1, 1)
            b, nb = surface(originals[index], (.51, .57, z - .11), 1, 1)
            cylinder(meshes[index], add(a, scale(na, .004)), add(b, scale(nb, .004)), .0035, RUBBER, segments=6)
    for index in (12, 13):
        for z in (-.53, .53):
            p, n = surface(originals[index], (1.94, 1., z), 1, 1)
            cylinder(meshes[index], p, add(p, scale(n, .004)), .008, SILVER, segments=12)
    for index in (23, 24):
        solid_wing(meshes[index], originals[index], definition['body_subdivisions'])


def wheel_arch_opaque_uvs(meshes):
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', mesh.faces[f], 2))
            for mesh in meshes for f in sorted(getattr(mesh, 'wheel_arch_faces', ()))
            if struct.unpack_from('<h', mesh.faces[f], 8)[0] == PAINT]



def bonnet_opaque_uvs(meshes):
    """Release canonical key remnants only on the fitted right paint edge.

    The source/redraw registration shifts the silver bonnet rim into this
    20px-wide strip on the1254px artwork. Clip actual painted triangles to that
    strip so grille, lamps, unrelated paint and empty canvas keep their masks.
    """
    result = []
    for index in (12, 13):
        mesh = meshes[index]
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != PAINT:
                continue
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            for axis, bound, sign in ((3, 1220 / 1254, 1), (3, 1240 / 1254, -1),
                                      (4, 180 / 1254 - 1, 1), (4, 550 / 1254 - 1, -1)):
                vertices = panels.clip(vertices, axis, bound, sign)
            for i in range(1, len(vertices) - 1):
                triangle = tuple((p[3], p[4] + 1) for p in (vertices[0], vertices[i], vertices[i + 1]))
                a, b, c = triangle
                if abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) > 1e-12:
                    result.append(triangle)
    return result


def lamps_opaque_uvs(meshes):
    return [tuple((meshes[index].vertices[i][3], meshes[index].vertices[i][4] + 1) for i in struct.unpack_from('<3H', face, 2))
            for index in (15, 17, 44, 46) for face in meshes[index].faces
            if struct.unpack_from('<h', face, 8)[0] == PAINT] + [
                tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', mesh.faces[f], 2))
                for index, mesh in enumerate(meshes) if index in (0, 1, 2, 4, 5, 6)
                for f in sorted(getattr(mesh, 'registered_lamp_faces', ()))]




def _emit_uv_polygon(mesh, vertices, template, uv_mapping=None):
    """Split a source face while retaining its interpolated positions/normals."""
    if len(vertices) < 3:
        return
    indices = []
    for vertex in vertices:
        uv = uv_mapping(vertex[3:5]) if uv_mapping else vertex[3:5]
        indices.append(len(mesh.vertices))
        mesh.vertices.append((*vertex[:3], *uv, *vertex[5:]))
    for i in range(1, len(indices) - 1):
        a, b, c = [mesh.vertices[j] for j in (indices[0], indices[i], indices[i + 1])]
        normal = cross(sub(b[:3], a[:3]), sub(c[:3], a[:3]))
        if dot(normal, normal) < 1e-16:
            continue
        face = bytearray(template)
        struct.pack_into('<3H', face, 2, indices[0], indices[i], indices[i + 1])
        mesh.faces.append(bytes(face))


def register_paint_motifs(meshes, regions):
    """Reflect local U inside three source-supported silver side-paint motifs."""
    allowed = {((742, 116, 887, 164), (26, 27, 28)),
               ((22, 59, 130, 101), (19, 20, 21, 26, 27, 28))}
    affected = set()
    for region in regions:
        bounds, selected, quad = region.get('bounds'), region.get('meshes'), region.get('quad')
        if quad is not None:
            if quad != [[984, 142], [1148, 109], [1148, 138], [984, 171]] or selected != [4, 5, 6]:
                raise ValueError('Audi slanted paint registration must follow its measured silver DUNLOP island')
            outline = [(u / 1254, v / 1254 - 1) for u, v in quad]
            slope = (quad[1][1] - quad[0][1]) / (quad[1][0] - quad[0][0])
            center_sum = (quad[0][0] + quad[1][0]) / 1254
            mirror = lambda uv: (center_sum - uv[0], uv[1] + slope * (center_sum - 2 * uv[0]))
        else:
            if not isinstance(bounds, list) or not isinstance(selected, list) or (tuple(bounds), tuple(selected)) not in allowed:
                raise ValueError('Audi paint registration must use its measured motif regions')
            x0, y0, x1, y1 = bounds
            outline = [(u / 1254, v / 1254 - 1) for u, v in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
            mirror = lambda uv: ((x0 + x1) / 1254 - uv[0], uv[1])
        for index in selected:
            affected.add(index)
            mesh = meshes[index]
            old_faces = list(mesh.faces)
            old_marks = getattr(mesh, 'wheel_arch_faces', ())
            mesh.faces, marks = [], set()
            for old_index, face in enumerate(old_faces):
                start = len(mesh.faces)
                vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                inside, outside = vertices, []
                if struct.unpack_from('<h', face, 8)[0] == PAINT:
                    for a, b in zip(outline, outline[1:] + outline[:1]):
                        outside.append(_clip_uv_line(inside, a, b, -1))
                        inside = _clip_uv_line(inside, a, b, 1)
                else:
                    inside = []
                uv_area = sum(a[3] * b[4] - b[3] * a[4] for a, b in zip(inside, inside[1:] + inside[:1])) if len(inside) >= 3 else 0
                if abs(uv_area) < 1e-12:
                    mesh.faces.append(face)
                else:
                    for piece in outside:
                        _emit_uv_polygon(mesh, piece, face)
                    _emit_uv_polygon(mesh, inside, face, mirror)
                if old_index in old_marks:
                    marks.update(range(start, len(mesh.faces)))
            if hasattr(mesh, 'wheel_arch_faces'):
                mesh.wheel_arch_faces = marks
    remove_quantized_slivers(meshes, affected)


def _clip_uv_line(vertices, a, b, sign):
    def distance(p):
        return sign * ((b[0] - a[0]) * (p[4] - a[1]) - (b[1] - a[1]) * (p[3] - a[0]))
    if not vertices:
        return []
    result = []
    previous, dp = vertices[-1], distance(vertices[-1])
    for current in vertices:
        dc = distance(current)
        if (dp >= 0) != (dc >= 0):
            result.append(interpolate(previous, current, dp / (dp - dc)))
        if dc >= 0:
            result.append(current)
        previous, dp = current, dc
    return result


def fit_lamp_uvs(meshes, originals):
    """Bind existing Audi glass charts to the measured1254px artwork.

    The side fender's small lamp triangle shares its front-atlas edge with the
    main glass instead of sampling a second whole headlight in the side chart.
    Rear side charts retain their amber motifs, with their band rows registered
    to the rear-facing white strip. Positions/normals and broken slots stay put.
    """
    def chart(source, indices, targets, triangles, rows=None):
        return ([source.vertices[i][3:5] for i in indices],
                [(u / 1254, v / 1254 - 1) for u, v in targets], triangles, rows)

    def original_pixels(index, indices):
        return [(originals[index].vertices[i][3] * 1254, (originals[index].vertices[i][4] + 1) * 1254) for i in indices]

    positive_full = original_pixels(44, (0, 1, 2, 3))
    negative_full = original_pixels(46, (0, 1, 2, 3))
    front_positive, front_negative = list(positive_full), list(negative_full)
    # The complete outline comprises the main quad plus the small source side
    # triangle. Reserve that triangle's share of the full framed photograph.
    for lamp, fender, inner, target, full in ((44, 0, 2, front_positive, positive_full), (46, 4, 1, front_negative, negative_full)):
        part = abs(originals[lamp].vertices[0][2]) - abs(originals[lamp].vertices[inner][2])
        whole = abs(originals[fender].vertices[10][2]) - abs(originals[lamp].vertices[inner][2])
        target[0] = interpolate(full[inner], full[0], part / whole)
    registrations = {
        44: [chart(originals[44], (0, 1, 2, 3), front_positive, ((2, 1, 0), (3, 0, 1)))],
        46: [chart(originals[46], (0, 1, 2, 3), front_negative, ((2, 1, 0), (0, 3, 2)))],
        15: [chart(originals[15], (0, 1, 2, 3), original_pixels(15, (0, 1, 2, 3)), ((2, 1, 0), (3, 2, 0))),
             chart(originals[15], (6, 7, 5, 4), original_pixels(15, (6, 7, 5, 4)),
                   ((0, 2, 3), (2, 0, 1)))],
        17: [chart(originals[17], (0, 1, 2, 3), original_pixels(17, (0, 1, 2, 3)), ((2, 1, 0), (1, 3, 0))),
             chart(originals[17], (5, 7, 6, 4), original_pixels(17, (5, 7, 6, 4)),
                   ((2, 0, 3), (0, 2, 1)))],
    }
    positive_wrap = chart(originals[0], (9, 10, 11), [positive_full[3], positive_full[0], front_positive[0]], ((0, 1, 2),))
    negative_wrap = chart(originals[4], (10, 9, 11), [negative_full[0], negative_full[3], front_negative[0]], ((0, 1, 2),))

    def barycentric(point, triangle):
        a, b, c = triangle
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-12:
            return None
        wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / den
        wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / den
        return wa, wb, 1 - wa - wb

    def inside(point, source, triangles):
        for indices in triangles:
            weights = barycentric(point, [source[i] for i in indices])
            if weights is not None and min(weights) >= -1e-7:
                return indices, weights
        return None

    def bilinear(points, u, v):
        weights = (1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v
        return tuple(sum(w * p[k] for w, p in zip(weights, points)) for k in (0, 1))

    def map_point(point, registration):
        source, target, triangles, rows = registration
        match = inside(point, source, triangles)
        if match is None:
            raise ValueError('Audi glass UV escaped its original chart')
        indices, weights = match
        if rows is None:
            return tuple(sum(w * target[i][k] for w, i in zip(weights, indices)) for k in (0, 1))
        # Recover the original quad's local axes. Its two painted horizontal
        # separators are then aligned without shifting any physical glass edge.
        u = v = .5
        for _ in range(8):
            current = bilinear(source, u, v)
            du = tuple((1 - v) * (source[1][k] - source[0][k]) + v * (source[2][k] - source[3][k]) for k in (0, 1))
            dv = tuple((1 - u) * (source[3][k] - source[0][k]) + u * (source[2][k] - source[1][k]) for k in (0, 1))
            det = du[0] * dv[1] - du[1] * dv[0]
            residual = point[0] - current[0], point[1] - current[1]
            u += (residual[0] * dv[1] - residual[1] * dv[0]) / det
            v += (du[0] * residual[1] - du[1] * residual[0]) / det
        u, v = max(0., min(1., u)), max(0., min(1., v))
        for a, b in zip(rows, rows[1:]):
            if a[0] - 1e-9 <= v <= b[0] + 1e-9:
                v = a[1] + (v - a[0]) / (b[0] - a[0]) * (b[1] - a[1])
                break
        return bilinear(target, u, v)

    for index, charts in registrations.items():
        mesh = meshes[index]
        original_faces = list(mesh.faces)
        mesh.faces, mesh.registered_lamp_faces = [], set()
        for face in original_faces:
            if struct.unpack_from('<h', face, 8)[0] != PAINT:
                mesh.faces.append(face)
                continue
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            center = tuple(sum(p[k] for p in vertices) / 3 for k in (3, 4))
            registration = next((record for record in charts if inside(center, record[0], record[2]) is not None), None)
            if registration is None:
                if index in (15, 17, 44, 46):
                    raise ValueError('Audi lens face escaped its original source chart')
                mesh.faces.append(face)
                continue
            start = len(mesh.faces)
            source, _, _, rows = registration
            if rows is None:
                _emit_uv_polygon(mesh, vertices, face, lambda uv: map_point(uv, registration))
            else:
                # Put vertices on the two chart separators before their UV
                # remapping, preserving the existing planes and normal field.
                for lower, upper in zip(rows, rows[1:]):
                    piece = vertices
                    for row, center_t in ((lower[0], (lower[0] + upper[0]) / 2), (upper[0], (lower[0] + upper[0]) / 2)):
                        a, b = bilinear(source, 0, row), bilinear(source, 1, row)
                        center_uv = bilinear(source, .5, center_t)
                        side = (b[0] - a[0]) * (center_uv[1] - a[1]) - (b[1] - a[1]) * (center_uv[0] - a[0])
                        piece = _clip_uv_line(piece, a, b, 1 if side > 0 else -1)
                    _emit_uv_polygon(mesh, piece, face, lambda uv: map_point(uv, registration))
            mesh.registered_lamp_faces.update(range(start, len(mesh.faces)))


    for index, registration, bounds in ((0, positive_wrap, (0, 0, 76, 1254)),
                                         (1, positive_wrap, (0, 0, 76, 1254)),
                                         (2, positive_wrap, (0, 0, 76, 1254)),
                                         (4, negative_wrap, (1176, 0, 1254, 1254)),
                                         (5, negative_wrap, (1176, 0, 1254, 1254)),
                                         (6, negative_wrap, (1176, 0, 1254, 1254))):
        mesh = meshes[index]
        old_faces, old_marks = list(mesh.faces), getattr(mesh, 'wheel_arch_faces', ())
        mesh.faces, marks, mesh.registered_lamp_faces = [], set(), set()
        x0, y0, x1, y1 = bounds
        limits = ((3, x0 / 1254, 1), (3, x1 / 1254, -1), (4, y0 / 1254 - 1, 1), (4, y1 / 1254 - 1, -1))
        for old_index, face in enumerate(old_faces):
            start = len(mesh.faces)
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            inner, outside = vertices, []
            if struct.unpack_from('<h', face, 8)[0] == PAINT:
                for axis, bound, sign in limits:
                    outside.append(panels.clip(inner, axis, bound, -sign))
                    inner = panels.clip(inner, axis, bound, sign)
            else:
                inner = []
            area = sum(a[3] * b[4] - b[3] * a[4] for a, b in zip(inner, inner[1:] + inner[:1])) if len(inner) >= 3 else 0
            if abs(area) < 1e-12:
                mesh.faces.append(face)
            else:
                for piece in outside:
                    _emit_uv_polygon(mesh, piece, face)
                first = len(mesh.faces)
                center = tuple(sum(v[k] for v in vertices) / 3 for k in (3, 4))
                if inside(center, registration[0], registration[2]) is not None:
                    _emit_uv_polygon(mesh, inner, face, lambda uv: map_point(uv, registration))
                else:
                    # The rest of the AI ellipse is on painted source faces7/8,
                    # not glass. Sample the same Audi's nearby clean silver band.
                    def silver(uv):
                        x, y = uv[0] * 1254, (uv[1] + 1) * 1254
                        u = 76 + (76 - x) * .02 if index < 4 else 1176 - (x - 1176) * .02
                        v = y
                        return u / 1254, v / 1254 - 1
                    _emit_uv_polygon(mesh, inner, face, silver)
                mesh.registered_lamp_faces.update(range(first, len(mesh.faces)))
            if old_index in old_marks:
                marks.update(range(start, len(mesh.faces)))
        if hasattr(mesh, 'wheel_arch_faces'):
            mesh.wheel_arch_faces = marks

    # Preserve the full photographic frame and silver paint border. A UV seam
    # skips only the generated green stripe inside that photograph.
    for index in (0, 1, 2, 4, 5, 6, 44, 46):
        mesh = meshes[index]
        old_faces = list(mesh.faces)
        registered = getattr(mesh, 'registered_lamp_faces', ())
        old_marks = getattr(mesh, 'wheel_arch_faces', ())
        mesh.faces, new_registered, marks = [], set(), set()
        for old_index, face in enumerate(old_faces):
            start = len(mesh.faces)
            if old_index not in registered:
                mesh.faces.append(face)
            else:
                vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                for low, high, shift in ((0, 580, 0), (580, 588, -1), (588, 596, 1), (596, 1254, 0)):
                    part = panels.clip(panels.clip(vertices, 4, low / 1254 - 1, 1), 4, high / 1254 - 1, -1)
                    def skip_green(uv):
                        y = (uv[1] + 1) * 1254
                        target = y if shift == 0 else (580 + (y - 580) / 4 if shift < 0 else 594 + (y - 588) / 4)
                        return uv[0], target / 1254 - 1
                    _emit_uv_polygon(mesh, part, face, skip_green)
                new_registered.update(range(start, len(mesh.faces)))
            if old_index in old_marks:
                marks.update(range(start, len(mesh.faces)))
        mesh.registered_lamp_faces = new_registered
        if hasattr(mesh, 'wheel_arch_faces'):
            mesh.wheel_arch_faces = marks
    # The bumper shares the lamp's lower source edge (atlas Y589), so its
    # small silver top strip must skip the same green bleed. Preserve the
    # Castrol artwork below Y596 and every side/body chart.
    for index in (8, 9, 10):
        mesh = meshes[index]
        old_faces = mesh.faces
        mesh.faces = []
        for face in old_faces:
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            if (struct.unpack_from('<h', face, 8)[0] != PAINT or
                    not all(830 <= p[3] * 1254 <= 1230 and (p[4] + 1) * 1254 >= 588 for p in vertices)):
                mesh.faces.append(face)
                continue
            for low, high in ((588, 596), (596, 1254)):
                piece = panels.clip(panels.clip(vertices, 4, low / 1254 - 1, 1), 4, high / 1254 - 1, -1)
                def bumper_rim(uv):
                    y = (uv[1] + 1) * 1254
                    return uv[0], (594 + (y - 588) / 4 if high == 596 else y) / 1254 - 1
                _emit_uv_polygon(mesh, piece, face, bumper_rim)
    remove_quantized_slivers(meshes, (0, 1, 2, 4, 5, 6, 8, 9, 10, 15, 17, 44, 46))



def detail_optics(meshes, directory):
    """Apply bounded convex reflector regions after the final Audi artwork fit."""
    metadata = json.loads((Path(directory) / 'metadata.json').read_text(encoding='utf-8'))
    for index, regions in metadata.get('optics_recess_uvs', {}).items():
        if index not in ('15', '17', '44', '46') or not isinstance(regions, list):
            raise ValueError('Audi optics must address existing healthy lamp panels')
        for region in regions:
            outline, depth = region.get('outline'), region.get('depth')
            if (not isinstance(outline, list) or not 3 <= len(outline) <= 64 or
                    type(depth) not in (int, float) or not math.isfinite(depth) or not 0 < depth <= .018 or
                    any(not isinstance(p, list) or len(p) != 2 or
                        any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in p) for p in outline)):
                raise ValueError('Audi reflector contours/depth must remain bounded')
            turns = [((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]))
                     for a, b, c in zip(outline, outline[1:] + outline[:1], outline[2:] + outline[:2])]
            if min(turns) < -1e-12 or max(turns) <= 1e-12:
                raise ValueError('Audi reflector contours must be convex and counterclockwise')
            target = [(u, v - 1) for u, v in outline]
            bindings = (44, 0, 1, 2) if index == '44' else (46, 4, 5, 6) if index == '46' else (int(index),)
            for point in target:
                supported = False
                for binding in bindings[:2] if len(bindings) > 1 else bindings:
                    try:
                        uv_vertex(meshes[binding], point)
                        supported = True
                        break
                    except ValueError:
                        pass
                if not supported:
                    raise ValueError('Audi reflector must stay on its complete source glass chart')
            for binding in bindings:
                mesh = meshes[binding]
                if binding in (0, 1, 2, 4, 5, 6):
                    # Retain the exact source-face footprint through cavity
                    # clipping; opaque masks must follow the emitted faces.
                    old_faces = mesh.faces
                    tracked = {name: getattr(mesh, name, set()) for name in ('registered_lamp_faces', 'wheel_arch_faces')}
                    result, marks = [], {name: set() for name in tracked}
                    for face_index, face in enumerate(old_faces):
                        piece = Mesh(mesh.header, mesh.vertices, [face])
                        recess_uv(piece, target, depth, textured_walls=True)
                        for name, prior in tracked.items():
                            if face_index in prior:
                                marks[name].update(range(len(result), len(result) + len(piece.faces)))
                        result.extend(piece.faces)
                    mesh.faces = result
                    for name, faces in marks.items():
                        setattr(mesh, name, faces)
                else:
                    recess_uv(mesh, target, depth, textured_walls=True)



def remove_quantized_slivers(meshes, selected):
    """Drop clipped fragments that collapse in CM3D16.16, retaining face marks."""
    for index in selected:
        mesh = meshes[index]
        faces, marks, lamp_marks = [], set(), set()
        old_marks = getattr(mesh, 'wheel_arch_faces', ())
        old_lamps = getattr(mesh, 'registered_lamp_faces', ())
        for face_index, face in enumerate(mesh.faces):
            a, b, c = [tuple(round(v * 65536) / 65536 for v in mesh.vertices[i][:3])
                       for i in struct.unpack_from('<3H', face, 2)]
            normal = cross(sub(b, a), sub(c, a))
            if dot(normal, normal) < 1e-16:
                continue
            if face_index in old_marks:
                marks.add(len(faces))
            if face_index in old_lamps:
                lamp_marks.add(len(faces))
            faces.append(face)
        mesh.faces = faces
        if hasattr(mesh, 'registered_lamp_faces'):
            mesh.registered_lamp_faces = lamp_marks
        if hasattr(mesh, 'wheel_arch_faces'):
            mesh.wheel_arch_faces = marks


def prepare(source, definition):
    if crc32(source) != definition['source_model_crc32']:
        raise ValueError('Audi geometry requires the canonical HIGH member4')
    header, nodes, originals, textures = decode(source)
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    shared = shared_edge_controls(originals, set(definition['curved_meshes']))
    for index in definition['curved_meshes']:
        meshes[index] = curved_mesh(originals[index], definition['body_subdivisions'], definition['body_max_curve'], shared)
    meshes[49] = wheel_mesh(originals[49].header, definition)
    detail_body(meshes, originals, definition)
    remove_quantized_slivers(meshes, definition['curved_meshes'])
    return header, nodes, meshes, textures


def generate(source, definition, texture_directory=None, *, quality=70):
    header, nodes, meshes, textures = prepare(source, definition)
    if texture_directory is not None:
        import audi_textures
        count = struct.unpack_from('<H', header, 16)[0]
        audi_textures.fit_uvs(meshes, textures, count, texture_directory)
        fit_lamp_uvs(meshes, decode(source)[2])
        detail_optics(meshes, texture_directory)
        remove_quantized_slivers(meshes, (0, 1, 2, 4, 5, 6, 15, 17, 44, 46))
        textures = audi_textures.replace_textures(textures, count, texture_directory,
                                                 wheel_arch_opaque_uvs(meshes) + lamps_opaque_uvs(meshes) + bonnet_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, decode(source)[2]),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'wheel_triangles': len(meshes[49].faces), 'wheel_vertices': len(meshes[49].vertices),
                    'stored_triangles': sum(len(m.faces) for m in meshes), 'curved_meshes': len(definition['curved_meshes'])}
