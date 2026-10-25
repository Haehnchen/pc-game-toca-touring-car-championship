"""Exercise BFP extraction without an original installation or asset index."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/unpack-game-data.py"
SPEC = importlib.util.spec_from_file_location("toca_test_game_data", TOOL)
DATA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DATA)


@unittest.skipUnless(shutil.which("zstd"), "zstd is required to unpack game data")
class GameDataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = self.root / "bundle"
        self.bundle.mkdir()
        self.output = self.root / "runtime"
        self.payloads = {"zebra.bfp": b"arbitrary BFP bytes\x00\xff", "new-archive.bfp": b"another archive"}
        for name, payload in self.payloads.items():
            self.compress(name, payload)

    def compress(self, name, payload):
        result = subprocess.run(["zstd", "-q", "-c"], input=payload, capture_output=True, check=True)
        (self.bundle / (name + ".zst")).write_bytes(result.stdout)

    def test_cli_discovers_bfps_and_extracts_exact_bytes(self):
        (self.bundle / "ignored.txt").write_text("This is not a BFP archive.")
        result = subprocess.run([sys.executable, "-B", str(TOOL), "--source-dir", str(self.bundle),
                                 "--output-dir", str(self.output)], cwd=self.root,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual({path.name: path.read_bytes() for path in (self.output / "data").iterdir()}, self.payloads)
        self.assertIn("Unpacked 2", result.stdout)

    def test_unpack_returns_sorted_outputs_and_replaces_previous_bytes(self):
        paths = DATA.unpack(self.bundle, self.output)
        self.assertEqual(paths, [self.output / "data" / name for name in sorted(self.payloads)])
        self.compress("new-archive.bfp", b"replacement bytes")
        DATA.unpack(self.bundle, self.output)
        self.assertEqual(paths[0].read_bytes(), b"replacement bytes")
        self.assertEqual(paths[1].read_bytes(), self.payloads["zebra.bfp"])

    def test_decoder_failure_preserves_previous_file_and_removes_temporary(self):
        target = self.output / "data/new-archive.bfp"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"previous bytes")
        (self.bundle / "new-archive.bfp.zst").write_bytes(b"not a Zstandard stream")
        with self.assertRaisesRegex(DATA.GameDataError, "Cannot decode game data"):
            DATA.unpack(self.bundle, self.output)
        self.assertEqual(target.read_bytes(), b"previous bytes")
        self.assertEqual(list(target.parent.iterdir()), [target])

    def test_missing_decoder_reports_error_and_preserves_previous_file(self):
        target = self.output / "data/new-archive.bfp"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"previous bytes")
        with self.assertRaisesRegex(DATA.GameDataError, "Cannot unpack game data"):
            DATA.unpack(self.bundle, self.output, zstd=self.root / "missing-decoder")
        self.assertEqual(target.read_bytes(), b"previous bytes")
        self.assertEqual(list(target.parent.iterdir()), [target])

    def test_rejects_symlink_sources_and_destinations(self):
        source = self.bundle / "new-archive.bfp.zst"
        actual = self.root / "actual.zst"
        source.rename(actual)
        source.symlink_to(actual)
        with self.assertRaisesRegex(DATA.GameDataError, "symlinks"):
            DATA.unpack(self.bundle, self.output)
        self.assertFalse(self.output.exists())
        source.unlink()
        actual.rename(source)
        self.output.mkdir()
        outside = self.root / "outside"
        outside.mkdir()
        (self.output / "data").symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(DATA.GameDataError, "symlinks"):
            DATA.unpack(self.bundle, self.output)
        self.assertEqual(list(outside.iterdir()), [])
        (self.output / "data").unlink()
        (self.output / "data").mkdir()
        destination = self.output / "data/new-archive.bfp"
        destination.symlink_to(outside / "untouched.bfp")
        with self.assertRaisesRegex(DATA.GameDataError, "symlinks"):
            DATA.unpack(self.bundle, self.output)
        self.assertEqual(list(outside.iterdir()), [])

    def test_rejects_source_tree_destinations_and_missing_sources(self):
        for forbidden in (ROOT / "assets", ROOT / "references", self.bundle):
            with self.subTest(forbidden=forbidden), self.assertRaisesRegex(DATA.GameDataError, "outside"):
                DATA.unpack(self.bundle, forbidden / "game-data-test-must-not-be-created")
        with self.assertRaisesRegex(DATA.GameDataError, "must not contain its source"):
            DATA.unpack(self.bundle, self.root)
        with self.assertRaisesRegex(DATA.GameDataError, "source is not a directory"):
            DATA.unpack(self.root / "missing", self.output)
        with self.assertRaisesRegex(DATA.GameDataError, "source is not a directory"):
            DATA.unpack(self.bundle / "new-archive.bfp.zst", self.output)

    def test_rejects_directory_at_output_file(self):
        (self.output / "data/new-archive.bfp").mkdir(parents=True)
        with self.assertRaisesRegex(DATA.GameDataError, "not a regular file"):
            DATA.unpack(self.bundle, self.output)
        self.assertEqual(list((self.output / "data/new-archive.bfp").iterdir()), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
