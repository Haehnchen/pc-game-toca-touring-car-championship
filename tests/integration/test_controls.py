"""Exercise moving controls, follow camera, audio lifecycle and live deadlines."""

import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

from native_support import capture_window, leave_scene, TraceReader, check_flag_animation, check_horizon_animation, qualifying_rollout, read_png, require, wait_for, window_manager_ready, xdo


def run_case(executable, data_root, cap, vsync, full, detail=3):
    prefix = f"controls-{cap}-{vsync}"
    if detail != 3:
        prefix += f"-detail{detail}"
    trace = Path(prefix + ".csv")
    ticks = Path(str(trace) + ".simulation.csv")
    trace_reader, tick_reader = TraceReader(trace), TraceReader(ticks)
    with Path(prefix + ".log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", prefix + ".cfg",
                                "--resolution", "640x480", "--vsync", vsync, "--fps-limit", str(cap), "--graphics-detail", str(detail),
                                "--audio-offline", "--trace", str(trace)], stdout=log, stderr=subprocess.STDOUT)

    def rows():
        require(app.poll() is None, f"Application exited; see {prefix}.log")
        return trace_reader.rows()

    try:
        wait_for(lambda: rows(), "controls startup", timeout=8)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            before = int(rows()[-1]["poll"])
            xdo("key", key)
            return wait_for(lambda: row if (current := rows()) and (row := current[-1])
                            and int(row["poll"]) > before and row["screen"] == screen
                            and all(row[field] == str(value) for field, value in values.items()) else None,
                            f"{key}: {screen} {values}", timeout=10)

        action("space", "language")
        for screen in ("main_menu", "race_menu", "single_options", "difficulty", "name_entry", "car_selection"):
            action("Return", screen)
        action("Down", "car_selection", manual_transmission=1)
        action("Return", "track_selection")
        # Silverstone's start straight supports the straight acceleration and
        # short steering probe; Donington hands over on a bend.
        action("Right", "track_selection", track_id=1)
        entered = action("Return", "scene", music_track=4)
        require(entered["scene_detail"] == str(detail) and entered["stored_car_id"] == "0", "Brake-light check must use the selected Honda and graphics preset")
        if detail == 4:
            require(any(line.startswith("Mega model:") and "car-0.glb " in line for line in Path(prefix + ".log").read_text().splitlines()),
                    "Mega brake-light check must load the actual Honda GLB")
        require(int(entered["audio_voices"]) >= 5, "Scene must start three motor layers, WHINE and the dry WHIRR loop")
        position = tuple(entered[field] for field in ("body_x", "body_y", "body_z"))
        loading = [row for row in rows() if row["screen"] == "scene_loading"]
        require(all(row["audio_voices"] == "0" and row["music_track"] == "0" for row in loading),
                "Engine samples must load without starting playback")
        qualifying_rollout(tick_reader)
        before = tick_reader.rows()[-1]
        xdo("keydown", "Up")
        active = wait_for(lambda: row if (current := tick_reader.rows()) and (row := current[-1])
                          and int(row["throttle"]) > 0 and int(row["engine_torque"]) > 0 and int(row["car_volume"]) > 0 else None,
                          "source throttle, torque and car-specific engine layer")
        next_gear = min(6, int(active["gear"]) + 1)
        xdo("key", "f")
        wait_for(lambda: tick_reader.rows()[-1]["gear"] == str(next_gear), "restored manual F shift and pending commit")
        wait_for(lambda: len(tick_reader.rows()) >= int(before["step"]) + 30, "live acceleration interval")
        moving = tick_reader.rows()[-1]
        require(int(moving["speed"]) > int(before["speed"]) and int(moving["rpm"]) > 2000,
                "Wheel forces must move the body and recouple engine RPM")
        xdo("keydown", "Left")
        wait_for(lambda: (row := tick_reader.rows()[-1]) and int(row["steering"]) > 0 and int(row["steering_angle"]) < 0,
                 "Left key reaches the original normal-projection steering direction")
        wait_for(lambda: int(tick_reader.rows()[-1]["step"]) >= int(moving["step"]) + 6, "short steering interval")
        xdo("keyup", "Left")
        xdo("keyup", "Up")
        wait_for(lambda: tick_reader.rows()[-1]["throttle"] == "0", "source throttle release ramp")
        speed_before_braking = int(tick_reader.rows()[-1]["speed"])
        require(rows()[-1]["brake_lights"] == "0", "Human driver's brake lights must be off before the pedal press")
        xdo("keydown", "Down")
        wait_for(lambda: int(tick_reader.rows()[-1]["brake"]) > 0, "manual brake filter")
        wait_for(lambda: (row := rows()[-1])["brake_lights"] == "1" and row["presented"] == "1",
                 "Human brake pedal enables the rendered rear lamps")
        if full or detail == 4:
            capture_window(window, Path(f"{prefix}-braking.png"))
        wait_for(lambda: int(tick_reader.rows()[-1]["speed"]) < speed_before_braking, "brake reduces actual body speed")
        xdo("keyup", "Down")
        wait_for(lambda: tick_reader.rows()[-1]["brake"] == "0", "brake release")
        wait_for(lambda: (row := rows()[-1])["brake_lights"] == "0" and row["presented"] == "1",
                 "Human brake release disables the rendered rear lamps")
        if full or detail == 4:
            capture_window(window, Path(f"{prefix}-brake-released.png"))

        # An owned-process stall exercises the actual host's queued deadlines.
        before_stall = tick_reader.rows()[-1]
        app.send_signal(signal.SIGSTOP)
        time.sleep(.28)
        app.send_signal(signal.SIGCONT)
        wait_for(lambda: int(tick_reader.rows()[-1]["step"]) >= int(before_stall["step"]) + 7, "stall catch-up")
        steady = tick_reader.rows()
        check_flag_animation(steady)
        check_horizon_animation(steady)
        require(all(int(b["timestamp_ns"]) - int(a["timestamp_ns"]) == 40_000_000
                    and int(b["simulation_ns"]) - int(a["simulation_ns"]) == 40_000_000
                    for a, b in zip(steady, steady[1:])), "Presentation/stall must not select simulation delta or discard due updates")
        require(sum(bool(int(row["pressed"]) & 256) for row in steady) == 1, "Presentation/catch-up replayed a gear edge")
        require(any(abs(int(moving[field]) - int(position[index])) > 32768
                    for index, field in enumerate(("body_x", "body_y", "body_z"))), "Drive input did not translate the visible body")
        require(all(row["motion_boundary"] == "0" for row in steady), "Short supported drive hit an unimplemented contact boundary")
        require(all(int(row["whirr_volume"]) == 0 for row in steady if int(row["speed_integer"]) < 80),
                "WHIRR must retain the original minimum-speed gate")
        require(all(row["whine_active"] == "1" for row in steady), "Local WHINE loop must survive normal controls and shifts")
        require(any(int(row["whine_volume"]) > 0 for row in steady), "Moving car must receive original WHINE volume controls")
        require(len({row["whine_hz"] for row in steady}) > 1, "WHINE rate must follow actual speed and shifts")
        require(all(row["hud_gear"] == row["gear"] for row in steady), "Original HUD must show the current simulation gear")
        require(all(abs(int(b["hud_rpm"]) - int(a["hud_rpm"])) <= 500 for a, b in zip(steady, steady[1:])),
                "Original HUD RPM smoothing must advance only once per simulation update")

        if full:
            xdo("keydown", "Up")
            wait_for(lambda: int(tick_reader.rows()[-1]["throttle"]) > 0, "held pedal before focus loss")
            xdo("windowminimize", window)
            wait_for(lambda: rows()[-1]["scene_active"] == "0" and rows()[-1]["audio_voices"] == "0", "focus pauses simulation and engine voices")
            paused_steps = rows()[-1]["simulation_steps"]
            time.sleep(.18)
            require(rows()[-1]["simulation_steps"] == paused_steps, "Inactive time advanced vehicle controls")
            xdo("keyup", "Up")
            xdo("windowmap", window)
            xdo("windowactivate", "--sync", window)
            wait_for(lambda: rows()[-1]["scene_active"] == "1" and int(rows()[-1]["audio_voices"]) >= 5, "focus restores motor, WHINE and WHIRR loops")
            wait_for(lambda: tick_reader.rows()[-1]["throttle"] == "0", "focus release survives resume")
            for width, height in ((640, 480), (960, 540)):
                prior_frame = int(rows()[-1]["frame"])
                xdo("windowsize", "--sync", window, width, height)
                wait_for(lambda: (row := rows()[-1])["width"] == str(width) and row["height"] == str(height)
                         and row["presented"] == "1" and int(row["frame"]) >= prior_frame + 2, "scene resize/present")
                target = Path(f"{prefix}-{width}x{height}.png")
                capture_window(window, target)
                captured_width, captured_height, pixels = read_png(target)
                require((captured_width, captured_height) == (width, height) and len(set(pixels)) > 100,
                        "Controls capture must contain the restored textured scene")

        leave_scene(lambda: rows()[-1], music_track=2, music_starts=3)
        if full:
            for screen in ("race_menu", "single_options", "difficulty", "name_entry", "car_selection"):
                action("Return", screen)
            action("Up", "car_selection", manual_transmission=0)
            action("Return", "track_selection")
            action("Left", "track_selection", track_id=0)
            tick_start = len(tick_reader.rows())
            action("Return", "scene", music_track=3)
            qualifying_rollout(tick_reader, tick_start)
            xdo("keydown", "Down")
            reverse = wait_for(lambda: row if (row := tick_reader.rows()[-1])["gear"] == "7" else None,
                               "held automatic Down stops the car and selects reverse", timeout=12)

            def forward_displacement(row, start):
                return sum((int(row[f"body_{axis}"]) - int(start[f"body_{axis}"])) * int(start[f"body_forward_{axis}"])
                           for axis in ("x", "z")) // 65536

            backward = wait_for(lambda: row if (row := tick_reader.rows()[-1])["gear"] == "7"
                                and int(row["signed_speed"]) < -0xccc and forward_displacement(row, reverse) < -65536 else None,
                                "held automatic Down propels the body backward", timeout=5)
            require(backward["hud_gear"] == "7" and backward["brake"] == "0" and int(backward["throttle"]) > 0,
                    "Automatic reverse must show R and use the brake pedal as gas")
            xdo("keyup", "Down")
            xdo("keydown", "Up")
            forward = wait_for(lambda: row if (row := tick_reader.rows()[-1])["gear"] == "1" else None,
                               "gas restores automatic first gear")
            wait_for(lambda: int((row := tick_reader.rows()[-1])["signed_speed"]) > 0xccc
                     and forward_displacement(row, forward) > 65536,
                     "automatic gas stops reverse travel and propels the body forward", timeout=5)
            xdo("keyup", "Up")
            leave_scene(lambda: rows()[-1], music_track=2, music_starts=5)
        action("Up", "main_menu", selection=4)
        xdo("key", "Return")
        require(app.wait(timeout=5) == 0, "Controls flow could not exit normally")
        final = trace_reader.rows()[-1]
        require(final["audio_voices"] == "0" and final["music_track"] == "0", "Exit leaked engine or music playback")
        print(f"PASS: cap={cap}, vsync={vsync}: timed motion/gear/audio, moving body and camera, stall catch-up and return; full={full}")
        return [tuple(row[field] for field in ("horizon_updates", "horizon_moving_count", "horizon_x", "horizon_y", "horizon_z"))
                for row in steady]
    finally:
        if app.poll() is None:
            app.send_signal(signal.SIGCONT)
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    executable, data_root, mode = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Controls acceptance requires isolated dummy/offline audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("controls-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        cases = ((30, "off", True),) if mode == "controls" else ((30, "off", False),) if mode == "brake-lights-mega" else ((120, "off", False), (0, "off", False), (60, "on", False))
        previous_horizon = None
        for cap, vsync, full in cases:
            horizon = run_case(executable, data_root, cap, vsync, full, 4 if mode == "brake-lights-mega" else 3)
            if previous_horizon is not None:
                common = min(len(previous_horizon), len(horizon))
                require(previous_horizon[:common] == horizon[:common],
                        "HOR trajectories at identical simulation ticks must match across FPS caps and VSync")
            previous_horizon = horizon
    finally:
        manager.terminate()
        try:
            manager.wait(timeout=2)
        except subprocess.TimeoutExpired:
            manager.kill()
            manager.wait(timeout=2)
