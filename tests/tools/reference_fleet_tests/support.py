"""Fleet schema integrity, actual actor coverage and packed native handoff."""

import importlib.util
import copy
import csv
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("reference_fleet", ROOT / "tools/reference_fleet.py")
FLEET = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FLEET)
CLI_SPEC = importlib.util.spec_from_file_location("replay_fleet_reference", ROOT / "tools/replay-fleet-reference.py")
CLI = importlib.util.module_from_spec(CLI_SPEC)
CLI_SPEC.loader.exec_module(CLI)


def state(boundary, counter=1, moving=False):
    actors = []
    for actor in range(16):
        entry = {"actor": actor}
        for name in ("body", "dynamics", "gameplay"):
            raw = bytearray(FLEET.LAYOUT[name + "_stride"])
            if name == "body":
                struct.pack_into("<I", raw, 0, 0x100000 + actor * 0x200)
                struct.pack_into("<I", raw, 8, 0x100100 + actor * 0x200)
                struct.pack_into("<3i", raw, 0xbc, (actor + int(moving)) * 65536, 0, 0)
                struct.pack_into("<i", raw, 0xe0, 65536 if moving else 0)
                struct.pack_into("<I", raw, 0x4f0, actor % 2)
                raw[0x516] = actor % 8
                if actor == 0 and moving:
                    raw[0x529:0x52b] = bytes([1, 1])
            elif name == "dynamics":
                struct.pack_into("<i", raw, 0x140, 10000 + actor)
                struct.pack_into("<I", raw, 0x27c, 2)
            entry[name + "_hex"] = raw.hex()
        registration = bytearray(0x24)
        struct.pack_into("<4H", registration, 0, actor, 0xffff, 0xffff, 0xffff)
        registration[0x20:0x22] = b"\x01\x00"
        entry["root"] = {"pointer": 0x100000 + actor * 0x200, "registration_hex": registration.hex(),
                         "sector": actor, "neighbors": [0xffff] * 3, "mask": 1, "detail": 0,
                         "sector_visible": True}
        entry["effect_root"] = {"pointer": 0x100100 + actor * 0x200, "flags_hex": "01000000"}
        actors.append(entry)
    shared = {name + "_hex": bytes(size).hex() for name, (_, size) in FLEET.BLOCKS.items()}
    shared.update({name: 0 for name in FLEET.WORDS})
    shared["detached_free_hex"] = struct.pack("<10I", *([1] * 10)).hex()
    shared["barrier_records_pointer"] = 0x600000
    shared["provenance"] = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.PROVENANCE_BLOCKS.items()}
    shared["props"] = {name: {"count": 0, "stride": stride, "records_hex": "", **({"model_records_hex": ""} if name == "signs" else {})}
                       for name, (_, _, stride) in FLEET.PROPS.items()}
    return {"boundary": boundary, "actor": 0, "actors": actors, "shared": shared,
            "body_hex": actors[0]["body_hex"], "dynamics_hex": actors[0]["dynamics_hex"], "gameplay_hex": actors[0]["gameplay_hex"],
            "rng": 0x12345678, "roughness_cursor": 0, "update_counter": counter, "qpc_low": 1,
            "delta_q16": 2621, "reciprocal_q16": 1638425, "extra_update": False,
            "limiter_active": 0, "limiter_load_q16": 0, "session_state": 9,
            "source_mode": 0, "allocated_actor_count": 16,
            "car_id": 0, "weather": 1, "track_id": 0, "steering_profile": 2, "transmission_mode": 2,
            "input_q16": {"left": 0, "right": 0, "throttle": 10000, "brake": 0, "gear_edge": 0}}


def fixture(count=2):
    config = bytearray(0x620)
    struct.pack_into("<I", config, 0x20, 1)
    struct.pack_into("<I", config, 0xac, 1)
    header = {"type": "header", "schema": 2, "scope": "fleet", "exe_crc32": "8ce56d41",
              "layout": FLEET.LAYOUT, "boundaries": FLEET.BOUNDARIES, "physical_root_body_offset": 0, "sign_model_stride": 68,
              "shared_blocks": FLEET.BLOCKS, "shared_words": FLEET.WORDS, "props_layout": FLEET.PROPS,
              "extra_provenance_blocks": FLEET.PROVENANCE_BLOCKS,
              "session": "single-race", "source_mode": 0, "actor_count": 16, "allocated_actor_count": 16,
              "local_actor": 0, "configuration_hex": config.hex(),
              "initial_configuration": {"car_id": 0, "weather": 1, "track_id": 0, "steering_profile": 2, "transmission_mode": 2}}
    records = [{"type": "update", "id": index,
                **{name: state(boundary, index if name == "pre" else index + 1, name == "post")
                   for name, boundary in FLEET.BOUNDARIES.items()}} for index in range(count)]
    return header, records


def write_trace(path, header, records):
    prefix = b"".join((json.dumps(record) + "\n").encode() for record in [header, *records])
    footer = {"type": "complete", "updates": len(records), "prefix_crc32": f"{zlib.crc32(prefix):08x}"}
    path.write_bytes(prefix + (json.dumps(footer) + "\n").encode())


def ai_fixture(count=2):
    header, records = fixture(count)
    points = struct.pack("<I", 64) + bytes(range(256)) * 16
    content = {"resolved_ai_hex": bytes(0x540).hex(),
               "route_descriptor_hex": struct.pack("<4I", 0x500000, 64, 1, 0x501004).hex(),
               "points_hex": points.hex(), "start_bound": 25, "end_bound": 56,
               "startline_primary_hex": bytes(396).hex(), "startline_alternate_hex": bytes(396).hex()}
    for name in ("resolved_ai", "points", "startline_primary", "startline_alternate"):
        content[name + "_crc32"] = f"{zlib.crc32(bytes.fromhex(content[name + '_hex'])):08x}"
    header.update(ai_state_manifest=FLEET.AI_STATE_MANIFEST, ai_boundaries=FLEET.AI_BOUNDARIES, ai_initial_state=content)
    for index, record in enumerate(records):
        for name, boundary in FLEET.AI_BOUNDARIES.items():
            record[name] = state(boundary, index + 1)
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            record[name]["shared"]["barrier_records_pointer"] = 0x501004
            record[name]["shared"]["ai_state"] = {
                "release_counter": 360 - index,
                **{key: value for key, value in content.items() if not key.endswith("_hex")},
                "route_descriptor_hex": content["route_descriptor_hex"]}
    return header, records


def nearest_fixture(jump=True):
    header, records = ai_fixture(1)
    table = bytearray(256)
    table[1] = 40
    header.update(nearest_recovery_manifest=FLEET.NEAREST_RECOVERY_MANIFEST,
                  nearest_recovery_initial_state={"nearest_map_hex": table.hex(), "nearest_map_crc32": f"{zlib.crc32(table):08x}"})
    record = records[0]
    pre = bytearray.fromhex(record["ai_pre"]["actors"][0]["gameplay_hex"])
    struct.pack_into("<2I", pre, 4, 1, 1)
    struct.pack_into("<2h", pre, 0xa2, 0, 63)
    record["ai_pre"]["actors"][0]["gameplay_hex"] = pre.hex()
    record["ai_pre"]["gameplay_hex"] = pre.hex()
    #Ordinary progression already advanced0→1 before the recovery block.
    struct.pack_into("<I", pre, 0x3c, 1)
    struct.pack_into("<2h", pre, 0xa2, 1, 0)
    before = {"boundary": "4046a3-entry", "actor_index": 0, "cursor_pointer": FLEET.LAYOUT["gameplay_base"] + 0xa2,
              "position_pointer": FLEET.LAYOUT["body_base"] + 0xbc, "body_hex": record["ai_pre"]["actors"][0]["body_hex"],
              "gameplay_hex": pre.hex(), "nearest_map_hex": table.hex(), "rng": 0x12345678}
    post = bytearray(pre)
    if jump: struct.pack_into("<2h", post, 0xa2, 40, 39)
    after = {**before, "boundary": "404731-exit", "gameplay_hex": post.hex()}
    record["ai_post"]["actors"][0]["gameplay_hex"] = post.hex()
    record["ai_post"]["gameplay_hex"] = post.hex()
    record["nearest_recoveries"] = [{"actor": 0, "pre": before, "post": after}]
    return header, records


def joined_fixture():
    header, records = ai_fixture(3)
    bypass = bytes(64)
    initial = {"point_count": 64, "bypass_pointer": 0x700002, "brake_bypass_hex": bypass.hex(), "brake_bypass_crc32": f"{zlib.crc32(bypass):08x}"}
    cumulative = struct.pack("<64i", *(point * 65536 for point in range(64)))
    setup = {"cumulative_route_hex": cumulative.hex(), "cumulative_route_crc32": f"{zlib.crc32(cumulative):08x}", "rank_threshold": 4000000}
    call_pre = {"release_counter": 0, "rng": 7, "race_clock": 1000, "release_epoch": 0, "source_mode": 0, "session_stage": 2, "local_players": 1}
    header.update(traffic_control_manifest=FLEET.TRAFFIC_CONTROL_MANIFEST, traffic_initial_state=initial, traffic_boundaries=FLEET.TRAFFIC_BOUNDARIES,
                  joined_ai_manifest=FLEET.JOINED_AI_MANIFEST, joined_initial_state=setup,
                  grid_initialization={"return_address": 0x447de5, "pre": {**call_pre, "boundary": "460170-entry"},
                                       "post": {**call_pre, "boundary": "460170-return"}})
    for index, record in enumerate(records):
        record["traffic_pre"] = json.loads(json.dumps(record["ai_pre"]))
        record["traffic_pre"]["boundary"] = "404c1a-entry"
        record.update(update_caller_return_address=0x448434 if index == 2 else 0x4488a1,
                      release_events=[{"return_address": 0x4220a6, "pre": {**call_pre, "boundary": "448130-entry"},
                                       "post": {**call_pre, "boundary": "448130-return", "release_counter": 360, "release_epoch": 1000}}] if index == 1 else [])
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            state = record[phase]
            state["session_state"] = 6 if index == 2 else 9
            state["shared"]["ai_state"]["release_counter"] = (0, 360, 359)[index]
            state["shared"]["joined_ai"] = {"cumulative_route_crc32": setup["cumulative_route_crc32"], "rank_threshold": setup["rank_threshold"], "point_count": 64}
            raw = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.TRAFFIC_CONTROL_BLOCKS.items()}
            raw.update({key: value for key, value in initial.items() if key != "brake_bypass_hex"})
            raw.update(brake_bypass_pointer_hex=struct.pack("<I", initial["bypass_pointer"]).hex(), force_acceleration_hex=bytes(4).hex())
            state["shared"]["traffic_control"] = raw
            for actor in state["actors"]:
                game = bytearray.fromhex(actor["gameplay_hex"])
                struct.pack_into("<2I", game, 4, 1, int(actor["actor"] == 0))
                game[0x8d] = actor["actor"]
                actor["gameplay_hex"] = game.hex()
            state["gameplay_hex"] = state["actors"][0]["gameplay_hex"]
        record["traffic_pre"]["rng"] += 1
        record["ai_post"]["rng"] = record["traffic_pre"]["rng"]
        record["consumed"]["rng"] += 2
    return header, records


def timing_fixture(count=2):
    header, records = ai_fixture(count)
    header["checkpoint_timing_manifest"] = FLEET.CHECKPOINT_TIMING_MANIFEST
    for index, record in enumerate(records):
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            snapshot = record[name]
            snapshot["race_clock"] = 50000 + index * 40
            snapshot["session_state"] = 6
            snapshot["shared"]["checkpoint_timing"] = {"split_table_hex": bytes(576).hex(),
                                                        "race_flags_hex": bytes(4).hex(), "session_stage": 2}
            world = bytearray.fromhex(snapshot["shared"]["provenance"]["world_status_hex"])
            struct.pack_into("<H", world, 0x14, 3)
            snapshot["shared"]["provenance"]["world_status_hex"] = world.hex()
            noqual = bytearray.fromhex(snapshot["shared"]["provenance"]["noqual_flags_hex"])
            struct.pack_into("<I", noqual, 8, 1)
            snapshot["shared"]["provenance"]["noqual_flags_hex"] = noqual.hex()
    return header, records


def finish_fixture(count=2):
    header, records = timing_fixture(count)
    header["finish_timing_manifest"] = FLEET.FINISH_TIMING_MANIFEST
    for record in records:
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            snapshot = record[name]
            finish = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.FINISH_TIMING_BLOCKS.items()}
            finish["race_counters_hex"] = struct.pack("<4I", 0, snapshot["update_counter"], snapshot["race_clock"], 1000).hex()
            snapshot["shared"]["finish_timing"] = finish
    return header, records


def launch_state(snapshot, launch, *, hold=False, epoch=None, tires=None):
    dynamics = bytearray.fromhex(snapshot["dynamics_hex"])
    struct.pack_into("<i", dynamics, 0x124, 623902)
    struct.pack_into("<I", dynamics, 0x148, hold)
    if tires is not None:
        struct.pack_into("<8i", dynamics, 0xdc, *tires)
    snapshot["dynamics_hex"] = snapshot["actors"][0]["dynamics_hex"] = dynamics.hex()
    gameplay = bytearray.fromhex(snapshot["gameplay_hex"])
    struct.pack_into("<I", gameplay, 8, 1)
    snapshot["gameplay_hex"] = snapshot["actors"][0]["gameplay_hex"] = gameplay.hex()
    snapshot["shared"]["provenance"]["local_launch_tires_hex"] = struct.pack("<4i", launch, 0, 0, 0).hex()
    if epoch is not None:
        snapshot["race_clock"] = epoch + 1000
        lifecycle = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.LIFECYCLE_BLOCKS.items()}
        raw = bytearray(0x14)
        struct.pack_into("<II", raw, 8, snapshot["race_clock"], epoch)
        lifecycle["race_lifecycle_hex"] = raw.hex()
        snapshot["shared"]["lifecycle"] = lifecycle


class TraceTestCase(unittest.TestCase):
    """Private synthetic trace storage, cleaned up even when a check fails."""

    trace_filename = "trace.jsonl"
    capture_recorder = False

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / self.trace_filename
        if self.capture_recorder:
            spec = importlib.util.spec_from_file_location("trace_reference", ROOT / "tools/trace-reference.py")
            self.trace = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.trace)


class FleetTestCase(TraceTestCase):
    """Shared synthetic trace fixtures and CSV writers, without test cases."""

    def joined_csv(self, records):
        native = self.traffic_csv(records)
        for suffix, phase, expected, actor_rows in ((".ai-route.csv", "traffic_pre", FLEET.expected_ai_route, True),
                                                    (".joined-progress.csv", "ai_post", FLEET.expected_joined_progress, True),
                                                    (".joined-world.csv", "ai_pre", lambda state, _: {"release_counter": state["shared"]["ai_state"]["release_counter"]}, False)):
            fields = ["id", *(["actor"] if actor_rows else []), *expected(records[0][phase], 0)]
            with Path(str(native) + suffix).open("w", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=fields)
                writer.writeheader()
                for record in records:
                    for actor in (range(16) if actor_rows else [None]):
                        writer.writerow({"id": record["id"], **({"actor": actor} if actor_rows else {}), **expected(record[phase], actor)})
        return native

    def nearest_csv(self, header, records):
        native = self.path.with_suffix(".native.csv")
        table = bytes.fromhex(header["nearest_recovery_initial_state"]["nearest_map_hex"])
        with Path(str(native) + ".nearest-map.csv").open("w", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["index", "candidate"])
            writer.writerows(enumerate(table[:64]))
        with Path(str(native) + ".nearest-recovery.csv").open("w", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["id", "event", "actor", "route_next", "route_previous", "rng_final"])
            for record in records:
                for index, event in enumerate(record["nearest_recoveries"]):
                    post = FLEET.nearest_recovery_values(event["post"])
                    writer.writerow([record["id"], index, event["actor"], post["route_next"], post["route_previous"], post["rng"]])
        return native

    def traffic_fixture(self):
        header, records = ai_fixture(2)
        content = {"point_count": 64, "bypass_pointer": 0x700002, "brake_bypass_hex": bytes(range(64)).hex(),
                   "brake_bypass_crc32": f"{zlib.crc32(bytes(range(64))):08x}"}
        header.update(traffic_control_manifest=FLEET.TRAFFIC_CONTROL_MANIFEST, traffic_initial_state=content, traffic_boundaries=FLEET.TRAFFIC_BOUNDARIES)
        for record in records:
            record["traffic_pre"] = json.loads(json.dumps(record["ai_pre"]))
            record["traffic_pre"]["boundary"] = "404c1a-entry"
            for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                state = record[name]
                for actor in state["actors"]:
                    game = bytearray.fromhex(actor["gameplay_hex"])
                    struct.pack_into("<I", game, 4, 1)
                    struct.pack_into("<I", game, 8, int(actor["actor"] == 0))
                    game[0x81] = 1
                    actor["gameplay_hex"] = game.hex()
                state["gameplay_hex"] = state["actors"][0]["gameplay_hex"]
                raw = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.TRAFFIC_CONTROL_BLOCKS.items()}
                raw["brake_bypass_pointer_hex"] = struct.pack("<I", content["bypass_pointer"]).hex()
                raw.update({key: value for key, value in content.items() if key != "brake_bypass_hex"})
                raw["force_acceleration_hex"] = bytes(4).hex()
                braking = bytearray(1212)
                struct.pack_into("<i", braking, 0x104, -12345)  #Base4bcde8 index-1, adjacent setup memory.
                raw["braking_memory_hex"] = braking.hex()
                state["shared"]["traffic_control"] = raw
        post = records[1]["ai_post"]["actors"][1]
        game = bytearray.fromhex(post["gameplay_hex"])
        game[0x81] = 2
        struct.pack_into("<I", game, 0x2c, 1)
        post["gameplay_hex"] = game.hex()
        actor = records[1]["consumed"]["actors"][1]
        dynamics = bytearray.fromhex(actor["dynamics_hex"])
        struct.pack_into("<4i", dynamics, 0x138, 12345, 0, 0, 65536)
        actor["dynamics_hex"] = dynamics.hex()
        write_trace(self.path, header, records)
        return FLEET.read_trace(self.path)

    def traffic_csv(self, records):
        native = self.path.with_suffix(".native.csv")
        for suffix, phase, actors, expected in ((".traffic.csv", "ai_post", lambda record: range(16), FLEET.expected_traffic),
                                                (".traffic-world.csv", "ai_post", lambda record: [None], lambda state, _: FLEET.expected_traffic_world(state)),
                                                (".ai-controls.csv", "consumed", FLEET.traffic_control_actors, FLEET.expected_ai_controls),
                                                (".ai-control-world.csv", "consumed", lambda record: [None], lambda state, _: FLEET.expected_traffic_world(state, True))):
            actor_rows = suffix in (".traffic.csv", ".ai-controls.csv")
            fields = ["id", *(["actor"] if actor_rows else []), *expected(records[0][phase], 0 if actor_rows else None)]
            with Path(str(native) + suffix).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                writer.writerows({"id": record["id"], **({"actor": actor} if actor_rows else {}), **expected(record[phase], actor)}
                                 for record in records for actor in actors(record))
        return native

    def session_fixture(self, extended=True):
        header, records = finish_fixture(2)
        if extended:
            header.update(local_handoff_manifest=FLEET.LOCAL_HANDOFF_MANIFEST, session_boundary_manifest=FLEET.SESSION_BOUNDARY_MANIFEST)
            for record in records:
                record["local_handoffs"] = []
                for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
                    record[name]["shared"]["session_boundary"] = {**{key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.SESSION_BOUNDARY_BLOCKS.items()}, "configuration_tail_hex": bytes(0x30).hex()}
        write_trace(self.path, header, records)
        return FLEET.read_trace(self.path)

    def session_csv(self, records, unsupported=()):
        native = self.path.with_suffix(".native.csv")
        schemas = [(".session-unsupported.csv", ["id", "reason"], [{"id": item, "reason": "source case outside bounded consumer"} for item in unsupported])]
        supported = [record for record in records if record["id"] not in unsupported]
        for suffix, identity, count, expected in ((".checkpoint.csv", "slot", 48, FLEET.expected_checkpoint),
                                                 (".ai-timing.csv", "actor", 16, lambda state, actor: FLEET.expected_ai_timing(state, actor, True)),
                                                 (".finish.csv", None, 1, lambda state, _: FLEET.expected_finish(state)),
                                                 (".split-display.csv", "player", 4, FLEET.expected_split_display)):
            fields = ["id", *([identity] if identity else []), *expected(records[0]["ai_post"], 0)]
            data = [{"id": record["id"], **({identity: index} if identity else {}), **expected(record["ai_post"], index)} for record in supported for index in range(count)]
            schemas.append((suffix, fields, data))
        for suffix, identity, fields, expected in ((".session.csv", None, list(FLEET.expected_session(records[0]["ai_post"])), FLEET.expected_session),
                                                  (".session-controller.csv", "actor", list(FLEET.expected_session_controller(records[0]["ai_post"])), FLEET.expected_session_controller),
                                                  (".session-classification.csv", None, list(FLEET.SESSION_CLASSIFICATION_FIELDS), FLEET.expected_session_classification)):
            data = [{"id": record["id"], **({identity: record["ai_post"]["actor"]} if identity else {}), **expected(record["ai_post"])}
                    for record in supported if suffix != ".session-classification.csv" or "session_boundary" in record["ai_post"]["shared"]]
            schemas.append((suffix, ["id", *([identity] if identity else []), *fields], data))
        for suffix, fields, data in schemas:
            with Path(str(native) + suffix).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                writer.writerows(data)
        return native

    def handoff_fixture(self):
        header, records = ai_fixture(1)
        header["local_handoff_manifest"] = FLEET.LOCAL_HANDOFF_MANIFEST
        header["qualifying"] = True
        config = bytearray.fromhex(header["configuration_hex"])
        struct.pack_into("<I", config, 0x20, 0)
        header["configuration_hex"] = config.hex()
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            records[0][name]["session_state"] = 4
        pre = {"boundary": "402c80-entry", "gameplay_hex": records[0]["pre"]["gameplay_hex"],
               "dynamics_hex": records[0]["pre"]["dynamics_hex"], "rng": 123, "ai_tire_base": 777}
        post = {**pre, "boundary": "402c80-return"}
        game = bytearray.fromhex(post["gameplay_hex"])
        struct.pack_into("<I", game, 8, 1)
        post["gameplay_hex"] = game.hex()
        dynamics = bytearray.fromhex(post["dynamics_hex"])
        struct.pack_into("<8i", dynamics, 0xdc, 0, 0, 0, 0, 0xccc, 0, 0, 0)
        post["dynamics_hex"] = dynamics.hex()
        records[0]["local_handoffs"] = [{"kind": 1, "actor": 0, "return_address": 0x421827,
                                            "stack_hex": struct.pack("<2I", 0x421827, 0).hex(), "pre": pre, "post": post}]
        return header, records

    def write_handoff_csv(self, records, path):
        fields = ["id", "event", "actor", "kind", "return_address", "rng_final", *FLEET.LOCAL_HANDOFF_FIELDS]
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            for record in records:
                for index, event in enumerate(record["local_handoffs"]):
                    writer.writerow({"id": record["id"], "event": index, "actor": event["actor"], "kind": event["kind"],
                                     "return_address": event["return_address"], "rng_final": event["post"]["rng"],
                                     **FLEET.local_handoff_values(event["post"])})

    def timing_csv(self, records, finish=False):
        native = self.path.with_suffix(".native.csv")
        for suffix, identity, count, expected in ((".checkpoint.csv", "slot", 48, FLEET.expected_checkpoint),
                                                (".ai-timing.csv", "actor", 16, lambda state, actor: FLEET.expected_ai_timing(state, actor, finish))):
            with Path(str(native) + suffix).open("w", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=("id", identity, *expected(records[0]["ai_post"], 0)))
                writer.writeheader()
                writer.writerows({"id": record["id"], identity: item, **expected(record["ai_post"], item)}
                                 for record in records for item in range(count))
        return native

    def finish_csv(self, records):
        native = self.path.with_suffix(".native.csv")
        with Path(str(native) + ".finish.csv").open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=("id", *FLEET.expected_finish(records[0]["ai_post"])))
            writer.writeheader()
            writer.writerows({"id": record["id"], **FLEET.expected_finish(record["ai_post"])} for record in records)
        with Path(str(native) + ".split-display.csv").open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=("id", "player", *FLEET.expected_split_display(records[0]["ai_post"], 0)))
            writer.writeheader()
            writer.writerows({"id": record["id"], "player": player, **FLEET.expected_split_display(record["ai_post"], player)}
                             for record in records for player in range(4))
        return native

    def ai_rows(self, records):
        return [{"id": record["id"], "actor": actor,
                 **FLEET.expected_ai_route(record["ai_post"], actor)} for record in records for actor in range(16)]

    def ai_csv(self, rows, unsupported=()):
        path = self.path.with_suffix(".ai-route.csv")
        with path.open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=("id", "actor", *FLEET.expected_ai_route(state("403dd0-return"), 0)))
            writer.writeheader()
            writer.writerows(rows)
        with self.path.with_suffix(".ai-route-unsupported.csv").open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=("id", "reason"))
            writer.writeheader()
            writer.writerows(unsupported)
        return path
