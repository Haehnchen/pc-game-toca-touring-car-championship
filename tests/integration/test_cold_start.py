"""Enter the first default race with fresh storage and a real muted audio device."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from native_support import TraceReader, require, wait_for, window_manager_ready, xdo


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


def run(executable):
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy" and os.environ.get("SDL_VIDEODRIVER") == "x11"
            and "WAYLAND_DISPLAY" not in os.environ, "Cold startup requires an isolated, muted X11 runtime")
    for root in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
        require(root in os.environ and not any(Path(os.environ[root]).iterdir()), "Cold startup requires fresh user storage and caches")
    for tool in ("xdotool", "openbox", "xprop"):
        require(shutil.which(tool), f"{tool} required for private cold-start acceptance")
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    deadline = time.monotonic() + 80
    application = manager = None
    reader = TraceReader(Path("cold-start.csv"))

    def current():
        require(application.poll() is None, "Cold-start application exited early; see cold-start.log")
        rows = reader.rows()
        return rows[-1] if rows else {}

    def wait(description, predicate, timeout=8):
        return wait_for(predicate, description, timeout=min(timeout, max(.05, deadline - time.monotonic())))

    def action(key, **values):
        before = int(current()["poll"])
        xdo("key", key)
        return wait(f"{key}: {values}", lambda: (row := current()) and int(row["poll"]) > before
                    and all(row.get(field) == str(value) for field, value in values.items()) and row)

    try:
        with Path("cold-start-wm.log").open("w") as log:
            manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
        wait("private cold-start window manager", window_manager_ready)
        with Path("cold-start.log").open("w") as log:
            # Exercise normal defaults, including live MIX_CreateMixerDevice output.
            # Diagnostic stepping/offline audio and CLI preference overrides bypass this path.
            application = subprocess.Popen([executable, "--trace", "cold-start.csv"], stdout=log, stderr=subprocess.STDOUT)
        wait("cold default title", lambda: (row := current()) and row["screen"] == "title" and row, timeout=12)
        window = xdo("search", "--onlyvisible", "--pid", application.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)
        initial = wait("focused default fullscreen", lambda: (row := current()) and row["focused"] == "1" and row)
        # Fullscreen selects the closest available mode; the standard private
        # Xvfb display exposes 1280x960 rather than the requested default 1280x720.
        require(initial["fullscreen"] == "1" and initial["fps_limit"] == "120" and initial["vsync"] == "1" and initial["graphics_detail"] == "4",
                "Cold launch must retain default fullscreen, Mega, VSync and FPS120")
        require(initial["audio_active"] == "1", "Cold launch must initialize its live muted audio device")
        action("space", screen="language")
        for screen in ("main_menu", "race_menu", "single_options", "difficulty", "name_entry", "car_selection", "track_selection"):
            action("Return", screen=screen)
        xdo("key", "Return")
        entered = wait("first default qualifying scene", lambda: (row := current()) and row["screen"] == "scene"
                       and row["presented"] == "1" and row, timeout=60)
        history = reader.rows()
        loading = [row for row in history if row["screen"] == "scene_loading" and row["presented"] == "1"]
        require(loading and all(row["music_track"] == "0" and row["music_playing"] == "0" and row["audio_voices"] == "0" for row in loading),
                "First scene preparation must present loading frames before starting race music or driving voices")
        preview = next(row for row in history if row["screen"] == "scene" and row["presented"] == "1")
        require(preview["simulation_steps"] == "0", "First race presentation must precede the first scheduled physical update")
        require(entered["scene_detail"] == "4" and entered["qualifying"] == "1" and entered["scene_active"] == "1",
                "First race must consume default Mega resources and normal qualifying ownership")
        wait("first scene keeps simulating and presenting", lambda: (row := current()) and row["screen"] == "scene"
             and row["presented"] == "1" and int(row["frame"]) >= int(entered["frame"]) + 8
             and int(row["simulation_steps"]) >= int(entered["simulation_steps"]) + 4 and row)
        paused = action("Escape", screen="scene", pause_active=1, scene_active=0)
        still_paused = wait("paused scene continues presenting", lambda: (row := current()) and row["presented"] == "1"
                           and int(row["frame"]) >= int(paused["frame"]) + 8 and row)
        require(still_paused["simulation_steps"] == paused["simulation_steps"], "Pause response must stop simulation while presentation continues")
        resumed = action("Escape", screen="scene", pause_active=0, scene_active=1)
        wait("first race resumes normal simulation", lambda: (row := current()) and row["presented"] == "1"
             and int(row["simulation_steps"]) >= int(resumed["simulation_steps"]) + 3 and row)
        xdo("key", "alt+F4")
        require(application.wait(timeout=5) == 0, "Cold-start race must retain normal window-close handling")
        print("PASS: fresh default Mega/fullscreen/VSync race, live dummy audio, continuous scene presentation and responsive Pause")
    finally:
        stop(application)
        stop(manager)


if __name__ == "__main__":
    run(*sys.argv[1:])
