"""Reach a real Donington barrier through the normal menu and keyboard path."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

from native_support import capture_window, leave_scene, TraceReader, qualifying_rollout, read_png, require, wait_for, window_manager_ready, xdo


def run(executable, data_root):
    trace = Path("contacts.csv")
    ticks = Path(str(trace) + ".simulation.csv")
    trace_reader, tick_reader = TraceReader(trace), TraceReader(ticks)
    with Path("contacts.log").open("w") as log:
        app = subprocess.Popen([executable, "--fullscreen", "off", "--data-root", data_root, "--config", "contacts.cfg",
                                "--resolution", "960x540", "--graphics-detail", "3", "--vsync", "off", "--fps-limit", "30",
                                "--audio-offline", "--trace", str(trace)], stdout=log, stderr=subprocess.STDOUT)

    def rows():
        require(app.poll() is None, "Application exited; see contacts.log")
        return trace_reader.rows()

    try:
        wait_for(lambda: rows(), "contact-flow startup", timeout=8)
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
        for screen in ("main_menu", "race_menu", "single_options", "difficulty", "name_entry", "car_selection"):
            action("Return", screen)
        action("Down", "car_selection", manual_transmission=1)  # Source manual Honda, qualifying enabled.
        action("Return", "track_selection")
        entered = action("Return", "scene")
        require(entered["qualifying"] == "1" and entered["stored_car_id"] == "0"
                and entered["manual_transmission"] == "1", "Contact flow must use the normal Honda qualifying start")
        require(entered["hud_lap"] == "1" and entered["hud_lap_limit"] == "3" and entered["hud_best_lap_ms"] == "0",
                "Qualifying HUD must expose three measured laps without inventing a best time")
        # Let the original AI approach finish before applying player controls.
        qualifying_rollout(tick_reader)
        xdo("keydown", "Up", "Left")
        impact = wait_for(lambda: row if (current := tick_reader.rows()) and (row := current[-1])
                          and int(row["impacts"]) > 0 and int(row["impact_plays"]) > 0
                          and int(row["damage_parts"]) > 0 else None,
                          "actual barrier damage and audible source impact event", timeout=12)
        scrape = wait_for(lambda: row if (current := tick_reader.rows()) and (row := current[-1])
                          and int(row["contact_sparks"]) > 0 and row["scrape_playing"] == "1"
                          and int(row["scrape_volume"]) > 0 else None,
                          "real wall sparks and original scrape playback", timeout=12)
        xdo("keyup", "Up", "Left")
        states = tick_reader.rows()
        require(all(row["motion_boundary"] == "0" for row in states), "Supported barrier drive reached missing ground coverage")
        require(any(int(row["barrier_contacts"]) > 0 for row in states), "Impact audio lacked a real barrier contact")
        require(any(int(row["wheel_constant_commands"]) > 0 and int(row["wheel_impact_magnitude"]) > 0
                    and (abs(int(row["wheel_impact_x"])) == 100 or abs(int(row["wheel_impact_y"])) == 100)
                    for row in states),
                "Real local damage must reach the original directional wheel impulse consumer without attached hardware")
        require(any(int(row["skid_volume"]) > 0 and 39 <= int(row["skid_sample"]) <= 49
                    and any(abs(int(row[f"longitudinal_slip_{wheel}"])) + abs(int(row[f"lateral_slip_{wheel}"])) > 0x4ccc
                            for wheel in range(4)) for row in states),
                "The turning car must play original tire feedback from actual wheel slip before the wall impact")
        require(25 <= int(impact["impact_sample"]) <= 36 and int(impact["impact_volume"]) > 0,
                "Barrier event must use an original finite impact sample with audible gain")
        require(any(int(row["impact_plays"]) > 0 and int(row["impact_pan"]) != 64 for row in states)
                and any(row["scrape_playing"] == "1" and int(row["scrape_pan"]) != 64 for row in states),
                "Actual contact positions and camera direction must reach impact and scrape stereo playback")
        require(int(impact["hud_gear"]) == int(impact["gear"]), "HUD gear must follow the colliding vehicle")
        require(int(impact["damage_sum"]) >= int(impact["damage_parts"]) > 0,
                "A real wall impulse must select visible original damage variants")
        frame = int(rows()[-1]["frame"])
        wait_for(lambda: (row := rows()[-1])["presented"] == "1" and int(row["frame"]) >= frame + 2,
                 "contact and HUD presentation")
        target = Path("contacts-960x540.png")
        capture_window(window, target)
        width, height, pixels = read_png(target)
        require((width, height) == (960, 540) and len(set(pixels)) > 100, "Contact capture must contain the textured scene and HUD")
        paused = action("Escape", "scene", pause_active=1, scene_active=0, scrape_playing=0)
        require(paused["wheel_constant_commands"] == "0" and paused["wheel_sine_commands"] == "0"
                and int(paused["wheel_stop_commands"]) > 0,
                "Pausing must stop both wheel effects and clear their last physical commands")
        frozen = wait_for(lambda: row if (row := rows()[-1]) and int(row["frame"]) >= int(paused["frame"]) + 3
                          else None, "contact effects remain frozen during pause")
        require(all(frozen[field] == paused[field] for field in
                    ("simulation_steps", "contact_fragments", "contact_sparks", "scrape_plays")),
                "Pause must freeze contact particles and stop scrape playback")
        action("Escape", "scene", pause_active=0, scene_active=1)
        returned = leave_scene(lambda: rows()[-1])
        require(returned["music_track"] == "2", "Contact scene must return to menu music")
        action("Up", "main_menu", selection=4)
        xdo("key", "Return")
        require(app.wait(timeout=5) == 0, "Contact flow failed to exit")
        final = trace_reader.rows()[-1]
        require(final["audio_voices"] == "0" and final["music_track"] == "0", "Contact exit leaked sample voices or music")
        print(f"PASS: real barrier contact at tick {impact['step']}, sample {impact['impact_sample']}, "
              f"volume {impact['impact_volume']}, {impact['damage_parts']} damaged parts, "
              f"{scrape['contact_sparks']} sparks, scrape volume {scrape['scrape_volume']}, "
              f"pan {impact['impact_pan']}/{scrape['scrape_pan']}; pause and clean menu return")
    finally:
        if app.poll() is None:
            app.terminate()
            try:
                app.wait(timeout=2)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=2)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Contact checks require isolated dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("contacts-wm.log").open("w") as log:
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
