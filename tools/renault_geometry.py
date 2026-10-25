"""Authored Renault body components in the original CM3D meter coordinates.

Forward is +X, up is +Y and the axle direction is Z. Components stay in their
original damage-node mesh families. This module creates geometry, not textures.
"""

from functools import partial
import math
import struct

import vehicle_geometry as panels
from vehicle_model import (Mesh, add, sub, scale, dot, cross, polygon, box, cylinder,
                           interpolate, crc32, decode, pack, visible_triangles)

mul = scale


BLUE = (8, 63, 173, 255)
CARBON = (16, 20, 27, 255)
RUBBER = (9, 12, 15, 255)
METAL = (130, 144, 157, 255)
GLASS = (90, 124, 148, 255)
# Centers of the painted circular optics in the1254px generated REN atlas.
# Original UVs project these onto each angular panel; fixed world coordinates
# put the old solid-gray inserts above the actual painted reflector centers.
HEADLAMP_UV_CENTERS = {
    1: ((1174 / 1254, 588 / 1254), (1208 / 1254, 585 / 1254)),
    -1: ((887 / 1254, 588 / 1254), (850 / 1254, 585 / 1254)),
}
HEADLAMP_UV_RADIUS = .010
LAMP_SIDE_PAINT_UVS = {
    1: (1180.5 / 1254, 155.5 / 1254 - 1),
    -1: (1180.5 / 1254, 1120.5 / 1254 - 1),
}


def unit(a):
    size = math.sqrt(dot(a, a))
    return mul(a, 1 / size) if size > 1e-12 else (0., 1., 0.)


# Authored Renault normals use a near-zero threshold, and its panel faces
# retain source winding. Supply those policies to the common algorithms.
surface = partial(panels.surface, normalize=unit)
clip = panels.clip
emit_source_polygon = panels.emit_source_polygon
conform_numeric_decals = partial(panels.conform_numeric_decals, paint=0, decal_slots=(7, 8), normalize=unit)
edge_key = panels.edge_key
edge_controls = partial(panels.edge_controls, normalize=unit)
shared_edge_controls = partial(panels.shared_edge_controls, paint=0, excluded_meshes=(49,), normalize=unit)
curved_mesh = partial(panels.curved_mesh, paint=0, normalize=unit, align_normals=False)


def follow_damage(mesh, start, healthy, damaged):
    """Carry new solid details with their original panel's damage deformation.

    UV correspondences identify displaced original corners. Repeated solid-color
    UVs use the nearest physical corner, while removed corners clamp to surviving
    source geometry. Local interpolation avoids attaching intact floating extras.
    """
    if healthy is damaged:
        return
    corners = {}
    for vertex in healthy.vertices:
        if vertex[:3] in corners:
            continue
        matching = [v for v in damaged.vertices if sum((v[k] - vertex[k]) ** 2 for k in (3, 4)) < 1e-7]
        candidates = matching or damaged.vertices
        target = min(candidates, key=lambda v: dot(sub(v[:3], vertex[:3]), sub(v[:3], vertex[:3])))
        if dot(sub(target[:3], vertex[:3]), sub(target[:3], vertex[:3])) > .3 ** 2:
            target = min(damaged.vertices, key=lambda v: dot(sub(v[:3], vertex[:3]), sub(v[:3], vertex[:3])))
        corners[vertex[:3]] = sub(target[:3], vertex[:3])
    for index in range(start, len(mesh.vertices)):
        vertex = mesh.vertices[index]
        nearest = sorted(corners.items(), key=lambda c: dot(sub(vertex[:3], c[0]), sub(vertex[:3], c[0])))[:3]
        weights = [1 / max(1e-8, dot(sub(vertex[:3], point), sub(vertex[:3], point))) ** 2 for point, _ in nearest]
        delta = tuple(sum(w * corner[1][axis] for w, corner in zip(weights, nearest)) / sum(weights) for axis in range(3))
        mesh.vertices[index] = (*add(vertex[:3], delta), *vertex[3:])
    normals = {}
    for face in mesh.faces:
        indices = struct.unpack_from("<3H", face, 2)
        if min(indices) < start:
            continue
        a, b, c = [mesh.vertices[i][:3] for i in indices]
        normal = cross(sub(b, a), sub(c, a))
        for index in indices:
            normals[index] = add(normals.get(index, (0, 0, 0)), normal)
    for index, normal in normals.items():
        mesh.vertices[index] = (*mesh.vertices[index][:5], *unit(normal))


def front_intake(mesh, original, healthy, damage):
    # Cut a genuine opening in the lower painted bumper. Its dark back is55mm
    # behind the source skin; the reveal walls and grille bars have real depth.
    boundaries = ((1, -.478, 1), (1, -.374, -1), (2, -.52, 1), (2, .52, -1))
    source_faces = list(mesh.faces)
    mesh.faces = []
    for face in source_faces:
        vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
        if sum(v[5] for v in vertices) / 3 < .6:
            mesh.faces.append(face)
            continue
        interior = vertices
        for axis, boundary, sign in boundaries:
            outside = clip(interior, axis, boundary, -sign)
            emit_source_polygon(mesh, outside, face)
            interior = clip(interior, axis, boundary, sign)
        if len(interior) < 3:
            continue
        back = [add(v[:3], (-.055, 0, 0)) for v in interior]
        polygon(mesh, back, RUBBER, (1, 0, 0))
        for a, b in zip(interior, interior[1:] + interior[:1]):
            if not any(abs(a[axis] - boundary) < 1e-6 and abs(b[axis] - boundary) < 1e-6 for axis, boundary, _ in boundaries):
                continue
            positions = (a[:3], b[:3], add(b[:3], (-.055, 0, 0)), add(a[:3], (-.055, 0, 0)))
            polygon(mesh, positions, CARBON)
    for row in range(3 if damage < 3 else 2):
        y = -.454 + row * .031
        for segment in range(8):
            z0, z1 = -.505 + segment * .12625, -.505 + (segment + 1) * .12625
            a, _ = surface(original, (2.2, y, z0), 0, 1)
            b, _ = surface(original, (2.2, y, z1), 0, 1)
            # Four-sided solid slats follow the bumper's original curved front.
            a, b = add(a, (-.024, 0, 0)), add(b, (-.024, 0, 0))
            for dx, dy, normal in ((0, -.003, (0, -1, 0)), (0, .003, (0, 1, 0))):
                polygon(mesh, (add(a, (dx, dy, 0)), add(b, (dx, dy, 0)), add(b, (-.012, dy, 0)), add(a, (-.012, dy, 0))), CARBON, normal)
            polygon(mesh, (add(a, (0, -.003, 0)), add(b, (0, -.003, 0)), add(b, (0, .003, 0)), add(a, (0, .003, 0))), METAL, (1, 0, 0))
    # A thin splitter follows the original bumper's lower silhouette.
    if damage == 3:
        return  # The original final variant has lost most of the lower bumper.
    splitter_start = len(mesh.vertices)
    edge = [(1.76, -.505, -.86), (2.03, -.508, -.73), (2.12, -.508, -.60), (2.222, -.508, -.22),
            (2.222, -.508, .22), (2.12, -.508, .60), (2.03, -.508, .73), (1.76, -.505, .86)]
    for a, b in zip(edge, edge[1:]):
        outward = unit((.9, 0, (a[2] + b[2]) * .3))
        a1, b1 = add(a, mul(outward, .025)), add(b, mul(outward, .025))
        polygon(mesh, (a, b, b1, a1), CARBON, (0, 1, 0))
        polygon(mesh, (a1, b1, add(b1, (0, -.013, 0)), add(a1, (0, -.013, 0))), CARBON, outward)
    follow_damage(mesh, splitter_start, healthy, original)


def boundary_loop(mesh, face_indices):
    edges = {}
    for index in face_indices:
        face = mesh.faces[index]
        indices = struct.unpack_from("<3H", face, 2)
        for a, b in ((indices[0], indices[1]), (indices[1], indices[2]), (indices[2], indices[0])):
            key = tuple(sorted((a, b)))
            edges[key] = edges.get(key, 0) + 1
    boundary = [edge for edge, count in edges.items() if count == 1]
    result = [boundary[0][0], boundary[0][1]]
    while len(result) < len(boundary):
        following = [b if a == result[-1] else a for a, b in boundary if result[-1] in (a, b) and (b if a == result[-1] else a) not in result]
        if not following:
            raise ValueError("Lamp boundary is not a simple closed loop")
        result.append(following[0])
    return result


def line_clip(vertices, a, b, sign):
    """Clip CM3D vertices by a directed edge in the lamp's Y/Z plane."""
    def distance(vertex):
        return sign * ((b[0] - a[0]) * (vertex[2] - a[1]) - (b[1] - a[1]) * (vertex[1] - a[0]))
    if not vertices:
        return []
    result = []
    previous, previous_distance = vertices[-1], distance(vertices[-1])
    for current in vertices:
        current_distance = distance(current)
        if (current_distance >= 0) != (previous_distance >= 0):
            result.append(interpolate(previous, current, previous_distance / (previous_distance - current_distance)))
        if current_distance >= 0:
            result.append(current)
        previous, previous_distance = current, current_distance
    return result


def front_optics(mesh, source, selected, side):
    """Cut apertures in the lens and place reflector bowls behind its surface.

    All optical boundaries are projected onto the actual angular lamp panels.
    No reflector surface protrudes through the lens plane or outside its bezel.
    """
    triangles = [[source[i] for i in struct.unpack_from("<3H", face, 2)] for face in selected]

    def project_uv(u, v):
        for triangle in triangles:
            a, b, c = triangle
            denominator = (b[4] - c[4]) * (a[3] - c[3]) + (c[3] - b[3]) * (a[4] - c[4])
            wa = ((b[4] - c[4]) * (u - c[3]) + (c[3] - b[3]) * (v - c[4])) / denominator
            wb = ((c[4] - a[4]) * (u - c[3]) + (a[3] - c[3]) * (v - c[4])) / denominator
            weights = wa, wb, 1 - wa - wb
            if min(weights) >= -1e-6:
                return tuple(sum(weights[j] * triangle[j][k] for j in range(3)) for k in range(8))
        return None

    def project(y, z):
        for triangle in triangles:
            a, b, c = triangle
            denominator = (b[2] - c[2]) * (a[1] - c[1]) + (c[1] - b[1]) * (a[2] - c[2])
            wa = ((b[2] - c[2]) * (y - c[1]) + (c[1] - b[1]) * (z - c[2])) / denominator
            wb = ((c[2] - a[2]) * (y - c[1]) + (a[1] - c[1]) * (z - c[2])) / denominator
            weights = wa, wb, 1 - wa - wb
            if min(weights) >= -1e-6:
                return tuple(sum(weights[j] * triangle[j][k] for j in range(3)) for k in range(8))
        return None

    holes = []
    for u, v in HEADLAMP_UV_CENTERS[side]:
        vertex = project_uv(u, v - 1)
        circle = [project_uv(u + HEADLAMP_UV_RADIUS * math.cos(math.tau * i / 16),
                             v - 1 + HEADLAMP_UV_RADIUS * math.sin(math.tau * i / 16)) for i in range(16)]
        if vertex is None or any(point is None for point in circle):
            raise ValueError("Front optic UV aperture leaves the original lamp panel")
        center = vertex[1:3]
        boundary = [point[1:3] for point in circle]
        if sum(a[0] * b[1] - a[1] * b[0] for a, b in zip(boundary, boundary[1:] + boundary[:1])) < 0:
            boundary.reverse()
        holes.append((center, boundary))

    # Subtract each convex aperture from the original textured triangular lens.
    pieces = triangles
    for _, boundary in holes:
        retained = []
        for piece in pieces:
            inside = piece
            for a, b in zip(boundary, boundary[1:] + boundary[:1]):
                outside = line_clip(inside, a, b, -1)
                if len(outside) >= 3:
                    retained.append(outside)
                inside = line_clip(inside, a, b, 1)
        pieces = retained
    for piece in pieces:
        # Keep the source's interpolated corner-normal field over paint/glass;
        # averaging each clipped polygon would create new lighting facets.
        indices = []
        for vertex in piece:
            indices.append(len(mesh.vertices))
            mesh.vertices.append(vertex)
        for i in range(1, len(indices) - 1):
            mesh.triangle((indices[0], indices[i], indices[i + 1]), (255, 255, 255, 255), 0)

    for center, boundary in holes:
        # Split a boundary where it crosses an original triangle edge; the ring
        # then follows the same piecewise lens surface as the cut opening.
        perimeter = []
        for a, b in zip(boundary, boundary[1:] + boundary[:1]):
            cuts = {0.}
            r = b[0] - a[0], b[1] - a[1]
            for triangle in triangles:
                for va, vb in zip(triangle, triangle[1:] + triangle[:1]):
                    c, d = (va[1], va[2]), (vb[1], vb[2])
                    s = d[0] - c[0], d[1] - c[1]
                    denominator = r[0] * s[1] - r[1] * s[0]
                    if abs(denominator) < 1e-12:
                        continue
                    q = c[0] - a[0], c[1] - a[1]
                    t = (q[0] * s[1] - q[1] * s[0]) / denominator
                    u = (q[0] * r[1] - q[1] * r[0]) / denominator
                    if 1e-7 < t < 1 - 1e-7 and 0 <= u <= 1:
                        cuts.add(round(t, 10))
            perimeter.extend((a[0] + r[0] * t, a[1] + r[1] * t) for t in sorted(cuts))
        rings = []
        for factor, depth in ((1., 0.), (.83, -.004), (.25, -.012)):
            ring = []
            for y, z in perimeter:
                y, z = center[0] + (y - center[0]) * factor, center[1] + (z - center[1]) * factor
                vertex = project(y, z)
                ring.append(((vertex[0] + depth, y, z), vertex[3:5]))
            rings.append(ring)
        count = len(perimeter)
        for a, b in zip(rings, rings[1:]):
            for i in range(count):
                quad = (a[i], a[(i + 1) % count], b[(i + 1) % count], b[i])
                polygon(mesh, [point for point, _ in quad], (255, 255, 255, 255),
                        uvs=[uv for _, uv in quad], texture=0)
        vertex = project(*center)
        center_vertex = ((vertex[0] - .014, *center), vertex[3:5])
        for i in range(count):
            triangle = (center_vertex, rings[-1][i], rings[-1][(i + 1) % count])
            polygon(mesh, [point for point, _ in triangle], (255, 255, 255, 255),
                    uvs=[uv for _, uv in triangle], texture=0)


def flush_rear_lens(mesh, original):
    """Keep the rear lens perimeter flush, without raised borders or bars.

    The source atlas already authors the lens divisions and dark edge gasket.
    Preserve that contour/UV mapping and add only0.8mm of interior lens crown.
    Damage variants use their own original faces and texture coordinates.
    """
    source = original.vertices
    mesh.faces = list(original.faces[2:])
    for face_index in range(2):
        face = original.faces[face_index]
        indices = struct.unpack_from("<3H", face, 2)
        vertices = [source[i] for i in indices]
        grid = {}
        for i in range(4):
            for j in range(4 - i):
                weights = (i / 3, j / 3, 1 - (i + j) / 3)
                point = tuple(sum(weights[a] * vertices[a][k] for a in range(3)) for k in range(3))
                normal = unit(tuple(sum(weights[a] * vertices[a][k] for a in range(3)) for k in (5, 6, 7)))
                point = add(point, mul(normal, .0008 * 27 * math.prod(weights)))
                uv = tuple(sum(weights[a] * vertices[a][k] for a in range(3)) for k in (3, 4))
                grid[i, j] = mesh.vertex(point, normal, uv)
        for i in range(3):
            for j in range(3 - i):
                mesh.triangle((grid[i, j], grid[i + 1, j], grid[i, j + 1]), (255, 255, 255, 255), 0)
                if i + j < 2:
                    mesh.triangle((grid[i + 1, j], grid[i + 1, j + 1], grid[i, j + 1]), (255, 255, 255, 255), 0)


def lamp_housing(mesh, original, front, side, broken):
    if not front:
        flush_rear_lens(mesh, original)
        return
    # These panels include yellow paint and grille, not just the glass lens.
    # Keep their full original silhouette/UVs and never outline the panel edge.
    # Broken lamps use different atlas islands and have lost their reflectors.
    mesh.vertices, mesh.faces = list(original.vertices), list(original.faces)
    if broken:
        return
    mesh.faces = list(original.faces[3:])
    front_optics(mesh, original.vertices, original.faces[:3], side)
    # Only the real outer glass edge gets a0.75mm flush gasket; the rest of
    # this source panel is paint/grille and must not receive a surrounding rim.
    top, bottom = original.vertices[5 if side > 0 else 6], original.vertices[8]
    edge = [interpolate(top, bottom, t) for t in (.04, .77)]
    inset = [surface(original, (point[0] + .00075, point[1], point[2]), 2, side)[0] for point in edge]
    normal = unit(add(edge[0][5:], edge[1][5:]))
    polygon(mesh, (edge[0][:3], edge[1][:3], inset[1], inset[0]), RUBBER, normal)


def front_lamp_paint(mesh, side):
    """Finish only the obsolete painted lamp tip on the adjacent fender.

    The old side-view sprite continues onto the fender beyond the actual glass
    mesh. Sample adjacent plain yellow paint there instead of leaving a second
    optical tip. Keep existing positions and normal interpolation; exclude the
    Kaliber artwork above and the white sponsor strip below this small region.
    """
    planes = ((0, 1.835, 1), (0, 1.91, -1), (1, -.10, 1),
              (1, .075, -1), (2, side * .73, side))

    def smooth(value):
        value = max(0., min(1., value))
        return value * value * (3 - 2 * value)

    def paint_uv(vertex):
        # Fade only in the surrounding original paint, keeping full replacement
        # over the obsolete optical tip and along the actual glass join.
        weight = (smooth((vertex[0] - 1.835) / .017) *
                  smooth((vertex[1] + .10) / .015) *
                  smooth((.075 - vertex[1]) / .020) *
                  smooth((abs(vertex[2]) - .73) / .007))
        target_u, target_v = LAMP_SIDE_PAINT_UVS[side]
        # Stay within a measured yellow corridor while changing islands' rows:
        # moving straight toward the sample crosses the old lens or Kaliber red.
        corridor = 1188.5 / 1254
        u = vertex[3] + (corridor - vertex[3]) * smooth(weight * 3)
        v = vertex[4] + (target_v - vertex[4]) * smooth(weight * 3 - 1)
        u += (target_u - u) * smooth(weight * 3 - 2)
        return (*vertex[:3], u, v, *vertex[5:])

    def clip(vertices, axis, limit, sign):
        output = []
        if not vertices:
            return output
        a = vertices[-1]
        da = (a[axis] - limit) * sign
        for b in vertices:
            db = (b[axis] - limit) * sign
            if (da >= 0) != (db >= 0):
                output.append(interpolate(a, b, da / (da - db)))
            if db >= 0:
                output.append(b)
            a, da = b, db
        return output

    faces = []
    for face in mesh.faces:
        if struct.unpack_from("<h", face, 8)[0] != 0:
            faces.append(face)
            continue
        original = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
        inside, outside = original, []
        for axis, limit, sign in planes:
            piece = clip(inside, axis, limit, -sign)
            if len(piece) >= 3:
                outside.append(piece)
            inside = clip(inside, axis, limit, sign)
            if len(inside) < 3:
                break
        if len(inside) < 3:
            faces.append(face)
            continue
        pieces = [(piece, False) for piece in outside]
        # UV interpolation needs vertices through the short fade, while every
        # added position/normal remains barycentric on the existing body face.
        for x0, x1 in zip((1.835, 1.84, 1.844, 1.848, 1.852), (1.84, 1.844, 1.848, 1.852, 1.91)):
            strip = clip(clip(inside, 0, x0, 1), 0, x1, -1)
            if len(strip) < 3:
                continue
            heights = (-.10, -.096, -.092, -.088, -.085, .055, .060, .065, .070, .075)
            for y0, y1 in zip(heights, heights[1:]):
                piece = clip(clip(strip, 1, y0, 1), 1, y1, -1)
                if len(piece) >= 3:
                    pieces.append((piece, True))
        for piece, paint in pieces:
            indices = []
            for vertex in piece:
                if paint:
                    vertex = paint_uv(vertex)
                indices.append(len(mesh.vertices))
                mesh.vertices.append(vertex)
            for offset in range(1, len(indices) - 1):
                a, b, c = [mesh.vertices[i][:3] for i in (indices[0], indices[offset], indices[offset + 1])]
                if dot(cross(sub(b, a), sub(c, a)), cross(sub(b, a), sub(c, a))) < 1e-14:
                    continue
                added = bytearray(face)
                struct.pack_into("<3H", added, 2, indices[0], indices[offset], indices[offset + 1])
                faces.append(bytes(added))
    mesh.faces = faces


def smooth_wheel_arches(meshes):
    arches = ((213.0308, 7.8108, 24.8, 26.,
                   ((14., 191.78, 232.53), (18., 192.78, 231.78), (22., 193.78, 230.78), (26., 197.78, 229.78))),
                  (53.1860, .9872, 27.8, 24.,
                   ((14., 31.94, 73.19), (18., 33.94, 71.19), (22., 36.94, 69.19), (24., 38.69, 67.19))))
    definition = {"wheel_arches": arches, "arch_meshes": (0, 1, 2, 4, 5, 6, 9, 10, 11, 20, 21, 22, 27, 28, 29, 30, 31, 32)}
    panels.smooth_wheel_arches(meshes, definition, paint=0, minimum_side=.55,
                               atlas_coordinates=lambda vertex, side: (vertex[3] * 256, ((vertex[4] + 1) if side > 0 else -vertex[4]) * 256))


def mirror(mesh, side):
    mesh.vertices, mesh.faces = [], []
    center = (.64, .300, side * .895)
    rings = []
    # Streamlined shell, broad in Z and rounded in Y; the rear is a real recess.
    for x, factor in ((.755, .15), (.735, .68), (.675, 1.), (.565, .94)):
        ring = []
        for i in range(20):
            angle = math.tau * i / 20
            cy, cz = math.cos(angle), math.sin(angle)
            position = (x, center[1] + .063 * factor * cy, center[2] + .083 * factor * cz)
            normal = unit((.3 if x > .70 else -.1, cy, cz))
            ring.append(mesh.vertex(position, normal))
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        for i in range(20):
            mesh.quad((a[i], a[(i + 1) % 20], b[(i + 1) % 20], b[i]), BLUE)
    front = mesh.vertex((.759, center[1], center[2]), (1, 0, 0))
    for i in range(20):
        mesh.triangle((front, rings[0][i], rings[0][(i + 1) % 20]), BLUE)
    glass_ring = []
    for i in range(20):
        angle = math.tau * i / 20
        glass_ring.append(mesh.vertex((.571, center[1] + .050 * math.cos(angle), center[2] + .067 * math.sin(angle)), (-1, .1, side * .1)))
    for i in range(20):
        # The recessed lip faces rearward and inward, unlike the shell's
        # outward radial normals. Give this real annulus its own face normals:
        # averaging shell/glass normals can reverse individual triangles and
        # expose the hollow housing through back-face culling.
        j = (i + 1) % 20
        positions = [mesh.vertices[index][:3] for index in (rings[-1][i], glass_ring[i], glass_ring[j], rings[-1][j])]
        polygon(mesh, positions, RUBBER)
    rear = mesh.vertex((.568, center[1], center[2]), (-1, 0, 0))
    for i in range(20):
        mesh.triangle((rear, glass_ring[i], glass_ring[(i + 1) % 20]), GLASS)
    cylinder(mesh, (.77, .242, side * .824), (.685, .277, side * .876), .022, CARBON, 8)


def door_details(mesh, original, healthy, side):
    for x, y in ((-.105, .151), (-1.025, .163)):
        point, _ = surface(original, (x, y, side * .84), 2, side)
        # A shallow inset surround plus a raised handle with rounded ends.
        box(mesh, add(point, (0, 0, side * .004)), (.078, .021, .003), RUBBER)
        cylinder(mesh, add(point, (-.055, .003, side * .014)), add(point, (.055, .003, side * .014)), .009, BLUE, 8)
    # An actual small side-skirt flange, clear of both wheel arches.
    skirt_start = len(mesh.vertices)
    box(mesh, (0., -.479, side * .88), (.88, .014, .022), CARBON)
    follow_damage(mesh, skirt_start, healthy, original)


def bonnet_pins(mesh, original):
    for side in (-1, 1):
        point, normal = surface(original, (1.82, .14, side * .58), 1, 1)
        cylinder(mesh, add(point, mul(normal, .003)), add(point, mul(normal, .008)), .022, METAL, 10)
        cylinder(mesh, add(point, (-.018, .011, -.013)), add(point, (.018, .011, .013)), .0025, CARBON, 6)


def wipers(mesh, original):
    for pivot, elbow, blade_start, blade_end in (
            ((1.06, .29, -.40), (.99, .33, -.12), (.96, .35, -.36), (1.04, .30, .18)),
            ((1.06, .29, .30), (.99, .33, .40), (.95, .35, .17), (1.02, .31, .62))):
        a, na = surface(original, pivot, 1, 1)
        b, nb = surface(original, elbow, 1, 1)
        a, b = add(a, mul(na, .012)), add(b, mul(nb, .012))
        cylinder(mesh, a, b, .005, CARBON, 6)
        c, nc = surface(original, blade_start, 1, 1)
        d, nd = surface(original, blade_end, 1, 1)
        cylinder(mesh, add(c, mul(nc, .014)), add(d, mul(nd, .014)), .006, RUBBER, 6)


def window_seal(mesh, original, texture):
    selected = [index for index, face in enumerate(original.faces) if struct.unpack_from("<h", face, 8)[0] == texture]
    loop = boundary_loop(original, selected)
    for a, b in zip(loop, loop[1:] + loop[:1]):
        va, vb = original.vertices[a], original.vertices[b]
        pa, pb = add(va[:3], mul(unit(va[5:]), .003)), add(vb[:3], mul(unit(vb[5:]), .003))
        cylinder(mesh, pa, pb, .0032, RUBBER, 6, caps=False)


def rear_wing(mesh, original, healthy):
    # Remove the old wedge's wing surfaces, keeping the separately textured boot.
    def wing_face(face):
        points = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
        return all(p[0] < -2.0 for p in points) and max(p[1] for p in points) > .37
    mesh.faces = [face for face in mesh.faces if not wing_face(face)]
    wing_start = len(mesh.vertices)
    # Five chord positions form a curved aerofoil with a closed 24mm underside.
    rings = []
    for section in range(17):
        z = -.76 + section * .095
        span = abs(z) / .76
        top = []
        bottom = []
        for chord in range(5):
            t = chord / 4
            x = -2.0198 - .1579 * t
            y = (.4862 - .0388 * span ** 2) * (1 - t) + .5142 * t + .018 * math.sin(math.pi * t)
            # Preserve original wing artwork by following its original UV span.
            u = .8173 + z / .76 * .1794
            v = -.2687 - .0372 * t
            top.append(mesh.vertex((x, y, z), (.22, 1., 0), (u, v)))
            bottom.append(mesh.vertex((x, y - .024, z), (-.1, -1., 0)))
        rings.append((top, bottom))
    for section in range(16):
        for surface_index in (0, 1):
            a, b = rings[section][surface_index], rings[section + 1][surface_index]
            for chord in range(4):
                indices = (a[chord], b[chord], b[chord + 1], a[chord + 1])
                if surface_index == 0:
                    mesh.triangle(indices[:3], (255, 255, 255, 255), 0)
                    mesh.triangle((indices[0], indices[2], indices[3]), (255, 255, 255, 255), 0)
                else:
                    mesh.quad(indices, BLUE)
        for chord in (0, 4):
            polygon(mesh, [mesh.vertices[i][:3] for i in (rings[section][0][chord], rings[section + 1][0][chord], rings[section + 1][1][chord], rings[section][1][chord])], BLUE)
    for section, side in ((0, -1), (16, 1)):
        for chord in range(4):
            polygon(mesh, [mesh.vertices[i][:3] for i in (rings[section][0][chord], rings[section][0][chord + 1], rings[section][1][chord + 1], rings[section][1][chord])], BLUE, (0, 0, side))
    for side in (-1, 1):
        # Flat endplates and two shaped central mounting uprights.
        box(mesh, (-2.096, .486, side * .772), (.099, .049, .006), BLUE)
        box(mesh, (-2.087, .407, side * .47), (.024, .064, .013), BLUE)
    follow_damage(mesh, wing_start, healthy, original)


def exhaust(mesh):
    rings = []
    for x, radius in ((-2.12, .045), (-2.275, .045), (-2.276, .033), (-2.20, .033)):
        ring = []
        for i in range(16):
            angle = math.tau * i / 16
            y, z = math.cos(angle), math.sin(angle)
            normal = (0, y, z) if len(rings) < 2 else (0, -y, -z)
            ring.append(mesh.vertex((x, -.385 + radius * y, .47 + radius * z), normal))
        rings.append(ring)
    for row, (a, b) in enumerate(zip(rings, rings[1:])):
        for i in range(16):
            mesh.quad((a[i], a[(i + 1) % 16], b[(i + 1) % 16], b[i]), METAL if row < 2 else CARBON)
    polygon(mesh, [mesh.vertices[i][:3] for i in rings[-1]], RUBBER, (-1, 0, 0))


def detail_body(meshes, originals):
    """Return counts while editing the corresponding existing damage families."""
    before = [len(mesh.faces) for mesh in meshes]
    for mesh in meshes:
        conform_numeric_decals(mesh)
    for damage, index in enumerate((9, 10, 11, 12)):
        front_intake(meshes[index], originals[index], originals[9], damage)
    for index in (13, 14):
        bonnet_pins(meshes[index], originals[index])
    for index, side, broken in ((16, 1, False), (17, 1, True), (18, -1, False), (19, -1, True),
                                (45, 1, False), (46, 1, True), (47, -1, False), (48, -1, True)):
        lamp_housing(meshes[index], originals[index], index >= 45, side, broken)
    for side, indices in ((1, (0, 1, 2)), (-1, (4, 5, 6))):
        for index in indices:
            front_lamp_paint(meshes[index], side)
    for index in (24, 25):
        rear_wing(meshes[index], originals[index], originals[24])
    exhaust(meshes[26])
    for index in (27, 28, 29):
        door_details(meshes[index], originals[index], originals[27], -1)
        window_seal(meshes[index], originals[index], 3)
    for index in (30, 31, 32):
        door_details(meshes[index], originals[index], originals[30], 1)
        window_seal(meshes[index], originals[index], 3)
    for index in (33, 34, 35):
        wipers(meshes[index], originals[index])
        window_seal(meshes[index], originals[index], 2)
    for index in (38, 39, 40):
        window_seal(meshes[index], originals[index], 2)
    mirror(meshes[41], -1)
    mirror(meshes[43], 1)
    smooth_wheel_arches(meshes)
    return {index: len(mesh.faces) - before[index] for index, mesh in enumerate(meshes) if len(mesh.faces) != before[index]}


def wheel_mesh(header, definition):
    """Keep the source-sized broad tread, with rounded shoulders and blue rim."""
    mesh = Mesh(header, [], [])
    segments = definition["wheel_segments"]
    if not 24 <= segments <= 48:
        raise ValueError("Wheel segment count must be24..48")
    tire = definition["tire_color"]
    rim = definition["rim_color"]
    edge = definition["rim_edge_color"]
    profile = definition["wheel_profile"]
    rings = []
    for row, (z, radius) in enumerate(profile):
        previous = profile[max(0, row - 1)]
        following = profile[min(len(profile) - 1, row + 1)]
        dz, dr = following[0] - previous[0], following[1] - previous[1]
        ring = []
        for i in range(segments):
            angle = math.tau * i / segments
            x, y = math.cos(angle), math.sin(angle)
            # Small alternating tread-edge sipes are geometry, not extra decals.
            tread = .0015 if row in (1, len(profile) - 2) and i % 2 else 0
            ring.append(mesh.vertex((x * (radius - tread), y * (radius - tread), z), (x * dz, y * dz, -dr)))
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            mesh.quad((a[i], a[j], b[j], b[i]), tire)
    for side in (-1, 1):
        # A raised blue outer bead and a recessed textured center establish depth.
        outer, inner = [], []
        for i in range(segments):
            angle = math.tau * i / segments
            x, y = math.cos(angle), math.sin(angle)
            outer.append(mesh.vertex((x * .258, y * .258, side * .126), (0, 0, side)))
            inner.append(mesh.vertex((x * .235, y * .235, side * .117), (0, 0, side)))
        for i in range(segments):
            j = (i + 1) % segments
            mesh.quad((outer[i], outer[j], inner[j], inner[i]), edge)
        center = mesh.vertex((0, 0, side * .103), (0, 0, side), (.0618, -.4176))
        disk = []
        for i in range(segments):
            angle = math.tau * i / segments
            x, y = .235 * math.cos(angle), .235 * math.sin(angle)
            disk.append(mesh.vertex((x, y, side * .103), (0, 0, side), (.0618 + x / .7 * .1226, -.4176 + y / .7 * .125)))
        for i in range(segments):
            mesh.triangle((center, disk[i], disk[(i + 1) % segments]), (255, 255, 255, 255), 0)
        for i in range(definition["wheel_spokes"]):
            angle = math.tau * i / definition["wheel_spokes"]
            radial, tangent = (math.cos(angle), math.sin(angle)), (-math.sin(angle), math.cos(angle))
            corners = []
            for r, width in ((.061, -.012), (.232, -.009), (.232, .009), (.061, .012)):
                corners.append(mesh.vertex((radial[0] * r + tangent[0] * width, radial[1] * r + tangent[1] * width, side * .106), (0, 0, side)))
            inner_ridge = mesh.vertex((radial[0] * .061, radial[1] * .061, side * .130), (0, 0, side))
            outer_ridge = mesh.vertex((radial[0] * .232, radial[1] * .232, side * .119), (0, 0, side))
            mesh.quad((corners[0], corners[1], outer_ridge, inner_ridge), rim)
            mesh.quad((inner_ridge, outer_ridge, corners[2], corners[3]), edge)
        # Domed central locking hub; its sides meet the spoke plane.
        hub = mesh.vertex((0, 0, side * .139), (0, 0, side))
        hub_ring = [mesh.vertex((.066 * math.cos(math.tau * i / 16), .066 * math.sin(math.tau * i / 16), side * .121), (0, 0, side)) for i in range(16)]
        for i in range(16):
            mesh.triangle((hub, hub_ring[i], hub_ring[(i + 1) % 16]), edge)
    return mesh


def generate(source, definition, texture_directory=None):
    header, nodes, original, textures = decode(source)
    meshes = [Mesh(mesh.header, list(mesh.vertices), list(mesh.faces)) for mesh in original]
    shared_edges = shared_edge_controls(original, set(definition["curved_meshes"]))
    for index in definition["curved_meshes"]:
        meshes[index] = curved_mesh(original[index], definition["body_subdivisions"], definition["body_max_curve"], shared_edges)
    meshes[49] = wheel_mesh(original[49].header, definition)
    base_triangles = visible_triangles(nodes, meshes)
    body_details = detail_body(meshes, original) if definition.get("body_details", False) else {}
    if texture_directory is not None:
        texture_count = struct.unpack_from("<H", header, 16)[0]
        import renault_textures as texture_tools
        texture_tools.fit_name_uvs(meshes, textures, texture_count, texture_directory)
        texture_tools.fit_rear_lamp_uvs(meshes, texture_directory)
        texture_tools.fit_front_lamp_uvs(meshes, texture_directory)
        textures = texture_tools.replace_textures(textures, texture_count, texture_directory,
                                                  texture_tools.lamp_opaque_uvs(meshes, texture_directory) +
                                                  texture_tools.wheel_arch_opaque_uvs(meshes))
    output = pack(header, nodes, meshes, textures)
    stats = {"source_model_crc32": crc32(source), "output_crc32": crc32(output), "bytes": len(output),
             "original_visible_triangles": visible_triangles(nodes, original),
             "visible_triangles": visible_triangles(nodes, meshes), "wheel_triangles": len(meshes[49].faces),
             "wheel_vertices": len(meshes[49].vertices), "wheel_segments": definition["wheel_segments"],
             "curved_meshes": len(definition["curved_meshes"]), "body_detail_meshes": len(body_details),
             "body_detail_triangles": sum(body_details.values()),
             "visible_body_detail_triangles": visible_triangles(nodes, meshes) - base_triangles,
             "visible_body_triangles": visible_triangles(nodes, meshes) - 4 * len(meshes[49].faces)}
    return output, stats
