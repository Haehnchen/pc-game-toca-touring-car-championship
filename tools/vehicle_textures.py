"""Bounded vehicle artwork IO and exact final WebP payload embedding."""

import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

MAX_DIMENSION = 2048
ARTWORK_SUFFIXES = (".bmp", ".png", ".webp")


def webp_dimensions(data, *, max_dimension=MAX_DIMENSION, max_bytes=None, lossy_only=False):
    """Validate a bounded, static WebP container without decoding/re-encoding it."""
    if max_bytes is None:
        max_bytes = max_dimension * max_dimension * 8
    if (len(data) < 20 or len(data) > max_bytes or
            data[:4] != b"RIFF" or data[8:12] != b"WEBP" or
            struct.unpack_from("<I", data, 4)[0] + 8 != len(data)):
        raise ValueError("Expected a complete bounded WebP texture")
    offset, dimensions, image_count, canvas_seen = 12, None, 0, False
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("Truncated WebP chunk")
        tag, size = struct.unpack_from("<4sI", data, offset)
        start = offset + 8
        end = start + size
        if end + (size & 1) > len(data):
            raise ValueError("Truncated WebP chunk payload")
        chunk = data[start:end]
        if tag in (b"ANIM", b"ANMF"):
            raise ValueError("Vehicle artwork must be a static WebP")
        if tag == b"VP8X":
            if (size != 10 or canvas_seen or offset != 12 or chunk[0] & 0xc3
                    or chunk[1:4] != bytes(3)):
                raise ValueError("Invalid or animated WebP canvas")
            canvas_seen = True
            dimensions = (int.from_bytes(chunk[4:7], "little") + 1,
                          int.from_bytes(chunk[7:10], "little") + 1)
        elif tag == b"VP8 ":
            if size < 10 or chunk[3:6] != b"\x9d\x01\x2a" or chunk[0] & 1:
                raise ValueError("Invalid WebP VP8 frame")
            frame = tuple(value & 0x3fff for value in struct.unpack_from("<HH", chunk, 6))
            if dimensions is not None and dimensions != frame:
                raise ValueError("WebP frame does not match its canvas")
            dimensions = frame
            image_count += 1
        elif tag == b"VP8L":
            if lossy_only:
                raise ValueError("Generated source assets must use lossy WebP")
            if size < 5 or chunk[0] != 0x2f:
                raise ValueError("Invalid WebP VP8L frame")
            bits = int.from_bytes(chunk[1:5], "little")
            frame = ((bits & 0x3fff) + 1, ((bits >> 14) & 0x3fff) + 1)
            if bits >> 29 or (dimensions is not None and dimensions != frame):
                raise ValueError("Invalid WebP lossless canvas/version")
            dimensions = frame
            image_count += 1
        offset = end + (size & 1)
    if image_count != 1 or dimensions is None or min(dimensions) < 1 or max(dimensions) > max_dimension:
        raise ValueError(f"WebP artwork requires one image, at most{max_dimension} pixels per side")
    return dimensions


def webp_texture(payload, *, quality=70):
    """Embed final WebP verbatim, or encode prepared BMP with exact-black alpha."""
    if payload[:4] == b"RIFF":
        webp_dimensions(payload)
        return payload
    bmp_dimensions(payload)
    converter = shutil.which("magick") or shutil.which("convert")
    if not converter:
        raise ValueError("ImageMagick with WebP support is required for Mega model generation")
    result = subprocess.run(
        [converter, "bmp:-", "-alpha", "on", "-transparent", "black", "-strip",
         "-define", "webp:lossless=false", "-define", "webp:alpha-quality=100",
         "-define", "webp:method=6", "-quality", str(quality), "webp:-"],
        input=payload, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
    webp_dimensions(result)
    return result


def lossless_webp_dimensions(data):
    """Validate a source WebP with a lossless VP8L frame and bounded dimensions."""
    dimensions = webp_dimensions(data)
    offset = 12
    while offset < len(data):
        tag, size = struct.unpack_from("<4sI", data, offset)
        if tag == b"VP8L":
            return dimensions
        offset += 8 + size + (size & 1)
    raise ValueError("Source artwork requires lossless WebP")


def artwork_files(directory, names):
    """Select final slot images, falling back to lossless sources under work/."""
    directory = Path(directory).resolve(strict=True)
    if not directory.is_dir():
        raise ValueError("Vehicle texture replacement path must be a directory")
    allowed = {name + suffix for name in names for suffix in ARTWORK_SUFFIXES}
    slots = {}
    for source_directory in (directory, directory / "work"):
        if not source_directory.exists():
            continue
        if not source_directory.resolve(strict=True).is_relative_to(directory) or not source_directory.is_dir():
            raise ValueError("Vehicle artwork directory escapes its directory")
        local_slots = {}
        for path in source_directory.iterdir():
            if path.suffix.lower() not in ARTWORK_SUFFIXES:
                continue
            if path.name not in allowed:
                raise ValueError(f"Unknown vehicle texture replacement: {path.name}")
            source = path.resolve(strict=True)
            if not source.is_relative_to(directory) or not source.is_file():
                raise ValueError("Vehicle texture replacement escapes its directory")
            if path.stem in local_slots:
                raise ValueError(f"Supply one generated image for {path.stem}")
            if source.stat().st_size > MAX_DIMENSION * MAX_DIMENSION * 8:
                raise ValueError("Vehicle artwork exceeds the bounded texture file size")
            if source_directory != directory:
                if path.suffix != ".webp":
                    raise ValueError("Working vehicle artwork requires lossless WebP")
                lossless_webp_dimensions(source.read_bytes())
            local_slots[path.stem] = path
        for name, path in local_slots.items():
            slots.setdefault(name, path)
    return slots


def artwork_metadata(directory, label):
    directory = Path(directory).resolve(strict=True)
    path = directory / "metadata.json"
    if not path.resolve(strict=True).is_relative_to(directory) or path.stat().st_size > 16384:
        raise ValueError(f"Invalid {label} artwork metadata path/size")
    return json.loads(path.read_text(encoding="utf-8"))


def replace_vehicle_textures(section, count, directory, metadata_reader, prepare_artwork, opaque_uvs=(), *, quality=70):
    if directory is None:
        return section
    metadata = metadata_reader(directory)
    return replace_textures(section, count, directory,
                            prepare=lambda name, original, source: prepare_artwork(name, original, source, metadata, opaque_uvs, quality=quality))


def replace_textures(section, texture_count, directory=None, prepare=None):
    """Keep slot identity/order; prepare working images before their final encode.

    Root WebP files are final artwork and retain exact bytes and alpha. Lossless
    WebP sources under work/ enter the same preparation as other source images.
    prepare(name, original, source_path) receives a bounded, validated image;
    consumers decode it only in the form their pixel operations actually use.
    """
    if directory is None:
        return section
    records = texture_records(section, texture_count)
    directory = Path(directory).resolve(strict=True)
    sources = artwork_files(directory, [name for name, _, _ in records])
    original_names = {}
    for name, _, original in records:
        if name in sources and name in original_names and original_names[name] != original:
            raise ValueError(f"Ambiguous replacement for distinct original texture slots: {name}")
        original_names[name] = original
    result = bytearray()
    for name, raw_name, original in records:
        replacement = original
        if name in sources:
            source = sources[name]
            if source.suffix == ".webp" and source.parent == directory:
                replacement = source.read_bytes()
                width, height = webp_dimensions(replacement)
            else:
                width, height = source_dimensions(source)
                old_width, old_height, _ = bmp_dimensions(original)
                if width * old_height != height * old_width:
                    raise ValueError(f"Generated {name} must preserve the original texture aspect ratio")
                replacement = prepare(name, original, source) if prepare else generated_bmp(source)
                if replacement[:4] == b"RIFF":
                    width, height = webp_dimensions(replacement)
                else:
                    width, height, _ = bmp_dimensions(replacement, replacement=True)
            old_width, old_height, _ = bmp_dimensions(original)
            if width * old_height != height * old_width:
                raise ValueError(f"Generated {name} must preserve the original texture aspect ratio")
        result.extend(raw_name)
        result.extend(struct.pack("<I", len(replacement)))
        result.extend(replacement)
    return bytes(result)


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def texture_records(section, count):
    offset = 0
    records = []
    for _ in range(count):
        if offset + 16 > len(section):
            raise ValueError("Truncated vehicle texture record")
        raw_name = section[offset:offset + 12]
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
            raise ValueError("Unsafe vehicle texture name")
        size = struct.unpack_from("<I", section, offset + 12)[0]
        end = offset + 16 + size
        if end > len(section):
            raise ValueError("Truncated vehicle texture payload")
        records.append((name, raw_name, section[offset + 16:end]))
        offset = end
    if offset != len(section):
        raise ValueError("Unaccounted vehicle texture bytes")
    return records


def bmp_dimensions(data, replacement=False):
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("Expected a complete BMP texture")
    size, pixel_offset = struct.unpack_from("<I4xI", data, 2)
    dib_size = struct.unpack_from("<I", data, 14)[0]
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if dib_size < 40 or width <= 0 or height == 0 or planes != 1 or pixel_offset < 14 + dib_size or pixel_offset > len(data):
        raise ValueError("Invalid BMP texture header")
    if replacement:
        if width > MAX_DIMENSION or abs(height) > MAX_DIMENSION or bits not in (24, 32) or compression != 0:
            raise ValueError("Generated textures require uncompressed24/32-bit BMP, at most2048 pixels per side")
        row_bytes = ((width * bits + 31) // 32) * 4
        if size != len(data) or pixel_offset + row_bytes * abs(height) != len(data):
            raise ValueError("Invalid generated BMP pixel bounds")
    return width, abs(height), bits


def source_dimensions(path):
    """Bound source dimensions before a preparation callback decodes its pixels."""
    if path.stat().st_size > MAX_DIMENSION * MAX_DIMENSION * 8:
        raise ValueError("Generated vehicle image exceeds the bounded texture file size")
    if path.suffix.lower() == ".bmp":
        width, height, _ = bmp_dimensions(path.read_bytes(), replacement=True)
        return width, height
    if path.suffix.lower() == ".webp":
        return lossless_webp_dimensions(path.read_bytes())
    with path.open("rb") as stream:
        header = stream.read(24)
    if len(header) != 24 or header[:16] != b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR":
        raise ValueError("Expected a PNG texture")
    width, height = struct.unpack_from(">II", header, 16)
    if min(width, height) <= 0 or max(width, height) > MAX_DIMENSION:
        raise ValueError("Generated PNG textures are limited to2048 pixels per side")
    return width, height


def generated_bmp(path):
    source_dimensions(path)
    if path.suffix.lower() == ".bmp":
        data = path.read_bytes()
    else:
        converter = shutil.which("magick") or shutil.which("convert")
        if not converter:
            raise ValueError("ImageMagick is required to decode source textures to BMP3")
        with tempfile.TemporaryDirectory(prefix="toca-vehicle-texture-") as directory:
            target = Path(directory) / "texture.bmp"
            subprocess.run([converter, str(path), "-alpha", "off", "-depth", "8", "-type", "TrueColor", f"BMP3:{target}"],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            data = target.read_bytes()
    bmp_dimensions(data, replacement=True)
    return data


def finalize_artwork(entry, generate, *, verify=False, stage_source=None):
    """Stage masters and use each package's recorded final recipe.

    Vehicle generators own pixel/UV registration. An optional staging callback
    applies source-specific canvas preparation inside the temporary workspace.
    """
    from vehicle_manifest import load_definition
    from vehicle_model import PROJECT, decode, read_source

    key = entry["key"]
    label = key.capitalize()
    package = PROJECT / entry["texture_directory"]
    path = package / "provenance.json"
    provenance = json.loads(path.read_text())
    quality = provenance.get("finalization", {}).get("quality", 70) if verify else 70
    metadata = (package / "metadata.json").read_bytes()
    with tempfile.TemporaryDirectory(prefix=f"toca-{key}-finalize-") as temporary:
        working = Path(temporary)
        (working / "metadata.json").write_bytes(metadata)
        (working / "work").mkdir()
        for asset in provenance["assets"]:
            source = (package / asset["file"]).resolve(strict=True)
            if not source.is_relative_to(package / "work") or source.suffix != ".webp":
                raise ValueError(f"{label} artwork work-source path/CRC mismatch")
            payload = source.read_bytes()
            if crc32(payload) != asset["crc32"]:
                raise ValueError(f"{label} artwork work-source path/CRC mismatch")
            lossless_webp_dimensions(payload)
            destination = working / "work" / source.name
            (stage_source or shutil.copyfile)(source, destination)
        generated, _ = generate(read_source(entry["source"]), load_definition(entry), working, quality=quality)
        header, _, _, section = decode(generated)
        records = {name: payload for name, _, payload in texture_records(section, struct.unpack_from("<H", header, 16)[0])}
        outputs = []
        for asset in provenance["assets"]:
            name = Path(asset["file"]).stem
            payload = records[name]
            dimensions = webp_dimensions(payload)
            output = package / (name + ".webp")
            if verify:
                if output.read_bytes() != payload:
                    raise ValueError(f"{label} final artwork is not reproducible: {output.name}")
            else:
                output.write_bytes(payload)
            outputs.append(dict(file=output.name, source=asset["file"], source_crc32=asset["crc32"], dimensions=list(dimensions),
                                bytes=len(payload), crc32=crc32(payload)))
    if not verify:
        provenance["finalization"] = {"quality": quality}
        path.write_text(json.dumps(provenance, indent=2) + "\n")
    return dict(verified=verify, textures=len(outputs), bytes=sum(asset["bytes"] for asset in outputs), assets=outputs)


def bmp_pixels(data):
    """Decode the canonical indexed BMP or generated RGB BMP without resampling."""
    width, height, bits = bmp_dimensions(data)
    if max(width, height) > MAX_DIMENSION or bits not in (8, 24, 32) or struct.unpack_from("<I", data, 30)[0] != 0:
        raise ValueError("Expected bounded uncompressed indexed/RGB BMP pixels")
    offset = struct.unpack_from("<I", data, 10)[0]
    stride = ((width * bits + 31) // 32) * 4
    if offset + stride * height > len(data):
        raise ValueError("Truncated BMP pixels")
    palette = []
    if bits == 8:
        start = 14 + struct.unpack_from("<I", data, 14)[0]
        count = struct.unpack_from("<I", data, 46)[0] or 256
        if count > 256 or start + count * 4 > offset:
            raise ValueError("Invalid indexed BMP palette")
        palette = [tuple(reversed(data[start + n * 4:start + n * 4 + 3])) for n in range(count)]
    bottom_up = struct.unpack_from("<i", data, 22)[0] > 0
    pixels = []
    for y in range(height):
        row = offset + (height - y - 1 if bottom_up else y) * stride
        for x in range(width):
            position = row + x * (bits // 8)
            if bits == 8:
                index = data[position]
                if index >= len(palette):
                    raise ValueError("BMP palette index exceeds the supplied palette")
                pixels.append(palette[index])
            else:
                pixels.append(tuple(reversed(data[position:position + 3])))
    return width, height, pixels


def rgb_bmp(width, height, pixels):
    stride = (width * 3 + 3) & ~3
    result = bytearray(54 + stride * height)
    result[:2] = b"BM"
    struct.pack_into("<I4xIIiiHHI", result, 2, len(result), 54, 40, width, height, 1, 24, 0)
    for y in range(height):
        row = 54 + (height - 1 - y) * stride
        result[row:row + width * 3] = bytes(channel for pixel in pixels[y * width:(y + 1) * width] for channel in reversed(pixel))
    return bytes(result)
