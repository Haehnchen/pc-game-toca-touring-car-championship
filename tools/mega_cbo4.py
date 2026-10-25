"""Donington DA28CBO4_1 roof, window surrounds and projecting sign thickness.

This one source-identified building retains all59 vertices/30 faces and their
five materials. The dark COMA facade motifs remain uninterpreted; additions
retain the roof silhouette, the clear COM4 window bands and the actual
PIT3 board. No collision, actor or unrelated CBO family geometry is changed.
"""

import math
import struct

import vehicle_model as geometry


VERTICES_CRC32 = '28f1749d'
TOPOLOGY_CRC32 = 'e7f2ca52'
MATERIALS = ('DA28COM9', 'DA28COMA', 'DA28PIT3', 'DA28COM4', 'DA02GSD9')
# Pale/neutral sill pixels in canonical COM4 rows7/8 form exactly these three
#10-pixel groups. The front's right15m tile mirrors U; clipping keeps that map.
WINDOWS = ((10., 3., 20., 10.), (22., 3., 32., 10.), (53., 3., 63., 10.))
FRONT_WINDOW_FACES = (18, 19, 25, 27)
SIGN_BLANK_UV = (.04, -.90)
WHITE = (255, 255, 255, 255)


def _names(texture_names):
    return [(name.decode('ascii') if isinstance(name, bytes) else name).rstrip('\0').upper()
            for name in texture_names]


def _source_matches(original, name, vertices_count, faces_count, vertices_crc32, topology_crc32):
    if original.header[:12].rstrip(b'\0') != name or len(original.vertices) != vertices_count or len(original.faces) != faces_count:
        return False
    vertices = b''.join(struct.pack('<8i', *(round(component * 65536) for component in vertex))
                        for vertex in original.vertices)
    topology = b''.join(face[:8] + face[10:] for face in original.faces)
    return geometry.crc32(vertices) == vertices_crc32 and geometry.crc32(topology) == topology_crc32


def matches(original, texture_names):
    if not _source_matches(original, b'DA28CBO4_1', 59, 30, VERTICES_CRC32, TOPOLOGY_CRC32):
        return False
    names = _names(texture_names)
    expected = ('DA28COM9',) * 8 + ('DA28COMA',) * 8 + ('DA28PIT3',) * 2 + ('DA28COM4',) * 10 + ('DA02GSD9',) * 2
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _uv(vertex):
    return (max(.02, min(.99, vertex[3] - (1 if vertex[3] > 1 else 0))),
            max(.02, min(.99, vertex[4] + 1)) - 1)


def _triangle(original, face_index):
    face = original.faces[face_index]
    vertices = [original.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
    samples = [(vertex[:3], _uv(vertex)) for vertex in vertices]
    normal = geometry.unit(geometry.cross(geometry.sub(vertices[1][:3], vertices[0][:3]), geometry.sub(vertices[2][:3], vertices[0][:3])))
    lighting = tuple(sum(vertex[axis] for vertex in vertices) for axis in (5, 6, 7))
    if geometry.dot(normal, lighting) < 0:
        samples.reverse()
        normal = geometry.mul(normal, -1)
    return samples, normal, struct.unpack_from('<h', face, 8)[0]


def _clip(samples, axis, boundary, greater):
    result = []
    for first, second in zip(samples, samples[1:] + samples[:1]):
        a, b = first[1][axis] - boundary, second[1][axis] - boundary
        inside_a = a >= -1e-12 if greater else a <= 1e-12
        inside_b = b >= -1e-12 if greater else b <= 1e-12
        if inside_a:
            result.append(first)
        if inside_a != inside_b:
            t = a / (a - b)
            result.append((geometry.interpolate(first[0], second[0], t), geometry.interpolate(first[1], second[1], t)))
    cleaned = []
    for sample in result:
        if not cleaned or math.dist(sample[0], cleaned[-1][0]) > 1e-9:
            cleaned.append(sample)
    if len(cleaned) > 1 and math.dist(cleaned[0][0], cleaned[-1][0]) < 1e-9:
        cleaned.pop()
    return cleaned


def _polygon(mesh, samples, texture, normal=None):
    geometry.polygon(mesh, [sample[0] for sample in samples], WHITE, normal, [sample[1] for sample in samples], texture)


def _raised(mesh, samples, normal, texture, height):
    front = [(geometry.add(point, geometry.mul(normal, height)), uv) for point, uv in samples]
    _polygon(mesh, front, texture, normal)
    for index in range(len(samples)):
        j = (index + 1) % len(samples)
        _polygon(mesh, (samples[index], samples[j], front[j], front[index]), texture)


def _frame(mesh, original, face_indices, bounds, width_u, width_v, height):
    u0, v0, u1, v1 = bounds
    rectangles = ((u0, v0, u0 + width_u, v1), (u1 - width_u, v0, u1, v1),
                  (u0 + width_u, v0, u1 - width_u, v0 + width_v),
                  (u0 + width_u, v1 - width_v, u1 - width_u, v1))
    count = 0
    for face_index in face_indices:
        samples, normal, texture = _triangle(original, face_index)
        for left, top, right, bottom in rectangles:
            clipped = samples
            for axis, boundary, greater in ((0, left, True), (0, right, False), (1, top, True), (1, bottom, False)):
                clipped = _clip(clipped, axis, boundary, greater) if clipped else []
            if len(clipped) >= 3:
                _raised(mesh, clipped, normal, texture, height)
                count += 1
    return count


def _window_panel(mesh, original, face_indices, bounds, height=.065):
    """One shallow painted window panel per source triangle, without tiny rims.

    Clipping retains each original tile and its mirrored/tapered UV mapping.
    The unchanged facade closes the back of these simple shallow volumes.
    """
    count = 0
    left, top, right, bottom = bounds
    for face_index in face_indices:
        samples, normal, texture = _triangle(original, face_index)
        for axis, boundary, greater in ((0, left, True), (0, right, False), (1, top, True), (1, bottom, False)):
            samples = _clip(samples, axis, boundary, greater) if samples else []
        if len(samples) >= 3:
            _raised(mesh, samples, normal, texture, height)
            count += 1
    return count


def _prism(mesh, first, second, normal, texture):
    tangent = geometry.unit(geometry.sub(second[0], first[0]))
    across = geometry.unit(geometry.cross(normal, tangent))
    offsets = [geometry.add(geometry.mul(across, sign * .045), geometry.mul(normal, height))
               for sign, height in ((-1, 0.), (1, 0.), (1, .06), (-1, .06))]
    rings = [[(geometry.add(sample[0], offset), sample[1]) for offset in offsets] for sample in (first, second)]
    center = geometry.add(geometry.mul(geometry.add(first[0], second[0]), .5), geometry.mul(normal, .03))
    for index in range(4):
        j = (index + 1) % 4
        samples = (rings[0][index], rings[1][index], rings[1][j], rings[0][j])
        outward = geometry.cross(geometry.sub(samples[1][0], samples[0][0]), geometry.sub(samples[2][0], samples[0][0]))
        middle = tuple(sum(sample[0][axis] for sample in samples) / 4 for axis in range(3))
        if geometry.dot(outward, geometry.sub(middle, center)) < 0:
            outward = geometry.mul(outward, -1)
        _polygon(mesh, samples, texture, outward)
    _polygon(mesh, rings[0], texture, geometry.mul(tangent, -1))
    _polygon(mesh, list(reversed(rings[1])), texture, tangent)


def _boundary(original, indices):
    edges = {}
    for face_index in indices:
        samples, normal, texture = _triangle(original, face_index)
        for first, second in zip(samples, samples[1:] + samples[:1]):
            edges.setdefault(tuple(sorted((first[0], second[0]))), []).append((first, second, normal, texture))
    return [candidates[0] for candidates in edges.values() if len(candidates) == 1]


def _project(original, face_indices, point):
    """Recover the source triangle's UV at an unchanged physical roof point."""
    for face_index in face_indices:
        samples, _, _ = _triangle(original, face_index)
        a, b, c = [sample[0] for sample in samples]
        first, second, offset = geometry.sub(b, a), geometry.sub(c, a), geometry.sub(point, a)
        aa, ab, bb = geometry.dot(first, first), geometry.dot(first, second), geometry.dot(second, second)
        determinant = aa * bb - ab * ab
        beta = (bb * geometry.dot(offset, first) - ab * geometry.dot(offset, second)) / determinant
        gamma = (aa * geometry.dot(offset, second) - ab * geometry.dot(offset, first)) / determinant
        weights = (1 - beta - gamma, beta, gamma)
        if min(weights) >= -1e-7:
            uv = tuple(sum(weight * sample[1][axis] for weight, sample in zip(weights, samples)) for axis in range(2))
            return point, uv
    raise ValueError('CBO4 ridge detail must project inside its original roof tile')


def _ridge(mesh, original):
    for ridge_indices, front_indices, rear_indices, front_faces, rear_faces in (
            ((10, 1), (0, 2), (6, 7), (0, 4), (2, 6)),
            ((1, 3), (2, 11), (7, 15), (1, 5), (3, 7))):
        rings = []
        texture = struct.unpack_from('<h', original.faces[front_faces[0]], 8)[0]
        for ridge_index, front_index, rear_index in zip(ridge_indices, front_indices, rear_indices):
            ridge = original.vertices[ridge_index][:3]
            front = geometry.interpolate(ridge, original.vertices[front_index][:3], .03)
            rear = geometry.interpolate(ridge, original.vertices[rear_index][:3], .03)
            top = _project(original, front_faces, ridge)
            rings.append((_project(original, front_faces, front),
                          (geometry.add(top[0], (0., 0., -.08)), top[1]), _project(original, rear_faces, rear)))
        for index, normal in ((0, (0., -1., -1.)), (1, (0., 1., -1.))):
            _polygon(mesh, (rings[0][index], rings[1][index], rings[1][index + 1], rings[0][index + 1]), texture, normal)
        _polygon(mesh, rings[0], texture, (-1., 0., 0.))
        _polygon(mesh, list(reversed(rings[1])), texture, (1., 0., 0.))


def _sign_backing(mesh, original):
    # The original red board is a two-triangle sheet. Give it an actual back
    # and closed ends, sampling only its unlettered red margin on the reverse.
    for face_index in (16, 17):
        samples, normal, texture = _triangle(original, face_index)
        back = [(geometry.add(point, geometry.mul(normal, -.07)), SIGN_BLANK_UV) for point, _ in samples]
        _polygon(mesh, back, texture, geometry.mul(normal, -1))
    for first, second, normal, texture in _boundary(original, (16, 17)):
        a = (geometry.add(first[0], geometry.mul(normal, -.07)), SIGN_BLANK_UV)
        b = (geometry.add(second[0], geometry.mul(normal, -.07)), SIGN_BLANK_UV)
        edge = geometry.sub(second[0], first[0])
        outward = geometry.cross(edge, normal)
        _polygon(mesh, ((first[0], SIGN_BLANK_UV), a, b, (second[0], SIGN_BLANK_UV)), texture, outward)


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    """Return this exact CBO4 package, or preserve an unmatched/budgeted source."""
    stats = dict(donington_cbo4=0, cbo4_roof_edges=0,
                 cbo4_ridge_sections=0, cbo4_window_frames=0, cbo4_sign_backings=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    edges = _boundary(original, range(8))
    for first, second, normal, texture in edges:
        _prism(mesh, first, second, normal, texture)
    _ridge(mesh, original)
    for x0, y0, x1, y1 in WINDOWS:
        _window_panel(mesh, original, FRONT_WINDOW_FACES, (x0 / 64, y0 / 16 - 1, x1 / 64, y1 / 16 - 1))
    _sign_backing(mesh, original)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_cbo4=1, cbo4_roof_edges=len(edges),
                 cbo4_ridge_sections=2, cbo4_window_frames=6, cbo4_sign_backings=1, added_triangles=added)
    return mesh, stats


def sign_backing(mesh, original, faces, blank_uv):
    # The four-triangle front forms two adjacent source panels. Reverse and
    # perimeter sample the unlettered red margin, retaining the original face UVs.
    for face_index in faces:
        samples, normal, texture = _triangle(original, face_index)
        back = [(geometry.add(point, geometry.mul(normal, -.07)), blank_uv) for point, _ in samples]
        _polygon(mesh, back, texture, geometry.mul(normal, -1))
    for first, second, normal, texture in _boundary(original, faces):
        a = (geometry.add(first[0], geometry.mul(normal, -.07)), blank_uv)
        b = (geometry.add(second[0], geometry.mul(normal, -.07)), blank_uv)
        outward = geometry.cross(geometry.sub(second[0], first[0]), normal)
        _polygon(mesh, ((first[0], blank_uv), a, b, (second[0], blank_uv)), texture, outward)
