#!/usr/bin/env python3
"""Ford extraction, fifth-row name fitting and masked WebP70 preparation."""

import argparse
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess

import vehicle_textures as images
from vehicle_manifest import load_manifest
from vehicle_model import decode, read_source

PROJECT = Path(__file__).resolve().parents[1]
PACKAGE = PROJECT / "assets/models/vehicles/ford/textures"
SLOTS = ("MON", "ENGFLO", "SIDEG", "WSCRN", "WARCH", "ListA", "ListB", "Num9", "Num15")
SOURCE = dict(archive="data/allcars.bfp", archive_crc32="92273ec9", member=16,
              member_name="cars\\ford\\highpc", member_crc32="eda6480d")


def _entry():
    return next(entry for entry in load_manifest()["vehicles"] if entry["key"] == "ford")


def _metadata(directory):
    return images.artwork_metadata(directory, "Ford")


def fit_uvs(meshes, section, count, directory=None):
    """Ford uses its canonical fifth name row without modifying original UVs."""
    return


def opaque_pixels(width, height, triangles):
    """Rasterize only the geometry helper's surviving body-face UV footprints."""
    mask = bytearray(width * height)
    for triangle in triangles:
        if len(triangle) != 3 or any(len(point) != 2 or any(not math.isfinite(v) or not 0 <= v <= 1 for v in point) for point in triangle):
            raise ValueError("Ford opacity footprints must be bounded UV triangles")
        points = [(u * width, v * height) for u, v in triangle]
        a, b, c = points
        area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(area) < 1e-10:
            continue
        sign = 1 if area > 0 else -1
        edges = [(p, q[0] - p[0], q[1] - p[1], math.dist(p, q)) for p, q in ((a, b), (b, c), (c, a))]
        for y in range(max(0, math.floor(min(p[1] for p in points) - 2)), min(height, math.ceil(max(p[1] for p in points) + 2))):
            for x in range(max(0, math.floor(min(p[0] for p in points) - 2)), min(width, math.ceil(max(p[0] for p in points) + 2))):
                if all(sign * (dx * (y + .5 - p[1]) - dy * (x + .5 - p[0])) >= -2 * length for p, dx, dy, length in edges):
                    mask[y * width + x] = 1
    return mask


def continue_arch_paint(width, height, rgba, released):
    """Continue fresh blue paint into actual arch faces exposed by mask relief.

    Only the four source-measured upper arch boxes participate. The actual
    geometry footprint supplies every target pixel, so the opening keeps its
    canonical/geometry alpha and no unrelated black detail can be released.
    """
    source = bytes(rgba)
    result = bytearray(rgba)
    def blue(x, y):
        if not 0 <= x < width or not 0 <= y < height:
            return False
        r, g, b = source[(y * width + x) * 4:(y * width + x) * 4 + 3]
        return b > 50 and b > r * 1.2 and b > g * 1.1
    for index, keep in enumerate(released):
        if not keep or max(source[index * 4:index * 4 + 3]) >= 32:
            continue
        x, y = index % width, index // width
        u, v = x * 256 / width, y * 256 / height
        v = min(v, 255.36328125 - v)
        if not 0 <= v <= 37 or not (32 <= u <= 76 or 193 <= u <= 239):
            continue
        nearest, distance = None, float("inf")
        for radius in range(1, math.ceil(width / 16) + 1):
            ring = [(x + dx, y + dy) for dx in range(-radius, radius + 1) for dy in (-radius, radius)]
            ring += [(x + dx, y + dy) for dy in range(-radius + 1, radius) for dx in (-radius, radius)]
            for xx, yy in ring:
                squared = (xx-x) ** 2 + (yy-y) ** 2
                if squared < distance and blue(xx, yy):
                    nearest, distance = (xx, yy), squared
            if radius * radius >= distance:
                break
        if nearest is None:
            raise ValueError("Ford released arch paint has no nearby generated blue continuation")
        offset = (nearest[1] * width + nearest[0]) * 4
        result[index * 4:index * 4 + 3] = source[offset:offset + 3]
    return result


def prepare_pixels(name, original, width, height, rgba, opaque_uvs=()):
    """Keep canonical holes, fresh name shapes and real number-decal alpha."""
    if name not in SLOTS or len(rgba) != width * height * 4:
        raise ValueError("Invalid Ford artwork slot/pixel bounds")
    result = bytearray(rgba)
    if name in ("Num9", "Num15"):
        result = _number_pixels(width, height, result, 2 if name == "Num15" else 1)
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
    released = opaque_pixels(width, height, opaque_uvs) if name in ("MON",) and opaque_uvs else None
    if released:
        result = continue_arch_paint(width, height, result, released)
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
        raise ValueError("Ford number artwork requires connected digit shapes")
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


def register_number_bounds(original, width, height, rgba):
    """Fit fresh digit ink to the actual canonical decal's non-key rectangle.

    The selected Imagegen PNG stays untouched. This final packaging transform
    preserves the generated digit pattern and its transparent counterspaces;
    the source supplies placement/extent only, never original color pixels.
    """
    old_width, old_height, pixels = images.bmp_pixels(original)
    old_ink = [(i % old_width, i // old_width) for i, color in enumerate(pixels) if color != (0, 0, 0)]
    new_ink = [(i % width, i // width) for i, alpha in enumerate(rgba[3::4]) if alpha]
    if not old_ink or not new_ink:
        raise ValueError("Ford number registration needs canonical and generated ink")
    sx, sy = (min(p[k] for p in new_ink) for k in (0, 1))
    right, bottom = (max(p[k] for p in new_ink) + 1 for k in (0, 1))
    tx = round(min(p[0] for p in old_ink) * width / old_width)
    ty = round(min(p[1] for p in old_ink) * height / old_height)
    target_right = round((max(p[0] for p in old_ink) + 1) * width / old_width)
    target_bottom = round((max(p[1] for p in old_ink) + 1) * height / old_height)
    return subprocess.run([_converter(), "-size", f"{width}x{height}", "xc:none", "(",
                           "-size", f"{width}x{height}", "-depth", "8", "rgba:-",
                           "-crop", f"{right-sx}x{bottom-sy}+{sx}+{sy}", "+repage", "-filter", "Lanczos",
                           "-resize", f"{target_right-tx}x{target_bottom-ty}!", ")",
                           "-geometry", f"+{tx}+{ty}", "-compose", "Over", "-composite", "-depth", "8", "rgba:-"],
                          input=rgba, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout


def register_name_bounds(original, width, height, rgba):
    """Register each fresh name to its canonical row and exact ink rectangle."""
    old_width, old_height, pixels = images.bmp_pixels(original)
    result = bytearray(width * height * 4)
    for row in range(8):
        y0, y1 = round(row * height / 8), round((row + 1) * height / 8)
        oy0, oy1 = row * old_height // 8, (row + 1) * old_height // 8
        old = [(i % old_width, i // old_width) for i in range(oy0 * old_width, oy1 * old_width) if pixels[i] != (0, 0, 0)]
        new = [(i % width, i // width) for i in range(y0 * width, y1 * width) if rgba[i * 4 + 3]]
        if not old or not new:
            raise ValueError("Ford name atlas requires eight source and generated names")
        sx, sy = min(x for x, y in new), min(y for x, y in new)
        right, bottom = max(x for x, y in new) + 1, max(y for x, y in new) + 1
        tx, ty = round(min(x for x, y in old) * width / old_width), round(min(y for x, y in old) * height / old_height)
        tw = round((max(x for x, y in old) + 1) * width / old_width) - tx
        th = round((max(y for x, y in old) + 1) * height / old_height) - ty
        fitted = subprocess.run([_converter(), "-size", f"{width}x{height}", "xc:none", "(",
                                "-size", f"{width}x{height}", "-depth", "8", "rgba:-", "-crop",
                                f"{right-sx}x{bottom-sy}+{sx}+{sy}", "+repage", "-filter", "Lanczos", "-resize", f"{tw}x{th}!", ")",
                                "-geometry", f"+{tx}+{ty}", "-compose", "Over", "-composite", "-depth", "8", "rgba:-"],
                               input=rgba, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
        result[y0 * width * 4:y1 * width * 4] = fitted[y0 * width * 4:y1 * width * 4]
    return bytes(result)


def _converter():
    converter = shutil.which("magick") or shutil.which("convert")
    if not converter:
        raise ValueError("ImageMagick with WebP support is required for Ford artwork")
    return converter


def prepare_artwork(name, original, source, metadata, opaque_uvs=(), *, quality=70):
    """Resize/mask working artwork before its single final lossy WebP70 encode."""
    dimensions = metadata.get("final_dimensions", {}).get(name)
    if (not isinstance(dimensions, list) or len(dimensions) != 2 or
            any(type(v) is not int or not 1 <= v <= images.MAX_DIMENSION for v in dimensions)):
        raise ValueError(f"Invalid final Ford dimensions for {name}")
    width, height = dimensions
    old_width, old_height, _ = images.bmp_dimensions(original)
    if width * old_height != height * old_width:
        raise ValueError("Ford final dimensions must preserve the canonical aspect ratio")
    command = [_converter(), str(source)]
    command += ["-alpha", "on", "-filter", "Lanczos", "-resize", f"{width}x{height}!", "-depth", "8", "rgba:-"]
    rgba = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if name in ("Num9", "Num15") and (not any(v == 0 for v in rgba[3::4]) or not any(v == 255 for v in rgba[3::4])):
        raise ValueError("New Ford numbers require explicit transparent background and opaque ink")
    rgba = prepare_pixels(name, original, width, height, rgba, opaque_uvs)
    if name in ("ListA", "ListB"):
        rgba = register_name_bounds(original, width, height, rgba)
    if name in ("Num9", "Num15"):
        rgba = register_number_bounds(original, width, height, rgba)
    encoded = subprocess.run([_converter(), "-size", f"{width}x{height}", "-depth", "8", "rgba:-", "-strip",
                              "-define", "webp:lossless=false", "-define", "webp:alpha-quality=100", "-define", "webp:method=6",
                              "-quality", str(quality), "webp:-"], input=rgba, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if images.webp_dimensions(encoded) != (width, height):
        raise ValueError("Ford final encoder changed texture dimensions")
    return encoded


def replace_textures(section, count, directory=None, opaque_uvs=(), *, quality=70):
    return images.replace_vehicle_textures(section, count, directory, _metadata, prepare_artwork, opaque_uvs, quality=quality)


def extract(output):
    output = Path(output).resolve()
    if output != (PROJECT / "build/ford-source-v04").resolve():
        raise ValueError("Original Ford extraction must remain in ignored build/ford-source-v04/")
    header, _, _, section = decode(read_source(SOURCE))
    output.mkdir(parents=True, exist_ok=True)
    records = images.texture_records(section, struct.unpack_from("<H", header, 16)[0])
    report = dict(source=SOURCE, textures=[])
    for slot, (name, _, payload) in enumerate(records):
        bmp = output / (name + ".bmp")
        png = output / (name + ".png")
        if bmp.is_symlink() or png.is_symlink():
            raise ValueError("Refusing original Ford extraction through symbolic links")
        bmp.write_bytes(payload)
        subprocess.run([_converter(), str(bmp), str(png)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        width, height, bits = images.bmp_dimensions(payload)
        report["textures"].append(dict(slot=slot, name=name, bmp_crc32=images.crc32(payload), png_crc32=images.crc32(png.read_bytes()),
                                       width=width, height=height, bits=bits))
    (output / "source.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def finalize(verify=False):
    from ford_geometry import generate
    return images.finalize_artwork(_entry(), generate, verify=verify)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    extraction = commands.add_parser("extract")
    extraction.add_argument("--output", type=Path)
    finalization = commands.add_parser("finalize")
    finalization.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    output = args.output if args.command == "extract" else None
    if args.command == "extract" and output is None:
        output = PROJECT / "build/ford-source-v04"
    print(json.dumps(extract(output) if args.command == "extract" else finalize(args.verify), indent=2))


if __name__ == "__main__":
    main()
