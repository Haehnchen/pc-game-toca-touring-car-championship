"""Check every original menu car and its generated Mega panorama on private Xvfb."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

import test_settings as settings
from native_support import require, wait_for, window_manager_ready
from test_menu_icons import IconWindow, artwork_source, backdrop, covered_background


CARS = ("audi", "renault", "honda", "volvo", "ford", "nissan", "peugeot", "vectra")


def flow():
    source = {car: artwork_source("menu-background-wide" if car == "audi" else f"menu-background-{car}-wide")
              for car in CARS}
    original_signatures = []
    mega_signatures = []
    for index, car in enumerate(CARS):
        app = IconWindow(f"menu-background-{car}", f"menu-background-{car}.cfg", "--state-dir", f"menu-background-{car}-state",
                         "--resolution", "640x480", "--graphics-detail", "4", "--menu-background", str(index), "--vsync", "off", "--fps-limit", "60")
        try:
            app.language()
            app.key("Return", screen="main_menu", selection=0)
            mega = app.shot(f"mega-{car}")
            covered_background(mega, 640, 480, source[car])
            mega_signatures.append(backdrop(mega))
            if index == 7:
                for width, height in ((1280, 720), (1280, 800)):
                    covered_background(app.shot(f"mega-{car}-{width}x{height}", width, height), width, height, source[car])
                app.shot(f"mega-{car}-4x3")
            app.graphics()
            app.detail(3)
            app.apply(graphics_detail=3)
            app.main()
            original = app.shot(f"original-{car}")
            original_signatures.append(backdrop(original))
            app.quit()
        finally:
            settings.stop(app.process)
    require(len(set(original_signatures)) == 8, "Original menu car image/palette selection repeated a backdrop")
    require(len(set(mega_signatures)) == 8, "Mega menu car selection repeated a panorama")

    # The default path must choose one of the eight complete assets without an
    # override; the index is intentionally not saved to the user's preferences.
    app = IconWindow("menu-background-random", "menu-background-random.cfg", "--state-dir", "menu-background-random-state",
                     "--resolution", "640x480", "--graphics-detail", "4", "--vsync", "off", "--fps-limit", "60")
    try:
        app.language()
        app.key("Return", screen="main_menu", selection=0)
        pixels = app.shot("mega-random")
        matches = []
        for car in CARS:
            try:
                covered_background(pixels, 640, 480, source[car])
                matches.append(car)
            except AssertionError:
                pass
        require(len(matches) == 1, f"Random startup background must match exactly one car, got {matches}")
        app.quit()
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    settings.executable, settings.data_root = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "menu backgrounds require private dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import", "convert"):
        require(shutil.which(tool), f"{tool} required for private menu background acceptance")
    with Path("menu-backgrounds-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private menu background window manager")
        flow()
        print("PASS: eight original and Mega menu cars, random startup, three aspect ratios and profile switch")
    finally:
        settings.stop(manager)
