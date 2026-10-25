"""Original camera keys, Extras, resize and audio on one private muted display."""

import os
from pathlib import Path
import subprocess
import sys

import test_settings as settings
from test_extras import ExtrasWindow
from native_support import TraceReader, require, wait_for, window_manager_ready, xdo


def flow():
    app = ExtrasWindow("cameras", "camera-profile")
    ticks = TraceReader("cameras.csv.simulation.csv")

    def start_scene(**values):
        xdo("key", "Return")
        # A private display has a fresh driver cache; scene entry includes the
        # first complete GPU frame, rather than only a frontend key response.
        return wait_for(lambda: (row := app.current()) and row["screen"] == "scene"
                        and all(row.get(key) == str(value) for key, value in values.items()) and row,
                        "camera scene entry", timeout=20)

    try:
        app.enter()
        app.car()
        app.key("Down", manual_transmission=1)
        app.key("Return", screen="track_selection")
        start_scene(camera_view=2, engine_camera_view=2)
        xdo("key", "f")
        shifted = app.wait("committed manual gear queues the hand while outside", engine_gear=1, cockpit_shift_phase=1)
        wait_for(lambda: int(app.current()["simulation_steps"]) >= int(shifted["simulation_steps"]) + 3,
                 "outside cockpit retains pending gesture")
        require(app.current()["cockpit_shift_phase"] == "1", "The original hand advances only in cockpit view")

        def key(key, view, **values):
            before = int(app.current()["simulation_steps"])
            xdo("key", key)
            return wait_for(lambda: (row := app.current()) and int(row["simulation_steps"]) > before
                            and row["camera_view"] == str(view)
                            and all(row[field] == str(value) for field, value in values.items()) and row,
                            f"camera {key}: {view} {values}")

        def weather_car(weather):
            app.main()
            app.key("Return", screen="race_menu")
            app.key("Return", screen="single_options")
            app.select(3)
            while int(app.current()["weather"]) != weather:
                app.key("Right")
            app.select(4)
            for screen in ("difficulty", "name_entry", "car_selection"):
                app.key("Return", screen=screen)

        for width, height in ((640, 480), (1280, 720), (1280, 800)):
            for view in (3, 4, 1, 2):
                key("c", view, engine_camera_view=view)
                app.capture(f"camera-{view}-{width}x{height}.png", width, height)
            before = app.current()
            xdo("keydown", "z")
            rear = app.wait("held rear camera", camera_view=5, engine_camera_view=5)
            require(rear["listener_heading"] != before["listener_heading"], "Rear view must rotate the real audio listener")
            app.capture(f"camera-rear-{width}x{height}.png", width, height)
            xdo("keyup", "z")
            app.wait("rear release restores chase", camera_view=2, engine_camera_view=2)

        xdo("keydown", "c")
        held = app.wait("cycle press", camera_view=3)
        wait_for(lambda: int(app.current()["simulation_steps"]) >= int(held["simulation_steps"]) + 8,
                 "held key spans several simulation deadlines")
        require(app.current()["camera_view"] == "3", "Held C must not cycle repeatedly")
        xdo("keyup", "c")
        xdo("keydown", "z")
        app.wait("rear before focus loss", camera_view=5)
        xdo("windowminimize", app.window)
        app.wait("inactive camera restores saved view", scene_active=0, camera_view=3, audio_voices=0)
        xdo("keyup", "z")
        xdo("windowmap", app.window)
        xdo("windowactivate", "--sync", app.window)
        app.wait("camera/audio resume", scene_active=1, camera_view=3, engine_camera_view=3)
        key("F5", 3, camera_trackside=0)  # Disabled without CMFOLLOW.
        xdo("key", "Escape")
        paused = app.wait("pause overlay", pause_active=1)
        xdo("key", "c")
        require(app.current()["camera_view"] == paused["camera_view"], "Pause menu must not change the driving camera")
        xdo("key", "Escape")
        app.wait("resume from pause", pause_active=0)
        key("c", 4)
        key("c", 1, engine_camera_view=1)
        wait_for(lambda: app.current()["cockpit_shift_phase"] == "0", "first shift hand returns to the wheel")
        xdo("key", "f")
        app.wait("second gear moves the actual shifting hand", engine_gear=2, cockpit_shift_phase=1)
        app.capture("camera-shifting-cockpit.png")
        wait_for(lambda: app.current()["cockpit_shift_phase"] == "0", "shifting hand completes reach, hold and return")
        xdo("keydown", "Up")
        moving = wait_for(lambda: (row := app.current()) and int(row["speed"]) > 50000 and row,
                          "physical driving from cockpit", timeout=12)
        xdo("keydown", "Left")
        wait_for(lambda: int(app.current()["simulation_steps"]) >= int(moving["simulation_steps"]) + 4,
                 "moving cockpit steering and head spring")
        xdo("keyup", "Left")
        xdo("keyup", "Up")
        app.capture("camera-moving-cockpit.png")
        require(all(row["motion_boundary"] == "0" for row in ticks.rows()), "Camera keys must not alter driving/contact state")
        app.key("Escape", screen="main_menu")

        for row in (0, 1, 2, 3):
            app.option("camera", row, True)
        app.car()
        app.key("Return", screen="track_selection")
        start_scene(camera_view=3, engine_camera_view=3)
        key("c", 4)
        key("c", 0, camera_trackside=1, camera_render_view=0, engine_camera_view=3)
        app.capture("camera-follow.png")
        key("c", 1, camera_trackside=0, camera_render_view=1, engine_camera_view=1)
        app.capture("camera-chun-handy.png")
        key("F5", 1, camera_trackside=1, camera_render_view=0, engine_camera_view=3)
        key("F5", 1, camera_trackside=0, camera_render_view=1, engine_camera_view=1)
        app.key("Escape", screen="main_menu")

        # CMCOPTER adds the original automatic HOR actor camera. Its track
        # restriction and camera precedence remain active alongside CMFOLLOW.
        app.option("camera", 4, True)
        weather_car(1)
        app.key("Return", screen="track_selection", track_id=0, track_available=0)
        app.key("Right", track_id=1, track_available=1)
        start_scene(camera_view=3, rotorcraft_enabled=1,
                    actor_camera_available=1, actor_camera_rotorcraft=1)
        key("c", 4)
        key("c", 0, camera_trackside=0, camera_render_view=0, engine_camera_view=0)
        airborne = wait_for(lambda: (row := app.current()) and int(row["horizon_updates"]) >= 110 and row,
                           "original helicopter completes its 100-tick ground wait")
        moving = wait_for(lambda: (row := app.current()) and int(row["horizon_updates"]) >= int(airborne["horizon_updates"]) + 12 and row,
                         "helicopter flight translates the real camera and audio listener")
        require(moving["listener_y"] != airborne["listener_y"], "The helicopter camera must move with the climbing actor")
        for axis in ("x", "y", "z"):
            require(abs((int(moving[f"listener_{axis}"]) - int(airborne[f"listener_{axis}"]))
                        - (int(moving[f"rotorcraft_{axis}"]) - int(airborne[f"rotorcraft_{axis}"]))) <= 2,
                    "Camera and audio listener must inherit the actual helicopter root translation")
        for width, height in ((640, 480), (1280, 720), (1280, 800)):
            app.capture(f"camera-copter-{width}x{height}.png", width, height)
        xdo("keydown", "z")
        app.wait("rear view from helicopter", camera_view=5, engine_camera_view=5)
        xdo("keyup", "z")
        app.wait("rear release restores helicopter", camera_view=0, engine_camera_view=0)
        key("F5", 0, camera_trackside=1, engine_camera_view=3)
        key("F5", 0, camera_trackside=0, engine_camera_view=0)
        xdo("key", "Escape")
        paused = app.wait("pause freezes helicopter", pause_active=1, scene_active=0)
        wait_for(lambda: int(app.current()["poll"]) >= int(paused["poll"]) + 4, "paused host continues polling")
        require(app.current()["horizon_updates"] == paused["horizon_updates"], "Pause must freeze the automatic flight and camera")
        xdo("key", "Escape")
        app.wait("helicopter resumes", pause_active=0, scene_active=1)
        key("c", 1, camera_trackside=0, engine_camera_view=1)
        app.key("Escape", screen="main_menu")

        # Rain does not load a helicopter in the original. The regular cycle
        # stays usable, and FOLLOW can still supply authored track cameras.
        app.option("camera", 3, False)
        weather_car(3)
        app.key("Return", screen="track_selection")
        start_scene()
        app.wait("wet actor availability", resolved_weather=3, actor_camera_available=0, rotorcraft_enabled=0, camera_view=3)
        key("c", 4)
        key("c", 1, engine_camera_view=1)
        app.key("Escape", screen="main_menu")
        app.quit()
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    settings.executable, settings.data_root = sys.argv[1:]
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Camera test requires isolated dummy/offline audio")
    with Path("camera-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private camera window manager")
        flow()
        print("PASS: camera cycle/rear, cockpit shifts, helicopter route/availability, focus/pause, three aspects, audio and Extras")
    finally:
        settings.stop(manager)
