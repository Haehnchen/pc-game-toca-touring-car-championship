"""Fleet reference checks for schema."""

from .support import (
    CLI,
    FLEET,
    FleetTestCase,
    ai_fixture,
    csv,
    finish_fixture,
    fixture,
    io,
    json,
    patch,
    state,
    struct,
    subprocess,
    sys,
    timing_fixture,
    write_trace,
    zlib,
)


class FleetCliTests(FleetTestCase):
    def test_cli_prefix_preserves_complete_source_and_detached_comparison(self):
        for compare_ai, compare_timing, compare_finish in ((False, False, False), (True, False, False), (True, True, False), (True, True, True)):
            with self.subTest(compare_ai=compare_ai, compare_timing=compare_timing, compare_finish=compare_finish):
                write_trace(self.path, *(finish_fixture(2) if compare_finish else timing_fixture(2) if compare_timing else ai_fixture(2) if compare_ai else fixture(2)))
                original = self.path.read_bytes()
                arguments = ["replay-fleet-reference.py", str(self.path), "--updates", "1"]
                if compare_ai:
                    arguments.append("--compare-ai-route")
                if compare_timing:
                    arguments.append("--compare-ai-timing")
                if compare_finish:
                    arguments.append("--compare-ai-finish")
                physical = {"parity": True, "exact_prefix_updates": 1, "mismatch_count": 0,
                            "coverage": {"contact_coverage": True, "damage_coverage": False, "roughness_coverage": False, "detached_coverage": False}}
                detached = {"parity": True, "exact_prefix_updates": 1, "mismatch_count": 0}
                launch = {"parity": True, "derived_output_parity": None, "exact_prefix_updates": 1, "mismatch_count": 0}
                selection = {"parity": True, "exact_prefix_updates": 1, "mismatch_count": 0}
                ai = {"parity": False, "supported_parity": None, "exact_prefix_updates": 0,
                      "supported_exact_updates": 0, "supported_updates": 0,
                      "unsupported_updates": [{"id": 0, "reason": "route progression"}]}
                timing = {"parity": False, "exact_prefix_updates": 0, "mismatch_count": 0}
                finish = {"parity": False, "exact_prefix_updates": 0, "mismatch_count": 0, "coverage": {"newly_finished_actors": []}}
                with patch.object(sys, "argv", arguments), \
                        patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execution, \
                        patch.object(CLI.FLEET, "compare", return_value=physical) as compare, \
                        patch.object(CLI.FLEET, "compare_detached", return_value=detached) as compare_detached, \
                        patch.object(CLI.FLEET, "compare_launch", return_value=launch) as compare_launch, \
                        patch.object(CLI.FLEET, "compare_selection", return_value=selection) as compare_selection, \
                        patch.object(CLI.FLEET, "compare_ai_route", return_value=ai) as compare_ai_route, \
                        patch.object(CLI.FLEET, "compare_ai_timing", return_value=timing) as compare_ai_timing, \
                        patch.object(CLI.FLEET, "compare_ai_finish", return_value=finish) as compare_ai_finish, \
                        patch.object(sys, "stdout", io.StringIO()) as stdout:
                    self.assertEqual(CLI.main(), 1 if compare_ai else 0)
                imported = (self.path.parent / "fleet-replay.bin").read_bytes()
                self.assertEqual(struct.unpack_from("<I", imported, 16)[0], 1)
                self.assertEqual(struct.unpack_from("<I", imported, 32)[0], int(compare_ai))
                self.assertEqual(struct.unpack_from("<I", imported, 36)[0], int(compare_timing))
                self.assertEqual(struct.unpack_from("<I", imported, 40)[0], int(compare_finish))
                command = execution.call_args.args[0]
                self.assertEqual("--compare-ai-route" in command, compare_ai)
                self.assertEqual("--compare-ai-timing" in command, compare_timing)
                self.assertEqual("--compare-ai-finish" in command, compare_finish)
                measured = compare.call_args.args[0]
                self.assertEqual(len(measured), 1)
                self.assertIs(compare_detached.call_args.args[0], measured)
                self.assertIs(compare_launch.call_args.args[0], measured)
                self.assertIs(compare_selection.call_args.args[0], measured)
                if compare_ai:
                    self.assertIs(compare_ai_route.call_args.args[1], measured)
                else:
                    compare_ai_route.assert_not_called()
                if compare_timing:
                    self.assertIs(compare_ai_timing.call_args.args[1], measured)
                else:
                    compare_ai_timing.assert_not_called()
                if compare_finish:
                    self.assertIs(compare_ai_finish.call_args.args[1], measured)
                    self.assertTrue(compare_ai_timing.call_args.kwargs["compare_finish"])
                else:
                    compare_ai_finish.assert_not_called()
                self.assertEqual(compare_detached.call_args.args[1].name, "fleet-native-continuous-physics-inputs.csv.detached.csv")
                report = json.loads((self.path.parent / "fleet-comparison-continuous-physics-inputs.json").read_text())
                self.assertEqual(report["source_updates"], 2)
                self.assertEqual(report["trace_crc32"], f"{zlib.crc32(original):08x}")
                self.assertIs(report["physical_parity"], True)
                self.assertEqual(report["physical_exact_prefix_updates"], 1)
                self.assertEqual(report["exact_prefix_updates"], 0 if compare_ai else 1)
                self.assertEqual(report["parity"], not compare_ai)
                self.assertIs(report["contact_parity"], True)
                self.assertIsNone(report["damage_parity"])
                self.assertIsNone(report["detached_parity"])
                self.assertIsNone(report["local_launch_parity"])
                self.assertIn("1 exact physical updates", stdout.getvalue())
                self.assertEqual(self.path.read_bytes(), original)

    def test_cli_early_python_failure_replaces_stale_success_report(self):
        header, records = fixture(1)
        header["schema"] = 999
        write_trace(self.path, header, records)
        report_path = self.path.parent / "fleet-comparison-continuous-physics-inputs.json"
        report_path.write_text(json.dumps({"status": "compared", "parity": True, "old_success": True}))
        arguments = ["replay-fleet-reference.py", str(self.path)]
        with patch.object(sys, "argv", arguments), patch.object(CLI.subprocess, "run") as execution, patch.object(sys, "stderr", io.StringIO()):
            self.assertEqual(CLI.main(), 2)
        execution.assert_not_called()
        report = json.loads(report_path.read_text())
        self.assertEqual(report["status"], "error")
        self.assertIs(report["parity"], False)
        self.assertEqual(report["mode"], "continuous-physics-inputs")
        self.assertIn("canonical fleet schema", report["reason"])
        self.assertNotIn("old_success", report)

    def test_cli_native_timeout_rejects_nonpositive_and_nonfinite_values_before_replay(self):
        for value in ("0", "-1", "nan", "inf", "-inf", "invalid"):
            with self.subTest(value=value), patch.object(sys, "argv", ["replay-fleet-reference.py", str(self.path), "--native-timeout=" + value]), \
                    patch.object(CLI, "replay") as replay, patch.object(sys, "stderr", io.StringIO()):
                with self.assertRaises(SystemExit) as error: CLI.main()
                self.assertEqual(error.exception.code, 2)
                replay.assert_not_called()

    def test_cli_native_timeout_reaches_every_native_branch_and_preserves_timeout_error(self):
        header, records = fixture(1)
        writers = ("write_cold_grid_sequence_binary", "write_cold_grid_binary", "write_combined_binary", "write_binary",
                   "write_startup_join_binary", "write_startup_clock_binary", "write_skid_binary", "validate_replay_scope")
        for option in (None, "--compare-cold-grid-sequence", "--compare-cold-grid-init", "--compare-combined-rng",
                       "--compare-startup-world-reset", "--compare-skid-dispatch"):
            for timeout in (None, "600", "0.5"):
                with self.subTest(option=option, timeout=timeout):
                    argv = ["replay-fleet-reference.py", str(self.path), *([option] if option else []),
                            *(["--native-timeout", timeout] if timeout else [])]
                    expected = 120 if timeout is None else float(timeout)
                    with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                            patch.multiple(CLI.FLEET, **{name: lambda *args, **kwargs: None for name in writers}), \
                            patch.object(CLI.subprocess, "run", side_effect=subprocess.TimeoutExpired("native", expected)) as execution, \
                            patch.object(sys, "stderr", io.StringIO()):
                        self.assertEqual(CLI.main(), 2)
                    execution.assert_called_once()
                    self.assertEqual(execution.call_args.kwargs["timeout"], expected)
                    mode = {None: "continuous-physics-inputs", "--compare-cold-grid-sequence": "cold-grid-sequence",
                            "--compare-cold-grid-init": "cold-grid-initialization", "--compare-combined-rng": "combined-rng",
                            "--compare-startup-world-reset": "startup-world-reset", "--compare-skid-dispatch": "independent-skid-dispatch"}[option]
                    report = json.loads((self.path.parent / f"fleet-comparison-{mode}.json").read_text())
                    self.assertEqual(report["status"], "error")
                    self.assertFalse(report["parity"])
                    self.assertIn("timed out", report["reason"])

    def test_cli_native_signal_is_error_and_explicit_unsupported_exit_is_distinct(self):
        write_trace(self.path, *fixture(1))
        report_path = self.path.parent / "fleet-comparison-continuous-physics-inputs.json"
        arguments = ["replay-fleet-reference.py", str(self.path), "--replay", str(self.path.parent / "unused-replay")]
        for returncode, expected_status, stderr in ((-11, "error", ""), (1, "error", "Cannot open output"),
                                                     (2, "unsupported", "Unsupported source state")):
            with self.subTest(returncode=returncode):
                result = subprocess.CompletedProcess([], returncode, "", stderr)
                with patch.object(sys, "argv", arguments), patch.object(CLI.subprocess, "run", return_value=result) as execution, \
                        patch.object(CLI.FLEET, "compare") as compare, patch.object(sys, "stderr", io.StringIO()):
                    self.assertEqual(CLI.main(), 2)
                execution.assert_called_once()
                compare.assert_not_called()
                report = json.loads(report_path.read_text())
                self.assertEqual(report["status"], expected_status)
                self.assertIs(report["parity"], False)
                self.assertEqual(report["native_exit_code"], returncode)
                self.assertEqual(report["requested_updates"], 1)
                self.assertEqual(report["source_updates"], 1)
                self.assertTrue(report["reason"])
                if stderr:
                    self.assertEqual(report["reason"], stderr)
                else:
                    self.assertIn(str(returncode), report["reason"])


class FleetSchemaTests(FleetTestCase):
    def test_lifecycle_clock_and_manifest_are_observation_evidence(self):
        header, records = fixture(1)
        header["lifecycle_blocks"] = FLEET.LIFECYCLE_BLOCKS
        for snapshot in records[0].values():
            if not isinstance(snapshot, dict):
                continue
            snapshot["race_clock"] = 12345
            lifecycle = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.LIFECYCLE_BLOCKS.items()}
            race = bytearray(0x14)
            struct.pack_into("<I", race, 8, 12345)
            lifecycle["race_lifecycle_hex"] = race.hex()
            snapshot["shared"]["lifecycle"] = lifecycle
        write_trace(self.path, header, records)
        _, parsed = FLEET.read_trace(self.path)
        self.assertEqual(parsed[0]["pre"]["race_clock"], 12345)
        records[0]["ground"]["race_clock"] += 1
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "race clock"):
            FLEET.read_trace(self.path)

    def test_coverage_distinguishes_repeated_dispatch_contacts_and_roughness(self):
        initial = state("421aa0-entry", 0)
        coverage = FLEET.Coverage(initial)
        for tick in range(3):
            consumed = state("415090-entry", tick)
            post = state("421aa0-return", tick + 1, True)
            raw = bytearray.fromhex(consumed["actors"][0]["body_hex"])
            struct.pack_into("<I", raw, 0x4f0, (tick + 1) % 2)
            consumed["actors"][0]["body_hex"] = raw.hex()
            post["roughness_cursor"] = 1
            post["shared"]["detached_count"] = 2
            coverage.consume(consumed, post)
        result = coverage.summary()
        self.assertEqual(result["full_distant_transitions_per_actor"][0], 3)
        self.assertEqual(result["actors_with_repeated_full_distant_transitions"], [0])
        self.assertEqual(result["unique_contact_pairs"], [(0, 1)])
        self.assertEqual(result["contact_updates"], 3)
        self.assertEqual(result["roughness_changed_updates"], 3)
        self.assertEqual(result["maximum_detached_count"], 2)

    def test_complete_all_actor_five_boundaries_and_binary_offsets(self):
        header, records = fixture()
        # Real active barrier pointers become record indices in native input.
        barrier = bytearray.fromhex(records[0]["consumed"]["shared"]["barriers_hex"])
        struct.pack_into("<I", barrier, 0xc, 0x600000 + 3 * 0x58)
        struct.pack_into("<I", barrier, 0x28, 0x600000 + 5 * 0x58)
        struct.pack_into("<II", barrier, 0x2c, 1, 1)
        records[0]["consumed"]["shared"]["barriers_hex"] = barrier.hex()
        flags = bytearray.fromhex(records[0]["consumed"]["shared"]["limiter_active_hex"])
        loads = bytearray.fromhex(records[0]["consumed"]["shared"]["limiter_load_hex"])
        struct.pack_into("<I", flags, 15 * 4, 1)
        struct.pack_into("<i", loads, 15 * 4, -123)
        records[0]["consumed"]["shared"]["limiter_active_hex"] = flags.hex()
        records[0]["consumed"]["shared"]["limiter_load_hex"] = loads.hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        output = self.path.with_suffix(".bin")
        FLEET.write_binary(output, header, records)
        raw = output.read_bytes()
        self.assertEqual(len(raw), 64 + 2 * (42571 + 12 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES))
        self.assertEqual(struct.unpack_from("<8s6I", raw), (b"TOCAFLET", 13, 16, 2, 0, 1, 2))
        self.assertEqual(struct.unpack_from("<IiiIIIIiIII", raw, 64), (0, 2621, 1638425, 0, 0x12345678, 0, 0, 0, 0, 0, 0))
        actor0 = 64 + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 12
        self.assertEqual(struct.unpack_from("<4H4B", raw, actor0), (0, 0xffff, 0xffff, 0xffff, 1, 0, 1, 0))
        self.assertEqual(struct.unpack_from("<I", raw, actor0 + 20 + 0xc)[0], 3)
        self.assertEqual(struct.unpack_from("<I", raw, actor0 + 20 + 0x28)[0], 5)
        actor15 = actor0 + 15 * 2528
        self.assertEqual(struct.unpack_from("<Ii", raw, actor15 + 12), (1, -123))
        self.assertEqual(struct.unpack_from("<i", raw, actor15 + 20 + 0x34 + 0x53c + 0x140)[0], 10015)

    def test_crc_and_complete_footer_are_required(self):
        header, records = fixture()
        write_trace(self.path, header, records)
        original = self.path.read_bytes()
        for damaged in (original[:-1], original.replace(b"305419896", b"305419897", 1), original.splitlines(keepends=True)[0]):
            self.path.write_bytes(damaged)
            with self.assertRaises((ValueError, KeyError)):
                FLEET.read_trace(self.path)

    def test_mutated_actor_array_boundary_and_decoded_fields_rejected(self):
        def mutate_root(records):
            records[0]["ground"]["actors"][15]["root"]["sector"] = 12
        def mutate_actor(records):
            records[0]["contacts"]["actors"][3]["actor"] = 2
        def mutate_input(records):
            records[0]["consumed"]["input_q16"]["throttle"] = 0
        def skip_update(records):
            records[1]["pre"]["update_counter"] = 8
        def remove_boundary(records):
            del records[0]["ground"]
        for mutation in (mutate_root, mutate_actor, mutate_input, skip_update, remove_boundary):
            with self.subTest(mutation=mutation.__name__):
                header, records = fixture()
                mutation(records)
                write_trace(self.path, header, records)
                with self.assertRaises((ValueError, KeyError)):
                    FLEET.read_trace(self.path)

    def test_noqual_and_manifest_prove_observed_session(self):
        for field in ("configuration_hex", "shared_words", "allocated_actor_count"):
            header, records = fixture()
            if field == "configuration_hex":
                raw = bytearray.fromhex(header[field])
                struct.pack_into("<I", raw, 0x20, 0)
                header[field] = raw.hex()
            elif field == "shared_words":
                header[field] = {**header[field], "damage_cycle": 0}
            else:
                header[field] = 1
            write_trace(self.path, header, records)
            with self.assertRaises(ValueError):
                FLEET.read_trace(self.path)

    def test_held_grid_is_not_moving_fleet_coverage(self):
        initial = state("421aa0-entry", 0)
        coverage = FLEET.Coverage(initial)
        coverage.consume(initial, initial)
        self.assertFalse(coverage.summary()["passed"])
        moving = state("421aa0-return", 1, True)
        moving["roughness_cursor"] = 2
        coverage.consume(initial, moving)
        result = coverage.summary()
        self.assertTrue(result["passed"])
        self.assertEqual(result["actors_moved"], list(range(16)))
        self.assertEqual(result["body_mode_actor_updates"], {"0": 16, "1": 16})
        self.assertEqual(result["confirmed_neighbor_entries"], 1)
        self.assertEqual(result["roughness_advances_mod15"], 2)

    def test_comparison_accounts_for_all_actors_phases_and_signed_differences(self):
        header, records = fixture()
        write_trace(self.path, header, records)
        _, records = FLEET.read_trace(self.path)
        rows = []
        for record in records:
            for phase, name in (("integration", "contacts"), ("ground", "ground")):
                for actor, entry in enumerate(record[name]["actors"]):
                    rows.append({"id": record["id"], "actor": actor, "phase": phase,
                                 **FLEET.expected_actor(entry, record[name])})
        native = self.path.with_suffix(".csv")
        def save():
            with native.open("w", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
        save()
        report = FLEET.compare(records, native, "continuous-physics-inputs")
        self.assertTrue(report["parity"])
        self.assertFalse(report["autonomous_ai_parity"])
        self.assertEqual(report["initial_world_imports"], 1)
        self.assertEqual(report["actor_state_imports"], 16)
        self.assertEqual(report["compared_values"], 64 * len(FLEET.expected_actor(records[0]["ground"]["actors"][0], records[0]["ground"])))
        # A final actor's ground difference must not disappear in an aggregate.
        rows[-1]["velocity_x"] -= 3
        save()
        report = FLEET.compare(records, native, "independent-physics")
        self.assertEqual(report["mismatch_count"], 1)
        self.assertEqual(report["first_mismatches"][0], {"update": 1, "actor": 15, "phase": "ground",
                                                         "field": "velocity_x", "original": 0, "native": -3, "difference": -3})
        self.assertEqual(report["exact_prefix_updates"], 1)
        self.assertEqual(report["actor_state_imports"], 32)
        rows[-1] = dict(rows[0])
        save()
        with self.assertRaisesRegex(ValueError, "Repeated"):
            FLEET.compare(records, native, "independent-physics")

    def test_cold_prop_baseline_is_not_assumed_for_already_moving_props(self):
        snapshot = state("415090-entry")
        cone = bytearray(0xcc)
        cone[0xc0] = 4
        snapshot["shared"]["props"]["cones"] = {"count": 1, "stride": 0xcc, "records_hex": cone.hex()}
        FLEET.validate_snapshot(snapshot, "415090-entry")
        self.assertEqual(FLEET.cold_prop_baseline(snapshot), [])
        cone[0x84] = 1
        snapshot["shared"]["props"]["cones"]["records_hex"] = cone.hex()
        FLEET.validate_snapshot(snapshot, "415090-entry")
        self.assertEqual(FLEET.cold_prop_baseline(snapshot), [{"pool": "cones", "index": 0}])

    def test_warm_world_damage_and_debris_numeric_inputs_survive_v3(self):
        header, records = fixture(1)
        shared = records[0]["consumed"]["shared"]
        levels = bytearray.fromhex(shared["damage_levels_hex"])
        levels[3 * 16 + 15] = 2
        broken = bytearray.fromhex(shared["broken_features_hex"])
        struct.pack_into("<I", broken, 15 * 16 + 8, 1)
        pool = bytearray.fromhex(shared["detached_pool_hex"])
        struct.pack_into("<I", pool, 0x108, 1)
        pool[0x10c] = 3
        shared.update(damage_levels_hex=levels.hex(), broken_features_hex=broken.hex(), detached_pool_hex=pool.hex(),
                      detached_free_hex=struct.pack("<10I", 0, *([1] * 9)).hex(), detached_count=1)
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        FLEET.validate_replay_scope(records, "independent-physics")
        output = self.path.with_suffix(".bin")
        FLEET.write_binary(output, header, records)
        raw = output.read_bytes()
        world = 64 + 44 + 0x800 + 16 + 15
        self.assertEqual(raw[world + 3 * 16 + 15], 2)
        self.assertEqual(struct.unpack_from("<I", raw, world + 272 + 15 * 16 + 8)[0], 1)
        self.assertEqual(raw[world + 272 + 256 + 0x10c], 3)
        expected = FLEET.expected_actor(records[0]["consumed"]["actors"][15], records[0]["consumed"])
        self.assertEqual(expected["damage_level_3"], 2)
        self.assertEqual(expected["broken_feature_2"], 1)

    def test_detached_comparison_requires_all_three_phases_and_inactive_slots(self):
        header, records = fixture(1)
        # A warm part moves during 415e10, after the two actor physics phases.
        for name, position in (("contacts", 100), ("ground", 100), ("post", 97)):
            shared = records[0][name]["shared"]
            pool = bytearray.fromhex(shared["detached_pool_hex"])
            struct.pack_into("<i", pool, 0xec, position)
            struct.pack_into("<i", pool, 0xf8, -3)
            struct.pack_into("<I", pool, 0x108, 1)
            pool[0x10c] = 2
            struct.pack_into("<H", pool, 0x10e, 123)
            # Source rotation uses padded rows; basis uses tightly packed rows.
            struct.pack_into("<i", pool, 0x88 + 16 + 8, -65536)
            struct.pack_into("<i", pool, 0xc8 + 12 + 8, 65536)
            # Inactive numeric state must also be compared, not zeroed away.
            struct.pack_into("<i", pool, 9 * 0x11c + 0x110, -45)
            shared.update(detached_pool_hex=pool.hex(), detached_count=1,
                          detached_free_hex=struct.pack("<10I", 0, *([1] * 9)).hex())
        write_trace(self.path, header, records)
        _, records = FLEET.read_trace(self.path)
        rows = [{"id": 0, "phase": phase, "slot": slot,
                 **FLEET.expected_detached(records[0][name], slot)}
                for phase, name in (("integration", "contacts"), ("ground", "ground"), ("post", "post"))
                for slot in range(10)]
        native = self.path.with_suffix(".detached.csv")
        def save(values):
            with native.open("w", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(values)
        save(rows)
        report = FLEET.compare_detached(records, native)
        self.assertTrue(report["parity"])
        self.assertEqual(report["phases"], ["integration", "ground", "post"])
        self.assertEqual(rows[0]["source_slot"], 3)
        self.assertEqual(rows[0]["rotation_12"], -65536)
        self.assertEqual(rows[0]["basis_12"], 65536)
        rows[20]["position_x"] += 3
        rows[29]["timer"] = 0
        save(rows)
        report = FLEET.compare_detached(records, native)
        self.assertEqual(report["mismatch_count"], 2)
        self.assertEqual(report["exact_prefix_updates"], 0)
        self.assertEqual(report["first_mismatches"][0]["phase"], "post")
        self.assertEqual(report["first_mismatches"][1]["slot"], 9)
        save(rows[:20])
        with self.assertRaisesRegex(ValueError, "omitted"):
            FLEET.compare_detached(records, native)
        rows[20], rows[21] = rows[21], rows[20]
        save(rows)
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_detached(records, native)

    def test_event_coverage_uses_ground_contacts_damage_and_exact_surface_gates(self):
        header, records = fixture(1)
        record = records[0]
        ground = record["ground"]
        for actor in (0, 1, 2, 3):
            entry = ground["actors"][actor]
            body = bytearray.fromhex(entry["body_hex"])
            dynamics = bytearray.fromhex(entry["dynamics_hex"])
            struct.pack_into("<I", body, 0x4f0, 1 if actor == 3 else 0)
            struct.pack_into("<i", body, 0xe0, 0x8000 if actor == 2 else 0x8001)
            dynamics[0x328:0x330] = bytes((2, 3, 0x82, 0x83, 0, 1, 4, 5))
            if actor < 2:
                body[0x529:0x52b] = bytes((1, 1 - actor))
            entry.update(body_hex=body.hex(), dynamics_hex=dynamics.hex())
        ground["roughness_cursor"] = 8  # Four supported points on each of two full bodies.
        shared = ground["shared"]
        levels = bytearray.fromhex(shared["damage_levels_hex"])
        levels[3 * 16] = 2
        broken = bytearray.fromhex(shared["broken_features_hex"])
        struct.pack_into("<I", broken, 0, 1)
        shared.update(damage_levels_hex=levels.hex(), broken_features_hex=broken.hex(), detached_count=1,
                      detached_free_hex=struct.pack("<10I", 0, *([1] * 9)).hex())
        # The part can disappear and neighbor lists can clear by post; ground
        # remains the authoritative contact/allocation observation.
        record["post"]["actors"][0]["body_hex"] = record["post"]["actors"][1]["body_hex"]
        coverage = FLEET.Coverage(record["pre"])
        coverage.consume(record["consumed"], record["post"], ground)
        result = coverage.summary("fleet-pursuit")
        self.assertEqual(result["unique_contact_pairs"], [(0, 1)])
        self.assertEqual(result["confirmed_pair_occurrences"], 1)
        self.assertEqual(result["damage_feature_level_increases"], 2)
        self.assertEqual(result["broken_feature_transitions"], 1)
        self.assertEqual(result["detached_allocations"], 1)
        self.assertEqual(result["roughness_draws_from_full_supports"], 8)
        self.assertEqual(result["roughness_remainder_mismatches"], 0)
        self.assertTrue(all(result[name + "_coverage"] for name in ("contact", "damage", "roughness")))
        self.assertTrue(result["passed"])
        no_events = FLEET.Coverage(record["pre"])
        no_events.consume(record["consumed"], record["post"], record["consumed"])
        self.assertFalse(no_events.summary("fleet-pursuit")["passed"])
        self.assertFalse(no_events.summary()["contact_coverage"])

    def test_detached_coverage_requires_spawn_and_actual_motion_then_tracks_lifecycle(self):
        _, records = fixture(3)
        def part(snapshot, active, position, resting=False, timer=0):
            raw = bytearray(10 * 0x11c)
            struct.pack_into("<i", raw, 0xec, position)
            struct.pack_into("<I", raw, 0x108, active)
            struct.pack_into("<i", raw, 0x110, timer)
            struct.pack_into("<I", raw, 0x118, resting)
            snapshot["shared"].update(detached_pool_hex=raw.hex(), detached_count=int(active),
                                        detached_free_hex=struct.pack("<10I", int(not active), *([1] * 9)).hex())
        part(records[0]["ground"], True, 100)
        part(records[0]["post"], True, 120)
        part(records[1]["consumed"], True, 120)
        part(records[1]["ground"], True, 120)
        part(records[1]["post"], True, 125, True, 700)
        part(records[2]["consumed"], True, 125, True, 0)
        part(records[2]["ground"], True, 125, True, 0)
        part(records[2]["post"], False, 125, True, 0)
        coverage = FLEET.Coverage(records[0]["pre"])
        empty = FLEET.Coverage(records[0]["pre"])
        empty.consume(records[0]["consumed"], records[0]["post"], records[0]["consumed"])
        self.assertFalse(empty.summary("fleet-detachment")["passed"])
        for record in records:
            coverage.consume(record["consumed"], record["post"], record["ground"])
        result = coverage.summary("fleet-detachment")
        self.assertEqual(result["detached_allocations"], 1)
        self.assertEqual(result["detached_position_changes"], 2)
        self.assertEqual(result["detached_landings"], 1)
        self.assertEqual(result["detached_retirements"], 1)
        self.assertEqual(result["detached_timer_expirations"], 1)
        self.assertEqual(result["detached_flying_updates"], 2)
        self.assertEqual(result["detached_resting_updates"], 2)
        self.assertTrue(result["detached_coverage"])
        self.assertTrue(result["passed"])

    def test_live_prop_import_binary_and_manifest_integrity(self):
        header, records = fixture(1)
        header["extra_provenance_blocks"] = FLEET.PROVENANCE_BLOCKS
        for name in FLEET.BOUNDARIES:
            shared = records[0][name]["shared"]
            shared["provenance"] = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.PROVENANCE_BLOCKS.items()}
            cone = bytearray(0xcc)
            cone[0xc0] = 2  # Already settled, definitely not cold reset4.
            shared["props"]["cones"] = {"count": 1, "stride": 0xcc, "records_hex": cone.hex()}
            model = bytearray(68)
            for index in range(4):
                struct.pack_into("<f", model, index * 20, 1.0)
            model[64] = 0xf
            sign = bytearray(0x40)
            struct.pack_into("<I", sign, 0x3c, 3)
            shared["props"]["signs"] = {"count": 1, "stride": 0x40, "records_hex": sign.hex(), "model_records_hex": model.hex()}
        write_trace(self.path, header, records)
        header, parsed = FLEET.read_trace(self.path)
        FLEET.validate_replay_scope(parsed, "independent-physics")
        output = self.path.with_suffix(".bin")
        FLEET.write_binary(output, header, parsed)
        raw = output.read_bytes()
        world = 64 + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES
        self.assertEqual(struct.unpack_from("<3I", raw, world), (1, 0, 1))
        self.assertEqual(raw[world + 12 + 0xc0], 2)
        self.assertEqual(raw[world + 12 + 0xcc + 0x40 + 64], 0xf)
        self.assertEqual(len(raw), 64 + 42571 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 12 + 0xcc + 0x40 + 68)
        # The reader must not accept a world missing one declared observation.
        for mutate in (lambda: records[0]["ground"]["shared"].pop("provenance"),
                       lambda: records[0]["ground"]["shared"]["props"]["signs"].pop("model_records_hex")):
            # Each mutation is applied independently to the complete fixture.
            saved = json.loads(json.dumps(records))
            mutate()
            write_trace(self.path, header, records)
            with self.assertRaises((ValueError, KeyError)):
                FLEET.read_trace(self.path)
            records[:] = saved

    def test_unsupported_low_gravity_cannot_enter_default_native_physics(self):
        header, records = fixture(1)
        config = bytearray.fromhex(header["configuration_hex"])
        struct.pack_into("<I", config, 0x5c, 1)
        header["configuration_hex"] = config.hex()
        write_trace(self.path, header, records)
        # Observation remains valid; only the current replay contract lacks
        # this real source input and must refuse to construct the wrong world.
        header, records = FLEET.read_trace(self.path)
        with self.assertRaisesRegex(ValueError, "low-gravity"):
            FLEET.write_binary(self.path.with_suffix(".bin"), header, records)
        self.assertFalse(self.path.with_suffix(".bin").exists())

    def test_visibility_bitset_and_source_global_overrides(self):
        shared = state("421aa0-entry")["shared"]
        shared["sector_visibility_disabled"] = 1
        bits = bytearray(0x100)
        bits[13 // 8] = 1 << (13 % 8)
        shared["sector_visibility_hex"] = bits.hex()
        self.assertTrue(FLEET.sector_visible(13, shared))
        self.assertFalse(FLEET.sector_visible(12, shared))
        self.assertFalse(FLEET.sector_visible(0xffff, shared))
        shared["sector_visibility_override"] = 1
        self.assertTrue(FLEET.sector_visible(12, shared))

    def test_ai_tire_base_comes_from_captured_provenance_without_output_inference(self):
        header, records = fixture(1)
        globals_raw = bytearray(0x24)
        struct.pack_into("<i", globals_raw, 0x20, 0x228f)
        records[0]["consumed"]["shared"]["provenance"]["ai_globals_hex"] = globals_raw.hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records)
        offset = 64 + 44 + 0x800 + 16 + 15 + 3672
        self.assertEqual(struct.unpack_from("<i", binary.read_bytes(), offset)[0], 0x228f)
        records[0]["consumed"]["shared"]["provenance"].pop("ai_globals")
        missing = self.path.with_suffix(".missing.bin")
        with self.assertRaisesRegex(ValueError, "captured AI globals"):
            FLEET.write_binary(missing, header, records)
        self.assertFalse(missing.exists())

    def test_batched_capture_preserves_sign_transform_flags_and_distinct_actor_roots(self):
        initial = state("421aa0-entry")
        initial["race_clock"] = 0
        image = {}
        def store(address, raw):
            image.update((address + index, value) for index, value in enumerate(raw))
        for name, (address, size) in FLEET.BLOCKS.items():
            store(address, bytes.fromhex(initial["shared"][name + "_hex"]))
        for name, address in FLEET.WORDS.items():
            store(address, struct.pack("<I", initial["shared"][name]))
        for name, (address, size) in FLEET.PROVENANCE_BLOCKS.items():
            store(address, bytes.fromhex(initial["shared"]["provenance"][name + "_hex"]))
        for address, size in FLEET.LIFECYCLE_BLOCKS.values():
            store(address, bytes(size))
        sign_root = 0x300000
        model = bytes(range(64)) + bytes(0xa4) + b"\x0f\x02\x03\x04"
        self.assertEqual(len(model), 0xe8)
        store(sign_root + 0x5c, model)
        sign = struct.pack("<2I", sign_root, sign_root + 0x5c) + bytes(0x38)
        for name, (count_address, address, stride) in FLEET.PROPS.items():
            store(count_address, struct.pack("<I", name == "signs"))
            if name == "signs":
                store(address, sign)
        for name in ("body", "dynamics", "gameplay"):
            store(FLEET.LAYOUT[name + "_base"], b"".join(bytes.fromhex(actor[name + "_hex"]) for actor in initial["actors"]))
        for actor in initial["actors"]:
            store(actor["root"]["pointer"] + 0x120, bytes.fromhex(actor["root"]["registration_hex"]))
            store(actor["effect_root"]["pointer"] + 0x140, bytes.fromhex(actor["effect_root"]["flags_hex"]))
        class Reader:
            ranges = []
            def preload(self, ranges):
                self.ranges.extend(ranges)
            def memory(self, address, size):
                return bytes(image[address + index] for index in range(size))
            def word(self, address):
                return struct.unpack("<I", self.memory(address, 4))[0]
        reader = Reader()
        captured = FLEET.capture(reader, "421aa0-entry", initial)
        FLEET.validate_snapshot(captured, "421aa0-entry")
        self.assertEqual(bytes.fromhex(captured["shared"]["props"]["signs"]["model_records_hex"]), bytes(range(64)) + model[-4:])
        self.assertIn((sign_root + 0x5c, 0xe8), reader.ranges)
        self.assertEqual(len(reader.ranges), 33)
        for actor in captured["actors"]:
            self.assertEqual(actor["root"]["sector"], actor["actor"])
            self.assertEqual(actor["effect_root"]["flags_hex"], "01000000")
