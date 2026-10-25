# Mega Vehicle Plan

V00–V10 (eight touring cars, Tank, Flexmobile, Warwick/Vectra2) are registered in
the normal build. Open: K0 dashboards, F0, T17, G2.
[Model inventory](porting-completed/vehicles.md#exteriors) ·
[Build/load contract](cm3d.md#mega-glb-models) · [Work rules](../plan.md).

## Fixed Contracts

- Source `references/toca-1.1a/`; CRC32, model structure, artwork and fallback
  per the [implementation contract](cm3d.md#mega-glb-models).
- Physics, actor mapping, unlocks and animations are source-bound; presentation
  deviates only per the wheel rule.
- Keep original motifs, material slots, UV partitioning and real alpha openings;
  correct documented lamp/livery offsets locally; new images follow the
  [asset rules](../plan.md#assets).

## ALLCARS Inventory

| Render ID | Model |
| --- | --- |
| 0–7 | Honda Accord, Audi A4, Vectra (Cleland), Volvo S40, Ford Mondeo, Nissan Primera, Peugeot 406, Renault Laguna |
| 8 / 9 | Tank / Flexmobile (replace the Ford actor pair when unlocked) |
| 10 | Vectra2 (Warwick): actor 5, physical car 2, own pivots/decals/livery |

- Definitions `assets/models/vehicles/<key>/definition.json`, registration
  `assets/models/mega/vehicles.json`; member order, 33 LOD members, CRC32,
  hierarchy, slots: `tools/inventory-vehicles.py`.
- Output `car-<id>.glb` (WebP80, Meshopt); detail 0–3 and original MEDIUM/LOW
  unchanged; original HIGH fallback.
- Image sources: 62 lossless WebP under `textures/work/`; finished WebP are
  embedded unchanged. `tools/vehicle_textures.py` finalizes Honda, Audi,
  Vectra, Volvo, Ford, Nissan, Peugeot, Flexmobile (export quality; Peugeot
  digit canvas as before).
- Finalization recipes record the image sources with CRC32 and export quality.

## Wheel and Body Clearance

- Touring cars (V00–V07, V10): nominal wheel radius 350 mm, width 250 mm.
  Definition `wheel_outset` = symmetric outer wheel-node offset per axle
  (`tools/vehicle_wheel_stance.py`), max. 100 mm; wheelbase, height, base and
  physics original.
- Wheel-arch openings follow the body, inner surfaces follow the visible wheels.
  Only generated inner/cavity surfaces carry `toca_ghost_hidden`.
- Mega ghost: inner surfaces with body-fixed `_TOCA_GHOST_POSITION` (20 % inner
  depth), alpha 96 instead of 150; ghost wheels alpha 96; the opaque model keeps
  its full shape.
- Do not trace coarse source cutouts back via geometric wheel arches.
- Check on changes: paint/inner surfaces for clearance at both steering limits
  and during wheel rotation; same camera/lighting, both sides, front/rear,
  detail, damage, actual LODs. Renew garage contributions per the
  [garage contract](mega-garage-plan.md).
- Tank/Flexmobile, finale and track vehicles: source-bound special dimensions.

## Order

K0/K vehicles → F0 → G2. T01–T16 accepted, T17 open. Agent A:
definition/geometry/bindings; Agent B: artwork/UV/provenance; Main: build/loader,
acceptance, commit ([plan.md](../plan.md#zusammenarbeit-und-prüfungen)).

## Finale Convoy (S01/S02)

Source `data/champfin.bfp` (`28f9b97c`), members 0/1; [registry](../assets/models/mega/finale.json),
[sources/bindings](../assets/models/mega/finale-generation.json). `ChampionshipFinaleView`
uses the two convoy actors; outputs `finale/pace-car.glb`, `finale/transporter.glb`
via Mega detail 4, original fallback per role.

| Package | Facts |
| --- | --- |
| S01 Pace Car | Yellow Audi A4, red checkerboard; eight nodes, four wheel pivots; four WebP70 atlases replace five slots (PACE0/4 share one image); license plate `PCE 97` reconstructed. Wheel Ø0.699982 m, width 0.25 m, wheelbase 2.609985 m, track 1.399994 m; closed 32-sided wheels, four wheel-house backs; source underbody kept, no touring-car clearance rule. |
| S02 Transporter | White-blue stair transporter, one WebP70 atlas; logo `RSL` (Racing Support Logistics) reconstructed; seven nodes, driver mounts, source UV/alpha kept. Wheel Ø0.923981 m, width 0.329987 m, wheelbase 4.441986 m, track 2.112 m; wheel body follows the source photo center (old heptagon run offset by 0.022873 m), node pivots unchanged. F0 driver artwork remains a separate open task. |

## K0 and Per-Vehicle Interiors

Honda is a complete K00 cockpit; steering wheel/wipers and hands/forearms of all
nine interiors are implemented; dashboards of the other eight interiors remain
open. Exterior models and the Mega HUD do not cover this remaining work.

Nine DASX archives provide five slots each: left hand+forearm, right hand+forearm,
dashboard, wiper, steering wheel (45 slots, 29 distinct model payloads).
Steering wheel and wiper are byte-identical in all nine archives; the other 27 full
model members differ. Archive CRC32, side assignment:
[cockpit audit](cockpit.md#asset-and-driver-side-audit),
[inventory](../tools/inventory-cockpits.py),
[Honda definition](../assets/models/cockpits/honda/definition.json).

Shared cockpit loader: all five roles for players 1/2; role 2 (dashboard) is
registered for Honda only, the others keep the original dashboard. Missing/damaged/
incompatible role → complete original of that role, detail 4 only; other
roles and the CRC32 material pass are independent. Roles 3/4 are parsed once per
session across vehicles for identical source bytes/replacement paths;
`SessionModels` shares vehicle/cockpit data per render ID, `ModelTextures` GPU
images/lamp derivations; animation/visibility state per player. Outputs
`cockpit/<key>/`. `cockpit_left_hand_drive()`: flag 0 = right-hand drive, flag 1 =
left-hand drive.

### K0-S Shared Steering Wheel and Wipers

2788T / 181T; 512×512 / 512×32, alpha, WebP80, Meshopt; original pivot,
grip/UV/motion basis; original rank-deficient wiper transform basis kept.
Rain/snow/extras masks, head motion, near clip unchanged.
[Recipes](porting-completed/vehicles.md#shared-cockpit-parts).

### K0-H Hands and Forearms

Shared geometry with Audi offset, six mesh slots/four material slots per hand,
7720T, 36 WebP80 images; nine original material/pattern sets. Shoulder joint,
grip basis, both driver sides and Flex lever original; Flex left at the original
lever. Grips behind the real rim over the whole steering range; shift commits
Reach/Hold/Return. [Evidence](porting-completed/vehicles.md#shared-cockpit-parts).

### K0 Shared Infrastructure and Remaining Dashboard Work

- [ ] **K0d dashboards/textures:** For the other eight vehicles, regenerate
  dashboard surfaces, scales, existing lettering/logos with Imagegen from local
  original sources, keep UV/alpha partitioning, output WebP70.
  Accept lettering from the driver view. The documented 3D dash stays
  static; actual speed/RPM/gear are shown by the existing Mega HUD. Moving 3D
  needles/displays would be a separate new feature task.

Dashboard XYZ: two groups documented, Nissan with separate UVs. Cockpits change
neither wheel dimensions nor presentation track; view 1 hides the exterior car as
in the original.

### K00–K09 Vehicle Packages

Each package: source/reuse mapping → geometry → vehicle-specific
textures/provenance → shared load/build path → actual interior view including
K0 exterior connection → acceptance/commit. Complete one package fully, then the
next. Do not generate the shared steering wheel/wiper nine times.

- K00 Honda: four WebP70 dashboard textures, original front with thin closed
  backs, all five roles; [definition/material slots](../assets/models/cockpits/honda/definition.json).
- [ ] **K01 Audi:** edit `data/audidasx.bfp` through the full cockpit flow including K0 exterior connection.
- [ ] **K02 Vectra:** edit `data/vectdasx.bfp` through the cockpit flow including K0 exterior connection.
- [ ] **K03 Volvo:** edit `data/volvdasx.bfp` through the cockpit flow including K0 exterior connection.
- [ ] **K04 Ford:** edit `data/forddasx.bfp` through the cockpit flow including K0 exterior connection.
- [ ] **K05 Nissan:** edit `data/nissdasx.bfp` through the cockpit flow including K0 exterior connection.
- [ ] **K06 Peugeot:** edit `data/p406dasx.bfp` through the cockpit flow including K0 exterior connection.
- [ ] **K07 Renault:** edit `data/rendasx.bfp` through the cockpit flow;
  check the K0 exterior connection; the earlier exterior-model pilot does not cover this work.
- [ ] **K09 Flexmobile:** edit `data/flexdasx.bfp` with documented interior/hand/shift logic and K0 exterior connection.
- [ ] **K08/K10:** Check existing behavior without own cockpit archive path for Tank
  and render ID 10, including K0 exterior connection; do not add invented interiors.

## F0 Driver Figures of the Finale Sequence

These companion models are not further vehicles. Edit per member Q/G/T/I/A in the
finale context and tick off separately from the transporter status:

- [ ] **F0 integration:** Main extends the build/override/fallback path introduced
  in S01 for the eight figure slots. Planned outputs are
  `assets/models/mega/finale/driver-<member>.glb` for members 2–9. Secure
  bundling, structure check and original fallback per slot; only then integrate
  individual figure packages on top and accept them in the finale sequence.
- [ ] Member 2: `HONDADR_1`.
- [ ] Member 3: `AUDIDR_1`.
- [ ] Member 4: `S40DR_1`.
- [ ] Member 5: `VECTRADR_1`.
- [ ] Member 6: `NISSANDR_1`.
- [ ] Member 7: `MONDR_1`.
- [ ] Member 8: `RENDR_1`.
- [ ] Member 9: `P406DR_1`.

Keep the existing selection `2 + actor/2`, even though the archive order partly
differs from the racing-team order; see [finale](championship.md).
Member 10 contains credits and is not part of these model tasks.

## T0 and Static Track Vehicles

T01–T16 accepted. Inventory: 30 HIGH bindings, 27 source types, 15 model names;
117 of 118 BMP identities replaced, `D15SHL7` (`1c07ca8e`, face 69, constant
source UV) remains the original constant sampler. HIGH/LOW/ZERO of all ten
archives reconciled; no LOW-/ZERO-exclusive names or unassigned shape
duplicates. Mesh indices belong to member 5 `OBJECTS.C3D` of the respective
`data/<archive>.bfp`. Exact CRC32/bindings: [scenery inventory](../tools/inventory-scenery.py)
(`package: T0`), [M00](mega-scenery-plan.md#m00--concrete-inventory-and-first-handover).

Rules: keep scale, locations, material motifs, UV connections, instance/sector/transform/
LOD/collision data; improve geometry only if shape, thickness or backs
visibly gain; add tire reliefs and closed undersides.
Share common shapes/images; shared side images keep UV mirroring on the
opposite side. Lettering reconstructed legibly, no literal source transcripts.
Buildings/masts (`DA44TRNS_1`, `DA80FOSC_1`, `SI06TRA1/2/4_1`, `SI13ROV_1`, `TV_1`,
`TV01_1`, `TH03AIRP_1`) belong to M07/[R01](mega-tracks-plan.md#r01--silverstone).
Moving HOR flying objects are done (33 images, six shared payloads; Donington
shares two jets, HOR helicopter family empty); do not re-commission.
[Technical reconciliation](porting-completed/track-assets.md#donington-t0-inventory-check).

| Package | Model | OBJECTS mesh indices per archive | Output / sources |
| --- | --- | --- | --- |
| T01 | `LVECTRA_1`, VECTRA advertising truck | branfree 31; croffree 18; knocfree 32; snetfree 42 | 21 WebP80 for 22 BMP identities; 12 HIGH locations; [static-vectra](../assets/models/mega/static-vectra-generation.json) |
| T02 | `LVOLVO_1`, Volvo box truck | branfree 32; croffree 19; knocfree 29; snetfree 41 | T02–T08 total: 58 WebP80 + 3 reused T01 Volvo images, 62 BMP identities, 36 material roles, 13 HIGH bindings/28 locations; [static-fleet](../assets/models/mega/static-fleet-generation.json) |
| T03 | `LAMBULAN_1`, red ambulance (REDLINE RESCUE) | branfree 45; croffree 7; knocfree 30 | see T02 |
| T04 | `LFIRETRK_1`, fire-service SUV (FIRE RESCUE) | knocfree 11; snetfree 26 | see T02 |
| T05 | `LRACJEEP_1`, track-support SUV (RACE SUPPORT) | snetfree 34 | see T02 |
| T06 | `LCAR1_1`, flat parked car row (4V/4T, double-sided) | branfree 47 | see T02 |
| T07 | `CR11CVAN_1`, caravan | croffree 1 | see T02 |
| T08 | `CR03BUS2_1`, green double-decker (GL/Greenline) | croffree 9 | see T02 |
| T09 | `DA12SPTF_1`, static Spitfire | donlfree/donsfree 2, type `e8a3b83b` | 5 WebP80; [sources](../assets/models/mega/donington-static-spitfire-generation.json) |
| T10 | `DA41TRA_1`, transport truck "Silverline Motorsport" | donlfree/donsfree 34, type `7653b1bc` | 6 WebP80; [sources](../assets/models/mega/donington-static-truck-generation.json) |
| T11 | `CRAN3_1`, SKY LIFT aerial platform | donlfree 55; donsfree 57, type `2b9bfb4e` | 2 WebP80; [sources](../assets/models/mega/donington-static-platform-generation.json) |
| T12 | `CARROW_1`, photographic parked row | silvfree 26, type `fbf92d23` | 3 WebP80 on cross surfaces, double-sided source phase; FREE6/7 without type; [sources](../assets/models/mega/silverstone-parked-cars-generation.json) |
| T13 | `BH17HEL_1`, static helicopter | silvfree 18, type `da357b54` | 4 WebP80 (3 HOR images as detail basis), four HIGH locations, LOW/ZERO only the first; [sources](../assets/models/mega/silverstone-static-helicopter-generation.json) |
| T14 | `TH36APLN_1`/`TH37APLN_1`, twin-/single-engine aircraft | thrufree 10/12, types `9347a89e`/`b8ad1d9a` | 8 WebP80 (5 HOR detail bases + 3 additional motifs), thin closed wings, ten HIGH locations; [sources](../assets/models/mega/thruxton-static-aircraft-generation.json) |
| T15 | `CRAN3_1`, shared SKY LIFT platform | thrufree 41, type `37fdcb96`; oultfree 28, type `6c721efc`; shape shared with T11 | 4 WebP80 Thruxton/Oulton, corrected Donington side alpha, six wheels on three axles, tire relief ≤ 3 cm, eleven HIGH locations; [sources](../assets/models/mega/static-platform-generation.json) |
| T16 | `BH17HEL_1`, Brands helicopter | branfree 34, type `2ae9351e`; shape shared with T13, own UVs/palette | 4 WebP70 from T13 details, 86V/70T, LOW two locations, ZERO without type; [bindings](../assets/models/mega/brands-static-helicopter-generation.json) |

Donington: 13 images (T09–T11), one replacement set for GP/Short with the respective original slots.

- [ ] **T17 Croft transporter:** `BH06WT2_1`, FREE5 #17, type `d217fa62`,
  four bindings 131/133/145/147. Six original `BH6W*` slots and
  existing body unchanged; own vehicle acceptance, outside M07.

## G2 Shared Final Acceptance

- [ ] Check wheel/body connections of all exterior packages per the
  [geometry contract](#wheel-and-body-clearance); associated garages are current.
- [ ] All eleven registered ALLCARS outputs are buildable, bundled, valid and
  visible in the correct consumer; the two Vectra liveries stay separate.
- [ ] Check original fallback for missing/invalid files and with Mega generation
  switched off. Detail 0–3 and existing distance LOD stay correct.
- [ ] Measure one dense 16-vehicle race with complete Mega resources and one
  split-screen case: frame time, memory, texture reuse and draw batches.
  Mega uses distance LOD at 40/80 units without the original 4/5 vehicle limit.
  Optimize measurably if needed,
  then repeat only the affected checks.
- [ ] Cover ghost, both local players, bonus selection, damage and LOD switches
  with the complete fleet. Audio and driving states must not change through assets.
- [ ] Accept finale vehicles/drivers and all nine cockpit packages through their
  actual flows; check static vehicle types at the confirmed instances.
- [ ] Complete the G0 inventory; T01–T16 are accepted, T17 stays separately open.
- [ ] Update the shared inventory and remaining limits; close the parent
  plan only after full integration and acceptance.

Checks (`make check TESTS='...'`, beforehand `CTEST_ARGS='-N'`): `vehicle-model-generator`,
`vehicle-textures`, `vehicle-model-sync`, `vehicle-reference-inventory`,
`vehicle-model-<renault|honda|audi|vectra>`, `<honda|audi|vectra|renault>-model-generator`,
`<honda|audi|vectra|renault>-textures`, `native-<audi|vectra>-model`,
`renault-glb-generator`, `formats-glb`. No track/weather matrix per vehicle.
