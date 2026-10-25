"""Four identified Knockhill sheep cards with simple closed body volumes.

The source cards, UV orientation and alpha remain the visible posture boundary.
Rounded torso/neck/head and four narrow legs supply thickness behind that plane;
no wool, ear or alpha-pixel geometry is added. All surfaces reuse the original
SHEEP slot and its affine UV projection, including LSHEEP1's mirrored U.
"""

import math
import struct

import vehicle_model as geometry


SOURCES = {
    'LSHEEP1_1': ('d6627061', 'SHEEP2', 'rear'),
    'LSHEEP1A_1': ('78ff389c', 'SHEEP4', 'side'),
    'LSHEEP3A_1': ('8933387d', 'SHEEP7', 'rear'),
    'LSHEEP3B_1': ('8c1cec34', 'SHEEP8', 'side'),
}
DEPTH = .30
FRONT_INSET = .006
# Keep the leg shafts under the torso depth, rather than against the source
# card. Their upper caps sit inside the rounded body even at its narrow hips.
LEG_CENTER_DEPTH = .15
WHITE = (255, 255, 255, 255)
# Normalized source-image centers/radii. Few smooth solids supply only the
# broad anatomy actually visible in both source resolutions; alpha owns edges.
PARTS = {
    'rear': (
        (.275, .49, .245, .245, .147),  # nearer sheep rump
        (.735, .505, .235, .225, .125),  # overlapping second sheep torso
        (.52, .35, .145, .24, .105),  # neck enters the rump core
        (.53, .145, .105, .105, .085),  # head
        (.155, .7075, .035, .2575, .050),
        (.305, .7075, .035, .2575, .050),
        (.665, .7075, .035, .2575, .050),
        (.855, .7075, .035, .2575, .050),
    ),
    'side': (
        (.565, .455, .345, .195, .147),
        (.245, .30, .095, .15, .075),
        (.145, .165, .105, .085, .065),
        (.285, .705, .025, .255, .050),
        (.395, .705, .025, .255, .050),
        (.775, .705, .025, .255, .050),
        (.900, .705, .025, .255, .050),
    ),
}


def _identity(original, texture_names):
    name = original.header[:12].rstrip(b'\0').decode('ascii', errors='replace')
    if name not in SOURCES:
        return None
    checksum, material, posture = SOURCES[name]
    if geometry.crc32(original.encode()) != checksum:
        return None
    names = [(n.decode('ascii') if isinstance(n, bytes) else n).rstrip('\0').upper()
             for n in texture_names]
    slots = {struct.unpack_from('<h', face, 8)[0] for face in original.faces}
    if len(slots) != 1:
        return None
    slot = next(iter(slots))
    if not 0 <= slot < len(names) or names[slot] != material:
        return None
    return posture, slot


def matches(original, texture_names):
    return _identity(original, texture_names) is not None


def _project(original, u, v):
    # Runtime clamps source anchor UVs before interpolation. Inverting those
    # same anchors keeps the added body artwork aligned with the retained card.
    horizontal = sorted({(max(.02, min(.99, p[3])), p[0]) for p in original.vertices})
    vertical = sorted({(max(.02, min(.99, p[4] + 1)), p[2]) for p in original.vertices})
    def axis(value, anchors):
        (a, x), (b, y) = anchors
        return x + (y - x) * (value - a) / (b - a)
    return axis(u, horizontal), axis(v, vertical)


def _solid(mesh, original, part, slot, center_depth=None):
    u, v, ru, rv, depth_radius = part
    rings = []
    segments, latitudes = 8, 4
    center_y = original.vertices[0][1] + (FRONT_INSET + depth_radius
                                        if center_depth is None else center_depth)
    def vertex(t, angle):
        # A low-resolution ellipsoid, poles along the image's vertical axis.
        latitude = -math.pi / 2 + math.pi * t
        du = ru * math.cos(latitude) * math.cos(angle)
        dy = depth_radius * math.cos(latitude) * math.sin(angle)
        dv = rv * math.sin(latitude)
        x, z = _project(original, u + du, v + dv)
        px, pz = _project(original, u, v)
        normal = geometry.unit(((x - px) / (ru * 1.3) ** 2,
                                dy / depth_radius ** 2, (z - pz) / rv ** 2))
        return mesh.vertex((x, center_y + dy, z), normal, (u + du, v + dv - 1))
    bottom = vertex(0, 0)
    for layer in range(1, latitudes):
        rings.append([vertex(layer / latitudes, math.tau * i / segments) for i in range(segments)])
    top = vertex(1, 0)
    for i in range(segments):
        j = (i + 1) % segments
        mesh.triangle((bottom, rings[0][i], rings[0][j]), WHITE, slot)
        for a, b in zip(rings, rings[1:]):
            mesh.triangle((a[i], b[i], b[j]), WHITE, slot)
            mesh.triangle((a[i], b[j], a[j]), WHITE, slot)
        mesh.triangle((rings[-1][i], top, rings[-1][j]), WHITE, slot)


def enhance(original, mesh_type, texture_names, atlas_index, triangle_budget):
    identity = _identity(original, texture_names)
    stats = dict(knockhill_sheep=0, sheep_bodies=0, added_triangles=0)
    if identity is None:
        return original, stats
    posture, slot = identity
    mesh = mesh_type(original.header, list(original.vertices), list(original.faces))
    parts = PARTS[posture]
    for index, part in enumerate(parts):
        _solid(mesh, original, part, slot,
               LEG_CENTER_DEPTH if index >= len(parts) - 4 else None)
    added = len(mesh.faces) - len(original.faces)
    if added > triangle_budget:
        return original, stats
    stats.update(knockhill_sheep=1, sheep_bodies=len(PARTS[posture]), added_triangles=added)
    return mesh, stats
