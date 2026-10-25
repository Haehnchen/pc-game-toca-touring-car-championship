"""Source-bound roof, window and sign detail for Donington DA28CBO5_1.

The CBO5 source shares CBO4's five BMPs but has its own 60-vertex shape,
roof height, facade UVs and four-face sign. Only this exact GP/Short type is
enhanced; its original faces, hierarchy, placement and ground line stay intact.
"""

import struct

import mega_cbo4 as shared
import vehicle_model as geometry


VERTICES_CRC32 = 'c63c0126'
TOPOLOGY_CRC32 = 'b9923e5a'
MATERIALS = ('DA28COMA', 'DA28COM9', 'DA28COM4', 'DA28PIT3', 'DA02GSD9')
ROOF_FACES = range(8, 16)
WINDOW_FACES = (16, 17, 23, 24)
SIGN_FACES = (26, 27, 28, 29)


def matches(original, texture_names):
    if not shared._source_matches(original, b'DA28CBO5_1', 60, 32, VERTICES_CRC32, TOPOLOGY_CRC32):
        return False
    names = shared._names(texture_names)
    expected = ('DA28COMA',) * 8 + ('DA28COM9',) * 8 + ('DA28COM4',) * 10 + ('DA28PIT3',) * 4 + ('DA02GSD9',) * 2
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _ridge(mesh, original):
    for ridge_indices, front_indices, rear_indices, front_faces, rear_faces in (
            ((16, 17), (26, 18), (22, 23), (8, 12), (10, 14)),
            ((21, 25), (20, 19), (24, 29), (9, 13), (11, 15))):
        rings = []
        texture = struct.unpack_from('<h', original.faces[front_faces[0]], 8)[0]
        for ridge_index, front_index, rear_index in zip(ridge_indices, front_indices, rear_indices):
            ridge = original.vertices[ridge_index][:3]
            front = geometry.interpolate(ridge, original.vertices[front_index][:3], .03)
            rear = geometry.interpolate(ridge, original.vertices[rear_index][:3], .03)
            top = shared._project(original, front_faces, ridge)
            rings.append((shared._project(original, front_faces, front),
                          (geometry.add(top[0], (0., 0., -.08)), top[1]), shared._project(original, rear_faces, rear)))
        for index, normal in ((0, (0., -1., -1.)), (1, (0., 1., -1.))):
            shared._polygon(mesh, (rings[0][index], rings[1][index], rings[1][index + 1], rings[0][index + 1]), texture, normal)
        shared._polygon(mesh, rings[0], texture, (-1., 0., 0.))
        shared._polygon(mesh, list(reversed(rings[1])), texture, (1., 0., 0.))


def _sign_backing(mesh, original):
    shared.sign_backing(mesh, original, SIGN_FACES, shared.SIGN_BLANK_UV)


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    stats = dict(donington_cbo5=0, cbo5_roof_edges=0,
                 cbo5_ridge_sections=0, cbo5_window_frames=0, cbo5_sign_backings=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    edges = shared._boundary(original, ROOF_FACES)
    for first, second, normal, texture in edges:
        shared._prism(mesh, first, second, normal, texture)
    _ridge(mesh, original)
    for x0, y0, x1, y1 in shared.WINDOWS:
        shared._window_panel(mesh, original, WINDOW_FACES, (x0 / 64, y0 / 16 - 1, x1 / 64, y1 / 16 - 1))
    _sign_backing(mesh, original)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_cbo5=1, cbo5_roof_edges=len(edges),
                 cbo5_ridge_sections=2, cbo5_window_frames=6, cbo5_sign_backings=1, added_triangles=added)
    return mesh, stats
