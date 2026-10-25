#!/usr/bin/env python3
"""Validate an original recording and replay its consumed inputs and time steps.

Sequential replay imports the initial vehicle state once. Independent replay
starts each update from its recorded snapshot to isolate individual differences.
Neither mode establishes an unperturbed wall-clock baseline.
"""

import argparse
import csv
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

PROJECT = Path(__file__).resolve().parent.parent
LAYOUT = {"body_base": 0x4CD760, "body_stride": 0x53C,
          "dynamics_base": 0x4C5DA0, "dynamics_stride": 0x338,
          "gameplay_base": 0x4CC518, "gameplay_stride": 0x124}
BOUNDARIES = {"pre": "421aa0-entry", "consumed": "415090-entry", "post": "421aa0-return"}
INPUT_OFFSETS = {"left": 0x138, "right": 0x13C, "throttle": 0x140, "brake": 0x144, "gear_edge": 0x14C}


def integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"Invalid {label}: {value!r}")
    return value


def word(data, offset, form="i"):
    return struct.unpack_from("<" + form, data, offset)[0]


def snapshot(value, boundary):
    if value["boundary"] != boundary:
        raise ValueError(f"Expected boundary {boundary}, got {value['boundary']}")
    integer(value["actor"], 0, 15, "actor")
    for name in ("body", "dynamics", "gameplay"):
        raw = bytes.fromhex(value[name + "_hex"])
        if len(raw) != LAYOUT[name + "_stride"]:
            raise ValueError(f"Incorrect {name} record size")
        value[name] = raw
    integer(value["delta_q16"], 1, 0x30000, "source delta")
    integer(value["reciprocal_q16"], 1, 0x7FFFFFFF, "source inverse delta")
    if type(value["extra_update"]) is not bool:
        raise ValueError("extra_update must be boolean")
    for name in ("rng", "qpc_low", "update_counter"):
        integer(value[name], 0, 0xFFFFFFFF, name)
    integer(value["qpc_divisor"], 1, 0x7FFFFFFF, "QPC divisor")
    integer(value["limiter_active"], 0, 1, "limiter flag")
    integer(value["limiter_load_q16"], -0x80000000, 0x7FFFFFFF, "limiter load")
    integer(value["roughness_cursor"], 0, 14, "roughness cursor")
    integer(value["steering_profile"], 1, 3, "steering profile")
    for name, offset in INPUT_OFFSETS.items():
        if value["input_q16"][name] != word(value["dynamics"], offset, "b" if name == "gear_edge" else "i"):
            raise ValueError(f"Decoded {name} differs from the recorded consumed field")
    if value["car_id"] != value["body"][0x516]:
        raise ValueError("Decoded car ID differs from body +516")
    if value["transmission_mode"] != word(value["dynamics"], 0x27C):
        raise ValueError("Decoded transmission differs from dynamics +27c")
    return value


def read_trace(path):
    lines = Path(path).read_bytes().splitlines(keepends=True)
    if len(lines) < 3 or not all(line.endswith(b"\n") for line in lines):
        raise ValueError("Recording is empty or truncated")
    header = json.loads(lines[0])
    if header.get("type") != "header" or header.get("schema") != 1 or header.get("exe_crc32") != "8ce56d41":
        raise ValueError("Expected canonical TOCA 1.1a trace schema 1")
    if header["layout"] != LAYOUT or header["boundaries"] != BOUNDARIES:
        raise ValueError("Unknown source layout or update boundaries")
    footer = json.loads(lines[-1])
    checksum = f"{zlib.crc32(b''.join(lines[:-1])):08x}"
    if footer.get("type") != "complete" or footer.get("prefix_crc32") != checksum:
        raise ValueError("Missing completion footer or incorrect trace CRC32")
    records = []
    for index, line in enumerate(lines[1:-1]):
        record = json.loads(line)
        if record.get("type") != "update" or record.get("id") != index:
            raise ValueError("Missing, repeated or out-of-order update")
        for name, boundary in BOUNDARIES.items():
            snapshot(record[name], boundary)
        pre, consumed, post = (record[name] for name in BOUNDARIES)
        if len({state["actor"] for state in (pre, consumed, post)}) != 1:
            raise ValueError("Actor changed within update")
        if records and pre["actor"] != records[0]["pre"]["actor"]:
            raise ValueError("Actor changed between updates")
        if "source_mode" in header:
            if header["source_mode"] != {"single-race": 0, "time-trial": 2}.get(header.get("session")):
                raise ValueError("Source mode differs from the recorded session")
            for state in (pre, consumed, post):
                if state["actor"] != header["local_actor"] or any(state[field] != header[field] for field in ("source_mode", "allocated_actor_count")):
                    raise ValueError("Session actor configuration changed within recording")
        if consumed["update_counter"] != ((pre["update_counter"] + 1) & 0xFFFFFFFF) or post["update_counter"] != consumed["update_counter"]:
            raise ValueError("Missed source update boundary")
        for field in ("delta_q16", "reciprocal_q16", "extra_update"):
            if len({state[field] for state in (pre, consumed, post)}) != 1:
                raise ValueError(f"Timing changed within update: {field}")
        for field in ("car_id", "weather", "track_id", "steering_profile", "transmission_mode"):
            if any(state[field] != header["initial_configuration"][field] for state in (pre, consumed, post)):
                raise ValueError(f"Configuration changed within recording: {field}")
        if records and pre["update_counter"] != records[-1]["post"]["update_counter"]:
            raise ValueError("An original update was skipped between records")
        records.append(record)
    if footer["updates"] != len(records):
        raise ValueError("Completion count differs from recorded updates")
    return header, records


def write_binary(path, header, records):
    config = header["initial_configuration"]
    car = integer(config["car_id"], 0, 7, "normal car ID")
    weather = integer(config["weather"], 1, 5, "resolved weather")
    mode = integer(config["transmission_mode"], 1, 2, "player transmission mode")
    integer(config["track_id"], 0, 9, "track ID")
    with Path(path).open("wb") as output:
        output.write(struct.pack("<8s5I", b"TOCAREPL", 1, car, weather, int(mode == 1), len(records)))
        for record in records:
            state = record["consumed"]
            output.write(struct.pack("<IiiIIIiII", record["id"], state["delta_q16"], state["reciprocal_q16"],
                                     state["extra_update"], state["rng"], state["limiter_active"],
                                     state["limiter_load_q16"], state["roughness_cursor"], state["steering_profile"]))
            for name in ("body", "dynamics", "gameplay"):
                output.write(state[name])


def expected_state(snapshot):
    body, dynamics = snapshot["body"], snapshot["dynamics"]
    fields = {}
    for axis, offset in zip("xyz", (0xBC, 0xC0, 0xC4)):
        fields["position_" + axis] = word(body, offset)
        fields["velocity_" + axis] = word(body, offset + 12)
    fields["speed"] = word(body, 0xE0)
    fields["contact_count"] = body[0x51F]
    for row in range(3):
        for column in range(3):
            fields[f"basis_{row}{column}"] = word(body, 0x150 + row * 12 + column * 4)
    for name, offset in (("throttle", 0x11C), ("engine_speed", 0x124), ("brake", 0x130), ("steering", 0x150)):
        fields[name] = word(dynamics, offset)
    fields["steering_angle"] = word(dynamics, 0x274, "h")
    fields["visual_local_x"] = word(dynamics, 0x278, "h")
    fields["visual_absolute_z"] = word(dynamics, 0x27a, "h")
    fields["gear"] = word(dynamics, 0x330, "b")
    for wheel in range(4):
        fields[f"surface_{wheel}"] = dynamics[0x328 + wheel]
        fields[f"contact_{wheel}"] = int(body[0x517 + wheel] == 0)
        fields[f"wheel_speed_{wheel}"] = word(dynamics, 0xFC + wheel * 4)
        fields[f"longitudinal_slip_{wheel}"] = word(dynamics, 0x1E4 + wheel * 4)
        fields[f"lateral_slip_{wheel}"] = word(dynamics, 0x1F4 + wheel * 4)
    for axis in range(3):
        fields[f"angular_velocity_{axis}"] = word(body, 0xE4 + axis * 4)
    fields["boundary"] = 0
    return fields


def compare(records, csv_path, mode="independent"):
    with Path(csv_path).open(newline="") as source:
        reader = csv.DictReader(source)
        rows = list(reader)
    if len(rows) != len(records):
        raise ValueError("Native output did not contain every recorded update")
    mismatches = []
    maxima = {}
    vehicle_contacts = []
    roughness_advances = []
    for record, row in zip(records, rows):
        if int(row["id"]) != record["id"]:
            raise ValueError("Native output update IDs differ")
        expected = expected_state(record["post"])
        body = record["post"]["body"]
        contact_count = body[0x529]
        if contact_count:
            partners = list(body[0x52A:0x52A + contact_count])
            if len(partners) != contact_count or any(partner > 15 for partner in partners):
                raise ValueError("Invalid original vehicle contact list")
            vehicle_contacts.append({"update": record["id"], "partners": partners})
        # 42e6f0 advances the shared 15-entry roughness cursor once for each
        # grass/gravel body support above 0x8000 speed. Other actors run the
        # same ground completion inside 421980; their advances are not inputs
        # to this one-vehicle replay. The observed remainder is modulo15.
        own_advances = sum(surface in (2, 3) for surface in record["post"]["dynamics"][0x328:0x330]) if word(body, 0xE0) > 0x8000 else 0
        external = (record["post"]["roughness_cursor"] - record["consumed"]["roughness_cursor"] - own_advances) % 15
        if external:
            roughness_advances.append({"update": record["id"], "external_advance_modulo15": external})
        for field, original in expected.items():
            actual = int(row[field])
            difference = actual - original
            maxima[field] = max(maxima.get(field, 0), abs(difference))
            if difference:
                mismatches.append({"update": record["id"], "field": field,
                                   "original": original, "native": actual, "difference": difference})
    return {"updates": len(records), "fields_per_update": len(expected),
            "mismatch_count": len(mismatches), "max_absolute_difference": maxima,
            "first_mismatches": mismatches[:40],
            "exact_prefix_updates": mismatches[0]["update"] if mismatches else len(records),
            "mode": "continuous-consumed-input-updates" if mode == "sequential" else "independent-consumed-snapshot-updates",
            "initial_state_imports": 1 if mode == "sequential" else len(records),
            "world_scope": ["local_vehicle", "track_barriers"] +
                           (["persistent_canonical_props"] if mode == "sequential" else []),
            "world_assumptions": (["props_start_at_canonical_initial_state"] if mode == "sequential" else
                                  ["prop_controller_state_not_imported"]),
            "unmodeled_original_vehicle_contacts": vehicle_contacts,
            "unmodeled_original_roughness_advances": roughness_advances,
            "parity": not mismatches}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--replay", type=Path, default=PROJECT / "build/dev/toca-reference-replay")
    parser.add_argument("--data-root", type=Path, default=PROJECT / "references/toca-1.1a")
    parser.add_argument("--output", type=Path, help="artifact directory; defaults to recording directory")
    parser.add_argument("--mode", choices=("sequential", "independent"), default="sequential",
                        help="retain native state across updates, or isolate each update from its source snapshot")
    args = parser.parse_args()
    header, records = read_trace(args.trace)
    directory = args.output or args.trace.resolve().parent
    directory.mkdir(parents=True, exist_ok=True)
    binary, native = directory / "replay.bin", directory / f"native-{args.mode}.csv"
    write_binary(binary, header, records)
    subprocess.run([str(args.replay.resolve()), str(args.data_root.resolve()),
                    str(header["initial_configuration"]["track_id"]), str(binary), str(native), args.mode],
                   check=True, timeout=60)
    report = compare(records, native, args.mode)
    report["exe_crc32"] = header["exe_crc32"]
    report["trace_crc32"] = f"{zlib.crc32(args.trace.read_bytes()):08x}"
    for field in ("session", "source_mode", "local_actor", "allocated_actor_count", "active_ground_actor_count"):
        if field in header:
            report[field] = header[field]
    report_path = directory / f"comparison-{args.mode}.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{report['updates']} original updates replayed ({args.mode}); {report['mismatch_count']} field differences; "
          f"{report['exact_prefix_updates']} consecutive exact updates. {report_path}")
    return 0 if report["parity"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Reference replay failed: {error}", file=sys.stderr)
        sys.exit(2)
