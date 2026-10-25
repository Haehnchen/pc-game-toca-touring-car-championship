#!/usr/bin/env python3
"""Reproduce the nine canonical DASX cockpit contracts without exporting media."""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct

from git_game_data import archive_bytes, bfp_members
from vehicle_model import PROJECT, crc32, decode, node_records
from vehicle_textures import texture_records, bmp_pixels


OUTPUT = PROJECT / 'build/asset-inventories/cockpit-inventory.json'
CONSUMERS = (
    (0, 'honda', 'hond', '6c68f4a3', 0),
    (1, 'audi', 'audi', 'f3f7ab71', 1),
    (2, 'vectra', 'vect', '2949934a', 1),
    (3, 'volvo', 'volv', '30f85a03', 0),
    (4, 'ford', 'ford', '505d1bfb', 1),
    (5, 'nissan', 'niss', '08de1330', 0),
    (6, 'peugeot', 'p406', '9a0f77d6', 0),
    (7, 'renault', 'ren', 'fd3542e3', 1),
    (9, 'flexmobile', 'flex', '038cfaae', 0),
)
ROLES = ('left-hand', 'right-hand', 'dashboard', 'wiper', 'wheel')


def archive_members(data):
    members = bfp_members(data)
    if len(members) != 5:
        raise ValueError('A canonical DASX archive must contain exactly five members')
    return members


def nodes_metadata(data, mesh_count):
    return [{key: node[key] for key in ('parent', 'id_flags', 'matrix_16_16', 'variants')}
            for node in node_records(data, mesh_count)]


def bitmap_metadata(bmp):
    """Hash decoded palette content and exact-RGB-black alpha, not index zero."""
    if len(bmp) < 54 or bmp[:2] != b'BM' or struct.unpack_from('<H', bmp, 28)[0] != 8:
        raise ValueError('Cockpit inventory requires canonical indexed BMPs')
    width, height, pixels = bmp_pixels(bmp)
    rgba, alpha = bytearray(), bytearray()
    for rgb in pixels:
        a = 0 if rgb == (0, 0, 0) else 255
        rgba.extend((*rgb, a))
        alpha.append(a)
    return {'bmp_crc32': crc32(bmp), 'bytes': len(bmp), 'size': [width, height], 'bits': 8,
            'decoded_rgba_crc32': crc32(rgba), 'alpha_mask_crc32': crc32(alpha),
            'transparent_pixels': alpha.count(0), 'opaque_pixels': alpha.count(255),
            'alpha_rule': 'Exact decoded RGB(0,0,0) is transparent; every other palette color is opaque.'}


def fixed_records(mesh, fields):
    return b''.join(struct.pack(f'<{len(fields)}i', *(round(v[i] * 65536) for i in fields)) for v in mesh.vertices)


def model_metadata(raw):
    header, nodes, meshes, textures = decode(raw)
    ns = nodes_metadata(nodes, len(meshes))
    records = texture_records(textures, struct.unpack_from('<H', header, 16)[0])
    material_info = [{'slot': i, 'name': name, **bitmap_metadata(bmp)} for i, (name, _, bmp) in enumerate(records)]
    info = []
    for index, mesh in enumerate(meshes):
        vertices = [tuple(round(x * 65536) for x in v) for v in mesh.vertices]
        bounds = [[min(v[a] for v in vertices), max(v[a] for v in vertices)] for a in range(3)]
        edges = Counter()
        for face in mesh.faces:
            points = [vertices[i][:3] for i in struct.unpack_from('<3H', face, 2)]
            edges.update(tuple(sorted((a, b))) for a, b in zip(points, points[1:] + points[:1]))
        slots = Counter(struct.unpack_from('<h', f, 8)[0] for f in mesh.faces)
        if any(slot < -1 or slot >= len(records) for slot in slots):
            raise ValueError('Unknown cockpit texture slot')
        info.append({'mesh': index, 'name': mesh.header[:12].split(b'\0', 1)[0].decode('ascii'),
                     'vertices': len(vertices), 'triangles': len(mesh.faces), 'raw_mesh_crc32': crc32(mesh.encode()),
                     'vertex_records_crc32': crc32(fixed_records(mesh, range(8))),
                     'xyz_crc32': crc32(fixed_records(mesh, range(3))),
                     'uv_crc32': crc32(fixed_records(mesh, (3, 4))),
                     'normal_crc32': crc32(fixed_records(mesh, (5, 6, 7))),
                     'face_records_crc32': crc32(b''.join(mesh.faces)),
                     'index_topology_crc32': crc32(b''.join(f[2:8] for f in mesh.faces)),
                     'bounds_16_16': bounds, 'dimensions_m': [(b - a) / 65536 for a, b in bounds],
                     'vertex_uvs_16_16': [list(v[3:5]) for v in vertices],
                     'material_faces': [{'slot': slot, 'faces': [i for i, f in enumerate(mesh.faces) if struct.unpack_from('<h', f, 8)[0] == slot]}
                                        for slot in sorted(slots)],
                     'bindings': [[ni, vi] for ni, node in enumerate(ns) for vi, mi in enumerate(node['variants']) if mi == index],
                     'open_boundary_edges': sum(n == 1 for n in edges.values()),
                     'nonmanifold_edges': sum(n > 2 for n in edges.values())})
    return {'bytes': len(raw), 'crc32': crc32(raw), 'node_section_crc32': crc32(nodes),
            'mesh_section_crc32': crc32(b''.join(mesh.encode() for mesh in meshes)),
            'xyz_crc32': crc32(b''.join(fixed_records(mesh, range(3)) for mesh in meshes)),
            'uv_crc32': crc32(b''.join(fixed_records(mesh, (3, 4)) for mesh in meshes)),
            'normal_crc32': crc32(b''.join(fixed_records(mesh, (5, 6, 7)) for mesh in meshes)),
            'texture_section_crc32': crc32(textures), 'nodes': ns, 'meshes': info, 'textures': material_info}


def inventory(data_root):
    archives, payloads, geometry, dash_shapes = [], defaultdict(list), defaultdict(list), defaultdict(list)
    models, mesh_records, bitmaps = {}, {}, {}
    for car_id, key, prefix, expected, side in CONSUMERS:
        relative = f'data/{prefix}dasx.bfp'
        raw = archive_bytes(data_root, relative, expected_crc32=expected)
        members = []
        for member, (name, data) in enumerate(archive_members(raw)):
            metadata = model_metadata(data)
            checksum = metadata['crc32']
            if checksum not in models:
                shared = dict(metadata)
                shared['meshes'] = []
                for mesh in metadata['meshes']:
                    mesh_id = mesh['raw_mesh_crc32']
                    value = {k: v for k, v in mesh.items() if k not in ('mesh', 'bindings')}
                    if mesh_id in mesh_records and mesh_records[mesh_id] != value:
                        raise ValueError('Cockpit mesh CRC32 metadata collision')
                    mesh_records[mesh_id] = value
                    shared['meshes'].append({'mesh': mesh['mesh'], 'record': mesh_id, 'bindings': mesh['bindings']})
                shared['textures'] = []
                for texture in metadata['textures']:
                    bitmap_id = texture['bmp_crc32']
                    value = {k: v for k, v in texture.items() if k not in ('slot', 'name')}
                    if bitmap_id in bitmaps and bitmaps[bitmap_id] != value:
                        raise ValueError('Cockpit bitmap CRC32 metadata collision')
                    bitmaps[bitmap_id] = value
                    shared['textures'].append({k: texture[k] for k in ('slot', 'name', 'bmp_crc32')})
                models[checksum] = shared
            record = {'member': member, 'member_name': name,
                      'stored_name_hex': raw[12 + member * 24:28 + member * 24].hex(),
                      'role': ROLES[member], 'model': checksum}
            members.append(record)
            payloads[metadata['crc32']].append([car_id, member])
            geometry[(ROLES[member], metadata['mesh_section_crc32'])].append(car_id)
            if member == 2:
                dash_shapes[metadata['xyz_crc32']].append(car_id)
        archives.append({'car_id': car_id, 'key': key, 'archive': relative, 'archive_crc32': expected,
                         'source_side_flag': side, 'driver_side': 'left' if side else 'right',
                         'driver_body_z_sign': 1 if side else -1, 'members': members})
    return {'version': 1, 'command': 'python3 tools/inventory-cockpits.py --data-root DIR',
            'source': 'Versioned canonical1.1a Git inputs; checksums are IEEE/zlib CRC32, eight hexadecimal digits.',
            'scope': 'Nine DASX archives, five original roles each; tank8 and separate exterior render-ID10 have no additional cockpit branch.',
            'conventions': {'matrix': 'Original row-vector signed16.16; translation indices12..14; no driver-side mirroring.',
                            'uv': 'Original signed16.16 UV values; native image V is rawV+1.',
                            'reuse': 'Whole-payload, mesh, XYZ, UV, normal and image identities remain separate; names alone are not aliases.',
                            'alpha': 'Canonical exact-RGB-black key; encoded Mega alpha has its own authored source contract.',
                            'tables': 'Archive member.model resolves models; model meshes resolve mesh_records, textures resolve bitmaps. Shared records occur once.'},
            'dashboard_runtime': {'static': True, 'source_transform_16_16': [65536, 0, 0, 0, 0, 0, 65536, 0, 0, -65536, 0, 0, -6553, 3276, 0, 65536],
                                  'visibility': 'Cockpit view1, hidden together with wiper by CMCHUN; no live gauge/needle consumer.',
                                  'source_consumer': 'src/scene/cockpit.cpp CockpitState::frame; recovered0042de80/0042bff0.',
                                  'fallback': 'Missing/incompatible per-role Mega override retains the complete canonical role; detail0..3 retains source.'},
            'summary': {'archives': len(archives), 'members': sum(len(a['members']) for a in archives),
                        'unique_complete_payloads': len(payloads), 'dashboard_xyz_groups': len(dash_shapes)},
            'payload_reuse': [{'crc32': checksum, 'consumers': consumers} for checksum, consumers in sorted(payloads.items())],
            'mesh_reuse': [{'role': role, 'mesh_section_crc32': checksum, 'car_ids': cars} for (role, checksum), cars in sorted(geometry.items())],
            'dashboard_xyz_groups': [{'xyz_crc32': checksum, 'car_ids': cars} for checksum, cars in sorted(dash_shapes.items())],
            'models': dict(sorted(models.items())), 'mesh_records': dict(sorted(mesh_records.items())), 'bitmaps': dict(sorted(bitmaps.items())),
            'archives': archives}


def encode(document):
    return (json.dumps(document, indent=2) + '\n').encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path, required=True, help='Unpacked Git game-data root')
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    document = inventory(args.data_root)
    raw = encode(document)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    print(json.dumps(dict(crc32=crc32(raw), bytes=len(raw), output=str(args.output))))


if __name__ == '__main__':
    main()
