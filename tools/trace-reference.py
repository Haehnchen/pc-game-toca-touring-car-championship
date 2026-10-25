#!/usr/bin/env python3
"""Capture source-boundary TOCA 1.1a state through its own Wine debugger.

The canonical executable and data remain read-only. Debugger pauses perturb QPC;
recorded deltas are observations, not an unperturbed timing baseline.
"""

import argparse
from dataclasses import dataclass
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib

PROJECT = Path(__file__).resolve().parent.parent
LAYOUT = {"body_base": 0x4CD760, "body_stride": 0x53C,
          "dynamics_base": 0x4C5DA0, "dynamics_stride": 0x338,
          "gameplay_base": 0x4CC518, "gameplay_stride": 0x124}
GLOBALS = {"delta_q16": 0x4AD080, "reciprocal_q16": 0x4AD084,
           "extra_update": 0x4BE19C, "rng": 0x4B9E78,
           "qpc_low": 0x4BE0CC, "qpc_divisor": 0x4BE148,
           "qpc_difference": 0x4BE194, "clock_first_sample": 0x4AD0F0,
           "update_counter": 0x4D383C, "race_clock": 0x4D3840}
ANSI = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07]*\x07")
SHUTTLE_SCENARIOS = ("shuttle", "slalom-shuttle", "offroad-shuttle")
WEATHER_CHOICES = {"sunny": 1, "cloudy": 2, "rain": 3, "snow": 4, "storm": 5}
ONE = 65536


# Explicit local load also works when this script is imported by its tool tests.
FLEET_SPEC = importlib.util.spec_from_file_location("reference_fleet", PROJECT / "tools/reference_fleet.py")
FLEET = importlib.util.module_from_spec(FLEET_SPEC)
FLEET_SPEC.loader.exec_module(FLEET)
SCENE_SPEC = importlib.util.spec_from_file_location("reference_scene_journal", PROJECT / "tools/reference_scene_journal.py")
SCENE = importlib.util.module_from_spec(SCENE_SPEC)
SCENE_SPEC.loader.exec_module(SCENE)


def signed(value):
    return value if value < 0x80000000 else value - 0x100000000


def body_values(snapshot):
    body = bytes.fromhex(snapshot["body_hex"])
    return {"position": struct.unpack_from("<3i", body, 0xbc),
            "velocity": struct.unpack_from("<3i", body, 0xc8),
            "speed": struct.unpack_from("<i", body, 0xe0)[0],
            "forward": struct.unpack_from("<3i", body, 0x150),
            "side": struct.unpack_from("<3i", body, 0x168),
            "wheel_airborne": body[0x517:0x51b]}


def dot(first, second):
    return sum((a * b) >> 16 for a, b in zip(first, second))


class InputPolicy:
    """Choose physical keys; thresholds here are test inputs, never car tuning.

    40a770 publishes the world pose/basis; 422f60 maps the actual keyboard;
    418fd0 supplies automatic reverse. No original state or time is written.
    """

    def __init__(self, scenario, updates, initial):
        self.scenario = scenario
        self.updates = updates
        pose = body_values(initial)
        self.forward = pose["forward"]
        self.side = pose["side"]
        self.anchor = None
        self.anchor_update = None
        self.direction = 1
        self.offroad_stage = "grass-entry" if scenario == "offroad-shuttle" else None
        self.grass_entry_update = None
        self.parameters = {"version": 1, "keys_only": True}
        if scenario in SHUTTLE_SCENARIOS:
            if initial["transmission_mode"] != 2:
                raise RuntimeError("Shuttle recordings require the original automatic transmission")
            self.parameters.update(version=2, settle_speed_q16=0x2000, longitudinal_limit_q16=12 * ONE,
                                   maximum_drive_speed_q16=0x8000,
                                   slalom_lateral_q16=3 * ONE // 2,
                                   steering_pulse_period=3, steering_deadband_q16=0x800,
                                   maximum_heading_target_q16=0x5000,
                                   maximum_longitudinal_q16=120 * ONE, maximum_lateral_q16=50 * ONE)
            if scenario == "offroad-shuttle":
                self.parameters.update(version=3, grass_entry_lateral_q16=-10 * ONE,
                                       minimum_grounded_grass_wheels=2, grass_entry_heading_side_q16=-0x6000,
                                       grass_anchor_after_physical_braking=True)

    def choose(self, index, snapshot):
        if self.scenario not in SHUTTLE_SCENARIOS:
            desired = set() if self.scenario == "coast" else {"Up"}
            if self.scenario == "accel-turn-brake":
                if self.updates // 3 <= index < 2 * self.updates // 3:
                    desired.add("Right")
                elif index >= 2 * self.updates // 3:
                    desired = {"Down"}
            elif self.scenario == "brake-lamps":
                quarter = min(3, 4 * index // self.updates)
                desired = ({"Up"}, {"Down"}, {"Up"}, set())[quarter]
            elif self.scenario in ("accel-left", "accel-right"):
                desired.add("Left" if self.scenario == "accel-left" else "Right")
            return desired, {"phase": self.scenario, "index": index}
        pose = body_values(snapshot)
        if self.anchor is None:
            if pose["speed"] > self.parameters["settle_speed_q16"]:
                if index >= 200:
                    raise RuntimeError("Original initial braking did not settle within200 updates")
                return {"Down"}, {"phase": "initial-brake", "speed_q16": pose["speed"]}
            # This is an observed pose defining the test's local target frame,
            # not a reset or a write to the game.
            self.anchor = pose["position"]
            self.anchor_update = index
        delta = tuple(value - origin for value, origin in zip(pose["position"], self.anchor))
        longitudinal, lateral = dot(delta, self.forward), dot(delta, self.side)
        if abs(longitudinal) > self.parameters["maximum_longitudinal_q16"] or abs(lateral) > self.parameters["maximum_lateral_q16"]:
            raise RuntimeError("Original car left the declared local shuttle bounds")
        frame = {"anchor_position_q16": self.anchor, "anchor_update": self.anchor_update,
                 "anchor_forward_q16": self.forward, "anchor_side_q16": self.side,
                 "longitudinal_q16": longitudinal, "lateral_q16": lateral}
        if self.offroad_stage in ("grass-entry", "grass-brake"):
            surfaces = bytes.fromhex(snapshot["dynamics_hex"])[0x328:0x32c]
            grass_wheels = sum(surface == 2 and not airborne for surface, airborne in zip(surfaces, pose["wheel_airborne"]))
            if self.offroad_stage == "grass-entry" and lateral <= self.parameters["grass_entry_lateral_q16"] and grass_wheels >= self.parameters["minimum_grounded_grass_wheels"]:
                self.offroad_stage = "grass-brake"
                self.grass_entry_update = index
            if self.offroad_stage == "grass-brake":
                if pose["speed"] <= self.parameters["settle_speed_q16"]:
                    # The second frame is sampled after an actual grass drive
                    # and physical braking. The original car is never reset.
                    self.anchor = pose["position"]
                    self.forward, self.side = pose["forward"], pose["side"]
                    self.anchor_update = index
                    self.direction = 1
                    self.offroad_stage = "grass-shuttle"
                    return self.choose(index, snapshot)
                return {"Down"}, {"phase": "grass-brake", **frame, "speed_q16": pose["speed"],
                                  "grounded_grass_wheels": grass_wheels, "grass_entry_update": self.grass_entry_update}
            coasting = dot(pose["velocity"], pose["forward"]) > 0 and pose["speed"] > self.parameters["maximum_drive_speed_q16"]
            heading = dot(pose["forward"], self.side)
            heading_target = self.parameters["grass_entry_heading_side_q16"]
            heading_error = heading_target - heading
            keys = set() if coasting else {"Up"}
            if abs(heading_error) > self.parameters["steering_deadband_q16"]:
                keys.add("Left" if heading_error > 0 else "Right")
            return keys, {"phase": "grass-entry", **frame, "speed_q16": pose["speed"],
                          "coasting": coasting, "target_lateral_q16": self.parameters["grass_entry_lateral_q16"],
                          "heading_side_q16": heading, "target_heading_q16": heading_target,
                          "steering_error_q16": heading_error,
                          "grounded_grass_wheels": grass_wheels}
        limit = self.parameters["longitudinal_limit_q16"]
        if longitudinal >= limit:
            self.direction = -1
        elif longitudinal <= -limit:
            self.direction = 1
        active = index - self.anchor_update
        target = 0
        if self.scenario == "slalom-shuttle":
            target = self.parameters["slalom_lateral_q16"] * (1 if active % 120 < 60 else -1)
        velocity = dot(pose["velocity"], self.forward)
        travel_sign = 1 if velocity > 0x333 else -1 if velocity < -0x333 else self.direction
        side_error = target - lateral
        heading_target = max(-0x5000, min(0x5000, side_error // 12)) * travel_sign
        heading = dot(pose["forward"], self.side)
        steering_error = (heading_target - heading) * travel_sign
        coasting = velocity * self.direction > 0 and pose["speed"] > self.parameters["maximum_drive_speed_q16"]
        desired = set() if coasting else {"Up" if self.direction > 0 else "Down"}
        if active % self.parameters["steering_pulse_period"] == 0 and abs(steering_error) > self.parameters["steering_deadband_q16"]:
            desired.add("Left" if steering_error > 0 else "Right")
        return desired, {"phase": ("grass-" if self.offroad_stage == "grass-shuttle" else "") + ("forward" if self.direction > 0 else "reverse"),
                         **frame, "grass_entry_update": self.grass_entry_update,
                         "target_lateral_q16": target, "velocity_longitudinal_q16": velocity,
                         "speed_q16": pose["speed"], "coasting": coasting,
                         "heading_side_q16": heading, "target_heading_q16": heading_target,
                         "steering_error_q16": steering_error}


class FleetPursuitPolicy:
    """Keys-only pursuit of one observed rival; no native actor state is imported."""

    def __init__(self, initial):
        self.actor = initial["actor"]
        local = body_values(initial)
        candidates = []
        for entry in initial["actors"]:
            if entry["actor"] == self.actor:
                continue
            target = body_values(entry)
            delta = tuple(a - b for a, b in zip(target["position"], local["position"]))
            front = dot(delta, local["forward"])
            if front > 0:
                candidates.append((sum(value * value for value in delta), entry["actor"]))
        if not candidates:
            raise RuntimeError("Fleet pursuit requires an actual rival ahead of the local car")
        self.target = min(candidates)[1]
        self.parameters = {"version": 1, "keys_only": True, "target_actor": self.target,
                           "selection": "nearest initial rival with positive forward distance",
                           "steering_deadband_radians": 0.035, "neutral_pulse_period": 4}

    def choose(self, index, snapshot):
        if snapshot["actor"] != self.actor:
            raise RuntimeError("Fleet pursuit local identity changed")
        local = body_values(snapshot)
        entry = snapshot["actors"][self.target]
        if entry["actor"] != self.target:
            raise RuntimeError("Fleet pursuit target identity changed")
        target = body_values(entry)
        delta = tuple(a - b for a, b in zip(target["position"], local["position"]))
        front, side = dot(delta, local["forward"]), dot(delta, local["side"])
        angle = math.atan2(side, front)
        desired = {"Up"}
        # Once alongside/behind, brake physically and wait for the same rival
        # instead of commanding an instantaneous turn around the body.
        phase = "pursuit"
        if front < -2 * ONE:
            desired = {"Down"}
            phase = "wait-for-target"
        elif abs(angle) > self.parameters["steering_deadband_radians"] and index % 4 != 3:
            desired.add("Left" if angle > 0 else "Right")
        return desired, {"phase": phase, "target_actor": self.target, "front_q16": front,
                         "side_q16": side, "distance_metres": math.sqrt(sum(value * value for value in delta)) / ONE,
                         "signed_angle_radians": angle}


@dataclass(frozen=True)
class DetachmentApproachGate:
    progress_q16: int
    minimum_speed_q16: int
    reference_trace_crc32: str
    reference_update: int


class FleetDetachmentPolicy:
    """Accelerate, turn into the outer wall, then observe actual detached parts."""

    def __init__(self, initial, approach_gate=None):
        self.actor = initial["actor"]
        self.released_updates = 0
        self.spawned = False
        pose = body_values(initial)
        initial_side = pose["side"]
        self.target_right = tuple(-value for value in initial_side)
        self.aligned = False
        self.approach_gate = approach_gate
        self.initial_position, self.initial_forward = pose["position"], pose["forward"]
        self.approach_ready = False
        self.parameters = {"version": 5, "keys_only": True, "acceleration_updates_after_release": 50,
                           "wall_turn_key": "Right", "release_gate": "session6 and start-clutch dynamics148==0",
                           "initial_side_q16": initial_side, "target_right_q16": self.target_right,
                           "alignment_dot_gate_q16": 0xb333,
                           "coast_gate": "detached_count>0 and an actual pool record active108!=0",
                           "source": "409fa0 impact;419ad0 severity/cycle;416320 spawn;417890 lifecycle"}
        if approach_gate is not None:
            self.parameters.pop("acceleration_updates_after_release")
            self.parameters.update(version=6, approach_gate={
                "kind": "horizontal-forward-progress-and-speed", "progress_q16": approach_gate.progress_q16,
                "minimum_speed_q16": approach_gate.minimum_speed_q16,
                "reference_trace_crc32": approach_gate.reference_trace_crc32, "reference_update": approach_gate.reference_update,
                "initial_position_q16": self.initial_position, "initial_forward_q16": self.initial_forward})

    def choose(self, index, snapshot):
        if snapshot["actor"] != self.actor:
            raise RuntimeError("Fleet detachment local identity changed")
        shared = snapshot["shared"]
        pool = bytes.fromhex(shared["detached_pool_hex"])
        active_slots = [slot for slot in range(10) if struct.unpack_from("<I", pool, slot * 0x11c + 0x108)[0]]
        self.spawned |= shared["detached_count"] > 0 and bool(active_slots)
        dynamics = bytes.fromhex(snapshot["dynamics_hex"])
        released = snapshot["session_state"] == 6 and struct.unpack_from("<i", dynamics, 0x148)[0] == 0
        pose = body_values(snapshot)
        alignment = dot(pose["forward"], self.target_right)
        progress = None
        if self.approach_gate is None:
            turn_ready = self.released_updates >= self.parameters["acceleration_updates_after_release"]
        else:
            # First-grid Y is an ungrounded sentinel. The approach input uses
            # only the actual horizontal displacement along the initial basis.
            progress = sum(((pose["position"][axis] - self.initial_position[axis]) * self.initial_forward[axis]) >> 16
                           for axis in (0, 2))
            self.approach_ready |= (released and progress >= self.approach_gate.progress_q16
                                    and pose["speed"] >= self.approach_gate.minimum_speed_q16)
            turn_ready = self.approach_ready
        if turn_ready and alignment >= self.parameters["alignment_dot_gate_q16"]:
            self.aligned = True
        if self.spawned:
            keys, phase = set(), "observe-detached"
        elif self.aligned:
            keys, phase = {"Up"}, "accelerate-to-wall"
        elif turn_ready:
            keys, phase = {"Up", self.parameters["wall_turn_key"]}, "wall-impact"
        else:
            keys, phase = {"Up"}, "accelerate" if released else "held-grid"
        decision = {"phase": phase, "released_updates": self.released_updates,
                    "alignment_dot_to_right_q16": alignment, "aligned_to_right": self.aligned,
                    "detached_count": shared["detached_count"], "active_detached_slots": active_slots}
        if self.approach_gate is not None:
            decision.update(approach_progress_q16=progress, approach_ready=self.approach_ready)
        if released:
            self.released_updates += 1
        return keys, decision


class Coverage:
    def __init__(self, initial):
        self.previous = body_values(initial)["position"]
        self.counts = {name: 0 for name in ("updates", "moving_updates", "steered_moving_updates", "braked_updates", "reverse_moving_updates", "offroad_moving_updates")}
        self.surfaces = [0, 0, 0, 0]
        self.distance = 0.0

    def consume(self, consumed, post):
        pose = body_values(post)
        moving = pose["speed"] > 0xccc
        axes = consumed["input_q16"]
        surfaces = bytes.fromhex(post["dynamics_hex"])[0x328:0x32c]
        self.counts["updates"] += 1
        self.counts["moving_updates"] += moving
        self.counts["steered_moving_updates"] += moving and bool(axes["left"] or axes["right"])
        self.counts["braked_updates"] += bool(axes["brake"])
        self.counts["reverse_moving_updates"] += dot(pose["velocity"], pose["forward"]) < -0xccc
        self.counts["offroad_moving_updates"] += moving and any(surface in (2, 3) and not airborne for surface, airborne in zip(surfaces, pose["wheel_airborne"]))
        for surface in surfaces:
            if surface < len(self.surfaces):
                self.surfaces[surface] += 1
        self.distance += math.dist(self.previous, pose["position"]) / ONE
        self.previous = pose["position"]

    def summary(self, scenario):
        gates = {}
        if scenario in SHUTTLE_SCENARIOS and self.counts["updates"] >= 500:
            gates = {"moving_majority": self.counts["moving_updates"] >= self.counts["updates"] * 3 // 5,
                     "travelled100m": self.distance >= 100,
                     "actual_braking": self.counts["braked_updates"] >= 50,
                     "actual_reverse": self.counts["reverse_moving_updates"] >= 50}
            if scenario != "shuttle":
                gates["actual_steering"] = self.counts["steered_moving_updates"] >= 20
            if scenario == "offroad-shuttle":
                gates["actual_offroad"] = self.counts["offroad_moving_updates"] >= 20
        return {**self.counts, "path_distance_metres": self.distance,
                "surface_wheel_samples": dict(zip(("asphalt", "curb", "grass", "gravel"), self.surfaces)),
                "gates": gates, "passed": all(gates.values())}


def register_address(output):
    numbers = re.findall(r"0x([0-9a-fA-F]{1,8})(?![0-9a-fA-F])", output)
    if not numbers:
        numbers = re.findall(r"(?<![0-9A-Za-z])([0-9a-fA-F]{8})(?![0-9a-fA-F])", output)
    if len(numbers) != 1:
        raise RuntimeError(f"Missing or ambiguous original register address: {output}")
    return int(numbers[0], 16)


def register_value(output):
    """Wine prints scalar zero as decimal, while pointers retain hex width."""
    if re.fullmatch(r"-?\d+", output.strip()):
        return int(output.strip(), 10) & 0xffffffff
    return register_address(output)


def parse_memory(output, address, size):
    """Accept only an exact, contiguous WineDbg x/Nx memory response."""
    words = []
    expected = address
    for line in output.splitlines():
        match = re.match(r"\s*(?:0x)?([0-9a-fA-F]{8})(?:\s+[^:]*)?:\s*(.*)", line)
        if not match:
            # Wine's x /Nx prints bare values, including the one-word case.
            # Some debugger builds include row addresses; validate those when
            # present and require exact widths/counts in both output forms.
            if not re.fullmatch(r"\s*(?:(?:0x)?[0-9a-fA-F]{8}\s*)+", line):
                continue
            data = line
        else:
            actual = int(match[1], 16)
            if actual != expected:
                raise RuntimeError(f"Noncontiguous debugger memory: {actual:08x}, expected {expected:08x}")
            data = match[2]
        values = re.findall(r"(?<![0-9A-Za-z])(?:0x)?([0-9a-fA-F]{8})(?![0-9a-fA-F])", data)
        if not values:
            raise RuntimeError("Debugger memory row has no complete words")
        words.extend(int(value, 16) for value in values)
        expected += len(values) * 4
    result = b"".join(struct.pack("<I", word) for word in words)
    if len(result) != size:
        raise RuntimeError(f"Debugger returned {len(result)} bytes at {address:08x}; expected {size}")
    return result


class SourceException(RuntimeError):
    """An actual exception reported by the original, before a complete update."""


class Debugger:
    def __init__(self, process, directory, deadline):
        self.process = process
        self.deadline = deadline
        self.pending = b""
        self.pause_serial = 0
        self.confirmed_pause = False
        self.log = (directory / "debugger.log").open("wb")
        self.commands = (directory / "debugger.commands").open("w")
        self.memory_fd = None

    def enable_process_memory(self, unix_pid):
        """Read the proven owned process only while WineDbg confirms its stop."""
        if not self.confirmed_pause or self.memory_fd is not None:
            raise RuntimeError("Process memory requires one confirmed source pause")
        descriptor = os.open(f"/proc/{unix_pid}/mem", os.O_RDONLY | os.O_CLOEXEC)
        try:
            # Verify the address space against WineDbg before changing readers.
            # A second probe covers live writable actor data, not just the EXE.
            for address, size in ((0x400000, 64), (LAYOUT["body_base"], 64)):
                expected = self.memory(address, size)
                if os.pread(descriptor, size, address) != expected:
                    raise RuntimeError("Owned process memory differs from the debugger")
        except BaseException:
            os.close(descriptor)
            raise
        self.memory_fd = descriptor

    def close_memory(self):
        if getattr(self, "memory_fd", None) is not None:
            os.close(self.memory_fd)
            self.memory_fd = None

    def process_memory(self, address, size):
        if not self.confirmed_pause:
            raise RuntimeError("Process memory reads require a confirmed source pause")
        raw = os.pread(self.memory_fd, size, address)
        if len(raw) != size:
            raise RuntimeError(f"Short owned process memory read at {address:08x}")
        return raw

    def read_prompt(self, timeout=15):
        until = min(self.deadline, time.monotonic() + timeout)
        while b"Wine-dbg>" not in self.pending:
            remaining = until - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Original debugger did not reach its next boundary/prompt")
            if not select.select([self.process.stdout], [], [], remaining)[0]:
                continue
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                raise RuntimeError(f"Original debugger exited: {self.process.poll()}")
            self.log.write(chunk)
            self.log.flush()
            self.pending += ANSI.sub(b"", chunk).replace(b"\r", b"")
        response, self.pending = self.pending.split(b"Wine-dbg>", 1)
        response = response.decode("utf-8", errors="replace")
        exception = re.search(r"Unhandled exception:[^\n]*", response)
        if exception:
            self.invalidate_pause()
            raise SourceException(exception.group())
        return response

    def invalidate_pause(self):
        self.pause_serial += 1
        self.confirmed_pause = False

    def confirm_pause(self):
        self.pause_serial += 1
        self.confirmed_pause = True

    def paused_memory(self):
        return PausedMemory(self)

    def send(self, command):
        # Only explicit reads and debugger breakpoint management preserve a
        # confirmed pause. Every other command may execute/change source state.
        if not re.fullmatch(r"x /[0-9]+x 0x[0-9a-f]+|print \$[a-z]+|break \*0x[0-9a-f]+|delete break [0-9]+", command):
            self.invalidate_pause()
        self.commands.write(command + "\n")
        self.commands.flush()
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()

    def command(self, command, timeout=15):
        self.send(command)
        result = self.read_prompt(timeout)
        if re.search(r"Syntax Error|Unable to|Cannot |No process|No symbols|Undefined symbol|Invalid expression", result, re.I):
            raise RuntimeError(f"Debugger rejected {command!r}: {result}")
        if command == "cont":
            while True:
                hooks = ((getattr(self, "contact_audio", None), ContactAudioRecorder),
                         (getattr(self, "brake_publication", None), BrakePublicationRecorder))
                intercepted = any(isinstance(hook, cls) and not hook.busy and hook.intercept(result) for hook, cls in hooks)
                scene = getattr(self, "scene_journal", None)
                if not intercepted and scene is not None:
                    intercepted = scene.intercept(result)
                if not intercepted:
                    break
                self.send(command)
                result = self.read_prompt(timeout)
        return result

    def memory(self, address, size):
        if size % 4:
            raise ValueError("Trace memory reads require whole words")
        if getattr(self, "memory_fd", None) is not None:
            return self.process_memory(address, size)
        return parse_memory(self.command(f"x /{size // 4}x 0x{address:08x}"), address, size)

    def memory_many(self, ranges):
        """Queue bounded x reads; each command still has its own verified prompt."""
        if not self.confirmed_pause:
            raise RuntimeError("Batch memory reads require a confirmed pause")
        ranges = list(ranges)
        if any(address & 3 or size < 4 or size % 4 for address, size in ranges):
            raise ValueError("Batch memory ranges require aligned whole words")
        results = []
        try:
            if getattr(self, "memory_fd", None) is not None:
                return [self.process_memory(address, size) for address, size in ranges]
            for start in range(0, len(ranges), 64):
                batch = ranges[start:start + 64]
                commands = [f"x /{size // 4}x 0x{address:08x}" for address, size in batch]
                payload = "\n".join(commands) + "\n"
                self.commands.write(payload)
                self.commands.flush()
                self.process.stdin.write(payload.encode())
                self.process.stdin.flush()
                # Drain every queued response before parsing; a rejected read
                # cannot leave a later response attached to the wrong address.
                responses = [self.read_prompt() for _ in commands]
                for command, response, (address, size) in zip(commands, responses, batch):
                    if re.search(r"Syntax Error|Unable to|Cannot |No process|No symbols|Undefined symbol|Invalid expression", response, re.I):
                        raise RuntimeError(f"Debugger rejected {command!r}: {response}")
                    results.append(parse_memory(response, address, size))
        except BaseException:
            self.invalidate_pause()
            raise
        return results

    def word(self, address):
        return struct.unpack("<I", self.memory(address, 4))[0]

    def breakpoint(self, address):
        response = self.command(f"break *0x{address:08x}")
        match = re.search(r"Breakpoint\s+(\d+)", response)
        if not match:
            raise RuntimeError(f"Debugger did not acknowledge breakpoint: {response}")
        return int(match[1])

    def stop_at(self, address):
        response = self.command("cont", 30)
        # WineDbg emits both breakpoint identity and instruction address.
        if f"{address:08x}" not in response.lower():
            raise RuntimeError(f"Missed boundary {address:08x}: {response}")
        self.confirm_pause()
        return response

    def snapshot(self, boundary):
        actor = self.word(0x4D37A0) & 0xff
        if actor >= 16:
            raise RuntimeError(f"Invalid local source actor: {actor}")
        result = {"boundary": boundary, "actor": actor}
        for record in ("body", "dynamics", "gameplay"):
            result[record + "_hex"] = self.memory(LAYOUT[record + "_base"] + actor * LAYOUT[record + "_stride"], LAYOUT[record + "_stride"]).hex()
        for name, address in GLOBALS.items():
            value = self.word(address)
            result[name] = bool(value) if name == "extra_update" else signed(value) if name in ("delta_q16", "reciprocal_q16", "qpc_divisor") else value
        dynamics = bytes.fromhex(result["dynamics_hex"])
        result["input_q16"] = {name: struct.unpack_from("<i", dynamics, offset)[0]
                               for name, offset in (("left", 0x138), ("right", 0x13c), ("throttle", 0x140), ("brake", 0x144))}
        result["input_q16"]["gear_edge"] = struct.unpack_from("<b", dynamics, 0x14c)[0]
        result["configuration_pointer"] = self.word(0x4B0660)
        config = result["configuration_pointer"]
        result["session_state"] = self.word(config + 0x5f8) & 0xffff
        result["source_mode"] = self.word(config + 4)
        result["allocated_actor_count"] = self.word(0x4C5D6E) & 0xffff
        result["weather"] = self.word(0x4C5D74) & 0xffff
        result["car_id"] = bytes.fromhex(result["body_hex"])[0x516]
        result["transmission_mode"] = struct.unpack_from("<I", dynamics, 0x27c)[0]
        result["limiter_active"] = self.word(0x4BE150 + actor * 4)
        result["limiter_load_q16"] = signed(self.word(0x4BE108 + actor * 4))
        result["roughness_cursor"] = self.word(0x4D5634) & 0xff
        result["track_id"] = self.word(config + 0x1c)
        result["steering_profile"] = self.word(config + 0x100)
        return result


class PausedMemory:
    """A read-through cache valid only for one confirmed stopped instruction."""

    def __init__(self, debugger):
        if not debugger.confirmed_pause:
            raise RuntimeError("Memory batching requires a confirmed source pause")
        self.debugger = debugger
        self.serial = debugger.pause_serial
        self.blocks = []

    def check(self):
        if not self.debugger.confirmed_pause or self.serial != self.debugger.pause_serial:
            raise RuntimeError("Snapshot cache crossed a source progress/stop boundary")

    def preload(self, ranges):
        self.check()
        pending = []
        for address, size in ranges:
            if not size:
                continue
            if address & 3 or size % 4 or size < 0:
                raise ValueError("Snapshot ranges require aligned whole words")
            if not any(base <= address and address + size <= base + len(raw) for base, raw in self.blocks):
                pending.append((address, address + size))
        merged = []
        for address, end in sorted(pending):
            if merged and address <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
            else:
                merged.append((address, end))
        requests = [(address, end - address) for address, end in merged]
        if not requests:
            return
        values = self.debugger.memory_many(requests)
        self.check()
        self.blocks.extend((address, raw) for (address, _), raw in zip(requests, values))

    def memory(self, address, size):
        self.check()
        if size == 0:
            return b""
        for base, raw in self.blocks:
            if base <= address and address + size <= base + len(raw):
                return raw[address - base:address - base + size]
        self.preload([(address, size)])
        return self.memory(address, size)

    def word(self, address):
        return struct.unpack("<I", self.memory(address, 4))[0]


def original_environment(environment, home, runner):
    # Existing run-reference.sh owns its serialized prefix and exact executable
    # copy. Display/audio routes remain those of our private Xvfb environment.
    environment.update(HOME=str(home), XDG_CACHE_HOME=str(home / ".cache"),
                       TOCA_WINE_RUNNER=str(runner), WINEPREFIX=str(home / ".cache/toca-1.1a/wine"),
                       WINEDEBUG="-all")
    return environment


def reference_audio_route(environment, null_sink=False):
    if not null_sink:
        return "--mute", {"mode": "drivers-disabled", "pulse_server": None}
    server = os.environ.get("PULSE_SERVER", "")
    if not server.startswith("unix:/") or not Path(server[5:]).is_absolute() or not Path(server[5:]).is_socket():
        raise ValueError("--audio-null-sink requires an explicit absolute Unix PULSE_SERVER socket")
    environment["PULSE_SERVER"] = server
    return "--audio-null-sink", {"mode": "winepulse-private-null-sink", "pulse_server": server,
                                "wine_alsa_oss_disabled": True}


def owned_game_processes(environment, proc_root=Path("/proc")):
    """Prove ownership using the unique display/runtime of this invocation."""
    result = []
    required = {name: environment[name].encode() for name in ("DISPLAY", "XDG_RUNTIME_DIR", "WINEPREFIX")}
    for process in proc_root.iterdir():
        if not process.name.isdecimal():
            continue
        try:
            if process.stat().st_uid != os.getuid() or (process / "comm").read_text().strip().lower() != "tourcars.exe":
                continue
            variables = dict(item.split(b"=", 1) for item in (process / "environ").read_bytes().split(b"\0") if b"=" in item)
            if all(variables.get(name.encode()) == value for name, value in required.items()):
                result.append(int(process.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return sorted(result)


def keyboard(environment, *arguments):
    windows = subprocess.run(["xdotool", "search", "--name", "^TOCA Touring Car Championship$"],
                             env=environment, capture_output=True, text=True, timeout=5)
    if windows.returncode == 0:
        identities = windows.stdout.splitlines()
        if len(identities) != 1:
            raise RuntimeError("Ambiguous original game window for keyboard focus")
        subprocess.run(["xdotool", "windowfocus", "--sync", identities[0]], env=environment,
                       check=True, capture_output=True, timeout=5)
    subprocess.run(["xdotool", *arguments], env=environment, check=True,
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=5)


def warmup_keys(debugger, environment, updates, keys):
    """Advance real source updates using WineDbg's breakpoint repeat count.

    This intentionally saves only endpoints: a later measured interval imports
    its warm initial world once and cannot claim continuity with the grid.
    """
    before = {name: debugger.word(GLOBALS[name]) for name in ("update_counter", "race_clock")}
    pressed = []
    started = time.monotonic()
    try:
        for key in sorted(set(keys)):
            keyboard(environment, "keydown", key)
            pressed.append(key)
        response = debugger.command(f"cont {updates}", 60)
        if "00421aa0" not in response.lower():
            raise RuntimeError(f"Warmup missed the source update boundary: {response}")
        debugger.confirm_pause()
    finally:
        for key in pressed:
            keyboard(environment, "keyup", key)
    after = {name: debugger.word(GLOBALS[name]) for name in ("update_counter", "race_clock")}
    advanced = (after["update_counter"] - before["update_counter"]) & 0xffffffff
    if advanced != updates:
        raise RuntimeError(f"Warmup advanced {advanced} source updates; requested {updates}")
    return {"keys_only": True, "requested_keys": pressed, "updates": updates,
            "before": before, "after": after, "elapsed_wall_seconds": time.monotonic() - started,
            "recorded_boundaries": False, "continuous_from_grid": False}

def warmup_step(value):
    """Parse a bounded physical-key driving stage, e.g.300:Up or40:Up,Left."""
    count, separator, names = value.partition(":")
    try:
        updates = int(count)
    except ValueError as error:
        raise argparse.ArgumentTypeError("warmup step requires UPDATES:KEY,KEY") from error
    keys = names.split(",") if names else []
    if not separator or not 1 <= updates <= 1000 or any(key not in ("Up", "Down", "Left", "Right") for key in keys):
        raise argparse.ArgumentTypeError("warmup step requires 1..1000 updates and only Up, Down, Left, Right keys")
    return updates, sorted(set(keys))


def warmup_until_ai_point(debugger, environment, target, limit=3000, finish_ready=False):
    """Observe real rival driving at421aa0-entry; preserve only endpoints."""
    descriptor = debugger.memory(0x54C120, 16)
    _, count, _, _ = FLEET.route_descriptor(descriptor)
    FLEET.integer(target, 0, count - 1, "warmup target live POINTS index")
    before = {name: debugger.word(GLOBALS[name]) for name in ("update_counter", "race_clock")}
    batches, advanced = [], 0
    configuration = debugger.word(0x4B0660) if target == 0 else None
    while True:
        raw = debugger.memory(LAYOUT["gameplay_base"], 16 * LAYOUT["gameplay_stride"])
        observed = []
        for actor in range(16):
            game = raw[actor * 0x124:(actor + 1) * 0x124]
            observed.append({"actor": actor, "route_next": struct.unpack_from("<h", game, 0xa2)[0],
                             "route_previous": struct.unpack_from("<h", game, 0xa4)[0],
                             "human": struct.unpack_from("<I", game, 8)[0],
                             "player_identity": struct.unpack_from("<b", game, 0x80)[0],
                             "backwards": struct.unpack_from("<I", game, 0xc)[0],
                             "active": struct.unpack_from("<I", game, 4)[0] != 0,
                             "checkpoint": game[0xa6], "checkpoint_marker": game[0x9d],
                             "lap": struct.unpack_from("<h", game, 0xa8)[0],
                             "finished": struct.unpack_from("<I", game, 0x54)[0]})
        rivals = [actor for actor in observed if actor["active"] and actor["player_identity"] == -1
                  and actor["human"] == 0 and actor["backwards"] == 0 and 0 <= actor["route_next"] < count]
        lap_context = None
        if target == 0:
            stage = debugger.memory(configuration + 0x5f4, 8)
            lap_context = {"session_stage": struct.unpack_from("<h", stage, 2)[0],
                           "session_state": struct.unpack_from("<h", stage, 4)[0],
                           "lap_limit": debugger.word(0x4C5D6C) & 0xffff}
            ready = lap_context["session_stage"] in (2, 4) and lap_context["session_state"] == 6
            rivals = [actor for actor in rivals if ready and actor["checkpoint"] == 0 and actor["checkpoint_marker"] == 0
                      and actor["finished"] == 0 and actor["lap"] >= 1
                      and (actor["lap"] == lap_context["lap_limit"] if finish_ready else actor["lap"] < lap_context["lap_limit"])]
        selected = [actor["actor"] for actor in rivals if actor["route_next"] == target]
        if selected:
            break
        if advanced >= limit:
            raise RuntimeError(f"No ordinary rival reached live POINTS index {target} within {limit} actual warmup updates")
        near = any((target - actor["route_next"]) % count <= 8 for actor in rivals)
        step = min(1 if near else 25, limit - advanced)
        warmup_keys(debugger, environment, step, [])
        batches.append(step)
        advanced += step
    after = {name: debugger.word(GLOBALS[name]) for name in ("update_counter", "race_clock")}
    return {"kind": "observed-ai-point", "target_point": target, "maximum_updates": limit,
            "updates": advanced, "advance_batches": batches, "requested_keys": [], "keys_only": True,
            "before": before, "after": after, "endpoint_boundary": "421aa0-entry",
            "endpoint_actors": observed, "selected_rivals": selected, "route_descriptor_hex": descriptor.hex(),
            "target_filter": ("finish-ready checkpoint0" if finish_ready else "lap-ready nonfinish checkpoint0") if target == 0 else "ordinary forward rival",
            "lap_context": lap_context,
            "recorded_boundaries": False, "continuous_from_grid": False}


def menu_key(debugger, environment, address, key):
    keyboard(environment, "keydown", key)
    debugger.stop_at(address)
    keyboard(environment, "keyup", key)
    # Inputs are polled before handler entry. This entry observes the prior
    # handler consuming the held key, then its release; no source writes.
    debugger.stop_at(address)


def select_session(debugger, environment, session):
    """Reach RaceMenu through Return, then select the authored session row."""
    if session not in ("single-race", "time-trial"):
        raise RuntimeError("Unsupported reference session selection")
    target = 0 if session == "single-race" else 3
    result = {"kind": "session-selection", "requested_session": session, "keys": []}
    for _ in range(30):
        stack = register_address(debugger.command("print $esp"))
        menu = debugger.word(stack + 4)
        if menu == 0x4B6D40:
            break
        menu_key(debugger, environment, 0x46E6C2, "Return")
        result["keys"].append({"key": "Return", "menu_address": menu})
    else:
        raise RuntimeError("Original frontend did not reach RaceMenu")
    if debugger.word(0x52C21C) != 0xb:
        raise RuntimeError("Original session selection stopped outside RaceMenu")
    for _ in range(5):
        selection = (debugger.word(0x4B6D68) >> 8) & 0xff
        if selection == target:
            result["final_row"] = selection
            return result
        if selection not in range(5):
            raise RuntimeError("Invalid original RaceMenu row")
        # 46e6c2 skips disabled rows, notably the default locked Showdown2.
        menu_key(debugger, environment, 0x46E6C2, "Down")
        observed = (debugger.word(0x4B6D68) >> 8) & 0xff
        if observed == selection:
            raise RuntimeError("Original did not consume RaceMenu Down key")
        result["keys"].append({"key": "Down", "observed_row": observed})
    raise RuntimeError("Original RaceMenu did not reach " + session)


def select_time_trial(debugger, environment):
    return select_session(debugger, environment, "time-trial")


def weather_keyboard_steps(selection, current, target, session="single-race"):
    rows, weather, continuation = (3, 0, 2) if session == "time-trial" else (5, 3, 4)
    if session not in ("single-race", "time-trial") or selection not in range(rows) or current not in range(6) or target not in range(6):
        raise RuntimeError("Invalid original weather-menu selection")
    # 469713: five rows, weather3/Continue4. 469979: three rows,
    # weather0/Continue2. 47284c wraps six real choices, including DEFAULT0.
    after_weather = ["Up"] if session == "time-trial" else ["Down"]
    return ["Up"] * ((selection - weather) % rows) + ["Right"] * ((target - current) % 6) + after_weather


def select_qualifying(debugger, environment, enabled):
    """Set source Single Race row2 through real keys (42be90 toggles +20)."""
    handler, menu = 0x469713, 0x4B6EC0
    for _ in range(120):
        if debugger.word(0x52EE18) == 1:
            break
        debugger.stop_at(handler)
    else:
        raise RuntimeError("Original options transition did not complete")
    if debugger.word(0x52C21C) != 0xd:
        raise RuntimeError("Original qualifying selection stopped outside race options")
    config = debugger.word(0x4B0660)
    if debugger.word(config + 4) != 0 or debugger.word(config + 0xac) != 1:
        raise RuntimeError("Reference capture requires original one-player Single Race")
    selection = (debugger.word(menu + 0x28) >> 8) & 0xff
    if selection not in range(5) or debugger.word(config + 0x20) not in (0, 1):
        raise RuntimeError("Invalid original qualifying menu state")
    target = int(not enabled)
    result = {"kind": "qualifying-selection", "requested_qualifying": enabled,
              "initial_row": selection, "initial_flag": debugger.word(config + 0x20), "keys": []}
    for _ in range((selection - 2) % 5):
        menu_key(debugger, environment, handler, "Up")
        selection = (selection - 1) % 5
        if (debugger.word(menu + 0x28) >> 8) & 0xff != selection:
            raise RuntimeError("Original did not consume qualifying menu navigation")
        result["keys"].append({"key": "Up", "observed_row": selection})
    if debugger.word(config + 0x20) != target:
        menu_key(debugger, environment, handler, "Right")
        result["keys"].append({"key": "Right", "observed_flag": debugger.word(config + 0x20)})
    if debugger.word(config + 0x20) != target:
        raise RuntimeError("Original did not consume requested qualifying selection")
    result["final_flag"] = target
    return result


def select_no_qualifying(debugger, environment):
    return select_qualifying(debugger, environment, False)


def select_laps(debugger, environment, laps):
    """4694f0 row1;4728b0/472ad6 select the actual menu lap count."""
    choices = (1, 2, 3, 5, 10, 15, 20)
    if laps not in choices:
        raise ValueError("Unsupported original menu lap count")
    config = debugger.word(0x4B0660)
    if debugger.word(0x52C21C) != 0xd or debugger.word(config + 4) != 0:
        raise RuntimeError("Lap selection requires the original Single Race options")
    selection = (debugger.word(0x4B6EC0 + 0x28) >> 8) & 0xff
    current = debugger.word(config + 0x14) & 0xff
    if selection not in range(5) or current not in range(7):
        raise RuntimeError("Invalid original lap menu state")
    result = {"kind": "lap-selection", "requested_laps": laps, "initial_choice": current, "keys": []}
    for _ in range((selection - 1) % 5):
        menu_key(debugger, environment, 0x469713, "Up")
        selection = (selection - 1) % 5
        if (debugger.word(0x4B6EC0 + 0x28) >> 8) & 0xff != selection:
            raise RuntimeError("Original did not consume lap menu navigation")
        result["keys"].append({"key": "Up", "observed_row": selection})
    target = choices.index(laps)
    key, direction = ("Left", -1) if (current - target) % 7 <= (target - current) % 7 else ("Right", 1)
    while current != target:
        menu_key(debugger, environment, 0x469713, key)
        current = (current + direction) % 7
        if debugger.word(config + 0x14) & 0xff != current or debugger.word(config + 0x18) != choices[current]:
            raise RuntimeError("Original did not consume requested lap menu selection")
        result["keys"].append({"key": key, "observed_laps": choices[current]})
    result["final_laps"] = choices[current]
    return result


def select_weather(debugger, environment, target, session="single-race"):
    handler, menu, state, rows, continuation = (0x469979, 0x4B71C0, 0xe, 3, 2) if session == "time-trial" else (0x469713, 0x4B6EC0, 0xd, 5, 4)
    # The actual authored options handler sets52ee18 once its transition ends.
    for _ in range(120):
        if debugger.word(0x52EE18) == 1:
            break
        debugger.stop_at(handler)
    else:
        raise RuntimeError("Original options transition did not complete")
    if debugger.word(0x52C21C) != state:
        raise RuntimeError("Original weather selection stopped outside its session options")
    config = debugger.word(0x4B0660)
    current = debugger.word(config + 0xa0)
    selection = (debugger.word(menu + 0x28) >> 8) & 0xff
    target = current if target is None else target
    result = {"kind": "weather-selection", "session": session, "requested_choice": target,
              "initial_choice": current, "initial_row": selection, "keys": []}
    for key in weather_keyboard_steps(selection, current, target, session):
        menu_key(debugger, environment, handler, key)
        observed_selection = (debugger.word(menu + 0x28) >> 8) & 0xff
        observed_choice = debugger.word(config + 0xa0)
        expected_selection = (selection - 1) % rows if key == "Up" else (selection + 1) % rows if key == "Down" else selection
        expected_choice = (current + 1) % 6 if key == "Right" else current
        if (observed_selection, observed_choice) != (expected_selection, expected_choice):
            raise RuntimeError(f"Original did not consume weather-menu key {key}: row{observed_selection}, weather{observed_choice}")
        result["keys"].append({"key": key, "observed_row": observed_selection, "observed_choice": observed_choice})
        selection, current = observed_selection, observed_choice
    if selection != continuation or current != target:
        raise RuntimeError("Original weather-menu selection did not reach Continue with the requested option")
    result["final_choice"] = current
    return result


def handoff_snapshot(debugger, actor, kind, phase):
    reader = debugger.paused_memory()
    game = LAYOUT["gameplay_base"] + actor * LAYOUT["gameplay_stride"]
    dynamics = LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"]
    reader.preload([(game, 0x124), (dynamics, 0x338), (0x4b9e78, 4), (0x4c3b88, 4)])
    return {"boundary": f"{FLEET.LOCAL_HANDOFF_CALLS[kind]['entry']:x}-{phase}",
            "gameplay_hex": reader.memory(game, 0x124).hex(), "dynamics_hex": reader.memory(dynamics, 0x338).hex(),
            "rng": reader.word(0x4b9e78), "ai_tire_base": struct.unpack("<i", reader.memory(0x4c3b88, 4))[0]}


def nearest_recovery_snapshot(debugger, phase):
    frame = register_address(debugger.command("print $ebp"))
    cursor = register_address(debugger.command("print $edi"))
    reader = debugger.paused_memory()
    reader.preload([(frame - 4, 16), (0x4bcce0, 256), (GLOBALS["rng"], 4)])
    actor = reader.word(frame + 8)
    FLEET.integer(actor, 0, 15, "nearest recovery source actor")
    body = LAYOUT["body_base"] + actor * 0x53c
    game = LAYOUT["gameplay_base"] + actor * 0x124
    position = reader.word(frame - 4)
    if (cursor, position) != (game + 0xa2, body + 0xbc):
        raise ValueError("Unexpected nearest recovery source frame/register context")
    reader.preload([(body, 0x53c), (game, 0x124)])
    pc = FLEET.NEAREST_RECOVERY_MANIFEST["entry" if phase == "pre" else "exit"]
    return {"boundary": f"{pc:x}-{'entry' if phase == 'pre' else 'exit'}", "actor_index": actor,
            "cursor_pointer": cursor, "position_pointer": position, "body_hex": reader.memory(body, 0x53c).hex(),
            "gameplay_hex": reader.memory(game, 0x124).hex(), "nearest_map_hex": reader.memory(0x4bcce0, 256).hex(),
            "rng": reader.word(GLOBALS["rng"])}


def start_call_snapshot(debugger, entry, phase):
    reader = debugger.paused_memory()
    reader.preload([(0x4b0660, 4), (0x4d37b0, 4), (GLOBALS["rng"], 4), (0x4d3838, 16)])
    config = reader.word(0x4b0660)
    reader.preload([(config + 4, 4), (config + 0xac, 4), (config + 0x5f4, 8)])
    return {"boundary": f"{entry:x}-{'entry' if phase == 'pre' else 'return'}", "release_counter": reader.word(0x4d37b0),
            "rng": reader.word(GLOBALS["rng"]), "race_clock": reader.word(0x4d3840), "release_epoch": reader.word(0x4d3844),
            "source_mode": reader.word(config + 4), "session_stage": struct.unpack("<H", reader.memory(config + 0x5f6, 2))[0],
            "local_players": reader.word(config + 0xac)}


def capture_start_call(debugger, entry, cold_grid=None):
    if cold_grid is not None and (entry != 0x460170 or cold_grid):
        raise ValueError("Cold-grid evidence requires one actual460170 call")
    caller = debugger.word(register_address(debugger.command("print $esp")))
    if not 0x400000 <= caller < 0x4b0000:
        raise ValueError("Source start-call return address outside executable")
    if cold_grid is not None:
        cold_grid["pre"] = FLEET.capture_cold_grid_boundary(debugger.paused_memory(), "460170-entry")
    event = {"return_address": caller, "pre": start_call_snapshot(debugger, entry, "pre")}
    point = debugger.breakpoint(caller)
    debugger.stop_at(caller)
    if cold_grid is not None:
        cold_grid["post"] = FLEET.capture_cold_grid_boundary(debugger.paused_memory(), "460170-return")
    event["post"] = start_call_snapshot(debugger, entry, "post")
    debugger.command(f"delete break {point}")
    FLEET.validate_start_call(event, entry)
    if cold_grid is not None:
        FLEET.validate_cold_grid_initialization(cold_grid, event)
    return event


class ContactAudioRecorder:
    """Intercept only actual source audio calls while preserving caller breakpoints."""
    def __init__(self, debugger):
        self.debugger = debugger
        self.phase = "pre_to_ai"
        self.outer = None
        self.busy = False
        self.serial = 0
        self.calls = []
        self.points = [debugger.breakpoint(pc) for pc in (0x424860, 0x4250a0)]
        debugger.contact_audio = self

    def order(self):
        self.serial += 1
        return self.serial

    def intercept(self, response):
        matches = [pc for pc in (0x424860, 0x4250a0) if f"{pc:08x}" in response.lower()]
        if not matches: return False
        if len(matches) != 1 or self.outer is not None:
            raise ValueError("Unsupported nested source contact audio chronology: " + str(self.outer))
        self.busy = True
        self.debugger.confirm_pause()
        try:
            event = capture_contact_audio_call(self.debugger, matches[0])
            event.update(order=self.order(), phase=self.phase)
            self.calls.append({"kind": "impact" if matches[0] == 0x424860 else "scrape", "event": event})
        finally:
            self.busy = False
        return True

    def close(self):
        self.debugger.contact_audio = None
        for point in self.points: self.debugger.command(f"delete break {point}")


class BrakePublicationRecorder:
    """Actual descending publication loop, then separately ordered tail consumers."""
    def __init__(self, debugger):
        self.debugger = debugger
        self.busy = False
        self.phase = "pre_to_ai"
        self.serial = 0
        self.call = 0
        self.calls = []
        self.point = debugger.breakpoint(0x4608b0)
        debugger.brake_publication = self

    def order(self):
        self.serial += 1
        return self.serial

    def intercept(self, response):
        if "004608b0" not in response.lower(): return False
        contact = getattr(self.debugger, "contact_audio", None)
        phase = contact.phase if isinstance(contact, ContactAudioRecorder) else self.phase
        if phase in ("ai_route", "ai_to_consumed") or isinstance(contact, ContactAudioRecorder) and (contact.busy or contact.outer is not None):
            raise ValueError("Unsupported nested brake publication chronology: " + phase)
        self.busy = True
        points = []
        self.debugger.confirm_pause()
        try:
            returned = self.debugger.word(register_address(self.debugger.command("print $esp")))
            if not 0x400000 <= returned < 0x4b0000:
                raise ValueError("Brake publication return outside executable")
            event = {"call": self.call, "phase": phase, "order": contact_order(self.debugger),
                     "return_address": returned,
                     "pre": FLEET.capture_brake_publication(self.debugger.paused_memory(), "4608b0-entry")}
            self.call += 1
            for pc in (0x4610ed, returned): points.append(self.debugger.breakpoint(pc))
            self.debugger.stop_at(0x4610ed)
            event["stores"] = FLEET.capture_brake_publication(self.debugger.paused_memory(), "4610ed")
            self.calls.append({"kind": "publication", "event": event})
            self.debugger.stop_at(returned)
            event["post"] = FLEET.capture_brake_publication(self.debugger.paused_memory(), "4608b0-return")
            self.calls.append({"kind": "publication_return", "event": {"call": event["call"], "phase": phase,
                               "order": contact_order(self.debugger), "post": event["post"]}})
            for name, boundary in (("pre", "4608b0-entry"), ("stores", "4610ed"), ("post", "4608b0-return")):
                FLEET.validate_brake_publication(event[name], boundary)
        finally:
            for point in points: self.debugger.command(f"delete break {point}")
            self.busy = False
        return True

    def close(self):
        self.debugger.brake_publication = None
        self.debugger.command(f"delete break {self.point}")


def set_consumer_phase(debugger, phase):
    for name, cls in (("contact_audio", ContactAudioRecorder), ("brake_publication", BrakePublicationRecorder)):
        hook = getattr(debugger, name, None)
        if isinstance(hook, cls): hook.phase = phase


def contact_order(debugger):
    hook = getattr(debugger, "contact_audio", None)
    if isinstance(hook, ContactAudioRecorder): return hook.order()
    publication = getattr(debugger, "brake_publication", None)
    return publication.order() if isinstance(publication, BrakePublicationRecorder) else None


def capture_contact_audio_call(debugger, address):
    stack = register_address(debugger.command("print $esp"))
    returned, first, second, third, fourth = struct.unpack("<5I", debugger.memory(stack, 20))
    if address == 0x424860:
        position, kind, strength_pointer, local = first, signed(second), third, fourth
        handle = -1
    else:
        handle, position, kind, strength_pointer = signed(first), second, signed(third), fourth
        local = 1
    if not 0x400000 <= returned < 0x4b0000 or not 0 <= kind <= 27:
        raise ValueError("Unexpected original impact/scrape call arguments")
    reader = debugger.paused_memory()
    result = {"return_address": returned, "kind": kind, "handle": handle, "local": local,
              "position_hex": reader.memory(position, 12).hex(), "strength": signed(reader.word(strength_pointer)),
              "pre": FLEET.capture_contact_context(reader), "queries": [], "commands": [], "sample": None}
    # These instructions expose actual numerical outputs before mixer allocation.
    pcs = (returned, 0x424921, 0x4250e7, 0x4249b9, 0x425121, 0x4249d0, 0x425134,
           0x424a22, 0x424a1d, 0x42515f, 0x42518c, 0x4795f6, 0x4798cb, 0x4795cf, 0x479afc, 0x479b3d, 0x479b21)
    points = {pc: debugger.breakpoint(pc) for pc in dict.fromkeys(pcs)}
    pending_query = None
    pending_play = None
    try:
        for _ in range(256):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            matched = [pc for pc in points if f"{pc:08x}" in response]
            if len(matched) != 1: raise ValueError("Unexpected source contact audio stop: " + response)
            pc = matched[0]
            if pending_play is not None:
                if pc != pending_play["return_address"]: raise ValueError("Contact play wrapper missed its actual return")
                pending_play["return_eax"] = register_value(debugger.command("print $eax"))
                pending_play["allocation_succeeded"] = int(pending_play["return_eax"] != 0)
                pending_play["voice_handle_after"] = debugger.word(pending_play["target"])
                debugger.command(f"delete break {points.pop(pc)}")
                pending_play = None
                continue
            if pending_query is not None:
                if pc != pending_query["return_address"]: raise ValueError("Contact playing query missed its return")
                pending_query["playing"] = int(register_value(debugger.command("print $eax")) != 0)
                result["queries"].append(pending_query)
                debugger.command(f"delete break {points.pop(pc)}")
                pending_query = None
                continue
            if pc == returned:
                result["post"] = FLEET.capture_contact_context(debugger.paused_memory())
                return result
            if pc in (0x424921, 0x4250e7):
                frame = register_address(debugger.command("print $ebp"))
                accepted = int(register_value(debugger.command("print $eax")) != 0)
                volume_at, angle_at = (frame + 0x10, frame - 4) if address == 0x424860 else (frame - 4, frame + 8)
                result["spatial"] = {"accepted": accepted, "volume": signed(debugger.word(volume_at)) if accepted else -1,
                                     "angle": signed(debugger.word(angle_at)) if accepted else -1}
            elif pc == 0x4249b9:
                result["sample"] = register_value(debugger.command("print $esi"))
            elif pc == 0x425121:
                result["sample"] = 50  # actual executed kind10 discarded-draw instruction
                result["discarded_draw_pc"] = pc
            elif pc in (0x4249d0, 0x425134):
                if kind == 11: result["sample"] = 11
                result["gain"] = signed(register_value(debugger.command("print $eax")))
            elif pc in (0x424a1d, 0x42515f, 0x42518c):
                frame = register_address(debugger.command("print $ebp"))
                if pc != 0x42518c:
                    result["volume"] = signed(debugger.word(frame + 0x10 if pc == 0x424a1d else frame - 4))
                if pc != 0x42515f:
                    result["pan"] = signed(register_value(debugger.command("print $ebx" if pc == 0x424a1d else "print $edi")))
            elif pc == 0x424a22:
                result["allocation"] = register_value(debugger.command("print $eax"))
            else:
                stack = register_address(debugger.command("print $esp"))
                if pc == 0x4795f6:
                    ret, target = struct.unpack("<2I", debugger.memory(stack, 8))
                    if ret in points: raise ValueError("Ambiguous source contact query return")
                    pending_query = {"return_address": ret, "target": target}
                    points[ret] = debugger.breakpoint(ret)
                elif pc == 0x4798cb:
                    ret, sample, loops, volume, offset, pan, target = struct.unpack("<7I", debugger.memory(stack, 28))
                    result["commands"].append({"kind": "play", "return_address": ret, "sample": signed(sample), "loops": signed(loops),
                                               "volume": signed(volume), "offset": signed(offset), "pan": signed(pan), "target": target})
                    if kind == 25: result["sample"] = signed(sample)
                    if ret in points: raise ValueError("Ambiguous actual contact play return")
                    pending_play = result["commands"][-1]
                    points[ret] = debugger.breakpoint(ret)
                elif pc == 0x4795cf:
                    ret, target = struct.unpack("<2I", debugger.memory(stack, 8))
                    result["commands"].append({"kind": "stop", "return_address": ret, "target": target})
                else:
                    ret, target, value = struct.unpack("<3I", debugger.memory(stack, 12))
                    result["commands"].append({"kind": {0x479afc: "pan", 0x479b3d: "volume", 0x479b21: "rate"}[pc],
                                               "return_address": ret, "target": target, "value": signed(value)})
        raise ValueError("Too many original contact audio nested events")
    finally:
        for point in points.values(): debugger.command(f"delete break {point}")


def detachment_approach_snapshot(debugger):
    reader = debugger.paused_memory()
    reader.preload([(0x4d37a0, 4), (0x4b0660, 4), (0x4be220, 4), (0x4bf8a8, 10 * 0x11c)])
    actor = reader.word(0x4d37a0) & 255
    FLEET.integer(actor, 0, 15, "detachment warmup local actor")
    configuration = reader.word(0x4b0660)
    body = LAYOUT["body_base"] + actor * LAYOUT["body_stride"]
    dynamics = LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"]
    reader.preload([(configuration + 0x5f8, 4), (body, LAYOUT["body_stride"]), (dynamics, LAYOUT["dynamics_stride"])])
    return {"actor": actor, "body_hex": reader.memory(body, LAYOUT["body_stride"]).hex(),
            "dynamics_hex": reader.memory(dynamics, LAYOUT["dynamics_stride"]).hex(),
            "session_state": struct.unpack("<h", reader.memory(configuration + 0x5f8, 2))[0],
            "shared": {"detached_count": reader.word(0x4be220), "detached_pool_hex": reader.memory(0x4bf8a8, 10 * 0x11c).hex()}}


def warmup_detachment_approach(debugger, environment, limit=1000):
    """Retain the actual keys-only controller from grid to five ticks before its turn."""
    snapshot = detachment_approach_snapshot(debugger)
    policy = FleetDetachmentPolicy(snapshot)
    before = {name: debugger.word(GLOBALS[name]) for name in ("update_counter", "race_clock")}
    held, phases = set(), {}
    try:
        for advanced in range(limit + 1):
            if policy.released_updates >= 45:
                return policy, {"kind": "retained-fleet-detachment-approach", "updates": advanced, "maximum_updates": limit,
                                "released_updates": policy.released_updates, "requested_released_updates": 45,
                                "keys_only": True, "source_policy_parameters": policy.parameters, "phase_counts": phases,
                                "before": before, "after": {name: debugger.word(GLOBALS[name]) for name in before},
                                "endpoint_boundary": "421aa0-entry", "recorded_boundaries": False, "continuous_from_grid": False}
            if advanced == limit: break
            desired, decision = policy.choose(advanced, snapshot)
            if policy.spawned or decision["phase"] not in ("held-grid", "accelerate"):
                raise ValueError("Detachment approach reached an impact/turn before its selected warm endpoint")
            phases[decision["phase"]] = phases.get(decision["phase"], 0) + 1
            for key in sorted(held - desired): keyboard(environment, "keyup", key)
            for key in sorted(desired - held): keyboard(environment, "keydown", key)
            held = desired
            debugger.stop_at(0x421aa0)
            snapshot = detachment_approach_snapshot(debugger)
    finally:
        for key in sorted(held): keyboard(environment, "keyup", key)
    raise RuntimeError("Detachment keys-only approach did not reach45 released updates within its warmup bound")


def detached_expiry_snapshot(debugger):
    snapshot = detachment_approach_snapshot(debugger)
    reader = debugger.paused_memory()
    reader.preload([(0x4b0660, 4), (0x4be2c8, 40), (0x597e90, 256),
                    (0x597fa4, 4), (0x597fb0, 4), (0x4c5d74, 4),
                    *[(GLOBALS[name], 4) for name in ("update_counter", "race_clock")]])
    config = reader.word(0x4b0660)
    reader.preload([(config + 0x1c, 8), (config + 0x100, 4)])
    snapshot.update({name: reader.word(GLOBALS[name]) for name in ("update_counter", "race_clock")})
    snapshot["initial_configuration"] = {
        "track_id": reader.word(config + 0x1c), "weather": reader.word(0x4c5d74) & 0xffff,
        "car_id": bytes.fromhex(snapshot["body_hex"])[0x516],
        "transmission_mode": struct.unpack_from("<I", bytes.fromhex(snapshot["dynamics_hex"]), 0x27c)[0],
        "steering_profile": reader.word(config + 0x100)}
    snapshot["noqual"] = reader.word(config + 0x20)
    snapshot["shared"].update(
        detached_free_hex=reader.memory(0x4be2c8, 40).hex(),
        sector_visibility_hex=reader.memory(0x597e90, 256).hex(),
        sector_visibility_disabled=reader.word(0x597fa4),
        sector_visibility_override=reader.word(0x597fb0))
    return snapshot


def detached_expiry_endpoint(snapshot, slot):
    shared = snapshot["shared"]
    pool = bytes.fromhex(shared["detached_pool_hex"])
    offset = slot * 0x11c
    return {"active": struct.unpack_from("<I", pool, offset + 0x108)[0],
            "free": struct.unpack_from("<I", bytes.fromhex(shared["detached_free_hex"]), slot * 4)[0],
            "resting": struct.unpack_from("<I", pool, offset + 0x118)[0],
            "timer": struct.unpack_from("<i", pool, offset + 0x110)[0],
            "count": shared["detached_count"],
            "sector": struct.unpack_from("<I", pool, offset + 0x114)[0]}


def warmup_until_detached_expiry(debugger, environment, limit=1400):
    """Drive the existing wall policy, then coast to a real resting timer2.

    Only physical keys advance the source. Batched resting observations require
    an exact timer decrement; they do not stand in for recorded physics phases.
    """
    snapshot = detached_expiry_snapshot(debugger)
    recipe = {"track_id": 0, "weather": 1, "car_id": 7,
              "transmission_mode": 2, "steering_profile": 2}
    if (snapshot["initial_configuration"] != recipe or snapshot["noqual"] != 1
            or snapshot["session_state"] != 9 or snapshot["update_counter"] != 0
            or snapshot["shared"]["detached_count"] != 0
            or any(detached_expiry_endpoint(snapshot, slot)["active"]
                   or detached_expiry_endpoint(snapshot, slot)["free"] != 1 for slot in range(10))):
        raise ValueError("Detached expiry warmup requires the empty car7 sunny Donington automatic NoQual grid")
    policy = FleetDetachmentPolicy(snapshot, DetachmentApproachGate(2211506, 53635, "a9135ddd", 62))
    before = {name: snapshot[name] for name in ("update_counter", "race_clock")}
    phases, batches = {}, []
    selected, source_type, selected_count, first_rest_timer = None, None, None, None
    advanced, previous_timer, previous_step = 0, None, None
    blocked_updates, previous_position = 0, body_values(snapshot)["position"]
    while True:
        if snapshot["initial_configuration"] != recipe or snapshot["noqual"] != 1 or snapshot["session_state"] not in (9, 6):
            raise ValueError("Detached expiry warmup left its ordinary source configuration")
        pool = bytes.fromhex(snapshot["shared"]["detached_pool_hex"])
        active = [slot for slot in range(10) if detached_expiry_endpoint(snapshot, slot)["active"]]
        if selected is None and active:
            if len(active) != 1 or snapshot["shared"]["detached_count"] != 1:
                raise ValueError("Detached expiry warmup requires one unambiguous first live part")
            selected = active[0]
            source_type = pool[selected * 0x11c + 0x10c]
            selected_count = snapshot["shared"]["detached_count"]
        endpoint = None if selected is None else detached_expiry_endpoint(snapshot, selected)
        if endpoint is not None:
            if (not endpoint["active"] or endpoint["free"] != 0 or endpoint["count"] != selected_count
                    or pool[selected * 0x11c + 0x10c] != source_type):
                raise ValueError("Selected detached part retired, disappeared, or changed before expiry endpoint")
            if endpoint["resting"]:
                if not FLEET.sector_visible(endpoint["sector"], snapshot["shared"]):
                    raise ValueError("Selected detached part became invisible before expiry endpoint")
                if first_rest_timer is None:
                    first_rest_timer = endpoint["timer"]
                if previous_timer is not None and endpoint["timer"] != previous_timer - previous_step:
                    raise ValueError("Detached resting timer did not decrement once per source update")
                if endpoint["timer"] < 2:
                    raise ValueError("Selected detached part passed the requested expiry endpoint")
                if endpoint["timer"] == 2:
                    return policy, {"kind": "observed-detached-expiry-ready", "slot": selected, "type": source_type,
                                    "requested_timer": 2, "first_rest_timer": first_rest_timer,
                                    "updates": advanced, "maximum_updates": limit, "keys_only": True,
                                    "before": before, "after": {name: snapshot[name] for name in before},
                                    "initial_configuration": recipe, "source_policy_parameters": policy.parameters,
                                    "phase_counts": phases, "batches": batches, "endpoint": endpoint,
                                    "endpoint_boundary": "421aa0-entry", "recorded_boundaries": False,
                                    "continuous_from_grid": False}
            elif previous_timer is not None:
                raise ValueError("Selected detached part stopped resting before expiry endpoint")
        if advanced == limit:
            raise RuntimeError("Detached part did not reach resting timer2 within source warmup bound")
        keys, decision = policy.choose(advanced, snapshot)
        pose = body_values(snapshot)
        displacement_squared = sum((pose["position"][axis] - previous_position[axis]) ** 2 for axis in (0, 2))
        blocked = (selected is None and decision.get("approach_ready", False)
                   and pose["speed"] < 0x1000 and displacement_squared < 0x1000 ** 2)
        blocked_updates = blocked_updates + 1 if blocked else 0
        if blocked_updates >= 100:
            raise RuntimeError("Detached expiry approach remained blocked for100 source updates without a live part")
        previous_position = pose["position"]
        step = min(32, endpoint["timer"] - 2) if endpoint is not None and endpoint["resting"] else 1
        step = min(step, limit - advanced)
        if selected is not None and keys:
            raise ValueError("Detached expiry warmup must coast after observing the selected part")
        phases[decision["phase"]] = phases.get(decision["phase"], 0) + step
        previous_timer = endpoint["timer"] if endpoint is not None and endpoint["resting"] else None
        previous_step = step
        batches.append(warmup_keys(debugger, environment, step, keys))
        advanced += step
        snapshot = detached_expiry_snapshot(debugger)


def capture_cold_grid_sequence_call(debugger, events, outer):
    if outer is None or len(events) >= 2:
        raise ValueError("Cold-grid sequence omitted outer caller or observed an extra initializer")
    expected = FLEET.COLD_GRID_SEQUENCE_MANIFEST["outer_return_addresses"][len(events)]
    if outer != expected:
        raise ValueError("Cold-grid sequence unexpected actual447980 caller")
    boundaries = {}
    legacy = capture_start_call(debugger, 0x460170, cold_grid=boundaries)
    event = {"ordinal": len(events), "outer_entry": 0x447980, "outer_return_address": outer,
             "initialization": legacy, "boundaries": boundaries}
    events.append(event)
    return event


def capture_cold_timer_creation(debugger, sequence):
    """Read the actual creator clock after loader init and before state9 init."""
    manifest = FLEET.COLD_TIMER_CREATION_MANIFEST
    if len(sequence["events"]) != 1 or sequence.get("timer_creation") is not None:
        raise ValueError("Cold timer creator must occur once between the two initializers")
    caller = debugger.word(register_address(debugger.command("print $esp")))
    if caller != manifest["caller_return_address"]:
        raise ValueError("Unexpected cold timer creation caller")
    point = debugger.breakpoint(manifest["sample_pc"])
    try:
        debugger.stop_at(manifest["sample_pc"])
        creation_clock = register_value(debugger.command("print $eax"))
    finally:
        debugger.command(f"delete break {point}")
    event = {name: value for name, value in manifest.items() if name != "scope"}
    event["creation_clock"] = creation_clock
    sequence["timer_creation"] = FLEET.validate_cold_timer_creation(event)
    return event


class QualifyingStartupRecorder:
    """Read-only initializer boundaries; stops before the first physical call."""
    def __init__(self, debugger, owned_pid=None):
        self.debugger, self.owned_pid = debugger, owned_pid
        self.events = []
        manifest = FLEET.QUALIFYING_STARTUP_MANIFEST
        self.points = {pc: debugger.breakpoint(pc) for pc in (manifest["entry"], manifest["timer_entry"])}

    def intercept(self, response):
        matches = [pc for pc in self.points if f"{pc:08x}" in response.lower()]
        if not matches: return False
        if len(matches) != 1: raise ValueError("Ambiguous qualifying startup boundary")
        pc, d = matches[0], self.debugger
        d.confirm_pause()
        if self.owned_pid is not None and d.memory_fd is None: d.enable_process_memory(self.owned_pid)
        manifest = FLEET.QUALIFYING_STARTUP_MANIFEST
        caller = d.word(register_address(d.command("print $esp")))
        if pc == manifest["entry"]:
            if len(self.events) not in (0, 2): raise ValueError("Unexpected extra qualifying initializer")
            ordinal = len(self.events) // 2
            if caller != manifest["returns"][ordinal]: raise ValueError("Unexpected qualifying startup caller")
            event = {"kind": "initializer", "ordinal": ordinal, "return_address": caller,
                     "pre": FLEET.capture_qualifying_startup_boundary(d.paused_memory(), "443f60-entry")}
            point = d.breakpoint(caller)
            try:
                d.stop_at(caller)
                event["post"] = FLEET.capture_qualifying_startup_boundary(d.paused_memory(), "443f60-return")
            finally: d.command(f"delete break {point}")
            self.events.append(event)
        else:
            if len(self.events) != 1 or caller != manifest["timer_return"]:
                raise ValueError("Qualifying timer creator is outside its actual initializer interval")
            point = d.breakpoint(manifest["timer_sample"])
            try:
                d.stop_at(manifest["timer_sample"])
                creation = register_value(d.command("print $eax"))
            finally: d.command(f"delete break {point}")
            self.events.append({"kind": "timer_creation", "return_address": caller, "creation_clock": creation})
            d.command(f"delete break {self.points.pop(pc)}")
        return True

    def endpoint(self):
        if len(self.events) != 3: raise ValueError("Qualifying startup omitted an initializer or creator clock")
        d = self.debugger
        caller = d.word(register_address(d.command("print $esp")))
        self.events.append({"kind": "first_pre", "return_address": caller,
            "snapshot": FLEET.capture_qualifying_startup_boundary(d.paused_memory(), "421aa0-entry")})
        return self.events

    def close(self):
        for point in self.points.values(): self.debugger.command(f"delete break {point}")
        self.points.clear()


def capture_qpc_values(debugger):
    reader = debugger.paused_memory()
    reader.preload([(GLOBALS[name], 4) for name in FLEET.QPC_SAMPLE_FIELDS])
    result = {}
    for name in FLEET.QPC_SAMPLE_FIELDS:
        value = reader.word(GLOBALS[name])
        result[name] = bool(value) if name == "extra_update" else signed(value) if name in ("delta_q16", "reciprocal_q16", "qpc_divisor") else value
    return result


def capture_qpc_sample(debugger):
    """One actual412ab0 call, including frames without a physical update."""
    returned = debugger.word(register_address(debugger.command("print $esp")))
    if returned != FLEET.QPC_SAMPLE_MANIFEST["return_address"]:
        raise ValueError("Unexpected source QPC sample caller")
    before = capture_qpc_values(debugger)
    point = debugger.breakpoint(returned)
    try:
        debugger.stop_at(returned)
        after = capture_qpc_values(debugger)
    finally:
        debugger.command(f"delete break {point}")
    return {"return_address": returned, "counter": after["qpc_low"], "pre": before, "post": after}


def validate_joined_capture_start(grid_initialization, initial, selected_interval=False):
    if grid_initialization is None:
        raise ValueError("Joined AI capture requires observed460170 initialization")
    if selected_interval:
        FLEET.integer(initial["shared"]["ai_state"]["release_counter"], 0, 360, "selected initial release counter")
        if initial["session_state"] not in (9, 6): raise ValueError("Selected contact/weather interval is outside NoQual grid/race")
    elif initial["session_state"] != 9 or initial["update_counter"] != 0 or initial["shared"]["ai_state"]["release_counter"] != 0:
        raise ValueError("Joined AI capture requires its first unreleased NoQual grid update")


def warmup_until_thunder_ready(debugger, environment, limit=1000):
    """Stop at an actual update entry with positive pending source thunder."""
    before = {name: debugger.word(GLOBALS[name]) for name in ("update_counter", "race_clock")}
    for advanced in range(limit + 1):
        state = {"pending": debugger.word(0x52bc5c), "strength": signed(debugger.word(0x52b69c)),
                 "delay": signed(debugger.word(0x52b6f4)), "flash": signed(debugger.word(0x52b77c))}
        if state["pending"] != 0 and state["strength"] > 0xf5b:
            return {"kind": "observed-positive-thunder-ready", "updates": advanced, "maximum_updates": limit,
                    "keys_only": True, "requested_keys": [], "before": before,
                    "after": {name: debugger.word(GLOBALS[name]) for name in before}, "weather_state": state,
                    "endpoint_boundary": "421aa0-entry", "recorded_boundaries": False, "continuous_from_grid": False}
        if advanced == limit: break
        warmup_keys(debugger, environment, 1, [])
    raise RuntimeError("No positive source pending thunder reached an update entry within warmup bound")


def capture_surface_call(debugger):
    stack = register_address(debugger.command("print $esp"))
    returned, position, kind, material, actor, wheel, strength = struct.unpack("<7I", debugger.memory(stack, 28))
    if actor >= 16 or wheel >= 4 or position != LAYOUT["body_base"] + actor * LAYOUT["body_stride"] + 0xf0 + wheel * 12:
        raise ValueError("Unexpected original surface consumer arguments")
    result = {"return_address": returned, "actor": actor, "wheel": wheel, "kind": kind,
              "material": material, "strength": strength, "pre": FLEET.capture_combined_audio(debugger.paused_memory())}
    order = contact_order(debugger)
    if order is not None: result["order"] = order
    point = debugger.breakpoint(returned)
    try:
        debugger.stop_at(returned)
        result["post"] = FLEET.capture_combined_audio(debugger.paused_memory())
        return result
    finally:
        debugger.command(f"delete break {point}")


def capture_ground_surfaces(debugger, capture_world):
    points = {pc: debugger.breakpoint(pc) for pc in (0x425400, 0x446a80)}
    calls = []
    try:
        for _ in range(65):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            if "00446a80" in response:
                return calls, capture_world("446a80-entry")
            if "00425400" not in response or len(calls) == 64:
                raise ValueError("Unexpected surface/ground source chronology: " + response)
            calls.append(capture_surface_call(debugger))
        raise ValueError("Original ground did not reach its effects boundary")
    finally:
        for point in points.values(): debugger.command(f"delete break {point}")


def capture_mute_call(debugger):
    stack = register_address(debugger.command("print $esp"))
    returned = debugger.word(stack)
    result = {"return_address": returned, "pre": FLEET.capture_combined_audio(debugger.paused_memory())}
    point = debugger.breakpoint(returned)
    try:
        debugger.stop_at(returned)
        result["post"] = FLEET.capture_combined_audio(debugger.paused_memory())
        return result
    finally:
        debugger.command(f"delete break {point}")


class WarmRestartCapture:
    """Observe one real RACE AGAIN choice and its warm reset, using only keys."""
    def __init__(self, environment):
        self.environment = environment
        self.return_held = False
        self.requested = self.restarted = self.grid = False

    def primary(self, debugger):
        # Wait for44a020's own forty-second result timeout to open overlay3.
        # Never manufacture the request flag or alter the original menu state.
        if not self.requested and debugger.word(0x4dcd9c) >> 16:
            keyboard(self.environment, "keyup" if self.return_held else "keydown", "Return")
            self.return_held = not self.return_held

    def release(self):
        if self.return_held:
            keyboard(self.environment, "keyup", "Return")
            self.return_held = False

    def request(self, debugger):
        configuration = debugger.word(0x4b0660)
        if self.requested or not self.return_held or debugger.word(configuration + 0x5f8) & 0xffff != 13:
            raise ValueError("Warm restart requires one actual RACE AGAIN YES from results")
        self.release()
        self.requested = True
        return {"action": "confirm"}

    def restart(self, debugger):
        if not self.requested or self.restarted:
            raise ValueError("Warm restart lacks its single actual menu confirmation")
        returned = debugger.word(register_address(debugger.command("print $esp")))
        point = debugger.breakpoint(0x447a77)
        end = debugger.breakpoint(returned)
        try:
            debugger.stop_at(0x447a77)
            clock = register_value(debugger.command("print $eax"))
            debugger.stop_at(returned)
        finally:
            for value in (point, end): debugger.command(f"delete break {value}")
        self.restarted = True
        return {"creation_clock": clock}

    def initialize_grid(self, debugger):
        if not self.restarted or self.grid:
            raise ValueError("Warm restart requires exactly one subsequent grid writer")
        returned = debugger.word(register_address(debugger.command("print $esp")))
        point, end = debugger.breakpoint(0x460170), debugger.breakpoint(returned)
        boundaries = {}
        try:
            debugger.stop_at(0x460170)
            initialization = capture_start_call(debugger, 0x460170, cold_grid=boundaries)
            debugger.stop_at(returned)
        finally:
            for value in (point, end): debugger.command(f"delete break {value}")
        self.grid = True
        return {"outer_return_address": returned, "initialization": initialization, "boundaries": boundaries}


def advance_combined_update(debugger, local_session=False, warm_restart=None, qpc_samples=False, scene_frames=None, stop_return=None):
    entries = {0x448130: "release", 0x446b50: "wheel", 0x44a6f0: "motor", 0x425e90: "mute"}
    if local_session and scene_frames is None:
        entries[0x44a370] = "primary"
    if warm_restart is not None:
        entries.update({0x441b33: "restart_request", 0x449f50: "restart", 0x447980: "warm_grid"})
    if qpc_samples and scene_frames is None:
        entries[FLEET.QPC_SAMPLE_MANIFEST["entry"]] = "qpc_sample"
    points = {pc: debugger.breakpoint(pc) for pc in entries}
    journal = []
    try:
        for _ in range(1025):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            if "00421aa0" in response or stop_return is not None and f"{stop_return:08x}" in response:
                if scene_frames is not None:
                    journal.extend(scene_frames.drain_clock_events())
                    journal.sort(key=lambda entry: entry["event"]["order"])
                return journal
            matches = [pc for pc in entries if f"{pc:08x}" in response]
            if len(matches) != 1 or len(journal) == 1024:
                raise ValueError("Unexpected combined source frame journal: " + response)
            pc = matches[0]
            order = contact_order(debugger)
            hook = getattr(debugger, "contact_audio", None)
            if isinstance(hook, ContactAudioRecorder): hook.outer = None if pc in (0x441b33, 0x449f50, 0x447980) else entries[pc]
            if pc == 0x44a370:
                event = {"cached_clock": debugger.word(0x4eda6c)}
                if warm_restart is not None: warm_restart.primary(debugger)
            elif pc == 0x441b33: event = warm_restart.request(debugger)
            elif pc == 0x449f50: event = warm_restart.restart(debugger)
            elif pc == 0x447980: event = warm_restart.initialize_grid(debugger)
            elif pc == FLEET.QPC_SAMPLE_MANIFEST["entry"]: event = capture_qpc_sample(debugger)
            elif pc == 0x448130: event = capture_start_call(debugger, pc)
            elif pc == 0x446b50: event = capture_wheel_callback(debugger)
            elif pc == 0x44a6f0: event = capture_motor_callback(debugger)
            else: event = capture_mute_call(debugger)
            if isinstance(hook, ContactAudioRecorder): hook.outer = None
            if order is not None: event["order"] = order
            journal.append({"kind": entries[pc], "event": event})
    finally:
        for point in points.values(): debugger.command(f"delete break {point}")


def capture_whine_call(debugger):
    manifest = FLEET.WHINE_MANIFEST
    stack = register_address(debugger.command("print $esp"))
    returned, argument = struct.unpack("<2I", debugger.memory(stack, 8))
    result = {"return_address": returned, "input_hex": debugger.memory(argument, 24).hex(),
              "pre": FLEET.capture_whine_state(debugger.paused_memory()), "queries": [], "commands": []}
    points = {pc: debugger.breakpoint(pc) for pc in (returned, manifest["playing"], manifest["rate"], manifest["volume"], manifest["offset_pc"])}
    pending_query = None
    try:
        for _ in range(128):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            matches = [pc for pc in points if f"{pc:08x}" in response]
            if len(matches) != 1:
                raise ValueError("Unexpected WHINE source stop: " + response)
            pc = matches[0]
            if pc == returned:
                if pending_query is not None:
                    raise ValueError("WHINE returned before its playing query")
                result["post"] = FLEET.capture_whine_state(debugger.paused_memory())
                if len({query["playing"] for query in result["queries"]}) > 1:
                    raise ValueError("WHINE voice playing gate changed inside one source call")
                return result
            if pending_query is not None and pc == pending_query:
                value = register_value(debugger.command("print $eax"))
                FLEET.integer(value, 0, 1, "actual WHINE playing result")
                result["queries"].append({"return_address": pc, "playing": value})
                debugger.command(f"delete break {points.pop(pc)}")
                pending_query = None
            elif pc == manifest["offset_pc"]:
                if "offset" in result:
                    raise ValueError("Repeated WHINE shift offset boundary")
                result["offset"] = signed(register_value(debugger.command("print $ebp")))
            else:
                stack = register_address(debugger.command("print $esp"))
                caller, pointer, value = struct.unpack("<3I", debugger.memory(stack, 12))
                if pointer != manifest["voice_pointer"]:
                    continue
                if pc == manifest["playing"]:
                    if pending_query is not None:
                        raise ValueError("Nested WHINE playing query")
                    pending_query = caller
                    points[caller] = debugger.breakpoint(caller)
                else:
                    result["commands"].append({"kind": "rate" if pc == manifest["rate"] else "volume", "value": signed(value), "return_address": caller})
        raise ValueError("Too many source WHINE observations")
    finally:
        for point in points.values(): debugger.command(f"delete break {point}")


def capture_engine_call(debugger):
    stack = register_address(debugger.command("print $esp"))
    returned, argument, player = struct.unpack("<3I", debugger.memory(stack, 12))
    result = {"return_address": returned, "player": player, "input_hex": debugger.memory(argument, 24).hex(),
              "pre": FLEET.capture_whine_state(debugger.paused_memory()), "whine": []}
    points = {pc: debugger.breakpoint(pc) for pc in (returned, FLEET.WHINE_MANIFEST["whine"])}
    try:
        for _ in range(2):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            if f"{returned:08x}" in response:
                result["post_input_hex"] = debugger.memory(argument, 24).hex()
                result["post"] = FLEET.capture_whine_state(debugger.paused_memory())
                return result
            if "00414780" not in response or result["whine"]:
                raise ValueError("Unexpected source engine/WHINE chronology: " + response)
            result["whine"].append(capture_whine_call(debugger))
        raise ValueError("Source engine callback did not return")
    finally:
        for point in points.values(): debugger.command(f"delete break {point}")


def capture_motor_callback(debugger):
    stack = register_address(debugger.command("print $esp"))
    returned = debugger.word(stack)
    result = {"return_address": returned, "pre": FLEET.capture_whine_caller(debugger.paused_memory()), "engines": []}
    points = {pc: debugger.breakpoint(pc) for pc in (returned, FLEET.WHINE_MANIFEST["engine"])}
    try:
        for _ in range(5):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            if f"{returned:08x}" in response:
                result["post"] = FLEET.capture_whine_state(debugger.paused_memory())
                return result
            if "00414240" not in response:
                raise ValueError("Unexpected source motor caller stop: " + response)
            result["engines"].append(capture_engine_call(debugger))
        raise ValueError("Too many original local engine callbacks")
    finally:
        for point in points.values(): debugger.command(f"delete break {point}")


def advance_whine_update(debugger):
    point = debugger.breakpoint(FLEET.WHINE_MANIFEST["caller"])
    callbacks = []
    try:
        for _ in range(257):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            if "00421aa0" in response: return callbacks
            if "0044a6f0" not in response or len(callbacks) == 256:
                raise ValueError("Unexpected source WHINE frame chronology: " + response)
            callbacks.append(capture_motor_callback(debugger))
    finally:
        debugger.command(f"delete break {point}")


def capture_wheel_callback(debugger):
    manifest = FLEET.WHEEL_QUEUE_MANIFEST
    stack = register_address(debugger.command("print $esp"))
    returned, view = struct.unpack("<2I", debugger.memory(stack, 8))
    if not 0x400000 <= returned < 0x4b0000:
        raise ValueError("Invalid source wheel callback return")
    result = {"return_address": returned, "view": view,
              "pre": FLEET.capture_wheel_state(debugger.paused_memory(), "446b50-entry"), "producers": []}
    points = {pc: debugger.breakpoint(pc) for pc in (returned, manifest["producer"])}
    current = None
    try:
        for _ in range(2 * 16 + 1):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            matched = [pc for pc in points if f"{pc:08x}" in response]
            if len(matched) != 1:
                raise ValueError("Unexpected source wheel callback stop: " + response)
            pc = matched[0]
            if pc == returned:
                if current is not None:
                    raise ValueError("Wheel callback returned with pending producer")
                result["post"] = FLEET.capture_wheel_state(debugger.paused_memory(), "446b50-return")
                FLEET.validate_wheel_callback(result)
                return result
            if pc == manifest["producer"]:
                if current is not None:
                    raise ValueError("Nested source wheel producer")
                stack = register_address(debugger.command("print $esp"))
                caller, dynamics, body = struct.unpack("<3I", debugger.memory(stack, 12))
                actor, remainder = divmod(body - LAYOUT["body_base"], LAYOUT["body_stride"])
                if remainder or not 0 <= actor < 16 or dynamics != LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"] or caller not in manifest["producer_returns"]:
                    raise ValueError("Unexpected source wheel producer arguments/caller")
                current = {"actor": actor, "return_address": caller,
                           "pre": FLEET.capture_wheel_actor(debugger.paused_memory(), actor)}
                points[caller] = debugger.breakpoint(caller)
            else:
                if current is None or pc != current["return_address"]:
                    raise ValueError("Source wheel producer returned without entry")
                current["post"] = FLEET.capture_wheel_actor(debugger.paused_memory(), current["actor"])
                result["producers"].append(current)
                current = None
                debugger.command(f"delete break {points.pop(pc)}")
        raise ValueError("Too many wheel producer calls in one source callback")
    finally:
        for point in points.values():
            debugger.command(f"delete break {point}")


def advance_wheel_queue_update(debugger):
    """Observe every real callback; a simulation interval may have zero or many."""
    point = debugger.breakpoint(FLEET.WHEEL_QUEUE_MANIFEST["entry"])
    callbacks = []
    try:
        for _ in range(257):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            if "00421aa0" in response:
                return callbacks
            if "00446b50" not in response:
                raise ValueError("Unexpected source stop between wheel queue updates: " + response)
            if len(callbacks) == 256:
                raise ValueError("Too many original wheel callbacks between simulation updates")
            callbacks.append(capture_wheel_callback(debugger))
    finally:
        debugger.command(f"delete break {point}")


def capture_skid_dispatch(debugger, combined=False):
    """Observe actual dispatcher calls and selected EBX samples; never write memory."""
    manifest = FLEET.SKID_MANIFEST
    entry = debugger.breakpoint(manifest["entry"])
    debugger.stop_at(manifest["entry"])
    debugger.command(f"delete break {entry}")
    stack = register_address(debugger.command("print $esp"))
    caller = debugger.word(stack)
    if not 0x400000 <= caller < 0x4b0000:
        raise ValueError("Invalid skid dispatcher source return address")
    result = {"return_address": caller,
              "pre": FLEET.capture_skid_state(debugger.paused_memory(), "44c540-entry"), "calls": []}
    if combined: result["audio_pre"] = FLEET.capture_combined_audio(debugger.paused_memory())
    points = {pc: debugger.breakpoint(pc) for pc in (caller, manifest["callee"], *manifest["sample_pcs"].values())}
    current = None
    try:
        for _ in range(3 * 64 + 1):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            matched = [pc for pc in points if f"{pc:08x}" in response]
            if len(matched) != 1:
                raise ValueError("Unexpected source skid dispatcher stop: " + response)
            pc = matched[0]
            if pc == caller:
                if current is not None:
                    raise ValueError("Skid dispatcher returned inside a pending callee")
                result["post"] = FLEET.capture_skid_state(debugger.paused_memory(), "44c540-return")
                if combined: result["audio_post"] = FLEET.capture_combined_audio(debugger.paused_memory())
                return result
            if pc == manifest["callee"]:
                if current is not None:
                    raise ValueError("Nested source skid dispatcher call")
                stack = register_address(debugger.command("print $esp"))
                args = struct.unpack("<7I", debugger.memory(stack, 28))
                returned, position, kind, wheel_kind, strength, identity, wheel = args
                actor, offset = divmod(position - LAYOUT["body_base"] - 0xf0, LAYOUT["body_stride"])
                if returned != manifest["callee_return"] or not 0 <= actor < 16 or not 0 <= wheel < 4 or offset != wheel * 12 or identity >= 16:
                    raise ValueError("Unexpected skid dispatcher source arguments")
                if strength != 0x4ef2f0 + (identity * 4 + wheel) * 4:
                    raise ValueError("Unexpected source skid strength pointer")
                current = {"actor": actor, "wheel": wheel, "identity": identity, "kind": signed(kind),
                           "wheel_kind": wheel_kind & 255, "strength": signed(debugger.word(strength))}
                current["pre"] = skid_call_state(debugger, identity)
                points[manifest["callee_return"]] = debugger.breakpoint(manifest["callee_return"])
            elif pc == manifest["callee_return"]:
                if current is None:
                    raise ValueError("Skid return without a source callee")
                current["post"] = skid_call_state(debugger, current["identity"])
                result["calls"].append(current)
                current = None
                debugger.command(f"delete break {points.pop(manifest['callee_return'])}")
            else:
                if current is None or "sample" in current:
                    raise ValueError("Unexpected source skid sample decision")
                member = register_address(debugger.command("print $ebx"))
                frame = register_address(debugger.command("print $ebp"))
                fade, restart = struct.unpack("<2I", debugger.memory(frame - 0x14, 8))
                current["sample"] = {"pc": pc, "member": member, "restart": restart, "fade": fade}
        raise ValueError("Too many source skid dispatcher events")
    finally:
        for point in points.values():
            debugger.command(f"delete break {point}")


def skid_call_state(debugger, identity):
    reader = debugger.paused_memory()
    reader.preload([(0x4b9e78, 4), (0x4d3de8 + (identity & ~3), 4), (0x4d39f8 + identity * 4, 4)])
    return {"rng": reader.word(0x4b9e78), "mask": reader.memory(0x4d3de8 + identity, 1)[0],
            "reset": reader.word(0x4d39f8 + identity * 4)}


def advance_joined_update(debugger):
    events = []
    for _ in range(17):
        response = debugger.command("cont", 30).lower()
        debugger.confirm_pause()
        if "00421aa0" in response:
            return events
        if "00448130" not in response:
            raise RuntimeError("Unexpected stop between joined source updates: " + response)
        events.append(capture_start_call(debugger, 0x448130))
    raise RuntimeError("Too many source release calls between updates")


def capture_ai_boundaries(debugger, capture_world, local_handoffs=False, local_actor=None, traffic_controls=False, nearest_recovery=False):
    """Bracket403dd0 and optional actual local controller call transactions."""
    entry = debugger.breakpoint(0x403DD0)
    debugger.stop_at(0x403DD0)
    pre = capture_world(FLEET.AI_BOUNDARIES["ai_pre"])
    set_consumer_phase(debugger, "ai_route")
    debugger.command(f"delete break {entry}")
    address = debugger.word(register_address(debugger.command("print $esp")))
    if not 0x400000 <= address < 0x4b0000:
        raise RuntimeError("Source AI return address outside executable code")
    exit_point = debugger.breakpoint(address)
    events = []
    traffic_pre = None
    traffic_point = debugger.breakpoint(0x404c1a) if traffic_controls else None
    recovery_point = debugger.breakpoint(FLEET.NEAREST_RECOVERY_MANIFEST["entry"]) if nearest_recovery else None
    recoveries = []
    calls = {kind: debugger.breakpoint(call["entry"]) for kind, call in FLEET.LOCAL_HANDOFF_CALLS.items()} if local_handoffs else {}
    if not local_handoffs and not traffic_controls and not nearest_recovery:
        debugger.stop_at(address)
    else:
        for _ in range(256):
            response = debugger.command("cont", 30).lower()
            debugger.confirm_pause()
            if f"{address:08x}" in response:
                if traffic_controls and traffic_pre is None:
                    raise RuntimeError("Source AI returned without its required traffic boundary")
                break
            if traffic_point is not None and "00404c1a" in response:
                traffic_pre = capture_world(FLEET.TRAFFIC_BOUNDARIES["traffic_pre"])
                debugger.command(f"delete break {traffic_point}")
                traffic_point = None
                continue
            if recovery_point is not None and "004046a3" in response:
                before = nearest_recovery_snapshot(debugger, "pre")
                if len(recoveries) >= 16:
                    raise RuntimeError("Too many human nearest-recovery checks inside one update")
                recovery_exit = debugger.breakpoint(FLEET.NEAREST_RECOVERY_MANIFEST["exit"])
                debugger.stop_at(FLEET.NEAREST_RECOVERY_MANIFEST["exit"])
                after = nearest_recovery_snapshot(debugger, "post")
                debugger.command(f"delete break {recovery_exit}")
                recoveries.append({"actor": before["actor_index"], "pre": before, "post": after})
                continue
            matches = [kind for kind, call in FLEET.LOCAL_HANDOFF_CALLS.items() if f"{call['entry']:08x}" in response]
            if len(matches) != 1:
                raise RuntimeError("Unexpected stop while observing local controller calls: " + response)
            kind = matches[0]
            stack = debugger.memory(register_address(debugger.command("print $esp")), 8)
            caller, actor = struct.unpack("<2I", stack)
            if not 0 <= actor < 16:
                raise RuntimeError("Invalid controller call actor argument")
            if actor != local_actor:
                continue
            if caller not in FLEET.LOCAL_HANDOFF_CALLS[kind]["returns"] or len(events) >= 16:
                raise RuntimeError("Unsupported local controller caller or event count")
            event = {"kind": kind, "actor": actor, "return_address": caller, "stack_hex": stack.hex(),
                     "pre": handoff_snapshot(debugger, actor, kind, "entry")}
            call_return = debugger.breakpoint(caller)
            debugger.stop_at(caller)
            event["post"] = handoff_snapshot(debugger, actor, kind, "return")
            debugger.command(f"delete break {call_return}")
            events.append(event)
        else:
            raise RuntimeError("Too many controller calls inside one source update")
    post = capture_world(FLEET.AI_BOUNDARIES["ai_post"])
    set_consumer_phase(debugger, "ai_to_consumed")
    for point in calls.values():
        debugger.command(f"delete break {point}")
    if recovery_point is not None:
        debugger.command(f"delete break {recovery_point}")
    debugger.command(f"delete break {exit_point}")
    return {"ai_pre": pre, "ai_post": post, **({"local_handoffs": events} if local_handoffs else {}),
            **({"traffic_pre": traffic_pre} if traffic_controls else {}),
            **({"nearest_recoveries": recoveries} if nearest_recovery else {})}


def warmup_until_local_rollout(debugger, environment, limit=1000):
    """Observe the automatic QualOn approach; stop before the ordered first line."""
    actor = debugger.word(0x4d37a0) & 0xff
    if actor >= 16:
        raise RuntimeError("Invalid local rollout actor")
    for updates in range(limit + 1):
        config = debugger.word(0x4b0660)
        raw = debugger.memory(LAYOUT["gameplay_base"] + actor * LAYOUT["gameplay_stride"], 0x124)
        state = debugger.word(config + 0x5f8) & 0xffff
        stage = debugger.word(config + 0x5f4) >> 16
        human = struct.unpack_from("<I", raw, 8)[0]
        lap, point = struct.unpack_from("<h", raw, 0xa8)[0], struct.unpack_from("<h", raw, 0xa2)[0]
        if state == 4 and stage in (1, 3) and not human and raw[0x80] < 4 and lap == 1 and point == 0 and raw[0xa6] == 0 and raw[0x9d] in (0, 2):
            return {"updates": updates, "max_updates": limit, "actor": actor, "lap": lap, "route_next": point,
                    "session_state": state, "session_stage": stage, "checkpoint": raw[0xa6], "marker": raw[0x9d], "endpoint_boundary": "421aa0-entry",
                    "keys_only": True, "recorded_boundaries": False, "continuous_from_grid": False,
                    "predicate": "QualOn automatic local displayed lap1 ordered point0 before402c80 handback"}
        if human or lap != 1 or state != 4:
            raise RuntimeError("Qualifying rollout handback already occurred or source left automatic approach")
        if updates == limit:
            break
        warmup_keys(debugger, environment, 1, [])
    raise RuntimeError(f"No local qualifying rollout endpoint within {limit} actual updates")


class LocalLapPolicy:
    def __init__(self, initialized_points, snapshot):
        spec = importlib.util.spec_from_file_location("reference_lap_driver", PROJECT / "tools/reference_lap_driver.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.driver = module.SourceLapDriver(initialized_points, snapshot)
        self.parameters = self.driver.parameters

    def choose(self, _index, snapshot):
        return self.driver.choose(snapshot)


class NearestRecoveryPolicy:
    """Physical-key acquisition toward an observed nonlocal route candidate."""

    def __init__(self, points, nearest_map, initial, origin):
        self.lap = LocalLapPolicy(points, initial)
        self.count = struct.unpack_from("<I", points)[0]
        FLEET.integer(origin, 0, self.count - 1, "nearest recovery approach POINTS index")
        self.map = bytes(nearest_map)
        FLEET.nearest_map_content({"nearest_map_hex": self.map.hex(), "nearest_map_crc32": f"{zlib.crc32(self.map):08x}"}, self.count)
        self.points = [struct.unpack_from("<3i", points, 4 + point * 64 + 12) for point in range(self.count)]
        self.origin, self.candidate = origin, self.map[origin]
        self.diverting = False
        self.previous_forward = None
        self.parameters = {"version": 1, "keys_only": True, "origin": origin, "candidate": self.candidate,
                           "nearest_map_crc32": f"{zlib.crc32(self.map):08x}", "points_crc32": f"{zlib.crc32(points):08x}",
                           "maximum_speed": .55, "minimum_speed": .3, "corner_slowdown": .35, "brake_margin": .08,
                           "yaw_damping": 8, "steering_deadband": .025,
                           "policy": "lap approach then physical-key drive toward captured nearest candidate; not original AI"}

    def pending(self, snapshot):
        body, game = (bytes.fromhex(snapshot[name + "_hex"]) for name in ("body", "gameplay"))
        cursor = struct.unpack_from("<h", game, 0xa2)[0]
        FLEET.integer(cursor, 0, self.count - 1, "observed local route cursor")
        position = struct.unpack_from("<3i", body, 0xbc)
        def distance(point):
            dx, dz = (signed((position[axis] - self.points[point][axis]) & 0xffffffff) >> 16 for axis in (0, 2))
            return signed((dx * dx + dz * dz) & 0xffffffff)
        candidate = self.map[cursor]
        current_distance, candidate_distance = distance(cursor), distance(candidate)
        info = {"route_next": cursor, "route_previous": struct.unpack_from("<h", game, 0xa4)[0],
                "candidate": candidate, "position_q16": list(position), "current_distance_squared": current_distance,
                "candidate_distance_squared": candidate_distance, "human": struct.unpack_from("<I", game, 8)[0]}
        pending = bool(info["human"] and struct.unpack_from("<I", game, 4)[0] and not struct.unpack_from("<I", game, 0x54)[0]
                       and snapshot["session_state"] == 6 and candidate_distance < current_distance)
        return pending, info

    def choose(self, index, snapshot):
        _, info = self.pending(snapshot)
        self.diverting = self.diverting or info["route_next"] == self.origin and info["human"] == 1
        if not self.diverting:
            keys, decision = self.lap.choose(index, snapshot)
            return keys, {**decision, "acquisition_phase": "approach-recovery-origin", **info}
        if not info["human"] or snapshot["session_state"] != 6:
            return set(), {**info, "acquisition_phase": "wait-for-human-race"}
        pose = body_values(snapshot)
        position, forward = pose["position"], pose["forward"]
        target = self.points[self.candidate]
        dx, dz = target[0] - position[0], target[2] - position[2]
        error = math.atan2(forward[0] * dz - forward[2] * dx, forward[0] * dx + forward[2] * dz)
        previous = self.previous_forward
        yaw = 0 if previous is None else math.atan2(previous[0] * forward[2] - previous[2] * forward[0],
                                                   previous[0] * forward[0] + previous[2] * forward[2])
        steering = error - self.parameters["yaw_damping"] * yaw
        speed = pose["speed"] / ONE
        desired_speed = max(self.parameters["minimum_speed"], self.parameters["maximum_speed"] - abs(error) * self.parameters["corner_slowdown"])
        keys = {"Up"} if speed < desired_speed else {"Down"} if speed > desired_speed + self.parameters["brake_margin"] else set()
        if abs(steering) > self.parameters["steering_deadband"]:
            keys.add("Left" if steering > 0 else "Right")
        self.previous_forward = forward
        return keys, {**info, "acquisition_phase": "drive-toward-nearest-candidate", "target_q16": list(target),
                      "speed": speed, "desired_speed": desired_speed, "steering_radians": steering, "yaw_radians": yaw}


def warmup_nearest_ready(debugger, environment, policy, output, limit=6000):
    """Stop before a pending recovery; only later exact-block events prove jumps."""
    before = local_driving_snapshot(debugger)
    initial_counter = before["update_counter"]
    held, resets = set(), []
    with output.open("w") as log:
        try:
            for updates in range(limit + 1):
                pending, info = policy.pending(before)
                if pending:
                    log.flush()
                    return {"updates": updates, "maximum_updates": limit, "initial_update_counter": initial_counter,
                            "endpoint_update_counter": before["update_counter"], "endpoint_boundary": "421aa0-entry",
                            "endpoint": info, "policy": policy.parameters, "counter_resets": resets,
                            "keys_only": True, "recorded_boundaries": False, "continuous_from_grid": False,
                            "decision_log": output.name, "decision_log_crc32": f"{zlib.crc32(output.read_bytes()):08x}",
                            "predicate": "observed local human candidate strictly closer by source integer-XZ distance; later ordinary gate progression may change the cursor"}
                if updates == limit:
                    break
                desired, decision = policy.choose(updates, before)
                for key in sorted(held - desired): keyboard(environment, "keyup", key)
                for key in sorted(desired - held): keyboard(environment, "keydown", key)
                held = desired
                log.write(json.dumps({"update": updates, "counter": before["update_counter"], "requested_keys": sorted(desired), "decision": decision}) + "\n")
                log.flush()
                debugger.stop_at(0x421aa0)
                after = local_driving_snapshot(debugger)
                reset = validate_driving_step(before, after)
                if reset is not None: resets.append(reset)
                before = after
                if (updates + 1) % 250 == 0:
                    print(f"Nearest-route keys-only warmup: {updates + 1}/{limit}, point{info['route_next']}, candidate{info['candidate']}", flush=True)
        finally:
            for key in sorted(held): keyboard(environment, "keyup", key)
    raise RuntimeError(f"Pending human nearest-route jump not reached within {limit} actual driving updates")


def local_driving_snapshot(debugger):
    """Small observed update endpoint, never a seven-boundary replay record."""
    reader = debugger.paused_memory()
    reader.preload([(0x4d37a0, 4), (0x4b0660, 4), (0x4d3838, 16), (0x4d3854, 8),
                    (0x4c9140, 16), (0x4c39c8, 4), (0x4c5d68, 8),
                    (LAYOUT["gameplay_base"], 16 * 0x124)])
    actor = reader.word(0x4d37a0) & 0xff
    if actor >= 16:
        raise RuntimeError("Invalid local driving actor")
    config = reader.word(0x4b0660)
    reader.preload([(config + 4, 4), (config + 0x5f4, 8),
                    (LAYOUT["body_base"] + actor * 0x53c, 0x53c),
                    (LAYOUT["dynamics_base"] + actor * 0x338, 0x338)])
    games = reader.memory(LAYOUT["gameplay_base"], 16 * 0x124)
    clocks = reader.memory(0x4d3838, 16)
    stage, state = struct.unpack_from("<hh", reader.memory(config + 0x5f4, 8), 2)
    return {"actor": actor, "body_hex": reader.memory(LAYOUT["body_base"] + actor * 0x53c, 0x53c).hex(),
            "dynamics_hex": reader.memory(LAYOUT["dynamics_base"] + actor * 0x338, 0x338).hex(),
            "gameplay_hex": games[actor * 0x124:(actor + 1) * 0x124].hex(), "all_gameplay_hex": games.hex(),
            "session_state": state, "session_stage": stage, "source_mode": reader.word(config + 4),
            "lap_limit": struct.unpack_from("<H", reader.memory(0x4c5d68, 8), 4)[0],
            "update_counter": struct.unpack_from("<I", clocks, 4)[0], "race_clock": struct.unpack_from("<I", clocks, 8)[0],
            "finish_counts_hex": reader.memory(0x4d3854, 8).hex(), "finish_order_hex": reader.memory(0x4c9140, 16).hex(),
            "local_finish_flags_hex": reader.memory(0x4c39c8, 4).hex()}


def validate_driving_step(before, after):
    if after["update_counter"] == (before["update_counter"] + 1) & 0xffffffff:
        return None
    if after["update_counter"] != 0 or bytes.fromhex(before["finish_counts_hex"])[0] != 0 or not 1 <= bytes.fromhex(after["finish_counts_hex"])[0] <= 16:
        raise RuntimeError("Unexplained source counter discontinuity during local driving")
    driver = bytes.fromhex(after["finish_order_hex"])[0]
    old_games, new_games = (bytes.fromhex(item["all_gameplay_hex"]) for item in (before, after))
    matches = []
    for actor in range(16):
        pre, post = (raw[actor * 0x124:(actor + 1) * 0x124] for raw in (old_games, new_games))
        player = struct.unpack_from("<b", pre, 0x80)[0]
        local_finished = 0 <= player < 4 and bytes.fromhex(after["local_finish_flags_hex"])[player] != 0
        if (pre[0x8d] == driver and (player == -1 or local_finished) and struct.unpack_from("<I", pre, 0x54)[0] == 0
                and struct.unpack_from("<I", post, 0x54)[0] == 1
                and struct.unpack_from("<h", pre, 0xa8)[0] == before["lap_limit"]
                and struct.unpack_from("<h", post, 0xa8)[0] == before["lap_limit"] + 1):
            matches.append(actor)
    if before["source_mode"] != 0 or before["session_state"] != 6 or before["session_stage"] not in (2, 4) or len(matches) != 1:
        raise RuntimeError("Counter reset lacks exact first-finisher source evidence")
    return {"reason": "source421390 first finisher counter reset", "actor": matches[0], "driver": driver,
            "before": before, "after": after}


def warmup_local_finish(debugger, environment, policy, output, qualifying=False, limit=6000):
    """Keys-only feedback, ending before a pending local final-line crossing."""
    before = local_driving_snapshot(debugger)
    initial_counter = before["update_counter"]
    held, resets = set(), []
    target_lap = 3 if qualifying else before["lap_limit"]
    with output.open("w") as log:
        try:
            for updates in range(limit + 1):
                game = bytes.fromhex(before["gameplay_hex"])
                lap = struct.unpack_from("<h", game, 0xa8)[0]
                next_point = struct.unpack_from("<h", game, 0xa2)[0]
                if (struct.unpack_from("<I", game, 8)[0] and not struct.unpack_from("<I", game, 0x54)[0]
                        and before["session_state"] in (4, 6) and lap == target_lap and next_point == 0
                        and game[0xa6] == 0 and game[0x9d] == 0 and struct.unpack_from("<I", game, 0xc)[0] == 0):
                    return {"updates": updates, "max_updates": limit, "target_lap": target_lap, "actor": before["actor"],
                            "endpoint_boundary": "421aa0-entry", "endpoint": {name: before[name] for name in ("session_state", "session_stage", "update_counter", "race_clock")},
                            "initial_update_counter": initial_counter, "counter_resets": resets,
                            "keys_only": True, "recorded_boundaries": False, "continuous_from_grid": False,
                            "decision_log": output.name, "decision_log_crc32": f"{zlib.crc32(output.read_bytes()):08x}",
                            "policy": policy.parameters, "predicate": "local human ordered final lap point0/checkpoint0/marker0 before finish handoff"}
                if updates == limit or lap > target_lap or struct.unpack_from("<I", game, 0x54)[0]:
                    break
                desired, info = policy.choose(updates, before)
                for key in sorted(held - desired): keyboard(environment, "keyup", key)
                for key in sorted(desired - held): keyboard(environment, "keydown", key)
                held = desired
                log.write(json.dumps({"update": updates, "counter": before["update_counter"], "requested_keys": sorted(desired), "decision": info}) + "\n")
                log.flush()
                debugger.stop_at(0x421aa0)
                after = local_driving_snapshot(debugger)
                reset = validate_driving_step(before, after)
                if reset is not None: resets.append(reset)
                before = after
                if (updates + 1) % 250 == 0:
                    print(f"Local keys-only warmup: {updates + 1}/{limit}, lap{lap}, point{next_point}", flush=True)
        finally:
            for key in sorted(held): keyboard(environment, "keyup", key)
    raise RuntimeError(f"Local final-line endpoint not reached within {limit} real driving updates")


def capture_quality(activity_quality, header, detached_records=None, timing_records=None, local_records=None):
    """Require the selected lifecycle boundary for focused warm profiles."""
    if local_records is not None:
        if FLEET.retained_warm_restart_enabled(header):
            focused = FLEET.retained_warm_restart_coverage(header, local_records)
            passed = focused["targeted_warm_restart_required"] and focused["targeted_warm_restart_observed"] and focused["coverage_sufficient"]
            return {"profile": "retained-warm-restart", **focused, "activity_diagnostics": activity_quality,
                    "gates": {"local_finish_results_and_warm_restart": passed}, "passed": passed}
        focused = FLEET.retained_local_session_coverage(header, local_records)
        passed = (focused["targeted_local_finish_required"] and focused["targeted_local_finish_observed"]
                  and focused["coverage_sufficient"])
        return {"profile": "retained-local-finish", **focused, "activity_diagnostics": activity_quality,
                "gates": {"local_finish_and_results": passed}, "passed": passed}
    if timing_records is not None:
        focused = FLEET.retained_race_timing_coverage(header, timing_records)
        passed = (focused["targeted_ai_finish_required"] and focused["targeted_ai_finish_observed"]
                  and focused["coverage_sufficient"])
        return {"profile": "retained-ai-finish", **focused, "activity_diagnostics": activity_quality,
                "gates": {"chosen_ai_finish": passed}, "passed": passed}
    if detached_records is None:
        return activity_quality
    focused = FLEET.detached_expiry_coverage(header, detached_records)
    passed = (focused["chosen_expiry_required"] and focused["chosen_expiry_observed"]
              and focused["coverage_sufficient"])
    return {"profile": "detached-expiry", **focused, "activity_diagnostics": activity_quality,
            "gates": {"chosen_visible_expiry": passed}, "passed": passed}


def retained_prefix_exact(report, updates, checksum=None):
    def unsupported(value):
        if isinstance(value, dict):
            return bool(value.get("unsupported_updates")) or any(unsupported(item) for item in value.values())
        return isinstance(value, list) and any(unsupported(item) for item in value)
    return (report.get("status") == "compared" and report.get("mode") == "startup-world-reset"
             and report.get("source_updates") == updates and report.get("complete_source_interval") is True
             and report.get("numerical_parity") is True and report.get("exact_prefix_updates") == updates
             and (checksum is None or report.get("trace_crc32") == checksum)
             and not unsupported(report))


def verify_retained_prefix(trace, directory, replay, updates):
    """Final comparison only; the source must already have been cleaned up."""
    output = directory / "verify"
    report_path = output / "fleet-comparison-startup-world-reset.json"
    report_path.unlink(missing_ok=True)
    execution = subprocess.run([sys.executable, str(PROJECT / "tools/replay-fleet-reference.py"),
                                str(trace), "--compare-startup-world-reset", "--replay", str(replay.resolve()),
                                "--output", str(output)], capture_output=True, text=True, timeout=600)
    if execution.returncode not in (0, 1) or not report_path.is_file():
        raise RuntimeError(execution.stderr.strip() or f"Retained prefix replay failed with exit {execution.returncode}")
    exact = retained_prefix_exact(json.loads(report_path.read_text()), updates)
    print(f"Verified {updates} retained source updates: {'exact' if exact else 'DIFF'}. {report_path}", flush=True)
    return exact


class RetainedPrefixVerifier:
    """One background comparison of an immutable, complete source prefix."""
    def __init__(self, directory, replay):
        self.directory, self.replay = directory, replay
        self.process = self.log = None
        self.receipt = None
        self.verified_updates = 0

    def record_exact(self, receipt):
        self.receipt = receipt
        self.verified_updates = receipt["updates"]
        temporary = self.directory / "verify-last-exact.json.tmp"
        temporary.write_text(json.dumps(receipt, indent=2) + "\n")
        temporary.replace(self.directory / "verify-last-exact.json")

    def start(self, source, updates, position, checksum, quality):
        if self.process is not None:
            return False
        output = self.directory / "verify"
        output.mkdir(exist_ok=True)
        snapshot = self.directory / "verify-prefix.jsonl"
        footer = (json.dumps({"type": "complete", "updates": updates,
                              "prefix_crc32": f"{checksum:08x}", "coverage": quality}) + "\n").encode()
        try:
            shutil.copyfile(source, snapshot)
            if snapshot.stat().st_size != position:
                raise RuntimeError("Source prefix changed during snapshot creation")
            with snapshot.open("ab") as target:
                target.write(footer)
            self.receipt = {"updates": updates, "source_bytes": position, "prefix_crc32": f"{checksum:08x}",
                            "trace_crc32": f"{zlib.crc32(footer, checksum):08x}", "status": "running"}
            (output / "fleet-comparison-startup-world-reset.json").unlink(missing_ok=True)
            self.log = (output / "replay.log").open("wb")
            self.process = subprocess.Popen([sys.executable, str(PROJECT / "tools/replay-fleet-reference.py"),
                str(snapshot), "--compare-startup-world-reset", "--replay", str(self.replay.resolve()), "--output", str(output)],
                stdin=subprocess.DEVNULL, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
            self.started = time.monotonic()
        except BaseException:
            self.cancel()
            if self.log is not None: self.log.close()
            self.log = None
            snapshot.unlink(missing_ok=True)
            raise
        return True

    def cancel(self):
        if self.process is not None:
            try:
                try: os.killpg(self.process.pid, signal.SIGTERM)
                except ProcessLookupError: pass
                try: self.process.wait(timeout=5)
                except subprocess.TimeoutExpired: pass
            finally:
                try:
                    # A reaped Python leader can leave its native child alive.
                    try: os.killpg(self.process.pid, signal.SIGKILL)
                    except ProcessLookupError: pass
                    self.process.wait(timeout=5)
                finally:
                    if self.log is not None: self.log.close()
                    self.process = self.log = None
                    if self.receipt is not None and self.receipt["status"] == "running": self.receipt["status"] = "cancelled"

    def poll(self):
        if self.process is None:
            return None
        status = self.process.poll()
        if status is None and time.monotonic() - self.started < 600:
            return None
        if status is None:
            self.cancel()
            self.receipt.update(status="error", reason="Background prefix comparison exceeded600 seconds")
        else:
            self.process.wait()
            self.log.close()
            self.process = self.log = None
            path = self.directory / "verify/fleet-comparison-startup-world-reset.json"
            try:
                report = json.loads(path.read_text())
                if status not in (0, 1):
                    raise RuntimeError(report.get("reason", f"Prefix replay exited with status {status}"))
                exact = retained_prefix_exact(report, self.receipt["updates"], self.receipt["trace_crc32"])
                self.receipt.update(status="exact" if exact else "different")
                if exact: self.record_exact(self.receipt)
            except (OSError, ValueError, RuntimeError) as error:
                self.receipt.update(status="error", reason=str(error))
        print(f"Background prefix {self.receipt['updates']}: {self.receipt['status']}", flush=True)
        return self.receipt

    def finish(self):
        """Called after source cleanup; no later source QPC can observe this wait."""
        if self.process is not None:
            try:
                self.process.wait(timeout=max(0, 600 - (time.monotonic() - self.started)))
            except subprocess.TimeoutExpired:
                self.cancel()
                self.receipt.update(status="error", reason="Background prefix comparison exceeded600 seconds")
                return self.receipt
            except BaseException:
                self.cancel()
                raise
        return self.poll()


def finalize_aborted_prefix(path, updates, position, checksum, quality, error, closed_prefix):
    """Only finished JSON records become an explicitly aborted observation."""
    with path.open("a+b") as target:
        target.seek(0, 2)
        if target.tell() != position:
            raise RuntimeError("Aborted capture has an incomplete JSON record")
        if not updates:
            return
        target.seek(-1, 2)
        if target.read(1) != b"\n":
            raise RuntimeError("Aborted capture has an incomplete JSON record")
        target.seek(0, 2)
        target.write((json.dumps({"type": "complete", "updates": updates, "prefix_crc32": f"{checksum:08x}",
            "coverage": {**quality, "passed": False}, "capture_status": "aborted", "source_exception": error,
            "last_closed_clock_prefix": closed_prefix}) + "\n").encode())


def verify_final_capture(path, directory, replay, updates, checksum, quality):
    """Persist a complete footer even when final verification fails or times out."""
    try:
        with path.open("a+") as trace:
            trace.seek(0, 2)
            trace.write(json.dumps({"type": "complete", "updates": updates,
                                    "prefix_crc32": f"{checksum:08x}", "coverage": quality}) + "\n")
            trace.flush()
            exact = verify_retained_prefix(path, directory, replay, updates)
        return exact, None
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        return False, str(error)


def local_checkpoint_endpoint(record, local, endpoint):
    """Observe one real local split write and three later display consumers."""
    before, after = record["ai_pre"], record["ai_post"]
    old_game, game = (bytes.fromhex(snapshot["actors"][local]["gameplay_hex"]) for snapshot in (before, after))
    display = after["shared"]["finish_timing"]
    active = struct.unpack_from("<i", bytes.fromhex(display["display_active_hex"]))[0]
    ticks = struct.unpack_from("<h", bytes.fromhex(display["display_ticks_hex"]))[0]
    if (before["session_state"] != 6 or after["session_state"] != 6 or active != 1
            or struct.unpack_from("<I", old_game, 8)[0] != 1 or struct.unpack_from("<I", game, 0x54)[0]):
        return None
    if endpoint is not None:
        if ticks not in (endpoint["display_ticks"], endpoint["display_ticks"] + 1):
            return None
        if ticks > endpoint["display_ticks"] and len(endpoint["followup_updates"]) < 3:
            endpoint["followup_updates"].append(record["id"])
        endpoint["display_ticks"] = ticks
        return endpoint
    if ticks != 0:
        return None
    old, new = (bytes.fromhex(snapshot["shared"]["checkpoint_timing"]["split_table_hex"]) for snapshot in (before, after))
    for slot in range(48):
        start = slot * 12
        clock, lap, actor = struct.unpack_from("<Iii", new, start)
        sector, rank = divmod(slot, 16)
        if (old[start:start + 12] != new[start:start + 12] and actor == local and clock > 0
                and clock == after["race_clock"] and lap >= 1 and (sector != 0 or lap > 1)
                and rank + 1 == game[0x8f]):
            return {"update": record["id"], "slot": slot, "sector": sector, "rank": rank + 1,
                    "actor": actor, "clock": clock, "lap": lap, "display_ticks": 0, "followup_updates": []}
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qualifying-startup", action="store_true",
                        help="init-only QualOn capture: two443f60 calls, creator clock and first State4 PRE; zero physical updates")
    parser.add_argument("--updates", type=int, default=16)
    parser.add_argument("--scope", choices=("vehicle", "fleet"), default="vehicle",
                        help="fleet schema2 captures all16 cars at five common boundaries; selects NoQual")
    parser.add_argument("--scene-autonomous", action="store_true",
                        help="complete cold combined NoQual profile: join scene expectations with autonomous startup/physical provenance; requires at least64 updates and full timing flags")
    parser.add_argument("--scene-frames", action="store_true",
                        help="basic fleet only: record complete primary frames with physical bodies and external QPC samples; implies --scene-journal")
    parser.add_argument("--scene-journal", action="store_true",
                        help="basic fleet only: record actual single-player View2 init/migration/camera/LOD calls and separate expectations; stop after moving published scene coverage")
    parser.add_argument("--ai-state", action="store_true",
                        help="fleet only: capture initialized live AI data and403dd0 entry/return boundaries")
    parser.add_argument("--checkpoint-timing", action="store_true",
                        help="with --ai-state: observe shared split times, race flags and session stage at all boundaries")
    parser.add_argument("--finish-timing", action="store_true",
                        help="with --checkpoint-timing: observe finish lifecycle and player split-display state")
    parser.add_argument("--traffic-controls", action="store_true", help="with --ai-state: capture actual404c1a post-route/pre-stopping boundary and live traffic/control tuning+brake-bypass provenance")
    parser.add_argument("--joined-ai", action="store_true", help="with --ai-state --traffic-controls: observe actual grid initialization/release calls and initialized route ranks for one-PRE route→traffic→controls comparison; bounded warm intervals require contact/weather provenance")
    parser.add_argument("--cold-grid-init", action="store_true",
                        help="with --joined-ai: capture fixed raw460170 entry/return actor/CONFIG/setup evidence; no initializer or replay inputs")
    parser.add_argument("--cold-grid-sequence", action="store_true",
                        help="with --cold-grid-init --joined-ai: record every loader/state9 initializer and raw first-PRE endpoint")
    parser.add_argument("--skid-dispatch", action="store_true", help="fleet only: observe actual44c540 entry/return,424d50 calls and sample decisions; independent PRE queues, no frame/physics parity")
    parser.add_argument("--wheel-queue", action="store_true", help="with --skid-dispatch: observe all actual intervening446b50 callbacks and44b690 producer transactions; callback/physics/audio inputs remain observed")
    parser.add_argument("--whine-rng", action="store_true", help="fleet only: observe every inter-update44a6f0/414240/414780 call, WHINE playing queries and actual rate/volume commands; independent phase RNG scope")
    parser.add_argument("--combined-rng", action="store_true", help="fleet only: one initial RNG with joined AI, ground surfaces, tire dispatcher and ordered wheel/motor/mute/release frame journal")
    parser.add_argument("--contact-weather-rng", action="store_true", help="with fleet: extend combined chronology with every actual impact/scrape/thunder consumer; permits bounded keys-only warmup and one initial release counter")
    parser.add_argument("--warmup-detachment-approach", action="store_true", help="with contact-weather fleet-detachment: retain the real keys-only policy through45 released updates, then measure before its first wall turn")
    parser.add_argument("--warmup-until-detached-expiry", action="store_true", help="with basic fleet-detachment: drive actual car7 sunny Donington keys, then coast to a visible resting part timer2; at most1400 unrecorded source updates")
    parser.add_argument("--warmup-until-thunder-ready", action="store_true", help="with --contact-weather-rng --weather storm: observe real updates until positive pending thunder at421aa0 entry, maximum1000; no source writes")
    parser.add_argument("--audio-null-sink", action="store_true", help="enable source audio only through an explicit private absolute Unix PULSE_SERVER socket; ALSA/OSS fallback disabled")
    parser.add_argument("--session-boundaries", action="store_true", help="with --finish-timing and --local-handoffs: observe local lap caller/classification globals and current score/podium configuration")
    parser.add_argument("--local-handoffs", action="store_true", help="with --ai-state: capture actual local402be0/402c80 call entry/return transactions")
    parser.add_argument("--nearest-recovery", action="store_true", help="with --ai-state and NoQual: capture every actual human4046a3..404731 nearest-route check after ordinary gate progression")
    parser.add_argument("--qualifying", action="store_true", help="with --local-handoffs: select actual QualOn through the original menu; whole qualifying replay remains outside scope")
    parser.add_argument("--warmup-until-local-rollout", action="store_true", help="with --qualifying: observe at most1000 coast updates until local displayed lap1 ordered point0 before automatic handback")
    parser.add_argument("--warmup-until-local-finish", action="store_true", help="with local-lap and --local-handoffs: drive real keys to the local final-lap ordered point0 endpoint; default6000 updates, configurable with --local-lap-max-updates; unrecorded physics")
    parser.add_argument("--warm-restart", action="store_true", help="with the complete retained local-finish profile: wait for the result timeout, confirm actual RACE AGAIN YES and observe one warm grid reset")
    parser.add_argument("--stop-after-local-checkpoint", action="store_true", help="complete cold local-session profile only: finalize after a new local split and three actual display-consumer wrappers; full lifecycle coverage remains pending")
    parser.add_argument("--warmup-until-nearest-ready", type=int, metavar="ORIGIN", help="with --nearest-recovery: follow live POINTS to ORIGIN then drive real keys toward its actual nearest-map candidate; stop at421aa0-entry once a human candidate is strictly closer, maximum --local-lap-max-updates; recorded block must independently prove a jump")
    parser.add_argument("--local-lap-max-updates", type=int, default=6000, help="bound local final-lap/nearest-candidate driving warmup to1..20000 actual updates")
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--scenario", choices=("coast", "accel", "accel-turn-brake", "brake-lamps", "accel-left", "accel-right", "fleet-pursuit", "fleet-detachment", "local-lap", "camera-cycle", *SHUTTLE_SCENARIOS), default="accel-turn-brake")
    parser.add_argument("--warmup-step", type=warmup_step, action="append", default=[], metavar="UPDATES:KEY,KEY",
                        help="repeatable real-key stages before recording, e.g.300:Up then40:Up,Left; maximum 1000 total updates")
    parser.add_argument("--warmup-until-ai-point", type=int, metavar="INDEX",
                        help="fleet only: coast until a rival reaches live POINTS INDEX, maximum3000 unrecorded updates; INDEX0 additionally requires checkpoint/marker0 and1<=lap<limit in active nonfinish race")
    parser.add_argument("--warmup-until-ai-finish", action="store_true",
                        help="fleet only: coast to point0 with checkpoint/marker0 on a rival's final lap, maximum3000 unrecorded updates")
    parser.add_argument("--laps", type=int, choices=(1, 2, 3, 5, 10, 15, 20),
                        help="select actual Single Race lap count through the original menu")
    parser.add_argument("--session", choices=("single-race", "time-trial"), default="single-race",
                        help="select the original session through its real RaceMenu row")
    parser.add_argument("--weather", choices=("current", *WEATHER_CHOICES), default="current",
                        help="select original weather through the real session options menu")
    parser.add_argument("--artifacts", type=Path, default=PROJECT / "build/test-artifacts")
    parser.add_argument("--runner", type=Path, default=Path.home() / ".local/share/lutris/runners/wine/ge-proton/files")
    parser.add_argument("--home", type=Path, default=Path.home())
    parser.add_argument("--memory-reader", choices=("debugger", "proc"), default="debugger",
                        help="proc: read the owned paused Linux process directly after checking bytes against WineDbg")
    parser.add_argument("--replay", type=Path, help="run tools/replay-reference.py using this native replay executable after capture")
    parser.add_argument("--verify-replay", type=Path, help="complete cold local-session fleet only: verify complete startup prefixes at64,128,256,... updates and lifecycle completion; stop on the first numerical/unsupported boundary")
    parser.add_argument("--replay-mode", choices=("sequential", "independent"), default="sequential")
    args = parser.parse_args()
    if args.qualifying_startup:
        incompatible = ("ai_state", "checkpoint_timing", "finish_timing", "traffic_controls", "joined_ai", "cold_grid_init", "cold_grid_sequence",
            "skid_dispatch", "wheel_queue", "whine_rng", "combined_rng", "contact_weather_rng", "session_boundaries", "local_handoffs",
            "nearest_recovery", "warm_restart", "scene_autonomous", "scene_frames", "scene_journal", "replay", "verify_replay", "stop_after_local_checkpoint")
        if args.session != "single-race" or any(getattr(args, name) for name in incompatible) or any(
                value for name, value in vars(args).items() if name.startswith("warmup") and value is not None and value is not False and value != []):
            parser.error("--qualifying-startup is init-only and excludes physical/replay/warmup profiles")
        args.scope, args.qualifying = "fleet", True
    if args.scene_autonomous: args.scene_frames = True
    if args.scene_frames: args.scene_journal = True
    if args.scenario == "camera-cycle" and (not args.scene_autonomous or args.updates < 96):
        parser.error("camera-cycle requires --scene-autonomous and at least96 physical updates")
    if args.scene_frames and (args.qualifying or args.session != "single-race"):
        parser.error("--scene-frames requires ordinary NoQual single-race")
    if args.scene_journal and not args.scene_autonomous and (args.scope != "fleet" or args.ai_state or args.combined_rng or args.contact_weather_rng
            or args.joined_ai or args.skid_dispatch or args.wheel_queue or args.whine_rng or args.warm_restart
            or args.warmup_step or args.warmup_until_ai_point is not None or args.warmup_until_ai_finish
            or args.warmup_until_local_finish or args.warmup_until_local_rollout or args.warmup_until_nearest_ready is not None
            or args.warmup_detachment_approach or args.warmup_until_detached_expiry or args.warmup_until_thunder_ready):
        parser.error("--scene-journal requires basic fleet capture without other hooks or unrecorded warmup")
    if args.contact_weather_rng: args.combined_rng = True
    if args.warmup_until_detached_expiry and (
            args.scope != "fleet" or args.scenario != "fleet-detachment" or args.weather != "sunny"
            or args.session != "single-race" or args.ai_state or args.combined_rng or args.joined_ai
            or args.skid_dispatch or args.wheel_queue or args.whine_rng or args.qualifying
            or args.warmup_step or args.warmup_detachment_approach or args.warmup_until_thunder_ready
            or args.warmup_until_ai_point is not None or args.warmup_until_ai_finish
            or args.warmup_until_local_rollout or args.warmup_until_local_finish
            or args.warmup_until_nearest_ready is not None):
        parser.error("--warmup-until-detached-expiry requires basic --scope fleet --scenario fleet-detachment --weather sunny and excludes other hooks/warmups")
    if args.warmup_detachment_approach and (not args.contact_weather_rng or args.scenario != "fleet-detachment" or args.warmup_step or args.warmup_until_thunder_ready or args.warmup_until_ai_point is not None or args.warmup_until_ai_finish):
        parser.error("--warmup-detachment-approach requires contact-weather fleet-detachment and excludes other warmups")
    if args.warmup_until_thunder_ready and (not args.contact_weather_rng or args.weather != "storm" or args.warmup_step):
        parser.error("--warmup-until-thunder-ready requires contact/weather storm and excludes fixed warmup stages")
    if args.combined_rng:
        if args.scope != "fleet": parser.error("--combined-rng requires --scope fleet")
        args.ai_state = args.traffic_controls = args.joined_ai = args.skid_dispatch = args.wheel_queue = args.whine_rng = True
    if args.cold_grid_init and not args.joined_ai:
        parser.error("--cold-grid-init requires --joined-ai")
    if args.cold_grid_sequence and (not args.cold_grid_init or not args.joined_ai):
        parser.error("--cold-grid-sequence requires --cold-grid-init --joined-ai")
    if args.skid_dispatch and args.scope != "fleet":
        parser.error("--skid-dispatch requires --scope fleet")
    if args.wheel_queue and (not args.skid_dispatch or args.joined_ai) and not args.combined_rng:
        parser.error("--wheel-queue requires --skid-dispatch and excludes joined-AI release hooks")
    if args.whine_rng and (args.scope != "fleet" or args.joined_ai or args.wheel_queue) and not args.combined_rng:
        parser.error("--whine-rng requires fleet and excludes joined-AI/wheel-queue frame hooks")
    if args.scenario in ("fleet-pursuit", "fleet-detachment") and args.scope != "fleet":
        parser.error(args.scenario + " requires --scope fleet")
    if args.scenario == "local-lap" and not args.ai_state:
        parser.error("local-lap requires fleet --ai-state")
    if not 1 <= args.local_lap_max_updates <= 20000:
        parser.error("--local-lap-max-updates must be1..20000")
    if args.warmup_until_local_finish and (args.scenario != "local-lap" or not args.local_handoffs or args.warmup_until_local_rollout or args.warmup_until_ai_finish or args.warmup_until_ai_point is not None):
        parser.error("--warmup-until-local-finish requires local-lap --local-handoffs and excludes other targeted warmup")
    if args.traffic_controls and not args.ai_state:
        parser.error("--traffic-controls requires --ai-state")
    joined_finish_warmup = args.contact_weather_rng and args.finish_timing and args.warmup_until_ai_finish
    local_session_flags = args.contact_weather_rng and args.session_boundaries and args.local_handoffs
    cold_local_session_profile = local_session_flags and args.cold_grid_sequence
    local_session_profile = local_session_flags and (args.warmup_until_local_finish or cold_local_session_profile)
    journal_clock_profile = cold_local_session_profile and (args.warm_restart or args.scene_autonomous)
    if args.scene_autonomous and (not cold_local_session_profile or args.warm_restart or args.updates < 64 or args.verify_replay):
        parser.error("--scene-autonomous requires the complete cold local-session profile, at least64 updates, and no warm-restart/background verifier")
    if cold_local_session_profile and (args.scenario not in (("local-lap", "camera-cycle") if args.scene_autonomous else ("local-lap",)) or args.qualifying or args.warmup_step
            or args.warmup_until_local_finish or args.warmup_until_thunder_ready or args.warmup_until_ai_finish
            or args.warmup_until_ai_point is not None or args.warmup_until_local_rollout
            or args.warmup_until_nearest_ready is not None or args.warmup_detachment_approach or args.warmup_until_detached_expiry):
        parser.error("Cold local session requires local-lap and the complete cold grid without unrecorded warmup")
    if args.verify_replay and not cold_local_session_profile:
        parser.error("--verify-replay requires the complete cold local-session fleet profile")
    if args.stop_after_local_checkpoint and not (cold_local_session_profile and args.verify_replay):
        parser.error("--stop-after-local-checkpoint requires the complete cold local-session profile and --verify-replay")
    if args.warm_restart and not local_session_profile:
        parser.error("--warm-restart requires the complete retained local-finish profile")
    if joined_finish_warmup and (args.cold_grid_init or args.cold_grid_sequence or args.warmup_step or args.warmup_until_thunder_ready):
        parser.error("Targeted joined AI finish excludes cold-grid and other warmup profiles")
    if local_session_profile and not cold_local_session_profile and (args.cold_grid_init or args.cold_grid_sequence or args.warmup_step or args.warmup_until_thunder_ready):
        parser.error("Targeted local session excludes cold-grid and other warmup profiles")
    if args.joined_ai and (not args.traffic_controls or args.qualifying or args.local_handoffs and not local_session_profile or args.nearest_recovery or args.warmup_step and not args.contact_weather_rng or args.warmup_until_local_finish and not local_session_profile or args.warmup_until_local_rollout or args.warmup_until_nearest_ready is not None or args.warmup_until_ai_finish and not joined_finish_warmup or args.warmup_until_ai_point is not None):
        parser.error("--joined-ai requires --traffic-controls and NoQual; targeted finish profiles require contact/weather and complete timing/session provenance")
    if args.nearest_recovery and (not args.ai_state or args.qualifying or args.local_handoffs):
        parser.error("--nearest-recovery requires --ai-state NoQual without controller handoffs")
    if args.warmup_until_nearest_ready is not None and (not args.nearest_recovery or args.warmup_until_local_finish or args.warmup_until_local_rollout or args.warmup_until_ai_finish or args.warmup_until_ai_point is not None):
        parser.error("--warmup-until-nearest-ready requires --nearest-recovery and excludes other targeted warmup")
    if args.session_boundaries and (not args.finish_timing or not args.local_handoffs):
        parser.error("--session-boundaries requires --finish-timing and --local-handoffs")
    if args.local_handoffs and not args.ai_state:
        parser.error("--local-handoffs requires --ai-state")
    if args.qualifying and not args.local_handoffs and not args.qualifying_startup:
        parser.error("--qualifying requires --local-handoffs")
    if args.warmup_until_local_rollout and (not args.qualifying or args.warmup_step or args.warmup_until_ai_finish or args.warmup_until_ai_point is not None):
        parser.error("--warmup-until-local-rollout requires --qualifying and excludes other warmup options")
    if args.ai_state and args.scope != "fleet":
        parser.error("--ai-state requires --scope fleet")
    if args.checkpoint_timing and not args.ai_state:
        parser.error("--checkpoint-timing requires --ai-state")
    if args.finish_timing and not args.checkpoint_timing:
        parser.error("--finish-timing requires --checkpoint-timing")
    if args.warmup_until_ai_point is not None and args.scope != "fleet":
        parser.error("--warmup-until-ai-point requires --scope fleet")
    if args.warmup_until_ai_finish and (args.scope != "fleet" or not args.finish_timing or args.warmup_until_ai_point is not None):
        parser.error("--warmup-until-ai-finish requires fleet --finish-timing and excludes --warmup-until-ai-point")
    if args.laps is not None and args.session != "single-race":
        parser.error("--laps requires Single Race")
    if args.scope == "fleet" and (args.session != "single-race" or args.replay):
        parser.error("Fleet capture requires Single Race; use replay-fleet-reference.py separately")
    if args.updates < 1 or args.timeout <= 0:
        parser.error("--updates and --timeout must be positive")
    if sum(updates for updates, _ in args.warmup_step) > 1000:
        parser.error("warmup stages must total at most 1000 updates")
    executable = PROJECT / "references/toca-1.1a/tourcars.exe"
    checksum = f"{zlib.crc32(executable.read_bytes()):08x}"
    if checksum != "8ce56d41":
        raise RuntimeError("Canonical 1.1a executable checksum differs")
    spec = importlib.util.spec_from_file_location("isolated", PROJECT / "tools/run-isolated.py")
    isolated = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(isolated)
    args.artifacts.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="reference-trace-", dir=args.artifacts.resolve()))
    for child in ("home", "data", "config", "cache", "state", "runtime", "tmp", "work"):
        (directory / child).mkdir(mode=0o700)
    runtime = isolated.Runtime(directory)
    deadline = time.monotonic() + args.timeout
    print(f"Artifacts: {directory}", flush=True)
    dbg = None
    warm_restart = None
    scene_journal = None
    qualifying_startup = None
    verifier = RetainedPrefixVerifier(directory, args.verify_replay) if args.verify_replay else None
    capture_finalized = False
    source_error = verification_error = None
    previous_handlers = {sig: signal.signal(sig, isolated.interrupted) for sig in (signal.SIGTERM, signal.SIGINT)}
    try:
        environment = original_environment(isolated.private_environment(directory), args.home.resolve(), args.runner.resolve())
        audio_flag, audio_provenance = reference_audio_route(environment, args.audio_null_sink)
        isolated.start_display(runtime, environment, "800x600x24")
        with (directory / "reference.log").open("wb") as log:
            game = runtime.launch([str(PROJECT / "tools/run-reference.sh"), audio_flag], environment, log)
        wine = str(args.runner.resolve() / "bin/wine")
        debugger_exe = str(args.runner.resolve() / "lib/wine/i386-windows/winedbg.exe")
        wine_pid = None
        startup_deadline = min(deadline, time.monotonic() + 20)
        # This line is emitted only after run-reference.sh acquired its lock,
        # verified/copied 1.1a and finished preparing its serialized prefix.
        while time.monotonic() < startup_deadline:
            if game.poll() is not None:
                raise RuntimeError("Reference launcher exited before obtaining its serialized runtime")
            if b"TOCA 1.1a " in (directory / "reference.log").read_bytes():
                break
            time.sleep(.05)
        else:
            raise RuntimeError("Reference launcher did not confirm its owned runtime")
        # The launcher marker precedes Wine's GUI bootstrap. Wait for the
        # actual private-display game window before asking a second Wine
        # process to enumerate/attach; otherwise info-proc can race bootstrap.
        while time.monotonic() < startup_deadline:
            window = subprocess.run(["xdotool", "search", "--name", "^TOCA Touring Car Championship$"],
                                    env=environment, capture_output=True, timeout=5)
            if window.returncode == 0:
                break
            time.sleep(.1)
        else:
            raise RuntimeError("Owned original game window did not initialize")
        while time.monotonic() < startup_deadline:
            if game.poll() is not None:
                raise RuntimeError("Reference launcher exited before attachment; see reference.log")
            query = subprocess.run([wine, debugger_exe, "--command", "info proc"], env=environment,
                                   capture_output=True, text=True, timeout=10)
            matches = re.findall(r"^\s*([0-9a-fA-F]+)\s+\d+\s+.*tourcars\.exe", query.stdout, re.M)
            if len(matches) == 1:
                wine_pid = int(matches[0], 16)
                break
            if len(matches) > 1:
                raise RuntimeError("Multiple original processes; refusing ambiguous attachment")
            time.sleep(.2)
        if wine_pid is None:
            raise RuntimeError("Owned original process not found")
        owned = owned_game_processes(environment)
        if game.poll() is not None or len(owned) != 1:
            raise RuntimeError(f"Cannot prove a single owned original process: {owned}")
        # The Wine PID is unique within this prefix. Its matching Unix game
        # must also retain our exact private display/runtime before attachment.
        if owned_game_processes(environment) != owned:
            raise RuntimeError("Owned original process changed before attachment")
        process = subprocess.Popen([wine, debugger_exe, str(wine_pid)], env=environment,
                                   cwd=directory / "work", stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        runtime.groups.append(process)
        dbg = Debugger(process, directory, deadline)
        dbg.read_prompt()
        if args.qualifying_startup:
            qualifying_startup = QualifyingStartupRecorder(dbg, owned[0] if args.memory_reader == "proc" else None)
        if args.scene_journal:
            scene_journal = SCENE.Recorder(dbg, directory / "scene-journal.jsonl", checksum,
                                           owned[0] if args.memory_reader == "proc" else None,
                                           directory / "scene-frames.jsonl" if args.scene_frames else None,
                                           directory.name if args.scene_autonomous else None, args.scenario == "camera-cycle")
            camera_held = set()
            if args.scenario == "camera-cycle":
                def inject_camera_input(physical_id):
                    requested = SCENE.FRAMES.camera_keys(physical_id)
                    desired = {key for name, key in (("cycle_held", "c"), ("rear_held", "z")) if requested[name]}
                    for key in sorted(camera_held - desired): keyboard(environment, "keyup", key)
                    for key in sorted(desired - camera_held): keyboard(environment, "keydown", key)
                    camera_held.clear()
                    camera_held.update(desired)
                    return requested
                scene_journal.frames.configure_camera_inputs(inject_camera_input)
            if args.scene_autonomous:
                scene_journal.frames.configure_combined(lambda: contact_order(dbg), lambda: capture_qpc_values(dbg))
        pre_break = None if args.scene_frames else dbg.breakpoint(0x421AA0)
        grid_init_break = dbg.breakpoint(0x460170) if args.joined_ai else None
        grid_initialization = None
        cold_grid_initialization = {} if args.cold_grid_init else None
        sequence_outer_break = dbg.breakpoint(0x447980) if args.cold_grid_sequence else None
        timer_creation_break = dbg.breakpoint(FLEET.COLD_TIMER_CREATION_MANIFEST["entry"]) if journal_clock_profile else None
        sequence_outer = None
        cold_grid_sequence = {"events": [], "first_pre": None} if args.cold_grid_sequence else None

        options_address = 0x469979 if args.session == "time-trial" else 0x469713
        weather_break = dbg.breakpoint(options_address)
        race_break = dbg.breakpoint(0x46E6C2)
        dbg.send("cont")
        # Default frontend path: Enter accepts language/race/options/name/car/
        # track screens. Never force process state or invent physics inputs.
        navigation = []
        startup_stops = 0
        while True:
            try:
                response = dbg.read_prompt(.75)
                if qualifying_startup is not None and qualifying_startup.intercept(response):
                    dbg.send("cont")
                    continue
                if scene_journal is not None and scene_journal.intercept(response):
                    dbg.send("cont")
                    continue
                if "00421aa0" not in response.lower():
                    if timer_creation_break is not None and "00447a70" in response.lower():
                        dbg.confirm_pause()
                        if sequence_outer is not None:
                            raise ValueError("Cold timer creation overlaps an unmatched grid initializer")
                        capture_cold_timer_creation(dbg, cold_grid_sequence)
                        dbg.command(f"delete break {timer_creation_break}")
                        timer_creation_break = None
                        dbg.send("cont")
                        continue
                    if grid_init_break is not None and "00460170" in response.lower():
                        dbg.confirm_pause()
                        if args.cold_grid_sequence:
                            event = capture_cold_grid_sequence_call(dbg, cold_grid_sequence["events"], sequence_outer)
                            sequence_outer = None
                            if grid_initialization is None:
                                grid_initialization = event["initialization"]
                                cold_grid_initialization = event["boundaries"]
                        else:
                            grid_initialization = capture_start_call(dbg, 0x460170, cold_grid=cold_grid_initialization)
                            dbg.command(f"delete break {grid_init_break}")
                            grid_init_break = None
                        dbg.send("cont")
                        continue
                    if sequence_outer_break is not None and "00447980" in response.lower():
                        dbg.confirm_pause()
                        if sequence_outer is not None:
                            raise ValueError("Cold-grid sequence outer entry was not followed by actual460170")
                        sequence_outer = dbg.word(register_address(dbg.command("print $esp")))
                        dbg.send("cont")
                        continue
                    if race_break is not None and "0046e6c2" in response.lower():
                        navigation.append(select_session(dbg, environment, args.session))
                        dbg.command(f"delete break {race_break}")
                        race_break = None
                        dbg.send("cont")
                        continue
                    if weather_break is not None and f"{options_address:08x}" in response.lower():
                        target = None if args.weather == "current" else WEATHER_CHOICES[args.weather]
                        if args.session == "single-race":
                            navigation.append(select_qualifying(dbg, environment, args.scope == "vehicle" or args.qualifying))
                            if args.laps is not None:
                                navigation.append(select_laps(dbg, environment, args.laps))
                        navigation.append(select_weather(dbg, environment, target, args.session))
                        dbg.command(f"delete break {weather_break}")
                        weather_break = None
                        dbg.send("cont")
                        continue
                    if "ntdll+" in response and startup_stops < 3:
                        startup_stops += 1
                        dbg.send("cont")
                        continue
                    raise RuntimeError(f"Unexpected frontend stop: {response}")
                dbg.confirm_pause()
                if qualifying_startup is not None:
                    events = qualifying_startup.endpoint()
                    runtime_executable = args.home / ".cache/toca-1.1a/wine/drive_c/TOCA/tourcars.exe"
                    if f"{zlib.crc32(runtime_executable.read_bytes()):08x}" != checksum or f"{zlib.crc32(executable.read_bytes()):08x}" != checksum:
                        raise RuntimeError("Qualifying source executable provenance changed")
                    header = {"type": "header", "schema": 1, "scope": "qualifying-startup", "exe_crc32": checksum,
                        "manifest": FLEET.QUALIFYING_STARTUP_MANIFEST, "navigation": navigation, "audio_provenance": audio_provenance}
                    footer = FLEET.write_qualifying_startup_capture(directory / "trace.jsonl", header, events)
                    qualifying_startup.close()
                    dbg.command(f"delete break {pre_break}")
                    dbg.command("detach")
                    dbg.send("quit")
                    process.wait(timeout=5)
                    (directory / "complete.json").write_text(json.dumps(footer) + "\n")
                    capture_finalized = True
                    print(f"QUALIFYING STARTUP COMPLETE: zero physical updates: {directory / 'trace.jsonl'}", flush=True)
                    return 0
                if args.cold_grid_sequence:
                    if journal_clock_profile and (timer_creation_break is not None or "timer_creation" not in cold_grid_sequence):
                        raise ValueError("Cold grid reached first PRE without actual timer creation")
                    if sequence_outer is not None:
                        raise ValueError("Cold-grid sequence has an unmatched outer initializer")
                    cold_grid_sequence["first_pre"] = {
                        "caller_return_address": dbg.word(register_address(dbg.command("print $esp"))),
                        "snapshot": FLEET.capture_cold_grid_boundary(dbg.paused_memory(), "421aa0-entry")}
                    FLEET.validate_cold_grid_sequence(cold_grid_sequence, grid_initialization, cold_grid_initialization)
                    dbg.command(f"delete break {grid_init_break}")
                    dbg.command(f"delete break {sequence_outer_break}")
                    grid_init_break = sequence_outer_break = None
                break
            except TimeoutError:
                if scene_journal is not None and dbg.confirmed_pause:
                    raise # A paused source-hook failure is not a menu wait.
                if time.monotonic() >= deadline - 30:
                    raise RuntimeError("Original frontend did not reach the driving update")
                keyboard(environment, "key", "--clearmodifiers", "--delay", "80", "Return")
                navigation.append({"key": "Return", "elapsed": args.timeout - (deadline - time.monotonic())})
        if args.memory_reader == "proc" and dbg.memory_fd is None:
            dbg.enable_process_memory(owned[0])
        warmup = []
        while True:
            actor = dbg.word(0x4D37A0) & 0xff
            if actor >= 16:
                raise RuntimeError("Invalid local actor during original approach")
            mode = dbg.word(LAYOUT["dynamics_base"] + actor * LAYOUT["dynamics_stride"] + 0x27c)
            if not warmup:
                config = dbg.word(0x4B0660)
                startup_state = {"requested_session": args.session, "source_mode": dbg.word(config + 4),
                                 "session_state": dbg.word(config + 0x5f8) & 0xffff,
                                 "actor": actor, "transmission_mode": mode,
                                 "allocated_actor_count": dbg.word(0x4C5D6E) & 0xffff,
                                 "configuration_hex": dbg.memory(config, 0x620).hex()}
                (directory / "startup-state.json").write_text(json.dumps(startup_state, indent=2) + "\n")
            if mode in (1, 2) or args.scope == "fleet":
                break
            if mode != 0 or len(warmup) >= 1000:
                raise RuntimeError("Original automatic approach did not restore player control")
            warmup.append(dbg.word(GLOBALS["update_counter"]))
            dbg.stop_at(0x421AA0)
        ai_initial_state = None
        traffic_initial_state = None
        joined_initial_state = None
        def capture_world(boundary):
            nonlocal ai_initial_state, traffic_initial_state, joined_initial_state
            reader = dbg
            ai_state = None
            if args.scope == "fleet":
                reader = dbg.paused_memory()
                reader.preload(FLEET.fixed_reads(GLOBALS, ai_state=args.ai_state, checkpoint_timing=args.checkpoint_timing, finish_timing=args.finish_timing, session_boundaries=args.session_boundaries, traffic_controls=args.traffic_controls))
                config = reader.word(0x4B0660)
                reader.preload([(config, 0x62c if args.session_boundaries else 0x620), *FLEET.registered_prop_reads(reader)])
                if args.ai_state:
                    ai_state, ai_content = FLEET.capture_ai_state(reader)
                    if ai_initial_state is None:
                        ai_initial_state = ai_content
            local = Debugger.snapshot(reader, boundary)
            if args.scope == "fleet" and (local["source_mode"] != 0 or local["allocated_actor_count"] != 16):
                raise RuntimeError("Original did not enter16-actor Single Race; refusing inactive actor reads")
            timing = FLEET.capture_checkpoint_timing(reader, config) if args.checkpoint_timing else None
            finish = {name + "_hex": reader.memory(address, size).hex() for name, (address, size) in FLEET.FINISH_TIMING_BLOCKS.items()} if args.finish_timing else None
            session_boundary = FLEET.capture_session_boundary(reader, config) if args.session_boundaries else None
            traffic_control = None
            if args.traffic_controls:
                traffic_control, content = FLEET.capture_traffic_control(reader, config, FLEET.ai_content(ai_initial_state)["point_count"])
                if traffic_initial_state is None:
                    traffic_initial_state = content
                elif content != traffic_initial_state:
                    raise ValueError("Live traffic brake-bypass setup changed during capture")
            result = FLEET.capture(reader, boundary, local, ai_state, timing, finish, session_boundary, traffic_control) if args.scope == "fleet" else local
            if local_session_profile:
                result["shared"]["lifecycle"]["local_session_entry_flag"] = reader.word(0x4af050)
            if args.joined_ai:
                count = FLEET.ai_content(ai_initial_state)["point_count"]
                reader.preload([(0x4bd1a8, count * 4), (0x4bccd0, 4)])
                table = reader.memory(0x4bd1a8, count * 4)
                current = {"cumulative_route_hex": table.hex(), "cumulative_route_crc32": f"{zlib.crc32(table):08x}",
                           "rank_threshold": signed(reader.word(0x4bccd0))}
                FLEET.joined_content(current, count)
                if joined_initial_state is None:
                    joined_initial_state = current
                elif current != joined_initial_state:
                    raise ValueError("Initialized joined route/rank setup changed")
                result["shared"]["joined_ai"] = {"cumulative_route_crc32": current["cumulative_route_crc32"],
                                                  "rank_threshold": current["rank_threshold"], "point_count": count}
            return result
        warmup_provenance = None
        detachment_policy = None
        if args.warmup_until_detached_expiry:
            detachment_policy, expiry_ready = warmup_until_detached_expiry(dbg, environment)
            warmup_provenance = {"targeted_detached_expiry": expiry_ready, "steps": [], "keys_only": True,
                                 "continuous_from_grid": False, "recorded_boundaries": False}
        if args.warmup_detachment_approach:
            detachment_policy, approached = warmup_detachment_approach(dbg, environment)
            warmup_provenance = {"targeted_detachment": approached, "steps": [], "keys_only": True, "continuous_from_grid": False, "recorded_boundaries": False}
        if args.warmup_step:
            warmup_provenance = {"steps": [warmup_keys(dbg, environment, updates, keys)
                                            for updates, keys in args.warmup_step],
                                 "keys_only": True, "continuous_from_grid": False,
                                 "recorded_boundaries": False}
        if args.warmup_until_thunder_ready:
            warmup_provenance = {"targeted_thunder": warmup_until_thunder_ready(dbg, environment),
                                 "steps": [], "keys_only": True, "continuous_from_grid": False, "recorded_boundaries": False}
        if args.warmup_until_ai_point is not None or args.warmup_until_ai_finish:
            target = 0 if args.warmup_until_ai_finish else args.warmup_until_ai_point
            targeted = warmup_until_ai_point(dbg, environment, target, finish_ready=args.warmup_until_ai_finish)
            if warmup_provenance is None:
                warmup_provenance = {"steps": [], "keys_only": True, "continuous_from_grid": False, "recorded_boundaries": False}
            warmup_provenance["targeted_ai_point"] = targeted
        if args.warmup_until_local_rollout:
            warmup_provenance = {"targeted_local_rollout": warmup_until_local_rollout(dbg, environment),
                                 "keys_only": True, "recorded_boundaries": False, "continuous_from_grid": False}
        if args.warmup_until_nearest_ready is not None:
            reader = dbg.paused_memory()
            reader.preload([*FLEET.AI_STATE_BLOCKS.values(), (0x4bcce0, 256)])
            _, observed_content = FLEET.capture_ai_state(reader)
            nearest_policy = NearestRecoveryPolicy(bytes.fromhex(observed_content["points_hex"]), reader.memory(0x4bcce0, 256),
                                                   local_driving_snapshot(dbg), args.warmup_until_nearest_ready)
            driven = warmup_nearest_ready(dbg, environment, nearest_policy, directory / "nearest-driving-warmup.jsonl", args.local_lap_max_updates)
            if warmup_provenance is None:
                warmup_provenance = {"steps": [], "keys_only": True, "recorded_boundaries": False, "continuous_from_grid": False}
            warmup_provenance["targeted_nearest_ready"] = driven
        local_policy = None
        if args.scenario == "local-lap":
            reader = dbg.paused_memory()
            reader.preload(FLEET.AI_STATE_BLOCKS.values())
            _, observed_content = FLEET.capture_ai_state(reader)
            local_policy = LocalLapPolicy(bytes.fromhex(observed_content["points_hex"]), local_driving_snapshot(dbg))
            if args.warmup_until_local_finish:
                driven = warmup_local_finish(dbg, environment, local_policy, directory / "local-driving-warmup.jsonl", args.qualifying, args.local_lap_max_updates)
                if warmup_provenance is None:
                    warmup_provenance = {"steps": [], "keys_only": True, "continuous_from_grid": False, "recorded_boundaries": False}
                warmup_provenance["targeted_local_finish"] = driven
        initial = capture_world("421aa0-entry")
        if args.joined_ai:
            validate_joined_capture_start(grid_initialization, initial, selected_interval=args.contact_weather_rng)
        if args.cold_grid_sequence:
            FLEET.validate_cold_grid_sequence(cold_grid_sequence, grid_initialization, cold_grid_initialization, initial)
        if args.scope == "fleet":
            config = initial["configuration_pointer"]
            # 449f50 and44f0a3 loader: NoQual(+20=1) enters grid state9;
            # actual racing is6. State4 is qualifying/TimeTrial approach.
            if initial["allocated_actor_count"] != 16 or initial["session_state"] not in ((4, 6) if args.qualifying else (9, 6)) or (dbg.word(config + 0x20), dbg.word(config + 0xac)) != (0 if args.qualifying else 1, 1):
                raise RuntimeError("Original fleet startup is not a16-actor one-player NoQual grid/race (state9/6)")
        if args.weather != "current" and (weather_break is not None or initial["weather"] != WEATHER_CHOICES[args.weather]):
            raise RuntimeError("Original resolved weather differs from the requested menu selection")
        expected_source_mode = 2 if args.session == "time-trial" else 0
        if initial["source_mode"] != expected_source_mode or (args.session == "time-trial" and initial["actor"] != 0):
            raise RuntimeError("Original did not enter the requested session through the frontend")
        if args.session == "time-trial" and (race_break is not None or weather_break is not None):
            raise RuntimeError("Original TimeTrial did not reach its live player update boundary")
        if local_policy is not None:
            policy = local_policy
        elif args.scenario == "fleet-pursuit":
            policy = FleetPursuitPolicy(initial)
        elif args.scenario == "fleet-detachment":
            policy = detachment_policy if detachment_policy is not None else FleetDetachmentPolicy(initial)
        else:
            policy = InputPolicy(args.scenario, args.updates, initial)
        if args.scenario == "camera-cycle":
            policy.parameters.update(camera_input_poll="44a1d0-entry", camera_keys_only=True, camera_schedule_updates=96)
        coverage = FLEET.Coverage(initial) if args.scope == "fleet" else Coverage(initial)
        config_pointer = initial["configuration_pointer"]
        runtime_executable = args.home / ".cache/toca-1.1a/wine/drive_c/TOCA/tourcars.exe"
        if f"{zlib.crc32(runtime_executable.read_bytes()):08x}" != checksum:
            raise RuntimeError("Owned runtime executable differs from the canonical copy")
        header = {"type": "header", "schema": 2 if args.scope == "fleet" else 1, "exe_crc32": checksum,
                  "audio_provenance": audio_provenance,
                  "scenario": args.scenario, "layout": LAYOUT, "globals": GLOBALS,
                  "session": args.session, "source_mode": initial["source_mode"],
                  "local_actor": initial["actor"], "initial_session_state": initial["session_state"],
                  "allocated_actor_count": initial["allocated_actor_count"],
                  "active_ground_actor_count": 1 if initial["source_mode"] == 2 else initial["allocated_actor_count"],
                  "scenario_parameters": policy.parameters,
                  "controlled_delta": None, "debugger_pauses_perturb_qpc": True,
                  "memory_reader": args.memory_reader,
                  "input_width_bits": {"left": 32, "right": 32, "throttle": 32, "brake": 32, "gear_edge": 8},
                  "wine_pid": wine_pid, "navigation": navigation,
                  "unix_pid": owned[0], "display": environment["DISPLAY"],
                  "private_runtime_dir": environment["XDG_RUNTIME_DIR"],
                  "automatic_approach_update_counters": warmup,
                  "unrecorded_keyboard_warmup": warmup_provenance,
                  "initial_configuration": {name: initial[name] for name in ("track_id", "weather", "car_id", "transmission_mode", "steering_profile")},
                  "configuration_hex": dbg.memory(config_pointer, 0x620).hex(),
                  "boundaries": {"pre": "421aa0-entry", "consumed": "415090-entry", "post": "421aa0-return"}}
        if args.scope == "fleet":
            header.update(scope="fleet", boundaries=FLEET.BOUNDARIES, actor_count=16, physical_root_body_offset=0,
                          shared_blocks=FLEET.BLOCKS, shared_words=FLEET.WORDS, props_layout=FLEET.PROPS,
                          extra_provenance_blocks=FLEET.PROVENANCE_BLOCKS, lifecycle_blocks=FLEET.LIFECYCLE_BLOCKS,
                          sign_model_stride=68,
                          replay_scopes=["independent-physics", "continuous-physics-inputs"],
                          autonomous_ai_parity=False)
            if args.ai_state:
                header.update(ai_state_manifest=FLEET.AI_STATE_MANIFEST, ai_initial_state=ai_initial_state,
                              ai_boundaries=FLEET.AI_BOUNDARIES)
            if args.skid_dispatch:
                header["skid_dispatch_manifest"] = FLEET.SKID_MANIFEST
            if args.wheel_queue:
                header.update(wheel_queue_manifest=FLEET.WHEEL_QUEUE_MANIFEST,
                              human_lamps_manifest=FLEET.HUMAN_LAMPS_MANIFEST,
                              wheel_queue_initial_state=FLEET.capture_wheel_state(dbg.paused_memory(), "421aa0-entry"))
            if args.whine_rng:
                header.update(whine_manifest=FLEET.WHINE_MANIFEST,
                              whine_initial_state=FLEET.capture_whine_state(dbg.paused_memory()),
                              whine_tables=FLEET.capture_whine_tables(dbg.paused_memory()))
            if args.scene_autonomous:
                header.update(scene_autonomous_manifest=FLEET.SCENE_AUTONOMOUS_MANIFEST, scene_capture_id=directory.name)
                if args.scenario == "camera-cycle":
                    header["scene_camera_controls_manifest"] = FLEET.SCENE_CAMERA_CONTROLS_MANIFEST
            if args.combined_rng:
                header.update(brake_publication_manifest=FLEET.BRAKE_PUBLICATION_MANIFEST,
                              combined_rng_manifest=FLEET.COMBINED_MANIFEST,
                              combined_audio_initial=FLEET.capture_combined_audio(dbg.paused_memory()))
            if args.contact_weather_rng:
                header.update(contact_weather_manifest=FLEET.CONTACT_WEATHER_MANIFEST, contact_play_returns=True,
                              contact_audio_initial=FLEET.capture_contact_initial(dbg.paused_memory()),
                              combined_initial_release_counter=initial["shared"]["ai_state"]["release_counter"])
            if args.checkpoint_timing:
                header["checkpoint_timing_manifest"] = FLEET.CHECKPOINT_TIMING_MANIFEST
            if args.finish_timing:
                header["finish_timing_manifest"] = FLEET.FINISH_TIMING_MANIFEST
            if args.traffic_controls:
                header.update(traffic_control_manifest=FLEET.TRAFFIC_CONTROL_MANIFEST, traffic_initial_state=traffic_initial_state,
                              traffic_boundaries=FLEET.TRAFFIC_BOUNDARIES)
            if args.session_boundaries:
                header["session_boundary_manifest"] = FLEET.SESSION_BOUNDARY_MANIFEST
            if local_session_profile:
                header.update(local_session_lifecycle_manifest=FLEET.LOCAL_SESSION_LIFECYCLE_MANIFEST,
                              local_session_initial_state={"entry_flag": initial["shared"]["lifecycle"]["local_session_entry_flag"]})
            if args.warm_restart:
                header["warm_restart_manifest"] = FLEET.WARM_RESTART_MANIFEST
            if args.local_handoffs:
                header["local_handoff_manifest"] = FLEET.LOCAL_HANDOFF_MANIFEST
                header["qualifying"] = args.qualifying
            if args.nearest_recovery:
                nearest_map = dbg.memory(0x4bcce0, 256)
                header.update(nearest_recovery_manifest=FLEET.NEAREST_RECOVERY_MANIFEST,
                              nearest_recovery_initial_state={"nearest_map_hex": nearest_map.hex(), "nearest_map_crc32": f"{zlib.crc32(nearest_map):08x}"})
                FLEET.nearest_map_content(header["nearest_recovery_initial_state"], FLEET.ai_content(ai_initial_state)["point_count"])
            if args.joined_ai:
                header.update(joined_ai_manifest=FLEET.JOINED_AI_MANIFEST, joined_initial_state=joined_initial_state,
                              grid_initialization=grid_initialization)
                if args.cold_grid_init:
                    header.update(cold_grid_initialization_manifest=FLEET.COLD_GRID_INIT_MANIFEST,
                                  cold_grid_initialization=cold_grid_initialization)
                if args.cold_grid_sequence:
                    header.update(cold_grid_sequence_manifest=FLEET.COLD_GRID_SEQUENCE_MANIFEST,
                                  cold_grid_sequence=cold_grid_sequence)
                if journal_clock_profile:
                    header.update(cold_timer_creation_manifest=FLEET.COLD_TIMER_CREATION_MANIFEST,
                                  qpc_sample_manifest=FLEET.QPC_SAMPLE_MANIFEST)
        with (directory / "trace.jsonl").open("w") as trace:
            trace_crc32 = 0
            written_position = 0
            def write_record(record):
                nonlocal trace_crc32, written_position
                line = json.dumps(record) + "\n"
                trace_crc32 = zlib.crc32(line.encode(), trace_crc32)
                trace.write(line)
                trace.flush()
                written_position = trace.tell()
            if not args.combined_rng: write_record(header)
            held = set()
            release_events = []
            wheel_callbacks = []
            whine_callbacks = []
            combined_journal = []
            derived_release_counter = header.get("combined_initial_release_counter", 0)
            contact_audio = ContactAudioRecorder(dbg) if args.contact_weather_rng else None
            brake_publication = BrakePublicationRecorder(dbg) if args.combined_rng else None
            release_break = dbg.breakpoint(0x448130) if args.joined_ai and not args.combined_rng else None
            detached_records = [] if args.warmup_until_detached_expiry else None
            timing_records = [] if joined_finish_warmup else None
            local_records = [] if local_session_profile else None
            warm_restart = WarmRestartCapture(environment) if args.warm_restart else None
            verified_difference = False
            pending_clock_extra = initial["extra_update"]
            physical_in_frame = 0
            next_verification = 64
            checkpoint_endpoint = None
            bounded_stop = False
            captured_updates = 0
            closed_prefix = {"updates": 0, "source_bytes": 0, "prefix_crc32": "00000000"}
            try:
                for index in range(args.updates + int(args.scene_autonomous)):
                    pre = initial if index == 0 else capture_world("421aa0-entry")
                    if journal_clock_profile:
                        if any(entry["kind"] == "primary" for entry in combined_journal): physical_in_frame = 0
                        physical_in_frame += 1
                        if physical_in_frame > 2 or physical_in_frame == 2 and not pre["extra_update"]:
                            raise ValueError("Recorded update lacks an actual primary or extra frame permit")
                        pending_clock_extra = physical_in_frame == 1 and pre["extra_update"]
                    elif index:
                        pending_clock_extra = False if pending_clock_extra else pre["extra_update"]
                    desired, decision = policy.choose(index, pre)
                    if warm_restart is not None and (pre["session_state"] != 6 or warm_restart.requested):
                        desired, decision = set(), {"phase": "warm-restart-coast"}
                    for key in sorted(held - desired):
                        keyboard(environment, "keyup", key)
                    for key in sorted(desired - held):
                        keyboard(environment, "keydown", key)
                    held = desired
                    stack_response = dbg.command("print $esp")
                    return_address = dbg.word(register_address(stack_response))
                    if not 0x400000 <= return_address < 0x4b0000:
                        raise RuntimeError("Source update return address outside executable code")
                    additional = {"update_caller_return_address": return_address, "release_events": release_events} if args.joined_ai else {}
                    if args.wheel_queue:
                        additional["wheel_queue_callbacks"] = wheel_callbacks
                    if args.whine_rng:
                        additional.update(whine_callbacks=whine_callbacks, whine_update_pre=FLEET.capture_whine_state(dbg.paused_memory()))
                    if args.combined_rng:
                        additional.update(combined_journal=combined_journal, combined_audio_pre=FLEET.capture_combined_audio(dbg.paused_memory()))
                    set_consumer_phase(dbg, "pre_to_ai")
                    if args.ai_state:
                        additional.update(capture_ai_boundaries(dbg, capture_world, args.local_handoffs, pre["actor"], args.traffic_controls, args.nearest_recovery))
                    if args.combined_rng and index == 0:
                        header["combined_initial_rng"] = additional["ai_pre"]["rng"]
                        write_record(header)
                    set_consumer_phase(dbg, "ai_to_consumed")
                    consumed_break = dbg.breakpoint(0x415090)
                    dbg.stop_at(0x415090)
                    consumed = capture_world("415090-entry")
                    dbg.command(f"delete break {consumed_break}")
                    set_consumer_phase(dbg, "consumed_to_contacts")
                    if args.scope == "fleet":
                        for name, address in (("contacts", 0x421980), ("ground", 0x446A80)):
                            if args.combined_rng and name == "ground":
                                additional["surface_calls"], additional["ground"] = capture_ground_surfaces(dbg, capture_world)
                                continue
                            point = dbg.breakpoint(address)
                            dbg.stop_at(address)
                            additional[name] = capture_world(FLEET.BOUNDARIES[name])
                            dbg.command(f"delete break {point}")
                            set_consumer_phase(dbg, "contacts_to_ground")
                        if args.skid_dispatch:
                            set_consumer_phase(dbg, "ground_to_skid")
                            additional["skid_dispatch"] = capture_skid_dispatch(dbg, args.combined_rng)
                            FLEET.validate_skid_dispatch(additional)
                    set_consumer_phase(dbg, "skid_to_post")
                    post_break = None if args.scene_frames else dbg.breakpoint(return_address)
                    dbg.stop_at(return_address)
                    post = capture_world("421aa0-return")
                    if args.whine_rng:
                        additional["whine_update_post"] = FLEET.capture_whine_state(dbg.paused_memory())
                    if args.combined_rng: additional["combined_audio_post"] = FLEET.capture_combined_audio(dbg.paused_memory())
                    if post_break is not None: dbg.command(f"delete break {post_break}")
                    if contact_audio:
                        additional["contact_audio_calls"] = list(contact_audio.calls)
                        contact_audio.calls.clear()
                    if brake_publication:
                        additional["publication_calls"] = list(brake_publication.calls)
                        brake_publication.calls.clear()
                    if len({pre["actor"], consumed["actor"], post["actor"]}) != 1:
                        raise RuntimeError("Local source actor changed within an update")
                    if args.scope == "fleet":
                        FLEET.validate_update_counters({"pre": pre, "consumed": consumed, "post": post, **additional}, args.finish_timing)
                        if args.nearest_recovery:
                            FLEET.validate_nearest_recoveries(header, additional)
                        if args.joined_ai:
                            derived_release_counter = FLEET.validate_joined_release({"pre": pre, "consumed": consumed, "post": post, **additional}, derived_release_counter, selected_initial=args.contact_weather_rng and index == 0, local_session=local_session_profile)
                    elif consumed["update_counter"] != pre["update_counter"] + 1 or post["update_counter"] != consumed["update_counter"]:
                        raise RuntimeError("Missing or repeated original update boundary")
                    for snapshot in (pre, consumed, post):
                        for field in ("car_id", "weather", "track_id", "steering_profile", "transmission_mode", "source_mode", "allocated_actor_count"):
                            if args.scope == "fleet" and field == "transmission_mode":
                                continue
                            if snapshot[field] != initial[field]:
                                raise RuntimeError(f"Original recording configuration changed: {field}")
                    if args.scope == "fleet":
                        coverage.consume(consumed, post, additional["ground"])
                    else:
                        coverage.consume(consumed, post)
                    record = {"type": "update", "id": index, "requested_keys": sorted(desired),
                              "decision": decision, "pre": pre, "consumed": consumed, "post": post, **additional}
                    if args.scenario == "camera-cycle":
                        transaction = scene_journal.frames.camera_transaction(index)
                        record.update(camera_input=transaction["input"], camera_control_expected=transaction["expected"])
                    if detached_records is not None:
                        detached_records.append(FLEET.detached_lifecycle_record(record))
                    if timing_records is not None:
                        timing_records.append(FLEET.retained_race_timing_record(record))
                    if local_records is not None:
                        local_records.append(FLEET.retained_local_session_record(record))
                    scene_closed = False
                    if args.scene_autonomous and index + 1 >= args.updates and not pending_clock_extra:
                        if not scene_journal.frames.ready_to_close():
                            raise ValueError("Final autonomous scene frame is missing physical calls")
                        scene_journal.frames.draining = True
                        set_consumer_phase(dbg, "frame")
                        tail = advance_combined_update(dbg, local_session_profile, qpc_samples=True,
                            scene_frames=scene_journal.frames, stop_return=scene_journal.frames.primary_return)
                        if contact_audio:
                            tail.extend(contact_audio.calls)
                            contact_audio.calls.clear()
                        if brake_publication:
                            tail.extend(brake_publication.calls)
                            brake_publication.calls.clear()
                        tail.sort(key=lambda entry: entry["event"]["order"])
                        record["scene_final_journal"] = tail
                        for kind, name in (("wheel", "wheel_queue_callbacks"), ("motor", "whine_callbacks"), ("release", "release_events")):
                            record[name].extend(entry["event"] for entry in tail if entry["kind"] == kind)
                        scene_closed = True
                    write_record(record)
                    captured_updates = index + 1
                    written_position = trace.tell()
                    if not pending_clock_extra:
                        closed_prefix = {"updates": captured_updates, "source_bytes": written_position, "prefix_crc32": f"{trace_crc32:08x}"}
                    if args.stop_after_local_checkpoint:
                        checkpoint_endpoint = local_checkpoint_endpoint(record, header["local_actor"], checkpoint_endpoint)
                    stop_requested = checkpoint_endpoint is not None and len(checkpoint_endpoint["followup_updates"]) == 3
                    if index % 25 == 0 or index + 1 == args.updates:
                        print(f"Recorded {index + 1}/{args.updates} actual original updates", flush=True)
                    lifecycle_endpoint = (warm_restart.grid and pre["session_state"] == 6 if warm_restart is not None else pre["session_state"] == 13)
                    if verifier is not None:
                        receipt = verifier.poll()
                        if receipt is not None:
                            verified_difference = receipt["status"] == "different"
                            if receipt["status"] == "error": verification_error = receipt["reason"]
                    if not pending_clock_extra and (verified_difference or verification_error):
                        break
                    if scene_closed: break
                    if scene_journal is not None and not args.scene_autonomous and not pending_clock_extra:
                        if args.scene_frames and scene_journal.frames.ready_to_close() and (scene_journal.frames.ready_to_stop() or index + 1 == args.updates):
                            scene_journal.frames.draining = True
                            dbg.stop_at(scene_journal.frames.primary_return)
                            break
                        if not args.scene_frames and scene_journal.complete():
                            break
                    if verifier is not None and not pending_clock_extra and (captured_updates == args.updates or lifecycle_endpoint or stop_requested):
                        quality = capture_quality(coverage.summary(args.scenario), header, detached_records, timing_records, local_records)
                        bounded_stop = stop_requested
                        if quality["passed"] or stop_requested or captured_updates == args.updates:
                            break
                    if verifier is not None and not pending_clock_extra and captured_updates >= next_verification and verifier.process is None:
                        quality = capture_quality(coverage.summary(args.scenario), header, detached_records, timing_records, local_records)
                        try:
                            verifier.start(directory / "trace.jsonl", captured_updates, written_position, trace_crc32, quality)
                        except (OSError, RuntimeError) as error:
                            verification_error = str(error)
                            break
                        while next_verification <= captured_updates: next_verification *= 2
                    if index + 1 != args.updates or args.scene_autonomous:
                        if args.combined_rng:
                            set_consumer_phase(dbg, "frame")
                            combined_journal = advance_combined_update(dbg, local_session_profile, warm_restart, **({"qpc_samples": True} if journal_clock_profile else {}),
                                **({"scene_frames": scene_journal.frames} if args.scene_autonomous else {}))
                            if contact_audio:
                                combined_journal += contact_audio.calls
                                combined_journal.sort(key=lambda item: item["event"]["order"])
                                contact_audio.calls.clear()
                            if brake_publication:
                                combined_journal += brake_publication.calls
                                combined_journal.sort(key=lambda item: item["event"]["order"])
                                brake_publication.calls.clear()
                            release_events = [item["event"] for item in combined_journal if item["kind"] == "release"]
                            wheel_callbacks = [item["event"] for item in combined_journal if item["kind"] == "wheel"]
                            whine_callbacks = [item["event"] for item in combined_journal if item["kind"] == "motor"]
                        elif args.whine_rng:
                            whine_callbacks = advance_whine_update(dbg)
                        elif args.wheel_queue:
                            wheel_callbacks = advance_wheel_queue_update(dbg)
                        elif args.joined_ai:
                            release_events = advance_joined_update(dbg)
                        else:
                            dbg.stop_at(0x421AA0)
            except SourceException as error:
                source_error = str(error)
            if source_error is None:
                for key in sorted(held):
                    keyboard(environment, "keyup", key)
                if args.scenario == "camera-cycle":
                    for key in sorted(camera_held): keyboard(environment, "keyup", key)
                    camera_held.clear()
                if brake_publication: brake_publication.close()
                if contact_audio: contact_audio.close()
                if scene_journal is not None: scene_journal.close()
                if pre_break is not None: dbg.command(f"delete break {pre_break}")
                if release_break is not None:
                    dbg.command(f"delete break {release_break}")
                dbg.command("detach")
                dbg.send("quit")
                process.wait(timeout=5)
            if f"{zlib.crc32(executable.read_bytes()):08x}" != checksum:
                raise RuntimeError("Canonical executable changed during trace acquisition")
            if source_error is None:
                quality = capture_quality(coverage.summary(args.scenario), header, detached_records, timing_records, local_records)
            else:
                quality = {"passed": False, "aborted": True, "activity_diagnostics": coverage.summary(args.scenario)}
            trace.flush()
            capture_finalized = True
    finally:
        # Every source process and display is gone before any final replay wait.
        acquisition_error = sys.exc_info()[1]
        for sig in previous_handlers:
            signal.signal(sig, signal.SIG_IGN)
        try:
            try:
                if warm_restart is not None and source_error is None:
                    warm_restart.release()
                if dbg is not None:
                    dbg.close_memory()
                    dbg.log.close()
                    dbg.commands.close()
            finally:
                try:
                    runtime.cleanup()
                finally:
                    if not capture_finalized and verifier is not None:
                        verifier.cancel()
        finally:
            for sig, handler in previous_handlers.items():
                signal.signal(sig, handler)
            if scene_journal is not None:
                if args.scene_autonomous and "trace_crc32" in locals():
                    scene_journal.frames.writer.binding = {"prefix_crc32": f"{trace_crc32:08x}", "updates": captured_updates}
                scene_journal.finish(source_error or acquisition_error)
    # Source cleanup above is unconditional. No synchronous verification below
    # can alter the next original QPC interval.
    if verifier is not None:
        receipt = verifier.poll()
        if source_error is not None:
            receipt = verifier.finish() or receipt
        elif verifier.process is not None:
            verifier.cancel() #The one final comparison supersedes an older running prefix.
        if receipt is not None:
            verified_difference |= receipt["status"] == "different"
            if receipt["status"] == "error": verification_error = receipt["reason"]
    if source_error is not None:
        finalize_aborted_prefix(directory / "trace.jsonl", captured_updates, written_position, trace_crc32, quality, source_error, closed_prefix)
    else:
        if verifier is not None and not verified_difference and verification_error is None:
            exact, verification_error = verify_final_capture(directory / "trace.jsonl", directory, args.verify_replay, captured_updates, trace_crc32, quality)
            verified_difference = not exact and verification_error is None
            if exact:
                try:
                    report = json.loads((directory / "verify/fleet-comparison-startup-world-reset.json").read_text())
                    verifier.record_exact({"updates": captured_updates, "source_bytes": written_position,
                        "prefix_crc32": f"{trace_crc32:08x}", "trace_crc32": report["trace_crc32"], "status": "exact"})
                except (OSError, ValueError, KeyError) as error:
                    verification_error = str(error)
        else:
            with (directory / "trace.jsonl").open("a") as trace:
                trace.write(json.dumps({"type": "complete", "updates": captured_updates, "prefix_crc32": f"{trace_crc32:08x}", "coverage": quality}) + "\n")
    completion = {"updates": captured_updates, "exe_crc32": checksum, "coverage": quality,
                  "last_closed_clock_prefix": closed_prefix}
    if scene_journal is not None:
        completion["scene_journal_coverage"] = SCENE.coverage(scene_journal.writer.events)
        if args.scene_frames: completion["scene_frames_coverage"] = SCENE.FRAMES.coverage(scene_journal.frames.writer.frames)
    if verifier is not None:
        completion.update(verified_updates=verifier.verified_updates, verification_receipt=verifier.receipt)
    if source_error is not None:
        completion.update(capture_status="aborted", source_exception=source_error)
    if verification_error is not None:
        completion.update(verification_error=verification_error)
    if bounded_stop:
        completion.update(stop_reason="observed local checkpoint and three display-consumer wrappers", local_checkpoint=checkpoint_endpoint)
    (directory / "complete.json").write_text(json.dumps(completion) + "\n")
    failed = source_error is not None or verification_error is not None or verified_difference
    status = "ABORTED" if source_error is not None else "VERIFY ERROR" if verification_error is not None else "DIFF" if verified_difference else "BOUNDED CAPTURE COMPLETE; lifecycle pending" if bounded_stop else "PASS" if quality["passed"] else "CAPTURE COMPLETE; lifecycle coverage pending"
    if scene_journal is not None and not failed:
        scene_passed = completion["scene_journal_coverage"]["passed"] and (not args.scene_frames or completion["scene_frames_coverage"]["passed"])
        status = "SCENE CAPTURE COMPLETE" + ("; coverage pending" if not scene_passed else "")
    print(f"{status}: {captured_updates} complete original updates: {directory / 'trace.jsonl'}", flush=True)
    if scene_journal is not None:
        print("Scene journal: " + json.dumps(completion["scene_journal_coverage"], sort_keys=True), flush=True)
        if args.scene_frames: print("Scene frames: " + json.dumps(completion["scene_frames_coverage"], sort_keys=True), flush=True)
    if source_error is not None: print(source_error, flush=True)
    coverage_summary = {name: quality[name] for name in ("profile", "gates", "passed") if name in quality}
    print(f"Coverage: {json.dumps(coverage_summary)}", flush=True)
    if failed:
        return 1
    if scene_journal is not None:
        return 0 if completion["scene_journal_coverage"]["passed"] and (not args.scene_frames or completion["scene_frames_coverage"]["passed"]) else 2
    if bounded_stop:
        return 0
    if args.replay:
        replay = subprocess.run([sys.executable, str(PROJECT / "tools/replay-reference.py"),
                                 str(directory / "trace.jsonl"), "--replay", str(args.replay.resolve()), "--mode", args.replay_mode],
                                check=False, timeout=120)
        if replay.returncode == 1:
            print(f"Original acquisition is complete; native comparison differs. See comparison-{args.replay_mode}.json.", flush=True)
            return 1
        if replay.returncode != 0:
            raise RuntimeError(f"Original acquisition is complete, but replay validation failed with exit {replay.returncode}")
    if not quality["passed"]:
        print("Original acquisition is complete, but the declared scenario coverage is insufficient.", flush=True)
        return 2
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Reference trace failed: {error}", file=sys.stderr)
        sys.exit(1)
