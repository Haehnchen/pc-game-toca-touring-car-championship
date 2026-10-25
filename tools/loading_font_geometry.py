"""Independently authored smooth outlines for LOADHIGH member0.

The canonical 1.1a bitmap was inspected to recover its wide, upright geometric
face, original cells and ink extents. This module reads no original pixels or
installed font. HUD/Menu masters have different weight/slant/proportions; only
their generic path flattener is shared. Latin1 codes retain the original atlas
identity, including uppercase drawings stored at lowercase accented codes.
"""
from PIL import Image, ImageChops, ImageDraw
from hud_font_geometry import flatten_path, rounded_box

LOADING_ATLAS_SIZE = (256, 256)
LOADING_SCALE = 4
LOADING_LINE_HEIGHT = 11
LOADING_SPACE_WIDTH = 10
LOADING_ADVANCE = -1
LOADING_REFERENCE_CRC32 = "fad48def"
LOADING_FACE = (8, 82, 173)

LOADING_GLYPHS = {'A': (0, 0, 15, 11),
 'B': (16, 0, 13, 11),
 'C': (30, 0, 13, 11),
 'D': (44, 0, 13, 11),
 'E': (58, 0, 12, 11),
 'F': (71, 0, 11, 11),
 'G': (83, 0, 14, 11),
 'H': (98, 0, 13, 11),
 'I': (112, 0, 4, 11),
 'J': (117, 0, 12, 11),
 'K': (130, 0, 13, 11),
 'L': (144, 0, 10, 11),
 'M': (155, 0, 18, 11),
 'N': (174, 0, 14, 11),
 'O': (189, 0, 13, 11),
 'P': (203, 0, 12, 11),
 'Q': (216, 0, 14, 11),
 'R': (231, 0, 13, 11),
 'S': (0, 11, 14, 11),
 'T': (15, 11, 13, 11),
 'U': (29, 11, 14, 11),
 'V': (44, 11, 15, 11),
 'W': (60, 11, 21, 11),
 'X': (82, 11, 14, 11),
 'Y': (97, 11, 13, 11),
 'Z': (111, 11, 13, 11),
 'À': (125, 11, 15, 11),
 'Á': (141, 11, 15, 11),
 'Â': (157, 11, 15, 11),
 'Ã': (173, 11, 15, 11),
 'Å': (189, 11, 15, 11),
 'È': (205, 11, 12, 11),
 'É': (218, 11, 12, 11),
 'Ê': (231, 11, 12, 11),
 'Ë': (0, 22, 12, 11),
 'Æ': (13, 22, 18, 11),
 'ì': (32, 22, 3, 11),
 'í': (36, 22, 5, 11),
 'î': (42, 22, 5, 11),
 'ï': (48, 22, 5, 11),
 'ò': (54, 22, 13, 11),
 'ó': (68, 22, 14, 11),
 'ô': (83, 22, 14, 11),
 'ö': (98, 22, 14, 11),
 'õ': (113, 22, 14, 11),
 'Ø': (128, 22, 14, 11),
 'ù': (143, 22, 14, 11),
 'ú': (158, 22, 14, 11),
 'û': (173, 22, 14, 11),
 'ü': (188, 22, 13, 11),
 'ý': (202, 22, 13, 11),
 'ç': (216, 22, 13, 11),
 '0': (230, 22, 13, 11),
 '1': (244, 22, 7, 11),
 '2': (0, 33, 13, 11),
 '3': (14, 33, 12, 11),
 '4': (27, 33, 12, 11),
 '5': (40, 33, 13, 11),
 '6': (54, 33, 13, 11),
 '7': (68, 33, 13, 11),
 '8': (82, 33, 13, 11),
 '9': (96, 33, 12, 11),
 '?': (109, 33, 12, 11),
 '.': (122, 33, 4, 11),
 ',': (127, 33, 5, 11),
 '-': (133, 33, 7, 11),
 ':': (141, 33, 4, 11)}
LOADING_INK_BOUNDS = {'A': (0, 1, 15, 9),
 'B': (0, 1, 13, 9),
 'C': (0, 1, 13, 9),
 'D': (0, 1, 13, 9),
 'E': (0, 1, 12, 9),
 'F': (0, 1, 11, 9),
 'G': (0, 1, 14, 9),
 'H': (0, 1, 13, 9),
 'I': (0, 1, 4, 9),
 'J': (0, 1, 12, 9),
 'K': (0, 1, 13, 9),
 'L': (0, 1, 10, 9),
 'M': (0, 1, 18, 9),
 'N': (0, 1, 14, 9),
 'O': (0, 1, 13, 9),
 'P': (0, 1, 12, 9),
 'Q': (0, 1, 14, 9),
 'R': (0, 1, 13, 9),
 'S': (0, 1, 14, 9),
 'T': (0, 1, 13, 9),
 'U': (0, 1, 14, 9),
 'V': (0, 1, 15, 9),
 'W': (0, 1, 21, 9),
 'X': (0, 1, 14, 9),
 'Y': (0, 1, 13, 9),
 'Z': (0, 1, 13, 9),
 'À': (0, 1, 15, 9),
 'Á': (0, 0, 15, 9),
 'Â': (0, 0, 15, 9),
 'Ã': (0, 0, 15, 9),
 'Å': (0, 0, 15, 9),
 'È': (0, 1, 12, 9),
 'É': (0, 0, 12, 9),
 'Ê': (0, 0, 12, 9),
 'Ë': (0, 0, 12, 9),
 'Æ': (0, 1, 18, 9),
 'ì': (0, 1, 3, 9),
 'í': (0, 0, 5, 9),
 'î': (0, 0, 5, 9),
 'ï': (0, 0, 5, 9),
 'ò': (0, 1, 13, 9),
 'ó': (0, 0, 14, 9),
 'ô': (0, 0, 14, 9),
 'ö': (0, 0, 14, 9),
 'õ': (0, 0, 14, 9),
 'Ø': (0, 0, 14, 9),
 'ù': (0, 0, 14, 9),
 'ú': (0, 0, 14, 9),
 'û': (0, 0, 14, 9),
 'ü': (0, 0, 13, 9),
 'ý': (0, 0, 13, 9),
 'ç': (0, 1, 13, 10),
 '0': (0, 1, 13, 9),
 '1': (0, 1, 7, 9),
 '2': (0, 1, 13, 9),
 '3': (0, 1, 12, 9),
 '4': (0, 1, 12, 9),
 '5': (0, 1, 13, 9),
 '6': (0, 1, 13, 9),
 '7': (0, 1, 13, 9),
 '8': (0, 1, 13, 9),
 '9': (0, 1, 12, 9),
 '?': (0, 1, 12, 9),
 '.': (0, 6, 4, 9),
 ',': (0, 6, 5, 11),
 '-': (0, 4, 7, 6),
 ':': (0, 3, 4, 9)}



def _shapes(character):
    """Closed contours in a 100 by100 cap box, with broad open counters."""
    rect = lambda l, t, r, b: (f"M{l} {t} L{r} {t} L{r} {b} L{l} {b} Z", [])
    poly = lambda points: ("M" + " L".join(f"{x} {y}" for x, y in points) + " Z", [])
    # The source face has nearly square rounded bowls, 1.3px horizontal bars
    # and2px stems at its typical13 by7 cap size. These are new continuous
    # curves; the low-resolution source's stair steps are not reproduced.
    outer = rounded_box(0, 0, 100, 100, 20)
    hole = rounded_box(17, 19, 83, 81, 9)
    if character in "O0":
        return [(outer, [hole])]
    if character == "A":
        return [("M0 100 L39 0 L61 0 L100 100 L81 100 L71 75 L29 75 L19 100 Z", ["M36 56 L64 56 L50 20 Z"])]
    if character == "B":
        return [("M0 0 L78 0 C93 0 100 9 100 25 C100 39 94 45 86 49 C95 52 100 61 100 75 C100 92 92 100 77 100 L0 100 Z",
                 [rounded_box(17, 18, 82, 41, 7), rounded_box(17, 59, 83, 82, 7)])]
    if character == "C":
        return [("M100 28 L83 28 C83 21 78 19 69 19 L32 19 C22 19 17 24 17 35 L17 65 C17 76 22 81 32 81 L69 81 C78 81 83 78 83 70 L100 70 C99 90 89 100 72 100 L28 100 C9 100 0 88 0 67 L0 33 C0 11 10 0 29 0 L72 0 C89 0 99 9 100 28 Z", [])]
    if character == "D":
        return [("M0 0 L71 0 C91 0 100 12 100 34 L100 66 C100 89 90 100 71 100 L0 100 Z", ["M17 19 L67 19 C78 19 83 25 83 37 L83 63 C83 75 78 81 67 81 L17 81 Z"])]
    if character in "EF":
        return [rect(0, 0, 17, 100), rect(0, 0, 100, 19), rect(0, 41, 93, 60)] + ([rect(0, 81, 100, 100)] if character == "E" else [])
    if character == "G":
        return [("M100 28 L83 28 C83 21 78 19 69 19 L32 19 C22 19 17 24 17 35 L17 65 C17 76 22 81 32 81 L83 81 L83 61 L54 61 L54 43 L100 43 L100 100 L28 100 C9 100 0 88 0 67 L0 33 C0 11 10 0 29 0 L72 0 C89 0 99 9 100 28 Z", [])]
    if character == "H":
        return [rect(0, 0, 17, 100), rect(83, 0, 100, 100), rect(0, 41, 100, 60)]
    if character == "I":
        return [rect(18, 0, 82, 100)]
    if character == "J":
        return [("M83 0 L100 0 L100 66 C100 89 90 100 71 100 L29 100 C10 100 0 89 0 68 L17 68 C17 77 22 81 33 81 L67 81 C78 81 83 75 83 63 Z", [])]
    if character == "K":
        return [rect(0, 0, 17, 100), poly([(17, 43), (72, 0), (100, 0), (39, 49), (100, 100), (72, 100), (17, 57)])]
    if character == "L":
        return [rect(0, 0, 21, 100), rect(0, 81, 100, 100)]
    if character == "M":
        return [poly([(0, 100), (0, 0), (15, 0), (50, 65), (85, 0), (100, 0), (100, 100), (86, 100), (86, 27), (55, 87), (45, 87), (14, 27), (14, 100)])]
    if character == "N":
        return [poly([(0, 100), (0, 0), (17, 0), (83, 73), (83, 0), (100, 0), (100, 100), (83, 100), (17, 27), (17, 100)])]
    if character in "PR":
        shapes = [("M0 100 L0 0 L76 0 C92 0 100 10 100 29 C100 49 92 58 76 58 L17 58 L17 100 Z", [rounded_box(17, 18, 83, 40, 6)])]
        return shapes + ([poly([(57, 54), (78, 54), (100, 100), (79, 100)])] if character == "R" else [])
    if character == "Q":
        return [(outer, [hole]), poly([(55, 65), (72, 65), (100, 100), (80, 100)])]
    if character == "S":
        return [("M100 27 L83 27 C83 20 78 18 69 18 L31 18 C21 18 17 22 17 30 C17 38 21 41 31 41 L73 41 C92 41 100 50 100 70 C100 90 91 100 72 100 L28 100 C10 100 0 91 0 72 L17 72 C17 80 22 82 32 82 L68 82 C78 82 83 78 83 70 C83 62 79 59 69 59 L27 59 C9 59 0 50 0 30 C0 10 9 0 28 0 L72 0 C90 0 100 9 100 27 Z", [])]
    if character == "T":
        return [rect(0, 0, 100, 19), rect(42, 0, 58, 100)]
    if character == "U":
        return [("M0 0 L17 0 L17 63 C17 75 22 81 33 81 L67 81 C78 81 83 75 83 63 L83 0 L100 0 L100 66 C100 89 90 100 71 100 L29 100 C10 100 0 89 0 66 Z", [])]
    if character == "V":
        return [poly([(0, 0), (18, 0), (50, 79), (82, 0), (100, 0), (59, 100), (41, 100)])]
    if character == "W":
        return [poly([(0, 0), (12, 0), (28, 77), (43, 0), (57, 0), (72, 77), (88, 0), (100, 0), (79, 100), (65, 100), (50, 28), (35, 100), (21, 100)])]
    if character == "X":
        return [poly([(0, 0), (23, 0), (50, 35), (77, 0), (100, 0), (62, 50), (100, 100), (77, 100), (50, 65), (23, 100), (0, 100), (38, 50)])]
    if character == "Y":
        return [poly([(0, 0), (22, 0), (50, 39), (78, 0), (100, 0), (59, 58), (59, 100), (41, 100), (41, 58)])]
    if character == "Z":
        return [poly([(0, 0), (100, 0), (100, 20), (25, 81), (100, 81), (100, 100), (0, 100), (0, 80), (75, 19), (0, 19)])]
    if character == "1":
        return [poly([(0, 22), (43, 0), (80, 0), (80, 100), (48, 100), (48, 22), (0, 44)])]
    if character == "2":
        return [("M0 28 C0 9 10 0 29 0 L72 0 C91 0 100 10 100 28 C100 44 92 51 76 58 L25 81 L100 81 L100 100 L0 100 L0 81 C0 74 5 68 15 63 L70 39 C79 35 83 33 83 27 C83 21 78 19 68 19 L32 19 C22 19 17 21 17 28 Z", [])]
    if character == "3":
        return [("M0 0 L74 0 C91 0 100 9 100 26 C100 38 95 45 87 49 C96 52 100 60 100 73 C100 91 91 100 73 100 L0 100 L0 81 L68 81 C78 81 83 78 83 71 C83 64 78 60 68 60 L32 60 L32 41 L68 41 C78 41 83 37 83 30 C83 23 78 19 68 19 L0 19 Z", [])]
    if character == "4":
        return [("M65 0 L83 0 L83 60 L100 60 L100 79 L83 79 L83 100 L65 100 L65 79 L0 79 L0 59 Z", ["M65 22 L22 60 L65 60 Z"])]
    if character == "5":
        return [("M0 0 L100 0 L100 19 L17 19 L17 41 L72 41 C91 41 100 51 100 71 C100 91 91 100 72 100 L0 100 L0 81 L68 81 C78 81 83 78 83 71 C83 64 78 60 68 60 L0 60 Z", [])]
    if character == "6":
        return [("M100 19 L32 19 C22 19 17 24 17 41 L72 41 C91 41 100 51 100 71 C100 91 91 100 72 100 L28 100 C9 100 0 88 0 67 L0 33 C0 11 10 0 29 0 L100 0 Z", [rounded_box(17, 60, 83, 81, 6)])]
    if character == "7":
        return [poly([(0, 0), (100, 0), (100, 20), (29, 100), (5, 100), (76, 19), (0, 19)])]
    if character == "8":
        return [(outer, [rounded_box(17, 18, 83, 41, 7), rounded_box(17, 60, 83, 82, 7)])]
    if character == "9":
        return [("M0 81 L68 81 C78 81 83 76 83 59 L28 59 C9 59 0 49 0 29 C0 9 9 0 28 0 L72 0 C91 0 100 12 100 33 L100 67 C100 89 90 100 71 100 L0 100 Z", [rounded_box(17, 19, 83, 40, 6)])]
    if character == "?":
        return [("M0 25 C0 8 10 0 29 0 L72 0 C91 0 100 9 100 27 C100 43 91 51 73 57 L53 64 L53 73 L36 73 L36 63 C36 56 40 50 49 47 L69 40 C79 36 83 33 83 26 C83 21 78 19 68 19 L32 19 C22 19 17 21 17 25 Z", []), rect(36, 83, 53, 100)]
    raise KeyError(character)


_ACCENTS = {
    "À": ("A", "grave"), "Á": ("A", "acute"), "Â": ("A", "circumflex"), "Ã": ("A", "tilde"), "Å": ("A", "ring"),
    "È": ("E", "grave"), "É": ("E", "acute"), "Ê": ("E", "circumflex"), "Ë": ("E", "diaeresis"),
    "ì": ("I", "grave"), "í": ("I", "acute"), "î": ("I", "circumflex"), "ï": ("I", "diaeresis"),
    "ò": ("O", "grave"), "ó": ("O", "acute"), "ô": ("O", "circumflex"), "ö": ("O", "diaeresis"), "õ": ("O", "tilde"),
    "ù": ("U", "grave"), "ú": ("U", "acute"), "û": ("U", "circumflex"), "ü": ("U", "diaeresis"), "ý": ("Y", "acute"),
}


def _glyph(character, scale):
    _, _, width, height = LOADING_GLYPHS[character]
    mask = Image.new("L", (width * scale, height * scale))
    draw = ImageDraw.Draw(mask)
    def polygon(points, value=255):
        draw.polygon([(x * scale, y * scale) for x, y in points], fill=value)
    def rectangle(left, top, right, bottom):
        polygon([(left, top), (right, top), (right, bottom), (left, bottom)])
    if character in ".,-:":
        if character == "-":
            rectangle(.2, 4.15, width - .2, 5.75)
        else:
            rectangle(.35, 6.25, width - .35, 8.5)
            if character == ":":
                rectangle(.35, 3.15, width - .35, 4.85)
            if character == ",":
                polygon([(width - .35, 7.8), (width - .35, 9), (1.15, 10.7), (.35, 10.7), (2.1, 8.3)])
        return mask
    base, accent = _ACCENTS.get(character, (character, None))
    if character in ("Ø", "ç", "Æ"):
        base = {"Ø": "O", "ç": "C", "Æ": "A"}[character]
    # Preserve the original accent/code inventory and source ink extents; the
    # base cap remains low and broad, with accents tucked into its top margin.
    top, bottom = (1.6 if accent else 1.2), 8.3
    left, right = .2, width - .2
    if character == "Æ":
        right = width * .62
    for outer, holes in _shapes(base):
        part = Image.new("L", mask.size)
        part_draw = ImageDraw.Draw(part)
        def mapped(path):
            return [((left + x / 100 * (right - left)) * scale, (top + y / 100 * (bottom - top)) * scale) for x, y in flatten_path(path)]
        part_draw.polygon(mapped(outer), fill=255)
        for hole in holes:
            part_draw.polygon(mapped(hole), fill=0)
        mask = ImageChops.lighter(mask, part)
    draw = ImageDraw.Draw(mask)
    if character == "Æ":
        x = width * .49
        rectangle(x, top, x + 1.8, bottom)
        for y in (top, 4.05, 6.95):
            rectangle(x, y, width - .2, y + 1.35)
    if character == "Ø":
        polygon([(1, 8.3), (3.2, 8.3), (width - 1, .2), (width - 3.2, .2)])
    if character == "ç":
        polygon([(width * .56, 8), (width * .56, 9), (width * .38, 9.7), (width * .25, 9.7), (width * .25, 8.9), (width * .4, 8.6), (width * .4, 8)])
    if accent:
        middle = width / 2
        accent_top = max(.05, LOADING_INK_BOUNDS[character][1] + .05)
        accent_bottom = 1.45
        if accent in ("grave", "acute"):
            dx = -1 if accent == "grave" else 1
            polygon([(middle - dx * 1.6, accent_top), (middle - dx * .3, accent_top), (middle + dx * 1.5, accent_bottom), (middle + dx * .2, accent_bottom)])
        elif accent == "circumflex":
            polygon([(middle - 2, accent_bottom), (middle - .6, accent_top), (middle + .6, accent_top), (middle + 2, accent_bottom), (middle + .7, accent_bottom), (middle, accent_top + .4), (middle - .7, accent_bottom)])
        elif accent == "diaeresis":
            rectangle(middle - 2.2, accent_top, middle - .7, accent_bottom)
            rectangle(middle + .7, accent_top, middle + 2.2, accent_bottom)
        elif accent == "tilde":
            polygon([(middle - 2.5, 1.1), (middle - 1.4, accent_top), (middle + .9, .65), (middle + 2.5, accent_top), (middle + 2.5, .9), (middle + 1.4, 1.45), (middle - .9, .8), (middle - 2.5, 1.45)])
        elif accent == "ring":
            draw.ellipse(((middle - 1.25) * scale, accent_top * scale, (middle + 1.25) * scale, 1.6 * scale), fill=255)
            draw.ellipse(((middle - .5) * scale, .5 * scale, (middle + .5) * scale, 1.1 * scale), fill=0)
    return mask


def loading_font_atlas(scale=LOADING_SCALE, antialias=4):
    """Rasterize fresh curves and antialias each unchanged cell independently."""
    if type(scale) is not int or scale != LOADING_SCALE:
        raise ValueError("LOADHIGH replacement must use fourfold original cells")
    if type(antialias) is not int or antialias < 1:
        raise ValueError("Antialias factor must be a positive integer")
    atlas = Image.new("RGBA", tuple(value * scale for value in LOADING_ATLAS_SIZE), (*LOADING_FACE, 0))
    for character, (x, y, width, height) in LOADING_GLYPHS.items():
        mask = _glyph(character, scale * antialias).resize((width * scale, height * scale), Image.Resampling.LANCZOS)
        # Keep unused source rows and adjacent original cells transparent even
        # when the reconstruction's antialias filter has a ringing lobe.
        bounds = tuple(value * scale for value in LOADING_INK_BOUNDS[character])
        clipped = Image.new("L", mask.size)
        clipped.paste(mask.crop(bounds), bounds[:2])
        glyph = Image.new("RGBA", mask.size, (*LOADING_FACE, 0))
        glyph.putalpha(clipped)
        atlas.paste(glyph, (x * scale, y * scale))
    return atlas
