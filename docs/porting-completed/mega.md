# Completed: Mega assets and rendering

Open work: [porting plan](../porting-plan.md) and linked asset plans. Recipes retain
source/output identities and import parameters; the asset plans and the type/status ledger
define acceptance scope.

## Vehicles and shared cockpit parts

Exteriors, original bindings, wheel wells/stance, shared wheel/wiper/hands:
[vehicle completion record](vehicles.md). Per-car cockpits, GPU resource sharing, remaining
roster: [vehicle plan](../mega-vehicles-plan.md).

## HOR models

Eleven track-specific paths reuse six rebuilt GLB payloads and 32 source-matched WebP80
materials (≤ 512 px). Palette-qualified CRC32 mapping, hierarchy, weather gates, routes,
animation slots remain original. [Contract](../scene-models.md#mega-track-props-and-scenery).

## Roadside objects

M00 inventories shared geometry/UV/material types and package ownership; T/F vehicles and finale
figures stay with the vehicle plan. Near-tree packages M01a–M01d-f keep source placement,
footprint, silhouettes, palette-qualified artwork ([inventory, tree QA](scenery.md)). Donington
GP/Short buildings: Dunlop, barn, grandstand, CBO1–CBO6, startlight. Status:
[scenery plan](../mega-scenery-plan.md); exact bindings:
[type ledger](../../assets/models/mega/scenery-acceptance.json). Shared images do not approve
another model or its placement; all props stay in the [texture review](../mega-scenery-texture-review.md).

## Skies, lenses and woodland

48 active skies, seven lens sprites, four M02a woodland motifs: source-qualified artwork;
Weather/Extras/Lavaland selection, palette, silhouette, lens blend, mode gates original
([sources/CRC32](../../assets/models/mega/backgrounds-generation.json),
[plan](../mega-backgrounds-plan.md)). Croft, Snetterton, Oulton woodland: simple closed bodies
with source UVs/bindings ([record](scenery.md#woodland)). Further geometry needs a visible defect
at driving distance; foliage detail belongs in textures; image sharing does not accept every
woodland type.

## Ground materials and filtering

S00–S16 on all ten tracks: 134 GROUND slots, 119 image sources, Mega 2× MSAA, mipmaps/trilinear
with 2× anisotropy, neutral world-space grain, guarded boundary repairs; original profiles and
contact data unchanged ([contract](ground-materials.md)).

## Track asset inventory

The inventory establishes required materials/models and exact source sharing; S/M/B/T packages own
assets; remaining local placements and common performance/split-screen acceptance belong to
[R00–R10](../mega-tracks-plan.md). [Inventory contracts](track-assets.md).

## View distance

Mega: projection 500, short-object ranges 256/384, rival HIGH/MEDIUM 40/80 with the original
ten-dispatch hold. Original detail 0–3 keeps its budgets, angles, data. Local 16-bit batches
support more than 65536 total vertices.
[Visibility](../scene-rendering.md#mega-range--2026-10-17),
[vehicle LOD](../scene-models.md#vehicle-families-and-rival-detail).
