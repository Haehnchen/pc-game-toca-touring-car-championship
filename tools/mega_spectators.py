"""Shallow source-shaped backing for the three Donington spectator cards.

Keep the original front vertices, faces, UVs and placements. Four overlapping
pairs retain the eight-person crowd; the two narrow cards retain their original
combined figure/equipment silhouettes without interpreting ambiguous anatomy.
"""

import struct

import vehicle_model as geometry


SOURCE_CRCS = {
    'LPEOP5_1': ('LPEOPLE5', {'ca3b28db'}),
    'LPEOP2_1': ('LPEOPLE2', {'fedbbd80', 'fdafb9fa'}),
    'LPEOP3_1': ('LPEOPLE3', {'598c5245', '94157609'}),
}
FRONT_INSET = .004
DEPTH = .14
WHITE = (255, 255, 255, 255)

# Broad outlines in canonical image coordinates, not pixel-boundary extrusion.
# Original alpha remains authoritative for all visible gaps and fine edges.
CONTOURS = {
    'LPEOP5_1': (128., 64., (
        ((13, 7), (18, 7), (22, 14), (27, 8), (31, 9), (33, 16),
         (37, 26), (34, 39), (35, 63), (24, 63), (22, 40),
         (19, 63), (8, 63), (9, 39), (5, 32), (6, 17)),
        ((51, 7), (56, 7), (59, 5), (63, 6), (65, 13), (69, 19),
         (69, 33), (65, 40), (66, 64), (57, 64), (54, 42),
         (51, 63), (41, 63), (42, 39), (40, 24), (44, 16)),
        ((76, 5), (82, 6), (84, 13), (88, 7), (92, 8), (95, 16),
         (98, 26), (95, 40), (96, 64), (84, 64), (81, 43),
         (79, 63), (68, 63), (69, 36), (67, 26), (70, 15)),
        ((102, 6), (107, 7), (110, 13), (113, 3), (118, 4), (120, 12),
         (125, 17), (128, 25), (124, 39), (127, 64), (114, 64),
         (111, 43), (108, 63), (97, 63), (97, 38), (94, 26), (97, 15)),
    )),
    'LPEOP2_1': (32., 64., (
        ((15, 0), (20, 0), (21, 7), (26, 8), (28, 14), (32, 18),
         (30, 29), (28, 36), (31, 46), (30, 62), (23, 62),
         (20, 41), (17, 40), (16, 64), (6, 64), (5, 45),
         (7, 36), (1, 31), (1, 16), (6, 10), (12, 8)),
    )),
    'LPEOP3_1': (32., 64., (
        ((8, 1), (14, 2), (16, 7), (20, 8), (22, 15), (27, 19),
         (31, 27), (29, 34), (32, 52), (30, 64), (23, 64),
         (19, 43), (17, 39), (16, 58), (13, 64), (4, 64),
         (1, 59), (2, 45), (0, 28), (2, 14), (5, 8)),
    )),
}


def _identity(original, texture_names):
    name = original.header[:12].rstrip(b'\0').decode('ascii', errors='replace')
    if name not in SOURCE_CRCS:
        return None
    material, checksums = SOURCE_CRCS[name]
    # Reconstruct the COMPLETE source mesh including its header, UVs, normals,
    # face fields and original material slots. Other-track names are not enough.
    encoded = geometry.Mesh(original.header, original.vertices, original.faces).encode()
    if geometry.crc32(encoded) not in checksums:
        return None
    names = [(value.decode('ascii') if isinstance(value, bytes) else value).rstrip('\0').upper()
             for value in texture_names]
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    if not slots or any(slot < 0 or slot >= len(names) or names[slot] != material for slot in slots):
        return None
    return name, slots[0]


def matches(original, texture_names):
    return _identity(original, texture_names) is not None


def _cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _triangles(contour):
    """Triangulate a coarse concave outline without filling its leg notches."""
    remaining = list(range(len(contour)))
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(contour, contour[1:] + contour[:1]))
    if area < 0:
        remaining.reverse()
    result = []
    while len(remaining) > 3:
        for offset, current in enumerate(remaining):
            previous, following = remaining[offset - 1], remaining[(offset + 1) % len(remaining)]
            a, b, c = contour[previous], contour[current], contour[following]
            if _cross(a, b, c) <= 1e-10:
                continue
            if any(min(_cross(a, b, contour[index]), _cross(b, c, contour[index]),
                       _cross(c, a, contour[index])) >= -1e-10
                   for index in remaining if index not in (previous, current, following)):
                continue
            result.append((previous, current, following))
            del remaining[offset]
            break
        else:
            raise ValueError('Spectator outline must be a simple polygon')
    result.append(tuple(remaining))
    return result


def _projection(original, u, v):
    # Crowd U=.496048 is the original seam between two coplanar halves.
    # Static rendering clamps EACH source vertex UV before interpolation.
    # Invert those clamped anchors, rather than interpolating raw atlas edges.
    # Otherwise the backing moves the texture's silhouettes relative to front.
    u, v = max(.02, min(.99, u)), max(.02, min(.99, v))
    horizontal = sorted({(max(.02, min(.99, vertex[3] - (1 if vertex[3] > 1 else 0))), vertex[0])
                         for vertex in original.vertices})
    vertical = sorted({(max(.02, min(.99, vertex[4] + 1)), vertex[2])
                       for vertex in original.vertices})

    def interpolate(value, anchors):
        for (left_uv, left_position), (right_uv, right_position) in zip(anchors, anchors[1:]):
            if left_uv <= value <= right_uv:
                return left_position + (right_position - left_position) * (value - left_uv) / (right_uv - left_uv)
        raise ValueError('Spectator contour lies outside its clamped source UV range')

    return interpolate(u, horizontal), original.vertices[0][1], interpolate(v, vertical)


def _volume(mesh, original, contour, width, height, texture):
    contour = tuple((max(.02, min(.99, x / width)), max(.02, min(.99, y / height)))
                    for x, y in contour)
    anchors = sorted({max(.02, min(.99, vertex[3])) for vertex in original.vertices})
    seams = anchors[1:-1]

    def clip(polygon, seam, left):
        result = []
        for first, second in zip(polygon, polygon[1:] + polygon[:1]):
            inside = first[0] <= seam if left else first[0] >= seam
            other_inside = second[0] <= seam if left else second[0] >= seam
            if inside:
                result.append(first)
            if inside != other_inside:
                fraction = (seam - first[0]) / (second[0] - first[0])
                result.append((seam, first[1] + fraction * (second[1] - first[1])))
        clean = []
        for point in result:
            if not clean or abs(point[0] - clean[-1][0]) + abs(point[1] - clean[-1][1]) > 1e-12:
                clean.append(point)
        if len(clean) > 1 and abs(clean[0][0] - clean[-1][0]) + abs(clean[0][1] - clean[-1][1]) < 1e-12:
            clean.pop()
        return clean

    front, rear = {}, {}

    def cap_vertex(sample, back):
        cache = rear if back else front
        key = tuple(round(value, 12) for value in sample)
        if key not in cache:
            point = _projection(original, *sample)
            cache[key] = mesh.vertex(geometry.add(point, (0., DEPTH if back else FRONT_INSET, 0.)),
                                     (0., 1. if back else -1., 0.), (sample[0], sample[1] - 1))
        return cache[key]

    # An added primitive must stay within one original affine UV chart. The
    # crowd's two original halves have slightly different U-to-X scales.
    for triangle in _triangles(contour):
        polygons = [[contour[index] for index in triangle]]
        for seam in seams:
            polygons = [part for polygon in polygons for part in (clip(polygon, seam, True), clip(polygon, seam, False))
                        if len(part) >= 3]
        for polygon in polygons:
            for index in range(1, len(polygon) - 1):
                samples = (polygon[0], polygon[index], polygon[index + 1])
                if abs(_cross(*samples)) <= 1e-12:
                    continue
                mesh.triangle(tuple(cap_vertex(sample, False) for sample in samples), WHITE, texture)
                mesh.triangle(tuple(cap_vertex(sample, True) for sample in samples), WHITE, texture)

    boundary = []
    for first, second in zip(contour, contour[1:] + contour[:1]):
        boundary.append(first)
        crossings = [seam for seam in seams if min(first[0], second[0]) < seam < max(first[0], second[0])]
        for seam in sorted(crossings, reverse=second[0] < first[0]):
            fraction = (seam - first[0]) / (second[0] - first[0])
            boundary.append((seam, first[1] + fraction * (second[1] - first[1])))
    for first, second in zip(boundary, boundary[1:] + boundary[:1]):
        point, other = _projection(original, *first), _projection(original, *second)
        uv, other_uv = (first[0], first[1] - 1), (second[0], second[1] - 1)
        geometry.polygon(mesh, [geometry.add(point, (0., FRONT_INSET, 0.)),
                                geometry.add(point, (0., DEPTH, 0.)),
                                geometry.add(other, (0., DEPTH, 0.)),
                                geometry.add(other, (0., FRONT_INSET, 0.))],
                         WHITE, uvs=[uv, uv, other_uv, other_uv], texture=texture)


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    stats = dict(donington_spectators=0, spectator_backings=0, added_triangles=0)
    identity = _identity(original, texture_names)
    if identity is None:
        return original, stats
    name, texture = identity
    width, height, contours = CONTOURS[name]
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    for contour in contours:
        _volume(mesh, original, contour, width, height, texture)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_spectators=1, spectator_backings=len(contours), added_triangles=added)
    return mesh, stats
