"""Authored continuous contours for ALLCARS WIN3's upright car-name face.

Only the original cell directory and metrics are retained as numeric records.
The heavy stems, narrow counters and shallow rounded bowls are new vector
geometry, rasterized without original bitmap pixels or an installed typeface.
Unlike the HUD faces, WIN3 has white ink without an outline or shadow.
"""

from PIL import Image, ImageChops, ImageDraw

from hud_font_geometry import flatten_path, rounded_box


CAR_NAME_SCALE = 2
CAR_NAME_ATLAS_SIZE = (256, 256)
CAR_NAME_REFERENCE_CRC32 = "afa81da8"
CAR_NAME_LINE_HEIGHT = 14
CAR_NAME_SPACE_WIDTH = 8
CAR_NAME_ADVANCE = -1


def _records():
    rows = [("ABCDEFGHIJKLMNOPQRSTUVWXYZ01", [9, 8, 7, 8, 7, 6, 7, 8, 3, 7, 8, 6, 11, 9, 8, 8, 8, 9, 8, 7, 8, 8, 13, 9, 9, 7, 8, 5]),
            ("23456789'.", [8, 8, 10, 8, 8, 8, 8, 8, 5, 4])]
    records = {}
    for row, (characters, widths) in enumerate(rows):
        x = 0
        for character, width in zip(characters, widths, strict=True):
            records[character] = (x, row * 14, width, 14)
            x += width + 1
    return records


CAR_NAME_GLYPHS = _records()
CAR_NAME_INK_BOUNDS = {character: (0, 0, width, height) for character, (_, _, width, height) in CAR_NAME_GLYPHS.items()}
CAR_NAME_INK_BOUNDS["'"] = (0, 0, 5, 5)
CAR_NAME_INK_BOUNDS["."] = (0, 12, 4, 14)


def _shape(outer, *holes):
    return [(outer, holes)]


def _rect(left, top, right, bottom):
    return _shape(rounded_box(left, top, right, bottom, 0))


# WIN3's three-unit vertical stems and two-unit horizontal strokes are distinct
# from LIL640's larger beveled face. Smooth bowls retain WIN3's almost-square
# shoulders. O, Q and zero deliberately share the source's untailed silhouette.
_BOWL = rounded_box(0, 0, 8, 14, 1.3)
_COUNTER = rounded_box(3, 2, 5, 12, 0.12)
CAR_NAME_OUTLINES = {
    "A": _shape("M0 14 L3.1 0 L5.9 0 L9 14 L6 14 L5.3 10 L3.7 10 L3 14 Z", "M4.1 7.7 L4.9 7.7 L4.5 4.8 Z"),
    "B": _shape("M0 0 L6.6 0 C7.5 0 8 0.6 8 1.5 L8 5.3 C8 6.1 7.5 6.6 6.8 7 C7.5 7.3 8 7.9 8 8.7 L8 12.5 C8 13.5 7.4 14 6.6 14 L0 14 Z",
                rounded_box(3, 2, 5, 6, 0.1), rounded_box(3, 8, 5, 12, 0.1)),
    "C": _shape("M7 1.5 L7 5 L4 5 L4 2 L3 2 L3 12 L4 12 L4 8 L7 8 L7 12.5 C7 13.5 6.4 14 5.5 14 L1.4 14 C0.5 14 0 13.4 0 12.5 L0 1.5 C0 0.5 0.5 0 1.4 0 L5.5 0 C6.4 0 7 0.5 7 1.5 Z"),
    "D": _shape("M0 0 L6.5 0 C7.4 0 8 0.7 8 1.6 L8 12.4 C8 13.3 7.4 14 6.5 14 L0 14 Z", rounded_box(3, 2, 5, 12, 0.1)),
    "E": _rect(0, 0, 3, 14) + _rect(0, 0, 7, 2) + _rect(0, 6, 7, 8) + _rect(0, 12, 7, 14),
    "F": _rect(0, 0, 3, 14) + _rect(0, 0, 6, 2) + _rect(0, 6, 6, 8),
    "G": _shape("M7 1.4 L7 5 L4 5 L4 2 L3 2 L3 12 L4 12 L4 9 L3 9 L3 7 L7 7 L7 14 L1.4 14 C0.5 14 0 13.4 0 12.5 L0 1.5 C0 0.5 0.5 0 1.4 0 L5.5 0 C6.5 0 7 0.5 7 1.4 Z"),
    "H": _rect(0, 0, 3, 14) + _rect(5, 0, 8, 14) + _rect(0, 6, 8, 8),
    "I": _rect(0, 0, 3, 14),
    "J": _shape("M4 0 L7 0 L7 12.4 C7 13.4 6.4 14 5.4 14 L1.4 14 C0.5 14 0 13.4 0 12.4 L0 8 L3 8 L3 12 L4 12 Z"),
    "K": _shape("M0 0 L3 0 L3 4.4 L5.6 0 L8 0 L4.7 6.7 L8 14 L5.2 14 L3 8.7 L3 14 L0 14 Z"),
    "L": _rect(0, 0, 3, 14) + _rect(0, 12, 6, 14),
    "M": _shape("M0 14 L0 0 L4 0 L5.5 6.2 L7 0 L11 0 L11 14 L8 14 L8 6.2 L6.8 14 L4.2 14 L3 6.2 L3 14 Z"),
    "N": _shape("M0 14 L0 0 L3 0 L6 5.4 L6 0 L9 0 L9 14 L6 14 L3 8.6 L3 14 Z"),
    "O": _shape(_BOWL, _COUNTER),
    "P": _shape("M0 14 L0 0 L6.5 0 C7.5 0 8 0.6 8 1.5 L8 7.5 C8 8.5 7.5 9 6.5 9 L3 9 L3 14 Z", rounded_box(3, 2, 5, 7, 0.1)),
    "Q": _shape(_BOWL, _COUNTER),
    "R": _shape("M0 14 L0 0 L6.4 0 C7.5 0 8 0.6 8 1.5 L8 6.1 C8 7 7.5 7.5 6.6 7.8 L9 14 L5.6 14 L3.7 8 L3 8 L3 14 Z", rounded_box(3, 2, 5, 6, 0.1)),
    "S": _shape("M8 1.5 L8 5 L5 5 L5 2 L3 2 L3 4.1 C3 4.8 3.6 5.2 4.7 6.2 L6.4 7.7 C7.5 8.6 8 9.2 8 10.4 L8 12.5 C8 13.5 7.4 14 6.4 14 L1.5 14 C0.5 14 0 13.5 0 12.5 L0 9 L3 9 L3 12 L5 12 L5 10.4 C5 9.7 4.5 9.3 3.4 8.3 L1.6 6.6 C0.5 5.6 0 5 0 3.8 L0 1.5 C0 0.5 0.5 0 1.5 0 L6.5 0 C7.5 0 8 0.5 8 1.5 Z"),
    "T": _rect(0, 0, 7, 2) + _rect(2, 0, 5, 14),
    "U": _shape("M0 0 L3 0 L3 12 L5 12 L5 0 L8 0 L8 12.5 C8 13.5 7.5 14 6.5 14 L1.5 14 C0.5 14 0 13.5 0 12.5 Z"),
    "V": _shape("M0 0 L3 0 L4 7 L5 0 L8 0 L5.5 14 L2.5 14 Z"),
    "W": _shape("M0 0 L3 0 L4.2 7.3 L5.3 0 L7.7 0 L8.8 7.3 L10 0 L13 0 L10.8 14 L7.7 14 L6.5 7.1 L5.3 14 L2.2 14 Z"),
    "X": _shape("M0 0 L3.2 0 L4.5 4 L5.8 0 L9 0 L6.1 7 L9 14 L5.8 14 L4.5 10 L3.2 14 L0 14 L2.9 7 Z"),
    "Y": _shape("M0 0 L3.2 0 L4.5 4.3 L5.8 0 L9 0 L6 8.1 L6 14 L3 14 L3 8.1 Z"),
    "Z": _shape("M0 0 L7 0 L7 3 L3 12 L7 12 L7 14 L0 14 L0 11 L4 2 L0 2 Z"),
    "0": _shape(_BOWL, _COUNTER),
    "1": _shape("M0 2.2 L2 0 L5 0 L5 14 L2 14 L2 4 L0 4 Z"),
    "2": _shape("M0 1.5 C0 0.5 0.5 0 1.5 0 L6.5 0 C7.5 0 8 0.5 8 1.5 L8 5 C8 6.2 7.6 7 6.6 8 L3 11.5 L3 12 L8 12 L8 14 L0 14 L0 10.4 L4.3 5.9 C4.8 5.4 5 5 5 4.3 L5 2 L3 2 L3 5 L0 5 Z"),
    "3": _shape("M0 1.5 C0 0.5 0.5 0 1.5 0 L6.5 0 C7.5 0 8 0.6 8 1.5 L8 5.4 C8 6.3 7.5 6.7 6.7 7 C7.5 7.3 8 7.8 8 8.6 L8 12.5 C8 13.5 7.5 14 6.5 14 L1.5 14 C0.5 14 0 13.5 0 12.5 L0 9 L3 9 L3 12 L5 12 L5 8 L2 8 L2 6 L5 6 L5 2 L3 2 L3 5 L0 5 Z"),
    "4": _shape("M3.5 0 L8 0 L8 10 L10 10 L10 12 L8 12 L8 14 L5 14 L5 12 L0 12 L0 9.8 Z", "M2.7 10 L5 10 L5 3.3 Z"),
    "5": _shape("M0 0 L8 0 L8 2 L3 2 L3 5.6 C4 5.2 4.8 5 5.8 5 C7.2 5 8 5.8 8 7.2 L8 12.5 C8 13.5 7.5 14 6.5 14 L1.5 14 C0.5 14 0 13.5 0 12.5 L0 8 L3 8 L3 12 L5 12 L5 7 L0 7 Z"),
    "6": _shape("M8 1.5 L8 5 L5 5 L5 2 L3 2 L3 6 L6.5 6 C7.5 6 8 6.5 8 7.5 L8 12.5 C8 13.5 7.5 14 6.5 14 L1.5 14 C0.5 14 0 13.5 0 12.5 L0 1.5 C0 0.5 0.5 0 1.5 0 L6.5 0 C7.5 0 8 0.5 8 1.5 Z", rounded_box(3, 8, 5, 12, 0.1)),
    "7": _shape("M0 0 L8 0 L8 3.5 L3.5 14 L0.3 14 L5 2 L0 2 Z"),
    "8": _shape("M1.5 0 L6.5 0 C7.5 0 8 0.5 8 1.5 L8 5.4 C8 6.2 7.5 6.7 6.7 7 C7.5 7.3 8 7.8 8 8.6 L8 12.5 C8 13.5 7.5 14 6.5 14 L1.5 14 C0.5 14 0 13.5 0 12.5 L0 8.6 C0 7.8 0.5 7.3 1.3 7 C0.5 6.7 0 6.2 0 5.4 L0 1.5 C0 0.5 0.5 0 1.5 0 Z", rounded_box(3, 2, 5, 6, 0.1), rounded_box(3, 8, 5, 12, 0.1)),
    "9": _shape("M0 1.5 C0 0.5 0.5 0 1.5 0 L6.5 0 C7.5 0 8 0.5 8 1.5 L8 12.5 C8 13.5 7.5 14 6.5 14 L1.5 14 C0.5 14 0 13.5 0 12.5 L0 9 L3 9 L3 12 L5 12 L5 8 L1.5 8 C0.5 8 0 7.5 0 6.5 Z", rounded_box(3, 2, 5, 6, 0.1)),
    "'": _shape("M1 0 L5 0 L5 2 L2.2 5 L0 5 L1 2.8 Z"),
    ".": _rect(0, 12, 4, 14),
}


def car_name_glyph(character, scale=CAR_NAME_SCALE, antialias=4):
    """Rasterize one cell independently; retain exact ink bounds without a rim."""
    if type(scale) is not int or scale != CAR_NAME_SCALE:
        raise ValueError("Car-name artwork must use exactly two times the original resolution")
    if type(antialias) is not int or not 1 <= antialias <= 4:
        raise ValueError("Car-name antialias factor must be an integer in 1..4")
    _, _, width, height = CAR_NAME_GLYPHS[character]
    high = scale * antialias
    mask = Image.new("L", (width * high, height * high))
    for outer, holes in CAR_NAME_OUTLINES[character]:
        part = Image.new("L", mask.size)
        draw = ImageDraw.Draw(part)
        for path, value in [(outer, 255), *((hole, 0) for hole in holes)]:
            draw.polygon([(x * high, y * high) for x, y in flatten_path(path)], fill=value)
        mask = ImageChops.lighter(mask, part)
    if antialias != 1:
        mask = mask.resize((width * scale, height * scale), Image.Resampling.LANCZOS)
    # Do not let the antialias kernel add ink above the original punctuation.
    bounds = tuple(value * scale for value in CAR_NAME_INK_BOUNDS[character])
    bounded = Image.new("L", mask.size)
    bounded.paste(mask.crop(bounds), bounds[:2])
    result = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    result.putalpha(bounded)
    return result


def car_name_atlas(scale=CAR_NAME_SCALE, antialias=4):
    """Pack precisely the 38 original WIN3 glyphs into a 512x512 white atlas."""
    if type(scale) is not int or scale != CAR_NAME_SCALE:
        raise ValueError("Car-name artwork must use exactly two times the original resolution")
    atlas = Image.new("RGBA", tuple(value * scale for value in CAR_NAME_ATLAS_SIZE), (255, 255, 255, 0))
    for character, (x, y, _, _) in CAR_NAME_GLYPHS.items():
        atlas.paste(car_name_glyph(character, scale, antialias), (x * scale, y * scale))
    return atlas
