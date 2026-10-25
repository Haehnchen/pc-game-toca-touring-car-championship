"""Strict parsing of real WineDbg memory rows used for original trace capture."""

import importlib.util
from pathlib import Path
import unittest
import struct
import io
import time
from types import SimpleNamespace
from unittest.mock import Mock, call, patch

SPEC = importlib.util.spec_from_file_location("trace_reference", Path(__file__).resolve().parents[3] / "tools/trace-reference.py")
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)


def vehicle(position=(0, 0, 0), velocity=(0, 0, 0), speed=0, surface=0):
    body = bytearray(0x53c)
    dynamics = bytearray(0x338)
    struct.pack_into("<3i", body, 0xbc, *position)
    struct.pack_into("<3i", body, 0xc8, *velocity)
    struct.pack_into("<i", body, 0xe0, speed)
    struct.pack_into("<9i", body, 0x150, 65536, 0, 0, 0, 65536, 0, 0, 0, 65536)
    dynamics[0x328:0x32c] = bytes([surface] * 4)
    return {"body_hex": body.hex(), "dynamics_hex": dynamics.hex(), "transmission_mode": 2,
            "input_q16": {"left": 0, "right": 0, "throttle": 0, "brake": 0, "gear_edge": 0}}


class TraceTestCase(unittest.TestCase):
    """Synthetic actor memory and stopped-debugger fixtures; never launches Wine."""

    @staticmethod
    def expiry_snapshot(counter, active=False, resting=False, timer=0):
        snapshot = vehicle(); pool = bytearray(10 * 0x11c)
        struct.pack_into("<I", pool, 0x108, int(active)); pool[0x10c] = 2
        pool[0x10d] = 0xa5  #Unclaimed padding must not widen the source type byte.
        struct.pack_into("<iII", pool, 0x110, timer, 0, int(resting))
        snapshot.update(actor=14, session_state=9 if counter == 0 else 6, noqual=1, update_counter=counter, race_clock=counter * 40,
                        initial_configuration={"track_id": 0, "weather": 1, "car_id": 7, "transmission_mode": 2, "steering_profile": 2},
                        shared={"detached_pool_hex": pool.hex(), "detached_count": int(active),
                                "detached_free_hex": struct.pack("<10I", int(not active), *([1] * 9)).hex(),
                                "sector_visibility_disabled": 0, "sector_visibility_override": 0, "sector_visibility_hex": bytes(256).hex()})
        return snapshot

    def expiry_warmup(self, snapshots, limit=10):
        index, calls = [0], []
        def keys(debugger, environment, updates, held):
            before = {key: snapshots[index[0]][key] for key in ("update_counter", "race_clock")}
            index[0] += 1
            after = {key: snapshots[index[0]][key] for key in before}
            calls.append((updates, held))
            return {"updates": updates, "requested_keys": sorted(held), "before": before, "after": after,
                    "keys_only": True, "recorded_boundaries": False, "continuous_from_grid": False}
        gate = TRACE.DetachmentApproachGate(2211506, 53635, "a9135ddd", 62)
        policy = TRACE.FleetDetachmentPolicy(snapshots[0], gate)
        policy.choose = lambda index, snapshot: ({"Up"} if snapshot["shared"]["detached_count"] == 0 else set(),
                                                 {"phase": "approach" if index == 0 else "coast"})
        debugger = Mock()
        with patch.object(TRACE, "detached_expiry_snapshot", side_effect=lambda debugger: snapshots[index[0]]), \
                patch.object(TRACE, "warmup_keys", side_effect=keys), patch.object(TRACE, "FleetDetachmentPolicy", return_value=policy) as factory:
            result = TRACE.warmup_until_detached_expiry(debugger, {}, limit)
        factory.assert_called_once_with(snapshots[0], gate)
        debugger.command.assert_not_called()
        return result, calls

    def debugger(self, pending=b""):
        debugger = TRACE.Debugger.__new__(TRACE.Debugger)
        debugger.deadline = time.monotonic() + 1
        debugger.pending = pending
        debugger.pause_serial = 0
        debugger.confirmed_pause = False
        debugger.memory_fd = None
        debugger.log = io.BytesIO()
        debugger.commands = io.StringIO()
        debugger.process = SimpleNamespace(stdin=io.BytesIO(), stdout=None)
        debugger.confirm_pause()
        return debugger

    @staticmethod
    def checkpoint_record(index=10, ticks=0, sector=1, actor=14, lap=1, rank=2, write=True):
        game = bytearray(0x124)
        struct.pack_into("<I", game, 8, 1)
        game[0x8f] = rank
        table = bytearray(576)
        if write: struct.pack_into("<Iii", table, (sector * 16 + rank - 1) * 12, 1000, lap, actor)
        def snapshot(split):
            return {"session_state": 6, "race_clock": 1000, "actors": [{"gameplay_hex": game.hex()} for _ in range(16)],
                    "shared": {"checkpoint_timing": {"split_table_hex": split.hex()},
                               "finish_timing": {"display_active_hex": struct.pack("<4i", 1, 0, 0, 0).hex(),
                                                 "display_ticks_hex": struct.pack("<4h", ticks, 0, 0, 0).hex()}}}
        return {"id": index, "ai_pre": snapshot(bytes(576)), "ai_post": snapshot(table)}
