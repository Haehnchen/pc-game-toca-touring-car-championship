"""Fleet reference checks for startup physics."""

import copy

from .support import (
    TraceTestCase,
    CLI,
    FLEET,
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
from .physics import DispatchFeedbackTests
from .startup_join import StartupFirstSelectionTests


class StartupPhysicalBodyTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = BrakePublicationTests.fixture
    flags = DispatchFeedbackTests.flags
    change = staticmethod(StartupFirstSelectionTests.change)

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        inputs = {"player_name": b"PLAYER 1", "car": 0}
        with patch.object(FLEET, "validate_startup_first_selection_scope", return_value=inputs), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=True,
                               startup_join=True, startup_first_selection=True, startup_physical_body=True, **self.flags)
            FLEET.write_binary(physical, header, records, driver_physics_inputs=True, startup_physical_body=True, **self.flags)
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, startup_join=True, startup_physical_body=True, **self.flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def test_only_52_body_definitions_175_bytes_are_erased_after_feedback_initial_copies(self):
        self.assertEqual(len(FLEET.STARTUP_PHYSICAL_BODY_FIELDS), 52)
        self.assertEqual(sum(struct.calcsize("<" + field[4]) for field in FLEET.STARTUP_PHYSICAL_BODY_FIELDS), 175)
        header, records = self.fixture(); before = self.binaries(header, records)
        #PRE restoration in allfeedback must not put these fields back after
        #the startup scrub, including the traffic copy and physical sidecar.
        for phase in ("pre", "ai_pre", "consumed"):
            for actor in records[0][phase]["actors"]:
                raw = bytearray.fromhex(actor["body_hex"])
                for _, _, _, offset, form in FLEET.STARTUP_PHYSICAL_BODY_FIELDS:
                    struct.pack_into("<" + form, raw, offset, 1)
                actor.update(body=bytes(raw), body_hex=raw.hex())
        self.assertEqual(before, self.binaries(header, records))

    def test_initial_physical_complements_remain_observed_and_later_expectations_do_not_seed(self):
        for offset in (0xc8, 0x1c4, 0x1e4, 0x1f0, 0x268):
            header, records = self.fixture(); before = self.binaries(header, records)
            self.change(records[0]["pre"]["actors"][3], "body", offset, 123456)
            with self.subTest(offset=offset): self.assertNotEqual(before[2], self.binaries(header, records)[2])
        header, records = self.fixture(); before = self.binaries(header, records)
        for record in records[1:]:
            for phase in ("pre", "ai_pre", "consumed"):
                for actor in record[phase]["actors"]: self.change(actor, "body", 0xbc, 123456)
        self.assertEqual(before, self.binaries(header, records))

    def test_first_body_csv_uses_pre_not_after_selection_and_rejects_corruption_or_layout(self):
        header, records = self.fixture()
        for actor in records[0]["pre"]["actors"]: self.change(actor, "body", 0x4f0, 1)
        rows = list(FLEET.startup_physical_body_expected(records))
        self.assertEqual(len(rows), 832)
        self.assertEqual([row["value"] for row in rows if row["field"] == "body_mode"], [1] * 16)
        def write(values):
            with self.path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["group", "actor", "field", "value"])
                writer.writeheader(); writer.writerows(values)
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            write(rows); self.assertTrue(FLEET.compare_startup_physical_body(header, records, self.path)["parity"])
            changed = [dict(row) for row in rows]; changed[0]["value"] ^= 1
            write(changed); report = FLEET.compare_startup_physical_body(header, records, self.path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            for malformed in (rows[:-1], [*rows, rows[-1]], [rows[1], rows[0], *rows[2:]]):
                write(malformed)
                with self.assertRaises(ValueError): FLEET.compare_startup_physical_body(header, records, self.path)

    def test_new_coverage_requires_body_handoff_and_actual_physical_interval_without_old_contact_gate(self):
        header, records = self.fixture()
        counts = {"full_updates": 4, "distant_updates": 12, "following_pre_updates": len(records) - 1,
                  "contact_updates": 0, "pending_force_updates": 0}
        comparison = {"combined": {"parity": False}, "parity": False, "coverage_sufficient": False,
                      "physical_feedback": {"positive_source_counts": counts, "compared_values": 100, "initial_physical_world_imports": 1, "later_physical_world_imports": 0},
                      "dispatch_feedback": {"coverage_sufficient": True}, "reused_coverage_required": ["pedal_feedback"]}
        body = {"compared_values": 832, "bytes_per_actor": 175}; selection = {"coverage_sufficient": True}
        with patch.object(FLEET, "startup_join_coverage", return_value={"sufficient": True}) as joined:
            before = json.loads(json.dumps(comparison))
            report = FLEET.startup_physical_body_coverage(header, records, comparison, body, selection)
            self.assertTrue(report["sufficient"]); self.assertFalse(report["old_aggregate_diagnostics"]["parity"])
            self.assertFalse(report["uncovered_branches"]["positive_contact_and_pending_force"])
            self.assertIs(joined.call_args.args[2], comparison["combined"]); self.assertEqual(comparison, before)
            for group, field, bad in ((body, "compared_values", 831), (body, "bytes_per_actor", 176),
                                      (counts, "full_updates", 0), (counts, "following_pre_updates", 0),
                                      (comparison["physical_feedback"], "later_physical_world_imports", 1),
                                      (comparison["dispatch_feedback"], "coverage_sufficient", False)):
                original = group[field]; group[field] = bad
                with self.subTest(field=field): self.assertFalse(FLEET.startup_physical_body_coverage(header, records, comparison, body, selection)["sufficient"])
                group[field] = original

    def test_cli_uses_one_pre_restore_and_bounds_acceptance_by_every_numeric_result(self):
        header, records = self.fixture(); self.path.write_text("startup physical source")
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-physical-body", "--output", str(self.path.parent)]
        for missing in (None, "startup", "selection", "body", "numerical", "coverage"):
            comparison = {"parity": False, "numerical_parity": missing != "numerical", "physical_parity": True, "continuous_physics_parity": True,
                          "mismatch_count": 0, "exact_prefix_updates": len(records), "combined": {"parity": False}}
            body = {"parity": missing != "body", "remaining_initial_context": "observed complement"}
            with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                    patch.object(CLI.FLEET, "write_binary") as fleet, patch.object(CLI.FLEET, "write_combined_binary") as journal, patch.object(CLI.FLEET, "write_startup_join_binary"), \
                    patch.object(CLI.FLEET, "compare_driver_physics", return_value=comparison), patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value={"parity": missing != "startup"}), \
                    patch.object(CLI.FLEET, "compare_startup_first_selection", return_value={"parity": missing != "selection"}), \
                    patch.object(CLI.FLEET, "compare_startup_physical_body", return_value=body), patch.object(CLI.FLEET, "startup_join_coverage", return_value={"sufficient": True}) as old_coverage, \
                    patch.object(CLI.FLEET, "startup_physical_body_coverage", return_value={"sufficient": missing != "coverage"}), \
                    patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
                with self.subTest(missing=missing): self.assertEqual(CLI.main(), 0 if missing is None else 1)
            command = execute.call_args.args[0]; self.assertEqual(len(command), 9); self.assertEqual(command[1], "--compare-startup-physical-body")
            self.assertTrue(fleet.call_args_list[0].kwargs["startup_join"]); self.assertTrue(fleet.call_args_list[1].kwargs["startup_physical_body"])
            self.assertTrue(journal.call_args.kwargs["dispatch_feedback"]); self.assertIs(old_coverage.call_args.args[2], comparison["combined"])
            report = json.loads((self.path.parent / "fleet-comparison-startup-physical-body.json").read_text())
            self.assertFalse(report["driver_physics"]["parity"]); self.assertEqual(report["parity"], missing is None)
        for extra in (["--updates", "1"], ["--compare-startup-first-selection"], ["--compare-physical-feedback"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as result: CLI.main()
            self.assertEqual(result.exception.code, 2)

    def test_new_mode_cannot_be_used_as_a_generic_startup_physical_escape(self):
        header, records = self.fixture()
        for function in (FLEET.write_binary, FLEET.write_combined_binary):
            with self.assertRaisesRegex(ValueError, "Startup physical BODY requires"):
                function(self.path, header, records, startup_physical_body=True)


class StartupControlCalibrationTests(TraceTestCase):
    trace_filename = 'driver'
    flags = DispatchFeedbackTests.flags
    change = staticmethod(StartupFirstSelectionTests.change)

    def fixture(self):
        from .drivers import BrakePublicationTests

        header, records = BrakePublicationTests.fixture(self)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                for actor, entry in enumerate(record[phase]["actors"]):
                    values = (0x6000 + actor * 100, 0x6000 + actor * 100, 0xccc, 65536, 0x1ae4,
                              0x8000 + actor * 100, 0xccc, 0x3333)
                    for (_, offset), value in zip(FLEET.STARTUP_CONTROL_CALIBRATION_FIELDS, values):
                        self.change(entry, "dynamics", offset, value)
        return header, records

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        with patch.object(FLEET, "validate_startup_first_selection_scope", return_value={"player_name": b"PLAYER 1", "car": 0}), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=True,
                               startup_join=True, startup_first_selection=True, startup_physical_body=True, startup_control_calibration=True, **self.flags)
            FLEET.write_binary(physical, header, records, driver_physics_inputs=True, startup_physical_body=True, startup_control_calibration=True, **self.flags)
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, startup_join=True, startup_physical_body=True, **self.flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def test_first_calibration_aliases_are_erased_from_all_three_full_snapshot_copies(self):
        header, records = self.fixture(); before = self.binaries(header, records)
        self.assertEqual(len(FLEET.STARTUP_CONTROL_CALIBRATION_FIELDS), 8)
        for phase in ("pre", "ai_pre", "consumed"):
            for actor in records[0][phase]["actors"]:
                for _, offset in FLEET.STARTUP_CONTROL_CALIBRATION_FIELDS: self.change(actor, "dynamics", offset, 123456)
        self.assertEqual(before, self.binaries(header, records))
        self.assertNotEqual(before, StartupPhysicalBodyTests.binaries(self, header, records))

    def test_other_initial_words_and_later_current_and_baseline_copies_remain_observed(self):
        for index, offset in ((0, 0x20), (0, 0x34), (1, 0x0c), (1, 0x10)):
            header, records = self.fixture(); before = self.binaries(header, records)
            for phase in ("pre", "ai_pre", "consumed"):
                self.change(records[index][phase]["actors"][3], "dynamics", offset, 76543)
            with self.subTest(index=index, offset=offset): self.assertNotEqual(before, self.binaries(header, records))

    def write_rows(self, rows):
        with self.path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["phase", "actor", "field", "value"])
            writer.writeheader(); writer.writerows(rows)

    def test_calibration_comparison_reports_generated_pre_and_actual_restored_consumer(self):
        header, records = self.fixture(); rows = list(FLEET.startup_control_calibration_expected(records))
        self.assertEqual(len(rows), 256)
        self.assertEqual([row["phase"] for row in rows], ["first_pre"] * 128 + ["restored"] * 128)
        self.change(records[0]["consumed"]["actors"][3], "dynamics", 0x10, 98765)
        self.assertEqual(rows, list(FLEET.startup_control_calibration_expected(records)))
        self.write_rows(rows)
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            report = FLEET.compare_startup_control_calibration(header, records, self.path)
            self.assertTrue(report["parity"]); self.assertEqual(report["bytes_per_actor"], 32)
            self.assertIn("writes current maximum00c", report["mutability"])
            rows[128 + 3 * 8 + 1]["value"] += 1; self.write_rows(rows)
            report = FLEET.compare_startup_control_calibration(header, records, self.path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            self.assertEqual(report["phases"]["first_pre"]["mismatch_count"], 0)
            self.assertEqual(report["phases"]["restored"]["mismatch_count"], 1)
            for malformed in (rows[:-1], [*rows, rows[-1]], [rows[1], rows[0], *rows[2:]]):
                self.write_rows(malformed)
                with self.assertRaises(ValueError): FLEET.compare_startup_control_calibration(header, records, self.path)

    def test_positive_calibration_requires_real_car_variation_and_cold_baseline_equality(self):
        for missing in ("variation", "baseline"):
            header, records = self.fixture()
            if missing == "variation":
                for actor in records[0]["pre"]["actors"]:
                    for offset in (0x0c, 0x10): self.change(actor, "dynamics", offset, 65536)
            else: self.change(records[0]["pre"]["actors"][3], "dynamics", 0x10, 65536)
            self.write_rows(list(FLEET.startup_control_calibration_expected(records)))
            with patch.object(FLEET, "validate_startup_first_selection_scope"), self.subTest(missing=missing):
                report = FLEET.compare_startup_control_calibration(header, records, self.path)
                self.assertTrue(report["numerical_parity"]); self.assertFalse(report["parity"])

    def test_cli_selects_new_mode_and_includes_calibration_failure_in_bounded_gate(self):
        header, records = self.fixture(); self.path.write_text("startup control calibration source")
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-control-calibration", "--output", str(self.path.parent)]
        for calibration_ok in (True, False):
            comparison = {"parity": False, "numerical_parity": True, "physical_parity": True, "continuous_physics_parity": True,
                          "mismatch_count": 0, "exact_prefix_updates": len(records), "combined": {"parity": False}}
            calibration = {"parity": calibration_ok, "remaining_context": "other context observed"}
            with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                    patch.object(CLI.FLEET, "write_binary") as fleet, patch.object(CLI.FLEET, "write_combined_binary"), patch.object(CLI.FLEET, "write_startup_join_binary"), \
                    patch.object(CLI.FLEET, "compare_driver_physics", return_value=comparison), patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value={"parity": True}), \
                    patch.object(CLI.FLEET, "compare_startup_first_selection", return_value={"parity": True}), patch.object(CLI.FLEET, "compare_startup_physical_body", return_value={"parity": True, "remaining_initial_context": "observed"}), \
                    patch.object(CLI.FLEET, "startup_join_coverage", return_value={"sufficient": True}), patch.object(CLI.FLEET, "startup_physical_body_coverage", return_value={"sufficient": True}), \
                    patch.object(CLI.FLEET, "compare_startup_control_calibration", return_value=calibration), \
                    patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(CLI.main(), 0 if calibration_ok else 1)
            self.assertEqual(len(execute.call_args.args[0]), 9); self.assertEqual(execute.call_args.args[0][1], "--compare-startup-control-calibration")
            self.assertTrue(all(call.kwargs["startup_control_calibration"] for call in fleet.call_args_list))
            report = json.loads((self.path.parent / "fleet-comparison-startup-control-calibration.json").read_text())
            self.assertEqual(report["parity"], calibration_ok); self.assertFalse(report["driver_physics"]["parity"])
        for extra in (["--updates", "1"], ["--compare-startup-physical-body"], ["--compare-control-feedback"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as result: CLI.main()
            self.assertEqual(result.exception.code, 2)

    def test_calibration_opt_in_requires_the_body_handoff(self):
        header, records = self.fixture()
        with self.assertRaisesRegex(ValueError, "requires the startup physical BODY"):
            FLEET.write_binary(self.path, header, records, startup_control_calibration=True)


class StartupContactCacheTests(TraceTestCase):
    trace_filename = 'driver'
    flags = DispatchFeedbackTests.flags
    change = staticmethod(StartupFirstSelectionTests.change)

    @staticmethod
    def change_cache(entry, block, offset, form, value):
        raw = bytearray.fromhex(entry[block + "_hex"]); struct.pack_into("<" + form, raw, offset, value)
        entry[block] = bytes(raw); entry[block + "_hex"] = raw.hex()

    def fixture(self):
        header, records = StartupControlCalibrationTests.fixture(self)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                for actor, entry in enumerate(record[phase]["actors"]):
                    for field, block, offset, form in FLEET.STARTUP_CONTACT_CACHE_FIELDS:
                        index = int(field.rsplit("_", 1)[1])
                        value = (100000 + actor * 100 + index * 3 if field.startswith("height_") else
                                 (-1000 - actor, 65000, 1000 + actor)[index] if field.startswith("normal_") else
                                 2000 + actor * 10 + index if field.startswith("triangle_") else 0)
                        self.change_cache(entry, block, offset, form, value)
        return header, records

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        with patch.object(FLEET, "validate_startup_first_selection_scope", return_value={"player_name": b"PLAYER 1", "car": 0}), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=True,
                               startup_join=True, startup_first_selection=True, startup_physical_body=True, startup_control_calibration=True, startup_contact_cache=True, **self.flags)
            FLEET.write_binary(physical, header, records, driver_physics_inputs=True, startup_physical_body=True,
                               startup_control_calibration=True, startup_contact_cache=True, **self.flags)
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, startup_join=True, startup_physical_body=True, **self.flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def test_first_cache_aliases_are_erased_with_exact_widths_from_all_three_full_copies(self):
        self.assertEqual(len(FLEET.STARTUP_CONTACT_CACHE_FIELDS), 27)
        self.assertEqual(sum(struct.calcsize("<" + field[3]) for field in FLEET.STARTUP_CONTACT_CACHE_FIELDS), 68)
        header, records = self.fixture(); before = self.binaries(header, records)
        for phase in ("pre", "ai_pre", "consumed"):
            for entry in records[0][phase]["actors"]:
                for _, block, offset, form in FLEET.STARTUP_CONTACT_CACHE_FIELDS:
                    self.change_cache(entry, block, offset, form, 12)
        self.assertEqual(before, self.binaries(header, records))
        self.assertNotEqual(before, StartupControlCalibrationTests.binaries(self, header, records))

    def test_inherited_support_tails_adjacent_configuration_and_force_history_remain_observed(self):
        for block, offset, form in (("body", 0x51b, "B"), ("body", 0x525, "B"), ("body", 0x1f0, "i"), ("dynamics", 0x276, "H")):
            header, records = self.fixture(); before = self.binaries(header, records)
            #The physical cache/force ranges come from PRE; adjacent steering
            #configuration remains the existing consumed-phase observation.
            phase = "consumed" if block == "dynamics" else "pre"
            self.change_cache(records[0][phase]["actors"][3], block, offset, form, 1)
            with self.subTest(block=block, offset=offset): self.assertTrue(before[2] != self.binaries(header, records)[2])
        header, records = self.fixture(); before = self.binaries(header, records)
        for record in records[1:]:
            for phase in ("pre", "ai_pre", "consumed"):
                for entry in record[phase]["actors"]:
                    for _, block, offset, form in FLEET.STARTUP_CONTACT_CACHE_FIELDS: self.change_cache(entry, block, offset, form, 12)
        self.assertEqual(before, self.binaries(header, records))

    def write_rows(self, rows):
        with self.path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["phase", "actor", "field", "value"])
            writer.writeheader(); writer.writerows(rows)

    def test_cache_comparison_distinguishes_generated_and_restored_consumers_and_detects_corruption(self):
        header, records = self.fixture(); rows = list(FLEET.startup_contact_cache_expected(records)); self.write_rows(rows)
        self.assertEqual(len(rows), 864)
        self.assertEqual([row["phase"] for row in rows], ["first_pre"] * 432 + ["restored"] * 432)
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            report = FLEET.compare_startup_contact_cache(header, records, self.path)
            self.assertTrue(report["parity"]); self.assertEqual(report["surface_classes"], [0])
            self.assertFalse(report["positive_nonzero_surface_class"])
            self.assertLess(next(row["value"] for row in rows if row["field"] == "normal_0"), 0)
            rows[432 + 3 * 27 + 11]["value"] += 1; self.write_rows(rows)
            report = FLEET.compare_startup_contact_cache(header, records, self.path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            self.assertEqual(report["phases"]["restored"]["mismatch_count"], 1)
            for malformed in (rows[:-1], [*rows, rows[-1]], [rows[1], rows[0], *rows[2:]]):
                self.write_rows(malformed)
                with self.assertRaises(ValueError): FLEET.compare_startup_contact_cache(header, records, self.path)

    def test_geometry_coverage_does_not_turn_uniform_zero_caches_into_positive_loader_evidence(self):
        header, records = self.fixture()
        for entry in records[0]["pre"]["actors"]:
            for _, block, offset, form in FLEET.STARTUP_CONTACT_CACHE_FIELDS: self.change_cache(entry, block, offset, form, 0)
        self.write_rows(list(FLEET.startup_contact_cache_expected(records)))
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            report = FLEET.compare_startup_contact_cache(header, records, self.path)
        self.assertTrue(report["numerical_parity"]); self.assertFalse(report["coverage_sufficient"]); self.assertFalse(report["parity"])

    def test_cli_selects_cache_mode_and_combines_its_failure_with_prior_bounded_results(self):
        header, records = self.fixture(); self.path.write_text("startup contact cache source")
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-contact-cache", "--output", str(self.path.parent)]
        for cache_ok in (True, False):
            comparison = {"parity": False, "numerical_parity": True, "physical_parity": True, "continuous_physics_parity": True,
                          "mismatch_count": 0, "exact_prefix_updates": len(records), "combined": {"parity": False}}
            with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                    patch.object(CLI.FLEET, "write_binary") as fleet, patch.object(CLI.FLEET, "write_combined_binary"), patch.object(CLI.FLEET, "write_startup_join_binary"), \
                    patch.object(CLI.FLEET, "compare_driver_physics", return_value=comparison), patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value={"parity": True}), \
                    patch.object(CLI.FLEET, "compare_startup_first_selection", return_value={"parity": True}), patch.object(CLI.FLEET, "compare_startup_physical_body", return_value={"parity": True, "remaining_initial_context": "observed"}), \
                    patch.object(CLI.FLEET, "startup_join_coverage", return_value={"sufficient": True}), patch.object(CLI.FLEET, "startup_physical_body_coverage", return_value={"sufficient": True}), \
                    patch.object(CLI.FLEET, "compare_startup_control_calibration", return_value={"parity": True, "remaining_context": "observed"}), \
                    patch.object(CLI.FLEET, "compare_startup_contact_cache", return_value={"parity": cache_ok, "remaining_context": "inherited support tails observed"}), \
                    patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(CLI.main(), 0 if cache_ok else 1)
            self.assertEqual(len(execute.call_args.args[0]), 9); self.assertEqual(execute.call_args.args[0][1], "--compare-startup-contact-cache")
            self.assertTrue(all(call.kwargs["startup_contact_cache"] for call in fleet.call_args_list))
            report = json.loads((self.path.parent / "fleet-comparison-startup-contact-cache.json").read_text())
            self.assertEqual(report["parity"], cache_ok); self.assertFalse(report["driver_physics"]["parity"])
        for extra in (["--updates", "1"], ["--compare-startup-control-calibration"], ["--compare-dispatch-feedback"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as result: CLI.main()
            self.assertEqual(result.exception.code, 2)

    def test_contact_cache_opt_in_requires_the_calibration_handoff(self):
        header, records = self.fixture()
        with self.assertRaisesRegex(ValueError, "requires the startup control calibration"):
            FLEET.write_binary(self.path, header, records, startup_contact_cache=True)


class StartupWorldResetTests(TraceTestCase):
    trace_filename = 'driver'
    flags = DispatchFeedbackTests.flags
    change = staticmethod(StartupFirstSelectionTests.change)
    change_cache = staticmethod(StartupContactCacheTests.change_cache)
    def fixture(self):
        header, records = StartupContactCacheTests.fixture(self)
        cone, post, sign, model = bytearray(0xcc), bytearray(0x13c), bytearray(0x40), bytearray(68)
        for axis in range(3):
            struct.pack_into("<i", cone, 0x48 + axis * 16, 65536)
            struct.pack_into("<i", post, 0x48 + axis * 20, 65536)
            struct.pack_into("<i", post, 0xc8 + axis * 16, 65536)
        struct.pack_into("<3i", cone, 0x6c, 100, 200, -300); cone[0xc0] = 4
        for offset in (0x78, 0xec): struct.pack_into("<3i", post, offset, 400, 500, -600)
        struct.pack_into("<3i", post, 0x104, 0, 65536, 0)
        for offset in (0x114, 0x120): struct.pack_into("<3i", post, offset, 400, 500 + 0x8666, -600)
        struct.pack_into("<3i", sign, 8, 700, 800, -900); struct.pack_into("<3i", sign, 0x14, 701, 800, -900)
        struct.pack_into("<3i", sign, 0x20, 0, 65536, 0)
        struct.pack_into("<16f", model, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1.25, 2.5, -3.75, 1); model[64] = 1
        for record in records:
            for phase in ("pre", "ai_pre", "consumed"):
                pools = record[phase]["shared"]["props"]
                for name, raw in (("cones", cone), ("posts", post), ("signs", sign)):
                    pools[name].update(count=1, records=bytes(raw), records_hex=raw.hex())
                pools["signs"].update(model_records=bytes(model), model_records_hex=model.hex())
        #This fixture starts from the shared brake-publication transactions;
        #its first PRE must now represent a fresh cold constructor boundary.
        for actor in records[0]["pre"]["actors"]:
            for _, block, offset, size in FLEET.STARTUP_COLD_ACTOR_FIELDS:
                raw = bytearray(actor[block]); raw[offset:offset + size] = bytes(size)
                actor.update({block: bytes(raw), block + "_hex": raw.hex()})
        return header, records

    @staticmethod
    def change_pair(snapshot, pair, handle=None, touching=None):
        shared = snapshot["shared"]; raw = bytearray(shared["pair_matrix"])
        if handle is not None: struct.pack_into("<I", raw, pair * 8, handle)
        if touching is not None: struct.pack_into("<I", raw, pair * 8 + 4, touching)
        shared.update(pair_matrix=bytes(raw), pair_matrix_hex=raw.hex())

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        with patch.object(FLEET, "validate_startup_first_selection_scope", return_value={"player_name": b"PLAYER 1", "car": 0}), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=True,
                               startup_join=True, startup_first_selection=True, startup_physical_body=True, startup_control_calibration=True,
                               startup_contact_cache=True, startup_world_reset=True, **self.flags)
            FLEET.write_binary(physical, header, records, driver_physics_inputs=True, startup_physical_body=True,
                               startup_control_calibration=True, startup_contact_cache=True, startup_world_reset=True, **self.flags)
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, startup_join=True, startup_physical_body=True, startup_world_reset=True, **self.flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def test_first_contact_state_and_unused_handles_are_omitted(self):
        raw = b"".join(struct.pack("<2I", 0x90000000 + pair, pair % 2 + 1) for pair in range(256))
        masked = FLEET.startup_world_reset_input(raw)
        for pair in range(256):
            self.assertEqual(masked[pair * 8:pair * 8 + 4], bytes(4))
            self.assertEqual(masked[pair * 8 + 4:pair * 8 + 8], bytes(4))
        header, records = self.fixture(); before = self.binaries(header, records)
        self.assertNotEqual(before, StartupContactCacheTests.binaries(self, header, records))
        for phase in ("pre", "ai_pre", "consumed"):
            for pair in range(256): self.change_pair(records[0][phase], pair, touching=1)
        self.assertEqual(before, self.binaries(header, records))
        self.assertTrue(before != StartupContactCacheTests.binaries(self, header, records))

    def test_both_streams_omit_all_initial_and_later_owned_world_groups(self):
        header, records = self.fixture(); before = self.binaries(header, records)
        for record in records:
            for phase in ("pre", "ai_pre", "consumed"):
                snapshot = record[phase]; shared = snapshot["shared"]
                snapshot["roughness_cursor"] = 1
                shared.update(damage_cycle=1, detached_count=1)
                for name in ("pair_matrix", "damage_scratch", "damage_levels", "broken_features", "detached_pool", "detached_free", "barriers"):
                    raw = bytes([0xa5]) * len(shared[name]); shared.update({name: raw, name + "_hex": raw.hex()})
                for pool in shared["props"].values():
                    for name in ("records", "model_records"):
                        if name in pool:
                            raw = bytes([0xa5]) * len(pool[name]); pool.update({name: raw, name + "_hex": raw.hex()})
                for actor in snapshot["actors"]:
                    body = bytearray(actor["body"]); struct.pack_into("<H", body, 0x4ec, 123); body[0x514] = 2
                    body[0x529:0x539] = bytes([3] * 16)
                    actor.update(body=bytes(body), body_hex=body.hex())
        self.assertEqual(before, self.binaries(header, records))

    def test_initial_consumed_geometry_is_expected_but_dormant_payloads_are_not_claimed(self):
        _, records = self.fixture()
        values = {(field, index): value for field, index, value in FLEET.startup_world_initial_values(records[0]["pre"])}
        self.assertEqual(values["cone_position_2", 0], -300)
        self.assertEqual(values["post_saved_position_0", 0], 400)
        self.assertEqual(values["sign_transform_12", 0], 81920)
        self.assertEqual(values["detached_free", 0], 1)
        self.assertNotIn(("cone_normal_1", 0), values)
        self.assertNotIn(("sign_pivot_0", 0), values)
        snapshot = copy.deepcopy(records[0]["pre"])
        for name, offset in (("cones", 0x78), ("posts", 0x88), ("signs", 0x30)):
            pool = snapshot["shared"]["props"][name]; raw = bytearray(pool["records"]); raw[offset:offset + 4] = b"\x01" * 4
            pool.update(records=bytes(raw), records_hex=raw.hex())
        self.assertEqual(FLEET.startup_world_initial_values(snapshot), FLEET.startup_world_initial_values(records[0]["pre"]))

    def test_cold_actor_and_identity_aliases_are_omitted_before_driver_and_physical_restore(self):
        header, records = self.fixture(); before = self.binaries(header, records)
        for phase in ("pre", "ai_pre", "consumed"):
            snapshot = records[0][phase]; shared = snapshot["shared"]
            for name, raw in (("ownership", bytes([7] * 16)), ("limiter_active", struct.pack("<16I", *([1] * 16))), ("limiter_load", struct.pack("<16i", *([123] * 16)))):
                shared.update({name: raw, name + "_hex": raw.hex()})
            for actor in snapshot["actors"]:
                for name, block, offset, size in FLEET.STARTUP_COLD_ACTOR_FIELDS:
                    if phase == "consumed" and name == "raw_axes": continue #The real human mapper remains a journal input.
                    raw = bytearray(actor[block]); raw[offset:offset + size] = bytes([0x5a] * size)
                    actor.update({block: bytes(raw), block + "_hex": raw.hex()})
        self.assertEqual(before, self.binaries(header, records))
        self.assertEqual(len(FLEET.STARTUP_COLD_ACTOR_FIELDS), 18)
        self.assertEqual(sum(field[3] for field in FLEET.STARTUP_COLD_ACTOR_FIELDS), 183)

    def test_unclaimed_storage_frame_metadata_and_actual_mapper_inputs_keep_their_contract(self):
        for context in ("pending_gear", "previous_position", "root", "later_owner", "mapped_axes"):
            header, records = self.fixture(); before = self.binaries(header, records)
            with self.subTest(context=context):
                if context == "pending_gear":
                    actor = records[0]["pre"]["actors"][0]; raw = bytearray(actor["dynamics"]); raw[0x334] ^= 1
                    actor.update(dynamics=bytes(raw), dynamics_hex=raw.hex())
                elif context == "previous_position": self.change(records[0]["pre"]["actors"][0], "body", 0x268, 99)
                elif context == "root":
                    for phase in ("ai_pre", "consumed"): records[0][phase]["actors"][0]["root"]["detail"] = 1
                elif context == "later_owner":
                    for phase in ("ai_pre", "consumed"): records[1][phase]["shared"]["ownership"] = bytes([7] * 16)
                else: self.change(records[0]["consumed"]["actors"][0], "dynamics", 0x138, 100)
                after = self.binaries(header, records)
                if context == "mapped_axes":
                    self.assertEqual((before[0], before[2]), (after[0], after[2]))
                    self.assertNotEqual(before[1], after[1])
                else: self.assertNotEqual(before, after)

    def test_consumed_cold_actor_values_and_identity_remain_strict_expectations(self):
        header, records = self.fixture(); self.write_rows(list(FLEET.startup_world_reset_expected(records)))
        self.change(records[0]["pre"]["actors"][3], "dynamics", 0x2c0, 1)
        actor = records[0]["pre"]["actors"][4]; body = bytearray(actor["body"]); body[0x515] ^= 1
        actor.update(body=bytes(body), body_hex=body.hex())
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            report = FLEET.compare_startup_world_reset(header, records, self.path)
        self.assertFalse(report["numerical_parity"])
        self.assertEqual(report["mismatch_count"], 4) #Both the generated and live-consumer boundaries.
        self.assertFalse(report["coverage"]["observed_consumed_cold_actor_defaults"])

    def write_rows(self, rows):
        with self.path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["phase", "pair", "field", "value"])
            writer.writeheader(); writer.writerows(rows)

    def test_pair_reset_comparison_reads_pre_and_live_restored_consumer_and_detects_corruption(self):
        header, records = self.fixture(); rows = list(FLEET.startup_world_reset_expected(records)); self.write_rows(rows)
        count = len(FLEET.startup_world_initial_values(records[0]["pre"]))
        self.assertEqual(len(rows), count * 2)
        self.assertEqual([row["phase"] for row in rows], ["first_pre"] * count + ["restored"] * count)
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            report = FLEET.compare_startup_world_reset(header, records, self.path)
            self.assertTrue(report["parity"]); self.assertEqual(report["named_values_per_boundary"], count)
            self.assertEqual((report["initial_mutable_world_imports"], report["later_mutable_world_imports"]), (0, 0))
            self.assertIn("no positive contact transition", report["scope"])
            rows[count + 128]["value"] = 1; self.write_rows(rows)
            report = FLEET.compare_startup_world_reset(header, records, self.path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            self.assertEqual(report["phases"]["restored"]["mismatch_count"], 1)
            for malformed in (rows[:-1], [*rows, rows[-1]], [rows[1], rows[0], *rows[2:]]):
                self.write_rows(malformed)
                with self.assertRaises(ValueError): FLEET.compare_startup_world_reset(header, records, self.path)

    def test_observed_nonzero_initial_touching_cannot_pass_the_reset_contract(self):
        header, records = self.fixture(); self.change_pair(records[0]["pre"], 7, touching=1)
        self.write_rows(list(FLEET.startup_world_reset_expected(records)))
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            report = FLEET.compare_startup_world_reset(header, records, self.path)
        self.assertTrue(report["numerical_parity"]); self.assertFalse(report["coverage_sufficient"]); self.assertFalse(report["parity"])

    def test_cli_adds_world_reset_result_to_the_existing_bounded_parity_gate(self):
        header, records = self.fixture(); self.path.write_text("startup world reset source")
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-world-reset", "--output", str(self.path.parent)]
        for world_ok, cache_ok, clock_ok in ((True, True, True), (False, True, True), (True, False, True), (True, True, False)):
            comparison = {"parity": False, "numerical_parity": True, "physical_parity": True, "continuous_physics_parity": True,
                          "mismatch_count": 0, "exact_prefix_updates": len(records), "combined": {"parity": False}}
            with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                    patch.object(CLI.FLEET, "write_binary") as fleet, patch.object(CLI.FLEET, "write_combined_binary") as journal, patch.object(CLI.FLEET, "write_startup_join_binary"), \
                    patch.object(CLI.FLEET, "compare_driver_physics", return_value=comparison), patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value={"parity": True}), \
                    patch.object(CLI.FLEET, "compare_startup_first_selection", return_value={"parity": True}), patch.object(CLI.FLEET, "compare_startup_physical_body", return_value={"parity": True, "remaining_initial_context": "observed"}), \
                    patch.object(CLI.FLEET, "startup_join_coverage", return_value={"sufficient": True}), patch.object(CLI.FLEET, "startup_physical_body_coverage", return_value={"sufficient": True}), \
                    patch.object(CLI.FLEET, "compare_startup_control_calibration", return_value={"parity": True, "remaining_context": "observed"}), \
                    patch.object(CLI.FLEET, "compare_startup_contact_cache", return_value={"parity": cache_ok, "remaining_context": "observed"}), \
                    patch.object(CLI.FLEET, "compare_startup_world_reset", return_value={"parity": world_ok, "remaining_context": "unclaimed world state observed"}), \
                    patch.object(CLI.FLEET, "write_startup_clock_binary") as clock_input, \
                    patch.object(CLI.FLEET, "compare_startup_clock", return_value={"parity": clock_ok, "remaining_context": "external caller clocks observed"}) as clock_compare, \
                    patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(CLI.main(), 0 if world_ok and cache_ok and clock_ok else 1)
            command = execute.call_args.args[0]
            self.assertEqual(len(command), 10); self.assertEqual(command[1], "--compare-startup-world-reset")
            self.assertEqual(command[-1], str(self.path.parent / "fleet-startup-clock-inputs.bin"))
            self.assertEqual(clock_input.call_args.args, (self.path.parent / "fleet-startup-clock-inputs.bin", header, records))
            self.assertEqual(clock_compare.call_args.args, (header, records, self.path.parent / "fleet-combined-native.physical.csv"))
            self.assertTrue(all(call.kwargs["startup_world_reset"] for call in fleet.call_args_list))
            self.assertTrue(journal.call_args.kwargs["startup_world_reset"])
            report = json.loads((self.path.parent / "fleet-comparison-startup-world-reset.json").read_text())
            self.assertEqual(report["parity"], world_ok and cache_ok and clock_ok); self.assertFalse(report["driver_physics"]["parity"])
            self.assertEqual(report["clock_transactions"]["parity"], clock_ok)
            self.assertIsNone(report["autonomous_ai_parity"])
            self.assertIn("initial timer origins and caller clocks", report["remaining_initial_context"])
        for extra in (["--updates", "1"], ["--compare-startup-contact-cache"], ["--compare-dispatch-feedback"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as result: CLI.main()
            self.assertEqual(result.exception.code, 2)

    def test_world_reset_opt_in_requires_the_contact_cache_handoff(self):
        header, records = self.fixture()
        with self.assertRaisesRegex(ValueError, "requires the startup contact cache"):
            FLEET.write_binary(self.path, header, records, startup_world_reset=True)
