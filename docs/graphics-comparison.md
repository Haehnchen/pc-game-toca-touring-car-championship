# Matched original rendering

`capture-render-reference.py` reads unchanged 1.1a (`tourcars.exe` CRC32 `8ce56d41`)
on muted private Xvfb under the reference lock. Original files, screenshots and
probe outputs remain outside Git. Automated tests use synthetic/versioned data.

## Frame identity

`489069` entry follows published camera ancestry, visibility, transforms and
precipitation; the recorder retains the last actual draw before `48268e` presents.
It reads the published `49a660` camera inverse, Q16 projection, physical root,
steering/wheel/body angles, actual LOD/node masks and variants, lights, damage,
ordered shadows/effects, HUD, animated scenery and observed captions.

`TOCA_RENDER_12` imports these bounded presentation inputs through shared native
consumers. Canonical meshes, textures and rebuilt car-node worlds remain native;
source worlds are comparison expectations. Actor correspondence uses parent and
sibling paths, including duplicate LOW IDs. Both original ghost buffers retain
actual resource/name/visibility ownership; the fixture never synthesizes playback.
Ghost HIGH/MEDIUM/LOW selection uses the published family or the retained runtime
LOD owner, with separate ghost texture ownership and unpaired liveries.
Cockpit snapshots retain five actual role masks and local/parent transforms.
Skid snapshots preserve the 100-slot physical ring followed by the active queue,
exact Q16 corners/ARGB and source sector/viewport eligibility. Spark snapshots read
the actual `418920` export, including the full-ring empty quirk. The separate
`442480/443030` queue retains 16 SGRID lines plus the POINTS[0] start line, with
ordered Q16 endpoints and opaque colors. Runtime uses HPC support and the source
camera-distance/color rules; frozen comparisons use the actual admitted queue.

### Completed presentation

Schema 2 freezes only the verified game thread and detaches WineDbg, allowing
Wine/dgVoodoo/DXVK workers to finish. A stable owned game-window capture must retain
unchanged body, view, projection and update-counter bytes. Cleanup releases exactly
its own suspend count. The comparator rejects captures without this barrier.
Schema 1 images can show an earlier frame and cannot establish raster fidelity;
their numeric matrix evidence remains usable. Detailed scenes retain two color
targets A/B/A and clear depth each world frame; detail0/1 and Lavaland clear color
too. MSAA seeding, the single-sample overlay, UI clearing and resize initialization
have independent GPU checks.
`TOCA_RENDER_12` replays at most 64 ordered preceding numerical presentations through
the same native renderer, so gaps inherit native frameN−2. Actual C-key camera
switches remain in that sequence. No original pixels are imported; captures without
history retain uncensored raster observations.

## Shader color and sampling

The original comparison backend is configured dgVoodoo 2.63/DXVK. Source `486fbf`,
ordered `489069` passes and `48fae2` material modes establish these contracts:

| Operation | Recovered behavior |
| --- | --- |
| Key/opacity | P8 index zero controls discard independently of material alpha; nonzero black remains opaque. Nonindexed alpha conversion keys exact RGB black. Authored RGBA and shadow opacity remain separate. |
| Sampling | Inherited CLAMP; bilinear ground/marks/shadows, point models/objects. Mega repeat/mipmapped mappings remain explicit. |
| Gouraud | Source perspective mode interpolates diffuse RGB/alpha, fog alpha and UV with RHW. |
| Precision | Dither, fog and alpha test default off with no game enable. Displayed RGB8 does not imply an extra RGB565 quantizer. |
| UI | `490c00` modulates gauges, housing, lamps and ordinary captions by opaque RGB200. POS plaque is at `(437,9)`, rank digits at y5. |
| HUD UV | `490cb0` adds +0.1 texel before normalizing every original HUD UV corner, including scaled sprites; Mega UVs remain unchanged. |
| Texture ownership | `496250` reuses the first 12-byte texture name separately for ordinary and ghost materials. Actor-ordered HIGH/MEDIUM/LOW preparation preserves shared palettes; Mega images retain exact-byte sharing. |
| Draw order | `48d367` submits each node, its children, then siblings. Matrix and mask identities retain allocation indices; draw traversal follows the hierarchy. |
| Cockpit | `494a40/48e837` disable depth test/write when the first variant mesh name starts with `DASH`. `42de80` orders wiper parent, dashboard, gloves, wheel; `489069` defers the cockpit until after precipitation/sparks. The wiper child composes with its already stored parent world. |
| Livery | `44e2a0` uses unpaired materials for Time Trial, split and Linkup; ordinary one-player races use `44df6e/49a780` team pairing. |

Read-only D3D2 probes establish perspective color/alpha and RGB8 precision;
`gpu-states` retains independent exact vectors. At `(176,136)` perspective RGB is
`(221,28,7)`; white diffuse alpha over blue gives `(21,21,255)`. Original wiper BMP
CRC32 `80393135` has white index0 and black index85, requiring index-aware keying.

The explicit `reference_dgvoodoo263` profile reproduces the observed wrapper's TL
center (`0.495`) and keyed indexed level-zero kernel (`UV*size-0.4999000132`,
four byte-valued palette samples, two X lerps then Y, normalization afterward).
Actual bindings across 599 Draws prove 583 P8 shader uses,80 linear and503 point,
plus separate RGBA sky/shadow and untextured passes. RGB565 output quantization
is disabled; RGB565-looking values already exist in source BMP palettes.
The default native profile retains its backend convention. Reports name the
profile and retain every framebuffer difference; neither profile implies a tolerance.

### Resident projection

`48741e/47e730/487ee9` use 53-bit x87 arithmetic with explicit binary32 WV/WVP,
clip, RHW and TL stores. Portable 32-bit integer shader arithmetic reproduces it;
source near clipping retains its own formulas, UV stores and topology. Original
camera-world translation reaches sky placement directly. Read-only vectors match
all 32 WV/WVP coefficients,63 TL XY/RHW components and the four-vertex/33-index near
case. CPU-double/GPU-integer pixel checks require exact equality.

`47f500` hierarchy composition preserves even/odd column accumulation order;
`47fe20` wheel/body rotation preserves its binary32 Rodrigues spills. Ordinary
`487afe` lighting rounds nine basis/light products and three local-normal partials
before the Q16 angle-table and material stores. Normal gameplay sets `488f49(1)`,
so ghosts use this CPU-lighting/TL path too. Textured runs use diffuse RGBA
`(150,150,255,150)`, including its ambient multiplier; untextured runs preserve
face RGB with alpha255. `D3DMATERIAL.ambient` is not the input to `487afe`.
Finale keeps its distinct translation-first arithmetic. Optimized scene consumers disable contraction where required.

### Prelit hardware primitives

`497180` surface/snow billboards and `498430` shadows submit D3DLVERTEX type2;
lamp reflections/contours/halos, skidmarks and fragments use the same vertex type.
Rain/sparks `47cb80` submit type2 lines. The observed D3D11/DXVK rasterizer
requests 1.4-pixel quadrilateral lines even with a single-sample target. Direct
Vulkan coverage over all 256 subpixel phases establishes its 1.375-pixel rectangle;
the actual spark gradient also matches after RGBA8 blending. The reference
profile expands this fixed geometry on the GPU, keeping the raw endpoint stream
([D3D11 specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.4.5%20Quadrilateral%20Line%20Rasterization%20Rules)).
The reference profile retains actual
separate world/view/projection DP4/FMA stages, hardware clipping and a 0.5-pixel
center, independently of CPU-TL0.495. HUD and screen lightning retain screen/TL
contracts. The normal-game model selector above prevents hardware-lighting rules
from being applied to ghost models.

### Vehicle-shadow alpha

`484bc2` converts SHADAM to A4R4G4B4 before filtering: discard the low index nibble,
then expand UNORM4 (148→153;0–15→0). All4096 original upload texels match, CRC32
`95b0f62e`. GPU checks cover all 256 indices and a filtered15/16 boundary.

### Clamped shadow edges

Source shadows inherit CLAMP. All15 type2 positions/colors/UVs,36 indices,
matrices and viewport match independently of screenshots. Asymmetric edge/corner
regressions retain that contract. Multiple actors submit shadows in source slot order.

## Matched vehicle and detail comparison

The preserved22 Donington View2 cases cover cars0–9 at detail1/3 and Honda at0/2.
All424 canonical node worlds now match exactly. Source local selection is MEDIUM
at detail0/1 and HIGH at2/3; rivals and ghosts select all three families. `44d7f9` medium wheel-pivot
preparation is shared by runtime and comparison. Resource10 is actor5's separate
Vectra livery, not a selectable car. Detail0/1 omits static sky and keeps the source
weather clear color. Diagnostic selectors stage only track/car/detail/weather
before the unchanged loader, recording all writes; no body/camera/geometry writes.

Seven completed-presentation cases add a full16-car grid, both genuine ghost
owners, cockpit1, bonnet4 and positive original damage level2. All784 canonical
paths match; all 713 root/LOD/node/ancestor-eligible worlds are exact. Hidden roots
retain stale source worlds (52 raw differences), reported separately from active
geometry. LOW resources0–7/10 and all three visible LOD families are represented.

## Full-lap and raster scope

A genuine original Renault7 Donington sunny lap supplies3647 continuous human
updates: the retained native motion consumer matches all 49 fields with one initial
import and no intermediate reset. An additional3793-update route/session comparison
matches ordered POINTS, lap/best and cached HUD timing; best/last240565ms.
Approach positions and source clock samples remain explicit inputs; this is not
an autonomous wall-clock or AI proof. [Full lap](full-lap.md#scope).

All ten tracks and Donington rain/snow retain accepted numeric scene coverage;
their historical schema1 images make no cross-track raster claim. Current schema2
reports include complete-frame and named fixed-region raw errors without masks,
thresholds or semantic segmentation. Source presentation state and raster agreement
are distinct results. Seven complete reference-profile frames cover the 16-car grid,
both actual ghost owners, Time Trial, damage/effects and cockpit/bonnet views.
Six match all 307200 pixels. The three isolated cockpit pixel differences are
accepted by the user as visually insignificant; C05j/C02 is complete. Raw comparisons
retain every pixel difference. The positive damage frame includes89 skid
quads,12 brown, and9 spark lines. Captured active car worlds and all23 cockpit node
worlds match exactly; hidden exterior nodes retain their stale source worlds in the
raw geometry audit.

Separate compile-time native/reference world modules and exact normal binary32
decoding resolve the Vulkan pipeline-start blocker without changing precision.
GPU/native Time Trial, representative Mesa ASan/UBSan, cockpit/HUD GPU and94 strict
reader checks pass. [Completed shader plan](porting-plan.md#c05j--c02--matched-original-rendering).

```sh
python3 tools/capture-render-reference.py --session single-race --no-qualifying
python3 tools/capture-render-reference.py --tt-lap --tt-save-best
python3 tools/capture-render-reference.py --load-best --tt-wait-ghost
python3 tools/capture-render-reference.py --session single-race --no-qualifying --view 1
python3 tools/capture-render-reference.py --damage
python3 tools/compare-render-reference.py CAPTURE/state.json \
  --renderer build/dev/toca-reference-scene --output build/comparison \
  --reference-raster
```

The comparator owns a separate muted Xvfb and captures only its game window.
Capture tooling/options: [reference setup](reference-setup.md#original-frame-capture).
`reference-render-reader` validates boundaries, source ownership, strict wire widths,
canonical selection and uncensored raster reporting without Wine or a GPU.
