#!/usr/bin/env python3
"""Reproduce M02 TREES proximity evidence; distance ranks never grant visual acceptance."""

import argparse
from collections import Counter
import importlib.util
import json
import math
from pathlib import Path
import struct

import checksum
from git_game_data import archive_bytes
import mega_woodland
import vehicle_model

PROJECT = Path(__file__).resolve().parents[1]
OUTPUT = PROJECT / "build/asset-inventories/woodland-audit.json"
PREFIXES = ("donl", "silv", "thru", "bran", "oult", "dons", "crof", "knoc", "snet")
spec = importlib.util.spec_from_file_location("scenery_inventory", PROJECT / "tools/inventory-scenery.py")
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)
add, sub, scale, dot, cross = (getattr(vehicle_model, name) for name in ("add", "sub", "scale", "dot", "cross"))


def clamp(value):
    return max(0., min(1., value))


def closest_segments(a, b, c, d):
    """Exact finite 3D segment pair, including point/parallel segments."""
    u, v, w = sub(b, a), sub(d, c), sub(a, c)
    aa, bb, cc, dd, ee = dot(u, u), dot(u, v), dot(v, v), dot(u, w), dot(v, w)
    if aa < 1e-20:
        s, t = 0., clamp(ee / cc) if cc else 0.
    elif cc < 1e-20:
        s, t = clamp(-dd / aa), 0.
    else:
        determinant = aa * cc - bb * bb
        s = clamp((bb * ee - cc * dd) / determinant) if determinant > 1e-20 else 0.
        t = (bb * s + ee) / cc
        if t < 0:
            t, s = 0., clamp(-dd / aa)
        elif t > 1:
            t, s = 1., clamp((bb - dd) / aa)
    return add(a, scale(u, s)), add(c, scale(v, t))


def inside(point, triangle, normal):
    return all(dot(cross(sub(b, a), sub(point, a)), normal) >= -1e-8
               for a, b in zip(triangle, triangle[1:] + triangle[:1]))


def closest_triangle_segment(triangle, start, end):
    """Exact filled triangle/finite segment distance with both witness points."""
    a, b, c = triangle
    normal = cross(sub(b, a), sub(c, a))
    squared = dot(normal, normal)
    candidates = [closest_segments(p, q, start, end) for p, q in zip(triangle, triangle[1:] + triangle[:1])]
    if squared > 1e-20:
        for point in (start, end):
            projection = sub(point, scale(normal, dot(sub(point, a), normal) / squared))
            if inside(projection, triangle, normal):
                candidates.append((projection, point))
        delta = sub(end, start)
        denominator = dot(normal, delta)
        if abs(denominator) > 1e-15:
            t = dot(normal, sub(a, start)) / denominator
            if 0 <= t <= 1:
                point = add(start, scale(delta, t))
                if inside(point, triangle, normal):
                    candidates.append((point, point))
    p, q = min(candidates, key=lambda pair: dot(sub(*pair), sub(*pair)))
    return math.dist(p, q), p, q


def transform(point, matrix):
    """Original static_model.cpp per-product arithmetic shift and 32-bit wrap."""
    result = []
    for row in range(3):
        value = matrix[12 + row] + sum((matrix[column * 4 + row] * point[column]) >> 16 for column in range(3))
        value = (value + 0x80000000) % 0x100000000 - 0x80000000
        result.append(value / 65536)
    return tuple(result)


def source_archive(prefix, kind, data_root):
    relative = "data/" + prefix + kind + ".bfp"
    raw = archive_bytes(data_root, relative)
    return dict(path=relative, crc32=vehicle_model.crc32(raw)), inventory.archive_members(raw)


def route_points(raw):
    count = struct.unpack_from("<I", raw)[0]
    if not 3 <= count <= 256 or 4 + count * 64 > len(raw):
        raise ValueError("Expected complete original POINTS route")
    points, widths = [], []
    for index in range(count):
        record = struct.unpack_from("<12i", raw, 4 + 64 * index)
        first, target, second = (tuple(x / 65536 for x in record[begin:begin + 3]) for begin in (0, 3, 9))
        points.append(target)
        # Route target need not be the road midpoint. This radius encloses
        # both original lateral endpoints instead of assuming half the width.
        widths.append(max(math.dist(target, first), math.dist(target, second)))
    return points, widths


def cameras(raw):
    count = struct.unpack_from("<H", raw, 12)[0]
    offset, result = 32, []
    for _ in range(count):
        size = struct.unpack_from("<I", raw, offset)[0]
        if size != 80:
            raise ValueError("Expected transform-only camera nodes")
        result.append(tuple(value / 65536 for value in struct.unpack_from("<3i", raw, offset + 56)))
        offset += size
    if offset != len(raw):
        raise ValueError("Unaccounted original camera bytes")
    return result


def rounded(point):
    return [round(value, 4) for value in point]


def evidence(distance, point, route_point, segment, primitive, radius):
    return dict(distance_m=round(distance, 4), conservative_corridor_lower_bound_m=round(max(0., distance - radius), 4),
                primitive=primitive, route_segment=segment, surface_point=rounded(point), route_point=rounded(route_point))


def measure(vertices, faces, route, widths):
    triangles = [tuple(vertices[i] for i in struct.unpack_from("<3H", face, 2)) for face in faces]
    # Weld by exact baked coordinates, not source vertex index: exporters
    # duplicate vertices at UV seams and along triangulation diagonals.
    edges = Counter(tuple(sorted((a, b))) for tri in triangles for a, b in zip(tri, tri[1:] + tri[:1]))
    ends = [edge for edge, count in edges.items() if count == 1 and abs(edge[1][1] - edge[0][1]) > 1.
            and math.hypot(edge[1][0] - edge[0][0], edge[1][2] - edge[0][2]) < abs(edge[1][1] - edge[0][1]) * .2]
    best_face, best_end, best_corridor = None, None, float("inf")
    for ri, start in enumerate(route):
        end = route[(ri + 1) % len(route)]
        radius = max(widths[ri], widths[(ri + 1) % len(route)])
        for fi, triangle in enumerate(triangles):
            distance, p, q = closest_triangle_segment(triangle, start, end)
            best_corridor = min(best_corridor, max(0., distance - radius))
            if best_face is None or distance < best_face[0]:
                best_face = distance, p, q, ri, fi, radius
        for ei, edge in enumerate(ends):
            p, q = closest_segments(*edge, start, end)
            distance = math.dist(p, q)
            if best_end is None or distance < best_end[0]:
                best_end = distance, p, q, ri, ei, radius
    end_evidence = evidence(*best_end) if best_end else None
    if end_evidence:
        end_evidence["edge_endpoints"] = [rounded(point) for point in ends[best_end[4]]]
    return dict(face=evidence(*best_face), exposed_end=end_evidence,
                corridor_lower_bound_m=round(best_corridor, 4), geometric_boundary_edges=sum(n == 1 for n in edges.values()),
                vertical_end_edges=len(ends))


def spatial_groups(rows, radius=80.):
    """Greedy seed-radius groups: no transitive chain joining a whole circuit."""
    remaining = sorted(rows, key=lambda row: (row["proximity"]["face"]["distance_m"], row["mesh"]))
    groups = []
    while remaining:
        seed = remaining[0]
        point = seed["proximity"]["face"]["route_point"]
        members = [row for row in remaining if math.dist(row["proximity"]["face"]["route_point"], point) <= radius]
        indices = {row["mesh"] for row in members}
        groups.append(dict(seed_mesh=seed["mesh"], meshes=sorted(indices), route_point=point,
                           source_triangles=sum(row["source_triangles"] for row in members),
                           minimum_distance_m=seed["proximity"]["face"]["distance_m"]))
        remaining = [row for row in remaining if row["mesh"] not in indices]
    return groups


def review_groups(prefix, rows, groups):
    """Enumerate every near case; group membership is a proposed view, not coverage."""
    by_mesh = {row["mesh"]: row for row in rows}
    result = []
    for group in groups:
        near = [by_mesh[index] for index in group["meshes"] if by_mesh[index]["risk"] == "near-review"]
        if not near:
            continue
        identifier = f"{prefix}-near-{group['seed_mesh']}"
        seed = min(near, key=lambda row: (row["proximity"]["face"]["distance_m"], row["mesh"]))
        ends = [row for row in near if row["exposed_end_review"]]
        end_seed = min(ends, key=lambda row: row["proximity"]["exposed_end"]["distance_m"]) if ends else None
        for row in near:
            row["review_group"] = identifier
        result.append(dict(id=identifier, status="native-review-required", mesh=seed["mesh"], node=seed["node"],
                           required_meshes=[row["mesh"] for row in near], required_nodes=[row["node"] for row in near],
                           types=[row["type"] for row in near], nearby_meshes=group["meshes"],
                           route_witness=seed["proximity"]["face"], camera_hint=seed["camera_hint"],
                           end_review_meshes=[row["mesh"] for row in ends], end_view_seed_mesh=end_seed["mesh"] if end_seed else None,
                           end_view_witness=end_seed["proximity"]["exposed_end"] if end_seed else None))
    return result


def audit(data_root):
    mapping_path = PROJECT / "assets/models/mega/texture-replacements.json"
    mapping = json.loads(mapping_path.read_text())
    source_inventory = inventory.inventory(data_root)
    known = {(row["track"], row["mesh"]): row for row in source_inventory["meshes"] if row["member"] == 2}
    tracks, rows, types, materials = [], [], {}, {}
    source_records = {}
    for track, prefix in enumerate(PREFIXES):
        archive, free = source_archive(prefix, "free", data_root)
        keep_archive, keep = source_archive(prefix, "keep", data_root)
        name, raw = free[2]
        if name != "TREES.C3D" or keep[1][0] != "POINTS.DAT" or free[4][0] != "CAMERAS.C3D":
            raise ValueError("Unexpected original member contract")
        nodes, meshes, slots = inventory.parse_model(raw)
        route, widths = route_points(keep[1][1])
        camera_positions = cameras(free[4][1])
        header, node_bytes, decoded, texture_bytes = vehicle_model.decode(raw)
        names = [slot["name"] for slot in slots]
        connected, _ = mega_woodland.enhance_connected(decoded, vehicle_model.Mesh, names)
        output_meshes = [mega_woodland.enhance(mesh, vehicle_model.Mesh, names)[0] for mesh in connected]
        source_records[track] = raw
        material_ids = []
        for slot, material in enumerate(slots):
            mid = vehicle_model.crc32(json.dumps(material, sort_keys=True, separators=(",", ":")).encode())
            material_ids.append(mid)
            matches = [entry for entry in mapping["replacements"] if material["name"] in entry["names"] and material["size"] == entry["source_size"]
                       and ("source_crc32" not in entry or material["crc32"] in entry["source_crc32"])]
            images = sorted({entry["image"] for entry in matches})
            if len(images) != 1:
                raise ValueError(f"Expected unambiguous accepted foliage mapping: {prefix}/{material['name']}")
            path = mapping_path.parent / images[0]
            materials[mid] = dict(material, image=path.relative_to(PROJECT).as_posix(), image_crc32=checksum.crc32_file(path), image_bytes=path.stat().st_size)
        track_rows = []
        for ni, node in enumerate(nodes):
            if node["parent"] != -1 or not node["flags"] & 0x20000000:
                raise ValueError("Audit requires confirmed static root-only TREES contract")
            for variant, mi in enumerate(node["variants"]):
                if mi == 65535:
                    continue
                mesh = meshes[mi]
                tid = vehicle_model.crc32(inventory.identity(mesh, material_ids))
                reference = known[(track, mi)]
                if tid != reference["type"] or vehicle_model.crc32(mesh["raw"]) != reference["source_crc32"]:
                    raise ValueError("Scenery type ledger differs from canonical TREES")
                types[tid] = dict(source_inventory["types"][tid])
                vertices = [transform(v[:3], node["matrix"]) for v in mesh["vertices"]]
                proximity = measure(vertices, mesh["faces"], route, widths)
                rebuilt = output_meshes[mi]
                changed = rebuilt.encode() != decoded[mi].encode()
                if changed and reference["status"] != "accepted":
                    raise ValueError("Authored woodland geometry has no exact accepted type")
                distance = proximity["corridor_lower_bound_m"]
                end = proximity["exposed_end"]
                risk = "accepted-geometry-pilot" if changed else "near-review" if distance <= 25 else "middle-distance-review" if distance <= 75 else "distant-candidate"
                nearest_camera = min(range(len(camera_positions)), key=lambda ci: math.dist(camera_positions[ci], proximity["face"]["route_point"]))
                row = dict(track=track, mesh=mi, name=mesh["name"], type=tid, node=ni, variant=variant,
                           node_flags=f"{node['flags']:08x}", transform_crc32=vehicle_model.crc32(struct.pack("<16i", *node["matrix"])),
                           origin=rounded(tuple(v / 65536 for v in node["matrix"][12:15])), source_crc32=reference["source_crc32"],
                           source_triangles=len(mesh["faces"]), material_slots=sorted({struct.unpack_from("<h", face, 8)[0] for face in mesh["faces"]}),
                           bounds=[[round(min(v[a] for v in vertices), 4), round(max(v[a] for v in vertices), 4)] for a in range(3)],
                           geometry="accepted-pilot" if changed else "original", output_mesh_crc32=vehicle_model.crc32(rebuilt.encode()),
                           output_triangles=len(rebuilt.faces), risk=risk,
                           exposed_end_review=bool(not changed and end and end["conservative_corridor_lower_bound_m"] <= 25),
                           proximity=proximity, camera_hint=nearest_camera, camera_origin=rounded(camera_positions[nearest_camera]))
                track_rows.append(row)
                rows.append(row)
        groups = spatial_groups(track_rows)
        near_groups = review_groups(prefix, track_rows, groups)
        unmodified = [row for row in track_rows if row["geometry"] == "original"]
        nearest = sorted(unmodified, key=lambda row: (row["proximity"]["face"]["distance_m"], row["mesh"]))[:3]
        end_candidates = sorted((row for row in unmodified if row["proximity"]["exposed_end"]),
                                key=lambda row: (row["proximity"]["exposed_end"]["distance_m"], row["mesh"]))[:2]
        dense = max((group for group in groups if group["minimum_distance_m"] <= 75),
                    key=lambda group: (group["source_triangles"], -group["minimum_distance_m"]))
        tracks.append(dict(track=track, prefix=prefix, source=archive, member=2, member_crc32=vehicle_model.crc32(raw),
                           nodes=len(nodes), nodes_crc32=vehicle_model.crc32(node_bytes), mesh_count=len(meshes), instances=len(track_rows), material_slots=material_ids,
                           authored_member_crc32=vehicle_model.crc32(vehicle_model.pack(header, node_bytes, output_meshes, texture_bytes)),
                           authored_triangles=sum(len(mesh.faces) for mesh in output_meshes),
                           route=dict(source=keep_archive, member=1, member_crc32=vehicle_model.crc32(keep[1][1]), points=len(route),
                                      maximum_lateral_radius_m=round(max(widths), 4)), camera_member_crc32=vehicle_model.crc32(free[4][1]),
                           spatial_groups=groups, near_review_groups=near_groups, nearest_original_meshes=[row["mesh"] for row in nearest],
                           nearest_original_ends=[row["mesh"] for row in end_candidates], dense_group=dense))
        print(f"{prefix}: {len(track_rows)} instances; nearest original " + ", ".join(f"node{r['node']}/mesh{r['mesh']} {r['proximity']['face']['distance_m']:.1f}m" for r in nearest), flush=True)
    donl, dons = tracks[0], tracks[5]
    reuse = dict(tracks=[0, 5], source_member_byteidentical=source_records[0] == source_records[5],
                 source_member_crc32=donl["member_crc32"], materials_and_images_identical=donl["material_slots"] == dons["material_slots"],
                 nodes_crc32=donl["nodes_crc32"], authored_member_crc32=donl["authored_member_crc32"],
                 authored_geometry_identical=donl["authored_member_crc32"] == dons["authored_member_crc32"],
                 placement_differences=[], geometry_differences=[], route_identical=donl["route"]["member_crc32"] == dons["route"]["member_crc32"],
                 explanation="The complete TREES member, including nodes/transforms, is byteidentical. Different POINTS routes change exposure, not placements.")
    for a, b in zip([row for row in rows if row["track"] == 0], [row for row in rows if row["track"] == 5]):
        if (a["node"], a["variant"], a["transform_crc32"]) != (b["node"], b["variant"], b["transform_crc32"]):
            reuse["placement_differences"].append(a["mesh"])
        if (a["source_crc32"], a["output_mesh_crc32"]) != (b["source_crc32"], b["output_mesh_crc32"]):
            reuse["geometry_differences"].append(a["mesh"])
    return dict(version=1, command="python3 tools/audit-woodland.py --data-root DIR", scope="Canonical1.1a FREE member2 TREES, nine real track IDs including both Donington layouts; metadata only.",
                method=dict(surface="Exact filled world-space source triangle versus every closed KEEP POINTS target segment; native fixed transform per shifted product.",
                            end="Geometrically welded single-use boundary edges with vertical span>1m and horizontal span<20% vertical span; diagonal/top/ground borders excluded.",
                            corridor="Lower bound: segment distance minus maximum target-to-first/second endpoint radius at both segment ends; conservative tube, not collision or occlusion.",
                            thresholds_m=dict(near=25, middle=75), interpretation="Risk labels select native review; no distance or image mapping grants acceptance. No terrain/object occlusion is inferred.",
                            groups="Greedy80m radius around route witness of nearest remaining mesh; nontransitive, no whole-circuit chain.",
                            cameras="Nearest authored CAMERAS origin to route witness; look-at target must be the selected woodland, visibility requires native inspection."),
                dependencies=dict(woodland_geometry_crc32=checksum.crc32_file(PROJECT / "tools/mega_woodland.py"),
                                  material_mapping_crc32=vehicle_model.crc32(json.dumps(materials, sort_keys=True, separators=(",", ":")).encode())),
                summary=dict(tracks=len(tracks), mesh_rows=len(rows), types=len(types), instances=len(rows),
                             source_triangles=sum(row["source_triangles"] for row in rows), accepted_geometry_pilots=sum(row["geometry"] == "accepted-pilot" for row in rows),
                             near_review_groups=sum(len(track["near_review_groups"]) for track in tracks),
                             near_exposed_end_candidates=sum(row["exposed_end_review"] for row in rows),
                             risk_counts=dict(sorted(Counter(row["risk"] for row in rows).items()))),
                donington_reuse=reuse, materials=materials, types=types, tracks=tracks, instances=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root', type=Path, required=True, help='Unpacked Git game-data root')
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    document = audit(args.data_root)
    raw = (json.dumps(document, indent=2, ensure_ascii=False) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    print(json.dumps(document["summary"], sort_keys=True))


if __name__ == "__main__":
    main()
