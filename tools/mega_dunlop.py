"""Donington's shared DA16DUN_1 tyre bridge, in original +Z-up space.

Only the byte-identified GP/Short member5 mesh is accepted. The five authored
materials and every source triangle remain intact. Rounded shoulder mouldings,
the inner/outer barrel and closed support end walls add simple physical
detail to the source's polygonal tyre. No actor, collision or material is added.
"""

import math
import struct

import vehicle_model as geometry


VERTICES_CRC32 = 'c59fe62b'
TOPOLOGY_CRC32 = '9e7258e9'
MATERIALS = ('DA16DUN0', 'DA16DUN1', 'DA16DUN4', 'DA16DUN8', 'DA16DUN9')
WHITE = (255, 255, 255, 255)
# Original outline indices, left foot through crown to right foot. Several
# vertices are duplicated for the two material islands; use positions only.
OUTER = (114, 112, 116, 118, 120, 10, 8, 6, 4, 2, 0)
INNER = (113, 115, 117, 119, 121, 11, 9, 7, 3, 5, 1)
DEPTH = 180479 / 65536
BARREL_OFFSET = .055


def _names(texture_names):
    return [(name.decode('ascii') if isinstance(name, bytes) else name).rstrip('\0').upper()
            for name in texture_names]


def matches(original, texture_names):
    """GP and Short share exact vertices/topology, with different material slots."""
    if original.header[:12].rstrip(b'\0') != b'DA16DUN_1' or len(original.vertices) != 156 or len(original.faces) != 116:
        return False
    names = _names(texture_names)
    if any(name not in names for name in MATERIALS):
        return False
    vertices = b''.join(struct.pack('<8i', *(round(component * 65536) for component in vertex))
                        for vertex in original.vertices)
    topology = b''.join(face[:8] + face[10:] for face in original.faces)
    if geometry.crc32(vertices) != VERTICES_CRC32 or geometry.crc32(topology) != TOPOLOGY_CRC32:
        return False
    # Bindings are part of identity too: the mesh must not merely have the five
    # names somewhere in its archive. This handles the GP49/Short46 prefix.
    expected = ('DA16DUN0',) * 20 + ('DA16DUN1',) * 4 + ('DA16DUN4',) * 12 + ('DA16DUN8',) * 12 + ('DA16DUN9',) * 68
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _outline(original, indices, subdivisions=2):
    points = [(original.vertices[index][0], original.vertices[index][2]) for index in indices]
    curved = []
    for index in range(len(points) - 1):
        for step in range(subdivisions):
            t = step / subdivisions
            p0, p1 = points[max(0, index - 1)], points[index]
            p2, p3 = points[index + 1], points[min(len(points) - 1, index + 2)]
            # Cubic interpolation rounds the measured polygon shoulders. The
            # source control points, feet and crown remain exact; constrain the
            # interpolation to the solid side of each source chord so the
            # inner barrel cannot reduce the original drive-through clearance.
            point = tuple(.5 * (2 * p1[a] + (-p0[a] + p2[a]) * t +
                                (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t * t +
                                (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t * t * t) for a in range(2))
            tangent = geometry.sub(p2, p1)
            normal = geometry.unit((-tangent[1], tangent[0]))
            outside = geometry.dot(geometry.sub(point, p1), normal)
            if outside < 0:
                point = geometry.sub(point, geometry.mul(normal, outside))
            curved.append(point)
    curved.append(points[-1])
    normals = []
    for first, second in zip(curved, curved[1:]):
        dx, dz = second[0] - first[0], second[1] - first[1]
        length = math.hypot(dx, dz)
        normals.append((-dz / length, dx / length))
    miters = [normals[0]]
    for before, after in zip(normals, normals[1:]):
        normal = geometry.unit(geometry.add(before, after))
        miters.append(geometry.mul(normal, 1 / geometry.dot(normal, before)))
    miters.append(normals[-1])
    return list(zip(curved, miters))


def _source_uv(original, slot, side, point):
    """Project shoulder paint through the original inset triangle UV mapping.

    A shoulder extends a few centimetres beyond the polygon; there it inherits
    the closest edge's rubber bleed. The lettering and shared N use exactly the
    canonical planar map, rather than stretching a newly invented strip.
    """
    selected, distance = None, math.inf
    for face in original.faces:
        if struct.unpack_from('<h', face, 8)[0] != slot:
            continue
        vertices = [original.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
        if not all(abs(vertex[1] - side * DEPTH) < 1e-8 for vertex in vertices):
            continue
        a, b, c = [(vertex[0], vertex[2]) for vertex in vertices]
        first, second, query = geometry.sub(b, a), geometry.sub(c, a), geometry.sub(point, a)
        det = first[0] * second[1] - first[1] * second[0]
        beta = (query[0] * second[1] - query[1] * second[0]) / det
        gamma = (first[0] * query[1] - first[1] * query[0]) / det
        weights = (1 - beta - gamma, beta, gamma)
        if min(weights) >= -1e-9:
            candidates = [weights]
        else:
            candidates = []
            for i, j in ((0, 1), (1, 2), (2, 0)):
                edge = geometry.sub((a, b, c)[j], (a, b, c)[i])
                fraction = max(0., min(1., geometry.dot(geometry.sub(point, (a, b, c)[i]), edge) / geometry.dot(edge, edge)))
                candidate = [0., 0., 0.]
                candidate[i], candidate[j] = 1 - fraction, fraction
                candidates.append(candidate)
        for candidate in candidates:
            projected = tuple(sum(weight * vertex[axis] for weight, vertex in zip(candidate, vertices)) for axis in (0, 2))
            separation = math.dist(projected, point)
            if separation < distance:
                # Static scenery clamps each original corner before triangle
                # interpolation. Preserve that map in the additional vertices.
                uvs = [(max(.02, min(.99, vertex[3] - (1 if vertex[3] > 1 else 0))),
                        max(.02, min(.99, vertex[4] + 1)) - 1) for vertex in vertices]
                selected = tuple(sum(weight * uv[axis] for weight, uv in zip(candidate, uvs)) for axis in range(2))
                distance = separation
    if selected is None:
        raise ValueError('Canonical Dunlop shoulder lacks its original sidewall UV')
    return selected


def _shoulders(mesh, original, outline, slots, inner=False):
    # Cross section from the source sidewall onto the barrel. The inner moulding
    # always offsets into rubber, preserving the complete drive-through hole.
    profile = ((.28, DEPTH + .018), (.18, DEPTH + .045), (.02, DEPTH - .34)) if inner else \
              ((-.32, DEPTH + .018), (-.20, DEPTH + .045), (BARREL_OFFSET, DEPTH - .34))
    direction = -1 if inner else 1
    crown_x = original.vertices[10][0]
    for side in (-1, 1):
        for (a, na), (b, nb) in zip(outline, outline[1:]):
            slot = slots['DA16DUN0'] if ((a[0] + b[0]) / 2 >= crown_x) == (side == 1) else slots['DA16DUN9']
            for band in range(len(profile) - 1):
                indices = []
                for point, radial, level in ((a, na, band), (b, nb, band), (b, nb, band + 1), (a, na, band + 1)):
                    offset, depth = profile[level]
                    xz = geometry.add(point, geometry.mul(radial, offset))
                    xz = (xz[0], max(original.vertices[0][2], xz[1]))
                    before, after = profile[max(0, level - 1)], profile[min(len(profile) - 1, level + 1)]
                    dr, dd = after[0] - before[0], after[1] - before[1]
                    normal = (radial[0] * -dd * direction, side * dr * direction, radial[1] * -dd * direction)
                    indices.append(mesh.vertex((xz[0], side * depth, xz[1]), normal, _source_uv(original, slot, side, xz)))
                mesh.triangle(indices[:3], WHITE, slot)
                mesh.triangle((indices[0], indices[2], indices[3]), WHITE, slot)


def _tread_uv(across, along):
    # DA16DUN9's original tread island: both source outer-barrel rows sample
    # U=.4567..9606 and V=-.0079..-.2283. Avoid its border and preserve the
    # accepted braking markers/arrows and continuous sidewall artwork.
    return (.49 + .43 * across, -.04 - .15 * along)


def _barrel(mesh, outline, slot, inner=False):
    offset = .02 if inner else BARREL_OFFSET
    for (a, na), (b, nb) in zip(outline, outline[1:]):
        for first, second in ((-1., 1.),):
            indices = []
            for point, radial, fraction in ((a, na, first), (b, nb, first), (b, nb, second), (a, na, second)):
                xz = geometry.add(point, geometry.mul(radial, offset))
                normal = (radial[0] * (-1 if inner else 1), 0., radial[1] * (-1 if inner else 1))
                indices.append(mesh.vertex((xz[0], fraction * (DEPTH - .34), xz[1]), normal,
                                           _tread_uv((fraction + 1) / 2, min(1., max(0., point[1] / 15.4)))))
            mesh.triangle(indices[:3], WHITE, slot)
            mesh.triangle((indices[0], indices[2], indices[3]), WHITE, slot)


def _supports(mesh, original, slot):
    # Broad original buttress faces remain exact. One quad closes each open
    # measured outer end; the texture supplies the small edge/bevel detail.
    for outer, front, rear in ((36, 32, 38), (45, 44, 48)):
        x = original.vertices[outer][0]
        y0, y1 = original.vertices[rear][1], original.vertices[front][1]
        z0, z1 = original.vertices[outer][2], original.vertices[front][2]
        sign = 1 if x > 0 else -1
        points = [(x + sign * .015, y, z) for y, z in ((y0, z0), (y1, z0), (y1, z1), (y0, z1))]
        geometry.polygon(mesh, points, WHITE, (sign, 0., 0.),
                         ((.04, -.04), (.96, -.04), (.96, -.96), (.04, -.96)), slot)

def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    """Return an identified bridge plus bounded detail, or the untouched input.

    ``surface_index`` is accepted alongside the other scenery generators; this
    pilot reuses the five source materials, so no extra atlas is required.
    A family budget unable to hold the complete model leaves it unchanged.
    """
    stats = dict(dunlop_bridges=0, dunlop_shoulders=0, dunlop_tread_lugs=0, dunlop_support_ends=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    slots = {name: _names(texture_names).index(name) for name in MATERIALS}
    outer, inner = _outline(original, OUTER), _outline(original, INNER)
    _shoulders(mesh, original, outer, slots)
    _shoulders(mesh, original, inner, slots, inner=True)
    _barrel(mesh, outer, slots['DA16DUN9'])
    _barrel(mesh, inner, slots['DA16DUN9'], inner=True)
    _supports(mesh, original, slots['DA16DUN4'])
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(dunlop_bridges=1, dunlop_shoulders=4, dunlop_tread_lugs=0,
                 dunlop_support_ends=2, added_triangles=added)
    return mesh, stats
