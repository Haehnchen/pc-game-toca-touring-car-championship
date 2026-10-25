#!/usr/bin/env python3
"""Reproduce static HOR metadata; optionally export local-only original/UV PNGs."""

import argparse
from pathlib import Path

from background_inventory import OUTPUT, REFERENCES, encode, export_atmosphere_references, export_references, inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, required=True, help="Unpacked Git game-data root")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--export-references", nargs="*", metavar="MATERIAL", help="export selected materials, or all51 if empty; always beneath ignored build/")
    parser.add_argument("--export-atmosphere", action="store_true", help="export B05 sun/lens/glow source PNGs beneath ignored build/")
    args = parser.parse_args()
    document = encode(inventory(args.data_root))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(document)
    if args.export_references is not None:
        print(f"Exported {export_references(args.export_references, args.data_root)} original/UV pairs to {REFERENCES}")
    if args.export_atmosphere:
        print(f"Exported {export_atmosphere_references(args.data_root)} atmosphere source references to {REFERENCES}")
    print("Background inventory:51 images,43 unique static members,10 archives")


if __name__ == "__main__":
    main()
