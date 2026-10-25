#!/usr/bin/env python3
"""Menu vector contours and committed, cell-isolated Mega font atlases."""

from io import BytesIO
from pathlib import Path
import re
import sys
import unittest
from unittest.mock import patch

from PIL import Image, ImageChops, ImageDraw


PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
import hud_menu_font_geometry as FONT


def bezier_segments(path):
    """Read control points independently of the recipe's rasterizer."""
    tokens = re.findall(r"[MLCQZ]|-?\d+(?:\.\d+)?", path)
    index = 0
    current = start = (0.0, 0.0)
    while index < len(tokens):
        command = tokens[index]
        index += 1
        count = {"M": 2, "L": 2, "Q": 4, "C": 6, "Z": 0}[command]
        values = [float(value) for value in tokens[index:index + count]]
        index += count
        if command in "ML":
            current = tuple(values)
            if command == "M":
                start = current
        elif command == "Z":
            current = start
        else:
            controls = [current, *zip(values[::2], values[1::2])]
            yield controls
            current = controls[-1]


def derivative_range(coordinates):
    """Exact derivative extrema on [0,1], with the positive degree omitted."""
    if len(coordinates) == 3:
        p0, p1, p2 = coordinates
        values = [p1 - p0, p2 - p1]
    else:
        p0, p1, p2, p3 = coordinates
        a = -p0 + 3 * p1 - 3 * p2 + p3
        b = 2 * (p0 - 2 * p1 + p2)
        c = p1 - p0
        values = [c, a + b + c]
        if a and 0 < -b / (2 * a) < 1:
            t = -b / (2 * a)
            values.append(a * t * t + b * t + c)
    return min(values), max(values)


class MenuFontTests(unittest.TestCase):
    def test_curved_shoulders_and_terminals_never_reverse(self):
        # These masters split bowls at their extrema. A segment must advance
        # toward its endpoint on both axes; equal endpoint coordinates must
        # stay constant. This detects the C/G lip bumps at every weight, plus
        # the same erroneous inner-bowl handles in other letters and digits.
        characters = {character for font in FONT.MENU_FONTS.values()
                      for character in font["glyphs"] if character.isalnum()}
        for weight in (17, 22, 29):  # HELP, OPTION/settings, TITLE
            for character in sorted(characters):
                contours = FONT._master(character, weight)
                self.assertTrue(contours, (character, weight))
                for outer, holes in contours:
                    for path in (outer, *holes):
                        for controls in bezier_segments(path):
                            for axis in (0, 1):
                                values = [point[axis] for point in controls]
                                minimum, maximum = derivative_range(values)
                                with self.subTest(character=character, weight=weight, axis="xy"[axis], controls=values):
                                    if values[-1] >= values[0]:
                                        self.assertGreaterEqual(minimum, -1e-9)
                                    if values[-1] <= values[0]:
                                        self.assertLessEqual(maximum, 1e-9)

    def test_committed_atlases_preserve_recipe_alpha_cells(self):
        directory = PROJECT / "assets/ui/mega/hud"
        for index, font in FONT.MENU_FONTS.items():
            with self.subTest(font=index):
                # The replacement recipe must remain independent of bitmap inputs.
                with patch.object(Image, "open", side_effect=AssertionError("Menu vector recipe must not read bitmaps")):
                    authored = FONT.menu_font_atlas(index)
                data = (directory / font["filename"]).read_bytes()
                encoded = Image.open(BytesIO(data)).convert("RGBA")
                self.assertEqual(encoded.size, tuple(value * 4 for value in font["atlas_size"]))
                alpha = encoded.getchannel("A")
                self.assertEqual(alpha.tobytes(), authored.getchannel("A").tobytes())
                self.assertTrue(any(0 < value < 255 for value in alpha.getdata()))
                permitted = Image.new("L", encoded.size)
                draw = ImageDraw.Draw(permitted)
                for character, (x, y, width, height) in font["glyphs"].items():
                    box = (x * 4, y * 4, (x + width) * 4, (y + height) * 4)
                    cell = alpha.crop(box)
                    self.assertIsNotNone(cell.getbbox(), (index, character))
                    edges = (cell.crop((0, 0, cell.width, 1)), cell.crop((0, cell.height - 1, cell.width, cell.height)),
                             cell.crop((0, 0, 1, cell.height)), cell.crop((cell.width - 1, 0, cell.width, cell.height)))
                    self.assertTrue(all(edge.getbbox() is None for edge in edges), (index, character))
                    draw.rectangle((box[0], box[1], box[2] - 1, box[3] - 1), fill=255)
                self.assertIsNone(ImageChops.subtract(alpha, permitted).getbbox(), "Unassigned cells must stay transparent")


if __name__ == "__main__":
    unittest.main()
