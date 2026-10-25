"""Source-gated Thruxton parked aircraft, in the original local -Z-up space.

Keep the authored fuselage, glazing, fins and stationary propeller panels.
Only the thin wing skins and photographic landing-gear panels are replaced.
The broad painted faces keep source UV anchors; closing faces sample the same
original material rather than painting over its alpha silhouette.
"""

import math
import struct
from collections import Counter

import mega_aircraft
import vehicle_model as geometry


CONTRACTS = {
    'TH36APLN_1': {
        'vertices': 103, 'triangles': 68,
        'vertex_crc32': '613f3bf0', 'topology_crc32': '0ad2f061',
        'materials': (('TH36AI1', 12), ('TH36AIR', 16), ('TH36AIR3', 6),
                      ('TH36AIR4', 4), ('TH36AIR5', 8), ('TH36AIR7', 14),
                      ('TH36AIR6', 8)),
        # Each patch gives replaced faces, upper anchor triangles, lower UV
        # triangles and inward thickness. Coordinates come from those faces.
        'patches': (
            ('main-right', tuple(range(48, 53)), (49, 50), (48, 51), .035),
            ('main-left', tuple(range(53, 58)), (53, 56), (54, 55), .035),
            ('tail-right', (58, 60, 61, 62, 63), (62, 63), (60, 61), .025),
            ('tail-left', (59, 64, 65, 66, 67), (66, 67), (64, 65), .025),
        ),
    },
    'TH37APLN_1': {
        'vertices': 112, 'triangles': 76,
        'vertex_crc32': '7fd9cc93', 'topology_crc32': 'f727925b',
        'materials': (('TH36AI1', 12), ('TH36AIR7', 18), ('TH36AIR4', 2),
                      ('TH36AIR6', 8), ('TH36AIR5', 14), ('TH36AIR3', 2),
                      ('TH37AIR', 20)),
        'patches': (
            ('main-left', tuple(range(14, 19)), (14, 15), (16, 17), .035),
            ('main-right', tuple(range(19, 24)), (19, 20), (21, 22), .035),
            ('tail-left', tuple(range(32, 36)), (34, 35), (32, 33), .025),
            ('tail-right', tuple(range(36, 40)), (38, 39), (36, 37), .025),
        ),
    },
}

RUBBER = (43, 43, 39, 255)
STEEL = (150, 151, 143, 255)


def _names(texture_names):
    return tuple((name.decode('ascii') if isinstance(name, bytes) else name)
                 .rstrip('\0').upper() for name in texture_names)


def source_identity(original):
    vertices = b''.join(struct.pack('<8i', *(round(x * 65536) for x in v))
                        for v in original.vertices)
    topology = b''.join(face[:8] + face[10:] for face in original.faces)
    return geometry.crc32(vertices), geometry.crc32(topology)


def matches(original, texture_names):
    name = original.header[:12].rstrip(b'\0').decode('ascii')
    contract = CONTRACTS.get(name)
    if not contract or (len(original.vertices), len(original.faces)) != (
            contract['vertices'], contract['triangles']):
        return False
    if source_identity(original) != (contract['vertex_crc32'], contract['topology_crc32']):
        return False
    names = _names(texture_names)
    expected = tuple(material for material, count in contract['materials'] for _ in range(count))
    slots = (struct.unpack_from('<h', face, 8)[0] for face in original.faces)
    return all(0 <= slot < len(names) and names[slot] == material
               for slot, material in zip(slots, expected))


def _face_vertices(original, index):
    return tuple(original.vertices[i] for i in struct.unpack_from('<3H', original.faces[index], 2))


def _normal(points):
    return geometry.unit(geometry.cross(geometry.sub(points[1], points[0]),
                                        geometry.sub(points[2], points[0])))


def _triangle(mesh, points, uvs, texture, color, normal=None, normals=None):
    normal = normal or _normal(points)
    indices = tuple(mesh.vertex(point, vertex_normal, uv)
                    for point, uv, vertex_normal in zip(points, uvs, normals or (normal,) * 3))
    mesh.triangle(indices, color, texture)


def _quad(mesh, points, uvs, texture, color, normal):
    _triangle(mesh, points[:3], uvs[:3], texture, color, normal)
    _triangle(mesh, (points[0], points[2], points[3]),
              (uvs[0], uvs[2], uvs[3]), texture, color, normal)


def _paint(original, mesh_type, names, faces, normals=None):
    vertices = list(original.vertices)
    for index, normal in (normals or {}).items():
        vertices[index] = (*vertices[index][:5], *normal)
    return mega_aircraft.Paint(mesh_type(original.header, vertices,
                                        [original.faces[i] for i in faces]), names)


def _skin_normals(original, faces, side):
    """Repair only normals contaminated by the opposite source skin.

    Some source indices are shared by upper and lower surfaces. Preserve
    correctly sided source smoothing, but reconstruct a wrong-sided vertex
    from the incident geometric normals of this skin alone.
    """
    incident = {}
    indices = set()
    for index in faces:
        vertices = _face_vertices(original, index)
        normal = _normal([v[:3] for v in vertices])
        for vertex in vertices:
            incident.setdefault(vertex[:3], []).append(normal)
        indices.update(struct.unpack_from('<3H', original.faces[index], 2))
    result = {}
    for index in indices:
        vertex = original.vertices[index]
        normal = vertex[5:]
        if normal[2] * side < 0:
            normal = geometry.unit(tuple(sum(n[axis] for n in incident[vertex[:3]])
                                         for axis in range(3)))
        result[index] = normal
    return result


def _project(paint, point, normal):
    face = paint.choose(point, normal)
    weights = paint.weights(face, point, clamp=False)
    return tuple(max(min(v[axis] for v in face[0]),
                     min(max(v[axis] for v in face[0]),
                         sum(weights[j] * face[0][j][axis] for j in range(3))))
                 for axis in (3, 4))


def _project_normal(paint, point, normal):
    face = paint.choose(point, normal)
    # Thickness can move a corner slightly outside its source triangle.
    # Keep normals a convex interpolation of that skin, not an extrapolation
    # through its boundary into the opposite normal hemisphere.
    weights = paint.weights(face, point)
    return geometry.unit(tuple(sum(weights[j] * face[0][j][axis] for j in range(3))
                               for axis in (5, 6, 7)))


def _wing_patch(mesh, original, mesh_type, names, upper, lower, depth):
    """Close the actual two-triangle source patch, never its bounding box.

    Upper XYZ/UV anchors are copied from source triangles. Undersides project
    onto the original underside triangles, and edge UVs interpolate between
    those two source samples. Transparent source regions remain transparent.
    """
    upper_normals = _skin_normals(original, upper, -1)
    below = _paint(original, mesh_type, names, lower,
                   _skin_normals(original, lower, 1))
    boundaries = {}
    counts = Counter()
    for index in upper:
        source = original.faces[index]
        records = [(*original.vertices[i][:5], *upper_normals[i])
                   for i in struct.unpack_from('<3H', source, 2)]
        points = [v[:3] for v in records]
        if _normal(points)[2] > 0:
            records[1], records[2] = records[2], records[1]
            points = [v[:3] for v in records]
        normal = _normal(points)
        uvs = [v[3:5] for v in records]
        texture, = struct.unpack_from('<h', source, 8)
        color = tuple(source[10:14])
        _triangle(mesh, points, uvs, texture, color, normal,
                  normals=[v[5:] for v in records])
        bottom = [geometry.add(point, (0., 0., depth)) for point in points]
        bottom_normal = geometry.mul(normal, -1)
        bottom_uv = [_project(below, point, bottom_normal) for point in bottom]
        _triangle(mesh, bottom, bottom_uv, texture, color, bottom_normal,
                  normals=[_project_normal(below, point, (0., 0., 1.)) for point in bottom])
        for i, j in ((0, 1), (1, 2), (2, 0)):
            key = tuple(sorted((points[i], points[j])))
            counts[key] += 1
            boundaries[key] = (points[i], points[j], uvs[i], uvs[j],
                               bottom[i], bottom[j], bottom_uv[i], bottom_uv[j],
                               texture, color, normal)
    for key, count in counts.items():
        if count != 1:
            continue
        a, b, ua, ub, da, db, uda, udb, texture, color, top_normal = boundaries[key]
        outward = geometry.unit(geometry.cross(geometry.sub(b, a), top_normal))
        _quad(mesh, (a, b, db, da), (ua, ub, udb, uda), texture, color, outward)


def _landing_gear(mesh, original, mesh_type, names):
    """Three source wheel centers, textured disc ends and plain rubber walls."""
    groups = {}
    for index in range(12):
        vertices = _face_vertices(original, index)
        groups.setdefault(vertices[0][0], []).append(index)
    details = []
    for x, faces in sorted(groups.items()):
        first = len(mesh.faces)
        vertices = [v for index in faces for v in _face_vertices(original, index)]
        y0, y1 = min(v[1] for v in vertices), max(v[1] for v in vertices)
        top, ground = min(v[2] for v in vertices), max(v[2] for v in vertices)
        y = (y0 + y1) * .5
        radius = (y1 - y0) * .4
        half_width = radius * .24
        center_z = ground - radius
        texture, = struct.unpack_from('<h', original.faces[faces[0]], 8)
        color = tuple(original.faces[faces[0]][10:14])
        paint = _paint(original, mesh_type, names, faces)
        rings = []
        for side in (-1, 1):
            ring = []
            for i in range(12):
                angle = math.tau * i / 12
                ring.append((x + side * half_width, y + math.cos(angle) * radius,
                             center_z + math.sin(angle) * radius))
            rings.append(ring)
            normal = (side, 0., 0.)
            center = (x + side * half_width, y, center_z)
            # Project at the original panel X, so extra width does not shift
            # the tyre/hub crop or accidentally choose a neighbouring island.
            uv_center = _project(paint, (x, y, center_z), normal)
            for i in range(12):
                j = (i + 1) % 12
                uv_a = _project(paint, (x, ring[i][1], ring[i][2]), normal)
                uv_b = _project(paint, (x, ring[j][1], ring[j][2]), normal)
                _triangle(mesh, (center, ring[i], ring[j]),
                          (uv_center, uv_a, uv_b), texture, color, normal)
        for i in range(12):
            j = (i + 1) % 12
            outward = (0., (rings[0][i][1] + rings[0][j][1]) * .5 - y,
                       (rings[0][i][2] + rings[0][j][2]) * .5 - center_z)
            _quad(mesh, (rings[0][i], rings[1][i], rings[1][j], rings[0][j]),
                  ((0., -1.),) * 4, -1, RUBBER, geometry.unit(outward))
        first_leg = len(mesh.faces)
        geometry.cylinder(mesh, (x, y, top), (x, y, center_z), radius * .16,
                          STEEL, segments=6)
        details.append(dict(center=(x, y, center_z), radius=radius,
                            width=half_width * 2, source_faces=faces,
                            wheel_face_range=(first, first_leg),
                            leg_face_range=(first_leg, len(mesh.faces))))
    return details


def _build(original, mesh_type, texture_names, triangle_budget):
    result = mesh_type(original.header, list(original.vertices), list(original.faces))
    empty = dict(added_triangles=0, rebuilt_static_aircraft=0)
    if not matches(original, texture_names):
        return result, empty
    name = original.header[:12].rstrip(b'\0').decode('ascii')
    contract = CONTRACTS[name]
    names = _names(texture_names)
    replaced = set(range(12))
    for _, faces, _, _, _ in contract['patches']:
        replaced.update(faces)
    result.faces = [face for i, face in enumerate(original.faces) if i not in replaced]
    parts = []
    for label, faces, upper, lower, depth in contract['patches']:
        first = len(result.faces)
        _wing_patch(result, original, mesh_type, names, upper, lower, depth)
        parts.append(dict(name=label, source_faces=faces,
                          output_face_range=(first, len(result.faces)), thickness=depth))
    first_gear = len(result.faces)
    gear = _landing_gear(result, original, mesh_type, names)
    added = len(result.faces) - len(original.faces)
    if added > triangle_budget:
        return mesh_type(original.header, list(original.vertices), list(original.faces)), empty
    return result, dict(added_triangles=added, rebuilt_static_aircraft=1,
                        source_triangles=len(original.faces), replaced_faces=sorted(replaced),
                        preserved_faces=[i for i in range(len(original.faces)) if i not in replaced],
                        closed_wing_parts=parts, landing_gear=gear,
                        gear_face_range=(first_gear, len(result.faces)))


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    """Source-gated geometry and numeric counters for the common build pipeline."""
    result, details = _build(original, mesh_type, texture_names, triangle_budget)
    return result, dict(added_triangles=details['added_triangles'],
                        rebuilt_static_aircraft=details['rebuilt_static_aircraft'],
                        closed_wing_parts=len(details.get('closed_wing_parts', ())),
                        aircraft_wheels=len(details.get('landing_gear', ())),
                        aircraft_legs=len(details.get('landing_gear', ())))


def describe(original, mesh_type, texture_names, triangle_budget=2048):
    """Separate source/output face correspondence for targeted audit and checks."""
    return _build(original, mesh_type, texture_names, triangle_budget)[1]
