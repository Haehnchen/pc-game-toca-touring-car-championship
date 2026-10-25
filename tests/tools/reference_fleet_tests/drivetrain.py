"""Fleet reference checks for drivetrain."""

from .support import (
    TraceTestCase,
    CLI,
    FLEET,
    Path,
    csv,
    fixture,
    io,
    json,
    patch,
    struct,
    subprocess,
    sys,
)
from .drivers import BrakePublicationTests
from .drivers import DriverPhysicsTests
from .drivers import DriverRetentionTests


class DrivetrainFeedbackTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = DriverRetentionTests.fixture
    change = staticmethod(DriverPhysicsTests.change)
    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True,
                           compare_driver_retention=True, drivetrain_feedback=True)
        FLEET.write_binary(physical, header, records, driver_physics_inputs=True, drivetrain_feedback=True)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, drivetrain_feedback=True)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def test_drivetrain_initializes_from_first_pre_only_and_all_other_copies_are_expectations(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                for entry in record[phase]["actors"]:
                    for offset in (0x330, 0x334, 0x30c, 0x304): self.change(entry, "dynamics", offset, 1)
            for kind, _, event in FLEET.combined_events(record):
                if kind == "wheel":
                    for phase in ("pre", "post"): event[phase]["reverse_pedal_hex"] = bytes([0xa5] * 64).hex()
                if kind == "motor":
                    for entry in event["pre"]["physical"]: entry["gear_hex"] = "07"
        self.assertEqual(before, self.binaries(header, records))
        self.change(records[0]["pre"]["actors"][15], "dynamics", 0x334, 1)
        changed = self.binaries(header, records)
        self.assertNotEqual(before[0], changed[0])
        self.assertEqual(before[1:], changed[1:])

    def test_remaining_physical_neighbors_mapper_clutch_and_queued_edges_stay_observed(self):
        header, records = self.fixture()
        for offset in (0x331, 0x332, 0x333, 0x335, 0x336, 0x337, 0x124, 0x148, 0x14c):
            before = self.binaries(header, records)
            self.change(records[1]["consumed"]["actors"][15], "dynamics", offset, 1)
            after = self.binaries(header, records)
            self.assertNotEqual(before[2], after[2], offset)
        before = self.binaries(header, records)
        self.change(records[1]["consumed"]["actors"][0], "dynamics", 0x140, 1)
        self.assertNotEqual(before[1], self.binaries(header, records)[1])

    def feedback_sidecar(self, records):
        rows = list(FLEET.drivetrain_feedback_expected(records))
        path = Path(str(self.path) + ".drivetrain-feedback.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_all_actor_all_boundary_checkpoints_and_actual_positives_are_reported(self):
        header, records = self.fixture()
        for offset, value in ((0x334, 3), (0x30c, 1)):
            self.change(records[1]["consumed"]["actors"][15], "dynamics", offset, value)
        self.change(records[1]["contacts"]["actors"][15], "dynamics", 0x330, 3)
        self.change(records[1]["contacts"]["actors"][15], "dynamics", 0x304, 1)
        path = self.feedback_sidecar(records)
        report = FLEET.compare_drivetrain_feedback(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertNotIn("actors", records[0]["skid_dispatch"]["pre"])
        self.assertEqual(report["direct_actor_snapshot_values"], len(records) * 16 * 7 * 4)
        self.assertEqual(report["unchanged_expectation_values"], len(records) * 16 * 4)
        self.assertNotIn("skid_pre", report["direct_actor_snapshot_phases"])
        self.assertIn("skid_pre", report["unchanged_expectation_phases"])
        self.assertEqual(report["rows"], len(records) * 16 * 8)
        self.assertEqual(report["compared_values"], len(records) * 16 * 8 * 4)
        self.assertEqual(report["compared_values_per_phase"], dict.fromkeys((*FLEET.COMBINED_PHASES, "ai_return"), len(records) * 16 * 4))
        self.assertEqual(report["positive_source_counts"]["pending_commits"], 1)
        self.assertEqual(report["positive_source_counts"]["physical_reverse_pedal_changes"], 1)
        self.assertEqual(report["later_drivetrain_imports"], 0)
        self.assertIsNone(report["source_ai_recovery_parity"])
        self.assertIn("AI recovery entry", report["unobserved_coverage"])
        self.assertIsNone(report["continuous_physics_parity"])
        with path.open(newline="") as stream: rows = list(csv.DictReader(stream))
        rows[-1]["pending_gear"] = str(int(rows[-1]["pending_gear"]) + 1)
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        report = FLEET.compare_drivetrain_feedback(header, records, self.path)
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 1)
        self.assertEqual(report["exact_prefix_updates"], len(records) - 1)
        self.assertEqual(report["mismatches_per_actor"][-1], 1)

    def test_missing_extra_reordered_checkpoints_fail_closed(self):
        header, records = self.fixture()
        path = self.feedback_sidecar(records)
        original = path.read_text().splitlines()
        for rows, message in ((original[:-1], "Missing"), (original + original[-1:], "Extra"),
                              ([original[0], original[2], original[1], *original[3:]], "Reordered")):
            path.write_text("\n".join(rows) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_drivetrain_feedback(header, records, self.path)

    def test_cli_selects_feedback_without_second_ai_or_callback_consumer(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original complete interval")
        expected = {"parity": True, "physical_parity": True, "mismatch_count": 0, "exact_prefix_updates": len(records)}
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-drivetrain-feedback"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as binary, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_count, 1)
        self.assertEqual(execute.call_args.args[0][1], "--compare-drivetrain-feedback")
        self.assertEqual(len(execute.call_args.args[0]), 8)
        self.assertEqual(binary.call_count, 2)
        self.assertTrue(all(call.kwargs["drivetrain_feedback"] for call in binary.call_args_list))
        self.assertTrue(journal.call_args.kwargs["drivetrain_feedback"])
        self.assertTrue(compare.call_args.kwargs["drivetrain_feedback"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-drivetrain-feedback.json").read_text())
        self.assertTrue(report["complete_source_interval"])
        self.assertIsNone(report["continuous_physics_parity"])


class RpmFeedbackTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = BrakePublicationTests.fixture
    change = staticmethod(DriverPhysicsTests.change)

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True,
                           compare_driver_retention=True, drivetrain_feedback=True, rpm_feedback=True)
        FLEET.write_binary(physical, header, records, driver_physics_inputs=True, drivetrain_feedback=True, rpm_feedback=True)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, drivetrain_feedback=True, rpm_feedback=True)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def sidecar(self, records):
        rows = list(FLEET.rpm_feedback_expected(records))
        path = Path(str(self.path) + ".rpm-feedback.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_all_later_raw_rpm_copies_launch_and_actual_motor_rpm_are_expectations_only(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                for entry in record[phase]["actors"]:
                    for offset in range(0x124, 0x12c): self.change(entry, "dynamics", offset)
            for kind, _, event in FLEET.combined_events(record):
                if kind == "motor":
                    for entry in event["pre"]["physical"]: entry["engine_speed_hex"] = struct.pack("<i", 0x12345678).hex()
        self.assertEqual(before, self.binaries(header, records))
        for offset in (0x124, 0x128):
            self.change(records[0]["pre"]["actors"][15], "dynamics", offset, 1)
            after = self.binaries(header, records)
            self.assertNotEqual(before[0], after[0])
            self.assertEqual(before[1:], after[1:])
            self.change(records[0]["pre"]["actors"][15], "dynamics", offset, 1)

    def test_remaining_physical_mapping_callback_and_launch_maximum_contexts_stay_observed(self):
        header, records = self.fixture()
        for name, offset in (("dynamics", 0x120), ("dynamics", 0x12c), ("dynamics", 0x11c),
                             ("dynamics", 0x148), ("dynamics", 0x14c), ("dynamics", 0x331),
                             ("body", 0xbc), ("body", 0x1f0)):
            before = self.binaries(header, records)
            self.change(records[1]["consumed"]["actors"][15], name, offset, 1)
            self.assertNotEqual(before[2], self.binaries(header, records)[2], (name, offset))
        before = self.binaries(header, records)
        self.change(records[1]["consumed"]["actors"][0], "dynamics", 0x140, 1)
        self.assertNotEqual(before[1], self.binaries(header, records)[1])
        before = self.binaries(header, records)
        self.change(records[1]["ai_pre"]["actors"][header["local_actor"]], "dynamics", 0x20, 1)
        self.assertNotEqual(before[1], self.binaries(header, records)[1])
        motor = next(event for record in records for kind, _, event in FLEET.combined_events(record) if kind == "motor")
        for name in ("rpm_scale_hex", "throttle_hex"):
            before = self.binaries(header, records)
            motor["pre"]["physical"][0][name] = struct.pack("<i", 1234).hex()
            self.assertNotEqual(before[1], self.binaries(header, records)[1], name)
        before = self.binaries(header, records)
        motor["pre"]["session_state"] += 1
        self.assertNotEqual(before[1], self.binaries(header, records)[1])

    def test_audio_only_skid_pre_uses_unchanged_ground_expectations_and_all_other_snapshots_are_direct(self):
        header, records = self.fixture()
        self.assertNotIn("actors", records[0]["skid_dispatch"]["pre"])
        for offset in (0x124, 0x128): self.change(records[1]["contacts"]["actors"][15], "dynamics", offset, 1)
        path = self.sidecar(records)
        report = FLEET.compare_rpm_feedback(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_values"], len(records) * 16 * 8 * 2)
        self.assertEqual(report["direct_actor_snapshot_values"], len(records) * 16 * 7 * 2)
        self.assertEqual(report["unchanged_expectation_values"], len(records) * 16 * 2)
        self.assertNotIn("skid_pre", report["direct_actor_snapshot_phases"])
        self.assertIn("skid_pre", report["unchanged_expectation_phases"])
        self.assertEqual(report["later_rpm_imports"], 0)
        self.assertEqual(report["positive_source_counts"]["physical_rpm_changes"], 1)
        self.assertEqual(report["positive_source_counts"]["physical_previous_rpm_changes"], 1)
        self.assertGreater(report["positive_source_counts"]["actual_motor_player_calls"], 0)
        self.assertGreater(report["positive_source_counts"]["actual_launch_calls"], 0)
        self.assertIn("explicit observed schedule", report["contact_tail_scope"])
        with path.open(newline="") as stream: rows = list(csv.DictReader(stream))
        rows[-1]["previous_engine_speed"] = str(int(rows[-1]["previous_engine_speed"]) + 1)
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        report = FLEET.compare_rpm_feedback(header, records, self.path)
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 1)
        self.assertEqual(report["mismatches_per_actor"][-1], 1)
        self.assertEqual(report["exact_prefix_updates"], len(records) - 1)

    def test_missing_extra_reordered_and_incorrect_headers_fail_closed(self):
        header, records = self.fixture()
        path = self.sidecar(records)
        original = path.read_text().splitlines()
        for rows, message in ((original[:-1], "Missing"), (original + original[-1:], "Extra"),
                              ([original[0], original[2], original[1], *original[3:]], "Reordered"),
                              ([original[0].replace("engine_speed", "rpm", 1), *original[1:]], "header")):
            path.write_text("\n".join(rows) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_rpm_feedback(header, records, self.path)

    def test_rpm_comparison_keeps_prior_capture_launch_scope_explicit(self):
        from .drivers import DriverRetentionTests

        header, records = DriverRetentionTests.fixture(self)
        self.sidecar(records)
        report = FLEET.compare_rpm_feedback(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["positive_source_counts"]["actual_launch_calls"], 0)
        self.assertIn("local launch scalar remains observed", report["launch_scope"])

    def test_aggregate_report_marks_rpm_generated_and_keeps_other_world_and_tail_inputs_explicit(self):
        from .drivers import DriverPhysicsTests

        header, records = self.fixture()
        DriverPhysicsTests.sidecars(self, records)
        DrivetrainFeedbackTests.feedback_sidecar(self, records)
        self.sidecar(records)
        combined = {"parity": True, "numerical_parity": True, "coverage_sufficient": True,
                    "compared_values": 17, "mismatch_count": 0, "exact_prefix_updates": len(records),
                    "whine": {}, "joined_ai": {}, "physical_synchronizations": ["observed physical gear and reverse pedal"]}
        with patch.object(FLEET, "compare_combined", return_value=combined):
            report = FLEET.compare_driver_physics(header, records, self.path, drivetrain_feedback=True, rpm_feedback=True)
        self.assertTrue(report["parity"])
        self.assertEqual(report["physical"]["mode"], "retained-rpm-feedback")
        self.assertIn("engine RPM", report["launch"]["derived_fields"])
        self.assertNotIn("engine RPM", report["launch"]["observed_fields"])
        self.assertIn("retained physical RPM124", report["combined"]["whine"]["input_scope"])
        self.assertEqual(report["rpm_feedback"]["compared_values"], len(records) * 16 * 8 * 2)
        self.assertIn("contact-event strengths observed", report["boundary_scope"])
        self.assertIsNone(report["continuous_physics_parity"])

    def test_actor_unbound_type17_contacts_remain_actual_observed_arguments_and_schedule(self):
        from .random import ContactWeatherRngTests

        header, records = self.fixture()
        contact_header, contacts = ContactWeatherRngTests.fixture(self)
        for key in ("contact_weather_manifest", "contact_audio_initial", "combined_initial_release_counter"):
            header[key] = contact_header[key]
        header["combined_initial_release_counter"] = 0
        entry = json.loads(json.dumps(contacts[0]["contact_audio_calls"][0]))
        entry["event"]["kind"] = 17
        records[0]["contact_audio_calls"] = [entry]
        before = self.binaries(header, records)
        entry["event"]["strength"] += 1
        self.assertNotEqual(before[1], self.binaries(header, records)[1])
        self.sidecar(records)
        report = FLEET.compare_rpm_feedback(header, records, self.path)
        self.assertEqual(report["positive_source_counts"]["observed_type17_contact_calls"], 1)
        self.assertIn("no generated contact-tail caller claim", report["contact_tail_scope"])

    def test_rpm_mode_requires_retained_drivetrain(self):
        header, records = self.fixture()
        with self.assertRaisesRegex(ValueError, "requires drivetrain"):
            FLEET.write_binary(self.path, header, records, rpm_feedback=True)
        with self.assertRaisesRegex(ValueError, "requires drivetrain"):
            FLEET.write_combined_binary(self.path, header, records, compare_driver_retention=True, rpm_feedback=True)
        with self.assertRaisesRegex(ValueError, "requires drivetrain"):
            FLEET.compare_driver_physics(header, records, self.path, rpm_feedback=True)

    def test_cli_selects_one_rpm_driver_physics_shared_rng_transaction(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original complete interval")
        expected = {"parity": True, "physical_parity": True, "mismatch_count": 0, "exact_prefix_updates": len(records)}
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-rpm-feedback"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as binary, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_count, 1)
        self.assertEqual(execute.call_args.args[0][1], "--compare-rpm-feedback")
        self.assertEqual(len(execute.call_args.args[0]), 8)
        self.assertEqual(binary.call_count, 2)
        self.assertTrue(all(call.kwargs["rpm_feedback"] and call.kwargs["drivetrain_feedback"] for call in binary.call_args_list))
        self.assertTrue(journal.call_args.kwargs["rpm_feedback"])
        self.assertTrue(compare.call_args.kwargs["rpm_feedback"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-rpm-feedback.json").read_text())
        self.assertTrue(report["complete_source_interval"])
        self.assertIsNone(report["continuous_physics_parity"])


class LimiterFeedbackTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = BrakePublicationTests.fixture
    change = staticmethod(DriverPhysicsTests.change)

    @staticmethod
    def limiter(snapshot, actor, active, load):
        for name, value, format in (("limiter_active", active, "<I"), ("limiter_load", load, "<i")):
            raw = bytearray.fromhex(snapshot["shared"][name + "_hex"])
            struct.pack_into(format, raw, actor * 4, value)
            snapshot["shared"][name] = bytes(raw)
            snapshot["shared"][name + "_hex"] = raw.hex()

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True,
                           compare_driver_retention=True, drivetrain_feedback=True, rpm_feedback=True, limiter_feedback=True)
        FLEET.write_binary(physical, header, records, driver_physics_inputs=True,
                           drivetrain_feedback=True, rpm_feedback=True, limiter_feedback=True)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True,
                                       drivetrain_feedback=True, rpm_feedback=True, limiter_feedback=True)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def sidecar(self, records):
        rows = list(FLEET.limiter_feedback_expected(records))
        path = Path(str(self.path) + ".limiter-feedback.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_limiter_initializes_only_first_pre_all_later_global_and_metadata_copies_are_absent(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = record[phase]
                snapshot["limiter_active"], snapshot["limiter_load_q16"] = 1, 98765
                if record is records[0] and phase == "pre": continue
                for actor in range(16): self.limiter(snapshot, actor, 1, -1234 - actor)
        self.assertEqual(before, self.binaries(header, records))
        self.limiter(records[0]["pre"], 15, 1, 4321)
        after = self.binaries(header, records)
        self.assertNotEqual(before[0], after[0])
        self.assertEqual(before[1:], after[1:])
        # Existing RPM mode still observes per-update limiter context.
        rpm = RpmFeedbackTests.binaries(self, header, records)
        self.limiter(records[1]["consumed"], 15, 0, -9988)
        self.assertNotEqual(rpm[2], RpmFeedbackTests.binaries(self, header, records)[2])

    def test_raw_globals_are_expected_at_each_boundary_and_skid_carries_only_ground(self):
        header, records = self.fixture()
        for index, record in enumerate(records):
            for phase_index, phase in enumerate((*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES)):
                for actor in range(16): self.limiter(record[phase], actor, (actor + phase_index) % 2, index * 10000 + phase_index * 100 + actor)
        rows = list(FLEET.limiter_feedback_expected(records))
        for record in records:
            for ordinal, (kind, phase, event) in enumerate(FLEET.combined_events(record)):
                if kind not in ("boundary", "ai"): continue
                wanted = FLEET.limiter_values(record["ground"] if phase == "skid_pre" else event, 15)
                self.assertEqual(next(row for row in rows if row["id"] == record["id"] and row["event"] == ordinal and row["actor"] == 15),
                                 {"id": record["id"], "event": ordinal, "actor": 15, **wanted})
        path = self.sidecar(records)
        report = FLEET.compare_limiter_feedback(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_values"], len(records) * 16 * 8 * 2)
        self.assertEqual(report["direct_raw_global_snapshot_values"], len(records) * 16 * 7 * 2)
        self.assertEqual(report["unchanged_expectation_values"], len(records) * 16 * 2)
        self.assertNotIn("skid_pre", report["direct_raw_global_snapshot_phases"])
        self.assertEqual(report["later_limiter_imports"], 0)
        with path.open(newline="") as stream: output = list(csv.DictReader(stream))
        output[-1]["limiter_load_q16"] = str(int(output[-1]["limiter_load_q16"]) + 1)
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(output[0])); writer.writeheader(); writer.writerows(output)
        report = FLEET.compare_limiter_feedback(header, records, self.path)
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 1)
        self.assertEqual(report["mismatches_per_actor"][-1], 1)
        self.assertEqual(report["exact_prefix_updates"], len(records) - 1)

    def test_positive_source_counts_distinguish_full_consumer_reset_and_distant_active_retention(self):
        header, records = self.fixture()
        record = records[0]
        self.limiter(record["consumed"], 14, 1, 123)
        self.limiter(record["contacts"], 14, 0, 123)
        self.limiter(record["consumed"], 15, 1, 321)
        self.limiter(record["contacts"], 15, 1, 321)
        raw = bytearray(record["consumed"]["actors"][15]["body"])
        struct.pack_into("<I", raw, 0x4f0, 1)
        record["consumed"]["actors"][15]["body"] = bytes(raw)
        record["consumed"]["actors"][15]["body_hex"] = raw.hex()
        self.limiter(record["contacts"], 13, 1, 999)
        raw = bytearray(record["consumed"]["actors"][13]["body"])
        struct.pack_into("<I", raw, 0x4f0, 0)
        record["consumed"]["actors"][13]["body"] = bytes(raw)
        record["consumed"]["actors"][13]["body_hex"] = raw.hex()
        self.sidecar(records)
        report = FLEET.compare_limiter_feedback(header, records, self.path)
        counts = report["positive_source_counts"]
        self.assertTrue(report["coverage_sufficient"])
        self.assertEqual(counts["full_active_before"], 1)
        self.assertEqual(counts["full_flag_resets"], 1)
        self.assertEqual(counts["full_active_after"], 1)
        self.assertEqual(counts["physical_load_changes"], 1)
        self.assertEqual(counts["distant_active_retains"], 1)
        self.assertEqual(counts["distant_nonzero_load_retains"], 1)
        self.assertIn("frozen limiter retention", report["unobserved_coverage"])
        self.assertIsNone(report["continuous_physics_parity"])
        self.assertIsNone(report["autonomous_race_parity"])

    def test_aggregate_reports_generated_limiter_and_remaining_observed_world_inputs(self):
        from .drivers import DriverPhysicsTests

        header, records = self.fixture()
        DriverPhysicsTests.sidecars(self, records)
        DrivetrainFeedbackTests.feedback_sidecar(self, records)
        RpmFeedbackTests.sidecar(self, records)
        self.sidecar(records)
        combined = {"parity": True, "numerical_parity": True, "coverage_sufficient": True,
                    "compared_values": 17, "mismatch_count": 0, "exact_prefix_updates": len(records),
                    "whine": {}, "joined_ai": {}, "physical_synchronizations": []}
        with patch.object(FLEET, "compare_combined", return_value=combined):
            report = FLEET.compare_driver_physics(header, records, self.path, drivetrain_feedback=True,
                                                 rpm_feedback=True, limiter_feedback=True)
        self.assertTrue(report["parity"])
        self.assertEqual(report["physical"]["mode"], "retained-limiter-feedback")
        self.assertEqual(report["limiter_feedback"]["compared_values"], len(records) * 16 * 8 * 2)
        self.assertNotIn("limiter", report["physical"]["observed_inputs_each_update"][-1])
        self.assertIn("observed physical PRE", report["physical"]["world_assumptions"][0])
        self.assertIn("remaining physical PRE", report["boundary_scope"])
        self.assertIn("contact-event strengths observed", report["boundary_scope"])
        self.assertFalse(report["coverage_sufficient"])
        self.assertIsNone(report["continuous_physics_parity"])
        self.assertIsNone(report["autonomous_race_parity"])

    def test_missing_extra_reordered_and_incorrect_headers_fail_closed(self):
        header, records = self.fixture()
        path = self.sidecar(records)
        original = path.read_text().splitlines()
        for rows, message in ((original[:-1], "Missing"), (original + original[-1:], "Extra"),
                              ([original[0], original[2], original[1], *original[3:]], "Reordered"),
                              ([original[0].replace("limiter_load_q16", "load", 1), *original[1:]], "header")):
            path.write_text("\n".join(rows) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_limiter_feedback(header, records, self.path)

    def test_mode_requires_retained_rpm(self):
        header, records = self.fixture()
        for function in (FLEET.write_binary, FLEET.write_combined_binary):
            with self.assertRaisesRegex(ValueError, "requires RPM"):
                function(self.path, header, records, limiter_feedback=True)
        with self.assertRaisesRegex(ValueError, "requires RPM"):
            FLEET.compare_driver_physics(header, records, self.path, limiter_feedback=True)

    def test_cli_selects_one_limiter_rpm_drivetrain_driver_physics_shared_rng_transaction(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original complete interval")
        expected = {"parity": True, "physical_parity": True, "mismatch_count": 0, "exact_prefix_updates": len(records)}
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-limiter-feedback"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as binary, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_count, 1)
        self.assertEqual(execute.call_args.args[0][1], "--compare-limiter-feedback")
        self.assertEqual(len(execute.call_args.args[0]), 8)
        self.assertEqual(binary.call_count, 2)
        self.assertTrue(all(call.kwargs["limiter_feedback"] and call.kwargs["rpm_feedback"] and call.kwargs["drivetrain_feedback"] for call in binary.call_args_list))
        self.assertTrue(journal.call_args.kwargs["limiter_feedback"])
        self.assertTrue(compare.call_args.kwargs["limiter_feedback"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-limiter-feedback.json").read_text())
        self.assertTrue(report["complete_source_interval"])
        self.assertIsNone(report["continuous_physics_parity"])
