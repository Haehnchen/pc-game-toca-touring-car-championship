"""Source trace checks for clocks."""

import unittest
from unittest.mock import Mock
from unittest.mock import call
from unittest.mock import patch

from .support import TRACE, TraceTestCase


class ClockCaptureTests(TraceTestCase):
    def test_qpc_capture_brackets_actual_sampler_and_copies_external_low_word(self):
        debugger = Mock()
        debugger.command.side_effect = lambda command, *args: "0x1000" if command == "print $esp" else ""
        debugger.word.return_value = 0x44a516
        debugger.breakpoint.return_value = 8
        before = {"qpc_low": 0xfffffffa, "qpc_difference": 17}
        after = {"qpc_low": 20, "qpc_difference": 25}
        with patch.object(TRACE, "capture_qpc_values", side_effect=[before, after]):
            event = TRACE.capture_qpc_sample(debugger)
        self.assertEqual(event, {"return_address": 0x44a516, "counter": 20, "pre": before, "post": after})
        debugger.stop_at.assert_called_once_with(0x44a516)
        self.assertIn(call("delete break 8"), debugger.command.call_args_list)
        debugger.word.return_value = 0x44a470
        with self.assertRaisesRegex(ValueError, "caller"): TRACE.capture_qpc_sample(debugger)

    def test_cold_timer_creator_reads_actual_return_in_between_initializer_calls(self):
        for clock in (0, 13668):
            debugger = Mock()
            debugger.word.return_value = 0x44a470
            debugger.breakpoint.return_value = 7
            debugger.command.side_effect = lambda value, *args: "0x1000" if value == "print $esp" else str(clock) if value == "print $eax" else ""
            sequence = {"events": [{"ordinal": 0}], "first_pre": None}
            result = TRACE.capture_cold_timer_creation(debugger, sequence)
            self.assertEqual(result["creation_clock"], clock)
            self.assertEqual(result["caller_return_address"], 0x44a470)
            self.assertEqual(len(sequence["events"]), 1)
            self.assertIs(sequence["timer_creation"], result)
            debugger.stop_at.assert_called_once_with(0x447a77)
            self.assertIn(call("delete break 7"), debugger.command.call_args_list)

    def test_cold_timer_creator_rejects_wrong_caller_or_initializer_order(self):
        for sequence in ({"events": []}, {"events": [{}, {}]}, {"events": [{}], "timer_creation": {}}):
            with self.assertRaisesRegex(ValueError, "once between"):
                TRACE.capture_cold_timer_creation(Mock(), sequence)
        debugger = Mock()
        debugger.command.return_value = "0x1000"
        debugger.word.return_value = 0x449f50
        with self.assertRaisesRegex(ValueError, "caller"):
            TRACE.capture_cold_timer_creation(debugger, {"events": [{}]})
        debugger.breakpoint.assert_not_called()

    def test_primary_journal_records_only_outer_boundary_and_cached_clock(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda pc: pc
        replies = iter(["Stopped at0044a370", "Stopped at00421aa0"])
        debugger.command.side_effect = lambda command, *args: next(replies) if command == "cont" else ""
        debugger.word.return_value = 12345
        with patch.object(TRACE, "contact_order", return_value=17):
            journal = TRACE.advance_combined_update(debugger, local_session=True)
        self.assertEqual(journal, [{"kind": "primary", "event": {"cached_clock": 12345, "order": 17}}])
        debugger.word.assert_called_once_with(0x4eda6c)

    def test_warm_restart_observes_separate_tick_sample_before_source_writes(self):
        capture = TRACE.WarmRestartCapture({})
        capture.requested = True
        debugger = Mock()
        debugger.command.side_effect = lambda command, *args: "$esp = 0x10000" if command == "print $esp" else "$eax = 0x76543210"
        debugger.word.return_value = 0x44a500
        debugger.breakpoint.side_effect = lambda address: address
        self.assertEqual(capture.restart(debugger), {"creation_clock": 0x76543210})
        self.assertEqual(debugger.stop_at.call_args_list, [unittest.mock.call(0x447a77), unittest.mock.call(0x44a500)])
        self.assertTrue(capture.restarted)
        with self.assertRaisesRegex(ValueError, "single actual"):
            capture.restart(debugger)
