"""Source-bound window surrounds, roof edges and sign depth for DA28CBO1_1.

The control tower has a flat terrace and pentagonal observation cabin. Its
original 84 vertices/54 faces, all nine atlas roles and placements stay intact.
"""

import struct

import mega_cbo4 as shared
import vehicle_model as geometry


VERTICES_CRC32 = '23072d78'
TOPOLOGY_CRC32 = '1a9d803d'
MATERIALS = ('DA28COM', 'DA28COM6', 'DA28COM7', 'DA28COMA', 'DA28PIT2',
             'DA28COM8', 'DA28COM9', 'DA27STB6', 'DA02GSD9')
MATERIAL_COUNTS = (7, 4, 20, 4, 6, 6, 2, 3, 2)
SIGN_FACES = (35, 36, 37, 38)
SIGN_BLANK_UV = (.985, -.985)
ROOFS = ((47, 48), (21, 22, 23), (11, 12))
# Source pixel coordinates, not a generic facade grid. The two COM7 full
# cabin panels are distinct from its mast and irregular building-end triangles.
WINDOW_GROUPS = (
    (tuple(range(7)), 64, 32, ((.6, 3., 63.4, 10.7), (.6, 25.5, 63.4, 31.5))),
    (tuple(range(7, 11)), 64, 32, ((.6, 3., 63.4, 10.7), (.6, 25.5, 63.4, 31.5))),
    ((19, 20, 24, 25), 32, 32, ((.7, 5.2, 31.3, 17.),)),
    (tuple(range(41, 47)), 32, 32, ((.7, 5.2, 31.3, 17.),)),
    ((28, 29, 30), 32, 32, ((.7, 5.2, 31.3, 17.),)),
)


def matches(original, texture_names):
    if not shared._source_matches(original, b'DA28CBO1_1', 84, 54, VERTICES_CRC32, TOPOLOGY_CRC32):
        return False
    names = shared._names(texture_names)
    expected = (('DA28COM',) * 7 + ('DA28COM6',) * 4 + ('DA28COM7',) * 20 + ('DA28COMA',) * 4 +
                ('DA28PIT2',) * 6 + ('DA28COM8',) * 6 + ('DA28COM9',) * 2 + ('DA27STB6',) * 3 + ('DA02GSD9',) * 2)
    slots = [struct.unpack_from('<h', face, 8)[0] for face in original.faces]
    return all(0 <= slot < len(names) and names[slot] == name for slot, name in zip(slots, expected))


def _windows(mesh, original):
    patches = 0
    for faces, width, height, rectangles in WINDOW_GROUPS:
        for x0, y0, x1, y1 in rectangles:
            patches += shared._window_panel(mesh, original, faces, (x0 / width, y0 / height - 1, x1 / width, y1 / height - 1))
    return patches


def _sign_backing(mesh, original):
    shared.sign_backing(mesh, original, SIGN_FACES, SIGN_BLANK_UV)


def enhance(original, mesh_type, texture_names, surface_index=None, triangle_budget=2048):
    stats = dict(donington_cbo1=0, cbo1_window_patches=0, cbo1_roof_edges=0,
                 cbo1_sign_backings=0, added_triangles=0)
    if not matches(original, texture_names):
        return original, stats
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    windows = _windows(mesh, original)
    edges = [edge for faces in ROOFS for edge in shared._boundary(original, faces)]
    for first, second, normal, texture in edges:
        shared._prism(mesh, first, second, normal, texture)
    _sign_backing(mesh, original)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(donington_cbo1=1, cbo1_window_patches=windows, cbo1_roof_edges=len(edges),
                 cbo1_sign_backings=1, added_triangles=added)
    return mesh, stats
