"""Fleet reference checks for controls."""

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


class PedalFeedbackTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = BrakePublicationTests.fixture
    @staticmethod
    def change(snapshot, actor, name, offset, value):
        entry = snapshot["actors"][actor]
        raw = bytearray(entry[name]); struct.pack_into("<i", raw, offset, value)
        entry[name] = bytes(raw); entry[name + "_hex"] = raw.hex()

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        flags = dict(drivetrain_feedback=True, rpm_feedback=True, limiter_feedback=True, pedal_feedback=True)
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True,
                           compare_driver_retention=True, **flags)
        FLEET.write_binary(physical, header, records, driver_physics_inputs=True, **flags)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, **flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def sidecar(self, records):
        rows = list(FLEET.pedal_feedback_expected(records))
        path = Path(str(self.path) + ".pedal-feedback.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_first_pre_is_the_only_pedal_initialization_all_other_copies_are_expectations(self):
        from .drivetrain import LimiterFeedbackTests

        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                for actor in range(16):
                    for offset in FLEET.PEDAL_OFFSETS: self.change(record[phase], actor, "dynamics", offset, 54321 + actor + offset)
            for kind, _, callback in FLEET.combined_events(record):
                if kind != "motor": continue
                for physical in callback["pre"]["physical"]: physical["throttle_hex"] = struct.pack("<i", -98765).hex()
        self.assertEqual(before, self.binaries(header, records))
        for offset in FLEET.PEDAL_OFFSETS:
            before = self.binaries(header, records)
            self.change(records[0]["pre"], 15, "dynamics", offset, -45678 - offset)
            after = self.binaries(header, records)
            self.assertNotEqual(before[0], after[0])
            self.assertEqual(before[1:], after[1:])
        #Prior limiter mode still imports these per-update filter contexts.
        before = LimiterFeedbackTests.binaries(self, header, records)
        self.change(records[1]["consumed"], 15, "dynamics", 0x134, -112233)
        self.assertNotEqual(before[2], LimiterFeedbackTests.binaries(self, header, records)[2])

    def test_motor_payload_removes_retained_throttle_but_preserves_observed_engine_scale(self):
        header, records = self.fixture()
        callback = next(callback for record in records for kind, _, callback in FLEET.combined_events(record) if kind == "motor")
        before = FLEET.combined_motor_payload(callback, True, True, True, True)
        physical = callback["pre"]["physical"][0]
        physical["throttle_hex"] = struct.pack("<i", 777777).hex()
        self.assertEqual(before, FLEET.combined_motor_payload(callback, True, True, True, True))
        self.assertNotEqual(before, FLEET.combined_motor_payload(callback, True, True, True))
        physical["rpm_scale_hex"] = struct.pack("<i", 123456).hex()
        self.assertNotEqual(before, FLEET.combined_motor_payload(callback, True, True, True, True))

    def test_every_raw_actor_boundary_and_ground_only_skid_expectation_are_checked(self):
        header, records = self.fixture()
        for index, record in enumerate(records):
            for phase_index, phase in enumerate((*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES)):
                for actor in range(16):
                    for offset in FLEET.PEDAL_OFFSETS:
                        self.change(record[phase], actor, "dynamics", offset, index * 100000 + phase_index * 1000 + actor * 10 + offset)
        rows = list(FLEET.pedal_feedback_expected(records))
        for record in records:
            for ordinal, (kind, phase, event) in enumerate(FLEET.combined_events(record)):
                if kind not in ("boundary", "ai"): continue
                wanted = FLEET.pedal_values(record["ground"] if phase == "skid_pre" else event, 15)
                self.assertEqual(next(row for row in rows if row["id"] == record["id"] and row["event"] == ordinal and row["actor"] == 15),
                                 {"id": record["id"], "event": ordinal, "actor": 15, **wanted})
        path = self.sidecar(records)
        report = FLEET.compare_pedal_feedback(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_values"], len(records) * 16 * 8 * 6)
        self.assertEqual(report["direct_raw_actor_snapshot_values"], len(records) * 16 * 7 * 6)
        self.assertEqual(report["unchanged_expectation_values"], len(records) * 16 * 6)
        for name in FLEET.PEDAL_FIELDS:
            with path.open(newline="") as stream: output = list(csv.DictReader(stream))
            original = output[-1][name]; output[-1][name] = str(int(original) + 1)
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(output[0])); writer.writeheader(); writer.writerows(output)
            report = FLEET.compare_pedal_feedback(header, records, self.path)
            self.assertFalse(report["parity"])
            self.assertEqual(report["mismatch_count"], 1)
            self.assertEqual(report["mismatches_per_actor"][-1], 1)
            self.assertEqual(report["exact_prefix_updates"], len(records) - 1)
            self.sidecar(records)

    def test_source_coverage_distinguishes_cached_steps_filter_advances_and_direct_retention(self):
        header, records = self.fixture()
        for index, record in enumerate(records):
            for actor in range(16):
                mode = index % 2 if actor == 15 else (index + 1) % 2
                self.change(record["consumed"], actor, "body", 0x4f0, mode)
                for offset in (0x18, 0x3c): self.change(record["consumed"], actor, "dynamics", offset, 65536)
                for offset in FLEET.PEDAL_OFFSETS:
                    self.change(record["consumed"], actor, "dynamics", offset, 1)
                    self.change(record["contacts"], actor, "dynamics", offset,
                                record["consumed"]["delta_q16"] if mode == 0 and offset in (0x14, 0x38) else 1 if offset in (0x14, 0x38) else 2)
        self.sidecar(records)
        report = FLEET.compare_pedal_feedback(header, records, self.path)
        self.assertTrue(report["coverage_sufficient"])
        self.assertTrue(all(report["physical_changes_per_field"].values()))
        self.assertGreater(report["positive_source_counts"]["distant_filter_advances"], 0)
        self.assertEqual(report["positive_source_counts"]["distant_cached_step_mismatches"], 0)
        self.assertEqual(report["positive_source_counts"]["full_step_refresh_mismatches"], 0)
        self.assertIn("frozen pedal retention", report["unobserved_coverage"])
        self.change(records[0]["contacts"], 0, "dynamics", 0x14, 998877)
        self.sidecar(records)
        self.assertFalse(FLEET.compare_pedal_feedback(header, records, self.path)["coverage_sufficient"])

    def test_missing_extra_reordered_and_incorrect_headers_fail_closed(self):
        header, records = self.fixture()
        path = self.sidecar(records); original = path.read_text().splitlines()
        for rows, message in ((original[:-1], "Missing"), (original + original[-1:], "Extra"),
                              ([original[0], original[2], original[1], *original[3:]], "Reordered"),
                              ([original[0].replace("brake_q16", "brake", 1), *original[1:]], "header")):
            path.write_text("\n".join(rows) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_pedal_feedback(header, records, self.path)

    def test_aggregate_adds_six_words_and_names_remaining_observed_inputs(self):
        from .drivers import DriverPhysicsTests
        from .drivetrain import DrivetrainFeedbackTests
        from .drivetrain import LimiterFeedbackTests
        from .drivetrain import RpmFeedbackTests

        header, records = self.fixture()
        DriverPhysicsTests.sidecars(self, records)
        DrivetrainFeedbackTests.feedback_sidecar(self, records)
        RpmFeedbackTests.sidecar(self, records)
        LimiterFeedbackTests.sidecar(self, records)
        self.sidecar(records)
        combined = {"parity": True, "numerical_parity": True, "coverage_sufficient": True,
                    "compared_values": 17, "mismatch_count": 0, "exact_prefix_updates": len(records),
                    "whine": {}, "joined_ai": {}, "physical_synchronizations": []}
        with patch.object(FLEET, "compare_combined", return_value=combined):
            report = FLEET.compare_driver_physics(header, records, self.path, drivetrain_feedback=True,
                                                 rpm_feedback=True, limiter_feedback=True, pedal_feedback=True)
        self.assertTrue(report["parity"])
        self.assertEqual(report["physical"]["mode"], "retained-pedal-feedback")
        self.assertEqual(report["pedal_feedback"]["compared_values"], len(records) * 16 * 8 * 6)
        self.assertIn("scale24", report["combined"]["whine"]["input_scope"])
        self.assertNotIn("pedal11c", report["rpm_feedback"]["observed_inputs"][2])
        self.assertIn("throttle11c retained", report["limiter_feedback"]["observed_inputs"][2])
        self.assertIn("steering150/274", report["physical"]["observed_inputs_each_update"][0])
        self.assertIn("remaining physical PRE", report["boundary_scope"])
        self.assertIsNone(report["continuous_physics_parity"])
        header.pop("brake_publication_manifest")
        self.assertIn("older callback profile", FLEET.compare_pedal_feedback(header, records, self.path)["callback_capture_scope"])

    def test_mode_requires_all_prior_feedback_layers(self):
        header, records = self.fixture()
        for function in (FLEET.write_binary, FLEET.write_combined_binary, FLEET.compare_driver_physics):
            with self.assertRaisesRegex(ValueError, "requires limiter"):
                function(self.path, header, records, pedal_feedback=True) if function != FLEET.compare_driver_physics else function(header, records, self.path, pedal_feedback=True)

    def test_cli_selects_one_pedal_limiter_rpm_drivetrain_driver_shared_rng_transaction(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original complete interval")
        expected = {"parity": True, "physical_parity": True, "mismatch_count": 0, "exact_prefix_updates": len(records)}
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-pedal-feedback"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as binary, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_count, 1)
        self.assertEqual(execute.call_args.args[0][1], "--compare-pedal-feedback")
        self.assertEqual(len(execute.call_args.args[0]), 8)
        self.assertEqual(binary.call_count, 2)
        self.assertTrue(all(call.kwargs["pedal_feedback"] and call.kwargs["limiter_feedback"] and call.kwargs["rpm_feedback"] and call.kwargs["drivetrain_feedback"] for call in binary.call_args_list))
        self.assertTrue(journal.call_args.kwargs["pedal_feedback"])
        self.assertTrue(compare.call_args.kwargs["pedal_feedback"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-pedal-feedback.json").read_text())
        self.assertTrue(report["complete_source_interval"])
        self.assertIsNone(report["continuous_physics_parity"])


class ControlFeedbackTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = BrakePublicationTests.fixture

    @staticmethod
    def change(snapshot, actor, offset, size, value):
        entry = snapshot["actors"][actor]
        raw = bytearray(entry["dynamics"])
        struct.pack_into("<h" if size == 2 else "<i", raw, offset, value)
        entry["dynamics"] = bytes(raw); entry["dynamics_hex"] = raw.hex()

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        flags = dict(drivetrain_feedback=True, rpm_feedback=True, limiter_feedback=True, pedal_feedback=True, control_feedback=True)
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True,
                           compare_driver_retention=True, **flags)
        FLEET.write_binary(physical, header, records, driver_physics_inputs=True, **flags)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, **flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def sidecar(self, records):
        rows = list(FLEET.control_feedback_expected(records))
        path = Path(str(self.path) + ".control-feedback.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_initial_pre_only_and_signed_int16_angle_never_imports_later_copies(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                for actor in range(16):
                    for offset, size in FLEET.CONTROL_SIZES: self.change(record[phase], actor, offset, size, -1234 - actor)
        self.assertEqual(before, self.binaries(header, records))
        for offset, size in FLEET.CONTROL_SIZES:
            before = self.binaries(header, records)
            self.change(records[0]["pre"], 15, offset, size, -32768 if size == 2 else -123456789)
            after = self.binaries(header, records)
            self.assertNotEqual(before[0], after[0]); self.assertEqual(before[1:], after[1:])
        self.assertEqual(FLEET.control_values(records[0]["pre"], 15)["angle"], -32768)
        #Existing pedal mode retains its observed per-update steering context.
        before = PedalFeedbackTests.binaries(self, header, records)
        self.change(records[1]["consumed"], 15, 0x150, 4, -13579)
        self.assertNotEqual(before[2], PedalFeedbackTests.binaries(self, header, records)[2])

    def test_seven_raw_phases_plus_ground_skid_expectation_and_each_field_corruption(self):
        header, records = self.fixture()
        for index, record in enumerate(records):
            for phase_index, phase in enumerate((*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES)):
                for actor in range(16):
                    for offset, size in FLEET.CONTROL_SIZES:
                        self.change(record[phase], actor, offset, size, -10000 + index * 1000 + phase_index * 100 + actor)
        rows = list(FLEET.control_feedback_expected(records))
        for record in records:
            for ordinal, (kind, phase, event) in enumerate(FLEET.combined_events(record)):
                if kind not in ("boundary", "ai"): continue
                wanted = FLEET.control_values(record["ground"] if phase == "skid_pre" else event, 15)
                self.assertEqual(next(row for row in rows if row["id"] == record["id"] and row["event"] == ordinal and row["actor"] == 15),
                                 {"id": record["id"], "event": ordinal, "actor": 15, **wanted})
        path = self.sidecar(records)
        report = FLEET.compare_control_feedback(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_values"], len(records) * 16 * 8 * 3)
        self.assertEqual(report["direct_raw_actor_snapshot_values"], len(records) * 16 * 7 * 3)
        self.assertEqual(report["unchanged_expectation_values"], len(records) * 16 * 3)
        for name in FLEET.CONTROL_FIELDS:
            with path.open(newline="") as stream: output = list(csv.DictReader(stream))
            output[-1][name] = str(int(output[-1][name]) + 1)
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(output[0])); writer.writeheader(); writer.writerows(output)
            report = FLEET.compare_control_feedback(header, records, self.path)
            self.assertEqual(report["mismatch_count"], 1); self.assertFalse(report["parity"])
            self.assertEqual(report["mismatches_per_actor"][-1], 1)
            self.assertEqual(report["exact_prefix_updates"], len(records) - 1)
            self.sidecar(records)

    def test_malformed_sidecars_and_missing_prior_layer_fail_closed(self):
        header, records = self.fixture()
        path = self.sidecar(records); original = path.read_text().splitlines()
        for rows, message in ((original[:-1], "Missing"), (original + original[-1:], "Extra"),
                              ([original[0], original[2], original[1], *original[3:]], "Reordered"),
                              ([original[0].replace("angle", "angle32", 1), *original[1:]], "header")):
            path.write_text("\n".join(rows) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_control_feedback(header, records, self.path)
        for function in (FLEET.write_binary, FLEET.write_combined_binary, FLEET.compare_driver_physics):
            with self.assertRaisesRegex(ValueError, "requires pedal"):
                function(self.path, header, records, control_feedback=True) if function != FLEET.compare_driver_physics else function(header, records, self.path, control_feedback=True)

    def test_aggregate_child_scopes_name_steering_as_retained_and_keep_inherited_coverage(self):
        from .drivers import DriverPhysicsTests
        from .drivetrain import DrivetrainFeedbackTests
        from .drivetrain import LimiterFeedbackTests
        from .drivetrain import RpmFeedbackTests

        header, records = self.fixture()
        DriverPhysicsTests.sidecars(self, records); DrivetrainFeedbackTests.feedback_sidecar(self, records)
        RpmFeedbackTests.sidecar(self, records); LimiterFeedbackTests.sidecar(self, records)
        PedalFeedbackTests.sidecar(self, records); self.sidecar(records)
        combined = {"parity": True, "numerical_parity": True, "coverage_sufficient": True,
                    "compared_values": 17, "mismatch_count": 0, "exact_prefix_updates": len(records),
                    "whine": {}, "joined_ai": {}, "driver_retention": {}, "physical_synchronizations": []}
        with patch.object(FLEET, "compare_combined", return_value=combined):
            report = FLEET.compare_driver_physics(header, records, self.path, drivetrain_feedback=True,
                                                 rpm_feedback=True, limiter_feedback=True, pedal_feedback=True, control_feedback=True)
        self.assertTrue(report["parity"])
        self.assertEqual(report["physical"]["mode"], "retained-control-feedback")
        self.assertEqual(report["control_feedback"]["compared_values"], len(records) * 16 * 8 * 3)
        self.assertFalse(report["coverage_sufficient"])
        self.assertIn("pedal_feedback", report["reused_coverage_required"])
        for name in ("drivetrain_feedback", "rpm_feedback", "limiter_feedback", "pedal_feedback"):
            self.assertNotIn("steering150/274", report[name]["observed_inputs"][0])
            self.assertIn("all control filters retained", report[name]["observed_inputs"][0])
        self.assertIn("body-dependent steering sensitivity", report["physical"]["observed_inputs_each_update"][0])
        self.assertIn("all control filters", report["boundary_scope"])
        self.assertIn("all control-filter feedback", report["combined"]["driver_retention"]["inputs"])

    def test_cli_selects_one_grouped_control_transaction(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original complete interval")
        expected = {"parity": True, "physical_parity": True, "mismatch_count": 0, "exact_prefix_updates": len(records)}
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-control-feedback"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as binary, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_count, 1); self.assertEqual(len(execute.call_args.args[0]), 8)
        self.assertEqual(execute.call_args.args[0][1], "--compare-control-feedback")
        self.assertEqual(binary.call_count, 2)
        self.assertTrue(all(all(call.kwargs[name] for name in ("control_feedback", "pedal_feedback", "limiter_feedback", "rpm_feedback", "drivetrain_feedback")) for call in binary.call_args_list))
        self.assertTrue(journal.call_args.kwargs["control_feedback"]); self.assertTrue(compare.call_args.kwargs["control_feedback"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-control-feedback.json").read_text())
        self.assertTrue(report["complete_source_interval"]); self.assertIsNone(report["continuous_physics_parity"])
