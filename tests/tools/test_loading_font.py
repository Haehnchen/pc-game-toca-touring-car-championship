#!/usr/bin/env python3
"""LOADHIGH cells, fresh vector edges and committed artwork."""
from io import BytesIO
from pathlib import Path
import runpy
import struct
import sys
import unittest
from unittest.mock import patch
import zlib
import numpy as np
from PIL import Image, ImageChops, ImageDraw

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from git_game_data import configured_data_root
import loading_font_geometry as FONT


def crc32(data):
    return f"{zlib.crc32(data):08x}"


class LoadingFontTests(unittest.TestCase):
    def test_smooth_blue_face_stays_in_original_cells_without_bitmap_inputs(self):
        with patch.object(Image, "open", side_effect=AssertionError("No bitmap input belongs in vector authoring")):
            atlas = FONT.loading_font_atlas()
        self.assertEqual((atlas.mode, atlas.size), ("RGBA", (1024, 1024)))
        self.assertEqual(len(FONT.LOADING_GLYPHS), 67)
        alpha = atlas.getchannel("A")
        self.assertGreater(sum(0 < value < 255 for value in alpha.tobytes()), 1000)
        self.assertEqual(atlas.convert("RGB").tobytes(), bytes((8, 82, 173)) * 1024 * 1024)
        allowed = Image.new("L", atlas.size)
        draw = ImageDraw.Draw(allowed)
        for character, (x, y, width, height) in FONT.LOADING_GLYPHS.items():
            cell = alpha.crop((x * 4, y * 4, (x + width) * 4, (y + height) * 4))
            self.assertIsNotNone(cell.getbbox(), character)
            left, top, right, bottom = FONT.LOADING_INK_BOUNDS[character]
            draw.rectangle(((x + left) * 4, (y + top) * 4, (x + right) * 4 - 1, (y + bottom) * 4 - 1), fill=255)
        self.assertIsNone(ImageChops.subtract(alpha, allowed).getbbox(), "Original unused rows/cells must stay transparent")

    def test_committed_asset_matches_vector_alpha(self):
        directory = PROJECT / "assets/ui/mega/loading"
        data = (directory / "font.webp").read_bytes()
        decoded = Image.open(BytesIO(data)).convert("RGBA")
        self.assertEqual(decoded.size, (1024, 1024))
        self.assertEqual(decoded.getchannel("A").tobytes(), FONT.loading_font_atlas().getchannel("A").tobytes())

    def test_canonical_codes_cells_metrics_and_ink_extents(self):
        archive = (configured_data_root() / "data/loadhigh.bfp").read_bytes()
        self.assertEqual(crc32(archive), "af7b3a1d")
        count, table_size = struct.unpack_from("<II", archive)
        self.assertGreater(count, 0)
        offset, size = struct.unpack_from("<II", archive, 28)
        source = archive[12 + table_size + offset:12 + table_size + offset + size]
        self.assertEqual(crc32(source), FONT.LOADING_REFERENCE_CRC32)
        width, height = struct.unpack_from("<II", source, 18)
        self.assertEqual((width, height), FONT.LOADING_ATLAS_SIZE)
        appendix = 0x436 + ((width + 3) & ~3) * height
        self.assertEqual(struct.unpack_from("<iii", source, appendix + 4), (11, 10, -1))
        self.assertEqual(source[appendix + 1], len(FONT.LOADING_GLYPHS))
        reference = Image.open(BytesIO(source)).convert("RGB").convert("L")
        generated = FONT.loading_font_atlas().getchannel("A")
        for index in range(source[appendix + 1]):
            code, x, y, w, h = struct.unpack_from("<BxHHHH", source, appendix + 24 + index * 16)
            character = chr(code)
            self.assertEqual(FONT.LOADING_GLYPHS[character], (x, y, w, h), character)
            original = reference.crop((x, y, x + w, y + h))
            self.assertEqual(original.getbbox(), FONT.LOADING_INK_BOUNDS[character], character)
            if character in "AKNVWXZ":
                cell = generated.crop((x * 4, y * 4, (x + w) * 4, (y + h) * 4))
                for resampler in (Image.Resampling.NEAREST, Image.Resampling.BILINEAR, Image.Resampling.LANCZOS):
                    self.assertNotEqual(cell.tobytes(), original.resize(cell.size, resampler).tobytes(), character)


TRACKS = {
    "donington": (3, "b25b18ed", ["LOADING..", "DONINGTON", "PARK(GP)"]),
    "silverstone": (6, "05763141", ["LOADING..", "SILVERSTONE", "INTERNATIONAL"]),
    "thruxton": (8, "cc51de50", ["LOADING..", "THRUXTON"]),
    "brands-hatch": (1, "dc6eb2a9", ["LOADING..", "BRANDSHATCH", "INDY"]),
    "oulton-park": (5, "6a940289", ["LOADING..", "OULTONPARK"]),
    "donington-short": (9, "2dccf331", ["LOADING..", "DONINGTON", "PARK(SHORT)"]),
    "croft": (2, "5ed129e7", ["LOADING..", "CROFT"]),
    "knockhill": (4, "b776c64e", ["LOADING..", "KNOCKHILL"]),
    "snetterton": (7, "3f18e2c7", ["LOADING..", "SNETTERTON"]),
    "lavaland": (10, "87272ceb", ["LOADING.."]),
}

# Deck rims measured independently in the committed artwork, in original
# pixels relative to (24,312). The front point is the gray/brown surface seam,
# not the lower tip of the block's side wall. Order: back, right, front, left.
MEGA_DECKS = {
    "donington": ((134.5, 4), (256.5, 56.5), (123, 139), (6, 50.5)),
    "silverstone": ((126.5, 7), (253.75, 67.25), (121, 144), (7, 57.75)),
    "thruxton": ((133.75, 7), (255.75, 60.25), (121, 140), (7, 51.25)),
    "brands-hatch": ((144.25, 6), (255.5, 62.5), (122, 132), (5.25, 42.25)),
    "oulton-park": ((132.25, 7), (256.75, 53.25), (123, 122), (7.25, 46.5)),
    "donington-short": ((135.5, 8), (257.5, 50), (124, 120), (7.25, 46.5)),
    "croft": ((132, 6), (255.75, 58.25), (123, 139), (5, 48.25)),
    "knockhill": ((127.25, 7), (249.75, 60.75), (118, 121), (7, 53.25)),
    "snetterton": ((135.25, 8), (257.75, 62.25), (124, 140), (8, 54.5)),
}


class LoadingArtworkPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = PROJECT / "assets/ui/mega/loading"
        cls.composer = runpy.run_path(str(PROJECT / "tools/compose-loading-artwork.py"))
        archive = (configured_data_root() / "data/loadhigh.bfp").read_bytes()
        entries = cls.composer["READ_BFP"](archive)
        cls.originals, cls.paths = {}, {}
        for name, member, _, _, bottom in cls.composer["TRACKS"]:
            _, _, offset, size = entries[member]
            cls.originals[name] = Image.open(BytesIO(archive[offset:offset + size])).convert("RGB")
            if name != "lavaland":
                paths, _ = cls.composer["route_geometry"](cls.originals[name], bottom)
                cls.paths[name], _ = cls.composer["place_route"](name, paths, cls.composer["marker_shapes"](name))

    def image(self, name):
        with Image.open(self.directory / f"{name}.webp") as image:
            self.assertEqual(image.format, "WEBP", name)
            self.assertEqual(image.size, (2560, 1920), name)
            self.assertEqual(image.convert("RGBA").getchannel("A").getextrema(), (255, 255), name)
            return image.convert("RGB")

    def assert_black(self, image, box, label):
        region = image.crop(tuple(value * 4 for value in box))
        self.assertLessEqual(max(high for low, high in region.getextrema()), 5, label)

    @staticmethod
    def route_mask(image):
        data = np.asarray(image.crop((24 * 4, 312 * 4, 288 * 4, 480 * 4)), dtype=np.int16)
        red, green, blue = (data[:, :, channel] for channel in range(3))
        # Recover saturated stroke cores without confusing the newly visible
        # brown side walls with yellow antialiasing around the route.
        return (red > 115) & (green > 110) & (blue < 85) & (abs(red - green) < 42) & (green > blue * 2.5)

    def test_finished_files_preserve_statistics_and_margin_areas(self):
        for filename in (f"{name}.webp" for name in (*TRACKS, "sides")):
            with self.subTest(file=filename):
                image = self.image(filename.removesuffix(".webp"))
                if filename != "sides.webp":
                    # Leave room for the Lavaland underline ending at y322.
                    top = 330 if filename == "lavaland.webp" else 310
                    self.assert_black(image, (312, top, 636, 478), f"Live statistics area: {filename}")
        sides = self.image("sides")
        # The small guard excludes WebP edge ringing at the two decoration bands.
        self.assert_black(sides, (0, 235, 640, 480), "Sides must contain no lower captions, maps or statistics")
        self.assert_black(sides, (0, 74, 640, 220), "Sides retain only flag and fire decoration")
        self.assertGreater(max(high for low, high in sides.crop((0, 0, 2560, 72 * 4)).getextrema()), 100)

    def test_track_mapping_captions_and_visible_route_geometry(self):
        masks = {}
        for name, (member, checksum, captions) in TRACKS.items():
            with self.subTest(track=name):
                if name == "lavaland":
                    continue
                image = self.image(name)
                yellow = self.route_mask(image)
                masks[name] = yellow
                self.assertGreater(int(yellow.sum()), 500, name)
                points = [point for path in self.paths[name] for point in path]
                self.assertGreater(len(points), 5, name)
                self.assertEqual(len(self.paths[name]), 1, f"Faint source ink and the direction marker must not leave route gaps: {name}")
                self.assertEqual(self.paths[name][0][0], self.paths[name][0][-1], f"The circuit must close: {name}")
                hits = 0
                for x, y in points:
                    x, y = round(x * 4), round(y * 4)
                    # The encoded strokes must follow the placed route; the
                    # crop is relative to the original relief box.
                    hits += bool(yellow[max(0, y - 5):y + 6, max(0, x - 5):x + 6].any())
                self.assertGreater(hits / len(points), .8, name)
                self.assertTrue(self.composer["marker_shapes"](name), name)
                data = np.asarray(image.crop((24 * 4, 312 * 4, 288 * 4, 480 * 4)), dtype=np.int16)
                red = (data[:, :, 0] > 120) & (data[:, :, 0] > data[:, :, 1] * 2) & (data[:, :, 0] > data[:, :, 2] * 2)
                self.assertGreater(int(red.sum()), 30, name)
        gp, short = masks["donington"], masks["donington-short"]
        self.assertNotEqual(self.paths["donington"], self.paths["donington-short"])
        self.assertGreater(int(np.logical_xor(gp, short).sum()), .1 * min(int(gp.sum()), int(short.sum())), "GP and Short must show different routes")

    def test_encoded_routes_and_arrows_are_centered_inside_the_relief_decks(self):
        for name, corners in MEGA_DECKS.items():
            with self.subTest(track=name):
                image = self.image(name)
                data = np.asarray(image.crop((24 * 4, 312 * 4, 288 * 4, 480 * 4)), dtype=np.int16)
                red = (data[:, :, 0] > 120) & (data[:, :, 0] > data[:, :, 1] * 2) & (data[:, :, 0] > data[:, :, 2] * 2)
                # Measure actual WebP ink, including the marker, independently
                # of the composer's source recovery and projective transform.
                rows, columns = np.where(self.route_mask(image) | red)
                points = np.column_stack((columns + .5, rows + .5)) / 4
                deck = np.asarray(corners, dtype=float)
                distances = []
                for start, end in zip(deck, np.roll(deck, -1, axis=0), strict=True):
                    edge = end - start
                    offset = points - start
                    distances.append((edge[0] * offset[:, 1] - edge[1] * offset[:, 0]) / np.linalg.norm(edge))
                distances = np.asarray(distances)
                self.assertGreater(float(distances.min()), 3, f"Route/arrow crosses or crowds the relief rim: {name}")
                # Independently rasterize the measured deck to obtain its
                # visible area center; the diagonal intersection lies too far
                # back on these asymmetric reliefs. Compare ink extents, since
                # an asymmetric circuit's ink centroid is not its placement
                # center. No production transform or centroid helper is used.
                surface = Image.new("L", (264 * 4, 168 * 4))
                ImageDraw.Draw(surface).polygon([tuple(point * 4) for point in deck], fill=255)
                surface_y, surface_x = np.where(np.asarray(surface))
                center = np.asarray((surface_x.mean() + .5, surface_y.mean() + .5)) / 4
                ink_center = (points.min(axis=0) + points.max(axis=0)) / 2
                for actual, expected in zip(ink_center, center, strict=True):
                    self.assertAlmostEqual(float(actual), float(expected), delta=1.5, msg=f"Route/arrow is off center on the deck: {name}")

    def test_canonical_members_joined_routes_and_source_caption_boxes(self):
        composer = runpy.run_path(str(PROJECT / "tools/compose-loading-artwork.py"))
        archive = (configured_data_root() / "data/loadhigh.bfp").read_bytes()
        self.assertEqual(crc32(archive), "af7b3a1d")
        count, table_size = struct.unpack_from("<II", archive)
        self.assertEqual(count, 11)
        for name, (member, checksum, _) in TRACKS.items():
            offset, size = struct.unpack_from("<II", archive, 12 + member * 24 + 16)
            raw = archive[12 + table_size + offset:12 + table_size + offset + size]
            self.assertEqual(crc32(raw), checksum, name)
            original = Image.open(BytesIO(raw)).convert("RGB")
            if name in ("donington", "silverstone"):
                paths, arrows = composer["route_geometry"](original, 98 if name == "donington" else 116)
                self.assertEqual(len(paths), 1, f"Source gaps must join into one circuit: {name}")
                self.assertTrue(arrows, name)
            source = np.asarray(original, dtype=np.int16)
            blue = (source[:, :, 2] > source[:, :, 0] * 2) & (source[:, :, 2] > source[:, :, 1] * 1.4) & (source[:, :, 1] > 30)
            canvas = Image.new("RGB", (2560, 1920))
            labels = next(labels for track, _, _, labels, _ in composer["TRACKS"] if track == name)
            for line, text in enumerate(["LOADING..", *labels]):
                left, top, right, bottom = (24, 240, 294, 282) if line == 0 else (310, 242 + (line - 1) * 24, 618, 264 + (line - 1) * 24)
                for glyph in composer["caption"](canvas, original, text, (left, top, right, bottom)):
                    x, y, width, height = glyph["xywh"]
                    self.assertTrue(left <= x < x + width <= right and top <= y < y + height <= bottom, (name, glyph))
                    ink = blue[y:y + height, x:x + width]
                    self.assertTrue(ink[0].any() and ink[-1].any() and ink[:, 0].any() and ink[:, -1].any(), (name, glyph))
        # Embedded headings use independent large contours, while live status
        # retains the separately reconstructed LOADHIGH face and its metrics.
        with patch.object(Image, "open", side_effect=AssertionError("Caption contours must not open source bitmaps")):
            caption = composer["letter"]("A", 26, 28)
        self.assertEqual(caption.size, (104, 112))
        self.assertGreater(sum(0 < value < 255 for value in caption.tobytes()), 50)


if __name__ == "__main__":
    unittest.main()
