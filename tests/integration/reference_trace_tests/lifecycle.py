"""Source trace checks for lifecycle."""

import unittest
import struct
import io
from unittest.mock import Mock
from unittest.mock import call
from unittest.mock import patch

from .support import TRACE, TraceTestCase


class SessionLifecycleTests(TraceTestCase):
    def test_selected_contact_warm_start_keeps_grid_provenance_and_cold_mode_guard(self):
        initial = {"session_state": 6, "update_counter": 340, "shared": {"ai_state": {"release_counter": 32}}}
        TRACE.validate_joined_capture_start({"observed": True}, initial, True)
        with self.assertRaisesRegex(ValueError, "first unreleased"): TRACE.validate_joined_capture_start({"observed": True}, initial)
        with self.assertRaisesRegex(ValueError, "observed460170"): TRACE.validate_joined_capture_start(None, initial, True)
        initial["shared"]["ai_state"]["release_counter"] = 361
        with self.assertRaises(ValueError): TRACE.validate_joined_capture_start({"observed": True}, initial, True)

    def test_explicit_local_lap_warmup_bound_accepts_three_lap_capture_budget(self):
        import sys
        for updates in (18000, 20000):
            with self.subTest(updates=updates), patch.object(sys, "argv", ["trace-reference.py", "--local-lap-max-updates", str(updates)]), patch.object(TRACE.Path, "read_bytes", side_effect=RuntimeError("argument validation passed")):
                #Stop before executable reads or runtime setup: this tests only
                #the explicit parser budget, without starting the original.
                with self.assertRaisesRegex(RuntimeError, "argument validation passed"):
                    TRACE.main()
        for updates in (0, 20001):
            with self.subTest(updates=updates), patch.object(sys, "argv", ["trace-reference.py", "--local-lap-max-updates", str(updates)]), patch.object(TRACE.Path, "read_bytes") as read, patch("sys.stderr", new=io.StringIO()), self.assertRaises(SystemExit) as error:
                TRACE.main()
            self.assertEqual(error.exception.code, 2)
            read.assert_not_called()

    def test_joined_ai_finish_warmup_requires_complete_bounded_capture_profile(self):
        import sys
        profile = ["trace-reference.py", "--scope", "fleet", "--contact-weather-rng", "--checkpoint-timing",
                   "--finish-timing", "--warmup-until-ai-finish", "--laps", "1", "--scenario", "coast"]
        with patch.object(sys, "argv", profile), patch.object(TRACE.Path, "read_bytes", side_effect=RuntimeError("argument validation passed")):
            with self.assertRaisesRegex(RuntimeError, "argument validation passed"):
                TRACE.main()
        invalid = [[arg for arg in profile if arg != "--finish-timing"],
                   [*profile, "--cold-grid-init"], [*profile, "--warmup-step", "1:Up"],
                   ["trace-reference.py", "--scope", "fleet", "--ai-state", "--traffic-controls", "--joined-ai",
                    "--checkpoint-timing", "--finish-timing", "--warmup-until-ai-finish"]]
        for arguments in invalid:
            with self.subTest(arguments=arguments), patch.object(sys, "argv", arguments), patch.object(TRACE.Path, "read_bytes") as read, \
                    patch("sys.stderr", new=io.StringIO()), self.assertRaises(SystemExit) as error:
                TRACE.main()
            self.assertEqual(error.exception.code, 2)
            read.assert_not_called()

    def test_joined_ai_finish_capture_quality_uses_the_shared_positive_event_gate(self):
        broad = {"passed": False, "actors_moved": list(range(15))}
        header, sparse = {"scope": "fixture"}, [{"id": 0}]
        for required, observed, sufficient, passed in ((True, True, True, True), (True, False, False, False), (False, False, True, False)):
            focused = {"targeted_ai_finish_required": required, "targeted_ai_finish_observed": observed,
                       "coverage_sufficient": sufficient, "coverage": {"newly_finished_ai": []}}
            with patch.object(TRACE.FLEET, "retained_race_timing_coverage", return_value=focused) as common:
                quality = TRACE.capture_quality(broad, header, timing_records=sparse)
            with self.subTest(required=required, observed=observed):
                common.assert_called_once_with(header, sparse)
                self.assertEqual(quality["profile"], "retained-ai-finish")
                self.assertEqual(quality["gates"], {"chosen_ai_finish": passed})
                self.assertEqual(quality["passed"], passed)
                self.assertEqual(quality["activity_diagnostics"], broad)
                self.assertEqual({key: quality[key] for key in focused}, focused)

    def test_local_finish_profile_requires_complete_session_and_lifecycle_provenance(self):
        import sys
        profile = ["trace-reference.py", "--scope", "fleet", "--contact-weather-rng", "--checkpoint-timing", "--finish-timing",
                   "--local-handoffs", "--session-boundaries", "--warmup-until-local-finish", "--scenario", "local-lap", "--laps", "1"]
        with patch.object(sys, "argv", profile), patch.object(TRACE.Path, "read_bytes", side_effect=RuntimeError("argument validation passed")):
            with self.assertRaisesRegex(RuntimeError, "argument validation passed"):
                TRACE.main()
        for missing in ("--local-handoffs", "--session-boundaries", "--finish-timing"):
            arguments = [value for value in profile if value != missing]
            with self.subTest(missing=missing), patch.object(sys, "argv", arguments), patch.object(TRACE.Path, "read_bytes") as read, \
                    patch("sys.stderr", new=io.StringIO()), self.assertRaises(SystemExit) as error:
                TRACE.main()
            self.assertEqual(error.exception.code, 2)
            read.assert_not_called()

    def test_warm_restart_uses_real_menu_key_and_accepts_only_one_results_choice(self):
        capture = TRACE.WarmRestartCapture({"DISPLAY": ":private"})
        debugger = Mock()
        debugger.word.side_effect = lambda address: {0x4dcd9c: 0, 0x4b0660: 0x100000, 0x1005f8: 13}[address]
        with patch.object(TRACE, "keyboard") as key:
            capture.primary(debugger)
            key.assert_not_called()
            debugger.word.side_effect = lambda address: {0x4dcd9c: 1 << 16, 0x4b0660: 0x100000, 0x1005f8: 13}[address]
            capture.primary(debugger)
            self.assertEqual(capture.request(debugger), {"action": "confirm"})
            self.assertEqual(key.call_args_list, [unittest.mock.call({"DISPLAY": ":private"}, "keydown", "Return"),
                                                 unittest.mock.call({"DISPLAY": ":private"}, "keyup", "Return")])
            with self.assertRaisesRegex(ValueError, "one actual"):
                capture.request(debugger)

    def test_warm_grid_observation_reuses_callee_boundaries_without_a_second_writer(self):
        capture = TRACE.WarmRestartCapture({})
        capture.restarted = True
        debugger = Mock()
        debugger.command.return_value = "$esp = 0x10000"
        debugger.word.return_value = 0x4487d2
        debugger.breakpoint.side_effect = lambda address: address
        def observed(debugger, entry, cold_grid):
            cold_grid.update(pre={"actual": "entry"}, post={"actual": "return"})
            return {"return_address": 0x4479e5}
        with patch.object(TRACE, "capture_start_call", side_effect=observed) as call:
            event = capture.initialize_grid(debugger)
        call.assert_called_once()
        self.assertEqual(event, {"outer_return_address": 0x4487d2, "initialization": {"return_address": 0x4479e5},
                                 "boundaries": {"pre": {"actual": "entry"}, "post": {"actual": "return"}}})
        with self.assertRaisesRegex(ValueError, "exactly one"):
            capture.initialize_grid(debugger)

    def test_warm_restart_requires_the_complete_local_capture_profile(self):
        import sys
        with patch.object(sys, "argv", ["trace-reference.py", "--warm-restart"]), patch.object(TRACE.Path, "read_bytes") as read, \
                patch("sys.stderr", new=io.StringIO()), self.assertRaises(SystemExit) as error:
            TRACE.main()
        self.assertEqual(error.exception.code, 2)
        read.assert_not_called()

    def test_local_finish_capture_quality_requires_the_selected_finish_and_result_transition(self):
        broad = {"passed": True, "actors_moved": list(range(16))}
        header, sparse = {"scope": "fixture"}, [{"id": 0}]
        for required, observed, sufficient, passed in ((True, True, True, True), (True, True, False, False), (False, False, True, False)):
            focused = {"targeted_local_finish_required": required, "targeted_local_finish_observed": observed,
                       "coverage_sufficient": sufficient, "coverage": {"result_transitions": []}}
            with patch.object(TRACE.FLEET, "retained_local_session_coverage", return_value=focused) as common:
                quality = TRACE.capture_quality(broad, header, local_records=sparse)
            with self.subTest(required=required, sufficient=sufficient):
                common.assert_called_once_with(header, sparse)
                self.assertEqual(quality["profile"], "retained-local-finish")
                self.assertEqual(quality["gates"], {"local_finish_and_results": passed})
                self.assertEqual(quality["passed"], passed)
                self.assertEqual(quality["activity_diagnostics"], broad)

    def test_warm_restart_capture_quality_requires_the_post_reset_updates(self):
        focused = {"targeted_warm_restart_required": True, "targeted_warm_restart_observed": True,
                   "coverage_sufficient": False, "coverage": {"warm_update_ids": [40]}}
        with patch.object(TRACE.FLEET, "retained_warm_restart_enabled", return_value=True), \
                patch.object(TRACE.FLEET, "retained_warm_restart_coverage", return_value=focused) as common:
            quality = TRACE.capture_quality({"passed": True}, {}, local_records=[{"id": 40}])
        common.assert_called_once_with({}, [{"id": 40}])
        self.assertEqual(quality["profile"], "retained-warm-restart")
        self.assertFalse(quality["passed"])

    def test_local_driver_counter_reset_requires_actual_first_finisher_evidence(self):
        games = bytearray(16 * 0x124)
        offset = 5 * 0x124
        games[offset + 0x80], games[offset + 0x8d] = 0xff, 9
        struct.pack_into("<h", games, offset + 0xa8, 1)
        before = {"update_counter": 2444, "source_mode": 0, "session_state": 6, "session_stage": 2, "lap_limit": 1,
                  "finish_counts_hex": bytes(8).hex(), "all_gameplay_hex": games.hex()}
        struct.pack_into("<I", games, offset + 0x54, 1)
        struct.pack_into("<h", games, offset + 0xa8, 2)
        after = {**before, "update_counter": 0, "finish_counts_hex": bytes([1] + [0] * 7).hex(),
                 "finish_order_hex": bytes([9] + [0] * 15).hex(), "all_gameplay_hex": games.hex(), "local_finish_flags_hex": bytes(4).hex()}
        self.assertEqual(TRACE.validate_driving_step(before, after)["actor"], 5)
        after["finish_order_hex"] = bytes(16).hex()
        with self.assertRaisesRegex(RuntimeError, "exact first-finisher"):
            TRACE.validate_driving_step(before, after)
        after["finish_counts_hex"] = bytes(8).hex()
        with self.assertRaisesRegex(RuntimeError, "discontinuity"):
            TRACE.validate_driving_step(before, after)

    def test_lap_selection_uses_original_menu_keys_and_observes_derived_lap_count(self):
        state = {"row": 3, "choice": 2}
        debugger = Mock()
        choices = (1, 2, 3, 5, 10, 15, 20)
        def word(address):
            return {0x4b0660: 0x600000, 0x52c21c: 0xd, 0x600004: 0,
                    0x4b6ee8: state["row"] << 8, 0x600014: state["choice"], 0x600018: choices[state["choice"]]}[address]
        def key(_debugger, _environment, handler, name):
            self.assertEqual(handler, 0x469713)
            if name == "Up": state["row"] = (state["row"] - 1) % 5
            else: state["choice"] = (state["choice"] + (1 if name == "Right" else -1)) % 7
        debugger.word.side_effect = word
        with patch.object(TRACE, "menu_key", side_effect=key) as keys:
            result = TRACE.select_laps(debugger, {}, 1)
        self.assertEqual([entry.args[-1] for entry in keys.call_args_list], ["Up", "Up", "Left", "Left"])
        self.assertEqual(result["final_laps"], 1)
        debugger.command.assert_not_called()

    def test_checkpoint_endpoint_requires_actual_local_write_clock_rank_and_sector_contract(self):
        for kwargs in ({"write": False}, {"actor": 13}, {"sector": 0, "lap": 1}):
            self.assertIsNone(TRACE.local_checkpoint_endpoint(self.checkpoint_record(**kwargs), 14, None))
        record = self.checkpoint_record()
        for field, value in (("race_clock", 999), ("session_state", 9)):
            original = record["ai_post"][field]; record["ai_post"][field] = value
            self.assertIsNone(TRACE.local_checkpoint_endpoint(record, 14, None))
            record["ai_post"][field] = original
        game = bytearray.fromhex(record["ai_post"]["actors"][14]["gameplay_hex"]); game[0x8f] = 3
        record["ai_post"]["actors"][14]["gameplay_hex"] = game.hex()
        self.assertIsNone(TRACE.local_checkpoint_endpoint(record, 14, None))
        endpoint = TRACE.local_checkpoint_endpoint(self.checkpoint_record(), 14, None)
        self.assertEqual((endpoint["slot"], endpoint["actor"], endpoint["sector"], endpoint["rank"]), (17, 14, 1, 2))

    def test_checkpoint_endpoint_proves_three_later_display_wrappers_and_rejects_reset(self):
        endpoint = TRACE.local_checkpoint_endpoint(self.checkpoint_record(), 14, None)
        for index, ticks in ((11, 1), (12, 1), (13, 2), (14, 3)):
            endpoint = TRACE.local_checkpoint_endpoint(self.checkpoint_record(index, ticks, write=False), 14, endpoint)
        self.assertEqual(endpoint["followup_updates"], [11, 13, 14])
        for ticks in (0, 5):
            self.assertIsNone(TRACE.local_checkpoint_endpoint(self.checkpoint_record(15, ticks, write=False), 14, endpoint))
        record = self.checkpoint_record(15, 4, write=False)
        game = bytearray.fromhex(record["ai_post"]["actors"][14]["gameplay_hex"]); struct.pack_into("<I", game, 0x54, 1)
        record["ai_post"]["actors"][14]["gameplay_hex"] = game.hex()
        self.assertIsNone(TRACE.local_checkpoint_endpoint(record, 14, endpoint))

    def test_weather_selection_uses_original_five_rows_and_six_choices(self):
        self.assertEqual(TRACE.weather_keyboard_steps(4, 0, 3), ["Up", "Right", "Right", "Right", "Down"])
        self.assertEqual(TRACE.weather_keyboard_steps(4, 5, 1), ["Up", "Right", "Right", "Down"])
        self.assertEqual(TRACE.weather_keyboard_steps(0, 4, 4), ["Up", "Up", "Down"])
        with self.assertRaises(RuntimeError):
            TRACE.weather_keyboard_steps(5, 0, 1)

    def test_time_trial_weather_uses_three_rows_without_loading_a_ghost(self):
        self.assertEqual(TRACE.weather_keyboard_steps(2, 0, 3, "time-trial"),
                         ["Up", "Up", "Right", "Right", "Right", "Up"])
        self.assertEqual(TRACE.weather_keyboard_steps(2, 4, 4, "time-trial"), ["Up", "Up", "Up"])
        self.assertEqual(TRACE.weather_keyboard_steps(0, 0, 0, "time-trial"), ["Up"])
        with self.assertRaises(RuntimeError):
            TRACE.weather_keyboard_steps(3, 0, 1, "time-trial")

    def test_time_trial_weather_observes_its_handler_and_keeps_continue_selected(self):
        class Options:
            def __init__(self):
                self.selection, self.weather, self.key = 2, 0, None

            def word(self, address):
                return {0x52ee18: 1, 0x52c21c: 0xe, 0x4b0660: 0x900000,
                        0x9000a0: self.weather, 0x4b71e8: self.selection << 8}[address]

            def stop_at(self, address):
                self_outer.assertEqual(address, 0x469979)
                if self.key == "Up":
                    self.selection = (self.selection - 1) % 3
                elif self.key == "Right":
                    self_outer.assertEqual(self.selection, 0)
                    self.weather = (self.weather + 1) % 6
                self.key = None

        self_outer = self
        options = Options()
        def keyboard(environment, action, key):
            if action == "keydown":
                options.key = key
        with patch.object(TRACE, "keyboard", keyboard):
            result = TRACE.select_weather(options, {}, 3, "time-trial")
        self.assertEqual((options.selection, options.weather), (2, 3))
        self.assertEqual(result["session"], "time-trial")
        self.assertEqual(result["final_choice"], 3)
        self.assertFalse(any(key["key"] == "Return" for key in result["keys"]))

    def test_time_trial_selection_observes_real_menu_and_skips_disabled_showdown(self):
        class Menus:
            def __init__(self):
                self.menu, self.selection, self.key = 0x4b6bc0, 0, None

            def command(self, command):
                self_outer.assertEqual(command, "print $esp")
                return "0x800000"

            def word(self, address):
                return {0x800004: self.menu, 0x52c21c: 0xb,
                        0x4b6d68: self.selection << 8}[address]

            def stop_at(self, address):
                self_outer.assertEqual(address, 0x46e6c2)
                if self.key == "Return":
                    self.menu = 0x4b6d40
                elif self.key == "Down":
                    self.selection = {0: 1, 1: 3}[self.selection]
                self.key = None

        self_outer = self
        menus = Menus()
        def keyboard(environment, action, key):
            self.assertIn(action, ("keydown", "keyup"))
            if action == "keydown":
                menus.key = key
        with patch.object(TRACE, "keyboard", keyboard):
            result = TRACE.select_time_trial(menus, {})
        self.assertEqual(result["final_row"], 3)
        self.assertEqual([key["key"] for key in result["keys"]], ["Return", "Down", "Down"])
        self.assertEqual([key["observed_row"] for key in result["keys"] if "observed_row" in key], [1, 3])

    def test_single_race_selection_wraps_persisted_time_trial_choice(self):
        class Menus:
            selection = 3
            def command(self, command):
                return "0x800000"
            def word(self, address):
                return {0x800004: 0x4b6d40, 0x52c21c: 0xb,
                        0x4b6d68: self.selection << 8}[address]
        menus = Menus()
        def key(debugger, environment, address, value):
            self.assertEqual(value, "Down")
            menus.selection = (menus.selection + 1) % 5
        with patch.object(TRACE, "menu_key", key):
            result = TRACE.select_session(menus, {}, "single-race")
        self.assertEqual(result["final_row"], 0)
        self.assertEqual(len(result["keys"]), 2)

    def test_no_qualifying_uses_real_row2_keys_and_verifies_original_flag(self):
        class Menus:
            selection = 4
            qualifying = 0
            def word(self, address):
                return {0x52ee18: 1, 0x52c21c: 0xd, 0x4b0660: 0x900000,
                        0x900004: 0, 0x9000ac: 1, 0x900020: self.qualifying,
                        0x4b6ee8: self.selection << 8}[address]
        menus = Menus()
        def key(debugger, environment, address, value):
            self.assertIs(debugger, menus)
            self.assertEqual(address, 0x469713)
            if value == "Up":
                menus.selection = (menus.selection - 1) % 5
            elif value == "Right":
                self.assertEqual(menus.selection, 2)
                menus.qualifying ^= 1
        with patch.object(TRACE, "menu_key", key):
            result = TRACE.select_no_qualifying(menus, {})
        self.assertEqual([entry["key"] for entry in result["keys"]], ["Up", "Up", "Right"])
        self.assertEqual(result["final_flag"], 1)
        with patch.object(TRACE, "menu_key", key):
            result = TRACE.select_no_qualifying(menus, {})
        self.assertEqual(result["keys"], [])

    def test_vehicle_qualifying_yes_restores_a_persisted_fleet_noqual_choice(self):
        class Menus:
            selection = 4
            qualifying = 1
            def word(self, address):
                return {0x52ee18: 1, 0x52c21c: 0xd, 0x4b0660: 0x900000,
                        0x900004: 0, 0x9000ac: 1, 0x900020: self.qualifying,
                        0x4b6ee8: self.selection << 8}[address]
        menus = Menus()
        def key(debugger, environment, address, value):
            self.assertEqual(address, 0x469713)
            if value == "Up":
                menus.selection = (menus.selection - 1) % 5
            elif value == "Right":
                self.assertEqual(menus.selection, 2)
                menus.qualifying ^= 1
        with patch.object(TRACE, "menu_key", key):
            result = TRACE.select_qualifying(menus, {}, True)
        self.assertTrue(result["requested_qualifying"])
        self.assertEqual(result["initial_flag"], 1)
        self.assertEqual(result["final_flag"], 0)
        self.assertEqual([entry["key"] for entry in result["keys"]], ["Up", "Up", "Right"])
        with patch.object(TRACE, "menu_key", key):
            result = TRACE.select_qualifying(menus, {}, True)
        self.assertEqual(result["keys"], [])
