#!/usr/bin/env python3
"""Render captured original scene inputs natively and report uncensored raster differences."""

import argparse
import csv
import importlib.util
import json
import math
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time
import zlib

PROJECT = Path(__file__).resolve().parent.parent
ANIMATION_SPEC = importlib.util.spec_from_file_location("render_reference_animation", PROJECT / "tools/render-reference-animation.py")
animation = importlib.util.module_from_spec(ANIMATION_SPEC)
ANIMATION_SPEC.loader.exec_module(animation)
ACTORS_SPEC = importlib.util.spec_from_file_location("render_reference_actors", PROJECT / "tools/render-reference-actors.py")
actors = importlib.util.module_from_spec(ACTORS_SPEC)
ACTORS_SPEC.loader.exec_module(actors)
SURFACE_TEXTURES = ("smoke", "dust", "spray", "mud", "gravel", "grass")
SURFACE_ORDER = ("spray", "mud", "gravel", "grass", "smoke", "dust")
EFFECT_TEXTURES = (*SURFACE_TEXTURES, "bang")
EFFECT_ORDER = ("spray", "bang", "mud", "gravel", "grass", "smoke", "dust")


def crc32(path):
    return f"{zlib.crc32(path.read_bytes()):08x}"


def verify_car_geometry(geometry, state):
    if len(geometry) != len(state["car_nodes"]) or any(not int(row["matched"]) for row in geometry):
        raise RuntimeError("Native canonical car hierarchy is incomplete or contains unmatched source node IDs")
    root = state["car_nodes"][0]
    roots = [row for row in geometry if int(row["node_id"]) == root["id"]]
    # The cockpit hides the exterior LOD. The source then retains its old world
    # matrices, while the physical car root continues to follow the body.
    if len(roots) != 1 or root.get("visible", 1) & 1 and float(roots[0]["max_matrix_difference"]) != 0:
        raise RuntimeError("Native canonical car root differs from the exact captured published root")


def render_effect_batches(state):
    """Retain admitted source batches; unknown bindings stay explicit in scope."""
    batches = state.get("effect_batches", [])
    if not isinstance(batches, list) or len(batches) >= 128:
        raise ValueError("Captured effect batches exceed the source queue")
    result, sprites, previous = [], 0, -1
    for batch in batches:
        if not isinstance(batch, dict) or not isinstance(batch.get("sprites"), list):
            raise ValueError("Captured effect batch is malformed")
        sprites += len(batch["sprites"])
        if sprites >= 128:
            raise ValueError("Captured effects exceed the127-sprite queue")
        known = batch.get("texture") in EFFECT_TEXTURES
        if known:
            rank = EFFECT_ORDER.index(batch["texture"])
            if rank <= previous:
                raise ValueError("Captured surface batches duplicate or reorder source submissions")
            previous = rank
            expected = 6 if batch["texture"] in ("spray", "bang") else 2
            if batch.get("flags") != expected:
                raise ValueError("Captured surface batch has the wrong source blend flags")
        for sprite in batch["sprites"]:
            for name in ("center", "minimum", "maximum"):
                values = sprite.get(name)
                if (not isinstance(values, (list, tuple)) or len(values) != 3
                        or any(type(value) is not int or not -(1 << 31) <= value < (1 << 31) for value in values)):
                    raise ValueError("Captured sprite requires signed Q16 vectors")
            if any(type(sprite.get(name)) is not int or not 0 <= sprite[name] <= 255 for name in ("shade0", "shade1")):
                raise ValueError("Captured sprite shades must be bytes")
            if known and sprite.get("transform_pointer", 0):
                raise ValueError("Actor-relative captured sprites need their source transform before comparison")
        if known:
            result.append(batch)
    return result


def surface_batches(state):
    return [batch for batch in render_effect_batches(state) if batch["texture"] in SURFACE_TEXTURES]


def hud_snapshot(state):
    hud = state.get("hud")
    if hud is None:
        return None
    if not isinstance(hud, dict):
        raise ValueError("Captured HUD must be a cache snapshot")
    bounds = {"speed_integer": (0, 260), "rpm": (2000, 8500), "gear": (0, 7), "start_phase": (0, 4),
              "lap": (0, 99), "lap_limit": (0, 99), "lap_time_ms": (0, 0xffffffff), "best_lap_ms": (0, 0xffffffff),
              "mode": (0, 3), "position": (0, 16), "split_ticks": (-32768, 32767)}
    for name, (minimum, maximum) in bounds.items():
        if type(hud.get(name)) is not int or not minimum <= hud[name] <= maximum:
            raise ValueError(f"Captured HUD {name} is outside its displayed range")
    for name in ("kilometres", "visible", "qualifying", "split_active"):
        if type(hud.get(name)) is not bool:
            raise ValueError(f"Captured HUD {name} must be boolean")
    if hud["mode"] != 2 and hud["lap_limit"] == 0:
        raise ValueError("Captured race HUD requires a positive lap limit")
    text = hud.get("split_text")
    if not isinstance(text, str) or len(text) > 15 or any(character not in "0123456789:+-." for character in text):
        raise ValueError("Captured HUD split text has unsupported glyphs")
    return hud


def session_caption(state):
    caption = state.get("session_caption")
    if caption is None:
        return None
    if (not isinstance(caption, dict) or type(caption.get("language")) is not int or not 0 <= caption["language"] <= 4
            or type(caption.get("mode")) is not int or not 0 <= caption["mode"] <= 3
            or type(caption.get("visible")) is not bool):
        raise ValueError("Captured rollout caption requires its original language, mode and observed visibility")
    return caption


def validate(state, *, history=False):
    if state.get("schema") != 2 or state.get("exe_crc32") != "8ce56d41":
        raise ValueError("Unsupported original render capture identity/schema")
    barrier = state.get("presentation_barrier")
    if not history and (not isinstance(barrier, dict)
            or any(barrier.get(name) is not True for name in ("main_thread_frozen", "workers_released", "state_unchanged", "stable_window"))):
        raise ValueError("Original pixel comparison requires a verified presentation barrier")
    if not (0 <= state["car_id"] < 10 and 0 <= state["detail"] < 4):
        raise ValueError("Captured local selection requires car0..9 and detail0..3; rival model10 is not a selectable car")
    if "lod_roots" in state:
        expected = "high" if state["detail"] >= 2 else "medium"
        roots = state["lod_roots"]
        expected_visible = [] if state.get("source_view") == 1 else [expected]
        if (state.get("source_view") not in (1, 2, 3, 4, 5) or state.get("selected_lod") != expected
                or sorted(root["name"] for root in roots) != ["high", "low", "medium"]
                or [root["name"] for root in roots if root["visible_mask"] & 1] != expected_visible
                or next(root["address"] for root in roots if root["name"] == expected) != state["car_nodes"][0]["address"]):
            raise ValueError("Captured view/local LOD eligibility does not match the selected detail")
    if state.get("source_boundary") != "489069-entry-published-scene-before-draw" or not state.get("body_unchanged_during_present") or not state.get("published_body_root_matches"):
        raise ValueError("Original screenshot is not bound to a stable completed scene frame")
    for name in ("world_to_view", "camera_world", "car_matrix"):
        if len(state[name]) != 16 or any(not math.isfinite(value) for value in state[name]):
            raise ValueError(f"Invalid original {name} matrix")
    body = bytes.fromhex(state["body_hex"])
    if len(body) != 0x53c:
        raise ValueError("Original draw-boundary body record is truncated")
    if any(struct.pack("<f", coordinate / 65536) != struct.pack("<f", state["car_matrix"][12 + axis])
           for axis, coordinate in enumerate(struct.unpack_from("<3i", body, 0xbc))):
        raise ValueError("Original published car root and recorded draw-boundary body disagree")
    projection = state["projection_q16"]
    if (not isinstance(projection, (list, tuple)) or len(projection) != 16
            or any(type(value) is not int or not -(1 << 31) <= value < (1 << 31) for value in projection)
            or (state["width"], state["height"]) != (640, 480)):
        raise ValueError("Original projection/drawable state is invalid")
    if any(value != (65536 if index == 11 else 0) for index, value in enumerate(projection) if index not in (0, 5, 10, 14)):
        raise ValueError("Original projection has unsupported off-axis or homogeneous coefficients")
    if len(state["weather_particles"]) != len(state["weather_alpha"]):
        raise ValueError("Original precipitation pool/alpha lengths differ")
    if len(state.get("car_visual_angles", [])) != 2:
        raise ValueError("Capture predates the two original car-visual angle fields; preserve its baseline comparison and capture a fresh frame")
    identities = [node["id"] for node in state["car_nodes"]]
    if not identities or len(identities) != len(set(identities)):
        raise ValueError("Original car hierarchy node IDs are missing or ambiguous")
    if len(bytes.fromhex(state.get("shadow_records_hex", ""))) not in (0, 136):
        raise ValueError("Capture contains multiple actors' shadow records; one-car fixture cannot claim scene identity")
    render_effect_batches(state)
    hud_snapshot(state)
    session_caption(state)
    animation.validate_snapshot(state)
    actors.validate_snapshot(state)
    actors.validate_cockpit(state)
    actors.skid_snapshot(state)
    actors.spark_snapshot(state)
    actors.queued_lines_snapshot(state)
    previous = state.get("presentation_history", [])
    if not isinstance(previous, list) or len(previous) > 64 or history and previous:
        raise ValueError("Original presentation history must contain at most64 nonnested frames")
    selection = ("track_id", "car_id", "weather", "detail", "horizon_index", "width", "height")
    for ordinal, frame in enumerate(previous):
        if (not isinstance(frame, dict) or type(frame.get("history_ordinal")) is not int or frame["history_ordinal"] != ordinal
                or any(frame.get(key) != state[key] for key in selection)):
            raise ValueError("Original presentation history lost its chronological scene identity")
        validate(frame, history=True)


def frame_text(state, *, history=False):
    validate(state, history=history)
    rows = ["TOCA_RENDER_12", " ".join(str(state[key]) for key in ("track_id", "car_id", "weather", "detail", "horizon_index", "width", "height"))]
    rows.append(" ".join(str(value) for value in state["world_to_view"]))
    rows.append(" ".join(str(value) for value in struct.unpack_from("<3i", bytes.fromhex(state["body_hex"]), 0xbc)))
    rows.extend(" ".join(str(value) for value in state[key]) for key in ("projection_q16", "car_matrix"))
    rows.append(" ".join(str(value) for value in (state["steering_angle"], *state["wheel_angles"])))
    rows.append(" ".join(str(value) for value in state["car_visual_angles"]))
    shadow = bytes.fromhex(state.get("shadow_records_hex", ""))
    rows.append(str(bool(shadow) * 1))
    if shadow:
        rows.append(" ".join(str(value) for value in struct.unpack("<33iI", shadow)))
    rows.append(str(len(state["weather_particles"])))
    for position, alpha in zip(state["weather_particles"], state["weather_alpha"]):
        rows.append(" ".join(str(value) for value in (*position, *state["weather_streak"], alpha)))
    batches = render_effect_batches(state)
    rows.append(str(len(batches)))
    for batch in batches:
        rows.append(f"{EFFECT_TEXTURES.index(batch['texture'])} {batch['flags']} {len(batch['sprites'])}")
        for sprite in batch["sprites"]:
            rows.append(" ".join(str(value) for value in (*sprite["center"], *sprite["minimum"], *sprite["maximum"], sprite["shade0"], sprite["shade1"])))
    hud = hud_snapshot(state)
    rows.append(str(int(hud is not None)))
    if hud is not None:
        fields = ("speed_integer", "rpm", "gear", "start_phase", "kilometres", "visible", "qualifying", "lap", "lap_limit",
                  "lap_time_ms", "best_lap_ms", "mode", "position", "split_active", "split_ticks")
        rows.append(" ".join(str(int(hud[key])) for key in fields))
        rows.append(json.dumps(hud["split_text"], ensure_ascii=True))
    rows.append(str(len(state["car_nodes"])))
    for node in state["car_nodes"]:
        rows.append(" ".join(str(value) for value in (node["id"], *node["world"])))
    rows.append(" ".join(str(value) for value in state["camera_world"][12:15]))
    animation.append_rows(rows, state)
    caption = session_caption(state)
    rows.append("0" if caption is None else f"1 {caption['language']} {caption['mode']} {int(caption['visible'])}")
    actors.append_rows(rows, state)
    actors.append_cockpit_rows(rows, state)
    actors.append_skid_rows(rows, state)
    actors.append_spark_rows(rows, state)
    actors.append_queued_line_rows(rows, state)
    previous = state.get("presentation_history", [])
    rows.append(str(len(previous)))
    rows.extend(frame_text(frame, history=True).rstrip("\n") for frame in previous)
    return "\n".join(rows) + "\n"


def raster_metrics(first, second, width, rectangle):
    x, y, w, h = rectangle
    errors = []
    changed = 0
    for row in range(y, y + h):
        for column in range(x, x + w):
            offset = (row * width + column) * 3
            delta = [abs(first[offset + channel] - second[offset + channel]) for channel in range(3)]
            errors.extend(delta)
            changed += any(delta)
    return {"rectangle": rectangle, "pixels": w * h, "changed_pixels": changed,
            "mean_absolute_rgb_error": sum(errors) / len(errors),
            "maximum_channel_error": max(errors),
            "pixels_equal_fraction": (w * h - changed) / (w * h)}


def image_rgb(path):
    result = subprocess.run(["convert", str(path), "-alpha", "off", "-depth", "8", "RGB:-"],
                            check=True, capture_output=True, timeout=15).stdout
    if len(result) != 640 * 480 * 3:
        raise RuntimeError("Matched screenshots must both be640x480 RGB")
    return result


def run_native(renderer, state_path, output, timeout, reference_raster=False):
    (output / "capture.done").unlink(missing_ok=True)
    spec = importlib.util.spec_from_file_location("isolated_render", PROJECT / "tools/run-isolated.py")
    isolated = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(isolated)
    directory = Path(tempfile.mkdtemp(prefix="native-runtime-", dir=output))
    for child in ("home", "data", "config", "cache", "state", "runtime", "tmp", "work"):
        (directory / child).mkdir(mode=0o700)
    runtime = isolated.Runtime(directory)
    handlers = {sig: signal.signal(sig, isolated.interrupted) for sig in (signal.SIGTERM, signal.SIGINT)}
    try:
        environment = isolated.private_environment(directory)
        isolated.start_display(runtime, environment, "800x600x24")
        with (output / "native.log").open("wb") as log:
            command = [str(renderer.resolve()), str(PROJECT / "references/toca-1.1a"), str(state_path.resolve()), str(output.resolve())]
            if reference_raster:
                command.append("--reference-raster")
            process = runtime.launch(command, environment, log)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if "TOCA_MATCHED_FRAME_READY" in (output / "native.log").read_text(errors="replace"):
                break
            if process.poll() is not None:
                raise RuntimeError("Native captured-state renderer failed; see native.log")
            time.sleep(.05)
        else:
            raise TimeoutError("Native matched scene did not render")
        windows = subprocess.run(["xdotool", "search", "--name", "^TOCA matched original scene$"], env=environment,
                                 check=True, capture_output=True, text=True, timeout=5).stdout.splitlines()
        if len(windows) != 1:
            raise RuntimeError("Cannot identify a single owned native scene window")
        subprocess.run(["import", "-window", windows[0], str(output / "native-window.png")], env=environment,
                       check=True, capture_output=True, timeout=15)
        (output / "capture.done").touch()
        if process.wait(timeout=max(1, deadline - time.monotonic())) != 0:
            raise RuntimeError("Native matched renderer failed after window capture")
    finally:
        runtime.cleanup()
        for sig, handler in handlers.items():
            signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("state", type=Path)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=45)
    parser.add_argument("--reference-raster", action="store_true", help="use the explicitly identified dgVoodoo2.63 TL/P8 raster convention")
    args = parser.parse_args()
    state = json.loads(args.state.read_text())
    validate(state)
    manifest = json.loads((args.state.parent / "manifest.json").read_text())
    original = args.state.parent / "original.png"
    if manifest["state_crc32"] != crc32(args.state) or manifest["original_png_crc32"] != crc32(original):
        raise RuntimeError("Original render-state or screenshot CRC32 changed")
    args.output.mkdir(parents=True, exist_ok=True)
    renderer_crc32 = crc32(args.renderer)
    frame = args.output / "frame.txt"
    frame.write_text(frame_text(state))
    run_native(args.renderer, frame, args.output, args.timeout, args.reference_raster)
    if crc32(args.renderer) != renderer_crc32:
        raise RuntimeError("Native renderer executable changed during its matched-frame run")
    first, second = image_rgb(original), image_rgb(args.output / "native-window.png")
    # Every framebuffer pixel is reported. Fixed regions are additional
    # diagnostics, without tolerance masks or semantic segmentation.
    regions = {"full_frame": (0, 0, 640, 480),
               "central_scene_fixed_region": (0, 180, 640, 210), "left_lower_fixed_region": (0, 200, 150, 190),
               "center_lower_fixed_region": (150, 200, 390, 190), "left_upper_fixed_region": (0, 45, 100, 45)}
    geometry = list(csv.DictReader((args.output / "geometry.csv").open()))
    actor_geometry = list(csv.DictReader((args.output / "actors.csv").open()))
    if any(not int(row["matched"]) for row in actor_geometry):
        raise RuntimeError("Native actor hierarchy has an unmatched canonical parent/sibling path")
    active_actor_geometry = [row for row in actor_geometry if int(row["rendered"])]
    native_camera = [float(value) for value in (args.output / "native-state.txt").read_text().splitlines()[-1].split()]
    if len(native_camera) != 16 or struct.pack("<16f", *native_camera) != struct.pack("<16f", *state["world_to_view"]):
        raise RuntimeError("Native renderer did not retain the exact captured camera matrix")
    verify_car_geometry(geometry, state)
    report = {"schema": 1, "raster_profile": "reference_dgvoodoo263" if args.reference_raster else "native",
              "source_state_crc32": crc32(args.state), "frame_text_crc32": crc32(frame), "native_renderer_crc32": renderer_crc32,
              "original_png_crc32": crc32(original), "native_window_png_crc32": crc32(args.output / "native-window.png"),
              "selection": {key: state[key] for key in ("track_id", "car_id", "weather", "detail", "horizon_index")},
              "state_match": {"source_presentation_barrier_verified": True,
                              "camera_and_projection_imported_exactly": True, "car_root_imported_exactly": True,
                              "wheel_phases_and_steering_imported": True, "source_shadow_record_imported": bool(state.get("shadow_records_hex")),
                              "source_car_visual_angles_imported": state["car_visual_angles"],
                              "precipitation_positions_imported": len(state["weather_particles"]),
                              "surface_batches_imported": len(surface_batches(state)),
                              "surface_sprites_imported": sum(len(batch["sprites"]) for batch in surface_batches(state)),
                              "engine_sprites_imported": sum(len(batch["sprites"]) for batch in render_effect_batches(state) if batch["texture"] == "bang"),
                              "matched_car_nodes": sum(int(row["matched"]) for row in geometry),
                              "unmatched_car_node_ids": [int(row["node_id"]) for row in geometry if not int(row["matched"])],
                              "native_car_nodes": len(geometry),
                              "maximum_car_node_matrix_error": max(float(row["max_matrix_difference"]) for row in geometry),
                              "matched_actor_paths": len(actor_geometry), "active_actor_nodes": len(active_actor_geometry),
                              "maximum_active_actor_matrix_error": max((float(row["max_matrix_difference"]) for row in active_actor_geometry), default=0),
                              "maximum_retained_actor_matrix_error": max((float(row["max_matrix_difference"]) for row in actor_geometry), default=0)},
              "raster": {name: raster_metrics(first, second, 640, rectangle) for name, rectangle in regions.items()},
              "scope": {**state["scope"], "surface_effect_pools_imported": "effect_batches" in state,
                        "rival_models_imported": actors.validate_snapshot(state) is not None,
                        "ghost_buffers_imported": actors.validate_snapshot(state) is not None and state["render_actors"]["mode"] == 2,
                        "captured_actor_slots": len(state["render_actors"]["actors"]) if actors.validate_snapshot(state) is not None else 0,
                        "animated_scenery_imported": animation.validate_snapshot(state) is not None,
                        "unimported_effect_batches": [{"texture": batch["texture"], "sprites": len(batch["sprites"]), "flags": batch["flags"]}
                                                      for batch in state.get("effect_batches", []) if batch["texture"] not in EFFECT_TEXTURES],
                        "engine_effect_pools_imported": "effect_batches" in state, "native_hud_rendered": hud_snapshot(state) is not None,
                        "native_session_captions_rendered": session_caption(state) is not None,
                        "skid_marks_imported": actors.skid_snapshot(state) is not None,
                        "submitted_skid_quads": sum(quad["rendered"] for quad in state["skid_marks"]["quads"]) if actors.skid_snapshot(state) is not None else 0,
                        "contact_sparks_imported": actors.spark_snapshot(state) is not None,
                        "submitted_spark_lines": sum(line["rendered"] for line in state["spark_lines"]["lines"]) if actors.spark_snapshot(state) is not None else 0,
                        "native_presentation_history_frames": len(state.get("presentation_history", [])),
                        "queued_world_lines_imported": actors.queued_lines_snapshot(state) is not None,
                        "submitted_queued_world_lines": state["queued_lines"]["count"] if actors.queued_lines_snapshot(state) is not None else 0,
                        "fixed_regions_have_no_semantic_segmentation": True},
              "raster_equivalence_established": first == second}
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    subprocess.run(["convert", str(original), str(args.output / "native-window.png"), "+append", str(args.output / "side-by-side.png")], check=True, timeout=15)
    subprocess.run(["convert", str(original), str(args.output / "native-window.png"), "-compose", "difference", "-composite", str(args.output / "difference.png")], check=True, timeout=15)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
