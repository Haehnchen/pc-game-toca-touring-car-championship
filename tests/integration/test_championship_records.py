"""Accept the original Championship table through ending, storage and menu callers."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

import test_settings as settings
from test_championship_presentation import ChampionshipWindow
from native_support import require, wait_for, window_manager_ready, xdo


def records(path):
    data = path.read_bytes()
    require(len(data) == 228 and int.from_bytes(data[-4:], "little") == zlib.crc32(data[:-4]),
            "The nine-row championship table must retain its native CRC32")
    return [(name.split(b"\0", 1)[0].decode(), points, round_number, awards)
            for name, points, round_number, awards in struct.iter_unpack("<12sIII", data[8:-4])]


def run(executable, data_root, fixtures):
    settings.executable, settings.data_root = executable, data_root
    os.environ["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0xffff/0xffff"
    root = Path("records-state").resolve()
    subprocess.run([fixtures, str(root), "--records"], check=True, timeout=15)
    table = root / "championship/records.dat"
    app = None

    def start(name):
        nonlocal app
        app = ChampionshipWindow(name, "championship-records.cfg", "--state-dir", str(root),
                                 "--resolution", "960x600", "--vsync", "off", "--fps-limit", "30")
        app.wait("record startup", screen="title")
        app.window = xdo("search", "--onlyvisible", "--pid", app.process.pid,
                         "--name", "^TOCA Touring Car Championship$").splitlines()[0]
        xdo("windowactivate", "--sync", app.window)
        app.key("space", screen="language")
        app.key("Return", screen="main_menu")

    def menu_selection(index):
        while int(app.current()["selection"]) != index:
            app.key("Down", screen="main_menu")

    def high_scores(**expected):
        menu_selection(2)
        app.key("Return", screen="time_trial_records", selection=0, music_track=2)
        app.key("Return", screen="time_trial_records", selection=1)
        return app.key("Return", screen="championship_records", **expected)

    def load(slot, showdown=False):
        menu_selection(0)
        app.key("Return", screen="race_menu")
        target = 4 if showdown else 1
        while int(app.current()["selection"]) != target:
            app.key("Down", screen="race_menu")
        app.key("Return", screen="championship_menu", selection=0)
        app.key("Down", selection=1)
        app.key("Return", screen="championship_load_slots")
        while int(app.current()["selection"]) != slot:
            app.key("Down", screen="championship_load_slots")
        app.key("Return", screen="scene" if slot == 1 else "championship_overview")

    def finish_finale():
        app.wait("ending input released", screen="championship_finale", finale_can_exit=1)
        xdo("key", "Escape")
        app.wait("ending returns Main", screen="main_menu", music_track=2)

    def quit_app():
        menu_selection(4)
        xdo("key", "Return")
        require(app.process.wait(timeout=5) == 0, "Record acceptance must close through the ordinary menu")

    try:
        start("championship-records")
        high_scores(champ_records_error=0)
        require(not table.exists(), "Viewing defaults must not create a saved result")
        app.capture("championship-default-records.png", 960, 600)
        app.key("Escape", screen="time_trial_records", selection=1)
        app.key("Return", screen="championship_records")
        app.key("Return", screen="main_menu", selection=2)

        # A confirmed manual abandon is not either original terminal caller.
        load(1)
        app.key("Escape", pause_active=1)
        app.key("Up", pause_selection=3)
        app.key("Return", pause_page=4, pause_selection=1)
        app.key("Up", pause_selection=0)
        app.key("Return", screen="main_menu")
        require(not table.exists(), "Manual abandon must not submit championship points")

        load(0)
        app.key("Return", screen="championship_honors", championship_phase=4)
        require(not table.exists(), "Final honors must precede record submission")
        # Readable original table location, but the parent prevents atomic temp
        # creation. The prepared candidate must survive this failed write.
        parent = table.parent
        parent.chmod(0o555)
        try:
            app.key("Return", screen="championship_honors", championship_error=1)
            require(not table.exists(), "A failed write must keep the final entry unconsumed")
            app.capture("championship-record-save-error.png", 960, 600)
        finally:
            parent.chmod(0o755)
        app.key("Return", screen="championship_finale", champ_record_driver="CHAMP", champ_record_points=312,
                champ_record_round=24, champ_record_awards=255, championship_error=0)
        saved = table.read_bytes()
        rows = records(table)
        require(rows[0] == ("CHAMP", 312, 24, 255) and sum(row[0] == "CHAMP" for row in rows) == 1,
                "Successful retry must store the final awards and exactly one result")
        finish_finale()
        high_scores(champ_record_driver="CHAMP", champ_record_points=312)
        for width, height in ((640, 480), (960, 540), (960, 600)):
            app.capture(f"championship-records-{width}x{height}.png", width, height)
        app.key("Return", screen="main_menu", selection=2)
        require(table.read_bytes() == saved, "Viewing and leaving the ending must not submit the same attempt again")
        quit_app()

        start("championship-records-reloaded")
        high_scores(champ_record_driver="CHAMP", champ_record_round=24, champ_record_awards=255)
        app.key("Return", screen="main_menu", selection=2)
        load(0, showdown=True)
        app.key("Return", screen="championship_honors", championship_phase=4)
        app.key("Return", screen="championship_finale", champ_record_driver="SHOWDOWN", champ_record_points=312)
        require(records(table)[:2] == [("SHOWDOWN", 312, 24, 255), ("CHAMP", 312, 24, 255)],
                "Normal and Showdown must share one table, with new tied points before the older result")
        finish_finale()
        load(2)
        app.key("Return", screen="championship_overview", championship_phase=3)
        require(("ELIMINATE", 199, 22, 63) in records(table),
                "Elimination entry must save its source round and existing medals before confirmation")
        after_elimination = table.read_bytes()
        app.key("Return", screen="main_menu")
        require(table.read_bytes() == after_elimination, "Elimination confirmation must not duplicate its record")
        quit_app()

        # A corrupt file is visible and preserved. Reopening after repair
        # reloads the actual table without restarting or silently replacing it.
        corrupt = bytearray(after_elimination)
        corrupt[20] ^= 1
        table.write_bytes(corrupt)
        start("championship-records-corrupt")
        high_scores(champ_records_error=1)
        app.capture("championship-record-load-error.png", 960, 600)
        require(table.read_bytes() == corrupt, "Opening a damaged table must never overwrite it with defaults")
        app.key("Return", screen="main_menu")
        table.write_bytes(after_elimination)
        high_scores(champ_records_error=0, champ_record_driver="SHOWDOWN")
        app.key("Return", screen="main_menu")
        quit_app()
        print("PASS: shared nine-row scores, final/elim callers, abandon gate, atomic retry, process reload and visible corruption recovery")
    finally:
        table.parent.chmod(0o755)
        if app:
            settings.stop(app.process)


if __name__ == "__main__":
    require(os.environ.get("SDL_AUDIODRIVER") == "dummy", "Championship record checks require private dummy audio")
    for tool in ("xdotool", "openbox", "xprop", "import"):
        require(shutil.which(tool), f"{tool} required")
    with Path("championship-records-wm.log").open("w") as log:
        manager = subprocess.Popen(["openbox", "--sm-disable"], stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for(window_manager_ready, "private championship record window manager")
        run(*sys.argv[1:])
    finally:
        settings.stop(manager)
