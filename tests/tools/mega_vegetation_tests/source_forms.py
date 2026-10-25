"""Synthetic source tree/grove charts with canonical dimensions, UV phase and face layout."""

import math
import struct

from .environment import vegetation
from .geometry import Mesh, bounds


def fit_bounds(mesh, extent):
    """Fit positions to an authored carrier while keeping its UVs and normals."""
    source = bounds(mesh)
    mesh.vertices = [(*(extent[a][0] + (v[a] - source[a][0]) * (extent[a][1] - extent[a][0]) /
                         (source[a][1] - source[a][0]) for a in range(3)), *v[3:]) for v in mesh.vertices]
    return mesh


def source_tree():
    mesh = Mesh(b"TRIS04_1\0".ljust(48, b"\0"), [], [])
    for axis in (0, 1):
        offset = len(mesh.vertices)
        for horizontal, z, u, v in ((-3.8, -10.8, 0, -1), (3.8, -10.8, 1, -1),
                                    (3.8, -.0014, 1, 0), (-3.8, -.0014, 0, 0)):
            point = (horizontal, horizontal if axis == 0 else -horizontal, z)
            mesh.vertex(point, vegetation.unit((1., -1. if axis == 0 else 1., 0.)), (u, v))
        mesh.triangle((offset, offset + 1, offset + 2), (255,) * 4, 0)
        mesh.triangle((offset, offset + 2, offset + 3), (255,) * 4, 0)
    return mesh


def source_tree7():
    mesh = source_tree()
    mesh.header = b"TRIS07_1\0".ljust(48, b"\0")
    extent = ((-212697 / 65536, 213989 / 65536), (-214428 / 65536, 212257 / 65536),
              (-918859 / 65536, 1153 / 65536))
    fit_bounds(mesh, extent)
    return mesh


def source_tree4b():
    mesh = source_tree()
    mesh.header = b"TRIS04B_1\0".ljust(48, b"\0")
    extent = ((-392943 / 65536, 391510 / 65536), (-392227 / 65536, 392227 / 65536),
              (-1202268 / 65536, -6251 / 65536))
    fit_bounds(mesh, extent)
    return mesh


def source_tree2b():
    mesh = source_tree()
    mesh.header = b"TRIS02B_1\0".ljust(48, b"\0")
    # This source crosses along local X and Y; TRIS04 crosses diagonally.
    factor = math.sqrt(.5)
    mesh.vertices = [((v[0] + v[1]) * factor, (v[1] - v[0]) * factor, *v[2:5],
                      (v[5] + v[6]) * factor, (v[6] - v[5]) * factor, v[7]) for v in mesh.vertices]
    extent = ((-266191 / 65536, 267166 / 65536), (-268036 / 65536, 265322 / 65536),
              (-1151877 / 65536, -1860 / 65536))
    fit_bounds(mesh, extent)
    # Match the canonical 20-vertex / 16-triangle cross for additional-face
    # accounting, without embedding original vertex or face records.
    original_faces, mesh.faces = mesh.faces, []
    for face in original_faces:
        a, b, c = struct.unpack_from("<3H", face, 2)
        mids = []
        for first, second in ((a, b), (b, c), (c, a)):
            vertex = tuple((x + y) * .5 for x, y in zip(mesh.vertices[first], mesh.vertices[second]))
            mids.append(mesh.vertex(vertex[:3], vertex[5:], vertex[3:5]))
        ab, bc, ca = mids
        for indices in ((a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)):
            mesh.triangle(indices, (255,) * 4, 0)
    return mesh


def source_grove():
    mesh = Mesh(b"TRGE17_1\0".ljust(48, b"\0"), [], [])
    columns = ((2499 / 65536, -1507 / 65536, 32509 / 65536),
               (862571 / 65536, -3317 / 65536, 1.), (-857572 / 65536, 301 / 65536, 0.))
    for x, y, u in columns:
        for z, v in ((-925264 / 65536, -65535 / 65536), (-5250 / 65536, 1 / 65536)):
            mesh.vertex((x, y, z), (0., -1., 0.), (u, v))
    for indices in ((0, 2, 3), (0, 3, 1), (4, 0, 1), (4, 1, 5)):
        mesh.triangle(indices, (255,) * 4, 0)
    return mesh


def source_tree1():
    mesh = source_tree2b()
    mesh.header = b"TRIE01_1\0".ljust(48, b"\0")
    extent = ((-212697 / 65536, 213989 / 65536), (-214428 / 65536, 212257 / 65536),
              (-918859 / 65536, 1153 / 65536))
    fit_bounds(mesh, extent)
    return mesh


def source_grove_lm4():
    mesh = Mesh(b"TRGS22_1\0".ljust(48, b"\0"), [], [])
    # A reversed two-chart group with a shallow center fold and two sides.
    for x, y, u in ((3015 / 65536, -3015 / 65536, 32247 / 65536),
                    (-711144 / 65536, 0., 1.), (711144 / 65536, 0., 0.)):
        for z, v in ((-712156 / 65536, -1.), (-91 / 65536, 0.)):
            mesh.vertex((x, y, z), (0., -1., 0.), (u, v))
    for indices in ((0, 1, 2), (1, 3, 2), (0, 4, 5), (0, 5, 1)):
        mesh.triangle(indices, (255,) * 4, 0)
        mesh.triangle(tuple(reversed(indices)), (255,) * 4, 0)
    return mesh


def source_grove_lm2():
    mesh = source_grove()
    mesh.header = b"TRGS20_1\0".ljust(48, b"\0")
    mesh.vertices = [(*v[:3], v[3] if v[3] in (0., 1.) else 32247 / 65536,
                      -1. if v[4] < -.5 else 0., *v[5:]) for v in mesh.vertices]
    return mesh
