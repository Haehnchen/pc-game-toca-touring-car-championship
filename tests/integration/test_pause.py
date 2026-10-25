"""Exercise the original pause menu, audio controls and qualifying skip in-game."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import test_settings as settings
from native_support import capture_window, TraceReader, leave_scene, read_png, require, wait_for, window_manager_ready, xdo


class PauseWindow(settings.SettingsWindow):
    def key(self, key, **values):
        before = int(self.current()["poll"])
        xdo("key", key)
        return wait_for(lambda: (row := self.current()) and int(row["poll"]) > before
                        and all(row.get(field) == str(value) for field, value in values.items()) and row,
                        f"{key}: {values}", timeout=10)

    def capture(self, name, width, height):
        xdo("windowsize", "--sync", self.window, width, height)
        self.wait("pause resized", width=width, height=height, pause_active=1)
        frame = int(self.current()["frame"])
        wait_for(lambda: int(self.current()["frame"]) >= frame + 2, "pause drawn")
        target = Path(name)
        capture_window(self.window, target)
        w, h, pixels = read_png(target)
        require((w, h) == (width, height) and len(set(pixels)) > 100, "Pause capture must show real scene and menu")


def run():
    app = PauseWindow("pause", "pause.cfg", "--resolution", "640x480", "--vsync", "off", "--fps-limit", "30")
    ticks = TraceReader(Path("pause.csv.simulation.csv"))
    try:
        app.wait("startup", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        app.key("Up", screen="language", selection=2)
        app.key("Return", screen="main_menu")
        app.key("Down", selection=1)
        app.key("Return", screen="settings", selection=0)
        app.key("Return", screen="sound_options", selection=0)
        app.key("Left", effects_volume=90)
        app.key("Down", selection=1)
        app.key("Left", music_volume=90)
        app.key("Down", selection=2)
        app.key("Right", game_music=0, music_track=2, music_playing=1)
        app.key("Down", selection=3)
        app.key("Right", stereo_effects=0)
        app.key("Escape", screen="settings", selection=0)
        app.key("Escape", screen="main_menu", selection=1)
        app.key("Up", selection=0)
        for screen in ("race_menu", "single_options", "difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            app.key("Return", screen=screen)
        app.wait("qualifying rolling", session_state=4, ai_controlled=1)
        paused = app.key("Escape", screen="scene", pause_active=1, pause_page=0, scene_active=0)
        music_starts = paused["music_starts"]
        for width, height in ((640, 480), (960, 540), (960, 600)):
            app.capture(f"pause-{width}x{height}.png", width, height)
        frozen = app.current()
        fields = ("simulation_steps", "simulation_ns", "body_x", "body_y", "body_z", "flag_updates", "horizon_updates", "prop_updates", "lap_time_ms")
        require(all(paused[field] == frozen[field] for field in fields), "Pause advanced scene simulation or timers")
        require(frozen["music_starts"] == music_starts and frozen["music_playing"] == "0", "Frontend Game Music Off must prevent scene music")
        xdo("windowminimize", app.window)
        app.wait("unfocused pause", focused=0)
        xdo("windowmap", app.window)
        xdo("windowactivate", "--sync", app.window)
        app.wait("focused pause remains paused", focused=1, pause_active=1, scene_active=0)
        require(all(paused[field] == app.current()[field] for field in fields), "Focus resume bypassed the pause menu")
        app.key("Down", pause_selection=1)
        app.key("Return", pause_page=1, pause_selection=0)
        app.key("Left", effects_volume=80)
        app.key("Down", pause_selection=1)
        app.key("Left", music_volume=80)
        app.key("Left", music_volume=70)
        app.key("Down", pause_selection=2)
        app.key("Right", game_music=1, music_starts=int(music_starts) + 1)
        app.key("Right", game_music=0, music_track=0)
        app.key("Right", game_music=1, music_starts=int(music_starts) + 2)
        app.capture("pause-sound.png", 960, 540)
        app.key("Escape", pause_active=0, scene_active=1)
        wait_for(lambda: int(app.current()["simulation_steps"]) > int(paused["simulation_steps"]), "scene resumes")
        app.key("Escape", pause_active=1, pause_selection=0)
        app.key("Down", pause_selection=1)
        app.key("Down", pause_selection=2)
        ended = app.key("Return", pause_active=0, session_state=7, best_lap_ms=3600000, qualifying_rank=16)
        require(ended["completed_laps"] == "0" and ended["ai_controlled"] == "1" and ended["finish_playing"] == "1",
                "Skipping must enter real finish flow with source no-time record and finish audio")
        time.sleep(1.15)
        app.key("Return", session_state=8)
        frame = int(app.current()["frame"])
        wait_for(lambda: int(app.current()["frame"]) > frame + 1, "qualifying table drawn")
        capture_window(app.window, "pause-qualifying-results.png")
        app.key("Return", session_state=9, qualifying_phase=0, start_held=1, pause_active=0)
        app.key("Escape", pause_active=1, scene_active=0)
        xdo("keydown", "Up")
        app.wait("held menu input consumed", pause_selection=2)
        app.key("Escape", pause_active=0, scene_active=1)
        time.sleep(.16)
        xdo("keyup", "Up")
        require(ticks.rows()[-1]["held"] == "0", "Menu-held input leaked into resumed driving")
        app.key("Escape", pause_active=1, pause_selection=0)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=2, pause_selection=0)
        app.key("Escape", pause_active=1, scene_active=0, pause_page=2, pause_selection=0)
        before_restart = app.current()
        restarted = app.key("Return", screen="scene", session_state=4, qualifying_phase=1, pause_active=0)
        require(app.current()["best_lap_ms"] == "0", "Race Again must reset original qualifying setup and records")
        require(restarted["ai_controlled"] == "1" and restarted["start_held"] == "0" and restarted["rival_count"] == "3"
                and restarted["completed_laps"] == "0" and restarted["release_counter"] == "0",
                "Qualifying Race Again must restore the AI-owned rolling approach rather than the last race grid")
        retained = ("simulation_steps", "simulation_ns", "source_rng_state", "fragment_cursor", "damage_cycle", "exhaust_cooldown", "exhaust_phase", "skid_cursor")
        require(all(restarted[field] == before_restart[field] for field in retained),
                "Warm qualifying restart must retain the world and clocks without grid-classification draws or an extra tick")
        require(not any(row["screen"] == "scene_loading" for row in app.rows()
                        if int(before_restart["poll"]) < int(row["poll"]) <= int(restarted["poll"])),
                "Warm qualifying restart must retain its loaded resources")
        restart_step = int(restarted["simulation_steps"])
        warm_ticks = wait_for(lambda: history if len(history := [row for row in ticks.rows() if int(row["step"]) > restart_step]) >= 2 else None,
                              "warm qualifying rollout updates")[:2]
        require([int(row["simulation_ns"]) - int(restarted["simulation_ns"]) for row in warm_ticks] == [40_000_000, 80_000_000]
                and all(row["session_state"] == "4" and row["ai_controlled"] == "1" and row["start_held"] == "0"
                        and row["motion_boundary"] == "0" and int(row["speed"]) > 0 for row in warm_ticks),
                "Warm qualifying rollout must move under ordinary AI physics at the next 40/80ms deadlines")
        app.key("Escape", pause_active=1, pause_page=0)
        app.key("Up", pause_selection=3)
        app.key("Return", pause_page=2, pause_selection=0)
        before_rollout_restart = app.current()
        repeated = app.key("Return", session_state=4, qualifying_phase=1, pause_active=0, ai_controlled=1)
        require(repeated["source_rng_state"] == before_rollout_restart["source_rng_state"]
                and repeated["simulation_steps"] == before_rollout_restart["simulation_steps"],
                "Restarting an active qualifying approach must reuse the same warm path without RNG or physics work")
        leave_scene(app.current)
        app.wait("frontend return", screen="main_menu", music_track=2)
        saved = settings.preferences(Path("pause.cfg"))
        require(saved["effects_volume"] == "80" and saved["music_volume"] == "70" and saved["game_music"] == "on" and saved["stereo_effects"] == "off",
                "Pause audio changes were not persisted")
        app.key("Down", selection=1)
        app.key("Return", screen="settings", selection=0)
        app.key("Return", screen="sound_options", selection=0, effects_volume=80, music_volume=70, game_music=1, stereo_effects=0)
        app.key("Left", effects_volume=70)
        require(settings.preferences(Path("pause.cfg"))["music_volume"] == "70", "Frontend sound overwrote newer pause settings")
        app.key("Escape", screen="settings", selection=0)
        app.key("Escape", screen="main_menu", selection=1)
        app.key("Up", selection=0)
        app.key("Up", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Pause flow exit failed")
        print("PASS: original pause, three aspects, focus/input isolation, sound/music, qualifying skip, warm qualifying restarts and return")
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Pause checks require private muted audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    settings.executable, settings.data_root = sys.argv[1:]
    with Path("pause-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        run()
    finally:
        settings.stop(manager)
