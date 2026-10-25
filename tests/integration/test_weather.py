"""Select original weather through the normal menu and inspect active deadlines."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from native_support import capture_window, TraceReader, read_png, require, wait_for, window_manager_ready, xdo


def run(executable, data_root, weather):
    weather = int(weather)
    particles = {3: 90, 4: 25, 5: 10}[weather]
    trace = TraceReader("weather.csv")
    ticks = TraceReader("weather.csv.simulation.csv")
    with Path("weather.log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "weather.cfg",
                                "--resolution", "640x480", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", str({3: 15, 4: 60, 5: 30}[weather]),
                                "--audio-offline", "--trace", "weather.csv"], stdout=log, stderr=subprocess.STDOUT)

    def rows():
        require(app.poll() is None, "Application exited; see weather.log")
        return trace.rows()

    try:
        wait_for(rows, "weather startup", timeout=25)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            previous = int(rows()[-1]["poll"])
            xdo("key", key)
            return wait_for(lambda: row if (row := rows()[-1]) and int(row["poll"]) > previous
                            and row["screen"] == screen and all(row[field] == str(value) for field, value in values.items())
                            else None, f"{key}: {screen} {values}", timeout=12)

        action("space", "language")
        for screen in ("main_menu", "race_menu", "single_options"):
            action("Return", screen)
        action("Up", "single_options", selection=3)
        for choice in range(1, weather + 1):
            action("Right", "single_options", weather=choice)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            action("Return", screen)
        require(rows()[-1]["resolved_weather"] == str(weather), "Menu weather must reach the actual scene")
        wait_for(lambda: int(rows()[-1]["weather_updates"]) >= 25, "active precipitation updates")
        history = ticks.rows()
        require(all(int(row["weather_particles"]) == particles for row in history), "Scene must retain the original precipitation pool size")
        require(all(int(row["weather_updates"]) == int(row["step"]) for row in history), "Particles must advance once per active simulation deadline")
        require(len({(row["weather_first_x"], row["weather_first_y"], row["weather_first_z"]) for row in history}) > 10,
                "Camera-relative precipitation must move during automatic qualifying approach")
        for width, height in ((640, 480), (960, 540), (960, 600)):
            xdo("windowsize", "--sync", window, width, height)
            wait_for(lambda: rows()[-1]["width"] == str(width) and rows()[-1]["height"] == str(height)
                     and rows()[-1]["presented"] == "1", "weather aspect presentation")
            path = Path(f"weather-{weather}-{width}x{height}.png")
            capture_window(window, path)
            actual_width, actual_height, pixels = read_png(path)
            require((actual_width, actual_height) == (width, height) and len(set(pixels)) > 100,
                    "Capture must contain the actual weather scene at the selected aspect")
        if weather == 5:
            thunder = wait_for(lambda: row if int((row := rows()[-1])["thunder_plays"]) >= 1 else None,
                               "delayed original thunder", timeout=8)
            require(thunder["thunder_sample"] in ("76", "77"), "Storm must play an original thunder variant")
            require(any(int(row["weather_lightning"]) > 0 for row in ticks.rows()), "Storm must expose its original lightning sequence")
        paused_menu = action("Escape", "scene", pause_active=1)
        time.sleep(.25)
        require(rows()[-1]["weather_updates"] == paused_menu["weather_updates"], "In-game pause must freeze weather")
        action("Escape", "scene", pause_active=0)
        wait_for(lambda: int(rows()[-1]["weather_updates"]) > int(paused_menu["weather_updates"]), "weather after pause menu")
        xdo("windowminimize", window)
        paused = wait_for(lambda: row if (row := rows()[-1])["scene_active"] == "0" and row["audio_voices"] == "0"
                          else None, "weather focus pause")
        time.sleep(.25)
        frozen = rows()[-1]
        require(all(frozen[field] == paused[field] for field in
                    ("weather_updates", "weather_first_x", "weather_first_y", "weather_first_z", "weather_lightning", "thunder_plays")),
                "Inactive weather must freeze particles, lightning and thunder")
        xdo("windowmap", window)
        xdo("windowactivate", "--sync", window)
        wait_for(lambda: int(rows()[-1]["weather_updates"]) > int(paused["weather_updates"]) + 5, "weather resume")
        xdo("key", "alt+F4")
        require(app.wait(timeout=5) == 0, "Weather flow failed to exit cleanly")
        history = ticks.rows()
        require(all(int(row["weather_updates"]) == int(row["step"]) for row in history), "Resize and focus resume must preserve update cadence")
        print(f"PASS: menu weather {weather}, original {particles}-particle pool, moving camera, three aspects, focus freeze/resume and clean exit")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Weather checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("weather-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private window manager")
        run(*sys.argv[1:])
    finally:
        manager.terminate()
        try:
            manager.wait(timeout=2)
        except subprocess.TimeoutExpired:
            manager.kill()
            manager.wait(timeout=2)
