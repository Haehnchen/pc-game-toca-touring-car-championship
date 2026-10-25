"""Shared cockpit builds preserve authored payloads and clean only their roles."""

import importlib.util
import copy
import json
import shutil
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import vehicle_model as model
from git_game_data import configured_data_root
spec = importlib.util.spec_from_file_location("cockpit_build", PROJECT / "tools/build-cockpit-models.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class CockpitBuildTests(unittest.TestCase):
    def test_all_nine_interiors_produce_deterministic_shared_and_per_car_models(self):
        document = builder.load_manifest()
        first = builder.build(document)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "data").mkdir()
            for consumer in document["consumers"]:
                archive = Path(consumer["archive"])
                shutil.copyfile(configured_data_root() / "data" / archive.name, root / "data" / archive.name)
            with mock.patch.object(model, "PROJECT", root / "unavailable-reference"):
                second = builder.build(document, data_root=root)
        self.assertEqual(first, second)
        self.assertEqual([entry[0] for entry in first], [entry["output"] for entry in document["models"]])
        self.assertEqual(len(first), 21)
        definitions = {entry["output"]: entry for entry in document["models"]}
        for name, payload, stats in first:
            self.assertEqual(payload[:4], b"glTF")
            self.assertEqual(builder.crc32(payload), stats["crc32"])
            self.assertGreater(stats["visible_triangles"], stats["original_visible_triangles"])
            entry = definitions[name]
            directory = PROJECT / entry.get("texture_directory", document["texture_directory"])
            names = (["ARM", "GLOVE03", "GLOVE01", "GLOVE02"] if entry["member"] < 2 else
                     ["dashc", "dash", "dasha", "dashb"] if entry["member"] == 2 else
                     ["wiper" if entry["member"] == 3 else "wheel01"])
            for artwork in names:
                authored = (directory / (artwork + ".webp")).read_bytes()
                self.assertIn(authored, payload, "Final generated WebP must be embedded without another lossy encode")
            if entry["member"] == 2:
                self.assertEqual(stats["authored_webp_quality"], 70)
        hands = {(stats["source_car_id"], stats["role"]): stats for _, _, stats in first if stats["source_car_id"] is not None}
        honda, vectra = hands[0, "right-hand"], hands[2, "right-hand"]
        self.assertEqual(honda["source_geometry_crc32"], vectra["source_geometry_crc32"])
        self.assertNotEqual(honda["geometry_crc32"], vectra["geometry_crc32"], "The two original shoulder/driver-side groups need distinct right-hand grip fits")
        self.assertEqual(hands[0, "left-hand"]["geometry_crc32"], hands[2, "left-hand"]["geometry_crc32"])

    def test_bundle_disable_preserves_unrelated_mega_resources(self):
        document = builder.load_manifest()
        with tempfile.TemporaryDirectory(dir=PROJECT / "build", prefix="cockpit-bundle-") as temporary:
            generated = Path(temporary) / "generated"
            bundle = Path(temporary) / "bundle"
            for directory in (generated, bundle):
                directory.mkdir()
                (directory / "unrelated.glb").write_bytes(b"another package")
            for entry in document["models"]:
                path = generated / entry["output"]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(entry["role"].encode())
            (generated / "cockpit-manifest.json").write_bytes(b"completion marker")
            builder.sync(document, generated, bundle)
            for entry in document["models"]:
                self.assertEqual((bundle / entry["output"]).read_bytes(), entry["role"].encode())
            for directory in (generated, bundle):
                (directory / "cockpit/honda/other.glb").write_bytes(b"unrelated model")
            reduced = {**document, "models": [entry for entry in document["models"] if entry["member"] != 2]}
            builder.sync(reduced, generated, bundle)
            for directory in (generated, bundle):
                self.assertFalse((directory / "cockpit/honda/dashboard.glb").exists(), "Unregistered dashboards must not remain loadable")
                self.assertEqual((directory / "cockpit/honda/other.glb").read_bytes(), b"unrelated model")
            builder.sync(reduced, generated, bundle, enabled=False)
            self.assertFalse((generated / "cockpit-manifest.json").exists())
            for directory in (generated, bundle):
                self.assertEqual((directory / "unrelated.glb").read_bytes(), b"another package")
                for entry in document["models"]:
                    self.assertFalse((directory / entry["output"]).exists())

    def test_dashboard_registration_rejects_wrong_side_role_or_other_vehicle(self):
        original = builder.load_manifest()
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "manifest.json"
            for field, value in (("member", 1), ("output", "cockpit/audi/dashboard.glb"),
                                 ("texture_directory", "assets/models/cockpits/honda/../audi/textures")):
                document = copy.deepcopy(original)
                document['models'][-1][field] = value
                path.write_text(json.dumps(document))
                with self.assertRaises(ValueError):
                    builder.load_manifest(path)


if __name__ == "__main__":
    unittest.main()
