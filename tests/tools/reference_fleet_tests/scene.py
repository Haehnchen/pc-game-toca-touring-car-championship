"""Joined cold physical/scene provenance, input exclusion and terminal clocks."""

from .support import (
    TraceTestCase,
    CLI,
    FLEET,
    copy,
    io,
    json,
    patch,
    state,
    struct,
    subprocess,
    sys,
)
from .audio import WhineTests, WheelQueueTests
from .clock import ColdLocalLifecycleTests, ColdTimerCreationTests
from .physics import DispatchFeedbackTests
from .retained_session import WarmRestartInputTests
from .startup_join import StartupFirstSelectionTests
from .startup_physics import StartupContactCacheTests, StartupWorldResetTests


class AutonomousSceneTests(TraceTestCase):
    change = staticmethod(StartupFirstSelectionTests.change)
    change_cache = staticmethod(StartupContactCacheTests.change_cache)
    trace_filename = 'driver'
    fixture = StartupWorldResetTests.fixture
    flags = DispatchFeedbackTests.flags

    def binaries(self, header, records):
        fleet, physical, journal = (self.path.with_suffix(suffix) for suffix in (".fleet", ".physical", ".journal"))
        with patch.object(FLEET, "scene_autonomous_enabled", return_value=True), patch.object(FLEET, "validate_startup_first_selection_scope", return_value={"player_name": b"PLAYER 1", "car": 0}), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=True,
                startup_join=True, startup_first_selection=True, startup_physical_body=True, startup_control_calibration=True,
                startup_contact_cache=True, startup_world_reset=True, scene_owned=True, **self.flags)
            FLEET.write_binary(physical, header, records, driver_physics_inputs=True, startup_physical_body=True,
                startup_control_calibration=True, startup_contact_cache=True, startup_world_reset=True, scene_owned=True, **self.flags)
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True,
                startup_join=True, startup_physical_body=True, startup_world_reset=True, scene_owned=True, **self.flags)
        return fleet.read_bytes(), physical.read_bytes(), journal.read_bytes()

    def test_owned_scene_observations_never_enter_either_flet_or_wheel_stream(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for snapshot in record.values():
                if not isinstance(snapshot, dict) or "actors" not in snapshot: continue
                shared = snapshot["shared"]
                shared.update(carcontacts_enabled=99, sector_visibility=bytes([0xa5]) * len(shared["sector_visibility"]), sector_visibility_disabled=1, sector_visibility_override=1)
                for actor in snapshot["actors"]:
                    actor["root"].update(sector=123, neighbors=[124, 125, 126], detail=7, sector_visible=True)
                    actor["effect_root"]["flags"] = bytes([0xa5]) * 4
                    raw = bytearray(actor["body"]); struct.pack_into("<H", raw, 0x4ec, 127)
                    actor.update(body=bytes(raw), body_hex=raw.hex())
            for entry in record["combined_journal"]:
                if entry["kind"] == "wheel": entry["event"]["pre"]["visibility_hex"] = bytes([0xa5] * 16).hex()
        self.assertEqual(before, self.binaries(header, records))
        with self.assertRaisesRegex(ValueError, "joined autonomous cold"):
            FLEET.write_binary(self.path, header, records, scene_owned=True)

    def test_cold_scene_clock_uses_creator_and_terminal_sample_without_warm_restart(self):
        owner = ColdTimerCreationTests(); header, records = owner.fixture()
        header.pop("warm_restart_manifest")
        header.update(scene_autonomous_manifest=FLEET.SCENE_AUTONOMOUS_MANIFEST, scene_capture_id="one-source")
        before = {name: records[-1]["pre"][name] for name in FLEET.QPC_SAMPLE_FIELDS}
        after = {**before, "qpc_low": 1000, "qpc_difference": 100, "delta_q16": 65536, "reciprocal_q16": 65536, "extra_update": False}
        records[-1]["scene_final_journal"] = [owner.sample(before, after, 3)]
        self.assertTrue(FLEET.ordered_clock_enabled(header))
        inputs = FLEET.startup_clock_inputs(header, records)
        self.assertEqual(inputs["operations"][-1], (len(records), 3, 1, 1000))
        broken = copy.deepcopy(records)
        broken[-1]["scene_final_journal"][0]["event"]["post"]["delta_q16"] += 1
        with self.assertRaisesRegex(ValueError, "terminal sample POST"):
            FLEET.startup_clock_inputs(header, broken)
        records[-1].pop("scene_final_journal")
        with self.assertRaisesRegex(ValueError, "terminal QPC"):
            FLEET.startup_clock_inputs(header, records)
        header.pop("scene_capture_id")
        with self.assertRaisesRegex(ValueError, "complete cold ordinary"):
            FLEET.ordered_clock_enabled(header)

    def test_terminal_audio_callbacks_follow_physical_checkpoints_without_weakening_them(self):
        header, all_records = WhineTests().fixture()
        tail = all_records[3]["whine_callbacks"][0]
        tail["order"] = 17
        records = all_records[:3]
        records[-1]["whine_callbacks"].append(tail)
        records[-1]["scene_final_journal"] = [{"kind": "motor", "event": tail}]
        self.assertEqual(FLEET.validate_whine_scope(header, records), (3, 3, 3))
        broken = copy.deepcopy(records)
        broken[-1]["whine_update_post"]["state"]["phase"] += 1
        with self.assertRaisesRegex(ValueError, "outside actual frame callbacks"):
            FLEET.validate_whine_scope(header, broken)
        header, all_records = WheelQueueTests().fixture()
        tail = all_records[3]["wheel_queue_callbacks"][0]
        tail["order"] = 17
        records = all_records[:3]
        records[-1]["wheel_queue_callbacks"].append(tail)
        records[-1]["scene_final_journal"] = [{"kind": "wheel", "event": tail}]
        self.assertEqual(FLEET.validate_wheel_queue_scope(header, records), (3, 3))
        raw = bytearray.fromhex(records[-1]["pre"]["actors"][0]["dynamics_hex"])
        struct.pack_into("<i", raw, 0x2c4, 999)
        records[-1]["pre"]["actors"][0]["dynamics_hex"] = raw.hex()
        with self.assertRaisesRegex(ValueError, "following update PRE"):
            FLEET.validate_wheel_queue_scope(header, records)

    def test_cli_uses_scene_owned_streams_and_gates_parity_on_scene_results(self):
        from contextlib import ExitStack
        header, records = self.fixture()
        self.path.write_text(json.dumps({"prefix_crc32": "12345678"}) + "\n")
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-scene", "--output", str(self.path.parent)]
        comparison = {"parity": False, "numerical_parity": True, "physical_parity": True, "continuous_physics_parity": True,
                      "mismatch_count": 0, "exact_prefix_updates": len(records), "combined": {"parity": False}}
        for scene_ok in (True, False):
            with ExitStack() as stack:
                stack.enter_context(patch.object(sys, "argv", argv))
                stack.enter_context(patch.object(CLI.FLEET, "read_trace", return_value=(header, records)))
                stack.enter_context(patch.object(CLI.FLEET, "scene_autonomous_enabled", return_value=True))
                stack.enter_context(patch.object(CLI.FLEET, "cold_local_session_enabled", return_value=False))
                fleet = stack.enter_context(patch.object(CLI.FLEET, "write_binary"))
                journal = stack.enter_context(patch.object(CLI.FLEET, "write_combined_binary"))
                for name in ("write_startup_join_binary", "write_startup_clock_binary"):
                    stack.enter_context(patch.object(CLI.FLEET, name))
                stack.enter_context(patch.object(CLI.FLEET, "compare_driver_physics", return_value=comparison))
                for name in ("compare_cold_grid_sequence", "compare_startup_first_selection", "compare_startup_physical_body", "compare_startup_control_calibration", "compare_startup_contact_cache", "compare_startup_world_reset", "compare_startup_clock"):
                    stack.enter_context(patch.object(CLI.FLEET, name, return_value={"parity": True, "remaining_initial_context": "observed", "remaining_context": "observed"}))
                for name in ("startup_join_coverage", "startup_physical_body_coverage"):
                    stack.enter_context(patch.object(CLI.FLEET, name, return_value={"sufficient": True}))
                stack.enter_context(patch.object(CLI.SCENE, "read", return_value=({}, [], {"prefix_crc32": "aabbccdd", "coverage": {"passed": True}})))
                stack.enter_context(patch.object(CLI.SCENE.FRAMES, "read_frames", return_value=({}, [], {"prefix_crc32": "11223344", "coverage": {"passed": True}})))
                stack.enter_context(patch.object(CLI.SCENE.FRAMES, "validate_autonomous_join", return_value={"frames": 3}))
                stack.enter_context(patch.object(CLI, "compare_scene_rows", return_value={"numerical_parity": scene_ok, "compared_values": 10}))
                execute = stack.enter_context(patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")))
                stack.enter_context(patch("sys.stdout", new=io.StringIO()))
                self.assertEqual(CLI.main(), 0 if scene_ok else 1)
            self.assertEqual(execute.call_args.args[0][1], "--compare-startup-scene")
            self.assertEqual(len(execute.call_args.args[0]), 10)
            self.assertEqual(len(fleet.call_args_list), 2)
            self.assertTrue(all(call.kwargs["scene_owned"] for call in fleet.call_args_list))
            self.assertTrue(journal.call_args.kwargs["scene_owned"])
            report = json.loads((self.path.parent / "fleet-comparison-startup-scene.json").read_text())
            self.assertEqual(report["parity"], scene_ok)
            self.assertEqual(report["scene_input_imports"], 0)
            self.assertEqual(report["audio_listener_context"], "observed existing contact/audio boundary")

    def test_final_callbacks_remain_after_physical_post_and_comparator_rejects_missing_rows(self):
        _, records = self.fixture()
        record = records[-1]
        event = {"kind": "qpc_sample", "event": {"counter": 99}}
        record["scene_final_journal"] = [event]
        events = FLEET.combined_events(record)
        self.assertEqual(events[-2:], [("boundary", "post", record["post"]), ("qpc_sample", None, event["event"])])
        self.path.write_text("order,group,index,field,value\n0,camera,0,world_bits,123\n")
        wanted = [(0, "camera", 0, "world_bits", 123)]
        self.assertTrue(CLI.compare_scene_rows(wanted, self.path)["numerical_parity"])
        self.assertFalse(CLI.compare_scene_rows([(0, "camera", 0, "world_bits", 124)], self.path)["numerical_parity"])
        with self.assertRaisesRegex(ValueError, "Missing autonomous scene"):
            CLI.compare_scene_rows([*wanted, *wanted], self.path)


class OrdinaryCameraWireTests(TraceTestCase):
    trace_filename = 'driver'
    flags = DispatchFeedbackTests.flags

    def fixture(self):
        owner = ColdLocalLifecycleTests(); owner.path = self.path
        header, records = owner.fixture()
        header.update(cold_timer_creation_manifest=FLEET.COLD_TIMER_CREATION_MANIFEST,
            cold_grid_sequence_manifest=FLEET.COLD_GRID_SEQUENCE_MANIFEST,
            qpc_sample_manifest=FLEET.QPC_SAMPLE_MANIFEST, scene_autonomous_manifest=FLEET.SCENE_AUTONOMOUS_MANIFEST,
            scene_capture_id="camera-run", scene_camera_controls_manifest=FLEET.SCENE_CAMERA_CONTROLS_MANIFEST, scenario="camera-cycle")
        header["cold_grid_sequence"]["timer_creation"] = ColdTimerCreationTests.event()
        for record in records:
            record["camera_input"] = {"physical_id": record["id"], "cycle_held": record["id"] == 1, "rear_held": False}
        return header, records

    def binary(self, header, records):
        with patch.object(FLEET, "validate_startup_first_selection_scope"), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(self.path, header, records, compare_driver_retention=True,
                startup_join=True, startup_physical_body=True, startup_world_reset=True, scene_owned=True, **self.flags)
        return self.path.read_bytes()

    def test_v9_places_requested_keys_after_ai_and_strips_observed_material_view(self):
        header, records = self.fixture()
        before = self.binary(header, records)
        self.assertEqual(struct.unpack_from("<I", before, 8)[0], 9)
        self.assertIn(struct.pack("<4I", 18, 1, 1, 0), before)
        kinds = [kind for kind, _, _ in FLEET.combined_events(records[1])]
        location = kinds.index("camera_input")
        self.assertEqual(kinds[location - 1:location + 2], ["ai", "camera_input", "boundary"])
        for record in records:
            for call in record["surface_calls"]:
                call["pre"]["camera_views_hex"] = struct.pack("<4h", 5, 4, 3, 1).hex()
        self.assertEqual(before, self.binary(header, records))
        records[1]["camera_input"]["cycle_held"] = False
        changed = self.binary(header, records)
        self.assertNotEqual(before, changed)
        self.assertEqual(len(before), len(changed))
        with self.assertRaisesRegex(ValueError, "requested keys only"):
            FLEET.validate_camera_input({**records[1]["camera_input"], "selected_view": 2}, 1)

    def test_mode0_ignores_boolean_time_trial_best_lap_option_but_rejects_other_values(self):
        owner = ColdLocalLifecycleTests(); owner.path = self.path
        header, records = owner.fixture()
        before = owner.binaries(header, records)
        raw = bytearray.fromhex(header["configuration_hex"])
        struct.pack_into("<I", raw, 0x30, 1)
        header["configuration_hex"] = raw.hex()
        self.assertEqual(owner.binaries(header, records), before)
        with patch.object(FLEET, "validate_joined_ai_scope"):
            self.assertIs(FLEET.validate_retained_race_timing_scope(header, records), records[0]["ai_pre"])
            struct.pack_into("<I", raw, 0x30, 2)
            header["configuration_hex"] = raw.hex()
            with self.assertRaisesRegex(ValueError, "Time Trial best-lap option"):
                FLEET.validate_retained_race_timing_scope(header, records)
            struct.pack_into("<I", raw, 0x30, 1)
            header["configuration_hex"] = raw.hex()
            header["source_mode"] = 2
            with self.assertRaisesRegex(ValueError, "ordinary active"):
                FLEET.validate_retained_race_timing_scope(header, records)

    def test_requested_control_timeline_rejects_poll_lag_and_selected_view_changes(self):
        state = {"selected_view": 2, "projection_view": 2, "rear_held": 0, "saved_view": 0}
        last_cycle = False
        records = []
        for index in range(8):
            cycle = index in (1, 2, 4, 6)
            rear = 3 <= index <= 4
            pressed = cycle and not last_cycle
            before, after = dict(state), dict(state)
            if pressed and not before["rear_held"]:
                after["selected_view"] = before["selected_view"] % 4 + 1
                after["projection_view"] = after["selected_view"]
            if rear and not before["rear_held"]:
                after.update(saved_view=after["selected_view"], selected_view=5, rear_held=1)
            elif not rear and before["rear_held"]:
                after.update(selected_view=before["saved_view"], rear_held=0)
            records.append({"id": index, "camera_input": {"physical_id": index, "cycle_held": cycle, "rear_held": rear},
                "camera_control_expected": {"cycle_pressed": pressed, "actions": {"cycle_pressed": pressed, "rear_held": rear}, "pre": before, "post": after}})
            state, last_cycle = after, cycle
        self.assertTrue(FLEET.validate_camera_controls(records))
        changed = copy.deepcopy(records)
        changed[1]["camera_control_expected"]["actions"]["cycle_pressed"] = False
        with self.assertRaisesRegex(ValueError, "cached mapper actions"):
            FLEET.validate_camera_controls(changed)
        changed = copy.deepcopy(records)
        changed[4]["camera_control_expected"]["post"]["selected_view"] = 1
        with self.assertRaisesRegex(ValueError, "requested C/Z"):
            FLEET.validate_camera_controls(changed)


class SceneLifecycleConsumerTests(TraceTestCase):
    trace_filename = 'driver'

    def fixture(self):
        warm = WarmRestartInputTests(); warm.path = self.path
        header, records = warm.fixture()
        cold = ColdLocalLifecycleTests(); cold.path = self.path
        cold_header, cold_records = cold.fixture()
        header.update(cold_header)
        header.update(warm_restart_manifest=FLEET.WARM_RESTART_MANIFEST,
            cold_timer_creation_manifest=FLEET.COLD_TIMER_CREATION_MANIFEST,
            cold_grid_sequence_manifest=FLEET.COLD_GRID_SEQUENCE_MANIFEST,
            qpc_sample_manifest=FLEET.QPC_SAMPLE_MANIFEST, qualifying=False, scenario="local-lap")
        header["cold_grid_sequence"]["timer_creation"] = ColdTimerCreationTests.event()
        records.insert(0, cold_records[0])
        active = copy.deepcopy(records[-1])
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            active[phase]["session_state"] = 6
        records.append(active)
        for index, record in enumerate(records): record["id"] = index
        return header, records

    def test_existing_one_run_lifecycle_requires_finish_results_restart_and_renewed_green(self):
        header, records = self.fixture()
        original = copy.deepcopy(header)
        with patch.object(FLEET, "startup_clock_inputs", return_value={"coverage": {"menu_only_primary_frames": 1}}):
            result = FLEET.validate_scene_lifecycle_scope(header, records)
            self.assertTrue(result["sufficient"])
            self.assertEqual(result["session_states"], [6, 9, 10, 13])
            self.assertEqual(result["warm_active_update_ids"], [9])
            self.assertIsNone(result["camera_matrix_parity"])
            self.assertIsNone(result["ordered_pvs_parity"])
            self.assertNotIn("scene_final_journal", records[-1])
            with self.assertRaisesRegex(ValueError, "renewed green"):
                FLEET.validate_scene_lifecycle_scope(header, records[:-1])
        self.assertEqual(header, original)
        for name in ("warm_restart_manifest", "cold_timer_creation_manifest", "qpc_sample_manifest"):
            invalid = copy.deepcopy(header); invalid.pop(name)
            with self.subTest(name=name), self.assertRaises(ValueError):
                FLEET.validate_scene_lifecycle_scope(invalid, records)

    def test_consumer_rows_preserve_actual_wheel_and_pre_order_and_raw_root_bits(self):
        snapshot = state("421aa0-entry")
        raw = bytearray.fromhex(snapshot["actors"][0]["root"]["registration_hex"])
        raw[0x18], raw[0x20] = 0x0d, 0x0e
        snapshot["actors"][0]["root"]["registration_hex"] = raw.hex()
        snapshot["actors"][0]["effect_root"]["flags_hex"] = "03000000"
        snapshot["shared"].update(sector_visibility_hex="81" + "00" * 255,
            sector_visibility_disabled=3, sector_visibility_override=0)
        wheel = {"pre": {"visibility_hex": bytes([15, 14] * 8).hex()}}
        events = [("wheel", None, wheel), ("boundary", "pre", snapshot), ("wheel", None, wheel)]
        records = [{"id": 7}]
        rows = list(CLI.SCENE.FRAMES.expected_lifecycle_consumer_rows(records, lambda _: events))
        self.assertEqual(rows[0], (7, "wheel_full", 0, "mask", 1))
        self.assertEqual(rows[1], (7, "wheel_full", 1, "mask", 0))
        self.assertEqual(rows[16], (7, "pre_root", 0, "sector", 0))
        self.assertIn((7, "pre_root", 0, "render_mask", 1), rows)
        self.assertIn((7, "pre_root", 0, "manual_mask", 0), rows)
        self.assertIn((7, "pre_root", 0, "effect_mask", 1), rows)
        self.assertIn((7, "pre_pvs", 0, "bits", 129), rows)
        self.assertIn((7, "pre_scene", 0, "selected_count", 3), rows)
        self.assertEqual(rows[-16], (7, "wheel_full", 16, "mask", 1))
        self.path.write_text("order,group,index,field,value\n" + "".join(",".join(map(str, row)) + "\n" for row in rows))
        self.assertTrue(CLI.compare_scene_rows(iter(rows), self.path)["numerical_parity"])
        changed = list(rows); changed[0] = (*changed[0][:-1], 0)
        self.assertFalse(CLI.compare_scene_rows(iter(changed), self.path)["numerical_parity"])
        with self.assertRaisesRegex(ValueError, "Missing autonomous scene"):
            CLI.compare_scene_rows(iter([*rows, rows[-1]]), self.path)

    def test_owned_scene_material_view_is_generated_even_without_camera_keys(self):
        owner = AutonomousSceneTests(); owner.path = self.path
        header, records = owner.fixture()
        before = owner.binaries(header, records)
        for record in records:
            for call in record["surface_calls"]:
                call["pre"]["camera_views_hex"] = struct.pack("<4h", 5, 4, 3, 1).hex()
        self.assertEqual(owner.binaries(header, records), before)

    def test_lifecycle_cli_rejects_prefixes_and_scene_sidecars_before_native_launch(self):
        for extra in (["--updates", "3"], ["--scene-journal", "/tmp/other-run.jsonl"], ["--compare-startup-scene"]):
            with self.subTest(extra=extra), patch.object(sys, "argv", ["replay-fleet-reference.py", str(self.path),
                    "--compare-startup-scene-lifecycle", *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit): CLI.main()

    def test_cli_uses_existing_source_consumers_and_propagates_comparison_failure(self):
        from contextlib import ExitStack
        header, records = self.fixture()
        self.path.write_text('{"type":"complete","prefix_crc32":"12345678"}\n')
        local = {"coverage_sufficient": True, "targeted_warm_restart_required": True,
                 "targeted_warm_restart_observed": True, "coverage": {}}
        comparison = {"parity": False, "numerical_parity": True, "physical_parity": True,
            "continuous_physics_parity": True, "mismatch_count": 0, "exact_prefix_updates": len(records),
            "combined": {"parity": False, "local_session": local}}
        for numerical in (True, False):
            with ExitStack() as stack:
                stack.enter_context(patch.object(sys, "argv", ["replay-fleet-reference.py", str(self.path),
                    "--compare-startup-scene-lifecycle", "--output", str(self.path.parent)]))
                stack.enter_context(patch.object(CLI.FLEET, "read_trace", return_value=(header, records)))
                stack.enter_context(patch.object(CLI.FLEET, "validate_scene_lifecycle_scope", return_value={"sufficient": True}))
                for name in ("write_binary", "write_combined_binary", "write_startup_join_binary", "write_startup_clock_binary"):
                    stack.enter_context(patch.object(CLI.FLEET, name))
                stack.enter_context(patch.object(CLI.FLEET, "compare_driver_physics", return_value=comparison))
                for name in ("compare_cold_grid_sequence", "compare_startup_first_selection", "compare_startup_physical_body",
                        "compare_startup_control_calibration", "compare_startup_contact_cache", "compare_startup_world_reset", "compare_startup_clock"):
                    stack.enter_context(patch.object(CLI.FLEET, name, return_value={"parity": True,
                        "remaining_initial_context": "observed", "remaining_context": "observed"}))
                for name in ("startup_join_coverage", "startup_physical_body_coverage"):
                    stack.enter_context(patch.object(CLI.FLEET, name, return_value={"sufficient": True}))
                sidecar = stack.enter_context(patch.object(CLI.SCENE, "read", side_effect=AssertionError("No other-run scene sidecars")))
                stack.enter_context(patch.object(CLI, "compare_scene_rows", return_value={"numerical_parity": numerical,
                    "compared_values": 12, "mismatch_count": int(not numerical)}))
                execute = stack.enter_context(patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")))
                stack.enter_context(patch("sys.stdout", new=io.StringIO()))
                self.assertEqual(CLI.main(), 0 if numerical else 1)
            self.assertEqual(execute.call_args.args[0][1], "--compare-startup-scene-lifecycle")
            self.assertEqual(len(execute.call_args.args[0]), 10)
            sidecar.assert_not_called()
            report = json.loads((self.path.parent / "fleet-comparison-startup-scene-lifecycle.json").read_text())
            self.assertEqual(report["parity"], numerical)
            self.assertEqual(report["scene_source_prefix_crc32"], "12345678")
            self.assertEqual(report["scene_input_imports"], 0)
            self.assertIsNone(report["camera_matrix_parity"])
