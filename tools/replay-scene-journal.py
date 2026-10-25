#!/usr/bin/env python3
"""Compare recorded scene operations or generated physical-boundary consumers."""

import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
import zlib

import reference_fleet as FLEET
import reference_scene_journal as SCENE
import reference_scene_frames as FRAMES


PROJECT = Path(__file__).resolve().parent.parent


def positive_seconds(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("native timeout must be positive and finite")
    return number


def compare(expected_rows, path):
    fields = ["order", "group", "index", "field", "value"]
    count, mismatch_count, mismatches = 0, 0, []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != fields:
            raise ValueError("Unexpected native scene CSV header")
        for expected in expected_rows:
            raw = next(reader, None)
            if raw is None:
                raise ValueError("Missing native scene result")
            actual = (int(raw["order"]), raw["group"], int(raw["index"]), raw["field"], int(raw["value"]))
            if actual[:4] != expected[:4]:
                raise ValueError("Reordered native scene result")
            count += 1
            if actual[4] != expected[4]:
                mismatch_count += 1
                if len(mismatches) < 64:
                    mismatches.append({"order": expected[0], "group": expected[1], "index": expected[2],
                                       "field": expected[3], "expected": expected[4], "actual": actual[4]})
        if next(reader, None) is not None:
            raise ValueError("Extra native scene result")
    return {"numerical_parity": mismatch_count == 0, "compared_values": count,
            "mismatch_count": mismatch_count, "first_mismatches": mismatches}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("journal", type=Path)
    parser.add_argument("--replay", type=Path, default=PROJECT / "build/dev/toca-scene-journal-replay")
    parser.add_argument("--data-root", type=Path, default=PROJECT / "build/dev/generated/original-data")
    parser.add_argument("--output", type=Path)
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument("--frames", type=Path, help="Generate camera and refresh cadence from the joined physical-frame sidecar")
    profile.add_argument("--qualifying-consumers", action="store_true",
                         help="Use a QualOn fleet trace and its bound warmup log; compare the authored7/8 physical-consumer suffix")
    parser.add_argument("--native-timeout", type=positive_seconds, default=120)
    args = parser.parse_args()
    destination = args.output or args.journal.parent / ("qualifying-scene-replay" if args.qualifying_consumers else "scene-frame-replay" if args.frames else "scene-replay")
    destination.mkdir(parents=True, exist_ok=True)
    report_path = destination / "scene-comparison.json"
    report = {"parity": False, "source": str(args.journal), "scope": {
        "camera_generation": False, "physics_autonomy": False,
        "scene_schedule": "actual observed init/migrate/select/model/publish operations",
        "roots": "sixteen physical car parents and one follow anchor",
        "initial_neighbors": "unclaimed until each root's actual first migration",
        "model_profile": "normal single-player View2/factor0; canonical retained refresh request1"}}
    try:
        binary = destination / "scene-inputs.bin"
        native = destination / "scene-native.csv"
        command = [str(args.replay)]
        if args.qualifying_consumers:
            header, records = FLEET.read_trace(args.journal)
            name = header.get("unrecorded_keyboard_warmup", {}).get("targeted_local_finish", {}).get("decision_log")
            if not isinstance(name, str) or not name or Path(name).name != name:
                raise ValueError("Qualifying warmup must be the bound sibling decision log")
            inputs = FRAMES.qualifying_consumer_inputs(header, records, args.journal.with_name(name))
            checksum = 0
            with args.journal.open("rb") as source:
                for raw in source:
                    checksum = zlib.crc32(raw, checksum)
                    last = raw
            report.update(source_prefix_crc32=json.loads(last)["prefix_crc32"], source_crc32=f"{checksum:08x}",
                source_updates=len(records), native_inputs=FRAMES.write_qualifying_frames_binary(binary, inputs),
                scene_input_imports=0, coverage={"passed": True, **inputs["scope"]})
            report["scope"] = {**inputs["scope"], "camera_generation": True,
                "scene_schedule": "observed raw4d3848 primary/extra boundaries; publication follows each primary physical call",
                "roots": "canonical authored cameras, sixteen car parents and ordinary camera anchors",
                "model_profile": "qualifying active quarter and ordinary View2; source masks remain expectations"}
            expectations = FRAMES.expected_qualifying_consumer_rows(records, inputs)
            complete = True
            command.append("--frames")
        else:
            header, events, footer = SCENE.read(args.journal)
            report.update(source_prefix_crc32=footer["prefix_crc32"], coverage=footer["coverage"], events=len(events))
            expectations = SCENE.expected_rows(events)
            complete = footer["coverage"]["passed"]
        if args.frames:
            frame_header, frames, frame_footer = FRAMES.read_frames(args.frames, header, events)
            report.update(frame_source=str(args.frames), frame_prefix_crc32=frame_footer["prefix_crc32"],
                          frame_coverage=frame_footer["coverage"], frames=len(frames),
                          native_inputs=FRAMES.write_frames_binary(binary, frame_header, frames, events))
            report["scope"].update(camera_generation=True,
                                  scene_schedule="source clock: primary publication before optional extra physics",
                                  physical_states="explicit pre/post physical boundary; no physical autonomy")
            complete = complete and frame_footer["coverage"]["passed"]
            expectations = FRAMES.expected_frame_rows(frames, events)
            command.append("--frames")
        elif not args.qualifying_consumers:
            report["native_inputs"] = SCENE.write_binary(binary, header, events)
        execute = subprocess.run(command + [str(args.data_root), str(binary), str(native)],
                                 text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=args.native_timeout)
        (destination / "native.log").write_text(execute.stdout + execute.stderr)
        report["native_returncode"] = execute.returncode
        if execute.returncode:
            raise ValueError("Native scene replay rejected the input: " + execute.stderr.strip())
        report.update(compare(expectations, native))
        report["parity"] = report["numerical_parity"] and complete
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("parity", "compared_values", "mismatch_count", "error") if key in report}))
    return 0 if report["parity"] else 1


if __name__ == "__main__":
    sys.exit(main())
