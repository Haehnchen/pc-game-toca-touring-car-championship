"""Synthetic parser rejection cases and exact round trips for all Git BFP inputs."""
import runpy
from pathlib import Path
import struct
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import archive_bytes, configured_data_root

tool = runpy.run_path(str(PROJECT / "tools/extract-assets.py"))
read_bfp = tool["read_bfp"]


def fixture():
    # Repeated and path-like names are legal metadata, never output paths.
    name = b"..\\same.bmp\0\0\0\0\0"
    return (struct.pack("<III", 2, 48, 3)
            + struct.pack("<16sII", name, 0, 3)
            + struct.pack("<16sII", name, 3, 2) + b"abcde")


class BfpTests(unittest.TestCase):
    def test_member_order_and_duplicate_names(self):
        data = fixture()
        entries = read_bfp(data)
        self.assertEqual([(i, offset, size) for i, _, offset, size in entries],
                         [(0, 60, 3), (1, 63, 2)])
        self.assertEqual(entries[0][1], entries[1][1])
        self.assertEqual(data[entries[1][2]:], b"de")

    def test_rejects_truncation(self):
        for size in (0, 11, 30, 60, 64):
            with self.subTest(size=size), self.assertRaises(ValueError):
                read_bfp(fixture()[:size])

    def test_rejects_overlap_out_of_bounds_and_invalid_header(self):
        for position, value in ((4, 24), (8, 999), (28, 1), (32, 500), (52, 1)):
            data = bytearray(fixture())
            struct.pack_into("<I", data, position, value)
            with self.subTest(position=position), self.assertRaises(ValueError):
                read_bfp(data)

    def test_rejects_unaccounted_tail(self):
        with self.assertRaises(ValueError):
            read_bfp(fixture() + b"extra")

    def test_classifies_payload_instead_of_truncated_name(self):
        classify = tool["classify"]
        self.assertEqual(classify(b"BM" + b"\0" * 30, "FONTS\\OPTION3.F"), ("images", ".bmp"))
        self.assertEqual(classify(b"CM3D" + b"\0" * 30, "..\\objects\\ha"), ("models", ".c3d"))


class ReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = configured_data_root()
        cls.paths = [path.relative_to(cls.root).as_posix() for path in sorted((cls.root / "data").glob("*.bfp"))]

    def test_git_archives_have_valid_members(self):
        self.assertTrue(self.paths)
        for relative in self.paths:
            with self.subTest(archive=relative):
                data = archive_bytes(self.root, relative)
                entries = read_bfp(data)
                self.assertTrue(entries)
                self.assertEqual([entry[0] for entry in entries], list(range(len(entries))))
                self.assertTrue(all(len(name) == 16 for _, name, _, _ in entries))

    def test_all_archives_reconstruct_byte_identically(self):
        for relative in self.paths:
            with self.subTest(archive=relative):
                data = archive_bytes(self.root, relative)
                entries = read_bfp(data)
                base = 12 + 24 * len(entries)
                rebuilt = (struct.pack("<III", len(entries), base - 12,
                                       max(size for _, _, _, size in entries))
                           + b"".join(struct.pack("<16sII", name, offset - base, size)
                                      for _, name, offset, size in entries)
                           + b"".join(data[offset:offset + size] for _, _, offset, size in entries))
                self.assertEqual(rebuilt, data)


if __name__ == "__main__":
    unittest.main()
