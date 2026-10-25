"""Physical-boundary primary frames; camera and scene-call observations are outputs."""

import json
import re
import struct
import zlib

PRIMARY = 0x44a370
PHYSICAL = 0x421aa0
INPUT_POLL = 0x44a1d0
MAPPER = 0x422f60
CAMERA_PROFILE = "ordinary-views"
CAMERA_INPUT_MANIFEST = {"version": 1, "poll_entry": INPUT_POLL, "mapper_entry": MAPPER,
    "actions": ["cycle_held", "rear_held"], "views": [1, 2, 3, 4, 5]}

def camera_keys(physical_id):
    cycle = any(start <= physical_id < start + 2 for start in (8, 18, 28, 40, 50, 62, 72, 80, 88, 92))
    rear = 38 <= physical_id < 44 or 60 <= physical_id < 66
    return {"cycle_held": cycle, "rear_held": rear}

def validate_camera_input(value, physical_id=None):
    if not isinstance(value, dict) or set(value) != {"physical_id", "cycle_held", "rear_held"}:
        raise ValueError("Camera inputs contain requested keys only")
    integer(value["physical_id"], 0, 0x7fffffff)
    if physical_id is not None and value["physical_id"] != physical_id: raise ValueError("Camera input physical ID changed")
    if any(type(value[name]) is not bool for name in ("cycle_held", "rear_held")):
        raise ValueError("Camera input keys must be booleans")
SAMPLE = 0x412ab0
SAMPLE_RETURN = 0x44a516
BODY = 0x4cd760
STRIDE = 0x53c
CLOCK = {"divisor": 0x4be148, "delta_q16": 0x4ad080,
         "reciprocal_q16": 0x4ad084, "extra_update": 0x4be19c}


def encode(value):
    return (json.dumps(value, separators=(",", ":"), allow_nan=False) + "\n").encode()


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError("Invalid scene frame integer")
    return value


def vector(value, count):
    if not isinstance(value, list) or len(value) != count:
        raise ValueError("Invalid scene frame vector")
    for component in value: integer(component, -0x80000000, 0x7fffffff)


def validate(frame):
    integer(frame.get("order"), 0, 0x7fffffff)
    inputs, expected = frame.get("input"), frame.get("expected")
    if not isinstance(inputs, dict) or set(inputs) != {"physical", "qpc_counter"} or not isinstance(expected, dict):
        raise ValueError("Scene frames accept physical bodies and external counter only")
    integer(inputs["qpc_counter"], 0, 0xffffffff)
    physical = inputs["physical"]
    if not isinstance(physical, list) or len(physical) not in (1, 2):
        raise ValueError("Scene frame requires one or two complete physical calls")
    for update in physical:
        if set(update) != {"session_state", "pre_body", "post_bodies"} or update["session_state"] not in (6, 9):
            raise ValueError("Unsupported scene frame physical scope")
        if set(update["pre_body"]) != {"position_q16", "basis_q16"}:
            raise ValueError("Scene pre-body cannot import camera state")
        vector(update["pre_body"]["position_q16"], 3)
        vector(update["pre_body"]["basis_q16"], 9)
        if len(update["post_bodies"]) != 16:
            raise ValueError("Scene frame requires sixteen physical body outputs")
        for body in update["post_bodies"]:
            if set(body) != {"position_q16", "mode"}:
                raise ValueError("Scene body cannot import scene state")
            vector(body["position_q16"], 3)
            integer(body["mode"], 0, 1)
    for key in ("select_order", "car_lod_order", "publish_order"):
        integer(expected.get(key), 0, 0x7fffffff)
    if not expected["select_order"] < expected["car_lod_order"] < expected["publish_order"]:
        raise ValueError("Reordered scene frame publication")
    if "camera_transactions" in expected:
        transactions = expected["camera_transactions"]
        if len(transactions) != len(physical): raise ValueError("Camera transactions omit a physical call")
        for transaction in transactions:
            validate_camera_input(transaction.get("input"))
            observed = transaction.get("expected", {})
            if set(observed) != {"cycle_pressed", "actions", "pre", "post"}:
                raise ValueError("Incomplete actual camera mapper checkpoints")
            if observed["actions"] != {"cycle_pressed": observed["cycle_pressed"], "rear_held": transaction["input"]["rear_held"]}:
                raise ValueError("Camera requested keys and observed mapper actions disagree")
        control = expected.get("control", {})
        if set(control) != {"selected_view", "projection_view", "rear_held", "ancestor_root"} or control["selected_view"] not in range(1, 6) or control["projection_view"] not in range(1, 5):
            raise ValueError("Incomplete camera publication control expectations")
        integer(control["rear_held"], 0, 1)
        integer(control["ancestor_root"], 0, 17)
    next_clock = expected.get("next_clock", {})
    if set(next_clock) != set(CLOCK): raise ValueError("Incomplete next scene clock expectation")
    integer(next_clock["divisor"], 1, 0x7fffffff)
    integer(next_clock["delta_q16"], 1, 0x30000)
    integer(next_clock["reciprocal_q16"], 1, 0x7fffffff)
    integer(next_clock["extra_update"], 0, 1)
    timing = expected.get("timing", {})
    if set(timing) != {"delta_q16", "reciprocal_q16", "extra_update", "physical_count"}:
        raise ValueError("Incomplete scene frame timing expectations")
    integer(timing["delta_q16"], 1, 0x30000)
    integer(timing["reciprocal_q16"], 1, 0x7fffffff)
    integer(timing["extra_update"], 0, 1)
    if timing["physical_count"] != len(physical) or len(physical) != 1 + timing["extra_update"]:
        raise ValueError("Scene physical calls disagree with retained extra permit")


def coverage(frames):
    return {"frames": len(frames), "publications": len(frames),
            "extra_frames": sum(len(frame["input"]["physical"]) == 2 for frame in frames),
            "passed": len(frames) >= 3 and any(len(frame["input"]["physical"]) == 2 for frame in frames)}


class Writer:
    def __init__(self, path, checksum, capture_id=None, camera_controls=False):
        self.capture_id = capture_id
        self.camera_controls = camera_controls
        self.binding = None
        self.output = path.open("wb")
        self.checksum = checksum
        self.crc = 0
        self.frames = []
        self.header = None
        self.closed = False

    def write(self, record):
        raw = encode(record)
        self.output.write(raw)
        self.output.flush()
        self.crc = zlib.crc32(raw, self.crc)

    def start(self, setup, initial):
        if self.header is not None: raise ValueError("Repeated scene frame initialization")
        self.header = {"type": "header", "schema": 1, "contract": "single-player-normal-view2-physical-frames",
                       "exe_crc32": self.checksum, "setup": setup, "initial_clock": initial}
        if self.capture_id is not None: self.header["capture_id"] = self.capture_id
        if self.camera_controls:
            self.header["camera_controls_manifest"] = CAMERA_INPUT_MANIFEST
            self.header["contract"] = "single-player-ordinary-views-physical-frames"
        self.write(self.header)

    def append(self, frame):
        validate(frame)
        if self.header is None or self.closed or frame["order"] != len(self.frames):
            raise ValueError("Uninitialized or unordered scene frame")
        self.write({"type": "frame", **frame})
        self.frames.append(frame)

    def finish(self, error=None, pending=False):
        if self.closed: return
        if self.header is None:
            self.start(None, None)
            error = error or "source stopped before first physical frame"
        aborted = error is not None or pending
        footer = {"type": "aborted" if aborted else "complete", "frames": len(self.frames),
                  "prefix_crc32": f"{self.crc:08x}", "coverage": coverage(self.frames), "incomplete_frames": int(pending)}
        if self.binding is not None: footer["fleet_source"] = self.binding
        if aborted: footer["reason"] = str(error or "source stopped inside a primary frame")
        self.output.write(encode(footer))
        self.output.close()
        self.closed = True


def read_frames(path, journal_header, events):
    crc, header, footer, frames = 0, None, None, []
    with path.open("rb") as source:
        for raw in source:
            if footer is not None or not raw.endswith(b"\n"): raise ValueError("Incomplete/trailing scene frame record")
            record = json.loads(raw, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
            if record.get("type") in ("complete", "aborted"):
                footer = record
                if footer["prefix_crc32"] != f"{crc:08x}" or footer["frames"] != len(frames):
                    raise ValueError("Scene frame CRC/count mismatch")
                continue
            if header is None:
                if record.get("type") != "header" or record.get("schema") != 1:
                    raise ValueError("Unsupported scene frame header")
                header = record
            else:
                if record.get("type") != "frame" or record.get("order") != len(frames):
                    raise ValueError("Unordered scene frame record")
                validate(record)
                frames.append(record)
            crc = zlib.crc32(raw, crc)
    if header is None or footer is None or footer["type"] != "complete" or footer["incomplete_frames"]:
        raise ValueError("Scene frames are not complete")
    if header.get("exe_crc32") != "8ce56d41" or header["exe_crc32"] != journal_header.get("exe_crc32"):
        raise ValueError("Scene frame provenance disagrees with scene journal")
    camera_controls = header.get("camera_controls_manifest")
    if camera_controls is not None and camera_controls != CAMERA_INPUT_MANIFEST:
        raise ValueError("Unsupported camera control manifest")
    if bool(camera_controls) != (journal_header.get("contract") == "single-player-ordinary-views-scene-boundary"):
        raise ValueError("Scene/frame camera profiles disagree")
    initial = events[0]["input"]
    setup = {name: initial[name] for name in ("track_id", "car_id", "weather", "graphics_detail", "local_actor")}
    if header.get("setup") != setup or set(header.get("initial_clock", {})) != set(CLOCK):
        raise ValueError("Scene frame setup/clock context disagrees")
    clock = header["initial_clock"]
    integer(clock["divisor"], 1, 0x7fffffff)
    if (clock["delta_q16"], clock["reciprocal_q16"], clock["extra_update"]) != (65536, 65536, 0):
        raise ValueError("Scene frames require canonical initial retained step")
    previous = -1
    for frame in frames:
        expected = frame["expected"]
        if bool(camera_controls) != ("camera_transactions" in expected):
            raise ValueError("Camera frame lacks its declared control transactions")
        if expected["next_clock"]["divisor"] != clock["divisor"]:
            raise ValueError("Scene frame stored clock divisor changed")
        for key, kind in (("select_order", "select"), ("car_lod_order", "car_lod"), ("publish_order", "publish")):
            order = expected[key]
            if order >= len(events) or order <= previous or events[order]["kind"] != kind:
                raise ValueError("Missing/reordered scene frame expectation link")
            previous = order
    if {frame["expected"]["publish_order"] for frame in frames} != {event["order"] for event in events if event["kind"] == "publish"}:
        raise ValueError("Scene frames omit an observed publication")
    for frame in frames:
        models = events[frame["expected"]["car_lod_order"]]["input"]
        bodies = frame["input"]["physical"][0]["post_bodies"]
        if models["body_positions_q16"] != [body["position_q16"] for body in bodies] or models["body_modes"] != [body["mode"] for body in bodies]:
            raise ValueError("Scene frame bodies disagree with actual model consumer")
    if footer["coverage"] != coverage(frames): raise ValueError("Scene frame coverage mismatch")
    return header, frames, footer


def write_frames_binary(path, header, frames, events):
    if header.get("camera_controls_manifest") is not None:
        raise ValueError("Ordinary camera controls require autonomous fleet replay; no body/root input transport")
    setup, clock = header["setup"], header["initial_clock"]
    data = bytearray(b"TOCASFR1")
    data += struct.pack("<6I3iI", 1, *(setup[name] for name in ("track_id", "car_id", "weather", "graphics_detail", "local_actor")),
                        clock["divisor"], clock["delta_q16"], clock["reciprocal_q16"], clock["extra_update"])
    roots = events[0]["input"]["roots"]
    data += struct.pack("<I", len(roots))
    for root in roots:
        data += struct.pack("<3I3f", {"car_parent": 1, "follow_anchor": 2}[root["role"]], root.get("actor", 0xffffffff), root["flags"], *root["local_position"])
    data += struct.pack("<I", len(frames))
    for frame in frames:
        validate(frame)
        physical = frame["input"]["physical"]
        data += struct.pack("<2I", len(physical), frame["input"]["qpc_counter"])
        for update in physical:
            data += struct.pack("<I12i", update["session_state"], *update["pre_body"]["position_q16"], *update["pre_body"]["basis_q16"])
            for body in update["post_bodies"]:
                data += struct.pack("<3iI", *body["position_q16"], body["mode"])
    path.write_bytes(data)
    return {"bytes": len(data), "crc32": f"{zlib.crc32(data):08x}"}


def expected_frame_rows(frames, events):
    car_roots = {root["actor"]: root["id"] for root in events[0]["input"]["roots"] if root["role"] == "car_parent"}
    def bits(value): return struct.unpack("<I", struct.pack("<f", value))[0]
    for frame in frames:
        order, expected = frame["order"], frame["expected"]
        for name in ("delta_q16", "reciprocal_q16", "extra_update", "physical_count"):
            yield order, "timing", 0, name, expected["timing"][name]
        if "control" in expected:
            for name in ("selected_view", "projection_view", "rear_held", "ancestor_root"):
                yield order, "control", 0, name, expected["control"][name]
        selection = events[expected["select_order"]]
        for name, field in (("camera_world", "world_bits"), ("world_to_view", "view_bits"), ("world_position", "position_bits")):
            for index, value in enumerate(selection["input"][name]): yield order, "camera", index, field, bits(value)
        for index, value in enumerate(selection["input"]["projection_q16"]): yield order, "projection", index, "q16", value
        selected = selection["expected"]
        yield order, "pvs", 0, "count", len(selected["ordered_sectors"])
        for index, value in enumerate(selected["ordered_sectors"]): yield order, "pvs", index, "order", value
        for index, value in enumerate(selected["detail"]): yield order, "pvs", index, "detail", value
        for index, value in enumerate(bytes.fromhex(selected["selected_bits_hex"])): yield order, "pvs", index, "bits", value
        for group, source in (("models", events[expected["car_lod_order"]]["expected"]), ("published_models", events[expected["publish_order"]]["expected"])):
            yield order, group, 0, "refresh_request", source["retained_refresh_request"]
            for actor, car in enumerate(source["cars"]):
                for name, value in zip(("high_manual", "medium_manual", "low_manual"), car["lod_manual_masks"]): yield order, group, actor, name, value & 1
                for name, field in (("car_mask", "source_car_mask"), ("high_medium_mask", "source_high_medium_mask"), ("full_body_mask", "source_full_body_mask")):
                    yield order, group, actor, name, car[field] & 1
        for actor, car in enumerate(events[expected["publish_order"]]["expected"]["cars"]):
            root, physical = car_roots[actor], car["physical"]
            yield order, "root", root, "sector", physical["registration"]["sector"]
            for index, value in enumerate(physical["registration"]["neighbors"]): yield order, "root", root, f"neighbor_{index}", value
            for name in ("render_mask", "manual_mask"): yield order, "root", root, name, physical[name] & 1
        for name in ("delta_q16", "reciprocal_q16", "extra_update"):
            yield order, "next_timing", 0, name, expected["next_clock"][name]


def expected_autonomous_rows(frames, events):
    """Authored roots are compared before cold grid migrations overwrite them."""
    initial = events[0]
    for root, expected in zip(initial["input"]["roots"], initial["expected"]["roots"]):
        for axis, value in zip(("x", "y", "z"), root["local_position"]):
            bits = struct.unpack("<I", struct.pack("<f", value))[0]
            yield 0, "initial_roots", root["id"], f"position_{axis}_bits", bits
        yield 0, "initial_roots", root["id"], "sector", expected["registration"]["sector"]
    yield from expected_frame_rows(frames, events)


def expected_lifecycle_consumer_rows(records, combined_events):
    """Recorded consumer checkpoints only; no missing camera/frame state is inferred."""
    for record in records:
        wheel = 0
        for kind, phase, event in combined_events(record):
            order = record["id"]
            if kind == "wheel":
                masks = bytes.fromhex(event["pre"]["visibility_hex"])
                if len(masks) != 16: raise ValueError("Incomplete source wheel scene masks")
                for actor, mask in enumerate(masks):
                    yield order, "wheel_full", wheel * 16 + actor, "mask", mask & 1
                wheel += 1
            elif kind == "boundary" and phase == "pre":
                for actor, car in enumerate(event["actors"]):
                    root = car["root"]
                    raw = bytes.fromhex(root["registration_hex"])
                    if len(raw) != 0x24: raise ValueError("Incomplete source scene root registration")
                    yield order, "pre_root", actor, "sector", root["sector"]
                    for index, neighbor in enumerate(root["neighbors"]):
                        yield order, "pre_root", actor, f"neighbor_{index}", neighbor
                    yield order, "pre_root", actor, "render_mask", raw[0x18] & 1
                    yield order, "pre_root", actor, "manual_mask", raw[0x20] & 1
                    yield order, "pre_root", actor, "effect_mask", bytes.fromhex(car["effect_root"]["flags_hex"])[0] & 1
                    yield order, "pre_root", actor, "sector_visible", int(root["sector_visible"])
                shared = event["shared"]
                bitmap = bytes.fromhex(shared["sector_visibility_hex"])
                if len(bitmap) != 256: raise ValueError("Incomplete source scene PVS bitmap")
                for index, value in enumerate(bitmap):
                    yield order, "pre_pvs", index, "bits", value
                yield order, "pre_scene", 0, "selected_count", shared["sector_visibility_disabled"]
                yield order, "pre_scene", 0, "all_sectors", int(bool(shared["sector_visibility_override"]))


def qualifying_consumer_inputs(header, records, warmup_path):
    """A warm physical boundary with observed scheduling, never restored scene state."""
    if (header.get("exe_crc32") != "8ce56d41" or header.get("schema") != 2
            or header.get("session") != "single-race" or header.get("source_mode") != 0
            or header.get("qualifying") is not True or header.get("actor_count") != 16
            or not records or [r.get("id") for r in records] != list(range(len(records)))):
        raise ValueError("Qualifying scene consumers require one complete QualOn fleet recording")
    config = bytes.fromhex(header["configuration_hex"])
    if (len(config) != 0x620 or any(struct.unpack_from("<I", config, at)[0] != value
            for at, value in ((4, 0), (0x20, 0), (0xac, 1), (0x48, 0), (0x7c, 0), (0x88, 0)))):
        raise ValueError("Qualifying scene consumers exclude alternate camera controllers")
    local = integer(header["local_actor"], 0, 15)
    setup = {name: header["initial_configuration"][name] for name in ("track_id", "car_id", "weather")}
    setup.update(graphics_detail=struct.unpack_from("<I", config, 0x328)[0], local_actor=local)
    for key, high in (("track_id", 9), ("car_id", 7), ("weather", 5), ("graphics_detail", 3)):
        integer(setup[key], int(key == "weather"), high)
    warmup = header.get("unrecorded_keyboard_warmup", {}).get("targeted_local_finish", {})
    raw = warmup_path.read_bytes()
    if (warmup.get("decision_log") != warmup_path.name or warmup.get("decision_log_crc32") != f"{zlib.crc32(raw):08x}"
            or warmup.get("initial_update_counter") != 0 or warmup.get("counter_resets") != [] or warmup.get("target_lap") != 3):
        raise ValueError("Qualifying authored nodes require bound cold approach/drive provenance")
    history = [json.loads(line) for line in raw.splitlines()]
    if (not raw.endswith(b"\n") or len(history) != warmup.get("updates") or not history
            or history[0].get("decision", {}).get("session_state") != 4):
        raise ValueError("Qualifying camera history must begin at the cold state4 approach")
    for index, observed in enumerate(history):
        if (observed.get("update") != index or observed.get("counter") != index
                or observed.get("decision", {}).get("session_state") not in (4, 6)
                or not isinstance(observed.get("requested_keys"), list)
                or set(observed["requested_keys"]) - {"Up", "Down", "Left", "Right"}):
            raise ValueError("Qualifying authored nodes were not pristine throughout the bound approach/drive")
    first = records[0]["pre"]
    endpoint = {name: first[name] for name in ("session_state", "update_counter", "race_clock")}
    if (first["session_state"] != 6 or first["update_counter"] != len(history)
            or any(warmup.get("endpoint", {}).get(name) != value for name, value in endpoint.items())):
        raise ValueError("Qualifying warmup provenance belongs to a different physical interval")
    start = next((index for index, record in enumerate(records) if record["pre"]["session_state"] == 7), None)
    if start is None or start == 0 or any(r["pre"]["session_state"] != 6 for r in records[:start]):
        raise ValueError("Qualifying scene boundary requires the recorded first state7 entry")
    calls = records[start - 1].get("local_handoffs", [])
    if len(calls) != 1 or (calls[0].get("kind"), calls[0].get("actor"), calls[0].get("return_address")) != (0, local, 0x4217aa):
        raise ValueError("Qualifying scene boundary lacks the actual local qualifying finish")

    def pose(body):
        return [*struct.unpack_from("<3i", body, 0xbc), *struct.unpack_from("<9i", body, 0x150)]

    selected, frame_pass, frame_step, frame_state = [], None, None, None
    first_authored = None
    for record in records:
        for snapshot in (value for value in record.values() if isinstance(value, dict) and "actors" in value):
            lifecycle = snapshot["shared"]["lifecycle"]
            if (struct.unpack_from("<H", bytes.fromhex(lifecycle["player_lifecycle_hex"]), 2)[0] != 2
                    or struct.unpack_from("<I", bytes.fromhex(snapshot["shared"]["provenance"]["world_status_hex"]), 0x18)[0]):
                raise ValueError("Qualifying scene boundary requires ordinary View2 without watch mode")
        if record["id"] < start: continue
        pre, post = record["pre"], record["post"]
        lifecycle = pre["shared"]["lifecycle"]
        pass_id = struct.unpack_from("<I", bytes.fromhex(lifecycle["race_lifecycle_hex"]), 16)[0]
        integer(pass_id, 0, 1)
        if any(struct.unpack_from("<I", bytes.fromhex(snapshot["shared"]["lifecycle"]["race_lifecycle_hex"]), 16)[0] != pass_id
                for snapshot in (value for value in record.values() if isinstance(value, dict) and "actors" in value)):
            raise ValueError("Actual qualifying current-pass marker changed inside physics")
        step = {name: pre[name] for name in ("delta_q16", "reciprocal_q16", "extra_update")}
        integer(step["delta_q16"], 1, 0x30000)
        integer(step["reciprocal_q16"], 1, 0x7fffffff)
        if type(step["extra_update"]) is not bool: raise ValueError("Invalid observed qualifying extra permit")
        state = pre["session_state"]
        if state not in (7, 8) or (frame_state == 8 and state != 8):
            raise ValueError("Qualifying scene boundary covers ordered states7 then8 only")
        if pass_id == 0:
            if frame_pass == 0 and frame_step["extra_update"]:
                raise ValueError("Qualifying scene frame omitted its actual extra physical call")
            frame_step, frame_state = step, state
        elif frame_pass != 0 or not frame_step["extra_update"] or step != frame_step or state != frame_state:
            raise ValueError("Qualifying extra call lacks its recorded primary/retained permit")
        frame_pass = pass_id
        origin = struct.unpack_from("<I", bytes.fromhex(lifecycle["session_timers_hex"]))[0]
        clock = pre["race_clock"]
        timer = ((((clock - origin) * 16) & 0xffffffff) // 1000) << 12 if origin < clock else 0
        integer(timer, 0, 0x7fffffff)
        if record["id"] == start and (pass_id != 0 or state != 7 or origin != clock):
            raise ValueError("Qualifying scene boundary must begin at the actual state7 timer reset")
        if pass_id == 0 and state == 7 and timer >= 0x20000 and first_authored is None:
            first_authored = record["id"]
        bodies = [bytes.fromhex(actor["body_hex"]) for actor in post["actors"]]
        modes = [struct.unpack_from("<I", body, 0x4f0)[0] for body in bodies]
        if any(mode not in (0, 1) if actor % 4 == local % 4 else mode != 4 for actor, mode in enumerate(modes)):
            raise ValueError("Qualifying physical boundary must preserve the typed active quarter")
        before = bytes.fromhex(pre["actors"][local]["body_hex"])
        after = bodies[local]
        selected.append({"source_id": record["id"], "step": step, "state": state, "timer0_q16": timer,
            "current_pass": pass_id, "before": pose(before), "after": pose(after),
            "previous_velocity": list(struct.unpack_from("<3i", before, 0xc8)),
            "velocity": list(struct.unpack_from("<3i", after, 0xc8)),
            "angular_velocity_y": struct.unpack_from("<i", after, 0xe8)[0],
            "positions": [list(struct.unpack_from("<3i", body, 0xbc)) for body in bodies], "modes": modes})
    if (not selected or frame_pass == 0 and frame_step["extra_update"] or first_authored is None
            or not any(value["state"] == 8 and value["current_pass"] == 0 for value in selected)):
        raise ValueError("Qualifying scene boundary requires complete frames and positive authored7/8 coverage")
    compare_begin = first_authored + 1
    if not any(value["source_id"] >= compare_begin and value["state"] == 8 for value in selected):
        raise ValueError("Qualifying scene suffix omits the state8 consumer")
    for value in selected: value["compare"] = value["source_id"] >= compare_begin
    return {"setup": setup, "start_state": 7,
        "initial_bodies": [pose(bytes.fromhex(actor["body_hex"])) for actor in records[start]["pre"]["actors"]],
        "physical": selected, "compare_begin": compare_begin,
        "scope": {"source_begin": start, "source_end": records[-1]["id"], "compare_begin": compare_begin,
            "physical_updates": len(selected), "compared_physical_consumers": sum(value["compare"] for value in selected),
            "warmup_crc32": f"{zlib.crc32(raw):08x}", "pristine_authored_history": True,
            "physical_states": "explicit observed PRE/POST poses and modes", "timing": "observed TimeStep, raw4d3848 current-pass and lifecycle timer; no QPC stream",
            "initial_ordinary_camera_history": "unclaimed; comparison begins after generated authored7 publication",
            "camera_matrix_parity": None, "ordered_pvs_parity": None, "physics_autonomy": False,
            "clock_autonomy": False, "qualifying_to_race_reset_source_parity": None}}


def write_qualifying_frames_binary(path, profile):
    """TOCASFR1/v2 carries physical/timing boundaries, with canonical scene roles."""
    setup, physical = profile["setup"], profile["physical"]
    data = bytearray(b"TOCASFR1")
    data += struct.pack("<8I", 2, *(setup[name] for name in ("track_id", "car_id", "weather", "graphics_detail", "local_actor")), profile["start_state"], len(physical))
    for pose in profile["initial_bodies"]: data += struct.pack("<12i", *pose)
    for value in physical:
        step = value["step"]
        data += struct.pack("<IiiIIiII", value["source_id"], step["delta_q16"], step["reciprocal_q16"], step["extra_update"],
            value["state"], value["timer0_q16"], value["current_pass"], value["compare"])
        data += struct.pack("<31i", *value["before"], *value["after"], *value["previous_velocity"], *value["velocity"], value["angular_velocity_y"])
        for position, mode in zip(value["positions"], value["modes"]): data += struct.pack("<3iI", *position, mode)
    path.write_bytes(data)
    return {"bytes": len(data), "crc32": f"{zlib.crc32(data):08x}", "version": 2}


def expected_qualifying_consumer_rows(records, profile):
    selected = [record for record in records if record["id"] >= profile["compare_begin"]]
    for record in selected:
        yield from expected_lifecycle_consumer_rows([record], lambda r: [("boundary", "pre", r["pre"])])
        authored, view = struct.unpack_from("<2H", bytes.fromhex(record["pre"]["shared"]["lifecycle"]["player_lifecycle_hex"]))
        yield record["id"], "pre_camera", 0, "authored_index", authored
        yield record["id"], "pre_camera", 0, "ordinary_view", view


class Recorder:
    """Nested read-only hooks; physical stops remain visible to the fleet recorder."""
    def __init__(self, journal, path, checksum, capture_id=None, camera_controls=False):
        self.journal, self.debugger = journal, journal.debugger
        self.writer = Writer(path, checksum, capture_id, camera_controls)
        self.points = {pc: self.debugger.breakpoint(pc) for pc in (PRIMARY, SAMPLE, PHYSICAL)}
        self.camera_controls = camera_controls
        self.camera_injector = None
        self.camera_transactions = {}
        self.last_cycle_held = False
        self.mapper_return = None
        if camera_controls:
            self.points.update({pc: self.debugger.breakpoint(pc) for pc in (INPUT_POLL, MAPPER)})
        self.primary = None
        self.physical = None
        self.sample_return = None
        self.primary_return = None
        self.physical_return = None
        self.physical_id = 0
        self.clock_events = []
        self.combined_order = None
        self.combined_clock = None

    def configure_camera_inputs(self, injector):
        if not self.camera_controls: raise ValueError("Camera injector requires the ordinary-view profile")
        self.camera_injector = injector

    def camera_state(self):
        view = self.journal.values("H", 0x4c3a66)[0]
        rear = self.journal.values("H", 0x4d3900)[0]
        saved = self.journal.values("H", 0x4d3918)[0]
        return {"selected_view": view, "projection_view": saved if rear else view, "rear_held": int(bool(rear)), "saved_view": saved}

    def mapped_camera_actions(self):
        d = self.debugger
        binding, primary, secondary = (d.word(address) for address in (0x4d38e0, 0x4d3890, 0x4d38c0))
        if not binding or not primary: raise ValueError("Camera mapper lacks its actual input binding")
        def action(index, pressed):
            offset = 8 if pressed else 4
            value = d.word(primary + offset) & d.word(binding + 8 + index * 4)
            if secondary: value |= d.word(secondary + offset) & d.word(binding + 0x3c + index * 4)
            return bool(value)
        return {"cycle_pressed": action(7, True), "rear_held": action(6, False)}

    def camera_transaction(self, physical_id):
        value = self.camera_transactions.get(physical_id)
        if value is None or "post" not in value["expected"]: raise ValueError("Incomplete camera mapper checkpoint")
        return value

    def configure_combined(self, order, clock):
        self.combined_order, self.combined_clock = order, clock

    def drain_clock_events(self):
        events, self.clock_events = self.clock_events, []
        return events

    def clock(self):
        return {name: self.journal.values("i" if name != "extra_update" else "I", address)[0] for name, address in CLOCK.items()}

    def stack_return(self):
        address = int(re.search(r"0x([0-9a-fA-F]+)", self.debugger.command("print $esp")).group(1), 16)
        returned = self.journal.values("I", address)[0]
        if not 0x400000 <= returned < 0x4b0000: raise ValueError("Invalid scene frame source return")
        return returned

    def observe(self, kind, order):
        if self.primary is not None and kind in ("select", "car_lod", "publish"):
            key = {"select": "select_order", "car_lod": "car_lod_order", "publish": "publish_order"}[kind]
            if key in self.primary["expected"]: raise ValueError("Repeated scene publication in primary frame")
            if self.physical is not None or len(self.primary["input"]["physical"]) != 1 or "qpc_counter" in self.primary["input"]:
                raise ValueError("Scene publication must follow primary physics before an extra call")
            self.primary["expected"][key] = order
            if kind == "select" and self.camera_controls:
                state = self.camera_state()
                state.pop("saved_view")
                self.primary["expected"]["control"] = state

    def intercept(self, response):
        pcs = (PRIMARY, SAMPLE, PHYSICAL, self.primary_return, self.physical_return, self.sample_return,
               *([INPUT_POLL, MAPPER, self.mapper_return] if self.camera_controls else []))
        matches = {pc for pc in pcs if pc is not None and f"{pc:08x}" in response.lower()}
        if not matches: return None
        if len(matches) != 1: raise ValueError("Ambiguous scene frame hook")
        pc, d = matches.pop(), self.debugger
        d.confirm_pause()
        if pc == INPUT_POLL:
            if not self.journal.initialized: return True
            if self.camera_injector is None or self.physical is not None: raise ValueError("Camera poll lacks a valid external injector")
            requested = {"physical_id": self.physical_id, **self.camera_injector(self.physical_id)}
            validate_camera_input(requested, self.physical_id)
            if self.physical_id in self.camera_transactions: raise ValueError("Repeated camera input poll")
            self.camera_transactions[self.physical_id] = {"input": requested,
                "expected": {"cycle_pressed": requested["cycle_held"] and not self.last_cycle_held}}
            self.last_cycle_held = requested["cycle_held"]
            return True
        if pc == MAPPER:
            if not self.journal.initialized or self.physical is None: return True
            if self.mapper_return is not None: raise ValueError("Nested local camera mapper")
            stack = int(re.search(r"0x([0-9a-fA-F]+)", d.command("print $esp")).group(1), 16)
            _, dynamics, player = self.journal.values("3I", stack)
            local_actor = self.journal.values("B", 0x4d37a0)[0]
            if player != 0 or dynamics != 0x4c5da0 + local_actor * 0x338:
                raise ValueError("Camera mapper is not the ordinary local player")
            transaction = self.camera_transactions.get(self.physical_id)
            if transaction is None: raise ValueError("Camera mapper has no preceding requested input poll")
            actions = self.mapped_camera_actions()
            if actions != {"cycle_pressed": transaction["expected"]["cycle_pressed"], "rear_held": transaction["input"]["rear_held"]}:
                raise ValueError("Requested camera keys disagree with actual cached poll actions")
            transaction["expected"].update(actions=actions, pre=self.camera_state())
            self.mapper_return = self.stack_return()
            self.points[self.mapper_return] = d.breakpoint(self.mapper_return)
            return True
        if pc == self.mapper_return:
            transaction = self.camera_transactions[self.physical_id]
            transaction["expected"]["post"] = self.camera_state()
            d.command(f"delete break {self.points.pop(pc)}")
            self.mapper_return = None
            return True
        if pc == PRIMARY:
            if self.primary is not None: raise ValueError("Nested scene primary frame")
            self.primary_return = self.stack_return()
            self.points[self.primary_return] = d.breakpoint(self.primary_return)
            self.primary = {"order": len(self.writer.frames), "input": {"physical": []}, "expected": {"physical_ids": []},
                            "provenance": {"entry": PRIMARY, "return": self.primary_return, "physical_returns": []}}
            if self.combined_order is not None:
                order = self.combined_order()
                if order is not None:
                    self.clock_events.append({"kind": "primary", "event": {"order": order, "cached_clock": d.word(0x4eda6c)}})
            return True
        if pc == PHYSICAL:
            if not self.journal.initialized: return False
            if self.primary is None or self.physical is not None or self.sample_return is not None or "qpc_counter" in self.primary["input"]:
                raise ValueError("Physical call outside an open scene primary frame")
            completed = len(self.primary["input"]["physical"])
            if completed and (completed != 1 or not self.primary["expected"]["timing"]["extra_update"] or "publish_order" not in self.primary["expected"]):
                raise ValueError("Extra physics lacks its retained permit or preceding publication")
            context, clock = self.journal.scope(), self.clock()
            if self.writer.header is None:
                if (clock["delta_q16"], clock["reciprocal_q16"], clock["extra_update"]) != (65536, 65536, 0) or self.journal.values("I", 0x4ad0f0)[0] != 1:
                    raise ValueError("Scene frames require canonical cold clock context")
                self.writer.start({name: context[name] for name in ("track_id", "car_id", "weather", "graphics_detail", "local_actor")}, clock)
            if not self.primary["input"]["physical"]:
                self.primary["expected"]["timing"] = {name: clock[name] for name in ("delta_q16", "reciprocal_q16", "extra_update")}
                self.primary["expected"]["timing"]["physical_count"] = 1 + clock["extra_update"]
            elif clock != self.current_clock:
                raise ValueError("Source timing changed inside a scene primary frame")
            self.current_clock = clock
            actor = context["local_actor"]
            body = BODY + actor * STRIDE
            self.physical = {"session_state": self.journal.values("H", d.word(0x4b0660) + 0x5f8)[0],
                             "pre_body": {"position_q16": self.journal.values("3i", body + 0xbc), "basis_q16": self.journal.values("9i", body + 0x150)}}
            self.physical_return = self.stack_return()
            self.points[self.physical_return] = d.breakpoint(self.physical_return)
            return False
        if pc == self.physical_return:
            self.physical["post_bodies"] = [{"position_q16": self.journal.values("3i", BODY + actor * STRIDE + 0xbc), "mode": d.word(BODY + actor * STRIDE + 0x4f0)} for actor in range(16)]
            if self.camera_controls:
                transaction = self.camera_transaction(self.physical_id)
                self.primary["expected"].setdefault("camera_transactions", []).append(transaction)
            self.primary["input"]["physical"].append(self.physical)
            self.primary["expected"]["physical_ids"].append(self.physical_id)
            self.physical_id += 1
            self.primary["provenance"]["physical_returns"].append(pc)
            self.physical = None
            d.command(f"delete break {self.points.pop(pc)}")
            self.physical_return = None
            return False
        if pc == SAMPLE:
            if self.primary is None or self.physical is not None or self.sample_return is not None or "qpc_counter" in self.primary["input"]: raise ValueError("QPC sample outside scene frame tail")
            if self.journal.initialized and (not self.ready_to_close() or "publish_order" not in self.primary["expected"]):
                raise ValueError("Scene QPC sample precedes complete physics/publication")
            returned = self.stack_return()
            if returned != SAMPLE_RETURN: raise ValueError("Unsupported scene QPC caller")
            if self.combined_clock is not None:
                self.sample_event = {"order": self.combined_order(), "return_address": returned, "pre": self.combined_clock()}
            self.sample_return = returned
            self.points[returned] = d.breakpoint(returned)
            return True
        if pc == self.sample_return:
            self.primary["input"]["qpc_counter"] = d.word(0x4be0cc)
            self.primary["provenance"]["qpc_return"] = pc
            self.primary["expected"]["next_clock"] = self.clock()
            if self.combined_clock is not None:
                self.sample_event.update(counter=d.word(0x4be0cc), post=self.combined_clock())
                self.clock_events.append({"kind": "qpc_sample", "event": self.sample_event})
            d.command(f"delete break {self.points.pop(pc)}")
            self.sample_return = None
            return True
        if pc == self.primary_return:
            d.command(f"delete break {self.points.pop(pc)}")
            self.primary_return = None
            if self.primary["input"]["physical"]: self.writer.append(self.primary)
            self.primary = None
            return False if getattr(self, "draining", False) else True
        return None

    def ready_to_close(self):
        return self.primary is not None and self.physical is None and len(self.primary["input"]["physical"]) == self.primary["expected"].get("timing", {}).get("physical_count")

    def ready_to_stop(self):
        return self.ready_to_close() and "publish_order" in self.primary["expected"] and coverage([*self.writer.frames, self.primary])["passed"] and self.journal.complete()

    def close(self):
        for point in self.points.values(): self.debugger.command(f"delete break {point}")
        self.points.clear()

    def finish(self, error=None):
        self.writer.finish(error, self.primary is not None or self.physical is not None or self.sample_return is not None)


def validate_autonomous_join(fleet_header, records, journal_header, events, frame_header, frames, footer, fleet_crc32):
    """Bind expectations to one source run; no bodies or roots enter native input."""
    if fleet_header.get("scene_camera_controls_manifest") != frame_header.get("camera_controls_manifest"):
        raise ValueError("Fleet and scene camera input profiles disagree")
    identity = fleet_header.get("scene_capture_id")
    if not identity or journal_header.get("capture_id") != identity or frame_header.get("capture_id") != identity:
        raise ValueError("Scene expectations belong to a different source execution")
    if footer.get("fleet_source") != {"prefix_crc32": fleet_crc32, "updates": len(records)}:
        raise ValueError("Scene expectations disagree with the complete fleet CRC32 prefix")
    setup = frame_header["setup"]
    for name in ("track_id", "car_id", "weather"):
        if setup[name] != fleet_header["initial_configuration"][name]:
            raise ValueError("Scene expectations disagree with fleet configuration")
    config = bytes.fromhex(fleet_header["configuration_hex"])
    if setup["graphics_detail"] != struct.unpack_from("<I", config, 0x328)[0]:
        raise ValueError("Scene expectations disagree with graphics detail")
    if setup["local_actor"] != fleet_header["local_actor"]:
        raise ValueError("Scene expectations disagree with the local actor")
    actual_ids = []
    for frame in frames:
        ids = frame["expected"].get("physical_ids")
        if not isinstance(ids, list) or len(ids) != len(frame["input"]["physical"]):
            raise ValueError("Joined scene frames require explicit physical expectation IDs")
        transactions = frame["expected"].get("camera_transactions")
        for physical_index, (update_id, physical) in enumerate(zip(ids, frame["input"]["physical"])):
            if type(update_id) is not int or update_id != len(actual_ids) or update_id >= len(records):
                raise ValueError("Missing/reordered scene physical expectation ID")
            actual_ids.append(update_id)
            record = records[update_id]
            if transactions is not None:
                transaction = transactions[physical_index]
                validate_camera_input(transaction["input"], update_id)
                if transaction["input"] != record.get("camera_input") or transaction["expected"] != record.get("camera_control_expected"):
                    raise ValueError("Camera input/checkpoint linkage differs from fleet physical ID")
            actor = setup["local_actor"]
            body = bytes.fromhex(record["pre"]["actors"][actor]["body_hex"])
            before = {"position_q16": list(struct.unpack_from("<3i", body, 0xbc)), "basis_q16": list(struct.unpack_from("<9i", body, 0x150))}
            after = [{"position_q16": list(struct.unpack_from("<3i", bytes.fromhex(value["body_hex"]), 0xbc)),
                      "mode": struct.unpack_from("<I", bytes.fromhex(value["body_hex"]), 0x4f0)[0]} for value in record["post"]["actors"]]
            if physical != {"session_state": record["pre"]["session_state"], "pre_body": before, "post_bodies": after}:
                raise ValueError("Scene physical expectations disagree with their fleet update")
    if actual_ids != list(range(len(records))):
        raise ValueError("Scene frames omit fleet physical updates")
    samples = [entry["event"]["counter"] for record in records for entry in [*record["combined_journal"], *record.get("scene_final_journal", [])] if entry["kind"] == "qpc_sample"]
    if samples != [frame["input"]["qpc_counter"] for frame in frames]:
        raise ValueError("Scene and fleet external counter sequences differ")
    return {"frames": len(frames), "physical_updates": len(actual_ids), "source_prefix_crc32": fleet_crc32}


def camera_coverage(frames, events):
    controls = [frame["expected"].get("control") for frame in frames]
    controls = [value for value in controls if value is not None]
    transactions = [transaction for frame in frames for transaction in frame["expected"].get("camera_transactions", [])]
    views = sorted({value["selected_view"] for value in controls})
    blocked = any(t["expected"]["actions"]["cycle_pressed"] and t["expected"]["pre"]["rear_held"] and t["expected"]["post"]["selected_view"] == 5 for t in transactions)
    released = [t for t in transactions if t["expected"]["pre"]["rear_held"] and not t["input"]["rear_held"]]
    restores = bool(released) and all(t["expected"]["post"]["selected_view"] == t["expected"]["pre"]["saved_view"] and t["expected"]["post"]["projection_view"] == t["expected"]["pre"]["projection_view"] for t in released)
    fovs = {events[frame["expected"]["select_order"]]["input"]["projection_q16"][0] for frame in frames}
    fresh = any(t["expected"]["actions"]["cycle_pressed"] and not t["expected"]["pre"]["rear_held"] and t["expected"]["post"]["selected_view"] != t["expected"]["pre"]["selected_view"] for t in transactions if released and t["input"]["physical_id"] > released[0]["input"]["physical_id"])
    gates = {"fresh_cycle_after_rear_release": fresh, "all_ordinary_views": views == [1, 2, 3, 4, 5], "cycle_blocked_during_rear": blocked,
             "rear_release_restores_view_and_projection": restores, "projection_changes": len(fovs) >= 2,
             "returns_to_view2": bool(controls) and controls[-1]["selected_view"] == 2}
    return {"views": views, "rear_releases": len(released), "gates": gates, "passed": all(gates.values())}
