#!/usr/bin/env python3
"""Inventory canonical ALLCARS model metadata without exporting original media."""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct

from git_game_data import archive_bytes, bfp_members
from vehicle_model import crc32, decode, node_records, visible_triangles
from vehicle_textures import bmp_dimensions, texture_records

PROJECT = Path(__file__).resolve().parents[1]
ARCHIVE = "data/allcars.bfp"
ARCHIVE_CRC32 = "92273ec9"
OUTPUT = PROJECT / "build/asset-inventories/vehicle-inventory.json"

# Member order: car_member_indices; identities/actors: mega-vehicles-plan.md.
# Bonus selections retain their own physical IDs and replace Ford's actor pair.
ROSTER = (
    ("honda", "Honda Accord", 1, 0, ((0, "J. Thompson"), (1, "G. Tarquini"))),
    ("audi", "Audi A4", 4, 1, ((2, "F. Biela"), (3, "J. Bintcliffe"))),
    ("vectra", "Vauxhall Vectra", 7, 2, ((4, "J. Cleland"),)),
    ("volvo", "Volvo S40", 13, 3, ((6, "R. Rydell"), (7, "K. Burt"))),
    ("ford", "Ford Mondeo", 16, 4, ((8, "W. Hoy"), (9, "P. Radisich"))),
    ("nissan", "Nissan Primera", 19, 5, ((10, "D. Leslie"), (11, "A. Reid"))),
    ("peugeot", "Peugeot 406", 22, 6, ((12, "T. Harvey"), (13, "P. Watts"))),
    ("renault", "Renault Laguna", 25, 7, ((14, "A. Menu"), (15, "J. Plato"))),
    ("tank", "Tank", 28, 8, ((8, "W. Hoy"), (9, "P. Radisich"))),
    ("flexmobile", "Flexmobile", 31, 9, ((8, "W. Hoy"), (9, "P. Radisich"))),
    ("vectra2", "Vectra2", 10, 2, ((5, "D. Warwick"),)),
)




def inspect_model(source, data):
    header, node_bytes, meshes, section = decode(data)
    node_count, mesh_count, texture_count = struct.unpack_from("<3H", header, 12)
    nodes = node_records(node_bytes, mesh_count)
    if len(nodes) != node_count or len(meshes) != mesh_count:
        raise ValueError("CM3D declared counts differ from its parsed records")
    textures = texture_records(section, texture_count)
    bindings = defaultdict(set)
    for node in nodes:
        for index in node["variants"]:
            if index != 65535:
                bindings[index].add(node["index"])
    texture_names = defaultdict(list)
    texture_info = []
    for index, (name, _, payload) in enumerate(textures):
        width, height, bits = bmp_dimensions(payload)
        texture_info.append(dict(index=index, name=name, crc32=crc32(payload), bytes=len(payload),
                                 width=width, height=height, bits=bits))
        texture_names[name].append(index)
    mesh_info = []
    for index, mesh in enumerate(meshes):
        slots = Counter(struct.unpack_from("<h", face, 8)[0] for face in mesh.faces)
        if any(slot < -1 or slot >= texture_count for slot in slots):
            raise ValueError("CM3D mesh references an unknown texture slot")
        mesh_info.append(dict(index=index, name=mesh.header[:12].split(b"\0", 1)[0].decode("ascii"),
                              vertices=len(mesh.vertices), triangles=len(mesh.faces),
                              bytes=struct.unpack_from("<I", mesh.header, 12)[0],
                              bound_nodes=sorted(bindings[index]),
                              texture_slots=[dict(index=slot, name=textures[slot][0] if slot >= 0 else None,
                                                  triangles=count) for slot, count in sorted(slots.items())]))
    return dict(source=source, bytes=len(data), node_count=node_count, mesh_count=mesh_count,
                texture_count=texture_count, nodes=nodes, meshes=mesh_info, textures=texture_info,
                bound_mesh_indices=sorted(index for index, nodes in bindings.items() if nodes),
                unbound_mesh_indices=[index for index in range(mesh_count) if not bindings[index]],
                first_variant_mesh_instances=sum(bool(node["variants"]) and node["variants"][0] != 65535 for node in nodes),
                first_variant_visible_triangles=visible_triangles(node_bytes, meshes),
                stored_triangles=sum(len(mesh.faces) for mesh in meshes),
                duplicate_texture_names={name: indices for name, indices in texture_names.items() if len(indices) > 1})


def inventory(data_root):
    archive = archive_bytes(data_root, "data/allcars.bfp", expected_crc32=ARCHIVE_CRC32)
    members = bfp_members(archive)
    count = len(members)
    if count != 34:
        raise ValueError("Unexpected canonical ALLCARS directory")
    vehicles = []
    for render_id, (key, name, first, physical_id, actors) in enumerate(ROSTER):
        models = {}
        for lod, member in zip(("high", "medium", "low"), range(first, first + 3)):
            member_name, payload = members[member]
            source = dict(archive=ARCHIVE, archive_crc32=ARCHIVE_CRC32, member=member,
                          member_name=member_name, member_crc32=crc32(payload))
            models[lod] = inspect_model(source, payload)
        usage = ["local-player", "opponents", "ghost", "split-screen-players"] if render_id < 10 else ["opponent-actor5"]
        vehicles.append(dict(package_id=f"V{render_id:02d}", key=key, name=name, render_id=render_id,
                             physical_id=physical_id, selectable=render_id < 10, consumers=usage,
                             actors=[dict(actor=index, driver=driver) for index, driver in actors],
                             actor_binding="conditional-ford-pair-replacement" if render_id in (8, 9) else "normal",
                             models=models))
    return dict(version=1, scope="Eleven canonical ALLCARS exterior sets; no original media exported.",
                command="python3 tools/inventory-vehicles.py --data-root DIR",
                archive=dict(path=ARCHIVE, crc32=ARCHIVE_CRC32, bytes=len(archive), members=count),
                evidence=["src/frontend/resources.cpp:car_member_indices", "src/frontend/resources.cpp:car_livery_materials",
                          "docs/mega-vehicles-plan.md:ALLCARS Inventory", "docs/scene-models.md:Vehicle families and rival detail",
                          "docs/cheats.md:Bonus cars", "docs/cm3d.md:Header and nodes"],
                conventions=dict(identity="Archive path + member index + member CRC32; stored16-byte names are labels only.",
                                 variants="Slot order is preserved;65535 means no mesh. Bindings include all variants.",
                                 matrix="Row-vector local4x4, signed16.16; pivot_local is its translation at indices12..14.",
                                 visible_triangles="First variant per node, counting repeated wheel instances; no inferred damage state.",
                                 stored_triangles="Sum of every stored mesh once, including unbound meshes and alternate variants.",
                                 duplicate_names="Canonical texture names may repeat; indices remain distinct.",
                                 bonus_actors="Tank/Flexmobile conditionally replace actors8/9 and retain the original Ford driver bank.",
                                 track_candidates="T0 remains the unconfirmed metadata candidate list in docs/mega-vehicles-plan.md; no visual classification is claimed."),
                vehicles=vehicles)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path, required=True, help='Unpacked Git game-data root')
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    document = inventory(args.data_root)
    raw = (json.dumps(document, indent=2) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    print(json.dumps(dict(vehicles=len(document["vehicles"]), bytes=len(raw), crc32=crc32(raw), output=str(args.output))))


if __name__ == "__main__":
    main()
