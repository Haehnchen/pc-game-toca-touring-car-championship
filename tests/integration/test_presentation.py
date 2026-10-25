"""Verify car, aircraft, rain and spray motion between unchanged40ms states."""

import os
from pathlib import Path
import subprocess
import sys
import time

from native_support import TraceReader, require, wait_for, window_manager_ready, xdo


def run(executable, data_root, detail="0"):
    # Debug uses the low-cost profile; pass4 with the optimized game for Mega.
    trace = Path("presentation.csv")
    reader, ticks = TraceReader(trace), TraceReader(Path(str(trace) + ".simulation.csv"))
    with Path("presentation.log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "presentation.cfg",
                                "--resolution", "640x480", "--graphics-detail", detail, "--fps-limit", "120",
                                "--vsync", "off", "--audio-offline", "--trace", str(trace)],
                               stdout=log, stderr=subprocess.STDOUT)

    def current():
        require(app.poll() is None, "Application exited; see presentation.log")
        rows = reader.rows()
        return rows[-1] if rows else None

    def action(key, **expected):
        before = int(current()["poll"])
        xdo("key", key)
        return wait_for(lambda: (row := current()) and int(row["poll"]) > before
                        and all(row[field] == str(value) for field, value in expected.items()) and row,
                        f"{key}: {expected}", timeout=15)

    def position(row):
        return tuple(float(row[f"presentation_body_{axis}"]) for axis in "xyz")

    def aircraft_position(row):
        return tuple(int(row[f"presentation_horizon_{axis}"]) for axis in "xyz")

    def effect_position(row, kind):
        return tuple(int(row[f"presentation_{kind}_{axis}"]) for axis in "xyz")

    try:
        wait_for(current, "presentation startup", timeout=15)
        window = xdo("search", "--onlyvisible", "--pid", app.pid,
                     "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", window)
        action("space", screen="language")
        for screen in ("main_menu", "race_menu", "single_options"):
            action("Return", screen=screen)
        action("Up", screen="single_options", selection=3)
        for weather in (1, 2, 3):
            action("Right", screen="single_options", weather=weather)
        action("Down", screen="single_options", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection", "scene"):
            action("Return", screen=screen)
        require(current()["resolved_weather"] == "3", "Presentation flow must load actual rain and wet-wheel effects")
        # The qualifying approach already drives continuously through the real
        # simulation. Watch actual render inputs, not just a mathematical helper.
        def smooth_pairs():
            rows = [row for row in reader.rows() if row["screen"] == "scene" and row["presented"] == "1"
                    and row["scene_active"] == "1" and row["pause_active"] == "0"]
            return [(a, b) for a, b in zip(rows, rows[1:])
                    if a["simulation_steps"] == b["simulation_steps"] and position(a) != position(b)]

        pairs = wait_for(lambda: (pairs := smooth_pairs()) and len(pairs) >= 8 and pairs,
                         "multiple moving images within unchanged physics ticks", timeout=12)
        for a, b in pairs:
            require(a["scene_detail"] == b["scene_detail"] == detail, "Expected the selected render detail")
            require(all(a[f"body_{axis}"] == b[f"body_{axis}"] for axis in "xyz"),
                    "Presentation must not rewrite the authoritative body")
            require(a["horizon_updates"] == b["horizon_updates"]
                    and all(a[f"horizon_{axis}"] == b[f"horizon_{axis}"] for axis in "xyz"),
                    "Presentation must not rewrite the authoritative aircraft")
            require(aircraft_position(a) != aircraft_position(b),
                    "Aircraft must move between presented frames, just like the car")
            require(0 <= float(a["presentation_alpha"]) < float(b["presentation_alpha"]) <= 1,
                    "Interpolation fraction must advance within one simulation tick")

        def effect_pairs(kind):
            rows = [row for row in reader.rows() if row["screen"] == "scene" and row["presented"] == "1"
                    and row["scene_active"] == "1" and row["pause_active"] == "0"]
            return [(a, b) for a, b in zip(rows, rows[1:])
                    if a["simulation_steps"] == b["simulation_steps"]
                    and (kind != "surface" or int(a["surface_trails"]) > 0 and int(b["surface_trails"]) > 0)
                    and effect_position(a, kind) != (0, 0, 0) and effect_position(b, kind) != (0, 0, 0)
                    and effect_position(a, kind) != effect_position(b, kind)]

        weather_pairs = wait_for(lambda: (found := effect_pairs("weather")) and len(found) >= 8 and found,
                                 "rain particle images between unchanged simulation ticks", timeout=12)
        spray_pairs = wait_for(lambda: (found := effect_pairs("surface")) and len(found) >= 8 and found,
                               "wet-wheel spray images between unchanged simulation ticks", timeout=12)
        for kind, observed, source_fields in (
                ("weather", weather_pairs, ("weather_updates", "weather_particles", "weather_first_x", "weather_first_y", "weather_first_z")),
                ("surface", spray_pairs, ("surface_updates", "surface_particles", "surface_trails", "surface_emitted"))):
            for a, b in observed:
                require(all(a[field] == b[field] for field in source_fields),
                        f"Rendered {kind} movement must retain the same authoritative source state")
                require(0 <= float(a["presentation_alpha"]) < float(b["presentation_alpha"]) <= 1,
                        f"Rendered {kind} movement must use the shared advancing interpolation fraction")
        physical = ticks.rows()
        require(all(int(b["simulation_ns"]) - int(a["simulation_ns"]) == 40_000_000
                    for a, b in zip(physical, physical[1:])), "Rendering changed the 25Hz simulation schedule")
        require(physical and all(row["weather_updates"] == row["step"] == row["surface_updates"] for row in physical),
                "Rain and spray controllers must advance once per source simulation deadline")

        paused = action("Escape", pause_active=1, scene_active=0, presented=1)
        time.sleep(.18)
        frozen = current()
        require(position(paused) == position(frozen) and paused["simulation_steps"] == frozen["simulation_steps"],
                "Paused presentation must stay frozen")
        require(aircraft_position(paused) == aircraft_position(frozen)
                and paused["horizon_updates"] == frozen["horizon_updates"],
                "Paused aircraft presentation must stay frozen")
        for kind in ("weather", "surface"):
            require(effect_position(paused, kind) == effect_position(frozen, kind),
                    f"Paused {kind} render positions must stay frozen")
        require(paused["weather_updates"] == frozen["weather_updates"] and paused["surface_updates"] == frozen["surface_updates"],
                "Pause must freeze source rain and wet-wheel effect updates")
        require(frozen["presentation_alpha"] == "1", "Pause must discard interpolation history")
        resumed = action("Escape", pause_active=0, scene_active=1)
        wait_for(lambda: int(current()["simulation_steps"]) > int(resumed["simulation_steps"]), "resumed physical motion")
        # Resize must apply the new projection without blending camera modes.
        xdo("windowsize", "--sync", window, 800, 450)
        wait_for(lambda: (row := current()) and row["width"] == "800" and row["height"] == "450"
                 and row["presented"] == "1", "resized interpolated scene")
        xdo("key", "alt+F4")
        require(app.wait(timeout=5) == 0, "Presentation flow could not exit normally")
        print(f"PASS: detail{detail} intermediate pairs: {len(pairs)} car/aircraft, {len(weather_pairs)} rain, {len(spray_pairs)} spray; "
              "unchanged40ms states, pause/resume and resize")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Presentation acceptance requires private muted audio")
    with Path("presentation-wm.log").open("w") as log:
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
