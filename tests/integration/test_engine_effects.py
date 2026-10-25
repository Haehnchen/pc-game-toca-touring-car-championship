"""Original type8 exhaust flames through ordinary muted Time Trial menus."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import test_settings as settings
from test_extras import ExtrasWindow
from native_support import TraceReader, require, wait_for, window_manager_ready, xdo


class ExhaustWindow(ExtrasWindow):
    def __init__(self, fps):
        settings.SettingsWindow.__init__(
            self, "engine-effects", "engine-effects.cfg", "--state-dir", "engine-effects-profile",
            "--resolution", "640x480", "--vsync", "off", "--fps-limit", str(fps))


def run(executable, data_root, fps="60"):
    require(fps in ("15", "60"), "Native engine acceptance uses the two explicit presentation caps15/60")
    settings.executable, settings.data_root = executable, data_root
    app = ExhaustWindow(fps)
    ticks = TraceReader("engine-effects.csv.simulation.csv")
    frozen = ("simulation_steps", "engine_effect_updates", "exhaust_sprites", "exhaust_emitted",
              "exhaust_cooldown", "exhaust_phase", "exhaust_plays", "exhaust_sample")

    def live_flames(start):
        def observed():
            history = ticks.rows()[start:]
            if not history:
                return None
            row = history[-1]
            require(row["motion_boundary"] == "0", "Original type8 approach left supported ground")
            return row if int(row["engine_effect_updates"]) >= 5 and row["exhaust_sprites"] == "2" else None
        return wait_for(observed, "actual Time Trial type8 exhaust sprites", timeout=12)

    def enter_trial():
        app.main()
        app.key("Return", screen="race_menu")
        while app.current()["selection"] != "2":
            app.key("Down", screen="race_menu")
        app.key("Return", screen="time_trial_options", selection=2)
        app.key("Return", screen="name_entry")
        app.key("Return", screen="car_selection")
        while app.current()["car_id"] != "8":
            app.key("Right", screen="car_selection", car_available=1)
        app.key("Return", screen="track_selection", car_id=8)
        while app.current()["track_id"] != "0":
            app.key("Left", screen="track_selection")
        start = len(ticks.rows())
        app.key("Return", screen="scene", game_mode=2, stored_car_id=8)
        observed = live_flames(start)
        require(observed["exhaust_plays"] == "0" and observed["exhaust_sample"] == "0",
                "The original permanent type8 Time Trial branch must remain silent")
        return observed, start

    def quit_trial():
        if app.current()["pause_active"] == "0":
            app.key("Escape", pause_active=1, pause_selection=0)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=3, pause_selection=1)
        app.key("Up", pause_selection=0)
        app.key("Return", screen="time_trial_records", selection=0, music_track=2)
        app.key("Escape", screen="main_menu", selection=0, music_track=2)

    try:
        app.enter()
        app.option("unlocks", 1, True, extras_bonus_cars=1, extras_error=0)
        observed, start = enter_trial()
        require(int(observed["exhaust_emitted"]) >= 10 and int(observed["exhaust_cooldown"]) < 900,
                "Source type8 flames must use active ticks and the genuine global cooldown clock")
        paused = app.key("Escape", pause_active=1, pause_selection=0, scene_active=0)
        for width, height in ((640, 480), (960, 540), (960, 600)):
            app.capture(f"engine-effects-pause-{width}x{height}.png", width, height)
        time.sleep(.25)
        require(all(app.current()[field] == paused[field] for field in frozen),
                "Pause must freeze exhaust geometry, shared emission/timers and its audio counters")
        app.key("Escape", pause_active=0, scene_active=1)
        wait_for(lambda: int(app.current()["engine_effect_updates"]) > int(paused["engine_effect_updates"]) + 2,
                 "engine effects resume on active deadlines")
        xdo("windowminimize", app.window)
        inactive = wait_for(lambda: (row := app.current()) and row["scene_active"] == "0"
                            and row["audio_voices"] == "0" and row, "focus pauses engine state and actual audio")
        time.sleep(.25)
        require(all(app.current()[field] == inactive[field] for field in frozen),
                "Focus loss must freeze every exhaust update/emission/timer field")
        xdo("windowmap", app.window)
        xdo("windowactivate", "--sync", app.window)
        wait_for(lambda: int(app.current()["engine_effect_updates"]) > int(inactive["engine_effect_updates"]) + 2,
                 "focus regain resumes the real type8 effect")
        app.capture("engine-effects-active.png", 960, 540)
        history = ticks.rows()[start:]
        require(history and all(int(row["engine_effect_updates"]) == int(row["step"]) for row in history),
                "Engine-effect clock must follow simulation steps independently of the15/60 presentation cap")
        require(all(row["exhaust_plays"] == "0" and row["exhaust_sample"] == "0" for row in history),
                "Time Trial type8 flames cannot invent repeated backfire sounds")

        # Time Trial has an original Quit entry, not Single Race's Race Again.
        # Reenter through its normal menus to exercise fresh scene setup.
        quit_trial()
        restarted, restart_start = enter_trial()
        first_restart = ticks.rows()[restart_start]
        require(int(first_restart["engine_effect_updates"]) <= 1 and 2 <= int(first_restart["exhaust_emitted"]) <= 4
                and int(first_restart["exhaust_cooldown"]) >= 899,
                "A newly entered Time Trial must restart4209d0 timers and emission totals rather than retain the previous scene")
        require(restarted["exhaust_sprites"] == "2" and restarted["exhaust_plays"] == "0",
                "Fresh native scene must retain the original silent type8 effect")
        quit_trial()
        app.quit()
        print(f"PASS: native type8 Time Trial exhaust at{fps}FPS, original menus, real sprites, three aspects, "
              "pause/focus, silent source branch, cadence and new-scene restart")
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Engine checks require private muted audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("engine-effects-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private engine-effects window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
