"""Reusable, independently authored contours for the original-style HUD font.

Coordinates use each original large glyph's 35-unit cap box. Curved figures
share a consistent italic stem direction and broad counters; 1/4/7 retain their
straight cuts. No bitmap, installed font, tracing library or runtime dependency
is used. Small figures use their own upright masters and one common italic shear.
This module supplies font geometry; build-hud-assets.py owns colors, bevels,
labels, atlas placement and WebP encoding.
"""

import re

LARGE_CAP_TOP = 1
LARGE_CAP_BOTTOM = 33


def cap_y(value):
    # Original colored texels occupy rows1..32, followed by two shadow rows.
    # A vector edge at33 covers that last colored row without changing the35px
    # source box. Raw contours use1..32; keep their proportions and top anchor.
    return LARGE_CAP_TOP + (value - 1) * (LARGE_CAP_BOTTOM - LARGE_CAP_TOP) / 31


# Paths use M/L/C/Q/Z commands. Counters are separate closed contours.
LARGE_PATHS = {
    "0": (62,
          "M22 1 L50 1 C57 1 61 3 61 7 C61 12 58 23 55 27 C53 30 50 32 45 32 L9 32 C3 32 0 30 1 25 L6 11 C9 4 14 1 22 1 Z",
          ["M28 8 L41 8 C47 8 50 9 49 13 L45 23 C43 27 39 28 34 28 L23 28 C17 28 14 26 16 21 L19 13 C20 9 23 8 28 8 Z"]),
    "1": (36,
          "M17 1 L35 1 L25 32 L12 32 L20 8 L14 11 L7 14 L2 14 L0 10 Z", []),
    "2": (63,
          "M23 1 L52 1 C59 1 62 4 62 7 L61 11 C60 15 57 17 51 18 L21 23 C17 24 15 26 14 27 L56 27 L54 32 L1 32 L3 25 C4 21 7 18 14 17 L44 12 C48 11 49 10 48 9 C47 8 44 8 40 8 L28 8 C24 8 22 10 21 12 L8 12 C10 6 15 1 23 1 Z", []),
    "3": (62,
          "M20 1 L51 1 C59 1 62 4 61 9 C60 12 58 14 54 15 C58 16 58 20 56 25 C55 30 51 32 45 32 L8 32 C2 32 0 30 0 26 L13 26 C14 28 17 28 21 28 L34 28 C40 28 43 27 45 25 C47 22 45 21 41 21 L24 21 L26 14 L42 14 C47 14 49 12 49 10 C49 8 45 8 40 8 L26 8 C22 8 19 9 18 11 L5 11 C7 5 13 1 20 1 Z", []),
    "4": (60,
          "M41 1 L58 1 L51 21 L59 21 L56 27 L49 27 L47 32 L35 32 L37 27 L0 27 L3 19 Z",
          ["M43 8 L17 21 L39 21 Z"]),
    "5": (61,
          "M10 1 L60 1 L58 7 L20 7 L18 12 L42 12 C53 12 58 15 57 21 C56 29 51 32 44 32 L8 32 C2 32 0 30 0 26 L13 26 C14 28 17 28 22 28 L34 28 C40 28 44 26 44 23 C44 20 40 19 34 19 L3 19 Z", []),
    "6": (61,
          "M21 1 L50 1 C57 1 61 4 60 8 L47 9 C46 7 43 7 39 7 L28 7 C22 7 19 9 17 14 C22 12 26 12 31 12 L42 12 C52 12 57 16 56 22 C55 29 51 32 44 32 L9 32 C2 32 0 30 1 24 L6 10 C8 4 14 1 21 1 Z",
          ["M23 20 L36 20 C42 20 45 21 44 24 C43 27 39 28 34 28 L23 28 C17 28 13 27 14 24 C15 21 18 20 23 20 Z"]),
    "7": (57,
          "M2 1 L56 1 L54 7 L17 32 L1 32 L39 8 L0 8 Z", []),
    "8": (62,
          "M20 1 L51 1 C58 1 62 4 61 8 C60 12 57 14 53 16 C57 18 58 21 56 26 C54 30 50 32 44 32 L10 32 C3 32 0 30 1 25 C2 21 5 18 10 16 C5 14 5 10 7 7 C9 3 13 1 20 1 Z",
          ["M28 7 L38 7 C45 7 49 8 48 11 C47 14 43 15 38 15 L27 15 C21 15 18 14 19 11 C20 8 23 7 28 7 Z",
           "M24 21 L35 21 C41 21 44 22 44 24 C43 27 39 28 33 28 L23 28 C17 28 14 27 15 24 C16 22 19 21 24 21 Z"]),
    "9": (60,
          "M21 1 L48 1 C55 1 59 4 59 9 C59 14 56 25 53 28 C51 31 47 32 42 32 L7 32 C2 32 0 30 0 26 L12 26 C13 28 16 28 20 28 L33 28 C39 28 42 26 44 21 C39 22 35 22 31 22 L18 22 C9 22 5 20 4 16 C3 12 6 6 10 4 C13 2 17 1 21 1 Z",
          ["M28 8 L38 8 C44 8 48 9 48 12 C47 15 44 17 38 17 L27 17 C21 17 17 16 18 13 C19 10 22 8 28 8 Z"]),
    "R": (65,
          "M15 1 L52 1 C60 1 64 4 63 9 C62 14 59 16 53 18 C57 20 58 22 56 27 L54 32 L42 32 L44 25 C45 22 43 21 39 21 L22 21 L18 32 L4 32 Z",
          ["M27 8 L43 8 C49 8 51 9 50 12 C49 15 45 16 40 16 L24 16 Z"]),
    "N": (64,
          "M13 1 L27 1 L43 22 L50 1 L63 1 L53 32 L39 32 L23 11 L16 32 L3 32 Z", []),
}


def flatten_path(path, steps=16):
    """Sample authored Bezier curves; straight cuts keep their exact endpoints."""
    tokens = re.findall(r"[MLCQZ]|-?\d+(?:\.\d+)?", path)
    points = []
    index = 0
    current = (0.0, 0.0)
    start = current
    while index < len(tokens):
        command = tokens[index]
        index += 1
        count = {"M": 2, "L": 2, "Q": 4, "C": 6, "Z": 0}[command]
        values = [float(value) for value in tokens[index:index + count]]
        index += count
        if command in {"M", "L"}:
            current = tuple(values)
            points.append(current)
            if command == "M":
                start = current
        elif command == "Z":
            current = start
        else:
            previous = current
            for step in range(1, steps + 1):
                t = step / steps
                s = 1 - t
                if command == "Q":
                    point = tuple(s * s * previous[c] + 2 * s * t * values[c] + t * t * values[c + 2] for c in (0, 1))
                else:
                    point = tuple(s ** 3 * previous[c] + 3 * s * s * t * values[c] + 3 * s * t * t * values[c + 2] + t ** 3 * values[c + 4] for c in (0, 1))
                points.append(point)
            current = tuple(values[-2:])
    return points


def large_geometry():
    """Return the existing generator's (natural width, outline, counters) format."""
    return {character: (width, [(x, cap_y(y)) for x, y in flatten_path(outer)], [[(x, cap_y(y)) for x, y in flatten_path(hole)] for hole in holes])
            for character, (width, outer, holes) in LARGE_PATHS.items()}


def large_glyph_mask(character, width, height, scale):
    """Rasterize an authored large contour, without automatic corner rounding.

    The caller supplies supersampling and downsampling. Original source boxes,
    advancement remain under the atlas generator.
    """
    from PIL import Image, ImageDraw

    natural, outer, holes = LARGE_PATHS[character]
    mask = Image.new("L", (round(width * scale), round(height * scale)))
    draw = ImageDraw.Draw(mask)
    for path, value in [(outer, 255), *((hole, 0) for hole in holes)]:
        points = [(x * width / natural * scale, cap_y(y) * height / 35 * scale) for x, y in flatten_path(path)]
        draw.polygon(points, fill=value)
    return mask


SMALL_CAP_TOP = 1
SMALL_CAP_BOTTOM = 11
SMALL_SHEAR = .30


def rounded_box(left, top, right, bottom, radius):
    """One shared upright corner construction for bowls and counters."""
    radius = min(radius, (right - left) / 2, (bottom - top) / 2)
    return (f"M{left + radius} {top} L{right - radius} {top} "
            f"Q{right} {top} {right} {top + radius} L{right} {bottom - radius} "
            f"Q{right} {bottom} {right - radius} {bottom} L{left + radius} {bottom} "
            f"Q{left} {bottom} {left} {bottom - radius} L{left} {top + radius} "
            f"Q{left} {top} {left + radius} {top} Z")


# These masters are upright. Individual contours contain no italic offsets;
# digit slant is applied once, after curves/counters have been constructed.
# Bowls use2.2-unit horizontal and2.5-unit vertical strokes. Punctuation keeps
# its original upright alignment and ink bounds.
SMALL_PATHS = {
    "0": (rounded_box(1.5, 1, 15.5, 11, 2.5), [rounded_box(4, 3.2, 13, 8.8, 1.1)]),
    "1": ("M7 1 L10.5 1 L10.5 11 L7.8 11 L7.8 3.8 L4.5 5 L4.5 2.8 Z", []),
    "2": ("M4.5 1 L12.5 1 C14.5 1 15.5 2.1 15.5 3.8 C15.5 5.6 14.2 6.3 12 6.9 L5 8.8 L15.5 8.8 L15.5 11 L1.5 11 L1.5 8.9 C1.5 7.6 2.5 6.8 4.5 6.3 L11.5 4.4 C12.5 4.1 13 3.9 13 3.6 C13 3.3 12.5 3.2 11.8 3.2 L5.5 3.2 C4.5 3.2 4 3.6 4 4.2 L1.5 4.2 C1.5 2.1 2.5 1 4.5 1 Z", []),
    "3": ("M2 1 L12.5 1 C14.5 1 15.5 2 15.5 3.6 C15.5 4.8 15 5.5 14 6 C15 6.5 15.5 7.2 15.5 8.4 C15.5 10 14.5 11 12.5 11 L2 11 L2 8.8 L11.5 8.8 C12.5 8.8 13 8.5 13 7.9 C13 7.3 12.5 7.1 11.5 7.1 L7 7.1 L7 4.9 L11.5 4.9 C12.5 4.9 13 4.7 13 4.1 C13 3.5 12.5 3.2 11.5 3.2 L2 3.2 Z", []),
    "4": ("M10.5 1 L13 1 L13 7 L15.5 7 L15.5 9.2 L13 9.2 L13 11 L10.5 11 L10.5 9.2 L1.5 9.2 L1.5 7 Z", ["M5 7 L10.5 7 L10.5 3.4 Z"]),
    "5": ("M2 1 L15.5 1 L15.5 3.2 L4.5 3.2 L4.5 4.9 L12.5 4.9 C14.5 4.9 15.5 6 15.5 7.9 C15.5 9.9 14.5 11 12.5 11 L2 11 L2 8.8 L11.5 8.8 C12.5 8.8 13 8.5 13 7.9 C13 7.3 12.5 7.1 11.5 7.1 L2 7.1 Z", []),
    "6": ("M4.5 1 L12.5 1 C14.5 1 15.5 2 15.5 3.2 L12.7 3.2 L6 3.2 C4.5 3.2 4 3.8 4 4.9 L12.5 4.9 C14.5 4.9 15.5 6 15.5 8 C15.5 10 14.5 11 12.5 11 L4.5 11 C2.5 11 1.5 10 1.5 8 L1.5 4 C1.5 2 2.5 1 4.5 1 Z", [rounded_box(4, 7.1, 13, 8.8, .65)]),
    "7": ("M1.5 1 L15.5 1 L15.5 3.2 L6 11 L2.5 11 L12 3.2 L1.5 3.2 Z", []),
    "8": ("M4.5 1 L12.5 1 C14.5 1 15.5 2 15.5 3.6 C15.5 4.8 15 5.5 14 6 C15 6.5 15.5 7.2 15.5 8.4 C15.5 10 14.5 11 12.5 11 L4.5 11 C2.5 11 1.5 10 1.5 8.4 C1.5 7.2 2 6.5 3 6 C2 5.5 1.5 4.8 1.5 3.6 C1.5 2 2.5 1 4.5 1 Z", [rounded_box(4, 3.2, 13, 4.9, .65), rounded_box(4, 7.1, 13, 8.8, .65)]),
    ":": (rounded_box(1, 3, 6, 5, .1), []),
    "-": (rounded_box(3, 4, 15, 6, .1), []),
    "+": ("M4.5 0 L7 0 L7 4 L11 4 L11 6.5 L7 6.5 L7 10 L4.5 10 L4.5 6.5 L0.5 6.5 L0.5 4 L4.5 4 Z", []),
    ".": (rounded_box(1, 8, 5, 10, .1), []),
}
SMALL_NATURAL_WIDTHS = {":": 7, "-": 19, "+": 11, ".": 7}


def small_glyph_mask(character, width, height, scale):
    """Draw the coherent small font, then apply its single affine italic shear.

    6/9 are the same upright master rotated180 degrees. Punctuation stays
    upright. Red/yellow rows share exactly the same glyph geometry.
    """
    from PIL import Image, ImageDraw

    master = "6" if character == "9" else character
    outer, holes = SMALL_PATHS[master]
    contours = [(outer, 255), *((hole, 0) for hole in holes)]
    if character == ":":
        contours.append((rounded_box(1, 8, 6, 10, .1), 255))
    natural = SMALL_NATURAL_WIDTHS.get(character, 19)
    mask = Image.new("L", (round(width * scale), round(height * scale)))
    draw = ImageDraw.Draw(mask)
    for path, value in contours:
        points = []
        for x, y in flatten_path(path):
            if character == "9":
                x, y = 17 - x, 12 - y
            if character.isdigit():
                x += SMALL_SHEAR * (SMALL_CAP_BOTTOM - y)
            points.append((x * width / natural * scale, y * height / 12 * scale))
        draw.polygon(points, fill=value)
    return mask
