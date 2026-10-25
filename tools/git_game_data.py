"""Read unpacked Git game data from the configured root."""

import os
from pathlib import Path, PurePosixPath
import zlib
import struct


def configured_data_root():
    """Resolve the unpacked Git inputs configured by CMake or an offline tool.

    A local reference installation is never an implicit data source.
    """
    value = os.environ.get("TOCA_DATA_ROOT")
    if not value:
        raise ValueError("TOCA_DATA_ROOT must identify the unpacked Git game-data root")
    return Path(value)


def archive_bytes(data_root, relative, *, expected_crc32=None):
    """Read an archive, optionally checking an author's specific source binding."""
    if PurePosixPath(relative).is_absolute() or ".." in PurePosixPath(relative).parts:
        raise ValueError("Archive path must stay inside its game-data root")
    root = Path(data_root).resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        raise ValueError("Archive escapes its game-data root")
    data = path.read_bytes()
    if expected_crc32 is not None and f"{zlib.crc32(data):08x}" != expected_crc32:
        raise ValueError(f"Game-data source binding CRC32 mismatch: {relative}")
    return data


def bfp_members(data):
    if len(data) < 12:
        raise ValueError("Truncated BFP header")
    count, size = struct.unpack_from("<II", data)
    if size != count * 24 or 12 + size > len(data):
        raise ValueError("Invalid BFP directory")
    result = []
    for index in range(count):
        name, offset, length = struct.unpack_from("<16sII", data, 12 + index * 24)
        start = 12 + size + offset
        if start + length > len(data):
            raise ValueError("Truncated BFP member")
        result.append((name.split(b"\0", 1)[0].decode("ascii"), data[start:start + length]))
    return result
