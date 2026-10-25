"""Source-gated CRAN3 parked access-platform wheel relief and underside.

The cab, body, raised articulated boom and open work basket retain their source
shape. Six actual photographic wheel discs gain a small closed relief joined
to the original body, with the existing artwork/UVs on their outer faces.
"""

import math
import struct
from collections import Counter, defaultdict

import vehicle_model as geometry


VERTEX_CRC = 'bbc2e55f'
TOPOLOGY_CRC = '64771e20'
RELIEF = .03
INNER_PHOTO_RADIUS = .88
RUBBER = (35, 36, 35, 255)
UNDERSIDE = (43, 45, 44, 255)
# Static terrain lighting does not multiply untextured face RGB. Reuse this
# opaque dark tyre pixel in CRANE4B instead of adding a texture/material slot.
DARK_UV = (16.5 / 128, -1 + 14.5 / 32)
BODY_GROUPS = {
    'cab': tuple(range(8)) + (64, 65),
    'chassis': tuple(range(16, 38)),
}
# Same original image wheel crops, appearing at three actual axle positions.
# The small repeated dark middle crop is deliberately not another wheel.
WHEELS = (
    ('cab-left', 'cab', (0, 4), -1, 106 / 128),
    ('cab-right', 'cab', (2, 3), 1, 106 / 128),
    ('body-front-left', 'chassis', (36, 37), -1, 14 / 128),
    ('body-front-right', 'chassis', (24, 25), 1, 14 / 128),
    ('body-rear-left', 'chassis', (32, 33), -1, 14 / 128),
    ('body-rear-right', 'chassis', (28, 29), 1, 14 / 128),
)


def source_identity(mesh):
    vertices = b''.join(struct.pack('<8i', *(round(x * 65536) for x in v))
                        for v in mesh.vertices)
    topology = b''.join(face[:8] + face[10:] for face in mesh.faces)
    return geometry.crc32(vertices), geometry.crc32(topology)


def matches(mesh, texture_names):
    if mesh.header[:12].rstrip(b'\0') != b'CRAN3_1' or (
            len(mesh.vertices), len(mesh.faces)) != (136, 66):
        return False
    if source_identity(mesh) != (VERTEX_CRC, TOPOLOGY_CRC):
        return False
    names = [(n.decode('ascii') if isinstance(n, bytes) else n).rstrip('\0').upper()
             for n in texture_names]
    return all(0 <= (slot := struct.unpack_from('<h', face, 8)[0]) < len(names)
               and names[slot] == ('CRANE4B' if i < 64 else 'CRANF4B')
               for i, face in enumerate(mesh.faces))


def _records(mesh, face):
    return [mesh.vertices[i] for i in struct.unpack_from('<3H', mesh.faces[face], 2)]


def _weights(records, point, axes):
    a, b = axes
    x, y = point
    p, q, r = records
    denominator = (q[a] - p[a]) * (r[b] - p[b]) - (q[b] - p[b]) * (r[a] - p[a])
    u = ((x - p[a]) * (r[b] - p[b]) - (y - p[b]) * (r[a] - p[a])) / denominator
    v = ((q[a] - p[a]) * (y - p[b]) - (q[b] - p[b]) * (x - p[a])) / denominator
    return 1 - u - v, u, v


def _at_uv(mesh, faces, uv):
    candidates = []
    for face in faces:
        records = _records(mesh, face)
        weights = _weights(records, uv, (3, 4))
        candidates.append((sum(max(0., -w) for w in weights), records, weights))
    _, records, weights = min(candidates, key=lambda c: c[0])
    return tuple(sum(weights[j] * records[j][axis] for j in range(3)) for axis in range(3))


def _clip(polygon, a, b, inside):
    if not polygon:
        return []
    sign = 1 if inside else -1
    distance = lambda p: sign * ((b[0] - a[0]) * (p[2] - a[1]) - (b[1] - a[1]) * (p[1] - a[0]))
    output = []
    previous, before = polygon[-1], distance(polygon[-1])
    for current in polygon:
        after = distance(current)
        if (before >= 0) != (after >= 0):
            fraction = before / (before - after)
            output.append(tuple(round(x + (y - x) * fraction, 12)
                                for x, y in zip(previous, current)))
        if after >= 0:
            output.append(current)
        previous, before = current, after
    clean = []
    for record in output:
        if not clean or any(abs(x - y) > 1e-10 for x, y in zip(record[:3], clean[-1][:3])):
            clean.append(record)
    if len(clean) > 1 and all(abs(x - y) <= 1e-10 for x, y in zip(clean[0][:3], clean[-1][:3])):
        clean.pop()
    return clean


def _partition(records, ring):
    remaining, outside = records, []
    for a, b in zip(ring, ring[1:] + ring[:1]):
        fragment = _clip(remaining, a, b, False)
        if len(fragment) >= 3:
            outside.append(fragment)
        remaining = _clip(remaining, a, b, True)
    return outside, remaining


def _triangle(mesh, records, normal, texture, color, shift=0., uv=None):
    points = [(r[0] + shift, r[1], r[2]) for r in records]
    cross = geometry.cross(geometry.sub(points[1], points[0]), geometry.sub(points[2], points[0]))
    if geometry.dot(cross, cross) <= 1e-18:
        return
    indices = [mesh.vertex(point, normal, uv if uv is not None else record[3:5])
               for point, record in zip(points, records)]
    mesh.triangle(indices, color, texture)


def _polygon(mesh, records, normal, texture, color, shift=0., uv=None):
    for i in range(1, len(records) - 1):
        _triangle(mesh, (records[0], records[i], records[i + 1]), normal, texture, color, shift, uv)


def _key(point):
    return tuple(round(x, 8) for x in point[:3])


def _boundary(mesh, faces):
    counts, records = Counter(), {}
    for face in faces:
        vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', mesh.faces[face], 2)]
        for a, b in zip(vertices, vertices[1:] + vertices[:1]):
            key = tuple(sorted((_key(a), _key(b))))
            counts[key] += 1
            records[key] = a, b
    return [records[key] for key, count in counts.items() if count == 1]


def conform_surface_edges(mesh, faces):
    """Split existing triangle edges at clip intersections, retaining UVs.

    Subtracting a disc partitions the adjacent source triangles differently.
    Make their collinear edge points explicit on both sides so the resulting
    body is manifold, rather than leaving invisible T junctions.
    """
    points = {_key(mesh.vertices[i]): mesh.vertices[i][:3]
              for face in faces for i in struct.unpack_from('<3H', mesh.faces[face], 2)}
    result, replacements = [], {}
    for face_id in faces:
        face = mesh.faces[face_id]
        records = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
        polygon = []
        for a, b in zip(records, records[1:] + records[:1]):
            edge = geometry.sub(b[:3], a[:3])
            length2 = geometry.dot(edge, edge)
            splits = [(0., a)]
            for point in points.values():
                fraction = geometry.dot(geometry.sub(point, a[:3]), edge) / length2
                if not 1e-8 < fraction < 1 - 1e-8:
                    continue
                projected = geometry.add(a[:3], geometry.mul(edge, fraction))
                if max(abs(x - y) for x, y in zip(point, projected)) > 1e-8:
                    continue
                splits.append((fraction, (*point, *(a[i] + (b[i] - a[i]) * fraction for i in range(3, 8)))))
            polygon.extend(record for _, record in sorted(splits, key=lambda p: p[0]))
        if len(polygon) == 3:
            result.append(face_id)
            replacements[face_id] = [face_id]
            continue
        normal = geometry.unit(geometry.cross(geometry.sub(records[1][:3], records[0][:3]),
                                              geometry.sub(records[2][:3], records[0][:3])))
        center = tuple(sum(v[i] for v in records) / 3 for i in range(8))
        slot, = struct.unpack_from('<h', face, 8)
        start = len(mesh.faces)
        for a, b in zip(polygon, polygon[1:] + polygon[:1]):
            _triangle(mesh, (center, a, b), normal, slot, tuple(face[10:14]))
        generated = list(range(start, len(mesh.faces)))
        mesh.faces[face_id] = mesh.faces[generated[0]]
        replacement = [face_id] + generated[1:]
        # The first appended triangle now lives in the old face position.
        # Leave no duplicate surface in the final mesh.
        mesh.faces[generated[0]] = None
        result.extend(replacement)
        replacements[face_id] = replacement
    return result, replacements


def wheel_relief(mesh, source, faces, side, center_uv, radius_u, dark_slot, dark_uv,
                 relief=RELIEF, inner_photo_radius=INNER_PHOTO_RADIUS, radius_v=None):
    """Small source-photo wheel relief shared by confirmed static trucks."""
    u, v = center_uv
    center = _at_uv(source, faces, center_uv)
    left = _at_uv(source, faces, (u - radius_u, v))
    right = _at_uv(source, faces, (u + radius_u, v))
    radius_y = abs(right[1] - left[1]) * .5
    records = [v for f in faces for v in _records(source, f)]
    lowest_uv = max(v[4] for v in records)
    bottom = sorted({v[:3] for v in records if abs(v[4] - lowest_uv) < 1e-8}, key=lambda v: v[1])
    if len(bottom) < 2 or abs(bottom[-1][1] - bottom[0][1]) < 1e-12:
        # A source cab can use different image rows at its two lower corners.
        # Its geometric lower edge still determines the unchanged contact.
        lower_edge = max((edge for edge in _boundary(source, faces)
                          if abs(edge[0][1] - edge[1][1]) > 1e-12),
                         key=lambda edge: (edge[0][2] + edge[1][2]) * .5)
        bottom = sorted((lower_edge[0][:3], lower_edge[1][:3]), key=lambda v: v[1])
    fraction = (center[1] - bottom[0][1]) / (bottom[-1][1] - bottom[0][1])
    ground_z = bottom[0][2] + fraction * (bottom[-1][2] - bottom[0][2])
    ground_edge = (bottom[0], bottom[-1])
    radius_z = ground_z - center[2]
    if radius_v is not None:
        top = _at_uv(source, faces, (u, v - radius_v))
        bottom = _at_uv(source, faces, (u, v + radius_v))
        radius_z = abs(bottom[2] - top[2]) * .5
    ring = [(center[1] + radius_y * math.cos(math.tau * i / 12),
             center[2] + radius_z * math.sin(math.tau * i / 12)) for i in range(12)]
    inner_ring = [(center[1] + (y - center[1]) * inner_photo_radius,
                   center[2] + (z - center[2]) * inner_photo_radius) for y, z in ring]
    normal = (side, 0., 0.)
    body_faces, cap_faces, ring_faces = [], [], []
    for face in faces:
        outside, cap = _partition(_records(source, face), ring)
        texture, = struct.unpack_from('<h', source.faces[face], 8)
        color = tuple(source.faces[face][10:14])
        first = len(mesh.faces)
        for fragment in outside:
            _polygon(mesh, fragment, normal, texture, color)
        body_faces.extend(range(first, len(mesh.faces)))
        border, photo = _partition(cap, inner_ring)
        first = len(mesh.faces)
        _polygon(mesh, photo, normal, texture, color, side * relief)
        cap_faces.extend(range(first, len(mesh.faces)))
        first = len(mesh.faces)
        for fragment in border:
            _polygon(mesh, fragment, normal, dark_slot, RUBBER, side * relief, dark_uv)
        ring_faces.extend(range(first, len(mesh.faces)))
    cap_surface, remap = conform_surface_edges(mesh, cap_faces + ring_faces)
    cap_faces = [child for face in cap_faces for child in remap[face]]
    ring_faces = [child for face in ring_faces for child in remap[face]]
    first = len(mesh.faces)
    # Join cap perimeter to its exact original photo-disc perimeter. This is
    # one closed body+wheel surface, without hidden coincident inner caps.
    for a, b in _boundary(mesh, cap_surface):
        c = (b[0] - side * relief, *b[1:])
        d = (a[0] - side * relief, *a[1:])
        wall_normal = geometry.unit((0., (a[1] + b[1]) * .5 - center[1],
                                     (a[2] + b[2]) * .5 - center[2]))
        _polygon(mesh, (a, b, c, d), wall_normal, dark_slot, RUBBER, uv=dark_uv)
    wheel_faces = cap_faces + ring_faces + list(range(first, len(mesh.faces)))
    return body_faces, wheel_faces, dict(center=center, radius_y=radius_y, radius_z=radius_z,
                                       original_ground_z=ground_z, outward_relief=relief,
                                       source_ground_edge=ground_edge,
                                       source_faces=faces, cap_faces=cap_faces,
                                       ring_faces=ring_faces, inner_photo_radius=inner_photo_radius,
                                       tread_faces=list(range(first, len(mesh.faces))))


def close_underside(mesh, faces, dark_slot, dark_uv=DARK_UV):
    """Close the actual original body boundary with its existing dark sampler."""
    boundary = _boundary(mesh, faces)
    neighbours, positions = defaultdict(list), {}
    for a, b in boundary:
        ka, kb = _key(a), _key(b)
        neighbours[ka].append(kb)
        neighbours[kb].append(ka)
        positions[ka], positions[kb] = a[:3], b[:3]
    if not neighbours or any(len(v) != 2 for v in neighbours.values()):
        raise ValueError('CRAN3 body underside must have a single simple boundary')
    first = min(neighbours)
    loop, previous, current = [], None, first
    while current not in loop:
        loop.append(current)
        following = next(v for v in neighbours[current] if v != previous)
        previous, current = current, following
    if current != first or len(loop) != len(neighbours):
        raise ValueError('Unexpected CRAN3 body boundary loops')
    points = [positions[key] for key in loop]
    center = tuple(sum(p[a] for p in points) / len(points) for a in range(3))
    start = len(mesh.faces)
    for a, b in zip(points, points[1:] + points[:1]):
        _triangle(mesh, [(*p, 0., -1.) for p in (center, a, b)],
                  (0., 0., 1.), dark_slot, UNDERSIDE, uv=dark_uv)
    return list(range(start, len(mesh.faces)))


def _build(source, mesh_type, names, triangle_budget):
    output = mesh_type(source.header, list(source.vertices), list(source.faces))
    empty = dict(added_triangles=0, rebuilt_static_platform=0, platform_wheels=0, closed_platform_bodies=0)
    if not matches(source, names):
        return output, empty, {}
    replaced = {f for _, _, faces, _, _ in WHEELS for f in faces}
    kept = [i for i in range(len(source.faces)) if i not in replaced]
    output.faces = [source.faces[i] for i in kept]
    remap = {source_id: output_id for output_id, source_id in enumerate(kept)}
    bodies = {name: [remap[i] for i in faces if i in remap] for name, faces in BODY_GROUPS.items()}
    wheels = []
    dark_slot, = struct.unpack_from('<h', source.faces[0], 8)
    for name, group, faces, side, u in WHEELS:
        body_faces, wheel_faces, details = wheel_relief(output, source, faces, side,
                                                      (u, -1 + 23 / 32), 10.5 / 128,
                                                      dark_slot, DARK_UV)
        bodies[group].extend(body_faces + wheel_faces)
        wheels.append(dict(name=name, body=group, side=side, **details))
    subdivisions = []
    for name, faces in bodies.items():
        bodies[name], mapping = conform_surface_edges(output, faces)
        subdivisions.extend(i for i in kept if remap[i] in mapping and len(mapping[remap[i]]) > 1)
        for wheel in wheels:
            if wheel['body'] == name:
                for key in ('cap_faces', 'ring_faces', 'tread_faces'):
                    wheel[key] = [child for face in wheel[key] for child in mapping[face]]
    # Conforming splits replace faces without leaving the discarded first
    # child as an overlapping copy. Compact only the face array, not vertices.
    face_remap = {}
    compact = []
    for i, face in enumerate(output.faces):
        if face is not None:
            face_remap[i] = len(compact)
            compact.append(face)
    output.faces = compact
    bodies = {name: [face_remap[i] for i in faces] for name, faces in bodies.items()}
    for wheel in wheels:
        for key in ('cap_faces', 'ring_faces', 'tread_faces'):
            wheel[key] = [face_remap[i] for i in wheel[key]]
    underside = {}
    for name, faces in bodies.items():
        underside[name] = close_underside(output, faces, dark_slot)
        faces.extend(underside[name])
    added = len(output.faces) - len(source.faces)
    if added > triangle_budget:
        return mesh_type(source.header, list(source.vertices), list(source.faces)), empty, {}
    return output, dict(added_triangles=added, rebuilt_static_platform=1,
                        platform_wheels=len(wheels), closed_platform_bodies=len(bodies)), \
        dict(replaced_source_faces=sorted(replaced), subdivided_source_faces=subdivisions,
             preserved_source_faces=[i for i in kept if i not in subdivisions],
             wheels=wheels, body_components=bodies, underside_faces=underside)


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    output, stats, _ = _build(original, mesh_type, texture_names, triangle_budget)
    return output, stats


def describe(original, mesh_type, texture_names, triangle_budget=2048):
    return _build(original, mesh_type, texture_names, triangle_budget)[2]
