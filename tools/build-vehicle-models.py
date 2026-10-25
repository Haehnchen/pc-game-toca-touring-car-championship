#!/usr/bin/env python3
"""Build registered Mega vehicle GLBs from the unchanged canonical 1.1a archive."""

import argparse
import importlib
import json
import os
from pathlib import Path
import tempfile

from vehicle_manifest import DEFAULT_MANIFEST, load_definition, load_manifest
from vehicle_model import PROJECT, crc32, decode, pack, read_source, visible_triangles
import vehicle_glb


def build(entry, definition, meshopt_encoder=None, data_root=None):
    source = read_source(entry["source"], data_root)
    ghost_hidden_starts = {}
    ghost_positions = {}
    geometry = definition["geometry"]
    if geometry == "source":
        header, nodes, meshes, textures = decode(source)
        model = pack(header, nodes, meshes, textures)
        if model != source:
            raise ValueError("Reference mode must preserve the complete canonical CM3D payload")
        count = visible_triangles(nodes, meshes)
        stats = {"source_model_crc32": crc32(source), "original_visible_triangles": count,
                 "visible_triangles": count, "reference": True}
    else:
        # load_definition restricts geometry to the canonical vehicle modules.
        generator = importlib.import_module(f"{geometry}_geometry")
        texture_directory = PROJECT / entry["texture_directory"] if entry["texture_directory"] else None
        model, stats = generator.generate(source, definition, texture_directory)
        ghost_hidden_starts.update(stats.pop("ghost_hidden_starts", {}))
        ghost_positions.update(stats.pop("ghost_positions", {}))
    if geometry in ("renault", "honda", "audi", "vectra", "volvo", "ford", "nissan", "peugeot"):
        import vehicle_wheel_stance
        import vehicle_wheel_wells
        model, stance = vehicle_wheel_stance.apply(model, definition)
        stats.update(stance)
        model, wells = vehicle_wheel_wells.apply(model, definition)
        for field in ("body_detail_triangles", "visible_body_detail_triangles", "visible_body_triangles"):
            if field in stats:
                stats[field] += wells["wheel_well_triangles"]
        if "body_detail_meshes" in stats:
            stats["body_detail_meshes"] += 1
        stats.update(wells)
        ghost_hidden_starts[wells["wheel_well_mesh"]] = wells["wheel_well_first_face"]
        ghost_positions.update(stats.pop("ghost_positions"))
    data, packing = vehicle_glb.encode(*decode(model), meshopt_encoder=meshopt_encoder, ghost_hidden_starts=ghost_hidden_starts, ghost_positions=ghost_positions)
    return data, {**stats, **packing, "uncompressed_bytes": len(model), "bytes": len(data), "output_crc32": crc32(data)}


def write(output, data):
    output = output.resolve()
    if not output.is_relative_to(PROJECT / "build") or output.suffix != ".glb":
        raise ValueError("Vehicle GLB output must remain below ignored project build/")
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != data:
        staged = None
        try:
            with tempfile.NamedTemporaryFile(prefix=".vehicle-", dir=output.parent, delete=False) as temporary:
                temporary.write(data)
                staged = Path(temporary.name)
            os.replace(staged, output)
        finally:
            if staged is not None and staged.exists():
                staged.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--key", required=True, help="Registered vehicle key")
    parser.add_argument("--reference", action="store_true", help="Allow a source-only reference entry")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--meshopt-encoder", type=Path)
    parser.add_argument("--data-root", type=Path, help="Unpacked canonical 1.1a data (defaults to the local reference)")
    args = parser.parse_args()
    try:
        document = load_manifest(args.manifest)
        entry = next((entry for entry in document["vehicles"] if entry["key"] == args.key), None)
        if entry is None:
            raise ValueError(f"Unknown registered vehicle: {args.key}")
        if (entry["mode"] == "reference") != args.reference:
            raise ValueError("Source-only entries require --reference; Mega entries use their vehicle definition")
        definition = load_definition(entry)
        output = args.output or PROJECT / "build/model-overrides" / Path(entry["output"]).relative_to("assets/models")
        output = output.resolve()
        if not output.is_relative_to(PROJECT / "build") or output.suffix != ".glb":
            raise ValueError("--output must be a GLB below ignored project build/")
        data, stats = build(entry, definition, args.meshopt_encoder, args.data_root)
        write(output, data)
    except (ValueError, KeyError, OSError) as error:
        parser.error(str(error))
    print(json.dumps({"key": entry["key"], "render_id": entry["render_id"], "physical_id": entry["physical_id"],
                      "source_crc32": entry["source"]["archive_crc32"], "output": str(output), **stats}, sort_keys=True))


if __name__ == "__main__":
    main()
