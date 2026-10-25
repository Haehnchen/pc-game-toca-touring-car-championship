#!/usr/bin/env python3
"""Reproduce canonical FREE scenery metadata; never export original media."""

import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re
import struct
import zlib


from git_game_data import archive_bytes, bfp_members as archive_members

PROJECT = Path(__file__).resolve().parents[1]
OUTPUT = PROJECT / "build/asset-inventories/scenery-inventory.json"
MAPPING = PROJECT / "assets/models/mega/texture-replacements.json"
ACCEPTANCE = PROJECT / "assets/models/mega/scenery-acceptance.json"
PREFIXES = ("donl", "silv", "thru", "bran", "oult", "dons", "crof", "knoc", "snet", "land")
FAMILIES = {2: "trees", 3: "fence", 5: "objects", 8: "cones", 9: "posts", 10: "signs", 11: "flags"}


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def label(raw):
    return raw.split(b"\0", 1)[0].decode("ascii")




def parse_model(data):
    """Small bounded metadata reader, independent of the model authoring tools."""
    if len(data) < 32 or data[:4] != b"CM3D" or struct.unpack_from("<II", data, 4) != (100, 32):
        raise ValueError("Expected CM3D100")
    nc, mc, tc = struct.unpack_from("<3H", data, 12)
    ns, ms, ts = struct.unpack_from("<3I", data, 20)
    if ns != 32 or not ns <= ms <= ts <= len(data):
        raise ValueError("Invalid CM3D section bounds")
    nodes, offset, root = [], ns, None
    for index in range(nc):
        if offset + 80 > ms:
            raise ValueError("Truncated CM3D node")
        size, parent = struct.unpack_from("<Ih", data, offset)
        count = data[offset + 76]
        if size != ((80 + count * 2 + 3) & ~3) or offset + size > ms or parent < -1:
            raise ValueError("Invalid CM3D node bounds")
        if parent == -1:
            root = index
        if root is None:
            raise ValueError("CM3D node has no preceding root")
        variants = list(struct.unpack_from(f"<{count}H", data, offset + 80))
        if any(mesh != 65535 and mesh >= mc for mesh in variants):
            raise ValueError("CM3D node references unknown mesh")
        nodes.append(dict(parent=parent, flags=struct.unpack_from("<I", data, offset + 72)[0],
                          variants=variants, matrix=list(struct.unpack_from("<16i", data, offset + 8)), root=root))
        offset += size
    if offset != ms:
        raise ValueError("Unaccounted CM3D node bytes")
    meshes = []
    for _ in range(mc):
        if offset + 48 > ts:
            raise ValueError("Truncated CM3D mesh")
        size, vc, fc = struct.unpack_from("<IHH", data, offset + 12)
        if size != 48 + vc * 32 + fc * 24 or offset + size > ts:
            raise ValueError("Invalid CM3D mesh bounds")
        raw = data[offset:offset + size]
        vertices = [struct.unpack_from("<8i", raw, 48 + i * 32) for i in range(vc)]
        fs = 48 + vc * 32
        faces = [raw[fs + i * 24:fs + (i + 1) * 24] for i in range(fc)]
        for face in faces:
            if max(struct.unpack_from("<3H", face, 2)) >= vc:
                raise ValueError("CM3D face references unknown vertex")
            if not -1 <= struct.unpack_from("<h", face, 8)[0] < tc:
                raise ValueError("CM3D face references unknown material")
        meshes.append(dict(name=label(raw[:12]), raw=raw, vertices=vertices, faces=faces))
        offset += size
    if offset != ts:
        raise ValueError("Unaccounted CM3D mesh bytes")
    materials = []
    for _ in range(tc):
        if offset + 16 > len(data):
            raise ValueError("Truncated CM3D texture record")
        name, size = struct.unpack_from("<12sI", data, offset)
        bmp = data[offset + 16:offset + 16 + size]
        if len(bmp) != size or len(bmp) < 54 or bmp[:2] != b"BM":
            raise ValueError("Invalid CM3D BMP payload")
        width, height = struct.unpack_from("<ii", bmp, 18)
        if width <= 0 or height == 0:
            raise ValueError("Invalid CM3D BMP size")
        materials.append(dict(name=label(name), crc32=crc32(bmp), size=[width, abs(height)],
                              bits=struct.unpack_from("<H", bmp, 28)[0], bytes=size))
        offset += 16 + size
    # Empty FLAGS/TREES carry four unused exporter bytes (not necessarily zero);
    # counted families may carry the documented single NULL.
    permitted = (b"", b"\0")
    if data[offset:] not in permitted and not (not (nc or mc or tc) and len(data) - offset == 4):
        raise ValueError("Unaccounted CM3D texture bytes")
    return nodes, meshes, materials


def semantic_package(name, member, track):
    """Names are routing evidence, never a claim of completed visual QA."""
    if member == 2:
        return "M02", "forest-strip", "source-family"
    if member == 3:
        return "M03", "boundary", "source-family"
    if member == 8 and track == 9:
        return "M05e", "gargoyle", "plan-confirmed"
    if member in (8, 9, 10):
        return "M08", {8: "cone", 9: "post", 10: "track-sign"}[member], "source-family"
    if member == 11:
        return "M06", "flag-or-mast", "source-family"
    if member == 5 and track in (0, 5):
        # Canonical artwork/shape confirms these roles; names alone do not.
        # Source bindings are recorded in the static vehicle generation recipes.
        confirmed = {
            "DA12SPTF_1": ("T0", "static-spitfire"),
            "DA41TRA_1": ("T0", "truck-and-trailer"),
            "CRAN3_1": ("T0", "vehicle-mounted-work-platform"),
            "DA44TRNS_1": (f"M07-{track}", "entrance-turnstile-building"),
            "DA80FOSC_1": (f"M07-{track}", "advertising-can-structure"),
        }
        if name in confirmed:
            package, kind = confirmed[name]
            return package, kind, "source-texture-confirmed"
    if member == 5 and track == 1 and name == "CARROW_1":
        # CARSDE1/3 and CARROW2 depict parked cars, not direction markers.
        return "T0", "parked-car-row", "source-texture-confirmed"
    if member == 5 and track in (1, 3) and name == "BH17HEL_1":
        # The fixed rotor and landing skids belong to the FREE static helicopter.
        return "T0", "static-helicopter", "source-texture-confirmed"
    if member == 5 and ((name == "TV_1" and track in (1, 6, 8)) or (name == "TV01_1" and track == 2)):
        # Original advertising artwork on a zero-depth mast/poster structure,
        # not a road vehicle despite its shared Vectra/other publicity crops.
        return f"M07-{track}", "advertising-or-broadcast-mast", "source-texture-confirmed"
    if member == 5 and track == 2 and name == "TH03AIRP_1":
        # TH03AI facade photographs and the building body confirm an airport.
        return "M07-2", "airport-building", "source-texture-confirmed"
    if member == 5 and track == 2 and name in ("TH36APLN_1", "TH37APLN_1"):
        # Parked aircraft share wing/gear materials and retain static FREE actors.
        return "T0", "static-aircraft", "source-texture-confirmed"
    if member == 5 and track in (2, 4) and name == "CRAN3_1":
        # Thruxton/Oulton share the confirmed Donington work-platform body.
        return "T0", "vehicle-mounted-work-platform", "source-texture-confirmed"
    if member == 5 and track in (3, 6, 7, 8) and name == "LVECTRA_1":
        # Vectra is the promotion on a box truck, not a parked ALLCARS car.
        return "T0", "promotional-box-truck", "source-texture-confirmed"
    if member == 5:
        # Shapes, source photographs and actual HIGH bindings confirm this
        # service fleet. Reconstructed lettering is not identity evidence.
        fleet = {
            "LVOLVO_1": ((3, 6, 7, 8), "transport-box-truck"),
            "LAMBULAN_1": ((3, 6, 7), "rescue-van"),
            "LFIRETRK_1": ((7, 8), "fire-rescue-vehicle"),
            "LRACJEEP_1": ((8,), "track-support-4x4"),
            "LCAR1_1": ((3,), "parked-car-row"),
            "CR11CVAN_1": ((6,), "static-caravan"),
            "CR03BUS2_1": ((6,), "double-decker-bus"),
            "BH06WT2_1": ((6,), "race-team-transporter"),
        }
        if name in fleet and track in fleet[name][0]:
            return "T0", fleet[name][1], "source-texture-confirmed"
    if name.startswith(("CRAN", "LVECTRA", "LVOLVO", "LAMBULAN", "LFIRETRK", "LRACJEEP", "LCAR", "CR11CVAN", "CR03BUS", "BH17HEL", "TH36APLN", "TH37APLN")):
        return "T0", "vehicle-crane-or-static-aircraft-candidate", "name-candidate"
    if name == "TRIS04_1":
        return "M01a", "near-tree", "plan-confirmed"
    if name == "TRIS07_1":
        return "M01b", "near-tree", "plan-confirmed"
    if name == "TRGE17_1":
        return "M01c", "tree-group", "plan-confirmed"
    if name == "TRIS04B_1":
        return "M01d-a", "large-near-tree", "plan-confirmed"
    if name == "TRIS02B_1":
        return "M01d-b", "near-tree", "plan-confirmed"
    if name == "TRIE01_1":
        return "M01d-c", "narrow-near-tree", "plan-confirmed"
    if name == "TRGS18_1":
        return "M01d-d", "tree-group", "plan-confirmed"
    if name == "TRGS22_1":
        return "M01d-e", "tree-group", "plan-confirmed"
    if name == "TRGS20_1":
        return "M01d-f", "tree-group", "plan-confirmed"
    if name.startswith("TR"):
        return "M01d", "vegetation-candidate", "name-candidate"
    if name.startswith(("LPEOP", "LCROWD", "SMLCRWD", "PEOBACK", "PEOMID", "LGIRLS")):
        return "M04a", "people-candidate", "name-candidate"
    if name.startswith("LMARSH"):
        return "M04b", "marshal-candidate", "name-candidate"
    if name == "PCREWS_1":
        return "M04c", "pit-crew", "plan-confirmed"
    for prefixes, package, kind in (
        (("LSPEAKER",), "M05a", "speaker"), (("LBIN",), "M05b", "bin"),
        (("FOTENT", "NITENT", "CR15TENT", "CR16TABL", "BH24TSB", "LHOTDOG"), "M05c", "tent-table-or-stall"),
        (("LSHEEP",), "M05d", "sheep"),
    ):
        if name.startswith(prefixes):
            return package, kind + "-candidate", "name-candidate"
    if name.startswith(("DA09AD", "CHERV", "CARROW")):
        return "M08", "advertising-or-marker-candidate", "name-candidate"
    return f"M07-{track}", "building-or-track-structure-candidate", "name-candidate"


def shape(mesh):
    """Conservative face-plane evidence, with explicit quantization tolerance."""
    groups = []
    degenerates = 0
    for face in mesh["faces"]:
        a, b, c = [mesh["vertices"][i][:3] for i in struct.unpack_from("<3H", face, 2)]
        ab, ac = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
        n = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
        length = math.sqrt(sum(v * v for v in n))
        if not length:
            degenerates += 1
            continue
        n = tuple(v / length for v in n)
        for group in groups:
            if abs(sum(v * w for v, w in zip(n, group["normal"]))) >= .9995:
                group["points"].extend((a, b, c))
                break
        else:
            groups.append(dict(normal=n, points=[a, b, c]))
    spread = max((max(v[i] for v in mesh["vertices"]) - min(v[i] for v in mesh["vertices"]) for i in range(3)), default=0)
    planes = all(max(sum(x * y for x, y in zip(p, g["normal"])) for p in g["points"]) -
                 min(sum(x * y for x, y in zip(p, g["normal"])) for p in g["points"]) <= max(2, spread * .01)
                 for g in groups)
    orthogonal = len(groups) == 2 and abs(sum(a * b for a, b in zip(groups[0]["normal"], groups[1]["normal"]))) < .05
    kind = "planar" if len(groups) == 1 and planes else "crossed-planes" if orthogonal and planes else "multiple-plane-surfaces"
    if not groups:
        kind = "empty-or-degenerate"
    return dict(kind=kind, normal_groups=len(groups), degenerate_triangles=degenerates)


def identity(mesh, material_ids):
    """Ignore labels/global slot numbers; compare exact geometry, UV and materials."""
    data = bytearray(struct.pack("<II", len(mesh["vertices"]), len(mesh["faces"])))
    data.extend(mesh["raw"][48:48 + len(mesh["vertices"]) * 32])
    for face in mesh["faces"]:
        slot = struct.unpack_from("<h", face, 8)[0]
        material = material_ids[slot] if slot >= 0 else "solid"
        data.extend(face[:8] + face[10:] + material.encode("ascii") + b"\0")
    return bytes(data)


def materialized_flags(nodes):
    """Mirror decode_props' owned actor resolution, including Brands' overwrite."""
    result, dynamic = [], False
    for source, original in enumerate(nodes):
        node = dict(original, source=source)
        if node["parent"] == -1:
            dynamic = not bool(node["flags"] & 0x20000000)
        if not dynamic:
            continue
        if node["parent"] == -1 or node["parent"] < len(result):
            result.append(node)
        else:
            if not result:
                raise ValueError("Unresolved FLAGS node has no owned actor")
            previous = result[-1]
            node["parent"] = previous["parent"]
            node["flags"] = (previous["flags"] & 0xff000000) | (node["flags"] & 0xffffff)
            result[-1] = node
    return result


def apply_acceptance(rows, document):
    """Apply authored status only to the exact package and source type."""
    if document.get("version") != 1 or not isinstance(document.get("packages"), list):
        raise ValueError("Scenery acceptance requires version1 and explicit packages")
    applied = set()
    available = {(row["package"], row["type"]) for row in rows}
    for package in document["packages"]:
        package_id, types = package.get("id"), package.get("types")
        status = package.get("status")
        if (not isinstance(package_id, str) or not isinstance(types, list) or not types or
                any(not isinstance(tid, str) or re.fullmatch("[0-9a-f]{8}", tid) is None for tid in types) or
                status != "accepted"):
            raise ValueError("Scenery acceptance requires explicit type IDs and accepted status")
        selected = {(package_id, tid) for tid in types}
        if not selected <= available or applied.intersection(selected):
            raise ValueError("Scenery acceptance has unknown or repeated package/type bindings")
        applied.update(selected)
        for row in rows:
            if (row["package"], row["type"]) in selected:
                row["status"] = status


def inventory(data_root):
    mapping_bytes = MAPPING.read_bytes()
    mapping = json.loads(mapping_bytes)
    if mapping.get("version") != 1 or mapping.get("webp_quality") not in (70, 80):
        raise ValueError("Expected version1 mapping with recorded WebP quality70/80")
    archives, resources, materials, transforms, types, rows = [], {}, {}, {}, {}, []
    material_keys, type_keys, resource_keys = {}, {}, {}
    for track, prefix in enumerate(PREFIXES):
        relative = "data/" + prefix + "free.bfp"
        raw_archive = archive_bytes(data_root, relative)
        checksum = crc32(raw_archive)
        members = archive_members(raw_archive)
        archive = dict(track=track, path=relative, crc32=checksum,
                       bytes=len(raw_archive), members={})
        archives.append(archive)
        for member, family in FAMILIES.items():
            member_name, raw = members[member]
            rid = crc32(raw)
            archive["members"][str(member)] = rid
            if rid in resource_keys and resource_keys[rid] != raw:
                raise ValueError("Source member CRC32 collision")
            nodes, meshes, slots = parse_model(raw)
            mids = []
            for material in slots:
                signature = json.dumps(material, sort_keys=True, separators=(",", ":")).encode()
                mid = crc32(signature)
                if mid in material_keys and material_keys[mid] != signature:
                    raise ValueError("Material identity CRC32 collision")
                material_keys[mid] = signature
                materials.setdefault(mid, dict(material, ai=None))
                mids.append(mid)
            packed_nodes = []
            for node in nodes:
                matrix = node["matrix"]
                tid = crc32(struct.pack("<16i", *matrix))
                if tid in transforms and transforms[tid] != matrix:
                    raise ValueError("Transform CRC32 collision")
                transforms[tid] = matrix
                packed_nodes.append([node["parent"], node["flags"], node["variants"], tid, node["root"]])
            if rid not in resources:
                resource_keys[rid] = raw
                resources[rid] = dict(name=member_name, family=family, bytes=len(raw),
                                      material_slots=mids, nodes=packed_nodes)
                if member == 11:
                    resources[rid]["dynamic_nodes"] = [
                        [n["source"], n["parent"], n["flags"], n["variants"], crc32(struct.pack("<16i", *n["matrix"]))]
                        for n in materialized_flags(nodes)]
            bindings = defaultdict(list)
            for ni, node in enumerate(nodes):
                for variant, mi in enumerate(node["variants"]):
                    if mi != 65535:
                        bindings[mi].append([ni, variant])
            for mi, mesh in enumerate(meshes):
                signature = identity(mesh, mids)
                tid = crc32(signature)
                if tid in type_keys and type_keys[tid] != signature:
                    raise ValueError("Type identity CRC32 collision")
                type_keys[tid] = signature
                if tid not in types:
                    verts = mesh["vertices"]
                    counts = Counter(mids[slot] if slot >= 0 else "solid" for slot in
                                     (struct.unpack_from("<h", face, 8)[0] for face in mesh["faces"]))
                    types[tid] = dict(vertices=len(verts), triangles=len(mesh["faces"]),
                                     bounds_16_16=[[min(v[a] for v in verts), max(v[a] for v in verts)] for a in range(3)] if verts else [],
                                     uv_16_16=[[min(v[a] for v in verts), max(v[a] for v in verts)] for a in (3, 4)] if verts else [],
                                     materials=[[mid if mid != "solid" else None, count] for mid, count in sorted(counts.items())],
                                     shape=shape(mesh))
                package, kind, evidence = semantic_package(mesh["name"], member, track)
                rows.append(dict(track=track, member=member, mesh=mi, name=mesh["name"],
                                 source_crc32=crc32(mesh["raw"]), type=tid, nodes=bindings[mi],
                                 package=package, kind=kind, evidence=evidence, status="source-inventoried"))
    # Mega's shared texture cache is keyed by BMP CRC, including palette bytes.
    # Resolve names collectively per byte identity, as the normal builder does.
    names_by_crc = defaultdict(set)
    for material in materials.values():
        names_by_crc[material["crc32"]].add(material["name"])
    generated = {}
    for entry in mapping["replacements"]:
        for material in materials.values():
            if (names_by_crc[material["crc32"]].intersection(entry["names"]) and material["size"] == entry["source_size"] and
                    ("source_crc32" not in entry or material["crc32"] in entry["source_crc32"])):
                image = entry["image"]
                raw = (MAPPING.parent / image).read_bytes()
                if image not in generated:
                    generated[image] = dict(crc32=crc32(raw), bytes=len(raw))
                if material["ai"] not in (None, image):
                    raise ValueError("Conflicting AI mapping")
                material["ai"] = image
    coverage = {}
    for member, family in FAMILIES.items():
        all_slots = [mid for archive in archives for mid in resources[archive["members"][str(member)]]["material_slots"]]
        coverage[family] = dict(slots=len(all_slots), ai_slots=sum(materials[mid]["ai"] is not None for mid in all_slots))
    acceptance_bytes = ACCEPTANCE.read_bytes()
    apply_acceptance(rows, json.loads(acceptance_bytes))
    acceptance = dict(path=ACCEPTANCE.relative_to(PROJECT).as_posix(), crc32=crc32(acceptance_bytes))
    return dict(version=1, command="python3 tools/inventory-scenery.py --data-root DIR",
                scope="Canonical1.1a FREE members2/3/5/8/9/10/11, all10 tracks; metadata only; source labels are not visual QA.",
                mapping=dict(path=MAPPING.relative_to(PROJECT).as_posix(), crc32=crc32(mapping_bytes)),
                acceptance=acceptance,
                conventions=dict(
                    source="archives[track].members[member] resolves resources; mesh source_crc32 hashes the complete original mesh record.",
                    type="CRC32 of exact ordered signed16.16 vertex records (XYZ/UV/normals), faces/flags/color and resolved material identities; labels/global material slot numbers excluded; collisions rejected.",
                    material="CRC32 ID hashes name/BMP CRC32/size/bits/bytes; BMP CRC32 includes the original palette. material_slots preserve original indices, including unused slots.",
                    nodes="Resource node rows: [source parent, id_flags, mesh variants, transform ID, preceding root index]. Mesh nodes: [source node, variant slot];65535 is no mesh.",
                    transforms="Shared exact signed16.16 row-vector4x4 matrices; translation indices12..14. Local axes need not be world axes.",
                    static="Root bit0x20000000 registers every variant using the preceding root transform directly, ignoring child transforms; source nodes preserve sector/ID data.",
                    objects="Dynamic OBJECTS roots also render through register_model(objects, ActorKind::scenery). TRIS04_1 has230 static and5 dynamic bindings; Snetterton supplies all5 dynamic instances.",
                    flags="dynamic_nodes: [source node, materialized parent, id_flags, variants, transform ID], mirroring decode_props; Brands unresolved parents overwrite the previous owned actor. Raw bindings are not controller registrations.",
                    uv="Original signed16.16 UVs retained. Runtime U>1 subtracts1 once, V adds1; static scenery clamps both to0.02..0.99. Existing material indices and black-key alpha stay intact.",
                    shape="Face normals cluster at abs(dot)>=0.9995; plane residual<=1% max extent (minimum2 fixed units). crossed-planes requires two nearly perpendicular groups, abs(dot)<0.05; multiple-plane-surfaces does not prove a solid volume.",
                    routing="Plan/source-family routes are evidence; name-candidate needs visual confirmation. Vehicles/cranes/static aircraft route exclusively to T0. Building candidates remain open even when advertising has AI coverage.",
                    status="source-inventoried records metadata only. scenery-acceptance.json applies authored status only to exact package+source type IDs; AI mapping alone never completes geometry.",
                    ai="Global texture-replacements.json name/exact-size/optional-source-CRC matching, shared by BMP CRC across inventoried slots; generated stores authored output metadata once."),
                coverage=coverage, archives=archives, generated=generated, materials=materials, transforms=transforms,
                resources=resources, types=types, meshes=rows)


def encode(document):
    """One row per table entry: readable diffs without verbose numeric arrays."""
    lines = ["{"]
    for index, (key, value) in enumerate(document.items()):
        suffix = "," if index < len(document) - 1 else ""
        if key in ("materials", "transforms", "types"):
            lines.append(f"  {json.dumps(key)}: {{")
            for i, (row_key, row) in enumerate(value.items()):
                lines.append(f"    {json.dumps(row_key)}: {json.dumps(row, separators=(',', ':'))}" + ("," if i < len(value) - 1 else ""))
            lines.append("  }" + suffix)
        elif key == "resources":
            lines.append('  "resources": {')
            for ri, (rid, resource) in enumerate(value.items()):
                lines.append(f'    "{rid}": {{')
                for fi, (field, field_value) in enumerate(resource.items()):
                    comma = "," if fi < len(resource) - 1 else ""
                    if field in ("nodes", "dynamic_nodes"):
                        lines.append(f'      "{field}": [')
                        lines.extend("        " + json.dumps(row, separators=(",", ":")) + ("," if ni < len(field_value) - 1 else "")
                                     for ni, row in enumerate(field_value))
                        lines.append("      ]" + comma)
                    else:
                        lines.append(f'      "{field}": ' + json.dumps(field_value, separators=(",", ":")) + comma)
                lines.append("    }" + ("," if ri < len(value) - 1 else ""))
            lines.append("  }" + suffix)
        elif key == "meshes":
            lines.append('  "meshes": [')
            lines.extend("    " + json.dumps(row, separators=(",", ":")) + ("," if i < len(value) - 1 else "") for i, row in enumerate(value))
            lines.append("  ]" + suffix)
        else:
            encoded = json.dumps(value, indent=2).splitlines()
            lines.append("  " + json.dumps(key) + ": " + encoded[0])
            lines.extend("  " + line for line in encoded[1:])
            lines[-1] += suffix
    return ("\n".join(lines) + "\n}\n").encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path, required=True, help='Unpacked Git game-data root')
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    document = inventory(args.data_root)
    raw = encode(document)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    print(json.dumps(dict(bytes=len(raw), crc32=crc32(raw), meshes=len(document["meshes"]), types=len(document["types"]), output=str(args.output))))


if __name__ == "__main__":
    main()
