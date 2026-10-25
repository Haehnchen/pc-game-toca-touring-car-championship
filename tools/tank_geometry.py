"""Source-bound Tank armor, circular fittings and body-attached cavity backs.

Planar armor and its atlas charts stay canonical. Source circular profiles gain
four arc segments per edge; openings follow the actual steered tire solids.
"""
from collections import defaultdict
import math
import struct

from vehicle_model import (Mesh, decode, pack, crc32,
                           visible_triangles, add, sub, scale, dot, cross, unit,
                           interpolate, polygon)
import vehicle_geometry as panels
from vehicle_wheel_wells import GHOST_DEPTH, ghost_polygon

TEXTURE = 0
BODY_MESH, WHEEL_MESH = 2, 3
SIDE_COLOR = (167, 167, 167, 255)
CAVITY_COLOR = (13, 15, 13, 255)
EDGE_COLOR = (48, 49, 41, 255)


def face_vertices(mesh, face):
    return [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]


def circular_profiles(original):
    """Six visible seven-corner profiles, identified in CRC-bound source faces."""
    points = lambda first, last: {v[:3] for f in original.faces[first:last + 1]
                                 for v in face_vertices(original, f)}
    inner = {p for p in points(186, 192) if p[0] < 1.6}
    outer = {p for p in points(193, 206) if p not in inner}
    back = {p for p in points(207, 220) if p[0] < 1.4}
    hatch_outer = {p for p in points(221, 234) if p[0] > .8}
    hatch_inner = {p for p in points(235, 248) if p not in hatch_outer}
    return [points(134, 138), inner, outer, back, hatch_outer, hatch_inner]


def circular_edges(original, steps):
    """Shared arc samples preserve original corners, UV seams and sharp folds."""
    result = {}
    for group in circular_profiles(original):
        if len(group) != 7:
            raise ValueError('Tank circular profile differs from its canonical seven corners')
        cy, cz = (sum(p[k] for p in group) / 7 for k in (1, 2))
        ordered = sorted(group, key=lambda p: math.atan2(p[2] - cz, p[1] - cy))
        for a, b in zip(ordered, ordered[1:] + ordered[:1]):
            aa, ab = math.atan2(a[2] - cz, a[1] - cy), math.atan2(b[2] - cz, b[1] - cy)
            delta = (ab - aa) % math.tau
            ra, rb = math.hypot(a[1] - cy, a[2] - cz), math.hypot(b[1] - cy, b[2] - cz)
            positions = [a]
            for i in range(1, steps):
                t = i / steps
                angle, radius = aa + t * delta, ra + t * (rb - ra)
                positions.append((a[0] + t * (b[0] - a[0]), cy + radius * math.cos(angle), cz + radius * math.sin(angle)))
            positions.append(b)
            result[tuple(sorted((a, b)))] = positions if a < b else list(reversed(positions))
    return result


def profile_uv_gradient(original, group, a, b):
    """Recover a ring's original affine Y/Z atlas projection, per UV chart.

    A geometric arc bows outside its old straight chord. Reusing chord UVs
    would turn circular painted rings into seven-lobed shapes. The projection
    correction applies only to added vertices; every original UV stays exact.
    Several profiles have a separate cap/side chart at the same position.
    """
    midpoint = tuple((x + y) / 2 for x, y in zip(a[3:5], b[3:5]))
    samples = []
    for p in sorted(group):
        choices = {v[3:5] for v in original.vertices if v[:3] == p}
        uv = min(choices, key=lambda q: sum((x - y) ** 2 for x, y in zip(q, midpoint)))
        samples.append((p[1], p[2], *uv))
    mean = [sum(v[k] for v in samples) / len(samples) for k in range(4)]
    centered = [tuple(x - y for x, y in zip(v, mean)) for v in samples]
    yy = sum(v[0] ** 2 for v in centered)
    zz = sum(v[1] ** 2 for v in centered)
    yz = sum(v[0] * v[1] for v in centered)
    determinant = yy * zz - yz * yz
    result = []
    for k in (2, 3):
        yu = sum(v[0] * v[k] for v in centered)
        zu = sum(v[1] * v[k] for v in centered)
        result.append(((yu * zz - zu * yz) / determinant,
                       (zu * yy - yu * yz) / determinant))
    return result


def rounded_fittings(original, definition):
    """Split source circular edges; unrelated armor triangles stay planar."""
    edges = circular_edges(original, definition['fitting_edge_segments'])
    profiles = circular_profiles(original)
    mesh = Mesh(original.header, list(original.vertices), [])
    for face in original.faces:
        tri = face_vertices(original, face)
        boundary = []
        for a, b in zip(tri, tri[1:] + tri[:1]):
            samples = edges.get(tuple(sorted((a[:3], b[:3]))))
            if samples is None:
                boundary.append(a)
                continue
            samples = samples if a[:3] < b[:3] else list(reversed(samples))
            group = next(group for group in profiles if a[:3] in group and b[:3] in group)
            gradient = profile_uv_gradient(original, group, a, b)
            for i, position in enumerate(samples[:-1]):
                v = interpolate(a, b, i / (len(samples) - 1))
                dy, dz = position[1] - v[1], position[2] - v[2]
                uv = tuple(v[k + 3] + derivative[0] * dy + derivative[1] * dz
                           for k, derivative in enumerate(gradient))
                boundary.append((*position, *uv, *v[5:]))
        if len(boundary) == 3:
            mesh.faces.append(face)
        else:
            center = tuple(sum(v[k] for v in tri) / 3 for k in range(8))
            for a, b in zip(boundary, boundary[1:] + boundary[:1]):
                panels.emit_source_polygon(mesh, (center, a, b), face)
    return mesh


def tire_profile(definition):
    """Fixed-point radial bounds contain every spin of the emitted wheel."""
    r, h, disc, bevel, count = (definition[k] for k in
                              ('wheel_radius', 'wheel_half_width', 'wheel_disc_radius', 'wheel_bevel', 'wheel_segments'))
    return tuple((round(z * 65536) / 65536,
                  max(math.hypot(round(radius * math.cos(math.tau * i / count) * 65536) / 65536,
                                 round(radius * math.sin(math.tau * i / count) * 65536) / 65536)
                      for i in range(count)))
                 for z, radius in ((-h, disc), (-h + bevel, r), (h - bevel, r), (h, disc)))


def steering_angles():
    return [math.tau * 398 / 4096 * i / 4 for i in range(-4, 5)]


def tire_support(direction, steering, profile):
    c, s = math.cos(steering), math.sin(steering)
    x, y, z = direction
    x, z = x * c - z * s, x * s + z * c
    return max(math.hypot(x, y) * r + z * axial for axial, r in profile)


def clearance_directions(definition):
    """Support planes of circular treads, both shoulders and yaw end caps."""
    slope = (definition['wheel_radius'] - definition['wheel_disc_radius']) / definition['wheel_bevel']
    directions = set()
    for angle in (-steering_angles()[-1], 0., steering_angles()[-1]):
        c, s = math.cos(angle), math.sin(angle)
        for i in range(24):
            x, y = math.cos(math.tau * i / 24), math.sin(math.tau * i / 24)
            for z in (-slope, 0., slope):
                directions.add(unit((x * c + z * s, y, -x * s + z * c)))
        for z in (-1., 1.):
            directions.add((z * s, 0., z * c))
    return tuple(sorted(directions))


def cutter_planes(center, front, definition):
    profile = tire_profile(definition)
    angles = steering_angles() if front else [0.]
    margin = definition['tire_clearance_cut']
    return [(n, dot(n, center) + max(tire_support(n, a, profile) for a in angles) + margin)
            for n in clearance_directions(definition)]


def clip_plane(vertices, normal, bound, sign):
    if not vertices:
        return []
    result = []
    previous = vertices[-1]
    da = (dot(normal, previous[:3]) - bound) * sign
    for current in vertices:
        db = (dot(normal, current[:3]) - bound) * sign
        if (da >= 0) != (db >= 0):
            result.append(interpolate(previous, current, da / (da - db)))
        if db >= 0:
            result.append(current)
        previous, da = current, db
    return result


def trim_tire_envelopes(mesh, nodes, definition):
    """Subtract steered tire envelopes with source-interpolated UVs.

    Every retained polygon has a separating support plane away from every
    tire spin at all nine steering positions. No original wheel node moves.
    """
    for node, center in sorted(wheel_nodes(nodes).items()):
        planes = cutter_planes(center, node < 3, definition)
        old_faces = mesh.faces
        mesh.faces = []
        for face in old_faces:
            tri = face_vertices(mesh, face)
            if any(min(v[k] for v in tri) > center[k] + .72 or
                   max(v[k] for v in tri) < center[k] - .72 for k in range(3)):
                mesh.faces.append(face)
                continue
            if any(min(dot(n, v[:3]) for v in tri) >= b for n, b in planes):
                mesh.faces.append(face)
                continue
            inside, pieces = tri, []
            for normal, bound in planes:
                piece = clip_plane(inside, normal, bound, 1)
                if len(piece) >= 3:
                    pieces.append(piece)
                inside = clip_plane(inside, normal, bound, -1)
                if len(inside) < 3:
                    break
            for piece in pieces:
                panels.emit_source_polygon(mesh, piece, face)


def armor_backs(mesh, definition):
    """Welded armor returns and inward dark backs close one-sided sheets.

    Exposed thickness remains exterior. Only inward faces disappear in Ghost.
    """
    faces = list(mesh.faces)
    normals, edges = defaultdict(list), defaultdict(list)
    key = lambda p: tuple(round(x * 65536) for x in p[:3])
    for face in faces:
        tri = face_vertices(mesh, face)
        normal = unit(cross(sub(tri[1][:3], tri[0][:3]), sub(tri[2][:3], tri[0][:3])))
        for v in tri:
            normals[key(v)].append(normal)
        for a, b in zip(tri, tri[1:] + tri[:1]):
            edges[tuple(sorted((key(a), key(b))))].append((a, b))
    back = {p: sub(tuple(v / 65536 for v in p), scale(unit(tuple(sum(n[k] for n in ns) for k in range(3))), definition['armor_thickness']))
            for p, ns in normals.items()}
    # Clipping adjacent source triangles can create collinear T junctions.
    # Split these spans before counting boundaries: an internal subdivision
    # must not acquire a spurious dark return along its exposed half-edge.
    loose = [uses[0] for uses in edges.values() if len(uses) == 1]
    endpoints = {key(v) for edge in loose for v in edge}
    spans = defaultdict(int)
    for a, b in loose:
        ka, kb = key(a), key(b)
        delta = sub(kb, ka)
        length2 = dot(delta, delta)
        if length2 < 1:
            continue
        ordered = [(0., ka), (1., kb)]
        for p in endpoints:
            t = dot(sub(p, ka), delta) / length2
            if 1e-8 < t < 1 - 1e-8 and dot(sub(p, add(ka, scale(delta, t))), sub(p, add(ka, scale(delta, t)))) < 1.1:
                ordered.append((t, p))
        ordered.sort()
        for (_, p), (_, q) in zip(ordered, ordered[1:]):
            if p != q:
                spans[tuple(sorted((p, q)))] += 1
    for (ka, kb), count in spans.items():
        if count != 1:
            continue
        a, b = tuple(x / 65536 for x in ka), tuple(x / 65536 for x in kb)
        # Original aperture plane: its recessed wall is constructed separately.
        if abs(a[0] - .8282470703125) < 2 / 65536 and abs(b[0] - .8282470703125) < 2 / 65536 and min(a[1], b[1]) > .5:
            continue
        polygon(mesh, (a, b, back[kb], back[ka]), EDGE_COLOR)
    mesh.armor_return_triangles = len(mesh.faces) - len(faces)
    mesh.cavity_first_face = len(mesh.faces)
    for face in faces:
        outer = [v[:3] for v in reversed(face_vertices(mesh, face))]
        inner = [back[key(v)] for v in reversed(face_vertices(mesh, face))]
        ghost = [add(a, scale(sub(b, a), GHOST_DEPTH)) for a, b in zip(outer, inner)]
        ghost_polygon(mesh, inner, ghost, CAVITY_COLOR)


def hatch_cavity(mesh, original, definition):
    """Retain the source opening and give it depth, never a flat front plug."""
    group = circular_profiles(original)[-1]
    cy, cz = (sum(p[k] for p in group) / 7 for k in (1, 2))
    ordered = sorted(group, key=lambda p: math.atan2(p[2] - cz, p[1] - cy))
    arcs = circular_edges(original, definition['fitting_edge_segments'])
    front = []
    for a, b in zip(ordered, ordered[1:] + ordered[:1]):
        samples = arcs[tuple(sorted((a, b)))]
        samples = samples if a < b else list(reversed(samples))
        front.extend(samples[:-1])
    rear = [sub(p, (definition['hatch_depth'], 0., 0.)) for p in front]
    ghost_rear = [sub(p, (GHOST_DEPTH * definition['hatch_depth'], 0., 0.)) for p in front]
    for i, (a, b, c, d) in enumerate(zip(front, front[1:] + front[:1], rear[1:] + rear[:1], rear)):
        normal = (0., cy - (a[1] + b[1]) / 2, cz - (a[2] + b[2]) / 2)
        j = (i + 1) % len(front)
        ghost_polygon(mesh, (a, b, c, d), (a, b, ghost_rear[j], ghost_rear[i]), CAVITY_COLOR, normal)
    ghost_polygon(mesh, rear, ghost_rear, CAVITY_COLOR, (1., 0., 0.))


def refined_body(original, nodes, definition):
    mesh = rounded_fittings(original, definition)
    trim_tire_envelopes(mesh, nodes, definition)
    mesh.skin_triangles = len(mesh.faces)
    armor_backs(mesh, definition)
    hatch_cavity(mesh, original, definition)
    return mesh


def wheel_mesh(header, definition):
    return panels.textured_wheel(header, definition, side_color=SIDE_COLOR, texture=TEXTURE)


def prepare(source, definition):
    if definition['source_model_crc32'] != '3d3ea7c9' or crc32(source) != '3d3ea7c9':
        raise ValueError('Tank geometry requires canonical HIGH member28')
    header, nodes, originals, textures = decode(source)
    if [mesh.header[:12].split(b'\0', 1)[0] for mesh in originals] != [b'GUNMID_1', b'GUNOUT_1', b'TankBody_1', b'WHLTANK_1']:
        raise ValueError('Tank source mesh order differs from the registered HIGH model')
    meshes = [Mesh(m.header, list(m.vertices), list(m.faces)) for m in originals]
    meshes[BODY_MESH] = refined_body(originals[BODY_MESH], nodes, definition)
    meshes[WHEEL_MESH] = wheel_mesh(originals[WHEEL_MESH].header, definition)
    return header, nodes, meshes, textures


FIXED_TOLERANCE = 2 / 65536


def wheel_nodes(nodes):
    """Original wheel pivots (node ids 1..4) from the 16.16 translation row."""
    result, offset = {}, 0
    while offset < len(nodes):
        size = struct.unpack_from('<I', nodes, offset)[0]
        node_id = struct.unpack_from('<I', nodes, offset + 72)[0] & 0xffffff
        if node_id in (1, 2, 3, 4):
            result[node_id] = tuple(value / 65536 for value in struct.unpack_from('<3i', nodes, offset + 56))
        offset += size
    return result


def tire_vertices(mesh):
    return {index for face in mesh.faces if struct.unpack_from('<h', face, 8)[0] == -1
            for index in struct.unpack_from('<3H', face, 2)}


def measure(source, definition):
    """Source-backed wheel measurements, in millimetres, for the touring-standard exception."""
    _, nodes, meshes, _ = prepare(source, definition)
    original = decode(source)[2]
    old, new, body = original[WHEEL_MESH], meshes[WHEEL_MESH], meshes[BODY_MESH]
    bounds = [(min(v[axis] for v in old.vertices), max(v[axis] for v in old.vertices)) for axis in range(3)]
    inside = all(low - FIXED_TOLERANCE <= vertex[axis] <= high + FIXED_TOLERANCE
                 for vertex in new.vertices for axis, (low, high) in enumerate(bounds))
    radius = max(math.hypot(new.vertices[i][0], new.vertices[i][1]) for i in tire_vertices(new))
    half_width = max(abs(vertex[2]) for vertex in new.vertices)
    pivots = wheel_nodes(nodes)
    outer = {node: abs(pivot[2]) + half_width for node, pivot in pivots.items()}
    body_half_width = max(abs(vertex[2]) for vertex in body.vertices)
    sweep = math.hypot(bounds[0][1], bounds[1][1])
    mm = lambda value: round(value * 1000, 3)
    return {
        'source_wheel_prism_half_extent_mm': [mm(bounds[0][1]), mm(bounds[1][1]), mm(bounds[2][1])],
        'source_spin_sweep_radius_mm': mm(sweep),
        'tyre_radius_mm': mm(radius),
        'tyre_width_mm': mm(2 * half_width),
        'wheel_pivot_abs_z_mm': {str(node): mm(abs(pivot[2])) for node, pivot in sorted(pivots.items())},
        'tyre_outer_plane_abs_z_mm': {str(node): mm(value) for node, value in sorted(outer.items())},
        'body_half_width_mm': mm(body_half_width),
        'tyre_inset_from_widest_body_mm': mm(body_half_width - max(outer.values())),
        'tyre_inside_source_prism': inside,
        'spin_sweep_not_larger_than_source': radius <= sweep,
    }


def generate(source, definition, texture_directory=None):
    header, nodes, meshes, textures = prepare(source, definition)
    if texture_directory is not None:
        # One canonical TANK atlas slot; final WebP preserves its source key.
        import vehicle_textures
        textures = vehicle_textures.replace_textures(textures, struct.unpack_from('<H', header, 16)[0], texture_directory)
    result = pack(header, nodes, meshes, textures)
    originals = decode(source)[2]
    return result, {'source_model_crc32': crc32(source), 'output_crc32': crc32(result), 'bytes': len(result),
                    'original_visible_triangles': visible_triangles(nodes, originals),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'wheel_triangles': len(meshes[WHEEL_MESH].faces), 'wheel_vertices': len(meshes[WHEEL_MESH].vertices),
                    'body_triangles': len(meshes[BODY_MESH].faces),
                    'skin_triangles': meshes[BODY_MESH].skin_triangles,
                    'armor_return_triangles': meshes[BODY_MESH].armor_return_triangles,
                    'cavity_triangles': len(meshes[BODY_MESH].faces) - meshes[BODY_MESH].cavity_first_face,
                    'ghost_hidden_starts': {BODY_MESH: meshes[BODY_MESH].cavity_first_face},
                    'ghost_positions': {BODY_MESH: meshes[BODY_MESH].ghost_positions}}


def texture_footprints(source, definition):
    """Final quantized atlas triangles, in canonical negative-V coordinates."""
    meshes = decode(generate(source, definition)[0])[2]
    return [tuple(v[3:5] for v in face_vertices(mesh, face))
            for mesh in meshes for face in mesh.faces if struct.unpack_from('<h', face, 8)[0] == TEXTURE]


def geometry_report(source, definition):
    """Certify entire quantized triangles against every spin, not just vertices."""
    h, nodes, prepared, textures = prepare(source, definition)
    body = prepared[BODY_MESH]
    quantized = decode(pack(h, nodes, prepared, textures))[2][BODY_MESH]
    directions, profile = clearance_directions(definition), tire_profile(definition)
    results = []
    for node, center in sorted(wheel_nodes(nodes).items()):
        angles = steering_angles() if node < 3 else [0.]
        supports = [[dot(n, center) + tire_support(n, a, profile) for n in directions] for a in angles]
        minima = [float('inf')] * len(angles)
        checked = 0
        for face in quantized.faces:
            tri = face_vertices(quantized, face)
            if any(min(v[k] for v in tri) > center[k] + .85 or max(v[k] for v in tri) < center[k] - .85 for k in range(3)):
                continue
            checked += 1
            lower = [min(dot(n, v[:3]) for v in tri) for n in directions]
            for i, support in enumerate(supports):
                minima[i] = min(minima[i], max(a - b for a, b in zip(lower, support)))
        results.append({'node': node, 'checked_triangles': checked,
                        'steering_degrees': [round(math.degrees(a), 6) for a in angles],
                        'clearance_lower_bounds_mm': [round(1000 * x, 6) for x in minima]})
    minimum = min(min(r['clearance_lower_bounds_mm']) for r in results)
    # Runtime truncates each sine/cosine toward zero to16.16. The yaw axis
    # length is <=1; expanding its Rodrigues spin gives a coefficient-error
    # Frobenius bound <6/65536 (including the nonunit squared-axis term).
    # The emitted wheel fits a564mm sphere, hence position error <0.052mm.
    # Reserve0.1mm, also covering float products, rather than claiming ideal
    # trigonometry is the exact runtime matrix.
    sphere_radius = max(math.hypot(z, r) for z, r in profile)
    allowance = .1
    return {'method': 'Separating support planes between complete final quantized body triangles and convex continuous-spin tire profile; front nine steering angles, rear straight. Includes armor returns and all cavity backs.',
            'minimum_clearance_mm': minimum,
            'runtime_rotation_basis_error_bound_mm': round(1000 * sphere_radius * 6 / 65536, 6),
            'runtime_rotation_basis_allowance_mm': allowance,
            'runtime_rotation_basis_reason': 'Each16.16 sine/cosine error is less than1/65536. Expanding yaw and Rodrigues spin, including the nonunit axis, bounds the matrix coefficient-error Frobenius norm by6/65536. At the actual wheel bounding radius below564mm this moves points less than0.052mm;0.1mm also covers float arithmetic.',
            'effective_minimum_clearance_mm': round(minimum - allowance, 6),
            'wheels': results, 'skin_triangles': body.skin_triangles,
            'armor_return_triangles': body.armor_return_triangles,
            'cavity_first_face': body.cavity_first_face,
            'cavity_triangles': len(body.faces) - body.cavity_first_face,
            'circular_profile_segments': 7 * definition['fitting_edge_segments'],
            'armor_thickness_mm': 1000 * definition['armor_thickness'],
            'hatch_depth_mm': 1000 * definition['hatch_depth']}
