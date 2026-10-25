#!/usr/bin/env python3
"""Extract TOCA 1.1a BFP members without changing or guessing their contents."""

import csv
import os
from pathlib import Path
import re
import struct
import tempfile
import zlib

PROJECT = Path(__file__).resolve().parent.parent
REFERENCE = PROJECT / "references/toca-1.1a"
OUTPUT = PROJECT / "references/assets"
EXE_CRC32 = "8ce56d41"
FIELDS = ("source", "index", "stored_name", "name_field_hex", "offset", "size",
          "crc32", "kind", "output", "storage")


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def read_bfp(data):
    """Layout verified against 1.1a functions 0x486080 and 0x4862a2."""
    if len(data) < 12:
        raise ValueError("Truncated BFP header")
    count, table_size, largest = struct.unpack_from("<III", data)
    base = 12 + table_size
    if table_size != count * 24 or base > len(data):
        raise ValueError("Invalid BFP directory size")
    entries = []
    expected_offset = 0
    for index in range(count):
        name, offset, size = struct.unpack_from("<16sII", data, 12 + index * 24)
        if offset != expected_offset or base + offset + size > len(data):
            raise ValueError(f"Invalid BFP member bounds/order at index {index}")
        entries.append((index, name, base + offset, size))
        expected_offset += size
    if base + expected_offset != len(data):
        raise ValueError("Unaccounted bytes after BFP payload")
    if largest != max((entry[3] for entry in entries), default=0):
        raise ValueError("BFP maximum member size disagrees with directory")
    return entries


def classify(data, name):
    if data.startswith(b"CM3D"):
        return "models", ".c3d"
    if data.startswith(b"BM"):
        return "images", ".bmp"
    if data.startswith(b"RIFF") and data[8:12] == b"WAVE":
        return "audio", ".wav"
    suffix = Path(name.replace("\\", "/")).suffix.lower()
    if suffix == ".hpc":
        return "track-data", suffix
    if suffix in (".dat", ".bin", ".ali", ".al", ".pal"):
        return "tables", suffix
    if suffix == ".raw":
        return "speech", suffix
    return "unknown", ".bin"


def extract():
    if crc32((REFERENCE / "tourcars.exe").read_bytes()) != EXE_CRC32:
        raise ValueError("The reference executable is not the unchanged TOCA 1.1a")
    if OUTPUT.exists():
        raise FileExistsError(f"{OUTPUT} already exists; existing assets will not be overwritten")
    assets = []
    archive_count = 0
    member_count = 0
    # Publish only after the whole extraction succeeds. A failure removes its own staging directory.
    with tempfile.TemporaryDirectory(prefix=".assets-", dir=OUTPUT.parent) as temporary:
        stage = Path(temporary) / "assets"
        stage.mkdir()
        for source in sorted(REFERENCE.rglob("*.bfp")):
            relative = source.relative_to(REFERENCE)
            data = source.read_bytes()
            entries = read_bfp(data)
            archive_count += 1
            member_count += len(entries)
            for index, raw_name, offset, size in entries:
                name = raw_name.split(b"\0", 1)[0].decode("cp1252")
                payload = data[offset:offset + size]
                kind, extension = classify(payload, name)
                # Names may be truncated, repeated, or contain '..'. Identity is archive + index.
                label = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_") or "unnamed"
                path = Path(kind) / relative.with_suffix("") / f"{index:04d}_{label}{extension}"
                target = stage / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(payload)
                assets.append({
                    "source": relative.as_posix(), "index": index,
                    "stored_name": name, "name_field_hex": raw_name.hex(),
                    "offset": offset, "size": size, "crc32": crc32(payload),
                    "kind": kind, "output": path.as_posix(), "storage": "extracted",
                })
        for folder in ("fe", "speech"):
            for source in sorted((REFERENCE / folder).rglob("*")):
                if not source.is_file() or source.suffix.lower() == ".bfp":
                    continue
                relative = source.relative_to(REFERENCE)
                path = Path("loose") / relative
                target = stage / path
                target.parent.mkdir(parents=True, exist_ok=True)
                # Compute for the final location; loose data stays in the single reference copy.
                target.symlink_to(os.path.relpath(source, (OUTPUT / path).parent))
                data = source.read_bytes()
                assets.append({
                    "source": relative.as_posix(), "index": None, "offset": 0,
                    "size": len(data), "crc32": crc32(data),
                    "kind": classify(data, source.name)[0],
                    "output": path.as_posix(), "storage": "symlink",
                })
        with (stage / "manifest.csv").open("w", encoding="utf-8", newline="") as catalog:
            writer = csv.DictWriter(catalog, fieldnames=FIELDS, lineterminator="\n")
            writer.writeheader()
            writer.writerows(assets)
        stage.rename(OUTPUT)
    print(f"Extracted {member_count} members from {archive_count} archives; "
          f"{len(assets)} assets catalogued in {OUTPUT / 'manifest.csv'}")


if __name__ == "__main__":
    extract()
