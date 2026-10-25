"""Ordered, read-only scene-boundary inputs and separate original expectations.

The JSONL journal has its own CRC32 and lifetime. It does not extend FLET or
derive camera events from physical updates. Camera generation and physics are
outside this boundary; their actual consumer arguments are explicit inputs.
"""

import importlib.util
import json
from pathlib import Path
import math
import re
import struct
import zlib


FRAMES_SPEC = importlib.util.spec_from_file_location("reference_scene_frames", Path(__file__).with_name("reference_scene_frames.py"))
FRAMES = importlib.util.module_from_spec(FRAMES_SPEC)
FRAMES_SPEC.loader.exec_module(FRAMES)


SCHEMA = 1
HOOKS = {0x47de00: "init", 0x47e3c0: "migrate", 0x47d3d0: "select", 0x4271d0: "car_lod"}
CALLERS = {"init": 0x44ec96, "select": 0x487235, "car_lod": 0x420400, "publish": 0x487280}
PUBLISH = 0x489069
BODY = 0x4cd760
BODY_STRIDE = 0x53c
ORDINARY_PROFILE = "ordinary-views"
VIEW_FACTORS = (0, 0x20000, 0, 0, 0x140000, 0xa0000)


def encode(record):
    return (json.dumps(record, separators=(",", ":"), allow_nan=False) + "\n").encode()


def vector(value, count, integer=False):
    if not isinstance(value, list) or len(value) != count:
        raise ValueError("Invalid scene journal vector")
    for item in value:
        valid = type(item) is int if integer else type(item) in (float, int) and math.isfinite(item)
        if not valid: raise ValueError("Invalid scene journal vector")


def validate(event):
    kind, inputs = event.get("kind"), event.get("input")
    ordinary = event.get("profile") == ORDINARY_PROFILE
    root_count = 18 if ordinary else 17
    if type(event.get("order")) is not int or event["order"] < 0 or kind not in (*HOOKS.values(), "publish") or not isinstance(inputs, dict) or not isinstance(event.get("expected"), dict):
        raise ValueError("Invalid scene journal event")
    if kind in ("init", "car_lod"):
        if inputs.get("actor_count") != 16 or type(inputs.get("local_actor")) is not int or not 0 <= inputs["local_actor"] < 16 or inputs.get("source_view") not in (range(1, 6) if ordinary else (2,)) or inputs.get("view") != 0 or inputs.get("camera_branch") != 0 or inputs.get("factor_q16") != (VIEW_FACTORS[inputs["source_view"]] if ordinary and inputs.get("source_view") in range(1, 6) else 0) or inputs.get("graphics_detail") not in range(4) or inputs.get("source_mode") != 0:
            raise ValueError("Scene journal scope is not ordinary single-player View2")
    if kind == "init":
        roots = inputs.get("roots", [])
        cars = [root.get("actor") for root in roots if root.get("role") == "car_parent"]
        if len(roots) != root_count or sum(root.get("role") == "cockpit_anchor" for root in roots) != int(ordinary) or sorted(cars) != list(range(16)) or sum(root.get("role") == "follow_anchor" for root in roots) != 1:
            raise ValueError("Scene journal requires sixteen distinct car parents and one follow anchor")
        for index, root in enumerate(roots):
            if root.get("id") != index or root.get("role") not in (("car_parent", "follow_anchor", "cockpit_anchor") if ordinary else ("car_parent", "follow_anchor")) or type(root.get("flags")) is not int or root["flags"] == 999:
                raise ValueError("Invalid scene journal initialization role/order")
            vector(root.get("local_position"), 3)
    elif kind == "migrate":
        if type(inputs.get("root")) is not int or not 0 <= inputs["root"] < root_count:
            raise ValueError("Scene journal migration lacks an initialized root")
        vector(inputs.get("world_position"), 3)
    elif kind == "select":
        vector(inputs.get("camera_world"), 16)
        vector(inputs.get("world_to_view"), 16)
        vector(inputs.get("world_position"), 3)
        vector(inputs.get("projection_q16"), 16, True)
        if inputs.get("near_q16") != 0x4000 or type(inputs.get("far_q16")) is not int or not inputs["near_q16"] < inputs["far_q16"] <= 0x7fffffff:
            raise ValueError("Invalid actual scene projection near/far inputs")
        if type(inputs.get("all_sectors")) is not bool or any(type(root) is not int or not 0 <= root < root_count for root in inputs.get("ancestor_roots", [-1])):
            raise ValueError("Invalid scene journal camera ancestry")
    elif kind == "car_lod":
        if "refresh_request" in inputs or event["expected"].get("retained_refresh_request") != 1:
            raise ValueError("Retained refresh request is generated context, not an input")
        positions = inputs.get("body_positions_q16", [])
        if len(positions) != 16:
            raise ValueError("Scene journal requires sixteen physical scene-boundary inputs")
        for position in positions: vector(position, 3, True)
        vector(inputs.get("body_modes"), 16, True)
        if any(mode not in range(5) for mode in inputs["body_modes"]):
            raise ValueError("Invalid scene journal body mode")
    elif inputs:
        raise ValueError("Scene publication cannot import internal output state")


def coverage(events):
    """Coverage describes observed changes, independently of numerical parity."""
    kinds = [event["kind"] for event in events]
    publishes = [event for event in events if event["kind"] == "publish"]
    by_root = {}
    moving = False
    for event in events:
        if event["kind"] == "migrate":
            root, position = event["input"]["root"], event["input"]["world_position"]
            moving |= root in by_root and by_root[root] != position
            by_root[root] = position
    selects = [event["expected"] for event in events if event["kind"] == "select"]
    changed_pvs = any(item != selects[0] for item in selects[1:])
    lod = [tuple(tuple(mask & 1 for mask in car["lod_manual_masks"]) for car in event["expected"]["cars"])
           for event in events if event["kind"] == "car_lod"]
    changed_lod = any(item != lod[0] for item in lod[1:])
    registrations = {}
    changed_registration = False
    for event in events:
        if event["kind"] == "migrate":
            root, registration = event["input"]["root"], event["expected"]["registration"]
            changed_registration |= root in registrations and registrations[root] != registration
            registrations[root] = registration
    gates = {"initialized": kinds.count("init") == 1, "published_refreshes": len(publishes) >= 2,
             "moving_source": moving, "positive_pvs_registration_or_lod_change": changed_pvs or changed_registration or changed_lod}
    return {"events": len(events), "publishes": len(publishes), "gates": gates, "passed": all(gates.values())}


class Writer:
    def __init__(self, path, header):
        self.path = path
        self.output = path.open("wb")
        self.crc = 0
        self.events = []
        self.closed = False
        self.write({"type": "header", "schema": SCHEMA, "contract": "single-player-normal-view2-scene-boundary", **header})

    def write(self, record):
        raw = encode(record)
        self.output.write(raw)
        self.output.flush()
        self.crc = zlib.crc32(raw, self.crc)

    def append(self, event):
        validate(event)
        if self.closed or event["expected"] is None:
            raise ValueError("Scene journal requires complete source calls")
        if event["order"] != len(self.events):
            raise ValueError("Scene journal actual order is not contiguous")
        if (not self.events) != (event["kind"] == "init"):
            raise ValueError("Scene journal must initialize once before its other operations")
        self.write({"type": "event", **event})
        self.events.append(event)

    def finish(self, error=None, pending=0):
        if self.closed:
            return
        quality = coverage(self.events)
        aborted = error is not None or bool(pending)
        footer = {"type": "aborted" if aborted else "complete", "events": len(self.events),
                  "prefix_crc32": f"{self.crc:08x}", "coverage": quality, "incomplete_calls": pending}
        if aborted:
            footer["reason"] = str(error or "source stopped inside a scene call")
        try:
            self.output.write(encode(footer))
        finally:
            self.output.close()
            self.closed = True


def read(path, allow_aborted=False):
    crc = 0
    events = []
    header = footer = None
    with path.open("rb") as source:
        for raw in source:
            if footer is not None or not raw.endswith(b"\n"):
                raise ValueError("Scene journal contains an incomplete/trailing record")
            record = json.loads(raw, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
            if record.get("type") in ("complete", "aborted"):
                footer = record
                if record["prefix_crc32"] != f"{crc:08x}" or record["events"] != len(events):
                    raise ValueError("Scene journal footer CRC/count mismatch")
                continue
            if header is None:
                if record.get("type") != "header" or record.get("schema") != SCHEMA:
                    raise ValueError("Unsupported scene journal header")
                header = record
            elif record.get("type") != "event" or record.get("order") != len(events) or record.get("expected") is None:
                raise ValueError("Incomplete/unordered scene journal event")
            else:
                validate(record)
                if (record.get("profile") == ORDINARY_PROFILE) != (header.get("contract") == "single-player-ordinary-views-scene-boundary"):
                    raise ValueError("Scene event profile disagrees with its header")
                if (not events) != (record["kind"] == "init"):
                    raise ValueError("Scene journal must initialize once before its other operations")
                events.append(record)
            crc = zlib.crc32(raw, crc)
    if header is None or footer is None or footer["type"] == "aborted" and not allow_aborted:
        raise ValueError("Scene journal is not complete")
    if footer["coverage"] != coverage(events):
        raise ValueError("Scene journal coverage disagrees with actual events")
    return header, events, footer


def write_binary(path, header, events):
    """TOCASCN1 transports actual inputs only; expectations stay in JSONL."""
    if header.get("contract") == "single-player-ordinary-views-scene-boundary":
        raise ValueError("Ordinary-view journals are expectations for autonomous fleet replay")
    if header.get("exe_crc32") != "8ce56d41" or not events or events[0]["kind"] != "init":
        raise ValueError("Scene binary requires canonical initial scene provenance")
    initial = events[0]["input"]
    data = bytearray(b"TOCASCN1")
    data += struct.pack("<7I", 1, initial["track_id"], initial["car_id"], initial["weather"],
                        initial["graphics_detail"], initial["local_actor"], len(events))
    kinds = {"init": 1, "migrate": 2, "select": 3, "car_lod": 4, "publish": 5}
    for index, event in enumerate(events):
        validate(event)
        if event["order"] != index:
            raise ValueError("Scene binary events must preserve actual invocation order")
        kind, inputs = event["kind"], event["input"]
        data += struct.pack("<IQ", kinds[kind], event["order"])
        if kind == "init":
            data += struct.pack("<I", len(inputs["roots"]))
            for root in inputs["roots"]:
                data += struct.pack("<3I3f", {"external": 0, "car_parent": 1, "follow_anchor": 2}[root["role"]],
                                    root.get("actor", 0xffffffff), root["flags"], *root["local_position"])
        elif kind == "migrate":
            data += struct.pack("<I3f", inputs["root"], *inputs["world_position"])
        elif kind == "select":
            data += struct.pack("<16f3f18iI", *inputs["world_to_view"], *inputs["world_position"],
                                *inputs["projection_q16"], inputs["near_q16"], inputs["far_q16"], len(inputs["ancestor_roots"]))
            data += struct.pack(f'<{len(inputs["ancestor_roots"])}II', *inputs["ancestor_roots"], inputs["all_sectors"])
        elif kind == "car_lod":
            data += struct.pack("<4Ii", inputs["local_actor"], inputs["graphics_detail"], inputs["source_view"],
                                inputs["camera_branch"], inputs["factor_q16"])
            data += struct.pack("<48i16I", *(axis for position in inputs["body_positions_q16"] for axis in position), *inputs["body_modes"])
    path.write_bytes(data)
    return {"bytes": len(data), "crc32": f"{zlib.crc32(data):08x}"}


def expected_rows(events):
    """Active-view/consumed projection shared with the native scene CSV."""
    roots = events[0]["input"]["roots"]
    car_roots = {root["actor"]: root["id"] for root in roots if root["role"] == "car_parent"}
    migrated = set()
    for event in events:
        order, kind, expected = event["order"], event["kind"], event["expected"]
        registrations = []
        if kind == "init":
            registrations = list(enumerate(expected["roots"]))
        elif kind == "migrate":
            registrations = [(event["input"]["root"], expected)]
            migrated.add(event["input"]["root"])
        elif kind == "publish":
            registrations = [(car_roots[actor], car["physical"]) for actor, car in enumerate(expected["cars"])]
        for root, values in registrations:
            registration = values["registration"]
            yield order, "root", root, "sector", registration["sector"]
            if root in migrated:
                for axis, neighbor in enumerate(registration["neighbors"]):
                    yield order, "root", root, f"neighbor_{axis}", neighbor
            if kind == "publish":
                for field in ("render_mask", "manual_mask"):
                    yield order, "root", root, field, values[field] & 1
        if kind == "select":
            yield order, "pvs", 0, "count", len(expected["ordered_sectors"])
            for index, sector in enumerate(expected["ordered_sectors"]):
                yield order, "pvs", index, "order", sector
            for sector, detail in enumerate(expected["detail"]):
                yield order, "pvs", sector, "detail", detail
            for index, bits in enumerate(bytes.fromhex(expected["selected_bits_hex"])):
                yield order, "pvs", index, "bits", bits
        if kind in ("car_lod", "publish"):
            yield order, "models", 0, "refresh_request", expected["retained_refresh_request"]
            for actor, car in enumerate(expected["cars"]):
                for name, mask in zip(("high_manual", "medium_manual", "low_manual"), car["lod_manual_masks"]):
                    yield order, "models", actor, name, mask & 1
                for field, source in (("car_mask", "source_car_mask"), ("high_medium_mask", "source_high_medium_mask"), ("full_body_mask", "source_full_body_mask")):
                    yield order, "models", actor, field, car[source] & 1


class Recorder:
    """Nonblocking entry/return hooks retain nested migration chronology."""
    def __init__(self, debugger, path, checksum, owned_pid=None, frames_path=None, capture_id=None, camera_controls=False):
        self.debugger = debugger
        self.owned_pid = owned_pid
        self.camera_controls = camera_controls
        self.writer = Writer(path, {"exe_crc32": checksum,
            "scope": {"camera_generation": False, "physics_autonomy": False,
                      "tracked_roots": "physical-car-parents-follow-and-cockpit-anchors" if camera_controls else "physical-car-parents-and-follow-anchor"}, **({"capture_id": capture_id} if capture_id is not None else {}),
            **({"contract": "single-player-ordinary-views-scene-boundary"} if camera_controls else {})})
        self.points = {pc: debugger.breakpoint(pc) for pc in HOOKS}
        self.returns = {}
        self.pending = []
        self.queue = []
        self.roots = {}
        self.root_inputs = []
        self.publish_point = None
        self.initialized = False
        self.frames = FRAMES.Recorder(self, frames_path, checksum, capture_id, camera_controls) if frames_path is not None else None
        debugger.scene_journal = self

    def block(self, address, size):
        start = address & ~3
        raw = self.debugger.memory(start, ((address - start + size + 3) // 4) * 4)
        return raw[address - start:address - start + size]

    def values(self, fmt, address):
        return list(struct.unpack("<" + fmt, self.block(address, struct.calcsize("<" + fmt))))

    def scope(self):
        d = self.debugger
        config = d.word(0x4b0660)
        count = self.values("H", 0x4c5d6e)[0]
        actor = self.values("B", 0x4d37a0)[0]
        branch = d.word(0x4c3a58)
        view = self.values("H", 0x4c3a66)[0]
        factor = d.word(0x4ae7b4 + view * 4) if view in range(6) else -1
        allowed = view in range(1, 6) and factor == VIEW_FACTORS[view] if self.camera_controls else view == 2 and factor == 0
        if d.word(config + 0xac) != 1 or d.word(config + 4) != 0 or count != 16 or actor >= count or branch or not allowed:
            raise ValueError("Scene journal requires single-player ordinary View2/factor0")
        return {"actor_count": count, "local_actor": actor, "view": 0, "source_view": view,
                "camera_branch": branch, "factor_q16": factor, "graphics_detail": d.word(config + 0x328),
                "track_id": d.word(config + 0x1c), "source_mode": d.word(config + 4),
                "car_id": d.word(config + 0xdc), "weather": self.values("H", 0x4c5d74)[0]}

    def root_expected(self, pointer):
        sector, *neighbors = self.values("4H", pointer + 0x120)
        return {"registration": {"sector": sector, "neighbors": neighbors},
                "render_mask": self.values("B", pointer + 0x138)[0],
                "manual_mask": self.values("B", pointer + 0x140)[0]}

    def initialize(self):
        d = self.debugger
        context = self.scope()
        if self.camera_controls and any(d.word(d.word(0x4b0660) + offset) for offset in (0x48, 0x7c, 0x88)):
            raise ValueError("Ordinary camera journal excludes MICRO, trackside and copter controllers")
        parents = self.values(f'{context["actor_count"]}I', 0x4c3968)
        follow = d.word(0x4c3638)
        wanted = {pointer: ("car_parent", actor) for actor, pointer in enumerate(parents)}
        if not follow or follow in wanted or len(wanted) != len(parents):
            raise ValueError("Scene journal has ambiguous car/follow initialization roles")
        wanted[follow] = ("follow_anchor", None)
        if self.camera_controls:
            cockpit = d.word(0x4c38f8)
            if not cockpit or cockpit in wanted: raise ValueError("Ambiguous cockpit anchor role")
            wanted[cockpit] = ("cockpit_anchor", None)
        manager = d.word(d.word(0x4b9b94) + 0x200)
        self.scene_root_pointer = manager
        self.follow_pointer = follow
        pointer = d.word(manager + 4)
        visited = set()
        roots = []
        while pointer:
            if pointer in visited or len(visited) >= 8192:
                raise ValueError("Source scene root list is cyclic/oversized")
            visited.add(pointer)
            if pointer in wanted:
                flags = d.word(pointer + 0x18)
                if flags == 999:
                    raise ValueError("Tracked scene role is excluded from47de00")
                role, actor = wanted[pointer]
                root = {"id": len(roots), "role": role, "flags": flags,
                        "local_position": self.values("3f", pointer + 0x8c)}
                if actor is not None: root["actor"] = actor
                roots.append(root)
                self.roots[pointer] = root["id"]
            pointer = d.word(pointer)
        if set(self.roots) != set(wanted):
            raise ValueError("Scene roles were not present at actual47de00 initialization")
        self.root_inputs = roots
        self.initialization_provenance = {"scene_manager_pointer": manager,
            "root_head_pointer": d.word(manager + 4),
            "roles": [{"id": index, "pointer": pointer} for pointer, index in sorted(self.roots.items(), key=lambda item: item[1])]}
        self.initialized = True
        return {**context, "roots": roots}

    def input(self, kind, argument):
        d = self.debugger
        if kind == "init":
            if self.initialized: raise ValueError("Scene journal covers one initialization lifetime")
            return self.initialize()
        if not self.initialized:
            return None
        if kind == "migrate":
            if argument not in self.roots: return None
            return {"root": self.roots[argument], "world_position": self.values("3f", argument + 0xcc)}
        if kind == "select":
            context = self.scope()
            if argument != d.word(0x4c3478 + context["source_view"] * 4):
                raise ValueError("Scene selection does not use its actual View2 camera")
            ancestors, visited = [], set()
            pointer = argument
            while pointer:
                if pointer in visited or len(visited) >= 64:
                    raise ValueError("Invalid scene camera ancestry")
                visited.add(pointer)
                if pointer in self.roots:
                    ancestors.append(self.roots[pointer])
                elif pointer == argument and d.word(pointer + 8) in self.roots and d.word(pointer + 0x18) == 999 and self.values("H", pointer + 0x120)[0] == 0xffff:
                    pass # Fresh camera leaf is not a47de00 registered root.
                elif pointer == self.scene_root_pointer and d.word(pointer + 0x18) == 1000 and self.values("H", pointer + 0x120)[0] == 0xffff:
                    pass # Scene root itself is excluded from its child list.
                else:
                    raise ValueError("Camera ancestry contains a root outside the initialized scene subset")
                pointer = d.word(pointer + 8)
            renderer = d.word(0x4b9b94)
            near = self.values("f", 0x5b5060)[0]
            if near != .25:
                raise ValueError("Scene journal requires the observed ordinary near-plane caller0x4000")
            self.projection_provenance = {"near_float": near,
                "pvs_limits_q16": self.values("4i", 0x4b99a4)}
            return {"camera_world": self.values("16f", argument + 0x9c),
                    "world_to_view": self.values("16f", renderer + 0x4300),
                    "world_position": self.values("3f", argument + 0xcc),
                    "projection_q16": self.values("16i", renderer + 0x4240),
                    "near_q16": 0x4000, "far_q16": self.values("i", 0x4f2938)[0],
                    "ancestor_roots": ancestors, "all_sectors": bool(d.word(0x597fb0))}
        context = self.scope()
        if argument != 0: raise ValueError("Scene journal requires viewport0")
        if d.word(0x4adeb8) != 1:
            raise ValueError("Fresh View2 scene requires canonical retained refresh request1")
        return {**context,
                "body_positions_q16": [self.values("3i", BODY + actor * BODY_STRIDE + 0xbc) for actor in range(context["actor_count"])],
                "body_modes": [d.word(BODY + actor * BODY_STRIDE + 0x4f0) for actor in range(context["actor_count"])]}

    def expected(self, event):
        kind, d = event["kind"], self.debugger
        if kind == "init":
            return {"roots": [{"registration": {"sector": self.values("H", pointer + 0x120)[0]}} for pointer, _ in sorted(self.roots.items(), key=lambda item: item[1])]}
        if kind == "migrate":
            return {"registration": self.root_expected(event["provenance"]["argument_pointer"])["registration"]}
        if kind == "select":
            count, sectors = d.word(0x597fa4), d.word(0x597e80)
            if count > sectors or sectors > 2048:
                raise ValueError("Source PVS selection exceeds its canonical bound")
            return {"ordered_sectors": self.values(f"{count}H", 0x568e68) if count else [],
                    "detail": [self.values("B", 0x581e94 + sector * 0x2c)[0] for sector in range(sectors)],
                    "selected_bits_hex": self.block(0x597e90, 256).hex()}
        count = len([root for root in self.root_inputs if root["role"] == "car_parent"])
        return {"retained_refresh_request": d.word(0x4adeb8),
                "cars": [{"physical": self.root_expected(d.word(0x4c3968 + actor * 4)),
                           "lod_manual_masks": [self.values("B", d.word(table + actor * 4) + 0x140)[0] for table in (0x4c39d0, 0x4c34f8, 0x4c3650)],
                           "source_car_mask": self.values("B", 0x4ed880 + actor)[0],
                           "source_high_medium_mask": self.values("B", 0x4ed898 + actor)[0],
                           "source_full_body_mask": self.values("B", 0x4ed968 + actor)[0]} for actor in range(count)]}

    def intercept(self, response):
        if self.frames is not None:
            intercepted = self.frames.intercept(response)
            if intercepted is not None: return intercepted
        candidates = [pc for pc in (*HOOKS, *self.returns, *([PUBLISH] if self.publish_point is not None else [])) if f"{pc:08x}" in response.lower()]
        if not candidates: return False
        if len(set(candidates)) != 1: raise ValueError("Ambiguous scene journal debugger boundary")
        pc = candidates[0]
        if pc not in HOOKS and pc not in self.returns and not (pc == PUBLISH and self.publish_point is not None):
            return False
        d = self.debugger
        d.confirm_pause()
        if pc == 0x47de00 and self.owned_pid is not None and getattr(d, "memory_fd", None) is None:
            d.enable_process_memory(self.owned_pid)
        if pc in self.returns:
            if not self.pending or self.pending[-1][0] != pc:
                raise ValueError("Scene journal return does not close the innermost source call")
            _, event = self.pending.pop()
            event["expected"] = self.expected(event)
            point, users = self.returns[pc]
            if users == 1:
                d.command(f"delete break {point}")
                del self.returns[pc]
            else: self.returns[pc] = (point, users - 1)
            if event["kind"] == "car_lod" and self.publish_point is None:
                self.publish_point = d.breakpoint(PUBLISH)
            self.flush()
            return True
        if pc == PUBLISH:
            stack = self.values("4I", int(re.search(r"0x([0-9a-fA-F]+)", d.command("print $esp")).group(1), 16))
            if stack[0] != CALLERS["publish"] or stack[3] != 0:
                raise ValueError("Scene geometry publication has an unsupported caller/view")
            event = {"order": len(self.writer.events) + len(self.queue), "kind": "publish", "input": {},
                     "provenance": {"entry": pc, "return": stack[0], "camera_pointer": stack[1]}, "expected": self.expected({"kind": "publish"})}
            if self.camera_controls: event["profile"] = ORDINARY_PROFILE
            self.queue.append(event)
            if self.frames is not None: self.frames.observe("publish", event["order"])
            d.command(f"delete break {self.publish_point}")
            self.publish_point = None
            self.flush()
            return True
        stack = self.values("2I", int(re.search(r"0x([0-9a-fA-F]+)", d.command("print $esp")).group(1), 16))
        returned, argument = stack
        if not 0x400000 <= returned < 0x4b0000:
            raise ValueError("Scene hook return lies outside canonical executable")
        if HOOKS[pc] in CALLERS and returned != CALLERS[HOOKS[pc]]:
            raise ValueError("Scene hook has an unsupported actual source caller")
        inputs = self.input(HOOKS[pc], argument & 255 if pc == 0x4271d0 else argument)
        if inputs is None: return True
        event = {"order": len(self.writer.events) + len(self.queue), "kind": HOOKS[pc], "input": inputs,
                 "provenance": {"entry": pc, "return": returned, "argument_pointer": argument}, "expected": None}
        if pc == 0x47de00:
            event["provenance"].update(getattr(self, "initialization_provenance", {}))
        elif pc == 0x47d3d0:
            event["provenance"].update(getattr(self, "projection_provenance", {}))
        if self.camera_controls: event["profile"] = ORDINARY_PROFILE
        self.queue.append(event)
        if self.frames is not None:
            self.frames.observe(event["kind"], event["order"])
            if self.camera_controls and event["kind"] == "select":
                ancestors = inputs["ancestor_roots"]
                if len(ancestors) != 1: raise ValueError("Ordinary camera requires one registered ancestor")
                self.frames.primary["expected"]["control"]["ancestor_root"] = ancestors[0]
        if returned in self.returns:
            point, users = self.returns[returned]
            self.returns[returned] = (point, users + 1)
        else: self.returns[returned] = (d.breakpoint(returned), 1)
        self.pending.append((returned, event))
        return True

    def flush(self):
        while self.queue and self.queue[0]["expected"] is not None:
            self.writer.append(self.queue.pop(0))

    def complete(self):
        return not self.pending and not self.queue and coverage(self.writer.events)["passed"]

    def close(self):
        if self.frames is not None: self.frames.close()
        self.debugger.scene_journal = None
        for point in [*self.points.values(), *(item[0] for item in self.returns.values()), *([self.publish_point] if self.publish_point else [])]:
            self.debugger.command(f"delete break {point}")

    def finish(self, error=None):
        self.writer.finish(error, sum(event["expected"] is None for event in self.queue))
        if self.frames is not None: self.frames.finish(error)
