#!/usr/bin/env python3
"""Render all garage vehicles locally, then compose the original panorama views.

No image-generation service or original garage bitmap is used. Native Mega GLBs
are optional; the exporter falls back to canonical original HIGH models. Every
working render stays in ignored build/. An explicit --publish-dir step copies
reviewed final vehicle WebPs and provenance into the approved garage assets.
"""

import argparse
from copy import deepcopy
from io import BytesIO
import json
import math
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import zlib

from PIL import Image, __version__ as PILLOW_VERSION, features

PROJECT = Path(__file__).resolve().parent.parent
CANVAS = [1152, 720]
VIEW_RECT = (2, 0, 1148, 714)
VIEW_SIZE = [1146, 714]
STARTS = [0, 7, 14, 20, 27, 33, 40, 46, 53, 59]
HIGH_MEMBERS = [1, 4, 7, 13, 16, 19, 22, 25, 28, 31]
NAMES = ["honda", "audi", "vectra", "volvo", "ford", "nissan", "peugeot", "renault", "tank", "flexmobile"]
NUMBERS = [20, 1, 8, 3, 9, 4, 12, 2, None, None]
RECTS = [[304, 312, 608, 386], [220, 336, 594, 364], [200, 326, 574, 374], [280, 340, 608, 362],
         [220, 336, 592, 352], [300, 344, 598, 352], [208, 344, 602, 356], [244, 348, 582, 352],
         [268, 94, 736, 616], [374, 354, 704, 344]]
DEFAULT_LAYOUT = {
    "cars": {str(car): {"rect": rect, "yaw": 20, "eye_height": 1.0} for car, rect in enumerate(RECTS)},
    "interiors": {
        "audi": {"rect": [420, 286, 290, 170], "yaw": 0, "eye_height": 1.0, "darken": 0.30},
        "renault": {"rect": [574, 310, 242, 170], "yaw": 0, "eye_height": 1.0, "darken": 0.35},
    },
}


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def encoded_json(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def fingerprint(value):
    return crc32(encoded_json(value))


def source_label(path):
    try:
        return str(path.relative_to(PROJECT))
    except ValueError:
        return "external/" + path.name


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_bytes() == data:
        return False
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".garage-", delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
            stream.close()
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    return True


def load_json(path, default):
    try:
        return json.loads(path.read_text())
    except (FileNotFoundError, ValueError):
        return default


def layout_from(path):
    layout = deepcopy(DEFAULT_LAYOUT)
    if path:
        overrides = json.loads(path.read_text())
        if not isinstance(overrides, dict) or set(overrides) - {"cars", "interiors"}:
            raise ValueError("Layout contains only cars/interiors override objects")
        for group, entries in overrides.items():
            if not isinstance(entries, dict) or set(entries) - set(layout[group]):
                raise ValueError(f"Unknown {group} layout keys")
            for key, values in entries.items():
                if not isinstance(values, dict) or set(values) - set(layout[group][key]):
                    raise ValueError(f"Unknown layout fields: {group}/{key}")
                layout[group][key].update(values)
    for group in layout.values():
        for config in group.values():
            rect = config["rect"]
            if not isinstance(rect, list) or len(rect) != 4 or any(type(n) not in (int, float) or not math.isfinite(n) for n in rect):
                raise ValueError("Layout rect requires four finite numbers")
            x, y, width, height = rect
            if width <= 0 or height <= 0 or x >= CANVAS[0] or y >= CANVAS[1] or x + width <= 0 or y + height <= 0:
                raise ValueError("Layout rectangle must intersect the canvas")
            if any(type(config[key]) not in (int, float) or not math.isfinite(config[key]) for key in ("yaw", "eye_height")):
                raise ValueError("Layout camera requires finite yaw/eye_height")
            if "darken" in config and (type(config["darken"]) not in (int, float) or not 0 < config["darken"] <= 1):
                raise ValueError("Interior darken must be above0 and at most1")
    return layout


def fit_car(image, rect):
    """Fit alpha>=200 uniformly, preserving all softer edges/shadows."""
    bbox = image.getchannel("A").point(lambda alpha: 255 if alpha >= 200 else 0).getbbox()
    if bbox is None:
        raise ValueError("Native vehicle render contains no opaque vehicle")
    left, top, right, bottom = bbox
    x, y, width, height = rect
    scale = min(width / (right - left), height / (bottom - top))
    dx = x + (width - (right - left) * scale) / 2 - left * scale
    dy = y + height - bottom * scale
    return image.transform(tuple(CANVAS), Image.Transform.AFFINE,
                           (1 / scale, 0, -dx / scale, 0, 1 / scale, -dy / scale), Image.Resampling.BICUBIC)


def render_batch(path):
    """The only GPU subprocesses run inside run-isolated.py's owned display."""
    for request in json.loads(path.read_text()):
        result = subprocess.run(request["command"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        Path(request["log"]).write_text(result.stdout)
        print(result.stdout, flush=True)
        if result.returncode:
            raise RuntimeError(f"Native garage exporter failed: {request['key']}")
        image_path = Path(request["png"])
        with Image.open(image_path) as source:
            if source.mode != "RGBA" or list(source.size) != CANVAS:
                raise ValueError("Native garage render must be RGBA1152x720")
            fitted = fit_car(source, request["rect"])
        stream = BytesIO()
        fitted.save(stream, "PNG")
        write_changed(image_path, stream.getvalue())


def build(args):
    output = args.output_dir.resolve()
    build_root = (PROJECT / "build").resolve()
    if not output.is_relative_to(build_root) or output == build_root:
        raise ValueError("All garage-vehicle outputs must remain in a dedicated directory under ignored build/")
    root, models, exporter = args.data_root.resolve(), args.model_dir.resolve(), args.exporter.resolve()
    if output.is_relative_to(models) or models.is_relative_to(output) or root.is_relative_to(output) or output.is_relative_to(root):
        raise ValueError("Vehicle output must be separate from model and canonical-data directories")
    archive = (root / "data/allcars.bfp").read_bytes()
    if crc32(archive) != "92273ec9" or crc32((root / "tourcars.exe").read_bytes()) != "8ce56d41":
        raise ValueError("Expected unchanged canonical TOCA1.1a data/executable")
    read_bfp = runpy.run_path(str(PROJECT / "tools/extract-assets.py"))["read_bfp"]
    directory = read_bfp(archive)
    layout = layout_from(args.layout)
    tool_crc, exporter_crc = crc32(Path(__file__).read_bytes()), crc32(exporter.read_bytes())
    shader_dir = args.shader_dir.resolve() if args.shader_dir else exporter.parent / "shaders"
    shaders = {path.name: crc32(path.read_bytes()) for path in sorted(shader_dir.glob("legacy.*"))
               if path.suffix in (".spv", ".dxil", ".msl")}
    if not any(all(f"legacy.{stage}.{suffix}" in shaders for stage in ("vert", "frag")) for suffix in ("spv", "dxil", "msl")):
        raise ValueError("Compiled exporter shaders are missing; supply their actual directory with --shader-dir")
    policy = {"script": "tools/render-garage-vehicles.py", "script_crc32": tool_crc,
              "exporter": source_label(exporter), "exporter_crc32": exporter_crc,
              "shader_directory": source_label(shader_dir), "shader_crc32": shaders,
              "pillow": PILLOW_VERSION, "webp": features.version("webp"), "quality": 70, "method": 6}
    cache_path = output / ".garage-vehicles-cache.json"
    old_cache = load_json(cache_path, {})
    if not isinstance(old_cache, dict):
        old_cache = {}
    old_renders = old_cache.get("renders", {})
    if not isinstance(old_renders, dict):
        old_renders = {}
    original_sources, overrides = {}, {}
    snapshot_dir = output / "_model-sources"
    for car, member in enumerate(HIGH_MEMBERS):
        _, name, offset, size = directory[member]
        original_sources[car] = {"kind": "original-high-cm3d", "archive": source_label(root / "data/allcars.bfp"),
                                 "archive_crc32": "92273ec9", "member": member,
                                 "stored_name": name.split(b"\0", 1)[0].decode("cp1252"), "crc32": crc32(archive[offset:offset + size])}
        path = models / f"car-{car}.glb"
        snapshot = snapshot_dir / path.name
        if path.is_file():
            data = path.read_bytes()
            write_changed(snapshot, data)
            overrides[car] = {"kind": "mega-glb", "path": source_label(path), "crc32": crc32(data)}
        else:
            snapshot.unlink(missing_ok=True)
    configurations = [(f"car-{car}", car, "second" if car == 0 else "first", layout["cars"][str(car)], f"renders/car-{car}.png")
                      for car in range(10)]
    configurations += [("interior-" + name, car, "second", layout["interiors"][name], f"interiors/{name}.png")
                       for name, car in (("audi", 1), ("renault", 7))]
    requests, render_records = [], {}
    for key, car, livery, config, relative in configurations:
        render_config = {key: config[key] for key in ("rect", "yaw", "eye_height")}
        token = fingerprint({"policy": policy, "original": original_sources[car], "override": overrides.get(car),
                             "livery": livery, "camera": render_config})
        record = old_renders.get(key)
        png = output / relative
        if isinstance(record, dict) and record.get("fingerprint") == token and png.is_file() and crc32(png.read_bytes()) == record.get("crc32"):
            render_records[key] = record
            continue
        png.parent.mkdir(parents=True, exist_ok=True)
        log = png.with_suffix(".log")
        command = [str(exporter), str(root), str(snapshot_dir), str(car), livery, str(config["yaw"]), str(config["eye_height"]),
                   *map(str, config["rect"]), str(png)]
        requests.append({"key": key, "command": command, "log": str(log), "png": str(png), "rect": config["rect"]})
        render_records[key] = {"fingerprint": token, "file": relative, "car_id": car, "livery": livery,
                               "name": NAMES[car], "original_high": original_sources[car], "override": overrides.get(car),
                               "race_number": NUMBERS[car] if key.startswith("car-") else (7 if car == 1 else 22),
                               "camera": render_config, "log": str(log.relative_to(output))}
    if requests:
        request_path = output / "_render-batch.json"
        write_changed(request_path, encoded_json(requests))
        command = [sys.executable, str(PROJECT / "tools/run-isolated.py"), "--name", "garage-vehicles", "--artifacts", str(output / "_isolated"),
                   "--timeout", str(max(60, len(requests) * 15)), "--display", "--screen", "1152x720x24", "--", sys.executable,
                   str(Path(__file__).resolve()), "--_render-batch", str(request_path)]
        subprocess.run(command, check=True)
    for request in requests:
        key = request["key"]
        record = render_records[key]
        log = Path(request["log"]).read_text()
        actual_mega = "Garage model reference: source=mega " in log
        if not actual_mega and "Garage model reference: source=original " not in log:
            raise ValueError("Native exporter did not report its actual model source")
        record["model"] = overrides[record["car_id"]] if actual_mega else original_sources[record["car_id"]]
        record["crc32"] = crc32(Path(request["png"]).read_bytes())
        with Image.open(request["png"]) as image:
            record["alpha_bbox_xyxy"] = list(image.getchannel("A").getbbox())
    token = fingerprint({"policy": policy, "layout": layout, "starts": STARTS,
                         "renders": {key: record["crc32"] for key, record in render_records.items()}})
    index_path = output / "vehicle-layers.json"
    previous = load_json(index_path, {})
    if old_cache.get("composition_fingerprint") == token and isinstance(previous, dict) and previous.get("entries"):
        files = list(previous["entries"].values()) + [previous.get("empty_layer", {})]
        if all(isinstance(record, dict) and isinstance(record.get("file"), str) and (output / record["file"]).is_file()
               and crc32((output / record["file"]).read_bytes()) == record.get("crc32") for record in files):
            previous["renders"] = render_records
            write_changed(index_path, encoded_json(previous))
            write_changed(cache_path, encoded_json({"schema_version": 1, "renders": render_records, "composition_fingerprint": token}))
            print(f"Garage vehicle layers: {len(requests)} renders, 19 unchanged foregrounds")
            return
    images = {car: Image.open(output / f"renders/car-{car}.png").convert("RGBA") for car in range(10)}
    interiors = {}
    for name, car in (("audi", 1), ("renault", 7)):
        image = Image.open(output / f"interiors/{name}.png").convert("RGBA")
        red, green, blue, alpha = image.split()
        table = [round(value * layout["interiors"][name]["darken"]) for value in range(256)]
        interiors[car] = Image.merge("RGBA", (red.point(table), green.point(table), blue.point(table), alpha))
    foregrounds = [(1, car) for car in range(7)] + [(variant, car) for variant in range(1, 5) for car in range(7, 10)]
    entries, written = {}, 0
    for variant, car in foregrounds:
        image = Image.new("RGBA", tuple(CANVAS))
        for neighbor, layer in interiors.items():
            image.alpha_composite(layer, ((STARTS[neighbor] - STARTS[car]) * 128, 0))
        included = []
        for neighbor, layer in images.items():
            if (neighbor == 8 and variant not in (2, 4)) or (neighbor == 9 and variant not in (3, 4)):
                continue
            shift = (STARTS[neighbor] - STARTS[car]) * 128
            bounds = render_records[f"car-{neighbor}"]["alpha_bbox_xyxy"]
            if bounds[0] + shift >= CANVAS[0] or bounds[2] + shift <= 0:
                continue
            image.alpha_composite(layer, (shift, 0))
            included.append({"car_id": neighbor, "x_shift": shift})
        relative = f"foregrounds/gar{variant}-car{car}.webp"
        stream = BytesIO()
        image = image.crop(VIEW_RECT)
        image.save(stream, "WEBP", quality=70, method=6)
        data = stream.getvalue()
        written += write_changed(output / relative, data)
        bbox = image.getchannel("A").getbbox()
        entries[f"gar{variant}-car{car}"] = {"file": relative, "crc32": crc32(data), "bytes": len(data), "variant": variant, "car_id": car,
                                            "foreground_empty": bbox is None, "alpha_bbox_xyxy": list(bbox) if bbox else None,
                                            "vehicles": included, "source": "local-model-render"}
    empty_stream = BytesIO()
    Image.new("RGBA", tuple(VIEW_SIZE)).save(empty_stream, "WEBP", quality=70, method=6)
    empty_data = empty_stream.getvalue()
    write_changed(output / "foregrounds/empty.webp", empty_data)
    index = {"schema_version": 1, "canvas": VIEW_SIZE, "render_canvas": CANVAS, "crop": list(VIEW_RECT),
             "encoding": "WebP quality70 method6; original alpha preserved", "policy": policy,
             "layout": layout, "panorama_starts": STARTS, "strip_width": 128, "renders": render_records, "entries": entries,
             "empty_layer": {"file": "foregrounds/empty.webp", "crc32": crc32(empty_data)},
             "note": "Every vehicle is rendered locally. Interiors use second livery, native yaw0 and RGB-only darkening; neighboring cars use original strip offsets."}
    write_changed(index_path, encoded_json(index))
    write_changed(cache_path, encoded_json({"schema_version": 1, "renders": render_records, "composition_fingerprint": token}))
    print(f"Garage vehicle layers: {len(requests)} renders, {written} foregrounds written, {len(entries)} indexed")


def publish(output, destination, manifest_path):
    output, destination, manifest_path = output.resolve(), destination.resolve(), manifest_path.resolve()
    garage = (PROJECT / "assets/ui/mega/garage").resolve()
    if not destination.is_relative_to(garage) or not manifest_path.is_relative_to(garage):
        raise ValueError("Publishing is restricted to assets/ui/mega/garage/ artwork and manifest")
    index = json.loads((output / "vehicle-layers.json").read_text())
    manifest = json.loads(manifest_path.read_text())
    expected_keys = {f"gar1-car{car}" for car in range(7)} | {f"gar{variant}-car{car}" for variant in range(1, 5) for car in range(7, 10)}
    if index.get("schema_version") != 1 or index.get("canvas") != VIEW_SIZE or set(index.get("entries", {})) != expected_keys:
        raise ValueError("Publishing requires the complete19-view local render index")
    if manifest.get("schema_version") != 1 or manifest.get("canvas") != VIEW_SIZE or not isinstance(manifest.get("entries"), list):
        raise ValueError("Expected the garage generation manifest schema1/canvas1146x714")
    if len(manifest["entries"]) != 19 or {entry.get("key") for entry in manifest["entries"]} != expected_keys:
        raise ValueError("Publishing requires all19 unique original garage keys in the manifest")
    payloads, records = {}, {}
    for key, record in index["entries"].items():
        if record.get("file") != f"foregrounds/{key}.webp":
            raise ValueError("Unexpected local foreground output path")
        data = (output / record["file"]).read_bytes()
        if crc32(data) != record["crc32"]:
            raise ValueError(f"Local foreground CRC32 differs: {key}")
        if len(data) != record["bytes"]:
            raise ValueError(f"Local foreground size differs: {key}")
        filename = key + ".webp"
        payloads[filename] = data
        records[key] = dict(record, file=filename)
    empty = index["empty_layer"]
    empty_data = (output / empty["file"]).read_bytes()
    if crc32(empty_data) != empty["crc32"]:
        raise ValueError("Local empty layer CRC32 differs")
    payloads["empty.webp"] = empty_data
    seen = set()
    for entry in manifest["entries"]:
        key = entry.get("key")
        if key not in records or key in seen:
            raise ValueError("Garage manifest contains unknown or duplicate local foreground keys")
        seen.add(key)
        target = destination / records[key]["file"]
        entry["foreground"] = str(target.relative_to(manifest_path.parent))
        entry["foreground_empty"] = records[key]["foreground_empty"]
        entry.pop("foreground_source", None)
    if isinstance(manifest.get("local_render"), dict):
        manifest["local_render"] = {"index": str((destination / "rendering.json").relative_to(manifest_path.parent))}
    published = {field: index[field] for field in ("crop", "strip_width", "panorama_starts", "layout")}
    published["entries"] = {key: {"vehicles": record["vehicles"]} for key, record in records.items()}
    published["renders"] = {key: {"alpha_bbox_xyxy": record["alpha_bbox_xyxy"]}
                            for key, record in index["renders"].items()}
    for filename, data in payloads.items():
        write_changed(destination / filename, data)
    write_changed(destination / "rendering.json", encoded_json(published))
    write_changed(manifest_path, (json.dumps(manifest, indent=2) + "\n").encode())
    print(f"Published19 local vehicle layers +empty.webp and rendering.json to {source_label(destination)}; updated {len(seen)} manifest entries")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exporter", type=Path)
    parser.add_argument("--data-root", type=Path)
    parser.add_argument("--model-dir", "--models-dir", dest="model_dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--layout", type=Path, help="JSON overrides for cars/interiors rect,yaw,eye_height and interior darken")
    parser.add_argument("--shader-dir", type=Path, help="Compiled shader location for CRC32 tracking (default: exporter directory/shaders)")
    parser.add_argument("--publish-dir", type=Path, help="Explicitly copy final WebP layers +rendering.json into assets/ui/mega/garage/cars")
    parser.add_argument("--manifest", type=Path, help="Garage generation.json to update when --publish-dir is requested")
    parser.add_argument("--_render-batch", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args._render_batch:
        render_batch(args._render_batch)
        return
    if any(getattr(args, name) is None for name in ("exporter", "data_root", "model_dir", "output_dir")):
        parser.error("--exporter, --data-root, --model-dir and --output-dir are required")
    if bool(args.publish_dir) != bool(args.manifest):
        parser.error("Use --publish-dir and --manifest together for the explicit publish step")
    build(args)
    if args.publish_dir:
        publish(args.output_dir, args.publish_dir, args.manifest)


if __name__ == "__main__":
    main()
