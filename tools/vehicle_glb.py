"""Pack canonical model geometry into compact GLB2 with embedded WebP."""

from collections import OrderedDict
import json
import math
import struct
import subprocess

import vehicle_textures


webp_texture = vehicle_textures.webp_texture


def compact_geometry(mesh, ghost_hidden_start=None, ghost_positions=None):
    """Discard unreferenced vertices and merge only identical output records.

    The key includes normals, UV seams, signed zero and the optional Ghost
    position. Face order, material grouping and winding remain unchanged.
    """
    records, source_indices, unique_records = [], {}, {}
    groups = OrderedDict()
    for face_index, face in enumerate(mesh.faces):
        mapped = []
        for source_index in struct.unpack_from("<3H", face, 2):
            if source_index not in source_indices:
                x, y, z, u, v, nx, ny, nz = mesh.vertices[source_index]
                record = struct.pack("<8f", x, y, z, nx, ny, nz, u - 1 if u > 1 else u, v + 1)
                if ghost_positions is not None:
                    record += struct.pack("<3f", *ghost_positions.get(source_index, (x, y, z)))
                if record not in unique_records:
                    unique_records[record] = len(records)
                    records.append(record)
                source_indices[source_index] = unique_records[record]
            mapped.append(source_indices[source_index])
        texture = struct.unpack_from("<h", face, 8)[0]
        ghost_hidden = ghost_hidden_start is not None and face_index >= ghost_hidden_start
        groups.setdefault((texture, tuple(face[10:14]), ghost_hidden), []).extend(mapped)
    # Material groups are the emitted draw order. Lay out the vertex table in
    # that order to improve fetch locality without changing any face ordering.
    remap, ordered = {}, []
    for indices in groups.values():
        for corner, old in enumerate(indices):
            if old not in remap:
                remap[old] = len(ordered)
                ordered.append(records[old])
            indices[corner] = remap[old]
    return ordered, groups, len(source_indices)


def compress_geometry(document, blob, encoder):
    """Batch meshopt encoding; keep small buffers ordinary when that is smaller.

    The second, URI-less buffer is the standard extension's virtual fallback:
    no duplicate uncompressed geometry is stored in the GLB binary chunk.
    INDICES preserves exact corner order, including each triangle's first corner.
    """
    views = document["bufferViews"]
    geometry = [index for index, record in enumerate(views) if "target" in record]
    request = bytearray(struct.pack("<II", 0x54504f4d, len(geometry)))
    for index in geometry:
        record = views[index]
        stride = record.get("byteStride", 2)
        data = blob[record["byteOffset"]:record["byteOffset"] + record["byteLength"]]
        request.extend(struct.pack("<4I", int(record["target"] == 34963), stride, len(data) // stride, len(data)))
        request.extend(data)
    response = subprocess.run([str(encoder)], input=request, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, check=True).stdout
    compressed, offset = {}, 0
    for index in geometry:
        if offset + 4 > len(response):
            raise ValueError("Incomplete meshoptimizer response")
        size, = struct.unpack_from("<I", response, offset)
        offset += 4
        if not size or size > len(response) - offset:
            raise ValueError("Invalid meshoptimizer response size")
        # Extension JSON costs ~150 bytes per view; don't expand tiny primitives.
        if size + 160 < views[index]["byteLength"]:
            compressed[index] = response[offset:offset + size]
        offset += size
    if offset != len(response):
        raise ValueError("Unexpected meshoptimizer response data")
    packed, decoded_size, saved = bytearray(), 0, 0
    for index, record in enumerate(views):
        start, size = record["byteOffset"], record["byteLength"]
        packed.extend(b"\0" * (-len(packed) % 4))
        if index in compressed:
            data = compressed[index]
            stride = record.get("byteStride", 2)
            record["extensions"] = {"EXT_meshopt_compression": {
                "buffer": 0, "byteOffset": len(packed), "byteLength": len(data),
                "byteStride": stride, "count": size // stride,
                "mode": "ATTRIBUTES" if record["target"] == 34962 else "INDICES", "filter": "NONE"}}
            decoded_size += -decoded_size % 4
            record.update(buffer=1, byteOffset=decoded_size)
            decoded_size += size
            saved += size - len(data)
        else:
            data = blob[start:start + size]
            record["byteOffset"] = len(packed)
        packed.extend(data)
    document["buffers"] = [{"byteLength": len(packed)}]
    if compressed:
        document["buffers"].append({"byteLength": decoded_size,
                                     "extensions": {"EXT_meshopt_compression": {"fallback": True}}})
        document["extensionsUsed"].append("EXT_meshopt_compression")
        document["extensionsRequired"].append("EXT_meshopt_compression")
    return packed, {"meshopt_buffer_count": len(compressed), "meshopt_bytes_saved": saved}


def encode(header, node_bytes, meshes, texture_section, meshopt_encoder=None, texture_encoder=None, ghost_hidden_starts=None, ghost_positions=None,
           authored_webp_quality=None):
    """Pack meshes already passed through the canonical16.16 quantizer.

    A supplied texture_encoder(bmp) returns final authored WebP directly,
    avoiding a second lossy encode of generated materials.
    generated_webp_quality describes new fallback encodes, not verbatim artwork.
    """
    blob = bytearray()
    document = {
        "asset": {"version": "2.0", "generator": "TOCA Mega model builder"},
        "extensionsUsed": ["EXT_texture_webp", "KHR_materials_unlit"],
        "extensionsRequired": ["EXT_texture_webp"],
        "buffers": [], "bufferViews": [], "accessors": [],
        "images": [], "textures": [], "materials": [], "meshes": [], "nodes": [],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 10497, "wrapT": 10497}],
        "scenes": [{"nodes": []}], "scene": 0,
        "extras": {"toca_model_version": 1, "generated_webp_quality": 70},
    }
    if authored_webp_quality is not None:
        document["extras"] = {"toca_model_version": 1, "authored_webp_quality": authored_webp_quality}

    views, duplicate_bytes = {}, 0

    def view(data, **properties):
        nonlocal duplicate_bytes
        key = (bytes(data), tuple(properties.items()))
        if key in views:
            duplicate_bytes += len(data)
            return views[key]
        blob.extend(b"\0" * (-len(blob) % 4))
        record = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), **properties}
        blob.extend(data)
        document["bufferViews"].append(record)
        index = len(document["bufferViews"]) - 1
        views[key] = index
        return index

    def accessor(buffer_view, component_type, count, kind, **properties):
        document["accessors"].append({"bufferView": buffer_view, "componentType": component_type,
                                      "count": count, "type": kind, **properties})
        return len(document["accessors"]) - 1

    texture_count = struct.unpack_from("<H", header, 16)[0]
    texture_bytes = 0
    for name, _, bmp in vehicle_textures.texture_records(texture_section, texture_count):
        encoded = (texture_encoder or webp_texture)(bmp)
        if len(encoded) < 20 or encoded[:4] != b"RIFF" or encoded[8:12] != b"WEBP":
            raise ValueError("Embedded model textures must be WebP")
        texture_bytes += len(encoded)
        index = len(document["images"])
        document["images"].append({"name": name, "mimeType": "image/webp", "bufferView": view(encoded)})
        document["textures"].append({"name": name, "sampler": 0, "extensions": {"EXT_texture_webp": {"source": index}}})

    materials = {}

    if set(ghost_positions or {}) != set(ghost_hidden_starts or {}):
        raise ValueError("Generated Ghost faces require explicit positions for every marked mesh")

    def material(texture, color):
        key = (texture, color)
        if key not in materials:
            pbr = {"baseColorFactor": [value / 255 for value in color], "metallicFactor": 0, "roughnessFactor": 1}
            if texture >= 0:
                pbr["baseColorTexture"] = {"index": texture}
            materials[key] = len(document["materials"])
            document["materials"].append({"pbrMetallicRoughness": pbr, "alphaMode": "MASK", "alphaCutoff": .5,
                                          "extensions": {"KHR_materials_unlit": {}}})
        return materials[key]

    source_vertices = referenced_vertices = packed_vertices = 0
    for mesh_index, mesh in enumerate(meshes):
        hidden_start = (ghost_hidden_starts or {}).get(mesh_index)
        if hidden_start is not None and not 0 <= hidden_start < len(mesh.faces):
            raise ValueError("Ghost-hidden face start must identify generated mesh faces")
        ghost = (ghost_positions or {}).get(mesh_index)
        if ghost is not None:
            required = {index for face in mesh.faces[hidden_start:] for index in struct.unpack_from('<3H', face, 2)}
            if not required <= ghost.keys() or any(type(index) is not int or not 0 <= index < len(mesh.vertices) for index in ghost):
                raise ValueError("Ghost positions must cover all generated face vertices")
            if any(len(point) != 3 or any(not math.isfinite(v) or abs(v) > 32768 or v * 65536 != round(v * 65536) for v in point)
                   for point in ghost.values()):
                raise ValueError("Ghost positions require finite canonical16.16 coordinates")
        records, groups, referenced = compact_geometry(mesh, hidden_start, ghost)
        source_vertices += len(mesh.vertices)
        referenced_vertices += referenced
        packed_vertices += len(records)
        positions = [struct.unpack_from("<3f", record) for record in records]
        vertex_view = view(b"".join(record[:32] for record in records), byteStride=32, target=34962)
        attributes = {
            "POSITION": accessor(vertex_view, 5126, len(records), "VEC3", byteOffset=0,
                                 min=[min(v[axis] for v in positions) for axis in range(3)],
                                 max=[max(v[axis] for v in positions) for axis in range(3)]),
            "NORMAL": accessor(vertex_view, 5126, len(records), "VEC3", byteOffset=12),
            "TEXCOORD_0": accessor(vertex_view, 5126, len(records), "VEC2", byteOffset=24),
        }
        if ghost is not None:
            ghost_view = view(b"".join(record[32:] for record in records), byteStride=12, target=34962)
            attributes["_TOCA_GHOST_POSITION"] = accessor(ghost_view, 5126, len(records), "VEC3")
        primitives = []
        for (texture, color, ghost_hidden), indices in groups.items():
            index_view = view(struct.pack(f"<{len(indices)}H", *indices), target=34963)
            primitive = {"attributes": attributes,
                         "indices": accessor(index_view, 5123, len(indices), "SCALAR"),
                         "material": material(texture, color), "mode": 4}
            if ghost_hidden:
                primitive["extras"] = {"toca_ghost_hidden": True}
            primitives.append(primitive)
        document["meshes"].append({"name": mesh.header[:12].split(b"\0", 1)[0].decode("ascii"), "primitives": primitives})

    offset = 0
    while offset < len(node_bytes):
        size, parent = struct.unpack_from("<Ih", node_bytes, offset)
        count = node_bytes[offset + 76]
        variants = list(struct.unpack_from(f"<{count}H", node_bytes, offset + 80))
        node = {"name": f"node-{len(document['nodes'])}",
                "matrix": [value / 65536 for value in struct.unpack_from("<16i", node_bytes, offset + 8)],
                "extras": {"toca_id_flags": struct.unpack_from("<I", node_bytes, offset + 72)[0], "toca_variants": variants}}
        if variants and variants[0] != 65535:
            node["mesh"] = variants[0]
        index = len(document["nodes"])
        document["nodes"].append(node)
        if parent < 0:
            document["scenes"][0]["nodes"].append(index)
        else:
            document["nodes"][parent].setdefault("children", []).append(index)
        offset += size

    document["buffers"] = [{"byteLength": len(blob)}]
    compression = {"meshopt_buffer_count": 0, "meshopt_bytes_saved": 0}
    if meshopt_encoder:
        blob, compression = compress_geometry(document, blob, meshopt_encoder)
    text = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    text += b" " * (-len(text) % 4)
    blob.extend(b"\0" * (-len(blob) % 4))
    total = 12 + 8 + len(text) + 8 + len(blob)
    result = (struct.pack("<III", 0x46546c67, 2, total) + struct.pack("<II", len(text), 0x4e4f534a) + text
              + struct.pack("<II", len(blob), 0x004e4942) + blob)
    return result, {"format": "GLB2", "generated_webp_quality": 70, "embedded_webp_bytes": texture_bytes, "embedded_webp_count": texture_count,
                    "source_mesh_vertices": source_vertices, "referenced_mesh_vertices": referenced_vertices, "packed_mesh_vertices": packed_vertices,
                    "removed_unused_vertices": source_vertices - referenced_vertices, "merged_duplicate_vertices": referenced_vertices - packed_vertices,
                    "vertex_bytes_saved": (source_vertices - packed_vertices) * 32,
                    "duplicate_buffer_bytes_saved": duplicate_bytes, **compression}
