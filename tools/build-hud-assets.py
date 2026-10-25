#!/usr/bin/env python3
"""Generate the complete Mega HUD font/gauge set from reusable vector geometry.

The wide italic numerals, bevels, labels and tachometer segments follow the
canonical HUD's design and coordinates. No source bitmap or installed font is
used to produce the artwork. Supersampling antialiases these new vector shapes;
it never enlarges original pixels. Optional exports provide named WebP sprites,
a source-rectangle manifest and a labelled preview. Original reference exports
stay under build/.
"""

import argparse
from io import BytesIO
import json
from math import cos, hypot, pi, sin, sqrt
from pathlib import Path
import runpy
import struct
import zlib

from PIL import Image, ImageChops, ImageDraw, ImageFilter

from git_game_data import configured_data_root
from car_name_font_geometry import CAR_NAME_GLYPHS, CAR_NAME_SCALE, car_name_atlas
from hud_art_exports import export_sprites, save_preview, sprite_catalog
from hud_caption_geometry import CAPTION_GLYPHS, caption_atlas
from hud_font_geometry import large_glyph_mask, small_glyph_mask
from hud_menu_font_geometry import MENU_FONTS, menu_font_atlas
from hud_small_caption_geometry import SMALL_CAPTION_GLYPHS, small_caption_atlas


FACTOR = 4
ANTIALIAS = 2
SCALE = FACTOR * ANTIALIAS
SIZE = 256

LARGE = [(60, 70, 62, 35), (0, 0, 36, 35), (36, 0, 63, 35), (99, 0, 62, 35), (161, 0, 60, 35), (0, 35, 61, 35),
         (61, 35, 61, 35), (123, 35, 57, 35), (180, 35, 62, 35), (0, 70, 60, 35), (122, 70, 65, 35), (187, 70, 64, 35)]
SMALL_X = [0, 21, 40, 59, 78, 97, 116, 135, 154, 173, 192, 200, 219, 235]
SMALL_WIDTH = [19] * 10 + [7, 19, 11, 7]
RPM_RECTS = [(5, 179, 20, 11), (28, 175, 20, 15), (53, 171, 19, 18), (75, 169, 20, 19), (96, 164, 19, 17), (20, 150, 16, 16),
             (39, 147, 16, 16), (55, 147, 14, 17), (69, 146, 12, 17), (83, 145, 10, 16), (93, 145, 9, 16), (102, 145, 17, 16)]
RPM_COLORS = [(247, 239, 247), (255, 255, 206), (255, 255, 148), (255, 255, 99), (255, 255, 49), (255, 255, 0),
              (255, 206, 0), (255, 148, 0), (255, 99, 0), (255, 49, 0), (255, 24, 0), (255, 0, 0)]
# One analytic ellipse with a parallel inner edge at a constant normal distance.
# It has positive monotone curvature, no interpolation anchors or inflections.
# The horizontal left cap retains the original y467 envelope; sector cuts stay
# inside the original neighboring sprite-cell overlaps.
RPM_ELLIPSE_CENTER = (568, 470.99641650618594)
RPM_ELLIPSE_RADII = (115.5, 42.99641650618593)
RPM_BAND_WIDTH = 12
RPM_LEFT_CAP_Y = 467
RPM_OUTER_CUTS = [453, 458.76, 465.5, 473.5, 483.5, 492.5, 502, 512, 522.5, 532.5, 542, 551, 567]
RPM_INNER_CUTS = [472, 476.5, 482.5, 491, 499.5, 507.5, 516, 524, 533, 542, 551, 562]
RPM_SEPARATOR_WIDTH = .5
RPM_POSITIONS = [(453, 457), (457, 449), (465, 443), (472, 438), (482, 435), (492, 433),
                 (501, 431), (511, 429), (522, 428), (532, 427), (542, 427), (551, 427)]
DIM_COLORS = [(90, 82, 90), (99, 99, 49), (99, 99, 0), (99, 99, 0), (99, 99, 0), (99, 99, 0),
              (99, 49, 0), (99, 0, 0), (99, 0, 0), (99, 0, 0), (99, 0, 0), (99, 0, 0)]

# Independently drawn block-letter outlines, matching the compact straight-edged
# original labels. Counters are explicit; no host typeface is substituted.
LETTERS = {
    "A": (10, [(0, 7), (3.7, 0), (6.3, 0), (10, 7), (7.4, 7), (6.5, 5.3), (3.4, 5.3), (2.5, 7)], [[(4.2, 3.8), (5.9, 3.8), (5, 1.9)]]),
    "B": (10, [(0, 0), (8, 0), (9.5, 1), (9.5, 3), (10, 4), (10, 6), (8.5, 7), (0, 7)], [[(2.5, 1.5), (7, 1.5), (7, 2.6), (2.5, 2.6)], [(2.5, 4.3), (7.3, 4.3), (7.3, 5.5), (2.5, 5.5)]]),
    "E": (10, [(0, 0), (10, 0), (10, 1.6), (2.6, 1.6), (2.6, 2.7), (9.5, 2.7), (9.5, 4.2), (2.6, 4.2), (2.6, 5.4), (10, 5.4), (10, 7), (0, 7)], []),
    "G": (10, [(1.5, 0), (9, 0), (10, 1.5), (7.5, 1.5), (2.7, 1.5), (2.7, 5.5), (7.5, 5.5), (7.5, 4.2), (5.3, 4.2), (5.3, 2.9), (10, 2.9), (10, 7), (1.5, 7), (0, 5.5), (0, 1.5)], []),
    "H": (10, [(0, 0), (2.5, 0), (2.5, 2.7), (7.5, 2.7), (7.5, 0), (10, 0), (10, 7), (7.5, 7), (7.5, 4.3), (2.5, 4.3), (2.5, 7), (0, 7)], []),
    "I": (2.6, [(0, 0), (2.6, 0), (2.6, 7), (0, 7)], []),
    "K": (10, [(0, 0), (2.5, 0), (2.5, 2.7), (7, 0), (10, 0), (4.8, 3.4), (10, 7), (6.7, 7), (2.5, 4.1), (2.5, 7), (0, 7)], []),
    "L": (9, [(0, 0), (2.6, 0), (2.6, 5.4), (9, 5.4), (9, 7), (0, 7)], []),
    "M": (13, [(0, 0), (3.2, 0), (6.5, 3.5), (9.8, 0), (13, 0), (13, 7), (10.2, 7), (10.2, 2.6), (6.5, 6.4), (2.8, 2.6), (2.8, 7), (0, 7)], []),
    "O": (10, [(1.5, 0), (8.5, 0), (10, 1.5), (10, 5.5), (8.5, 7), (1.5, 7), (0, 5.5), (0, 1.5)], [[(2.5, 1.6), (7.5, 1.6), (7.5, 5.4), (2.5, 5.4)]]),
    "P": (10, [(0, 0), (8, 0), (10, 1.5), (10, 3.5), (8, 5), (2.5, 5), (2.5, 7), (0, 7)], [[(2.5, 1.6), (7.2, 1.6), (7.2, 3.3), (2.5, 3.3)]]),
    "S": (10, [(1.5, 0), (8.5, 0), (10, 1.6), (7.4, 1.6), (2.5, 1.6), (2.5, 2.7), (8.5, 2.7), (10, 4), (10, 5.5), (8.5, 7), (1.5, 7), (0, 5.4), (7.5, 5.4), (7.5, 4.3), (1.5, 4.3), (0, 3), (0, 1.5)], []),
    "T": (10, [(0, 0), (10, 0), (10, 1.6), (6.3, 1.6), (6.3, 7), (3.7, 7), (3.7, 1.6), (0, 1.6)], []),
    "e": (8, [(1, 2), (7, 2), (8, 3), (8, 4.9), (2, 4.9), (2, 5.7), (7.6, 5.7), (7.6, 7), (1, 7), (0, 6), (0, 3)], [[(2, 3.2), (6, 3.2), (6, 3.8), (2, 3.8)]]),
    "a": (8, [(1, 2), (7, 2), (8, 3), (8, 7), (1, 7), (0, 6), (0, 4.5), (1, 4), (6, 4), (6, 3.3), (1, 3.3)], [[(2, 5.1), (6, 5.1), (6, 5.8), (2, 5.8)]]),
    "r": (7, [(0, 2), (6, 2), (7, 3), (7, 4), (5, 4), (5, 3.4), (2, 3.4), (2, 7), (0, 7)], []),
    "p": (8, [(0, 2), (6.5, 2), (8, 3.5), (8, 5.5), (6.5, 7), (2, 7), (2, 9), (0, 9)], [[(2, 3.4), (6, 3.4), (6, 5.6), (2, 5.6)]]),
    "h": (8, [(0, 0), (2, 0), (2, 2), (6.5, 2), (8, 3.5), (8, 7), (6, 7), (6, 3.4), (2, 3.4), (2, 7), (0, 7)], []),
}


def crc32(data):
    return f"{zlib.crc32(data):08x}"


def rounded(points, radius=0.45):
    """Round polygon corners using short quadratic curves in vector space."""
    result = []
    for index, point in enumerate(points):
        previous, following = points[index - 1], points[(index + 1) % len(points)]
        def toward(other):
            dx, dy = other[0] - point[0], other[1] - point[1]
            length = (dx * dx + dy * dy) ** 0.5
            amount = min(radius, length / 3) / length
            return point[0] + dx * amount, point[1] + dy * amount
        start, end = toward(previous), toward(following)
        for step in range(5):
            t = step / 4
            result.append(((1 - t) ** 2 * start[0] + 2 * (1 - t) * t * point[0] + t * t * end[0],
                           (1 - t) ** 2 * start[1] + 2 * (1 - t) * t * point[1] + t * t * end[1]))
    return result


def outline_mask(size, outer, holes=(), transform=lambda x, y: (x, y), radius=0.45):
    mask = Image.new("L", (size[0] * SCALE, size[1] * SCALE))
    draw = ImageDraw.Draw(mask)
    for points, color in [(outer, 255), *((hole, 0) for hole in holes)]:
        vertices = rounded(points, radius) if radius else points
        draw.polygon([(transform(x, y)[0] * SCALE, transform(x, y)[1] * SCALE) for x, y in vertices], fill=color)
    return mask


def gradient(size, color):
    """The original numerals have a pale top bevel and dark lower bevel."""
    width, height = size
    result = Image.new("RGBA", size)
    draw = ImageDraw.Draw(result)
    stops = [(0, (247, 239, 247)), (0.07, (247, 239, 247)), (0.12, tuple(min(255, c + 100) for c in color)),
             (0.2, color), (0.84, color), (0.95, tuple(round(c * .47) for c in color)), (1, tuple(round(c * .17) for c in color))]
    for y in range(height):
        value = y / max(1, height - 1)
        for (left, first), (right, second) in zip(stops, stops[1:]):
            if left <= value <= right:
                t = (value - left) / (right - left)
                rgb = tuple(round(a + (b - a) * t) for a, b in zip(first, second))
                draw.line((0, y, width, y), fill=(*rgb, 255))
                break
    return result


def number(character, width, height, color, bevel):
    if height == 12:
        mask = small_glyph_mask(character, width, height, SCALE)
    else:
        mask = large_glyph_mask(character, width, height, SCALE)
    result = Image.new("RGBA", mask.size)
    # Both small flat-color rows and large beveled digits have the original
    # one-pixel gray8 edge, clipped to each unchanged sprite cell.
    shadow = mask.filter(ImageFilter.MaxFilter(2 * SCALE + 1))
    result.paste((8, 8, 8, 255), (0, 0), shadow)
    if bevel:
        fill = gradient(mask.size, color)
    else:
        fill = Image.new("RGBA", mask.size, (*color, 255))
    result.paste(fill, (0, 0), mask)
    return result


def paste(atlas, image, x, y):
    atlas.alpha_composite(image, (round(x * SCALE), round(y * SCALE)))


def polygon(atlas, points, color, shadow=False):
    draw = ImageDraw.Draw(atlas)
    vertices = [(round(x * SCALE), round(y * SCALE)) for x, y in points]
    if shadow:
        draw.line(vertices + [vertices[0]], fill=(8, 8, 8, 255), width=2 * SCALE, joint="curve")
    draw.polygon(vertices, fill=(*color, 255))


def rpm_curves():
    """Return the true ellipse, its constant-width parallel curve and left cap.

    x=cx-rx*sin(t), y=cy-ry*cos(t). The unit inward normal is proportional
    to(ry*sin(t),rx*cos(t)); moving exactly12 units along it defines the inner
    edge. Keeping the width below the ellipse's minimum curvature radius avoids
    a cusp or inflection in the parallel curve.
    """
    cx, cy = RPM_ELLIPSE_CENTER
    rx, ry = RPM_ELLIPSE_RADII
    if not 0 < RPM_BAND_WIDTH < min(rx, ry) ** 2 / max(rx, ry):
        raise ValueError("RPM band width must be smaller than the ellipse's minimum curvature radius")
    def outer(x):
        return cy - ry * sqrt(1 - ((x - cx) / rx) ** 2)
    def inward(theta):
        s, c = sin(theta), cos(theta)
        length = hypot(ry * s, rx * c)
        return (cx - rx * s + RPM_BAND_WIDTH * ry * s / length,
                cy - ry * c + RPM_BAND_WIDTH * rx * c / length)
    def inner(x):
        low, high = 0, pi / 2
        for _ in range(44):
            theta = (low + high) / 2
            if inward(theta)[0] > x:
                low = theta
            else:
                high = theta
        return inward((low + high) / 2)[1]
    low, high = 0, pi / 2
    for _ in range(44):
        theta = (low + high) / 2
        if inward(theta)[1] < RPM_LEFT_CAP_Y:
            low = theta
        else:
            high = theta
    return outer, inner, inward((low + high) / 2)[0]


def rpm_geometry():
    outer, inner, inner_start = rpm_curves()
    inner_cuts = [inner_start, *RPM_INNER_CUTS]
    def sample(curve, left, right):
        count = max(2, round((right - left) * 4))
        return [(left + (right - left) * i / count, curve(left + (right - left) * i / count)) for i in range(count + 1)]
    sectors = [sample(outer, left, right) + sample(inner, inner_left, inner_right)[::-1]
               for left, right, inner_left, inner_right in zip(RPM_OUTER_CUTS, RPM_OUTER_CUTS[1:], inner_cuts, inner_cuts[1:])]
    envelope = sample(outer, RPM_OUTER_CUTS[0], RPM_OUTER_CUTS[-1]) + sample(inner, inner_cuts[0], inner_cuts[-1])[::-1]
    boundaries = [((x, outer(x)), (inner_x, inner(inner_x))) for x, inner_x in zip(RPM_OUTER_CUTS[1:-1], inner_cuts[1:-1])]
    return envelope, sectors, boundaries


def rpm_layers():
    """Render the complete band once, then expose identical active/dim cutouts.

    Layers use the original background's world rectangle452,427,116,41. Active
    sprites are cropped from these layers into their exact original source cells.
    There is no separately outlined quadrilateral at an illumination boundary.
    """
    envelope, sectors, boundaries = rpm_geometry()
    size = (116 * SCALE, 41 * SCALE)
    def local(points):
        return [((x - 452) * SCALE, (y - 427) * SCALE) for x, y in points]
    band = Image.new("L", size)
    ImageDraw.Draw(band).polygon(local(envelope), fill=255)
    dividers = Image.new("L", size)
    draw = ImageDraw.Draw(dividers)
    for boundary in boundaries:
        draw.line(local(boundary), fill=255, width=round(RPM_SEPARATOR_WIDTH * SCALE))
    background = Image.new("RGBA", size)
    background.paste((8, 8, 8, 255), (0, 0), band.filter(ImageFilter.MaxFilter(SCALE + 1)))
    active = []
    for index, (points, color, dim, position, rectangle) in enumerate(zip(sectors, RPM_COLORS, DIM_COLORS, RPM_POSITIONS, RPM_RECTS)):
        mask = Image.new("L", size)
        ImageDraw.Draw(mask).polygon(local(points), fill=255)
        mask = ImageChops.subtract(ImageChops.multiply(mask, band), dividers)
        # Fail before cropping if a future curve edit would silently cut off a
        # sector. Only the exact original destination/source cells are accepted.
        x, y = position
        _, _, width, height = rectangle
        cell = ((x - 452) * SCALE, (y - 427) * SCALE, (x - 452 + width) * SCALE, (y - 427 + height) * SCALE)
        bounds = mask.getbbox()
        if bounds and (bounds[0] < cell[0] or bounds[1] < cell[1] or bounds[2] > cell[2] or bounds[3] > cell[3]):
            raise ValueError(f"RPM sector{index + 1} alpha bounds{bounds} exceed original sprite cell{cell}")
        background.paste((*dim, 255), (0, 0), mask)
        sprite = Image.new("RGBA", size)
        sprite.paste((*color, 255), (0, 0), mask)
        active.append(sprite)
    return background, active


def label(atlas, text, x, y, width, height, bevel=False, italic=False, outline=False):
    advances = [LETTERS[char][0] + 1.2 for char in text]
    natural = sum(advances) - 1.2
    mask = Image.new("L", (round(width * SCALE), round(height * SCALE)))
    draw = ImageDraw.Draw(mask)
    left = 0
    for character, advance in zip(text, advances):
        _, outer, holes = LETTERS[character]
        for vertices, fill in [(outer, 255), *((hole, 0) for hole in holes)]:
            points = [((left + px + (7 - py) * .18 * italic) * width / (natural + 1.3 * italic) * SCALE, py * height / 7 * SCALE) for px, py in vertices]
            draw.polygon(points, fill=fill)
        left += advance
    result = Image.new("RGBA", mask.size)
    if bevel:
        result.paste(gradient(mask.size, (247, 239, 247)), (0, 0), mask)
    else:
        result.paste((247, 239, 247, 255), (0, 0), mask)
    if outline:
        expanded = Image.new("L", (mask.width + 2 * SCALE, mask.height + 2 * SCALE))
        expanded.paste(mask, (SCALE, SCALE))
        shadow = Image.new("RGBA", expanded.size)
        shadow.paste((8, 8, 8, 255), (0, 0), expanded.filter(ImageFilter.MaxFilter(2 * SCALE + 1)))
        shadow.alpha_composite(result, (SCALE, SCALE))
        paste(atlas, shadow, x - 1, y - 1)
    else:
        paste(atlas, result, x, y)


def reference_detail_label(atlas, shapes, origin, size, stops):
    """Draw measured original label contours with their own baseline/descender.

    Coordinates are expressed directly in the source atlas, avoiding the generic
    seven-unit cap box that clipped p and moved the English M five pixels right.
    """
    mask = Image.new("L", (size[0] * SCALE, size[1] * SCALE))
    draw = ImageDraw.Draw(mask)
    for outer, holes in shapes:
        for points, color in [(outer, 255), *((hole, 0) for hole in holes)]:
            draw.polygon([((x - origin[0]) * SCALE, (y - origin[1]) * SCALE) for x, y in rounded(points, .12)], fill=color)
    fill = Image.new("RGBA", mask.size)
    pixels = ImageDraw.Draw(fill)
    for row in range(mask.height):
        y = origin[1] + row / SCALE
        for (top, a), (bottom, b) in zip(stops, stops[1:]):
            if top <= y <= bottom:
                amount = (y - top) / (bottom - top)
                color = tuple(round(x + (z - x) * amount) for x, z in zip(a, b))
                pixels.line((0, row, mask.width, row), fill=(*color, 255))
                break
    result = Image.new("RGBA", mask.size)
    result.paste(fill, (0, 0), mask)
    paste(atlas, result, *origin)


def gauge_labels(atlas, kilometres):
    gear = [
        ([(54, 192.5), (60, 192.5), (60.5, 193.8), (55, 193.8), (53, 194.5), (52.5, 196), (53, 196.7), (57.7, 196.7),
          (58, 195.7), (55, 195.7), (55.2, 194.7), (59.6, 194.7), (59, 197), (57, 198), (52.5, 198), (51.5, 196.8), (51.8, 194.8), (52.7, 193.2)], []),
        ([(64, 194), (70, 194), (71, 195), (70.5, 196.2), (64, 196.2), (64, 197), (69.8, 197), (69.2, 198), (63.5, 198), (62.5, 197), (62.8, 195)],
         [[(65, 195), (69, 195), (68.8, 195.6), (64.8, 195.6)]]),
        ([(75, 194), (81, 194), (81.5, 195), (80.8, 198), (74, 198), (73, 197), (73.5, 195.5), (79.5, 195.5), (79.7, 195), (74.8, 195)],
         [[(75.7, 196.3), (79.2, 196.3), (79, 197.2), (75.4, 197.2)]]),
        ([(85, 194), (91, 194), (92, 195), (91.5, 196), (90.2, 196), (90.3, 195), (86.8, 195), (86.2, 198), (84.3, 198)], []),
    ]
    white = (247, 239, 247)
    reference_detail_label(atlas, gear, (48, 191), (48, 9),
                           [(191, (80, 80, 80)), (192.5, (132, 132, 132)), (193.2, white), (196.5, white), (198.5, (90, 90, 90)), (200, (90, 90, 90))])
    k = ([(57.3, 202.5), (59.4, 202.5), (58.7, 204.5), (63.6, 202.5), (65.8, 202.5), (60.1, 205), (63.6, 208), (61.4, 208),
          (58.1, 205.8), (57.4, 208), (55.4, 208)], [])
    m = ([(52.5, 202.5), (55.2, 202.5), (57.9, 206.2), (62.1, 202.5), (65, 202.5), (63.3, 208), (61.5, 208), (62.6, 204.4),
          (58.8, 207.8), (56.6, 207.8), (54.4, 204.3), (53.2, 208), (51.4, 208)], [])
    p = ([(68.4, 204.1), (74.3, 204.1), (75.4, 205), (74.7, 207), (73.7, 208), (68.7, 208), (68.2, 209.5), (66.5, 209.5)],
         [[(69.5, 205.1), (73.3, 205.1), (73.1, 206.8), (69.1, 206.8)]])
    h = ([(79.5, 202.5), (81.6, 202.5), (80.9, 204.6), (86.1, 204.6), (86.7, 202.5), (88.8, 202.5), (87.1, 208), (85, 208),
          (85.8, 205.8), (80.5, 205.8), (79.8, 208), (77.9, 208)], [])
    reference_detail_label(atlas, [k if kilometres else m, p, h], (48, 201), (48, 9),
                           [(201, (100, 100, 100)), (202.5, (132, 132, 132)), (203.1, white), (207, white), (208, (156, 156, 156)),
                            (209.5, (90, 90, 90)), (210, (90, 90, 90))])


def primary(kilometres):
    atlas = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE))
    for index, character in enumerate("0123456789R"):
        x, y, width, height = LARGE[index]
        paste(atlas, number(character, width, height, (0, 123, 189), True), x, y)
    for row, color in [(106, (255, 0, 0)), (118, (255, 255, 0))]:
        for index, character in enumerate("0123456789:-+."):
            paste(atlas, number(character, SMALL_WIDTH[index], 12, color, False), SMALL_X[index], row)
    background, active = rpm_layers()
    paste(atlas, background, 123, 144)
    for (x, y, width, height), (world_x, world_y), sprite in zip(RPM_RECTS, RPM_POSITIONS, active):
        left, top = world_x - 452, world_y - 427
        patch = sprite.crop((left * SCALE, top * SCALE, (left + width) * SCALE, (top + height) * SCALE))
        paste(atlas, patch, x, y)
    # Near-black original strips, preserving the separate transparent regions.
    polygon(atlas, [(15, 190), (165, 190), (165, 210), (15, 210)], (8, 8, 8))
    gauge_labels(atlas, kilometres)
    # Preserve the complete original backing. A visible separator replaces the
    # almost-black diagonal in its original source rectangle before the limit.
    polygon(atlas, [(0, 212), (186, 212), (186, 227), (0, 227)], (8, 8, 8))
    separator = outline_mask((9, 13), [(6, 0), (8, 0), (2, 13), (0, 13)], radius=.15)
    slash = Image.new("RGBA", separator.size)
    slash.paste((8, 8, 8, 255), (0, 0), separator.filter(ImageFilter.MaxFilter(7)))
    slash.paste(gradient(separator.size, (247, 239, 247)), (0, 0), separator)
    paste(atlas, slash, 128, 227)
    label(atlas, "LAP", 129, 215, 55, 9, bevel=True)
    polygon(atlas, [(0, 241), (187, 241), (187, 256), (0, 256)], (8, 8, 8))
    label(atlas, "POS", 3, 244, 56, 9, bevel=True)
    # Separate label backing follows each original letter silhouette, not a box.
    label(atlas, "TIME", 179, 187, 67, 9, outline=True)
    label(atlas, "BEST", 179, 201, 70, 9, outline=True)
    # 4ada60: checkpoint label at (1,132,75,11), shared by both units.
    label(atlas, "SPLIT", 2, 133, 73, 9, outline=True)
    return atlas


def secondary():
    atlas = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE))
    for index, character in enumerate("0123456789RN"):
        x, y, width, height = LARGE[index]
        if character != "N":
            paste(atlas, number(character, width, height, (255, 0, 0), True), x, y)
        paste(atlas, number(character, width, height, (255, 255, 0), True), x, y + 106)
    return atlas


def export_reference(root, output):
    """Optional read-only authoring aid. Never put these exports in assets/."""
    if "build" not in output.parts:
        raise ValueError("Original reference exports must stay beneath build/")
    archive = (root / "data/image640.bfp").read_bytes()
    entries = runpy.run_path(str(Path(__file__).with_name("extract-assets.py")))["read_bfp"](archive)
    output.mkdir(parents=True, exist_ok=True)
    for index in (25, 26, 27, 32, 33, 36):
        _, _, offset, size = entries[index]
        Image.open(BytesIO(archive[offset:offset + size])).convert("RGB").save(output / f"atlas-{index}.png")
    executable = (root / "tourcars.exe").read_bytes()
    pe = struct.unpack_from("<I", executable, 60)[0]
    count = struct.unpack_from("<H", executable, pe + 6)[0]
    start = pe + 24 + struct.unpack_from("<H", executable, pe + 20)[0]
    sections = [struct.unpack_from("<III", executable, start + i * 40 + 12) for i in range(count)]
    def word(address):
        rva = address - 0x400000
        for virtual, size, offset in sections:
            if virtual <= rva < virtual + size:
                return struct.unpack_from("<h", executable, offset + rva - virtual)[0]
        raise ValueError(f"No canonical data section at{address:x}")
    metadata = {"archive_crc32": crc32(archive), "executable_crc32": crc32(executable),
                "large_glyphs": LARGE, "small_glyph_x": SMALL_X, "small_glyph_width": SMALL_WIDTH,
                "rpm_segments": RPM_RECTS,
                "rectangles": {name: [word(address + i * 2) for i in range(4)] for name, address in
                               {"speed_background": 0x4ada18, "rpm_background": 0x4ada48, "lap_background": 0x4ad9b8,
                                "timing_labels": 0x4ad9a0, "position_background": 0x4ad9e8}.items()}}
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets/ui/mega/hud"))
    parser.add_argument("--sprites", type=Path, help="Also export every named font/gauge sprite as WebP70 with a manifest")
    parser.add_argument("--preview", type=Path, help="Save a labelled overview of the encoded font/gauge set")
    parser.add_argument("--export-reference", type=Path)
    parser.add_argument("--data-root", type=Path, help="Unpacked Git game-data root for reference exports; defaults to TOCA_DATA_ROOT")
    args = parser.parse_args()
    if args.export_reference:
        export_reference(args.data_root or configured_data_root(), args.export_reference)
        return
    args.output.mkdir(parents=True, exist_ok=True)
    catalog = sprite_catalog(LARGE, SMALL_X, SMALL_WIDTH, RPM_RECTS, FACTOR)
    punctuation = {"!": "exclamation", "?": "question", ".": "dot", ",": "comma", "-": "dash", "'": "apostrophe",
                   "[": "inverted-exclamation", "]": "inverted-question", ":": "colon", "#": "ordinal"}
    for character, (x, y, width, height) in CAPTION_GLYPHS.items():
        catalog[f"caption-{punctuation.get(character, character)}"] = {
            "atlas": "caption-big.webp", "source": [value * FACTOR for value in (x, y, width, height)],
            "logical_size": [width, height], "group": "BIG640 captions, headings and credits"}
    for prefix, atlas, glyphs, group in [
        ("caption-small", "caption-small.webp", SMALL_CAPTION_GLYPHS, "LIL640 pause, results and credits"),
        *[(f"menufont-{index}", font["filename"], font["glyphs"], f"Frontend font {index}") for index, font in MENU_FONTS.items()],
    ]:
        for character, (x, y, width, height) in glyphs.items():
            name = punctuation.get(character, character if character.isascii() and character.isalnum() else f"u{ord(character):04x}")
            catalog[f"{prefix}-{name}"] = {"atlas": atlas, "source": [value * FACTOR for value in (x, y, width, height)],
                                          "logical_size": [width, height], "group": group}
    for character, (x, y, width, height) in CAR_NAME_GLYPHS.items():
        name = punctuation.get(character, character)
        catalog[f"car-name-{name}"] = {"atlas": "car-name.webp", "source": [value * CAR_NAME_SCALE for value in (x, y, width, height)],
                                       "logical_size": [width, height], "group": "WIN3 car names, exactly double resolution"}
    images = {}
    authored = {name: image.resize((SIZE * FACTOR, SIZE * FACTOR), Image.Resampling.LANCZOS)
                for name, image in [("primary-km", primary(True)), ("primary-miles", primary(False)), ("secondary", secondary())]}
    # Fonts are antialiased per cell before packing. Resampling a packed atlas
    # could pull a shadow from a neighboring glyph into its transparent border.
    authored["caption-big"] = caption_atlas(FACTOR, antialias=ANTIALIAS)
    authored["caption-small"] = small_caption_atlas(FACTOR, antialias=ANTIALIAS)
    for index in MENU_FONTS:
        authored[f"menufont-{index}"] = menu_font_atlas(index, FACTOR, antialias=ANTIALIAS)
    authored["car-name"] = car_name_atlas()
    for name, image in authored.items():
        # Antialias a new vector raster at its declared target resolution. This is not
        # enlargement or interpolation of any original game bitmap.
        path = args.output / f"{name}.webp"
        images[path.name] = image
        temporary = path.with_suffix(".webp.tmp")
        image.save(temporary, format="WEBP", quality=70, alpha_quality=100, method=6, exact=True)
        temporary.replace(path)
        data = path.read_bytes()
        print(f"{path}: {len(data)}bytes, CRC32{crc32(data)}")
    if args.sprites:
        export_sprites(images, catalog, args.sprites)
        print(f"Sprite set: {args.sprites}")
    if args.preview:
        decoded = {}
        for name in images:
            with Image.open(args.output / name) as image:
                decoded[name] = image.convert("RGBA")
        save_preview(decoded, catalog, args.preview)
        print(f"Overview: {args.preview}")


if __name__ == "__main__":
    main()
