"""Exercise championship saves, earned honors and the moving finale through the native UI."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

import test_settings as settings
from test_time_trial import TrialWindow
from native_support import capture_window, read_png, require, wait_for, window_manager_ready, xdo


class ChampionshipWindow(TrialWindow):
    def capture(self, name, width, height):
        xdo("windowsize", "--sync", self.window, width, height)
        self.wait("championship drawable resize", width=width, height=height)
        frame = int(self.current()["frame"])
        wait_for(lambda: int(self.current()["frame"]) >= frame + 2, "championship presentation")
        target = Path(name)
        capture_window(self.window, target)
        w, h, pixels = read_png(target)
        # Original CSCREENS uses a small tinted palette, including the final
        # START picture. A generic richly colored race-scene gate is unsuitable.
        require((w, h) == (width, height) and len(set(pixels)) >= 16,
                "Championship capture must retain the authored background and visible text")
        if self.current()["screen"] == "championship_honors":
            require((255, 255, 255) in pixels, "Honors must overlay the original white award captions")
        if self.current()["screen"] == "championship_finale":
            require(len(set(pixels)) > 100, "The finale must render its textured Silverstone world and convoy")


def run(executable, data_root, fixture_executable, graphics_detail="0"):
    settings.executable, settings.data_root = executable, data_root
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    root = Path("presentation-state").resolve()
    subprocess.run([fixture_executable, str(root)], check=True, timeout=15)
    normal = root / "championship/normal"
    showdown = root / "championship/showdown"
    kept = (normal / "slot-2.dat").read_bytes()
    final = (normal / "slot-4.dat").read_bytes()
    extra = (showdown / "slot-1.dat").read_bytes()
    app = ChampionshipWindow("championship-presentation", "championship-presentation.cfg", "--state-dir", str(root),
                      "--resolution", "640x480", "--graphics-detail", graphics_detail, "--vsync", "off", "--fps-limit", "120")

    def load_menu(showdown_mode=False):
        app.key("Return", screen="race_menu")
        # Returning to Main retains the selected race mode.
        target = 4 if showdown_mode else 1
        while int(app.current()["selection"]) != target:
            app.key("Down", screen="race_menu")
        app.key("Return", screen="championship_menu", selection=0)
        app.key("Down", selection=1)
        app.key("Return", screen="championship_load_slots")
        while int(app.current()["selection"]) != 0:
            app.key("Down", screen="championship_load_slots")

    def quit_scene():
        app.key("Escape", pause_active=1, pause_selection=0)
        app.key("Up", pause_selection=3)
        app.key("Return", pause_page=4, pause_selection=1)
        app.key("Up", pause_selection=0)
        app.key("Return", screen="main_menu", music_track=2)

    def play_finale(prefix, full_cycle):
        # Keep the intermediate-frame observation cheap in Debug. The existing
        # captures below still exercise the complete textured scene at all sizes.
        xdo("windowsize", "--sync", app.window, 640, 480)
        app.wait("finale motion viewport", width=640, height=480, presented=1)
        started = app.key("Return", screen="championship_finale", championship_phase=4, music_track=4)
        require(started["finale_can_exit"] == "0" and int(started["audio_voices"]) >= 3,
                "Finale must start its source input lock, CD04 and real motor voices")
        xdo("key", "Return")
        wait_for(lambda: int(app.current()["finale_active_ms"]) >= 240, "early finale input consumed")
        require(app.current()["screen"] == "championship_finale", "The first four source seconds must reject exit input")
        if graphics_detail == "4":
            log = Path("championship-presentation.log").read_text()
            require(all(f"Mega finale: {role} " in log for role in ("pace-car", "transporter")),
                    "The moving Mega finale must load both real generated convoy models")
        positions = tuple(started[k] for k in ("finale_pace_x", "finale_pace_z", "finale_truck_x", "finale_truck_z"))
        moving = wait_for(lambda: (row := app.current()) and row["presented"] == "1" and int(row["finale_active_ms"]) >= 1000
                          and tuple(row[k] for k in ("finale_pace_x", "finale_pace_z", "finale_truck_x", "finale_truck_z")) != positions
                          and int(row["finale_model_triangles"]) > 0 and row, "physical moving convoy", timeout=5)
        require(int(moving["gpu_model_draw_calls"]) > 0 and int(moving["gpu_model_instances"]) >= 5,
                "The moving native finale must submit its convoy through resident GPU model draws")

        def rendered_position(row):
            return tuple(float(row[f"finale_presentation_{axis}"]) for axis in "xyz")

        def smooth_pairs():
            rows = [row for row in app.rows() if row["screen"] == "championship_finale" and row["presented"] == "1"
                    and row["focused"] == "1" and row["minimized"] == "0" and int(row["poll"]) >= int(started["poll"])]
            return [(a, b) for a, b in zip(rows, rows[1:])
                    if a["finale_active_ms"] == b["finale_active_ms"] and rendered_position(a) != rendered_position(b)]

        pairs = wait_for(lambda: (found := smooth_pairs()) and len(found) >= 8 and found,
                         "finale images moving between unchanged40ms convoy ticks", timeout=5)
        for a, b in pairs:
            require(all(a[field] == b[field] for field in ("finale_pace_x", "finale_pace_z", "finale_truck_x", "finale_truck_z",
                                                         "finale_phase", "finale_credit_page", "finale_can_exit")),
                    "Finale render motion must retain authoritative convoy poses, camera phases and credit chronology")
            require(0 <= float(a["finale_presentation_alpha"]) < float(b["finale_presentation_alpha"]) <= 1,
                    "Finale render motion must use its advancing shared interpolation fraction")
            require(int(a["finale_active_ms"]) % 40 == 0, "Finale source timing must retain exact40ms simulation deadlines")
        if full_cycle:
            app.capture(f"{prefix}-orbit.png", 960, 600)
            xdo("windowminimize", app.window)
            frozen = app.wait("finale focus pause", focused=0, minimized=1, audio_voices=0)
            wait_for(lambda: int(app.current()["poll"]) >= int(frozen["poll"]) + 4, "paused finale polls")
            require(all(app.current()[field] == frozen[field] for field in
                        ("finale_active_ms", "finale_credit_page", "finale_phase", "finale_pace_x", "finale_pace_z", "finale_truck_x", "finale_truck_z",
                         "finale_presentation_x", "finale_presentation_y", "finale_presentation_z", "finale_presentation_alpha")),
                    "Focus loss must freeze source physics, rendered convoy motion, cameras and credits together")
            xdo("windowmap", "--sync", app.window)
            xdo("windowactivate", "--sync", app.window)
            app.wait("finale focus restored", focused=1, minimized=0)
        app.wait("second authored finale camera", finale_phase=1, finale_can_exit=1)
        app.capture(f"{prefix}-transporter.png", 960, 600)
        credits = wait_for(lambda: (row := app.current()) and row["finale_phase"] == "2"
                           and int(row["finale_credit_glyphs"]) > 0 and row,
                           "third authored camera and first nonempty original credits", timeout=10)
        require(int(credits["finale_credit_page"]) >= 2 and int(credits["finale_active_ms"]) > int(moving["finale_active_ms"]),
                "The leading original PAGE must preserve its empty first switch before the credits appear")
        aspects = ((640, 480), (960, 540), (960, 600)) if full_cycle else ((960, 600),)
        for width, height in aspects:
            app.capture(f"{prefix}-{width}x{height}.png", width, height)
        if full_cycle:
            wait_for(lambda: (row := app.current()) and int(row["finale_active_ms"]) >= 12500
                     and row["finale_phase"] == "0" and row["finale_can_exit"] == "0", "repeated finale camera cycle", timeout=7)
            xdo("key", "Escape")
            locked = app.current()
            wait_for(lambda: int(app.current()["poll"]) >= int(locked["poll"]) + 2, "second-cycle exit rejection")
            require(app.current()["screen"] == "championship_finale", "Each twelve-second cycle must repeat its source four-second exit lock")
            app.wait("finale exit released again", finale_can_exit=1)
        xdo("key", "Escape")
        app.wait("leave finale for ordinary Main", screen="main_menu", music_track=2, audio_voices=0)

    try:
        app.wait("championship presentation startup", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        app.key("Return", screen="main_menu")
        load_menu()
        for width, height in ((640, 480), (960, 540), (960, 600)):
            app.capture(f"championship-slots-{width}x{height}.png", width, height)
        app.key("Down", selection=1)
        app.key("Down", selection=3)
        app.key("Down", selection=0)
        require((normal / "slot-3.dat").read_bytes() == b"damaged private test slot",
                "Load navigation must skip the damaged and empty slots while keeping other saves available")
        app.key("Return", screen="championship_overview", championship_meeting=2, championship_phase=2)
        progress = root / "extras.cfg"
        prior_progress = progress.read_bytes() if progress.exists() else None
        if progress.exists():
            progress.unlink()
        progress.mkdir()
        (progress / "block-progress").write_text("private test progress failure")
        app.key("Return", screen="championship_overview", championship_meeting=2, championship_phase=2)
        require(not (normal / "slot-5.dat").exists(), "Failed earned-progress storage must retain the unconsumed race result")
        (progress / "block-progress").unlink()
        progress.rmdir()
        if prior_progress is not None:
            progress.write_bytes(prior_progress)
        app.key("Return", screen="championship_save_prompt", selection=0, championship_meeting=3)
        app.key("Return", screen="championship_save_slots", selection=0)
        app.key("Down", selection=1)
        app.key("Return", screen="championship_save_overwrite", selection=1)
        app.capture("championship-overwrite-no.png", 960, 600)
        app.key("Return", screen="championship_save_feedback")
        app.wait("replacement declined", screen="championship_save_prompt", selection=0)
        app.key("Return", screen="championship_save_slots", selection=1)
        require((normal / "slot-2.dat").read_bytes() == kept, "Default No must preserve the selected existing championship")
        app.key("Down", selection=2)
        app.key("Down", selection=3)
        app.key("Down", selection=4)
        app.key("Return", screen="championship_save_feedback")
        app.wait("saved block honors", screen="championship_honors")
        saved = (normal / "slot-5.dat").read_bytes()
        require(int.from_bytes(saved[-4:], "little") == zlib.crc32(saved[:-4]), "Named saves must have a valid IEEE CRC32")
        require(struct.unpack_from("<III", saved, 20) == (3, 0, 0), "Saved state must begin meeting4 qualifying after the completed six-race block")
        require(saved[40:52].split(b"\0", 1)[0] == b"GOLD", "Slot name must be the stored driver name")
        require((normal / "slot-2.dat").read_bytes() == kept and (showdown / "slot-1.dat").read_bytes() == extra,
                "Saving one slot must preserve other slots and the independent Showdown bank")
        for width, height in ((640, 480), (960, 540), (960, 600)):
            app.capture(f"championship-honors-{width}x{height}.png", width, height)
        app.key("Return", screen="scene", championship_meeting=3, championship_race=0, championship_phase=0, track_id=3)
        quit_scene()
        require((normal / "slot-5.dat").read_bytes() == saved, "Leaving a session must not overwrite explicitly saved slots")

        load_menu()
        app.key("Down", selection=1)
        app.key("Return", screen="championship_overview", championship_meeting=0)
        app.key("Return", screen="championship_save_prompt", selection=0)
        app.key("Return", screen="championship_save_slots", selection=1)
        app.key("Up", selection=0)
        blocked = normal / "slot-1.dat"
        earlier = blocked.read_bytes()
        blocked.unlink()
        blocked.mkdir()
        (blocked / "block-replacement").write_text("private test write failure")
        app.key("Return", screen="championship_save_overwrite", selection=1)
        app.key("Up", selection=0)
        app.key("Return", screen="championship_save_slots", selection=0)
        app.capture("championship-save-error.png", 960, 600)
        require((normal / "slot-2.dat").read_bytes() == kept, "A failed replacement must retain other saves and permit retry")
        (blocked / "block-replacement").unlink()
        blocked.rmdir()
        blocked.write_bytes(earlier)
        app.key("Return", screen="championship_save_overwrite", selection=1)
        app.key("Up", selection=0)
        app.key("Return", screen="championship_save_feedback")
        # Three-second source feedback precedes two distinct loading steps.
        # Observe each bounded transition independently; a fast original load
        # may already have completed a later step when the gate observes it.
        wait_for(lambda: (row := app.current()) and row["screen"] in ("scene_loading", "scene")
                 and row["championship_meeting"] == "1" and row["track_id"] == "1" and row,
                 "explicit replacement finishes feedback and begins qualifying load", timeout=8)
        for step in (1, 2):
            wait_for(lambda: (row := app.current()) and row["screen"] in ("scene_loading", "scene")
                     and row["championship_meeting"] == "1" and row["track_id"] == "1"
                     and int(row["loading_step"]) >= step and row,
                     f"explicit replacement completes qualifying loading step {step}", timeout=8)
        app.wait("explicit replacement resumes qualifying", screen="scene", championship_meeting=1)
        replaced = blocked.read_bytes()
        require(replaced != earlier and replaced[40:52].split(b"\0", 1)[0] == b"KEEP",
                "Retry with explicit Yes must replace only the chosen slot with the retained current championship")
        quit_scene()

        load_menu()
        app.key("Down", selection=1)
        app.key("Return", screen="championship_overview", championship_meeting=0)
        app.key("Return", screen="championship_save_prompt", selection=0)
        app.key("Down", selection=1)
        app.key("Return", screen="scene", championship_meeting=1, track_id=1)
        quit_scene()
        require((normal / "slot-2.dat").read_bytes() == kept, "Declining Save must leave the earlier checkpoint untouched")

        load_menu()
        app.key("Down", selection=1)
        app.key("Down", selection=3)
        app.key("Return", screen="championship_overview", championship_meeting=11)
        app.key("Return", screen="championship_honors", championship_phase=4)
        app.capture("championship-final-honors.png", 960, 600)
        play_finale("championship-finale", full_cycle=True)
        require((normal / "slot-4.dat").read_bytes() == final, "Final awards must not silently replace the selected save")

        load_menu(showdown_mode=True)
        app.capture("showdown-independent-slots.png", 960, 600)
        app.key("Return", screen="championship_overview", championship_meeting=11)
        app.key("Return", screen="championship_honors", championship_phase=4)
        require(app.current()["championship_speech_plays"] == "1" and app.current()["championship_speech_playing"] == "1",
                "The English Showdown final page must play its existing FINAL speech through the real offline audio service")
        app.capture("showdown-final-honors.png", 960, 600)
        play_finale("showdown-finale", full_cycle=False)
        require((showdown / "slot-1.dat").read_bytes() == extra, "Showdown completion must preserve the source save checkpoint")
        app.key("Up", selection=4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Presentation acceptance must exit cleanly through Main")
        print("PASS: named saves, earned honors, physical/interpolated finale convoy, authored camera/credit cycle, focus freeze and original ending audio")
    finally:
        settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Championship presentation requires private dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required")
    with Path("championship-presentation-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private championship presentation window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
