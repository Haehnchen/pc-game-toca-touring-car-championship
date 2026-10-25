#!/usr/bin/env python3
"""Peugeot extraction, seventh-row name fitting and masked WebP70 preparation."""

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
PACKAGE = PROJECT / "assets/models/vehicles/peugeot/textures"
SLOTS = ("ENGFLO", "P406", "WSCRN", "SIDEG", "WARCH", "ListA", "ListB", "Num12", "Num14")
SOURCE = dict(archive="data/allcars.bfp", archive_crc32="92273ec9", member=22,
              member_name="cars\\p406\\highpc", member_crc32="c6af623c")


def _entry():
    return next(entry for entry in load_manifest()["vehicles"] if entry["key"] == "peugeot")


def _metadata(directory):
    return images.artwork_metadata(directory, "Peugeot")


def fit_uvs(meshes, section, count, directory=None):
    """Peugeot uses its canonical seventh name row without modifying original UVs."""
    return


def opaque_pixels(width, height, triangles):
    """Rasterize only the geometry helper's surviving body-face UV footprints."""
    mask = bytearray(width * height)
    for triangle in triangles:
        if len(triangle) != 3 or any(len(point) != 2 or any(not math.isfinite(v) or not 0 <= v <= 1 for v in point) for point in triangle):
            raise ValueError("Peugeot opacity footprints must be bounded UV triangles")
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


def prepare_pixels(name, original, width, height, rgba, opaque_uvs=()):
    """Keep canonical holes, fresh name shapes and real number-decal alpha."""
    if name not in SLOTS or len(rgba) != width * height * 4:
        raise ValueError("Invalid Peugeot artwork slot/pixel bounds")
    result = bytearray(rgba)
    if name in ("Num12", "Num14"):
        result = _number_pixels(width, height, result, 2)
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
    released = opaque_pixels(width, height, opaque_uvs) if name in ("P406",) and opaque_uvs else None
    if released:
        result = continue_arch_paint(original, width, height, result, released)
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
        raise ValueError("Peugeot number artwork requires connected digit shapes")
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
        raise ValueError("Peugeot number registration needs canonical and generated ink")
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
            raise ValueError("Peugeot name atlas has an empty generated row")
        top, bottom = max(runs, key=lambda run: run[1]-run[0])
        new = [(i % width, i // width) for i in range(top * width, bottom * width) if rgba[i * 4 + 3]]
        if not old or not new:
            raise ValueError("Peugeot name atlas requires eight source and generated names")
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
        raise ValueError("ImageMagick with WebP support is required for Peugeot artwork")
    return converter


def continue_arch_paint(original, width, height, rgba, released):
    """Continue generated paint over released source keys on surviving arches."""
    ow, oh, old = images.bmp_pixels(original)
    source, result = bytes(rgba), bytearray(rgba)
    def paint(index):
        r,g,b = source[index*4:index*4+3]
        return (g > 65 and g > r*1.15 and g > b*1.15) or (r > 95 and g > 65 and b < r*.9)
    for i, keep in enumerate(released):
        if not keep:
            continue
        x,y = i%width,i//width
        u,v = (x+.5)*256/width,(y+.5)*256/height
        v = min(v,256.05859375-v)
        if not (-1 <= v <= 40 and (24 <= u <= 79 or 181 <= u <= 237)):
            continue
        keyed = old[(y*oh//height)*ow+x*ow//width] == (0,0,0)
        if not keyed:
            # A generated dark antialias rim can sit just outside the old
            # nearest-neighbor key. Continue only its two-output-texel
            # bilinear guard, restricted to surviving physical arch paint.
            if max(source[i*4:i*4+3])>64:
                continue
            nearby=False
            for yy in range(max(0,y-2),min(height,y+3)):
                for xx in range(max(0,x-2),min(width,x+3)):
                    if old[(yy*oh//height)*ow+xx*ow//width]==(0,0,0):
                        nearby=True
                        break
                if nearby:
                    break
            if not nearby:
                continue
        selected = None
        for reach in range(1, math.ceil(width/16)+1):
            candidates=[]
            for yy in range(max(0,y-reach),min(height,y+reach+1)):
                for xx in range(max(0,x-reach),min(width,x+reach+1)):
                    if max(abs(xx-x),abs(yy-y)) != reach:
                        continue
                    j=yy*width+xx
                    if old[(yy*oh//height)*ow+xx*ow//width] != (0,0,0) and paint(j):
                        candidates.append(((xx-x)**2+(yy-y)**2,j))
            if candidates:
                selected=min(candidates)[1]
                break
        if selected is None:
            raise ValueError("Peugeot released arch has no adjacent generated gold/green paint")
        result[i*4:i*4+3]=source[selected*4:selected*4+3]
    return result


def register_gold_edges(original, width, height, rgba, metadata):
    """Locally continue fresh gold along three source-proven physical joins."""
    source,result=bytes(rgba),bytearray(rgba)
    ow,oh,old=images.bmp_pixels(original)
    def gold(i):
        r,g,b=source[i*4:i*4+3]
        return r>100 and g>75 and b<r*.72 and g>r*.55
    def distance(point,a,b):
        dx,dy=b[0]-a[0],b[1]-a[1]
        t=max(0,min(1,((point[0]-a[0])*dx+(point[1]-a[1])*dy)/(dx*dx+dy*dy)))
        return math.hypot(point[0]-a[0]-t*dx,point[1]-a[1]-t*dy)
    for entry in metadata.get("edge_paint_registrations",[]):
        path=entry["path_source"]
        full,fade=entry["full_width_source"],entry["fade_width_source"]
        if (not 0<full<=1 or not 0<fade<=2 or len(path)<2 or
                any(len(p)!=2 or any(not math.isfinite(v) or not 0<=v<=256 for v in p) for p in path)):
            raise ValueError("Peugeot seam registration must stay within measured1+2sourcepx bands")
        margin=full+fade
        left=max(0,math.floor((min(p[0] for p in path)-margin)*width/256))
        right=min(width,math.ceil((max(p[0] for p in path)+margin)*width/256))
        top=max(0,math.floor((min(p[1] for p in path)-margin)*height/256))
        bottom=min(height,math.ceil((max(p[1] for p in path)+margin)*height/256))
        for y in range(top,bottom):
            for x in range(left,right):
                i=y*width+x
                d=min(distance(((x+.5)*256/width,(y+.5)*256/height),a,b) for a,b in zip(path,path[1:]))
                if d>=margin or gold(i):
                    continue
                chosen=None
                for reach in range(1,math.ceil(width/32)+1):
                    choices=[]
                    for yy in range(max(0,y-reach),min(height,y+reach+1)):
                        for xx in range(max(0,x-reach),min(width,x+reach+1)):
                            if max(abs(xx-x),abs(yy-y))!=reach:
                                continue
                            j=yy*width+xx
                            if old[(yy*oh//height)*ow+xx*ow//width]!=(0,0,0) and gold(j):
                                choices.append(((xx-x)**2+(yy-y)**2,j))
                    if choices:
                        chosen=min(choices)[1]
                        break
                if chosen is None:
                    raise ValueError("Peugeot seam has no adjacent fresh gold paint")
                blend=min(1,(margin-d)/fade)
                for k in range(3):
                    result[i*4+k]=round(source[i*4+k]*(1-blend)+source[chosen*4+k]*blend)
    return bytes(result)


def rotate_michelin_ink(width, height, rgba, metadata, registrations=None):
    """Correct only three ink islands; the positive-Z chart itself rotates180°.

    Rear word and mascot rotate independently in their existing bounds. The
    front mascot already has the correct physical orientation and stays put.
    Newly uncovered background comes from adjacent generated gold/green paint;
    source key masks and all material UVs remain unchanged.
    """
    result=bytearray(rgba)
    for entry in metadata.get("michelin_ink_rotations",[]):
        x,y,w,h=entry["master_rectangle"]
        if (entry["kind"] not in ("word","mascot") or
                any(type(v) is not int for v in (x,y,w,h)) or
                min(x,y)<0 or min(w,h)<1 or max(x+w,y+h)>1254):
            raise ValueError("Peugeot Michelin ink registration exceeds selected master")
        left,right=round(x*width/1254),round((x+w)*width/1254)
        top,bottom=round(y*height/1254),round((y+h)*height/1254)
        source=bytes(result)
        def green(i):
            r,g,b=source[i*4:i*4+3]
            return g>40 and g>r*1.15 and g>b*1.15
        selected=set()
        for yy in range(top,bottom):
            for xx in range(left,right):
                i=yy*width+xx;r,g,b=source[i*4:i*4+3]
                dark=max(r,g,b)<180 and not green(i)
                white=entry["kind"]=="mascot" and min(r,g,b)>100 and max(r,g,b)-min(r,g,b)<80
                if dark or white:
                    selected.add(i)
        # Include the generated antialias fringe, but never the nearby green
        # wave. All output alpha remains untouched; this is RGB registration.
        fringe=set(selected)
        for i in selected:
            xx,yy=i%width,i//width
            for ny in range(max(top,yy-1),min(bottom,yy+2)):
                for nx in range(max(left,xx-1),min(right,xx+2)):
                    j=ny*width+nx
                    if max(source[j*4:j*4+3])<210 and not green(j):
                        fringe.add(j)
        selected=fringe
        if not selected:
            raise ValueError("Peugeot Michelin registration has no generated ink")
        def paint(i):
            r,g,b=source[i*4:i*4+3]
            return (g>65 and g>r*1.2 and g>b*1.1) or (r>150 and g>100 and b<r*.8)
        for i in selected:
            xx,yy=i%width,i//width
            found=None
            for reach in range(1,max(right-left,bottom-top)+3):
                candidates=[]
                for ny in range(max(0,yy-reach),min(height,yy+reach+1)):
                    for nx in range(max(0,xx-reach),min(width,xx+reach+1)):
                        if max(abs(nx-xx),abs(ny-yy))!=reach:
                            continue
                        j=ny*width+nx
                        if j not in selected and paint(j):
                            candidates.append(((nx-xx)**2+(ny-yy)**2,j))
                if candidates:
                    found=min(candidates)[1]
                    break
            if found is None:
                raise ValueError("Peugeot Michelin ink has no adjacent fresh paint")
            result[i*4:i*4+3]=source[found*4:found*4+3]
        targets=set()
        for i in selected:
            xx,yy=i%width,i//width
            j=(top+bottom-1-yy)*width+left+right-1-xx
            result[j*4:j*4+3]=source[i*4:i*4+3]
            targets.add(j)
        if registrations is not None:
            registrations.append(dict(label=entry["label"],bounds=(left,top,right,bottom),selected=selected,targets=targets))
    return bytes(result)


def prepare_artwork(name, original, source, metadata, opaque_uvs=(), *, quality=70):
    """Prepare generated PNG once, then encode final WebP70 with exact alpha."""
    dimensions=metadata.get("final_dimensions",{}).get(name)
    if (not isinstance(dimensions,list) or len(dimensions)!=2 or
            any(type(v) is not int or not 1<=v<=images.MAX_DIMENSION for v in dimensions)):
        raise ValueError(f"Invalid final Peugeot dimensions for {name}")
    width,height=dimensions
    ow,oh,_=images.bmp_dimensions(original)
    if width*oh != height*ow:
        raise ValueError("Peugeot final aspect ratio differs from canonical source")
    command=[_converter(),str(source)]
    if name == "P406":
        for patch in metadata.get("body_rotations",[]):
            x,y,w,h=patch["master_rectangle"]
            if any(type(v) is not int for v in (x,y,w,h)) or min(x,y)<0 or min(w,h)<1 or max(x+w,y+h)>1254:
                raise ValueError("Peugeot body rotation exceeds selected1254px master")
            command += ["(",str(source),"-crop",f"{w}x{h}+{x}+{y}","+repage","-rotate","180",
                        ")","-geometry",f"+{x}+{y}","-compose","Over","-composite"]
    command += ["-alpha","on","-filter","Lanczos","-resize",f"{width}x{height}!","-depth","8","rgba:-"]
    rgba=subprocess.check_output(command)
    if name in ("Num12","Num14") and (not any(a==0 for a in rgba[3::4]) or not any(a==255 for a in rgba[3::4])):
        raise ValueError("Peugeot numbers require transparent background and opaque ink")
    if name == "P406":
        rgba=rotate_michelin_ink(width,height,rgba,metadata)
        rgba=register_gold_edges(original,width,height,rgba,metadata)
    rgba=prepare_pixels(name,original,width,height,rgba,opaque_uvs)
    if name in ("ListA","ListB"):
        rgba=register_name_bounds(original,width,height,rgba)
    if name in ("Num12","Num14"):
        rgba=register_number_bounds(original,width,height,rgba)
    encoded=subprocess.check_output([_converter(),"-size",f"{width}x{height}","-depth","8","rgba:-","-strip","-define","webp:lossless=false","-define","webp:alpha-quality=100","-define","webp:method=6","-quality",str(quality),"webp:-"],input=rgba)
    if images.webp_dimensions(encoded)!=(width,height):
        raise ValueError("Peugeot encoder changed dimensions")
    return encoded


def replace_textures(section, count, directory=None, opaque_uvs=(), *, quality=70):
    return images.replace_vehicle_textures(section, count, directory, _metadata, prepare_artwork, opaque_uvs, quality=quality)


def extract(output):
    output = Path(output).resolve()
    if output != (PROJECT / "build/peugeot-source-v06").resolve():
        raise ValueError("Original Peugeot extraction must remain in ignored build/peugeot-source-v06/")
    header, _, _, section = decode(read_source(SOURCE))
    output.mkdir(parents=True, exist_ok=True)
    records = images.texture_records(section, struct.unpack_from("<H", header, 16)[0])
    report = dict(source=SOURCE, textures=[])
    for slot, (name, _, payload) in enumerate(records):
        bmp = output / (name + ".bmp")
        png = output / (name + ".png")
        if bmp.is_symlink() or png.is_symlink():
            raise ValueError("Refusing original Peugeot extraction through symbolic links")
        bmp.write_bytes(payload)
        subprocess.run([_converter(), str(bmp), str(png)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        width, height, bits = images.bmp_dimensions(payload)
        report["textures"].append(dict(slot=slot, name=name, bmp_crc32=images.crc32(payload), png_crc32=images.crc32(png.read_bytes()),
                                       width=width, height=height, bits=bits))
    (output / "source.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def stage_artwork(source, destination):
    if source.stem in ("Num12", "Num14"):
        # Add transparent canvas to the landscape numeral master without
        # resampling its generated ink.
        subprocess.run([_converter(), str(source), "-gravity", "center", "-background", "none", "-extent", "1536x1536",
                        "-define", "webp:lossless=true", "-define", "webp:exact=true",
                        "-define", "webp:method=6", "-quality", "100", str(destination)], check=True)
    else:
        shutil.copyfile(source, destination)


def finalize(verify=False):
    from peugeot_geometry import generate
    return images.finalize_artwork(_entry(), generate, verify=verify, stage_source=stage_artwork)


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
        output = PROJECT / "build/peugeot-source-v06"
    print(json.dumps(extract(output) if args.command == "extract" else finalize(args.verify), indent=2))


if __name__ == "__main__":
    main()
