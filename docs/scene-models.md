# Supported track model consumers

## Mega track props and scenery

- Detail 4 loads generated families from `build/<config>/generated/mega/scene/track-N/`,
  bundled as `assets/models/mega/scene/track-N/`: cones, posts, signs, trees,
  fence, objects, active HOR groups. All ten tracks; Lavaland keeps its absent
  tree/post/sign families. Detail 0–3 uses canonical resources. A GLB path alone
  does not establish rebuilt geometry.
- Builder reads canonical FREE/HOR archives and verifies their CRC32 ledger.
  Generated GLBs stay under ignored `build/`; authored images/provenance live in
  `assets/models/mega/`. Exact BMP-CRC32/name/dimension mappings replace only
  reviewed materials; unmapped materials keep canonical pixels. Shared source
  images do not establish completed geometry for other consumers.
- Nodes, transforms, variants, sectors, original collision/actor data unchanged.
  Extra geometry: ≤2,048 triangles per scenery mesh, ≤48,000 per family
  (ceilings). Stack selection, sloped-ground contact, cone feet/bands, prop
  bindings fixed; no added placements. Concrete selected separately.
- Donington cones/posts/signs keep simple source shapes; advertising boards keep
  original fronts with shallow solid sides/backs; tyre barriers hollow stacks;
  steel rails folded profiles.
- Images: WebP 70, method 6, transparency, ≤512 px/edge, ≤256 KiB each; imports
  crop/downsample generated pixels only. GLBs share identical buffers, drop
  unused/duplicate vertices, keep winding/material order, lossless meshopt, no
  duplicate raw streams, ≤8 MiB each. Cache reconciliation removes retired
  overrides incrementally.
- Current generated sizes/CRC32: `build/dev/generated/mega/scene-manifest.json`.
  [Initial acceptance](porting-completed/scene-models.md#scenery-baseline).
  Woodland/vegetation contracts below; remaining work belongs to the
  [scenery plan](mega-scenery-plan.md), including independent texture review.

### Donington buildings (GP / Short)

All keep source forms, UVs, material slots and placements. The `mega_*.py`
generators select by exact source identity. Evidence:
[porting-completed](porting-completed/scene-models.md#donington-buildings).

| Model | Generator | GP / Short mesh/node | Source v/t → tris | Notes |
| --- | --- | --- | --- | --- |
| `DA16DUN_1` Dunlop bridge | `mega_dunlop.py` | mesh22/node59 / mesh21/node61 | 156 v/116 t prefix → 520 | Rounded shoulders, tyre barrels, closed supports keep ground attachment/passage; five named materials, one generator. |
| `DA50BARN_1` | `mega_barn.py` | mesh13/nodes39,41 / mesh12/nodes41,43 | 30 v/14 t → 382 | Four 512×256 WebP80 (boarded walls, asymmetric windows, pale rear, black doorway, weathered roof); gable/roof edges/doorway depth; four placements. |
| `DA02GST_1` grandstand | `mega_grandstand.py` | mesh8/nodes27,223 / mesh37/nodes249,399 | 136 v/80 t → 498 | Roof supports/overhang, DONINGTON fascia, stairs; six WebP80, original atlas proportions, case-sensitive `Da02gsd5`; COM9/GSD9/GSD4 shared by exact BMP does not close other buildings. |
| `DA28CBO4_1` | `mega_cbo4.py` | mesh10/node33 / mesh9/node35 | 59 v/30 t → 248 | Five slots; roof/ridge, six window groups, closed twofold-sign reverse; COMA/COM4/PIT3 source-qualified WebP80, COM9/GSD9 reused. |
| `DA28CBO5_1` | `mega_cbo5.py` | mesh11/node35 / mesh10/node37 | 60 v/32 t → 256 | Five roles, uneven base; shared CBO4/grandstand images, no new slots; four-face sign thickness. |
| `DA28CBO6_1` | `mega_cbo6.py` | mesh12/node37 / mesh11/node39 | 63 v/34 t → 258 | Five roles; PIT3 end and GSD9 rear/end triangles distinct; closed four-triangle-sign reverse. |
| `DA28CBO1_1` tower | `mega_cbo1.py` | mesh48/node307 / mesh50/node347 | 84 v/54 t → 501 | Nine roles; window bands, terrace/cabin/top-block edges, PIT2 sign reverse; six source-atlas WebP80 (Shell/HELIX/ADVANCE), COMA/COM9/GSD9 reused. |
| `DA28CBO3_1` | `mega_cbo3.py` | mesh51/node339 / mesh53/node379 | 64 v/32 t → 476 | Simple roof edges/ridges, six window groups, sign back; thin COM4 side/rear strips, no facade frames; all five images reuse CBO4/grandstand artwork. |
| `DA28CBO2_1` | `mega_cbo2.py` | mesh52/node341 / mesh54/node381 (`7c0884d1`) | 61 v/32 f → 74 | Closed roof shell, blank-red sign backing, no window/roof-course relief; COMB one source-layout WebP, four shared. [Quellen](../assets/models/mega/donington-cbo2-generation.json). |
| `DA03STL_1` startlight (`9804820a`) | — | mesh0/node1 both | unchanged | Four direct WebP replacements; STL2/3 explicit alpha for black-keyed openings, STL4/5 opaque; casing already thick, no geometry. [Quellen](../assets/models/mega/donington-stl-generation.json). |

Dunlop bridge materials: all five `DA16DUN_1` materials replaced on GP/Short;
banner halves 512×128, support 256×256, tyre atlases 512×512. One continuous
generated arch inversely projected (bilinear, texel-center) into canonical
triangle UVs incl. .02/.99 clamps and unequal crown coordinates so the middle N
joins; shared underside/support-edge region unlettered; braking, arrow, tread
islands keep pixels. Slots/UVs/geometry unchanged by the material pass.
[Packing evidence](porting-completed/scene-models.md#donington-buildings).
Lettering governed by the current texture review.

### Mega near trees

Shared contract: source-specific crown/foot, UV charts, materials, nodes,
sectors, transforms stay; no animation/collision/sound added. All FREE member 5
unless noted. Provenance: [porting-completed/scenery.md](porting-completed/scenery.md#near-trees).

| Type / name | Placements | Tris | Facts |
| --- | --- | --- | --- |
| `TRIS04_1` / `LHRTS4` (M01a) | 235 (230 static on eight circuits, five dynamic Snetterton roots) | 244 | Broad irregular crown, original footprint/trunk foot; local height −Z; eight palettes keep slots/UVs; appended MegaTreeLeaf/Bark reuse 17 images. |
| `TRIS07_1` / `LHRTS7` (M01b) | 16 (Donington GP/Short 5 each, Brands 6) | 300 | Alpha outline 2.75×10.94 m inside padded 6.51×14.04 m bounds; short stem, broad lower crown, narrow tip; two redraws/two sprites, MegaTreeLf7 + shared bark. |
| `TRGE17_1` / `LHRTWS3` (M01c) | 100 (Donington GP/Short, Thruxton, Brands, Croft, Snetterton) | 268 | Five crown masses, rear depth, five feet, open lower gaps; 26.25×14.04 m; five palettes with distinct alpha gaps; MegaGroveLf/Bark. |
| `TRIS04B_1` / `LHRTS4` (M01d-a) | 51 (Donington GP/Short 13 each, Brands 25) | 244 | Two source types, 11.97×11.97×18.25 m, foot Z=−6251/65536; BMPs match M01a palettes, images reused; own topology/UV seams. |
| `TRIS02B_1` / `LHRTS2` (M01d-b) | 30 (Donington GP/Short 15 each) | 300 | Mesh35/type `98533c35`, CRC `5938e222`; 8.14×17.55 m, foot Z=−1860/65536; slot 74 BMP `fdd155e8`; redraw 256×512 + MegaTreeLf2 sprite 512×512; inner motif wraps at equal angular U intervals. |
| `TRIE01_1` / `LHRTS1` (M01d-c) | 16 (Croft) | 428 | Mesh23/type `36157f86`, source `2043ce10`; alpha envelope 3.39×12.97 m, foot Z=1153/65536; slot 70 BMP `065f127e`; MegaTreeLf1 whole-branch sprites; four lowest core cards fit visible alpha edge to the foliage floor (only transparent margin below; no UV/artwork change); native 90% silhouette-retention gate unchanged. |
| `TRGS18_1` / `LHRTWS4` (M01d-d) | 11 (Brands 7, Oulton 4) | 196 | Meshes 41/30, types `119e173f`/`09f18eae`; 26.25×14.04 m, foot Z=−5250/65536; separate palettes/alpha, original inset UVs (Oulton V differs 1/65536); MegaGrovLf4 + shared bark. |
| `TRGS22_1` / `LMRTS4` (M01d-e) | 29 (Brands) | 316 | Mesh38/type `f6e83713`, source `9aa00e17`; both sides visible, foot Z=−91/65536; slot 135 BMP `fdd0802f`, U opposite X; nine short feet/open gaps (painted bottom stems become feet); MegaGrovLm4. |
| `TRGS20_1` / `LMRTS2` (M01d-f) | 24 (Brands) | 236 | Mesh40/type `bbca0ae4`, source `13e0d1fa`; 26.247×14.038 m, foot Z=−5250/65536; slot 136 BMP `2bb5e0c1`, U increases with X; two joined narrow left crowns, separate wide right, three feet, 1.647 m source gap kept; MegaGrovLm2/Bark. |

### Woodland panels

Provenance: [porting-completed/scenery.md](porting-completed/scenery.md#woodland).

#### Mega simple Croft woodland

- FREE member2 meshes 23/24, nodes 127/128 form one closed 40-triangle
  body up to 0.9 m wide; four joined quads keep sloped base, UV repeats, slots,
  ownership across the original split diagonal; only a 1/65536 m X discrepancy
  welded; three height rows, one cell per quad; accepted 512×128 SAP1 gives
  foliage detail. Further geometry requires a visible defect at driving distance.

#### Mega Snetterton LLRTS2 woodland

- `TREES_20`, mesh19/node60, type `808e1de3`, source CRC32
  `51b727dc`; closed 14-triangle body; full panel, rising end-triangle boundary,
  ≤1.4 m depth, UV repeats, node data kept; 512×64 LLRTS2 WebP (`a7eca032`)
  reused; other LLRTS2 meshes keep source geometry.

#### Mega Snetterton LLRTSX woodland

- `TREES_1`, mesh0/node6/type `b0101817`; closed 6-triangle
  wedge (two per skin, two at right end); five welded positions keep ground tip,
  rising top edge, slot 2, UV domain `0<=-V<=U<=1`, ≤1.4 m width; 512×128 WebP
  `6ad9bec4`.

#### Mega Oulton woodland

- FREE member2 meshes 100/101, nodes 260/261 (types `0cb6c102`/`f4d5604b`),
  13/10 triangles; LLRTS1 panels share the 61.285° bend, continuous closed
  cross-section, ≤1.4 m width, 10 cm outer ends; mesh100's three LLRTS2 side
  faces exact. Meshes 14/15, nodes 63/64 (types `b98fa8bc`/`7595a58f`), 18/10
  triangles: three connected LLRTS1 panels, shared B/C joints, tapered A/D ends.
  Neighboring end triangles unchanged; panel UV repeats, slots, sloping feet,
  placements kept; other Oulton woodland unchanged.

### Roadside coverage audit — 2026-10-17

[Source inventory](porting-completed/scene-models.md#roadside-baseline) separates
shared geometry, materials and actual placements. Woodland covers all 15 material
slots; near-tree types above (incl. TRIS04B) are accepted; unconverted types,
remaining fences/people/buildings and static vehicles keep their owning TODOs in
the [scenery](mega-scenery-plan.md) and [vehicle](mega-vehicles-plan.md) plans.
Shared names/images or a generated GLB never imply completed geometry;
placements, transforms and slots stay per canonical track.

### Mega aircraft, helicopters and balloons

`mega_aircraft.py` rebuilds original HOR slots: aircraft, opaque PSP jet,
four-phase propellers/rotors, balloons, Audi airship. Source UV/livery/color
identities kept; PSP uses an appended opaque `MegaPSPSkin` atlas (not a CRC
override). 32 source-qualified redraws plus the atlas, WebP80. Six moving/five
helicopter paths reuse six payloads, keeping weather, route, visibility,
animation. Palette-255 CRC keys use the full original PSP1 bank. Wing/tail
sections reach the support centerline without changing span/bounds/paint
(48 attachment samples guard containment).
[Acceptance and wing correction](porting-completed/scene-models.md#hor-models).

## Original model consumers

`SceneView` combines terrain, selected car, Trees/Fence/Objects and horizon with
body/follow matrices. Row vectors, translation 12–14; resources outlive the view;
GPU textures and immutable model meshes can be shared by split views. No fitted
inspection scale or substitute light.

| Source | Consumer |
| --- | --- |
| `00494a40`, `0047f500` | Dynamic nodes keep parent hierarchy; world = local × parent × actor pose. Loader sets active geometry variant 0 (intact car); other variants stored for damage. |
| `004271d0`, `00427010`, `00427090` | Local player car: medium at detail 0/1, high at 2/3; only that model of the archive triple visible. Car IDs 0–9 keep their triple, incl. Tank and Flex. |
| `0044d7f9`, `00480920` | Medium-model wheel nodes 1/2 and 3/4 get original signed local-Z offsets. Audi, Vectra, Laguna own constants; others 0x7ae. Applied once, only to MEDIUM at detail 0/1, incl. rivals and ghosts. |
| `00494a40`, `00496980` | Root bit 29 = static baking. Root's raw 16.16 matrix carries into child meshes (no dynamic hierarchy). Each signed product shifted before wrapping add. Normals mutate cumulatively across repeated references. UVs from byte stream, fold U once, offset V, clamp .02–.99. |
| `00496c50`, `00488f59` | Static models: grayscale per-vertex light, alpha 255; no face 0x80 override (unlike terrain). TL draw passes colors directly; no extra face-color multiply. |
| `00487afe`, `0048e837` | Dynamic normals use node world basis, quantized angular table, nearest-rounded per-channel material diffuse + ambient. Textured → white, untextured → face RGB/alpha. |
| `0044eda9`, `0049da90`, `00478b20` | Donington light position (50,100,75); fixed midpoint-sqrt/reciprocal → first-light direction (24300,48600,36450)/65536. Weather 1–5 select diffuse/ambient tables `004af5e8`/`004af608`; diffuse × fixed 0xcccc, ambient × 255 then >>16. No float normalization. |
| `00431440`, `00431670`, `0048ea47` | Horizon root: no camera rotation; follows camera world position minus track-specific Y offset ([per-track horizon height](scene-rendering.md#per-track-horizon-height)); full affine inverse recovers position despite the non-unit quantized basis. Sky first, no dynamic lighting/far rejection, depth test/write and color key off, TL depth bits 0x3f7fff58. |
| `00489069`, `0048fae2(1)` | P8 index-zero key off in horizon pass, on for later textured model draws. Terrain/model passes share the source near clipping, projection, winding, RHW adapter. |

- `parse_static_model` returns original `Model` and baked `Terrain` from one CM3D
  decode, shared by Trees/Fence/Objects and prop loading. Terrain and static
  models share raw Q16 baking in `formats/cm3d_bake.hpp`; node traversal, sector
  registration, face-flag policies stay per consumer. Baked node/variant
  identities survive; canonical Donington Trees/Fence/Objects drawable nodes each
  have one slot. Dynamic car variants start at 0; dynamic Objects roots stay out
  of static baking. Detail selects the archive member. Sector order/LOD, props,
  weather, shadows: [shared consumers](scene-rendering.md#scene-visibility-and-sector-order).
- The hardware shader color/sampler contract is accepted; complete scene raster
  equality and translucent-window source comparisons remain open. Generic color
  uses host nearest rounding; the asin table retains its host-CRT limitation.
  Local 16-bit batches; no global 65,536-vertex frame limit.
- Race-model geometry/normals stay resident in object space; node matrices,
  light values, personal-name rows uploaded separately; `world.vert` keeps the
  recovered material rounding, ghost interior/alpha, sky depth, projection.
  CRC32 plus exact byte comparison shares identical corners/attributes across
  families and split views; adjacent identical ranges can be instanced;
  different materials, panes, overlay phases stop merging. No draw sorting. The
  [finale convoy and drivers](championship.md#final-convoy-and-credits) share this
  path with translation-first position arithmetic. Dynamic effects stream
  prelit triangles/lines through the same shader; geometry construction and
  queue decisions stay on the CPU.
- Track lighting/LOADHIGH constants: [scene resources](scene-resources.md#remaining-track-integration);
  [completion record](porting-completed/scene-models.md#original-consumer-acceptance).

## Qualifying side-view shadow correction

Honda's wheel/bumper opening is the canonical black-key cutout (confirmed in
original qualifying); no mesh/texture/winding correction. The undercar shadow
darkens the road visible through it. `437770` builds grounded, directional and
side-silhouette records; `498430` draws them before models using `4608b0`
contact-plane/body/sample-height inputs. Support normal persists while airborne,
updates on landing. LOWIMAGE0/1 supplies RGB/byte-index alpha; capability branch
disables depth test/write and vertical lift. Dry 1/2 use track sunlight/car
profiles; wet weather keeps the rectangle. Unwritten shrink scalar `4dbe74`
stays zero. [Arithmetic](visual-effects.md#original-vehicle-shadows),
[acceptance](porting-completed/scene-models.md#shadows-and-body-tilt).

## Contact-driven body tilt

`0040caf0` advances dynamics `+278/+27a` from tire/contact moments before
barrier/prop integration: thresholds `0x640000/0x320000`, opposite steps 4/2,
strict limits near ±20; no support resets both. `004608b0` applies absolute Z,
then local X to HIGH node 300, keeping its pivot. Body descendants tilt; physical
orientation and wheel pivots separate; MEDIUM no tilt. Imports/replays and
SceneView share the angles.
[Matched 25-node evidence and limits](porting-completed/scene-models.md#shadows-and-body-tilt).

## Brake, tail and headlight overlays

`00499580` RGBA colors repacked by `00499740` into D3D ARGB: brake `ffff0000`,
tail `ffc80000`, headlight `ffffffe6`. Original profiles keep untextured
polygons; Mega illuminates textured glass; all profiles use the halo. HIGH/Mega
lamps use animated body node 300, MEDIUM/LOW the physical root. Global detail
selects coordinate tables/lens sections even for distant rivals; mirrored sides
and damage masks intact. Human activation: raw brake `+144` → body `+4e8` →
`00446b50`: input ≥ `0x1999` and no reverse-pedal mapping; AI keeps its own
latch. Local/split/network presentation share this Motion publication.
[Vehicle lamps](visual-effects.md#vehicle-lamps), [artifacts](porting-completed/scene-models.md#lamps).

## Vehicle families and rival detail

- Actor 5 uses render model 10 (Vectra 2), physical car 2 for
  motion/audio/contact/wheel preparation. Both Vectra texture/model caches
  independent; model 10 not selectable. `004608b0` animates the hierarchy
  selected by LOD (incl. MEDIUM under detail 3); renderer/comparison share
  prepared wheel pivots.
- Mega: rival cutoffs 40/80 with all 16 HIGH/MEDIUM actors; detail 0–3 keeps
  original 20/40 ranges and 4-HIGH/5-MEDIUM limits. Strict squared-XZ cutoffs
  stay strict. Mega retains its native ten-dispatch hold; normal one-player
  NoQual View2 uses the source body observer and forced model dispatch instead.
  Only HIGH uses Mega geometry; MEDIUM/LOW canonical.
  [LOD/family evidence](porting-completed/scene-models.md#families-and-low-detail).
- ALLCARS inventory: 9,312 faces, all alpha 255, no DASH mesh. The
  capability-dependent DASH depth exception does not establish window blending,
  fog or sorting for ordinary cars. Matched cockpit/capability captures and
  complete original raster equality remain open; see
  [model/detail comparison](graphics-comparison.md#matched-vehicle-and-detail-comparison).

## Sky at low graphics detail

`00452376` disables static sky at detail 0/1 (loading/drawing honor it); moving
HOR keeps independent rules. Ordinary observed capability branch is used;
`0044f0a3` can force sky for another original capability. Low detail clears
through `0048728b/00486e75` to weather 1–5 RGB `(100,180,208)`, `(178,217,231)`,
`(164,173,190)`, `(74,97,107)`, `(40,49,58)`; Lavaland `(173,89,12)`. The
depth-disabled fill precedes normal scenery; detail 2/3 keeps textured sky.
[Acceptance](porting-completed/scene-models.md#families-and-low-detail).
