"""Rebind the native keyboard, persist it, and drive a short real Time Trial."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

import test_settings as settings
from test_time_trial import TrialWindow
from native_support import StepInput, TraceReader, qualifying_rollout, require, wait_for, window_manager_ready, xdo


BINDINGS = {"throttle": "k26", "brake": "k22", "left": "k4", "right": "k7", "gear_up": "k8", "gear_down": "k20",
            "camera_rear": "k21", "camera_next": "k23", "horn": "k11"}
PLAYER2_KEYS = ("i", "k", "j", "l", "u", "o", "b", "n", "m")
PLAYER2_BINDINGS = dict(zip(BINDINGS, ("k12", "k14", "k13", "k15", "k24", "k18", "k5", "k17", "k16")))


def configured_keys(path, player2=False):
    values = settings.preferences(path)
    require(all(values[f"key_{action}"] == code for action, code in BINDINGS.items()),
            "The completed W/S/A/D/E/Q/R/T/H capture must persist all nine SDL scancodes together")
    if player2:
        require(all(values[f"p2_key_{action}"] == code for action, code in PLAYER2_BINDINGS.items()),
                "Player two must persist its own complete I/K/J/L/U/O/B/N/M map independently")
    return values


def configure(config):
    app = TrialWindow("controller-setup", config, "--resolution", "640x480", "--vsync", "off", "--fps-limit", "60")
    try:
        app.enter()
        app.open_options(2, "controller_players")
        app.capture("controller-setup-players.png", 640, 480)
        app.key("Down", screen="controller_players", selection=1)
        app.key("Down", screen="controller_players", selection=2)
        app.key("Down", screen="controller_players", selection=0)
        app.key("Return", screen="controller_options", controller_player=0)
        app.capture("controller-setup-menu.png", 640, 480)
        app.select(1)
        app.key("Return", screen="controller_keyboard_bindings", binding_capture_index=0)
        app.key("w", screen="controller_keyboard_bindings", binding_capture_index=1)
        app.capture("controller-setup-wizard.png", 640, 480)
        app.key("Escape", screen="controller_options", selection=1)
        require(not config.exists() and app.current()["settings_requests"] == "0",
                "Cancelling a partial capture must leave runtime preferences and disk untouched")

        app.key("Return", screen="controller_keyboard_bindings", binding_capture_index=0)
        # F1 keysym synthesis on Xvfb may press Alt first; use a reserved
        # physical key already exercised by this window. CPU coverage tests F1.
        app.key("Return", screen="controller_keyboard_bindings", binding_capture_index=0)
        app.key("w", screen="controller_keyboard_bindings", binding_capture_index=1)
        app.key("w", screen="controller_keyboard_bindings", binding_capture_index=1)
        require(not config.exists(), "Reserved and duplicate inputs must not persist a partial map")
        # A directory at the final file path fails atomically, independently of
        # user permissions. The already running application loaded no such file.
        config.mkdir()
        for index, key in enumerate(("s", "a", "d", "e", "q", "r", "t"), 2):
            app.key(key, screen="controller_keyboard_bindings", binding_capture_index=index)
        app.key("h", screen="controller_options", selection=1, settings_error=1, settings_requests=1, settings_saved=0)
        require(config.is_dir(), "A failed completed-map save must preserve the obstructing destination")
        config.rmdir()
        app.key("Escape", screen="controller_players", selection=0, settings_requests=2, settings_saved=1, settings_error=0)
        configured_keys(config)

        app.key("Escape", screen="settings", selection=2)
        app.key("Return", screen="controller_players", selection=0)
        app.key("Return", screen="controller_options", controller_player=0)
        app.select(2)
        app.key("Right", input_sensitivity=3, settings_saved=2)
        app.select(3)
        app.key("Return", screen="controller_native_options", selection=0)
        app.key("Right", input_deadzone=3, settings_saved=3)
        app.select(2)
        app.key("Return", settings_saved=4)
        values = configured_keys(config)
        require(values["input_device"] == "" and values["input_sensitivity"] == "3" and values["input_deadzone"] == "3" and values["input_rumble"] == "off",
                "Controller options must save keyboard selection, original sensitivity, native deadzone and vibration preference")
        app.select(3)
        app.key("Return", screen="controller_options", selection=3)
        app.select(4)
        app.key("Return", screen="controller_players", selection=0)
        app.key("Down", screen="controller_players", selection=1)
        app.key("Return", screen="controller_options", controller_player=1)
        app.capture("controller-setup-player2.png", 640, 480)
        app.select(1)
        before = config.read_bytes()
        app.key("Return", screen="controller_keyboard_bindings", binding_capture_index=0)
        app.key("i", screen="controller_keyboard_bindings", binding_capture_index=1)
        app.key("Escape", screen="controller_options", selection=1)
        require(config.read_bytes() == before, "Cancelling player two's staged map must preserve both saved profiles")
        app.key("Return", screen="controller_keyboard_bindings", binding_capture_index=0)
        for index, key in enumerate(PLAYER2_KEYS[:-1], 1):
            app.key(key, screen="controller_keyboard_bindings", binding_capture_index=index)
        app.key(PLAYER2_KEYS[-1], screen="controller_options", selection=1, settings_saved=5)
        app.select(2)
        app.key("Left", settings_saved=6)
        app.select(3)
        app.key("Return", screen="controller_native_options", selection=0)
        app.key("Right", settings_saved=7)
        app.select(2)
        app.key("Return", settings_saved=8)
        values = configured_keys(config, player2=True)
        require(values["p2_input_device"] == "" and values["p2_input_sensitivity"] == "1"
                and values["p2_input_deadzone"] == "3" and values["p2_input_rumble"] == "off"
                and values["input_sensitivity"] == "3" and values["input_deadzone"] == "3",
                "Player two options must persist separately while preserving player one's configuration")
        app.key("Escape", screen="controller_options", selection=3)
        app.key("Escape", screen="controller_players", selection=1)
        app.key("Up", screen="controller_players", selection=0)
        app.key("Return", screen="controller_options", controller_player=0, selection=0)
        app.key("Right", input_auto=1, settings_saved=9)
        require(configured_keys(config, player2=True)["input_device"] == "auto",
                "Player one's Auto choice must persist without replacing either binding map")
        app.capture("controller-setup-auto.png", 640, 480)
        app.select(1)
        app.key("Return", screen="controller_auto_configure", selection=0)
        app.key("Down", selection=1)
        app.key("Return", screen="controller_auto_configure", selection=1, settings_saved=9)
        app.key("Up", selection=0)
        app.key("Return", screen="controller_keyboard_bindings", binding_capture_index=0)
        app.key("w", screen="controller_keyboard_bindings", binding_capture_index=1)
        app.key("Escape", screen="controller_auto_configure", selection=0, settings_saved=9)
        app.key("Escape", screen="controller_options", selection=1)
        app.key("Escape", screen="controller_players", selection=0)
        app.key("Escape", screen="settings", selection=2)
        saved = config.read_bytes()
        app.quit()
        return saved
    finally:
        settings.stop(app.process)


def restored_drive(config, saved):
    app = TrialWindow("controller-restored", config, "--state-dir", str(Path("controller-state").resolve()), "--step-input")
    ticks = TraceReader(Path("controller-restored.csv.simulation.csv"))
    try:
        restored = app.enter()
        require(restored["input_sensitivity"] == "3" and restored["input_deadzone"] == "3" and restored["input_auto"] == "1"
                and config.read_bytes() == saved,
                "A fresh native process must restore the complete controller configuration without rewriting it")
        configured_keys(config, player2=True)
        app.key("Escape", screen="main_menu", selection=1)
        app.key("Up", selection=0)
        app.key("Return", screen="race_menu", selection=0)
        app.key("Down", selection=1)
        app.key("Down", selection=2)
        app.key("Return", screen="time_trial_options", selection=2)
        app.key("Return", screen="name_entry")
        app.key("Return", screen="car_selection")
        app.key("Down", manual_transmission=1)
        app.key("Return", screen="track_selection")
        entered = app.key("Return", screen="scene", game_mode=2, music_track=3)
        require(entered["input_auto"] == "1", "Keyboard race confirmation must retain Auto mode for driving")
        require(int(entered["audio_voices"]) >= 5,
                "The ordinary Time Trial must start real engine and driving audio")
        qualifying_rollout(ticks)
        bridge = StepInput(app.process)
        bridge.ready()
        bridge.advance(4)
        bridge.advance(0)
        bridge.resume()
        previous_view = app.current()["camera_view"]
        app.key("t")
        changed = wait_for(lambda: (row := app.current()) and row["camera_view"] != previous_view and row,
                           "Rebound T changes the actual selected driving camera")
        selected_view = changed["camera_view"]
        app.key("c", camera_view=selected_view)
        app.key("z", camera_view=selected_view)
        xdo("keydown", "r")
        app.wait("Rebound R supplies held rear view", camera_view=5, pause_active=0)
        xdo("keyup", "r")
        app.wait("Rear release restores selected camera", camera_view=selected_view)
        plays = int(app.current()["horn_plays"])
        xdo("keydown", "h")
        app.wait("Rebound H starts the real owned horn loop", horn_playing=1, horn_plays=plays + 1, pause_active=0)
        xdo("keyup", "h")
        app.wait("Horn key release stops its owned loop", horn_playing=0)
        before = ticks.rows()[-1]
        xdo("keydown", "w")
        active = wait_for(lambda: (row := ticks.rows()[-1]) and int(row["step"]) > int(before["step"])
                          and int(row["held"]) & 4 and int(row["throttle"]) > 0 and int(row["engine_torque"]) > 0
                          and int(row["car_volume"]) > 0 and row,
                          "Restored W reaches player controls, source throttle, engine torque and actual audio")
        gear = min(6, int(active["gear"]) + 1)
        xdo("key", "e")
        wait_for(lambda: (current := ticks.rows()) and current[-1]["gear"] == str(gear)
                 and any(int(row["step"]) > int(active["step"]) and int(row["pressed"]) & 256 for row in current),
                 "Restored E supplies one gear edge and commits the manual change")
        wait_for(lambda: int(ticks.rows()[-1]["step"]) >= int(active["step"]) + 15, "Short continuously simulated rebound drive")
        moved = ticks.rows()[-1]
        require(any(abs(int(moved[field]) - int(before[field])) > 8192 for field in ("body_x", "body_y", "body_z")),
                "The short rebound drive must translate the actual simulated body")
        require(all(row["motion_boundary"] == "0" for row in ticks.rows()), "Rebound drive must remain within supported track contacts")

        xdo("keydown", "h")
        app.wait("Held horn before focus suspension", horn_playing=1, pause_active=0)
        xdo("windowminimize", app.window)
        unfocused = app.wait("Focus loss suspends rebound input and owned audio", focused=0, scene_active=0, audio_voices=0, horn_playing=0)
        wait_for(lambda: int(app.current()["poll"]) >= int(unfocused["poll"]) + 3, "Unfocused host continues polling")
        require(app.current()["simulation_steps"] == unfocused["simulation_steps"], "Unfocused time must not advance simulation")
        xdo("keyup", "w")
        xdo("keyup", "h")
        xdo("windowmap", app.window)
        xdo("windowactivate", "--sync", app.window)
        app.wait("Focused Time Trial resumes without replaying the horn", focused=1, scene_active=1, horn_playing=0)
        wait_for(lambda: (row := ticks.rows()[-1]) and int(row["step"]) > int(moved["step"]) and row["held"] == "0" and row["throttle"] == "0",
                 "Focus regain must not resurrect the held rebound pedal")
        app.key("Escape", pause_active=1, pause_page=0, pause_selection=0, scene_active=0)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=3, pause_selection=1)
        app.key("Up", pause_selection=0)
        app.key("Return", screen="time_trial_records", selection=0, scene_active=0, music_track=2)
        app.key("Return", screen="time_trial_records", selection=1)
        app.key("Return", screen="main_menu", selection=0)
        app.key("Down", selection=1)
        app.quit()
        require(config.read_bytes() == saved, "Driving, focus changes and fixed-menu keys must preserve saved bindings")
    finally:
        if app.process.poll() is None:
            xdo("keyup", "w", "r", "h")
        settings.stop(app.process)


if __name__ == "__main__":
    settings.executable, settings.data_root = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Controller acceptance requires private muted audio")
    # Xvfb isolates keys, while physical SDL pads are shared with the desktop.
    # This flow proves Auto's keyboard fallback; virtual-pad tests cover merging.
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required for private controller acceptance")
    with Path("controller-setup-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "Private controller window manager")
        config = Path("controller.cfg")
        restored_drive(config, configure(config))
        print("PASS: Controller Setup original player-selection/controls routes, independent two-profile cancel/complete capture, "
              "Auto selection/configuration and keyboard fallback, native SDL settings, reserved/duplicate keys, save retry, process restart, "
              "nine persisted bindings, actual W/E Time Trial controls/audio, rebound cameras/horn, vibration preference, focus clear and fixed menu/pause keys")
    finally:
        settings.stop(manager)
