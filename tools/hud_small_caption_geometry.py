"""Programmatic upright reconstruction of IMAGE640's LIL640 font.

The original cell records and metrics are preserved. Independently authored
contours, including the original extended Latin and punctuation inventory, are
rasterized without bitmap inputs or a machine-installed typeface. White faces
and RGB8 outlines retain the original tintable menu-font color model.
"""

import re

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SMALL_CAPTION_ATLAS_SIZE = (256, 256)
SMALL_CAPTION_SIZE = SMALL_CAPTION_ATLAS_SIZE
SMALL_CAPTION_LINE_HEIGHT = 28
SMALL_CAPTION_SPACE_WIDTH = 10
SMALL_CAPTION_ADVANCE = 0
SMALL_CAPTION_BASELINE = 21
SMALL_CAPTION_REFERENCE_CRC32 = "29d0f179"


def _shape(outer, *holes):
    return [(outer, holes)]


# Native upright cap height is 13 units. Thick stems, small counters and
# broad rounded bowls distinguish LIL640 from the italic BIG640 title font.
_O = "M6.5 0 C10.8 0 13 1.8 13 6.5 C13 11.2 10.8 13 6.5 13 C2.2 13 0 11.2 0 6.5 C0 1.8 2.2 0 6.5 0 Z"
_O_HOLE = "M6.5 3 C5.1 3 5 4.5 5 6.5 C5 8.5 5.1 10 6.5 10 C7.9 10 8 8.5 8 6.5 C8 4.5 7.9 3 6.5 3 Z"
SMALL_CAPTION_OUTLINES = {
    "A": _shape("M5 0 L11 0 L16 13 L11 13 L10 10 L6 10 L5 13 L0 13 Z", "M7 7 L9 7 L8 3.3 Z"),
    "B": _shape("M0 0 L8.5 0 C11 0 12 1.2 12 3.4 C12 4.8 11.5 5.6 10.3 6 C12 6.5 12 7.7 12 9.4 C12 11.7 10.6 13 8 13 L0 13 Z", "M5 3 L6.6 3 C7.4 3 7.6 3.5 7.6 4 C7.6 4.7 7.4 5 6.6 5 L5 5 Z", "M5 8 L7 8 C7.9 8 8 8.4 8 9 C8 9.7 7.7 10 7 10 L5 10 Z"),
    "C": _shape("M12 4.8 L7.3 5 C7.3 3.2 7 3 6 3 C5 3 5 4.4 5 6.5 C5 8.6 5 10 6 10 C7 10 7.3 9.5 7.4 7.6 L12 8 C11.8 11.5 9.8 13 6 13 C2 13 0 10.9 0 6.5 C0 2.2 2.3 0 6 0 C9.7 0 11.7 1.3 12 4 Z"),
    "D": _shape("M0 0 L6 0 C10 0 12 2.1 12 6.5 C12 10.9 10 13 6 13 L0 13 Z", "M5 3 L6 3 C7.1 3 7.2 4.3 7.2 6.5 C7.2 8.7 7.1 10 6 10 L5 10 Z"),
    "E": _shape("M0 0 L11 0 L11 3 L5 3 L5 5 L10 5 L10 8 L5 8 L5 10 L11 10 L11 13 L0 13 Z"),
    "F": _shape("M0 0 L10 0 L10 3 L5 3 L5 5 L9 5 L9 8 L5 8 L5 13 L0 13 Z"),
    "G": _shape("M13 4 L8 4 C7.9 3.2 7.3 3 6.5 3 C5.1 3 5 4.3 5 6.5 C5 8.7 5.2 10 6.6 10 C7.4 10 8 9.8 8 9 L6 9 L6 5 L13 5 L13 11 C11.5 12.5 9 13 6.5 13 C2.5 13 0 10.7 0 6.5 C0 2.3 2.5 0 6.5 0 C10.5 0 12.5 1.4 13 4 Z"),
    "H": _shape("M0 0 L5 0 L5 5 L8 5 L8 0 L13 0 L13 13 L8 13 L8 8 L5 8 L5 13 L0 13 Z"),
    "I": _shape("M0 0 L5 0 L5 13 L0 13 Z"),
    "J": _shape("M7 0 L12 0 L12 8.6 C12 12 10.8 13 6 13 C1.2 13 0 11.8 0 8.5 L4.7 8.3 C4.7 9.6 5 10 6 10 C6.8 10 7 9.7 7 8.5 Z"),
    "K": _shape("M0 0 L5 0 L5 4.6 L9.2 0 L16 0 L9.4 6 L16 13 L9.5 13 L5.8 8.8 L5 9.5 L5 13 L0 13 Z"),
    "L": _shape("M0 0 L5 0 L5 10 L10 10 L10 13 L0 13 Z"),
    "M": _shape("M0 0 L6 0 L7.5 5.2 L9 0 L15 0 L15 13 L11 13 L11 5.2 L9.1 13 L5.9 13 L4 5.2 L4 13 L0 13 Z"),
    "N": _shape("M0 0 L4 0 L9 7.2 L9 0 L13 0 L13 13 L9 13 L4 5.9 L4 13 L0 13 Z"),
    "O": _shape(_O, _O_HOLE),
    "P": _shape("M0 0 L7 0 C10 0 11 1.5 11 4.5 C11 7.5 9.5 9 6.5 9 L5 9 L5 13 L0 13 Z", "M5 3 L6 3 C6.8 3 7 3.5 7 4.5 C7 5.5 6.8 6 6 6 L5 6 Z"),
    "Q": _shape(_O, _O_HOLE) + _shape("M6 8 L10 8 L13 12.5 L9.5 14 L6 10 Z"),
    "R": _shape("M0 0 L8 0 C11.2 0 13 1.5 13 4 C13 6 12 7.2 10.5 7.5 L13 13 L8 13 L5.8 8.3 L5 8.3 L5 13 L0 13 Z", "M5 3 L6.7 3 C7.7 3 8 3.4 8 4.2 C8 5 7.7 5.3 6.7 5.3 L5 5.3 Z"),
    "S": _shape("M11 3.7 L6.5 4 C6.5 3 6 2.6 5.3 2.6 C4.5 2.6 4.3 3 4.3 3.5 C4.3 4.2 5 4.3 6.8 4.7 C10 5.4 11 6.6 11 9 C11 11.8 9 13 5.6 13 C2 13 0 11.5 0 9 L4.4 8.8 C4.4 9.8 5 10.3 5.8 10.3 C6.7 10.3 7 9.9 7 9.3 C7 8.6 6.4 8.5 4.5 8 C1.5 7.3 0 6.3 0 3.8 C0 1.2 2 0 5.5 0 C9 0 10.7 1.3 11 3.7 Z"),
    "T": _shape("M0 0 L13 0 L13 3 L9 3 L9 13 L4 13 L4 3 L0 3 Z"),
    "U": _shape("M0 0 L5 0 L5 9 C5 9.8 5.5 10 6.5 10 C7.5 10 8 9.8 8 9 L8 0 L13 0 L13 9 C13 11.8 11 13 6.5 13 C2 13 0 11.8 0 9 Z"),
    "V": _shape("M0 0 L5 0 L7 8.5 L9 0 L14 0 L10 13 L4 13 Z"),
    "W": _shape("M0 0 L5 0 L6.7 8.5 L8 0 L12 0 L13.3 8.5 L15 0 L20 0 L17 13 L12 13 L10 9 L8 13 L3 13 Z"),
    "X": _shape("M0 0 L5.5 0 L7 3.7 L8.5 0 L14 0 L10 6.3 L14 13 L8.6 13 L7 9 L5.4 13 L0 13 L4 6.3 Z"),
    "Y": _shape("M0 0 L5.5 0 L7.5 4.8 L9.5 0 L15 0 L10 7.7 L10 13 L5 13 L5 7.7 Z"),
    "Z": _shape("M1 0 L11 0 L11 3 L5 10 L11 10 L11 13 L0 13 L0 10 L6 3 L1 3 Z"),
    "0": _shape("M4.5 0 C8 0 9 2.3 9 6.5 C9 10.7 8 13 4.5 13 C1 13 0 10.7 0 6.5 C0 2.3 1 0 4.5 0 Z", "M4.5 3 C4 3 4 4.5 4 6.5 C4 8.5 4 10 4.5 10 C5 10 5 8.5 5 6.5 C5 4.5 5 3 4.5 3 Z"),
    "1": _shape("M0 3.5 L4 0 L8 0 L8 13 L4 13 L4 4.5 L0 6.7 Z"),
    "2": _shape("M0 4 C0 1.3 1.5 0 4.5 0 C7.5 0 9 1.5 9 4 C9 6.3 7 7.5 4.1 10 L9 10 L9 13 L0 13 L0 9.5 C1.7 8 3.8 6.1 4.8 5 C6.4 3.3 5.3 2.5 4.5 2.5 C3.8 2.5 3.6 3.2 3.6 4 Z"),
    "3": _shape("M0 3.8 C0 1.3 1.6 0 4.5 0 C7.5 0 9 1.3 9 3.7 C9 5.1 8.3 5.9 7.2 6.3 C8.5 6.7 9 7.6 9 9.1 C9 11.8 7.3 13 4.5 13 C1.6 13 0 11.5 0 9 L3.7 8.8 C3.7 10 4 10.4 4.7 10.4 C5.4 10.4 5.6 9.9 5.6 9.1 C5.6 8.1 5.2 7.8 3.5 7.8 L3.5 5 C5 5 5.4 4.6 5.4 3.8 C5.4 3 5.2 2.6 4.5 2.6 C3.8 2.6 3.6 3 3.6 3.8 Z"),
    "4": _shape("M6 0 L11 0 L11 7 L12 7 L12 10 L11 10 L11 13 L7 13 L7 10 L0 10 L0 7 Z", "M4.1 7 L7 7 L7 3.3 Z"),
    "5": _shape("M.5 0 L9 0 L9 3 L4 3 L3.7 4.8 C4.5 4.3 5.1 4.2 5.7 4.2 C8 4.2 9 5.7 9 8.5 C9 11.5 7.4 13 4.5 13 C1.7 13 .1 11.6 0 9 L3.6 8.8 C3.6 10 3.9 10.3 4.6 10.3 C5.3 10.3 5.5 9.7 5.5 8.5 C5.5 7.3 5.2 6.8 4.5 6.8 C3.8 6.8 3.5 7.1 3.4 7.7 L0 7.3 Z"),
    "6": _shape("M9 3.5 L5.4 3.8 C5.4 3 5.1 2.6 4.5 2.6 C3.7 2.6 3.5 3.5 3.5 5.5 C4.1 4.9 4.9 4.7 5.7 4.7 C8 4.7 9 6.1 9 8.8 C9 11.5 7.5 13 4.5 13 C1.2 13 0 10.6 0 6.5 C0 2.1 1.5 0 4.5 0 C7.4 0 8.8 1.3 9 3.5 Z", "M4.5 7.4 C3.8 7.4 3.6 7.9 3.6 8.9 C3.6 10 3.8 10.4 4.5 10.4 C5.2 10.4 5.4 10 5.4 8.9 C5.4 7.9 5.2 7.4 4.5 7.4 Z"),
    "7": _shape("M0 0 L9 0 L9 3 C6 6.1 5 8.7 4.5 13 L.7 13 C1.1 8.6 2.5 5.5 5 3 L0 3 Z"),
    "8": _shape("M4.5 0 C7.5 0 9 1.3 9 3.6 C9 4.9 8.4 5.7 7.4 6.1 C8.5 6.5 9 7.5 9 9 C9 11.7 7.5 13 4.5 13 C1.5 13 0 11.7 0 9 C0 7.5 .5 6.5 1.6 6.1 C.6 5.7 0 4.9 0 3.6 C0 1.3 1.5 0 4.5 0 Z", "M4.5 2.5 C3.8 2.5 3.7 3 3.7 3.8 C3.7 4.6 3.8 5 4.5 5 C5.2 5 5.3 4.6 5.3 3.8 C5.3 3 5.2 2.5 4.5 2.5 Z", "M4.5 7.6 C3.8 7.6 3.6 8.1 3.6 9 C3.6 10 3.8 10.5 4.5 10.5 C5.2 10.5 5.4 10 5.4 9 C5.4 8.1 5.2 7.6 4.5 7.6 Z"),
    "9": _shape("M0 9.5 L3.6 9.2 C3.6 10 3.9 10.4 4.5 10.4 C5.3 10.4 5.5 9.5 5.5 7.5 C4.9 8.1 4.1 8.3 3.3 8.3 C1 8.3 0 6.9 0 4.2 C0 1.5 1.5 0 4.5 0 C7.8 0 9 2.4 9 6.5 C9 10.9 7.5 13 4.5 13 C1.6 13 .2 11.7 0 9.5 Z", "M4.5 2.6 C3.8 2.6 3.6 3 3.6 4.1 C3.6 5.1 3.8 5.6 4.5 5.6 C5.2 5.6 5.4 5.1 5.4 4.1 C5.4 3 5.2 2.6 4.5 2.6 Z"),
    "!": _shape("M0 0 L5 0 L4.5 8 L.5 8 Z") + _shape("M0 9 L5 9 L5 13 L0 13 Z"),
    "?": _shape("M0 4 C0 1.3 2 0 5.5 0 C9 0 11 1.3 11 4 C11 5.8 10 6.7 8 7.8 C7 8.3 7 8.6 7 9 L3 9 C3 7.4 3.5 6.5 5.1 5.5 C6.5 4.6 6.5 4.4 6.5 3.8 C6.5 3.1 6.2 2.8 5.5 2.8 C4.8 2.8 4.5 3.2 4.5 4 Z") + _shape("M3 9.5 L7.5 9.5 L7.5 13 L3 13 Z"),
    "[": _shape("M0 0 L5 0 L5 4 L0 4 Z") + _shape("M.5 5 L4.5 5 L5 13 L0 13 Z"),
    "]": _shape("M3.5 0 L8 0 L8 3.5 L3.5 3.5 Z") + _shape("M4 4 L8 4 C8 5.6 7.5 6.5 5.9 7.5 C4.5 8.4 4.5 8.6 4.5 9.2 C4.5 9.9 4.8 10.2 5.5 10.2 C6.2 10.2 6.5 9.8 6.5 9 L11 9 C11 11.7 9 13 5.5 13 C2 13 0 11.7 0 9 C0 7.2 1 6.3 3 5.2 C4 4.7 4 4.4 4 4 Z"),
    ".": _shape("M0 0 L5 0 L5 4 L0 4 Z"),
    ",": _shape("M0 0 L5 0 L5 4 C5 6 3 7.5 0 8 L0 6 C1.7 5.5 2.2 5 2.3 4 L0 4 Z"),
    "'": _shape("M0 0 L5 0 L5 3 C5 4 3 5 0 5 L0 4 C1.3 3.8 1.7 3.5 1.7 3 L0 3 Z"),
    "-": _shape("M0 0 L10 0 L10 3 L0 3 Z"),
    "+": _shape("M3.5 0 L6.5 0 L6.5 3 L10 3 L10 6 L6.5 6 L6.5 9 L3.5 9 L3.5 6 L0 6 L0 3 L3.5 3 Z"),
    ":": _shape("M0 0 L5 0 L5 4 L0 4 Z") + _shape("M0 6 L5 6 L5 10 L0 10 Z"),
    "#": _shape("M4.5 0 C7.6 0 9 1.3 9 4 C9 6.7 7.6 8 4.5 8 C1.4 8 0 6.7 0 4 C0 1.3 1.4 0 4.5 0 Z", "M4.5 2.8 C4 2.8 4 3.3 4 4 C4 4.7 4 5.2 4.5 5.2 C5 5.2 5 4.7 5 4 C5 3.3 5 2.8 4.5 2.8 Z") + _shape("M0 9 L9 9 L9 12 L0 12 Z"),
    "Æ": _shape("M5 0 L17 0 L17 3 L12 3 L12 5 L16 5 L16 8 L12 8 L12 10 L17 10 L17 13 L8 13 L8 10 L5 10 L4 13 L0 13 Z", "M6 7 L8 7 L8 3 Z"),
    "ß": _shape("M0 15 L0 4 C0 1.1 2 0 5.5 0 C9 0 11 1.2 11 3.7 C11 5.1 10.5 5.8 9 6.3 C10.7 6.7 11 8.3 11 10.5 C11 13.2 9.2 14 6 14 L5 14 L5 11 L6 11 C6.8 11 7 10.5 7 9.9 C7 8.4 6.5 8 5 8 L5 5 C6.5 5 6.8 4.5 6.8 3.9 C6.8 3.1 6.5 2.7 5.5 2.7 C4.6 2.7 4.5 3.2 4.5 4 L4.5 15 Z"),
}

# Original accents keep the upright base on y8..21 rather than stretching the
# cap height to fill the taller accented cell.
_ACCENTS = {
    "À": ("A", "M6 3 L10 3 L14 5 L10 5 Z"),
    "Á": ("A", "M12 3 L16 3 L12 5 L8 5 Z"),
    "Å": ("A", "M11 1 C12.4 1 13 1.8 13 3 C13 4.2 12.4 5 11 5 C9.6 5 9 4.2 9 3 C9 1.8 9.6 1 11 1 Z", "M11 2 C10.4 2 10.1 2.3 10.1 3 C10.1 3.7 10.4 4 11 4 C11.6 4 11.9 3.7 11.9 3 C11.9 2.3 11.6 2 11 2 Z"),
    "Ã": ("A", "M7 4 C8 2.8 9 2.5 10.6 3.2 C12.2 4 12.8 4.1 14 3 L15 4 C13.8 5.2 12.5 5.4 11 4.6 C9.4 3.8 9 3.8 8 5 Z"),
    "Ä": ("A", "M7 3 L9 3 L9 5 L7 5 Z", "M13 3 L15 3 L15 5 L13 5 Z"),
    "Â": ("A", "M10.5 2 L13.5 5 L12 5 L10.5 3.5 L9 5 L7.5 5 Z"),
    "È": ("E", "M4 3 L8 3 L12 5 L8 5 Z"),
    "É": ("E", "M9 3 L13 3 L9 5 L5 5 Z"),
    "Ê": ("E", "M8 2 L11 5 L9.5 5 L8 3.5 L6.5 5 L5 5 Z"),
    "Ë": ("E", "M5 3 L7 3 L7 5 L5 5 Z", "M11 3 L13 3 L13 5 L11 5 Z"),
}

SMALL_CAPTION_GLYPHS = {
    'A': (0, 0, 20, 28),
    'B': (21, 0, 16, 28),
    'C': (38, 0, 16, 28),
    'D': (55, 0, 16, 28),
    'E': (72, 0, 15, 28),
    'F': (88, 0, 14, 28),
    'G': (103, 0, 17, 28),
    'H': (121, 0, 17, 28),
    'I': (139, 0, 9, 28),
    'J': (149, 0, 15, 28),
    'K': (165, 0, 19, 28),
    'L': (185, 0, 14, 28),
    'M': (200, 0, 19, 28),
    'N': (220, 0, 17, 28),
    'O': (0, 28, 17, 28),
    'P': (18, 28, 15, 28),
    'Q': (34, 28, 17, 28),
    'R': (52, 28, 17, 28),
    'S': (70, 28, 15, 28),
    'T': (86, 28, 17, 28),
    'U': (104, 28, 17, 28),
    'V': (122, 28, 18, 28),
    'W': (141, 28, 24, 28),
    'X': (166, 28, 18, 28),
    'Y': (185, 28, 19, 28),
    'Z': (205, 28, 15, 28),
    '[': (221, 28, 9, 28),
    ']': (231, 28, 15, 28),
    'À': (0, 56, 20, 28),
    'Á': (21, 56, 20, 28),
    'Å': (42, 56, 20, 28),
    'Ã': (63, 56, 20, 28),
    'Ä': (84, 56, 20, 28),
    'Â': (105, 56, 20, 28),
    'Æ': (126, 56, 21, 28),
    'Ç': (148, 56, 16, 28),
    'È': (165, 56, 15, 28),
    'É': (181, 56, 15, 28),
    'Ê': (197, 56, 15, 28),
    'Ë': (213, 56, 15, 28),
    'Ø': (229, 56, 17, 28),
    'ß': (0, 84, 15, 28),
    '#': (16, 84, 13, 28),
    '!': (30, 84, 9, 28),
    "'": (40, 84, 9, 28),
    '+': (50, 84, 14, 28),
    ',': (65, 84, 9, 28),
    '-': (75, 84, 14, 28),
    '.': (90, 84, 9, 28),
    '0': (100, 84, 13, 28),
    '1': (114, 84, 12, 28),
    '2': (127, 84, 13, 28),
    '3': (141, 84, 13, 28),
    '4': (155, 84, 16, 28),
    '5': (172, 84, 13, 28),
    '6': (186, 84, 13, 28),
    '7': (200, 84, 13, 28),
    '8': (214, 84, 13, 28),
    '9': (228, 84, 13, 28),
    ':': (242, 84, 9, 28),
    '?': (0, 112, 15, 28),
}

SMALL_CAPTION_SPACING = {
    'A': (4, 2, 1, 3, 2, 0),
    'B': (0, 0, 0, 1, 0, 0),
    'C': (1, 0, 0, 0, 0, 1),
    'D': (0, 0, 0, 0, 0, 1),
    'E': (0, 0, 0, 0, 1, 0),
    'F': (0, 0, 0, 0, 1, 5),
    'G': (1, 0, 0, 0, 0, 0),
    'H': (0, 0, 0, 0, 0, 0),
    'I': (0, 0, 0, 0, 0, 0),
    'J': (6, 2, 0, 0, 0, 0),
    'K': (0, 0, 0, 2, 3, 0),
    'L': (0, 0, 0, 5, 5, 0),
    'M': (0, 0, 0, 0, 0, 0),
    'N': (0, 0, 0, 0, 0, 0),
    'O': (1, 0, 0, 0, 0, 1),
    'P': (0, 0, 0, 0, 0, 4),
    'Q': (1, 0, 0, 0, 0, 0),
    'R': (0, 0, 0, 1, 2, 0),
    'S': (0, 0, 0, 0, 0, 0),
    'T': (0, 4, 4, 0, 4, 4),
    'U': (0, 0, 0, 0, 0, 1),
    'V': (0, 2, 3, 1, 2, 4),
    'W': (0, 1, 2, 1, 1, 2),
    'X': (0, 3, 1, 2, 3, 0),
    'Y': (0, 3, 5, 2, 4, 5),
    'Z': (1, 3, 0, 0, 3, 0),
    '[': (0, 0, 0, 0, 0, 0),
    ']': (3, 2, 0, 3, 0, 1),
    'À': (4, 2, 1, 3, 2, 0),
    'Á': (4, 2, 1, 3, 2, 0),
    'Å': (4, 2, 1, 3, 2, 0),
    'Ã': (4, 2, 1, 3, 2, 0),
    'Ä': (4, 2, 1, 3, 2, 0),
    'Â': (4, 2, 1, 3, 2, 0),
    'Æ': (4, 2, 1, 0, 1, 0),
    'Ç': (1, 0, 0, 0, 0, 1),
    'È': (0, 0, 0, 0, 1, 0),
    'É': (0, 0, 0, 0, 1, 0),
    'Ê': (0, 0, 0, 0, 1, 0),
    'Ë': (0, 0, 0, 0, 1, 0),
    'Ø': (1, 0, 0, 0, 0, 1),
    'ß': (0, 0, 0, 1, 0, 0),
    '#': (0, 0, 0, 0, 2, 0),
    '!': (0, 0, 0, 0, 0, 0),
    "'": (0, 1, 9, 0, 3, 9),
    '+': (3, 0, 3, 3, 0, 3),
    ',': (9, 9, 0, 9, 9, 0),
    '-': (14, 2, 0, 14, 0, 2),
    '.': (9, 9, 0, 9, 9, 0),
    '0': (1, 0, 0, 0, 0, 1),
    '1': (2, 0, 4, 0, 0, 0),
    '2': (0, 4, 1, 0, 1, 0),
    '3': (0, 3, 0, 1, 0, 0),
    '4': (4, 1, 0, 2, 0, 0),
    '5': (1, 0, 0, 1, 0, 0),
    '6': (1, 0, 0, 0, 0, 0),
    '7': (0, 4, 2, 0, 2, 3),
    '8': (0, 0, 0, 0, 0, 0),
    '9': (0, 0, 0, 0, 0, 1),
    ':': (2, 0, 0, 0, 0, 0),
    '?': (1, 0, 3, 0, 2, 3),
}

_FACE_BOUNDS = {
    'A': (3, 8, 19, 21),
    'B': (3, 8, 15, 21),
    'C': (3, 8, 15, 21),
    'D': (3, 8, 15, 21),
    'E': (3, 8, 14, 21),
    'F': (3, 8, 13, 21),
    'G': (3, 8, 16, 21),
    'H': (3, 8, 16, 21),
    'I': (3, 8, 8, 21),
    'J': (3, 8, 14, 21),
    'K': (3, 8, 18, 21),
    'L': (3, 8, 13, 21),
    'M': (3, 8, 18, 21),
    'N': (3, 8, 16, 21),
    'O': (3, 8, 16, 21),
    'P': (3, 8, 14, 21),
    'Q': (3, 8, 16, 22),
    'R': (3, 8, 16, 21),
    'S': (3, 8, 14, 21),
    'T': (3, 8, 16, 21),
    'U': (3, 8, 16, 21),
    'V': (3, 8, 17, 21),
    'W': (3, 8, 23, 21),
    'X': (3, 8, 17, 21),
    'Y': (3, 8, 18, 21),
    'Z': (3, 8, 14, 21),
    '[': (3, 8, 8, 21),
    ']': (3, 8, 14, 21),
    'À': (3, 3, 19, 21),
    'Á': (3, 3, 19, 21),
    'Å': (3, 1, 19, 21),
    'Ã': (3, 3, 19, 21),
    'Ä': (3, 3, 19, 21),
    'Â': (3, 2, 19, 21),
    'Æ': (3, 8, 20, 21),
    'Ç': (3, 8, 15, 24),
    'È': (3, 3, 14, 21),
    'É': (3, 3, 14, 21),
    'Ê': (3, 2, 14, 21),
    'Ë': (3, 3, 14, 21),
    'Ø': (3, 8, 16, 21),
    'ß': (3, 8, 14, 23),
    '#': (3, 8, 12, 20),
    '!': (3, 8, 8, 21),
    "'": (3, 8, 8, 13),
    '+': (3, 10, 13, 19),
    ',': (3, 17, 8, 25),
    '-': (3, 15, 13, 18),
    '.': (3, 17, 8, 21),
    '0': (3, 8, 12, 21),
    '1': (3, 8, 11, 21),
    '2': (3, 8, 12, 21),
    '3': (3, 8, 12, 21),
    '4': (3, 8, 15, 21),
    '5': (3, 8, 12, 21),
    '6': (3, 8, 12, 21),
    '7': (3, 8, 12, 21),
    '8': (3, 8, 12, 21),
    '9': (3, 8, 12, 21),
    ':': (3, 11, 8, 21),
    '?': (3, 8, 14, 21),
}


def _path_points(description):
    """Sample authored straight, quadratic and cubic contour commands."""
    tokens = re.findall(r"[MLCQZ]|-?(?:\d+(?:\.\d+)?|\.\d+)", description)
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
            for step in range(1, 33):
                t, u = step / 32, 1 - step / 32
                if command == "C":
                    point = tuple(u ** 3 * start[k] + 3 * u * u * t * values[k] + 3 * u * t * t * values[k + 2] + t ** 3 * values[k + 4] for k in (0, 1))
                else:
                    point = tuple(u * u * start[k] + 2 * u * t * values[k] + t * t * values[k + 2] for k in (0, 1))
                result.append(point)
            current = tuple(values[-2:])
    return result


def _fitted_groups(character):
    """Return native cell-space contours; no italic or oblique transform."""
    base = _ACCENTS.get(character, (character,))[0]
    if character == "Ç":
        base = "C"
    elif character == "Ø":
        base = "O"
    groups = [(_path_points(outer), [_path_points(hole) for hole in holes]) for outer, holes in SMALL_CAPTION_OUTLINES[base]]
    points = [point for outer, _ in groups for point in outer]
    left, right = min(x for x, y in points), max(x for x, y in points)
    top, bottom = min(y for x, y in points), max(y for x, y in points)
    target = _FACE_BOUNDS[base]
    def transform(point):
        x, y = point
        return (target[0] + (x - left) * (target[2] - target[0]) / (right - left),
                target[1] + (y - top) * (target[3] - target[1]) / (bottom - top))
    result = [([transform(p) for p in outer], [[transform(p) for p in hole] for hole in holes]) for outer, holes in groups]
    if character in _ACCENTS:
        accents = _ACCENTS[character][1:]
        if character == "Å":
            result.append((_path_points(accents[0]), [_path_points(accents[1])]))
        else:
            result.extend((_path_points(path), []) for path in accents)
    elif character == "Ç":
        result.append((_path_points("M8 20.8 C8 21.5 10.5 22 9.6 23.1 C9.3 23.7 8.7 24 8 24 L8 23 C9 22.8 8.8 22.4 7.7 22 L7 21 Z"), []))
    elif character == "Ø":
        result.append((_path_points("M14.8 8 L16 8 L4.2 21 L3 21 Z"), []))
    return result


def _face_mask(character, scale):
    """Rasterize authored paths at a supersampled cell resolution."""
    _, _, width, height = SMALL_CAPTION_GLYPHS[character]
    mask = Image.new("L", (width * scale, height * scale))
    def raster(point):
        return (point[0] * scale, point[1] * scale)
    for outer, holes in _fitted_groups(character):
        part = Image.new("L", mask.size)
        draw = ImageDraw.Draw(part)
        draw.polygon([raster(point) for point in outer], fill=255)
        for hole in holes:
            draw.polygon([raster(point) for point in hole], fill=0)
        mask = ImageChops.lighter(mask, part)
    # Geometric endpoints lie on pixel boundaries, while ImageDraw polygons
    # include endpoints. Restrict that final sample to the original ink bounds.
    left, top, right, bottom = _FACE_BOUNDS[character]
    bounded = Image.new("L", mask.size)
    bounded.paste(mask.crop((left * scale, top * scale, right * scale, bottom * scale)), (left * scale, top * scale))
    return bounded


def _validate_raster(scale, antialias):
    if not isinstance(scale, int) or not 1 <= scale <= 16:
        raise ValueError("Small caption raster scale must be an integer in 1..16")
    if not isinstance(antialias, int) or not 1 <= antialias <= 4 or scale * antialias > 32:
        raise ValueError("Small caption antialias factor must be in 1..4 with total raster scale at most 32")


def small_caption_glyph(character, scale=4, color=(255, 255, 255), antialias=2):
    """Draw one original cell with isolated AA and a linear-filtering guard.

    White face and RGB8 edge/shadow can be multiplied by the original menu
    selection colors. Each cell has one transparent output texel at its border,
    which touches only shadow, never the original face contour.
    """
    _validate_raster(scale, antialias)
    raster_scale = scale * antialias
    _, _, width, height = SMALL_CAPTION_GLYPHS[character]
    face = _face_mask(character, raster_scale)
    edge = face.filter(ImageFilter.MaxFilter(2 * raster_scale + 1))
    shadow = Image.new("L", face.size)
    shadow.paste(edge, (-2 * raster_scale, 2 * raster_scale))
    edge = ImageChops.lighter(edge, shadow)
    image = Image.new("RGBA", face.size)
    image.paste((8, 8, 8, 255), (0, 0), edge)
    image.paste((*color[:3], 255), (0, 0), face)
    if antialias != 1:
        image = image.resize((width * scale, height * scale), Image.Resampling.LANCZOS)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, image.width - 1, 0), fill=(0, 0, 0, 0))
    draw.rectangle((0, image.height - 1, image.width - 1, image.height - 1), fill=(0, 0, 0, 0))
    draw.rectangle((0, 0, 0, image.height - 1), fill=(0, 0, 0, 0))
    draw.rectangle((image.width - 1, 0, image.width - 1, image.height - 1), fill=(0, 0, 0, 0))
    return image


def small_caption_atlas(scale=4, color=(255, 255, 255), antialias=2):
    """Pack independently antialiased glyphs in the canonical LIL640 cells."""
    _validate_raster(scale, antialias)
    atlas = Image.new("RGBA", tuple(dimension * scale for dimension in SMALL_CAPTION_ATLAS_SIZE))
    for character, (x, y, _, _) in SMALL_CAPTION_GLYPHS.items():
        atlas.alpha_composite(small_caption_glyph(character, scale, color, antialias), (x * scale, y * scale))
    return atlas
