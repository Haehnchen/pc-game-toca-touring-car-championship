"""Produce original wheel particles and tyre marks with actual menu/grid inputs."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from native_support import capture_window, TraceReader, read_png, require, wait_for, window_manager_ready, xdo


def run(executable, data_root, selected_weather):
    wet = int(selected_weather) == 3
    trace = TraceReader("surface.csv")
    ticks = TraceReader("surface.csv.simulation.csv")
    size = "960x540" if wet else "640x480"
    with Path("surface.log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "surface.cfg",
                                "--resolution", size, "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "60" if wet else "15",
                                "--audio-offline", "--trace", "surface.csv"], stdout=log, stderr=subprocess.STDOUT)

    def rows():
        require(app.poll() is None, "Application exited; see surface.log")
        return trace.rows()

    try:
        wait_for(rows, "surface-flow startup", timeout=25)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            previous = int(rows()[-1]["poll"])
            xdo("key", key)
            return wait_for(lambda: next((row for row in rows() if int(row["poll"]) > previous
                                          and row["screen"] == screen and all(row[field] == str(value) for field, value in values.items())), None),
                            f"{key}: {screen} {values}", timeout=12)

        action("space", "language")
        for screen in ("main_menu", "race_menu", "single_options"):
            action("Return", screen)
        action("Up", "single_options", selection=3)
        for choice in range(1, int(selected_weather) + 1):
            action("Right", "single_options", weather=choice)
        action("Up", "single_options", selection=2)
        action("Left", "single_options", qualifying=0)
        action("Down", "single_options", selection=3)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            action("Return", screen)
        require(rows()[-1]["resolved_weather"] == selected_weather, "Selected weather must reach the actual driving scene")
        xdo("keydown", "Up")
        if not wet:
            xdo("keydown", "Left")

        def emitted():
            history = ticks.rows()
            if not history:
                return None
            row = history[-1]
            require(row["motion_boundary"] == "0", "Surface-effect drive left supported ground")
            on_loose = any(row[f"surface_{wheel}"] in ("2", "3") for wheel in range(4))
            active = (int(row["surface_trails"]) > 0 and int(row["speed"]) > 0x8000) if wet else (
                on_loose and int(row["surface_particles"]) >= 5 and int(row["surface_kind_mask"]) & ((1 << 4) | (1 << 5)))
            audible = row["surface_audio_sample"] == "81" and int(row["surface_audio_volume"]) > 0 if wet else True
            marks = int(row["skid_persistent"]) > 0 and int(row["skid_emitted"]) > 0
            return row if active and audible and marks else None

        observed = wait_for(emitted, "wheel-triggered water spray" if wet else "real loose-surface particles", timeout=18)
        xdo("keyup", "Up", "Left")
        path = Path("surface-wet.png" if wet else "surface-dry.png")
        capture_window(window, path)
        width, height, pixels = read_png(path)
        require(f"{width}x{height}" == size and len(set(pixels)) > 100, "Surface capture must contain the actual game window")
        paused = action("Escape", "scene", pause_active=1)
        time.sleep(.25)
        frozen_fields = ("surface_updates", "surface_particles", "surface_trails", "surface_emitted",
                         "skid_updates", "skid_persistent", "skid_live", "skid_emitted", "skid_cursor",
                         "wheel_queue_updates", "wheel_queue_events", "wheel_audio_events")
        require(all(rows()[-1][field] == paused[field] for field in frozen_fields), "Pause menu must freeze surface particle state")
        action("Escape", "scene", pause_active=0)
        wait_for(lambda: int(rows()[-1]["surface_updates"]) > int(paused["surface_updates"]) + 2, "surface resume")
        xdo("windowminimize", window)
        inactive = wait_for(lambda: row if (row := rows()[-1])["scene_active"] == "0" and row["audio_voices"] == "0" else None,
                            "surface focus pause and audio stop")
        time.sleep(.25)
        require(all(rows()[-1][field] == inactive[field] for field in frozen_fields), "Focus loss must freeze surface effects")
        xdo("windowmap", window)
        xdo("windowactivate", "--sync", window)
        wait_for(lambda: int(rows()[-1]["surface_updates"]) > int(inactive["surface_updates"]) + 2, "surface focus resume")
        history = ticks.rows()
        require(all(int(row["surface_updates"]) == int(row["step"]) for row in history), "Surface effects must follow simulation deadlines independently of render FPS")
        require(all(row["motion_boundary"] == "0" for row in history), "Surface test reached unsupported ground")
        require(all(int(row["skid_updates"]) == int(row["step"]) for row in history), "Tyre marks must follow simulation deadlines")
        require(all(int(row["wheel_queue_updates"]) == int(row["step"]) for row in history),
                "Wheel queues must advance exactly once per active simulation tick independently of render FPS")
        require(any(int(row["skid_volume"]) > 0 and int(row["skid_sample"]) in (*range(39, 45), 47, 48, 49) for row in history),
                "Real slipping wheels must retain their original tyre audio")
        require(any(int(row["skid_volume"]) > 0 and int(row["skid_pan"]) != 64 for row in history),
                "Actual wheel positions and camera direction must reach tyre-skid stereo playback")
        if wet:
            require(observed["rolling_pan"] == "64", "Wet asphalt on both car sides must retain the original centered rolling sound")
        # Warm Race Again clears live pools while retaining their shared clocks/tables.
        before_restart = action("Escape", "scene", pause_active=1, pause_selection=0)
        action("Up", "scene", pause_selection=2)
        action("Return", "scene", pause_page=2, pause_selection=0)
        restarted = action("Return", "scene", pause_active=0, start_held=1)
        require(restarted["surface_particles"] == "0" and restarted["surface_trails"] == "0"
                and restarted["surface_emitted"] == before_restart["surface_emitted"], "Race Again retained old wheel particles or lost cumulative emission history")
        require(restarted["skid_persistent"] == "0" and restarted["skid_live"] == "0" and restarted["skid_emitted"] == before_restart["skid_emitted"],
                "Race Again retained old visible tyre marks")
        require(restarted["skid_cursor"] == before_restart["skid_cursor"], "Scene restart lost the original persistent ring cursor")
        require(all(restarted[field] == before_restart[field] for field in
                    ("fragment_cursor", "damage_cycle", "exhaust_cooldown", "exhaust_phase", "wheel_queue_updates", "wheel_queue_events", "wheel_audio_events")),
                "Warm Race Again must retain shared effect phases and cumulative audio diagnostics")
        require(restarted["surface_audio_volume"] == "0" and restarted["rival_skid_voices"] == "0",
                "Warm Race Again must stop old wheel voices and discard prepared audio queues")
        xdo("key", "alt+F4")
        require(app.wait(timeout=5) == 0, "Surface flow failed to exit cleanly")
        print(f"PASS: weather {selected_weather}, real wheel emission and tyre marks at tick {observed['step']}, "
              f"surface audio and source tyre pan, pause/focus, race reset and simulation cadence")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Surface checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("surface-wm.log").open("w") as log:
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
