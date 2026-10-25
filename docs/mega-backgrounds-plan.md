# Mega In-Game Backgrounds, Skies and Horizons

Source, image and integration contract for skies/horizons/backdrops visible while
driving; menus/garage are separate packages. No open background package.
[Porting plan](porting-plan.md), [work rules](../plan.md), [check profiles](porting.md),
[completions and CRC32](porting-completed/backgrounds.md). [Scenery plan](mega-scenery-plan.md):
woodland geometry; [vehicle plan](mega-vehicles-plan.md): vehicles/finale figures.

## Inventory: Which Images Are Where

[Reproducible inventory and sources](porting-completed/backgrounds.md#inventory-which-images-are-where).

### Sky and Horizon in the Race

Source `references/toca-1.1a/data/<prefix>hor.bfp`, zero-based members:

| Member | Original consumer |
|---|---|
| 2 / 3 / 4 | Sunny XX01 / cloudy XX02 / cloudy XX03 |
| 5 | Rain/snow shared XX04 |
| 6 | Storm XX05/05L, original lightning node switch |
| 7 / 8 | CMSTARS/SILLYLAN and CMTOON/CARTOON |

51 sky BMPs/43 unique members, 48 active; GP/Short share the DO set.
`TH02`, `BH03`, `SN02` inactive. Short/Silverstone/Oulton/Knockhill: two cloud variants;
Croft `CR02` reachable in Championship meeting 6. Lavaland takes precedence over
weather/extras. 24 source triangles, storm 48; UV atlases with all-around seam and a
small top cap region, no panoramas.
[Archives, CRC32, selection](porting-completed/backgrounds.md#sky-and-horizon-in-races).

### Distant Scenery and Other Atmosphere Images

Woodland in FREE member 2 TREES/M02; `SAP1`, `trge31`, narrow `LLRTS2`/`LLRTSX` replaced.
Active flying objects HOR members 0/1. IMAGE640/320 members 38–46: sun/lens inventory,
seven lenses active, BGLOW/HGLOW belong to vehicles. Lightning/precipitation keep their
effect path. [Inventory, original CRC32](porting-completed/backgrounds.md#distant-scenery-and-other-atmosphere-images).

## Image Sizes, Sources and Presentation

WebP70, method 6. Sky 256×256 → 512×512; woodland 128×32 → max. 512×128, 128×16 →
512×64; lenses 32–256 px. Max. 512 px/256 KiB per track image; colors, silhouettes,
horizon height and UV layout preserved. Skies, including black, opaque; real
masks/transparency preserved, alpha quality 100. Imagegen with actual original/UV
templates (references only under the ignored `build/`); sources, palettes and outputs
with CRC32.
[Recipes and limits](porting-completed/backgrounds.md#image-sizes-sources-and-rendering).

## Sources and Integration

[Inventory](../tools/inventory-backgrounds.py),
[generation recipes](../assets/models/mega/backgrounds-generation.json). BH04, CR04,
CARTOON have their own short palettes; no PSP1 palette substitution.

[Loader](../src/scene/resources.cpp): `apply_mega_scene_resources`/`replace_model`
replaces `resources.horizon` via `scene/<original BMP CRC32>.webp`.
[Builder](../tools/build-mega-assets.py)/[importer](../tools/import-mega-textures.py)
capture HOR2–8; `texture-replacements.json` binds source/name/size/CRC32;
store shared images once; no `horizon.glb`.
The black-alpha rule of `native_texture()` must not apply to skies.
Detail 0/1 flat colors, 2/3 original images; selection, camera, horizon height and
depth handling are original. Load selected images only.

Seven lenses after 3D/before HUD; split screen and time trial disable the effect.
SUN/WHITE2/SUNMASK suppressed in the hardware path. [Original contract](visual-effects.md#original-sun-and-lens-consumer).

New objects/local checks: [scenery plan](mega-scenery-plan.md),
[track plan](mega-tracks-plan.md). Do not release other woodland types as geometry in
bulk; further woodland work only for visible defects at normal driving distance and
with simple bodies.
