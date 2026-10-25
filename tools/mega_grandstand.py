"""Donington DA02GST_1: simple roof framing, closed roof edges and coarse stair flights.

The shared GP/Short source has five DONINGTON fascia panels, an inclined crowd
surface, a framed green roof underside and three painted stair flights on its
brown base. Details follow those source UV features; no new architectural style,
stand placement, collision shape or material is introduced. All original broad
surfaces, crowd geometry and their lighting/UV records remain exact.
"""

import math
import struct

import vehicle_model as geometry


VERTICES_CRC32 = 'ea7bcc19'
TOPOLOGY_CRC32 = '665280f1'
MATERIALS = ('DA02GSD5', 'DA02GSD2', 'DA02GSD6', 'DA02GSD9', 'DA02GSD4', 'DA28COM9')
WHITE = (255, 255, 255, 255)
# RGB column means of the canonical64x64 GSD6, rows4..59, peak at
#11/12,24/25,37/38,50/51. The0/63 edge frame is covered by the roof edge
# profiles and keeps its source pixels; do not mistake dark field centers for
# structural beams.
BEAM_U_PIXELS = (11.5, 24.5, 37.5, 50.5)
# Three source-painted stair flights; coarse treads retain their shape while
# the accepted image carries the original closely spaced stair markings.
STAIRS = (((47, 48), (40., 5.), (62., 17.), 4),
          ((49, 50), (33., 46.), (60., 61.), 4),
          ((53, 54), (1.5, 13.), (11.5, 2.), 3))


def _names(texture_names):
    return [(name.decode('ascii') if isinstance(name, bytes) else name).rstrip('\0').upper()
            for name in texture_names]


def matches(original, texture_names):
    if original.header[:12].rstrip(b'\0') != b'DA02GST_1' or len(original.vertices) != 136 or len(original.faces) != 80:
        return False
    vertices = b''.join(struct.pack('<8i', *(round(component * 65536) for component in vertex))
                        for vertex in original.vertices)
    topology = b''.join(face[:8] + face[10:] for face in original.faces)
    if geometry.crc32(vertices) != VERTICES_CRC32 or geometry.crc32(topology) != TOPOLOGY_CRC32:
        return False
    names = _names(texture_names)
    expected = (('DA02GSD5',) * 10 + ('DA02GSD2',) * 20 + ('DA02GSD6',) * 17 +
                ('DA02GSD9',) * 8 + ('DA02GSD4',) * 18 + ('DA28COM9',) * 7)
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _uv(vertex):
    return (max(.02, min(.99, vertex[3] - (1 if vertex[3] > 1 else 0))),
            max(.02, min(.99, vertex[4] + 1)) - 1)


def _triangle(original, face_index):
    face = original.faces[face_index]
    vertices = [original.vertices[index] for index in struct.unpack_from('<3H', face, 2)]
    points = [(vertex[:3], _uv(vertex)) for vertex in vertices]
    normal = geometry.unit(geometry.cross(geometry.sub(vertices[1][:3], vertices[0][:3]),
                                          geometry.sub(vertices[2][:3], vertices[0][:3])))
    source_normal = tuple(sum(vertex[axis] for vertex in vertices) for axis in (5, 6, 7))
    if geometry.dot(normal, source_normal) < 0:
        points.reverse()
        normal = geometry.mul(normal, -1)
    return points, normal, struct.unpack_from('<h', face, 8)[0]


def _interpolate(a, b, fraction):
    return (geometry.interpolate(a[0], b[0], fraction), geometry.interpolate(a[1], b[1], fraction))


def _clip(samples, axis, boundary, greater):
    """Clip a source triangle in its actual inset UV map, carrying positions."""
    result = []
    for first, second in zip(samples, samples[1:] + samples[:1]):
        a, b = first[1][axis] - boundary, second[1][axis] - boundary
        inside_a = a >= -1e-12 if greater else a <= 1e-12
        inside_b = b >= -1e-12 if greater else b <= 1e-12
        if inside_a:
            result.append(first)
        if inside_a != inside_b:
            result.append(_interpolate(first, second, a / (a - b)))
    # An exact UV corner may otherwise occur twice after two clipping passes.
    cleaned = []
    for sample in result:
        if not cleaned or math.dist(sample[0], cleaned[-1][0]) > 1e-9:
            cleaned.append(sample)
    if len(cleaned) > 1 and math.dist(cleaned[0][0], cleaned[-1][0]) < 1e-9:
        cleaned.pop()
    return cleaned


def _polygon(mesh, samples, texture, normal=None):
    geometry.polygon(mesh, [sample[0] for sample in samples], WHITE, normal,
                     [sample[1] for sample in samples], texture)


def _raised(mesh, samples, normal, texture, height):
    front = [(geometry.add(point, geometry.mul(normal, height)), uv) for point, uv in samples]
    _polygon(mesh, front, texture, normal)
    for index in range(len(samples)):
        next_index = (index + 1) % len(samples)
        _polygon(mesh, (samples[index], samples[next_index], front[next_index], front[index]), texture)


def _bands(mesh, original, face_indices, axis, centers, half_width, height):
    count = 0
    for face_index in face_indices:
        samples, normal, texture = _triangle(original, face_index)
        for center in centers:
            clipped = _clip(samples, axis, center - half_width, True)
            clipped = _clip(clipped, axis, center + half_width, False) if clipped else []
            if len(clipped) < 3:
                continue
            area = geometry.cross(geometry.sub(clipped[1][0], clipped[0][0]), geometry.sub(clipped[2][0], clipped[0][0]))
            if geometry.dot(area, area) < 1e-14:
                continue
            _raised(mesh, clipped, normal, texture, height)
            count += 1
    return count


def _sample(original, face_indices, uv):
    for face_index in face_indices:
        samples, normal, texture = _triangle(original, face_index)
        a, b, c = [sample[1] for sample in samples]
        first, second, point = geometry.sub(b, a), geometry.sub(c, a), geometry.sub(uv, a)
        determinant = first[0] * second[1] - first[1] * second[0]
        beta = (point[0] * second[1] - point[1] * second[0]) / determinant
        gamma = (first[0] * point[1] - first[1] * point[0]) / determinant
        weights = (1 - beta - gamma, beta, gamma)
        if min(weights) < -1e-8:
            continue
        position = tuple(sum(weight * sample[0][axis] for weight, sample in zip(weights, samples)) for axis in range(3))
        return (position, uv), normal, texture
    raise ValueError('Grandstand detail lies outside its identified source UV panel')


def _stairs(mesh, original):
    count = 0
    for faces, start, end, steps in STAIRS:
        for step in range(steps):
            first = geometry.interpolate(start, end, step / steps)
            second = geometry.interpolate(start, end, (step + 1) / steps)
            u0, u1 = first[0] / 64, second[0] / 64
            v0, v1 = min(first[1], second[1]) / 64 - 1, (max(first[1], second[1]) + 1.2) / 64 - 1
            samples = [_sample(original, faces, uv) for uv in ((u0, v0), (u1, v0), (u1, v1), (u0, v1))]
            points, normal, texture = [sample[0] for sample in samples], samples[0][1], samples[0][2]
            cross = geometry.cross(geometry.sub(points[1][0], points[0][0]), geometry.sub(points[2][0], points[0][0]))
            if geometry.dot(cross, normal) < 0:
                points.reverse()
            _raised(mesh, points, normal, texture, .14)
            count += 1
    return count


def _edge_prism(mesh, first, second, normal, texture, width=.045, depth=.04):
    tangent = geometry.unit(geometry.sub(second[0], first[0]))
    across = geometry.unit(geometry.cross(normal, tangent))
    offsets = [geometry.add(geometry.mul(across, sign * width / 2), geometry.mul(normal, height))
               for sign, height in ((-1, 0.), (1, 0.), (1, depth), (-1, depth))]
    rings = [[(geometry.add(sample[0], offset), sample[1]) for offset in offsets] for sample in (first, second)]
    center = geometry.mul(geometry.add(first[0], second[0]), .5)
    center = geometry.add(center, geometry.mul(normal, depth / 2))
    for index in range(4):
        j = (index + 1) % 4
        samples = (rings[0][index], rings[1][index], rings[1][j], rings[0][j])
        face_normal = geometry.cross(geometry.sub(samples[1][0], samples[0][0]), geometry.sub(samples[2][0], samples[0][0]))
        middle = tuple(sum(sample[0][axis] for sample in samples) / 4 for axis in range(3))
        if geometry.dot(face_normal, geometry.sub(middle, center)) < 0:
            face_normal = geometry.mul(face_normal, -1)
        _polygon(mesh, samples, texture, face_normal)
    _polygon(mesh, rings[0], texture, geometry.mul(tangent, -1))
    _polygon(mesh, list(reversed(rings[1])), texture, tangent)


def _roof_edges(mesh, original):
    edges = {}
    for face_index in range(73, 80):
        samples, normal, texture = _triangle(original, face_index)
        for first, second in zip(samples, samples[1:] + samples[:1]):
            key = tuple(sorted((first[0], second[0])))
            edges.setdefault(key, []).append((first, second, normal, texture))
    count = 0
    for candidates in edges.values():
        if len(candidates) != 1:
            continue
        first, second, normal, texture = candidates[0]
        _edge_prism(mesh, first, second, normal, texture)
        count += 1
    return count


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    """Append the complete identified package within the caller's family budget."""
    stats = dict(donington_grandstands=0, grandstand_beam_patches=0, grandstand_roof_course_patches=0,
                 grandstand_fascia_frames=0, grandstand_stair_nosings=0, grandstand_roof_edges=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    beams = _bands(mesh, original, range(30, 47), 0, [value / 64 for value in BEAM_U_PIXELS], .7 / 64, .08)
    stairs = _stairs(mesh, original)
    edges = _roof_edges(mesh, original)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_grandstands=1, grandstand_beam_patches=beams, grandstand_roof_course_patches=0,
                 grandstand_fascia_frames=0, grandstand_stair_nosings=stairs, grandstand_roof_edges=edges, added_triangles=added)
    return mesh, stats
