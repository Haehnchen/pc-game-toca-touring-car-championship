#!/usr/bin/env python3
"""Manually compose Mega LOADHIGH bitmaps from separately generated artwork.

Builds copy the finished WebPs. Canonical bitmaps supply identity, letter
positions and geometric circuit/arrow coordinates only, never output pixels.
The terrain and shared header are newly generated imagegen layers. Embedded
captions are rasterized from continuous geometric letter masters; dynamic
statistics remain a separate bitmap font in the runtime. Circuit geometry is
registered and centered on the visible terrain deck before drawing.
"""

import argparse
from collections import defaultdict
from io import BytesIO
from pathlib import Path
import runpy
import zlib

import numpy as np
from PIL import Image, ImageDraw

from git_game_data import configured_data_root
from hud_font_geometry import flatten_path
from hud_menu_font_geometry import _master


ROOT = Path(__file__).resolve().parents[1]
READ_BFP = runpy.run_path(str(ROOT / 'tools/extract-assets.py'))['read_bfp']
FACTOR = 4
TRACKS = [
    ('donington', 3, 'b25b18ed', ['DONINGTON', 'PARK (GP)'], 98),
    ('silverstone', 6, '05763141', ['SILVERSTONE', 'INTERNATIONAL'], 116),
    ('thruxton', 8, 'cc51de50', ['THRUXTON'], 100),
    ('brands-hatch', 1, 'dc6eb2a9', ['BRANDS HATCH', 'INDY'], 98),
    ('oulton-park', 5, '6a940289', ['OULTON PARK'], 98),
    ('donington-short', 9, '2dccf331', ['DONINGTON', 'PARK (SHORT)'], 90),
    ('croft', 2, '5ed129e7', ['CROFT'], 100),
    ('knockhill', 4, 'b776c64e', ['KNOCKHILL'], 95),
    ('snetterton', 7, '3f18e2c7', ['SNETTERTON'], 100),
    ('lavaland', 10, '87272ceb', [], 0),
]
RELIEF_BOX = (24, 312, 288, 480)
# Deck corners (left, back, right, front), local to RELIEF_BOX: canonical
# LOADHIGH followed by the fitted authored relief. The front corner is the
# gray/brown deck edge, not the bottom of the relief's sidewall bounding box.
RELIEF_SURFACES = {
    'donington': (((6, 47), (134, 4), (256, 51), (121, 137)),
                  ((6, 50.5), (134.5, 4), (256.5, 56.5), (123, 139))),
    'silverstone': (((7, 56), (134, 7), (253, 65), (121, 146)),
                    ((7, 57.75), (126.5, 7), (253.75, 67.25), (121, 144))),
    'thruxton': (((7, 50), (134, 7), (255, 57), (122, 140)),
                 ((7, 51.25), (133.75, 7), (255.75, 60.25), (121, 140))),
    'brands-hatch': (((5, 40), (147, 6), (255, 61), (122, 131)),
                     ((5.25, 42.25), (144.25, 6), (255.5, 62.5), (122, 132))),
    'oulton-park': (((7, 51), (134, 7), (256, 57), (122, 140)),
                    ((7.25, 46.5), (132.25, 7), (256.75, 53.25), (123, 122))),
    'donington-short': (((7, 51), (135, 8), (257, 54), (122, 140)),
                        ((7.25, 46.5), (135.5, 8), (257.5, 50), (124, 120))),
    'croft': (((5, 48), (133, 6), (255, 54), (120, 140)),
              ((5, 48.25), (132, 6), (255.75, 58.25), (123, 139))),
    'knockhill': (((7, 60), (131, 7), (249, 67), (119, 137)),
                  ((7, 53.25), (127.25, 7), (249.75, 60.75), (118, 121))),
    'snetterton': (((8, 53), (135, 8), (257, 60), (122, 142)),
                   ((8, 54.5), (135.25, 8), (257.75, 62.25), (124, 140))),
}
# Clean geometric heads and bent stems measured in the source miniatures.
# These replace the low-resolution red edge staircase with fresh vector ink.
ARROW_CONTROLS = {
    'donington': ([(80, 67), (90, 63), (90, 69)], [(88, 66), (97, 63), (101, 63), (104, 66)]),
    'silverstone': ([(170, 91), (175, 79), (184, 85)], [(177, 85), (184, 79), (199, 83)]),
    'thruxton': ([(85, 82), (101, 84), (94, 91)], [(93, 96), (98, 86), (94, 85)]),
    'brands-hatch': ([(101, 20), (97, 29), (105, 28)], [(101, 27), (103, 36), (112, 42)]),
    'oulton-park': ([(150, 75), (153, 64), (162, 69)], [(155, 69), (161, 63), (177, 69)]),
    'donington-short': ([(81, 71), (91, 67), (91, 73)], [(89, 70), (98, 67), (102, 67), (105, 70)]),
    'croft': ([(143, 36), (144, 45), (152, 42)], [(148, 41), (153, 44), (156, 47)]),
    'knockhill': ([(136, 45), (124, 45), (127, 51)], [(107, 45), (117, 51), (128, 48)]),
    'snetterton': ([(150, 37), (139, 40), (146, 46)], [(125, 40), (132, 48), (137, 47), (145, 42)]),
}


def crc32(data):
    return f'{zlib.crc32(data):08x}'


def simplify(points, tolerance=1.2):
    """Geometric Ramer-Douglas-Peucker reduction, without raster enlargement."""
    if len(points) < 3:
        return points
    a, b = np.asarray(points[0], float), np.asarray(points[-1], float)
    data = np.asarray(points, float)
    delta = b - a
    length = float(delta @ delta)
    if length:
        position = np.clip((data - a) @ delta / length, 0, 1)
        distances = np.linalg.norm(data - a - position[:, None] * delta, axis=1)
    else:
        distances = np.linalg.norm(data - a, axis=1)
    index = int(np.argmax(distances))
    if distances[index] <= tolerance:
        return [points[0], points[-1]]
    return simplify(points[:index + 1], tolerance)[:-1] + simplify(points[index:], tolerance)


def thin(mask):
    """Recover a centerline; final ink is drawn afresh as smooth vector strokes."""
    pixels = np.pad(mask.astype(np.uint8), 1)
    for _ in range(32):
        changed = False
        for phase in (0, 1):
            p = [pixels[:-2, 1:-1], pixels[:-2, 2:], pixels[1:-1, 2:], pixels[2:, 2:],
                 pixels[2:, 1:-1], pixels[2:, :-2], pixels[1:-1, :-2], pixels[:-2, :-2]]
            count = sum(p)
            transitions = sum((p[i] == 0) & (p[(i + 1) % 8] != 0) for i in range(8))
            if phase == 0:
                gate = (p[0] * p[2] * p[4] == 0) & (p[2] * p[4] * p[6] == 0)
            else:
                gate = (p[0] * p[2] * p[6] == 0) & (p[0] * p[4] * p[6] == 0)
            remove = (pixels[1:-1, 1:-1] != 0) & (count >= 2) & (count <= 6) & (transitions == 1) & gate
            if remove.any():
                pixels[1:-1, 1:-1][remove] = 0
                changed = True
        if not changed:
            break
    return pixels[1:-1, 1:-1] != 0


def center_paths(mask):
    coordinates = {tuple(p[::-1]) for p in np.argwhere(thin(mask))}
    graph = {}
    for x, y in coordinates:
        neighbors = []
        for dx, dy in ((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)):
            candidate = (x + dx, y + dy)
            if candidate not in coordinates:
                continue
            if dx and dy and ((x + dx, y) in coordinates or (x, y + dy) in coordinates):
                continue
            neighbors.append(candidate)
        graph[(x, y)] = sorted(neighbors)
    used, paths = set(), []
    starts = sorted(graph, key=lambda p: (len(graph[p]) == 2, p))
    for start in starts:
        for following in graph[start]:
            if frozenset((start, following)) in used:
                continue
            path = [start]
            previous, current = start, following
            while True:
                used.add(frozenset((previous, current)))
                path.append(current)
                choices = [p for p in graph[current] if p != previous and frozenset((current, p)) not in used]
                if len(graph[current]) != 2 or not choices:
                    break
                previous, current = current, choices[0]
            if len(path) >= 3:
                paths.append(simplify(path))
    return paths


def smooth_path(points):
    """Centrally sampled cubic interpolation, bounded to adjacent waypoints."""
    if len(points) < 3:
        return points
    closed = points[0] == points[-1]
    padded = [points[-2] if closed else points[0], *points, points[1] if closed else points[-1]]
    result = []
    for index in range(1, len(padded) - 2):
        a, b, c, d = (np.asarray(padded[index + offset], float) for offset in (-1, 0, 1, 2))
        steps = max(4, int(np.linalg.norm(c - b) * 3))
        for t in np.linspace(0, 1, steps, endpoint=False):
            value = .5 * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (-a + 3 * b - 3 * c + d) * t * t * t)
            result.append(tuple(value))
    result.append(tuple(points[-1]))
    return result


def contours(mask):
    """Simplified closed marker outlines; preserve pointed arrowheads."""
    edges = defaultdict(list)
    for y, x in np.argwhere(mask):
        for dx, dy, a, b in ((0, -1, (x, y), (x + 1, y)), (1, 0, (x + 1, y), (x + 1, y + 1)),
                             (0, 1, (x + 1, y + 1), (x, y + 1)), (-1, 0, (x, y + 1), (x, y))):
            xx, yy = x + dx, y + dy
            if yy < 0 or xx < 0 or yy >= mask.shape[0] or xx >= mask.shape[1] or not mask[yy, xx]:
                edges[a].append(b)
    result = []
    while edges:
        first = next(iter(edges))
        current, path = first, [first]
        while current in edges:
            following = edges[current].pop()
            if not edges[current]:
                del edges[current]
            path.append(following)
            current = following
            if current == first:
                break
        if len(path) >= 7:
            # Split the closed ring before reduction, otherwise endpoints coincide.
            middle = len(path) // 2
            result.append(simplify(path[:middle + 1], .7)[:-1] + simplify(path[middle:], .7))
    return result


def route_geometry(source, bottom):
    data = np.asarray(source.crop(RELIEF_BOX), dtype=np.int16)
    red, green, blue = (data[:, :, i] for i in range(3))
    yellow = (red > 90) & (green > 85) & (blue < 60) & (abs(red - green) < 48) & (green > blue * 2)
    yellow[bottom:] = False
    marker = (red > 80) & (red > green * 1.8) & (red > blue * 1.8)
    marker[bottom:] = False
    paths = center_paths(yellow)
    # Palette antialiasing can interrupt the narrow yellow source line. Join
    # adjacent geometric endpoints before drawing a continuous fresh stroke.
    while len(paths) > 1:
        choices = []
        for i, left in enumerate(paths):
            for j in range(i + 1, len(paths)):
                for a in (0, -1):
                    for b in (0, -1):
                        distance = np.linalg.norm(np.asarray(left[a]) - paths[j][b])
                        if distance <= 16:
                            choices.append((distance, i, j, a, b))
        if not choices:
            break
        _, i, j, a, b = min(choices)
        left = paths[i] if a == -1 else paths[i][::-1]
        right = paths[j] if b == 0 else paths[j][::-1]
        paths[i] = simplify(left + right)
        del paths[j]
    for points in paths:
        distance = np.linalg.norm(np.asarray(points[0]) - points[-1])
        if 0 < distance <= 16:
            points.append(points[0])
    return paths, contours(marker)


def homography(source, target):
    """Map four corresponding deck corners, excluding the relief's sidewalls."""
    equations, values = [], []
    for (x, y), (u, v) in zip(source, target, strict=True):
        equations.extend(((x, y, 1, 0, 0, 0, -u * x, -u * y),
                          (0, 0, 0, x, y, 1, -v * x, -v * y)))
        values.extend((u, v))
    return np.r_[np.linalg.solve(equations, values), 1].reshape(3, 3)


def project_points(points, transform):
    projected = np.column_stack((points, np.ones(len(points)))) @ transform.T
    return projected[:, :2] / projected[:, 2:]


def place_route(name, paths, arrows):
    """Register and visually center the circuit and marker inside the deck."""
    original, authored = RELIEF_SURFACES[name]
    to_artwork = homography(original, authored)
    # Smooth in source image coordinates first. Smoothing projected controls
    # would change the curve and could push its ink back over an authored edge.
    geometry = [project_points(smooth_path(path), to_artwork) for path in paths]
    geometry.extend(project_points(outline, to_artwork) for outline in arrows)
    points = np.vstack(geometry)
    lower, upper = points.min(axis=0), points.max(axis=0)
    route_center = (lower + upper) / 2
    deck = np.asarray(authored, float)
    following = np.roll(deck, -1, axis=0)
    areas = deck[:, 0] * following[:, 1] - following[:, 0] * deck[:, 1]
    center = ((deck + following) * areas[:, None]).sum(axis=0) / (3 * areas.sum())
    # A raised rear corner makes the projective square's center look too far
    # back. Use the visible deck's area center and fit against every deck edge.
    # Keep 20% of each center-to-edge distance clear, including the arrow.
    offsets = points - route_center
    limits = []
    for start, edge in zip(deck, following - deck, strict=True):
        distance = edge[0] * (center[1] - start[1]) - edge[1] * (center[0] - start[0])
        nearest = np.min(edge[0] * offsets[:, 1] - edge[1] * offsets[:, 0])
        if nearest < 0:
            limits.append(.80 * distance / -nearest)
    scale = min(limits)
    placed = [list(map(tuple, (points - route_center) * scale + center)) for points in geometry]
    return placed[:len(paths)], placed[len(paths):]


def draw_route(canvas, paths, arrows):
    scale = FACTOR * 3
    layer = Image.new('RGBA', (264 * scale, 168 * scale))
    draw = ImageDraw.Draw(layer)
    for path in paths:
        points = [(x * scale, y * scale) for x, y in path]
        draw.line(points, fill=(187, 179, 18, 255), width=round(1.05 * scale), joint='curve')
    for outline in arrows:
        draw.polygon([(x * scale, y * scale) for x, y in outline], fill=(231, 25, 8, 255))
    layer = layer.resize((264 * FACTOR, 168 * FACTOR), Image.Resampling.LANCZOS)
    canvas.paste(layer, (24 * FACTOR, 312 * FACTOR), layer)


def marker_shapes(name):
    head, stem = ARROW_CONTROLS[name]
    shapes = [head]
    for first, second in zip(stem, stem[1:]):
        a, b = np.asarray(first, float), np.asarray(second, float)
        delta = b - a
        normal = np.asarray((-delta[1], delta[0])) / np.linalg.norm(delta) * .65
        shapes.append([tuple(a + normal), tuple(b + normal), tuple(b - normal), tuple(a - normal)])
    for x, y in stem[1:-1]:
        shapes.append([(x + np.cos(t) * .65, y + np.sin(t) * .65) for t in np.linspace(0, 2 * np.pi, 12, endpoint=False)])
    return shapes


def letter(character, width, height):
    scale = FACTOR * 3
    mask = Image.new('L', (width * scale, height * scale))
    draw = ImageDraw.Draw(mask)
    if character == '.':
        shapes = [('M0 0 L100 0 L100 100 L0 100 Z', [])]
    elif character in '()':
        left = character == '('
        points = []
        for t in np.linspace(-1.1, 1.1, 50):
            x = (1 - np.cos(t)) / (1 - np.cos(1.1)) * 68
            if not left:
                x = 100 - x
            points.append((x * width * scale / 100, (t / 2.2 + .5) * height * scale))
        draw.line(points, fill=255, width=max(1, round(width * scale * .32)), joint='curve')
        return mask.resize((width * FACTOR, height * FACTOR), Image.Resampling.LANCZOS)
    else:
        shapes = _master(character, 21)
    for outer, holes in shapes:
        part = Image.new('L', mask.size)
        part_draw = ImageDraw.Draw(part)
        for path, color in [(outer, 255), *((hole, 0) for hole in holes)]:
            part_draw.polygon([(x * width * scale / 100, y * height * scale / 100) for x, y in flatten_path(path)], fill=color)
        mask = Image.fromarray(np.maximum(np.asarray(mask), np.asarray(part)))
    return mask.resize((width * FACTOR, height * FACTOR), Image.Resampling.LANCZOS)


def caption(canvas, source, text, box):
    data = np.asarray(source.crop(box), dtype=np.int16)
    mask = (data[:, :, 2] > data[:, :, 0] * 2) & (data[:, :, 2] > data[:, :, 1] * 1.4) & (data[:, :, 1] > 30)
    occupied = mask.any(axis=0)
    boundaries = np.flatnonzero(np.diff(np.r_[False, occupied, False]))
    cells = list(zip(boundaries[::2], boundaries[1::2]))
    # A/T and T/T touch along a single authored scanline in these three words.
    # Retain the independently measured letter boundaries instead of treating
    # the joined ink as one glyph or changing the original word spacing.
    touching = {'INTERNATIONAL': (145, 173, 194), 'BRANDS HATCH': (204, 232, 252),
                'SNETTERTON': (134, 156, 178)}
    if text in touching:
        left, split, right = touching[text]
        cell = cells.index((left, right))
        cells[cell:cell + 1] = [(left, split), (split, right)]
    characters = text.replace(' ', '')
    if len(cells) != len(characters):
        raise ValueError(f'Caption cell count differs for {text}: {len(cells)}')
    records = []
    for character, (left, right) in zip(characters, cells, strict=True):
        ys = np.flatnonzero(mask[:, left:right].any(axis=1))
        top, bottom = int(ys[0]), int(ys[-1]) + 1
        bounds = [int(left + box[0]), top + box[1], int(right - left), bottom - top]
        ink = letter(character, bounds[2], bounds[3])
        canvas.paste((8, 82, 173), (bounds[0] * FACTOR, bounds[1] * FACTOR), ink)
        records.append({'character': character, 'xywh': bounds})
    return records


def generated_image(path):
    image = Image.open(path).convert('RGB')
    values = np.asarray(image).copy()
    values[np.max(values, axis=2) < 5] = 0
    return Image.fromarray(values)


def compose(input_dir, output_dir, reference_dir):
    archive = (reference_dir / 'data/loadhigh.bfp').read_bytes()
    if crc32(archive) != 'af7b3a1d':
        raise ValueError('Canonical LOADHIGH identity differs')
    entries = READ_BFP(archive)
    header_path = input_dir / 'header.png'
    header = generated_image(header_path).resize((640 * FACTOR, 233 * FACTOR), Image.Resampling.LANCZOS)
    header_values = np.asarray(header).copy()
    header_values[72 * FACTOR:222 * FACTOR][np.max(header_values[72 * FACTOR:222 * FACTOR], axis=2) < 12] = 0
    header = Image.fromarray(header_values)
    outputs = {}
    for name, member, checksum, labels, route_bottom in TRACKS:
        _, _, offset, size = entries[member]
        raw = archive[offset:offset + size]
        if crc32(raw) != checksum:
            raise ValueError(f'Canonical loading image differs: {name}')
        source = Image.open(BytesIO(raw)).convert('RGB')
        canvas = Image.new('RGB', (640 * FACTOR, 480 * FACTOR))
        canvas.paste(header, (0, 0))
        caption(canvas, source, 'LOADING..', (24, 240, 294, 282))
        # The original progress outline is embedded; only its yellow fill is live.
        draw = ImageDraw.Draw(canvas)
        draw.rectangle((31 * FACTOR, 288 * FACTOR, 290 * FACTOR - 1, 305 * FACTOR - 1), outline=(255, 0, 0), width=FACTOR)
        if name != 'lavaland':
            path = input_dir / f'{name}-relief.png'
            relief = generated_image(path)
            source_relief = np.asarray(source.crop(RELIEF_BOX))
            ys, xs = np.where(np.max(source_relief, axis=2) > 20)
            target = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
            values = np.asarray(relief)
            gy, gx = np.where(np.max(values, axis=2) > 20)
            cropped = relief.crop((int(gx.min()), int(gy.min()), int(gx.max()) + 1, int(gy.max()) + 1))
            fitted = cropped.resize(((target[2] - target[0]) * FACTOR, (target[3] - target[1]) * FACTOR), Image.Resampling.LANCZOS)
            canvas.paste(fitted, ((24 + target[0]) * FACTOR, (312 + target[1]) * FACTOR))
            paths, _ = route_geometry(source, route_bottom)
            arrows = marker_shapes(name)
            paths, arrows = place_route(name, paths, arrows)
            draw_route(canvas, paths, arrows)
            for index, label in enumerate(labels):
                caption(canvas, source, label, (310, 242 + index * 24, 618, 264 + index * 24))
        else:
            path = input_dir / 'lavaland-overlay.png'
            generated = Image.open(path).convert('RGBA')
            # Keep the generated upper decorations at source canvas positions;
            # fit the independently cut burnt miniature to its original box.
            upper = generated.copy()
            ImageDraw.Draw(upper).rectangle((0, round(generated.height * .60), round(generated.width * .48), generated.height), fill=(0, 0, 0, 0))
            upper = upper.resize(canvas.size, Image.Resampling.LANCZOS)
            canvas.paste(upper, (0, 0), upper)
            burn = generated.crop((0, round(generated.height * .60), round(generated.width * .48), generated.height))
            burn = burn.crop(burn.getchannel('A').getbbox()).resize((251 * FACTOR, 148 * FACTOR), Image.Resampling.LANCZOS)
            canvas.paste(burn, (30 * FACTOR, 317 * FACTOR), burn)
        encoded = BytesIO()
        canvas.save(encoded, 'WEBP', quality=70, method=6)
        outputs[f'{name}.webp'] = encoded.getvalue()
    # Clean shared decoration only: the header's TOCA logo never enters margins.
    sides = Image.new('RGB', (640 * FACTOR, 480 * FACTOR))
    for top, bottom in ((0, 72), (222, 233)):
        sides.paste(header.crop((0, top * FACTOR, 640 * FACTOR, bottom * FACTOR)), (0, top * FACTOR))
    values = np.asarray(sides).copy()
    original = np.asarray(header)
    for x in range(8 * FACTOR, 160 * FACTOR):
        weight = min(x - 8 * FACTOR, 16 * FACTOR)
        values[:72 * FACTOR, x] = ((original[:72 * FACTOR, x].astype(int) * (16 * FACTOR - weight)
                                  + original[:72 * FACTOR, x + 242 * FACTOR].astype(int) * weight + 8 * FACTOR)
                                 // (16 * FACTOR)).astype(np.uint8)
    sides = Image.fromarray(values)
    encoded = BytesIO()
    sides.save(encoded, 'WEBP', quality=70, method=6)
    outputs['sides.webp'] = encoded.getvalue()
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        (output_dir / name).write_bytes(data)
    return list(outputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, default=ROOT / 'build/loading-artwork')
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/ui/mega/loading')
    parser.add_argument('--data-root', type=Path, help='Unpacked Git game-data root; defaults to TOCA_DATA_ROOT')
    args = parser.parse_args()
    if args.output.resolve().is_relative_to((ROOT / 'references').resolve()):
        parser.error('Artwork output must remain outside references')
    result = compose(args.inputs, args.output, args.data_root or configured_data_root())
    print(f'Composed {len(result)} Mega loading bitmaps as WebP70/method6')


if __name__ == '__main__':
    main()
