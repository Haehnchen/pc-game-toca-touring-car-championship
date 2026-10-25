"""Fleet reference checks for audio."""

from .support import (
    TraceTestCase,
    CLI,
    FLEET,
    Path,
    copy,
    csv,
    fixture,
    io,
    json,
    patch,
    state,
    struct,
    subprocess,
    sys,
    write_trace,
    zlib,
)


class SkidDispatcherTests(TraceTestCase):
    trace_filename = 'trace.jsonl'

    def fixture(self):
        header, records = fixture(2)
        header["skid_dispatch_manifest"] = FLEET.SKID_MANIFEST
        for index, record in enumerate(records):
            raw = {name + "_hex": bytes(size).hex() for name, (_, size) in FLEET.SKID_BLOCKS.items()}
            raw.update(enabled_hex="01000000", ownership_hex="00" + "ff" * 15,
                       identities_hex=bytes(range(16)).hex(), wheel_kinds_hex=bytes(64).hex(),
                       events_hex=struct.pack("<64h", *([-1] * 64)).hex())
            before = {"boundary": "44c540-entry", "rng": 0, **raw}
            after = {**before, "boundary": "44c540-return"}
            calls = []
            if index == 0:
                before["events_hex"] = after["events_hex"] = struct.pack("<64h", 7, 8, *([-1] * 62)).hex()
                after.update(rng=505908858, mask_hex="03" + "00" * 15)
                calls = [{"actor": 0, "identity": 0, "wheel": 0, "kind": 7, "wheel_kind": 0, "strength": 0,
                          "pre": {"rng": 0, "mask": 0, "reset": 0}, "post": {"rng": 2531011, "mask": 1, "reset": 0},
                          "sample": {"pc": 0x424eb2, "member": 41, "restart": 1, "fade": 1}},
                         {"actor": 0, "identity": 0, "wheel": 1, "kind": 8, "wheel_kind": 0, "strength": 0,
                          "pre": {"rng": 2531011, "mask": 1, "reset": 0}, "post": {"rng": 505908858, "mask": 3, "reset": 0},
                          "sample": {"pc": 0x424eb2, "member": 42, "restart": 0, "fade": 0}}]
            record["ground"]["rng"] = before["rng"]
            record["post"]["rng"] = after["rng"]
            record["skid_dispatch"] = {"return_address": 0x446b24, "pre": before, "post": after, "calls": calls}
        return header, records

    def sidecars(self, records):
        prefix = self.path.with_suffix("")
        with Path(str(prefix) + ".skid-dispatch.csv").open("w", newline="") as target:
            writer = csv.writer(target)
            writer.writerow(["id", "actor", "mask", "reset", "rng"])
            for record in records:
                after = record["skid_dispatch"]["post"]
                masks = bytes.fromhex(after["mask_hex"])
                reset = struct.unpack("<16I", bytes.fromhex(after["reset_hex"]))
                for actor in range(16):
                    writer.writerow([record["id"], actor, masks[actor], reset[actor], after["rng"]])
        with Path(str(prefix) + ".skid-events.csv").open("w", newline="") as target:
            writer = csv.writer(target)
            writer.writerow(["id", "actor", "wheel", "kind", "sample", "dispatch", "restart", "fade", "rng", "mask", "reset"])
            writer.writerow([0, 0, 0, 7, 41, 2, 1, 1, 2531011, 1, 0])
            writer.writerow([0, 0, 1, 8, 42, 2, 0, 0, 505908858, 3, 0])
        return prefix

    def test_positive_sample_calls_and_empty_update_have_complete_source_comparison(self):
        header, records = self.fixture()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_skid_binary(binary, header, records)
        self.assertEqual(struct.unpack_from("<8sII", binary.read_bytes()), (b"TOCASKID", 1, 2))
        self.assertEqual(len(binary.read_bytes()), 16 + 2 * 592)
        report = FLEET.compare_skid_dispatch(header, records, self.sidecars(records))
        self.assertTrue(report["parity"])
        self.assertEqual(report["compared_updates"], 2)
        self.assertEqual(report["coverage"]["draws"], 2)
        self.assertEqual(report["coverage"]["sample_counts"], {"41": 1, "42": 1})
        self.assertTrue(report["observed_ground_to_post_rng_gap_explained"])
        self.assertIsNone(report["physical_parity"])

    def test_targeted_local_finish_keeps_empty_skid_diagnostic_and_checks_numeric_output(self):
        from .retained_session import RetainedLocalSessionTests

        header, records = RetainedLocalSessionTests.fixture(self)
        header["skid_dispatch_manifest"] = FLEET.SKID_MANIFEST
        _, skid_records = self.fixture()
        for record in records:
            record["skid_dispatch"] = copy.deepcopy(skid_records[1]["skid_dispatch"])
            record["ground"]["rng"] = record["post"]["rng"] = 0
        self.assertTrue(FLEET.retained_local_session_coverage(header, records)["coverage_sufficient"])
        prefix = self.sidecars(records)
        events = Path(str(prefix) + ".skid-events.csv")
        events.write_text(events.read_text().splitlines()[0] + "\n")
        report = FLEET.compare_skid_dispatch(header, records, prefix)
        self.assertTrue(report["parity"])
        self.assertFalse(report["positive_source_coverage"])
        old = dict(header); old.pop("local_session_lifecycle_manifest")
        with self.assertRaisesRegex(ValueError, "positive source calls"): FLEET.compare_skid_dispatch(old, records, prefix)
        output = Path(str(prefix) + ".skid-dispatch.csv")
        rows = output.read_text().splitlines()
        columns = rows[1].split(","); columns[2] = "1"; rows[1] = ",".join(columns)
        output.write_text("\n".join(rows) + "\n")
        report = FLEET.compare_skid_dispatch(header, records, prefix)
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 1)

    def test_post_expectations_never_enter_binary(self):
        header, records = self.fixture()
        first, second = self.path.with_suffix(".a.bin"), self.path.with_suffix(".b.bin")
        FLEET.write_skid_binary(first, header, records)
        records[0]["post"]["rng"] ^= 1
        records[0]["ground"]["rng"] ^= 1
        records[0]["skid_dispatch"]["calls"][0]["sample"]["member"] = 44
        FLEET.write_skid_binary(second, header, records)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        report = FLEET.compare_skid_dispatch(header, records, self.sidecars(records))
        self.assertFalse(report["parity"])
        self.assertEqual(report["mismatch_count"], 1)
        self.assertFalse(report["observed_ground_to_post_rng_gap_explained"])
        self.assertEqual(len(report["rng_residual_boundaries"]["ground_to_dispatcher"]["nonzero_residual_updates"]), 1)

    def test_missing_source_call_and_zero_positive_scope_are_rejected(self):
        header, records = self.fixture()
        records[0]["skid_dispatch"]["calls"].pop()
        with self.assertRaisesRegex(ValueError, "out-of-order skid"):
            FLEET.write_skid_binary(self.path.with_suffix(".bin"), header, records)
        header, records = self.fixture()
        with self.assertRaisesRegex(ValueError, "positive original"):
            FLEET.write_skid_binary(self.path.with_suffix(".bin"), header, records[1:])

    def test_cli_runs_standalone_source_dispatcher_and_keeps_report_scope(self):
        header, records = self.fixture()
        write_trace(self.path, header, records)
        report = FLEET.compare_skid_dispatch(header, records, self.sidecars(records))
        with patch.object(sys, "argv", ["replay-fleet-reference.py", str(self.path), "--compare-skid-dispatch"]), \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as run, \
                patch.object(CLI.FLEET, "compare_skid_dispatch", return_value=report), patch.object(sys, "stdout", io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(run.call_args.args[0][1], "--compare-skid-dispatch")
        saved = json.loads((self.path.parent / "fleet-comparison-independent-skid-dispatch.json").read_text())
        self.assertEqual(saved["mode"], "independent-skid-dispatch")
        self.assertIsNone(saved["physical_parity"])
        self.assertEqual(saved["source_updates"], 2)


class WheelQueueTests(TraceTestCase):
    trace_filename = 'trace.jsonl'

    def fixture(self):
        header, records = fixture(4)
        header["skid_dispatch_manifest"] = FLEET.SKID_MANIFEST
        header["wheel_queue_manifest"] = FLEET.WHEEL_QUEUE_MANIFEST
        def actor(index):
            body = bytearray(8)
            body[1] = index
            flags = [0] * 8
            if index == 1:
                flags[0], flags[4] = 1, 9
            return {"actor": index, "body_segment_hex": body.hex(), "slip_segment_hex": bytes(32).hex(),
                    "flag_segment_hex": struct.pack("<8i", *flags).hex(), "events_hex": struct.pack("<4h", *([-1] * 4)).hex(),
                    "strengths_hex": bytes(16).hex(), "rng": 0}
        initial = {"boundary": "421aa0-entry", "actors": [actor(i) for i in range(16)], "ownership_hex": "00" + "ff" * 15,
                   "visibility_hex": bytes(16).hex(), "pending_mute_hex": bytes(64).hex(), "render_bypass": 0,
                   "actor_count": 16, "local_players": 1, "rng": 0}
        header["wheel_queue_initial_state"] = initial
        clone = lambda value: json.loads(json.dumps(value))
        def callback(retained, old, current, event, strength):
            pre = clone(retained)
            pre["boundary"] = "446b50-entry"
            pre["pending_mute_hex"] = bytes(64).hex()  #external audio work may clear it between callbacks
            pre["actors"][0]["slip_segment_hex"] = struct.pack("<8i", 0x6000 if current else 0, *([0] * 7)).hex()
            post = clone(pre)
            post["boundary"] = "446b50-return"
            for entry in post["actors"]:
                entry["events_hex"] = struct.pack("<4h", *([-1] * 4)).hex()
            post["actors"][1]["flag_segment_hex"] = bytes(32).hex()
            first = post["actors"][0]
            first["flag_segment_hex"] = struct.pack("<8i", current, 0, 0, 0, old, 0, 0, 0).hex()
            first["events_hex"] = struct.pack("<4h", event, 9, 9, 9).hex()
            first["strengths_hex"] = struct.pack("<4i", strength, 0x8000, 0x8000, 0x8000).hex()
            post["pending_mute_hex"] = struct.pack("<16i", 0, *([1] * 15)).hex()
            producer_pre = clone(pre["actors"][0])
            producer_pre["events_hex"] = struct.pack("<4h", *([-1] * 4)).hex()
            return {"view": 0, "return_address": 0x420429, "pre": pre, "post": post,
                    "producers": [{"actor": 0, "return_address": 0x446c9d, "pre": producer_pre, "post": clone(first)}]}
        a = callback(initial, 0, 1, 7, 29496)
        b = callback(a["post"], 1, 1, 8, 29496)
        c = callback(b["post"], 1, 0, 9, 0x8000)
        retained_states = [initial, b["post"], b["post"], c["post"]]
        callback_lists = [[], [a, b], [], [c]]
        for index, (record, retained, callbacks) in enumerate(zip(records, retained_states, callback_lists)):
            arrays = FLEET.wheel_arrays(retained)
            record["wheel_queue_callbacks"] = callbacks
            for actor_index in range(16):
                for phase in FLEET.BOUNDARIES:
                    dynamics = bytearray.fromhex(record[phase]["actors"][actor_index]["dynamics_hex"])
                    dynamics[0x2c4:0x2e4] = bytes.fromhex(retained["actors"][actor_index]["flag_segment_hex"])
                    record[phase]["actors"][actor_index]["dynamics_hex"] = dynamics.hex()
                    if actor_index == 0:
                        record[phase]["dynamics_hex"] = dynamics.hex()
            before = {"boundary": "44c540-entry", "rng": 0, "enabled_hex": "01000000", "reset_hex": bytes(64).hex(),
                      "surface_hex": bytes(16).hex(), "mask_hex": bytes(16).hex(), "events_hex": arrays["events"].hex(),
                      "strengths_hex": arrays["strengths"].hex(), "ownership_hex": "00" + "ff" * 15,
                      "identities_hex": bytes(range(16)).hex(), "wheel_kinds_hex": bytes(64).hex()}
            after = {**before, "boundary": "44c540-return"}
            calls = []
            if index:
                active = index in (1, 2)
                for wheel in range(4):
                    draw = active
                    call = {"actor": 0, "identity": 0, "wheel": wheel, "kind": 8 if active and wheel == 0 else 9,
                            "wheel_kind": 0, "strength": 29496 if active and wheel == 0 else 0x8000,
                            "pre": {"rng": 0 if wheel == 0 else 2531011 if draw else 0, "mask": 0 if wheel == 0 else int(active), "reset": 0},
                            "post": {"rng": 2531011 if active else 0, "mask": int(active), "reset": 0}}
                    if active and wheel == 0:
                        call["sample"] = {"pc": 0x424eb2, "member": 41, "restart": 0, "fade": 0}
                    calls.append(call)
                if active:
                    after.update(rng=2531011, mask_hex="01" + "00" * 15)
            record["skid_dispatch"] = {"return_address": 0x446b24, "pre": before, "post": after, "calls": calls}
            record["ground"]["rng"] = before["rng"]
            record["post"]["rng"] = after["rng"]
        return header, records

    def sidecars(self, records):
        prefix = self.path.with_suffix("")
        streams = {
            ".wheel-queue.csv": ["id", "callback", "actor", "wheel", "event", "strength", "current", "previous"],
            ".wheel-producers.csv": ["id", "callback", "ordinal", "actor", "wheel", "event", "strength", "current", "previous"],
            ".wheel-call-order.csv": ["id", "callback", "ordinal", "actor"],
            ".wheel-mute.csv": ["id", "callback", "actor", "pending"],
            ".wheel-dispatch-queue.csv": ["id", "actor", "wheel", "event", "strength", "current", "previous"],
            ".skid-dispatch.csv": ["id", "actor", "mask", "reset", "rng"],
            ".skid-events.csv": ["id", "actor", "wheel", "kind", "sample", "dispatch", "restart", "fade", "rng", "mask", "reset"]}
        for suffix, columns in streams.items():
            with Path(str(prefix) + suffix).open("w", newline="") as target:
                writer = csv.writer(target)
                writer.writerow(columns)
                def actor_rows(metadata, actor):
                    events = struct.unpack("<4h", bytes.fromhex(actor["events_hex"]))
                    strengths = struct.unpack("<4i", bytes.fromhex(actor["strengths_hex"]))
                    flags = struct.unpack("<8i", bytes.fromhex(actor["flag_segment_hex"]))
                    for wheel in range(4):
                        writer.writerow([*metadata, actor["actor"], wheel, events[wheel], strengths[wheel], flags[wheel], flags[wheel + 4]])
                for record in records:
                    for ci, callback in enumerate(record["wheel_queue_callbacks"]):
                        if suffix == ".wheel-queue.csv":
                            for actor in callback["post"]["actors"]: actor_rows([record["id"], ci], actor)
                        elif suffix == ".wheel-producers.csv":
                            for ordinal, call in enumerate(callback["producers"]): actor_rows([record["id"], ci, ordinal], call["post"])
                        elif suffix == ".wheel-call-order.csv":
                            for ordinal, call in enumerate(callback["producers"]): writer.writerow([record["id"], ci, ordinal, call["actor"]])
                        elif suffix == ".wheel-mute.csv":
                            for actor, pending in enumerate(struct.unpack("<16i", bytes.fromhex(callback["post"]["pending_mute_hex"]))): writer.writerow([record["id"], ci, actor, pending])
                    transaction = record["skid_dispatch"]
                    if suffix == ".wheel-dispatch-queue.csv":
                        for actor in range(16):
                            actor_rows([record["id"]], {"actor": actor, "events_hex": transaction["pre"]["events_hex"][actor * 16:actor * 16 + 16],
                                                       "strengths_hex": transaction["pre"]["strengths_hex"][actor * 32:actor * 32 + 32],
                                                       "flag_segment_hex": bytes.fromhex(record["pre"]["actors"][actor]["dynamics_hex"])[0x2c4:0x2e4].hex()})
                    elif suffix == ".skid-dispatch.csv":
                        for actor, mask in enumerate(bytes.fromhex(transaction["post"]["mask_hex"])): writer.writerow([record["id"], actor, mask, 0, transaction["post"]["rng"]])
                    elif suffix == ".skid-events.csv":
                        for call in transaction["calls"]:
                            sample = call.get("sample", {}).get("member", -1)
                            writer.writerow([record["id"], call["actor"], call["wheel"], call["kind"], sample, 2 if sample != -1 else 0, 0, 0,
                                             call["post"]["rng"], call["post"]["mask"], call["post"]["reset"]])
        return prefix

    def test_retained_queue_crosses_zero_and_multiple_callbacks_without_later_imports(self):
        header, records = self.fixture()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_wheel_queue_binary(binary, header, records)
        self.assertEqual(struct.unpack_from("<8sII", binary.read_bytes()), (b"TOCAWQUE", 1, 4))
        self.assertEqual(len(binary.read_bytes()), 16 + 896 + 4 * 208 + 3 * 704)
        report = FLEET.compare_wheel_queue(header, records, self.sidecars(records))
        self.assertTrue(report["parity"])
        self.assertEqual(report["coverage"]["callback_count_histogram"], {"2": 1, "0": 1, "1": 1})
        self.assertEqual(report["coverage"]["producer_event_counts"], {"7": 1, "8": 1, "9": 10})
        self.assertEqual(report["later_queue_flag_imports"], 0)
        self.assertEqual(report["skid_dispatch"]["coverage"]["draws"], 2)
        self.assertEqual(report["skid_dispatch"]["inputs"], "derived retained queues; actual dispatcher PRE mask/reset/surface/enabled/RNG only")
        self.assertNotIn("44b690 queue producer", report["skid_dispatch"]["outside_compared_fields"])
        independent = FLEET.compare_skid_dispatch(header, records, self.path.with_suffix(""))
        self.assertIn("one actual PRE queue", independent["inputs"])
        self.assertIn("44b690 queue producer", independent["outside_compared_fields"])

    def test_actual_producer_order_and_skipped_frame_gate_are_required(self):
        header, records = self.fixture()
        records[1]["wheel_queue_callbacks"][0]["pre"]["visibility_hex"] = "0001" + "00" * 14
        with self.assertRaisesRegex(ValueError, "eligibility/order"):
            FLEET.validate_wheel_queue_scope(header, records)
        header, records = self.fixture()
        records[2]["skid_dispatch"]["pre"]["events_hex"] = struct.pack("<64h", *([-1] * 64)).hex()
        with self.assertRaisesRegex(ValueError, "following dispatcher PRE"):
            FLEET.validate_wheel_queue_scope(header, records)

    def test_later_expected_queues_and_flags_do_not_enter_binary(self):
        header, records = self.fixture()
        first, second = self.path.with_suffix(".a.bin"), self.path.with_suffix(".b.bin")
        FLEET.write_wheel_queue_binary(first, header, records)
        callback = records[3]["wheel_queue_callbacks"][0]
        callback["post"]["actors"][0]["strengths_hex"] = struct.pack("<4i", 123, 0x8000, 0x8000, 0x8000).hex()
        records[3]["skid_dispatch"]["pre"]["strengths_hex"] = struct.pack("<i", 123).hex() + records[3]["skid_dispatch"]["pre"]["strengths_hex"][8:]
        FLEET.write_wheel_queue_binary(second, header, records)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_cli_selects_retained_queue_parser_and_full_source_scope(self):
        header, records = self.fixture()
        write_trace(self.path, header, records)
        report = FLEET.compare_wheel_queue(header, records, self.sidecars(records))
        with patch.object(sys, "argv", ["replay-fleet-reference.py", str(self.path), "--compare-wheel-queue"]), \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as run, \
                patch.object(CLI.FLEET, "compare_wheel_queue", return_value=report), patch.object(sys, "stdout", io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(run.call_args.args[0][1], "--compare-wheel-queue")
        saved = json.loads((self.path.parent / "fleet-comparison-retained-wheel-queue.json").read_text())
        self.assertEqual(saved["mode"], "retained-wheel-queue")
        self.assertEqual(saved["source_updates"], 4)
        self.assertEqual(saved["wheel_queue"]["later_queue_flag_imports"], 0)
        self.assertIsNone(saved["physical_parity"])


class WhineTests(TraceTestCase):
    trace_filename = 'trace.jsonl'

    def fixture(self):
        header, records = fixture(4)
        clone = lambda value: json.loads(json.dumps(value))
        def numeric(previous=-1, phase=0, shift=0, ramp=0, throttle=0, volume=17):
            return dict(zip(FLEET.WHINE_STATE, [previous & 0xffffffff, phase, shift, ramp, throttle, throttle, volume]))
        def snapshot(state, rng):
            return {"state": clone(state), "rng": rng, "enabled": 1, "mix": 65536,
                    "local_actors_hex": "00000000", "local_players": 1, "session_state": 9}
        tables = {"header_hex": struct.pack("<3i", 193, 0, 300).hex(), "rates_hex": struct.pack("<194H", *([11025] * 194)).hex(),
                  "gains_hex": bytes([50] * 194).hex(), "offsets_hex": bytes(96).hex()}
        for name in ("header", "rates", "gains", "offsets"):
            tables[name + "_crc32"] = f"{zlib.crc32(bytes.fromhex(tables[name + '_hex'])):08x}"
        initial = numeric()
        header.update(whine_manifest=FLEET.WHINE_MANIFEST, whine_initial_state=snapshot(initial, 0), whine_tables=tables)
        arguments = struct.pack("<6i", 2000, 120, 1, 100, 0, 0).hex()  #state9 overrides physical gear0/speed0
        def callback(before, prefix, after, old_rng, new_rng, volume):
            pre = snapshot(before, old_rng)
            pre["physical"] = [{"player": 0, "actor": 0, "rpm_scale_hex": struct.pack("<i", 65536).hex(),
                                "throttle_hex": struct.pack("<i", 65536).hex(), "engine_speed_hex": bytes(4).hex(),
                                "gear_hex": "00", "speed_hex": bytes(4).hex()}]
            whine = {"return_address": 0x4144d3, "input_hex": arguments, "pre": snapshot(prefix, old_rng), "post": snapshot(after, new_rng),
                     "queries": [{"return_address": 0x4148d9, "playing": 1}], "offset": 0,
                     "commands": [{"kind": "rate", "value": 11025, "return_address": 0x4148f2},
                                  {"kind": "volume", "value": volume, "return_address": 0x41494e}]}
            engine = {"return_address": 0x44a92c, "player": 0, "input_hex": arguments, "post_input_hex": arguments,
                      "pre": snapshot(before, old_rng), "post": snapshot(after, new_rng), "whine": [whine]}
            return {"return_address": 0x44a340, "pre": pre, "post": snapshot(after, new_rng), "engines": [engine]}
        a_state = numeric(1, 26, 2, 10, 20, 0)
        b_state = numeric(1, 27, 1, 20, 40, 0)
        c_state = numeric(1, 28, 0, 30, 60, 15)
        a = callback(initial, numeric(-1, 0, 0, 0, 20, 17), a_state, 0, 2531011, 0)
        b = callback(a_state, numeric(1, 26, 1, 10, 40, 0), b_state, 2531011, 2531011, 0)
        c = callback(b_state, numeric(1, 27, 0, 20, 60, 0), c_state, 2531011, 2531011, 15)
        for record, callbacks, state, rng in zip(records, [[], [a, b], [], [c]], [initial, b_state, b_state, c_state], [0, 2531011, 2531011, 2531011]):
            record.update(whine_callbacks=callbacks, whine_update_pre=snapshot(state, rng), whine_update_post=snapshot(state, rng))
            record["pre"]["rng"] = record["post"]["rng"] = rng
        return header, records

    def sidecars(self, records):
        prefix = self.path.with_suffix("")
        fields = list(FLEET.WHINE_STATE)
        columns = {".whine-inputs.csv": ["id", "callback", "player", "stage", "rpm", "speed", "gear", "throttle", "flag0", "flag1"],
                   ".whine-state.csv": ["id", "callback", "player", "stage", *fields, "rng"],
                   ".whine-offset.csv": ["id", "callback", "player", "offset"], ".whine-commands.csv": ["id", "callback", "player", "ordinal", "kind", "value"],
                   ".whine-updates.csv": ["id", *fields, "rng"]}
        def state_values(snapshot):
            return [value if name == "phase" or value < 0x80000000 else value - 0x100000000 for name, value in snapshot["state"].items()] + [snapshot["rng"]]
        for suffix, names in columns.items():
            with Path(str(prefix) + suffix).open("w", newline="") as target:
                writer = csv.writer(target)
                writer.writerow(names)
                for record in records:
                    if suffix == ".whine-updates.csv": writer.writerow([record["id"], *state_values(record["whine_update_pre"])])
                    for ci, callback in enumerate(record["whine_callbacks"]):
                        for engine in callback["engines"]:
                            whine = engine["whine"][0]
                            metadata = [record["id"], ci, engine["player"]]
                            if suffix in (".whine-inputs.csv", ".whine-state.csv"):
                                stages = [(engine["pre"], engine["input_hex"]), (whine["pre"], whine["input_hex"]),
                                          (whine["post"], whine["input_hex"]), (engine["post"], engine["post_input_hex"])]
                                for stage, (snapshot, inputs) in enumerate(stages):
                                    writer.writerow([*metadata, stage, *(struct.unpack("<6i", bytes.fromhex(inputs)) if suffix == ".whine-inputs.csv" else state_values(snapshot))])
                            elif suffix == ".whine-offset.csv": writer.writerow([*metadata, whine["offset"]])
                            elif suffix == ".whine-commands.csv":
                                for ordinal, command in enumerate(whine["commands"]): writer.writerow([*metadata, ordinal, int(command["kind"] == "volume"), command["value"]])
        return prefix

    def test_actual_caller_numeric_commands_and_whole_intervals_keep_one_initial_state(self):
        header, records = self.fixture()
        write_trace(self.path, header, records)
        header, records = FLEET.read_trace(self.path)
        binary = self.path.with_suffix(".bin")
        FLEET.write_whine_binary(binary, header, records)
        self.assertEqual(struct.unpack_from("<8sII", binary.read_bytes()), (b"TOCAWHIN", 1, 4))
        self.assertEqual(len(binary.read_bytes()), 16 + 28 + 690 + 4 * 12 + 3 * 44)
        report = FLEET.compare_whine(header, records, self.sidecars(records))
        self.assertTrue(report["parity"])
        self.assertTrue(report["positive_command_coverage"])
        self.assertEqual(report["coverage"]["callback_count_histogram"], {"2": 1, "0": 1, "1": 1})
        self.assertEqual(report["coverage"]["rng_advance_calls"], 1)
        self.assertEqual(report["later_numeric_state_imports"], 0)
        self.assertEqual(report["intermediate_rng_imports"], 0)
        self.assertEqual(report["interval_rng_imports"], 3)
        self.assertIsNone(report["whole_process_rng_parity"])

    def test_callee_numeric_post_expectations_never_enter_binary(self):
        header, records = self.fixture()
        first, second = self.path.with_suffix(".a.bin"), self.path.with_suffix(".b.bin")
        FLEET.write_whine_binary(first, header, records)
        callback = records[3]["whine_callbacks"][0]
        for snapshot in (callback["post"], callback["engines"][0]["post"], callback["engines"][0]["whine"][0]["post"],
                         records[3]["whine_update_pre"], records[3]["whine_update_post"]):
            snapshot["state"]["last_volume"] = 16
        FLEET.write_whine_binary(second, header, records)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_missing_whine_call_or_unstable_actual_voice_gate_is_rejected(self):
        header, records = self.fixture()
        records[1]["whine_callbacks"][0]["engines"][0]["whine"] = []
        with self.assertRaisesRegex(ValueError, "engine gate"):
            FLEET.validate_whine_scope(header, records)
        header, records = self.fixture()
        whine = records[1]["whine_callbacks"][0]["engines"][0]["whine"][0]
        whine["queries"].append({"return_address": 0x41497c, "playing": 0})
        with self.assertRaisesRegex(ValueError, "Unstable observed"):
            FLEET.write_whine_binary(self.path.with_suffix(".bin"), header, records)

    def test_cli_selects_whine_phase_parser_and_reports_explicit_scope(self):
        header, records = self.fixture()
        write_trace(self.path, header, records)
        report = FLEET.compare_whine(header, records, self.sidecars(records))
        with patch.object(sys, "argv", ["replay-fleet-reference.py", str(self.path), "--compare-whine-rng"]), \
                patch.object(CLI.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")) as run, \
                patch.object(CLI.FLEET, "compare_whine", return_value=report), patch.object(sys, "stdout", io.StringIO()):
            self.assertEqual(CLI.main(), 0)
        self.assertEqual(run.call_args.args[0][1], "--compare-whine-rng")
        saved = json.loads((self.path.parent / "fleet-comparison-retained-whine-rng.json").read_text())
        self.assertIsNone(saved["physical_parity"])
        self.assertEqual(saved["whine"]["later_numeric_state_imports"], 0)
