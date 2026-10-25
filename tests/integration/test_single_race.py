"""Drive an actual one-lap, sixteen-car Single Race via ordinary menu/input paths."""
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import test_settings as settings
from test_time_trial import TrialWindow
from test_full_lap import route_targets, target_ahead
from native_support import capture_window, StepInput, TraceReader, require, wait_for, window_manager_ready, xdo


def run(executable, data_root):
    settings.executable, settings.data_root = executable, data_root
    app = TrialWindow("single-race", "single-race.cfg", "--resolution", "640x480", "--fullscreen", "off", "--graphics-detail", "3",
                      "--vsync", "off", "--fps-limit", "30", "--step-input")
    ticks = TraceReader(Path("single-race.csv.simulation.csv"))
    bridge = None
    try:
        app.wait("fresh race process", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        for screen in ("main_menu", "race_menu", "single_options"):
            app.key("Return", screen=screen)
        app.key("Up", selection=3)
        app.key("Up", selection=2)
        app.key("Left", qualifying=0)
        app.key("Up", selection=1)
        app.key("Left", laps=2)
        app.key("Left", laps=1)
        app.key("Up", selection=0)
        app.key("Up", selection=4)
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection"):
            app.key("Return", screen=screen)
        xdo("key", "Return")
        wait_for(lambda: (row := app.current()) and row["screen"] == "scene" and row,
                 "single-race scene entry", timeout=20)
        grid = app.current()
        require(grid["rival_count"] == "15" and grid["race_position"] == "16" and grid["hud_lap_limit"] == "1",
                "NoQual must initialize all sixteen real actors and place local last with the selected lap limit")
        # The first diagnostic READY can already be waiting for stdin; capture
        # its published image without asking the paused clock for more frames.
        capture_window(app.window, "single-race-grid.png")
        bridge = StepInput(app.process)
        bridge.ready(timeout=12)
        startup_ticks = ticks.rows()
        require(len(startup_ticks) >= 2 and [int(row["step"]) for row in startup_ticks[:2]] == [1, 2]
                and [int(row["simulation_ns"]) for row in startup_ticks[:2]] == [40_000_000, 80_000_000],
                "Generated first-body handoff must use the first two ordinary deadlines without an extra startup tick")
        require(all(row["motion_boundary"] == "0" and abs(int(row["body_y"]) / 65536) < 500 for row in startup_ticks[:2]),
                "The ordinary first physical pass must resolve the temporary source height before its simulation trace")
        scene_frames = [row for row in app.rows() if row["screen"] == "scene" and row["presented"] == "1"]
        preview = next((row for row in scene_frames if row["simulation_steps"] == "0"), None)
        first_updated_frame = next((row for row in scene_frames if int(row["simulation_steps"]) > 0), None)
        require(preview is not None and first_updated_frame is not None,
                "The native flow must exercise grounded preview and the first rendered frame after scheduled simulation")
        startup_frames = (preview, first_updated_frame)
        require(all(math.isfinite(float(row["presentation_body_y"])) and abs(float(row["presentation_body_y"])) < 500
                    for row in startup_frames),
                "First-frame interpolation must never expose the source's temporary negative initialization height")
        points = route_targets(data_root)
        previous_forward = None
        split_captured = False
        deadline = time.monotonic() + 150
        next_report = time.monotonic()
        while time.monotonic() < deadline:
            row = ticks.rows()[-1]
            app.current()
            require(row["motion_boundary"] == "0", f"Local body left supported Motion at point{row['next_point']}")
            if row["session_state"] == "10":
                break
            require(row["session_state"] == "6" and row["ai_controlled"] == "0", "Measured Single Race must use actual player controls")
            if not split_captured and row["hud_split_active"] == "1" and int(row["hud_split_ticks"]) >= 48 and int(row["step"]) % 8 == 0:
                # READY follows this sampled frame; the diagnostic clock waits for our next input.
                capture_window(app.window, "single-race-split.png")
                split_captured = True
            position = tuple(int(row[field]) / 65536 for field in ("body_x", "body_z"))
            forward = tuple(int(row[field]) / 65536 for field in ("body_forward_x", "body_forward_z"))
            speed = int(row["speed"]) / 65536
            target = target_ahead(points, int(row["next_point"]), position, 12)
            dx, dz = target[0] - position[0], target[1] - position[1]
            error = math.atan2(forward[0] * dz - forward[1] * dx, forward[0] * dx + forward[1] * dz)
            yaw = 0 if previous_forward is None else math.atan2(previous_forward[0] * forward[1] - previous_forward[1] * forward[0],
                                                               previous_forward[0] * forward[0] + previous_forward[1] * forward[1])
            steering = error - 8 * yaw
            desired_speed = max(.42, .8 - abs(error) * .8)
            mask = (4 if speed < desired_speed else 8 if speed > desired_speed + .1 else 0)
            mask |= 1 if steering > .025 else 2 if steering < -.025 else 0
            step = int(row["step"])
            require(bridge.advance(mask) == step + 1, "Diagnostic race must advance exactly one physical tick")
            previous_forward = forward
            if time.monotonic() >= next_report:
                print(f"Single Race: point{row['next_point']}/{len(points)}, position{row['race_position']}, rivals{row['rival_moving']}, contacts{row['car_contacts']}", flush=True)
                next_report = time.monotonic() + 15
        else:
            raise AssertionError(f"Physical Single Race did not finish: {ticks.rows()[-1]}")
        finish = ticks.rows()[-1]
        require(finish["completed_laps"] == "1" and finish["session_lap"] == "1" and finish["ai_controlled"] == "1"
                and finish["race_classified"] == "1" and int(finish["race_result_ms"]) > 0
                and finish["race_result_ms"] == finish["race_total_ms"] and finish["finish_playing"] == "1",
                "Selected finite lap must finish, transfer actual body to AI and classify the measured total with finish audio")
        history = ticks.rows()
        launch = next((index for index, row in enumerate(history) if row["release_counter"] == "360"), None)
        require(launch is not None and launch + 360 < len(history), "The real grid start must install and exhaust the360-update release interval")
        green, active = history[launch:launch + 2]
        require(green["start_phase"] == "3" and green["session_state"] == "9" and green["gear"] == "0"
                and active["release_counter"] == "359" and active["session_state"] == "6" and active["gear"] == "1",
                "Green must retain the grid clutch before the next ordinary update enters state6 and engages automatic first gear")
        require(int(active["step"]) == int(green["step"]) + 1
                and int(active["simulation_ns"]) - int(green["simulation_ns"]) == 40_000_000,
                "Deferred green release must use consecutive ordinary 40ms deadlines")
        require(all(int(history[launch + offset]["release_counter"]) == 360 - offset for offset in range(361)),
                "The native start counter must decay once per physical update through the ordinary race path")
        require(split_captured and any(row["hud_split_active"] == "1" and ":" in row["hud_split_text"] for row in history),
                "Actual player checkpoints must publish a timed split to the ordinary rendered HUD")
        require(any(row["hud_split_active"] == "0" and int(row["hud_split_ticks"]) > 200 for row in history),
                "Checkpoint display must expire through simulation updates during the driven race")
        phases = {int(row["surface_roughness_cursor"]) for row in history}
        require(len(phases) > 1 and phases <= set(range(15)), "The physical fleet must advance the shared fifteen-entry rough-surface cycle")
        require(all(row["rival_boundaries"] == "0" for row in history), "Every opponent must stay inside supported physical track coverage through the finish")
        require(max(int(row["rival_moving"]) for row in history) == 15 and max(int(row["rival_laps"]) for row in history) > 0,
                "All fifteen physical opponents must drive and actual route records must complete laps")
        require(any(int(row["rival_voices"]) > 0 and int(row["rival_volume"]) > 0 for row in history),
                "The real offline audio service must play spatial rival engines")
        require(any(int(row["rival_wheel_audio_events"]) > 0 and int(row["rival_skid_voices"]) > 0 for row in history),
                "Retained opponent wheel queues must reach actual spatial skid playback")
        for _ in range(130):
            bridge.advance(0)
        results = ticks.rows()[-1]
        require(results["session_state"] == "13" and int(results["finish_camera"]) >= 0 and results["finish_volume"] == "127"
                and results["rival_voices"] == "0" and results["rival_skid_voices"] == "0"
                and results["race_result_ms"] == finish["race_result_ms"],
                "Original timed finish must enter the authored camera/results with stable totals, stopped motors")
        #44a370 resets timer0 at result entry; forty seconds from the finish
        #must still leave the table open until its own later timeout.
        while int(ticks.rows()[-1]["simulation_ns"]) - int(finish["simulation_ns"]) <= 40_000_000_000:
            bridge.advance(0)
            retained_result = ticks.rows()[-1]
            require(retained_result["session_state"] == "13" and retained_result["pause_active"] == "0"
                    and retained_result["race_result_ms"] == finish["race_result_ms"] and retained_result["finish_playing"] == "1",
                    "The result-entry timer must retain the table, measured total and finish loop beyond forty seconds from the finish")
        bridge.resume()
        app.wait("race results presentation", session_state=13)
        app.capture("single-race-results.png", 640, 480)
        app.key("Return", pause_active=1, pause_page=2, pause_selection=0)
        app.capture("single-race-again.png", 640, 480)
        before_restart = app.current()
        require(before_restart["session_state"] == "13" and before_restart["finish_playing"] == "1"
                and before_restart["finish_volume"] == "127" and before_restart["finish_plays"] == results["finish_plays"],
                "Race Again must retain the one-player result state and its separate finish loop without replaying it")
        overlay_fields = ("simulation_steps", "simulation_ns", "session_state", "race_result_ms", "pause_active", "pause_page", "pause_selection")
        xdo("windowminimize", app.window)
        muted_overlay = app.wait("unfocused Race Again is silent", focused=0, pause_active=1, pause_page=2, finish_playing=0)
        require(all(muted_overlay[field] == before_restart[field] for field in overlay_fields)
                and muted_overlay["finish_plays"] == before_restart["finish_plays"] and muted_overlay["finish_volume"] == "127",
                "Real focus loss must silence the finish voice while retaining the paused overlay, result and requested gain")
        xdo("windowmap", app.window)
        xdo("windowactivate", "--sync", app.window)
        resumed_overlay = app.wait("focused Race Again restores its finish loop", focused=1, minimized=0,
                                   pause_active=1, pause_page=2, finish_playing=1, finish_volume=127)
        require(all(resumed_overlay[field] == before_restart[field] for field in overlay_fields)
                and int(resumed_overlay["finish_plays"]) == int(before_restart["finish_plays"]) + 1,
                "Focus resume must restore exactly one finish voice without advancing or closing Race Again")
        before_restart = resumed_overlay
        retained_phase = before_restart["surface_roughness_cursor"]
        restarted = app.key("Return", screen="scene", session_state=9, completed_laps=0, pause_active=0, rival_count=15, race_position=16)
        require(restarted["finish_playing"] == "1" and restarted["finish_volume"] == "127"
                and restarted["finish_plays"] == before_restart["finish_plays"],
                "Warm grid entry must retain the existing finish voice before its state9 fade")
        require(restarted["simulation_steps"] == before_restart["simulation_steps"],
                "Warm Race Again must prepare the existing world before its next scheduled tick")
        require(not any(row["screen"] == "scene_loading" for row in app.rows()
                        if int(before_restart["poll"]) < int(row["poll"]) <= int(restarted["poll"])),
                "Warm Race Again must retain scene resources without entering Loading")
        expected_rng = int(before_restart["source_rng_state"])
        for _ in range(15):
            expected_rng = (expected_rng * 0x343fd + 0x269ec3) & 0xffffffff
        require(int(restarted["source_rng_state"]) == expected_rng,
                "Warm Race Again must run one fifteen-draw grid writer without resource or cold double initialization")
        retained_fields = ("damage_cycle", "fragment_cursor", "exhaust_cooldown", "exhaust_phase", "skid_cursor")
        require(all(restarted[field] == before_restart[field] for field in retained_fields),
                "Warm grid reset must preserve damage scratch, shared effect tables, exhaust clocks and skid history")
        require(all(restarted[field] == "0" for field in ("damage_parts", "damage_sum", "damage_detached", "contact_fragments", "contact_sparks",
                                                         "surface_particles", "surface_trails", "skid_persistent", "skid_live", "exhaust_sprites")),
                "Warm grid reset must repair actors and clear old detached parts, fragments, particles and marks")
        require(app.current()["race_classified"] == "0" and app.current()["best_lap_ms"] == "0", "Race Again YES must reset the original NoQual grid and all race records")
        require(app.current()["release_counter"] == "0", "Race Again must reset the source release counter before the next green light")
        require(app.current()["surface_roughness_cursor"] == retained_phase, "Race Again must preserve the process roughness phase while recreating all sixteen actors on the grid")
        restart_step = int(restarted["simulation_steps"])
        warm_ticks = wait_for(lambda: rows if len(rows := [row for row in ticks.rows() if int(row["step"]) > restart_step]) >= 2 else None,
                              "first two ordinary warm-grid updates")[:2]
        require([int(row["step"]) for row in warm_ticks] == [restart_step + 1, restart_step + 2]
                and [int(row["simulation_ns"]) - int(restarted["simulation_ns"]) for row in warm_ticks] == [40_000_000, 80_000_000],
                "Warm restart must retain monotonic ticks and the ordinary 40/80ms schedule")
        require(all(row["session_state"] == "9" and row["start_held"] == "1" and row["gear"] == "0"
                    and row["release_counter"] == "0" and row["motion_boundary"] == "0" for row in warm_ticks),
                "Warm grid must execute supported physics with the clutch held and a fresh countdown")
        require([int(row["finish_volume"]) for row in warm_ticks] == [123, 119]
                and all(row["finish_plays"] == before_restart["finish_plays"] for row in warm_ticks),
                "Warm state9 must fade the retained finish loop by four per dispatch without starting another voice")
        app.wait("warm one-player finish-loop fade", finish_playing=0, finish_volume=0)
        app.key("Escape", pause_active=1, pause_page=0, pause_selection=0)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=2, pause_selection=0)
        app.key("Down", pause_selection=1)
        app.key("Return", screen="main_menu")
        returned = app.current()
        require(returned["scene_active"] == "0" and returned["rival_count"] == "0" and returned["music_track"] == "2",
                "Race Again NO must release the complete race and restore the frontend/music")
        require(returned["surface_roughness_cursor"] == retained_phase, "Returning to menus must retain the process roughness phase")
        for screen in ("race_menu", "single_options"):
            app.key("Return", screen=screen)
        require(app.current()["selection"] == "4", "Single Race options must reopen on Continue")
        for screen in ("difficulty", "name_entry", "car_selection", "track_selection"):
            app.key("Return", screen=screen)
        xdo("key", "Return")
        wait_for(lambda: (row := app.current()) and row["screen"] == "scene" and row,
                 "single-race scene entry", timeout=20)
        require(app.current()["surface_roughness_cursor"] == retained_phase, "A newly loaded scene must borrow the same process roughness phase")
        require(app.current()["damage_cycle"] == restarted["damage_cycle"],
                "Full scene destruction and reload must preserve process damage selection state")
        require(int(app.current()["exhaust_cooldown"]) >= 899,
                "Full resource loading must initialize the exhaust clock separately from a warm restart")
        app.key("Escape", pause_active=1, pause_page=0, pause_selection=0)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=2, pause_selection=0)
        app.key("Down", pause_selection=1)
        app.key("Return", screen="main_menu")
        app.key("Up", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Ordinary quit must terminate cleanly after the completed race")
        print(f"PASS: actual16-car race finished in{finish['race_total_ms']}ms at position{finish['race_position']}; results, single-writer warm restart, world lifetime, fresh-scene reload and frontend return")
    finally:
        if bridge:
            Path("single-race-step.log").write_bytes(bridge.transcript)
        settings.stop(app.process)


if __name__ == "__main__":
    os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
    # Xvfb isolates keyboard/mouse events, but physical gamepads remain shared.
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required")
    with Path("single-race-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private Single Race window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
