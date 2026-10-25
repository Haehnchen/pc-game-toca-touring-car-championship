"""Source trace checks for warmup."""

import tempfile
import struct
from pathlib import Path
from unittest.mock import Mock
from unittest.mock import call
from unittest.mock import patch

from .support import TRACE, TraceTestCase, vehicle


class WarmupCaptureTests(TraceTestCase):
    def test_detachment_warmup_retains_same_policy_until_five_ticks_before_turn(self):
        snapshot = vehicle()
        snapshot.update(actor=14, session_state=6, shared={"detached_count": 0, "detached_pool_hex": bytes(10 * 0x11c).hex()})
        debugger = Mock()
        debugger.word.return_value = 0
        with patch.object(TRACE, "detachment_approach_snapshot", return_value=snapshot), patch.object(TRACE, "keyboard") as keyboard:
            policy, provenance = TRACE.warmup_detachment_approach(debugger, {}, 100)
        self.assertEqual(policy.released_updates, 45)
        self.assertEqual(provenance["updates"], 45)
        self.assertEqual(provenance["phase_counts"], {"accelerate": 45})
        self.assertEqual(debugger.stop_at.call_count, 45)
        self.assertEqual(keyboard.call_args_list, [call({}, "keydown", "Up"), call({}, "keyup", "Up")])
        for tick in range(5): self.assertEqual(policy.choose(tick, snapshot)[0], {"Up"})
        self.assertEqual(policy.choose(5, snapshot)[0], {"Up", "Right"})
        self.assertFalse(provenance["continuous_from_grid"])

    def test_detached_expiry_warmup_uses_real_key_steps_then_bounded_coast(self):
        snapshots = [self.expiry_snapshot(0), self.expiry_snapshot(1, True), self.expiry_snapshot(2, True, True, 6), self.expiry_snapshot(6, True, True, 2)]
        (_, metadata), calls = self.expiry_warmup(snapshots)
        self.assertEqual(calls, [(1, {"Up"}), (1, set()), (4, set())])
        self.assertEqual((metadata["updates"], metadata["slot"], metadata["type"], metadata["first_rest_timer"]), (6, 0, 2, 6))
        self.assertEqual(metadata["endpoint"], {"active": 1, "free": 0, "resting": 1, "timer": 2, "count": 1, "sector": 0})
        self.assertEqual(metadata["after"], {"update_counter": 6, "race_clock": 240})
        parameters = metadata["source_policy_parameters"]
        self.assertEqual(parameters["version"], 6)
        self.assertNotIn("acceleration_updates_after_release", parameters)
        self.assertEqual(parameters["approach_gate"], {"kind": "horizontal-forward-progress-and-speed", "progress_q16": 2211506,
                         "minimum_speed_q16": 53635, "reference_trace_crc32": "a9135ddd", "reference_update": 62,
                         "initial_position_q16": (0, 0, 0), "initial_forward_q16": (65536, 0, 0)})
        self.assertFalse(metadata["continuous_from_grid"] or metadata["recorded_boundaries"])

    def test_detached_expiry_warmup_fails_if_selected_part_disappears_or_timer_skips(self):
        for failure in ("retired", "timer", "visibility", "limit"):
            snapshots = [self.expiry_snapshot(0), self.expiry_snapshot(1, True), self.expiry_snapshot(2, True, True, 6), self.expiry_snapshot(6, True, True, 2)]
            if failure == "retired": snapshots[2] = self.expiry_snapshot(2)
            elif failure == "timer": snapshots[3] = self.expiry_snapshot(6, True, True, 1)
            elif failure == "visibility": snapshots[2]["shared"]["sector_visibility_disabled"] = 1
            with self.subTest(failure=failure), self.assertRaises((ValueError, RuntimeError)):
                self.expiry_warmup(snapshots, 2 if failure == "limit" else 10)

    def test_focused_capture_quality_uses_shared_expiry_gate_without_weakening_old_profile(self):
        broad = {"passed": False, "detached_allocations": 0, "detached_coverage": False}
        self.assertIs(TRACE.capture_quality(broad, {}), broad)
        header, sparse = {"scope": "fixture"}, [{"id": 0}]
        for required, observed, sufficient, passed in ((True, True, True, True), (True, False, False, False), (False, None, True, False)):
            focused = {"chosen_expiry_required": required, "chosen_expiry_observed": observed, "coverage_sufficient": sufficient,
                       "lifecycle_coverage": {"counts": {"allocations": 0, "landings": 0, "reuses": 0}}}
            with patch.object(TRACE.FLEET, "detached_expiry_coverage", return_value=focused) as common:
                quality = TRACE.capture_quality(broad, header, sparse)
            with self.subTest(required=required, observed=observed):
                common.assert_called_once_with(header, sparse)
                self.assertEqual(quality["profile"], "detached-expiry")
                self.assertEqual(quality["gates"], {"chosen_visible_expiry": passed})
                self.assertEqual(quality["passed"], passed)
                self.assertEqual(quality["activity_diagnostics"], broad)
                self.assertEqual({key: quality[key] for key in focused}, focused)
        self.assertFalse(broad["passed"])

    def test_thunder_warmup_stops_only_at_positive_actual_pending_update_entry(self):
        debugger = Mock()
        step = [0]
        def word(address):
            if address == 0x52bc5c: return int(step[0] >= 1)
            if address == 0x52b69c: return 0xf5b if step[0] == 1 else 65536
            if address == TRACE.GLOBALS["update_counter"]: return step[0]
            return 0
        debugger.word.side_effect = word
        def advance(*args): step[0] += 1
        with patch.object(TRACE, "warmup_keys", side_effect=advance) as warmup:
            actual = TRACE.warmup_until_thunder_ready(debugger, {}, 3)
        self.assertEqual(actual["updates"], 2)
        self.assertEqual(actual["weather_state"]["pending"], 1)
        self.assertEqual(warmup.call_count, 2)
        self.assertFalse(actual["continuous_from_grid"])

    def test_nearest_warmup_requires_strict_distance_and_observes_current_gate_cursor(self):
        points = bytearray(4 + 64 * 64)
        struct.pack_into("<I", points, 0, 64)
        for index in (40, 41): struct.pack_into("<3i", points, 4 + index * 64 + 12, 20 * 65536, 0, 0)
        nearest_map = bytearray(256)
        nearest_map[0], nearest_map[1] = 40, 41
        def snapshot(x, cursor=0, counter=0):
            result = vehicle(position=(x * 65536, 0, 0))
            game = bytearray(0x124)
            struct.pack_into("<2I", game, 4, 1, 1)
            struct.pack_into("<2h", game, 0xa2, cursor, 0)
            result.update(actor=14, gameplay_hex=game.hex(), update_counter=counter, session_state=6)
            return result
        first, last = snapshot(0), snapshot(11, 1, 1)
        policy = TRACE.NearestRecoveryPolicy(bytes(points), nearest_map, first, 0)
        self.assertFalse(policy.pending(snapshot(10))[0])
        self.assertTrue(policy.pending(snapshot(11))[0])
        debugger = Mock()
        with tempfile.TemporaryDirectory() as directory, patch.object(TRACE, "local_driving_snapshot", side_effect=[first, last]), patch.object(TRACE, "keyboard") as keyboard:
            result = TRACE.warmup_nearest_ready(debugger, {}, policy, Path(directory) / "drive.jsonl", limit=2)
            log = (Path(directory) / "drive.jsonl").read_bytes()
        self.assertEqual(result["updates"], 1)
        self.assertEqual((result["endpoint"]["route_next"], result["endpoint"]["candidate"]), (1, 41))
        self.assertEqual(result["decision_log_crc32"], f"{TRACE.zlib.crc32(log):08x}")
        self.assertFalse(result["recorded_boundaries"])
        debugger.stop_at.assert_called_once_with(0x421aa0)
        self.assertTrue(any(item.args[1:] == ("keydown", "Up") for item in keyboard.call_args_list))
        self.assertTrue(any(item.args[1:] == ("keyup", "Up") for item in keyboard.call_args_list))

    def test_local_driving_warmup_uses_keys_and_stops_before_pending_finish(self):
        def snapshot(point):
            game = bytearray(0x124)
            struct.pack_into("<I", game, 8, 1)
            struct.pack_into("<h", game, 0xa8, 1)
            struct.pack_into("<h", game, 0xa2, point)
            return {"actor": 14, "gameplay_hex": game.hex(), "update_counter": 10 if point else 11,
                    "race_clock": 1000, "session_state": 6, "session_stage": 2, "lap_limit": 1}
        debugger, policy = Mock(), Mock()
        policy.parameters = {"keys_only": True}
        policy.choose.return_value = ({"Up", "Left"}, {"action": "drive"})
        with tempfile.TemporaryDirectory() as directory, patch.object(TRACE, "local_driving_snapshot", side_effect=[snapshot(253), snapshot(0)]), patch.object(TRACE, "keyboard") as keyboard:
            result = TRACE.warmup_local_finish(debugger, {}, policy, Path(directory) / "drive.jsonl", limit=2)
        self.assertEqual(result["updates"], 1)
        self.assertEqual(result["target_lap"], 1)
        self.assertFalse(result["recorded_boundaries"])
        self.assertEqual(keyboard.call_args_list, [call({}, "keydown", "Left"), call({}, "keydown", "Up"), call({}, "keyup", "Left"), call({}, "keyup", "Up")])
        debugger.stop_at.assert_called_once_with(0x421aa0)

    def test_rollout_warmup_stops_before_handback_and_rejects_missed_event(self):
        raw = bytearray(0x124)
        struct.pack_into("<h", raw, 0xa8, 1)
        raw[0x9d] = 2
        struct.pack_into("<h", raw, 0xa2, 5)
        debugger = Mock()
        debugger.word.side_effect = lambda address: {0x4d37a0: 14, 0x4b0660: 0x600000, 0x6005f8: 4, 0x6005f4: 1 << 16}[address]
        ready = bytearray(raw)
        struct.pack_into("<h", ready, 0xa2, 0)
        debugger.memory.side_effect = [bytes(raw), bytes(ready)]
        with patch.object(TRACE, "warmup_keys") as advance:
            result = TRACE.warmup_until_local_rollout(debugger, {}, limit=2)
        advance.assert_called_once_with(debugger, {}, 1, [])
        self.assertEqual(result["updates"], 1)
        self.assertFalse(result["recorded_boundaries"])
        struct.pack_into("<I", ready, 8, 1)
        debugger.memory.side_effect = [bytes(ready)]
        with patch.object(TRACE, "warmup_keys") as advance, self.assertRaisesRegex(RuntimeError, "already occurred"):
            TRACE.warmup_until_local_rollout(debugger, {})
        advance.assert_not_called()

    def test_observed_ai_point_warmup_validates_live_target_and_uses_bounded_real_coast_updates(self):
        descriptor = struct.pack("<4I", 0x500002, 100, 1, 0x501906)
        def rivals(point, backwards=0):
            raw = bytearray(16 * 0x124)
            base = 2 * 0x124
            struct.pack_into("<I", raw, base + 4, 1)
            struct.pack_into("<I", raw, base + 0xc, backwards)
            raw[base + 0x80] = 0xff
            struct.pack_into("<2h", raw, base + 0xa2, point, (point - 1) % 100)
            return bytes(raw)
        debugger = Mock()
        debugger.memory.side_effect = [descriptor, rivals(0), rivals(78), rivals(86)]
        debugger.word.side_effect = [10, 1000, 36, 2040]
        with patch.object(TRACE, "warmup_keys") as advance:
            result = TRACE.warmup_until_ai_point(debugger, {}, 86)
        self.assertEqual(advance.call_args_list, [call(debugger, {}, 25, []), call(debugger, {}, 1, [])])
        self.assertEqual(result["updates"], 26)
        self.assertEqual(result["selected_rivals"], [2])
        self.assertEqual(result["endpoint_boundary"], "421aa0-entry")
        self.assertFalse(result["recorded_boundaries"])
        self.assertFalse(result["continuous_from_grid"])
        debugger.memory.side_effect = [descriptor]
        with patch.object(TRACE, "warmup_keys") as advance, self.assertRaisesRegex(ValueError, "POINTS index"):
            TRACE.warmup_until_ai_point(debugger, {}, 100)
        advance.assert_not_called()
        #A grid correction at the same index is not a forward rival endpoint.
        debugger.memory.side_effect = [descriptor, rivals(86, backwards=1), rivals(86, backwards=1)]
        debugger.word.side_effect = [0, 0]
        with patch.object(TRACE, "warmup_keys") as advance, self.assertRaisesRegex(RuntimeError, "within 25 actual"):
            TRACE.warmup_until_ai_point(debugger, {}, 86, limit=25)
        advance.assert_called_once_with(debugger, {}, 25, [])

    def test_point_zero_warmup_requires_ordered_nonfinish_lap_endpoint(self):
        descriptor = struct.pack("<4I", 0x500002, 100, 1, 0x501906)
        def rivals(checkpoint, lap=1):
            raw = bytearray(16 * 0x124)
            struct.pack_into("<I", raw, 4, 1)
            raw[0x80] = 0xff
            raw[0xa6] = checkpoint
            struct.pack_into("<h", raw, 0xa8, lap)
            return bytes(raw)
        stage = struct.pack("<4h", 0, 2, 6, 0)
        debugger = Mock()
        debugger.memory.side_effect = [descriptor, rivals(1), stage, rivals(0), stage]
        debugger.word.side_effect = [0, 0, 0x600000, 2, 2, 25, 1000]
        with patch.object(TRACE, "warmup_keys") as advance:
            result = TRACE.warmup_until_ai_point(debugger, {}, 0)
        advance.assert_called_once_with(debugger, {}, 25, [])
        self.assertEqual(result["selected_rivals"], [0])
        self.assertEqual(result["target_filter"], "lap-ready nonfinish checkpoint0")
        self.assertEqual(result["lap_context"], {"session_stage": 2, "session_state": 6, "lap_limit": 2})
        debugger.memory.side_effect = [descriptor, rivals(0, lap=2), stage, rivals(0, lap=2), stage]
        debugger.word.side_effect = [0, 0, 0x600000, 2, 2]
        with patch.object(TRACE, "warmup_keys"), self.assertRaisesRegex(RuntimeError, "within 25 actual"):
            TRACE.warmup_until_ai_point(debugger, {}, 0, limit=25)
        debugger.memory.side_effect = [descriptor, rivals(0), stage]
        debugger.word.side_effect = [0, 0, 0x600000, 1, 0, 0]
        with patch.object(TRACE, "warmup_keys") as advance:
            result = TRACE.warmup_until_ai_point(debugger, {}, 0, finish_ready=True)
        advance.assert_not_called()
        self.assertEqual(result["target_filter"], "finish-ready checkpoint0")

    def test_warmup_stages_allow_neutral_driving_and_reject_non_driving_keys(self):
        self.assertEqual(TRACE.warmup_step("300:Up,Left,Up"), (300, ["Left", "Up"]))
        self.assertEqual(TRACE.warmup_step("1:"), (1, []))
        for value in ("0:Up", "1001:Up", "30:Return", "30:Up,", "30", "no:Up"):
            with self.subTest(value=value), self.assertRaises(TRACE.argparse.ArgumentTypeError):
                TRACE.warmup_step(value)

    def test_keyboard_warmup_records_observed_endpoints_without_grid_continuity(self):
        debugger = Mock()
        debugger.word.side_effect = [10, 1000, 13, 1200]
        debugger.command.return_value = "Stopped at00421aa0"
        with patch.object(TRACE, "keyboard") as keyboard:
            result = TRACE.warmup_keys(debugger, {}, 3, ["Up", "Left", "Up"])
        debugger.command.assert_called_once_with("cont 3", 60)
        debugger.confirm_pause.assert_called_once_with()
        self.assertEqual(keyboard.call_args_list, [call({}, "keydown", "Left"), call({}, "keydown", "Up"),
                                                  call({}, "keyup", "Left"), call({}, "keyup", "Up")])
        self.assertEqual(result["before"], {"update_counter": 10, "race_clock": 1000})
        self.assertEqual(result["after"], {"update_counter": 13, "race_clock": 1200})
        self.assertFalse(result["continuous_from_grid"])
        self.assertFalse(result["recorded_boundaries"])

    def test_keyboard_warmup_releases_inputs_when_source_boundary_fails(self):
        debugger = Mock()
        debugger.word.return_value = 0
        debugger.command.side_effect = TimeoutError("source stalled")
        with patch.object(TRACE, "keyboard") as keyboard, self.assertRaises(TimeoutError):
            TRACE.warmup_keys(debugger, {}, 3, ["Up"])
        self.assertEqual(keyboard.call_args_list, [call({}, "keydown", "Up"), call({}, "keyup", "Up")])

    def test_keyboard_warmup_rejects_missing_source_updates(self):
        debugger = Mock()
        debugger.word.side_effect = [10, 1000, 12, 1200]
        debugger.command.return_value = "Stopped at00421aa0"
        with patch.object(TRACE, "keyboard"), self.assertRaisesRegex(RuntimeError, "advanced 2"):
            TRACE.warmup_keys(debugger, {}, 3, [])

    def test_detachment_policy_uses_real_release_and_active_pool_then_only_keys(self):
        initial = {**vehicle(), "actor": 14, "session_state": 9,
                   "shared": {"detached_count": 0, "detached_pool_hex": bytes(10 * 0x11c).hex()}}
        policy = TRACE.FleetDetachmentPolicy(initial)
        self.assertEqual(policy.parameters["version"], 5)
        self.assertEqual(policy.parameters["initial_side_q16"], (0, 0, 65536))
        self.assertEqual(policy.parameters["target_right_q16"], (0, 0, -65536))
        for index in range(10):
            keys, decision = policy.choose(index, initial)
            self.assertEqual(keys, {"Up"})
            self.assertEqual(decision["released_updates"], 0)
        initial["session_state"] = 6
        dynamics = bytearray.fromhex(initial["dynamics_hex"])
        struct.pack_into("<i", dynamics, 0x148, 1)
        initial["dynamics_hex"] = dynamics.hex()
        self.assertEqual(policy.choose(10, initial)[1]["phase"], "held-grid")
        struct.pack_into("<i", dynamics, 0x148, 0)
        initial["dynamics_hex"] = dynamics.hex()
        for index in range(50):
            self.assertEqual(policy.choose(11 + index, initial)[0], {"Up"})
        self.assertEqual(policy.choose(61, initial)[0], {"Up", "Right"})
        initial["shared"]["detached_count"] = 1
        # Count alone cannot invent a live detached body.
        self.assertEqual(policy.choose(62, initial)[0], {"Up", "Right"})
        body = bytearray.fromhex(initial["body_hex"])
        # Positive initial side is the opposite turn, not a valid alignment.
        struct.pack_into("<3i", body, 0x150, 0, 0, 65536)
        initial["body_hex"] = body.hex()
        keys, decision = policy.choose(63, initial)
        self.assertEqual(keys, {"Up", "Right"})
        self.assertEqual(decision["alignment_dot_to_right_q16"], -65536)
        self.assertFalse(decision["aligned_to_right"])
        # Release steering at the measured 0.7 alignment before keyboard lag
        # carries the real body past its desired frontal approach.
        struct.pack_into("<3i", body, 0x150, 0, 0, -0xb332)
        initial["body_hex"] = body.hex()
        self.assertEqual(policy.choose(64, initial)[0], {"Up", "Right"})
        struct.pack_into("<3i", body, 0x150, 0, 0, -0xb333)
        initial["body_hex"] = body.hex()
        keys, decision = policy.choose(65, initial)
        self.assertEqual(keys, {"Up"})
        self.assertEqual(decision["alignment_dot_to_right_q16"], 0xb333)
        self.assertEqual(decision["phase"], "accelerate-to-wall")
        self.assertTrue(decision["aligned_to_right"])
        # Recoil or later yaw must not restart the continuous steering arc.
        struct.pack_into("<3i", body, 0x150, 65536, 0, 0)
        initial["body_hex"] = body.hex()
        self.assertEqual(policy.choose(66, initial)[0], {"Up"})
        pool = bytearray(10 * 0x11c)
        struct.pack_into("<I", pool, 3 * 0x11c + 0x108, 1)
        initial["shared"]["detached_pool_hex"] = pool.hex()
        keys, decision = policy.choose(67, initial)
        self.assertEqual(keys, set())
        self.assertEqual(decision["active_detached_slots"], [3])
        initial["shared"].update(detached_count=0, detached_pool_hex=bytes(10 * 0x11c).hex())
        self.assertEqual(policy.choose(68, initial)[0], set())

    def test_expiry_approach_uses_horizontal_source_pose_and_speed_instead_of_update_count(self):
        gate = TRACE.DetachmentApproachGate(2211506, 53635, "a9135ddd", 62)
        origin = (5 * 65536, -2097152000, 8 * 65536)
        initial = {**vehicle(position=origin), "actor": 14, "session_state": 9,
                   "shared": {"detached_count": 0, "detached_pool_hex": bytes(10 * 0x11c).hex()}}
        body = bytearray.fromhex(initial["body_hex"])
        struct.pack_into("<3i", body, 0x150, 0, 0x1000, 65536)
        struct.pack_into("<3i", body, 0x168, -65536, 0, 0)
        initial["body_hex"] = body.hex()
        def pose(progress, speed):
            current = {**initial, "session_state": 6}; raw = bytearray(body)
            #A grounded Y replacing the initial sentinel and a large lateral
            #displacement must not advance the horizontal initial-forward gate.
            struct.pack_into("<3i", raw, 0xbc, 1000 * 65536, 2147483647, origin[2] + progress)
            struct.pack_into("<i", raw, 0xe0, speed); current["body_hex"] = raw.hex()
            return current
        for elapsed_calls in (3, 150):
            policy = TRACE.FleetDetachmentPolicy(initial, gate)
            for index in range(elapsed_calls):
                keys, decision = policy.choose(index, pose(gate.progress_q16 - 1, gate.minimum_speed_q16))
                self.assertEqual(keys, {"Up"})
                self.assertEqual(decision["approach_progress_q16"], gate.progress_q16 - 1)
                self.assertFalse(decision["approach_ready"])
            self.assertEqual(policy.choose(elapsed_calls, pose(gate.progress_q16, gate.minimum_speed_q16 - 1))[0], {"Up"})
            keys, decision = policy.choose(elapsed_calls + 1, pose(gate.progress_q16, gate.minimum_speed_q16))
            with self.subTest(elapsed_calls=elapsed_calls):
                self.assertEqual(keys, {"Up", "Right"}); self.assertTrue(decision["approach_ready"])
                self.assertEqual(decision["approach_progress_q16"], gate.progress_q16)
                self.assertEqual(policy.choose(elapsed_calls + 2, pose(0, 0))[0], {"Up", "Right"})
