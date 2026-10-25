"""Honda's original static dashboard skin with thin closed backing surfaces."""

import json
from pathlib import Path
import struct

from vehicle_model import Mesh, add, crc32, cross, decode, dot, pack, scale, sub, unit, visible_triangles
from vehicle_textures import artwork_files, replace_textures, texture_records, webp_dimensions


DEFINITION = Path(__file__).resolve().parents[1] / 'assets/models/cockpits/honda/definition.json'


def _geometry(originals, thickness):
    """Close the whole four-material skin, without duplicate walls at its joins."""
    faces, edges, incident = {}, {}, {}
    for mi, mesh in enumerate(originals):
        for fi, face in enumerate(mesh.faces):
            vertices = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            points = [v[:3] for v in vertices]
            normal = unit(cross(sub(points[1], points[0]), sub(points[2], points[0])))
            faces[mi, fi] = normal
            for p in points:
                incident.setdefault(p, set()).add((mi, fi))
            for a, b in zip(vertices, vertices[1:] + vertices[:1]):
                key = tuple(sorted((a[:3], b[:3])))
                edges.setdefault(key, []).append((mi, fi, a, b, face))
    if any(len(uses) > 2 for uses in edges.values()):
        raise ValueError('Honda dashboard source skin has a nonmanifold edge')
    backs, corner_fans, point_fans = {}, {}, []
    for p, owners in sorted(incident.items()):
        neighbors = {owner: set() for owner in owners}
        for edge, uses in edges.items():
            if p in edge and len(uses) == 2:
                a, b = [use[:2] for use in uses]
                neighbors[a].add(b)
                neighbors[b].add(a)
        remaining, groups = set(owners), []
        while remaining:
            pending, group = [min(remaining)], set()
            while pending:
                owner = pending.pop()
                if owner in group:
                    continue
                group.add(owner)
                pending.extend(neighbors[owner] - group)
            remaining -= group
            groups.append(sorted(group))
        descriptions = []
        for group in groups:
            fan = group[0]
            normals = sorted({faces[owner] for owner in group})
            total = tuple(sum(n[a] for n in normals) for a in range(3))
            if dot(total, total) < 1e-12:
                raise ValueError('Honda dashboard backing has an ambiguous source corner')
            # Source sheets also touch at isolated points. Only edge-connected
            # fans share a back corner; joining point-only sheets would create
            # four wall faces on the same extruded edge.
            backs[p, fan] = tuple(round(x * 65536) / 65536 for x in sub(p, scale(unit(total), thickness)))
            for owner in group:
                corner_fans[p, owner] = fan
            descriptions.append({'source_faces': group, 'back_position': backs[p, fan]})
        if len(groups) > 1:
            point_fans.append({'source_position_16_16': [round(x * 65536) for x in p], 'fans': descriptions})

    def back(p, owner):
        return backs[p, corner_fans[p, owner]]
    results, details = [], []
    for mi, mesh in enumerate(originals):
        result = Mesh(mesh.header, list(mesh.vertices), list(mesh.faces))
        first = len(result.faces)
        for fi, face in enumerate(mesh.faces):
            source = [mesh.vertices[i] for i in struct.unpack_from('<3H', face, 2)]
            normal = scale(unit(cross(sub(source[1][:3], source[0][:3]), sub(source[2][:3], source[0][:3]))), -1)
            indices = [result.vertex(back(v[:3], (mi, fi)), normal, v[3:5]) for v in source]
            result.triangle(indices, tuple(face[10:14]), struct.unpack_from('<h', face, 8)[0])
        results.append(result)
        details.append({'source_faces': len(mesh.faces), 'back_face_range': [first, len(result.faces)], 'wall_faces': []})
    for uses in edges.values():
        if len(uses) != 1:
            continue
        mi, fi, a, b, face = uses[0]
        result = results[mi]
        points = (a[:3], back(a[:3], (mi, fi)), back(b[:3], (mi, fi)), b[:3])
        normal = unit(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        if dot(cross(sub(points[1], points[0]), sub(points[2], points[0])),
               cross(sub(points[1], points[0]), sub(points[2], points[0]))) < 1e-16:
            raise ValueError('Honda dashboard backing creates a degenerate boundary wall')
        first = len(result.faces)
        indices = [result.vertex(p, normal, uv) for p, uv in zip(points, (a[3:5], a[3:5], b[3:5], b[3:5]))]
        color, slot = tuple(face[10:14]), struct.unpack_from('<h', face, 8)[0]
        result.triangle(indices[:3], color, slot)
        result.triangle((indices[0], indices[2], indices[3]), color, slot)
        details[mi]['wall_faces'].extend(range(first, len(result.faces)))
    return results, details, point_fans


def generate(raw, car_id=0, texture_directory=None):
    """Preserve Honda's source actor/material contract; no other dash is authored."""
    definition = json.loads(DEFINITION.read_text())
    if car_id != 0 or crc32(raw) != definition['source']['member_crc32']:
        raise ValueError('Honda dashboard geometry requires canonical Honda member2')
    header, nodes, originals, textures = decode(raw)
    if ([m.header[:12].split(b'\0', 1)[0].decode('ascii') for m in originals] != definition['mesh_names']
            or [crc32(m.encode()) for m in originals] != definition['source_mesh_crc32']):
        raise ValueError('Honda dashboard source mesh contract differs')
    records = texture_records(textures, struct.unpack_from('<H', header, 16)[0])
    if [(name, crc32(bmp)) for name, _, bmp in records] != [(m['name'], m['source_bmp_crc32']) for m in definition['materials']]:
        raise ValueError('Honda dashboard original material identities differ')
    meshes, details, point_fans = _geometry(originals, definition['backing_thickness_m'])
    if texture_directory is not None:
        files = artwork_files(texture_directory, [name for name, _, _ in records])
        if set(files) != {name for name, _, _ in records}:
            raise ValueError('Honda dashboard requires all four final material images')
        for material in definition['materials']:
            path = files[material['name']]
            if path.suffix != '.webp':
                raise ValueError('Honda dashboard artwork must be final WebP')
            width, height = webp_dimensions(path.read_bytes())
            sw, sh = material['source_size']
            if max(width, height) > 512 or width * sh != height * sw:
                raise ValueError('Honda dashboard artwork must retain source aspect within512px')
        textures = replace_textures(textures, len(records), texture_directory)
    output = pack(header, nodes, meshes, textures)
    report = {'role': 'dashboard', 'source_car_id': car_id, 'source_model_crc32': crc32(raw),
              'output_crc32': crc32(output), 'definition_crc32': crc32(DEFINITION.read_bytes()),
              'backing_thickness_m': definition['backing_thickness_m'], 'source_side_flag': 0, 'driver_side': 'right',
              'nodes_unchanged': True, 'source_front_skin_unchanged': True,
              'source_images_fully_opaque': True, 'mesh_details': details, 'source_point_fans': point_fans,
              'vertices': sum(len(m.vertices) for m in meshes), 'stored_triangles': sum(len(m.faces) for m in meshes),
              'original_visible_triangles': visible_triangles(nodes, originals), 'visible_triangles': visible_triangles(nodes, meshes),
              'bytes': len(output),
              'bounds_m': [[[min(v[a] for v in mesh.vertices), max(v[a] for v in mesh.vertices)] for a in range(3)] for mesh in meshes],
              'limits': 'Eight-millimetre hidden backing, exact source front/UV/padding, no gauge animation; nearclip/pillar/join acceptance is native.'}
    return output, report
