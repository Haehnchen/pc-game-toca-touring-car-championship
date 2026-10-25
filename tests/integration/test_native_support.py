"""CPU-only checks for incremental native CSV polling."""

import csv
import io
import os
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

from native_support import read_png, StepInput, TraceReader


class PngReaderTests(unittest.TestCase):
    @staticmethod
    def fixture(channels, methods):
        width = 5
        rows = [bytes((x * 53 + y * 37) % 256 for x in range(width * channels)) for y in range(len(methods))]
        filtered = bytearray()
        prior = bytes(width * channels)
        for row, method in zip(rows, methods):
            filtered.append(method)
            for x, value in enumerate(row):
                a = row[x - channels] if x >= channels else 0
                b, c = prior[x], prior[x - channels] if x >= channels else 0
                p = a + b - c
                distances = (abs(p - a), abs(p - b), abs(p - c))
                predictions = (0, a, b, (a + b) // 2, (a, b, c)[distances.index(min(distances))])
                filtered.append((value - predictions[method if method < 5 else 0]) % 256)
            prior = row

        def chunk(kind, payload):
            return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

        header = struct.pack(">IIBBBBB", width, len(rows), 8, 2 if channels == 3 else 6, 0, 0, 0)
        image = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(filtered)) + chunk(b"IEND", b"")
        pixels = [tuple(row[x:x + 3]) for row in rows for x in range(0, len(row), channels)]
        return image, (width, len(rows), pixels)

    def test_all_filters_preserve_rgb_and_rgba_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.png"
            for channels in (3, 4):
                for method in range(5):
                    with self.subTest(channels=channels, method=method):
                        image, expected = self.fixture(channels, [method] * 4)
                        path.write_bytes(image)
                        self.assertEqual(read_png(path), expected)
                image, expected = self.fixture(channels, [0, 4, 1, 2, 3])
                path.write_bytes(image)
                self.assertEqual(read_png(path), expected)

    def test_invalid_row_filter_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.png"
            path.write_bytes(self.fixture(3, [5])[0])
            with self.assertRaisesRegex(AssertionError, "invalid PNG filter"):
                read_png(path)


class TraceReaderTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "trace.csv"
        self.reader = TraceReader(self.path)

    def append(self, data):
        with self.path.open("ab") as stream:
            stream.write(data)

    def test_partial_records_and_utf8_are_not_published_early(self):
        self.assertEqual(self.reader.rows(), [])
        self.append(b"poll,screen,name\r")
        self.assertEqual(self.reader.rows(), [])
        self.append(b"\n1,scene,Ren\xc3")
        self.assertEqual(self.reader.rows(), [])
        self.append(b"\xa9\r\n2,main_menu,\"A,B\"")
        first = self.reader.rows()
        self.assertEqual(first, [{"poll": "1", "screen": "scene", "name": "René"}])
        self.assertEqual(self.reader.rows(), first)
        self.append(b"\n3,scene,REENTRY\n")
        rows = self.reader.rows()
        self.assertEqual([row["screen"] for row in rows], ["scene", "main_menu", "scene"])
        self.assertEqual(rows[1]["name"], "A,B")
        self.assertEqual(len(first), 1, "previous snapshots must not grow on append")

    def test_only_new_records_are_parsed(self):
        self.path.write_bytes(b"poll,screen\n1,title\n")
        with patch("native_support.csv.reader", wraps=csv.reader) as parser:
            self.assertEqual(len(self.reader.rows()), 1)
            for _ in range(20):
                self.assertEqual(len(self.reader.rows()), 1)
            self.assertEqual(parser.call_count, 1, "unchanged polls must not reparse the CSV")
            self.append(b"2,scene\n")
            self.assertEqual(len(self.reader.rows()), 2)
            self.assertEqual(parser.call_count, 2)

    def test_truncation_replacement_and_missing_file_clear_history(self):
        self.path.write_bytes(b"poll,screen\n100,long_old_scene\n")
        self.assertEqual(len(self.reader.rows()), 1)
        self.path.write_bytes(b"poll,screen\n1,new\n")
        self.assertEqual(self.reader.rows(), [{"poll": "1", "screen": "new"}])
        replacement = self.path.with_suffix(".replacement")
        replacement.write_bytes(b"poll,screen\n2,replaced\n")
        replacement.replace(self.path)
        self.assertEqual(self.reader.rows(), [{"poll": "2", "screen": "replaced"}])
        self.path.unlink()
        self.assertEqual(self.reader.rows(), [])
        self.path.write_bytes(b"step,rpm\n1,2000\n")
        self.assertEqual(self.reader.rows(), [{"step": "1", "rpm": "2000"}])

    def test_fast_regrowth_and_explicit_process_reset(self):
        self.path.write_bytes(b"poll,screen\n100,old\npartial")
        self.assertEqual(self.reader.rows()[0]["screen"], "old")
        self.path.write_bytes(b"poll,screen\n1,new_scene_with_a_longer_record\n")
        self.assertEqual(self.reader.rows(), [{"poll": "1", "screen": "new_scene_with_a_longer_record"}])
        self.reader.reset()
        self.assertEqual(len(self.reader.rows()), 1)
        other = TraceReader(self.path)
        self.assertEqual(other.rows(), self.reader.rows())

    def test_complete_invalid_record_is_not_silently_accepted(self):
        self.path.write_bytes(b"poll,screen\n1\n")
        with self.assertRaisesRegex(AssertionError, "CSV header"):
            self.reader.rows()


class StepInputTests(unittest.TestCase):
    def setUp(self):
        read_fd, self.write_fd = os.pipe()
        self.output = os.fdopen(read_fd, "rb", buffering=0)
        self.addCleanup(self.output.close)
        self.addCleanup(os.close, self.write_fd)
        self.input = io.BytesIO()
        self.bridge = StepInput(SimpleNamespace(stdout=self.output, stdin=self.input))

    def test_buffered_replies_and_action_commands(self):
        os.write(self.write_fd, b"renderer notice\nTOCA_STEP 195\nTOCA_STEP 196\n")
        self.assertEqual(self.bridge.ready(), 195)
        self.assertEqual(self.bridge.advance(5), 196)
        self.bridge.resume()
        self.assertEqual(self.input.getvalue(), b"step 5\nlive\n")

    def test_missing_and_skipped_steps_fail_promptly(self):
        with self.assertRaisesRegex(AssertionError, "Timed out"):
            self.bridge.ready(timeout=.001)
        os.write(self.write_fd, b"TOCA_STEP 195\nTOCA_STEP 197\n")
        self.bridge.ready()
        with self.assertRaisesRegex(AssertionError, "skipped or repeated"):
            self.bridge.advance(4)


if __name__ == "__main__":
    unittest.main()
