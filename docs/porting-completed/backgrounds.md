# Backgrounds: sources and completions

B00–B06 completed. [Background contract](../mega-backgrounds-plan.md). Recipes record image
CRC32, input identities, source bindings, import parameters.
Order was: inventory → DO02 pilot → track sets → extras/Lavaland → backdrops → lenses → B06.

## Inventory: which images are where

[Inventory](../../tools/inventory-backgrounds.py), [sky recipes](../../assets/models/mega/backgrounds-generation.json),
[lenses](../../assets/models/mega/atmosphere-generation.json),
[M02a recipes](../../assets/models/mega/woodland-generation.json).

### Sky and horizon in races

51 sky BMPs/43 unique members, 48 active. GP/Short share archives CRC32 `8771ca20`.
Inactive: TH02/BH03/SN02; Croft CR02 stays reachable in Championship meeting 6.
Rain/snow share XX04; check XX05/05L together; keep random cloud selection and
finale consumers.

### Distant scenery and other atmosphere images

FREE2/TREES stays M02; HOR0/1 are moving models. IMAGE640/320 members 38–46: sun/lens
inventory; BGLOW/HGLOW belong to vehicles. [Original evidence](../visual-effects.md#original-sun-and-lens-consumer).

## Image sizes, sources and rendering

Sky 512×512; forest up to 512×128 or 512×64; lenses 32–256 px. WebP80 (later WebP70), real
alpha plane kept, sky opaque. Templates/UVs exclusively from the unchanged 1.1a data. Own
palettes: BH04 = 149, CR04 = 212, CARTOON = 43 entries; no PSP1 replacement. UV wraparound seam,
cap region and storm consumers are inventoried.

Integration: HOR2–8 supply CRC32 texture replacement for `resources.horizon`; no `horizon.glb`;
`native_texture()` would be wrong because of black alpha; load only selected images; keep original
selection, geometry, depth. Sources: [resource loader](../../src/scene/resources.cpp),
[Mega builder](../../tools/build-mega-assets.py), [importer](../../tools/import-mega-textures.py).

## Acceptance per set

Image sources/CRC32/import parameters: [sky manifest](../../assets/models/mega/backgrounds-generation.json).

| Set | Facts |
| --- | --- |
| B01 DO02 pilot | Member 3 `fd1149f7`, BMP `87048995`, palette `2d92689f`; output 512×512/36528 bytes/CRC32 `806e8682`; GP/Short load the same cache content. Storm node correction with B02. |
| B02a Donington | Six DO images, both Short cloud variants, rain/snow, original storm node/UV switch. |
| B02b Silverstone | Six SI images, two cloud variants, SI02 in the Championship finale; seam/cap face. |
| B02c Thruxton | Five TH atlases; TH02 inactive; original lightning length, wraparound seam, cap face. |
| B02d Brands Hatch | Five BH atlases; BH03 inactive, BH04 with short palette; seam and lightning shared. |
| B02e Oulton Park | Six OP atlases; OP02 without visible seam (mean indicator 20, source distance/maximum value separate). |
| B02f Croft | Six CR atlases; CR04 with 212 palette entries, CR02 in Championship meeting 6; eight storm directions. |
| B02g Knockhill | Six KN atlases, two cloud variants; hill silhouette, raised horizon position, lightning position. |
| B02h Snetterton | Five SN atlases; SN02 inactive; low horizon, golden fields, narrow forest strips. |
| B03a Lavaland | LAVABAK 512×512, 27026 bytes; selection priority (Lavaland before menu weather/extras) kept. |
| B03b Starry sky | SILLYLAN 512×512, 10218 bytes; opaque black, shared file, extras selection. |
| B03c Cartoon | CARTOON 512×512, 12108 bytes; 43-color motif, selection with CMRAINUP/CMCATDOG. |
| B04 Scenery backdrops | Four M02a motifs generated once (commits M02a `8e8821f`, Croft `38204ed`, Knockhill `3e5e93f`, Snetterton `e54edb8`); shared Croft/Knockhill/Snetterton views; M02 owns geometry ([M02](../mega-scenery-plan.md#m02--forest-strips-and-missing-vegetation-images)). Foliage motifs and alpha from canonical templates; [foliage density](../../assets/models/mega/woodland-density-generation.json); body/UV contracts [scene-models](../scene-models.md#mega-simple-croft-woodland); shared images do not geometrically clear all 440 forest types. |
| B05 Lenses | Seven images, 3274 bytes encoded/495616 bytes RGBA8; overlay after 3D/before HUD; split screen/time trial blocked; SUN/WHITE2/SUNMASK suppressed in the hardware draw path; no new bloom. No true 2×MSAA resolve on the test GPU. |

## B02b Silverstone acceptance — 2026-10-17

Six SI images, two cloud variants, SI02 in the Championship finale (table above).

## B06 completion

59 files/1120078 bytes; 13 checks, 200 original/Mega pairs. Normal sky 1 MiB, storm
2 MiB, lenses 495616 bytes; largest selection Snetterton 2.75 MiB/SceneView; no decoding per
frame. Checks: `background-inventory`, `mega-assets`, resource/setup cases, background GPU cases,
`scene-reference-backgrounds-all` (explicit completion).
