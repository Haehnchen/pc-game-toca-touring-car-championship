"""Independently authored, reusable geometry for the four original menu faces.

The source records below describe layout only. No original bitmap, installed
font, tracing library or game file is read by this module. OPTION and HELP use
upright, broad geometric masters; TITLE uses the same geometric vocabulary
with its own heavier stroke, italic shear and dark silver bevel. Rasterization
and antialiasing happen separately inside every original glyph cell.
"""
from PIL import Image, ImageChops, ImageDraw
from hud_font_geometry import flatten_path, rounded_box

def _records(rows, height):
    result = {}
    for row, (characters, widths) in enumerate(rows):
        x = 0
        for character, width in zip(characters, widths, strict=True):
            result[character] = (x, row * height, width, height)
            x += width + 1
    return result
_ROWS = [[('ABCDEF', [23, 21, 23, 23, 20, 19]), ('GHIJKL', [23, 20, 4, 20, 21, 17]), ('MNOPQR', [23, 22, 23, 20, 23, 21]), ('STUVWX', [23, 21, 23, 23, 23, 21]), ('YZ0123', [22, 23, 23, 12, 22, 22]), ('456789', [22, 22, 22, 22, 23, 22]), ("/-:',.", [10, 16, 3, 3, 3, 3])], [('ABCDEF', [18, 16, 18, 19, 16, 16]), ('GHIJKL', [18, 16, 3, 16, 17, 14]), ('MNOPQR', [18, 18, 18, 16, 18, 17]), ('STUVWX', [19, 17, 19, 19, 18, 17]), ('YZ0123', [17, 19, 19, 10, 18, 18]), ('456789', [18, 18, 18, 18, 19, 18]), ("/-:',.", [8, 13, 2, 2, 3, 3])], [('ABCDEF', [11, 11, 12, 11, 10, 10]), ('GHIJKL', [12, 11, 3, 11, 12, 9]), ('MNOPQR', [17, 13, 13, 11, 12, 12]), ('STUVWX', [11, 11, 11, 14, 20, 13]), ('YZ0123', [11, 11, 11, 8, 11, 11]), ('456789', [11, 11, 11, 11, 11, 11]), (":',?.(", [2, 2, 2, 6, 2, 3]), (')-+/&¿', [3, 7, 6, 5, 9, 6])], [('ABCDEF', [38, 34, 34, 34, 34, 33]), ('GHIJKL', [34, 34, 13, 31, 35, 24]), ('MNOPQR', [47, 38, 34, 32, 34, 33]), ('STUVWX', [34, 30, 35, 37, 50, 40]), ('YZ0123', [32, 36, 33, 21, 34, 33]), ('456789', [31, 32, 33, 31, 32, 31]), ('.-', [6, 18])]]
MENU_FONTS = {}
for _index, (_size, _line, _advance, _crc) in enumerate([((140, 100), 14, 1, 'ba58f7fb'), ((116, 100), 14, 1, 'c715d22c'), ((88, 64), 8, 1, '12c1a3a5'), ((232, 148), 21, -1, '9b146e0e')]):
    _glyphs = _records(_ROWS[_index], _line)
    _extended = _size
    if _index < 2:
        _glyphs.update({'(': (0, 100, 5, 14), ')': (5, 100, 5, 14)})
        _extended = (_size[0], 114)
    MENU_FONTS[_index] = {'filename': f'menufont-{_index}.webp', 'glyphs': _glyphs, 'atlas_size': _extended, 'raw_atlas_size': _size, 'line_height': _line, 'space_width': 8, 'advance': _advance, 'reference_crc32': _crc}

def _master(character, weight):
    """Closed upright paths in a 100-unit cap box, with explicit counters."""
    s = weight
    q = 100 - s
    half = s / 2
    # Inner curve handles must stay inside the stems even in the heavy TITLE
    # master. Fixed lightweight handles otherwise turn back and form hooks.
    inset = lambda value: max(s, min(q, value))
    box = lambda l, t, r, b, rad=0: rounded_box(l, t, r, b, rad)
    rect = lambda l, t, r, b: (box(l, t, r, b), [])
    polygon = lambda points: ('M' + ' L'.join((f'{x} {y}' for x, y in points)) + ' Z', [])
    bowl = lambda t, b: box(s, t, q, b, min(13, (b - t) / 3))
    outer = box(0, 0, 100, 100, 22)
    hole = box(s, s, q, q, 12)
    if character in 'O0':
        return [(outer, [hole])]
    if character == 'I':
        return [rect(0, 0, 100, 100)]
    if character == 'A':
        return [('M0 100 L40 0 L60 0 L100 100 L76 100 L67 76 L33 76 L24 100 Z', ['M41 54 L59 54 L50 26 Z'])]
    if character == 'B':
        return [('M0 0 L73 0 C91 0 100 9 100 25 C100 37 94 44 85 48 C95 52 100 59 100 74 C100 91 90 100 73 100 L0 100 Z', [bowl(s, 43), bowl(59, q)])]
    if character in 'CDG':
        if character == 'D':
            return [('M0 0 L67 0 C89 0 100 19 100 50 C100 81 89 100 67 100 L0 100 Z', ['M' + f'{s} {s} L64 {s} C{inset(75)} {s} {q} 31 {q} 50 C{q} 69 {inset(75)} {q} 64 {q} L{s} {q} Z'])]
        # Both open terminals are straight cuts. A cubic with equal endpoint
        # heights and an off-baseline handle adds a spike to the inner lip.
        shapes = [(f'M100 {s} L35 {s} C{inset(25)} {s} {s} 29 {s} 38 L{s} 62 C{s} {inset(72)} {inset(25)} {q} 35 {q} L100 {q} C100 92 85 100 70 100 L30 100 C10 100 0 83 0 65 L0 35 C0 17 10 0 30 0 L70 0 C86 0 100 8 100 {s} Z', [])]
        if character == 'G':
            shapes += [rect(55, 47, 100, 47 + s), rect(q, 47, 100, 100)]
        return shapes
    if character in 'EF':
        shapes = [rect(0, 0, s, 100), rect(0, 0, 100, s), rect(0, 50 - half, 84, 50 + half)]
        return shapes + ([rect(0, q, 100, 100)] if character == 'E' else [])
    if character == 'H':
        return [rect(0, 0, s, 100), rect(q, 0, 100, 100), rect(0, 50 - half, 100, 50 + half)]
    if character == 'J':
        return [(f'M{q} 0 L100 0 L100 67 C100 90 87 100 65 100 L30 100 C10 100 0 90 0 70 L0 60 L{s} 60 L{s} 67 C{s} {inset(75)} 30 {q} 38 {q} L62 {q} C{inset(72)} {q} {q} 70 {q} 62 Z', [])]
    if character == 'K':
        return [rect(0, 0, s, 100), polygon([(s, 44), (70, 0), (100, 0), (51, 47), (100, 100), (70, 100), (s, 61)])]
    if character == 'L':
        return [rect(0, 0, s, 100), rect(0, q, 100, 100)]
    if character == 'M':
        return [polygon([(0, 100), (0, 0), (s, 0), (50, 47), (q, 0), (100, 0), (100, 100), (q, 100), (q, 33), (50, 77), (s, 33), (s, 100)])]
    if character == 'N':
        return [polygon([(0, 100), (0, 0), (s, 0), (q, 65), (q, 0), (100, 0), (100, 100), (q, 100), (s, 35), (s, 100)])]
    if character in 'PR':
        shapes = [(f'M0 100 L0 0 L72 0 C91 0 100 10 100 30 C100 48 90 59 72 59 L{s} 59 L{s} 100 Z', [bowl(s, 59 - s)])]
        if character == 'R':
            shapes.append(polygon([(51, 56), (77, 56), (100, 100), (74, 100)]))
        return shapes
    if character == 'Q':
        return [(outer, [hole]), polygon([(54, 61), (73, 61), (100, 100), (76, 100)])]
    if character in 'S235689':
        if character == 'S':
            return [(f'M100 {s} L{s} {s} L{s} {50 - half} L{q} {50 - half} C94 {50 - half} 100 57 100 70 L100 80 C100 94 90 100 77 100 L0 100 L0 {q} L{q} {q} L{q} {50 + half} L{s} {50 + half} C7 {50 + half} 0 43 0 30 L0 20 C0 6 10 0 23 0 L100 0 Z', [])]
        if character == '2':
            return [(f'M0 {s} C0 8 10 0 28 0 L72 0 C91 0 100 8 100 24 L100 33 C100 45 93 53 77 58 L29 {inset(74)} C{inset(24)} {inset(76)} {s} {inset(78)} {s} {q} L100 {q} L100 100 L0 100 L0 80 C0 66 6 59 22 53 L70 37 C{inset(76)} 35 {q} 32 {q} {inset(27)} C{q} {inset(22)} {inset(74)} {s} 66 {s} L{s} {s} Z', [])]
        if character == '3':
            return [(f'M0 0 L73 0 C91 0 100 8 100 25 C100 37 94 44 83 49 C94 54 100 62 100 75 C100 92 91 100 73 100 L0 100 L0 {q} L66 {q} C{inset(75)} {q} {q} {inset(73)} {q} 67 C{q} {max(61, 50 + half)} {inset(74)} {50 + half} 66 {50 + half} L31 {50 + half} L31 {50 - half} L66 {50 - half} C{inset(74)} {50 - half} {q} {min(38, 50 - half)} {q} 31 C{q} {inset(25)} {inset(75)} {s} 66 {s} L0 {s} Z', [])]
        if character == '5':
            return [(f'M100 0 L100 {s} L{s} {s} L{s} {50 - half} L72 {50 - half} C91 {50 - half} 100 58 100 75 C100 92 91 100 72 100 L0 100 L0 {q} L65 {q} C{inset(74)} {q} {q} {inset(73)} {q} 66 C{q} {max(59, 50 + half)} {inset(74)} {50 + half} 65 {50 + half} L0 {50 + half} L0 0 Z', [])]
        if character == '8':
            return [(outer, [bowl(s, 50 - half), bowl(50 + half, q)])]
        if character == '6':
            return [(f'M100 {s} L34 {s} C{inset(25)} {s} {s} 30 {s} {50 - half} L72 {50 - half} C91 {50 - half} 100 57 100 75 C100 91 91 100 72 100 L30 100 C10 100 0 85 0 65 L0 35 C0 15 10 0 30 0 L100 0 Z', [bowl(50 + half, q)])]
        if character == '9':
            return [(f'M0 {q} L66 {q} C{inset(75)} {q} {q} 70 {q} {50 + half} L28 {50 + half} C9 {50 + half} 0 43 0 25 C0 9 9 0 28 0 L70 0 C90 0 100 15 100 35 L100 65 C100 85 90 100 70 100 L0 100 Z', [bowl(s, 50 - half)])]
    if character == 'T':
        return [rect(0, 0, 100, s), rect(50 - half, 0, 50 + half, 100)]
    if character == 'U':
        return [(f'M0 0 L{s} 0 L{s} 65 C{s} {inset(74)} {inset(28)} {q} 37 {q} L63 {q} C{inset(72)} {q} {q} {inset(74)} {q} 65 L{q} 0 L100 0 L100 68 C100 89 90 100 68 100 L32 100 C10 100 0 89 0 68 Z', [])]
    if character == 'V':
        return [polygon([(0, 0), (s, 0), (50, 74), (q, 0), (100, 0), (61, 100), (39, 100)])]
    if character == 'W':
        return [polygon([(0, 0), (s, 0), (29, 70), (43, 16), (57, 16), (71, 70), (q, 0), (100, 0), (85, 100), (64, 100), (50, 53), (36, 100), (15, 100)])]
    if character == 'X':
        return [polygon([(0, 0), (26, 0), (50, 33), (74, 0), (100, 0), (63, 50), (100, 100), (74, 100), (50, 67), (26, 100), (0, 100), (37, 50)])]
    if character == 'Y':
        return [polygon([(0, 0), (26, 0), (50, 40), (74, 0), (100, 0), (50 + half, 62), (50 + half, 100), (50 - half, 100), (50 - half, 62)])]
    if character == 'Z':
        return [polygon([(0, 0), (100, 0), (100, s), (s, q), (100, q), (100, 100), (0, 100), (0, q), (q, s), (0, s)])]
    if character == '1':
        return [polygon([(0, 25), (62, 0), (100, 0), (100, 100), (64, 100), (64, 27), (0, 53)])]
    if character == '4':
        return [(f'M63 0 L88 0 L88 65 L100 65 L100 {65 + s} L88 {65 + s} L88 100 L63 100 L63 {65 + s} L0 {65 + s} L0 65 Z', ['M63 24 L28 65 L63 65 Z'])]
    if character == '7':
        return [polygon([(0, 0), (100, 0), (100, s), (30, 100), (0, 100), (71, s), (0, s)])]
    raise KeyError(character)

def _glyph_mask(index, character, width, height, scale):
    mask = Image.new('L', (width * scale, height * scale))
    draw = ImageDraw.Draw(mask)
    guard = 0.125
    top = 1 if index >= 2 else guard
    bottom = height - 1 if index == 3 else height - guard
    if not character.isalnum():

        def poly(points):
            draw.polygon([(round(x * scale), round(y * scale)) for x, y in points], fill=255)

        def rect(l, t, r, b):
            poly([(l, t), (r, t), (r, b), (l, b)])
        mid = height * 0.52
        dot = min(width - 2 * guard, 1.8 if index == 2 else 2.8)
        if character in '.,':
            rect(guard, height - dot - guard, width - guard, bottom)
            if character == ',':
                poly([(width - guard, height - dot), (width * 0.5, bottom), (guard, bottom), (width * 0.4, height - dot)])
        elif character == ':':
            rect(guard, height * 0.24, width - guard, height * 0.24 + dot)
            rect(guard, height * 0.69, width - guard, height * 0.69 + dot)
        elif character == "'":
            rect(guard, top, width - guard, top + height * 0.35)
        elif character in '-+':
            stroke = 1 if index == 2 else 3
            rect(guard, mid - stroke / 2, width - guard, mid + stroke / 2)
            if character == '+':
                rect(width / 2 - stroke / 2, height * 0.2, width / 2 + stroke / 2, height * 0.82)
        elif character == '/':
            poly([(width * 0.7, top), (width - guard, top), (width * 0.3, bottom), (guard, bottom)])
        elif character in '()':
            points = flatten_path('M100 0 C38 18 35 80 100 100 L57 100 C-19 72 -19 28 57 0 Z')
            if character == ')':
                points = [(100 - x, y) for x, y in points]
            poly([(guard + x / 100 * (width - 2 * guard), top + y / 100 * (bottom - top)) for x, y in points])
        elif character in '?¿':
            points = flatten_path('M0 25 C0 8 18 0 50 0 C82 0 100 12 100 31 C100 47 89 53 66 65 L66 75 L34 75 L34 57 C34 50 65 43 65 30 C65 22 60 18 50 18 C39 18 34 21 34 29 L0 29 Z')
            if character == '¿':
                points = [(100 - x, 100 - y) for x, y in points]
            poly([(guard + x / 100 * (width - 2 * guard), top + y / 100 * (bottom - top)) for x, y in points])
            y = top if character == '¿' else bottom - dot
            rect(width * 0.34, y, width * 0.66, y + dot)
        elif character == '&':
            for outer, holes in _master('8', 25):
                draw.polygon([(round((guard + x / 100 * (width - 2 * guard)) * scale), round((top + y / 100 * (bottom - top)) * scale)) for x, y in flatten_path(outer)], fill=255)
                for hole in holes:
                    draw.polygon([(round((guard + x / 100 * (width - 2 * guard)) * scale), round((top + y / 100 * (bottom - top)) * scale)) for x, y in flatten_path(hole)], fill=0)
            poly([(width * 0.2, height * 0.2), (width * 0.43, height * 0.2), (width - guard, bottom), (width * 0.72, bottom)])
        else:
            raise KeyError(character)
        return mask
    weight = 22 if index < 2 else 17 if index == 2 else 29
    shear = 0.31 * (bottom - top) if index == 3 else 0
    body = width - 2 * guard - shear

    def mapped(path):
        return [(round((guard + x / 100 * body + shear * (1 - y / 100)) * scale), round((top + y / 100 * (bottom - top)) * scale)) for x, y in flatten_path(path)]
    contours = _master(character, weight)
    if index == 2 and character == 'A':
        contours = [('M0 100 L0 26 C0 8 10 0 28 0 L72 0 C90 0 100 8 100 26 L100 100 L75 100 L75 57 L25 57 L25 100 Z', ['M25 17 L75 17 L75 40 L25 40 Z'])]
    for outer, holes in contours:
        draw.polygon(mapped(outer), fill=255)
        for hole in holes:
            draw.polygon(mapped(hole), fill=0)
    return mask

def menu_font_glyph(index, character, scale=4, antialias=2):
    """Render one original cell, with a transparent final-pixel safety border.

    OPTION/HELP originally touch cell boundaries. Removing one output texel
    after cell-local antialiasing gives their outlines a quarter-logical-pixel
    safety edge at the standard 4x scale. Metrics and atlas UVs stay unchanged;
    adjacent cells cannot contribute a line through linear texture sampling.
    """
    if scale < 1 or antialias < 1:
        raise ValueError('scale and antialias must be positive integers')
    _, _, width, height = MENU_FONTS[index]['glyphs'][character]
    high = scale * antialias
    mask = _glyph_mask(index, character, width, height, high)
    cell = Image.new('RGBA', mask.size, (255, 255, 255, 0))
    if index == 3:
        shift = max(1, round(high * 0.65))
        down = Image.new('L', mask.size)
        down.paste(mask, (shift, shift))
        up = Image.new('L', mask.size)
        up.paste(mask, (-shift, -shift))
        highlight = ImageChops.subtract(mask, down)
        lowlight = ImageChops.subtract(mask, up)
        draw = ImageDraw.Draw(cell)
        for row in range(cell.height):
            t = row / max(1, cell.height - 1)
            level = round(11 + 12 * (1 - t))
            draw.line((0, row, cell.width, row), fill=(level, level + 2, level + 5, 255))
        cell.paste(Image.new('RGBA', mask.size, (164, 171, 180, 255)), (0, 0), highlight)
        cell.paste(Image.new('RGBA', mask.size, (0, 0, 0, 255)), (0, 0), lowlight)
    cell.putalpha(mask)
    if antialias != 1:
        cell = cell.resize((width * scale, height * scale), Image.Resampling.LANCZOS)
    alpha = cell.getchannel('A')
    draw = ImageDraw.Draw(alpha)
    draw.rectangle((0, 0, cell.width - 1, cell.height - 1), outline=0, width=1)
    cell.putalpha(alpha)
    return cell

def menu_font_atlas(index, scale=4, antialias=2):
    """Pack each separately antialiased cell, including added parentheses."""
    font = MENU_FONTS[index]
    atlas = Image.new('RGBA', tuple((value * scale for value in font['atlas_size'])), (255, 255, 255, 0))
    for character, (x, y, _, _) in font['glyphs'].items():
        atlas.paste(menu_font_glyph(index, character, scale, antialias), (x * scale, y * scale))
    return atlas
