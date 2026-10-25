"""Fleet reference checks for ai."""

from .support import (
    CLI,
    FLEET,
    FleetTestCase,
    Path,
    ai_fixture,
    csv,
    fixture,
    io,
    joined_fixture,
    json,
    nearest_fixture,
    patch,
    state,
    struct,
    subprocess,
    sys,
    write_trace,
    zlib,
)


class JoinedAiTests(FleetTestCase):
    def test_joined_binary_contains_only_one_pre_seed_world_and_no_counter_observations(self):
        header, records = joined_fixture()
        for index, raw in enumerate((records[0]["ai_pre"], records[0]["consumed"])):
            raw["shared"]["pair_matrix_hex"] = bytes([17 + index] * 0x800).hex()
            globals_raw = bytearray.fromhex(raw["shared"]["provenance"]["ai_globals_hex"])
            struct.pack_into("<i", globals_raw, 0x20, 100 + index)
            raw["shared"]["provenance"]["ai_globals_hex"] = globals_raw.hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_joined_ai=True)
        original = binary.read_bytes()
        self.assertEqual(struct.unpack_from("<8I", original, 32), (1, 0, 0, 0, 0, 1, 0, 1))
        initial = 12 + 0x540 + 4100 + 2 * 396 + 4 + 64 + 4 + 64 * 4
        world = 64 + initial
        self.assertEqual(struct.unpack_from("<I", original, world + 16)[0], 0)
        self.assertEqual(original[world + 44:world + 44 + 0x800], bytes([17] * 0x800))
        self.assertEqual(struct.unpack_from("<i", original, world + 44 + 0x800 + 16 + 15 + 3672)[0], 100)
        ai = world + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES
        self.assertEqual(struct.unpack_from("<2I", original, ai), (0, 0))
        traffic = ai + 5576
        self.assertEqual(struct.unpack_from("<I", original, traffic)[0], records[0]["ai_pre"]["rng"])
        self.assertEqual(struct.unpack_from("<I", original, traffic + 16)[0], 0)
        self.assertEqual(original[ai - FLEET.SELECTION_BYTES - FLEET.LAUNCH_BYTES:ai - FLEET.SELECTION_BYTES], bytes(FLEET.LAUNCH_BYTES))
        #Every later measured world/seed may change without altering ANY joined
        #input bytes. The observed release scalar is checked, never serialized.
        for record in records:
            for phase in ("traffic_pre", "ai_post", "consumed", "contacts", "ground", "post"):
                state = record[phase]
                state["rng"] ^= 0x1234
                for actor in state["actors"]:
                    for name in ("body", "dynamics", "gameplay"):
                        actor[name + "_hex"] = bytes(FLEET.LAYOUT[name + "_stride"]).hex()
                        actor[name] = bytes(FLEET.LAYOUT[name + "_stride"])
        FLEET.write_binary(binary, header, records, compare_joined_ai=True)
        self.assertEqual(binary.read_bytes(), original)
        records[1]["ai_pre"]["shared"]["ai_state"]["release_counter"] = 359
        with self.assertRaisesRegex(ValueError, "source-derived sequence"):
            FLEET.write_binary(binary, header, records, compare_joined_ai=True)

    def test_joined_comparison_requires_complete_outputs_and_catches_rng_counter_errors(self):
        header, records = joined_fixture()
        game = bytearray.fromhex(records[0]["ai_post"]["actors"][0]["gameplay_hex"])
        struct.pack_into("<i", game, 0xec, 999)
        struct.pack_into("<i", game, 0xf4, 37)
        records[0]["ai_post"]["gameplay_hex"] = records[0]["ai_post"]["actors"][0]["gameplay_hex"] = game.hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        self.assertEqual(FLEET.expected_joined_progress(records[0]["ai_post"], 0)["distance_delta"], 37)
        native = self.joined_csv(records)
        report = FLEET.compare_joined_ai(header, records, native)
        self.assertTrue(report["parity"])
        self.assertEqual(report["intermediate_rng_imports"], 0)
        self.assertEqual(report["release_decrement_updates"], [2])
        self.assertEqual(report["release_events"][0]["update"], 1)
        self.assertEqual(report["compared_values"], 3 * (16 * 26 + 2 + 15 * 16 + 2 + 16 * 25 + 16 * 13 + 1))
        world = Path(str(native) + ".joined-world.csv")
        world.write_text(world.read_text().replace("1,360", "1,359"))
        report = FLEET.compare_joined_ai(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], "joined-world.release_counter")
        self.joined_csv(records)
        progress = Path(str(native) + ".joined-progress.csv")
        with progress.open(newline="") as source:
            reader = csv.DictReader(source); progress_fields = reader.fieldnames; progress_rows = list(reader)
        progress_rows[0]["distance_delta"] = "999"
        with progress.open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=progress_fields); writer.writeheader(); writer.writerows(progress_rows)
        report = FLEET.compare_joined_ai(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], "joined-progress.distance_delta")
        self.joined_csv(records)
        route = Path(str(native) + ".ai-route.csv")
        with route.open(newline="") as source:
            reader = csv.DictReader(source); fields = reader.fieldnames; rows = list(reader)
        rows[0]["rng_final"] = str(int(rows[0]["rng_final"]) + 1)
        with route.open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=fields); writer.writeheader(); writer.writerows(rows)
        report = FLEET.compare_joined_ai(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], "ai-route.rng_final")
        with route.open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=fields); writer.writeheader(); writer.writerows(rows[:-1])
        with self.assertRaisesRegex(ValueError, "Missing or extra complete"):
            FLEET.compare_joined_ai(header, records, native)

    def test_joined_cli_keeps_physics_untested_and_requires_positive_release_coverage(self):
        header, records = joined_fixture()
        write_trace(self.path, header, records)
        output = self.path.parent / "joined-only"
        for count in (3, 1):
            native_source = self.joined_csv(records[:count])
            argv = ["replay-fleet-reference.py", str(self.path), "--output", str(output), "--compare-joined-ai", "--updates", str(count)]
            def execute(command, **_kwargs):
                self.assertIn("--compare-joined-ai", command)
                for suffix in (".ai-route.csv", ".joined-progress.csv", ".joined-world.csv", ".traffic.csv", ".traffic-world.csv", ".ai-controls.csv", ".ai-control-world.csv"):
                    Path(str(command[4]) + suffix).write_bytes(Path(str(native_source) + suffix).read_bytes())
                return subprocess.CompletedProcess([], 0, "", "")
            with patch.object(sys, "argv", argv), patch.object(CLI.subprocess, "run", side_effect=execute), patch.object(CLI.FLEET, "compare", side_effect=AssertionError("physical execution")), patch.object(CLI.FLEET, "validate_replay_scope", side_effect=AssertionError("physical scope")), patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(CLI.main(), 0 if count == 3 else 1)
            report = json.loads((output / "fleet-comparison-continuous-physics-inputs.json").read_text())
            self.assertIsNone(report["physical_parity"])
            self.assertIsNone(report["joined_ai"]["continuous_process_rng_parity"])
            self.assertEqual(report["joined_ai"]["coverage_sufficient"], count == 3)
            self.assertEqual(report["joined_ai"]["intermediate_world_imports"], 0)


class NearestRecoveryTests(FleetTestCase):
    def test_nearest_exact_block_distinguishes_gate_progression_and_rejects_changed_outputs(self):
        header, records = nearest_fixture()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        native = self.nearest_csv(header, records)
        report = FLEET.compare_nearest_recovery(header, records, native)
        self.assertTrue(report["parity"])
        self.assertEqual(report["cursor_jumps"][0]["before"], [1, 0])
        self.assertEqual(report["cursor_jumps"][0]["after"], [40, 39])
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_nearest_recovery=True)
        original = binary.read_bytes()
        initial = 12 + 0x540 + 4100 + 2 * 396
        self.assertEqual(original[64 + initial:64 + initial + 256], bytes.fromhex(header["nearest_recovery_initial_state"]["nearest_map_hex"]))
        offset = 64 + initial + 256 + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 5576
        self.assertEqual(struct.unpack_from("<9I", original, offset), (1, 0, 0x12345678, 0, 0, 1, 0, 1, 1))
        changed = bytearray.fromhex(records[0]["nearest_recoveries"][0]["post"]["gameplay_hex"])
        struct.pack_into("<h", changed, 0xa2, 41)
        records[0]["nearest_recoveries"][0]["post"]["gameplay_hex"] = changed.hex()
        FLEET.write_binary(binary, header, records, compare_nearest_recovery=True)
        self.assertEqual(binary.read_bytes(), original)
        report = FLEET.compare_nearest_recovery(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], "route_next")
        # An ordinary0→1 gate change with an unchanged1→1 internal block is
        # exact numerical retention, but cannot satisfy positive jump coverage.
        header, records = nearest_fixture(False)
        report = FLEET.compare_nearest_recovery(header, records, self.nearest_csv(header, records))
        self.assertTrue(report["numerical_parity"])
        self.assertFalse(report["parity"])
        self.assertIsNone(report["original_positive_jump_parity"])

    def test_nearest_provenance_requires_all_checks_and_rejects_extra_raw_writes(self):
        header, records = nearest_fixture()
        FLEET.validate_nearest_recoveries(header, records[0])
        event = records[0]["nearest_recoveries"].pop()
        with self.assertRaisesRegex(ValueError, "Missing, extra"):
            FLEET.validate_nearest_recoveries(header, records[0])
        records[0]["nearest_recoveries"].append(event)
        raw = bytearray.fromhex(event["post"]["gameplay_hex"])
        raw[0xa6] = 1
        event["post"]["gameplay_hex"] = raw.hex()
        with self.assertRaisesRegex(ValueError, "retained raw"):
            FLEET.validate_nearest_recoveries(header, records[0])
        raw[0xa6] = 0
        event["post"]["gameplay_hex"] = raw.hex()
        native = self.nearest_csv(header, records)
        Path(str(native) + ".nearest-recovery.csv").write_text("id,event,actor,route_next,route_previous,rng_final\n")
        with self.assertRaisesRegex(ValueError, "Missing or extra actual"):
            FLEET.compare_nearest_recovery(header, records, native)
        native = self.nearest_csv(header, records)
        table = Path(str(native) + ".nearest-map.csv")
        table.write_text(table.read_text().replace("1,40", "1,41"))
        report = FLEET.compare_nearest_recovery(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], "nearest_map")

    def test_nearest_only_cli_has_no_physical_claim_and_requires_positive_event(self):
        output = self.path.parent / "nearest-only"
        argv = ["replay-fleet-reference.py", str(self.path), "--output", str(output), "--compare-nearest-recovery"]
        for jump in (True, False):
            header, records = nearest_fixture(jump)
            write_trace(self.path, header, records)
            source = self.nearest_csv(header, records)
            def execute(command, **_kwargs):
                self.assertIn("--compare-nearest-recovery", command)
                for suffix in (".nearest-map.csv", ".nearest-recovery.csv"):
                    Path(str(command[4]) + suffix).write_bytes(Path(str(source) + suffix).read_bytes())
                return subprocess.CompletedProcess([], 0, "", "")
            with patch.object(sys, "argv", argv), patch.object(CLI.subprocess, "run", side_effect=execute), patch.object(CLI.FLEET, "compare", side_effect=AssertionError("physical execution")), patch.object(CLI.FLEET, "validate_replay_scope", side_effect=AssertionError("physical scope")), patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(CLI.main(), 0 if jump else 1)
            report = json.loads((output / "fleet-comparison-continuous-physics-inputs.json").read_text())
            self.assertIsNone(report["physical_parity"])
            self.assertEqual(report["nearest_recovery"]["coverage_sufficient"], jump)


class TrafficControlTests(FleetTestCase):
    def test_traffic_binary_imports_real_pre_only_and_corrupt_outputs_fail(self):
        header, records = self.traffic_fixture()
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_traffic_controls=True)
        original = binary.read_bytes()
        self.assertEqual(struct.unpack_from("<6I", original, 32), (1, 0, 0, 0, 0, 1))
        initial = 12 + 0x540 + 4100 + 2 * 396
        self.assertEqual(struct.unpack_from("<I", original, 64 + initial)[0], 64)
        self.assertEqual(original[64 + initial + 4:64 + initial + 68], bytes(range(64)))
        offset = 64 + initial + 68 + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 5576
        pre = records[0]["traffic_pre"]
        self.assertEqual(struct.unpack_from("<IIiII", original, offset), (pre["rng"], 0, 9, 0, 360))
        self.assertEqual(original[offset + 96 + 1212:offset + 96 + 1212 + 0x53c], bytes.fromhex(pre["actors"][0]["body_hex"]))
        self.assertEqual(FLEET.TRAFFIC_RECORD_BYTES, 96 + 1212 + 16 * (0x53c + 0x338 + 0x124))
        self.assertEqual(original[offset + 96:offset + 96 + 1212], pre["shared"]["traffic_control"]["braking_memory"])
        native = self.traffic_csv(records)
        report = FLEET.compare_traffic_controls(header, records, native)
        self.assertTrue(report["parity"])
        self.assertEqual(report["coverage"]["eligible_control_actor_updates"], 30)
        self.assertTrue(report["original_traffic_event_parity"])
        #Expected RETURN andCONSUMED values never become typed PRE inputs.
        game = bytearray.fromhex(records[1]["ai_post"]["actors"][1]["gameplay_hex"])
        game[0x81] = 5
        records[1]["ai_post"]["actors"][1]["gameplay_hex"] = game.hex()
        FLEET.write_binary(binary, header, records, compare_traffic_controls=True)
        self.assertEqual(binary.read_bytes(), original)
        report = FLEET.compare_traffic_controls(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], "traffic.mode")
        dynamics = bytearray.fromhex(records[1]["consumed"]["actors"][1]["dynamics_hex"])
        struct.pack_into("<i", dynamics, 0x138, 12346)
        records[1]["consumed"]["actors"][1]["dynamics_hex"] = dynamics.hex()
        report = FLEET.compare_traffic_controls(header, records, native)
        self.assertIn("ai-controls.raw_left", [item["field"] for item in report["first_mismatches"]])
        self.assertTrue(report["rng_parity"])
        records[1]["consumed"]["rng"] += 1
        report = FLEET.compare_traffic_controls(header, records, native)
        self.assertIn("ai-control-world.rng_final", [item["field"] for item in report["first_mismatches"]])
        self.assertFalse(report["rng_parity"])

    def test_traffic_controls_reject_missing_rows_and_use_pre_eligibility(self):
        header, records = self.traffic_fixture()
        native = self.traffic_csv(records)
        path = Path(str(native) + ".ai-controls.csv")
        lines = path.read_text().splitlines(keepends=True)
        path.write_text("".join(lines[:-1]))
        with self.assertRaisesRegex(ValueError, "Missing or extra"):
            FLEET.compare_traffic_controls(header, records, native)
        self.traffic_csv(records)
        path = Path(str(native) + ".traffic.csv")
        lines = path.read_text().splitlines(keepends=True)
        lines[1], lines[2] = lines[2], lines[1]
        path.write_text("".join(lines))
        with self.assertRaisesRegex(ValueError, "Out-of-order"):
            FLEET.compare_traffic_controls(header, records, native)
        self.assertNotIn(0, FLEET.traffic_control_actors(records[0]))
        world = bytearray.fromhex(records[0]["traffic_pre"]["shared"]["provenance"]["world_status_hex"])
        struct.pack_into("<H", world, 0x12, 1)
        records[0]["traffic_pre"]["shared"]["provenance"]["world_status_hex"] = world.hex()
        self.assertIn(0, FLEET.traffic_control_actors(records[0]))

    def test_traffic_only_cli_skips_physics_and_requires_observed_profile(self):
        header, records = self.traffic_fixture()
        #The fixture already wrote the canonical JSON before decoding.
        output = self.path.parent / "traffic-only"
        traffic = {"parity": True, "exact_prefix_updates": 2, "mismatch_count": 0}
        argv = ["replay-fleet-reference.py", str(self.path), "--output", str(output), "--compare-traffic-controls"]
        with patch.object(sys, "argv", argv), patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as native, patch.object(CLI.FLEET, "compare_traffic_controls", return_value=traffic), patch.object(CLI.FLEET, "compare", side_effect=AssertionError("physical execution")), patch.object(CLI.FLEET, "validate_replay_scope", side_effect=AssertionError("physical scope")), patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertIn("--compare-traffic-controls", native.call_args.args[0])
        report = json.loads((output / "fleet-comparison-continuous-physics-inputs.json").read_text())
        self.assertIsNone(report["physical_parity"])
        header.pop("traffic_control_manifest")
        with self.assertRaisesRegex(ValueError, "recorded404c1a"):
            FLEET.write_binary(self.path.with_suffix(".missing.bin"), header, records, compare_traffic_controls=True)

    def test_traffic_profile_requires_real_internal_boundary_and_stable_live_bypass_crc(self):
        header, records = ai_fixture(1)
        content = {"point_count": 64, "bypass_pointer": 0x700002, "brake_bypass_hex": bytes(range(64)).hex(),
                   "brake_bypass_crc32": f"{zlib.crc32(bytes(range(64))):08x}"}
        header.update(traffic_control_manifest=FLEET.TRAFFIC_CONTROL_MANIFEST, traffic_initial_state=content, traffic_boundaries=FLEET.TRAFFIC_BOUNDARIES)
        record = records[0]
        record["traffic_pre"] = json.loads(json.dumps(record["ai_pre"]))
        record["traffic_pre"]["boundary"] = "404c1a-entry"
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
            raw = {key + "_hex": bytes(size).hex() for key, (_, size) in FLEET.TRAFFIC_CONTROL_BLOCKS.items()}
            raw["brake_bypass_pointer_hex"] = struct.pack("<I", content["bypass_pointer"]).hex()
            raw.update({key: value for key, value in content.items() if key != "brake_bypass_hex"})
            raw["force_acceleration_hex"] = bytes(4).hex()
            record[name]["shared"]["traffic_control"] = raw
        write_trace(self.path, header, records)
        _, parsed = FLEET.read_trace(self.path)
        self.assertEqual(parsed[0]["traffic_pre"]["boundary"], "404c1a-entry")
        record["consumed"]["shared"]["traffic_control"]["brake_bypass_crc32"] = "00000000"
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "brake-bypass setup change"):
            FLEET.read_trace(self.path)
        record["consumed"]["shared"]["traffic_control"]["brake_bypass_crc32"] = content["brake_bypass_crc32"]
        record.pop("traffic_pre")
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "Missing required traffic"):
            FLEET.read_trace(self.path)
        header["traffic_initial_state"]["brake_bypass_crc32"] = "00000000"
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "brake-bypass CRC32"):
            FLEET.read_trace(self.path)


class AiRouteTests(FleetTestCase):
    def test_ai_capture_reads_live_initialized_route_and_validates_optional_boundaries(self):
        header, records = ai_fixture(1)
        content = header["ai_initial_state"]
        memory = {FLEET.AI_STATE_BLOCKS["release_counter"]: struct.pack("<I", 360),
                  FLEET.AI_STATE_BLOCKS["resolved_ai"]: bytes.fromhex(content["resolved_ai_hex"]),
                  FLEET.AI_STATE_BLOCKS["route_descriptor"]: bytes.fromhex(content["route_descriptor_hex"]),
                  FLEET.AI_STATE_BLOCKS["startline_start"]: struct.pack("<I", 25),
                  FLEET.AI_STATE_BLOCKS["startline_end"]: struct.pack("<I", 56),
                  (0x4ffffc, 4100): bytes.fromhex(content["points_hex"]),
                  (0x4bd790, 396): bytes(396), (0x4bd5d8, 396): bytes(396)}
        class Reader:
            def preload(self, requests):
                self.requests = requests
            def memory(self, address, size):
                return memory[(address, size)]
        reader = Reader()
        observed, initial = FLEET.capture_ai_state(reader)
        self.assertEqual(initial, content)
        self.assertEqual(observed, records[0]["ai_pre"]["shared"]["ai_state"])
        self.assertEqual(reader.requests, [(0x4ffffc, 4100), (0x4bd790, 396), (0x4bd5d8, 396)])
        #Actual archive pointers need not be word aligned. Only the cache's
        #underlying read expands; hashes preserve the exact initialized bytes.
        memory[FLEET.AI_STATE_BLOCKS["route_descriptor"]] = struct.pack("<4I", 0x500002, 64, 1, 0x501006)
        memory[(0x4ffffe, 4100)] = bytes.fromhex(content["points_hex"])
        _, packed = FLEET.capture_ai_state(reader)
        self.assertEqual(reader.requests[0], (0x4ffffc, 4104))
        self.assertEqual(packed["points_crc32"], content["points_crc32"])
        self.assertEqual(packed["points_hex"], content["points_hex"])
        write_trace(self.path, header, records)
        parsed_header, parsed = FLEET.read_trace(self.path)
        self.assertEqual(FLEET.validate_ai_route_scope(parsed_header, parsed)["points"], bytes.fromhex(content["points_hex"]))
        records[0].pop("ai_post")
        write_trace(self.path, header, records)
        with self.assertRaises((ValueError, KeyError)):
            FLEET.read_trace(self.path)

    def test_ai_setup_strict_counts_pointers_crc_and_live_route_binary(self):
        # Original packed POINTS pointer from the first --ai-state acquisition.
        self.assertEqual(FLEET.route_descriptor(bytes.fromhex("32590a02fe000000e5000000b6980a02")),
                         (0x020a5932, 254, 229, 0x020a98b6))
        header, records = ai_fixture(1)
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        output = self.path.with_suffix(".bin")
        FLEET.write_binary(output, header, records, compare_ai_route=True)
        raw = output.read_bytes()
        self.assertEqual(struct.unpack_from("<11I", raw, 32), (1, 0, 0, 0, 0, 0, 0, 0, 64, 25, 56))
        initial = header["ai_initial_state"]
        self.assertEqual(raw[76 + 0x540:76 + 0x540 + 4100], bytes.fromhex(initial["points_hex"]))
        initial_bytes = 12 + 0x540 + 4100 + 2 * 396
        ai_offset = 64 + initial_bytes + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES
        self.assertEqual(struct.unpack_from("<2I", raw, ai_offset), (360, 0x12345678))
        self.assertEqual(len(raw), 64 + initial_bytes + 42571 + 12 + 3676 + FLEET.LAUNCH_BYTES + FLEET.SELECTION_BYTES + 5576)
        for mutation, message in (
            ({"points_hex": (struct.pack("<I", 63) + bytes(4096)).hex()}, "prefix"),
            ({"route_descriptor_hex": struct.pack("<4I", 3, 64, 1, 4103).hex()}, "pointer"),
            ({"route_descriptor_hex": struct.pack("<4I", 0x500000, 257, 1, 0x504044).hex()}, "count"),
            ({"resolved_ai_crc32": "00000000"}, "CRC32"),
            ({"start_bound": 24}, "bounds"),
            ({"resolved_ai_hex": (bytes(0x53c) + b"\x65" + bytes(3)).hex()}, "distribution count")):
            with self.subTest(mutation=mutation.keys()), self.assertRaisesRegex(ValueError, message):
                FLEET.ai_content({**initial, **mutation})
        old_header, old_records = fixture(1)
        write_trace(self.path, old_header, old_records)
        old_header, old_records = FLEET.read_trace(self.path)
        output.unlink()
        with self.assertRaisesRegex(ValueError, "--ai-state"):
            FLEET.write_binary(output, old_header, old_records, compare_ai_route=True)
        self.assertFalse(output.exists())

    def test_ai_later_outputs_rng_and_chronology_are_compared_not_imported(self):
        header, records = ai_fixture(2)
        records[1]["ai_post"]["rng"] += 1
        game = bytearray.fromhex(records[1]["ai_post"]["actors"][7]["gameplay_hex"])
        game[0x9e] = 2
        records[1]["ai_post"]["actors"][7]["gameplay_hex"] = game.hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        rows = self.ai_rows(records)
        csv_path = self.ai_csv(rows)
        report = FLEET.compare_ai_route(header, records, csv_path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["supported_cache_refreshes"], 1)
        self.assertEqual(report["supported_rng_advance_updates"], 1)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_ai_route=True)
        before = binary.read_bytes()
        game = bytearray.fromhex(records[1]["ai_post"]["actors"][7]["gameplay_hex"])
        struct.pack_into("<i", game, 0x108, 123)
        records[1]["ai_post"]["actors"][7]["gameplay_hex"] = game.hex()
        records[1]["ai_post"]["rng"] += 1
        FLEET.write_binary(binary, header, records, compare_ai_route=True)
        self.assertEqual(binary.read_bytes(), before)
        report = FLEET.compare_ai_route(header, records, csv_path)
        self.assertFalse(report["parity"])
        self.assertEqual(report["exact_prefix_updates"], 1)
        self.assertEqual(report["mismatch_count"], 17)
        rows[17], rows[18] = rows[18], rows[17]
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_ai_route(header, records, self.ai_csv(rows))

    def test_ai_partial_coverage_never_claims_complete_interval_or_all_skipped_parity(self):
        header, records = ai_fixture(3)
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        unsupported = [{"id": 0, "reason": "checkpoint timing outside independent route slice"}]
        rows = self.ai_rows(records[1:])
        result = FLEET.compare_ai_route(header, records, self.ai_csv(rows, unsupported))
        self.assertTrue(result["supported_parity"])
        self.assertFalse(result["parity"])
        self.assertFalse(result["complete_source_interval"])
        self.assertEqual(result["supported_exact_updates"], 2)
        self.assertEqual(result["exact_prefix_updates"], 0)
        result = FLEET.compare_ai_route(header, records, self.ai_csv([], [{"id": i, "reason": "unsupported"} for i in range(3)]))
        self.assertIsNone(result["supported_parity"])
        self.assertFalse(result["parity"])
        self.assertEqual(result["compared_values"], 0)
        with self.assertRaisesRegex(ValueError, "omitted"):
            FLEET.compare_ai_route(header, records, self.ai_csv(rows[:-1], unsupported))
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_ai_route(header, records, self.ai_csv(rows, unsupported * 2))

    def test_ai_stability_and_supported_scope_are_explicit_guards(self):
        header, records = ai_fixture(1)
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        path = self.ai_csv(self.ai_rows(records))
        raw = records[0]["post"]["shared"]["ai_state"]
        raw["points_crc32"] = "00000000"
        with self.assertRaisesRegex(ValueError, "setup change"):
            FLEET.compare_ai_route(header, records, path)
        raw["points_crc32"] = header["ai_initial_state"]["points_crc32"]
        game = bytearray.fromhex(records[0]["ai_pre"]["actors"][2]["gameplay_hex"])
        struct.pack_into("<I", game, 0x30, 1)
        records[0]["ai_pre"]["actors"][2]["gameplay_hex"] = game.hex()
        with self.assertRaisesRegex(ValueError, "accepted an unsupported"):
            FLEET.compare_ai_route(header, records, path)
        # Unsupported eligibility is decided from native PRE guards; Python
        # never silently removes records merely because expected outputs changed.
        result = FLEET.compare_ai_route(header, records, self.ai_csv([], [{"id": 0, "reason": "PRE pit state active"}]))
        self.assertFalse(result["parity"])

    def test_ai_progression_outputs_are_compared_without_post_import_and_cadence_uses_pre_counter(self):
        header, records = ai_fixture(1)
        resolved = bytearray.fromhex(header["ai_initial_state"]["resolved_ai_hex"])
        resolved[0x13] = 8
        header["ai_initial_state"]["resolved_ai_hex"] = resolved.hex()
        checksum = f"{zlib.crc32(resolved):08x}"
        header["ai_initial_state"]["resolved_ai_crc32"] = checksum
        for name in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES):
            records[0][name]["shared"]["ai_state"]["resolved_ai_crc32"] = checksum
        pre = bytearray.fromhex(records[0]["ai_pre"]["actors"][14]["gameplay_hex"])
        struct.pack_into("<I", pre, 4, 1)
        struct.pack_into("<I", pre, 8, 1)
        pre[0x86] = 8
        struct.pack_into("<2h", pre, 0xa2, 63, 62)
        #PRE+3c=0 is the previous tick's output; signed counter/period requests
        #the current human recovery independently of that stale flag.
        post = bytearray(pre)
        struct.pack_into("<I", post, 0x3c, 1)
        post[0x86] = 0
        post[0x9a], post[0x9b] = 2, 2
        struct.pack_into("<2h", post, 0xa2, 0, 63)
        records[0]["ai_pre"]["actors"][14]["gameplay_hex"] = pre.hex()
        records[0]["ai_post"]["actors"][14]["gameplay_hex"] = post.hex()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        rows = self.ai_rows(records)
        native = self.ai_csv(rows)
        report = FLEET.compare_ai_route(header, records, native)
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_values"], 16 * 25)
        coverage = report["coverage"]
        self.assertEqual(coverage["route_pair_changes"], 1)
        self.assertEqual(coverage["forward_index_changes"], 1)
        self.assertEqual(coverage["index_wraps"], 1)
        self.assertEqual(coverage["refresh_requests"], 1)
        self.assertEqual(coverage["human_nearest_route_checks"], 1)
        self.assertEqual(coverage["human_checks_with_unchanged_pre_to_return_cursor"], 0)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_ai_route=True)
        original = binary.read_bytes()
        changed = bytearray(post)
        struct.pack_into("<h", changed, 0xa2, 1)
        records[0]["ai_post"]["actors"][14]["gameplay_hex"] = changed.hex()
        FLEET.write_binary(binary, header, records, compare_ai_route=True)
        self.assertEqual(binary.read_bytes(), original)
        report = FLEET.compare_ai_route(header, records, native)
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 1)
        self.assertEqual(report["first_mismatches"][0]["field"], "route_next")
        rows[14]["refresh_requested"] = 0
        report = FLEET.compare_ai_route(header, records, self.ai_csv(rows))
        self.assertEqual(report["mismatch_count"], 2)


class BodySelectionTests(FleetTestCase):
    def test_selection_binary_uses_pre_inputs_direction_and_ring_without_consumed_modes(self):
        header, records = fixture(1)
        pre = records[0]["pre"]
        game = bytearray.fromhex(pre["gameplay_hex"])
        struct.pack_into("<i", game, 0x10, -1)
        pre["gameplay_hex"] = pre["actors"][0]["gameplay_hex"] = game.hex()
        ai = bytearray.fromhex(pre["shared"]["provenance"]["ai_globals_hex"])
        struct.pack_into("<i", ai, 0x20, 1234)
        pre["shared"]["provenance"]["ai_globals_hex"] = ai.hex()
        actor = pre["actors"][3]
        body, dynamics, game = (bytearray.fromhex(actor[name + "_hex"]) for name in ("body", "dynamics", "gameplay"))
        struct.pack_into("<i", body, 0xb4, -1234)
        body[0x51f] = 6
        struct.pack_into("<i", dynamics, 0xdc, -999)
        struct.pack_into("<2I", dynamics, 0x2a0, 1, 2)
        game[0x80], game[0x96], game[0x97] = 0xff, 7, 8
        struct.pack_into("<i", game, 0xf0, -500)
        for name, raw in (("body", body), ("dynamics", dynamics), ("gameplay", game)):
            actor[name + "_hex"] = raw.hex()
        before = FLEET.body_selection_input(pre)
        # The completed selection is an expected output, never a typed input.
        output = bytearray.fromhex(records[0]["consumed"]["actors"][3]["body_hex"])
        struct.pack_into("<I", output, 0x4f0, 4)
        records[0]["consumed"]["actors"][3]["body_hex"] = output.hex()
        self.assertEqual(FLEET.body_selection_input(pre), before)
        write_trace(self.path, header, records)
        header, parsed = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, parsed)
        offset = 64 + 44 + 0x800 + 16 + 15 + 3676 + FLEET.LAUNCH_BYTES
        raw = binary.read_bytes()
        self.assertEqual(struct.unpack_from("<IIIiII", raw, offset), (16, 1, 0, 1234, 4, 1))
        self.assertEqual(struct.unpack_from("<IIiIIiiIi", raw, offset + 24 + 3 * 36),
                         (1, 0, -1, 3, 6, -999, -1234, 7 | 8 << 8, -500))
        bad = bytearray.fromhex(pre["actors"][3]["gameplay_hex"])
        bad[0x96] = 16
        pre["actors"][3]["gameplay_hex"] = bad.hex()
        with self.assertRaisesRegex(ValueError, "ahead actor"):
            FLEET.body_selection_input(pre)

    def test_selection_comparison_detects_corrupted_decisions_and_actor_chronology(self):
        header, records = fixture(2)
        write_trace(self.path, header, records)
        _, records = FLEET.read_trace(self.path)
        rows = [{"id": record["id"], "actor": actor["actor"],
                 "mode": struct.unpack_from("<I", actor["body"], 0x4f0)[0]}
                for record in records for actor in record["consumed"]["actors"]]
        native = self.path.with_suffix(".selection.csv")
        def save():
            with native.open("w", newline="") as output:
                writer = csv.DictWriter(output, fieldnames=("id", "actor", "mode"))
                writer.writeheader()
                writer.writerows(rows)
        save()
        independent = FLEET.compare_selection(records, native, "continuous-physics-inputs")
        retained = FLEET.compare_selection(records, native, "continuous-derived-body-selection")
        self.assertTrue(independent["parity"])
        self.assertEqual(independent["initial_pre_physical_imports"], 2)
        self.assertEqual(retained["initial_pre_physical_imports"], 1)
        self.assertEqual(retained["retained_native_selection_updates"], 1)
        self.assertFalse(retained["autonomous_ai_parity"])
        rows[-1]["mode"] = 0
        save()
        mismatch = FLEET.compare_selection(records, native, "continuous-derived-body-selection")
        self.assertEqual(mismatch["mismatch_count"], 1)
        self.assertEqual(mismatch["first_mismatches"][0]["actor"], 15)
        self.assertEqual(mismatch["exact_prefix_updates"], 1)
        rows[0], rows[1] = rows[1], rows[0]
        save()
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_selection(records, native, "continuous-physics-inputs")
        rows.pop()
        save()
        with self.assertRaisesRegex(ValueError, "omitted"):
            FLEET.compare_selection(records, native, "continuous-physics-inputs")
