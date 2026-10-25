"""Source-gated Finale convoy wheels and original-contour inner wheel housings.

The two actors retain their own dimensions, texture records, pivots and lamp
variants. No ALLCARS stance/profile or body scaling is applied to CHAMPFIN.
"""

import math
import struct

from vehicle_model import Mesh, crc32, decode, pack, polygon, sub, unit
from vehicle_geometry import source_boundary
from vehicle_wheel_wells import node_records


CONTRACTS = {
    'pace-car': {'source_crc32': '7076ff0e', 'wheel': 3,
                 'mesh_names': ('PaceBody_1', 'PaceBody_2', 'PaceBody_3', 'HWhlPace_1'),
                 'dark_pixel': (54, 44), 'dark_rgb': (0, 8, 8)},
    'transporter': {'source_crc32': '94b4e25b', 'wheel': 1,
                    'mesh_names': ('TRKBODY_1', 'TWHEEL_1'),
                    'dark_pixel': (223, 102), 'dark_rgb': (8, 8, 8)},
}
SEGMENTS = 32
WHITE = (255, 255, 255, 255)


def _bounds(mesh):
    return [[min(v[a] for v in mesh.vertices), max(v[a] for v in mesh.vertices)] for a in range(3)]


def _constant_polygon(mesh, points, normal, slot, uv):
    polygon(mesh, points, WHITE, normal, [uv] * len(points), slot)


def _photo_uv(source, side, point):
    """Use the actual source side-quad affine chart, including Truck's mirror."""
    face = next(f for f in source.faces if struct.unpack_from('<h', f, 8)[0] >= 0
                and all(source.vertices[i][2] * side > 0 for i in struct.unpack_from('<3H', f, 2)))
    a, b, c = [source.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
    denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    wa = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / denominator
    wb = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / denominator
    return tuple(wa * a[k] + wb * b[k] + (1 - wa - wb) * c[k] for k in (3, 4))


def _wheel(source, dark_uv):
    """Round the original seven-sided barrel; keep the source photo projection."""
    result = Mesh(source.header, [], [])
    bounds = _bounds(source)
    radius = (bounds[0][1] - bounds[0][0]) / 2
    half = (bounds[2][1] - bounds[2][0]) / 2
    slot = next(struct.unpack_from('<h', f, 8)[0] for f in source.faces
                if struct.unpack_from('<h', f, 8)[0] >= 0)
    photo_color = tuple(next(f for f in source.faces if struct.unpack_from('<h', f, 8)[0] >= 0)[10:14])
    # The photograph quad defines the actor's local hub; do not carry the old
    # Truck barrel's small off-centre seven-vertex exporter offset into a new hub.
    center = ((bounds[0][0] + bounds[0][1]) / 2,
              (bounds[1][0] + bounds[1][1]) / 2)
    profile = ((-half, .96 * radius), (-.82 * half, radius),
               (.82 * half, radius), (half, .96 * radius))
    rings = []
    for z, r in profile:
        rings.append([(center[0] + r * math.cos(math.tau * i / SEGMENTS),
                       center[1] + r * math.sin(math.tau * i / SEGMENTS), z)
                      for i in range(SEGMENTS)])
    for row, (a, b) in enumerate(zip(rings, rings[1:])):
        for i in range(SEGMENTS):
            j = (i + 1) % SEGMENTS
            angle = math.tau * (i + .5) / SEGMENTS
            dz, dr = profile[row + 1][0] - profile[row][0], profile[row + 1][1] - profile[row][1]
            normal = unit((dz * math.cos(angle), dz * math.sin(angle), -dr))
            _constant_polygon(result, (a[i], a[j], b[j], b[i]), normal, slot, dark_uv)
    for side, ring in ((-1, rings[0]), (1, rings[-1])):
        hub = (*center, side * half)
        for i in range(SEGMENTS):
            points = (hub, ring[i], ring[(i + 1) % SEGMENTS])
            polygon(result, points, photo_color, (0, 0, side),
                    [_photo_uv(source, side, p) for p in points], slot)
    return result, {'radius': radius, 'axial_width': 2 * half,
                    'hub_local': [*center, 0], 'photo_slot': slot,
                    'source_barrel_note': 'Original photo quad/hub retained; seven-sided barrel rounded, no pivot change.',
                    'bounds': _bounds(result)}


def _arch_chain(body, center, radius, side):
    """Trace the real open painted arch, excluding sill/bottom border segments."""
    selected = []
    for a, b in source_boundary(body):
        if (all(abs(v[0] - center[0]) < 1.35 * radius and v[2] * side > abs(center[2])
                and v[1] < center[1] + 1.5 * radius for v in (a, b))
                and max(a[1], b[1]) > center[1] + .02):
            selected.append((a[:3], b[:3]))
    neighbors = {}
    for a, b in selected:
        neighbors.setdefault(a, []).append(b)
        neighbors.setdefault(b, []).append(a)
    ends = [p for p, n in neighbors.items() if len(n) == 1]
    if len(ends) != 2 or any(len(n) > 2 for n in neighbors.values()):
        raise ValueError('Finale source wheel arch must be one unbranched open contour')
    chain = [min(ends)]
    while len(chain) < len(neighbors):
        following = [p for p in neighbors[chain[-1]] if len(chain) < 2 or p != chain[-2]]
        if len(following) != 1:
            raise ValueError('Finale source arch contour is disconnected')
        chain.append(following[0])
    return chain


def _housings(body, nodes, radius, half, dark_uv):
    """Add only arch walls/back faces; retain the source open lower wheel mouths."""
    descriptions = []
    for node in node_records(nodes):
        if node['id'] not in (1, 2, 3, 4):
            continue
        center = node['origin']
        side = 1 if center[2] > 0 else -1
        chain = _arch_chain(body, center, radius, side)
        inner_z = center[2] - side * (half + .055)
        back = [(p[0], p[1], inner_z) for p in chain]
        first = len(body.faces)
        for a, b, c, d in zip(chain, chain[1:], back[1:], back):
            middle = tuple((a[k] + b[k]) / 2 for k in range(3))
            _constant_polygon(body, (a, b, c, d), unit((center[0] - middle[0], center[1] - middle[1], 0)), 0, dark_uv)
        # Convex source arch back, closing the original see-through body above
        # its sill chord. The lower mouth stays open; no new floor cuts a tyre.
        hub = tuple(sum(p[k] for p in back) / len(back) for k in range(3))
        for a, b in zip(back, back[1:] + back[:1]):
            _constant_polygon(body, (hub, a, b), (0, 0, side), 0, dark_uv)
        descriptions.append({'wheel_id': node['id'], 'center': center,
                             'source_arch': chain, 'inner_z': inner_z,
                             'first_face': first, 'faces': len(body.faces) - first,
                             'lower_mouth': 'Original open sill chord retained; no cross-tyre floor.'})
    return descriptions


def generate(raw, role):
    """Return source-textured CM3D and QA data; reject any unreviewed source."""
    if role not in CONTRACTS:
        raise ValueError('Unknown Finale convoy role')
    contract = CONTRACTS[role]
    if crc32(raw) != contract['source_crc32']:
        raise ValueError('Finale model differs from canonical CHAMPFIN source')
    header, nodes, meshes, textures = decode(raw)
    names = tuple(m.header[:12].rstrip(b'\0').decode('ascii') for m in meshes)
    if names != contract['mesh_names']:
        raise ValueError('Finale canonical mesh roles differ')
    dark_uv = ((contract['dark_pixel'][0] + .5) / 256,
               (contract['dark_pixel'][1] + .5) / 256 - 1)
    wi = contract['wheel']
    source_wheel = meshes[wi]
    meshes[wi], wheel = _wheel(source_wheel, dark_uv)
    original_body_faces = len(meshes[0].faces)
    housings = _housings(meshes[0], nodes, wheel['radius'], wheel['axial_width'] / 2, dark_uv)
    output = pack(header, nodes, meshes, textures)
    report = {'role': role, 'source_crc32': crc32(raw), 'output_crc32': crc32(output),
              'nodes_unchanged': True, 'texture_section_unchanged': True,
              'source_body_faces': original_body_faces,
              'body_source_faces_retained': True, 'wheel_mesh': wi, 'wheel': wheel,
              'housings': housings,
              'dark_sampler': {'slot': 0, 'pixel': contract['dark_pixel'],
                               'source_rgb': contract['dark_rgb'], 'raw_uv': dark_uv,
                               'reason': 'Opaque source sampler also survives the static/terrain material path.'},
              'meshes': [{'name': n, 'vertices': len(m.vertices), 'triangles': len(m.faces),
                          'raw_mesh_crc32': crc32(m.encode()), 'bounds': _bounds(m)}
                         for n, m in zip(names, meshes)]}
    return output, report
