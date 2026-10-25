"""Body-attached inner housings joined to the generated Mega wheel apertures.

Canonical wheel nodes supply the centers. The invariant floor node carries
all four housings without changing wheel animation or damage bindings. Mouths
follow the actual healthy painted panels, including their lower sill ends.
"""

import math
import struct

from vehicle_model import cross, decode, dot, pack, polygon, sub, visible_triangles


SEGMENTS = 48
RADIAL_CLEARANCE = .055
THROAT_DEPTH = .08
INNER_DEPTH = -.30
FIXED_UNIT = 1 / 65536
GHOST_DEPTH = .2


def ghost_polygon(mesh, points, ghost_points, color, normal=None):
    """Add ordinary geometry and its quantized, mouth-attached Ghost shape."""
    first = len(mesh.vertices)
    polygon(mesh, points, color, normal)
    if not hasattr(mesh, 'ghost_positions'):
        mesh.ghost_positions = {}
    for index, point in enumerate(ghost_points, first):
        mesh.ghost_positions[index] = tuple(round(v * 65536) / 65536 for v in point)


def node_records(node_bytes):
    """Resolve the canonical translation-only node hierarchy in model space."""
    nodes, offset = [], 0
    identity_basis = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.)
    while offset < len(node_bytes):
        size, parent = struct.unpack_from('<Ih', node_bytes, offset)
        matrix = tuple(v / 65536 for v in struct.unpack_from('<16i', node_bytes, offset + 8))
        if matrix[:12] != identity_basis or matrix[15] != 1:
            raise ValueError('Mega wheel wells require canonical translation-only vehicle nodes')
        if parent >= len(nodes) or parent < -1:
            raise ValueError('Invalid vehicle parent order')
        origin = matrix[12:15]
        if parent >= 0:
            origin = tuple(a + b for a, b in zip(origin, nodes[parent]['origin']))
        count = node_bytes[offset + 76]
        variants = struct.unpack_from(f'<{count}H', node_bytes, offset + 80)
        node_id = struct.unpack_from('<I', node_bytes, offset + 72)[0] & 0xffffff
        nodes.append({'id': node_id, 'origin': origin, 'variants': variants})
        offset += size
    return nodes


def anchors(node_bytes):
    """Return the floor slot and wheel centers in the floor's local frame."""
    ids = {node['id']: node for node in node_records(node_bytes)}
    floor = ids[14]
    floor_meshes = set(floor['variants'])
    if len(floor_meshes) != 1 or 65535 in floor_meshes:
        raise ValueError('Mega wheel wells require the invariant canonical floor mesh')
    centers = [tuple(a - b for a, b in zip(ids[i]['origin'], floor['origin'])) for i in range(1, 5)]
    wheels = {slot for i in range(1, 5) for slot in ids[i]['variants']}
    if len(wheels) != 1 or 65535 in wheels or floor_meshes & wheels:
        raise ValueError('Mega wheel wells require separate canonical wheel and floor meshes')
    return next(iter(floor_meshes)), next(iter(wheels)), centers


def aperture(meshes, node_bytes, center, definition):
    """Trace the first physical skin edge radially around a wheel opening.

    Every nearby panel-vertex angle participates, so the contour retains the
    actual piecewise edges instead of fitting a floating circular approximation.
    Side triangles are the healthy visible painted meshes, transformed into
    the floor's frame. The available angular interval ends at the real lower
    skirt corners; neither endpoint is a prescribed upper-barrel angle.
    """
    records = node_records(node_bytes)
    floor_origin = next(node['origin'] for node in records if node['id'] == 14)
    paint = 1 if definition['geometry'] in ('honda', 'peugeot') else 0
    side = 1 if center[2] > 0 else -1
    triangles, angles = [], {math.tau * i / SEGMENTS for i in range(-SEGMENTS // 4, 3 * SEGMENTS // 4 + 1)}
    for node in records:
        if not node['variants'] or node['id'] in (1, 2, 3, 4, 14):
            continue
        mesh = meshes[node['variants'][0]]
        delta = sub(node['origin'], floor_origin)
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != paint:
                continue
            tri = [tuple(mesh.vertices[i][k] + delta[k] for k in range(3))
                   for i in struct.unpack_from('<3H', face, 2)]
            if (min(side * p[2] for p in tri) < .55 or
                    min(p[0] for p in tri) > center[0] + .65 or
                    max(p[0] for p in tri) < center[0] - .65 or
                    min(p[1] for p in tri) > center[1] + .65 or
                    max(p[1] for p in tri) < center[1] - .35):
                continue
            if abs(cross(sub(tri[1], tri[0]), sub(tri[2], tri[0]))[2]) < 1e-10:
                continue
            triangles.append(tri)
            for point in tri:
                dx, dy = point[0] - center[0], point[1] - center[1]
                if .18 < math.hypot(dx, dy) < .75:
                    angle = math.atan2(dy, dx)
                    angles.add(angle if angle >= -math.pi / 2 else angle + math.tau)

    edges = [(a, b) for tri in triangles for a, b in zip(tri, tri[1:] + tri[:1])]

    def trace(angle):
        dx, dy = math.cos(angle), math.sin(angle)
        best = None
        for a, b in edges:
            ex, ey = b[0] - a[0], b[1] - a[1]
            denominator = dx * ey - dy * ex
            if abs(denominator) < 1e-12:
                continue
            ax, ay = a[0] - center[0], a[1] - center[1]
            distance = (ax * ey - ay * ex) / denominator
            t = (ax * dy - ay * dx) / denominator
            if .18 < distance < .65 and -1e-8 <= t <= 1 + 1e-8:
                t = min(1., max(0., t))
                point = tuple(a[k] + t * (b[k] - a[k]) for k in range(3))
                if best is None or distance < best[0] - 1e-9 or (abs(distance - best[0]) < 1e-9 and side * point[2] > side * best[1][2]):
                    best = distance, point
        return best[1] if best else None

    traced = [(angle, trace(angle)) for angle in sorted(angles)]
    contour = [point for angle, point in traced if point is not None]
    if len(contour) < 10:
        raise ValueError('Could not trace the complete generated wheel aperture')
    # Quantize exactly like CM3D. Remove only sub-grid repeats/collinear points;
    # the remaining mouths coincide with the real skin to one fixed unit.
    contour = [tuple(round(v * 65536) / 65536 for v in point) for point in contour]
    unique = []
    for point in contour:
        if not unique or math.dist(point, unique[-1]) > FIXED_UNIT:
            unique.append(point)
    simplified = []
    for point in unique:
        while len(simplified) > 1:
            a, b = simplified[-2:]
            length = math.dist(a, point)
            if not length or math.sqrt(dot(cross(sub(b, a), sub(point, a)), cross(sub(b, a), sub(point, a)))) / length > FIXED_UNIT:
                break
            simplified.pop()
        simplified.append(point)
    return simplified


def _surface(mesh, points, color, normal, ghost_points=None):
    """Emit both cavity directions; discard only fixed-grid zero-area faces."""
    for i in range(1, len(points) - 1):
        tri = (points[0], points[i], points[i + 1])
        quantized = [tuple(round(v * 65536) for v in point) for point in tri]
        area = cross(sub(quantized[1], quantized[0]), sub(quantized[2], quantized[0]))
        if dot(area, area) == 0:
            continue
        # A warped connector needs each triangle's own plane. Quantize before
        # choosing its winding: a sub-grid sliver can reverse during packing.
        tri = [tuple(v / 65536 for v in point) for point in quantized]
        facing = area if dot(area, normal) >= 0 else tuple(-v for v in area)
        ghost_tri = tuple(ghost_points[j] for j in (0, i, i + 1)) if ghost_points is not None else tri
        ghost_polygon(mesh, tri, ghost_tri, color, facing)
        ghost_polygon(mesh, tri, ghost_tri, color, tuple(-v for v in facing))


def body_skin(meshes, node_bytes, definition, side):
    """Query the outer painted skin in the floor-local side projection."""
    records = node_records(node_bytes)
    floor_origin = next(node['origin'] for node in records if node['id'] == 14)
    paint = 1 if definition['geometry'] in ('honda', 'peugeot') else 0
    triangles = []
    for node in records:
        if not node['variants'] or node['id'] in (1, 2, 3, 4, 14):
            continue
        mesh = meshes[node['variants'][0]]
        delta = sub(node['origin'], floor_origin)
        for face in mesh.faces:
            if struct.unpack_from('<h', face, 8)[0] != paint:
                continue
            a, b, c = [tuple(mesh.vertices[i][k] + delta[k] for k in range(3))
                       for i in struct.unpack_from('<3H', face, 2)]
            denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(denominator) < 1e-12 or min(side * p[2] for p in (a, b, c)) < .55:
                continue
            bounds = (min(p[0] for p in (a, b, c)), max(p[0] for p in (a, b, c)),
                      min(p[1] for p in (a, b, c)), max(p[1] for p in (a, b, c)))
            triangles.append((a, b, c, denominator, bounds))

    def query(x, y):
        depths = []
        for a, b, c, denominator, (left, right, bottom, top) in triangles:
            if x < left - 1e-8 or x > right + 1e-8 or y < bottom - 1e-8 or y > top + 1e-8:
                continue
            wa = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / denominator
            wb = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / denominator
            wc = 1 - wa - wb
            if min(wa, wb, wc) >= -1e-7:
                depths.append(side * (wa * a[2] + wb * b[2] + wc * c[2]))
        return max(depths) if depths else None

    return query


def fit_throat(outer, throat, skin, side):
    """Keep the flare beneath the curved skin, with its mouth exactly welded.

    A radius alone cannot describe the folded fender: even a millimeter of
    lateral protrusion exposes a large black blade over the sponsor paint.
    Bound each flare ray and both emitted triangle planes by the actual skin.
    The2mm recess fades to zero at the original aperture attachment.
    """
    fitted = [list(point) for point in throat]
    for lip, point in zip(outer, fitted):
        depth = side * point[2]
        for t in (.05, .1, .2, .4, .6, .8, 1.):
            x, y = (a + t * (b - a) for a, b in zip(lip[:2], point[:2]))
            bound = skin(x, y)
            if bound is not None:
                depth = min(depth, (bound - (1 - t) * side * lip[2]) / t - .002)
        point[2] = side * depth

    # A pair of safe vertices does not make a safe triangle over a bent panel.
    # Probe both triangle centers and every edge midpoint, including seams.
    probes = ((1 / 3, 1 / 3, 1 / 3), (.5, .5, 0), (.5, 0, .5), (0, .5, .5))
    for _ in range(4):
        changed = False
        for i in range(len(outer) - 1):
            for corners in ((0, 1, 3), (0, 3, 2)):
                for weights in probes:
                    quad = (outer[i], outer[i + 1], fitted[i], fitted[i + 1])
                    point = tuple(sum(weight * quad[corner][k] for weight, corner in zip(weights, corners)) for k in range(3))
                    influence = sum(weight for weight, corner in zip(weights, corners) if corner >= 2)
                    if not influence:
                        continue  # The exact original skin edge stays fixed.
                    bound = skin(*point[:2])
                    if bound is not None and side * point[2] > bound - .002 * influence + FIXED_UNIT:
                        retreat = (side * point[2] - bound) / influence + .002
                        fitted[i][2] -= side * retreat
                        fitted[i + 1][2] -= side * retreat
                        changed = True
        if not changed:
            break
    return [tuple(point) for point in fitted]


def liner(mesh, center, radius, color, outer, skin):
    """Join the skin to a tire-clear throat, barrel and deep D-shaped backing.

    The mouth can be smaller than the tire where the painted fender overlaps
    the shoulder. It therefore expands inward to the clearance radius before
    continuing behind the wheel. Lower walls follow the actual sill endpoints.
    """
    x, y, z = center
    side = 1 if z > 0 else -1
    returned = []
    for point in outer:
        dx, dy = point[0] - x, point[1] - y
        distance = math.hypot(dx, dy)
        depth = side * point[2] - .002
        # Return inward before expanding radially. A neighboring overlapping
        # panel can start immediately beyond the opening at a shallower depth;
        # the flare must pass behind that measured fold, not bridge over it.
        for advance in (.0005, .001, .002):
            bound = skin(point[0] + dx / distance * advance, point[1] + dy / distance * advance)
            if bound is not None:
                depth = min(depth, bound - .002)
        returned.append((point[0], point[1], side * depth))
    throat = []
    for point in outer:
        dx, dy = point[0] - x, point[1] - y
        distance = math.hypot(dx, dy)
        expanded = max(radius, distance)
        throat.append((x + dx * expanded / distance, y + dy * expanded / distance, z + side * THROAT_DEPTH))
    throat = fit_throat(returned, throat, skin, side)
    inner = [(p[0], p[1], z + side * INNER_DEPTH) for p in throat]
    back_center = (x, y, inner[0][2])
    def contracted(points):
        # Ghost housings do not need the opaque tire-clear radial flare.
        # Extrude the exact skin contour inward, so compressed walls cannot
        # re-emerge through a curved fender beyond the original opening.
        return [(lip[0], lip[1], lip[2] + GHOST_DEPTH * (p[2] - lip[2]))
                for p, lip in zip(points, outer)]
    ghost_returned = contracted(returned)
    ghost_throat = contracted(throat)
    ghost_inner = contracted(inner)
    anchor = sum(p[2] for p in outer) / len(outer)
    ghost_center = (x, y, anchor + GHOST_DEPTH * (back_center[2] - anchor))
    for i in range(len(outer) - 1):
        dx = (outer[i][0] + outer[i + 1][0]) / 2 - x
        dy = (outer[i][1] + outer[i + 1][1]) / 2 - y
        _surface(mesh, (outer[i], outer[i + 1], returned[i + 1], returned[i]), color, (-dx, -dy, 0),
                 (outer[i], outer[i + 1], ghost_returned[i + 1], ghost_returned[i]))
        connector = (returned[i], returned[i + 1], throat[i + 1], throat[i])
        normal = cross(sub(connector[1], connector[0]), sub(connector[2], connector[0]))
        if normal[0] * dx + normal[1] * dy > 0:
            normal = tuple(-v for v in normal)
        _surface(mesh, connector, color, normal,
                 (ghost_returned[i], ghost_returned[i + 1], ghost_throat[i + 1], ghost_throat[i]))
        _surface(mesh, (throat[i], throat[i + 1], inner[i + 1], inner[i]), color, (-dx, -dy, 0),
                 (ghost_throat[i], ghost_throat[i + 1], ghost_inner[i + 1], ghost_inner[i]))
        _surface(mesh, (back_center, inner[i], inner[i + 1]), color, (0, 0, side),
                 (ghost_center, ghost_inner[i], ghost_inner[i + 1]))
    _surface(mesh, (back_center, inner[-1], inner[0]), color, (0, 0, side),
             (ghost_center, ghost_inner[-1], ghost_inner[0]))


def apply(model, definition):
    """Extend only the existing floor mesh, preserving packed media verbatim."""
    header, nodes, meshes, textures = decode(model)
    floor, wheel, centers = anchors(nodes)
    colors = {tuple(face[10:14]) for face in meshes[wheel].faces
              if struct.unpack_from('<h', face, 8)[0] == -1 and face[13] == 255}
    color = min(colors, key=lambda value: sum(value[:3]))
    radius = max(row[1] for row in definition['wheel_profile']) + RADIAL_CLEARANCE
    skins = {side: body_skin(meshes, nodes, definition, side) for side in (-1, 1)}
    contours = [aperture(meshes, nodes, center, definition)
                for center in centers]
    before = len(meshes[floor].faces)
    for center, contour in zip(centers, contours):
        liner(meshes[floor], center, radius, color, contour, skins[1 if center[2] > 0 else -1])
    triangles = len(meshes[floor].faces) - before
    return pack(header, nodes, meshes, textures), {
        'wheel_well_count': len(centers), 'wheel_well_triangles': triangles,
        'wheel_well_contour_vertices': [len(contour) for contour in contours],
        'wheel_well_mesh': floor, 'wheel_well_first_face': before,
        'ghost_positions': {floor: meshes[floor].ghost_positions},
        'visible_triangles': visible_triangles(nodes, meshes),
        'stored_triangles': sum(len(mesh.faces) for mesh in meshes),
    }
