"""Categorized Extras, saved switches and playable effects on a muted display."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import test_settings as settings
from native_support import capture_window, leave_scene, read_png, require, wait_for, window_manager_ready, xdo


class ExtrasWindow(settings.SettingsWindow):
    categories = {"unlocks": 0, "camera": 1, "effects": 2, "racing": 3}

    def __init__(self, name, state="profile"):
        super().__init__(name, f"{name}.cfg", "--state-dir", str(state), "--resolution", "640x480",
                         "--vsync", "off", "--fps-limit", "60")

    def key(self, key, **values):
        if self.current().get("screen") == "scene":
            if key == "Escape" and values.get("screen") == "main_menu":
                return leave_scene(self.current)
            xdo("key", key)
            return self.wait("scene action", **values)
        return super().key(key, **values)

    def enter(self):
        self.wait("startup", screen="title")
        self.window = xdo("search", "--onlyvisible", "--pid", self.process.pid,
                          "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", self.window)
        self.wait("focused", focused=1)
        xdo("key", "Return")
        self.wait("language", screen="language")
        self.key("Up", selection=2)
        return self.key("Return", screen="main_menu", selection=0, language=2)

    def select(self, target):
        while int(self.current()["selection"]) != target:
            self.key("Down")

    def extras(self):
        if self.current()["screen"].startswith("extras_"):
            self.key("Escape", screen="extras")
        elif self.current()["screen"] == "main_menu":
            self.select(3)
            self.key("Return", screen="extras", selection=0)
        require(self.current()["screen"] == "extras", "expected Extras screen")

    def category(self, name):
        if self.current()["screen"] == "extras_" + name:
            return
        self.extras()
        self.select(self.categories[name])
        self.key("Return", screen="extras_" + name, selection=0)

    def option(self, category, row, enabled, **values):
        self.category(category)
        self.select(row)
        before = self.current()
        result = self.key("Right" if enabled else "Left", screen="extras_" + category,
                          cheat_announcements=before["cheat_announcements"], **values)
        require(result["music_track"] == "2" and result["music_playing"] == "1",
                "Extras switches must preserve menu music and ordinary option sounds")
        return result

    def main(self):
        while self.current()["screen"] != "main_menu":
            self.key("Escape")
        self.select(0)

    def car(self):
        self.main()
        self.key("Return", screen="race_menu")
        self.key("Return", screen="single_options")
        self.select(2)
        if self.current()["qualifying"] == "1":
            self.key("Right", qualifying=0)
        self.select(4)
        self.key("Return", screen="difficulty")
        self.key("Return", screen="name_entry")
        self.key("Return", screen="car_selection")

    def capture(self, path, width=640, height=480):
        xdo("windowsize", "--sync", self.window, width, height)
        self.wait("capture extent", width=width, height=height, presented=1)
        frame = subprocess.run(["xprop", "-id", self.window, "_NET_FRAME_EXTENTS"],
                               check=True, capture_output=True, text=True).stdout
        edges = re.search(r"= (\d+), (\d+), (\d+), (\d+)", frame)
        require(edges, "missing private window extents")
        xdo("windowmove", "--sync", "--", self.window, -int(edges[1]), -int(edges[3]))
        capture_window(self.window, path)
        actual_width, actual_height, pixels = read_png(Path(path))
        require((actual_width, actual_height) == (width, height) and len(set(pixels)) > 50, "empty or clipped Extras capture")

    def quit(self):
        self.main()
        self.select(4)
        xdo("key", "Return")
        require(self.process.wait(timeout=5) == 0, "Extras left normal Quit unusable")


def flow():
    app = ExtrasWindow("extras")
    try:
        app.enter()
        app.car()
        app.key("Return", screen="track_selection", track_id=0, track_available=1)
        app.key("Right", track_id=1, track_available=1)
        app.key("Right", track_id=2, track_available=0)
        app.key("Return", screen="track_selection", track_available=0, music_track=2)
        app.key("Escape", screen="car_selection")
        app.key("Escape", screen="name_entry")
        xdo("type", "--delay", "15", "jhammo")
        app.wait("typed original code", player_name="JHAMMO")
        before = int(app.current()["cheat_announcements"])
        app.key("Return", screen="name_entry", player_name="", extras_all_tracks=1,
                cheat_announcements=before + 1, cheat_sample=7, extras_error=0)
        xdo("type", "--delay", "15", "jhammo")
        app.wait("repeat original code", player_name="JHAMMO")
        app.key("Return", screen="name_entry", player_name="", extras_all_tracks=0, cheat_announcements=before + 1)
        xdo("type", "DRIVER")
        app.wait("normal name after codes", player_name="DRIVER")
        app.main()
        app.select(3)
        app.capture("main-extras-icon.png")
        # Reuse the German frontend route to inspect the longest Apply label.
        app.select(1)
        app.key("Return", screen="settings", selection=0)
        app.select(1)
        app.key("Return", screen="graphics_options", selection=0)
        app.select(4)
        app.capture("graphics-apply-de-640x480.png")
        app.key("Escape", screen="settings", selection=1)
        app.key("Escape", screen="main_menu", selection=1)
        app.extras()
        app.capture("extras-640x480.png")
        app.category("effects")
        for width, height in ((640, 480), (1280, 720), (1280, 800)):
            app.capture(f"effects-{width}x{height}.png", width, height)
        app.capture("effects-restored.png")
        app.option("unlocks", 0, True, extras_all_tracks=1, extras_bonus_track=1, extras_error=0)
        app.option("unlocks", 1, True, extras_bonus_cars=1, earned_car_8=0, earned_car_9=0)
        app.capture("unlocks-640x480.png")
        app.option("camera", 0, True)  # Original overhead camera.
        app.capture("camera-640x480.png")
        app.select(1)
        app.capture("camera-cockpit.png")
        app.option("effects", 0, True)  # Original special sky.
        app.option("effects", 2, True)  # Vertical projection and steering flip.
        for category, row, bit in (("effects", 3, 5), ("racing", 2, 8), ("racing", 3, 11), ("racing", 4, 19)):
            app.option(category, row, True, code_flags=int(app.current()["code_flags"]) | (1 << bit))
        app.option("racing", 0, True, code_delta=131072)
        app.capture("racing-640x480.png")
        app.wait("boost reaches live step", code_delta=131072)
        expected = app.current()["code_flags"]
        app.quit()
    finally:
        settings.stop(app.process)

    app = ExtrasWindow("extras-restored")
    try:
        app.enter()
        app.wait("persisted codes", code_flags=expected, extras_all_tracks=1, extras_bonus_cars=1, extras_bonus_track=1)
        app.car()
        for car in range(1, 9):
            app.key("Right", car_id=car, car_available=1)
        app.capture("bonus-car8.png")
        app.key("Return", screen="track_selection")
        while app.current()["track_id"] != "0":
            app.key("Left")
        app.key("Return", screen="scene", stored_car_id=8, horizon_index=5, resolved_weather=1)
        wait_for(lambda: int(app.current()["simulation_steps"]) > 2, "bonus car8 scene updates")
        app.capture("bonus-car8-scene.png")
        require(int(app.current()["scene_vertices"]) > 1000 and int(app.current()["audio_voices"]) >= 3,
                "bonus car8 must render with original engine audio")
        xdo("keydown", "Up")
        try:
            wait_for(lambda: int(app.current()["speed"]) > 0, "bonus car8 accelerates from original grid start", timeout=12)
        finally:
            xdo("keyup", "Up")
        app.key("Escape", screen="main_menu")
        app.option("camera", 0, False)
        app.option("effects", 0, False)
        app.option("effects", 2, False)
        app.option("racing", 0, False, code_delta=65536)
        app.option("effects", 1, True)
        app.car()
        app.key("Right", car_id=9, car_available=1)
        app.capture("bonus-car9.png")
        app.key("Return", screen="track_selection")
        app.key("Return", screen="scene", stored_car_id=9, horizon_index=6)
        app.capture("bonus-car9-toon.png")
        require(int(app.current()["scene_vertices"]) > 1000 and int(app.current()["audio_voices"]) >= 3,
                "bonus car9 must render with original engine audio")
        app.key("Escape", screen="main_menu")
        app.car()
        app.key("Return", screen="track_selection")
        for track in range(1, 10):
            app.key("Right", track_id=track, track_available=1)
        app.key("Return", screen="scene", stored_track_id=9, horizon_index=0, resolved_weather=1)
        app.capture("lavaland-unlocked.png")
        app.key("Escape", screen="main_menu")
        app.option("effects", 1, False)
        app.option("unlocks", 0, False, extras_all_tracks=0, extras_bonus_track=0)
        for category, row, bit in (("effects", 3, 5), ("racing", 2, 8), ("racing", 3, 11), ("racing", 4, 19)):
            app.option(category, row, False, code_flags=int(app.current()["code_flags"]) & ~(1 << bit))
        app.option("unlocks", 1, False, code_flags=0, extras_bonus_cars=0)
        app.quit()
    finally:
        settings.stop(app.process)


def showdown():
    app = ExtrasWindow("extras-showdown", "showdown-profile")
    try:
        app.enter()
        for category, row, bit in (("effects", 3, 5), ("racing", 0, 17), ("racing", 2, 8), ("racing", 4, 19), ("unlocks", 2, 21)):
            app.option(category, row, True, code_flags=int(app.current()["code_flags"]) | (1 << bit))
        saved_flags = app.current()["code_flags"]
        app.main()
        app.key("Return", screen="race_menu")
        app.select(4)
        app.key("Return", screen="championship_menu")
        app.key("Return", screen="championship_options", selection=1)
        app.key("Return", screen="name_entry")
        # Championship accepts its original allowed codes only.
        xdo("type", "XBOOSTME")
        app.wait("restricted code text", player_name="XBOOSTME")
        before = app.current()["cheat_announcements"]
        app.key("Return", screen="car_selection", player_name="XBOOSTME", cheat_announcements=before)
        app.key("Return", screen="scene", game_mode=1, scene_code_flags=1 << 19, code_delta=65536,
                code_flags=saved_flags, championship_phase=0)
        app.capture("showdown-from-extras.png")
        app.key("Escape", pause_active=1)
        app.key("Up", pause_selection=3)
        app.key("Return", pause_page=4, pause_selection=1)
        app.key("Up", pause_selection=0)
        app.key("Return", screen="main_menu")
        app.option("unlocks", 2, False)
        require("earned_special_championship=off" in Path("showdown-profile/extras.cfg").read_text(),
                "Reversible Showdown access must not manufacture earned progress")
        app.main()
        app.key("Return", screen="race_menu", selection=1)
        app.select(0)
        app.key("Up", selection=3)  # Four entries again: Showdown is hidden.
        app.quit()
    finally:
        settings.stop(app.process)


def save_failure():
    state = Path("unwritable-profile")
    app = ExtrasWindow("extras-failure", state)
    try:
        app.enter()
        state.write_text("block profile directory")
        app.option("unlocks", 0, True, extras_all_tracks=1, extras_error=1)
        require(not state.is_dir(), "failed save unexpectedly replaced blocking file")
        state.unlink()
        app.key("Escape", screen="extras", extras_saved=1, extras_error=0)
        require((state / "extras.cfg").is_file(), "leaving Extras did not retry the failed save")
        app.quit()
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    settings.executable, settings.data_root = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Extras checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required for private Extras acceptance")
    with Path("extras-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private Extras window manager")
        flow()
        showdown()
        save_failure()
        print("PASS: complete Extras, original typed codes, saved effects, bonus scenes, playable Showdown, mode gates and save retry")
    finally:
        settings.stop(manager)
