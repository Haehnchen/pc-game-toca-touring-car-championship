"""Common CM3D records, canonical BFP reading and mesh primitives."""

from dataclasses import dataclass
import math
from pathlib import Path
import struct
import zlib

from git_game_data import configured_data_root

PROJECT = Path(__file__).resolve().parent.parent


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x * s for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(a):
    length = math.sqrt(dot(a, a))
    return scale(a, 1 / length) if length else (0., 1., 0.)


@dataclass
class Mesh:
    header: bytes
    vertices: list
    faces: list

    def vertex(self, position, normal, uv=(0., -1.)):
        self.vertices.append((*position, *uv, *unit(normal)))
        return len(self.vertices) - 1

    def triangle(self, indices, color, texture=-1):
        # CM3D's ordinary outward winding, also consumed by software clipping.
        a, b, c = (self.vertices[i] for i in indices)
        normal = add(add(a[5:], b[5:]), c[5:])
        if dot(cross(sub(b[:3], a[:3]), sub(c[:3], a[:3])), normal) < 0:
            indices = (indices[0], indices[2], indices[1])
        self.faces.append(struct.pack("<H3Hh4B10x", 0, *indices, texture, *color))

    def quad(self, indices, color):
        self.triangle(indices[:3], color)
        self.triangle((indices[0], indices[2], indices[3]), color)

    def encode(self):
        if max(len(self.vertices), len(self.faces)) > 65535:
            raise ValueError("CM3D mesh exceeds 16-bit counts")
        header = bytearray(self.header)
        struct.pack_into("<IHH", header, 12, 48 + len(self.vertices) * 32 + len(self.faces) * 24,
                         len(self.vertices), len(self.faces))
        return bytes(header) + b"".join(struct.pack("<8i", *(round(x * 65536) for x in v)) for v in self.vertices) + b"".join(self.faces)


mul = scale


def polygon(mesh, positions, color, normal=None, uvs=None, texture=-1):
    normal = normal or unit(cross(sub(positions[1], positions[0]), sub(positions[2], positions[0])))
    indices = [mesh.vertex(p, normal, uvs[i] if uvs else (0., -1.)) for i, p in enumerate(positions)]
    for i in range(1, len(indices) - 1):
        mesh.triangle((indices[0], indices[i], indices[i + 1]), color, texture)


def box(mesh, center, half, color):
    for axis in range(3):
        a, b = [i for i in range(3) if i != axis]
        for sign in (-1, 1):
            normal = tuple(sign if k == axis else 0 for k in range(3))
            corners = []
            for sa, sb in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = list(center)
                p[axis] += half[axis] * sign
                p[a] += half[a] * sa
                p[b] += half[b] * sb
                corners.append(tuple(p))
            polygon(mesh, corners, color, normal)


def cylinder(mesh, start, end, radius, color, segments=8, caps=True):
    axis = unit(sub(end, start))
    first = unit(cross(axis, (0, 1, 0) if abs(axis[1]) < .9 else (1, 0, 0)))
    second = cross(axis, first)
    rings = []
    for origin in (start, end):
        rings.append([mesh.vertex(add(origin, mul(add(mul(first, math.cos(math.tau * i / segments)), mul(second, math.sin(math.tau * i / segments))), radius)),
                                  add(mul(first, math.cos(math.tau * i / segments)), mul(second, math.sin(math.tau * i / segments)))) for i in range(segments)])
    for i in range(segments):
        j = (i + 1) % segments
        mesh.quad((rings[0][i], rings[0][j], rings[1][j], rings[1][i]), color)
    if caps:
        for ring, origin, normal in ((rings[0], start, mul(axis, -1)), (rings[1], end, axis)):
            for i in range(segments):
                polygon(mesh, (origin, mesh.vertices[ring[i]][:3], mesh.vertices[ring[(i + 1) % segments]][:3]), color, normal)


def decode(data):
    if data[:4] != b"CM3D" or struct.unpack_from("<II", data, 4) != (100, 32):
        raise ValueError("Expected CM3D version100")
    node_start, mesh_start, texture_start = struct.unpack_from("<III", data, 20)
    meshes = []
    offset = mesh_start
    for _ in range(struct.unpack_from("<H", data, 14)[0]):
        size, vc, fc = struct.unpack_from("<IHH", data, offset + 12)
        if size != 48 + 32 * vc + 24 * fc or offset + size > texture_start:
            raise ValueError("Invalid source mesh bounds")
        vertices = [tuple(x / 65536 for x in struct.unpack_from("<8i", data, offset + 48 + n * 32)) for n in range(vc)]
        face_start = offset + 48 + vc * 32
        faces = [data[face_start + n * 24:face_start + (n + 1) * 24] for n in range(fc)]
        meshes.append(Mesh(data[offset:offset + 48], vertices, faces))
        offset += size
    if offset != texture_start or node_start != 32:
        raise ValueError("Invalid source sections")
    return data[:32], data[node_start:mesh_start], meshes, data[texture_start:]


def visible_triangles(nodes, meshes):
    total, offset = 0, 0
    while offset < len(nodes):
        size = struct.unpack_from("<I", nodes, offset)[0]
        if nodes[offset + 76]:
            index = struct.unpack_from("<H", nodes, offset + 80)[0]
            if index != 65535:
                total += len(meshes[index].faces)
        offset += size
    return total


def read_source(source, data_root=None):
    """Read a fully identified BFP member, checking both archive and member CRC32."""
    archive = source["archive"]
    if not isinstance(archive, str) or not archive.startswith("data/"):
        raise ValueError("Vehicle source must identify a package-relative data/ archive")
    relative = archive
    if ("\\" in relative or ":" in relative or "\0" in relative
            or any(part in ("", ".", "..") for part in relative.split("/"))):
        raise ValueError("Vehicle source must use a safe canonical archive path")
    root = Path(data_root if data_root is not None else configured_data_root()).resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        raise ValueError("Vehicle source escapes its game-data root")
    data = path.read_bytes()
    if crc32(data) != source["archive_crc32"]:
        raise ValueError("Canonical vehicle archive CRC32 mismatch")
    if len(data) < 12:
        raise ValueError("Truncated BFP header")
    count, table_size = struct.unpack_from("<II", data)
    index = source["member"]
    if table_size != count * 24 or 12 + table_size > len(data) or not 0 <= index < count:
        raise ValueError("Invalid BFP directory/member index")
    name, offset, size = struct.unpack_from("<16sII", data, 12 + index * 24)
    if name.split(b"\0", 1)[0].decode("ascii") != source["member_name"]:
        raise ValueError("BFP member name differs from the manifest")
    start = 12 + table_size + offset
    if size > len(data) - start:
        raise ValueError("Truncated BFP member")
    result = data[start:start + size]
    if crc32(result) != source["member_crc32"]:
        raise ValueError("Canonical vehicle member CRC32 mismatch")
    return result


def pack(header, nodes, meshes, textures):
    encoded = b"".join(mesh.encode() for mesh in meshes)
    output_header = bytearray(header)
    struct.pack_into("<III", output_header, 20, 32, 32 + len(nodes), 32 + len(nodes) + len(encoded))
    return bytes(output_header) + nodes + encoded + textures


def interpolate(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def node_records(data, mesh_count):
    nodes, offset = [], 0
    while offset < len(data):
        if offset + 80 > len(data):
            raise ValueError("Truncated CM3D node")
        size, parent = struct.unpack_from("<Ih", data, offset)
        count = data[offset + 76]
        if size != (80 + count * 2 + 3) & ~3 or offset + size > len(data):
            raise ValueError("Invalid CM3D node bounds")
        if parent < -1 or parent >= len(nodes):
            raise ValueError("CM3D parent must precede its child")
        variants = list(struct.unpack_from(f"<{count}H", data, offset + 80))
        if any(index != 65535 and index >= mesh_count for index in variants):
            raise ValueError("CM3D node references an unknown mesh")
        matrix = list(struct.unpack_from("<16i", data, offset + 8))
        identifier = struct.unpack_from("<I", data, offset + 72)[0]
        nodes.append(dict(index=len(nodes), parent=parent, id_flags=identifier,
                          node_id=identifier & 0xffffff, flags=identifier & 0xff000000,
                          variants=variants, matrix_16_16=matrix,
                          pivot_local=[value / 65536 for value in matrix[12:15]]))
        offset += size
    return nodes
