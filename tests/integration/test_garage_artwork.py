"""Accept all 19 original/Mega garage pairs on private muted Xvfb.

Run through: make check TESTS='^native-garage-artwork$'
Normal builds copy the committed WebPs. Four sequential games exercise all
independent earned bonus flags in both profiles without original GAR archives.
One garage also checks optional-Mega fallback and three aspects. CTest owns
Xvfb and dummy audio; mutable artwork is isolated from the build.
"""

import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

import test_settings as settings
from native_support import require, wait_for, window_manager_ready
from test_menu_icons import IconWindow, changed, region


ASPECTS = ((640, 480), (1280, 720), (1280, 800))
CASES = ((1, False, False, tuple(range(10)), (640, 480)),
         (2, True, False, (7, 8, 9), (1280, 720)),
         (3, False, True, (7, 8, 9), (1280, 800)),
         (4, True, True, (7, 8, 9), (640, 480)))
CAR_RECT = (230, 235, 205, 80)
FRAME_ANCHORS = ((25, 100), (32, 200), (32, 300), (609, 200), (616, 300))


def garage(app):
    app.main()
    app.key("Return", screen="race_menu")
    app.select(0)
    app.key("Return", screen="single_options")
    app.select(4)
    app.key("Return", screen="difficulty")
    app.key("Return", screen="name_entry")
    app.key("Return", screen="car_selection", car_id=0, car_available=1)


def select_car(app, car, available):
    while (current := int(app.current()["car_id"])) != car:
        app.key("Right" if current < car else "Left", screen="car_selection")
    app.wait("garage car and independent unlock", car_id=car, car_available=int(available))


def car_pixels(pixels, width, height):
    # Below every technical label, above every transmission/navigation sprite.
    return region(pixels, width, height, *CAR_RECT)


def same_frame(first, second, width, height):
    # Compare within the same detail profile; menu frames can differ by detail.
    for x, y in FRAME_ANCHORS:
        require(region(first, width, height, x, y, 1, 1) == region(second, width, height, x, y, 1, 1),
                f"garage frame moved at {width}x{height}: {x},{y}")


def linear_sample(image, x, y):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    pixels = [image.getpixel((max(0, min(image.width - 1, px)), max(0, min(image.height - 1, py))))
              for px, py in ((ix, iy), (ix + 1, iy), (ix, iy + 1), (ix + 1, iy + 1))]
    return tuple((1 - fy) * ((1 - fx) * pixels[0][c] + fx * pixels[1][c])
                 + fy * ((1 - fx) * pixels[2][c] + fx * pixels[3][c]) for c in range(3))


def matches_composite(pixels, width, height, source, key):
    scale, left = height / 480, (width - 640 * height / 480) / 2
    # These points avoid all labels, badge overlays, arrows and the menu frame.
    # Sampling the actual bundled image catches a wrong car/variant binding,
    # incorrect source crop or lost high-resolution image without assuming its art style.
    for sy in (82, 120, 140, 240, 280, 315):
        for sx in (65, 145, 225, 305, 385, 465, 545, 575):
            x, y = int(left + (sx + .5) * scale), int((sy + .5) * scale)
            tx = source.width / 573 * ((x + .5 - left) / scale - 34) - .5
            ty = source.height / 357 * ((y + .5) / scale - 63) - .5
            expected, actual = linear_sample(source, tx, ty), pixels[y * width + x]
            # Allow only GPU bilinear-filter/capture rounding, not arbitrary
            # image similarity.
            require(max(abs(a - b) for a, b in zip(actual, expected)) <= 6,
                    f"{key} composition/crop mismatch at {width}x{height}, pixel {x},{y}: {actual} vs {expected}")


def load_composites():
    root = Path(settings.executable).resolve().parent / "assets/ui"
    sources = {}
    for profile, dimensions in (("original", (573, 357)), ("mega", (1146, 714))):
        sources[profile] = {}
        for variant, _, _, cars, _ in CASES:
            for car in cars:
                key = f"gar{variant}-car{car}"
                path = root / profile / "garage" / (key + ".webp")
                require(path.is_file(), f"{profile} garage artwork is required for fleet acceptance: {path.name}")
                with Image.open(path) as image:
                    require(image.format == "WEBP" and image.size == dimensions and image.mode == "RGB",
                            f"{profile}/{key} must be an opaque {dimensions[0]}x{dimensions[1]} finished WebP")
                    sources[profile][key] = image.convert("RGB")
    return sources


def private_runtime(directory):
    executable = Path(settings.executable).resolve()
    runtime = directory / "runtime"
    runtime.mkdir()
    target = runtime / executable.name
    shutil.copy2(executable, target)
    assets = runtime / "assets"
    assets.mkdir()
    for source in (executable.parent / "assets").iterdir():
        if source.name == "ui":
            shutil.copytree(source, assets / source.name)
        else:
            (assets / source.name).symlink_to(source, target_is_directory=source.is_dir())
    for name in ("shaders", "lib", "_deps"):
        source = executable.parent / name
        if source.exists():
            (runtime / name).symlink_to(source, target_is_directory=source.is_dir())
    settings.executable = str(target)


def seed_progress(variant, bonus8, bonus9):
    state = Path(f"garage-artwork-state-{variant}")
    state.mkdir(exist_ok=True)
    state.joinpath("extras.cfg").write_text(
        "version=1\nbonus_cars=off\n"
        + f"earned_car_8={'on' if bonus8 else 'off'}\n"
        + f"earned_car_9={'on' if bonus9 else 'off'}\n")
    return state


def check_badge(first, second, width, height):
    # The center alpha is 251..253, allowing a small underlying-image contribution.
    badge = region(first, width, height, 491, 367, 40, 24)
    other = region(second, width, height, 491, 367, 40, 24)
    require(len(set(badge)) > 20
            and all(max(abs(a - b) for a, b in zip(a_pixel, b_pixel)) <= 6
                    for a_pixel, b_pixel in zip(badge, other)),
            f"garage artwork covered the transmission badge at {width}x{height}")


def flow(variant, bonus8, bonus9, cars, dimensions, sources):
    state = seed_progress(variant, bonus8, bonus9)
    width, height = dimensions
    app = IconWindow(f"garage-artwork-{variant}", f"garage-artwork-{variant}.cfg", "--state-dir", str(state),
                     "--resolution", f"{width}x{height}", "--graphics-detail", "0", "--vsync", "off", "--fps-limit", "60")
    available = lambda car: car < 8 or (bonus8 if car == 8 else bonus9)
    seen = set()
    original, mega = {}, {}
    try:
        app.language()
        app.wait("independent earned bonus state", earned_car_8=int(bonus8), earned_car_9=int(bonus9), extras_bonus_cars=0)
        app.key("Return", screen="main_menu", selection=0)
        garage(app)
        for car in cars:
            select_car(app, car, available(car))
            key = f"gar{variant}-car{car}"
            original[car] = app.shot("original-" + key, width, height)
            matches_composite(original[car], width, height, sources["original"][key], "original/" + key)
            same_frame(original[cars[0]], original[car], width, height)
            seen.add(("original", key))
            if not available(car):
                app.key("Return", screen="car_selection", car_id=car, car_available=0)
        app.graphics()
        app.detail(4)
        app.apply(graphics_detail=4)
        garage(app)
        for car in cars:
            select_car(app, car, available(car))
            key = f"gar{variant}-car{car}"
            mega[car] = app.shot("mega-" + key, width, height)
            matches_composite(mega[car], width, height, sources["mega"][key], "mega/" + key)
            changed(car_pixels(original[car], width, height), car_pixels(mega[car], width, height), key)
            same_frame(mega[cars[0]], mega[car], width, height)
            seen.add(("mega", key))
            if car != cars[0] and available(car):
                check_badge(mega[cars[0]], mega[car], width, height)
            if not available(car):
                app.key("Return", screen="car_selection", car_id=car, car_available=0)

        if variant == 1:
            select_car(app, 0, True)
            app.key("Up", manual_transmission=1)
            manual = app.shot("mega-honda-manual", width, height)
            changed(region(mega[0], width, height, 48, 384, 260, 28),
                    region(manual, width, height, 48, 384, 260, 28), "visible transmission caption")
            require(car_pixels(mega[0], width, height) == car_pixels(manual, width, height),
                    "changing transmission displaced the composed garage")
            app.key("Up", manual_transmission=0)
            for size in ASPECTS:
                shot = app.shot(f"mega-honda-{size[0]}x{size[1]}", *size)
                matches_composite(shot, *size, sources["mega"]["gar1-car0"], "mega/gar1-car0")
            # The copied runtime makes optional-artwork removal safe for other tests.
            optional = Path(settings.executable).parent / "assets/ui/mega/garage/gar1-car0.webp"
            saved = optional.with_suffix(".hidden")
            optional.rename(saved)
            try:
                select_car(app, 1, True)
                select_car(app, 0, True)
                fallback = app.shot("mega-honda-original-fallback", width, height)
                matches_composite(fallback, width, height, sources["original"]["gar1-car0"], "fallback/gar1-car0")
                require(car_pixels(fallback, width, height) == car_pixels(original[0], width, height),
                        "missing Mega garage did not select the original image")
            finally:
                saved.rename(optional)
            app.graphics()
            app.detail(0)
            app.apply(graphics_detail=0)
            garage(app)
            restored = app.shot("detail0-honda-restored", width, height)
            require(car_pixels(restored, width, height) == car_pixels(original[0], width, height),
                    "returning to detail0 did not restore the original garage")
            matches_composite(restored, width, height, sources["original"]["gar1-car0"], "original/gar1-car0")
        row = app.current()
        require(row["audio_active"] == "1" and row["music_track"] == "2"
                and row["music_playing"] == "1" and row["music_starts"] == "1",
                f"GAR{variant} artwork route interrupted the real frontend audio/CD02 path")
        app.quit()
        final = app.reader.rows()[-1]
        require(final["audio_active"] == "0" and final["audio_voices"] == "0", "garage quit leaked audio")
    finally:
        settings.stop(app.process)
    return seen


if __name__ == "__main__":
    settings.executable, canonical_data = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "garage acceptance requires private dummy audio")
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required for private garage artwork acceptance")
    with Path("garage-artwork-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private garage artwork window manager")
        with tempfile.TemporaryDirectory(prefix="garage-data-", dir=Path.cwd()) as temporary:
            private_runtime(Path(temporary))
            sources = load_composites()
            isolated_data = Path(temporary) / "game-data"
            shutil.copytree(canonical_data, isolated_data,
                            ignore=shutil.ignore_patterns("gar1.bfp", "gar2.bfp", "gar3.bfp", "gar4.bfp"))
            require(not list((isolated_data / "fe/images").glob("gar*.bfp")),
                    "garage acceptance must run without original GAR archives")
            settings.data_root = str(isolated_data)
            seen = set()
            for case in CASES:
                seen.update(flow(*case, sources))
        require(seen == {(profile, key) for profile, images in sources.items() for key in images} and len(seen) == 38, "native garage acceptance missed a reachable variant/car mapping")
        print("PASS: all19 original/Mega pairs without original GARs, independent bonus states, locked confirmation, caption/badge ordering, optional-Mega fallback, three aspects and CD02")
    finally:
        settings.stop(manager)
