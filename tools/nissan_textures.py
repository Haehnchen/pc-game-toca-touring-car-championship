#!/usr/bin/env python3
"""Nissan extraction, sixth-row name fitting and masked WebP70 preparation."""

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
PACKAGE = PROJECT / "assets/models/vehicles/nissan/textures"
SLOTS = ("Nissan", "SIDEG", "WSCRN", "ENGFLO", "WARCH", "ListA", "ListB", "Num4", "Num23")
SOURCE = dict(archive="data/allcars.bfp", archive_crc32="92273ec9", member=19,
              member_name="cars\\nissan\\high", member_crc32="73b8aae9")


def _entry():
    return next(entry for entry in load_manifest()["vehicles"] if entry["key"] == "nissan")


def _metadata(directory):
    return images.artwork_metadata(directory, "Nissan")


def fit_uvs(meshes, section, count, directory=None):
    """Nissan uses its canonical sixth name row without modifying original UVs."""
    return


def opaque_pixels(width, height, triangles):
    """Rasterize only the geometry helper's surviving body-face UV footprints."""
    mask = bytearray(width * height)
    for triangle in triangles:
        if len(triangle) != 3 or any(len(point) != 2 or any(not math.isfinite(v) or not 0 <= v <= 1 for v in point) for point in triangle):
            raise ValueError("Nissan opacity footprints must be bounded UV triangles")
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


def continue_arch_paint(width, height, rgba, released, original=None):
    """Continue fresh paint only over actual surviving arch and mouth texels.

    World-space aperture clipping supplies all surviving paint footprints. Old
    source cutouts can still cross their lower mouths; those keys receive fresh
    neighboring paint, while source lettering outside the narrow inner band is
    preserved. The lookup rejects generated dark cutout rims and black lettering
    so neither can be repeated as opaque blades across the new paint.
    """
    source, result = bytes(rgba), bytearray(rgba)
    canonical = images.bmp_pixels(original) if original is not None else None
    phase = 255.953125
    for index, keep in enumerate(released):
        if not keep:
            continue
        x, y = index % width, index // width
        u, source_v = (x + .5) * 256 / width, (y + .5) * 256 / height
        lower = source_v > phase / 2
        v = phase - source_v if lower else source_v
        for cx, cy, radius in ((54., 8., 21.5), (207.5, 8., 21.)):
            dx, dy = u - cx, v - cy
            distance = math.hypot(dx, dy)
            if not (-256 / height <= v <= 36 and radius - 5 <= distance <= radius + 5):
                continue
            old_inner_band = (5 - 256 / height <= v and radius - 1 <= distance <= radius + 2)
            if not old_inner_band:
                if canonical is None:
                    continue
                old_width, old_height, old_pixels = canonical
                if old_pixels[(y * old_height // height) * old_width + x * old_width // width] != (0,0,0):
                    continue
            scale = max(radius + 3.5, distance + 1) / distance
            sample_u, sample_v = cx + dx * scale, cy + dy * scale
            if lower:
                sample_v = phase - sample_v
            xx = max(0, min(width - 1, round(sample_u * width / 256 - .5)))
            yy = max(0, min(height - 1, round(sample_v * height / 256 - .5)))
            # Choose the closest genuine generated paint around that radial
            # continuation point, rather than another black cutout/font pixel.
            selected = None
            for reach in range(math.ceil(width / 64) + 1):
                candidates = []
                for py in range(max(0,yy-reach),min(height,yy+reach+1)):
                    for px in range(max(0,xx-reach),min(width,xx+reach+1)):
                        if reach and max(abs(px-xx),abs(py-yy)) != reach:
                            continue
                        offset = (py * width + px) * 4
                        color = source[offset:offset+3]
                        if (max(color) >= 150 or (max(color) > 50 and max(color)-min(color) > 25)):
                            candidates.append(((px-xx)**2+(py-yy)**2,offset))
                if candidates:
                    selected = min(candidates)[1]
                    break
            if selected is None:
                raise ValueError("Nissan released arch paint has no nearby generated paint continuation")
            result[index*4:index*4+3] = source[selected:selected+3]
            break
    return result


def continue_bumper_paint(original, width, height, rgba, released):
    """Extend fresh lower-lip paint only across released coarse corner keys."""
    old_width, old_height, old_pixels = images.bmp_pixels(original)
    source, result = bytes(rgba), bytearray(rgba)
    guard_u, guard_v = 2 * 256 / width, 2 * 256 / height
    for index, keep in enumerate(released):
        if not keep:
            continue
        x, y = index % width, index // width
        u, v = (x + .5) * 256 / width, (y + .5) * 256 / height
        corner = next(((left,right) for left,right in ((163,174),(246,256))
                       if left-guard_u <= u <= right+guard_u and 149-guard_v <= v <= 156+guard_v), None)
        if corner is None or old_pixels[(y * old_height // height) * old_width + x * old_width // width] != (0,0,0):
            continue
        left, right = corner
        # Keep lookup inside this lip chart; a separate fascia chart starts at
        # source row155. Silver and colored marks come only from generated art.
        yy = max(0,min(height-1,round(min(154.5,max(149.,v))*height/256-.5)))
        candidates = []
        for xx in range(max(0,math.ceil(left*width/256)),min(width,math.floor(right*width/256))):
            offset = (yy * width + xx) * 4
            canonical = old_pixels[(yy * old_height // height)*old_width + xx*old_width//width]
            if canonical != (0,0,0) and max(source[offset:offset+3]) > 32:
                candidates.append((abs(xx-x),offset))
        if not candidates:
            raise ValueError("Nissan released lower lip has no fresh paint in its own chart row")
        offset = min(candidates)[1]
        result[index*4:index*4+3] = source[offset:offset+3]
    return result


def prepare_pixels(name, original, width, height, rgba, opaque_uvs=()):
    """Keep canonical holes, fresh name shapes and real number-decal alpha."""
    if name not in SLOTS or len(rgba) != width * height * 4:
        raise ValueError("Invalid Nissan artwork slot/pixel bounds")
    result = bytearray(rgba)
    if name in ("Num4", "Num23"):
        result = _number_pixels(width, height, result, 2 if name == "Num23" else 1)
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
    released = opaque_pixels(width, height, opaque_uvs) if name in ("Nissan",) and opaque_uvs else None
    if released:
        result = continue_arch_paint(width, height, result, released, original)
        result = continue_bumper_paint(original, width, height, result, released)
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
        raise ValueError("Nissan number artwork requires connected digit shapes")
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
        raise ValueError("Nissan number registration needs canonical and generated ink")
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
        # Generation can let a preceding name's last scanline cross an
        # equal-height row boundary. Select the longest occupied vertical run
        # before fitting, so those detached lines never reach the real card.
        occupied = [any(rgba[(y * width + x) * 4 + 3] for x in range(width)) for y in range(y0, y1)]
        runs, begin = [], None
        for y, ink in enumerate(occupied + [False], y0):
            if ink and begin is None:
                begin = y
            elif not ink and begin is not None:
                runs.append((begin,y))
                begin = None
        if not runs:
            raise ValueError("Nissan name atlas has an empty generated row")
        top, bottom = max(runs, key=lambda run: run[1]-run[0])
        new = [(i % width, i // width) for i in range(top * width, bottom * width) if rgba[i * 4 + 3]]
        if not old or not new:
            raise ValueError("Nissan name atlas requires eight source and generated names")
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


def register_body_words(original, width, height, rgba, metadata, registrations=None):
    """Register existing generated word ink; retain surrounding paint/streaks."""
    result = bytearray(rgba)
    old_width,old_height,old = images.bmp_pixels(original)
    for entry in metadata.get("body_words", []):
        source = bytes(result)
        x,y,w,h = entry["search_master"]
        left,right = round(x*width/1254),round((x+w)*width/1254)
        top,bottom = round(y*height/1254),round((y+h)*height/1254)
        red = entry["ink"] == "red"
        def ink(i, weak=False):
            r,g,b = source[i*4:i*4+3]
            return (r>80 and r>(1.15 if weak else 1.5)*g and r>(1.15 if weak else 1.5)*b) if red else max(r,g,b)<(195 if weak else 100)
        remaining = {yy*width+xx for yy in range(top,bottom) for xx in range(left,right) if ink(yy*width+xx)}
        parts = []
        while remaining:
            todo,part = [remaining.pop()],[]
            while todo:
                i = todo.pop();part.append(i);xx,yy=i%width,i//width
                for nx,ny in ((xx-1,yy),(xx+1,yy),(xx,yy-1),(xx,yy+1)):
                    j = ny*width+nx
                    if left<=nx<right and top<=ny<bottom and j in remaining:
                        remaining.remove(j);todo.append(j)
            if max(i//width for i in part)-min(i//width for i in part)+1 >= entry["minimum_height_master"]*height/1254:
                parts.append(part)
        if len(parts) != entry["components"]:
            raise ValueError(f"Nissan {entry['label']} registration requires its exact generated glyph components: {len(parts)}")
        strong = {i for part in parts for i in part}
        sx,sy = min(i%width for i in strong),min(i//width for i in strong)
        sr,sb = max(i%width for i in strong)+1,max(i//width for i in strong)+1
        selected = set(strong)
        for i in strong:
            xx,yy=i%width,i//width
            for ny in range(max(top,yy-1),min(bottom,yy+2)):
                for nx in range(max(left,xx-1),min(right,xx+2)):
                    j = ny*width+nx
                    if ink(j,True):selected.add(j)
        isolated = bytearray(width*height*4)
        for i in selected:
            isolated[i*4:i*4+4] = source[i*4:i*4+3]+b"\xff"
        # Erase only the former ink using nearby neutral generated paint;
        # colored graphics and every other original working pixel stay put.
        for i in selected:
            xx,yy=i%width,i//width
            found = None
            for distance in range(1,math.ceil(height/16)+1):
                for ny in (yy-distance,yy+distance):
                    if not 0<=ny<height:continue
                    j=ny*width+xx;color=source[j*4:j*4+3]
                    if j not in selected and min(color)>170 and max(color)-min(color)<25:
                        found=source[j*4:j*4+3];break
                if found is not None:break
            if found is None:raise ValueError("Nissan word registration has no adjacent fresh neutral paint")
            result[i*4:i*4+3]=found
        if "source_search" in entry:
            x0,y0,x1,y1 = entry["source_search"]
            points = [(xx,yy) for yy in range(y0,y1) for xx in range(x0,x1)
                      if old[yy*old_width+xx][0]>90 and old[yy*old_width+xx][0]>1.5*old[yy*old_width+xx][1]
                      and old[yy*old_width+xx][0]>1.5*old[yy*old_width+xx][2]]
            tx,ty = round(min(x for x,y in points)*width/old_width),round(min(y for x,y in points)*height/old_height)
            tw = round((max(x for x,y in points)+1)*width/old_width)-tx
            th = round((max(y for x,y in points)+1)*height/old_height)-ty
        else:
            tx,ty,tw,th = sx,sy,sr-sx,sb-sy
        command = [_converter(),"-size",f"{width}x{height}","-depth","8","rgba:-","-crop",f"{sr-sx}x{sb-sy}+{sx}+{sy}","+repage"]
        if entry.get("rotate",0):command += ["-rotate",str(entry["rotate"])]
        command += ["-filter","Lanczos","-resize",f"{tw}x{th}!","-depth","8","rgba:-"]
        fitted = subprocess.run(command,input=isolated,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True).stdout
        if "height_source" in entry:
            new_height = round(entry['height_source']*height/256)
            fitted = subprocess.run([_converter(),"-size",f"{tw}x{th}","-depth","8","rgba:-",
                                     "-filter","Lanczos","-resize",f"{tw}x{new_height}!","-depth","8","rgba:-"],
                                    input=fitted,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True).stdout
            ty += round((th-new_height)/2)
            th = new_height
        ty += round(entry.get("vertical_shift_source",0)*height/256)
        if registrations is not None:
            registrations.append(dict(label=entry['label'],x=tx,y=ty,width=tw,height=th,rgba=fitted))
        for yy in range(th):
            for xx in range(tw):
                offset=(yy*tw+xx)*4;target=((ty+yy)*width+tx+xx)*4;a=fitted[offset+3]
                if a:
                    for k in range(3):result[target+k]=round((fitted[offset+k]*a+result[target+k]*(255-a))/255)
    return bytes(result)


def _converter():
    converter = shutil.which("magick") or shutil.which("convert")
    if not converter:
        raise ValueError("ImageMagick with WebP support is required for Nissan artwork")
    return converter


def prepare_artwork(name, original, source, metadata, opaque_uvs=(), *, quality=70):
    """Resize/mask working artwork before its single final lossy WebP70 encode."""
    dimensions = metadata.get("final_dimensions", {}).get(name)
    if (not isinstance(dimensions, list) or len(dimensions) != 2 or
            any(type(v) is not int or not 1 <= v <= images.MAX_DIMENSION for v in dimensions)):
        raise ValueError(f"Invalid final Nissan dimensions for {name}")
    width, height = dimensions
    old_width, old_height, _ = images.bmp_dimensions(original)
    if width * old_height != height * old_width:
        raise ValueError("Nissan final dimensions must preserve the canonical aspect ratio")
    command = [_converter(), str(source)]
    command += ["-alpha", "on", "-filter", "Lanczos", "-resize", f"{width}x{height}!", "-depth", "8", "rgba:-"]
    rgba = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if name == "Nissan":
        rgba = register_body_words(original, width, height, rgba, metadata)
    if name in ("Num4", "Num23") and (not any(v == 0 for v in rgba[3::4]) or not any(v == 255 for v in rgba[3::4])):
        raise ValueError("New Nissan numbers require explicit transparent background and opaque ink")
    rgba = prepare_pixels(name, original, width, height, rgba, opaque_uvs)
    if name in ("ListA", "ListB"):
        rgba = register_name_bounds(original, width, height, rgba)
    if name in ("Num4", "Num23"):
        rgba = register_number_bounds(original, width, height, rgba)
    encoded = subprocess.run([_converter(), "-size", f"{width}x{height}", "-depth", "8", "rgba:-", "-strip",
                              "-define", "webp:lossless=false", "-define", "webp:alpha-quality=100", "-define", "webp:method=6",
                              "-quality", str(quality), "webp:-"], input=rgba, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if images.webp_dimensions(encoded) != (width, height):
        raise ValueError("Nissan final encoder changed texture dimensions")
    return encoded


def replace_textures(section, count, directory=None, opaque_uvs=(), *, quality=70):
    return images.replace_vehicle_textures(section, count, directory, _metadata, prepare_artwork, opaque_uvs, quality=quality)


def extract(output):
    output = Path(output).resolve()
    if output != (PROJECT / "build/nissan-source-v05").resolve():
        raise ValueError("Original Nissan extraction must remain in ignored build/nissan-source-v05/")
    header, _, _, section = decode(read_source(SOURCE))
    output.mkdir(parents=True, exist_ok=True)
    records = images.texture_records(section, struct.unpack_from("<H", header, 16)[0])
    report = dict(source=SOURCE, textures=[])
    for slot, (name, _, payload) in enumerate(records):
        bmp = output / (name + ".bmp")
        png = output / (name + ".png")
        if bmp.is_symlink() or png.is_symlink():
            raise ValueError("Refusing original Nissan extraction through symbolic links")
        bmp.write_bytes(payload)
        subprocess.run([_converter(), str(bmp), str(png)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        width, height, bits = images.bmp_dimensions(payload)
        report["textures"].append(dict(slot=slot, name=name, bmp_crc32=images.crc32(payload), png_crc32=images.crc32(png.read_bytes()),
                                       width=width, height=height, bits=bits))
    (output / "source.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def finalize(verify=False):
    from nissan_geometry import generate
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
        output = PROJECT / "build/nissan-source-v05"
    print(json.dumps(extract(output) if args.command == "extract" else finalize(args.verify), indent=2))


if __name__ == "__main__":
    main()
