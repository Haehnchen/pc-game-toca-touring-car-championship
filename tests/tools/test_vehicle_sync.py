"""Retired/off vehicle entries never leave active overrides or touch scenery."""

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
SPEC = importlib.util.spec_from_file_location("vehicle_sync", PROJECT / "tools/sync-vehicle-models.py")
SYNC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SYNC)


class VehicleSyncTests(unittest.TestCase):
    def test_removal_disable_reenable_and_unrelated_assets(self):
        with tempfile.TemporaryDirectory(dir=PROJECT / "build") as temporary:
            generated, bundle = [Path(temporary) / name for name in ("generated", "bundled")]
            for directory in (generated, bundle):
                (directory / "scene").mkdir(parents=True)
                (directory / "scene/object.glb").write_bytes(b"scenery")
                (directory / "car-9.glb").write_bytes(b"unmanaged")
            document = {"vehicles": [{"output": "assets/models/mega/car-7.glb", "mode": "mega"}]}
            (generated / "car-7.glb").write_bytes(b"accepted vehicle")
            SYNC.sync(document, generated, bundle)
            self.assertEqual((bundle / "car-7.glb").read_bytes(), b"accepted vehicle")
            timestamp = (bundle / "car-7.glb").stat().st_mtime_ns
            SYNC.sync(document, generated, bundle)
            self.assertEqual((bundle / "car-7.glb").stat().st_mtime_ns, timestamp)
            SYNC.sync({"vehicles": []}, generated, bundle)
            for directory in (generated, bundle):
                self.assertFalse((directory / "car-7.glb").exists())
            (generated / "car-7.glb").write_bytes(b"rebuilt vehicle")
            SYNC.sync(document, generated, bundle)
            SYNC.sync(document, generated, bundle, enabled=False)
            for directory in (generated, bundle):
                self.assertFalse((directory / "car-7.glb").exists())
                self.assertEqual((directory / "scene/object.glb").read_bytes(), b"scenery")
                self.assertEqual((directory / "car-9.glb").read_bytes(), b"unmanaged")

    def test_reference_is_not_bundled_and_unsafe_ledger_cannot_delete(self):
        with tempfile.TemporaryDirectory(dir=PROJECT / "build") as temporary:
            generated, bundle = [Path(temporary) / name for name in ("generated", "bundled")]
            generated.mkdir()
            bundle.mkdir()
            document = {"vehicles": [{"output": "assets/models/mega/car-0.glb", "mode": "reference"}]}
            (generated / "car-0.glb").write_bytes(b"reference")
            SYNC.sync(document, generated, bundle)
            self.assertFalse((bundle / "car-0.glb").exists())
            (bundle / SYNC.LEDGER).write_text(json.dumps({"version": 1, "managed": ["scene/object.glb"]}))
            with self.assertRaisesRegex(ValueError, "unmanaged"):
                SYNC.sync(document, generated, bundle)


if __name__ == "__main__":
    unittest.main()
