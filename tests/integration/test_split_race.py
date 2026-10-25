"""Complete a two-human race using only real driving inputs and physical pits.

The feedback driver reads original POINTS and diagnostic poses. It supplies normal
held actions, never positions, lap counters, race ownership or finish times.
"""

import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zlib

import test_settings as settings
from test_time_trial import TrialWindow
from test_full_lap import route_source, route_targets, target_ahead
from native_support import StepInput, TraceReader, capture_window, require, wait_for, window_manager_ready, xdo


PIT_APPROACH = ((-123, -80), (-106, -96), (-100, -115), (-114, -135),
                (-150, -145), (-225, -145), (-199, -120))


def field(row, player, name):
    aliases = {"body_x": "x", "body_z": "z", "body_forward_x": "forward_x", "body_forward_z": "forward_z",
               "motion_boundary": "boundary", "race_total_ms": "total_ms", "race_result_ms": "result_ms"}
    return int(row[("p2_" + aliases.get(name, name)) if player else name])


def position(row, player):
    return tuple(field(row, player, name) / 65536 for name in ("body_x", "body_z"))


def stop(row, player):
    return 8 if field(row, player, "speed") > 12000 else 0


class Driver:
    def __init__(self, player, points):
        self.player, self.points = player, points
        self.stage = 0
        self.previous_forward = None
        self.phase = "pit-approach"

    def pit(self, row):
        p = self.player
        if field(row, p, "pit_exits"):
            self.phase = "lap"
            self.previous_forward = None
            return self.lap(row)
        if field(row, p, "pit_active"):
            self.phase = "pit-ai"
            return 0
        x, z = position(row, p)
        tx, tz = PIT_APPROACH[self.stage]
        if math.hypot(tx - x, tz - z) < 9 and self.stage < len(PIT_APPROACH) - 1:
            self.stage += 1
            tx, tz = PIT_APPROACH[self.stage]
        reverse = self.stage < len(PIT_APPROACH) - 1
        heading = math.atan2(-(tz - z), tx - x) * 4096 / (2 * math.pi) + (2048 if reverse else 0)
        error = (heading - field(row, p, "heading") + 2048) % 4096 - 2048
        mask = (8 if reverse else 4) if field(row, p, "speed") < 32 * 65536 / 160 else 0
        if abs(error) > 50:
            mask |= 1 if (error > 0 if reverse else error < 0) else 2
        return mask

    def lap(self, row, target=None):
        p = self.player
        point = field(row, p, "next_point")
        pos = position(row, p)
        forward = tuple(field(row, p, name) / 65536 for name in ("body_forward_x", "body_forward_z"))
        target = target if target is not None else target_ahead(self.points, point, pos, 12)
        dx, dz = target[0] - pos[0], target[1] - pos[1]
        error = math.atan2(forward[0] * dz - forward[1] * dx, forward[0] * dx + forward[1] * dz)
        yaw = 0 if self.previous_forward is None else math.atan2(
            self.previous_forward[0] * forward[1] - self.previous_forward[1] * forward[0],
            self.previous_forward[0] * forward[0] + self.previous_forward[1] * forward[1])
        self.previous_forward = forward
        steering = error - 8 * yaw
        wanted_speed = max(.42, .8 - abs(error) * .8)
        speed = field(row, p, "speed") / 65536
        return (4 if speed < wanted_speed else 8 if speed > wanted_speed + .1 else 0) | (1 if steering > .025 else 2 if steering < -.025 else 0)


def run(executable, data_root):
    settings.executable, settings.data_root = executable, data_root
    app = TrialWindow("split-race", "split-race.cfg", "--state-dir", str(Path("split-race-state").resolve()),
                      "--resolution", "640x480", "--vsync", "off", "--fps-limit", "30", "--step-input")
    ticks = TraceReader(Path("split-race.csv.simulation.csv"))
    bridge = None
    report = {"status": "failed", "original_full_race_parity": False, "input_transport": "diagnostic-step-input-two-players"}
    started = time.monotonic()
    try:
        app.wait("fresh split race", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid, "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        for screen in ("main_menu", "race_menu", "single_options"):
            app.key("Return", screen=screen)
        app.key("Down", selection=0)
        app.key("Right", players=2, qualifying=0)
        app.key("Down", selection=1)
        app.key("Left", laps=2)
        app.key("Left", laps=1)
        app.key("Down", selection=2)
        app.key("Down", selection=3)
        app.key("Right", weather=1)
        app.key("Down", selection=4)
        app.key("Return", screen="difficulty")
        app.key("Return", screen="name_entry", selection_player=0)
        xdo("type", "--delay", "15", "ALPHA")
        app.wait("first driver's name", player_name="ALPHA")
        app.key("Return", screen="car_selection", selection_player=0)
        app.key("Return", screen="name_entry", selection_player=1)
        xdo("type", "--delay", "15", "BETA")
        app.wait("second driver's name", p2_name="BETA")
        app.key("Return", screen="car_selection", selection_player=1)
        app.key("Right", p2_selected_car=1)
        app.key("Return", screen="track_selection")
        app.key("Return", screen="scene", local_players=2, rival_count=0, p2_car=1)
        bridge = StepInput(app.process)
        bridge.ready(timeout=15)
        points = route_targets(data_root)
        drivers = [Driver(p, points) for p in (0, 1)]
        first_finish = None
        contact_step = None
        next_report = time.monotonic()
        captured = set()
        with Path("split-race-driver.csv").open("w") as log:
            log.write("step,phase1,stage1,phase2,stage2,mask1,mask2\n")
            for _ in range(22000):
                require(time.monotonic() - started < 240, "Physical split-race driver exceeded its wall bound; inspect trajectory before attributing a runtime fault")
                row = ticks.rows()[-1]
                require(app.process.poll() is None, "Split race application exited")
                require(all(field(row, p, "motion_boundary") == 0 for p in (0, 1)), "A local car left supported physical track coverage")
                step = int(row["step"])
                if row["race_classified"] == "1":
                    break
                if contact_step is None:
                    if int(row["car_contacts"]) > 0:
                        contact_step = step
                        drivers[1].previous_forward = None
                        print(f"Split race: physical local collision at{step}", flush=True)
                    else:
                        require(step < 500, "Both physical cars failed to meet during the input-driven collision approach")
                        masks = [stop(row, 0), drivers[1].lap(row, position(row, 0))]
                if contact_step is not None:
                    masks = []
                    for p, driver in enumerate(drivers):
                        if field(row, p, "completed_laps"):
                            if first_finish is None:
                                first_finish = (p, step, field(row, p, "race_total_ms"))
                            driver.phase = "finished-ai"
                            masks.append(0)
                        elif p == 1 and field(row, 0, "pit_exits") == 0:
                            driver.phase = "wait-for-first-pit"
                            masks.append(stop(row, p))
                        elif field(row, p, "pit_exits") == 0:
                            masks.append(driver.pit(row))
                        elif p == 1 and (first_finish is None or step - first_finish[1] <= 150):
                            driver.phase = "wait-for-first-finish"
                            masks.append(stop(row, p))
                        else:
                            driver.phase = "lap"
                            masks.append(driver.lap(row))
                log.write(f"{step},{drivers[0].phase},{drivers[0].stage},{drivers[1].phase},{drivers[1].stage},{masks[0]},{masks[1]}\n")
                for name, should_capture in (("split-race-pits.png", row["pit_active"] == "1"),
                                             ("split-race-p2-pits.png", row["p2_pit_active"] == "1"),
                                             ("split-race-waiting.png", first_finish is not None and step - first_finish[1] > 125)):
                    if should_capture and name not in captured and step % 8 == 0:
                        capture_window(app.window, name)
                        captured.add(name)
                if first_finish and step - first_finish[1] > 125 and field(row, 1 - first_finish[0], "completed_laps") == 0:
                    require(row["race_classified"] == "0" and field(row, first_finish[0], "session_state") == 10
                            and field(row, 1 - first_finish[0], "session_state") == 6,
                            "The first finisher must wait beyond the normal five-second results delay")
                    require(field(row, first_finish[0], "race_total_ms") == first_finish[2], "Waiting changed the first driver's measured result")
                    require(row["finish_plays"] == "0", "Shared finish audio must wait for both drivers")
                require(bridge.advance(*masks) == step + 1, "Two input masks must advance exactly one shared physical deadline")
                if time.monotonic() >= next_report:
                    log.flush()
                    print(f"Split race tick{step}: " + "; ".join(f"P{p + 1} {d.phase}/{d.stage} point{field(row,p,'next_point')} pose{position(row,p)} pits{field(row,p,'pit_entries')}/{field(row,p,'pit_exits')} laps{field(row,p,'completed_laps')}" for p, d in enumerate(drivers)), flush=True)
                    next_report = time.monotonic() + 15
            else:
                raise AssertionError("Split race exceeded its physical tick bound")
        finish = ticks.rows()[-1]
        require(contact_step is not None and first_finish is not None, "Race must contain real mutual contact and staggered physical finishes")
        require(int(finish["step"]) - first_finish[1] > 150, "The second driver must finish after the first driver's wait interval")
        history = ticks.rows()
        contact_index = next(i for i, row in enumerate(history) if int(row["car_contacts"]) > 0)
        before_contact = history[contact_index - 1]
        contact_response = history[contact_index:contact_index + 6]
        require(any(field(row, 0, "speed") > field(before_contact, 0, "speed") for row in contact_response)
                and any(field(row, 1, "speed") < field(before_contact, 1, "speed") for row in contact_response)
                and all(any(position(row, p) != position(before_contact, p) for row in contact_response) for p in (0, 1)),
                "Mutual contact must move both real bodies and transfer momentum from the approaching car")
        for p in (0, 1):
            require(field(finish, p, "completed_laps") == 1 and field(finish, p, "ai_controlled") == 1,
                    "Each finite actual lap must hand its finisher to AI")
            require(field(finish, p, "race_result_ms") > 0 and field(finish, p, "race_result_ms") == field(finish, p, "race_total_ms"),
                    "Each classification must retain its own actual measured total")
            require(field(finish, p, "pit_entries") == field(finish, p, "pit_exits") == field(finish, p, "pit_plays") == 1,
                    "Each player must physically enter and exit the original pits once with its entry sound")
            pit_points = [field(r, p, "pit_point") for r in history if field(r, p, "pit_active")]
            require(set(pit_points) == set(range(1, 32)) and all(a <= b <= a + 1 for a, b in zip(pit_points, pit_points[1:])),
                    "Each pit AI must traverse adjacent authored gates")
            require(any(field(r, p, "pit_barriers") == 0 for r in history if field(r, p, "pit_active")),
                    "Each final pit stage must apply original barrier suppression")
            require(any(field(r, p, "pit_exits") == 1 and field(r, p, "ai_controlled") == 0 for r in history),
                    "Each pit exit must return real human control")
            require({field(row, p, "next_point") for row in history} == set(range(len(points))),
                    "Each actual car must traverse the complete route, including its automatic pit handoff")
        require(finish["race_result_count"] == "2" and int(finish["race_result_first"]) == first_finish[0]
                and int(finish["race_result_second"]) == 1 - first_finish[0], "The result table must contain both players in actual finish order")
        require(finish["finish_plays"] == "1" and finish["finish_playing"] == "1", "Both finishes must start the shared finish cue once")
        totals = (finish["race_result_ms"], finish["p2_result_ms"])
        require({finish["race_position"], finish["p2_position"]} == {"1", "2"}, "Both humans need distinct classified positions")
        finish_step = int(finish["step"])
        finish_ns = int(finish["simulation_ns"])
        for offset in range(1, 127):
            require(bridge.advance(0, 0) == finish_step + offset, "Finish waiting must consume one shared physical deadline")
            waiting = ticks.rows()[-1]
            require(waiting["session_state"] == waiting["p2_session_state"] == "10" and waiting["pause_active"] == "0"
                    and waiting["finish_playing"] == "1" and (waiting["race_result_ms"], waiting["p2_result_ms"]) == totals,
                    "Both finishers must retain state10, measured totals and the finish loop through the quantized five-second boundary")
        require(int(waiting["simulation_ns"]) - finish_ns == 5_040_000_000,
                "The last waiting tick must still quantize to exactly five source seconds")
        # Let the next deadline open the overlay without a confirm key. The
        # diagnostic input transport requires an active scene for its next ACK.
        bridge.resume()
        before_restart = app.wait("automatic two-driver Race Again", session_state=10, p2_session_state=10,
                                  pause_active=1, pause_page=2, pause_selection=0, finish_playing=1, finish_volume=127)
        require(int(before_restart["simulation_steps"]) == finish_step + 127
                and int(before_restart["simulation_ns"]) - finish_ns == 5_080_000_000,
                "Race Again must open on the first tick strictly beyond five quantized seconds")
        require((before_restart["race_result_ms"], before_restart["p2_result_ms"]) == totals
                and before_restart["finish_plays"] == "1",
                "Automatic Race Again must retain both actual results and the separate owned finish loop without replaying it")
        require(all(row["session_state"] != "13" and row["p2_session_state"] != "13" for row in ticks.rows()),
                "A two-player finish must open Race Again directly without the one-player result table")
        app.capture("split-race-results.png", 640, 480)
        restarted = app.key("Return", screen="scene", pause_active=0, completed_laps=0, p2_completed_laps=0, local_players=2, start_held=1)
        require(restarted["race_result_ms"] == restarted["p2_result_ms"] == "0", "Race Again must clear both classifications")
        require(restarted["finish_playing"] == "1" and restarted["finish_volume"] == "127"
                and restarted["finish_plays"] == before_restart["finish_plays"],
                "Warm grid entry must retain the existing finish voice before its state9 fade")
        require(all(field(restarted, p, name) == 0 for p in (0, 1) for name in ("pit_entries", "pit_exits"))
                and restarted["race_result_count"] == "0", "Race Again must clear both pit histories and the two-row classification")
        require(all(field(restarted, p, "pit_plays") == field(before_restart, p, "pit_plays") for p in (0, 1)),
                "Warm restart must retain cumulative pit-audio diagnostics while resetting both pit consumers")
        require(restarted["session_state"] == restarted["p2_session_state"] == "9"
                and restarted["ai_controlled"] == restarted["p2_ai_controlled"] == "0"
                and restarted["engine_gear"] == restarted["p2_gear"] == "0",
                "Warm two-player restart must restore both human controllers and held neutral grid drivetrains")
        retained = ("simulation_steps", "simulation_ns", "source_rng_state", "fragment_cursor", "damage_cycle", "exhaust_cooldown", "exhaust_phase", "skid_cursor")
        require(all(restarted[key] == before_restart[key] for key in retained),
                "Warm local restart must retain loaded world state without classification draws or extra simulation work")
        require(not any(row["screen"] == "scene_loading" for row in app.rows()
                        if int(before_restart["poll"]) < int(row["poll"]) <= int(restarted["poll"])),
                "Warm two-player restart must retain both views and their resources")
        require(all(restarted[key] == "0" for key in ("damage_parts", "damage_sum", "damage_detached", "contact_fragments", "contact_sparks",
                                                      "surface_particles", "surface_trails", "skid_persistent", "skid_live", "exhaust_sprites")),
                "Warm local restart must clear active shared damage and effect pools")
        restart_step = int(restarted["simulation_steps"])
        warm_ticks = wait_for(lambda: history if len(history := [row for row in ticks.rows() if int(row["step"]) > restart_step]) >= 2 else None,
                              "warm two-player grid updates")[:2]
        require([int(row["simulation_ns"]) - int(restarted["simulation_ns"]) for row in warm_ticks] == [40_000_000, 80_000_000]
                and all(row["session_state"] == row["p2_session_state"] == "9" and row["start_held"] == "1"
                        and row["gear"] == row["p2_gear"] == "0" and row["motion_boundary"] == row["p2_boundary"] == "0" for row in warm_ticks),
                "Both warm grid actors must execute ordinary supported 40/80ms updates before release")
        require([int(row["finish_volume"]) for row in warm_ticks] == [123, 119]
                and all(row["finish_plays"] == before_restart["finish_plays"] for row in warm_ticks),
                "Warm state9 must fade the retained finish loop by four per dispatch without starting another voice")
        app.wait("warm two-player finish-loop fade", finish_playing=0, finish_volume=0)
        app.key("Escape", pause_active=1)
        app.key("Up", pause_selection=2)
        app.key("Return", pause_page=2)
        app.key("Down", pause_selection=1)
        app.key("Return", screen="main_menu", music_track=2)
        returned = app.current()
        require(returned["scene_active"] == "0" and returned["p2_engine_active"] == "0" and returned["music_playing"] == "1",
                "Return must release both cars and restore frontend music")
        app.key("Up", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Completed split race must quit cleanly")
        _, source = route_source(data_root)
        report.update(source, status="passed", contact_step=contact_step, first_finish_step=first_finish[1],
                      final_finish_step=int(finish["step"]), player_result_ms=list(totals), ordered_points=len(points),
                      unmodified_motion_and_session=True)
        print(f"PASS: physical two-player race, mutual contact, both pits and laps, staggered finishes{totals}, automatic Race Again/restart/return", flush=True)
    except Exception as error:
        report["failure"] = str(error)
        history = ticks.rows()
        report["terminal"] = history[-1] if history else {}
        raise
    finally:
        if bridge:
            Path("split-race-step.log").write_bytes(bridge.transcript)
        settings.stop(app.process)
        report["wall_seconds"] = round(time.monotonic() - started, 3)
        paths = [Path(name) for name in ("split-race.csv", "split-race.csv.simulation.csv", "split-race-driver.csv", "split-race-results.png", "split-race-pits.png", "split-race-p2-pits.png", "split-race-waiting.png", "split-race.log", "split-race-step.log")]
        report["artifacts_crc32"] = {path.name: f"{zlib.crc32(path.read_bytes()):08x}" for path in paths if path.exists()}
        Path("split-race-acceptance.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Split race requires private dummy/offline audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} is required")
    with Path("split-race-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private split-race window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
