"""Actual-texture alpha coverage from the unchanged projected triangle rasterizer."""

import math
import struct

from .geometry import bounds


def crown_alpha_mask(mesh, textures, angle, source):
    """Rasterize front-facing crown materials with their actual image alpha."""
    across = (-math.sin(angle), math.cos(angle))
    projected_source = [v[0] * across[0] + v[1] * across[1] for v in source.vertices]
    left, right = min(projected_source), max(projected_source)
    top, bottom = bounds(source)[2]
    scale = 128 / (bottom - top)
    width, height = math.ceil((right - left) * scale) + 2, 130
    result = bytearray(width * height)
    for face in mesh.faces:
        slot = struct.unpack_from("<h", face, 8)[0]
        if slot not in textures:
            continue
        tw, th, alpha = textures[slot]
        vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
        points = [(1 + (v[0] * across[0] + v[1] * across[1] - left) * scale, 1 + (v[2] - top) * scale) for v in vertices]
        (ax, ay), (bx, by), (cx, cy) = points
        area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        if area <= 1e-9:
            continue
        for y in range(max(0, math.floor(min(p[1] for p in points))), min(height, math.ceil(max(p[1] for p in points)))):
            for x in range(max(0, math.floor(min(p[0] for p in points))), min(width, math.ceil(max(p[0] for p in points)))):
                if result[y * width + x]:
                    continue
                first = ((bx - x - .5) * (cy - y - .5) - (by - y - .5) * (cx - x - .5)) / area
                second = ((cx - x - .5) * (ay - y - .5) - (cy - y - .5) * (ax - x - .5)) / area
                weights = (first, second, 1 - first - second)
                if min(weights) < -1e-9:
                    continue
                u = sum(weight * vertex[3] for weight, vertex in zip(weights, vertices))
                v = 1 + sum(weight * vertex[4] for weight, vertex in zip(weights, vertices))
                tx, ty = max(0, min(tw - 1, int(u * tw))), max(0, min(th - 1, int(v * th)))
                if alpha[ty * tw + tx] >= 128:
                    result[y * width + x] = 1
    return result, width, height
