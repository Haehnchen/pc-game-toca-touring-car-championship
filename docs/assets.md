# TOCA 1.1a assets

Canonical source identity: TOCA 1.1a. Builds and automated tests consume approved files
committed under `assets/`; they need no `references/`, original executable, extracted
catalog or private recording. [Build/data contract](build-and-tests.md#remaining-original-data-dependencies).

## Versioned inputs

- `assets/data/original/`: 48 unchanged non-audio BFPs, individually Zstd19; builds unpack
  `*.bfp.zst` under `data/` by filename.
- `assets/audio/`: five packs of converted speech/effects; builds unpack the committed Vorbis
  bytes at the TAR member paths without re-encoding. [Audio contract](music.md).
- `assets/music/`: five approved CD tracks, Vorbis 96 stereo, addressed by filename.
- `assets/ui/original/`, `assets/ui/mega/`: finished WebPs; startup/garage images copied by the
  build, the shared loader applies same-name Mega overrides. [Frontend resources](frontend-options.md#frontend-and-car-resource-contracts).
- `assets/models/`: definitions, source artwork, compact import recipes for the generated GLBs;
  generators read the unpacked Git BFP root explicitly.

Asset JSONs keep only consumed geometry, UV/material bindings and manual import parameters (no
QA snapshots, camera presets, duplicate inventories, separate checksum ledgers). Source archive
names are package-relative (`data/allcars.bfp`); version and CRC32 identify provenance. Python
checks get the unpacked root via `TOCA_DATA_ROOT`.

## Native loading

- `AssetStore` reads loose files and BFP archives from the bundled game-data root; ASCII-
  insensitive component matching, original backslash separators accepted, ambiguous matches
  rejected; never writes assets or uses an extracted tree/catalog.
- `for_loading()` shares immutable BFP bytes across opens within one loading stage (scene/car
  and HUD/audio use separate scopes, released at the end of synchronous loading, also on
  exception unwinding; retained members/cursors may extend lifetime). Path validation runs on
  cache hits; ordinary stores stay uncached.
- `BfpArchive` owns immutable bytes and validates directory/payload layout. Members retain
  archive storage; reads/skips advance one archive index with an explicit end condition. Raw
  16-byte names are metadata, never lookup keys: identity is archive + member index (names may
  repeat, contain path-like text or be truncated).
- The car loader selects three consecutive CM3D members from `data/allcars.bfp` (Honda indices
  1–3). Tracks, placements, horizon resources decode archive members directly.
  [CM3D](cm3d.md), [scene resources](scene-resources.md), [placement](scene-rendering.md#start-placement-and-collision-surface),
  [lifecycle](scene-lifecycle.md).

## Checks

`bfp-parser` (synthetic malformed headers, truncation, overlap, duplicate/path-like names),
`bfp-reference-assets` (byte-identical reconstruction of unpacked BFPs),
`native-reference-assets` (directory/member/cursor reads), `cm3d-reference-cars` (all 33 car
members from ALLCARS without loose `.c3d`), `bundled-audio-decoding` (Vorbis formats, sample
counts, EOF, loop). “reference” in test names means source-data comparison, not an
unversioned filesystem dependency.

## Analysis extraction

Manual workflow only: with the unchanged local 1.1a installation `python3 -B tools/extract-assets.py`
produces an ignored `references/assets/` tree and `manifest.csv` (checks executable CRC32,
keeps archive/member identities, refuses to overwrite; loose speech/frontend files are symlinks).
Unnecessary for build/runtime/tests and removable. Preserve `references/toca-1.1a/`, CD
provenance and analysis work ([reference setup](reference-setup.md)); never commit extracted
media or use the tree as a test fixture.

## BFP layout

Verified against the original loader at `0x00486080` and member reader at `0x004862a2`; all
integers little-endian uint32.

```text
Header, 12 bytes:
  member_count
  directory_bytes = member_count * 24
  largest_member_size

Directory entry, 24 bytes:
  stored_name[16]
  offset_relative_to_payload
  payload_size

Payload starts at 12 + directory_bytes.
```

Payloads are contiguous and ordered, no trailing data; the parser validates layout, member
bounds and maximum size. Image palettes, alpha keys, CM3D hierarchy/material slots and HPC
semantics belong to their typed consumers; file extensions do not establish a compatible
external format.

## Image formats and scaling

- Every active texture mapping row (1358 rows, 1244 image files in `assets/models/mega/texture-replacements.json`) keeps the original texture's aspect ratio; the build checks image dimensions. Differing crop proportions alone are not distortion: original motif and model UVs decide. `air-balllog1.webp` keeps the original 4:1 format.
- 3D materials: WebP payloads go into the GLBs unchanged; the game does not re-encode on load.
- Menu icons: `tools/menu_icon_assets.py` fits motifs proportionally onto transparent target areas (84 files) ([frontend graphics](frontend-graphics.md)).
- Garages: backgrounds 1152×720, crop 1146×714, shown on the 573×357 area ([garage plan](mega-garage-plan.md)).
- HUD: vector outlines with supersampling produce the 4× atlases; start lights come from lossless templates ([HUD](session-hud.md)).
- Title and menu panorama scale proportionally with centered crop; HUD, frames, icons and garages share the proportional UI scaling (4:3, 16:9, 16:10). No Blender in the normal build.

## Canonical tool paths

- Vehicle models: `tools/build-vehicle-models.py` (one dispatcher, the definition selects geometry); shared wheel/lamp/seal/wheel-arch operations in `tools/vehicle_geometry.py`; per-vehicle coordinates, colors, slots stay in `<vehicle>_geometry.py`/definition.
- Vehicle textures: `tools/vehicle_textures.py` (shared metadata reader/adapter); final WebPs are taken unchanged.
- CBO buildings: `tools/mega_cbo1.py`–`mega_cbo6.py` share source-identity checks and sign backs (CBO2 keeps its own offset); tests `tests/tools/test_mega_cbo.py`, `tests/render/test_mega_cbo.cpp`.
- Inventories: shared BFP/node/image readers in `tools/git_game_data.py`, `tools/vehicle_model.py`; `inventory-vehicles.py` reads all 33 models from one checked ALLCARS archive; `inventory-scenery.py` keeps its independent scenery-CM3D checker.
- Tests: `cmake/Testing.cmake` plus four domain includes under `cmake/Testing/`.
- Kept deliberately separate: `tools/cockpit_*_geometry.py` (different roles), `src/scene/model_meshes.hpp`/`model_textures.hpp` (different caches/lifetimes), manual recipes (`export-vehicle-qa.py`, `export-ground-textures.py`, `build-loading-font.py`), reference tools (`tools/ExportReference.java`, `dgVoodoo.conf`, `reference_lap_driver.py`), 32 vegetation images read by `vegetation_materials`, all 62 final vehicle images are bound (other vehicle WebPs are recipe working templates).
- One public package command: `make release`. `references/assets/` (extraction cache) and `dist/toca-Linux-x86_64/` are removable; `python3 tools/extract-assets.py` recreates the cache. Links to ignored `build/` files are local artifact paths.
