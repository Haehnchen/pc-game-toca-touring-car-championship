# Mega Roadside and Object Plan

Packages M00–M08. [Porting plan](porting-plan.md), [work rules](../plan.md),
[setup/tests](porting.md), [model contracts](scene-models.md).

Scope: the [track plan](mega-tracks-plan.md) bundles per-track acceptance R00–R10;
[sky/horizon](mega-backgrounds-plan.md) covers B00–B06 (forest strips here in M02, transition
to sky B04); [GROUND/filtering](mega-track-materials-plan.md) covers ground materials and
edge joins. Menu, title, loading and garage images are out of scope. Static cars, transporters,
rescue vehicles, mobile cranes and static flying objects:
[T0](mega-vehicles-plan.md#t0-and-static-track-vehicles); finale drivers: F0.
Forest, figures and buildings stay in the FREE track pipeline.

## Completed scope

M00–M08 and all ten per-track packages are complete: 1983 pure mesh rows /
1580 types / 5294 raw bindings accepted. No open asset findings.

**Texture review of the remaining track objects:** [MT00–MT05](mega-scenery-texture-review.md)
is binding, including for already accepted or shared images. Fidelity to the original,
material appearance and legibility on the model count; existing AI images are not a free
pass. Vehicles and cockpits are excluded.

Textures supply the detail; keep simple spatial bodies, silhouettes, backs and ground
joins. Spare geometry budget does not increase the existing placement density. No fixed
triangle or performance targets and no new report series. All confirmed near-tree types
are accepted per track.

Order: **M00 → M01a → M01b → M01c → M01d → M02 → M03 → M04 → M05 →
M06 → M07 per track → M08.** M00 may register further evidenced types as sub-packages.

## Inventory and references

Integrated: pylons, posts, braking signs, billboard frames, tyre stacks,
steel guardrails ([acceptance](scene-models.md#mega-track-props-and-scenery)); active
aircraft/helicopters/balloons/Audi airship ([HOR](scene-models.md#mega-aircraft-helicopters-and-balloons), `e4bc0ec`).
Inventory check: [roadside audit](scene-models.md#roadside-coverage-audit--2026-10-17).
Generated GLBs do not prove full coverage; the inventory and package acceptances hold the state.
Near trees in OBJECTS are distinct from forest strips (TREES).

| Reference | Use |
| --- | --- |
| `references/toca-1.1a/data/<prefix>free.bfp` | Only source; zero-based members 2 TREES, 3 FENCE, 5 OBJECTS, 6/7 lower object levels, 8 pylons, 9 posts, 10 signs, 11 flags. Lavaland skips empty families. |
| [Resource contracts](scene-resources.md), [loader](../src/scene/resources.cpp) | Original consumer `0044e7bb`, families, placement, Mega override, structure check. |
| [Model consumers](scene-models.md#original-model-consumers), [visibility](scene-rendering.md#scene-visibility-and-sector-order) | Static baking, nodes, sectors, material/alpha/distance handling. |
| [Builder](../tools/build-mega-assets.py), [geometry](../tools/mega_geometry.py) | Generation, selection, budgets, bundling, compression. |
| [Artwork/import](../assets/models/mega/README.md), [mappings](../assets/models/mega/texture-replacements.json), [importer](../tools/import-mega-textures.py) | Names/dimensions/CRC32, import parameters, WebP70 export. |
| `build/dev/generated/mega/scene-manifest.json` | Local size/coverage record, not an original source. |
| [Geometry](../tests/tools/test_mega_geometry.py), [asset](../tests/tools/test_mega_assets.py), [GPU checks](../tests/render/test_scene_models.cpp) | Check paths; a green old closeup test does not check a new object. |

Track ID/prefix: `0 donl`, `1 silv`, `2 thru`, `3 bran`, `4 oult`, `5 dons`,
`6 crof`, `7 knoc`, `8 snet`, `9 land`. Donington GP/Short shares TREES (CRC32 `0e4e8cd8`),
forest geometry, images and placements, not the driving route.
Tooling modules: M01 `tools/mega_vegetation.py`, M02 `tools/mega_woodland.py`;
vegetation recipe `vegetation-generation.json`.

- [x] All confirmed pure M00 types complete; T0 and cockpits separate.

## Recipe per sub-package (Q/T/G/I/A)

1. **Q:** archive/member/mesh index, mesh name, node binding, variants, slots, instances,
   CRC32 (archive, original data, outputs). Equal names do not prove equal types;
   compare geometry, UVs and material binding.
2. **T:** image generation with attached original templates, 1:1 replacement with the same
   UVs/slots/motifs; no upscaler, no pixel blocks/pseudo-lettering. Match unclear lettering
   against related original images, otherwise document it as reconstruction.
   Check tile periods/joins on the model after encoding. Slot indices stay; append new
   slots only; new 3D surfaces may use a documented additional atlas.
3. **G:** simple thickness, backs, recognisable shape; keep silhouette, size, ground
   attachment, nodes/mesh slots/hierarchy/sectors/transforms/contact and animation data.
   Do not leave replaced image surfaces as additional cross planes. Detail 0–3
   remain original resources.
4. **I:** normal Mega generation/bundling; the type reaches all confirmed instances with
   the original binding. Images WebP70 with alpha, ≤ 512 px per edge and ≤ 256 KiB; GLB ≤ 8 MiB,
   `EXT_meshopt_compression`; budgets 2,048 triangles per scenery mesh, 48,000 per family
   (adjust only with measurements and a Main decision).
5. **A:** same object/camera/light before/after, both sides, oblique, near/driving distance,
   variants; alpha, see-through, ground join, flicker, backs, lettering.
   Triangle counts alone do not close a package.

Original extracts/derived models live under the ignored `build/`; only code, metadata and
new artwork are versioned. No new sounds, figure motion or collisions.

## M00 — Concrete inventory and first handover

[inventory-scenery.py](../tools/inventory-scenery.py): 2014 mesh rows, 1608 exact
geometry/UV/material types in seven object families (TREES/FENCE/OBJECTS/cones/posts/
signs/flags) with source/material CRC32, mesh/node binding, variants, UVs, dimensions,
package assignment. FREE8/9/10 complete. Generate with:
`python3 tools/inventory-scenery.py --data-root build/dev/generated/original-data`
(output `build/asset-inventories/`). [Evidence](porting-completed/scenery.md#inventory).
Advertising surfaces do not close buildings.

## M01 — Near trees

Source FREE member 5 OBJECTS; LLRTS1/2 forest-strip images do not replace near-tree templates.
Keep crown shapes, alpha silhouettes, trunk bases, gaps and material variants.
[Model contracts](scene-models.md#mega-near-trees), [inventory](../tools/inventory-scenery.py),
[object status](../assets/models/mega/scenery-acceptance.json).

- `TRGS15_1`: Thruxton, type `ed14d531`, 15 bindings, `LHRTWS1`;
  [package](../assets/models/mega/thruxton-vegetation-generation.json).
- `TRGS16_1`: Oulton FREE5 #29, type `d5dd3df8`, two bindings, `LHRTWS2`;
  [package](../assets/models/mega/oulton-nature-generation.json).

- [x] All confirmed per-track near-tree types with material variants accepted.

## M02 — Forest strips and missing vegetation images

Source FREE member 2 TREES; FENCE hedges belong to M03. Simple closed bodies,
clean joins, foliage detail from textures; no crown/leaf fine geometry.
Use existing forest bodies/images; keep material binding, original end triangles,
shared creases and ground lines. Shared images do not release unedited forest meshes
as geometry. Further work only for visible defects at normal driving distance;
low visibility is not a near-view or detail release.

| Track | Scope | Package |
| --- | --- | --- |
| Thruxton | 59 forest meshes, `LLRTS1/2` (alpha, colour distribution, repeat phase) | [thruxton-vegetation](../assets/models/mega/thruxton-vegetation-generation.json) |
| Brands Hatch | 61 forest meshes, 7 near-tree types, 8 direct images | [brands-vegetation](../assets/models/mega/brands-vegetation-generation.json) |
| Oulton Park | 131 forest meshes, 3 near-tree types/120 bindings | [oulton-nature](../assets/models/mega/oulton-nature-generation.json) |

[Woodland contracts](scene-models.md#mega-simple-croft-woodland),
[source/distance inventory](../tools/audit-woodland.py),
[image mapping](../assets/models/mega/texture-replacements.json).

## M03 — Remaining fences and boundaries

| Scope | Package |
| --- | --- |
| Silverstone: 93 meshes/18 slots (tyres, rails, mesh, wood, masonry) | [silverstone-rest](../assets/models/mega/silverstone-rest-generation.json) |
| Thruxton: 55 meshes, eight tyre periods, source phase | [thruxton-rest](../assets/models/mega/thruxton-rest-generation.json) |
| Brands Hatch: 45 meshes, eight tyre periods, Mobil `1` field | [brands-rest](../assets/models/mega/brands-rest-generation.json) |
| Oulton: 125 meshes, eight tyre periods, rail cut-outs, end profiles; water image/native colour constant unchanged | [oulton-nature](../assets/models/mega/oulton-nature-generation.json) |
| Billboards: simple back panel with side thickness, no applied frames/struts; 295 placements on nine tracks (114 GP/Short) | — |
| Donington cones/posts/signs: 121 placements, four source textures; DHL white area, shared Dunlop arch | [donington-dynamic-props](../assets/models/mega/donington-dynamic-props-generation.json) |

- [x] All per-track mesh, rail, tyre, wood, masonry and hedge materials checked; Croft uses four side rows, two figure-eight rows on tyre top faces and unlettered MOBILTOP wood.

## M04 — Spectators, marshals and pit crew

Accepted (direct textures, original silhouettes/cards/masks/poses, simple flat
volumes):

| Scope | Package |
| --- | --- |
| Donington GP/Short: 3 figure motifs, 85 placements | [donington-spectators](../assets/models/mega/donington-spectators-generation.json) |
| Silverstone: `LCROWDS3_1` at 90 places (shared `Crowd7`/`CROWD7`); `LMARSH3_1` at 15 places | [stands](../assets/models/mega/silverstone-stands-generation.json), [rest](../assets/models/mega/silverstone-rest-generation.json) |
| Thruxton: `PEOBACK_1`/`PEOMID_1`, 81 bindings, two alpha masks | [thruxton-rest](../assets/models/mega/thruxton-rest-generation.json) |
| Brands Hatch: 5 types/200 bindings, two side-by-side crew cards, 6 images | [brands-rest](../assets/models/mega/brands-rest-generation.json) |
| Oulton: 2 types/122 bindings (eight spectators, one cameraman) | [oulton-rest](../assets/models/mega/oulton-rest-generation.json) |
| Snetterton: spectators, marshals, grid staff, crew | [snetterton-nature-rest](../assets/models/mega/snetterton-nature-rest-generation.json) |

- [x] Spectators, marshals and crew accepted on all tracks with original poses, head counts, cards and masks; no new animations or detail bodies required.

## M05 — Small parts, stands, tents and animals

Accepted (original bodies/UVs kept):

| Scope | Package |
| --- | --- |
| Brands: `LSPEAKER_1`, `LBIN1_1`, both `BH24TSB*`, 13 images; small `CLEAN`/`AWAY` inscription as uncertain reconstruction | [brands-rest](../assets/models/mega/brands-rest-generation.json) |
| Oulton: `BH24TSB_1`, ten fabric/service surfaces | [oulton-rest](../assets/models/mega/oulton-rest-generation.json) |
| Silverstone: `FOTENT_1` at five places, Ford fabric texture | [silverstone-rest](../assets/models/mega/silverstone-rest-generation.json) |
| Knockhill M05d: four sheep shapes/20 bindings (`SHEEP2/4/7/8`), pair/single posture, pivots; three container bindings | [knockhill-nature](../assets/models/mega/knockhill-nature-generation.json) |
| Lavaland M05e: FREE8 `CONES.C3D` (`ca267324`), five `GARCON_1`, two opaque 256² WebP80 `GARG01`/`GARG02`, closed face relief | [lavaland-gargoyle](../assets/models/mega/lavaland-gargoyle-generation.json) |

- [x] All confirmed containers, tents, tables and stands including Croft complete; existing simple bodies and ground joins kept.

## M06 — Flags and poles

FREE11 as `flags.glb` in the Mega builder/resource path; the `PropResources.model` hierarchy
materialised by `decode_props` stays; dynamic variants and static baking are separate.
Animation: three cloth phases with four-tick switching,
[contract](scene-resources.md#track-props-and-starting-cameras). Accepted:

| Track | Scope | Package |
| --- | --- | --- |
| Donington GP/Short | FRA/POR/UK, nine materials, three phases | [donington-mt03](../assets/models/mega/donington-mt03-generation.json) |
| Thruxton | 24 controllers, 15 cloth images | [thruxton-rest](../assets/models/mega/thruxton-rest-generation.json) |
| Brands Hatch | 8 mesh rows/5 types/126 raw bindings; 21 childless identity roots without controller; three UK images | [brands-rest](../assets/models/mega/brands-rest-generation.json) |
| Oulton | 20 mesh rows/14 types/108 raw bindings; 12 images, 4 nations | [oulton-rest](../assets/models/mega/oulton-rest-generation.json) |
| Lavaland | 96 mesh rows/17 types/24 controllers, 15 images; twelve missing source mappings added | [lavaland-flags](../assets/models/mega/lavaland-flags-generation.json) |
| Knockhill | 44 mesh rows/20 types/27 controllers, 18 images, 6 nations | [knockhill-rest](../assets/models/mega/knockhill-rest-generation.json) |
| Snetterton | 12 mesh rows/10 types/21 controllers, 9 images (UK/DK/DE) | [snetterton-flags](../assets/models/mega/snetterton-flags-generation.json) |

Pole constant native; hierarchy and source alpha kept.

- [x] All visible FREE11 consumers, pole slots and cloth variants accepted with per-track hierarchies/phases; existing animation/GPU evidence reconciled.

## M07 — Buildings, stands, towers and bridges per track

Per track: M00 lists instances with original images; A roof edges, openings, supports,
stairs/railings, superstructures; B facades, windows, roofs, legible original signs; Main
checks the view from the course, both sides, passages, neighbour joins, sector borders.
Related types with their instances form a Q/G/T/I/A package. Do not unify similar buildings
of different tracks without data evidence. Shared images do not close further
building types; the [texture review](mega-scenery-texture-review.md) remains binding.
Accepted (original bodies/UVs/slots/alpha kept):

| Package | Scope | Recipe |
| --- | --- | --- |
| M07-0/5 Donington GP/Short | Buildings, stands, bridges | [track-assets](porting-completed/track-assets.md#r00--donington-gp) |
| M07-1 Silverstone | 31 types/85 places incl. 4 TV boards | [stands](../assets/models/mega/silverstone-stands-generation.json), [R01](mega-tracks-plan.md#r01--silverstone) |
| M07-2 Thruxton | 37 types/55 places; 89 source images (83 direct, 5 existing, 1 native flat roof sampler) | [buildings](../assets/models/mega/thruxton-buildings-generation.json), [structures](../assets/models/mega/thruxton-structures-generation.json) |
| M07-3 Brands Hatch | 26 types/40 bindings, 108 source images (107 new, 1 existing Snap-on); bridge material separated from the native helicopter sampler | [brands-buildings](../assets/models/mega/brands-buildings-generation.json) |
| M07-4 Oulton | 18 types/37 places, 78 source images (49 new), `OP05SDE` shared unchanged | [pits](../assets/models/mega/oulton-pits-generation.json), [buildings](../assets/models/mega/oulton-buildings-generation.json) |
| M07-6 Croft | 3 pit/control buildings + 4 TV structures, 4 types/7 bindings, 18 images; `BH06WT2_1` belongs to T0 | [croft-buildings](../assets/models/mega/croft-buildings-generation.json) |
| M07-7 Knockhill | 17 types/27 places; animals stay in M05 | [buildings](../assets/models/mega/knockhill-buildings-generation.json), [rest](../assets/models/mega/knockhill-rest-generation.json) |
| M07-8 Snetterton | 24 types/53 places | [snetterton-buildings](../assets/models/mega/snetterton-buildings-generation.json) |
| M07-9/M03 Lavaland | 2 temple instances, 3 spatial FENCE sections, 5 images; TREE/POST/SIGN families empty | [lavaland-rest](../assets/models/mega/lavaland-rest-generation.json) |

## M08 — Completeness and joint acceptance

Accepted (advertising/props, original bodies/UVs):

| Scope | Package |
| --- | --- |
| Thruxton: 14 types/72 bindings, 15 source images (12 existing, 3 new) | [thruxton-props](../assets/models/mega/thruxton-props-generation.json) |
| Brands Hatch: 16 types/70 bindings, 17 images; eleven image sources shared with Croft, checked there representatively; per-track Croft acceptance is complete | [brands-props](../assets/models/mega/brands-props-generation.json) |
| Silverstone: 171 meshes/419 bindings, 130 source images | [R01](mega-tracks-plan.md#r01--silverstone) |
| Oulton: 19 types/79 bindings, 319 mesh rows; six sign bodies with white DHL structure surface, red remainder at the lower arrow edge corrected | [oulton-rest](../assets/models/mega/oulton-rest-generation.json), [R04](mega-tracks-plan.md#r04--oulton-park) |

- [x] All 1983 pure mesh rows accepted; existing Donington acceptances added. Shared sources, per-track variants and driving/audio/split evidence reconciled; Croft original prop bodies/UVs and new materials checked representatively. No performance measurements or new full matrix.

## Check assignments

| Change | Existing checks |
| --- | --- |
| Geometry/material/GLB | `mega-geometry`, `mega-vegetation`, `mega-assets`, `scenery-inventory`, `formats-glb`, `renault-glb-generator`; vegetation additionally `scene-reference-vegetation` |
| Resource/family integration | `scene-resources`, `scene-reference-resources`; extend `scene-reference-mega` with new closeups |
| Alpha/image decode path | `frontend-resources`, `gpu-states` |
| Flags | `scene-props`, `flag-animation`, `scene-reference-props`, `scene-reference-models` |
| Contact-capable props | `prop-contacts`, `prop-fragments`, `prop-scene`; `native-prop-contacts` only for integration |
| Native Mega integration | `native-renault-model`, `native-split-mega-hud` |

Example: `make check TESTS='^(scenery-inventory|mega-vegetation|mega-assets|scene-reference-vegetation)$'`.
