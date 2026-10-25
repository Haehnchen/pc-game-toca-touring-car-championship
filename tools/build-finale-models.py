#!/usr/bin/env python3
"""Build the two canonical finale vehicle overrides and their runtime bundle."""

import argparse
import json
import os
from pathlib import Path
import tempfile

import vehicle_glb
from vehicle_model import PROJECT, crc32, decode, read_source
from vehicle_manifest import relative_path


MANIFEST = PROJECT / "assets/models/mega/finale.json"
SOURCES = {"pace-car": (0, "7076ff0e"), "transporter": (1, "94b4e25b")}


def load_manifest(path=MANIFEST):
    document = json.loads(Path(path).read_text())
    if document.get("version") != 1 or len(document.get("models", [])) != 2:
        raise ValueError("Finale manifest must register the two original convoy vehicles")
    if {entry.get("role") for entry in document["models"]} != set(SOURCES):
        raise ValueError("Finale model roles must be pace-car and transporter")
    for entry in document["models"]:
        role = entry["role"]
        source = entry["source"]
        if (source.get("archive"), source.get("archive_crc32"), source.get("member"), source.get("member_crc32")) != (
                "data/champfin.bfp", "28f9b97c", *SOURCES[role]):
            raise ValueError("Finale sources must match unchanged canonical CHAMPFIN members0/1")
        if entry.get("output") != f"finale/{role}.glb":
            raise ValueError("Finale output must match its original vehicle role")
        names = set()
        for texture in entry["textures"]:
            if texture["name"] in names:
                raise ValueError("Finale material names must remain unique within each source")
            names.add(texture["name"])
            relative_path(texture["image"], "assets/models/finale/textures")
    return document


def source_textures(section, count):
    import struct
    offset = 0
    result = []
    for _ in range(count):
        name, size = struct.unpack_from("<12sI", section, offset)
        bmp = section[offset + 16:offset + 16 + size]
        if len(bmp) != size or bmp[:2] != b"BM":
            raise ValueError("Invalid canonical finale texture section")
        result.append((name.split(b"\0", 1)[0].decode("ascii"), bmp))
        offset += 16 + size
    if section[offset:] not in (b"", b"\0"):
        raise ValueError("Unaccounted canonical finale texture bytes")
    return result


def build(document, encoder, data_root=None):
    import struct
    import mega_finale

    results = []
    for entry in document["models"]:
        original = read_source(entry["source"], data_root)
        source_header, source_nodes, _, source_section = decode(original)
        generated, geometry = mega_finale.generate(original, entry["role"])
        header, nodes, meshes, section = decode(generated)
        if nodes != source_nodes or section != source_section:
            raise ValueError("Finale generation must retain original nodes, pivots and texture section")
        originals = source_textures(source_section, struct.unpack_from("<H", source_header, 16)[0])
        bindings = {row["name"]: row for row in entry["textures"]}
        if set(bindings) != {name for name, _ in originals}:
            raise ValueError("Finale artwork must cover every original material slot")
        encoded = {}
        for name, bmp in originals:
            binding = bindings[name]
            if crc32(bmp) != binding["source_bmp_crc32"]:
                raise ValueError("Finale artwork source identity differs from the canonical bitmap")
            path = relative_path(binding["image"], "assets/models/finale/textures")
            image = path.read_bytes()
            if crc32(image) != binding["crc32"]:
                raise ValueError(f"Finale artwork CRC32 mismatch: {path}")
            previous = encoded.setdefault(crc32(bmp), image)
            if previous != image:
                raise ValueError("Identical original finale bitmaps require identical replacements")
        data, packing = vehicle_glb.encode(header, nodes, meshes, section, meshopt_encoder=encoder,
                                           texture_encoder=lambda bmp: encoded[crc32(bmp)], authored_webp_quality=70)
        packing = {key: value for key, value in packing.items() if not key.endswith('webp_quality')}
        packing["authored_webp_quality"] = 70  # Authored bytes pass through without re-encoding.
        results.append((entry["output"], data, dict(role=entry["role"], output=entry["output"],
                                                   source_crc32=entry["source"]["member_crc32"],
                                                   generated_cm3d_crc32=crc32(generated), geometry=geometry,
                                                   **packing, bytes=len(data), output_crc32=crc32(data))))
    return results


def directory(path):
    path = Path(path).resolve()
    if not path.is_relative_to(PROJECT / "build"):
        raise ValueError("Derived finale models must stay under ignored build/")
    path.mkdir(parents=True, exist_ok=True)
    return path


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise ValueError("Managed finale output must be an ordinary file")
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".finale-", delete=False) as temporary:
        temporary.write(data)
        staged = Path(temporary.name)
    try:
        os.replace(staged, path)
    finally:
        staged.unlink(missing_ok=True)


def sync(document, generated, bundle, enabled=True):
    generated, bundle = directory(generated), directory(bundle)
    for entry in document["models"]:
        relative = entry["output"]
        payload = (generated / relative).read_bytes() if enabled else None
        for root in (generated, bundle):
            path = root / relative
            if path.is_symlink() or (path.exists() and not path.is_file()):
                raise ValueError("Managed finale output must be an ordinary file")
            if not enabled:
                path.unlink(missing_ok=True)
            elif root == bundle:
                write(path, payload)
    if not enabled:
        # Re-enabling must regenerate on Makefile generators as well as Ninja.
        (generated / "finale-build.json").unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--meshopt-encoder", type=Path)
    parser.add_argument("--data-root", type=Path, help="Unpacked canonical 1.1a data (defaults to the local reference)")
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--bundle-only", action="store_true")
    parser.add_argument("--disabled", action="store_true")
    args = parser.parse_args()
    document = load_manifest()
    output = directory(args.output)
    if not args.bundle_only and not args.disabled:
        if args.meshopt_encoder is None:
            parser.error("--meshopt-encoder is required to generate finale models")
        results = build(document, args.meshopt_encoder, args.data_root)
        for relative, data, _ in results:
            write(output / relative, data)
        report = output / "finale-build.json"
        write(report, (json.dumps(dict(version=1, models=[row for _, _, row in results]), indent=2) + "\n").encode())
        report.touch()
    if args.disabled and args.bundle is None:
        parser.error("--disabled requires the runtime bundle directory")
    if args.bundle is not None:
        sync(document, output, args.bundle, enabled=not args.disabled)


if __name__ == "__main__":
    main()
