"""Drive from the normal grid into Donington's original PITS with real keys."""

import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from native_support import capture_window, leave_scene, TraceReader, read_png, require, wait_for, window_manager_ready, xdo


def run(executable, data_root):
    trace = TraceReader("pits.csv")
    ticks = TraceReader("pits.csv.simulation.csv")
    with Path("pits.log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "pits.cfg",
                                "--resolution", "640x480", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "30",
                                "--audio-offline", "--trace", "pits.csv"], stdout=log, stderr=subprocess.STDOUT)

    def rows():
        require(app.poll() is None, "Application exited; see pits.log")
        return trace.rows()

    try:
        # Instrumented startup includes first-use GPU/resource preparation.
        wait_for(rows, "pit-flow startup", timeout=20)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            previous = int(rows()[-1]["poll"])
            xdo("key", key)
            return wait_for(lambda: row if (row := rows()[-1]) and int(row["poll"]) > previous
                            and row["screen"] == screen and all(row[field] == str(value) for field, value in values.items())
                            else None, f"{key}: {screen} {values}", timeout=10)

        action("space", "language")
        for screen in ("main_menu", "race_menu", "single_options"):
            action("Return", screen)
        action("Up", "single_options", selection=3)
        action("Up", "single_options", selection=2)
        action("Left", "single_options", qualifying=0)
        action("Down", "single_options", selection=3)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            action("Return", screen)
        require(rows()[-1]["stored_track_id"] == "0", "Pit route must use the original Donington grid")
        # The pit-divider barrier prevents a direct crossing from the grid.
        # Reverse around the adjacent authored hairpin, then drive forward
        # through PITS0. These are keyboard waypoints, never application poses.
        waypoints = ((-123, -80), (-106, -96), (-100, -115), (-114, -135),
                     (-150, -145), (-225, -145), (-199, -120))
        stage, held, entry = 0, set(), None
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            current = ticks.rows()
            require(app.poll() is None, "Pit driving exited; see pits.log")
            if not current:
                time.sleep(.03)
                continue
            row = current[-1]
            require(row["motion_boundary"] == "0", f"Keyboard pit approach left supported ground at waypoint {stage}")
            if row["pit_active"] == "1":
                entry = row
                break
            x, z = (int(row[field]) / 65536 for field in ("body_x", "body_z"))
            target_x, target_z = waypoints[stage]
            if math.hypot(target_x - x, target_z - z) < 9 and stage < len(waypoints) - 1:
                stage += 1
                target_x, target_z = waypoints[stage]
            reverse = stage < len(waypoints) - 1
            target_heading = math.atan2(-(target_z - z), target_x - x) * 4096 / (2 * math.pi)
            if reverse:
                target_heading += 2048
            error = (target_heading - int(row["heading"]) + 2048) % 4096 - 2048
            wanted = ({"Down"} if reverse else {"Up"}) if int(row["speed_integer"]) < 32 else set()
            if abs(error) > 50:
                wanted.add("Left" if (error > 0 if reverse else error < 0) else "Right")
            for key in held - wanted:
                xdo("keyup", key)
            for key in wanted - held:
                xdo("keydown", key)
            held = wanted
            time.sleep(.03)
        for key in held:
            xdo("keyup", key)
        require(entry is not None, f"Normal keyboard pit approach did not enter: waypoint {stage}, last row {ticks.rows()[-1]}")
        require(entry["pit_entries"] == entry["pit_plays"] == entry["ai_controlled"] == "1",
                "Original pit gate must transfer the actual human car and play one entry announcement")
        # Opposing human pedals/steering must not replace the pit AI axes.
        xdo("keydown", "Down", "Left")
        wait_for(lambda: row if (row := ticks.rows()[-1]) and int(row["step"]) >= int(entry["step"]) + 10
                 and row["ai_controlled"] == "1" and int(row["throttle"]) > 0 else None,
                 "pit AI despite opposing keyboard inputs", timeout=8)
        xdo("keyup", "Down", "Left")
        capture_window(window, "pits-entry.png")
        width, height, pixels = read_png(Path("pits-entry.png"))
        require((width, height) == (640, 480) and len(set(pixels)) > 100, "Pit capture must contain the actual game scene")

        xdo("windowminimize", window)
        paused = wait_for(lambda: row if (row := rows()[-1])["scene_active"] == "0"
                          and row["audio_voices"] == "0" else None, "pit focus pause and audio stop")
        time.sleep(.25)
        frozen = rows()[-1]
        require(all(frozen[field] == paused[field] for field in
                    ("simulation_steps", "body_x", "body_y", "body_z", "pit_active", "pit_point", "pit_entries", "pit_exits", "pit_plays")),
                "Focus loss must freeze the pit actor, route cursor and event counters")
        xdo("windowmap", window)
        xdo("windowactivate", "--sync", window)
        wait_for(lambda: int(rows()[-1]["simulation_steps"]) > int(paused["simulation_steps"]), "pit resume")
        exited = wait_for(lambda: row if (row := ticks.rows()[-1])["pit_exits"] == "1" else None,
                          "complete original pit traversal and human handback", timeout=30)
        require(exited["pit_active"] == "0" and exited["pit_point"] == "0" and exited["pit_barriers"] == "1"
                and exited["ai_controlled"] == "0" and exited["pit_entries"] == exited["pit_plays"] == "1",
                "Pit exit must restore human controls/barriers without replaying the entry sound")
        history = ticks.rows()
        require(all(row["motion_boundary"] == "0" for row in history), "Native pit traversal reached unsupported ground")
        active = [row for row in history if row["pit_active"] == "1"]
        points = [int(row["pit_point"]) for row in active]
        require(set(points) == set(range(1, 32)) and all(a <= b <= a + 1 for a, b in zip(points, points[1:])),
                "Native car must cross all original Donington PITS gates in order")
        require(any(row["pit_barriers"] == "0" for row in active), "Final pit stages must apply original barrier suppression")
        xdo("keydown", "Right", "Up")
        handback = wait_for(lambda: row if (row := ticks.rows()[-1]) and int(row["step"]) > int(exited["step"]) + 5
                           and int(row["steering_angle"]) > 0 else None, "human steering after pit exit", timeout=8)
        xdo("keyup", "Right", "Up")
        require(handback["ai_controlled"] == "0", "Pit exit must retain actual human ownership")
        returned = leave_scene(lambda: rows()[-1])
        require(returned["music_track"] == "2", "Pit scene must restore menu music")
        xdo("key", "alt+F4")
        require(app.wait(timeout=5) == 0, "Pit flow failed to exit cleanly")
        print(f"PASS: keyboard pit entry at tick {entry['step']}, all gates, one sound, focus pause, human handback and menu return")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Pit checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("pits-wm.log").open("w") as log:
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
