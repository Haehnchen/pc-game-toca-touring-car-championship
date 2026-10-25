"""Exercise original/Mega sample policy through real menus, HUD and pause lifecycle."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

from native_support import TraceReader, capture_window, leave_scene, read_png, require, wait_for, window_manager_ready, xdo


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


def run(executable, data_root, detail, track=0):
    prefix = f"antialiasing-detail{detail}" if track == 0 else f"antialiasing-track{track}-detail{detail}"
    profile = "Mega" if detail == 4 else "Very High"
    log_path = Path(f"{prefix}.log")
    trace = TraceReader(Path(f"{prefix}.csv"))
    ticks = TraceReader(Path(f"{prefix}.csv.simulation.csv"))
    with log_path.open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", f"{prefix}.cfg",
                                "--state-dir", f"{prefix}-state", "--resolution", "640x480",
                                "--vsync", "off", "--fps-limit", "30", "--graphics-detail", str(detail),
                                "--audio-offline", "--mute", "on",
                                "--trace", f"{prefix}.csv"], stdout=log, stderr=subprocess.STDOUT)

    def current():
        if app.poll() is not None:
            raise AssertionError(f"{prefix} application exited with{app.returncode}; see{log_path}")
        rows = trace.rows()
        return rows[-1] if rows else None

    try:
        wait_for(current, f"{prefix} startup", timeout=12)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, **values):
            previous = int(current()["poll"])
            xdo("key", key)
            return wait_for(lambda: row if (row := current()) and int(row["poll"]) > previous
                            and all(row.get(field) == str(value) for field, value in values.items()) else None,
                            f"{prefix}{key}:{values}", timeout=12)

        def presented(description, width, height):
            previous = int(current()["frame"])
            return wait_for(lambda: row if (row := current()) and row["presented"] == "1"
                            and int(row["frame"]) >= previous + 2
                            and (row["width"], row["height"]) == (str(width), str(height)) else None,
                            f"{prefix}{description}", timeout=12)

        def capture(name, width, height):
            row = presented(name, width, height)
            expected_samples = 2 if detail == 4 and "2x MSAA unavailable" not in log_path.read_text() else 1
            require(row["gpu_samples"] == str(expected_samples),
                    f"{profile} presented scene must use {expected_samples} samples")
            require(int(row["gpu_world_draw_calls"]) > 0,
                    f"{profile} presented scene must draw resident world geometry through the GPU projection shader")
            require(int(row["gpu_model_draw_calls"]) > 0 and int(row["gpu_model_instances"]) > 0,
                    f"{profile} presented scene must transform and light resident models on the GPU")
            mip_draws = int(row["gpu_mipmapped_draw_calls"])
            require(mip_draws > 0 if detail == 4 else mip_draws == 0,
                    f"{profile} presented ground must retain its profile-specific mipmap policy")
            target = Path(f"{prefix}-{name}.png")
            capture_window(window, target)
            actual_width, actual_height, pixels = read_png(target)
            require((actual_width, actual_height) == (width, height) and len(set(pixels)) > 64,
                    f"{prefix} capture must contain the real rendered scene at its drawable size")
            # The normal race HUD occupies the lower-right part of either
            # aspect. Require visible gauge artwork in the actual window capture.
            gauge = [pixels[y * width + x] for y in range(height * 4 // 5, height)
                     for x in range(width * 3 // 4, width)]
            require(len(set(gauge)) > 16, f"{prefix} scene must retain visible {profile} gauge artwork")

        action("space", screen="language")
        action("Return", screen="main_menu")
        if track == 9:
            action("Up", screen="main_menu", selection=4)
            action("Up", screen="main_menu", selection=3)
            action("Return", screen="extras", selection=0)
            action("Return", screen="extras_unlocks", selection=0)
            action("Right", screen="extras_unlocks", extras_all_tracks=1, extras_bonus_track=1)
            action("Escape", screen="extras", selection=0)
            action("Escape", screen="main_menu", selection=3)
            action("Down", screen="main_menu", selection=4)
            action("Down", screen="main_menu", selection=0)
        for screen in ("race_menu", "single_options", "difficulty", "name_entry", "car_selection", "track_selection"):
            action("Return", screen=screen)
        for selected in range(1, track + 1):
            action("Right", screen="track_selection", track_id=selected, track_available=1)
        entered = action("Return", screen="scene", track_id=track)
        require(entered["graphics_detail"] == str(detail) and entered["scene_detail"] == str(detail),
                f"{prefix} must load the selected {profile} scene through the normal route")
        start_step = int(entered["simulation_steps"])
        moving = wait_for(lambda: row if (row := current()) and row["scene_active"] == "1" and row["presented"] == "1"
                          and int(row["simulation_steps"]) >= start_step + 3 and int(row["hud_speed"]) > 0 else None,
                          f"{prefix} live scene and HUD", timeout=12)
        require(int(moving["scene_vertices"]) > 100 and int(moving["gpu_draw_calls"]) > 0,
                f"{prefix} must render actual world geometry")
        capture("hud-640x480", 640, 480)

        paused = action("Escape", screen="scene", pause_active=1, scene_active=0)
        frozen_fields = ("simulation_steps", "simulation_ns", "body_x", "body_y", "body_z", "lap_time_ms", "flag_updates", "horizon_updates", "prop_updates")
        xdo("windowsize", "--sync", window, 960, 540)
        capture("pause-960x540", 960, 540)
        frozen = current()
        require(frozen["pause_active"] == "1" and all(frozen[field] == paused[field] for field in frozen_fields),
                f"{prefix} pause/resize must freeze simulation while presenting the scene and menu")
        resumed = action("Escape", pause_active=0, scene_active=1)
        wait_for(lambda: (row := current()) and int(row["simulation_steps"]) >= int(resumed["simulation_steps"]) + 3,
                 f"{prefix} resumes real simulation after resize", timeout=12)
        capture("resumed-960x540", 960, 540)
        records = ticks.rows()
        require(records and all(row["hud_lap_time_ms"] == row["lap_time_ms"]
                                and row["hud_best_lap_ms"] == row["best_lap_ms"]
                                and row["hud_lap"] == row["session_lap"] for row in records),
                f"{prefix} must retain the authoritative HUD records")
        require(leave_scene(current)["music_track"] == "2", f"{prefix} scene exit must return to menu music")
        action("Up", screen="main_menu", selection=4)
        xdo("key", "Return")
        require(app.wait(timeout=8) == 0, f"{prefix} must exit normally")
        print(f"PASS:{prefix} real {profile} menu/scene route, sample policy, HUD, pause, resize, resume and exit")
    finally:
        stop(app)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Native AA checks require private muted audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    require(len(sys.argv) in (3, 4), "Expected GAME CANONICAL_ROOT [TRACK]")
    executable, data_root = sys.argv[1:3]
    track = int(sys.argv[3]) if len(sys.argv) == 4 else 0
    require(track in (0, 9), "AA integration accepts the original pilot or audited Lavaland")
    with Path("antialiasing-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        for detail in (3, 4):
            run(executable, data_root, detail, track)
    finally:
        stop(manager)
