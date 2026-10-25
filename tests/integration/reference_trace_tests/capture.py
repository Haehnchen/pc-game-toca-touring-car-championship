"""Source trace checks for capture."""

import struct
from unittest.mock import Mock
from unittest.mock import call
from unittest.mock import patch

from .support import TRACE, TraceTestCase


class SourceCaptureTests(TraceTestCase):
    def test_start_call_snapshot_reads_aligned_ranges_before_grid_actors_exist(self):
        debugger, reader = Mock(), Mock()
        debugger.paused_memory.return_value = reader
        memory = {0x4b0660: 0x600000, 0x4d37b0: 0, TRACE.GLOBALS["rng"]: 7,
                  0x4d3840: 1234, 0x4d3844: 0, 0x600004: 0, 0x6000ac: 1}
        reader.word.side_effect = memory.__getitem__
        reader.memory.return_value = struct.pack("<H", 2)
        result = TRACE.start_call_snapshot(debugger, 0x460170, "post")
        self.assertEqual((result["boundary"], result["release_counter"], result["session_stage"]), ("460170-return", 0, 2))
        self.assertTrue(all(address % 4 == 0 and size % 4 == 0 for invocation in reader.preload.call_args_list for address, size in invocation.args[0]))
        self.assertFalse(any(TRACE.LAYOUT["body_base"] <= address < TRACE.LAYOUT["body_base"] + 16 * 0x53c for address in memory))

    def test_joined_advance_records_actual_release_before_next_update_only(self):
        debugger = Mock()
        debugger.command.side_effect = ["Stopped at00448130", "Stopped at00421aa0"]
        event = {"return_address": 0x4220a6}
        with patch.object(TRACE, "capture_start_call", return_value=event) as capture:
            self.assertEqual(TRACE.advance_joined_update(debugger), [event])
        capture.assert_called_once_with(debugger, 0x448130)
        self.assertEqual(debugger.command.call_args_list, [call("cont", 30), call("cont", 30)])
        debugger.command.side_effect = ["Stopped at00448131"]
        with self.assertRaisesRegex(RuntimeError, "Unexpected stop"):
            TRACE.advance_joined_update(debugger)

    def test_joined_release_derives360_then_wrapper_decrement_without_counter_input(self):
        before = {"boundary": "448130-entry", "release_counter": 0, "rng": 7, "race_clock": 1234,
                  "release_epoch": 0, "source_mode": 0, "local_players": 1, "session_stage": 2}
        event = {"return_address": 0x4220a6, "pre": before,
                 "post": {**before, "boundary": "448130-return", "release_counter": 360, "release_epoch": 1234}}
        def record(counter, state, caller, events):
            return {"update_caller_return_address": caller, "release_events": events,
                    **{name: {"session_state": state, "shared": {"ai_state": {"release_counter": counter}}}
                       for name in (*TRACE.FLEET.BOUNDARIES, *TRACE.FLEET.AI_BOUNDARIES, *TRACE.FLEET.TRAFFIC_BOUNDARIES)}}
        self.assertEqual(TRACE.FLEET.validate_joined_release(record(360, 9, 0x4488a1, [event]), 0), 360)
        self.assertEqual(TRACE.FLEET.validate_joined_release(record(359, 6, 0x448434, []), 360), 359)
        with self.assertRaisesRegex(ValueError, "source-derived sequence"):
            TRACE.FLEET.validate_joined_release(record(360, 6, 0x448434, []), 360)
        with self.assertRaisesRegex(ValueError, "caller/session"):
            TRACE.FLEET.validate_joined_release(record(359, 6, 0x4488a1, []), 360)
        event["return_address"] = 0x421854
        with self.assertRaisesRegex(ValueError, "release caller"):
            TRACE.FLEET.validate_joined_release(record(360, 9, 0x4488a1, [event]), 0)

    def test_nearest_capture_brackets_exact_block_after_gate_progression(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = range(1, 10)
        debugger.word.return_value = 0x421c48
        responses = iter(("Stopped at004046a3", "Stopped at00421c48"))
        debugger.command.side_effect = lambda command, *args: "esp = 0x100000" if command == "print $esp" else next(responses) if command == "cont" else ""
        with patch.object(TRACE, "nearest_recovery_snapshot", side_effect=[{"actor_index": 14, "phase": "pre"}, {"actor_index": 14, "phase": "post"}]):
            result = TRACE.capture_ai_boundaries(debugger, lambda boundary: {"boundary": boundary}, nearest_recovery=True)
        self.assertEqual(result["nearest_recoveries"], [{"actor": 14, "pre": {"actor_index": 14, "phase": "pre"}, "post": {"actor_index": 14, "phase": "post"}}])
        self.assertEqual(debugger.stop_at.call_args_list, [call(0x403dd0), call(0x404731)])
        self.assertIn(call(0x4046a3), debugger.breakpoint.call_args_list)
        self.assertIn(call("delete break 3"), debugger.command.call_args_list)

    def test_nearest_snapshot_validates_actual_source_frame_and_registers(self):
        debugger, reader = Mock(), Mock()
        actor, frame = 14, 0x100000
        body = TRACE.LAYOUT["body_base"] + actor * 0x53c
        game = TRACE.LAYOUT["gameplay_base"] + actor * 0x124
        debugger.command.side_effect = lambda command: f"ebp = 0x{frame:x}" if command == "print $ebp" else f"edi = 0x{game + 0xa2:x}"
        debugger.paused_memory.return_value = reader
        reader.word.side_effect = lambda address: {frame + 8: actor, frame - 4: body + 0xbc, TRACE.GLOBALS["rng"]: 7}[address]
        reader.memory.side_effect = lambda address, size: bytes(size)
        result = TRACE.nearest_recovery_snapshot(debugger, "pre")
        self.assertEqual((result["actor_index"], result["cursor_pointer"], result["position_pointer"]), (actor, game + 0xa2, body + 0xbc))
        self.assertEqual(result["boundary"], "4046a3-entry")
        debugger.command.side_effect = lambda command: f"ebp = 0x{frame:x}" if command == "print $ebp" else f"edi = 0x{game + 0xa4:x}"
        with self.assertRaisesRegex(ValueError, "frame/register"):
            TRACE.nearest_recovery_snapshot(debugger, "post")

    def test_traffic_boundary_capture_stops_before_stopping_distances_and_requires_it(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = range(1, 10)
        debugger.word.return_value = 0x421c48
        responses = iter(("Stopped at00404c1a", "Stopped at00421c48"))
        debugger.command.side_effect = lambda command, *args: "esp = 0x100000" if command == "print $esp" else next(responses) if command == "cont" else ""
        capture = Mock(side_effect=lambda boundary: {"boundary": boundary})
        result = TRACE.capture_ai_boundaries(debugger, capture, traffic_controls=True)
        self.assertEqual([item.args[0] for item in capture.call_args_list], ["403dd0-entry", "404c1a-entry", "403dd0-return"])
        self.assertEqual(result["traffic_pre"], {"boundary": "404c1a-entry"})
        self.assertIn(call("delete break 3"), debugger.command.call_args_list)
        debugger.breakpoint.side_effect = range(1, 10)
        debugger.command.side_effect = lambda command, *args: "esp = 0x100000" if command == "print $esp" else "Stopped at00421c48"
        with self.assertRaisesRegex(RuntimeError, "without its required traffic boundary"):
            TRACE.capture_ai_boundaries(debugger, capture, traffic_controls=True)

    def test_traffic_bypass_capture_reads_live_unaligned_bytes_and_crc_without_padding(self):
        debugger = Mock()
        pointer, count = 0x700002, 254
        def memory(address, size):
            if address == 0x54b948: return struct.pack("<I", pointer)
            if address == pointer: return bytes(range(count))
            return bytes(size)
        debugger.memory.side_effect = memory
        state, initial = TRACE.FLEET.capture_traffic_control(debugger, 0x600000, count)
        debugger.preload.assert_called_once_with([(0x700000, 256)])
        self.assertEqual(TRACE.FLEET.traffic_content(initial), bytes(range(count)))
        self.assertEqual(state["brake_bypass_crc32"], initial["brake_bypass_crc32"])
        self.assertNotIn("brake_bypass_hex", state)
        debugger.memory.side_effect = lambda address, size: struct.pack("<I", 0xfffffffe) if address == 0x54b948 else bytes(size)
        debugger.preload.reset_mock()
        with self.assertRaisesRegex(ValueError, "pointer/range"):
            TRACE.FLEET.capture_traffic_control(debugger, 0x600000, count)
        debugger.preload.assert_not_called()

    def test_session_boundary_capture_reads_live_configuration_tail_and_fixed_globals(self):
        debugger = Mock()
        debugger.memory.side_effect = lambda address, size: bytes([address & 255]) * size
        result = TRACE.FLEET.capture_session_boundary(debugger, 0x600000)
        self.assertEqual(result["configuration_tail_hex"], bytes([0xfc] * 0x30).hex())
        self.assertEqual(debugger.memory.call_args_list, [*(call(address, size) for address, size in TRACE.FLEET.SESSION_BOUNDARY_BLOCKS.values()), call(0x6005fc, 0x30)])
        debugger.command.assert_not_called()

    def test_local_handoff_capture_brackets_only_actual_local_callee(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = range(1, 10)
        debugger.word.return_value = 0x421abc
        debugger.command.side_effect = lambda command, *args: "esp = 0x100000" if command == "print $esp" else "Stopped at00402c80" if command == "cont" and not getattr(debugger, "done", False) else "Stopped at00421abc"
        debugger.memory.return_value = struct.pack("<2I", 0x421827, 14)
        def snapshot(_debugger, actor, kind, phase):
            self.assertEqual((actor, kind), (14, 1))
            if phase == "return": debugger.done = True
            return {"phase": phase}
        debugger.done = False
        with patch.object(TRACE, "handoff_snapshot", side_effect=snapshot):
            result = TRACE.capture_ai_boundaries(debugger, lambda boundary: {"boundary": boundary}, True, 14)
        self.assertEqual(result["local_handoffs"][0]["post"], {"phase": "return"})
        self.assertEqual(result["local_handoffs"][0]["stack_hex"], struct.pack("<2I", 0x421827, 14).hex())
        self.assertIn(call(0x421827), debugger.stop_at.call_args_list)
        debugger.memory.return_value = struct.pack("<2I", 0x421828, 14)
        debugger.done = False
        debugger.breakpoint.side_effect = range(1, 10)
        with self.assertRaisesRegex(RuntimeError, "caller"):
            TRACE.capture_ai_boundaries(debugger, Mock(), True, 14)

    def test_ai_boundaries_capture_actual_call_return_before_later_control_rng(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = [11, 12]
        debugger.command.side_effect = ["", "$esp = 0x01020304", ""]
        debugger.word.return_value = 0x421b12
        capture = Mock(side_effect=[{"rng": 123}, {"rng": 456}])
        result = TRACE.capture_ai_boundaries(debugger, capture)
        self.assertEqual(result, {"ai_pre": {"rng": 123}, "ai_post": {"rng": 456}})
        self.assertEqual(debugger.stop_at.call_args_list, [call(0x403dd0), call(0x421b12)])
        debugger.word.assert_called_once_with(0x01020304)
        self.assertEqual(capture.call_args_list, [call("403dd0-entry"), call("403dd0-return")])
        self.assertEqual(debugger.command.call_args_list,
                         [call("delete break 11"), call("print $esp"), call("delete break 12")])
        debugger.word.return_value = 0x1234
        debugger.breakpoint.side_effect = [13]
        debugger.command.side_effect = ["", "$esp = 0x01020304"]
        with self.assertRaisesRegex(RuntimeError, "return address"):
            TRACE.capture_ai_boundaries(debugger, Mock())
