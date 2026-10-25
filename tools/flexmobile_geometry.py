"""Source-bound Flexmobile panels, physical arches and body-attached housings.

UV charts, sharp fins, lower chrome folds and all node records remain canonical.
Shared PN patches round the broad skin; the front lamp shoulders receive a
local rolled profile with conforming samples and registered optical shells.
Wheel dimensions follow the actual seven-sided tread, not its keyed square caps.
"""
from collections import Counter
from functools import lru_cache
import math
import statistics
import struct

from vehicle_model import (Mesh, decode, pack, crc32, visible_triangles, add,
                           sub, scale, cross, dot, unit, interpolate, polygon)
import vehicle_geometry as panels
from vehicle_wheel_wells import GHOST_DEPTH, ghost_polygon, node_records

TEXTURE = 0
BODY_MESH, WHEEL_MESH = 0, 1
SIDE_COLOR = (167, 167, 167, 255)
TIRE_COLOR = (0, 8, 8, 255)
WELL_COLOR = (8, 10, 13, 255)
FIXED = 1 / 65536

# Face identities are bound to the CRC-checked HIGH member. Fins, lower chrome
# folds and the engine floor stay outside the broad-panel smoothing set.
FRONT_LAMP_FACES = frozenset(range(10))
LAMP_SUPPORT_FACES = frozenset((*FRONT_LAMP_FACES, 123, 242,
                               *range(223, 226), *range(235, 238)))
CURVED_FACES = frozenset((*range(18, 42), *range(103, 119), *range(137, 161),
                          *range(186, 193), *range(204, 223), *range(226, 235),
                          *range(256, 280), *range(305, 312))) | LAMP_SUPPORT_FACES

# Centres measured on the existing 256-square source/accepted FLEX chart.
# The full existing chrome/glass footprints feed four 32-segment rolled optical
# caps; the source atlas coordinates and Cadillac twin-lamp motifs stay intact.
FRONT_LENS_CENTERS = ((9., 79.5), (19.2, 79.5), (87., 79.5), (97.2, 79.5))


def wheel_centers(nodes):
    records = {record['id']: record for record in node_records(nodes)}
    if set(records) != {504, 300, 1, 2, 3, 4, 30}:
        raise ValueError('Flexmobile requires its original seven node identities')
    return [records[i]['origin'] for i in range(1, 5)]


def curved_body(original, definition, *, lenses=True):
    canonical = original
    original = Mesh(original.header, list(original.vertices), list(original.faces))
    register_front_lamp_chart(original, canonical)
    curved = Mesh(original.header, list(original.vertices),
                  [f for i, f in enumerate(original.faces) if i in CURVED_FACES - FRONT_LAMP_FACES])
    held = Mesh(original.header, list(original.vertices),
                [f for i, f in enumerate(original.faces) if i not in CURVED_FACES - FRONT_LAMP_FACES])
    controls = panels.shared_edge_controls([curved, held], {0}, paint=TEXTURE)
    result = panels.curved_mesh(curved, definition['body_subdivisions'],
                                definition['body_max_curve'], controls, paint=TEXTURE)
    # Linear samples on touched held neighbours match the PN edge samples.
    # The following local profile can then bend complete shared edges without
    # opening the old long-chord fender seams or the grille/pedestal joins.
    local, distant = [], []
    for face in held.faces:
        ps = [held.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        touched = max(p[0] for p in ps) > 2.5 and max(p[1] for p in ps) > .075
        (local if touched else distant).append(face)
    linear = panels.curved_mesh(Mesh(held.header, held.vertices, local),
                                definition['body_subdivisions'], 0., paint=TEXTURE)
    for face in linear.faces:
        panels.emit_source_polygon(result, [linear.vertices[i] for i in struct.unpack_from('<3H', face, 2)], face)
    result.faces.extend(distant)
    refine_lamp_support(result)
    round_lamp_support(result, definition, canonical)
    if lenses:
        round_front_lenses(result, definition, canonical)
    return result


def refine_lamp_support(mesh):
    local, distant = [], []
    for face in mesh.faces:
        ps = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        touched = max(p[0] for p in ps) > 2.5 and max(p[1] for p in ps) > .075
        (local if touched else distant).append(face)
    refined = panels.curved_mesh(Mesh(mesh.header, mesh.vertices, local), 3, 0., paint=TEXTURE)
    mesh.faces = distant
    for face in refined.faces:
        panels.emit_source_polygon(mesh, [refined.vertices[i] for i in struct.unpack_from('<3H', face, 2)], face)


def register_front_lamp_chart(mesh, original):
    """Keep the drawn circular optics affine before shaping the bay in depth.

    Coincident side/bonnet seam copies receive the same correction, so the
    optical chart can retain its original projection without opening a join.
    """
    front = Mesh(original.header, original.vertices, original.faces[:10])
    a, b, c = (original.vertices[i] for i in (3, 5, 4))
    du, dv = b[3] - a[3], c[4] - a[4]
    tangent_u = tuple((b[k] - a[k]) / du for k in range(3))
    tangent_v = tuple((c[k] - a[k] - tangent_u[k] * (c[3] - a[3])) / dv for k in range(3))
    corrections = {}
    for p in mesh.vertices:
        if p[0] <= 2.7:
            continue
        try:
            q = panels.uv_vertex(front, p[3:5], paint=TEXTURE)
        except ValueError:
            continue
        if math.dist(p[:3], q[:3]) > .036:
            continue
        blend = min(1., max(0., (q[1] - .075) / .035))
        blend = blend * blend * (3 - 2 * blend)
        affine = tuple(a[k] + tangent_u[k] * (p[3] - a[3]) + tangent_v[k] * (p[4] - a[4]) for k in range(3))
        target = tuple(q[k] + blend * (affine[k] - q[k]) for k in range(3))
        if math.dist(p[:3], target) > 1e-10:
            corrections[tuple(round(v * 65536) for v in p[:3])] = target
    for i, p in enumerate(mesh.vertices):
        position = corrections.get(tuple(round(v * 65536) for v in p[:3]))
        if position is not None:
            mesh.vertices[i] = (*position, *p[3:])


def round_lamp_support(mesh, definition, original):
    """Roll the optical bay and its outer shoulders across shared samples."""
    radius = definition['front_lamp_corner_radius']
    depth = definition['front_lamp_support_depth']
    front = Mesh(original.header, original.vertices, original.faces[:10])
    optics = [panels.uv_vertex(front, (x / 256, y / 256 - 1), paint=TEXTURE)
              for x, y in FRONT_LENS_CENTERS]
    optical_radius = definition['front_lens_radius_pixels'] * 5.78 / 256
    top, outside = .2738, 1.092
    def smooth(t):
        t = min(1., max(0., t))
        return t * t * (3 - 2 * t)
    def profile(p):
        x, y, z = p
        if x <= 2.5 or y <= .075 or abs(z) <= .67:
            return tuple(p)
        front, upper = smooth((x - 2.5) / .35), smooth((y - .075) / .04)
        lateral = max(.76 - abs(z), 0., abs(z) - .99) / .14
        vertical = (y - .18) / .12
        distance = lateral * lateral + vertical * vertical
        bulge = depth * max(0., 1 - distance) ** 2 * front * upper * smooth((abs(z) - .67) / .08)
        if radius and y > top - radius and abs(z) > outside - radius:
            cy, cz = (y - top + radius) / radius, (abs(z) - outside + radius) / radius
            length = math.hypot(cy, cz)
            if length > 1.:
                nearest = min(math.hypot(y - p[1], z - p[2]) for p in optics)
                keep_circle = 1 - (1 - smooth((nearest - optical_radius) / .035)) * smooth((x - 2.70) / .10)
                blend = smooth((x - 2.5) / .3621) * upper * keep_circle
                y += blend * radius * cy * (1 / length - 1)
                z += (1 if z > 0 else -1) * blend * radius * cz * (1 / length - 1)
        return (x + bulge, y, z)
    for i, p in enumerate(mesh.vertices):
        position = profile(p[:3])
        if position == p[:3]:
            continue
        normal = unit(p[5:])
        tangent = unit(cross(normal, (0., 0., 1.) if abs(normal[2]) < .9 else (0., 1., 0.)))
        bitangent = cross(normal, tangent)
        ta = sub(profile(add(p[:3], scale(tangent, .00001))), position)
        tb = sub(profile(add(p[:3], scale(bitangent, .00001))), position)
        mesh.vertices[i] = (*position, *p[3:5], *unit(cross(ta, tb)))


def round_front_lenses(mesh, definition, original):
    """Add compact round lens caps over the intact, source-textured support.

    Rigid radial cells preserve the canonical front chart's circular y/z field.
    The front frame seats ahead of the complete shaped support footprint; its
    closed sleeve returns behind that support without distorting the optics.
    """
    radius = definition['front_lens_radius_pixels'] / 256
    depth, lip = definition['front_lens_depth'], definition['front_lens_lip']
    segments = definition['front_lens_segments']
    support = Mesh(mesh.header, list(mesh.vertices), list(mesh.faces))
    def triangle(indices):
        # The radial topology establishes outward winding, including the rear
        # closure. Its shading normals must not turn that closure front-facing.
        mesh.faces.append(struct.pack('<H3Hh4B10x', 0, *indices, TEXTURE, *SIDE_COLOR))
    a, b, c = (original.vertices[i] for i in (3, 5, 4))
    du, dv = b[3] - a[3], c[4] - a[4]
    basis_u = tuple((b[k] - a[k]) / du for k in range(3))
    basis_v = tuple((c[k] - a[k] - basis_u[k] * (c[3] - a[3])) / dv for k in range(3))
    anchor = a
    tangent_u, tangent_v = basis_u[1:], basis_v[1:]
    plane_y = basis_v[0] / basis_v[1]
    plane_z = (basis_u[0] - plane_y * basis_u[1]) / basis_u[2]
    for px, py in FRONT_LENS_CENTERS:
        center = (px / 256, py / 256 - 1)
        source_center = tuple(anchor[k] + basis_u[k] * (center[0] - anchor[3]) + basis_v[k] * (center[1] - anchor[4]) for k in range(3))
        world_radius = radius * max(math.hypot(*tangent_u), math.hypot(*tangent_v))
        outline = [(source_center[1] + radius * (math.cos(i * math.tau / 64) * tangent_u[0] + math.sin(i * math.tau / 64) * tangent_v[0]),
                    source_center[2] + radius * (math.cos(i * math.tau / 64) * tangent_u[1] + math.sin(i * math.tau / 64) * tangent_v[1]))
                   for i in range(64)]
        seat_offset = 0.
        for face in support.faces:
            ps = [support.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            if (min(p[0] for p in ps) > 2.7 and
                    min(p[1] for p in ps) <= source_center[1] + world_radius and
                    max(p[1] for p in ps) >= source_center[1] - world_radius and
                    min(p[2] for p in ps) <= source_center[2] + world_radius and
                    max(p[2] for p in ps) >= source_center[2] - world_radius):
                for a, b in zip(outline, outline[1:] + outline[:1]):
                    clipped = []
                    def distance(p):
                        return (b[0] - a[0]) * (p[2] - a[1]) - (b[1] - a[1]) * (p[1] - a[0])
                    previous, dp = ps[-1], distance(ps[-1])
                    for current in ps:
                        dc = distance(current)
                        if (dp >= 0) != (dc >= 0):
                            clipped.append(interpolate(previous, current, dp / (dp - dc)))
                        if dc >= 0:
                            clipped.append(current)
                        previous, dp = current, dc
                    ps = clipped
                    if not ps:
                        break
                if ps:
                    seat_offset = max(seat_offset, *(p[0] - source_center[0] - plane_y * (p[1] - source_center[1]) -
                                                    plane_z * (p[2] - source_center[2]) for p in ps))
        seat = source_center[0] + seat_offset + .002
        def height(fraction):
            if fraction <= .68:
                return .6 * lip + depth * (1 - (fraction / .8) ** 2) ** 2
            knots = ((.68, .6 * lip + depth * (1 - (.68 / .8) ** 2) ** 2),
                     (.82, lip), (.92, .9 * lip), (1., .3 * lip))
            for (a, ha), (b, hb) in zip(knots, knots[1:]):
                if fraction <= b:
                    t = (fraction - a) / (b - a)
                    return ha + (hb - ha) * t * t * (3 - 2 * t)
            return .3 * lip
        def vertex(fraction, angle, base=False):
            u, v = fraction * math.cos(angle), fraction * math.sin(angle)
            uv = (center[0] + radius * u, center[1] + radius * v)
            y, z = (source_center[k + 1] + radius * (u * tangent_u[k] + v * tangent_v[k]) for k in (0, 1))
            offset = height(fraction)
            derivative = (height(min(1., fraction + .0001)) - height(max(0., fraction - .0001))) / .0002
            slope = derivative / (radius * 5.78)
            frame_x = seat + plane_y * (y - source_center[1]) + plane_z * (z - source_center[2])
            normal = unit((1., -plane_y + slope * math.sin(angle), -plane_z - slope * math.cos(angle)))
            if base:
                contact, _ = panels.surface(support, (0., y, z), 0, 1)
                x = max(contact[0] - .003, frame_x - .020) if contact[0] else frame_x - .012
                normal = unit((-.2, -math.sin(angle), math.cos(angle))) if fraction else (-1., 0., 0.)
            else:
                x = frame_x + offset
            return mesh.vertex((x, y, z), normal, uv)
        middle = vertex(0., 0.)
        back = vertex(0., 0., True)
        rings = [[vertex(fraction, math.tau * i / segments)
                  for i in range(segments)] for fraction in (.68, .82, .92, 1.)]
        base = [vertex(1., math.tau * i / segments, True) for i in range(segments)]
        for i in range(segments):
            j = (i + 1) % segments
            triangle((middle, rings[0][i], rings[0][j]))
            for inner, outer in zip(rings, rings[1:]):
                triangle((inner[i], outer[i], outer[j]))
                triangle((inner[i], outer[j], inner[j]))
            triangle((rings[-1][i], base[i], base[j]))
            triangle((rings[-1][i], base[j], rings[-1][j]))
            triangle((back, base[j], base[i]))


def arch_boundary(center, definition, front):
    x, y, _ = center
    count = definition['arch_segments'] // 2
    if front:
        rx = ry = definition['front_arch_radius']
        cy, left, right = y, x - .60, x + .40
    else:
        # The source rear skirt has a broad shallow cut, biased rearwards
        # relative to the wheel pivot. A round touring-car opening would
        # remove its defining painted skirt. Keep that measured shallow lip.
        bottom = y - .55
        top = definition['rear_arch_center_y'] + definition['rear_arch_height']
        left, right = (x + offset for offset in definition['rear_arch_offsets'])
        return [(right, bottom), (right, top - .045),
                (right - .015, top - .010), (right - .035, top),
                (left + .035, top), (left + .015, top - .010),
                (left, top - .045), (left, bottom)]
    # Start at the lower right; the open body's actual bottom cuts the two
    # lower edges. The last segment is below the entire source body.
    points = [(right, y - .55)] + [
        (x + rx * math.cos(math.pi * i / count), cy + ry * math.sin(math.pi * i / count))
        for i in range(count + 1)] + [(left, y - .55)]
    # The source's lower front mouth is wider toward the sill. Its tangent
    # joins the circular upper contour through a convex hull, avoiding a
    # non-convex clipping turn at the left equator.
    def turn(a, b, c):
        return (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
    halves = []
    for sequence in (sorted(points), list(reversed(sorted(points)))):
        half = []
        for point in sequence:
            while len(half) > 1 and turn(half[-2], half[-1], point) <= 1e-12:
                half.pop()
            half.append(point)
        halves.append(half[:-1])
    hull = halves[0] + halves[1]
    first = hull.index((right, y - .55))
    return hull[first:] + hull[:first]


def _clip(poly, a, b, sign):
    def distance(vertex):
        return sign * ((b[0] - a[0]) * (vertex[1] - a[1]) -
                       (b[1] - a[1]) * (vertex[0] - a[0]))
    result = []
    if not poly:
        return result
    previous, da = poly[-1], distance(poly[-1])
    for current in poly:
        db = distance(current)
        if (da >= 0) != (db >= 0):
            result.append(interpolate(previous, current, da / (da - db)))
        if db >= 0:
            result.append(current)
        previous, da = current, db
    return result


def cut_arches(mesh, centers, definition):
    """Subtract actual openings; mark only surviving front-arch UV fragments."""
    mesh.wheel_arch_faces = set()
    for wheel, center in enumerate(centers):
        front, side = wheel < 2, 1 if center[2] > 0 else -1
        boundary = arch_boundary(center, definition, front)
        old_faces, old_marks = mesh.faces, mesh.wheel_arch_faces
        mesh.faces, mesh.wheel_arch_faces = [], set()
        for index, face in enumerate(old_faces):
            tri = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            texture = struct.unpack_from('<h', face, 8)[0]
            nearby = (min(side * p[2] for p in tri) > .75 and
                      max(p[0] for p in tri) > center[0] - .67 and
                      min(p[0] for p in tri) < center[0] + .48 and
                      min(p[1] for p in tri) < max(p[1] for p in boundary) + 1e-9)
            first = len(mesh.faces)
            if not nearby:
                mesh.faces.append(face)
            else:
                inside, outside = tri, []
                for a, b in zip(boundary, boundary[1:] + boundary[:1]):
                    piece = _clip(inside, a, b, -1)
                    if len(piece) >= 3:
                        outside.append(piece)
                    inside = _clip(inside, a, b, 1)
                    if len(inside) < 3:
                        break
                for piece in outside:
                    panels.emit_source_polygon(mesh, piece, face)
            if index in old_marks or (nearby and front and texture == TEXTURE):
                mesh.wheel_arch_faces.update(range(first, len(mesh.faces)))

    # The source ENGFLO floor/engine diagonals cross behind the front wheels.
    # Retain those charts inboard; the closed housings replace their outboard
    # sections, which otherwise pass through the new steering envelope.
    old_faces, old_marks = mesh.faces, mesh.wheel_arch_faces
    mesh.faces, mesh.wheel_arch_faces = [], set()
    for index, face in enumerate(old_faces):
        first = len(mesh.faces)
        if struct.unpack_from('<h', face, 8)[0] == 1:
            tri = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            poly = panels.clip(panels.clip(tri, 2, -.44, 1), 2, .44, -1)
            panels.emit_source_polygon(mesh, poly, face)
        else:
            mesh.faces.append(face)
        if index in old_marks:
            mesh.wheel_arch_faces.update(range(first, len(mesh.faces)))
    # Partitioning a large source face can also produce distant outside
    # fragments. They need no alpha release even though the original face
    # crossed the arch, so keep only the actual local surviving skin.
    front_x = centers[0][0]
    mesh.wheel_arch_faces = {
        index for index in mesh.wheel_arch_faces
        if max(mesh.vertices[i][0] for i in struct.unpack_from('<3H', mesh.faces[index], 2)) > front_x - .67
        and min(mesh.vertices[i][0] for i in struct.unpack_from('<3H', mesh.faces[index], 2)) < front_x + .48}


def wheel_arch_opaque_uvs(meshes):
    """Localized final skin footprints, in top-origin normalized atlas space."""
    mesh = meshes[BODY_MESH]
    return [tuple((mesh.vertices[i][3], mesh.vertices[i][4] + 1)
                  for i in struct.unpack_from('<3H', mesh.faces[f], 2))
            for f in sorted(getattr(mesh, 'wheel_arch_faces', ()))]


def mouth_contour(mesh, center, definition, front):
    """Collect the exact clipped physical skin edges, including subdivision joins."""
    boundary = arch_boundary(center, definition, front)
    side = 1 if center[2] > 0 else -1
    ordered = {}
    for face in mesh.faces:
        if struct.unpack_from('<h', face, 8)[0] != TEXTURE:
            continue
        tri = [mesh.vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
        if min(side * p[2] for p in tri) < .90:
            continue
        for a, b in zip(tri, tri[1:] + tri[:1]):
            for segment, (p, q) in enumerate(zip(boundary[:-1], boundary[1:])):
                dx, dy = q[0] - p[0], q[1] - p[1]
                length2 = dx * dx + dy * dy
                def parameter(v):
                    return ((v[0] - p[0]) * dx + (v[1] - p[1]) * dy) / length2
                ts = [parameter(v) for v in (a, b)]
                if max(ts) < -1e-8 or min(ts) > 1 + 1e-8:
                    continue
                if any(abs(dx * (v[1] - p[1]) - dy * (v[0] - p[0])) > 1e-7 for v in (a, b)):
                    continue
                if abs(ts[1] - ts[0]) < 1e-12:
                    uses = zip(ts, (a, b))
                else:
                    values = (max(0., min(ts)), min(1., max(ts)))
                    uses = [(t, interpolate(a, b, (t - ts[0]) / (ts[1] - ts[0]))) for t in values]
                for t, v in uses:
                    key = round((segment + min(1., max(0., t))) * 1e7)
                    if key not in ordered or side * v[2] > side * ordered[key][2]:
                        ordered[key] = tuple(round(c * 65536) / 65536 for c in v)
    points = [v for _, v in sorted(ordered.items())]
    result = []
    for p in points:
        if not result or math.dist(p, result[-1]) > 2 * FIXED:
            result.append(p)
    if len(result) < 12:
        raise ValueError('Flexmobile physical mouth could not be traced')
    # A shallow skirt edge subtends almost a half-turn in a few centimetres.
    # Insert exact collinear mouth joins before expanding it into a round
    # throat; otherwise a long chord would cut through the actual tire.
    refined = [result[0]]
    for a, b in zip(result, result[1:]):
        first = math.atan2(a[1] - center[1], a[0] - center[0])
        last = math.atan2(b[1] - center[1], b[0] - center[0])
        while last < first:
            last += math.tau
        steps = max(1, math.ceil((last - first) / (math.tau / definition['arch_segments'])))
        for i in range(1, steps):
            angle = first + (last - first) * i / steps
            dx, dy = math.cos(angle), math.sin(angle)
            ex, ey = b[0] - a[0], b[1] - a[1]
            numerator = (a[0] - center[0]) * dy - (a[1] - center[1]) * dx
            denominator = ex * dy - ey * dx
            if abs(denominator) > 1e-12:
                t = -numerator / denominator
                if 0 < t < 1:
                    refined.append(tuple(round(c * 65536) / 65536 for c in interpolate(a, b, t)))
        refined.append(b)
    return refined


def _surface(mesh, vertices, normal, ghost_vertices):
    """Quantized dark surfaces face into the closed cavity."""
    points = [tuple(round(v * 65536) / 65536 for v in p) for p in vertices]
    for i in range(1, len(points) - 1):
        tri = (points[0], points[i], points[i + 1])
        area = cross(sub(tri[1], tri[0]), sub(tri[2], tri[0]))
        if dot(area, area) < 1e-16:
            continue
        ghost_polygon(mesh, tri, (ghost_vertices[j] for j in (0, i, i + 1)), WELL_COLOR, normal)


def add_well(mesh, center, mouth, definition):
    x, y, z = center
    side = 1 if z > 0 else -1
    depth = definition['well_return_depth']
    radius = definition['well_radius']
    lower_lip = min(p[1] for p in mouth)
    returned = [(p[0], p[1], z + side * min(depth, side * (p[2] - z) - .002)) for p in mouth]
    throat = []
    for p in mouth:
        dx, dy = p[0] - x, p[1] - y
        distance = math.hypot(dx, dy)
        ratio = max(1., radius / distance)
        throat.append((x + dx * ratio, max(lower_lip, y + dy * ratio), z + side * depth))
    # A real wheelhouse is open underneath. Close only its upper backing,
    # following the actual mouth endpoints; a rectangular below-wheel D would
    # project past the Cadillac's low skirts as four conspicuous black flaps.
    inner = [(p[0], p[1], z + side * definition['well_back_depth']) for p in throat]
    back_center = tuple(sum(p[k] for p in inner) / len(inner) for k in range(3))
    def contracted(points):
        return [(lip[0], lip[1], lip[2] + GHOST_DEPTH * (p[2] - lip[2]))
                for p, lip in zip(points, mouth)]
    ghost_returned, ghost_throat, ghost_inner = map(contracted, (returned, throat, inner))
    anchor = sum(p[2] for p in mouth) / len(mouth)
    ghost_center = (*(sum(p[k] for p in mouth) / len(mouth) for k in range(2)),
                    anchor + GHOST_DEPTH * (back_center[2] - anchor))
    before = len(mesh.faces)
    def emit(points, ghost_points):
        midpoint = tuple(sum(p[k] for p in points) / len(points) for k in range(3))
        _surface(mesh, points, sub(center, midpoint), ghost_points)
    for i in range(len(mouth) - 1):
        emit((mouth[i], mouth[i + 1], returned[i + 1], returned[i]),
             (mouth[i], mouth[i + 1], ghost_returned[i + 1], ghost_returned[i]))
        emit((returned[i], returned[i + 1], throat[i + 1], throat[i]),
             (ghost_returned[i], ghost_returned[i + 1], ghost_throat[i + 1], ghost_throat[i]))
    for i in range(len(throat) - 1):
        j = i + 1
        emit((throat[i], throat[j], inner[j], inner[i]),
             (ghost_throat[i], ghost_throat[j], ghost_inner[j], ghost_inner[i]))
        emit((back_center, inner[i], inner[j]), (ghost_center, ghost_inner[i], ghost_inner[j]))
    emit((back_center, inner[-1], inner[0]), (ghost_center, ghost_inner[-1], ghost_inner[0]))
    return {'mouth': mouth, 'returned': returned, 'throat': throat,
            'inner': inner, 'first_face': before, 'face_count': len(mesh.faces) - before}


def remove_slivers(mesh):
    old_faces, marks = mesh.faces, getattr(mesh, 'wheel_arch_faces', set())
    mesh.faces, mesh.wheel_arch_faces = [], set()
    for index, face in enumerate(old_faces):
        ids = struct.unpack_from('<3H', face, 2)
        vs = [mesh.vertices[i] for i in ids]
        ps = [tuple(round(c * 65536) / 65536 for c in v[:3]) for v in vs]
        area = cross(sub(ps[1], ps[0]), sub(ps[2], ps[0]))
        if dot(area, area) < 1e-16:
            continue
        if dot(area, add(add(vs[0][5:], vs[1][5:]), vs[2][5:])) < 0:
            record = bytearray(face)
            struct.pack_into('<3H', record, 2, ids[0], ids[2], ids[1])
            face = bytes(record)
        if index in marks:
            mesh.wheel_arch_faces.add(len(mesh.faces))
        mesh.faces.append(face)


@lru_cache(maxsize=8)
def _quantized_tire_profile(radius, half, disc, bevel, segments):
    result = []
    for z, r in ((-half, disc), (-half + bevel, radius), (half - bevel, radius), (half, disc)):
        bound = max(math.hypot(round(r * math.cos(math.tau * i / segments) * 65536) / 65536,
                               round(r * math.sin(math.tau * i / segments) * 65536) / 65536)
                    for i in range(segments))
        result.append((round(z * 65536) / 65536, bound))
    return tuple(result)


def tire_triangle_clearance(triangle, center, steering, definition):
    """Certified separation from the continuous arbitrary-spin tire solid.

    A support plane separates an entire final triangle from the convex surface
    of revolution. The returned gap is a conservative distance lower bound,
    covering triangle interiors and every spin angle, not just sampled points.
    """
    cosine, sine = math.cos(steering), math.sin(steering)
    points = []
    for vertex in triangle:
        x, y, z = sub(vertex, center)
        points.append((x * cosine - z * sine, y, x * sine + z * cosine))
    radius, half = definition['wheel_radius'], definition['wheel_half_width']
    disc, bevel = definition['wheel_disc_radius'], definition['wheel_bevel']
    profile = _quantized_tire_profile(radius, half, disc, bevel, definition['wheel_segments'])
    directions = [(0, 0, 1), (0, 0, -1)]
    normal = cross(sub(points[1], points[0]), sub(points[2], points[0]))
    directions.extend((normal, scale(normal, -1)))
    radial = [p[:2] for p in points]
    radial.append(tuple(sum(p[k] for p in points) / 3 for k in (0, 1)))
    for a, b in zip(points, points[1:] + points[:1]):
        delta = sub(b, a)
        denominator = delta[0] ** 2 + delta[1] ** 2
        if denominator:
            t = min(1., max(0., -(a[0] * delta[0] + a[1] * delta[1]) / denominator))
            radial.append((a[0] + t * delta[0], a[1] + t * delta[1]))
    for x, y in radial:
        length = math.hypot(x, y)
        if not length:
            continue
        x, y = x / length, y / length
        directions.append((x, y, 0))
        for (za, ra), (zb, rb) in zip(profile, profile[1:]):
            dz, dr = zb - za, rb - ra
            directions.append((x * dz, y * dz, -dr))
        # Mixed planes bridge the rounded shoulder/cap near steered skin folds.
        for z in (-2., -1., -.5, .5, 1., 2.):
            directions.append((x, y, z))
    gap = 0.
    for direction in directions:
        length = math.sqrt(dot(direction, direction))
        if length < 1e-12:
            continue
        n = scale(direction, 1 / length)
        support = max(math.hypot(n[0], n[1]) * r + n[2] * z for z, r in profile)
        gap = max(gap, min(dot(n, p) for p in points) - support)
    return gap


def geometry_report(source, definition):
    """Measured source dimensions and final-triangle housing/steering evidence."""
    header, nodes, meshes, textures = prepare(source, definition)
    original = decode(source)[2]
    tread = {i for f in original[WHEEL_MESH].faces if struct.unpack_from('<h', f, 8)[0] == -1
             for i in struct.unpack_from('<3H', f, 2)}
    radii = [math.hypot(original[WHEEL_MESH].vertices[i][0], original[WHEEL_MESH].vertices[i][1]) for i in tread]
    centers, body = wheel_centers(nodes), meshes[BODY_MESH]
    angles = [math.tau * 398 / 4096 * i / 4 for i in range(-4, 5)]
    minima, checked, gaps = [], [], []
    for wheel, (center, well) in enumerate(zip(centers, body.well_geometry)):
        # Include final skin and all housing faces, not just shell vertices.
        triangles = [[tuple(round(c * 65536) / 65536 for c in body.vertices[i][:3])
                      for i in struct.unpack_from('<3H', face, 2)] for face in body.faces]
        candidates = [tri for tri in triangles if all(
            max(p[k] for p in tri) >= center[k] - .70 and
            min(p[k] for p in tri) <= center[k] + .70 for k in range(3))]
        minima.append([min(tire_triangle_clearance(tri, center, angle, definition)
                           for tri in candidates) for angle in (angles if wheel < 2 else [0.])])
        checked.append(len(candidates))
        side = 1 if center[2] > 0 else -1
        samples = []
        for degrees in range(0, 181, 15):
            dx, dy = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
            hits = []
            for a, b in zip(well['mouth'], well['mouth'][1:]):
                ex, ey = b[0] - a[0], b[1] - a[1]
                determinant = dx * ey - dy * ex
                if abs(determinant) < 1e-12:
                    continue
                ax, ay = a[0] - center[0], a[1] - center[1]
                distance = (ax * ey - ay * ex) / determinant
                t = (ax * dy - ay * dx) / determinant
                if distance >= 0 and -1e-7 <= t <= 1 + 1e-7:
                    hits.append((distance, side * (a[2] + t * (b[2] - a[2]) - center[2]) - definition['wheel_half_width']))
            samples.append(1000 * min(hits)[1] if hits else None)
        gaps.append(samples)
    return {'source_crc32': crc32(source), 'source_tread_radius_mm': [1000 * min(radii), 1000 * max(radii)],
            'source_width_mm': 2000 * max(abs(v[2]) for v in original[WHEEL_MESH].vertices),
            'source_square_cap_corner_radii_mm': [1000 * min(math.hypot(v[0], v[1]) for v in original[WHEEL_MESH].vertices[:8]),
                                                 1000 * max(math.hypot(v[0], v[1]) for v in original[WHEEL_MESH].vertices[:8])],
            'wheel_centers': centers, 'steering_degrees': [math.degrees(a) for a in angles],
            'triangle_clearance_lower_bounds_mm': [[1000 * v for v in row] for row in minima],
            'checked_triangles_per_wheel': checked,
            'skin_gap_angles_degrees': list(range(0, 181, 15)),
            'skin_gap_samples_mm': gaps,
            'skin_gap_medians_mm': [statistics.median(v for v in row if v is not None) for row in gaps],
            'stance_policy': 'Source-bound special: original 365.385mm radius /330.2mm width and unchanged pivots; measured deep front wings and rear painted skirts do not use touring350/250mm or20..60mm gap targets.',
            'clearance_method': 'Separating support planes for final quantized triangles against continuous convex tire profile; covers all spin angles.',
            'wheel_well_mouth_vertices': [len(w['mouth']) for w in body.well_geometry],
            'body_triangles': body.well_first_face, 'well_triangles': len(body.faces) - body.well_first_face,
            'visible_triangles': visible_triangles(nodes, meshes),
            'ghost_hidden_starts': {BODY_MESH: body.well_first_face}}


def wheel_mesh(header, definition):
    return panels.textured_wheel(header, definition, side_color=SIDE_COLOR, texture=TEXTURE)


def prepare(source, definition):
    if definition['source_model_crc32'] != '3234277d' or crc32(source) != '3234277d':
        raise ValueError('Flexmobile geometry requires canonical HIGH member31')
    header, nodes, originals, textures = decode(source)
    if [mesh.header[:12].split(b'\0', 1)[0].decode() for mesh in originals] != ['FLEXBODY_1', 'HWHLFLEX_1']:
        raise ValueError('Flexmobile source mesh order differs from the registered HIGH model')
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    meshes[BODY_MESH] = curved_body(originals[BODY_MESH], definition)
    centers = wheel_centers(nodes)
    cut_arches(meshes[BODY_MESH], centers, definition)
    remove_slivers(meshes[BODY_MESH])
    mouths = [mouth_contour(meshes[BODY_MESH], c, definition, i < 2)
              for i, c in enumerate(centers)]
    body = meshes[BODY_MESH]
    body.well_first_face = len(body.faces)
    body.well_geometry = [add_well(body, c, mouth, definition) for c, mouth in zip(centers, mouths)]
    meshes[WHEEL_MESH] = wheel_mesh(originals[WHEEL_MESH].header, definition)
    return header, nodes, meshes, textures


def generate(source, definition, texture_directory=None, *, quality=70):
    header, nodes, meshes, textures = prepare(source, definition)
    if texture_directory is not None:
        # Two atlas slots. The canonical exact-black key becomes real alpha;
        # generated pure-black motif ink stays opaque near-black.
        import flexmobile_textures
        count = struct.unpack_from('<H', header, 16)[0]
        flexmobile_textures.fit_uvs(meshes, textures, count, texture_directory)
        textures = flexmobile_textures.replace_textures(textures, count, texture_directory,
                                                        arch_uvs=wheel_arch_opaque_uvs(meshes), quality=quality)
    result = pack(header, nodes, meshes, textures)
    originals = decode(source)[2]
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, originals),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'wheel_triangles': len(meshes[WHEEL_MESH].faces), 'wheel_vertices': len(meshes[WHEEL_MESH].vertices),
                    'wheel_well_count': 4, 'wheel_well_mesh': BODY_MESH,
                    'wheel_well_first_face': meshes[BODY_MESH].well_first_face,
                    'wheel_well_triangles': len(meshes[BODY_MESH].faces) - meshes[BODY_MESH].well_first_face,
                    'ghost_hidden_starts': {BODY_MESH: meshes[BODY_MESH].well_first_face},
                    'ghost_positions': {BODY_MESH: meshes[BODY_MESH].ghost_positions}}
