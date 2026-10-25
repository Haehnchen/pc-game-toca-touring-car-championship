"""Check the checksum CLI and dependency-cache rejection before extraction."""

import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zlib

PROJECT = Path(__file__).resolve().parents[2]
TOOL = PROJECT / "tools/checksum.py"


class ChecksumTests(unittest.TestCase):
    def test_known_vector_and_manifest_errors(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sample = root / "name with spaces.bin"
            sample.write_bytes(b"123456789")
            result = subprocess.run([sys.executable, str(TOOL), str(sample)],
                                    capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "cbf43926\n")
            manifest = root / "CRC32SUMS"
            manifest.write_text("cbf43926  name with spaces.bin\n")
            command = [sys.executable, str(TOOL), "--check", str(manifest), "--quiet"]
            result = subprocess.run(command, cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            sample.write_bytes(b"123456780")
            result = subprocess.run(command, cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("expected cbf43926", result.stderr)
            sample.unlink()
            result = subprocess.run(command, cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            for invalid in ("", "not-a-checksum  file\n"):
                manifest.write_text(invalid)
                result = subprocess.run(command, cwd=root, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)

    def test_dependency_cache_checks_before_extracting_or_reusing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "cache"
            archives = cache / "archives"
            archives.mkdir(parents=True)
            archive = archives / "fixture.tar"
            with tarfile.open(archive, "w") as bundle:
                entry = tarfile.TarInfo("fixture/payload")
                entry.size = 7
                bundle.addfile(entry, io.BytesIO(b"payload"))
            crc = f"{zlib.crc32(archive.read_bytes()):08x}"
            lock = {"archives": {"fixture": {"url": "https://example.invalid/fixture.tar",
                    "archive": archive.name, "root": "fixture", "crc32": crc}}}
            script = root / "probe.cmake"
            script.write_text(
                'cmake_minimum_required(VERSION 3.25)\n'
                f'set(Python3_EXECUTABLE "{Path(sys.executable).as_posix()}")\n'
                f'set(TOCA_DEPENDENCY_CACHE "{cache.as_posix()}")\n'
                f'include("{(PROJECT / "cmake/Dependencies.cmake").as_posix()}")\n'
                f'set(_toca_dependency_lock [==[{json.dumps(lock)}]==])\n'
                'toca_dependency_source(fixture source)\n')
            command = ["cmake", "-P", str(script)]
            for _ in range(2):
                result = subprocess.run(command, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
            extracted = cache / "sources" / f"fixture-{crc}"
            self.assertEqual((extracted / "fixture/payload").read_bytes(), b"payload")
            self.assertEqual((extracted / ".toca-extracted").read_text(), crc)
            archive.write_bytes(b"corrupted")
            result = subprocess.run(command, capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CRC32", result.stderr)
            self.assertEqual((extracted / "fixture/payload").read_bytes(), b"payload")


if __name__ == "__main__":
    unittest.main(verbosity=2)
