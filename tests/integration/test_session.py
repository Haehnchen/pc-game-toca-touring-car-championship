"""Exercise the original no-qualifying start through real menus and timed input."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from native_support import capture_window, leave_scene, TraceReader, read_png, require, wait_for, window_manager_ready, xdo


def run(executable, data_root):
    trace = Path("session.csv")
    trace_reader = TraceReader(trace)
    tick_reader = TraceReader(Path(str(trace) + ".simulation.csv"))
    with Path("session.log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "session.cfg",
                                "--resolution", "960x540", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "30",
                                "--audio-offline", "--trace", str(trace)], stdout=log, stderr=subprocess.STDOUT)

    def rows():
        require(app.poll() is None, "Application exited; see session.log")
        return trace_reader.rows()

    try:
        wait_for(lambda: rows(), "session startup", timeout=8)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            previous = int(rows()[-1]["poll"])
            xdo("key", key)
            return wait_for(lambda: row if (row := rows()[-1]) and int(row["poll"]) > previous
                            and row["screen"] == screen and all(row[field] == str(value) for field, value in values.items())
                            else None, f"{key}: {screen} {values}", timeout=10)

        def phase(number):
            return wait_for(lambda: row if (current := tick_reader.rows()) and (row := current[-1])
                            and int(row["start_phase"]) == number else None,
                            f"original start phase {number}", timeout=8)

        def capture(name):
            frame = int(rows()[-1]["frame"])
            wait_for(lambda: (row := rows()[-1])["presented"] == "1" and int(row["frame"]) >= frame + 2,
                     f"{name} lamp presentation")
            target = Path(f"session-{name}-960x540.png")
            capture_window(window, target)
            width, height, pixels = read_png(target)
            require((width, height) == (960, 540) and len(set(pixels)) > 100,
                    "Start capture must contain the textured game scene and HUD")

        action("space", "language")
        for screen in ("main_menu", "race_menu", "single_options"):
            action("Return", screen)
        action("Up", "single_options", selection=3)
        action("Up", "single_options", selection=2)
        action("Left", "single_options", qualifying=0)
        action("Down", "single_options", selection=3)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection"):
            action("Return", screen)
        entered = action("Return", "scene")
        require(entered["qualifying"] == "0" and entered["manual_transmission"] == "0"
                and entered["session_state"] == "9" and entered["start_held"] == "1"
                and entered["engine_gear"] == "0" and entered["start_plays"] == "0",
                "Grid start must hold the clutch in neutral without an invented loading cue")
        require(entered["hud_lap"] == "1" and entered["hud_lap_limit"] == entered["laps"]
                and entered["hud_lap_time_ms"] == "0" and entered["hud_best_lap_ms"] == "0",
                "Grid HUD must use the selected lap limit and unmeasured initial time")
        xdo("keydown", "Up")
        housing = phase(1)
        require(housing["start_plays"] == "0", "Housing phase must stay silent")
        red = phase(2)
        require(red["start_plays"] == "1" and red["start_sample"] == "69"
                and red["start_held"] == "1" and red["session_state"] == "9",
                "Red must play original cue69 while the grid remains held")
        before_release = tick_reader.rows()
        require(all(row["wheel_drive"] == "0" for row in before_release), "Open start clutch transmitted wheel drive")
        require(max(int(row["rpm"]) for row in before_release) > int(entered["engine_rpm"]) + 1000,
                "Grid hold must still allow the real engine to rev")
        require(any(int(row["whirr_volume"]) > 0 for row in before_release),
                "State9 must apply its original pre-start WHIRR controls")
        capture("red")

        xdo("windowminimize", window)
        paused = wait_for(lambda: row if (row := rows()[-1])["scene_active"] == "0" and row["audio_voices"] == "0"
                          else None, "focus pauses grid timer and sample voices")
        time.sleep(.16)
        require(rows()[-1]["simulation_steps"] == paused["simulation_steps"]
                and rows()[-1]["start_phase"] == paused["start_phase"]
                and rows()[-1]["hud_lap_time_ms"] == paused["hud_lap_time_ms"], "Inactive time advanced the grid start/HUD")
        xdo("keyup", "Up")
        xdo("windowmap", window)
        xdo("windowactivate", "--sync", window)
        resumed = wait_for(lambda: row if (row := rows()[-1])["scene_active"] == "1" else None, "resume grid start")
        require(resumed["start_plays"] == "1", "Focus resume replayed the red signal")
        xdo("keydown", "Up")

        phase(3)
        # Read the first green deadline, even if polling observed a later tick.
        green = next(row for row in tick_reader.rows() if row["start_phase"] == "3")
        require(green["start_plays"] == "2" and green["start_sample"] == "70"
                and green["start_held"] == "1" and green["session_state"] == "9" and green["gear"] == "0",
                "Green must play cue70 while retaining the held state9 drivetrain")
        released = wait_for(lambda: next((row for row in tick_reader.rows()
                                         if int(row["simulation_ns"]) > int(green["simulation_ns"])), None),
                            "first physical deadline after green")
        require(int(released["simulation_ns"]) - int(green["simulation_ns"]) == 40_000_000
                and released["start_phase"] == "3" and released["start_plays"] == "2"
                and released["start_held"] == "0" and released["session_state"] == "6" and released["gear"] == "1",
                "The next40ms deadline must commit state6, release the clutch and select first gear without replaying cue70")
        capture("green")
        expired = phase(4)
        xdo("keyup", "Up")
        states = tick_reader.rows()
        transitions = {}
        for row in states:
            transitions.setdefault(int(row["start_phase"]), int(row["simulation_ns"]) // 1_000_000)
        require([transitions[i] for i in (1, 2, 3, 4)] == [3080, 6160, 9240, 10840],
                f"Original quantized timers drifted with rendering/focus: {transitions}")
        require(all(row["motion_boundary"] == "0" for row in states), "Grid start reached missing motion coverage")
        require(any(int(row["wheel_drive"]) > 0 for row in states if row["start_held"] == "0")
                and int(expired["speed"]) > int(green["speed"]), "Released grid car did not accelerate")
        require(int(expired["lap_time_ms"]) >= 1600 and expired["completed_laps"] == "0",
                "Session must time the released lap without counting grid-to-line as a completed lap")
        require(all(row["hud_lap_time_ms"] == row["lap_time_ms"] and row["hud_best_lap_ms"] == row["best_lap_ms"]
                    and row["hud_lap"] == row["session_lap"] for row in states),
                "Displayed lap/time must follow the actual session at each simulation deadline")
        capture("timing")
        returned = leave_scene(lambda: rows()[-1])
        require(returned["music_track"] == "2", "Session exit must restore menu music")
        action("Up", "main_menu", selection=4)
        xdo("key", "Return")
        require(app.wait(timeout=5) == 0, "Session flow failed to exit")
        final = trace_reader.rows()[-1]
        require(final["audio_voices"] == "0" and final["music_track"] == "0", "Session exit leaked voices or music")
        print("PASS: original grid timers, real clutch release, red/go audio, focus pause, lap/time HUD and clean return")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Session checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("session-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        run(*sys.argv[1:])
    finally:
        manager.terminate()
        try:
            manager.wait(timeout=2)
        except subprocess.TimeoutExpired:
            manager.kill()
            manager.wait(timeout=2)
