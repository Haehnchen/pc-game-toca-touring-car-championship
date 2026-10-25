"""Mega hands and shared parts in native single-player and both local cockpits."""

import os
from pathlib import Path
import subprocess
import sys

import test_settings as settings
from test_extras import ExtrasWindow
from native_support import leave_scene, require, wait_for, window_manager_ready, xdo


class MegaWindow(ExtrasWindow):
    def __init__(self, name):
        settings.SettingsWindow.__init__(self, name, name + ".cfg", "--state-dir", name + "-state",
                                         "--graphics-detail", "4", "--resolution", "960x600",
                                         "--vsync", "off", "--fps-limit", "30")


def flow(split):
    name = "mega-cockpit-split" if split else "mega-cockpit"
    app = MegaWindow(name)
    try:
        app.enter()
        app.key("Return", screen="race_menu")
        app.key("Return", screen="single_options")
        app.select(0)
        if split:
            app.key("Right", players=2, qualifying=0)
        else:
            app.select(2)
            app.key("Right", qualifying=0)
        app.select(3)
        while app.current()["weather"] != "3":
            app.key("Right")
        app.select(4)
        app.key("Return", screen="difficulty")
        app.key("Return", screen="name_entry")
        app.key("Return", screen="car_selection")
        app.key("Down", manual_transmission=1)
        if split:
            app.key("Return", screen="name_entry", selection_player=1)
            app.key("Return", screen="car_selection", selection_player=1)
            require(app.current()["p2_selected_car"] == "0", "Both local Honda interiors must consume the new dashboard")
        app.key("Return", screen="track_selection")
        xdo("key", "Return")
        wait_for(lambda: (row := app.current()) and row["screen"] == "scene" and row["scene_detail"] == "4"
                 and row["local_players"] == ("2" if split else "1"), "full Mega scene load", timeout=30)
        for view in (3, 4, 1):
            xdo("key", "c")
            app.wait("first Mega cockpit camera cycle", camera_view=view)
            if view == 4:
                app.capture(name + "-bonnet.png", 960, 600)
        if split:
            for view in (3, 4, 1):
                xdo("key", "Tab")
                app.wait("second Mega cockpit camera cycle", p2_camera=view)
            require(app.current()["camera_view"] == "1", "Second driver camera changed the first cockpit")
        row = app.wait("inside engine samples", engine_camera_view=1)
        require(int(row["audio_voices"]) > 0, "Cockpit must retain its actual inside motor/audio path")
        log = Path(name + ".log").read_text()
        for role in (0, 1, 2, 3, 4):
            require(f"Mega cockpit: car0 role{role}" in log, "Both local Honda cockpits must use all five authored roles")
        require("Mega cockpit unavailable" not in log, "Native cockpit unexpectedly used role fallback")
        app.capture(name + ".png", 1280, 800)
        if not split:
            xdo("keydown", "z")
            app.wait("source rear view", camera_view=5)
            app.capture(name + "-rear.png", 960, 600)
            xdo("keyup", "z")
            app.wait("return to cockpit", camera_view=1, engine_camera_view=1)
            wait_for(lambda: app.current()["start_held"] == "0", "grid release", timeout=15)
            xdo("key", "f")
            app.wait("manual gear commit", engine_gear=1)
            wait_for(lambda: app.current()["cockpit_shift_phase"] == "0", "shift hand returns to new wheel")
            xdo("keydown", "Up")
            wait_for(lambda: int(app.current()["speed"]) > 20000, "Mega cockpit driving", timeout=12)
            xdo("keydown", "Left")
            before = int(app.current()["simulation_steps"])
            wait_for(lambda: int(app.current()["simulation_steps"]) > before + 8, "moving wheel and rain wiper")
            xdo("keyup", "Left")
            xdo("keyup", "Up")
            app.capture(name + "-moving.png", 1280, 720)
            xdo("windowminimize", app.window)
            frozen = app.wait("cockpit focus freeze", focused=0, minimized=1, audio_voices=0)
            wait_for(lambda: int(app.current()["poll"]) >= int(frozen["poll"]) + 4, "frozen cockpit polls")
            require(app.current()["simulation_steps"] == frozen["simulation_steps"], "Focus loss must freeze cockpit driving and animation")
            xdo("windowmap", "--sync", app.window)
            xdo("windowactivate", "--sync", app.window)
            app.wait("cockpit focus restored", focused=1, minimized=0)
        app.key("Escape", pause_active=1, scene_active=0, audio_voices=0)
        frozen = app.current()["simulation_steps"]
        app.capture(name + "-paused.png", 960, 600)
        require(app.current()["simulation_steps"] == frozen, "Paused Mega cockpit advanced simulation")
        app.key("Escape", pause_active=0, scene_active=1)
        leave_scene(app.current)
        app.wait("menu music restored", screen="main_menu", music_track=2)
        app.quit()
        print(f"PASS: {name}, new gloves/forearms and shared parts, rain, inside audio, actual cockpits and pause/return")
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Mega cockpit acceptance requires isolated muted audio")
    require(len(sys.argv) == 3, "Expected executable and canonical data root")
    settings.executable, settings.data_root = sys.argv[1:]
    with Path("mega-cockpit-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        flow(False)
        flow(True)
    finally:
        settings.stop(manager)
