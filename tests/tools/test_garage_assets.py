#!/usr/bin/env python3
"""Manual garage publication preserves source layers and finished artwork."""

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

from PIL import Image, ImageDraw

PROJECT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("garage_assets", PROJECT / "tools/build-garage-assets.py")
ASSETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ASSETS)
ORIGINAL_SPEC = importlib.util.spec_from_file_location("garage_originals", PROJECT / "tools/import-garage-originals.py")
ORIGINAL = importlib.util.module_from_spec(ORIGINAL_SPEC)
ORIGINAL_SPEC.loader.exec_module(ORIGINAL)


class GarageAssetTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source, self.output = self.root / "source", self.root / "output"
        (self.source / "backgrounds").mkdir(parents=True)
        (self.source / "cars").mkdir()
        self.background = self.source / "backgrounds/test.webp"
        self.foreground = self.source / "cars/test.webp"
        Image.new("RGB", tuple(ASSETS.CANVAS), (255, 0, 0)).save(self.background, lossless=True)
        foreground = Image.new("RGBA", tuple(ASSETS.CANVAS), (0, 255, 0, 0))
        draw = ImageDraw.Draw(foreground)
        draw.rectangle((128, 128, 383, 383), fill=(0, 0, 255, 255))
        draw.rectangle((512, 128, 767, 383), fill=(0, 0, 255, 128))
        foreground.save(self.foreground, lossless=True, exact=True)
        self.entry = {"key": "gar1-car0", "variant": 1, "car_id": 0,
                      "background": "backgrounds/test.webp", "foreground": "cars/test.webp"}
        self.write_manifest()

    def write_manifest(self):
        self.manifest = {"schema_version": 1, "canvas": ASSETS.CANVAS, "entries": [self.entry]}
        self.save_manifest()

    def save_manifest(self):
        (self.source / "generation.json").write_text(json.dumps(self.manifest))

    def add_contact_shadow(self):
        stamp = Image.new("RGBA", (512, 256))
        ImageDraw.Draw(stamp).rectangle((64, 32, 447, 223), fill=(0, 0, 0, 96))
        path = self.source / "shadow.webp"
        stamp.save(path, lossless=True, exact=True)
        self.index = {"crop": [2, 0, 1148, 714], "strip_width": 128,
                      "panorama_starts": [0] + [100] * 9,
                      "renders": {name: {"alpha_bbox_xyxy": [128, 128, 384, 384]}
                                  for name in ("car-0", "interior-audi", "interior-renault")},
                      "entries": {self.entry["key"]: {"vehicles": [{"car_id": 0, "x_shift": 0}]}}}
        self.manifest["contact_shadow"] = {"shadow": "shadow.webp"}
        self.manifest["local_render"] = {"index": "cars/rendering.json"}
        self.save_shadow_index()
        self.save_manifest()
        return stamp

    def save_shadow_index(self):
        (self.source / "cars/rendering.json").write_text(json.dumps(self.index))

    def test_contact_shadow_darkens_floor_beneath_unchanged_opaque_vehicle(self):
        with Image.open(self.foreground) as image:
            foreground = image.convert("RGBA")
        ImageDraw.Draw(foreground).rectangle((192, 320, 319, 383), fill=(0, 0, 0, 0))
        foreground.save(self.foreground, lossless=True, exact=True)
        self.write_manifest()
        ASSETS.compose(self.source, self.output)
        destination = self.output / "gar1-car0.webp"
        with Image.open(destination) as image:
            before = image.convert("RGB")
        self.add_contact_shadow()
        ASSETS.compose(self.source, self.output)
        with Image.open(destination) as image:
            self.assertLess(image.getpixel((256, 350))[0], before.getpixel((256, 350))[0] - 60)
            self.assertTrue(all(abs(a - b) <= 5 for a, b in zip(image.getpixel((256, 280)), before.getpixel((256, 280)))))
            self.assertGreater(image.getpixel((32, 32))[0], 250)
        self.assertEqual(ASSETS.compose(self.source, self.output)["written"], 0)

    def test_contact_shadow_clips_negative_neighbor_shift_and_applies_source_crop(self):
        stamp = self.add_contact_shadow()
        self.index["crop"] = [80, 60, 1226, 774]
        self.index["renders"]["car-0"]["alpha_bbox_xyxy"] = [200, 200, 600, 500]
        self.index["entries"][self.entry["key"]]["vehicles"][0]["x_shift"] = -300
        alpha = ASSETS.contact_shadows(stamp, self.index, self.entry).getchannel("A")
        self.assertGreater(alpha.getpixel((50, 400)), 80)
        self.assertEqual(alpha.getpixel((260, 400)), 0)
        self.assertEqual(alpha.getpixel((50, 450)), 0)

    def test_empty_foreground_has_no_contact_shadow(self):
        Image.new("RGBA", tuple(ASSETS.CANVAS)).save(self.foreground, lossless=True)
        self.entry.update(key="gar1-car8", car_id=8, foreground_empty=True)
        self.write_manifest()
        ASSETS.compose(self.source, self.output)
        destination = self.output / "gar1-car8.webp"
        before = destination.read_bytes()
        stamp = self.add_contact_shadow()
        self.assertIsNone(ASSETS.contact_shadows(stamp, self.index, self.entry).getbbox())
        ASSETS.compose(self.source, self.output)
        self.assertEqual(destination.read_bytes(), before)

    def test_committed_honda_shadow_remains_visible_beside_front_bumper(self):
        source = PROJECT / "assets/ui/mega/garage"
        manifest = json.loads((source / "generation.json").read_text())
        index = json.loads((source / manifest["local_render"]["index"]).read_text())
        entry = next(entry for entry in manifest["entries"] if entry["key"] == "gar1-car0")
        stamp = ASSETS.layer(source, manifest["contact_shadow"], "shadow", [512, 256])
        shadow = ASSETS.contact_shadows(stamp, index, entry).getchannel("A")
        # Regression: the first footprint was almost completely hidden by the car.
        # This small floor patch must be clear of bodywork but visibly shaded.
        patch = (915, 670, 935, 685)
        foreground = ASSETS.layer(source, entry, "foreground").getchannel("A")
        self.assertIsNone(foreground.crop(patch).getbbox())
        histogram = shadow.crop(patch).histogram()
        mean_alpha = sum(alpha * count for alpha, count in enumerate(histogram)) / sum(histogram)
        self.assertGreater(mean_alpha, 40)
        # A front-only sliver is lost at the bottom crop and navigation badge.
        # The unobstructed left tire contacts need a continuous visible band.
        for point in ((335, 550), (350, 565), (375, 590), (400, 620), (425, 645), (440, 660)):
            with self.subTest(point=point):
                self.assertEqual(foreground.getpixel(point), 0)
                self.assertGreater(shadow.getpixel(point), 40)

    def test_composition_alpha_reproduction_and_managed_cleanup(self):
        originals = {path: (path.read_bytes(), path.stat().st_mtime_ns)
                     for path in (self.background, self.foreground)}
        self.output.mkdir()
        (self.output / "gar4-car9.webp").write_bytes(b"obsolete")
        (self.output / "keep.webp").write_bytes(b"unmanaged")
        (self.output / "gar1-car10.webp").write_bytes(b"outside-managed-pattern")
        self.assertEqual(ASSETS.compose(self.source, self.output), {"active": 1, "written": 1, "removed": 1})
        destination = self.output / "gar1-car0.webp"
        with Image.open(destination) as image:
            self.assertEqual(image.size, tuple(ASSETS.CANVAS))
            self.assertEqual(image.mode, "RGB")
            for xy, expected in (((32, 32), (255, 0, 0)), ((256, 256), (0, 0, 255)), ((640, 256), (127, 0, 128))):
                self.assertTrue(all(abs(actual - wanted) <= 5 for actual, wanted in zip(image.getpixel(xy), expected)))
        before = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in self.output.iterdir()}
        self.assertEqual(ASSETS.compose(self.source, self.output), {"active": 1, "written": 0, "removed": 0})
        self.assertEqual(before, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in self.output.iterdir()})
        self.assertEqual(originals, {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in originals})
        destination.write_bytes(b"corrupted")
        self.assertEqual(ASSETS.compose(self.source, self.output)["written"], 1)
        foreground = Image.open(self.foreground).convert("RGBA")
        ImageDraw.Draw(foreground).rectangle((128, 128, 383, 383), fill=(0, 255, 0, 255))
        foreground.save(self.foreground, lossless=True)
        self.write_manifest()
        self.assertEqual(ASSETS.compose(self.source, self.output)["written"], 1)
        with Image.open(destination) as image:
            self.assertGreater(image.getpixel((256, 256))[1], 245)
        self.manifest["entries"] = []
        self.save_manifest()
        self.assertEqual(ASSETS.compose(self.source, self.output), {"active": 0, "written": 0, "removed": 1})
        self.assertEqual((self.output / "keep.webp").read_bytes(), b"unmanaged")
        self.assertEqual((self.output / "gar1-car10.webp").read_bytes(), b"outside-managed-pattern")

    def test_incomplete_layers_and_invalid_mapping_do_not_publish(self):
        for update, message in (({"foreground": "cars/missing.webp"}, "Missing foreground"),
                                ({"variant": 2}, "mapping"), ({"key": "gar1-car1"}, "mapping")):
            with self.subTest(update=update):
                entry = dict(self.entry)
                self.manifest["entries"] = [entry, dict(entry, **update)]
                # The invalid pair is checked after a valid one, before any output writes.
                if "key" not in update:
                    self.manifest["entries"][1]["key"] = "gar1-car1"
                    self.manifest["entries"][1]["car_id"] = 1
                self.save_manifest()
                with self.assertRaisesRegex(ValueError, message):
                    ASSETS.compose(self.source, self.output)
                self.assertFalse(self.output.exists())

    def test_layer_canvas_and_alpha_requirements(self):
        for kind, mode, color, size, message in (
                ("foreground", "RGB", (0, 0, 255), ASSETS.CANVAS, "transparent background"),
                ("foreground", "RGBA", (0, 0, 255, 0), ASSETS.CANVAS, "visible artwork"),
                ("background", "RGBA", (255, 0, 0, 128), ASSETS.CANVAS, "opaque"),
                ("foreground", "RGBA", (0, 0, 255, 128), [576, 360], "1146x714")):
            with self.subTest(kind=kind, mode=mode, size=size):
                path = self.foreground if kind == "foreground" else self.background
                original = path.read_bytes()
                Image.new(mode, tuple(size), color).save(path, lossless=True)
                self.write_manifest()
                with self.assertRaisesRegex(ValueError, message):
                    ASSETS.compose(self.source, self.output)
                self.assertFalse(self.output.exists())
                path.write_bytes(original)

    def test_composition_paths_preserve_sources_and_references(self):
        for output in (self.source.parent, self.source / "cars", self.source / "composed", self.source / "nested/child"):
            with self.subTest(output=output), self.assertRaisesRegex(ValueError, "directory"):
                ASSETS.compose(self.source, output)
        originals = {path: path.read_bytes() for path in (self.background, self.foreground)}
        self.assertEqual(ASSETS.compose(self.source, self.source)["active"], 1)
        self.assertEqual(originals, {path: path.read_bytes() for path in originals})
        self.foreground = self.source / "gar1-car0.webp"
        self.entry["foreground"] = "gar1-car0.webp"
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "managed output filenames"):
            ASSETS.compose(self.source, self.source)
        self.entry["foreground"] = "../escaped.webp"
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "under source-dir"):
            ASSETS.compose(self.source, self.output)

    def test_explicit_empty_foreground_only_represents_locked_bonus_cars(self):
        original = self.foreground.read_bytes()
        Image.new("RGBA", tuple(ASSETS.CANVAS), (0, 0, 0, 0)).save(self.foreground, lossless=True)
        for variant, car in ((1, 8), (3, 8), (1, 9), (2, 9)):
            with self.subTest(variant=variant, car=car):
                self.entry.update(key=f"gar{variant}-car{car}", variant=variant, car_id=car, foreground_empty=True)
                self.write_manifest()
                self.assertEqual(ASSETS.compose(self.source, self.output)["active"], 1)
                with Image.open(self.output / (self.entry["key"] + ".webp")) as image:
                    self.assertEqual(image.mode, "RGB")
                    red, green, blue = image.getpixel((256, 256))
                    self.assertGreater(red, 250)
                    self.assertLess(max(green, blue), 5)
        self.entry["foreground_empty"] = False
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "visible artwork"):
            ASSETS.compose(self.source, self.root / "rejected")
        for variant, car in ((2, 8), (4, 8), (3, 9), (4, 9), (1, 7)):
            self.entry.update(key=f"gar{variant}-car{car}", variant=variant, car_id=car, foreground_empty=True)
            self.write_manifest()
            with self.subTest(variant=variant, car=car), self.assertRaisesRegex(ValueError, "locked bonus-car mappings"):
                ASSETS.compose(self.source, self.root / "rejected")
        self.foreground.write_bytes(original)
        self.entry.update(key="gar1-car8", variant=1, car_id=8, foreground_empty=True)
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "entirely transparent"):
            ASSETS.compose(self.source, self.root / "rejected")
        self.assertFalse((self.root / "rejected").exists())

    def test_committed_fleet_covers_every_unlock_mapping(self):
        source = PROJECT / "assets/ui/mega/garage"
        manifest = json.loads((source / "generation.json").read_text())
        expected = {f"gar1-car{car}.webp" for car in range(10)}
        expected |= {f"gar{variant}-car{car}.webp" for variant in (2, 3, 4) for car in (7, 8, 9)}
        self.assertEqual({path.name for path in source.glob("*.webp")}, expected)
        self.assertEqual({entry["key"] + ".webp" for entry in manifest["entries"]}, expected)
        ASSETS.layer(source, manifest["contact_shadow"], "shadow", [512, 256])
        index = json.loads((source / manifest["local_render"]["index"]).read_text())
        for entry in manifest["entries"]:
            filename = entry["key"] + ".webp"
            with self.subTest(filename=filename):
                self.assertIn(entry["key"], index["entries"])
                for kind in ("background", "foreground"):
                    ASSETS.layer(source, entry, kind)
                with Image.open(source / filename) as image:
                    self.assertEqual(image.format, "WEBP")
                    self.assertEqual(list(image.size), ASSETS.CANVAS)
                    self.assertEqual(image.mode, "RGB")

    def test_original_crop_keeps_indices_with_the_active_frontend_palette(self):
        strips = []
        for index in range(9):
            payload = bytearray(1078 + 64 * 360)
            payload[:2] = b"BM"
            struct.pack_into("<III", payload, 2, len(payload), 0, 1078)
            # Explicit fields keep the fixture's original INDEX8 DIB contract.
            struct.pack_into("<IiiHHI", payload, 14, 40, 64, 360, 1, 8, 0)
            payload[54:1078] = bytes((0, 0, 255, 0)) * 256
            payload[1078:] = bytes([index + 1]) * (64 * 360)
            # Source BMP rows are bottom-up; the first displayed row differs.
            payload[-64:] = bytes([20 + index]) * 64
            strips.append(bytes(payload))
        palette = bytes(channel for index in range(256) for channel in (index, 255 - index, 17))
        image = ORIGINAL.original_view(strips, palette)
        self.assertEqual(image.size, (573, 357))
        self.assertEqual(image.mode, "RGB")
        self.assertEqual(image.getpixel((0, 0)), (20, 235, 17))
        self.assertEqual(image.getpixel((63, 1)), (2, 253, 17))
        self.assertEqual(image.getpixel((572, 356)), (9, 246, 17))
        with self.assertRaisesRegex(ValueError, "outside reference"):
            ORIGINAL.import_garages(self.source, self.source / "nested-import", "unused")

    def test_committed_originals_match_the_mega_roster(self):
        root = PROJECT / "assets/ui"
        source = root / "original/garage"
        expected = {key + ".webp" for key in ORIGINAL.EXPECTED}
        self.assertEqual({path.name for path in source.glob("*.webp")}, expected)
        self.assertEqual({path.name for path in (root / "mega/garage").glob("*.webp")}, expected)
        for filename in expected:
            with self.subTest(filename=filename), Image.open(source / filename) as image:
                self.assertEqual(image.format, "WEBP")
                self.assertEqual(image.mode, "RGB")
                self.assertEqual(image.size, (573, 357))


if __name__ == "__main__":
    unittest.main()
