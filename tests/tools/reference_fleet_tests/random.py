"""Fleet reference checks for random."""

from .support import (
    TraceTestCase,
    FLEET,
    Path,
    csv,
    fixture,
    io,
    joined_fixture,
    patch,
    struct,
    write_trace,
)


class CombinedRngTests(TraceTestCase):
    trace_filename = 'trace.jsonl'

    def fixture(self):
        from .audio import WheelQueueTests
        from .audio import WhineTests

        whine_header, whine_records = WhineTests.fixture(self)
        wheel_header, records = WheelQueueTests.fixture(self)
        header = {**wheel_header, **whine_header, "combined_rng_manifest": FLEET.COMBINED_MANIFEST, "combined_initial_rng": 0}
        def audio(rng=0):
            return {**{name + "_hex": bytes(size).hex() for name, (_, size) in FLEET.COMBINED_AUDIO_BLOCKS.items()},
                    "rng": rng, "enabled": 1, "wet": 0, "showcase": 0, "ownership_hex": "00" + "ff" * 15,
                    "camera_views_hex": bytes(8).hex(), "camera_state_hex": bytes(8).hex()}
        header["combined_audio_initial"] = audio()
        for record, whine_record in zip(records, whine_records):
            for name in ("whine_callbacks", "whine_update_pre", "whine_update_post"): record[name] = whine_record[name]
            record.update(combined_audio_pre=audio(), combined_audio_post=audio(), release_events=[], surface_calls=[])
            record["combined_journal"] = [{"kind": "wheel", "event": callback} for callback in record["wheel_queue_callbacks"]]
            record["combined_journal"] += [{"kind": "motor", "event": callback} for callback in record["whine_callbacks"]]
            if record["combined_journal"]: record["combined_journal"].insert(1, {"kind": "mute", "event": {"pre": audio(), "post": audio()}})
            record["ai_pre"] = record["pre"]
            record["skid_dispatch"].update(audio_pre=audio(), audio_post=audio())
        records[1]["surface_calls"] = [{"actor": 0, "wheel": 3, "material": 2, "pre": audio(), "post": audio()}]
        return header, records

    def binary(self, header, records):
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(self.path, header, records)
        return self.path.read_bytes()

    def components(self):
        base = {"parity": True, "compared_values": 1, "mismatch_count": 0, "first_mismatches": [], "exact_prefix_updates": 4,
                "outside_compared_fields": []}
        joined = {**base, "coverage_sufficient": True}
        wheel = {**base, "skid_dispatch": {**base, "coverage": {"draws": 1}}}
        whine = {**base, "coverage": {"rng_advance_calls": 1}, "positive_command_coverage": True}
        return joined, wheel, whine

    def compare(self, header, records, prechecks=()):
        joined, wheel, whine = self.components()
        with patch.object(FLEET, "validate_combined_scope"), patch.object(FLEET, "combined_pre_expectations", return_value=prechecks), patch.object(FLEET, "compare_joined_ai", return_value=joined), \
                patch.object(FLEET, "compare_wheel_queue", return_value=wheel), patch.object(FLEET, "compare_whine", return_value=whine):
            return FLEET.compare_combined(header, records, self.path)

    def sidecars(self, header, records):
        expected = FLEET.combined_expected(header, records)
        for suffix, rows in expected.items():
            with Path(str(self.path) + suffix).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
        return expected

    def test_reader_rejects_partial_or_unmanifested_combined_observations(self):
        header, records = fixture(1)
        header["combined_initial_rng"] = 0
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "combined RNG provenance"): FLEET.read_trace(self.path)
        del header["combined_initial_rng"]
        records[0]["combined_journal"] = []
        write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "lack their initial provenance"): FLEET.read_trace(self.path)

    def test_actual_journal_preserves_zero_multiple_and_mixed_callback_order(self):
        header, records = self.fixture()
        self.assertEqual([kind for kind, _, _ in FLEET.combined_events(records[1])][:5], ["wheel", "mute", "wheel", "motor", "motor"])
        self.assertEqual(FLEET.combined_events(records[2])[0][:2], ("boundary", "pre"))
        binary = self.binary(header, records)
        self.assertEqual(struct.unpack_from("<8sIII", binary), (b"TOCACOMB", 1, 4, 0))
        # One fixed initial wheel/audio/engine block, then only raw event inputs.
        self.assertEqual(struct.unpack_from("<2I", binary, 20 + 896 + 232 + 718), (0, 9))

    def test_later_rng_and_audio_numeric_expectations_never_enter_journal_binary(self):
        header, records = self.fixture()
        before = self.binary(header, records)
        def mutate(value):
            if isinstance(value, dict):
                for key, item in value.items():
                    if key == "rng": value[key] = int(item) ^ 0x76543210
                    elif key in ("reset_hex", "mask_hex", "surface_hex", "wheel_materials_hex", "pending_mute_hex", "camera_state_hex"):
                        value[key] = bytes([31] * len(bytes.fromhex(item))).hex()
                    elif key == "state": value[key] = {name: 123 for name in item}
                    else: mutate(item)
            elif isinstance(value, list):
                for item in value: mutate(item)
        mutate(records)
        self.assertEqual(self.binary(header, records), before)
        header["combined_initial_rng"] = 0x76543210
        after = self.binary(header, records)
        self.assertEqual(after[:16], before[:16])
        self.assertNotEqual(after[16:20], before[16:20])
        self.assertEqual(after[20:], before[20:])

    def test_raw_surface_and_physical_context_inputs_change_binary(self):
        header, records = self.fixture()
        before = self.binary(header, records)
        records[1]["surface_calls"][0]["pre"]["wet"] = 1
        self.assertNotEqual(self.binary(header, records), before)
        before = self.binary(header, records)
        records[1]["whine_callbacks"][0]["pre"]["physical"][0]["engine_speed_hex"] = struct.pack("<i", 5000).hex()
        self.assertNotEqual(self.binary(header, records), before)

    def test_combined_fleet_inputs_contain_no_rng_observations_including_ai_pre(self):
        header, records = joined_fixture()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, header, records, compare_joined_ai=True, compare_combined_rng=True)
        before = binary.read_bytes()
        for record in records:
            for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES): record[phase]["rng"] ^= 0x76543210
            for event in record["release_events"]:
                for phase in ("pre", "post"): event[phase]["rng"] ^= 0x76543210
        FLEET.write_binary(binary, header, records, compare_joined_ai=True, compare_combined_rng=True)
        self.assertEqual(binary.read_bytes(), before)
        with self.assertRaisesRegex(ValueError, "require joined AI"):
            FLEET.write_binary(binary, header, records, compare_combined_rng=True)

    def test_all_boundary_rng_and_material_audio_outputs_are_compared(self):
        header, records = self.fixture()
        self.sidecars(header, records)
        report = self.compare(header, records)
        self.assertTrue(report["parity"])
        self.assertEqual(report["rng_initializations"], 1)
        self.assertEqual(report["later_rng_imports"], 0)
        self.assertIn("refreshed", report["driver_state"])
        records[1]["surface_calls"][0]["post"]["mask_hex"] = "01" + "00" * 15
        report = self.compare(header, records)
        self.assertFalse(report["parity"])
        self.assertTrue(any(item["field"].endswith(".mask") for item in report["first_mismatches"]))
        self.sidecars(header, records)
        records[2]["contacts"]["rng"] ^= 17
        report = self.compare(header, records)
        self.assertFalse(report["parity"])
        self.assertTrue(any(item["field"].endswith(".rng") for item in report["first_mismatches"]))

    def test_compared_pre_checkpoint_state_is_expectation_only(self):
        header, records = self.fixture()
        before = self.binary(header, records)
        self.sidecars(header, records)
        records[2]["combined_audio_pre"]["reset_hex"] = struct.pack("<16i", 1, *([0] * 15)).hex()
        self.assertEqual(self.binary(header, records), before)
        self.assertFalse(self.compare(header, records)["parity"])
        self.sidecars(header, records)
        records[2]["whine_update_pre"]["state"]["phase"] += 1
        self.assertEqual(self.binary(header, records), before)
        self.assertFalse(self.compare(header, records)["parity"])

    def test_actual_material_pre_state_is_compared_against_prior_native_event(self):
        header, records = self.fixture()
        before = self.binary(header, records)
        self.sidecars(header, records)
        material_ordinal = next(i for i, (kind, _, _) in enumerate(FLEET.combined_events(records[1])) if kind == "material")
        def checkpoints():
            return [(suffix, previous, actor, {"mask": wanted["mask"]}, metadata)
                    for suffix, previous, actor, wanted, metadata in FLEET.combined_pre_expectations(records)
                    if suffix == ".combined-audio.csv" and actor == 0 and metadata == {"update": 1, "event": material_ordinal}]
        self.assertEqual(len(checkpoints()), 1)
        self.assertTrue(self.compare(header, records, checkpoints())["parity"])
        records[1]["surface_calls"][0]["pre"]["mask_hex"] = "01" + "00" * 15
        self.assertEqual(self.binary(header, records), before)
        report = self.compare(header, records, checkpoints())
        self.assertFalse(report["parity"])
        self.assertEqual(report["first_mismatches"][0]["field"], ".combined-audio.csv.PRE.mask")

    def test_complete_chronology_rejects_missing_or_reordered_native_events(self):
        header, records = self.fixture()
        self.sidecars(header, records)
        path = Path(str(self.path) + ".combined-rng.csv")
        rows = list(csv.reader(io.StringIO(path.read_text())))
        path.write_text("\n".join(",".join(row) for row in rows[:-1]) + "\n")
        with self.assertRaisesRegex(ValueError, "Missing or extra"): self.compare(header, records)
        self.sidecars(header, records)
        rows = list(csv.reader(io.StringIO(path.read_text())))
        rows[1], rows[2] = rows[2], rows[1]
        path.write_text("\n".join(",".join(row) for row in rows) + "\n")
        with self.assertRaisesRegex(ValueError, "chronology"): self.compare(header, records)


class ContactWeatherRngTests(TraceTestCase):
    trace_filename = 'contact'

    def fixture(self):
        header, records = CombinedRngTests.fixture(self)
        header.update(contact_weather_manifest=FLEET.CONTACT_WEATHER_MANIFEST,
                      contact_audio_initial={"scale_hex": struct.pack("<f", 65536).hex(),
                                             "gains_hex": struct.pack("<6i", 65536, 42598, 39321, 0, 29491, 26214).hex(), "effect_flag": 0},
                      combined_initial_release_counter=242)
        audio = header["combined_audio_initial"]
        def context(rng):
            return {**audio, "rng": rng, "stereo": 0, "effect_flag": 0,
                    "listeners": [{"player": 0, "view": 0, "showcase": 0, "position_float_hex": bytes(12).hex(), "inverse_heading": 0}]}
        def event(kind, order, phase):
            item = {"kind": kind, "order": order, "phase": phase, "handle": 0, "local": 1, "position_hex": bytes(12).hex(), "strength": 65536,
                    "pre": context(0), "post": context(2531011), "sample": 31 if kind == 3 else 50 if kind == 10 else 76,
                    "queries": [], "commands": [], "spatial": {"accepted": 1, "volume": 75, "angle": 64}, "volume": 44, "pan": 64}
            if kind == 3:
                item.update(allocation=0x4d3d28, queries=[{"return_address": 0x424ce2, "target": 0x4d3d28, "playing": 0},
                                                       {"return_address": 0x424a2e, "target": 0x4d3d28, "playing": 0},
                                                       {"return_address": 0x424a47, "target": 0x4d3d28, "playing": 1}])
                item["commands"] = [{"kind": "play", "sample": 31, "loops": 1, "volume": 44, "offset": 0, "pan": 64, "target": 0x4d3d28, "allocation_succeeded": 1}]
            elif kind == 25:
                del item["spatial"]
                item["commands"] = [{"kind": "play", "sample": 76, "loops": 1, "volume": 6553600, "offset": 0, "pan": 64, "target": 0x4d3d18}]
                item["volume"] = 6553600
            return item
        for record in records:
            record["contact_audio_calls"] = []
            for index, entry in enumerate(record["combined_journal"]): entry["event"]["order"] = index + 1
            for call in record["surface_calls"]: call["order"] = 20
        records[0]["contact_audio_calls"] = [{"kind": "impact", "event": event(3, 10, "consumed_to_contacts")}]
        records[1]["contact_audio_calls"] = [{"kind": "scrape", "event": event(10, 19, "contacts_to_ground")},
                                               {"kind": "impact", "event": event(3, 21, "contacts_to_ground")}]
        records[2]["contact_audio_calls"] = [{"kind": "impact", "event": event(25, 30, "skid_to_post")}]
        return header, records

    def binary(self, header, records):
        with patch.object(FLEET, "validate_combined_scope"):
            FLEET.write_combined_binary(self.path, header, records)
        return self.path.read_bytes()

    def test_raw_listener_camera_inputs_remain_float_bytes_and_pre_camera_mix_is_not_position(self):
        camera = 0x600000
        data = {0x4b0660: struct.pack("<I", 0x700000), 0x7000ac: struct.pack("<I", 1),
                0x4c3478: struct.pack("<24I", *([camera] * 24)), 0x4c3790: bytes(24),
                0x4c3a66: struct.pack("<4h", 2, 0, 0, 0), 0x4c3a64: bytes(2),
                0x4d59c8: struct.pack("<h", -123), camera + 0xcc: struct.pack("<3f", 1.25, 2.5, -3.75),
                0x4d3ad0: struct.pack("<2i", 1, 0)}
        class Reader:
            def preload(self, ranges): pass
            def memory(self, address, size): return data.get(address, bytes(size))[:size]
            def word(self, address): return struct.unpack("<I", self.memory(address, 4))[0]
        actual = FLEET.capture_contact_context(Reader())
        self.assertEqual(actual["listeners"][0]["position_float_hex"], struct.pack("<3f", 1.25, 2.5, -3.75).hex())
        self.assertEqual(actual["listeners"][0]["view"], 2)
        self.assertEqual(actual["listeners"][0]["inverse_heading"], -123)
        self.assertEqual(actual["camera_state_hex"], struct.pack("<2i", 1, 0).hex())

    def test_play_return_evidence_is_optional_for_prior_capture_and_never_a_runtime_input(self):
        header, records = self.fixture()
        FLEET.validate_contact_weather_scope(header, records)
        header["contact_play_returns"] = True
        with self.assertRaises(KeyError): FLEET.validate_contact_weather_scope(header, records)
        for record in records:
            for entry in record["contact_audio_calls"]:
                for command in entry["event"]["commands"]:
                    if command["kind"] == "play": command.update(return_eax=1, allocation_succeeded=1, voice_handle_after=0x600000)
        FLEET.validate_contact_weather_scope(header, records)
        before = self.binary(header, records)
        command = records[0]["contact_audio_calls"][0]["event"]["commands"][0]
        command.update(return_eax=0, allocation_succeeded=0, voice_handle_after=0)
        FLEET.validate_contact_weather_scope(header, records)
        self.assertEqual(self.binary(header, records), before)

    def test_v2_preserves_actual_contact_material_tire_thunder_order(self):
        header, records = self.fixture()
        FLEET.validate_contact_weather_scope(header, records)
        kinds = [kind for kind, _, _ in FLEET.combined_events(records[1])]
        selected = kinds[kinds.index("scrape"):]
        self.assertEqual(selected[:3], ["scrape", "material", "impact"])
        kinds = [kind for kind, _, _ in FLEET.combined_events(records[2])]
        self.assertEqual(kinds[-3:], ["tire", "impact", "boundary"])
        raw = self.binary(header, records)
        self.assertEqual(struct.unpack_from("<8sIII", raw), (b"TOCACOMB", 2, 4, 0))
        self.assertEqual(struct.unpack_from("<I", raw, 20), (242,))
        self.assertEqual(struct.unpack_from("<f", raw, 24), (65536.0,))
        self.assertEqual(struct.unpack_from("<2I", raw, 20 + 36 + 896 + 232 + 718), (0, 10))

    def test_later_audio_rng_sample_allocation_and_commands_are_not_runtime_inputs(self):
        header, records = self.fixture()
        before = self.binary(header, records)
        for record in records:
            for entry in record["contact_audio_calls"]:
                event = entry["event"]
                event.update(sample=99, volume=-8, pan=33, allocation=0, gain=33)
                for phase in ("pre", "post"):
                    event[phase]["rng"] ^= 0x12345678
                    for name in FLEET.COMBINED_AUDIO_BLOCKS: event[phase][name + "_hex"] = bytes([3] * len(bytes.fromhex(event[phase][name + "_hex"]))).hex()
                    event[phase]["camera_state_hex"] = bytes(8).hex()
                for command in event["commands"]: command.update(sample=42, volume=1, allocation_succeeded=0)
        self.assertEqual(self.binary(header, records), before)

    def test_observed_physical_and_external_voice_gates_are_explicit_inputs(self):
        header, records = self.fixture()
        before = self.binary(header, records)
        event = records[0]["contact_audio_calls"][0]["event"]
        event["pre"]["listeners"][0]["position_float_hex"] = struct.pack("<3f", 1.0, 2.0, 3.0).hex()
        self.assertNotEqual(self.binary(header, records), before)
        before = self.binary(header, records)
        event["queries"][1]["playing"] = 1
        self.assertNotEqual(self.binary(header, records), before)

    def test_voice_pool_prefix_derives_allocation_without_importing_allocation_output(self):
        _, records = self.fixture()
        event = records[0]["contact_audio_calls"][0]["event"]
        self.assertEqual(FLEET.contact_voice_gates("impact", event), (0, 0, 1))
        event["allocation"] = 0
        self.assertEqual(FLEET.contact_voice_gates("impact", event), (0, 0, 1))
        with self.assertRaisesRegex(ValueError, "allocation differs"): FLEET.contact_voice_gates("impact", event, audit_allocation=True)
        event["queries"][0]["target"] += 4
        with self.assertRaisesRegex(ValueError, "pool traversal"): FLEET.contact_voice_gates("impact", event)

    def test_actual_selected_counter_is_not_decremented_twice_and_later_values_derive(self):
        _, records = joined_fixture()
        record = records[2]
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES): record[phase]["shared"]["ai_state"]["release_counter"] = 242
        record["release_events"] = []
        self.assertEqual(FLEET.validate_joined_release(record, 242, selected_initial=True), 242)
        with self.assertRaisesRegex(ValueError, "source-derived sequence"): FLEET.validate_joined_release(record, 242)
        for phase in (*FLEET.BOUNDARIES, *FLEET.AI_BOUNDARIES, *FLEET.TRAFFIC_BOUNDARIES): record[phase]["shared"]["ai_state"]["release_counter"] = 241
        self.assertEqual(FLEET.validate_joined_release(record, 242), 241)

    def test_source_unpartitioned_ai_contacts_and_missing_chronology_are_rejected(self):
        header, records = self.fixture()
        records[0]["contact_audio_calls"][0]["event"]["phase"] = "ai_to_consumed"
        with self.assertRaisesRegex(ValueError, "unpartitioned AI"): FLEET.validate_contact_weather_scope(header, records)
        records[0]["contact_audio_calls"][0]["event"]["phase"] = "consumed_to_contacts"
        records[1]["contact_audio_calls"].reverse()
        with self.assertRaisesRegex(ValueError, "out-of-order"): FLEET.validate_contact_weather_scope(header, records)

    def test_comparison_separates_raw_wrapper_requests_and_allocation_evidence(self):
        header, records = self.fixture()
        wanted = FLEET.contact_expected(records)
        fields = {".contact-events.csv": ["id", "event", "consumer", "kind", "spatial", "accepted", "drew", "sample", "spatial_volume", "volume", "pan", "angle", "rng", "primary", "secondary", "action"],
                  ".contact-commands.csv": ["id", "event", "ordinal", "kind", "sample", "loops", "volume", "offset", "pan", "target", "value"]}
        for suffix, rows in wanted.items():
            with Path(str(self.path) + suffix).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fields[suffix], restval=0); writer.writeheader(); writer.writerows(rows)
        result = FLEET.compare_contact_weather(records, self.path)
        self.assertTrue(result["parity"])
        self.assertEqual(result["coverage"]["random_calls_by_kind"], {"3": 2, "10": 1, "25": 1})
        self.assertEqual(result["coverage"]["wrapper_allocation_results"], {"1": 2})
        self.assertIn("not independent backend-start proof", result["request_scope"])
        records[2]["contact_audio_calls"][0]["event"]["commands"][0]["volume"] -= 1
        self.assertFalse(FLEET.compare_contact_weather(records, self.path)["parity"])
