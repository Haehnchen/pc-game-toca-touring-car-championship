"""Volumetric HOR aircraft and balloons in their original local coordinates.

Only mesh contents change. The caller retains every original node, mesh slot,
animation sibling and material name. New surfaces project into the original
material UV islands; source artwork remains the paint specification.
"""

import math
import struct

import vehicle_model as g

WHITE = (255, 255, 255, 255)
RUBBER = (18, 19, 19, 255)
STEEL = (118, 121, 120, 255)
ROPE = (138, 129, 107, 255)
_CACHE = {}
MARK_ISLAND = (.355, .705, -.48, -.18)


def extent(mesh):
    return [(min(v[i] for v in mesh.vertices), max(v[i] for v in mesh.vertices)) for i in range(3)]


def clip_uv(polygon, axis, bound, greater):
    """Clip complete position/UV/normal records against one UV half-plane."""
    if not polygon:
        return []
    sign = 1 if greater else -1
    result = []
    previous = polygon[-1]
    before = (previous[axis] - bound) * sign
    for current in polygon:
        after = (current[axis] - bound) * sign
        if (before >= 0) != (after >= 0):
            fraction = before / (before - after)
            intersection = [a + (b - a) * fraction for a, b in zip(previous, current)]
            intersection[axis] = bound
            result.append(tuple(intersection))
        if after >= 0:
            result.append(current)
        previous, before = current, after
    clean = []
    for record in result:
        if not clean or any(abs(a - b) > 1e-12 for a, b in zip(record, clean[-1])):
            clean.append(record)
    if len(clean) > 1 and all(abs(a - b) <= 1e-12 for a, b in zip(clean[0], clean[-1])):
        clean.pop()
    return clean


def lettering_partition(polygon):
    """Disjoint original-UV outside polygons plus one complete mirror island."""
    u0, u1, v0, v1 = MARK_ISLAND
    outside = []
    inside = polygon
    for axis, bound, greater in ((3, u0, True), (3, u1, False), (4, v0, True), (4, v1, False)):
        if not inside:
            break
        sign = 1 if greater else -1
        distances = [(v[axis] - bound) * sign for v in inside]
        if min(distances) >= 0:
            continue
        if max(distances) <= 0:
            outside.append(inside)
            inside = []
            break
        fragment = clip_uv(inside, axis, bound, not greater)
        if len(fragment) >= 3:
            outside.append(fragment)
        inside = clip_uv(inside, axis, bound, greater)
    return outside, inside


def painted_polygon(mesh, polygon, texture, color, mirror=False):
    for index in range(1, len(polygon) - 1):
        records = (polygon[0], polygon[index], polygon[index + 1])
        normal = g.cross(g.sub(records[1][:3], records[0][:3]), g.sub(records[2][:3], records[0][:3]))
        if g.dot(normal, normal) <= 1e-20:
            continue
        indices = []
        for record in records:
            u = MARK_ISLAND[0] + MARK_ISLAND[1] - record[3] if mirror else record[3]
            indices.append(mesh.vertex(record[:3], record[5:], (u, record[4])))
        mesh.triangle(indices, color, texture)


class Paint:
    """Closest original triangle supplies material and barycentric UVs."""
    def __init__(self, source, names, readable_mark=False):
        self.names = names
        self.readable_mark = readable_mark
        self.faces = []
        for face in source.faces:
            vertices = [source.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            a, b, c = [v[:3] for v in vertices]
            ab, ac = g.sub(b, a), g.sub(c, a)
            aa, cc, cross = g.dot(ab, ab), g.dot(ac, ac), g.dot(ab, ac)
            denominator = aa * cc - cross * cross
            if denominator <= 1e-15:
                continue
            texture = struct.unpack_from('<h', face, 8)[0]
            self.faces.append((vertices, texture, tuple(face[10:14]), ab, ac, aa, cc, cross, denominator,
                               g.unit(g.cross(ab, ac))))
        bounds = extent(source)
        self.penalty = sum((b - a) ** 2 for a, b in bounds) * .0005

    @staticmethod
    def weights(face, point, clamp=True):
        vertices, _, _, ab, ac, aa, cc, cross, denominator, _ = face
        delta = g.sub(point, vertices[0][:3])
        d, e = g.dot(delta, ab), g.dot(delta, ac)
        u, v = (cc * d - cross * e) / denominator, (aa * e - cross * d) / denominator
        if not clamp:
            return 1 - u - v, u, v
        weights = [max(0., 1 - u - v), max(0., u), max(0., v)]
        total = sum(weights)
        return [value / total for value in weights]

    def choose(self, point, normal, allowed=()):
        candidates = self.faces
        if allowed:
            selected = [f for f in candidates if f[1] >= 0 and self.names[f[1]] in allowed]
            candidates = selected or candidates
        def score(face):
            weights = self.weights(face, point)
            closest = tuple(sum(weights[j] * face[0][j][i] for j in range(3)) for i in range(3))
            return g.dot(g.sub(point, closest), g.sub(point, closest)) + self.penalty * (1 - g.dot(normal, face[9]))
        return min(candidates, key=score)

    def triangle(self, mesh, points, normals, allowed=()):
        center = tuple(sum(p[i] for p in points) / 3 for i in range(3))
        normal = g.unit(tuple(sum(n[i] for n in normals) for i in range(3)))
        face = self.choose(center, normal, allowed)
        polygon = []
        for point, direction in zip(points, normals):
            weights = self.weights(face, point, clamp=False)
            uv = tuple(max(min(v[i] for v in face[0]), min(max(v[i] for v in face[0]),
                       sum(weights[j] * face[0][j][i] for j in range(3)))) for i in (3, 4))
            polygon.append((*point, *uv, *direction))
        if self.readable_mark and face[1] >= 0 and self.names[face[1]] == 'MARKAM' and normal[0] > .3:
            # Split before changing UVs. Per-corner rectangle decisions warp
            # crossing triangles and repeat edge letters across the whole face.
            outside, inside = lettering_partition(polygon)
            for fragment in outside:
                painted_polygon(mesh, fragment, face[1], face[2])
            painted_polygon(mesh, inside, face[1], face[2], mirror=True)
        else:
            painted_polygon(mesh, polygon, face[1], face[2])


class OpaqueSkin:
    """Sample actual opaque painted metal, excluding photographic background.

    A flat cutout photograph cannot be projected over a volumetric aircraft:
    its alpha silhouette would cut holes through wings and the fuselage. Small
    original-faithful metal samples provide a continuous skin instead.
    UV rectangles are inset into opaque pixels of the authored source image.
    """
    def __init__(self, names, material, rectangle):
        self.texture = names.index(material)
        self.rectangle = rectangle

    def triangle(self, mesh, points, normals, allowed=()):
        u0, v0, u1, v1 = self.rectangle
        indices = []
        for point, normal in zip(points, normals):
            along = max(0., min(1., (point[1] + 2.393) / 8.))
            height = max(0., min(1., (point[2] + 12.) / 4.))
            indices.append(mesh.vertex(point, normal, (u0 + (u1 - u0) * along, v0 + (v1 - v0) * height)))
        mesh.triangle(indices, WHITE, self.texture)


HELI_TAIL_SKIN = (336 / 512, 68 / 128 - 1, 344 / 512, 72 / 128 - 1)


class PSPAtlas:
    """Explicit UVs for the opaque512x256 original-grey jet material atlas.

    Top: full fuselage side, nose at right. Bottom left: wing/fin panels.
    Bottom right: engine skin. Inset region edges protect linear filtering.
    """
    def __init__(self, names, region, sections=(), wing_points=()):
        self.texture = names.index('MegaPSPSkin')
        self.region, self.sections, self.wing_points = region, sections, wing_points

    @staticmethod
    def clamp(value):
        return max(0., min(1., value))

    def uv(self, point, normal):
        if self.region == 'body':
            u = self.clamp((5.607 - point[1]) / 8.)
            before, after = self.sections[0], self.sections[-1]
            for a, b in zip(self.sections, self.sections[1:]):
                if a[0] <= point[1] <= b[0]:
                    before, after = a, b
                    break
            fraction = self.clamp((point[1] - before[0]) / (after[0] - before[0]))
            center = before[2] + (after[2] - before[2]) * fraction
            radius = before[4] + (after[4] - before[4]) * fraction
            height = self.clamp(.5 + (point[2] - center) / (2 * radius))
            return u, -.98 + .46 * height
        if self.region == 'wing':
            root_lead, root_trail, tip_lead, tip_trail = self.wing_points
            span = self.clamp((point[0] - root_lead[0]) / (tip_lead[0] - root_lead[0]))
            lead, trail = g.interpolate(root_lead, tip_lead, span), g.interpolate(root_trail, tip_trail, span)
            chord = self.clamp((point[1] - lead[1]) / (trail[1] - lead[1]))
            return .02 + .46 * span, -.48 + .46 * chord
        if self.region == 'engine':
            along = self.clamp((point[1] - 1.2) / 1.3)
            around = (math.atan2(normal[2], normal[0]) + math.pi) / math.tau
            return .52 + .46 * along, -.48 + .46 * around
        # The vertical stabilizer uses the same understated wing panel stock.
        return .02 + .46 * self.clamp((point[1] - 4.) / 1.607), -.48 + .46 * self.clamp((point[2] + 12.) / 2.15)

    def triangle(self, mesh, points, normals, allowed=()):
        indices = [mesh.vertex(point, normal, self.uv(point, normal)) for point, normal in zip(points, normals)]
        mesh.triangle(indices, WHITE, self.texture)


def triangle(mesh, paint, points, normals=None, allowed=(), color=None):
    normals = normals or [g.unit(g.cross(g.sub(points[1], points[0]), g.sub(points[2], points[0])))] * 3
    if color is None:
        paint.triangle(mesh, points, normals, allowed)
    else:
        indices = [mesh.vertex(point, normal) for point, normal in zip(points, normals)]
        mesh.triangle(indices, color)


def quad(mesh, paint, points, normals=None, allowed=(), color=None):
    if normals is None:
        normals = [g.unit(g.cross(g.sub(points[1], points[0]), g.sub(points[2], points[0])))] * 4
    for corners in ((0, 1, 2), (0, 2, 3)):
        triangle(mesh, paint, [points[i] for i in corners], [normals[i] for i in corners], allowed, color)


def profile_samples(sections, steps=3):
    """Shape-preserving smoothstep interpolation; no overshoot of source bounds."""
    rows = []
    for a, b in zip(sections, sections[1:]):
        for step in range(steps):
            t = step / steps
            t = t * t * (3 - 2 * t)
            rows.append(tuple(x + (y - x) * t for x, y in zip(a, b)))
    return rows + [sections[-1]]


def loft(mesh, paint, sections, axis=1, segments=24, steps=3, exponent=1., allowed=()):
    """Sections: longitudinal position, two centers, two radii.

    Exponent below1 gives the Audi advertising balloon's rounded car-shaped
    cross section instead of changing it into an ordinary spherical balloon.
    """
    rows = profile_samples(sections, steps)
    axes = [i for i in range(3) if i != axis]
    points, centers = [], []
    for position, c1, c2, r1, r2 in rows:
        center = [0., 0., 0.]
        center[axis], center[axes[0]], center[axes[1]] = position, c1, c2
        centers.append(tuple(center))
        ring = []
        for index in range(segments):
            angle = math.tau * index / segments
            c, s = math.cos(angle), math.sin(angle)
            point = list(center)
            point[axes[0]] += r1 * math.copysign(abs(c) ** exponent, c)
            point[axes[1]] += r2 * math.copysign(abs(s) ** exponent, s)
            ring.append(tuple(point))
        points.append(ring)
    normals = []
    for row, ring in enumerate(points):
        directions = []
        for index, point in enumerate(ring):
            across = g.sub(ring[(index + 1) % segments], ring[(index - 1) % segments])
            along = g.sub(points[min(row + 1, len(rows) - 1)][index], points[max(0, row - 1)][index])
            normal = g.unit(g.cross(across, along))
            if g.dot(normal, g.sub(point, centers[row])) < 0:
                normal = g.mul(normal, -1)
            directions.append(normal)
        normals.append(directions)
    for row in range(len(rows) - 1):
        for index in range(segments):
            next_index = (index + 1) % segments
            quad(mesh, paint, (points[row][index], points[row][next_index], points[row + 1][next_index], points[row + 1][index]),
                 (normals[row][index], normals[row][next_index], normals[row + 1][next_index], normals[row + 1][index]), allowed)
    for row, sign in ((0, -1), (len(rows) - 1, 1)):
        normal = tuple(sign if i == axis else 0 for i in range(3))
        for index in range(segments):
            triangle(mesh, paint, (centers[row], points[row][index], points[row][(index + 1) % segments]), [normal] * 3, allowed)


def cylinder(mesh, start, end, radius, color, segments=12):
    g.cylinder(mesh, start, end, radius, color, segments)


def wing(mesh, paint, root_lead, root_trail, tip_lead, tip_trail, thickness, allowed=(), attachment_x=None):
    """Closed airfoil with rounded leading edge and a sharp trailing edge.

    The authored root station preserves the exposed planform, but its X value
    alone cannot describe a joint to a rounded fuselage or a thin tail fin.
    Continue that same airfoil inward to the supporting body's centerline;
    the overlapping solids then meet even where the curved body is narrow.
    """
    chord = (0., .035, .12, .3, .6, .85, 1.)
    rings = []
    for span in range(6):
        t = span / 5
        lead, trail = g.interpolate(root_lead, tip_lead, t), g.interpolate(root_trail, tip_trail, t)
        ring = []
        for side in (-1, 1):
            for fraction in chord if side == -1 else reversed(chord[1:-1]):
                point = list(g.interpolate(lead, trail, fraction))
                point[2] += side * thickness * math.sin(math.pi * fraction) ** .65 * (1 - .55 * t)
                ring.append(tuple(point))
        rings.append(ring)
    if attachment_x is not None:
        rings.insert(0, [(attachment_x, point[1], point[2]) for point in rings[0]])
    for row in range(len(rings) - 1):
        for index in range(len(rings[0])):
            nxt = (index + 1) % len(rings[0])
            quad(mesh, paint, (rings[row][index], rings[row][nxt], rings[row + 1][nxt], rings[row + 1][index]), allowed=allowed)
    for row in (0, len(rings) - 1):
        center = tuple(sum(v[i] for v in rings[row]) / len(rings[row]) for i in range(3))
        for index in range(len(rings[row])):
            triangle(mesh, paint, (center, rings[row][index], rings[row][(index + 1) % len(rings[row])]), allowed=allowed)


def fin(mesh, paint, outline, thickness, allowed=()):
    """Solid vertical tail, preserving the source's authored YZ silhouette."""
    for side in (-1, 1):
        points = [g.add(point, (thickness * side, 0., 0.)) for point in outline]
        normal = (side, 0., 0.)
        for index in range(1, len(points) - 1):
            triangle(mesh, paint, (points[0], points[index], points[index + 1]), [normal] * 3, allowed)
    for a, b in zip(outline, outline[1:] + outline[:1]):
        quad(mesh, paint, (g.add(a, (-thickness, 0., 0.)), g.add(b, (-thickness, 0., 0.)),
                          g.add(b, (thickness, 0., 0.)), g.add(a, (thickness, 0., 0.))), allowed=allowed)


def wheel(mesh, x, y, z, radius, width):
    cylinder(mesh, (x - width / 2, y, z), (x + width / 2, y, z), radius, RUBBER, 20)
    cylinder(mesh, (x - width * .53, y, z), (x + width * .53, y, z), radius * .38, STEEL, 12)


def tail_rotor_ring(mesh, center, radius=.35, wall=.045, depth=.025):
    """Open three-dimensional tail-rotor duct; no photographed background."""
    segments = 24
    for index in range(segments):
        a, b = math.tau * index / segments, math.tau * (index + 1) / segments
        def point(offset, r, angle):
            return (center[0] + offset, center[1] + r * math.cos(angle), center[2] + r * math.sin(angle))
        for r, sign in ((radius, 1), (radius - wall, -1)):
            normals = [(0., sign * math.cos(angle), sign * math.sin(angle)) for angle in (a, b, b, a)]
            quad(mesh, None, (point(-depth, r, a), point(-depth, r, b), point(depth, r, b), point(depth, r, a)), normals, color=STEEL)
        for offset, sign in ((-depth, -1), (depth, 1)):
            quad(mesh, None, (point(offset, radius, a), point(offset, radius, b),
                              point(offset, radius - wall, b), point(offset, radius - wall, a)), [(sign, 0., 0.)] * 4, color=STEEL)


def propellers(original, mesh, helicopter=False):
    name = original.header[:12].split(b'\0', 1)[0].decode('ascii')
    phase = int(name[-1]) - 1
    groups = [original.vertices] if helicopter else [original.vertices[i:i + 4] for i in range(0, len(original.vertices), 4)]
    for vertices in groups:
        center = tuple(sum(v[i] for v in vertices) / len(vertices) for i in range(3))
        axes = (0, 1) if helicopter else (0, 2)
        radius = max(max(v[i] for v in vertices) - min(v[i] for v in vertices) for i in axes) * .46
        normal_axis = 2 if helicopter else 1
        normal = tuple(1 if i == normal_axis else 0 for i in range(3))
        cylinder(mesh, g.add(center, g.mul(normal, -.035)), g.add(center, g.mul(normal, .035)), radius * .08, STEEL, 12)
        for blade in range(3):
            angle = blade * math.tau / 3 + phase * math.tau / 12
            along = tuple(math.cos(angle) if i == axes[0] else math.sin(angle) if i == axes[1] else 0 for i in range(3))
            across = g.unit(g.cross(normal, along))
            start = g.add(center, g.mul(along, radius * .08))
            end = g.add(center, g.mul(along, radius))
            outline = [g.add(point, g.mul(across, chord))
                       for point, chord in ((start, -radius * .045), (end, -radius * .022),
                                            (end, radius * .022), (start, radius * .045))]
            # A real blade has chord and thickness; its phase lives in the
            # existing four sibling meshes rather than a new animation system.
            for side in (-1, 1):
                points = [g.add(point, g.mul(normal, side * radius * .006)) for point in outline]
                g.polygon(mesh, points, STEEL, g.mul(normal, side))
            for p, q in zip(outline, outline[1:] + outline[:1]):
                g.polygon(mesh, (g.add(p, g.mul(normal, -.006 * radius)), g.add(q, g.mul(normal, -.006 * radius)),
                                 g.add(q, g.mul(normal, .006 * radius)), g.add(p, g.mul(normal, .006 * radius))), STEEL)


def light_aircraft(mesh, paint, twin=False):
    if not twin:
        sections = ((-2.365, -.025, -.828, .018, .018), (-2.066, -.027, -.828, .149, .149),
                    (-1.899, .006, -.83, .35, .19), (-.943, .006, -1.06, .343, .49),
                    (.1, .006, -1.035, .43, .50), (.592, .006, -1.033, .425, .517),
                    (1.6, .006, -1.43, .23, .19), (2.366, .006, -1.427, .057, .09), (3.156, .006, -1.202, .04, .03))
        loft(mesh, paint, sections, allowed=('TH37AIR', 'TH36AIR3', 'TH36AI1'))
        for side in (-1, 1):
            wing(mesh, paint, (side * .39, -.702, -.578), (side * .425, .601, -.516),
                 (side * 2.732, -.401, -.882), (side * 2.77, .484, -.896), .045, ('TH36AIR7',), attachment_x=.006)
            wing(mesh, paint, (side * .075, 2.579, -1.723), (side * .075, 3.068, -1.723),
                 (side * .888, 2.579, -1.723), (side * 1.03, 3.068, -1.723), .025, ('TH36AIR3',), attachment_x=.006)
        fin(mesh, paint, [(0.004, 2.366, -1.427), (.004, 2.643, -2.519), (.004, 3.156, -2.519), (.004, 3.156, -1.202)], .018, ('TH37AIR',))
        for x in (-1.355, 1.653):
            cylinder(mesh, (x, -.198, -.673), (x, -.198, -.18), .035, STEEL)
            wheel(mesh, x, -.198, -.16, .16, .09)
        cylinder(mesh, (-.009, -1.217, -.609), (-.009, -1.217, -.12), .025, STEEL)
        wheel(mesh, -.009, -1.217, -.109, .105, .07)
    else:
        cx = -.75
        sections = ((-4.01, cx, -1.091, .272, .045), (-3.764, cx, -.9, .272, .21),
                    (-2.403, cx, -1.3, .62, .6), (-2.003, cx, -1.345, .47, .65),
                    (0., cx, -1.345, .466, .65), (1.6, cx, -1.6, .3, .395),
                    (2.4, cx, -1.75, .15, .20), (3., cx, -1.695, .078, .045))
        loft(mesh, paint, sections, allowed=('TH36AIR', 'TH36AIR3', 'TH36AI1'))
        for side in (-1, 1):
            wing(mesh, paint, (cx + side * .37, -1.951, -.77), (cx + side * .45, -.578, -.78),
                 (cx + side * 4.88, -1.285, -1.35), (cx + side * 4.75, -.578, -1.315), .055, ('TH36AIR7',), attachment_x=cx)
            wing(mesh, paint, (cx + side * .17, 2.105, -1.696), (cx + side * .17, 2.807, -1.74),
                 (cx + side * 1.14, 2.334, -1.696), (cx + side * 1.24, 2.807, -1.74), .025, ('TH36AIR3',), attachment_x=cx)
            # Two original propeller centers sit in separate wing nacelles.
            engine_x = -2.873 if side < 0 else 1.339
            loft(mesh, paint, ((-2.02, engine_x, -1.147, .16, .18), (-1.7, engine_x, -1.147, .25, .25),
                              (-.8, engine_x, -1.147, .18, .20)), segments=16, steps=2, allowed=('TH36AIR3', 'TH36AIR'))
        fin(mesh, paint, [(cx, 1.6, -1.995), (cx, 2.4, -3.502), (cx, 3., -3.502), (cx, 3., -1.695)], .025, ('TH36AIR',))
        for x, y, top in ((-2.433, -1.141, -.856), (1.006, -1.141, -.856), (-.751, -3.095, -.728)):
            cylinder(mesh, (x, y, top), (x, y, -.18), .04, STEEL)
            wheel(mesh, x, y, -.17, .177, .12)


def jet(mesh, paint, billboard=False):
    if billboard:
        names = paint.names
        if 'MegaPSPSkin' not in names:
            raise KeyError('PSP1_1 requires the opaque MegaPSPSkin material')
        sections = ((-2.393, -.038, -11.02, .025, .025), (-1.8, -.038, -10.98, .28, .24),
                    (-.5, -.038, -10.78, .36, .29), (2., -.038, -10.38, .36, .29),
                    (4.5, -.038, -10.0, .20, .17), (5.607, -.038, -9.85, .025, .025))
        loft(mesh, PSPAtlas(names, 'body', sections), sections)
        for side in (-1, 1):
            main = ((side * .25, .3, -10.6), (side * .25, 2.5, -10.5),
                    (side * 3.25, 3.3, -10.42), (side * 3.25, 3.8, -10.4))
            tail = ((side * .15, 4.1, -10.02), (side * .15, 5.4, -9.85),
                    (side * 1.4, 5.1, -10.0), (side * 1.4, 5.6, -9.9))
            wing(mesh, PSPAtlas(names, 'wing', wing_points=main), *main, .055, attachment_x=-.038)
            wing(mesh, PSPAtlas(names, 'wing', wing_points=tail), *tail, .035, attachment_x=-.038)
            loft(mesh, PSPAtlas(names, 'engine'),
                 ((1.2, side * 1.05, -10.1, .19, .19), (2.1, side * 1.05, -10.0, .22, .22),
                              (2.5, side * 1.05, -9.95, .14, .14)), segments=16, steps=2)
        fin(mesh, PSPAtlas(names, 'fin'),
            [(-.038, 4.0, -10.08), (-.038, 4.8, -12.), (-.038, 5.5, -11.85), (-.038, 5.607, -9.85)], .035)
    else:
        sections = ((-20.615, -.059, -2.196, .35, .35), (-19., -.059, -2.1, 1.35, 1.1),
                    (-17.741, -.059, -2.497, 2.281, 2.31), (-10., -.059, -2.497, 2.281, 2.31),
                    (5., -.059, -2.497, 2.281, 2.31), (15.59, -.059, -2.497, 2.281, 2.15),
                    (20., -.059, -2.3, .95, .95), (23.564, -.059, -2.206, .08, .08))
        loft(mesh, paint, sections, allowed=('MARKAM', 'MARKAMB'))
        for side in (-1, 1):
            wing(mesh, paint, (side * 2.25, -1.076, -.266), (side * 2.25, 15.59, -.344),
                 (side * 21.3, 15.0, -.344), (side * 21.3, 15.59, -.344), .22, ('MARKAMW',), attachment_x=-.059)
            wing(mesh, paint, (side * .6, 19., -4.8), (side * .6, 23.5, -4.5),
                 (side * 6., 23., -4.8), (side * 6., 25., -4.5), .1, ('MARKAMW', 'MARKAM'), attachment_x=-.059)
        fin(mesh, paint, [(-.059, 9.681, -4.808), (-.059, 22.134, -8.976),
                          (-.059, 28.779, -8.112), (-.059, 23.564, -2.206)], .09, ('MARKAM',))


def helicopter(mesh, paint):
    sections = ((-2.005, .006, -.86, .22, .13), (-1.75, .006, -1.15, .47, .40),
                (-1.39, .006, -1.25, .42, .50), (-.626, .006, -1.38, .35, .635),
                (.107, .006, -1.39, .30, .59), (.7, .006, -1.4, .20, .29),
                (1.237, .006, -1.49, .145, .13), (2.5, .006, -1.55, .09, .10), (3.3, .006, -1.5, .045, .06))
    loft(mesh, paint, sections, allowed=('BH17HEL', 'BH17HEL3'))
    for side in (-1, 1):
        points = [(side * .55, -2.227, -.25), (side * .60, -1.95, -.175),
                  (side * .647, .3, -.175), (side * .55, .48, -.209)]
        for a, b in zip(points, points[1:]):
            cylinder(mesh, a, b, .035, STEEL)
        for y in (-1.39, .107):
            cylinder(mesh, (side * .38, y, -.82), (side * .61, y, -.19), .032, STEEL)
    cylinder(mesh, (.006, -.19, -1.98), (.006, -.19, -2.32), .04, STEEL)
    tail_paint = OpaqueSkin(paint.names, 'BH17HEL', HELI_TAIL_SKIN)
    fin(mesh, tail_paint, [(.006, 3.2, -1.55), (.006, 3.6, -1.60),
                           (.006, 3.719, -.874), (.006, 3.3, -1.1)], .018)
    # The small tail rotor belongs to the fixed body in the original mesh.
    center = (.03, 3.35, -1.90)
    tail_rotor_ring(mesh, center)
    cylinder(mesh, (-.055, center[1], center[2]), (.055, center[1], center[2]), .08, STEEL)
    for index in range(3):
        angle = index * math.tau / 3
        end = (.03, center[1] + .27 * math.cos(angle), center[2] + .27 * math.sin(angle))
        cylinder(mesh, center, end, .018, STEEL, 6)


def balloon(mesh, paint, audi=False):
    if audi:
        sections = ((-16.84, 0., -16.60, 8.69, 2.49), (-15.67, 0., -18., 8.5, 6.07),
                    (-6.33, 0., -20.2, 8.9, 8.17), (3.42, 0., -20.2, 9.38, 8.25),
                    (11.26, 0., -17.6, 8.94, 5.66), (18.99, 0., -17.3, 8.3, 4.34),
                    (20.39, 0., -15.72, 8.35, 1.97))
        loft(mesh, paint, sections, axis=0, segments=32, exponent=.45,
             allowed=('CR13AUDB', 'CR13AUD1', 'CR13AUD2', 'CR13AUD3', 'CR13AUD4'))
        # Keep the original advertising balloon's tapered lower envelope.
        loft(mesh, paint, ((-12., 0., 0., 8.65, 8.6), (-8., 0., 0., 5., 4.8), (-2.624, .005, -.154, .18, .18)),
             axis=2, segments=32, steps=3, allowed=('CR13AUD5',))
        bottom, top, half = -2.88, 0., 1.6
    else:
        sections = ((-32.546, 0., 0., 3.458, 3.458), (-30.035, 0., 0., 11.095, 11.095),
                    (-24.83, 0., 0., 14.393, 14.393), (-17.898, 0., 0., 13.07, 13.07),
                    (-10., 0., 0., 8.5, 8.5), (-3., 0., 0., 2.8, 2.8), (-.003, .002, 0., .15, .15))
        # The source envelope's six-sided rings have different X/Y extrema;
        # retain those dimensions while replacing their angular silhouette.
        sections = tuple((*row[:4], row[4] * math.sqrt(3) / 2) for row in sections)
        loft(mesh, paint, sections, axis=2, segments=32, steps=3)
        bottom, top, half = 1.939, 3.514, 1.575
    # The basket is open at its upper lip (negative localZ means upward).
    for side in (-1, 1):
        for axis in (0, 1):
            a, b = [0., 0., bottom], [0., 0., top]
            a[axis] = b[axis] = side * half
            other = 1 - axis
            points = []
            for lateral, height in ((-half, bottom), (half, bottom), (half, top), (-half, top)):
                p = list(a)
                p[other], p[2] = lateral, height
                points.append(tuple(p))
            normal = tuple(side if i == axis else 0 for i in range(3))
            quad(mesh, paint, points, [normal] * 4)
        for y in (-half, half):
            cylinder(mesh, (side * half, y, bottom), (side * half * .55, y * .55, bottom - 2.3), .045, ROPE, 8)
    quad(mesh, paint, ((-half, -half, top), (half, -half, top), (half, half, top), (-half, half, top)), [(0., 0., 1.)] * 4)
    corners = [(-half, -half, bottom), (half, -half, bottom), (half, half, bottom), (-half, half, bottom)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        cylinder(mesh, a, b, .09, ROPE, 10)


def enhance(original, mesh_type, texture_names, family='moving'):
    """Return rebuilt mesh and stats; no node/material allocation or mutation."""
    name = original.header[:12].split(b'\0', 1)[0].decode('ascii')
    names = tuple((n.decode('ascii') if isinstance(n, bytes) else n).rstrip('\0') for n in texture_names)
    known = name in ('AIRMARK_1', 'PSP1_1', 'PLANE1R_5', 'PLANE2R_5', 'HELI_5', 'CR05BALL_1', 'CR06BALL_1', 'CR13AUDB_1') \
        or name in tuple(f'HELI_{i}' for i in range(1, 5)) or name in tuple(f'PLANE{p}R_{i}' for p in (1, 2) for i in range(1, 5))
    if not known:
        return mesh_type(original.header, list(original.vertices), list(original.faces)), dict(rebuilt_meshes=0, triangles=len(original.faces))
    key = (name, tuple(original.vertices), tuple(original.faces), names)
    if key in _CACHE:
        vertices, faces, stats = _CACHE[key]
        return mesh_type(original.header, list(vertices), list(faces)), dict(stats)
    mesh = mesh_type(original.header, [], [])
    paint = Paint(original, names, readable_mark=name == 'AIRMARK_1')
    if name.startswith('HELI_') and name != 'HELI_5':
        propellers(original, mesh, True)
    elif name.startswith('PLANE') and name not in ('PLANE1R_5', 'PLANE2R_5'):
        propellers(original, mesh)
    elif name in ('AIRMARK_1', 'PSP1_1'):
        jet(mesh, paint, name == 'PSP1_1')
    elif name.startswith('PLANE'):
        light_aircraft(mesh, paint, name == 'PLANE2R_5')
    elif name == 'HELI_5':
        helicopter(mesh, paint)
    else:
        balloon(mesh, paint, name == 'CR13AUDB_1')
    if not mesh.faces or max(len(mesh.faces), len(mesh.vertices)) > 65535 or len(mesh.faces) > 5000:
        raise ValueError(f'Mega aircraft mesh budget exceeded: {name}')
    stats = dict(rebuilt_meshes=1, triangles=len(mesh.faces), source_triangles=len(original.faces))
    _CACHE[key] = (tuple(mesh.vertices), tuple(mesh.faces), stats)
    return mesh, stats
