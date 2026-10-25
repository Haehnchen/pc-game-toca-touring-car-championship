"""Exercise native startup and frontend through its CLI and owned X11 window."""

import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time

from PIL import Image

from native_support import capture_window, leave_scene, TraceReader, check_flag_animation, check_horizon_animation, check_horizon_selection, qualifying_rollout, read_png, require, wait_for, window_manager_ready, xdo


def read_startup_image(path):
    with Image.open(path) as image:
        require(image.format == "WEBP", "startup artwork must be WebP")
        return image.width, image.height, list(image.convert("RGB").getdata())


def run_case(name, *arguments, environment=None):
    trace = Path(f"{name}.csv")
    with Path(f"{name}.log").open("w") as log:
        result = subprocess.run([executable, "--fullscreen", "off", "--graphics-detail", "3", "--data-root", data_root, "--config", f"{name}.cfg",
                                 "--trace", str(trace), *arguments], stdout=log,
                                stderr=subprocess.STDOUT, env=environment, timeout=12)
    require(result.returncode == 0, f"{name} exited {result.returncode}; see {name}.log")
    rows = TraceReader(trace).rows()
    require(rows and any(row["audio_active"] == "1" for row in rows), "native audio service was not active")
    require(all(int(row["audio_events"]) >= 1 for row in rows), "startup event did not reach audio service")
    return rows


def copyright_aspects(artwork):
    image_width, image_height, image = read_startup_image(artwork / "mega/cpyright.webp")
    require(image_width * 3 == image_height * 4 and image_width > 640, "copyright replacement must retain a higher-resolution 4:3 canvas")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required for private copyright acceptance")
    with Path("copyright-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    application = None
    try:
        wait_for(window_manager_ready, "copyright window manager")
        for width, height, detail, skip_key in ((640, 480, 3, "Return"), (1280, 720, 4, "Up"), (1280, 800, 3, "q")):
            name = f"copyright-{width}x{height}"
            with Path(f"{name}.log").open("w") as log:
                application = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", f"{name}.cfg",
                                                 "--resolution", f"{width}x{height}", "--vsync", "off", "--fps-limit", "60",
                                                 "--graphics-detail", str(detail), "--audio-offline", "--trace", f"{name}.csv"],
                                                stdout=log, stderr=subprocess.STDOUT)
            reader = TraceReader(Path(f"{name}.csv"))
            wait_for(lambda: reader.rows(), f"{name} first frame")
            window = xdo("search", "--onlyvisible", "--pid", application.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
            xdo("windowactivate", "--sync", window)
            # Openbox moves the frame, so compensate its border to keep the
            # full client inside the private display at its maximum width.
            frame = subprocess.run(["xprop", "-id", window, "_NET_FRAME_EXTENTS"],
                                   check=True, capture_output=True, text=True, timeout=3).stdout
            edges = re.search(r"= (\d+), (\d+), (\d+), (\d+)", frame)
            require(edges, "private window manager did not report frame extents")
            xdo("windowmove", "--sync", "--", window, -int(edges[1]), -int(edges[3]))
            xdo("key", "Return")
            wait_for(lambda: any(row["screen"] == "copyright" and row["presented"] == "1" for row in reader.rows()),
                     f"{name} presented copyright")
            capture_window(window, f"{name}.png")
            actual_width, actual_height, pixels = read_png(Path(f"{name}.png"))
            require((actual_width, actual_height) == (width, height), "copyright capture has stale drawable dimensions")
            scale = min(width / image_width, height / image_height)
            left = (width - image_width * scale) / 2
            top = (height - image_height * scale) / 2
            for y in range(height):
                for x in (0, max(0, int(left) - 1), width - 1):
                    if x < left or x >= width - left or y < top or y >= height - top:
                        require(pixels[y * width + x] == (0, 0, 0), "copyright bars must remain opaque black")
            for y in range(max(0, int(top)) + 4, int(height - top) - 4, max(1, height // 19)):
                for x in range(max(0, int(left)) + 4, int(width - left) - 4, max(1, width // 23)):
                    def texels(position, origin, limit):
                        coordinate = (position + .5 - origin) / scale
                        nearest = round(coordinate)
                        values = (nearest - 1, nearest) if abs(coordinate - nearest) < .001 else (int(coordinate),)
                        return {max(0, min(limit - 1, value)) for value in values}

                    expected = {image[sy * image_width + sx] for sy in texels(y, top, image_height) for sx in texels(x, left, image_width)}
                    require(pixels[y * width + x] in expected, f"copyright lost its complete centered 4:3 artwork at {width}x{height}, pixel {x},{y}")
            require(reader.rows()[-1]["screen"] == "copyright", "copyright capture ran past its loading interval")
            copyright_row = next(row for row in reader.rows() if row["screen"] == "copyright" and row["presented"] == "1")
            xdo("key", skip_key)
            wait_for(lambda: sum(row["screen"] == "language" for row in reader.rows()) >= 3,
                     f"{name} {skip_key} skips copyright without confirming language")
            current = reader.rows()
            language_rows = [row for row in current if row["screen"] == "language"]
            language_row = language_rows[0]
            require(int(language_row["timestamp_ns"]) - int(copyright_row["timestamp_ns"]) < 3_000_000_000,
                    f"{skip_key} must skip copyright before its default three-second timeout")
            require(current[-1]["screen"] == "language" and all(row["selection"] == "0" for row in language_rows),
                    f"copyright skip key {skip_key} leaked confirmation or navigation into language selection")
            require(all(row["frontend_cues"] == "0" for row in current), "copyright skip must not invent a menu cue")
            require(all(row["music_track"] == "2" and row["music_playing"] == "1" and row["music_starts"] == "1" for row in language_rows),
                    "copyright skip must start CD02 exactly once on the language menu")
            xdo("key", "alt+F4")
            require(application.wait(timeout=5) == 0, f"{name} window-close exit failed")
            application = None
    finally:
        for process in (application, manager):
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=2)


def aspects():
    artwork = Path(executable).resolve().parent / "assets/ui"
    title_width, title_height, title = read_startup_image(artwork / "original/title.webp")
    require((title_width, title_height) == (640, 480), "unexpected 4:3 title dimensions")
    image_width, image_height, wide = read_startup_image(artwork / "mega/title-wide.webp")
    for width, height in ((640, 480), (1280, 720), (1280, 800), (1140, 540), (720, 720)):
        name = f"title-{width}x{height}"
        rows = run_case(name, "--resolution", f"{width}x{height}", "--vsync", "off",
                        "--frames", "3", "--screenshot", f"{name}.png")
        actual_width, actual_height, pixels = read_png(Path(f"{name}.png"))
        require((actual_width, actual_height) == (width, height), "readback is not actual drawable size")
        require(any((int(row["width"]), int(row["height"])) == (width, height) for row in rows), "drawable trace mismatch")
        if width * 3 == height * 4:
            require(pixels == title, "committed 640x480 WebP title differs from readback")
            continue
        scale = max(width / image_width, height / image_height)
        left = (width - image_width * scale) / 2
        top = (height - image_height * scale) / 2

        def source_texels(pixel, origin, limit):
            coordinate = (pixel + .5 - origin) / scale
            nearest = round(coordinate)
            # Nearest sampling exactly on a source-texel boundary can choose
            # either adjacent texel after GPU floating-point interpolation.
            candidates = (nearest - 1, nearest) if abs(coordinate - nearest) < .001 else (int(coordinate),)
            return {max(0, min(limit - 1, value)) for value in candidates}

        # Cover both the center and outer edges of the complete replacement;
        # 16:10/square crop horizontally, while 19:9 crops vertically.
        xs = {0, width - 1, *range(width // 34, width, max(1, width // 17))}
        ys = {0, height - 1, *range(height // 34, height, max(1, height // 17))}
        for y in ys:
            for x in xs:
                expected = {wide[sy * image_width + sx]
                            for sy in source_texels(y, top, image_height)
                            for sx in source_texels(x, left, image_width)}
                require(pixels[y * width + x] in expected,
                        f"complete title artwork lost centered uniform cover at {width}x{height}, pixel {x},{y}")
    copyright_aspects(artwork)
    run_case("saved", "--resolution", "800x600", "--fps-limit", "60", "--vsync", "off",
             "--volume", "0.25", "--mute", "on", "--save-settings", "--frames", "2")
    saved = Path("saved.cfg").read_bytes()
    rows = run_case("restored", "--config", "saved.cfg", "--frames", "2")
    require(any(row["width"] == "800" and row["height"] == "600" and row["fps_limit"] == "60" for row in rows),
            "saved video preferences were not restored")
    run_case("override", "--config", "saved.cfg", "--resolution", "640x480", "--volume", "0.5", "--frames", "2")
    require(Path("saved.cfg").read_bytes() == saved, "diagnostic CLI override persisted without a save action")
    print("PASS: original 4:3 title, complete wide title, centered 4:3 copyright with black bars and keyboard skip, audio lifecycle and preference save/restore")


def pacing():
    for cap in (30, 60, 120):
        # Exercise pacing with a modest drawable: large title rendering can
        # exceed the120FPS budget before pacing runs. Aspect coverage above
        # exercises the larger drawable sizes.
        rows = run_case(f"cap-{cap}", "--resolution", "640x480", "--vsync", "off", "--fps-limit", str(cap), "--frames", "36")
        times = [int(row["timestamp_ns"]) for row in rows if row["presented"] == "1"][5:]
        intervals = [(b - a) / 1e9 for a, b in zip(times, times[1:])]
        median = statistics.median(intervals)
        # Xvfb measures cap scheduling, not a physical monitor. Allow 20% or 2ms.
        tolerance = max(0.002, 0.2 / cap)
        require(abs(median - 1 / cap) <= tolerance, f"{cap} FPS median {median:.6f}s exceeds tolerance {tolerance:.6f}s")
        require(all(int(current["timestamp_ns"]) > int(previous["timestamp_ns"])
                    and int(current["elapsed_ns"]) == int(current["timestamp_ns"]) - int(previous["timestamp_ns"])
                    for previous, current in zip(rows, rows[1:])), "explicit elapsed clock differs from observed monotonic time")
        print(f"PASS: cap={cap}, median={median:.6f}s, tolerance={tolerance:.6f}s")


def interaction():
    for tool in ("xdotool", "openbox", "xprop"):
        require(shutil.which(tool), f"{tool} required for native window interaction")
    with Path("openbox.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    application = None

    def xdo(*args):
        return subprocess.run(["xdotool", *map(str, args)], capture_output=True, text=True, timeout=2)

    try:
        wait_for(window_manager_ready, "private window manager readiness")
        with Path("interaction.log").open("w") as log:
            application = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "interaction.cfg",
                                             "--resolution", "960x720", "--vsync", "off", "--fps-limit", "60",
                                             "--trace", "interaction.csv", "--screenshot", "interaction.png"],
                                            stdout=log, stderr=subprocess.STDOUT)
        window = wait_for(lambda: xdo("search", "--onlyvisible", "--pid", application.pid,
                                     "--name", "^TOCA Touring Car Championship$").stdout.strip(), "native window")
        window = window.splitlines()[0]
        require(xdo("windowactivate", "--sync", window).returncode == 0, "cannot activate native window")
        trace = Path("interaction.csv")
        trace_reader = TraceReader(trace)
        wait_for(lambda: any(row["focused"] == "1" for row in trace_reader.rows()), "focused native frame")
        xdo("keydown", "Right")
        wait_for(lambda: any(int(row["pressed"]) & 2 for row in trace_reader.rows()), "right press edge")
        xdo("keydown", "Right")
        time.sleep(0.1)
        xdo("windowminimize", window)
        wait_for(lambda: any(row["focused"] == "0" and int(row["released"]) & 2 for row in trace_reader.rows()),
                 "focus loss releases held input")
        wait_for(lambda: any(row["minimized"] == "1" for row in trace_reader.rows()), "minimize notification")
        xdo("keyup", "Right")
        xdo("windowmap", window)
        xdo("windowactivate", "--sync", window)
        xdo("windowsize", "--sync", window, 1000, 600)
        wait_for(lambda: any(row["width"] == "1000" and row["height"] == "600"
                             and row["presented"] == "1" for row in trace_reader.rows()), "restored resized drawable")
        xdo("mousemove", "--window", window, 500, 300)
        wait_for(lambda: any(row["pointer_inside"] == "1" and abs(float(row["pointer_x"]) - 320) < 1
                             and abs(float(row["pointer_y"]) - 240) < 1 for row in trace_reader.rows()), "inverse UI pointer mapping")
        before = len(trace_reader.rows())
        xdo("mousemove", "--window", window, 5, 300)
        wait_for(lambda: any(row["pointer_inside"] == "0" for row in trace_reader.rows()[before:]), "pointer outside UI")
        xdo("key", "alt+F4")
        require(application.wait(timeout=5) == 0, "native window-close exit failed")
        rows = trace_reader.rows()
        require(sum(bool(int(row["pressed"]) & 2) for row in rows) == 1, "held/repeated key generated extra input edges")
        require(all(row["audio_active"] == "1" for row in rows[:-1]) and rows[-1]["audio_active"] == "0",
                "audio service lifecycle differs from window/application lifecycle")
        require(max(int(row["audio_events"]) for row in rows) >= 3, "focus events did not reach audio service")
        require(read_png(Path("interaction.png"))[:2] == (1000, 600), "resized capture has stale extent")
        print("PASS: key edges, focus, minimize/restore, live drawable resize, pointer mapping, audio continuity and window close")
    finally:
        for process in (application, manager):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)


def frontend():
    run_case("gpu-driver", "--gpu-driver", "vulkan", "--resolution", "640x480", "--frames", "2",
             environment={**os.environ, "SDL_GPU_DRIVER": "unsupported"})
    require("Renderer: SDL GPU / vulkan /" in Path("gpu-driver.log").read_text(),
            "Explicit GPU driver must override an inherited unsupported backend and render real frames")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required for private frontend acceptance")
    with Path("frontend-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    application = None

    try:
        wait_for(window_manager_ready, "frontend window manager")
        with Path("frontend.log").open("w") as log:
            application = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "frontend.cfg",
                                             "--resolution", "640x480", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "60",
                                             "--audio-offline", "--trace", "frontend.csv", "--screenshot", "main-menu.png"],
                                            stdout=log, stderr=subprocess.STDOUT)
        trace = Path("frontend.csv")
        trace_reader = TraceReader(trace)

        def rows():
            require(application.poll() is None, "frontend exited early; see frontend.log")
            return trace_reader.rows()

        wait_for(lambda: rows(), "first frontend trace", timeout=8)
        window = xdo("search", "--onlyvisible", "--pid", application.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)
        initial = rows()
        require(initial[-1]["screen"] == "title" and initial[-1]["music_track"] == "0", "title must begin without music")
        # Exercise the real five-second timeout once; unit cases cover arbitrary-key dismissal.
        wait_for(lambda: any(row["screen"] == "copyright" and row["presented"] == "1" for row in rows()),
                 "timed title to copyright", timeout=8)
        copyright_row = next(row for row in rows() if row["screen"] == "copyright" and row["presented"] == "1")
        # Leave loading unpressed to exercise its default visibility interval.
        wait_for(lambda: any(row["screen"] == "language" for row in rows()), "copyright to language", timeout=8)
        current = rows()
        language_row = next(row for row in current if row["screen"] == "language")
        require(int(language_row["timestamp_ns"]) - int(copyright_row["timestamp_ns"]) >= 3_000_000_000,
                "copyright must remain visible for at least three seconds after its first presentation")
        require(language_row["selection"] == "0", "timed copyright handoff must start on the first language")
        require(all(row["music_track"] == "0" and row["frontend_cues"] == "0" for row in current if row["screen"] == "copyright"),
                "copyright wait must remain silent until the language menu")
        require(language_row["music_track"] == "2" and language_row["music_playing"] == "1"
                and language_row["music_starts"] == "1" and language_row["frontend_cues"] == "0",
                "CD02 must start once before language, with no invented title cue")
        xdo("key", "Up")
        wait_for(lambda: rows()[-1]["selection"] == "2", "wrap to third visible language")
        xdo("key", "KP_Enter")
        wait_for(lambda: rows()[-1]["screen"] == "main_menu", "confirm German main menu with keypad Enter")
        require(rows()[-1]["language"] == "2", "language selection changed")

        def action(key, screen, **values):
            before = int(rows()[-1]["frontend_cues"])
            xdo("key", key)
            wait_for(lambda: rows()[-1]["screen"] == screen
                     and int(rows()[-1]["frontend_cues"]) > before
                     and all(rows()[-1][field] == str(value) for field, value in values.items()),
                     f"{key} -> {screen} {values}")

        def capture_menu(name):
            wait_for(lambda: (row := rows()[-1]) and int(row["menu_animation_ns"]) - int(row["menu_entered_ns"]) >= 466_666_667,
                     "menu turn settles before static artwork comparison")
            capture_window(window, name)

        # Explicit native Extras replaces the temporary all-content bypass.
        action("Up", "main_menu", selection=4)
        action("Up", "main_menu", selection=3)
        action("Return", "extras", selection=0)
        action("Return", "extras_unlocks", selection=0)
        action("Right", "extras_unlocks", extras_all_tracks=1, extras_bonus_track=1)
        action("Escape", "extras", selection=0)
        action("Escape", "main_menu", selection=3)
        action("Down", "main_menu", selection=4)
        action("Down", "main_menu", selection=0)
        action("Return", "race_menu", selection=0)
        action("Down", "race_menu", selection=1)
        action("Return", "championship_menu", selection=0)
        action("Escape", "race_menu", selection=1)
        action("Up", "race_menu", selection=0)
        action("Return", "single_options", selection=4)
        action("Up", "single_options", selection=3)
        action("Up", "single_options", selection=2)
        action("Up", "single_options", selection=1)
        action("Right", "single_options", laps=5)
        action("Down", "single_options", selection=2)
        action("Down", "single_options", selection=3)
        action("Down", "single_options", selection=4)
        capture_menu("single-options.png")
        action("Return", "difficulty", selection=1)
        action("Down", "difficulty", selection=2)
        action("Return", "name_entry")
        xdo("type", "--delay", "50", "daniel")
        wait_for(lambda: rows()[-1]["player_name"] == "DANIEL", "first typed name replaces the default and becomes uppercase")
        action("BackSpace", "name_entry", player_name="DANIE")
        xdo("type", "l")
        wait_for(lambda: rows()[-1]["player_name"] == "DANIEL", "append to edited name")
        capture_menu("name-entry.png")
        action("Return", "car_selection", car_id=0, manual_transmission=0)

        capture_menu("honda.png")
        action("Right", "car_selection", car_id=1)
        action("Up", "car_selection", manual_transmission=1)
        action("Return", "track_selection", car_id=1, stored_car_id=1, track_id=0, track_available=1)
        action("Right", "track_selection", track_id=1, track_available=1)
        capture_menu("track1.png")
        for track in range(2, 10):
            action("Right", "track_selection", track_id=track, track_available=1)
        capture_menu("track9-lavaland.png")
        action("Escape", "car_selection", car_id=1, stored_track_id=9)
        action("Return", "track_selection", track_id=9, stored_track_id=9)
        for track in range(8, -1, -1):
            action("Left", "track_selection", track_id=track, track_available=1)
        baseline = None
        # Leave room for the private window manager's border inside Xvfb.
        for width, height in ((640, 480), (960, 540), (960, 600), (1120, 480)):
            xdo("windowsize", "--sync", window, width, height)
            wait_for(lambda: rows()[-1]["width"] == str(width) and rows()[-1]["height"] == str(height), "track drawable resize")
            target = Path(f"track0-{width}x{height}.png")
            capture_menu(target)
            actual_width, actual_height, pixels = read_png(target)
            require((actual_width, actual_height) == (width, height), "track capture extent differs from drawable")
            if width == 640:
                baseline = pixels
                require(len(set(pixels)) > 50, "track resource canvas is empty")
            else:
                scale, left = height / 480, (width - 640 * height / 480) / 2
                for begin, end in ((0, int(left)), (width - int(left), width)):
                    colors = {pixels[y * width + x] for y in range(0, height, 3) for x in range(begin, end, 3)}
                    require(len(colors) > 50, f"track side band lacks background detail at {width}x{height}")
                    outer = range(begin, min(begin + 4, end)) if begin == 0 else range(max(begin, end - 4), end)
                    require(len({pixels[y * width + x] for y in range(0, height, 3) for x in outer}) > 12,
                            f"track background does not reach the outer edge at {width}x{height}")
                # Static track artwork excludes animated selection indicators.
                for sy in (25, 34, 42, 345, 355, 375, 390, 440, 446):
                    for sx in range(55, 470, 11):
                        x, y = int(left + (sx + 0.5) * scale), int((sy + 0.5) * scale)
                        require(pixels[y * width + x] == baseline[sy * 640 + sx], "track artwork lost uniform logical scaling")
        xdo("windowsize", "--sync", window, 640, 480)
        wait_for(lambda: rows()[-1]["width"] == "640" and rows()[-1]["height"] == "480", "restore baseline track size")
        target = Path("track0-rotated.png")
        rotation = rows()[-1]["track_rotation"]
        wait_for(lambda: rows()[-1]["track_rotation"] != rotation, "elapsed-time preview rotation")
        time.sleep(0.15)
        capture_menu(target)
        _, _, rotated = read_png(target)
        differences = sum(rotated[y * 640 + x] != baseline[y * 640 + x] for y in range(102, 302) for x in range(151, 471))
        require(differences > 100, "rotating original track geometry did not reach GPU readback")
        require(rows()[-1]["music_playing"] == "1" and rows()[-1]["music_starts"] == "1", "track selection must preserve CD02 playback")
        action("Escape", "car_selection", car_id=1, stored_track_id=0)
        action("Escape", "name_entry", player_name="DANIEL")
        action("Return", "car_selection", car_id=1, stored_car_id=1, manual_transmission=1)
        action("Escape", "name_entry", player_name="DANIEL")
        action("Escape", "difficulty", selection=2)
        action("Escape", "single_options", selection=4, laps=5)
        action("Escape", "race_menu", selection=0)
        action("Escape", "main_menu", selection=0)
        before = rows()[-1]["frontend_cues"]
        xdo("key", "Escape")
        time.sleep(0.08)
        require(rows()[-1]["screen"] == "main_menu" and rows()[-1]["frontend_cues"] == before, "main back-state must be itself")
        action("Up", "main_menu", selection=4)
        xdo("key", "Return")
        require(application.wait(timeout=5) == 0, "original Quit must exit directly")
        current = trace_reader.rows()
        require(current[-1]["music_starts"] == "1" and current[-1]["music_playing"] == "0" and current[-1]["music_track"] == "0",
                "frontend shutdown must stop CD02")
        require(all(row["music_track"] == "2" and row["music_starts"] == "1" for row in current
                    if row["screen"] in ("race_menu", "single_options", "difficulty", "name_entry", "car_selection", "track_selection")),
                "selection route must not restart or switch menu music")
        require(any(int(row["audio_voices"]) > 0 for row in current), "menu cues did not reach the real mixer voices")
        print("PASS: default three-second copyright hold, car/track commit and Back, all ten available tracks, rotating preview at three aspects, cue voices and continuous CD02")
    finally:
        for process in (application, manager):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)


def scene(track=0):
    initial_detail = 0 if track == 0 else 3
    music = (3, 4, 5, 6, 3, 4, 5, 6, 3, 4)[track]
    qualifying_xz = ((-7749269, -9039511), (-25638838, 9972735), (-28698342, -28680451), (-20411506, -7437131),
                     (41121659, -15104501), (-4908848, -9493710), (1157677, -15352300), (-27443439, 12460271),
                     (13831482, -6883645), (8397069, 1453390))[track]
    grid_xz = ((-9642299, -5298024), (-16697834, 23754680), (-36466633, -24871634), (-10585580, 5233159),
               (28261650, -14476253), (-9642299, -5298024), (4946883, -8859174), (-10136293, 11532203),
               (-2652656, -11039975), (2916107, -677912))[track]
    cloudy = track in (1, 4, 5, 7)
    weather = 2 if cloudy or track == 9 else 1
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required for private scene acceptance")
    with Path("scene-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    application = None

    try:
        wait_for(window_manager_ready, "scene window manager")
        with Path("scene.log").open("w") as log:
            application = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "scene.cfg",
                                             "--resolution", "640x480", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "60",
                                             "--audio-offline", "--trace", "scene.csv"], stdout=log, stderr=subprocess.STDOUT)
        trace = Path("scene.csv")
        trace_reader = TraceReader(trace)
        tick_reader = TraceReader(Path(str(trace) + ".simulation.csv"))

        def rows():
            require(application.poll() is None, "scene application exited early; see scene.log")
            return trace_reader.rows()

        wait_for(lambda: rows(), "first scene application trace", timeout=8)
        window = xdo("search", "--onlyvisible", "--pid", application.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            before = int(rows()[-1]["poll"])
            xdo("key", key)
            return wait_for(lambda: current[-1] if (current := rows()) and int(current[-1]["poll"]) > before
                            and current[-1]["screen"] == screen
                            and all(current[-1][field] == str(value) for field, value in values.items()) else None,
                            f"{key} -> {screen} {values}", timeout=40 if screen == "scene" else 10)

        def set_detail(value):
            action("Down", "main_menu", selection=1)
            action("Return", "settings", selection=0)
            action("Down", "settings", selection=1)
            action("Return", "graphics_options", selection=0)
            for selection in range(1, 6):
                action("Down", "graphics_options", selection=selection)
            for _ in range(5):
                if rows()[-1]["draft_graphics_detail"] == str(value):
                    break
                next_detail = (int(rows()[-1]["draft_graphics_detail"]) + 1) % 5
                action("Right", "graphics_options", draft_graphics_detail=next_detail)
            action("Up", "graphics_options", selection=4)
            action("Return", "graphics_options", graphics_detail=value, settings_error=0)
            action("Escape", "settings", selection=1)
            action("Escape", "main_menu", selection=1)
            action("Up", "main_menu", selection=0)

        action("space", "language")
        action("Return", "main_menu")
        if track == 0:
            set_detail(initial_detail)
        if track > 1:
            action("Up", "main_menu", selection=4)
            action("Up", "main_menu", selection=3)
            action("Return", "extras", selection=0)
            action("Return", "extras_unlocks", selection=0)
            action("Right", "extras_unlocks", extras_all_tracks=1, extras_bonus_track=1)
            action("Escape", "extras", selection=0)
            action("Escape", "main_menu", selection=3)
            action("Down", "main_menu", selection=4)
            action("Down", "main_menu", selection=0)
        for screen in ("race_menu", "single_options", "difficulty", "name_entry", "car_selection", "track_selection"):
            action("Return", screen)
        for selected in range(1, track + 1):
            action("Right", "track_selection", track_id=selected, track_available=1)
        entered = action("Return", "scene", track_id=track, stored_track_id=track, stored_car_id=0, qualifying=1, music_track=music, music_starts=2)
        initial = next(row for row in rows() if row["screen"] == "scene" and row["simulation_steps"] == "0")
        require(initial["scene_detail"] == str(initial_detail), "Scene must consume the applied original graphics detail")
        qualifying_position = tuple(int(initial[key]) for key in ("body_x", "body_y", "body_z"))
        require(qualifying_position[0::2] == qualifying_xz,
                "Qualifying scene did not consume original start XZ")
        camera_start = (2218, 5006, -682, 1024, 1024, 1024, 3072, 512, 1024, 1024)[track]
        require(abs(int(initial["camera_heading"]) - camera_start) <= 22,
                "Qualifying must initialize the authored map camera angle before its first State4 follow step")
        require(int(initial["cone_count"]) > 0, "Scene must load its authored track cones")
        require((int(initial["post_count"]) > 0 and int(initial["sign_count"]) > 0) if track != 9
                else initial["post_count"] == initial["sign_count"] == "0", "Post/sign selection must preserve Lavaland's original skip")
        require((int(initial["flag_count"]) > 0) == (initial_detail != 0 and track not in (1, 3)),
                "Flag controllers must preserve empty Silverstone and the original Brands hierarchy")
        check_horizon_selection(initial, track)
        loading_rows = [row for row in rows() if row["screen"] == "scene_loading"]
        require({row["loading_step"] for row in loading_rows} == {"0", "1", "2"}
                and all(row["music_track"] == "0" and row["music_playing"] == "0" and row["audio_voices"] == "0" for row in loading_rows),
                "Loading must present completed resource stages after releasing CD02 and frontend samples")
        require(int(entered["scene_vertices"]) > 100 and entered["music_playing"] == "1", "Scene geometry or selected race music did not start")
        require(initial["ai_controlled"] == "1" and initial["session_state"] == "4",
                "Qualifying must begin with AI ownership")
        if track == 0:
            wait_for(lambda: int(rows()[-1]["simulation_steps"]) >= 5, "automatic approach presentation")
            capture_window(window, "scene-qualifying-rollout.png")
            xdo("windowminimize", window)
            paused = wait_for(lambda: row if (row := rows()[-1])["scene_active"] == "0"
                              and row["audio_voices"] == "0" else None, "pause automatic approach and audio")
            time.sleep(.18)
            require(rows()[-1]["simulation_steps"] == paused["simulation_steps"]
                    and all(rows()[-1][field] == paused[field] for field in
                            ("flag_updates", "flag_frame", "horizon_updates", "horizon_x", "horizon_y", "horizon_z")),
                    "Inactive time must not advance AI approach, flags or moving HOR actors")
            xdo("windowmap", window)
            xdo("windowactivate", "--sync", window)
            wait_for(lambda: rows()[-1]["scene_active"] == "1" and int(rows()[-1]["audio_voices"]) >= 4,
                     "resume automatic approach and driving sounds")
        qualifying_rollout(tick_reader)
        check_flag_animation(tick_reader.rows())
        check_horizon_animation(tick_reader.rows())
        if track != 0:
            default_weather, default_horizon = ((2, 1), (5, 4), (2, 2), (2, 1), (1, 0), (3, 3), (2, 2), (1, 0), (2, 2), (1, 0))[track]
            require(entered["resolved_weather"] == str(default_weather) and entered["horizon_index"] == str(default_horizon),
                    "Selected track must resolve to its original default weather and horizon")
            wait_for(lambda: tick_reader.rows(), "track simulation startup")
            before = tick_reader.rows()[-1]
            xdo("keydown", "Up")
            wait_for(lambda: int(tick_reader.rows()[-1]["step"]) >= int(before["step"]) + 30,
                     "track acceleration")
            xdo("keyup", "Up")
            moving = tick_reader.rows()[-1]
            acceleration = [row for row in tick_reader.rows() if int(row["step"]) > int(before["step"])]
            # An automatic shift can silence the engine layer on the final
            # sampled tick; inspect playback over the actual acceleration span.
            require(int(moving["speed"]) > int(before["speed"])
                    and any(int(row["throttle"]) > 0 and int(row["car_volume"]) > 0 for row in acceleration),
                    "Gas must accelerate the car and drive its original motor layers")
            require(any(abs(int(moving[field]) - int(before[field])) > 32768 for field in ("body_x", "body_z")),
                    "Wheel forces did not move the car through the selected scene")
            xdo("keydown", "Left")
            wait_for(lambda: int(tick_reader.rows()[-1]["steering_angle"]) < 0, "track steering")
            xdo("keyup", "Left")
            xdo("keydown", "Down")
            wait_for(lambda: int(tick_reader.rows()[-1]["speed"]) < int(moving["speed"]), "track braking")
            xdo("keyup", "Down")
            require(all(row["motion_boundary"] == "0" for row in tick_reader.rows()),
                    "Short drive left supported original ground")
        for width, height in ((640, 480), (960, 540), (960, 600)):
            xdo("windowsize", "--sync", window, width, height)
            wait_for(lambda: rows()[-1]["width"] == str(width) and rows()[-1]["height"] == str(height), "scene drawable resize")
            target = Path(f"scene-{width}x{height}.png")
            capture_window(window, target)
            actual_width, actual_height, pixels = read_png(target)
            require((actual_width, actual_height) == (width, height) and len(set(pixels)) > 100,
                    "Scene must render actual textured drawable pixels at every aspect")
        leave_scene(lambda: rows()[-1], music_track=2, music_starts=3, language=0)
        if track == 0:
            set_detail(3)
        for screen in ("race_menu", "single_options"):
            action("Return", screen)
        action("Up", "single_options", selection=3)
        action("Up", "single_options", selection=2)
        action("Left", "single_options", qualifying=0)
        action("Down", "single_options", selection=3)
        action("Right", "single_options", weather=1)
        if weather == 2:
            action("Right", "single_options", weather=2)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection"):
            action("Return", screen)
        action("Right", "car_selection", car_id=1)
        action("Return", "track_selection", stored_car_id=1, track_id=track)
        entered = action("Return", "scene", stored_car_id=1, stored_track_id=track, qualifying=0, weather=weather, music_track=music, music_starts=4)
        if cloudy:
            require(entered["resolved_weather"] == "2" and entered["horizon_index"] in ("1", "2"),
                    "Cloudy tracks must load one of the two original randomized horizons")
        elif track == 9:
            require(entered["resolved_weather"] == "1" and entered["horizon_index"] == "0",
                    "Lavaland must ignore requested cloudy weather and keep its original LAVA03 horizon")
        initial = next(row for row in rows() if row["screen"] == "scene" and row["stored_car_id"] == "1" and row["simulation_steps"] == "0")
        require(initial["scene_detail"] == "3", "Scene re-entry must consume the current graphics detail")
        if track == 0:
            require(int(initial["flag_count"]) > 0, "Very high detail must restore flags after low-detail qualifying")
        check_horizon_selection(initial, track)
        require(initial["horizon_updates"] == "0", "A newly loaded grid scene must initialize the background actors")
        race_position = tuple(int(initial[key]) for key in ("body_x", "body_y", "body_z"))
        require(race_position[0::2] == grid_xz and race_position != qualifying_position,
                "Race re-entry must consume selected setup and original last grid slot")
        capture_window(window, "scene-audi-race.png")
        leave_scene(lambda: rows()[-1], music_track=2, music_starts=5)
        action("Up", "main_menu", selection=4)
        xdo("key", "Return")
        require(application.wait(timeout=5) == 0, "Scene return must leave normal Quit usable")
        final = trace_reader.rows()[-1]
        require(final["music_track"] == "0" and final["music_playing"] == "0" and final["audio_voices"] == "0",
                "Scene/application shutdown leaked active audio")
        print(f"PASS: track{track}: menu -> loading -> selected scene at three aspects -> menu; CD02/{music:02} handoff and race re-entry")
    finally:
        for process in (application, manager):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)


if __name__ == "__main__":
    executable, data_root, mode = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "native acceptance requires the isolated dummy audio output")
    {"aspects": aspects, "pacing": pacing, "interaction": interaction, "frontend": frontend,
     "scene": scene, "silverstone": lambda: scene(1), "thruxton": lambda: scene(2), "brands-hatch": lambda: scene(3),
     "oulton-park": lambda: scene(4), "donington-short": lambda: scene(5), "croft": lambda: scene(6),
     "knockhill": lambda: scene(7), "snetterton": lambda: scene(8), "lavaland": lambda: scene(9)}[mode]()
