"""Drive three measured qualifying laps, hand off and restart on the earned grid."""

import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import test_settings as settings
from test_full_lap import route_targets, target_ahead
from test_time_trial import TrialWindow
from native_support import StepInput, TraceReader, capture_window, qualifying_rollout, require, wait_for, window_manager_ready, xdo


def fleet_actors(row):
    entries = [tuple(map(int, entry.split(":"))) for entry in row["rival_actors"].split("/") if entry]
    require(len(entries) == 16 and all(len(entry) == 7 for entry in entries)
            and [entry[0] for entry in entries] == list(range(16)),
            "Fleet trace must retain all sixteen ordered permanent actor identities")
    return {entry[0]: {"mode": entry[1], "position": entry[2:5], "next_point": entry[5], "laps": entry[6]} for entry in entries}


def qualifying_fleet(history, local, measured_laps=False):
    require(history, "Qualifying fleet needs actual physical updates")
    active = {actor for actor in range(16) if actor % 4 == local % 4}
    snapshots = [fleet_actors(row) for row in history]
    require(all({actor for actor, state in snapshot.items() if state["mode"] != 4} == active for snapshot in snapshots),
            "Qualifying must retain exactly the local quarter: four physical actors and twelve removed")
    for actor in set(range(16)) - active:
        require(len({snapshot[actor]["position"] for snapshot in snapshots}) == 1,
                f"Removed qualifying actor{actor} must not run physical updates")
    for actor in active - {local}:
        require(len({snapshot[actor]["position"] for snapshot in snapshots}) > 1
                and len({snapshot[actor]["next_point"] for snapshot in snapshots}) > 1,
                f"Qualifying rival{actor} must drive its own body through real route gates")
        if measured_laps:
            require(max(snapshot[actor]["laps"] for snapshot in snapshots) > 0,
                    f"Qualifying rival{actor} must complete a real ordered lap")
    require(all(row["rival_count"] == "3" and row["rival_boundaries"] == "0" for row in history)
            and max(int(row["rival_moving"]) for row in history) == 3,
            "All three qualifying opponents must move inside supported physical coverage")
    if measured_laps:
        require(any(int(row["rival_voices"]) > 0 and int(row["rival_volume"]) > 0 for row in history),
                "The active qualifying opponents must reach actual spatial engine playback")


def run(executable, data_root):
    settings.executable, settings.data_root = executable, data_root
    app = TrialWindow("qualifying-finish", "qualifying-finish.cfg", "--resolution", "640x480", "--fullscreen", "off", "--graphics-detail", "3",
                      "--vsync", "off", "--fps-limit", "30", "--step-input")
    ticks = TraceReader(Path("qualifying-finish.csv.simulation.csv"))
    bridge = None
    try:
        app.wait("fresh qualifying process", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        for screen in ("main_menu", "race_menu", "single_options", "difficulty", "name_entry", "car_selection", "track_selection"):
            app.key("Return", screen=screen)
        xdo("key", "Return")
        wait_for(lambda: (row := app.current()) and row["screen"] == "scene" and row,
                 "qualifying scene entry", timeout=20)
        qualifying_rollout(ticks)
        local = int(app.current()["car_id"]) * 2
        qualifying_fleet(ticks.rows(), local)
        bridge = StepInput(app.process)
        bridge.ready(timeout=12)
        points = route_targets(data_root)
        previous_forward = None
        deadline = time.monotonic() + 180
        next_report = time.monotonic()
        while time.monotonic() < deadline:
            row = ticks.rows()[-1]
            app.current()
            require(row["motion_boundary"] == "0", f"Qualifying body left supported Motion at point{row['next_point']}")
            if row["session_state"] == "7":
                break
            require(row["session_state"] == "6" and row["ai_controlled"] == "0", "All measured qualifying laps require player ownership")
            position = tuple(int(row[field]) / 65536 for field in ("body_x", "body_z"))
            forward = tuple(int(row[field]) / 65536 for field in ("body_forward_x", "body_forward_z"))
            speed = int(row["speed"]) / 65536
            target = target_ahead(points, int(row["next_point"]), position, 12)
            dx, dz = target[0] - position[0], target[1] - position[1]
            error = math.atan2(forward[0] * dz - forward[1] * dx, forward[0] * dx + forward[1] * dz)
            yaw = 0 if previous_forward is None else math.atan2(previous_forward[0] * forward[1] - previous_forward[1] * forward[0],
                                                               previous_forward[0] * forward[0] + previous_forward[1] * forward[1])
            steering = error - 8 * yaw
            desired_speed = max(.42, .8 - abs(error) * .8)
            mask = 4 if speed < desired_speed else 8 if speed > desired_speed + .1 else 0
            mask |= 1 if steering > .025 else 2 if steering < -.025 else 0
            require(bridge.advance(mask) == int(row["step"]) + 1, "Qualifying feedback must advance one physical update")
            previous_forward = forward
            if time.monotonic() >= next_report:
                print(f"Qualifying: lap{row['session_lap']}, point{row['next_point']}/{len(points)}, best{row['best_lap_ms']}ms", flush=True)
                next_report = time.monotonic() + 15
        else:
            raise AssertionError(f"Three-lap qualifying driver did not finish: {ticks.rows()[-1]}")
        finish = ticks.rows()[-1]
        history = ticks.rows()
        release = next(row for row in history if row["session_state"] == "6")
        measured = [row for row in history if int(row["step"]) >= int(release["step"])]
        crossings = [b for a, b in zip(measured, measured[1:]) if a["completed_laps"] != b["completed_laps"]]
        require([row["completed_laps"] for row in crossings] == ["1", "2", "3"], "Qualifying must record exactly three real complete laps")
        changes = [int(b["next_point"]) for a, b in zip(measured, measured[1:]) if a["next_point"] != b["next_point"]]
        require(changes == [(index + 2) % len(points) for index in range(3 * len(points))], "Qualifying omitted or reordered route gates")
        times = [int(row["simulation_ns"]) for row in [release, *crossings]]
        laps = [(b - a) // 1_000_000 for a, b in zip(times, times[1:])]
        require(finish["session_lap"] == "4" and finish["ai_controlled"] == "1" and int(finish["best_lap_ms"]) == min(laps)
                and finish["finish_playing"] == "1" and finish["finish_volume"] == "127",
                "Third measured crossing must retain the actual best lap, hand off to AI and start the original finish audio")
        require(all(row["ai_controlled"] == "0" for row in measured[:-1]), "Player ownership changed before the final measured crossing")
        for _ in range(64):
            bridge.advance(0)
        while int(ticks.rows()[-1]["step"]) % 8:
            bridge.advance(0)
        camera = ticks.rows()[-1]
        require(camera["session_state"] == "7" and int(camera["finish_camera"]) >= 0 and camera["ai_controlled"] == "1" and app.current()["music_track"] == "0",
                "Qualifying finish must select its authored camera while the actual body continues under AI")
        require(any(camera[field] != finish[field] for field in ("body_x", "body_y", "body_z")),
                "Qualifying AI must continue the physical body through its ordinary braking regime")
        capture_window(app.window, "qualifying-finish-camera.png")
        for _ in range(100):
            bridge.advance(0)
        results = ticks.rows()[-1]
        require(results["session_state"] == "8" and results["best_lap_ms"] == finish["best_lap_ms"]
                and 1 <= int(results["qualifying_rank"]) <= 16 and results["finish_volume"] == "127",
                "Timed qualifying results must use the measured best and retain the finish loop")
        qualifying_fleet(ticks.rows(), local, measured_laps=True)
        require(any(int(row["visibility_roots"]) > 0 for row in app.rows()
                    if row["screen"] == "scene" and row["presented"] == "1" and row["rival_count"] == "3"),
                "The qualifying fleet must reach the existing rendered dynamic scene")
        rank = results["qualifying_rank"]
        bridge.resume()
        app.wait("qualifying results presentation", session_state=8)
        app.capture("qualifying-finish-results.png", 640, 480)
        app.key("Return", session_state=9, qualifying_phase=0, completed_laps=0, rival_count=15)
        grid = app.current()
        require(grid["race_position"] == rank and grid["best_lap_ms"] == "0" and grid["music_track"] == "3",
                "Confirm must create the actual classified grid, reset race timing and restart track music")
        grid_actors = fleet_actors(grid)
        removed = {actor for actor in range(16) if actor % 4 != local % 4}
        result_actors = fleet_actors(results)
        require(all(state["mode"] != 4 for state in grid_actors.values())
                and any(grid_actors[actor]["position"] != result_actors[actor]["position"] for actor in removed),
                "The earned race grid must reactivate all sixteen actors and place the removed qualifying cars on the grid")
        app.capture("qualifying-finish-grid.png", 640, 480)
        app.key("Escape", pause_active=1, pause_page=0)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=2, pause_selection=0)
        before_restart = app.current()
        warm_start = len(ticks.rows())
        restarted = app.key("Return", session_state=4, qualifying_phase=1, pause_active=0, rival_count=3)
        require(restarted["ai_controlled"] == "1" and restarted["completed_laps"] == "0" and restarted["best_lap_ms"] == "0"
                and restarted["simulation_steps"] == before_restart["simulation_steps"]
                and restarted["source_rng_state"] == before_restart["source_rng_state"],
                "Warm Race Again must rebuild the four-actor qualifying approach without extra physics or classification draws")
        require({actor for actor, state in fleet_actors(restarted).items() if state["mode"] != 4}
                == {actor for actor in range(16) if actor % 4 == local % 4},
                "Warm qualifying must remove the same twelve actors from the former sixteen-car race grid")
        qualifying_rollout(ticks, warm_start)
        qualifying_fleet(ticks.rows()[warm_start:], local)
        app.key("Escape", pause_active=1, pause_page=0)
        app.key("Up", pause_selection=3)
        app.key("Return", pause_page=2, pause_selection=0)
        app.key("Down", pause_selection=1)
        app.key("Return", screen="main_menu", music_track=2)
        app.key("Up", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Qualifying/grid flow did not quit cleanly")
        print(f"PASS: three ordered qualifying laps {laps}ms, actual best{min(laps)}ms, three physical rivals, finish handoff/camera/audio, classified grid position{rank} and warm qualifying fleet")
    finally:
        if bridge:
            Path("qualifying-finish-step.log").write_bytes(bridge.transcript)
        settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Qualifying finish requires private dummy/offline audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required")
    with Path("qualifying-finish-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private qualifying window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
