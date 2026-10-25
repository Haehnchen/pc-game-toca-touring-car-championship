"""Drive, record, save and reload a real native Time Trial across two processes.

The test driver supplies held action masks through the optional diagnostic
clock and normal keyboard filters using read-only POINTS/trace feedback. Every
recorded pose comes from the application's actual Motion; reload runs live.
"""

import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zlib

import test_settings as settings
from test_full_lap import route_targets, target_ahead
from test_time_trial import TrialWindow
from native_support import StepInput, TraceReader, qualifying_rollout, require, wait_for, window_manager_ready, xdo


def launch(executable, data_root, state_root, name, load_best, stepped=False):
    settings.executable, settings.data_root = executable, data_root
    app = TrialWindow(name, f"{name}.cfg", "--state-dir", str(state_root),
                      "--resolution", "640x480", "--vsync", "off", "--fps-limit", "30",
                      *(["--step-input"] if stepped else []))
    ticks = TraceReader(Path(f"{name}.csv.simulation.csv"))
    app.wait("fresh process startup", screen="title")
    app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
    xdo("windowactivate", "--sync", app.window)
    app.key("space", screen="language")
    app.key("Return", screen="main_menu")
    app.key("Return", screen="race_menu", selection=0)
    app.key("Down", selection=1)
    app.key("Down", selection=2)
    app.key("Return", screen="time_trial_options", selection=2)
    if load_best:
        app.key("Up", selection=1)
        app.key("Right", selection=1)
        app.key("Down", selection=2)
    for screen in ("name_entry", "car_selection", "track_selection", "scene"):
        app.key("Return", screen=screen)
    require(app.current()["game_mode"] == "2" and app.current()["car_id"] == "0"
            and app.current()["manual_transmission"] == "0" and app.current()["resolved_weather"] == "2",
            "Driven acceptance requires an ordinary automatic Honda Time Trial at cloudy Donington")
    return app, ticks


def leave_and_quit(app, expected_record, highlight):
    if app.current()["pause_active"] == "0":
        app.key("Escape", pause_active=1, pause_selection=0)
    app.key("Up", pause_selection=2)
    app.key("Return", pause_page=3, pause_selection=1)
    app.key("Up", pause_selection=0)
    before = int(app.current()["poll"])
    xdo("key", "Return")
    returned = wait_for(lambda: (row := app.current()) and int(row["poll"]) > before
                        and row["screen"] in ("main_menu", "time_trial_records") and row,
                        "ordinary Time Trial save and scene exit", timeout=12)
    require(returned["music_track"] == "2" and returned["pause_active"] == "0" and returned["scene_active"] == "0",
            "Time Trial exit must release scene/audio after saving and restart frontend CD02")
    require(returned["screen"] == "time_trial_records" and returned["selection"] == "0"
            and returned["record_lap_ms"] == str(expected_record) and returned["record_car"] == "0"
            and returned["record_driver"] == "PLAYER 1" and len(returned["record_code"]) == 10
            and returned["record_highlight"] == str(highlight),
            "Ordinary exit must show the actual persistent track record, car, driver, result code and source highlight state")
    app.capture(f"{app.name}-records-page0.png", 640, 480)
    app.key("Return", screen="time_trial_records", selection=1)
    app.capture(f"{app.name}-records-page1.png", 640, 480)
    app.key("Return", screen="main_menu", selection=0)
    app.key("Up", screen="main_menu", selection=4)
    xdo("key", "Return")
    require(app.process.wait(timeout=5) == 0, "Ordinary Quit must terminate the first native process cleanly")


def drive_lap(app, ticks, points, bridge):
    qualifying_rollout(ticks)
    require(bridge.ready() == int(ticks.rows()[-1]["step"]), "Diagnostic Time Trial reply must match its flushed simulation trace")
    release = next(row for row in ticks.rows() if row["session_state"] == "6")
    previous_step, previous_forward, previous_tick = -1, None, None
    next_report = time.monotonic()
    deadline = time.monotonic() + 75

    with Path("time-trial-driven-input.csv").open("w") as log:
        log.write("step,point,speed,error,yaw,held_mask,wanted\n")
        while time.monotonic() < deadline:
            app.current()
            row = ticks.rows()[-1]
            step = int(row["step"])
            if step == previous_step:
                time.sleep(.004)
                continue
            require(row["motion_boundary"] == "0", f"Driven Time Trial hit motion boundary at step{step}, point{row['next_point']}")
            require(row["ai_controlled"] == "0", "The measured Time Trial lap must use player keyboard controls")
            if int(row["completed_laps"]) == 1:
                break
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
            mask = sum({"Left": 1, "Right": 2, "Up": 4, "Down": 8}[key] for key in wanted)
            require(bridge.advance(mask) == step + 1, "Driven Time Trial feedback must advance exactly one application tick")
            advanced = ticks.rows()[-1]
            require(int(advanced["step"]) == step + 1 and int(advanced["held"]) == mask
                    and int(advanced["pressed"]) == mask & ~int(row["held"]),
                    "Normal keyboard input must consume the requested held mask and new edges exactly once")
            require(int(advanced["timestamp_ns"]) - int(row["timestamp_ns"]) == 40_000_000
                    and int(advanced["simulation_ns"]) - int(row["simulation_ns"]) == 40_000_000,
                    "Diagnostic driving must retain the normal host/simulation40ms deadline")
            log.write(f"{step},{row['next_point']},{speed},{error},{yaw},{mask},{' '.join(sorted(wanted))}\n")
            log.flush()
            previous_step, previous_forward, previous_tick = step, forward, row
            if time.monotonic() >= next_report:
                print(f"Driven Time Trial: point{row['next_point']}/{len(points)}, lap{row['lap_time_ms']}ms", flush=True)
                next_report = time.monotonic() + 15
        else:
            raise AssertionError(f"Time Trial keyboard driver did not finish: {previous_tick}")
    bridge.resume()

    measured = [row for row in ticks.rows() if int(row["step"]) >= int(release["step"])]
    crossing = next(row for row in measured if row["completed_laps"] == "1")
    measured = measured[:measured.index(crossing) + 1]
    changes = [int(b["next_point"]) for a, b in zip(measured, measured[1:]) if a["next_point"] != b["next_point"]]
    require(len({row["next_point"] for row in measured}) == len(points)
            and changes == [(i + 2) % len(points) for i in range(len(points))],
            "Physically driven Time Trial must cross every original POINTS gate in order exactly once")
    elapsed = (int(crossing["simulation_ns"]) - int(release["simulation_ns"])) // 1_000_000
    require(crossing["session_lap"] == "2" and crossing["lap_time_ms"] == "0"
            and int(crossing["best_lap_ms"]) == elapsed and int(crossing["ghost_best_ms"]) == elapsed
            and crossing["ghost_dirty"] == "1" and crossing["ghost_visible"] == "1" and crossing["ghost_car"] == "0",
            "Real full-lap crossing must promote matching timing/ghost/model and begin its in-session playback")
    require(all(row["motion_boundary"] == "0" and row["ai_controlled"] == "0" for row in measured)
            and all(int(b["simulation_ns"]) - int(a["simulation_ns"]) == 40_000_000
                    and int(b["timestamp_ns"]) - int(a["timestamp_ns"]) == 40_000_000 for a, b in zip(measured, measured[1:])),
            "The complete lap must retain actual uninterrupted player Motion at independent40ms deadlines")
    require(all(row["hud_lap"] == row["session_lap"] and row["hud_lap_time_ms"] == row["lap_time_ms"]
                and row["hud_best_lap_ms"] == row["best_lap_ms"] for row in measured), "Driven Time Trial HUD disagrees with lap timing")
    require(any(int(row["low_volume"]) + int(row["high_volume"]) > 0 for row in measured), "Driven lap must consume actual engine audio controls")
    return elapsed, int(crossing["ghost_samples"]), crossing


def run(executable, data_root):
    started = time.monotonic()
    state_root = Path("driven-trial-state").resolve()
    points = route_targets(data_root)
    first = second = bridge = None
    try:
        first, ticks = launch(executable, data_root, state_root, "time-trial-driven-first", False, stepped=True)
        require(first.current()["ghost_samples"] == "0" and first.current()["ghost_best_ms"] == "0",
                "First native process must begin with fresh state and no synthetic or saved ghost")
        bridge = StepInput(first.process)
        elapsed, samples, crossing = drive_lap(first, ticks, points, bridge)
        promoted = wait_for(lambda: (row := first.current()) and row["completed_laps"] == "1" and row["ghost_visible"] == "1" and row,
                            "new best visible in ordinary scene")
        position = tuple(promoted[field] for field in ("ghost_x", "ghost_y", "ghost_z"))
        wait_for(lambda: (row := first.current()) and row["ghost_visible"] == "1"
                 and tuple(row[field] for field in ("ghost_x", "ghost_y", "ghost_z")) != position,
                 "actual newly driven best moves during next attempt")
        first.key("Escape", pause_active=1)
        first.capture("time-trial-driven-new-best.png", 640, 480)
        leave_and_quit(first, elapsed, 0)
        saved = state_root / "time-trial/track-0.ghost"
        raw = saved.read_bytes()
        magic, version, track, car, weather, saved_ms, saved_samples, name_size = struct.unpack_from("<4s7I", raw)
        require((magic, version, track, car, weather, saved_ms, saved_samples) == (b"TCTT", 3, 0, 0, 2, elapsed, samples)
                and samples == elapsed // 40 + 1 and len(raw) == 32 + name_size + samples * 28,
                "Ordinary scene exit must persist the actual completed lap, selected car/weather and complete pose stream")
        first_time, = struct.unpack_from("<I", raw, 32 + name_size + 24)
        last_time, = struct.unpack_from("<I", raw, 32 + name_size + (samples - 1) * 28 + 24)
        require(first_time == 0 and last_time == elapsed, "Saved ghost must span the complete measured lap through its finish")
        history = ticks.rows()
        release = next(row for row in history if row["session_state"] == "6")
        by_step = {int(row["step"]): row for row in history}
        for index in range(samples):
            offset = 32 + name_size + index * 28
            recorded_position = struct.unpack_from("<3i", raw, offset + 12)
            sample_time, = struct.unpack_from("<I", raw, offset + 24)
            # Session/TimeTrial consume the preceding actual body pose before
            # this deadline's Motion update; the trace then publishes that step.
            physical = by_step[int(release["step"]) + index - 1]
            require(sample_time == index * 40 and recorded_position == tuple(int(physical[field]) for field in ("body_x", "body_y", "body_z")),
                    f"Persisted ghost sample{index} did not come from the continuous physically integrated player lap")
        table_file = state_root / "time-trial/records.dat"
        table = table_file.read_bytes()
        minutes, seconds, milliseconds = struct.unpack_from("<3I", table, 8)
        table_driver = table[20:32].split(b"\0", 1)[0].decode()
        table_car, = struct.unpack_from("<I", table, 32)
        table_code = table[36:52].split(b"\0", 1)[0].decode()
        require(len(table) == 452 and struct.unpack_from("<4sI", table) == (b"TCLR", 1)
                and zlib.crc32(table[:-4]) == struct.unpack_from("<I", table, len(table) - 4)[0]
                and minutes * 60000 + seconds * 1000 + milliseconds == elapsed
                and table_driver == "PLAYER 1" and table_car == 0 and len(table_code) == 10,
                "The physically driven lap must also update the independent source-shaped persistent track-record table")
        old_pid = first.process.pid
        second, replay_ticks = launch(executable, data_root, state_root, "time-trial-driven-reloaded", True)
        loaded = second.current()
        require(second.process.pid != old_pid and first.process.poll() == 0
                and loaded["ghost_best_ms"] == str(elapsed) and loaded["hud_best_lap_ms"] == str(elapsed)
                and loaded["ghost_samples"] == str(samples) and loaded["ghost_car"] == "0" and loaded["ghost_dirty"] == "0"
                and loaded["ghost_error"] == "0" and loaded["record_lap_ms"] == str(elapsed)
                and loaded["record_driver"] == table_driver and loaded["record_car"] == "0" and loaded["record_code"] == table_code,
                "A fresh native process must restore the newly driven ghost plus independent track record/name/car/code")
        qualifying_rollout(replay_ticks)
        playback = wait_for(lambda: (row := second.current()) and row["ghost_visible"] == "1" and row,
                            "saved real-lap ghost after fresh-process approach")
        before = tuple(playback[field] for field in ("ghost_x", "ghost_y", "ghost_z"))
        wait_for(lambda: (row := second.current()) and row["ghost_visible"] == "1"
                 and tuple(row[field] for field in ("ghost_x", "ghost_y", "ghost_z")) != before,
                 "fresh-process ghost playback moves from persisted physical poses")
        require(int(second.current()["ghost_recording_samples"]) > 1 and second.current()["ghost_dirty"] == "0",
                "Reloaded ghost must play while a separate new player attempt records")
        second.key("Escape", pause_active=1)
        second.capture("time-trial-driven-reloaded.png", 640, 480)
        leave_and_quit(second, elapsed, -1)
        require(saved.read_bytes() == raw and table_file.read_bytes() == table,
                "An incomplete fresh-process attempt must preserve both newly saved ghost and record table exactly")
        report = {"track": 0, "car": car, "weather": weather, "ordered_points": len(points), "lap_ms": elapsed,
                  "recorded_samples": samples, "recorded_until_ms": last_time, "prefix_truncated": last_time < elapsed,
                  "ghost_file_crc32": f"{zlib.crc32(raw):08x}", "records_file_crc32": f"{zlib.crc32(table):08x}", "result_code": table_code,
                  "records_payload_crc32": f"{zlib.crc32(table[:-4]):08x}",
                  "first_process": old_pid, "reloaded_process": second.process.pid,
                  "input_transport": "diagnostic-step-input", "keyboard_filters": True,
                  "physical_x11_driving": False, "reload_clock": "realtime", "synthetic_poses": False,
                  "wall_seconds": round(time.monotonic() - started, 3)}
        Path("time-trial-driven-acceptance.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"PASS: physically driven Time Trial, {len(points)} ordered gates, {elapsed}ms lap, {samples} pose samples "
              f"through{last_time}ms, diagnostic keyboard input, ordinary save/exit, live-process reload and moving ghost; "
              f"wall{report['wall_seconds']:.2f}s", flush=True)
    finally:
        if bridge:
            Path("time-trial-driven-stdout.log").write_bytes(bridge.transcript)
        if first:
            settings.stop(first.process)
        if second:
            settings.stop(second.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Driven Time Trial checks require private muted audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("time-trial-driven-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private Time Trial window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
