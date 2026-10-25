"""Fleet reference checks for retained session."""

from .support import (
    TraceTestCase,
    CLI,
    FLEET,
    FleetTestCase,
    Path,
    copy,
    csv,
    io,
    joined_fixture,
    json,
    patch,
    state,
    struct,
    subprocess,
    sys,
    zlib,
)
from .drivers import DriverRetentionTests


class RetainedRaceTimingTests(TraceTestCase):
    trace_filename = 'driver'
    binaries = DriverRetentionTests.binaries
    timing_csv = FleetTestCase.timing_csv
    finish_csv = FleetTestCase.finish_csv

    def fixture(self, multiple=False):
        from .drivers import DriverRetentionTests

        header, records = DriverRetentionTests.fixture(self)
        header.update(checkpoint_timing_manifest=FLEET.CHECKPOINT_TIMING_MANIFEST,
                      finish_timing_manifest=FLEET.FINISH_TIMING_MANIFEST,
                      contact_weather_manifest=FLEET.CONTACT_WEATHER_MANIFEST, combined_initial_release_counter=0,
                      contact_audio_initial={"scale_hex": struct.pack("<f", 65536).hex(), "gains_hex": bytes(24).hex(), "effect_flag": 0})
        for index, record in enumerate(records):
            record.update(update_caller_return_address=0x448434, release_events=[])
            record["combined_journal"] = [entry for entry in record["combined_journal"] if entry["kind"] != "release"]
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = record[phase]
                changed = index == 2 or index == 1 and phase not in ("pre", "ai_pre")
                snapshot["update_counter"] = (100, 101, 0)[index] if phase == "pre" else 0 if index == 1 and changed else (101, 102, 1)[index]
                snapshot["session_state"] = 6
                shared = snapshot["shared"]
                shared["ai_state"]["release_counter"] = 0
                shared["checkpoint_timing"] = {"session_stage": 2, "split_table": bytes(576), "split_table_hex": bytes(576).hex(),
                                                "race_flags": bytes(4), "race_flags_hex": bytes(4).hex()}
                world = bytearray(shared["provenance"]["world_status"])
                struct.pack_into("<H", world, 0x14, 1)
                for mapping, name, raw in ((shared["provenance"], "world_status", bytes(world)),
                                           (shared["provenance"], "noqual_flags", struct.pack("<4I", 0, 1, 1, 0)),
                                           (shared["traffic_control"], "status", struct.pack("<6I", 0, 1, 1, 0, 0, 0))):
                    mapping[name], mapping[name + "_hex"] = raw, raw.hex()
                finish = {name: bytes(size) for name, (_, size) in FLEET.FINISH_TIMING_BLOCKS.items()}
                finish["pending_state"] = struct.pack("<I", 6)
                finish["race_counters"] = struct.pack("<4I", 0, snapshot["update_counter"], snapshot["race_clock"], 1000)
                finish["display_ticks"] = struct.pack("<4h", 201, 201, 201, 201)
                if changed:
                    finish["finish_counts"] = struct.pack("<2I", 2 if multiple else 1, 0)
                    finish["finish_order"] = bytes([1, 2] if multiple else [1]) + bytes(14 if multiple else 15)
                finish.update({name + "_hex": raw.hex() for name, raw in list(finish.items())})
                shared["finish_timing"] = finish
                for actor, entry in enumerate(snapshot["actors"]):
                    game = bytearray.fromhex(entry["gameplay_hex"])
                    struct.pack_into("<2h", game, 0xa2, 0, 63)
                    struct.pack_into("<h", game, 0xa8, 2 if changed and actor in ((1, 2) if multiple else (1,)) else 1)
                    struct.pack_into("<I", game, 0x54, int(changed and actor in ((1, 2) if multiple else (1,))))
                    entry["gameplay"], entry["gameplay_hex"] = bytes(game), game.hex()
                snapshot["gameplay_hex"] = snapshot["actors"][0]["gameplay_hex"]
        def endpoint(entry):
            raw = bytes.fromhex(entry["gameplay_hex"])
            return {"actor": entry["actor"], "route_next": 0, "route_previous": 63, "human": struct.unpack_from("<I", raw, 8)[0],
                    "player_identity": struct.unpack_from("<b", raw, 0x80)[0], "backwards": 0, "active": True,
                    "checkpoint": 0, "checkpoint_marker": 0, "lap": 1, "finished": 0}
        header["unrecorded_keyboard_warmup"] = {"keys_only": True, "continuous_from_grid": False, "recorded_boundaries": False,
            "targeted_ai_point": {"kind": "observed-ai-point", "target_filter": "finish-ready checkpoint0", "target_point": 0,
                "keys_only": True, "continuous_from_grid": False, "recorded_boundaries": False, "requested_keys": [],
                "endpoint_boundary": "421aa0-entry", "selected_rivals": [1],
                "after": {name: records[0]["pre"][name] for name in ("update_counter", "race_clock")},
                "lap_context": {"session_stage": 2, "session_state": 6, "lap_limit": 1},
                "endpoint_actors": [endpoint(entry) for entry in records[0]["ai_pre"]["actors"]]}}
        return header, records

    def test_sparse_actual_boundaries_require_chosen_finish_and_shared_reset(self):
        for multiple in (False, True):
            header, records = self.fixture(multiple)
            projected = [FLEET.retained_race_timing_record(record) for record in records]
            self.assertEqual(json.loads(json.dumps(projected)), projected)
            self.assertEqual(FLEET.retained_race_timing_coverage(header, records), FLEET.retained_race_timing_coverage(header, projected))
            coverage = FLEET.retained_race_timing_coverage(header, projected)
            self.assertTrue(coverage["coverage_sufficient"])
            self.assertEqual(len(coverage["coverage"]["newly_finished_ai"]), 2 if multiple else 1)
            self.assertEqual(coverage["coverage"]["first_finisher_counter_resets"], [1])
            self.assertFalse(FLEET.retained_race_timing_coverage(header, projected[:1])["coverage_sufficient"])
        projected[1]["ai_post"]["shared"]["finish_timing"]["finish_order_hex"] = bytes(16).hex()
        self.assertFalse(FLEET.retained_race_timing_coverage(header, projected)["targeted_ai_finish_observed"])

    def test_target_metadata_and_recorded_readiness_fail_closed(self):
        header, records = self.fixture()
        target = header["unrecorded_keyboard_warmup"]["targeted_ai_point"]
        for field, replacement in (("selected_rivals", [0]), ("requested_keys", ["Up"]), ("keys_only", False),
                                   ("after", {"race_clock": 1, "update_counter": 100}), ("target_point", 1)):
            old = target[field]; target[field] = replacement
            with self.assertRaisesRegex(ValueError, "[Tt]argeted"): FLEET.retained_race_timing_coverage(header, records)
            target[field] = old
        raw = bytearray.fromhex(records[0]["ai_pre"]["actors"][1]["gameplay_hex"]); raw[0xa6] = 1
        records[0]["ai_pre"]["actors"][1]["gameplay_hex"] = raw.hex()
        with self.assertRaisesRegex(ValueError, "finish-ready"): FLEET.retained_race_timing_coverage(header, records)

    def test_retained_binary_imports_only_initial_world_and_journal_clock(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<8I", before[0], 32)[1:3], (1, 1))
        for record in records[1:]:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                shared = record[phase]["shared"]
                for block in ("checkpoint_timing", "finish_timing"):
                    for field, raw in list(shared[block].items()):
                        if isinstance(raw, bytes):
                            shared[block][field] = bytes([0xa5]) * len(raw)
                            shared[block][field + "_hex"] = shared[block][field].hex()
        self.assertEqual(self.binaries(header, records), before)
        records[0]["ai_pre"]["shared"]["checkpoint_timing"]["split_table"] = bytes([1]) + bytes(575)
        self.assertNotEqual(self.binaries(header, records)[0], before[0])
        records[0]["ai_pre"]["shared"]["checkpoint_timing"]["split_table"] = bytes(576)
        records[2]["ai_pre"]["race_clock"] += 1
        clock_changed = self.binaries(header, records)
        self.assertEqual(clock_changed[0], before[0])
        self.assertNotEqual(clock_changed[1], before[1])

    def test_existing_sidecars_compare_generated_timing_and_reject_exact_short_prefix(self):
        header, records = self.fixture()
        native = self.timing_csv(records, finish=True)
        self.finish_csv(records)
        report = FLEET.compare_retained_race_timing(header, records, native)
        self.assertTrue(report["parity"])
        self.assertEqual(report["initial_timing_world_imports"], 1)
        self.assertEqual(report["later_timing_world_imports"], 0)
        short = records[:1]
        self.timing_csv(short, finish=True); self.finish_csv(short)
        report = FLEET.compare_retained_race_timing(header, short, native)
        self.assertTrue(report["numerical_parity"])
        self.assertFalse(report["parity"])
        self.timing_csv(records, finish=True); self.finish_csv(records)
        changed = bytearray.fromhex(records[2]["ai_post"]["shared"]["checkpoint_timing"]["split_table_hex"])
        changed[0] = 1
        records[2]["ai_post"]["shared"]["checkpoint_timing"]["split_table_hex"] = changed.hex()
        report = FLEET.compare_retained_race_timing(header, records, native)
        self.assertFalse(report["numerical_parity"])
        self.assertEqual(report["exact_prefix_updates"], 2)

    def test_partial_manifest_and_unsupported_initial_session_are_rejected(self):
        header, records = self.fixture()
        del header["finish_timing_manifest"]
        with self.assertRaisesRegex(ValueError, "complete checkpoint"): self.binaries(header, records)
        header["finish_timing_manifest"] = FLEET.FINISH_TIMING_MANIFEST
        finish = records[0]["ai_pre"]["shared"]["finish_timing"]
        finish["finish_counts"] = struct.pack("<2I", 0, 0xaabbcc00)
        finish["finish_counts_hex"] = finish["finish_counts"].hex()
        finish["pending_state"] = struct.pack("<I", 0xaabb0006)
        finish["pending_state_hex"] = finish["pending_state"].hex()
        self.binaries(header, records)  # Source byte/WORD writes retain their upper context.
        finish["display_active_hex"] = struct.pack("<4I", 1, 0, 0, 0).hex()
        with self.assertRaisesRegex(ValueError, "inactive split"): self.binaries(header, records)

    def test_new_consumer_coverage_keeps_old_activity_diagnostic_and_numerical_gates(self):
        header, records = self.fixture()
        native = self.timing_csv(records, finish=True); self.finish_csv(records)
        base = {"parity": True, "numerical_parity": True, "compared_values": 0, "mismatch_count": 0,
                "first_mismatches": [], "exact_prefix_updates": 3}
        joined = {**base, "coverage_sufficient": False}
        wheel = {**base, "skid_dispatch": {"coverage": {"draws": 0}}}
        whine = {**base, "coverage": {"rng_advance_calls": 0}, "positive_command_coverage": False}
        with patch.object(FLEET, "validate_combined_scope"), patch.object(FLEET, "combined_expected", return_value={}), \
                patch.object(FLEET, "combined_pre_expectations", return_value=[]), patch.object(FLEET, "compare_joined_ai", return_value=joined), \
                patch.object(FLEET, "compare_wheel_queue", return_value=wheel), patch.object(FLEET, "compare_whine", return_value=whine), \
                patch.object(FLEET, "compare_contact_weather", return_value={**base, "positive_random_coverage": False}), \
                patch.object(FLEET, "compare_driver_retention", return_value={**base, "human_mapper_transactions": 3}):
            report = FLEET.compare_combined(header, records, native, compare_retained_drivers=True)
            self.assertTrue(report["parity"])
            self.assertFalse(report["coverage"]["previous_consumer_coverage_sufficient"])
            self.assertTrue(report["coverage"]["positive_retained_race_timing"])
            raw = bytearray.fromhex(records[2]["ai_post"]["actors"][1]["gameplay_hex"]); raw[0xc0] ^= 1
            records[2]["ai_post"]["actors"][1]["gameplay_hex"] = raw.hex()
            report = FLEET.compare_combined(header, records, native, compare_retained_drivers=True)
            self.assertFalse(report["parity"])
            self.assertFalse(report["numerical_parity"])

    def test_cli_reuses_retained_driver_mode_and_reports_consumed_timing_scope(self):
        header, records = self.fixture()
        source = self.path.with_suffix(".jsonl")
        timing = {"parity": True, "initial_timing_world_imports": 1, "later_timing_world_imports": 0}
        comparison = {"parity": True, "exact_prefix_updates": 3, "mismatch_count": 0, "race_timing": timing}
        with patch.object(sys, "argv", ["replay-fleet-reference.py", str(source), "--compare-driver-retention", "--output", str(self.path.parent)]), \
                patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), patch.object(CLI.FLEET, "write_binary"), \
                patch.object(CLI.FLEET, "write_combined_binary"), patch.object(CLI.FLEET, "compare_combined", return_value=comparison), \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch.object(sys, "stdout", io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_args.args[0][1], "--compare-combined-rng")
        report = json.loads((self.path.parent / "fleet-comparison-retained-driver-rng.json").read_text())
        self.assertEqual(report["retained_race_timing"], timing)
        self.assertIsNone(report["autonomous_ai_parity"])


class RetainedLocalSessionTests(TraceTestCase):
    trace_filename = 'driver'
    binaries = DriverRetentionTests.binaries
    timing_csv = FleetTestCase.timing_csv
    finish_csv = FleetTestCase.finish_csv

    def fixture(self):
        from .drivers import HumanLampTests

        header, initial = RetainedRaceTimingTests.fixture(self)
        lamps, _ = HumanLampTests.fixture(self)
        header.update(human_lamps_manifest=FLEET.HUMAN_LAMPS_MANIFEST, brake_publication_manifest=FLEET.BRAKE_PUBLICATION_MANIFEST,
                      wheel_queue_initial_state=lamps["wheel_queue_initial_state"], local_handoff_manifest=FLEET.LOCAL_HANDOFF_MANIFEST,
                      session_boundary_manifest=FLEET.SESSION_BOUNDARY_MANIFEST, local_session_lifecycle_manifest=FLEET.LOCAL_SESSION_LIFECYCLE_MANIFEST,
                      local_session_initial_state={"entry_flag": 0}, lifecycle_blocks=FLEET.LIFECYCLE_BLOCKS)
        clocks, states = [1000, 1040, 1100, 6200, 6240, 6300], [6, 6, 10, 10, 10, 13]
        records = []
        for index, clock in enumerate(clocks):
            record = copy.deepcopy(initial[0])
            record.update(id=index, publication_calls=[], local_handoffs=[], combined_journal=[],
                          update_caller_return_address={6: 0x448434, 10: 0x448c46, 13: 0x44a336}[states[index]])
            if index in (2, 3, 5):
                record["combined_journal"] = [{"kind": "primary", "event": {"order": 1, "cached_clock": clocks[index - 1]}}]
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = record[phase]; shared = snapshot["shared"]
                finished = index > 0 or phase not in ("pre", "ai_pre")
                pending = 13 if index >= 4 else 10 if finished else 6
                snapshot.update(race_clock=clock, update_counter=100 + index + (phase != "pre"), session_state=states[index])
                world = bytearray(shared["provenance"]["world_status"])
                struct.pack_into("<H", world, 0x12, int(finished))
                shared["provenance"]["world_status"], shared["provenance"]["world_status_hex"] = bytes(world), world.hex()
                origins = [11, 22, 33, 44, 55]
                if index >= 2: origins[0] = origins[3] = 1100
                if index == 5: origins[0] = 6240
                shared["lifecycle"] = {name + "_hex": bytes(size).hex() for name, (_, size) in FLEET.LIFECYCLE_BLOCKS.items()}
                shared["lifecycle"].update(session_timers_hex=struct.pack("<5I", *origins).hex(), lifecycle_clock_hex=struct.pack("<I", clock).hex(),
                                           race_lifecycle_hex=struct.pack("<5I", 0, snapshot["update_counter"], clock, 1000, 0).hex(), local_session_entry_flag=0)
                finish = shared["finish_timing"]
                finish.update(finish_counts=struct.pack("<2I", 16 if finished else 15, int(finished)),
                              finish_order=bytes(range(1, 16)) + bytes([0]), local_finish_flags=bytes([16 if finished else 0, 0, 0, 0]),
                              pending_state=struct.pack("<I", pending), race_status=struct.pack("<HH", 0, int(finished)),
                              race_counters=struct.pack("<4I", 0, snapshot["update_counter"], clock, 1000))
                finish.update({name + "_hex": raw.hex() for name, raw in list(finish.items()) if isinstance(raw, bytes)})
                extended = {name: bytes(size) for name, (_, size) in FLEET.SESSION_BOUNDARY_BLOCKS.items()}
                tail = bytearray(48)
                if finished:
                    for actor, award in enumerate((15, 12, 10, 8, 6, 5, 4, 3, 2, 1), 1): struct.pack_into("<h", tail, 2 + actor * 2, award)
                extended.update(configuration_tail=bytes(tail), classification_order=bytes(range(1, 16)) + bytes([0]) if finished else bytes(range(16)),
                                split_enable=struct.pack("<4I", int(not finished), 0, 0, 0), lap_distance=struct.pack("<I", 4000000))
                extended.update({name + "_hex": raw.hex() for name, raw in list(extended.items())})
                shared["session_boundary"] = extended
                for actor, entry in enumerate(snapshot["actors"]):
                    game = bytearray.fromhex(entry["gameplay_hex"])
                    struct.pack_into("<I", game, 8, int(actor == 0 and not finished))
                    struct.pack_into("<I", game, 0x54, int(actor != 0 or finished))
                    struct.pack_into("<h", game, 0xa8, 1 if actor == 0 else 2)
                    if actor == 0 and finished:
                        game[0x83] = 25
                        struct.pack_into("<4i", game, 0x60, 0, 0, 0, 2)
                    entry["gameplay"], entry["gameplay_hex"] = bytes(game), game.hex()
                    dynamics = bytearray.fromhex(entry["dynamics_hex"])
                    if actor == 0 and finished:
                        struct.pack_into("<3i", dynamics, 0x310, 1, 1, 0)
                        struct.pack_into("<I", dynamics, 0x27c, 0)
                    entry["dynamics"], entry["dynamics_hex"] = bytes(dynamics), dynamics.hex()
                snapshot["gameplay_hex"] = snapshot["actors"][0]["gameplay_hex"]
                snapshot["dynamics_hex"] = snapshot["actors"][0]["dynamics_hex"]
            records.append(record)
        pre = {"boundary": "402be0-entry", "gameplay_hex": records[0]["ai_pre"]["gameplay_hex"],
               "dynamics_hex": records[0]["ai_pre"]["dynamics_hex"], "rng": 7, "ai_tire_base": 0}
        post = {**pre, "boundary": "402be0-return"}
        game = bytearray.fromhex(pre["gameplay_hex"]); struct.pack_into("<I", game, 8, 0); game[0x83] = 25
        struct.pack_into("<4i", game, 0x60, 0, 0, 0, 2)
        post.update(gameplay_hex=game.hex(), dynamics_hex=records[0]["ai_post"]["dynamics_hex"])
        records[0]["local_handoffs"] = [{"kind": 0, "actor": 0, "return_address": 0x42160b, "stack_hex": struct.pack("<2I", 0x42160b, 0).hex(), "pre": pre, "post": post}]
        header["unrecorded_keyboard_warmup"] = {"keys_only": True, "recorded_boundaries": False, "continuous_from_grid": False,
            "targeted_local_finish": {"actor": 0, "target_lap": 1, "endpoint_boundary": "421aa0-entry", "keys_only": True,
                "recorded_boundaries": False, "continuous_from_grid": False,
                "endpoint": {"session_state": 6, "session_stage": 2, "update_counter": 100, "race_clock": 1000}}}
        return header, records

    def session_sidecars(self, header, records):
        for suffix, identity, expected in ((".session.csv", None, FLEET.expected_session), (".session-controller.csv", "actor", FLEET.expected_session_controller),
                                          (".session-classification.csv", None, FLEET.expected_session_classification)):
            with Path(str(self.path) + suffix).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["id", *([identity] if identity else []), *expected(records[0]["ai_post"])])
                writer.writeheader()
                writer.writerows({"id": record["id"], **({identity: header["local_actor"]} if identity else {}), **expected(record["ai_post"])} for record in records)
        FleetTestCase.write_handoff_csv(self, records, Path(str(self.path) + ".local-handoffs.csv"))

    def test_sparse_quality_requires_local_sixteenth_place_and_primary_results_commit(self):
        header, records = self.fixture()
        sparse = [FLEET.retained_local_session_record(record) for record in records]
        self.assertEqual(json.loads(json.dumps(sparse)), sparse)
        coverage = FLEET.retained_local_session_coverage(header, sparse)
        self.assertTrue(coverage["coverage_sufficient"])
        self.assertEqual(coverage["coverage"]["state13_update"], 5)
        self.assertFalse(FLEET.retained_local_session_coverage(header, sparse[:5])["coverage_sufficient"])
        sparse[-1]["combined_journal"] = []
        self.assertFalse(FLEET.retained_local_session_coverage(header, sparse)["coverage_sufficient"])

    def test_primary_can_write_pending13_and_commit_results_between_physical_records(self):
        header, records = self.fixture()
        sparse = [FLEET.retained_local_session_record(record) for record in records]
        for snapshot in (sparse[-2][phase] for phase in ("pre", "ai_pre", "ai_post", "consumed", "post")):
            snapshot["shared"]["finish_timing"]["pending_state_hex"] = struct.pack("<I", 10).hex()
        self.assertTrue(FLEET.retained_local_session_coverage(header, sparse)["coverage_sufficient"])
        for defect in ("missing_primary", "nonadjacent_record", "previous_pending", "current_pending", "current_state"):
            with self.subTest(defect=defect):
                changed = copy.deepcopy(sparse)
                if defect == "missing_primary": changed[-1]["combined_journal"] = []
                elif defect == "nonadjacent_record": changed[-1]["id"] += 1
                elif defect == "previous_pending": changed[-2]["post"]["shared"]["finish_timing"]["pending_state_hex"] = struct.pack("<I", 6).hex()
                elif defect == "current_pending": changed[-1]["pre"]["shared"]["finish_timing"]["pending_state_hex"] = struct.pack("<I", 10).hex()
                elif defect == "current_state": changed[-1]["pre"]["session_state"] = 10
                self.assertFalse(FLEET.retained_local_session_coverage(header, changed)["coverage_sufficient"])

    def test_neutral_primary_and_initial_lifecycle_are_the_only_clock_inputs(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<I", before[1], 8)[0], 6)
        self.assertEqual(struct.unpack_from("<7I", before[1], 128), (11, 22, 33, 44, 55, 1000, 0))
        self.assertEqual(struct.unpack_from("<8I", before[0], 32)[3:5], (1, 1))
        records[1]["post"]["shared"]["lifecycle"]["session_timers_hex"] = bytes([0xa5] * 20).hex()
        records[1]["post"]["shared"]["lifecycle"]["local_session_entry_flag"] = 1
        self.assertEqual(self.binaries(header, records), before)
        records[2]["combined_journal"][0]["event"]["cached_clock"] += 1
        with self.assertRaisesRegex(ValueError, "cached clock"): self.binaries(header, records)

    def test_unused_player_launch_slots_require_explicit_supported_initial_context(self):
        header, records = self.fixture()
        records[0]["pre"]["shared"]["provenance"]["local_launch_tires_hex"] = struct.pack("<4I", 0, 1, 0, 0).hex()
        with self.assertRaisesRegex(ValueError, "unused player launch slots"): self.binaries(header, records)

    def test_later_classification_modes_and_handoff_pre_are_expectations_only(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        for record in records[1:]:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = record[phase]
                for name, raw in list(snapshot["shared"]["session_boundary"].items()):
                    if isinstance(raw, bytes): snapshot["shared"]["session_boundary"][name] = bytes([0xa5]) * len(raw)
                for actor in snapshot["actors"]:
                    raw = bytearray(actor["dynamics"])
                    for offset in (0x27c, 0x310, 0x314, 0x318): raw[offset:offset + 4] = bytes([0xa5] * 4)
                    actor["dynamics"], actor["dynamics_hex"] = bytes(raw), raw.hex()
        self.assertEqual(self.binaries(header, records), before)
        event = records[0]["local_handoffs"][0]
        event["pre"]["rng"] ^= 1; event["post"]["rng"] ^= 1
        self.assertEqual(self.binaries(header, records), before)

    def test_existing_session_and_handoff_outputs_detect_changed_classification(self):
        header, records = self.fixture()
        self.session_sidecars(header, records)
        report = FLEET.compare_retained_local_session(header, records, self.path)
        self.assertTrue(report["parity"])
        raw = bytearray.fromhex(records[2]["ai_post"]["shared"]["session_boundary"]["configuration_tail_hex"]); raw[4] ^= 1
        records[2]["ai_post"]["shared"]["session_boundary"]["configuration_tail_hex"] = raw.hex()
        report = FLEET.compare_retained_local_session(header, records, self.path)
        self.assertFalse(report["numerical_parity"])
        self.assertEqual(report["exact_prefix_updates"], 2)

    def test_mapper_axes_stop_at_generated_handoff_and_lifecycle_checkpoint_is_unique(self):
        from .drivers import DriverRetentionTests

        header, records = self.fixture()
        DriverRetentionTests.sidecars(self, header, records)
        report = FLEET.compare_driver_retention(header, records, self.path)
        self.assertEqual(report["human_mapper_transactions"], 0)
        source = Path(str(self.path) + ".driver-world.csv")
        with source.open() as stream: lifecycle = [row for row in csv.DictReader(stream) if row["kind"] == "3"]
        self.assertEqual(len(lifecycle), len(records) * 9)
        raw = bytearray.fromhex(records[1]["consumed"]["actors"][0]["dynamics_hex"]); raw[0x140] ^= 1
        records[1]["consumed"]["actors"][0]["dynamics_hex"] = raw.hex()
        self.assertFalse(FLEET.compare_driver_retention(header, records, self.path)["parity"])

    def test_result_wrapper_axes_are_unclaimed_only_before_the_consumed_ai_mapper(self):
        from .drivers import DriverPhysicsTests
        from .drivers import DriverRetentionTests

        header, records = self.fixture()
        DriverRetentionTests.sidecars(self, header, records)
        self.session_sidecars(header, records)
        traffic = FleetTestCase.traffic_csv(self, records)
        final = records[-1]
        for phase in ("pre", "ai_pre", "ai_post", "traffic_pre"):
            snapshot = final[phase]
            raw = bytearray.fromhex(snapshot["actors"][0]["dynamics_hex"])
            struct.pack_into("<4I", raw, 0x138, 0xa5d9d8db, 0x12345678, 0x76543210, 0x10203040)
            snapshot["actors"][0]["dynamics_hex"] = snapshot["dynamics_hex"] = raw.hex()
        driver = FLEET.compare_driver_retention(header, records, self.path)
        session = FLEET.compare_retained_local_session(header, records, self.path)
        route = FLEET.compare_traffic_controls(header, records, traffic)
        self.assertTrue(driver["parity"] and session["numerical_parity"] and route["parity"])
        self.assertEqual([report["unclaimed_control_values"] for report in (driver, session, route)], [4, 2, 2])
        self.assertEqual(driver["unclaimed_control_fields"][0]["boundary"], "403dd0-entry")
        self.assertEqual(session["unclaimed_control_fields"][0]["fields"], ["raw_steering_left", "raw_steering_right"])
        self.assertEqual(route["unclaimed_control_fields"][0]["fields"], ["raw_left", "raw_right"])
        #415090 consumes the generated non-reversing AI mapper, whose axes
        #remain fully checked even though that actor was previously human.
        raw = bytearray.fromhex(final["consumed"]["actors"][0]["dynamics_hex"]); raw[0x140] ^= 1
        final["consumed"]["actors"][0]["dynamics_hex"] = raw.hex()
        self.assertFalse(FLEET.compare_driver_retention(header, records, self.path)["parity"])
        self.assertFalse(FLEET.compare_traffic_controls(header, records, traffic)["parity"])
        self.assertFalse(FLEET.local_state13_wrapper_axes_unclaimed(header, records[-2]["ai_pre"], 0))
        self.assertFalse(FLEET.local_state13_wrapper_axes_unclaimed(header, final["ai_pre"], 1))
        reverse = copy.deepcopy(final["ai_pre"])
        game = bytearray.fromhex(reverse["actors"][0]["gameplay_hex"]); struct.pack_into("<I", game, 0x28, 1)
        reverse["actors"][0]["gameplay_hex"] = game.hex()
        self.assertFalse(FLEET.local_state13_wrapper_axes_unclaimed(header, reverse, 0))
        physical = DriverPhysicsTests.sidecars(self, records)
        self.assertTrue(FLEET.compare(records, physical, "independent-physics")["parity"])
        with physical.open(newline="") as stream: rows = list(csv.DictReader(stream))
        output = next(row for row in rows if int(row["id"]) == final["id"] and row["actor"] == "0" and row["phase"] == "integration")
        output["raw_left"] = str(int(output["raw_left"]) + 1)
        with physical.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        self.assertFalse(FLEET.compare(records, physical, "independent-physics")["parity"])


class WarmRestartInputTests(TraceTestCase):
    trace_filename = 'driver'

    def grid_event(self, counter=1703, clock=9100):
        from .cold_grid import ColdGridInitializationTests

        scalar = {"release_counter": 0, "rng": 0x12345678, "race_clock": clock, "release_epoch": 1000,
                  "source_mode": 0, "session_stage": 2, "local_players": 1}
        seed = scalar["rng"]
        for _ in range(15): seed = (seed * 0x343fd + 0x269ec3) & 0xffffffff
        legacy = {"return_address": 0x4479e5, "pre": {**scalar, "boundary": "460170-entry"},
                  "post": {**scalar, "rng": seed, "boundary": "460170-return"}}
        boundaries = {}
        for phase in ("pre", "post"):
            reader = ColdGridInitializationTests.Reader(legacy[phase])
            reader.put(0x4d383c, struct.pack("<I", counter))
            boundaries[phase] = FLEET.capture_cold_grid_boundary(reader, legacy[phase]["boundary"])
        return {"order": 4, "outer_return_address": 0x4487d2, "initialization": legacy, "boundaries": boundaries}

    def fixture(self):
        header, records = RetainedLocalSessionTests.fixture(self)
        header["warm_restart_manifest"] = FLEET.WARM_RESTART_MANIFEST
        for index, clock in enumerate((50200, 50240), len(records)):
            record = copy.deepcopy(records[-1])
            record.update(id=index, local_handoffs=[], combined_journal=[], update_caller_return_address=0x4488a1)
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = record[phase]
                snapshot.update(session_state=9, race_clock=clock, update_counter=100 + index + (phase != "pre"))
                shared = snapshot["shared"]
                shared["lifecycle"].update(session_timers_hex=struct.pack("<5I", 50100, 50100, 50200, 50200, 50100).hex(),
                    lifecycle_clock_hex=struct.pack("<I", clock).hex(),
                    race_lifecycle_hex=struct.pack("<5I", 0, snapshot["update_counter"], clock, 1000, 0).hex())
                shared["finish_timing"]["pending_state_hex"] = struct.pack("<I", 9).hex()
            if index == 6:
                grid = self.grid_event(counter=106, clock=clock); grid["order"] = 5
                record["combined_journal"] = [
                    {"kind": "primary", "event": {"order": 1, "cached_clock": 50000}},
                    {"kind": "restart_request", "event": {"order": 2, "action": "confirm"}},
                    {"kind": "restart", "event": {"order": 3, "creation_clock": 50100}},
                    {"kind": "primary", "event": {"order": 4, "cached_clock": 50100}},
                    {"kind": "warm_grid", "event": grid}]
            records.append(record)
        return header, records

    def test_targeted_coverage_requires_actual_reset_and_two_retained_counter_updates(self):
        header, records = self.fixture()
        sparse = [FLEET.retained_local_session_record(record) for record in records]
        self.assertEqual(json.loads(json.dumps(sparse)), sparse)
        coverage = FLEET.retained_warm_restart_coverage(header, sparse)
        self.assertTrue(coverage["coverage_sufficient"])
        self.assertEqual(coverage["coverage"]["warm_update_ids"], [6, 7])
        self.assertEqual(coverage["coverage"]["warm_grid_writers"][0]["counter_after"], 106)
        for end in (6, 7):
            self.assertFalse(FLEET.retained_warm_restart_coverage(header, sparse[:end])["coverage_sufficient"])
        bad = copy.deepcopy(sparse)
        bad[6]["combined_journal"][1]["event"]["action"] = "cancel"
        with self.assertRaisesRegex(ValueError, "confirmation"): FLEET.retained_warm_restart_coverage(header, bad)
        bad = copy.deepcopy(sparse)
        bad[6]["combined_journal"] = bad[6]["combined_journal"][2:]
        with self.assertRaisesRegex(ValueError, "confirmation"): FLEET.retained_warm_restart_coverage(header, bad)

    def test_v7_wire_uses_pre_clock_and_neutral_reset_events_without_observed_outputs(self):
        from .drivers import DriverRetentionTests

        header, records = self.fixture()
        fleet, journal = DriverRetentionTests.binaries(self, header, records)
        self.assertEqual(struct.unpack_from("<I", journal, 8)[0], 7)
        changed = copy.deepcopy(records)
        changed[6]["ai_pre"]["race_clock"] ^= 0x2000
        # The sole caller clock is PRE; later AI clock aliases never enter v7.
        self.assertEqual(DriverRetentionTests.binaries(self, header, changed), (fleet, journal))
        changed = copy.deepcopy(records)
        changed[6]["pre"]["race_clock"] += 1
        changed_fleet, changed_journal = DriverRetentionTests.binaries(self, header, changed)
        self.assertNotEqual(changed_fleet, fleet)
        self.assertEqual(changed_journal, journal)
        changed = copy.deepcopy(records)
        snapshot = changed[6]["combined_journal"][-1]["event"]["boundaries"]["post"]
        raw = bytearray.fromhex(snapshot["actors"][0]["body_hex"]); raw[0] ^= 1
        snapshot["actors"][0]["body_hex"] = raw.hex()
        snapshot["crc32"]["body"] = f"{zlib.crc32(FLEET.cold_grid_boundary_bytes(snapshot)['body']):08x}"
        self.assertEqual(DriverRetentionTests.binaries(self, header, changed), (fleet, journal))

    def test_warm_grid_expectations_clear_only_source_owned_audio_queue_fields(self):
        header, records = self.fixture()
        for actor, state in enumerate(header["wheel_queue_initial_state"]["actors"]):
            state.update(events_hex=struct.pack("<4h", *range(actor * 4, actor * 4 + 4)).hex(),
                strengths_hex=struct.pack("<4i", *range(100 + actor * 4, 104 + actor * 4)).hex(),
                flag_segment_hex=struct.pack("<8i", *([1] * 4), *([2] * 4)).hex())
        audio = header["combined_audio_initial"]
        audio["camera_state_hex"] = struct.pack("<2i", 3, 4).hex()
        for name in ("reset", "pending_mute"): audio[name + "_hex"] = struct.pack("<16i", *range(10, 26)).hex()
        audio["surface_hex"], audio["mask_hex"], audio["wheel_materials_hex"] = bytes(range(16)).hex(), bytes(range(16, 32)).hex(), bytes(range(64)).hex()
        record = records[6]
        record["combined_journal"] = [record["combined_journal"][-1]]
        with patch.object(FLEET, "combined_events", return_value=[("warm_grid", None, record["combined_journal"][0]["event"])]):
            expected = FLEET.combined_expected(header, [record])
        first = expected[".combined-audio.csv"][0]
        self.assertEqual((first["primary"], first["secondary"]), (3, 4))
        self.assertEqual([first["current" + str(i)] for i in range(4)], [0] * 4)
        self.assertEqual([first["previous" + str(i)] for i in range(4)], [0] * 4)
        self.assertEqual([first["event" + str(i)] for i in range(4)], list(range(4)))
        self.assertEqual([first["strength" + str(i)] for i in range(4)], list(range(100, 104)))
        self.assertEqual((first["dom"], first["reset"], first["mask"], first["pending"]), (0, 10, 16, 10))
        self.assertEqual([first["material" + str(i)] for i in range(4)], list(range(4)))
        names = ("previous_gear", "phase", "shift", "ramp", "throttle", "mirror", "volume")
        self.assertEqual([expected[".combined-engine.csv"][0][name] for name in names],
                         [value if field == "phase" or value < 0x80000000 else value - 0x100000000
                          for field, value in header["whine_initial_state"]["state"].items()])

    def test_exact_local_finish_prefix_cannot_pass_combined_warm_target(self):
        header, records = self.fixture()
        records = records[:6]
        self.assertTrue(FLEET.retained_local_session_coverage(header, records)["coverage_sufficient"])
        local = FLEET.retained_warm_restart_coverage(header, records)
        base = {"parity": True, "numerical_parity": True, "coverage_sufficient": True, "mismatch_count": 0,
                "compared_values": 0, "first_mismatches": [], "exact_prefix_updates": len(records)}
        joined = {**base}
        wheel = {**base, "skid_dispatch": {"coverage": {"draws": 0}}}
        whine = {**base, "coverage": {"rng_advance_calls": 0}, "positive_command_coverage": False}
        with patch.multiple(FLEET, validate_combined_scope=lambda *args, **kwargs: None,
                combined_expected=lambda *args: {}, combined_pre_expectations=lambda *args: [],
                compare_joined_ai=lambda *args: joined, compare_wheel_queue=lambda *args: wheel,
                compare_whine=lambda *args, **kwargs: whine,
                compare_contact_weather=lambda *args: {**base, "positive_random_coverage": False},
                compare_driver_retention=lambda *args: {**base, "human_mapper_transactions": 0},
                compare_retained_race_timing=lambda *args: {**base},
                compare_retained_local_session=lambda *args: {**base, **local},
                compare_human_lamps=lambda *args: {**base, "row_count": 0},
                compare_brake_publication=lambda *args: {**base}):
            report = FLEET.compare_combined(header, records, self.path, compare_retained_drivers=True)
        self.assertTrue(report["numerical_parity"])
        self.assertFalse(report["coverage_sufficient"])
        self.assertFalse(report["parity"])

    def test_only_v7_interstitial_state13_menu_wheel_bypass_is_supported(self):
        header, records = self.fixture()
        callback = {"kind": "wheel", "event": {"order": 2, "pre": {"render_bypass": 1}, "post": {"render_bypass": 1}}}
        records[6]["combined_journal"].insert(1, callback)
        FLEET.validate_retained_lamp_callback_scope(header, records)
        old = {name: value for name, value in header.items() if name != "warm_restart_manifest"}
        with self.assertRaisesRegex(ValueError, "unpaused"): FLEET.validate_retained_lamp_callback_scope(old, records)
        records[5]["ai_post"]["session_state"] = 10
        with self.assertRaisesRegex(ValueError, "unpaused"): FLEET.validate_retained_lamp_callback_scope(header, records)
        records[5]["ai_post"]["session_state"] = 13
        records[6]["combined_journal"].remove(callback)
        records[6]["combined_journal"].append(callback)
        with self.assertRaisesRegex(ValueError, "unpaused"): FLEET.validate_retained_lamp_callback_scope(header, records)
        records[6]["combined_journal"].remove(callback)
        header["wheel_queue_initial_state"]["render_bypass"] = 1
        with self.assertRaisesRegex(ValueError, "unpaused"): FLEET.validate_retained_lamp_callback_scope(header, records)

    def test_warm_grid_retains_nonzero_update_counter(self):
        event = self.grid_event()
        before, after = FLEET.validate_warm_grid_event(event)
        self.assertEqual(struct.unpack_from("<I", before["race_lifecycle"], 4)[0], 1703)
        self.assertEqual(struct.unpack_from("<I", after["race_lifecycle"], 4)[0], 1703)
        snapshot = event["boundaries"]["post"]
        raw = bytearray.fromhex(snapshot["globals"]["race_lifecycle_hex"])
        struct.pack_into("<I", raw, 4, 0)
        snapshot["globals"]["race_lifecycle_hex"] = raw.hex()
        snapshot["crc32"]["race_lifecycle"] = f"{zlib.crc32(raw):08x}"
        with self.assertRaisesRegex(ValueError, "counter"): FLEET.validate_warm_grid_event(event)

    def test_warm_grid_chronology_resets_only_its_release_counter_and_checks_rng(self):
        _, records = joined_fixture()
        record = records[0]
        record["combined_journal"] = [{"kind": "warm_grid", "event": self.grid_event()}]
        self.assertEqual(FLEET.validate_joined_release(record, 55, local_session=True), 0)
        event = record["combined_journal"][0]["event"]
        event["initialization"]["post"]["rng"] ^= 1
        snapshot = event["boundaries"]["post"]
        snapshot["globals"]["rng_hex"] = struct.pack("<I", event["initialization"]["post"]["rng"]).hex()
        snapshot["crc32"]["rng"] = f"{zlib.crc32(bytes.fromhex(snapshot['globals']['rng_hex'])):08x}"
        with self.assertRaisesRegex(ValueError, "fifteen-draw"): FLEET.validate_joined_release(record, 55, local_session=True)

    def test_only_v7_overlay_primaries_allow_external_clock_samples_without_an_ai_update(self):
        header, records = RetainedLocalSessionTests.fixture(self)
        extra = copy.deepcopy(records[-1]); extra["id"] = 6
        extra["combined_journal"] = [{"kind": "primary", "event": {"order": 1, "cached_clock": 6500}},
                                      {"kind": "primary", "event": {"order": 2, "cached_clock": 6700}}]
        records.append(extra)
        with self.assertRaisesRegex(ValueError, "cached clock"): FLEET.validate_local_session_lifecycle(header, records)
        header["warm_restart_manifest"] = FLEET.WARM_RESTART_MANIFEST
        FLEET.validate_local_session_lifecycle(header, records)
