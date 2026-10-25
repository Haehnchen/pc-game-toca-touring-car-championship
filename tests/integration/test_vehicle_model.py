"""Exercise a registered Mega vehicle through the normal muted native race."""

import os
from pathlib import Path
import subprocess
import sys

import test_settings as settings
from test_extras import ExtrasWindow
from native_support import require, wait_for, window_manager_ready, xdo


class VehicleWindow(ExtrasWindow):
    def __init__(self, name, *arguments):
        settings.SettingsWindow.__init__(self, name, f"{name}.cfg", "--state-dir", f"{name}-state",
                                        "--resolution", "1280x720", "--vsync", "off", "--fps-limit", "60", *arguments)


def flow(detail, render_id):
    car_id = 2 if render_id == 10 else render_id
    name = f"vehicle-{car_id}-detail{detail}"
    if render_id in (8, 9):
        # Unlock only the selected earned bonus car.
        state = Path(f"{name}-state")
        state.mkdir(exist_ok=True)
        state.joinpath("extras.cfg").write_text(
            "version=1\nbonus_cars=off\n"
            f"earned_car_8={'on' if render_id == 8 else 'off'}\n"
            f"earned_car_9={'on' if render_id == 9 else 'off'}\n")
    if render_id == 4 and detail == 4:
        state = Path(f"{name}-state")
        state.mkdir(exist_ok=True)
        state.joinpath("extras.cfg").write_text(
            "version=1\nbonus_cars=off\nearned_car_8=on\nearned_car_9=on\n")
    app = VehicleWindow(name, "--graphics-detail", str(detail))
    try:
        app.enter()
        require(app.current()["graphics_detail"] == str(detail), "Unexpected graphics preset")
        app.car()
        if render_id == 7:
            app.key("Escape", screen="name_entry")
            xdo("type", "--delay", "15", "ALPHA")
            app.wait("Renault personal name entered through the actual editor", player_name="ALPHA")
            app.key("Return", screen="car_selection", player_name="ALPHA")
        while app.current()["car_id"] != str(car_id):
            app.key("Right")
        if render_id in (8, 9):
            require(app.current()["car_available"] == "1", "Earned bonus car must be selectable")
        if render_id == 4 and detail == 4:
            app.capture(f"{name}-ford-before-bonus.png", 1280, 720)
            for bonus_id in (8, 9):
                while app.current()["car_id"] != str(bonus_id):
                    app.key("Right", screen="car_selection")
                require(app.current()["car_available"] == "1", "Earned bonus car must remain selectable")
                app.capture(f"{name}-bonus-{bonus_id}.png", 1280, 720)
                app.key("Return", screen="track_selection", stored_car_id=bonus_id)
                app.key("Escape", screen="car_selection", car_id=bonus_id)
            while app.current()["car_id"] != str(car_id):
                app.key("Left", screen="car_selection")
            app.capture(f"{name}-ford-after-bonus.png", 1280, 720)
        app.key("Return", screen="track_selection", stored_car_id=car_id)
        app.key("Return")
        # Loading all16 Mega cars can exceed the ordinary5s menu-key deadline
        # in Debug. Wait for the completed scene, keeping the fleet assertions.
        wait_for(lambda: (row := app.current()) and row["screen"] == "scene"
                 and row["stored_car_id"] == str(car_id) and row["scene_detail"] == str(detail)
                 and row["rival_count"] == "15", "selected scene and complete fleet loaded", timeout=15)
        if render_id == 7:
            require(app.current()["player_name"] == "ALPHA", "The selected Renault must retain the editor's personal name")
        app.capture(f"{name}-grid.png", 1280, 720)
        xdo("keydown", "Up")
        wait_for(lambda: app.current()["session_state"] == "6", "original start lamps release the grid", timeout=15)
        moving = wait_for(lambda: (row := app.current()) and int(row["speed"]) > 50000 and row,
                          "Selected vehicle accelerates with the complete physical fleet", timeout=8)
        xdo("keydown", "Left")
        wait_for(lambda: int(app.current()["simulation_steps"]) >= int(moving["simulation_steps"]) + 4,
                 "Selected vehicle steers with animated front wheels")
        xdo("keyup", "Left")
        xdo("keyup", "Up")
        app.capture(f"{name}-driving.png", 1280, 720)
        require(app.current()["audio_active"] == "1" and int(app.current()["audio_voices"]) > 0
                and int(app.current()["engine_rpm"]) > 0, "Replacement must retain the normal engine audio service")
        app.key("Escape", screen="main_menu")
        app.quit()
        log = Path(f"{name}.log").read_text().splitlines()
        for model_id in {car_id, render_id}:
            loaded = any(line.startswith("Mega model:") and f"car-{model_id}.glb " in line for line in log)
            require(loaded == (detail == 4), "Native preset did not select the requested vehicle model source")
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    # This keyboard-only fixture must not receive desktop gamepad input.
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    settings.executable, settings.data_root, selected_car = sys.argv[1:]
    render_id = int(selected_car)
    require(render_id in range(11), "Native vehicle check expects a registered exterior render resource")
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Vehicle check requires isolated dummy/offline audio")
    with Path("vehicle-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private vehicle window manager")
        flow(3, render_id)
        flow(4, render_id)
        print(f"PASS: original Very High, Mega GLB car{render_id}, 16-car grid, driving, audio and return")
    finally:
        settings.stop(manager)
