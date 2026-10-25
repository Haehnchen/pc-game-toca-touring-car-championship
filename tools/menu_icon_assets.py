#!/usr/bin/env python3
"""Export canonical menu references and import newly generated Mega WebP70 art.

Original/reference pixels are written only beneath ignored build/. Import only
crops equal sheet cells (or explicit auxiliary rectangles), fits without
distortion on transparent canvases and encodes.
"""

import argparse
from io import BytesIO
import json
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib

from PIL import Image, ImageOps

from git_game_data import configured_data_root

PROJECT = Path(__file__).resolve().parent.parent
ARCHIVE = 'data/frontend.bfp'
OUTPUT = PROJECT / 'assets/ui/mega/menu-icons'
PREVIEWS = PROJECT / 'build/menu-icons/previews'
BATCHES = ((3, 26, 36, 7, 1, 2), (6, 8, 9, 10, 13, 14),
           (16, 17, 18, 19, 20, 23), (25, 27, 28, 29, 30, 31),
           (32, 33, 34, 50, 51, 53), (57, 58, 59), tuple(range(256, 262)))
AUXILIARY = {
    'arrow-left-normal': (1, (154, 400, 24, 30)),
    'arrow-left-selected': (1, (64, 400, 24, 30)),
    'arrow-right-normal': (1, (91, 400, 24, 30)),
    'arrow-right-selected': (1, (1, 400, 24, 30)),
    'arrow-up-selected': (1, (1, 344, 30, 24)),
    'unavailable': (23, (0, 0, 88, 56)),
}


def crc32(data):
    return f'{zlib.crc32(data):08x}'


def relative(path):
    try:
        return str(path.resolve().relative_to(PROJECT))
    except ValueError:
        return str(path.resolve())


def canonical(archive_path=None):
    path = Path(archive_path) if archive_path is not None else configured_data_root() / 'data/frontend.bfp'
    data = path.read_bytes()
    count, directory_size, largest = struct.unpack_from('<III', data)
    if directory_size != count * 24 or 12 + directory_size > len(data):
        raise ValueError('Invalid canonical frontend archive directory')
    members, position = [], 0
    for index in range(count):
        offset, length = struct.unpack_from('<II', data, 12 + index * 24 + 16)
        if offset != position or 12 + directory_size + offset + length > len(data):
            raise ValueError('Invalid canonical frontend archive member')
        start = 12 + directory_size + offset
        members.append(data[start:start + length])
        position += length
    if 12 + directory_size + position != len(data) or max(map(len, members)) != largest:
        raise ValueError('Invalid canonical frontend archive payload')
    palette = list(Image.open(BytesIO(members[29])).getpalette()[:768])
    palette[0:3] = palette[155 * 3:155 * 3 + 3] = [0, 0, 0]
    return data, members, palette


def source_icon(icon, selected):
    group = (icon - 1) // 12
    member = {0: 0, 1: 1, 2: 2, 4: 3}[group]
    local = icon - group * 12
    row, remainder = divmod(local, 3)
    column = 2 if remainder == 0 else remainder - 1
    row = row * 2 - 2 + selected if remainder == 0 else row * 2 + selected
    return member, (column * 89 + 1, row * 57 + 1, 88, 56)


def indexed_source(member, rect, archive, members, palette):
    image = Image.open(BytesIO(members[member]))
    if image.mode != 'P':
        raise ValueError('Canonical menu reference must remain indexed')
    x, y, width, height = rect
    crop = image.crop((x, y, x + width, y + height))
    if x < 0 or y < 0 or x + width > image.width or y + height > image.height:
        raise ValueError('Canonical menu reference rectangle exceeds member')
    indices = crop.tobytes()
    crop.putpalette(palette)
    rgba = crop.convert('RGBA')
    rgba.putalpha(Image.frombytes('L', crop.size, bytes(0 if value == 0 else 255 for value in indices)))
    provenance = {'archive': str(ARCHIVE), 'archive_crc32': crc32(archive),
                  'member': member, 'member_crc32': crc32(members[member]),
                  'rect': list(rect), 'indices_crc32': crc32(indices),
                  'palette_rgb888_crc32': crc32(bytes(palette)), 'transparent_index': 0}
    return rgba, provenance


def native_references():
    """Run the same badge authoring code and corrected Audi palette as View."""
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    _, members, _ = canonical()
    with tempfile.TemporaryDirectory(prefix='menu-native-', dir=PREVIEWS.parent) as temporary:
        directory = Path(temporary)
        (directory / 'audi.bmp').write_bytes(members[29])
        source = directory / 'native.cpp'
        source.write_text('''#include "frontend/extras_icon.hpp"
#include <string>
#include <stdexcept>
int main(int, char** argv) {
    using namespace toca::frontend;
    Surface source(SDL_LoadBMP((std::string(argv[1])+"/audi.bmp").c_str()), SDL_DestroySurface);
    if (!source) throw std::runtime_error(SDL_GetError());
    auto* palette=SDL_GetSurfacePalette(source.get());
    const SDL_Color black{0,0,0,255};
    if (!palette || !SDL_SetPaletteColors(palette,&black,0,1) || !SDL_SetPaletteColors(palette,&black,155,1)) throw std::runtime_error(SDL_GetError());
    for(int symbol=0; symbol<6; ++symbol) for(int selected=0; selected<2; ++selected) {
        const auto icon=make_badge_icon(palette,bool(selected),BadgeSymbol(symbol));
        const auto path=std::string(argv[1])+"/native-"+std::to_string(256+symbol)+(selected?"-selected.bmp":"-normal.bmp");
        if (!SDL_SaveBMP(icon.get(),path.c_str())) throw std::runtime_error(SDL_GetError());
    }
}
''')
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True))
        executable = directory / 'native'
        subprocess.run(['c++', '-std=c++23', '-I' + str(PROJECT / 'src'), str(source), '-o', str(executable), *flags], check=True)
        subprocess.run([str(executable), str(directory)], check=True)
        for path in directory.glob('native-*.bmp'):
            image = Image.open(path)
            rgba = image.convert('RGBA')
            rgba.putalpha(Image.frombytes('L', image.size, bytes(0 if value == 0 else 255 for value in image.tobytes())))
            rgba.save(PREVIEWS / (path.stem + '.png'))


def sources(batch):
    archive, members, palette = canonical()
    result = []
    for icon in BATCHES[batch]:
        for selected, state in enumerate(('normal', 'selected')):
            if icon < 256:
                image, provenance = indexed_source(*source_icon(icon, selected), archive, members, palette)
            else:
                path = PREVIEWS / f'native-{icon}-{state}.png'
                image = Image.open(path).convert('RGBA') if path.exists() else None
                provenance = {'header': 'src/frontend/extras_icon.hpp',
                              'header_crc32': crc32((PROJECT / 'src/frontend/extras_icon.hpp').read_bytes()),
                              'symbol': icon - 256, 'selected': bool(selected),
                              'palette_archive': str(ARCHIVE), 'palette_archive_crc32': crc32(archive),
                              'palette_member': 29, 'palette_member_crc32': crc32(members[29]),
                              'palette_rgb888_crc32': crc32(bytes(palette)), 'transparent_index': 0}
            result.append((f'icon-{icon}-{state}', image, provenance, (88, 56)))
    return result


def export(batch):
    if batch == 6:
        native_references()
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    entries = sources(batch)
    sheet = Image.new('RGBA', (704, 224 * len(BATCHES[batch])))
    for index, (name, image, _, _) in enumerate(entries):
        image.save(PREVIEWS / (name + '.png'))
        sheet.paste(image.resize((352, 224), Image.Resampling.NEAREST), (index % 2 * 352, index // 2 * 224))
    path = PREVIEWS / f'batch-{batch}.png'
    sheet.save(path)
    print(relative(path))


def auxiliary_sources():
    archive, members, palette = canonical()
    return [(name, *indexed_source(member, rect, archive, members, palette), rect[2:])
            for name, (member, rect) in AUXILIARY.items()]


def fit_cell(cell, target):
    fitted = ImageOps.contain(cell, target, Image.Resampling.LANCZOS)
    canvas = Image.new('RGBA', target)
    canvas.alpha_composite(fitted, ((target[0] - fitted.width) // 2, (target[1] - fitted.height) // 2))
    return canvas


def import_artwork(args):
    generated = args.input.read_bytes()
    image = Image.open(BytesIO(generated)).convert('RGBA')
    minimum, maximum = image.getchannel('A').getextrema()
    if minimum == 255 or maximum == 0:
        raise ValueError('Generated artwork requires visible pixels and a transparent background')
    if args.aux_spec:
        archive, members, palette = canonical()
        entries, crops = [], []
        for spec in json.loads(args.aux_spec.read_text()):
            name = spec['name']
            member, rect = AUXILIARY[name]
            _, provenance = indexed_source(member, rect, archive, members, palette)
            size = tuple(spec[key] for key in ('width', 'height'))
            if size != rect[2:]:
                raise ValueError('Auxiliary size must retain original logical dimensions')
            entries.append((name, None, provenance, size))
            x, y, width, height = spec['rect']
            crops.append((x, y, x + width, y + height))
        batch_key = 'auxiliary'
    else:
        entries, crops = sources(args.batch), []
        rows = len(BATCHES[args.batch])
        for index in range(len(entries)):
            column, row = index % 2, index // 2
            crops.append((image.width * column // 2, image.height * row // rows,
                          image.width * (column + 1) // 2, image.height * (row + 1) // rows))
        if args.cells:
            rectangles = json.loads(args.cells.read_text())
            if len(rectangles) != len(entries):
                raise ValueError('Explicit cell count must match the batch normal/selected entries')
            crops = []
            for rect in rectangles:
                if len(rect) != 4 or any(type(value) is not int for value in rect):
                    raise ValueError('Explicit cells require integer [x,y,width,height] rectangles')
                x, y, width, height = rect
                crops.append((x, y, x + width, y + height))
        batch_key = str(args.batch)
    prepared = []
    for entry, crop in zip(entries, crops):
        if crop[0] < 0 or crop[1] < 0 or crop[2] > image.width or crop[3] > image.height or crop[2] <= crop[0] or crop[3] <= crop[1]:
            raise ValueError('Generated sheet crop exceeds image bounds')
        cell = image.crop(crop)
        minimum, maximum = cell.getchannel('A').getextrema()
        if minimum == 255 or maximum == 0:
            raise ValueError('Generated artwork cell requires visible pixels and a transparent background')
        prepared.append((entry, crop, cell))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for (name, _, _, size), _, cell in prepared:
        path = OUTPUT / (name + '.webp')
        target = tuple(value * 4 for value in size)
        fit_cell(cell, target).save(path, 'WEBP', quality=70, method=6)

    print(f'Imported {len(entries)} WebP70 files to {relative(OUTPUT)}')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('export', 'import', 'inventory'))
    parser.add_argument('--batch', type=int, choices=range(len(BATCHES)))
    parser.add_argument('--input', type=Path)
    parser.add_argument('--aux-spec', type=Path, help='JSON list of {name,width,height,rect:[x,y,w,h]}')
    parser.add_argument('--cells', type=Path, help='JSON [x,y,width,height] cells in batch normal/selected order')
    args = parser.parse_args()
    if args.command == 'import':
        if not args.input or (args.batch is None) == (args.aux_spec is None):
            parser.error('import requires --input and exactly one of --batch/--aux-spec')
        if args.cells and args.aux_spec:
            parser.error('--cells applies to --batch; --aux-spec already defines explicit rectangles')
        import_artwork(args)
    elif args.command == 'export':
        for batch in range(len(BATCHES)) if args.batch is None else (args.batch,):
            export(batch)
        if args.batch is None:
            for name, image, _, _ in auxiliary_sources():
                image.save(PREVIEWS / (name + '.png'))
    else:
        result = {str(batch): [{'name': name, 'source': provenance, 'logical_dimensions': list(size)}
                               for name, _, provenance, size in sources(batch)] for batch in range(len(BATCHES))}
        result['auxiliary'] = [{'name': name, 'source': provenance, 'logical_dimensions': list(size)}
                               for name, _, provenance, size in auxiliary_sources()]
        print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
