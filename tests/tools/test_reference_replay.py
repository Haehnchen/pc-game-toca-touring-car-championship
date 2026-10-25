"""Trace integrity and comparisons must not accept partial original evidence."""

import copy
import csv
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

PROJECT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("replay_reference", PROJECT / "tools/replay-reference.py")
REPLAY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPLAY)


def fixture():
    config = {"track_id": 0, "weather": 1, "car_id": 0, "transmission_mode": 2, "steering_profile": 2}
    header = {"type": "header", "schema": 1, "exe_crc32": "8ce56d41", "layout": REPLAY.LAYOUT,
              "boundaries": REPLAY.BOUNDARIES, "initial_configuration": config}
    dynamics = bytearray(0x338)
    struct.pack_into("<i", dynamics, 0x27C, 2)
    struct.pack_into("<i", dynamics, 0x140, 65536)
    records = []
    for index in range(2):
        dynamics[0x14C] = 0xFF if index else 0
        record = {"type": "update", "id": index}
        for name, boundary in REPLAY.BOUNDARIES.items():
            record[name] = dict(config, boundary=boundary, actor=0, body_hex=bytes(0x53C).hex(),
                                dynamics_hex=dynamics.hex(), gameplay_hex=bytes(0x124).hex(),
                                delta_q16=32768, reciprocal_q16=131072, extra_update=False,
                                rng=17, qpc_low=100 + index, qpc_divisor=400000,
                                update_counter=index + (name != "pre"), limiter_active=0,
                                limiter_load_q16=0, roughness_cursor=0,
                                input_q16={"left": 0, "right": 0, "throttle": 65536, "brake": 0, "gear_edge": -1 if index else 0})
        records.append(record)
    return header, records


def write_trace(path, header, records):
    prefix = b"".join((json.dumps(record) + "\n").encode() for record in [header, *records])
    footer = {"type": "complete", "updates": len(records), "prefix_crc32": f"{zlib.crc32(prefix):08x}"}
    path.write_bytes(prefix + (json.dumps(footer) + "\n").encode())


class ReferenceReplayTests(unittest.TestCase):
    def test_consumed_input_and_time_survive_binary_conversion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_trace(root / "trace.jsonl", *fixture())
            header, records = REPLAY.read_trace(root / "trace.jsonl")
            REPLAY.write_binary(root / "replay.bin", header, records)
            binary = (root / "replay.bin").read_bytes()
            self.assertEqual(struct.unpack_from("<8s5I", binary), (b"TOCAREPL", 1, 0, 1, 0, 2))
            self.assertEqual(struct.unpack_from("<IiiIIIiII", binary, 28), (0, 32768, 131072, 0, 17, 0, 0, 0, 2))
            self.assertEqual(struct.unpack_from("<i", binary, 28 + 36 + 0x53C + 0x140)[0], 65536)
            self.assertEqual(records[1]["consumed"]["input_q16"]["gear_edge"], -1)
            self.assertEqual(len(binary), 28 + 2 * (36 + 0x53C + 0x338 + 0x124))

    def test_partial_corrupt_skipped_or_inconsistent_trace_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "trace.jsonl"
            header, records = fixture()
            write_trace(path, header, records)
            complete = path.read_bytes()
            for data in (complete[:-1], b"".join(complete.splitlines(keepends=True)[:-1]), complete.replace(b'"rng": 17', b'"rng": 18', 1)):
                with self.subTest(truncated_or_corrupted=True):
                    path.write_bytes(data)
                    with self.assertRaises(ValueError):
                        REPLAY.read_trace(path)
            for field, value in (("body_hex", "00"), ("delta_q16", 0), ("boundary", "wrong"),
                                 ("update_counter", 9), ("actor", 7), ("extra_update", 1),
                                 ("transmission_mode", 1), ("input_q16", dict(records[0]["consumed"]["input_q16"], throttle=0))):
                with self.subTest(field=field):
                    changed = copy.deepcopy(records)
                    changed[0]["consumed"][field] = value
                    write_trace(path, header, changed)
                    with self.assertRaises(ValueError):
                        REPLAY.read_trace(path)
            changed = copy.deepcopy(records)
            changed[1]["id"] = 3
            write_trace(path, header, changed)
            with self.assertRaises(ValueError):
                REPLAY.read_trace(path)
            changed = copy.deepcopy(records)
            for state in (changed[1][name] for name in REPLAY.BOUNDARIES):
                state["actor"] = 1
            write_trace(path, header, changed)
            with self.assertRaisesRegex(ValueError, "Actor changed between updates"):
                REPLAY.read_trace(path)

    def test_comparison_reports_exact_signed_difference_and_stopped_physics(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_trace(root / "trace.jsonl", *fixture())
            _, records = REPLAY.read_trace(root / "trace.jsonl")
            rows = [dict(id=record["id"], **REPLAY.expected_state(record["post"])) for record in records]

            def compare(mode="independent"):
                with (root / "native.csv").open("w", newline="") as output:
                    writer = csv.DictWriter(output, fieldnames=rows[0].keys())
                    writer.writeheader()
                    writer.writerows(rows)
                return REPLAY.compare(records, root / "native.csv", mode)

            self.assertTrue(compare()["parity"])
            complete = compare("sequential")
            self.assertEqual(complete["mode"], "continuous-consumed-input-updates")
            self.assertEqual(complete["initial_state_imports"], 1)
            self.assertEqual(complete["exact_prefix_updates"], 2)
            self.assertIn("persistent_canonical_props", complete["world_scope"])
            self.assertEqual(complete["unmodeled_original_vehicle_contacts"], [])
            self.assertEqual(complete["unmodeled_original_roughness_advances"], [])
            records[1]["post"]["roughness_cursor"] = 3
            self.assertEqual(compare("sequential")["unmodeled_original_roughness_advances"],
                             [{"update": 1, "external_advance_modulo15": 3}])
            body = bytearray(records[1]["post"]["body"])
            body[0x529:0x52C] = bytes((2, 2, 7))
            records[1]["post"]["body"] = bytes(body)
            self.assertEqual(compare("sequential")["unmodeled_original_vehicle_contacts"],
                             [{"update": 1, "partners": [2, 7]}])
            rows[1]["velocity_z"] = 1
            self.assertEqual(compare("sequential")["exact_prefix_updates"], 1)
            rows[1]["velocity_z"] = 0
            rows[0]["velocity_x"] = -11
            rows[1]["boundary"] = 1
            report = compare()
            self.assertFalse(report["parity"])
            self.assertEqual(report["mismatch_count"], 2)
            self.assertEqual(report["first_mismatches"][0], {"update": 0, "field": "velocity_x", "original": 0, "native": -11, "difference": -11})
            rows.pop()
            with self.assertRaisesRegex(ValueError, "every recorded update"):
                compare()


if __name__ == "__main__":
    unittest.main(verbosity=2)
