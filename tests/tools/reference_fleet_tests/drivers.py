"""Fleet reference checks for drivers."""

from .support import (
    TraceTestCase,
    CLI,
    FLEET,
    Path,
    csv,
    fixture,
    io,
    joined_fixture,
    json,
    patch,
    state,
    struct,
    subprocess,
    sys,
    write_trace,
)


class DriverRetentionTests(TraceTestCase):
    trace_filename = 'driver'

    def fixture(self):
        from .random import CombinedRngTests

        header, records = joined_fixture()
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = record[phase]
                snapshot["race_clock"] = 1000 + 40 * record["id"]
                for actor, entry in enumerate(snapshot["actors"]):
                    game = bytearray.fromhex(entry["gameplay_hex"])
                    game[0x80] = 255 if actor else 0
                    entry["gameplay_hex"] = game.hex()
        trace = self.path.with_suffix(".jsonl")
        write_trace(trace, header, records)
        header, records = FLEET.read_trace(trace)
        audio_header, audio_records = CombinedRngTests.fixture(self)
        for key in ("combined_rng_manifest", "combined_initial_rng", "combined_audio_initial", "wheel_queue_initial_state", "whine_initial_state", "whine_tables"):
            header[key] = audio_header[key]
        for record, audio in zip(records, audio_records):
            for key in ("combined_journal", "surface_calls", "skid_dispatch"):
                record[key] = audio[key]
            record["combined_journal"] = [{"kind": "release", "event": event} for event in record["release_events"]] + record["combined_journal"]
        return header, records

    def binaries(self, header, records):
        fleet = self.path.with_suffix(".fleet")
        journal = self.path.with_suffix(".journal")
        FLEET.write_binary(fleet, header, records, compare_joined_ai=True, compare_combined_rng=True, compare_driver_retention=True)
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(journal, header, records, compare_driver_retention=True)
        return fleet.read_bytes(), journal.read_bytes()

    def sidecars(self, header, records):
        streams = {}
        try:
            for suffix, row in FLEET.driver_retention_expected(header, records):
                if suffix not in streams:
                    stream = Path(str(self.path) + suffix).open("w", newline="")
                    writer = csv.DictWriter(stream, fieldnames=list(row)); writer.writeheader()
                    streams[suffix] = stream, writer
                streams[suffix][1].writerow(row)
        finally:
            for stream, _ in streams.values(): stream.close()

    def test_all_later_gameplay_history_light_tire_and_ai_axes_are_expectations_only(self):
        header, records = self.fixture()
        original = self.binaries(header, records)
        for record in records[1:]:
            for event in record["combined_journal"]:
                if event["kind"] == "motor":
                    for physical in event["event"]["pre"]["physical"]:
                        physical["speed_hex"] = struct.pack("<i", 0x12345678).hex()
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                snapshot = record[phase]
                snapshot["rng"] ^= 0x76543210
                traffic = snapshot["shared"]["traffic_control"]
                for field, size in (("status", 24), ("braking_memory", 1212)):
                    traffic[field] = bytes([0xa5]) * size
                    traffic[field + "_hex"] = traffic[field].hex()
                for actor, entry in enumerate(snapshot["actors"]):
                    entry["gameplay"] = bytes([0xa5]) * 0x124
                    entry["gameplay_hex"] = entry["gameplay"].hex()
                    for field, spans in (("body", ((0x500, 4), (0x508, 4))),
                                         ("dynamics", ((0xdc, 32), *((((0x138, 16),) if actor else ()))))):
                        raw = bytearray(entry[field])
                        for offset, size in spans: raw[offset:offset + size] = bytes([0xa5]) * size
                        entry[field] = bytes(raw); entry[field + "_hex"] = raw.hex()
        self.assertEqual(self.binaries(header, records), original)
        # All POST and intermediate snapshots in the initial update are also expectations.
        first = records[0]["post"]["actors"][1]
        first["gameplay_hex"] = bytes([0x77] * 0x124).hex()
        self.assertEqual(self.binaries(header, records), original)

    def test_initial_state_and_explicit_physical_context_still_affect_input(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        entry = records[0]["ai_pre"]["actors"][1]
        raw = bytearray(entry["gameplay"]); raw[0x86] ^= 1
        entry["gameplay"] = bytes(raw); entry["gameplay_hex"] = raw.hex()
        self.assertNotEqual(self.binaries(header, records)[0], before[0])
        for actor, offset in ((1, 0x330), (1, 0x304), (0, 0x140)):
            before = self.binaries(header, records)[0]
            entry = records[1]["ai_pre"]["actors"][actor]
            raw = bytearray(entry["dynamics"]); raw[offset] ^= 1
            entry["dynamics"] = bytes(raw); entry["dynamics_hex"] = raw.hex()
            self.assertNotEqual(self.binaries(header, records)[0], before)

    def test_v3_cold_header_and_release_clock_are_actual_context_not_later_seed(self):
        header, records = self.fixture()
        _, before = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<8s4I", before), (b"TOCACOMB", 3, 3, 0, 0))
        self.assertEqual(before[24:60], bytes(36))
        release = records[1]["release_events"][0]
        release["pre"]["race_clock"] += 1
        release["post"]["race_clock"] += 1
        release["post"]["release_epoch"] += 1
        _, changed = self.binaries(header, records)
        self.assertEqual(sum(a != b for a, b in zip(before, changed)), 1)
        records[2]["ai_pre"]["race_clock"] += 1
        self.assertNotEqual(self.binaries(header, records)[1], changed)

    def test_complete_checkpoints_detect_later_expected_game_and_last_history_byte(self):
        header, records = self.fixture()
        self.sidecars(header, records)
        report = FLEET.compare_driver_retention(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["later_driver_state_imports"], 0)
        snapshot = records[2]["pre"]
        entry = snapshot["actors"][15]
        game = bytearray.fromhex(entry["gameplay_hex"]); game[-1] ^= 1
        entry["gameplay_hex"] = game.hex()
        history = bytearray.fromhex(snapshot["shared"]["traffic_control"]["braking_memory_hex"]); history[-1] ^= 1
        snapshot["shared"]["traffic_control"]["braking_memory_hex"] = history.hex()
        report = FLEET.compare_driver_retention(header, records, self.path)
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 2)
        self.assertEqual(report["exact_prefix_updates"], 2)
        self.assertEqual({item["word"] for item in report["first_mismatches"]}, {72, 302})

    def test_missing_or_reordered_full_driver_checkpoint_fails_closed(self):
        header, records = self.fixture()
        self.sidecars(header, records)
        path = Path(str(self.path) + ".driver-gameplay.csv")
        rows = path.read_text().splitlines()
        rows[1], rows[2] = rows[2], rows[1]
        path.write_text("\n".join(rows) + "\n")
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_driver_retention(header, records, self.path)
        self.sidecars(header, records)
        path.write_text("\n".join(path.read_text().splitlines()[:-1]) + "\n")
        with self.assertRaisesRegex(ValueError, "Missing"):
            FLEET.compare_driver_retention(header, records, self.path)

    def test_contact_v3_initialization_preserves_live_gains_and_selected_counter(self):
        header, records = self.fixture()
        header.update(contact_weather_manifest=FLEET.CONTACT_WEATHER_MANIFEST, combined_initial_release_counter=0,
                      contact_audio_initial={"scale_hex": struct.pack("<f", 65536).hex(),
                                             "gains_hex": struct.pack("<6i", 65536, 42598, 39321, 0, 29491, 26214).hex(), "effect_flag": 0})
        _, data = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<I", data, 20)[0], 1)
        self.assertEqual(data[28:32], bytes.fromhex(header["contact_audio_initial"]["scale_hex"]))
        self.assertEqual(data[32:56], bytes.fromhex(header["contact_audio_initial"]["gains_hex"]))

    def test_human_mapper_transaction_is_explicit_external_input_not_autonomous_output(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        self.sidecars(header, records)
        entry = records[1]["consumed"]["actors"][0]
        raw = bytearray.fromhex(entry["dynamics_hex"])
        struct.pack_into("<i", raw, 0x140, 43210)
        entry["dynamics_hex"] = raw.hex()
        after = self.binaries(header, records)
        self.assertEqual(before[0], after[0])
        self.assertNotEqual(before[1], after[1])
        report = FLEET.compare_driver_retention(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["human_mapper_transactions"], 3)
        self.assertIsNone(report["human_axis_output_parity"])
        self.assertIn("422f60", report["human_mapper_source"])

    def test_cli_selects_retained_mode_and_keeps_whole_world_parity_null(self):
        header, records = self.fixture()
        source = self.path.with_suffix(".jsonl")
        comparison = {"parity": True, "exact_prefix_updates": 3, "mismatch_count": 0,
                      "driver_initializations": 1, "later_driver_imports": 0}
        arguments = ["replay-fleet-reference.py", str(source), "--compare-driver-retention", "--output", str(self.path.parent)]
        with patch.object(sys, "argv", arguments), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_binary") as fleet, patch.object(CLI.FLEET, "write_combined_binary") as journal, \
                patch.object(CLI.FLEET, "compare_combined", return_value=comparison) as compare, \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")), patch.object(sys, "stdout", io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertTrue(fleet.call_args.kwargs["compare_driver_retention"])
        self.assertTrue(journal.call_args.kwargs["compare_driver_retention"])
        self.assertTrue(compare.call_args.kwargs["compare_retained_drivers"])
        report = json.loads((self.path.parent / "fleet-comparison-retained-driver-rng.json").read_text())
        self.assertEqual(report["mode"], "retained-driver-rng")
        self.assertIsNone(report["physical_parity"])
        self.assertIsNone(report["autonomous_ai_parity"])


class HumanLampTests(TraceTestCase):
    trace_filename = 'lamps'

    def fixture(self):
        header, records = DriverRetentionTests.fixture(self)
        header["human_lamps_manifest"] = FLEET.HUMAN_LAMPS_MANIFEST
        def lamps(state, before, after=None, published=0):
            state.update(published_brake_hex=struct.pack("<16i", published, *([0] * 15)).hex(),
                         reverse_pedal_hex=bytes(64).hex(), human_flags_hex=struct.pack("<16i", 1, *([0] * 15)).hex(),
                         lamps_hex=struct.pack("<16I", before if after is None else after, *([0] * 15)).hex())
        lamps(header["wheel_queue_initial_state"], 0)
        current = 0
        for record in records:
            for event in record["combined_journal"]:
                if event["kind"] != "wheel": continue
                callback = event["event"]
                following = 1 - current
                published = 0x1999 if following else 0
                lamps(callback["pre"], current, published=published)
                lamps(callback["post"], following, published=published)
                current = following
        return header, records

    def binaries(self, header, records):
        return DriverRetentionTests.binaries(self, header, records)

    def sidecar(self, records):
        path = Path(str(self.path) + ".human-lamps.csv")
        rows = list(FLEET.human_lamps_expected(records))
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_capture_reads_callback_published_gate_and_lamps_from_exact_raw_addresses(self):
        class Reader:
            def __init__(self): self.data = {}; self.requests = []
            def preload(self, requests): self.requests.extend(requests)
            def memory(self, address, count): return bytes(self.data.get(address + i, 0) for i in range(count))
            def word(self, address): return struct.unpack("<I", self.memory(address, 4))[0]
            def put(self, address, raw): self.data.update({address + i: value for i, value in enumerate(raw)})
        reader = Reader()
        reader.put(0x4b0660, struct.pack("<I", 0x600000))
        reader.put(0x600000 + 0xac, struct.pack("<I", 1))
        reader.put(0x4c5d6e, struct.pack("<h", 16))
        for actor in range(16):
            body = FLEET.LAYOUT["body_base"] + actor * FLEET.LAYOUT["body_stride"]
            dynamics = FLEET.LAYOUT["dynamics_base"] + actor * FLEET.LAYOUT["dynamics_stride"]
            game = FLEET.LAYOUT["gameplay_base"] + actor * FLEET.LAYOUT["gameplay_stride"]
            reader.put(body + 0x515, bytes([actor]))
            reader.put(body + 0x4e8, struct.pack("<i", actor - 8))
            reader.put(body + 0x508, struct.pack("<I", actor & 1))
            reader.put(dynamics + 0x304, struct.pack("<i", actor % 3))
            reader.put(game + 8, struct.pack("<i", actor == 5))
        observed = FLEET.capture_wheel_state(reader, "446b50-entry")
        FLEET.validate_wheel_state(observed, "446b50-entry")
        self.assertEqual(struct.unpack("<16i", bytes.fromhex(observed["published_brake_hex"])), tuple(range(-8, 8)))
        self.assertEqual(struct.unpack("<16i", bytes.fromhex(observed["reverse_pedal_hex"])), tuple(i % 3 for i in range(16)))
        self.assertEqual(struct.unpack("<16I", bytes.fromhex(observed["lamps_hex"])), tuple(i & 1 for i in range(16)))
        self.assertEqual(struct.unpack("<16i", bytes.fromhex(observed["human_flags_hex"])), tuple(int(i == 5) for i in range(16)))
        observed.pop("human_flags_hex")
        with self.assertRaisesRegex(ValueError, "Incomplete actual callback"):
            FLEET.validate_wheel_state(observed, "446b50-entry")

    def test_v4_serializes_actual_pre_physical_gates_without_importing_any_lamp_or_human_expectation(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<I", before[1], 8)[0], 4)
        for record in records:
            for event in record["combined_journal"]:
                if event["kind"] != "wheel": continue
                for phase in ("pre", "post"):
                    state = event["event"][phase]
                    state["lamps_hex"] = bytes([0x77] * 64).hex()
                    state["human_flags_hex"] = bytes([0xa5] * 64).hex()
                    state["rng"] ^= 0x76543210
                event["event"]["post"]["published_brake_hex"] = bytes([0x55] * 64).hex()
                event["event"]["post"]["reverse_pedal_hex"] = bytes([0x55] * 64).hex()
        self.assertEqual(self.binaries(header, records), before)
        wheel = next(e["event"] for r in records for e in r["combined_journal"] if e["kind"] == "wheel")
        wheel["pre"]["published_brake_hex"] = struct.pack("<16i", -1, *([0] * 15)).hex()
        self.assertNotEqual(self.binaries(header, records)[1], before[1])
        after = self.binaries(header, records)
        wheel["pre"]["reverse_pedal_hex"] = struct.pack("<16i", 1, *([0] * 15)).hex()
        self.assertNotEqual(self.binaries(header, records)[1], after[1])

    def test_all_actors_pre_post_transactions_require_positive_on_and_off_and_preserve_rng(self):
        header, records = self.fixture()
        self.sidecar(records)
        report = FLEET.compare_human_lamps(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["coverage"]["on_changes"], 1)
        self.assertEqual(report["coverage"]["off_changes"], 1)
        self.assertEqual(report["coverage"]["ai_rows"], 30)
        self.assertEqual(report["coverage"]["ai_changes"], 0)
        self.assertEqual(report["row_count"], 32)
        self.assertEqual(report["later_lamp_imports"], 0)
        self.assertEqual(report["later_human_flag_imports"], 0)
        wheel = next(e["event"] for r in records for e in r["combined_journal"] if e["kind"] == "wheel")
        for phase, field in (("pre", "lamps_hex"), ("post", "lamps_hex"), ("pre", "human_flags_hex"), ("post", "human_flags_hex")):
            original = wheel[phase][field]
            raw = bytearray.fromhex(original); raw[15 * 4] ^= 1; wheel[phase][field] = raw.hex()
            report = FLEET.compare_human_lamps(header, records, self.path)
            self.assertFalse(report["numerical_parity"])
            self.assertEqual(report["first_mismatches"][0]["actor"], 15)
            wheel[phase][field] = original
        wheel["post"]["rng"] += 1
        self.assertFalse(FLEET.compare_human_lamps(header, records, self.path)["numerical_parity"])

    def test_launch_initializes_once_later_source_scalars_are_expectations_only(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        DriverRetentionTests.sidecars(self, header, records)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                if record is records[0] and phase == "pre": continue
                provenance = record[phase]["shared"]["provenance"]
                provenance["local_launch_tires_hex"] = bytes([0x77] * 16).hex()
                provenance["local_launch_tires"] = bytes([0x77] * 16)
        self.assertEqual(self.binaries(header, records), before)
        report = FLEET.compare_driver_retention(header, records, self.path)
        self.assertFalse(report["parity"])
        self.assertTrue(any(item.get("word") == 0 and item["field"] == ".driver-world.csv.value" for item in report["first_mismatches"]))
        self.assertEqual(report["later_launch_scalar_imports"], 0)
        self.assertEqual(report["launch_scalar_initializations"], 1)
        initial = records[0]["pre"]["shared"]["provenance"]
        initial["local_launch_tires_hex"] = struct.pack("<4i", -5242, 0, 0, 0).hex()
        after = self.binaries(header, records)
        self.assertEqual(before[0], after[0])
        self.assertNotEqual(before[1], after[1])

    def test_release_rpm_and_max_are_explicit_physical_context_and_bypass_is_outside_launch_scope(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        actor = records[1]["ai_pre"]["actors"][header["local_actor"]]
        for offset in (0x124, 0x20):
            raw = bytearray.fromhex(actor["dynamics_hex"])
            raw[offset] ^= 1
            actor["dynamics_hex"] = raw.hex()
            after = self.binaries(header, records)
            self.assertNotEqual(before[1], after[1])
            before = after
        callback = next(e["event"] for r in records for e in r["combined_journal"] if e["kind"] == "wheel")
        callback["pre"]["render_bypass"] = 1
        with self.assertRaisesRegex(ValueError, "unpaused keys-only"):
            self.binaries(header, records)

    def test_reader_requires_manifest_and_complete_actual_callback_lamp_snapshots(self):
        from .audio import WheelQueueTests

        header, records = WheelQueueTests.fixture(self)
        header["human_lamps_manifest"] = FLEET.HUMAN_LAMPS_MANIFEST
        snapshots = [header["wheel_queue_initial_state"]]
        snapshots += [callback[phase] for record in records for callback in record["wheel_queue_callbacks"] for phase in ("pre", "post")]
        for snapshot in snapshots:
            snapshot.update({name + "_hex": bytes(64).hex() for name in FLEET.HUMAN_LAMPS_FIELDS})
        path = self.path.with_suffix(".jsonl")
        write_trace(path, header, records)
        FLEET.read_trace(path)
        header.pop("human_lamps_manifest")
        write_trace(path, header, records)
        with self.assertRaisesRegex(ValueError, "differs from its manifest"):
            FLEET.read_trace(path)
        header["human_lamps_manifest"] = FLEET.HUMAN_LAMPS_MANIFEST
        snapshots[-1].pop("lamps_hex")
        write_trace(path, header, records)
        with self.assertRaisesRegex(ValueError, "Incomplete actual callback"):
            FLEET.read_trace(path)

    def test_lamp_mode_accepts_actual_on_off_without_inventing_new_contact_coverage(self):
        from .random import CombinedRngTests

        header, records = self.fixture()
        self.sidecar(records)
        header["contact_weather_manifest"] = FLEET.CONTACT_WEATHER_MANIFEST
        lamps = FLEET.compare_human_lamps(header, records, self.path)
        joined, wheel, whine = CombinedRngTests.components(self)
        base = {"parity": True, "compared_values": 1, "mismatch_count": 0, "first_mismatches": [], "exact_prefix_updates": 3}
        contact = {**base, "parity": False, "numerical_parity": True, "positive_random_coverage": False}
        driver = {**base, "human_mapper_transactions": 3}
        with patch.object(FLEET, "validate_combined_scope"), patch.object(FLEET, "combined_expected", return_value={}), \
                patch.object(FLEET, "combined_pre_expectations", return_value=()), patch.object(FLEET, "compare_joined_ai", return_value=joined), \
                patch.object(FLEET, "compare_wheel_queue", return_value=wheel), patch.object(FLEET, "compare_whine", return_value=whine), \
                patch.object(FLEET, "compare_contact_weather", return_value=contact), patch.object(FLEET, "compare_driver_retention", return_value=driver):
            report = FLEET.compare_combined(header, records, self.path, compare_retained_drivers=True)
        self.assertTrue(report["parity"])
        self.assertFalse(report["coverage"]["positive_contact_weather_random_coverage"])
        self.assertTrue(report["coverage"]["positive_human_lamp_on_off"])
        self.assertEqual(report["human_lamps"]["compared_values"], lamps["compared_values"])
        self.assertEqual(report["rng_initializations"], 1)
        self.assertEqual(report["later_driver_imports"], 0)

    def test_missing_reordered_and_zero_changing_callbacks_fail_acceptance(self):
        header, records = self.fixture()
        path = self.sidecar(records)
        lines = path.read_text().splitlines()
        lines[1], lines[2] = lines[2], lines[1]
        path.write_text("\n".join(lines) + "\n")
        with self.assertRaisesRegex(ValueError, "chronology"):
            FLEET.compare_human_lamps(header, records, self.path)
        self.sidecar(records)
        path.write_text("\n".join(path.read_text().splitlines()[:-1]) + "\n")
        with self.assertRaisesRegex(ValueError, "Missing"):
            FLEET.compare_human_lamps(header, records, self.path)
        for record in records:
            for entry in record["combined_journal"]:
                if entry["kind"] == "wheel":
                    entry["event"]["pre"]["lamps_hex"] = bytes(64).hex()
                    entry["event"]["post"]["lamps_hex"] = bytes(64).hex()
        self.sidecar(records)
        report = FLEET.compare_human_lamps(header, records, self.path)
        self.assertTrue(report["numerical_parity"])
        self.assertFalse(report["parity"])
        self.assertFalse(report["coverage_sufficient"])


class BrakePublicationTests(TraceTestCase):
    trace_filename = 'publication'

    def fixture(self):
        header, records = HumanLampTests.fixture(self)
        header["brake_publication_manifest"] = FLEET.BRAKE_PUBLICATION_MANIFEST
        serial = 0
        def publication(call, phase, order, value):
            snapshots = [{"boundary": boundary, "actor_count": 16, "rng": 0,
                          "raw_brake_hex": struct.pack("<16i", value, *([0] * 15)).hex(),
                          "published_brake_hex": struct.pack("<16i", value, *([0] * 15)).hex()}
                         for boundary in ("4608b0-entry", "4610ed", "4608b0-return")]
            event = {"call": call, "phase": phase, "order": order, "return_address": 0x44a200,
                     **dict(zip(("pre", "stores", "post"), snapshots))}
            return [{"kind": "publication", "event": event},
                    {"kind": "publication_return", "event": {"call": call, "phase": phase, "order": order + 1, "post": snapshots[2]}}]
        for record in records: record["publication_calls"] = []
        journal = []
        for entry in records[1]["combined_journal"]:
            if entry["kind"] == "wheel":
                journal += publication(serial, "frame", len(journal) + 1, 0x1999 if serial == 0 else 0)
                serial += 1
            entry["event"]["order"] = len(journal) + 1
            journal.append(entry)
        records[1]["combined_journal"] = journal
        records[2]["publication_calls"] = publication(serial, "pre_to_ai", len(journal) + 1, 0)
        return header, records

    def binaries(self, header, records): return DriverRetentionTests.binaries(self, header, records)

    def sidecar(self, records):
        rows = list(FLEET.brake_publication_expected(records))
        path = Path(str(self.path) + ".brake-publication.csv")
        fields = ["id", "event", "call", "stage", "actor", "raw_brake", "published_brake", "rng"]
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields); writer.writeheader(); writer.writerows(rows)
        return path

    def test_v5_initial_published_once_all_later_human_raw_and_published_are_expectations(self):
        header, records = self.fixture()
        original = self.binaries(header, records)
        self.assertEqual(struct.unpack_from("<I", original[1], 8)[0], 5)
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES):
                for actor, entry in enumerate(record[phase]["actors"]):
                    for field, offset in (("body", 0x4e8), ("dynamics", 0x144)):
                        if record is records[0] and phase == "pre": continue
                        if phase == "consumed" and actor == 0 and field == "dynamics": continue  # explicit actual human mapper input
                        raw = bytearray(entry[field]); raw[offset:offset + 4] = struct.pack("<i", 0x1234567 + actor)
                        entry[field] = bytes(raw); entry[field + "_hex"] = raw.hex()
            for kind, _, event in FLEET.combined_events(record):
                if kind == "wheel":
                    for phase in ("pre", "post"):
                        event[phase]["published_brake_hex"] = bytes([0x77] * 64).hex()
                if kind == "publication":
                    for phase in ("pre", "stores", "post"):
                        event[phase].update(raw_brake_hex=bytes([0x55] * 64).hex(), published_brake_hex=bytes([0x44] * 64).hex(), rng=999)
        self.assertEqual(self.binaries(header, records), original)
        raw = bytearray.fromhex(records[0]["pre"]["actors"][15]["body_hex"]); raw[0x4e8] ^= 1
        records[0]["pre"]["actors"][15]["body_hex"] = raw.hex()
        self.assertEqual(self.binaries(header, records)[0], original[0])
        self.assertNotEqual(self.binaries(header, records)[1], original[1])

    def test_actual_human_mapper_brake_and_callback_reverse_remain_explicit_inputs(self):
        header, records = self.fixture()
        before = self.binaries(header, records)
        raw = bytearray.fromhex(records[1]["consumed"]["actors"][0]["dynamics_hex"]); raw[0x144] ^= 1
        records[1]["consumed"]["actors"][0]["dynamics_hex"] = raw.hex()
        self.assertEqual(self.binaries(header, records)[0], before[0])
        self.assertNotEqual(self.binaries(header, records)[1], before[1])
        before = self.binaries(header, records)
        wheel = next(event for kind, _, event in FLEET.combined_events(records[1]) if kind == "wheel")
        wheel["pre"]["reverse_pedal_hex"] = struct.pack("<16i", 1, *([0] * 15)).hex()
        self.assertNotEqual(self.binaries(header, records)[1], before[1])

    def test_complete_stages_all16_descending_zero_multiple_and_inside_update(self):
        header, records = self.fixture()
        FLEET.validate_publication_scope(header, records)
        self.sidecar(records)
        report = FLEET.compare_brake_publication(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertEqual(report["coverage"]["calls"], 3)
        self.assertEqual(report["row_count"], 3 * 3 * 16)
        self.assertEqual(report["compared_values"], 3 * 3 * 16 * 3)
        self.assertEqual(report["coverage"]["inter_update_call_histogram"], {"2": 1, "0": 1})
        self.assertEqual(report["coverage"]["inside_update_calls"], 1)
        rows = list(FLEET.brake_publication_expected(records))
        self.assertEqual([row["actor"] for row in rows[:16]], list(range(15, -1, -1)))
        event = next(event for kind, _, event in FLEET.combined_events(records[1]) if kind == "publication")
        event["stores"]["raw_brake_hex"] = bytes([1] * 64).hex()
        self.assertFalse(FLEET.compare_brake_publication(header, records, self.path)["numerical_parity"])

    def test_tail_contacts_stay_between_store_checkpoint_and_return(self):
        header, records = self.fixture()
        journal = records[1]["combined_journal"]
        index = next(i for i, entry in enumerate(journal) if entry["kind"] == "publication_return")
        order = journal[index]["event"]["order"]
        for entry in journal[index:]: entry["event"]["order"] += 1
        journal.insert(index, {"kind": "impact", "event": {"phase": "frame", "order": order}})
        kinds = [kind for kind, _, _ in FLEET.combined_events(records[1])]
        i = kinds.index("publication")
        self.assertEqual(kinds[i:i + 3], ["publication", "impact", "publication_return"])
        FLEET.validate_publication_scope(header, records)

    def test_combined_report_replaces_observed_published_scope_without_inventing_contact_coverage(self):
        from .random import CombinedRngTests

        header, records = self.fixture()
        header["contact_weather_manifest"] = FLEET.CONTACT_WEATHER_MANIFEST
        self.sidecar(records)
        HumanLampTests.sidecar(self, records)
        joined, wheel, whine = CombinedRngTests.components(self)
        base = {"parity": True, "compared_values": 1, "mismatch_count": 0, "first_mismatches": [], "exact_prefix_updates": 3}
        contact = {**base, "parity": False, "numerical_parity": True, "positive_random_coverage": False}
        driver = {**base, "human_mapper_transactions": 3}
        with patch.object(FLEET, "validate_combined_scope"), patch.object(FLEET, "combined_expected", return_value={}), \
                patch.object(FLEET, "combined_pre_expectations", return_value=()), patch.object(FLEET, "compare_joined_ai", return_value=joined), \
                patch.object(FLEET, "compare_wheel_queue", return_value=wheel), patch.object(FLEET, "compare_whine", return_value=whine), \
                patch.object(FLEET, "compare_contact_weather", return_value=contact), patch.object(FLEET, "compare_driver_retention", return_value=driver):
            report = FLEET.compare_combined(header, records, self.path, compare_retained_drivers=True)
        self.assertTrue(report["parity"])
        self.assertFalse(report["coverage"]["positive_contact_weather_random_coverage"])
        self.assertTrue(report["coverage"]["positive_brake_publication"])
        self.assertEqual(report["brake_publication"]["published_initializations"], 1)
        self.assertEqual(report["brake_publication"]["later_raw_brake_imports"], 0)
        self.assertIn("derived retained body4e8", report["human_lamps"]["input_scope"])
        self.assertEqual(report["human_lamps"]["physical_input_values_checked"], report["human_lamps"]["row_count"])
        self.assertIn("first421aa0 PRE", report["boundary_scope"])

    def test_missing_return_actor_checkpoint_and_zero_calls_are_not_accepted(self):
        header, records = self.fixture()
        path = self.sidecar(records)
        lines = path.read_text().splitlines(); path.write_text("\n".join(lines[:-1]) + "\n")
        with self.assertRaisesRegex(ValueError, "Missing actual publication"): FLEET.compare_brake_publication(header, records, self.path)
        records[2]["publication_calls"].pop()
        with self.assertRaisesRegex(ValueError, "Missing actual publication return"): FLEET.validate_publication_scope(header, records)
        for record in records:
            record["publication_calls"] = []
            record["combined_journal"] = [entry for entry in record["combined_journal"] if entry["kind"] not in ("publication", "publication_return")]
        self.sidecar(records)
        report = FLEET.compare_brake_publication(header, records, self.path)
        self.assertTrue(report["numerical_parity"])
        self.assertFalse(report["parity"])


class DriverPhysicsTests(TraceTestCase):
    trace_filename = 'driver'
    fixture = DriverRetentionTests.fixture

    def physical_binary(self, header, records):
        path = self.path.with_suffix(".physical.bin")
        FLEET.write_binary(path, header, records, driver_physics_inputs=True)
        return path.read_bytes()

    @staticmethod
    def change(entry, name, offset, value=0xa5):
        raw = bytearray(entry[name]); raw[offset] ^= value
        entry[name] = bytes(raw); entry[name + "_hex"] = raw.hex()

    def test_consumed_and_future_control_copies_never_enter_physical_inputs(self):
        header, records = self.fixture()
        original = self.physical_binary(header, records)
        for record in records:
            for phase in ("consumed", "contacts", "ground", "post"):
                for entry in record[phase]["actors"]:
                    for name, offsets in (("gameplay", (8, 0x28, 0xcc)), ("body", (0x500, 0x508)),
                                          ("dynamics", (0xdc, 0xf8, 0x138, 0x13c, 0x140, 0x144, 0x330))):
                        for offset in offsets: self.change(entry, name, offset)
        self.assertEqual(self.physical_binary(header, records), original)

    def test_declared_physical_fields_and_gear_neighbor_bytes_remain_inputs(self):
        header, records = self.fixture()
        for name, offset in (("body", 0xbc), ("body", 0x1f0), ("dynamics", 0x11c),
                             ("dynamics", 0x304), ("dynamics", 0x14c), ("dynamics", 0x148),
                             ("dynamics", 0x331), ("dynamics", 0x332), ("dynamics", 0x333)):
            before = self.physical_binary(header, records)
            self.change(records[1]["consumed"]["actors"][1], name, offset, 1)
            self.assertNotEqual(self.physical_binary(header, records), before, (name, offset))
        before, _ = DriverRetentionTests.binaries(self, header, records)
        self.change(records[1]["ai_pre"]["actors"][1], "dynamics", 0x330, 1)
        after, _ = DriverRetentionTests.binaries(self, header, records)
        self.assertNotEqual(before, after)

    def test_published_brake_is_generated_only_with_actual_publication_provenance(self):
        header, records = self.fixture()
        before = self.physical_binary(header, records)
        self.change(records[1]["consumed"]["actors"][1], "body", 0x4e8)
        self.assertNotEqual(self.physical_binary(header, records), before)
        header["brake_publication_manifest"] = FLEET.BRAKE_PUBLICATION_MANIFEST
        before = self.physical_binary(header, records)
        self.change(records[1]["consumed"]["actors"][1], "body", 0x4e8)
        self.assertEqual(self.physical_binary(header, records), before)

    def sidecars(self, records):
        path = Path(str(self.path) + ".physical.csv")
        physical = [{"id": record["id"], "actor": actor, "phase": phase,
                     **FLEET.expected_actor(entry, record[source])}
                    for record in records for phase, source in (("integration", "contacts"), ("ground", "ground"))
                    for actor, entry in enumerate(record[source]["actors"])]
        detached = [{"id": record["id"], "phase": phase, "slot": slot,
                     **FLEET.expected_detached(record[source], slot)}
                    for record in records for phase, source in (("integration", "contacts"), ("ground", "ground"), ("post", "post"))
                    for slot in range(10)]
        launch = [{"id": record["id"], "actor": 0, "phase": "consumed", **FLEET.expected_launch(record["consumed"])} for record in records]
        selection = [{"id": record["id"], "actor": actor, "mode": struct.unpack_from("<I", entry["body"], 0x4f0)[0]}
                     for record in records for actor, entry in enumerate(record["consumed"]["actors"])]
        for suffix, rows in (("", physical), (".detached.csv", detached), (".launch.csv", launch), (".selection.csv", selection)):
            with Path(str(path) + suffix).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        return path

    def test_reports_complete_actor_phase_counts_and_rejects_final_actor_corruption(self):
        header, records = self.fixture()
        path = self.sidecars(records)
        combined = {"parity": True, "numerical_parity": True, "coverage_sufficient": True,
                    "compared_values": 17, "mismatch_count": 0, "exact_prefix_updates": len(records)}
        with patch.object(FLEET, "compare_combined", return_value=combined):
            report = FLEET.compare_driver_physics(header, records, self.path)
        self.assertTrue(report["parity"])
        self.assertIsNone(report["continuous_physics_parity"])
        self.assertEqual(report["physical_world_imports"], len(records))
        self.assertNotIn("initial_import", report["launch"])
        self.assertNotIn("local_mapping_updates", report["launch"])
        self.assertEqual(report["launch"]["derived_fields"], ["tire offsets"])
        self.assertIn("no second422f60 call", report["launch"]["input_provenance"])
        physical = report["physical"]
        self.assertEqual(physical["rows"], len(records) * 16 * 2)
        self.assertEqual(physical["compared_values_per_phase"],
                         dict.fromkeys(("integration", "ground"), len(records) * 16 * physical["fields_per_actor_phase"]))
        with path.open(newline="") as stream: rows = list(csv.DictReader(stream))
        rows[-1]["raw_brake"] = str(int(rows[-1]["raw_brake"]) + 1)
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        with patch.object(FLEET, "compare_combined", return_value=combined):
            broken = FLEET.compare_driver_physics(header, records, self.path)
        self.assertFalse(broken["parity"])
        self.assertEqual(broken["mismatch_count"], 1)
        self.assertEqual(broken["physical"]["mismatches_per_actor"][-1], 1)
        self.assertEqual(broken["exact_prefix_updates"], len(records) - 1)

    def test_cli_selects_single_joined_transaction_and_preserves_original_interval_scope(self):
        header, records = self.fixture()
        trace = self.path.with_suffix(".jsonl"); trace.write_text("original interval")
        expected = {"parity": True, "physical_parity": True, "mismatch_count": 0, "exact_prefix_updates": 2}
        commands = []
        def execute(command, **_kwargs):
            commands.append(command)
            return subprocess.CompletedProcess(command, 0, "", "")
        argv = ["replay-fleet-reference.py", str(trace), "--output", str(self.path.parent), "--compare-driver-physics", "--updates", "2"]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "validate_combined_scope"), patch.object(CLI.FLEET, "compare_driver_physics", return_value=expected), \
                patch.object(CLI.subprocess, "run", side_effect=execute), patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(len(commands), 1)
        self.assertEqual(commands[0][1], "--compare-driver-physics")
        self.assertEqual(len(commands[0]), 8)
        report = json.loads((self.path.parent / "fleet-comparison-retained-driver-independent-physics.json").read_text())
        self.assertTrue(report["physical_parity"])
        self.assertFalse(report["complete_source_interval"])
        self.assertIsNone(report["continuous_physics_parity"])
        self.assertEqual(report["source_updates"], len(records))
