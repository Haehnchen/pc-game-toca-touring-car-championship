#!/usr/bin/env python3
"""Check captured-scene boundary and precision contracts without launching Wine."""

import importlib.util
import contextlib
import copy
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

PROJECT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("compare_render", PROJECT / "tools/compare-render-reference.py")
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)
CAPTURE_SPEC = importlib.util.spec_from_file_location("capture_render", PROJECT / "tools/capture-render-reference.py")
capture = importlib.util.module_from_spec(CAPTURE_SPEC)
CAPTURE_SPEC.loader.exec_module(capture)
ANIMATION_SPEC = importlib.util.spec_from_file_location("render_animation", PROJECT / "tools/render-reference-animation.py")
animation = importlib.util.module_from_spec(ANIMATION_SPEC)
ANIMATION_SPEC.loader.exec_module(animation)
actors = compare.actors


def fixture():
    matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -115.90690612792969, 8.40478515625, -136.39598083496094, 1.0]
    body = bytearray(0x53c)
    struct.pack_into("<3i", body, 0xbc, *(round(value * 65536) for value in matrix[12:15]))
    return {"schema": 2, "exe_crc32": "8ce56d41", "source_boundary": "489069-entry-published-scene-before-draw", "published_body_root_matches": True,
            "presentation_barrier": {"main_thread_frozen": True, "workers_released": True, "state_unchanged": True, "stable_window": True},
            "body_unchanged_during_present": True, "world_to_view": matrix[:], "camera_world": matrix[:], "car_matrix": matrix[:],
            "projection_q16": [83886, 0, 0, 0, 0, 104857, 0, 0, 0, 0, 65601, 65536, 0, 0, -16400, 0],
            "width": 640, "height": 480, "track_id": 0, "car_id": 7, "weather": 1, "detail": 3, "horizon_index": 0, "actor": 0,
            "steering_angle": -32768, "wheel_angles": [-123, 32767, 0, 123], "weather_particles": [], "weather_alpha": [],
            "car_visual_angles": [-8, 0],
            "weather_streak": [0, 0, 0], "car_nodes": [{"id": 501, "world": matrix[:]}], "body_hex": body.hex()}


def effect_sprite():
    return {"center": [-2147483648, 2147483647, -65537],
            "minimum": [-131073, -65536, -1], "maximum": [65537, 131072, 2147483647],
            "shade0": 0, "shade1": 255, "transform_pointer": 0, "source_packet_hex": ""}


def effect_batch(texture="smoke", sprites=None):
    return {"texture": texture, "surface": 0x12345678, "flags": 6 if texture in ("spray", "bang") else 2,
            "sprites": [effect_sprite()] if sprites is None else sprites}


def hud_fixture():
    return {"speed_integer": 259, "rpm": 8499, "gear": 7, "start_phase": 4,
            "kilometres": False, "visible": True, "qualifying": False,
            "lap": 99, "lap_limit": 99, "lap_time_ms": 4294967295, "best_lap_ms": 2147483649,
            "mode": 0, "position": 16, "split_active": True, "split_ticks": -32768,
            "split_text": "-00:12.34"}


def thread_listing():
    return r"""process  tid      prio    name (all IDs are in hex)
00000020 start.exe
	["C:\windows\system32\start.exe" /exec C:\TOCA\tourcars.exe]
	00000024    0
000000ec (D) C:\TOCA\tourcars.exe
	[C:\TOCA\tourcars.exe C:\TOCA D:\ 0]
	000000f0    0 <==
	0000020c    0     dxvk-submit
	00000218    0     dxvk-cs
000000fc rpcss.exe
	[C:\windows\system32\rpcss.exe]
	00000100    0     wine_rpcrt4_server
"""


class EffectMemory:
    """Synthetic ready queue; no Wine, assets or reference installation."""

    def __init__(self):
        self.bytes = bytearray(0x620000)

    def put(self, address, data):
        self.bytes[address:address + len(data)] = data

    def put_word(self, address, value):
        self.put(address, struct.pack("<I", value))

    def memory(self, address, length):
        if address < 0 or length < 0 or address + length > len(self.bytes):
            raise AssertionError("Effect capture read outside synthetic memory")
        return bytes(self.bytes[address:address + length])

    def word(self, address):
        return struct.unpack("<I", self.memory(address, 4))[0]


def queued_effects():
    debugger = EffectMemory()
    # Distinct wrappers prove texture classification follows the actual bound
    # surface rather than queue position, descriptor order or material handle.
    for index, address in enumerate((0x4ed948, 0x4ed94c, 0x4ed950, 0x4ed8b0, 0x4ed8b4, 0x4ed8b8, 0x4ed8a8)):
        wrapper, image, surface = 0x1000 + index * 0x100, 0x2000 + index * 0x100, 0x3000 + index * 0x100
        debugger.put_word(address, wrapper)
        debugger.put_word(wrapper + 0xc, image)
        debugger.put_word(image + 4, surface)
    minimum, maximum, center = [-131073, -65536, -1], [65537, 131072, 2147483647], [-2147483648, 2147483647, -65537]
    corners = [minimum[2], minimum[1], minimum[0], maximum[2], minimum[1], maximum[0],
               minimum[2], maximum[1], minimum[0], maximum[2], maximum[1], maximum[0]]
    first = struct.pack("<15iBB2xI", *corners, *center, 137, 241, 0)
    second = struct.pack("<15iBB2xI", *corners, *[17, -19, 23], 255, 1, 0)
    debugger.put_word(0x61ba48, 2)
    debugger.put_word(0x61ba50, 2)
    debugger.put(0x61b848, struct.pack("<2I", 1, 1))
    debugger.put(0x614dc8, struct.pack("<2I", 0x3000, 0xdeadbeef))
    debugger.put(0x617248, struct.pack("<2I", 0x7777, 0x8888))
    debugger.put(0x614fc8, bytes([2, 6]))
    debugger.put(0x615048, first + second)
    return debugger, first, second


class RenderCaptureTests(unittest.TestCase):
    def test_hidden_exterior_retains_source_world_without_weakening_visible_root_check(self):
        state = fixture()
        state["car_nodes"] = [{"id": 501, "visible": 0}]
        rows = [{"node_id": "501", "matched": "1", "max_matrix_difference": ".000228881836"}]
        compare.verify_car_geometry(rows, state)
        state["car_nodes"][0]["visible"] = 1
        with self.assertRaises(RuntimeError):
            compare.verify_car_geometry(rows, state)
        rows[0]["max_matrix_difference"] = "0"
        compare.verify_car_geometry(rows, state)

    def test_observed_local_lod_and_view_must_match_selection(self):
        state = fixture()
        state.update(source_view=2, selected_lod="high", lod_roots=[
            {"name": name, "address": address, "visible_mask": visible}
            for name, address, visible in (("high", 100, 1), ("medium", 200, 0), ("low", 300, 0))])
        state["car_nodes"][0]["address"] = 100
        compare.validate(state)
        state["lod_roots"][1]["visible_mask"] = 1
        with self.assertRaises(ValueError):
            compare.validate(state)
        state["lod_roots"][1]["visible_mask"] = 0
        state["source_view"] = 1
        with self.assertRaises(ValueError):
            compare.validate(state)

    def test_bonus_local_selection_and_separate_rival_model_boundary(self):
        for car in (8, 9):
            with self.subTest(car=car):
                args = capture.parse_arguments(["--stage-scene-selectors", "--car", str(car), "--detail", "1"])
                self.assertEqual((args.car, args.detail), (car, 1))
                state = fixture()
                state["car_id"] = car
                state["detail"] = 1
                state["car_nodes"] = [{"id": 507, "world": state["car_matrix"]}]
                compare.validate(state)
        for arguments in (["--stage-scene-selectors", "--car", "10"], ["--car", "8"]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.parse_arguments(arguments)
        state = fixture()
        state["car_id"] = 10
        with self.assertRaises(ValueError):
            compare.validate(state)

    def test_requires_same_completed_frame(self):
        state = fixture()
        for name, value in (("source_boundary", "49a660-return"), ("body_unchanged_during_present", False)):
            with self.subTest(name=name):
                changed = {**state, name: value}
                with self.assertRaises(ValueError):
                    compare.frame_text(changed)

    def test_requires_workers_released_with_main_thread_and_render_state_frozen(self):
        state = fixture()
        compare.validate(state)
        missing = dict(state)
        del missing["presentation_barrier"]
        with self.assertRaises(ValueError):
            compare.frame_text(missing)
        for field in state["presentation_barrier"]:
            for value in (False, 1, "true", None):
                with self.subTest(field=field, value=value):
                    changed = {**state, "presentation_barrier": {**state["presentation_barrier"], field: value}}
                    with self.assertRaises(ValueError):
                        compare.frame_text(changed)
            with self.subTest(missing_field=field):
                barrier = dict(state["presentation_barrier"])
                del barrier[field]
                with self.assertRaises(ValueError):
                    compare.frame_text({**state, "presentation_barrier": barrier})

    def test_rejects_stale_capture_schema_and_malformed_presentation_barrier(self):
        state = fixture()
        with self.assertRaises(ValueError):
            compare.frame_text({**state, "schema": 1})
        for value in (None, [], "all threads stopped", True):
            with self.subTest(barrier=value):
                with self.assertRaises(ValueError):
                    compare.frame_text({**state, "presentation_barrier": value})

    def test_source_thread_selection_uses_the_debugged_owned_process_block(self):
        self.assertEqual(capture.current_source_thread(thread_listing(), 0xec), 0xf0)
        # An executable mentioned by another process's command line is not
        # evidence that the selected thread belongs to the game process.
        outside = thread_listing().replace("000000f0    0 <==", "000000f0    0")
        outside = outside.replace("00000100    0     wine_rpcrt4_server", "00000100    0 <== wine_rpcrt4_server")
        with self.assertRaises((ValueError, RuntimeError)):
            capture.current_source_thread(outside, 0xec)

    def test_source_thread_selection_rejects_wrong_pid_ambiguous_or_missing_selection(self):
        listing = thread_listing()
        invalid = [(listing, 0xfc),
                   (listing.replace("00000218    0     dxvk-cs", "00000218    0 <== dxvk-cs"), 0xec),
                   (listing.replace("000000f0    0 <==", "000000f0    0"), 0xec),
                   (listing.replace(r"000000ec (D) C:\TOCA\tourcars.exe", r"000000ec (D) C:\TOCA\other.exe"), 0xec),
                   (listing.replace("000000ec (D)", "000000ec"), 0xec)]
        for text, expected_pid in invalid:
            with self.subTest(listing=text, expected_pid=expected_pid):
                with self.assertRaises((ValueError, RuntimeError)):
                    capture.current_source_thread(text, expected_pid)

    def test_source_float_bits_and_signed_fixed_values_survive_conversion(self):
        state = fixture()
        rows = compare.frame_text(state).splitlines()
        converted = [float(value) for value in rows[2].split()]
        self.assertEqual(struct.pack("<16f", *converted), struct.pack("<16f", *state["world_to_view"]))
        self.assertEqual([int(value) for value in rows[3].split()], list(struct.unpack_from("<3i", bytes.fromhex(state["body_hex"]), 0xbc)))
        self.assertEqual(rows[6], "-32768 -123 32767 0 123")

    def test_rejects_nonfinite_matrix_and_wrong_drawable(self):
        state = fixture()
        state["world_to_view"][0] = float("nan")
        with self.assertRaises(ValueError):
            compare.validate(state)
        state = fixture()
        state["width"] = 1280
        with self.assertRaises(ValueError):
            compare.validate(state)

    def test_rejects_unmatched_precipitation_and_multiple_actor_shadows(self):
        state = fixture()
        state["weather_particles"] = [[1, 2, 3]]
        with self.assertRaises(ValueError):
            compare.validate(state)
        state = fixture()
        state["shadow_records_hex"] = bytes(272).hex()
        with self.assertRaises(ValueError):
            compare.validate(state)

    def test_projection_rejects_unrepresented_coefficients_and_non_q16_values(self):
        # The importer retains X/Y/depth coefficients and fixes clip W=Z.
        for index in range(16):
            if index in (0, 5, 10, 14):
                continue
            with self.subTest(coefficient=index):
                state = fixture()
                state["projection_q16"][index] = 0 if index == 11 else 1
                with self.assertRaisesRegex(ValueError, "unsupported off-axis or homogeneous"):
                    compare.frame_text(state)
        for value in (True, 1.5, "83886", -(1 << 31) - 1, 1 << 31):
            with self.subTest(value=value):
                state = fixture()
                state["projection_q16"][0] = value
                with self.assertRaisesRegex(ValueError, "projection/drawable"):
                    compare.frame_text(state)
        state = fixture()
        state["projection_q16"][0] += 1
        state["projection_q16"][5] += 1
        state["projection_q16"][10] += 1
        state["projection_q16"][14] -= 1
        self.assertEqual([int(value) for value in compare.frame_text(state).splitlines()[4].split()], state["projection_q16"])

    def test_raster_report_retains_every_rgb_difference(self):
        result = compare.raster_metrics(bytes([10, 20, 30, 40, 50, 60]), bytes([10, 20, 30, 41, 52, 63]), 2, (0, 0, 2, 1))
        self.assertEqual(result["changed_pixels"], 1)
        self.assertEqual(result["mean_absolute_rgb_error"], 1)
        self.assertEqual(result["maximum_channel_error"], 3)
        self.assertEqual(result["pixels_equal_fraction"], .5)

    def test_capture_without_effect_pools_serializes_an_explicit_empty_surface_section(self):
        state = fixture()
        self.assertEqual(compare.surface_batches(state), [])
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows[0], "TOCA_RENDER_12")
        self.assertEqual(rows[8:13], ["0", "0", "0", "0", "1"])
        self.assertTrue(rows[13].startswith("501 "))

    def test_surface_packets_preserve_source_order_signed_precision_and_shades(self):
        state = fixture()
        state["weather_particles"] = [[-7, 11, -13]]
        state["weather_alpha"] = [193]
        state["weather_streak"] = [-17, 19, -23]
        names = ["spray", "mud", "gravel", "grass", "smoke", "dust"]
        state["effect_batches"] = [effect_batch(name) for name in names]
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows[9:12], ["1", "-7 11 -13 -17 19 -23 193", "6"])
        self.assertEqual([batch["texture"] for batch in compare.surface_batches(state)], names)
        expected = "-2147483648 2147483647 -65537 -131073 -65536 -1 65537 131072 2147483647 0 255"
        for index, texture_id in enumerate((2, 3, 4, 5, 0, 1)):
            self.assertEqual(rows[12 + index * 2], f"{texture_id} {6 if texture_id == 2 else 2} 1")
            self.assertEqual(rows[13 + index * 2], expected)
        self.assertEqual(rows[24:26], ["0", "1"])
        self.assertTrue(rows[26].startswith("501 "))

    def test_engine_packets_share_source_order_and_preserve_surface_texture_ids(self):
        state = fixture()
        names = ["spray", "bang", "mud", "gravel", "grass", "smoke", "dust"]
        state["effect_batches"] = [effect_batch(name) for name in names]
        self.assertEqual([batch["texture"] for batch in compare.render_effect_batches(state)], names)
        self.assertEqual([batch["texture"] for batch in compare.surface_batches(state)], [name for name in names if name != "bang"])
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows[10], "7")
        expected = "-2147483648 2147483647 -65537 -131073 -65536 -1 65537 131072 2147483647 0 255"
        for index, texture_id in enumerate((2, 6, 3, 4, 5, 0, 1)):
            self.assertEqual(rows[11 + index * 2], f"{texture_id} {6 if texture_id in (2, 6) else 2} 1")
            self.assertEqual(rows[12 + index * 2], expected)
        self.assertEqual(rows[25:27], ["0", "1"])
        self.assertTrue(rows[27].startswith("501 "))

    def test_engine_rejects_wrong_blend_and_reordered_or_duplicate_queue_entries(self):
        for names in (["bang", "spray"], ["mud", "bang"], ["bang", "bang"]):
            with self.subTest(names=names):
                state = fixture()
                state["effect_batches"] = [effect_batch(name) for name in names]
                with self.assertRaises(ValueError):
                    compare.frame_text(state)
        for flags in (0, 2, 7):
            with self.subTest(flags=flags):
                state = fixture()
                state["effect_batches"] = [{**effect_batch("bang"), "flags": flags}]
                with self.assertRaises(ValueError):
                    compare.frame_text(state)

    def test_engine_and_unknown_packets_share_the_source_sprite_budget(self):
        state = fixture()
        state["effect_batches"] = [effect_batch("bang", [effect_sprite() for _ in range(64)]),
                                   effect_batch("smoke", [effect_sprite() for _ in range(63)])]
        self.assertEqual(sum(len(batch["sprites"]) for batch in compare.render_effect_batches(state)), 127)
        state["effect_batches"].append(effect_batch("unclassified"))
        with self.assertRaises(ValueError):
            compare.frame_text(state)

    def test_hud_serializes_cached_values_exactly_without_smoothing_or_unit_conversion(self):
        state = fixture()
        state["hud"] = hud_fixture()
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows[11], "1")
        self.assertEqual(rows[12], "259 8499 7 4 0 1 0 99 99 4294967295 2147483649 0 16 1 -32768")
        self.assertEqual(rows[13], json.dumps("-00:12.34", ensure_ascii=True))
        self.assertEqual(rows[14], "1")
        self.assertTrue(rows[15].startswith("501 "))
        self.assertEqual(state["hud"], hud_fixture())

    def test_hud_keeps_hidden_snapshot_and_time_trial_zero_lap_limit(self):
        state = fixture()
        state["hud"] = {**hud_fixture(), "speed_integer": 0, "rpm": 2000, "gear": 0, "start_phase": 0,
                        "kilometres": True, "visible": False, "lap": 0, "lap_limit": 0, "lap_time_ms": 0,
                        "best_lap_ms": 0, "mode": 2, "position": 0, "split_active": False,
                        "split_ticks": 32767, "split_text": ""}
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows[11:14], ["1", "0 2000 0 0 1 0 0 0 0 0 0 2 0 0 32767", '""'])

    def test_hud_rejects_invalid_integer_widths_ranges_and_non_integer_values(self):
        invalid = [("speed_integer", -1), ("speed_integer", 261), ("rpm", 1999), ("rpm", 8501),
                   ("gear", -1), ("gear", 8), ("start_phase", -1), ("start_phase", 5),
                   ("lap", -1), ("lap", 100), ("lap_limit", 0), ("lap_limit", 100),
                   ("lap_time_ms", -1), ("lap_time_ms", 4294967296), ("best_lap_ms", -1), ("best_lap_ms", 4294967296),
                   ("mode", -1), ("mode", 4), ("position", -1), ("position", 17),
                   ("split_ticks", -32769), ("split_ticks", 32768), ("speed_integer", 10.5), ("rpm", True)]
        for field, value in invalid:
            with self.subTest(field=field, value=value):
                state = fixture()
                state["hud"] = {**hud_fixture(), field: value}
                with self.assertRaises(ValueError):
                    compare.frame_text(state)

    def test_hud_rejects_malformed_presence_booleans_and_split_text(self):
        for value in ({}, [], "cached HUD", 1):
            with self.subTest(hud=value):
                state = fixture()
                state["hud"] = value
                with self.assertRaises(ValueError):
                    compare.frame_text(state)
        for field in ("kilometres", "visible", "qualifying", "split_active"):
            with self.subTest(field=field):
                state = fixture()
                state["hud"] = {**hud_fixture(), field: 2}
                with self.assertRaises(ValueError):
                    compare.frame_text(state)
        for text in ("1" * 16, "NaN", '"00:12.34"', "00:12.34\n", "\u221200:12.34", 12):
            with self.subTest(split_text=text):
                state = fixture()
                state["hud"] = {**hud_fixture(), "split_text": text}
                with self.assertRaises(ValueError):
                    compare.frame_text(state)

    def test_unclassified_queue_batches_remain_explicit_without_becoming_surface_packets(self):
        state = fixture()
        unknown = effect_batch("unclassified")
        unknown["surface"] = 0xdeadbeef
        state["effect_batches"] = [unknown, effect_batch()]
        self.assertEqual([batch["texture"] for batch in compare.surface_batches(state)], ["smoke"])
        self.assertEqual([batch["texture"] for batch in compare.render_effect_batches(state)], ["smoke"])
        self.assertEqual(state["effect_batches"][0], unknown)
        self.assertEqual(compare.frame_text(state).splitlines()[10:12], ["1", "0 2 1"])

    def test_rejects_unrepresentable_surface_packets_and_changed_blend_or_transform(self):
        invalid = [("center", [-2147483649, 0, 0]), ("center", [2147483648, 0, 0]),
                   ("minimum", [0, 0]), ("maximum", [0, .5, 0]),
                   ("shade0", -1), ("shade1", 256), ("transform_pointer", 0x1234)]
        for field, value in invalid:
            with self.subTest(field=field, value=value):
                state = fixture()
                batch = effect_batch()
                batch["sprites"][0][field] = value
                state["effect_batches"] = [batch]
                with self.assertRaises(ValueError):
                    compare.frame_text(state)
        for name, flags in (("smoke", 6), ("spray", 2)):
            with self.subTest(texture=name, flags=flags):
                state = fixture()
                batch = effect_batch(name)
                batch["flags"] = flags
                state["effect_batches"] = [batch]
                with self.assertRaises(ValueError):
                    compare.frame_text(state)

    def test_rejects_reordered_duplicate_or_overfull_surface_queue(self):
        for names in (["smoke", "mud"], ["smoke", "smoke"]):
            with self.subTest(names=names):
                state = fixture()
                state["effect_batches"] = [effect_batch(name) for name in names]
                with self.assertRaises(ValueError):
                    compare.frame_text(state)
        state = fixture()
        state["effect_batches"] = [effect_batch(sprites=[effect_sprite() for _ in range(127)])]
        compare.validate(state)
        state["effect_batches"].append(effect_batch("unclassified"))
        with self.assertRaises(ValueError):
            compare.frame_text(state)

    def test_ready_queue_decodes_source_corner_permutation_and_bound_texture(self):
        debugger, first, second = queued_effects()
        batches = capture.effect_batches(debugger)
        self.assertEqual(len(batches), 2)
        self.assertEqual([batch["texture"] for batch in batches], ["smoke", "unclassified"])
        self.assertEqual([batch["surface"] for batch in batches], [0x3000, 0xdeadbeef])
        self.assertEqual([batch["flags"] for batch in batches], [2, 6])
        self.assertEqual([len(batch["sprites"]) for batch in batches], [1, 1])
        sprite = batches[0]["sprites"][0]
        self.assertEqual(list(sprite["center"]), [-2147483648, 2147483647, -65537])
        self.assertEqual(list(sprite["minimum"]), [-131073, -65536, -1])
        self.assertEqual(list(sprite["maximum"]), [65537, 131072, 2147483647])
        self.assertEqual((sprite["shade0"], sprite["shade1"], sprite["transform_pointer"]), (137, 241, 0))
        self.assertEqual(sprite["source_packet_hex"], first.hex())
        self.assertEqual(batches[1]["sprites"][0]["source_packet_hex"], second.hex())
        self.assertEqual(list(batches[1]["sprites"][0]["center"]), [17, -19, 23])

    def test_ready_queue_rejects_bounds_and_inconsistent_batch_lengths(self):
        for address, value in ((0x61ba48, 128), (0x61ba50, 128), (0x61b848, 3)):
            with self.subTest(address=hex(address), value=value):
                debugger, _, _ = queued_effects()
                debugger.put_word(address, value)
                with self.assertRaises((ValueError, RuntimeError)):
                    capture.effect_batches(debugger)

    def test_ready_queue_keeps_engine_between_spray_and_wheel_effects(self):
        debugger, first, _ = queued_effects()
        names = ["spray", "bang", "mud", "gravel", "grass", "smoke", "dust"]
        surfaces = [0x3200, 0x3600, 0x3300, 0x3400, 0x3500, 0x3000, 0x3100]
        debugger.put_word(0x61ba48, len(names))
        debugger.put_word(0x61ba50, len(names))
        debugger.put(0x61b848, struct.pack("<7I", *([1] * len(names))))
        debugger.put(0x614dc8, struct.pack("<7I", *surfaces))
        debugger.put(0x614fc8, bytes([6, 6, 2, 2, 2, 2, 2]))
        debugger.put(0x615048, first * len(names))
        batches = capture.effect_batches(debugger)
        self.assertEqual([batch["texture"] for batch in batches], names)
        self.assertEqual([batch["surface"] for batch in batches], surfaces)
        self.assertEqual([batch["flags"] for batch in batches], [6, 6, 2, 2, 2, 2, 2])
        self.assertTrue(all(batch["sprites"][0]["source_packet_hex"] == first.hex() for batch in batches))

    def test_ready_queue_keeps_unknown_binding_between_known_effects(self):
        debugger, first, second = queued_effects()
        debugger.put_word(0x61ba48, 3)
        debugger.put_word(0x61ba50, 3)
        debugger.put(0x61b848, struct.pack("<3I", 1, 1, 1))
        debugger.put(0x614dc8, struct.pack("<3I", 0x3200, 0xdeadbeef, 0x3600))
        debugger.put(0x614fc8, bytes([6, 2, 6]))
        debugger.put(0x615048, first + second + first)
        batches = capture.effect_batches(debugger)
        self.assertEqual([batch["texture"] for batch in batches], ["spray", "unclassified", "bang"])
        self.assertEqual([batch["surface"] for batch in batches], [0x3200, 0xdeadbeef, 0x3600])
        self.assertEqual([batch["sprites"][0]["source_packet_hex"] for batch in batches], [first.hex(), second.hex(), first.hex()])


def animated_fixture():
    return {"flags": [], **{family: [] for family in animation.FAMILIES}}


def animated_node(parent=-1, identity=756, visible=15):
    matrix = [65536, 0, 0, 0, 0, 65536, 0, 0, 0, 0, 65536, 0, -65537, 131073, -1, 65536]
    local = struct.pack("<16f", *(value / 65536 for value in matrix))
    return {"parent": parent, "id": identity, "visible": visible, "transform_q16": matrix, "local_float_hex": local.hex()}


def put_scene_node(debugger, address, sibling=0, child=0, parent=0, identity=756, visible=15):
    debugger.put(address, struct.pack("<3I", sibling, child, parent))
    debugger.put_word(address + 0x18, identity)
    debugger.put(address + 0x5c, bytes.fromhex(animated_node()["local_float_hex"]))
    # Deliberately unrelated world matrix; an accidental +9c import must fail.
    debugger.put(address + 0x9c, struct.pack("<16f", *([99.5] * 16)))
    debugger.put(address + 0x140, bytes([visible]))


def actor_memory():
    debugger = EffectMemory()
    config = 0x1000
    debugger.put_word(0x4b0660, config)
    for offset, value in ((4, 2), (0xac, 1), (0x30, 1)):
        debugger.put_word(config + offset, value)
    debugger.put(config + 0xb0, b"DRIVER\0")
    debugger.put(0x4fa038, b"SAVED\0")
    for address, value in ((0x4c5d6e, 3), (0x4c3a66, 2), (0x4fa030, 1), (0x52b3e4, 1)):
        debugger.put_word(address, value)
    local = fixture()["car_matrix"]
    for slot in range(3):
        physical = 0x10000 + slot * 0x1000
        debugger.put_word(0x4c3968 + slot * 4, physical)
        debugger.put(physical + 0x5c, struct.pack("<16f", *local))
        debugger.put(physical + 0x138, bytes([int(slot < 2)]))
        debugger.put(physical + 0x140, bytes([int(slot < 2)]))
        for lod, table in enumerate(actors.LOD_TABLES):
            root = physical + 0x200 + lod * 0x200
            debugger.put_word(table + slot * 4, root)
            put_scene_node(debugger, root, parent=physical, identity=501 + lod, visible=int(lod == 0))
            debugger.put(root + 0x138, bytes([int(lod == 0)]))
    debugger.put_word(0x61e150, 1)
    debugger.put(0x4ed880, bytes([1]))
    shadow = struct.pack("<33iI", *range(33), 2)
    debugger.put(0x61d888, shadow)
    debugger.put(0x4db028, shadow)
    loads = {slot: {"model_id": resource, "ordinary_material": int(slot == 0), "provenance": "44dc6d-entry"}
             for slot, resource in enumerate((7, 0, 7))}
    return debugger, loads


class ActorCaptureTests(unittest.TestCase):
    def test_loader_resource_and_material_arguments_survive_without_body_car_guessing(self):
        debugger = EffectMemory()
        debugger.put(0x100, struct.pack("<4I", 0x4439ac, 1, 0, 0))
        slot, binding = actors.capture_load(debugger, 0x100)
        self.assertEqual((slot, binding["model_id"], binding["ordinary_material"]), (1, 0, 0))
        self.assertEqual(binding["caller"], 0x4439ac)
        self.assertEqual(binding["provenance"], "44dc6d-entry")

    def test_only_ordinary_odd_clones_inherit_an_observed_source_resource(self):
        loads = {0: {"model_id": 8, "ordinary_material": 1}, 2: {"model_id": 1, "ordinary_material": 1},
                 4: {"model_id": 2, "ordinary_material": 1}, 5: {"model_id": 10, "ordinary_material": 1}}
        result = actors.resource_bindings(loads, 0, 6)
        self.assertEqual([result[slot]["model_id"] for slot in range(6)], [8, 8, 1, 1, 2, 10])
        self.assertEqual(result[1]["cloned_from"], 0)
        self.assertEqual(result[1]["provenance"], "44df6e-odd-slot-clone")
        with self.assertRaises(ValueError):
            actors.resource_bindings({0: loads[0]}, 2, 3)
        with self.assertRaises(ValueError):
            actors.resource_bindings({}, 0, 16)

    def test_time_trial_keeps_recorded_ghost_resource_owner_names_and_hidden_buffer(self):
        debugger, loads = actor_memory()
        snapshot = actors.capture_snapshot(debugger, loads)
        self.assertEqual((snapshot["ghost_owner"], snapshot["ghost_visible"]), (1, 1))
        self.assertEqual([actor["role"] for actor in snapshot["actors"]], [0, 2, 3])
        self.assertEqual([actor["model_id"] for actor in snapshot["actors"]], [7, 0, 7])
        self.assertEqual([actor["name"] for actor in snapshot["actors"]], ["DRIVER", "SAVED", "DRIVER"])
        self.assertEqual([actor["name_kind"] for actor in snapshot["actors"]], [1, 2, 1])
        self.assertEqual([actor["root_mask"] for actor in snapshot["actors"]], [1, 1, 0])
        self.assertEqual([actor["lod_mask"] for actor in snapshot["actors"]], [1, 1, 1])
        self.assertEqual(snapshot["actors"][0]["shadow_ordinal"], 0)
        self.assertEqual([actor["shadow_records_hex"] for actor in snapshot["actors"]][1:], ["", ""])

    def test_manual_eligibility_cannot_replace_actual_pvs_geometry_mask(self):
        debugger, loads = actor_memory()
        debugger.put(0x11000 + 0x138, b"\0")
        captured = actors.capture_snapshot(debugger, loads)["actors"][1]
        self.assertEqual((captured["manual_root_mask"], captured["root_mask"], captured["lod_mask"]), (1, 0, 1))
        self.assertEqual(captured["nodes"][0]["visible"], 1)

    def test_ghost_owner_switch_uses_published_new_best_buffer_without_playback(self):
        debugger, loads = actor_memory()
        debugger.put_word(0x4fa030, 2)
        for address, value in ((0x11000, 0), (0x12000, 1)):
            debugger.put(address + 0x138, bytes([value]))
            debugger.put(address + 0x140, bytes([value]))
        snapshot = actors.capture_snapshot(debugger, loads)
        self.assertEqual([actor["root_mask"] for actor in snapshot["actors"]], [1, 0, 1])
        self.assertEqual(snapshot["actors"][2]["object_to_world"], fixture()["car_matrix"])

    def test_livery_follows_observed_remap_root_and_flags_instead_of_actor_parity(self):
        debugger, loads = actor_memory()
        debugger.put_word(0x1004, 0)
        loads = {slot: {"model_id": 7, "ordinary_material": 1} for slot in range(3)}
        debugger.put(0x100, struct.pack("<6I", 0x44e1c0, 0x11000, 0x12000, 2, 22, 2))
        remap = actors.capture_livery(debugger, 0x100)
        snapshot = actors.capture_snapshot(debugger, loads, liveries=[remap])
        self.assertEqual([actor["second_driver"] for actor in snapshot["actors"]], [False, False, True])
        self.assertEqual(snapshot["actors"][2]["livery_remaps"][0]["number_b"], 22)
        self.assertEqual([actors.remap_livery(actor) for actor in snapshot["actors"]], [False, True, True])

    def test_noqual_paired_num2_and_tt_authored_num22_use_the_original_call_audit(self):
        # Small vectors from NoQual14 and TT0: both are first-driver Renaults,
        # but only44df6e called49a780 to replace node15's Num22 with Num2.
        remap = {"first_root": 0x10000, "second_root": 0x11000, "number_a": 2, "number_b": 22,
                 "flags": 2, "caller": 0x44e1c0, "provenance": "49a780-entry"}
        paired = {"slot": 14, "physical_root": 0x10000, "second_driver": False, "livery_remaps": [remap]}
        unpaired = {"slot": 0, "physical_root": 0x12000, "second_driver": False, "livery_remaps": []}
        self.assertTrue(actors.remap_livery(paired))
        self.assertFalse(actors.remap_livery(unpaired))
        # Actor numbering cannot override either observed call history.
        self.assertTrue(actors.remap_livery({**paired, "slot": 1}))
        self.assertFalse(actors.remap_livery({**unpaired, "slot": 15}))
        for audit in (None, {}, [remap, remap], [{**remap, "flags": True}],
                      [{**remap, "first_root": 0x13000}], [{**remap, "provenance": "actor-parity"}]):
            with self.subTest(audit=audit), self.assertRaises(ValueError):
                actors.remap_livery({**paired, "livery_remaps": audit})

    def test_duplicate_bonus_low_ids_retain_parent_and_sibling_path_identity(self):
        debugger = EffectMemory()
        root, child, sibling = 0x10000, 0x11000, 0x12000
        put_scene_node(debugger, root, child=child, identity=509)
        put_scene_node(debugger, child, sibling=sibling, parent=root, identity=709)
        put_scene_node(debugger, sibling, parent=root, identity=709)
        nodes = actors.capture_nodes(debugger, root, {})
        self.assertEqual([(node["parent"], node["sibling"], node["id"]) for node in nodes], [(-1, 0, 509), (0, 0, 709), (0, 1, 709)])
        snapshot = actors.capture_snapshot(*actor_memory())
        snapshot["actors"][0]["nodes"] = nodes
        actors.validate_snapshot({"render_actors": snapshot})
        nodes[2]["sibling"] = 0
        with self.assertRaises(ValueError):
            actors.validate_snapshot({"render_actors": snapshot})

    def test_shadow_identity_uses_exact_actor_submission_record(self):
        debugger, loads = actor_memory()
        debugger.put_word(0x4db028, 999)
        with self.assertRaisesRegex(ValueError, "exact submitting actor"):
            actors.capture_snapshot(debugger, loads)
        debugger, loads = actor_memory()
        debugger.put(0x4ed880, b"\0")
        with self.assertRaisesRegex(ValueError, "submission masks"):
            actors.capture_snapshot(debugger, loads)

    def test_name_texture_audit_resolves_actual_group_handle_and_bound_surface(self):
        debugger = EffectMemory()
        debugger.put_word(0x4b9b94, 0x2000)
        debugger.put_word(0x5abbd8, 1)
        debugger.put_word(0x2000 + 0x2240, 0x6000)
        debugger.put_word(0x6000 + 0x14, 123)
        debugger.put_word(0x6000 + 0xc, 0x7000)
        debugger.put(0x6000 + 0x1a, b"List2\0")
        debugger.put_word(0x7000 + 4, 456)
        put_scene_node(debugger, 0x10000, identity=15)
        debugger.put_word(0x10000 + 0xc, 0x11000)
        debugger.put(0x11000 + 0x14, bytes([1, 2, 0, 0]))
        debugger.put_word(0x11000 + 8, 0x12000)
        debugger.put_word(0x12000 + 0x1d0, 1)
        debugger.put(0x12000 + 0x14, struct.pack("<4I", 123, 456, 0, 0))
        nodes = actors.capture_nodes(debugger, 0x10000, actors.texture_bindings(debugger))
        self.assertEqual(nodes[0]["variant"], 1)
        self.assertEqual(nodes[0]["name_bindings"], [{"group": 0, "texture": "List2", "handle": 123, "surface": 456}])

    def test_vectra2_baked_side_and_rear_glass_require_exact_observed_binders(self):
        # Original slot5 MEDIUM: source handles512=vectra2,480=SIDEG,482=WSCRN.
        nodes = [{"id": identity, "variant": 0, "name_bindings": [],
                  "material_bindings": [{"texture": texture} for texture in textures]}
                 for identity, textures in ((15, ("vectra2", "SIDEG")), (16, ("vectra2", "SIDEG")), (21, ("WSCRN",)))]
        actors.validate_name_bindings(10, nodes)
        actors.validate_name_bindings(4, nodes, lod=1)
        with self.assertRaisesRegex(ValueError, "unclassified active texture"):
            actors.validate_name_bindings(7, nodes)
        nodes[0]["material_bindings"][1]["texture"] = "another-window"
        with self.assertRaisesRegex(ValueError, "unclassified active texture"):
            actors.validate_name_bindings(10, nodes)

    def test_renault_high_damage_two_removes_name_faces_but_keeps_baked_glass(self):
        # Canonical HIGH CRC e0fffe5d: variants2/3 use RENBody30/33/41;
        # unlike variants0/1, those meshes contain no ListA/B triangles.
        nodes = [{"id": identity, "variant": 2, "name_bindings": [],
                  "material_bindings": [{"texture": glass, "handle": 480}, {"texture": None, "handle": 0, "surface": 123}]}
                 for identity, glass in ((15, "SIDEG"), (16, "SIDEG"), (21, "WSCRN"))]
        actors.validate_name_bindings(7, nodes)
        for resource, variant in ((7, 1), (6, 2)):
            with self.subTest(resource=resource, variant=variant), self.assertRaisesRegex(ValueError, "unclassified active texture"):
                actors.validate_name_bindings(resource, [{**node, "variant": variant} for node in nodes])
        nodes[0]["material_bindings"][-1]["handle"] = 999
        with self.assertRaisesRegex(ValueError, "unclassified active texture"):
            actors.validate_name_bindings(7, nodes)

    def test_actor_wire_follows_caption_with_exact_signed_fields_and_source_local_world(self):
        snapshot = actors.capture_snapshot(*actor_memory())
        snapshot["actors"][0]["steering_angle"] = -32768
        snapshot["actors"][0]["wheel_angles"] = [-32768, 32767, 7, -9]
        snapshot["actors"][0]["damage"] = [3] * 17
        state = fixture()
        state["render_actors"] = snapshot
        rows = []
        actors.append_rows(rows, state)
        self.assertEqual(rows[:3], ["1", "3 2 1 1 0", "0 0 7 0 1 1 0 0 1 1 1"])
        self.assertEqual(rows[4], "-32768 -32768 32767 7 -9 0 0")
        self.assertEqual(rows[5], "0 0 0")
        self.assertEqual(rows[7], '"DRIVER"')
        node = rows[8].split()
        self.assertEqual(node[:5], ["-1", "0", "501", "1", "-1"])
        self.assertEqual([float(value) for value in node[5:21]], snapshot["actors"][0]["nodes"][0]["local"])
        self.assertEqual([float(value) for value in node[21:]], [99.5] * 16)
        self.assertEqual(compare.frame_text(state).splitlines()[-len(rows) - 5:-5], rows)
        self.assertEqual(compare.frame_text(fixture()).splitlines()[-1], "0")

    def test_actor_snapshot_rejects_widths_roles_paths_nonfloat32_and_invented_ghosts(self):
        baseline = actors.capture_snapshot(*actor_memory())
        for field, value in (("role", 1), ("model_id", 11), ("lod", 3), ("root_mask", 256), ("steering_angle", 32768),
                             ("wheel_angles", [0] * 3), ("damage", [4] * 17), ("broken", [2] * 4),
                             ("object_to_world", [0.1] * 16), ("shadow_ordinal", 1), ("name", "X" * 10), ("lights", [2, 0, 0])):
            with self.subTest(field=field):
                snapshot = copy.deepcopy(baseline)
                snapshot["actors"][0][field] = value
                with self.assertRaises(ValueError):
                    actors.validate_snapshot({"render_actors": snapshot})
        for field, value in (("ghost_owner", 0), ("mode", 3), ("local_slot", 1), ("source_view", 6)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                actors.validate_snapshot({"render_actors": {**baseline, field: value}})

    def test_noqual_capture_is_explicit_and_requires_actual_single_race_menu(self):
        parsed = capture.parse_arguments(["--session", "single-race", "--no-qualifying"])
        self.assertTrue(parsed.no_qualifying)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            capture.parse_arguments(["--no-qualifying"])

    def test_tt_acquisition_requires_consistent_session_and_saved_ghost_selection(self):
        self.assertTrue(capture.parse_arguments(["--tt-lap", "--tt-save-best"]).tt_lap)
        self.assertTrue(capture.parse_arguments(["--load-best", "--tt-wait-ghost"]).tt_wait_ghost)
        for arguments in (["--tt-save-best"], ["--tt-wait-ghost"], ["--tt-lap", "--tt-wait-ghost"],
                          ["--session", "single-race", "--tt-lap"], ["--tt-max-updates", "0"]):
            with self.subTest(arguments=arguments), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.parse_arguments(arguments)

    def test_full_original_lap_has_its_own_capture_deadline_and_honors_explicit_limit(self):
        self.assertEqual(capture.parse_arguments([]).timeout, 150)
        self.assertEqual(capture.parse_arguments(["--tt-lap"]).timeout, 600)
        self.assertEqual(capture.parse_arguments(["--tt-lap", "--timeout", "90"]).timeout, 90)

    def test_load_best_uses_real_menu_for_both_saved_and_fresh_lap_acquisition(self):
        for initial, enabled in ((0, True), (1, False), (0, False), (1, True)):
            with self.subTest(initial=initial, enabled=enabled):
                words = {0x4b0660: 0x1000, 0x52ee18: 1, 0x52c21c: 0xe, 0x1004: 2,
                         0x4b71e8: 0x100, 0x1030: initial}
                debugger = mock.Mock()
                debugger.word.side_effect = words.__getitem__
                def press(*unused):
                    words[0x1030] ^= 1
                with mock.patch.object(capture.reference, "menu_key", side_effect=press) as keyboard:
                    selected = capture.select_load_best(debugger, {}, enabled)
                self.assertEqual(selected, {"kind": "load-best-selection", "requested_load_best": enabled, "keys_only": True})
                self.assertEqual(words[0x1030], int(enabled))
                self.assertEqual(keyboard.call_count, int(initial != enabled))
                if initial != enabled:
                    keyboard.assert_called_once_with(debugger, {}, 0x469979, "Right")

    def test_tt_acquisition_records_real_boundary_snapshots_and_releases_keys(self):
        debugger = mock.Mock()
        game = bytearray(0x124)
        struct.pack_into("<h", game, 0xa2, 5)
        struct.pack_into("<h", game, 0xa8, 2)
        initial = {"boundary": "421aa0-entry", "source_mode": 2, "actor": 0, "allocated_actor_count": 3,
                   "track_id": 0, "weather": 1, "car_id": 7, "transmission_mode": 2, "steering_profile": 1,
                   "configuration_pointer": 0x1000, "update_counter": 10}
        consumed = {**initial, "boundary": "415090-entry", "update_counter": 11}
        post = {**consumed, "boundary": "421aa0-return", "gameplay_hex": game.hex(), "session_state": 6}
        debugger.snapshot.side_effect = [initial, consumed, post]
        words = {0x2000: 0x421abc, 0x4fa030: 2, 0x52b3e4: 1}
        best = iter((999999, 120000))
        debugger.word.side_effect = lambda address: next(best) if address == 0x4f9f00 else words[address]
        debugger.memory.side_effect = lambda address, size: bytes(size)
        debugger.breakpoint.return_value = 4
        policy = SimpleNamespace(parameters={"points_crc32": "12345678"}, choose=lambda index, state: ({"Up"}, {"human": 1}))
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(capture.reference, "owned_game_processes", return_value=[77]), \
                mock.patch.object(capture.reference.FLEET, "route_descriptor", return_value=(0x3004, 6, 0, 0x3188)), \
                mock.patch.object(capture.reference, "LocalLapPolicy", return_value=policy), \
                mock.patch.object(capture.reference, "keyboard") as keyboard, \
                mock.patch.object(capture, "stack", return_value=0x2000), \
                mock.patch.object(capture, "returned") as returned, contextlib.redirect_stdout(io.StringIO()):
            result = capture.acquire_tt_ghost(debugger, {}, Path(directory), 1)
            rows = Path(directory, "trace.jsonl").read_text().splitlines(keepends=True)
            self.assertEqual(json.loads(rows[1])["pre"], initial)
            self.assertEqual(json.loads(rows[1])["consumed"], consumed)
            self.assertEqual(json.loads(rows[1])["post"], post)
            self.assertEqual(json.loads(rows[-1])["prefix_crc32"], f"{capture.zlib.crc32(''.join(rows[:-1]).encode()):08x}")
            self.assertEqual(result["best_ms"], 120000)
            self.assertEqual(result["route_points_seen"], [5])
            self.assertEqual(keyboard.call_args_list, [mock.call({}, "keydown", "Up"), mock.call({}, "keyup", "Up")])
            debugger.stop_at.assert_called_once_with(0x415090)
            returned.assert_called_once_with(debugger, 0x421abc)
            debugger.close_memory.assert_called_once()

    def test_render_wait_observes_qualifying_to_active_handoff_without_predicting_it(self):
        debugger = mock.Mock()
        debugger.breakpoint.side_effect = (4, 5)
        debugger.command.side_effect = ("Stopped at0x004480e0", "Stopped at0x00448440", "", "")
        self.assertEqual(capture.wait_session_render(debugger, 2, (0x4480e0, 0x448440)), 0x448440)
        self.assertEqual(debugger.confirm_pause.call_count, 2)
        self.assertEqual(debugger.command.call_args_list[-2:], [mock.call("delete break 4"), mock.call("delete break 5")])

    def test_human_tt_trace_selects_existing_stable_states_without_changing_lifecycle(self):
        game = bytearray(0x124)
        struct.pack_into("<I", game, 8, 1)
        phases = {phase: {"transmission_mode": 2, "gameplay_hex": game.hex(), "session_state": 6,
                          "update_counter": 10, "track_id": 0}
                  for phase in ("pre", "consumed", "post")}
        approach = copy.deepcopy(phases)
        approach["pre"]["transmission_mode"] = 0
        header = {"type": "header", "initial_configuration": {"track_id": 0, "transmission_mode": 0}}
        records = [{"type": "update", "id": 0, **approach}, {"type": "update", "id": 1, **phases}]
        prefix = "".join(json.dumps(row) + "\n" for row in (header, *records)).encode()
        raw = prefix + (json.dumps({"type": "complete", "updates": 2, "prefix_crc32": f"{capture.zlib.crc32(prefix):08x}"}) + "\n").encode()
        with tempfile.TemporaryDirectory() as directory:
            source, destination = Path(directory, "lifecycle.jsonl"), Path(directory, "human.jsonl")
            source.write_bytes(raw)
            result = capture.write_human_tt_trace(source, destination)
            rows = [json.loads(line) for line in destination.read_text().splitlines()]
            self.assertEqual(source.read_bytes(), raw)
            self.assertEqual(result["updates"], 1)
            self.assertEqual(rows[0]["initial_configuration"]["transmission_mode"], 2)
            self.assertEqual((rows[1]["id"], rows[1]["source_update_id"]), (0, 1))
            for phase in phases:
                self.assertEqual(rows[1][phase], phases[phase])


def cockpit_fixture():
    return {"car_id": 7, "wheel_angle": -32768, "wiper_angle": 113, "duplicate_wiper": False,
            "roles": [{"actual_mask": 1, "manual_mask": 15, "local": actors.IDENTITY[:], "parent_local": actors.IDENTITY[:]} for _ in range(5)]}


class CockpitCaptureTests(unittest.TestCase):
    def test_view_selection_uses_real_c_edges_and_observed_update_progress(self):
        debugger = mock.Mock()
        current = {"view": 2, "counter": 10}
        debugger.word.side_effect = lambda address: {0x4b0660: 0x1000, 0x15f8: 9, 0x4c3a66: current["view"], 0x4d383c: current["counter"]}[address]
        def key(*_args):
            current["view"] = {2: 3, 3: 4, 4: 1}[current["view"]]
            current["counter"] += 2
        with mock.patch.object(capture.reference, "menu_key", side_effect=key) as press, mock.patch.object(capture.reference, "keyboard"):
            selected = capture.select_view(debugger, {}, 1)
        self.assertEqual(selected["observed_views"], [2, 3, 4, 1])
        self.assertEqual(selected["update_counters"], [10, 12, 14, 16])
        self.assertEqual(press.call_args_list, [mock.call(debugger, {}, 0x421aa0, "c")] * 3)
        self.assertEqual(capture.parse_arguments(["--view", "4"]).view, 4)

    def test_cockpit_capture_retains_local_parents_and_actual_masks_without_world_injection(self):
        debugger, _ = actor_memory()
        state = {"source_view": 1, "actor": 0, "car_id": 7}
        debugger.put_word(0x4d537c, 1)
        debugger.put(0x4d54e0, struct.pack("<hH", -32768, 9))
        debugger.put(0x4d5360, struct.pack("<hH", 113, 0xcafe))
        for index, global_address in enumerate(actors.COCKPIT_ROOTS):
            address = 0x20000 + index * 0x2000
            debugger.put_word(global_address, address)
            put_scene_node(debugger, address, parent=0x30000 if index == 3 else 0x10000, identity=0)
            debugger.put(address + 0x138, b"\x01")
        put_scene_node(debugger, 0x30000, parent=0x10000, identity=0)
        debugger.put(0x30000 + 0x138, b"\0")
        snapshot = actors.capture_cockpit(debugger, state)
        self.assertEqual((snapshot["wheel_angle"], snapshot["wiper_angle"]), (-32768, 113))
        self.assertEqual([role["actual_mask"] for role in snapshot["roles"]], [1, 1, 1, 0, 1])
        self.assertEqual(snapshot["roles"][0]["parent_local"], actors.IDENTITY)
        self.assertEqual(snapshot["roles"][3]["parent_local"], snapshot["roles"][3]["local"])
        self.assertEqual(snapshot["roles"][0]["nodes"][0]["world"], [99.5] * 16)
        self.assertNotEqual(snapshot["roles"][0]["local"], [99.5] * 16)
        debugger.put_word(0x4d537c, 0)
        with self.assertRaisesRegex(ValueError, "second wiper"):
            actors.capture_cockpit(debugger, state)

    def test_wire7_appends_exact_published_cockpit_roles_after_actors(self):
        state = {**fixture(), "source_view": 1, "cockpit": cockpit_fixture()}
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows[0], "TOCA_RENDER_12")
        self.assertEqual(rows[-11:-9], ["1", "7 -32768 113 0"])
        self.assertEqual([float(value) for value in rows[-9].split()[2:18]], actors.IDENTITY)
        self.assertEqual(len(rows[-9].split()), 34)
        state["detail"] = 3
        state["selected_lod"] = "high"
        state["lod_roots"] = [{"name": name, "address": address, "visible_mask": 0} for name, address in (("high", 100), ("medium", 200), ("low", 300))]
        state["car_nodes"][0]["address"] = 100
        compare.validate(state)
        state["lod_roots"][0]["visible_mask"] = 1
        with self.assertRaisesRegex(ValueError, "LOD eligibility"):
            compare.validate(state)

    def test_cockpit_snapshot_rejects_missing_roles_wrong_owner_masks_and_inexact_floats(self):
        state = {**fixture(), "source_view": 1, "cockpit": cockpit_fixture()}
        for field, value in (("car_id", 0), ("wheel_angle", 32768), ("wiper_angle", -32769), ("duplicate_wiper", True), ("roles", [])):
            with self.subTest(field=field), self.assertRaises(ValueError):
                actors.validate_cockpit({**state, "cockpit": {**state["cockpit"], field: value}})
        for field, value in (("actual_mask", 16), ("manual_mask", 16), ("manual_mask", True), ("local", [0.1] * 16), ("parent_local", [2.] * 16)):
            bad = copy.deepcopy(state)
            bad["cockpit"]["roles"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                actors.validate_cockpit(bad)
        with self.assertRaises(ValueError):
            actors.validate_cockpit({**state, "cockpit": None})

    def test_damage_acquisition_requires_noqual_and_matching_published_mesh_variant(self):
        parsed = capture.parse_arguments(["--session", "single-race", "--no-qualifying", "--damage"])
        self.assertTrue(parsed.damage)
        for arguments in (["--damage"], ["--session", "single-race", "--damage"], ["--damage-max-updates", "0"]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.parse_arguments(arguments)
        for published in (0, 2):
            debugger = mock.Mock()
            pre = {"source_mode": 0, "allocated_actor_count": 16, "actor": 14, "update_counter": 10}
            body = bytearray(0x53c)
            body[0x529] = 2
            post = {**pre, "update_counter": 11, "configuration_pointer": 0x1000, "session_state": 6, "body_hex": body.hex()}
            debugger.snapshot.side_effect = [pre, post]
            raw = bytearray(17 * 16)
            raw[3 * 16 + 14] = 2
            debugger.memory.side_effect = lambda address, size: bytes(raw) if address == 0x4c25a8 else bytes([published, 4, 0, 0]) if address == 0x4014 else bytes(size)
            words = {0x4be220: 0, 0x2000: 0x421abc, 0x1328: 3, 0x4c26c8 + 3 * 0x40 + 14 * 4: 0x3000, 0x300c: 0x4000,
                     0x4bdbd8 + 14 * 0x34 + 0x30: 1}
            debugger.word.side_effect = words.__getitem__
            policy = SimpleNamespace(parameters={"keys_only": True}, choose=lambda index, state: ({"Up", "Right"}, {"phase": "wall-impact"}))
            with tempfile.TemporaryDirectory() as directory, \
                    mock.patch.object(capture.reference, "owned_game_processes", return_value=[77]), \
                    mock.patch.object(capture.reference, "FleetDetachmentPolicy", return_value=policy), \
                    mock.patch.object(capture.reference, "keyboard") as keyboard, \
                    mock.patch.object(capture, "stack", return_value=0x2000), \
                    mock.patch.object(capture, "returned"), contextlib.redirect_stdout(io.StringIO()):
                if published == 2:
                    result = capture.acquire_damage(debugger, {}, Path(directory), 1)
                    self.assertEqual(result["active_variants"], [{"damage_slot": 3, "level": 2, "variant": 2, "node": 0x3000}])
                    self.assertEqual(result["barrier_updates"], [0])
                    self.assertEqual(result["car_contact_updates"], [0])
                    journal = json.loads((Path(directory) / "damage-driving.jsonl").read_text())
                    self.assertEqual((journal["barrier_active"], journal["car_contact_count"]), (1, 2))
                else:
                    with self.assertRaisesRegex(RuntimeError, "positive selected damage variant"):
                        capture.acquire_damage(debugger, {}, Path(directory), 1)
                self.assertEqual(keyboard.call_args_list[-2:], [mock.call({}, "keyup", "Right"), mock.call({}, "keyup", "Up")])
                debugger.close_memory.assert_called_once()


class SkidCaptureTests(unittest.TestCase):
    def memory(self):
        debugger = EffectMemory()
        debugger.put_word(0x4f11c0, 2)
        debugger.put_word(0x4edc88, 1)
        debugger.put_word(0x597fa4, 1)
        debugger.put_word(0x597e90, 1 << 5)
        points = [-2147483648, 2147483647, -65537, 1, 2, 3, 4, 5, 6, 7, 8, 9]
        for address, sector, mask in ((0x4edc90, 5, 1), (0x4edcc8, 6, 1), (0x4f11c8, 5, 2)):
            debugger.put(address, struct.pack("<12i4BHBB", *points, 90, 66, 41, 220, sector, mask, 0))
        return debugger

    def test_skid_ring_then_active_preserves_q16_argb_and_actual_sector_visibility(self):
        snapshot = capture.capture_skid_marks(self.memory())
        self.assertEqual((snapshot["persistent_count"], snapshot["active_count"]), (2, 1))
        self.assertEqual([(q["queue"], q["ordinal"], q["rendered"]) for q in snapshot["quads"]], [(0, 0, True), (0, 1, False), (1, 0, False)])
        quad = snapshot["quads"][0]
        self.assertEqual(quad["color"], 0xdc5a4229)
        self.assertEqual(quad["vertices_q16"][0], [-2147483648, 2147483647, -65537])
        rows = []
        actors.append_skid_rows(rows, {"skid_marks": snapshot})
        self.assertEqual(rows[:2], ["1", "2 1"])
        self.assertEqual(rows[2].split()[:6], ["5", "1", str(0xdc5a4229), "-2147483648", "2147483647", "-65537"])
        self.assertEqual(rows[3].split()[:2], ["6", "0"])
        self.assertEqual(rows[4].split()[:2], ["5", "0"])
        self.assertEqual(snapshot["quads"][1]["visibility"], 1)
        self.assertEqual(snapshot["quads"][2]["visibility"], 2)
        state = {**fixture(), "skid_marks": snapshot}
        self.assertEqual(compare.frame_text(state).splitlines()[-len(rows) - 3:-3], rows)

    def test_skid_filter_override_and_viewport_use_source_gates(self):
        for address, value in ((0x597fb0, 1), (0x597fa4, 0)):
            debugger = self.memory()
            debugger.put_word(address, value)
            self.assertEqual([q["rendered"] for q in capture.capture_skid_marks(debugger)["quads"]], [True, True, False])
        self.assertEqual([q["rendered"] for q in capture.capture_skid_marks(self.memory(), 1)["quads"]], [False, False, True])

    def test_skid_snapshot_rejects_widths_counts_reordering_and_invented_eligibility(self):
        baseline = capture.capture_skid_marks(self.memory())
        for field, value in (("persistent_count", 101), ("active_count", 100), ("viewport", 4), ("sector_visibility_hex", "00"),
                             ("sector_visibility_prefix", -1), ("sector_visibility_disabled", True)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                actors.skid_snapshot({"skid_marks": {**baseline, field: value}})
        for field, value in (("queue", 1), ("ordinal", 1), ("color", -1), ("visibility", 16), ("sector", 2048),
                             ("rendered", False), ("vertices_q16", [[2147483648, 0, 0]] * 4)):
            snapshot = copy.deepcopy(baseline)
            snapshot["quads"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                actors.skid_snapshot({"skid_marks": snapshot})
        for address, value in ((0x4f11c0, 101), (0x4edc88, 100)):
            debugger = self.memory()
            debugger.put_word(address, value)
            with self.assertRaises(ValueError):
                capture.capture_skid_marks(debugger)

    def test_skid_outside_sector_uses_signed_prefix_and_rejects_a_submitted_sentinel(self):
        debugger = self.memory()
        debugger.put(0x4edc90 + 52, struct.pack("<H", 0xffff))
        self.assertFalse(capture.capture_skid_marks(debugger)["quads"][0]["rendered"])
        debugger.put_word(0x597e8c, 0x80000000)
        with self.assertRaisesRegex(ValueError, "no supported authored sector"):
            capture.capture_skid_marks(debugger)

    def test_optional_skid_snapshot_keeps_absence_distinct_from_empty_queues(self):
        rows = []
        actors.append_skid_rows(rows, {})
        self.assertEqual(rows, ["0"])
        snapshot = capture.capture_skid_marks(EffectMemory())
        rows = []
        actors.append_skid_rows(rows, {"skid_marks": snapshot})
        self.assertEqual(rows, ["1", "0 0"])


class SparkCaptureTests(unittest.TestCase):
    def memory(self, head=2, tail=0, pool_count=2):
        debugger = EffectMemory()
        debugger.put_word(0x4be2c0, head)
        debugger.put_word(0x4c03c4, tail)
        debugger.put_word(0x4bea08, pool_count)
        count = (head - tail) % 64 if pool_count else 0
        debugger.put_word(0x4c0ecc, count)
        debugger.put_word(0x4ed988, 2)
        debugger.put(0x4ed908, bytes([255, 240, 150, 255]))
        for ordinal in range(count):
            debugger.put(0x4be308 + ordinal * 28,
                         struct.pack("<6iB3x", -2147483648, 2147483647, -65537, 11, -12, 13, 1 if ordinal == 0 else 2))
        return debugger

    def test_actual_export_preserves_endpoint_order_q16_color_and_visibility(self):
        snapshot = capture.capture_spark_lines(self.memory())
        self.assertEqual((snapshot["count"], snapshot["color_index"], snapshot["color"]), (2, 2, 0xfffff096))
        first = snapshot["lines"][0]
        self.assertEqual(first["from_q16"], [11, -12, 13])
        self.assertEqual(first["to_q16"], [-2147483648, 2147483647, -65537])
        self.assertEqual([line["rendered"] for line in snapshot["lines"]], [True, False])
        self.assertEqual([line["rendered"] for line in capture.capture_spark_lines(self.memory(), 1)["lines"]], [False, True])
        rows = []
        actors.append_spark_rows(rows, {"spark_lines": snapshot})
        self.assertEqual(rows[:3], ["1", "2", f"1 {0xfffff096} 11 -12 13 -2147483648 2147483647 -65537"])
        self.assertEqual(rows[3].split()[:2], ["0", str(0xfffff096)])
        self.assertEqual(compare.frame_text({**fixture(), "spark_lines": snapshot}).splitlines()[-len(rows)-2:-2], rows)

    def test_wrapped_export_and_full_ring_empty_quirk_use_exported_rows(self):
        wrapped = capture.capture_spark_lines(self.memory(head=1, tail=63))
        self.assertEqual([line["ordinal"] for line in wrapped["lines"]], [0, 1])
        full = capture.capture_spark_lines(self.memory(head=9, tail=9, pool_count=64))
        self.assertEqual((full["pool_count"], full["count"], full["lines"]), (64, 0, []))
        rows = []
        actors.append_spark_rows(rows, {"spark_lines": full})
        self.assertEqual(rows, ["1", "0"])
        rows = []
        actors.append_spark_rows(rows, {})
        self.assertEqual(rows, ["0"])

    def test_source_export_rejects_inconsistent_counts_widths_order_and_eligibility(self):
        baseline = capture.capture_spark_lines(self.memory())
        for field, value in (("count", 1), ("pool_count", 64), ("ring_head", 64), ("ring_tail", -1),
                             ("viewport", 4), ("color", -1), ("color_index", 3), ("color_index", True)):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                actors.spark_snapshot({"spark_lines": {**baseline, field: value}})
        for field, value in (("ordinal", 1), ("visibility", 16), ("rendered", False),
                             ("from_q16", [2147483648, 0, 0]), ("to_q16", [0, 0]), ("to_q16", [True, 0, 0])):
            snapshot = copy.deepcopy(baseline)
            snapshot["lines"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                actors.spark_snapshot({"spark_lines": snapshot})
        for address, value in ((0x4c0ecc, 65), (0x4ed988, 3), (0x4bea08, 1)):
            debugger = self.memory()
            debugger.put_word(address, value)
            with self.subTest(address=address), self.assertRaises(ValueError):
                capture.capture_spark_lines(debugger)


class QueuedWorldLineTests(unittest.TestCase):
    def memory(self):
        debugger = EffectMemory()
        debugger.put_word(0x4e8724, 2)
        for ordinal in range(2):
            debugger.put(0x4de918 + ordinal * 32,
                         struct.pack("<6iI4B", -2147483648, 2147483647, -65537, 11, -12, 13,
                                     0xdeadbeef, 24 + ordinal, 70, 170, 255))
        return debugger

    def test_actual_pool_preserves_rgba_q16_and_submission_order(self):
        snapshot = capture.capture_queued_lines(self.memory())
        self.assertEqual(snapshot["count"], 2)
        self.assertEqual(snapshot["lines"][0], {"ordinal": 0, "color": 0xff1846aa,
                                              "from_q16": [-2147483648, 2147483647, -65537], "to_q16": [11, -12, 13]})
        self.assertEqual(snapshot["lines"][1]["color"], 0xff1946aa)
        rows = []
        actors.append_queued_line_rows(rows, {"queued_lines": snapshot})
        self.assertEqual(rows[:3], ["1", "2", f"{0xff1846aa} -2147483648 2147483647 -65537 11 -12 13"])
        self.assertEqual(compare.frame_text({**fixture(), "queued_lines": snapshot}).splitlines()[-len(rows)-1:-1], rows)

    def test_absent_and_empty_queues_remain_distinct(self):
        rows = []
        actors.append_queued_line_rows(rows, {})
        self.assertEqual(rows, ["0"])
        rows = []
        snapshot = capture.capture_queued_lines(EffectMemory())
        actors.append_queued_line_rows(rows, {"queued_lines": snapshot})
        self.assertEqual(rows, ["1", "0"])

    def test_queue_rejects_counts_widths_and_reordering(self):
        baseline = capture.capture_queued_lines(self.memory())
        for value in (-1, 18, True, 1):
            with self.subTest(count=value), self.assertRaises(ValueError):
                actors.queued_lines_snapshot({"queued_lines": {**baseline, "count": value}})
        for field, value in (("ordinal", 1), ("color", 0x100000000), ("from_q16", [2147483648, 0, 0]),
                             ("to_q16", [0, 0]), ("to_q16", [True, 0, 0])):
            snapshot = copy.deepcopy(baseline)
            snapshot["lines"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                actors.queued_lines_snapshot({"queued_lines": snapshot})
        debugger = self.memory()
        debugger.put_word(0x4e8724, 18)
        with self.assertRaises(ValueError):
            capture.capture_queued_lines(debugger)


class PresentationHistoryTests(unittest.TestCase):
    def prior(self, ordinal):
        state = fixture()
        state.pop("presentation_barrier")
        state["history_ordinal"] = ordinal
        return state

    def test_history_uses_ordered_numeric_frames_without_previous_source_images(self):
        state = fixture()
        state["presentation_history"] = [self.prior(0), self.prior(1)]
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows.count("TOCA_RENDER_12"), 3)
        first_history = rows.index("TOCA_RENDER_12", 1)
        self.assertEqual(rows[first_history - 1], "2")
        self.assertEqual(rows[-1], "0")
        self.assertEqual(compare.frame_text(fixture()).splitlines()[-1], "0")

    def test_history_rejects_nesting_reordering_other_scenes_and_unstable_bodies(self):
        for changed in ({"history_ordinal": 1}, {"track_id": 1}, {"body_unchanged_during_present": False},
                        {"presentation_history": [self.prior(0)]}):
            state = fixture()
            state["presentation_history"] = [{**self.prior(0), **changed}]
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                compare.frame_text(state)
        state = fixture()
        state["presentation_history"] = [self.prior(0)] * 65
        with self.assertRaises(ValueError):
            compare.frame_text(state)
        state = fixture()
        state.pop("presentation_barrier")
        state["presentation_history"] = [self.prior(0)]
        with self.assertRaisesRegex(ValueError, "presentation barrier"):
            compare.frame_text(state)

    def test_capture_retains_each_prior_present_and_leaves_final_for_barrier(self):
        debugger = mock.Mock()
        debugger.memory.return_value = b"same-body"
        debugger.word.return_value = 0x482700
        frames = [{"actor": 0, "ordinal": ordinal} for ordinal in range(3)]
        with mock.patch.object(capture, "wait_session_render", return_value=0x448a10) as wait, \
                mock.patch.object(capture, "capture_frame", side_effect=frames) as read, \
                mock.patch.object(capture, "returned") as returned, mock.patch.object(capture, "stack", return_value=0x2000):
            entry, final = capture.capture_presented_frames(debugger, "families", "actors", "remaps", 3, (0x448a10,))
        self.assertEqual(entry, 0x448a10)
        self.assertIs(final, frames[2])
        self.assertEqual(final["presentation_history"], frames[:2])
        self.assertEqual([frame["history_ordinal"] for frame in final["presentation_history"]], [0, 1])
        self.assertTrue(all(frame["body_unchanged_during_present"] for frame in final["presentation_history"]))
        self.assertEqual(returned.call_args_list, [mock.call(debugger, 0x482700)] * 2)
        self.assertEqual(wait.call_args_list, [mock.call(debugger, 1, (0x448a10,))] * 3)
        self.assertEqual(read.call_args_list, [mock.call(debugger, "families", "actors", "remaps")] * 3)

    def test_camera_keys_record_intermediate_world_presents_and_restore_debugger(self):
        debugger = mock.Mock()
        original = debugger.command
        original.side_effect = ["Stopped at0x00489069", "Stopped at0x00421aa0", ""]
        debugger.breakpoint.return_value = 8
        debugger.word.return_value = 0x482700
        debugger.memory.return_value = b"same-body"
        frame = {"actor": 0, "source_view": 5}
        def choose(dbg, environment, target):
            self.assertEqual(dbg.command("cont", 30), "Stopped at0x00421aa0")
            return {"requested_view": target, "keys_only": True}
        with mock.patch.object(capture, "select_view", side_effect=choose), \
                mock.patch.object(capture, "capture_frame", return_value=frame) as read, \
                mock.patch.object(capture, "returned") as returned, mock.patch.object(capture, "stack", return_value=0x2000):
            selection, previous = capture.select_view_with_history(debugger, {}, 1, "families", "actors", "remaps")
        self.assertEqual(selection["requested_view"], 1)
        self.assertEqual(previous, [frame])
        self.assertEqual((frame["history_ordinal"], frame["body_unchanged_during_present"]), (0, True))
        read.assert_called_once_with(debugger, "families", "actors", "remaps", current_draw=True)
        returned.assert_called_once_with(debugger, 0x482700)
        self.assertIs(debugger.command, original)
        self.assertEqual(original.call_args_list[-1], mock.call("delete break 8"))

    def test_capture_rejects_a_body_change_during_previous_present(self):
        debugger = mock.Mock()
        debugger.memory.side_effect = [b"before", b"after"]
        debugger.word.return_value = 0x482700
        with mock.patch.object(capture, "wait_session_render", return_value=0x448a10), \
                mock.patch.object(capture, "capture_frame", return_value={"actor": 0}), \
                mock.patch.object(capture, "returned"), mock.patch.object(capture, "stack", return_value=0x2000), \
                self.assertRaisesRegex(RuntimeError, "changed its captured body"):
            capture.capture_presented_frames(debugger, None, None, (), 2, (0x448a10,))


class SessionCaptionTests(unittest.TestCase):
    def test_absent_caption_keeps_explicit_marker_after_empty_animation(self):
        state = fixture()
        self.assertIsNone(compare.session_caption(state))
        rows = compare.frame_text(state).splitlines()
        self.assertEqual(rows[0], "TOCA_RENDER_12")
        self.assertEqual(rows[-7:], ["0", "0", "0", "0", "0", "0", "0"])

    def test_observed_caption_visibility_serializes_after_actual_animation_without_timing_inference(self):
        # Language0 is English,2 German. The final case also retains the
        # compiled language/mode endpoints even when menu selection is narrower.
        for language, mode, visible in ((0, 2, False), (0, 2, True), (2, 2, False), (2, 2, True), (4, 3, True)):
            with self.subTest(language=language, mode=mode, visible=visible):
                state = fixture()
                state["session_caption"] = {"language": language, "mode": mode, "visible": visible}
                state["animated_scenery"] = animated_fixture()
                state["animated_scenery"]["moving"] = [animated_node()]
                expected_animation = []
                animation.append_rows(expected_animation, state)
                self.assertEqual(compare.session_caption(state), {"language": language, "mode": mode, "visible": visible})
                rows = compare.frame_text(state).splitlines()
                self.assertEqual(rows[-len(expected_animation) - 7:], expected_animation + [f"1 {language} {mode} {int(visible)}", "0", "0", "0", "0", "0", "0"])
                self.assertEqual(state["session_caption"]["visible"], visible)

    def test_caption_rejects_unknown_language_mode_noninteger_fields_and_nonboolean_visibility(self):
        invalid = [("language", -1), ("language", 5), ("language", True), ("language", 1.5), ("language", "2"),
                   ("mode", -1), ("mode", 4), ("mode", False), ("mode", 2.5), ("mode", "2"),
                   ("visible", -1), ("visible", 0), ("visible", 1), ("visible", "false"), ("visible", None)]
        for field, value in invalid:
            with self.subTest(field=field, value=value):
                state = fixture()
                state["session_caption"] = {"language": 2, "mode": 2, "visible": True, field: value}
                with self.assertRaises(ValueError):
                    compare.frame_text(state)

    def test_caption_rejects_malformed_objects_and_missing_observed_fields(self):
        for caption in ({}, [], "GET READY", 1, True,
                        {"mode": 2, "visible": True}, {"language": 0, "visible": True}, {"language": 0, "mode": 2}):
            with self.subTest(caption=caption):
                state = fixture()
                state["session_caption"] = caption
                with self.assertRaises(ValueError):
                    compare.frame_text(state)


class AnimationCaptureTests(unittest.TestCase):
    def test_load_identity_keeps_root_child_order_and_excludes_camera_subtree(self):
        debugger = EffectMemory()
        debugger.put_word(0x4c38f0, 0x1000)
        debugger.put_word(0x4c3478, 0x1200)
        put_scene_node(debugger, 0x1000, sibling=0x2000, child=0x1100, parent=0x900)
        put_scene_node(debugger, 0x1100, sibling=0x1200, parent=0x1000)
        put_scene_node(debugger, 0x1200, sibling=0x1300, child=0x1210, parent=0x1000)
        put_scene_node(debugger, 0x1210, parent=0x1200)
        put_scene_node(debugger, 0x1300, parent=0x1000)
        put_scene_node(debugger, 0x2000, parent=0x900)
        identities = animation.capture_family(debugger, "cones")
        self.assertEqual(identities, [{"address": 0x1000, "parent": -1}, {"address": 0x1100, "parent": 0},
                                      {"address": 0x1300, "parent": 0}, {"address": 0x2000, "parent": -1}])
        self.assertTrue(all(set(node) == {"address", "parent"} for node in identities))

    def test_hor_disabled_groups_ignore_stale_scratch_and_rotor_uses_identity_parent(self):
        debugger = EffectMemory()
        debugger.put_word(0x4c38f0, 0xdeadbeef)
        self.assertEqual(animation.capture_family(debugger, "moving"), [])
        self.assertEqual(animation.capture_family(debugger, "rotorcraft"), [])
        debugger.put_word(0x4dcb68, 1)
        debugger.put_word(0x4dcb70, 0x1000)
        put_scene_node(debugger, 0x1000, child=0x1100, identity=0)
        put_scene_node(debugger, 0x1100, parent=0x1000)
        self.assertEqual(animation.capture_family(debugger, "rotorcraft"),
                         [{"address": 0x1000, "parent": -1}, {"address": 0x1100, "parent": 0}])
        debugger.put_word(0x4c38f0, 0x1000)
        debugger.put_word(0x4dc908, 1)
        self.assertEqual(len(animation.capture_family(debugger, "moving")), 2)

    def test_forest_rejects_cycles_and_inconsistent_parent_links(self):
        for parent, sibling in ((0x900, 0), (0x1000, 0x1000)):
            with self.subTest(parent=parent, sibling=sibling):
                debugger = EffectMemory()
                debugger.put_word(0x4c38f0, 0x1000)
                put_scene_node(debugger, 0x1000, child=0x1100, sibling=sibling)
                put_scene_node(debugger, 0x1100, parent=parent)
                with self.assertRaises(ValueError):
                    animation.capture_family(debugger, "posts")

    def test_draw_snapshot_reads_new_local_matrix_and_keeps_saved_family_boundary(self):
        debugger = EffectMemory()
        debugger.put_word(0x4c38f0, 0x1000)
        put_scene_node(debugger, 0x1000, identity=0x840002f4, visible=241)
        identities = {family: [] for family in animation.FAMILIES}
        identities["cones"] = animation.capture_family(debugger, "cones")
        debugger.put_word(0x1000, 0x2000)  # A different family was loaded later.
        put_scene_node(debugger, 0x2000, identity=995)
        updated = animated_node(visible=241)
        updated["transform_q16"][12] = -131073
        updated["local_float_hex"] = struct.pack("<16f", *(value / 65536 for value in updated["transform_q16"])).hex()
        debugger.put(0x105c, bytes.fromhex(updated["local_float_hex"]))
        snapshot = animation.capture_snapshot(debugger, identities)
        self.assertEqual(snapshot["cones"], [updated])
        self.assertEqual(snapshot["flags"], [])
        self.assertEqual(snapshot["moving"], [])

    def test_flags_read_actual_material_variant_not_controller_phase(self):
        debugger = EffectMemory()
        debugger.put_word(0x4f2a5c, 3)
        for index, target in enumerate((0x1000, 0x1000, 0)):
            debugger.put_word(0x4d2b20 + index * 0x50, target)
            debugger.put_word(0x4d2b20 + index * 0x50 + 0x4c, index)
        debugger.put_word(0x100c, 0x2000)
        debugger.put(0x2014, b"\x02")
        snapshot = animation.capture_snapshot(debugger, {family: [] for family in animation.FAMILIES})
        self.assertEqual(snapshot["flags"], [2, 2, -1])
        debugger.put(0x2014, b"\x03")
        with self.assertRaises(ValueError):
            animation.capture_snapshot(debugger, {family: [] for family in animation.FAMILIES})

    def test_matrix_requires_exact_float32_round_trip_in_signed_q16(self):
        matrix = [animation.MIN_Q16, animation.MAX_Q16] + [0] * 14
        self.assertEqual(animation.signed_matrix(struct.pack("<16f", *(value / 65536 for value in matrix))), matrix)
        for value in (1 / 3, -0.0, float("nan"), float("inf"), 32769.0, -32769.0):
            with self.subTest(value=value), self.assertRaises(ValueError):
                animation.signed_matrix(struct.pack("<16f", value, *([0] * 15)))

    def test_animation_rows_follow_car_nodes_and_allow_repeated_source_ids(self):
        snapshot = animated_fixture()
        snapshot["flags"] = [-1, 2, 0]
        snapshot["moving"] = [animated_node(), animated_node(parent=0, visible=0)]
        snapshot["cones"] = [animated_node(identity=996)]
        rows = ["car node rows already emitted"]
        animation.append_rows(rows, {"animated_scenery": snapshot})
        self.assertEqual(rows[:4], ["car node rows already emitted", "1", "3", "-1 2 0"])
        self.assertEqual(rows[4], "2")
        self.assertEqual(rows[5], "-1 756 15 " + " ".join(str(value) for value in snapshot["moving"][0]["transform_q16"]))
        self.assertEqual(rows[6], "0 756 0 " + " ".join(str(value) for value in snapshot["moving"][1]["transform_q16"]))
        self.assertEqual(rows[7:9], ["0", "1"])
        self.assertEqual(rows[-2:], ["0", "0"])
        state = fixture()
        historical = compare.frame_text(state).splitlines()
        self.assertEqual(historical[-6:], ["0", "0", "0", "0", "0", "0"])
        state["animated_scenery"] = snapshot
        self.assertEqual(compare.frame_text(state).splitlines(), historical[:-8] + rows[1:] + ["0", "0", "0", "0", "0", "0", "0"])

    def test_empty_flags_do_not_add_an_empty_text_row(self):
        rows = []
        animation.append_rows(rows, {"animated_scenery": animated_fixture()})
        self.assertEqual(rows, ["1", "0", "0", "0", "0", "0", "0"])
        rows = []
        animation.append_rows(rows, {})
        self.assertEqual(rows, ["0"])
        self.assertIsNone(animation.validate_snapshot({}))

    def test_snapshot_rejects_invalid_widths_hierarchy_and_float_audit(self):
        invalid = [("parent", 0), ("parent", -2), ("parent", True), ("id", -1), ("id", 1 << 24),
                   ("visible", -1), ("visible", 256), ("visible", True), ("transform_q16", [0] * 15),
                   ("transform_q16", [animation.MIN_Q16 - 1] + [0] * 15),
                   ("transform_q16", [animation.MAX_Q16 + 1] + [0] * 15),
                   ("transform_q16", [True] + [0] * 15), ("local_float_hex", "bad hex"),
                   ("local_float_hex", bytes(64).hex())]
        for field, value in invalid:
            with self.subTest(field=field, value=value):
                snapshot = animated_fixture()
                snapshot["moving"] = [{**animated_node(), field: value}]
                with self.assertRaises(ValueError):
                    animation.validate_snapshot({"animated_scenery": snapshot})

    def test_snapshot_rejects_missing_families_flag_bounds_and_shared_node_overflow(self):
        for flags in ([3], [-2], [True], [0] * 41):
            with self.subTest(flags=flags):
                snapshot = {**animated_fixture(), "flags": flags}
                with self.assertRaises(ValueError):
                    animation.validate_snapshot({"animated_scenery": snapshot})
        snapshot = animated_fixture()
        del snapshot["signs"]
        with self.assertRaises(ValueError):
            animation.validate_snapshot({"animated_scenery": snapshot})
        snapshot = animated_fixture()
        snapshot["moving"] = [animated_node()] * animation.MAX_NODES
        snapshot["cones"] = [animated_node()]
        with self.assertRaises(ValueError):
            animation.validate_snapshot({"animated_scenery": snapshot})


@contextlib.contextmanager
def capture_frame_case(stops, *, source_text=0x6000):
    debugger = mock.Mock()
    words = {0x4b0660: 0x1000, 0x132c: 2, 0x1004: 2,
             0x2004: 0x3000, 0x2008: source_text, 0x200c: 0, 0x4d5204: 0x6000}
    debugger.word.side_effect = words.__getitem__
    debugger.breakpoint.side_effect = range(1, 5)
    debugger.command.side_effect = [f"Stopped on breakpoint at 0x{address:08x}" for address in stops] + [""] * 4
    worlds = [{"world_ordinal": index, "actor": 0, "source_view": 2, "car_id": 7} for index in range(stops.count(0x489069))]
    families = object()
    hud = {"cached_hud": "same completed frame"}
    animation_snapshot = {"captured_animation": "same completed frame"}
    skid_snapshot = {"captured_skids": "same completed frame"}
    spark_snapshot = {"captured_sparks": "same completed frame"}
    queued_snapshot = {"captured_lines": "same completed frame"}
    with mock.patch.object(capture, "stack", return_value=0x2000), \
            mock.patch.object(capture, "capture_inputs", side_effect=worlds) as inputs, \
            mock.patch.object(capture, "capture_hud", return_value=hud) as capture_hud, \
            mock.patch.object(capture, "capture_skid_marks", return_value=skid_snapshot) as capture_skids, \
            mock.patch.object(capture, "capture_spark_lines", return_value=spark_snapshot) as capture_sparks, \
            mock.patch.object(capture, "capture_queued_lines", return_value=queued_snapshot) as capture_lines, \
            mock.patch.object(capture.animation, "capture_snapshot", return_value=animation_snapshot) as capture_animation:
        yield SimpleNamespace(run=lambda: capture.capture_frame(debugger, families), debugger=debugger, inputs=inputs,
                              hud=hud, capture_hud=capture_hud, animation=animation_snapshot,
                              capture_animation=capture_animation, families=families, skids=skid_snapshot, capture_skids=capture_skids,
                              sparks=spark_snapshot, capture_sparks=capture_sparks,
                              queued=queued_snapshot, capture_lines=capture_lines)


class CaptureFrameOrderTests(unittest.TestCase):
    def test_borrowed_world_break_handles_real_update_stops_before_the_present(self):
        with capture_frame_case([0x489069, 0x421aa0, 0x489069, 0x41de90, 0x48268e]) as case:
            case.inputs.side_effect = [{"actor": 0, "source_view": 2, "car_id": 7, "world_ordinal": index} for index in range(3)]
            state = capture.capture_frame(case.debugger, case.families, current_draw=True)
            self.assertEqual((state["world_ordinal"], state["render_serial_before_flip"]), (2, 3))
            case.capture_hud.assert_called_once_with(case.debugger)
            self.assertEqual(case.debugger.breakpoint.call_count, 3)


    def test_actor_poses_and_local_shadow_follow_the_last_actual_world_draw(self):
        first = actors.capture_snapshot(*actor_memory())
        second = copy.deepcopy(first)
        second["actors"][1]["object_to_world"][12] = 8.125
        loads, remaps = object(), object()
        with capture_frame_case([0x489069, 0x489069, 0x48268e]) as case, \
                mock.patch.object(capture.actors, "capture_snapshot", side_effect=[first, second]) as capture_actors:
            state = capture.capture_frame(case.debugger, case.families, loads, remaps)
            self.assertIs(state["render_actors"], second)
            self.assertEqual(state["shadow_records_hex"], second["actors"][0]["shadow_records_hex"])
            self.assertEqual(capture_actors.call_args_list, [mock.call(case.debugger, loads, 0, remaps)] * 2)

    def test_rollout_caption_survives_world_draws_before_the_selected_present(self):
        # 4480e0 draws the preparation caption before41f520 draws the world.
        # Retaining a later world must not discard that same-frame draw marker.
        for stops in ([0x45ece4, 0x489069, 0x41de90, 0x489069, 0x41de90, 0x48268e],
                      [0x489069, 0x45ece4, 0x41de90, 0x489069, 0x41de90, 0x48268e]):
            with self.subTest(stops=stops), capture_frame_case(stops) as case:
                state = case.run()
                self.assertEqual(state["session_caption"], {"language": 2, "mode": 2, "visible": True})
                self.assertEqual(state["world_ordinal"], 1)
                self.assertEqual(state["render_serial_before_flip"], 2)
                self.assertIs(state["hud"], case.hud)
                self.assertIs(state["animated_scenery"], case.animation)
                self.assertIs(state["skid_marks"], case.skids)
                self.assertIs(state["spark_lines"], case.sparks)
                self.assertIs(state["queued_lines"], case.queued)
                self.assertEqual(case.inputs.call_args_list, [mock.call(case.debugger, 0x3000)] * 2)
                self.assertEqual(case.capture_hud.call_count, 2)
                self.assertEqual(case.capture_animation.call_args_list, [mock.call(case.debugger, case.families)] * 2)
                self.assertEqual(case.capture_skids.call_args_list, [mock.call(case.debugger, 0)] * 2)
                self.assertEqual(case.capture_sparks.call_args_list, [mock.call(case.debugger, 0)] * 2)
                self.assertEqual(case.capture_lines.call_args_list, [mock.call(case.debugger)] * 2)
                self.assertEqual(case.debugger.command.call_args_list[-4:],
                                 [mock.call(f"delete break {index}") for index in range(1, 5)])

    def test_rollout_caption_without_an_actual_draw_marker_remains_hidden(self):
        with capture_frame_case([0x489069, 0x41de90, 0x48268e]) as case:
            state = case.run()
            self.assertEqual(state["session_caption"], {"language": 2, "mode": 2, "visible": False})
            self.assertEqual(state["render_serial_before_flip"], 1)
            self.assertNotIn(mock.call(0x4d5204), case.debugger.word.call_args_list)
            self.assertIs(state["hud"], case.hud)

    def test_rollout_caption_draw_rejects_a_different_localized_text_pointer(self):
        with capture_frame_case([0x45ece4, 0x489069, 0x48268e], source_text=0x6004) as case:
            with self.assertRaisesRegex(RuntimeError, "selected localized caption"):
                case.run()
            case.inputs.assert_not_called()
            case.capture_hud.assert_not_called()
            case.capture_animation.assert_not_called()


class PresentationDebugger:
    def __init__(self, events):
        self.events = events
        self.confirmed_pause = True
        self.detached = False
        self.deadline = 10
        self.memory_fd = None
        self.memory_closes = 0
        self.guards = {0x4cd760: bytes([17]) * 0x53c, 0x5300: bytes([34]) * 64,
                       0x5240: bytes([51]) * 64, 0x4d383c: struct.pack("<I", 3)}

    def command(self, command):
        self.events.append(("command", command))
        self.confirmed_pause = False
        if command == "info thread":
            return thread_listing()
        if command == "detach":
            self.detached = True
            return "Detached"
        raise AssertionError(f"Unexpected debugger command: {command}")

    def confirm_pause(self):
        self.events.append(("confirm_pause",))
        self.confirmed_pause = True

    def enable_process_memory(self, unix_pid):
        if not self.confirmed_pause:
            raise AssertionError("Memory opened before confirming the observed debugger pause")
        self.events.append(("open_memory", unix_pid))
        self.memory_fd = 77

    def word(self, address):
        if address != 0x4b9b94 or not self.confirmed_pause:
            raise AssertionError("Unexpected renderer read or unconfirmed debugger pause")
        return 0x1000

    def memory(self, address, size):
        if not self.confirmed_pause or len(self.guards[address]) != size:
            raise AssertionError("Guard snapshot requires the confirmed original pause")
        self.events.append(("before_guard", address))
        return self.guards[address]

    def close_memory(self):
        self.events.append(("close_memory",))
        self.memory_closes += 1
        self.memory_fd = None


@contextlib.contextmanager
def presentation_case(*, suspend_exit=None, prior_owner_count=0, changed_guard=None):
    presentation = capture.presentation
    events = []
    debugger = PresentationDebugger(events)
    state = fixture()
    del state["presentation_barrier"]
    environment = {"DISPLAY": ":private", "WINEPREFIX": "isolated-test-prefix"}
    control = {"count": prior_owner_count, "modes": []}
    clock = [0.0]
    captures = []

    def helper_run(arguments, **kwargs):
        if arguments[3:] != ["ec", "f0"] or kwargs["env"] is not environment:
            raise AssertionError("Thread helper must target the owned PID/TID and private environment")
        mode = arguments[2]
        events.append(("control", mode))
        control["modes"].append(mode)
        if mode == "s" and suspend_exit is not None:
            return SimpleNamespace(returncode=suspend_exit, stderr=b"ownership mismatch")
        previous = control["count"]
        if mode == "s":
            control["count"] += 1
        elif mode == "r":
            control["count"] -= 1
        else:
            raise AssertionError("Unexpected thread-control operation")
        return SimpleNamespace(returncode=16 + previous, stderr=b"")

    def read_guard(descriptor, size, address):
        if descriptor != 77 or not debugger.detached or control["count"] != 1:
            raise AssertionError("Post-presentation guards require only the game thread suspended")
        events.append(("after_guard", address))
        value = debugger.guards[address]
        if address == changed_guard:
            value = bytes([value[0] ^ 1]) + value[1:]
        if len(value) != size:
            raise AssertionError("Wrong guard read length")
        return value

    def sleep(seconds):
        clock[0] += seconds

    def capture_window(actual_environment, output):
        if actual_environment is not environment or not debugger.detached or control["count"] != 1:
            raise AssertionError("Window captured before graphics workers were released safely")
        events.append(("capture", len(captures)))
        captures.append(output)
        output.write_bytes(b"constant synthetic completed window")

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        with mock.patch.object(presentation.subprocess, "run", side_effect=helper_run), \
                mock.patch.object(presentation.os, "pread", side_effect=read_guard), \
                mock.patch.object(presentation.time, "monotonic", side_effect=lambda: clock[0]), \
                mock.patch.object(presentation.time, "sleep", side_effect=sleep):
            def run():
                presentation.capture_completed_window(debugger, environment, Path("/synthetic/wine"), Path("/synthetic/thread-control.exe"),
                                                      0xec, 1234, state, directory, capture_window)
            yield SimpleNamespace(run=run, debugger=debugger, state=state, events=events, control=control, captures=captures)


class PresentationSafetyTests(unittest.TestCase):
    def test_completed_window_releases_workers_only_after_suspend_and_resumes_after_observation(self):
        with presentation_case() as case:
            case.run()
            self.assertEqual(case.control["modes"], ["s", "r"])
            self.assertEqual(case.control["count"], 0)
            self.assertGreaterEqual(len(case.captures), 3)
            events = case.events
            self.assertLess(events.index(("control", "s")), events.index(("command", "detach")))
            self.assertLess(events.index(("command", "detach")), events.index(("capture", 0)))
            self.assertLess(events.index(("capture", len(case.captures) - 1)), events.index(("control", "r")))
            self.assertLess(events.index(("control", "r")), events.index(("close_memory",)))
            barrier = case.state["presentation_barrier"]
            for field in ("main_thread_frozen", "workers_released", "state_unchanged", "stable_window"):
                self.assertIs(barrier[field], True)
            self.assertEqual((barrier["process_id"], barrier["thread_id"], barrier["previous_suspend_count"]), (0xec, 0xf0, 0))
            self.assertIsNone(case.debugger.memory_fd)
            self.assertEqual(case.debugger.memory_closes, 1)

    def test_changed_body_camera_projection_or_counter_fails_and_releases_only_our_suspend(self):
        for guard in (0x4cd760, 0x5300, 0x5240, 0x4d383c):
            with self.subTest(guard=hex(guard)), presentation_case(changed_guard=guard) as case:
                with self.assertRaisesRegex(RuntimeError, "changed during presentation"):
                    case.run()
                self.assertTrue(case.debugger.detached)
                self.assertEqual(case.control["modes"], ["s", "r"])
                self.assertEqual(case.control["count"], 0)
                self.assertNotIn("presentation_barrier", case.state)
                self.assertIsNone(case.debugger.memory_fd)
                self.assertEqual(case.debugger.memory_closes, 1)

    def test_rejected_thread_ownership_never_detaches_or_resumes_an_unowned_thread(self):
        with presentation_case(suspend_exit=3) as case:
            with self.assertRaisesRegex(RuntimeError, "failed: 3"):
                case.run()
            self.assertFalse(case.debugger.detached)
            self.assertEqual(case.control["modes"], ["s"])
            self.assertEqual(case.control["count"], 0)
            self.assertEqual(case.captures, [])
            self.assertNotIn("presentation_barrier", case.state)
            self.assertIsNone(case.debugger.memory_fd)
            self.assertEqual(case.debugger.memory_closes, 1)

    def test_prior_suspend_owner_remains_suspended_after_releasing_our_increment(self):
        with presentation_case(prior_owner_count=1) as case:
            with self.assertRaisesRegex(RuntimeError, "independent suspend owner"):
                case.run()
            self.assertFalse(case.debugger.detached)
            self.assertEqual(case.control["modes"], ["s", "r"])
            self.assertEqual(case.control["count"], 1)
            self.assertEqual(case.captures, [])
            self.assertNotIn("presentation_barrier", case.state)
            self.assertIsNone(case.debugger.memory_fd)
            self.assertEqual(case.debugger.memory_closes, 1)


if __name__ == "__main__":
    unittest.main()
