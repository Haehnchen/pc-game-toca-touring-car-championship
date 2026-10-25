"""Drive a full measured lap through normal keyboard controls and the application.

The read-only test driver follows authored POINTS targets. Its conservative
feedback policy is test input, not original AI or a change to vehicle physics.
"""

import argparse
import json
import math
import os
from pathlib import Path
import runpy
import shutil
import signal
import struct
import subprocess
import time
import zlib

from native_support import capture_window, StepInput, TraceReader, leave_scene, qualifying_rollout, require, wait_for, window_manager_ready, xdo


# Mirrors src/scene/catalog.hpp's original 44e717/44e673/44e5cf archive
# mapping. This test reads the canonical KEEP member, never extracted assets.
TRACKS = (
    ("donington", "donl", 3), ("silverstone", "silv", 4),
    ("thruxton", "thru", 5), ("brands-hatch", "bran", 6),
    ("oulton-park", "oult", 3), ("donington-short", "dons", 4),
    ("croft", "crof", 5), ("knockhill", "knoc", 6),
    ("snetterton", "snet", 3), ("lavaland", "land", 4),
)


def route_source(root, track=0):
    require(0 <= track < len(TRACKS), "Track must be in 0..9")
    read_bfp = runpy.run_path(str(Path(__file__).resolve().parents[2] / "tools/extract-assets.py"))["read_bfp"]
    path = Path("data") / (TRACKS[track][1] + "keep.bfp")
    archive = (Path(root) / path).read_bytes()
    members = [entry for entry in read_bfp(archive) if entry[1].split(b"\0", 1)[0] == b"POINTS.DAT"]
    require(len(members) == 1 and members[0][0] == 1, "Expected the original KEEP POINTS.DAT member 1")
    _, _, offset, size = members[0]
    points = archive[offset:offset + size]
    count, = struct.unpack_from("<I", points)
    require(count >= 6 and len(points) >= 4 + count * 64, "Truncated original POINTS records")
    return points, {"archive": path.as_posix(), "archive_crc32": f"{zlib.crc32(archive):08x}",
                    "points_crc32": f"{zlib.crc32(points):08x}", "points_member": 1}


def route_targets(root, track=0):
    points, _ = route_source(root, track)
    count, = struct.unpack_from("<I", points)
    # 4019f0: 64-byte POINTS records; +12 is the authored driving target.
    return [tuple(value / 65536 for value in struct.unpack_from("<3i", points, 4 + i * 64 + 12)[::2])
            for i in range(count)]


def target_ahead(points, next_point, position, lookahead):
    start, end = points[(next_point - 1) % len(points)], points[next_point]
    dx, dz = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dz)
    projection = max(0, min(length, ((position[0] - start[0]) * dx + (position[1] - start[1]) * dz) / length))
    distance = projection + lookahead
    for offset in range(len(points)):
        if distance <= length:
            return start[0] + dx * distance / length, start[1] + dz * distance / length
        distance -= length
        start, end = end, points[(next_point + offset + 1) % len(points)]
        dx, dz = end[0] - start[0], end[1] - start[1]
        length = math.hypot(dx, dz)
    raise AssertionError("Driver lookahead exhausted the route")


def matrix_approach(reader, point_count):
    """Prove the ordinary approach without track-specific bend/span thresholds."""
    wait_for(lambda: row if (history := reader.rows()) and (row := history[-1])
             and row["session_state"] == "6" and row["ai_controlled"] == "0" else None,
             "automatic track approach and ordered start-line player handover", timeout=20)
    history = reader.rows()
    first = next(row for row in history if row["session_state"] == "6")
    approach = [row for row in history[:history.index(first)] if row["session_state"] == "4"]
    require(approach and all(row["ai_controlled"] == "1" and row["motion_boundary"] == "0"
                             and row["lap_time_ms"] == "0" for row in approach),
            "The real qualifying approach must retain AI ownership, supported ground and an unstarted measured timer")
    gates = [int(row["next_point"]) for row in [*approach, first]]
    changes = [(a, b) for a, b in zip(gates, gates[1:]) if a != b]
    # 403dd0 consumes one adjacent plane per update in either direction.
    # Several authored approach poses begin just behind their initial cursor's
    # previous plane, so the first update legitimately moves that cursor back.
    require(changes and all(b in ((a + 1) % point_count, (a - 1) % point_count) for a, b in changes)
            and changes[-1] == (0, 1),
            "Approach must consume adjacent directed route gates through its first forward start-line crossing")
    require(first["ai_controlled"] == "0" and first["session_lap"] == "1" and first["lap_time_ms"] == "0"
            and first["completed_laps"] == "0",
            "The first start crossing must hand over player controls and start lap one without awarding a lap")
    return {"approach_updates": len(approach), "approach_initial_cursor": gates[0],
            "approach_route_transitions": len(changes),
            "approach_reverse_transitions": sum(b == (a - 1) % point_count for a, b in changes)}


def weather_observations(measured, resolved_weather):
    """Assert the existing weather/contacts/audio path without inventing cues."""
    particles = {1: 0, 2: 0, 3: 90, 4: 25}[resolved_weather]
    require(all(int(row["weather_particles"]) == particles for row in measured),
            "Measured lap must retain the source weather particle pool, including Lavaland's dry exclusion")
    require(all(int(row["weather_updates"]) == int(row["step"])
                and int(row["surface_updates"]) == int(row["step"]) for row in measured),
            "Weather and wheel effects must retain one update per physical40ms deadline")
    weather_positions = len({tuple(row[field] for field in ("weather_first_x", "weather_first_y", "weather_first_z")) for row in measured})
    trail_updates = sum(int(row["surface_trails"]) > 0 for row in measured)
    rolling_updates = sum(row["surface_audio_sample"] == "81" and int(row["surface_audio_volume"]) > 0 for row in measured)
    skid_updates = sum(int(row["skid_volume"]) > 0 and int(row["skid_sample"]) in (*range(39, 45), 47, 48, 49) for row in measured)
    slip_updates = sum(any(abs(int(row[f"longitudinal_slip_{wheel}"])) + abs(int(row[f"lateral_slip_{wheel}"])) > 0x4ccc
                           for wheel in range(4)) for row in measured)
    if particles:
        require(weather_positions > 10, "Rain/snow must move throughout the actual full-lap simulation")
        require(trail_updates > 0, "Wet wheels must produce their original surface plumes/trails during the lap")
        if resolved_weather == 3:
            require(rolling_updates > 0, "Rain asphalt must exercise original wet rolling sample81 with nonzero gain")
        else:
            require(rolling_updates == 0, "Snow uses SOUNDDRY and must not invent wet-road sample81 playback")
        if slip_updates:
            require(skid_updates > 0, "Actual slipping tyres must exercise their original audible skid samples")
    return {"weather_particles": particles, "weather_positions": weather_positions,
            "surface_trail_updates": trail_updates, "wet_rolling_audio_updates": rolling_updates,
            "slipping_updates": slip_updates, "audible_skid_updates": skid_updates}


def run(executable, data_root, mode="stepped", track=None, weather=None):
    require(mode in ("stepped", "--realtime"), "Expected optional --realtime mode")
    stepped = mode == "stepped"
    started = time.monotonic()
    require(weather in (None, "sunny", "rain", "snow"), "Expected sunny, rain or snow weather")
    matrix = track is not None or weather is not None
    weather_choice = {"sunny": 1, "rain": 3, "snow": 4}.get(weather, 1 if matrix else 0)
    track = 0 if track is None else track
    require(0 <= track < len(TRACKS), "Track must be in 0..9")
    resolved_weather = 1 if track == 9 else weather_choice if matrix else 2
    name = f"full-lap-track-{track}" if matrix else "full-lap"
    if weather is not None:
        name += f"-{weather}"
    points = route_targets(data_root, track)
    _, source = route_source(data_root, track)
    trace = Path(f"{name}.csv")
    frames = TraceReader(trace)
    ticks = TraceReader(Path(str(trace) + ".simulation.csv"))
    with Path(f"{name}.log").open("w") as log:
        app = subprocess.Popen([executable, "--data-root", data_root, "--config", f"{name}.cfg", "--state-dir", str(Path(f"{name}-state").resolve()),
                                "--resolution", "640x480", "--vsync", "off", "--fps-limit", "30",
                                "--audio-offline", "--trace", str(trace), *(["--step-input"] if stepped else [])],
                                stdin=subprocess.PIPE if stepped else subprocess.DEVNULL,
                                stdout=subprocess.PIPE if stepped else log, stderr=log)
    bridge = StepInput(app) if stepped else None
    held = set()
    report = None
    approach_metrics = {}

    def current():
        require(app.poll() is None, f"Application exited; see {name}.log")
        rows = frames.rows()
        return rows[-1] if rows else None

    def keys(wanted):
        nonlocal held
        arguments = []
        for key in sorted(held - wanted):
            arguments += ["keyup", key]
        for key in sorted(wanted - held):
            arguments += ["keydown", key]
        if arguments:
            xdo(*arguments)
        held = wanted

    try:
        wait_for(current, "full-lap startup", timeout=8)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **expected):
            before = int(current()["poll"])
            xdo("key", key)
            return wait_for(lambda: row if (row := current()) and int(row["poll"]) > before
                            and row["screen"] == screen and all(row.get(field) == str(value) for field, value in expected.items())
                            else None, f"{key}: {screen}, {expected}", timeout=10)

        action("space", "language")
        action("Return", "main_menu")
        if matrix:
            # Normal Extras switch makes even the locked circuits and Lavaland
            # accessible; no persistent state is fabricated for the test.
            for selected in range(1, 4):
                action("Down", "main_menu", selection=selected)
            action("Return", "extras", selection=0)
            action("Return", "extras_unlocks", selection=0)
            action("Right", "extras_unlocks", extras_all_tracks=1, extras_bonus_track=1)
            action("Escape", "extras")
            action("Escape", "main_menu")
            for selected in (4, 0):
                action("Down", "main_menu", selection=selected)
        action("Return", "race_menu")
        action("Return", "single_options")
        if matrix:
            # 4512b0 resolves explicit rain3/snow4 normally. Lavaland ignores
            # either requested weather and remains authored dry LAVA03.
            action("Up", "single_options", selection=3)
            for choice in range(1, weather_choice + 1):
                action("Right", "single_options", weather=choice)
            action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection"):
            action("Return", screen)
        for selected in range(1, track + 1):
            action("Right", "track_selection", track_id=selected, track_available=1)
        action("Return", "scene")
        require(current()["track_id"] == str(track) and current()["manual_transmission"] == "0"
                and current()["weather"] == str(weather_choice) and current()["resolved_weather"] == str(resolved_weather),
                "Expected requested/resolved track weather and ordinary automatic Honda")
        require(current()["music_track"] == str(TRACKS[track][2]), "Scene selected the wrong original CD track")
        if matrix:
            approach_metrics = matrix_approach(ticks, len(points))
        else:
            qualifying_rollout(ticks)
        if bridge:
            require(bridge.ready() == int(ticks.rows()[-1]["step"]), "Diagnostic reply does not match the flushed trace")
        release = next(row for row in ticks.rows() if row["session_state"] == "6")
        previous_step = -1
        previous_forward = None
        previous_tick = None
        next_report = time.monotonic()
        deadline = time.monotonic() + (90 if matrix and stepped else 75 if stepped else 420)
        last_route_step = int(release["step"])
        last_route_point = int(release["next_point"])
        max_steps = 15000
        with Path(f"{name}-driver.csv").open("w") as driver_log:
            driver_log.write("step,point,speed,error,yaw,wanted\n")
            while time.monotonic() < deadline:
                current()
                row = ticks.rows()[-1]
                step = int(row["step"])
                if step == previous_step:
                    time.sleep(.004)
                    continue
                require(row["motion_boundary"] == "0", f"Test-driven car hit motion boundary at step {step}, point {row['next_point']}; inspect trajectory before attributing a runtime bug")
                require(row["ai_controlled"] == "0", "Measured lap must stay under player control")
                if int(row["completed_laps"]) == 1:
                    break
                require(step - int(release["step"]) < max_steps,
                        f"Test driver exceeded {max_steps} physical updates at point {row['next_point']}; driver failure, not established runtime fault")
                if int(row["next_point"]) != last_route_point:
                    last_route_step, last_route_point = step, int(row["next_point"])
                require(step - last_route_step < 1000,
                        f"Test driver stalled at point {last_route_point} for 1000 updates; inspect input/pose before attributing a runtime bug")
                position = tuple(int(row[field]) / 65536 for field in ("body_x", "body_z"))
                forward = tuple(int(row[field]) / 65536 for field in ("body_forward_x", "body_forward_z"))
                speed = int(row["speed"]) / 65536
                target = target_ahead(points, int(row["next_point"]), position, 12)
                dx, dz = target[0] - position[0], target[1] - position[1]
                error = math.atan2(forward[0] * dz - forward[1] * dx, forward[0] * dx + forward[1] * dz)
                yaw = 0 if previous_forward is None else math.atan2(
                    previous_forward[0] * forward[1] - previous_forward[1] * forward[0],
                    previous_forward[0] * forward[0] + previous_forward[1] * forward[1]) / (step - previous_step)
                steering = error - 8 * yaw
                desired_speed = max(.42, .8 - abs(error) * .8)
                wanted = set()
                if speed < desired_speed:
                    wanted.add("Up")
                elif speed > desired_speed + .10:
                    wanted.add("Down")
                if steering > .025:
                    wanted.add("Left")
                elif steering < -.025:
                    wanted.add("Right")
                if bridge:
                    mask = sum({"Left": 1, "Right": 2, "Up": 4, "Down": 8}[key] for key in wanted)
                    require(bridge.advance(mask) == step + 1, "Feedback must drive exactly the next tick")
                    require(int(ticks.rows()[-1]["held"]) == mask, "Normal input consumer did not receive the requested driving actions")
                else:
                    keys(wanted)
                driver_log.write(f"{step},{row['next_point']},{speed},{error},{yaw},{' '.join(sorted(wanted))}\n")
                driver_log.flush()
                previous_step, previous_forward, previous_tick = step, forward, row
                if time.monotonic() >= next_report:
                    print(f"Player track{track} lap: step{step}, point{row['next_point']}/{len(points)}, {row['lap_time_ms']}ms", flush=True)
                    next_report = time.monotonic() + 15
            else:
                raise AssertionError(f"Test driver wall bound expired at track{track}, point{previous_tick['next_point']}; runtime bug not established")
        if bridge:
            bridge.resume()
        else:
            keys(set())
        measured = [row for row in ticks.rows() if int(row["step"]) >= int(release["step"])]
        crossing = next(row for row in measured if row["completed_laps"] == "1")
        measured = measured[:measured.index(crossing) + 1]
        require(len({row["next_point"] for row in measured}) == len(points), "Lap omitted route points")
        changes = [int(b["next_point"]) for a, b in zip(measured, measured[1:]) if a["next_point"] != b["next_point"]]
        require(changes == [(i + 2) % len(points) for i in range(len(points))], "Measured route must make one complete ordered circuit")
        elapsed = (int(crossing["simulation_ns"]) - int(release["simulation_ns"])) // 1_000_000
        require(crossing["session_lap"] == "2" and crossing["lap_time_ms"] == "0"
                and int(crossing["best_lap_ms"]) == elapsed, "Lap counter/timing disagrees with continuous simulation")
        require(all(row["hud_lap"] == row["session_lap"] and row["hud_lap_time_ms"] == row["lap_time_ms"]
                    and row["hud_best_lap_ms"] == row["best_lap_ms"] for row in measured), "HUD disagrees with session")
        require(all(row["motion_boundary"] == "0" and row["ai_controlled"] == "0" for row in measured), "Player path was interrupted")
        require(all(int(b["simulation_ns"]) - int(a["simulation_ns"]) == 40_000_000
                    and int(b["timestamp_ns"]) - int(a["timestamp_ns"]) == 40_000_000 for a, b in zip(measured, measured[1:])),
                "Full lap lost simulation deadlines")
        require(any(int(row["low_volume"]) + int(row["high_volume"]) > 0 for row in measured), "Lap has no engine audio")
        weather_metrics = weather_observations(measured, resolved_weather)
        wait_for(lambda: current()["completed_laps"] == "1" and current()["hud_lap"] == "2", "visible lap2")
        xdo("key", "Escape")
        wait_for(lambda: current()["pause_active"] == "1", "pause after finished lap")
        capture_window(window, f"{name}.png")
        xdo("key", "Escape")
        wait_for(lambda: current()["pause_active"] == "0", "resume after capture")
        leave_scene(current, music_track=2)
        # Wait for the actual selected Quit row. A newer idle poll alone can
        # precede Up consumption; sending Return then combines both edges and
        # the original menu prioritizes confirm, reopening Start Race.
        action("Up", "main_menu", selection=4)
        xdo("key", "Return")
        require(app.wait(timeout=5) == 0, "Full-lap flow did not exit normally")
        require(frames.rows()[-1]["audio_voices"] == "0" and frames.rows()[-1]["music_track"] == "0", "Exit leaked audio")
        wall_seconds = time.monotonic() - started
        travel = sum(math.hypot(int(b["body_x"]) - int(a["body_x"]), int(b["body_z"]) - int(a["body_z"])) / 65536
                     for a, b in zip(measured, measured[1:]))
        report = {"track": track, "track_name": TRACKS[track][0], "status": "passed", "car": 0,
                  "weather_choice": weather_choice, "resolved_weather": resolved_weather,
                  "lavaland_weather_exception": track == 9, "music_track": TRACKS[track][2], **source, **approach_metrics, **weather_metrics,
                  "ordered_points": len(points), "release_step": int(release["step"]), "finish_step": int(crossing["step"]),
                  "measured_updates": len(measured) - 1, "lap_ms": elapsed, "measured_travel_metres": round(travel, 3),
                  "barrier_contact_updates": sum(int(row["barrier_contacts"]) > 0 for row in measured),
                  "motion_boundary_updates": sum(int(row["motion_boundary"]) != 0 for row in measured),
                  "input_transport": "diagnostic-step-input" if stepped else "physical-x11-keyboard",
                  "unmodified_motion_and_session": True, "original_full_lap_parity": False,
                  "wall_seconds": round(wall_seconds, 3)}
        print(f"PASS: track {track} {TRACKS[track][0]}, mode={mode}, wall={wall_seconds:.2f}s, simulation={elapsed / 1000:.2f}s; "
              f"weather{weather_choice}->{resolved_weather}, one player car, {len(points)} ordered points, {elapsed}ms measured lap, HUD lap2 and best time, engine/music lifecycle")
    except Exception as error:
        history = ticks.rows()
        terminal = history[-1] if history else {}
        report = {"track": track, "track_name": TRACKS[track][0], **source, **approach_metrics, "weather_choice": weather_choice, "resolved_weather": resolved_weather, "status": "failed",
                  "failure": str(error), "failure_scope": "native test flow; runtime fault attribution requires trajectory/source triage",
                  "original_full_lap_parity": False, "wall_seconds": round(time.monotonic() - started, 3),
                  "terminal": {field: terminal.get(field) for field in
                               ("step", "next_point", "session_state", "completed_laps", "lap_time_ms", "body_x", "body_y", "body_z", "speed", "motion_boundary", "held")}}
        raise

    finally:
        if bridge:
            Path(f"{name}-stdout.log").write_bytes(bridge.transcript)
        if app.poll() is None:
            app.send_signal(signal.SIGCONT)
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)
        if report is not None:
            # The process has closed its streams: even failed-run CRC values
            # describe the final retained files rather than an active trace.
            artifacts = [trace, Path(str(trace) + ".simulation.csv"), Path(f"{name}-driver.csv"),
                         Path(f"{name}.png"), Path(f"{name}.log"), Path(f"{name}-stdout.log")]
            report["artifacts_crc32"] = {path.name: f"{zlib.crc32(path.read_bytes()):08x}"
                                         for path in artifacts if path.exists()}
            Path(f"{name}-acceptance.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Full-lap check requires private dummy/offline audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("full-lap-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("executable")
        parser.add_argument("data_root")
        parser.add_argument("--realtime", action="store_true", help="drive through physical X11 keys")
        parser.add_argument("--track", type=int, choices=range(10), default=None,
                            help="audit this circuit through Extras; explicit track defaults to sunny weather")
        parser.add_argument("--weather", choices=("sunny", "rain", "snow"), default=None,
                            help="select original weather1/3/4 through normal UI; Lavaland retains its source dry exception")
        arguments = parser.parse_args()
        run(arguments.executable, arguments.data_root, "--realtime" if arguments.realtime else "stepped", arguments.track, arguments.weather)
    finally:
        manager.terminate()
        try:
            manager.wait(timeout=2)
        except subprocess.TimeoutExpired:
            manager.kill()
            manager.wait(timeout=2)
