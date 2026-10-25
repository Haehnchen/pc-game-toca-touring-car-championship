"""Rounded Mega gloves in the canonical CMHANDY/shift-hand coordinate system.

The original sleeve sections supply the envelope of a new radial loft. Four
individually curled fingers and an opposed thumb replace the distal angular
mitten. Original triangles serve only as material/UV charts, not render faces.
No nodes, pivots, runtime bases or material identities are introduced.
"""

from functools import lru_cache
from collections import Counter
import json
import math
from pathlib import Path
import struct

from vehicle_model import Mesh, add, crc32, cross, decode, dot, interpolate, pack, scale, sub, unit, visible_triangles

DEFINITION = Path(__file__).resolve().parents[1] / 'assets/models/cockpits/hands/definition.json'
WHITE = (255, 255, 255, 255)
CAR_IDS = (0, 1, 2, 3, 4, 5, 6, 7, 9)


def _inverse_affine(matrix):
    rows = [[matrix[4 * j + i] for j in range(3)] + [1. if i == j else 0. for j in range(3)] for i in range(3)]
    for i in range(3):
        pivot = max(range(i, 3), key=lambda j: abs(rows[j][i]))
        rows[i], rows[pivot] = rows[pivot], rows[i]
        amount = rows[i][i]
        rows[i] = [value / amount for value in rows[i]]
        for j in range(3):
            if j != i:
                amount = rows[j][i]
                rows[j] = [a - amount * b for a, b in zip(rows[j], rows[i])]
    linear = [row[3:] for row in rows]
    return linear, tuple(-dot(row, matrix[12:15]) for row in linear)


def _fit_group(role, car_id):
    if car_id not in CAR_IDS:
        raise ValueError('Hand geometry requires a canonical cockpit car ID')
    if role == 'left-hand':
        return 'left-source'
    if car_id == 9:
        return 'right-flex'
    return 'right-lhd' if car_id in (1, 2, 4, 7) else 'right-rhd'


def _grip_affine(definition, group):
    """Map the authored left-hand grasp into the actual opposite runtime basis.

    This is authored mesh fitting, after UV assignment. Cockpit matrices remain
    untouched at runtime; the fixed normal-pose matrices are source evidence.
    """
    reference = definition['grip_fits']['reference']
    target = definition['grip_fits'][group]
    source = reference['hand_matrix']
    inverse, translation = _inverse_affine(target['hand_matrix'])
    # Both normal grip sides reflect about the wheel's lateral center. The
    # source role matrices already include the original fixed1.5 glove scale.
    reflected_offset = (source[12], source[13], reference['wheel_center_z'] + target['wheel_center_z'] - source[14])
    reflected_axes = [(source[4 * j], source[4 * j + 1], -source[4 * j + 2]) for j in range(3)]
    if target.get('rotate_in_wheel_chart'):
        chart = definition['grip_fits']['wheel_chart']
        slope, vertical = chart['slope'], chart['vertical']
        origin = (*chart['center_xy'], reference['wheel_center_z'])
        destination = (*chart['center_xy'], target['wheel_center_z'])
        first = math.atan2(-(source[14] - origin[2]), (source[13] - origin[1]) / vertical)
        last = math.atan2(target['hand_matrix'][14] - destination[2],
                          (target['hand_matrix'][13] - destination[1]) / vertical)
        cosine, sine = math.cos(last - first), math.sin(last - first)

        def chart_vector(point):
            q, z, depth = point[1] / vertical, -point[2], point[0] - slope * point[1]
            turn_q, turn_z = q * cosine - z * sine, q * sine + z * cosine
            return depth + slope * vertical * turn_q, vertical * turn_q, turn_z

        reflected_offset = add(destination, chart_vector(sub(source[12:15], origin)))
        reflected_axes = [chart_vector(source[4 * j:4 * j + 3]) for j in range(3)]
    linear = [[dot(row, column) for column in reflected_axes] for row in inverse]
    offset = add(tuple(dot(row, reflected_offset) for row in inverse), translation)
    return linear, offset


def _fit_weight(x, definition):
    start, end = definition['grip_fits']['wrist_blend']
    t = min(max((x - start) / (end - start), 0.), 1.)
    return t * t * (3 - 2 * t), 6 * t * (1 - t) / (end - start)


def fit_point(point, role, car_id, source_anchor, definition=None):
    """Return a fitted canonical point for read-only native grip diagnostics.

    Input/output remove the source Audi translation/right-hand reflection;
    source_anchor is mesh3 vertex0, as recorded by the native export fixture.
    This pure accessor uses the exact same wrist blend as generated vertices.
    """
    definition = definition or json.loads(DEFINITION.read_text(encoding='utf-8'))
    if 'grip_fits' not in definition:
        return tuple(point)
    role = {0: 'left-hand', 1: 'right-hand'}.get(role, role)
    group = _fit_group(role, car_id)
    if group == 'left-source':
        return tuple(point)
    sign = -1
    offset = sub(source_anchor, (definition['frame_anchor'][0], definition['frame_anchor'][1], sign * definition['frame_anchor'][2]))
    source_point = add((point[0], point[1], sign * point[2]), offset)
    linear, translation = _grip_affine(definition, group)
    fitted = add(tuple(dot(row, point) for row in linear), translation)
    weight, _ = _fit_weight(point[0], definition)
    result = sub(interpolate(source_point, fitted, weight), offset)
    return result[0], result[1], sign * result[2]


def thumb_path_for_source(role, source_anchor, definition=None):
    """Return the authored thumb controls, including the source Audi fit.

    The fixed root stays in the palm. Distal stations cancel only the original
    left-source X translation, closing the glove on the same actual rim. Curve
    evaluation and normals therefore follow the adjusted path naturally.
    Diagnostics apply fit_point to these controls, as for the other digits.
    """
    definition = definition or json.loads(DEFINITION.read_text(encoding='utf-8'))
    role = {0: 'left-hand', 1: 'right-hand'}.get(role, role)
    path = definition['thumb_path']
    if role != 'left-hand' or 'thumb_source_fit' not in definition:
        return [tuple(point) for point in path]
    correction = definition['frame_anchor'][0] - source_anchor[0]
    weights = definition['thumb_source_fit']['left_x_station_weights']
    return [(point[0] + correction * weight, *point[1:]) for point, weight in zip(path, weights)]


def _fit_meshes(meshes, definition, group, source_offset):
    if group == 'left-source':
        return
    linear, translation = _grip_affine(definition, group)
    # Convert the canonical fit affine to original right-hand mesh coordinates.
    linear = [[row[0], row[1], -row[2]] for row in linear]
    translation = sub(translation, tuple(dot(row, source_offset) for row in linear))
    for mesh in meshes:
        for i, vertex in enumerate(mesh.vertices):
            point, normal = vertex[:3], vertex[5:]
            x = point[0] - source_offset[0]
            weight, derivative = _fit_weight(x, definition)
            if weight == 0:
                continue
            fitted = add(tuple(dot(row, point) for row in linear), translation)
            delta = sub(fitted, point)
            # Inverse-transpose of the smooth wrist deformation's Jacobian;
            # its cofactor form also handles the varying blend derivative.
            jacobian = [[(1. if a == b else 0.) + weight * (linear[a][b] - (1. if a == b else 0.)) +
                         (derivative * delta[a] if b == 0 else 0.) for b in range(3)] for a in range(3)]
            columns = [tuple(row[a] for row in jacobian) for a in range(3)]
            cofactors = (cross(columns[1], columns[2]), cross(columns[2], columns[0]), cross(columns[0], columns[1]))
            adjusted_normal = unit(tuple(sum(normal[b] * cofactors[b][a] for b in range(3)) for a in range(3)))
            mesh.vertices[i] = (*interpolate(point, fitted, weight), *vertex[3:5], *adjusted_normal)
        # A curved/tapered pad can contain a very shallow triangle. Its fixed16
        # rounding changes after fitting, so repeat Mesh.triangle's winding
        # decision in the final encoded coordinate system, not the pre-fit one.
        for i, face in enumerate(mesh.faces):
            indices = struct.unpack_from('<3H', face, 2)
            vertices = [mesh.vertices[j] for j in indices]
            positions = [tuple(round(value * 65536) for value in v[:3]) for v in vertices]
            normal = tuple(sum(v[5 + a] for v in vertices) for a in range(3))
            if dot(cross(sub(positions[1], positions[0]), sub(positions[2], positions[0])), normal) < 0:
                mesh.faces[i] = face[:2] + struct.pack('<3H', indices[0], indices[2], indices[1]) + face[8:]


def _closest(point, triangle):
    """Closest triangle point and barycentrics (including edges/vertices)."""
    a, b, c = triangle
    ab, ac, ap = sub(b, a), sub(c, a), sub(point, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        return a, (1., 0., 0.)
    bp = sub(point, b)
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return b, (0., 1., 0.)
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return add(a, scale(ab, v)), (1 - v, v, 0.)
    cp = sub(point, c)
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return c, (0., 0., 1.)
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6)
        return add(a, scale(ac, w)), (1 - w, 0., w)
    va = d3 * d6 - d5 * d4
    if va <= 0 and d4 - d3 >= 0 and d5 - d6 >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return add(b, scale(sub(c, b), w)), (0., 1 - w, w)
    v, w = vb / (va + vb + vc), vc / (va + vb + vc)
    return add(add(a, scale(ab, v)), scale(ac, w)), (1 - v - w, v, w)


def _catmull(values, t):
    """Interpolating cubic; station coordinates remain source measurements."""
    t = min(max(t, 0.), len(values) - 1.)
    i = min(int(t), len(values) - 2)
    f = t - i
    p = [values[min(max(j, 0), len(values) - 1)] for j in (i - 1, i, i + 1, i + 2)]
    return tuple(.5 * (2 * b + (-a + c) * f + (2 * a - 5 * b + 4 * c - d) * f * f +
                       (-a + 3 * b - 3 * c + d) * f * f * f) for a, b, c, d in zip(*p))


def _projection(mesh, count=None):
    """Recover the original planar UV projection rather than refitting a decal.

    The main GLOVE01 back panel is its first17 source vertices; its remaining
    tip triangles have a separate projection in that same material slot.
    """
    vertices = mesh.vertices[:count] if count else mesh.vertices
    rows = [(*v[:3], 1.) for v in vertices]
    result = []
    for uv_axis in (3, 4):
        matrix = [[sum(row[i] * row[j] for row in rows) for j in range(4)] +
                  [sum(row[i] * vertex[uv_axis] for row, vertex in zip(rows, vertices))] for i in range(4)]
        for i in range(4):
            pivot = max(range(i, 4), key=lambda j: abs(matrix[j][i]))
            matrix[i], matrix[pivot] = matrix[pivot], matrix[i]
            denominator = matrix[i][i]
            if abs(denominator) < 1e-14:
                raise ValueError('Canonical glove chart has no stable projection')
            matrix[i] = [value / denominator for value in matrix[i]]
            for j in range(4):
                if j != i:
                    amount = matrix[j][i]
                    matrix[j] = [a - amount * b for a, b in zip(matrix[j], matrix[i])]
        result.append(tuple(matrix[i][4] for i in range(4)))
    return result


class _Surface:
    """Continuous original UV projections and coherent sewn panel domains."""

    def __init__(self, originals, definition):
        self.meshes = [Mesh(m.header, [], []) for m in originals]
        self.charts = []
        self.uv_cache = {}
        self.projections = [_projection(m, 17 if index == 3 else None) for index, m in enumerate(originals)]
        self.uv_bounds = [[(min(v[a] for v in m.vertices), max(v[a] for v in m.vertices)) for a in (3, 4)] for m in originals]
        self.tip_chart = definition['little_tip_chart']
        self.tip_frame = None
        self.uv_bounds[2] = [self.tip_chart['u'], self.tip_chart['v']]
        for index, mesh in enumerate(originals):
            for face in mesh.faces:
                vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
                points = [v[:3] for v in vertices]
                self.charts.append((index, points, [v[3:5] for v in vertices],
                                    unit(cross(sub(points[1], points[0]), sub(points[2], points[0]))),
                                    struct.unpack_from('<h', face, 8)[0]))
        self.parts = {}

    def uv(self, index, point, normal):
        key = index, point
        if key not in self.uv_cache:
            if index == 2:
                origin, side, up = self.tip_frame
                delta = sub(point, origin)
                uv = ((sum(self.tip_chart['u']) / 2) + dot(delta, side) * self.tip_chart['scale'],
                      (sum(self.tip_chart['v']) / 2) + dot(delta, up) * self.tip_chart['scale'])
            elif index == 0 and self.meshes[0].header.startswith(b'LEFT'):
                # Left ARM has an original cylindrical seam; its source chart
                # stays continuous inside each original patch. Compute once per
                # position so adjacent new triangles cannot disagree at a join.
                charts = [c for c in self.charts if c[0] == index]

                def metric(chart):
                    p, _ = _closest(point, chart[1])
                    return dot(sub(point, p), sub(point, p)) + .0001 * (1 - dot(normal, chart[3]))

                chart = min(charts, key=metric)
                _, weights = _closest(point, chart[1])
                uv = tuple(sum(w * v[a] for w, v in zip(weights, chart[2])) for a in range(2))
            else:
                uv = tuple(dot((*point, 1.), coefficients) for coefficients in self.projections[index])
            self.uv_cache[key] = tuple(min(max(value, lo), hi) for value, (lo, hi) in zip(uv, self.uv_bounds[index]))
        return self.uv_cache[key]

    def triangle(self, vertices, part, index):
        positions = [v[0] for v in vertices]
        # Reject only after the same fixed16 quantization as CM3D encoding.
        quantized = [tuple(round(x * 65536) for x in p) for p in positions]
        area = cross(sub(quantized[1], quantized[0]), sub(quantized[2], quantized[0]))
        if dot(area, area) == 0:
            return
        mesh = self.meshes[index]
        indices = []
        for point, vertex_normal in vertices:
            indices.append(mesh.vertex(point, vertex_normal, self.uv(index, point, vertex_normal)))
        texture = next(c[4] for c in self.charts if c[0] == index)
        mesh.triangle(tuple(indices), WHITE, texture)
        self.parts[part] = self.parts.get(part, 0) + 1

    def grid(self, rings, part, caps=False, panel=None):
        for i, (first, second) in enumerate(zip(rings, rings[1:])):
            for j in range(len(first)):
                k = (j + 1) % len(first)
                index = panel(i, j)
                self.triangle([first[j], second[j], second[k]], part, index)
                self.triangle([first[j], second[k], first[k]], part, index)
        if caps:
            ends = ((rings[0], -1), (rings[-1], 1))
            if isinstance(caps, tuple):
                ends = [end for end, include in zip(ends, caps) if include]
            for ring, sign in ends:
                center = tuple(sum(v[0][a] for v in ring) / len(ring) for a in range(3))
                normal = unit(scale(sub(rings[-1][0][0], rings[0][0][0]), sign))
                for j in range(len(ring)):
                    index = panel(0 if sign < 0 else len(rings) - 2, j)
                    if part == 'finger-1' and sign > 0:
                        index = 2  # original small fingertip GLOVE01 chart
                    self.triangle([(center, normal), ring[j], ring[(j + 1) % len(ring)]], part, index)


def _section(triangles, x, angles):
    """A rounded radial section inside the source's cross-section polygon."""
    segments = []
    for triangle in triangles:
        intersections = []
        for a, b in zip(triangle, triangle[1:] + triangle[:1]):
            if (a[0] <= x < b[0]) or (b[0] <= x < a[0]):
                p = interpolate(a, b, (x - a[0]) / (b[0] - a[0]))
                intersections.append(p[1:3])
        if len(intersections) == 2:
            segments.append(intersections)
    if not segments:
        raise ValueError('Canonical hand has no cross section at the requested station')
    points = [p for segment in segments for p in segment]
    # UV/material seams in the source are separate patches, some displaced by
    # a few fixed16 units. Their convex section closes those cracks; the new
    # opposed thumb supplies the glove's concave grasp separately.
    ordered = sorted(set(points))

    def turn(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    halves = []
    for sequence in (ordered, list(reversed(ordered))):
        half = []
        for point in sequence:
            while len(half) > 1 and turn(half[-2], half[-1], point) <= 0:
                half.pop()
            half.append(point)
        halves.append(half[:-1])
    hull = halves[0] + halves[1]
    segments = list(zip(hull, hull[1:] + hull[:1]))
    center = tuple((min(p[a] for p in points) + max(p[a] for p in points)) / 2 for a in range(2))
    radii = []
    sides = len(angles)
    for angle in angles:
        direction = (math.cos(angle), math.sin(angle))
        hits = []
        for a, b in segments:
            delta, offset = sub(b, a), sub(a, center)
            determinant = direction[0] * delta[1] - direction[1] * delta[0]
            if abs(determinant) < 1e-12:
                continue
            distance = (offset[0] * delta[1] - offset[1] * delta[0]) / determinant
            t = (offset[0] * direction[1] - offset[1] * direction[0]) / determinant
            if distance > 0 and -.00001 <= t <= 1.00001:
                hits.append(distance)
        if not hits:
            raise ValueError('Canonical hand cross section is not star shaped')
        radii.append(max(hits))
    # A circular five-tap filter rounds the old polygon corners instead of
    # reproducing their facets at a higher tessellation. Preserve radial size.
    smooth = [sum(radii[(i + k) % sides] * w for k, w in
                  ((-2, .0625), (-1, .25), (0, .375), (1, .25), (2, .0625))) for i in range(sides)]
    return center, smooth


def _shoulder(triangles, sides):
    """Keep the original eight corners of the slanted open shoulder exactly."""
    edges = Counter(tuple(sorted((a, b))) for tri in triangles for a, b in zip(tri, tri[1:] + tri[:1]))
    points = set(p for (a, b), count in edges.items() if count == 1 and max(a[0], b[0]) < -.43 for p in (a, b))
    if len(points) != 8:
        raise ValueError('Expected the canonical eight-corner shoulder boundary')
    cy, cz = (sum(p[a] for p in points) / len(points) for a in (1, 2))
    ordered = sorted(points, key=lambda p: math.atan2(p[2] - cz, p[1] - cy))
    corners = [math.atan2(p[2] - cz, p[1] - cy) for p in ordered]
    rings, angles = [], []
    steps = sides // len(ordered)
    for i, point in enumerate(ordered):
        controls = [ordered[(i + k) % len(ordered)] for k in (-1, 0, 1, 2)]
        lo, hi = corners[i], corners[(i + 1) % len(ordered)]
        if hi <= lo:
            hi += math.tau
        for j in range(steps):
            rings.append(_catmull(controls, 1 + j / steps))
            angles.append(lo + (hi - lo) * j / steps)
    return rings, angles


def _loft(surface, originals, transform, inverse, definition):
    triangles = [[inverse(m.vertices[i][:3]) for i in struct.unpack_from('<3H', f, 2)]
                 for m in originals for f in m.faces]
    sides = definition['loft_cross_segments']
    shoulder, angles = _shoulder(triangles, sides)
    stations = definition['loft_stations']
    sections = [_section(triangles, x, angles) for x in stations]
    controls = [(x, *center, *radii) for x, (center, radii) in zip(stations, sections)]
    rings = [[(transform(point), None) for point in shoulder]]
    count = (len(stations) - 1) * definition['loft_steps_per_station']
    for i in range(count + 1):
        t = i * (len(stations) - 1) / count
        x, cy, cz, *radii = _catmull(controls, t)
        # Small sewn-fabric folds are actual radial geometry on the sleeve;
        # the glove's wrist gets a raised cuff and two shallow piping seams.
        fold = (.0015 * math.sin((x + .49) * 125) * math.sin(math.pi * i / count) ** 2
                if x < -.18 else 0.)
        cuff = .0028 * math.exp(-((x + .159) / .010) ** 2)
        cuff += .0013 * math.exp(-((x + .145) / .003) ** 2)
        ring = []
        for j, radius in enumerate(radii):
            angle = angles[j]
            radius += fold + cuff
            point = transform((x, cy + radius * math.cos(angle), cz + radius * math.sin(angle)))
            ring.append((point, None))
        rings.append(ring)
    # The source mitten's full-width distal cross section must not become a
    # visible disk inside the grip. Keep the dorsal width beneath all four
    # roots while thinning/lifting the palmar side into a shallow padded web.
    # The final ring collapses to a pole; grid() discards its zero-area half
    # cells after quantization, leaving a closed curved surface without a fan.
    x, cy, cz, *radii = controls[-1]
    closure = definition['palm_closure']
    for i in range(1, closure['steps'] + 1):
        angle = math.pi * i / (2 * closure['steps'])
        shrink = math.cos(angle) if i < closure['steps'] else 0.
        center = (x + closure['length'] * math.sin(angle), cy * shrink,
                  cz + (closure['dorsal_z'] - cz) * math.sin(angle) ** 2)
        width = shrink ** closure['width_taper']
        depth = shrink ** closure['depth_taper']
        rings.append([(transform((center[0], center[1] + radius * width * math.cos(turn),
                                  center[2] + radius * depth * math.sin(turn))), None)
                      for radius, turn in zip(radii, angles)])
    # Normals follow both the radial curvature and the taper, including cuff
    # ridges. Their mirrored counterpart is handled by the original hand basis.
    count = len(rings) - 1
    for i, ring in enumerate(rings):
        for j, (point, _) in enumerate(ring):
            if i == count:
                ring[j] = (point, transform((1., 0., 0.), vector=True))
                continue
            along = sub(rings[min(i + 1, count)][j][0], rings[max(i - 1, 0)][j][0])
            around = sub(ring[(j + 1) % sides][0], ring[(j - 1) % sides][0])
            normal = unit(cross(around, along))
            radial = transform((0, math.cos(angles[j]), math.sin(angles[j])), vector=True)
            if dot(normal, radial) < 0:
                normal = scale(normal, -1)
            ring[j] = (point, normal)
    def panel(i, j):
        center_x = (inverse(rings[i][j][0])[0] + inverse(rings[i + 1][j][0])[0]) / 2
        if center_x < -.17:
            return 0
        angle = (angles[j] + (angles[(j + 1) % sides] if j + 1 < sides else angles[0] + math.tau)) / 2
        if math.sin(angle) > .35:
            return 3  # original broad dorsal logo panel
        return 5 if math.cos(angle) > .15 else 4  # upper cuff / palm panels

    surface.grid(rings, 'sleeve-palm-cuff', panel=panel)
    return rings


def _digit(surface, path, radii, transform, part, definition, width_scale=1., depth_scale=1.):
    sides = definition['thumb_cross_segments'] if part == 'thumb' else definition['finger_cross_segments']
    steps = definition['finger_path_steps']
    rings = []
    for i in range(steps + 1):
        t = i / steps * (len(path) - 1)
        center = _catmull(path, t)
        tangent = unit(sub(_catmull(path, min(t + .01, len(path) - 1)), _catmull(path, max(t - .01, 0))))
        # Stable transverse basis; fingers bend in X/Z, the thumb additionally
        # opposes across Y. Each has a real oval section, never a flat decal.
        side = unit(cross(tangent, (0., 0., 1.) if part == 'thumb' else (0., 1., 0.)))
        up = unit(cross(tangent, side))
        radius = _catmull([(r,) for r in radii], t)[0]
        ring = []
        for j in range(sides):
            angle = math.tau * j / sides
            normal = unit(add(scale(side, math.cos(angle) / depth_scale), scale(up, math.sin(angle) / width_scale)))
            point = add(center, add(scale(side, radius * depth_scale * math.cos(angle)),
                                    scale(up, radius * width_scale * math.sin(angle))))
            ring.append((transform(point), transform(normal, vector=True)))
        rings.append(ring)
    # Distal pads end in a rounded ellipsoid instead of a flat circular cap.
    # Its final pole follows the last phalanx's tangent, including the index's
    # downward/behind-rim direction. Keep the root cap buried in the palm.
    tip_steps = definition['fingertip_steps']
    tip_length = radii[-1] * definition['fingertip_length_scale']
    if part == 'finger-1':
        surface.tip_frame = transform(center), transform(side, vector=True), transform(up, vector=True)
    for i in range(1, tip_steps + 1):
        bend = math.pi * i / (2 * tip_steps)
        roundness = math.cos(bend) if i < tip_steps else 0.
        tip_center = add(center, scale(tangent, tip_length * math.sin(bend)))
        ring = []
        for j in range(sides):
            angle = math.tau * j / sides
            point = add(tip_center, add(scale(side, radius * depth_scale * roundness * math.cos(angle)),
                                       scale(up, radius * width_scale * roundness * math.sin(angle))))
            normal = unit(add(add(scale(side, roundness * math.cos(angle) / depth_scale),
                                  scale(up, roundness * math.sin(angle) / width_scale)),
                              scale(tangent, math.sin(bend) / definition['fingertip_length_scale'])))
            ring.append((transform(point), transform(normal, vector=True)))
        rings.append(ring)
    def panel(i, j):
        if part == 'thumb':
            return 1
        if part == 'finger-1' and i >= steps and math.cos(math.tau * (j + .5) / sides) > 0:
            return 2  # original little-finger tip chart, on its rounded pad
        # Outer and inner half-tubes meet at a longitudinal sewn seam. The
        # same source projection continues across all four dorsal fingers.
        return 3 if math.cos(math.tau * (j + .5) / sides) > 0 else 1

    surface.grid(rings, part, caps=(True, False), panel=panel)
    return rings


@lru_cache(maxsize=6)
def _build(geometry_section, role, fit_group):
    # This cache shares only generated immutable geometry, never animation or
    # per-car material bytes. The eight proven identical cages are built once.
    definition = json.loads(DEFINITION.read_text(encoding='utf-8'))
    meshes = []
    offset = 0
    while offset < len(geometry_section):
        size, vc, fc = struct.unpack_from('<IHH', geometry_section, offset + 12)
        vertices = [tuple(x / 65536 for x in struct.unpack_from('<8i', geometry_section, offset + 48 + n * 32)) for n in range(vc)]
        faces_at = offset + 48 + vc * 32
        meshes.append(Mesh(geometry_section[offset:offset + 48], vertices,
                           [geometry_section[faces_at + n * 24:faces_at + (n + 1) * 24] for n in range(fc)]))
        offset += size
    # Mesh3's bottom grip corner is present in both hands and in the Audi
    # exception. Derive translation/reflection from that actual source point.
    anchor = meshes[2].vertices[0][:3]
    canonical = definition['frame_anchor']
    sign = 1 if role == 'left-hand' else -1
    translation = sub(anchor, (canonical[0], canonical[1], sign * canonical[2]))

    def transform(p, vector=False):
        value = (p[0], p[1], sign * p[2])
        return value if vector else add(value, translation)

    def inverse(p):
        p = sub(p, translation)
        return p[0], p[1], sign * p[2]

    surface = _Surface(meshes, definition)
    _loft(surface, meshes, transform, inverse, definition)
    for i, digit in enumerate(definition['fingers']):
        curve = digit['path']
        path = [(x, digit['y'] * spread + drift, z) for (x, drift, z), spread in
                zip(curve, definition['finger_spread'])]
        _digit(surface, path, [r * digit['radius_scale'] for r in digit['radii']],
               transform, f'finger-{i + 1}', definition, digit['width_scale'], digit['depth_scale'])
    _digit(surface, thumb_path_for_source(role, anchor, definition), definition['thumb_radii'], transform, 'thumb', definition,
           depth_scale=definition['thumb_depth_scale'])
    _fit_meshes(surface.meshes, definition, fit_group, translation)
    encoded = b''.join(mesh.encode() for mesh in surface.meshes)
    return surface.meshes, dict(surface.parts), crc32(encoded)


def generate(source_bytes, role, texture_directory=None, *, car_id):
    """Return newly shaped CM3D bytes and metrics, preserving source bindings."""
    role = {0: 'left-hand', 1: 'right-hand'}.get(role, role)
    if role not in ('left-hand', 'right-hand'):
        raise ValueError('Hand geometry role must be left-hand or right-hand')
    fit_group = _fit_group(role, car_id)
    definition = json.loads(DEFINITION.read_text(encoding='utf-8'))
    header, nodes, originals, textures = decode(source_bytes)
    mesh_start, texture_start = struct.unpack_from('<II', header, 24)
    section = source_bytes[mesh_start:texture_start]
    source_geometry_crc = crc32(section)
    if (source_geometry_crc not in definition['source_geometry_crc32'][role] or
            crc32(nodes) != definition['source_nodes_crc32'] or len(originals) != 6 or
            struct.unpack_from('<H', header, 16)[0] != 4):
        raise ValueError('Hand geometry requires a canonical1.1a six-mesh/four-material member')
    meshes, parts, geometry_crc = _build(section, role, fit_group)
    if texture_directory is not None:
        import cockpit_hand_textures
        textures = cockpit_hand_textures.replace_textures(textures, 4, texture_directory)
    output = pack(header, nodes, meshes, textures)
    return output, {'role': role, 'car_id': car_id, 'hand_fit_group': fit_group, 'source_model_crc32': crc32(source_bytes),
                    'source_geometry_crc32': source_geometry_crc, 'geometry_crc32': geometry_crc,
                    'output_crc32': crc32(output), 'bytes': len(output),
                    'vertices': sum(len(m.vertices) for m in meshes),
                    'stored_triangles': sum(len(m.faces) for m in meshes),
                    'original_visible_triangles': visible_triangles(nodes, originals),
                    'visible_triangles': visible_triangles(nodes, meshes),
                    'original_vertices': sum(len(m.vertices) for m in originals),
                    'parts': parts, 'fingers': 4, 'thumbs': 1, 'material_slots': 4, 'mesh_slots': 6}
