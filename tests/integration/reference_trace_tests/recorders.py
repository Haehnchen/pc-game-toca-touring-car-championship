"""Source trace checks for recorders."""

import tempfile
import struct
import os
import socket
from pathlib import Path
from unittest.mock import Mock
from unittest.mock import call
from unittest.mock import patch

from .support import TRACE, TraceTestCase


class AudioRecorderTests(TraceTestCase):
    def test_contact_interceptor_preserves_requested_boundary_and_actual_call_order(self):
        debugger = TRACE.Debugger.__new__(TRACE.Debugger)
        debugger.send = Mock()
        debugger.read_prompt = Mock(side_effect=["Stopped at00424860", "Stopped at004250a0", "Stopped at00421980"])
        debugger.confirm_pause = Mock()
        hook = TRACE.ContactAudioRecorder.__new__(TRACE.ContactAudioRecorder)
        hook.debugger, hook.phase, hook.outer, hook.busy, hook.serial, hook.calls = debugger, "consumed_to_contacts", None, False, 0, []
        debugger.contact_audio = hook
        with patch.object(TRACE, "capture_contact_audio_call", side_effect=[{"kind": 3}, {"kind": 10}]):
            self.assertIn("00421980", TRACE.Debugger.command(debugger, "cont"))
        self.assertEqual([entry["kind"] for entry in hook.calls], ["impact", "scrape"])
        self.assertEqual([entry["event"]["order"] for entry in hook.calls], [1, 2])
        self.assertTrue(all(entry["event"]["phase"] == "consumed_to_contacts" for entry in hook.calls))
        hook.outer = "motor"
        with self.assertRaisesRegex(ValueError, "nested"): hook.intercept("Stopped at00424860")

    def test_publication_loop_and_tail_return_preserve_contact_order(self):
        debugger = Mock()
        debugger.command.return_value = "0x1000"
        debugger.word.return_value = 0x44a200
        hook = TRACE.BrakePublicationRecorder(debugger)
        contact = TRACE.ContactAudioRecorder.__new__(TRACE.ContactAudioRecorder)
        contact.phase, contact.busy, contact.outer, contact.serial = "frame", False, None, 0
        debugger.contact_audio = contact
        def stop(pc):
            if pc == 0x44a200:
                self.assertEqual(contact.order(), 2)  # actual tail contact's journal order
        debugger.stop_at.side_effect = stop
        snapshots = [{"boundary": name, "actor_count": 16, "rng": 7, "raw_brake_hex": "00" * 64,
                      "published_brake_hex": "00" * 64} for name in ("4608b0-entry", "4610ed", "4608b0-return")]
        with patch.object(TRACE.FLEET, "capture_brake_publication", side_effect=snapshots):
            self.assertFalse(hook.intercept("Stopped at00421aa0"))
            self.assertTrue(hook.intercept("Stopped at004608b0"))
        self.assertEqual([entry["kind"] for entry in hook.calls], ["publication", "publication_return"])
        self.assertEqual([entry["event"]["order"] for entry in hook.calls], [1, 3])
        self.assertEqual(debugger.stop_at.call_args_list, [call(0x4610ed), call(0x44a200)])
        self.assertEqual(hook.calls[0]["event"]["post"], hook.calls[1]["event"]["post"])
        contact.phase = "ai_route"
        with self.assertRaisesRegex(ValueError, "nested brake publication"): hook.intercept("Stopped at004608b0")

    def test_contact_capture_records_independent_spatial_selection_and_play_outputs(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda pc: pc
        pc = [0]
        stops = iter([0x424921, 0x4249b9, 0x4249d0, 0x424a1d, 0x424a22, 0x4795f6, 0x424a2e, 0x4798cb, 0x424a41, 0x429b6f])
        def command(value, *args):
            if value == "cont": pc[0] = next(stops); return f"Stopped at{pc[0]:08x}"
            if value == "print $esp": return "0x1000"
            if value == "print $ebp": return "0x2000"
            if value == "print $esi": return "31"
            if value == "print $ebx": return "64"
            if value == "print $eax": return "0x4d3d28" if pc[0] == 0x424a22 else "65536" if pc[0] == 0x4249d0 else "0" if pc[0] == 0x424a2e else "1"
            return ""
        def memory(address, size):
            if address == 0x1000 and size == 20: return struct.pack("<5I", 0x429b6f, 0x3000, 3, 0x4000, 1)
            if address == 0x1000 and size == 8: return struct.pack("<2I", 0x424a2e, 0x4d3d28)
            if address == 0x1000 and size == 28: return struct.pack("<7I", 0x424a41, 31, 1, 75, 0, 64, 0x4d3d28)
            if address == 0x3000: return bytes(12)
            raise AssertionError((address, size))
        debugger.command.side_effect = command
        debugger.memory.side_effect = memory
        debugger.word.side_effect = lambda address: 75 if address == 0x2010 else 42 if address == 0x1ffc else 65536
        debugger.paused_memory.return_value = debugger
        with patch.object(TRACE.FLEET, "capture_contact_context", side_effect=[{"rng": 0}, {"rng": 2531011}]):
            actual = TRACE.capture_contact_audio_call(debugger, 0x424860)
        self.assertEqual(actual["sample"], 31)
        self.assertEqual(actual["spatial"], {"accepted": 1, "volume": 75, "angle": 42})
        self.assertEqual(actual["volume"], 75)
        self.assertEqual(actual["pan"], 64)
        self.assertEqual(actual["queries"][0]["playing"], 0)
        self.assertEqual(actual["commands"][0]["sample"], 31)
        self.assertEqual(actual["commands"][0]["allocation_succeeded"], 1)
        self.assertEqual(actual["commands"][0]["return_eax"], 1)
        self.assertEqual(actual["commands"][0]["voice_handle_after"], 65536)
        self.assertEqual(actual["post"]["rng"], 2531011)

    def test_combined_frame_journal_preserves_mixed_source_order_and_empty_intervals(self):
        for sequence in ([], [0x446b50, 0x44a6f0, 0x425e90, 0x448130, 0x44a6f0, 0x425e90]):
            debugger = Mock()
            debugger.breakpoint.side_effect = lambda pc: pc
            stops = iter([f"Stopped at{pc:08x}" for pc in sequence] + ["Stopped at00421aa0"])
            debugger.command.side_effect = lambda command, *args: next(stops) if command == "cont" else ""
            with patch.object(TRACE, "capture_wheel_callback", return_value={"call": "wheel"}), \
                    patch.object(TRACE, "capture_motor_callback", return_value={"call": "motor"}), \
                    patch.object(TRACE, "capture_mute_call", return_value={"call": "mute"}), \
                    patch.object(TRACE, "capture_start_call", return_value={"call": "release"}):
                actual = TRACE.advance_combined_update(debugger)
            labels = {0x446b50: "wheel", 0x44a6f0: "motor", 0x425e90: "mute", 0x448130: "release"}
            self.assertEqual([event["kind"] for event in actual], [labels[pc] for pc in sequence])

    def test_combined_ground_records_actual_surface_calls_before_effect_boundary(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda pc: pc
        stops = iter(["Stopped at00425400", "Stopped at00425400", "Stopped at00446a80"])
        debugger.command.side_effect = lambda command, *args: next(stops) if command == "cont" else ""
        with patch.object(TRACE, "capture_surface_call", side_effect=[{"wheel": 3}, {"wheel": 2}]):
            calls, world = TRACE.capture_ground_surfaces(debugger, lambda boundary: {"boundary": boundary})
        self.assertEqual(calls, [{"wheel": 3}, {"wheel": 2}])
        self.assertEqual(world, {"boundary": "446a80-entry"})

    def test_reference_audio_launch_defaults_muted_and_requires_explicit_private_socket(self):
        environment = {"PULSE_SERVER": "unix:/private/no-server"}
        self.assertEqual(TRACE.reference_audio_route(environment)[0], "--mute")
        self.assertEqual(environment["PULSE_SERVER"], "unix:/private/no-server")
        for server in ("", "unix:relative", "tcp:localhost", "unix:/nonexistent/socket"):
            with patch.dict(os.environ, {"PULSE_SERVER": server}), self.assertRaisesRegex(ValueError, "absolute Unix"):
                TRACE.reference_audio_route(environment, True)
        #AF_UNIX paths have a small fixed limit; isolated build TMPDIRs can be long.
        with tempfile.TemporaryDirectory(dir="/tmp") as directory, socket.socket(socket.AF_UNIX) as listener:
            path = str(Path(directory) / "pulse.sock")
            listener.bind(path)
            with patch.dict(os.environ, {"PULSE_SERVER": "unix:" + path}):
                flag, provenance = TRACE.reference_audio_route(environment, True)
            self.assertEqual(flag, "--audio-null-sink")
            self.assertEqual(environment["PULSE_SERVER"], "unix:" + path)
            self.assertTrue(provenance["wine_alsa_oss_disabled"])
            self.assertEqual(provenance["mode"], "winepulse-private-null-sink")

    def test_whine_advance_preserves_zero_and_multiple_actual_motor_callbacks(self):
        for count in (0, 2):
            debugger = Mock()
            debugger.breakpoint.return_value = 9
            stops = iter(["Stopped at0044a6f0"] * count + ["Stopped at00421aa0"])
            debugger.command.side_effect = lambda command, *args: next(stops) if command == "cont" else ""
            with patch.object(TRACE, "capture_motor_callback", side_effect=[{"ordinal": i} for i in range(count)]):
                self.assertEqual(TRACE.advance_whine_update(debugger), [{"ordinal": i} for i in range(count)])
            self.assertIn(call("delete break 9"), debugger.command.call_args_list)

    def test_whine_records_raw_query_return_offset_and_actual_ail_commands(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda address: address
        stops = iter([0x414825, 0x4795f6, 0x4148d9, 0x479b21, 0x479b3d, 0x4144d3])
        active_pc = [None]
        def command(value, *args):
            if value == "print $esp": return "0x1000"
            if value == "print $ebp": return "0x19"
            if value == "print $eax": return "0x1"
            if value == "cont":
                active_pc[0] = next(stops)
                return f"Stopped at{active_pc[0]:08x}"
            return ""
        def memory(address, size):
            if size == 8: return struct.pack("<2I", 0x4144d3, 0x2000)
            if size == 24: return bytes(24)
            return struct.pack("<3I", 0x4148d9 if active_pc[0] == 0x4795f6 else 0x4148f2 if active_pc[0] == 0x479b21 else 0x41494e,
                               0x4be1d4, 22050 if active_pc[0] == 0x479b21 else 35)
        debugger.command.side_effect = command
        debugger.memory.side_effect = memory
        with patch.object(TRACE.FLEET, "capture_whine_state", side_effect=[{"rng": 1}, {"rng": 2}]):
            result = TRACE.capture_whine_call(debugger)
        self.assertEqual(result["offset"], 25)
        self.assertEqual(result["queries"], [{"return_address": 0x4148d9, "playing": 1}])
        self.assertEqual([(command["kind"], command["value"]) for command in result["commands"]], [("rate", 22050), ("volume", 35)])

    def test_wheel_queue_advance_records_zero_or_multiple_real_callbacks(self):
        for count in (0, 2):
            with self.subTest(callbacks=count):
                debugger = Mock()
                debugger.breakpoint.return_value = 11
                stops = iter(["Stopped at00446b50"] * count + ["Stopped at00421aa0"])
                debugger.command.side_effect = lambda command, *args: next(stops) if command == "cont" else ""
                with patch.object(TRACE, "capture_wheel_callback", side_effect=[{"view": 0, "ordinal": i} for i in range(count)]) as capture:
                    callbacks = TRACE.advance_wheel_queue_update(debugger)
                self.assertEqual([event["ordinal"] for event in callbacks], list(range(count)))
                self.assertEqual(capture.call_count, count)
                self.assertIn(call("delete break 11"), debugger.command.call_args_list)

    def test_wheel_callback_brackets_actual_producer_and_verifies_pointer_arguments(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda address: address
        stops = iter([0x44b690, 0x446c9d, 0x420429])
        debugger.command.side_effect = lambda command, *args: ("0x1000" if command == "print $esp" else f"Stopped at{next(stops):08x}" if command == "cont" else "")
        debugger.memory.side_effect = lambda address, size: (struct.pack("<2I", 0x420429, 0) if size == 8 else
                                                            struct.pack("<3I", 0x446c9d, TRACE.LAYOUT["dynamics_base"] + 14 * 0x338, TRACE.LAYOUT["body_base"] + 14 * 0x53c))
        with patch.object(TRACE.FLEET, "capture_wheel_state", side_effect=[{"boundary": "446b50-entry"}, {"boundary": "446b50-return"}]), \
                patch.object(TRACE.FLEET, "capture_wheel_actor", side_effect=[{"actor": 14, "rng": 1}, {"actor": 14, "rng": 1}]), \
                patch.object(TRACE.FLEET, "validate_wheel_callback") as validate:
            result = TRACE.capture_wheel_callback(debugger)
        self.assertEqual(result["view"], 0)
        self.assertEqual(result["producers"][0]["actor"], 14)
        self.assertEqual(result["producers"][0]["return_address"], 0x446c9d)
        validate.assert_called_once_with(result)

    def test_skid_dispatch_does_not_break_on_skipped_wheel_join_instruction(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda address: address
        def command(value, *args):
            if value == "print $esp":
                return "0x1000"
            if value == "cont":
                self.assertNotIn(call(TRACE.FLEET.SKID_MANIFEST["callee_return"]), debugger.breakpoint.call_args_list)
                return "Stopped at00446b24"
            return ""
        debugger.command.side_effect = command
        debugger.word.return_value = 0x446b24
        with patch.object(TRACE.FLEET, "capture_skid_state", side_effect=[{"boundary": "44c540-entry"}, {"boundary": "44c540-return"}]):
            result = TRACE.capture_skid_dispatch(debugger)
        self.assertEqual(result["calls"], [])
        self.assertEqual(result["post"]["boundary"], "44c540-return")

    def test_skid_dispatch_observes_actual_ebx_sample_and_callee_return(self):
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda address: address
        stops = iter([0x424d50, 0x424eb2, 0x44c5a3, 0x446b24])
        def command(value, *args):
            if value == "print $esp": return "0x1000"
            if value == "print $ebp": return "0x2000"
            if value == "print $ebx": return "0x2b"
            if value == "cont": return f"Stopped at{next(stops):08x}"
            return ""
        debugger.command.side_effect = command
        debugger.word.return_value = 0x446b24
        debugger.memory.side_effect = lambda address, size: (struct.pack("<7I", 0x44c5a3, TRACE.LAYOUT["body_base"] + 0xf0, 7, 2, 0x4ef2f0, 0, 0)
                                                             if address == 0x1000 else struct.pack("<2I", 1, 1))
        before = {"rng": 123, "mask": 0, "reset": 0}
        after = {"rng": 456, "mask": 1, "reset": 0}
        with patch.object(TRACE.FLEET, "capture_skid_state", side_effect=[{}, {}]), patch.object(TRACE, "skid_call_state", side_effect=[before, after]):
            result = TRACE.capture_skid_dispatch(debugger)
        event = result["calls"][0]
        self.assertEqual((event["actor"], event["wheel"], event["kind"]), (0, 0, 7))
        self.assertEqual(event["sample"], {"pc": 0x424eb2, "member": 43, "restart": 1, "fade": 1})
        self.assertEqual(event["post"], after)
        self.assertIn(call("delete break 4507043"), debugger.command.call_args_list)
