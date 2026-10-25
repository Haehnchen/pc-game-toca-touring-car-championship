"""Source-preserving panel, UV-clipping and fixed-point join mathematics.

Material slots, omitted meshes and atlas projections are supplied by each car.
No vehicle-specific coordinates or source identifiers belong in this module.
"""

import math
import struct
from vehicle_model import Mesh, add, sub, scale, dot, cross, unit, interpolate, polygon, cylinder

def edge_key(a, b):
    return tuple(sorted((tuple(a[:3]), tuple(b[:3]))))


def edge_controls(a, b, *, normalize=unit):
    normal = normalize(a[5:])
    return scale(sub(add(scale(a[:3], 2), b[:3]), scale(normal, dot(sub(b[:3], a[:3]), normal))), 1 / 3)


def shared_edge_controls(meshes, selected, *, paint, excluded_meshes=(), normalize=unit):
    """Make UV seams watertight and keep borders against glass/lamps straight.

    Original corner normals disagree at some duplicated UV vertices. Averaging
    their cubic edge controls gives one physical edge for all copies. If an
    unchanged neighbor uses that edge, retain its original straight boundary.
    Damage variants participate too, preserving their existing panel joins.
    """
    uses = {}
    for index, mesh in enumerate(meshes):
        if index in excluded_meshes:
            continue
        for face in mesh.faces:
            vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
            curved = index in selected and struct.unpack_from("<h", face, 8)[0] in (-1, paint)
            for a, b in ((vertices[0], vertices[1]), (vertices[1], vertices[2]), (vertices[2], vertices[0])):
                if a[:3] > b[:3]:
                    a, b = b, a
                uses.setdefault(edge_key(a, b), []).append((curved, edge_controls(a, b, normalize=normalize), edge_controls(b, a, normalize=normalize)))
    result = {}
    for key, values in uses.items():
        if not all(value[0] for value in values):
            a, b = key
            result[key] = (scale(add(scale(a, 2), b), 1 / 3), scale(add(a, scale(b, 2)), 1 / 3))
        else:
            result[key] = tuple(tuple(sum(value[which][k] for value in values) / len(values) for k in range(3)) for which in (1, 2))
    return result


def curved_mesh(mesh, steps, max_curve, shared_edges=None, *, paint, normalize=unit, align_normals=True):
    """Evaluate curved cubic PN triangles with original UV/material boundaries.

    Displacement is limited by the vehicle definition. Coincident patch edges reuse exactly the
    same cubic controls, so existing panel seams and damage corners survive.
    Decal triangles (other textures) stay flat and retain their source vertices.
    """
    result = Mesh(mesh.header, list(mesh.vertices), [])
    cache = {tuple(round(x * 65536) for x in v): i for i, v in enumerate(result.vertices)}
    for face in mesh.faces:
        indices = struct.unpack_from("<3H", face, 2)
        if struct.unpack_from("<h", face, 8)[0] not in (-1, paint):
            result.faces.append(face)
            continue
        source = [mesh.vertices[i] for i in indices]
        p = [v[:3] for v in source]
        n = [normalize(v[5:]) for v in source]
        controls = {}
        for a, b in ((0, 1), (1, 0), (0, 2), (2, 0), (1, 2), (2, 1)):
            if shared_edges is None:
                controls[a, b] = edge_controls(source[a], source[b], normalize=normalize)
            else:
                controls[a, b] = shared_edges[edge_key(source[a], source[b])][0 if p[a] < p[b] else 1]
        edge_average = scale(tuple(sum(v[k] for v in controls.values()) for k in range(3)), 1 / 6)
        center = add(edge_average, scale(sub(edge_average, scale(add(add(p[0], p[1]), p[2]), 1 / 3)), .5))
        grid = {}
        for i in range(steps + 1):
            for j in range(steps + 1 - i):
                weights = (i / steps, j / steps, 1 - (i + j) / steps)
                linear = tuple(sum(weights[a] * p[a][k] for a in range(3)) for k in range(3))
                curved = tuple(sum(weights[a] ** 3 * p[a][k] for a in range(3))
                               + sum(3 * weights[a] ** 2 * weights[b] * value[k] for (a, b), value in controls.items())
                               + 6 * math.prod(weights) * center[k] for k in range(3))
                displacement = sub(curved, linear)
                length = math.sqrt(dot(displacement, displacement))
                position = add(linear, scale(displacement, min(1., max_curve / length))) if length else linear
                uv = tuple(sum(weights[a] * source[a][k] for a in range(3)) for k in (3, 4))
                normal = normalize(tuple(sum(weights[a] * n[a][k] for a in range(3)) for k in range(3)))
                vertex = (*position, *uv, *normal)
                key = tuple(round(x * 65536) for x in vertex)
                if key not in cache:
                    cache[key] = len(result.vertices)
                    result.vertices.append(vertex)
                grid[i, j] = cache[key]
        for i in range(steps):
            for j in range(steps - i):
                triangles = [(grid[i, j], grid[i + 1, j], grid[i, j + 1])]
                if i + j < steps - 1:
                    triangles.append((grid[i + 1, j], grid[i + 1, j + 1], grid[i, j + 1]))
                for triangle in triangles:
                    output = bytearray(face)
                    a, b, c = [result.vertices[index] for index in triangle]
                    if align_normals and dot(cross(sub(b[:3], a[:3]), sub(c[:3], a[:3])), add(add(a[5:], b[5:]), c[5:])) < 0:
                        triangle = (triangle[0], triangle[2], triangle[1])
                    struct.pack_into("<3H", output, 2, *triangle)
                    result.faces.append(bytes(output))
    return result


def surface(mesh, point, axis, sign, *, normalize=unit):
    """Project onto the actual original panel, including its damage deformation."""
    axes = [i for i in range(3) if i != axis]
    candidates = []
    for face in mesh.faces:
        vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
        a, b, c = [(v[axes[0]], v[axes[1]]) for v in vertices]
        x, y = point[axes[0]], point[axes[1]]
        denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(denominator) < 1e-10:
            continue
        wa = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / denominator
        wb = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / denominator
        weights = wa, wb, 1 - wa - wb
        if min(weights) < -1e-6:
            continue
        position = tuple(sum(weights[j] * vertices[j][k] for j in range(3)) for k in range(3))
        normal = normalize(tuple(sum(weights[j] * vertices[j][k] for j in range(3)) for k in (5, 6, 7)))
        candidates.append((position[axis] * sign, position, normal))
    if not candidates:
        return tuple(point), tuple(sign if k == axis else 0 for k in range(3))
    _, position, normal = max(candidates)
    return position, normal


def clip(vertices, axis, bound, sign):
    result = []
    if not vertices:
        return result
    previous = vertices[-1]
    previous_distance = (previous[axis] - bound) * sign
    for current in vertices:
        distance = (current[axis] - bound) * sign
        if (distance >= 0) != (previous_distance >= 0):
            result.append(interpolate(previous, current, previous_distance / (previous_distance - distance)))
        if distance >= 0:
            result.append(current)
        previous, previous_distance = current, distance
    return result


def emit_source_polygon(mesh, vertices, face):
    if len(vertices) < 3:
        return
    indices = [mesh.vertex(v[:3], v[5:], v[3:5]) for v in vertices]
    for i in range(1, len(indices) - 1):
        a, b, c = [mesh.vertices[index][:3] for index in (indices[0], indices[i], indices[i + 1])]
        if dot(cross(sub(b, a), sub(c, a)), cross(sub(b, a), sub(c, a))) < 1e-16:
            continue
        output = bytearray(face)
        struct.pack_into("<3H", output, 2, indices[0], indices[i], indices[i + 1])
        mesh.faces.append(bytes(output))


def conform_numeric_decals(mesh, *, paint, decal_slots, clearance=.0025, normalize=unit):
    """Clip number decals to the actual painted surface, with2.5mm clearance.

    Each result lies on one body triangle, so a curved panel cannot cross through
    the decal between its vertices. Original corner UVs interpolate across every
    clipped fragment; there is no scale/crop change or large floating quad.
    """
    decals = [face for face in mesh.faces if struct.unpack_from("<h", face, 8)[0] in decal_slots]
    if not decals:
        return
    body = [[mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)] for face in mesh.faces if struct.unpack_from("<h", face, 8)[0] == paint]
    mesh.faces = [face for face in mesh.faces if struct.unpack_from("<h", face, 8)[0] not in decal_slots]
    for face in decals:
        original = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
        average_normal = tuple(sum(v[k] for v in original) for k in (5, 6, 7))
        axis = max(range(3), key=lambda i: abs(average_normal[i]))
        sign = 1 if average_normal[axis] > 0 else -1
        u, v = [i for i in range(3) if i != axis]
        for triangle in body:
            a, b, c = triangle
            determinant = (b[u] - a[u]) * (c[v] - a[v]) - (b[v] - a[v]) * (c[u] - a[u])
            if abs(determinant) < 1e-12:
                continue
            orientation = 1 if determinant > 0 else -1
            clipped = original
            for p, q in zip(triangle, triangle[1:] + triangle[:1]):
                def distance(vertex):
                    return orientation * ((q[u] - p[u]) * (vertex[v] - p[v]) - (q[v] - p[v]) * (vertex[u] - p[u]))
                if not clipped:
                    break
                output = []
                previous, previous_distance = clipped[-1], distance(clipped[-1])
                for current in clipped:
                    current_distance = distance(current)
                    if (current_distance >= 0) != (previous_distance >= 0):
                        output.append(interpolate(previous, current, previous_distance / (previous_distance - current_distance)))
                    if current_distance >= 0:
                        output.append(current)
                    previous, previous_distance = current, current_distance
                clipped = output
            if len(clipped) < 3:
                continue
            projected = []
            for vertex in clipped:
                # Barycentric interpolation on this exact body plane.
                wb = ((vertex[u] - a[u]) * (c[v] - a[v]) - (vertex[v] - a[v]) * (c[u] - a[u])) / determinant
                wc = ((b[u] - a[u]) * (vertex[v] - a[v]) - (b[v] - a[v]) * (vertex[u] - a[u])) / determinant
                weights = 1 - wb - wc, wb, wc
                position = list(vertex[:3])
                position[axis] = sum(weights[j] * triangle[j][axis] for j in range(3)) + clearance * sign
                normal = normalize(tuple(sum(weights[j] * triangle[j][k] for j in range(3)) for k in (5, 6, 7)))
                projected.append((*position, *vertex[3:5], *normal))
            emit_source_polygon(mesh, projected, face)


def smooth_wheel_arches(meshes, definition, *, paint, atlas_coordinates, minimum_side, segments=48):
    """Replace the canonical pixel cutouts by48-sided physical openings.

    Centers/radii come from the vehicle definition. Small original mesh
    notches outside that contour are bridged, rather than enlarging the opening
    into sponsor artwork. Every point/normal/UV comes from the existing panels;
    only these narrow remaining bands request opaque generated-atlas texels.
    """
    # Lower limits/bounds are measured from the unchanged opaque source panels.
    # Fit only the upper arc: continuing its circle downwards widens the wells.
    arches = definition['wheel_arches']

    for index in definition['arch_meshes']:
        mesh = meshes[index]
        mesh.wheel_arch_faces = set()
        for side in (-1, 1):
            def uv(vertex):
                return atlas_coordinates(vertex, side)

            for cx, cy, radius, join_height, original_widths in arches:
                source_faces = list(mesh.faces)
                prior_marks = mesh.wheel_arch_faces
                candidates = []
                for face in source_faces:
                    if struct.unpack_from("<h", face, 8)[0] != paint:
                        continue
                    tri = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                    points = [uv(v) for v in tri]
                    if (min(side * v[2] for v in tri) < minimum_side or
                            max(p[0] for p in points) < cx - radius - 2 or min(p[0] for p in points) > cx + radius + 2 or
                            max(p[1] for p in points) < cy - radius - 2 or min(p[1] for p in points) > cy + radius + 2):
                        continue
                    if abs(cross((points[1][0] - points[0][0], points[1][1] - points[0][1], 0),
                                 (points[2][0] - points[0][0], points[2][1] - points[0][1], 0))[2]) < 1e-9:
                        continue
                    candidates.append((face, tri))
                if not candidates:
                    continue

                def clip(poly, a, b, sign):
                    def distance(vertex):
                        p = uv(vertex)
                        return sign * ((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]))
                    result = []
                    if not poly:
                        return result
                    a_vertex, da = poly[-1], distance(poly[-1])
                    for b_vertex in poly:
                        db = distance(b_vertex)
                        if (da >= 0) != (db >= 0):
                            result.append(interpolate(a_vertex, b_vertex, da / (da - db)))
                        if db >= 0:
                            result.append(b_vertex)
                        a_vertex, da = b_vertex, db
                    return result

                def contour(r):
                    bottom = original_widths[0][0]
                    first = math.asin((bottom - cy) / r)
                    angles = {first, math.pi - first}
                    angles.update(math.tau * i / segments for i in range(segments) if first < math.tau * i / segments < math.pi - first)
                    for height, _, _ in original_widths[1:]:
                        angle = math.asin((height - cy) / r)
                        angles.update((angle, math.pi - angle))
                    points = []
                    for angle in sorted(angles):
                        x, y = cx + r * math.cos(angle), cy + r * math.sin(angle)
                        if y < join_height:
                            for a, b in zip(original_widths, original_widths[1:]):
                                if a[0] - 1e-8 <= y <= b[0] + 1e-8:
                                    side_index = 1 if x < cx else 2
                                    t = (y - a[0]) / (b[0] - a[0])
                                    old_x = a[side_index] + t * (b[side_index] - a[side_index])
                                    blend = max(0., min(1., (y - bottom) / (join_height - bottom)))
                                    blend = blend * blend * (3 - 2 * blend)
                                    x = old_x + blend * (x - old_x)
                                    break
                        points.append((x, max(bottom, y)))
                    # Convex clipping requires consistent turns through the
                    # tiny measured lower-boundary raster variations.
                    ordered = sorted(set(points))
                    def turn(a, b, c):
                        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
                    halves = []
                    for sequence in (ordered, list(reversed(ordered))):
                        half = []
                        for point in sequence:
                            while len(half) > 1 and turn(half[-2], half[-1], point) <= 1e-10:
                                half.pop()
                            half.append(point)
                        halves.append(half[:-1])
                    return halves[0] + halves[1]

                inner, outer = contour(radius), contour(radius + 1.5)

                def partition(poly, boundary):
                    outside = []
                    for a, b in zip(boundary, boundary[1:] + boundary[:1]):
                        piece = clip(poly, a, b, -1)
                        if len(piece) >= 3:
                            outside.append(piece)
                        poly = clip(poly, a, b, 1)
                        if len(poly) < 3:
                            return [], outside
                    return poly, outside

                # UV extrapolation is only used for the original tiny triangular
                # gaps. A common lookup keeps adjoining bridge vertices equal.
                projected = {}

                def project(point):
                    key = tuple(round(value, 10) for value in point)
                    if key in projected:
                        return projected[key]
                    best = None
                    for _, tri in candidates:
                        a, b, c = [uv(v) for v in tri]
                        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                        wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / den
                        wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / den
                        weights = wa, wb, 1 - wa - wb
                        score = min(weights)
                        if best is None or score > best[0]:
                            best = score, tuple(sum(w * vertex[k] for w, vertex in zip(weights, tri)) for k in range(8))
                    projected[key] = best[1]
                    return best[1]

                faces, marked = [], set()

                def emit(poly, template, opaque):
                    for i in range(1, len(poly) - 1):
                        tri = (poly[0], poly[i], poly[i + 1])
                        normal = cross(sub(tri[1][:3], tri[0][:3]), sub(tri[2][:3], tri[0][:3]))
                        if dot(normal, normal) < 1e-13:
                            continue
                        indices = list(range(len(mesh.vertices), len(mesh.vertices) + 3))
                        if dot(normal, add(add(tri[0][5:], tri[1][5:]), tri[2][5:])) < 0:
                            indices[1], indices[2] = indices[2], indices[1]
                        mesh.vertices.extend(tri)
                        face = bytearray(template)
                        struct.pack_into("<3H", face, 2, *indices)
                        if opaque:
                            marked.add(len(faces))
                        faces.append(bytes(face))

                candidate_ids = {id(face) for face, _ in candidates}
                for face_index, face in enumerate(source_faces):
                    if id(face) not in candidate_ids:
                        if face_index in prior_marks:
                            marked.add(len(faces))
                        faces.append(face)
                        continue
                    tri = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
                    band, outside = partition(tri, outer)
                    if not band:
                        if face_index in prior_marks:
                            marked.add(len(faces))
                        faces.append(face)
                        continue
                    for piece in outside:
                        emit(piece, face, face_index in prior_marks)
                    _, remaining = partition(band, inner)
                    for piece in remaining:
                        emit(piece, face, True)

                # Bridge just the small old coarse boundary notches, with the
                # original body remaining on the side away from the wheel hole.
                edges = {}
                for face, tri in candidates:
                    for a, b, third in ((tri[0], tri[1], tri[2]), (tri[1], tri[2], tri[0]), (tri[2], tri[0], tri[1])):
                        key = tuple(sorted((tuple(round(v, 8) for v in uv(a)), tuple(round(v, 8) for v in uv(b)))))
                        count, *_ = edges.get(key, (0,))
                        edges[key] = (count + 1, a, b, third, face)
                for count, a, b, third, face in edges.values():
                    if count != 1:
                        continue
                    pa, pb, pc = uv(a), uv(b), uv(third)
                    dx, dy = pb[0] - pa[0], pb[1] - pa[1]
                    if (dx * (cy - pa[1]) - dy * (cx - pa[0])) * (dx * (pc[1] - pa[1]) - dy * (pc[0] - pa[0])) >= 0:
                        continue
                    cuts = {0., 1.}
                    for r in (radius, radius + 1.5):
                        qa, qb = pa[0] - cx, pa[1] - cy
                        aa, bb, cc = dx * dx + dy * dy, 2 * (qa * dx + qb * dy), qa * qa + qb * qb - r * r
                        disc = bb * bb - 4 * aa * cc
                        if disc >= 0:
                            cuts.update(t for t in ((-bb - math.sqrt(disc)) / (2 * aa), (-bb + math.sqrt(disc)) / (2 * aa)) if 0 < t < 1)
                    for i in range(segments):
                        rx, ry = math.cos(math.tau * i / segments), math.sin(math.tau * i / segments)
                        den = dx * ry - dy * rx
                        if abs(den) > 1e-12:
                            t = ((cx - pa[0]) * ry - (cy - pa[1]) * rx) / den
                            if 0 < t < 1:
                                cuts.add(t)
                    ordered = sorted(cuts)
                    for t0, t1 in zip(ordered, ordered[1:]):
                        mid = (pa[0] + dx * (t0 + t1) / 2, pa[1] + dy * (t0 + t1) / 2)
                        if mid[1] < join_height or not radius < math.dist(mid, (cx, cy)) < radius + 1.5:
                            continue
                        old_edge = [interpolate(a, b, t) for t in (t0, t1)]
                        new_edge = []
                        for vertex in old_edge:
                            p = uv(vertex)
                            angle = math.atan2(p[1] - cy, p[0] - cx) % math.tau
                            offset = (angle % (math.tau / segments)) - math.pi / segments
                            r = radius * math.cos(math.pi / segments) / math.cos(offset)
                            new_edge.append(project((cx + r * math.cos(angle), cy + r * math.sin(angle))))
                        emit((old_edge[0], old_edge[1], new_edge[1], new_edge[0]), face, True)
                mesh.faces, mesh.wheel_arch_faces = faces, marked


def uv_vertex(mesh, point, *, paint):
    """Interpolate the actual painted panel at one atlas coordinate."""
    best = None
    for face in mesh.faces:
        if struct.unpack_from('<h', face, 8)[0] != paint:
            continue
        tri = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        a, b, c = tri
        den = (b[4] - c[4]) * (a[3] - c[3]) + (c[3] - b[3]) * (a[4] - c[4])
        if abs(den) < 1e-12:
            continue
        wa = ((b[4] - c[4]) * (point[0] - c[3]) + (c[3] - b[3]) * (point[1] - c[4])) / den
        wb = ((c[4] - a[4]) * (point[0] - c[3]) + (a[3] - c[3]) * (point[1] - c[4])) / den
        weights = wa, wb, 1 - wa - wb
        score = min(weights)
        if best is None or score > best[0]:
            best = score, tuple(sum(w * vertex[k] for w, vertex in zip(weights, tri)) for k in range(8))
    if best is None or best[0] < -.00001:
        raise ValueError('Detail UV must lie on its source panel')
    return best[1]


def recess_uv(mesh, outline, depth, textured_walls=False, *, paint, wall_color):
    """Cut a real convex cavity with its original atlas on the back wall.

    Only the registered UV island is changed. Paint, lamps and grille sharing
    another part of the same mesh retain their own source silhouette and UVs.
    """
    def split(poly, a, b, sign):
        def distance(vertex):
            return sign * ((b[0] - a[0]) * (vertex[4] - a[1]) - (b[1] - a[1]) * (vertex[3] - a[0]))
        result = []
        if not poly:
            return result
        previous, dp = poly[-1], distance(poly[-1])
        for current in poly:
            dc = distance(current)
            if (dp >= 0) != (dc >= 0):
                result.append(interpolate(previous, current, dp / (dp - dc)))
            if dc >= 0:
                result.append(current)
            previous, dp = current, dc
        return result

    faces = list(mesh.faces)
    mesh.faces = []
    for face in faces:
        if struct.unpack_from('<h', face, 8)[0] != paint:
            mesh.faces.append(face)
            continue
        inside = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        outside = []
        for a, b in zip(outline, outline[1:] + outline[:1]):
            piece = split(inside, a, b, -1)
            if len(piece) >= 3:
                outside.append(piece)
            inside = split(inside, a, b, 1)
            if len(inside) < 3:
                break
        if len(inside) < 3:
            mesh.faces.append(face)
            continue
        for piece in outside:
            emit_source_polygon(mesh, piece, face)
        back = [(*sub(p[:3], scale(unit(p[5:]), depth)), *p[3:]) for p in inside]
        emit_source_polygon(mesh, back, face)
        for i, a in enumerate(inside):
            j = (i + 1) % len(inside)
            b = inside[j]
            on_boundary = any(abs((q[0] - p[0]) * (a[4] - p[1]) - (q[1] - p[1]) * (a[3] - p[0])) < 1e-10 and
                              abs((q[0] - p[0]) * (b[4] - p[1]) - (q[1] - p[1]) * (b[3] - p[0])) < 1e-10
                              for p, q in zip(outline, outline[1:] + outline[:1]))
            if on_boundary and math.dist(a[:3], b[:3]) > .00001:
                positions = (a[:3], b[:3], back[j][:3], back[i][:3])
                if textured_walls:
                    # Carry the actual lens/reflector border down into the bowl.
                    # A dark untextured wall would draw a false black wire outline.
                    polygon(mesh, positions, (255, 255, 255, 255),
                            uvs=(a[3:5], b[3:5], b[3:5], a[3:5]), texture=paint)
                else:
                    polygon(mesh, positions, wall_color)


def source_boundary(mesh, textures=None):
    edges = {}
    for face in mesh.faces:
        if textures is not None and struct.unpack_from('<h', face, 8)[0] not in textures:
            continue
        vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        for a, b in zip(vertices, vertices[1:] + vertices[:1]):
            key = edge_key(a, b)
            edges.setdefault(key, []).append((a, b))
    return [uses[0] for uses in edges.values() if len(uses) == 1]


def remove_quantized_slivers(meshes, selected):
    """CM3D stores16.16 positions; choose winding on those actual stored points."""
    for index in selected:
        mesh = meshes[index]
        faces, marks = [], set()
        old_marks = getattr(mesh, 'wheel_arch_faces', ())
        for face_index, original in enumerate(mesh.faces):
            indices = struct.unpack_from('<3H', original, 2)
            points = [mesh.vertices[i] for i in indices]
            a, b, c = [tuple(round(v * 65536) / 65536 for v in p[:3]) for p in points]
            normal = cross(sub(b, a), sub(c, a))
            if dot(normal, normal) < 1e-16:
                continue
            if dot(normal, add(add(points[0][5:], points[1][5:]), points[2][5:])) < 0:
                face = bytearray(original)
                struct.pack_into('<3H', face, 2, indices[0], indices[2], indices[1])
                original = bytes(face)
            if face_index in old_marks:
                marks.add(len(faces))
            faces.append(original)
        mesh.faces = faces
        if hasattr(mesh, 'wheel_arch_faces'):
            mesh.wheel_arch_faces = marks


def register_side_band(meshes, selected, left, right, top, bottom, knots, mapped, fade, *, paint):
    """A bounded continuous UV field over source paint, including damage charts.

    Split only where the field changes slope. Geometry, normals, paint area
    and material identities remain those of the existing curved surface.
    """
    levels = sorted((top + t * (bottom - top)) / 256 - 1 for t in knots)
    limits = ((3, left / 256, 1), (3, right / 256, -1),
              (4, levels[0], 1), (4, levels[-1], -1))

    def mapping(p):
        t = ((p[4] + 1) * 256 - top) / (bottom - top)
        result = t
        for a, b, c, d in zip(knots, knots[1:], mapped, mapped[1:]):
            if a - 1e-7 <= t <= b + 1e-7:
                result = c + (t - a) / (b - a) * (d - c)
                break
        u = p[3] * 256
        start, end = fade
        weight = 1.
        if u < start and start > left:
            weight = max(0., (u - left) / (start - left))
        elif u > end and right > end:
            weight = max(0., (right - u) / (right - end))
        result = t + (result - t) * weight
        return (*p[:4], (top + result * (bottom - top)) / 256 - 1, *p[5:])

    for index in selected:
        mesh = meshes[index]
        old_faces, old_marks = mesh.faces, getattr(mesh, 'wheel_arch_faces', ())
        mesh.faces, marks = [], set()
        for face_index, face in enumerate(old_faces):
            first = len(mesh.faces)
            if struct.unpack_from('<h', face, 8)[0] != paint:
                mesh.faces.append(face)
            else:
                inside = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                outside = []
                for axis, bound, sign in limits:
                    outside.append(clip(inside, axis, bound, -sign))
                    inside = clip(inside, axis, bound, sign)
                if len(inside) < 3:
                    mesh.faces.append(face)
                else:
                    for part in outside:
                        emit_source_polygon(mesh, part, face)
                    for low, high in zip(levels, levels[1:]):
                        part = clip(clip(inside, 4, low, 1), 4, high, -1)
                        emit_source_polygon(mesh, [mapping(p) for p in part], face)
            if face_index in old_marks:
                marks.update(range(first, len(mesh.faces)))
        if hasattr(mesh, 'wheel_arch_faces'):
            mesh.wheel_arch_faces = marks


def spoked_wheel(header, definition, *, rubber, recess, silver, bright):
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
            mesh.quad((a[i], a[j], b[j], b[i]), rubber)
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
            mesh.triangle((center, disk[i], disk[j]), recess)
            mesh.quad((disk[i], disk[j], lip[j], lip[i]), silver)
        for i in range(definition['wheel_spokes']):
            angle = math.tau * i / definition['wheel_spokes']
            radial, tangent = (math.cos(angle), math.sin(angle)), (-math.sin(angle), math.cos(angle))
            # The source spokes taper at the barrel and widen around the hub.
            outline = [(radial[0] * r + tangent[0] * w, radial[1] * r + tangent[1] * w, side * .099)
                       for r, w in ((hub, -.010), (spoke, -.007), (spoke, .007), (hub, .010))]
            top = [(p[0], p[1], side * (.134 if j in (0, 3) else .124)) for j, p in enumerate(outline)]
            polygon(mesh, top, bright, (0, 0, side))
            polygon(mesh, list(reversed(outline)), silver, (0, 0, -side))
            for j, p in enumerate(outline):
                k = (j + 1) % 4
                polygon(mesh, (p, outline[k], top[k], top[j]), silver)
        cylinder(mesh, (0, 0, side * .097), (0, 0, side * .14), hub, silver, segments=16)
    return mesh


def textured_wheel(header, definition, *, side_color, texture):
    mesh = Mesh(header, [], [])
    segments = definition['wheel_segments']
    radius, half = definition['wheel_radius'], definition['wheel_half_width']
    disc, bevel = definition['wheel_disc_radius'], definition['wheel_bevel']
    tire = tuple(definition['tire_color'])
    center_u, center_v = definition['wheel_patch_center']
    # The source picture's circle spans the full plate (radius = tyre radius),
    # so a smaller hub disc samples the proportionally smaller picture area.
    patch = definition['wheel_patch_half'] * disc / radius
    profile = [(-half, disc), (-half + bevel, radius), (half - bevel, radius), (half, disc)]
    rings = []
    for row, (z, ring_radius) in enumerate(profile):
        before, after = profile[max(0, row - 1)], profile[min(len(profile) - 1, row + 1)]
        dz, dr = after[0] - before[0], after[1] - before[1]
        rings.append([mesh.vertex((ring_radius * math.cos(math.tau * i / segments), ring_radius * math.sin(math.tau * i / segments), z),
                                  (dz * math.cos(math.tau * i / segments), dz * math.sin(math.tau * i / segments), -dr))
                      for i in range(segments)])
    for a, b in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            mesh.quad((a[i], a[j], b[j], b[i]), tire)
    for side in (-1, 1):
        hub = mesh.vertex((0, 0, side * half), (0, 0, side), (center_u, center_v))
        ring = []
        for i in range(segments):
            c, s = math.cos(math.tau * i / segments), math.sin(math.tau * i / segments)
            # The source plate maps +x to +u and +y to decreasing v.
            ring.append(mesh.vertex((disc * c, disc * s, side * half), (0, 0, side),
                                    (center_u + patch * c, center_v - patch * s)))
        for i in range(segments):
            mesh.triangle((hub, ring[i], ring[(i + 1) % segments]), side_color, texture)
    return mesh


def closed_lens(original, *, back_color):
    """Close the complete source lamp with a three-millimetre backing.

    The glass artwork carries the rounded reflector outline. Retaining the
    source front triangles avoids fine subdivisions while keeping both rear
    charts joined along their actual physical fold.
    """
    mesh = Mesh(original.header, list(original.vertices), list(original.faces))
    def back(vertex):
        return sub(vertex[:3], scale(unit(vertex[5:]), .003))
    for face in original.faces:
        vertices = [original.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        vertices.reverse()
        polygon(mesh, [back(p) for p in vertices], back_color,
                scale(unit(add(add(vertices[0][5:], vertices[1][5:]), vertices[2][5:])), -1),
                [p[3:5] for p in vertices])
    for a, b in source_boundary(original):
        polygon(mesh, (a[:3], b[:3], back(b), back(a)), back_color,
                uvs=(a[3:5], b[3:5], b[3:5], a[3:5]))
    return mesh


def window_seals(mesh, original, texture, *, color):
    for a, b in source_boundary(original, {texture}):
        if math.dist(a[:3], b[:3]) < .025:
            continue
        normal = unit(add(a[5:], b[5:]))
        cylinder(mesh, add(a[:3], scale(normal, .001)), add(b[:3], scale(normal, .001)), .0025, color, segments=6)
