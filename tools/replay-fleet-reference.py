#!/usr/bin/env python3
"""Compare all16 original physical actors; autonomous AI is outside these modes.

continuous-physics-inputs imports one initial world, then uses observed controls,
body modes and registration for each tick. independent-physics reimports each
consumed snapshot and must not be presented as a continuous replay.
continuous-derived-body-selection derives dispatch from retained native physics
and observed pre-update race metadata; route/AI decisions remain observed inputs.
"""

import argparse
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import zlib

PROJECT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("reference_fleet", PROJECT / "tools/reference_fleet.py")
FLEET = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FLEET)
SCENE_SPEC = importlib.util.spec_from_file_location("reference_scene_journal", PROJECT / "tools/reference_scene_journal.py")
SCENE = importlib.util.module_from_spec(SCENE_SPEC)
SCENE_SPEC.loader.exec_module(SCENE)


def file_crc32(path):
    checksum = 0
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            checksum = zlib.crc32(block, checksum)
    return f"{checksum:08x}"


def source_prefix_crc32(path):
    with path.open("rb") as source:
        source.seek(0, 2)
        size = source.tell()
        source.seek(max(0, size - 2 * 1024 * 1024))
        tail = source.read().splitlines()
    footer = json.loads(tail[-1])
    value = footer.get("prefix_crc32")
    if footer.get("type") != "complete" or not isinstance(value, str) or len(value) != 8 or any(c not in "0123456789abcdef" for c in value):
        raise ValueError("Lifecycle scene comparison requires the complete source CRC32 prefix")
    return value


def compare_scene_rows(expected_rows, path):
    import csv
    count, differences, first = 0, 0, []
    with path.open(newline="") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != ["order", "group", "index", "field", "value"]:
            raise ValueError("Invalid autonomous scene CSV header")
        for expected in expected_rows:
            row = next(reader, None)
            if row is None: raise ValueError("Missing autonomous scene output")
            actual = (int(row["order"]), row["group"], int(row["index"]), row["field"], int(row["value"]))
            if actual[:4] != expected[:4]: raise ValueError("Reordered autonomous scene output")
            count += 1
            if actual[4] != expected[4]:
                differences += 1
                if len(first) < 64: first.append({"order": expected[0], "group": expected[1], "index": expected[2], "field": expected[3], "expected": expected[4], "actual": actual[4]})
        if next(reader, None) is not None: raise ValueError("Extra autonomous scene output")
    return {"numerical_parity": differences == 0, "compared_values": count, "mismatch_count": differences, "first_mismatches": first}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--replay", type=Path, default=PROJECT / "build/fleet-reference/toca-fleet-reference-replay")
    parser.add_argument("--native-timeout", type=float, default=120,
                        help="positive finite native replay timeout in seconds (default: 120)")
    parser.add_argument("--data-root", type=Path, default=PROJECT / "references/toca-1.1a")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--updates", type=int, help="compare a measured prefix without changing the original recording")
    parser.add_argument("--mode", choices=FLEET.REPLAY_MODES, default="continuous-physics-inputs")
    parser.add_argument("--compare-ai-route", action="store_true",
                        help="also compare independent403dd0 route progression/recovery/cache; requires --ai-state source provenance")
    parser.add_argument("--compare-ai-timing", action="store_true",
                        help="with --compare-ai-route, compare checkpoint table and nonfinish lap timing from --checkpoint-timing provenance")
    parser.add_argument("--compare-ai-finish", action="store_true",
                        help="with --compare-ai-timing, compare ordinary AI finish and shared split display from --finish-timing provenance")
    parser.add_argument("--compare-local-handoffs", action="store_true", help="compare recorded local402be0/402c80 entry-return transactions independently")
    parser.add_argument("--local-handoffs-only", action="store_true", help="with --compare-local-handoffs: run only captured controller transactions, without physical/session replay")
    parser.add_argument("--compare-session-boundaries", action="store_true", help="independently compare captured checkpoint/timer/local lap caller/classification effects only; no route, RNG or physical comparison")
    parser.add_argument("--compare-traffic-controls", action="store_true", help="independently compare actual post-route ascendingtraffic and descendingAI axes; no upstream route or physical comparison")
    parser.add_argument("--compare-nearest-recovery", action="store_true", help="independently compare every captured human4046a3..404731 check and initialized nearest-map output; requires an actual cursor jump")
    parser.add_argument("--compare-joined-ai", action="store_true", help="compare linked route/rank/traffic/descending AI controls from one403dd0 PRE world/RNG perupdate, with source-derived release counter")
    parser.add_argument("--compare-skid-dispatch", action="store_true", help="compare independent actual44c540 PRE queues and424d50 source numeric/sample/RNG decisions; no frame/physics parity")
    parser.add_argument("--compare-wheel-queue", action="store_true", help="derive retained queues/flags through all observed446b50 callbacks and following44c540 dispatchers; physical/schedule/audio inputs remain observed")
    parser.add_argument("--compare-whine-rng", action="store_true", help="derive retained WHINE numeric state and each complete inter-update RNG interval through actual raw physical caller inputs and voice/mix gates")
    parser.add_argument("--compare-combined-rng", action="store_true", help="one initial shared RNG across joined AI, retained tire queues/material/mute state and all actual WHINE/frame callbacks; physical/context/driver inputs remain observed")
    parser.add_argument("--compare-driver-retention", action="store_true", help="retain initialized driver/gameplay/braking state; enriched publication captures also derive body4e8 from retained raw brake through actual4608b0 calls; later snapshots are expectations only")
    parser.add_argument("--compare-driver-physics", action="store_true",
                        help="feed retained driver outputs into independent observed-PRE physical transactions; one shared RNG, no continuous physics claim")
    parser.add_argument("--compare-drivetrain-feedback", action="store_true",
                        help="retain generated physical gear/pending/reverse feedback through drivers and actual callbacks; remaining physical PRE stays observed")
    parser.add_argument("--compare-rpm-feedback", action="store_true",
                        help="retain generated physical RPM124/128 through following physical PRE, launch and actual engine/WHINE callbacks")
    parser.add_argument("--compare-limiter-feedback", action="store_true",
                        help="retain generated actor limiter flag4be150/load4be108 with RPM/drivetrain/driver feedback and one RNG; remaining physical PRE stays observed")
    parser.add_argument("--compare-pedal-feedback", action="store_true",
                        help="retain six physical throttle/brake filter words with limiter/RPM/drivetrain/driver and shared RNG; remaining physical PRE stays observed")
    parser.add_argument("--compare-control-feedback", action="store_true",
                        help="retain steering cached44/accumulator150/angle274 with pedal/limiter/RPM/drivetrain/driver feedback and shared RNG")
    parser.add_argument("--compare-physical-feedback", action="store_true",
                        help="join continuous retained Motion/world state with controls, drivers and one shared RNG")
    parser.add_argument("--compare-dispatch-feedback", action="store_true",
                        help="derive pre-AI body dispatch from retained Motion/driver state with full physical/control/shared RNG feedback")
    parser.add_argument("--compare-qualifying-startup", action="store_true",
                        help="compare init-only cold443f60 setup, creator clock and first State4 PRE")
    parser.add_argument("--compare-cold-grid-init", action="store_true",
                        help="standalone actual460170 entry→return comparison using canonical resources and typed entry inputs; requires --cold-grid-init capture")
    parser.add_argument("--compare-cold-grid-sequence", action="store_true",
                        help="derive both actual startup initializations from first typed entry only, then compare named first-PRE state")
    parser.add_argument("--compare-startup-join", action="store_true",
                        help="derive the first retained-driver/shared-RNG world from actual two-call startup evidence on this complete interval")
    parser.add_argument("--compare-startup-first-selection", action="store_true",
                        help="derive the first pre-AI body selection from generated startup; later body modes remain observed")
    parser.add_argument("--compare-startup-physical-body", action="store_true",
                        help="generate only the first physical PRE BODY writer subset from actual startup, then reuse continuous dispatch/physical feedback")
    parser.add_argument("--compare-startup-control-calibration", action="store_true",
                        help="derive eight canonical first-PRE Controls calibration words and compare the restored consumer, with startup BODY/physical feedback")
    parser.add_argument("--compare-startup-contact-cache", action="store_true",
                        help="derive canonical first-PRE contact heights/normal/triangle/surface caches and compare the restored Motion consumer")
    parser.add_argument("--compare-startup-scene", action="store_true",
                        help="join autonomous cold physical replay and generated scene frames; scene sidecars are expectations only")
    parser.add_argument("--compare-startup-scene-lifecycle", action="store_true",
                        help="generate finish/results/warm scene state and compare retained source consumer checkpoints; no scene sidecars or camera matrix parity")
    parser.add_argument("--scene-journal", type=Path, help="actual scene expectation journal (default: trace sibling scene-journal.jsonl)")
    parser.add_argument("--scene-frames", type=Path, help="joined frame expectations (default: trace sibling scene-frames.jsonl)")
    parser.add_argument("--compare-startup-world-reset", action="store_true",
                        help="generate cold actor constructor context/identity and mutable contact/damage/prop world; compare actual consumers while root/visibility and later frame ownership stay external")
    args = parser.parse_args()
    if not math.isfinite(args.native_timeout) or args.native_timeout <= 0:
        parser.error("--native-timeout must be positive and finite")
    for feature in ("startup_join", "startup_first_selection", "startup_physical_body", "startup_control_calibration", "startup_contact_cache", "startup_world_reset", "startup_scene", "startup_scene_lifecycle", "qualifying_startup"):
        if getattr(args, "compare_" + feature) and (args.updates is not None or args.local_handoffs_only or any(
                value for name, value in vars(args).items() if name.startswith("compare_") and name != "compare_" + feature)):
            parser.error("--compare-" + feature.replace("_", "-") + " is standalone and requires a complete source interval")
    if (args.scene_journal or args.scene_frames) and not args.compare_startup_scene:
        parser.error("Scene expectation paths require --compare-startup-scene")
    if args.compare_startup_scene or args.compare_startup_scene_lifecycle: args.compare_startup_world_reset = True
    if args.compare_startup_world_reset:
        args.compare_startup_contact_cache = True
    if args.compare_startup_contact_cache:
        args.compare_startup_control_calibration = True
    if args.compare_startup_control_calibration:
        args.compare_startup_physical_body = True
    if args.compare_startup_physical_body:
        args.compare_startup_first_selection = True
        args.compare_dispatch_feedback = True
    if args.compare_startup_first_selection:
        args.compare_startup_join = True
    if args.compare_cold_grid_sequence and (args.updates is not None or args.local_handoffs_only or any(
            value for name, value in vars(args).items() if name.startswith("compare_") and name != "compare_cold_grid_sequence")):
        parser.error("--compare-cold-grid-sequence is standalone and excludes update prefixes/other comparisons")
    if args.compare_cold_grid_init and (args.updates is not None or args.local_handoffs_only or any(
            value for name, value in vars(args).items() if name.startswith("compare_") and name != "compare_cold_grid_init")):
        parser.error("--compare-cold-grid-init is standalone and excludes update prefixes/other comparisons")
    if args.compare_startup_join:
        args.compare_driver_retention = True
    if args.compare_dispatch_feedback:
        args.compare_physical_feedback = True
    if args.compare_physical_feedback:
        args.compare_control_feedback = True
    if args.compare_control_feedback:
        args.compare_pedal_feedback = True
    if args.compare_pedal_feedback:
        args.compare_limiter_feedback = True
    if args.compare_limiter_feedback:
        args.compare_rpm_feedback = True
    if args.compare_rpm_feedback:
        args.compare_drivetrain_feedback = True
    if args.compare_drivetrain_feedback:
        args.compare_driver_physics = True
    if args.compare_driver_physics:
        args.compare_driver_retention = True
    if args.compare_driver_retention:
        args.compare_combined_rng = True
    if args.compare_combined_rng and any((args.compare_whine_rng, args.compare_wheel_queue, args.compare_skid_dispatch, args.compare_joined_ai, args.compare_nearest_recovery, args.compare_traffic_controls, args.compare_session_boundaries, args.compare_ai_route, args.compare_ai_timing, args.compare_ai_finish, args.compare_local_handoffs, args.local_handoffs_only)):
        parser.error("--compare-combined-rng selects its component comparisons internally")
    if args.compare_whine_rng and any((args.compare_wheel_queue, args.compare_skid_dispatch, args.compare_joined_ai, args.compare_nearest_recovery, args.compare_traffic_controls, args.compare_session_boundaries, args.compare_ai_route, args.compare_ai_timing, args.compare_ai_finish, args.compare_local_handoffs, args.local_handoffs_only)):
        parser.error("--compare-whine-rng runs independently of other replay comparisons")
    if args.compare_wheel_queue and any((args.compare_skid_dispatch, args.compare_joined_ai, args.compare_nearest_recovery, args.compare_traffic_controls, args.compare_session_boundaries, args.compare_ai_route, args.compare_ai_timing, args.compare_ai_finish, args.compare_local_handoffs, args.local_handoffs_only)):
        parser.error("--compare-wheel-queue runs independently of other replay comparisons")
    if args.compare_skid_dispatch and any((args.compare_joined_ai, args.compare_nearest_recovery, args.compare_traffic_controls, args.compare_session_boundaries, args.compare_ai_route, args.compare_ai_timing, args.compare_ai_finish, args.compare_local_handoffs, args.local_handoffs_only)):
        parser.error("--compare-skid-dispatch runs independently of other replay comparisons")
    if args.compare_joined_ai and (args.compare_nearest_recovery or args.compare_traffic_controls or args.compare_session_boundaries or args.compare_ai_route or args.compare_ai_timing or args.compare_ai_finish or args.compare_local_handoffs or args.local_handoffs_only):
        parser.error("--compare-joined-ai runs independently of other replay comparisons")
    if args.compare_nearest_recovery and (args.compare_traffic_controls or args.compare_session_boundaries or args.compare_ai_route or args.compare_ai_timing or args.compare_ai_finish or args.compare_local_handoffs or args.local_handoffs_only):
        parser.error("--compare-nearest-recovery runs independently of other replay comparisons")
    if args.compare_traffic_controls and (args.compare_session_boundaries or args.compare_ai_route or args.compare_ai_timing or args.compare_ai_finish or args.compare_local_handoffs or args.local_handoffs_only):
        parser.error("--compare-traffic-controls runs independently of other replay comparisons")
    if args.compare_session_boundaries and (args.compare_ai_route or args.compare_ai_timing or args.compare_ai_finish or args.compare_local_handoffs or args.local_handoffs_only):
        parser.error("--compare-session-boundaries runs independently of other replay comparisons")
    if args.local_handoffs_only and (not args.compare_local_handoffs or args.compare_ai_route or args.compare_ai_timing or args.compare_ai_finish):
        parser.error("--local-handoffs-only requires --compare-local-handoffs and excludes AI route/timing/finish comparisons")
    if args.compare_ai_timing and not args.compare_ai_route:
        parser.error("--compare-ai-timing requires --compare-ai-route")
    if args.compare_ai_finish and not args.compare_ai_timing:
        parser.error("--compare-ai-finish requires --compare-ai-timing")
    directory = args.output or args.trace.resolve().parent
    directory.mkdir(parents=True, exist_ok=True)
    report_mode = "qualifying-startup" if args.compare_qualifying_startup else "startup-scene-lifecycle" if getattr(args, "compare_startup_scene_lifecycle", False) else "startup-scene" if getattr(args, "compare_startup_scene", False) else "startup-world-reset" if args.compare_startup_world_reset else "startup-contact-cache" if args.compare_startup_contact_cache else "startup-control-calibration" if args.compare_startup_control_calibration else "startup-physical-body" if args.compare_startup_physical_body else "startup-first-selection" if args.compare_startup_first_selection else "startup-retained-driver-rng" if args.compare_startup_join else "cold-grid-sequence" if args.compare_cold_grid_sequence else "cold-grid-initialization" if args.compare_cold_grid_init else "retained-dispatch-feedback" if args.compare_dispatch_feedback else "retained-physical-feedback" if args.compare_physical_feedback else "retained-control-feedback" if args.compare_control_feedback else "retained-pedal-feedback" if args.compare_pedal_feedback else "retained-limiter-feedback" if args.compare_limiter_feedback else "retained-rpm-feedback" if args.compare_rpm_feedback else "retained-drivetrain-feedback" if args.compare_drivetrain_feedback else "retained-driver-independent-physics" if args.compare_driver_physics else "retained-driver-rng" if args.compare_driver_retention else "combined-rng" if args.compare_combined_rng else "retained-whine-rng" if args.compare_whine_rng else "retained-wheel-queue" if args.compare_wheel_queue else "independent-skid-dispatch" if args.compare_skid_dispatch else args.mode
    path = directory / f"fleet-comparison-{report_mode}.json"
    # Every invocation owns its report, including failures before native replay.
    path.unlink(missing_ok=True)
    try:
        return replay(args, directory, path)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        path.write_text(json.dumps({"status": "error", "parity": False, "mode": report_mode,
                                    "reason": str(error)}, indent=2) + "\n")
        print(f"Fleet replay failed: {error}. {path}", file=sys.stderr)
        return 2


def replay(args, directory, path):
    if getattr(args, "compare_qualifying_startup", False):
        capture = FLEET.read_qualifying_startup(args.trace)
        binary, native = directory / "qualifying-startup-inputs.bin", directory / "qualifying-startup-native.csv"
        FLEET.write_qualifying_startup_binary(binary, capture)
        command = [str(args.replay.resolve()), "--compare-qualifying-startup", str(args.data_root.resolve()), str(binary), str(native)]
        execution = subprocess.run(command, capture_output=True, text=True, timeout=args.native_timeout)
        if execution.returncode:
            raise ValueError(execution.stderr.strip() or "Native qualifying startup comparison failed")
        comparison = FLEET.compare_qualifying_startup(capture, native)
        report = {"status": "compared", "mode": "qualifying-startup", **comparison,
            "exe_crc32": capture["header"]["exe_crc32"], "trace_crc32": file_crc32(args.trace),
            "source_prefix_crc32": capture["footer"]["prefix_crc32"], "source_updates": 0,
            "actor_state_imports": 0, "scene_state_imports": 0,
            "input_scope": "authentic caller configuration, entry RNG, creation/first caller clocks and stored QPF divisor"}
        path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Qualifying startup: {comparison['compared_values']} values, {comparison['mismatch_count']} differences. {path}")
        return 0 if report["parity"] else 1
    header, records = FLEET.read_trace(args.trace)
    source_updates = len(records)
    scene_lifecycle = getattr(args, "compare_startup_scene_lifecycle", False)
    scene_owned = getattr(args, "compare_startup_scene", False) or scene_lifecycle
    scene_expectations = None
    lifecycle_coverage = FLEET.validate_scene_lifecycle_scope(header, records) if scene_lifecycle else None
    if scene_owned and not scene_lifecycle:
        if not FLEET.scene_autonomous_enabled(header):
            raise ValueError("Autonomous scene replay requires a joined source capture")
        journal_path = args.scene_journal or args.trace.resolve().with_name("scene-journal.jsonl")
        frames_path = args.scene_frames or args.trace.resolve().with_name("scene-frames.jsonl")
        scene_header, scene_events, scene_footer = SCENE.read(journal_path)
        frame_header, frames, frame_footer = SCENE.FRAMES.read_frames(frames_path, scene_header, scene_events)
        with args.trace.open("rb") as source:
            for raw in source: last_record = raw
        source_crc = json.loads(last_record)["prefix_crc32"]
        joined = SCENE.FRAMES.validate_autonomous_join(header, records, scene_header, scene_events, frame_header, frames, frame_footer, source_crc)
        scene_expectations = (scene_events, frames, scene_footer, frame_footer, joined)
    if args.compare_cold_grid_sequence:
        binary = directory / "fleet-start-grid-inputs.bin"
        native = directory / "fleet-start-grid-native.csv"
        FLEET.write_cold_grid_sequence_binary(binary, header)
        command = [str(args.replay.resolve()), "--compare-cold-grid-sequence", str(args.data_root.resolve()), str(binary), str(native)]
        execution = subprocess.run(command, capture_output=True, text=True, timeout=args.native_timeout)
        if execution.returncode:
            raise ValueError(execution.stderr.strip() or f"Native startup factory exited with status {execution.returncode}")
        comparison = FLEET.compare_cold_grid_sequence(header, records, native)
        report = {"status": "compared", "mode": "cold-grid-sequence", "parity": comparison["parity"],
                  "cold_grid_sequence": comparison, "source_updates": source_updates,
                  "physical_parity": None, "continuous_physics_parity": None, "autonomous_ai_parity": None,
                  "exe_crc32": header["exe_crc32"], "trace_crc32": file_crc32(args.trace)}
        path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Startup sequence: {comparison['compared_values']} named values, {comparison['mismatch_count']} differences. {path}")
        return 0 if comparison["parity"] else 1
    if args.compare_cold_grid_init:
        binary = directory / "fleet-cold-grid-inputs.bin"
        native = directory / "fleet-cold-grid-native.csv"
        FLEET.write_cold_grid_binary(binary, header)
        command = [str(args.replay.resolve()), "--compare-cold-grid-init", str(args.data_root.resolve()), str(binary), str(native)]
        execution = subprocess.run(command, capture_output=True, text=True, timeout=args.native_timeout)
        if execution.returncode:
            raise ValueError(execution.stderr.strip() or f"Native cold-grid factory exited with status {execution.returncode}")
        comparison = FLEET.compare_cold_grid(header, native)
        report = {"status": "compared", "mode": "cold-grid-initialization", "parity": comparison["parity"],
                  "cold_grid_initialization": comparison, "source_updates": source_updates,
                  "following_first_pre_rng_observed": records[0]["pre"]["rng"],
                  "return_to_first_pre_gap": "unrecorded; no firstPRE state substitution or continuous RNG claim",
                  "physical_parity": None, "continuous_physics_parity": None, "autonomous_ai_parity": None,
                  "exe_crc32": header["exe_crc32"], "trace_crc32": file_crc32(args.trace)}
        path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Cold-grid initialization: {comparison['compared_values']} values, {comparison['mismatch_count']} differences. {path}")
        return 0 if comparison["parity"] else 1
    if args.updates is not None:
        if not 1 <= args.updates <= source_updates:
            raise ValueError("Requested prefix must contain 1..recorded updates")
        records = records[:args.updates]
    if args.compare_combined_rng:
        binary = directory / "fleet-combined-replay.bin"
        journal = directory / "fleet-combined-journal.bin"
        native = directory / "fleet-combined-native"
        FLEET.write_combined_binary(journal, header, records, **({"startup_world_reset": True} if args.compare_startup_world_reset else {}), **({"scene_owned": True} if scene_owned else {}), **({"startup_physical_body": True} if args.compare_startup_physical_body else {}), compare_driver_retention=args.compare_driver_retention, drivetrain_feedback=args.compare_drivetrain_feedback, **({"startup_join": True} if args.compare_startup_join else {}), **({"rpm_feedback": True} if args.compare_rpm_feedback else {}), **({"limiter_feedback": True} if args.compare_limiter_feedback else {}), **({"pedal_feedback": True} if args.compare_pedal_feedback else {}), **({"control_feedback": True} if args.compare_control_feedback else {}), **({"physical_feedback": True} if args.compare_physical_feedback else {}), **({"dispatch_feedback": True} if args.compare_dispatch_feedback else {}))
        FLEET.write_binary(binary, header, records, **({"startup_world_reset": True} if args.compare_startup_world_reset else {}), **({"scene_owned": True} if scene_owned else {}), **({"startup_contact_cache": True} if args.compare_startup_contact_cache else {}), **({"startup_control_calibration": True} if args.compare_startup_control_calibration else {}), **({"startup_physical_body": True} if args.compare_startup_physical_body else {}), compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=args.compare_driver_retention, drivetrain_feedback=args.compare_drivetrain_feedback, **({"startup_first_selection": True} if args.compare_startup_first_selection else {}), **({"startup_join": True} if args.compare_startup_join else {}), **({"rpm_feedback": True} if args.compare_rpm_feedback else {}), **({"limiter_feedback": True} if args.compare_limiter_feedback else {}), **({"pedal_feedback": True} if args.compare_pedal_feedback else {}), **({"control_feedback": True} if args.compare_control_feedback else {}), **({"physical_feedback": True} if args.compare_physical_feedback else {}), **({"dispatch_feedback": True} if args.compare_dispatch_feedback else {}))
        command = [str(args.replay.resolve()), "--compare-startup-scene-lifecycle" if scene_lifecycle else "--compare-startup-scene" if scene_owned else "--compare-startup-world-reset" if args.compare_startup_world_reset else "--compare-startup-contact-cache" if args.compare_startup_contact_cache else "--compare-startup-control-calibration" if args.compare_startup_control_calibration else "--compare-startup-physical-body" if args.compare_startup_physical_body else "--compare-startup-first-selection" if args.compare_startup_first_selection else "--compare-startup-join" if args.compare_startup_join else "--compare-dispatch-feedback" if args.compare_dispatch_feedback else "--compare-physical-feedback" if args.compare_physical_feedback else "--compare-control-feedback" if args.compare_control_feedback else "--compare-pedal-feedback" if args.compare_pedal_feedback else "--compare-limiter-feedback" if args.compare_limiter_feedback else "--compare-rpm-feedback" if args.compare_rpm_feedback else "--compare-drivetrain-feedback" if args.compare_drivetrain_feedback else "--compare-driver-physics" if args.compare_driver_physics else "--compare-combined-rng", str(args.data_root.resolve()),
                   str(header["initial_configuration"]["track_id"]), str(binary), str(journal)]
        if args.compare_driver_physics:
            physical = directory / "fleet-driver-physical-inputs.bin"
            FLEET.write_binary(physical, header, records, **({"startup_world_reset": True} if args.compare_startup_world_reset else {}), **({"scene_owned": True} if scene_owned else {}), **({"startup_contact_cache": True} if args.compare_startup_contact_cache else {}), **({"startup_control_calibration": True} if args.compare_startup_control_calibration else {}), **({"startup_physical_body": True} if args.compare_startup_physical_body else {}), driver_physics_inputs=True, drivetrain_feedback=args.compare_drivetrain_feedback, **({"rpm_feedback": True} if args.compare_rpm_feedback else {}), **({"limiter_feedback": True} if args.compare_limiter_feedback else {}), **({"pedal_feedback": True} if args.compare_pedal_feedback else {}), **({"control_feedback": True} if args.compare_control_feedback else {}), **({"physical_feedback": True} if args.compare_physical_feedback else {}), **({"dispatch_feedback": True} if args.compare_dispatch_feedback else {}))
            command.append(str(physical))
        command.append(str(native))
        if args.compare_startup_join:
            startup = directory / "fleet-startup-inputs.bin"
            FLEET.write_startup_join_binary(startup, header, records)
            command.append(str(startup))
        if args.compare_startup_world_reset:
            clock = directory / "fleet-startup-clock-inputs.bin"
            FLEET.write_startup_clock_binary(clock, header, records)
            command.append(str(clock))
        execution = subprocess.run(command, capture_output=True, text=True, timeout=args.native_timeout)
        if execution.returncode:
            raise ValueError(execution.stderr.strip() or f"Native combined replay exited with status {execution.returncode}")
        comparison = FLEET.compare_driver_physics(header, records, native, drivetrain_feedback=True, **({"rpm_feedback": True} if args.compare_rpm_feedback else {}), **({"limiter_feedback": True} if args.compare_limiter_feedback else {}), **({"pedal_feedback": True} if args.compare_pedal_feedback else {}), **({"control_feedback": True} if args.compare_control_feedback else {}), **({"physical_feedback": True} if args.compare_physical_feedback else {}), **({"dispatch_feedback": True} if args.compare_dispatch_feedback else {})) if args.compare_drivetrain_feedback else FLEET.compare_driver_physics(header, records, native) if args.compare_driver_physics else FLEET.compare_combined(header, records, native, compare_retained_drivers=args.compare_driver_retention)
        report = {"status": "compared", "mode": "startup-scene-lifecycle" if getattr(args, "compare_startup_scene_lifecycle", False) else "startup-scene" if getattr(args, "compare_startup_scene", False) else "startup-world-reset" if args.compare_startup_world_reset else "startup-contact-cache" if args.compare_startup_contact_cache else "startup-control-calibration" if args.compare_startup_control_calibration else "startup-physical-body" if args.compare_startup_physical_body else "startup-first-selection" if args.compare_startup_first_selection else "startup-retained-driver-rng" if args.compare_startup_join else "retained-dispatch-feedback" if args.compare_dispatch_feedback else "retained-physical-feedback" if args.compare_physical_feedback else "retained-control-feedback" if args.compare_control_feedback else "retained-pedal-feedback" if args.compare_pedal_feedback else "retained-limiter-feedback" if args.compare_limiter_feedback else "retained-rpm-feedback" if args.compare_rpm_feedback else "retained-drivetrain-feedback" if args.compare_drivetrain_feedback else "retained-driver-independent-physics" if args.compare_driver_physics else "retained-driver-rng" if args.compare_driver_retention else "combined-rng", "parity": comparison["parity"],
                  "exact_prefix_updates": comparison["exact_prefix_updates"], "driver_physics" if args.compare_driver_physics else "combined": comparison,
                  "physical_parity": comparison["physical_parity"] if args.compare_driver_physics else None,
                  "continuous_physics_parity": comparison["continuous_physics_parity"] if args.compare_physical_feedback else None, "complete_source_interval": len(records) == source_updates, "autonomous_ai_parity": None, "exe_crc32": header["exe_crc32"],
                  "trace_crc32": file_crc32(args.trace), "source_updates": source_updates}
        timing = comparison.get("combined", comparison).get("race_timing")
        if timing is not None:
            report["retained_race_timing"] = timing
            report["retained_race_timing_scope"] = "Ordinary state6 laps/checkpoints and permanent-AI finishes consume retained driver state; local finish, qualifying and retirement remain unsupported."
        local = comparison.get("combined", comparison).get("local_session")
        if local is not None:
            report["retained_local_session"] = local
            report["retained_race_timing_scope"] = "Retained ordinary laps, local controller handoff/classification and generated primary dispatch through state10/13; qualifying, retirement and pits remain unsupported."
            if FLEET.retained_warm_restart_enabled(header):
                report["retained_warm_restart"] = {name: local[name] for name in ("targeted_warm_restart_required", "targeted_warm_restart_observed", "coverage_sufficient", "coverage")}
                report["retained_race_timing_scope"] = "Retained local finish/results, actual RACE AGAIN confirmation and one warm NoQual grid feeding later updates; qualifying, retirement and pits remain unsupported."
        if args.compare_startup_join:
            startup_comparison = FLEET.compare_cold_grid_sequence(header, records, Path(str(native) + ".startup.csv"))
            report["startup"] = startup_comparison
            bounded_coverage = FLEET.startup_join_coverage(header, records, comparison["combined"] if args.compare_startup_physical_body else comparison)
            report["startup_join_coverage"] = bounded_coverage
            report["parity"] = startup_comparison["parity"] and comparison["numerical_parity"] and bounded_coverage["sufficient"]
            report["parity_scope"] = "Bounded startup-owned subset into retained drivers/shared RNG; generic combined coverage remains separately reported."
            report["startup_owned_actor_fields"] = len(FLEET.STARTUP_JOIN_ACTOR_FIELDS)
            report["startup_seed_policy"] = "First typed entry seed only; factory consumes both classifications and continues directly into joined drivers/journal."
            report["remaining_initial_context"] = "Observed physical/loader/gameplay complement, post-selector body4f0, ownership/pairs, braking/status tails, frame clocks/audio state; no continuous physical claim."
            if args.compare_startup_first_selection:
                selection = FLEET.compare_startup_first_selection(header, records, Path(str(native) + ".startup-selection.csv"))
                report["first_selection"] = selection
                report["parity"] = report["parity"] and selection["parity"]
                report["parity_scope"] = "Generated startup and first pre-AI body selection into retained drivers/shared RNG; generic combined coverage remains separately reported."
                report["remaining_initial_context"] = "Observed physical/loader/gameplay complement, ownership/pairs, braking/status tails, frame clocks/audio state; later body modes and selector context remain observed, with no continuous physical claim."
            if args.compare_startup_physical_body:
                body = FLEET.compare_startup_physical_body(header, records, Path(str(native) + ".startup-physical-body.csv"))
                physical_coverage = FLEET.startup_physical_body_coverage(header, records, comparison, body, selection)
                report["startup_physical_body"] = body
                report["startup_physical_body_coverage"] = physical_coverage
                report["parity"] = report["parity"] and body["parity"] and physical_coverage["sufficient"]
                report["first_selection"]["remaining_context"] = "First selector uses generated startup and initialized Motion; later dispatch reuses the accepted retained Motion/driver selector. External allocation/lifecycle/visibility and clocks remain explicit."
                report["parity_scope"] = "First52 BODY fields feed physical PRE restore and first selector; actual interval reuses continuous dispatch/physical/control/driver/shared RNG. Broader old coverage gaps remain separate diagnostics."
                report["remaining_initial_context"] = body["remaining_initial_context"]
            if args.compare_startup_control_calibration:
                calibration = FLEET.compare_startup_control_calibration(header, records, Path(str(native) + ".startup-control-calibration.csv"))
                report["startup_control_calibration"] = calibration
                report["parity"] = report["parity"] and calibration["parity"]
                report["parity_scope"] = "Eight canonical first-PRE calibration words and restored Controls consumer, plus accepted startup BODY/selection and continuous physical/control/driver/shared RNG. Broader old coverage gaps remain separate diagnostics."
                report["remaining_initial_context"] = calibration["remaining_context"]
            if args.compare_startup_contact_cache:
                cache = FLEET.compare_startup_contact_cache(header, records, Path(str(native) + ".startup-contact-cache.csv"))
                report["startup_contact_cache"] = cache
                report["parity"] = report["parity"] and cache["parity"]
                report["parity_scope"] = "Canonical first-PRE contact cache and restored Motion consumer, plus accepted startup calibration/BODY/selection and continuous physical/control/driver/shared RNG. Broader old coverage gaps remain separate diagnostics."
                report["remaining_initial_context"] = cache["remaining_context"]
            if args.compare_startup_world_reset:
                world = FLEET.compare_startup_world_reset(header, records, Path(str(native) + ".startup-world-reset.csv"))
                clock = FLEET.compare_startup_clock(header, records, Path(str(native) + ".physical.csv"))
                report["startup_world_reset"] = world
                report["clock_transactions"] = clock
                report["parity"] = report["parity"] and world["parity"] and clock["parity"]
                report["parity_scope"] = "Generated source clock/countdown/release/deferred state, cold actor constructor context/identity and mutable contact/damage/prop world, plus startup caches/calibration/BODY/selection and continuous physical/control/driver/shared RNG. Root/visibility and later frame ownership remain external."
                report["remaining_initial_context"] = world["remaining_context"] + " Per-update TimeStep/state/caller/release imports are replaced by observed QPC samples/divisor, the explicit first retained step, initial timer origins and caller clocks. Earlier timer creation remains unproved; external caller/callback scheduling stays observed."
                if FLEET.cold_timer_creation(header) is not None:
                    report["remaining_initial_context"] = world["remaining_context"] + " Per-update TimeStep/state/caller/release imports are replaced by observed QPC samples/divisor, the explicit first retained step, one actual447a77 creation clock and caller clocks. All five initial timer origins are generated; external caller/callback scheduling stays observed."
        if args.compare_startup_world_reset:
            report["numerical_parity"] = comparison["numerical_parity"] and all(
                report[name].get("numerical_parity", report[name]["parity"]) for name in ("startup", "first_selection", "startup_physical_body",
                    "startup_control_calibration", "startup_contact_cache", "startup_world_reset", "clock_transactions") if name in report)
        if args.compare_startup_world_reset and FLEET.cold_local_session_enabled(header):
            report["coverage_sufficient"] = report["startup_join_coverage"]["sufficient"] and local is not None and local["coverage_sufficient"]
            report["parity"] = report["parity"] and report["coverage_sufficient"]
            report["parity_scope"] = "One retained cold startup through ordinary laps, local finish/results and optional warm restart; no intermediate driver, physical or timing world import."
        if scene_owned and not scene_lifecycle:
            scene_events, frames, scene_footer, frame_footer, joined = scene_expectations
            scene_result = compare_scene_rows(SCENE.FRAMES.expected_autonomous_rows(frames, scene_events), Path(str(native) + ".scene.csv"))
            report.update(scene=scene_result, scene_join=joined, scene_source_prefix_crc32=scene_footer["prefix_crc32"],
                          frame_source_prefix_crc32=frame_footer["prefix_crc32"], scene_input_imports=0, physical_scene_input_imports=0,
                          scene_input_scope="geometry, root registration, camera matrices, PVS and LOD",
                          audio_listener_context="observed existing contact/audio boundary")
            report["numerical_parity"] = report["numerical_parity"] and scene_result["numerical_parity"]
            report["coverage_sufficient"] = report["startup_join_coverage"]["sufficient"] and frame_footer["coverage"]["passed"] and scene_footer["coverage"]["passed"]
            if FLEET.camera_controls_enabled(header):
                report["camera_controls"] = SCENE.FRAMES.camera_coverage(frames, scene_events)
                report["coverage_sufficient"] = report["coverage_sufficient"] and report["camera_controls"]["passed"]
            report["parity"] = report["numerical_parity"] and report["coverage_sufficient"]
            report["parity_scope"] = "One bounded cold NoQual execution generates physical transforms, root registration, camera and primary/extra scene cadence; recorded scene bodies and roots are expectations only."
            report["remaining_initial_context"] = "Accepted unclaimed cold actor storage, mapped human input, inherited audio/mix/listener context and external callback origins remain explicit. Canonical resources, authored scene roots, physical transforms, camera matrices and geometry gates are generated."
        if scene_lifecycle:
            consumers = compare_scene_rows(SCENE.FRAMES.expected_lifecycle_consumer_rows(records, FLEET.combined_events), Path(str(native) + ".scene-lifecycle.csv"))
            report.update(scene_consumers=consumers, scene_lifecycle=lifecycle_coverage,
                          scene_input_imports=0, physical_scene_input_imports=0,
                          camera_matrix_parity=None, ordered_pvs_parity=None,
                          scene_source_prefix_crc32=source_prefix_crc32(args.trace),
                          audio_listener_context="observed existing contact/audio boundary")
            report["numerical_parity"] = report["numerical_parity"] and consumers["numerical_parity"]
            report["coverage_sufficient"] = report["coverage_sufficient"] and lifecycle_coverage["sufficient"]
            report["parity"] = report["numerical_parity"] and report["coverage_sufficient"]
            report["parity_scope"] = "One cold source execution through local finish/results and warm restart generates scene state; retained physical and wheel consumer checkpoints are compared. Camera matrices and ordered PVS lists were not recorded and are not claimed."
            report["remaining_initial_context"] = "Accepted unclaimed cold storage, mapped human input, inherited audio/mix/listener context and external callback origins remain explicit. Scene roots, physical transforms, lifecycle camera and geometry gates are generated. The final physical POST does not imply a recorded terminal publication or QPC sample."
        path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Combined RNG: {len(records)} updates, {comparison['mismatch_count']} differences. {path}")
        return 0 if report["parity"] else 1
    if args.compare_skid_dispatch or args.compare_wheel_queue or args.compare_whine_rng:
        feature = "whine-rng" if args.compare_whine_rng else "wheel-queue" if args.compare_wheel_queue else "skid"
        binary = directory / f"fleet-{feature}-replay.bin"
        native = directory / f"fleet-{feature}-native"
        writer = FLEET.write_whine_binary if args.compare_whine_rng else FLEET.write_wheel_queue_binary if args.compare_wheel_queue else FLEET.write_skid_binary
        writer(binary, header, records)
        command = [str(args.replay.resolve()), "--compare-whine-rng" if args.compare_whine_rng else "--compare-wheel-queue" if args.compare_wheel_queue else "--compare-skid-dispatch", str(binary), str(native)]
        execution = subprocess.run(command, capture_output=True, text=True, timeout=args.native_timeout)
        if execution.returncode:
            raise ValueError(execution.stderr.strip() or f"Native skid replay exited with status {execution.returncode}")
        compare = FLEET.compare_whine if args.compare_whine_rng else FLEET.compare_wheel_queue if args.compare_wheel_queue else FLEET.compare_skid_dispatch
        comparison = compare(header, records, native)
        report = {"status": "compared", "mode": "retained-whine-rng" if args.compare_whine_rng else "retained-wheel-queue" if args.compare_wheel_queue else "independent-skid-dispatch", "parity": comparison["parity"],
                  "exact_prefix_updates": comparison["exact_prefix_updates"], "whine" if args.compare_whine_rng else "wheel_queue" if args.compare_wheel_queue else "skid_dispatch": comparison,
                  "physical_parity": None, "autonomous_ai_parity": None,
                  "exe_crc32": header["exe_crc32"], "trace_crc32": file_crc32(args.trace),
                  "source_updates": source_updates}
        path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Source {feature}: {len(records)} updates, {comparison['mismatch_count']} differences. {path}")
        return 0 if comparison["parity"] else 1
    if not (args.local_handoffs_only or args.compare_session_boundaries or args.compare_traffic_controls or args.compare_nearest_recovery or args.compare_joined_ai):
        FLEET.validate_replay_scope(records, args.mode)
    binary = directory / "fleet-replay.bin"
    native = directory / f"fleet-native-{args.mode}.csv"
    FLEET.write_binary(binary, header, records, compare_ai_route=args.compare_ai_route, compare_ai_timing=args.compare_ai_timing, compare_ai_finish=args.compare_ai_finish, compare_local_handoffs=args.compare_local_handoffs, compare_session_boundaries=args.compare_session_boundaries, compare_traffic_controls=args.compare_traffic_controls, compare_nearest_recovery=args.compare_nearest_recovery, compare_joined_ai=args.compare_joined_ai)
    command = [str(args.replay.resolve()), str(args.data_root.resolve()), str(header["initial_configuration"]["track_id"]),
               str(binary), str(native), args.mode]
    if args.compare_ai_route:
        command.append("--compare-ai-route")
    if args.compare_ai_timing:
        command.append("--compare-ai-timing")
    if args.compare_ai_finish:
        command.append("--compare-ai-finish")
    if args.compare_local_handoffs:
        command.append("--compare-local-handoffs")
    if args.local_handoffs_only:
        command.append("--local-handoffs-only")
    if args.compare_session_boundaries:
        command.append("--compare-session-boundaries")
    if args.compare_traffic_controls:
        command.append("--compare-traffic-controls")
    if args.compare_nearest_recovery:
        command.append("--compare-nearest-recovery")
    if args.compare_joined_ai:
        command.append("--compare-joined-ai")
    execution = subprocess.run(command, capture_output=True, text=True, timeout=args.native_timeout)
    if execution.returncode:
        report = {"status": "unsupported" if execution.returncode == 2 else "error", "parity": False,
                  "mode": args.mode, "requested_updates": len(records), "source_updates": source_updates,
                  "autonomous_ai_parity": False, "exe_crc32": header["exe_crc32"],
                  "trace_crc32": file_crc32(args.trace),
                  "reason": execution.stderr.strip() or f"Native replay exited with status {execution.returncode}",
                  "native_exit_code": execution.returncode}
        path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Fleet replay {report['status']}: {report['reason']}. {path}", file=sys.stderr)
        return 2
    print(execution.stdout, end="")
    if args.compare_joined_ai:
        joined = FLEET.compare_joined_ai(header, records, native)
        report = {"mode": args.mode, "parity": joined["parity"], "exact_prefix_updates": joined["exact_prefix_updates"],
                  "physical_parity": None, "physical_exact_prefix_updates": None, "autonomous_ai_parity": None,
                  "physical_comparison_requested": False, "joined_ai": joined}
    elif args.compare_nearest_recovery:
        nearest = FLEET.compare_nearest_recovery(header, records, native)
        report = {"mode": args.mode, "parity": nearest["parity"], "exact_prefix_updates": nearest["exact_prefix_updates"],
                  "physical_parity": None, "physical_exact_prefix_updates": None, "autonomous_ai_parity": None,
                  "physical_comparison_requested": False, "nearest_recovery": nearest}
    elif args.compare_traffic_controls:
        traffic = FLEET.compare_traffic_controls(header, records, native)
        report = {"mode": args.mode, "parity": traffic["parity"], "exact_prefix_updates": traffic["exact_prefix_updates"],
                  "physical_parity": None, "physical_exact_prefix_updates": None, "autonomous_ai_parity": None,
                  "physical_comparison_requested": False, "traffic_controls": traffic}
    elif args.compare_session_boundaries:
        session = FLEET.compare_session_boundaries(header, records, native)
        report = {"mode": args.mode, "parity": session["parity"], "exact_prefix_updates": session["exact_prefix_updates"],
                  "physical_parity": None, "physical_exact_prefix_updates": None, "autonomous_ai_parity": None,
                  "physical_comparison_requested": False, "session_boundaries": session}
    elif args.local_handoffs_only:
        report = {"mode": args.mode, "parity": True, "exact_prefix_updates": len(records),
                  "physical_parity": None, "physical_exact_prefix_updates": None,
                  "autonomous_ai_parity": False, "physical_comparison_requested": False}
    else:
        report = FLEET.compare(records, native, args.mode, header.get("scenario"))
        report["detached"] = FLEET.compare_detached(records, Path(str(native) + ".detached.csv"), header=header)
        report["launch"] = FLEET.compare_launch(records, Path(str(native) + ".launch.csv"), args.mode)
        report["body_selection"] = FLEET.compare_selection(records, Path(str(native) + ".selection.csv"), args.mode)
        report["parity"] = report["parity"] and report["detached"]["parity"] and report["launch"]["parity"] and report["body_selection"]["parity"]
        report["exact_prefix_updates"] = min(report["exact_prefix_updates"], report["detached"]["exact_prefix_updates"],
                                             report["launch"]["exact_prefix_updates"], report["body_selection"]["exact_prefix_updates"])
        report["local_launch_parity"] = report["launch"]["derived_output_parity"]
        report["physical_parity"] = report["parity"]
        report["physical_exact_prefix_updates"] = report["exact_prefix_updates"]
    report["traffic_control_comparison_requested"] = args.compare_traffic_controls
    report["nearest_recovery_comparison_requested"] = args.compare_nearest_recovery
    report["joined_ai_comparison_requested"] = args.compare_joined_ai
    report["session_boundary_comparison_requested"] = args.compare_session_boundaries
    report["ai_initial_state_observed"] = "ai_initial_state" in header
    report["ai_route_comparison_requested"] = args.compare_ai_route
    report["ai_timing_comparison_requested"] = args.compare_ai_timing
    report["ai_finish_comparison_requested"] = args.compare_ai_finish
    if args.compare_ai_route:
        report["ai_route"] = FLEET.compare_ai_route(header, records, Path(str(native) + ".ai-route.csv"))
        report["parity"] = report["parity"] and report["ai_route"]["parity"]
        report["exact_prefix_updates"] = min(report["exact_prefix_updates"], report["ai_route"]["exact_prefix_updates"])
    if args.compare_ai_timing:
        report["ai_timing"] = FLEET.compare_ai_timing(header, records, native, report["ai_route"]["unsupported_updates"], compare_finish=args.compare_ai_finish)
        report["parity"] = report["parity"] and report["ai_timing"]["parity"]
        report["exact_prefix_updates"] = min(report["exact_prefix_updates"], report["ai_timing"]["exact_prefix_updates"])
    if args.compare_ai_finish:
        report["ai_finish"] = FLEET.compare_ai_finish(header, records, native, report["ai_route"]["unsupported_updates"])
        report["parity"] = report["parity"] and report["ai_finish"]["parity"]
        report["exact_prefix_updates"] = min(report["exact_prefix_updates"], report["ai_finish"]["exact_prefix_updates"])
        report["ai_route"]["unsupported_scope"] = ["local controller handoffs", "pit targets", "autonomous traffic/control/race decisions"]
    report["local_handoff_comparison_requested"] = args.compare_local_handoffs
    if args.compare_local_handoffs:
        report["local_handoffs"] = FLEET.compare_local_handoffs(header, records, Path(str(native) + ".local-handoffs.csv"))
        report["parity"] = report["parity"] and report["local_handoffs"]["parity"]
        report["local_controller_transaction_parity"] = report["local_handoffs"]["observed_event_parity"]
        report["exact_prefix_updates"] = min(report["exact_prefix_updates"], report["local_handoffs"]["exact_prefix_updates"])
    for feature in ("contact", "damage", "roughness", "detached"):
        report[feature + "_parity"] = (report["physical_parity"] if report["coverage"][feature + "_coverage"] else None) if not (args.local_handoffs_only or args.compare_session_boundaries or args.compare_traffic_controls or args.compare_nearest_recovery or args.compare_joined_ai) else None
    report["exe_crc32"] = header["exe_crc32"]
    report["trace_crc32"] = file_crc32(args.trace)
    report["source_updates"] = source_updates
    report["status"] = "compared"
    path.write_text(json.dumps(report, indent=2) + "\n")
    if args.compare_joined_ai:
        joined = report["joined_ai"]
        print(f"Joined route/traffic/AI controls: {len(records)} updates, {joined['mismatch_count']} differences, "
              f"{len(joined['release_events'])} release events; one PRE world/RNG perupdate, physics/frame RNG untested. {path}")
    elif args.compare_nearest_recovery:
        nearest = report["nearest_recovery"]
        print(f"Independent nearest-route recovery: {nearest['event_count']} actual checks, {len(nearest['cursor_jumps'])} cursor jumps, "
              f"{nearest['mismatch_count']} differences; ordinary gate progression/physics untested. {path}")
    elif args.compare_traffic_controls:
        traffic = report["traffic_controls"]
        print(f"Independent traffic/AI axes: {len(records)} updates, {traffic['mismatch_count']} differences; "
              f"upstream route/physics untested. {path}")
    elif args.compare_session_boundaries:
        session = report["session_boundaries"]
        print(f"Independent session boundaries: {session['supported_updates']}/{len(records)} supported updates, "
              f"{session['mismatch_count']} differences; route/RNG/physics untested. {path}")
    elif args.local_handoffs_only:
        print(f"Local controller transactions only: {report['local_handoffs']['event_count']} original events, "
              f"{report['local_handoffs']['mismatch_count']} differences; physical/session parity untested. {path}")
    else:
        print(f"{len(records)} updates,16 actors,2 physical phases: {report['mismatch_count']} named-field differences; "
              f"{report['detached']['mismatch_count']} detached and {report['launch']['mismatch_count']} local-control differences; "
              f"{report['body_selection']['mismatch_count']} body-selection differences; "
              f"{report['physical_exact_prefix_updates']} exact physical updates. Autonomous AI is not replayed. {path}")
    if args.compare_ai_route:
        print(f"AI route progression/recovery slice: {report['ai_route']['supported_exact_updates']}/{report['ai_route']['supported_updates']} supported updates exact; "
              f"{len(report['ai_route']['unsupported_updates'])} unsupported updates. Autonomous AI/race parity remains untested.")
    if args.compare_ai_timing:
        print(f"AI timing: {report['ai_timing']['mismatch_count']} checkpoint/lap differences.")
    if args.compare_ai_finish:
        print(f"AI finish/display: {report['ai_finish']['mismatch_count']} differences; "
              f"{len(report['ai_finish']['coverage']['newly_finished_actors'])} observed finishes. Local controller handoffs remain outside scope.")
    return 0 if report["parity"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Fleet replay failed: {error}", file=sys.stderr)
        sys.exit(2)
