"""Authored vector reconstruction of the original BIG640 caption typeface.

No bitmap input, host typeface or pixel enlargement is used. Original atlas
cells and text metrics remain fixed; new curves are rasterized at the requested
resolution. The neutral white face allows all original consumers to tint it.
"""

import re

from PIL import Image, ImageChops, ImageDraw, ImageFilter


CAPTION_ATLAS_SIZE = (256, 256)
CAPTION_LINE_HEIGHT = 36
CAPTION_SPACE_WIDTH = 32
CAPTION_ADVANCE = -1
CAPTION_BASELINE = 32
CAPTION_REFERENCE_CRC32 = "399cab32"
CAPTION_GLYPHS = {
    "A": (0, 0, 37, 36), "B": (38, 0, 38, 36), "C": (77, 0, 37, 36), "D": (115, 0, 38, 36), "E": (154, 0, 39, 36), "F": (194, 0, 37, 36),
    "G": (0, 36, 37, 36), "H": (38, 36, 43, 36), "I": (82, 36, 24, 36), "J": (107, 36, 36, 36), "K": (144, 36, 45, 36), "L": (190, 36, 31, 36),
    "M": (0, 72, 47, 36), "N": (48, 72, 43, 36), "O": (92, 72, 38, 36), "P": (131, 72, 37, 36), "Q": (169, 72, 38, 36), "R": (208, 72, 39, 36),
    "S": (0, 108, 35, 36), "T": (36, 108, 35, 36), "U": (72, 108, 40, 36), "V": (113, 108, 37, 36), "W": (151, 108, 49, 36), "X": (201, 108, 45, 36),
    "Y": (0, 144, 37, 36), "Z": (38, 144, 42, 36), "0": (81, 144, 32, 36), "1": (114, 144, 27, 36), "2": (142, 144, 37, 36),
    "3": (180, 144, 33, 36), "4": (214, 144, 35, 36), "5": (0, 180, 35, 36), "6": (36, 180, 33, 36), "7": (70, 180, 33, 36),
    "8": (104, 180, 33, 36), "9": (138, 180, 33, 36), "!": (172, 180, 23, 36), "?": (196, 180, 31, 36), ".": (228, 180, 16, 36),
    ",": (0, 216, 17, 36), "-": (18, 216, 20, 36), "'": (39, 216, 18, 36), "[": (58, 216, 24, 36), "]": (83, 216, 31, 36),
    ":": (115, 216, 21, 36), "#": (137, 216, 26, 36),
}


def _shape(outer, *holes):
    return [(outer, holes)]


# Independent upright contour drawings. The shared original italic angle is
# applied in vector space before fitting the original red-ink bounds.
_O = "M15 0 C25 0 30 5 30 14 C30 23 25 29 15 29 C5 29 0 24 0 15 C0 6 5 0 15 0 Z"
_O_HOLE = "M15 7 C11 7 10 10 10 15 C10 20 11 22 15 22 C19 22 20 19 20 14 C20 9 19 7 15 7 Z"
_P = "M0 0 L18 0 C25 0 28 3 28 9 C28 15 24 19 17 19 L10 19 L10 29 L0 29 Z"
_P_HOLE = "M10 6 L15 6 C18 6 19 7 19 9 C19 12 17 13 14 13 L10 13 Z"
_OUTLINES = {
    "A": _shape("M11 0 L22 0 L32 29 L22 29 L20 23 L11 23 L9 29 L0 29 Z", "M13 17 L18 17 L15.5 8 Z"),
    "B": _shape("M0 0 L18 0 C25 0 28 3 28 8 C28 12 26 14 22 15 C27 16 29 18 29 22 C29 27 25 29 18 29 L0 29 Z",
                "M10 6 L15 6 C18 6 19 7 19 9 C19 11 18 12 15 12 L10 12 Z", "M10 18 L16 18 C19 18 20 19 20 21 C20 23 19 24 16 24 L10 24 Z"),
    "C": _shape("M29 9 L19 10 C19 7 18 6 15 6 C11 6 10 9 10 15 C10 20 11 23 15 23 C18 23 19 21 20 18 L30 19 C28 26 24 29 15 29 C5 29 0 24 0 15 C0 5 6 0 15 0 C23 0 28 3 29 9 Z"),
    "D": _shape("M0 0 L14 0 C25 0 30 5 30 14 C30 24 25 29 14 29 L0 29 Z", "M10 7 L13 7 C18 7 20 9 20 14 C20 20 18 22 13 22 L10 22 Z"),
    "E": _shape("M0 0 L25 0 L25 6 L10 6 L10 11 L24 11 L24 17 L10 17 L10 23 L25 23 L25 29 L0 29 Z"),
    "F": _shape("M0 0 L24 0 L24 7 L10 7 L10 12 L23 12 L23 18 L10 18 L10 29 L0 29 Z"),
    "G": _shape("M30 9 L20 10 C19 7 18 6 15 6 C11 6 10 9 10 15 C10 21 11 23 15 23 C18 23 20 22 21 20 L15 20 L15 14 L30 14 L30 26 C25 28 21 29 15 29 C5 29 0 24 0 15 C0 5 6 0 15 0 C24 0 28 3 30 9 Z"),
    "H": _shape("M0 0 L10 0 L10 10 L20 10 L20 0 L30 0 L30 29 L20 29 L20 18 L10 18 L10 29 L0 29 Z"),
    "I": _shape("M0 0 L10 0 L10 29 L0 29 Z"),
    "J": _shape("M14 0 L24 0 L24 19 C24 26 20 29 12 29 C4 29 0 26 0 19 L9 18 C9 22 10 23 12 23 C14 23 14 21 14 19 Z"),
    "K": _shape("M0 0 L10 0 L10 10 L20 0 L32 0 L20 13 L33 29 L21 29 L13 19 L10 22 L10 29 L0 29 Z"),
    "L": _shape("M0 0 L10 0 L10 22 L24 22 L24 29 L0 29 Z"),
    "M": _shape("M0 0 L13 0 L18 17 L23 0 L36 0 L36 29 L27 29 L27 10 L22 29 L14 29 L9 10 L9 29 L0 29 Z"),
    "N": _shape("M0 0 L11 0 L21 16 L21 0 L30 0 L30 29 L20 29 L9 12 L9 29 L0 29 Z"),
    "O": _shape(_O, _O_HOLE),
    "P": _shape(_P, _P_HOLE),
    "Q": _shape(_O, _O_HOLE) + _shape("M16 18 L23 18 L31 27 L25 32 L15 22 Z"),
    "R": _shape("M0 0 L18 0 C25 0 29 3 29 9 C29 14 26 17 21 18 C25 19 27 23 30 29 L18 29 L13 20 L10 20 L10 29 L0 29 Z",
                "M10 6 L16 6 C19 6 20 7 20 9 C20 12 18 13 15 13 L10 13 Z"),
    "S": _shape("M28 8 L18 9 C18 6 16 5 13 5 C10 5 9 6 9 8 C9 10 11 10 16 11 C25 13 29 15 29 21 C29 27 23 29 14 29 C5 29 0 26 0 20 L10 19 C10 22 12 24 15 24 C18 24 20 23 20 21 C20 19 18 19 13 18 C4 16 0 14 0 8 C0 3 5 0 14 0 C23 0 27 3 28 8 Z"),
    "T": _shape("M0 0 L29 0 L29 7 L19 7 L19 29 L9 29 L9 7 L0 7 Z"),
    "U": _shape("M0 0 L10 0 L10 19 C10 22 11 23 14 23 C17 23 18 22 18 19 L18 0 L28 0 L28 19 C28 26 23 29 14 29 C5 29 0 26 0 19 Z"),
    "V": _shape("M0 0 L10 0 L16 19 L22 0 L32 0 L21 29 L11 29 Z"),
    "W": _shape("M0 0 L10 0 L14 19 L18 0 L28 0 L32 19 L36 0 L46 0 L38 29 L27 29 L23 12 L19 29 L8 29 Z"),
    "X": _shape("M0 0 L12 0 L18 8 L24 0 L36 0 L24 14 L37 29 L25 29 L18 20 L11 29 L0 29 L12 14 Z"),
    "Y": _shape("M0 0 L11 0 L17 10 L23 0 L34 0 L22 17 L22 29 L12 29 L12 17 Z"),
    "Z": _shape("M0 0 L29 0 L29 6 L13 22 L29 22 L29 29 L0 29 L0 23 L16 7 L0 7 Z"),
    "0": _shape("M12 0 C21 0 24 6 24 15 C24 24 20 29 12 29 C4 29 0 24 0 15 C0 5 4 0 12 0 Z",
                "M12 6 C9 6 9 10 9 15 C9 21 10 23 12 23 C15 23 15 20 15 15 C15 10 15 6 12 6 Z"),
    "1": _shape("M0 7 L10 0 L20 0 L20 29 L10 29 L10 10 L0 14 Z"),
    "2": _shape("M0 9 C0 3 6 0 14 0 C23 0 27 4 27 9 C27 15 23 18 12 23 L28 23 L28 29 L0 29 L0 23 C5 19 10 16 15 12 C18 10 18 8 17 7 C15 4 10 5 10 10 Z"),
    "3": _shape("M0 7 C1 2 6 0 13 0 C22 0 26 3 26 8 C26 11 24 13 21 14 C25 15 28 17 28 21 C28 26 23 29 14 29 C5 29 0 26 0 21 L10 20 C10 23 12 24 15 24 C18 24 19 22 19 20 C19 17 17 17 11 17 L11 12 C16 12 18 11 18 8 C18 6 17 5 14 5 C11 5 10 7 10 9 Z"),
    "4": _shape("M15 0 L26 0 L26 17 L30 17 L30 23 L26 23 L26 29 L16 29 L16 23 L0 23 L0 17 Z", "M8 17 L16 17 L16 7 Z"),
    "5": _shape("M3 0 L26 0 L26 6 L11 6 L10 10 C13 9 15 9 18 9 C25 9 28 13 28 19 C28 26 23 29 14 29 C6 29 1 26 0 21 L10 20 C10 23 12 24 15 24 C18 24 19 22 19 19 C19 16 18 15 15 15 C13 15 11 16 10 18 L1 16 Z"),
    "6": _shape("M27 7 L18 8 C18 5 16 5 14 5 C10 5 9 8 9 13 C12 10 14 10 17 10 C24 10 28 14 28 19 C28 26 23 29 14 29 C4 29 0 23 0 15 C0 5 5 0 14 0 C22 0 26 3 27 7 Z",
                "M14 16 C11 16 10 17 10 20 C10 23 12 24 14 24 C17 24 18 23 18 20 C18 17 17 16 14 16 Z"),
    "7": _shape("M0 0 L28 0 L28 6 C20 14 16 21 15 29 L4 29 C6 19 10 13 18 7 L0 7 Z"),
    "8": _shape("M14 0 C23 0 27 3 27 8 C27 11 25 13 22 14 C26 15 28 17 28 21 C28 27 23 29 14 29 C5 29 0 27 0 21 C0 17 2 15 6 14 C3 13 1 11 1 8 C1 3 5 0 14 0 Z",
                "M14 5 C11 5 10 6 10 8 C10 11 11 12 14 12 C17 12 18 11 18 8 C18 6 17 5 14 5 Z",
                "M14 17 C11 17 10 18 10 21 C10 23 11 24 14 24 C17 24 18 23 18 21 C18 18 17 17 14 17 Z"),
    "9": _shape("M0 22 L9 21 C9 24 11 24 13 24 C17 24 18 21 18 16 C15 19 13 19 10 19 C3 19 0 15 0 10 C0 3 5 0 14 0 C24 0 28 6 28 14 C28 24 23 29 14 29 C6 29 1 26 0 22 Z",
                "M14 5 C11 5 10 6 10 9 C10 12 11 13 14 13 C17 13 18 12 18 9 C18 6 17 5 14 5 Z"),
    "!": _shape("M0 0 L10 0 L9 20 L1 20 Z") + _shape("M0 23 L10 23 L10 29 L0 29 Z"),
    "?": _shape("M0 8 C0 2 5 0 13 0 C21 0 26 3 26 9 C26 13 24 15 19 18 C16 20 16 21 16 23 L7 23 C7 18 8 16 12 13 C15 11 16 10 16 8 C16 6 15 5 13 5 C10 5 10 6 10 9 Z") + _shape("M7 25 L17 25 L17 31 L7 31 Z"),
    ".": _shape("M0 0 L9 0 L9 8 L0 8 Z"),
    ",": _shape("M0 0 L9 0 L9 8 C9 12 7 15 1 16 L1 12 C4 11 5 10 5 8 L0 8 Z"),
    "-": _shape("M0 0 L14 0 L14 7 L0 7 Z"),
    "'": _shape("M0 0 L9 0 L9 8 C9 12 7 15 1 16 L1 12 C4 11 5 10 5 8 L0 8 Z"),
    # BIG640 uses Spanish inverted punctuation at the original '[' / ']' codes.
    "[": _shape("M0 0 L10 0 L10 6 L0 6 Z") + _shape("M1 9 L9 9 L10 29 L0 29 Z"),
    "]": _shape("M9 0 L19 0 L19 6 L9 6 Z") + _shape("M10 8 L19 8 C19 13 18 15 14 18 C11 20 10 21 10 23 C10 25 11 26 14 26 C17 26 18 25 18 22 L28 23 C28 29 23 32 14 32 C5 32 0 29 0 23 C0 19 2 17 7 14 C10 12 10 11 10 8 Z"),
    ":": _shape("M0 0 L9 0 L9 8 L0 8 Z") + _shape("M0 14 L9 14 L9 22 L0 22 Z"),
    # '#' is the original Spanish ordinal mark, a small o with an underline.
    "#": _shape("M9 0 C15 0 18 3 18 8 C18 13 15 16 9 16 C3 16 0 13 0 8 C0 3 3 0 9 0 Z",
                "M9 4 C7 4 6 5 6 8 C6 11 7 12 9 12 C11 12 12 11 12 8 C12 5 11 4 9 4 Z") + _shape("M0 20 L18 20 L18 24 L0 24 Z"),
}

_SPECIAL_BOUNDS = {"Q": (4, 3, 37, 35), "?": (5, 1, 30, 32), ".": (4, 24, 15, 32), ",": (3, 17, 16, 33),
                   "-": (4, 18, 19, 25), "'": (4, 2, 17, 18), "[": (5, 2, 23, 31), "]": (5, 2, 30, 33), ":": (4, 10, 20, 32), "#": (3, 2, 25, 26)}


def _path_points(description):
    tokens = re.findall(r"[MLCQZ]|-?\d+(?:\.\d+)?", description)
    result, index, current = [], 0, (0, 0)
    while index < len(tokens):
        command = tokens[index]
        index += 1
        if command == "Z":
            continue
        count = {"M": 2, "L": 2, "C": 6, "Q": 4}[command]
        values = list(map(float, tokens[index:index + count]))
        index += count
        if command in ("M", "L"):
            current = tuple(values)
            result.append(current)
        else:
            start = current
            for step in range(1, 25):
                t, u = step / 24, 1 - step / 24
                if command == "C":
                    point = tuple(u ** 3 * start[k] + 3 * u * u * t * values[k] + 3 * u * t * t * values[k + 2] + t ** 3 * values[k + 4] for k in (0, 1))
                else:
                    point = tuple(u * u * start[k] + 2 * u * t * values[k] + t * t * values[k + 2] for k in (0, 1))
                result.append(point)
            current = tuple(values[-2:])
    return result


def _caption_glyph_raster(character, scale, color):
    """Rasterize one authored outline before independent cell antialiasing."""
    _, _, width, height = CAPTION_GLYPHS[character]
    groups = [(_path_points(outer), [_path_points(hole) for hole in holes]) for outer, holes in _OUTLINES[character]]
    def slant(point):
        x, y = point
        return x + .32 * (29 - y), y
    points = [slant(point) for outer, _ in groups for point in outer]
    left, right = min(x for x, y in points), max(x for x, y in points)
    top, bottom = min(y for x, y in points), max(y for x, y in points)
    target = _SPECIAL_BOUNDS.get(character, (4, 3, width - 1, 32))
    def transform(point):
        x, y = slant(point)
        return ((target[0] + (x - left) * (target[2] - target[0]) / (right - left)) * scale,
                (target[1] + (y - top) * (target[3] - target[1]) / (bottom - top)) * scale)
    mask = Image.new("L", (width * scale, height * scale))
    for outer, holes in groups:
        part = Image.new("L", mask.size)
        draw = ImageDraw.Draw(part)
        draw.polygon([transform(point) for point in outer], fill=255)
        for hole in holes:
            draw.polygon([transform(point) for point in hole], fill=0)
        mask = ImageChops.lighter(mask, part)
    outline = mask.filter(ImageFilter.MaxFilter(2 * scale + 1))
    offset = Image.new("L", mask.size)
    offset.paste(outline, (-3 * scale, 2 * scale))
    outline = ImageChops.lighter(outline, offset)
    image = Image.new("RGBA", mask.size)
    image.paste((8, 8, 8, 255), (0, 0), outline)
    image.paste((*color[:3], 255), (0, 0), mask)
    return image


def _validate_raster(scale, antialias):
    if not isinstance(scale, int) or not 1 <= scale <= 16:
        raise ValueError("Caption raster scale must be an integer in 1..16")
    if not isinstance(antialias, int) or not 1 <= antialias <= 4 or scale * antialias > 32:
        raise ValueError("Caption antialias factor must be in 1..4 with total raster scale at most 32")


def caption_glyph(character, scale=8, color=(255, 255, 255), antialias=1):
    """Draw and antialias one complete glyph without reading neighboring cells.

    The one-output-texel transparent border protects linear texture filtering at
    shared row boundaries. Original face bounds are at least one logical pixel
    inside the cells, so this guard removes no face geometry.
    """
    _validate_raster(scale, antialias)
    _, _, width, height = CAPTION_GLYPHS[character]
    image = _caption_glyph_raster(character, scale * antialias, color)
    if antialias != 1:
        image = image.resize((width * scale, height * scale), Image.Resampling.LANCZOS)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, image.width - 1, 0), fill=(0, 0, 0, 0))
    draw.rectangle((0, image.height - 1, image.width - 1, image.height - 1), fill=(0, 0, 0, 0))
    draw.rectangle((0, 0, 0, image.height - 1), fill=(0, 0, 0, 0))
    draw.rectangle((image.width - 1, 0, image.width - 1, image.height - 1), fill=(0, 0, 0, 0))
    return image


def caption_atlas(scale=8, color=(255, 255, 255), antialias=1):
    """Pack glyphs already antialiased at the final target resolution.

    Use caption_atlas(4, antialias=2) for the shipped4x atlas. Resampling the
    packed atlas would mix Q's bottom shadow into W and the following rows.
    """
    _validate_raster(scale, antialias)
    atlas = Image.new("RGBA", tuple(dimension * scale for dimension in CAPTION_ATLAS_SIZE))
    for character, (x, y, _, _) in CAPTION_GLYPHS.items():
        atlas.alpha_composite(caption_glyph(character, scale, color, antialias), (x * scale, y * scale))
    return atlas
