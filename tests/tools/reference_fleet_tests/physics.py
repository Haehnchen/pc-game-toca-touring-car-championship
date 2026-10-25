"""Fleet reference checks for physics."""

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
    write_trace,
)
from .drivers import BrakePublicationTests


class PhysicalFeedbackTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = BrakePublicationTests.fixture

    def binaries(self, header, records):
        flags = dict(drivetrain_feedback=True, rpm_feedback=True, limiter_feedback=True, pedal_feedback=True,
                     control_feedback=True, physical_feedback=True)
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True,
                           compare_driver_retention=True, **flags)
        FLEET.write_binary(physical, header, records, driver_physics_inputs=True, **flags)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, **flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    @staticmethod
    def change(snapshot, name, offset, value):
        entry = snapshot["actors"][15]
        raw = bytearray(entry[name]); struct.pack_into("<i", raw, offset, value)
        entry[name] = bytes(raw); entry[name + "_hex"] = raw.hex()

    def sidecar(self, records):
        rows = list(FLEET.physical_feedback_expected(records))
        path = Path(str(self.path) + ".physical-feedback.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_first_pre_initializes_both_views_and_later_pose_wheels_forces_are_expectations(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                for name, offset in (("body", 0xbc), ("body", 0x1f0), ("dynamics", 0xfc)):
                    self.change(record[phase], name, offset, 123456789)
        self.assertEqual(before, self.binaries(header, records))
        self.change(records[0]["pre"], "body", 0xbc, -123456789)
        after = self.binaries(header, records)
        self.assertNotEqual(before[0], after[0]); self.assertEqual(before[1], after[1]); self.assertNotEqual(before[2], after[2])

    def test_phase_specific_physical_corruption_and_missing_checkpoint_fail_closed(self):
        header, records = self.fixture()
        path = self.sidecar(records)
        self.assertTrue(FLEET.compare_physical_feedback(header, records, self.path)["parity"])
        rows = list(FLEET.physical_feedback_expected(records))
        #Contacts, ground and following PRE each use their actual raw snapshot.
        for index, event in ((0, "contacts"), (0, "ground"), (1, "pre")):
            ordinal = next(i for i, (kind, phase, _) in enumerate(FLEET.combined_events(records[index])) if kind == "boundary" and phase == event)
            selected = next(row for row in rows if row["id"] == index and row["event"] == ordinal and row["actor"] == 15)
            selected["body_188"] += 1
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
            report = FLEET.compare_physical_feedback(header, records, self.path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            self.assertEqual(report["exact_prefix_updates"], index)
            selected["body_188"] -= 1
        path.write_text("\n".join(path.read_text().splitlines()[:-1]) + "\n")
        with self.assertRaisesRegex(ValueError, "Missing"): FLEET.compare_physical_feedback(header, records, self.path)

    def test_aggregate_distinguishes_new_physical_coverage_from_inherited_gaps(self):
        from .controls import ControlFeedbackTests
        from .drivers import DriverPhysicsTests
        from .drivetrain import DrivetrainFeedbackTests
        from .drivetrain import LimiterFeedbackTests
        from .controls import PedalFeedbackTests
        from .drivetrain import RpmFeedbackTests

        header, records = self.fixture()
        DriverPhysicsTests.sidecars(self, records); DrivetrainFeedbackTests.feedback_sidecar(self, records)
        RpmFeedbackTests.sidecar(self, records); LimiterFeedbackTests.sidecar(self, records)
        PedalFeedbackTests.sidecar(self, records); ControlFeedbackTests.sidecar(self, records); self.sidecar(records)
        combined = {"parity": True, "numerical_parity": True, "coverage_sufficient": True,
                    "compared_values": 17, "mismatch_count": 0, "exact_prefix_updates": len(records),
                    "whine": {}, "joined_ai": {}, "driver_retention": {}, "wheel_queue": {},
                    "physical_synchronizations": ["observed body position/basis/speed/contact context"]}
        with patch.object(FLEET, "compare_combined", return_value=combined):
            report = FLEET.compare_driver_physics(header, records, self.path, drivetrain_feedback=True, rpm_feedback=True,
                                                 limiter_feedback=True, pedal_feedback=True, control_feedback=True, physical_feedback=True)
        self.assertTrue(report["parity"]); self.assertTrue(report["continuous_physics_parity"])
        self.assertEqual(report["physical_world_imports"], 1)
        self.assertIn("pedal_feedback", report["reused_coverage_required"])
        self.assertEqual(report["new_feedback_coverage_sufficient"], report["physical_feedback"]["coverage_sufficient"])
        self.assertIn("retained generated body airborne/slip", report["combined"]["wheel_queue"]["physical_inputs"])
        self.assertIn("independent diagnostic", report["body_selection"]["role"])

    def test_cli_selects_continuous_physical_mode_and_all_prior_feedback(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original complete interval")
        expected = {"parity": True, "physical_parity": True, "continuous_physics_parity": True, "mismatch_count": 0, "exact_prefix_updates": len(records)}
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-physical-feedback"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as binary, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_args.args[0][1], "--compare-physical-feedback")
        self.assertTrue(all(call.kwargs["physical_feedback"] and call.kwargs["control_feedback"] for call in binary.call_args_list))
        self.assertTrue(journal.call_args.kwargs["physical_feedback"]); self.assertTrue(compare.call_args.kwargs["physical_feedback"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-physical-feedback.json").read_text())
        self.assertTrue(report["continuous_physics_parity"])


class DispatchFeedbackTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = BrakePublicationTests.fixture
    change = staticmethod(PhysicalFeedbackTests.change)
    flags = dict(drivetrain_feedback=True, rpm_feedback=True, limiter_feedback=True, pedal_feedback=True,
                 control_feedback=True, physical_feedback=True, dispatch_feedback=True)

    def binaries(self, header, records):
        fleet, journal, physical = (self.path.with_suffix(suffix) for suffix in (".fleet", ".journal", ".physical"))
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True,
                           compare_driver_retention=True, **self.flags)
        FLEET.write_binary(physical, header, records, driver_physics_inputs=True, **self.flags)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True, **self.flags)
        return fleet.read_bytes(), journal.read_bytes(), physical.read_bytes()

    def sidecar(self, records):
        rows = list(FLEET.dispatch_feedback_expected(records))
        path = Path(str(self.path) + ".dispatch-feedback.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_disabled_dispatch_option_preserves_previous_physical_streams_byte_exactly(self):
        header, records = self.fixture()
        previous = PhysicalFeedbackTests.binaries(self, header, records)
        with patch.object(self, "flags", {**self.flags, "dispatch_feedback": False}):
            self.assertEqual(previous, self.binaries(header, records))

    def test_only_first_pre_mode_vector_initializes_and_all_later_copies_are_expectations(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                self.change(record[phase], "body", 0x4f0, 4)
        self.assertEqual(before, self.binaries(header, records))
        self.change(records[0]["pre"], "body", 0x4f0, 0)
        after = self.binaries(header, records)
        self.assertEqual(before[:2], after[:2]); self.assertNotEqual(before[2], after[2])

    def test_selection_uses_retained_pre_gameplay_and_motion_not_snapshot_selector_copies(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        #These are all selector operands. Every later copy is an expectation;
        #the initial PRE remains the sole physical/driver initialization.
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                for name, offset, value in (("body", 0xb4, -98765), ("body", 0x51f, 6),
                                             ("dynamics", 0x2a0, 1), ("dynamics", 0x2a4, 1),
                                             ("dynamics", 0xdc, -98765), ("gameplay", 0xf0, 98765)):
                    self.change(record[phase], name, offset, value)
        with patch.object(FLEET, "body_selection_input", side_effect=AssertionError("Observed selector must not run")):
            self.assertEqual(before, self.binaries(header, records))
        self.change(records[0]["pre"], "gameplay", 0xf0, 76543)
        after = self.binaries(header, records)
        self.assertNotEqual(before[0], after[0]); self.assertEqual(before[1:], after[1:])

    def test_previous_human_axes_survive_pre_ai_and_only_actual_current_mapper_enters_journal(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        human = FLEET.retained_human_actors(records)[0]
        def right(snapshot, value):
            entry = snapshot["actors"][human]
            raw = bytearray(entry["dynamics"]); struct.pack_into("<i", raw, 0x13c, value)
            entry["dynamics"] = bytes(raw); entry["dynamics_hex"] = raw.hex()
        #The source traffic checkpoint precedes the current human mapper.
        #Later PRE copies cannot overwrite the previous journal-generated axes.
        right(records[1]["pre"], 65536); right(records[1]["ai_pre"], 65536)
        self.assertEqual(before, self.binaries(header, records))
        #422f60 is an explicit external transaction after403dd0. Its current
        #axes enter the journal, while driver/physical snapshot copies stay zero.
        right(records[1]["consumed"], 65536)
        after = self.binaries(header, records)
        self.assertEqual(before[0], after[0]); self.assertNotEqual(before[1], after[1]); self.assertEqual(before[2], after[2])

    def test_mode_expectations_preserve_pre_ai_current_physical_and_next_pre_phases(self):
        header, records = self.fixture()
        for phase, mode in (("pre", 1), ("ai_pre", 0), ("consumed", 0), ("contacts", 0), ("ground", 2), ("post", 2)):
            self.change(records[0][phase], "body", 0x4f0, mode)
        self.change(records[1]["pre"], "body", 0x4f0, 2)
        rows = list(FLEET.dispatch_feedback_expected(records))
        observed = {}
        for ordinal, (kind, phase, _) in enumerate(FLEET.combined_events(records[0])):
            if kind in ("boundary", "ai"):
                observed[phase if kind == "boundary" else "ai_return"] = next(row["mode"] for row in rows
                    if row["id"] == 0 and row["event"] == ordinal and row["actor"] == 15)
        self.assertEqual(observed, {"pre": 1, "ai_pre": 0, "ai_return": 0, "consumed": 0,
                                    "contacts": 0, "ground": 2, "skid_pre": 2, "post": 2})
        self.assertEqual(len(rows), len(records) * 16 * 8)
        self.assertEqual(next(row["mode"] for row in rows if row["id"] == 1 and row["actor"] == 15), 2)

    def test_corrupted_ai_pre_ground_and_following_pre_modes_fail_exact_phase(self):
        header, records = self.fixture()
        path = self.sidecar(records)
        rows = list(FLEET.dispatch_feedback_expected(records))
        self.assertTrue(FLEET.compare_dispatch_feedback(header, records, self.path)["parity"])
        for index, phase in ((0, "ai_pre"), (0, "ground"), (1, "pre")):
            ordinal = next(i for i, (kind, name, _) in enumerate(FLEET.combined_events(records[index])) if kind == "boundary" and name == phase)
            selected = next(row for row in rows if (row["id"], row["event"], row["actor"]) == (index, ordinal, 15))
            previous = selected["mode"]
            selected["mode"] = 4
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
            report = FLEET.compare_dispatch_feedback(header, records, self.path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            self.assertEqual(report["exact_prefix_updates"], index)
            self.assertEqual(report["first_mismatches"][0]["event"], ordinal)
            selected["mode"] = previous

    def test_missing_extra_reordered_and_wrong_header_mode_rows_fail_closed(self):
        header, records = self.fixture()
        path = self.sidecar(records); lines = path.read_text().splitlines()
        for changed, message in ((lines[:-1], "Missing"), ([*lines, lines[-1]], "Extra"),
                                 ([lines[0], lines[2], lines[1], *lines[3:]], "Reordered"),
                                 ([lines[0].replace("mode", "observed_mode"), *lines[1:]], "header")):
            path.write_text("\n".join(changed) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_dispatch_feedback(header, records, self.path)
        for function in (FLEET.write_binary, FLEET.write_combined_binary, FLEET.compare_driver_physics):
            with self.assertRaisesRegex(ValueError, "requires physical"):
                function(header, records, self.path, dispatch_feedback=True) if function == FLEET.compare_driver_physics else function(self.path, header, records, dispatch_feedback=True)

    def test_aggregate_adds_modes_preserves_physical_byte_checks_and_inherited_coverage(self):
        from .controls import ControlFeedbackTests
        from .drivers import DriverPhysicsTests
        from .drivetrain import DrivetrainFeedbackTests
        from .drivetrain import LimiterFeedbackTests
        from .controls import PedalFeedbackTests
        from .drivetrain import RpmFeedbackTests

        header, records = self.fixture()
        DriverPhysicsTests.sidecars(self, records); DrivetrainFeedbackTests.feedback_sidecar(self, records)
        RpmFeedbackTests.sidecar(self, records); LimiterFeedbackTests.sidecar(self, records)
        PedalFeedbackTests.sidecar(self, records); ControlFeedbackTests.sidecar(self, records)
        PhysicalFeedbackTests.sidecar(self, records); self.sidecar(records)
        combined = {"parity": True, "numerical_parity": True, "coverage_sufficient": True,
                    "compared_values": 17, "mismatch_count": 0, "exact_prefix_updates": len(records),
                    "whine": {}, "joined_ai": {"input_imports": "initial state"}, "driver_retention": {}, "wheel_queue": {}}
        with patch.object(FLEET, "compare_combined", return_value=combined):
            report = FLEET.compare_driver_physics(header, records, self.path, **self.flags)
        self.assertTrue(report["parity"]); self.assertTrue(report["continuous_physics_parity"])
        self.assertEqual(report["dispatch_feedback"]["compared_values"], len(records) * 16 * 8)
        physical_values = len(records) * 16 * 8 * len(FLEET.physical_feedback_values(records[0]["pre"]["actors"][0]))
        self.assertEqual(report["physical_feedback"]["compared_values"], physical_values)
        self.assertEqual(report["physical"]["mode"], "retained-dispatch-feedback")
        self.assertIn("before403dd0", report["body_selection"]["role"])
        self.assertIn("pedal_feedback", report["reused_coverage_required"])
        self.assertEqual(report["new_feedback_coverage_sufficient"], report["dispatch_feedback"]["coverage_sufficient"])

    def test_cli_dispatch_mode_selects_all_feedback_and_forwards_aggregate_parity(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original complete interval")
        expected = {"parity": False, "physical_parity": True, "continuous_physics_parity": False,
                    "mismatch_count": 1, "exact_prefix_updates": 0}
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-dispatch-feedback"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as binary, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 1)
        self.assertEqual(execute.call_args.args[0][1], "--compare-dispatch-feedback")
        self.assertEqual(len(execute.call_args.args[0]), 8)
        self.assertTrue(all(all(call.kwargs[name] for name in self.flags) for call in binary.call_args_list))
        self.assertTrue(journal.call_args.kwargs["dispatch_feedback"]); self.assertTrue(compare.call_args.kwargs["dispatch_feedback"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-dispatch-feedback.json").read_text())
        self.assertFalse(report["continuous_physics_parity"]); self.assertTrue(report["complete_source_interval"])


class DetachedLifetimeTests(TraceTestCase):
    trace_filename = 'trace.jsonl'

    @staticmethod
    def part(snapshot, active, timer, resting=True, part_type=2, count=None):
        shared = snapshot["shared"]; raw = bytearray.fromhex(shared["detached_pool_hex"])
        struct.pack_into("<2I", raw, 0, 0x4bf8b0, 0x4bf8f0)
        struct.pack_into("<I", raw, 0x108, int(active)); raw[0x10c] = part_type
        struct.pack_into("<iII", raw, 0x110, timer, 0, int(resting))
        free = struct.pack("<10I", int(not active), *([1] * 9))
        shared.update(detached_pool_hex=raw.hex(), detached_count=int(active) if count is None else count, detached_free_hex=free.hex())
        #Raw JSON fixtures stay hex-only; decoded cases retain both aliases.
        if "detached_pool" in shared: shared["detached_pool"] = bytes(raw)
        if "detached_free" in shared: shared["detached_free"] = free

    def fixture(self):
        header, records = fixture(4); header["initial_configuration"]["car_id"] = 7
        for index, record in enumerate(records):
            for phase, snapshot in ((phase, record[phase]) for phase in FLEET.BOUNDARIES):
                snapshot.update(car_id=7, race_clock=28000 + index * 40, update_counter=700 + index + int(phase != "pre"))
                body = bytearray.fromhex(snapshot["body_hex"]); body[0x516] = 7
                snapshot["body_hex"] = snapshot["actors"][0]["body_hex"] = body.hex()
                active = index < 3 and not (index == 2 and phase == "post")
                timer = 2 - index if index < 3 else 0
                if phase == "post" and index < 2: timer -= 1
                self.part(snapshot, active, timer)
            if index == 3:
                for phase in ("contacts", "ground", "post"): self.part(record[phase], True, 0, False, 3)
        batches, before = [], {"update_counter": 0, "race_clock": 0}
        for count, keys in [(1, ["Up"]), (1, ["Right"]), *[(32, [])] * 21, (26, [])]:
            after = {"update_counter": before["update_counter"] + count, "race_clock": before["race_clock"] + count * 40}
            batches.append({"keys_only": True, "requested_keys": keys, "updates": count, "before": before, "after": after,
                            "recorded_boundaries": False, "continuous_from_grid": False})
            before = after
        metadata = {"kind": "observed-detached-expiry-ready", "slot": 0, "type": 2, "requested_timer": 2, "first_rest_timer": 700,
                    "updates": 700, "maximum_updates": 1400, "keys_only": True, "before": {"update_counter": 0, "race_clock": 0}, "after": before,
                    "endpoint": {"active": 1, "free": 0, "resting": 1, "timer": 2, "count": 1, "sector": 0},
                    "endpoint_boundary": "421aa0-entry", "recorded_boundaries": False, "continuous_from_grid": False,
                    "source_policy_parameters": {"fixture": 1}, "phase_counts": {"approach": 2, "coast": 698}, "batches": batches,
                    "initial_configuration": dict(header["initial_configuration"])}
        header["unrecorded_keyboard_warmup"] = {"targeted_detached_expiry": metadata, "steps": [], "keys_only": True,
                                                 "recorded_boundaries": False, "continuous_from_grid": False}
        self.raw_records = records
        write_trace(self.path, header, records)
        return FLEET.read_trace(self.path)

    def compare(self, header, records, corrupt=False):
        rows = [{"id": record["id"], "phase": phase, "slot": slot, **FLEET.expected_detached(record[source], slot)}
                for record in records for phase, source in (("integration", "contacts"), ("ground", "ground"), ("post", "post")) for slot in range(10)]
        if corrupt: rows[80]["free"] = 0
        path = self.path.with_suffix(".csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return FLEET.compare_detached(records, path, header=header)

    def test_actual_slot_zero_then_expiry_then_reuse_and_short_prefix_gate(self):
        header, records = self.fixture(); report = self.compare(header, records)
        self.assertTrue(report["numerical_parity"] and report["coverage_sufficient"] and report["parity"])
        self.assertEqual(report["lifecycle_coverage"]["counts"]["timer_zero"], 1)
        self.assertEqual(report["lifecycle_coverage"]["counts"]["timer_expirations"], 1)
        self.assertEqual(report["lifecycle_coverage"]["positive_reuse_slots"], [0])
        expiry = report["lifecycle_coverage"]["events"]["timer_expirations"][0]
        self.assertEqual((expiry["update"], expiry["phase"], expiry["timer_before"], expiry["count_before"], expiry["count_after"]), (2, "post", 0, 1, 0))
        short = self.compare(header, records[:2])
        self.assertTrue(short["numerical_parity"]); self.assertFalse(short["coverage_sufficient"] or short["parity"])
        legacy = self.compare({}, records[:2])
        self.assertTrue(legacy["parity"]); self.assertFalse(legacy["chosen_expiry_required"])
        self.assertFalse(self.compare(header, records, corrupt=True)["numerical_parity"])

    def test_acquisition_and_replay_share_sparse_expiry_coverage_without_allocation(self):
        header, records = self.fixture(); records = records[:3]
        sparse = [FLEET.detached_lifecycle_record(record) for record in self.raw_records[:3]]
        self.assertEqual(sparse, [FLEET.detached_lifecycle_record(record) for record in records])
        for record in sparse:
            self.assertEqual(set(record), {"id", "pre", "consumed", "contacts", "ground", "post"})
            for phase in ("pre", "consumed", "contacts", "ground", "post"):
                self.assertEqual(set(record[phase]), {"shared", "race_clock", "update_counter"} if phase == "pre" else {"shared"})
                self.assertEqual(set(record[phase]["shared"]), {"detached_pool", "detached_free", "detached_count", "sector_visibility_hex",
                                                                 "sector_visibility_disabled", "sector_visibility_override"})
        acquisition = FLEET.detached_expiry_coverage(header, sparse)
        replay = self.compare(header, records)
        self.assertEqual(acquisition, {key: replay[key] for key in acquisition})
        self.assertTrue(acquisition["chosen_expiry_observed"] and acquisition["coverage_sufficient"])
        self.assertEqual({key: acquisition["lifecycle_coverage"]["counts"][key] for key in ("allocations", "landings", "reuses")},
                         {"allocations": 0, "landings": 0, "reuses": 0})
        short = FLEET.detached_expiry_coverage(header, sparse[:2])
        self.assertFalse(short["chosen_expiry_observed"] or short["coverage_sufficient"])

    def test_visibility_or_missing_count_decrease_cannot_masquerade_as_expiry(self):
        for missing in ("visible", "count", "zero"):
            header, records = self.fixture()
            if missing == "visible": records[2]["pre"]["shared"].update(sector_visibility_disabled=1, sector_visibility_hex=bytes(256).hex())
            elif missing == "count": records[2]["post"]["shared"]["detached_count"] = 1
            else:
                self.part(records[1]["post"], True, -1)
                for phase in ("pre", "consumed", "contacts", "ground"): self.part(records[2][phase], True, -1)
            with self.subTest(missing=missing):
                report = self.compare(header, records)
                self.assertTrue(report["numerical_parity"]); self.assertFalse(report["chosen_expiry_observed"] or report["parity"])

    def test_warmup_slot_endpoint_keys_and_counter_provenance_reject_tampering(self):
        for tamper in ("slot", "endpoint", "clock", "keys", "batch", "origin", "recorded"):
            header, records = self.fixture(); meta = header["unrecorded_keyboard_warmup"]["targeted_detached_expiry"]
            if tamper == "slot": meta["slot"] = 9
            elif tamper == "endpoint": meta["endpoint"]["active"] = True
            elif tamper == "clock": meta["after"]["race_clock"] += 1
            elif tamper == "keys": meta["batches"][0]["requested_keys"] = ["Escape"]
            elif tamper == "batch": meta["batches"][1]["before"] = {"update_counter": 0, "race_clock": 0}
            elif tamper == "origin": meta["first_rest_timer"] = 701
            else: meta["recorded_boundaries"] = True
            write_trace(self.path, header, self.raw_records)
            with self.subTest(tamper=tamper), self.assertRaises(ValueError): FLEET.read_trace(self.path)
