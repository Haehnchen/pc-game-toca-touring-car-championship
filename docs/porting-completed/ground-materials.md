# Ground materials: final technical state

S00–S16 accepted for all ten tracks. [Open model/integration tasks](../mega-track-materials-plan.md).

## Inventory and sources

134 used slots/119 original BMPs: 118 generated WebP80 plus existing `SNAPON`. GP/Short share
all 15 ground images; Knockhill `TYRES` is unused. Motifs, alpha, aspect ratio and directional
repeat phase follow `references/toca-1.1a/`; images mostly with doubled original edge length, at
most 512 px.

[Ground TSV](../mega-ground-textures.tsv) (CRC32 `a89e14e1`): sizes, usage, source CRC32.
[Replacement mapping](../../assets/models/mega/texture-replacements.json): `names`, `source_size`,
`source_crc32`, `image` → bundle `scene/<crc32>.webp`. Selective original export only into the
ignored `build/`: `tools/export-ground-textures.py`. All 142248 source corners lie inside the
image bounds.

## Track models per detail level and shared build-out

All profiles load FREE member 1 `GROUND.C3D`; `LOWPOINT.DAT` only limits rendering. Objects: Low
member 7 `ZEROBJ`, Medium/High member 6 `LOWOBJ`, Very High/Mega member 5 `OBJECTS` plus Mega
replacement; draw distance 150/150/200/250/500. Family GLBs do not prove a complete track
build-out. Consumer `0044e7bb`; [resource order](../scene-resources.md),
[loader](../../src/scene/resources.cpp), [model contract](../scene-models.md#mega-track-props-and-scenery).

### Separate optional step: ground geometry

No general ground GLB path. Further geometry must preserve or explicitly map chunk/node/variant
and sector identities, order, `origin_xz`, `sector_centers`, complete terrain matrices and light
flags. HPC/contact surfaces stay original; scenery baking is not directly transferable.

## Rendering contract

Mega only: caches are built at load time; original profiles 0–3, contact/route data and
references unchanged. Source weights interpolate lighting/UVs. 18 opaque neutral slots on nine
real tracks use world-XZ tiling at the original texel scale (19503 suitable triangles, 22
steep/degenerate ones excluded). Asphalt, markings, Croft's dark `SCRUB` and Lavaland keep source
UVs.

| Track | Blend strip | Faces | UV half-width |
|---|---|---:|---:|
| Donington GP/Short | `G&G` | 196/196 | 0.12 |
| Oulton Park | `G&G` | 46 | 0.12 |
| Silverstone | `GG` | 162 | 0.065 |
| Brands Hatch | `NGG` | 70 | 0.12 |
| Knockhill | `g&g` | 102 | 0.12 |
| Snetterton | `G&G2` | 124 | 0.12 |

Mask curves taken from the originals; Brands runs along U with grass at small V. Silverstone's
narrower band protects the partial grass UV edge. Edge normals only at exactly shared positions
with suitable adjacent faces and ≤ 15° deviation from the mean; markings, steep faces and `0x80`
block. Blend layers coplanar, 2×AF, one D16 step of raster depth bias.

30 direct edges (Silverstone 9/Thruxton 19/Brands 2): one alpha band inside existing grass
triangles, width 0.3 or ≤ 45 % of source height (minimum about 0.2473). Ends fade out before
protected materials; exactly two near edges, ≤ 0.002 distance/15° face change, no third users or
markings. Brands: eleven end-cap fields up to 0.3 world width on 20 blend faces; only their lookup
allows 0.01 distance. Near joins keep the 15° guard, exact original edges keep their 22° kinks;
no change outside the seam.

24 seam bridges/47 triangles (Brands 2/4, Lavaland 22/43) close only documented source gaps,
opaque without depth bias or normal averaging; overlaps and third faces cut out, separate height
levels excluded. Lavaland: exactly two source edges with offset > 0.002 up to 0.01, original
angles and source UVs; roads/wall use `TRACK05` as primary source; access per source face.
Micrometric source rounding stays outside the guard.

Implementation: [TerrainView](../../src/scene/view.cpp), [world UVs](../../src/scene/mega_ground_uv.cpp),
[transitions](../../src/scene/mega_ground_transitions.cpp),
[edge/seam cache](../../src/scene/mega_ground_edges.cpp).

## What mipmaps provide here

Full mips only for fully opaque Mega ground after black-key conversion:
`1+floor(log2(max(width,height)))`, at least 1 per dimension. Rectangular levels via temporary
single-level blits; identical contents with the same alpha/mip rule share GPU textures.
Trilinear, free LOD, directional motifs clamp-to-edge, world grain repeat; fixed 2×AF and 2×MSAA
for Mega 3D, no additional menu option. Alpha/key images, vehicles, vegetation, HUD keep their
own rules. Mips cost about 33 % more memory for the affected textures only.
[AA contract](../visual-effects.md#anti-aliasing-2-msaa-in-the-mega-profile); Vulkan verified on hardware, D3D12/Metal in the SDL code.

### S16: Ten-track reconciliation — 2026-10-18

896 blend faces, 30 direct edges, eleven end caps, 24 seam bridges; normal caches 609/3464
blend and 30/146 direct positions/copies. Visible Lavaland lava seam closed; Croft's gapless
grass/rough-asphalt edge stays unchanged. Original angles, UV stretching and protected markings
are deliberate limits, not open follow-up tasks. Additional rendering geometry is no commitment
to free rendering work.
