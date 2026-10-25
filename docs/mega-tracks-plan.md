# Mega Track Plan: Assets and Reuse

Local acceptance per track. S04 owns ground images, M00–M08 objects, B00–B06 skies;
R00–R09 check their local integration, R10 the overall completion. Vehicles/cockpits
belong exclusively to the vehicle plan.
[Porting plan](porting-plan.md), [ground](mega-track-materials-plan.md), [scenery](mega-scenery-plan.md),
[backgrounds](mega-backgrounds-plan.md), [vehicles](mega-vehicles-plan.md#t0-and-static-track-vehicles),
[work rules](../plan.md), [check profiles](porting.md).
[Inventory/reuse evidence and CRC32](porting-completed/track-assets.md).

## Scope, sources and status

- [x] **Texture review of all pure track objects:** [MT00–MT05](mega-scenery-texture-review.md)
  completed on all ten tracks, including existing/shared images.

All profiles use the same GROUND; the three original detail levels affect objects only.
Ground images/junctions of all ten tracks: [completion](porting-completed/ground-materials.md);
[ground geometry](mega-track-materials-plan.md#optional-step-ground-geometry)
remains an optional follow-up. All 48 active skies, seven lenses, four
added forest motifs, active HOR models, pylons/posts/signs and selected
advertising/tyre/steel barriers are present
([inventory](porting-completed/track-assets.md#what-already-exists)).

Source `references/toca-1.1a/`, zero-based FREE members: 1 ground, 2 forest, 3 FENCE,
5 full objects, 8 pylons, 9 posts, 10 signs, 11 flags; HOR 0/1 moving models,
other members skies. Prioritise textures, keep simple bodies/silhouettes;
compare original → new images → prototype with identical cameras, including driving distance.

| Authoritative inventory | Use |
|---|---|
| [Ground TSV](mega-ground-textures.tsv) | All 135 slots, source/image CRC32, sizes, use |
| [Scenery inventory](../tools/inventory-scenery.py) | 2014 mesh rows; filter by `meshes[].track`/`package`, check `type`/`nodes` |
| [Object acceptance](../assets/models/mega/scenery-acceptance.json) | Exact types and acceptance status |
| [Sky inventory](../tools/inventory-backgrounds.py) | Active weather/extras images |
| [Image mapping](../assets/models/mega/texture-replacements.json) | Active replacement |

"Integrated" = active replacement, "generated" = image without integration/acceptance, "open" = task.
Names without visual evidence remain candidates; a GLB/image mapping does not close a model family.
The complete remaining inventory via inventory filters stays mandatory
([contract](porting-completed/track-assets.md#scope-sources-and-status)).

## Shared model and placement review

- [x] Review existing shared models in the R packages with their confirmed
  material variants, local bindings and junctions.
- [x] Accept own slots, instances and placement differences per track;
  reuse shared types and do not remodel them.

Keep simple bodies and original silhouettes. A finished asset does not replace
the local R acceptance.

## Shared assets

Share directly only for identical geometry/UV/material data; otherwise share only the construction method.
GP/Short share 15 ground images as well as FREE2/3/11, 252 exact types and the DO sky;
OBJECTS as a whole differ. Other tracks have no further identical
ground BMPs. Brands/Lavaland share two UK flag phases (hierarchy separate) and
flag images; Brands/Croft confirmed advertising/T0 images.
[Exact types, sources, CRC32](porting-completed/track-assets.md#shared-assets).
Global image mapping acts by original BMP CRC32: capture all consumers, names
alone are not sufficient. Assign `CHEV`/`POST` to the actual object/
flag consumer before commissioning.

### Image demand per track

Ground is fully integrated. Bound BMPs are review inventory; only
[visible scenery consumers](porting-completed/track-assets.md#image-requirements-per-track)
trigger image tasks. Do not sum shared sources multiple times; additional atlases,
props 8–10, ground and HOR excluded.

## Deliverables and task split

S04 images: source-faithful WebP70 (method 6, max. 512 px) with recipe/CRC32. Build products
`build/<config>/generated/mega/scene/track-N/{trees,fence,objects,cones,posts,signs}.glb`,
M06 `flags.glb`; ground images `scene/<bmp-crc32>.webp`. Keep original slots, alpha, lettering,
silhouette, instances, hierarchy, sectors, contact/animation data;
HPC/POINTS/grip/road heights unchanged. Limits: 512 px/256 KiB per object texture,
2048 extra triangles/mesh, 48000/family, 8 MiB/GLB, `EXT_meshopt_compression`.
[Delivery contract](porting-completed/track-assets.md#delivery-scope-and-task-allocation).
Agents: two per subpackage, disjoint model/artwork or material/recipe lists; main owns shared manifests, builder/loader, native acceptance, package commit.

## Track completion R00–R10

R00–R10 are complete. Shared types and local variants are
reconciled via exact source bindings and existing functional evidence.

- [x] **R06 Croft:** All pure track objects and local materials accepted.
- [x] **R10 Overall completion:** All 1983 pure mesh rows/1580 types/5294
  raw bindings accepted; image bindings, local variants and existing
  driving/audio/split evidence reconciled. Vehicles/cockpits and general
  original-renderer parity stay with their own tasks.

All ten tracks are accepted (table below).

| Track | ID/prefix | FREE CRC32 | Inventory (mesh rows/types/bindings; source images) | Notes / recipes |
| --- | --- | --- | --- | --- |
| R00 Donington GP | 0 `donl` | — | 141 source materials + four leaf images | [track-assets](porting-completed/track-assets.md#r00--donington-gp) |
| R05 Donington Short | 5 `dons` | — | 136 shared source materials + four leaf images from GP; own OBJECTS slots/hierarchies/placements; `CHERV_1`, `LARROW5_1`, DO03 local | [track-assets](porting-completed/track-assets.md#r05--donington-short) |
| R01 Silverstone | 1 `silv` | `960a335c` | 171/–/419 (FREE2/3/5/8/9/10); 130 BMPs; FREE11 empty; `BH17HEL_1`/`CARROW_1` T0 only | [rest](../assets/models/mega/silverstone-rest-generation.json); 31 building types/85 locations, 90 spectator groups, 42 advertising boards, 8 arrow carriers, 51 cones/posts/signs, 93 barrier meshes, 15 marshal groups, 5 tents; 19 source images + 5 alpha masks; `TRIS04_1` ×4 with own leaf palette, 26 forest meshes; shared `TRACK2` uses the ground image unchanged; Luffield advertising boards: backplates ≤ 4 cm, at equal D16 depth the advertising face takes priority (`scene-reference-banner-backing`, `mega-geometry`); [sky](porting-completed/backgrounds.md#b02b-silverstone-acceptance--2026-10-17), [driving](scene-resources.md#silverstone-integration), [split](split-screen.md#finish-and-restart-behavior) |
| R02 Thruxton | 2 `thru` | `473c8fe8` | 210/187/512 (FREE2/3/5/8/9/10/11); 134 BMPs, 131 replaced; `TH15ROOF`, `BLAK`, `POST` native solid-colour samplers | [props](../assets/models/mega/thruxton-props-generation.json) (14/72), [buildings](../assets/models/mega/thruxton-buildings-generation.json) (37/55), [structures](../assets/models/mega/thruxton-structures-generation.json), [vegetation](../assets/models/mega/thruxton-vegetation-generation.json) (62/105, 59 forest meshes), [rest](../assets/models/mega/thruxton-rest-generation.json) (97/74/280, 24 images from 23 RGB templates; spectators share RGB, own masks; 24 flag controllers); `TH02` inactive; [driving](scene-resources.md#thruxton-integration) |
| R03 Brands Hatch | 3 `bran` | `ecc577c7` | 172/169/684; 164 images (161 global, 1 bridge only, 2 native constants) | [props](../assets/models/mega/brands-props-generation.json) (16/70, 17), [vegetation](../assets/models/mega/brands-vegetation-generation.json) (68/197, 8), [rest](../assets/models/mega/brands-rest-generation.json) (62/59/377, 31), [buildings](../assets/models/mega/brands-buildings-generation.json) (26/40, 108; twelve bridge faces with appended material alias, helicopter colour swatch native unchanged); `BH03` inactive |
| R04 Oulton Park | 4 `oult` | `e6e258cb` | 319/313/723; 136 images (134 mapped; `BLAK` `c4231456`, `POST` `42a1efc1` native) | [nature](../assets/models/mega/oulton-nature-generation.json), [pits](../assets/models/mega/oulton-pits-generation.json), [buildings](../assets/models/mega/oulton-buildings-generation.json), [rest](../assets/models/mega/oulton-rest-generation.json) (42/36/310, 40 images, 12 flag phases), DHL arrow signs [small-props](../assets/models/mega/silverstone-small-props-generation.json); ground/water/kerb junctions; `OP02`/`OP03` |
| R06 Croft | 6 `crof` | `e4399113` | 159/155/603; 83 images, 82 mapped + native POST constant; ground and CR sky accepted | see below |
| R07 Knockhill | 7 `knoc` | `7e00bade` | 168/144/507; 115 images (114 mapped, `POST` `a645301c` native) | [buildings](../assets/models/mega/knockhill-buildings-generation.json) (17/27), [nature](../assets/models/mega/knockhill-nature-generation.json) (66 barrier meshes, `SHEEP2/7` pairs, `SHEEP4/8` single, 20 bindings; wrong `MOBILBK` logo replaced by brick), [rest](../assets/models/mega/knockhill-rest-generation.json) (44 flag rows/20 types/27 controllers, 18 images, 6 nations); `KN02`/`KN03` |
| R08 Snetterton | 8 `snet` | `4111866a` | 160/158/573; 163 images (161 mapped; `BLAK` `67c1dd9b`, `POST` `16ef43b7` native) | [buildings](../assets/models/mega/snetterton-buildings-generation.json) (24/53), [nature-rest](../assets/models/mega/snetterton-nature-rest-generation.json), [flags](../assets/models/mega/snetterton-flags-generation.json) (21 controllers, 9 images, 21 init RNG calls); 103 WebP (96 new, 7 pure alpha variants with unchanged RGB); two small blue snack-bar inscriptions as limited reconstruction; `SN02` inactive |
| R09 Lavaland | 9 `land` | `dba52c51` | 101/22/152; 23 images (22 mapped; `POST` `22d32fc3` native unchanged) | [rest](../assets/models/mega/lavaland-rest-generation.json) (3 FENCE sections, 2 `TEMPLE_1`, 5 WebP70), [flags](../assets/models/mega/lavaland-flags-generation.json) (96 rows/17 types/24 controllers, 15 images; twelve source CRC32 added), [gargoyle](../assets/models/mega/lavaland-gargoyle-generation.json); TREE/POST/SIGN empty; `LAVABAK` |

All accepted tracks: geometry, UVs, slots, hierarchies, placements and
flag phases (three phases, four-tick change) unchanged; T0 vehicles stay in the vehicle plan.

## R06 — Croft

Complete. Track6, prefix `crof`, FREE archive CRC32 `e4399113`.
159 pure mesh rows/155 types/603 raw bindings and 83 source images reconciled:
82 direct/reviewed image mappings plus native mast constant `POST` (`e5f30441`).

- Three pit/control buildings and four TV structures keep original bodies,
  UVs, slots and gate sequences. `LVECTR2`, roof/cap panels and the transparent
  TV point sampler are preserved.
  [Building/material recipe](../assets/models/mega/croft-buildings-generation.json).
- Forest, near trees and 60 barrier meshes checked with local palettes, alpha,
  junctions and repeats. Tyres have four side rows and
  two figure-eight rows on the top faces; `MOBILTOP` is unlettered wood.
- Spectators, crew, containers, tents/tables and nine flag phases accepted with their
  source masks and original poses/hierarchies. Reviewed shared
  RGB payloads stay unchanged; local alpha variants change only the mask.
- Cones, posts and nine signs use the simple original bodies
  and UVs again; additional generic frames removed. Advertising-board backs and
  `CHERV` bodies are preserved; missing `BH31BCK` is assigned.
  [Shared rest recipe](../assets/models/mega/croft-rest-generation.json).
- Native near/oblique/rear/driving views passed at representative consumers.
  Ground/object junctions, active CR skies and existing driving/audio/split
  evidence, including Championship `CR02`, remain valid.

No open local asset findings. Five Croft T0 types remain accepted;
the transporter `BH06WT2_1` belongs separately to
[T17](mega-vehicles-plan.md#t0-and-static-track-vehicles).

## R00 — Donington GP

Track 0, `donl`; table above.

## R05 — Donington Short

Track 5, `dons`; table above.

## R01 — Silverstone

Track 1, `silv`, FREE CRC32 `960a335c`; table above.

## R02 — Thruxton

Track 2, `thru`; table above.

## R03 — Brands Hatch

Track 3, `bran`; table above.

## R04 — Oulton Park

Track 4, `oult`; table above.

## R07 — Knockhill

Track 7, `knoc`; table above.

## R08 — Snetterton

Track 8, `snet`; table above.

## R09 — Lavaland

Track 9, `land`; table above.

## Acceptance per subpackage and track

Contract for future changes; no open rollout tasks.

- **Q — Task complete:** Archive/member/mesh/nodes, original materials,
  CRC32, visible consumers, existing replacement images and reuse
  documented. Remaining inventory rows each accepted, a concrete follow-up task,
  or excluded with a stated missing consumer.
- **G/T — Asset finished:** Model and images checked against the template;
  new surfaces are real geometry, additional materials documented. Original slots,
  alpha, lettering, silhouette, ground junction and drive-through match.
- **I — Integrated in game:** Bundled resources actually loaded;
  all confirmed shared instances reached. Original detail levels 0–3,
  contacts, animation, weather/extras selection and existing audio paths preserved.
- **A — Targeted checks:** For ground `mega-assets` plus
  `scene-reference-ground-materials` for GP/Short or
  `scene-reference-ground-materials-trackN`; for models the matching geometry,
  resource and object GPU checks. M06 adds `flag-animation` and a
  GPU comparison of all three phases. Select with `make check TESTS='...'`.
- **Track view:** Compare original, new images on original geometry and complete
  Mega with identical camera/light. Check near/driving distance,
  backs, sector boundaries, repeats and motion at 1080p/1440p.
  Existing matching driving/audio/split evidence remains valid; only relevant
  integration changes or concrete defects justify a repeat.
  Muted private Xvfb, game frames only.
- **Completion:** Update file CRC32, asset sizes and quality evidence in the existing
  S/M/B/T plans, and only then close Rxx. Performance measurements
  are not an acceptance item. Full native
  track/weather matrices belong to the justified overall milestone, not to
  each object.
