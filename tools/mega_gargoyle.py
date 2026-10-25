"""Lavaland's source-bound stone head, with shallow closed facial relief.

The source crown, sides, back and underside retain their positions and UVs.
Only the three frontal GARG01 triangles gain form from the existing brow,
eye sockets, nose and muzzle. The original local origin stays unchanged;
SceneView centers both original and Mega cone visuals around the actor pivot.
"""

from collections import defaultdict
import math
import struct

import mega_static_platform as surfaces
import vehicle_model as geometry


SOURCE_MESH_CRC32 = '94130c59'
FRONT_FACES = (0, 1, 2)
# Six intervals put actual vertices at the source's eye row(.50), nose(.65)
# and mouth(.79), without subdividing the quiet side/closure atlas.
FRONT_INTERVALS = 6


def matches(original, texture_names):
    return (original.header[:12].rstrip(b'\0') == b'GARCON_1'
            and texture_names == ['GARG01', 'GARG02']
            and (len(original.vertices), len(original.faces)) == (26, 14)
            and geometry.crc32(original.encode()) == SOURCE_MESH_CRC32)


def _relief(u, v):
    """Depth follows normalized original GARG01 features, with fixed borders."""
    def feature(x, y, width, height):
        return math.exp(-((u - x) / width) ** 2 - ((v - y) / height) ** 2)

    # The right eye is the pale prominent one; the left socket remains subdued.
    depth = (-.017 * feature(.75, .36, .19, .12)
             -.010 * feature(.26, .37, .19, .12)
             +.014 * feature(.75, .50, .13, .09)
             +.010 * feature(.26, .50, .14, .10)
             -.012 * feature(.80, .64, .16, .14)
             -.010 * feature(.20, .64, .16, .14)
             -.024 * feature(.52, .65, .10, .13)
             -.011 * feature(.50, .76, .23, .08)
             +.006 * feature(.50, .82, .22, .05))
    border = max(0., min(1., min(u, 1 - u, v, 1 - v) * 8))
    return depth * border


def enhance(original, mesh_type, texture_names):
    output = mesh_type(original.header, list(original.vertices), list(original.faces))
    if not matches(original, texture_names):
        return output
    output.faces = [face for i, face in enumerate(original.faces) if i not in FRONT_FACES]
    front_start = len(output.vertices)
    front_faces = []
    bounds = [(min(vertex[axis] for vertex in original.vertices), max(vertex[axis] for vertex in original.vertices)) for axis in range(3)]
    nearest_z = bounds[2][0]
    for face_id in FRONT_FACES:
        source = original.faces[face_id]
        records = [original.vertices[i] for i in struct.unpack_from('<3H', source, 2)]
        grid = {}
        for i in range(FRONT_INTERVALS + 1):
            for j in range(FRONT_INTERVALS + 1 - i):
                weights = (1 - (i + j) / FRONT_INTERVALS, i / FRONT_INTERVALS, j / FRONT_INTERVALS)
                vertex = tuple(sum(w * record[axis] for w, record in zip(weights, records)) for axis in range(8))
                x, y = (max(low, min(high, vertex[axis])) for axis, (low, high) in enumerate(bounds[:2]))
                u, v = vertex[3], vertex[4] + 1
                z = max(nearest_z, vertex[2] + _relief(u, v))
                # Preserve the source perimeter exactly, including its slight
                # Q16 UV inset beyond V=1 at the bottom row.
                if any(abs(value - bound) < 1e-10 for value, extent in ((x, bounds[0]), (y, bounds[1])) for bound in extent):
                    z = vertex[2]
                grid[i, j] = len(output.vertices)
                output.vertices.append((x, y, z, *vertex[3:]))
        for i in range(FRONT_INTERVALS):
            for j in range(FRONT_INTERVALS - i):
                triangles = [(grid[i, j], grid[i + 1, j], grid[i, j + 1])]
                if i + j < FRONT_INTERVALS - 1:
                    triangles.append((grid[i + 1, j], grid[i + 1, j + 1], grid[i, j + 1]))
                for indices in triangles:
                    face = bytearray(source)
                    struct.pack_into('<3H', face, 2, *indices)
                    front_faces.append(len(output.faces))
                    output.faces.append(bytes(face))

    # Share smooth relief normals across the original three front triangles.
    # Quiet closure faces keep their separate original flat normals/UV seams.
    normals = defaultdict(lambda: (0., 0., 0.))
    for face_id in front_faces:
        points = [output.vertices[i][:3] for i in struct.unpack_from('<3H', output.faces[face_id], 2)]
        normal = geometry.cross(geometry.sub(points[1], points[0]), geometry.sub(points[2], points[0]))
        for point in points:
            key = tuple(round(x, 10) for x in point)
            normals[key] = geometry.add(normals[key], normal)
    for i in range(front_start, len(output.vertices)):
        vertex = output.vertices[i]
        normal = geometry.unit(normals[tuple(round(x, 10) for x in vertex[:3])])
        output.vertices[i] = (*vertex[:5], *normal)

    # Make the new front perimeter points explicit on the neighboring source
    # side, crown and underside triangles. This preserves their authored plane,
    # UV interpolation and normals while avoiding geometric T junctions.
    surfaces.conform_surface_edges(output, list(range(len(output.faces))))
    output.faces = [face for face in output.faces if face is not None]
    return output
