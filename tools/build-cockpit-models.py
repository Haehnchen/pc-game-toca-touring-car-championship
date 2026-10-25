#!/usr/bin/env python3
"""Build shared cockpit parts and per-car hands/dashboards from canonical1.1a."""

import argparse
import json
import os
from pathlib import Path
import tempfile

from vehicle_model import PROJECT, crc32, decode, read_source
import vehicle_glb
from vehicle_manifest import relative_path


MANIFEST = PROJECT / "assets/models/mega/cockpits.json"
CAR_KEYS = {0: "honda", 1: "audi", 2: "vectra", 3: "volvo", 4: "ford", 5: "nissan", 6: "peugeot", 7: "renault", 9: "flexmobile"}


def load_manifest(path=MANIFEST):
    document = json.loads(Path(path).read_text())
    if document["version"] != 1:
        raise ValueError("Unknown cockpit manifest version")
    if [(entry["role"], entry["member"], entry["output"]) for entry in document["models"][:2]] != [
            ("wiper", 3, "cockpit/shared/wiper.glb"), ("wheel", 4, "cockpit/shared/wheel.glb")]:
        raise ValueError("Shared cockpit manifest must bind original roles3/4")
    if [entry["car_id"] for entry in document["consumers"]] != [0, 1, 2, 3, 4, 5, 6, 7, 9]:
        raise ValueError("Shared cockpit manifest must cover all nine original interiors")
    if any(consumer["key"] != CAR_KEYS[consumer["car_id"]] for consumer in document["consumers"]):
        raise ValueError("Cockpit archive and vehicle key disagree")
    specific = document["models"][2:]
    if any(entry.get("source_car_id") not in CAR_KEYS for entry in specific):
        raise ValueError("Vehicle-specific cockpit roles require a registered original interior")
    if any(entry["role"] not in ("left-hand", "right-hand", "dashboard") for entry in specific):
        raise ValueError("Unknown vehicle-specific cockpit role")
    hands = [entry for entry in specific if entry["role"] != "dashboard"]
    expected = {(car, role, member, f"cockpit/{key}/{role}.glb")
                for car, key in CAR_KEYS.items() for member, role in ((0, "left-hand"), (1, "right-hand"))}
    actual = {(entry["source_car_id"], entry["role"], entry["member"], entry["output"]) for entry in hands}
    if len(hands) != 18 or actual != expected:
        raise ValueError("Cockpit hand manifest must bind both original hands for all nine cars")
    for entry in hands:
        key = CAR_KEYS[entry["source_car_id"]]
        if entry["texture_directory"] != f"assets/models/cockpits/hands/{key}/textures":
            raise ValueError("Cockpit hand artwork must use its registered vehicle package")
    dashboards = [entry for entry in specific if entry["role"] == "dashboard"]
    if len({entry["source_car_id"] for entry in dashboards}) != len(dashboards):
        raise ValueError("Each cockpit may register only one dashboard")
    for entry in dashboards:
        key = CAR_KEYS[entry["source_car_id"]]
        if (entry["member"], entry["output"], entry["definition"], entry["texture_directory"]) != (
                2, f"cockpit/{key}/dashboard.glb", f"assets/models/cockpits/{key}/definition.json", f"assets/models/cockpits/{key}/textures"):
            raise ValueError("Dashboard must bind original role2 in its vehicle package")
        relative_path(entry["definition"], f"assets/models/cockpits/{key}")
        relative_path(entry["texture_directory"], f"assets/models/cockpits/{key}")
    return document


def build(document, encoder=None, data_root=None):
    import cockpit_geometry
    import cockpit_hand_geometry
    import cockpit_dashboard_geometry

    results = []
    for entry in document["models"]:
        specific = "source_car_id" in entry
        consumers = [consumer for consumer in document["consumers"] if not specific or consumer["car_id"] == entry["source_car_id"]]
        sources = [read_source({**consumer, "member": entry["member"], "member_name": entry["member_name"], "member_crc32": entry["member_crc32"]}, data_root)
                   for consumer in consumers]
        if any(source != sources[0] for source in sources[1:]):
            raise ValueError("Shared cockpit role differs between original cars")
        directory = PROJECT / entry.get("texture_directory", document["texture_directory"])
        if entry["role"] == "dashboard":
            model, geometry = cockpit_dashboard_geometry.generate(sources[0], car_id=entry["source_car_id"], texture_directory=directory)
            data, packing = vehicle_glb.encode(*decode(model), meshopt_encoder=encoder, authored_webp_quality=70)
            packing = {key: value for key, value in packing.items() if not key.endswith("webp_quality")}
            packing["authored_webp_quality"] = 70
        else:
            generator = cockpit_hand_geometry if specific else cockpit_geometry
            fit = {"car_id": entry["source_car_id"]} if specific else {}
            model, geometry = generator.generate(sources[0], entry["role"], directory, **fit)
            data, packing = vehicle_glb.encode(*decode(model), meshopt_encoder=encoder)
        geometry["generated_cm3d_crc32"] = geometry.pop("output_crc32")
        results.append((entry["output"], data, {"role": entry["role"], "output": entry["output"], "source_car_id": entry.get("source_car_id"),
                                              "source_crc32": entry["member_crc32"],
                                              **geometry, **packing, "bytes": len(data), "output_crc32": crc32(data), "crc32": crc32(data)}))
    return results


def output_directory(path):
    path = Path(path).resolve()
    if not path.is_relative_to(PROJECT / "build"):
        raise ValueError("Derived cockpit assets must stay below ignored project build/")
    path.mkdir(parents=True, exist_ok=True)
    return path


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".cockpit-", delete=False) as temporary:
        temporary.write(data)
        staged = Path(temporary.name)
    try:
        os.replace(staged, path)
    finally:
        staged.unlink(missing_ok=True)


def sync(document, generated, bundle, enabled=True):
    generated, bundle = output_directory(generated), output_directory(bundle)
    entries = document["models"]
    payloads = {entry["output"]: (generated / entry["output"]).read_bytes() for entry in entries} if enabled else {}
    managed = {entry["output"] for entry in entries} | {f"cockpit/{key}/dashboard.glb" for key in CAR_KEYS.values()}
    for directory in (generated, bundle):
        for relative in managed:
            path = directory / relative
            if path.is_symlink() or (path.exists() and not path.is_file()):
                raise ValueError("Managed cockpit output must be an ordinary file")
    for directory in (generated, bundle):
        for relative in managed:
            path = directory / relative
            if relative not in payloads:
                path.unlink(missing_ok=True)
            elif directory == bundle:
                write(path, payloads[relative])
    if not enabled:
        (generated / "cockpit-manifest.json").unlink(missing_ok=True)


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
    directory = output_directory(args.output)
    if not args.bundle_only and not args.disabled:
        results = build(document, args.meshopt_encoder, args.data_root)
        for name, data, _ in results:
            write(directory / name, data)
        report = {"version": 1, "manifest_crc32": crc32(MANIFEST.read_bytes()), "models": [stats for _, _, stats in results]}
        write(directory / "cockpit-manifest.json", (json.dumps(report, indent=2) + "\n").encode())
        (directory / "cockpit-manifest.json").touch()
        print(json.dumps(report))
    if args.bundle:
        sync(document, directory, args.bundle, not args.disabled)


if __name__ == "__main__":
    main()
