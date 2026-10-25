"""Confirmed VECTRA advertising truck; retain its asymmetric original body.

Three photographic tyres gain shallow source-aligned relief. The broad empty
right body and existing underbody remain source-owned. Dark closure/tyre faces
sample the existing opaque LVOLVO4 strip: static terrain ignores -1 face RGB.
"""

import struct

import mega_static_platform as surfaces


VERTEX_CRCS = ('64792485', '01ef377d')
TOPOLOGY_CRC = 'd6438484'
MATERIALS = (('LVECTR1', 4), ('LVECTR2', 4), ('LVECTR3', 2),
             ('LVECTR4', 15), ('LVECTR5', 4), ('LVOLVO4', 14))
DARK_UV = (30.5 / 32, -1 + 16.5 / 32)
BODY_GROUPS = {'cab': tuple(range(4)) + tuple(range(23, 29)),
               'box': tuple(range(4, 23))}
WHEELS = (
    ('cab-left', 'cab', (0, 3), -1, (12.5 / 32, -1 + 56.5 / 64), 10.5 / 32, 10.5 / 64),
    ('cab-right', 'cab', (1, 2), 1, (12.5 / 32, -1 + 56.5 / 64), 10.5 / 32, 10.5 / 64),
    # The small opaque tyre is x108..117/y23..31; the much wider black-key
    # wheel bay is empty source silhouette, not a physical tyre diameter.
    ('body-photo-left', 'box', (4, 5, 6, 7), -1, (113 / 128, -1 + 28 / 32), 5 / 128, 5 / 32),
)


def matches(original, texture_names):
    if original.header[:12].rstrip(b'\0') != b'LVECTRA_1' or (
            len(original.vertices), len(original.faces)) != (71, 43):
        return False
    vertices, topology = surfaces.source_identity(original)
    if vertices not in VERTEX_CRCS or topology != TOPOLOGY_CRC:
        return False
    names = [(name.decode('ascii') if isinstance(name, bytes) else name).rstrip('\0').upper()
             for name in texture_names]
    expected = [name for name, count in MATERIALS for _ in range(count)]
    return all(0 <= (slot := struct.unpack_from('<h', face, 8)[0]) < len(names)
               and names[slot] == name for face, name in zip(original.faces, expected))


def _build(source, mesh_type, names, triangle_budget):
    output = mesh_type(source.header, list(source.vertices), list(source.faces))
    empty = dict(added_triangles=0, rebuilt_static_vectra_truck=0,
                 vectra_truck_wheels=0, closed_vectra_truck_bodies=0)
    if not matches(source, names):
        return output, empty, {}
    replaced = {face for _, _, faces, *_ in WHEELS for face in faces}
    kept = [i for i in range(len(source.faces)) if i not in replaced]
    output.faces = [source.faces[i] for i in kept]
    remap = {source_id: output_id for output_id, source_id in enumerate(kept)}
    bodies = {name: [remap[i] for i in faces if i in remap] for name, faces in BODY_GROUPS.items()}
    dark_slot, = struct.unpack_from('<h', source.faces[29], 8)
    wheels = []
    for name, group, faces, side, center_uv, radius_u, radius_v in WHEELS:
        body, relief, details = surfaces.wheel_relief(output, source, faces, side,
                                                     center_uv, radius_u, dark_slot, DARK_UV,
                                                     radius_v=radius_v)
        bodies[group].extend(body + relief)
        wheels.append(dict(name=name, body=group, side=side, **details))
    subdivisions = []
    for name, faces in bodies.items():
        bodies[name], mapping = surfaces.conform_surface_edges(output, faces)
        subdivisions.extend(i for i in kept if remap[i] in mapping and len(mapping[remap[i]]) > 1)
        for wheel in wheels:
            if wheel['body'] == name:
                for key in ('cap_faces', 'ring_faces', 'tread_faces'):
                    wheel[key] = [child for face in wheel[key] for child in mapping[face]]
    face_remap, compact = {}, []
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
        underside[name] = surfaces.close_underside(output, faces, dark_slot, DARK_UV)
        faces.extend(underside[name])
    added = len(output.faces) - len(source.faces)
    if added > triangle_budget:
        return mesh_type(source.header, list(source.vertices), list(source.faces)), empty, {}
    return output, dict(added_triangles=added, rebuilt_static_vectra_truck=1,
                        vectra_truck_wheels=len(wheels), closed_vectra_truck_bodies=len(bodies)), \
        dict(replaced_source_faces=sorted(replaced), subdivided_source_faces=subdivisions,
             preserved_source_faces=[i for i in kept if i not in subdivisions],
             wheels=wheels, body_components=bodies, underside_faces=underside,
             dark_sampler=dict(slot=dark_slot, name='LVOLVO4', uv=DARK_UV))


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    output, stats, _ = _build(original, mesh_type, texture_names, triangle_budget)
    return output, stats


def describe(original, mesh_type, texture_names, triangle_budget=2048):
    return _build(original, mesh_type, texture_names, triangle_budget)[2]
