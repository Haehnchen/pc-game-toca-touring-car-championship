# CM3D car models

## Mega GLB models

All eleven ALLCARS exterior resources are registered in
[vehicles.json](../assets/models/mega/vehicles.json); package records:
[exteriors](porting-completed/vehicles.md#exteriors). Render IDs 0–9 are
selectable cars; actor 5 uses Vectra2 render ID 10 with physical car 2 (own
canonical transforms/artwork). Tank/Flexmobile keep their unlock and
Ford-actor replacement behavior.

### Build and loading

- `cmake/VehicleModels.cmake` generates each `car-<id>.glb` from its
  definition/artwork under `assets/models/vehicles/<key>/`, bundled beside the
  executable under `assets/models/mega/`. `tools/build-vehicle-models.py --key <key>`
  builds one entry. Shared CM3D/GLB/WebP/meshopt code: `vehicle_model.py`,
  `vehicle_glb.py`; per-car geometry/UV rules in their own modules.
  `TOCA_MEGA_MODELS=OFF` and removed entries retire only ledger-managed files.
- Only graphics detail 4 (Mega) passes overrides to the loader (fresh settings
  default to detail 3). `frontend::load_car_models` loads the original
  HIGH/MEDIUM/LOW triple first, validates an optional HIGH replacement and
  decodes its images before adopting it; missing/invalid/incompatible files keep
  original HIGH (invalid ones log diagnostics). Detail 0–3 and MEDIUM/LOW stay
  original. Rival cutoffs 40/80 and ten-dispatch hold:
  [visibility](scene-rendering.md#mega-range--2026-10-17).
- Both readers produce the format-independent `Model` (`formats/model.hpp`);
  `formats/model_validation.hpp` shares replacement binding checks across cars,
  cockpits, scenery (each keeps its transform/material policy).
- `app/session_models.hpp`: scene-scoped immutable car/cockpit resources by
  render ID; primary resources borrowed, other IDs load once (rivals, Ghosts,
  second local player). ID 10 separate from physical car 2. Per-view model
  preparation moves temporary models into the consumer; animation/visibility
  state independent.
- `scene/model_textures.hpp` shares model, static scenery, ground GPU uploads per
  renderer/session (incl. lamp derivatives, split views). CRC32 narrows
  candidates; exact bytes, encoding, palette, mipmap policy decide identity.
  Replacements are decoded before adoption.
- Compatibility: original node/mesh/texture counts, hierarchy, IDs/flags, mesh
  identities, damage/animation bindings. Matrices canonical except symmetric
  outward local-Z offsets of wheel nodes 1–4, ≤100 mm per side, equal per axle
  (presentation only; wheelbase, height, contacts, simulation original).
  Vehicle definitions keep wheel dimensions and `wheel_outset`.

### Encoding and materials

- GLB2 embeds every image/buffer. Prepared WebP keeps alpha, embedded unchanged;
  new encodes quality 70/method 6; existing WebP80 keep provenance. Exact vertex
  compaction keeps triangle corners, UV seams, normals. Identical buffers shared;
  beneficial buffers use lossless `EXT_meshopt_compression`, no duplicate raw
  geometry. Decoder pins: `cmake/dependencies.lock.json`.
- Node extras `toca_id_flags`, `toca_variants` keep original bindings. Primitive
  extras `toca_ghost_hidden: true` mark generated wheel-housing/cavity faces
  using the float32 VEC3 attribute `_TOCA_GHOST_POSITION` in loaded and new-best
  Ghost drawing: alternate positions keep the body mouth, shorten inward depth
  to 20%; alpha 96 (exterior 150). Original positions, skin, plate thickness,
  UVs, opaque rendering intact. Marked meshes need a complete finite alternate
  table shared by all primitives; original models have neither. Mega Ghost wheel
  nodes 1–4 and children also use alpha 96 (geometry/steering/spin intact);
  requires the alternate interior table, so original-model Ghosts unaffected.
- Canonical black key becomes explicit alpha before lossy encoding. Local arch,
  lamp and paint registration may alter UV/opacity footprints; other motifs,
  slots, real openings stay source-bound. Each texture package keeps lossless
  masters, CRC32, export quality and a `finalize --verify` entry point. Original
  extracts and GLBs stay under ignored `build/`.
- `frontend::CarLiveryMaterials` follows `44df6e` → `49a780`/`49adc2`: fleet team
  parity in `RivalModel` incl. detached roofs. Local/Ghost drawing use explicit
  pose flags; split players use the personal-model path (`4437f0`, bindings
  `49b1f8`). Tank/Flexmobile: no second-team swap.
- User-name decals: `44df6e` draws the local name from CONFIG `+0xb0+10*player`
  into a 128×16 surface via `464423`; `49ae57` copies it into the `ListA`/`ListB`
  row at `y=(actor>>1)*16`. Renault's original row 112 holds `MENU` (Alain Menu) /
  `PLATO` (Jason Plato): selected human replaces `MENU`, teammate keeps `PLATO`.
- `frontend::CarNameFont`: ALLCARS member 0 WIN3.FON (CRC32 `afa81da8`): white
  glyphs, length-dependent spacing, centering, 128×16 clipping, skipped glyph
  drawing at 320-pixel width. Mega: authored 512×512 `car-name.webp` atlas and
  256×32 name bitmaps; original layout coordinates computed before exact
  doubling (incl. odd-width centering); 38 source glyph cells/ink bounds fixed;
  vector contours, white antialiased faces, no outlines. Recipe:
  [generator](../tools/build-hud-assets.py) (quality 70/method 6).
- Scene views bind names separately for local car, each named rival, saved Ghost;
  a new best lap republishes its driver name. Name faces on nodes 15/16/21 sample
  per-instance textures; shared model/atlas bytes, source UVs intact. Original
  BMP sampling: fixed 16-pixel row and subpixel insets; Mega: authored name
  band's UV interval. Vectra/Vectra2 keep separate A/B source slots. Mega name
  faces: linear clamped sampling, continuous alpha blending (also on original
  HIGH fallback); other materials keep source policy. Bonus cars and MEDIUM/LOW
  without name faces: no invented lettering.
  [Acceptance](porting-completed/vehicles.md#runtime-player-names).

### Preview

```sh
python3 tools/export-vehicle-qa.py --key renault --build-dir build/dev \
  --output build/renault-mega-qa
```

Real renderer, eight matched 2560×1440 views, motion/damage, actual rival LOD
selection; writes WebP70, `index.html`, `report.json`, `CRC32SUMS`.
`--livery first|second|both` (Vectra/Cleland `first`, Warwick is V10);
`--closeup LABEL EX EY EZ TX TY TZ SCALE`,
`--closeup-state LABEL STATE EX EY EZ TX TY TZ SCALE`. Artwork-only comparison
separates UV errors from geometry changes; MEDIUM/LOW comparisons use matched
sampling and must keep original pixels. Test selection:
[porting.md](porting.md#native-build-and-checks).

## Original format

Reader: `src/formats/cm3d.cpp`. Evidence: TOCA 1.1a loader `0x00494a40`,
embedded texture loader `0x00495ee0`, car loading `0x0044dc6d` (decompiled
export under `references/decompiled/`). Observed version-100 layout; bounds and
indices validated, unknown flags uninterpreted. Little-endian; signed fixed
point 16 fractional bits (`float = int32 / 65536`). Offsets are relative to the
containing record except the three header file offsets.

## Header and nodes

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | char[4] | `CM3D` |
| 4 | u32 | Version 100 |
| 8 | u32 | Header size 32 |
| 12, 14, 16 | u16 each | Node, mesh and texture counts |
| 18 | 2 bytes | Uninterpreted |
| 20, 24, 28 | u32 each | Absolute node, mesh and texture section offsets |

| Node offset | Type | Meaning |
| --- | --- | --- |
| 0 | u32 | Record size, including padding |
| 4 | i16 | Parent index; -1 is a root |
| 6 | 2 bytes | Uninterpreted |
| 8 | i32[16] | Local 4x4 matrix in 16.16 fixed point |
| 72 | u32 | Node identifier and flags, preserved together |
| 76 | u8 | Number of mesh variants |
| 77 | 3 bytes | Uninterpreted |
| 80 | u16[count] | Mesh indices; 0xffff denotes no mesh |

Node record size = `align_up(80 + 2 * variant_count, 4)`. Parents precede
children in all tested cars. Row-vector matrices, translation at indices 12–14;
child world = local × parent (viewer transposes to GLM column convention and
composes parent × local). Variants are alternatives, not simultaneous meshes;
runtime selection: [vehicle/damage consumer](scene-models.md#vehicle-families-and-rival-detail).

## Meshes

| Mesh offset | Type | Meaning |
| --- | --- | --- |
| 0 | char[12] | Name, possibly zero terminated |
| 12 | u32 | Record size |
| 16, 18 | u16 each | Vertex and triangle counts |
| 20 | 28 bytes | Uninterpreted |
| 48 | vertex[vertex_count] | 32 bytes per vertex |
| After vertices | face[face_count] | 24 bytes per triangle |

Record size = `48 + 32 * vertex_count + 24 * face_count`. Vertex: eight signed
16.16 values `x, y, z, u, v, nx, ny, nz`. UV adjustment of the original loader
after conversion: subtract 1 from U when U > 1, add 1 to V (not a modulo, not a
vertical flip).

| Face offset | Type | Meaning |
| --- | --- | --- |
| 0 | 2 bytes | Uninterpreted |
| 2, 4, 6 | u16 each | Three vertex indices |
| 8 | i16 | Texture index; -1 selects a solid color |
| 10 | u8[4] | R, G, B, A for solid faces |
| 14 | 10 bytes | Uninterpreted flags/metadata |

Texture index -1 selects the face RGBA; other indices bind the embedded texture.
Original material/raster parity remains separate from the accepted Mega
exterior replacements.

## Embedded textures

Record: 12-byte name, u32 payload length, that many BMP bytes (may include BMP
padding; advance by the record length, not by image dimensions). SDL decodes BMP
and palette in memory; the renderer uploads ARGB.

`DONLFREE.BFP` members 5–7 (`OBJECTS.C3D`, `LOWOBJ.C3D`, `ZEROBJ.C3D`) have
exactly one unused zero byte after the counted texture records; `00494a40`/
`00495ee0` consume the counted sections without interpreting it. The reader
accepts this and records `trailing_null_byte`; nonzero or longer tails are
errors.

### Color key and render passes

- `484111` sets source-blit key 0. Measured hardware path: `48664b` selects P8,
  `483261` preserves indices: key 0 = palette index 0 regardless of RGB;
  nonzero black entries stay opaque. The separate A1R5G5B5/A4R4G4B4 fallback
  derives alpha from exact RGB black before channel truncation (distinct source
  capability paths).
- Native BMP uploads keep palette RGB and encode the indexed key mask in alpha;
  `State::palette` keeps the mask separate from material opacity; draw state
  controls discard. Passes without keying keep index-0 RGB. Authored WebP alpha
  and paired shadow alpha keep explicit opacity. Samplers inherit CLAMP with
  per-pass point/bilinear selection. The wiper's white index 0/black index 85 is
  covered.
- `48fae2` modes 0–3 select source modulation, key, blend state, preserving
  state they do not write. Default hardware pass: keyed MODULATE; alpha and
  special screen/additive passes keep recovered blend contracts.
  [Shader contract, source/probe identities and limits](graphics-comparison.md#shader-color-and-sampling).

## Validation

All 33 car models / 696 meshes in `data/allcars.bfp` pass section, parent, index
and exact-file-consumption checks; Donington terrain/scenery/horizon models
([scene resources](scene-resources.md)) also validate, incl. the null-tail
variant. Terrain uses a separate fixed-transform/UV consumer; transform-only
camera resources have their own decoder. Current member/count data:
[inventory-vehicles.py](../tools/inventory-vehicles.py). GPU coverage/rotation
checks and accepted model replacements do not establish complete original
material/raster parity.
