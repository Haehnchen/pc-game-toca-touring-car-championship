"""Fleet reference checks for session."""

from .support import (
    CLI,
    FLEET,
    FleetTestCase,
    Path,
    ai_fixture,
    csv,
    finish_fixture,
    fixture,
    io,
    json,
    launch_state,
    patch,
    state,
    struct,
    subprocess,
    sys,
    timing_fixture,
    write_trace,
)


class SessionBoundaryTests(FleetTestCase):
    def test_session_binary_imports_pre_only_with_exact848_and104_byte_blocks(self):
        header, records = self.session_fixture()
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_session_boundaries=True)
        raw = binary.read_bytes()
        self.assertEqual(struct.unpack_from("<5I", raw, 32), (1, 1, 1, 0, 1))
        initial = 12 + 0x540 + 4100 + 2 * 396
        offset = 64 + initial + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 5576 + 600 + 372
        self.assertEqual(struct.unpack_from("<I", raw, offset)[0], 1)
        self.assertEqual(raw[offset + 4:offset + 4 + 0x338], bytes.fromhex(records[0]["ai_pre"]["dynamics_hex"]))
        self.assertEqual(raw[offset + 848:offset + 848 + 104], b"".join(records[0]["ai_pre"]["shared"]["session_boundary"][name] for name in FLEET.SESSION_TYPED_FIELDS))
        native = self.session_csv(records)
        self.assertTrue(FLEET.compare_session_boundaries(header, records, native)["parity"])
        post = records[1]["ai_post"]["shared"]["session_boundary"]
        tail = bytearray.fromhex(post["configuration_tail_hex"])
        struct.pack_into("<h", tail, 2, 77)
        post["configuration_tail_hex"] = tail.hex()
        FLEET.write_binary(binary, header, records, compare_session_boundaries=True)
        self.assertEqual(binary.read_bytes(), raw)
        report = FLEET.compare_session_boundaries(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], "session-classification.score_0")

    def test_session_sidecar_requires_chronology_and_retains_race_stop_word(self):
        header, records = self.session_fixture()
        snapshot = records[0]["ai_post"]
        snapshot["shared"]["finish_timing"]["race_status_hex"] = bytes([0, 0, 0, 1]).hex()
        self.assertEqual(FLEET.expected_finish(snapshot)["race_stop"], 256)
        native = self.session_csv(records)
        self.assertTrue(FLEET.compare_session_boundaries(header, records, native)["parity"])
        path = Path(str(native) + ".session-controller.csv")
        with path.open(newline="") as stream:
            reader = csv.DictReader(stream)
            columns, rows = reader.fieldnames, list(reader)
        rows.reverse()
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)
        with self.assertRaisesRegex(ValueError, "Out-of-order"):
            FLEET.compare_session_boundaries(header, records, native)

    def test_session_base_qualifying_profile_compares_positive_display_without_classification_claim(self):
        header, records = self.session_fixture(False)
        raw_config = bytearray.fromhex(header["configuration_hex"])
        struct.pack_into("<I", raw_config, 0x20, 0)
        header["configuration_hex"] = raw_config.hex()
        header["qualifying"] = True
        for record in records:
            for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
                noqual = bytearray.fromhex(record[name]["shared"]["provenance"]["noqual_flags_hex"])
                struct.pack_into("<I", noqual, 0, 1)
                record[name]["shared"]["provenance"]["noqual_flags_hex"] = noqual.hex()
        post = records[1]["ai_post"]["shared"]["finish_timing"]
        post["display_active_hex"] = struct.pack("<4i", 1, 0, 0, 0).hex()
        post["display_hex"] = (b"+01:05.80\0" + bytes(54)).hex()
        native = self.session_csv(records)
        with self.assertRaisesRegex(ValueError, "NoQual"):
            FLEET.validate_ai_route_scope(header, records)
        report = FLEET.compare_session_boundaries(header, records, native)
        self.assertTrue(report["parity"])
        self.assertTrue(report["original_player_split_display_parity"])
        self.assertEqual(report["coverage"]["extended_provenance_updates"], 0)
        self.assertIsNone(report["original_classification_parity"])
        self.assertIsNone(report["physical_parity"])
        self.assertIsNone(report["rng_parity"])
        #Supported subsets may be exact, but all-skipped must never pass.
        native = self.session_csv(records, (0, 1))
        report = FLEET.compare_session_boundaries(header, records, native)
        self.assertFalse(report["parity"])
        self.assertIsNone(report["supported_parity"])
        with Path(str(native) + ".session-unsupported.csv").open("a") as stream: stream.write("1,duplicate\n")
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_session_boundaries(header, records, native)

    def test_session_only_cli_skips_physics_and_preserves_null_parity_scopes(self):
        #read_trace consumes JSON, never the already-decoded synthetic object.
        header, raw = finish_fixture(1)
        write_trace(self.path, header, raw)
        output = self.path.parent / "session-only"
        session = {"parity": True, "exact_prefix_updates": 1, "supported_updates": 1, "mismatch_count": 0, "rng_parity": None}
        argv = ["replay-fleet-reference.py", str(self.path), "--output", str(output), "--compare-session-boundaries"]
        with patch.object(sys, "argv", argv), patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as native, patch.object(CLI.FLEET, "compare_session_boundaries", return_value=session), patch.object(CLI.FLEET, "compare", side_effect=AssertionError("physical comparison executed")), patch.object(CLI.FLEET, "validate_replay_scope", side_effect=AssertionError("physical scope executed")), patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertIn("--compare-session-boundaries", native.call_args.args[0])
        report = json.loads((output / "fleet-comparison-continuous-physics-inputs.json").read_text())
        self.assertIsNone(report["physical_parity"])
        self.assertIsNone(report["autonomous_ai_parity"])
        self.assertTrue(report["session_boundaries"]["parity"])

    def test_session_boundary_profile_is_optional_and_requires_complete_live_provenance(self):
        header, records = finish_fixture(1)
        header.update(local_handoff_manifest=FLEET.LOCAL_HANDOFF_MANIFEST, session_boundary_manifest=FLEET.SESSION_BOUNDARY_MANIFEST)
        records[0]["local_handoffs"] = []
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            records[0][name]["shared"]["session_boundary"] = {**{key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.SESSION_BOUNDARY_BLOCKS.items()}, "configuration_tail_hex": bytes(0x30).hex()}
        write_trace(self.path, header, records)
        _, parsed = FLEET.read_trace(self.path)
        self.assertEqual(len(parsed[0]["ai_pre"]["shared"]["session_boundary"]["configuration_tail"]), 0x30)
        records[0]["ground"]["shared"].pop("session_boundary")
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "Session boundary state"):
            FLEET.read_trace(self.path)
        header.pop("session_boundary_manifest")
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "Session boundary state"):
            FLEET.read_trace(self.path)


class LocalHandoffTests(FleetTestCase):
    def test_handoff_binary_pre_only_and_corrupt_post_is_detected(self):
        header, records = self.handoff_fixture()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_local_handoffs=True)
        original = binary.read_bytes()
        self.assertEqual(struct.unpack_from("<4I", original, 32), (0, 0, 0, 1))
        offset = 64 + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES
        self.assertEqual(struct.unpack_from("<I4Ii", original, offset), (1, 1, 0, 0x421827, 123, 777))
        self.assertEqual(struct.unpack_from("<IIIiII16i", original, offset + 24),
                         tuple(FLEET.local_handoff_values(records[0]["local_handoffs"][0]["pre"]).values()))
        csv_path = self.path.with_suffix(".handoffs.csv")
        self.write_handoff_csv(records, csv_path)
        self.assertTrue(FLEET.compare_local_handoffs(header, records, csv_path)["parity"])
        post = records[0]["local_handoffs"][0]["post"]
        raw = bytearray.fromhex(post["dynamics_hex"])
        struct.pack_into("<i", raw, 0xec, 0xccd)
        post["dynamics_hex"] = raw.hex()
        FLEET.write_binary(binary, header, records, compare_local_handoffs=True)
        self.assertEqual(binary.read_bytes(), original)
        report = FLEET.compare_local_handoffs(header, records, csv_path)
        self.assertEqual(report["mismatch_count"], 1)
        self.assertEqual(report["mismatches"][0]["field"], "tire_offset_4")

    def test_handoff_mode_labels_follow_source_steering_throttle_brake_order(self):
        _, records = self.handoff_fixture()
        snapshot = records[0]["local_handoffs"][0]["pre"]
        dynamics = bytearray.fromhex(snapshot["dynamics_hex"])
        gameplay = bytearray.fromhex(snapshot["gameplay_hex"])
        struct.pack_into("<3i", dynamics, 0x310, 11, 22, 33)
        struct.pack_into("<3i", gameplay, 0x60, 44, 55, 66)
        snapshot.update(dynamics_hex=dynamics.hex(), gameplay_hex=gameplay.hex())
        values = FLEET.local_handoff_values(snapshot)
        self.assertEqual(tuple(values[name] for name in ("mode_steering", "mode_throttle", "mode_brake")), (11, 22, 33))
        self.assertEqual(tuple(values[name] for name in ("saved_steering", "saved_throttle", "saved_brake")), (44, 55, 66))

    def test_restore_player_clears_only_queued_shift_low_byte(self):
        header, records = self.handoff_fixture()
        event = records[0]["local_handoffs"][0]
        for phase, queued in (("pre", 0x12345678), ("post", 0x12345600)):
            dynamics = bytearray.fromhex(event[phase]["dynamics_hex"])
            struct.pack_into("<I", dynamics, 0x14c, queued)
            event[phase]["dynamics_hex"] = dynamics.hex()
        write_trace(self.path, header, records)
        _, parsed = FLEET.read_trace(self.path)
        self.assertEqual(FLEET.local_handoff_values(parsed[0]["local_handoffs"][0]["post"])["queued_shift"], 0x12345600)
        dynamics = bytearray.fromhex(event["post"]["dynamics_hex"])
        #402ce2 is a byte write: clearing any retained upper byte is invalid.
        dynamics[0x14d] = 0
        event["post"]["dynamics_hex"] = dynamics.hex()
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "retained raw controller call change"):
            FLEET.read_trace(self.path)

    def test_handoff_qualifying_requires_manifest_real_menu_and_exact_call_provenance(self):
        header, records = self.handoff_fixture()
        for field, replacement, message in (("return_address", 0x421828, "caller"), ("actor", 1, "actor"), ("stack_hex", bytes(8).hex(), "stack")):
            event = records[0]["local_handoffs"][0]
            original = event[field]
            event[field] = replacement
            write_trace(self.path, header, records)
            with self.assertRaisesRegex(ValueError, message): FLEET.read_trace(self.path)
            event[field] = original
        post = records[0]["local_handoffs"][0]["post"]
        original = post["dynamics_hex"]
        raw = bytearray.fromhex(original)
        raw[0x138] = 1  #Later421390 steering clear is outside402c80.
        post["dynamics_hex"] = raw.hex()
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "retained raw"): FLEET.read_trace(self.path)
        post["dynamics_hex"] = original
        header.pop("local_handoff_manifest")
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "Qualifying fleet"): FLEET.read_trace(self.path)
        header["local_handoff_manifest"] = FLEET.LOCAL_HANDOFF_MANIFEST
        config = bytearray.fromhex(header["configuration_hex"])
        struct.pack_into("<I", config, 0x20, 1)
        header["configuration_hex"] = config.hex()
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "select actual"): FLEET.read_trace(self.path)

    def test_handoff_only_cli_does_not_claim_or_execute_physical_comparison(self):
        header, records = self.handoff_fixture()
        write_trace(self.path, header, records)
        output = self.path.parent / "handoff-only"
        output.mkdir()
        native = output / "fleet-native-continuous-physics-inputs.csv"
        self.write_handoff_csv(records, Path(str(native) + ".local-handoffs.csv"))
        argv = ["replay-fleet-reference.py", str(self.path), "--output", str(output), "--compare-local-handoffs", "--local-handoffs-only"]
        with patch.object(sys, "argv", argv), patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as native_run, patch.object(CLI.FLEET, "compare", side_effect=AssertionError("physical comparison executed")), patch.object(CLI.FLEET, "validate_replay_scope", side_effect=AssertionError("physical validation executed")), patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertIn("--local-handoffs-only", native_run.call_args.args[0])
        report = json.loads((output / "fleet-comparison-continuous-physics-inputs.json").read_text())
        self.assertIsNone(report["physical_parity"])
        self.assertTrue(report["local_controller_transaction_parity"])
        self.assertEqual(report["local_handoffs"]["event_count"], 1)
        records[0]["local_handoffs"] = []
        self.write_handoff_csv(records, Path(str(native) + ".local-handoffs.csv"))
        self.assertIsNone(FLEET.compare_local_handoffs(header, records, Path(str(native) + ".local-handoffs.csv"))["observed_event_parity"])
        self.assertFalse(FLEET.compare_local_handoffs(header, records, Path(str(native) + ".local-handoffs.csv"))["parity"])

    def test_first_ai_finisher_counter_reset_requires_complete_source_provenance(self):
        header, records = finish_fixture(1)
        record = records[0]
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            snap = record[name]
            world = bytearray.fromhex(snap["shared"]["provenance"]["world_status_hex"])
            struct.pack_into("<H", world, 0x14, 1)
            snap["shared"]["provenance"]["world_status_hex"] = world.hex()
            after = name not in ("pre", "ai_pre")
            game = bytearray.fromhex(snap["actors"][5]["gameplay_hex"])
            game[0x80], game[0x8d] = 0xff, 5
            struct.pack_into("<I", game, 0x54, int(after))
            struct.pack_into("<h", game, 0xa8, 2 if after else 1)
            snap["actors"][5]["gameplay_hex"] = game.hex()
            if after:
                snap["update_counter"] = 0
                finish = snap["shared"]["finish_timing"]
                finish["finish_counts_hex"] = struct.pack("<2I", 1, 0).hex()
                finish["finish_order_hex"] = bytes([5] + [0] * 15).hex()
                finish["race_counters_hex"] = struct.pack("<4I", 0, 0, snap["race_clock"], 1000).hex()
        self.assertTrue(FLEET.validate_update_counters(record, True))
        write_trace(self.path, header, records)
        _, parsed = FLEET.read_trace(self.path)
        self.assertEqual(parsed[0]["consumed"]["update_counter"], 0)
        with self.assertRaisesRegex(ValueError, "Missed source"):
            FLEET.validate_update_counters(record, False)
        record["ai_post"]["shared"]["finish_timing"]["finish_order_hex"] = bytes(16).hex()
        with self.assertRaisesRegex(ValueError, "first-finisher"):
            FLEET.validate_update_counters(record, True)


class RaceTimingTests(FleetTestCase):
    def test_finish_binary_uses_only_pre_globals_and_retains_exact_372_byte_layout(self):
        header, records = finish_fixture(1)
        pre = records[0]["ai_pre"]["shared"]["finish_timing"]
        pre["display_mode_hex"] = struct.pack("<4i", 1, 0, 0, 0).hex()
        pre["reference_2_hex"] = struct.pack("<4i", 77, 0, 0, 0).hex()
        pre["persisted_0_hex"] = struct.pack("<i", 1234).hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_ai_route=True, compare_ai_timing=True, compare_ai_finish=True)
        raw = binary.read_bytes()
        self.assertEqual(struct.unpack_from("<3I", raw, 32), (1, 1, 1))
        initial_bytes = 12 + 0x540 + 4100 + 2 * 396
        offset = 64 + initial_bytes + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 5576 + 600
        self.assertEqual(struct.unpack_from("<9I", raw, offset), (0, 0, 1000, 0, 0, 0, 1, 0, 0))
        self.assertEqual(struct.unpack_from("<3i4h", raw, offset + 56), (1, 0, 0, 0, 0, 0, 0))
        self.assertEqual(struct.unpack_from("<6i", raw, offset + 56 + 20), (0, 0, 77, 0, 0, 0))
        self.assertEqual(struct.unpack_from("<3i", raw, offset + 360), (1234, 0, 0))
        records[0]["ai_post"]["shared"]["finish_timing"]["persisted_0_hex"] = struct.pack("<i", 999).hex()
        FLEET.write_binary(binary, header, records, compare_ai_route=True, compare_ai_timing=True, compare_ai_finish=True)
        self.assertEqual(binary.read_bytes(), raw)
        del header["finish_timing_manifest"]
        missing = self.path.with_suffix(".missing.bin")
        with self.assertRaisesRegex(ValueError, "finish-timing"):
            FLEET.write_binary(missing, header, records, compare_ai_route=True, compare_ai_timing=True, compare_ai_finish=True)
        self.assertFalse(missing.exists())

    def test_finish_comparison_checks_all_globals_and_text_before_nul_without_false_coverage(self):
        empty_header, empty_records = finish_fixture(1)
        no_event = FLEET.compare_ai_finish(empty_header, empty_records, self.finish_csv(empty_records), [])
        self.assertTrue(no_event["parity"])
        self.assertIsNone(no_event["original_player_split_display_parity"])
        self.assertIsNone(no_event["original_qualifying_event_parity"])
        header, records = finish_fixture(2)
        display = records[1]["ai_post"]["shared"]["finish_timing"]
        display["display_hex"] = (b"+00:00.01\0" + bytes(6) + bytes(48)).hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        native = self.finish_csv(records)
        report = FLEET.compare_ai_finish(header, records, native, [])
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_values"], 196)
        self.assertEqual(report["compared_strings"], 16)
        self.assertEqual(len(report["coverage"]["display_changes"]), 1)
        self.assertFalse(report["coverage"]["newly_finished_actors"])
        #Unused bytes after NUL are retained raw but have no displayed meaning.
        display = records[1]["ai_post"]["shared"]["finish_timing"]
        display["display_hex"] = (b"+00:00.01\0" + b"unused" + bytes(48)).hex()
        self.assertTrue(FLEET.compare_ai_finish(header, records, native, [])["parity"])
        display["display_hex"] = (b"-00:00.01\0" + b"unused" + bytes(48)).hex()
        display["finish_order_hex"] = (bytes(15) + b"\xff").hex()
        report = FLEET.compare_ai_finish(header, records, native, [])
        self.assertEqual(report["mismatch_count"], 2)
        self.assertEqual(report["exact_prefix_updates"], 1)
        self.assertEqual({row["field"] for row in report["first_mismatches"]}, {"finish.finish_order_15", "split-display.display"})
        path = Path(str(native) + ".split-display.csv")
        lines = path.read_text().splitlines()
        lines[5], lines[6] = lines[6], lines[5]
        path.write_text("\n".join(lines) + "\n")
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_ai_finish(header, records, native, [])

    def test_finish_actor_timing_fields_are_conditional_and_checked(self):
        header, records = finish_fixture(1)
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        native = self.timing_csv(records, finish=True)
        self.assertEqual(FLEET.compare_ai_timing(header, records, native, [], compare_finish=True)["compared_values"], 432)
        game = bytearray.fromhex(records[0]["ai_post"]["actors"][7]["gameplay_hex"])
        struct.pack_into("<I", game, 0x120, 5000)
        game[0x8c] = 1
        records[0]["ai_post"]["actors"][7]["gameplay_hex"] = game.hex()
        report = FLEET.compare_ai_timing(header, records, native, [], compare_finish=True)
        self.assertEqual(report["mismatch_count"], 2)
        self.assertEqual({row["field"] for row in report["first_mismatches"]}, {"ai-timing.total_time_hundredths", "ai-timing.notice"})

    def test_checkpoint_binary_imports_only_pre_table_and_dynamic_timing_context(self):
        header, records = timing_fixture(1)
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_ai_route=True, compare_ai_timing=True)
        original = binary.read_bytes()
        self.assertEqual(struct.unpack_from("<2I", original, 32), (1, 1))
        initial_bytes = 12 + 0x540 + 4100 + 2 * 396
        timing_offset = 64 + initial_bytes + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 5576
        self.assertEqual(struct.unpack_from("<IiiIII", original, timing_offset), (50000, 2, 6, 3, 0, 1))
        self.assertEqual(original[timing_offset + 24:timing_offset + 600], bytes(576))
        records[0]["ai_post"]["shared"]["checkpoint_timing"]["split_table_hex"] = (bytes([1]) * 576).hex()
        game = bytearray.fromhex(records[0]["ai_post"]["actors"][7]["gameplay_hex"])
        struct.pack_into("<h", game, 0xa8, 2)
        records[0]["ai_post"]["actors"][7]["gameplay_hex"] = game.hex()
        FLEET.write_binary(binary, header, records, compare_ai_route=True, compare_ai_timing=True)
        self.assertEqual(binary.read_bytes(), original)
        del header["checkpoint_timing_manifest"]
        missing = self.path.with_suffix(".missing.bin")
        with self.assertRaisesRegex(ValueError, "checkpoint-timing"):
            FLEET.write_binary(missing, header, records, compare_ai_route=True, compare_ai_timing=True)
        self.assertFalse(missing.exists())

    def test_checkpoint_comparison_checks_unchanged_slots_actor_timers_and_chronology(self):
        header, records = timing_fixture(2)
        timing = records[1]["ai_post"]["shared"]["checkpoint_timing"]
        table = bytearray.fromhex(timing["split_table_hex"])
        struct.pack_into("<Iii", table, 16 * 12, 50040, 1, 7)
        timing["split_table_hex"] = table.hex()
        timing["race_flags_hex"] = struct.pack("<I", 0x10000).hex()
        game = bytearray.fromhex(records[1]["ai_post"]["actors"][7]["gameplay_hex"])
        struct.pack_into("<h", game, 0xa8, 2)
        struct.pack_into("<3I", game, 0xc0, 42000, 0, 50040)
        records[1]["ai_post"]["actors"][7]["gameplay_hex"] = game.hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        native = self.timing_csv(records)
        report = FLEET.compare_ai_timing(header, records, native, [])
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_values"], 2 * 384)
        self.assertEqual(report["coverage"]["changed_split_slots"], 1)
        self.assertEqual(len(report["coverage"]["lap_transitions"]), 1)
        self.assertEqual(report["coverage"]["race_flag_change_updates"], [1])
        #Slot47 was unchanged by the source event. It still must be checked.
        table = bytearray.fromhex(records[1]["ai_post"]["shared"]["checkpoint_timing"]["split_table_hex"])
        struct.pack_into("<I", table, 47 * 12, 1)
        records[1]["ai_post"]["shared"]["checkpoint_timing"]["split_table_hex"] = table.hex()
        game = bytearray.fromhex(records[1]["ai_post"]["actors"][7]["gameplay_hex"])
        struct.pack_into("<I", game, 0xc0, 43000)
        records[1]["ai_post"]["actors"][7]["gameplay_hex"] = game.hex()
        report = FLEET.compare_ai_timing(header, records, native, [])
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 2)
        self.assertEqual(report["exact_prefix_updates"], 1)
        self.assertEqual({row["field"] for row in report["first_mismatches"]}, {"checkpoint.clock", "ai-timing.best_ms"})
        checkpoint_path = Path(str(native) + ".checkpoint.csv")
        lines = checkpoint_path.read_text().splitlines()
        lines[49], lines[50] = lines[50], lines[49]
        checkpoint_path.write_text("\n".join(lines) + "\n")
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_ai_timing(header, records, native, [])

    def test_optional_checkpoint_capture_requires_all_boundary_observations(self):
        header, records = ai_fixture(1)
        header["checkpoint_timing_manifest"] = FLEET.CHECKPOINT_TIMING_MANIFEST
        memory = {(0x4cc2d8, 576): bytes(range(256)) * 2 + bytes(64),
                  (0x4dcdb0, 4): b"\x00\x00\x01\x00", (0x6005f6, 2): struct.pack("<h", 2)}
        class Reader:
            def memory(self, address, size):
                return memory[(address, size)]
        captured = FLEET.capture_checkpoint_timing(Reader(), 0x600000)
        self.assertEqual(captured["session_stage"], 2)
        self.assertEqual(bytes.fromhex(captured["split_table_hex"]), memory[(0x4cc2d8, 576)])
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            records[0][name]["shared"]["checkpoint_timing"] = dict(captured)
            records[0][name]["race_clock"] = 1000
        write_trace(self.path, header, records)
        _, parsed = FLEET.read_trace(self.path)
        self.assertEqual(parsed[0]["ai_post"]["shared"]["checkpoint_timing"]["split_table"], memory[(0x4cc2d8, 576)])
        records[0]["ground"]["shared"].pop("checkpoint_timing")
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "timing state"):
            FLEET.read_trace(self.path)


class LaunchStateTests(FleetTestCase):
    def test_launch_events_follow_epoch_session_and_mapping_not_tire_outputs(self):
        header, records = fixture(4)
        header["lifecycle_blocks"] = FLEET.LIFECYCLE_BLOCKS
        default = (0, 0, 0, 0, 0xccc, 0, 0, 0)
        for index, record in enumerate(records):
            pre = (0, -5242, -5127, -5012)[index]
            consumed = (0, -5127, -5012, -4897)[index]
            for name in FLEET.BOUNDARIES:
                state = record[name]
                state["session_state"] = 6 if index == 3 else 9
                value = pre if name == "pre" else consumed
                tires = default if value == 0 or (index == 3 and name == "pre") else (value,) * 8
                launch_state(state, value, hold=index < 3, epoch=0 if index == 0 else 23969, tires=tires)
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        metadata = [FLEET.launch_metadata(records, index) for index in range(4)]
        self.assertEqual(metadata[1]["flags"], FLEET.LAUNCH_FLAGS["begin_before_pre"] | FLEET.LAUNCH_FLAGS["decay"] |
                         FLEET.LAUNCH_FLAGS["release_epoch_observed"])
        self.assertEqual(metadata[3]["flags"], FLEET.LAUNCH_FLAGS["decay"] | FLEET.LAUNCH_FLAGS["release_epoch_observed"] |
                         FLEET.LAUNCH_FLAGS["reset_before_pre"] | FLEET.LAUNCH_FLAGS["clutch_release_before_pre"])
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records)
        offset = 64 + 44 + 0x800 + 16 + 15 + 3676
        self.assertEqual(struct.unpack_from("<IiiiIIIi", binary.read_bytes(), offset), (36, 0, 0, 623902, 1, 0, 9, 0))
        rows = [{"id": record["id"], "actor": 0, "phase": "consumed", **FLEET.expected_launch(record["consumed"])}
                for record in records]
        native = self.path.with_suffix(".launch.csv")
        def save():
            with native.open("w", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
        save()
        report = FLEET.compare_launch(records, native, "continuous-physics-inputs")
        self.assertTrue(report["derived_output_parity"])
        self.assertEqual(report["imported_consumed_rows"], [0])
        self.assertEqual(report["derived_consumed_rows"], 3)
        self.assertEqual(report["release_events_before_pre"], 1)
        self.assertEqual(report["reset_events_before_pre"], 1)
        rows[-1]["launch"] += 1
        rows[-1]["tire_offset_0"] += 2
        save()
        report = FLEET.compare_launch(records, native, "continuous-physics-inputs")
        self.assertFalse(report["derived_output_parity"])
        self.assertEqual(report["derived_mismatch_count"], 2)
        self.assertEqual(report["exact_prefix_updates"], 3)
        self.assertEqual(FLEET.launch_metadata(records, 3), metadata[3])

    def test_launch_decay_zero_crossing_and_old_observation_provenance(self):
        header, records = fixture(3)
        default = (0, 0, 0, 0, 0xccc, 0, 0, 0)
        for index, record in enumerate(records):
            for name in FLEET.BOUNDARIES:
                value = (-230, -115, 0)[index] if name == "pre" else (-115, 0, 0)[index]
                record[name]["session_state"] = 6
                launch_state(record[name], value, tires=(value,) * 8 if value else default)
        write_trace(self.path, header, records)
        _, records = FLEET.read_trace(self.path)
        native = self.path.with_suffix(".launch.csv")
        rows = [{"id": record["id"], "actor": 0, "phase": "consumed", **FLEET.expected_launch(record["consumed"])}
                for record in records]
        with native.open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        report = FLEET.compare_launch(records, native, "continuous-physics-inputs")
        self.assertEqual(report["decay_zero_crossing_updates"], [1])
        self.assertIn("epoch unavailable", report["observed_event_provenance"][1]["event_evidence"])
        self.assertFalse(report["autonomous_session_parity"])
        imported = FLEET.compare_launch(records, native, "independent-physics")
        self.assertEqual(imported["derived_consumed_rows"], 0)
        self.assertIsNone(imported["derived_output_parity"])
