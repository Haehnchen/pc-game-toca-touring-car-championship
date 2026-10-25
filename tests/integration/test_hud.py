"""Capture newly authored Mega HUD artwork through the normal race route."""

import os
from pathlib import Path
import subprocess
import sys

from native_support import TraceReader, capture_window, leave_scene, require, wait_for, window_manager_ready, xdo


def run(executable, data_root):
    trace = TraceReader(Path("hud.csv"))
    ticks = TraceReader(Path("hud.csv.simulation.csv"))
    with Path("hud.log").open("w") as log:
        app = subprocess.Popen([executable, "--data-root", data_root, "--config", "hud.cfg",
                                "--resolution", "1280x720", "--vsync", "off", "--fps-limit", "30",
                                "--graphics-detail", "4", "--audio-offline", "--trace", "hud.csv"],
                               stdout=log, stderr=subprocess.STDOUT)

    def row():
        require(app.poll() is None, "Application exited; see hud.log")
        current = trace.rows()
        return current[-1] if current else None

    try:
        wait_for(row, "HUD startup", timeout=10)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            previous = int(row()["poll"])
            xdo("key", key)
            return wait_for(lambda: current if (current := row()) and int(current["poll"]) > previous
                            and current["screen"] == screen
                            and all(current[field] == str(value) for field, value in values.items()) else None,
                            f"{key}: {screen} {values}", timeout=10)

        action("space", "language")
        frame = int(row()["frame"])
        wait_for(lambda: (current := row())["presented"] == "1" and int(current["frame"]) >= frame + 2
                 and int(current["menu_animation_ns"]) - int(current["menu_entered_ns"]) >= 500_000_000,
                 "Mega language font presentation")
        capture_window(window, Path("mega-font-language.png"))
        for screen in ("main_menu", "race_menu", "single_options"):
            action("Return", screen)
            if screen in ("main_menu", "single_options"):
                frame = int(row()["frame"])
                wait_for(lambda: (current := row())["presented"] == "1" and int(current["frame"]) >= frame + 2,
                         "Mega menu font presentation")
                capture_window(window, Path(f"mega-font-{screen}.png"))
        action("Up", "single_options", selection=3)
        action("Up", "single_options", selection=2)
        action("Left", "single_options", qualifying=0)
        action("Down", "single_options", selection=3)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            entered = action("Return", screen)
        require(entered["graphics_detail"] == "4" and entered["scene_detail"] == "4",
                "Mega must reach the playable scene")
        require(entered["hud_lap"] == "1" and entered["hud_lap_limit"] == entered["laps"]
                and entered["hud_lap_time_ms"] == "0" and entered["hud_best_lap_ms"] == "0",
                "Mega artwork must retain original initial records")
        xdo("keydown", "Up")
        for phase in (2, 3, 4):
            wait_for(lambda: current if (current := row())["start_phase"] == str(phase) else None,
                     f"HUD start phase{phase}", timeout=12)
            frame = int(row()["frame"])
            wait_for(lambda: (current := row())["presented"] == "1" and int(current["frame"]) >= frame + 2,
                     "Mega artwork presentation")
            capture_window(window, Path(f"mega-hud-phase{phase}.png"))
        xdo("keyup", "Up")
        records = ticks.rows()
        require(all(value["hud_lap_time_ms"] == value["lap_time_ms"]
                    and value["hud_best_lap_ms"] == value["best_lap_ms"]
                    and value["hud_lap"] == value["session_lap"] for value in records),
                "Replacement artwork must not change cached HUD records")
        require(any(int(value["hud_speed"]) > 0 for value in trace.rows()), "HUD must display actual moving-car speed")
        transitions = {}
        for value in records:
            transitions.setdefault(int(value["start_phase"]), int(value["simulation_ns"]) // 1_000_000)
        require([transitions[i] for i in (1, 2, 3, 4)] == [3080, 6160, 9240, 10840],
                "Replacement artwork changed original start deadlines")
        require(leave_scene(row)["music_track"] == "2", "Scene exit must restore menu music")
        for screen in ("race_menu", "single_options"):
            action("Return", screen)
        action("Up", "single_options", selection=3)
        action("Up", "single_options", selection=2)
        action("Right", "single_options", qualifying=1)
        action("Down", "single_options", selection=3)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            action("Return", screen)
        wait_for(lambda: (current := row())["ai_controlled"] == "1" and int(current["next_point"]) >= 5,
                 "Mega qualifying caption during the original automatic approach", timeout=10)
        frame = int(row()["frame"])
        wait_for(lambda: (current := row())["presented"] == "1" and int(current["frame"]) >= frame + 2,
                 "Mega caption presentation")
        capture_window(window, Path("mega-caption-qualifying.png"))
        xdo("key", "Escape")
        wait_for(lambda: row()["pause_active"] == "1", "Mega pause title")
        frame = int(row()["frame"])
        wait_for(lambda: int(row()["frame"]) >= frame + 2, "Mega pause title presentation")
        capture_window(window, Path("mega-caption-pause.png"))
        xdo("key", "Escape")
        wait_for(lambda: row()["pause_active"] == "0", "Resume Mega qualifying")
        require(leave_scene(row)["music_track"] == "2", "Qualifying exit must restore menu music")
        print("PASS: Mega menu fonts, HUD, qualifying/pause text, original start phases and live records through real menus")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=3)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "HUD checks require isolated dummy audio")
    with Path("hud-wm.log").open("w") as log:
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
