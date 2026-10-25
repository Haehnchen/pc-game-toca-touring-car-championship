"""Volvo S40 body, lenses and fifteen-spoke wheels from ALLCARS member13.

The S40 has its own fifty-slot topology: bonnet/grille share13, the wing/deck
share24, and rear lenses have independent rear and side UV charts. All details
remain attached to those exact source slots and their original damage levels.
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
RUBBER = (0, 8, 8, 255)
GLASS = (120, 197, 238, 255)
SILVER = (193, 198, 202, 255)
BRIGHT = (224, 230, 233, 255)
RECESS = (43, 46, 48, 255)
LAMP_SLOTS = (16, 18, 45, 47)
uv_vertex = partial(panels.uv_vertex, paint=PAINT)
recess_uv = partial(panels.recess_uv, paint=PAINT, wall_color=RUBBER)
curved_mesh = partial(panels.curved_mesh, paint=PAINT)
remove_quantized_slivers = panels.remove_quantized_slivers


def wheel_mesh(header, definition):
    """Rounded350mm tire and the original atlas's fifteen separate silver spokes."""
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
        center = mesh.vertex((0, 0, side * .092), (0, 0, side))
        disk, lip = [], []
        for i in range(segments):
            c, s = math.cos(math.tau * i / segments), math.sin(math.tau * i / segments)
            disk.append(mesh.vertex((spoke * c, spoke * s, side * .092), (0, 0, side)))
            lip.append(mesh.vertex((radius * c, radius * s, side * .125), (0, 0, side)))
        for i in range(segments):
            j = (i + 1) % segments
            mesh.triangle((center, disk[i], disk[j]), RECESS)
            mesh.quad((disk[i], disk[j], lip[j], lip[i]), SILVER)
        for i in range(definition['wheel_spokes']):
            angle = math.tau * i / definition['wheel_spokes']
            radial, tangent = (math.cos(angle), math.sin(angle)), (-math.sin(angle), math.cos(angle))
            # The source spokes taper at the barrel and widen around the hub.
            outline = [(radial[0] * r + tangent[0] * w, radial[1] * r + tangent[1] * w, side * .099)
                       for r, w in ((hub, -.010), (spoke, -.007), (spoke, .007), (hub, .010))]
            top = [(p[0], p[1], side * (.134 if j in (0, 3) else .124)) for j, p in enumerate(outline)]
            polygon(mesh, top, BRIGHT, (0, 0, side))
            polygon(mesh, list(reversed(outline)), SILVER, (0, 0, -side))
            for j, p in enumerate(outline):
                k = (j + 1) % 4
                polygon(mesh, (p, outline[k], top[k], top[j]), SILVER)
        cylinder(mesh, (0, 0, side * .097), (0, 0, side * .14), hub, SILVER, segments=24)
        for i in range(5):
            angle = math.tau * i / 5
            point = (.034 * math.cos(angle), .034 * math.sin(angle), side * .141)
            cylinder(mesh, point, add(point, (0, 0, side * .002)), .0045, RECESS, segments=6)
    return mesh


def mirror(original):
    """Close the S40's black housing around the measured rear-facing glass."""
    mesh = Mesh(original.header, [], [])
    glass = Mesh(original.header, list(original.vertices), [f for f in original.faces if tuple(f[10:14]) == GLASS])
    edges = panels.source_boundary(glass)
    if len(edges) != 4:
        raise ValueError('Volvo mirror requires its four source glass edges')
    outline = [edges[0][0][:3], edges[0][1][:3]]
    while len(outline) < 4:
        candidates = [(a, b) for a, b in edges if a[:3] == outline[-1] and b[:3] not in outline]
        if not candidates:
            candidates = [(b, a) for a, b in edges if b[:3] == outline[-1] and a[:3] not in outline]
        if len(candidates) != 1:
            raise ValueError('Volvo mirror glass must form one source loop')
        outline.append(candidates[0][1][:3])
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
    front_x = max(p[0] for p in original.vertices) - .070
    front = [(front_x, center[1] + (p[1] - center[1]) * .80, center[2] + (p[2] - center[2]) * .86) for p in perimeter]
    inner = [(p[0] + .003, center[1] + (p[1] - center[1]) * .88, center[2] + (p[2] - center[2]) * .88) for p in perimeter]
    front_center, glass_center = (front_x + .004, center[1], center[2]), add(center, (.003, 0, 0))
    for i, p in enumerate(perimeter):
        j = (i + 1) % len(perimeter)
        polygon(mesh, (p, front[i], front[j], perimeter[j]), RUBBER)
        polygon(mesh, (p, perimeter[j], inner[j], inner[i]), RECESS)
        polygon(mesh, (glass_center, inner[i], inner[j]), GLASS)
        polygon(mesh, (front_center, front[j], front[i]), RUBBER)
    attachment = max(original.vertices, key=lambda v: v[0])[:3]
    cylinder(mesh, front_center, attachment, .014, RUBBER, segments=10)
    return mesh


def wing_faces(source):
    return [face for face in source.faces
            if all(source.vertices[i][1] >= .35 and source.vertices[i][0] < -2.0
                   for i in struct.unpack_from('<3H', face, 2))]


def solid_wing(mesh, source):
    """Four-millimetre shell of the actual S40 top and two sloped endplates."""
    unique, normals, edges = {}, {}, {}
    for face in wing_faces(source):
        vertices = [source.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        key = tuple(sorted(p[:3] for p in vertices))
        if key in unique:
            continue
        normal = unit(cross(sub(vertices[1][:3], vertices[0][:3]), sub(vertices[2][:3], vertices[0][:3])))
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


def subdivide_lens(original, steps=8, depth=.0016):
    """Keep the complete two-chart rear or single-chart front lamp silhouette."""
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
    return u, v if side > 0 else 255.938 - v


def smooth_wheel_arches(meshes, definition):
    panels.smooth_wheel_arches(meshes, definition, paint=PAINT,
                              atlas_coordinates=arch_coordinates, minimum_side=.55)


def wheel_arch_opaque_uvs(meshes):
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1) for i in struct.unpack_from('<3H', mesh.faces[f], 2))
            for mesh in meshes for f in sorted(getattr(mesh, 'wheel_arch_faces', ()))
            if struct.unpack_from('<h', mesh.faces[f], 8)[0] == PAINT]


def lamps_opaque_uvs(meshes):
    return [tuple((meshes[index].vertices[i][3], meshes[index].vertices[i][4] + 1) for i in struct.unpack_from('<3H', face, 2))
            for index in LAMP_SLOTS for face in meshes[index].faces
            if struct.unpack_from('<h', face, 8)[0] == PAINT]


def register_front_lamp_frames(meshes, originals):
    """Repair the verified source white step at the two upper corner folds.

    The front charts start their dark trim below the already dark side return.
    Only the existing front-lens upper corner moves inward in atlasU; every
    physical point and the rounded side motif retain their source positions.
    The negative side lower trim also ends3.5% early in the source: register
    that endpoint on its fender and the same bumper's side continuation.
    """
    def weights(point, triangle):
        a, b, c = triangle
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-12:
            return None
        wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / den
        wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / den
        return wa, wb, 1 - wa - wb

    for lamp, corner in ((45, 1), (47, 0)):
        source = originals[lamp]
        charts = [struct.unpack_from('<3H', f, 2) for f in source.faces]
        delta = 34.7 / 256 - source.vertices[corner][3]
        for index, p in enumerate(meshes[lamp].vertices):
            for chart in charts:
                bary = weights(p[3:5], [source.vertices[i][3:5] for i in chart])
                if bary is not None and min(bary) >= -1e-7:
                    amount = bary[chart.index(corner)] * delta if corner in chart else 0.
                    meshes[lamp].vertices[index] = (*p[:3], p[3] + amount, *p[4:])
                    break
            else:
                raise ValueError('Volvo front lens escaped its source chart')

    for index, corner in ((4, 9), (5, 9), (9, 18), (10, 18)):
        source = originals[index]
        charts = [(struct.unpack_from('<3H', f, 2), f) for f in source.faces
                  if struct.unpack_from('<h', f, 8)[0] == PAINT and corner in struct.unpack_from('<3H', f, 2)]
        delta = 229.85 / 256 - 1 - source.vertices[corner][4]
        for vertex, p in enumerate(meshes[index].vertices):
            for chart, _ in charts:
                bary = weights(p[3:5], [source.vertices[i][3:5] for i in chart])
                if bary is not None and min(bary) >= -1e-7:
                    meshes[index].vertices[vertex] = (*p[:4], p[4] + bary[chart.index(corner)] * delta, *p[5:])
                    break


def register_rear_lamp_returns(meshes):
    """Join the accepted redraw's side and rear lower lamp outlines.

    Source outlines agree. The redraw's side return ends about10mm above its
    rear chart. Re-register only the lower band in the actual existing side
    atlas island, including its neighboring quarter panels. The upper red and
    clear bands, white panel outside this strip, and every position stay fixed.
    """
    for selected, top, bottom, target, sampled in (
            ((16, 30, 31), 48.8046875, 29.640625, .6215, .5915),
            ((18, 27, 28), 207.12890625, 226.296875, .6182, .5859)):
        knots = (0., .4, target, .82, 1.)
        mapped = (0., .4, sampled, .82, 1.)
        levels = sorted((top + t * (bottom - top)) / 256 - 1 for t in knots)
        limits = ((3, 3.6 / 256, 1), (3, 11.5 / 256, -1),
                  (4, levels[0], 1), (4, levels[-1], -1))

        def mapping(p):
            t = ((p[4] + 1) * 256 - top) / (bottom - top)
            result = t
            for a, b, c, d in zip(knots, knots[1:], mapped, mapped[1:]):
                if a - 1e-7 <= t <= b + 1e-7:
                    result = c + (t - a) / (b - a) * (d - c)
                    break
            return (*p[:4], (top + result * (bottom - top)) / 256 - 1, *p[5:])

        for index in selected:
            mesh = meshes[index]
            old_faces, old_marks = mesh.faces, getattr(mesh, 'wheel_arch_faces', ())
            mesh.faces, marks = [], set()
            for face_index, face in enumerate(old_faces):
                first = len(mesh.faces)
                if struct.unpack_from('<h', face, 8)[0] != PAINT:
                    mesh.faces.append(face)
                else:
                    inside = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                    outside = []
                    for axis, bound, sign in limits:
                        outside.append(panels.clip(inside, axis, bound, -sign))
                        inside = panels.clip(inside, axis, bound, sign)
                    if len(inside) < 3:
                        mesh.faces.append(face)
                    else:
                        for part in outside:
                            panels.emit_source_polygon(mesh, part, face)
                        for low, high in zip(levels, levels[1:]):
                            part = panels.clip(panels.clip(inside, 4, low, 1), 4, high, -1)
                            panels.emit_source_polygon(mesh, [mapping(p) for p in part], face)
                if face_index in old_marks:
                    marks.update(range(first, len(mesh.faces)))
            if hasattr(mesh, 'wheel_arch_faces'):
                mesh.wheel_arch_faces = marks


def seals(mesh, original, texture):
    return panels.window_seals(mesh, original, texture, color=RUBBER)


def grille(mesh):
    """Recess the actual narrow front chart without framing nearby bonnet paint."""
    source = Mesh(mesh.header, list(mesh.vertices), list(mesh.faces))
    x0, x1, y0, y1 = 27, 34.0, 87, 123
    outline = [(u / 256, v / 256 - 1) for u, v in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    recess_uv(mesh, outline, .022, textured_walls=True)
    for y in range(89, 123, 3):
        a, b = uv_vertex(source, (28 / 256, y / 256 - 1)), uv_vertex(source, (33.5 / 256, y / 256 - 1))
        cylinder(mesh, sub(a[:3], scale(unit(a[5:]), .014)), sub(b[:3], scale(unit(b[5:]), .014)), .0015, RECESS, segments=6)


def detail_optics(meshes, directory):
    metadata = json.loads((Path(directory) / 'metadata.json').read_text(encoding='utf-8'))
    for index, regions in metadata.get('optics_recess_uvs', {}).items():
        if index not in tuple(str(i) for i in LAMP_SLOTS) or not isinstance(regions, list):
            raise ValueError('Volvo optics must address existing intact lamp charts')
        for region in regions:
            outline, depth = region.get('outline'), region.get('depth')
            if (not isinstance(outline, list) or not 3 <= len(outline) <= 64 or
                    type(depth) not in (int, float) or not math.isfinite(depth) or not 0 < depth <= .018 or
                    any(not isinstance(p, list) or len(p) != 2 or
                        any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in p) for p in outline)):
                raise ValueError('Volvo reflector contours/depth must remain bounded')
            turns = [((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]))
                     for a, b, c in zip(outline, outline[1:] + outline[:1], outline[2:] + outline[:2])]
            if min(turns) < -1e-12 or max(turns) <= 1e-12:
                raise ValueError('Volvo reflector contours must be convex and counterclockwise')
            target = [(u, v - 1) for u, v in outline]
            for point in target:
                uv_vertex(meshes[int(index)], point)
            recess_uv(meshes[int(index)], target, depth, textured_walls=True)


def relieve_wheel_arches(meshes, nodes, definition):
    """Relieve the S40 front folds locally without moving their UV charts.

    The source arch is narrower than the swept tire at full steering. Move
    its edge radially in the side plane, keeping the lateral paint position,
    then fade the displacement over the adjacent panel. Shared physical
    points receive the same displacement on every damage mesh and UV seam.
    """
    clearance = definition.get('arch_tire_clearance')
    if clearance is None:
        return
    if (definition['source_model_crc32'] != 'c577ebac' or
            type(clearance) not in (int, float) or not .015 <= clearance <= .030):
        raise ValueError('Volvo arch relief requires its canonical source and 15..30mm clearance')
    import vehicle_wheel_wells as wells
    _, _, centers = wells.anchors(nodes)
    centers = [(x, y, z + (1 if z > 0 else -1) * definition.get('wheel_outset', [0, 0])[i // 2])
               for i, (x, y, z) in enumerate(centers)]
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
    if definition['source_model_crc32'] != 'c577ebac' or crc32(source) != 'c577ebac':
        raise ValueError('Volvo geometry requires canonical HIGH member13')
    header, nodes, originals, textures = decode(source)
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    for index in (24, 25):
        selected = set(wing_faces(originals[index]))
        meshes[index].faces = [face for face in meshes[index].faces if face not in selected]
    shared = panels.shared_edge_controls(meshes, set(definition['curved_meshes']), paint=PAINT, excluded_meshes=(49,))
    for index in definition['curved_meshes']:
        meshes[index] = curved_mesh(meshes[index], definition['body_subdivisions'], definition['body_max_curve'], shared)
    meshes[49] = wheel_mesh(originals[49].header, definition)
    for index in (13, 14, 27, 28, 29, 30, 31, 32):
        panels.conform_numeric_decals(meshes[index], paint=PAINT, decal_slots=(7, 8))
    smooth_wheel_arches(meshes, definition)
    for index in (41, 43):
        meshes[index] = mirror(originals[index])
    for index in LAMP_SLOTS:
        meshes[index] = subdivide_lens(originals[index])
    for index in (24, 25):
        solid_wing(meshes[index], originals[index])
    for index in (13, 14):
        grille(meshes[index])
    for index in (27, 28, 29, 30, 31, 32):
        seals(meshes[index], originals[index], 3)
        side = -1 if index < 30 else 1
        p, n = panels.surface(originals[index], (-.25, .16, side), 2, side)
        p = add(p, scale(n, .004))
        cylinder(meshes[index], add(p, (-.042, 0, 0)), add(p, (.042, 0, 0)), .006, RUBBER, segments=10)
    for index in (33, 34, 35, 38, 39, 40):
        seals(meshes[index], originals[index], 2)
    for index in (33, 34, 35):
        for z in (-.28, .28):
            points = []
            for step in range(9):
                t = step / 8
                p, n = panels.surface(originals[index], (.91 - .50 * t, .3, z - .13 * t), 1, 1)
                points.append(add(p, scale(n, .006)))
            for a, b in zip(points, points[1:]):
                cylinder(meshes[index], a, b, .0032, RUBBER, segments=6)
    for index in (13, 14):
        for z in (-.57, .57):
            p, n = panels.surface(originals[index], (1.80, 1, z), 1, 1)
            cylinder(meshes[index], p, add(p, scale(n, .004)), .006, SILVER, segments=10)
    relieve_wheel_arches(meshes, nodes, definition)
    remove_quantized_slivers(meshes, range(50))
    return header, nodes, meshes, textures


def generate(source, definition, texture_directory=None, *, quality=70):
    header, nodes, meshes, textures = prepare(source, definition)
    if texture_directory is not None:
        import volvo_textures
        count = struct.unpack_from('<H', header, 16)[0]
        volvo_textures.fit_uvs(meshes, textures, count, texture_directory)
        register_front_lamp_frames(meshes, decode(source)[2])
        register_rear_lamp_returns(meshes)
        detail_optics(meshes, texture_directory)
        remove_quantized_slivers(meshes, (*LAMP_SLOTS, 27, 28, 30, 31))
        textures = volvo_textures.replace_textures(textures, count, texture_directory,
                                                  wheel_arch_opaque_uvs(meshes) + lamps_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, decode(source)[2]),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'wheel_triangles': len(meshes[49].faces), 'wheel_vertices': len(meshes[49].vertices),
                    'curved_meshes': len(definition['curved_meshes'])}
