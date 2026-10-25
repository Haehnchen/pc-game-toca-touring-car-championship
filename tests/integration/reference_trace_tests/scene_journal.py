"""Ordered scene-input capture and strict wire/expectation boundaries."""

import importlib.util
import json
import subprocess
import sys
import tempfile
import zlib

from .support import TRACE, Mock, Path, call, io, patch, struct, unittest


class SceneJournalTests(unittest.TestCase):
    def inputs(self):
        return {"actor_count": 16, "local_actor": 14, "view": 0, "source_view": 2,
                "camera_branch": 0, "factor_q16": 0, "graphics_detail": 3, "source_mode": 0,
                "track_id": 0, "car_id": 7, "weather": 1,
                "roots": [{"id": actor, "role": "car_parent", "actor": actor,
                           "flags": 1, "local_position": [0., 0., 0.]} for actor in range(16)] +
                         [{"id": 16, "role": "follow_anchor", "flags": 1, "local_position": [0., 0., 0.]}]}

    def event(self, order, kind, inputs, expected=None):
        return {"order": order, "kind": kind, "input": inputs, "expected": expected or {}, "provenance": {}}

    def frame_fixture(self, extra=0):
        bodies = [{"position_q16": [actor, actor + 1, -actor], "mode": actor % 2} for actor in range(16)]
        physical = {"session_state": 9, "pre_body": {"position_q16": [1, 2, 3], "basis_q16": [65536, 0, 0, 0, 65536, 0, 0, 0, 65536]}, "post_bodies": bodies}
        selection = {"camera_world": [0.] * 16, "world_to_view": [1.] * 16, "world_position": [1., 2., 3.],
                     "projection_q16": [0] * 16, "near_q16": 0x4000, "far_q16": 100 * 65536,
                     "ancestor_roots": [16], "all_sectors": False}
        models = {key: value for key, value in self.inputs().items() if key != "roots"}
        models.update(body_positions_q16=[body["position_q16"] for body in bodies], body_modes=[body["mode"] for body in bodies])
        car = {"physical": {"registration": {"sector": 1, "neighbors": [2, 3, 4]}, "render_mask": 1, "manual_mask": 1},
               "lod_manual_masks": [1, 0, 0], "source_car_mask": 1, "source_high_medium_mask": 1, "source_full_body_mask": 1}
        observed = {"retained_refresh_request": 1, "cars": [car for _ in range(16)]}
        events = [self.event(0, "init", self.inputs()),
                  self.event(1, "select", selection, {"ordered_sectors": [1], "detail": [0, 1], "selected_bits_hex": "02" + "00" * 255}),
                  self.event(2, "car_lod", models, observed), self.event(3, "publish", {}, observed)]
        frame = {"order": 0, "input": {"physical": [physical for _ in range(1 + extra)], "qpc_counter": 123456},
                 "expected": {"select_order": 1, "car_lod_order": 2, "publish_order": 3,
                              "timing": {"delta_q16": 65536, "reciprocal_q16": 65536, "extra_update": extra, "physical_count": 1 + extra},
                              "next_clock": {"divisor": 1000, "delta_q16": 65536, "reciprocal_q16": 65536, "extra_update": 0}}, "provenance": {}}
        setup = {key: self.inputs()[key] for key in ("track_id", "car_id", "weather", "graphics_detail", "local_actor")}
        clock = {"divisor": 1000, "delta_q16": 65536, "reciprocal_q16": 65536, "extra_update": 0}
        return setup, clock, frame, events

    def test_frame_crc_links_binary_and_expectations_are_separate(self):
        frames_module = TRACE.SCENE.FRAMES
        setup, clock, frame, events = self.frame_fixture()
        with tempfile.TemporaryDirectory() as temporary:
            path, binary = Path(temporary) / "frames.jsonl", Path(temporary) / "frames.bin"
            writer = frames_module.Writer(path, "8ce56d41")
            writer.start(setup, clock)
            writer.append(frame)
            writer.finish()
            header, frames, footer = frames_module.read_frames(path, {"exe_crc32": "8ce56d41"}, events)
            self.assertFalse(footer["coverage"]["passed"])
            metadata = frames_module.write_frames_binary(binary, header, frames, events)
            before = binary.read_bytes()
            self.assertEqual(before[:8], b"TOCASFR1")
            self.assertEqual(struct.unpack_from("<6I3iI", before, 8), (1, 0, 7, 1, 3, 14, 1000, 65536, 65536, 0))
            self.assertEqual(struct.unpack_from("<3I", before, 460), (1, 1, 123456))
            self.assertEqual(metadata, {"bytes": len(before), "crc32": f"{zlib.crc32(before):08x}"})
            rows = list(frames_module.expected_frame_rows(frames, events))
            self.assertEqual(rows[:4], [(0, "timing", 0, "delta_q16", 65536), (0, "timing", 0, "reciprocal_q16", 65536),
                                       (0, "timing", 0, "extra_update", 0), (0, "timing", 0, "physical_count", 1)])
            self.assertIn((0, "camera", 0, "position_bits", 0x3f800000), rows)
            self.assertIn((0, "root", 0, "neighbor_2", 4), rows)
            self.assertEqual(rows[-3:], [(0, "next_timing", 0, "delta_q16", 65536), (0, "next_timing", 0, "reciprocal_q16", 65536), (0, "next_timing", 0, "extra_update", 0)])
            frames[0]["expected"]["next_clock"]["divisor"] = 2000
            invalid = Path(temporary) / "wrong-divisor.jsonl"
            invalid_writer = frames_module.Writer(invalid, "8ce56d41")
            invalid_writer.start(setup, clock)
            invalid_writer.append(frames[0])
            invalid_writer.finish()
            with self.assertRaisesRegex(ValueError, "divisor changed"): frames_module.read_frames(invalid, {"exe_crc32": "8ce56d41"}, events)
            frames[0]["expected"]["next_clock"]["divisor"] = 1000
            events[1]["input"]["camera_world"][0] = 5.
            frames[0]["expected"]["timing"]["reciprocal_q16"] = 123
            frames_module.write_frames_binary(binary, header, frames, events)
            self.assertEqual(binary.read_bytes(), before)
            raw = path.read_bytes()
            path.write_bytes(raw.replace(b'"qpc_counter":123456', b'"qpc_counter":123457'))
            with self.assertRaisesRegex(ValueError, "CRC/count"): frames_module.read_frames(path, {"exe_crc32": "8ce56d41"}, events)

    def test_frames_reject_imported_camera_schedule_and_partial_lifetime(self):
        module = TRACE.SCENE.FRAMES
        setup, clock, frame, events = self.frame_fixture(extra=1)
        module.validate(frame)
        frame["input"]["camera_world"] = [0.] * 16
        with self.assertRaisesRegex(ValueError, "external counter only"): module.validate(frame)
        frame["input"].pop("camera_world")
        frame["input"]["physical"].pop()
        with self.assertRaisesRegex(ValueError, "extra permit"): module.validate(frame)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "frames.jsonl"
            writer = module.Writer(path, "8ce56d41")
            writer.start(setup, clock)
            writer.finish(pending=True)
            with self.assertRaisesRegex(ValueError, "not complete"): module.read_frames(path, {"exe_crc32": "8ce56d41"}, events)
            self.assertEqual(json.loads(path.read_text().splitlines()[-1])["incomplete_frames"], 1)

    def test_frame_hooks_preserve_physical_stops_and_close_after_qpc(self):
        module = TRACE.SCENE.FRAMES
        setup, clock, frame, events = self.frame_fixture(extra=1)
        with tempfile.TemporaryDirectory() as temporary:
            debugger, journal = Mock(), Mock()
            debugger.breakpoint.side_effect = lambda address: address
            debugger.command.return_value = "0x1000"
            debugger.word.side_effect = lambda address: 0x10000 if address == 0x4b0660 else 123456 if address == 0x4be0cc else 0
            journal.debugger = debugger
            journal.initialized = True
            journal.scope.return_value = self.inputs()
            journal.complete.return_value = True
            current_clock = dict(clock)
            stack = [0x449000]
            def values(fmt, address):
                if address == 0x1000: return stack
                if address == 0x4ad0f0: return [1]
                if address == 0x105f8: return [9]
                if address in module.CLOCK.values(): return [current_clock[next(key for key, value in module.CLOCK.items() if value == address)]]
                return [0] * (struct.calcsize("<" + fmt) // 4)
            journal.values.side_effect = values
            recorder = module.Recorder(journal, Path(temporary) / "frames.jsonl", "8ce56d41")
            self.assertTrue(recorder.intercept("Stopped at0x0044a370"))
            stack[:] = [0x448fff]
            self.assertFalse(recorder.intercept("Stopped at0x00421aa0"))
            self.assertFalse(recorder.intercept("Stopped at0x00448fff"))
            with self.assertRaisesRegex(ValueError, "precedes complete"): recorder.intercept("Stopped at0x00412ab0")
            for kind, order in (("select", 1), ("car_lod", 2), ("publish", 3)): recorder.observe(kind, order)
            self.assertTrue(recorder.ready_to_close())
            self.assertFalse(recorder.ready_to_stop())
            stack[:] = [module.SAMPLE_RETURN]
            self.assertTrue(recorder.intercept("Stopped at0x00412ab0"))
            self.assertTrue(recorder.intercept("Stopped at0x0044a516"))
            recorder.draining = True
            self.assertFalse(recorder.intercept("Stopped at0x00449000"))
            self.assertEqual(len(recorder.writer.frames), 1)
            self.assertEqual(recorder.writer.frames[0]["input"]["qpc_counter"], 123456)
            self.assertIsNone(recorder.primary)
            recorder.close()
            recorder.finish()

    def test_honest_crc_coverage_and_aborted_lifetime(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "scene.jsonl"
            writer = TRACE.SCENE.Writer(path, {"exe_crc32": "8ce56d41"})
            writer.append(self.event(0, "init", self.inputs()))
            writer.finish("actual source exception", pending=1)
            with self.assertRaisesRegex(ValueError, "not complete"): TRACE.SCENE.read(path)
            header, events, footer = TRACE.SCENE.read(path, allow_aborted=True)
            self.assertEqual((header["schema"], len(events), footer["incomplete_calls"]), (1, 1, 1))
            self.assertFalse(footer["coverage"]["passed"])
            raw = path.read_bytes()
            self.assertEqual(footer["prefix_crc32"], f'{zlib.crc32(b"".join(raw.splitlines(keepends=True)[:-1])):08x}')
            path.write_bytes(raw.replace(b'"local_actor":14', b'"local_actor":13'))
            with self.assertRaisesRegex(ValueError, "CRC/count"): TRACE.SCENE.read(path, allow_aborted=True)

    def test_motion_without_positive_output_change_is_not_coverage(self):
        events = [self.event(0, "init", self.inputs())]
        for index in range(2):
            events.append(self.event(len(events), "migrate", {"root": 0, "world_position": [float(index), 0., 0.]}, {"registration": {"sector": 1}}))
            events.append(self.event(len(events), "publish", {}, {"cars": []}))
        self.assertFalse(TRACE.SCENE.coverage(events)["passed"])
        # Geometry/cache or another viewport bit is not a positive local LOD.
        events += [self.event(len(events), "car_lod", {}, {"cars": [{"lod_manual_masks": [1, 0, 0], "unclaimed": 0}]}),
                   self.event(len(events) + 1, "car_lod", {}, {"cars": [{"lod_manual_masks": [3, 0, 0], "unclaimed": 1}]})]
        self.assertFalse(TRACE.SCENE.coverage(events)["passed"])
        events[3]["expected"]["registration"]["sector"] = 2
        self.assertTrue(TRACE.SCENE.coverage(events)["passed"])

    def test_scene_inputs_reject_owned_refresh_and_publication_outputs(self):
        inputs = {key: value for key, value in self.inputs().items() if key != "roots"}
        inputs.update(body_positions_q16=[[0, 0, 0] for _ in range(16)], body_modes=[0] * 16)
        event = self.event(0, "car_lod", inputs, {"retained_refresh_request": 1})
        TRACE.SCENE.validate(event)
        inputs["refresh_request"] = 1
        with self.assertRaisesRegex(ValueError, "not an input"): TRACE.SCENE.validate(event)
        with self.assertRaisesRegex(ValueError, "cannot import"): TRACE.SCENE.validate(self.event(0, "publish", {"root_masks": [1]}))
        inputs.pop("refresh_request")
        inputs["factor_q16"] = 1
        with self.assertRaisesRegex(ValueError, "View2"): TRACE.SCENE.validate(event)

    def test_init_requires_actual_unique_role_and_order_binding(self):
        event = self.event(0, "init", self.inputs())
        TRACE.SCENE.validate(event)
        event["input"]["roots"][0]["actor"] = 1
        with self.assertRaisesRegex(ValueError, "distinct car"): TRACE.SCENE.validate(event)

    def test_nested_migration_keeps_entry_order_without_blocking_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            debugger = Mock()
            debugger.memory_fd = 1
            debugger.breakpoint.side_effect = lambda pc: pc
            stacks = iter([struct.pack("<2I", 0x44ec96, 0), struct.pack("<2I", 0x450004, 0x10000)])
            debugger.command.return_value = "0x1000"
            debugger.memory.side_effect = lambda address, size: next(stacks)
            recorder = TRACE.SCENE.Recorder(debugger, Path(temporary) / "scene.jsonl", "8ce56d41")
            recorder.input = Mock(side_effect=[self.inputs(), {"root": 0, "world_position": [1., 0., 0.]}])
            recorder.expected = Mock(side_effect=[{"registration": {"sector": 1}}, {"roots": []}])
            self.assertTrue(recorder.intercept("Stopped on breakpoint 1 at0x0047de00"))
            self.assertTrue(recorder.intercept("Stopped on breakpoint 2 at0x0047e3c0"))
            self.assertTrue(recorder.intercept("Stopped on breakpoint 3 at0x00450004"))
            self.assertEqual(recorder.writer.events, [])
            self.assertTrue(recorder.intercept("Stopped on breakpoint 4 at0x0044ec96"))
            self.assertEqual([event["kind"] for event in recorder.writer.events], ["init", "migrate"])
            self.assertNotIn(call("cont"), debugger.command.call_args_list)
            recorder.finish()

    def test_untracked_migration_never_becomes_an_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            debugger = Mock()
            debugger.memory_fd = 1
            debugger.breakpoint.side_effect = lambda pc: pc
            debugger.command.return_value = "0x1000"
            debugger.memory.return_value = struct.pack("<2I", 0x450004, 0x20000)
            recorder = TRACE.SCENE.Recorder(debugger, Path(temporary) / "scene.jsonl", "8ce56d41")
            recorder.initialized = True
            self.assertTrue(recorder.intercept("Stopped at0x0047e3c0"))
            self.assertFalse(recorder.pending)
            recorder.finish()

    def test_selection_models_and_one_shot_publish_are_actual_source_boundaries(self):
        selected = {"camera_world": [0.] * 16, "world_to_view": [0.] * 16,
                    "world_position": [0.] * 3, "projection_q16": [0] * 16, "near_q16": 0x4000, "far_q16": 100 * 65536,
                    "ancestor_roots": [16], "all_sectors": False}
        models = {key: value for key, value in self.inputs().items() if key != "roots"}
        models.update(body_positions_q16=[[0] * 3 for _ in range(16)], body_modes=[0] * 16)
        with tempfile.TemporaryDirectory() as temporary:
            debugger = Mock()
            debugger.memory_fd = 1
            debugger.breakpoint.side_effect = lambda pc: pc
            debugger.command.return_value = "0x1000"
            stack = iter([struct.pack("<2I", 0x487235, 0x20000), struct.pack("<2I", 0x420400, 0),
                          struct.pack("<4I", 0x487280, 0x20000, 0x30000, 0)])
            debugger.memory.side_effect = lambda address, size: next(stack)
            recorder = TRACE.SCENE.Recorder(debugger, Path(temporary) / "scene.jsonl", "8ce56d41")
            recorder.writer.append(self.event(0, "init", self.inputs()))
            recorder.input = Mock(side_effect=[selected, models])
            recorder.expected = Mock(return_value={"retained_refresh_request": 1, "cars": []})
            recorder.intercept("Stopped at0x0047d3d0")
            recorder.intercept("Stopped at0x00487235")
            recorder.intercept("Stopped at0x004271d0")
            self.assertNotIn(call(TRACE.SCENE.PUBLISH), debugger.breakpoint.call_args_list)
            recorder.intercept("Stopped at0x00420400")
            self.assertIn(call(TRACE.SCENE.PUBLISH), debugger.breakpoint.call_args_list)
            self.assertTrue(recorder.intercept("Stopped at0x00489069"))
            self.assertFalse(recorder.intercept("Stopped at0x00489069"))
            self.assertEqual([event["kind"] for event in recorder.writer.events], ["init", "select", "car_lod", "publish"])
            self.assertIn(call(f"delete break {TRACE.SCENE.PUBLISH}"), debugger.command.call_args_list)
            recorder.finish()

    def test_scene_cli_rejects_other_profiles_before_source_start(self):
        for profile in ("--scene-journal", "--scene-frames"):
            for flags in ([], ["--scope", "fleet", "--combined-rng"]):
                with self.subTest(profile=profile, flags=flags):
                    result = subprocess.run([sys.executable, str(TRACE.PROJECT / "tools/trace-reference.py"), profile, *flags], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("requires basic fleet capture", result.stderr)
        for flags in (["--qualifying"], ["--session", "time-trial"]):
            result = subprocess.run([sys.executable, str(TRACE.PROJECT / "tools/trace-reference.py"), "--scope", "fleet", "--scene-frames", *flags], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("requires ordinary NoQual", result.stderr)

    def test_source_binding_reads_actual_follow_init_and_world_migration_arguments(self):
        memory = {}
        def put(address, fmt, *values):
            memory.update({address + offset: value for offset, value in enumerate(struct.pack("<" + fmt, *values))})
        def block(address, size):
            return bytes(memory.get(address + offset, 0) for offset in range(size))
        debugger = Mock()
        debugger.memory.side_effect = block
        debugger.word.side_effect = lambda address: struct.unpack("<I", block(address, 4))[0]
        debugger.breakpoint.side_effect = lambda address: address
        config, renderer, manager = 0x10000, 0x20000, 0x30000
        follow, camera = 0x40000, 0x50000
        parents = [0x60000 + actor * 0x200 for actor in range(16)]
        put(0x4b0660, "I", config)
        put(config + 0xac, "I", 1)
        put(config + 0x328, "I", 3)
        put(0x4c5d6e, "H", 16)
        put(0x4d37a0, "H", 14)
        put(0x4d37a1, "B", 15) # Other local-player slot is not part of actor0.
        put(0x4c3a66, "H", 2)
        put(0x4b9b94, "I", renderer)
        put(renderer + 0x200, "I", manager)
        put(0x4c3968, "16I", *parents)
        put(0x4c3638, "I", follow)
        # A different View2 camera-table entry must never become the follow role.
        put(0x4c3478 + 8, "I", camera)
        order = [follow, *reversed(parents)]
        put(manager + 4, "I", order[0])
        put(manager + 0x18, "I", 1000)
        put(manager + 0x120, "H", 0xffff)
        for index, pointer in enumerate(order):
            put(pointer, "I", order[index + 1] if index + 1 < len(order) else 0)
            put(pointer + 0x18, "I", 1000)
            put(pointer + 0x8c, "3f", index + .25, 0., 0.)
            put(pointer + 0xcc, "3f", index + 100.5, 0., 0.)
        put(follow + 8, "I", manager)
        put(camera + 8, "I", follow)
        put(camera + 0x18, "I", 999)
        put(camera + 0x120, "H", 0xffff)
        put(camera + 0xcc, "3f", 10., 20., 30.)
        put(0x5b5060, "f", .25)
        put(0x4f2938, "i", 100 * 65536)
        with tempfile.TemporaryDirectory() as temporary:
            recorder = TRACE.SCENE.Recorder(debugger, Path(temporary) / "scene.jsonl", "8ce56d41")
            inputs = recorder.initialize()
            self.assertEqual(inputs["roots"][0]["role"], "follow_anchor")
            self.assertEqual([root["actor"] for root in inputs["roots"][1:]], list(reversed(range(16))))
            self.assertEqual(inputs["roots"][1]["local_position"], [1.25, 0., 0.])
            migrated = recorder.input("migrate", parents[-1])
            self.assertEqual(migrated, {"root": 1, "world_position": [101.5, 0., 0.]})
            selected = recorder.input("select", camera)
            self.assertEqual(selected["ancestor_roots"], [0])
            self.assertEqual(selected["world_position"], [10., 20., 30.])
            self.assertEqual(selected["far_q16"], 100 * 65536) # Actual special far, not detail3's250.
            self.assertNotIn("sector", selected)
            put(camera + 0x18, "I", 1)
            with self.assertRaisesRegex(ValueError, "ancestry"): recorder.input("select", camera)
            recorder.finish()

    def test_binary_transports_only_actual_inputs_and_consumed_rows_keep_scope(self):
        initial = self.inputs()
        models = {key: value for key, value in initial.items() if key != "roots"}
        models.update(body_positions_q16=[[actor, 0, -actor] for actor in range(16)], body_modes=[0] * 16)
        physical = {"registration": {"sector": 3, "neighbors": [4, 5, 6]}, "render_mask": 5, "manual_mask": 2}
        expected = {"retained_refresh_request": 1, "cars": [{"physical": physical,
            "lod_manual_masks": [0xfe, 0xff, 0], "source_car_mask": 1,
            "source_high_medium_mask": 2, "source_full_body_mask": 3} for _ in range(16)]}
        events = [self.event(0, "init", initial, {"roots": [{"registration": {"sector": actor}} for actor in range(17)]}),
                  self.event(1, "migrate", {"root": 0, "world_position": [1., 2., 3.]}, {"registration": physical["registration"]}),
                  self.event(2, "select", {"camera_world": [0.] * 16, "world_to_view": [1.] * 16,
                      "world_position": [4., 5., 6.], "projection_q16": list(range(16)), "near_q16": 0x4000, "far_q16": 100 * 65536,
                      "ancestor_roots": [16, 0], "all_sectors": False},
                      {"ordered_sectors": [2, 1], "detail": [0, 1, 2], "selected_bits_hex": "06" + "00" * 255}),
                  self.event(3, "car_lod", models, expected), self.event(4, "publish", {}, expected)]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "scene.bin"
            metadata = TRACE.SCENE.write_binary(path, {"exe_crc32": "8ce56d41"}, events)
            before = path.read_bytes()
            self.assertEqual(before[:8], b"TOCASCN1")
            self.assertEqual(struct.unpack_from("<7I", before, 8), (1, 0, 7, 1, 3, 14, 5))
            self.assertEqual(struct.unpack_from("<IQI", before, 36), (1, 0, 17))
            self.assertEqual(struct.unpack_from("<IQI3f", before, 460), (2, 1, 0, 1., 2., 3.))
            self.assertEqual(struct.unpack_from("<IQ", before, 488), (3, 2))
            self.assertEqual(struct.unpack_from("<2i", before, 640), (0x4000, 100 * 65536))
            self.assertEqual(struct.unpack_from("<IQ4Ii", before, 664), (4, 3, 14, 3, 2, 0, 0))
            self.assertEqual(before[-12:], struct.pack("<IQ", 5, 4))
            self.assertEqual(metadata, {"bytes": 964, "crc32": f"{zlib.crc32(before):08x}"})
            rows = list(TRACE.SCENE.expected_rows(events))
            self.assertEqual(len(rows), 528)
            self.assertEqual([row for row in rows if row[0] == 0], [(0, "root", actor, "sector", actor) for actor in range(17)])
            self.assertIn((3, "models", 0, "high_manual", 0), rows)
            self.assertIn((3, "models", 0, "medium_manual", 1), rows)
            self.assertIn((4, "root", 0, "render_mask", 1), rows)
            self.assertIn((4, "root", 0, "neighbor_0", 4), rows)
            self.assertNotIn((4, "root", 1, "neighbor_0", 4), rows)
            self.assertNotIn("model_detail", [row[3] for row in rows])
            events[4]["provenance"]["pointer"] = 0xdeadbeef
            physical["render_mask"] = 4
            TRACE.SCENE.write_binary(path, {"exe_crc32": "8ce56d41"}, events)
            self.assertEqual(path.read_bytes(), before)
            self.assertIn((4, "root", 0, "render_mask", 0), list(TRACE.SCENE.expected_rows(events)))

    def test_autonomous_capture_profile_is_validated_before_source_launch(self):
        flags = ["--scope", "fleet", "--scenario", "local-lap", "--scene-autonomous", "--contact-weather-rng",
                 "--cold-grid-init", "--cold-grid-sequence", "--checkpoint-timing", "--finish-timing",
                 "--session-boundaries", "--local-handoffs", "--updates", "64"]
        class BeforeLaunch(Exception): pass
        with patch.object(sys, "argv", ["trace-reference.py", *flags]), patch.object(TRACE.Path, "read_bytes", side_effect=BeforeLaunch):
            with self.assertRaises(BeforeLaunch): TRACE.main()
        for extra in (["--warm-restart"], ["--updates", "3"], ["--qualifying"], ["--warmup-step", "1:Up"]):
            with patch.object(sys, "argv", ["trace-reference.py", *flags, *extra]), patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as result: TRACE.main()
            self.assertEqual(result.exception.code, 2)

    def test_joined_clock_observer_owns_hooks_and_combined_collector_drains_queue(self):
        debugger, frames = Mock(), Mock()
        debugger.breakpoint.side_effect = lambda pc: pc
        debugger.command.return_value = "Stopped at0x00421aa0"
        frames.drain_clock_events.return_value = [
            {"kind": "qpc_sample", "event": {"order": 2, "counter": 123}},
            {"kind": "primary", "event": {"order": 3, "cached_clock": 456}}]
        with patch.object(TRACE, "capture_qpc_sample") as duplicate:
            journal = TRACE.advance_combined_update(debugger, local_session=True, qpc_samples=True, scene_frames=frames)
        self.assertEqual(journal, frames.drain_clock_events.return_value)
        self.assertNotIn(call(0x44a370), debugger.breakpoint.call_args_list)
        self.assertNotIn(call(0x412ab0), debugger.breakpoint.call_args_list)
        duplicate.assert_not_called()

    def test_joined_expectations_bind_capture_crc_physical_ids_and_authored_roots(self):
        module = TRACE.SCENE.FRAMES
        setup, clock, frame, events = self.frame_fixture()
        frame["expected"]["physical_ids"] = [0]
        physical = frame["input"]["physical"][0]
        pre = bytearray(0x53c)
        struct.pack_into("<3i", pre, 0xbc, *physical["pre_body"]["position_q16"])
        struct.pack_into("<9i", pre, 0x150, *physical["pre_body"]["basis_q16"])
        after = []
        for body in physical["post_bodies"]:
            raw = bytearray(0x53c)
            struct.pack_into("<3i", raw, 0xbc, *body["position_q16"])
            struct.pack_into("<I", raw, 0x4f0, body["mode"])
            after.append({"body_hex": raw.hex()})
        records = [{"pre": {"session_state": 9, "actors": [{"body_hex": pre.hex()} for _ in range(16)]},
                    "post": {"actors": after}, "combined_journal": [],
                    "scene_final_journal": [{"kind": "qpc_sample", "event": {"counter": frame["input"]["qpc_counter"]}}]}]
        configuration = bytearray(0x620); struct.pack_into("<I", configuration, 0x328, setup["graphics_detail"])
        header = {"scene_capture_id": "same-run", "configuration_hex": configuration.hex(),
                  "initial_configuration": setup, "local_actor": setup["local_actor"]}
        journal_header = {"capture_id": "same-run"}
        frame_header = {"capture_id": "same-run", "setup": setup}
        footer = {"fleet_source": {"prefix_crc32": "12345678", "updates": 1}}
        result = module.validate_autonomous_join(header, records, journal_header, events, frame_header, [frame], footer, "12345678")
        self.assertEqual(result["physical_updates"], 1)
        with self.assertRaisesRegex(ValueError, "CRC32 prefix"):
            module.validate_autonomous_join(header, records, journal_header, events, frame_header, [frame], footer, "12345679")
        frame_header["capture_id"] = "other-run"
        with self.assertRaisesRegex(ValueError, "different source execution"):
            module.validate_autonomous_join(header, records, journal_header, events, frame_header, [frame], footer, "12345678")
        frame_header["capture_id"] = "same-run"
        frame["expected"]["physical_ids"] = [1]
        with self.assertRaisesRegex(ValueError, "expectation ID"):
            module.validate_autonomous_join(header, records, journal_header, events, frame_header, [frame], footer, "12345678")
        frame["expected"]["physical_ids"] = [0]
        events[0]["expected"] = {"roots": [{"registration": {"sector": actor}} for actor in range(17)]}
        rows = list(module.expected_autonomous_rows([frame], events))
        self.assertEqual(rows[:4], [(0, "initial_roots", 0, "position_x_bits", 0), (0, "initial_roots", 0, "position_y_bits", 0),
                                   (0, "initial_roots", 0, "position_z_bits", 0), (0, "initial_roots", 0, "sector", 0)])
        self.assertEqual(rows[68][1], "timing")


class OrdinaryCameraTests(unittest.TestCase):
    inputs = SceneJournalTests.inputs
    event = SceneJournalTests.event

    def test_new_profile_requires_cockpit_root_and_keeps_view2_scope_specific(self):
        inputs = self.inputs()
        inputs["roots"].append({"id": 17, "role": "cockpit_anchor", "flags": 0, "local_position": [0., 0., 0.]})
        event = {**self.event(0, "init", inputs), "profile": TRACE.SCENE.ORDINARY_PROFILE}
        TRACE.SCENE.validate(event)
        inputs["roots"].pop()
        with self.assertRaisesRegex(ValueError, "car parents"): TRACE.SCENE.validate(event)
        for view in range(1, 6):
            context = {key: value for key, value in self.inputs().items() if key != "roots"}
            context.update(source_view=view, factor_q16=TRACE.SCENE.VIEW_FACTORS[view], body_positions_q16=[[0, 0, 0] for _ in range(16)], body_modes=[0] * 16)
            event = {**self.event(1, "car_lod", context, {"retained_refresh_request": 1}), "profile": TRACE.SCENE.ORDINARY_PROFILE}
            TRACE.SCENE.validate(event)
            if view != 2:
                event.pop("profile")
                with self.assertRaisesRegex(ValueError, "View2"): TRACE.SCENE.validate(event)
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "expectations for autonomous"):
                TRACE.SCENE.write_binary(Path(temporary) / "bad.bin", {"contract": "single-player-ordinary-views-scene-boundary"}, [])

    def test_requested_keys_are_checked_against_actual_cached_mapper_actions(self):
        module = TRACE.SCENE.FRAMES
        memory = {}
        def put(address, fmt, *values):
            memory.update({address + offset: value for offset, value in enumerate(struct.pack("<" + fmt, *values))})
        def values(fmt, address):
            raw = bytes(memory.get(address + offset, 0) for offset in range(struct.calcsize("<" + fmt)))
            return list(struct.unpack("<" + fmt, raw))
        debugger, journal = Mock(), Mock()
        debugger.breakpoint.side_effect = lambda pc: pc
        debugger.command.return_value = "0x30000"
        debugger.word.side_effect = lambda address: values("I", address)[0]
        journal.debugger, journal.values, journal.initialized = debugger, values, True
        put(0x4d38e0, "I", 0x10000)
        put(0x4d3890, "I", 0x20000)
        put(0x10000 + 8 + 7 * 4, "I", 0x400)
        put(0x10000 + 8 + 6 * 4, "I", 0x200)
        put(0x20000 + 8, "I", 0x400)
        put(0x4c3a66, "H", 2)
        put(0x30000, "3I", 0x404ff0, 0x4c5da0, 0)
        with tempfile.TemporaryDirectory() as temporary:
            recorder = module.Recorder(journal, Path(temporary) / "frames.jsonl", "8ce56d41", camera_controls=True)
            recorder.configure_camera_inputs(module.camera_keys)
            recorder.physical_id = 8
            self.assertTrue(recorder.intercept("Stopped at0x0044a1d0"))
            recorder.physical = {}
            self.assertTrue(recorder.intercept("Stopped at0x00422f60"))
            put(0x4c3a66, "H", 3)
            self.assertTrue(recorder.intercept("Stopped at0x00404ff0"))
            transaction = recorder.camera_transaction(8)
            self.assertEqual(transaction["input"], {"physical_id": 8, "cycle_held": True, "rear_held": False})
            self.assertEqual(transaction["expected"]["post"]["selected_view"], 3)
            self.assertNotIn("selected_view", transaction["input"])
            recorder.physical = None
            recorder.physical_id = 9
            put(0x20000 + 8, "I", 0)
            recorder.intercept("Stopped at0x0044a1d0")
            recorder.physical = {}
            recorder.intercept("Stopped at0x00422f60")
            recorder.intercept("Stopped at0x00404ff0")
            self.assertFalse(recorder.camera_transaction(9)["expected"]["cycle_pressed"])
            recorder.physical = None
            recorder.physical_id = 10
            recorder.intercept("Stopped at0x0044a1d0")
            put(0x20000 + 8, "I", 0x400)
            recorder.physical = {}
            with self.assertRaisesRegex(ValueError, "cached poll actions"):
                recorder.intercept("Stopped at0x00422f60")
            recorder.close()
            recorder.finish("synthetic interrupted interval")

    def test_camera_scenario_requires_complete_profile_and96_updates(self):
        profile = ["--scope", "fleet", "--scenario", "camera-cycle", "--scene-autonomous", "--contact-weather-rng", "--cold-grid-init", "--cold-grid-sequence", "--checkpoint-timing", "--finish-timing", "--session-boundaries", "--local-handoffs", "--updates", "96"]
        class BeforeLaunch(Exception): pass
        with patch.object(sys, "argv", ["trace-reference.py", *profile]), patch.object(TRACE.Path, "read_bytes", side_effect=BeforeLaunch):
            with self.assertRaises(BeforeLaunch): TRACE.main()
        with patch.object(sys, "argv", ["trace-reference.py", *profile, "--updates", "64"]), patch("sys.stderr", new=io.StringIO()):
            with self.assertRaises(SystemExit) as result: TRACE.main()
            self.assertEqual(result.exception.code, 2)
        module = TRACE.SCENE.FRAMES
        previous = False
        edges = []
        for physical_id in range(96):
            keys = module.camera_keys(physical_id)
            if keys["cycle_held"] and not previous: edges.append(physical_id)
            previous = keys["cycle_held"]
        self.assertEqual(edges, [8, 18, 28, 40, 50, 62, 72, 80, 88, 92])
        self.assertEqual(module.camera_keys(95), {"cycle_held": False, "rear_held": False})


class QualifyingSceneConsumerTests(unittest.TestCase):
    def setUp(self):
        import copy
        self.copy = copy.deepcopy
        directory = tempfile.TemporaryDirectory(prefix="qualifying-scene-")
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.module = TRACE.SCENE.FRAMES
        spec = importlib.util.spec_from_file_location("replay_scene_journal", Path(__file__).resolve().parents[3] / "tools/replay-scene-journal.py")
        self.cli = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"reference_scene_journal": TRACE.SCENE,
                "reference_scene_frames": self.module, "reference_fleet": TRACE.FLEET}):
            spec.loader.exec_module(self.cli)

    def fixture(self):
        warmup = self.directory / "local-driving-warmup.jsonl"
        history = [{"update": index, "counter": index, "requested_keys": [] if index == 0 else ["Up"],
                    "decision": {"session_state": 4 if index == 0 else 6}} for index in range(2)]
        warmup.write_text("".join(json.dumps(record) + "\n" for record in history))
        config = bytearray(0x620)
        struct.pack_into("<I", config, 0xac, 1)
        struct.pack_into("<I", config, 0x328, 3)
        header = {"exe_crc32": "8ce56d41", "schema": 2, "session": "single-race", "source_mode": 0,
                  "qualifying": True, "actor_count": 16, "local_actor": 14, "configuration_hex": config.hex(),
                  "initial_configuration": {"track_id": 0, "car_id": 7, "weather": 1},
                  "unrecorded_keyboard_warmup": {"targeted_local_finish": {"decision_log": warmup.name,
                      "decision_log_crc32": f"{zlib.crc32(warmup.read_bytes()):08x}", "updates": 2,
                      "initial_update_counter": 0, "counter_resets": [], "target_lap": 3,
                      "endpoint": {"session_state": 6, "update_counter": 2, "race_clock": 1000}}}}
        records = []
        for index, clock in enumerate((1000, 1500, 2000, 2500, 5000, 5500, 6000, 6500, 7000, 7500)):
            state = 6 if index < 2 else 7 if index < 6 else 8
            origin = 1000 if state == 6 else 2000 if state == 7 else 5500
            actors = []
            for actor in range(16):
                body = bytearray(0x53c)
                struct.pack_into("<3i", body, 0xbc, actor * 65536, 0, index * 65536)
                struct.pack_into("<9i", body, 0x150, 65536, 0, 0, 0, 65536, 0, 0, 0, 65536)
                struct.pack_into("<I", body, 0x4f0, 0 if actor % 4 == 2 else 4)
                registration = bytearray(0x24)
                struct.pack_into("<4H", registration, 0, actor, 0xffff, 0xffff, 0xffff)
                registration[0x18] = registration[0x20] = 1
                actors.append({"body_hex": body.hex(), "root": {"registration_hex": registration.hex(),
                    "sector": actor, "neighbors": [0xffff] * 3, "sector_visible": actor == 0},
                    "effect_root": {"flags_hex": "01000000"}})
            shared = {"lifecycle": {"race_lifecycle_hex": struct.pack("<5I", 0, index + 2, clock, 0, index % 2).hex(),
                "player_lifecycle_hex": struct.pack("<12H", 1, 2, *([0] * 10)).hex(),
                "session_timers_hex": struct.pack("<5I", origin, 0, 0, 0, 0).hex()},
                "provenance": {"world_status_hex": bytes(48).hex()}, "sector_visibility_hex": "01" + "00" * 255,
                "sector_visibility_disabled": 1, "sector_visibility_override": 0}
            snapshot = {"session_state": state, "update_counter": index + 2, "race_clock": clock,
                "delta_q16": 98304, "reciprocal_q16": 43690, "extra_update": True, "actors": actors, "shared": shared}
            records.append({"id": index, "pre": snapshot, "post": self.copy(snapshot), "local_handoffs": []})
        records[1]["local_handoffs"] = [{"kind": 0, "actor": 14, "return_address": 0x4217aa}]
        return header, records, warmup

    def test_v2_transports_actual_pass_and_physical_inputs_without_synthesized_qpc(self):
        header, records, warmup = self.fixture()
        inputs = self.module.qualifying_consumer_inputs(header, records, warmup)
        self.assertEqual((inputs["scope"]["source_begin"], inputs["compare_begin"]), (2, 5))
        self.assertEqual([value["current_pass"] for value in inputs["physical"]], [0, 1] * 4)
        self.assertEqual([value["compare"] for value in inputs["physical"]], [False] * 3 + [True] * 5)
        self.assertFalse(inputs["scope"]["clock_autonomy"])
        self.assertIsNone(inputs["scope"]["camera_matrix_parity"])
        path = self.directory / "frames.bin"
        info = self.module.write_qualifying_frames_binary(path, inputs)
        data = path.read_bytes()
        self.assertEqual(info["bytes"], 4104)
        self.assertEqual(data[:8], b"TOCASFR1")
        self.assertEqual(struct.unpack_from("<8I", data, 8), (2, 0, 7, 1, 3, 14, 7, 8))
        self.assertEqual(struct.unpack_from("<IiiIIiII", data, 40 + 16 * 48), (2, 98304, 43690, 1, 7, 0, 0, 0))
        self.assertNotIn("qpc_counter", inputs)

    def test_observed_scene_outputs_never_change_binary_but_physical_input_does(self):
        header, records, warmup = self.fixture()
        path = self.directory / "frames.bin"
        def binary():
            inputs = self.module.qualifying_consumer_inputs(header, records, warmup)
            self.module.write_qualifying_frames_binary(path, inputs)
            return path.read_bytes()
        before = binary()
        for record in records:
            for snapshot in (record["pre"], record["post"]):
                snapshot["shared"]["sector_visibility_hex"] = "ff" * 256
                snapshot["shared"]["sector_visibility_disabled"] = 999
                snapshot["shared"]["lifecycle"]["player_lifecycle_hex"] = struct.pack("<12H", 9, 2, *([0] * 10)).hex()
                for actor in snapshot["actors"]:
                    actor["root"].update(registration_hex="ff" * 36, sector=999, neighbors=[998] * 3, sector_visible=False)
                    actor["effect_root"]["flags_hex"] = "ff" * 4
        self.assertEqual(binary(), before)
        body = bytearray.fromhex(records[2]["pre"]["actors"][14]["body_hex"])
        struct.pack_into("<i", body, 0xbc, 123456)
        records[2]["pre"]["actors"][14]["body_hex"] = body.hex()
        self.assertNotEqual(binary(), before)

    def test_current_pass_aliases_and_pristine_history_are_required(self):
        for defect in ("pass", "alias", "missing_extra", "history", "history_crc", "view", "first_reset", "mode"):
            header, records, warmup = self.fixture()
            if defect in ("pass", "alias", "missing_extra"):
                index = 5 if defect == "missing_extra" else 4
                for phase in (("pre",) if defect == "alias" else ("pre", "post")):
                    raw = bytearray.fromhex(records[index][phase]["shared"]["lifecycle"]["race_lifecycle_hex"])
                    struct.pack_into("<I", raw, 16, int(defect != "missing_extra"))
                    records[index][phase]["shared"]["lifecycle"]["race_lifecycle_hex"] = raw.hex()
            elif defect == "history":
                history = [json.loads(line) for line in warmup.read_text().splitlines()]
                history[1]["decision"]["session_state"] = 13
                warmup.write_text("".join(json.dumps(value) + "\n" for value in history))
                header["unrecorded_keyboard_warmup"]["targeted_local_finish"]["decision_log_crc32"] = f"{zlib.crc32(warmup.read_bytes()):08x}"
            elif defect == "history_crc": warmup.write_text(warmup.read_text() + "\n")
            elif defect == "view": records[5]["pre"]["shared"]["lifecycle"]["player_lifecycle_hex"] = struct.pack("<12H", 1, 3, *([0] * 10)).hex()
            elif defect == "first_reset": records[2]["pre"]["race_clock"] += 1
            else:
                raw = bytearray.fromhex(records[4]["post"]["actors"][0]["body_hex"])
                struct.pack_into("<I", raw, 0x4f0, 0)
                records[4]["post"]["actors"][0]["body_hex"] = raw.hex()
            with self.subTest(defect=defect), self.assertRaises(ValueError):
                self.module.qualifying_consumer_inputs(header, records, warmup)

    def test_suffix_compares_all_neighbors_after_authored_entry_and_is_strict(self):
        header, records, warmup = self.fixture()
        inputs = self.module.qualifying_consumer_inputs(header, records, warmup)
        rows = list(self.module.expected_qualifying_consumer_rows(records, inputs))
        self.assertEqual(len(rows), 1940)
        self.assertEqual(sorted({row[0] for row in rows}), [5, 6, 7, 8, 9])
        self.assertIn((5, "pre_root", 0, "neighbor_0", 65535), rows)
        self.assertIn((5, "pre_root", 2, "neighbor_0", 65535), rows)
        self.assertIn((5, "pre_camera", 0, "authored_index", 1), rows)
        path = self.directory / "native.csv"
        def output(values):
            path.write_text("order,group,index,field,value\n" + "".join(",".join(map(str, row)) + "\n" for row in values))
        output(rows)
        self.assertTrue(self.cli.compare(iter(rows), path)["numerical_parity"])
        for values in (rows[:-1], [rows[1], rows[0], *rows[2:]], [*rows, rows[-1]]):
            output(values)
            with self.assertRaises(ValueError): self.cli.compare(iter(rows), path)

    def test_cli_reuses_native_frames_reader_and_reports_component_scope(self):
        header, records, warmup = self.fixture()
        inputs = self.module.qualifying_consumer_inputs(header, records, warmup)
        rows = list(self.module.expected_qualifying_consumer_rows(records, inputs))
        trace = self.directory / "trace.jsonl"
        trace.write_text('{"type":"complete","prefix_crc32":"12345678"}\n')
        for numerical in (True, False):
            def execute(command, **kwargs):
                values = list(rows)
                if not numerical: values[0] = (*values[0][:-1], values[0][-1] + 1)
                Path(command[-1]).write_text("order,group,index,field,value\n" + "".join(",".join(map(str, row)) + "\n" for row in values))
                return subprocess.CompletedProcess(command, 0, "", "")
            with patch.object(sys, "argv", ["replay-scene-journal.py", str(trace), "--qualifying-consumers", "--output", str(self.directory / "out")]), \
                    patch.object(self.cli.FLEET, "read_trace", return_value=(header, records)), \
                    patch.object(self.cli.subprocess, "run", side_effect=execute) as execution, patch("sys.stdout", new=io.StringIO()):
                self.assertEqual(self.cli.main(), 0 if numerical else 1)
            self.assertEqual(execution.call_args.args[0][1], "--frames")
            report = json.loads((self.directory / "out/scene-comparison.json").read_text())
            self.assertEqual(report["parity"], numerical)
            self.assertFalse(report["scope"]["physics_autonomy"])
            self.assertFalse(report["scope"]["clock_autonomy"])
            self.assertIsNone(report["scope"]["ordered_pvs_parity"])
            self.assertEqual(report["scene_input_imports"], 0)
