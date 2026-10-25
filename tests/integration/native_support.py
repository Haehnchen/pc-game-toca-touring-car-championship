"""Small shared helpers for native acceptance; no application or display startup."""

import csv
import os
from pathlib import Path
import select
import struct
import subprocess
import time
import zlib


class TraceReader:
    """Read appended, newline-terminated native CSV records once.

    A reader belongs to one trace, not one scene: scene re-entry appends to the
    same history. A new process should get a new reader (or call reset()). Files
    are opened per poll, so replacement never leaves a stale descriptor alive.
    Truncation/replacement and rewrites at the previous read boundary reset the
    history; arbitrary in-place edits are outside the append-only trace contract.
    """

    def __init__(self, path):
        self.path = Path(path)
        self.reset()

    def reset(self):
        self._identity = None
        self._offset = 0
        self._pending = b""
        self._anchor = b""
        self._header = None
        self._rows = []
        self._stamp = None

    def rows(self):
        try:
            stream = self.path.open("rb")
        except FileNotFoundError:
            self.reset()
            return []
        with stream:
            status = os.fstat(stream.fileno())
            identity = (status.st_dev, status.st_ino)
            stamp = (status.st_size, status.st_mtime_ns, status.st_ctime_ns)
            if identity != self._identity or status.st_size < self._offset:
                self.reset()
            elif stamp == self._stamp:
                return self._rows.copy()
            elif self._anchor:
                stream.seek(self._offset - len(self._anchor))
                if stream.read(len(self._anchor)) != self._anchor:
                    self.reset()
            self._identity = identity
            stream.seek(self._offset)
            added = stream.read()
            self._offset += len(added)
            self._anchor = (self._anchor + added)[-64:]
            complete, separator, self._pending = (self._pending + added).rpartition(b"\n")
            if separator:
                # App trace fields contain no embedded newlines. Decode only
                # complete records, retaining even split UTF-8 bytes until LF.
                for fields in csv.reader(complete.decode("utf-8").splitlines()):
                    if not fields:
                        continue
                    if self._header is None:
                        self._header = fields
                    else:
                        require(len(fields) == len(self._header), "complete trace row differs from its CSV header")
                        self._rows.append(dict(zip(self._header, fields)))
            self._stamp = stamp
        # Match the previous reader's snapshot semantics without reparsing rows.
        return self._rows.copy()


class StepInput:
    """Synchronize a test driver with the application's optional diagnostic clock.

    READY follows simulation, audio and flushed traces; rendering is sampled. Every command
    supplies only held driving actions for exactly the next normal 40ms tick.
    """

    def __init__(self, process):
        self.process = process
        self.pending = b""
        self.transcript = bytearray()
        self.step = None

    def ready(self, timeout=5):
        deadline = time.monotonic() + timeout
        while True:
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                if not line.startswith(b"TOCA_STEP "):
                    continue
                value = line.removeprefix(b"TOCA_STEP ")
                require(value.isdigit(), "Malformed diagnostic step reply")
                step = int(value)
                require(self.step is None or step == self.step + 1, "Diagnostic input skipped or repeated a simulation step")
                self.step = step
                return step
            remaining = deadline - time.monotonic()
            require(remaining > 0 and select.select([self.process.stdout], [], [], max(0, remaining))[0],
                    "Timed out waiting for diagnostic step; see application log")
            data = os.read(self.process.stdout.fileno(), 16384)
            require(data, "Application closed diagnostic step output")
            self.pending += data
            self.transcript.extend(data)

    def advance(self, held, held_player2=None):
        require(self.step is not None, "Wait for the first diagnostic step before driving")
        require(held >= 0 and held & ~783 == 0, "Diagnostic input accepts only driving actions")
        require(held_player2 is None or held_player2 >= 0 and held_player2 & ~783 == 0,
                "Diagnostic player2 input accepts only driving actions")
        second = "" if held_player2 is None else f" {held_player2}"
        self.process.stdin.write(f"step {held}{second}\n".encode("ascii"))
        self.process.stdin.flush()
        return self.ready()

    def resume(self):
        self.process.stdin.write(b"live\n")
        self.process.stdin.flush()


def xdo(*args):
    result = subprocess.run(["xdotool", *map(str, args)], capture_output=True, text=True, timeout=3)
    require(result.returncode == 0, f"xdotool {args}: {result.stderr}")
    return result.stdout.strip()


def window_manager_ready():
    return "WINDOW" in subprocess.run(["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"],
                                      capture_output=True, text=True, timeout=2).stdout


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def capture_window(window, path):
    # ImageMagick value 1 selects PNG filter 0, avoiding unnecessary reconstruction.
    subprocess.run(["import", "-window", str(window), "-define", "png:compression-filter=1", "PNG24:" + str(path)],
                   check=True, timeout=5)


def read_png(path):
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", "invalid PNG capture")
    offset, payload = 8, bytearray()
    while offset < len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        chunk = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", chunk)
            require(depth == 8 and color in (2, 6) and not (compression or filtering or interlace),
                    "capture must be noninterlaced 8-bit RGB/RGBA")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            payload.extend(chunk)
        offset += length + 12
    raw, pixels, prior = zlib.decompress(payload), [], bytearray(width * channels)
    stride = width * channels
    require(len(raw) == height * (stride + 1), "capture decompressed size mismatch")
    for y in range(height):
        begin = y * (stride + 1)
        method, row = raw[begin], bytearray(raw[begin + 1:begin + stride + 1])
        if method == 1:
            for x in range(channels, stride):
                row[x] = (row[x] + row[x - channels]) & 255
        elif method == 2:
            row = bytearray((value + above) & 255 for value, above in zip(row, prior))
        elif method == 3:
            for x in range(stride):
                a = row[x - channels] if x >= channels else 0
                row[x] = (row[x] + (a + prior[x]) // 2) & 255
        elif method == 4:
            for x in range(stride):
                a, b, c = row[x - channels] if x >= channels else 0, prior[x], prior[x - channels] if x >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                prediction = a if pa <= pb and pa <= pc else b if pb <= pc else c
                row[x] = (row[x] + prediction) & 255
        else:
            require(method == 0, "invalid PNG filter")
        pixels.extend(zip(row[0::channels], row[1::channels], row[2::channels]))
        prior = row
    return width, height, pixels


def wait_for(predicate, description, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.025)
    raise AssertionError(f"timed out: {description}")


def qualifying_rollout(reader, start=0):
    """Wait for the real ordered start-line handover through normal physics."""
    released = wait_for(lambda: row if (current := reader.rows()[start:]) and (row := current[-1])
                        and row["session_state"] == "6" and row["ai_controlled"] == "0" else None,
                        "automatic qualifying approach and player handover at the start line", timeout=20)
    current = reader.rows()[start:]
    approach = [row for row in current if row["session_state"] == "4"]
    require(len(approach) > 32 and all(row["ai_controlled"] == "1" for row in approach),
            "The complete qualifying approach must use AI controls")
    require(len({row["next_point"] for row in approach}) >= 16,
            "Automatic approach did not advance through the ordered route")
    require(max(int(row["heading"]) for row in approach) - min(int(row["heading"]) for row in approach) > 64,
            "Automatic approach must steer the actual body through the track bends")
    require(all(row["motion_boundary"] == "0" and row["lap_time_ms"] == "0" for row in approach),
            "Approach left supported ground or started the measured lap too early")
    first = next(row for row in current if row["session_state"] == "6")
    require(first["ai_controlled"] == "0" and first["session_lap"] == "1" and first["lap_time_ms"] == "0",
            "First start-line crossing must restore controls and start measured lap one together")
    return released


def check_flag_animation(ticks):
    require(ticks and all(row["flag_updates"] == row["step"] for row in ticks),
            "Flag animation must advance once per active simulation deadline")
    if ticks[0]["flag_count"] == "0":
        require(all(row["flag_frame"] == "-1" for row in ticks), "An empty flag group must not invent an animated actor")
        return
    phases = {(int(row["flag_frame"]) - int(row["flag_updates"]) // 4) % 3 for row in ticks}
    require(len(phases) == 1 and {int(row["flag_frame"]) for row in ticks} == {0, 1, 2},
            "Authored flags must cycle through three frames every four simulation updates, independently of presentation")


def check_horizon_selection(row, track):
    dry = row["resolved_weather"] in ("1", "2")
    moving = track in (0, 5) or (dry and track in (1, 2, 6, 8))
    rotorcraft = dry and track in (1, 3, 4, 7, 8)
    require((int(row["horizon_moving_count"]) > 0) == moving
            and (row["rotorcraft_enabled"] == "1") == rotorcraft,
            "Moving HOR groups must retain their original track/weather gates, including Lavaland's exclusion")


def check_horizon_animation(ticks):
    require(ticks and all(row["horizon_updates"] == row["step"] for row in ticks),
            "HOR motion must advance once per active simulation deadline")
    if int(ticks[0]["horizon_moving_count"]) > 0:
        positions = {tuple(row[key] for key in ("horizon_x", "horizon_y", "horizon_z")) for row in ticks}
        require(len(positions) > 1, "Authored HOR actors must move during the ordinary scene")
    if ticks[0]["rotorcraft_enabled"] == "1":
        positions = {tuple(row[key] for key in ("rotorcraft_x", "rotorcraft_y", "rotorcraft_z")) for row in ticks}
        require(len(positions) > 1, "Authored rotorcraft must leave its initial wait and follow its original route")


def leave_scene(current, **expected):
    """Use the original END RACE / RACE AGAIN? NO path."""
    def key(value, **expected):
        previous = int(current()["poll"])
        xdo("key", value)
        return wait_for(lambda: (row := current()) and int(row["poll"]) > previous
                        and all(row.get(field) == str(wanted) for field, wanted in expected.items()) and row,
                        f"pause exit {value}: {expected}")
    key("Escape", pause_active=1, pause_selection=0)
    last = 3 if current()["qualifying_phase"] == "1" and current()["session_state"] not in ("7", "8") else 2
    key("Up", pause_selection=last)
    key("Return", pause_page=2, pause_selection=0)
    key("Down", pause_selection=1)
    return key("Return", screen="main_menu", **expected)
