# Authored Mega scenery materials

The [Donington facade recipe](donington-cbo-texture-review.json) records the
current COMA/COM4 outputs, replacing those in the initial CBO4 recipe.
The [PIT3 correction](donington-pit3-correction.json) uses canonical PIT2 evidence
for Shell Oils and packs two equal periods into the original sampled UV range.
It rejects the earlier single-word Shell revision and its uneven repeat spacing.
The same recipe includes the source-matched COM9 roof courses and four-chart
GSD9 rear atlas. Current package status belongs to the
[scenery plan](../../../docs/mega-scenery-plan.md).
[CBO2](donington-cbo2-generation.json) adds a direct COMB facade replacement,
reuses four reviewed images and gives only roof/sign sheets solid thickness.

The `textures/` directory contains 253 newly generated scene textures from the
built-in Imagegen tool: sponsor lettering, braking markers, direction arrows,
Dunlop bridge atlases, cone bands, black/painted tyres, steel rails, hedgerows,
and original-faithful aircraft, helicopter, advertising-balloon, vegetation, sky
and horizon materials totaling 5,328,248 bytes. These are new images, not enlarged canonical bitmaps.
The separate `prop-materials.webp`
supplies generic material surfaces for additional geometry.

`texture-generation.json` contains source CRC32 values, reviewed crop rectangles
and output checksums.
`aircraft-generation.json` records the HOR textures with archive/member
provenance, palette-normalized CRC32 keys and generated outputs.
Their colors, lettering and panel layout follow the original artwork. Small
material details are newly generated; original images are not stored here.
The additional `aircraft-skin.webp` uses newly generated opaque fuselage, wing
and engine regions for the former flat PSP jet's new 3D UV layout. Its palette
and aircraft design follow the original. The same manifest records its
reviewed row crops. Including this atlas, the 33 new HOR images total
369,588 bytes (361 KiB).
`vegetation-generation.json` records eight complete `LHRTS4` tree redraws,
eight matching transparent foliage sprites and one opaque bark material for
M01a: 17 WebP80 images totaling 1,310,416 bytes. Whole-tree images retain the
original material slots; rebuilt trees use appended bark and foliage slots.
Each foliage sprite is bound to its exact original palette CRC32. The manifest
includes original archive/member identities, image bindings and final output
CRC32 values. Leaves and trees are 512×512;
bark is 256×256. Original reference pixels remain outside versioned assets.
M01b adds two `LHRTS7` tree redraws and two palette-matched foliage sprites
for Donington GP/Short and Brands Hatch, totaling 198,266 bytes. They retain
the source's narrow silhouette and transparent padding. The existing bark is
reused; the distinct `MegaTreeLf7` slot keeps M01a's foliage bindings unchanged.
M01c adds five `LHRTWS3` group redraws in 512×256 and five matching 512×512
foliage sprites for `MegaGroveLf`, totaling 611,486 bytes. The five original
palettes retain their distinct colors and lower alpha openings. The same
manifest records their reproducible imports. M01d-a reuses the exact accepted
M01a palettes for its larger `TRIS04B_1` trees without adding images.
M01d-b adds a 256×512 `LHRTS2` redraw and a 512×512 `MegaTreeLf2` sprite for
all 30 `TRIS02B_1` placements on Donington GP/Short, totaling 122,752 bytes.
Its source palette and tall oval silhouette remain distinct.
M01d-c adds a 512×512 `LHRTS1` redraw and a 512×512 `MegaTreeLf1` branch
sprite for all 16 Croft `TRIE01_1` placements, totaling 125,264 bytes. Original
transparent padding, the stepped crown and its long free stem are preserved.
M01d-d adds two 512×256 `LHRTWS4` redraws and two 512×512 `MegaGrovLf4`
branch sprites for all 11 Brands/Oulton `TRGS18_1` placements, totaling
274,256 bytes. Each palette retains its dense connected crown and ground line.
M01d-e adds a 512×256 `LMRTS4` redraw and a 512×512 `MegaGrovLm4`
branch sprite for all 29 Brands `TRGS22_1` placements, totaling
134,272 bytes. The dark palette, mirrored source UV layout and nine short
stem feet with open gaps remain distinct.
M01d-f adds a 512×256 `LMRTS2` redraw and a 512×512 `MegaGrovLm2`
branch sprite for all 24 Brands `TRGS20_1` placements, totaling 128,676 bytes.
The green-olive palette, central opening, two crown islands and three short
stem feet remain distinct. All 43 vegetation images are recorded in the same
manifest; earlier outputs remain unchanged.
`texture-replacements.json` assigns the artwork to exact source names and sizes.
Canonical artwork remains in the unchanged local reference directory.

`backgrounds-generation.json` records all 48 reachable sky atlases, their
original archive/member/UV references, import parameters and accepted WebP80
outputs. They are opaque 512×512 images totaling 998,616
bytes. `TH02`, `BH03` and `SN02` remain inactive source inventory.
`atmosphere-generation.json` adds seven active lens sprites (3,274 bytes);
`woodland-generation.json` adds four transparent M02a strips (118,188 bytes).
Together these 59 new images occupy 1,120,078 bytes. Exact source and output
CRC32 values are recorded per image.
The [background plan](../../../docs/mega-backgrounds-plan.md) records B00–B06
acceptance and closure of the named M02b woodland findings. Croft now uses a
simple joined 40-triangle body; see the [model contract](../../../docs/scene-models.md#mega-simple-croft-woodland).

`woodland-density-generation.json` replaces the two earlier shared foliage-atlas
outputs `hedge-a.webp`/`hedge-b.webp` with dense reference-based LLRTS1/2
redraws. The two 512×128 WebP80 images total 46,892 bytes; ten exact source
BMP keys cover 453 TREES bindings. Source palettes, archive/member CRCs,
image bindings and reproducible crops are recorded there. The background plan
records the completed geometry follow-ups.

To reimport the same reviewed generator outputs:

```sh
python3 tools/import-mega-textures.py /path/to/imagegen/output-directory
python3 tools/import-mega-textures.py /path/to/imagegen/output-directory --recipe assets/models/mega/aircraft-generation.json
python3 tools/import-mega-textures.py /path/to/imagegen/output-directory --recipe assets/models/mega/vegetation-generation.json
python3 tools/import-mega-textures.py /path/to/imagegen/output-directory --recipe assets/models/mega/backgrounds-generation.json
python3 tools/import-mega-textures.py /path/to/imagegen/output-directory --recipe assets/models/mega/atmosphere-generation.json
python3 tools/import-mega-textures.py /path/to/imagegen/output-directory --recipe assets/models/mega/woodland-generation.json
python3 tools/import-mega-textures.py /path/to/imagegen/output-directory --recipe assets/models/mega/woodland-density-generation.json
```

Only generated pixels are cropped and downsampled. New encodes use lossy
WebP quality 70, method 6, preserved alpha and a maximum edge of 512 pixels.
`make run` builds optimized, meshopt-compressed scene GLBs and removes obsolete
upscale cache entries from the generated and bundled scene directories. Material
slots without an authored replacement retain their source resolution. Existing
WebP80 assets and recorded recipes remain unchanged; embedding copies prepared
payloads without re-encoding.

Geometry and validation details: [scene models](../../../docs/scene-models.md).

Mega now builds the shared cockpit wheel/wiper through `cockpits.json` and
`tools/build-cockpit-models.py`, once for all nine interiors. Authored WebP80
materials, geometry definition and provenance live under
`assets/models/cockpits/shared/`; generated compressed GLBs remain under `build/`.
Both local players load them with independent animation and role fallback.
The same manifest builds18 per-car hand/forearm GLBs under `cockpit/<key>/`,
using the common source-fitted geometry and36 new WebP80 materials from
`assets/models/cockpits/hands/`. That package's `generation.json` retains
original palettes/UV references, image bindings and source/output CRC32.
Dashboards remain original resources. The
[cockpit audit](../../../docs/cockpit.md#asset-and-driver-side-audit)
records nine DASX sets, shared wheel/wiper and per-car hand/dashboard differences.
[K0/K00–K09](../../../docs/mega-vehicles-plan.md#k0-and-per-vehicle-interiors)
tracks the accepted shared parts/hands and pending per-car dashboard artwork/geometry.
Hand geometry and bindings: [definition](../cockpits/hands/definition.json).
Completed exterior/HUD assets do not close the remaining cockpit packages.
