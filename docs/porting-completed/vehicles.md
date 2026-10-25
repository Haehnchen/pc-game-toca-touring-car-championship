# Vehicle completion records

All eleven ALLCARS exterior resources V00–V10 are accepted. The [vehicle plan](../mega-vehicles-plan.md)
retains separate cockpit, finale, static-vehicle and combined acceptance work.

## Shared build

[vehicles.json](../../assets/models/mega/vehicles.json) registers render IDs 0–10 as Mega. The
normal build generates and bundles `car-<id>.glb`; embedded WebP80, lossless meshopt,
compatibility validation and original HIGH fallback follow the
[CM3D/GLB contracts](../cm3d.md#mega-glb-models). Detail 0–3 and original MEDIUM/LOW resources
are unchanged.

[inventory-vehicles.py](../../tools/inventory-vehicles.py) owns the 33 canonical LOD members,
source CRC32, hierarchy, slots, variants. Vehicle definitions keep geometry controls; artwork
recipes keep masters, CRC32, export quality; functional UV/alpha registrations stay with the
texture metadata.

## Exteriors

| Package / render ID | Model | Geometry definition |
| --- | --- | --- |
| V00 / 0 | Honda Accord | [definition](../../assets/models/vehicles/honda/definition.json) |
| V01 / 1 | Audi A4 | [definition](../../assets/models/vehicles/audi/definition.json) |
| V02 / 2 | Vauxhall Vectra, Cleland | [definition](../../assets/models/vehicles/vectra/definition.json) |
| V03 / 3 | Volvo S40 | [definition](../../assets/models/vehicles/volvo/definition.json) |
| V04 / 4 | Ford Mondeo | [definition](../../assets/models/vehicles/ford/definition.json) |
| V05 / 5 | Nissan Primera | [definition](../../assets/models/vehicles/nissan/definition.json) |
| V06 / 6 | Peugeot 406 | [definition](../../assets/models/vehicles/peugeot/definition.json) |
| V07 / 7 | Renault Laguna | [definition](../../assets/models/vehicles/renault/definition.json) |
| V08 / 8 | Tank | [definition](../../assets/models/vehicles/tank/definition.json) |
| V09 / 9 | Flexmobile | [definition](../../assets/models/vehicles/flexmobile/definition.json) |
| V10 / 10 | Vectra2, Warwick | [definition](../../assets/models/vehicles/vectra2/definition.json) |

IDs 0–9 are the selectable cars. Actor 5 uses render ID 10 and physical car 2; Vectra2 keeps its
own source pivots, decals, livery. Tank/Flexmobile replace the Ford actor pair when unlocked and
keep their source-supported variants. Acceptance covered matched native views, local lamp/paint/
arch joins, source bindings, steering/spin/tilt/damage, actual LOD selection, normal selection/
driving/audio; garage contributions are published from the accepted models.

## Runtime player names

Single Race/Championship, local split screen and Linkup publish each human's name to its own car
instance. Time Trial keeps the saved Ghost's driver name (also when its car matches the player) and
republishes the driver after a new best lap. AI/team names stay fixed. The canonical ALLCARS name
font keeps its 128×16 raster, source spacing/centering/clipping, 320-pixel drawing gate. Mega: 512×512
atlas of 38 authored WIN3 glyphs and 256×32 name bitmaps (exactly twice the source resolution);
source layout is computed before doubling coordinates; white antialiased contours keep all glyph
cells and ink bounds. The [HUD font generator](../../tools/build-hud-assets.py) owns `car-name.webp`.
Both raster sizes support nine-character profiles and valid longer native Ghost names.
[Material/source contract](../cm3d.md#encoding-and-materials).

Per-instance GPU name bindings keep shared decoded/texture resources, source mesh UVs, AI liveries.
Original HIGH and Mega name faces are supported incl. the separate Vectra A/Vectra2 B slots; bonus
cars and source MEDIUM/LOW families without name faces get no new panels. Mega name faces blend
continuous alpha with linear clamped sampling, also on original HIGH fallback models.
Checks: `car-name-reference` (also ASan/UBSan), `car-name-font-artwork`, `scene-hud-artwork`,
`vehicle-model-renault`/`-vectra`/`-vectra2`, `native-renault-model`, `native-split-screen`,
`native-split-mega-hud`, `native-time-trial`, `native-network-smoke`.

## Wheel wells and stance

Touring definitions keep wheel dimensions and visual axle outsets; Tank/Flexmobile keep
source-specific dimensions. Physical wheelbase, heights, collision state unchanged; permitted
visual offsets are bounded by the [loader contract](../cm3d.md#mega-glb-models).

Housings meet actual body openings. Only generated inner/cavity faces carry `toca_ghost_hidden`;
exterior armor/paint and ordinary thickness keep their Ghost behavior. Marked interiors render with
authored body-attached `_TOCA_GHOST_POSITION` vertices at 20 % inward depth and alpha 96; opaque
models keep the full housings. Mega Ghost wheels also use alpha 96 without altering geometry or
animation. Local alpha/UV repairs keep motifs, material slots, true openings. Do not restore coarse
source cutouts over geometric wheel arches. Checks: `formats-glb`, meshopt packing,
`time-trial-view`, authored-mouth/depth and paint-clearance for all nine touring resources plus
Tank/Flexmobile.

## Shared cockpit parts

K0-S: byte-identical wheel/wiper sources across nine archives, shared manifest/build/bundle, roles
3/4 fallback for both players. Wheel 2788T, wiper 181T; pivots, grips, source UV orientation,
motion plane kept. Imagegen WebP80 512×512/512×32 with alpha, repeatable imports, meshopt.
[Import recipe](../../assets/models/cockpits/shared/generation.json),
[geometry](../../assets/models/cockpits/shared/definition.json).

K0-H: roles 0/1 for all nine interiors: common rounded hand/forearm geometry, Audi-specific offset,
nine independent original material/pattern sets; each hand keeps six mesh/four material slots,
7720T; 36 Imagegen textures (four materials per car); source grip/shoulder/shift/Flex behavior kept.
Grip fit: index behind/below the rim, other fingers/thumb in smooth curves, both driver sides and Flex
right fitted separately; all 20 sampled finger ends/centers of the five gripping hands sit behind
the wheel from the driver's view; Flex left keeps its original lever. A CC0 Blender hand served only
as a proportion reference. [Hand geometry](../../assets/models/cockpits/hands/definition.json),
[material import recipe](../../assets/models/cockpits/hands/generation.json).

Shared wheel/wiper/hands, GPU resource sharing and Honda's dashboard are accepted; the other eight
dashboards remain open in the [vehicle plan](../mega-vehicles-plan.md#k0-and-per-vehicle-interiors).
