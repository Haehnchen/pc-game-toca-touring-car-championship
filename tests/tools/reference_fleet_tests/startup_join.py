"""Fleet reference checks for startup join."""

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
    zlib,
)
from .cold_grid import ColdGridFactoryTests
from .cold_grid import ColdGridInitializationTests
from .drivers import DriverRetentionTests


class StartupJoinTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = DriverRetentionTests.fixture
    Reader = ColdGridInitializationTests.Reader
    refresh = staticmethod(ColdGridFactoryTests.refresh)

    def binaries(self, header, records, startup=True):
        fleet, journal = self.path.with_suffix(".fleet"), self.path.with_suffix(".journal")
        #Serializer tests isolate the existing binary contract from the
        #separately tested actual-boundary/provenance predicate.
        with patch.object(FLEET, "validate_startup_join_scope", return_value={"player_name": b"PLAYER 1", "car": 0}), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=True, **({"startup_join": True} if startup else {}))
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, **({"startup_join": True} if startup else {}))
        return fleet.read_bytes(), journal.read_bytes()

    def test_initial_owned_fields_and_status_are_removed_from_every_binary_copy(self):
        header, records = self.fixture(); before = self.binaries(header, records)
        actor = records[0]["ai_pre"]["actors"][1]
        for block, offset, form, value in (("body", 0xbc, "i", 123456), ("gameplay", 0xbc, "h", 12),
                                           ("gameplay", 0x96, "B", 7), ("dynamics", 0x148, "I", 99)):
            raw = bytearray(actor[block]); struct.pack_into("<" + form, raw, offset, value)
            actor[block] = bytes(raw); actor[block + "_hex"] = raw.hex()
        traffic = records[0]["ai_pre"]["shared"]["traffic_control"]
        status = bytearray(traffic["status"]); status[:16] = b"\x71" * 16; traffic["status"] = bytes(status)
        self.assertEqual(before, self.binaries(header, records))
        self.assertNotEqual(before, self.binaries(header, records, startup=False))

    def test_post_selector_modes_unclaimed_rank_tail_and_physical_context_stay_observed(self):
        header, records = self.fixture(); before = self.binaries(header, records)
        actor = records[0]["ai_pre"]["actors"][1]
        original = actor["body"]; body = bytearray(original); struct.pack_into("<I", body, 0x4f0, 0)
        actor["body"] = bytes(body); actor["body_hex"] = body.hex()
        self.assertNotEqual(before, self.binaries(header, records))
        actor["body"] = original; actor["body_hex"] = original.hex()
        game = bytearray(actor["gameplay"]); game[0xbe] ^= 1
        actor["gameplay"] = bytes(game); actor["gameplay_hex"] = game.hex()
        self.assertNotEqual(before, self.binaries(header, records))

    def test_startup_seed_is_removed_only_in_new_journal_and_local_name_prefix_is_zero(self):
        header, records = self.fixture(); header["combined_initial_rng"] = 0x12345678
        resolved = bytearray.fromhex(header["ai_initial_state"]["resolved_ai_hex"])
        resolved[:16] = b"IMPORTED\0TAILXYZ"
        checksum = f"{zlib.crc32(resolved):08x}"
        header["ai_initial_state"].update(resolved_ai_hex=resolved.hex(), resolved_ai_crc32=checksum)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                record[phase]["shared"]["ai_state"]["resolved_ai_crc32"] = checksum
        fleet, journal = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<I", journal, 16)[0], 0)
        self.assertEqual(struct.unpack_from("<I", self.binaries(header, records, startup=False)[1], 16)[0], 0x12345678)
        #TOCAFLET header64 then AI descriptor12 then the resolved driver rows.
        self.assertEqual(fleet[76:85], bytes(9))
        self.assertEqual(fleet[85:92], b"TAILXYZ")
        self.assertEqual(self.binaries(header, records, startup=False)[0][76:85], b"IMPORTED\0")
        self.assertEqual(len(FLEET.STARTUP_JOIN_ACTOR_FIELDS), 116)
        self.assertNotIn("body_mode", {field[1] for field in FLEET.STARTUP_JOIN_ACTOR_FIELDS})

    def test_actual_boundary_scope_rejects_missing_provenance_phase_changes_and_no_release(self):
        from .cold_grid import ColdGridSequenceComparisonTests

        header, records = ColdGridSequenceComparisonTests.fixture(self)
        header["local_actor"] = 14
        records[0]["ai_pre"]["actors"] = json.loads(json.dumps(records[0]["pre"]["actors"]))
        with patch.object(FLEET, "validate_combined_scope"):
            inputs = FLEET.validate_startup_join_scope(header, records); self.assertEqual(inputs["car"], 7)
            record = records[0]["ai_pre"]["actors"][3]
            body = bytearray.fromhex(record["body_hex"]); body[0xbc] ^= 1; record["body_hex"] = body.hex()
            with self.assertRaisesRegex(ValueError, "changed before first AI consumer"): FLEET.validate_startup_join_scope(header, records)
            body[0xbc] ^= 1; record["body_hex"] = body.hex()
            dynamics = bytearray.fromhex(record["dynamics_hex"]); struct.pack_into("<i", dynamics, 0x144, 1); record["dynamics_hex"] = dynamics.hex()
            with self.assertRaisesRegex(ValueError, "zero inherited first-AI"): FLEET.validate_startup_join_scope(header, records)
            struct.pack_into("<i", dynamics, 0x144, 0); record["dynamics_hex"] = dynamics.hex()
            for record in records: record["release_events"] = []
            with self.assertRaisesRegex(ValueError, "positive release"): FLEET.validate_startup_join_scope(header, records)
            header.pop("cold_grid_sequence_manifest")
            with self.assertRaisesRegex(ValueError, "actual --cold-grid-sequence"): FLEET.write_startup_join_binary(self.path, header, records)

    def test_cli_runs_join_with_one_entry_sidecar_and_combines_startup_failure_in_exit_status(self):
        header, records = self.fixture(); self.path.write_text("actual sequence joined source")
        combined = {"parity": True, "numerical_parity": True, "exact_prefix_updates": 3, "mismatch_count": 0}
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-join", "--output", str(self.path.parent)]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as fleet, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "write_startup_join_binary") as startup, patch.object(CLI.FLEET, "compare_combined", return_value=combined), \
                patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value={"parity": False}), \
                patch.object(CLI.FLEET, "startup_join_coverage", return_value={"sufficient": True}), \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 1)
        command = execute.call_args.args[0]; self.assertEqual(len(command), 8); self.assertEqual(command[1], "--compare-startup-join")
        self.assertTrue(command[-1].endswith("fleet-startup-inputs.bin"))
        self.assertTrue(fleet.call_args.kwargs["startup_join"]); self.assertTrue(journal.call_args.kwargs["startup_join"])
        self.assertIs(startup.call_args.args[1], header)
        report = json.loads((self.path.parent / "fleet-comparison-startup-retained-driver-rng.json").read_text())
        self.assertFalse(report["parity"]); self.assertTrue(report["combined"]["parity"])
        self.assertIsNone(report["physical_parity"]); self.assertIsNone(report["continuous_physics_parity"])
        for extra in (["--updates", "1"], ["--compare-driver-retention"], ["--compare-physical-feedback"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as result: CLI.main()
            self.assertEqual(result.exception.code, 2)


class StartupFirstSelectionTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = DriverRetentionTests.fixture

    def binary(self, header, records):
        with patch.object(FLEET, "validate_startup_first_selection_scope", return_value={"player_name": b"PLAYER 1", "car": 0}):
            FLEET.write_binary(self.path, header, records, compare_joined_ai=True, compare_combined_rng=True,
                               compare_driver_retention=True, startup_join=True, startup_first_selection=True)
        return self.path.read_bytes()

    @staticmethod
    def change(actor, block, offset, value):
        raw = bytearray.fromhex(actor[block + "_hex"]); struct.pack_into("<I", raw, offset, value)
        actor[block] = bytes(raw); actor[block + "_hex"] = raw.hex()

    def selection_fixture(self):
        header, records = self.fixture()
        for actor in range(16):
            self.change(records[0]["pre"]["actors"][actor], "body", 0x4f0, 1)
            self.change(records[0]["ai_pre"]["actors"][actor], "body", 0x4f0, int(actor not in (8, 9, 13, 14)))
            for phase in ("pre", "ai_pre"):
                for offset in (0x2a0, 0x2a4): self.change(records[0][phase]["actors"][actor], "dynamics", offset, 0)
        records[0]["ai_pre"]["rng"] = records[0]["pre"]["rng"]
        return header, records

    def test_first_mode_rollover_and_context_aliases_cannot_enter_any_binary_copy(self):
        header, records = self.fixture(); before = self.binary(header, records)
        for actor in records[0]["ai_pre"]["actors"]:
            for block, offset, value in (("body", 0x4f0, 4), ("dynamics", 0x2a0, 0x81234567), ("dynamics", 0x2a4, 0x76543210)):
                self.change(actor, block, offset, value)
        game = records[0]["ai_pre"]["actors"][0]
        self.change(game, "gameplay", 0x10, 1)
        provenance = records[0]["ai_pre"]["shared"]["provenance"]
        raw = bytearray(provenance["ai_globals"]); struct.pack_into("<i", raw, 0x20, 123456)
        provenance.update(ai_globals=bytes(raw), ai_globals_hex=raw.hex())
        self.assertEqual(before, self.binary(header, records))
        old = StartupJoinTests.binaries(self, header, records)[0]
        self.assertNotEqual(before, old)

    def test_later_modes_rollover_and_first_unclaimed_context_remain_observed(self):
        for index, block, offset in ((1, "body", 0x4f0), (1, "dynamics", 0x2a0), (0, "body", 0x180)):
            header, records = self.fixture(); before = self.binary(header, records)
            actor = records[index]["ai_pre"]["actors"][3]
            self.change(actor, block, offset, 3)
            with self.subTest(index=index, block=block, offset=offset): self.assertNotEqual(before, self.binary(header, records))

    def test_selector_scope_rejects_noninitialized_rollover_at_either_boundary(self):
        header, records = self.selection_fixture()
        with patch.object(FLEET, "validate_startup_join_scope", return_value={"car": 0}):
            self.assertEqual(FLEET.validate_startup_first_selection_scope(header, records), {"car": 0})
            for phase in ("pre", "ai_pre"):
                for offset in (0x2a0, 0x2a4):
                    self.change(records[0][phase]["actors"][3], "dynamics", offset, 1)
                    with self.subTest(phase=phase, offset=offset), self.assertRaisesRegex(ValueError, "initialized rollover"):
                        FLEET.validate_startup_first_selection_scope(header, records)
                    self.change(records[0][phase]["actors"][3], "dynamics", offset, 0)

    def write_rows(self, rows):
        with self.path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["group", "actor", "field", "value"])
            writer.writeheader(); writer.writerows(rows)

    def test_selector_comparison_distinguishes_source_phases_and_detects_corruption(self):
        header, records = self.selection_fixture(); rows = list(FLEET.startup_first_selection_expected(records)); self.write_rows(rows)
        with patch.object(FLEET, "validate_startup_first_selection_scope"):
            report = FLEET.compare_startup_first_selection(header, records, self.path)
            self.assertTrue(report["parity"]); self.assertEqual(report["compared_values"], 67)
            self.assertEqual(report["source_full_actors"], [8, 9, 13, 14])
            self.assertEqual([row["value"] for row in rows if row["field"] == "previous"], [1] * 16)
            rows[8 * 4 + 3]["value"] = 1; self.write_rows(rows)
            report = FLEET.compare_startup_first_selection(header, records, self.path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            self.assertTrue(report["coverage_sufficient"])
            for malformed in (rows[:-1], [rows[1], rows[0], *rows[2:]], [*rows, rows[-1]]):
                self.write_rows(malformed)
                with self.assertRaises(ValueError): FLEET.compare_startup_first_selection(header, records, self.path)

    def test_selector_coverage_does_not_accept_no_transition_or_random_consumption(self):
        for missing in ("transition", "rng"):
            header, records = self.selection_fixture()
            if missing == "transition":
                for actor in records[0]["ai_pre"]["actors"]: self.change(actor, "body", 0x4f0, 1)
            else: records[0]["ai_pre"]["rng"] ^= 1
            self.write_rows(list(FLEET.startup_first_selection_expected(records)))
            with patch.object(FLEET, "validate_startup_first_selection_scope"), self.subTest(missing=missing):
                report = FLEET.compare_startup_first_selection(header, records, self.path)
                self.assertTrue(report["numerical_parity"]); self.assertFalse(report["parity"])

    def test_cli_selects_new_mode_and_requires_selection_startup_and_joined_results(self):
        header, records = self.fixture(); self.path.write_text("startup selection source")
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-first-selection", "--output", str(self.path.parent)]
        for selection, startup, numerical, coverage, expected in ((True, True, True, True, 0), (False, True, True, True, 1), (True, False, True, True, 1), (True, True, False, True, 1), (True, True, True, False, 1)):
            combined = {"parity": False, "numerical_parity": numerical, "exact_prefix_updates": 3, "mismatch_count": 0}
            with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                    patch.object(CLI.FLEET, "write_binary") as fleet, patch.object(CLI.FLEET, "write_combined_binary"), patch.object(CLI.FLEET, "write_startup_join_binary"), \
                    patch.object(CLI.FLEET, "compare_combined", return_value=combined), patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value={"parity": startup}), \
                    patch.object(CLI.FLEET, "startup_join_coverage", return_value={"sufficient": coverage}), patch.object(CLI.FLEET, "compare_startup_first_selection", return_value={"parity": selection}), \
                    patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(CLI.main(), expected)
            self.assertEqual(execute.call_args.args[0][1], "--compare-startup-first-selection")
            self.assertTrue(fleet.call_args.kwargs["startup_first_selection"])
            report = json.loads((self.path.parent / "fleet-comparison-startup-first-selection.json").read_text())
            self.assertEqual(report["parity"], expected == 0); self.assertFalse(report["combined"]["parity"])
            self.assertIsNone(report["continuous_physics_parity"])
        for extra in (["--updates", "1"], ["--compare-startup-join"], ["--compare-dispatch-feedback"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as result: CLI.main()
            self.assertEqual(result.exception.code, 2)


class StartupJoinCoverageTests(TraceTestCase):
    def fixture(self):
        header = {"cold_grid_sequence": {"events": [{}, {}]}, "scenario": "accel"}
        records = [{"pre": {}, "consumed": {}, "post": {}, "ground": {}} for _ in range(64)]
        source = {"updates": 64, "actors_moved": list(range(16)), "session_states": [6, 9], "all_actors_moving_updates": 58}
        combined = {"parity": False, "numerical_parity": True, "coverage_sufficient": False,
                    "requested_updates": 64, "compared_updates": 64,
                    "joined_ai": {"positive_cold_release_coverage": True, "positive_route_control_random_coverage": True, "coverage_sufficient": True},
                    "wheel_queue": {"skid_dispatch": {"coverage": {"draws": 323}}},
                    "whine": {"coverage": {"rng_advance_calls": 2}, "positive_command_coverage": False},
                    "human_lamps": {"parity": False, "numerical_parity": True, "coverage_sufficient": False},
                    "brake_publication": {"coverage_sufficient": True},
                    "driver_state_checkpoint_values": 506693, "human_mapper_transactions": 64,
                    "later_driver_imports": 0, "later_rng_imports": 0}
        return header, records, source, combined

    def compare(self, header, records, source, combined):
        with patch.object(FLEET, "validate_startup_join_scope"), patch.object(FLEET, "Coverage") as activity:
            activity.return_value.summary.return_value = source
            return FLEET.startup_join_coverage(header, records, combined)

    def test_new_bounded_coverage_keeps_legacy_command_and_lamp_gaps_unchanged(self):
        header, records, source, combined = self.fixture(); before = json.loads(json.dumps(combined))
        coverage = self.compare(header, records, source, combined)
        self.assertTrue(coverage["sufficient"]); self.assertTrue(all(coverage["checks"].values()))
        self.assertFalse(coverage["legacy_combined_acceptance"]["parity"])
        self.assertFalse(coverage["uncovered_branches"]["positive_whine_commands"])
        self.assertFalse(coverage["uncovered_branches"]["human_lamp_on_off"])
        self.assertEqual(combined, before)

    def test_missing_startup_interval_actor_release_and_random_positives_fail_new_gate(self):
        for missing in ("second_init", "full_interval", "active_state", "actor", "movement", "release", "AI_random", "tire_random", "whine_random", "publication", "retained_state", "later_import"):
            header, records, source, combined = self.fixture()
            if missing == "second_init": header["cold_grid_sequence"]["events"].pop()
            elif missing == "full_interval": records.pop()
            elif missing == "active_state": source["session_states"] = [9]
            elif missing == "actor": source["actors_moved"].pop()
            elif missing == "movement": source["all_actors_moving_updates"] = 0
            elif missing == "release": combined["joined_ai"]["positive_cold_release_coverage"] = False
            elif missing == "AI_random": combined["joined_ai"]["positive_route_control_random_coverage"] = False
            elif missing == "tire_random": combined["wheel_queue"]["skid_dispatch"]["coverage"]["draws"] = 0
            elif missing == "whine_random": combined["whine"]["coverage"]["rng_advance_calls"] = 0
            elif missing == "publication": combined["brake_publication"]["coverage_sufficient"] = False
            elif missing == "retained_state": combined["driver_state_checkpoint_values"] = 0
            else: combined["later_rng_imports"] = 1
            with self.subTest(missing=missing): self.assertFalse(self.compare(header, records, source, combined)["sufficient"])


class StartupJoinCoverageCliTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = DriverRetentionTests.fixture
    def test_bounded_cli_accepts_only_new_coverage_and_both_numeric_results(self):
        header, records = self.fixture(); self.path.write_text("startup source")
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-startup-join", "--output", str(self.path.parent)]
        for startup, sufficient, numerical, expected in ((True, True, True, 0), (False, True, True, 1), (True, False, True, 1), (True, True, False, 1)):
            combined = {"parity": False, "numerical_parity": numerical, "coverage_sufficient": False, "exact_prefix_updates": 3, "mismatch_count": 0}
            with self.subTest(startup=startup, sufficient=sufficient, numerical=numerical), \
                    patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                    patch.object(CLI.FLEET, "write_binary"), patch.object(CLI.FLEET, "write_combined_binary"), patch.object(CLI.FLEET, "write_startup_join_binary"), \
                    patch.object(CLI.FLEET, "compare_combined", return_value=combined), patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value={"parity": startup}), \
                    patch.object(CLI.FLEET, "startup_join_coverage", return_value={"sufficient": sufficient}), \
                    patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")), patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(CLI.main(), expected)
            report = json.loads((self.path.parent / "fleet-comparison-startup-retained-driver-rng.json").read_text())
            self.assertFalse(report["combined"]["parity"]); self.assertEqual(report["parity"], expected == 0)
