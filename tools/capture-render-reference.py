#!/usr/bin/env python3
"""Read one rendered original1.1a frame and its exact scene inputs on private Xvfb."""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import signal
import struct
import subprocess
import tempfile
import time
import zlib

PROJECT = Path(__file__).resolve().parent.parent


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, PROJECT / "tools" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


reference = module("reference_trace", "trace-reference.py")
isolated = module("isolated_render", "run-isolated.py")
animation = module("render_reference_animation", "render-reference-animation.py")
actors = module("render_reference_actors", "render-reference-actors.py")
presentation = module("render_reference_presentation", "render-reference-presentation.py")
current_source_thread = presentation.current_source_thread


def crc32(path):
    return f"{zlib.crc32(path.read_bytes()):08x}"


def stack(debugger):
    return reference.register_address(debugger.command("print $esp"))


def returned(debugger, address):
    stop = debugger.breakpoint(address)
    debugger.stop_at(address)
    debugger.command(f"delete break {stop}")


def floats(debugger, address):
    values = struct.unpack("<16f", debugger.memory(address, 64))
    if any(not (-1e8 < value < 1e8) for value in values):
        raise RuntimeError("Invalid source scene matrix")
    return values


def nodes(debugger, root):
    result = []
    visited = set()

    def visit(address, parent):
        while address:
            if address in visited or len(visited) >= 256:
                raise RuntimeError("Original car hierarchy is cyclic or exceeds256 nodes")
            visited.add(address)
            record = debugger.memory(address, 0x144)
            sibling, child, actual_parent = struct.unpack_from("<3I", record)
            if parent and actual_parent != parent:
                raise RuntimeError("Original car hierarchy parent mismatch")
            result.append({"address": address, "parent": actual_parent,
                           "id": struct.unpack_from("<I", record, 0x18)[0],
                           "local": struct.unpack_from("<16f", record, 0x5c),
                           "world": struct.unpack_from("<16f", record, 0x9c),
                           "visible": record[0x140]})
            if child:
                visit(child, address)
            address = sibling

    # Root's sibling belongs to another LOD, so inspect only this subtree.
    record = debugger.memory(root, 0x144)
    result.append({"address": root, "parent": struct.unpack_from("<I", record, 8)[0],
                   "id": struct.unpack_from("<I", record, 0x18)[0],
                   "local": struct.unpack_from("<16f", record, 0x5c),
                   "world": struct.unpack_from("<16f", record, 0x9c), "visible": record[0x140]})
    visited.add(root)
    child = struct.unpack_from("<I", record, 4)[0]
    if child:
        visit(child, root)
    return result


def effect_batches(debugger):
    """Read 497070's already admitted, visibility-filtered sprite queue."""
    batch_count, sprite_count = debugger.word(0x61ba48), debugger.word(0x61ba50)
    if batch_count >= 128 or sprite_count >= 128:
        raise ValueError("Original effect queue exceeds its127-sprite bound")
    if not batch_count:
        if sprite_count:
            raise ValueError("Original effect queue has sprites without batches")
        return []
    bindings = {}
    for name, address in (("smoke", 0x4ed948), ("dust", 0x4ed94c), ("spray", 0x4ed950),
                          ("mud", 0x4ed8b0), ("gravel", 0x4ed8b4), ("grass", 0x4ed8b8), ("bang", 0x4ed8a8)):
        texture = debugger.word(address)
        material = debugger.word(texture + 0xc) if texture else 0
        if material:
            bindings[debugger.word(material + 4)] = name
    lengths = struct.unpack(f"<{batch_count}I", debugger.memory(0x61b848, batch_count * 4))
    textures = struct.unpack(f"<{batch_count}I", debugger.memory(0x614dc8, batch_count * 4))
    flags = debugger.memory(0x614fc8, (batch_count + 3) // 4 * 4)
    if sum(lengths) != sprite_count:
        raise ValueError("Original effect batch lengths disagree with its sprite count")
    packets = debugger.memory(0x615048, sprite_count * 0x44) if sprite_count else b""
    result, offset = [], 0
    for batch, (length, texture) in enumerate(zip(lengths, textures)):
        sprites = []
        for _ in range(length):
            packet = packets[offset:offset + 0x44]
            offset += 0x44
            def signed(at):
                return struct.unpack_from("<i", packet, at)[0]
            sprites.append({"center": [signed(0x30), signed(0x34), signed(0x38)],
                            "minimum": [signed(8), signed(4), signed(0)],
                            "maximum": [signed(0x2c), signed(0x1c), signed(0xc)],
                            "shade0": packet[0x3c], "shade1": packet[0x3d],
                            "transform_pointer": struct.unpack_from("<I", packet, 0x40)[0],
                            "source_packet_hex": packet.hex()})
        result.append({"texture": bindings.get(texture, "unclassified"), "surface": texture,
                       "flags": flags[batch], "sprites": sprites})
    return result


def capture_hud(debugger):
    """Read41de90's published view0 cache, without advancing RPM/timers."""
    config = debugger.word(0x4b0660)
    if debugger.word(config + 0xac) != 1:
        raise RuntimeError("Matched HUD capture requires one local viewport")
    def signed(address):
        return struct.unpack("<i", debugger.memory(address, 4))[0]
    def time_ms(actor, address):
        if not 0 <= actor < 16:
            raise RuntimeError("HUD timer actor is outside0..15")
        minutes, seconds, centiseconds = struct.unpack("<3h", debugger.memory(address + actor * 0x124, 8)[:6])
        if not (0 <= minutes < 60 and 0 <= seconds < 60 and 0 <= centiseconds < 100):
            raise RuntimeError("HUD timer cache is outside its displayed range")
        return minutes * 60000 + seconds * 1000 + centiseconds * 10
    best_actor = signed(0x4c3310)
    text = debugger.memory(0x4c2f90, 16).split(b"\0", 1)[0].decode("ascii")
    return {"speed_integer": signed(0x4c3280), "rpm": signed(0x4c33d0), "gear": signed(0x4c2fe0),
            "start_phase": debugger.word(0x4c3a7a) & 0xffff, "kilometres": bool(debugger.word(0x4c30f8)),
            "visible": bool(debugger.word(0x4c2fd0)) and bool(debugger.word(0x4f294c)) and not debugger.word(0x4c5d70),
            "qualifying": debugger.word(config + 0x5f6) & 0xffff in (1, 3),
            "lap": debugger.word(0x4c3030), "lap_limit": debugger.word(0x4c31e0),
            "lap_time_ms": time_ms(signed(0x4c30d8), 0x4cc5c8),
            "best_lap_ms": 0 if best_actor == -1 else time_ms(best_actor, 0x4cc5c2),
            "mode": debugger.word(config + 4), "position": debugger.word(0x4c3340),
            "split_active": bool(debugger.word(0x4c3070)) and not debugger.memory(0x4c39c8, 4)[0],
            "split_ticks": struct.unpack("<h", debugger.memory(0x4c3010, 4)[:2])[0], "split_text": text}


def capture_inputs(debugger, camera_node):
    config = debugger.word(0x4b0660)
    actor = debugger.word(0x4d37a0) & 255
    if actor >= 16:
        raise RuntimeError("Invalid original local actor")
    renderer = debugger.word(0x4b9b94)
    body = debugger.memory(0x4cd760 + actor * 0x53c, 0x53c)
    dynamics = debugger.memory(0x4c5da0 + actor * 0x338, 0x338)
    detail = debugger.word(config + 0x328)
    car_root = debugger.word(0x4c3968 + actor * 4)
    model_root = debugger.word((0x4c39d0 if detail >= 2 else 0x4c34f8) + actor * 4)
    lod_roots = []
    for name, table in (("high", 0x4c39d0), ("medium", 0x4c34f8), ("low", 0x4c3650)):
        address = debugger.word(table + actor * 4)
        lod_roots.append({"name": name, "address": address,
                          "visible_mask": debugger.memory(address + 0x140, 4)[0]})
    source_view = debugger.word(0x4c3a66) & 0xffff
    expected_lod = "high" if detail >= 2 else "medium"
    expected_visible = [] if source_view == 1 else [expected_lod]
    if source_view not in (1, 2, 3, 4, 5) or [root["name"] for root in lod_roots if root["visible_mask"] & 1] != expected_visible:
        raise RuntimeError("Capture view does not match its published local LOD eligibility")
    result = {"schema": 2, "exe_crc32": "8ce56d41", "source_boundary": "489069-entry-published-scene-before-draw",
            "track_id": debugger.word(config + 0x1c), "car_id": body[0x516],
            "weather": debugger.word(0x4c5d74) & 0xffff, "detail": detail,
            "horizon_index": debugger.word(0x4f2a40), "width": 640, "height": 480,
            "actor": actor, "session_state": debugger.word(config + 0x5f8) & 0xffff,
            "source_view": source_view, "lod_roots": lod_roots, "selected_lod": expected_lod,
            "source_mode": debugger.word(config + 4), "update_counter": debugger.word(0x4d383c),
            "camera_node": camera_node, "camera_world": floats(debugger, camera_node + 0x9c),
            "world_to_view": floats(debugger, renderer + 0x4300),
            "projection_q16": struct.unpack("<16i", debugger.memory(renderer + 0x4240, 64)),
            "car_matrix": floats(debugger, car_root + 0x5c),
            "steering_angle": struct.unpack_from("<h", dynamics, 0x274)[0],
            "car_visual_angles": struct.unpack_from("<2h", dynamics, 0x278),
            "wheel_angles": struct.unpack("<4h", debugger.memory(0x4d37b8 + actor * 8, 8)),
            "car_nodes": nodes(debugger, model_root), "body_hex": body.hex(),
            "configuration_hex": debugger.memory(config, 0x620).hex(),
            "scope": {"physics_replay": False, "original_state_writes": False,
                      "rival_models_imported": False, "animated_scenery_imported": False}}
    physical_position = struct.unpack_from("<3i", body, 0xbc)
    result["published_body_root_matches"] = all(struct.pack("<f", coordinate / 65536) == struct.pack("<f", result["car_matrix"][12 + axis])
                                               for axis, coordinate in enumerate(physical_position))
    if not result["published_body_root_matches"]:
        raise RuntimeError("Draw-boundary source body does not match its published car root")
    count = debugger.word(0x61e150)
    if count > 16:
        raise RuntimeError("Original shadow queue exceeds its bound")
    result["shadow_records_hex"] = debugger.memory(0x61d888, count * 136).hex() if count else ""
    count = debugger.word(0x52bc4c) if result["weather"] >= 3 else 0
    if count > 90:
        raise RuntimeError("Original precipitation pool exceeds90")
    result["weather_particles"] = [struct.unpack("<3i", debugger.memory(0x52b7f0 + index * 12, 12)) for index in range(count)]
    result["weather_streak"] = struct.unpack("<3i", debugger.memory(0x52b790, 12))
    alpha_bytes = debugger.memory(0x52b720, ((count + 3) // 4) * 4) if count else b""
    result["weather_alpha"] = list(alpha_bytes[:count])
    result["effect_batches"] = effect_batches(debugger)
    # The hardware path queues41de90 before this world draw. Its cached gauge
    # and timer inputs do not advance inside the render; the software path's
    # later41de90 breakpoint replaces this snapshot if that path is selected.
    result["hud"] = capture_hud(debugger)
    return result


def capture_window(environment, output):
    query = subprocess.run(["xdotool", "search", "--name", "^TOCA Touring Car Championship$"],
                           env=environment, check=True, text=True, capture_output=True, timeout=5)
    windows = query.stdout.splitlines()
    if len(windows) != 1:
        raise RuntimeError("Ambiguous original capture window")
    subprocess.run(["import", "-window", windows[0], "-strip", str(output)], env=environment,
                   check=True, capture_output=True, timeout=15)


def select_load_best(debugger, environment, enabled=True):
    """469979 row1, through the original options input handler."""
    config = debugger.word(0x4b0660)
    for _ in range(120):
        if debugger.word(0x52ee18) == 1:
            break
        debugger.stop_at(0x469979)
    else:
        raise RuntimeError("Original TT options transition did not complete")
    if debugger.word(0x52c21c) != 0xe or debugger.word(config + 4) != 2:
        raise RuntimeError("Load Best requires the actual Time Trial options")
    selection = (debugger.word(0x4b71e8) >> 8) & 255
    if selection not in range(3):
        raise RuntimeError("Invalid original TT options row")
    for _ in range((selection - 1) % 3):
        reference.menu_key(debugger, environment, 0x469979, "Up")
    if (debugger.word(0x4b71e8) >> 8) & 255 != 1:
        raise RuntimeError("Original TT menu did not reach Load Best")
    if debugger.word(config + 0x30) != int(enabled):
        reference.menu_key(debugger, environment, 0x469979, "Right")
    if debugger.word(config + 0x30) != int(enabled):
        raise RuntimeError("Original TT menu did not select the requested Load Best setting")
    return {"kind": "load-best-selection", "requested_load_best": enabled, "keys_only": True}


def select_view(debugger, environment, target):
    """422f60 consumes the real C press edge in active states4/6/9."""
    config = debugger.word(0x4b0660)
    if debugger.word(config + 0x5f8) & 0xffff not in (4, 6, 9):
        raise RuntimeError("Original view cycling requires an active qualifying/grid/race update")
    result = {"requested_view": target, "keys_only": True, "observed_views": [], "update_counters": []}
    for attempt in range(6):
        observed = debugger.word(0x4c3a66) & 0xffff
        result["observed_views"].append(observed)
        result["update_counters"].append(debugger.word(0x4d383c))
        if observed == target:
            return result
        if attempt == 5:
            break
        try:
            reference.menu_key(debugger, environment, 0x421aa0, "c")
        finally:
            reference.keyboard(environment, "keyup", "c")
        if debugger.word(0x4d383c) == result["update_counters"][-1]:
            raise RuntimeError("Original view selection did not advance a real physical update")
    raise RuntimeError("Original C-key cycle did not expose the requested camera")


def acquire_tt_ghost(debugger, environment, directory, limit, saved_ghost=False):
    """Drive real keys; reuse schema1 PRE/consumed/POST observations verbatim."""
    debugger.confirm_pause()
    processes = reference.owned_game_processes(environment)
    if len(processes) != 1:
        raise RuntimeError("TT driving requires one owned original process")
    debugger.enable_process_memory(processes[0])
    pointer, count, _, _ = reference.FLEET.route_descriptor(debugger.memory(0x54c120, 16))
    points = debugger.memory(pointer - 4, 4 + count * 64)
    initial = debugger.snapshot("421aa0-entry")
    if initial["source_mode"] != 2 or initial["actor"] != 0 or initial["allocated_actor_count"] != 3:
        raise RuntimeError("TT driving requires the actual player0/ghost1/ghost2 session")
    policy = reference.LocalLapPolicy(points, initial)
    initial_best = debugger.word(0x4f9f00)
    header = {"type": "header", "schema": 1, "exe_crc32": "8ce56d41", "layout": reference.LAYOUT,
              "globals": reference.GLOBALS, "session": "time-trial", "source_mode": 2, "local_actor": 0,
              "allocated_actor_count": 3, "active_ground_actor_count": 1, "scenario": "local-lap",
              "scenario_parameters": policy.parameters, "controlled_delta": None, "debugger_pauses_perturb_qpc": True,
              "boundaries": {"pre": "421aa0-entry", "consumed": "415090-entry", "post": "421aa0-return"},
              "initial_configuration": {key: initial[key] for key in ("track_id", "weather", "car_id", "transmission_mode", "steering_profile")},
              "configuration_hex": debugger.memory(initial["configuration_pointer"], 0x620).hex()}
    checksum, updates, held, consumed_break = 0, 0, set(), None
    route_seen, laps_seen = set(), set()
    with (directory / "trace.jsonl").open("w") as trace:
        def write(record):
            nonlocal checksum
            line = json.dumps(record) + "\n"
            trace.write(line)
            trace.flush()
            checksum = zlib.crc32(line.encode(), checksum)
        write(header)
        try:
            for index in range(limit):
                pre = initial if index == 0 else debugger.snapshot("421aa0-entry")
                desired, decision = policy.choose(index, pre)
                if saved_ghost:
                    desired = set()  # Original rollout supplies the saved ghost start.
                for key in sorted(held - desired):
                    reference.keyboard(environment, "keyup", key)
                for key in sorted(desired - held):
                    reference.keyboard(environment, "keydown", key)
                held = desired
                returned_at = debugger.word(stack(debugger))
                if not 0x400000 <= returned_at < 0x4b0000:
                    raise RuntimeError("TT update caller is outside the original executable")
                consumed_break = debugger.breakpoint(0x415090)
                debugger.stop_at(0x415090)
                consumed = debugger.snapshot("415090-entry")
                debugger.command(f"delete break {consumed_break}")
                consumed_break = None
                returned(debugger, returned_at)
                post = debugger.snapshot("421aa0-return")
                if consumed["update_counter"] != (pre["update_counter"] + 1) & 0xffffffff or post["update_counter"] != consumed["update_counter"]:
                    raise RuntimeError("TT recording missed an actual physical update")
                game = bytes.fromhex(post["gameplay_hex"])
                next_point, lap = struct.unpack_from("<h", game, 0xa2)[0], struct.unpack_from("<h", game, 0xa8)[0]
                route_seen.add(next_point)
                laps_seen.add(lap)
                write({"type": "update", "id": index, "pre": pre, "consumed": consumed, "post": post,
                       "requested_keys": sorted(desired), "keyboard_decision": decision})
                updates += 1
                owner, visible, best = debugger.word(0x4fa030) & 255, debugger.word(0x52b3e4), debugger.word(0x4f9f00)
                if index % 100 == 0:
                    print(json.dumps({"tt_update": updates, "lap": lap, "point": next_point, "human": decision["human"],
                                      "state": post["session_state"], "ghost_owner": owner, "ghost_visible": visible, "best_ms": best}), flush=True)
                if visible and owner == (1 if saved_ghost else 2) and (saved_ghost or 0 < best < initial_best):
                    result = {"updates": updates, "best_ms": best, "ghost_owner": owner, "ghost_visible": visible,
                              "keys_only": True, "route_points_seen": sorted(route_seen), "laps_seen": sorted(laps_seen),
                              "points_crc32": policy.parameters["points_crc32"], "trace_prefix_crc32": f"{checksum:08x}"}
                    trace.write(json.dumps({"type": "complete", "updates": updates, "prefix_crc32": f"{checksum:08x}", "tt_acquisition": result}) + "\n")
                    return result
                debugger.stop_at(0x421aa0)
            raise RuntimeError(f"Original TT did not publish the requested positive ghost in {limit} actual updates")
        finally:
            for key in sorted(held):
                reference.keyboard(environment, "keyup", key)
            if consumed_break is not None:
                debugger.command(f"delete break {consumed_break}")
            debugger.close_memory()


def acquire_damage(debugger, environment, directory, limit):
    """Reuse the established detachment acquisition policy, stopping at damage."""
    debugger.confirm_pause()
    processes = reference.owned_game_processes(environment)
    if len(processes) != 1:
        raise RuntimeError("Damage acquisition requires one owned original process")
    debugger.enable_process_memory(processes[0])
    held = set()
    try:
        initial = debugger.snapshot("421aa0-entry")
        if initial["source_mode"] != 0 or initial["allocated_actor_count"] != 16:
            raise RuntimeError("Damage acquisition requires the original sixteen-actor NoQual race")
        policy = reference.FleetDetachmentPolicy(initial)
        actor = initial["actor"]
        barrier_updates, car_contact_updates = [], []
        with (directory / "damage-driving.jsonl").open("w") as journal:
            for index in range(limit):
                pre = initial if index == 0 else debugger.snapshot("421aa0-entry")
                pre["shared"] = {"detached_count": debugger.word(0x4be220), "detached_pool_hex": debugger.memory(0x4bf8a8, 10 * 0x11c).hex()}
                desired, decision = policy.choose(index, pre)
                for key in sorted(held - desired):
                    reference.keyboard(environment, "keyup", key)
                for key in sorted(desired - held):
                    reference.keyboard(environment, "keydown", key)
                held = desired
                returned_at = debugger.word(stack(debugger))
                if not 0x400000 <= returned_at < 0x4b0000:
                    raise RuntimeError("Damage update caller is outside the original executable")
                returned(debugger, returned_at)
                post = debugger.snapshot("421aa0-return")
                if post["actor"] != actor or post["update_counter"] != (pre["update_counter"] + 1) & 0xffffffff:
                    raise RuntimeError("Damage acquisition skipped an original update or changed actor")
                # These are separate source consumers: body+529 counts car
                # contacts; the barrier cache's +30 word marks wall contact.
                barrier_active = debugger.word(0x4bdbd8 + actor * 0x34 + 0x30)
                car_contacts = bytes.fromhex(post["body_hex"])[0x529]
                if barrier_active:
                    barrier_updates.append(index)
                if car_contacts:
                    car_contact_updates.append(index)
                raw = debugger.memory(0x4c25a8, 17 * 16)
                levels = [raw[slot * 16 + actor] for slot in range(17)]
                config = post["configuration_pointer"]
                table = 0x4c26c8 if debugger.word(config + 0x328) >= 2 else 0x4c2b48
                variants = []
                for slot, level in enumerate(levels):
                    if level:
                        node = debugger.word(table + slot * 0x40 + actor * 4)
                        material = debugger.word(node + 0xc) if node else 0
                        if material:
                            variant = debugger.memory(material + 0x14, 4)[0]
                            variants.append({"damage_slot": slot, "level": level, "variant": variant, "node": node})
                entry = {"update": index, "requested_keys": sorted(desired), "decision": decision, "damage_levels": levels,
                         "active_variants": variants, "source_update_counter": post["update_counter"], "state": post["session_state"],
                         "barrier_active": barrier_active, "car_contact_count": car_contacts}
                journal.write(json.dumps(entry) + "\n")
                journal.flush()
                if index % 100 == 0:
                    print(json.dumps(entry), flush=True)
                if post["session_state"] == 6 and any(value["variant"] == value["level"] > 0 for value in variants):
                    return {"updates": index + 1, "keys_only": True, "policy": policy.parameters, "damage_levels": levels,
                            "active_variants": variants, "detached_count": debugger.word(0x4be220),
                            "barrier_updates": barrier_updates, "car_contact_updates": car_contact_updates}
                debugger.stop_at(0x421aa0)
        raise RuntimeError("Original barrier driving did not publish a positive selected damage variant within its update bound")
    finally:
        for key in sorted(held):
            reference.keyboard(environment, "keyup", key)
        debugger.close_memory()


def save_tt_best(environment, home, state, directory):
    """Exit through the original pause menu; only45e7a0 writes the .lap file."""
    files = ("doningto", "silverst", "thruxton", "brands", "oulton", "donshort", "croft", "knockhil", "snettert", "lavaland")
    saved = home / ".cache/toca-1.1a/wine/drive_c/TOCA/ttsaves" / (files[state["track_id"]] + ".lap")
    before = crc32(saved) if saved.exists() else None
    #440510 TT rows: Continue, Sound, End Time Trial. Confirmation starts No.
    for key in ("Escape", "Down", "Down", "Return", "Up", "Return"):
        reference.keyboard(environment, "keydown", key)
        time.sleep(.15)
        reference.keyboard(environment, "keyup", key)
        time.sleep(.25)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if saved.is_file() and crc32(saved) != before:
            raw = saved.read_bytes()
            if len(raw) >= 24:
                samples, milliseconds = struct.unpack_from("<2I", raw)
                end = 20 + samples * 28
                if 0 < samples <= 3600 and end + 4 <= len(raw) and raw[end] == state["track_id"] and raw[end + 1] == state["car_id"] and raw.endswith(b"\0"):
                    result = {"path": str(saved), "crc32": crc32(saved), "samples": samples, "time_ms": milliseconds,
                              "keys_only": True, "writer": "original45e7a0-after-End-Time-Trial"}
                    (directory / "original-save.json").write_text(json.dumps(result, indent=2) + "\n")
                    return result
            raise RuntimeError("Original End Time Trial produced an invalid source .lap file")
        time.sleep(.1)
    capture_window(environment, directory / "tt-exit-menu.png")
    raise RuntimeError("Original End Time Trial did not publish its new best .lap file")


def wait_session_render(debugger, count, entries):
    """A completed update can hand state4 to state6 before the next render."""
    breaks = {address: debugger.breakpoint(address) for address in entries}
    selected = None
    try:
        for _ in range(count):
            response = debugger.command("cont", 30).lower()
            observed = [address for address in entries if f"{address:08x}" in response]
            if len(observed) != 1:
                raise RuntimeError("Original did not reach one supported actual session render")
            selected = observed[0]
            debugger.confirm_pause()
    finally:
        for identifier in breaks.values():
            debugger.command(f"delete break {identifier}")
    return selected


def write_human_tt_trace(source, destination):
    """Select stable human updates without rewriting any captured state bytes."""
    raw = source.read_bytes()
    lines = raw.splitlines(keepends=True)
    header, footer = json.loads(lines[0]), json.loads(lines[-1])
    if footer.get("type") != "complete" or footer.get("prefix_crc32") != f"{zlib.crc32(b''.join(lines[:-1])):08x}":
        raise ValueError("Original TT lifecycle recording is incomplete")
    updates = [json.loads(line) for line in lines[1:-1]]
    first = next((index for index, record in enumerate(updates)
                  if all(record[phase]["transmission_mode"] == 2 and struct.unpack_from("<I", bytes.fromhex(record[phase]["gameplay_hex"]), 8)[0] == 1
                         for phase in ("pre", "consumed", "post"))), None)
    if first is None:
        return None
    initial = updates[first]["pre"]
    header.update(initial_configuration={key: initial[key] for key in header["initial_configuration"]},
                  initial_session_state=initial["session_state"], source_lifecycle_crc32=f"{zlib.crc32(raw):08x}",
                  source_record_begin=first, source_record_count=len(updates),
                  automatic_approach_update_counters=[record["pre"]["update_counter"] for record in updates[:first]])
    selected = [{**record, "id": index, "source_update_id": record["id"]} for index, record in enumerate(updates[first:])]
    prefix = b"".join((json.dumps(record) + "\n").encode() for record in (header, *selected))
    destination.write_bytes(prefix + (json.dumps({"type": "complete", "updates": len(selected), "prefix_crc32": f"{zlib.crc32(prefix):08x}"}) + "\n").encode())
    return {"path": str(destination), "source_record_begin": first, "updates": len(selected), "crc32": crc32(destination)}


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--weather", choices=tuple(reference.WEATHER_CHOICES), default="sunny")
    parser.add_argument("--session", choices=("single-race", "time-trial"), default="time-trial")
    parser.add_argument("--view", type=int, choices=(1, 2, 3, 4), default=2, help="select an actual original camera using C press/release edges")
    parser.add_argument("--no-qualifying", action="store_true", help="select the actual Single Race NoQual menu option and capture its visible grid")
    parser.add_argument("--load-best", action="store_true", help="select actual TT Load Best Yes through its menu")
    parser.add_argument("--tt-lap", action="store_true", help="drive one actual original TT best lap with SourceLapDriver keyboard inputs")
    parser.add_argument("--tt-wait-ghost", action="store_true", help="coast through the original TT rollout until the saved ghost is visible")
    parser.add_argument("--tt-save-best", action="store_true", help="after --tt-lap capture, exit through End Time Trial to save the actual original best")
    parser.add_argument("--tt-max-updates", type=int, default=6000, help="bound original TT driving to this many actual updates")
    parser.add_argument("--damage", action="store_true", help="drive real NoQual keys until a positive original damage mesh is published")
    parser.add_argument("--damage-max-updates", type=int, default=1400)
    parser.add_argument("--stage-scene-selectors", action="store_true",
                        help="explicit diagnostic staging before44a6e0: selection only, never body/camera/geometry")
    parser.add_argument("--track", type=int, choices=range(10), default=0)
    parser.add_argument("--car", type=int, choices=range(10), default=7,
                        help="selectable source car0..9, including Tank8/Flex9; model10 is a separate rival-only resource")
    parser.add_argument("--detail", type=int, choices=range(4), default=3)
    parser.add_argument("--frame", type=int, default=3, help="actual qualifying/grid scene render to capture (no state writes)")
    parser.add_argument("--timeout", type=float, help="capture deadline in seconds; default600 for a full TT lap, otherwise150")
    parser.add_argument("--artifacts", type=Path, default=PROJECT / "build/test-artifacts")
    parser.add_argument("--runner", type=Path, default=Path.home() / ".local/share/lutris/runners/wine/ge-proton/files")
    parser.add_argument("--home", type=Path, default=Path.home())
    parser.add_argument("--thread-helper", type=Path,
                        help="prebuilt reference-thread-control.exe; otherwise build it with Clang/GNU ld")
    args = parser.parse_args(argv)
    if args.timeout is None:
        args.timeout = 600 if args.tt_lap else 150
    if not 1 <= args.frame <= 65 or args.timeout <= 0:
        parser.error("--frame must be1..65 and --timeout must be positive")
    if args.no_qualifying and args.session != "single-race":
        parser.error("--no-qualifying requires --session single-race")
    if args.damage and (args.session != "single-race" or not args.no_qualifying):
        parser.error("--damage requires --session single-race --no-qualifying")
    if not 1 <= args.damage_max_updates <= 6000:
        parser.error("--damage-max-updates must be1..6000")
    if (args.load_best or args.tt_lap or args.tt_wait_ghost or args.tt_save_best) and args.session != "time-trial":
        parser.error("TT ghost acquisition options require --session time-trial")
    if args.tt_lap and args.tt_wait_ghost or args.tt_wait_ghost and not args.load_best or args.tt_save_best and not args.tt_lap:
        parser.error("TT lap/save and saved-ghost rollout options are inconsistent")
    if not 1 <= args.tt_max_updates <= 20000:
        parser.error("--tt-max-updates must be1..20000")
    if not args.stage_scene_selectors and (args.track != 0 or args.car != 7 or args.detail != 3):
        parser.error("Requested track/car/detail selectors require --stage-scene-selectors")
    return args


def finish_history_frame(debugger, state, ordinal):
    before = debugger.memory(0x4cd760 + state["actor"] * 0x53c, 0x53c)
    returned(debugger, debugger.word(stack(debugger)))
    after = debugger.memory(0x4cd760 + state["actor"] * 0x53c, 0x53c)
    if before != after:
        raise RuntimeError("Original prior presentation changed its captured body")
    state["body_unchanged_during_present"] = True
    state["history_ordinal"] = ordinal
    return state


def select_view_with_history(debugger, environment, target, families, actor_loads, livery_remaps):
    """Retain the real world presents crossed by the C-key camera transition."""
    frames = []
    original_command = debugger.command
    draw_break = debugger.breakpoint(0x489069)
    recording = False

    def command(text, timeout=15):
        nonlocal recording
        response = original_command(text, timeout)
        while text == "cont" and not recording and "00489069" in response.lower():
            if len(frames) >= 64:
                raise RuntimeError("Original camera transition exceeded64 presentation frames")
            debugger.confirm_pause()
            recording = True
            try:
                state = capture_frame(debugger, families, actor_loads, livery_remaps, current_draw=True)
                frames.append(finish_history_frame(debugger, state, len(frames)))
            finally:
                recording = False
            response = original_command("cont", timeout)
        return response

    debugger.command = command
    try:
        selection = select_view(debugger, environment, target)
    finally:
        debugger.command = original_command
        original_command(f"delete break {draw_break}")
    return selection, frames


def capture_presented_frames(debugger, families, actor_loads, livery_remaps, frame_count, render_entries):
    history = []
    for ordinal in range(frame_count):
        render_entry = wait_session_render(debugger, 1, render_entries)
        state = capture_frame(debugger, families, actor_loads, livery_remaps)
        if ordinal + 1 == frame_count:
            state["presentation_history"] = history
            return render_entry, state
        history.append(finish_history_frame(debugger, state, ordinal))
    raise ValueError("Original presentation history requires at least one frame")


def capture_skid_marks(debugger, viewport=0):
    """441d10 consumes physical ring slots, then the active44b480 queue."""
    actors.integer(viewport, 0, 3, "skid viewport")
    persistent = actors.integer(debugger.word(0x4f11c0), 0, 100, "persistent skid count")
    active = actors.integer(debugger.word(0x4edc88), 0, 99, "active skid count")
    visibility = debugger.memory(0x597e8c, 260)
    words = struct.unpack("<65I", visibility)
    disabled, override = debugger.word(0x597fa4), debugger.word(0x597fb0)
    quads = []
    for queue, address, count in ((0, 0x4edc90, persistent), (1, 0x4f11c8, active)):
        raw = debugger.memory(address, count * 56) if count else b""
        for ordinal in range(count):
            record = raw[ordinal * 56:(ordinal + 1) * 56]
            coordinates = struct.unpack_from("<12i", record)
            red, green, blue, alpha = record[48:52]
            sector = struct.unpack_from("<H", record, 52)[0]
            mask = record[54]
            if sector >= 2048 and sector != 0xffff:
                raise ValueError("Original skid sector exceeds its source visibility table")
            bit = bool(words[0] & 0x80000000) if sector == 0xffff else bool(words[1 + (sector >> 5)] & (1 << (sector & 31)))
            quads.append({"queue": queue, "ordinal": ordinal, "vertices_q16": [list(coordinates[corner * 3:corner * 3 + 3]) for corner in range(4)],
                          "color": (alpha << 24) | (red << 16) | (green << 8) | blue, "sector": sector, "visibility": mask,
                          "rendered": bool(mask & (1 << viewport)) and bool(override or bit or not disabled)})
    result = {"viewport": viewport, "persistent_count": persistent, "active_count": active, "quads": quads,
              "sector_visibility_disabled": disabled, "sector_visibility_override": override,
              "sector_visibility_prefix": words[0], "sector_visibility_hex": visibility[4:].hex()}
    return actors.skid_snapshot({"skid_marks": result})


def capture_spark_lines(debugger, viewport=0):
    """Read418920's exported queue;446f20 submits its visible rows unchanged."""
    actors.integer(viewport, 0, 3, "spark viewport")
    count = actors.integer(debugger.word(0x4c0ecc), 0, 64, "exported spark count")
    color_index = actors.integer(debugger.word(0x4ed988), 0, 2, "spark color index")
    red, green, blue, alpha = debugger.memory(0x4ed900 + color_index * 4, 4)
    color = (alpha << 24) | (red << 16) | (green << 8) | blue
    raw = debugger.memory(0x4be308, count * 28) if count else b""
    lines = []
    for ordinal in range(count):
        record = raw[ordinal * 28:(ordinal + 1) * 28]
        coordinates = struct.unpack_from("<6i", record)
        mask = record[24]
        # Actual47ca40 argument order is endpoint first, then origin.
        lines.append({"ordinal": ordinal, "from_q16": list(coordinates[3:]), "to_q16": list(coordinates[:3]),
                      "visibility": mask, "rendered": bool(mask & (1 << viewport))})
    result = {"viewport": viewport, "count": count, "color_index": color_index, "color": color, "lines": lines,
              "pool_count": debugger.word(0x4bea08), "ring_head": debugger.word(0x4be2c0), "ring_tail": debugger.word(0x4c03c4)}
    return actors.spark_snapshot({"spark_lines": result})


def capture_queued_lines(debugger):
    count = actors.integer(debugger.word(0x4e8724), 0, 17, "queued world line count")
    raw = debugger.memory(0x4de918, count * 32) if count else b""
    lines = []
    for ordinal in range(count):
        record = raw[ordinal * 32:(ordinal + 1) * 32]
        coordinates = struct.unpack_from("<6i", record)
        red, green, blue, alpha = record[28:32]
        lines.append({"ordinal": ordinal, "from_q16": list(coordinates[:3]), "to_q16": list(coordinates[3:]),
                      "color": (alpha << 24) | (red << 16) | (green << 8) | blue})
    return actors.queued_lines_snapshot({"queued_lines": {"count": count, "lines": lines}})


def capture_frame(debugger, families, actor_loads=None, livery_remaps=(), *, current_draw=False):
    config = debugger.word(0x4b0660)
    caption = {"language": debugger.word(config + 0x32c), "mode": debugger.word(config + 4), "visible": False}
    draw_break = None if current_draw else debugger.breakpoint(0x489069)
    caption_break = debugger.breakpoint(0x45ece4)
    hud_break = debugger.breakpoint(0x41de90)
    present_break = debugger.breakpoint(0x48268e)
    response = "Stopped at0x00489069" if current_draw else debugger.command("cont", 30)
    state = None
    render_serial = 0
    stops = ("00489069", "0041de90", "0045ece4") + (("00421aa0",) if current_draw else ())
    while any(address in response.lower() for address in stops):
        if "00489069" in response.lower():
            render_serial += 1
            # Retain the last actual world/HUD draw preceding the flip.
            camera_node = debugger.word(stack(debugger) + 4)
            state = capture_inputs(debugger, camera_node)
            viewport = debugger.word(stack(debugger) + 12)
            if actor_loads is not None:
                state["render_actors"] = actors.capture_snapshot(debugger, actor_loads, viewport, livery_remaps)
                local = state["render_actors"]["actors"][state["actor"]]
                state["shadow_records_hex"] = local["shadow_records_hex"]
            state["cockpit"] = actors.capture_cockpit(debugger, state)
            state["skid_marks"] = capture_skid_marks(debugger, viewport)
            state["spark_lines"] = capture_spark_lines(debugger, viewport)
            state["queued_lines"] = capture_queued_lines(debugger)
            state["session_caption"] = caption.copy()
            state["animated_scenery"] = animation.capture_snapshot(debugger, families)
            state["render_serial_before_flip"] = render_serial
        elif "0045ece4" in response.lower():
            source_text = debugger.word(stack(debugger) + 8)
            expected_text = debugger.word(0x4d5204 if caption["mode"] == 2 else 0x4d5200)
            if source_text != expected_text:
                raise RuntimeError("Original rollout draw did not use its selected localized caption")
            caption["visible"] = True
            if state is not None:
                state["session_caption"] = caption.copy()
        elif state is not None and "0041de90" in response.lower():
            state["hud"] = capture_hud(debugger)
        response = debugger.command("cont", 30)
    if "0048268e" not in response.lower() or state is None:
        raise RuntimeError(f"Missed same-frame present: {response}")
    if draw_break is not None:
        debugger.command(f"delete break {draw_break}")
    debugger.command(f"delete break {caption_break}")
    debugger.command(f"delete break {hud_break}")
    debugger.command(f"delete break {present_break}")
    return state


def main():
    args = parse_arguments()
    if crc32(PROJECT / "references/toca-1.1a/tourcars.exe") != "8ce56d41":
        raise RuntimeError("Canonical executable CRC32 differs")
    args.artifacts.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix=f"reference-render-{args.weather}-", dir=args.artifacts.resolve()))
    for child in ("home", "data", "config", "cache", "state", "runtime", "tmp", "work"):
        (directory / child).mkdir(mode=0o700)
    runtime = isolated.Runtime(directory)
    deadline = time.monotonic() + args.timeout
    previous = {sig: signal.signal(sig, isolated.interrupted) for sig in (signal.SIGTERM, signal.SIGINT)}
    debugger = None
    print(f"Artifacts: {directory}", flush=True)
    try:
        thread_helper = presentation.build_thread_helper(directory, args.runner.resolve(), args.thread_helper)
        environment = reference.original_environment(isolated.private_environment(directory), args.home.resolve(), args.runner.resolve())
        isolated.start_display(runtime, environment, "800x600x24")
        with (directory / "reference.log").open("wb") as output:
            game = runtime.launch([str(PROJECT / "tools/run-reference.sh"), "--mute"], environment, output)
        startup_deadline = min(deadline, time.monotonic() + 25)
        while time.monotonic() < startup_deadline:
            if game.poll() is not None:
                raise RuntimeError("Original launcher exited before acquiring its reference lock")
            if b"TOCA 1.1a " in (directory / "reference.log").read_bytes() and reference.owned_game_processes(environment):
                break
            time.sleep(.1)
        else:
            raise RuntimeError("Original did not establish its owned muted runtime")
        wine = str(args.runner.resolve() / "bin/wine")
        debug_exe = str(args.runner.resolve() / "lib/wine/i386-windows/winedbg.exe")
        query = subprocess.run([wine, debug_exe, "--command", "info proc"], env=environment,
                               check=True, capture_output=True, text=True, timeout=15)
        identities = re.findall(r"^\s*([0-9a-fA-F]+)\s+\d+\s+.*tourcars\.exe", query.stdout, re.M)
        if len(identities) != 1 or len(reference.owned_game_processes(environment)) != 1:
            raise RuntimeError("Cannot prove a unique owned original process")
        process = subprocess.Popen([wine, debug_exe, str(int(identities[0], 16))], env=environment,
                                   cwd=directory / "work", stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        runtime.groups.append(process)
        debugger = reference.Debugger(process, directory, deadline)
        debugger.read_prompt()
        update_break = debugger.breakpoint(0x421aa0)
        options = 0x469979 if args.session == "time-trial" else 0x469713
        weather_break = debugger.breakpoint(options)
        race_break = debugger.breakpoint(0x46e6c2)
        loader_break = debugger.breakpoint(0x44a6e0) if args.stage_scene_selectors else None
        actor_break = debugger.breakpoint(0x44dc6d)
        livery_break = debugger.breakpoint(0x49a780)
        actor_loads, livery_remaps = {}, []
        family_returns = {0x44eae4: "cones", 0x44eaee: "posts", 0x44eaf8: "signs", 0x44ecd5: "moving", 0x44ece4: "rotorcraft"}
        family_breaks = {address: debugger.breakpoint(address) for address in family_returns}
        families = {name: [] for name in animation.FAMILIES}
        debugger.send("cont")
        navigation = []
        staged = []
        startup_stops = 0
        while True:
            try:
                response = debugger.read_prompt(.7)
                if "00421aa0" in response.lower():
                    break
                family_return = next((address for address in family_breaks if f"{address:08x}" in response.lower()), None)
                if "0044dc6d" in response.lower():
                    slot, binding = actors.capture_load(debugger, stack(debugger))
                    if slot in actor_loads:
                        raise RuntimeError("Original actor was loaded more than once before the selected scene")
                    actor_loads[slot] = binding
                elif "0049a780" in response.lower():
                    livery_remaps.append(actors.capture_livery(debugger, stack(debugger)))
                elif family_return is not None:
                    name = family_returns[family_return]
                    families[name] = animation.capture_family(debugger, name)
                    debugger.command(f"delete break {family_breaks.pop(family_return)}")
                elif loader_break is not None and "0044a6e0" in response.lower():
                    config = debugger.word(0x4b0660)
                    for name, offset, value in (("track", 0x1c, args.track), ("car", 0xdc, args.car),
                                                ("detail", 0x328, args.detail), ("weather_choice", 0xa0, reference.WEATHER_CHOICES[args.weather])):
                        address = config + offset
                        before = debugger.word(address)
                        debugger.command(f"set *(unsigned int*)0x{address:08x} = {value}")
                        after = debugger.word(address)
                        if after != value:
                            raise RuntimeError(f"Original diagnostic {name} selector write did not take effect")
                        staged.append({"selector": name, "address": f"{address:08x}", "before": before, "after": after,
                                       "boundary": "44a6e0-entry-before-scene-loader", "reason": "matched-render diagnostic scene selection"})
                    debugger.command(f"delete break {loader_break}")
                    loader_break = None
                elif race_break is not None and "0046e6c2" in response.lower():
                    navigation.append(reference.select_session(debugger, environment, args.session))
                    debugger.command(f"delete break {race_break}")
                    race_break = None
                elif weather_break is not None and f"{options:08x}" in response.lower():
                    if args.no_qualifying:
                        navigation.append(reference.select_no_qualifying(debugger, environment))
                    if args.load_best or args.tt_lap:
                        navigation.append(select_load_best(debugger, environment, args.load_best))
                    navigation.append(reference.select_weather(debugger, environment, reference.WEATHER_CHOICES[args.weather], args.session))
                    debugger.command(f"delete break {weather_break}")
                    weather_break = None
                elif startup_stops < 3 and re.search(r"0x7bf3cfc5 (?:ntdll|winex11|uxtheme)\+0xcfc5:\s+retl", response):
                    #WineDbg sometimes labels the same initial ntdll return
                    #with winex11/uxtheme during module enumeration. This is the
                    #observed attach/bootstrap stop, never a game exception.
                    startup_stops += 1
                    navigation.append({"kind": "wine-attach-return", "response": response.strip()})
                else:
                    raise RuntimeError(f"Unexpected original frontend stop: {response}")
                debugger.send("cont")
            except TimeoutError:
                if time.monotonic() > deadline - 25:
                    raise RuntimeError("Original frontend did not reach scene setup")
                reference.keyboard(environment, "key", "--clearmodifiers", "--delay", "80", "Return")
        debugger.command(f"delete break {actor_break}")
        debugger.command(f"delete break {livery_break}")
        for identifier in family_breaks.values():
            debugger.command(f"delete break {identifier}")
        acquisition = acquire_tt_ghost(debugger, environment, directory, args.tt_max_updates, args.tt_wait_ghost) if args.tt_lap or args.tt_wait_ghost else None
        damage_acquisition = acquire_damage(debugger, environment, directory, args.damage_max_updates) if args.damage else None
        if acquisition:
            acquisition["human_replay_trace"] = write_human_tt_trace(directory / "trace.jsonl", directory / "trace-human.jsonl")
        debugger.confirm_pause()
        # Return from a warmup is not the next421aa0 input boundary yet.
        if (acquisition or damage_acquisition) and debugger.word(0x4c3a66) & 0xffff != args.view:
            debugger.stop_at(0x421aa0)
        view_selection, view_history = select_view_with_history(debugger, environment, args.view, families, actor_loads, livery_remaps)
        debugger.command(f"delete break {update_break}")
        config = debugger.word(0x4b0660)
        session_state = debugger.word(config + 0x5f8) & 0xffff
        render_entry = {4: 0x4480e0, 6: 0x448440, 9: 0x448a10}.get(session_state)
        if render_entry is None:
            raise RuntimeError("Original capture has no supported current session render entry")
        render_entries = (0x4480e0, 0x448440) if acquisition else (0x448a10, 0x448440) if args.no_qualifying else (render_entry,)
        render_entry, state = capture_presented_frames(debugger, families, actor_loads, livery_remaps, args.frame, render_entries)
        history = view_history + state["presentation_history"]
        if len(history) > 64:
            raise RuntimeError("Original presentation history exceeded64 frames")
        for ordinal, frame in enumerate(history):
            frame["history_ordinal"] = ordinal
        state["presentation_history"] = history
        expected_weather = 1 if args.stage_scene_selectors and args.track == 9 else reference.WEATHER_CHOICES[args.weather]
        if state["session_state"] not in ((4, 6) if acquisition else (9, 6) if args.no_qualifying else (4,)) or state["weather"] != expected_weather:
            raise RuntimeError("Original capture did not match the requested qualifying/grid weather")
        if args.stage_scene_selectors and (loader_break is not None or state["track_id"] != args.track or state["car_id"] != args.car or state["detail"] != args.detail):
            raise RuntimeError("Original scene loader did not consume the staged selectors")
        state.update(navigation=navigation, display=environment["DISPLAY"], frame=args.frame,
                     staged_scene_selectors=staged, source_render_entry=render_entry, view_selection=view_selection)
        if state["source_view"] != args.view:
            raise RuntimeError("Original draw did not retain the requested actual camera")
        if acquisition:
            state["tt_acquisition"] = acquisition
        if damage_acquisition:
            state["damage_acquisition"] = damage_acquisition
        state["scope"]["original_state_writes"] = bool(staged)
        state["scope"]["normal_original_unlock_flow"] = not bool(staged)
        present_body = debugger.memory(0x4cd760 + state["actor"] * 0x53c, 0x53c).hex()
        state["live_body_before_present_hex"] = present_body
        returned(debugger, debugger.word(stack(debugger)))
        state["body_after_present_hex"] = debugger.memory(0x4cd760 + state["actor"] * 0x53c, 0x53c).hex()
        processes = reference.owned_game_processes(environment)
        if len(processes) != 1:
            raise RuntimeError("Original presentation lost its unique owned process")
        presentation.capture_completed_window(debugger, environment, args.runner.resolve(), thread_helper,
                                              int(identities[0], 16), processes[0], state, directory, capture_window)
        state["original_png_crc32"] = crc32(directory / "original.png")
        state["body_unchanged_during_present"] = present_body == state["body_after_present_hex"]
        (directory / "state.json").write_text(json.dumps(state, indent=2) + "\n")
        (directory / "manifest.json").write_text(json.dumps({"state_crc32": crc32(directory / "state.json"),
                                                               "original_png_crc32": state["original_png_crc32"]}, indent=2) + "\n")
        print(json.dumps({"directory": str(directory), "track": state["track_id"], "car": state["car_id"],
                          "weather": state["weather"], "body_unchanged": state["body_unchanged_during_present"],
                          "state_crc32": crc32(directory / "state.json")}), flush=True)
        if args.tt_save_best:
            print(json.dumps({"original_tt_save": save_tt_best(environment, args.home.resolve(), state, directory)}), flush=True)
    finally:
        if debugger is not None:
            debugger.close_memory()
            debugger.log.close()
            debugger.commands.close()
        runtime.cleanup()
        for sig, handler in previous.items():
            signal.signal(sig, handler)


if __name__ == "__main__":
    main()
