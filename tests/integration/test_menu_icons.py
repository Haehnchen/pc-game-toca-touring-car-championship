"""Exercise Mega menu artwork through private, muted native menu routes."""

import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

import test_settings as settings
from native_support import read_png, require, wait_for, window_manager_ready, xdo


ASPECTS = ((640, 480), (1280, 720), (1280, 800), (1140, 540))


def icon_row(count, index):
    return 224 - (count * 68 - 14) // 2 + 15 + index * 68


def region(pixels, width, height, x, y, w, h):
    scale = height / 480
    left = (width - 640 * scale) / 2
    return tuple(pixels[int((sy + .5) * scale) * width + int(left + (sx + .5) * scale)]
                 for sy in range(y, y + h) for sx in range(x, x + w))


def changed(first, second, description):
    require(len(first) == len(second) and sum(a != b for a, b in zip(first, second)) > len(first) // 30,
            f"{description}: Mega artwork did not replace the original pixels")


def backdrop(pixels, width=640, height=480):
    # Outside the frame, without icons, labels or help text.
    return (region(pixels, width, height, 2, 80, 10, 320)
            + region(pixels, width, height, 628, 80, 10, 320))


def artwork_source(name):
    artwork = Path(settings.executable).resolve().parent / f"assets/ui/mega/{name}.webp"
    require(artwork.is_file(), f"Mega {name} artwork is required for this acceptance")
    target = Path(f"{name}-source.png")
    subprocess.run(["convert", str(artwork), "-define", "png:compression-filter=1", "PNG24:" + str(target)],
                   check=True, timeout=10)
    return read_png(target)


def linear_sample(source, sx, sy):
    image_width, image_height, artwork = source

    def texel(x, y):
        x, y = max(0, min(image_width - 1, x)), max(0, min(image_height - 1, y))
        return artwork[y * image_width + x]

    ix, iy = math.floor(sx), math.floor(sy)
    fx, fy = sx - ix, sy - iy
    return tuple((1 - fy) * ((1 - fx) * texel(ix, iy)[c] + fx * texel(ix + 1, iy)[c])
                 + fy * ((1 - fx) * texel(ix, iy + 1)[c] + fx * texel(ix + 1, iy + 1)[c])
                 for c in range(3))


def cover_sample(source, width, height, x, y):
    image_width, image_height, _ = source
    scale = max(width / image_width, height / image_height)
    left, top = (width - image_width * scale) / 2, (height - image_height * scale) / 2
    return linear_sample(source, (x + .5 - left) / scale - .5, (y + .5 - top) / scale - .5)


def covered_background(pixels, width, height, source):
    ui_scale, ui_left = height / 480, (width - 640 * height / 480) / 2
    # Check both sides of the old centered-canvas boundary as well as clear
    # interior photograph areas. A reflected side strip or retained Audi inset
    # cannot match the corresponding locations in the complete source image.
    # ImageMagick's window capture can duplicate the final client column when
    # the window reaches the private display edge. Check the adjacent column.
    xs = {0, width - 2}
    xs.update(int(ui_left + x * ui_scale) for x in (-60, -20, -2, 2, 8, 550, 575, 632, 638, 642, 660, 700))
    for y in (int(value * ui_scale) for value in (85, 130, 190, 255, 320, 385)):
        for x in sorted(value for value in xs if 0 <= value < width):
            expected = cover_sample(source, width, height, x, y)
            actual = pixels[y * width + x]
            require(max(abs(a - b) for a, b in zip(actual, expected)) <= 6,
                    f"Mega background lost uniform cover at {width}x{height}, pixel {x},{y}: {actual} vs {expected}")


def frame_pixels(pixels, width=640, height=480):
    return (region(pixels, width, height, 20, 80, 25, 320)
            + region(pixels, width, height, 596, 80, 25, 320))


def centered_frame(pixels, original, width, height, source, alpha, background):
    image_width, image_height, _ = source
    scale, left = height / 480, (width - 640 * height / 480) / 2
    checked = 0
    replaced = 0
    for sy in (100, 160, 220, 280, 340, 400):
        for sx in (20, 25, 32, 39, 44, 596, 602, 609, 616, 620):
            x, y = int(left + (sx + .5) * scale), int((sy + .5) * scale)
            tx, ty = (x + .5 - left) * image_width / (640 * scale) - .5, (y + .5) * image_height / height - .5
            ix, iy = math.floor(tx), math.floor(ty)
            # The generated metal retains slight transparency. Test its solid
            # interiors against the actual frame/background alpha composite.
            if min(alpha[py * image_width + px] for py in (iy, iy + 1) for px in (ix, ix + 1)) < 240:
                continue
            fx, fy = tx - ix, ty - iy
            opacity = ((1 - fy) * ((1 - fx) * alpha[iy * image_width + ix] + fx * alpha[iy * image_width + ix + 1])
                       + fy * ((1 - fx) * alpha[(iy + 1) * image_width + ix] + fx * alpha[(iy + 1) * image_width + ix + 1])) / 255
            foreground, behind = linear_sample(source, tx, ty), cover_sample(background, width, height, x, y)
            expected = tuple(opacity * a + (1 - opacity) * b for a, b in zip(foreground, behind))
            actual = pixels[y * width + x]
            require(max(abs(a - b) for a, b in zip(actual, expected)) <= 6,
                    f"Mega frame lost centered 4:3 placement at {width}x{height}, pixel {x},{y}")
            checked += 1
            replaced += actual != original[y * width + x]
    require(checked >= 12 and replaced >= checked // 2,
            f"Mega frame did not visibly replace the original rails at {width}x{height}")


class IconWindow(settings.SettingsWindow):
    def select(self, target):
        for _ in range(20):
            if int(self.current()["selection"]) == target:
                return
            self.key("Down")
        raise AssertionError(f"cannot reach menu row {target}")

    def language(self):
        self.wait("startup", screen="title")
        self.window = xdo("search", "--onlyvisible", "--pid", self.process.pid,
                          "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", self.window)
        self.wait("focused startup", focused=1)
        xdo("key", "Return")
        self.wait("language", screen="language", selection=0)

    def main(self):
        for _ in range(10):
            if self.current()["screen"] == "main_menu":
                self.select(0)
                return
            self.key("Escape")
        raise AssertionError("menu artwork route could not return to Main")

    def graphics(self):
        self.main()
        self.select(1)
        self.key("Return", screen="settings", selection=0)
        self.select(1)
        self.key("Return", screen="graphics_options", selection=0)

    def detail(self, value):
        self.select(5)
        for _ in range(5):
            if self.current()["draft_graphics_detail"] == str(value):
                return
            previous = self.current()
            detail = (int(previous["draft_graphics_detail"]) + 1) % 5
            self.change_detail("Right", detail)
        raise AssertionError(f"detail {value} unavailable")

    def shot(self, name, width=640, height=480):
        xdo("windowsize", "--sync", self.window, width, height)
        self.wait("resized menu", width=width, height=height, presented=1)
        return self.capture(name + ".png", width, height)

    def quit(self):
        self.main()
        self.select(4)
        xdo("key", "Return")
        require(self.process.wait(timeout=5) == 0, "menu artwork route broke normal Quit")


def flow():
    source = artwork_source("menu-background-wide")
    frame = artwork_source("menu-frame")
    alpha = subprocess.run(["convert", str(Path(settings.executable).resolve().parent / "assets/ui/mega/menu-frame.webp"),
                            "-alpha", "extract", "-depth", "8", "gray:-"],
                           check=True, capture_output=True, timeout=10).stdout
    require(len(alpha) == frame[0] * frame[1], "Mega menu frame alpha dimensions differ from its color image")
    require(min(alpha) == 0 and max(alpha) == 255, "Mega menu frame must contain transparent holes and opaque rails")
    app = IconWindow("menu-icons", "menu-icons.cfg", "--state-dir", "menu-icons-state",
                     "--resolution", "640x480", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "60", "--menu-background", "0")
    try:
        app.language()
        original_language = app.shot("original-language")
        app.key("Return", screen="main_menu", selection=0)
        original_main = app.shot("original-main")
        original_aspects = {(640, 480): original_main}
        for width, height in ASPECTS[1:]:
            original_aspects[width, height] = app.shot(f"original-main-{width}x{height}", width, height)
        app.shot("original-main-4x3")
        app.select(3)
        original_extras = app.shot("original-main-extras")
        app.graphics()
        app.select(5)
        original_graphics = app.shot("original-graphics")
        crop = lambda pixels: region(pixels, 640, 480, 73, icon_row(5, 0), 88, 56)
        app.detail(2)
        high = app.shot("original-graphics-high")
        require(crop(high) == crop(original_graphics),
                "High and Very High must retain identical original icons")
        require(backdrop(high) == backdrop(original_graphics),
                "High and Very High must retain the original background")
        app.detail(4)
        require(settings.preferences(Path("menu-icons.cfg"))["graphics_detail"] == "4", "Mega selection did not save immediately")
        mega_graphics = app.shot("mega-graphics-immediate")
        changed(crop(original_graphics), crop(mega_graphics), "immediate detail icon")
        changed(backdrop(original_graphics), backdrop(mega_graphics), "immediate background")
        changed(frame_pixels(original_graphics), frame_pixels(mega_graphics), "immediate frame")
        saved = Path("menu-icons.cfg").read_bytes()
        app.key("Escape", screen="settings", selection=1, graphics_detail=4, draft_graphics_detail=4)
        app.key("Return", screen="graphics_options", selection=0)
        app.select(5)
        retained = app.shot("mega-graphics-retained")
        require(crop(retained) == crop(mega_graphics), "Leaving Graphics changed the saved quality icon")
        require(backdrop(retained) == backdrop(mega_graphics), "Leaving Graphics changed the saved Mega background")
        require(frame_pixels(retained) == frame_pixels(mega_graphics), "Leaving Graphics changed the saved Mega frame")
        require(Path("menu-icons.cfg").read_bytes() == saved, "Browsing Graphics rewrote saved quality")
        app.main()
        mega_main = app.shot("mega-main")
        covered_background(mega_main, 640, 480, source)
        for index in range(5):
            changed(region(original_main, 640, 480, 73, icon_row(5, index), 88, 56),
                    region(mega_main, 640, 480, 73, icon_row(5, index), 88, 56), f"Main row {index}")
        app.select(3)
        changed(region(original_extras, 640, 480, 73, icon_row(5, 3), 88, 56),
                region(app.shot("mega-main-extras"), 640, 480, 73, icon_row(5, 3), 88, 56), "selected Extras")
        app.graphics()
        app.detail(3)
        app.key("Escape", screen="settings", graphics_detail=3, draft_graphics_detail=3)
        require(settings.preferences(Path("menu-icons.cfg"))["graphics_detail"] == "3", "Leaving Graphics reverted immediately saved original detail")
        app.main()
        restored = app.shot("original-main-restored")
        for width, height in ASPECTS:
            pixels = restored if (width, height) == (640, 480) else app.shot(f"original-main-restored-{width}x{height}", width, height)
            require(backdrop(pixels, width, height) == backdrop(original_aspects[width, height], width, height),
                    f"leaving Mega failed to restore original background at {width}x{height}")
            require(frame_pixels(pixels, width, height) == frame_pixels(original_aspects[width, height], width, height),
                    f"leaving Mega failed to restore original frame at {width}x{height}")
        for index in range(5):
            require(region(restored, 640, 480, 73, icon_row(5, index), 88, 56)
                    == region(original_main, 640, 480, 73, icon_row(5, index), 88, 56),
                    f"leaving Mega did not restore original Main row {index}")
        app.graphics()
        app.detail(4)
        for width, height in ASPECTS:
            app.main()
            pixels = app.shot(f"mega-main-{width}x{height}", width, height)
            covered_background(pixels, width, height, source)
            centered_frame(pixels, original_aspects[width, height], width, height, frame, alpha, source)
            app.graphics()
            app.select(5)
            app.shot(f"mega-graphics-second-page-{width}x{height}", width, height)
        app.main()
        app.select(1)
        app.key("Return", screen="settings", selection=0)
        app.shot("mega-options-hub", 1280, 800)
        app.key("Return", screen="sound_options", selection=0)
        app.shot("mega-sound-max-arrows", 1280, 800)
        app.key("Return", effects_volume=0)
        app.shot("mega-sound-min-arrows", 1280, 800)
        app.key("Escape", screen="settings")
        app.select(2)
        app.key("Return", screen="controller_players", selection=0)
        app.shot("mega-controller-players", 1280, 800)
        app.key("Return", screen="controller_options")
        app.shot("mega-controller", 1280, 800)
        app.select(3)
        app.key("Return", screen="controller_native_options", selection=0)
        app.shot("mega-controller-native-options", 1280, 800)
        app.main()
        app.select(3)
        app.key("Return", screen="extras", selection=0)
        app.shot("mega-extras", 1280, 800)
        for category, index in (("unlocks", 0), ("camera", 1), ("effects", 2), ("racing", 3)):
            app.select(index)
            app.key("Return", screen="extras_" + category, selection=0)
            app.shot("mega-extras-" + category, 1280, 800)
            app.key("Escape", screen="extras")
        app.main()
        app.key("Return", screen="race_menu")
        app.shot("mega-race", 1280, 800)
        app.select(3)
        app.key("Return", screen="network_linkup", selection=0)
        app.shot("mega-network", 1280, 800)
        app.key("Escape", screen="race_menu")
        app.select(1)
        app.key("Return", screen="championship_menu", selection=0)
        app.shot("mega-championship", 1280, 800)
        app.key("Return", screen="championship_options", selection=1)
        app.shot("mega-championship-options", 1280, 800)
        app.main()
        app.key("Return", screen="race_menu")
        app.select(2)
        app.key("Return", screen="time_trial_options", selection=2)
        app.shot("mega-time-trial", 1280, 800)
        app.key("Escape", screen="race_menu")
        app.select(0)
        app.key("Return", screen="single_options", selection=4)
        app.shot("mega-single-options", 1280, 800)
        app.select(4)
        app.key("Return", screen="difficulty")
        app.shot("mega-difficulty", 1280, 800)
        app.key("Return", screen="name_entry")
        app.key("Return", screen="car_selection", car_id=0, car_available=1)
        app.shot("mega-car-available", 1280, 800)
        for car in range(1, 9):
            app.key("Right", car_id=car)
        app.wait("locked bonus car", car_available=0)
        app.shot("mega-car-unavailable", 1280, 800)
        app.key("Left", car_id=7, car_available=1)
        app.key("Return", screen="track_selection", track_id=0)
        app.shot("mega-track-first", 1280, 800)
        app.key("Right", track_id=1)
        app.key("Right", track_id=2, track_available=0)
        app.shot("mega-track-unavailable-arrow", 1280, 800)
        app.quit()
    finally:
        settings.stop(app.process)

    # A normal relaunch consumes saved Mega in language selection too. German
    # then exercises the widest localized settings labels in the same route.
    app = IconWindow("menu-icons-restored", "menu-icons.cfg", "--state-dir", "menu-icons-state",
                     "--resolution", "640x480", "--vsync", "off", "--fps-limit", "60", "--menu-background", "0")
    try:
        app.language()
        app.wait("saved Mega", graphics_detail=4)
        mega_language = app.shot("mega-language")
        require(backdrop(mega_language) == backdrop(mega_main), "saved Mega did not restore the menu background")
        require(frame_pixels(mega_language) == frame_pixels(mega_main), "saved Mega did not restore the menu frame")
        for index in range(3):
            changed(region(original_language, 640, 480, 73, icon_row(3, index), 88, 56),
                    region(mega_language, 640, 480, 73, icon_row(3, index), 88, 56), f"language row {index}")
        app.select(2)
        app.key("Return", screen="main_menu", language=2)
        app.shot("mega-main-german", 1280, 720)
        app.graphics()
        app.select(4)
        app.shot("mega-graphics-german", 1280, 720)
        app.quit()
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    settings.executable, settings.data_root = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "menu icon acceptance requires private dummy audio")
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    for tool in ("xdotool", "openbox", "xprop", "import", "convert"):
        require(shutil.which(tool), f"{tool} required for private menu icon acceptance")
    with Path("menu-icons-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private menu icon window manager")
        flow()
        print("PASS: Mega/original icons, background and frame, cover/placement, immediate quality save/restore, menu routes and four aspects")
    finally:
        settings.stop(manager)
