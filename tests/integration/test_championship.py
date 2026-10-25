"""Play Championship through native menus, a real race and standings."""

import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import test_settings as settings
from test_time_trial import TrialWindow
from test_full_lap import route_targets, target_ahead
from native_support import StepInput, TraceReader, require, wait_for, window_manager_ready, xdo


def run(executable, data_root):
    settings.executable, settings.data_root = executable, data_root
    # SDL gamepad devices are shared across X displays. This keyboard/step-input
    # acceptance must not consume Start/Back from a physical desktop controller.
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    state_root = Path("championship-state").resolve()
    app = TrialWindow("championship", "championship.cfg", "--state-dir", str(state_root),
                      "--resolution", "640x480", "--vsync", "off", "--fps-limit", "30", "--step-input")
    ticks = TraceReader(Path("championship.csv.simulation.csv"))
    bridge = None

    try:
        app.wait("championship startup", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        app.key("Return", screen="main_menu")
        app.key("Return", screen="race_menu", selection=0)
        app.key("Down", selection=1)
        app.key("Return", screen="championship_menu", selection=0)
        app.capture("championship-new-load.png", 640, 480)
        app.key("Return", screen="championship_options", selection=1)
        app.key("Up", selection=0)
        app.key("Right", selection=0)  # Short -> Full -> Short exercises the original option callback.
        app.key("Left", selection=0)
        app.capture("championship-length.png", 640, 480)
        app.key("Down", selection=1)
        app.key("Return", screen="name_entry")
        app.key("Return", screen="car_selection")
        entered = app.key("Return", screen="scene", game_mode=1, track_id=0, qualifying_phase=1,
                          championship_phase=0, championship_meeting=0, championship_race=0)
        require(entered["session_state"] == "4" and entered["ai_controlled"] == "1" and entered["music_track"] == "3"
                and entered["laps"] == "6" and entered["championship_points"] == "0",
                "Short Championship must schedule Donington qualifying and six race laps directly from the garage")
        app.key("Escape", pause_active=1, pause_page=0, pause_selection=0)
        app.capture("championship-qualifying.png", 640, 480)
        app.key("Down", pause_selection=1)
        app.key("Down", pause_selection=2)
        app.key("Return", pause_active=0, session_state=7, best_lap_ms=3600000, qualifying_rank=16)
        time.sleep(1.15)
        app.key("Return", session_state=8)
        app.capture("championship-qualifying-results.png", 640, 480)
        grid = app.key("Return", session_state=9, qualifying_phase=0, championship_phase=1,
                       championship_meeting=0, championship_race=0, hud_lap_limit=6)
        require(grid["rival_count"] == "15" and grid["race_position"] == "16",
                "Skipped qualifying must classify all sixteen original championship actors and start local last")
        app.capture("championship-classified-grid.png", 640, 480)
        bridge = StepInput(app.process)
        bridge.ready(timeout=12)
        targets = route_targets(data_root)
        previous_forward = None
        deadline = time.monotonic() + 700
        next_report = time.monotonic()
        while time.monotonic() < deadline:
            row = ticks.rows()[-1]
            app.current()
            require(row["motion_boundary"] == "0", f"Championship body left supported Motion at point{row['next_point']}")
            if row["session_state"] == "10":
                break
            require(row["session_state"] == "6" and row["ai_controlled"] == "0",
                    "Championship race must consume ordinary player controls")
            position = tuple(int(row[field]) / 65536 for field in ("body_x", "body_z"))
            forward = tuple(int(row[field]) / 65536 for field in ("body_forward_x", "body_forward_z"))
            speed = int(row["speed"]) / 65536
            target = target_ahead(targets, int(row["next_point"]), position, 12)
            dx, dz = target[0] - position[0], target[1] - position[1]
            error = math.atan2(forward[0] * dz - forward[1] * dx, forward[0] * dx + forward[1] * dz)
            yaw = 0 if previous_forward is None else math.atan2(previous_forward[0] * forward[1] - previous_forward[1] * forward[0],
                                                               previous_forward[0] * forward[0] + previous_forward[1] * forward[1])
            steering = error - 8 * yaw
            desired_speed = max(.42, .8 - abs(error) * .8)
            mask = 4 if speed < desired_speed else 8 if speed > desired_speed + .1 else 0
            mask |= 1 if steering > .025 else 2 if steering < -.025 else 0
            require(bridge.advance(mask) == int(row["step"]) + 1, "Championship input must advance exactly one real physical tick")
            previous_forward = forward
            if time.monotonic() >= next_report:
                print(f"Championship: lap{row['session_lap']}/6, point{row['next_point']}, position{row['race_position']}", flush=True)
                next_report = time.monotonic() + 15
        else:
            raise AssertionError(f"Physical championship race did not finish: {ticks.rows()[-1]}")
        finish = ticks.rows()[-1]
        require(finish["completed_laps"] == "6" and finish["race_classified"] == "1" and finish["ai_controlled"] == "1"
                and int(finish["race_result_ms"]) > 0 and finish["finish_playing"] == "1",
                "Actual sixth lap must classify the race, hand over to AI and play original finish audio")
        for _ in range(377):
            if ticks.rows()[-1]["session_state"] == "13":
                break
            bridge.advance(0)
        require(ticks.rows()[-1]["session_state"] == "13", "Championship must present its real timed race results")
        bridge.resume()
        app.wait("championship race results", session_state=13)
        app.capture("championship-race-results.png", 640, 480)
        standings = app.key("Return", screen="championship_overview", music_track=2)
        require(standings["championship_phase"] in ("2", "3") and standings["championship_meeting"] == "0"
                and standings["championship_race"] == "0" and standings["scene_active"] == "0",
                "Completed race must show accumulated championship standings and release race resources")
        for width, height in ((640, 480), (960, 540), (960, 600)):
            app.capture(f"championship-standings-{width}x{height}.png", width, height)
        app.key("Right", screen="championship_overview", selection=1)
        app.capture("championship-standings-page2.png", 960, 600)
        xdo("key", "Return")
        advanced = wait_for(lambda: (row := app.current()) and (row["screen"] == "scene" or row["championship_phase"] == "3") and row,
                            "original championship points gate or second qualifying", timeout=10)
        if advanced["championship_phase"] == "3":
            require(advanced["screen"] == "championship_overview" and int(advanced["championship_points"]) < int(advanced["championship_target"]),
                    "Insufficient first-race points must show original elimination without a replay")
            app.capture("championship-eliminated.png", 960, 600)
            app.key("Return", screen="main_menu")
        else:
            require(advanced["championship_phase"] == "0" and advanced["championship_meeting"] == "0"
                    and advanced["championship_race"] == "1" and advanced["qualifying_phase"] == "1" and advanced["track_id"] == "0",
                    "Passing the original points gate must schedule the second qualifying at the same meeting")
            # No further diagnostic steps: ordinary pause quits the next real session.
            app.key("Escape", pause_active=1, pause_page=0, pause_selection=0)
            app.key("Up", pause_selection=3)
            app.key("Return", pause_page=4, pause_selection=1)
            app.key("Up", pause_selection=0)
            app.key("Return", screen="main_menu")
        app.key("Up", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Championship acceptance must quit cleanly through Main")
        print(f"PASS: Championship original menus, classified grid and actual six-lap finish at position{finish['race_position']}")
    finally:
        if bridge:
            Path("championship-step.log").write_bytes(bridge.transcript)
        settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Championship checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required")
    with Path("championship-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private championship window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
