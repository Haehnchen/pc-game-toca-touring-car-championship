"""Source-bound roof, window and sign detail for Donington DA28CBO6_1.

The six PIT3 faces form a four-triangle front and a two-triangle end return;
those original surfaces stay intact. The eight GSD9 rear/end faces remain untouched.
Only this exact GP/Short type is enhanced with shared accepted artwork.
"""

import struct

import mega_cbo4 as shared
import vehicle_model as geometry


VERTICES_CRC32 = '43625725'
TOPOLOGY_CRC32 = '82c875ca'
MATERIALS = ('DA28COMA', 'DA28COM9', 'DA28COM4', 'DA28PIT3', 'DA02GSD9')
ROOF_FACES = range(8)
WINDOW_FACES = range(22, 26)
SIGN_FACES = (16, 17, 20, 21)


def matches(original, texture_names):
    if not shared._source_matches(original, b'DA28CBO6_1', 63, 34, VERTICES_CRC32, TOPOLOGY_CRC32):
        return False
    names = shared._names(texture_names)
    expected = ('DA28COM9',) * 8 + ('DA28COMA',) * 8 + ('DA28PIT3',) * 6 + ('DA28COM4',) * 4 + ('DA02GSD9',) * 8
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _ridge(mesh, original):
    for ridge_indices, front_indices, rear_indices, front_faces, rear_faces in (
            ((0, 1), (12, 2), (7, 8), (0, 4), (2, 6)),
            ((5, 13), (4, 3), (10, 15), (1, 5), (3, 7))):
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
    stats = dict(donington_cbo6=0, cbo6_roof_edges=0,
                 cbo6_ridge_sections=0, cbo6_window_frames=0, cbo6_sign_backings=0, added_triangles=0)
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
    stats.update(donington_cbo6=1, cbo6_roof_edges=len(edges),
                 cbo6_ridge_sections=2, cbo6_window_frames=6, cbo6_sign_backings=1, added_triangles=added)
    return mesh, stats
