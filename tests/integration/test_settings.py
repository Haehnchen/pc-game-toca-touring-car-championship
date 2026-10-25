"""Use the startup settings menu through its private, muted native window."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from native_support import capture_window, TraceReader, read_png, require, wait_for, window_manager_ready, xdo


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


def preferences(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if line and not line.startswith("#"))


class SettingsWindow:
    def __init__(self, name, config, *arguments):
        self.name = name
        self.config = Path(config)
        self.reader = TraceReader(Path(f"{name}.csv"))
        stepping = "--step-input" in arguments
        with Path(f"{name}.log").open("w") as log:
            self.process = subprocess.Popen(
                [executable, "--fullscreen", "off", "--data-root", data_root, "--config", str(self.config),
                 "--audio-offline", "--trace", f"{name}.csv", *arguments],
                stdin=subprocess.PIPE if stepping else None,
                stdout=subprocess.PIPE if stepping else log,
                stderr=log if stepping else subprocess.STDOUT)
        self.window = None

    def rows(self):
        require(self.process.poll() is None, f"settings exited early; see {self.name}.log")
        return self.reader.rows()

    def current(self):
        rows = self.rows()
        return rows[-1] if rows else {}

    def wait(self, description, **values):
        return wait_for(lambda: (row := self.current()) and all(row.get(key) == str(value) for key, value in values.items()) and row,
                        description, timeout=8)

    def key(self, key, **values):
        before = int(self.current()["frontend_cues"])
        xdo("key", key)
        return wait_for(lambda: (row := self.current()) and int(row["frontend_cues"]) > before
                        and all(row.get(field) == str(value) for field, value in values.items()) and row,
                        f"{key}: {values}")

    def enter(self):
        self.wait("native startup", screen="title")
        self.window = xdo("search", "--onlyvisible", "--pid", self.process.pid,
                          "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", self.window)
        self.wait("focused startup", focused=1)
        xdo("key", "Return")
        self.wait("title dismissal reaches language", screen="language")
        self.key("Return", screen="main_menu", selection=0)
        self.key("Down", screen="main_menu", selection=1)
        return self.key("Return", screen="settings", selection=0)

    def select(self, target):
        screen = self.current()["screen"]
        count = {"settings": 4, "graphics_options": 8, "sound_options": 5, "controller_players": 3,
                 "controller_options": 5, "controller_native_options": 4, "controller_auto_configure": 3}[screen]
        for _ in range(count + 1):
            if int(self.current()["selection"]) == target:
                return
            self.key("Down", screen=screen)
        raise AssertionError(f"cannot reach {screen} row {target}")

    def open_options(self, index, screen):
        self.select(index)
        return self.key("Return", screen=screen, selection=0)

    def resolution(self, width, height):
        self.select(0)
        for _ in range(40):
            row = self.current()
            if (row["draft_width"], row["draft_height"]) == (str(width), str(height)):
                return
            self.key("Right", screen="graphics_options", selection=0)
        raise AssertionError(f"resolution {width}x{height} missing from settings choices")

    def apply(self, **values):
        self.select(4)
        previous = self.current()
        requests, saved = int(previous["settings_requests"]), int(previous["settings_saved"])
        xdo("key", "Return")
        return self.wait("settings apply and save", screen="graphics_options", selection=4,
                         settings_requests=requests + 1, settings_saved=saved + 1, settings_error=0, **values)

    def change_detail(self, key, detail, **values):
        self.select(5)
        previous = self.current()
        return self.key(key, screen="graphics_options", selection=5, graphics_detail=detail, draft_graphics_detail=detail,
                        settings_requests=int(previous["settings_requests"]) + 1,
                        settings_saved=int(previous["settings_saved"]) + 1, settings_error=0, **values)

    def capture(self, name, width, height):
        self.wait("presented settings capture", presented=1, width=width, height=height)
        wait_for(lambda: (row := self.current()) and int(row["menu_animation_ns"]) - int(row["menu_entered_ns"]) >= 466_666_667,
                 "menu turn settles before static artwork comparison")
        # Put the client content at 0,0. Openbox positions its surrounding
        # frame; the border otherwise clips a 1280-wide capture by one pixel.
        frame = subprocess.run(["xprop", "-id", self.window, "_NET_FRAME_EXTENTS"],
                               check=True, capture_output=True, text=True, timeout=3).stdout
        edges = re.search(r"= (\d+), (\d+), (\d+), (\d+)", frame)
        require(edges, "private window manager did not report frame extents")
        xdo("windowmove", "--sync", "--", self.window, -int(edges[1]), -int(edges[3]))
        target = Path(name)
        capture_window(self.window, target)
        actual_width, actual_height, pixels = read_png(target)
        require((actual_width, actual_height) == (width, height),
                f"settings capture {actual_width}x{actual_height} differs from {width}x{height}; "
                + xdo("getwindowgeometry", "--shell", self.window))
        require(len(set(pixels)) > 50, "settings menu capture is empty")
        return pixels

    def quit(self):
        if self.current()["screen"] == "controller_auto_configure":
            self.key("Escape", screen="controller_options")
        if self.current()["screen"] == "controller_native_options":
            self.key("Escape", screen="controller_options")
        if self.current()["screen"] == "controller_options":
            self.key("Escape", screen="controller_players")
        if self.current()["screen"] == "controller_players":
            self.key("Escape", screen="settings")
        if self.current()["screen"] in ("graphics_options", "sound_options"):
            self.key("Escape", screen="settings")
        if self.current()["screen"] == "settings":
            self.key("Escape", screen="main_menu", selection=1)
        self.key("Down", screen="main_menu", selection=2)
        self.key("Down", screen="main_menu", selection=3)
        self.key("Down", screen="main_menu", selection=4)
        xdo("key", "Return")
        require(self.process.wait(timeout=5) == 0, "settings route did not leave normal Quit usable")
        final = self.reader.rows()[-1]
        require(final["audio_active"] == "0" and final["audio_voices"] == "0", "settings shutdown leaked native audio")


def settings_flow():
    config = Path("settings.cfg")
    app = SettingsWindow("settings", config, "--resolution", "640x480", "--vsync", "off",
                         "--fps-limit", "60", "--volume", "0.8", "--mute", "off", "--graphics-detail", "3")
    try:
        initial = app.enter()
        require(initial["graphics_detail"] == "3" and initial["effects_volume"] == "100" and initial["music_volume"] == "100"
                and initial["game_music"] == "1" and initial["stereo_effects"] == "1",
                "the original artwork fixture must retain Very High detail, full sound volumes, music and stereo")
        require(initial["audio_active"] == "1" and initial["music_track"] == "2" and initial["music_playing"] == "1",
                "settings must retain real frontend audio and CD02")
        app.capture("options-hub-640x480.png", 640, 480)
        app.open_options(1, "graphics_options")
        app.select(6)
        pending = app.current()
        for key, vertical in (("Left", 1), ("Right", 0), ("Return", 1)):
            app.key(key, screen="graphics_options", selection=6, graphics_detail=3, draft_graphics_detail=3,
                    split_vertical=0, draft_split_vertical=vertical,
                    settings_requests=pending["settings_requests"], settings_saved=pending["settings_saved"])
        require(not config.exists(), "split orientation must remain staged until Apply")
        app.capture("split-screen-640x480.png", 640, 480)
        app.select(0)
        baseline = app.capture("settings-640x480.png", 640, 480)
        app.resolution(800, 600)
        app.select(2)
        if app.current()["draft_vsync"] == "0":
            app.key("Right", draft_vsync=1)
        app.select(3)
        app.key("Right", draft_fps_limit=90)
        applied = app.apply(width=800, height=600, fullscreen=0, fps_limit=90, vsync=1, mute=0, graphics_detail=3, split_vertical=1)
        require(applied["music_starts"] == initial["music_starts"] and applied["music_playing"] == "1",
                "applying settings must retain the current frontend music instance")

        for width, height in ((1280, 720), (1280, 800)):
            app.resolution(width, height)
            app.apply(width=width, height=height)
            app.select(5)
            app.capture(f"graphics-second-page-{width}x{height}.png", width, height)
            app.select(0)
            pixels = app.capture(f"settings-{width}x{height}.png", width, height)
            scale, left = height / 480, (width - 640 * height / 480) / 2
            # Title, unchanged Fullscreen row and the surrounding frame
            # retain their original positions and proportions. Exclude edited
            # values, the selected row, transient notice and contextual help.
            samples = [(sx, sy) for sy in (25, 34, 42, 54, 165, 174, 183)
                       for sx in range(194, 590, 7)]
            samples.extend((sx, sy) for sy in range(144, 200, 5) for sx in range(73, 161, 5))
            samples.extend((sx, sy) for sx in (25, 32, 39, 602, 609, 616) for sy in range(75, 400, 9))
            for sx, sy in samples:
                x, y = int(left + (sx + 0.5) * scale), int((sy + 0.5) * scale)
                require(pixels[y * width + x] == baseline[sy * 640 + sx],
                        f"settings artwork lost uniform centered scaling at {width}x{height}: {sx},{sy}")
            for begin, end in ((0, int(left)), (width - int(left), width)):
                colors = {pixels[y * width + x] for y in range(0, height, 3) for x in range(begin, end, 3)}
                require(len(colors) > 50, f"settings side band lacks background detail at {width}x{height}")
                outer = range(begin, min(begin + 4, end)) if begin == 0 else range(max(begin, end - 4), end)
                require(len({pixels[y * width + x] for y in range(0, height, 3) for x in outer}) > 12,
                        f"settings background does not reach the outer edge at {width}x{height}")
            require(app.current()["music_starts"] == initial["music_starts"] and app.current()["music_playing"] == "1",
                    "widescreen settings must preserve continuous CD02")

        # Keep the original artwork for exact pixel scaling above. Mega uses
        # higher-resolution font/icon overlays, covered by native-menu-icons.
        app.change_detail("Right", 4)
        require(preferences(config)["graphics_detail"] == "4", "Mega must save immediately without Apply")
        app.select(1)
        app.key("Right", draft_fullscreen=1)
        app.apply(fullscreen=1)
        app.select(1)
        app.key("Left", draft_fullscreen=0)
        app.apply(fullscreen=0)
        app.resolution(1280, 800)
        restored = app.apply(width=1280, height=800, fullscreen=0)
        saved = config.read_bytes()
        values = preferences(config)
        require(values["width"] == "1280" and values["height"] == "800" and values["fullscreen"] == "off"
                and values["vsync"] == ("on" if restored["vsync"] == "1" else "off")
                and values["fps_limit"] == "90" and values["mute"] == "off" and values["graphics_detail"] == "4" and values["split_vertical"] == "on"
                and abs(float(values["volume"]) - 0.8) < 0.001, "saved settings differ from effective video/audio state")

        # Quality saves immediately without applying the pending display/split draft.
        app.resolution(640, 480)
        app.select(6)
        app.key("Left", draft_split_vertical=0, split_vertical=1)
        app.change_detail("Left", 3, width=1280, height=800, draft_width=640, draft_height=480,
                          split_vertical=1, draft_split_vertical=0)
        values = preferences(config)
        require(values["graphics_detail"] == "3" and values["width"] == "1280" and values["height"] == "800"
                and values["split_vertical"] == "on", "Immediate quality save applied the pending display/split draft")
        saved = config.read_bytes()
        app.key("Escape", screen="settings", selection=1)
        require(config.read_bytes() == saved, "Escape changed the immediately saved quality or persisted unapplied settings")
        app.key("Return", screen="graphics_options", selection=0, draft_width=1280, draft_height=800, draft_graphics_detail=3, draft_split_vertical=1)
        app.change_detail("Right", 4)
        saved = config.read_bytes()
        app.select(7)
        app.key("Return", screen="settings", selection=1)
        require(config.read_bytes() == saved and preferences(config)["graphics_detail"] == "4", "Back changed immediately saved quality")

        app.open_options(0, "sound_options")
        before = int(app.current()["settings_saved"])
        app.key("Left", effects_volume=90, settings_saved=before + 1)
        app.select(1)
        app.key("Left", music_volume=90, settings_saved=before + 2)
        app.select(2)
        app.key("Return", game_music=0, settings_saved=before + 3)
        app.select(3)
        app.key("Right", stereo_effects=0, settings_saved=before + 4)
        app.capture("sound-1280x800.png", 1280, 800)
        require(app.current()["music_starts"] == initial["music_starts"] and app.current()["music_playing"] == "1",
                "Game music switch must leave the current frontend CD02 instance playing")
        values = preferences(config)
        require(values["effects_volume"] == "90" and values["music_volume"] == "90"
                and values["game_music"] == "off" and values["stereo_effects"] == "off", "Original sound controls did not save immediately")
        saved = config.read_bytes()
        app.select(4)
        app.key("Return", screen="settings", selection=0)
        app.open_options(2, "controller_players")
        app.capture("controller-players-1280x800.png", 1280, 800)
        app.key("Return", screen="controller_options")
        app.capture("controller-1280x800.png", 1280, 800)
        app.select(4)
        app.key("Return", screen="controller_players", selection=0)
        app.select(2)
        app.key("Return", screen="settings", selection=2)
        require(config.read_bytes() == saved, "Browsing Controller Setup must retain preferences")
        app.quit()
    finally:
        stop(app.process)

    app = SettingsWindow("settings-restored", config)
    try:
        row = app.enter()
        require(row["width"] == "1280" and row["height"] == "800" and row["fullscreen"] == "0"
                and row["fps_limit"] == "90" and row["vsync"] == restored["vsync"] and row["mute"] == "0"
                and abs(float(row["volume"]) - 0.8) < 0.001 and row["effects_volume"] == "90" and row["music_volume"] == "90"
                and row["game_music"] == "0" and row["stereo_effects"] == "0" and row["graphics_detail"] == "4" and row["split_vertical"] == "1",
                "relaunch did not restore menu-saved display, detail and audio settings")
        require(config.read_bytes() == saved, "ordinary startup rewrote preferences")
        app.quit()
    finally:
        stop(app.process)


def save_failure():
    config = Path("settings-retry.cfg")
    app = SettingsWindow("settings-retry", config, "--fps-limit", "60", "--vsync", "off")
    try:
        fresh = app.enter()
        require(fresh["width"] == "1280" and fresh["height"] == "720" and fresh["graphics_detail"] == "4"
                and fresh["fullscreen"] == "0" and fresh["fps_limit"] == "60" and fresh["vsync"] == "0",
                "a fresh native launch must use 1280x720 and Mega while retaining explicit display overrides")
        app.open_options(1, "graphics_options")
        # Construct a deterministic write failure after initial config loading.
        config.mkdir()
        app.select(4)
        xdo("key", "Return")
        app.wait("failed save stays usable", screen="graphics_options", settings_requests=1, settings_saved=0, settings_error=1)
        app.capture("settings-save-error.png", 1280, 720)
        app.key("Escape", screen="settings", selection=1)
        config.rmdir()
        app.key("Return", screen="graphics_options", selection=0)
        app.apply()
        require(config.is_file() and preferences(config)["version"] == "1", "save did not recover after destination became writable")
        app.key("Escape", screen="settings", selection=1)
        app.open_options(0, "sound_options")
        config.unlink()
        config.mkdir()
        app.key("Left", effects_volume=90, settings_requests=3, settings_error=1)
        app.capture("sound-save-error.png", 1280, 720)
        config.rmdir()
        app.key("Escape", screen="settings", selection=0, settings_requests=4, settings_saved=2, settings_error=0)
        require(preferences(config)["effects_volume"] == "90", "Sound Back did not retry saving effective audio")
        app.open_options(1, "graphics_options")
        app.resolution(640, 480)
        app.select(6)
        app.key("Right", draft_split_vertical=1, split_vertical=0)
        config.unlink()
        config.mkdir()
        app.select(5)
        app.key("Left", graphics_detail=3, draft_graphics_detail=3, draft_width=640, draft_height=480,
                draft_split_vertical=1, width=1280, height=720, split_vertical=0,
                settings_requests=5, settings_saved=2, settings_error=1)
        config.rmdir()
        app.key("Escape", screen="settings", selection=1, settings_requests=6, settings_saved=3, settings_error=0)
        values = preferences(config)
        require(values["graphics_detail"] == "3" and values["width"] == "1280" and values["height"] == "720"
                and values["split_vertical"] == "off", "Graphics Back must retry quality persistence while discarding display/split drafts")
        app.quit()
    finally:
        stop(app.process)


if __name__ == "__main__":
    executable, data_root = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "settings acceptance requires isolated dummy audio")
    # Xvfb isolates keyboard/mouse events, but SDL gamepads remain shared with
    # the desktop. This keyboard-only route must ignore physical stick input.
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required for private settings acceptance")
    with Path("settings-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private settings window manager")
        settings_flow()
        save_failure()
        print("PASS: options hub, graphics, original sound, Controller Setup navigation, three aspects, save/restore, cancel and failed-save recovery")
    finally:
        stop(manager)
