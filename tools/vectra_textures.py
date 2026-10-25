#!/usr/bin/env python3
"""Vectra/Vectra2 extraction, third-row name fitting and masked WebP70 preparation."""

import argparse
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

import vehicle_textures as images
from vehicle_manifest import load_definition, load_manifest
from vehicle_model import decode, read_source

PROJECT = Path(__file__).resolve().parents[1]
PACKAGE = PROJECT / "assets/models/vehicles/vectra/textures"
SLOTS = ("VECTRA", "ENGFLO", "WARCH", "WSCRN", "SIDEG", "ListA", "Num8")
VECTRA2_SLOTS = ("VECTRA2", "ENGFLO", "WARCH", "WSCRN", "SIDEG", "ListB", "Num88")


def _entry(key="vectra"):
    if key not in ("vectra", "vectra2"):
        raise ValueError("Unsupported Vectra artwork package")
    return next(entry for entry in load_manifest()["vehicles"] if entry["key"] == key)


def _metadata(directory):
    return images.artwork_metadata(directory, "Vectra")


def fit_uvs(meshes, section, count, directory=None):
    """Fit the canonical third name row for Cleland or Warwick."""
    if directory is None:
        return
    metadata = _metadata(directory)
    ranges = metadata.get("name_uv_ranges", {})
    if not ranges:
        return
    if set(ranges) not in ({"ListA"}, {"ListB"}):
        raise ValueError("Vectra artwork requires its single canonical driver-name slot")
    interval = next(iter(ranges.values()))
    if (not isinstance(interval, list) or len(interval) != 2 or
            not all(type(v) in (int, float) and math.isfinite(v) for v in interval) or
            not 0 <= interval[0] < interval[1] <= 1):
        raise ValueError("Vectra name UV interval must remain within0..1")
    names = [record[0] for record in images.texture_records(section, count)]
    sources = images.artwork_files(directory, names)
    for mesh in meshes:
        selected = {}
        for face in mesh.faces:
            slot = struct.unpack_from("<h", face, 8)[0]
            name = names[slot] if slot >= 0 else None
            if name not in ranges or name not in sources:
                continue
            for index in struct.unpack_from("<3H", face, 2):
                if index in selected and selected[index] != name:
                    raise ValueError("Vectra name faces share vertices across texture slots")
                selected[index] = name
        for index in selected:
            vertex = mesh.vertices[index]
            v = vertex[4] + 1
            if not .25 <= v <= .37501:
                raise ValueError("Vectra name fitting requires the canonical third row")
            low, high = interval
            mesh.vertices[index] = (*vertex[:4], low + (v - .25) / .125 * (high - low) - 1, *vertex[5:])


def opaque_pixels(width, height, triangles):
    """Rasterize only the geometry helper's surviving body-face UV footprints."""
    mask = bytearray(width * height)
    for triangle in triangles:
        if len(triangle) != 3 or any(len(point) != 2 or any(not math.isfinite(v) or not 0 <= v <= 1 for v in point) for point in triangle):
            raise ValueError("Vectra opacity footprints must be bounded UV triangles")
        points = [(u * width, v * height) for u, v in triangle]
        a, b, c = points
        area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(area) < 1e-10:
            continue
        sign = 1 if area > 0 else -1
        edges = [(p, q[0] - p[0], q[1] - p[1], math.dist(p, q)) for p, q in ((a, b), (b, c), (c, a))]
        for y in range(max(0, math.floor(min(p[1] for p in points) - 1)), min(height, math.ceil(max(p[1] for p in points) + 1))):
            for x in range(max(0, math.floor(min(p[0] for p in points) - 1)), min(width, math.ceil(max(p[0] for p in points) + 1))):
                if all(sign * (dx * (y + .5 - p[1]) - dy * (x + .5 - p[0])) >= -length for p, dx, dy, length in edges):
                    mask[y * width + x] = 1
    return mask


def prepare_pixels(name, original, width, height, rgba, opaque_uvs=()):
    """Keep canonical holes, fresh name shapes and real number-decal alpha."""
    if name not in SLOTS + VECTRA2_SLOTS or len(rgba) != width * height * 4:
        raise ValueError("Invalid Vectra artwork slot/pixel bounds")
    result = bytearray(rgba)
    if name in ("Num8", "Num88"):
        result = _number_pixels(width, height, result, 2 if name == "Num88" else 1)
        # Canonical number ink is near-black(0,8,8), distinct from black key.
        # New number glyphs carry explicit alpha; protect opaque black strokes.
        for offset in range(0, len(result), 4):
            if result[offset + 3] >= 250:
                result[offset + 3] = 255
            if result[offset + 3] and max(result[offset:offset + 3]) <= 8:
                result[offset:offset + 3] = b"\x08\x08\x08"
            elif not result[offset + 3]:
                result[offset:offset + 3] = b"\0\0\0"
        return bytes(result)
    if name in ("ListA", "ListB"):
        for offset in range(0, len(result), 4):
            result[offset + 3] = 0 if max(result[offset:offset + 3]) <= 32 else 255
        return bytes(result)
    old_width, old_height, old_pixels = images.bmp_pixels(original)
    released = opaque_pixels(width, height, opaque_uvs) if name in ("VECTRA", "VECTRA2") and opaque_uvs else None
    for y in range(height):
        for x in range(width):
            index = y * width + x
            offset = index * 4
            source = old_pixels[(y * old_height // height) * old_width + x * old_width // width]
            if source == (0, 0, 0) and not (released and released[index]):
                result[offset:offset + 4] = b"\0\0\0\0"
            else:
                result[offset + 3] = 255
                if result[offset:offset + 3] == b"\0\0\0":
                    result[offset:offset + 3] = b"\x08\x08\x08"
    return bytes(result)


def _number_pixels(width, height, rgba, digits=1):
    """Retain real digit components and one texel of their source antialiasing."""
    alpha = rgba[3::4]
    visited = bytearray(width * height)
    components = []
    for index, value in enumerate(alpha):
        if value < 128 or visited[index]:
            continue
        queue, component = [index], []
        visited[index] = 1
        while queue:
            current = queue.pop()
            component.append(current)
            x, y = current % width, current // width
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < width and 0 <= ny < height:
                    neighbor = ny * width + nx
                    if not visited[neighbor] and alpha[neighbor] >= 128:
                        visited[neighbor] = 1
                        queue.append(neighbor)
        components.append(component)
    if not components:
        raise ValueError("Vectra number artwork requires connected digit shapes")
    components.sort(key=len, reverse=True)
    kept = bytearray(width * height)
    for component in components[:digits]:
        for index in component:
            x, y = index % width, index // width
            for ny in range(max(0, y - 1), min(height, y + 2)):
                for nx in range(max(0, x - 1), min(width, x + 2)):
                    kept[ny * width + nx] = 1
    result = bytearray(rgba)
    for index, keep in enumerate(kept):
        if not keep:
            result[index * 4:index * 4 + 4] = b"\0\0\0\0"
    return result


def _converter():
    converter = shutil.which("magick") or shutil.which("convert")
    if not converter:
        raise ValueError("ImageMagick with WebP support is required for Vectra artwork")
    return converter


def prepare_artwork(name, original, source, metadata, opaque_uvs=(), *, quality=70):
    """Resize/mask working artwork before its single final lossy WebP70 encode."""
    dimensions = metadata.get("final_dimensions", {}).get(name)
    if (not isinstance(dimensions, list) or len(dimensions) != 2 or
            any(type(v) is not int or not 1 <= v <= images.MAX_DIMENSION for v in dimensions)):
        raise ValueError(f"Invalid final Vectra dimensions for {name}")
    width, height = dimensions
    old_width, old_height, _ = images.bmp_dimensions(original)
    if width * old_height != height * old_width:
        raise ValueError("Vectra final dimensions must preserve the canonical aspect ratio")
    rgba = subprocess.run([_converter(), str(source), "-alpha", "on", "-filter", "Lanczos", "-resize", f"{width}x{height}!",
                           "-depth", "8", "rgba:-"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if name in ("Num8", "Num88") and (not any(v == 0 for v in rgba[3::4]) or not any(v == 255 for v in rgba[3::4])):
        raise ValueError("New Vectra numbers require explicit transparent background and opaque ink")
    rgba = prepare_pixels(name, original, width, height, rgba, opaque_uvs)
    encoded = subprocess.run([_converter(), "-size", f"{width}x{height}", "-depth", "8", "rgba:-", "-strip",
                              "-define", "webp:lossless=false", "-define", "webp:alpha-quality=100", "-define", "webp:method=6",
                              "-quality", str(quality), "webp:-"], input=rgba, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if images.webp_dimensions(encoded) != (width, height):
        raise ValueError("Vectra final encoder changed texture dimensions")
    return encoded


def replace_textures(section, count, directory=None, opaque_uvs=(), *, quality=70):
    return images.replace_vehicle_textures(section, count, directory, _metadata, prepare_artwork, opaque_uvs, quality=quality)


def extract(output, key="vectra"):
    output = Path(output).resolve()
    if not output.is_relative_to(PROJECT / "build"):
        raise ValueError("Original Vectra media extraction must remain below ignored build/")
    entry = _entry(key)
    header, _, _, section = decode(read_source(entry["source"]))
    output.mkdir(parents=True, exist_ok=True)
    records = images.texture_records(section, struct.unpack_from("<H", header, 16)[0])
    report = dict(source=entry["source"], textures=[])
    for slot, (name, _, payload) in enumerate(records):
        bmp = output / (name + ".bmp")
        png = output / (name + ".png")
        if bmp.is_symlink() or png.is_symlink():
            raise ValueError("Refusing original Vectra extraction through symbolic links")
        bmp.write_bytes(payload)
        subprocess.run([_converter(), str(bmp), str(png)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        width, height, bits = images.bmp_dimensions(payload)
        report["textures"].append(dict(slot=slot, name=name, bmp_crc32=images.crc32(payload), png_crc32=images.crc32(png.read_bytes()),
                                       width=width, height=height, bits=bits))
    (output / "source.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def finalize(verify=False, key="vectra"):
    from vectra_geometry import generate
    return images.finalize_artwork(_entry(key), generate, verify=verify)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    extraction = commands.add_parser("extract")
    extraction.add_argument("--output", type=Path)
    extraction.add_argument("--key", choices=("vectra", "vectra2"), default="vectra")
    finalization = commands.add_parser("finalize")
    finalization.add_argument("--verify", action="store_true")
    finalization.add_argument("--key", choices=("vectra", "vectra2"), default="vectra")
    args = parser.parse_args()
    output = args.output if args.command == "extract" else None
    if args.command == "extract" and output is None:
        output = PROJECT / ("build/vectra2-source-v10" if args.key == "vectra2" else "build/vectra-source-v01")
    print(json.dumps(extract(output, args.key) if args.command == "extract" else finalize(args.verify, args.key), indent=2))


if __name__ == "__main__":
    main()
