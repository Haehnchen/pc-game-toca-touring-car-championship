"""Fleet reference checks for clock."""

from .support import (
    TraceTestCase,
    FLEET,
    Path,
    copy,
    csv,
    io,
    json,
    patch,
    struct,
    subprocess,
    sys,
)
from .physics import DispatchFeedbackTests


class StartupClockTests(TraceTestCase):
    trace_filename = 'driver'

    @staticmethod
    def fixture():
        #The retained latch causes an extra update before the first sample.
        #First, split, capped and ordinary samples then have distinct IDs.
        times = [(2621, 1638425, True)] * 2 + [(65536, 65536, True)] * 4 + [(98304, 43690, True)] * 2 + [(65536, 65536, False)]
        counters = [100, 100, 200, 200, 400, 400, 800, 800, 900]
        differences = [17] * 4 + [200] * 2 + [400] * 2 + [100]
        records = []
        for index, (time, counter, difference) in enumerate(zip(times, counters, differences)):
            snapshot = {**dict(zip(FLEET.STARTUP_CLOCK_FIELDS, time)), "qpc_low": counter,
                        "qpc_divisor": 100, "qpc_difference": difference, "clock_first_sample": int(index < 2)}
            clock = 1000 + index * 4000
            phase = index if index < 4 else 3 if index == 4 else 4
            epoch = 13000 if index >= 3 else 0
            timers = (17000 if index >= 4 else 1000, 13000 if index >= 3 else 1000, min(clock, 13000), 1000, 1000)
            player = bytearray(24); struct.pack_into("<H", player, 22, phase)
            flags = struct.pack("<4I", 0, 1, int(index >= 3), int(index >= 4)).hex()
            snapshot.update(race_clock=clock, session_state=9 if index < 4 else 6,
                            shared={"lifecycle": {"session_timers_hex": struct.pack("<5I", *timers).hex(),
                                                  "player_lifecycle_hex": player.hex(),
                                                  "race_lifecycle_hex": struct.pack("<5I", 0, index, clock, epoch, 0).hex(),
                                                  "lifecycle_clock_hex": struct.pack("<I", clock).hex()},
                                    "provenance": {"noqual_flags_hex": flags},
                                    "traffic_control": {"status_hex": flags + bytes(8).hex()}})
            records.append({"id": index, "release_events": [{}] if index == 3 else [],
                            **{phase: json.loads(json.dumps(snapshot)) for phase in
                               (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES)}})
        return {"configuration_hex": bytes(0x620).hex()}, records

    def binary(self, header, records):
        FLEET.write_startup_clock_binary(self.path, header, records)
        return self.path.read_bytes()

    def rows(self, records):
        return [{"id": record["id"], "actor": actor, "phase": phase,
                 **{name: int(record["pre"][name]) for name in FLEET.STARTUP_CLOCK_FIELDS},
                 **dict(zip(FLEET.STARTUP_LIFECYCLE_FIELDS, FLEET.startup_lifecycle_values(record["pre"])))}
                for record in records for phase in ("integration", "ground") for actor in range(FLEET.ACTORS)]

    def write_rows(self, rows, fields=None):
        with self.path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields or ["id", "actor", "phase", *FLEET.STARTUP_CLOCK_FIELDS, *FLEET.STARTUP_LIFECYCLE_FIELDS])
            writer.writeheader(); writer.writerows(rows)

    @staticmethod
    def change_lifecycle(snapshot, field, value):
        lifecycle = snapshot["shared"]["lifecycle"]
        block, offset, form = (("session_timers", int(field[5]) * 4, "I") if field.startswith("timer") else
                               ("player_lifecycle", 22, "H") if field == "start_phase" else ("race_lifecycle", 12, "I"))
        raw = bytearray.fromhex(lifecycle[block + "_hex"]); struct.pack_into("<" + form, raw, offset, value)
        lifecycle[block + "_hex"] = raw.hex()

    def test_roles_follow_generated_extra_transactions_and_compact_sidecar_observes_only_inputs(self):
        header, records = self.fixture(); inputs = FLEET.startup_clock_inputs(header, records)
        self.assertEqual(inputs["roles"], ["retained", "extra", "primary", "extra", "primary", "extra", "primary", "extra", "primary"])
        self.assertEqual(inputs["coverage"], {"first_samples": 1, "ordinary_samples": 1, "split_samples": 1, "clamped_samples": 1, "extra_updates": 4})
        raw = self.binary(header, records)
        self.assertEqual(len(raw), 36 + 4 * 8 + 20 + 9 * 4)
        self.assertEqual(struct.unpack_from("<8sIIiiiII", raw), (b"TOCACLOK", 2, 9, 100, 2621, 1638425, 1, 4))
        self.assertEqual(list(struct.iter_unpack("<2I", raw[36:68])), [(2, 200), (4, 400), (6, 800), (8, 900)])
        self.assertEqual(struct.unpack_from("<5I", raw, 68), (1000,) * 5)
        self.assertEqual(struct.unpack_from("<9I", raw, 88), tuple(1000 + index * 4000 for index in range(9)))
        #Later observed TimeSteps are expectations, including the extra latch.
        for record in records[1:]:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                record[phase].update(delta_q16=1234, reciprocal_q16=5678, extra_update=False)
        self.assertEqual(raw, self.binary(header, records))

    def test_equal_counter_is_a_fresh_sample_and_incomplete_outer_transactions_fail(self):
        header, records = self.fixture()
        for prefix in (records[:1], records[:2], records[:3], records[:5], records[:7]):
            with self.subTest(length=len(prefix)), self.assertRaisesRegex(ValueError, "transaction is complete"):
                FLEET.startup_clock_inputs(header, prefix)
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            records[8][phase]["qpc_low"] = 800
        with self.assertRaisesRegex(ValueError, "positive physical interval"):
            FLEET.startup_clock_inputs(header, records)

    def test_initial_clear_latch_samples_next_update_and_first_sample_keeps_it_clear(self):
        header, source = self.fixture(); records = [source[0], source[2]]
        records[1]["id"] = 1
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                record[phase]["extra_update"] = False
        inputs = FLEET.startup_clock_inputs(header, records)
        self.assertEqual(inputs["roles"], ["retained", "primary"])
        self.assertEqual(inputs["samples"], [(1, 200)])
        self.assertEqual(inputs["initial_time"], (2621, 1638425, False))
        self.assertEqual(inputs["coverage"]["extra_updates"], 0)

    def test_all_source_counter_and_latch_aliases_are_required_and_typed(self):
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            for field, value in (("qpc_low", 401), ("qpc_divisor", 101), ("qpc_difference", 201), ("clock_first_sample", 1)):
                header, records = self.fixture(); records[4][phase][field] = value
                with self.subTest(phase=phase, field=field), self.assertRaisesRegex(ValueError, "aliases disagree"):
                    FLEET.startup_clock_inputs(header, records)
        for field, value in (("qpc_low", -1), ("qpc_divisor", 0), ("qpc_divisor", 0x80000000),
                             ("qpc_difference", 0x100000000), ("clock_first_sample", True),
                             ("delta_q16", 0), ("reciprocal_q16", 0), ("extra_update", 1)):
            header, records = self.fixture(); records[0]["pre"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                FLEET.startup_clock_inputs(header, records)
        header, records = self.fixture(); records[4]["id"] = 5
        with self.assertRaisesRegex(ValueError, "consecutive from zero"):
            FLEET.startup_clock_inputs(header, records)
        header, records = self.fixture(); config = bytearray.fromhex(header["configuration_hex"])
        struct.pack_into("<I", config, 0x8c, 1); header["configuration_hex"] = config.hex()
        with self.assertRaisesRegex(ValueError, "boost transform"):
            FLEET.startup_clock_inputs(header, records)

    def test_comparison_counts_unique_values_and_detects_observed_time_tampering(self):
        header, records = self.fixture(); original = self.binary(header, records); rows = self.rows(records)
        self.write_rows(rows); report = FLEET.compare_startup_clock(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual((report["compared_values"], report["generated_time_values"], report["derived_updates"]), (108, 24, 8))
        self.assertEqual((report["clock_compared_values"], report["lifecycle_compared_values"]), (27, 81))
        self.assertEqual((report["retained_initial_updates"], report["inherited_initial_values"]), (1, 3))
        self.assertEqual(report["physical_alias_checkpoints"], 9 * 16 * 2)
        self.assertEqual(report["stored_divisor_observed"], 100)
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            records[4][phase]["delta_q16"] += 1
        self.assertEqual(original, self.binary(header, records))
        self.write_rows(rows); report = FLEET.compare_startup_clock(header, records, self.path)
        self.assertFalse(report["numerical_parity"])
        self.assertEqual(report["first_mismatches"], [{"update": 4, "field": "delta_q16", "expected": 65537, "actual": 65536}])

    def test_later_lifecycle_expectations_do_not_enter_sidecar_and_corruption_is_compared(self):
        for field, value in (("start_phase", 2), ("timer1_origin", 13001), ("release_epoch", 13001), ("session_state", 9)):
            header, records = self.fixture(); before = self.binary(header, records); rows = self.rows(records)
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = records[4][phase]
                if field == "session_state": snapshot[field] = value
                else: self.change_lifecycle(snapshot, field, value)
            with self.subTest(field=field):
                self.assertEqual(before, self.binary(header, records))
                self.write_rows(rows); report = FLEET.compare_startup_clock(header, records, self.path)
                self.assertFalse(report["numerical_parity"])
                self.assertEqual([mismatch["field"] for mismatch in report["first_mismatches"]], [field])

    def test_lifecycle_initial_context_and_every_boundary_alias_are_validated(self):
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            for field, value in (("start_phase", 2), ("timer1_origin", 13001), ("release_epoch", 13001)):
                header, records = self.fixture(); self.change_lifecycle(records[4][phase], field, value)
                with self.subTest(phase=phase, field=field), self.assertRaisesRegex(ValueError, "lifecycle aliases disagree"):
                    FLEET.startup_lifecycle_inputs(records)
        for field, value in (("start_phase", 1), ("release_epoch", 1), ("session_state", 6)):
            header, records = self.fixture(); snapshot = records[0]["pre"]
            if field == "session_state": snapshot[field] = value
            else: self.change_lifecycle(snapshot, field, value)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "ordinary NoQual entry"):
                FLEET.startup_lifecycle_inputs(records)
        for block in ("race_lifecycle", "lifecycle_clock"):
            header, records = self.fixture(); lifecycle = records[4]["consumed"]["shared"]["lifecycle"]
            raw = bytearray.fromhex(lifecycle[block + "_hex"]); raw[8 if block == "race_lifecycle" else 0] ^= 1
            lifecycle[block + "_hex"] = raw.hex()
            with self.subTest(block=block), self.assertRaisesRegex(ValueError, "caller clock aliases disagree"):
                FLEET.startup_lifecycle_inputs(records)
        header, records = self.fixture(); records[4]["ground"]["shared"]["traffic_control"]["status_hex"] = bytes(24).hex()
        with self.assertRaisesRegex(ValueError, "NoQual aliases disagree"):
            FLEET.startup_lifecycle_inputs(records)
        header, records = self.fixture(); records[0]["pre"]["shared"]["provenance"]["noqual_flags_hex"] = bytes(16).hex()
        with self.assertRaisesRegex(ValueError, "ordinary NoQual entry"):
            FLEET.startup_lifecycle_inputs(records)
        header, records = self.fixture(); records[4]["pre"]["race_clock"] = True
        with self.assertRaisesRegex(ValueError, "observed caller clock"):
            FLEET.startup_lifecycle_inputs(records)

    def test_initial_origins_and_external_caller_clocks_are_explicit_inputs(self):
        header, records = self.fixture(); before = self.binary(header, records)
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            self.change_lifecycle(records[0][phase], "timer4_origin", 999)
        after = self.binary(header, records)
        self.assertEqual(before[:84], after[:84]); self.assertEqual(before[88:], after[88:])
        self.assertEqual(struct.unpack_from("<I", after, 84)[0], 999)
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            snapshot = records[4][phase]; snapshot["race_clock"] += 1
            lifecycle = snapshot["shared"]["lifecycle"]
            raw = bytearray.fromhex(lifecycle["race_lifecycle_hex"]); struct.pack_into("<I", raw, 8, snapshot["race_clock"])
            lifecycle["race_lifecycle_hex"] = raw.hex(); lifecycle["lifecycle_clock_hex"] = struct.pack("<I", snapshot["race_clock"]).hex()
        updated = self.binary(header, records)
        self.assertEqual(after[:104], updated[:104]); self.assertEqual(after[108:], updated[108:])
        self.assertEqual(struct.unpack_from("<I", updated, 104)[0], 17001)

    def test_native_clock_csv_rejects_inconsistent_aliases_missing_fields_chronology_and_eof(self):
        header, records = self.fixture(); valid = self.rows(records)
        changed = [dict(row) for row in valid]; changed[16]["delta_q16"] += 1
        reordered = list(valid); reordered[0], reordered[1] = reordered[1], reordered[0]
        for rows, message in ((changed, "aliases are inconsistent"), (reordered, "chronology"),
                              (valid[:-1], "chronology"), ([*valid, valid[-1]], "Extra native")):
            self.write_rows(rows)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                FLEET.compare_startup_clock(header, records, self.path)
        self.write_rows([{key: value for key, value in row.items() if key not in ("extra_update", *FLEET.STARTUP_LIFECYCLE_FIELDS)} for row in valid],
                        ["id", "actor", "phase", "delta_q16", "reciprocal_q16"])
        with self.assertRaisesRegex(ValueError, "omitted the generated clock/lifecycle fields"):
            FLEET.compare_startup_clock(header, records, self.path)
        self.write_rows([{key: value for key, value in row.items() if key != "start_phase"} for row in valid],
                        ["id", "actor", "phase", *FLEET.STARTUP_CLOCK_FIELDS, *[field for field in FLEET.STARTUP_LIFECYCLE_FIELDS if field != "start_phase"]])
        with self.assertRaisesRegex(ValueError, "omitted the generated clock/lifecycle fields"):
            FLEET.compare_startup_clock(header, records, self.path)

    def test_exact_but_short_clock_interval_does_not_claim_transaction_coverage(self):
        header, records = self.fixture(); records = records[:4]
        self.write_rows(self.rows(records)); report = FLEET.compare_startup_clock(header, records, self.path)
        self.assertTrue(report["numerical_parity"])
        self.assertFalse(report["coverage_sufficient"]); self.assertFalse(report["parity"])

    def test_exact_lifecycle_requires_both_release_and_visible_phase_expiry(self):
        for missing in ("release", "expiry"):
            header, records = self.fixture()
            if missing == "release": records[3]["release_events"] = []
            else:
                for record in records[5:]:
                    for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                        self.change_lifecycle(record[phase], "start_phase", 3)
            self.write_rows(self.rows(records)); report = FLEET.compare_startup_clock(header, records, self.path)
            with self.subTest(missing=missing):
                self.assertTrue(report["numerical_parity"])
                self.assertFalse(report["coverage_sufficient"]); self.assertFalse(report["parity"])

    def test_world_reset_zeroes_only_record_time_inputs_in_both_existing_handoffs(self):
        from .startup_physics import StartupContactCacheTests
        from .startup_physics import StartupWorldResetTests

        owner = StartupWorldResetTests(); owner.path = self.path
        header, records = owner.fixture(); before = owner.binaries(header, records)
        legacy = StartupContactCacheTests.binaries(owner, header, records)
        packed = []
        pack = struct.pack
        def observe(form, *values):
            if form == "<IiiIIIIiIII": packed.append(values)
            return pack(form, *values)
        with patch.object(FLEET.struct, "pack", side_effect=observe):
            self.assertEqual(before, owner.binaries(header, records))
        self.assertEqual(len(packed), len(records) * 2)
        self.assertTrue(all(values[1:4] == (0, 0, 0) for values in packed))
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                record[phase].update(delta_q16=1234, reciprocal_q16=5678, extra_update=True)
        self.assertEqual(before, owner.binaries(header, records))
        self.assertNotEqual(legacy, StartupContactCacheTests.binaries(owner, header, records))
        #Earlier modes keep their observed clock/caller inputs in both streams.
        self.assertNotEqual(before[1], legacy[1])

    def test_world_reset_removes_later_caller_state_release_and_journal_clock_imports(self):
        from .startup_physics import StartupWorldResetTests

        owner = StartupWorldResetTests(); owner.path = self.path
        header, records = owner.fixture()
        initial = FLEET.validate_joined_ai_scope(header, records, retained_drivers=True)
        with patch.object(FLEET, "validate_joined_ai_scope", return_value=initial):
            before = owner.binaries(header, records)
            for record in records:
                record["update_caller_return_address"] = 0x4488a1
                record["release_events"] = []
                for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                    snapshot = record[phase]; snapshot["session_state"] = 9; snapshot["race_clock"] += 100
                    status = bytearray.fromhex(snapshot["shared"]["traffic_control"]["status_hex"])
                    status[:16] = bytes([0x33]) * 16
                    snapshot["shared"]["traffic_control"]["status_hex"] = status.hex()
                    snapshot["shared"]["traffic_control"]["status"] = bytes(status)
                for kind, _, event in FLEET.combined_events(record):
                    if kind == "release": event["pre"]["race_clock"] += 100
            self.assertEqual(before, owner.binaries(header, records))
        with self.assertRaisesRegex(ValueError, "ordinary joined profile"):
            FLEET.write_binary(self.path, header, records, startup_world_reset=True, startup_contact_cache=True, compare_ai_timing=True)


class ColdTimerCreationTests(TraceTestCase):
    trace_filename = 'driver'
    binary = StartupClockTests.binary
    rows = StartupClockTests.rows
    write_rows = StartupClockTests.write_rows
    change_lifecycle = staticmethod(StartupClockTests.change_lifecycle)

    @staticmethod
    def event(clock=123):
        return {**{name: value for name, value in FLEET.COLD_TIMER_CREATION_MANIFEST.items() if name != "scope"}, "creation_clock": clock}

    def fixture(self, clock=123):
        header, records = StartupClockTests.fixture()
        header.update(cold_grid_sequence_manifest=FLEET.COLD_GRID_SEQUENCE_MANIFEST,
                      cold_timer_creation_manifest=FLEET.COLD_TIMER_CREATION_MANIFEST,
                      cold_grid_sequence={"timer_creation": self.event(clock)}, qpc_sample_manifest=FLEET.QPC_SAMPLE_MANIFEST,
                      warm_restart_manifest=FLEET.WARM_RESTART_MANIFEST, local_session_lifecycle_manifest=FLEET.LOCAL_SESSION_LIFECYCLE_MANIFEST,
                      local_handoff_manifest=FLEET.LOCAL_HANDOFF_MANIFEST, session_boundary_manifest=FLEET.SESSION_BOUNDARY_MANIFEST)
        for index, record in enumerate(records):
            record["combined_journal"] = []
            if index in (2, 4, 6, 8):
                record["combined_journal"] = [self.sample(records[index - 1]["pre"], record["pre"], 1),
                                              {"kind": "primary", "event": {"order": 2, "cached_clock": record["pre"]["race_clock"]}}]
        first_clock = records[0]["pre"]["race_clock"]
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            records[0][phase]["shared"]["lifecycle"]["session_timers_hex"] = struct.pack("<5I", clock, clock, first_clock, first_clock, clock).hex()
        return header, records

    @staticmethod
    def sample(before, after, order):
        return {"kind": "qpc_sample", "event": {"order": order, "return_address": 0x44a516, "counter": after["qpc_low"],
            "pre": {name: before[name] for name in FLEET.QPC_SAMPLE_FIELDS}, "post": {name: after[name] for name in FLEET.QPC_SAMPLE_FIELDS}}}

    def test_v3_transports_one_real_creation_sample_including_zero_and_no_origin_values(self):
        for clock in (0, 123):
            header, records = self.fixture(clock)
            before = self.binary(header, records)
            self.assertEqual(struct.unpack_from("<I", before, 8)[0], 3)
            operations = FLEET.startup_clock_inputs(header, records)["operations"]
            position = FLEET.STARTUP_CLOCK_HEADER.size + len(operations) * 16
            self.assertEqual(len(before), position + 4 + len(records) * 4)
            self.assertEqual(before[FLEET.STARTUP_CLOCK_HEADER.size:position], b"".join(struct.pack("<4I", *operation) for operation in operations))
            self.assertEqual(struct.unpack_from("<2I", before, position), (clock, 1000))
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                records[0][phase]["shared"]["lifecycle"]["session_timers_hex"] = struct.pack("<5I", 91, 92, 93, 94, 95).hex()
            self.assertEqual(self.binary(header, records), before)
            header["cold_grid_sequence"]["timer_creation"]["creation_clock"] = clock + 1
            changed = self.binary(header, records)
            self.assertEqual(before[:position], changed[:position]); self.assertEqual(before[position + 4:], changed[position + 4:])

    def test_creation_metadata_and_sequence_are_explicit_and_fail_closed(self):
        from .cold_grid import ColdGridSequenceCaptureTests

        header, records = self.fixture()
        for field, value in (("entry", 0x449f50), ("sample_pc", 0x447a79), ("caller_return_address", 0x44a49b),
                             ("after_initializer", 1), ("before_initializer", 0), ("creation_clock", True), ("creation_clock", -1)):
            changed = copy.deepcopy(header); changed["cold_grid_sequence"]["timer_creation"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): self.binary(changed, records)
        for field in ("cold_timer_creation_manifest", "cold_grid_sequence_manifest"):
            changed = copy.deepcopy(header); changed.pop(field)
            with self.assertRaises(ValueError): self.binary(changed, records)
        changed = copy.deepcopy(header); changed.pop("qpc_sample_manifest")
        with self.assertRaisesRegex(ValueError, "complete ordered QPC"): self.binary(changed, records)
        changed = copy.deepcopy(header); changed["cold_grid_sequence"]["timer_creation"].pop("creation_clock")
        with self.assertRaises(ValueError): self.binary(changed, records)
        owner = ColdGridSequenceCaptureTests()
        sequence_header, _ = owner.fixture()
        sequence = sequence_header["cold_grid_sequence"]; sequence["timer_creation"] = self.event()
        FLEET.validate_cold_grid_sequence(sequence, sequence_header["grid_initialization"], sequence_header["cold_grid_initialization"])
        sequence["timer_creation"]["before_initializer"] = 0
        with self.assertRaises(ValueError): FLEET.validate_cold_grid_sequence(sequence, sequence_header["grid_initialization"], sequence_header["cold_grid_initialization"])

    def test_all_five_initial_origins_remain_compared_outputs_without_becoming_inputs(self):
        header, records = self.fixture()
        rows = self.rows(records)
        self.write_rows(rows)
        report = FLEET.compare_startup_clock(header, records, self.path)
        self.assertTrue(report["numerical_parity"])
        self.assertEqual(report["creation_clock_observed"], 123)
        self.assertEqual(report["initial_timer_origin_imports"], 0)
        self.assertNotIn("initial_timer_origins_observed", report)
        for origin in range(5):
            changed = copy.deepcopy(records)
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                self.change_lifecycle(changed[0][phase], f"timer{origin}_origin", 999)
            result = FLEET.compare_startup_clock(header, changed, self.path)
            self.assertFalse(result["numerical_parity"])
            self.assertEqual([item["field"] for item in result["first_mismatches"]], [f"timer{origin}_origin"])

    def test_menu_only_primaries_keep_every_actual_sample_and_extra_physical_permit(self):
        header, records = self.fixture(); records = records[:6]
        prior = {name: records[3]["pre"][name] for name in FLEET.QPC_SAMPLE_FIELDS}
        one = {**prior, "qpc_low": 300, "qpc_difference": 100, "delta_q16": 65536, "reciprocal_q16": 65536, "extra_update": False}
        two = {**one, "qpc_low": 400}
        last = {**two, "qpc_low": 600, "qpc_difference": 200, "extra_update": True}
        records[4]["combined_journal"] = [self.sample(prior, one, 1), {"kind": "primary", "event": {"order": 2, "cached_clock": 1}},
            self.sample(one, two, 3), {"kind": "primary", "event": {"order": 4, "cached_clock": 2}},
            self.sample(two, last, 5), {"kind": "primary", "event": {"order": 6, "cached_clock": 3}}]
        for record in records[4:]:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES): record[phase].update(last)
        inputs = FLEET.startup_clock_inputs(header, records)
        self.assertEqual(inputs["roles"], ["retained", "extra", "primary", "extra", "primary", "extra"])
        self.assertEqual([op[3] for op in inputs["operations"] if op[2] == 1], [200, 300, 400, 600])
        self.assertEqual(inputs["coverage"]["menu_only_primary_frames"], 2)
        records[4]["combined_journal"].pop(2)
        with self.assertRaisesRegex(ValueError, "QPC state disagrees"): FLEET.startup_clock_inputs(header, records)

    def test_warm_reset_retains_step_extra_and_previous_difference_until_actual_first_sample(self):
        header, records = self.fixture(); records = records[:6]
        prior = {name: records[3]["pre"][name] for name in FLEET.QPC_SAMPLE_FIELDS}
        reset = {**prior, "clock_first_sample": 1, "qpc_divisor": 200}
        sampled = {**reset, "clock_first_sample": 0, "qpc_low": 500}
        records[4]["combined_journal"] = [{"kind": "primary", "event": {"order": 1, "cached_clock": 1}},
            {"kind": "warm_grid", "event": {"order": 2}}, self.sample(reset, sampled, 3),
            {"kind": "primary", "event": {"order": 4, "cached_clock": 2}}]
        for record in records[4:]:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES): record[phase].update(sampled)
        raw = ({"clock_first_sample": struct.pack("<I", 0), "qpc_divisor": struct.pack("<I", 100)},
               {"clock_first_sample": struct.pack("<I", 1), "qpc_divisor": struct.pack("<I", 200)})
        with patch.object(FLEET, "validate_warm_grid_event", return_value=raw):
            inputs = FLEET.startup_clock_inputs(header, records)
        self.assertEqual(inputs["operations"][-3:], [(4, 2, 2, 200), (4, 3, 1, 500), (4, 4, 0, 0)])
        self.assertEqual(inputs["coverage"]["first_samples"], 2)
        self.assertEqual(sampled["qpc_difference"], prior["qpc_difference"])

    def test_actual_sample_wrap_uses_signed32_difference_and_first_sample_retains_difference(self):
        header, records = self.fixture(); records = records[:6]
        first = {name: records[0]["pre"][name] for name in FLEET.QPC_SAMPLE_FIELDS}
        first.update(qpc_low=0xfffffff0, qpc_divisor=10)
        sampled = {**first, "qpc_low": 0xfffffff5, "clock_first_sample": 0, "delta_q16": 65536, "reciprocal_q16": 65536}
        wrapped = {**sampled, "qpc_low": 20, "qpc_difference": 30, "delta_q16": 98304, "reciprocal_q16": 43690}
        for index, record in enumerate(records):
            values = first if index < 2 else sampled if index < 4 else wrapped
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES): record[phase].update(values)
        records[2]["combined_journal"][0] = self.sample(first, sampled, 1)
        records[4]["combined_journal"][0] = self.sample(sampled, wrapped, 1)
        inputs = FLEET.startup_clock_inputs(header, records)
        self.assertEqual(inputs["coverage"]["split_samples"], 1)
        self.assertEqual(inputs["roles"][-2:], ["primary", "extra"])

    def test_ordered_clock_rejects_missing_duplicate_or_unpermitted_physical_events(self):
        header, records = self.fixture()
        for change in ("missing_primary", "duplicate_sample", "duplicate_order", "wrong_counter"):
            altered = copy.deepcopy(records)
            if change == "missing_primary": altered[2]["combined_journal"].pop()
            elif change == "duplicate_sample":
                event = copy.deepcopy(altered[2]["combined_journal"][0]); event["event"]["order"] = 2
                altered[2]["combined_journal"].insert(1, event); altered[2]["combined_journal"][-1]["event"]["order"] = 3
            elif change == "duplicate_order": altered[2]["combined_journal"][1]["event"]["order"] = 1
            else: altered[2]["combined_journal"][0]["event"]["counter"] = 201
            with self.subTest(change=change), self.assertRaises(ValueError): FLEET.startup_clock_inputs(header, altered)
        header, records = self.fixture(); records = records[:1]
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES): records[0][phase]["extra_update"] = False
        self.assertEqual(FLEET.startup_clock_inputs(header, records)["operations"], [])
        raw = self.binary(header, records)
        self.assertEqual(len(raw), FLEET.STARTUP_CLOCK_HEADER.size + 8)

    def test_v8_journal_echoes_the_same_external_counter_and_requires_full_clock_scope(self):
        from .physics import DispatchFeedbackTests

        owner = ColdLocalLifecycleTests(); owner.path = self.path
        header, records = owner.fixture()
        header.update(cold_grid_sequence_manifest=FLEET.COLD_GRID_SEQUENCE_MANIFEST,
                      cold_timer_creation_manifest=FLEET.COLD_TIMER_CREATION_MANIFEST,
                      qpc_sample_manifest=FLEET.QPC_SAMPLE_MANIFEST, warm_restart_manifest=FLEET.WARM_RESTART_MANIFEST)
        header["cold_grid_sequence"]["timer_creation"] = self.event()
        records[1]["combined_journal"] = [{"kind": "qpc_sample", "event": {"counter": 0xabcdef12}}]
        with patch.object(FLEET, "validate_startup_first_selection_scope"), patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(self.path, header, records[:2], compare_driver_retention=True,
                startup_join=True, startup_physical_body=True, startup_world_reset=True, **DispatchFeedbackTests.flags)
        raw = self.path.read_bytes()
        self.assertEqual(struct.unpack_from("<I", raw, 8)[0], 8)
        self.assertIn(struct.pack("<2I", 16, 0xabcdef12), raw)
        header.pop("warm_restart_manifest")
        with self.assertRaisesRegex(ValueError, "complete cold warm"): FLEET.ordered_clock_enabled(header)


class ColdLocalLifecycleTests(TraceTestCase):
    trace_filename = "cold-grid.jsonl"
    capture_recorder = True
    flags = DispatchFeedbackTests.flags

    def fixture(self):
        from .retained_session import RetainedLocalSessionTests

        header, records = RetainedLocalSessionTests.fixture(self)
        header.update(cold_grid_sequence={"events": [{}, {}]}, unrecorded_keyboard_warmup=None)
        first = records[0]
        first.update(local_handoffs=[], combined_journal=[], update_caller_return_address=0x4488a1)
        grid = copy.deepcopy(first["pre"])
        grid["session_state"] = 9
        shared = grid["shared"]
        for name, raw in (("finish_counts", bytes(8)), ("local_finish_flags", bytes(4)),
                          ("pending_state", struct.pack("<I", 9)), ("race_status", bytes(4))):
            shared["finish_timing"][name] = raw
            shared["finish_timing"][name + "_hex"] = raw.hex()
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            first[phase] = copy.deepcopy(grid)
            first[phase]["update_counter"] = int(phase != "pre")
        return header, records

    def binaries(self, header, records):
        from .startup_physics import StartupWorldResetTests

        with patch.object(FLEET, "validate_joined_ai_scope", return_value=FLEET.ai_content(header["ai_initial_state"])):
            return StartupWorldResetTests.binaries(self, header, records)

    def test_cold_local_timing_requires_state9_pending9_and_pristine_counter(self):
        header, records = self.fixture()
        with patch.object(FLEET, "validate_joined_ai_scope"):
            self.assertIs(FLEET.validate_retained_race_timing_scope(header, records), records[0]["ai_pre"])
            for field, replacement in (("session_state", 6), ("update_counter", 42)):
                original = records[0]["ai_pre"][field]
                records[0]["ai_pre"][field] = replacement
                with self.assertRaises(ValueError): FLEET.validate_retained_race_timing_scope(header, records)
                records[0]["ai_pre"][field] = original
            header["unrecorded_keyboard_warmup"] = {"keys_only": True}
            with self.assertRaises(ValueError): FLEET.validate_retained_race_timing_scope(header, records)

    def test_origins_have_one_clock_owner_and_later_timing_world_is_not_reimported(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<7I", before[1], 128), (0, 0, 0, 0, 0, 1000, 0))
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            records[0][phase]["shared"]["lifecycle"]["session_timers_hex"] = struct.pack("<5I", 1, 2, 3, 4, 5).hex()
        self.assertEqual(self.binaries(header, records), before)
        with patch.object(FLEET, "validate_retained_race_timing_scope"):
            for record in records[1:]:
                record["pre"]["race_clock"] += 100
                for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                    finish = record[phase]["shared"]["finish_timing"]
                    for name, raw in list(finish.items()):
                        if isinstance(raw, bytes): finish[name], finish[name + "_hex"] = bytes([0xa5]) * len(raw), (bytes([0xa5]) * len(raw)).hex()
            self.assertEqual(self.binaries(header, records), before)

    def test_complete_cold_profile_and_restart_are_accepted_without_launching_source(self):
        profile = ["--scope", "fleet", "--scenario", "local-lap", "--cold-grid-init", "--cold-grid-sequence",
                   "--contact-weather-rng", "--checkpoint-timing", "--finish-timing", "--session-boundaries", "--local-handoffs"]
        class BeforeLaunch(Exception): pass
        for extra in ([], ["--warm-restart"], ["--verify-replay", "/tmp/native-replay", "--warm-restart"],
                      ["--verify-replay", "/tmp/native-replay", "--stop-after-local-checkpoint"]):
            with patch.object(sys, "argv", ["trace-reference.py", *profile, *extra]), patch.object(self.trace.Path, "read_bytes", side_effect=BeforeLaunch):
                with self.assertRaises(BeforeLaunch): self.trace.main()
        for extra in (["--warmup-until-local-finish"], ["--warmup-step", "1:Up"], ["--stop-after-local-checkpoint"]):
            with patch.object(sys, "argv", ["trace-reference.py", *profile, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit): self.trace.main()

    def test_prefix_handshake_requires_exact_numeric_supported_complete_interval(self):
        output = self.path.parent / "verify"
        output.mkdir()
        report_path = output / "fleet-comparison-startup-world-reset.json"
        valid = {"status": "compared", "mode": "startup-world-reset", "source_updates": 64,
                 "complete_source_interval": True, "numerical_parity": True, "exact_prefix_updates": 64,
                 "parity": False, "coverage_sufficient": False, "retained_local_session": {"unsupported_updates": []}}
        def run(_command, **_kwargs):
            report_path.write_text(json.dumps(report))
            return subprocess.CompletedProcess([], 1, "", "")
        for changes, expected in (({}, True), ({"numerical_parity": False}, False), ({"exact_prefix_updates": 63}, False),
                                  ({"complete_source_interval": False}, False), ({"source_updates": 63}, False),
                                  ({"retained_local_session": {"unsupported_updates": [{"update": 63}]}}, False)):
            report = {**valid, **changes}
            with patch.object(self.trace.subprocess, "run", side_effect=run) as execution, patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(self.trace.verify_retained_prefix(self.path, self.path.parent, Path("/tmp/native"), 64), expected)
            self.assertIn("--compare-startup-world-reset", execution.call_args.args[0])
        report_path.write_text(json.dumps(valid))
        with patch.object(self.trace.subprocess, "run", return_value=subprocess.CompletedProcess([], 1, "", "")):
            with self.assertRaises(RuntimeError): self.trace.verify_retained_prefix(self.path, self.path.parent, Path("/tmp/native"), 64)
