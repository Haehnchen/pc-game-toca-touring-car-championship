#!/usr/bin/env python3
"""Build compact Mega track props/scenery from the unchanged canonical1.1a data.

Derived models/textures remain local under ignored build/. Only newly
generated materials are distributed as authored WebP70 source assets.
"""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

import mega_geometry
import mega_gargoyle
import mega_woodland
import vehicle_glb
import vehicle_model
import vehicle_textures
from checksum import crc32_file
from git_game_data import configured_data_root


PROJECT = Path(__file__).resolve().parent.parent
PREFIXES = ('donl', 'silv', 'thru', 'bran', 'oult', 'dons', 'crof', 'knoc', 'snet', 'land')
FAMILIES = {2: 'trees', 3: 'fence', 5: 'objects', 8: 'cones', 9: 'posts', 10: 'signs', 11: 'flags'}
# These authored prop shapes are already simple solids with correct UVs.
SOURCE_PROP_SHAPES = {
    (0, 8): '9408ddb9', (0, 9): '1a39b43f', (0, 10): '83a984a4',
    (1, 8): '923cec08', (1, 9): '7c127e1a', (1, 10): '9f9d3951',
    (2, 8): 'e6be0b26', (2, 9): 'cae2f0dc', (2, 10): 'dfe0e48d',
    (3, 8): '87440301', (3, 9): '46178c94', (3, 10): 'de435d9a',
    (4, 10): '067cf3ed',
    (5, 8): '866a6c58', (5, 9): '1a39b43f', (5, 10): 'f425de85',
    (6, 8): 'b15f9cf4', (6, 9): '55f0a25d', (6, 10): '2f088304',
}
HORIZON_FAMILIES = {0: 'moving', 1: 'rotorcraft'}
BACKGROUND_MEMBERS = range(2, 9)
FOLIAGE_MATERIALS = {
    'LHRTS4': ('MegaTreeLeaf', (64, 64)),
    'LHRTS7': ('MegaTreeLf7', (64, 64)),
    'LHRTWS3': ('MegaGroveLf', (128, 64)),
    'LHRTS2': ('MegaTreeLf2', (32, 64)),
    'LHRTS1': ('MegaTreeLf1', (64, 64)),
    'LHRTWS4': ('MegaGrovLf4', (128, 64)),
    'LMRTS4': ('MegaGrovLm4', (64, 32)),
    'LMRTS2': ('MegaGrovLm2', (64, 32)),
}
MAX_TEXTURE_EDGE = 512
MAX_TEXTURE_BYTES = 256 * 1024
MAX_MODEL_BYTES = 8 * 1024 * 1024


def active_families(track, records):
    for index, family in FAMILIES.items():
        if track == 9 and index in (2, 9, 10):
            continue
        # Silverstone has a genuine empty FREE11. Lavaland has its own
        # nonempty flag hierarchy; preserve it instead of inventing actors.
        if family == 'flags' and struct.unpack_from('<H', records[index], 14)[0] == 0:
            continue
        yield index, family


def flag_nodes(header, nodes):
    """Mirror 494c6d's retained actor when a FREE11 parent is unresolved."""
    records, offset = [], 0
    while offset < len(nodes):
        size, parent = struct.unpack_from('<Ih', nodes, offset)
        node = bytearray(nodes[offset:offset + size])
        if parent >= len(records):
            if not records:
                raise ValueError('Unresolved flag parent has no preceding actor')
            previous = records[-1]
            struct.pack_into('<h', node, 4, struct.unpack_from('<h', previous, 4)[0])
            old_flags = struct.unpack_from('<I', previous, 72)[0]
            flags = struct.unpack_from('<I', node, 72)[0]
            struct.pack_into('<I', node, 72, (old_flags & 0xff000000) | (flags & 0xffffff))
            records[-1] = node
        else:
            records.append(node)
        offset += size
    result = b''.join(records)
    output = bytearray(header)
    delta = len(result) - len(nodes)
    struct.pack_into('<H', output, 12, len(records))
    for field in (24, 28):
        struct.pack_into('<I', output, field, struct.unpack_from('<I', header, field)[0] + delta)
    return bytes(output), result


def crc32(data):
    return f'{zlib.crc32(data):08x}'


def prop_meshes(track, index, raw, meshes, texture_names):
    """Choose source-bound prop shapes before the ordinary material-atlas generators."""
    checksum = crc32(raw)
    if track == 9 and index == 8 and checksum == 'ca267324':
        # Lavaland's gargoyle is a distinct obstacle, not a tapered road cone.
        # Shallow head relief retains source boundaries, UVs and both material
        # slots. SceneView owns the shared visual-origin correction.
        if texture_names != ['GARG01', 'GARG02'] or len(meshes) != 1 or meshes[0].header[:12].split(b'\0', 1)[0] != b'GARCON_1':
            raise ValueError('Expected canonical Lavaland gargoyle mesh and materials')
        return [mega_gargoyle.enhance(meshes[0], vehicle_model.Mesh, texture_names)]
    if SOURCE_PROP_SHAPES.get((track, index)) == checksum:
        return meshes
    family = FAMILIES[index]
    make = {'cones': mega_geometry.cone, 'posts': mega_geometry.post, 'signs': mega_geometry.sign}[family]
    options = dict(texture_names=texture_names) if family == 'signs' else {}
    return [make(mesh, vehicle_model.Mesh, len(texture_names), **options) for mesh in meshes]


def members(data):
    count, size, _ = struct.unpack_from('<III', data)
    if size != count * 24 or 12 + size > len(data):
        raise ValueError('Invalid canonical BFP directory')
    result = []
    for index in range(count):
        offset, length = struct.unpack_from('<II', data, 12 + index * 24 + 16)
        start = 12 + size + offset
        if start + length > len(data):
            raise ValueError('Truncated canonical BFP member')
        result.append(data[start:start + length])
    return result


def textures(model):
    """Read exactly the texture records, permitting canonical trailing NULL."""
    if model[:4] != b'CM3D':
        return []
    offset = struct.unpack_from('<I', model, 28)[0]
    count = struct.unpack_from('<H', model, 16)[0]
    result = []
    for _ in range(count):
        name = model[offset:offset + 12]
        size = struct.unpack_from('<I', model, offset + 12)[0]
        data = model[offset + 16:offset + 16 + size]
        if len(data) != size or data[:2] != b'BM':
            raise ValueError('Invalid canonical scene texture')
        result.append((name, data))
        offset += 16 + size
    if count and model[offset:] not in (b'', b'\0'):
        raise ValueError('Unaccounted scene texture bytes')
    return result


def texture_size(bmp):
    width, height = struct.unpack_from('<ii', bmp, 18)
    if width <= 0 or height == 0 or max(width, abs(height)) > 2048:
        raise ValueError('Invalid source texture dimensions')
    return width, abs(height)


def horizon_members(track):
    """Families actually enabled by43cf60/43e2a0 in at least one weather."""
    return tuple(index for index, tracks in ((0, (0, 1, 2, 5, 6, 8)), (1, (1, 3, 4, 7, 8))) if track in tracks)


def horizon_palette(donington):
    donor = next(bmp for name, bmp in textures(donington) if name.split(b'\0', 1)[0] == b'PSP1')
    palette = donor[54:1078]
    if len(palette) != 1024 or crc32(palette) != '4613c335' or crc32(palette[:1020]) != '057d15c3':
        raise ValueError('Expected complete original Donington PSP1 horizon palette')
    return palette


def normalize_horizon_bmp(bmp, palette):
    """Match toca::decode_bmp's exact original missing-palette-entry repair."""
    if len(bmp) < 54 or struct.unpack_from('<H', bmp, 28)[0] != 8 or struct.unpack_from('<I', bmp, 46)[0] != 255:
        return bmp
    if len(palette) != 1024 or struct.unpack_from('<I', bmp, 14)[0] != 40 or struct.unpack_from('<I', bmp, 30)[0] != 0 or struct.unpack_from('<I', bmp, 10)[0] != 1074 or bmp[54:1074] != palette[:1020]:
        raise ValueError('Partial horizon palette must match the complete original PSP1 bank')
    restored = bytearray(bmp[:54] + palette + bmp[1074:])
    struct.pack_into('<I', restored, 2, len(restored))
    struct.pack_into('<I', restored, 10, 1078)
    struct.pack_into('<I', restored, 46, 256)
    return bytes(restored)


def psp_material(header, meshes, texture_records, skin_bmp):
    """Keep every original material slot; append the PSP's opaque 3D UV skin."""
    if not any(mesh.header[:12].split(b'\0', 1)[0] == b'PSP1_1' for mesh in meshes):
        return header, list(texture_records), False
    if any(name.split(b'\0', 1)[0] == b'MegaPSPSkin' for name, _ in texture_records):
        raise ValueError('Canonical PSP material prefix unexpectedly contains MegaPSPSkin')
    records = list(texture_records) + [(b'MegaPSPSkin\0', skin_bmp)]
    updated = bytearray(header)
    struct.pack_into('<H', updated, 16, len(records))
    return bytes(updated), records, True


def horizon_nodes(header, nodes, family):
    """Mirror decode_horizon_actors' IDs and helicopter identity parent exactly."""
    header, nodes = bytearray(header), bytearray(nodes)
    position = 0
    while position < len(nodes):
        size, parent = struct.unpack_from('<Ih', nodes, position)
        if size < 80 or position + size > len(nodes):
            raise ValueError('Invalid canonical horizon node record')
        flags = struct.unpack_from('<I', nodes, position + 72)[0]
        if family == 'rotorcraft':
            struct.pack_into('<h', nodes, position + 4, parent + 1)
            selected = position == 0
        elif family == 'moving':
            selected = parent == -1 and flags & 0xffffff not in (998, 999)
        else:
            raise ValueError('Unknown original horizon actor family')
        if selected:
            struct.pack_into('<I', nodes, position + 72, flags & 0xff000000 | 0x2f4)
        position += size
    if family == 'rotorcraft':
        parent = bytearray(80)
        struct.pack_into('<Ih', parent, 0, 80, -1)
        for index in (0, 5, 10, 15):
            struct.pack_into('<i', parent, 8 + index * 4, 65536)
        nodes = parent + nodes
        struct.pack_into('<H', header, 12, struct.unpack_from('<H', header, 12)[0] + 1)
    return bytes(header), bytes(nodes)


def native_texture(bmp):
    """Embed unsupported canonical pixels at their original size, without scaling."""
    texture_size(bmp)
    converter = shutil.which('magick') or shutil.which('convert')
    if not converter:
        raise ValueError('ImageMagick with WebP support is required for Mega assets')
    command = [converter, 'bmp:-', '-alpha', 'on', '-transparent', 'black',
               '-strip', '-define', 'webp:lossless=false', '-define', 'webp:alpha-quality=100',
               '-define', 'webp:method=6', '-quality', '70', 'webp:-']
    data = subprocess.run(command, input=bmp, check=True, capture_output=True).stdout
    if data[:4] != b'RIFF' or data[8:12] != b'WEBP' or len(data) > MAX_TEXTURE_BYTES:
        raise ValueError('Mega texture exceeds WebP/size budget')
    return data


def webp_size(data):
    """Use common static-container validation with the track material budget."""
    return vehicle_textures.webp_dimensions(
        data, max_dimension=MAX_TEXTURE_EDGE, max_bytes=MAX_TEXTURE_BYTES, lossy_only=True)


def replacements(path, sources):
    """Resolve explicit AI-authored images by canonical name, size and optional CRC."""
    document = json.loads(path.read_text())
    if not isinstance(document, dict) or document.get('version') != 1 or document.get('webp_quality') not in (70, 80) or not isinstance(document.get('replacements'), list):
        raise ValueError('Texture mapping requires version1, recorded WebP quality70/80 and explicit replacements')
    encoded, report = {}, {}
    for entry in document['replacements']:
        if not isinstance(entry, dict):
            raise ValueError('Each generated replacement requires an explicit mapping object')
        names, size, image = entry.get('names'), entry.get('source_size'), entry.get('image')
        restricted = entry.get('source_crc32')
        if not isinstance(names, list) or not names or any(not isinstance(name, str) or not name or '\0' in name for name in names):
            raise ValueError('Replacement requires explicit canonical texture names')
        if not isinstance(size, list) or len(size) != 2 or any(type(value) is not int or value <= 0 for value in size):
            raise ValueError('Replacement requires exact positive canonical dimensions')
        if restricted is not None and (not isinstance(restricted, list) or not restricted or any(not isinstance(key, str) or re.fullmatch('[0-9a-f]{8}', key) is None for key in restricted)):
            raise ValueError('Replacement source CRC32 values require eight lowercase hexadecimal digits')
        if not isinstance(image, str) or not image:
            raise ValueError('Replacement requires an authored WebP asset')
        asset = (path.parent / image).resolve()
        if not asset.is_relative_to(path.parent.resolve()) or asset.suffix != '.webp' or not asset.is_file():
            raise ValueError(f'Missing or invalid generated texture asset: {image}')
        data = asset.read_bytes()
        width, height = webp_size(data)
        if width * size[1] != height * size[0]:
            raise ValueError(f'Generated texture aspect differs from canonical slots: {image}')
        matched_names, matched_crc = set(), set()
        for key, source in sources.items():
            if source['names'].intersection(names) and source['size'] == tuple(size) and (restricted is None or key in restricted):
                if key in encoded and encoded[key] != data:
                    raise ValueError(f'Conflicting generated replacements for canonical CRC32 {key}')
                encoded[key] = data
                matched_names.update(source['names'].intersection(names))
                matched_crc.add(key)
                row = report.setdefault(key, dict(source_crc32=key, names=[], source_size=size, kind='generated', image=image,
                                                  output_crc32=crc32(data), bytes=len(data), size=[width, height]))
                row['names'] = sorted(set(row['names']).union(source['names'].intersection(names)))
        if matched_names != set(names) or (restricted is not None and matched_crc != set(restricted)):
            raise ValueError(f'Generated texture mapping does not match canonical names/dimensions/CRC32: {image}')
    return encoded, [report[key] for key in sorted(report)]


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(prefix='.mega-', dir=path.parent, delete=False) as temporary:
        temporary.write(data)
        staged = Path(temporary.name)
    os.replace(staged, path)


def reconcile_texture_cache(directory, encoded):
    """Remove obsolete generated cache entries, including the retired resize pass."""
    directory.mkdir(parents=True, exist_ok=True)
    for path in directory.glob('*.webp'):
        if re.fullmatch('[0-9a-f]{8}', path.stem) and path.stem not in encoded:
            path.unlink()
    for key, data in encoded.items():
        write(directory / (key + '.webp'), data)


def owned_scene_path(path):
    parts = path.parts
    return (len(parts) == 1 and re.fullmatch(r'[0-9a-f]{8}\.webp', parts[0]) is not None
            or len(parts) == 2 and re.fullmatch('track-[0-9]', parts[0]) is not None
            and parts[1] in {family + '.glb' for family in (*FAMILIES.values(), *HORIZON_FAMILIES.values())})


def scene_inventory(report):
    """Recover the complete producer-owned inventory from existing provenance."""
    inventory = {}
    rows = [(Path(f'track-{row["track"]}') / (row['family'] + '.glb'), row)
            for row in report['models']]
    rows.extend((Path(row['source_crc32'] + '.webp'), row) for row in report['textures'])
    for relative, row in rows:
        if (not owned_scene_path(relative) or relative in inventory
                or type(row['bytes']) is not int or row['bytes'] <= 0
                or not isinstance(row['output_crc32'], str)
                or re.fullmatch('[0-9a-f]{8}', row['output_crc32']) is None):
            raise ValueError(f'Invalid generated Mega scene identity: {relative}')
        inventory[relative] = row['bytes'], row['output_crc32']
    if not inventory:
        raise ValueError('Cannot bundle an empty generated Mega scene')
    return inventory


def verified_scene_files(source, report):
    files = {}
    for relative, (size, checksum) in scene_inventory(report).items():
        path = source / relative
        if (not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(source.resolve())
                or path.stat().st_size != size or crc32_file(path) != checksum):
            raise ValueError(f'Missing or corrupt generated Mega scene file: {relative}')
        files[relative] = path
    return files


def sync_scene(source, destination):
    """Verify every live source before publishing or retiring bundle artifacts."""
    report = json.loads((source.parent / 'scene-manifest.json').read_text())
    files = verified_scene_files(source, report)
    destination.mkdir(parents=True, exist_ok=True)
    for relative, path in files.items():
        write(destination / relative, path.read_bytes())
    for path in destination.rglob('*'):
        relative = path.relative_to(destination)
        if path.is_file() and owned_scene_path(relative) and relative not in files:
            path.unlink()


def update(source, output, atlas, encoder, mapping, inputs, aircraft_skin=None):
    """One build producer owns input freshness and all manifest-listed outputs."""
    paths = {Path(path).resolve() for path in inputs}
    paths.update(Path(path).resolve() for path in (atlas, encoder, mapping))
    if aircraft_skin is not None:
        paths.add(aircraft_skin.resolve())
    identities = {os.path.relpath(path, PROJECT): {'bytes': path.stat().st_size, 'crc32': crc32_file(path)}
                  for path in sorted(paths)}
    manifest = output / 'scene-manifest.json'
    try:
        report = json.loads(manifest.read_text())
        if isinstance(report, dict) and report.get('build_inputs') == identities:
            verified_scene_files(output / 'scene', report)
            return report
    except (OSError, ValueError, KeyError, TypeError):
        pass
    report = build(source, output, atlas, encoder, mapping, aircraft_skin)
    # Publish freshness only after the complete generation has succeeded.
    verified_scene_files(output / 'scene', report)
    report['build_inputs'] = identities
    write(manifest, (json.dumps(report, indent=2) + '\n').encode())
    return report


def reconcile_models(directory, models):
    expected = {Path(f'track-{row["track"]}') / (row['family'] + '.glb') for row in models}
    families = {*FAMILIES.values(), *HORIZON_FAMILIES.values()}
    for path in directory.glob('track-*/*.glb'):
        if re.fullmatch('track-[0-9]', path.parent.name) and path.stem in families and path.relative_to(directory) not in expected:
            path.unlink()


def vegetation_materials(path, sources):
    """Resolve foliage sprites against their exact canonical type and palette."""
    converter = shutil.which('magick') or shutil.which('convert')
    leaves, report = {}, []
    for entry in json.loads(path.read_text())['outputs']:
        mapping = entry.get('mapping', {})
        material = mapping.get('append_material')
        source_name = next((name for name, (target, _) in FOLIAGE_MATERIALS.items() if target == material), None)
        if source_name is None:
            continue
        asset = (path.parent / entry['image']).resolve()
        if not asset.is_relative_to(path.parent.resolve()) or asset.suffix != '.webp':
            raise ValueError('Vegetation sprite must be an authored WebP within the asset directory')
        data = asset.read_bytes()
        size = webp_size(data)
        if crc32(data) != entry['crc32']:
            raise ValueError('Vegetation sprite differs from its reviewed provenance CRC32')
        bmp = subprocess.run([converter, str(asset), '-alpha', 'off', '-depth', '8', '-type', 'TrueColor', 'BMP3:-'],
                             check=True, capture_output=True).stdout
        for key in mapping['source_crc32']:
            if key in leaves or key not in sources or source_name not in sources[key]['names'] or sources[key]['size'] != FOLIAGE_MATERIALS[source_name][1]:
                raise ValueError('Vegetation sprite must match its unique canonical tree type and palette')
            leaves[key] = (bmp, data)
        report.append(dict(image=entry['image'], material=material, source_name=source_name, source_crc32=mapping['source_crc32'], crc32=crc32(data), bytes=len(data), size=list(size)))
    expected = {key for key, source in sources.items()
                if any(name in source['names'] and source['size'] == size for name, (_, size) in FOLIAGE_MATERIALS.items())}
    if set(leaves) != expected:
        raise ValueError('Vegetation sprites must cover every supported canonical tree palette')
    return leaves, report


def vegetation_slots(texture_records, foliage, first_slot):
    """Keep the existing leaf slot first, then append each distinct tree type."""
    result = {}
    for source_name, (material_name, _) in FOLIAGE_MATERIALS.items():
        for source_slot, (name, bmp) in enumerate(texture_records):
            if name.split(b'\0', 1)[0].decode('ascii') == source_name:
                result[source_slot] = (first_slot + len(result), material_name.encode('ascii').ljust(12, b'\0'), foliage[crc32(bmp)][0])
    return result


def brands_bridge_material(track, index, raw, header, meshes, texture_records, artwork_bmp):
    """Bind the bridge chart locally; keep the shared helicopter scalar native."""
    if (track, index, crc32(raw)) != (3, 5, '90ddbd12'):
        return header, meshes, texture_records, False
    if (len(meshes) <= 34 or len(texture_records) <= 127 or
            meshes[33].header[:12].split(b'\0', 1)[0] != b'BH20BRDG_1' or
            crc32(meshes[33].encode()) != 'd96b514b' or
            texture_records[127][0].split(b'\0', 1)[0] != b'D15SHL7' or
            crc32(texture_records[127][1]) != '1c07ca8e'):
        raise ValueError('Expected canonical Brands bridge and shared D15 material prefix')
    bridge = meshes[33]
    source_faces = [i for i, face in enumerate(bridge.faces) if struct.unpack_from('<h', face, 8)[0] == 127]
    if len(source_faces) != 12:
        raise ValueError('Expected the twelve original Brands bridge lattice faces')
    slot = len(texture_records)
    replacement = vehicle_model.Mesh(bridge.header, list(bridge.vertices), list(bridge.faces))
    for face_index in source_faces:
        face = replacement.faces[face_index]
        replacement.faces[face_index] = face[:8] + struct.pack('<h', slot) + face[10:]
    updated_meshes = list(meshes)
    updated_meshes[33] = replacement
    updated_records = list(texture_records) + [(b'MegaBridgeX\0', artwork_bmp)]
    updated_header = bytearray(header)
    struct.pack_into('<H', updated_header, 16, len(updated_records))
    return bytes(updated_header), updated_meshes, updated_records, True


def build(source, output, atlas, encoder, mapping, aircraft_skin=None, vegetation_bark=None):
    sources, unique, identities, embedded = [], {}, {}, set()
    horizon_sources = []

    def read_archive(track, prefix, kind):
        path = source / 'data' / (prefix + kind + '.bfp')
        data = path.read_bytes()
        checksum = crc32(data)
        return track, path.name, checksum, members(data)

    def remember(name, bmp):
        key = crc32(bmp)
        if key in unique and unique[key] != bmp:
            raise ValueError('Source texture CRC32 collision')
        unique[key] = bmp
        identity = identities.setdefault(key, dict(names=set(), size=texture_size(bmp)))
        identity['names'].add(name.split(b'\0', 1)[0].decode('ascii'))
        return key

    for track, prefix in enumerate(PREFIXES):
        archive = read_archive(track, prefix, 'free')
        sources.append(archive)
        records = archive[3]
        for index in (1, 2, 3, 5, 8, 9, 10, 11):
            if track == 9 and index in (2, 9, 10):
                continue
            for name, bmp in textures(records[index]):
                key = remember(name, bmp)
                if index in FAMILIES:
                    embedded.add(key)
    horizon_archives = [read_archive(track, prefix, 'hor') for track, prefix in enumerate(PREFIXES)]
    palette = horizon_palette(horizon_archives[0][3][0])
    for track, name, checksum, records in horizon_archives:
        # Static skies own their embedded palettes, including intentionally
        # short banks. They use exact BMP keys and remain opaque. Only authored
        # overrides enter the cache: never pass these through native_texture.
        for index in BACKGROUND_MEMBERS:
            for texture_name, bmp in textures(records[index]):
                remember(texture_name, bmp)
        for index in horizon_members(track):
            selected = [(n, normalize_horizon_bmp(bmp, palette)) for n, bmp in textures(records[index])]
            for texture_name, bmp in selected:
                embedded.add(remember(texture_name, bmp))
            horizon_sources.append((track, name, checksum, index, HORIZON_FAMILIES[index], records[index], selected))
    # 434c70's seven screen-blended lens sprites retain opaque RGB black.
    # The sun's own image is loaded but suppressed by the original dispatcher.
    lens_archive = read_archive(0, 'image640', '')
    for index in range(7):
        remember(f'LENS{index + 1}320'.encode(), lens_archive[3][40 + index])
    generated, texture_report = replacements(mapping, identities)
    native_keys = sorted(embedded - generated.keys())
    with ThreadPoolExecutor(max_workers=6) as pool:
        encoded = dict(zip(native_keys, pool.map(native_texture, (unique[key] for key in native_keys))))
    encoded.update(generated)
    scene = output / 'scene'
    converter = shutil.which('magick') or shutil.which('convert')
    atlas_webp = atlas.read_bytes()
    webp_size(atlas_webp)
    atlas_bmp = subprocess.run([converter, str(atlas), '-alpha', 'off', '-depth', '8', '-type', 'TrueColor', 'BMP3:-'],
                               check=True, capture_output=True).stdout
    encoded[crc32(atlas_bmp)] = atlas_webp
    aircraft_skin = aircraft_skin or PROJECT / 'assets/models/mega/aircraft-skin.webp'
    skin_webp = aircraft_skin.read_bytes()
    if webp_size(skin_webp) != (512, 256):
        raise ValueError('PSP 3D skin requires its authored 512x256 WebP UV atlas')
    skin_bmp = subprocess.run([converter, str(aircraft_skin), '-alpha', 'off', '-depth', '8', '-type', 'TrueColor', 'BMP3:-'],
                              check=True, capture_output=True).stdout
    encoded[crc32(skin_bmp)] = skin_webp
    vegetation_bark = vegetation_bark or PROJECT / 'assets/models/mega/textures/vegetation-bark.webp'
    bark_webp = vegetation_bark.read_bytes()
    webp_size(bark_webp)
    bark_bmp = subprocess.run([converter, str(vegetation_bark), '-alpha', 'off', '-depth', '8', '-type', 'TrueColor', 'BMP3:-'],
                              check=True, capture_output=True).stdout
    encoded[crc32(bark_bmp)] = bark_webp
    foliage, foliage_report = vegetation_materials(PROJECT / 'assets/models/mega/vegetation-generation.json', identities)
    for bmp, data in foliage.values():
        encoded[crc32(bmp)] = data
    api = vehicle_model
    models = []
    for track, name, checksum, records in sources:
        for index, family in active_families(track, records):
            raw = records[index]
            header, nodes, meshes, _ = api.decode(raw)
            texture_records = textures(raw)
            original_faces = sum(len(mesh.faces) for mesh in meshes)
            original_vertices = sum(len(mesh.vertices) for mesh in meshes)
            original_textures = len(texture_records)
            generated_slots = sum(crc32(bmp) in generated for _, bmp in texture_records)
            texture_index = len(texture_records)
            leaf_slots = vegetation_slots(texture_records, foliage, texture_index + 2)
            details = Counter()
            if family == 'flags':
                # Artwork-only replacement. Keep all three cloth variants,
                # poles and the original loader's actor hierarchy. Brands'
                # self-parent references retain/overwrite the previous actor.
                header, nodes = flag_nodes(header, nodes)
            elif family in ('cones', 'posts', 'signs'):
                names = [name.split(b'\0', 1)[0].decode('ascii') for name, _ in texture_records]
                meshes = prop_meshes(track, index, raw, meshes, names)
            else:
                names = [name.split(b'\0', 1)[0].decode('ascii') for name, _ in texture_records]
                enhanced, remaining = [], 48000
                if family == 'trees':
                    meshes, connected = mega_woodland.enhance_connected(meshes, api.Mesh, names, triangle_budget=remaining)
                    details.update(connected)
                    remaining -= connected['added_triangles']
                for mesh in meshes:
                    source_slots = {struct.unpack_from('<h', face, 8)[0] for face in mesh.faces}
                    leaf_slot = leaf_slots.get(next(iter(source_slots))) if len(source_slots) == 1 else None
                    detailed, counts = mega_geometry.scenery(mesh, api.Mesh, names, texture_index, family,
                                                            triangle_budget=min(2048, remaining), vegetation_index=texture_index + 1,
                                                            foliage_index=leaf_slot[0] if leaf_slot else None)
                    enhanced.append(detailed)
                    # Enhancers also expose per-face diagnostic lists; this
                    # manifest field aggregates only numeric counters.
                    details.update({key: value for key, value in counts.items()
                                    if isinstance(value, (int, float))})
                    remaining -= counts.get('allocation_triangles', counts['added_triangles'])
                meshes = enhanced
            extra_materials = [(b'MegaSurface\0', atlas_bmp), (b'MegaTreeBark', bark_bmp)]
            extra_materials.extend((name, bmp) for _, name, bmp in leaf_slots.values())
            last_slot = max((struct.unpack_from('<h', face, 8)[0] for mesh in meshes for face in mesh.faces), default=-1)
            appended_count = max(0, last_slot - texture_index + 1)
            if appended_count > len(extra_materials):
                raise ValueError('Scenery geometry references an unbound generated material slot')
            if appended_count:
                texture_records.extend(extra_materials[:appended_count])
                header = bytearray(header)
                struct.pack_into('<H', header, 16, len(texture_records))
            local_materials = 0
            if track == 3 and index == 5:
                bridge_artwork = PROJECT / 'assets/models/mega/textures/brands-building-d15shl7.webp'
                bridge_webp = bridge_artwork.read_bytes()
                if webp_size(bridge_webp) != (512, 256):
                    raise ValueError('Brands bridge artwork requires its source2:1 chart')
                bridge_bmp = subprocess.run([converter, str(bridge_artwork), '-alpha', 'off', '-depth', '8',
                                             '-type', 'TrueColor', 'BMP3:-'], check=True, capture_output=True).stdout
                header, meshes, texture_records, added_bridge = brands_bridge_material(
                    track, index, raw, header, meshes, texture_records, bridge_bmp)
                if not added_bridge:
                    raise ValueError('Expected canonical Brands bridge material binding')
                encoded[crc32(bridge_bmp)] = bridge_webp
                local_materials = 1
                details['bridge_local_materials'] = 1
            texture_section = b''.join(n + struct.pack('<I', len(bmp)) + bmp for n, bmp in texture_records)
            data, stats = vehicle_glb.encode(header, nodes, meshes, texture_section, meshopt_encoder=encoder,
                                            texture_encoder=lambda bmp: encoded[crc32(bmp)])
            if len(data) > MAX_MODEL_BYTES:
                raise ValueError(f'{family} GLB exceeds8MiB budget')
            path = scene / f'track-{track}' / (family + '.glb')
            write(path, data)
            models.append(dict(track=track, family=family, source_archive=name, source_archive_crc32=checksum,
                               source_member=index, source_crc32=crc32(raw), output_crc32=crc32(data), bytes=len(data),
                               original_triangles=original_faces, original_vertices=original_vertices,
                               triangles=sum(len(mesh.faces) for mesh in meshes), generated_material_slots=generated_slots + appended_count + local_materials,
                               source_native_material_slots=original_textures - generated_slots, scenery_details=dict(details), **stats))
    import mega_aircraft
    shared_horizon = {}
    for track, name, checksum, index, family, raw, texture_records in horizon_sources:
        source_crc = crc32(raw)
        key = family, source_crc
        if key not in shared_horizon:
            header, nodes, meshes, _ = api.decode(raw)
            header, nodes = horizon_nodes(header, nodes, family)
            original_faces = sum(len(mesh.faces) for mesh in meshes)
            original_vertices = sum(len(mesh.vertices) for mesh in meshes)
            original_textures = len(texture_records)
            generated_slots = sum(crc32(bmp) in generated for _, bmp in texture_records)
            header, texture_records, added_skin = psp_material(header, meshes, texture_records, skin_bmp)
            names = [n.split(b'\0', 1)[0].decode('ascii') for n, _ in texture_records]
            enhanced, details = [], Counter()
            for mesh in meshes:
                detailed, counts = mega_aircraft.enhance(mesh, api.Mesh, names, family=family)
                enhanced.append(detailed)
                details.update(counts)
            section = b''.join(n + struct.pack('<I', len(bmp)) + bmp for n, bmp in texture_records)
            data, stats = vehicle_glb.encode(header, nodes, enhanced, section, meshopt_encoder=encoder,
                                            texture_encoder=lambda bmp: encoded[crc32(bmp)])
            if len(data) > MAX_MODEL_BYTES:
                raise ValueError(f'{family} GLB exceeds8MiB budget')
            description = dict(original_triangles=original_faces, original_vertices=original_vertices,
                               triangles=sum(len(mesh.faces) for mesh in enhanced), generated_material_slots=generated_slots + int(added_skin),
                               source_native_material_slots=original_textures - generated_slots, horizon_details=dict(details), **stats)
            shared_horizon[key] = data, description
        data, description = shared_horizon[key]
        write(scene / f'track-{track}' / (family + '.glb'), data)
        models.append(dict(track=track, family=family, source_archive=name, source_archive_crc32=checksum,
                           source_member=index, source_crc32=source_crc, output_crc32=crc32(data), bytes=len(data), **description))
    reconcile_models(scene, models)
    reconcile_texture_cache(scene, generated)
    report = dict(generated_webp_quality=70, texture_policy='explicit-ai-authored-replacements', max_texture_edge=MAX_TEXTURE_EDGE,
                  material_atlas_crc32=crc32(atlas_webp), textures=texture_report, models=models,
                  generated_texture_count=len(generated), source_native_texture_count=len(unique) - len(generated),
                  source_native_embedded_texture_count=len(native_keys),
                  generated_image_count=len({row['image'] for row in texture_report}),
                  horizon_palette_crc32=crc32(palette), horizon_model_count=len(horizon_sources),
                  unique_horizon_model_count=len(shared_horizon),
                  aircraft_skin=dict(material='MegaPSPSkin', crc32=crc32(skin_webp), bytes=len(skin_webp), size=[512, 256]),
                  vegetation_bark=dict(material='MegaTreeBark', crc32=crc32(bark_webp), bytes=len(bark_webp), size=list(webp_size(bark_webp))),
                  vegetation_leaves=foliage_report,
                  texture_bytes=sum(row['bytes'] for row in texture_report), model_bytes=sum(row['bytes'] for row in models))
    write(output / 'scene-manifest.json', (json.dumps(report, indent=2) + '\n').encode())
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, help='Unpacked Git game-data root; defaults to TOCA_DATA_ROOT')
    parser.add_argument('--output', type=Path, default=PROJECT / 'build/mega-assets')
    parser.add_argument('--atlas', type=Path, default=PROJECT / 'assets/models/mega/prop-materials.webp')
    parser.add_argument('--aircraft-skin', type=Path, default=PROJECT / 'assets/models/mega/aircraft-skin.webp')
    parser.add_argument('--replacements', type=Path, default=PROJECT / 'assets/models/mega/texture-replacements.json')
    parser.add_argument('--meshopt-encoder', type=Path)
    parser.add_argument('--update', action='store_true', help='verify inputs and every output before reusing generation')
    parser.add_argument('--inputs', type=Path, nargs='+', help='complete generator input closure for --update')
    parser.add_argument('--sync-scene', type=Path, nargs=2, metavar=('SOURCE', 'DESTINATION'))
    args = parser.parse_args()
    if args.sync_scene:
        source, destination = (path.resolve() for path in args.sync_scene)
        if source == destination or not all(path.is_relative_to(PROJECT / 'build') for path in (source, destination)):
            parser.error('Generated Mega bundles must use distinct ignored build/ directories')
        sync_scene(source, destination)
        return
    if args.meshopt_encoder is None:
        parser.error('--meshopt-encoder is required to build generated models')
    source = args.source if args.source is not None else configured_data_root()
    output = args.output.resolve()
    if not output.is_relative_to(PROJECT / 'build'):
        parser.error('Derived original media must remain inside ignored build/')
    if args.update:
        if not args.inputs:
            parser.error('--update requires the complete --inputs list')
        report = update(source, output, args.atlas, args.meshopt_encoder, args.replacements, args.inputs, args.aircraft_skin)
    else:
        report = build(source, output, args.atlas, args.meshopt_encoder, args.replacements, args.aircraft_skin)
    print(json.dumps({key: value for key, value in report.items() if key not in ('textures', 'models', 'build_inputs')} |
                     dict(textures=len(report['textures']), models=len(report['models']))))


if __name__ == '__main__':
    main()
