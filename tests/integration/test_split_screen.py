"""Two local drivers, saved orientations, independent controls and shared lifecycle."""

import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import test_settings as settings
from test_pause import PauseWindow
from native_support import capture_window, TraceReader, leave_scene, read_png, require, wait_for, window_manager_ready, xdo


class SplitWindow(PauseWindow):
    def key(self, key, **values):
        if self.current()["screen"] != "scene":
            # A poll can precede key consumption; frontend cues acknowledge the action.
            return settings.SettingsWindow.key(self, key, **values)
        return super().key(key, **values)


def pane_rectangles(width, height, vertical):
    return ((0, 0, width // 2, height), (width // 2, 0, width - width // 2, height)) if vertical else (
        (0, 0, width, height // 2), (0, height // 2, width, height - height // 2))


def check_start_lamps(path, vertical, phase):
    width, height, pixels = read_png(path)
    for x, y, w, h in pane_rectangles(width, height, vertical):
        scale = min(w / 640, h / 480)
        left = x + (w - 640 * scale) / 2
        for lamp in range(3):
            colored = 0
            for py in range(math.ceil(y + (52 if phase == 2 else 113) * scale), math.floor(y + (116 if phase == 2 else 177) * scale)):
                for px in range(math.ceil(left + (240 + lamp * 49) * scale), math.floor(left + (289 + lamp * 49) * scale)):
                    red, green, blue = pixels[py * width + px]
                    colored += (red > 100 and red > green + 40 and red > blue + 40) if phase == 2 else (
                        green > 100 and green > red + 40 and green > blue + 40)
            require(colored > 20 * scale * scale, "Each local pane must show all three correctly colored Mega start lamps")


def check_hud_anchors(width, height, pixels, vertical):
    for player, (x, y, w, h) in enumerate(pane_rectangles(width, height, vertical), 1):
        scale = min(w / 640, h / 480)
        center = (w - 640 * scale) / 2
        bottom = h - 480 * scale
        # Source HUD colors at the pane's left, center and bottom-right anchors.
        regions = (
            ("lap", x + 11 * scale, y + 5 * scale, 121 * scale, 35 * scale,
             lambda r, g, b: b > 100 and b > r + 40 and b > g + 10),
            ("time", x + center + 286 * scale, y + 11 * scale, 144 * scale, 15 * scale,
             lambda r, g, b: r > 100 and r > g + 40 and r > b + 40),
            ("gear", x + 2 * center + 566 * scale, y + bottom + 424 * scale, 54 * scale, 38 * scale,
             lambda r, g, b: r > 100 and g > 100 and r > b + 40 and g > b + 40),
        )
        for name, left, top, rw, rh, matches in regions:
            colored = sum(matches(*pixels[py * width + px])
                          for py in range(math.ceil(top), math.floor(top + rh))
                          for px in range(math.ceil(left), math.floor(left + rw)))
            require(colored > 20 * scale * scale,
                    f"Player{player} Mega {name} must remain visible in its own {w}x{h} pane")


def run(vertical, mega=False):
    name = ("split-mega-" if mega else "split-") + ("vertical" if vertical else "horizontal")
    # Exercise shared same-car resources and distinct car resources in both presets.
    second_car = 0 if vertical else 1
    app = SplitWindow(name, name + ".cfg", "--resolution", "960x600", "--vsync", "off", "--fps-limit", "30")
    ticks = TraceReader(Path(name + ".csv.simulation.csv"))
    try:
        app.enter()
        app.open_options(1, "graphics_options")
        if mega:
            app.select(5)
            app.key("Right", draft_graphics_detail=4)
        app.select(6)
        if vertical:
            app.key("Right", draft_split_vertical=1)
        app.apply(split_vertical=int(vertical))
        require(settings.preferences(app.config)["split_vertical"] == ("on" if vertical else "off"), "Orientation must persist through the actual settings menu")
        if mega:
            require(settings.preferences(app.config)["graphics_detail"] == "4", "The Mega preset must persist alongside the split orientation")
        app.key("Escape", screen="settings")
        app.key("Escape", screen="main_menu")
        app.key("Up", selection=0)
        app.key("Return", screen="race_menu")
        app.key("Return", screen="single_options", selection=4)
        app.key("Down", selection=0)
        app.key("Right", players=2, qualifying=0)
        app.key("Down", selection=1)
        app.key("Down", selection=2)
        if not vertical:
            app.key("Left", car_boost=0, qualifying=0)
        else:
            app.wait("original local Car Boost option", car_boost=1, qualifying=0)
            capture_window(app.window, Path(f"{name}-car-boost-menu.png"))
        app.key("Down", selection=3)
        app.key("Down", selection=4)
        app.key("Return", screen="difficulty")
        app.key("Return", screen="name_entry", selection_player=0)
        if vertical:
            xdo("type", "--delay", "15", "CMFOLLOW")
            app.wait("source trackside code", player_name="CMFOLLOW")
            app.key("Return", screen="name_entry", player_name="")
            xdo("type", "--delay", "15", "ALPHA")
            app.wait("first same-car driver's own name", player_name="ALPHA")
        app.key("Return", screen="car_selection", selection_player=0)
        app.key("Return", screen="name_entry", selection_player=1)
        if vertical:
            xdo("type", "--delay", "15", "BRAVO")
            app.wait("second same-car driver's own name", p2_name="BRAVO", player_name="ALPHA")
        app.key("Return", screen="car_selection", selection_player=1)
        if second_car:
            app.key("Right", p2_selected_car=second_car)
        app.key("Return", screen="track_selection")
        app.key("Right", track_id=1)
        app.key("Return", screen="scene", local_players=2, p2_car=second_car, rival_count=0)
        if vertical:
            app.wait("separate names survive the shared-model scene entry", player_name="ALPHA", p2_name="BRAVO")
        if mega:
            require(app.current()["graphics_detail"] == app.current()["scene_detail"] == "4", "Both local views must load the chosen Mega scene")
            for phase in (2, 3):
                app.wait("shared Mega start lamps", start_phase=phase)
                frame = int(app.current()["frame"])
                wait_for(lambda: int(app.current()["frame"]) >= frame + 2, "Mega start lamps presented")
                path = Path(f"{name}-phase{phase}.png")
                capture_window(app.window, path)
                check_start_lamps(path, vertical, phase)
        require(int(app.current()["audio_voices"]) >= 10, "Both cars must start their own engine layers")
        initial = app.current()
        require((initial["body_x"], initial["body_z"]) != (initial["p2_x"], initial["p2_z"]), "Local drivers need distinct grid positions")
        wait_for(lambda: app.current()["start_held"] == "0", "shared green light", timeout=15)

        xdo("keydown", "Up")
        wait_for(lambda: ticks.rows() and int(ticks.rows()[-1]["throttle"]) > 0 and ticks.rows()[-1]["p2_throttle"] == "0", "P1 pedal must not control P2")
        wait_for(lambda: int(app.current()["speed"]) > 65536, "first local car moves")
        xdo("keyup", "Up")
        wait_for(lambda: ticks.rows()[-1]["throttle"] == "0", "P1 releases")
        xdo("keydown", "w")
        wait_for(lambda: int(ticks.rows()[-1]["p2_throttle"]) > 0 and ticks.rows()[-1]["throttle"] == "0", "P2 pedal must not control P1")
        wait_for(lambda: int(app.current()["p2_speed"]) > 65536, "second local car moves")
        xdo("keyup", "w")
        records = ticks.rows()
        for prefix in ("", "p2_"):
            require(all(row[prefix + "maximum_throttle"] == row[prefix + "base_throttle"]
                        for row in records if row["start_held"] == "1"),
                    "Car Boost must wait for the shared grid release")
            require(all(int(row[prefix + "base_throttle"]) <= int(row[prefix + "maximum_throttle"])
                        <= int(row[prefix + "base_throttle"]) + 0x1999 for row in records),
                    "Each local car must retain its own source throttle baseline and bounded boost")
        if vertical:
            require(any(int(row["p2_maximum_throttle"]) > int(row["p2_base_throttle"])
                        and int(row["p2_tire_offset"]) > 0 for row in records if row["start_held"] == "0"),
                    "Local Car Boost must reach the trailing driver's real throttle and tire physics")
        else:
            require(all(row[prefix + "maximum_throttle"] == row[prefix + "base_throttle"]
                        for row in records for prefix in ("", "p2_")),
                    "Disabling local Car Boost must preserve both cars' original throttle limits")
        if mega:
            records = ticks.rows()
            for row in records:
                require(row["p2_hud_gear"] == row["p2_gear"] and row["p2_hud_lap"] == row["p2_lap"]
                        and row["p2_hud_lap_time_ms"] == row["p2_lap_ms"] and row["p2_hud_best_lap_ms"] == row["p2_best_ms"],
                        "Player2 Mega HUD must retain its own cached gear, lap and time records")
                require(row["hud_lap_time_ms"] == row["lap_time_ms"] and row["hud_best_lap_ms"] == row["best_lap_ms"],
                        "Player1 Mega HUD must retain its own cached time records")
            require(any(row["p2_hud_gear"] != row["gear"] for row in records), "The local HUDs must show independent driver values")
            transitions = {}
            for row in records:
                transitions.setdefault(int(row["start_phase"]), int(row["simulation_ns"]) // 1_000_000)
            require([transitions[i] for i in (1, 2, 3, 4)] == [3080, 6160, 9240, 10840], "Mega split rendering changed original start deadlines")
        old = app.current()["camera_view"]
        p2_old = app.current()["p2_camera"]
        xdo("key", "Tab")
        wait_for(lambda: app.current()["p2_camera"] != p2_old, "second camera changes independently")
        require(app.current()["camera_view"] == old, "P2 camera changed P1")
        xdo("key", "c")
        wait_for(lambda: app.current()["camera_view"] != old, "first camera changes")
        xdo("key", "F6")
        app.wait("P2 trackside camera obeys CMFOLLOW", p2_trackside=int(vertical), camera_trackside=0)
        xdo("key", "F6")
        app.wait("P2 trackside camera released", p2_trackside=0, camera_trackside=0)
        if not mega:
            app.key("Escape", pause_active=1, scene_active=0)
            frozen = app.current()
        for width, height in ((640, 480), (960, 540), (961, 601)):
            xdo("windowsize", "--sync", app.window, width, height)
            app.wait("split resize", width=width, height=height)
            frame = int(app.current()["frame"])
            wait_for(lambda: int(app.current()["frame"]) > frame + 2, "both resized views presented")
            row = app.current()
            expected = (width // 2, height, width - width // 2, height) if vertical else (width, height // 2, width, height - height // 2)
            require(tuple(int(row[k]) for k in ("view1_width", "view1_height", "view2_width", "view2_height")) == expected, "Both view extents must follow saved orientation and odd pixel remainders")
            target = Path(f"{name}-{width}x{height}.png")
            capture_window(app.window, target)
            w, h, pixels = read_png(target)
            require((w, h) == (width, height), "Capture must contain only the game window")
            if mega:
                require(row["pause_active"] == "0", "Mega HUD captures must remain unobstructed by the pause menu")
                check_hud_anchors(w, h, pixels, vertical)
            for player in (0, 1):
                pane = [pixels[y * width + x] for y in range(height) for x in range(width)
                        if ((x >= width // 2) if vertical else (y >= height // 2)) == bool(player)]
                require(len(set(pane)) > 100, "Each split pane must render a textured view")
        if mega:
            app.key("Escape", pause_active=1, scene_active=0)
            frozen = app.current()
        require(frozen["audio_voices"] == "0", "Pause must stop both audio banks")
        require(all(app.current()[field] == frozen[field] for field in ("simulation_steps", "body_x", "body_z", "p2_x", "p2_z", "lap_time_ms", "p2_lap_ms",
                    "maximum_throttle", "tire_offset", "p2_maximum_throttle", "p2_tire_offset")), "Pause or resizing advanced a local driver or Car Boost")
        xdo("windowminimize", app.window)
        app.wait("unfocused split pause", focused=0, scene_active=0, audio_voices=0)
        xdo("windowmap", app.window)
        xdo("windowactivate", "--sync", app.window)
        app.wait("focus preserves shared pause", focused=1, pause_active=1, scene_active=0, audio_voices=0)
        require(app.current()["simulation_steps"] == frozen["simulation_steps"], "Focus resumed the paused split simulation")
        xdo("keydown", "w")
        app.key("Escape", pause_active=0, scene_active=1)
        time.sleep(.15)
        xdo("keyup", "w")
        wait_for(lambda: ticks.rows()[-1]["p2_throttle"] == "0", "Pause-held P2 input must clear on resume")
        app.key("Escape", pause_active=1)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=2)
        app.key("Return", screen="scene", scene_active=1, pause_active=0, start_held=1, completed_laps=0, p2_completed_laps=0)
        require(app.current()["local_players"] == "2" and app.current()["p2_car"] == str(second_car), "Race Again must restore both selected drivers")
        if vertical:
            require(app.current()["player_name"] == "ALPHA" and app.current()["p2_name"] == "BRAVO",
                    "Race Again must retain separate personal names for both same-car drivers")
        restarted = app.current()
        require(restarted["car_boost"] == str(int(vertical)) and all(
            restarted[prefix + "maximum_throttle"] == restarted[prefix + "base_throttle"] and restarted[prefix + "tire_offset"] == "0"
            for prefix in ("", "p2_")), "Race Again must retain the Car Boost option and reset both drivers' accumulated boost")
        leave_scene(app.current)
        app.wait("return", screen="main_menu", music_track=2)
        app.key("Up", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Split game must exit cleanly")
        print(f"PASS: {name}, two cars/controls/cameras/audio, Car Boost {'on' if vertical else 'off'}, three aspects, pause/restart/return")
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Split acceptance requires isolated muted audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    require(len(sys.argv) in (3, 4) and (len(sys.argv) == 3 or sys.argv[3] == "--mega"), "Expected executable, canonical data and optional --mega")
    settings.executable, settings.data_root = sys.argv[1:3]
    mega = len(sys.argv) == 4
    with Path("split-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        run(False, mega)
        run(True, mega)
    finally:
        settings.stop(manager)
