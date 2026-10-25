"""Canonical static HOR metadata and local-only image references."""

from collections import defaultdict
import json
from pathlib import Path
import struct

from checksum import crc32_file
from git_game_data import archive_bytes
from vehicle_model import crc32, decode
from vehicle_textures import bmp_pixels, texture_records

PROJECT = Path(__file__).resolve().parents[1]
OUTPUT = PROJECT / "build/asset-inventories/background-inventory.json"
GENERATION = PROJECT / "assets/models/mega/backgrounds-generation.json"
ATMOSPHERE_GENERATION = PROJECT / "assets/models/mega/atmosphere-generation.json"
REFERENCES = PROJECT / "build/mega-backgrounds/references"
TRACKS = (
    ("donl", "Donington GP", "8771ca20"),
    ("silv", "Silverstone", "5bc8cde6"),
    ("thru", "Thruxton", "46eab623"),
    ("bran", "Brands Hatch", "7f2628e3"),
    ("oult", "Oulton Park", "716aabd8"),
    ("dons", "Donington Short", "8771ca20"),
    ("crof", "Croft", "f242381f"),
    ("knoc", "Knockhill", "2715d7f9"),
    ("snet", "Snetterton", "8cad122b"),
    ("land", "Lavaland", "25e04c2f"),
)
CLOUDY = ((3,), (3, 4), (4,), (3,), (3, 4), (3, 4), (4,), (3, 4), (4,))


def members(data):
    if len(data) < 12:
        raise ValueError("Truncated BFP header")
    count, table = struct.unpack_from("<II", data)
    if table != count * 24 or 12 + table > len(data):
        raise ValueError("Invalid BFP directory")
    records = []
    for index in range(count):
        label, offset, size = struct.unpack_from("<16sII", data, 12 + index * 24)
        start = 12 + table + offset
        if start + size > len(data):
            raise ValueError("Truncated BFP member")
        records.append((label.split(b"\0", 1)[0].decode("ascii"), data[start:start + size]))
    return records


def palette(bmp):
    """Keep the exact embedded palette, including valid short banks."""
    start = 14 + struct.unpack_from("<I", bmp, 14)[0]
    end = struct.unpack_from("<I", bmp, 10)[0]
    count = struct.unpack_from("<I", bmp, 46)[0] or 256
    if start != 54 or end != start + count * 4 or not 1 <= count <= 256:
        raise ValueError("Expected the complete embedded sky palette")
    return dict(entries=count, bytes=end - start, crc32=crc32(bmp[start:end]))


def nodes(data):
    result, offset = [], 0
    while offset < len(data):
        size, parent = struct.unpack_from("<Ih", data, offset)
        count = data[offset + 76]
        if size != (80 + count * 2 + 3) & ~3 or offset + size > len(data):
            raise ValueError("Invalid CM3D node bounds")
        result.append(dict(index=len(result), parent=parent,
                           id_flags=f"{struct.unpack_from('<I', data, offset + 72)[0]:08x}",
                           mesh_variants=list(struct.unpack_from(f"<{count}H", data, offset + 80))))
        offset += size
    return result


def usage(track, member):
    if track == 9:
        return (["lavaland-all-weather"] if member == 2 else []), (None if member == 2 else "Lavaland takes priority over weather and Extras")
    if member >= 7:
        return ["CMSTARS" if member == 7 else "CMTOON"], None
    weather = {2: ["sunny"], 3: ["cloudy"], 4: ["cloudy"], 5: ["rain", "snow", "CMRAINUP", "CMCATDOG"], 6: ["storm"]}[member]
    if member in (3, 4) and member not in CLOUDY[track]:
        # Championship meeting6 selects the first Croft cloudy variant.
        if track == 6 and member == 3:
            return ["championship-meeting-6-cloudy"], None
        return [], "Not selected by the original track/weather setup"
    return weather, None


def slot_meshes(meshes, slot, node_records):
    result = []
    for index, mesh in enumerate(meshes):
        faces = [f for f in mesh.faces if struct.unpack_from("<h", f, 8)[0] == slot]
        if not faces:
            continue
        indices = sorted({i for f in faces for i in struct.unpack_from("<3H", f, 2)})
        vertices = [mesh.vertices[i] for i in indices]
        cap_faces = [f for f in faces if all(-1 < mesh.vertices[i][4] < 0 for i in struct.unpack_from("<3H", f, 2))]
        cap_indices = sorted({i for f in cap_faces for i in struct.unpack_from("<3H", f, 2)})
        result.append(dict(index=index, name=mesh.header[:12].split(b"\0", 1)[0].decode("ascii"),
                           node_indices=[n["index"] for n in node_records if index in n["mesh_variants"]],
                           vertices=len(mesh.vertices), triangles=len(faces), cap_triangles=len(cap_faces),
                           uv_source_16_16=[[round(min(v[a] for v in vertices) * 65536), round(max(v[a] for v in vertices) * 65536)] for a in (3, 4)],
                           uv_runtime=[[min(v[3] - (1 if v[3] > 1 else 0) for v in vertices), max(v[3] - (1 if v[3] > 1 else 0) for v in vertices)],
                                       [min(v[4] + 1 for v in vertices), max(v[4] + 1 for v in vertices)]],
                           cap_uv_source_16_16=[[round(min(mesh.vertices[i][a] for i in cap_indices) * 65536), round(max(mesh.vertices[i][a] for i in cap_indices) * 65536)] for a in (3, 4)] if cap_indices else [],
                           uv_boundary_vertices={edge: sum(v[axis] == value for v in vertices) for edge, axis, value in (("u0", 3, 0), ("u1", 3, 1), ("v-1", 4, -1), ("v0", 4, 0))}))
    return result


def storm_uv_deltas(meshes):
    if len(meshes) != 2:
        return []
    def triangle(mesh, face):
        vertices = [mesh.vertices[i] for i in struct.unpack_from("<3H", face, 2)]
        return {tuple(round(v[a] * 65536) for a in range(3)): [round(v[a] * 65536) for a in (3, 4)] for v in vertices}
    ordinary = {tuple(sorted(triangle(meshes[0], f))): triangle(meshes[0], f) for f in meshes[0].faces}
    result = []
    for index, face in enumerate(meshes[1].faces):
        if struct.unpack_from("<h", face, 8)[0] != 0:
            continue
        after = triangle(meshes[1], face)
        before = ordinary[tuple(sorted(after))]
        changes = [dict(position_16_16=list(position), uv_before_16_16=before[position], uv_after_16_16=uv)
                   for position, uv in after.items() if before[position] != uv]
        if changes:
            result.append(dict(mesh=1, triangle=index, material_slot=0, changes=changes))
    return result


def source_rows(data_root):
    """Read only the ten canonical HOR archives and static members2..8."""
    archives, rows = [], []
    for track, (prefix, name, expected) in enumerate(TRACKS):
        relative = f"data/{prefix}hor.bfp"
        raw = archive_bytes(data_root, relative)
        if crc32(raw) != expected:
            raise ValueError(f"Canonical HOR CRC32 mismatch: {relative}")
        records = members(raw)
        if len(records) != 9:
            raise ValueError("Canonical HOR requires nine members")
        archives.append(dict(track_id=track, track=name, path=relative, crc32=expected, bytes=len(raw)))
        for member in range(2, 9):
            member_name, data = records[member]
            header, node_data, meshes, textures = decode(data)
            binding = nodes(node_data)
            for slot, (material, _, bmp) in enumerate(texture_records(textures, struct.unpack_from("<H", header, 16)[0])):
                width, height, pixels = bmp_pixels(bmp)
                if (width, height) != (256, 256) or len(pixels) != 65536:
                    raise ValueError("Canonical skies must be256x256 indexed images")
                consumers, inactive_reason = usage(track, member)
                rows.append((dict(track_id=track, track=name, archive=relative, archive_crc32=expected,
                                  member=member, member_name=member_name, member_crc32=crc32(data), member_bytes=len(data),
                                  material_slot=slot, material=material, nodes=binding,
                                  meshes=slot_meshes(meshes, slot, binding), consumers=consumers,
                                  storm_ordinary_uv_deltas=storm_uv_deltas(meshes) if member == 6 else [],
                                  scene_consumers=(["race", "qualifying", "time-trial", "local-split", "network-race"]
                                                   + (["championship-finale"] if track == 1 and member in (3, 7, 8) else [])) if consumers else [],
                                  active=bool(consumers), inactive_reason=inactive_reason), bmp, meshes))
    return archives, rows


def inventory(data_root):
    archives, rows = source_rows(data_root)
    groups = defaultdict(list)
    for occurrence, bmp, _ in rows:
        groups[crc32(bmp)].append((occurrence, bmp))
    images = []
    generated = {i["material"]: i for i in json.loads(GENERATION.read_text())["images"]}
    generated.update({i["material"]: i for i in json.loads(ATMOSPHERE_GENERATION.read_text())["images"]})
    for checksum, records in groups.items():
        first, bmp = records[0]
        name = first["material"]
        track, member = first["track_id"], first["member"]
        package = "B01" if name == "DO02" else ("B03a" if name == "LAVABAK" else "B03b" if name == "SILLYLAN" else "B03c" if name == "CARTOON" else f"B02{'abcdefgh'[(track if track < 5 else track - 1)]}")
        item = generated.get(name, {})
        images.append(dict(material=name, bmp_crc32=checksum, bmp_bytes=len(bmp), size=[256, 256], bits=8,
                           palette=palette(bmp), opaque=True, active=any(r[0]["active"] for r in records),
                           shared=len(records) > 1, occurrences=[r[0] for r in records],
                           target=dict(package=package, path=f"assets/models/mega/textures/background-{name.lower()}.webp",
                                       size=[512, 512], quality=70, alpha="opaque", output=item.get("output")),
                           generation_status="generated" if item.get("output") else "pending",
                           provenance="assets/models/mega/backgrounds-generation.json"))
    if len(images) != 51 or len({o["member_crc32"] for o, _, _ in rows}) != 43:
        raise ValueError("Expected51 distinct BMPs in43 distinct static CM3D members")
    return dict(version=1, command="python3 tools/inventory-backgrounds.py --data-root DIR",
                scope="Static in-race sky atlases only; source media are read from verified unpacked Git inputs; optional exports stay beneath ignored build/.",
                counts=dict(images=len(images), unique_members=43, archives=len(archives), material_occurrences=len(rows)),
                conventions=dict(checksum="CRC32 IEEE/zlib, eight lowercase hexadecimal digits; archive files use tools/checksum.py.",
                                 identity="BMP CRC32 includes each exact embedded palette; static skies never use moving-HOR PSP1 repair or black-key alpha.",
                                 uv="Source signed16.16: U>1 subtracts1 once, V adds1. Sky UVs are not scenery-clamped. Side panels repeat the whole atlas; upper cap samples the small interior patch.",
                                 selection="src/scene/setup.cpp:resolve_setup; Lavaland precedes stars, stars precede toon, then Championship, rain codes and normal weather.",
                                 detail="Detail0/1 use original weather/Lavaland clear colors;2/3 use original BMP;4 uses authored WebP when its exact source is available.",
                                 inactive="TH02, BH03, SN02 have no active consumer. Donington GP DO03 and Lavaland's copied weather/Extras slots are inactive occurrences of otherwise shared active images."),
                storm=dict(source="TOCA1.1a:FUN_004315a0, FUN_00430af0, FUN_00463170, FUN_004632c0",
                           contract="05 is the ordinary opaque storm atlas;05L is its localized lightning-strike atlas. Original315a0 switches node camera masks between ordinary node and lightning node when flash-pattern alpha is nonzero;632c0 also draws the white overlay.",
                           mesh_slots="member6: node1 mesh0 has24 triangles using material0=05; node2 mesh1 has20 triangles using05 including the upper cap and4 triangles using material1=05L on two side panels.",
                           uv_exception="Lit mesh changes UVs on one ordinary05 triangle in the negativeX/negativeZ facet, except Brands Hatch. Those original per-vertex changes are recorded on each occurrence and preserved with the mesh switch.",
                           native_evidence="src/scene/view.cpp:SceneView::frame/draw_model; src/scene/weather_render.cpp:append_lightning. Runtime switching must be verified with integration acceptance."),
                other_atmosphere=dict(woodland="M02: FREE TREES; B04 owns only joint sky/woodland visual acceptance.",
                                      sun_lens_glow=atmosphere_inventory(generated, data_root)),
                archives=archives, images=images)


def atmosphere_inventory(generated, data_root):
    """B05 distinguishes loaded-but-undrawn sun data from visible lens art."""
    archives = []
    relative = "data/image640.bfp"
    raw = archive_bytes(data_root, relative)
    archives.append((dict(path=relative, crc32=crc32(raw)), members(raw)))
    images = []
    for member, name in ((17, "BGLOW"), (18, "HGLOW"), (37, "WHITE2"), (38, "SUNMASK"), (39, "SUN"), *((40 + i, f"LENS{i + 1}320") for i in range(7))):
        bmp = archives[0][1][member][1]
        width, height, _ = bmp_pixels(bmp)
        visible = member >= 40
        item = generated.get(name, {})
        images.append(dict(material=name, sources=[dict(archive=a["path"], archive_crc32=a["crc32"], member=member,
                                                        member_name=records[member][0]) for a, records in archives],
                           bmp_crc32=crc32(bmp), bmp_bytes=len(bmp), size=[width, height],
                           bits=struct.unpack_from("<H", bmp, 28)[0],
                           palette=palette(bmp) if struct.unpack_from("<H", bmp, 28)[0] == 8 else None,
                           consumer="car-brake/headlamp-glow" if member < 37 else "projected-lens-screen-overlay" if visible else "sun-loaded-but-hardware-draw-suppressed",
                           background_generation=visible, package="B05" if member >= 37 else None,
                           selection="resolved weather1; single player; TimeTrial disabled; original blend-capable device" if visible else None,
                           target=dict(path=f"assets/models/mega/textures/background-{name.lower()}.webp", size=[width * 2, height * 2], quality=70,
                                       alpha="opaque RGB; black neutral under ONE/INVSRCCOLOR screen blend", output=item.get("output")) if visible else None,
                           generation_status=("generated" if item.get("output") else "pending") if visible else "excluded-confirmed-consumer"))
    return dict(evidence="docs/visual-effects.md; decompile44f0a3,4512b0,434b10,434b30,434c70,434d70,490b90,490cb0,49491f,434da0,437640",
                images=images)


def encode(document):
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def export_references(materials, data_root):
    """Export original media only beneath the ignored project build directory."""
    from PIL import Image, ImageDraw
    REFERENCES.mkdir(parents=True, exist_ok=True)
    selected = set(materials)
    exported = set()
    for source, bmp, meshes in source_rows(data_root)[1]:
        name = source["material"]
        if name in exported or (selected and name not in selected):
            continue
        width, height, pixels = bmp_pixels(bmp)
        original = Image.new("RGB", (width, height))
        original.putdata(pixels)
        original.save(REFERENCES / f"{name.lower()}-original.png")
        view = original.resize((768, 768), Image.Resampling.NEAREST)
        draw = ImageDraw.Draw(view)
        for mesh in meshes:
            for face in mesh.faces:
                if struct.unpack_from("<h", face, 8)[0] != source["material_slot"]:
                    continue
                indices = struct.unpack_from("<3H", face, 2)
                points = [(mesh.vertices[i][3] * 767, (mesh.vertices[i][4] + 1) * 767) for i in indices]
                draw.line(points + [points[0]], fill=(255, 60, 80), width=2)
        draw.text((10, 10), "UV U=[0,1]; bitmap V=source V+1. Red=triangle edges", fill="yellow")
        view.save(REFERENCES / f"{name.lower()}-uv.png")
        exported.add(name)
    if selected - exported:
        raise ValueError(f"Unknown background material: {sorted(selected - exported)}")
    return len(exported)


def export_atmosphere_references(data_root):
    from PIL import Image
    REFERENCES.mkdir(parents=True, exist_ok=True)
    records = members(archive_bytes(data_root, "data/image640.bfp"))
    images = atmosphere_inventory({}, data_root)["images"]
    for item in images:
        bmp = records[item["sources"][0]["member"]][1]
        width, height, pixels = bmp_pixels(bmp)
        image = Image.new("RGB", (width, height))
        image.putdata(pixels)
        image.save(REFERENCES / f"sun-{item['material'].lower()}-original.png")
    return len(images)
