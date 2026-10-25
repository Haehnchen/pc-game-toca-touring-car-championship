"""TOCA 1.1a fleet trace schema and observed coverage (no process writes)."""

import json
import struct
import zlib
from pathlib import Path

ACTORS = 16
BOUNDARIES = {"pre": "421aa0-entry", "consumed": "415090-entry",
              "contacts": "421980-entry", "ground": "446a80-entry", "post": "421aa0-return"}
LAYOUT = {"body_base": 0x4CD760, "body_stride": 0x53C,
          "dynamics_base": 0x4C5DA0, "dynamics_stride": 0x338,
          "gameplay_base": 0x4CC518, "gameplay_stride": 0x124}
# Fixed raw blocks are provenance and future import inputs, not implicitly
# imported into a continuous replay on every record.
BLOCKS = {"pair_matrix": (0x4D3F28, 0x800), "ownership": (0x4C5D58, 16),
          "barriers": (0x4BDBD8, 16 * 0x34), "damage_scratch": (0x4C26B8, 16),
          "damage_levels": (0x4C25A8, 17 * 16), "broken_features": (0x4DB8C8, 16 * 16),
          "detached_pool": (0x4BF8A8, 10 * 0x11C), "detached_free": (0x4BE2C8, 0x28),
          "limiter_active": (0x4BE150, 16 * 4), "limiter_load": (0x4BE108, 16 * 4),
          "sector_visibility": (0x597E90, 0x100)}
WORDS = {"damage_cycle": 0x4C2F8C, "detached_count": 0x4BE220,
         "carcontacts_enabled": 0x4ADF48, "sector_visibility_disabled": 0x597FA4,
         "sector_visibility_override": 0x597FB0, "barrier_records_pointer": 0x54C12C}
PROPS = {"cones": (0x4F2A50, 0x4C4570, 0xCC), "posts": (0x4F2A54, 0x4C9150, 0x13C),
         "signs": (0x4F2A58, 0x4CB658, 0x40), "flags": (0x4F2A5C, 0x4D2B20, 0x140)}

PROVENANCE_BLOCKS = {"world_status": (0x4C5D58, 0x30), "local_launch_tires": (0x4C5D80, 0x10),
                     "ai_globals": (0x4C3B68, 0x24), "noqual_flags": (0x4BD934, 0x10)}
LIFECYCLE_BLOCKS = {"race_lifecycle": (0x4D3838, 0x14), "player_lifecycle": (0x4C3A64, 0x18),
                    "session_timers": (0x4ED998, 0x14), "lifecycle_clock": (0x4EDA6C, 4)}
LAUNCH_FLAGS = {"begin_before_pre": 1, "begin_inside_pre": 2, "decay": 4,
                "clutch_release_before_pre": 8, "clutch_release_inside_pre": 16,
                "release_epoch_observed": 32, "reset_before_pre": 64, "reset_inside_pre": 128}
LAUNCH_BYTES = 32
SELECTION_BYTES = 24 + ACTORS * 36
REPLAY_MODES = ("independent-physics", "continuous-physics-inputs", "continuous-derived-body-selection")
AI_BOUNDARIES = {"ai_pre": "403dd0-entry", "ai_post": "403dd0-return"}
AI_STATE_BLOCKS = {"release_counter": (0x4D37B0, 4), "resolved_ai": (0x4C3B90, 0x540),
                   "route_descriptor": (0x54C120, 16), "startline_end": (0x4BD5C4, 4), "startline_start": (0x4BCDE4, 4)}
AI_STATE_MANIFEST = {"blocks": {name: list(value) for name, value in AI_STATE_BLOCKS.items()},
                     "primary_table": 0x4BD790, "alternate_table": 0x4BD5D8,
                     "point_stride": 64, "max_points": 256, "startline_vectors": 33}
CHECKPOINT_TIMING_BLOCKS = {"split_table": (0x4CC2D8, 0x240), "race_flags": (0x4DCDB0, 4)}
CHECKPOINT_TIMING_MANIFEST = {"blocks": {name: list(value) for name, value in CHECKPOINT_TIMING_BLOCKS.items()},
                              "session_stage_offset": 0x5f6, "split_stride": 12, "split_count": 48}
FINISH_TIMING_BLOCKS = {"race_counters": (0x4D3838, 16), "finish_counts": (0x4D3854, 8),
                        "local_finish_flags": (0x4C39C8, 4), "finish_order": (0x4C9140, 16),
                        "pending_state": (0x4AF020, 4), "race_status": (0x4C5D68, 4),
                        "display_ticks": (0x4C3010, 8), "display_mode": (0x4C3030, 16),
                        "display_leader": (0x4C3058, 16), "display_active": (0x4C3070, 16),
                        "display_minutes": (0x4C33B8, 8), "display_seconds": (0x4C3458, 8),
                        "display_hundredths": (0x4C33E0, 8),
                        "reference_0": (0x4C31F8, 16), "reference_1": (0x4C3398, 16), "reference_2": (0x4C3388, 16),
                        "current_0": (0x4C30A8, 16), "current_1": (0x4C3088, 16), "current_2": (0x4C3098, 16),
                        "display": (0x4C2F90, 64), "leader_display": (0x4C32D0, 64),
                        "persisted_0": (0x4FA020, 4), "persisted_1": (0x4FA00C, 4), "persisted_2": (0x4FA010, 4)}
FINISH_TIMING_MANIFEST = {"blocks": {name: list(value) for name, value in FINISH_TIMING_BLOCKS.items()},
                         "display_players": 4, "string_stride": 16}

TRAFFIC_BOUNDARIES = {"traffic_pre": "404c1a-entry"}
TRAFFIC_CONTROL_BLOCKS = {"weather_penalty": (0x4bd788, 4), "tuning": (0x4bd5b4, 32),
                          "status": (0x4bd934, 24), "headlight_gate_0": (0x4f2860, 4),
                          "headlight_gate_1": (0x4f2978, 4), "headlight_gate_2": (0x4f29e8, 4),
                          "brake_bypass_pointer": (0x54b948, 4), "braking_memory": (0x4bcce0, 1212)}
TRAFFIC_CONTROL_MANIFEST = {"blocks": {name: list(value) for name, value in TRAFFIC_CONTROL_BLOCKS.items()},
                            "configuration_force_acceleration_offset": 0x58,
                            "brake_bypass_stride": 1, "max_brake_bypass_count": 256,
                            "scope": "post-route ascending traffic and descending AI control axes"}

JOINED_AI_MANIFEST = {"cumulative_route": 0x4bd1a8, "cumulative_stride": 4, "rank_threshold": [0x4bccd0, 4],
                      "grid_initialization": 0x460170, "release_entry": 0x448130, "release_returns": [0x4220a6],
                      "update_callers": {"4488a1": 9, "4488d0": 9, "448434": 6},
                      "scope": "one403dd0-entry world/RNG through derived route, ranks, traffic and descending controls"}

# Optional evidence at the actual460170 call. These addresses are fixed;
# descriptor/root/bypass pointer bytes are never followed at entry or return.
COLD_GRID_INIT_BLOCKS = {
    **PROVENANCE_BLOCKS,
    "status": (0x4bd934, 24), "local_actor_slots": (0x4d37a0, 4),
    "player_lifecycle": (0x4c3a64, 0x18), "watch_demo": (0x4c3a58, 4),
    "resolved_ai": (0x4c3b90, 0x540), "route_descriptor": (0x54c120, 16),
    "startline_start": (0x4bcde4, 4), "startline_end": (0x4bd5c4, 4),
    "startline_primary": (0x4bd790, 33 * 12), "startline_alternate": (0x4bd5d8, 33 * 12),
    "braking_memory": (0x4bcce0, 1212), "rank_threshold": (0x4bccd0, 4),
    "cumulative_route": (0x4bd1a8, 256 * 4), "brake_bypass_pointer": (0x54b948, 4),
    "full_body_budget": (0x4ad880, 2), "dispatch_reset": (0x4be218, 4),
    "pair_matrix": (0x4d3f28, 0x800), "clock_first_sample": (0x4ad0f0, 4),
    "qpc_divisor": (0x4be148, 4), "race_lifecycle": (0x4d3838, 0x14), "rng": (0x4b9e78, 4),
}
COLD_GRID_INIT_MANIFEST = {
    "schema": "toca.cold-grid-boundaries.v1", "entry": 0x460170,
    "configuration_pointer": 0x4b0660, "configuration_bytes": 0x620,
    "actor_count": ACTORS, "layout": LAYOUT,
    "blocks": {name: list(value) for name, value in COLD_GRID_INIT_BLOCKS.items()},
    "scope": "actual460170 entry/return including its transitive frame/reset writers; evidence only, no initializer or replay inputs",
    "pointer_policy": "bounded CONFIG read only; actor roots, route descriptor and bypass pointer remain opaque bytes",
    "cumulative_route_policy": "fixed256-word storage, including unused tail; live count remains in opaque route descriptor",
}


COLD_GRID_SEQUENCE_MANIFEST = {
    "schema": "toca.cold-grid-sequence.v1", "entry": 0x460170, "outer_entry": 0x447980,
    "outer_return_addresses": [0x44fcff, 0x4487d2], "initializer_return_address": 0x4479e5,
    "endpoint": "421aa0-entry", "endpoint_caller_return_address": 0x4488a1,
    "boundary_manifest": COLD_GRID_INIT_MANIFEST,
    "scope": "every actual460170 entry/return from first loader call through first421aa0; fixed raw evidence only",
}

COLD_TIMER_CREATION_MANIFEST = {
    "entry": 0x447a70, "sample_pc": 0x447a77, "caller_return_address": 0x44a470,
    "after_initializer": 0, "before_initializer": 1,
    "scope": "observed4a0580 return between loader and state9 initializers; creation clock input, timer bank outputs remain expectations",
}


def validate_cold_timer_creation(event):
    fields = {name for name in COLD_TIMER_CREATION_MANIFEST if name != "scope"}
    if not isinstance(event, dict) or set(event) != fields | {"creation_clock"}:
        raise ValueError("Incomplete cold timer creation provenance")
    for name in fields:
        if integer(event[name], 0, 0xffffffff, "cold timer creation " + name) != COLD_TIMER_CREATION_MANIFEST[name]:
            raise ValueError("Unexpected cold timer creation caller or initializer order")
    integer(event["creation_clock"], 0, 0xffffffff, "observed timer creation clock")
    return event


def cold_timer_creation(header):
    event = header.get("cold_grid_sequence", {}).get("timer_creation")
    manifest = header.get("cold_timer_creation_manifest")
    if event is None and manifest is None:
        return None
    if manifest != COLD_TIMER_CREATION_MANIFEST or header.get("cold_grid_sequence_manifest") != COLD_GRID_SEQUENCE_MANIFEST:
        raise ValueError("Unknown cold timer creation manifest or missing cold sequence")
    return validate_cold_timer_creation(event)


def cold_grid_boundary_bytes(snapshot):
    """Validate fixed byte extents without interpreting stale entry actor data."""
    data = {"configuration": raw_hex(snapshot["configuration_hex"], 0x620, "cold-grid CONFIG")}
    actors = snapshot["actors"]
    if not isinstance(actors, list) or len(actors) != ACTORS:
        raise ValueError("Cold-grid evidence requires all sixteen fixed actor slots")
    for name in ("body", "dynamics", "gameplay"):
        for actor, entry in enumerate(actors):
            if entry["actor"] != actor or type(entry["actor"]) is not int:
                raise ValueError("Reordered cold-grid actor slots")
        data[name] = b"".join(raw_hex(entry[name + "_hex"], LAYOUT[name + "_stride"], "cold-grid " + name) for entry in actors)
    globals_ = snapshot["globals"]
    if set(globals_) != {name + "_hex" for name in COLD_GRID_INIT_BLOCKS}:
        raise ValueError("Incomplete cold-grid fixed globals")
    data.update({name: raw_hex(globals_[name + "_hex"], size, "cold-grid " + name)
                 for name, (_, size) in COLD_GRID_INIT_BLOCKS.items()})
    return data


def _capture_startup_boundary(reader, boundary):
    """Read no process-owned pointers except the validated CONFIG object."""
    reader.preload([(0x4b0660, 4)])
    config = integer(reader.word(0x4b0660), 0x10000, 0xffffffff - 0x620, "cold-grid CONFIG pointer")
    if config & 3: raise ValueError("Unaligned cold-grid CONFIG pointer")
    reads = [(config, 0x620), *COLD_GRID_INIT_BLOCKS.values(),
             *((LAYOUT[name + "_base"], ACTORS * LAYOUT[name + "_stride"]) for name in ("body", "dynamics", "gameplay"))]
    #The debugger cache accepts whole aligned words, including the budget's
    #two-byte field. Slice the exact original extent below for provenance/CRC.
    reader.preload([(address & ~3, ((address + size + 3) & ~3) - (address & ~3)) for address, size in reads])
    blocks = {name: reader.memory(LAYOUT[name + "_base"], ACTORS * LAYOUT[name + "_stride"])
              for name in ("body", "dynamics", "gameplay")}
    snapshot = {"boundary": boundary, "configuration_pointer": config, "configuration_hex": reader.memory(config, 0x620).hex(),
                "actors": [{"actor": actor, **{name + "_hex": raw[actor * LAYOUT[name + "_stride"]:(actor + 1) * LAYOUT[name + "_stride"]].hex()
                                               for name, raw in blocks.items()}} for actor in range(ACTORS)],
                "globals": {name + "_hex": reader.memory(address, size).hex() for name, (address, size) in COLD_GRID_INIT_BLOCKS.items()}}
    snapshot["crc32"] = {name: f"{zlib.crc32(raw):08x}" for name, raw in cold_grid_boundary_bytes(snapshot).items()}
    return snapshot


def capture_cold_grid_boundary(reader, boundary):
    if boundary not in ("460170-entry", "460170-return", "421aa0-entry"):
        raise ValueError("Cold-grid evidence requires an actual460170 boundary")
    return _capture_startup_boundary(reader, boundary)


QUALIFYING_STARTUP_MANIFEST = {"entry": 0x443f60, "returns": [0x44fd4f, 0x447f8b],
    "timer_entry": 0x447a70, "timer_sample": 0x447a77, "timer_return": 0x44a470,
    "endpoint": 0x421aa0, "endpoint_return": 0x4480ba,
    "boundary_manifest": {**COLD_GRID_INIT_MANIFEST, "schema": "toca.qualifying-startup-boundaries.v1", "entry": 0x443f60,
        "scope": "actual443f60 entry/return and first State4 PRE; fixed raw expectations, no physical updates"},
    "scope": "two actual443f60 calls, the intervening creator clock and first State4 PRE; zero physical updates"}
QUALIFYING_STARTUP_BLOCKS = {"timer_origins": (0x4ed998, 20), "cached_clock": (0x4eda6c, 4),
    "delta_q16": (0x4ad080, 4), "reciprocal_q16": (0x4ad084, 4), "extra_update": (0x4be19c, 4),
    "camera_heading": (0x52b3f2, 2), "release_counter": (0x4d37b0, 4)}
QUALIFYING_STARTUP_MANIFEST["qualifying_blocks"] = {name: list(value) for name, value in QUALIFYING_STARTUP_BLOCKS.items()}
QUALIFYING_STARTUP_BINARY = struct.Struct("<8s9I16h10s2Ii")


def capture_qualifying_startup_boundary(reader, boundary):
    if boundary not in ("443f60-entry", "443f60-return", "421aa0-entry"):
        raise ValueError("Qualifying startup requires an actual initializer/PRE boundary")
    snapshot = _capture_startup_boundary(reader, boundary)
    reader.preload([(at & ~3, ((at + size + 3) & ~3) - (at & ~3)) for at, size in QUALIFYING_STARTUP_BLOCKS.values()])
    snapshot["qualifying_globals"] = {name + "_hex": reader.memory(at, size).hex()
        for name, (at, size) in QUALIFYING_STARTUP_BLOCKS.items()}
    snapshot["qualifying_crc32"] = {name: f"{zlib.crc32(bytes.fromhex(raw)):08x}"
        for name, raw in snapshot["qualifying_globals"].items()}
    return snapshot


def qualifying_startup_bytes(snapshot):
    data = cold_grid_boundary_bytes(snapshot)
    if snapshot.get("crc32") != {name: f"{zlib.crc32(raw):08x}" for name, raw in data.items()}:
        raise ValueError("Qualifying startup boundary CRC32 mismatch")
    extra = snapshot.get("qualifying_globals", {})
    if set(extra) != {name + "_hex" for name in QUALIFYING_STARTUP_BLOCKS}:
        raise ValueError("Incomplete qualifying startup clock/heading provenance")
    if snapshot.get("qualifying_crc32") != {name: f"{zlib.crc32(bytes.fromhex(raw)):08x}" for name, raw in extra.items()}:
        raise ValueError("Qualifying startup clock/heading CRC32 mismatch")
    data.update({name: raw_hex(extra[name + "_hex"], size, "qualifying startup " + name)
        for name, (_, size) in QUALIFYING_STARTUP_BLOCKS.items()})
    config = data["configuration"]
    if any(struct.unpack_from("<I", config, at)[0] != value for at, value in ((4, 0), (0x20, 0), (0xac, 1))):
        raise ValueError("Qualifying startup requires ordinary one-player QualOn CONFIG")
    return data


def validate_qualifying_startup(capture):
    header, events = capture["header"], capture["events"]
    if (header.get("schema") != 1 or header.get("scope") != "qualifying-startup" or header.get("exe_crc32") != "8ce56d41"
            or header.get("manifest") != QUALIFYING_STARTUP_MANIFEST or len(events) != 4):
        raise ValueError("Unsupported complete qualifying startup provenance")
    if [event.get("kind") for event in events] != ["initializer", "timer_creation", "initializer", "first_pre"]:
        raise ValueError("Qualifying startup caller chronology is incomplete or reordered")
    for ordinal, event in enumerate((events[0], events[2])):
        if event.get("ordinal") != ordinal or event.get("return_address") != QUALIFYING_STARTUP_MANIFEST["returns"][ordinal]:
            raise ValueError("Unexpected actual qualifying initializer caller")
        for phase, boundary in (("pre", "443f60-entry"), ("post", "443f60-return")):
            if event[phase].get("boundary") != boundary:
                raise ValueError("Unexpected qualifying initializer boundary")
            data = qualifying_startup_bytes(event[phase])
            if any(struct.unpack_from("<h", bytes.fromhex(actor["gameplay_hex"]), 0xbc)[0] != actor["actor"]
                    for actor in event[phase]["actors"]):
                raise ValueError("Cold qualifying requires loader460070 actorIndex gameplay+bc bindings")
    creator = events[1]
    if creator.get("return_address") != QUALIFYING_STARTUP_MANIFEST["timer_return"]:
        raise ValueError("Unexpected qualifying timer creator caller")
    integer(creator.get("creation_clock"), 0, 0xffffffff, "qualifying creation clock")
    endpoint = events[3]
    if endpoint.get("return_address") != QUALIFYING_STARTUP_MANIFEST["endpoint_return"] or endpoint["snapshot"].get("boundary") != "421aa0-entry":
        raise ValueError("Qualifying startup must end at the first actual State4 physical PRE")
    data = qualifying_startup_bytes(endpoint["snapshot"])
    if (struct.unpack_from("<I", data["race_lifecycle"], 4)[0] != 0
            or struct.unpack_from("<H", data["configuration"], 0x5f8)[0] != 4
            or tuple(struct.unpack("<i", data[name])[0] for name in ("delta_q16", "reciprocal_q16", "extra_update")) != (65536, 65536, 0)):
        raise ValueError("Qualifying first PRE must retain the untouched cold step and zero physical counter")
    return capture


def write_qualifying_startup_capture(path, header, events):
    prefix = b"".join((json.dumps(record) + "\n").encode() for record in (header, *events))
    path = Path(path)
    path.write_bytes(prefix)
    footer = {"type": "complete-init", "updates": 0, "events": len(events), "prefix_crc32": f"{zlib.crc32(prefix):08x}"}
    try:
        validate_qualifying_startup({"header": header, "events": events})
    except Exception as error:
        footer.update(type="aborted-init", reason=str(error), error_type=type(error).__name__)
        with path.open("ab") as output:
            output.write((json.dumps(footer) + "\n").encode())
        raise
    with path.open("ab") as output:
        output.write((json.dumps(footer) + "\n").encode())
    return footer


def read_qualifying_startup(path):
    lines = Path(path).read_bytes().splitlines(keepends=True)
    if len(lines) != 6 or any(not line.endswith(b"\n") for line in lines):
        raise ValueError("Qualifying startup recording is incomplete")
    records = [json.loads(line) for line in lines]
    footer = records[-1]
    if (footer.get("type") != "complete-init" or footer.get("updates") != 0 or footer.get("events") != 4
            or footer.get("prefix_crc32") != f"{zlib.crc32(b''.join(lines[:-1])):08x}"):
        raise ValueError("Qualifying startup completion/CRC32 mismatch")
    return validate_qualifying_startup({"header": records[0], "events": records[1:-1], "footer": footer})


def qualifying_startup_inputs(capture):
    validate_qualifying_startup(capture)
    events = capture["events"]
    entry, endpoint = (qualifying_startup_bytes(snapshot) for snapshot in (events[0]["pre"], events[3]["snapshot"]))
    config = entry["configuration"]
    word = lambda at: struct.unpack_from("<I", config, at)[0]
    if any(word(at) for at in (8, 0x48, 0x58, 0x5c, 0x7c, 0x88, 0x94)):
        raise ValueError("Qualifying startup excludes alternate modes/camera/physics cheats")
    name = config[0xb0:0xc0].split(b"\0", 1)[0]
    if not 1 <= len(name) <= 9: raise ValueError("Qualifying player name must contain1..9 bytes")
    car = integer(word(0xdc), 0, 7, "qualifying car")
    if entry["local_actor_slots"][0] != car * 2:
        raise ValueError("Qualifying startup local binding disagrees with the selected car")
    transmission = integer(word(0xe0), 1, 2, "qualifying transmission")
    return {"track": integer(word(0x1c), 0, 8, "qualifying track"), "car": car,
        "weather": integer(struct.unpack_from("<H", entry["world_status"], 0x1c)[0], 1, 5, "qualifying weather"),
        "difficulty": integer(word(0x34), 0, 2, "qualifying difficulty"), "manual": int(transmission == 1),
        "steering": integer(word(0x100), 1, 3, "qualifying steering"), "seed": struct.unpack("<I", entry["rng"])[0],
        "entry_points": struct.unpack_from("<16h", config, 0x5fe), "player_name": name,
        "creation_clock": events[1]["creation_clock"], "first_caller_clock": struct.unpack("<I", endpoint["cached_clock"])[0],
        "divisor": integer(struct.unpack("<i", endpoint["qpc_divisor"])[0], 1, 0x7fffffff, "observed qualifying QPF divisor")}


def write_qualifying_startup_binary(path, capture):
    inputs = qualifying_startup_inputs(capture)
    Path(path).write_bytes(QUALIFYING_STARTUP_BINARY.pack(b"TOCAGRD1", 2, 0x443f60,
        *(inputs[name] for name in ("track", "car", "weather", "difficulty", "manual", "steering", "seed")),
        *inputs["entry_points"], inputs["player_name"].ljust(10, b"\0"),
        inputs["creation_clock"], inputs["first_caller_clock"], inputs["divisor"]))


def qualifying_startup_expected(capture):
    inputs = qualifying_startup_inputs(capture)
    events = capture["events"]
    for phase, snapshot in (("first_return", events[0]["post"]), ("second_return", events[2]["post"]), ("first_pre", events[3]["snapshot"])):
        data = qualifying_startup_bytes(snapshot)
        def row(group, actor, field, value): return {"phase": phase, "group": group, "actor": actor, "field": field, "value": value}
        for actor, observed in enumerate(snapshot["actors"]):
            for group, field, block, offset, form in COLD_GRID_ACTOR_FIELDS:
                if field == "queued_shift" and actor != inputs["car"] * 2: continue
                yield row(group, actor, field, struct.unpack_from("<" + form, bytes.fromhex(observed[block + "_hex"]), offset)[0])
            yield row("identity", actor, "ownership", struct.unpack_from("<b", data["world_status"], actor)[0])
            body = bytes.fromhex(observed["body_hex"])
            yield row("qualifying_motion", actor, "speed", struct.unpack_from("<i", body, 0xe0)[0])
            for axis, value in zip("xyz", struct.unpack_from("<3i", body, 0xc8)):
                yield row("qualifying_motion", actor, "velocity_" + axis, value)
        game = bytes.fromhex(snapshot["actors"][inputs["car"] * 2]["gameplay_hex"])
        for index, value in enumerate(struct.unpack_from("<4I", game, 0x60)):
            yield row("local_controller", inputs["car"] * 2, "saved_mode_" + str(index), value)
        for field, value in (("full_body_budget", struct.unpack("<H", data["full_body_budget"])[0]),
                ("dispatch_reset", struct.unpack("<I", data["dispatch_reset"])[0]),
                ("release_counter", struct.unpack("<I", data["release_counter"])[0])):
            yield row("reset", -1, field, value)
        for index, value in enumerate(struct.unpack_from("<4I", data["status"])): yield row("reset", -1, "status_" + str(index), value)
        for index, offset in enumerate((0xc, 0x10)): yield row("reset", -1, "ai_setup_reset_" + str(index), struct.unpack_from("<I", data["ai_globals"], offset)[0])
        for pair in range(256):
            yield row("pair_reset", pair, "handle", struct.unpack_from("<i", data["pair_matrix"], pair * 8)[0])
            yield row("pair_reset", pair, "touching", struct.unpack_from("<I", data["pair_matrix"], pair * 8 + 4)[0])
        yield row("clock_contract", -1, "first_sample", struct.unpack("<I", data["clock_first_sample"])[0])
        yield row("rng", -1, "final_seed", struct.unpack("<I", data["rng"])[0])
        yield row("camera", -1, "heading", struct.unpack("<h", data["camera_heading"])[0])
        if phase == "first_pre":
            for index, value in enumerate(struct.unpack("<5I", data["timer_origins"])): yield row("clock", -1, "timer" + str(index) + "_origin", value)
            for field in ("cached_clock", "qpc_divisor", "delta_q16", "reciprocal_q16", "extra_update"):
                yield row("clock", -1, field, struct.unpack("<I", data[field])[0])


def compare_qualifying_startup(capture, path):
    import csv
    count, mismatches = 0, []
    with Path(path).open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != ["phase", "group", "actor", "field", "value"]: raise ValueError("Invalid qualifying startup CSV header")
        for wanted in qualifying_startup_expected(capture):
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing qualifying startup result")
            if tuple(actual[name] for name in ("phase", "group", "field")) != tuple(wanted[name] for name in ("phase", "group", "field")) or int(actual["actor"]) != wanted["actor"]:
                raise ValueError("Reordered qualifying startup result")
            count += 1
            if int(actual["value"]) != wanted["value"]: mismatches.append({**wanted, "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra qualifying startup result")
    return {"parity": not mismatches, "numerical_parity": not mismatches, "compared_values": count,
        "mismatch_count": len(mismatches), "first_mismatches": mismatches[:64], "physical_updates": 0,
        "scope": "Cold443f60 initialization and first State4 PRE; no physical trajectory or source scene comparison",
        "continuous_physics_parity": None, "autonomous_qualifying_parity": None}


def validate_cold_grid_initialization(evidence, legacy):
    """Integrity/scope only; return bytes never enter replay initialization."""
    validate_start_call(legacy, 0x460170)
    if set(evidence) != {"pre", "post"}:
        raise ValueError("Incomplete cold-grid entry/return evidence")
    for phase in ("pre", "post"):
        snapshot, scalar = evidence[phase], legacy[phase]
        if snapshot["boundary"] != scalar["boundary"]:
            raise ValueError("Unexpected cold-grid callee boundary")
        pointer = integer(snapshot["configuration_pointer"], 0x10000, 0xffffffff - 0x620, "cold-grid CONFIG pointer")
        if pointer & 3: raise ValueError("Unaligned cold-grid CONFIG pointer")
        data = cold_grid_boundary_bytes(snapshot)
        expected = {name: f"{zlib.crc32(raw):08x}" for name, raw in data.items()}
        if snapshot["crc32"] != expected:
            raise ValueError("Cold-grid boundary CRC32 mismatch")
        config = data["configuration"]
        if (struct.unpack_from("<I", config, 4)[0], struct.unpack_from("<I", config, 0x20)[0], struct.unpack_from("<I", config, 0xac)[0]) != (0, 1, 1):
            raise ValueError("Cold-grid evidence requires ordinary one-player NoQual CONFIG")
        if (scalar["source_mode"], scalar["local_players"], scalar["session_stage"]) != (0, 1, struct.unpack_from("<H", config, 0x5f6)[0]):
            raise ValueError("Cold-grid CONFIG disagrees with original start-call scalars")
        if (struct.unpack_from("<H", data["world_status"], 0x16)[0] != ACTORS
                or struct.unpack_from("<I", data["world_status"], 0x18)[0] != 0
                or data["local_actor_slots"][0] >= ACTORS or struct.unpack("<I", data["watch_demo"])[0] != 0):
            raise ValueError("Cold-grid evidence requires sixteen allocated actors and an ordinary local binding")
        if struct.unpack("<I", data["rng"])[0] != scalar["rng"] or struct.unpack_from("<I", data["race_lifecycle"], 8)[0] != scalar["race_clock"]:
            raise ValueError("Cold-grid fixed RNG/clock disagrees with original start-call scalars")


def validate_cold_grid_sequence(sequence, legacy, first_evidence, initial=None):
    fields = {"events", "first_pre"}
    if set(sequence) not in (fields, fields | {"timer_creation"}) or len(sequence["events"]) != 2:
        raise ValueError("Cold-grid sequence requires exactly two actual initializer calls before first PRE")
    for ordinal, event in enumerate(sequence["events"]):
        if (set(event) != {"ordinal", "outer_entry", "outer_return_address", "initialization", "boundaries"}
                or event["ordinal"] != ordinal or event["outer_entry"] != 0x447980
                or event["outer_return_address"] != COLD_GRID_SEQUENCE_MANIFEST["outer_return_addresses"][ordinal]
                or event["initialization"]["return_address"] != 0x4479e5):
            raise ValueError("Cold-grid sequence actual caller/ordinal mismatch")
        validate_cold_grid_initialization(event["boundaries"], event["initialization"])
    if "timer_creation" in sequence:
        validate_cold_timer_creation(sequence["timer_creation"])
    if sequence["events"][0]["initialization"] != legacy or sequence["events"][0]["boundaries"] != first_evidence:
        raise ValueError("Cold-grid sequence changed legacy first-call evidence")
    endpoint = sequence["first_pre"]
    if (set(endpoint) != {"caller_return_address", "snapshot"} or endpoint["caller_return_address"] != 0x4488a1
            or endpoint["snapshot"]["boundary"] != "421aa0-entry"):
        raise ValueError("Cold-grid sequence requires actual first421aa0 caller")
    snapshot = endpoint["snapshot"]
    data = cold_grid_boundary_bytes(snapshot)
    if snapshot["crc32"] != {name: f"{zlib.crc32(raw):08x}" for name, raw in data.items()}:
        raise ValueError("Cold-grid sequence first-PRE CRC32 mismatch")
    if struct.unpack_from("<I", data["race_lifecycle"], 4)[0] != 0:
        raise ValueError("Cold-grid sequence endpoint is not the first update")
    if initial is not None:
        if (snapshot["actors"] != [{name: value for name, value in actor.items() if name in ("actor", "body_hex", "dynamics_hex", "gameplay_hex")}
                                  for actor in initial["actors"]]
                or struct.unpack("<I", data["rng"])[0] != initial["rng"]):
            raise ValueError("Cold-grid sequence endpoint disagrees with first recorded PRE")


# Named460170 writers only; raw pointer slots and inherited loader bytes are
# excluded. SourceSnapshot here is a generated factory publication, not input.
COLD_GRID_ACTOR_FIELDS = (
    *(("rank_ring", name, "gameplay", offset, form) for name, offset, form in (
        ("rank", 0xbc, "h"), ("grid_rank", 0x8e, "B"), ("race_rank", 0x8f, "B"),
        ("spatial_rank", 0x90, "B"), ("ahead", 0x96, "B"), ("behind", 0x97, "B"))),
    *(("identity", name, block, offset, form) for name, block, offset, form in (
        ("active", "gameplay", 4, "I"), ("human", "gameplay", 8, "I"), ("player", "gameplay", 0x80, "b"),
        ("driver", "gameplay", 0x8d, "B"), ("actor", "body", 0x515, "B"), ("car", "body", 0x516, "B"))),
    *(("route_cache", name, "gameplay", offset, form) for name, offset, form in (
        ("direction", 0x10, "I"), ("passing", 0x2c, "I"), ("refresh", 0x3c, "I"),
        ("swap_start_lines", 0x50, "I"), ("traffic_mode", 0x81, "B"), ("tire_cooldown", 0x83, "B"),
        ("cadence", 0x86, "B"), ("current_gate", 0x9a, "B"), ("previous_gate", 0x9b, "B"),
        ("marker", 0x9d, "B"), ("cached_point", 0x9e, "B"), ("next_point", 0xa2, "h"),
        ("previous_point", 0xa4, "h"), ("checkpoint", 0xa6, "B"), ("lap", 0xa8, "h"),
        ("game_speed", 0xcc, "i"), ("desired_speed", 0xd0, "i"), ("alternate_speed", 0xd4, "i"),
        ("speed_variation", 0xd8, "i"), ("race_distance", 0xdc, "i"), ("target_distance", 0xe0, "i"),
        ("projected_distance", 0xe4, "i"), ("projected_alias", 0xe8, "i"), ("stopped_distance", 0xec, "i"),
        ("forward_gap", 0xf0, "i"), ("distance_delta", 0xf4, "i"))),
    *(("route_cache", prefix + axis, "gameplay", offset + index * 4, "i")
      for prefix, offset in (("target_", 0x108), ("alternate_target_", 0x114)) for index, axis in enumerate("xyz")),
    *(("pose_support", "position_" + axis, "body", 0xbc + index * 4, "i") for index, axis in enumerate("xyz")),
    *(("pose_support", f"basis_{row}{column}", "body", 0x150 + row * 12 + column * 4, "i") for row in range(3) for column in range(3)),
    *(("pose_support", "ground_dot_" + str(index), "body", 0xb0 + index * 4, "i") for index in range(3)),
    ("pose_support", "support_count", "body", 0x51f, "B"),
    *(("pose_support", "top_support_flag_" + str(index), "body", 0x517 + index, "B") for index in range(4)),
    *(("pose_support", "top_support_index_" + str(index), "body", 0x521 + index, "B") for index in range(4)),
    *(("pose_support", f"contact_{point}_{axis}", "body", 0xf0 + point * 12 + index * 4, "i") for point in range(8) for index, axis in enumerate("xyz")),
    *(("modes_controls", name, block, offset, form) for name, block, offset, form in (
        ("body_mode", "body", 0x4f0, "I"), ("published_brake", "body", 0x4e8, "i"),
        ("clutch", "dynamics", 0x148, "I"), ("queued_shift", "dynamics", 0x14c, "b"),
        ("engine_speed", "dynamics", 0x124, "i"),
        ("throttle_step", "dynamics", 0x14, "i"), ("brake_step", "dynamics", 0x38, "i"),
        ("throttle_ramp", "dynamics", 0x120, "i"),
        ("brake", "dynamics", 0x130, "i"), ("brake_ramp", "dynamics", 0x134, "i"),
        ("steering_return", "dynamics", 0x44, "i"), ("steering_accumulator", "dynamics", 0x150, "i"),
        ("steering_angle", "dynamics", 0x274, "h"), ("gear", "dynamics", 0x330, "B"),
        ("pending_flag", "dynamics", 0x30c, "I"),
        ("reverse_pedal", "dynamics", 0x304, "i"), ("transmission_mode", "dynamics", 0x27c, "I"))),
    *(("modes_controls", "tire_" + str(index), "dynamics", 0xdc + index * 4, "i") for index in range(8)),
)
COLD_GRID_INHERITED_CONTROLS = (("throttle", 0x11c), ("left", 0x138), ("right", 0x13c),
                                ("raw_throttle", 0x140), ("raw_brake", 0x144))
COLD_GRID_BINARY = struct.Struct("<8s8I16h10s")


def cold_grid_factory_inputs(header):
    """Typed actual entry inputs only; no initialized actor/setup imports."""
    if header.get("cold_grid_initialization_manifest") != COLD_GRID_INIT_MANIFEST:
        raise ValueError("Cold-grid comparison requires actual --cold-grid-init evidence")
    evidence = header["cold_grid_initialization"]
    validate_cold_grid_initialization(evidence, header["grid_initialization"])
    entry = cold_grid_boundary_bytes(evidence["pre"])
    config = entry["configuration"]
    def word(offset): return struct.unpack_from("<I", config, offset)[0]
    if any(word(offset) for offset in (8, 0x58, 0x94, 0x5c)):
        raise ValueError("Cold-grid factory excludes Showdown, Mayhem, Impossible and low gravity")
    if struct.unpack_from("<h", config, 0x5f8)[0] == 1:
        raise ValueError("Cold-grid entry state1 permutation requires a separate RNG contract")
    car = integer(word(0xdc), 0, 7, "cold-grid standard car")
    if entry["local_actor_slots"][0] != car * 2:
        raise ValueError("Cold-grid entry local actor disagrees with standard car binding")
    name = config[0xb0:0xc0].split(b"\0", 1)[0]
    if not 1 <= len(name) <= 9:
        raise ValueError("Cold-grid entry player name must contain1..9 bytes")
    transmission = integer(word(0xe0), 1, 2, "cold-grid transmission mode")
    for actor, snapshot in enumerate(evidence["pre"]["actors"]):
        if bytes.fromhex(snapshot["gameplay_hex"])[0x8d] != 0:
            raise ValueError(f"Cold-grid classification requires pristine zero entry driver binding for actor{actor}")
        dynamics = bytes.fromhex(snapshot["dynamics_hex"])
        if any(struct.unpack_from("<i", dynamics, offset)[0] for _, offset in COLD_GRID_INHERITED_CONTROLS):
            raise ValueError(f"Cold-grid inherited controls require zero entry values for actor{actor}")
    local_gameplay = bytes.fromhex(evidence["pre"]["actors"][car * 2]["gameplay_hex"])
    if struct.unpack_from("<4I", local_gameplay, 0x60) != (0, 0, 0, transmission):
        raise ValueError("Cold-grid local saved controller modes disagree with entry CONFIG")
    track = integer(word(0x1c), 0, 8, "cold-grid ordinary track")
    weather = integer(struct.unpack_from("<H", entry["world_status"], 0x1c)[0], 1, 5, "cold-grid resolved weather")
    if weather == 2 and track in (1, 4, 5, 7):
        raise ValueError("Cold-grid cloudy track requires an independently observed horizon")
    return {"track": track, "car": car, "weather": weather,
            "difficulty": integer(word(0x34), 0, 2, "cold-grid difficulty"), "manual": int(transmission == 1),
            "steering": integer(word(0x100), 1, 3, "cold-grid steering profile"),
            "seed": header["grid_initialization"]["pre"]["rng"],
            "entry_points": struct.unpack_from("<16h", config, 0x5fe), "player_name": name}


def write_cold_grid_binary(path, header):
    inputs = cold_grid_factory_inputs(header)
    Path(path).write_bytes(COLD_GRID_BINARY.pack(b"TOCACGIN", 1, *(inputs[name] for name in
        ("track", "car", "weather", "difficulty", "manual", "steering", "seed")),
        *inputs["entry_points"], inputs["player_name"].ljust(10, b"\0")))


def cold_grid_expected(header, evidence=None, legacy=None):
    """Expectations only; alternate boundaries never become factory inputs."""
    evidence = header["cold_grid_initialization"] if evidence is None else evidence
    legacy = header["grid_initialization"] if legacy is None else legacy
    post = evidence["post"]
    data = cold_grid_boundary_bytes(post)
    def row(group, actor, field, value): return {"group": group, "actor": actor, "field": field, "value": value}
    local = data["local_actor_slots"][0]
    for actor, snapshot in enumerate(post["actors"]):
        for group, name, block, offset, form in COLD_GRID_ACTOR_FIELDS:
            if name == "queued_shift" and actor != local: continue
            yield row(group, actor, name, struct.unpack_from("<" + form, bytes.fromhex(snapshot[block + "_hex"]), offset)[0])
        yield row("identity", actor, "ownership", struct.unpack_from("<b", data["world_status"], actor)[0])
    entry_points = struct.unpack_from("<16h", cold_grid_boundary_bytes(evidence["pre"])["configuration"], 0x5fe)
    returned_points = struct.unpack_from("<16h", data["configuration"], 0x5fe)
    changed = [actor for actor, (before, after) in enumerate(zip(entry_points, returned_points)) if before != after]
    if len(changed) != 1 or returned_points[changed[0]] != (entry_points[changed[0]] + 1 + 32768) % 65536 - 32768:
        raise ValueError("Cold-grid return requires exactly one signed-short pole award")
    for index, value in enumerate(returned_points):
        yield row("configuration_points", index, "points", value)
    yield row("configuration_points", -1, "pole_actor", changed[0])
    local = data["local_actor_slots"][0]
    name_extent = len(cold_grid_factory_inputs(header)["player_name"]) + 1
    for byte in range(name_extent):
        yield row("local_name", local, "name_" + str(byte), data["resolved_ai"][local * 40 + byte])
    yield row("reset", -1, "full_body_budget", struct.unpack("<H", data["full_body_budget"])[0])
    yield row("reset", -1, "dispatch_reset", struct.unpack("<I", data["dispatch_reset"])[0])
    yield row("reset", -1, "release_counter", legacy["post"]["release_counter"])
    for index, value in enumerate(struct.unpack_from("<4I", data["status"])):
        yield row("reset", -1, "status_" + str(index), value)
    for index, offset in enumerate((0xc, 0x10)):
        yield row("reset", -1, "ai_setup_reset_" + str(index), struct.unpack_from("<I", data["ai_globals"], offset)[0])
    for pair in range(256):
        yield row("pair_reset", pair, "handle", struct.unpack_from("<i", data["pair_matrix"], pair * 8)[0])
        yield row("pair_reset", pair, "touching", struct.unpack_from("<I", data["pair_matrix"], pair * 8 + 4)[0])
    yield row("clock_contract", -1, "first_sample", struct.unpack("<I", data["clock_first_sample"])[0])
    yield row("rng", -1, "final_seed", legacy["post"]["rng"])


def compare_cold_grid(header, csv_path):
    import csv
    cold_grid_factory_inputs(header)
    mismatches, groups, count = [], {}, 0
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["group", "actor", "field", "value"]:
            raise ValueError("Unexpected cold-grid factory CSV header")
        for expected in cold_grid_expected(header):
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing cold-grid factory result")
            if (actual["group"], int(actual["actor"]), actual["field"]) != (expected["group"], expected["actor"], expected["field"]):
                raise ValueError("Reordered cold-grid factory result")
            count += 1
            group = groups.setdefault(expected["group"], {"compared_values": 0, "mismatch_count": 0})
            group["compared_values"] += 1
            if int(actual["value"]) != expected["value"]:
                group["mismatch_count"] += 1
                mismatches.append({**expected, "expected": expected["value"], "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra cold-grid factory result")
    for group in groups.values(): group["parity"] = group["mismatch_count"] == 0
    pre, post = (cold_grid_boundary_bytes(header["cold_grid_initialization"][phase]) for phase in ("pre", "post"))
    inherited = {name: pre[name] == post[name] for name in
                 ("route_descriptor", "startline_start", "startline_end", "startline_primary", "startline_alternate",
                  "braking_memory", "rank_threshold", "cumulative_route", "brake_bypass_pointer")}
    inherited_controls = []
    for actor, (entry, returned) in enumerate(zip(header["cold_grid_initialization"]["pre"]["actors"],
                                                 header["cold_grid_initialization"]["post"]["actors"])):
        before, after = (bytes.fromhex(snapshot["dynamics_hex"]) for snapshot in (entry, returned))
        for name, offset in COLD_GRID_INHERITED_CONTROLS:
            unchanged = struct.unpack_from("<i", before, offset)[0] == struct.unpack_from("<i", after, offset)[0]
            if not unchanged:
                raise ValueError(f"Cold-grid inherited control changed across initializer: actor{actor} {name}")
            inherited_controls.append({"actor": actor, "field": name, "source_entry_return_unchanged": unchanged})
    return {"parity": not mismatches, "numerical_parity": not mismatches, "compared_initializations": 1,
            "compared_values": count, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:64], "groups": groups,
            "input_boundary": "460170-entry", "comparison_boundary": "460170-return", "first_pre_substitutions": 0,
            "return_inputs": 0, "inputs": "canonical authored assets; typed entry track/car/resolved weather/difficulty/manual/steering/player name, sixteen score shorts and entry RNG",
            "inherited_loader_context": {"source_entry_return_unchanged": inherited, "native_generation_claim": False,
                                         "included_in_compared_values": False},
            "inherited_control_context": {"entry_zero_precondition": True, "source_observations": inherited_controls,
                                          "local_saved_modes_match_entry_configuration": True,
                                          "entry_classification_driver_binding": "pristine zero for all sixteen actors;444270 precedes401370 identity publication",
                                          "native_generation_claim": False, "included_in_compared_values": False},
            "clock_contract": {"generated": "first-sample latch only", "qpc_divisor": "observed source return; no independent QueryPerformanceFrequency evidence or generated numeric claim"},
            "remaining_scope": "actual initializer only; inherited rankBC upper bytes and local name tail after strcpy terminator, AI queued-shift14c bytes, previous RPM128/pending gear334/lamps500+508 and lower support-cache/status944+948 tails, transitive rendering/effect ownership, inherited raw loader fields and unrecorded return-to-firstPRE frame/RNG gap remain explicit",
            "continuous_physics_parity": None, "autonomous_race_parity": None}


def cold_grid_sequence_inputs(header):
    if header.get("cold_grid_sequence_manifest") != COLD_GRID_SEQUENCE_MANIFEST:
        raise ValueError("Startup comparison requires actual --cold-grid-sequence evidence")
    validate_cold_grid_sequence(header["cold_grid_sequence"], header["grid_initialization"], header["cold_grid_initialization"])
    return cold_grid_factory_inputs(header)


def write_cold_grid_sequence_binary(path, header):
    cold_grid_sequence_inputs(header)
    #Exactly the old82-byte FIRST entry request. No second entry/return,
    #endpoint, observed resolved table, or continuity metadata is serialized.
    write_cold_grid_binary(path, header)


def cold_grid_sequence_expected(header, records):
    cold_grid_sequence_inputs(header)
    sequence = header["cold_grid_sequence"]
    validate_cold_grid_sequence(sequence, header["grid_initialization"], header["cold_grid_initialization"], records[0]["pre"])
    for phase, event in zip(("first_return", "second_return"), sequence["events"]):
        for row in cold_grid_expected(header, event["boundaries"], event["initialization"]):
            yield {"phase": phase, **row}
    second = sequence["events"][1]
    endpoint = sequence["first_pre"]["snapshot"]
    endpoint_scalar = {"post": {"rng": struct.unpack("<I", cold_grid_boundary_bytes(endpoint)["rng"])[0],
                               "release_counter": records[0]["pre"]["shared"]["ai_state"]["release_counter"]}}
    for row in cold_grid_expected(header, {"pre": second["boundaries"]["pre"], "post": endpoint}, endpoint_scalar):
        yield {"phase": "first_pre", **row}


def cold_grid_retained_input_continuity(header):
    """Observed proof of required carry dependencies, not native inputs."""
    first, second = header["cold_grid_sequence"]["events"]
    before, after = (cold_grid_boundary_bytes(snapshot) for snapshot in (first["boundaries"]["post"], second["boundaries"]["pre"]))
    config_before, config_after = before["configuration"], after["configuration"]
    checks = {"rng": before["rng"] == after["rng"], "points": config_before[0x5fe:0x61e] == config_after[0x5fe:0x61e],
              "canonical_driver_rows_and_local_name": before["resolved_ai"] == after["resolved_ai"],
              "canonical_distributions_weather_tire_context": before["ai_globals"][0x14:] == after["ai_globals"][0x14:],
              "local_binding": before["local_actor_slots"] == after["local_actor_slots"],
              "caller_configuration": config_before == config_after,
              "retained_ownership": before["world_status"][:16] == after["world_status"][:16],
              "resolved_weather": before["world_status"][0x1c:0x1e] == after["world_status"][0x1c:0x1e],
              "entry_player_name": config_before[0xb0:0xc0] == config_after[0xb0:0xc0]}
    driver_bindings = []
    inherited_controls = []
    for actor in range(ACTORS):
        a = first["boundaries"]["post"]["actors"][actor]
        b = second["boundaries"]["pre"]["actors"][actor]
        driver_bindings.append(bytes.fromhex(a["gameplay_hex"])[0x8d] == bytes.fromhex(b["gameplay_hex"])[0x8d] == actor)
        for _, offset in COLD_GRID_INHERITED_CONTROLS:
            inherited_controls.append(struct.unpack_from("<i", bytes.fromhex(a["dynamics_hex"]), offset)[0]
                                      == struct.unpack_from("<i", bytes.fromhex(b["dynamics_hex"]), offset)[0] == 0)
    checks["generated_driver_bindings"] = all(driver_bindings)
    checks["cold_inherited_controls_remain_zero"] = all(inherited_controls)
    local = before["local_actor_slots"][0]
    saved = struct.unpack_from("<4I", bytes.fromhex(second["boundaries"]["pre"]["actors"][local]["gameplay_hex"]), 0x60)
    checks["saved_local_controller_modes"] = saved == (0, 0, 0, struct.unpack_from("<I", config_before, 0xe0)[0])
    return {"parity": all(checks.values()), "checks": checks, "mismatch_count": sum(not value for value in checks.values()), "native_input_count": 0,
            "generated_count": 0, "scope": "First generated identity/points/name/RNG feed second factory; canonical driver/distribution tables are retained internally, never imported from these raw observations."}


def compare_cold_grid_sequence(header, records, csv_path):
    import csv
    cold_grid_sequence_inputs(header)
    phases = {phase: {"compared_values": 0, "mismatch_count": 0, "groups": {}, "first_mismatches": []}
              for phase in ("first_return", "second_return", "first_pre")}
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["phase", "group", "actor", "field", "value"]:
            raise ValueError("Unexpected startup factory CSV header")
        for wanted in cold_grid_sequence_expected(header, records):
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing startup factory result")
            if tuple(actual[name] if name != "actor" else int(actual[name]) for name in ("phase", "group", "actor", "field")) != tuple(wanted[name] for name in ("phase", "group", "actor", "field")):
                raise ValueError("Reordered startup factory result")
            phase = phases[wanted["phase"]]; phase["compared_values"] += 1
            group = phase["groups"].setdefault(wanted["group"], {"compared_values": 0, "mismatch_count": 0})
            group["compared_values"] += 1
            if int(actual["value"]) != wanted["value"]:
                phase["mismatch_count"] += 1; group["mismatch_count"] += 1
                if len(phase["first_mismatches"]) < 64:
                    phase["first_mismatches"].append({**wanted, "expected": wanted["value"], "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra startup factory result")
    for phase in phases.values():
        phase["parity"] = phase["mismatch_count"] == 0
        for group in phase["groups"].values(): group["parity"] = group["mismatch_count"] == 0
    continuity = cold_grid_retained_input_continuity(header)
    events = header["cold_grid_sequence"]["events"]
    endpoint_snapshot = header["cold_grid_sequence"]["first_pre"]["snapshot"]
    endpoint = cold_grid_boundary_bytes(endpoint_snapshot)
    inherited_zero = {phase: all(struct.unpack_from("<i", bytes.fromhex(actor["dynamics_hex"]), offset)[0] == 0
                                for actor in snapshot["actors"] for _, offset in COLD_GRID_INHERITED_CONTROLS)
                      for phase, snapshot in (("first_return", events[0]["boundaries"]["post"]),
                                              ("second_entry", events[1]["boundaries"]["pre"]),
                                              ("second_return", events[1]["boundaries"]["post"]), ("first_pre", endpoint_snapshot))}
    parity = all(phase["parity"] for phase in phases.values()) and continuity["parity"] and all(inherited_zero.values())
    return {"parity": parity, "phases": phases, "compared_values": sum(phase["compared_values"] for phase in phases.values()),
            "mismatch_count": sum(phase["mismatch_count"] for phase in phases.values()),
            "first_return_to_second_entry": continuity, "continuity_mismatch_count": continuity["mismatch_count"], "initialization_calls": 2,
            "first_entry_inputs_only": True, "input_bytes": COLD_GRID_BINARY.size, "return_inputs": 0, "second_entry_inputs": 0,
            "first_pre_inputs": 0, "gap_closed_for_named_writer_subset": parity,
            "rng": {"first_entry": events[0]["initialization"]["pre"]["rng"], "first_return": events[0]["initialization"]["post"]["rng"],
                    "second_entry": events[1]["initialization"]["pre"]["rng"], "second_return": events[1]["initialization"]["post"]["rng"],
                    "first_pre": struct.unpack("<I", endpoint["rng"])[0]},
            "inherited_context": {"included_in_generated_count": False, "source_control_zero_observations": inherited_zero,
                                  "observations_per_phase": 80, "scope": "Cold raw axes/throttle and saved local modes are validated context; loader resources are canonical or internally retained. Unwritten rank/name/support/status tails and scene/render ownership remain outside the named projection."},
            "clock_contract": {"generated": "Initializer first-sample boolean; endpoint retention compared separately", "numeric_qpc_frequency": "external/unverified; no fabricated divisor or clock sample input"},
            "remaining_scope": "Bounded two-initializer transaction and named first-PRE survival only; full first-PRE bytes, autonomous racing and replacing initial imports in older joined intervals remain separate work.",
            "continuous_physics_parity": None, "autonomous_race_parity": None}


STARTUP_JOIN_ACTOR_FIELDS = tuple(field for field in COLD_GRID_ACTOR_FIELDS if field[1] != "body_mode")
STARTUP_PHYSICAL_BODY_FIELDS = tuple(field for field in COLD_GRID_ACTOR_FIELDS if field[2] == "body")
STARTUP_CONTROL_CALIBRATION_FIELDS = (("maximum_throttle", 0x0c), ("base_throttle", 0x10),
                                    ("throttle_base", 0x18), ("front_drive", 0x1c), ("rpm_scale", 0x24),
                                    ("maximum_brake", 0x30), ("brake_base", 0x3c), ("steering_return_base", 0x48))
STARTUP_CONTACT_CACHE_FIELDS = (
    *((f"height_{index}", "body", 0x1c4 + index * 4, "i") for index in range(8)),
    *((f"normal_{index}", "body", 0x1e4 + index * 4, "i") for index in range(3)),
    *((f"triangle_{index}", "dynamics", 0x264 + index * 2, "H") for index in range(8)),
    *((f"surface_{index}", "dynamics", 0x328 + index, "B") for index in range(8)),
)
STARTUP_COLD_ACTOR_FIELDS = (
    ("velocity", "body", 0xc8, 12), ("speed", "body", 0xe0, 4), ("angular_velocity", "body", 0xe4, 12),
    ("pending_force", "body", 0x1f0, 12), ("pending_moment", "body", 0x1fc, 12),
    ("wheel_speed", "dynamics", 0xfc, 16), ("longitudinal_slip", "dynamics", 0x1e4, 16), ("lateral_slip", "dynamics", 0x1f4, 16),
    ("visual_angles", "dynamics", 0x278, 4), ("rumble", "dynamics", 0x280, 16), ("previous_rumble", "dynamics", 0x290, 16),
    ("rollover", "dynamics", 0x2a0, 12), ("patch_latch", "dynamics", 0x2bc, 4), ("tire_effect_gate", "dynamics", 0x2c0, 4),
    ("player_righting", "dynamics", 0x31c, 4), ("rollover_ticks", "dynamics", 0x331, 3),
    ("throttle", "dynamics", 0x11c, 4), ("raw_axes", "dynamics", 0x138, 16),
)


def startup_cold_actor_input(block, raw):
    raw = bytearray(raw)
    for _, field_block, offset, size in STARTUP_COLD_ACTOR_FIELDS:
        if block == field_block: raw[offset:offset + size] = bytes(size)
    return raw


def validate_startup_join_scope(header, records):
    """Actual typed entry on this interval, before any generated consumer."""
    inputs = cold_grid_sequence_inputs(header)
    validate_combined_scope(header, records, compare_driver_retention=True)
    validate_cold_grid_sequence(header["cold_grid_sequence"], header["grid_initialization"], header["cold_grid_initialization"], records[0]["pre"])
    if header.get("unrecorded_keyboard_warmup") or "contact_weather_manifest" in header and not cold_local_session_enabled(header):
        raise ValueError("Startup join requires the complete ordinary cold combined interval without warmup")
    if header["local_actor"] != inputs["car"] * 2:
        raise ValueError("Startup join local actor disagrees with typed first entry")
    if not any(record["release_events"] for record in records) or not any(record["pre"]["session_state"] == 6 for record in records):
        raise ValueError("Startup join requires a positive release and active racing interval")
    for actor in range(ACTORS):
        raw_controls = bytes.fromhex(records[0]["ai_pre"]["actors"][actor]["dynamics_hex"])
        if any(struct.unpack_from("<i", raw_controls, offset)[0] for _, offset in COLD_GRID_INHERITED_CONTROLS):
            raise ValueError("Startup join requires zero inherited first-AI throttle/raw axes")
        for _, name, block, offset, form in STARTUP_JOIN_ACTOR_FIELDS:
            if name == "queued_shift" and actor != header["local_actor"]: continue
            first = bytes.fromhex(records[0]["pre"]["actors"][actor][block + "_hex"])
            consumer = bytes.fromhex(records[0]["ai_pre"]["actors"][actor][block + "_hex"])
            size = struct.calcsize("<" + form)
            if first[offset:offset + size] != consumer[offset:offset + size]:
                raise ValueError(f"Startup writer changed before first AI consumer: actor{actor} {name}")
    return inputs


def startup_join_actor_input(block, raw, actor, local):
    """Erase only accepted startup writers, preserving the observed complement."""
    raw = bytearray(raw)
    for _, name, field_block, offset, form in STARTUP_JOIN_ACTOR_FIELDS:
        if block != field_block or name == "queued_shift" and actor != local: continue
        size = struct.calcsize("<" + form)
        raw[offset:offset + size] = bytes(size)
    return raw


def write_startup_join_binary(path, header, records):
    validate_startup_join_scope(header, records)
    write_cold_grid_sequence_binary(path, header)


def validate_startup_first_selection_scope(header, records):
    inputs = validate_startup_join_scope(header, records)
    first = records[0]
    for phase in ("pre", "ai_pre"):
        for actor in first[phase]["actors"]:
            dynamics = bytes.fromhex(actor["dynamics_hex"])
            if struct.unpack_from("<2I", dynamics, 0x2a0) != (0, 0):
                raise ValueError("First startup selector requires initialized rollover flags at PRE and AI entry")
    return inputs


def startup_first_selection_expected(records):
    """Actual pre-selector inputs and first pre-AI outputs, never replay inputs."""
    first = records[0]
    for actor, (before, after) in enumerate(zip(first["pre"]["actors"], first["ai_pre"]["actors"])):
        previous = struct.unpack_from("<I", bytes.fromhex(before["body_hex"]), 0x4f0)[0]
        upside, sideways = struct.unpack_from("<2I", bytes.fromhex(before["dynamics_hex"]), 0x2a0)
        mode = struct.unpack_from("<I", bytes.fromhex(after["body_hex"]), 0x4f0)[0]
        for field, value in (("previous", previous), ("upside_down", upside), ("sideways", sideways), ("mode", mode)):
            yield {"group": "selection", "actor": actor, "field": field, "value": value}
    full = sum(struct.unpack_from("<I", bytes.fromhex(actor["body_hex"]), 0x4f0)[0] == 0 for actor in first["ai_pre"]["actors"])
    yield {"group": "selection", "actor": -1, "field": "full_count", "value": full}
    for field, phase in (("before", "pre"), ("after", "ai_pre")):
        yield {"group": "rng", "actor": -1, "field": field, "value": first[phase]["rng"]}


def compare_startup_first_selection(header, records, csv_path):
    import csv
    validate_startup_first_selection_scope(header, records)
    mismatches = []
    expected = list(startup_first_selection_expected(records))
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["group", "actor", "field", "value"]:
            raise ValueError("Unexpected first startup selection CSV header")
        for wanted in expected:
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing first startup selection result")
            if (actual["group"], int(actual["actor"]), actual["field"]) != (wanted["group"], wanted["actor"], wanted["field"]):
                raise ValueError("Reordered first startup selection result")
            if int(actual["value"]) != wanted["value"]:
                mismatches.append({**wanted, "expected": wanted["value"], "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra first startup selection result")
    previous = [row["value"] for row in expected if row["field"] == "previous"]
    modes = [row["value"] for row in expected if row["field"] == "mode"]
    checks = {"sixteen_initial_distant": previous == [1] * ACTORS,
              "four_full_twelve_distant": modes.count(0) == 4 and modes.count(1) == 12,
              "positive_distant_to_full": sum(a == 1 and b == 0 for a, b in zip(previous, modes)) == 4,
              "no_selection_random_draw": records[0]["pre"]["rng"] == records[0]["ai_pre"]["rng"]}
    return {"parity": not mismatches and all(checks.values()), "numerical_parity": not mismatches,
            "compared_values": len(expected), "mismatch_count": len(mismatches), "first_mismatches": mismatches,
            "coverage_sufficient": all(checks.values()), "coverage": checks,
            "source_full_actors": [actor for actor, mode in enumerate(modes) if mode == 0],
            "boundary": "First414e20/414ef0 selection after generated startup, before403dd0; PRE and AI entry are separate expectations.",
            "generated_cold_writers": {"dynamics_offsets": [0x2a0, 0x2a4], "source": "40a770", "separate_from_unchanged_117_field_table": True},
            "remaining_context": "Later body modes, physical state, allocation/lifecycle/visibility and frame clocks remain observed; no continuous physical or later selector claim."}


def startup_physical_body_input(raw):
    """Only the accepted consumed BODY writers, never a complete cold snapshot."""
    raw = bytearray(raw)
    for _, _, _, offset, form in STARTUP_PHYSICAL_BODY_FIELDS:
        size = struct.calcsize("<" + form)
        raw[offset:offset + size] = bytes(size)
    return raw


def startup_physical_body_expected(records):
    for actor, entry in enumerate(records[0]["pre"]["actors"]):
        raw = bytes.fromhex(entry["body_hex"])
        for _, name, _, offset, form in STARTUP_PHYSICAL_BODY_FIELDS:
            yield {"group": "body", "actor": actor, "field": name, "value": struct.unpack_from("<" + form, raw, offset)[0]}


def compare_startup_physical_body(header, records, csv_path):
    import csv
    validate_startup_first_selection_scope(header, records)
    expected = list(startup_physical_body_expected(records))
    mismatches = []
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["group", "actor", "field", "value"]:
            raise ValueError("Unexpected startup physical BODY CSV header")
        for wanted in expected:
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing startup physical BODY result")
            if (actual["group"], int(actual["actor"]), actual["field"]) != (wanted["group"], wanted["actor"], wanted["field"]):
                raise ValueError("Reordered startup physical BODY result")
            if int(actual["value"]) != wanted["value"]:
                mismatches.append({**wanted, "expected": wanted["value"], "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra startup physical BODY result")
    return {"parity": not mismatches, "numerical_parity": not mismatches,
            "compared_values": len(expected), "mismatch_count": len(mismatches), "first_mismatches": mismatches[:64],
            "actor_field_definitions": len(STARTUP_PHYSICAL_BODY_FIELDS),
            "bytes_per_actor": sum(struct.calcsize("<" + field[4]) for field in STARTUP_PHYSICAL_BODY_FIELDS),
            "boundary": "First physical Fleet::restore from421aa0 PRE, before the one first pre-AI selection.",
            "input_policy": "Same82 typed firstENTRY bytes; named BODY overlay only. Initial controls/gameplay follow retained-driver chronology; remaining initial physical/world state is observed.",
            "remaining_initial_context": "Calibration, cached surfaces, prior positions/forces, damage/props/pairs, registration/visibility, limiter/launch and callback clocks remain explicit. No complete cold Motion/world generation claim."}


def startup_physical_body_coverage(header, records, comparison, body, selection):
    """New cold handoff positives; preserve all older coverage diagnostics."""
    joined = startup_join_coverage(header, records, comparison["combined"])
    physical = comparison["physical_feedback"]
    dispatch = comparison["dispatch_feedback"]
    counts = physical["positive_source_counts"]
    checks = {"startup_retained_interval": joined["sufficient"],
              "all_52_body_fields_at_initial_restore": body["compared_values"] == ACTORS * len(STARTUP_PHYSICAL_BODY_FIELDS) == 832,
              "initial_field_extent_175_bytes": body["bytes_per_actor"] == 175,
              "first_selection_positive": selection["coverage_sufficient"],
              "positive_full_and_distant_physics": counts["full_updates"] > 0 and counts["distant_updates"] > 0,
              "following_pre_state_compared": counts["following_pre_updates"] == len(records) - 1 and physical["compared_values"] > 0,
              "later_physical_imports_absent": physical["initial_physical_world_imports"] == 1 and physical["later_physical_world_imports"] == 0,
              "retained_dispatch_transitions": dispatch["coverage_sufficient"]}
    return {"sufficient": all(checks.values()), "checks": checks, "startup_interval": joined,
            "scope": "First52 BODY writers feed physical PRE restore; one generated first selector, then the existing continuous dispatch/physical/control/driver/shared-RNG interval.",
            "old_aggregate_diagnostics": {"parity": comparison["parity"], "coverage_sufficient": comparison["coverage_sufficient"],
                                          "reused_coverage_required": comparison.get("reused_coverage_required", []),
                                          "physical_positive_source_counts": counts, "unchanged": True},
            "uncovered_branches": {"positive_contact_and_pending_force": counts["contact_updates"] > 0 and counts["pending_force_updates"] > 0,
                                   "full_cold_physical_initialization": False, "lifecycle_or_callback_clock_generation": False}}


def startup_control_calibration_input(raw):
    raw = bytearray(raw)
    for _, offset in STARTUP_CONTROL_CALIBRATION_FIELDS: raw[offset:offset + 4] = bytes(4)
    return raw


def startup_control_calibration_expected(records):
    #The second phase observes the actual Controls consumer after PRE restore;
    #it does not apply the cold values again after AI or physical integration.
    for phase in ("first_pre", "restored"):
        for actor, entry in enumerate(records[0]["pre"]["actors"]):
            raw = bytes.fromhex(entry["dynamics_hex"])
            for field, offset in STARTUP_CONTROL_CALIBRATION_FIELDS:
                yield {"phase": phase, "actor": actor, "field": field, "value": struct.unpack_from("<i", raw, offset)[0]}


def compare_startup_control_calibration(header, records, csv_path):
    import csv
    validate_startup_first_selection_scope(header, records)
    expected = list(startup_control_calibration_expected(records))
    mismatches = []
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["phase", "actor", "field", "value"]:
            raise ValueError("Unexpected startup control calibration CSV header")
        for wanted in expected:
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing startup control calibration result")
            if (actual["phase"], int(actual["actor"]), actual["field"]) != (wanted["phase"], wanted["actor"], wanted["field"]):
                raise ValueError("Reordered startup control calibration result")
            if int(actual["value"]) != wanted["value"]:
                mismatches.append({**wanted, "expected": wanted["value"], "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra startup control calibration result")
    values = {name: [row["value"] for row in expected if row["phase"] == "first_pre" and row["field"] == name]
              for name, _ in STARTUP_CONTROL_CALIBRATION_FIELDS}
    checks = {"eight_source_owned_words": len(STARTUP_CONTROL_CALIBRATION_FIELDS) == 8,
              "all_actors_and_both_consumers": len(expected) == 256,
              "positive_vehicle_calibration": all(value > 0 for name in ("maximum_throttle", "base_throttle", "maximum_brake", "rpm_scale") for value in values[name]),
              "positive_car_binding_variation": len(set(values["maximum_throttle"])) > 1,
              "cold_current_equals_baseline": values["maximum_throttle"] == values["base_throttle"]}
    return {"parity": not mismatches and all(checks.values()), "numerical_parity": not mismatches,
            "coverage_sufficient": all(checks.values()), "coverage": checks, "compared_values": len(expected),
            "mismatch_count": len(mismatches), "first_mismatches": mismatches[:64], "actor_field_definitions": 8, "bytes_per_actor": 32,
            "phases": {phase: {"compared_values": 128, "mismatch_count": sum(row["phase"] == phase for row in mismatches)} for phase in ("first_pre", "restored")},
            "boundary": "Generated first PRE calibration, then actual Controls state after initial Fleet::restore and before AI.",
            "source_writers": "40a770 canonical car calibration;412c90 reads rates18/3c/48 and writes cached14/38/44 only.",
            "mutability": "010 is the retained car baseline;428440 reads010 and writes current maximum00c. Initial equality is compared only at cold PRE, never imposed after AI or damage.",
            "remaining_context": "Other calibration/configuration, callback RPM-scale copies, inherited controls, cached surfaces, physical/world state and clocks keep their existing contracts; later calibration copies remain observed configuration context."}


def startup_contact_cache_input(block, raw):
    raw = bytearray(raw)
    for _, field_block, offset, form in STARTUP_CONTACT_CACHE_FIELDS:
        if field_block != block: continue
        size = struct.calcsize("<" + form)
        raw[offset:offset + size] = bytes(size)
    return raw


def startup_contact_cache_expected(records):
    for phase in ("first_pre", "restored"):
        for actor, entry in enumerate(records[0]["pre"]["actors"]):
            for field, block, offset, form in STARTUP_CONTACT_CACHE_FIELDS:
                yield {"phase": phase, "actor": actor, "field": field,
                       "value": struct.unpack_from("<" + form, bytes.fromhex(entry[block + "_hex"]), offset)[0]}


def compare_startup_contact_cache(header, records, csv_path):
    import csv
    validate_startup_first_selection_scope(header, records)
    expected = list(startup_contact_cache_expected(records))
    mismatches = []
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["phase", "actor", "field", "value"]:
            raise ValueError("Unexpected startup contact cache CSV header")
        for wanted in expected:
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing startup contact cache result")
            if (actual["phase"], int(actual["actor"]), actual["field"]) != (wanted["phase"], wanted["actor"], wanted["field"]):
                raise ValueError("Reordered startup contact cache result")
            if int(actual["value"]) != wanted["value"]:
                mismatches.append({**wanted, "expected": wanted["value"], "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra startup contact cache result")
    initial = [row for row in expected if row["phase"] == "first_pre"]
    heights = [row["value"] for row in initial if row["field"].startswith("height_")]
    triangles = [row["value"] for row in initial if row["field"].startswith("triangle_")]
    surfaces = sorted({row["value"] for row in initial if row["field"].startswith("surface_")})
    checks = {"all_27_fields_and_both_consumers": len(STARTUP_CONTACT_CACHE_FIELDS) == 27 and len(expected) == 864,
              "positive_height_geometry": any(heights) and len(set(heights)) > 1,
              "positive_triangle_lookup": any(triangles) and len(set(triangles)) > 1,
              "upward_support_normal": all(row["value"] > 0 for row in initial if row["field"] == "normal_1"),
              "nonflat_support_normal": any(row["value"] for row in initial if row["field"] in ("normal_0", "normal_2"))}
    return {"parity": not mismatches and all(checks.values()), "numerical_parity": not mismatches,
            "coverage_sufficient": all(checks.values()), "coverage": checks, "compared_values": len(expected),
            "mismatch_count": len(mismatches), "first_mismatches": mismatches[:64], "actor_field_definitions": 27, "bytes_per_actor": 68,
            "phases": {phase: {"compared_values": 432, "mismatch_count": sum(row["phase"] == phase for row in mismatches)} for phase in ("first_pre", "restored")},
            "surface_classes": surfaces, "positive_nonzero_surface_class": any(surfaces),
            "boundary": "Canonical loader-retained first PRE cache, then actual Motion cache after initial Fleet::restore and before AI.",
            "source_writers": "42e6f0 samples all8 heights/triangle IDs/material classes;40bbc0 publishes the3-word support normal. Existing canonical SGRID/HPC helpers preserve noncurrent actors0..14 and current actor15 placement order.",
            "input_policy": "Same82 actual firstENTRY bytes and canonical resources; no captured support/pose/cache is a factory input.",
            "remaining_context": "Upper support flags/indices, previous-position and force history, other inherited controls/body/world state and clocks keep existing contracts. Later caches follow the accepted retained physical path; other surface/lifecycle branches remain unproved."}


STARTUP_CLOCK_HEADER = struct.Struct("<8sIIiiiII")
STARTUP_CLOCK_FIELDS = ("delta_q16", "reciprocal_q16", "extra_update")
STARTUP_LIFECYCLE_FIELDS = (*(f"timer{index}_origin" for index in range(5)), "start_phase", "session_state", "release_epoch", "caller_clock")
QPC_SAMPLE_FIELDS = ("qpc_low", "qpc_divisor", "qpc_difference", "clock_first_sample", *STARTUP_CLOCK_FIELDS)
QPC_SAMPLE_MANIFEST = {"entry": 0x412ab0, "return_address": 0x44a516, "counter_copy": 0x4be0cc,
                       "scope": "every actual412ab0 call between physical updates, including menu-only primary frames; only the external QPC low counter is a sample input"}


SCENE_CAMERA_CONTROLS_MANIFEST = {"version": 1, "poll_entry": 0x44a1d0, "mapper_entry": 0x422f60,
    "actions": ["cycle_held", "rear_held"], "views": [1, 2, 3, 4, 5]}

def camera_controls_enabled(header):
    manifest = header.get("scene_camera_controls_manifest")
    if manifest is None: return False
    if manifest != SCENE_CAMERA_CONTROLS_MANIFEST or header.get("scenario") != "camera-cycle" or not scene_autonomous_enabled(header):
        raise ValueError("Ordinary camera controls require the joined camera-cycle profile")
    return True

def validate_camera_input(value, physical_id):
    if not isinstance(value, dict) or set(value) != {"physical_id", "cycle_held", "rear_held"} or type(value["physical_id"]) is not int or value["physical_id"] != physical_id:
        raise ValueError("Camera input requires its physical ID and requested keys only")
    if any(type(value[name]) is not bool for name in ("cycle_held", "rear_held")):
        raise ValueError("Camera keys must be booleans")
    return value


def validate_camera_controls(records):
    last_cycle = False
    previous = None
    for record in records:
        requested = validate_camera_input(record.get("camera_input"), record["id"])
        observed = record.get("camera_control_expected", {})
        if set(observed) != {"cycle_pressed", "actions", "pre", "post"}:
            raise ValueError("Missing actual camera mapper checkpoints")
        pressed = requested["cycle_held"] and not last_cycle
        if observed["cycle_pressed"] != pressed or observed["actions"] != {"cycle_pressed": pressed, "rear_held": requested["rear_held"]}:
            raise ValueError("Requested camera keys disagree with source cached mapper actions")
        before, after = observed["pre"], observed["post"]
        for value in (before, after):
            if set(value) != {"selected_view", "projection_view", "rear_held", "saved_view"} or value["selected_view"] not in range(1, 6) or value["projection_view"] not in range(1, 5) or value["rear_held"] not in (0, 1):
                raise ValueError("Invalid ordinary camera controller checkpoint")
        if previous is not None and before != previous:
            raise ValueError("Camera controller changed outside the recorded mapper")
        expected = dict(before)
        if pressed and not before["rear_held"]:
            expected["selected_view"] = before["selected_view"] % 4 + 1
            expected["projection_view"] = expected["selected_view"]
        if requested["rear_held"] and not before["rear_held"]:
            expected["saved_view"] = expected["selected_view"]
            expected["selected_view"] = 5
            expected["rear_held"] = 1
        elif not requested["rear_held"] and before["rear_held"]:
            expected["selected_view"] = before["saved_view"]
            expected["rear_held"] = 0
        if after != expected: raise ValueError("Source camera controller does not follow requested C/Z actions")
        previous, last_cycle = after, requested["cycle_held"]
    return True


SCENE_AUTONOMOUS_MANIFEST = {"version": 1, "profile": "cold-single-player-normal-view2",
    "clock_owner": "scene-frame-primary-qpc", "scene_inputs": "generated-from-retained-physical-fleet"}


def scene_autonomous_enabled(header):
    manifest = header.get("scene_autonomous_manifest")
    if manifest is None: return False
    if (manifest != SCENE_AUTONOMOUS_MANIFEST or not cold_local_session_enabled(header)
            or retained_warm_restart_enabled(header) or header.get("unrecorded_keyboard_warmup")
            or not isinstance(header.get("scene_capture_id"), str) or not header["scene_capture_id"]):
        raise ValueError("Joined scene requires the complete cold ordinary local-session provenance")
    return True


def scene_lifecycle_enabled(header):
    """Existing cold lifecycle provenance; no scene capture identity is invented."""
    return (header.get("exe_crc32") == "8ce56d41" and cold_local_session_enabled(header)
            and retained_warm_restart_enabled(header) and cold_timer_creation(header) is not None
            and ordered_clock_enabled(header) and header.get("source_mode") == 0
            and header.get("scenario") == "local-lap" and header.get("qualifying") is False)


def validate_scene_lifecycle_scope(header, records):
    if not scene_lifecycle_enabled(header) or not records or records[0]["pre"]["session_state"] != 9:
        raise ValueError("Lifecycle scene consumers require complete cold NoQual finish/results/warm provenance")
    lifecycle = retained_warm_restart_coverage(header, records)
    if not lifecycle["coverage_sufficient"]:
        raise ValueError("Lifecycle scene consumers require the local finish, results, warm grid and renewed green")
    clocks = startup_clock_inputs(header, records)
    states = sorted({record["pre"]["session_state"] for record in records})
    if states != [6, 9, 10, 13]:
        raise ValueError("Lifecycle scene consumers require states6/9/10/13 only")
    return {"sufficient": True, "physical_updates": len(records), "session_states": states,
            "local_finish_updates": [value["update"] for value in lifecycle["coverage"]["local_session"]["local_finishes"]],
            "state10_update": lifecycle["coverage"]["local_session"]["state10_update"],
            "state13_update": lifecycle["coverage"]["local_session"]["state13_update"],
            "warm_grid_updates": [value["update"] for value in lifecycle["coverage"]["warm_grid_writers"]],
            "warm_active_update_ids": lifecycle["coverage"]["warm_active_update_ids"],
            "clock_transactions": clocks["coverage"],
            "source_scene_journal": False, "camera_matrix_parity": None, "ordered_pvs_parity": None,
            "endpoint": "Complete physical records; only publications reached before the final physical POST are consumed. No terminal scene/QPC sample is synthesized."}


def ordered_clock_enabled(header):
    manifest = header.get("qpc_sample_manifest")
    creation = cold_timer_creation(header)
    if manifest is None:
        if creation is not None:
            raise ValueError("Cold timer creation requires the complete ordered QPC sample journal")
        return False
    if manifest != QPC_SAMPLE_MANIFEST or creation is None or not (retained_warm_restart_enabled(header) or scene_autonomous_enabled(header)) or header.get("unrecorded_keyboard_warmup"):
        raise ValueError("Ordered QPC samples require the complete cold warm-restart clock contract")
    return True


def validate_qpc_values(observed, expected, where):
    for name, value in expected.items():
        actual = observed[name]
        if name == "extra_update":
            if type(actual) is not bool:
                raise ValueError("QPC extra-update latch must be boolean")
        else:
            integer(actual, 1 if name in ("qpc_divisor", "delta_q16", "reciprocal_q16") else 0,
                    0x7fffffff if name in ("qpc_divisor", "delta_q16", "reciprocal_q16") else 1 if name == "clock_first_sample" else 0xffffffff, name)
        if actual != value:
            raise ValueError(f"Ordered QPC state disagrees at {where}: {name}")


def startup_journal_clock_inputs(header, records):
    """Derive time from actual QPC calls and primary/reset operations, not ticks."""
    first = records[0]["pre"]
    state = {name: first[name] for name in QPC_SAMPLE_FIELDS}
    validate_qpc_values(first, state, "initial PRE")
    if state["clock_first_sample"] != 1 or not 0 < state["delta_q16"] <= 0x30000:
        raise ValueError("Ordered QPC startup requires its pending first sample and retained step")
    initial = tuple(state[name] for name in STARTUP_CLOCK_FIELDS)
    divisor = state["qpc_divisor"]
    def signed(value): return (value + 0x80000000) % 0x100000000 - 0x80000000
    def divide(value, denominator): return value // denominator if value >= 0 else -((-value) // denominator)
    operations, roles = [], []
    counts = {"first_samples": 0, "ordinary_samples": 0, "split_samples": 0, "clamped_samples": 0,
              "extra_updates": 0, "primary_frames": 0, "warm_resets": 0}
    physical_in_frame = 0
    frame_sampled = False
    for index, record in enumerate(records):
        if record["id"] != index or type(record["id"]) is not int or index == 0 and record["combined_journal"]:
            raise ValueError("Ordered clock updates must start at the actual first PRE")
        previous_order = 0
        for entry in record["combined_journal"]:
            kind, event = entry["kind"], entry["event"]
            order = integer(event["order"], 1, 0x7fffffff, "actual clock source order")
            if order <= previous_order:
                raise ValueError("Repeated or reordered actual clock source operation")
            previous_order = order
            if kind == "primary":
                physical_in_frame = 0
                frame_sampled = False
                counts["primary_frames"] += 1
                operations.append((index, order, 0, 0))
            elif kind == "qpc_sample":
                if frame_sampled:
                    raise ValueError("Repeated actual QPC sample in one primary frame")
                frame_sampled = True
                if set(event) != {"order", "return_address", "counter", "pre", "post"} or event["return_address"] != QPC_SAMPLE_MANIFEST["return_address"]:
                    raise ValueError("Unexpected actual QPC sample caller or payload")
                for phase in ("pre", "post"):
                    if set(event[phase]) != set(QPC_SAMPLE_FIELDS):
                        raise ValueError("Incomplete actual QPC sample checkpoint")
                validate_qpc_values(event["pre"], state, f"update{index}/sample PRE")
                sampled = integer(event["counter"], 0, 0xffffffff, "external QPC sample")
                operations.append((index, order, 1, sampled))
                if state["clock_first_sample"]:
                    state.update(clock_first_sample=0, delta_q16=65536, reciprocal_q16=65536)
                    counts["first_samples"] += 1
                else:
                    difference = (sampled - state["qpc_low"] - int(state["qpc_low"] >= sampled)) & 0xffffffff
                    delta = signed(divide(signed(difference) * 65536, state["qpc_divisor"]))
                    extra = delta > 0x18000
                    if not extra: counts["ordinary_samples"] += 1
                    elif delta <= 0x30000:
                        delta = divide(delta, 2); counts["split_samples"] += 1
                    else:
                        delta = 0x18000; counts["clamped_samples"] += 1
                    if delta <= 0:
                        raise ValueError("Actual QPC sample is not a positive physical interval")
                    state.update(qpc_difference=difference, delta_q16=delta, reciprocal_q16=signed(divide(0x100000000, delta)), extra_update=extra)
                state["qpc_low"] = sampled
                validate_qpc_values(event["post"], state, f"update{index}/sample POST")
            elif kind == "warm_grid":
                before, after = validate_warm_grid_event(event)
                if struct.unpack("<I", before["clock_first_sample"])[0] != state["clock_first_sample"] or struct.unpack("<I", before["qpc_divisor"])[0] != state["qpc_divisor"]:
                    raise ValueError("Warm grid QPC reset entry disagrees with the retained clock")
                if struct.unpack("<I", after["clock_first_sample"])[0] != 1:
                    raise ValueError("Warm grid did not reset the source first-sample latch")
                state.update(clock_first_sample=1, qpc_divisor=integer(struct.unpack("<I", after["qpc_divisor"])[0], 1, 0x7fffffff, "observed reset QPF divisor"))
                operations.append((index, order, 2, state["qpc_divisor"]))
                counts["warm_resets"] += 1
        if physical_in_frame == 0:
            role = "retained" if index == 0 else "primary"
        elif physical_in_frame == 1 and state["extra_update"]:
            role = "extra"; counts["extra_updates"] += 1
        else:
            raise ValueError("Physical update has no actual primary or generated extra permit")
        physical_in_frame += 1
        roles.append(role)
        for phase in (*BOUNDARIES, *AI_BOUNDARIES, *TRAFFIC_BOUNDARIES):
            if phase in record:
                validate_qpc_values(record[phase], state, f"update{index}/{phase}")
    if physical_in_frame == 1 and state["extra_update"]:
        raise ValueError("Ordered clock interval ends before its sample/extra transaction is complete")
    tail = records[-1].get("scene_final_journal", [])
    if scene_autonomous_enabled(header):
        if any(record.get("scene_final_journal") for record in records[:-1]) or not tail or tail[-1]["kind"] != "qpc_sample" or sum(entry["kind"] == "qpc_sample" for entry in tail) != 1:
            raise ValueError("Joined scene requires exactly one terminal QPC sample after final physics")
        event = tail[-1]["event"]
        if set(event) != {"order", "return_address", "counter", "pre", "post"} or event["return_address"] != QPC_SAMPLE_MANIFEST["return_address"]:
            raise ValueError("Invalid terminal scene QPC provenance")
        validate_qpc_values(event["pre"], state, "terminal sample PRE")
        sampled = integer(event["counter"], 0, 0xffffffff, "terminal external QPC sample")
        operations.append((len(records), integer(event["order"], 1, 0x7fffffff, "terminal source order"), 1, sampled))
        if state["clock_first_sample"]:
            state.update(clock_first_sample=0, delta_q16=65536, reciprocal_q16=65536)
            counts["first_samples"] += 1
        else:
            difference = (sampled - state["qpc_low"] - int(state["qpc_low"] >= sampled)) & 0xffffffff
            delta = signed(divide(signed(difference) * 65536, state["qpc_divisor"]))
            extra = delta > 0x18000
            if not extra: counts["ordinary_samples"] += 1
            elif delta <= 0x30000:
                delta = divide(delta, 2); counts["split_samples"] += 1
            else:
                delta = 0x18000; counts["clamped_samples"] += 1
            if delta <= 0: raise ValueError("Terminal QPC sample is not a positive interval")
            state.update(qpc_difference=difference, delta_q16=delta, reciprocal_q16=signed(divide(0x100000000, delta)), extra_update=extra)
        state["qpc_low"] = sampled
        validate_qpc_values(event["post"], state, "terminal sample POST")
    elif tail:
        raise ValueError("Terminal scene events lack joined provenance")
    counts["menu_only_primary_frames"] = counts["primary_frames"] - roles.count("primary")
    return {"divisor": divisor, "initial_time": initial, "samples": [], "operations": operations,
            "roles": roles, "coverage": counts}


def startup_lifecycle_values(snapshot):
    lifecycle = snapshot["shared"]["lifecycle"]
    timers = struct.unpack("<5I", raw_hex(lifecycle["session_timers_hex"], 20, "source session timers"))
    player = raw_hex(lifecycle["player_lifecycle_hex"], 24, "source player lifecycle")
    race = raw_hex(lifecycle["race_lifecycle_hex"], 20, "source race lifecycle")
    clock = integer(snapshot["race_clock"], 0, 0xffffffff, "observed caller clock")
    if struct.unpack_from("<I", race, 8)[0] != clock or struct.unpack("<I", raw_hex(lifecycle["lifecycle_clock_hex"], 4, "source lifecycle clock"))[0] != clock:
        raise ValueError("Startup lifecycle caller clock aliases disagree")
    return (*timers, struct.unpack_from("<H", player, 22)[0],
            integer(snapshot["session_state"], 0, 0xffff, "source session state"), struct.unpack_from("<I", race, 12)[0], clock)


def startup_lifecycle_inputs(records, local_session=False):
    """Only the five initial origins and external caller clocks are inputs."""
    first = startup_lifecycle_values(records[0]["pre"])
    flags = struct.unpack("<4I", raw_hex(records[0]["pre"]["shared"]["provenance"]["noqual_flags_hex"], 16, "initial NoQual flags"))
    if first[5:8] != (0, 9, 0) or flags != (0, 1, 0, 0):
        raise ValueError("Startup lifecycle requires generated phase0/state9/unreleased ordinary NoQual entry")
    clocks, phases, states = [], set(), set()
    for record in records:
        values = startup_lifecycle_values(record["pre"])
        clocks.append(values[-1]); phases.add(values[5]); states.add(values[6])
        expected_flags = raw_hex(record["pre"]["shared"]["provenance"]["noqual_flags_hex"], 16, "NoQual flags")
        for phase in (*BOUNDARIES, *AI_BOUNDARIES, *TRAFFIC_BOUNDARIES):
            if phase not in record: continue
            snapshot = record[phase]
            actual_values = startup_lifecycle_values(snapshot)
            if (actual_values[-1] != values[-1] if local_session else actual_values != values):
                raise ValueError(f"Startup lifecycle aliases disagree at update{record['id']}/{phase}")
            shared = snapshot["shared"]
            flags = raw_hex(shared["provenance"]["noqual_flags_hex"], 16, "NoQual flags")
            if (not local_session and flags != expected_flags) or raw_hex(shared["traffic_control"]["status_hex"], 24, "traffic status")[:16] != flags:
                raise ValueError(f"Startup lifecycle NoQual aliases disagree at update{record['id']}/{phase}")
    return {"initial_origins": first[:5], "caller_clocks": clocks,
            "coverage": {"countdown_phases": sorted(phases), "session_states": sorted(states),
                         "release_events": sum(len(record.get("release_events", [])) for record in records)}}


def startup_clock_inputs(header, records):
    """Source outer-loop transactions; QPC equality never selects a role."""
    if not records or records[0]["id"] != 0:
        raise ValueError("Startup clock requires the complete initial source boundary")
    if struct.unpack_from("<I", bytes.fromhex(header["configuration_hex"]), 0x8c)[0]:
        raise ValueError("Startup clock excludes the source boost transform")
    if ordered_clock_enabled(header):
        return startup_journal_clock_inputs(header, records)
    first = records[0]["pre"]
    divisor = integer(first["qpc_divisor"], 1, 0x7fffffff, "observed stored QPC divisor")
    if integer(first["clock_first_sample"], 0, 1, "first-sample latch") != 1:
        raise ValueError("Startup clock requires the generated pending first-sample latch")
    initial = tuple(first[name] for name in STARTUP_CLOCK_FIELDS)
    if type(initial[2]) is not bool:
        raise ValueError("Initial retained extra-update latch must be boolean")
    integer(initial[0], 1, 0x30000, "initial retained delta")
    integer(initial[1], 1, 0x7fffffff, "initial retained reciprocal")
    def signed(value): return (value + 0x80000000) % 0x100000000 - 0x80000000
    def divide(value, denominator): return value // denominator if value >= 0 else -((-value) // denominator)
    counter = integer(first["qpc_low"], 0, 0xffffffff, "retained QPC low word")
    difference = integer(first["qpc_difference"], 0, 0xffffffff, "retained QPC difference")
    first_sample, pending_extra, extra_latch = True, initial[2], initial[2]
    step = initial
    samples, roles = [], []
    counts = {"first_samples": 0, "ordinary_samples": 0, "split_samples": 0, "clamped_samples": 0, "extra_updates": 0}
    for index, record in enumerate(records):
        if integer(record["id"], 0, 0xffffffff, "startup update ID") != index:
            raise ValueError("Startup clock update IDs must be consecutive from zero")
        if index == 0:
            role = "retained"
        elif pending_extra:
            role = "extra"
            pending_extra = False
            counts["extra_updates"] += 1
        else:
            role = "primary"
            sampled = integer(record["pre"]["qpc_low"], 0, 0xffffffff, "observed QPC sample")
            samples.append((index, sampled))
            if first_sample:
                first_sample = False
                step = (65536, 65536, extra_latch)
                counts["first_samples"] += 1
            else:
                difference = (sampled - counter - int(counter >= sampled)) & 0xffffffff
                delta = signed(divide(signed(difference) * 65536, divisor))
                if delta <= 0x18000:
                    extra_latch = False
                    counts["ordinary_samples"] += 1
                else:
                    extra_latch = True
                    if delta <= 0x30000:
                        delta = divide(delta, 2)
                        counts["split_samples"] += 1
                    else:
                        delta = 0x18000
                        counts["clamped_samples"] += 1
                if delta <= 0:
                    raise ValueError("Sampled source clock is not a positive physical interval")
                step = (delta, signed(divide(0x100000000, delta)), extra_latch)
            counter = sampled
            pending_extra = step[2]
        roles.append(role)
        expected = {"qpc_low": counter, "qpc_divisor": divisor, "qpc_difference": difference,
                    "clock_first_sample": int(first_sample)}
        for phase in (*BOUNDARIES, *AI_BOUNDARIES, *TRAFFIC_BOUNDARIES):
            if phase not in record:
                continue
            if any(integer(record[phase][name], 1 if name == "qpc_divisor" else 0,
                           0x7fffffff if name == "qpc_divisor" else 1 if name == "clock_first_sample" else 0xffffffff,
                           name) != value for name, value in expected.items()):
                raise ValueError(f"Startup clock counter/latch aliases disagree at update{index}/{phase}")
    if first_sample or pending_extra:
        raise ValueError("Startup clock interval ends before its sample/extra transaction is complete")
    return {"divisor": divisor, "initial_time": initial, "samples": samples, "roles": roles, "coverage": counts}


def write_startup_clock_binary(path, header, records):
    inputs = startup_clock_inputs(header, records)
    lifecycle = startup_lifecycle_inputs(records, retained_local_session_enabled(header))
    creation = cold_timer_creation(header)
    with Path(path).open("wb") as output:
        output.write(STARTUP_CLOCK_HEADER.pack(b"TOCACLOK", 3 if creation is not None else 2, len(records), inputs["divisor"],
                                              *inputs["initial_time"], len(inputs["operations"] if creation is not None else inputs["samples"])))
        if creation is not None:
            for operation in inputs["operations"]: output.write(struct.pack("<4I", *operation))
        else:
            for sample in inputs["samples"]: output.write(struct.pack("<2I", *sample))
        output.write(struct.pack("<I", creation["creation_clock"]) if creation is not None else struct.pack("<5I", *lifecycle["initial_origins"]))
        for clock in lifecycle["caller_clocks"]:
            output.write(struct.pack("<I", clock))


def compare_startup_clock(header, records, csv_path):
    """Count one clock/lifecycle checkpoint per update; require every alias."""
    import csv
    inputs = startup_clock_inputs(header, records)
    lifecycle = startup_lifecycle_inputs(records, retained_local_session_enabled(header))
    creation = cold_timer_creation(header)
    fields = (*STARTUP_CLOCK_FIELDS, *STARTUP_LIFECYCLE_FIELDS)
    mismatches = []
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None or len(reader.fieldnames) != len(set(reader.fieldnames)) or any(name not in reader.fieldnames for name in fields):
            raise ValueError("Native physical output omitted the generated clock/lifecycle fields")
        for index, record in enumerate(records):
            checkpoint = None
            for phase in ("integration", "ground"):
                for actor in range(ACTORS):
                    row = next(reader, None)
                    if row is None or (int(row["id"]), int(row["actor"]), row["phase"]) != (record["id"], actor, phase):
                        raise ValueError("Native clock rows changed source update/phase/actor chronology")
                    values = tuple(int(row[name]) for name in fields)
                    if checkpoint is None: checkpoint = values
                    elif values != checkpoint: raise ValueError("Native physical clock aliases are inconsistent")
            expected_values = (*[int(record["pre"][name]) for name in STARTUP_CLOCK_FIELDS], *startup_lifecycle_values(record["pre"]))
            for name, expected, actual in zip(fields, expected_values, checkpoint):
                if actual != expected:
                    mismatches.append({"update": index, "field": name, "expected": expected, "actual": actual})
        if next(reader, None) is not None:
            raise ValueError("Extra native physical clock rows")
    coverage = inputs["coverage"]
    lifecycle_coverage = lifecycle["coverage"]
    extended = cold_local_session_enabled(header)
    lifecycle_sufficient = (lifecycle_coverage["countdown_phases"] == [0, 1, 2, 3, 4]
                           and ({6, 9}.issubset(lifecycle_coverage["session_states"]) if extended else lifecycle_coverage["session_states"] == [6, 9])
                           and (lifecycle_coverage["release_events"] >= 1 if extended else lifecycle_coverage["release_events"] == 1))
    sample_count = sum(operation[2] == 1 for operation in inputs["operations"]) if creation is not None else len(inputs["samples"])
    sufficient = bool(coverage["first_samples"] and coverage["extra_updates"] and sample_count > 1 and lifecycle_sufficient)
    exact = not mismatches
    return {"parity": exact and sufficient, "numerical_parity": exact, "coverage_sufficient": sufficient,
            "compared_values": len(records) * len(fields), "clock_compared_values": len(records) * 3,
            "lifecycle_compared_values": len(records) * len(STARTUP_LIFECYCLE_FIELDS), "generated_time_values": (len(records) - 1) * 3,
            "derived_updates": len(records) - 1, "retained_initial_updates": 1,
            "inherited_initial_values": 3, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "physical_alias_checkpoints": len(records) * ACTORS * 2, "samples": sample_count, "coverage": coverage,
            "stored_divisor_observed": inputs["divisor"], "initial_retained_time_observed": dict(zip(STARTUP_CLOCK_FIELDS, inputs["initial_time"])),
            "lifecycle_coverage": lifecycle_coverage,
            **({"creation_clock_observed": creation["creation_clock"], "initial_timer_origins_expected": lifecycle["initial_origins"], "initial_timer_origin_imports": 0,
                "ordered_clock_operation_inputs": len(inputs["operations"])}
               if creation is not None else {"initial_timer_origins_observed": lifecycle["initial_origins"]}),
            "inputs": ("Observed divisor, first retained TimeStep/latch, every actual QPC sample and primary/warm-reset operation, one actual447a77 creation clock and external caller clocks. All five initial timer origins and later TimeSteps/phase/state/epoch/timers are expectations."
                       if creation is not None else "Observed divisor, first retained TimeStep/latch, primary QPC samples, five initial timer origins and external caller clocks. Source countdown/release/deferred state and extra-step transactions are generated; later phase/state/epoch/timers are expectations."),
            "remaining_context": ("External counter/callback/caller-clock schedule and other world/lifecycle branches; the observed creation clock is external time, with no inferred QPF or complete race parity."
                                  if creation is not None else "External counter/callback/caller-clock schedule, earlier timer-creation calls and other world/lifecycle branches; no inferred QPF or complete race parity.")}


def startup_world_reset_input(raw):
    """No cold contact state or unused numerical pair handles enter replay."""
    return bytes(len(raw))


def startup_world_reset_expected(records):
    values = startup_world_initial_values(records[0]["pre"])
    for phase in ("first_pre", "restored"):
        for field, index, value in values:
            yield {"phase": phase, "pair": index, "field": field, "value": value}


def startup_world_initial_values(snapshot):
    """Consumed cold state; exclude pointers, padding and dormant/free payloads."""
    shared = snapshot["shared"]
    raw = lambda name: bytes.fromhex(shared[name + "_hex"])
    values = [("touching", pair, struct.unpack_from("<I", raw("pair_matrix"), pair * 8 + 4)[0]) for pair in range(ACTORS * ACTORS)]
    values += [("roughness", 0, snapshot["roughness_cursor"]), ("damage_cycle", 0, shared["damage_cycle"]), ("detached_count", 0, shared["detached_count"])]
    values += [("damage_selection", index, value) for index, value in enumerate(raw("damage_scratch")[:15])]
    pool, free = raw("detached_pool"), raw("detached_free")
    for slot in range(10):
        values += [("detached_active", slot, struct.unpack_from("<I", pool, slot * 0x11c + 0x108)[0]),
                   ("detached_free", slot, struct.unpack_from("<I", free, slot * 4)[0])]
    levels, broken, barriers = raw("damage_levels"), raw("broken_features"), raw("barriers")
    for actor, entry in enumerate(snapshot["actors"]):
        body = bytes.fromhex(entry["body_hex"])
        values += [(f"damage_level_{level}", actor, levels[level * ACTORS + actor]) for level in range(17)]
        values += [(f"broken_feature_{feature}", actor, struct.unpack_from("<I", broken, actor * 16 + feature * 4)[0]) for feature in range(4)]
        values += [("damage_feedback_side", actor, body[0x514]), ("damage_feedback_strength", actor, struct.unpack_from("<H", body, 0x4ec)[0]),
                   ("barrier_active", actor, struct.unpack_from("<I", barriers, actor * 0x34 + 0x30)[0]), ("contact_count", actor, body[0x529])]
        for name, block, offset, size in STARTUP_COLD_ACTOR_FIELDS:
            record = bytes.fromhex(entry[block + "_hex"])
            form = "h" if name == "visual_angles" else "B" if name == "rollover_ticks" else "i"
            width = struct.calcsize("<" + form)
            values += [(f"cold_{name}_{index}", actor, struct.unpack_from("<" + form, record, offset + index * width)[0]) for index in range(size // width)]
        values += [("cold_limiter_active", actor, struct.unpack_from("<I", raw("limiter_active"), actor * 4)[0]),
                   ("cold_limiter_load", actor, struct.unpack_from("<i", raw("limiter_load"), actor * 4)[0]),
                   ("cold_identity_actor", actor, body[0x515]), ("cold_identity_car", actor, body[0x516]),
                   ("cold_identity_owner", actor, struct.unpack_from("<b", raw("ownership"), actor)[0])]
        dynamics, game = bytes.fromhex(entry["dynamics_hex"]), bytes.fromhex(entry["gameplay_hex"])
        values += [("cold_controller_human", actor, struct.unpack_from("<I", game, 8)[0])]
        values += [(f"cold_controller_mode_{index}", actor, struct.unpack_from("<i", dynamics, offset)[0]) for index, offset in enumerate((0x310, 0x314, 0x318, 0x27c))]
    def vector(prefix, index, record, offset):
        values.extend((prefix + f"_{axis}", index, value) for axis, value in enumerate(struct.unpack_from("<3i", record, offset)))
    def basis(prefix, index, record, offset, stride=12):
        for axis in range(3): vector(prefix + f"_{axis}", index, record, offset + axis * stride)
    for name in ("cones", "posts", "signs"):
        source = shared["props"][name]; records = bytes.fromhex(source["records_hex"])
        values.append((name + "_count", 0, source["count"]))
        models = bytes.fromhex(source.get("model_records_hex", ""))
        for index in range(source["count"]):
            record = records[index * source["stride"]:(index + 1) * source["stride"]]
            if name == "cones":
                basis("cone_axes", index, record, 0x48)
                for label, offset in (("position", 0x6c), ("velocity", 0x84), ("angular", 0x90)): vector("cone_" + label, index, record, offset)
                for label, offset, form in (("triangle", 0xb4, "H"), ("grounded", 0xb8, "I"), ("settled", 0xbc, "I"),
                                             ("state", 0xc0, "B"), ("translating", 0xc4, "I"), ("rotating", 0xc8, "I")):
                    values.append(("cone_" + label, index, struct.unpack_from("<" + form, record, offset)[0]))
            elif name == "posts":
                basis("post_saved", index, record, 0x48, 16)
                vector("post_saved_position", index, record, 0x78)
                basis("post_axes", index, record, 0xc8)
                for label, offset in (("position", 0xec), ("normal", 0x104), ("endpoint_0", 0x114), ("endpoint_1", 0x120)): vector("post_" + label, index, record, offset)
                for label, offset, form in (("degrees", 0x110, "i"), ("wait", 0x12c, "i"), ("state", 0x130, "B"), ("touched", 0x134, "I"), ("broken", 0x138, "I")):
                    values.append(("post_" + label, index, struct.unpack_from("<" + form, record, offset)[0]))
            else:
                for component, value in enumerate(struct.unpack_from("<16f", models, index * 68)):
                    scaled = value * 65536
                    values.append((f"sign_transform_{component}", index, int(scaled + 0.5 if scaled >= 0 else scaled - 0.5)))
                for label, offset in (("endpoint_0", 8), ("endpoint_1", 0x14), ("normal", 0x20)): vector("sign_" + label, index, record, offset)
                values += [("sign_state", index, struct.unpack_from("<I", record, 0x3c)[0]), ("sign_visible", index, int(models[index * 68 + 64] != 0))]
    return values


def compare_startup_world_reset(header, records, csv_path):
    import csv
    validate_startup_first_selection_scope(header, records)
    expected = list(startup_world_reset_expected(records))
    mismatches = []
    with Path(csv_path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["phase", "pair", "field", "value"]:
            raise ValueError("Unexpected startup world reset CSV header")
        for wanted in expected:
            actual = next(reader, None)
            if actual is None: raise ValueError("Missing startup world reset result")
            if (actual["phase"], int(actual["pair"]), actual["field"]) != (wanted["phase"], wanted["pair"], wanted["field"]):
                raise ValueError("Reordered startup world reset result")
            if int(actual["value"]) != wanted["value"]:
                mismatches.append({**wanted, "expected": wanted["value"], "actual": int(actual["value"])})
        if next(reader, None) is not None: raise ValueError("Extra startup world reset result")
    values = startup_world_initial_values(records[0]["pre"])
    zero_fields = {"touching", "roughness", "damage_cycle", "damage_selection", "detached_count", "detached_active", "damage_feedback_side", "damage_feedback_strength", "barrier_active", "contact_count"}
    checks = {"all_named_values_and_both_consumers": len(expected) == 2 * len(values),
              "observed_cold_process_and_grid_defaults": all(value == 0 for field, _, value in values if field in zero_fields or field.startswith(("damage_level_", "broken_feature_"))),
              "observed_consumed_cold_actor_defaults": all(value == 0 for field, _, value in values if field.startswith("cold_") and not field.startswith(("cold_identity_", "cold_controller_"))),
              "observed_free_parts": all(value == 1 for field, _, value in values if field == "detached_free"),
              "observed_registered_prop_baseline": not cold_prop_baseline(records[0]["pre"])}
    return {"parity": not mismatches and all(checks.values()), "numerical_parity": not mismatches,
            "coverage_sufficient": all(checks.values()), "coverage": checks, "compared_values": len(expected),
            "mismatch_count": len(mismatches), "first_mismatches": mismatches[:64], "pair_count": 256, "named_values_per_boundary": len(values),
            "initial_mutable_world_imports": 0, "later_mutable_world_imports": 0,
            "cold_actor_context_groups": len(STARTUP_COLD_ACTOR_FIELDS), "initial_actor_context_imports": 0, "initial_actor_identity_imports": 0,
            "phases": {phase: {"compared_values": len(values), "mismatch_count": sum(row["phase"] == phase for row in mismatches)} for phase in ("first_pre", "restored")},
            "boundary": "Generated cold grid identity and Motion/Controls constructor context, fresh-process shared world and canonical prop constructors, then live cold Fleet consumers before AI.",
            "source_writer": "Cold Motion/Controls first-read context and grid actor/car/controller identity; fresh roughness/damage persistence;4287f0 contacts,415d50 active/free parts,45c4c0 canonical props and actor damage/barrier/contact defaults.",
            "consumer": "Cold Fleet retains its own Motion/Controls constructor context and generated initial identity; neither initial actor-context nor initial/later mutable world imports are accepted.",
            "scope": "Consumed cold actor values, actual Motion/Controls identity, initial world and full registered prop geometry are compared; no positive contact transition, dormant/free-payload reset or later ownership lifetime is implied.",
            "remaining_context": "Authored resources, later lifecycle/frame ownership, root registration, model detail/visibility, contact enable, external clocks and unclaimed actor storage retain their existing contracts. Initial actor/car/controller identity and named consumed cold context are generated. Numerical pair handles, inactive barrier vectors, unread partners and dormant/free payloads are excluded; no render schedule or complete owner-lifetime generation is claimed."}


def startup_join_coverage(header, records, combined):
    """Bounded startup→retained-driver/RNG positives; old gates stay intact."""
    validate_startup_join_scope(header, records)
    activity = Coverage(records[0]["pre"])
    for record in records: activity.consume(record["consumed"], record["post"], record["ground"])
    source = activity.summary(header.get("scenario"))
    joined = combined["joined_ai"]
    tire = combined["wheel_queue"]["skid_dispatch"]["coverage"]
    whine = combined["whine"]
    checks = {
        "two_observed_initializations": len(header["cold_grid_sequence"]["events"]) == 2,
        "accepted_116_fields_survive_to_consumer": len(STARTUP_JOIN_ACTOR_FIELDS) == 116,
        "complete_64_or_more_updates": len(records) >= 64 and combined["requested_updates"] == combined["compared_updates"] == len(records),
        "ordinary_grid_to_active_race": {9, 6}.issubset(source["session_states"]) if cold_local_session_enabled(header) else set(source["session_states"]) == {9, 6},
        "all_sixteen_actors_positive": source["actors_moved"] == list(range(ACTORS)) and source["all_actors_moving_updates"] > 0,
        "positive_release_and_decrement": joined["positive_cold_release_coverage"],
        "positive_route_and_control_random": joined["positive_route_control_random_coverage"],
        "joined_coverage": joined["coverage_sufficient"],
        "positive_tire_random": tire["draws"] > 0,
        "positive_whine_random": whine["coverage"]["rng_advance_calls"] > 0,
        "retained_driver_checkpoints": combined["driver_state_checkpoint_values"] > 0 and combined["human_mapper_transactions"] > 0,
        "no_later_driver_or_rng_imports": combined["later_driver_imports"] == combined["later_rng_imports"] == 0,
    }
    if "brake_publication" in combined:
        checks["positive_publication_calls"] = combined["brake_publication"]["coverage_sufficient"]
    return {"sufficient": all(checks.values()), "checks": checks,
            "scope": "Actual two-call startup and116 first-consumer writers into a complete64-or-more-update retained-driver/shared-RNG interval; physical and frame inputs remain observed.",
            "source_activity": {name: source[name] for name in ("updates", "actors_moved", "session_states", "all_actors_moving_updates")},
            "observed_random_consumers": {"tire_draws": tire["draws"], "whine_random_calls": whine["coverage"]["rng_advance_calls"]},
            "uncovered_branches": {"positive_whine_commands": whine["positive_command_coverage"],
                                   "human_lamp_on_off": combined.get("human_lamps", {}).get("coverage_sufficient", False)},
            "legacy_combined_acceptance": {"parity": combined["parity"], "coverage_sufficient": combined["coverage_sufficient"],
                                           "unchanged": True, "scope": "The broader existing lamp/WHINE acceptance remains a separate diagnostic; it is not this startup join's coverage contract."}}


SKID_BLOCKS = {"enabled": (0x4d39e4, 4), "reset": (0x4d39f8, 64),
               "surface": (0x4d3a48, 16), "mask": (0x4d3de8, 16),
               "events": (0x4ef270, 128), "strengths": (0x4ef2f0, 256)}
SKID_MANIFEST = {"entry": 0x44c540, "callee": 0x424d50, "callee_return": 0x44c5a3,
                 "sample_pcs": {"0": 0x424eb2, "2": 0x424eed, "3": 0x424ec8},
                 "blocks": {name: list(value) for name, value in SKID_BLOCKS.items()},
                 "scope": "independent captured44c540 PRE wheel-audio dispatcher; queue producer and frame RNG excluded"}

WHEEL_QUEUE_MANIFEST = {"entry": 0x446b50, "producer": 0x44b690, "producer_returns": [0x446c9d, 0x446cf2],
                        "body_segment": [0x514, 8], "slip_segment": [0x1e4, 32], "flag_segment": [0x2c4, 32],
                        "visibility": [0x4ed968, 16], "pending_mute": [0x4d3a78, 64],
                        "render_bypass": [0x4dcd9e, 2],
                        "scope": "retained wheel queues/flags through actual intervening446b50 callbacks; physical/gate/callback/audio inputs observed"}

HUMAN_LAMPS_MANIFEST = {"entry": 0x446b50, "published_brake_body_offset": 0x4e8,
                        "reverse_pedal_dynamics_offset": 0x304, "lamp_body_offset": 0x508,
                        "human_gameplay_offset": 8, "actor_count": ACTORS,
                        "scope": "actual callback PRE/POST physical gates and lamp/human expectations; retained game8 and lamps, before render bypass"}
HUMAN_LAMPS_FIELDS = ("published_brake", "reverse_pedal", "lamps", "human_flags")

BRAKE_PUBLICATION_MANIFEST = {"entry": 0x4608b0, "store": 0x4610ce, "stores_end": 0x4610ed,
                              "raw_dynamics_offset": 0x144, "published_body_offset": 0x4e8,
                              "actor_order": list(range(ACTORS - 1, -1, -1)),
                              "scope": "every actual publication entry, descending store-loop end and return; tail consumers remain ordered; raw/published/RNG snapshots expectations only"}


def capture_brake_publication(reader, boundary):
    reader.preload([(0x4c5d6c, 4), (0x4b9e78, 4),
                    *((LAYOUT[block + "_base"] + actor * LAYOUT[block + "_stride"] + offset, 4)
                      for actor in range(ACTORS) for block, offset in (("dynamics", 0x144), ("body", 0x4e8)))])
    return {"boundary": boundary, "actor_count": struct.unpack_from("<h", reader.memory(0x4c5d6c, 4), 2)[0],
            "rng": reader.word(0x4b9e78),
            **{name + "_hex": b"".join(reader.memory(LAYOUT[block + "_base"] + actor * LAYOUT[block + "_stride"] + offset, 4)
                                      for actor in range(ACTORS)).hex()
               for name, block, offset in (("raw_brake", "dynamics", 0x144), ("published_brake", "body", 0x4e8))}}


def validate_brake_publication(snapshot, boundary):
    if snapshot["boundary"] != boundary or snapshot["actor_count"] != ACTORS:
        raise ValueError("Unexpected actual brake publication boundary/actor count")
    for name in ("raw_brake", "published_brake"): raw_hex(snapshot[name + "_hex"], 64, "publication " + name)
    integer(snapshot["rng"], 0, 0xffffffff, "publication RNG")

WHINE_STATE = {"previous_gear": 0x4be1a0, "phase": 0x4be1a4, "shift_ticks": 0x4be1f4,
               "ramp": 0x4be1e4, "throttle": 0x4be1e8, "throttle_mirror": 0x4be1ec, "last_volume": 0x4be214}
WHINE_MANIFEST = {"caller": 0x44a6f0, "engine": 0x414240, "whine": 0x414780, "offset_pc": 0x414825,
                  "playing": 0x4795f6, "rate": 0x479b21, "volume": 0x479b3d, "voice_pointer": 0x4be1d4,
                  "state": WHINE_STATE, "curve_header": [0x4ad488, 12], "curve_rates": [0x4ad494, 388],
                  "curve_gains": [0x4ad618, 194], "shift_offsets": [0x4ad820, 96],
                  "scope": "retained numeric WHINE state through actual frame callers; one prior update POST RNG per inter-update interval"}

COMBINED_AUDIO_BLOCKS = {"wheel_materials": (0x4d3ad8, 64), "surface": (0x4d3a48, 16),
                         "reset": (0x4d39f8, 64), "mask": (0x4d3de8, 16), "pending_mute": (0x4d3a78, 64)}
COMBINED_MANIFEST = {"surface": 0x425400, "mute": 0x425e90,
                     "audio_blocks": {name: list(value) for name, value in COMBINED_AUDIO_BLOCKS.items()},
                     "frame_order": ["release", "wheel", "motor", "mute"],
                     "scope": "one initial403dd0 RNG through all selected AI/tire/frame consumers; physical and callback observations remain inputs"}


def capture_combined_audio(reader):
    reader.preload([*COMBINED_AUDIO_BLOCKS.values(), (0x4b9e78, 4), (0x4d39e4, 4), (0x4d3d1c, 4),
                    (0x4c5d58, 16), (0x4c3a58, 4), (0x4c3a64, 12), (0x4d3ad0, 8)])
    return {**{name + "_hex": reader.memory(address, size).hex() for name, (address, size) in COMBINED_AUDIO_BLOCKS.items()},
            "rng": reader.word(0x4b9e78), "enabled": reader.word(0x4d39e4), "wet": reader.word(0x4d3d1c),
            "ownership_hex": reader.memory(0x4c5d58, 16).hex(), "showcase": reader.word(0x4c3a58),
            "camera_views_hex": reader.memory(0x4c3a66, 8).hex(), "camera_state_hex": reader.memory(0x4d3ad0, 8).hex()}


def validate_combined_audio(snapshot):
    for name, (_, size) in COMBINED_AUDIO_BLOCKS.items(): raw_hex(snapshot[name + "_hex"], size, "combined audio " + name)
    for name, size in (("ownership", 16), ("camera_views", 8), ("camera_state", 8)):
        raw_hex(snapshot[name + "_hex"], size, "combined " + name)
    for name in ("rng", "enabled", "wet", "showcase"): integer(snapshot[name], 0, 0xffffffff, "combined " + name)


# Combined events have no RNG payload. All observed RNG values remain expectations.
COMBINED_KINDS = {"boundary": 0, "ai": 1, "material": 2, "tire": 3, "wheel": 4, "motor": 5, "mute": 6, "release": 7, "impact": 8, "scrape": 9,
                  "publication": 10, "publication_return": 11, "primary": 12, "restart_request": 13, "restart": 14, "warm_grid": 15, "qpc_sample": 16, "camera_input": 18}
# Native Motion publication; source dispatch/registration and input fields stay external.
PHYSICAL_RANGES = {
    "body": ((0xb0, 12), (0xbc, 12), (0xc8, 12), (0xe0, 16), (0xf0, 96), (0x150, 36),
             (0x1c4, 32), (0x1e4, 12), (0x1f0, 24), (0x268, 12), (0x4ec, 2), (0x514, 1), (0x517, 8), (0x51f, 2), (0x521, 8)),
    "dynamics": ((0xfc, 16), (0x1b4, 48), (0x1e4, 32), (0x240, 36), (0x264, 16), (0x278, 4),
                 (0x280, 32), (0x2a0, 12), (0x2bc, 8), (0x31c, 4), (0x328, 8), (0x331, 3)),
}


COMBINED_PHASES = {"pre": 0, "ai_pre": 1, "consumed": 2, "contacts": 3, "ground": 4, "skid_pre": 5, "post": 6}


def combined_events(record):
    events = [(entry["kind"], None, entry["event"]) for entry in record["combined_journal"]]
    calls = record.get("contact_audio_calls", []) + record.get("publication_calls", [])
    def phase(name):
        return sorted([(entry["kind"], None, entry["event"]) for entry in calls if entry["event"]["phase"] == name],
                      key=lambda entry: entry[2]["order"])
    events += [("boundary", "pre", record["pre"])] + phase("pre_to_ai") + [("boundary", "ai_pre", record["ai_pre"]),
               ("ai", None, record["consumed"])]
    if "camera_input" in record: events.append(("camera_input", None, record["camera_input"]))
    events.append(("boundary", "consumed", record["consumed"]))
    events += phase("consumed_to_contacts") + [("boundary", "contacts", record["contacts"])]
    ground = [("material", None, call) for call in record["surface_calls"]] + phase("contacts_to_ground")
    if calls or any("order" in call for call in record["surface_calls"]): ground.sort(key=lambda entry: entry[2]["order"])
    events += ground + [("boundary", "ground", record["ground"])] + phase("ground_to_skid")
    events += [("boundary", "skid_pre", record["skid_dispatch"]["pre"]), ("tire", None, record["skid_dispatch"])]
    return events + phase("skid_to_post") + [("boundary", "post", record["post"])] + [(entry["kind"], None, entry["event"]) for entry in record.get("scene_final_journal", [])]


def validate_publication_scope(header, records):
    if header.get("brake_publication_manifest") != BRAKE_PUBLICATION_MANIFEST:
        raise ValueError("Unknown actual brake publication provenance")
    active, seen = None, set()
    phases = {"frame", "pre_to_ai", "consumed_to_contacts", "contacts_to_ground", "ground_to_skid", "skid_to_post"}
    for record in records:
        if not isinstance(record.get("publication_calls"), list) or len(record["publication_calls"]) > 2048:
            raise ValueError("Missing/invalid actual publication call list")
        for entries in (record["combined_journal"], record["publication_calls"], record.get("scene_final_journal", [])):
            orders = [integer(entry["event"]["order"], 1, 0x7fffffff, "source event order") for entry in entries]
            if orders != sorted(set(orders)): raise ValueError("Repeated/reordered source publication chronology")
        for kind, _, event in combined_events(record):
            if kind not in ("publication", "publication_return"): continue
            call = integer(event["call"], 0, 0xffffffff, "publication call")
            if event["phase"] not in phases: raise ValueError("Unpartitioned publication phase")
            integer(event["order"], 1, 0x7fffffff, "publication order")
            if kind == "publication":
                if active is not None or call in seen: raise ValueError("Repeated/nested actual publication")
                active = (call, event)
                seen.add(call)
                for name, boundary in (("pre", "4608b0-entry"), ("stores", "4610ed"), ("post", "4608b0-return")):
                    validate_brake_publication(event[name], boundary)
            else:
                if active is None or active[0] != call or active[1]["post"] != event["post"]:
                    raise ValueError("Unmatched actual publication return")
                active = None
    if active is not None: raise ValueError("Missing actual publication return")


def validate_combined_scope(header, records, compare_driver_retention=False):
    if header.get("combined_rng_manifest") != COMBINED_MANIFEST or not records:
        raise ValueError("Combined replay requires the complete source chronology manifest")
    validate_joined_ai_scope(header, records, retained_drivers=compare_driver_retention)
    validate_wheel_queue_scope(header, records)
    validate_whine_scope(header, records)
    validate_combined_audio(header["combined_audio_initial"])
    if header["combined_initial_rng"] != records[0]["ai_pre"]["rng"] or records[0]["pre"]["rng"] != header["combined_initial_rng"]:
        raise ValueError("Initial combined403dd0 seed differs from first update PRE")
    camera_controls = camera_controls_enabled(header)
    if camera_controls: validate_camera_controls(records)
    for index, record in enumerate(records):
        if camera_controls:
            validate_camera_input(record.get("camera_input"), record["id"])
            if not isinstance(record.get("camera_control_expected"), dict): raise ValueError("Missing actual camera mapper checkpoints")
        elif "camera_input" in record:
            raise ValueError("Camera inputs lack their ordinary-view manifest")
        prefix_journal = record["combined_journal"]
        tail = record.get("scene_final_journal", [])
        if not isinstance(prefix_journal, list) or not isinstance(tail, list):
            raise ValueError("Invalid complete combined callback chronology")
        if tail and (not scene_autonomous_enabled(header) or index != len(records) - 1 or any(entry["kind"] in ("primary", "restart_request", "restart", "warm_grid") for entry in tail)):
            raise ValueError("Unsupported terminal scene chronology")
        journal = prefix_journal + tail
        if not isinstance(journal, list) or len(journal) > 1024 or index == 0 and prefix_journal:
            raise ValueError("Invalid complete combined callback chronology")
        for kind, field in (("wheel", "wheel_queue_callbacks"), ("motor", "whine_callbacks"), ("release", "release_events")):
            if [entry["event"] for entry in journal if entry["kind"] == kind] != record[field]:
                raise ValueError("Combined journal differs from component chronology: " + kind)
        allowed = {"wheel", "motor", "mute", "release"}
        if ordered_clock_enabled(header): allowed.add("qpc_sample")
        if retained_local_session_enabled(header): allowed.add("primary")
        if retained_warm_restart_enabled(header): allowed.update(("restart_request", "restart", "warm_grid"))
        if "contact_weather_manifest" in header: allowed.update(("impact", "scrape"))
        if "brake_publication_manifest" in header: allowed.update(("publication", "publication_return"))
        if any(entry["kind"] not in allowed for entry in journal):
            raise ValueError("Unknown combined frame consumer")
        for name in ("combined_audio_pre", "combined_audio_post"):
            validate_combined_audio(record[name])
        for call in record["surface_calls"]:
            integer(call["actor"], 0, 15, "material actor")
            integer(call["wheel"], 0, 3, "material wheel")
            if call["material"] not in (0, 1, 2, 3, 16):
                raise ValueError("Unsupported original material input")
            for phase in ("pre", "post"): validate_combined_audio(call[phase])
        for entry in journal:
            if entry["kind"] == "mute":
                for phase in ("pre", "post"): validate_combined_audio(entry["event"][phase])
        for phase in ("pre", "post"): validate_combined_audio(record["skid_dispatch"]["audio_" + phase])
    if "contact_weather_manifest" in header: validate_contact_weather_scope(header, records)
    if "brake_publication_manifest" in header: validate_publication_scope(header, records)
    if retained_local_session_enabled(header): validate_local_session_lifecycle(header, records)
    if retained_warm_restart_enabled(header): retained_warm_restart_coverage(header, records)
    if ordered_clock_enabled(header): startup_clock_inputs(header, records)
    return records


def validate_retained_lamp_callback_scope(header, records):
    """A paused UI frame is allowed only before the actual warm restart."""
    message = "Retained launch/lamp interval requires the unpaused keys-only source scope"
    if header["wheel_queue_initial_state"]["render_bypass"]:
        raise ValueError(message)
    warm = retained_warm_restart_enabled(header)
    previous_state, restarted = records[0]["pre"]["session_state"], False
    for record in records:
        state = previous_state
        for entry in record["combined_journal"]:
            kind, event = entry["kind"], entry["event"]
            if kind == "wheel" and any(event[phase]["render_bypass"] for phase in ("pre", "post")):
                if not warm or state != 13 or restarted:
                    raise ValueError(message)
            elif kind == "restart":
                state, restarted = 9, True
        previous_state = record["ai_post"]["session_state"]


def combined_motor_payload(callback, retained_drivers=False, drivetrain_feedback=False, rpm_feedback=False, pedal_feedback=False, local_session=False):
    pre = callback["pre"]
    payload = struct.pack("<3I", pre["enabled"], pre["local_players"], 0 if local_session else pre["session_state"] & 0xffffffff) + bytes.fromhex(pre["local_actors_hex"])
    for physical, engine in zip(pre["physical"], callback["engines"]):
        values = [struct.unpack("<i", bytes.fromhex(physical[name + "_hex"]))[0] for name in ("engine_speed", "rpm_scale", "throttle")]
        if rpm_feedback: values[0] = 0
        if pedal_feedback: values[2] = 0
        values += [0 if drivetrain_feedback else struct.unpack("<b", bytes.fromhex(physical["gear_hex"]))[0],
                   0 if retained_drivers else struct.unpack("<i", bytes.fromhex(physical["speed_hex"]))[0]]
        whine = engine["whine"]
        mix = whine[0]["pre"]["mix"] if whine else engine["pre"]["mix"]
        playing = whine[0]["queries"][0]["playing"] if whine and whine[0]["queries"] else 0
        payload += struct.pack("<5i2I", *values, mix, playing)
    return payload


def retained_human_actors(records):
    """Initialized controller identities, never later observed driver flags."""
    return [actor for actor, entry in enumerate(records[0]["ai_pre"]["actors"])
            if struct.unpack_from("<b", bytes.fromhex(entry["gameplay_hex"]), 0x80)[0] >= 0
            and struct.unpack_from("<I", bytes.fromhex(entry["gameplay_hex"]), 8)[0] != 0]


def write_combined_binary(path, header, records, compare_driver_retention=False, drivetrain_feedback=False, rpm_feedback=False, limiter_feedback=False, pedal_feedback=False, control_feedback=False, physical_feedback=False, dispatch_feedback=False, startup_join=False, startup_physical_body=False, startup_world_reset=False, scene_owned=False):
    if scene_owned and (not startup_world_reset or not (scene_autonomous_enabled(header) or scene_lifecycle_enabled(header))):
        raise ValueError("Scene ownership requires the joined autonomous cold profile")
    if startup_world_reset and not startup_physical_body:
        raise ValueError("Startup lifecycle journal requires the startup physical BODY handoff")
    if dispatch_feedback and not physical_feedback:
        raise ValueError("Dispatch feedback requires physical feedback")
    if physical_feedback and not control_feedback:
        raise ValueError("Physical feedback requires control feedback")
    if control_feedback and not pedal_feedback:
        raise ValueError("Control feedback requires pedal feedback")
    if pedal_feedback and not limiter_feedback:
        raise ValueError("Pedal feedback requires limiter feedback")
    if limiter_feedback and not rpm_feedback:
        raise ValueError("Limiter feedback requires RPM feedback")
    if rpm_feedback and not drivetrain_feedback:
        raise ValueError("RPM feedback requires drivetrain feedback")
    if drivetrain_feedback and not compare_driver_retention:
        raise ValueError("Drivetrain feedback requires retained drivers")
    if startup_physical_body and not (startup_join and dispatch_feedback):
        raise ValueError("Startup physical BODY requires startup and continuous dispatch feedback")
    if startup_join:
        if not compare_driver_retention or not startup_physical_body and any((drivetrain_feedback, rpm_feedback, limiter_feedback, pedal_feedback, control_feedback, physical_feedback, dispatch_feedback)):
            raise ValueError("Startup join requires retained-driver/shared-RNG mode without physical feedback")
        (validate_startup_first_selection_scope if startup_physical_body else validate_startup_join_scope)(header, records)
    validate_combined_scope(header, records, compare_driver_retention=compare_driver_retention)
    initial = wheel_arrays(header["wheel_queue_initial_state"])
    audio = header["combined_audio_initial"]
    tables = whine_tables(header["whine_tables"])
    with Path(path).open("wb") as output:
        contact = "contact_weather_manifest" in header
        lamps = compare_driver_retention and "human_lamps_manifest" in header
        publication = lamps and "brake_publication_manifest" in header
        local_session = compare_driver_retention and retained_local_session_enabled(header)
        warm_restart = local_session and retained_warm_restart_enabled(header)
        ordered_clock = ordered_clock_enabled(header)
        if ordered_clock and not (startup_world_reset and (warm_restart or scene_owned and scene_autonomous_enabled(header))):
            raise ValueError("Ordered QPC journal requires the complete cold warm-start clock comparison")
        if local_session and not publication:
            raise ValueError("Retained local session requires publication/lamp provenance")
        output.write(struct.pack("<8sIII", b"TOCACOMB", 9 if camera_controls_enabled(header) else 8 if ordered_clock else 7 if warm_restart else 6 if local_session else 5 if publication else 4 if lamps else 3 if compare_driver_retention else 2 if contact else 1, len(records), 0 if startup_join else header["combined_initial_rng"]))
        if compare_driver_retention:
            output.write(struct.pack("<I", contact))
        if contact:
            initial_contact = header["contact_audio_initial"]
            output.write(struct.pack("<I", header["combined_initial_release_counter"]))
            output.write(bytes.fromhex(initial_contact["scale_hex"]))
            output.write(bytes.fromhex(initial_contact["gains_hex"]))
            output.write(struct.pack("<I", initial_contact["effect_flag"]))
        elif compare_driver_retention:
            # No contact callbacks exist in the older capture profile. These
            # unused fields do not invent live spatial initialization.
            output.write(bytes(36))
        if lamps:
            validate_retained_lamp_callback_scope(header, records)
            if any(struct.unpack_from("<I", bytes.fromhex(record["pre"]["shared"]["provenance"]["world_status_hex"]), 0x18)[0] for record in records):
                raise ValueError("Retained launch first-wrapper reset excludes showcase/watch mode")
            output.write(raw_hex(records[0]["pre"]["shared"]["provenance"]["local_launch_tires_hex"], 16, "initial local launch")[:4])
        if publication:
            output.write(bytes(16 * 4) if startup_join else b"".join(bytes.fromhex(actor["body_hex"])[0x4e8:0x4ec] for actor in records[0]["pre"]["actors"]))
        if local_session:
            lifecycle = validate_local_session_lifecycle(header, records)
            output.write(struct.pack("<7I", *((0,) * 5 + lifecycle[5:] if startup_world_reset else lifecycle)))
        for name in ("events", "strengths", "current", "previous"): output.write(initial[name])
        for name in ("reset", "surface", "mask", "wheel_materials", "pending_mute", "camera_state"):
            output.write(bytes.fromhex(audio[name + "_hex"]))
        output.write(struct.pack("<7I", *(header["whine_initial_state"]["state"][name] for name in WHINE_STATE)))
        for name in ("header", "rates", "gains", "offsets"): output.write(tables[name])
        for record in records:
            events = combined_events(record)
            output.write(struct.pack("<2I", record["id"], len(events)))
            for kind, phase, event in events:
                output.write(struct.pack("<I", COMBINED_KINDS[kind]))
                if kind == "boundary": output.write(struct.pack("<I", COMBINED_PHASES[phase]))
                elif kind == "material":
                    pre = event["pre"]
                    owner = bytes.fromhex(pre["ownership_hex"])[event["actor"]]
                    views = struct.unpack("<4h", bytes.fromhex(pre["camera_views_hex"]))
                    view = views[owner] if owner < 4 else 0
                    output.write(struct.pack("<8I", event["actor"], event["wheel"], event["material"], pre["enabled"], owner != 255, pre["wet"], pre["showcase"], 0 if scene_owned else view & 0xffffffff))
                elif kind == "tire":
                    pre = event["pre"]
                    output.write(bytes.fromhex(pre["enabled_hex"]))
                    for name in ("ownership", "identities", "wheel_kinds"): output.write(bytes.fromhex(pre[name + "_hex"]))
                elif kind == "wheel":
                    pre = event["pre"]
                    arrays = wheel_arrays(pre)
                    output.write(struct.pack("<4I", event["view"], pre["local_players"], bool(pre["render_bypass"]), pre["actor_count"]))
                    output.write(bytes.fromhex(pre["ownership_hex"]))
                    output.write(bytes(ACTORS) if scene_owned else bytes.fromhex(pre["visibility_hex"]))
                    for name in ("identities", "airborne", "longitudinal", "lateral"): output.write(bytes(len(arrays[name])) if physical_feedback and name != "identities" else arrays[name])
                    if lamps:
                        for name in (("reverse_pedal",) if publication else ("published_brake", "reverse_pedal")):
                            output.write(bytes(64) if drivetrain_feedback and name == "reverse_pedal" else raw_hex(pre[name + "_hex"], 64, "actual callback " + name))
                elif kind in ("publication", "publication_return"):
                    if not publication: raise ValueError("Publication requires retained v5 mode")
                    output.write(struct.pack("<I", event["call"]))
                elif kind == "motor": output.write(combined_motor_payload(event, retained_drivers=compare_driver_retention, drivetrain_feedback=drivetrain_feedback, rpm_feedback=rpm_feedback, pedal_feedback=pedal_feedback, **({"local_session": True} if local_session else {})))
                elif kind == "primary": output.write(struct.pack("<I", event["cached_clock"]))
                elif kind == "camera_input":
                    if not scene_owned or not camera_controls_enabled(header): raise ValueError("Camera keys require generated scene ownership")
                    value = validate_camera_input(event, record["id"])
                    output.write(struct.pack("<3I", value["physical_id"], value["cycle_held"], value["rear_held"]))
                elif kind == "qpc_sample":
                    if not ordered_clock: raise ValueError("QPC sample lacks the ordered clock contract")
                    output.write(struct.pack("<I", event["counter"]))
                elif kind == "restart_request":
                    if not warm_restart or event["action"] != "confirm": raise ValueError("Unsupported warm restart confirmation")
                    output.write(struct.pack("<I", 1))
                elif kind == "restart": output.write(struct.pack("<I", event["creation_clock"]))
                elif kind == "warm_grid":
                    validate_warm_grid_event(event)
                    output.write(struct.pack("<2I", event["outer_return_address"], event["initialization"]["return_address"]))
                elif kind in ("impact", "scrape"): output.write(contact_payload(kind, event))
                elif kind == "release" and compare_driver_retention:
                    output.write(struct.pack("<I", 0 if startup_world_reset or warm_restart else event["pre"]["race_clock"]))
                    if lamps:
                        #44888b→421f40→448130 returns before44889c→421aa0.
                        #No physical RPM writer runs before its403dd0 PRE;
                        #the same raw physical context is safe at this call.
                        physical = bytes.fromhex(record["ai_pre"]["actors"][header["local_actor"]]["dynamics_hex"])
                        output.write(bytes(4) if rpm_feedback else physical[0x124:0x128])
                        output.write(physical[0x20:0x24])
                elif kind == "ai" and compare_driver_retention:
                    output.write(struct.pack("<I", 0 if startup_world_reset or warm_restart else record["ai_pre"]["race_clock"]))
                    humans = retained_human_actors(records)
                    if local_session:
                        humans = [actor for actor in humans if struct.unpack_from("<I", bytes.fromhex(record["consumed"]["actors"][actor]["gameplay_hex"]), 8)[0] != 0]
                    output.write(struct.pack("<I", len(humans)))
                    for actor in humans:
                        # 421aa0 calls the human mapper422f60 between403dd0
                        # and415090. Its observed axes are external inputs,
                        # never evidence of autonomous keyboard mapping.
                        output.write(struct.pack("<I", actor))
                        output.write(bytes.fromhex(record["consumed"]["actors"][actor]["dynamics_hex"])[0x138:0x148])


def combined_expected(header, records):
    """Source snapshots are expectations only; unchanged fields carry across events."""
    state = {name: bytes.fromhex(header["combined_audio_initial"][name + "_hex"]) for name in (*COMBINED_AUDIO_BLOCKS, "camera_state")}
    queue = wheel_arrays(header["wheel_queue_initial_state"])
    engine = dict(header["whine_initial_state"]["state"])
    rng = header["combined_initial_rng"]
    output = {".combined-rng.csv": [], ".combined-audio.csv": [], ".combined-engine.csv": []}
    audio_names = ["dom", "reset", "mask", "pending", *(f"material{i}" for i in range(4)), *(f"current{i}" for i in range(4)), *(f"previous{i}" for i in range(4)), *(f"event{i}" for i in range(4)), *(f"strength{i}" for i in range(4)), "primary", "secondary"]
    def signed(value): return value if value < 0x80000000 else value - 0x100000000
    def audio_snapshot(raw):
        nonlocal rng
        for name in state: state[name] = bytes.fromhex(raw[name + "_hex"])
        rng = raw["rng"]
    def wheel_snapshot(raw):
        queue.update(wheel_arrays(raw))
        state["pending_mute"] = bytes.fromhex(raw["pending_mute_hex"])
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind == "boundary":
                rng = event["rng"]
                if phase in ("pre", "post"):
                    audio_snapshot(record["combined_audio_" + phase])
                    engine = dict(record["whine_update_" + phase]["state"])
                elif phase == "skid_pre": audio_snapshot(record["skid_dispatch"]["audio_pre"])
            elif kind == "ai": rng = event["rng"]
            elif kind in ("material", "mute", "impact", "scrape"): audio_snapshot(event["post"])
            elif kind == "tire": audio_snapshot(event["audio_post"])
            elif kind == "wheel":
                wheel_snapshot(event["post"])
                rng = event["post"]["rng"]
            elif kind == "motor":
                engine = dict(event["post"]["state"])
                rng = event["post"]["rng"]
            elif kind == "release" or kind == "publication_return": rng = event["post"]["rng"]
            elif kind == "warm_grid":
                rng = event["initialization"]["post"]["rng"]
                for name in ("current", "previous"): queue[name] = bytes(ACTORS * 4 * 4)
            elif kind == "publication": rng = event["stores"]["rng"]
            metadata = {"id": record["id"], "event": ordinal}
            output[".combined-rng.csv"].append({**metadata, "kind": COMBINED_KINDS[kind], "rng": rng})
            output[".combined-engine.csv"].append({**metadata, **dict(zip(("previous_gear", "phase", "shift", "ramp", "throttle", "mirror", "volume"), (value if name == "phase" else signed(value) for name, value in ((name, engine[name]) for name in WHINE_STATE))))})
            resets, pending, camera = struct.unpack("<16i", state["reset"]), struct.unpack("<16i", state["pending_mute"]), struct.unpack("<2i", state["camera_state"])
            events = struct.unpack("<64h", queue["events"])
            strengths, current, previous = (struct.unpack("<64i", queue[name]) for name in ("strengths", "current", "previous"))
            for actor in range(ACTORS):
                base = actor * 4
                values = [state["surface"][actor], resets[actor], state["mask"][actor], pending[actor], *state["wheel_materials"][base:base + 4], *current[base:base + 4], *previous[base:base + 4], *events[base:base + 4], *strengths[base:base + 4], *camera]
                output[".combined-audio.csv"].append({**metadata, "actor": actor, **dict(zip(audio_names, values))})
    return output



def combined_pre_expectations(records):
    """Match actual callee PRE checkpoints to the preceding native event state."""
    previous = None
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if previous is not None and kind in ("material", "mute", "wheel", "motor", "release", "impact", "scrape", "publication"):
                pre = event["pre"]
                metadata = {"update": record["id"], "event": ordinal}
                yield ".combined-rng.csv", previous, None, {"rng": pre["rng"]}, metadata
                if kind in ("material", "mute", "impact", "scrape"):
                    reset = struct.unpack("<16i", bytes.fromhex(pre["reset_hex"]))
                    pending = struct.unpack("<16i", bytes.fromhex(pre["pending_mute_hex"]))
                    materials = bytes.fromhex(pre["wheel_materials_hex"])
                    camera = struct.unpack("<2i", bytes.fromhex(pre["camera_state_hex"]))
                    for actor in range(ACTORS):
                        values = {"dom": bytes.fromhex(pre["surface_hex"])[actor], "mask": bytes.fromhex(pre["mask_hex"])[actor],
                                  "reset": reset[actor], "pending": pending[actor], "primary": camera[0], "secondary": camera[1],
                                  **{f"material{i}": materials[actor * 4 + i] for i in range(4)}}
                        yield ".combined-audio.csv", previous, actor, values, metadata
                elif kind == "wheel":
                    arrays = wheel_arrays(pre)
                    pending = struct.unpack("<16i", bytes.fromhex(pre["pending_mute_hex"]))
                    for actor in range(ACTORS):
                        values = {"pending": pending[actor]}
                        for name, singular, code in (("events", "event", "h"), ("strengths", "strength", "i"), ("current", "current", "i"), ("previous", "previous", "i")):
                            raw = struct.unpack("<64" + code, arrays[name])
                            values.update({f"{singular}{i}": raw[actor * 4 + i] for i in range(4)})
                        yield ".combined-audio.csv", previous, actor, values, metadata
                elif kind == "motor":
                    names = ("previous_gear", "phase", "shift", "ramp", "throttle", "mirror", "volume")
                    values = {output: value if name == "phase" or value < 0x80000000 else value - 0x100000000
                              for output, name in zip(names, WHINE_STATE) for value in [pre["state"][name]]}
                    yield ".combined-engine.csv", previous, None, values, metadata
            previous = (record["id"], ordinal)


def driver_retention_expected(header, records):
    """Complete retained state, independently read from source checkpoints."""
    snapshot = records[0]["pre"]
    games = [bytes.fromhex(actor["gameplay_hex"]) for actor in snapshot["actors"]]
    status = bytes.fromhex(snapshot["shared"]["traffic_control"]["status_hex"])
    braking = bytes.fromhex(snapshot["shared"]["traffic_control"]["braking_memory_hex"])
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind == "release":
                games = [bytearray(game) for game in games]
                struct.pack_into("<I", games[header["local_actor"]], 0xc8, event["pre"]["race_clock"])
                status = bytearray(status)
                struct.pack_into("<I", status, 8, 1)
            elif kind == "ai" or kind == "boundary" and phase in ("pre", "ai_pre", "contacts", "post"):
                snapshot = event
                games = [bytes.fromhex(actor["gameplay_hex"]) for actor in snapshot["actors"]]
                status = bytes.fromhex(snapshot["shared"]["traffic_control"]["status_hex"])
                braking = bytes.fromhex(snapshot["shared"]["traffic_control"]["braking_memory_hex"])
            else:
                continue
            metadata = {"id": record["id"], "event": ordinal}
            for actor, game in enumerate(games):
                for word, value in enumerate(struct.unpack("<73I", game)):
                    yield ".driver-gameplay.csv", {**metadata, "actor": actor, "word": word, "value": value}
            for category, raw in enumerate((status, braking)):
                for word, (value,) in enumerate(struct.iter_unpack("<I", raw)):
                    yield ".driver-world.csv", {**metadata, "kind": category, "word": word, "value": value}
            if "human_lamps_manifest" in header and kind != "release":
                launch = bytes.fromhex(snapshot["shared"]["provenance"]["local_launch_tires_hex"])
                yield ".driver-world.csv", {**metadata, "kind": 2, "word": 0, "value": struct.unpack_from("<I", launch)[0]}
            if kind == "ai" and retained_local_session_enabled(header):
                for word, value in enumerate(local_session_lifecycle_values(snapshot)):
                    yield ".driver-world.csv", {**metadata, "kind": 3, "word": word, "value": value}
            # Physical integration owns the later gear/reverse-pedal values.
            # These two complete controls checkpoints bracket only AI itself.
            if kind == "ai" or kind == "boundary" and phase == "ai_pre":
                for actor, entry in enumerate(snapshot["actors"]):
                    dynamics, body = (bytes.fromhex(entry[name + "_hex"]) for name in ("dynamics", "body"))
                    values = dict(zip(("left", "right", "throttle", "brake"), struct.unpack_from("<4i", dynamics, 0x138)))
                    values.update(gear=struct.unpack_from("<b", dynamics, 0x330)[0],
                                  reverse_pedal=struct.unpack_from("<i", dynamics, 0x304)[0],
                                  headlights=struct.unpack_from("<I", body, 0x500)[0],
                                  brake_lights=struct.unpack_from("<I", body, 0x508)[0])
                    values.update(zip((f"tire{i}" for i in range(8)), struct.unpack_from("<8i", dynamics, 0xdc)))
                    yield ".driver-controls.csv", {**metadata, "actor": actor, **values}


def compare_driver_retention(header, records, prefix):
    import csv
    from contextlib import ExitStack
    suffixes = (".driver-gameplay.csv", ".driver-world.csv", ".driver-controls.csv")
    mismatches, failed, counts, values = [], set(), dict.fromkeys(suffixes, 0), 0
    humans = set(retained_human_actors(records))
    human_checkpoints = {(record["id"], ordinal, actor) for record in records
                         for ordinal, (kind, phase, snapshot) in enumerate(combined_events(record))
                         if kind == "ai" or kind == "boundary" and phase == "ai_pre"
                         for actor in humans if struct.unpack_from("<I", bytes.fromhex(snapshot["actors"][actor]["gameplay_hex"]), 8)[0] != 0}
    unclaimed_checkpoints = {(record["id"], ordinal, header["local_actor"]) for record in records
                            for ordinal, (kind, phase, snapshot) in enumerate(combined_events(record))
                            if kind == "boundary" and phase == "ai_pre"
                            and local_state13_wrapper_axes_unclaimed(header, snapshot, header["local_actor"])}
    unclaimed = 0
    with ExitStack() as stack:
        readers = {suffix: csv.DictReader(stack.enter_context(Path(str(prefix) + suffix).open(newline=""))) for suffix in suffixes}
        for suffix, wanted in driver_retention_expected(header, records):
            reader = readers[suffix]
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected retained driver output header: " + suffix)
            row = next(reader, None)
            if row is None: raise ValueError("Missing retained driver checkpoint: " + suffix)
            actual = {key: int(value) for key, value in row.items()}
            identity = {"id", "event", "actor", "word", "kind"}
            if any(actual[key] != wanted[key] for key in wanted.keys() & identity):
                raise ValueError("Retained driver output changed source chronology: " + suffix)
            counts[suffix] += 1
            for key in wanted.keys() - identity:
                if suffix == ".driver-controls.csv" and (wanted["id"], wanted["event"], wanted["actor"]) in unclaimed_checkpoints and key in ("left", "right", "throttle", "brake"):
                    unclaimed += 1
                    continue
                if suffix == ".driver-controls.csv" and (wanted["id"], wanted["event"], wanted["actor"]) in human_checkpoints and key in ("left", "right", "throttle", "brake"):
                    continue
                values += 1
                if actual[key] != wanted[key]:
                    failed.add(wanted["id"])
                    if len(mismatches) < 100:
                        mismatches.append({"update": wanted["id"], "event": wanted["event"], "actor": wanted.get("actor"),
                                           "word": wanted.get("word"), "field": suffix + "." + key,
                                           "expected": wanted[key], "actual": actual[key]})
                    counts.setdefault("mismatches", 0)
                    counts["mismatches"] += 1
        if any(next(reader, None) is not None for reader in readers.values()):
            raise ValueError("Extra retained driver checkpoints")
    mismatch_count = counts.pop("mismatches", 0)
    return {"parity": not mismatch_count, "compared_values": values, "mismatch_count": mismatch_count,
            "first_mismatches": mismatches, "exact_prefix_updates": min(failed, default=len(records)),
            "row_counts": counts, "initial_driver_state_imports": 1, "later_driver_state_imports": 0,
            **local_state13_axes_scope(header, unclaimed, "403dd0-entry", ("left", "right", "throttle", "brake")),
            "braking_history_bytes_retained": 1212, "complete_gameplay_bytes_per_actor": 0x124,
            "human_mapper_transactions": sum(struct.unpack_from("<I", bytes.fromhex(record["consumed"]["actors"][actor]["gameplay_hex"]), 8)[0] != 0 for record in records for actor in humans),
            **({"launch_scalar_initializations": 1, "later_launch_scalar_imports": 0,
                "launch_scalar_checkpoint_values": len(records) * 5,
                "launch_physical_context": "release RPM/max from following403dd0 PRE; ordinary421f40→448130→448890→421aa0 path has no intervening physical writer",
                "launch_scope": "unpaused keys-only mode0 first-lap interval; no menu requests/showcase;422f60 advances shared scalar and8tires, first448434 wrapper restores player tires"}
               if "human_lamps_manifest" in header else {}),
            "human_mapper_source": "421d24 calls422f60, returns421d29, after403dd0 return and before415090 entry in selected state9/6; observed mapped axes only",
            "human_axis_output_parity": None,
            "excluded_observed_control_fields": ["human left/right/throttle/brake at AI_PRE and consumed checkpoints"],
            "inputs": "one initial gameplay/driver/status/braking/light/tire state; later physical body/gear/reverse-pedal, human PRE axes and explicit422f60 mapper transactions plus session/callback context observed",
            "outside_compared_fields": ["autonomous physics", "human keyboard mapping", "pit/finish/lap writers outside the bounded first-lap interval"]}


def human_lamps_expected(records):
    """Every actual callback, including bypassed/invisible actors and views."""
    for record in records:
        for ordinal, (kind, _, event) in enumerate(combined_events(record)):
            if kind != "wheel": continue
            pre, post = event["pre"], event["post"]
            arrays = {phase: {name: struct.unpack("<16" + ("I" if name == "lamps" else "i"),
                                                  raw_hex(state[name + "_hex"], 64, "actual lamp " + name))
                              for name in HUMAN_LAMPS_FIELDS}
                      for phase, state in (("pre", pre), ("post", post))}
            for actor in range(ACTORS):
                yield {"id": record["id"], "event": ordinal, "actor": actor, "view": event["view"],
                       "human_pre": arrays["pre"]["human_flags"][actor], "human_post": arrays["post"]["human_flags"][actor],
                       "published_pre": arrays["pre"]["published_brake"][actor], "published_post": arrays["post"]["published_brake"][actor],
                       "pedal_pre": arrays["pre"]["reverse_pedal"][actor], "pedal_post": arrays["post"]["reverse_pedal"][actor],
                       "lamp_pre": arrays["pre"]["lamps"][actor], "lamp_post": arrays["post"]["lamps"][actor],
                       "rng_pre": pre["rng"], "rng_post": post["rng"]}


def compare_human_lamps(header, records, prefix):
    import csv
    from collections import Counter
    if header.get("human_lamps_manifest") != HUMAN_LAMPS_MANIFEST:
        raise ValueError("Retained human lamps require actual callback gate provenance")
    mismatches, failed, values, count = [], set(), 0, 0
    coverage = {"callbacks": 0, "actor_rows": 0, "human_rows": 0, "human_on": 0, "human_off": 0,
                "on_changes": 0, "off_changes": 0, "ai_rows": 0, "ai_changes": 0,
                "views": Counter(), "bypass_callbacks": 0, "published_gate": Counter(), "pedal_gate": Counter()}
    for record in records:
        for kind, _, event in combined_events(record):
            if kind == "wheel":
                coverage["callbacks"] += 1
                coverage["views"][str(event["view"])] += 1
                coverage["bypass_callbacks"] += bool(event["pre"]["render_bypass"])
    with Path(str(prefix) + ".human-lamps.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in human_lamps_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected human lamp transaction header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing actual human lamp callback transaction")
            actual = {name: int(value) for name, value in row.items()}
            identity = {"id", "event", "actor", "view"}
            if any(actual[key] != wanted[key] for key in identity):
                raise ValueError("Human lamp transactions changed actual callback chronology")
            for key in wanted.keys() - identity:
                values += 1
                if actual[key] != wanted[key]:
                    failed.add(wanted["id"])
                    mismatches.append({"update": wanted["id"], "event": wanted["event"], "actor": wanted["actor"],
                                       "field": ".human-lamps.csv." + key, "expected": wanted[key], "actual": actual[key]})
            count += 1
            coverage["actor_rows"] += 1
            if wanted["human_pre"]:
                coverage["human_rows"] += 1
                coverage["human_on"] += wanted["lamp_post"] == 1
                coverage["human_off"] += wanted["lamp_post"] == 0
                coverage["on_changes"] += wanted["lamp_pre"] == 0 and wanted["lamp_post"] == 1
                coverage["off_changes"] += wanted["lamp_pre"] == 1 and wanted["lamp_post"] == 0
                coverage["published_gate"][str(wanted["published_pre"] >= 0x1999)] += 1
                coverage["pedal_gate"][str(wanted["pedal_pre"] == 0)] += 1
            else:
                coverage["ai_rows"] += 1
                coverage["ai_changes"] += wanted["lamp_pre"] != wanted["lamp_post"]
        if next(reader, None) is not None: raise ValueError("Extra human lamp callback transactions")
    positive = bool(coverage["on_changes"] and coverage["off_changes"])
    return {"parity": not mismatches and positive, "numerical_parity": not mismatches, "coverage_sufficient": positive,
            "requested_updates": len(records), "compared_updates": len(records), "compared_values": values,
            "mismatch_count": len(mismatches), "first_mismatches": mismatches[:100], "exact_prefix_updates": min(failed, default=len(records)),
            "row_count": count, "coverage": coverage, "initial_lamp_imports": 1, "later_lamp_imports": 0, "later_human_flag_imports": 0,
            "source_writer": "446b50 first actor loop, stores446be8/446bf0 before446c1c render bypass; retained game8 human, view0, signed published body4e8>=0x1999 anddyn304==0",
            "input_scope": "actual446b50-entry published brake and reverse pedal for every actor; rawpedal/outerupdate snapshots are not substituted",
            "expected_only": "actual callback PRE/POST lamp, humanflag and RNG; POST physical gates compared as source-unchanged, never imported",
            "physical_input_values_checked": count * 2, "derived_and_unchanged_values_checked": values - count * 2,
            "outside_compared_fields": ["autonomous callback timing", "rawpedal-to-published-body4e8 producer", "autonomous physics", "human rawkeyboard mapping"]}


def brake_publication_expected(records):
    for record in records:
        for ordinal, (kind, _, event) in enumerate(combined_events(record)):
            stages = ((0, event["pre"]), (1, event["stores"])) if kind == "publication" else ((2, event["post"]),) if kind == "publication_return" else ()
            for stage, snapshot in stages:
                raw, published = (struct.unpack("<16i", bytes.fromhex(snapshot[name + "_hex"])) for name in ("raw_brake", "published_brake"))
                for actor in range(ACTORS - 1, -1, -1):
                    yield {"id": record["id"], "event": ordinal, "call": event["call"], "stage": stage, "actor": actor,
                           "raw_brake": raw[actor], "published_brake": published[actor], "rng": snapshot["rng"]}


def compare_brake_publication(header, records, prefix):
    import csv
    from collections import Counter
    validate_publication_scope(header, records)
    mismatches, failed, count, values = [], set(), 0, 0
    calls = [(record["id"], event) for record in records for kind, _, event in combined_events(record) if kind == "publication"]
    with Path(str(prefix) + ".brake-publication.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in brake_publication_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected brake publication output header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing actual publication checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            identity = {"id", "event", "call", "stage", "actor"}
            if any(actual[key] != wanted[key] for key in identity): raise ValueError("Publication source chronology/actor order changed")
            for key in wanted.keys() - identity:
                values += 1
                if actual[key] != wanted[key]:
                    failed.add(wanted["id"])
                    mismatches.append({"update": wanted["id"], "event": wanted["event"], "actor": wanted["actor"], "stage": wanted["stage"],
                                       "field": ".brake-publication.csv." + key, "expected": wanted[key], "actual": actual[key]})
            count += 1
        if next(reader, None) is not None: raise ValueError("Extra actual publication checkpoint")
    intervals = [sum(entry["kind"] == "publication" for entry in record["combined_journal"]) for record in records[1:]]
    coverage = {"calls": len(calls), "phases": Counter(event["phase"] for _, event in calls),
                "inter_update_call_histogram": Counter(str(value) for value in intervals),
                "zero_publication_intervals": intervals.count(0), "multiple_publication_intervals": sum(value > 1 for value in intervals),
                "inside_update_calls": sum(event["phase"] != "frame" for _, event in calls),
                "changed_stores": sum(a != b for _, event in calls for a, b in zip(struct.unpack("<16i", bytes.fromhex(event["pre"]["published_brake_hex"])),
                                                                                  struct.unpack("<16i", bytes.fromhex(event["stores"]["published_brake_hex"]))))}
    return {"parity": not mismatches and bool(calls), "numerical_parity": not mismatches, "coverage_sufficient": bool(calls),
            "requested_updates": len(records), "compared_updates": len(records), "compared_values": values, "row_count": count,
            "mismatch_count": len(mismatches), "first_mismatches": mismatches[:100], "exact_prefix_updates": min(failed, default=len(records)),
            "coverage": coverage, "published_initializations": 1, "later_published_imports": 0, "later_raw_brake_imports": 0,
            "source": "4608b0 descending all16 actor loop, store4610ce; loop-end4610ed then actual return after separately ordered tail consumers",
            "inputs": "initial first421aa0PRE published vector/raw brake once; retained AI axes and explicit actual422f60 human mapper axes; actual publication schedule only",
            "expected_only": "all16 actual entry/4610ed/return raw brake, published brake and RNG; callback published PRE/POST and later driver snapshots",
            "outside_compared_fields": ["autonomous publication/render timing", "autonomous physics", "human raw keyboard mapping"]}


def compare_combined(header, records, prefix, compare_retained_drivers=False):
    import csv
    from collections import Counter
    validate_combined_scope(header, records, compare_driver_retention=compare_retained_drivers)
    expected = combined_expected(header, records)
    mismatches, failed, values, counts, indexed = [], set(), 0, {}, {}
    for suffix, wanted in expected.items():
        with Path(str(prefix) + suffix).open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != list(wanted[0]): raise ValueError("Unexpected combined output header: " + suffix)
            actual = [{name: int(value) for name, value in row.items()} for row in reader]
        if len(actual) != len(wanted): raise ValueError("Missing or extra combined chronology rows: " + suffix)
        counts[suffix] = len(actual)
        indexed[suffix] = {(row["id"], row["event"], row.get("actor")): row for row in actual}
        identity = {"id", "event", "actor", "kind"}
        for got, want in zip(actual, wanted):
            if any(got[key] != want[key] for key in want.keys() & identity): raise ValueError("Combined native output changed actual event chronology")
            for name in want.keys() - identity:
                values += 1
                if got[name] != want[name]:
                    failed.add(want["id"])
                    mismatches.append({"update": want["id"], "event": want["event"], "field": suffix + "." + name, "expected": want[name], "actual": got[name]})
    pre_values = 0
    for suffix, previous, actor, wanted, metadata in combined_pre_expectations(records):
        actual = indexed[suffix][(*previous, actor)]
        for name, expected_value in wanted.items():
            values += 1
            pre_values += 1
            if actual[name] != expected_value:
                failed.add(metadata["update"])
                mismatches.append({**metadata, "field": suffix + ".PRE." + name, "expected": expected_value, "actual": actual[name]})
    joined = compare_joined_ai(header, records, prefix)
    wheel = compare_wheel_queue(header, records, prefix)
    contact = "contact_weather_manifest" in header
    whine = compare_whine(header, records, prefix, require_random=False) if contact else compare_whine(header, records, prefix)
    joined.update(rng_initializations=1, intermediate_rng_imports=0,
                  input_imports="per-update403dd0 PRE physical/context and driver state refreshed; one retained global RNG; intermediate AI outputs never imported",
                  continuous_process_rng_parity=None)
    joined["outside_compared_fields"] = ["initialization before first403dd0", "human keyboard mapping", "physics", "pit and finish/lap side effects"]
    wheel["audio_inputs"] = "one initial audio state; derived material/pending-mute transitions and retained global RNG; observed gates only"
    wheel["skid_dispatch"]["inputs"] = "retained derived queues/material/mask/reset and global RNG; actual dispatcher gates/ownership/identity only"
    wheel["outside_compared_fields"] = ["autonomous callback timing/visibility", "skidmark geometry", "consecutive autonomous AI/physics"]
    wheel["skid_dispatch"]["outside_compared_fields"] = ["spatial playback/gain/voice lifetime", "consecutive autonomous AI/physics"]
    whine.update(interval_rng_imports=0, intermediate_rng_imports=0,
                 input_scope="raw physical/session caller inputs, observed callback schedule and WHINE mix/voice/gates; one retained global RNG")
    whine["outside_compared_fields"] = ["autonomous callback timing/physics", "WHIRR/motor-layer playback", "whole-process initialization"]
    if contact: whine["whole_process_rng_parity"] = None
    parts = {"joined_ai": joined, "wheel_queue": wheel, "whine": whine}
    if contact: parts["contact_weather"] = compare_contact_weather(records, prefix)
    if compare_retained_drivers:
        parts["driver_retention"] = compare_driver_retention(header, records, prefix)
        joined["input_imports"] = "one initial retained driver/gameplay/braking state and global RNG; per-update physical/context observations only; later driver snapshots are expectations"
        if retained_race_timing_enabled(header):
            parts["race_timing"] = compare_retained_race_timing(header, records, prefix)
            joined["outside_compared_fields"] = ["initialization before first403dd0", "human keyboard mapping", "physics", "pits, local finish/classification and retirement fallback"]
            joined["input_imports"] += "; one initial checkpoint/finish/display world, observed journal caller clocks and derived later lap/AI-finish state"
        if retained_local_session_enabled(header):
            parts["local_session"] = compare_retained_local_session(header, records, prefix)
            joined["outside_compared_fields"] = ["initialization before first403dd0", "human keyboard mapping", "physics", "pits, qualifying and retirement fallback"]
    if compare_retained_drivers and "human_lamps_manifest" in header:
        parts["human_lamps"] = compare_human_lamps(header, records, prefix)
    if compare_retained_drivers and "brake_publication_manifest" in header:
        parts["brake_publication"] = compare_brake_publication(header, records, prefix)
        lamps = parts["human_lamps"]
        lamps.update(input_scope="derived retained body4e8 through every actual4608b0 publication; actual446b50 PRE reverse pedal remains observed physical context",
                     expected_only="actual callback PRE/POST published brake, lamps, human flags and RNG; no published or humanflag imports",
                     physical_input_values_checked=lamps["row_count"], derived_and_unchanged_values_checked=lamps["compared_values"] - lamps["row_count"])
        lamps["outside_compared_fields"] = ["autonomous callback timing", "autonomous physics", "human rawkeyboard mapping"]
    coverage = {"event_counts": Counter(kind for record in records for kind, _, _ in combined_events(record)),
                "interval_event_count_histogram": Counter(str(len(record["combined_journal"])) for record in records[1:]),
                "material_inputs": Counter(str(call["material"]) for record in records for call in record["surface_calls"]),
                "positive_joined_ai_tire_whine": bool(joined["coverage_sufficient"] and wheel["skid_dispatch"]["coverage"]["draws"] and whine["coverage"]["rng_advance_calls"] and whine["positive_command_coverage"])}
    numerical = not mismatches and all(part.get("numerical_parity", part["parity"]) for part in parts.values())
    accepted_coverage = parts["contact_weather"]["positive_random_coverage"] if contact else coverage["positive_joined_ai_tire_whine"]
    if contact: coverage["positive_contact_weather_random_coverage"] = accepted_coverage
    if "human_lamps" in parts:
        accepted_coverage = parts["human_lamps"]["coverage_sufficient"]
        coverage["positive_human_lamp_on_off"] = accepted_coverage
    if "brake_publication" in parts:
        accepted_coverage = accepted_coverage and parts["brake_publication"]["coverage_sufficient"]
        coverage["positive_brake_publication"] = parts["brake_publication"]["coverage_sufficient"]
    if "race_timing" in parts:
        coverage["previous_consumer_coverage_sufficient"] = accepted_coverage
        accepted_coverage = parts["race_timing"]["coverage_sufficient"]
        coverage["positive_retained_race_timing"] = accepted_coverage
    if compare_retained_drivers and retained_warm_restart_enabled(header):
        accepted_coverage = accepted_coverage and parts["local_session"]["coverage_sufficient"]
        coverage["positive_retained_warm_restart"] = parts["local_session"]["coverage_sufficient"]
    first = mismatches[:100]
    for part in parts.values():
        values += part["compared_values"]
        first += part["first_mismatches"]
    projected = [part for part in (parts.get("driver_retention", {}), parts["joined_ai"].get("traffic_controls", {}), parts.get("local_session", {}))
                 if part.get("unclaimed_control_values")]
    return {"parity": numerical and accepted_coverage, "numerical_parity": numerical, "coverage_sufficient": accepted_coverage,
            "requested_updates": len(records), "compared_updates": len(records),
            "exact_prefix_updates": min([min(failed, default=len(records)), *(part["exact_prefix_updates"] for part in parts.values())]),
            "mismatch_count": len(mismatches) + sum(part["mismatch_count"] for part in parts.values()),
            "first_mismatches": first[:100], "compared_values": values, "direct_pre_checkpoint_values": pre_values, "row_counts": counts,
            "coverage": coverage, "rng_initializations": 1, "later_rng_imports": 0, "observed_draw_count_inputs": 0,
            **({"driver_initializations": 1, "later_driver_imports": 0,
                "human_mapper_transactions": parts["driver_retention"]["human_mapper_transactions"],
                "physical_synchronizations": ["observed body position/basis/speed/contact context", "observed physical gear and reverse pedal", "observed human PRE steering/throttle and explicit422f60 mapper transactions; raw brake retained" if "brake_publication" in parts else "observed human PRE axes and explicit422f60 mapper transactions", "observed session/voice/callback context"],
                "driver_state_checkpoint_values": parts["driver_retention"]["compared_values"]} if compare_retained_drivers else {}),
            "initial_audio_queue_engine_state_imports": 1, "later_audio_queue_engine_state_imports": 0,
            "driver_state": "initialized once and retained across every update; later gameplay/status/braking/AI-control snapshots are expectations only; human mapped axes are explicit external transactions" if compare_retained_drivers else "refreshed from each actual403dd0 PRE alongside physical/context inputs; route/control outputs are compared within each update",
            "boundary_scope": ("first421aa0 PRE through final421aa0 return; actual publication entry/store-end/return and tail consumer chronology included" if "brake_publication" in parts else "first403dd0 PRE through final421aa0 return, including every selected update and intervening recorded callback; zero-RNG phases compared explicitly"),
            "expectation_scope": "raw captured snapshots at mutation boundaries; source-unchanged fields carried between checkpoints as expectations only",
            **({"unclaimed_control_values": sum(part["unclaimed_control_values"] for part in projected),
                "unclaimed_control_fields": [scope for part in projected for scope in part["unclaimed_control_fields"]]} if projected else {}),
            "physical_parity": None, "autonomous_race_parity": None, "whole_process_initialization_parity": None,
            **parts}


def compare_driver_physics(header, records, prefix, drivetrain_feedback=False, rpm_feedback=False, limiter_feedback=False, pedal_feedback=False, control_feedback=False, physical_feedback=False, dispatch_feedback=False):
    """Retained drivers feed physical transactions, optionally retaining physical feedback."""
    if dispatch_feedback and not physical_feedback:
        raise ValueError("Dispatch feedback requires physical feedback")
    if physical_feedback and not control_feedback:
        raise ValueError("Physical feedback requires control feedback")
    if control_feedback and not pedal_feedback:
        raise ValueError("Control feedback requires pedal feedback")
    if pedal_feedback and not limiter_feedback:
        raise ValueError("Pedal feedback requires limiter feedback")
    if limiter_feedback and not rpm_feedback:
        raise ValueError("Limiter feedback requires RPM feedback")
    if rpm_feedback and not drivetrain_feedback:
        raise ValueError("RPM feedback requires drivetrain feedback")
    combined = compare_combined(header, records, prefix, compare_retained_drivers=True)
    physical_path = Path(str(prefix) + ".physical.csv")
    physical = compare(records, physical_path, "independent-physics", header.get("scenario"))
    detached = compare_detached(records, Path(str(physical_path) + ".detached.csv"), header=header)
    launch = compare_launch(records, Path(str(physical_path) + ".launch.csv"), "independent-physics")
    selection = compare_selection(records, Path(str(physical_path) + ".selection.csv"), "independent-physics")
    publication = "brake_publication_manifest" in header
    lamps = "human_lamps_manifest" in header
    physical.update(mode="retained-driver-independent-physics",
                    observed_inputs_each_update=["all16 physical PRE pose, velocity, inertia, filters, contacts and pending forces/moments",
                                                "roughness/damage/detached, car pair history and prop/sign state",
                                                "body selection PRE contexts and consumed body dispatch, root registration/detail/visibility",
                                                "incoming AI-PRE physical gear330 and consumed reverse pedal304",
                                                "human queued shift14c, clutch148 and explicit external422f60 mapper axes",
                                                "original delta/reciprocal/extra, limiter and callback clocks"],
                    generated_inputs_each_update=["all16 raw axes138..147, mapped gear330 BYTE, selected reverse game28 and complete retained gameplay",
                                                 "all16 tiresdc..fb and lampsbody500/508",
                                                 "published brakebody4e8" if publication else "published brake remains observed in this older capture profile",
                                                 "local launch scalar" if lamps else "local launch scalar remains observed in this older capture profile"],
                    compared_values_per_phase={phase: len(records) * ACTORS * physical["fields_per_actor_phase"] for phase in physical["phases"]},
                    captured_but_not_compared=["remaining raw physical fields", "prop transforms and detached render ownership"],
                    world_assumptions=["independent observed physical PRE transaction per update; no generated physical feedback",
                                       "shared RNG advances only in the retained chronology; physical replay does not rerun AI/ground/audio random consumers",
                                       "normal gravity; unknown imported detached owner identity"],
                    parity_scope="all named physical actor outputs at integration and ground after generated retained controls")
    # The standalone comparer describes consumed initialization and per-tick
    # local mapper events. This bridge only restores explicit physical context;
    # its controls already come from the retained driver chronology.
    for name in ("initial_import", "release_events_before_pre", "release_events_inside_pre", "local_mapping_updates",
                 "reset_events_before_pre", "reset_events_inside_pre", "decay_zero_crossing_updates",
                 "observed_event_provenance", "derived_mismatch_count"):
        launch.pop(name, None)
    launch.update(imported_consumed_rows=[], derived_consumed_rows=list(range(len(records))),
                  derived_fields=["tire offsets", *(["local launch scalar"] if lamps else [])],
                  observed_fields=["engine RPM", "clutch/start hold", *([] if lamps else ["local launch scalar"])],
                  derived_output_parity=launch["parity"],
                  input_provenance="retained driver tires and launch scalar where supported; original physical RPM and clutch; no second422f60 call",
                  parity_scope="local415090 generated launch/tire values and explicitly observed RPM/clutch",
                  unobserved_input_assumptions=[])
    parts = {"combined": combined, "physical": physical, "detached": detached, "launch": launch, "body_selection": selection}
    if drivetrain_feedback:
        parts["drivetrain_feedback"] = compare_drivetrain_feedback(header, records, prefix)
        physical["mode"] = "retained-drivetrain-feedback"
        physical["observed_inputs_each_update"][3] = "remaining physical context;330/334/30c/304 never reimported"
        physical["generated_inputs_each_update"].append("retained physical330/334/30c/304, with generated403dd0 recovery330/304 stores")
        physical["world_assumptions"][0] = "observed physical PRE except retained drivetrain feedback; no continuous body physics claim"
        if "human_lamps" in combined:
            lamps_report = combined["human_lamps"]
            lamps_report.update(input_scope="derived retained body4e8 when publication is captured; retained physical reverse pedal304 through every actual446b50 callback",
                                expected_only="actual callback PRE/POST reverse pedal, published brake, lamps, human flags and RNG",
                                physical_input_values_checked=0 if publication else lamps_report["row_count"],
                                derived_and_unchanged_values_checked=lamps_report["compared_values"] - (0 if publication else lamps_report["row_count"]))
        if "whine" in combined:
            combined["whine"]["input_scope"] = "retained physical gear330 and driver speed cache; observed RPM/scale/pedal/session/mix/voice inputs and actual callback schedule; one retained global RNG"
        if "joined_ai" in combined:
            combined["joined_ai"]["input_imports"] = "one initial retained driver/gameplay/braking and330/334/30c/304 state plus global RNG; remaining physical/context observations only"
        if "physical_synchronizations" in combined:
            combined["physical_synchronizations"] = [value.replace("observed physical gear and reverse pedal", "retained generated physical gear and reverse pedal")
                                                      for value in combined["physical_synchronizations"]]
    if rpm_feedback:
        parts["rpm_feedback"] = compare_rpm_feedback(header, records, prefix)
        physical["mode"] = "retained-rpm-feedback"
        physical["observed_inputs_each_update"][3] = "remaining physical context;124/128/330/334/30c/304 never reimported"
        physical["generated_inputs_each_update"].append("retained physical RPM124 and previous-RPM128; full updates copy128 before integration, distant/frozen modes retain128")
        physical["world_assumptions"][0] = "observed physical PRE except retained drivetrain and RPM124/128; no continuous body physics claim"
        launch["derived_fields"].append("engine RPM")
        launch["observed_fields"].remove("engine RPM")
        launch["input_provenance"] = "retained driver tires, RPM124 and launch scalar where supported; observed clutch and launch maximum20; no second422f60 call"
        launch["parity_scope"] = "local415090 retained launch/tire/RPM values and explicitly observed clutch"
        parts["drivetrain_feedback"]["observed_inputs"][0] = "remaining physical pose/speed/filters/contact/force state; RPM124/128 retained"
        if "whine" in combined:
            combined["whine"]["input_scope"] = "retained physical RPM124/gear330 and driver speed cache; observed scale/pedal/session/mix/voice inputs and actual callback schedule; one retained global RNG"
        if "joined_ai" in combined:
            combined["joined_ai"]["input_imports"] = "one initial retained driver/gameplay/braking,124/128/330/334/30c/304 state and global RNG; remaining physical/context observations only"
        if "physical_synchronizations" in combined:
            combined["physical_synchronizations"].append("retained124/128 feeds following physical PRE; actual448130 and44a6f0 read retained124")
    if limiter_feedback:
        parts["limiter_feedback"] = compare_limiter_feedback(header, records, prefix)
        physical["mode"] = "retained-limiter-feedback"
        physical["observed_inputs_each_update"][-1] = "original delta/reciprocal/extra and callback clocks"
        physical["generated_inputs_each_update"].append("retained per-actor limiter flag4be150/load4be108 from first PRE and generated physical results")
        physical["world_assumptions"][0] = "observed physical PRE except retained drivetrain, RPM and limiter flag/load; no continuous body physics claim"
        if "joined_ai" in combined:
            combined["joined_ai"]["input_imports"] += "; limiter arrays initialized once from first421aa0 PRE"
        if "physical_synchronizations" in combined:
            combined["physical_synchronizations"].append("retained4be150/4be108 feeds following physical PRE;40d3b0 consumes previous limiter before40d5f0 refresh, distant4448b0 retains both")
    if pedal_feedback:
        parts["pedal_feedback"] = compare_pedal_feedback(header, records, prefix)
        physical["mode"] = "retained-pedal-feedback"
        physical["observed_inputs_each_update"][0] = "all16 remaining physical PRE pose, velocity, inertia, steering150/274, contacts and pending forces/moments"
        physical["generated_inputs_each_update"].append("retained dynamics14/38 cached filter steps,120/134 ramps and11c/130 outputs; generated physical results feed next PRE")
        physical["world_assumptions"][0] = "observed physical PRE except retained drivetrain, RPM, limiter and six pedal words; no continuous body physics claim"
        if "whine" in combined:
            combined["whine"]["input_scope"] = "retained physical RPM124, throttle11c, gear330 and driver speed cache; observed engine scale24/session/mix/voice gates and actual callback schedule; one retained global RNG"
        if "joined_ai" in combined:
            combined["joined_ai"]["input_imports"] += "; six pedal words initialized once from first421aa0 PRE"
        if "physical_synchronizations" in combined:
            combined["physical_synchronizations"].append("retained14/38/120/134/11c/130 feeds following physical PRE and every boundary; actual44a6f0 reads retained11c")
        for name in ("drivetrain_feedback", "rpm_feedback", "limiter_feedback"):
            parts[name]["observed_inputs"][0] = "remaining physical body/wheel pose/motion, steering150/274, forces, contacts, dispatch, roughness/damage/props; six pedal words retained"
        for name in ("rpm_feedback", "limiter_feedback"):
            parts[name]["observed_inputs"][2] = "actual engine scale24/session/mix/voice gates and contact-event arguments/schedule; throttle11c retained"
        if "driver_retention" in combined:
            combined["driver_retention"]["inputs"] = "one initial driver/gameplay/status/braking/light/tire state; retained generated drivetrain/RPM/limiter/pedal feedback; remaining physical body context, human mapped axes and session/callback context observed"
    if control_feedback:
        parts["control_feedback"] = compare_control_feedback(header, records, prefix)
        physical["mode"] = "retained-control-feedback"
        physical["observed_inputs_each_update"][0] = "all16 remaining physical PRE pose, velocity, inertia, contacts and pending forces/moments; source body-dependent steering sensitivity remains observed"
        physical["generated_inputs_each_update"].append("retained dynamics44 cached recenter step,150 accumulator and int16 angle274; generated physical results feed next PRE")
        physical["world_assumptions"][0] = "observed physical PRE except retained drivetrain, RPM, limiter and all control filters; no continuous body physics claim"
        for name in ("drivetrain_feedback", "rpm_feedback", "limiter_feedback", "pedal_feedback"):
            parts[name]["observed_inputs"][0] = "remaining physical body/wheel pose/motion, source body-dependent steering sensitivity, forces, contacts, dispatch, roughness/damage/props; all control filters retained"
        if "joined_ai" in combined:
            combined["joined_ai"]["input_imports"] += "; cached44/accumulator150/int16 angle274 initialized once from first421aa0 PRE"
        if "driver_retention" in combined:
            combined["driver_retention"]["inputs"] = "one initial driver/gameplay/status/braking/light/tire state; retained generated drivetrain/RPM/limiter and all control-filter feedback; remaining physical body context, human mapped axes and session/callback context observed"
        if "physical_synchronizations" in combined:
            combined["physical_synchronizations"].append("retained44/150/274 feeds following physical PRE and every boundary; source body-dependent steering sensitivity remains explicit observed context")
    if physical_feedback:
        parts["physical_feedback"] = compare_physical_feedback(header, records, prefix)
        physical.update(mode="retained-physical-feedback",
                        observed_inputs_each_update=parts["physical_feedback"]["observed_inputs"],
                        generated_inputs_each_update=[parts["physical_feedback"]["retained_state"], "retained driver/control outputs and shared RNG"],
                        world_assumptions=["one firstPRE physical world import; same Motion/contact/damage/prop contexts survive all updates"],
                        parity_scope="named physical integration/ground outputs plus all retained body/wheel bytes at source boundaries and following PRE")
        for name in ("drivetrain_feedback", "rpm_feedback", "limiter_feedback", "pedal_feedback", "control_feedback"):
            parts[name]["observed_inputs"][0] = "body dispatch/root/visibility, source clocks and explicit human inputs; body/wheel/support/contact/force state retained"
        if "driver_retention" in combined:
            combined["driver_retention"]["inputs"] = "one initial driver and coherent physical world; generated controls/body/wheel feedback and shared RNG; external dispatch/root/visibility/clocks/input context"
        if "joined_ai" in combined:
            combined["joined_ai"]["input_imports"] = "one initial driver/coherent physical world and shared RNG; later body/wheel/contact-pair state generated by persistent native Motion and CarContacts"
        if "physical_synchronizations" in combined:
            combined["physical_synchronizations"] = [value.replace("observed body position/basis/speed/contact context", "retained native body position/basis/speed/support and native pair history")
                .replace("source body-dependent steering sensitivity remains explicit observed context", "source body-dependent steering sensitivity uses retained native body motion")
                for value in combined["physical_synchronizations"]]
        if "wheel_queue" in combined:
            combined["wheel_queue"]["physical_inputs"] = "retained generated body airborne/slip at actual callback phase; observed identity/visibility/ownership/configuration"
        selection["role"] = "independent diagnostic of observed dispatch; selected modes do not feed retained Motion, whose body dispatch remains explicit source input"
    if dispatch_feedback:
        parts["dispatch_feedback"] = compare_dispatch_feedback(header, records, prefix)
        physical["mode"] = "retained-dispatch-feedback"
        physical["observed_inputs_each_update"][0] = "allocation/lifecycle, root registration/detail/visibility and controller ownership; dispatch generated before AI"
        physical["generated_inputs_each_update"].append("all16 body4f0 modes selected before403dd0 from retained Motion and previous driver gameplay")
        parts["physical_feedback"]["observed_inputs"][0] = "allocation/lifecycle, root registration/detail/visibility and controller ownership; body dispatch retained and generated"
        for name in ("drivetrain_feedback", "rpm_feedback", "limiter_feedback", "pedal_feedback", "control_feedback"):
            parts[name]["observed_inputs"][0] = "allocation/lifecycle/root/visibility, source clocks and explicit human inputs; dispatch/body/wheel/support/contact/force state retained"
        selection.update(role="generated before403dd0; selected modes feed route/traffic/steering gates and current physical dispatch",
                         source_boundary="421aa0 PRE before403dd0", comparison_boundary="415090-entry",
                         initial_pre_physical_imports=1, retained_native_selection_updates=len(records),
                         observed_pre_inputs=["allocation/lifecycle, one-local-player four-body budget and AI tire threshold"],
                         physical_inputs="one firstPRE body mode vector; retained Motion rollover/support/tire0/ground normal and retained previous driver gameplay human/player/ring/gap/direction",
                         parity_scope="generated pre-AI selection consumed by joined continuous physical/control/driver/shared RNG loop")
        if "driver_retention" in combined:
            combined["driver_retention"]["inputs"] = "one firstPRE driver/coherent physical world; generated dispatch/controls/body/wheel and shared RNG; external allocation/lifecycle/root/visibility/clocks/input context"
        if "joined_ai" in combined:
            combined["joined_ai"]["input_imports"] += "; all16 body modes generated at414e20/414ef0 before403dd0 from retained PRE state"
    return {"parity": all(part["parity"] for part in parts.values()),
            "numerical_parity": all(part.get("numerical_parity", part["parity"]) for part in parts.values()),
            "coverage_sufficient": combined["coverage_sufficient"] and (not drivetrain_feedback or parts["drivetrain_feedback"]["coverage_sufficient"]) and (not rpm_feedback or parts["rpm_feedback"]["coverage_sufficient"]) and (not limiter_feedback or parts["limiter_feedback"]["coverage_sufficient"]) and (not pedal_feedback or parts["pedal_feedback"]["coverage_sufficient"]) and (not control_feedback or parts["control_feedback"]["coverage_sufficient"]) and (not physical_feedback or parts["physical_feedback"]["coverage_sufficient"]) and (not dispatch_feedback or parts["dispatch_feedback"]["coverage_sufficient"]),
            **({"new_feedback_coverage_sufficient": parts["dispatch_feedback" if dispatch_feedback else "physical_feedback" if physical_feedback else "control_feedback"]["coverage_sufficient"],
                "reused_coverage_required": [name for name in ("drivetrain_feedback", "rpm_feedback", "limiter_feedback", "pedal_feedback", *(("control_feedback",) if physical_feedback else ()), *(("physical_feedback",) if dispatch_feedback else ())) if not parts[name]["coverage_sufficient"]]} if control_feedback else {}),
            "compared_updates": len(records), "actors": ACTORS,
            "compared_values": sum(part["compared_values"] for part in parts.values()),
            "mismatch_count": sum(part["mismatch_count"] for part in parts.values()),
            "exact_prefix_updates": min(part["exact_prefix_updates"] for part in parts.values()),
            "physical_parity": all(part["parity"] for name, part in parts.items() if name != "combined"),
            "driver_initializations": 1, "later_driver_imports": 0, "rng_initializations": 1, "later_rng_imports": 0,
            "physical_world_imports": 1 if physical_feedback else len(records), "continuous_physics_parity": all(part["parity"] for name, part in parts.items() if name != "combined") if physical_feedback else None, "autonomous_race_parity": None,
            "boundary_scope": "complete selected source interval; pre-AI generated dispatch from retained Motion/previous driver state, continuous physical/control/driver/shared RNG feedback; allocation/lifecycle/root/visibility/clocks/input/contact audio context observed" if dispatch_feedback else "complete selected source interval; continuous Motion/body/wheel/support/contact/force and world state with retained controls/drivers/shared RNG; explicit body dispatch/root/visibility/clocks/input and contact audio arguments remain observed" if physical_feedback else "complete selected source interval; retained driver/shared RNG and all control filters with limiter/RPM/drivetrain feedback; remaining physical PRE and contact-event strengths observed" if control_feedback else "complete selected source interval; retained driver/shared RNG, six pedal words and limiter/RPM/drivetrain feedback; remaining physical PRE and contact-event strengths observed" if pedal_feedback else "complete selected source interval; retained driver/shared RNG and limiter4be150/4be108 plus124/128/330/334/30c/304 feedback; remaining physical PRE and contact-event strengths observed" if limiter_feedback else "complete selected source interval; retained driver/shared RNG and124/128/330/334/30c/304 feedback across all source boundaries; remaining physical PRE and contact-event strengths observed" if rpm_feedback else "complete selected source interval; retained driver/shared RNG and330/334/30c/304 feedback across all source boundaries; remaining physical PRE observed" if drivetrain_feedback else "complete selected source interval; retained driver and shared RNG chronology, independent415090 PRE→integration→ground physical transactions",
            **parts}


def dispatch_feedback_expected(records):
    """Raw mode at each source phase; PRE still has the previous dispatch."""
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind not in ("boundary", "ai"): continue
            snapshot = record["ground"] if phase == "skid_pre" else event
            for actor, entry in enumerate(snapshot["actors"]):
                yield {"id": record["id"], "event": ordinal, "actor": actor,
                       "mode": struct.unpack_from("<I", bytes.fromhex(entry["body_hex"]), 0x4f0)[0]}


def compare_dispatch_feedback(header, records, prefix):
    import csv
    mismatches, failed, rows = [], set(), 0
    with Path(str(prefix) + ".dispatch-feedback.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["id", "event", "actor", "mode"]:
            raise ValueError("Unexpected dispatch feedback output header")
        for wanted in dispatch_feedback_expected(records):
            row = next(reader, None)
            if row is None: raise ValueError("Missing dispatch feedback checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            if any(actual[name] != wanted[name] for name in ("id", "event", "actor")):
                raise ValueError("Reordered dispatch feedback checkpoint")
            rows += 1
            if actual["mode"] != wanted["mode"]:
                failed.add(wanted["id"])
                mismatches.append({**wanted, "expected": wanted["mode"], "actual": actual["mode"]})
        if next(reader, None) is not None: raise ValueError("Extra dispatch feedback checkpoint")
    counts = dict.fromkeys(("full_updates", "distant_updates", "full_to_distant", "distant_to_full", "following_pre_updates"), 0)
    observed = set()
    for index, record in enumerate(records):
        counts["following_pre_updates"] += index != 0
        for actor in range(ACTORS):
            previous = struct.unpack_from("<I", bytes.fromhex(record["pre"]["actors"][actor]["body_hex"]), 0x4f0)[0]
            selected = struct.unpack_from("<I", bytes.fromhex(record["ai_pre"]["actors"][actor]["body_hex"]), 0x4f0)[0]
            observed.update((previous, selected))
            counts["full_updates"] += selected == 0; counts["distant_updates"] += selected == 1
            counts["full_to_distant"] += previous == 0 and selected == 1
            counts["distant_to_full"] += previous == 1 and selected == 0
    return {"parity": not mismatches, "numerical_parity": not mismatches, "coverage_sufficient": all(counts.values()),
            "compared_values": rows, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "exact_prefix_updates": min(failed, default=len(records)), "rows": rows,
            "positive_source_counts": counts, "observed_modes": sorted(observed),
            "initial_mode_imports": 1, "later_mode_imports": 0,
            "selection_boundary": "414e20/414ef0 before403dd0 and descending driver control order",
            "publication_phases": [*COMBINED_PHASES, "ai_return"],
            "selector_inputs": "retained previous modes, Motion rollover/support/tire0/ground normal and previous driver gameplay human/player/ring/gap/local direction",
            "observed_inputs": "allocation/lifecycle, source one-player four-body budget and AI tire threshold; root/visibility/clocks/input and callback context",
            "uncovered_branches": ["frozen/crashed/crashing/removed positive coverage", "alternate direction positive coverage", "whole-race initialization", "full-feedback AI recovery"],
            "autonomous_race_parity": None}


def physical_feedback_values(entry):
    blocks = {name: bytes.fromhex(entry[name + "_hex"]) for name in PHYSICAL_RANGES}
    return {f"{name}_{byte}": blocks[name][byte]
            for name, ranges in PHYSICAL_RANGES.items()
            for offset, size in ranges for byte in range(offset, offset + size)}


def physical_feedback_expected(records):
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind not in ("boundary", "ai"): continue
            snapshot = record["ground"] if phase == "skid_pre" else event
            for actor, entry in enumerate(snapshot["actors"]):
                yield {"id": record["id"], "event": ordinal, "actor": actor, **physical_feedback_values(entry)}


def compare_physical_feedback(header, records, prefix):
    """Coherent Motion state at source phases, including following raw PRE."""
    import csv
    mismatches, failed, rows, values = [], set(), 0, 0
    with Path(str(prefix) + ".physical-feedback.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in physical_feedback_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected physical feedback output header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing physical feedback checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            if any(actual[name] != wanted[name] for name in ("id", "event", "actor")):
                raise ValueError("Reordered physical feedback checkpoint")
            rows += 1
            for name in list(wanted)[3:]:
                values += 1
                if actual[name] != wanted[name]:
                    failed.add(wanted["id"])
                    mismatches.append({"id": wanted["id"], "event": wanted["event"], "actor": wanted["actor"],
                                       "field": name, "expected": wanted[name], "actual": actual[name]})
        if next(reader, None) is not None: raise ValueError("Extra physical feedback checkpoint")
    counts = dict.fromkeys(("full_updates", "distant_updates", "full_to_distant", "distant_to_full", "contact_updates", "pending_force_updates", "following_pre_updates"), 0)
    for index, record in enumerate(records):
        counts["following_pre_updates"] += index != 0
        contact = pending = False
        for actor in range(ACTORS):
            before = record["consumed"]["actors"][actor]
            body = bytes.fromhex(before["body_hex"])
            mode = struct.unpack_from("<I", body, 0x4f0)[0]
            counts["full_updates"] += mode == 0; counts["distant_updates"] += mode == 1
            after = bytes.fromhex(record["ground"]["actors"][actor]["body_hex"])
            contact |= after[0x529] != 0
            pending |= any(after[0x1f0:0x208])
            if index:
                previous = struct.unpack_from("<I", bytes.fromhex(records[index - 1]["consumed"]["actors"][actor]["body_hex"]), 0x4f0)[0]
                counts["full_to_distant"] += previous == 0 and mode == 1
                counts["distant_to_full"] += previous == 1 and mode == 0
        counts["contact_updates"] += contact; counts["pending_force_updates"] += pending
    sufficient = all(counts.values())
    return {"parity": not mismatches, "numerical_parity": not mismatches, "coverage_sufficient": sufficient,
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "exact_prefix_updates": min(failed, default=len(records)), "rows": rows, "positive_source_counts": counts,
            "initial_physical_world_imports": 1, "later_physical_world_imports": 0,
            "publication_phases": {"contacts": "cached415090 return before421980 car/prop contacts", "ground": "421980 return after ascending415240", "next_pre": "retained ground state"},
            "direct_raw_actor_snapshot_phases": [phase for phase in COMBINED_PHASES if phase != "skid_pre"] + ["ai_return"],
            "unchanged_expectation_phases": {"skid_pre": "ground body/wheel fields;44c540 PRE captures audio only and no physical writer intervenes"},
            "observed_inputs": ["body dispatch, root registration/detail/visibility and controller ownership", "human mapper axes, clutch/queued shift, delta/reciprocal/extra and actual callback clocks", "material and contact-event arguments, audio gates and callback schedule"],
            "retained_state": "same persistent sixteen Motion objects, wheel/body/support/contact/force state, roughness/damage/detached, pair history and prop/sign world",
            "unsupported_discontinuities": "unmodeled reset, teleport, configuration or ownership discontinuity fails the exact phase/nextPRE comparisons; no observed numeric correction",
            "autonomous_race_parity": None}


DRIVETRAIN_FIELDS = ("gear", "pending_gear", "pending_flag", "reverse_pedal")


def drivetrain_values(entry):
    raw = bytes.fromhex(entry["dynamics_hex"])
    return dict(zip(DRIVETRAIN_FIELDS, (raw[0x330], raw[0x334], struct.unpack_from("<I", raw, 0x30c)[0],
                                        struct.unpack_from("<i", raw, 0x304)[0])))


def drivetrain_feedback_expected(records):
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind not in ("boundary", "ai"): continue
            #44c540 PRE records audio/queues, not complete actor snapshots.
            #No330/334/30c/304 writer occurs after ground before this boundary;
            #the previous source checkpoint is an unchanged expectation only.
            snapshot = record["ground"] if phase == "skid_pre" else event
            for actor, entry in enumerate(snapshot["actors"]):
                yield {"id": record["id"], "event": ordinal, "actor": actor, **drivetrain_values(entry)}


def compare_drivetrain_feedback(header, records, prefix):
    """Every actor at every captured boundary; no inferred frame events."""
    import csv
    mismatches, failed, rows, values = [], set(), 0, 0
    per_actor = [0] * ACTORS
    phase_counts = dict.fromkeys((*COMBINED_PHASES, "ai_return"), 0)
    commits = shifts = reverse_changes = recovery_entries = recovery_exits = startup_writes = 0
    for index, record in enumerate(records):
        for kind, phase, _ in combined_events(record):
            if kind == "boundary": phase_counts[phase] += ACTORS * len(DRIVETRAIN_FIELDS)
            elif kind == "ai": phase_counts["ai_return"] += ACTORS * len(DRIVETRAIN_FIELDS)
        for actor in range(ACTORS):
            pre = drivetrain_values(record["consumed"]["actors"][actor])
            post = drivetrain_values(record["contacts"]["actors"][actor])
            commits += int(pre["pending_flag"] != 0 and post["pending_flag"] == 0 and post["gear"] == pre["pending_gear"])
            shifts += int(pre["gear"] != post["gear"])
            reverse_changes += int(pre["reverse_pedal"] != post["reverse_pedal"])
            before = bytes.fromhex(record["ai_pre"]["actors"][actor]["gameplay_hex"])
            after = bytes.fromhex(record["consumed"]["actors"][actor]["gameplay_hex"])
            old_reverse, new_reverse = (struct.unpack_from("<I", raw, 0x28)[0] for raw in (before, after))
            human = struct.unpack_from("<I", before, 8)[0]
            if human == 0:
                recovery_entries += int(old_reverse == 0 and new_reverse != 0 and pre["gear"] == 7)
                recovery_exits += int(old_reverse != 0 and new_reverse == 0 and pre["gear"] == 1 and pre["reverse_pedal"] == 0)
            if index:
                previous = drivetrain_values(records[index - 1]["post"]["actors"][actor])
                current = drivetrain_values(record["pre"]["actors"][actor])
                startup_writes += int(previous["gear"] == 0 and current["gear"] == 1)
    path = Path(str(prefix) + ".drivetrain-feedback.csv")
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in drivetrain_feedback_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected drivetrain feedback output header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing drivetrain feedback checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            if any(actual[name] != wanted[name] for name in ("id", "event", "actor")):
                raise ValueError("Reordered drivetrain feedback checkpoint")
            rows += 1
            for name in DRIVETRAIN_FIELDS:
                values += 1
                if actual[name] != wanted[name]:
                    failed.add(wanted["id"]); per_actor[wanted["actor"]] += 1
                    mismatches.append({**{name: wanted[name] for name in ("id", "event", "actor")}, "field": name,
                                       "expected": wanted[name], "actual": actual[name]})
        if next(reader, None) is not None: raise ValueError("Extra drivetrain feedback checkpoint")
    exact = not mismatches
    return {"parity": exact, "numerical_parity": exact, "coverage_sufficient": bool(rows),
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "exact_prefix_updates": min(failed, default=len(records)), "rows": rows, "actors": ACTORS,
            "fields_per_actor_checkpoint": len(DRIVETRAIN_FIELDS), "compared_values_per_phase": phase_counts,
            "direct_actor_snapshot_phases": [*filter(lambda phase: phase != "skid_pre", COMBINED_PHASES), "ai_return"],
            "unchanged_expectation_phases": {"skid_pre": "44c540 PRE captures queues/audio only; ground330/334/30c/304 carried unchanged because no source drivetrain writer intervenes"},
            "direct_actor_snapshot_values": values - phase_counts["skid_pre"],
            "unchanged_expectation_values": phase_counts["skid_pre"],
            "mismatches_per_actor": per_actor, "initial_drivetrain_imports": 1, "later_drivetrain_imports": 0,
            "positive_source_counts": {"pending_commits": commits, "physical_gear_changes": shifts,
                                       "physical_reverse_pedal_changes": reverse_changes, "ai_recovery_entries": recovery_entries,
                                       "ai_recovery_exits": recovery_exits, "startup_release_gear_writes": startup_writes},
            "source_pending_commit_parity": exact if commits else None,
            "source_reverse_change_parity": exact if reverse_changes else None,
            "source_ai_recovery_parity": exact if recovery_entries or recovery_exits else None,
            "inputs": "first421aa0 PRE330/334/30c/304 only; generated physical output survives subsequent driver PRE and actual callbacks; explicit401260 automatic release and403dd0 recovery stores",
            "observed_inputs": ["remaining physical pose/speed/RPM/filters/contact/force state", "human mapper axes, queued shift14c, clutch148, clocks and actual callback schedule"],
            "callback_scope": "actual446b50 reverse pedal checked by human_lamps when captured; publication uses retained raw pedals; actual WHINE gear derives from retained330",
            "unobserved_coverage": [name for name, count in (("pending commit", commits), ("physical reverse pedal change", reverse_changes),
                                      ("AI recovery entry", recovery_entries), ("AI recovery exit", recovery_exits)) if not count],
            "continuous_physics_parity": None, "autonomous_race_parity": None}


RPM_FIELDS = ("engine_speed", "previous_engine_speed")


def rpm_values(entry):
    return dict(zip(RPM_FIELDS, struct.unpack_from("<2i", bytes.fromhex(entry["dynamics_hex"]), 0x124)))


def rpm_feedback_expected(records):
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind not in ("boundary", "ai"): continue
            #44c540 PRE has no actors. No124/128 writer intervenes after ground.
            snapshot = record["ground"] if phase == "skid_pre" else event
            for actor, entry in enumerate(snapshot["actors"]):
                yield {"id": record["id"], "event": ordinal, "actor": actor, **rpm_values(entry)}


def compare_rpm_feedback(header, records, prefix):
    """Check raw124/128 at seven actor snapshots and unchanged skid PRE."""
    import csv
    mismatches, failed, rows, values = [], set(), 0, 0
    per_actor = [0] * ACTORS
    phase_counts = dict.fromkeys((*COMBINED_PHASES, "ai_return"), 0)
    rpm_changes = previous_changes = full_copies = distant_retains = frozen_retains = 0
    motors, releases, type17 = [], [], 0
    for record in records:
        for kind, phase, event in combined_events(record):
            if kind == "boundary": phase_counts[phase] += ACTORS * len(RPM_FIELDS)
            elif kind == "ai": phase_counts["ai_return"] += ACTORS * len(RPM_FIELDS)
            elif kind == "motor": motors.extend(struct.unpack("<i", bytes.fromhex(value["engine_speed_hex"]))[0] for value in event["pre"]["physical"])
            elif kind == "release": releases.append(record["id"])
            elif kind == "impact" and event["kind"] == 17: type17 += 1
        for actor in range(ACTORS):
            before = rpm_values(record["consumed"]["actors"][actor])
            after = rpm_values(record["contacts"]["actors"][actor])
            rpm_changes += before["engine_speed"] != after["engine_speed"]
            previous_changes += before["previous_engine_speed"] != after["previous_engine_speed"]
            mode = struct.unpack_from("<I", bytes.fromhex(record["consumed"]["actors"][actor]["body_hex"]), 0x4f0)[0]
            if mode == 0: full_copies += after["previous_engine_speed"] == before["engine_speed"]
            elif mode == 1: distant_retains += after["previous_engine_speed"] == before["previous_engine_speed"]
            else: frozen_retains += after["previous_engine_speed"] == before["previous_engine_speed"]
    with Path(str(prefix) + ".rpm-feedback.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in rpm_feedback_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected RPM feedback output header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing RPM feedback checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            if any(actual[name] != wanted[name] for name in ("id", "event", "actor")):
                raise ValueError("Reordered RPM feedback checkpoint")
            rows += 1
            for name in RPM_FIELDS:
                values += 1
                if actual[name] != wanted[name]:
                    failed.add(wanted["id"]); per_actor[wanted["actor"]] += 1
                    mismatches.append({**{name: wanted[name] for name in ("id", "event", "actor")}, "field": name,
                                       "expected": wanted[name], "actual": actual[name]})
        if next(reader, None) is not None: raise ValueError("Extra RPM feedback checkpoint")
    exact = not mismatches
    lamps = "human_lamps_manifest" in header
    return {"parity": exact, "numerical_parity": exact, "coverage_sufficient": bool(rows),
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "exact_prefix_updates": min(failed, default=len(records)), "rows": rows, "actors": ACTORS,
            "fields_per_actor_checkpoint": len(RPM_FIELDS), "compared_values_per_phase": phase_counts,
            "direct_actor_snapshot_phases": [*filter(lambda phase: phase != "skid_pre", COMBINED_PHASES), "ai_return"],
            "unchanged_expectation_phases": {"skid_pre": "44c540 PRE captures queues/audio only; ground124/128 carried unchanged because no source RPM writer intervenes"},
            "direct_actor_snapshot_values": values - phase_counts["skid_pre"], "unchanged_expectation_values": phase_counts["skid_pre"],
            "mismatches_per_actor": per_actor, "initial_rpm_imports": 1, "later_rpm_imports": 0,
            "positive_source_counts": {"physical_rpm_changes": rpm_changes, "physical_previous_rpm_changes": previous_changes,
                                       "full_previous_rpm_copies": full_copies, "distant_previous_rpm_retains": distant_retains,
                                       "frozen_previous_rpm_retains": frozen_retains, "actual_motor_player_calls": len(motors),
                                       "distinct_actual_motor_rpm_values": len(set(motors)), "actual_launch_calls": len(releases) if lamps else 0,
                                       "observed_type17_contact_calls": type17},
            "inputs": "first421aa0 PRE124/128 only; native physical results feed following physical PRE and shift thresholds; actual448130/44a6f0 read retained124",
            "previous_rpm_contract": "40b360 copies128=124 before full integration; distant444830/4448b0 and frozen body modes preserve128;420be0 reads124-128",
            "launch_scope": "actual448130 uses retained124 and observed maximum20" if lamps else "older capture profile lacks retained launch scalar/callback context; local launch scalar remains observed",
            "observed_inputs": ["physical pose/velocity, wheel motion, other filters, forces, contacts, body dispatch, roughness/damage/props",
                                "human mapping, clutch148/queued shift14c, delta/reciprocal/extra, actual callback clocks",
                                "actual engine scale24/pedal11c/session/mix/voice gates; contact-event strengths/positions/listeners/voice gates"],
            "contact_tail_scope": "420be0 tests124-128>9999 for type17 eligibility, then passes constant0x10000 strength; recorded events lack actor identity, so type17 calls remain an explicit observed schedule with no generated contact-tail caller claim",
            "continuous_physics_parity": None, "autonomous_race_parity": None}


LIMITER_FIELDS = ("limiter_active", "limiter_load_q16")


def limiter_values(snapshot, actor):
    # Actual globals at this boundary are the expectations. Actor/local decoded
    # metadata is deliberately not a substitute for either raw global array.
    shared = snapshot["shared"]
    return {name: struct.unpack_from(format, bytes.fromhex(shared[key + "_hex"]), actor * 4)[0]
            for name, key, format in (("limiter_active", "limiter_active", "<I"),
                                      ("limiter_load_q16", "limiter_load", "<i"))}


def limiter_feedback_expected(records):
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind not in ("boundary", "ai"): continue
            #44c540 PRE has only audio/queue snapshots. The audited source has
            #no4be150/4be108 writer between ground and this actual callback.
            snapshot = record["ground"] if phase == "skid_pre" else event
            for actor in range(ACTORS):
                yield {"id": record["id"], "event": ordinal, "actor": actor, **limiter_values(snapshot, actor)}


def compare_limiter_feedback(header, records, prefix):
    """Check raw actor flag/load at seven boundaries plus unchanged skid PRE."""
    import csv
    mismatches, failed, rows, values = [], set(), 0, 0
    per_actor = [0] * ACTORS
    phase_counts = dict.fromkeys((*COMBINED_PHASES, "ai_return"), 0)
    counts = dict.fromkeys(("physical_flag_changes", "physical_load_changes", "full_updates", "full_active_before",
                           "full_active_after", "full_flag_resets", "full_load_changes", "distant_retains",
                           "distant_active_retains", "distant_nonzero_load_retains", "frozen_retains"), 0)
    for record in records:
        for kind, phase, _ in combined_events(record):
            if kind == "boundary": phase_counts[phase] += ACTORS * len(LIMITER_FIELDS)
            elif kind == "ai": phase_counts["ai_return"] += ACTORS * len(LIMITER_FIELDS)
        for actor in range(ACTORS):
            before = limiter_values(record["consumed"], actor)
            after = limiter_values(record["contacts"], actor)
            counts["physical_flag_changes"] += before["limiter_active"] != after["limiter_active"]
            counts["physical_load_changes"] += before["limiter_load_q16"] != after["limiter_load_q16"]
            mode = struct.unpack_from("<I", bytes.fromhex(record["consumed"]["actors"][actor]["body_hex"]), 0x4f0)[0]
            if mode == 0:
                counts["full_updates"] += 1
                counts["full_active_before"] += before["limiter_active"] != 0
                counts["full_active_after"] += after["limiter_active"] != 0
                counts["full_flag_resets"] += before["limiter_active"] != 0 and after["limiter_active"] == 0
                counts["full_load_changes"] += before["limiter_load_q16"] != after["limiter_load_q16"]
            elif mode == 1:
                counts["distant_retains"] += before == after
                counts["distant_active_retains"] += before == after and before["limiter_active"] != 0
                counts["distant_nonzero_load_retains"] += before == after and before["limiter_load_q16"] != 0
            else: counts["frozen_retains"] += before == after
    with Path(str(prefix) + ".limiter-feedback.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in limiter_feedback_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected limiter feedback output header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing limiter feedback checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            if any(actual[name] != wanted[name] for name in ("id", "event", "actor")):
                raise ValueError("Reordered limiter feedback checkpoint")
            rows += 1
            for name in LIMITER_FIELDS:
                values += 1
                if actual[name] != wanted[name]:
                    failed.add(wanted["id"]); per_actor[wanted["actor"]] += 1
                    mismatches.append({**{name: wanted[name] for name in ("id", "event", "actor")}, "field": name,
                                       "expected": wanted[name], "actual": actual[name]})
        if next(reader, None) is not None: raise ValueError("Extra limiter feedback checkpoint")
    exact = not mismatches
    return {"parity": exact, "numerical_parity": exact,
            "coverage_sufficient": bool(rows and counts["physical_flag_changes"] and counts["physical_load_changes"] and counts["full_updates"] and counts["distant_retains"]),
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "exact_prefix_updates": min(failed, default=len(records)), "rows": rows, "actors": ACTORS,
            "fields_per_actor_checkpoint": len(LIMITER_FIELDS), "compared_values_per_phase": phase_counts,
            "direct_raw_global_snapshot_phases": [*filter(lambda phase: phase != "skid_pre", COMBINED_PHASES), "ai_return"],
            "unchanged_expectation_phases": {"skid_pre": "44c540 PRE captures queues/audio only; ground4be150/4be108 carried unchanged because no source limiter writer intervenes"},
            "direct_raw_global_snapshot_values": values - phase_counts["skid_pre"], "unchanged_expectation_values": phase_counts["skid_pre"],
            "mismatches_per_actor": per_actor, "initial_limiter_imports": 1, "later_limiter_imports": 0,
            "local_metadata_imports": 0, "physical_limiter_imports": 0, "journal_limiter_imports": 0,
            "positive_source_counts": counts,
            "inputs": "first421aa0 PRE raw4be150/4be108 arrays only; native physical outputs feed following physical PRE; all later arrays/local metadata are zeroed and rejected on import",
            "limiter_contract": "40d3b0 consumes previous active/load; full40d5f0 clears active then conditionally refreshes active/load above0xcccc excess; inactive full updates retain load; distant4448b0 retains both",
            "observed_inputs": ["physical body/wheel pose and motion, filters, forces, contacts, body dispatch, roughness/damage/props",
                                "human mapping, clutch148/queued shift14c, delta/reciprocal/extra and actual callback clocks",
                                "actual engine scale/pedal/session/mix/voice gates; contact arguments and schedule, including actor-unbound type17"],
            "unobserved_coverage": [name for name, key in (("next full update consuming active limiter", "full_active_before"),
                                      ("distant active retention", "distant_active_retains"), ("frozen limiter retention", "frozen_retains")) if not counts[key]],
            "continuous_physics_parity": None, "autonomous_race_parity": None}


PEDAL_FIELDS = ("throttle_step_q16", "brake_step_q16", "throttle_ramp_q16", "brake_ramp_q16", "throttle_q16", "brake_q16")
PEDAL_OFFSETS = (0x14, 0x38, 0x120, 0x134, 0x11c, 0x130)


def pedal_values(snapshot, actor):
    raw = bytes.fromhex(snapshot["actors"][actor]["dynamics_hex"])
    return {name: struct.unpack_from("<i", raw, offset)[0] for name, offset in zip(PEDAL_FIELDS, PEDAL_OFFSETS)}


def pedal_feedback_expected(records):
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind not in ("boundary", "ai"): continue
            #44c540 records queues/audio only. Source callbacks between ground
            #and this PRE do not write these six physical filter words.
            snapshot = record["ground"] if phase == "skid_pre" else event
            for actor in range(ACTORS):
                yield {"id": record["id"], "event": ordinal, "actor": actor, **pedal_values(snapshot, actor)}


def compare_pedal_feedback(header, records, prefix):
    """Compare retained filters across seven raw actor boundaries and skid PRE."""
    import csv
    mismatches, failed, rows, values = [], set(), 0, 0
    per_actor = [0] * ACTORS
    phase_counts = dict.fromkeys((*COMBINED_PHASES, "ai_return"), 0)
    changes = dict.fromkeys(PEDAL_FIELDS, 0)
    counts = dict.fromkeys(("full_updates", "distant_updates", "frozen_updates", "full_step_refreshes", "full_step_refresh_mismatches",
                           "distant_cached_step_retains", "distant_cached_step_mismatches", "distant_filter_advances",
                           "full_filter_advances", "full_to_distant", "distant_to_full", "frozen_retains",
                           "direct_throttle_ramp_retains", "direct_brake_ramp_retains", "reverse_filter_updates", "actual_motor_calls"), 0)
    filter_changes = {mode: dict.fromkeys(PEDAL_FIELDS[2:], 0) for mode in ("full", "distant")}
    prior_modes = [None] * ACTORS
    for record in records:
        for kind, phase, _ in combined_events(record):
            if kind == "boundary": phase_counts[phase] += ACTORS * len(PEDAL_FIELDS)
            elif kind == "ai": phase_counts["ai_return"] += ACTORS * len(PEDAL_FIELDS)
            elif kind == "motor": counts["actual_motor_calls"] += 1
        for actor in range(ACTORS):
            before, after = (pedal_values(record[phase], actor) for phase in ("consumed", "contacts"))
            body = bytes.fromhex(record["consumed"]["actors"][actor]["body_hex"])
            dynamics = bytes.fromhex(record["consumed"]["actors"][actor]["dynamics_hex"])
            mode = struct.unpack_from("<I", body, 0x4f0)[0]
            if (prior_modes[actor], mode) == (0, 1): counts["full_to_distant"] += 1
            if (prior_modes[actor], mode) == (1, 0): counts["distant_to_full"] += 1
            prior_modes[actor] = mode
            for name in PEDAL_FIELDS: changes[name] += before[name] != after[name]
            if mode in (0, 1):
                label = "full" if mode == 0 else "distant"
                counts[label + "_updates"] += 1
                advanced = any(before[name] != after[name] for name in PEDAL_FIELDS[2:])
                counts[label + "_filter_advances"] += advanced
                for name in PEDAL_FIELDS[2:]: filter_changes[label][name] += before[name] != after[name]
                if mode == 0:
                    wanted = [struct.unpack_from("<i", dynamics, offset)[0] * record["consumed"]["delta_q16"] >> 16 for offset in (0x18, 0x3c)]
                    refresh = [after[name] for name in PEDAL_FIELDS[:2]] == wanted
                    counts["full_step_refreshes"] += refresh
                    counts["full_step_refresh_mismatches"] += not refresh
                else:
                    retained = all(before[name] == after[name] for name in PEDAL_FIELDS[:2])
                    counts["distant_cached_step_retains"] += retained
                    counts["distant_cached_step_mismatches"] += not retained
                for label, offset, name in (("throttle", 0x314, "throttle_ramp_q16"), ("brake", 0x318, "brake_ramp_q16")):
                    counts["direct_" + label + "_ramp_retains"] += struct.unpack_from("<I", dynamics, offset)[0] != 0 and before[name] == after[name]
                counts["reverse_filter_updates"] += struct.unpack_from("<i", dynamics, 0x304)[0] != 0
            else:
                counts["frozen_updates"] += 1
                counts["frozen_retains"] += before == after
    with Path(str(prefix) + ".pedal-feedback.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in pedal_feedback_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected pedal feedback output header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing pedal feedback checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            if any(actual[name] != wanted[name] for name in ("id", "event", "actor")):
                raise ValueError("Reordered pedal feedback checkpoint")
            rows += 1
            for name in PEDAL_FIELDS:
                values += 1
                if actual[name] != wanted[name]:
                    failed.add(wanted["id"]); per_actor[wanted["actor"]] += 1
                    mismatches.append({**{name: wanted[name] for name in ("id", "event", "actor")}, "field": name,
                                       "expected": wanted[name], "actual": actual[name]})
        if next(reader, None) is not None: raise ValueError("Extra pedal feedback checkpoint")
    exact = not mismatches
    sufficient = bool(rows and all(changes.values()) and counts["full_updates"] and counts["distant_updates"]
                      and counts["full_to_distant"] and counts["distant_to_full"] and counts["full_filter_advances"]
                      and counts["distant_filter_advances"] and not counts["full_step_refresh_mismatches"]
                      and not counts["distant_cached_step_mismatches"])
    return {"parity": exact, "numerical_parity": exact, "coverage_sufficient": sufficient,
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "exact_prefix_updates": min(failed, default=len(records)), "rows": rows, "actors": ACTORS,
            "fields_per_actor_checkpoint": len(PEDAL_FIELDS), "compared_values_per_phase": phase_counts,
            "direct_raw_actor_snapshot_phases": [*filter(lambda phase: phase != "skid_pre", COMBINED_PHASES), "ai_return"],
            "unchanged_expectation_phases": {"skid_pre": "44c540 PRE captures queues/audio only; six ground filter words carried unchanged because no source filter writer intervenes"},
            "direct_raw_actor_snapshot_values": values - phase_counts["skid_pre"], "unchanged_expectation_values": phase_counts["skid_pre"],
            "mismatches_per_actor": per_actor, "initial_pedal_imports": 1, "later_pedal_imports": 0,
            "physical_pedal_imports": 0, "journal_pedal_imports": 0, "callback_pedal_imports": 0,
            "positive_source_counts": counts, "physical_changes_per_field": changes, "filter_changes_per_mode": filter_changes,
            "inputs": "first421aa0 PRE six raw dynamics words only, in the initial retained driver vector; every other fleet/physical/callback copy zeroed and rejected on import; generated physical values feed following PRE",
            "filter_contract": "full412c90 refreshes14/38 from18/3c times delta; distant retains cached steps but runs418cd0;419630/4197a0 advance ramps and outputs; direct/analog branches retain ramps; reverse304 is resolved before filters",
            "actual_motor_scope": "44a6f0 reads retained11c throttle,124 RPM and330 gear; dynamics24 engine scale and session/mix/voice/callback clocks remain observed",
            "callback_capture_scope": "actual callbacks retained; publication and launch context derived where enriched manifests exist" if "brake_publication_manifest" in header else "older callback profile: publication/body4e8 and launch context remain observed; no claim to derive uncaptured callback activity",
            "observed_inputs": ["remaining physical pose/velocity/wheel state, steering150/274, forces, contacts, body dispatch, roughness/damage/props",
                                "human422f60 mapper axes, clutch148/queued shift14c, delta/reciprocal/extra and actual callback clocks",
                                "engine scale24/session/mix/voice gates, contact-event arguments including actor-unbound type17"],
            "unobserved_coverage": [name for name, key in (("direct throttle ramp retention", "direct_throttle_ramp_retains"),
                                      ("direct brake ramp retention", "direct_brake_ramp_retains"), ("reverse filter path", "reverse_filter_updates"),
                                      ("frozen pedal retention", "frozen_retains")) if not counts[key]],
            "continuous_physics_parity": None, "autonomous_race_parity": None}


CONTROL_FIELDS = ("return_step_q16", "accumulator_q16", "angle")
CONTROL_SIZES = ((0x44, 4), (0x150, 4), (0x274, 2))


def control_values(snapshot, actor):
    raw = bytes.fromhex(snapshot["actors"][actor]["dynamics_hex"])
    return {name: struct.unpack_from("<h" if size == 2 else "<i", raw, offset)[0]
            for name, (offset, size) in zip(CONTROL_FIELDS, CONTROL_SIZES)}


def control_feedback_expected(records):
    for record in records:
        for ordinal, (kind, phase, event) in enumerate(combined_events(record)):
            if kind not in ("boundary", "ai"): continue
            #44c540 captures queues/audio only; no intervening steering writer.
            snapshot = record["ground"] if phase == "skid_pre" else event
            for actor in range(ACTORS):
                yield {"id": record["id"], "event": ordinal, "actor": actor, **control_values(snapshot, actor)}


def compare_control_feedback(header, records, prefix):
    """Three retained steering fields at seven raw phases and unchanged skid PRE."""
    import csv
    mismatches, failed, rows, values = [], set(), 0, 0
    per_actor = [0] * ACTORS
    phase_counts = dict.fromkeys((*COMBINED_PHASES, "ai_return"), 0)
    changes = dict.fromkeys(CONTROL_FIELDS, 0)
    counts = dict.fromkeys(("full_updates", "distant_updates", "frozen_updates", "full_step_refreshes", "full_step_refresh_mismatches",
                           "distant_cached_step_retains", "distant_cached_step_mismatches", "full_to_distant", "distant_to_full",
                           "keyboard_updates", "keyboard_accumulator_advances", "keyboard_recenter_advances", "human_slew_updates",
                           "human_slew_advances", "direct_ai_updates", "direct_accumulator_retains", "frozen_retains"), 0)
    prior_modes = [None] * ACTORS
    for record in records:
        for kind, phase, _ in combined_events(record):
            if kind == "boundary": phase_counts[phase] += ACTORS * len(CONTROL_FIELDS)
            elif kind == "ai": phase_counts["ai_return"] += ACTORS * len(CONTROL_FIELDS)
        for actor in range(ACTORS):
            before, after = (control_values(record[phase], actor) for phase in ("consumed", "contacts"))
            entry = record["consumed"]["actors"][actor]
            body, dynamics, gameplay = (bytes.fromhex(entry[name + "_hex"]) for name in ("body", "dynamics", "gameplay"))
            mode = struct.unpack_from("<I", body, 0x4f0)[0]
            counts["full_to_distant"] += (prior_modes[actor], mode) == (0, 1)
            counts["distant_to_full"] += (prior_modes[actor], mode) == (1, 0)
            prior_modes[actor] = mode
            for name in CONTROL_FIELDS: changes[name] += before[name] != after[name]
            if mode not in (0, 1):
                counts["frozen_updates"] += 1; counts["frozen_retains"] += before == after
                continue
            counts["full_updates" if mode == 0 else "distant_updates"] += 1
            if mode == 0:
                wanted = struct.unpack_from("<i", dynamics, 0x48)[0] * record["consumed"]["delta_q16"] >> 16
                matches = after["return_step_q16"] == wanted
                counts["full_step_refreshes"] += matches; counts["full_step_refresh_mismatches"] += not matches
            else:
                matches = after["return_step_q16"] == before["return_step_q16"]
                counts["distant_cached_step_retains"] += matches; counts["distant_cached_step_mismatches"] += not matches
            direct = struct.unpack_from("<I", dynamics, 0x310)[0] != 0
            human = struct.unpack_from("<I", gameplay, 8)[0] != 0
            advanced = before["accumulator_q16"] != after["accumulator_q16"]
            if not direct:
                counts["keyboard_updates"] += 1; counts["keyboard_accumulator_advances"] += advanced
                axes = struct.unpack_from("<2i", dynamics, 0x138)
                counts["keyboard_recenter_advances"] += advanced and axes == (0, 0)
            elif human:
                counts["human_slew_updates"] += 1
                counts["human_slew_advances"] += before["angle"] != after["angle"]
            else:
                counts["direct_ai_updates"] += 1; counts["direct_accumulator_retains"] += not advanced
    with Path(str(prefix) + ".control-feedback.csv").open(newline="") as stream:
        reader = csv.DictReader(stream)
        for wanted in control_feedback_expected(records):
            if reader.fieldnames != list(wanted): raise ValueError("Unexpected control feedback output header")
            row = next(reader, None)
            if row is None: raise ValueError("Missing control feedback checkpoint")
            actual = {name: int(value) for name, value in row.items()}
            if any(actual[name] != wanted[name] for name in ("id", "event", "actor")):
                raise ValueError("Reordered control feedback checkpoint")
            rows += 1
            for name in CONTROL_FIELDS:
                values += 1
                if actual[name] != wanted[name]:
                    failed.add(wanted["id"]); per_actor[wanted["actor"]] += 1
                    mismatches.append({**{name: wanted[name] for name in ("id", "event", "actor")}, "field": name,
                                       "expected": wanted[name], "actual": actual[name]})
        if next(reader, None) is not None: raise ValueError("Extra control feedback checkpoint")
    exact = not mismatches
    sufficient = bool(rows and all(changes.values()) and counts["full_updates"] and counts["distant_updates"]
                      and counts["full_to_distant"] and counts["distant_to_full"] and counts["keyboard_accumulator_advances"]
                      and counts["direct_accumulator_retains"] and not counts["full_step_refresh_mismatches"]
                      and not counts["distant_cached_step_mismatches"])
    return {"parity": exact, "numerical_parity": exact, "coverage_sufficient": sufficient,
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:32],
            "exact_prefix_updates": min(failed, default=len(records)), "rows": rows, "actors": ACTORS,
            "fields_per_actor_checkpoint": len(CONTROL_FIELDS), "compared_values_per_phase": phase_counts,
            "direct_raw_actor_snapshot_phases": [*filter(lambda phase: phase != "skid_pre", COMBINED_PHASES), "ai_return"],
            "unchanged_expectation_phases": {"skid_pre": "44c540 PRE captures queues/audio only; ground44/150/274 carried unchanged because no source steering writer intervenes"},
            "direct_raw_actor_snapshot_values": values - phase_counts["skid_pre"], "unchanged_expectation_values": phase_counts["skid_pre"],
            "mismatches_per_actor": per_actor, "initial_control_imports": 1, "later_control_imports": 0,
            "physical_control_imports": 0, "journal_control_imports": 0, "callback_control_imports": 0,
            "positive_source_counts": counts, "physical_changes_per_field": changes,
            "inputs": "first421aa0 PRE raw44/150/int16 274 only in the initial retained driver vector; all other fleet/physical copies zeroed and rejected on import; journal/callback payloads contain no steering fields; generated physical results feed following PRE",
            "filter_contract": "full412c90 refreshes44 from48 times delta; distant retains44;418cd0 selects419230 keyboard recenter/accumulation or4190f0 direct steering; direct branches retain150; human direct branch uses signed-int16 angle and90-unit slew",
            "ownership_reset_scope": "402be0/402c80 change modes/ownership without writing44/150/274;412040 clears150/274 and445360 clears274; later external reset/ownership events require captured supported provenance or fail closed",
            "observed_inputs": ["remaining physical body/wheel pose/motion and body-dependent steering sensitivity, forces, contacts, dispatch, roughness/damage/props",
                                "human422f60 mapper axes, delta/reciprocal/extra, clutch/queued shift and actual callback clocks",
                                "engine scale24/session/mix/voice gates and contact-event arguments/schedule"],
            "unobserved_coverage": [name for name, key in (("keyboard recenter", "keyboard_recenter_advances"),
                                      ("human direct slew", "human_slew_advances"), ("frozen steering retention", "frozen_retains")) if not counts[key]],
            "continuous_physics_parity": None, "autonomous_race_parity": None}


CONTACT_WEATHER_MANIFEST = {"impact": 0x424860, "scrape": 0x4250a0, "spatial": 0x424b50,
                            "sample_pc": 0x4249b9, "discarded_scrape_pc": 0x425121,
                            "scale": 0x4b99b4, "stereo": 0x4d3d20, "effect_flag": 0x4d3e48,
                            "voice_pool": [0x4d3d28, 12], "scrape_handles": [0x4d39e8, 4],
                            "scope": "one selected-interval seed with actual contact/weather calls; raw spatial/context/voice gates observed, numerical outputs expected only"}
CONTACT_GAINS = (0x4d3c28, 0x4d3bf0, 0x4d3bb8, 0x4d3c30, 0x4d3bf8, 0x4d3bc0)


def capture_contact_initial(reader):
    reader.preload([(0x4b99b4, 4), (0x4d3e48, 4), *((address, 4) for address in CONTACT_GAINS)])
    return {"scale_hex": reader.memory(0x4b99b4, 4).hex(),
            "gains_hex": b"".join(reader.memory(address, 4) for address in CONTACT_GAINS).hex(),
            "effect_flag": reader.word(0x4d3e48)}


def capture_contact_context(reader):
    result = capture_combined_audio(reader)
    reader.preload([(0x4b0660, 4), (0x4d3d20, 4), (0x4d3e48, 4), (0x4d59c8, 4), (0x4c3478, 96), (0x4c3790, 24)])
    configuration = reader.word(0x4b0660)
    reader.preload([(configuration + 0xac, 4)])
    players = integer(reader.word(configuration + 0xac), 1, 4, "contact listener count")
    views = struct.unpack("<4h", bytes.fromhex(result["camera_views_hex"]))
    showcase_index = struct.unpack("<h", reader.memory(0x4c3a64, 2))[0]
    heading = struct.unpack("<h", reader.memory(0x4d59c8, 2))[0]
    result.update(stereo=reader.word(0x4d3d20), effect_flag=reader.word(0x4d3e48), listeners=[])
    camera_table = struct.unpack("<24I", reader.memory(0x4c3478, 96))
    showcase_table = struct.unpack("<6I", reader.memory(0x4c3790, 24))
    for player in range(players):
        if result["showcase"]:
            integer(showcase_index, 0, 5, "showcase camera index")
            camera = showcase_table[showcase_index]
        else:
            integer(views[player], 0, 5, "contact camera view")
            camera = camera_table[player * 6 + views[player]]
        integer(camera, 0x10000, 0xfffffff0, "contact camera pointer")
        reader.preload([(camera + 0xcc, 12)])
        result["listeners"].append({"player": player, "view": views[player], "showcase": result["showcase"],
                                    "position_float_hex": reader.memory(camera + 0xcc, 12).hex(), "inverse_heading": heading})
    return result


def validate_contact_context(snapshot):
    validate_combined_audio(snapshot)
    integer(snapshot["stereo"], 0, 0xffffffff, "contact stereo")
    if snapshot["effect_flag"] != 0: raise ValueError("Unsettled source event27 distance flag is outside selected contact scope")
    listeners = snapshot["listeners"]
    if not 1 <= len(listeners) <= 4 or [entry["player"] for entry in listeners] != list(range(len(listeners))):
        raise ValueError("Incomplete actual source contact listener order")
    for listener in listeners:
        integer(listener["view"], 0, 5, "contact raw view")
        integer(listener["showcase"], 0, 0xffffffff, "contact showcase")
        integer(listener["inverse_heading"], -32768, 32767, "contact inverse heading")
        raw_hex(listener["position_float_hex"], 12, "raw contact camera position")


def contact_voice_gates(consumer, event, audit_allocation=False):
    """Observed external voice queries are inputs; allocation outputs are audited."""
    queries = event["queries"]
    if consumer == "scrape":
        playing = [query["playing"] for query in queries if query["return_address"] in (0x425105, 0x4251a0, 0x4251c4)]
        if len(playing) > 1: raise ValueError("Repeated source scrape playing gate")
        return (playing[0] if playing else 0,)
    pool = [query for query in queries if query["return_address"] == 0x424ce2]
    if [query["target"] for query in pool] != [0x4d3d28 + index * 4 for index in range(len(pool))] or len(pool) > 12:
        raise ValueError("Actual impact voice queries changed pool traversal")
    if pool and (any(query["playing"] == 0 for query in pool[:-1]) or len(pool) < 12 and pool[-1]["playing"]):
        raise ValueError("Incomplete actual impact voice-pool prefix")
    busy = sum(query["playing"] << index for index, query in enumerate(pool))
    selected = [query["playing"] for query in queries if query["return_address"] == 0x424a2e]
    succeeded = [query["playing"] for query in queries if query["return_address"] == 0x424a47]
    if len(selected) > 1 or len(succeeded) > 1: raise ValueError("Repeated selected impact voice gate")
    if audit_allocation and "allocation" in event:
        expected = next((0x4d3d28 + index * 4 for index in range(12) if not (busy >> index & 1)), 0)
        if event["allocation"] != expected: raise ValueError("Observed impact allocation differs from actual voice-query prefix")
    return busy, selected[0] if selected else 0, succeeded[0] if succeeded else 0


def validate_contact_weather_scope(header, records):
    if header.get("contact_weather_manifest") != CONTACT_WEATHER_MANIFEST:
        raise ValueError("Unknown source contact/weather provenance")
    initial = header["contact_audio_initial"]
    scale = struct.unpack("<f", raw_hex(initial["scale_hex"], 4, "initialized source spatial scale"))[0]
    if scale != 65536.0: raise ValueError("Unexpected initialized source contact spatial scale")
    raw_hex(initial["gains_hex"], 24, "initialized source contact gains")
    if initial["effect_flag"] != 0: raise ValueError("Unsettled initial source event27 distance flag")
    integer(header["combined_initial_release_counter"], 0, 360, "selected initial release counter")
    if type(header.get("contact_play_returns", False)) is not bool: raise ValueError("Invalid source play-return capture provenance")
    supported_phases = {"pre_to_ai", "consumed_to_contacts", "contacts_to_ground", "ground_to_skid", "skid_to_post"}
    for record in records:
        calls = record["contact_audio_calls"]
        if not isinstance(calls, list) or len(calls) > 2048: raise ValueError("Invalid complete contact audio call list")
        if [entry["event"]["order"] for entry in calls] != sorted(set(entry["event"]["order"] for entry in calls)):
            raise ValueError("Repeated or out-of-order contact audio calls")
        frame = record["combined_journal"]
        if [entry["event"]["order"] for entry in frame] != sorted(set(entry["event"]["order"] for entry in frame)):
            raise ValueError("Combined frame callback/contact order changed")
        all_calls = [(entry, False) for entry in calls] + [(entry, True) for entry in frame if entry["kind"] in ("impact", "scrape")]
        for entry, in_frame in all_calls:
            consumer, event = entry["kind"], entry["event"]
            if consumer not in ("impact", "scrape"): raise ValueError("Unknown contact audio consumer")
            if in_frame and event["phase"] != "frame" or not in_frame and event["phase"] not in supported_phases:
                raise ValueError("Contact audio inside unpartitioned AI phase is outside the selected replay contract")
            integer(event["order"], 1, 0x7fffffff, "actual source contact order")
            integer(event["kind"], 0, 27, "actual contact event kind")
            if consumer == "scrape":
                integer(event["handle"], -1, 3, "actual scrape handle")
                if event["kind"] not in (10, 11, 12): raise ValueError("Unknown actual source scrape event")
            raw_hex(event["position_hex"], 12, "actual contact source position")
            integer(event["strength"], -0x80000000, 0x7fffffff, "actual source contact strength")
            for phase in ("pre", "post"): validate_contact_context(event[phase])
            for query in event["queries"]: integer(query["playing"], 0, 1, "actual source contact voice gate")
            contact_voice_gates(consumer, event, audit_allocation=True)
            for command in event["commands"]:
                if command["kind"] not in ("play", "stop", "pan", "volume", "rate"): raise ValueError("Unknown actual source contact command")
                contact_command_target(consumer, event, command["target"])
                if command["kind"] == "play" and header.get("contact_play_returns"):
                    integer(command["return_eax"], 0, 1, "actual contact play wrapper return")
                    if command["allocation_succeeded"] != command["return_eax"]:
                        raise ValueError("Actual contact allocation evidence differs from wrapper return")
                    integer(command["voice_handle_after"], 0, 0xffffffff, "actual source voice handle after play request")
                    if command["allocation_succeeded"] and command["voice_handle_after"] in (0, 0xffffffff):
                        raise ValueError("Successful source allocation lacks an actual voice handle")


def contact_payload(consumer, event):
    pre = event["pre"]
    common = [event["kind"], event["strength"] & 0xffffffff]
    if consumer == "impact":
        common += [event["local"], pre["enabled"], pre["stereo"], len(pre["listeners"]), *contact_voice_gates(consumer, event)]
    else:
        common += [event["handle"] & 0xffffffff, pre["enabled"], pre["stereo"], len(pre["listeners"]), *contact_voice_gates(consumer, event)]
    payload = struct.pack("<" + str(len(common)) + "I", *common) + bytes.fromhex(event["position_hex"])
    for listener in pre["listeners"]:
        payload += struct.pack("<3I", listener["view"] & 0xffffffff, listener["showcase"], listener["inverse_heading"] & 0xffffffff)
        payload += bytes.fromhex(listener["position_float_hex"])
    return payload


def contact_command_target(consumer, event, target):
    if consumer == "scrape":
        expected = 0x4d39e8 + event["handle"] * 4
        if target != expected: raise ValueError("Actual scrape command changed source handle")
        return event["handle"]
    if event["kind"] == 25:
        if target != 0x4d3d18: raise ValueError("Actual thunder command changed source voice")
        return -1
    index, offset = divmod(target - 0x4d3d28, 4)
    if offset or not 0 <= index < 12: raise ValueError("Actual impact command is outside source voice pool")
    return index


def contact_expected(records):
    events, commands = [], []
    for record in records:
        for ordinal, (consumer, _, event) in enumerate(combined_events(record)):
            if consumer not in ("impact", "scrape"): continue
            spatial = event.get("spatial")
            actual_commands = event["commands"]
            action = 1 if any(command["kind"] == "play" for command in actual_commands) else 3 if any(command["kind"] == "stop" for command in actual_commands) else 2 if any(command["kind"] in ("pan", "volume") for command in actual_commands) else 0
            metadata = {"id": record["id"], "event": ordinal}
            primary, secondary = struct.unpack("<2i", bytes.fromhex(event["post"]["camera_state_hex"]))
            expected = {**metadata, "consumer": int(consumer == "scrape"), "kind": event["kind"],
                        "spatial": int(spatial is not None), "accepted": spatial["accepted"] if spatial else int(consumer == "impact" and event["kind"] == 25 and action == 1),
                        "drew": int(event["pre"]["rng"] != event["post"]["rng"]),
                        "sample": event["sample"] if event["sample"] is not None else -1,
                        "rng": event["post"]["rng"], "primary": primary, "secondary": secondary, "action": action}
            if spatial and spatial["accepted"]:
                expected.update(spatial_volume=spatial["volume"], angle=spatial["angle"])
            if "volume" in event: expected["volume"] = event["volume"]
            if "pan" in event: expected["pan"] = event["pan"]
            for index, command in enumerate(actual_commands):
                row = {**metadata, "ordinal": index, "kind": {"play": 0, "stop": 1, "pan": 2, "volume": 3, "rate": 4}[command["kind"]],
                       "sample": -1, "loops": 0, "volume": 0, "offset": 0, "pan": 0,
                       "target": contact_command_target(consumer, event, command["target"]), "value": 0}
                if command["kind"] == "play":
                    for field in ("sample", "loops", "volume", "offset", "pan"): row[field] = command[field]
                    expected.update(volume=command["volume"], pan=command["pan"])
                elif command["kind"] != "stop": row["value"] = command["value"]
                commands.append(row)
            events.append(expected)
    return {".contact-events.csv": events, ".contact-commands.csv": commands}


def compare_contact_weather(records, prefix):
    import csv
    from collections import Counter
    columns = {".contact-events.csv": ["id", "event", "consumer", "kind", "spatial", "accepted", "drew", "sample", "spatial_volume", "volume", "pan", "angle", "rng", "primary", "secondary", "action"],
               ".contact-commands.csv": ["id", "event", "ordinal", "kind", "sample", "loops", "volume", "offset", "pan", "target", "value"]}
    expected = contact_expected(records)
    mismatches, failed, values, rows = [], set(), 0, {}
    for suffix, wanted in expected.items():
        with Path(str(prefix) + suffix).open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != columns[suffix]: raise ValueError("Unexpected source contact output header: " + suffix)
            actual = [{name: int(value) for name, value in row.items()} for row in reader]
        if len(actual) != len(wanted): raise ValueError("Missing or extra source contact output rows: " + suffix)
        rows[suffix] = len(actual)
        for got, want in zip(actual, wanted):
            if any(got[field] != want[field] for field in ("id", "event") + (("ordinal",) if "ordinal" in want else ("consumer", "kind"))):
                raise ValueError("Native contact outputs changed actual source chronology")
            for field, original in want.items():
                if field in ("id", "event", "ordinal", "consumer", "kind"): continue
                values += 1
                if got[field] != original:
                    failed.add(want["id"])
                    mismatches.append({"update": want["id"], "event": want["event"], "field": suffix + "." + field, "expected": original, "actual": got[field]})
    source_events = [(consumer, event) for record in records for consumer, _, event in combined_events(record) if consumer in ("impact", "scrape")]
    coverage = {"calls": len(source_events), "consumer_counts": Counter(consumer for consumer, _ in source_events),
                "event_kind_counts": Counter(str(event["kind"]) for _, event in source_events),
                "random_calls_by_kind": Counter(str(event["kind"]) for _, event in source_events if event["pre"]["rng"] != event["post"]["rng"]),
                "sample_counts": Counter(str(event["sample"]) for _, event in source_events if event["sample"] is not None),
                "spatial_rejected_calls": sum(event.get("spatial", {}).get("accepted") == 0 for _, event in source_events),
                "command_counts": Counter(command["kind"] for _, event in source_events for command in event["commands"]),
                "playing_queries": Counter(str(query["playing"]) for _, event in source_events for query in event["queries"]),
                "camera_transition_calls": sum(event["pre"]["camera_state_hex"] != event["post"]["camera_state_hex"] for _, event in source_events),
                "playing_after_impact_request": sum(query["return_address"] == 0x424a47 and query["playing"] == 1 for consumer, event in source_events if consumer == "impact" for query in event["queries"]),
                "wrapper_allocation_results": Counter(str(command["allocation_succeeded"]) for _, event in source_events for command in event["commands"] if command["kind"] == "play" and "allocation_succeeded" in command)}
    positive = bool(coverage["random_calls_by_kind"])
    return {"parity": not mismatches and positive, "numerical_parity": not mismatches, "positive_random_coverage": positive,
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:100],
            "exact_prefix_updates": min(failed, default=len(records)), "row_counts": rows, "coverage": coverage,
            "unobserved_fields": "Numerical angle/volume/pan compared only where source spatial-success or command/intention checkpoints expose them; rejected uninitialized locals excluded.",
            "request_scope": "actual internal4798cb entry arguments before volume127 clamp; these are wrapper requests, not independent backend-start proof",
            "voice_scope": "actual pool-query prefix, selected playing/postplay success and scrape playing are external backend gates; wrapper return success is separate observed allocation evidence, not derived voice-lifetime parity"}


def capture_whine_state(reader):
    reader.preload([*((address, 4) for address in WHINE_STATE.values()), (0x4b9e78, 4), (0x4be1a8, 4),
                    (0x4ad810, 4), (0x4d37a0, 4), (0x4b0660, 4)])
    configuration = reader.word(0x4b0660)
    reader.preload([(configuration + 0xac, 4), (configuration + 0x5f8, 4)])
    return {"state": {name: reader.word(address) for name, address in WHINE_STATE.items()}, "rng": reader.word(0x4b9e78),
            "enabled": reader.word(0x4be1a8), "mix": reader.word(0x4ad810),
            "local_actors_hex": reader.memory(0x4d37a0, 4).hex(), "local_players": reader.word(configuration + 0xac),
            "session_state": struct.unpack("<h", reader.memory(configuration + 0x5f8, 2))[0]}


def capture_whine_caller(reader):
    result = capture_whine_state(reader)
    actors = bytes.fromhex(result["local_actors_hex"])
    result["physical"] = []
    for player in range(result["local_players"]):
        actor = actors[player]
        integer(actor, 0, ACTORS - 1, "WHINE local physical actor")
        dynamics = LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"]
        gameplay = LAYOUT["gameplay_base"] + actor * LAYOUT["gameplay_stride"]
        reader.preload([(dynamics + 0x24, 4), (dynamics + 0x11c, 12), (dynamics + 0x330, 4), (gameplay + 0xcc, 4)])
        result["physical"].append({"player": player, "actor": actor, "rpm_scale_hex": reader.memory(dynamics + 0x24, 4).hex(),
                                   "throttle_hex": reader.memory(dynamics + 0x11c, 4).hex(),
                                   "engine_speed_hex": reader.memory(dynamics + 0x124, 4).hex(),
                                   "gear_hex": reader.memory(dynamics + 0x330, 1).hex(),
                                   "speed_hex": reader.memory(gameplay + 0xcc, 4).hex()})
    return result


def capture_whine_tables(reader):
    reader.preload([(0x4ad488, 12), (0x4ad494, 388), (0x4ad618, 196), (0x4ad820, 96)])
    result = {"header_hex": reader.memory(0x4ad488, 12).hex(), "rates_hex": reader.memory(0x4ad494, 388).hex(),
              "gains_hex": reader.memory(0x4ad618, 194).hex(), "offsets_hex": reader.memory(0x4ad820, 96).hex()}
    for name in ("header", "rates", "gains", "offsets"):
        result[name + "_crc32"] = f"{zlib.crc32(bytes.fromhex(result[name + '_hex'])):08x}"
    return result


def whine_tables(raw):
    result = {}
    for name, size in (("header", 12), ("rates", 388), ("gains", 194), ("offsets", 96)):
        result[name] = raw_hex(raw[name + "_hex"], size, "initialized WHINE " + name)
        if raw[name + "_crc32"] != f"{zlib.crc32(result[name]):08x}":
            raise ValueError("Initialized WHINE table CRC32 mismatch")
    intervals, minimum, maximum = struct.unpack("<3i", result["header"])
    if intervals != 193 or minimum < 0 or maximum <= minimum:
        raise ValueError("Unknown source WHINE curve layout")
    return result


def validate_whine_state(snapshot):
    if set(snapshot["state"]) != set(WHINE_STATE):
        raise ValueError("Incomplete source WHINE numeric state")
    for name, value in snapshot["state"].items():
        integer(value, 0, 0xffffffff, "WHINE raw " + name)
    for name in ("rng", "enabled", "mix"):
        integer(snapshot[name], 0, 0xffffffff, "WHINE " + name)
    integer(snapshot["local_players"], 1, 4, "WHINE local player count")
    integer(snapshot["session_state"], -32768, 32767, "WHINE caller session state")
    raw_hex(snapshot["local_actors_hex"], 4, "WHINE player actors")


def validate_whine_scope(header, records):
    if header.get("whine_manifest") != WHINE_MANIFEST or not records:
        raise ValueError("WHINE comparison requires actual caller/callee provenance and selected updates")
    whine_tables(header["whine_tables"])
    validate_whine_state(header["whine_initial_state"])
    retained = header["whine_initial_state"]["state"]
    shared_rng = "contact_weather_manifest" in header
    callback_count = engine_count = whine_count = 0
    for index, record in enumerate(records):
        callbacks = record["whine_callbacks"]
        if not isinstance(callbacks, list) or len(callbacks) > 256 or index == 0 and callbacks:
            raise ValueError("Invalid actual WHINE frame chronology")
        rng = records[index - 1]["post"]["rng"] if index else record["pre"]["rng"]
        terminal_orders = {entry["event"]["order"] for entry in record.get("scene_final_journal", []) if entry["kind"] == "motor"}
        physical_checked = False
        def check_physical():
            for phase in ("pre", "post"):
                snapshot = record["whine_update_" + phase]
                validate_whine_state(snapshot)
                if snapshot["state"] != retained or snapshot["rng"] != record[phase]["rng"]:
                    raise ValueError("WHINE numeric state changed outside actual frame callbacks")
        for callback in callbacks:
            if callback.get("order") in terminal_orders and not physical_checked:
                check_physical()
                physical_checked = True
            pre = callback["pre"]
            validate_whine_state(pre)
            if pre["state"] != retained or pre["rng"] != rng and not shared_rng:
                raise ValueError("Motor callback PRE differs from retained WHINE state/interval RNG")
            rng = pre["rng"]
            if [engine["player"] for engine in callback["engines"]] != list(range(pre["local_players"])):
                raise ValueError("Missing, repeated or out-of-order actual engine call")
            if [physical["player"] for physical in pre["physical"]] != list(range(pre["local_players"])):
                raise ValueError("Incomplete raw motor caller physical inputs")
            actors = bytes.fromhex(pre["local_actors_hex"])
            for physical in pre["physical"]:
                if physical["actor"] != actors[physical["player"]]:
                    raise ValueError("Motor caller physical actor differs from source player map")
                for name, size in (("rpm_scale", 4), ("throttle", 4), ("engine_speed", 4), ("gear", 1), ("speed", 4)):
                    raw_hex(physical[name + "_hex"], size, "motor caller " + name)
            for engine in callback["engines"]:
                for phase in ("pre", "post"):
                    validate_whine_state(engine[phase])
                if engine["pre"]["state"] != retained or engine["pre"]["rng"] != rng:
                    raise ValueError("Engine PRE differs from preceding retained source state")
                raw_hex(engine["input_hex"], 24, "actual engine entry arguments")
                raw_hex(engine["post_input_hex"], 24, "actual engine return arguments")
                reached = bool(engine["pre"]["enabled"]) and engine["pre"]["local_players"] == 1
                if len(engine["whine"]) != int(reached):
                    raise ValueError("Actual WHINE calls disagree with engine gate")
                for whine in engine["whine"]:
                    for phase in ("pre", "post"):
                        validate_whine_state(whine[phase])
                    raw_hex(whine["input_hex"], 24, "actual WHINE arguments")
                    if whine["pre"]["rng"] != rng or whine["post"]["state"] != engine["post"]["state"] or whine["post"]["rng"] != engine["post"]["rng"]:
                        raise ValueError("WHINE source entry/return does not reach actual engine endpoint")
                    playing = {integer(query["playing"], 0, 1, "actual WHINE playing result") for query in whine["queries"]}
                    if len(playing) > 1:
                        raise ValueError("Unstable observed WHINE voice gate")
                    for command in whine["commands"]:
                        if command["kind"] not in ("rate", "volume"):
                            raise ValueError("Unknown WHINE AIL control command")
                        integer(command["value"], -0x80000000, 0x7fffffff, "actual WHINE command value")
                    whine_count += 1
                retained, rng = engine["post"]["state"], engine["post"]["rng"]
                engine_count += 1
            validate_whine_state(callback["post"])
            if callback["post"]["state"] != retained or callback["post"]["rng"] != rng:
                raise ValueError("Engine transactions do not reach motor callback POST")
            callback_count += 1
        if not physical_checked: check_physical()
        if rng != record["pre"]["rng"] and not shared_rng and not terminal_orders:
            raise ValueError("WHINE transactions do not reach following source update PRE RNG")
    return callback_count, engine_count, whine_count


def write_whine_binary(path, header, records):
    callbacks, engines, whines = validate_whine_scope(header, records)
    if not callbacks or not engines or not whines:
        raise ValueError("WHINE comparison requires positive actual caller and callee coverage")
    tables = whine_tables(header["whine_tables"])
    with Path(path).open("wb") as output:
        output.write(struct.pack("<8sII", b"TOCAWHIN", 1, len(records)))
        output.write(struct.pack("<7I", *(header["whine_initial_state"]["state"][name] for name in WHINE_STATE)))
        for name in ("header", "rates", "gains", "offsets"): output.write(tables[name])
        for index, record in enumerate(records):
            callbacks = record["whine_callbacks"]
            seed = records[index - 1]["post"]["rng"] if index else record["pre"]["rng"]
            output.write(struct.pack("<3I", record["id"], seed, len(callbacks)))
            for callback in callbacks:
                pre = callback["pre"]
                output.write(struct.pack("<3I", pre["enabled"], pre["local_players"], pre["session_state"] & 0xffffffff))
                output.write(bytes.fromhex(pre["local_actors_hex"]))
                for physical, engine in zip(pre["physical"], callback["engines"]):
                    values = [struct.unpack("<i", bytes.fromhex(physical[name + "_hex"]))[0] for name in ("engine_speed", "rpm_scale", "throttle")]
                    values += [struct.unpack("<b", bytes.fromhex(physical["gear_hex"]))[0], struct.unpack("<i", bytes.fromhex(physical["speed_hex"]))[0]]
                    output.write(struct.pack("<5i", *values))
                    whine = engine["whine"]
                    mix = whine[0]["pre"]["mix"] if whine else engine["pre"]["mix"]
                    playing = whine[0]["queries"][0]["playing"] if whine and whine[0]["queries"] else 0
                    output.write(struct.pack("<2I", mix, playing))


def compare_whine(header, records, prefix, require_random=True):
    import csv
    from collections import Counter

    callbacks, engines, whines = validate_whine_scope(header, records)
    state_fields = list(WHINE_STATE)
    columns = {".whine-inputs.csv": ["id", "callback", "player", "stage", "rpm", "speed", "gear", "throttle", "flag0", "flag1"],
               ".whine-state.csv": ["id", "callback", "player", "stage", *state_fields, "rng"],
               ".whine-offset.csv": ["id", "callback", "player", "offset"],
               ".whine-commands.csv": ["id", "callback", "player", "ordinal", "kind", "value"],
               ".whine-updates.csv": ["id", *state_fields, "rng"]}
    expected = {name: [] for name in columns}
    coverage = {"callbacks": callbacks, "engine_calls": engines, "whine_calls": whines,
                "callback_count_histogram": Counter(), "playing_query_results": Counter(), "commands": Counter(),
                "positive_volume_commands": 0, "rng_advance_calls": 0, "audio_gear_transitions": Counter(),
                "session_states": Counter(), "offset_counts": Counter(), "disabled_calls": 0, "bonus_calls": 0}
    def numeric(snapshot):
        return {name: value if name == "phase" or value < 0x80000000 else value - 0x100000000
                for name, value in snapshot["state"].items()}
    for index, record in enumerate(records):
        if index: coverage["callback_count_histogram"][str(len(record["whine_callbacks"]))] += 1
        for callback_index, callback in enumerate(record["whine_callbacks"]):
            coverage["session_states"][str(callback["pre"]["session_state"])] += 1
            for engine in callback["engines"]:
                metadata = {"id": record["id"], "callback": callback_index, "player": engine["player"]}
                stages = [(0, engine["pre"], engine["input_hex"])]
                coverage["disabled_calls"] += not engine["pre"]["enabled"]
                if engine["whine"]:
                    whine = engine["whine"][0]
                    stages += [(1, whine["pre"], whine["input_hex"]), (2, whine["post"], whine["input_hex"])]
                    before = numeric(whine["pre"])["previous_gear"]
                    after = numeric(whine["post"])["previous_gear"]
                    coverage["audio_gear_transitions"][f"{before}->{after}"] += 1
                    coverage["rng_advance_calls"] += whine["pre"]["rng"] != whine["post"]["rng"]
                    actor = struct.unpack("<b", bytes.fromhex(whine["pre"]["local_actors_hex"])[:1])[0]
                    coverage["bonus_calls"] += actor >> 1 in (8, 9)
                    for query in whine["queries"]: coverage["playing_query_results"][str(query["playing"])] += 1
                    if "offset" in whine:
                        expected[".whine-offset.csv"].append({**metadata, "offset": whine["offset"]})
                        coverage["offset_counts"][str(whine["offset"])] += 1
                    for ordinal, command in enumerate(whine["commands"]):
                        expected[".whine-commands.csv"].append({**metadata, "ordinal": ordinal, "kind": int(command["kind"] == "volume"), "value": command["value"]})
                        coverage["commands"][command["kind"]] += 1
                        coverage["positive_volume_commands"] += command["kind"] == "volume" and command["value"] > 0
                stages.append((3, engine["post"], engine["post_input_hex"]))
                for stage, snapshot, inputs in stages:
                    expected[".whine-inputs.csv"].append({**metadata, "stage": stage,
                                                         **dict(zip(("rpm", "speed", "gear", "throttle", "flag0", "flag1"), struct.unpack("<6i", bytes.fromhex(inputs))))})
                    expected[".whine-state.csv"].append({**metadata, "stage": stage, **numeric(snapshot), "rng": snapshot["rng"]})
        snapshot = record["whine_update_pre"]
        expected[".whine-updates.csv"].append({"id": record["id"], **numeric(snapshot), "rng": record["pre"]["rng"]})
    if not callbacks or not engines or not whines or require_random and not coverage["rng_advance_calls"]:
        raise ValueError("WHINE comparison requires positive actual callbacks, WHINE calls and random advances")
    mismatches, failed, values, row_counts = [], set(), 0, {}
    for suffix, wanted in expected.items():
        with Path(str(prefix) + suffix).open(newline="") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames != columns[suffix]: raise ValueError("Unexpected WHINE sidecar header: " + suffix)
            actual = [{name: int(value) for name, value in row.items()} for row in reader]
        if len(actual) != len(wanted): raise ValueError("Missing or extra WHINE sidecar rows: " + suffix)
        row_counts[suffix] = len(actual)
        identity = [name for name in ("id", "callback", "player", "stage", "ordinal") if name in columns[suffix]]
        for got, want in zip(actual, wanted):
            if any(got[name] != want[name] for name in identity): raise ValueError("Native WHINE rows changed actual source chronology")
            for name in want.keys() - set(identity):
                values += 1
                if got[name] != want[name]:
                    failed.add(want["id"])
                    mismatches.append({**{field: want[field] for field in identity}, "field": suffix + "." + name,
                                       "expected": want[name], "actual": got[name]})
    return {"parity": not mismatches, "requested_updates": len(records), "compared_updates": len(records),
            "compared_intervals": max(len(records) - 1, 0), "exact_updates": len(records) - len(failed),
            "exact_prefix_updates": next((i for i, record in enumerate(records) if record["id"] in failed), len(records)),
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:100], "row_counts": row_counts,
            "coverage": coverage, "initial_numeric_state_imports": 1, "later_numeric_state_imports": 0,
            "interval_rng_imports": max(len(records) - 1, 0), "intermediate_rng_imports": 0,
            "command_parity": not any(item["field"].startswith(".whine-commands") for item in mismatches) if expected[".whine-commands.csv"] else None,
            "positive_command_coverage": bool(coverage["commands"]["rate"] and coverage["positive_volume_commands"]),
            "input_scope": "raw physical/session caller inputs, observed actual callback schedule and WHINE mix/voice/gates; one prior421aa0-return RNG per interval",
            "output_scope": "derived414240 arguments, retained numeric state, actual414825 shift offset, WHINE AIL Hz/volume commands, following421aa0-entry RNG",
            "whole_process_rng_parity": None, "autonomous_ai_parity": None,
            "outside_compared_fields": ["AI/simulation RNG within updates", "autonomous callback timing/physics", "WHIRR/motor-layer playback", "whole-process initialization"]}


def wheel_actor_reads(actor):
    return [(LAYOUT["body_base"] + actor * LAYOUT["body_stride"] + 0x514, 8),
            (LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"] + 0x1e4, 32),
            (LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"] + 0x2c4, 32),
            (0x4ef270 + actor * 8, 8), (0x4ef2f0 + actor * 16, 16), (0x4b9e78, 4)]


def capture_wheel_actor(reader, actor):
    reader.preload(wheel_actor_reads(actor))
    body = LAYOUT["body_base"] + actor * LAYOUT["body_stride"]
    dynamics = LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"]
    return {"actor": actor, "body_segment_hex": reader.memory(body + 0x514, 8).hex(),
            "slip_segment_hex": reader.memory(dynamics + 0x1e4, 32).hex(),
            "flag_segment_hex": reader.memory(dynamics + 0x2c4, 32).hex(),
            "events_hex": reader.memory(0x4ef270 + actor * 8, 8).hex(),
            "strengths_hex": reader.memory(0x4ef2f0 + actor * 16, 16).hex(), "rng": reader.word(0x4b9e78)}


def capture_wheel_state(reader, boundary):
    reader.preload([(0x4c5d58, 16), (0x4ed968, 16), (0x4d3a78, 64),
                    (0x4dcd9c, 4), (0x4c5d6c, 4), (0x4b0660, 4),
                    *(request for actor in range(ACTORS) for request in wheel_actor_reads(actor)),
                    *((LAYOUT[block + "_base"] + actor * LAYOUT[block + "_stride"] + offset, 4)
                      for actor in range(ACTORS) for block, offset in (("body", 0x4e8), ("body", 0x508), ("dynamics", 0x304), ("gameplay", 8)))])
    configuration = reader.word(0x4b0660)
    reader.preload([(configuration + 0xac, 4)])
    lamps = {name + "_hex": b"".join(reader.memory(LAYOUT[block + "_base"] + actor * LAYOUT[block + "_stride"] + offset, 4)
                                    for actor in range(ACTORS)).hex()
             for name, block, offset in (("published_brake", "body", 0x4e8), ("reverse_pedal", "dynamics", 0x304),
                                         ("lamps", "body", 0x508), ("human_flags", "gameplay", 8))}
    return {"boundary": boundary, "actors": [capture_wheel_actor(reader, actor) for actor in range(ACTORS)], **lamps,
            "ownership_hex": reader.memory(0x4c5d58, 16).hex(), "visibility_hex": reader.memory(0x4ed968, 16).hex(),
            "pending_mute_hex": reader.memory(0x4d3a78, 64).hex(),
            "render_bypass": struct.unpack_from("<h", reader.memory(0x4dcd9c, 4), 2)[0],
            "actor_count": struct.unpack_from("<h", reader.memory(0x4c5d6c, 4), 2)[0],
            "local_players": reader.word(configuration + 0xac), "rng": reader.word(0x4b9e78)}


def validate_wheel_state(state, boundary):
    if state["boundary"] != boundary or state["actor_count"] != ACTORS:
        raise ValueError("Unexpected wheel queue world boundary/count")
    integer(state["local_players"], 1, 4, "wheel queue local players")
    integer(state["render_bypass"], -32768, 32767, "wheel queue render bypass")
    integer(state["rng"], 0, 0xffffffff, "wheel queue RNG")
    for name, size in (("ownership", 16), ("visibility", 16), ("pending_mute", 64)):
        raw_hex(state[name + "_hex"], size, "wheel queue " + name)
    lamp_fields = {name + "_hex" for name in HUMAN_LAMPS_FIELDS}
    observed = lamp_fields.intersection(state)
    if observed and observed != lamp_fields:
        raise ValueError("Incomplete actual callback lamp gates/expectations")
    for name in observed: raw_hex(state[name], 64, "wheel callback " + name)
    if [actor["actor"] for actor in state["actors"]] != list(range(ACTORS)):
        raise ValueError("Wheel queue world lacks ascending sixteen actors")
    for actor in state["actors"]:
        validate_wheel_actor(actor)
    return state


def validate_wheel_actor(actor):
    identity = integer(actor["actor"], 0, 15, "wheel producer actor")
    body = raw_hex(actor["body_segment_hex"], 8, "wheel body segment")
    if body[1] != identity:
        raise ValueError("Wheel producer identity differs from actor slot")
    for name, size in (("slip_segment", 32), ("flag_segment", 32), ("events", 8), ("strengths", 16)):
        raw_hex(actor[name + "_hex"], size, "wheel actor " + name)
    integer(actor["rng"], 0, 0xffffffff, "wheel producer RNG")


def validate_wheel_callback(callback):
    pre = validate_wheel_state(callback["pre"], "446b50-entry")
    post = validate_wheel_state(callback["post"], "446b50-return")
    view = integer(callback["view"], 0, pre["local_players"] - 1, "wheel callback view")
    visibility = bytes.fromhex(pre["visibility_hex"])
    ownership = bytes.fromhex(pre["ownership_hex"])
    eligible = [] if pre["render_bypass"] else ([actor for actor in range(ACTORS) if visibility[actor] or ownership[actor] != 255]
                                               if pre["local_players"] == 1 else [view])
    calls = callback["producers"]
    if [event["actor"] for event in calls] != eligible:
        raise ValueError("Actual wheel producer calls differ from source callback eligibility/order")
    for event in calls:
        if event["return_address"] not in WHEEL_QUEUE_MANIFEST["producer_returns"]:
            raise ValueError("Unexpected original wheel producer caller")
        for phase in ("pre", "post"):
            validate_wheel_actor(event[phase])
            if event[phase]["actor"] != event["actor"]:
                raise ValueError("Wheel producer transaction changed actor")
        for field in ("body_segment_hex", "slip_segment_hex"):
            if event["pre"][field] != event["post"][field]:
                raise ValueError("Wheel producer changed physical queue inputs")
    for name in ("ownership_hex", "visibility_hex", "render_bypass", "actor_count", "local_players"):
        if pre[name] != post[name]:
            raise ValueError("Wheel callback changed eligibility input")
    return pre, post


def wheel_arrays(state):
    actors = state["actors"]
    bodies = [bytes.fromhex(actor["body_segment_hex"]) for actor in actors]
    slips = [bytes.fromhex(actor["slip_segment_hex"]) for actor in actors]
    flags = [bytes.fromhex(actor["flag_segment_hex"]) for actor in actors]
    return {"events": b"".join(bytes.fromhex(actor["events_hex"]) for actor in actors),
            "strengths": b"".join(bytes.fromhex(actor["strengths_hex"]) for actor in actors),
            "current": b"".join(raw[:16] for raw in flags), "previous": b"".join(raw[16:] for raw in flags),
            "identities": bytes(raw[1] for raw in bodies), "airborne": b"".join(raw[3:7] for raw in bodies),
            "longitudinal": b"".join(raw[:16] for raw in slips), "lateral": b"".join(raw[16:] for raw in slips)}


def validate_wheel_queue_scope(header, records):
    if header.get("wheel_queue_manifest") != WHEEL_QUEUE_MANIFEST or header.get("skid_dispatch_manifest") != SKID_MANIFEST:
        raise ValueError("Wheel queue comparison requires actual callback/producer/dispatcher provenance")
    initial = validate_wheel_state(header["wheel_queue_initial_state"], "421aa0-entry")
    retained = wheel_arrays(initial)
    callback_count = producer_count = 0
    for index, record in enumerate(records):
        callbacks = record["wheel_queue_callbacks"]
        if not isinstance(callbacks, list) or len(callbacks) > 256 or index == 0 and callbacks:
            raise ValueError("Invalid intervening wheel callback chronology")
        sequence = [("wheel", callback) for callback in callbacks]
        if retained_warm_restart_enabled(header):
            sequence = [(entry["kind"], entry["event"]) for entry in record["combined_journal"] if entry["kind"] in ("wheel", "warm_grid")]
        terminal_orders = {entry["event"]["order"] for entry in record.get("scene_final_journal", []) if entry["kind"] == "wheel"}
        physical_checked = False
        def check_physical():
            dispatch = skid_state(record["skid_dispatch"]["pre"], "44c540-entry")
            for name in ("events", "strengths"):
                if dispatch[name] != retained[name]:
                    raise ValueError("Retained source wheel queue differs from following dispatcher PRE: " + name)
            for actor in range(ACTORS):
                flags = bytes.fromhex(record["pre"]["actors"][actor]["dynamics_hex"])[0x2c4:0x2e4]
                if flags != retained["current"][actor * 16:actor * 16 + 16] + retained["previous"][actor * 16:actor * 16 + 16]:
                    raise ValueError("Retained wheel flags differ from following update PRE")
        for kind, callback in sequence:
            if callback.get("order") in terminal_orders and not physical_checked:
                check_physical()
                physical_checked = True
            if kind == "warm_grid":
                for name in ("current", "previous"): retained[name] = bytes(ACTORS * 4 * 4)
                continue
            pre, post = validate_wheel_callback(callback)
            actual_pre = wheel_arrays(pre)
            for name in ("events", "strengths", "current", "previous"):
                if actual_pre[name] != retained[name]:
                    raise ValueError("Wheel callback PRE differs from retained source queue/flags: " + name)
            retained = wheel_arrays(post)
            callback_count += 1
            producer_count += len(callback["producers"])
        if not physical_checked: check_physical()
    return callback_count, producer_count


def write_wheel_queue_binary(path, header, records):
    callbacks, producers = validate_wheel_queue_scope(header, records)
    if not records or not callbacks or not producers:
        raise ValueError("Wheel queue comparison requires positive callbacks and actual producers")
    initial = wheel_arrays(header["wheel_queue_initial_state"])
    with Path(path).open("wb") as output:
        output.write(struct.pack("<8sII", b"TOCAWQUE", 1, len(records)))
        for name in ("events", "strengths", "current", "previous"):
            output.write(initial[name])
        for record in records:
            callbacks = record["wheel_queue_callbacks"]
            output.write(struct.pack("<2I", record["id"], len(callbacks)))
            for callback in callbacks:
                pre = callback["pre"]
                arrays = wheel_arrays(pre)
                output.write(struct.pack("<4I", callback["view"], pre["local_players"], bool(pre["render_bypass"]), pre["actor_count"]))
                output.write(bytes.fromhex(pre["ownership_hex"]))
                output.write(bytes.fromhex(pre["visibility_hex"]))
                for name in ("identities", "airborne", "longitudinal", "lateral"):
                    output.write(arrays[name])
                output.write(bytes.fromhex(pre["pending_mute_hex"]))
            pre = record["skid_dispatch"]["pre"]
            audio = skid_state(pre, "44c540-entry")
            output.write(struct.pack("<2I", pre["rng"], struct.unpack("<I", audio["enabled"])[0]))
            for name in ("ownership", "identities", "reset", "surface", "mask", "wheel_kinds"):
                output.write(audio[name])


def compare_wheel_queue(header, records, prefix):
    import csv
    from collections import Counter

    callback_count, producer_count = validate_wheel_queue_scope(header, records)
    if not callback_count or not producer_count:
        raise ValueError("Wheel queue comparison requires positive callbacks and producers")
    expected = {name: [] for name in (".wheel-queue.csv", ".wheel-producers.csv", ".wheel-call-order.csv", ".wheel-mute.csv", ".wheel-dispatch-queue.csv")}
    columns = {".wheel-queue.csv": ["id", "callback", "actor", "wheel", "event", "strength", "current", "previous"],
               ".wheel-producers.csv": ["id", "callback", "ordinal", "actor", "wheel", "event", "strength", "current", "previous"],
               ".wheel-call-order.csv": ["id", "callback", "ordinal", "actor"],
               ".wheel-mute.csv": ["id", "callback", "actor", "pending"],
               ".wheel-dispatch-queue.csv": ["id", "actor", "wheel", "event", "strength", "current", "previous"]}
    coverage = {"callbacks": callback_count, "producers": producer_count, "callback_count_histogram": Counter(),
                "producer_actor_counts": Counter(), "producer_event_counts": Counter(), "eligible_actors": 0,
                "skipped_actors": 0, "airborne_eq1_wheels": 0, "saturated_strengths": 0,
                "current_flag_transitions": Counter(), "callbacks_with_render_bypass": 0}
    def append_actor(target, metadata, actor):
        events = struct.unpack("<4h", bytes.fromhex(actor["events_hex"]))
        strengths = struct.unpack("<4i", bytes.fromhex(actor["strengths_hex"]))
        flags = struct.unpack("<8i", bytes.fromhex(actor["flag_segment_hex"]))
        for wheel in range(4):
            expected[target].append({**metadata, "actor": actor["actor"], "wheel": wheel, "event": events[wheel],
                                     "strength": strengths[wheel], "current": flags[wheel], "previous": flags[wheel + 4]})
    for index, record in enumerate(records):
        callbacks = record["wheel_queue_callbacks"]
        if index:
            coverage["callback_count_histogram"][str(len(callbacks))] += 1
        for callback_index, callback in enumerate(callbacks):
            metadata = {"id": record["id"], "callback": callback_index}
            pre, post = callback["pre"], callback["post"]
            coverage["callbacks_with_render_bypass"] += pre["render_bypass"] != 0
            coverage["eligible_actors"] += len(callback["producers"])
            coverage["skipped_actors"] += ACTORS - len(callback["producers"]) if pre["local_players"] == 1 and not pre["render_bypass"] else 0
            for actor in post["actors"]:
                append_actor(".wheel-queue.csv", metadata, actor)
            pending = struct.unpack("<16i", bytes.fromhex(post["pending_mute_hex"]))
            for actor in range(ACTORS):
                expected[".wheel-mute.csv"].append({**metadata, "actor": actor, "pending": pending[actor]})
            for ordinal, producer in enumerate(callback["producers"]):
                call = {**metadata, "ordinal": ordinal, "actor": producer["actor"]}
                expected[".wheel-call-order.csv"].append(call)
                append_actor(".wheel-producers.csv", {**metadata, "ordinal": ordinal}, producer["post"])
                coverage["producer_actor_counts"][str(producer["actor"])] += 1
                before_flags = struct.unpack("<8i", bytes.fromhex(producer["pre"]["flag_segment_hex"]))
                after_flags = struct.unpack("<8i", bytes.fromhex(producer["post"]["flag_segment_hex"]))
                airborne = bytes.fromhex(producer["pre"]["body_segment_hex"])[3:7]
                events = struct.unpack("<4h", bytes.fromhex(producer["post"]["events_hex"]))
                strengths = struct.unpack("<4i", bytes.fromhex(producer["post"]["strengths_hex"]))
                for wheel in range(4):
                    coverage["producer_event_counts"][str(events[wheel])] += 1
                    coverage["airborne_eq1_wheels"] += airborne[wheel] == 1
                    coverage["saturated_strengths"] += strengths[wheel] == 0x10000
                    coverage["current_flag_transitions"][f"{before_flags[wheel]}->{after_flags[wheel]}"] += 1
        dispatch = record["skid_dispatch"]["pre"]
        for actor in range(ACTORS):
            append_actor(".wheel-dispatch-queue.csv", {"id": record["id"]},
                         {"actor": actor, "events_hex": dispatch["events_hex"][actor * 16:actor * 16 + 16],
                          "strengths_hex": dispatch["strengths_hex"][actor * 32:actor * 32 + 32],
                          "flag_segment_hex": bytes.fromhex(record["pre"]["actors"][actor]["dynamics_hex"])[0x2c4:0x2e4].hex()})
    mismatches, failed, compared_values, row_counts = [], set(), 0, {}
    for suffix, wanted in expected.items():
        with Path(str(prefix) + suffix).open(newline="") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames != columns[suffix]:
                raise ValueError("Unexpected wheel queue native sidecar header: " + suffix)
            got = [{name: int(value) for name, value in row.items()} for row in reader]
        if len(got) != len(wanted):
            raise ValueError("Missing or extra wheel queue sidecar rows: " + suffix)
        row_counts[suffix] = len(got)
        identity_fields = [name for name in ("id", "callback", "ordinal", "actor", "wheel") if name in columns[suffix]]
        for actual, expectation in zip(got, wanted):
            if any(actual[name] != expectation[name] for name in identity_fields):
                raise ValueError("Native wheel queue sidecar changed source callback/actor chronology")
            for name in expectation.keys() - set(identity_fields):
                compared_values += 1
                if actual[name] != expectation[name]:
                    failed.add(expectation["id"])
                    mismatches.append({**{field: expectation[field] for field in identity_fields}, "field": suffix + "." + name,
                                       "expected": expectation[name], "actual": actual[name]})
    dispatcher = compare_skid_dispatch(header, records, prefix)
    dispatcher["inputs"] = "derived retained queues; actual dispatcher PRE mask/reset/surface/enabled/RNG only"
    dispatcher["outside_compared_fields"] = [field for field in dispatcher["outside_compared_fields"] if field != "44b690 queue producer"]
    failed.update(item["update"] for item in dispatcher["first_mismatches"])
    parity = not mismatches and dispatcher["parity"]
    return {"parity": parity, "requested_updates": len(records), "compared_updates": len(records),
            "exact_prefix_updates": min(next((i for i, record in enumerate(records) if record["id"] in failed), len(records)), dispatcher["exact_prefix_updates"]),
            "mismatch_count": len(mismatches) + dispatcher["mismatch_count"], "first_mismatches": mismatches[:100],
            "compared_values": compared_values + dispatcher["compared_values"], "row_counts": row_counts, "coverage": coverage,
            "skid_dispatch": dispatcher, "initial_queue_flag_imports": 1, "later_queue_flag_imports": 0,
            "callback_schedule": "all observed446b50 calls between each prior update return and following entry, including zero/multiple callbacks",
            "physical_inputs": "actual body airborne/slip/identity and visibility/ownership/configuration at callback entry",
            "audio_inputs": "callback PRE pending-mute flags; independent dispatcher PRE mask/reset/surface/enabled/RNG",
            "whole_process_rng_parity": None, "autonomous_physics_parity": None,
            "outside_compared_fields": ["autonomous callback timing/visibility", "skidmark geometry", "pending-mute audio lifecycle outside callbacks", "between-update WHINE", "consecutive autonomous AI/physics"]}


def capture_skid_state(reader, boundary):
    reader.preload([*SKID_BLOCKS.values(), (0x4b9e78, 4), (0x4c5d58, 16),
                    (LAYOUT["body_base"], ACTORS * LAYOUT["body_stride"]),
                    (LAYOUT["dynamics_base"], ACTORS * LAYOUT["dynamics_stride"])])
    bodies = reader.memory(LAYOUT["body_base"], ACTORS * LAYOUT["body_stride"])
    dynamics = reader.memory(LAYOUT["dynamics_base"], ACTORS * LAYOUT["dynamics_stride"])
    return {"boundary": boundary, "rng": reader.word(0x4b9e78),
            **{name + "_hex": reader.memory(address, size).hex() for name, (address, size) in SKID_BLOCKS.items()},
            "ownership_hex": reader.memory(0x4c5d58, 16).hex(),
            "identities_hex": bytes(bodies[index * LAYOUT["body_stride"] + 0x515] for index in range(ACTORS)).hex(),
            "wheel_kinds_hex": b"".join(dynamics[index * LAYOUT["dynamics_stride"] + 0x328:index * LAYOUT["dynamics_stride"] + 0x32c]
                                          for index in range(ACTORS)).hex()}


def skid_state(snapshot, boundary):
    if snapshot["boundary"] != boundary:
        raise ValueError("Unexpected skid dispatcher boundary")
    integer(snapshot["rng"], 0, 0xffffffff, "skid RNG")
    result = {name: raw_hex(snapshot[name + "_hex"], size, "skid " + name)
              for name, (_, size) in SKID_BLOCKS.items()}
    for name, size in (("ownership", 16), ("identities", 16), ("wheel_kinds", 64)):
        result[name] = raw_hex(snapshot[name + "_hex"], size, "skid " + name)
    if any(identity >= ACTORS for identity in result["identities"]):
        raise ValueError("Invalid skid actor identity")
    if result["identities"] != bytes(range(ACTORS)):
        raise ValueError("Skid actor identities disagree with fleet slots")
    if struct.unpack("<I", result["enabled"])[0] not in (0, 1):
        raise ValueError("Skid audio gate must be boolean")
    if any(surface > 3 for surface in result["surface"]):
        raise ValueError("Unsupported source skid surface")
    return result


def validate_skid_dispatch(record):
    transaction = record["skid_dispatch"]
    pre = skid_state(transaction["pre"], "44c540-entry")
    post = skid_state(transaction["post"], "44c540-return")
    for name in ("enabled", "surface", "events", "strengths", "ownership", "identities", "wheel_kinds"):
        if pre[name] != post[name]:
            raise ValueError("Skid dispatcher changed a source input: " + name)
    expected = [(actor, wheel, kind) for actor in range(ACTORS) for wheel in range(4)
                if (kind := struct.unpack_from("<h", pre["events"], (actor * 4 + wheel) * 2)[0]) != -1]
    events = transaction["calls"]
    if [(event["actor"], event["wheel"], event["kind"]) for event in events] != expected:
        raise ValueError("Missing, repeated or out-of-order skid dispatch call")
    rng = transaction["pre"]["rng"]
    masks = list(pre["mask"])
    resets = list(struct.unpack("<16I", pre["reset"]))
    for event in events:
        identity = pre["identities"][event["actor"]]
        if event["identity"] != identity or event["wheel_kind"] != pre["wheel_kinds"][event["actor"] * 4 + event["wheel"]]:
            raise ValueError("Skid callee arguments differ from raw PRE dispatcher input")
        if event["strength"] != struct.unpack_from("<i", pre["strengths"], (identity * 4 + event["wheel"]) * 4)[0]:
            raise ValueError("Skid strength argument differs from source identity lookup")
        if event["pre"] != {"rng": rng, "mask": masks[identity], "reset": resets[identity]}:
            raise ValueError("Skid callee PRE differs from preceding observed return")
        after = event["post"]
        integer(after["rng"], 0, 0xffffffff, "skid call RNG")
        integer(after["mask"], 0, 255, "skid call mask")
        integer(after["reset"], 0, 0xffffffff, "skid call reset")
        sample = event.get("sample")
        if sample is not None:
            if sample["pc"] != SKID_MANIFEST["sample_pcs"][str(pre["surface"][identity])]:
                raise ValueError("Unexpected source sample decision instruction")
            integer(sample["member"], 39, 49, "observed skid sample")
            for field in ("restart", "fade"):
                integer(sample[field], 0, 1, "observed skid " + field)
        rng, masks[identity], resets[identity] = after["rng"], after["mask"], after["reset"]
    if rng != transaction["post"]["rng"] or bytes(masks) != post["mask"] or struct.pack("<16I", *resets) != post["reset"]:
        raise ValueError("Skid callee returns do not reach dispatcher POST")
    return pre


def write_skid_binary(path, header, records):
    if header.get("skid_dispatch_manifest") != SKID_MANIFEST:
        raise ValueError("Skid comparison requires actual dispatcher/callee provenance")
    if not records:
        raise ValueError("Skid comparison requires selected updates")
    calls = draws = samples = 0
    with Path(path).open("wb") as output:
        output.write(struct.pack("<8sII", b"TOCASKID", 1, len(records)))
        for record in records:
            pre = validate_skid_dispatch(record)
            transaction = record["skid_dispatch"]
            calls += len(transaction["calls"])
            draws += sum(event["pre"]["rng"] != event["post"]["rng"] for event in transaction["calls"])
            samples += sum(event.get("sample") is not None for event in transaction["calls"])
            output.write(struct.pack("<4I", record["id"], transaction["pre"]["rng"], struct.unpack("<I", pre["enabled"])[0], ACTORS))
            for name in ("ownership", "identities", "reset", "surface", "mask", "events", "strengths", "wheel_kinds"):
                output.write(pre[name])
    if not calls or not draws or not samples:
        raise ValueError("Skid comparison requires positive original calls, random draws and observed sample decisions")


def compare_skid_dispatch(header, records, prefix):
    import csv
    from collections import Counter

    if header.get("skid_dispatch_manifest") != SKID_MANIFEST or not records:
        raise ValueError("Skid comparison requires source dispatcher provenance and selected updates")
    def rows(suffix, columns):
        with Path(str(prefix) + suffix).open(newline="") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames != columns:
                raise ValueError("Unexpected skid native sidecar header: " + suffix)
            return [{name: int(value) for name, value in row.items()} for row in reader]
    final = rows(".skid-dispatch.csv", ["id", "actor", "mask", "reset", "rng"])
    actual = rows(".skid-events.csv", ["id", "actor", "wheel", "kind", "sample", "dispatch", "restart", "fade", "rng", "mask", "reset"])
    expected_final, expected_events = [], []
    coverage = {"calls": 0, "draws": 0, "observed_sample_decisions": 0, "kind_counts": Counter(),
                "sample_counts": Counter(), "surface_counts": Counter(), "sample_pcs": Counter(),
                "restart_calls": 0, "stop_calls": 0, "update_calls": 0, "ignored_calls": 0,
                "nonhuman_sample_rejections": 0}
    residuals = {"ground_to_dispatcher": [], "dispatcher_to_update_return": []}
    for record in records:
        pre = validate_skid_dispatch(record)
        transaction = record["skid_dispatch"]
        post = skid_state(transaction["post"], "44c540-return")
        resets = struct.unpack("<16I", post["reset"])
        for actor in range(ACTORS):
            expected_final.append({"id": record["id"], "actor": actor, "mask": post["mask"][actor],
                                   "reset": resets[actor], "rng": transaction["post"]["rng"]})
        for event in transaction["calls"]:
            identity, kind = event["identity"], event["kind"]
            sample = event.get("sample")
            enabled = struct.unpack("<I", pre["enabled"])[0] != 0
            restart = int(enabled and kind != 9 and
                          not (kind == 7 and event["pre"]["mask"] and not event["pre"]["reset"]) and
                          (event["pre"]["reset"] != 0 or kind == 7))
            dispatch = int(enabled and kind == 9 and
                           bool(event["pre"]["mask"] & (1 << event["wheel"])) and event["post"]["mask"] == 0)
            if sample is not None:
                restart = sample["restart"]
                if pre["surface"][identity] == 0 or pre["ownership"][identity] != 255:
                    dispatch = 2
                coverage["observed_sample_decisions"] += 1
                coverage["sample_counts"][str(sample["member"])] += 1
                coverage["sample_pcs"][f"{sample['pc']:x}"] += 1
            expected_events.append({"id": record["id"], "actor": event["actor"], "wheel": event["wheel"],
                                    "kind": kind, "sample": -1 if sample is None else sample["member"],
                                    "dispatch": dispatch, "restart": restart,
                                    "fade": restart if sample is None else sample["fade"], **event["post"]})
            coverage["calls"] += 1
            coverage["restart_calls"] += bool(restart)
            coverage["stop_calls"] += dispatch == 1
            coverage["update_calls"] += dispatch == 2
            coverage["ignored_calls"] += dispatch == 0
            coverage["nonhuman_sample_rejections"] += sample is not None and dispatch == 0
            coverage["draws"] += event["pre"]["rng"] != event["post"]["rng"]
            coverage["kind_counts"][str(kind)] += 1
            coverage["surface_counts"][str(pre["surface"][identity])] += 1
        for name, before, after in (("ground_to_dispatcher", record["ground"]["rng"], transaction["pre"]["rng"]),
                                    ("dispatcher_to_update_return", transaction["post"]["rng"], record["post"]["rng"])):
            residuals[name].append({"update": record["id"], "before": before, "after": after, "equal": before == after})
    positive = all(coverage[name] for name in ("calls", "draws", "observed_sample_decisions"))
    local_session = retained_local_session_enabled(header)
    if not positive and not local_session:
        raise ValueError("Skid comparison requires positive source calls, draws and actual sample decisions")
    mismatches, failed, values = [], set(), 0
    for label, observed, expected in (("dispatcher", final, expected_final), ("calls", actual, expected_events)):
        if len(observed) != len(expected):
            raise ValueError("Missing or extra native skid " + label + " rows")
        identity_fields = ("id", "actor", "wheel", "kind") if label == "calls" else ("id", "actor")
        for got, want in zip(observed, expected):
            if any(got[name] != want[name] for name in identity_fields):
                raise ValueError("Native skid rows changed source chronology")
            for field in want.keys() - set(identity_fields):
                values += 1
                if got[field] != want[field]:
                    failed.add(want["id"])
                    mismatches.append({"update": want["id"], "actor": want["actor"], "wheel": want.get("wheel"),
                                       "field": label + "." + field, "expected": want[field], "actual": got[field]})
    gaps = {name: {"compared_updates": len(evidence), "equal_updates": sum(row["equal"] for row in evidence),
                   "nonzero_residual_count": sum(not row["equal"] for row in evidence),
                   "nonzero_residual_updates": [row for row in evidence if not row["equal"]]}
            for name, evidence in residuals.items()}
    return {"parity": not mismatches, "requested_updates": len(records), "compared_updates": len(records),
            "exact_updates": len(records) - len(failed), "exact_prefix_updates": next((i for i, record in enumerate(records) if record["id"] in failed), len(records)),
            "compared_values": values, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:100],
            "coverage": coverage, "rng_residual_boundaries": gaps,
            **({"positive_source_coverage": positive, "coverage_role": "diagnostic; retained local finish/classification/state transitions supply the targeted positive coverage"} if local_session else {}),
            "observed_ground_to_post_rng_gap_explained": all(not gap["nonzero_residual_updates"] for gap in gaps.values()),
            "input_boundary": "44c540-entry", "comparison_boundary": "44c540-return",
            "inputs": "one actual PRE queue/audio-state/RNG import per update; no observed draw counts or POST inputs",
            "sample_evidence": "actual EBX at source sample selection instructions before spatial/ownership rejection",
            "physical_parity": None, "autonomous_race_parity": None,
            "outside_compared_fields": ["44b690 queue producer", "spatial playback/gain/voice lifetime", "between-update WHINE RNG", "consecutive autonomous AI/physics"]}


def joined_content(initial, count):
    integer(count, 3, 256, "joined route count")
    data = raw_hex(initial["cumulative_route_hex"], count * 4, "initialized cumulative route distances")
    if initial["cumulative_route_crc32"] != f"{zlib.crc32(data):08x}":
        raise ValueError("Initialized cumulative route CRC32 mismatch")
    integer(initial["rank_threshold"], 1, 0x7fffffff, "source rank threshold")
    return data


def validate_start_call(event, entry):
    caller = integer(event["return_address"], 0x400000, 0x4affff, "source start-call return address")
    if entry == 0x448130 and caller not in JOINED_AI_MANIFEST["release_returns"]:
        raise ValueError("Unsupported original release caller")
    for phase in ("pre", "post"):
        state = event[phase]
        if state["boundary"] != f"{entry:x}-{'entry' if phase == 'pre' else 'return'}":
            raise ValueError("Unexpected start-state callee boundary")
        for name in ("release_counter", "rng", "race_clock", "release_epoch", "source_mode", "local_players", "session_stage"):
            integer(state[name], 0, 0xffffffff, "start-call " + name)
    if entry == 0x448130:
        before, after = event["pre"], event["post"]
        if any(before[name] != after[name] for name in ("rng", "source_mode", "session_stage", "local_players", "race_clock")):
            raise ValueError("Unexpected retained source release-call state change")
        expected = 360 if before["local_players"] and before["session_stage"] in (2, 4) and before["source_mode"] != 2 else 0
        if after["release_counter"] != expected or after["release_epoch"] != before["race_clock"]:
            raise ValueError("Original release-call output disagrees with source stage/mode")
    elif event["post"]["release_counter"] != 0:
        raise ValueError("Observed source grid initialization did not clear release counter")


def validate_joined_release(record, previous_counter, selected_initial=False, local_session=False):
    events = record["release_events"]
    if not isinstance(events, list) or len(events) > 16:
        raise ValueError("Invalid source release event count")
    counter = previous_counter
    if local_session and any(entry["kind"] == "warm_grid" for entry in record.get("combined_journal", [])):
        for entry in record["combined_journal"]:
            if entry["kind"] == "warm_grid": validate_warm_grid_event(entry["event"])
        counter = 0
    for event in events:
        validate_start_call(event, 0x448130)
        if event["pre"]["release_counter"] != counter:
            raise ValueError("Release entry counter differs from retained derived state")
        state = event["pre"]
        counter = 360 if state["local_players"] and state["session_stage"] in (2, 4) and state["source_mode"] != 2 else 0
    caller = integer(record["update_caller_return_address"], 0x400000, 0x4affff, "source update caller")
    callers = {**LOCAL_SESSION_LIFECYCLE_MANIFEST["update_callers"], **WARM_RESTART_MANIFEST["update_callers"]} if local_session else JOINED_AI_MANIFEST["update_callers"]
    expected_state = callers.get(f"{caller:x}")
    if expected_state != record["pre"]["session_state"]:
        raise ValueError("Unsupported joined source update caller/session state")
    if caller == 0x448434 and counter > 0 and not selected_initial:
        counter -= 1
    for name in (*BOUNDARIES, *AI_BOUNDARIES, *TRAFFIC_BOUNDARIES):
        if record[name]["shared"]["ai_state"]["release_counter"] != counter:
            raise ValueError("Observed release counter differs from source-derived sequence")
    return counter


def traffic_content(raw):
    count = integer(raw["point_count"], 3, 256, "traffic brake-bypass point count")
    pointer = integer(raw["bypass_pointer"], 0x10000, 0xffffffff, "traffic brake-bypass pointer")
    if pointer + count > 0x100000000:
        raise ValueError("Traffic brake-bypass range overflows source address space")
    data = raw_hex(raw["brake_bypass_hex"], count, "live traffic brake-bypass table")
    if raw["brake_bypass_crc32"] != f"{zlib.crc32(data):08x}":
        raise ValueError("Incorrect traffic brake-bypass CRC32")
    return data


def capture_traffic_control(debugger, configuration, point_count):
    blocks = {name + "_hex": debugger.memory(address, size).hex() for name, (address, size) in TRAFFIC_CONTROL_BLOCKS.items()}
    pointer = struct.unpack("<I", bytes.fromhex(blocks["brake_bypass_pointer_hex"]))[0]
    integer(point_count, 3, 256, "traffic brake-bypass point count")
    if pointer < 0x10000 or pointer + point_count > 0x100000000:
        raise ValueError("Invalid live traffic brake-bypass pointer/range")
    aligned = pointer & ~3
    size = (pointer - aligned + point_count + 3) & ~3
    debugger.preload([(aligned, size)])
    data = debugger.memory(pointer, point_count)
    initial = {"bypass_pointer": pointer, "point_count": point_count, "brake_bypass_hex": data.hex(),
               "brake_bypass_crc32": f"{zlib.crc32(data):08x}"}
    traffic_content(initial)
    return {**blocks, **{key: value for key, value in initial.items() if key != "brake_bypass_hex"},
            "force_acceleration_hex": debugger.memory(configuration + 0x58, 4).hex()}, initial


SESSION_BOUNDARY_BLOCKS = {"classification_order": (0x4c9128, 16), "split_enable": (0x4c2fd0, 16),
                           "lap_distance": (0x4bd5b4, 4), "release_marker": (0x4edaac, 4), "release_flags": (0x4af04c, 16)}
SESSION_BOUNDARY_MANIFEST = {"blocks": {name: list(value) for name, value in SESSION_BOUNDARY_BLOCKS.items()},
                             "configuration_tail_offset": 0x5fc, "configuration_tail_bytes": 0x30,
                             "scope": "observed local421390 caller and classification inputs/outputs"}
LOCAL_SESSION_LIFECYCLE_MANIFEST = {"primary_entry": 0x44a370, "cached_clock": [0x4eda6c, 4], "entry_flag": [0x4af050, 4],
                                    "update_callers": {"448434": 6, "448c46": 10, "44a336": 13},
                                    "scope": "neutral primary dispatch chronology; generated pending state commits and timer origins"}
WARM_RESTART_MANIFEST = {"schema": "toca.retained-warm-restart.v1", "request_pc": 0x441b33,
                         "restart_entry": 0x449f50, "creation_clock_pc": 0x447a77, "grid_outer_entry": 0x447980,
                         "outer_return_address": 0x4487d2, "initializer_entry": 0x460170, "initializer_return_address": 0x4479e5,
                         "boundary_manifest": COLD_GRID_INIT_MANIFEST,
                         "update_callers": {"4488a1": 9, "4488d0": 9},
                         "scope": "one actual results RACE AGAIN confirmation, external restart creation clock and one retained warm grid writer"}


def retained_warm_restart_enabled(header):
    if "warm_restart_manifest" not in header:
        return False
    if header["warm_restart_manifest"] != WARM_RESTART_MANIFEST or not retained_local_session_enabled(header):
        raise ValueError("Warm restart requires complete retained local session provenance")
    return True


def validate_warm_grid_event(event):
    if set(event) != {"order", "outer_return_address", "initialization", "boundaries"}:
        raise ValueError("Unexpected warm grid event fields")
    integer(event["order"], 1, 0x7fffffff, "warm grid order")
    if event.get("outer_return_address") != WARM_RESTART_MANIFEST["outer_return_address"]:
        raise ValueError("Unexpected warm grid outer caller")
    legacy = event["initialization"]
    if legacy["return_address"] != WARM_RESTART_MANIFEST["initializer_return_address"]:
        raise ValueError("Unexpected warm grid initializer caller")
    validate_cold_grid_initialization(event["boundaries"], legacy)
    before, after = (cold_grid_boundary_bytes(event["boundaries"][phase]) for phase in ("pre", "post"))
    if after["race_lifecycle"][4:8] != before["race_lifecycle"][4:8] or legacy["post"]["release_counter"] != 0:
        raise ValueError("Warm grid writer must preserve the update counter and reset the release counter")
    expected = legacy["pre"]["rng"]
    for _ in range(ACTORS - 1): expected = (expected * 0x343fd + 0x269ec3) & 0xffffffff
    if legacy["post"]["rng"] != expected:
        raise ValueError("Warm grid must consume one fifteen-draw classification from its retained RNG")
    return before, after


def retained_local_session_enabled(header):
    if "local_session_lifecycle_manifest" not in header:
        return False
    if (header["local_session_lifecycle_manifest"] != LOCAL_SESSION_LIFECYCLE_MANIFEST
            or header.get("local_handoff_manifest") != LOCAL_HANDOFF_MANIFEST
            or header.get("session_boundary_manifest") != SESSION_BOUNDARY_MANIFEST):
        raise ValueError("Incomplete retained local session provenance")
    return True


def cold_local_session_enabled(header):
    """One retained lifecycle begins at the measured, unreleased cold grid."""
    return (retained_local_session_enabled(header) and "cold_grid_sequence" in header
            and not header.get("unrecorded_keyboard_warmup"))


def local_state13_wrapper_axes_unclaimed(header, snapshot, actor):
    """The unrecorded device writer is overwritten before the physical mapper."""
    if not retained_local_session_enabled(header) or snapshot["session_state"] != 13 or actor != header["local_actor"]:
        return False
    entry = snapshot["actors"][actor]
    game, body = bytes.fromhex(entry["gameplay_hex"]), bytes.fromhex(entry["body_hex"])
    return (struct.unpack_from("<b", game, 0x80)[0] == 0 and struct.unpack_from("<I", game, 8)[0] == 0
            and struct.unpack_from("<I", game, 0x54)[0] == 1 and struct.unpack_from("<I", game, 0x28)[0] == 0
            and struct.unpack_from("<I", body, 0x4f0)[0] == 0)


def local_state13_axes_scope(header, values, boundary, fields):
    if not values:
        return {}
    return {"unclaimed_control_values": values,
            "unclaimed_control_fields": [{"actor": header["local_actor"], "permanent_player": 0, "session_state": 13,
                                          "boundary": boundary, "fields": list(fields),
                                          "source_writer": "422a90→422f60 reads unrecorded device4d3890+78/+7c",
                                          "scope": "non-reversing local full body only; AI mapper overwrites these axes before415090; all consumed controls and physical outputs remain compared"}]}


def local_session_lifecycle_values(snapshot):
    lifecycle = snapshot["shared"]["lifecycle"]
    timers = struct.unpack("<5I", raw_hex(lifecycle["session_timers_hex"], 20, "local session timer origins"))
    clock = struct.unpack("<I", raw_hex(lifecycle["lifecycle_clock_hex"], 4, "local session cached clock"))[0]
    if clock != snapshot["race_clock"]:
        raise ValueError("Local session cached clock aliases disagree")
    flag = integer(lifecycle["local_session_entry_flag"], 0, 1, "local session entry flag")
    pending = struct.unpack("<I", bytes.fromhex(snapshot["shared"]["finish_timing"]["pending_state_hex"]))[0]
    return (*timers, clock, flag, snapshot["session_state"], pending)


def validate_local_session_lifecycle(header, records):
    if not retained_local_session_enabled(header) or not records:
        raise ValueError("Missing retained local session lifecycle")
    initial = header.get("local_session_initial_state")
    if not isinstance(initial, dict) or set(initial) != {"entry_flag"}:
        raise ValueError("Invalid retained local session initial state")
    values = local_session_lifecycle_values(records[0]["pre"])
    if integer(initial["entry_flag"], 0, 1, "initial local session entry flag") != values[6] or values[7] != (9 if cold_local_session_enabled(header) else 6):
        raise ValueError("Retained local session must begin at observed active state6 or its complete cold grid")
    previous_clock = values[5]
    warm = retained_warm_restart_enabled(header)
    previous_state = records[0]["pre"]["session_state"]
    for index, record in enumerate(records):
        for entry in record["combined_journal"]:
            if entry["kind"] == "primary":
                event = entry["event"]
                if set(event) != {"order", "cached_clock"} or event["cached_clock"] != previous_clock and not (warm and previous_state == 13):
                    raise ValueError("Primary dispatch cached clock differs from retained caller chronology")
                integer(event["cached_clock"], 0, 0xffffffff, "primary cached clock")
                integer(event["order"], 1, 0x7fffffff, "primary dispatch order")
                previous_clock = event["cached_clock"]
            elif entry["kind"] == "restart":
                if not warm: raise ValueError("Restart event lacks warm provenance")
                previous_clock = integer(entry["event"]["creation_clock"], 0, 0xffffffff, "restart creation clock")
            elif entry["kind"] == "warm_grid":
                if not warm: raise ValueError("Warm grid event lacks warm provenance")
                validate_warm_grid_event(entry["event"])
        previous_clock = record["ai_pre"]["race_clock"]
        previous_state = record["ai_post"]["session_state"]
    return values[:7]


def capture_session_boundary(debugger, configuration):
    return {**{name + "_hex": debugger.memory(address, size).hex()
               for name, (address, size) in SESSION_BOUNDARY_BLOCKS.items()},
            "configuration_tail_hex": debugger.memory(configuration + 0x5fc, 0x30).hex()}



LOCAL_HANDOFF_CALLS = {0: {"entry": 0x402be0, "returns": [0x42160b, 0x4217aa]},
                       1: {"entry": 0x402c80, "returns": [0x421827]}}
LOCAL_HANDOFF_MANIFEST = {"calls": {str(key): value for key, value in LOCAL_HANDOFF_CALLS.items()},
                          "gameplay_bytes": 0x124, "dynamics_bytes": 0x338,
                          "max_events_per_update": 16, "scope": "local-controller-call-entry-return"}
LOCAL_HANDOFF_FIELDS = ("human", "reverse_fallback", "countdown", "player", "gear", "queued_shift",
                        "mode_steering", "mode_throttle", "mode_brake", "mode_transmission",
                        "saved_steering", "saved_throttle", "saved_brake", "saved_transmission",
                        *("tire_offset_" + str(index) for index in range(8)))

NEAREST_RECOVERY_MANIFEST = {"entry": 0x4046a3, "exit": 0x404731, "nearest_map": [0x4bcce0, 256],
                             "body_bytes": 0x53c, "gameplay_bytes": 0x124, "max_events_per_update": 16,
                             "scope": "human nearest-route block after ordinary gate progression"}


def nearest_map_content(initial, point_count):
    raw = raw_hex(initial["nearest_map_hex"], 256, "nearest-route map")
    integer(point_count, 1, 256, "nearest-route POINTS count")
    if any(candidate >= point_count for candidate in raw[:point_count]):
        raise ValueError("Nearest-route map candidate outside initialized POINTS")
    if initial["nearest_map_crc32"] != f"{zlib.crc32(raw):08x}":
        raise ValueError("Nearest-route map CRC32 mismatch")
    return raw


def nearest_recovery_values(snapshot):
    body = raw_hex(snapshot["body_hex"], 0x53c, "nearest recovery body")
    game = raw_hex(snapshot["gameplay_hex"], 0x124, "nearest recovery gameplay")
    return {"body_x": struct.unpack_from("<i", body, 0xbc)[0], "body_z": struct.unpack_from("<i", body, 0xc4)[0],
            "route_next": struct.unpack_from("<h", game, 0xa2)[0], "route_previous": struct.unpack_from("<h", game, 0xa4)[0],
            "refresh": struct.unpack_from("<I", game, 0x3c)[0], "human": struct.unpack_from("<I", game, 8)[0],
            "rng": integer(snapshot["rng"], 0, 0xffffffff, "nearest recovery RNG")}


def validate_nearest_recoveries(header, record):
    count = ai_content(header["ai_initial_state"])["point_count"]
    table = nearest_map_content(header["nearest_recovery_initial_state"], count)
    resolved = bytes.fromhex(header["ai_initial_state"]["resolved_ai_hex"])
    expected = []
    for actor, entry in enumerate(record["ai_pre"]["actors"]):
        pre = bytes.fromhex(entry["gameplay_hex"])
        post = bytes.fromhex(record["ai_post"]["actors"][actor]["gameplay_hex"])
        human = struct.unpack_from("<I", pre, 8)[0]
        if human != struct.unpack_from("<I", post, 8)[0]:
            raise ValueError("Nearest recovery profile does not include controller handoffs")
        period = struct.unpack_from("<b", resolved, pre[0x8d] * 40 + 0x13)[0]
        if struct.unpack_from("<I", pre, 4)[0] and human and struct.unpack_from("<b", pre, 0x86)[0] >= period:
            expected.append(actor)
    events = record["nearest_recoveries"]
    if not isinstance(events, list) or len(events) > 16 or [event["actor"] for event in events] != expected:
        raise ValueError("Missing, extra or out-of-order actual human nearest-recovery checks")
    for event in events:
        actor = integer(event["actor"], 0, 15, "nearest recovery actor")
        for phase, pc in (("pre", 0x4046a3), ("post", 0x404731)):
            state = event[phase]
            if state["boundary"] != f"{pc:x}-{'entry' if phase == 'pre' else 'exit'}":
                raise ValueError("Unexpected nearest-recovery internal boundary")
            if (state["actor_index"], state["cursor_pointer"], state["position_pointer"]) != (actor, LAYOUT["gameplay_base"] + actor * 0x124 + 0xa2, LAYOUT["body_base"] + actor * 0x53c + 0xbc):
                raise ValueError("Nearest recovery source register/frame provenance mismatch")
            values = nearest_recovery_values(state)
            if values["human"] != 1 or values["refresh"] != 1 or not 0 <= values["route_next"] < count or not 0 <= values["route_previous"] < count:
                raise ValueError("Invalid nearest recovery cursor or human/refresh gate")
            if raw_hex(state["nearest_map_hex"], 256, "nearest recovery map") != table:
                raise ValueError("Nearest-route map changed inside captured interval")
        for name in ("body", "gameplay"):
            before, after = (bytes.fromhex(event[phase][name + "_hex"]) for phase in ("pre", "post"))
            if any(left != right and not (name == "gameplay" and 0xa2 <= offset < 0xa6)
                   for offset, (left, right) in enumerate(zip(before, after))):
                raise ValueError("Unexpected retained raw nearest-recovery change: " + name)
        if event["pre"]["rng"] != event["post"]["rng"]:
            raise ValueError("Nearest recovery unexpectedly changed source RNG")


def local_handoff_values(snapshot):
    game = raw_hex(snapshot["gameplay_hex"], 0x124, "handoff gameplay")
    dynamics = raw_hex(snapshot["dynamics_hex"], 0x338, "handoff dynamics")
    values = [struct.unpack_from("<I", game, offset)[0] for offset in (8, 0x28)]
    values += [game[0x83], struct.unpack_from("<b", game, 0x80)[0], dynamics[0x330],
               struct.unpack_from("<I", dynamics, 0x14c)[0]]
    values += [struct.unpack_from("<i", dynamics, offset)[0] for offset in (0x310, 0x314, 0x318, 0x27c)]
    values += list(struct.unpack_from("<4i", game, 0x60))
    values += list(struct.unpack_from("<8i", dynamics, 0xdc))
    integer(values[0], 0, 1, "handoff human ownership")
    integer(values[3], -1, 3, "handoff player identity")
    integer(snapshot["rng"], 0, 0xffffffff, "handoff RNG")
    integer(snapshot["ai_tire_base"], -0x80000000, 0x7fffffff, "handoff AI tire base")
    return dict(zip(LOCAL_HANDOFF_FIELDS, values))


def validate_local_handoffs(record, local):
    events = record["local_handoffs"]
    if not isinstance(events, list) or len(events) > 16:
        raise ValueError("Invalid local controller handoff event count")
    for event in events:
        kind = integer(event["kind"], 0, 1, "handoff kind")
        if event["actor"] != local or event["return_address"] not in LOCAL_HANDOFF_CALLS[kind]["returns"]:
            raise ValueError("Unexpected local controller handoff actor/caller")
        if struct.unpack("<2I", raw_hex(event["stack_hex"], 8, "handoff source stack")) != (event["return_address"], local):
            raise ValueError("Local handoff stack provenance disagrees with caller/actor")
        for phase in ("pre", "post"):
            snapshot = event[phase]
            if snapshot["boundary"] != f"{LOCAL_HANDOFF_CALLS[kind]['entry']:x}-{'entry' if phase == 'pre' else 'return'}":
                raise ValueError("Unexpected local handoff boundary")
            local_handoff_values(snapshot)
        #Validate the callee's complete retained raw state, beyond the named
        #outputs. Later421390/classification writes must not leak into a call.
        allowed = {"gameplay": [(8, 4)], "dynamics": [(0xdc, 32), (0x27c, 4), (0x310, 12)]}
        if kind == 0:
            allowed["gameplay"] += [(0x28, 4), (0x60, 16), (0x83, 1)]
        else:
            allowed["dynamics"] += [(0x14c, 1)]
        for name, spans in allowed.items():
            before, after = (bytes.fromhex(event[phase][name + "_hex"]) for phase in ("pre", "post"))
            writable = {index for start, size in spans for index in range(start, start + size)}
            if any(left != right and index not in writable for index, (left, right) in enumerate(zip(before, after))):
                raise ValueError("Unexpected retained raw controller call change: " + name)
        if event["pre"]["ai_tire_base"] != event["post"]["ai_tire_base"]:
            raise ValueError("AI tire base changed inside local controller transaction")


def compare_local_handoffs(header, records, path):
    import csv
    if header.get("local_handoff_manifest") != LOCAL_HANDOFF_MANIFEST:
        raise ValueError("Local handoff comparison requires original call-entry/return provenance")
    columns = ["id", "event", "actor", "kind", "return_address", "rng_final", *LOCAL_HANDOFF_FIELDS]
    expected = []
    observations = []
    for record in records:
        for index, event in enumerate(record["local_handoffs"]):
            expected.append({"id": record["id"], "event": index, "actor": event["actor"], "kind": event["kind"],
                             "return_address": event["return_address"], "rng_final": event["post"]["rng"],
                             **local_handoff_values(event["post"])})
            observations.append({"update": record["id"], "event": index, "actor": event["actor"], "kind": event["kind"],
                                 "return_address": f"{event['return_address']:08x}"})
    differences = []
    with open(path, newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != columns:
            raise ValueError("Unexpected local handoff sidecar header")
        rows = list(reader)
    if len(rows) != len(expected):
        raise ValueError("Missing or extra local handoff sidecar events")
    for source, actual in zip(expected, rows):
        for name in columns:
            value = int(actual[name])
            if name in ("id", "event", "actor", "kind", "return_address") and value != source[name]:
                raise ValueError("Out-of-order or wrong local handoff sidecar event")
            if value != source[name]:
                differences.append({"update": source["id"], "event": source["event"], "field": name,
                                    "original": source[name], "native": value})
    return {"parity": bool(expected) and not differences, "observed_event_parity": not differences if expected else None,
            "event_count": len(expected), "compared_values": len(expected) * (len(LOCAL_HANDOFF_FIELDS) + 1),
            "mismatch_count": len(differences), "mismatches": differences[:100], "events": observations,
            "exact_prefix_updates": min((entry["update"] for entry in differences), default=len(records)) if expected else 0,
            "input_boundary": "actual controller callee entry", "expected_boundary": "actual controller callee return",
            "whole_session_parity": None, "outside_compared_fields": ["later421390 effects", "classification", "autonomous session progression"]}


def fixed_reads(globals_map, ai_state=False, checkpoint_timing=False, finish_timing=False, session_boundaries=False, traffic_controls=False):
    """Known image-data ranges; dependencies are resolved while still paused."""
    requests = [*BLOCKS.values(), *PROVENANCE_BLOCKS.values(), *LIFECYCLE_BLOCKS.values(), *((address, 4) for address in WORDS.values()),
                *((address, 4) for address in globals_map.values()), (0x4B0660, 4), (0x4D37A0, 4), (0x4D5634, 4),
                (0x4C5D58, 0x30), (0x4F2A50, 16)]
    requests.extend((LAYOUT[name + "_base"], ACTORS * LAYOUT[name + "_stride"]) for name in ("body", "dynamics", "gameplay"))
    if ai_state:
        requests.extend(AI_STATE_BLOCKS.values())
    if checkpoint_timing:
        requests.extend(CHECKPOINT_TIMING_BLOCKS.values())
    if finish_timing:
        requests.extend(FINISH_TIMING_BLOCKS.values())
    if session_boundaries:
        requests.extend(SESSION_BOUNDARY_BLOCKS.values())
    if traffic_controls:
        requests.extend(TRAFFIC_CONTROL_BLOCKS.values())
    return requests


def route_descriptor(raw):
    if len(raw) != 16:
        raise ValueError("AI route descriptor requires sixteen bytes")
    pointer, count, barrier_count, barriers = struct.unpack("<4I", raw)
    integer(count, 3, 256, "live POINTS count")
    #479182 keeps the archive's live block address. Packed entries can be
    #unaligned (observed020a5932); x86 int loads do not require alignment.
    if pointer < 0x10004 or pointer + count * 64 + 4 > 0xffffffff:
        raise ValueError("Invalid live POINTS pointer/range")
    integer(barrier_count, 0, 0x8000, "live barrier count")
    if barriers != pointer + count * 64 + 4:
        raise ValueError("AI route/barrier descriptor pointers disagree")
    return pointer, count, barrier_count, barriers


def ai_content(raw):
    """Validate observed setup state; no canonical-disk route substitution."""
    resolved = raw_hex(raw["resolved_ai_hex"], 0x540, "resolved AI setup")
    descriptor = raw_hex(raw["route_descriptor_hex"], 16, "AI route descriptor")
    pointer, count, _, _ = route_descriptor(descriptor)
    points = raw_hex(raw["points_hex"], 4 + count * 64, "live initialized POINTS")
    if struct.unpack_from("<I", points)[0] != count:
        raise ValueError("Live POINTS prefix disagrees with captured descriptor count")
    start = integer(raw["start_bound"], 0, 0xffffffff, "startline start bound") & 0xff
    end = integer(raw["end_bound"], 0, 0xffffffff, "startline end bound") & 0xff
    if start != 25 or end != count - 8 or not 0 <= start < end <= count:
        raise ValueError("Startline bounds differ from4019f0 initialized live route")
    vectors = count - end + start
    primary = raw_hex(raw["startline_primary_hex"], vectors * 12, "startline primary targets")
    alternate = raw_hex(raw["startline_alternate_hex"], vectors * 12, "startline alternate targets")
    for offset in (0x53c, 0x53d, 0x53e):
        integer(resolved[offset], 0, 100, "resolved AI distribution count")
    for name, data in (("resolved_ai", resolved), ("points", points), ("startline_primary", primary), ("startline_alternate", alternate)):
        if raw[name + "_crc32"] != f"{zlib.crc32(data):08x}":
            raise ValueError("Incorrect AI state CRC32: " + name)
    return {"resolved_ai": resolved, "points": points, "startline_primary": primary,
            "startline_alternate": alternate, "route_pointer": pointer, "point_count": count, "startline_vectors": vectors}


def capture_ai_state(debugger):
    """Observe initial setup and per-boundary stability using actual memory."""
    blocks = {name: debugger.memory(address, size) for name, (address, size) in AI_STATE_BLOCKS.items()}
    pointer, count, _, _ = route_descriptor(blocks["route_descriptor"])
    #The cache queues aligned debugger words, then slices the original packed
    #POINTS range exactly; adjacent archive bytes never enter provenance/CRC.
    aligned = (pointer - 4) & ~3
    extent = (pointer - 4 - aligned + 4 + count * 64 + 3) & ~3
    debugger.preload([(aligned, extent), (0x4BD790, 33 * 12), (0x4BD5D8, 33 * 12)])
    content = {"resolved_ai_hex": blocks["resolved_ai"].hex(), "route_descriptor_hex": blocks["route_descriptor"].hex(),
               "points_hex": debugger.memory(pointer - 4, 4 + count * 64).hex(),
               "start_bound": struct.unpack("<I", blocks["startline_start"])[0],
               "end_bound": struct.unpack("<I", blocks["startline_end"])[0],
               "startline_primary_hex": debugger.memory(0x4BD790, 33 * 12).hex(),
               "startline_alternate_hex": debugger.memory(0x4BD5D8, 33 * 12).hex()}
    for name in ("resolved_ai", "points", "startline_primary", "startline_alternate"):
        content[name + "_crc32"] = f"{zlib.crc32(bytes.fromhex(content[name + '_hex'])):08x}"
    ai_content(content)
    state = {"release_counter": struct.unpack("<I", blocks["release_counter"])[0],
             **{name: value for name, value in content.items() if not name.endswith("_hex")},
             "route_descriptor_hex": content["route_descriptor_hex"]}
    return state, content


def registered_prop_reads(reader):
    result = []
    for name, (count_address, address, stride) in PROPS.items():
        count = reader.word(count_address)
        if count > 1024:
            raise RuntimeError(f"Invalid original registered {name} count: {count}")
        if count:
            result.append((address, count * stride))
    return result


def sector_visible(sector, shared):
    if shared["sector_visibility_override"] or not shared["sector_visibility_disabled"]:
        return True
    if sector >= 2048:
        return False
    bits = bytes.fromhex(shared["sector_visibility_hex"])
    return bool(bits[sector // 8] & (1 << (sector % 8)))


def capture_checkpoint_timing(debugger, configuration):
    return {**{name + "_hex": debugger.memory(address, size).hex()
               for name, (address, size) in CHECKPOINT_TIMING_BLOCKS.items()},
            "session_stage": struct.unpack("<h", debugger.memory(configuration + 0x5f6, 2))[0]}


def capture(debugger, boundary, scalar_snapshot, ai_state=None, checkpoint_timing=None, finish_timing=None, session_boundary=None, traffic_control=None):
    """Read one common paused world; scalar_snapshot supplies local metadata."""
    result = dict(scalar_snapshot)
    result["boundary"] = boundary
    shared = {name + "_hex": debugger.memory(address, size).hex()
              for name, (address, size) in BLOCKS.items()}
    shared.update({name: debugger.word(address) for name, address in WORDS.items()})
    shared["provenance"] = {name + "_hex": debugger.memory(address, size).hex()
                            for name, (address, size) in PROVENANCE_BLOCKS.items()}
    shared["lifecycle"] = {name + "_hex": debugger.memory(address, size).hex()
                           for name, (address, size) in LIFECYCLE_BLOCKS.items()}
    if ai_state is not None:
        shared["ai_state"] = ai_state
    if checkpoint_timing is not None:
        shared["checkpoint_timing"] = checkpoint_timing
    if finish_timing is not None:
        shared["finish_timing"] = finish_timing
    if session_boundary is not None:
        shared["session_boundary"] = session_boundary
    if traffic_control is not None:
        shared["traffic_control"] = traffic_control
    shared["props"] = {}
    for name, (count_address, address, stride) in PROPS.items():
        count = debugger.word(count_address)
        if count > 1024:
            raise RuntimeError(f"Invalid original registered {name} count: {count}")
        shared["props"][name] = {"count": count, "stride": stride,
                                 "records_hex": debugger.memory(address, count * stride).hex() if count else ""}
    signs = shared["props"]["signs"]
    root_reads = []
    raw_signs = bytes.fromhex(signs["records_hex"])
    for index in range(signs["count"]):
        root, matrix = struct.unpack_from("<2I", raw_signs, index * signs["stride"])
        if root < 0x10000 or root & 3 or matrix != root + 0x5c:
            raise RuntimeError("Invalid original sign model transform pointer")
        root_reads.append((matrix, 0xe8))
    bodies = debugger.memory(LAYOUT["body_base"], ACTORS * LAYOUT["body_stride"])
    for actor in range(ACTORS):
        root, _, effect = struct.unpack_from("<3I", bodies, actor * LAYOUT["body_stride"])
        if any(pointer < 0x10000 or pointer & 3 for pointer in (root, effect)):
            raise RuntimeError(f"Invalid original model root for actor {actor}")
        root_reads.extend(((root + 0x120, 0x24), (effect + 0x140, 4)))
    debugger.preload(root_reads)
    models = []
    raw_signs = bytes.fromhex(signs["records_hex"])
    for index in range(signs["count"]):
        root, matrix = struct.unpack_from("<2I", raw_signs, index * signs["stride"])
        if root < 0x10000 or matrix != root + 0x5c:
            raise RuntimeError("Invalid original sign model transform pointer")
        model = debugger.memory(matrix, 0xe8)
        models.append(model[:64] + model[0xe4:0xe8])
    signs["model_records_hex"] = b"".join(models).hex()
    arrays = {name: debugger.memory(LAYOUT[name + "_base"], ACTORS * LAYOUT[name + "_stride"])
              for name in ("body", "dynamics", "gameplay")}
    actors = []
    for actor in range(ACTORS):
        entry = {"actor": actor}
        for name, raw in arrays.items():
            size = LAYOUT[name + "_stride"]
            entry[name + "_hex"] = raw[actor * size:(actor + 1) * size].hex()
        body = bytes.fromhex(entry["body_hex"])
        root = struct.unpack_from("<I", body, 0)[0]
        if root < 0x10000 or root & 3:
            raise RuntimeError(f"Invalid original physical root for actor {actor}: {root:08x}")
        registration = debugger.memory(root + 0x120, 0x24)
        sector, *neighbors = struct.unpack_from("<4H", registration)
        entry["root"] = {"pointer": root, "registration_hex": registration.hex(),
                         "sector": sector, "neighbors": neighbors,
                         "mask": registration[0x20], "detail": registration[0x21],
                         "sector_visible": sector_visible(sector, shared)}
        effect_root = struct.unpack_from("<I", body, 8)[0]
        if effect_root < 0x10000 or effect_root & 3:
            raise RuntimeError(f"Invalid original effect root for actor {actor}")
        entry["effect_root"] = {"pointer": effect_root, "flags_hex": debugger.memory(effect_root + 0x140, 4).hex()}
        actors.append(entry)
    result["actors"] = actors
    result["shared"] = shared
    return result


class Coverage:
    """Report actual fleet activity; a held-grid pilot is explicitly incomplete."""

    def __init__(self, initial):
        self.initial = [bytes.fromhex(actor["body_hex"]) for actor in initial["actors"]]
        self.moved = set()
        self.moving_updates = [0] * ACTORS
        self.modes = {}
        self.race_states = set()
        self.contacts = 0
        self.roughness_draws_mod15 = 0
        self.updates = 0
        self.released_updates = 0
        self.contact_updates = 0
        self.contact_pairs = set()
        self.confirmed_pair_occurrences = 0
        self.damage_feature_increases = 0
        self.broken_feature_transitions = 0
        self.damage_updates = 0
        self.detached_allocations = 0
        self.detached_active_updates = 0
        self.detached_position_changes = 0
        self.detached_flying_updates = 0
        self.detached_resting_updates = 0
        self.detached_landings = 0
        self.detached_retirements = 0
        self.detached_timer_expirations = 0
        self.roughness_support_draws = 0
        self.roughness_remainder_mismatches = 0
        self.previous_modes = [struct.unpack_from("<I", body, 0x4f0)[0] for body in self.initial]
        self.transitions = [0] * ACTORS
        self.transition_directions = {}
        self.roughness_updates = 0
        self.roughness_cursors = set()
        self.prop_changed_updates = {name: 0 for name in ("cones", "posts", "signs")}
        self.updates_with_damage = 0
        self.maximum_detached_count = 0

    def consume(self, consumed, post, ground=None):
        ground = ground or post
        self.updates += 1
        self.race_states.update((consumed["session_state"], post["session_state"]))
        # 421aa0 clears the initial hold through the real session clock; motion
        # is the observable release criterion here, not a manufactured flag.
        moving_this_update = 0
        for actor, entry in enumerate(post["actors"]):
            body = bytes.fromhex(entry["body_hex"])
            if body[0xBC:0xC8] != self.initial[actor][0xBC:0xC8]:
                self.moved.add(actor)
            speed = struct.unpack_from("<i", body, 0xE0)[0]
            if speed > 0x800:
                self.moving_updates[actor] += 1
                moving_this_update += 1
            mode = struct.unpack_from("<I", body, 0x4F0)[0]
            self.modes[str(mode)] = self.modes.get(str(mode), 0) + 1
            self.contacts += body[0x529]
            dispatch = struct.unpack_from("<I", bytes.fromhex(consumed["actors"][actor]["body_hex"]), 0x4f0)[0]
            previous = self.previous_modes[actor]
            if previous != dispatch and previous in (0, 1) and dispatch in (0, 1):
                self.transitions[actor] += 1
                direction = f"{previous}->{dispatch}"
                self.transition_directions[direction] = self.transition_directions.get(direction, 0) + 1
            self.previous_modes[actor] = dispatch
        if moving_this_update == ACTORS:
            self.released_updates += 1
        self.roughness_draws_mod15 += (ground["roughness_cursor"] - consumed["roughness_cursor"]) % 15
        self.roughness_updates += ground["roughness_cursor"] != consumed["roughness_cursor"]
        self.roughness_cursors.update((consumed["roughness_cursor"], ground["roughness_cursor"]))
        pairs = set()
        draws = 0
        for actor, entry in enumerate(ground["actors"]):
            body = bytes.fromhex(entry["body_hex"])
            dynamics = bytes.fromhex(entry["dynamics_hex"])
            for partner in body[0x52a:0x52a + body[0x529]]:
                pairs.add(tuple(sorted((actor, partner))))
            if struct.unpack_from("<I", body, 0x4f0)[0] == 0 and struct.unpack_from("<i", body, 0xe0)[0] > 0x8000:
                draws += sum((material & 0x7f) in (2, 3) for material in dynamics[0x328:0x330])
        self.contact_updates += bool(pairs)
        self.confirmed_pair_occurrences += len(pairs)
        self.contact_pairs.update(pairs)
        self.roughness_support_draws += draws
        self.roughness_remainder_mismatches += (ground["roughness_cursor"] - consumed["roughness_cursor"] - draws) % 15 != 0
        before, after = consumed["shared"], ground["shared"]
        levels_before = bytes.fromhex(before["damage_levels_hex"])
        levels_after = bytes.fromhex(after["damage_levels_hex"])
        increases = sum(max(new - old, 0) for old, new in zip(levels_before, levels_after))
        broken_before = struct.unpack("<64I", bytes.fromhex(before["broken_features_hex"]))
        broken_after = struct.unpack("<64I", bytes.fromhex(after["broken_features_hex"]))
        fractures = sum(not old and bool(new) for old, new in zip(broken_before, broken_after))
        self.damage_feature_increases += increases
        self.broken_feature_transitions += fractures
        self.damage_updates += bool(increases or fractures)
        free_before = struct.unpack("<10I", bytes.fromhex(before["detached_free_hex"]))
        free_after = struct.unpack("<10I", bytes.fromhex(after["detached_free_hex"]))
        self.detached_allocations += sum(bool(old) and not new for old, new in zip(free_before, free_after))
        self.detached_active_updates += ground["shared"]["detached_count"] > 0
        for name in self.prop_changed_updates:
            self.prop_changed_updates[name] += before["props"][name]["records_hex"] != post["shared"]["props"][name]["records_hex"]
        self.updates_with_damage += any(levels_after) or any(broken_after)
        self.maximum_detached_count = max(self.maximum_detached_count, after["detached_count"])
        pools = [bytes.fromhex(state["shared"]["detached_pool_hex"]) for state in (consumed, ground, post)]
        flying, resting = False, False
        for slot in range(10):
            offset = slot * 0x11c
            for raw in pools[1:]:
                active = struct.unpack_from("<I", raw, offset + 0x108)[0] != 0
                settled = struct.unpack_from("<I", raw, offset + 0x118)[0] != 0
                flying |= active and not settled
                resting |= active and settled
            for previous, current in zip(pools, pools[1:]):
                was_active = struct.unpack_from("<I", previous, offset + 0x108)[0] != 0
                is_active = struct.unpack_from("<I", current, offset + 0x108)[0] != 0
                was_resting = struct.unpack_from("<I", previous, offset + 0x118)[0] != 0
                is_resting = struct.unpack_from("<I", current, offset + 0x118)[0] != 0
                self.detached_position_changes += was_active and is_active and previous[offset + 0xec:offset + 0xf8] != current[offset + 0xec:offset + 0xf8]
                self.detached_landings += was_active and is_active and not was_resting and is_resting
                self.detached_retirements += was_active and not is_active
                self.detached_timer_expirations += was_active and not is_active and was_resting and struct.unpack_from("<i", previous, offset + 0x110)[0] < 1
        self.detached_flying_updates += flying
        self.detached_resting_updates += resting

    def summary(self, scenario=None):
        return {"updates": self.updates, "actors_moved": sorted(self.moved),
                "moving_updates_per_actor": self.moving_updates, "body_mode_actor_updates": self.modes,
                "session_states": sorted(self.race_states), "confirmed_neighbor_entries": self.contacts,
                "roughness_advances_mod15": self.roughness_draws_mod15,
                "all_actors_moving_updates": self.released_updates,
                "contact_updates": self.contact_updates, "unique_contact_pairs": sorted(self.contact_pairs),
                "confirmed_pair_occurrences": self.confirmed_pair_occurrences,
                "damage_feature_level_increases": self.damage_feature_increases,
                "broken_feature_transitions": self.broken_feature_transitions, "damage_updates": self.damage_updates,
                "detached_allocations": self.detached_allocations, "detached_active_updates": self.detached_active_updates,
                "detached_position_changes": self.detached_position_changes,
                "detached_flying_updates": self.detached_flying_updates, "detached_resting_updates": self.detached_resting_updates,
                "detached_landings": self.detached_landings, "detached_retirements": self.detached_retirements,
                "detached_timer_expirations": self.detached_timer_expirations,
                "detached_coverage": bool(self.detached_allocations and self.detached_position_changes),
                "roughness_draws_from_full_supports": self.roughness_support_draws,
                "roughness_remainder_mismatches": self.roughness_remainder_mismatches,
                "roughness_changed_updates": self.roughness_updates,
                "observed_roughness_cursors": sorted(self.roughness_cursors),
                "full_distant_transitions_per_actor": self.transitions,
                "full_distant_transition_directions": self.transition_directions,
                "actors_with_repeated_full_distant_transitions": [actor for actor, count in enumerate(self.transitions) if count >= 2],
                "prop_controller_changed_updates": self.prop_changed_updates,
                "updates_with_damage": self.updates_with_damage,
                "maximum_detached_count": self.maximum_detached_count,
                "contact_coverage": bool(self.contact_updates), "damage_coverage": bool(self.damage_updates),
                "roughness_coverage": bool(self.roughness_support_draws),
                "passed": len(self.moved) == ACTORS and self.released_updates > 0
                          and (scenario != "fleet-pursuit" or self.contact_updates > 0)
                          and (scenario != "fleet-detachment" or bool(self.detached_allocations and self.detached_position_changes)),
                "scope": "observed fleet activity; no autonomous native AI parity claim"}


def integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"Invalid {label}: {value!r}")
    return value


def raw_hex(value, size, label):
    if not isinstance(value, str):
        raise ValueError(f"Invalid {label} hex")
    raw = bytes.fromhex(value)
    if len(raw) != size:
        raise ValueError(f"Incorrect {label} size: {len(raw)} != {size}")
    return raw


def validate_ai_state(state):
    integer(state["release_counter"], 0, 360, "AI release counter")
    route_descriptor(raw_hex(state["route_descriptor_hex"], 16, "AI route descriptor"))
    for name in ("start_bound", "end_bound"):
        integer(state[name], 0, 0xffffffff, "AI " + name)
    for name in ("resolved_ai", "points", "startline_primary", "startline_alternate"):
        value = state[name + "_crc32"]
        if not isinstance(value, str) or len(value) != 8 or any(letter not in "0123456789abcdef" for letter in value):
            raise ValueError("Invalid AI state CRC32: " + name)


def validate_snapshot(state, boundary):
    if state["boundary"] != boundary:
        raise ValueError(f"Expected common fleet boundary {boundary}")
    local = integer(state["actor"], 0, 15, "local actor")
    for name in ("rng", "update_counter", "qpc_low"):
        integer(state[name], 0, 0xffffffff, name)
    integer(state["delta_q16"], 1, 0x30000, "delta")
    integer(state["reciprocal_q16"], 1, 0x7fffffff, "reciprocal")
    integer(state["roughness_cursor"], 0, 14, "roughness cursor")
    integer(state["limiter_active"], 0, 1, "limiter")
    integer(state["limiter_load_q16"], -0x80000000, 0x7fffffff, "limiter load")
    if type(state["extra_update"]) is not bool:
        raise ValueError("Fleet extra_update must be boolean")
    if state["source_mode"] != 0 or state["allocated_actor_count"] != ACTORS:
        raise ValueError("Fleet snapshot is not a16-actor Single Race")
    integer(state["session_state"], 0, 0xffff, "race state")
    actors = state["actors"]
    if len(actors) != ACTORS:
        raise ValueError("Fleet snapshot must contain all16 actors")
    shared = state["shared"]
    if "ai_state" in shared:
        validate_ai_state(shared["ai_state"])
        if struct.unpack_from("<I", bytes.fromhex(shared["ai_state"]["route_descriptor_hex"]), 12)[0] != shared["barrier_records_pointer"]:
            raise ValueError("AI route descriptor differs from captured barrier pointer")
    if "checkpoint_timing" in shared:
        timing = shared["checkpoint_timing"]
        if "race_clock" not in state:
            raise ValueError("Checkpoint timing requires observed race clock")
        integer(timing["session_stage"], -0x8000, 0x7fff, "source session stage")
        for name, (_, size) in CHECKPOINT_TIMING_BLOCKS.items():
            timing[name] = raw_hex(timing[name + "_hex"], size, "checkpoint " + name)
    if "traffic_control" in shared:
        traffic = shared["traffic_control"]
        for name, (_, size) in TRAFFIC_CONTROL_BLOCKS.items():
            traffic[name] = raw_hex(traffic[name + "_hex"], size, "traffic " + name)
        traffic["force_acceleration"] = raw_hex(traffic["force_acceleration_hex"], 4, "traffic force acceleration")
        if struct.unpack("<I", traffic["brake_bypass_pointer"])[0] != traffic["bypass_pointer"]:
            raise ValueError("Traffic brake-bypass pointer observations disagree")
    if "session_boundary" in shared:
        session = shared["session_boundary"]
        for name, (_, size) in SESSION_BOUNDARY_BLOCKS.items():
            session[name] = raw_hex(session[name + "_hex"], size, "session boundary " + name)
        session["configuration_tail"] = raw_hex(session["configuration_tail_hex"], 0x30, "session boundary configuration tail")
    if "finish_timing" in shared:
        for name, (_, size) in FINISH_TIMING_BLOCKS.items():
            shared["finish_timing"][name] = raw_hex(shared["finish_timing"][name + "_hex"], size, "finish " + name)
        counters = shared["finish_timing"]["race_counters"]
        if struct.unpack_from("<2I", counters, 4) != (state["update_counter"], state["race_clock"]):
            raise ValueError("Finish lifecycle counters differ from source scalar observations")
    for name, (_, size) in BLOCKS.items():
        shared[name] = raw_hex(shared[name + "_hex"], size, name)
    if "provenance" in shared:
        for name, (_, size) in PROVENANCE_BLOCKS.items():
            shared["provenance"][name] = raw_hex(shared["provenance"][name + "_hex"], size, name)
    if "race_clock" in state:
        integer(state["race_clock"], 0, 0xffffffff, "race clock")
    if "lifecycle" in shared:
        for name, (_, size) in LIFECYCLE_BLOCKS.items():
            shared["lifecycle"][name] = raw_hex(shared["lifecycle"][name + "_hex"], size, name)
        if state["race_clock"] != struct.unpack_from("<I", shared["lifecycle"]["race_lifecycle"], 8)[0]:
            raise ValueError("Decoded race clock differs from source lifecycle state")
    for name in WORDS:
        integer(shared[name], 0, 0xffffffff, name)
    integer(shared["detached_count"], 0, 10, "detached count")
    integer(shared["carcontacts_enabled"], 0, 1, "car contacts flag")
    for actor in range(ACTORS):
        integer(struct.unpack_from("<I", shared["limiter_active"], actor * 4)[0], 0, 1, "actor limiter flag")
    if set(shared["props"]) != set(PROPS):
        raise ValueError("Missing shared registered prop pools")
    for name, (_, _, stride) in PROPS.items():
        pool = shared["props"][name]
        count = integer(pool["count"], 0, 1024, name + " count")
        if pool["stride"] != stride:
            raise ValueError("Unknown original prop record stride")
        pool["records"] = raw_hex(pool["records_hex"], count * stride, name)
        if name == "signs" and "model_records_hex" in pool:
            pool["model_records"] = raw_hex(pool["model_records_hex"], count * 68, "live sign models")
    for actor, entry in enumerate(actors):
        if type(entry["actor"]) is not int or entry["actor"] != actor:
            raise ValueError("Missing, repeated or reordered fleet actor")
        for name in ("body", "dynamics", "gameplay"):
            entry[name] = raw_hex(entry[name + "_hex"], LAYOUT[name + "_stride"], name)
        body = entry["body"]
        integer(struct.unpack_from("<I", body, 0x4f0)[0], 0, 4, "body mode")
        count = integer(body[0x529], 0, 15, "contact neighbor count")
        neighbors = body[0x52a:0x52a + count]
        if any(partner >= ACTORS or partner == actor for partner in neighbors) or len(set(neighbors)) != count:
            raise ValueError("Invalid confirmed contact neighbors")
        root = entry["root"]
        integer(root["sector"], 0, 0xffff, "root sector")
        if len(root["neighbors"]) != 3:
            raise ValueError("Physical root must have three retained neighbor slots")
        for neighbor in root["neighbors"]:
            integer(neighbor, 0, 0xffff, "root neighbor")
        for name in ("mask", "detail"):
            integer(root[name], 0, 255, "root " + name)
        registration = raw_hex(root["registration_hex"], 0x24, "physical root registration")
        if root["pointer"] != struct.unpack_from("<I", body, 0)[0]:
            raise ValueError("Physical root pointer differs from body+0 (body+8 is the LOD/effect child)")
        effect = entry["effect_root"]
        if effect["pointer"] != struct.unpack_from("<I", body, 8)[0]:
            raise ValueError("Effect root pointer differs from body+8")
        effect["flags"] = raw_hex(effect["flags_hex"], 4, "effect root flags")
        for pointer in (root["pointer"], effect["pointer"]):
            integer(pointer, 0x10000, 0xffffffff, "original root pointer")
            if pointer & 3:
                raise ValueError("Unaligned original root pointer")
        if [root["sector"], *root["neighbors"]] != list(struct.unpack_from("<4H", registration)):
            raise ValueError("Decoded physical sector registration differs")
        if (root["mask"], root["detail"]) != (registration[0x20], registration[0x21]):
            raise ValueError("Decoded physical root flags differ")
        if type(root["sector_visible"]) is not bool or root["sector_visible"] != sector_visible(root["sector"], shared):
            raise ValueError("Decoded physical sector visibility differs")
    for name in ("body", "dynamics", "gameplay"):
        if raw_hex(state[name + "_hex"], LAYOUT[name + "_stride"], "local " + name) != actors[local][name]:
            raise ValueError("Local scalar snapshot differs from complete actor array")
    dynamics = actors[local]["dynamics"]
    for name, offset in (("left", 0x138), ("right", 0x13c), ("throttle", 0x140), ("brake", 0x144), ("gear_edge", 0x14c)):
        form = "b" if name == "gear_edge" else "i"
        if state["input_q16"][name] != struct.unpack_from("<" + form, dynamics, offset)[0]:
            raise ValueError("Decoded human input differs from source dynamics")
    if state["car_id"] != actors[local]["body"][0x516] or state["transmission_mode"] != struct.unpack_from("<I", dynamics, 0x27c)[0]:
        raise ValueError("Decoded local vehicle configuration differs")
    if state["limiter_active"] != struct.unpack_from("<I", shared["limiter_active"], 4 * local)[0] or state["limiter_load_q16"] != struct.unpack_from("<i", shared["limiter_load"], 4 * local)[0]:
        raise ValueError("Decoded local limiter differs from global array")
    return state


def validate_update_counters(record, finish_profile=False):
    """Allow only the source421390 first-finisher reset, with raw evidence."""
    expected = (record["pre"]["update_counter"] + 1) & 0xffffffff
    later = [name for name in (*BOUNDARIES, *AI_BOUNDARIES, *TRAFFIC_BOUNDARIES) if name != "pre" and name in record]
    if all(record[name]["update_counter"] == expected for name in later):
        return False
    if not finish_profile or "ai_pre" not in record or "ai_post" not in record:
        raise ValueError("Missed source fleet update boundary")
    before, after = record["ai_pre"], record["ai_post"]
    pre = before["shared"]["finish_timing"]
    post = after["shared"]["finish_timing"]
    old_count = bytes.fromhex(pre["finish_counts_hex"])[0]
    new_count = bytes.fromhex(post["finish_counts_hex"])[0]
    if old_count != 0 or not 1 <= new_count <= ACTORS or before["update_counter"] != expected:
        raise ValueError("Unexplained source fleet counter reset")
    first_driver = bytes.fromhex(post["finish_order_hex"])[0]
    local_flags = bytes.fromhex(pre["local_finish_flags_hex"])
    lap_limit = struct.unpack_from("<H", bytes.fromhex(before["shared"]["provenance"]["world_status_hex"]), 0x14)[0]
    context = before["shared"]["checkpoint_timing"]
    if before["session_state"] != 6 or context["session_stage"] not in (2, 4):
        raise ValueError("Counter reset is outside ordinary race finish timing")
    first_finisher = False
    for actor in range(ACTORS):
        old = bytes.fromhex(before["actors"][actor]["gameplay_hex"])
        new = bytes.fromhex(after["actors"][actor]["gameplay_hex"])
        identity = struct.unpack_from("<b", old, 0x80)[0]
        eligible = identity == -1 or 0 <= identity < 4 and local_flags[identity] != 0
        if (old[0x8d] == first_driver and eligible and struct.unpack_from("<I", old, 0x54)[0] == 0
                and struct.unpack_from("<I", new, 0x54)[0] == 1
                and struct.unpack_from("<h", old, 0xa8)[0] == lap_limit
                and struct.unpack_from("<h", new, 0xa8)[0] == lap_limit + 1):
            first_finisher = True
    if not first_finisher or any(record[name]["update_counter"] != 0 for name in later if name != "ai_pre"):
        raise ValueError("Counter reset lacks the source first-finisher transition")
    return True


def read_trace(path):
    """Require a complete CRC-protected schema2 observation, never partial data."""
    import json
    import zlib
    from pathlib import Path
    with Path(path).open("rb") as source:
        first = source.readline()
        if not first.endswith(b"\n"):
            raise ValueError("Truncated fleet header")
        header = json.loads(first)
        if (header.get("type"), header.get("schema"), header.get("scope"), header.get("exe_crc32")) != ("header", 2, "fleet", "8ce56d41"):
            raise ValueError("Expected canonical fleet schema2")
        if header.get("physical_root_body_offset") != 0:
            raise ValueError("Fleet capture lacks confirmed body+0 physical root registration")
        if header["layout"] != LAYOUT or header["boundaries"] != BOUNDARIES:
            raise ValueError("Unknown fleet layout or common boundaries")
        for field, expected in (("shared_blocks", BLOCKS), ("shared_words", WORDS), ("props_layout", PROPS)):
            normalized = {key: list(value) if isinstance(value, tuple) else value for key, value in expected.items()}
            if header[field] != normalized:
                raise ValueError("Unknown fleet shared-state manifest")
        if "extra_provenance_blocks" in header and header["extra_provenance_blocks"] != {key: list(value) for key, value in PROVENANCE_BLOCKS.items()}:
            raise ValueError("Unknown fleet extra provenance manifest")
        if "sign_model_stride" in header and header["sign_model_stride"] != 68:
            raise ValueError("Unknown original sign model snapshot stride")
        if "lifecycle_blocks" in header and header["lifecycle_blocks"] != {key: list(value) for key, value in LIFECYCLE_BLOCKS.items()}:
            raise ValueError("Unknown fleet lifecycle manifest")
        ai_fields = {"ai_state_manifest", "ai_initial_state", "ai_boundaries"}
        observed_ai = ai_fields.intersection(header)
        if observed_ai and observed_ai != ai_fields:
            raise ValueError("Incomplete initial AI provenance manifest")
        if observed_ai:
            if header["ai_state_manifest"] != AI_STATE_MANIFEST or header["ai_boundaries"] != AI_BOUNDARIES:
                raise ValueError("Unknown initial AI state manifest/boundaries")
            ai_content(header["ai_initial_state"])
        observed_timing = "checkpoint_timing_manifest" in header
        if observed_timing and (not observed_ai or header["checkpoint_timing_manifest"] != CHECKPOINT_TIMING_MANIFEST):
            raise ValueError("Unknown checkpoint timing manifest or missing AI boundaries")
        observed_finish = "finish_timing_manifest" in header
        if observed_finish and (not observed_timing or header["finish_timing_manifest"] != FINISH_TIMING_MANIFEST):
            raise ValueError("Unknown finish timing manifest or missing checkpoint timing")
        observed_handoffs = "local_handoff_manifest" in header
        if observed_handoffs and (not observed_ai or header["local_handoff_manifest"] != LOCAL_HANDOFF_MANIFEST):
            raise ValueError("Unknown local handoff manifest or missing AI boundaries")
        qualifying = header.get("qualifying", False)
        if type(qualifying) is not bool or qualifying and not observed_handoffs:
            raise ValueError("Qualifying fleet requires explicit local handoff provenance")
        nearest_fields = {"nearest_recovery_manifest", "nearest_recovery_initial_state"}
        observed_nearest = nearest_fields.intersection(header)
        if observed_nearest:
            if observed_nearest != nearest_fields or not observed_ai or observed_handoffs or qualifying or header["nearest_recovery_manifest"] != NEAREST_RECOVERY_MANIFEST:
                raise ValueError("Incomplete or unsupported nearest-recovery provenance manifest")
            nearest_map_content(header["nearest_recovery_initial_state"], ai_content(header["ai_initial_state"])["point_count"])
        observed_session = "session_boundary_manifest" in header
        if observed_session and (not observed_finish or not observed_handoffs or header["session_boundary_manifest"] != SESSION_BOUNDARY_MANIFEST):
            raise ValueError("Unknown session boundary manifest or missing finish/controller provenance")
        observed_local = retained_local_session_enabled(header)
        retained_warm_restart_enabled(header)
        if ("local_session_initial_state" in header) != observed_local:
            raise ValueError("Local session lifecycle state differs from its manifest")
        observed_skid = "skid_dispatch_manifest" in header
        if observed_skid and header["skid_dispatch_manifest"] != SKID_MANIFEST:
            raise ValueError("Unknown skid dispatcher provenance manifest")
        wheel_fields = {"wheel_queue_manifest", "wheel_queue_initial_state"}
        observed_wheel = wheel_fields.intersection(header)
        if observed_wheel:
            if observed_wheel != wheel_fields or not observed_skid or header["wheel_queue_manifest"] != WHEEL_QUEUE_MANIFEST:
                raise ValueError("Incomplete or unknown wheel queue provenance")
            validate_wheel_state(header["wheel_queue_initial_state"], "421aa0-entry")
        if "brake_publication_manifest" in header and header["brake_publication_manifest"] != BRAKE_PUBLICATION_MANIFEST:
            raise ValueError("Unknown brake publication manifest")
        observed_lamps = "human_lamps_manifest" in header
        if observed_lamps and (not observed_wheel or header["human_lamps_manifest"] != HUMAN_LAMPS_MANIFEST):
            raise ValueError("Unknown actual human-lamp callback provenance")
        if observed_wheel and ("published_brake_hex" in header["wheel_queue_initial_state"]) != observed_lamps:
            raise ValueError("Initial callback lamp state differs from its manifest")
        whine_fields = {"whine_manifest", "whine_initial_state", "whine_tables"}
        observed_whine = whine_fields.intersection(header)
        if observed_whine:
            if observed_whine != whine_fields or header["whine_manifest"] != WHINE_MANIFEST:
                raise ValueError("Incomplete or unknown WHINE provenance")
            whine_tables(header["whine_tables"])
            validate_whine_state(header["whine_initial_state"])
        traffic_fields = {"traffic_control_manifest", "traffic_initial_state", "traffic_boundaries"}
        observed_traffic = traffic_fields.intersection(header)
        if observed_traffic:
            if observed_traffic != traffic_fields or not observed_ai or header["traffic_control_manifest"] != TRAFFIC_CONTROL_MANIFEST or header["traffic_boundaries"] != TRAFFIC_BOUNDARIES:
                raise ValueError("Incomplete or unknown traffic/control provenance manifest")
            traffic_content(header["traffic_initial_state"])
            if header["traffic_initial_state"]["point_count"] != ai_content(header["ai_initial_state"])["point_count"]:
                raise ValueError("Traffic brake-bypass table count differs from live POINTS")
        joined_fields = {"joined_ai_manifest", "joined_initial_state", "grid_initialization"}
        observed_joined = joined_fields.intersection(header)
        if observed_joined:
            if observed_joined != joined_fields or not observed_traffic or observed_handoffs and not observed_local or qualifying or header["joined_ai_manifest"] != JOINED_AI_MANIFEST:
                raise ValueError("Incomplete or unsupported joined AI provenance")
            joined_content(header["joined_initial_state"], ai_content(header["ai_initial_state"])["point_count"])
            validate_start_call(header["grid_initialization"], 0x460170)
            if header.get("unrecorded_keyboard_warmup") and "contact_weather_manifest" not in header:
                raise ValueError("Joined AI capture must begin at observed cold grid initialization")
        cold_fields = {"cold_grid_initialization_manifest", "cold_grid_initialization"}
        observed_cold = cold_fields.intersection(header)
        if observed_cold:
            if observed_cold != cold_fields or not observed_joined or header["cold_grid_initialization_manifest"] != COLD_GRID_INIT_MANIFEST:
                raise ValueError("Incomplete or unsupported cold-grid boundary provenance")
            validate_cold_grid_initialization(header["cold_grid_initialization"], header["grid_initialization"])
        sequence_fields = {"cold_grid_sequence_manifest", "cold_grid_sequence"}
        observed_sequence = sequence_fields.intersection(header)
        if observed_sequence:
            if (observed_sequence != sequence_fields or observed_cold != cold_fields
                    or header["cold_grid_sequence_manifest"] != COLD_GRID_SEQUENCE_MANIFEST):
                raise ValueError("Incomplete or unsupported cold-grid sequence provenance")
            validate_cold_grid_sequence(header["cold_grid_sequence"], header["grid_initialization"], header["cold_grid_initialization"])
        ordered_clock_enabled(header)
        camera_controls_enabled(header)
        combined_fields = {"combined_rng_manifest", "combined_audio_initial", "combined_initial_rng"}
        observed_combined = combined_fields.intersection(header)
        if observed_combined and (observed_combined != combined_fields or not (observed_joined and observed_wheel and observed_whine) or header["combined_rng_manifest"] != COMBINED_MANIFEST):
            raise ValueError("Incomplete or unknown combined RNG provenance")
        contact_fields = {"contact_weather_manifest", "contact_audio_initial", "combined_initial_release_counter"}
        observed_contact = contact_fields.intersection(header)
        if observed_contact and (observed_contact != contact_fields or not observed_combined or header["contact_weather_manifest"] != CONTACT_WEATHER_MANIFEST):
            raise ValueError("Incomplete or unknown contact/weather provenance")
        if observed_local and not (observed_combined and observed_contact and observed_session and observed_finish):
            raise ValueError("Retained local session requires combined contact and extended finish provenance")
        if (header["session"], header["source_mode"], header["actor_count"], header["allocated_actor_count"]) != ("single-race", 0, ACTORS, ACTORS):
            raise ValueError("Not a complete Single Race fleet recording")
        local = integer(header["local_actor"], 0, 15, "header local actor")
        config = raw_hex(header["configuration_hex"], 0x620, "initial configuration")
        if any(struct.unpack_from("<I", config, offset)[0] != expected for offset, expected in ((4, 0), (0x20, 0 if qualifying else 1), (0xac, 1))):
            raise ValueError("Fleet capture did not select actual one-player NoQual")
        checksum = zlib.crc32(first)
        records = []
        footer = None
        derived_release_counter = header.get("combined_initial_release_counter", 0)
        for line in source:
            if not line.endswith(b"\n"):
                raise ValueError("Truncated fleet record")
            record = json.loads(line)
            if record.get("type") == "complete":
                footer = record
                if source.read(1):
                    raise ValueError("Data follows fleet completion")
                break
            checksum = zlib.crc32(line, checksum)
            if record.get("type") != "update" or type(record.get("id")) is not int or record.get("id") != len(records):
                raise ValueError("Missing, repeated or out-of-order fleet update")
            boundaries = {**BOUNDARIES, **(AI_BOUNDARIES if observed_ai else {}), **(TRAFFIC_BOUNDARIES if observed_traffic else {})}
            if observed_traffic and "traffic_pre" not in record:
                raise ValueError("Missing required traffic/control boundary")
            if not observed_traffic and any(name in record for name in TRAFFIC_BOUNDARIES):
                raise ValueError("Traffic boundary observations lack their initial manifest")
            if not observed_ai and any(name in record for name in AI_BOUNDARIES):
                raise ValueError("AI boundary observations lack their initial manifest")
            for name, boundary in boundaries.items():
                state = validate_snapshot(record[name], boundary)
                if ("ai_state" in state["shared"]) != bool(observed_ai):
                    raise ValueError("AI boundary state differs from initial manifest")
                if ("checkpoint_timing" in state["shared"]) != observed_timing:
                    raise ValueError("Checkpoint timing state differs from capture manifest")
                if ("traffic_control" in state["shared"]) != bool(observed_traffic):
                    raise ValueError("Traffic/control state differs from capture manifest")
                if ("joined_ai" in state["shared"]) != bool(observed_joined):
                    raise ValueError("Joined route/rank state differs from capture manifest")
                if observed_joined:
                    setup = state["shared"]["joined_ai"]
                    if setup != {"rank_threshold": header["joined_initial_state"]["rank_threshold"],
                                 "cumulative_route_crc32": header["joined_initial_state"]["cumulative_route_crc32"],
                                 "point_count": ai_content(header["ai_initial_state"])["point_count"]}:
                        raise ValueError("Initialized joined route/rank setup changed")
                if observed_traffic:
                    for field in ("bypass_pointer", "point_count", "brake_bypass_crc32"):
                        if state["shared"]["traffic_control"][field] != header["traffic_initial_state"][field]:
                            raise ValueError("Unsupported live traffic brake-bypass setup change")
                if ("session_boundary" in state["shared"]) != observed_session:
                    raise ValueError("Session boundary state differs from capture manifest")
                if ("finish_timing" in state["shared"]) != observed_finish:
                    raise ValueError("Finish timing state differs from capture manifest")
                if ("provenance" in state["shared"]) != ("extra_provenance_blocks" in header):
                    raise ValueError("Fleet provenance differs from its header manifest")
                if ("lifecycle" in state["shared"]) != ("lifecycle_blocks" in header):
                    raise ValueError("Fleet lifecycle differs from its header manifest")
                if ("local_session_entry_flag" in state["shared"].get("lifecycle", {})) != observed_local:
                    raise ValueError("Local session entry flag differs from its manifest")
                if observed_local: local_session_lifecycle_values(state)
                if ("model_records" in state["shared"]["props"]["signs"]) != ("sign_model_stride" in header):
                    raise ValueError("Sign model snapshots differ from header manifest")
                if state["actor"] != local:
                    raise ValueError("Local actor changed in fleet recording")
                for field in ("weather", "track_id", "steering_profile", "car_id"):
                    if state[field] != header["initial_configuration"][field]:
                        raise ValueError("Fleet recording configuration changed: " + field)
            if "publication_calls" in record and "brake_publication_manifest" not in header:
                raise ValueError("Publication observations lack their provenance manifest")
            if not observed_contact and "contact_audio_calls" in record:
                raise ValueError("Contact/audio observations lack their provenance manifest")
            if not observed_combined and any(name in record for name in ("combined_journal", "surface_calls", "combined_audio_pre", "combined_audio_post")):
                raise ValueError("Combined observations lack their initial provenance manifest")
            if observed_handoffs:
                validate_local_handoffs(record, local)
            elif "local_handoffs" in record:
                raise ValueError("Local handoff events lack their capture manifest")
            if observed_skid:
                validate_skid_dispatch(record)
            elif "skid_dispatch" in record:
                raise ValueError("Skid dispatcher calls lack their capture manifest")
            if observed_wheel:
                if not isinstance(record["wheel_queue_callbacks"], list):
                    raise ValueError("Invalid source wheel callback list")
                for callback in record["wheel_queue_callbacks"]:
                    validate_wheel_callback(callback)
                    if any(("published_brake_hex" in callback[phase]) != observed_lamps for phase in ("pre", "post")):
                        raise ValueError("Callback lamp gates/expectations differ from their manifest")
            elif "wheel_queue_callbacks" in record:
                raise ValueError("Wheel callbacks lack their capture manifest")
            if observed_whine:
                for phase in ("pre", "post"): validate_whine_state(record["whine_update_" + phase])
            elif "whine_callbacks" in record:
                raise ValueError("WHINE callbacks lack their capture manifest")
            if observed_nearest:
                validate_nearest_recoveries(header, record)
            elif "nearest_recoveries" in record:
                raise ValueError("Nearest recovery events lack their capture manifest")
            pre = record["pre"]
            if observed_joined:
                if not records and "contact_weather_manifest" not in header and (pre["session_state"] != 9 or pre["update_counter"] != 0):
                    raise ValueError("Joined AI capture lacks the first unreleased grid update")
                derived_release_counter = validate_joined_release(record, derived_release_counter, selected_initial=not records and "contact_weather_manifest" in header, local_session=observed_local)
            elif "release_events" in record or "update_caller_return_address" in record:
                raise ValueError("Release/caller observations lack joined provenance")
            if not records and pre["session_state"] not in ((4, 6) if qualifying else (9, 6)):
                raise ValueError("Original fleet recording did not begin on NoQual grid/race (state9/6)")
            validate_update_counters(record, observed_finish)
            if records and pre["update_counter"] != records[-1]["post"]["update_counter"]:
                raise ValueError("Skipped original fleet update")
            for field in ("delta_q16", "reciprocal_q16", "extra_update"):
                if any(record[name][field] != pre[field] for name in boundaries):
                    raise ValueError("Timing changed inside fleet update: " + field)
            records.append(record)
        if not records or footer is None or footer.get("updates") != len(records) or footer.get("prefix_crc32") != f"{checksum:08x}":
            raise ValueError("Missing fleet completion, count mismatch or incorrect CRC32")
    if "combined_rng_manifest" in header:
        validate_combined_scope(header, records)
    if observed_wheel:
        validate_wheel_queue_scope(header, records)
    if observed_whine:
        validate_whine_scope(header, records)
    if "cold_grid_sequence" in header:
        validate_cold_grid_sequence(header["cold_grid_sequence"], header["grid_initialization"], header["cold_grid_initialization"], records[0]["pre"])
    validate_detached_expiry_warmup(header, records)
    return header, records


def relocated_barrier(raw, records_pointer):
    """Relocate active source terrain pointers to indices, never native pointers."""
    output = bytearray(raw)
    if struct.unpack_from("<I", raw, 0x30)[0]:
        for pointer_offset, enabled in ((0x0c, True), (0x28, bool(struct.unpack_from("<I", raw, 0x2c)[0]))):
            if enabled:
                pointer = struct.unpack_from("<I", raw, pointer_offset)[0]
                difference = pointer - records_pointer
                if not records_pointer or difference < 0 or difference % 0x58 or difference // 0x58 > 0x7fff:
                    raise ValueError("Active barrier pointer cannot be relocated to a source record")
                struct.pack_into("<I", output, pointer_offset, difference // 0x58)
    return output


def local_launch_state(snapshot):
    """Decode captured inputs and outputs without synthesizing missing memory."""
    provenance = snapshot["shared"].get("provenance", {})
    local = snapshot["actor"]
    owner = struct.unpack_from("<b", bytes.fromhex(snapshot["shared"]["ownership_hex"]), local)[0]
    if not 0 <= owner < 4:
        raise ValueError("Local launch replay requires a captured local controller slot 0..3")
    launch = raw_hex(provenance["local_launch_tires_hex"], 16, "local launch state")
    world = raw_hex(provenance["world_status_hex"], 0x30, "world status")
    noqual = raw_hex(provenance["noqual_flags_hex"], 16, "NoQual state")
    dynamics, gameplay = bytes.fromhex(snapshot["dynamics_hex"]), bytes.fromhex(snapshot["gameplay_hex"])
    lifecycle = snapshot["shared"].get("lifecycle", {})
    epoch = struct.unpack_from("<I", bytes.fromhex(lifecycle["race_lifecycle_hex"]), 12)[0] if lifecycle else None
    return {"launch": struct.unpack_from("<i", launch, owner * 4)[0],
            "engine_speed": struct.unpack_from("<i", dynamics, 0x124)[0],
            "start_hold": int(struct.unpack_from("<I", dynamics, 0x148)[0] != 0),
            "owner": owner, "human": struct.unpack_from("<I", gameplay, 8)[0] != 0,
            "watch": struct.unpack_from("<h", world, 0x12)[0],
            "watch_demo": struct.unpack_from("<I", world, 0x18)[0],
            "noqual_approach": struct.unpack_from("<I", noqual)[0],
            "player_registered": gameplay[0x80] != 0xff,
            "session_state": snapshot["session_state"], "release_epoch": epoch}


def launch_metadata(records, index):
    """Typed event inputs; later launch/RPM/tire values remain expected outputs.

    448790→421f40→448130 precedes421aa0. State6 entry4483da calls402c80
    separately from401260.421aa0's owned-human branch then calls422f60.
    """
    record = records[index]
    pre, consumed = local_launch_state(record["pre"]), local_launch_state(record["consumed"])
    previous = local_launch_state(records[index - 1]["post"]) if index else None
    flags = 0
    if pre["release_epoch"] is not None and consumed["release_epoch"] is not None:
        flags |= LAUNCH_FLAGS["release_epoch_observed"]
        before = previous is not None and previous["release_epoch"] != pre["release_epoch"]
        inside = pre["release_epoch"] != consumed["release_epoch"]
        event_evidence = "captured448130 release epoch"
    else:
        # Historical captures lack4d3844. This observed event does not establish
        # autonomous timing and never imports the resulting scalar or tires.
        before = previous is not None and previous["launch"] != pre["launch"]
        inside = False
        event_evidence = "observed between-update launch scalar; release epoch unavailable"
    if before:
        flags |= LAUNCH_FLAGS["begin_before_pre"]
    if inside:
        flags |= LAUNCH_FLAGS["begin_inside_pre"]
    #421aa0 selects422f60 after any403dd0 controller handoff. Normal local
    #recordings exclude network input and state18's forced demonstration.
    if consumed["human"] and consumed["watch"] == 0 and consumed["session_state"] != 0x12:
        flags |= LAUNCH_FLAGS["decay"]
    if previous and previous["start_hold"] and not pre["start_hold"]:
        flags |= LAUNCH_FLAGS["clutch_release_before_pre"]
    if pre["start_hold"] and not consumed["start_hold"]:
        flags |= LAUNCH_FLAGS["clutch_release_inside_pre"]
    reset_before = previous and (not previous["human"] and pre["human"] or
                    previous["session_state"] == 9 and pre["session_state"] == 6 and
                    pre["noqual_approach"] == 0 and pre["watch_demo"] == 0)
    if reset_before and pre["player_registered"]:
        flags |= LAUNCH_FLAGS["reset_before_pre"]
    if not pre["human"] and consumed["human"] and consumed["player_registered"]:
        flags |= LAUNCH_FLAGS["reset_inside_pre"]
    return {"flags": flags, "consumed_launch": consumed["launch"], "pre_launch": pre["launch"],
            "pre_RPM": pre["engine_speed"], "pre_start_hold": pre["start_hold"],
            "release_epoch": pre["release_epoch"] or 0, "pre_session_state": pre["session_state"],
            "pre_controller_owner": pre["owner"], "event_evidence": event_evidence}


def body_selection_input(snapshot, local_players=1):
    """414e20/414ef0 inputs from421aa0 entry, before route/control decisions.

    414d20 initializes the four-body budget. No consumed mode, rank update,
    post-integration ground value or source RNG is an input to this decision.
    """
    if local_players != 1 or snapshot["source_mode"] != 0 or snapshot["session_state"] == 0x12:
        raise ValueError("Body selection replay requires ordinary one-player Single Race pre state")
    provenance = snapshot["shared"].get("provenance", {})
    base = raw_hex(provenance["ai_globals_hex"], 0x24, "pre AI globals")
    local = integer(snapshot["actor"], 0, ACTORS - 1, "selection local actor")
    local_game = bytes.fromhex(snapshot["actors"][local]["gameplay_hex"])
    context = {"actor_count": ACTORS, "local_players": local_players, "local_actor": local,
               "tire_base": struct.unpack_from("<i", base, 0x20)[0], "budget": 4,
               "alternate_direction": int(struct.unpack_from("<i", local_game, 0x10)[0] != 0)}
    actors = []
    for actor, entry in enumerate(snapshot["actors"]):
        body, dynamics, gameplay = (bytes.fromhex(entry[name + "_hex"]) for name in ("body", "dynamics", "gameplay"))
        ahead = integer(gameplay[0x96], 0, ACTORS - 1, "pre selection ahead actor")
        behind = integer(gameplay[0x97], 0, ACTORS - 1, "pre selection behind actor")
        actors.append({"mode": integer(struct.unpack_from("<I", body, 0x4f0)[0], 0, 4, "pre body mode"),
                       "human": int(struct.unpack_from("<I", gameplay, 8)[0] != 0),
                       "player_identity": integer(struct.unpack_from("<b", gameplay, 0x80)[0], -1, 3, "pre player identity"),
                       "rollover_bits": int(struct.unpack_from("<I", dynamics, 0x2a0)[0] != 0) |
                                        int(struct.unpack_from("<I", dynamics, 0x2a4)[0] != 0) << 1,
                       "support_count": integer(body[0x51f], 0, 8, "pre support count"),
                       "first_tire": struct.unpack_from("<i", dynamics, 0xdc)[0],
                       "normal_up": struct.unpack_from("<i", body, 0xb4)[0],
                       "packed_ring": ahead | behind << 8,
                       "forward_gap": struct.unpack_from("<i", gameplay, 0xf0)[0]})
    if len(actors) != ACTORS:
        raise ValueError("Body selection requires all sixteen pre actors")
    return context, actors


def validate_ai_route_scope(header, records, require_noqual=True, retained_drivers=False):
    """Require observed initialized tables and isolate403dd0 from later controls."""
    if "ai_initial_state" not in header or header.get("ai_boundaries") != AI_BOUNDARIES:
        raise ValueError("AI route comparison requires --ai-state original capture provenance")
    initial_raw = header["ai_initial_state"]
    initial = ai_content(initial_raw)
    for record in records:
        if require_noqual and struct.unpack_from("<I", bytes.fromhex(record["ai_pre"]["shared"]["provenance"]["noqual_flags_hex"]))[0] != 0:
            raise ValueError("AI route comparison requires the ordinary NoQual start-line branch")
        for name in (*BOUNDARIES, *AI_BOUNDARIES):
            observed = record[name]["shared"]["ai_state"]
            for field in ("route_descriptor_hex", "start_bound", "end_bound", "resolved_ai_crc32", "points_crc32",
                          "startline_primary_crc32", "startline_alternate_crc32"):
                if observed[field] != initial_raw[field]:
                    raise ValueError(f"Unsupported live AI setup change at update {record['id']} boundary {name}: {field}")
        for actor in range(ACTORS if not retained_drivers or record is records[0] else 0):
            pre = bytes.fromhex(record["ai_pre"]["actors"][actor]["gameplay_hex"])
            integer(pre[0x8d], 0, ACTORS - 1, "resolved AI driver index")
    return initial


def validate_session_boundary_scope(header, records):
    if header.get("checkpoint_timing_manifest") != CHECKPOINT_TIMING_MANIFEST or header.get("finish_timing_manifest") != FINISH_TIMING_MANIFEST:
        raise ValueError("Session boundary comparison requires original AI/checkpoint/finish provenance")
    if header["source_mode"] != 0 or struct.unpack_from("<I", bytes.fromhex(header["configuration_hex"]), 0xac)[0] != 1:
        raise ValueError("Session boundary comparison requires ordinary one-player Single Race")
    return validate_ai_route_scope(header, records, require_noqual=False)

SESSION_TYPED_FIELDS = ("classification_order", "split_enable", "lap_distance", "release_marker", "configuration_tail", "release_flags")
SESSION_BASE_BYTES = 848
SESSION_EXTENDED_BYTES = 104


def validate_traffic_control_scope(header, records, retained_drivers=False):
    if header.get("traffic_control_manifest") != TRAFFIC_CONTROL_MANIFEST or header.get("traffic_boundaries") != TRAFFIC_BOUNDARIES:
        raise ValueError("Traffic/control comparison requires recorded404c1a boundary and live setup provenance")
    initial = validate_ai_route_scope(header, records, require_noqual=False, retained_drivers=retained_drivers)
    if len(traffic_content(header["traffic_initial_state"])) != initial["point_count"]:
        raise ValueError("Traffic brake-bypass length differs from live POINTS")
    for record in records:
        if record["traffic_pre"]["session_state"] != record["consumed"]["session_state"]:
            raise ValueError("Traffic/control interval changed session state before415090")
    return initial

TRAFFIC_RECORD_BYTES = 40604


def validate_nearest_recovery_scope(header, records):
    if header.get("nearest_recovery_manifest") != NEAREST_RECOVERY_MANIFEST:
        raise ValueError("Nearest recovery comparison requires captured4046a3..404731 provenance")
    initial = validate_ai_route_scope(header, records)
    nearest_map_content(header["nearest_recovery_initial_state"], initial["point_count"])
    for record in records:
        validate_nearest_recoveries(header, record)
    return initial


def validate_joined_ai_scope(header, records, retained_drivers=False):
    if header.get("joined_ai_manifest") != JOINED_AI_MANIFEST:
        raise ValueError("Joined AI comparison requires original initialized-rank and release-call provenance")
    initial = validate_traffic_control_scope(header, records, retained_drivers=retained_drivers)
    joined_content(header["joined_initial_state"], initial["point_count"])
    validate_start_call(header["grid_initialization"], 0x460170)
    bounded = "contact_weather_manifest" in header
    if not records or not bounded and (records[0]["pre"]["session_state"] != 9 or records[0]["pre"]["update_counter"] != 0):
        raise ValueError("Joined AI comparison requires the first cold NoQual grid update")
    counter = integer(header["combined_initial_release_counter"], 0, 360, "selected initial release counter") if bounded else 0
    for index, record in enumerate(records):
        if (retained_local_session_enabled(header) and not retained_warm_restart_enabled(header)
                and record["pre"]["session_state"] == 9 and not cold_local_session_enabled(header)):
            raise ValueError("Retained local state9 requires actual warm restart provenance")
        counter = validate_joined_release(record, counter, selected_initial=bounded and index == 0, local_session=retained_local_session_enabled(header))
    return initial


def retained_race_timing_enabled(header):
    fields = {"checkpoint_timing_manifest", "finish_timing_manifest"}
    present = fields.intersection(header)
    if not present:
        return False
    if present != fields or header["checkpoint_timing_manifest"] != CHECKPOINT_TIMING_MANIFEST or header["finish_timing_manifest"] != FINISH_TIMING_MANIFEST:
        raise ValueError("Retained race timing requires complete checkpoint and finish provenance")
    return True


def validate_retained_race_timing_scope(header, records):
    if not retained_race_timing_enabled(header) or not records:
        raise ValueError("Retained race timing requires observed initial timing state")
    validate_joined_ai_scope(header, records, retained_drivers=True)
    if (header.get("local_handoff_manifest") or header.get("session_boundary_manifest")) and not retained_local_session_enabled(header):
        raise ValueError("Retained race timing excludes local controller and classification transactions")
    initial = records[0]["ai_pre"]
    timing, finish = initial["shared"]["checkpoint_timing"], initial["shared"]["finish_timing"]
    configuration = bytes.fromhex(header["configuration_hex"])
    cold = cold_local_session_enabled(header)
    initial_state = 9 if cold else 6
    #CONFIG+30 is LOAD BEST LAP (4660c7/46612a); loader44f0a3 only
    #activates its ghost at mode2. It is inert in this required mode0 race.
    integer(struct.unpack_from("<I", configuration, 0x30)[0], 0, 1, "Time Trial best-lap option")
    if (header["source_mode"] != 0 or initial["session_state"] != initial_state or timing["session_stage"] not in (2, 4)
            or struct.unpack_from("<I", configuration, 0xac)[0] != 1 or struct.unpack_from("<I", configuration, 0x20)[0] != 1):
        raise ValueError("Retained timing requires an ordinary active one-player NoQual race")
    raw = lambda name: bytes.fromhex(finish[name + "_hex"])
    if (raw("finish_counts")[4] != 0 or any(raw("local_finish_flags"))
            or struct.unpack("<I", raw("pending_state"))[0] & 0xffff != initial_state or struct.unpack_from("<H", raw("race_status"), 2)[0] != 0
            or cold and (raw("finish_counts")[0] != 0 or initial["update_counter"] != 1)):
        raise ValueError("Retained timing excludes local finish, retirement and result state")
    if any(raw("display_active")):
        raise ValueError("Retained timing requires inactive split displays without external lifetime mutations")
    if retained_local_session_enabled(header) and any(raw_hex(records[0]["pre"]["shared"]["provenance"]["local_launch_tires_hex"], 16, "initial local launch slots")[4:]):
        raise ValueError("Retained local session excludes nonzero unused player launch slots")
    return initial


def compare_nearest_recovery(header, records, native_path):
    """Compare4019f0 map outputs and exact4046a3..404731 transactions."""
    import csv
    from pathlib import Path
    initial = validate_nearest_recovery_scope(header, records)
    source_map = nearest_map_content(header["nearest_recovery_initial_state"], initial["point_count"])
    mismatches, failed = [], set()
    def rows(suffix, fields):
        with Path(str(native_path) + suffix).open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != fields:
                raise ValueError("Unexpected nearest recovery sidecar header: " + suffix)
            return list(reader)
    map_rows = rows(".nearest-map.csv", ["index", "candidate"])
    if len(map_rows) != initial["point_count"]:
        raise ValueError("Missing or extra initialized nearest-map output rows")
    for index, row in enumerate(map_rows):
        if int(row["index"]) != index:
            raise ValueError("Out-of-order initialized nearest-map output")
        if int(row["candidate"]) != source_map[index]:
            failed.update(record["id"] for record in records)
            mismatches.append({"field": "nearest_map", "index": index, "original": source_map[index], "native": int(row["candidate"])})
    identities = [(record, index, event) for record in records for index, event in enumerate(record["nearest_recoveries"])]
    observed = rows(".nearest-recovery.csv", ["id", "event", "actor", "route_next", "route_previous", "rng_final"])
    if len(observed) != len(identities):
        raise ValueError("Missing or extra actual nearest-recovery output rows")
    jumps, retained = [], 0
    for (record, index, event), row in zip(identities, observed):
        if (int(row["id"]), int(row["event"]), int(row["actor"])) != (record["id"], index, event["actor"]):
            raise ValueError("Out-of-order nearest-recovery update/event/actor")
        before, after = (nearest_recovery_values(event[phase]) for phase in ("pre", "post"))
        for name, expected in (("route_next", after["route_next"]), ("route_previous", after["route_previous"]), ("rng_final", after["rng"])):
            actual = int(row[name])
            if actual != expected:
                failed.add(record["id"])
                mismatches.append({"update": record["id"], "event": index, "actor": event["actor"],
                                   "field": name, "original": expected, "native": actual})
        old, new = [before[name] for name in ("route_next", "route_previous")], [after[name] for name in ("route_next", "route_previous")]
        if old != new:
            jumps.append({"update": record["id"], "event": index, "actor": event["actor"], "before": old, "after": new})
        else:
            retained += 1
    exact = not mismatches
    return {"parity": exact and bool(jumps), "numerical_parity": exact, "coverage_sufficient": bool(jumps),
            "complete_source_interval": True, "requested_updates": len(records), "compared_updates": len(records),
            "exact_updates": len(records) - len(failed), "exact_prefix_updates": min(failed, default=len(records)),
            "compared_values": initial["point_count"] + len(identities) * 3, "map_rows": len(map_rows), "event_count": len(identities),
            "mismatch_count": len(mismatches), "first_mismatches": mismatches[:100], "cursor_jumps": jumps,
            "unchanged_checks": retained, "original_positive_jump_parity": exact if jumps else None,
            "input_boundary": "4046a3-entry", "comparison_boundary": "404731-exit",
            "input_imports": "observed transaction PRE cursor and body XZ only; source map is compared against production initialization, not imported into decisions",
            "scope": "independent human nearest-route checks after ordinary gate progression; only exact-block cursor writes establish a recovery jump",
            "rng_scope": "exact retained RNG before/after each transaction; no RNG draws in this source block",
            "physical_parity": None, "autonomous_route_parity": None, "autonomous_race_parity": None}


def write_binary(path, header, records, compare_ai_route=False, compare_ai_timing=False, compare_ai_finish=False, compare_local_handoffs=False, compare_session_boundaries=False, compare_traffic_controls=False, compare_nearest_recovery=False, compare_joined_ai=False, compare_combined_rng=False, compare_driver_retention=False, driver_physics_inputs=False, drivetrain_feedback=False, rpm_feedback=False, limiter_feedback=False, pedal_feedback=False, control_feedback=False, physical_feedback=False, dispatch_feedback=False, startup_join=False, startup_first_selection=False, startup_physical_body=False, startup_control_calibration=False, startup_contact_cache=False, startup_world_reset=False, scene_owned=False):
    if scene_owned and (not startup_world_reset or not (scene_autonomous_enabled(header) or scene_lifecycle_enabled(header))):
        raise ValueError("Scene ownership requires the joined autonomous cold profile")
    """TOCAFLET13: optional source PRE inputs, initialized ranks and release calls."""
    from pathlib import Path
    if compare_combined_rng and not compare_joined_ai:
        raise ValueError("Combined fleet inputs require joined AI mode")
    if compare_driver_retention and not compare_combined_rng:
        raise ValueError("Driver retention requires combined RNG mode")
    retained_session = compare_driver_retention and retained_local_session_enabled(header)
    retained_timing = compare_driver_retention and retained_race_timing_enabled(header)
    if retained_timing:
        validate_retained_race_timing_scope(header, records)
    if driver_physics_inputs and any((compare_ai_route, compare_ai_timing, compare_ai_finish, compare_local_handoffs,
                                      compare_session_boundaries, compare_traffic_controls, compare_nearest_recovery,
                                      compare_joined_ai, compare_combined_rng, compare_driver_retention)):
        raise ValueError("Driver physical inputs contain only independent physical PRE transactions")
    if dispatch_feedback and not physical_feedback:
        raise ValueError("Dispatch feedback requires physical feedback")
    if physical_feedback and not control_feedback:
        raise ValueError("Physical feedback requires control feedback")
    if control_feedback and not pedal_feedback:
        raise ValueError("Control feedback requires pedal feedback")
    if pedal_feedback and not limiter_feedback:
        raise ValueError("Pedal feedback requires limiter feedback")
    if limiter_feedback and not rpm_feedback:
        raise ValueError("Limiter feedback requires RPM feedback")
    if rpm_feedback and not drivetrain_feedback:
        raise ValueError("RPM feedback requires drivetrain feedback")
    if drivetrain_feedback and not (compare_driver_retention or driver_physics_inputs):
        raise ValueError("Drivetrain feedback requires retained driver or physical inputs")
    startup_inputs = None
    if startup_world_reset and not startup_contact_cache:
        raise ValueError("Startup world reset requires the startup contact cache handoff")
    if startup_world_reset and any((compare_ai_timing, compare_ai_finish, compare_session_boundaries, compare_local_handoffs, compare_nearest_recovery)) and not cold_local_session_enabled(header):
        raise ValueError("Startup lifecycle uses the ordinary joined profile without extended timing/finish/session imports")
    if startup_contact_cache and not startup_control_calibration:
        raise ValueError("Startup contact cache requires the startup control calibration handoff")
    if startup_control_calibration and not startup_physical_body:
        raise ValueError("Startup control calibration requires the startup physical BODY handoff")
    if startup_physical_body and not (dispatch_feedback and (startup_join or driver_physics_inputs)):
        raise ValueError("Startup physical BODY requires startup or physical sidecar with continuous dispatch feedback")
    if startup_physical_body and driver_physics_inputs:
        validate_startup_first_selection_scope(header, records)
    if startup_first_selection and not startup_join:
        raise ValueError("First startup selection requires the startup join")
    if startup_join:
        if not compare_driver_retention or not startup_physical_body and any((driver_physics_inputs, drivetrain_feedback, rpm_feedback, limiter_feedback, pedal_feedback, control_feedback, physical_feedback, dispatch_feedback)):
            raise ValueError("Startup join requires retained-driver/shared-RNG mode without physical feedback")
        startup_inputs = (validate_startup_first_selection_scope if startup_first_selection else validate_startup_join_scope)(header, records)
    initial_owners = [struct.unpack_from("<b", bytes.fromhex(actor["gameplay_hex"]), 0x80)[0]
                      for actor in records[0]["ai_pre"]["actors"]] if compare_driver_retention else []
    publication = (compare_driver_retention or driver_physics_inputs) and "brake_publication_manifest" in header
    def finish_driver_input(name, raw, actor, keep_modes=False):
        #Feedback's initial PRE copy runs before this scrub. Otherwise it would
        #put observed startup writers back into a supposedly generated stream.
        if startup_physical_body and record_index == 0:
            if startup_join:
                raw = startup_join_actor_input(name, raw, actor, header["local_actor"])
                if name == "dynamics":
                    raw = bytearray(raw); raw[0x2a0:0x2a8] = bytes(8)
            if name == "body": raw = startup_physical_body_input(raw)
            if startup_control_calibration and name == "dynamics": raw = startup_control_calibration_input(raw)
            if startup_contact_cache: raw = startup_contact_cache_input(name, raw)
        if (compare_driver_retention or driver_physics_inputs) and retained_local_session_enabled(header) and name == "dynamics" and not keep_modes:
            raw = bytearray(raw)
            for offset in (0x27c, 0x310, 0x314, 0x318): raw[offset:offset + 4] = bytes(4)
        if startup_world_reset and name == "body":
            raw = bytearray(raw)
            raw[0x4ec:0x4ee] = bytes(2)
            raw[0x514] = 0
            raw[0x529:0x539] = bytes(16) #Generated contact count; unread partner slots never enter the stream.
        if startup_world_reset and record_index == 0:
            raw = startup_cold_actor_input(name, raw)
        return raw

    def driver_input(name, raw, actor, pedal_initial=False):
        if startup_join and record_index == 0:
            raw = startup_join_actor_input(name, raw, actor, header["local_actor"])
            if startup_first_selection:
                raw = bytearray(raw)
                if name == "body": raw[0x4f0:0x4f4] = bytes(4)
                if name == "dynamics": raw[0x2a0:0x2a8] = bytes(8)
        if dispatch_feedback:
            if record_index == 0 and pedal_initial:
                raw = bytes.fromhex(records[0]["pre"]["actors"][actor][name + "_hex"])
            if name == "gameplay" and not (record_index == 0 and pedal_initial):
                raw = bytes(len(raw))
            if name == "body":
                raw = bytearray(raw)
                raw[0x4f0:0x4f4] = (bytes.fromhex(records[0]["pre"]["actors"][actor]["body_hex"])[0x4f0:0x4f4]
                                         if driver_physics_inputs and record_index == 0 else bytes(4))
        if physical_feedback and name in PHYSICAL_RANGES:
            raw = bytearray(raw)
            initial = bytes.fromhex(records[0]["pre"]["actors"][actor][name + "_hex"])
            for offset, size in PHYSICAL_RANGES[name]:
                raw[offset:offset + size] = initial[offset:offset + size] if record_index == 0 and (driver_physics_inputs or pedal_initial) else bytes(size)
        if control_feedback and name == "dynamics":
            raw = bytearray(raw)
            initial = bytes.fromhex(records[0]["pre"]["actors"][actor]["dynamics_hex"])
            for offset, size in CONTROL_SIZES:
                raw[offset:offset + size] = initial[offset:offset + size] if pedal_initial and record_index == 0 else bytes(size)
        if pedal_feedback and name == "dynamics":
            raw = bytearray(raw)
            initial = bytes.fromhex(records[0]["pre"]["actors"][actor]["dynamics_hex"])
            for offset in PEDAL_OFFSETS:
                raw[offset:offset + 4] = initial[offset:offset + 4] if pedal_initial and record_index == 0 else bytes(4)
        # All copies in TOCAFLET are sanitized, including physical/selection
        # copies that the current combined path does not otherwise consume.
        if driver_physics_inputs:
            raw = bytearray(raw)
            if name == "gameplay": raw[:] = bytes(len(raw))
            elif name == "body":
                for offset in ((0x4e8, 0x500, 0x508) if publication else (0x500, 0x508)):
                    raw[offset:offset + 4] = bytes(4)
            elif name == "dynamics":
                raw[0xdc:0xfc] = bytes(32)
                raw[0x138:0x148] = bytes(16)
                raw[0x330] = 0  #331..333 are observed physical rollover counters.
            if drivetrain_feedback and name == "dynamics":
                for offset, size in ((0x330, 1), (0x334, 1), (0x30c, 4), (0x304, 4)):
                    raw[offset:offset + size] = bytes(size)
            if rpm_feedback and name == "dynamics": raw[0x124:0x12c] = bytes(8)
            return finish_driver_input(name, raw, actor)
        if not compare_driver_retention: return finish_driver_input(name, raw, actor)
        raw = bytearray(raw)
        if publication:
            if name == "body": raw[0x4e8:0x4ec] = bytes(4)
            elif name == "dynamics":
                raw[0x144:0x148] = bytes(4) if record_index else bytes.fromhex(records[0]["pre"]["actors"][actor]["dynamics_hex"])[0x144:0x148]
        if drivetrain_feedback and name == "dynamics":
            initial = bytes.fromhex(records[0]["pre"]["actors"][actor]["dynamics_hex"])
            for offset, size in ((0x330, 1), (0x334, 1), (0x30c, 4), (0x304, 4)):
                raw[offset:offset + size] = bytes(size) if record_index else initial[offset:offset + size]
        if rpm_feedback and name == "dynamics":
            initial = bytes.fromhex(records[0]["pre"]["actors"][actor]["dynamics_hex"])
            raw[0x124:0x12c] = bytes(8) if record_index else initial[0x124:0x12c]
        if dispatch_feedback and not (record_index == 0 and pedal_initial):
            if name == "dynamics":
                raw[0xdc:0xfc] = bytes(32)
                raw[0x138:0x148] = bytes(16)
            elif name == "body":
                for offset in (0x500, 0x508): raw[offset:offset + 4] = bytes(4)
        if record_index == 0: return finish_driver_input(name, raw, actor, keep_modes=pedal_initial)
        if name == "gameplay": raw[:] = bytes(len(raw))
        elif name == "body":
            for offset in (0x500, 0x508): raw[offset:offset + 4] = bytes(4)
        elif name == "dynamics":
            raw[0xdc:0xfc] = bytes(32)
            if initial_owners[actor] == -1: raw[0x138:0x148] = bytes(16)
        return finish_driver_input(name, raw, actor)
    config = header["initial_configuration"]
    configuration = raw_hex(header["configuration_hex"], 0x620, "source configuration")
    if struct.unpack_from("<I", configuration, 0x5c)[0] != 0:
        raise ValueError("Fleet replay does not yet support source low-gravity configuration+5c")
    for record in records:
        if "ai_globals" not in record["consumed"]["shared"].get("provenance", {}):
            raise ValueError("Fleet replay requires captured AI globals for source tire-offset validation")
    launch_records = [dict.fromkeys(("flags", "consumed_launch", "pre_launch", "pre_RPM", "pre_start_hold", "release_epoch", "pre_session_state", "pre_controller_owner"), 0) for _ in records] if compare_joined_ai or driver_physics_inputs else [launch_metadata(records, index) for index in range(len(records))]
    if driver_physics_inputs and "human_lamps_manifest" not in header:
        launch_records = [{**launch, "consumed_launch": local_launch_state(record["consumed"])["launch"]}
                          for launch, record in zip(launch_records, records)]
    selection_records = []
    for index, record in enumerate(records):
        snapshot = record["ai_pre" if compare_joined_ai else "pre"]
        if compare_driver_retention and index:
            snapshot = {**snapshot, "actors": [{**actor, "gameplay_hex": initial["gameplay_hex"]}
                        for actor, initial in zip(snapshot["actors"], records[0]["ai_pre"]["actors"])]}
        if dispatch_feedback:
            #Only configuration/lifecycle context enters here. Every actor
            #selector input comes from retained Motion and previous gameplay at
            #the actual pre-AI boundary, never a later source snapshot.
            pre = record["pre"]
            if struct.unpack_from("<I", configuration, 0xac)[0] != 1 or pre["source_mode"] != 0 or pre["session_state"] == 0x12:
                raise ValueError("Dispatch feedback requires ordinary one-player Single Race")
            base = raw_hex(pre["shared"]["provenance"]["ai_globals_hex"], 0x24, "pre AI globals")
            selection_records.append(({"actor_count": ACTORS, "local_players": 1, "local_actor": header["local_actor"],
                                       "tire_base": struct.unpack_from("<i", base, 0x20)[0], "budget": 4, "alternate_direction": 0},
                                      [dict.fromkeys(("mode", "human", "player_identity", "rollover_bits", "support_count", "first_tire",
                                                      "normal_up", "packed_ring", "forward_gap"), 0) for _ in range(ACTORS)]))
        else:
            selection_records.append(body_selection_input(snapshot, struct.unpack_from("<I", configuration, 0xac)[0]))
    ai_initial = validate_joined_ai_scope(header, records, retained_drivers=compare_driver_retention) if compare_joined_ai else validate_nearest_recovery_scope(header, records) if compare_nearest_recovery else validate_traffic_control_scope(header, records) if compare_traffic_controls else validate_session_boundary_scope(header, records) if compare_session_boundaries else validate_ai_route_scope(header, records) if compare_ai_route else None
    if startup_join:
        ai_initial = dict(ai_initial)
        resolved = bytearray(ai_initial["resolved_ai"])
        name_start = header["local_actor"] * 40
        resolved[name_start:name_start + len(startup_inputs["player_name"]) + 1] = bytes(len(startup_inputs["player_name"]) + 1)
        ai_initial["resolved_ai"] = bytes(resolved)
    if compare_ai_timing and (not compare_ai_route or header.get("checkpoint_timing_manifest") != CHECKPOINT_TIMING_MANIFEST):
        raise ValueError("AI timing comparison requires --compare-ai-route and --checkpoint-timing original provenance")
    if compare_ai_finish and (not compare_ai_timing or header.get("finish_timing_manifest") != FINISH_TIMING_MANIFEST):
        raise ValueError("AI finish comparison requires --compare-ai-timing and --finish-timing original provenance")
    if compare_local_handoffs and header.get("local_handoff_manifest") != LOCAL_HANDOFF_MANIFEST:
        raise ValueError("Local handoff comparison requires original call-entry/return provenance")
    emit_handoffs = compare_local_handoffs or retained_session
    emit_session = compare_session_boundaries or retained_session
    emit_timing = compare_ai_timing or emit_session or retained_timing
    emit_finish = compare_ai_finish or compare_session_boundaries or retained_timing
    emit_traffic = compare_traffic_controls or compare_joined_ai
    with Path(path).open("wb") as output:
        output.write(struct.pack("<8s6I", b"TOCAFLET", 13, ACTORS, len(records), header["local_actor"], config["weather"], config["steering_profile"]))
        output.write(struct.pack("<8I", bool(ai_initial), emit_timing, emit_finish, emit_handoffs, emit_session, emit_traffic, compare_nearest_recovery, compare_joined_ai))
        if ai_initial is not None:
            raw = header["ai_initial_state"]
            output.write(struct.pack("<3I", ai_initial["point_count"], raw["start_bound"] & 0xff, raw["end_bound"] & 0xff))
            output.write(ai_initial["resolved_ai"])
            output.write(ai_initial["points"])
            output.write(ai_initial["startline_primary"])
            output.write(ai_initial["startline_alternate"])
        if emit_traffic:
            bypass = traffic_content(header["traffic_initial_state"])
            output.write(struct.pack("<I", len(bypass)))
            output.write(bypass)
        if compare_nearest_recovery:
            output.write(nearest_map_content(header["nearest_recovery_initial_state"], ai_initial["point_count"]))
        if compare_joined_ai:
            output.write(struct.pack("<i", header["joined_initial_state"]["rank_threshold"]))
            output.write(joined_content(header["joined_initial_state"], ai_initial["point_count"]))
        for record_index, (record, launch, (selection, selection_actors)) in enumerate(zip(records, launch_records, selection_records)):
            if (startup_join or startup_physical_body) and record_index == 0:
                selection = {**selection, "budget": 0}
                selection_actors = [{**actor, **dict.fromkeys(("human", "player_identity", "support_count", "first_tire", "normal_up", "packed_ring", "forward_gap"), 0)}
                                    for actor in selection_actors]
                if startup_first_selection or startup_physical_body:
                    selection = {**selection, "tire_base": 0, "alternate_direction": 0}
                    selection_actors = [{**actor, "mode": 0, "rollover_bits": 0} for actor in selection_actors]
            if compare_driver_retention and record_index:
                selection = {**selection, "alternate_direction": 0}
                selection_actors = [{**actor, "human": 0, "player_identity": 0, "first_tire": 0,
                                     "packed_ring": 0, "forward_gap": 0} for actor in selection_actors]
            state = record["ai_pre" if compare_joined_ai else "consumed"]
            shared = state["shared"]
            retain_world = physical_feedback and (startup_world_reset or record_index != 0 or not driver_physics_inputs)
            world_bytes = lambda value: bytes(len(value)) if retain_world else value
            time_input = (0, 0, 0) if startup_world_reset else tuple(state[name] for name in STARTUP_CLOCK_FIELDS)
            output.write(struct.pack("<IiiIIIIiIII", record["id"], *time_input,
                                     0 if compare_joined_ai or driver_physics_inputs else state["rng"], 0 if retain_world else state["roughness_cursor"], 0 if limiter_feedback else state["limiter_active"], 0 if limiter_feedback else state["limiter_load_q16"],
                                     0 if retain_world else shared["damage_cycle"], 0 if retain_world else shared["detached_count"], 0 if scene_owned else shared["carcontacts_enabled"]))
            source_shared = record["ai_pre"]["shared"] if compare_joined_ai else shared
            pair_input = world_bytes(source_shared["pair_matrix"])
            if startup_world_reset and record_index == 0: pair_input = startup_world_reset_input(pair_input)
            output.write(pair_input)
            output.write(bytes(ACTORS) if startup_world_reset and record_index == 0 else shared["ownership"])
            output.write(world_bytes(shared["damage_scratch"][:15]))
            for name in ("damage_levels", "broken_features", "detached_pool", "detached_free", "sector_visibility"):
                output.write(bytes(len(shared[name])) if scene_owned and name == "sector_visibility" else shared[name] if name == "sector_visibility" else world_bytes(shared[name]))
            output.write(bytes(8) if scene_owned else struct.pack("<2I", shared["sector_visibility_disabled"], shared["sector_visibility_override"]))
            # 402ac0's return ramp uses the real difficulty/weather base, not a
            # value inferred from a later observed tire-offset output.
            output.write(bytes(4) if (startup_first_selection or startup_physical_body) and record_index == 0 else source_shared["provenance"]["ai_globals"][0x20:0x24])
            output.write(struct.pack("<IiiiIIIi", *(launch[name] for name in
                         ("flags", "consumed_launch", "pre_launch", "pre_RPM", "pre_start_hold",
                          "release_epoch", "pre_session_state", "pre_controller_owner"))))
            output.write(struct.pack("<IIIiII", *(selection[name] for name in
                         ("actor_count", "local_players", "local_actor", "tire_base", "budget", "alternate_direction"))))
            for actor in selection_actors:
                output.write(struct.pack("<IIiIIiiIi", *(actor[name] for name in
                             ("mode", "human", "player_identity", "rollover_bits", "support_count", "first_tire",
                              "normal_up", "packed_ring", "forward_gap"))))
            if ai_initial is not None:
                pre = record["ai_pre"]
                output.write(struct.pack("<2I", 0 if compare_joined_ai else pre["shared"]["ai_state"]["release_counter"], 0 if compare_joined_ai else pre["rng"]))
                for actor_index, actor in enumerate(pre["actors"]):
                    body = driver_input("body", bytes.fromhex(actor["body_hex"]), actor_index)
                    output.write(struct.pack("<Ii", struct.unpack_from("<I", body, 0x4f0)[0], struct.unpack_from("<i", body, 0xe0)[0]))
                    output.write(body[0xbc:0xc8])
                    output.write(body[0x150:0x174])
                    output.write(driver_input("gameplay", bytes.fromhex(actor["gameplay_hex"]), actor_index))
            if emit_timing:
                pre = record["ai_pre"]
                timing = pre["shared"]["checkpoint_timing"]
                output.write(struct.pack("<IiiIII", 0 if startup_world_reset else record["pre"]["race_clock"] if retained_session and retained_warm_restart_enabled(header) else 0 if retained_timing else pre["race_clock"], timing["session_stage"], pre["session_state"],
                                         struct.unpack_from("<H", pre["shared"]["provenance"]["world_status"], 0x14)[0],
                                         struct.unpack("<I", timing["race_flags"])[0],
                                         struct.unpack_from("<I", pre["shared"]["provenance"]["noqual_flags"], 8)[0]) if not retained_timing or not record_index
                             else struct.pack("<I", record["pre"]["race_clock"]) + bytes(20) if not startup_world_reset and retained_session and retained_warm_restart_enabled(header) else bytes(24))
                output.write(timing["split_table"] if not retained_timing or not record_index else bytes(576))
            if emit_finish:
                finish = record["ai_pre"]["shared"]["finish_timing"]
                timer2, timer3, _, release = struct.unpack("<4I", finish["race_counters"])
                counts = struct.unpack("<2I", finish["finish_counts"])
                if startup_world_reset and record_index == 0:
                    timer2 = timer3 = release = 0
                if retained_timing and record_index:
                    finish = {name: bytes(size) for name, (_, size) in FINISH_TIMING_BLOCKS.items()}
                    timer2 = timer3 = release = 0
                    counts = (0, 0)
                #Retained timing requires mode0 above; its inactive mode2
                #ghost preference must not become a replay input or later import.
                record_playback = 0 if retained_timing else struct.unpack_from("<I", configuration, 0x30)[0]
                output.write(struct.pack("<9I", header["source_mode"], record_playback, release,
                                         *counts, timer2, timer3, struct.unpack("<I", finish["pending_state"])[0], struct.unpack_from("<H", finish["race_status"], 2)[0]))
                output.write(finish["local_finish_flags"])
                output.write(finish["finish_order"])
                for player in range(4):
                    output.write(struct.pack("<3i4h", *(struct.unpack_from("<i", finish[name], player * 4)[0]
                                                       for name in ("display_mode", "display_leader", "display_active")),
                                             *(struct.unpack_from("<h", finish[name], player * 2)[0]
                                               for name in ("display_ticks", "display_minutes", "display_seconds", "display_hundredths"))))
                    output.write(struct.pack("<6i", *(struct.unpack_from("<i", finish[name + "_" + str(sector)], player * 4)[0]
                                                      for name in ("reference", "current") for sector in range(3))))
                    for name in ("display", "leader_display"):
                        output.write(finish[name][player * 16:(player + 1) * 16])
                output.write(struct.pack("<3i", *(struct.unpack("<i", finish["persisted_" + str(sector)])[0] for sector in range(3))))
            if emit_handoffs:
                validate_local_handoffs(record, header["local_actor"])
                output.write(struct.pack("<I", len(record["local_handoffs"])))
                for event in record["local_handoffs"]:
                    pre = event["pre"]
                    values = local_handoff_values(pre)
                    output.write(struct.pack("<4Ii", event["kind"], event["actor"], event["return_address"], 0 if retained_session else pre["rng"], 0 if retained_session else pre["ai_tire_base"]))
                    output.write(bytes(88) if retained_session else struct.pack("<IIIiII16i", *(values[name] for name in LOCAL_HANDOFF_FIELDS)))
            if emit_session:
                pre = record["ai_pre"]
                extended = pre["shared"].get("session_boundary")
                output.write(struct.pack("<I", extended is not None))
                output.write(bytes(0x338) if retained_session else bytes.fromhex(pre["dynamics_hex"]))
                output.write(bytes(4) if retained_session else pre["shared"]["provenance"]["ai_globals"][0x20:0x24])
                output.write(bytes(16) if retained_session else pre["shared"]["provenance"]["local_launch_tires"])
                if extended is not None:
                    for name in SESSION_TYPED_FIELDS:
                        output.write(bytes(len(extended[name])) if retained_session and record_index else extended[name])
            if emit_traffic:
                pre = record["ai_pre" if compare_joined_ai else "traffic_pre"]
                traffic = pre["shared"]["traffic_control"]
                output.write(struct.pack("<IIiII", 0 if compare_combined_rng else pre["rng"], pre["source_mode"], 0 if startup_world_reset or retained_timing and record_index else pre["session_state"],
                                         0 if retained_timing and record_index else struct.unpack_from("<H", pre["shared"]["provenance"]["world_status"], 0x12)[0],
                                         0 if compare_joined_ai else pre["shared"]["ai_state"]["release_counter"]))
                for name in ("weather_penalty", "tuning", "status", "headlight_gate_0", "headlight_gate_1", "headlight_gate_2", "force_acceleration"):
                    if startup_join and record_index == 0 and name == "status":
                        output.write(bytes(16) + traffic[name][16:])
                    else:
                        output.write(bytes(len(traffic[name])) if compare_driver_retention and record_index and name == "status" else traffic[name])
                output.write(bytes(1212) if compare_driver_retention and record_index else traffic["braking_memory"])
                for actor_index, actor in enumerate(pre["actors"]):
                    for name in ("body", "dynamics", "gameplay"):
                        output.write(driver_input(name, actor[name], actor_index, pedal_initial=True))
            if compare_nearest_recovery:
                output.write(struct.pack("<I", len(record["nearest_recoveries"])))
                for event in record["nearest_recoveries"]:
                    pre = nearest_recovery_values(event["pre"])
                    output.write(struct.pack("<8I", event["actor"], pre["rng"], pre["body_x"] & 0xffffffff, pre["body_z"] & 0xffffffff,
                                             pre["route_next"], pre["route_previous"], pre["refresh"], pre["human"]))
            if compare_joined_ai:
                output.write(struct.pack("<2I", 0 if startup_world_reset else record["update_caller_return_address"], 0 if startup_world_reset else len(record["release_events"])))
                for event in ([] if startup_world_reset else record["release_events"]):
                    pre = event["pre"]
                    output.write(struct.pack("<4I", event["return_address"], pre["source_mode"], pre["session_stage"], pre["local_players"]))
            pools = shared["props"]
            if "model_records" not in pools["signs"]:
                raise ValueError("Fleet physical replay requires captured live sign model transforms")
            output.write(struct.pack("<3I", *(pools[name]["count"] for name in ("cones", "posts", "signs"))))
            for name in ("cones", "posts", "signs"):
                output.write(world_bytes(pools[name]["records"]))
            output.write(world_bytes(pools["signs"]["model_records"]))
            for actor, entry in enumerate(state["actors"]):
                root = entry["root"]
                output.write(bytes(12) if scene_owned else struct.pack("<4H4B", root["sector"], *root["neighbors"], entry["effect_root"]["flags"][0], root["detail"], root["sector_visible"], 0))
                for name in ("limiter_active", "limiter_load"):
                    if limiter_feedback:
                        # Sole initialization is first421aa0 PRE. All later
                        # copies, including local metadata and physical PRE, are zero.
                        initial = records[0]["pre"]["shared"][name]
                        output.write(initial[actor * 4:(actor + 1) * 4] if record_index == 0 and compare_driver_retention and not startup_world_reset else bytes(4))
                    else:
                        output.write(shared[name][actor * 4:(actor + 1) * 4])
                output.write(bytes(0x34) if retain_world else relocated_barrier(shared["barriers"][actor * 0x34:(actor + 1) * 0x34], shared["barrier_records_pointer"]))
                for name in ("body", "dynamics", "gameplay"):
                    output.write(driver_input(name, entry[name], actor))


def cold_prop_baseline(state):
    """45c4c0's observable reset flags; raw pools remain in the recording."""
    problems = []
    for name in ("cones", "posts", "signs"):
        pool = state["shared"]["props"][name]
        for index in range(pool["count"]):
            raw = pool["records"][index * pool["stride"]:(index + 1) * pool["stride"]]
            if name == "cones":
                reset = raw[0xc0] == 4 and struct.unpack_from("<3i", raw, 0x84) == (0, 0, 0)
            elif name == "posts":
                reset = raw[0x130] == 0 and struct.unpack_from("<i", raw, 0x110)[0] == 0 and struct.unpack_from("<i", raw, 0x134)[0] == 0
            else:
                reset = struct.unpack_from("<i", raw, 0x3c)[0] == 0
            if not reset:
                problems.append({"pool": name, "index": index})
    return problems


def expected_actor(entry, state):
    """Only named implemented outputs; raw captured blocks are broader evidence."""
    body, dynamics = entry["body"], entry["dynamics"]
    def word(raw, offset, form="i"):
        return struct.unpack_from("<" + form, raw, offset)[0]
    fields = {}
    for axis, offset in zip("xyz", (0xbc, 0xc0, 0xc4)):
        fields["position_" + axis] = word(body, offset)
        fields["velocity_" + axis] = word(body, offset + 12)
    fields["speed"] = word(body, 0xe0)
    fields["contact_count"] = body[0x51f]
    for row in range(3):
        for column in range(3):
            fields[f"basis_{row}{column}"] = word(body, 0x150 + row * 12 + column * 4)
    for name, offset in (("throttle", 0x11c), ("engine_speed", 0x124), ("brake", 0x130), ("steering", 0x150)):
        fields[name] = word(dynamics, offset)
    for name, offset in (("steering_angle", 0x274), ("visual_local_x", 0x278), ("visual_absolute_z", 0x27a)):
        fields[name] = word(dynamics, offset, "h")
    fields["gear"] = word(dynamics, 0x330, "b")
    fields["speed_integer"] = word(entry["gameplay"], 0xcc) >> 16
    for name, offset in (("raw_throttle", 0x140), ("raw_brake", 0x144), ("raw_left", 0x138), ("raw_right", 0x13c), ("engine_drive", 0x12c)):
        fields[name] = word(dynamics, offset)
    fields["gear_pending"] = int(word(dynamics, 0x30c) != 0)
    fields["reverse_pedal"] = int(word(dynamics, 0x304) != 0)
    for index in range(8):
        fields[f"tire_offset_{index}"] = word(dynamics, 0xdc + index * 4)
    for wheel in range(4):
        fields[f"rumble_{wheel}"] = int(word(dynamics, 0x280 + wheel * 4) != 0)
        fields[f"previous_rumble_{wheel}"] = int(word(dynamics, 0x290 + wheel * 4) != 0)
        fields[f"surface_{wheel}"] = dynamics[0x328 + wheel]
        fields[f"contact_{wheel}"] = int(body[0x517 + wheel] == 0)
        fields[f"wheel_speed_{wheel}"] = word(dynamics, 0xfc + wheel * 4)
        fields[f"longitudinal_slip_{wheel}"] = word(dynamics, 0x1e4 + wheel * 4)
        fields[f"lateral_slip_{wheel}"] = word(dynamics, 0x1f4 + wheel * 4)
    for axis in range(3):
        fields[f"angular_velocity_{axis}"] = word(body, 0xe4 + axis * 4)
    for point in range(8):
        fields[f"height_{point}"] = word(body, 0x1c4 + point * 4)
        fields[f"triangle_{point}"] = word(dynamics, 0x264 + point * 2, "H")
        for axis in range(3):
            fields[f"contact_position_{point}_{axis}"] = word(body, 0xf0 + point * 12 + axis * 4)
    fields["boundary"] = 0
    actor = entry["actor"]
    for feature in range(17):
        fields[f"damage_level_{feature}"] = state["shared"]["damage_levels"][feature * ACTORS + actor]
    for feature in range(4):
        fields[f"broken_feature_{feature}"] = word(state["shared"]["broken_features"], actor * 16 + feature * 4, "I")
    fields["damage_feedback_side"] = body[0x514]
    fields["damage_feedback_strength"] = word(body, 0x4ec, "H")
    fields["body_mode"] = word(body, 0x4f0, "I")
    fields["roughness_cursor"] = state["roughness_cursor"]
    fields["damage_cycle"] = state["shared"]["damage_cycle"]
    for axis in range(3):
        fields[f"pending_acceleration_{axis}"] = word(body, 0x1f0 + axis * 4)
        fields[f"pending_moment_{axis}"] = word(body, 0x1fc + axis * 4)
    fields["car_contact_count"] = body[0x529]
    for partner in range(15):
        fields[f"car_partner_{partner}"] = body[0x52a + partner] if partner < body[0x529] else -1
    return fields


def compare(records, csv_path, mode, scenario=None):
    import csv
    from pathlib import Path
    if mode not in REPLAY_MODES:
        raise ValueError("Unknown fleet replay scope")
    validate_replay_scope(records, mode)
    with Path(csv_path).open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("Invalid native fleet CSV header")
        rows = list(reader)
    if len(rows) != len(records) * ACTORS * 2:
        raise ValueError("Native fleet output omitted actor/phase rows")
    seen = set()
    mismatches = []
    mismatch_count = 0
    maxima = {}
    actor_counts = [0] * ACTORS
    phase_counts = {"integration": 0, "ground": 0}
    first_update = len(records)
    fields_per_row = None
    first_per_actor_phase = {}
    for ordinal, row in enumerate(rows):
        index = integer(int(row["id"]), 0, len(records) - 1, "native update")
        actor = integer(int(row["actor"]), 0, ACTORS - 1, "native actor")
        phase = row["phase"]
        if phase not in phase_counts:
            raise ValueError("Unknown native fleet phase")
        key = (index, actor, phase)
        if key in seen:
            raise ValueError("Repeated native fleet actor/phase")
        seen.add(key)
        expected_key = (ordinal // (ACTORS * 2), ordinal % ACTORS, "integration" if ordinal % (ACTORS * 2) < ACTORS else "ground")
        if key != expected_key:
            raise ValueError("Native fleet rows changed source update/phase/actor chronology")
        state = records[index]["contacts" if phase == "integration" else "ground"]
        expected = expected_actor(state["actors"][actor], state)
        fields_per_row = len(expected)
        for field, original in expected.items():
            if field not in row:
                raise ValueError("Native fleet field missing: " + field)
            actual = int(row[field])
            difference = actual - original
            maxima[field] = max(maxima.get(field, 0), abs(difference))
            if difference:
                mismatch_count += 1
                actor_counts[actor] += 1
                phase_counts[phase] += 1
                first_update = min(first_update, index)
                first_per_actor_phase.setdefault((actor, phase), {"update": index, "actor": actor, "phase": phase,
                                                                  "field": field, "original": original, "native": actual, "difference": difference})
                if len(mismatches) < 80:
                    mismatches.append({"update": index, "actor": actor, "phase": phase,
                                       "field": field, "original": original, "native": actual, "difference": difference})
    coverage = Coverage(records[0]["pre"])
    rng_boundaries = []
    for record in records:
        coverage.consume(record["consumed"], record["post"], record["ground"])
        rng_boundaries.append({"update": record["id"], **{name: record[name]["rng"] for name in BOUNDARIES}})
    independent = mode == "independent-physics"
    return {"updates": len(records), "actors": ACTORS, "phases": list(phase_counts), "rows": len(rows),
            "fields_per_actor_phase": fields_per_row, "compared_values": len(rows) * fields_per_row,
            "mode": mode, "initial_world_imports": len(records) if independent else 1,
            "actor_state_imports": len(records) * ACTORS if independent else ACTORS,
            "observed_inputs_each_update": ["all16 mapped control axes and AI gear requests", "all16 start-clutch holds and controller ownership",
                                            "local release/reset events and422f60 mapping eligibility; later local launch/RPM/tire values are compared outputs",
                                            "all16 observed AI tire-offset changes, validated against source402b10/402ac0 and captured4c3b88 base",
                                            "source car-contact enable flag",
                                            "pre spatial ring, gaps, ownership, direction and AI tire base; body dispatch derived from retained native state"
                                            if mode == "continuous-derived-body-selection" else "all16 body-mode dispatch",
                                            "all16 retained root registration/detail/visibility", "original delta/reciprocal/extra"],
            "autonomous_ai_parity": False,
            "native_columns_not_compared": sorted(set(rows[0]) - {"id", "actor", "phase"} - set(expected)),
            "captured_but_not_compared": ["pre421aa0 autonomous AI/gameplay progression", "remaining raw body/dynamics/gameplay fields",
                                          "raw prop pools and fragment/spark queues", "pair scrape-handle matrix", "post446a80 visual/audio effects and RNG consumers"],
            "world_assumptions": ["registered cone/post/sign state imported with live sign models",
                                  "normal gravity (source configuration+5c==0)", "imported detached actor/car identity is unknown; numeric physics only", "RNG captured at five boundaries, not replayed or compared"],
            "race_clock_observed": all("race_clock" in record[name] for record in records for name in BOUNDARIES),
            "nonbaseline_initial_props": cold_prop_baseline(records[0]["consumed"]),
            "mismatch_count": mismatch_count, "mismatches_per_actor": actor_counts,
            "mismatches_per_phase": phase_counts, "max_absolute_difference": maxima,
            "first_mismatches": mismatches, "first_mismatch_per_actor_phase": list(first_per_actor_phase.values()),
            "exact_prefix_updates": first_update,
            "coverage": coverage.summary(scenario), "observed_rng_by_boundary": rng_boundaries,
            "parity": mismatch_count == 0, "parity_scope": "named fleet physical outputs only"}


def validate_replay_scope(records, mode):
    """Never accept parity after silently discarding an unsupported warm import."""
    if mode not in REPLAY_MODES:
        raise ValueError("Unknown fleet replay scope")
    imported = records if mode == "independent-physics" else records[:1]
    for record in imported:
        state = record["consumed"]
        shared = state["shared"]
        prefix = f"Unsupported consumed world import at update{record['id']}: "
        if "model_records" not in shared["props"]["signs"]:
            raise ValueError(prefix + "missing live sign model transforms")


def expected_detached(state, slot):
    shared = state["shared"]
    raw = shared["detached_pool"][slot * 0x11c:(slot + 1) * 0x11c]
    word = lambda offset, form="i": struct.unpack_from("<" + form, raw, offset)[0]
    part_type = raw[0x10c]
    fields = {"count": shared["detached_count"], "active": int(word(0x108, "I") != 0),
              "free": struct.unpack_from("<I", shared["detached_free"], slot * 4)[0], "type": part_type,
              "source_slot": {0: 0, 1: 1, 2: 3, 3: 2, 4: 4}.get(part_type, -1)}
    for row in range(3):
        for column in range(3):
            fields[f"basis_{row}{column}"] = word(0xc8 + row * 12 + column * 4)
            fields[f"rotation_{row}{column}"] = word(0x88 + row * 16 + column * 4)
    for axis, name in enumerate("xyz"):
        fields["position_" + name] = word(0xec + axis * 4)
        fields["velocity_" + name] = word(0xf8 + axis * 4)
    fields.update(initial_y=word(0x104), timer=word(0x110), sector=word(0x114, "I"),
                  triangle=word(0x10e, "H"), resting=int(word(0x118, "I") != 0))
    return fields


def detached_lifecycle_record(record):
    """Retain only the observed pool/visibility and initial caller-clock context."""
    result = {"id": record["id"]}
    for phase in ("pre", "consumed", "contacts", "ground", "post"):
        snapshot = record[phase]; shared = snapshot["shared"]
        retained = {name: shared[name] for name in ("detached_count", "sector_visibility_hex", "sector_visibility_disabled", "sector_visibility_override")}
        for name, size in (("detached_pool", 10 * 0x11c), ("detached_free", 40)):
            retained[name] = raw_hex(shared[name + "_hex"], size, "detached lifecycle " + name)
        result[phase] = {"shared": retained}
        if phase == "pre":
            result[phase].update({name: snapshot[name] for name in ("update_counter", "race_clock")})
    return result


def detached_lifecycle_coverage(records):
    """Observe transitions of the real ten slots, without compacting the pool."""
    events = {name: [] for name in ("allocations", "reuses", "landings", "timer_zero", "timer_expirations", "visibility_retirements", "other_retirements")}
    previous, retired, generations = None, set(), [0] * 10
    for record in records:
        for phase in ("consumed", "contacts", "ground", "post"):
            snapshot = record[phase]
            current = [expected_detached(snapshot, slot) for slot in range(10)]
            if previous is not None:
                before_snapshot, before_slots = previous
                for slot, (before, after) in enumerate(zip(before_slots, current)):
                    event = {"update": record["id"], "phase": phase, "slot": slot, "type": before["type"], "generation": generations[slot],
                             "timer_before": before["timer"], "timer_after": after["timer"],
                             "count_before": before["count"], "count_after": after["count"]}
                    if not before["active"] and before["free"] == 1 and after["active"] and after["free"] == 0 and after["count"] > before["count"]:
                        generations[slot] += 1
                        event.update(type=after["type"], generation=generations[slot])
                        events["allocations"].append(event)
                        if slot in retired: events["reuses"].append(event)
                    if before["active"] and after["active"] and not before["resting"] and after["resting"]:
                        events["landings"].append(event)
                    if before["active"] and before["resting"] and after["active"] and after["resting"] and before["timer"] == 1 and after["timer"] == 0:
                        events["timer_zero"].append(event)
                    if before["active"] and not after["active"] and after["free"] == 1 and after["count"] < before["count"]:
                        visible = sector_visible(before["sector"], before_snapshot["shared"])
                        cause = ("timer_expirations" if visible and sector_visible(before["sector"], record["pre"]["shared"]) and before["resting"] and before["timer"] < 1 else
                                 "visibility_retirements" if not visible and before["resting"] and before["timer"] > 0 else "other_retirements")
                        events[cause].append({**event, "sector": before["sector"], "sector_visible": visible})
                        retired.add(slot)
            previous = snapshot, current
    return {"counts": {name: len(values) for name, values in events.items()}, "events": events,
            "positive_expiry_slots": sorted({event["slot"] for event in events["timer_expirations"]}),
            "positive_reuse_slots": sorted({event["slot"] for event in events["reuses"]})}


def validate_detached_expiry_warmup(header, records):
    warmup = header.get("unrecorded_keyboard_warmup")
    if not isinstance(warmup, dict) or "targeted_detached_expiry" not in warmup:
        return None
    metadata = warmup["targeted_detached_expiry"]
    required = {"kind", "slot", "type", "requested_timer", "updates", "maximum_updates", "keys_only", "before", "after", "endpoint", "endpoint_boundary",
                "recorded_boundaries", "continuous_from_grid", "first_rest_timer", "source_policy_parameters", "phase_counts", "batches", "initial_configuration"}
    if not isinstance(metadata, dict) or set(metadata) != required:
        raise ValueError("Incomplete detached-expiry warmup provenance")
    for context in (warmup, metadata):
        if context.get("keys_only") is not True or context.get("recorded_boundaries") is not False or context.get("continuous_from_grid") is not False:
            raise ValueError("Detached-expiry warmup must retain keys-only unrecorded boundaries")
    if metadata["kind"] != "observed-detached-expiry-ready" or metadata["endpoint_boundary"] != "421aa0-entry" or metadata["requested_timer"] != 2:
        raise ValueError("Unknown detached-expiry warmup endpoint")
    slot = integer(metadata["slot"], 0, 9, "detached-expiry selected slot")
    integer(metadata["type"], 0, 4, "detached-expiry source type")
    updates = integer(metadata["updates"], 1, 1400, "detached-expiry warmup updates")
    if integer(metadata["maximum_updates"], 1, 1400, "detached-expiry maximum updates") < updates:
        raise ValueError("Detached-expiry warmup exceeded its source update limit")
    landing_timer = integer(metadata["first_rest_timer"], 3, 700, "observed detached landing timer")
    if updates < landing_timer - metadata["requested_timer"]:
        raise ValueError("Detached-expiry warmup has fewer source updates than its observed timer countdown")
    for phase in ("before", "after"):
        if not isinstance(metadata[phase], dict) or set(metadata[phase]) != {"update_counter", "race_clock"}:
            raise ValueError("Incomplete detached-expiry warmup clock endpoints")
        for value in metadata[phase].values(): integer(value, 0, 0xffffffff, "detached-expiry source clock")
    if (metadata["after"]["update_counter"] - metadata["before"]["update_counter"]) & 0xffffffff != updates:
        raise ValueError("Detached-expiry warmup source update count disagrees")
    first = records[0]["pre"]
    if metadata["after"] != {"update_counter": first["update_counter"], "race_clock": first["race_clock"]}:
        raise ValueError("Detached-expiry warmup endpoint differs from first PRE")
    expected_configuration = {name: header["initial_configuration"][name] for name in ("track_id", "weather", "car_id", "transmission_mode", "steering_profile")}
    if expected_configuration != {"track_id": 0, "weather": 1, "car_id": 7, "transmission_mode": 2, "steering_profile": 2}:
        raise ValueError("Detached-expiry warmup requires the declared car7 sunny Donington automatic profile")
    if not isinstance(metadata["initial_configuration"], dict) or set(metadata["initial_configuration"]) != set(expected_configuration):
        raise ValueError("Incomplete detached-expiry warmup configuration")
    for value in metadata["initial_configuration"].values(): integer(value, 0, 0xffffffff, "detached-expiry configuration value")
    if metadata["initial_configuration"] != expected_configuration:
        raise ValueError("Detached-expiry warmup configuration differs from the measured interval")
    part = expected_detached(first, slot)
    endpoint = {name: part[name] for name in ("active", "free", "resting", "timer", "count", "sector")}
    if not isinstance(metadata["endpoint"], dict) or set(metadata["endpoint"]) != set(endpoint):
        raise ValueError("Incomplete detached-expiry selected slot endpoint")
    for name in ("active", "free", "resting"): integer(metadata["endpoint"][name], 0, 1, "detached-expiry " + name)
    integer(metadata["endpoint"]["timer"], 2, 2, "detached-expiry endpoint timer")
    integer(metadata["endpoint"]["count"], 1, 10, "detached-expiry endpoint pool count")
    integer(metadata["endpoint"]["sector"], 0, 0xffffffff, "detached-expiry endpoint sector")
    if metadata["endpoint"] != endpoint or (part["active"], part["free"], part["resting"], part["timer"], part["type"]) != (1, 0, 1, 2, metadata["type"]) or part["count"] < 1:
        raise ValueError("Detached-expiry selected slot is not the observed resting timer2 endpoint")
    if not sector_visible(part["sector"], first["shared"]):
        raise ValueError("Detached-expiry selected source sector is not visible")
    if not isinstance(metadata["source_policy_parameters"], dict) or not metadata["source_policy_parameters"] or not isinstance(metadata["phase_counts"], dict):
        raise ValueError("Missing detached-expiry keys-only policy provenance")
    for value in metadata["phase_counts"].values(): integer(value, 0, updates, "detached-expiry policy phase count")
    if sum(metadata["phase_counts"].values()) != updates:
        raise ValueError("Detached-expiry policy phase counts disagree with source updates")
    if not isinstance(metadata["batches"], list):
        raise ValueError("Invalid detached-expiry coasting provenance")
    previous, total = metadata["before"], 0
    for batch in metadata["batches"]:
        if not isinstance(batch, dict) or not {"updates", "keys_only", "recorded_boundaries", "continuous_from_grid", "requested_keys", "before", "after"} <= batch.keys():
            raise ValueError("Incomplete detached-expiry warmup batch provenance")
        count = integer(batch["updates"], 1, 32, "detached-expiry coasting batch")
        if batch.get("keys_only") is not True or batch.get("recorded_boundaries") is not False or batch.get("continuous_from_grid") is not False:
            raise ValueError("Detached-expiry batches must retain keys-only unrecorded provenance")
        keys = batch["requested_keys"]
        if not isinstance(keys, list) or any(type(key) is not str for key in keys) or keys != sorted(set(keys)) or any(key not in ("Up", "Down", "Left", "Right") for key in keys) or count > 1 and keys:
            raise ValueError("Invalid detached-expiry physical warmup keys")
        for phase in ("before", "after"):
            if not isinstance(batch[phase], dict) or set(batch[phase]) != {"update_counter", "race_clock"}:
                raise ValueError("Incomplete detached-expiry batch source endpoints")
            for value in batch[phase].values(): integer(value, 0, 0xffffffff, "detached-expiry batch source clock")
        if (batch["after"]["update_counter"] - batch["before"]["update_counter"]) & 0xffffffff != count:
            raise ValueError("Detached-expiry coasting batch source count disagrees")
        if batch["before"] != previous:
            raise ValueError("Detached-expiry warmup batch endpoints are not consecutive")
        previous, total = batch["after"], total + count
    if previous != metadata["after"] or total != updates:
        raise ValueError("Detached-expiry warmup batch totals disagree with source endpoints")
    return metadata


def detached_expiry_coverage(header, records):
    """Shared acquisition/replay gate; source observations alone establish coverage."""
    lifecycle = detached_lifecycle_coverage(records)
    target = validate_detached_expiry_warmup(header, records)
    order = {phase: index for index, phase in enumerate(("consumed", "contacts", "ground", "post"))}
    chosen_expiry = target is not None and any(
        expiry["slot"] == target["slot"] and expiry["type"] == target["type"] and expiry["generation"] == 0
        and any(zero["slot"] == target["slot"] and zero["generation"] == 0
                and (zero["update"], order[zero["phase"]]) < (expiry["update"], order[expiry["phase"]]) for zero in lifecycle["events"]["timer_zero"])
        for expiry in lifecycle["events"]["timer_expirations"])
    return {"lifecycle_coverage": lifecycle, "chosen_expiry_required": target is not None,
            "chosen_expiry_observed": chosen_expiry if target is not None else None, "coverage_sufficient": target is None or chosen_expiry}


def compare_detached(records, csv_path, header=None):
    """Compare every numeric pool slot, including real415e10 post integration."""
    import csv
    from pathlib import Path
    phases = {"integration": "contacts", "ground": "ground", "post": "post"}
    with Path(csv_path).open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("Invalid native detached CSV header")
        rows = list(reader)
    if len(rows) != len(records) * 30:
        raise ValueError("Native detached output omitted slot/phase rows")
    mismatch_count = 0
    first = []
    first_per_slot = {}
    maxima = {}
    first_update = len(records)
    fields = {}
    for ordinal, row in enumerate(rows):
        index, slot, phase = int(row["id"]), int(row["slot"]), row["phase"]
        expected_key = (ordinal // 30, ordinal % 10, list(phases)[ordinal % 30 // 10])
        if (index, slot, phase) != expected_key:
            raise ValueError("Native detached rows changed source update/phase/slot chronology")
        fields = expected_detached(records[index][phases[phase]], slot)
        for name, original in fields.items():
            if name not in row:
                raise ValueError("Native detached field missing: " + name)
            actual = int(row[name])
            difference = actual - original
            maxima[name] = max(maxima.get(name, 0), abs(difference))
            if difference:
                mismatch_count += 1
                first_update = min(first_update, index)
                item = {"update": index, "slot": slot, "phase": phase, "field": name,
                        "original": original, "native": actual, "difference": difference}
                first_per_slot.setdefault((slot, phase), item)
                if len(first) < 80:
                    first.append(item)
    coverage = detached_expiry_coverage(header or {}, records)
    return {"rows": len(rows), "fields_per_slot_phase": len(fields), "compared_values": len(rows) * len(fields),
            "mismatch_count": mismatch_count, "exact_prefix_updates": first_update,
            "max_absolute_difference": maxima, "first_mismatches": first,
            "first_mismatch_per_slot_phase": list(first_per_slot.values()),
            "phases": list(phases), "owner_identity": "unknown for imported records; not reconstructed or compared",
            "parity_scope": "named detached numeric physics only", "numerical_parity": mismatch_count == 0,
            **coverage,
            "remaining_scope": "Imported detached render owner/car model is unknown; original type17 actor identity and exhaust eligibility are unrecorded.",
            "parity": mismatch_count == 0 and coverage["coverage_sufficient"]}


def expected_launch(snapshot):
    state = local_launch_state(snapshot)
    dynamics = bytes.fromhex(snapshot["dynamics_hex"])
    return {"launch": state["launch"], "engine_speed": state["engine_speed"], "start_hold": state["start_hold"],
            **{f"tire_offset_{index}": struct.unpack_from("<i", dynamics, 0xdc + index * 4)[0] for index in range(8)}}


def compare_launch(records, csv_path, mode):
    """Compare retained local controls at415090, before new engine/body physics."""
    import csv
    from pathlib import Path
    if mode not in REPLAY_MODES:
        raise ValueError("Unknown fleet replay scope")
    with Path(csv_path).open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("Invalid native launch CSV header")
        rows = list(reader)
    if len(rows) != len(records):
        raise ValueError("Native launch output omitted local consumed rows")
    first_update, mismatch_count, derived_mismatches = len(records), 0, 0
    first, maxima = [], {}
    events = []
    imported_rows = list(range(len(records))) if mode == "independent-physics" else [0]
    imported = set(imported_rows)
    for index, (record, row) in enumerate(zip(records, rows)):
        state = record["consumed"]
        if (int(row["id"]), int(row["actor"]), row["phase"]) != (record["id"], state["actor"], "consumed"):
            raise ValueError("Native launch rows changed source update/actor/boundary chronology")
        for name, original in expected_launch(state).items():
            if name not in row:
                raise ValueError("Native launch field missing: " + name)
            actual = int(row[name])
            difference = actual - original
            maxima[name] = max(maxima.get(name, 0), abs(difference))
            if difference:
                mismatch_count += 1
                derived_mismatches += index not in imported
                first_update = min(first_update, index)
                if len(first) < 80:
                    first.append({"update": record["id"], "actor": state["actor"], "phase": "consumed",
                                  "field": name, "original": original, "native": actual, "difference": difference})
        metadata = launch_metadata(records, index)
        events.append({"update": record["id"], "imported": index in imported, **metadata})
    derived_rows = len(records) - len(imported_rows)
    derived_events = [event for event in events if not event["imported"]]
    count = lambda flag: sum(bool(event["flags"] & LAUNCH_FLAGS[flag]) for event in derived_events)
    zero_crossings = [event["update"] for event in derived_events if event["flags"] & LAUNCH_FLAGS["decay"]
                      and not event["flags"] & (LAUNCH_FLAGS["begin_before_pre"] | LAUNCH_FLAGS["begin_inside_pre"])
                      and event["pre_launch"] != 0 and event["consumed_launch"] == 0]
    initial = expected_launch(records[0]["consumed"])
    return {"rows": len(rows), "fields_per_row": len(initial), "compared_values": len(rows) * len(initial),
            "boundary": "415090-entry", "imported_consumed_rows": imported_rows, "derived_consumed_rows": derived_rows,
            "initial_import": {"boundary": "415090-entry", "update": records[0]["id"], **initial},
            "release_events_before_pre": count("begin_before_pre"), "release_events_inside_pre": count("begin_inside_pre"),
            "local_mapping_updates": count("decay"), "reset_events_before_pre": count("reset_before_pre"),
            "reset_events_inside_pre": count("reset_inside_pre"), "decay_zero_crossing_updates": zero_crossings,
            "observed_event_provenance": events, "mismatch_count": mismatch_count,
            "derived_mismatch_count": derived_mismatches,
            "max_absolute_difference": maxima, "first_mismatches": first, "exact_prefix_updates": first_update,
            "derived_output_parity": derived_mismatches == 0 if derived_rows else None,
            "parity": mismatch_count == 0,
            "parity_scope": "retained local launch, RPM, clutch and tire outputs after upstream logic; imported rows are initialization evidence",
            "unobserved_input_assumptions": ["normal422f60 playback gate4dcd9e==0; later scalar/RPM/tire outputs are checked, never imported"],
            "autonomous_session_parity": False}


def compare_selection(records, csv_path, mode):
    """414e20/414ef0 decisions compared before415090, never used as inputs."""
    import csv
    from pathlib import Path
    if mode not in REPLAY_MODES:
        raise ValueError("Unknown fleet replay scope")
    with Path(csv_path).open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("Invalid native body-selection CSV header")
        rows = list(reader)
    if len(rows) != len(records) * ACTORS:
        raise ValueError("Native body-selection output omitted update/actor rows")
    mismatch_count, first_update, first = 0, len(records), []
    per_actor = [0] * ACTORS
    transitions = [0] * ACTORS
    directions = set()
    for ordinal, row in enumerate(rows):
        index, actor = divmod(ordinal, ACTORS)
        if (int(row["id"]), int(row["actor"])) != (records[index]["id"], actor):
            raise ValueError("Native body-selection rows changed source update/actor chronology")
        expected = struct.unpack_from("<I", bytes.fromhex(records[index]["consumed"]["actors"][actor]["body_hex"]), 0x4f0)[0]
        actual = integer(int(row["mode"]), 0, 4, "native selected body mode")
        pre = records[index]["pre"]
        previous = struct.unpack_from("<I", bytes.fromhex(pre["actors"][actor]["body_hex"]), 0x4f0)[0]
        transitions[actor] += previous != expected
        if actor == pre["actor"]:
            local_game = bytes.fromhex(pre["actors"][actor]["gameplay_hex"])
            directions.add(int(struct.unpack_from("<i", local_game, 0x10)[0] != 0))
        if actual != expected:
            mismatch_count += 1
            per_actor[actor] += 1
            first_update = min(first_update, index)
            if len(first) < 80:
                first.append({"update": records[index]["id"], "actor": actor, "field": "body_mode",
                              "pre_mode": previous, "original": expected, "native": actual, "difference": actual - expected})
    retained = mode == "continuous-derived-body-selection"
    return {"rows": len(rows), "compared_values": len(rows), "source_boundary": "421aa0-entry",
            "comparison_boundary": "415090-entry", "initial_pre_physical_imports": 1 if retained else len(records),
            "retained_native_selection_updates": len(records) - 1 if retained else 0,
            "observed_pre_inputs": ["spatial ahead/behind ring", "forward gaps", "human/controller identity",
                                    "local direction", "AI tire threshold"],
            "physical_inputs": "initial pre state, then retained native modes/rollover/tire/support/ground-dot state"
                               if retained else "captured pre mode, rollover, tire, support and ground-dot state per update",
            "budget": 4, "budget_provenance": "414d20 local initialization",
            "observed_directions": sorted(directions), "pre_to_consumed_mode_changes_per_actor": transitions,
            "mismatch_count": mismatch_count, "mismatches_per_actor": per_actor,
            "first_mismatches": first, "exact_prefix_updates": first_update,
            "parity": mismatch_count == 0,
            "parity_scope": "body dispatch with retained native physical state and observed pre race metadata"
                            if retained else "independent body-selection decisions from captured pre state",
            "autonomous_ai_parity": False, "autonomous_race_parity": False}


def expected_ai_route(snapshot, actor):
    raw = bytes.fromhex(snapshot["actors"][actor]["gameplay_hex"])
    word = lambda offset: struct.unpack_from("<i", raw, offset)[0]
    if word(0xe4) != word(0xe8):
        raise ValueError("Source AI projected-distance aliases disagree")
    result = {"cache_point": raw[0x9e], "desired_speed": word(0xd0), "alternate_speed": word(0xd4),
              "speed_variation": word(0xd8), "distance": word(0xe0), "projected_distance": word(0xe4),
              "angle": struct.unpack_from("<h", raw, 0xfc)[0], "alternate_angle": struct.unpack_from("<h", raw, 0xfe)[0],
              "route_angle": struct.unpack_from("<h", raw, 0x100)[0], "rng_final": snapshot["rng"]}
    for name, offset in (("target", 0x108), ("alternate", 0x114)):
        for axis, component in enumerate("xyz"):
            result[name + "_" + component] = word(offset + axis * 4)
    result.update(route_next=struct.unpack_from("<h", raw, 0xa2)[0], route_previous=struct.unpack_from("<h", raw, 0xa4)[0],
                  backwards=word(0xc), current_gate=raw[0x9a], previous_gate=raw[0x9b],
                  refresh_ticks=struct.unpack_from("<b", raw, 0x86)[0], refresh_requested=word(0x3c),
                  checkpoint=raw[0xa6], checkpoint_marker=raw[0x9d])
    return result


def ai_route_coverage(header, records):
    """Observed branch evidence within the independently compared updates."""
    resolved = bytes.fromhex(header["ai_initial_state"]["resolved_ai_hex"])
    count = struct.unpack_from("<I", bytes.fromhex(header["ai_initial_state"]["points_hex"]))[0]
    counts = {name: 0 for name in ("route_pair_changes", "current_index_changes", "previous_index_changes",
                                  "forward_index_changes", "backward_index_changes", "other_index_changes",
                                  "index_wraps", "refresh_requests", "human_nearest_route_checks",
                                  "human_checks_with_unchanged_pre_to_return_cursor")}
    changes, recoveries = [], []
    for record in records:
        for actor, (before, after) in enumerate(zip(record["ai_pre"]["actors"], record["ai_post"]["actors"])):
            pre, post = (bytes.fromhex(item["gameplay_hex"]) for item in (before, after))
            old, new = struct.unpack_from("<2h", pre, 0xa2), struct.unpack_from("<2h", post, 0xa2)
            if old != new:
                counts["route_pair_changes"] += 1
                changes.append({"update": record["id"], "actor": actor, "before": list(old), "after": list(new)})
            counts["previous_index_changes"] += old[1] != new[1]
            if old[0] != new[0]:
                counts["current_index_changes"] += 1
                direction = "forward" if new[0] == (old[0] + 1) % count else "backward" if new[0] == old[1] else "other"
                counts[direction + "_index_changes"] += 1
                counts["index_wraps"] += abs(old[0] - new[0]) == count - 1
            #403dd0 overwrites game+3c using this signed counter/period test;
            #the captured PRE+3c flag still belongs to the previous update.
            period = struct.unpack_from("<b", resolved, pre[0x8d] * 40 + 0x13)[0]
            refresh = bool(struct.unpack_from("<I", pre, 4)[0]) and struct.unpack_from("<b", pre, 0x86)[0] >= period
            counts["refresh_requests"] += refresh
            if struct.unpack_from("<I", pre, 8)[0] and refresh:
                counts["human_nearest_route_checks"] += 1
                counts["human_checks_with_unchanged_pre_to_return_cursor"] += old == new
                recoveries.append({"update": record["id"], "actor": actor, "pre_to_return_cursor_changed": old != new})
    return {**counts, "route_changes": changes, "human_recovery_checks": recoveries,
            "refresh_evidence": "signed PRE gameplay+86 counter compared with captured resolved-driver+13 period",
            "scope": "source observations in supported independent updates; cursor deltas cover the entire AI pass and do not attribute movement to nearest-route recovery"}


def compare_ai_route(header, records, csv_path):
    """Independent403dd0 route progression/recovery, not autonomous traffic."""
    import csv
    from pathlib import Path
    validate_ai_route_scope(header, records)
    with Path(str(csv_path).removesuffix(".ai-route.csv") + ".ai-route-unsupported.csv").open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != ["id", "reason"]:
            raise ValueError("Invalid native unsupported AI-route CSV header")
        unsupported = list(reader)
    ids = [int(row["id"]) for row in unsupported]
    if ids != sorted(set(ids)) or any(value not in {record["id"] for record in records} for value in ids):
        raise ValueError("Unsupported AI-route rows changed source update chronology")
    if any(not row["reason"].strip() for row in unsupported):
        raise ValueError("Unsupported AI-route update lacks a reason")
    unsupported_ids = set(ids)
    supported = [(index, record) for index, record in enumerate(records) if record["id"] not in unsupported_ids]
    with Path(csv_path).open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("Invalid native AI-route CSV header")
        rows = list(reader)
    if len(rows) != len(supported) * ACTORS:
        raise ValueError("Native AI-route output omitted update/actor rows")
    first_update = min((index for index, record in enumerate(records) if record["id"] in unsupported_ids), default=len(records))
    mismatches, first, maxima, failed_updates, refreshes, rng_updates = 0, [], {}, set(), 0, 0
    for _, record in supported:
        rng_updates += record["ai_pre"]["rng"] != record["ai_post"]["rng"]
        refreshes += sum(bytes.fromhex(before["gameplay_hex"])[0x9e] != bytes.fromhex(after["gameplay_hex"])[0x9e]
                         for before, after in zip(record["ai_pre"]["actors"], record["ai_post"]["actors"]))
    for ordinal, row in enumerate(rows):
        supported_index, actor = divmod(ordinal, ACTORS)
        index, record = supported[supported_index]
        if (int(row["id"]), int(row["actor"])) != (record["id"], actor):
            raise ValueError("Native AI-route rows changed source update/actor chronology")
        pre = bytes.fromhex(record["ai_pre"]["actors"][actor]["gameplay_hex"])
        if struct.unpack_from("<I", pre, 0x30)[0]:
            raise ValueError("Native AI-route accepted an unsupported pit update")
        expected = expected_ai_route(record["ai_post"], actor)
        for name, original in expected.items():
            if name not in row:
                raise ValueError("Native AI-route field missing: " + name)
            actual = int(row[name])
            difference = actual - original
            maxima[name] = max(maxima.get(name, 0), abs(difference))
            if difference:
                mismatches += 1
                failed_updates.add(index)
                first_update = min(first_update, index)
                if len(first) < 80:
                    first.append({"update": record["id"], "actor": actor, "field": name,
                                  "original": original, "native": actual, "difference": difference})
    return {"rows": len(rows), "compared_values": len(rows) * 25, "fields_per_actor": 25,
            "initial_setup_imports": 1, "pre_actor_imports": len(supported) * ACTORS,
            "rng_initializations": len(supported), "release_counter": "observed at403dd0 entry per update",
            "input_boundary": "403dd0-entry", "comparison_boundary": "403dd0-return",
            "mismatch_count": mismatches, "first_mismatches": first, "max_absolute_difference": maxima,
            "exact_prefix_updates": first_update, "parity": bool(supported) and not unsupported and mismatches == 0,
            "supported_parity": mismatches == 0 if supported else None,
            "supported_updates": len(supported), "supported_exact_updates": len(supported) - len(failed_updates),
            "supported_cache_refreshes": refreshes, "supported_rng_advance_updates": rng_updates,
            "coverage": ai_route_coverage(header, [record for _, record in supported]),
            "unsupported_updates": [{"id": int(row["id"]), "reason": row["reason"]} for row in unsupported],
            "complete_source_interval": not unsupported,
            "parity_scope": "independent route progression, human nearest-route recovery and target/cache/projection from captured initialization and PRE actor state",
            "unsupported_scope": ["timing paths without optional --compare-ai-timing", "finish paths", "pit targets", "autonomous traffic/control/race decisions"],
            "autonomous_ai_parity": False, "autonomous_race_parity": False}


def expected_ai_timing(snapshot, actor, finish=False):
    raw = bytes.fromhex(snapshot["actors"][actor]["gameplay_hex"])
    word = lambda offset: struct.unpack_from("<I", raw, offset)[0]
    short = lambda offset: struct.unpack_from("<h", raw, offset)[0]
    result = {"lap": short(0xa8), "record_flag": word(0), "best_ms": word(0xc0), "current_ms": word(0xc4),
            "lap_start_ms": word(0xc8), "best_minutes": short(0xaa), "best_seconds": short(0xac),
            "best_hundredths": short(0xae), "current_minutes": short(0xb0), "current_seconds": short(0xb2),
            "current_hundredths": short(0xb4), "best_notice": raw[0x8a]}
    if finish:
        result.update(finished=word(0x54), total_time_hundredths=word(0x120), notice=raw[0x8c])
    return result


def expected_checkpoint(snapshot, slot):
    timing = snapshot["shared"]["checkpoint_timing"]
    clock, lap, actor = struct.unpack_from("<Iii", bytes.fromhex(timing["split_table_hex"]), slot * 12)
    return {"clock": clock, "lap": lap, "actor": actor,
            "race_flags": struct.unpack("<I", bytes.fromhex(timing["race_flags_hex"]))[0]}


def compare_ai_timing(header, records, native_path, unsupported, compare_finish=False, session_scope=False, retained_scope=False):
    """Compare all shared entries and actor timers after the independent pass."""
    import csv
    from pathlib import Path
    if header.get("checkpoint_timing_manifest") != CHECKPOINT_TIMING_MANIFEST:
        raise ValueError("AI timing comparison requires --checkpoint-timing original provenance")
    if compare_finish and header.get("finish_timing_manifest") != FINISH_TIMING_MANIFEST:
        raise ValueError("Finish actor timing requires --finish-timing original provenance")
    (validate_retained_race_timing_scope if retained_scope else validate_session_boundary_scope if session_scope else validate_ai_route_scope)(header, records)
    skipped = {row["id"] for row in unsupported}
    if len(skipped) != len(unsupported) or skipped - {record["id"] for record in records}:
        raise ValueError("Invalid unsupported timing update chronology")
    supported = [(index, record) for index, record in enumerate(records) if record["id"] not in skipped]
    first_update = min((index for index, record in enumerate(records) if record["id"] in skipped), default=len(records))
    first, maxima, failed_updates, mismatches, values = [], {}, set(), 0, 0
    for suffix, identity, count, expected in ((".checkpoint.csv", "slot", 48, expected_checkpoint),
                                               (".ai-timing.csv", "actor", ACTORS, lambda state, actor: expected_ai_timing(state, actor, compare_finish))):
        with Path(str(native_path) + suffix).open(newline="") as source:
            reader = csv.DictReader(source)
            #The manifest guarantees source snapshot sizes even if every
            #native update is explicitly unsupported and there are no rows.
            fields = list(expected(records[0]["ai_post"], 0))
            if reader.fieldnames != ["id", identity, *fields]:
                raise ValueError("Invalid native timing CSV header: " + suffix)
            rows = list(reader)
        if len(rows) != len(supported) * count:
            raise ValueError("Native timing output omitted update/" + identity + " rows")
        for ordinal, row in enumerate(rows):
            update, item = divmod(ordinal, count)
            index, record = supported[update]
            if (int(row["id"]), int(row[identity])) != (record["id"], item):
                raise ValueError("Native timing rows changed source update/" + identity + " chronology")
            for name, original in expected(record["ai_post"], item).items():
                actual = int(row[name])
                values += 1
                field = suffix[1:-4] + "." + name
                difference = actual - original
                maxima[field] = max(maxima.get(field, 0), abs(difference))
                if difference:
                    mismatches += 1
                    failed_updates.add(index)
                    first_update = min(first_update, index)
                    if len(first) < 80:
                        first.append({"update": record["id"], identity: item, "field": field,
                                      "original": original, "native": actual, "difference": difference})
    changed_slots, laps, bests, flags = [], [], [], []
    for _, record in supported:
        before, after = record["ai_pre"], record["ai_post"]
        for slot in range(48):
            old, new = expected_checkpoint(before, slot), expected_checkpoint(after, slot)
            if any(old[field] != new[field] for field in ("clock", "lap", "actor")):
                changed_slots.append({"update": record["id"], "slot": slot,
                                      "before": {key: old[key] for key in ("clock", "lap", "actor")},
                                      "after": {key: new[key] for key in ("clock", "lap", "actor")}})
        if expected_checkpoint(before, 0)["race_flags"] != expected_checkpoint(after, 0)["race_flags"]:
            flags.append(record["id"])
        for actor in range(ACTORS):
            old, new = expected_ai_timing(before, actor, compare_finish), expected_ai_timing(after, actor, compare_finish)
            if old["lap"] != new["lap"]:
                laps.append({"update": record["id"], "actor": actor, "before": old["lap"], "after": new["lap"]})
            if old["best_ms"] != new["best_ms"]:
                bests.append({"update": record["id"], "actor": actor, "before": old["best_ms"], "after": new["best_ms"]})
    return {"compared_values": values, "shared_table_slots_per_update": 48, "actor_fields_per_update": ACTORS * (15 if compare_finish else 12),
            "mismatch_count": mismatches, "first_mismatches": first, "max_absolute_difference": maxima,
            "exact_prefix_updates": first_update, "supported_updates": len(supported),
            "supported_exact_updates": len(supported) - len(failed_updates), "unsupported_updates": unsupported,
            "supported_parity": mismatches == 0 if supported else None,
            "complete_source_interval": not unsupported, "parity": bool(supported) and not unsupported and mismatches == 0,
            "pre_table_imports": len(supported), "input_boundary": "403dd0-entry", "comparison_boundary": "403dd0-return",
            "coverage": {"changed_split_slots": len(changed_slots), "split_slot_changes": changed_slots,
                         "lap_transitions": laps, "best_time_changes": bests, "race_flag_change_updates": flags},
            "parity_scope": "independent captured-PRE checkpoint and actor timing side effects" if compare_finish else "independent nonfinish NoQual checkpoint table and lap timer side effects from captured PRE state",
            "outside_compared_fields": ["finish/display globals require separate finish comparison", "local controller handoffs", "autonomous race state"],
            "autonomous_ai_parity": False, "autonomous_race_parity": False}


def expected_finish(snapshot):
    raw = snapshot["shared"]["finish_timing"]
    data = lambda name: bytes.fromhex(raw[name + "_hex"])
    result = dict(zip(("finish_count", "local_finish_count"), struct.unpack("<2I", data("finish_counts"))))
    result.update(zip(("timer2", "timer3"), struct.unpack_from("<2I", data("race_counters"))))
    result.update(pending_state=struct.unpack("<I", data("pending_state"))[0], race_stop=struct.unpack_from("<H", data("race_status"), 2)[0])
    result.update(("local_finished_" + str(i), value) for i, value in enumerate(data("local_finish_flags")))
    result.update(("finish_order_" + str(i), value) for i, value in enumerate(data("finish_order")))
    return result


def retained_race_timing_record(record):
    """Keep only observed timing/lap boundaries; accept recorder or reader data."""
    def hexadecimal(mapping, name):
        value = mapping.get(name + "_hex", mapping.get(name))
        return value.hex() if isinstance(value, (bytes, bytearray)) else value
    result = {"id": record["id"], "pre": {name: record["pre"][name] for name in ("update_counter", "race_clock")}}
    for phase in ("ai_pre", "ai_post"):
        snapshot = record[phase]
        shared = snapshot["shared"]
        result[phase] = {name: snapshot[name] for name in ("update_counter", "race_clock", "session_state")}
        result[phase]["actors"] = [{"actor": actor["actor"], "gameplay_hex": hexadecimal(actor, "gameplay")} for actor in snapshot["actors"]]
        result[phase]["shared"] = {
            "provenance": {"world_status_hex": hexadecimal(shared["provenance"], "world_status")},
            "checkpoint_timing": {"session_stage": shared["checkpoint_timing"]["session_stage"],
                                  **{name + "_hex": hexadecimal(shared["checkpoint_timing"], name) for name in CHECKPOINT_TIMING_BLOCKS}},
            "finish_timing": {name + "_hex": hexadecimal(shared["finish_timing"], name) for name in FINISH_TIMING_BLOCKS}}
    return result


def retained_race_timing_coverage(header, records):
    """Source event coverage shared by acquisition and retained replay."""
    warmup = header.get("unrecorded_keyboard_warmup") or {}
    target = warmup.get("targeted_ai_point", {})
    required = target.get("target_filter") == "finish-ready checkpoint0"
    selected = []
    if required:
        if (target.get("kind") != "observed-ai-point" or target.get("endpoint_boundary") != "421aa0-entry"
                or any(context.get("keys_only") is not True or context.get("recorded_boundaries") is not False or context.get("continuous_from_grid") is not False for context in (warmup, target))
                or target.get("requested_keys") != [] or target.get("target_point") != 0):
            raise ValueError("Unsupported targeted AI finish provenance")
        selected = target.get("selected_rivals")
        if not isinstance(selected, list) or not selected or any(type(actor) is not int or not 0 <= actor < ACTORS for actor in selected) or len(set(selected)) != len(selected):
            raise ValueError("Invalid targeted AI finish actors")
        if records:
            initial = retained_race_timing_record(records[0])
            if target.get("after") != initial["pre"]:
                raise ValueError("Targeted AI finish endpoint differs from first recorded update")
            snapshot = initial["ai_pre"]
            limit = struct.unpack_from("<H", bytes.fromhex(snapshot["shared"]["provenance"]["world_status_hex"]), 0x14)[0]
            if target.get("lap_context") != {"session_stage": snapshot["shared"]["checkpoint_timing"]["session_stage"], "session_state": snapshot["session_state"], "lap_limit": limit}:
                raise ValueError("Targeted AI finish lap context differs from recorded input")
            endpoints = target.get("endpoint_actors")
            if not isinstance(endpoints, list) or len(endpoints) != ACTORS:
                raise ValueError("Missing targeted AI finish actor provenance")
            for actor in selected:
                raw = bytes.fromhex(snapshot["actors"][actor]["gameplay_hex"])
                observed = {"actor": actor, "route_next": struct.unpack_from("<h", raw, 0xa2)[0], "route_previous": struct.unpack_from("<h", raw, 0xa4)[0],
                            "human": struct.unpack_from("<I", raw, 8)[0], "player_identity": struct.unpack_from("<b", raw, 0x80)[0],
                            "backwards": struct.unpack_from("<I", raw, 0xc)[0], "active": struct.unpack_from("<I", raw, 4)[0] != 0,
                            "checkpoint": raw[0xa6], "checkpoint_marker": raw[0x9d], "lap": struct.unpack_from("<h", raw, 0xa8)[0],
                            "finished": struct.unpack_from("<I", raw, 0x54)[0]}
                if endpoints[actor] != observed or not observed["active"] or any(observed[name] != value for name, value in
                        (("route_next", 0), ("human", 0), ("player_identity", -1), ("backwards", 0), ("checkpoint", 0), ("checkpoint_marker", 0), ("lap", limit), ("finished", 0))):
                    raise ValueError("Targeted AI finish actor is not the observed finish-ready rival")
    laps, finishes, resets, chosen = [], [], [], []
    for record in records:
        projected = retained_race_timing_record(record)
        before, after = (projected[phase] for phase in ("ai_pre", "ai_post"))
        limit = struct.unpack_from("<H", bytes.fromhex(before["shared"]["provenance"]["world_status_hex"]), 0x14)[0]
        old = bytes.fromhex(before["shared"]["finish_timing"]["finish_counts_hex"])[0]
        new = bytes.fromhex(after["shared"]["finish_timing"]["finish_counts_hex"])[0]
        order = bytes.fromhex(after["shared"]["finish_timing"]["finish_order_hex"])
        candidates = []
        for actor, (entry, output) in enumerate(zip(before["actors"], after["actors"])):
            if (entry["actor"], output["actor"]) != (actor, actor):
                raise ValueError("Reordered retained timing actor boundary")
            pre, post = bytes.fromhex(entry["gameplay_hex"]), bytes.fromhex(output["gameplay_hex"])
            pre_lap, post_lap = (struct.unpack_from("<h", raw, 0xa8)[0] for raw in (pre, post))
            if pre_lap != post_lap:
                laps.append({"update": record["id"], "actor": actor, "before": pre_lap, "after": post_lap})
            if (struct.unpack_from("<b", pre, 0x80)[0] == -1 and struct.unpack_from("<I", pre, 8)[0] == 0
                    and struct.unpack_from("<I", pre, 0x54)[0] == 0 and struct.unpack_from("<I", post, 0x54)[0] == 1
                    and pre_lap == limit and post_lap == limit + 1):
                candidates.append({"update": record["id"], "actor": actor, "driver": pre[0x8d], "lap": post_lap})
        count_matches = new <= ACTORS and new - old == len(candidates) and list(order[old:new]) == [event["driver"] for event in candidates]
        incremented = before["update_counter"] == (record["pre"]["update_counter"] + 1) & 0xffffffff
        timer3 = struct.unpack_from("<I", bytes.fromhex(after["shared"]["finish_timing"]["race_counters_hex"]), 4)[0]
        reset = old == 0 and new > 0 and timer3 == 0 and after["update_counter"] == 0
        chronology = incremented and (reset if old == 0 and candidates else after["update_counter"] == before["update_counter"] == timer3)
        ordinary = before["session_state"] == after["session_state"] == 6 and before["shared"]["checkpoint_timing"]["session_stage"] in (2, 4)
        if candidates and count_matches and chronology and ordinary:
            finishes.extend(candidates)
            if reset: resets.append(record["id"])
            chosen.extend(event for event in candidates if event["actor"] in selected and reset)
    observed = bool(chosen) if required else bool(finishes)
    sufficient = bool(laps and finishes and (observed if required else True))
    return {"targeted_ai_finish_required": required, "targeted_ai_finish_observed": observed,
            "coverage_sufficient": sufficient,
            "coverage": {"lap_transitions": laps, "newly_finished_ai": finishes,
                         "first_finisher_counter_resets": resets, "selected_rivals": selected, "targeted_chosen_events": chosen}}


def retained_local_session_record(record):
    """JSON-safe local finish/session evidence without physical world payloads."""
    def hexadecimal(mapping, name):
        value = mapping.get(name + "_hex", mapping.get(name))
        return value.hex() if isinstance(value, (bytes, bytearray)) else value
    result = {"id": record["id"], "local_handoffs": record.get("local_handoffs", []),
              "combined_journal": [entry for entry in record["combined_journal"] if entry["kind"] in ("primary", "restart_request", "restart", "warm_grid")]}
    for phase in ("pre", "ai_pre", "ai_post", "consumed", "post"):
        snapshot = record[phase]; shared = snapshot["shared"]
        result[phase] = {name: snapshot[name] for name in ("update_counter", "race_clock", "session_state")}
        result[phase]["actors"] = [{"actor": actor["actor"], "gameplay_hex": hexadecimal(actor, "gameplay")} for actor in snapshot["actors"]]
        result[phase]["shared"] = {
            "provenance": {name + "_hex": hexadecimal(shared["provenance"], name) for name in PROVENANCE_BLOCKS},
            "lifecycle": {**{name + "_hex": hexadecimal(shared["lifecycle"], name) for name in LIFECYCLE_BLOCKS},
                          "local_session_entry_flag": shared["lifecycle"]["local_session_entry_flag"]},
            "checkpoint_timing": {"session_stage": shared["checkpoint_timing"]["session_stage"],
                                  **{name + "_hex": hexadecimal(shared["checkpoint_timing"], name) for name in CHECKPOINT_TIMING_BLOCKS}},
            "finish_timing": {name + "_hex": hexadecimal(shared["finish_timing"], name) for name in FINISH_TIMING_BLOCKS},
            "session_boundary": {name + "_hex": hexadecimal(shared["session_boundary"], name) for name in SESSION_TYPED_FIELDS}}
    return result


def retained_local_session_coverage(header, records):
    """Require the chosen local handoff/classification and later state13."""
    warmup = header.get("unrecorded_keyboard_warmup") or {}
    target = warmup.get("targeted_local_finish")
    required = target is not None or cold_local_session_enabled(header)
    if target is not None:
        if not isinstance(target, dict) or any(context.get("keys_only") is not True or context.get("continuous_from_grid") is not False or context.get("recorded_boundaries") is not False for context in (warmup, target)):
            raise ValueError("Unsupported targeted local finish provenance")
        if target.get("actor") != header["local_actor"] or target.get("endpoint_boundary") != "421aa0-entry":
            raise ValueError("Invalid targeted local finish actor/boundary")
    projected = [retained_local_session_record(record) for record in records]
    if projected:
        validate_local_session_lifecycle(header, projected)
    if target is not None and projected:
        first = projected[0]["pre"]
        stage = first["shared"]["checkpoint_timing"]["session_stage"]
        limit = struct.unpack_from("<H", bytes.fromhex(first["shared"]["provenance"]["world_status_hex"]), 0x14)[0]
        endpoint = {"session_state": first["session_state"], "session_stage": stage,
                    "update_counter": first["update_counter"], "race_clock": first["race_clock"]}
        raw = bytes.fromhex(first["actors"][header["local_actor"]]["gameplay_hex"])
        if (target.get("endpoint") != endpoint or target.get("target_lap") != limit or stage not in (2, 4) or first["session_state"] != 6
                or struct.unpack_from("<I", raw, 4)[0] != 1 or struct.unpack_from("<I", raw, 8)[0] != 1
                or struct.unpack_from("<b", raw, 0x80)[0] != 0 or struct.unpack_from("<I", raw, 0x54)[0] != 0
                or struct.unpack_from("<h", raw, 0xa8)[0] != limit or struct.unpack_from("<h", raw, 0xa2)[0] != 0
                or raw[0xa6] != 0 or raw[0x9d] != 0 or struct.unpack_from("<I", raw, 0xc)[0] != 0):
            raise ValueError("Targeted local finish endpoint is not the recorded final-line human")
    finishes, classifications, primaries, states, pending13 = [], [], [], [], []
    local = header["local_actor"]
    for record in projected:
        for entry in record["combined_journal"]:
            if entry["kind"] == "primary": primaries.append({"update": record["id"], **entry["event"]})
        for phase in ("pre", "ai_pre", "ai_post", "post"):
            snapshot = record[phase]
            states.append({"update": record["id"], "phase": phase, "state": snapshot["session_state"]})
            if expected_finish(snapshot)["pending_state"] & 0xffff == 13:
                pending13.append(record["id"])
        before, after = record["ai_pre"], record["ai_post"]
        pre, post = (bytes.fromhex(snapshot["actors"][local]["gameplay_hex"]) for snapshot in (before, after))
        old, new = expected_finish(before), expected_finish(after)
        calls = record["local_handoffs"]
        call = len(calls) == 1 and (calls[0]["kind"], calls[0]["actor"], calls[0]["return_address"]) == (0, local, 0x42160b)
        position = new["local_finished_0"]
        placed = (1 <= position <= ACTORS and (old["finish_count"] & 0xff) < position <= (new["finish_count"] & 0xff)
                  and new["finish_order_" + str(position - 1)] == local)
        if (struct.unpack_from("<I", pre, 8)[0] == 1 and struct.unpack_from("<I", post, 8)[0] == 0
                and struct.unpack_from("<I", pre, 0x54)[0] == 0 and struct.unpack_from("<I", post, 0x54)[0] == 1
                and (new["local_finish_count"] & 0xff) == (old["local_finish_count"] & 0xff) + 1
                and old["local_finished_0"] == 0 and placed
                and new["pending_state"] & 0xffff == 10 and new["race_stop"] == 1 and call):
            validate_local_handoffs(record, local)
            previous, current = (expected_session_classification(snapshot) for snapshot in (before, after))
            changed = [name for name in current if current[name] != previous[name] and name.startswith(("classification_order_", "score_", "podium_"))]
            if changed:
                classifications.append({"update": record["id"], "fields": changed})
                finishes.append({"update": record["id"], "actor": local, "return_address": 0x42160b})
    observed = bool(finishes)
    first_finish = finishes[0]["update"] if finishes else None
    ten = next((row["update"] for row in states if row["state"] == 10 and first_finish is not None and row["update"] > first_finish), None)
    thirteen = next((row["update"] for row in states if row["state"] == 13 and ten is not None and row["update"] > ten), None)
    pending_before_primary = (thirteen is not None and any(first_finish < update < thirteen for update in pending13)
                              and any(min(pending13) < event["update"] <= thirteen for event in primaries))
    # The state10 wrapper can write pending13 after physical POST; the next
    # actual primary commits state13 before either write is visible at PRE.
    primary_results_commit = any(
        current["id"] == thirteen and previous["id"] + 1 == current["id"]
        and sum(entry["kind"] == "primary" for entry in current["combined_journal"]) == 1
        and all(previous[phase]["session_state"] == 10 and expected_finish(previous[phase])["pending_state"] & 0xffff == 10
                and current[phase]["session_state"] == 13 and expected_finish(current[phase])["pending_state"] & 0xffff == 13
                for phase in ("pre", "ai_pre", "ai_post", "consumed", "post"))
        for previous, current in zip(projected, projected[1:]))
    committed = (ten is not None and thirteen is not None and any(first_finish < event["update"] <= ten for event in primaries)
                 and (pending_before_primary or primary_results_commit))
    return {"targeted_local_finish_required": required, "targeted_local_finish_observed": observed,
            "coverage_sufficient": bool(observed and committed),
            "coverage": {"local_finishes": finishes, "classification_changes": classifications, "session_states": states,
                         "primary_dispatches": primaries, "state10_update": ten, "state13_update": thirteen}}


def retained_warm_restart_coverage(header, records):
    """One real confirmation/reset/grid transaction must feed later grid updates."""
    required = retained_warm_restart_enabled(header)
    local = retained_local_session_coverage(header, records)
    requests, restarts, grids, warm_updates, active_updates = [], [], [], [], []
    previous_state = records[0]["pre"]["session_state"] if records else 6
    for record in records:
        for entry in record["combined_journal"]:
            event = entry["event"]
            if entry["kind"] == "restart_request":
                if set(event) != {"order", "action"} or requests or previous_state != 13 or event.get("action") != "confirm":
                    raise ValueError("Warm restart requires one real result-menu confirmation")
                integer(event["order"], 1, 0x7fffffff, "warm confirmation order")
                requests.append({"update": record["id"], "order": event["order"]})
            elif entry["kind"] == "restart":
                if set(event) != {"order", "creation_clock"} or len(requests) != 1 or restarts:
                    raise ValueError("Warm restart transaction lacks its unique confirmation")
                integer(event["order"], 1, 0x7fffffff, "warm restart order")
                restarts.append({"update": record["id"], "creation_clock": integer(event["creation_clock"], 0, 0xffffffff, "warm creation clock")})
            elif entry["kind"] == "warm_grid":
                if len(restarts) != 1 or grids:
                    raise ValueError("Warm grid requires one preceding restart transaction")
                before, after = validate_warm_grid_event(event)
                grids.append({"update": record["id"], "rng_before": event["initialization"]["pre"]["rng"],
                              "rng_after": event["initialization"]["post"]["rng"], "classification_draws": ACTORS - 1,
                              "counter_before": struct.unpack_from("<I", before["race_lifecycle"], 4)[0],
                              "counter_after": struct.unpack_from("<I", after["race_lifecycle"], 4)[0]})
        if grids and record["pre"]["session_state"] == record["ai_post"]["session_state"] == 9:
            warm_updates.append(record["id"])
        if grids and record["pre"]["session_state"] == record["ai_post"]["session_state"] == 6:
            active_updates.append(record["id"])
        previous_state = record["ai_post"]["session_state"]
    observed = len(requests) == len(restarts) == len(grids) == 1
    sufficient = observed and local["coverage_sufficient"] and len(warm_updates) >= 2 and (not cold_local_session_enabled(header) or bool(active_updates))
    return {"targeted_warm_restart_required": required, "targeted_warm_restart_observed": observed,
            "coverage_sufficient": bool(sufficient), "coverage": {"local_session": local["coverage"], "requests": requests,
                "restarts": restarts, "warm_grid_writers": grids, "warm_update_ids": warm_updates,
                "minimum_warm_updates": 2, "warm_active_update_ids": active_updates}}


def expected_split_display(snapshot, player):
    raw = snapshot["shared"]["finish_timing"]
    data = lambda name: bytes.fromhex(raw[name + "_hex"])
    result = {name: struct.unpack_from("<i", data("display_" + name), player * 4)[0] for name in ("mode", "leader", "active")}
    result.update((name, struct.unpack_from("<h", data("display_" + name), player * 2)[0]) for name in ("ticks", "minutes", "seconds", "hundredths"))
    result.update((name + "_" + str(sector), struct.unpack_from("<i", data(name + "_" + str(sector)), player * 4)[0])
                  for name in ("reference", "current") for sector in range(3))
    result.update((name, data(name)[player * 16:(player + 1) * 16].split(b"\0", 1)[0].decode("ascii")) for name in ("display", "leader_display"))
    result.update(("persisted_" + str(sector), struct.unpack("<i", data("persisted_" + str(sector)))[0]) for sector in range(3))
    return result


def compare_ai_finish(header, records, native_path, unsupported, session_scope=False, retained_scope=False):
    """Finish globals and shared numeric/text split display from independent PRE."""
    import csv
    from pathlib import Path
    if header.get("finish_timing_manifest") != FINISH_TIMING_MANIFEST:
        raise ValueError("AI finish comparison requires --finish-timing original provenance")
    (validate_retained_race_timing_scope if retained_scope else validate_session_boundary_scope if session_scope else validate_ai_route_scope)(header, records)
    ids = [row["id"] for row in unsupported]
    if ids != sorted(set(ids)) or set(ids) - {record["id"] for record in records}:
        raise ValueError("Invalid unsupported finish update chronology")
    supported = [(i, record) for i, record in enumerate(records) if record["id"] not in set(ids)]
    first_update = min((i for i, record in enumerate(records) if record["id"] in set(ids)), default=len(records))
    mismatches, display_mismatches, values, text_values, first, maxima, failed = 0, 0, 0, 0, [], {}, set()
    for suffix, identity, count, expected in ((".finish.csv", None, 1, lambda state, _: expected_finish(state)),
                                               (".split-display.csv", "player", 4, expected_split_display)):
        fields = list(expected(records[0]["ai_post"], 0))
        with Path(str(native_path) + suffix).open(newline="") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames != ["id", *([identity] if identity else []), *fields]:
                raise ValueError("Invalid native finish CSV header: " + suffix)
            rows = list(reader)
        if len(rows) != len(supported) * count:
            raise ValueError("Native finish output omitted update/player rows")
        for ordinal, row in enumerate(rows):
            update, player = divmod(ordinal, count)
            index, record = supported[update]
            if int(row["id"]) != record["id"] or identity and int(row[identity]) != player:
                raise ValueError("Native finish rows changed source update/player chronology")
            for name, original in expected(record["ai_post"], player).items():
                is_text = isinstance(original, str)
                actual = row[name] if is_text else int(row[name])
                values += 1
                text_values += is_text
                field = suffix[1:-4] + "." + name
                if not is_text:
                    maxima[field] = max(maxima.get(field, 0), abs(actual - original))
                if actual != original:
                    mismatches += 1
                    display_mismatches += identity == "player"
                    failed.add(index)
                    first_update = min(first_update, index)
                    if len(first) < 80:
                        first.append({"update": record["id"], **({"player": player} if identity else {}), "field": field,
                                      "original": original, "native": actual, "difference": None if is_text else actual - original})
    finishes, resets, displays = [], [], []
    for _, record in supported:
        if validate_update_counters(record, True):
            resets.append(record["id"])
        for actor in range(ACTORS):
            old, new = (expected_ai_timing(record[name], actor, True) for name in ("ai_pre", "ai_post"))
            if not old["finished"] and new["finished"]:
                finishes.append({"update": record["id"], "actor": actor, "lap": new["lap"], "total_time_hundredths": new["total_time_hundredths"]})
        for player in range(4):
            old, new = (expected_split_display(record[name], player) for name in ("ai_pre", "ai_post"))
            fields = [name for name in old if old[name] != new[name]]
            if fields:
                displays.append({"update": record["id"], "player": player, "fields": fields})
    return {"compared_values": values, "compared_numeric_values": values - text_values, "compared_strings": text_values,
            "mismatch_count": mismatches, "first_mismatches": first, "max_absolute_difference": maxima,
            "exact_prefix_updates": first_update, "supported_updates": len(supported), "supported_exact_updates": len(supported) - len(failed),
            "unsupported_updates": unsupported, "supported_parity": mismatches == 0 if supported else None,
            "complete_source_interval": not unsupported, "parity": bool(supported) and not unsupported and mismatches == 0,
            "input_boundary": "403dd0-entry", "comparison_boundary": "403dd0-return", "pre_global_imports": len(supported),
            "coverage": {"newly_finished_actors": finishes, "first_finisher_counter_resets": resets, "display_changes": displays},
            "original_player_split_display_parity": display_mismatches == 0 if displays else None,
            "original_qualifying_event_parity": None,
            "parity_scope": "independent ordinary AI finish and shared player split-display kernel from observed PRE globals",
            "outside_compared_fields": ["local race-finish controller handoff", "qualifying outlap/final controller handoffs", "original qualifying event parity", "autonomous race/session state"],
            "autonomous_ai_parity": False, "autonomous_race_parity": False}


def expected_session(snapshot):
    shared = snapshot["shared"]
    return {"release_counter": shared["ai_state"]["release_counter"],
            "release_epoch": struct.unpack_from("<I", bytes.fromhex(shared["finish_timing"]["race_counters_hex"]), 12)[0],
            "race_running": struct.unpack_from("<I", bytes.fromhex(shared["provenance"]["noqual_flags_hex"]), 8)[0],
            **dict(zip(("launch_tire_" + str(i) for i in range(4)), struct.unpack("<4i", bytes.fromhex(shared["provenance"]["local_launch_tires_hex"]))))}


def compare_retained_race_timing(header, records, native_path):
    """Compare the retained timing world at the actual joined AI return."""
    validate_retained_race_timing_scope(header, records)
    timing = compare_ai_timing(header, records, native_path, [], compare_finish=True, retained_scope=True)
    finish = compare_ai_finish(header, records, native_path, [], retained_scope=True)
    local = retained_local_session_enabled(header)
    coverage = (retained_warm_restart_coverage if retained_warm_restart_enabled(header) else retained_local_session_coverage if local else retained_race_timing_coverage)(header, records)
    numerical = timing["parity"] and finish["parity"]
    timing.update(pre_table_imports=1, parity_scope="retained ordinary checkpoint table and actor lap timing through joined drivers")
    finish.update(pre_global_imports=1, parity_scope="retained local/AI finish/order/timer state and inactive split displays through joined drivers" if local
                  else "retained AI finish/order/timer state and inactive split displays through joined drivers")
    return {"parity": numerical and coverage["coverage_sufficient"], "numerical_parity": numerical,
            "complete_source_interval": True, "requested_updates": len(records), "compared_updates": len(records),
            "exact_prefix_updates": min(timing["exact_prefix_updates"], finish["exact_prefix_updates"]),
            "compared_values": timing["compared_values"] + finish["compared_values"],
            "mismatch_count": timing["mismatch_count"] + finish["mismatch_count"],
            "first_mismatches": (timing["first_mismatches"] + finish["first_mismatches"])[:100],
            **coverage, "timing": timing, "finish_display": finish,
            "initial_timing_world_imports": 1, "later_timing_world_imports": 0,
            "input_boundary": "first403dd0-entry", "comparison_boundary": "every403dd0-return",
            "input_scope": "one initial timing/finish/display world; journal AI caller clocks remain observed; later timer3 counters, lap/finish/order/checkpoint outputs are generated",
            "outside_compared_fields": ([] if local else ["local finish/controller/classification"]) +
                ["qualifying", "retirement RNG fallback", "pit branches", "external active split-display lifetime", "autonomous clocks/physics"]}


def compare_retained_local_session(header, records, native_path):
    """Existing session/controller/classification outputs, without PRE restoration."""
    import csv
    validate_local_session_lifecycle(header, records)
    mismatches, failed, values, unclaimed = [], set(), 0, 0
    schemas = ((".session.csv", None, expected_session), (".session-controller.csv", "actor", expected_session_controller),
               (".session-classification.csv", None, expected_session_classification))
    for suffix, identity, expected in schemas:
        fields = list(expected(records[0]["ai_post"]))
        with Path(str(native_path) + suffix).open(newline="") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames != ["id", *([identity] if identity else []), *fields]:
                raise ValueError("Unexpected retained session sidecar header: " + suffix)
            rows = list(reader)
        if len(rows) != len(records):
            raise ValueError("Missing or extra retained session output rows: " + suffix)
        for record, row in zip(records, rows):
            if int(row["id"]) != record["id"] or identity and int(row[identity]) != header["local_actor"]:
                raise ValueError("Retained session output changed source chronology")
            for field, original in expected(record["ai_post"]).items():
                if (suffix == ".session-controller.csv" and field in ("raw_steering_left", "raw_steering_right")
                        and local_state13_wrapper_axes_unclaimed(header, record["ai_post"], header["local_actor"])):
                    unclaimed += 1
                    continue
                values += 1
                actual = int(row[field])
                if actual != original:
                    failed.add(record["id"])
                    mismatches.append({"update": record["id"], "field": suffix + "." + field, "original": original, "native": actual})
    handoffs = compare_local_handoffs(header, records, Path(str(native_path) + ".local-handoffs.csv"))
    warm = retained_warm_restart_enabled(header)
    coverage = (retained_warm_restart_coverage if warm else retained_local_session_coverage)(header, records)
    numerical = not mismatches and handoffs["mismatch_count"] == 0
    return {"parity": numerical and coverage["coverage_sufficient"], "numerical_parity": numerical,
            "compared_values": values + handoffs["compared_values"], "mismatch_count": len(mismatches) + handoffs["mismatch_count"],
            "first_mismatches": (mismatches + handoffs["mismatches"])[:100],
            "exact_prefix_updates": min(min(failed, default=len(records)), handoffs["exact_prefix_updates"] if handoffs["event_count"] else len(records)),
            "complete_source_interval": True, "requested_updates": len(records), "compared_updates": len(records), **coverage,
            "handoffs": handoffs, "initial_classification_world_imports": 1, "later_classification_world_imports": 0,
            **local_state13_axes_scope(header, unclaimed, "403dd0-return", ("raw_steering_left", "raw_steering_right")),
            "input_scope": "initial classification/release/split-enable context; retained controller/gameplay state; neutral primary cached-clock events and observed AI caller clocks",
            "outside_compared_fields": ["qualifying", "retirement RNG fallback", "pits", "autonomous clock/callback/input scheduling"] +
                (["frontend outside the actual RACE AGAIN confirmation"] if warm else ["frontend after state13"])}


def expected_session_controller(snapshot):
    values = local_handoff_values({**snapshot, "ai_tire_base": struct.unpack_from("<i", bytes.fromhex(snapshot["shared"]["provenance"]["ai_globals_hex"]), 0x20)[0]})
    dynamics = bytes.fromhex(snapshot["dynamics_hex"])
    values.update(raw_steering_left=struct.unpack_from("<i", dynamics, 0x138)[0],
                  raw_steering_right=struct.unpack_from("<i", dynamics, 0x13c)[0],
                  steering_angle=struct.unpack_from("<h", dynamics, 0x274)[0])
    return values


SESSION_CLASSIFICATION_FIELDS = ("release_marker", *("release_flag_" + str(i) for i in range(4)),
                                 *("split_enable_" + str(i) for i in range(4)),
                                 *("classification_order_" + str(i) for i in range(16)),
                                 *("score_" + str(i) for i in range(16)),
                                 *("podium_" + str(i) for i in range(3)), "configuration_624")


def expected_session_classification(snapshot):
    raw = snapshot["shared"]["session_boundary"]
    data = lambda name: bytes.fromhex(raw[name + "_hex"])
    result = {"release_marker": struct.unpack("<I", data("release_marker"))[0]}
    for name, count, form in (("release_flag", 4, "I"), ("split_enable", 4, "I"), ("classification_order", 16, "B")):
        source = "release_flags" if name == "release_flag" else name
        result.update((name + "_" + str(i), value) for i, value in enumerate(struct.unpack("<" + str(count) + form, data(source))))
    config = data("configuration_tail")
    result.update(("score_" + str(i), value) for i, value in enumerate(struct.unpack_from("<16h", config, 2)))
    result.update(("podium_" + str(i), config[0x2c + i]) for i in range(3))
    result["configuration_624"] = struct.unpack_from("<I", config, 0x28)[0]
    return result


def compare_session_boundaries(header, records, native_path):
    """Independent source timer/checkpoint/caller effects; no AI route or physics."""
    import csv
    from pathlib import Path
    validate_session_boundary_scope(header, records)
    def rows(suffix, columns):
        with Path(str(native_path) + suffix).open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != columns:
                raise ValueError("Unexpected session boundary sidecar header: " + suffix)
            return list(reader)
    unsupported = rows(".session-unsupported.csv", ["id", "reason"])
    for item in unsupported:
        item["id"] = int(item["id"])
        if not item["reason"]:
            raise ValueError("Session unsupported update lacks reason")
    ids = [row["id"] for row in unsupported]
    if ids != sorted(set(ids)) or set(ids) - {record["id"] for record in records}:
        raise ValueError("Invalid unsupported session update chronology")
    supported = [record for record in records if record["id"] not in set(ids)]
    timing = compare_ai_timing(header, records, native_path, unsupported, compare_finish=True, session_scope=True)
    finish = compare_ai_finish(header, records, native_path, unsupported, session_scope=True)
    mismatches, values, failed = [], 0, set()
    schemas = [(".session.csv", None, list(expected_session(records[0]["ai_post"])), expected_session, supported),
               (".session-controller.csv", "actor", list(expected_session_controller(records[0]["ai_post"])), expected_session_controller, supported),
               (".session-classification.csv", None, list(SESSION_CLASSIFICATION_FIELDS), expected_session_classification,
                [record for record in supported if "session_boundary" in record["ai_pre"]["shared"]])]
    for suffix, identity, fields, expected, subset in schemas:
        actual_rows = rows(suffix, ["id", *([identity] if identity else []), *fields])
        if len(actual_rows) != len(subset):
            raise ValueError("Missing or extra session boundary sidecar update: " + suffix)
        for record, row in zip(subset, actual_rows):
            if int(row["id"]) != record["id"] or identity and int(row[identity]) != header["local_actor"]:
                raise ValueError("Out-of-order session boundary sidecar update/actor")
            for field, original in expected(record["ai_post"]).items():
                values += 1
                actual = int(row[field])
                if actual != original:
                    failed.add(record["id"])
                    mismatches.append({"update": record["id"], "field": suffix[1:-4] + "." + field,
                                       "original": original, "native": actual})
    events = [{"update": record["id"], "actor": event["actor"], "kind": event["kind"],
               "return_address": f"{event['return_address']:08x}"}
              for record in supported for event in record.get("local_handoffs", [])]
    classification, global_changes = [], []
    for record in supported:
        if "session_boundary" not in record["ai_pre"]["shared"]:
            continue
        old, new = (expected_session_classification(record[name]) for name in ("ai_pre", "ai_post"))
        fields = [field for field in old if old[field] != new[field]]
        if fields:
            global_changes.append({"update": record["id"], "fields": fields})
        classification_fields = [field for field in fields if field.startswith(("classification_order_", "score_", "podium_"))]
        if classification_fields:
            classification.append({"update": record["id"], "fields": classification_fields})
    total_mismatches = len(mismatches) + timing["mismatch_count"] + finish["mismatch_count"]
    exact_prefix = min(timing["exact_prefix_updates"], finish["exact_prefix_updates"], min(failed, default=len(records)))
    return {"parity": bool(supported) and not unsupported and total_mismatches == 0,
            "supported_parity": total_mismatches == 0 if supported else None,
            "supported_updates": len(supported), "unsupported_updates": unsupported,
            "complete_source_interval": not unsupported, "exact_prefix_updates": exact_prefix,
            "compared_values": values + timing["compared_values"] + finish["compared_values"],
            "mismatch_count": total_mismatches, "first_mismatches": mismatches[:80],
            "timing": timing, "finish_display": finish,
            "original_player_split_display_parity": finish["original_player_split_display_parity"],
            "original_local_caller_parity": total_mismatches == 0 if events else None,
            "original_classification_parity": total_mismatches == 0 if classification else None,
            "coverage": {"observed_local_calls": events, "extended_provenance_updates": len(schemas[2][4]),
                         "classification_changes": classification, "session_global_changes": global_changes,
                         "player_display_changes": finish["coverage"]["display_changes"]},
            "input_boundary": "403dd0-entry", "comparison_boundary": "403dd0-return",
            "parity_scope": "independent checkpoint/timer/local421390 caller and observed classification side effects",
            "physical_parity": None, "autonomous_ai_parity": None, "autonomous_race_parity": None,
            "route_target_parity": None, "rng_parity": None,
            "outside_compared_fields": ["autonomous route/target decisions", "shared RNG progression", "physics", "later frontend/session transitions"]}


def expected_traffic(snapshot, actor):
    entry = snapshot["actors"][actor]
    body, dynamics, game = (bytes.fromhex(entry[name + "_hex"]) for name in ("body", "dynamics", "gameplay"))
    word = lambda raw, offset: struct.unpack_from("<I", raw, offset)[0]
    result = {"mode": game[0x81], "blocked": word(game, 0x18), "brake": word(game, 0x1c),
              "passing": word(game, 0x2c), "steering_cached": word(game, 0x34), "forced_brake": word(game, 0x48),
              "passed_actor": game[0x98], "blocker_actor": game[0x99]}
    result.update(("corridor_" + str(i), word(game, 0x70 + i * 4)) for i in range(4))
    result.update(corner_0=game[0x9f], corner_1=game[0xa0],
                  obstacle_angle_0=struct.unpack_from("<h", game, 0xf8)[0], obstacle_angle_1=struct.unpack_from("<h", game, 0xfa)[0],
                  headlight_ticks=game[0x93], headlight_on_ticks=game[0x94], headlight_off_ticks=game[0x95],
                  headlights=word(body, 0x500), reverse=word(game, 0x28), reverse_ticks=game[0x82], gear=dynamics[0x330],
                  reverse_pedal=struct.unpack_from("<i", dynamics, 0x304)[0],
                  raw_left=struct.unpack_from("<i", dynamics, 0x138)[0], raw_right=struct.unpack_from("<i", dynamics, 0x13c)[0])
    return result


def traffic_control_actors(record):
    pre = record["traffic_pre"]
    stopped = struct.unpack_from("<H", bytes.fromhex(pre["shared"]["provenance"]["world_status_hex"]), 0x12)[0]
    return [actor for actor, entry in enumerate(pre["actors"])
            if struct.unpack_from("<I", bytes.fromhex(entry["gameplay_hex"]), 4)[0]
            and (not struct.unpack_from("<I", bytes.fromhex(entry["gameplay_hex"]), 8)[0] or stopped)]


def expected_ai_controls(snapshot, actor):
    entry = snapshot["actors"][actor]
    body, dynamics, game = (bytes.fromhex(entry[name + "_hex"]) for name in ("body", "dynamics", "gameplay"))
    result = {name: struct.unpack_from("<i", dynamics, offset)[0] for name, offset in
              (("raw_left", 0x138), ("raw_right", 0x13c), ("raw_throttle", 0x140), ("raw_brake", 0x144))}
    result.update(reverse=struct.unpack_from("<I", game, 0x28)[0], reverse_ticks=game[0x82], reaction_ticks=game[0x87],
                  brake_ticks=game[0x92], gear=dynamics[0x330], reverse_pedal=struct.unpack_from("<i", dynamics, 0x304)[0],
                  mode=game[0x81], steering_cached=struct.unpack_from("<I", game, 0x34)[0],
                  obstacle_angle_0=struct.unpack_from("<h", game, 0xf8)[0], obstacle_angle_1=struct.unpack_from("<h", game, 0xfa)[0],
                  headlights=struct.unpack_from("<I", body, 0x500)[0], brake_lights=struct.unpack_from("<I", body, 0x508)[0])
    return result


def expected_traffic_world(snapshot, controls=False):
    raw = bytes.fromhex(snapshot["shared"]["traffic_control"]["status_hex"])
    return {"rng_final": snapshot["rng"], **({"steering_scratch": struct.unpack_from("<I", raw, 0x10)[0]} if controls else
                                           {"traffic_counter": struct.unpack_from("<I", raw, 0x14)[0]})}


def expected_joined_progress(snapshot, actor):
    game = bytes.fromhex(snapshot["actors"][actor]["gameplay_hex"])
    dynamics = bytes.fromhex(snapshot["actors"][actor]["dynamics_hex"])
    result = {"route_distance": struct.unpack_from("<i", game, 0xdc)[0], "distance_delta": struct.unpack_from("<i", game, 0xf4)[0]}
    result.update({name: game[offset] for name, offset in (("rank", 0x8f), ("spatial_rank", 0x90), ("display_rank", 0x8e),
                                                         ("rank_ticks", 0x91), ("ahead", 0x96), ("behind", 0x97))})
    result.update({name: struct.unpack_from("<I", game, offset)[0] for name, offset in (("contact_ahead", 0x40), ("contact_behind", 0x44), ("traffic_flag", 0x14))})
    result.update(tire_countdown=game[0x83], tire_offset_0=struct.unpack_from("<i", dynamics, 0xdc)[0])
    return result


def compare_joined_ai(header, records, native_path):
    """One observed PRE seed/world; intermediate snapshots are outputs only."""
    import csv
    from pathlib import Path
    validate_joined_ai_scope(header, records)
    traffic = compare_traffic_controls(header, records, native_path)
    traffic["input_boundary"] = "403dd0-entry"
    traffic["input_imports"] = "same derived route world/drivers/RNG; no traffic_pre or ai_post imports"
    first, failed, values = [], set(), 0
    mismatches = 0
    for suffix, phase, expected, actor_rows in ((".ai-route.csv", "traffic_pre", expected_ai_route, True),
                                                (".joined-progress.csv", "ai_post", expected_joined_progress, True),
                                                (".joined-world.csv", "ai_pre", lambda state, _: {"release_counter": state["shared"]["ai_state"]["release_counter"]}, False)):
        fields = ["id", *(["actor"] if actor_rows else []), *expected(records[0][phase], 0)]
        with Path(str(native_path) + suffix).open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames is None or len(reader.fieldnames) != len(fields) or set(reader.fieldnames) != set(fields):
                raise ValueError("Unexpected joined AI sidecar header: " + suffix)
            rows = list(reader)
        identities = [(record, actor) for record in records for actor in (range(ACTORS) if actor_rows else [None])]
        if len(rows) != len(identities):
            raise ValueError("Missing or extra complete joined AI output rows: " + suffix)
        for (record, actor), row in zip(identities, rows):
            if int(row["id"]) != record["id"] or actor_rows and int(row["actor"]) != actor:
                raise ValueError("Out-of-order joined AI update/actor rows")
            for name, original in expected(record[phase], actor).items():
                values += 1
                actual = int(row[name])
                if actual != original:
                    mismatches += 1
                    failed.add(record["id"])
                    if len(first) < 100:
                        first.append({"update": record["id"], **({"actor": actor} if actor_rows else {}),
                                      "field": suffix[1:-4] + "." + name, "original": original, "native": actual})
    events = [{"update": record["id"], "return_address": event["return_address"],
               "source_mode": event["pre"]["source_mode"], "session_stage": event["pre"]["session_stage"],
               "local_players": event["pre"]["local_players"]} for record in records for event in record["release_events"]]
    bounded = "contact_weather_manifest" in header
    decrement_updates, previous = [], header["combined_initial_release_counter"] if bounded else 0
    for index, record in enumerate(records):
        entering = previous
        for event in record["release_events"]:
            state = event["pre"]
            entering = 360 if state["local_players"] and state["session_stage"] in (2, 4) and state["source_mode"] != 2 else 0
        counter = validate_joined_release(record, previous, selected_initial=bounded and index == 0, local_session=retained_local_session_enabled(header))
        if record["update_caller_return_address"] == 0x448434 and entering > 0 and not (bounded and index == 0):
            decrement_updates.append(record["id"])
        previous = counter
    route_draws = [record["id"] for record in records if record["ai_pre"]["rng"] != record["traffic_pre"]["rng"]]
    control_draws = [record["id"] for record in records if record["ai_post"]["rng"] != record["consumed"]["rng"]]
    positive = bounded or bool(events and decrement_updates and route_draws and control_draws)
    numerical = mismatches == 0 and traffic["parity"]
    prefix = min(min(failed, default=len(records)), traffic["exact_prefix_updates"])
    return {"parity": numerical and positive, "numerical_parity": numerical, "coverage_sufficient": positive,
            "complete_source_interval": True, "requested_updates": len(records), "compared_updates": len(records),
            "exact_prefix_updates": prefix, "compared_values": values + traffic["compared_values"],
            "mismatch_count": mismatches + traffic["mismatch_count"], "first_mismatches": (first + traffic["first_mismatches"])[:100],
            "release_events": events, "release_decrement_updates": decrement_updates,
            "positive_route_control_random_coverage": bool(route_draws and control_draws),
            "positive_cold_release_coverage": bool(events and decrement_updates),
            "coverage_requirement": "complete bounded selected AI calls; new consumer positivity required by contact/weather report" if bounded else "positive release, decrement, route and control random consumers",
            "release_counter_initialization": "one actual selected421aa0 PRE counter; first wrapper already applied, later wrappers/releases derived" if bounded else "observed460170 return; derived initial0, actual448130 PRE event context and wrapper448434 only",
            "route_rng_advance_updates": route_draws, "traffic_rng_advance_updates": traffic["coverage"]["traffic_rng_change_updates"],
            "control_rng_advance_updates": control_draws, "traffic_controls": traffic,
            "route_coverage": ai_route_coverage(header, records), "input_boundary": "403dd0-entry", "comparison_boundary": "415090-entry",
            "world_initializations": len(records), "rng_initializations": len(records), "intermediate_world_imports": 0, "intermediate_rng_imports": 0,
            "input_imports": "one observed403dd0 PRE world/RNG perupdate; same drivers derive route, cumulative distance, rank/ring/contact/gaps, traffic and descending AI controls",
            "physical_parity": None, "continuous_process_rng_parity": None, "autonomous_race_parity": None,
            "outside_compared_fields": ["initialization RNG before first403dd0", "human keyboard mapping", "physics", "ground/effect/frame RNG", "pit and finish/lap side effects"]}


def compare_traffic_controls(header, records, native_path):
    """Every measured traffic/control row, from the actual post-route boundary."""
    import csv
    from pathlib import Path
    validate_traffic_control_scope(header, records)
    mismatches, values, failed, unclaimed = [], 0, set(), 0
    schemas = [(".traffic.csv", "ai_post", lambda record: range(ACTORS), expected_traffic),
               (".traffic-world.csv", "ai_post", lambda record: [None], lambda state, _: expected_traffic_world(state)),
               (".ai-controls.csv", "consumed", traffic_control_actors, expected_ai_controls),
               (".ai-control-world.csv", "consumed", lambda record: [None], lambda state, _: expected_traffic_world(state, True))]
    row_counts = {}
    for suffix, phase, actors, expected in schemas:
        identities = [(record, actor) for record in records for actor in actors(record)]
        has_actor = suffix in (".traffic.csv", ".ai-controls.csv")
        fields = list(expected(records[0][phase], 0 if has_actor else None))
        with Path(str(native_path) + suffix).open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ["id", *(["actor"] if has_actor else []), *fields]:
                raise ValueError("Unexpected traffic/control sidecar header: " + suffix)
            rows = list(reader)
        if len(rows) != len(identities):
            raise ValueError("Missing or extra measured traffic/control rows: " + suffix)
        row_counts[suffix] = len(rows)
        for (record, actor), row in zip(identities, rows):
            if int(row["id"]) != record["id"] or has_actor and int(row["actor"]) != actor:
                raise ValueError("Out-of-order traffic/control update or actor")
            for name, original in expected(record[phase], actor).items():
                if (suffix == ".traffic.csv" and name in ("raw_left", "raw_right")
                        and local_state13_wrapper_axes_unclaimed(header, record[phase], actor)):
                    unclaimed += 1
                    continue
                values += 1
                actual = int(row[name])
                if actual != original:
                    failed.add(record["id"])
                    mismatches.append({"update": record["id"], **({"actor": actor} if has_actor else {}),
                                       "field": suffix[1:-4] + "." + name, "original": original, "native": actual})
    coverage = {"traffic_change_events": [], "mode_actor_updates": {}, "blocked_actor_updates": 0, "brake_actor_updates": 0,
                "passing_actor_updates": 0, "forced_brake_actor_updates": 0, "headlight_changes": 0,
                "nonzero_axes": {"raw_left": 0, "raw_right": 0, "raw_throttle": 0, "raw_brake": 0},
                "eligible_control_actor_updates": 0, "traffic_rng_change_updates": [], "control_rng_change_updates": []}
    for record in records:
        eligible = traffic_control_actors(record)
        for actor in range(ACTORS):
            old, new = (expected_traffic(record[phase], actor) for phase in ("traffic_pre", "ai_post"))
            mode = str(new["mode"])
            coverage["mode_actor_updates"][mode] = coverage["mode_actor_updates"].get(mode, 0) + 1
            fields = [name for name in ("mode", "blocked", "brake", "passing", "forced_brake") if old[name] != new[name]]
            if fields:
                coverage["traffic_change_events"].append({"update": record["id"], "actor": actor, "fields": fields})
            coverage["headlight_changes"] += old["headlights"] != new["headlights"]
            for name in ("blocked", "brake", "passing", "forced_brake"):
                coverage[name + "_actor_updates"] += bool(new[name])
        for actor in eligible:
            axes = expected_ai_controls(record["consumed"], actor)
            coverage["eligible_control_actor_updates"] += 1
            for name in coverage["nonzero_axes"]:
                coverage["nonzero_axes"][name] += axes[name] != 0
        for name, first, last in (("traffic_rng_change_updates", "traffic_pre", "ai_post"), ("control_rng_change_updates", "ai_post", "consumed")):
            if record[first]["rng"] != record[last]["rng"]:
                coverage[name].append(record["id"])
    parity = bool(records) and not mismatches
    return {"parity": parity, "complete_source_interval": True, "requested_updates": len(records), "compared_updates": len(records),
            "exact_updates": len(records) - len(failed), "exact_prefix_updates": min(failed, default=len(records)),
            "compared_values": values, "row_counts": row_counts, "mismatch_count": len(mismatches), "first_mismatches": mismatches[:100],
            "coverage": coverage, "original_traffic_event_parity": parity if coverage["traffic_change_events"] else None,
            **local_state13_axes_scope(header, unclaimed, "403dd0-return", ("raw_left", "raw_right")),
            "original_positive_axes_parity": parity if any(coverage["nonzero_axes"].values()) else None,
            "input_boundary": "404c1a-entry", "traffic_output_boundary": "403dd0-return", "control_output_boundary": "415090-entry",
            "execution_order": "all stopping distances ascending, all traffic decisions ascending, all brakes ascending, reverse recovery, AI controls descending",
            "input_imports": "one observed post-route world per update; later traffic/control outputs never imported",
            "rng_parity": not any(item["field"].endswith(".rng_final") for item in mismatches) if records else None,
            "physical_parity": None, "autonomous_route_target_parity": None, "autonomous_race_parity": None,
            "outside_compared_fields": ["upstream autonomous route/target/rank decisions", "human input mapping", "physics", "audio"]}
