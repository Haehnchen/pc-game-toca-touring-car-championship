"""Simple roof edges, window depth and sign backing for Donington DA28CBO3_1.

This GP/Short segment retains its64 source vertices/32 faces, uneven base,
thin COM4 end/rear fascia and all five existing atlas bindings.
"""

import struct

import mega_cbo4 as shared
import vehicle_model as geometry


VERTICES_CRC32 = '1692ad7a'
TOPOLOGY_CRC32 = '05804c7f'
MATERIALS = ('DA28COMA', 'DA28COM9', 'DA28COM4', 'DA28PIT3', 'DA02GSD9')
ROOF_FACES = range(22, 30)
WINDOW_FACES = (16, 17, 19, 20)
SIGN_FACES = (8, 9, 10, 11)


def matches(original, texture_names):
    if not shared._source_matches(original, b'DA28CBO3_1', 64, 32, VERTICES_CRC32, TOPOLOGY_CRC32):
        return False
    names = shared._names(texture_names)
    expected = ('DA28COMA',) * 8 + ('DA28PIT3',) * 4 + ('DA28COM4',) * 10 + ('DA28COM9',) * 8 + ('DA02GSD9',) * 2
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _ridge(mesh, original):
    for ridge_indices, front_indices, rear_indices, front_faces, rear_faces in (
            ((53, 45), (44, 46), (56, 58), (22, 25), (27, 28)),
            ((49, 47), (48, 54), (51, 59), (23, 26), (24, 29))):
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
    stats = dict(donington_cbo3=0, cbo3_roof_edges=0,
                 cbo3_ridge_sections=0, cbo3_window_frames=0, cbo3_sign_backings=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    edges = shared._boundary(original, ROOF_FACES)
    for first, second, normal, texture in edges:
        shared._prism(mesh, first, second, normal, texture)
    _ridge(mesh, original)
    for x0, y0, x1, y1 in shared.WINDOWS:
        shared._frame(mesh, original, WINDOW_FACES, (x0 / 64, y0 / 16 - 1, x1 / 64, y1 / 16 - 1), .35 / 64, .35 / 16, .065)
    _sign_backing(mesh, original)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_cbo3=1, cbo3_roof_edges=len(edges),
                 cbo3_ridge_sections=2, cbo3_window_frames=6, cbo3_sign_backings=1, added_triangles=added)
    return mesh, stats
