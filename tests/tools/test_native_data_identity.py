"""Native source identity is deterministic across checkout paths and OS newlines."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("native_data_identity", ROOT / "tools/build-native-data-identity.py")
IDENTITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IDENTITY)


class NativeDataIdentityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="toca-native-data-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "texts.hpp").write_bytes(b'constexpr auto title = "RACE";\n')
        (self.root / "cars.hpp").write_bytes(b"constexpr int maximum_rpm = 8500;\n")

    def test_identity_tracks_data_and_paths_with_stable_order(self):
        files = ["texts.hpp", "cars.hpp"]
        manifest = IDENTITY.source_manifest(self.root, files)
        expected = bytearray(b"TOCA-NATIVE-DATA\0")
        for name in sorted(files):
            data = (self.root / name).read_bytes()
            expected.extend(name.encode() + b"\0" + struct.pack("<II", len(data), zlib.crc32(data)))
        self.assertEqual(manifest, bytes(expected))
        self.assertEqual(manifest, IDENTITY.source_manifest(self.root, reversed(files)))
        (self.root / "cars.hpp").write_bytes(b"constexpr int maximum_rpm = 9000;\n")
        self.assertNotEqual(manifest, IDENTITY.source_manifest(self.root, files))

    def test_checkout_newlines_do_not_change_identity(self):
        files = ["texts.hpp", "cars.hpp"]
        expected = IDENTITY.source_manifest(self.root, files)
        for name in files:
            path = self.root / name
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(expected, IDENTITY.source_manifest(self.root, files))

    def test_header_uses_native_sources_without_reference_and_is_incremental(self):
        output = self.root / "build/native_data_identity.hpp"
        IDENTITY.generate(self.root, ["cars.hpp", "texts.hpp"], output)
        self.assertFalse((self.root / "external-inputs").exists())
        before, timestamp = output.read_bytes(), output.stat().st_mtime_ns
        self.assertIn(b"manifest_crc32 = 0x", before)
        IDENTITY.generate(self.root, ["texts.hpp", "cars.hpp"], output)
        self.assertEqual(output.read_bytes(), before)
        self.assertEqual(output.stat().st_mtime_ns, timestamp)
        for files in ([], ["cars.hpp", "cars.hpp"], ["../outside.hpp"]):
            with self.subTest(files=files), self.assertRaises(ValueError):
                IDENTITY.source_manifest(self.root, files)


if __name__ == "__main__":
    unittest.main()
