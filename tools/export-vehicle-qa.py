#!/usr/bin/env python3
"""Capture native vehicle QA privately and publish a local WebP70 comparison."""

import argparse
import html
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from vehicle_manifest import DEFAULT_MANIFEST, load_manifest
from vehicle_model import PROJECT, crc32


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--key', required=True)
    parser.add_argument('--livery', choices=('first', 'second', 'both'), default='both')
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument('--build-dir', type=Path, default=PROJECT / 'build/dev')
    parser.add_argument('--data-root', type=Path, help='Unpacked Git game-data root; defaults to BUILD_DIR/generated/original-data')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    entry = next((entry for entry in load_manifest(args.manifest)['vehicles'] if entry['key'] == args.key), None)
    if entry is None:
        parser.error('Unknown vehicle key')
    output = args.output.resolve()
    if not output.is_relative_to(PROJECT / 'build'):
        parser.error('Prepare the complete review under ignored build/ before exporting it')
    output.mkdir(parents=True, exist_ok=False)
    build = args.build_dir.resolve()
    reference = entry['mode'] == 'reference'
    model = build / ('generated/vehicle-reference' if reference else 'generated/mega') / Path(entry['output']).name
    data_root = args.data_root.resolve() if args.data_root else build / 'generated/original-data'
    command = [str(build / 'toca-vehicle-model-tests'), str(data_root), str(model),
               '--render-id', str(entry['render_id']), '--qa', '--lod', '--livery', args.livery]
    if reference:
        command.append('--reference')
    subprocess.run([sys.executable, str(PROJECT / 'tools/run-isolated.py'), '--name', args.key,
                    '--artifacts', str(output / 'capture'), '--display', '--screen', '2560x1440x24',
                    '--timeout', '180', '--', *command], check=True)
    work = next((output / 'capture').glob('*/work'))
    log = (work.parent / 'application.log').read_text()
    timing = re.search(r'load_us original=(\d+) mega=(\d+)', log)
    counts = re.search(r'model_counts original=(\d+),(\d+),(\d+) mega=(\d+),(\d+),(\d+)', log)
    converter = shutil.which('magick') or shutil.which('convert')
    if not converter:
        raise RuntimeError('ImageMagick with WebP support required')
    files = []
    for source in sorted(work.glob('*.png')):
        destination = output / (source.stem + '.webp')
        subprocess.run([converter, str(source), '-strip', '-define', 'webp:lossless=false',
                        '-define', 'webp:method=6', '-define', 'webp:alpha-quality=100',
                        '-quality', '70', str(destination)], check=True)
        data = destination.read_bytes()
        files.append({'file': destination.name, 'bytes': len(data), 'crc32': crc32(data)})
    drivers = ('first', 'second') if args.livery == 'both' else (args.livery,)
    report = {'key': entry['key'], 'render_id': entry['render_id'], 'physical_id': entry['physical_id'],
              'liveries': list(drivers),
              'model_bytes': model.stat().st_size, 'model_crc32': crc32(model.read_bytes()),
              'source': entry['source'], 'resolution': [2560, 1440], 'webp_quality': 70,
              'capture': 'Native SceneView, eight matched views per driver, private Xvfb, muted',
              'lod_review': 'Actual rival selector at 20/60/100 units: Mega HIGH, pixel-identical original MEDIUM/LOW with matched Mega sampling; projection scaled for inspection',
              'visual_review': 'pending', 'files': files}
    if timing:
        report['load_microseconds'] = dict(zip(('original', 'mega'), map(int, timing.groups())))
    if counts:
        values = list(map(int, counts.groups()))
        report['model_counts'] = {label: dict(zip(('visible_triangles', 'stored_triangles', 'vertices'), values[start:start + 3]))
                                  for label, start in (('original', 0), ('mega', 3))}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    title = html.escape(entry['key'].capitalize() + ' – Mega Fahrzeugabnahme')
    prefix = 'car-' + str(entry['render_id']) + '-'
    overview_files = {f'{prefix}{driver}-{kind}-{angle}.webp'
                      for driver in drivers for kind in ('original', 'mega')
                      for angle in range(0, 360, 45)}
    details = [row['file'] for row in files if row['file'] not in overview_files]
    detail_html = ''.join('<a href="' + name + '"><img loading="lazy" src="' + name + '"><span>' + html.escape(name.removeprefix(prefix).removesuffix('.webp')) + '</span></a>' for name in details)
    driver_options = ''.join(f'<option value="{driver}">{label}</option>'
                             for driver, label in (('first', 'Erster Fahrer'), ('second', 'Zweiter Fahrer'))
                             if driver in drivers)
    page = '''<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>TITLE</title><style>body{font:16px system-ui;background:#171b23;color:#edf0f4;margin:24px}h1{font-size:25px}button,select{font:inherit;padding:8px;margin:4px;background:#303949;color:inherit;border:1px solid #708198;border-radius:5px}a{color:#9dd6ff}#comparison{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}img{width:100%;display:block}figcaption{padding:8px}#details{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:12px}#details a{background:#242c37;text-decoration:none}#details span{display:block;padding:8px}button[aria-pressed=true]{background:#0e6890}@media(max-width:800px){#comparison{grid-template-columns:1fr}}</style>
<h1>TITLE</h1><p>Native Aufnahmen · 2560 × 1440 · gleiche Kamera und Beleuchtung · WebP70</p>
<label>Fahrer: <select id="driver">DRIVERS</select></label>
<nav id="angles" aria-label="360-Grad-Ansichten"></nav><div id="comparison"><figure><a id="oldlink"><img id="old" alt="Originalmodell"></a><figcaption>Original</figcaption></figure><figure><a id="newlink"><img id="new" alt="Mega-Modell"></a><figcaption>Mega</figcaption></figure></div>
<p><a href="report.json">Modell- und Aufnahmebericht</a> · <a href="CRC32SUMS">CRC32-Prüfsummen</a></p><h2>Bewegung, Schäden und Details</h2><div id="details">DETAILS</div>
<script>const prefix=PREFIX;let angle=0;const driver=document.querySelector('#driver');function show(){for(const [id,kind] of [['old','original'],['new','mega']]){const name=prefix+driver.value+'-'+kind+'-'+angle+'.webp';document.getElementById(id).src=name;document.getElementById(id+'link').href=name;}for(const button of document.querySelectorAll('button'))button.setAttribute('aria-pressed',button.dataset.angle==angle)}for(let a=0;a<360;a+=45){const button=document.createElement('button');button.textContent=a+'°';button.dataset.angle=a;button.onclick=()=>{angle=a;show()};document.querySelector('#angles').append(button)}driver.onchange=show;show();</script></html>'''
    page = page.replace('TITLE', title).replace('DETAILS', detail_html).replace('DRIVERS', driver_options).replace('PREFIX', json.dumps(prefix))
    (output / 'index.html').write_text(page)
    checks = [f'{crc32(path.read_bytes())}  {path.name}' for path in sorted(output.iterdir()) if path.is_file()]
    (output / 'CRC32SUMS').write_text('\n'.join(checks) + '\n')
    print(output / 'index.html')


if __name__ == '__main__':
    main()
