#!/usr/bin/env python3
"""Bundle registered vehicles and retire only manifest-managed car files."""

import argparse
import json
import os
from pathlib import Path
import re
import tempfile

from vehicle_manifest import DEFAULT_MANIFEST, load_manifest
from vehicle_model import PROJECT

LEDGER = ".vehicle-models.json"


def atomic_write(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".vehicle-", delete=False) as stream:
        stream.write(data)
        temporary = Path(stream.name)
    os.replace(temporary, path)


def managed_names(directory):
    ledger = directory / LEDGER
    if not ledger.exists():
        return set()
    value = json.loads(ledger.read_text())
    if value.get("version") != 1 or not isinstance(value.get("managed"), list):
        raise ValueError("Invalid vehicle output ledger")
    names = value["managed"]
    if any(not isinstance(name, str) or re.fullmatch(r"car-(?:[0-9]|10)\.glb", name) is None for name in names):
        raise ValueError("Vehicle ledger contains an unmanaged filename")
    return set(names)


def sync(document, generated, bundle, enabled=True):
    directories = [Path(generated), Path(bundle)]
    for directory in directories:
        if not directory.resolve().is_relative_to(PROJECT / "build"):
            raise ValueError("Derived vehicle outputs must remain under ignored project build/")
        directory.mkdir(parents=True, exist_ok=True)
    entries = document["vehicles"]
    registered = {Path(entry["output"]).name for entry in entries}
    active = {Path(entry["output"]).name for entry in entries if enabled and entry["mode"] == "mega"}
    # Validate all inputs/ledgers before changing either directory.
    previous = [managed_names(directory) for directory in directories]
    payloads = {name: (directories[0] / name).read_bytes() for name in active}
    for directory, old in zip(directories, previous):
        for name in old | registered:
            path = directory / name
            if path.is_symlink() or (path.exists() and not path.is_file()):
                raise ValueError("Managed vehicle output must be an ordinary file")
    for directory, old in zip(directories, previous):
        for name in sorted((old | registered) - active):
            (directory / name).unlink(missing_ok=True)
        if directory == directories[1]:
            for name, data in payloads.items():
                atomic_write(directory / name, data)
        atomic_write(directory / LEDGER, (json.dumps({"version": 1, "managed": sorted(registered)}, indent=2) + "\n").encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--generated", type=Path)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--disabled", action="store_true")
    args = parser.parse_args()
    document = load_manifest(args.manifest)
    if args.validate:
        return
    if args.generated is None or args.bundle is None:
        parser.error("--generated and --bundle are required")
    sync(document, args.generated, args.bundle, not args.disabled)


if __name__ == "__main__":
    main()
