"""Exercise playable Time Trial and saved ghosts through ordinary native menus."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import test_settings as settings
from native_support import capture_window, TraceReader, qualifying_rollout, read_png, require, wait_for, window_manager_ready, xdo


class TrialWindow(settings.SettingsWindow):
    def key(self, key, **values):
        previous = self.current()
        frontend = previous["screen"] not in ("title", "copyright", "scene", "scene_loading")
        xdo("key", key)
        return wait_for(lambda: (row := self.current()) and int(row["poll"]) > int(previous["poll"])
                        and (not frontend or int(row["frontend_cues"]) > int(previous["frontend_cues"]))
                        and all(row.get(field) == str(value) for field, value in values.items()) and row,
                        f"{key}: {values}", timeout=10)

    def capture(self, name, width, height):
        xdo("windowsize", "--sync", self.window, width, height)
        self.wait("Time Trial drawable resize", width=width, height=height)
        frame = int(self.current()["frame"])
        wait_for(lambda: int(self.current()["frame"]) >= frame + 2, "Time Trial presentation")
        target = Path(name)
        capture_window(self.window, target)
        w, h, pixels = read_png(target)
        require((w, h) == (width, height) and len(set(pixels)) > 100,
                "Time Trial capture must contain the actual textured scene and menu")


def run(executable, data_root, fixture_executable):
    state_root = Path("trial-state").resolve()
    subprocess.run([fixture_executable, "--fixture", data_root, str(state_root)], check=True, timeout=15)
    stored = state_root / "time-trial/track-0.ghost"
    original = stored.read_bytes()
    magic, version, track, car, weather, best_ms, samples, name_size = struct.unpack_from("<4s7I", original)
    require((magic, version, track, car, weather) == (b"TCTT", 3, 0, 1, 1) and best_ms > 0 and samples > 100
            and len(original) == 32 + name_size + samples * 28,
            "Fixture must record a complete Audi lap through canonical ordered gates")
    settings.executable, settings.data_root = executable, data_root
    app = TrialWindow("time-trial", "time-trial.cfg", "--state-dir", str(state_root),
                      "--resolution", "640x480", "--vsync", "off", "--fps-limit", "30", "--graphics-detail", "3")
    ticks = TraceReader(Path("time-trial.csv.simulation.csv"))

    def enter_scene():
        app.key("Return", screen="name_entry")
        app.key("Return", screen="car_selection", car_id=0)
        app.key("Return", screen="track_selection", car_id=0, track_id=0)
        start = len(ticks.rows())
        entered = app.key("Return", screen="scene", game_mode=2, qualifying_phase=0)
        require(entered["session_state"] == "4" and entered["ai_controlled"] == "1"
                and entered["hud_lap_limit"] == "0" and entered["start_plays"] == "0"
                and entered["music_track"] == "3" and entered["ghost_error"] == "0" and entered["pause_active"] == "0",
                "Time Trial must enter its real unlimited rolling approach with track music and no grid cues")
        return entered, start

    def quit_trial(expected_ghost=original):
        if app.current()["pause_active"] == "0":
            app.key("Escape", pause_active=1, pause_selection=0)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=3, pause_selection=1)
        app.key("Up", pause_selection=0)
        returned = app.key("Return", screen="time_trial_records", selection=0, music_track=2)
        require(returned["pause_active"] == "0" and returned["scene_active"] == "0",
                "Confirmed Time Trial exit must release its scene before restarting CD02")
        require(stored.read_bytes() == expected_ghost, "An uncompleted/slower session must preserve the saved ghost file exactly")
        app.key("Return", screen="time_trial_records", selection=1)
        app.key("Escape", screen="time_trial_records", selection=0)
        app.key("Return", screen="time_trial_records", selection=1)
        app.key("Return", screen="main_menu", selection=0, music_track=2)

    def reopen_options():
        app.key("Return", screen="race_menu", selection=2)
        app.key("Return", screen="time_trial_options", selection=2)

    try:
        app.wait("startup", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        app.key("Return", screen="main_menu")
        app.key("Return", screen="race_menu", selection=0)
        app.key("Down", selection=1)
        app.key("Down", selection=2)
        app.key("Return", screen="time_trial_options", selection=2)
        app.key("Up", selection=1)
        app.key("Right", selection=1)  # Fresh source default: LOAD BEST LAP NO -> YES.
        app.key("Down", selection=2)
        entered, start = enter_scene()
        app.capture("time-trial-start.png", 640, 480)
        require(entered["ghost_samples"] == str(samples) and entered["ghost_best_ms"] == str(best_ms)
                and entered["ghost_car"] == "1" and entered["car_id"] == "0" and entered["ghost_dirty"] == "0"
                and entered["weather"] == "0" and entered["resolved_weather"] == "2",
                "Saved Audi pose stream and time must load independently of the selected Honda player")
        qualifying_rollout(ticks, start)
        active = wait_for(lambda: (row := app.current()) and row["session_state"] == "6" and row["ghost_visible"] == "1" and row,
                          "saved ghost becomes visible after the actual start-line release", timeout=5)
        app.capture("time-trial-player-control.png", 640, 480)
        position = tuple(active[field] for field in ("ghost_x", "ghost_y", "ghost_z"))
        wait_for(lambda: (row := app.current()) and row["ghost_visible"] == "1"
                 and tuple(row[field] for field in ("ghost_x", "ghost_y", "ghost_z")) != position,
                 "saved ghost moves along its recorded lap")
        require(int(app.current()["ghost_recording_samples"]) > 0,
                "Playing the prior best must record the player's current attempt at the same time")
        paused = app.key("Escape", pause_active=1, pause_page=0, pause_selection=0, scene_active=0)
        frozen_fields = ("simulation_steps", "simulation_ns", "body_x", "body_y", "body_z", "lap_time_ms",
                         "ghost_x", "ghost_y", "ghost_z", "ghost_visible", "ghost_recording_samples")
        for width, height in ((640, 480), (960, 540), (960, 600)):
            app.capture(f"time-trial-pause-{width}x{height}.png", width, height)
        require(all(app.current()[field] == paused[field] for field in frozen_fields),
                "Pause must freeze the player, measured time, ghost cursor and current recording")
        xdo("windowminimize", app.window)
        app.wait("unfocused pause", focused=0)
        xdo("windowmap", app.window)
        xdo("windowactivate", "--sync", app.window)
        app.wait("focused pause remains paused", focused=1, pause_active=1, scene_active=0)
        require(all(app.current()[field] == paused[field] for field in frozen_fields),
                "Focus regain must retain paused Time Trial timing and both pose streams")
        app.key("Down", pause_selection=1)
        app.key("Return", pause_page=1, pause_selection=0)
        app.key("Left", effects_volume=90)
        app.key("Down", pause_selection=1)
        app.key("Left", music_volume=90)
        app.capture("time-trial-sound.png", 960, 540)
        app.key("Down", pause_selection=2)
        app.key("Down", pause_selection=3)
        app.key("Return", pause_page=0, pause_selection=0)
        saved = settings.preferences(Path("time-trial.cfg"))
        require(saved["effects_volume"] == "90" and saved["music_volume"] == "90",
                "Time Trial pause sound changes must use the live persisted audio controls")
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=3, pause_selection=1)
        app.capture("time-trial-quit-no.png", 960, 540)
        app.key("Escape", pause_page=3, pause_selection=1, pause_active=1)
        app.key("Return", pause_page=0, pause_selection=0, pause_active=1)
        require(all(app.current()[field] == paused[field] for field in frozen_fields),
                "QUIT TIME TRIAL NO must return to Continue with the current attempt intact")
        app.key("Return", pause_active=0, scene_active=1)
        resumed = wait_for(lambda: (row := app.current()) and int(row["simulation_steps"]) > int(paused["simulation_steps"]) + 2 and row,
                           "Time Trial resumes its actual attempt")
        require(int(resumed["lap_time_ms"]) > int(paused["lap_time_ms"])
                and int(resumed["ghost_recording_samples"]) > int(paused["ghost_recording_samples"]),
                "Resume must advance the current lap and recording rather than restarting either")
        quit_trial()

        reopen_options()
        app.key("Up", selection=1)
        app.key("Left", selection=1)  # LOAD BEST LAP YES -> NO.
        app.key("Down", selection=2)
        entered, start = enter_scene()
        require(entered["ghost_samples"] == "0" and entered["ghost_best_ms"] == "0" and entered["ghost_visible"] == "0",
                "Load Best Lap NO must start without the saved ghost or its session best")
        qualifying_rollout(ticks, start)
        wait_for(lambda: (row := app.current()) and int(row["ghost_recording_samples"]) > 3,
                 "Time Trial records an attempt with prior-ghost loading disabled")
        require(app.current()["ghost_visible"] == "0" and app.current()["ghost_dirty"] == "0",
                "An incomplete first attempt must not fabricate a ghost or mark the saved record dirty")
        quit_trial()

        reopen_options()
        app.key("Up", selection=1)
        app.key("Right", selection=1)
        app.key("Down", selection=2)
        reloaded, _ = enter_scene()
        require(reloaded["ghost_samples"] == str(samples) and reloaded["ghost_best_ms"] == str(best_ms)
                and reloaded["ghost_car"] == "1" and reloaded["ghost_error"] == "0",
                "Reenabled loading must recover the same faster saved Audi ghost after no-load play")
        quit_trial()
        unsupported = bytearray(original)
        struct.pack_into("<I", unsupported, 4, 0xffffffff)
        stored.write_bytes(unsupported)
        reopen_options()
        entered, _ = enter_scene()
        require(entered["ghost_samples"] == "0" and entered["ghost_best_ms"] == "0" and entered["ghost_visible"] == "0",
                "An unsupported ghost format must start like a missing file, without a pause or load error")
        quit_trial(unsupported)
        stored.write_bytes(original)
        app.key("Down", screen="main_menu", selection=1)
        app.key("Down", screen="main_menu", selection=2)
        app.key("Return", screen="time_trial_records", selection=0)
        app.key("Return", screen="time_trial_records", selection=1)
        app.key("Escape", screen="time_trial_records", selection=0)
        app.key("Escape", screen="main_menu", selection=2)
        app.key("Up", screen="main_menu", selection=1)
        app.key("Up", screen="main_menu", selection=0)
        app.key("Up", screen="main_menu", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Time Trial acceptance must exit cleanly")
        require(stored.read_bytes() == original, "Frontend return or application shutdown changed the saved faster ghost")
        print(f"PASS: ordinary Time Trial route, Audi ghost for Honda player, {samples} poses/{best_ms}ms, "
              "real approach, motion/recording, three aspects, pause/focus/No, exit/CD02 and enabled/disabled reload")
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Time Trial checks require private muted audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("time-trial-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
