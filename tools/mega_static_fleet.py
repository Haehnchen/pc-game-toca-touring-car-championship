"""Source-gated static service vehicles; retain photographed bodies and placement.

The car row stays a source billboard. Six existing solid forms gain only shallow
photo-registered tyres and missing underside surfaces. Dark faces sample an
existing opaque image pixel because the static terrain path ignores face RGB.
"""

import math
import struct

import mega_static_platform as surfaces
import vehicle_model as geometry


CONTRACTS = {
    'LVOLVO_1': (56, 34, ('bd1c8624', 'fbcd3a96'), '95c04fdf',
                  (('LVOLVO1', 10), ('LVOLVO2', 4), ('LVOLVO3', 12), ('LVOLVO4', 8))),
    'LAMBULAN_1': (26, 14, ('866743d6', 'ee40c82b'), '89fb504f',
                    (('AMBULAN1', 6), ('AMBULAN2', 2), ('AMBULAN3', 2), ('AMBULAN4', 2), ('AMBULAN5', 2))),
    'LFIRETRK_1': (90, 50, ('46e0b5cc',), '68914b36',
                    (('FIRETRK1', 26), ('FIRETRK2', 2), ('FIRETRK3', 6), ('FIRETRK5', 2),
                     ('FIRETRK6', 2), ('FIRETRK7', 2), ('FIRETRK8', 4), ('FIRETRK9', 4), ('FIRETRK4', 2))),
    'LRACJEEP_1': (76, 44, ('ce4f3cca',), '0579f1f0',
                    (('LRACJP1', 30), *((f'LRACJP{i}', 2) for i in range(2, 9)))),
    'CR11CVAN_1': (60, 38, ('08d031e5',), '16e0dbbe',
                    (('CR11BCK', 4), ('CR11FRT', 4), ('CR11ROF', 6), ('CR11SDE1', 16), ('CR11SDE2', 8))),
    'CR03BUS2_1': (93, 72, ('f7cbddc6',), '60dc3ffc',
                     (('CR03BUSB', 12), ('CR03BUSF', 6), ('CR03BUSS', 50), ('CR03BUST', 4))),
    'LCAR1_1': (4, 4, ('5483b8c9',), '9e62c21d', (('LCARS1', 4),)),
}
SAMPLERS = {'LVOLVO_1': ('LVOLVO4', 32, 32, 30, 16),
            'LAMBULAN_1': ('AMBULAN4', 32, 32, 10, 28),
            'LFIRETRK_1': ('FIRETRK1', 64, 32, 12, 19),
            'LRACJEEP_1': ('LRACJP1', 64, 32, 3, 23),
            'CR11CVAN_1': ('CR11SDE1', 64, 32, 10, 8),
            'CR03BUS2_1': ('CR03BUSS', 128, 64, 25, 50)}
# Each group is a genuine planar source flank. Multiple tyres partition its
# remaining body successively, never duplicate the whole original side.
# Pixel centres/radii refer to the unchanged canonical image registration.
FLANKS = {
    'LVOLVO_1': (('cab-left', 'cab', (10, 13), -1, 32, 64, ((15.5, 57.5, 8.5, 8.5),)),
                  ('cab-right', 'cab', (11, 12), 1, 32, 64, ((15.5, 57.5, 8.5, 8.5),)),
                  ('box-left', 'box', (3, 4, 6, 7), -1, 128, 32, ((47.5, 28.5, 9.5, 4.5),)),
                  ('box-right', 'box', (0, 5, 8, 9), 1, 128, 32, ((47.5, 28.5, 9.5, 4.5),))),
    'LAMBULAN_1': (('side-a', 'body', (0, 1, 2), -1, 64, 32, ((8.5, 28, 5.5, 5.5), (54.5, 28, 5.5, 5.5))),
                    ('side-b', 'body', (3, 4, 5), 1, 64, 32, ((8.5, 28, 5.5, 5.5), (54.5, 28, 5.5, 5.5)))),
    'LFIRETRK_1': (('side-a', 'chassis', (20, 21, 22), 1, 64, 32, ((18, 25, 7.5, 7.5), (56.5, 25, 7.5, 7.5))),
                    ('side-b', 'chassis', (23, 24, 25), -1, 64, 32, ((18, 25, 7.5, 7.5), (56.5, 25, 7.5, 7.5)))),
    'LRACJEEP_1': (('side-a', 'chassis', (4, 5, 16, 17, 18), -1, 64, 32, ((17, 25, 7.5, 7.5), (56.5, 25, 7.5, 7.5))),
                    ('side-b', 'chassis', (6, 7, 19, 20, 21), 1, 64, 32, ((17, 25, 7.5, 7.5), (56.5, 25, 7.5, 7.5)))),
    # Only the visible lower rubber receives relief. The source hub remains
    # at image row28; the mask centre is lower to leave gray arch paint flush.
    'CR11CVAN_1': (('side-a', 'body', (14, 15, 24, 25, 26, 27, 28, 29), 1, 64, 32, ((30.5, 29.5, 5, None),)),
                    ('side-b', 'body', (30, 31, 32, 33, 34, 35, 36, 37), -1, 64, 32, ((34.5, 29.5, 5, None),))),
    'CR03BUS2_1': (('side-a', 'body', (18, 19, 20, 25, 26, 27, 28, 29, 30, 52, 53, 55, 56, 57, 58, 59, 60, 61), 1,
                     128, 64, ((29.5, 57, 7.5, 7.5), (101, 57, 7.5, 7.5))),
                     ('side-b', 'body', (21, 22, 23, 24, 31, 32, 33, 34, 35, 36, 49, 50, 51, 54, 62, 63, 64, 65, 66, 67), -1,
                      128, 64, ((29.5, 57, 7.5, 7.5), (101, 57, 7.5, 7.5)))),
}


def _names(texture_names):
    return [(n.decode('ascii') if isinstance(n, bytes) else n).rstrip('\0').upper() for n in texture_names]


def matches(original, texture_names):
    name = original.header[:12].rstrip(b'\0').decode('ascii')
    contract = CONTRACTS.get(name)
    if contract is None:
        return False
    vc, fc, vertices, topology, roles = contract
    if (len(original.vertices), len(original.faces)) != (vc, fc):
        return False
    vertex_crc, topology_crc = surfaces.source_identity(original)
    if vertex_crc not in vertices or topology_crc != topology:
        return False
    names = _names(texture_names)
    expected = [role for role, count in roles for _ in range(count)]
    return all(0 <= (slot := struct.unpack_from('<h', face, 8)[0]) < len(names)
               and names[slot] == role for face, role in zip(original.faces, expected))


def _rotate(vertex, inverse=False):
    x, y, z, u, v, nx, ny, nz = vertex
    return (y, -x, z, u, v, ny, -nx, nz) if inverse else (-y, x, z, u, v, -ny, nx, nz)


def _groups(name, count):
    if name == 'LVOLVO_1':
        return {'cab': list(range(10, 14)) + [15, 16, 18, 21, 24, 25],
                'box': list(range(10)) + [14, 17, 19, 20, 22, 23]}
    if name in ('LFIRETRK_1', 'LRACJEEP_1'):
        cab = [0, 1, 2, 3, 28, 33, 34, 35, 38, 39] if name == 'LFIRETRK_1' else [0, 1, 2, 3, 32, 33, 36, 37, 40, 41]
        return {'cab': cab, 'chassis': [i for i in range(count) if i not in cab and not (name == 'LFIRETRK_1' and 40 <= i < 44)]}
    return {'body': list(range(count))}


def _inner_floor(mesh, source, name, slot, uv):
    # Original inner wheelwell endpoints define a narrow raised chassis, not
    # a full-width plate at ground level across the alpha-cut wheel bays.
    wheelwell_faces = range(4, 20) if name == 'LFIRETRK_1' else (*range(8, 16), *range(22, 30))
    wheelwell_vertices = [source.vertices[i] for fi in wheelwell_faces
                          for i in struct.unpack_from('<3H', source.faces[fi], 2)]
    xs = sorted({v[0] for v in wheelwell_vertices})
    lo, hi = xs[1], xs[-2]
    height = min(v[2] for v in wheelwell_vertices)
    front = max(v[1] for v in source.vertices)
    if name == 'LRACJEEP_1':
        # Its original front skin leans inward: the global ground-level
        # extremum would put this floor0.6mm through the numberplate plane.
        front = min(source.vertices[i][1] for fi in (34, 35)
                    for i in struct.unpack_from('<3H', source.faces[fi], 2))
    bounds = ((lo, hi), (min(v[1] for v in source.vertices), front), (height, height + .03))
    start = len(mesh.faces)
    for axis in range(3):
        a, b = [i for i in range(3) if i != axis]
        for side in (0, 1):
            points = []
            for sa, sb in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = [0.] * 3
                p[axis], p[a], p[b] = bounds[axis][side], bounds[a][sa], bounds[b][sb]
                points.append(p)
            normal = tuple((1 if side else -1) if i == axis else 0 for i in range(3))
            geometry.polygon(mesh, points, surfaces.UNDERSIDE, normal, [uv] * 4, slot)
    return list(range(start, len(mesh.faces))), bounds


def _flank(mesh, source, faces, side, width, height, landmarks, slot, uv, prebound=False):
    # Partition the original polygons before triangulating. Repeatedly cutting
    # already triangulated outside fragments would add needless internal edges.
    remaining = [(fi, surfaces._records(source, fi)) for fi in faces]
    wheels = []
    for x, y, rx, ry in landmarks:
        temporary = geometry.Mesh(source.header, list(source.vertices), [])
        _, _, data = surfaces.wheel_relief(temporary, source, faces, side,
                                           (x / width, -1 + y / height), rx / width,
                                           slot, uv, radius_v=ry / height if ry is not None else None)
        vertex_remap = {}
        for key in ('cap_faces', 'ring_faces', 'tread_faces'):
            copied = []
            for fi in data[key]:
                face = temporary.faces[fi]
                indices = []
                for old in struct.unpack_from('<3H', face, 2):
                    if old not in vertex_remap:
                        vertex_remap[old] = len(mesh.vertices)
                        mesh.vertices.append(temporary.vertices[old])
                    indices.append(vertex_remap[old])
                copied.append(len(mesh.faces))
                mesh.faces.append(face[:2] + struct.pack('<3H', *indices) + face[8:])
            data[key] = copied
        center = data['center']
        ring = [(center[1] + data['radius_y'] * math.cos(math.tau * i / 12),
                 center[2] + data['radius_z'] * math.sin(math.tau * i / 12)) for i in range(12)]
        # Restrict oblique circle support lines to the actual tyre box.
        # Otherwise their extensions split distant cab corners into slivers.
        cy, cz = center[1:]
        radius_y, radius_z = data['radius_y'], data['radius_z']
        box = ((cy - radius_y, cz - radius_z), (cy + radius_y, cz - radius_z),
               (cy + radius_y, cz + radius_z), (cy - radius_y, cz + radius_z))
        outside = []
        for fi, polygon in remaining:
            far, near = surfaces._partition(polygon, box) if prebound else ([], polygon)
            fragments, _ = surfaces._partition(near, ring)
            outside.extend((fi, fragment) for fragment in far + fragments)
        remaining = outside
        wheels.append(dict(source_pixel_center=(x, y), source_pixel_radius=(rx, ry),
                           vertical_radius_mode='original-ground-contact' if ry is None else 'source-pixel-radius',
                           image_size=(width, height), **data))
    triangles = [(fi, (polygon[0], polygon[i], polygon[i + 1]))
                 for fi, polygon in remaining for i in range(1, len(polygon) - 1)]
    cleanup = dict(prebounded_source_faces=list(faces) if prebound else [])
    for fi, triangle in triangles:
        a, b, c = (v[:3] for v in triangle)
        cross = geometry.cross(geometry.sub(b, a), geometry.sub(c, a))
        area2 = geometry.dot(cross, cross)
        if 1e-18 < area2 <= 1e-14:
            return [], [], dict(**cleanup, fallback_source_face=fi,
                                reason='Nonzero clip fragment cannot form a valid source-preserving face')
    first = len(mesh.faces)
    for fi, polygon in triangles:
        texture, = struct.unpack_from('<h', source.faces[fi], 8)
        surfaces._polygon(mesh, polygon, (side, 0., 0.), texture, tuple(source.faces[fi][10:14]))
    return list(range(first, len(mesh.faces))), wheels, cleanup


def _build(original, mesh_type, texture_names, triangle_budget):
    empty = dict(added_triangles=0, rebuilt_static_fleet=0, fleet_wheels=0, closed_fleet_undersides=0)
    copy = lambda: mesh_type(original.header, list(original.vertices), list(original.faces))
    if not matches(original, texture_names):
        return copy(), empty, {}
    name = original.header[:12].rstrip(b'\0').decode('ascii')
    if name == 'LCAR1_1':
        return copy(), empty, {'retained_photo_row': True}
    if triangle_budget <= 0:
        return copy(), empty, {}
    rotated = name != 'LVOLVO_1'
    source = mesh_type(original.header, [_rotate(v) if rotated else v for v in original.vertices], list(original.faces))
    output = mesh_type(source.header, list(source.vertices), list(source.faces))
    bodies = _groups(name, len(source.faces))
    sampler, width, height, px, py = SAMPLERS[name]
    dark_slot = _names(texture_names).index(sampler)
    dark_uv = ((px + .5) / width, -1 + (py + .5) / height)
    wheels, replaced, fragment_cleanup = [], set(), []
    for flank, group, source_faces, side, width, height, landmarks in FLANKS[name]:
        replaced.update(source_faces)
        body, new_wheels, cleanup = _flank(output, source, source_faces, side, width, height,
                                           landmarks, dark_slot, dark_uv, prebound=name == 'LFIRETRK_1')
        fragment_cleanup.append(dict(flank=flank, **cleanup))
        if 'fallback_source_face' in cleanup:
            return copy(), empty, dict(fragment_cleanup=fragment_cleanup)
        for i in source_faces:
            output.faces[i] = None
        bodies[group] = [i for i in bodies[group] if i not in source_faces] + body
        for number, data in enumerate(new_wheels):
            bodies[group].extend(i for key in ('cap_faces', 'ring_faces', 'tread_faces') for i in data[key])
            wheels.append(dict(name=f'{flank}-{number}', body=group, side=side,
                               source_hub_pixel=(data['source_pixel_center'][0], 28)
                               if name == 'CR11CVAN_1' else data['source_pixel_center'],
                               **data))
    for group, faces in bodies.items():
        bodies[group], mapping = surfaces.conform_surface_edges(output, faces)
        for wheel in wheels:
            if wheel['body'] == group:
                for key in ('cap_faces', 'ring_faces', 'tread_faces'):
                    wheel[key] = [child for face in wheel[key] for child in mapping[face]]
    remap, compact = {}, []
    for i, face in enumerate(output.faces):
        if face is not None:
            remap[i] = len(compact)
            compact.append(face)
    output.faces = compact
    bodies = {name: [remap[i] for i in faces] for name, faces in bodies.items()}
    for wheel in wheels:
        for key in ('cap_faces', 'ring_faces', 'tread_faces'):
            wheel[key] = [remap[i] for i in wheel[key]]
    underside, separate_floor = {}, None
    for group, faces in bodies.items():
        if group == 'chassis':
            continue
        underside[group] = surfaces.close_underside(output, faces, dark_slot, dark_uv)
        faces.extend(underside[group])
    if 'chassis' in bodies:
        faces, bounds = _inner_floor(output, source, name, dark_slot, dark_uv)
        underside['inner-chassis'] = faces
        separate_floor = dict(faces=faces, canonical_bounds=bounds)
    added = len(output.faces) - len(original.faces)
    if added > triangle_budget:
        return copy(), empty, {}
    if rotated:
        output.vertices = [_rotate(v, inverse=True) for v in output.vertices]
    return output, dict(added_triangles=added, rebuilt_static_fleet=1, fleet_wheels=len(wheels),
                        closed_fleet_undersides=len(underside)), \
        dict(name=name, canonical_rotation=rotated, replaced_source_faces=sorted(replaced),
             wheels=wheels, body_components=bodies, underside_faces=underside,
             separate_floor=separate_floor,
             fragment_cleanup=fragment_cleanup,
             dark_sampler=dict(name=sampler, slot=dark_slot, uv=dark_uv),
             source_inner_faces_retained=name in ('LFIRETRK_1', 'LRACJEEP_1'))


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    mesh, stats, _ = _build(original, mesh_type, texture_names, triangle_budget)
    return mesh, stats


def describe(original, mesh_type, texture_names, triangle_budget=2048):
    return _build(original, mesh_type, texture_names, triangle_budget)[2]
