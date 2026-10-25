# Track assets: reuse and evidence

All 1983 pure mesh rows accepted; [R00–R10 completed](../mega-tracks-plan.md).
Vehicles/cockpits stay separate. Ground evidence: [ground completion](ground-materials.md).

## Scope, sources and status

[Ground TSV](../mega-ground-textures.tsv), [scenery inventory](../../tools/inventory-scenery.py),
[type acceptances](../../assets/models/mega/scenery-acceptance.json),
[sky inventory](../../tools/inventory-backgrounds.py),
[image mapping](../../assets/models/mega/texture-replacements.json) provide sources, consumers, CRC32.

### What already exists

Ground, active sky/HOR and named M packages exist; additional model/texture types need their own
confirmation.

## Shared assets

Direct sharing requires identical geometry, UVs, materials; otherwise share only the construction
method. The global image mapping uses the original BMP CRC32; record all consumers beforehand.

| Group | Documented shared inventory | Usage |
| --- | --- | --- |
| Donington GP/Short ground | All 15 BMPs byte-identical; CRC32 per image in the ground TSV. | One shared image set; local mesh/UV joins accepted. |
| Donington GP/Short forest, fence, flags | Whole FREE members 2 `0e4e8cd8`, 3 `3785a2f2`, 11 `ba745fff` identical. | Shared asset sources; both track paths checked. |
| Donington GP/Short objects | 252 exact cross-track types across FREE2/3/5/11 (incl. bridge `DA16DUN_1`, type `2c491a7b`); OBJECTS as a whole differ. | Same-type assets shared; own OBJECTS mesh/node slots kept. |
| Donington GP/Short sky | Both HOR archives CRC32 `8771ca20`; DO set shared, cloudy selection differs. | Shared artwork; own cloudy selection incl. Short `DO03`. |
| Other tracks: ground | No further identical ground BMPs. | Same-named `GRASS`, `TRACK`, `RUMBLE` etc. as separate templates. |
| Other tracks: model types | Two further exact types: Brands/Lavaland UK flag phases `dc8b315c`, `11e18d37`. | Share only these phases; do not derive hierarchy/animation. |
| Near trees, figures, crew, fences | Similar names/shapes, often different types/palettes. | Share construction method; original materials/extra atlases by exact BMP/type identity. |
| Brands/Croft advertising images | `AUTOTRAD` `b8312165`, `LDV` `4f44412d`, `MCN` `fd6f4229`, `2SNAPON`/`SNAPON` `b3f7551c`. | Already assigned images shared, also across different material names. |
| Brands/Croft T0 vehicle images | `LVECTR3` `43276baa`, `LVECTR4` `88895cee`, `LVOLVO3` `91798394`, `AMBULAN2/3/4/5` `c7de90dd`/`c4462537`/`0580e30c`/`afabf336`. | One image task per source under T0; check different model types separately. |
| Brands/Lavaland flag images | `LIFLAGA1/2/3` `2dc93a9b`/`b221c277`/`f55e164d`. | Shared artwork under M06; keep consumers/phases per track. |
| Extras and sun | `SILLYLAN`, `CARTOON` on the real tracks; seven lens images shared. | Existing assets; Lavaland keeps its priority selection. |
| Static vehicles/flying objects | T0 candidates on several tracks, partly similar active HOR models. | Visual/geometry comparison under T0. |

254 shared types are not 254 new models; empty members (`b5424ac9`) create no task. Assign
CHEV/POST to their consumer before commissioning.

### Image requirements per track

Confirm visible consumers from the current inventory; do not sum shared BMPs more than once. A
material entry alone is not an image task.

## Delivery scope and task allocation

S04/M/B/T each keep one owner. The [active delivery contract](../mega-tracks-plan.md#deliverables-and-task-split)
governs image files, GLB bundling, hierarchy, responsibilities.

## R00 — Donington GP

141 source materials, 144 slots, four additional foliage images. Direct texture replacement
preserves UVs, slots, alpha, repeat phase; shared images qualified via original BMP CRC32.
[Building recipes](../../assets/models/mega/donington-buildings-generation.json),
[MT01 bindings](../../assets/models/mega/donington-mt01-generation.json),
[remaining materials](../../assets/models/mega/donington-mt03-generation.json). Barn, CBO1–6,
grandstand, Dunlop keep source faces/locations; simple bodies add shape, thickness, back faces
([building/bridge QA](scene-models.md#donington-buildings),
[CBO2 sources](../../assets/models/mega/donington-cbo2-generation.json),
[Mega integration](../scene-rendering.md#mega-range--2026-10-17)).

## R05 — Donington Short

136 source materials, 139 slots, four foliage images from GP; five DENSO tower images dropped.
Own OBJECTS member `f60fd8a0` instead of GP `5f670bb9`; mesh/node slots, locations, grandstand
slope original. `CHERV_1` #60/nodes 457/459 keeps its UV direction versus GP `CHERVL_1` (both
locations, opaque DA11WEL3 back face). FREE10 `LARROW5_1` #5/node 41 keeps the Dunlop atlas
crop, direction, back face. No additional images/types. Bindings:
[image mapping](../../assets/models/mega/texture-replacements.json),
[model contract](scene-models.md#donington-buildings). T0 with the vehicle plan.

## Donington T0 inventory check

GP/Short share geometry and placement of the three static vehicle types; positions, normals,
UVs, material slots, hierarchy, original detail selection stay; new images WebP80 ≤ 512 px;
no moving or damage variants.

| Type | Shared inventory and detail selection | Package |
| --- | --- | --- |
| Static Spitfire `e8a3b83b` | 112V/76T, FREE5 mesh 2/node 5; FREE6 identical, FREE7 missing. | [T09](../../assets/models/mega/donington-static-spitfire-generation.json) |
| Transport truck "Silverline Motorsport" `7653b1bc` | 99V/64T, FREE5 mesh 34; two instances each; FREE6/7 without type; vertex block CRC32 `ebe1c4a1`. | [T10](../../assets/models/mega/donington-static-truck-generation.json) |
| SKY LIFT aerial work platform `2b9bfb4e` | 136V/66T, GP mesh 55/Short 57; three instances each; FREE6 identical, FREE7 missing; vertex block CRC32 `e5cf68ea`. | [T11](../../assets/models/mega/donington-static-platform-generation.json) |

"Silverline Motorsport" replaces (user request) the illegible truck lettering in the existing
side panel; roof sample free; source UVs mirror the lettering on the local −X flank.
HOR jets/helicopters stay separate (33 images, eleven paths, six payloads); both Donington HOR
archives are identical (`8771ca20`) and contain only `AIRMARK_1`/`PSP1_1`; their geometry/UVs/
images are unsuitable as a 1:1 replacement for the static Spitfire. Other tracks have differing
`CRAN3_1` image identities; Donington's aerial work platform is bound exclusively to BMP CRC32
`6148ee1c`/`9ac42a3f`. [Open T0 types](../mega-vehicles-plan.md#t0-and-static-track-vehicles)
remain in the vehicle plan.

## R04 — Oulton Park

All 319 pure mesh rows and local materials accepted.
[Final technical state](../mega-tracks-plan.md#r04--oulton-park).

## R06 — Croft

SAP1 mesh 23/24 as 40T body, ground/back face, mesh 27 visibility accepted.
[Image recipes](../../assets/models/mega/woodland-generation.json),
[model contract](../scene-models.md#mega-simple-croft-woodland).

## R07 — Knockhill

trge31 stays distant backdrop; no blanket volume rebuild.

## R08 — Snetterton

LLRTS2 #19/node 60 (`808e1de3`) with 14T, LLRTSX #0/node 6 (`b0101817`) with 6T.
[LLRTS2](../scene-models.md#mega-snetterton-llrts2-woodland),
[LLRTSX](../scene-models.md#mega-snetterton-llrtsx-woodland).

## R09 — Lavaland

LAVABAK has priority, TREE/POST/SIGN stay empty. Derive no additional normal weather/race track
assets.

## Acceptance per work package and track

Source/UV/alpha contract, local track image, package commit:
[R acceptance contract](../mega-tracks-plan.md#acceptance-per-subpackage-and-track). Reuse existing
evidence; no performance measurements or full matrix per object.
