# Scene model completion records

Current source contracts: [scene-models](../scene-models.md); active rollout and texture tasks:
[scenery plan](../mega-scenery-plan.md).

## Scenery baseline

Initial pass: 41 images (615,486 bytes) covering 147 canonical texture identities and 57 GLB
paths; added geometry: 49 adverts, 950 stacks (2,662 tyres), 1,002 rails. Fixed afterwards:
advert backing depth conflict, blank LDHL UV regions. Generated/bundled caches contain only
approved overrides. Per-file data comes from the generated scene manifest.

## Donington buildings

Each package compares original, authored images on source geometry and full Mega using the
bundled GP/Short resources. Source prefixes, node/sector/contact data and placements remain;
shared images never imply another building's completed geometry.

| Package | Technical result | Provenance |
| --- | --- | --- |
| Dunlop materials | One continuous tyre arch inverse-packed into both canonical triangle maps closes the N seam; source clamps/unequal crown UVs, blank shared support region, arrow/tread islands remain. | [generation](../../assets/models/mega/texture-generation.json) |
| Dunlop geometry | GP 22/59, Short 21/61 share a generator and five slots; rounded tyre/shoulder depth, closed supports keep the source passage; N-seam mean difference < 12/255. | [test](../../tests/tools/test_mega_dunlop.py) |
| Barn | Four placements; four 512×256 images, 54,780 bytes; roof edges, gable, doorway depth keep the source form. | [generation](../../assets/models/mega/donington-barn-generation.json) |
| Grandstand | Source 136V/80T, six images, four bindings; source-projected fascia handles unequal UV maps; source-BMP-qualified COM9/GSD9/GSD4 sharing. | [generation](../../assets/models/mega/donington-grandstand-generation.json) |
| CBO4 | Source 59V/30T, three new COMA/COM4/PIT3 images plus shared COM9/GSD9; closed twofold-sign reverse, source-atlas frames. | [generation](../../assets/models/mega/donington-cbo4-generation.json) |
| CBO5 | Source 60V/32T, all five images reused; four-face sign reverse, roof/base differences. | [test](../../tests/tools/test_mega_cbo.py) |
| CBO6 | Source 63V/34T; PIT3 end/GSD9 rear triangles original; closed four-triangle-sign reverse; no new images. | [test](../../tests/tools/test_mega_cbo.py) |
| CBO1 | Source 84V/54T, six source-atlas images keep Shell/Oils/HELIX/ADVANCE; exact-BMP STB6 sharing. | [generation](../../assets/models/mega/donington-cbo1-generation.json) |
| CBO3 | Simple edges/ridges, six window groups, sign back form 476T; thin COM4 fascia excluded; all five images reused. | [test](../../tests/tools/test_mega_cbo.py) |

## Roadside baseline

Inventory before near-tree/woodland/building work: all nine TREES resources kept 1926 source
triangles and 11/15 authored material slots; OBJECTS 65/1090 covered slots, FENCE 38/97 (per-track
slots, not distinct images or current coverage). Near trees and most people/service vehicles/
buildings stayed original; only selected tyres/steel rails had detail. Shared labels require exact
geometry/UV/material comparison; advert frames do not complete the supporting building.
Current inventory: [inventory-scenery.py](../../tools/inventory-scenery.py); vegetation/woodland:
[scenery record](scenery.md).

## HOR models

Six moving/five helicopter paths share six encoded payloads. 32 faithful redraws plus the
dedicated opaque PSP skin total 369,588 bytes (initial combined scenery/HOR count: 73 images/179
canonical identities across 68 GLB paths). The PSP atlas is embedded only in the two Donington
moving outputs; the source material prefix remains for the texture-only comparison; the original
PSP1 bank qualifies palette 255 CRC32 keys consistently in builder/runtime. A 193 actor-frame
comparison preserves route, visibility, weather, rotor phase, camera state; corrected PSP opaque
coverage, helicopter tail paint/open rotor ring, AIRMARK UV-split lettering.

Wing correction: existing root airfoil sections extend into the supporting body centerline; span,
bounds, paint fixed; four fixed-wing bodies add 96T each; actual-mesh containment of 48 wing/tail
samples (37 failed beforehand).

## Original consumer acceptance

`scene-reference-models` checks hierarchy, local/rival LOD, material/ambient colors, fixed weather
lighting, affine horizon, raw static bake; canonical initial Honda follow views check selected
car, scenery, horizon, loading pixels on all ten tracks. Source light: Donington (50,100,75),
Silverstone (100,200,−40), Thruxton (75,100,75), Brands (−60,100,−45); midpoint reciprocal
arithmetic, weather tables, remaining track constants:
[scene resources](../scene-resources.md#remaining-track-integration).

## Shadows and body tilt

The original Honda side view keeps the keyed wheel/bumper cutout. Restoring the three-record
undercar shadow fixes road brightness through it, geometry unchanged. LOWIMAGE CRC32 `0f4c1a48`,
Honda HIGH `3562e854`; core/extension colors `c8000000`/`aa000000`; types 0/1/2 submit 2/8/12
triangles ([car-shadows](../visual-effects.md#original-vehicle-shadows)). Matched Donington capture: all 25 node matrices
within one float ULP after restoring HIGH node300 presentation tilt (source limit/reset,
transform/detail routing, replay imports); not full frame equality
([comparison](../graphics-comparison.md)).

## Lamps

Color/attachment correction: 15 GPU color cases and 6 HIGH/Mega attachment cases (MEDIUM already
attached correctly); damage rendering covers 11 families. Human input needed a separate fix:
forced lamp states had not exercised raw brake publication, so ordinary driving consumed the
AI-only latch (press/release ownership fixed). Brake CRC32: original `e9f9bda9`, Mega `3e33cf5d`;
Honda brake/release captures `6f24ad85`/`b6e15fb1`. [Lamp contract](../visual-effects.md#vehicle-lamps).

## Families and low detail

Both Vectras render with independent textures and MEDIUM animation after LOD changes. CPU coverage
spans 11 render IDs, four original graphics settings and three levels (signed offsets, immutable
sources). Mega 40/80 ranges/all 16 actors accepted; a LOD run compares both liveries at 20/60/100
with unchanged MEDIUM/LOW pixels. The ALLCARS alpha/DASH inventory is bounded; complete
raster/backend parity stays open.

Low detail static-sky suppression was recovered from matched original captures: every pixel
verified for weather 1–5/Lavaland at detail 0/1; moving HOR keeps separate visibility. Source
clear colors and capability branch:
[contract](../scene-models.md#sky-at-low-graphics-detail).
