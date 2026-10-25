"""Hit an authored Donington prop through the normal grid and keyboard route."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from native_support import capture_window, leave_scene, TraceReader, read_png, require, wait_for, window_manager_ready, xdo


def run(executable, data_root):
    trace = TraceReader("props.csv")
    ticks = TraceReader("props.csv.simulation.csv")
    with Path("props.log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "props.cfg",
                                "--resolution", "960x540", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "30",
                                "--audio-offline", "--trace", "props.csv"], stdout=log, stderr=subprocess.STDOUT)

    def rows():
        require(app.poll() is None, "Application exited; see props.log")
        return trace.rows()

    try:
        # Instrumented startup includes first-use GPU/resource preparation.
        wait_for(rows, "prop-flow startup", timeout=20)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)

        def action(key, screen, **values):
            previous = int(rows()[-1]["poll"])
            xdo("key", key)
            return wait_for(lambda: row if (row := rows()[-1]) and int(row["poll"]) > previous
                            and row["screen"] == screen and all(row[field] == str(value) for field, value in values.items())
                            else None, f"{key}: {screen} {values}", timeout=10)

        action("space", "language")
        for screen in ("main_menu", "race_menu", "single_options"):
            action("Return", screen)
        action("Up", "single_options", selection=3)
        action("Up", "single_options", selection=2)
        action("Left", "single_options", qualifying=0)
        action("Down", "single_options", selection=3)
        action("Down", "single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            action("Return", screen)
        require(rows()[-1]["stored_track_id"] == "0", "Prop route must use the original Donington grid")
        # Hold throttle and Left through the grid sequence, then release Left
        # with the clutch to reach the FREE cone row without turning into the wall.
        xdo("keydown", "Up", "Left")
        released = wait_for(lambda: next((row for row in ticks.rows()
                                          if row["session_state"] == "6" and row["start_held"] == "0"), None),
                            "prop-flow grid release", timeout=15)
        xdo("keyup", "Left")

        def audible_contact():
            current = ticks.rows()
            return next((row for before, row in zip(current, current[1:])
                         if int(row["step"]) >= int(released["step"])
                         and int(row["step"]) == int(before["step"]) + 1
                         and int(row["prop_feedback_q16"]) > 0
                         and int(row["prop_impact_plays"]) > int(before["prop_impact_plays"])), None)

        hit = wait_for(audible_contact, "local authored prop contact, vibration and new impact playback", timeout=15)
        xdo("keyup", "Up")
        require(int(hit["gamepad_rumble_low"]) > 0 and int(hit["gamepad_rumble_high"]) > 0,
                "A real local prop contact must feed both requested gamepad motors in the same simulation tick")
        require(int(hit["prop_impact_sample"]) == 45, "The authored cone contact must play the original CONE sample")
        require(all(row["motion_boundary"] == "0" for row in ticks.rows()), "Prop driving reached unsupported ground coverage")
        frame = int(rows()[-1]["frame"])
        wait_for(lambda: (row := rows()[-1])["presented"] == "1" and int(row["frame"]) > frame + 2, "moving prop presentation")
        capture_window(window, "props-contact.png")
        width, height, pixels = read_png(Path("props-contact.png"))
        require((width, height) == (960, 540) and len(set(pixels)) > 100, "Prop capture must contain the actual game scene")

        xdo("windowminimize", window)
        paused = wait_for(lambda: row if (row := rows()[-1])["scene_active"] == "0" else None, "prop focus pause")
        require(paused["gamepad_rumble_low"] == "0" and paused["gamepad_rumble_high"] == "0",
                "Focus loss must stop the requested gamepad vibration along with the prop simulation")
        deadline = time.monotonic() + .25
        wait_for(lambda: time.monotonic() >= deadline, "inactive interval")
        frozen = rows()[-1]
        require(frozen["prop_updates"] == paused["prop_updates"] and frozen["prop_contacts"] == paused["prop_contacts"],
                "Focus loss must freeze prop movement and contacts")
        xdo("windowmap", window)
        xdo("windowactivate", "--sync", window)
        wait_for(lambda: int(rows()[-1]["prop_updates"]) > int(paused["prop_updates"]), "prop resume")
        returned = leave_scene(lambda: rows()[-1])
        require(returned["music_track"] == "2", "Prop scene must restore menu music")
        xdo("key", "alt+F4")
        require(app.wait(timeout=5) == 0, "Prop flow failed to exit cleanly")
        print(f"PASS: normal grid drive hit an original prop at tick {hit['step']}, sample {hit['prop_impact_sample']}; focus pause and menu return")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Prop checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("props-wm.log").open("w") as log:
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
