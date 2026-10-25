"""Fleet reference checks for cold grid."""

from .support import (
    TraceTestCase,
    Path,
    copy,
    CLI,
    FLEET,
    ROOT,
    csv,
    io,
    joined_fixture,
    json,
    patch,
    state,
    struct,
    subprocess,
    sys,
    write_trace,
    zlib,
)


class ColdGridInitializationTests(TraceTestCase):
    trace_filename = 'cold-grid.jsonl'

    capture_recorder = True

    class Reader:
        def __init__(self, scalar, config_pointer=0x700000):
            self.data, self.reads, self.preloads = {}, [], []
            self.allowed = [(0x4b0660, 4), (0x700000, 0x620),
                            *((address & ~3, ((address + size + 3) & ~3) - (address & ~3)) for address, size in FLEET.COLD_GRID_INIT_BLOCKS.values()),
                            *((FLEET.LAYOUT[name + "_base"], 16 * FLEET.LAYOUT[name + "_stride"])
                              for name in ("body", "dynamics", "gameplay"))]
            self.put(0x4b0660, struct.pack("<I", config_pointer))
            config = bytearray(0x620)
            struct.pack_into("<I", config, 0x20, 1); struct.pack_into("<I", config, 0xac, 1)
            struct.pack_into("<H", config, 0x5f6, scalar["session_stage"])
            self.put(0x700000, config)
            self.put(0x4c5d6e, struct.pack("<H", 16))
            self.put(0x4d37a0, bytes([14, 255, 255, 255]))
            self.put(0x4b9e78, struct.pack("<I", scalar["rng"]))
            self.put(0x4d3840, struct.pack("<I", scalar["race_clock"]))
            #Pre-init actor bytes and embedded pointers may be stale. A raw
            #boundary capture must not validate body modes or follow these.
            for value, name in enumerate(("body", "dynamics", "gameplay"), 0xa5):
                self.put(FLEET.LAYOUT[name + "_base"], bytes([value]) * (16 * FLEET.LAYOUT[name + "_stride"]))
            self.put(0x54c120, struct.pack("<4I", 0xdeadbeef, 0xffffffff, 0, 0xcafebabe))
            self.put(0x54b948, struct.pack("<I", 0xdeadbeef))

        def put(self, address, raw):
            self.data.update({address + index: value for index, value in enumerate(raw)})

        def check(self, address, size):
            if not any(start <= address and address + size <= start + extent for start, extent in self.allowed):
                raise AssertionError(f"Unsafe nonfixed read {address:x}+{size:x}")

        def preload(self, ranges):
            for address, size in ranges:
                if address & 3 or size & 3: raise AssertionError("PausedMemory preload requires aligned whole words")
                self.check(address, size)
            self.preloads.extend(ranges)

        def memory(self, address, size):
            self.check(address, size); self.reads.append((address, size))
            return bytes(self.data.get(address + index, 0) for index in range(size))

        def word(self, address):
            return struct.unpack("<I", self.memory(address, 4))[0]

    def fixture(self):
        header, records = joined_fixture()
        legacy = header["grid_initialization"]
        evidence = {phase: FLEET.capture_cold_grid_boundary(self.Reader(legacy[phase]), legacy[phase]["boundary"])
                    for phase in ("pre", "post")}
        return header, records, evidence

    def test_fixed_entry_return_capture_accepts_stale_actor_and_descriptor_bytes_without_dereferences(self):
        header, _, evidence = self.fixture()
        FLEET.validate_cold_grid_initialization(evidence, header["grid_initialization"])
        for phase in ("pre", "post"):
            snapshot = evidence[phase]
            self.assertEqual(len(snapshot["actors"]), 16)
            self.assertEqual(bytes.fromhex(snapshot["actors"][15]["body_hex"])[0], 0xa5)
            self.assertEqual(bytes.fromhex(snapshot["globals"]["route_descriptor_hex"])[:4], struct.pack("<I", 0xdeadbeef))
            self.assertEqual(snapshot["crc32"], {name: f"{zlib.crc32(raw):08x}" for name, raw in FLEET.cold_grid_boundary_bytes(snapshot).items()})

    def test_invalid_config_pointer_rejects_before_any_config_or_pointer_target_read(self):
        scalar = joined_fixture()[0]["grid_initialization"]["pre"]
        for pointer in (0, 0x700001, 0xffffff00):
            reader = self.Reader(scalar, pointer)
            with self.assertRaisesRegex(ValueError, "CONFIG pointer"):
                FLEET.capture_cold_grid_boundary(reader, "460170-entry")
            self.assertEqual(reader.reads, [(0x4b0660, 4)])
        with self.assertRaisesRegex(ValueError, "actual460170"):
            FLEET.capture_cold_grid_boundary(self.Reader(scalar), "448130-entry")

    def test_crc_extent_actor_order_and_missing_phase_fail_closed(self):
        header, _, original = self.fixture()
        for mutate, message in ((lambda e: e.pop("post"), "entry/return"),
                                (lambda e: e["post"]["actors"].reverse(), "Reordered"),
                                (lambda e: e["pre"]["actors"][0].update(body_hex="00"), "cold-grid body"),
                                (lambda e: e["post"]["crc32"].update(pair_matrix="00000000"), "CRC32"),
                                (lambda e: e["post"]["globals"].pop("resolved_ai_hex"), "fixed globals"),
                                (lambda e: e["post"].update(boundary="421aa0-entry"), "callee boundary")):
            evidence = json.loads(json.dumps(original)); mutate(evidence)
            with self.assertRaisesRegex(ValueError, message):
                FLEET.validate_cold_grid_initialization(evidence, header["grid_initialization"])

    def test_noqual_allocation_watch_and_scalar_guards_use_captured_bytes(self):
        header, _, original = self.fixture()
        for block, offset, value, form, message in (("configuration", 0x20, 0, "I", "NoQual"),
                                                    ("world_status", 0x16, 15, "H", "sixteen"),
                                                    ("world_status", 0x18, 1, "I", "ordinary local"),
                                                    ("watch_demo", 0, 1, "I", "ordinary local"),
                                                    ("local_actor_slots", 0, 16, "B", "ordinary local"),
                                                    ("rng", 0, 8, "I", "RNG/clock"),
                                                    ("race_lifecycle", 8, 1, "I", "RNG/clock")):
            evidence = json.loads(json.dumps(original)); snapshot = evidence["pre"]
            raw = bytearray(FLEET.cold_grid_boundary_bytes(snapshot)[block]); struct.pack_into("<" + form, raw, offset, value)
            if block == "configuration": snapshot["configuration_hex"] = raw.hex()
            else: snapshot["globals"][block + "_hex"] = raw.hex()
            snapshot["crc32"][block] = f"{zlib.crc32(raw):08x}"
            with self.assertRaisesRegex(ValueError, message):
                FLEET.validate_cold_grid_initialization(evidence, header["grid_initialization"])

    def test_optional_header_roundtrips_and_return_evidence_never_changes_existing_replay_inputs(self):
        header, records, evidence = self.fixture()
        write_trace(self.path, header, records)
        old_header, parsed = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_binary(binary, old_header, parsed, compare_joined_ai=True)
        before = binary.read_bytes(); legacy = json.dumps(header["grid_initialization"])
        header.update(cold_grid_initialization_manifest=FLEET.COLD_GRID_INIT_MANIFEST, cold_grid_initialization=evidence)
        write_trace(self.path, header, records)
        enriched, parsed = FLEET.read_trace(self.path)
        self.assertEqual(json.dumps(enriched["grid_initialization"]), legacy)
        FLEET.write_binary(binary, enriched, parsed, compare_joined_ai=True)
        self.assertEqual(before, binary.read_bytes())
        #A different valid raw return changes evidence only, never inputs.
        evidence["post"]["actors"][0]["body_hex"] = bytes(0x53c).hex()
        evidence["post"]["crc32"]["body"] = f"{zlib.crc32(FLEET.cold_grid_boundary_bytes(evidence['post'])['body']):08x}"
        write_trace(self.path, header, records)
        enriched, parsed = FLEET.read_trace(self.path)
        FLEET.write_binary(binary, enriched, parsed, compare_joined_ai=True)
        self.assertEqual(before, binary.read_bytes())
        header.pop("cold_grid_initialization_manifest"); write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "cold-grid boundary provenance"): FLEET.read_trace(self.path)

    def test_existing_start_call_is_byte_identical_and_has_no_optional_snapshot_reads_by_default(self):
        header, _, evidence = self.fixture(); legacy = header["grid_initialization"]
        class Debugger:
            def command(self, _): return "0x800000"
            def word(self, _): return legacy["return_address"]
            def breakpoint(self, _): return 1
            def stop_at(self, _): pass
            def paused_memory(self): raise AssertionError("Unexpected optional capture read")
        with patch.object(self.trace, "start_call_snapshot", side_effect=[legacy["pre"], legacy["post"]]), \
                patch.object(self.trace.FLEET, "capture_cold_grid_boundary", side_effect=AssertionError("Unexpected cold-grid read")):
            observed = self.trace.capture_start_call(Debugger(), 0x460170)
        self.assertEqual(json.dumps(observed), json.dumps(legacy))
        captured = {}
        with patch.object(self.trace, "start_call_snapshot", side_effect=[legacy["pre"], legacy["post"]]), \
                patch.object(Debugger, "paused_memory", return_value=object()), \
                patch.object(self.trace.FLEET, "capture_cold_grid_boundary", side_effect=[evidence["pre"], evidence["post"]]) as snapshot:
            observed = self.trace.capture_start_call(Debugger(), 0x460170, cold_grid=captured)
        self.assertEqual(json.dumps(observed), json.dumps(legacy)); self.assertEqual(captured, evidence)
        self.assertEqual([call.args[1] for call in snapshot.call_args_list], ["460170-entry", "460170-return"])
        with self.assertRaisesRegex(ValueError, "one actual460170"):
            self.trace.capture_start_call(Debugger(), 0x448130, cold_grid={})

    def test_explicit_capture_flag_requires_joined_ai_before_launch(self):
        with patch.object(sys, "argv", ["trace-reference.py", "--cold-grid-init"]), patch("sys.stderr", new=io.StringIO()) as errors:
            with self.assertRaises(SystemExit) as result: self.trace.main()
        self.assertEqual(result.exception.code, 2); self.assertIn("--cold-grid-init requires --joined-ai", errors.getvalue())
        argv = ["trace-reference.py", "--scope", "fleet", "--ai-state", "--traffic-controls", "--joined-ai", "--cold-grid-init", "--updates", "1"]
        with patch.object(sys, "argv", argv), patch.object(self.trace.Path, "read_bytes", side_effect=RuntimeError("validated capture launch")):
            with self.assertRaisesRegex(RuntimeError, "validated capture launch"): self.trace.main()


class ColdGridSequenceCaptureTests(TraceTestCase):
    trace_filename = "cold-grid.jsonl"
    capture_recorder = True
    Reader = ColdGridInitializationTests.Reader

    def fixture(self):
        header, records, evidence = ColdGridInitializationTests.fixture(self)
        header["grid_initialization"]["return_address"] = 0x4479e5
        events = [{"ordinal": ordinal, "outer_entry": 0x447980, "outer_return_address": caller,
                   "initialization": json.loads(json.dumps(header["grid_initialization"])),
                   "boundaries": json.loads(json.dumps(evidence))} for ordinal, caller in enumerate((0x44fcff, 0x4487d2))]
        endpoint = json.loads(json.dumps(evidence["post"])); endpoint["boundary"] = "421aa0-entry"
        endpoint["actors"] = [{name: value for name, value in actor.items() if name in ("actor", "body_hex", "dynamics_hex", "gameplay_hex")}
                              for actor in records[0]["pre"]["actors"]]
        endpoint["globals"]["rng_hex"] = struct.pack("<I", records[0]["pre"]["rng"]).hex()
        ColdGridFactoryTests.refresh(endpoint)
        sequence = {"events": events, "first_pre": {"caller_return_address": 0x4488a1, "snapshot": endpoint}}
        header.update(cold_grid_initialization_manifest=FLEET.COLD_GRID_INIT_MANIFEST, cold_grid_initialization=evidence,
                      cold_grid_sequence_manifest=FLEET.COLD_GRID_SEQUENCE_MANIFEST, cold_grid_sequence=sequence)
        return header, records

    def test_two_actual_calls_keep_first_legacy_and_reject_missing_extra_or_wrong_callers(self):
        header, _ = self.fixture(); sequence = header["cold_grid_sequence"]
        validate = lambda value: FLEET.validate_cold_grid_sequence(value, header["grid_initialization"], header["cold_grid_initialization"])
        validate(sequence)
        for count in (0, 1, 3):
            changed = json.loads(json.dumps(sequence)); changed["events"] = (changed["events"] * 3)[:count]
            with self.assertRaisesRegex(ValueError, "exactly two"): validate(changed)
        for key, value in (("ordinal", 0), ("outer_return_address", 0x44fcff), ("outer_entry", 0x460170)):
            changed = json.loads(json.dumps(sequence)); changed["events"][1][key] = value
            with self.assertRaisesRegex(ValueError, "caller/ordinal"): validate(changed)
        changed = json.loads(json.dumps(sequence)); changed["events"][1]["initialization"]["return_address"] = 0x447de5
        with self.assertRaisesRegex(ValueError, "caller/ordinal"): validate(changed)
        changed = json.loads(json.dumps(sequence)); changed["first_pre"]["caller_return_address"] = 0x448434
        with self.assertRaisesRegex(ValueError, "actual first421aa0"): validate(changed)
        changed = json.loads(json.dumps(sequence)); changed["events"][0]["initialization"]["pre"]["release_counter"] = 1
        with self.assertRaises(ValueError): validate(changed)

    def test_sequence_roundtrip_preserves_existing_export_and_checks_endpoint_integrity(self):
        header, records = self.fixture(); sequence = header.pop("cold_grid_sequence"); manifest = header.pop("cold_grid_sequence_manifest")
        write_trace(self.path, header, records); old, parsed = FLEET.read_trace(self.path)
        path = self.path.with_suffix(".bin"); FLEET.write_binary(path, old, parsed, compare_joined_ai=True); before = path.read_bytes()
        legacy = json.dumps(header["grid_initialization"]); first = json.dumps(header["cold_grid_initialization"])
        header.update(cold_grid_sequence=sequence, cold_grid_sequence_manifest=manifest)
        write_trace(self.path, header, records); enriched, parsed = FLEET.read_trace(self.path)
        FLEET.write_binary(path, enriched, parsed, compare_joined_ai=True)
        self.assertEqual(before, path.read_bytes()); self.assertEqual(legacy, json.dumps(enriched["grid_initialization"]))
        self.assertEqual(first, json.dumps(enriched["cold_grid_initialization"]))
        endpoint = sequence["first_pre"]["snapshot"]; endpoint["actors"][3]["body_hex"] = bytes(0x53c).hex()
        with self.assertRaisesRegex(ValueError, "first-PRE CRC32"): FLEET.validate_cold_grid_sequence(sequence, header["grid_initialization"], header["cold_grid_initialization"])
        ColdGridFactoryTests.refresh(endpoint)
        with self.assertRaisesRegex(ValueError, "first recorded PRE"): FLEET.validate_cold_grid_sequence(sequence, header["grid_initialization"], header["cold_grid_initialization"], records[0]["pre"])
        header.pop("cold_grid_sequence_manifest"); write_trace(self.path, header, records)
        with self.assertRaisesRegex(ValueError, "sequence provenance"): FLEET.read_trace(self.path)

    def test_recorder_records_each_outer_caller_and_never_overwrites_first_event(self):
        header, _ = self.fixture(); events = []
        with patch.object(self.trace, "capture_start_call", side_effect=lambda debugger, entry, cold_grid: (cold_grid.update(header["cold_grid_initialization"]) or json.loads(json.dumps(header["grid_initialization"])))) as capture:
            first = self.trace.capture_cold_grid_sequence_call(object(), events, 0x44fcff)
            second = self.trace.capture_cold_grid_sequence_call(object(), events, 0x4487d2)
        self.assertEqual(capture.call_count, 2); self.assertIs(events[0], first); self.assertIs(events[1], second)
        self.assertIsNot(first["boundaries"], second["boundaries"])
        for outer in (None, 0x4487d2):
            with self.assertRaisesRegex(ValueError, "extra initializer"): self.trace.capture_cold_grid_sequence_call(object(), events, outer)
        with self.assertRaisesRegex(ValueError, "unexpected actual447980"): self.trace.capture_cold_grid_sequence_call(object(), [], 0x4487d2)

    def test_sequence_flag_requires_explicit_cold_grid_and_joined_before_launch(self):
        with patch.object(sys, "argv", ["trace-reference.py", "--cold-grid-sequence"]), patch("sys.stderr", new=io.StringIO()) as errors:
            with self.assertRaises(SystemExit) as result: self.trace.main()
        self.assertEqual(result.exception.code, 2); self.assertIn("--cold-grid-sequence requires", errors.getvalue())
        argv = ["trace-reference.py", "--scope", "fleet", "--ai-state", "--traffic-controls", "--joined-ai", "--cold-grid-init", "--cold-grid-sequence", "--updates", "1"]
        with patch.object(sys, "argv", argv), patch.object(self.trace.Path, "read_bytes", side_effect=RuntimeError("validated sequence launch")):
            with self.assertRaisesRegex(RuntimeError, "validated sequence launch"): self.trace.main()
        #Combined hooks are installed only after the startup hooks have been
        #removed at firstPRE. Its implied joined flags satisfy this guard.
        combined = ["trace-reference.py", "--scope", "fleet", "--combined-rng", "--cold-grid-init", "--cold-grid-sequence", "--updates", "64"]
        with patch.object(sys, "argv", combined), patch.object(self.trace.Path, "read_bytes", side_effect=RuntimeError("validated combined sequence launch")):
            with self.assertRaisesRegex(RuntimeError, "validated combined sequence launch"): self.trace.main()
        with patch.object(sys, "argv", ["trace-reference.py", "--scope", "fleet", "--combined-rng", "--cold-grid-sequence"]), patch("sys.stderr", new=io.StringIO()) as errors:
            with self.assertRaises(SystemExit) as result: self.trace.main()
        self.assertEqual(result.exception.code, 2); self.assertIn("--cold-grid-sequence requires --cold-grid-init", errors.getvalue())


class ColdGridFactoryTests(TraceTestCase):
    trace_filename = "cold-grid.jsonl"
    capture_recorder = True
    Reader = ColdGridInitializationTests.Reader

    def fixture(self):
        header, records, evidence = ColdGridInitializationTests.fixture(self)
        for phase in ("pre", "post"):
            snapshot = evidence[phase]
            config = bytearray.fromhex(snapshot["configuration_hex"])
            for offset, value in ((0xc, 1), (0x34, 1), (0x1c, 0), (0xdc, 7), (0xe0, 2), (0x100, 2)):
                struct.pack_into("<I", config, offset, value)
            config[0xb0:0xc0] = b"PLAYER 1\0".ljust(16, b"\0")
            struct.pack_into("<16h", config, 0x5fe, *range(-8, 8))
            if phase == "post": struct.pack_into("<h", config, 0x60c, 0)  #Pole actor7 increments entry−1.
            snapshot["configuration_hex"] = config.hex()
            world = bytearray.fromhex(snapshot["globals"]["world_status_hex"])
            struct.pack_into("<H", world, 0x1c, 1); snapshot["globals"]["world_status_hex"] = world.hex()
            for actor, state in enumerate(snapshot["actors"]):
                dynamics = bytearray.fromhex(state["dynamics_hex"])
                for _, offset in FLEET.COLD_GRID_INHERITED_CONTROLS: struct.pack_into("<i", dynamics, offset, 0)
                state["dynamics_hex"] = dynamics.hex()
                gameplay = bytearray.fromhex(state["gameplay_hex"]); gameplay[0x8d] = 0
                if actor == 14: struct.pack_into("<4I", gameplay, 0x60, 0, 0, 0, 2)
                state["gameplay_hex"] = gameplay.hex()
            self.refresh(snapshot)
        header.update(cold_grid_initialization_manifest=FLEET.COLD_GRID_INIT_MANIFEST, cold_grid_initialization=evidence)
        return header, records

    @staticmethod
    def refresh(snapshot):
        snapshot["crc32"] = {name: f"{zlib.crc32(raw):08x}" for name, raw in FLEET.cold_grid_boundary_bytes(snapshot).items()}

    def binary(self, header):
        path = self.path.with_suffix(".cold.bin"); FLEET.write_cold_grid_binary(path, header)
        return path.read_bytes()

    def sidecar(self, header):
        rows = list(FLEET.cold_grid_expected(header)); path = self.path.with_suffix(".cold.csv")
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["group", "actor", "field", "value"]); writer.writeheader(); writer.writerows(rows)
        return path, rows

    def test_native_csv_field_contract_matches_python_order_width_and_count(self):
        import re
        native = (ROOT / "tests/simulation/fleet_reference/startup.cpp").read_text()
        table = re.search(r"constexpr std::array<ActorField, (\d+)> fields(.*?);", native, re.S)
        self.assertIsNotNone(table)
        entries = re.findall(r'\{"(\w+)", "(\w+)", (\d+), (0x[0-9a-f]+), \'(.)\'\}', table[2])
        blocks = {"body": 0, "dynamics": 1, "gameplay": 2}
        expected = [(group, name, blocks[block], offset, form) for group, name, block, offset, form in FLEET.COLD_GRID_ACTOR_FIELDS]
        actual = [(group, name, int(block), int(offset, 16), form) for group, name, block, offset, form in entries]
        self.assertEqual(int(table[1]), len(expected))
        self.assertEqual(actual, expected)

    def test_binary_contains_only_typed_entry_configuration_name_score_shorts_and_seed(self):
        header, _ = self.fixture(); raw = self.binary(header)
        self.assertEqual(len(raw), 82)
        values = FLEET.COLD_GRID_BINARY.unpack(raw)
        self.assertEqual(values[:9], (b"TOCACGIN", 1, 0, 7, 1, 1, 0, 2, 7))
        self.assertEqual(values[9:25], tuple(range(-8, 8))); self.assertEqual(values[25], b"PLAYER 1\0\0")
        entry = header["cold_grid_initialization"]["pre"]
        config = bytearray.fromhex(entry["configuration_hex"]); struct.pack_into("<h", config, 0x60c, -321)
        entry["configuration_hex"] = config.hex(); self.refresh(entry)
        changed = self.binary(header)
        self.assertNotEqual(raw, changed); self.assertEqual(FLEET.COLD_GRID_BINARY.unpack(changed)[16], -321)

    def test_ordinary_difficulty_uses_config34_not_championship_round0c(self):
        #420460 selects ordinary AIBIN banks24..26 from34;444670 uses
        #the same field for444730's ordinary qualification time bounds.
        header, _ = self.fixture(); before = self.binary(header)
        entry = header["cold_grid_initialization"]["pre"]
        config = bytearray.fromhex(entry["configuration_hex"]); struct.pack_into("<I", config, 0xc, 24)
        entry["configuration_hex"] = config.hex(); self.refresh(entry)
        self.assertEqual(before, self.binary(header))
        for difficulty in range(3):
            struct.pack_into("<I", config, 0x34, difficulty)
            entry["configuration_hex"] = config.hex(); self.refresh(entry)
            self.assertEqual(FLEET.COLD_GRID_BINARY.unpack(self.binary(header))[5], difficulty)
        struct.pack_into("<I", config, 0x34, 3)
        entry["configuration_hex"] = config.hex(); self.refresh(entry)
        with self.assertRaisesRegex(ValueError, "difficulty"): self.binary(header)

    def test_return_first_pre_and_entry_initialized_actor_or_loader_copies_never_enter_binary(self):
        header, records = self.fixture(); before = self.binary(header)
        evidence = header["cold_grid_initialization"]
        for phase in ("pre", "post"):
            snapshot = evidence[phase]
            for actor in snapshot["actors"]:
                actor["body_hex"] = bytes(0x53c).hex(); actor["dynamics_hex"] = bytes(0x338).hex()
                gameplay = bytearray(0x124)
                if actor is snapshot["actors"][14]: struct.pack_into("<4I", gameplay, 0x60, 0, 0, 0, 2)
                actor["gameplay_hex"] = gameplay.hex()
            snapshot["globals"]["resolved_ai_hex"] = bytes(0x540).hex()
            snapshot["globals"]["startline_primary_hex"] = bytes([17] * 396).hex()
            self.refresh(snapshot)
        #Return configuration is expectation-only even when score/name differ.
        post = evidence["post"]; config = bytearray.fromhex(post["configuration_hex"])
        struct.pack_into("<16h", config, 0x5fe, *range(100, 116)); config[0xb0:0xb8] = b"CHANGED!"
        post["configuration_hex"] = config.hex(); self.refresh(post)
        header["configuration_hex"] = "unconsumed first-PRE configuration"
        header["initial_configuration"] = {"track_id": 8, "car_id": 1, "weather": 5}
        records[0]["pre"]["rng"] = 0xdeadbeef
        self.assertEqual(before, self.binary(header))

    def test_unsupported_entry_configuration_binding_name_and_missing_evidence_fail_closed(self):
        header, _ = self.fixture()
        for offset, value, message in ((8, 1, "Showdown"), (0x58, 1, "Mayhem"), (0x94, 1, "Impossible"),
                                       (0x5c, 1, "low gravity"), (0xdc, 8, "standard car"), (0xe0, 0, "transmission"),
                                       (0x100, 0, "steering"), (0x5f8, 1, "state1 permutation")):
            changed = json.loads(json.dumps(header)); entry = changed["cold_grid_initialization"]["pre"]
            config = bytearray.fromhex(entry["configuration_hex"]); struct.pack_into("<I", config, offset, value)
            entry["configuration_hex"] = config.hex(); self.refresh(entry)
            with self.assertRaisesRegex(ValueError, message): self.binary(changed)
        changed = json.loads(json.dumps(header)); entry = changed["cold_grid_initialization"]["pre"]
        config = bytearray.fromhex(entry["configuration_hex"]); config[0xb0:0xc0] = b"LONGERNAME\0".ljust(16, b"\0")
        entry["configuration_hex"] = config.hex(); self.refresh(entry)
        with self.assertRaisesRegex(ValueError, "player name"): self.binary(changed)
        header.pop("cold_grid_initialization_manifest")
        with self.assertRaisesRegex(ValueError, "actual --cold-grid-init"): self.binary(header)

    def test_inherited_controls_require_cold_entry_without_becoming_generated_outputs(self):
        header, _ = self.fixture(); before = self.binary(header)
        for name, offset in FLEET.COLD_GRID_INHERITED_CONTROLS:
            changed = json.loads(json.dumps(header)); entry = changed["cold_grid_initialization"]["pre"]
            dynamics = bytearray.fromhex(entry["actors"][3]["dynamics_hex"]); struct.pack_into("<i", dynamics, offset, 1)
            entry["actors"][3]["dynamics_hex"] = dynamics.hex(); self.refresh(entry)
            with self.assertRaisesRegex(ValueError, "inherited controls require zero"): self.binary(changed)
        for index in range(4):
            changed = json.loads(json.dumps(header)); entry = changed["cold_grid_initialization"]["pre"]
            gameplay = bytearray.fromhex(entry["actors"][14]["gameplay_hex"]); struct.pack_into("<I", gameplay, 0x60 + index * 4, 7)
            entry["actors"][14]["gameplay_hex"] = gameplay.hex(); self.refresh(entry)
            with self.assertRaisesRegex(ValueError, "saved controller modes"): self.binary(changed)
        path, rows = self.sidecar(header); report = FLEET.compare_cold_grid(header, path)
        self.assertEqual(len(report["inherited_control_context"]["source_observations"]), 80)
        self.assertFalse(report["inherited_control_context"]["included_in_compared_values"])
        self.assertFalse({name for name, _ in FLEET.COLD_GRID_INHERITED_CONTROLS} & {row["field"] for row in rows})
        self.assertIn("published_brake", {row["field"] for row in rows})
        post = header["cold_grid_initialization"]["post"]
        dynamics = bytearray.fromhex(post["actors"][3]["dynamics_hex"]); struct.pack_into("<i", dynamics, 0x144, 1)
        post["actors"][3]["dynamics_hex"] = dynamics.hex(); self.refresh(post)
        self.assertEqual(before, self.binary(header))
        with self.assertRaisesRegex(ValueError, "inherited control changed"): FLEET.compare_cold_grid(header, path)

    def test_classification_requires_zero_entry_driver_binding_before_identity_publication(self):
        header, _ = self.fixture(); before = self.binary(header)
        post = header["cold_grid_initialization"]["post"]
        for actor, snapshot in enumerate(post["actors"]):
            gameplay = bytearray.fromhex(snapshot["gameplay_hex"]); gameplay[0x8d] = actor
            snapshot["gameplay_hex"] = gameplay.hex()
        self.refresh(post)
        self.assertEqual(before, self.binary(header))
        for actor in (0, 7, 15):
            changed = json.loads(json.dumps(header)); entry = changed["cold_grid_initialization"]["pre"]
            gameplay = bytearray.fromhex(entry["actors"][actor]["gameplay_hex"]); gameplay[0x8d] = 1
            entry["actors"][actor]["gameplay_hex"] = gameplay.hex(); self.refresh(entry)
            with self.assertRaisesRegex(ValueError, "pristine zero entry driver binding"): self.binary(changed)

    def test_keyboard_profiles_and_cloudy_loader_scope_match_runtime_contract(self):
        header, _ = self.fixture(); entry = header["cold_grid_initialization"]["pre"]
        for profile in (1, 3):
            config = bytearray.fromhex(entry["configuration_hex"]); struct.pack_into("<I", config, 0x100, profile)
            entry["configuration_hex"] = config.hex(); self.refresh(entry)
            self.assertEqual(FLEET.COLD_GRID_BINARY.unpack(self.binary(header))[7], profile)
        config = bytearray.fromhex(entry["configuration_hex"]); struct.pack_into("<I", config, 0x1c, 1)
        entry["configuration_hex"] = config.hex()
        world = bytearray.fromhex(entry["globals"]["world_status_hex"]); struct.pack_into("<H", world, 0x1c, 2)
        entry["globals"]["world_status_hex"] = world.hex(); self.refresh(entry)
        with self.assertRaisesRegex(ValueError, "independently observed horizon"): self.binary(header)

    def test_return_fields_are_distinct_from_entry_and_only_actual_short_and_strcpy_extent_are_compared(self):
        header, _ = self.fixture(); post = header["cold_grid_initialization"]["post"]
        actor = post["actors"][7]; game = bytearray.fromhex(actor["gameplay_hex"])
        struct.pack_into("<h", game, 0xbc, 12); game[0xbe:0xc0] = b"\x55\xaa"
        actor["gameplay_hex"] = game.hex(); self.refresh(post)
        rows = list(FLEET.cold_grid_expected(header))
        self.assertEqual(next(row["value"] for row in rows if (row["group"], row["actor"], row["field"]) == ("rank_ring", 7, "rank")), 12)
        self.assertEqual(sum(row["group"] == "local_name" for row in rows), len(b"PLAYER 1") + 1)
        self.assertEqual([row["actor"] for row in rows if row["field"] == "queued_shift"], [14])
        self.assertNotIn("previous_engine_speed", {row["field"] for row in rows})
        self.assertNotIn("pending_gear", {row["field"] for row in rows})
        self.assertNotIn("headlights", {row["field"] for row in rows})
        self.assertNotIn("qpc_divisor", {row["field"] for row in rows})
        #Unwritten upper rank/name-tail bytes do not become generated claims.
        game[0xbe:0xc0] = b"\x11\x22"; actor["gameplay_hex"] = game.hex()
        ai = bytearray.fromhex(post["globals"]["resolved_ai_hex"]); ai[14 * 40 + 12] ^= 1
        post["globals"]["resolved_ai_hex"] = ai.hex(); self.refresh(post)
        self.assertEqual(rows, list(FLEET.cold_grid_expected(header)))

    def test_grouped_return_comparison_detects_rank_pose_controls_score_name_and_rng_corruption(self):
        header, _ = self.fixture(); path, rows = self.sidecar(header)
        report = FLEET.compare_cold_grid(header, path)
        self.assertTrue(report["parity"]); self.assertEqual(report["first_pre_substitutions"], 0)
        self.assertEqual(report["return_inputs"], 0); self.assertFalse(report["inherited_loader_context"]["native_generation_claim"])
        for group, field in (("rank_ring", "rank"), ("pose_support", "position_x"), ("modes_controls", "clutch"),
                              ("configuration_points", "points"), ("local_name", "name_0"), ("rng", "final_seed")):
            changed = [dict(row) for row in rows]; selected = next(row for row in changed if row["group"] == group and row["field"] == field)
            selected["value"] += 1
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["group", "actor", "field", "value"]); writer.writeheader(); writer.writerows(changed)
            report = FLEET.compare_cold_grid(header, path)
            self.assertFalse(report["parity"]); self.assertEqual(report["mismatch_count"], 1)
            self.assertEqual(report["groups"][group]["mismatch_count"], 1)

    def test_missing_extra_reordered_or_wrong_header_native_rows_fail_closed(self):
        header, _ = self.fixture(); path, _ = self.sidecar(header); lines = path.read_text().splitlines()
        for changed, message in ((lines[:-1], "Missing"), ([*lines, lines[-1]], "Extra"),
                                  ([lines[0], lines[2], lines[1], *lines[3:]], "Reordered"),
                                  ([lines[0].replace("value", "post_value"), *lines[1:]], "header")):
            path.write_text("\n".join(changed) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_cold_grid(header, path)

    def test_cli_selects_one_standalone_initialization_without_update_or_joined_inputs(self):
        header, records = self.fixture(); self.path.write_text("original source initialization")
        expected = {"parity": True, "compared_values": 100, "mismatch_count": 0}
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-cold-grid-init", "--output", str(self.path.parent)]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_cold_grid_binary") as binary, patch.object(CLI.FLEET, "write_binary", side_effect=AssertionError("No joined input")), \
                patch.object(CLI.FLEET, "compare_cold_grid", return_value=expected), \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_count, 1); self.assertEqual(len(execute.call_args.args[0]), 5)
        self.assertEqual(execute.call_args.args[0][1], "--compare-cold-grid-init")
        self.assertIs(binary.call_args.args[1], header)
        report = json.loads((self.path.parent / "fleet-comparison-cold-grid-initialization.json").read_text())
        self.assertIsNone(report["continuous_physics_parity"]); self.assertIn("unrecorded", report["return_to_first_pre_gap"])
        for extra in (["--updates", "1"], ["--compare-dispatch-feedback"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as error: CLI.main()
            self.assertEqual(error.exception.code, 2)


class ColdGridSequenceComparisonTests(TraceTestCase):
    trace_filename = "cold-grid.jsonl"
    capture_recorder = True
    Reader = ColdGridInitializationTests.Reader
    refresh = staticmethod(ColdGridFactoryTests.refresh)

    def fixture(self):
        header, records = ColdGridFactoryTests.fixture(self)
        legacy = header["grid_initialization"]; legacy["return_address"] = 0x4479e5
        first = header["cold_grid_initialization"]
        for index, actor in enumerate(first["post"]["actors"]):
            gameplay = bytearray.fromhex(actor["gameplay_hex"]); gameplay[0x8d] = index
            actor["gameplay_hex"] = gameplay.hex()
        self.refresh(first["post"])
        second = {"pre": json.loads(json.dumps(first["post"])), "post": json.loads(json.dumps(first["post"]))}
        second["pre"]["boundary"] = "460170-entry"
        config = bytearray.fromhex(second["post"]["configuration_hex"])
        points = struct.unpack_from("<h", config, 0x5fe + 15 * 2)[0]; struct.pack_into("<h", config, 0x5fe + 15 * 2, points + 1)
        second["post"]["configuration_hex"] = config.hex(); self.refresh(second["post"])
        second_legacy = {"return_address": 0x4479e5, "pre": {**legacy["post"], "boundary": "460170-entry"}, "post": dict(legacy["post"])}
        endpoint = json.loads(json.dumps(second["post"])); endpoint["boundary"] = "421aa0-entry"
        records[0]["pre"]["actors"] = json.loads(json.dumps(endpoint["actors"]))
        records[0]["pre"]["rng"] = legacy["post"]["rng"]
        sequence = {"events": [{"ordinal": 0, "outer_entry": 0x447980, "outer_return_address": 0x44fcff, "initialization": legacy, "boundaries": first},
                               {"ordinal": 1, "outer_entry": 0x447980, "outer_return_address": 0x4487d2, "initialization": second_legacy, "boundaries": second}],
                    "first_pre": {"caller_return_address": 0x4488a1, "snapshot": endpoint}}
        header.update(cold_grid_sequence_manifest=FLEET.COLD_GRID_SEQUENCE_MANIFEST, cold_grid_sequence=sequence)
        return header, records

    def binary(self, header):
        path = self.path.with_suffix(".start.bin"); FLEET.write_cold_grid_sequence_binary(path, header); return path.read_bytes()

    def sidecar(self, header, records):
        rows = list(FLEET.cold_grid_sequence_expected(header, records)); path = self.path.with_suffix(".start.csv")
        self.write_rows(path, rows)
        return path, rows

    @staticmethod
    def write_rows(path, rows):
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["phase", "group", "actor", "field", "value"]); writer.writeheader(); writer.writerows(rows)

    def test_only_first_typed_entry_enters_binary_and_later_boundaries_are_expectations(self):
        header, records = self.fixture(); before = self.binary(header)
        expected = self.path.with_suffix(".fresh.bin"); FLEET.write_cold_grid_binary(expected, header)
        self.assertEqual(before, expected.read_bytes()); self.assertEqual(len(before), 82)
        events = header["cold_grid_sequence"]["events"]
        for snapshot in (events[0]["boundaries"]["post"], events[1]["boundaries"]["pre"], events[1]["boundaries"]["post"], header["cold_grid_sequence"]["first_pre"]["snapshot"]):
            body = bytearray.fromhex(snapshot["actors"][3]["body_hex"]); body[0xbc] ^= 1
            snapshot["actors"][3]["body_hex"] = body.hex(); self.refresh(snapshot)
        ai = bytearray.fromhex(events[1]["boundaries"]["pre"]["globals"]["resolved_ai_hex"]); ai[0] ^= 1
        events[1]["boundaries"]["pre"]["globals"]["resolved_ai_hex"] = ai.hex(); self.refresh(events[1]["boundaries"]["pre"])
        self.assertEqual(before, self.binary(header))
        self.assertFalse(FLEET.cold_grid_retained_input_continuity(header)["parity"])
        header["cold_grid_sequence"]["events"].pop()
        with self.assertRaisesRegex(ValueError, "exactly two"): self.binary(header)

    def test_separate_returns_and_first_pre_projection_detect_phase_specific_corruption(self):
        header, records = self.fixture(); path, rows = self.sidecar(header, records)
        report = FLEET.compare_cold_grid_sequence(header, records, path)
        self.assertTrue(report["parity"]); self.assertTrue(report["gap_closed_for_named_writer_subset"])
        self.assertEqual(report["compared_values"], 7266)
        self.assertEqual({phase: value["compared_values"] for phase, value in report["phases"].items()}, dict.fromkeys(("first_return", "second_return", "first_pre"), 2422))
        first_expected = list(FLEET.cold_grid_expected(header))
        self.assertEqual([{key: value for key, value in row.items() if key != "phase"} for row in rows if row["phase"] == "first_return"], first_expected)
        for phase, group, field in (("first_return", "rank_ring", "rank"), ("second_return", "configuration_points", "points"),
                                    ("second_return", "rng", "final_seed"), ("first_pre", "pose_support", "basis_00"),
                                    ("first_pre", "clock_contract", "first_sample")):
            changed = [dict(row) for row in rows]
            next(row for row in changed if row["phase"] == phase and row["group"] == group and row["field"] == field)["value"] += 1
            self.write_rows(path, changed); report = FLEET.compare_cold_grid_sequence(header, records, path)
            self.assertFalse(report["parity"]); self.assertFalse(report["gap_closed_for_named_writer_subset"])
            self.assertEqual(report["mismatch_count"], 1); self.assertEqual(report["phases"][phase]["mismatch_count"], 1)

    def test_retained_dependency_continuity_rejects_later_rng_scores_bindings_and_tables_without_importing_them(self):
        header, records = self.fixture(); before = self.binary(header)
        for target in ("rng", "points", "driver", "table", "name", "config", "branch", "ownership", "weather"):
            changed = json.loads(json.dumps(header)); second = changed["cold_grid_sequence"]["events"][1]
            entry = second["boundaries"]["pre"]
            if target == "rng":
                entry["globals"]["rng_hex"] = struct.pack("<I", 99).hex(); second["initialization"]["pre"]["rng"] = 99
            elif target in ("points", "config", "branch"):
                config = bytearray.fromhex(entry["configuration_hex"]); config[{"points": 0x5fe, "config": 0x34, "branch": 0x5f8}[target]] ^= 2
                entry["configuration_hex"] = config.hex()
            elif target in ("ownership", "weather"):
                world = bytearray.fromhex(entry["globals"]["world_status_hex"]); world[0 if target == "ownership" else 0x1c] ^= 1
                entry["globals"]["world_status_hex"] = world.hex()
            elif target == "driver":
                game = bytearray.fromhex(entry["actors"][7]["gameplay_hex"]); game[0x8d] = 3; entry["actors"][7]["gameplay_hex"] = game.hex()
            else:
                ai = bytearray.fromhex(entry["globals"]["resolved_ai_hex"]); ai[0 if target == "table" else 14*40] ^= 1
                entry["globals"]["resolved_ai_hex"] = ai.hex()
            self.refresh(entry)
            self.assertEqual(before, self.binary(changed))
            self.assertFalse(FLEET.cold_grid_retained_input_continuity(changed)["parity"])

    def test_first_pre_inherited_controls_are_observations_excluded_from_generated_count(self):
        header, records = self.fixture(); path, _ = self.sidecar(header, records)
        endpoint = header["cold_grid_sequence"]["first_pre"]["snapshot"]
        dynamics = bytearray.fromhex(endpoint["actors"][3]["dynamics_hex"]); struct.pack_into("<i", dynamics, 0x138, 1)
        endpoint["actors"][3]["dynamics_hex"] = dynamics.hex(); self.refresh(endpoint)
        records[0]["pre"]["actors"][3]["dynamics_hex"] = dynamics.hex()
        report = FLEET.compare_cold_grid_sequence(header, records, path)
        self.assertEqual(report["mismatch_count"], 0); self.assertFalse(report["parity"])
        self.assertFalse(report["gap_closed_for_named_writer_subset"])
        self.assertFalse(report["inherited_context"]["included_in_generated_count"])
        self.assertFalse(report["inherited_context"]["source_control_zero_observations"]["first_pre"])

    def test_phase_csv_requires_exact_order_count_and_header(self):
        header, records = self.fixture(); path, _ = self.sidecar(header, records); lines = path.read_text().splitlines()
        for changed, message in ((lines[:-1], "Missing"), ([*lines, lines[-1]], "Extra"),
                                  ([lines[0], lines[2], lines[1], *lines[3:]], "Reordered"),
                                  ([lines[0].replace("phase", "observed_phase"), *lines[1:]], "header")):
            path.write_text("\n".join(changed) + "\n")
            with self.assertRaisesRegex(ValueError, message): FLEET.compare_cold_grid_sequence(header, records, path)

    def test_cli_uses_single_first_entry_binary_and_new_phase_report_only(self):
        header, records = self.fixture(); self.path.write_text("sequence source")
        expected = {"parity": True, "compared_values": 7266, "mismatch_count": 0}
        argv = ["replay-fleet-reference.py", str(self.path), "--compare-cold-grid-sequence", "--output", str(self.path.parent)]
        with patch.object(sys, "argv", argv), patch.object(CLI.FLEET, "read_trace", return_value=(header, records)), \
                patch.object(CLI.FLEET, "write_cold_grid_sequence_binary") as binary, patch.object(CLI.FLEET, "write_binary", side_effect=AssertionError("No initialworld import")), \
                patch.object(CLI.FLEET, "compare_cold_grid_sequence", return_value=expected), \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as execute, patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(len(execute.call_args.args[0]), 5); self.assertEqual(execute.call_args.args[0][1], "--compare-cold-grid-sequence")
        self.assertIs(binary.call_args.args[1], header)
        report = json.loads((self.path.parent / "fleet-comparison-cold-grid-sequence.json").read_text())
        self.assertIsNone(report["continuous_physics_parity"]); self.assertEqual(report["cold_grid_sequence"], expected)
        for extra in (["--updates", "1"], ["--compare-cold-grid-init"], ["--compare-joined-ai"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as error: CLI.main()
            self.assertEqual(error.exception.code, 2)


class QualifyingStartupTests(TraceTestCase):
    trace_filename = "qualifying-startup.jsonl"
    capture_recorder = True

    def fixture(self):
        class Reader(ColdGridInitializationTests.Reader):
            def __init__(self):
                super().__init__({"session_stage": 1, "rng": 0x12345678, "race_clock": 2000})
                self.allowed += [(at & ~3, ((at + size + 3) & ~3) - (at & ~3)) for at, size in FLEET.QUALIFYING_STARTUP_BLOCKS.values()]
                config = bytearray(self.memory(0x700000, 0x620))
                for at, value in ((0x20, 0), (0xdc, 7), (0xe0, 2), (0x100, 2)):
                    struct.pack_into("<I", config, at, value)
                struct.pack_into("<H", config, 0x5f8, 4)
                config[0xb0:0xb9] = b"PLAYER 1\0"
                self.put(0x700000, config)
                self.put(0x4c5d74, struct.pack("<H", 1))
                self.put(0x4be148, struct.pack("<I", 400000))
                for name in ("body", "dynamics", "gameplay"):
                    self.put(FLEET.LAYOUT[name + "_base"], bytes(16 * FLEET.LAYOUT[name + "_stride"]))
                for actor in range(16):
                    self.put(FLEET.LAYOUT["gameplay_base"] + actor * FLEET.LAYOUT["gameplay_stride"] + 0xbc, struct.pack("<h", actor))
                self.put(0x4ed998, struct.pack("<5I", *([1000] * 5)))
                self.put(0x4eda6c, struct.pack("<I", 2000))
                for at in (0x4ad080, 0x4ad084): self.put(at, struct.pack("<I", 65536))
                self.put(0x52b3f2, struct.pack("<h", -376))
        reader = Reader()
        snapshots = [FLEET.capture_qualifying_startup_boundary(reader, boundary)
            for boundary in ("443f60-entry", "443f60-return", "443f60-entry", "443f60-return", "421aa0-entry")]
        header = {"type": "header", "schema": 1, "scope": "qualifying-startup", "exe_crc32": "8ce56d41",
                  "manifest": copy.deepcopy(FLEET.QUALIFYING_STARTUP_MANIFEST)}
        events = [{"kind": "initializer", "ordinal": 0, "return_address": 0x44fd4f, "pre": snapshots[0], "post": snapshots[1]},
            {"kind": "timer_creation", "return_address": 0x44a470, "creation_clock": 1000},
            {"kind": "initializer", "ordinal": 1, "return_address": 0x447f8b, "pre": snapshots[2], "post": snapshots[3]},
            {"kind": "first_pre", "return_address": 0x4480ba, "snapshot": snapshots[4]}]
        return {"header": header, "events": events}

    def test_init_only_capture_roundtrip_crc_and_distinct_completion(self):
        capture = self.fixture()
        footer = FLEET.write_qualifying_startup_capture(self.path, capture["header"], capture["events"])
        self.assertEqual((footer["type"], footer["updates"]), ("complete-init", 0))
        self.assertEqual(FLEET.read_qualifying_startup(self.path)["events"], capture["events"])
        self.path.write_bytes(self.path.read_bytes().replace(b'"creation_clock": 1000', b'"creation_clock": 1001'))
        with self.assertRaisesRegex(ValueError, "CRC32"): FLEET.read_qualifying_startup(self.path)
        self.assertEqual(FLEET.COLD_GRID_SEQUENCE_MANIFEST["boundary_manifest"], FLEET.COLD_GRID_INIT_MANIFEST)

    def test_rejected_raw_boundaries_are_retained_with_aborted_crc_footer(self):
        capture = self.fixture()
        capture["events"][2]["return_address"] = 0x4487d2
        with self.assertRaisesRegex(ValueError, "initializer caller"):
            FLEET.write_qualifying_startup_capture(self.path, capture["header"], capture["events"])
        lines = self.path.read_bytes().splitlines(keepends=True)
        footer = json.loads(lines[-1])
        self.assertEqual((footer["type"], footer["events"], footer["updates"]), ("aborted-init", 4, 0))
        self.assertEqual(footer["prefix_crc32"], f"{zlib.crc32(b''.join(lines[:-1])):08x}")
        self.assertEqual([json.loads(line) for line in lines[1:-1]], capture["events"])
        self.assertIn("initializer caller", footer["reason"])
        with self.assertRaisesRegex(ValueError, "completion/CRC32"):
            FLEET.read_qualifying_startup(self.path)

    def test_wire_contains_only_typed_entry_and_three_clock_inputs(self):
        capture = self.fixture()
        FLEET.write_qualifying_startup_binary(self.path, capture)
        before = self.path.read_bytes()
        self.assertEqual(len(before), 98)
        self.assertEqual(struct.unpack_from("<8s3I", before), (b"TOCAGRD1", 2, 0x443f60, 0))
        self.assertEqual(struct.unpack_from("<2Ii", before, 86), (1000, 2000, 400000))
        snapshot = capture["events"][0]["post"]
        raw = bytearray.fromhex(snapshot["actors"][0]["body_hex"]); struct.pack_into("<i", raw, 0xbc, 123456)
        snapshot["actors"][0]["body_hex"] = raw.hex()
        snapshot["crc32"] = {name: f"{zlib.crc32(data):08x}" for name, data in FLEET.cold_grid_boundary_bytes(snapshot).items()}
        FLEET.write_qualifying_startup_binary(self.path, capture)
        self.assertEqual(before, self.path.read_bytes())

    def test_cold_bc_callers_and_first_pre_step_are_strict(self):
        for defect in ("binding", "caller", "chronology", "step"):
            capture = self.fixture()
            if defect == "binding":
                snapshot = capture["events"][0]["pre"]
                raw = bytearray.fromhex(snapshot["actors"][2]["gameplay_hex"]); struct.pack_into("<h", raw, 0xbc, 1)
                snapshot["actors"][2]["gameplay_hex"] = raw.hex()
                snapshot["crc32"] = {name: f"{zlib.crc32(data):08x}" for name, data in FLEET.cold_grid_boundary_bytes(snapshot).items()}
            elif defect == "caller": capture["events"][2]["return_address"] = 0x4487d2
            elif defect == "chronology": capture["events"][0], capture["events"][1] = capture["events"][1], capture["events"][0]
            else:
                snapshot = capture["events"][3]["snapshot"]
                snapshot["qualifying_globals"]["extra_update_hex"] = "01000000"
                snapshot["qualifying_crc32"] = {name: f"{zlib.crc32(bytes.fromhex(raw)):08x}" for name, raw in snapshot["qualifying_globals"].items()}
            with self.subTest(defect=defect), self.assertRaises(ValueError): FLEET.qualifying_startup_inputs(capture)

    def test_comparator_preserves_phase_and_field_order_without_pole_or_name_claims(self):
        capture = self.fixture(); rows = list(FLEET.qualifying_startup_expected(capture))
        self.assertNotIn("configuration_points", {row["group"] for row in rows})
        self.assertNotIn("local_name", {row["group"] for row in rows})
        self.assertEqual([row["value"] for row in rows if row["group"] == "camera"], [-376] * 3)
        self.assertEqual(len([row for row in rows if row["group"] == "clock"]), 10)
        def write(values):
            with self.path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["phase", "group", "actor", "field", "value"])
                writer.writeheader(); writer.writerows(values)
        write(rows); self.assertTrue(FLEET.compare_qualifying_startup(capture, self.path)["parity"])
        changed = copy.deepcopy(rows); changed[-1]["value"] += 1; write(changed)
        self.assertEqual(FLEET.compare_qualifying_startup(capture, self.path)["mismatch_count"], 1)
        for values in (rows[:-1], [*rows, rows[-1]], [rows[1], rows[0], *rows[2:]]):
            write(values)
            with self.assertRaises(ValueError): FLEET.compare_qualifying_startup(capture, self.path)

    def test_capture_profile_accepts_before_launch_and_rejects_combined_or_warmup(self):
        class BeforeLaunch(Exception): pass
        argv = ["trace-reference.py", "--qualifying-startup", "--weather", "sunny"]
        with patch.object(sys, "argv", argv), patch.object(self.trace.Path, "read_bytes", side_effect=BeforeLaunch):
            with self.assertRaises(BeforeLaunch): self.trace.main()
        for extra in (["--combined-rng"], ["--warmup-step", "1:Up"], ["--scene-frames"]):
            with patch.object(sys, "argv", [*argv, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit): self.trace.main()

    def test_replay_cli_dispatches_init_only_without_fleet_physics_reader(self):
        capture = self.fixture()
        FLEET.write_qualifying_startup_capture(self.path, capture["header"], capture["events"])
        def run(command, **kwargs):
            with Path(command[-1]).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["phase", "group", "actor", "field", "value"])
                writer.writeheader(); writer.writerows(FLEET.qualifying_startup_expected(capture))
            return subprocess.CompletedProcess(command, 0, "", "")
        with patch.object(sys, "argv", ["replay-fleet-reference.py", str(self.path), "--compare-qualifying-startup"]), \
                patch.object(CLI.subprocess, "run", side_effect=run) as execute, \
                patch.object(CLI.FLEET, "read_trace", side_effect=AssertionError("No physical records")), patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(execute.call_args.args[0][1], "--compare-qualifying-startup")
        report = json.loads(self.path.with_name("fleet-comparison-qualifying-startup.json").read_text())
        self.assertEqual(report["physical_updates"], 0)
        self.assertEqual(report["actor_state_imports"], 0)
        self.assertIsNone(report["autonomous_qualifying_parity"])

    def test_recorder_follows_actual_callers_and_stops_before_first_physics(self):
        from unittest.mock import Mock
        capture = self.fixture()
        snapshots = [capture["events"][0]["pre"], capture["events"][0]["post"],
                     capture["events"][2]["pre"], capture["events"][2]["post"], capture["events"][3]["snapshot"]]
        debugger = Mock()
        debugger.breakpoint.side_effect = lambda pc: pc
        debugger.command.side_effect = lambda command: "0x000003e8" if command == "print $eax" else "0x00800000"
        with patch.object(self.trace.FLEET, "capture_qualifying_startup_boundary", side_effect=snapshots):
            recorder = self.trace.QualifyingStartupRecorder(debugger)
            for pc, caller in ((0x443f60, 0x44fd4f), (0x447a70, 0x44a470), (0x443f60, 0x447f8b)):
                debugger.word.return_value = caller
                self.assertTrue(recorder.intercept(f"Stopped at 0x{pc:08x}"))
            debugger.word.return_value = 0x4480ba
            self.assertEqual(recorder.endpoint(), capture["events"])
            recorder.close()
        self.assertEqual([call.args[0] for call in debugger.stop_at.call_args_list], [0x44fd4f, 0x447a77, 0x447f8b])
