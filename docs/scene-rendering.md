# Terrain and static world draw consumer

`scene::TerrainView` uploads the ground's embedded images and keeps world-space
triangle buffers through the SDL GPU adapter; it takes explicit `CameraInputs`
and `Lighting`, neither chooses a gameplay camera nor places a car.
`scene/camera.hpp` holds the projection contract. `world.vert` preserves the
source TL projection/clipping on the GPU; the CPU triangle projection consumer
serves as test oracle. The lookup formula uses host `asin`; the original CRT
implementation is not ported, so full cross-platform table-bit fidelity remains open.

| Original site | Native contract |
| --- | --- |
| `0x0044f0a3` → `0x00488f49(1)`, `0x00488f59` | Racing terrain uses TL vertices. Resident terrain/static/model geometry and streamed prelit world triangles/lines reproduce the preparation in `world.vert`. |
| `0x00478b20`, `0x004a1524` | 4096-entry asin table generated at runtime: `asin(index/4095)*65536`, truncate, ×651 >>16. No runtime table in the executable file. |
| `0x00496c50`, asm `0x00496cfc`..`0x00496d91` | Dot baked normal with first-light direction; signed fixed, clamp magnitude to 65535, lookup at magnitude>>4, negative angular contribution clamped to 0. Brightness = truncated `angle*(light.red*250/1024)+ambient.red`, saturated 255, copied to all RGB. No point Lambert/inspection light. |
| `0x004962e0` | After initial lighting, face flag 0x80 sets every referenced vertex to RGB 130; shared vertices keep it even on an adjacent unflagged face. Loader preserves flags separately. |
| `0x0047e730` | World-to-view matrix and original projection. TL X/Y use viewport center/half-size directly (no -0.5; shader handles pixel-center conversion). Depth = fixed-rounded `depth_scale + depth_offset/z`; RHW = 1/z. |
| `0x00487ee9` | Near crossing interpolates unprojected coordinates and UVs. New intersections: RHW=1/near, depth 0, packed diffuse copied from the outside endpoint (not interpolated). Unclipped vertices keep the rounded depth curve. |
| `0x0048f5f9` | D3DCULL_CCW=3 removes nonpositive projected area with TL Y down; the world shader rejects the same polygons before optional reflection. |
| `0x00489069` → `0x0048fae2(1)` | Textured terrain keeps P8 index-zero key, depth test/write; source chunk/face order, no material sorting. |

## Draw model

- Sector selection on the CPU. Resident geometry uploaded once per chunk/light
  value; camera movement changes only shader constants. Material runs keep
  source order (incl. feathered Mega ground); quantized colors keep face
  overrides and custom Mega normals. Shader emits ≤2 triangles per source
  polygon, degenerates rejected output without CPU readback.
- Weather, surface, exhaust, lamp-halo billboards stream prelit source corners
  through the same shader in original order (fixed-point geometry, close-camera
  rejection, shared 127-sprite queue unchanged). Car shadows, skid marks, lamp
  lenses/polygons, wet-ground reflections, both windings of prop/impact
  fragments use the same stream; topology/colors/order are CPU inputs, GPU
  culling does not remove queued descriptors. Lamp reflections keep ALWAYS depth
  comparison and writes; translucent skid marks keep depth writes.
- Rain and contact sparks: separate raw endpoint ranges, hardware-line shader
  branch. Strict `< near` rejection keeps on-plane endpoints (unlike triangle
  clipping); crossing colors interpolate and truncate. Rejected lines lie
  outside the clip volume (no zero-length leak). Hardware side/far clipping,
  one-pixel width, fade, depth writes intact.
- Dynamic frame storage/cyclic uploads keep capacity without allocating
  resident meshes; split panes rebase dynamic ranges independently of TL
  indices; queued frames, reflections, MSAA pass boundaries preserved.
- Dynamic TL draws use local 16-bit indices and a GPU vertex offset; a new batch
  starts when the vertex range fills (>65536 vertices OK, order unchanged).
  HUD/menu overlays keep the offsets. GPU address overflow = explicit error.
- Lifetime: `Terrain` outlives its view; queued frames hold immutable mesh
  buffers; textures/meshes destroyed before the renderer.
- Division candidates are corrected with an integer significand residual
  (binary32 rounding without float64 hardware). Camera changes reuse mesh
  identity; light changes keep older queued frames. Authored `CAMERAS.C3D[0]`
  fixture: 2978 projected vertices / 994 triangles. The cloudy-light fixture uses
  native float normalization of the source light position, not the recovered
  fixed routine; it is no initial race-camera frame or reference-image comparison.
- Scene composition: terrain + [model lighting/variants](scene-models.md),
  car/prop passes, sector selection and draw order (see Scene visibility and
  sector order).
  `0x00431670` moves the sky root with the camera and Y-1.5; `0x0048ea47` forces
  its special depth. Moving HOR actors use their own 2000-unit projection before
  terrain. Material/raster equality still needs matched original comparisons.
- Race-model transformation/lighting and ordered instancing use the same
  shader ([model resource contract](graphics-prototype.md#adapter-scope-and-remaining-game-work));
  finale actors use resident models with separate position evaluation order.
  All three shader formats compile; runtime checks cover Linux Vulkan.

## Original ground filtering

`00489069` sets render states 17/18 (texture min/mag) to 2 (linear) before
visible ground, tire marks and shadows, and restores 1 (point) before sector
objects and cars. `0046333f` enables the separate `BILINEAR_CARS` option only
when supplied. The GPU adapter selects a cached point/bilinear sampler per draw;
ground and car shadows request linear, other consumers keep the source default
(a pass switch, not a global smoothing option). `486fbf` sets CLAMP once.
Indexed textures use an index-zero key independent of diffuse opacity. Matched
captures/differences: [graphics comparison](graphics-comparison.md); keying,
interpolation, display precision, dither: [shader color/sampling contract](graphics-comparison.md#shader-color-and-sampling).

## Per-track horizon height

`00431440` selects a Q16 height by track; `00431670` subtracts its float
conversion from the camera world Y (found from the Knockhill view; the old
fixed Donington offset was wrong). X/Z stay camera-relative.

| Track IDs | Q16 height offset |
| --- | ---: |
| 0, 5 | `0x18000` |
| 1, 2, 3, 9 | `0x14ccc` |
| 4 | `0x1b333` |
| 6, 8 | `0x16666` |
| 7 | `0x20000` |

## Initial body and follow camera

`scene/body.hpp` produces the initial body transform for normal cars 0–7 from
the selected horizontal start and the track HPC. Position/basis/contact values
are signed 16.16. Render matrix: row vectors, translation at indices 12–14,
above the car's CM3D hierarchy.

### Body setup

- `0040a770`: per-car dimensions in its normal-car switch (fixed constants, not
  a mesh bounding box); halved for contact extents.
- `00412040`: Euler rotation reset to `(0,heading,0)`; body at
  `(startX,0x83000000,startZ)`. Yaw from sine lookup (`0047fa20`/`0047fe20`).
- `0040c280`: shifts positive half-extent products before combining signs into
  eight world contacts. Separately multiplied negative corners change fixed
  rounding.
- First `0042e6f0` pass queries all eight HPC contacts (cached faces zero); the
  second pass retains each successful first-pass face identity.
- `0040bbc0`: contacts 0/1/2 and sampled heights → upward plane normal,
  normalized. `00412040` projects the old X basis onto the plane, normalizes X,
  `Z = X × up` normalized; rebuilds contacts, queries HPC again.
- `00410190`: shifts body Y and all contacts by the largest positive
  surface-minus-contact correction; if none is positive, the closest negative
  correction above the -500-unit search bound. Single correction, not a
  suspension-settling simulation. Missing contacts/degenerate bases fail
  explicitly.
- Normalization: `00478b20` sqrt midpoint entries, fixed arithmetic with
  separate shift per product and 32-bit wrap; table entries generated with exact
  integer square root. Trigonometry follows the table formula with the literal
  double `2*pi` at `004ac6d8`; host `sin` is used (old CRT not ported).

### Default View 2

- `004508ce` selects default view 2 when config `+48==0`. Camera table indexed
  from `004c3478`: View 2 = entry `004c3480`, single-player local position
  `(-0x5668e,0x1800f,0)`, Euler `(0x6e,0x400,0)`. Node `004c3484`, position
  `(-7,2,0)`, pitch `0x38`, is View 3.
- The camera is a child of a separate follow anchor: `0045ea50` gives the
  initial yaw, `004608b0` the final body position. The camera does not inherit
  body roll/pitch. The native pure function takes the anchor heading
  explicitly, composes the local camera with the yaw/position anchor, then
  applies the affine inverse used by `0047c520` (a transpose would assume more
  orthonormal precision than the lookup values have).
- `00460070` initializes actor grid ranks to actor indices. Qualifying's
  `00443f60` calls `0045ea50` with geometric `SGRID[player_slot].heading`,
  `player_slot=car_id*2`; its body heading comes from the runtime route records
  (see Start placement and collision surface). Fresh Single Race without
  qualifying: `00460170` gives the local player rank 15, so both use SGRID[15].
- Anchor updates and yaw smoothing (`0045ea90`): see
  [FollowCamera](driving-audio.md#moving-follow-camera-and-local-driving-audio); camera collision and the full original session lifecycle remain open.

### Moving wheel presentation

- Inputs: body pose, steering angle, four wheel phases. `004608b0` subtracts
  `mul16(wheel_speed,0x28b)` from each signed16-bit phase; the native host
  advances phases once per active simulation tick (FPS independent); the
  complete original render/publish cadence is not claimed.
- High detail: nodes 1/2 `00480750` steering yaw, then `004809e0`/`0047fe20`
  local-Z spin; nodes 3/4 `004807f0` absolute Z spin. Pivots/hierarchy intact.
- Medium detail: steers only nodes 1/2 and negates the right wheel's X/Z basis
  rows, as in the original caller.

### Boundary

`tests/scene/test_body.cpp` covers flat-plane Honda, sloped support for all car
dimensions, contact correction, follow-anchor transform; optional canonical
root checks both Donington starts for all eight cars (final contacts on/above
surface, at least one touching, horizontal start preserved).
[Motion](vehicle-motion.md) advances wheel/body state afterwards. Opponents and the original session countdown remain open.

## Start placement and collision surface

`scene/placement.hpp` consumes original POINTS/LOWPOINT and SGRID; returns only
fixed 16.16 XZ and the original signed 12-bit-turn heading in an `int16_t`. It
does not infer car origin height from terrain nor turn a collision-plane height
into a body placement.

### Single-player starts

- `00460160` installs SGRID: first 16 records geometric grid strips, second 16
  overwritten during qualifying setup. `00401190` selects a strip by race rank,
  passes endpoints to `00401000`, keeps the stored heading. `00401000`: strip
  midpoint plus 2.5-unit sideways offset, normalize `(-dz,0,dx)` for
  `first-second`.
- `00460170`: local player rank 15 for fresh normal Single Race, qualifying
  disabled, one player (independent of car). `single_race_start` implements it;
  `race_grid_start` takes an explicit rank 0–15 for a future consumer of actual
  qualifying ranks.
- `00479182`: POINTS = 32-bit count + 64-byte records; XYZ at `+0c/+10/+14`
  gives the qualifying start. The actor/route suffix is not decoded here.
- `00443d30` builds runtime grid records in rotated actor order
  `(iteration + player_slot) mod 16`; `00451ccd` sets `player_slot=car_id*2`, so
  the local player's iteration is 0: position from POINTS `count-18`, direction
  from `count-17`. `00443f60` passes them to body setup. LOWPOINT replaces
  POINTS below detail 3; both canonical Donington versions give the same start.
- Qualifying heading: normalize the 3D route direction, `004265b0` with
  reference `(1,0,1)`, subtract `0x200` (not a new `atan2` convention); lookup
  rounding kept. `00478b20` builds the sqrt midpoint and asin tables
  (disassembly `00478b20–00478d03` confirms fixed-scale factors omitted by the C
  export). Native sqrt entries: exact integer sqrt of `(16*i+8)*65536`. Angle
  entry: `trunc(asin(i/4095)*65536)*651 >> 16` with host `asin` (CRT not ported).
- `00401790` installs PITS (owned bytes) for later pit consumers; not a source
  of start positions.

Canonical Donington values: qualifying XZ `(-7749269,-9039511)`, heading `-376`,
HPC height `509805`; no-qualifying race XZ `(-9642299,-5298024)`, heading
`2161`, HPC height `335053` (terrain samples, not body origins).

### HPC height consumer

- `formats/hpc.hpp` owns collision data from KEEP member 0. Layout (`0042f1c0`;
  export pointer types misleading): XZ origin, five signed cell counts + header
  padding, five contiguous arrays of 4-byte cells, 4-byte vertex count, 12-byte
  fixed XYZ vertices, 4-byte face count, 8-byte faces (`a,b,c,flags`), 2-byte
  index count + 16-bit index list, root width and height. Native parsing
  validates counts, references, branches, leaf ranges and exact consumption.
- `0042eed0`: 256-unit root cells; each branch subdivides 4×4 through five
  levels to one-unit cells. Leaves scan triangles in stored order. `0042f000`:
  inclusive XZ bounds, three fixed half-plane tests. A containing cached
  triangle wins before tree traversal (eight-contact caller `0042e6f0`).
  Out-of-root/empty queries return no sample; invalid cached identities and
  selected vertical planes are explicit errors.
- Height plane (`0042e6f0`): `(B-A) × (C-A)`, X/Z divided by Y, Y=65536,
  `dot(normal,A)-nx*x-nz*z`; each signed product shifted separately by 16;
  add/sub wrap at 32 bits. `HpcSample::normal_ratio` is this plane form, not a
  unit normal. Raw flags and face identity retained. Speed-dependent surface
  class 2/3 bumps and the caller's changing contact/light fields are outside
  this consumer.
- `HpcSurface` keeps two immutable plane records per face: `0042e6f0` ratios
  and the separately edge-normalized `0042e9f0` prop normal/constant, each with
  its own signed arithmetic (incl. prop height division); a zero-Y plane is
  rejected only when selected. Cache: 32 bytes/face (≤1 MiB); tree/cache
  precedence unchanged.

### Body boundary

`00412040` sets a sentinel Y, builds eight contacts (`0040c280`), queries HPC
heights, derives tilt (`0040bbc0`), repeats the query, shifts the body via
`00410190` (see Body setup). Velocity, moving suspension and race simulation
follow separately.

## Scene visibility and sector order

The native scene uses the original runtime sector selection of TOCA 1.1a
(`tourcars.exe`, CRC32 `8ce56d41`), generated from GROUND geometry and the
camera; no visibility asset, no replacement frustum heuristic. LOWPOINT/POINTS
supply independent barrier candidates.

### Sector selection

- `47de00` builds the grid from GROUND node centers in file order; sectors are
  64 world units wide, 32-unit half-size. `47dab0/47e370` keep the Q16
  arithmetic: raw 63 correction before the 22-bit shift, Lavaland's one-raw-unit
  row offsets. Transformed GROUND/static vertices give the same world XZ bounds
  as `496980`.
- `47cf40` projects four bounds corners at world Y 0. A positive-W corner
  strictly between clip X=-W and X=W admits the sector; edges crossing both
  walls also admit it (incl. a wide rectangle behind the eye). No vertical, near
  or far plane. Triangle clipping separate. Uses the drawable's actual projection
  X at 4:3, 16:9, 16:10.
- `47d3d0` walks the camera-centered grid in the growing up/right/down/left
  spiral, visiting at most the sector count (incl. steps outside the grid).
  Failure allowance starts at 2048; a visible sector resets it to 9, 5 more
  after each pair of legs. Selected sectors keep visit order and fill the
  runtime visibility bitmap.
- Squared camera distance gives detail 0..4 by strict thresholds in 64-unit
  sectors:

| Graphics detail | Projection far | Detail thresholds |
| --- | --- | --- |
| 0 / 1 | 150 | 2,3,3,4 |
| 2 | 200 | 2,3,4,5 |
| 3 | 250 | 2,3,5,6 |
| 4 / Mega | 500 | 4,6,9,10 |

- First two thresholds from the executable; `48f5f9/47e6e0` set the last two to
  `trunc((far+150)/64)-1` and `trunc((far+150)/64)`. Terrain draws only at
  detail below 3. Static objects have no sector-detail gate. Dynamic roots draw
  when sector detail < root limit: flag `04000000` → 1, else `02000000` → 2,
  else 3 plus flag `01000000`.

### Mega range — 2026-10-17

Native extension (not original): projection far 500. Above the original maximum
far 250 the first two thresholds scale by `far/250` (Mega: 256 and 384 world
units instead of 128 and 192); far-derived terrain and remaining root
thresholds become 576 and 640. Model flags and track data intact; detail 0–3
keeps the recovered behavior. Vehicle HIGH/MEDIUM ranges extend to 40/80 units
with the whole field at either level
([vehicle detail](scene-models.md#vehicle-families-and-rival-detail)). Identical
Mega assets and cameras at 250/500 add visible geometry (Silverstone mid-lap
17507 → 43643 vertices). Real GPU batch fixture: 65541 vertices across the
16-bit batch boundary.

### Registration and scene traversal

- `47de00` prepends static entries to each sector (reversing source allocation
  order); mesh bounds expand GROUND bounds; a sector without GROUND mesh keeps
  its cell rectangle; static variants share an entry, final valid mesh kept.
- Direct scene-root children, excluding sky ID 999, append to sector actor lists
  in sibling order; initial local translations truncate to Q16. Later
  `47f500→47e3c0` rounds world translations, removes a moved root from its old
  list, appends to the new. The 4.5-unit neighbor tests do not delay migration.
  Hidden roots skip the update. After removal outside the grid `47f500` no
  longer calls migration for the unregistered sector.
- `47f440/47f4d0` combine manual actor masks with sector visibility before the
  draw-distance gate. A hidden parent suppresses its children. Parent links
  determine root membership when serialized order interleaves roots/children.
- `47dd90` adds sectors of the camera's ancestor frames without reclassifying
  retained detail bytes. Ordinary views2/3 (`4508ce`) use the independent follow
  anchor; cockpit1 uses the independent head anchor; bonnet4/rear5 use the car
  parent. `4608b0` places both anchors at the body. The head anchor is a registered
  direct root with flags0; camera children and the global ID1000 root stay
  unregistered. Authored CAMERAS: `4510bf`
  creates them as direct scene children before `47de00`, so their own
  camera-position sector is forced; finish-camera inputs supply that Q16
  position explicitly and do not force the follow-anchor/body sector.

### Draw and effect consumers

- Hardware branch of `489069` draws: sky, special HOR actors, selected terrain,
  `442500` ground overlays and car shadows, then each selected sector's static
  list followed by its dynamic roots; shared sprite/triangle/line queues flush
  afterward. The source branch without a GROUND grid keeps forward static and
  actor allocation order, no spatial/root-LOD filtering.
- Moving HOR actors use the separate root from `496f70` (after `47de00`), bypass
  sector registration and root LOD. `489069` draws it before terrain with a
  separate 2000-unit projection: near `0x4000`, depth scale `0x10008`, depth
  offset `ffffbffe`, normal depth test/write. Only sky keeps its own depth.
- `47db00` admits all valid effect sectors when the selected list is empty.
  `4271d0/446e50` separate car effects from root geometry. Singleplayer:
  `4469a0` sets the car-effect gate to 1 (kept in normal racing); Time Trial
  sets local effect masks to 1. Manual body/wheel masks and actor state still
  apply. Canonical cockpit1 hides all local model/effect gates while retaining
  the physical parent and its camera ancestry. Other ordinary views keep local
  manual effects even when the body's sector/geometry is invisible, and return
  0 without a car. The sector-dependent car-effect
  branch belongs to future multiplayer; not applied as extra local wheel/exhaust
  culling. Root camera masks and geometry LOD independent.
- `439280` queues car shadows from the car-effect view mask; `498430` draws the
  footprints without sector/root-LOD test. `441d10` filters stored/current tyre
  marks by saved sector and view mask. `416320` gates detached-part creation via
  `47db00`; the source update also removes landed parts whose saved sector
  becomes invisible.
- `446f20` adds no per-particle sector lookup for weather, wheel/exhaust
  sprites, sparks, fragment triangles (existing masks/pools/queue budgets).
  Visibility adds no sound or RNG call.
- Original order: `421aa0→415090→40b360` Motion and `421980→446a80→432210`
  wheel emission use existing view selection/masks; `1f520→4608b0→420be0`
  engine emission precedes the next selection; then `1fa40→1ec20→202b0→871b2`
  update camera ancestry, run `47d3d0`, update car masks via `4271d0/446e50`,
  other root transforms/masks, then `420420` for tyre marks and weather.
- Native: one persistent selection per active 25 Hz deadline, after
  Motion/engine emission and before tyre marks/weather. Motion's detached-part
  callbacks read the retained previous selection. Frame preparation uses a copy
  (render FPS cannot migrate roots or change effect/RNG state). Two PVS
  refreshes per tick are not an original rule.
- Actor sector `ffff` = unregistered, bypasses the actor-sector test; mark-reset
  slots also use `ffff`; `47db00` indexes its bitmap without validating. Native
  sector queries reject the sentinel instead of an out-of-bounds lookup.
- No additional fog layer: the audited draw path establishes none; `48f5f9`
  supplies projection distance and sector thresholds, `48fae2` changes
  texture/alpha/blend states; TL vertices keep specular `ff000000`. No
  weather-fog emitter established.

`FleetScene` owns physical parent registration and vehicle LOD dispatch.
`SceneView::scene_context(local_actor)` publishes those values, the selected PVS
and separate geometry/contact/wheel/exhaust eligibility in one snapshot.
Motion damage registration, Rivals and Linkup consume the same snapshot;
detached callbacks and engine emission retain the previous selection until the
ordinary post-emission refresh. Presentation never advances this owner.

Single-player rival wheel/exhaust eligibility reads manual model`+140` and the
retained car-sector gate, not PVS-filtered root`+138`. A Full rival outside the
view can therefore retain effects even when its geometry mask is0. The existing
Full/Distant gate still applies; high-LOD contact effects and wreck retirement
keep their separate masks/sectors. Linkup retains its existing view policy.
Synthetic CPU and GPU visibility checks cover migration, LOD hold, offscreen
eligibility and unchanged snapshots across presentation calls.
The scoped fleet-consumer check covers real contacts and separate effect masks;
`native-single-race` passes the actual lap/results/warm-restart flow.

### Recorded scene inputs

`trace-reference.py --scene-journal` records actual initialization, migration,
selection, model and publication calls in a separate CRC32 JSONL journal.
Camera matrices, actual near/far, root transforms and physical body poses are
inputs; source PVS, registrations and active model/effect masks are expectations.
`replay-scene-journal.py` executes these operations through the production CPU
owners using Git resources. Root neighbors gain a source claim only after each
root's first actual migration. No camera generation or physical autonomy is claimed.

Normal one-player NoQual View2 uses the local body as LOD observer: its source
view factor is 0. Canonical process `adeb8=1` forces model dispatch at each actual
call; it is generated owner state. The live adapter and journal share this path.
Mega and other native profiles retain their existing observer/hold policy.

One bounded original startup reaches the named gates after 4 updates: 98 actual
operations, 3 publications, 12 registration changes and measured camera-Y settling.
All 3,411 compared values match; PVS and active LOD masks remain constant, so this
does not establish moving PVS/LOD transitions or a complete race. Journal prefix
CRC32 `3d0005e0`; evidence under
`build/test-artifacts/scene-frame-journal/reference-trace-c3mftma3/`.
[Capture and replay](reference-setup.md#scene-input-journal).

#### Generated normal View2 frames

`SourceSceneFrame` consumes initial root roles/translations, physical pre/post
bodies and external QPC samples. Authored `SGRID` heading seeds `FollowCamera`;
normal View2 world/inverse matrices and projection come from the shared runtime
owners. The camera inverse preserves source cofactor spills and rounded reciprocal;
world translation is published directly instead of recovered through its inverse.
`421aa0` slews from the pre-physics basis; the primary scene uses the completed
body translation. One publication precedes the optional extra physical pass;
`SourceClock` determines that pass and consumes the next sample afterward.
Scene operation lists, camera matrices, projection and later observed extra flags never
enter `TOCASFR1`. The live game shares the camera calculation and retains its
explicit native fixed-25-Hz scheduling adaptation.

One joined original capture has three complete primary frames and physical counts
`1,1,2`. All 3,216 values match exactly, including float matrix bits, generated
clock/extra decisions, PVS, root registration and model masks. Source journal CRC32
`4d3d02df`, frame prefix `8b2ecd2a`, input binary `963b637a` (1,720 bytes);
evidence: `build/test-artifacts/generated-scene-frames/reference-trace-svvds239/`.
Focused camera/frame checks, the native race/results/restart flow and a representative
ASan/UBSan component run cover the change. Physical states and initial root
translations remain inputs; controller switches, qualifying/finish cameras,
retirement/recovery bodies and moving PVS/LOD transitions remain outside this proof.

#### Joined physical and scene replay

`--compare-startup-scene` joins the retained driver/physical/cold-world replay
and `SourceSceneFrame` in one execution. Authored allocation roots use each
`SGRID` record's first edge at Y0; generated first/repeated cold body placements
then migrate them. Generated Motion PRE bases and completed positions/modes feed
the camera. Both FLET inputs reject root registrations, detail/effect gates and
PVS imports; the combined stream rejects observed wheel visibility. Physical
contacts, damage registration and effect eligibility consume the retained scene.
The first render motor/publication callbacks keep the old selection; `420420`'s
`446b50` wheel callback follows the new PVS/model selection and uses its masks.
The terminal frame drains its callbacks and external QPC sample before closure.

One joined cold driving prefix matches 64 physical updates and 33 publications
(31 extra passes) in Debug and ASan/UBSan: all 35,383 scene values and retained
physical/AI/RNG/timing comparisons are exact. Active state6 contains 23 adjacent
PVS changes and 2 LOD-mask changes. Initial root transforms and recorded body
states are expectations only; no cross-run splice or intermediate body restore.
FLET prefix CRC32 `d28613e3`, operation journal `2d92a477`, frame journal `d0c786f9`;
evidence: `build/test-artifacts/autonomous-scene-join/reference-trace-w408l4qo/`.

This proof covers normal one-player NoQual View2, states9/6 and full/distant
bodies. Original callback/device origins and contact-audio listener/mix context
remain supplied boundaries. It does not replace the separate full-finish/restart,
contact/damage or remaining camera/mode proofs. The native game keeps its accepted
fixed-25-Hz adaptation; unchanged native flow evidence is reused.

#### Ordinary camera controls

The joined owner also generates ordinary views1..5 from requested C/Z held bits.
The source capture injects them before `44a1d0` polls input and checks actual
cached pressed/held actions at `422f60`; selected/saved views remain expectations.
`TOCACOMB` v9 carries one input transaction per physical call and clears the
observed material-view word. The native owner slews from the old selection,
then applies the C edge and held Z before physics; cockpit motion follows the
completed body. Publication keeps its own view/projection snapshot across extras.

Camera ancestry uses the head/follow/car roots for views1/2–3/4–5 respectively.
Allocation includes the registered head anchor, giving 18 roots. Cockpit hiding
changes model/effect gates without hiding its physical parent. The shared
observer uses source Q16 factors `{0,0x20000,0,0,0x140000,0xa0000}` for views0..5;
cockpit uses the retained parent world position before post-model migration and
two separately truncated forward/body blends. The cold head basis stays zero
until first cockpit use; exit resets it to identity once. Camera composition and
inverse retain source float spills. The live original-detail adapter shares
these observer and visibility rules.

One continuous cold capture matches 96 physical updates, 49 publications and
47 extra passes: all 52,756 scene values are exact in Debug and ASan/UBSan.
It covers views1..5, held-C
edges, cycling blocked during rear view, both rear releases and retained FOV.
FLET prefix CRC32 `5b4eedff`, scene journal `2309a6a4`, frames `e929060e`;
evidence: `build/test-artifacts/scene-camera-lifecycle/reference-trace-ustx6ayo/`.

The qualifying scene component is covered below; its continuous physical/clock
join and manual trackside/actor/cheat views remain open. Native camera/audio/mode behavior remains
covered by its own contracts; listener/mix context is still an explicit replay
boundary.

#### Finish, results and warm scene consumers

`--compare-startup-scene-lifecycle` uses the existing continuous cold→finish→
results→RACE AGAIN→warm-green recording without scene sidecars. Generated
state/timer0 selects the ordinary camera until State10's two-second switch;
projection retains its selected factor. State13 owns the 60-call hold, nearest-camera cache and
HPC-clamped close origin. Authored roots precede car/follow/head allocation.
An actual State13 render callback can publish the last generated extra-pass
body while the menu skips physics. No heading, head spring or RNG advances there.

Warm entry resets authored selection/cache; grid initialization resets follow
heading, producer gates and the QPC first sample. Camera view/projection/C/Z,
head motion, authored-node close scratch, result hold and allocated scene state
retain their source lifetimes. HIGH-node contact eligibility is distinct from
HIGH/MED and full-body producer gates: cold HIGH starts hidden; warm grid retains
its last model selection and looks up visibility at the relocated sectors.

The comparison consumes generated registration/effect/PVS state and checks the
recorded physical-PRE roots, PVS bitmap/count and actual wheel callbacks. Camera
matrices and ordered PVS lists were not recorded and have no equality claim.
The final complete physical record may leave its next scene publication/QPC
outside the prefix; no terminal callback is synthesized. Live single-player
finish keeps the selected projection, and warm restart reseeds the retained
camera controller instead of reconstructing it.

The existing 3,744-update cold→warm prefix matches all 1,475,168 consumer values,
including the menu-only publication and first warm-grid lookup. Physical/RNG
producer outputs remain byte-identical to their complete source comparison.
Source prefix CRC32 `b049013e`, trace file `38155171`; evidence:
`build/test-artifacts/generated-scene-lifecycle/fixed/`.
Focused CPU/sanitizer cases, the ordinary96 regression and the native full
race/results/restart flow cover the shared implementation.

#### Qualifying scene consumers

`SourceSceneFrame` also owns states4/6/7/8 and the earned State9 grid.
`443d30` rotates all sixteen POINTS starts around the permanent local actor,
seven records apart. `443f60` hides the other three actor quarters, then
`44ffe1` forces registration of every parent at its authored start. Later
Mode4 publications retain those hidden registrations. State4 skips the
pre-physics heading update; its first renderer reseeds the track heading and
slews once from the completed primary body, never from an extra pass.

State7 seeds nearest-camera selection from the first PRE body. Below two
seconds it retains the ordinary view; afterward the renderer uses the completed
body. State8 resets the thirty-call cache and uses the regular authored look-at,
without State13's close-camera/sixty-call branch. The earned grid restores all
parents and producer gates and reseeds heading/clock, while retaining ordinary
view/projection/input latches, head state, HIGH eligibility and authored actor
selection. It does not invoke the separate `449f50` warm-session reset.

`replay-scene-journal.py --qualifying-consumers` reuses the natural qualifying
finish recording and its bound arrow-only warmup. `TOCASFR1` v2 supplies observed
physical PRE/POST poses, modes, TimeStep, raw `4d3848` primary/extra markers and
phase/timer boundaries to a clockless production owner. No QPC samples, camera,
root registrations, masks or PVS are synthesized/imported as scene inputs.
Comparison begins after the first generated authored State7 publication;
the earlier ordinary-camera history remains unclaimed. Numerical camera
matrices, ordered PVS lists and autonomous qualifying physics/clocks are outside
this bounded component proof.

The eight physical calls24..31 match all 1,940 values across five PRE consumer
snapshots through State8, including all sixteen parent sectors and neighbors.
Source prefix CRC32 `60a99c1f`, trace file `fd9bbc0e`, bound warmup
`139bd73c`, input binary `72f2add6`; evidence:
`build/test-artifacts/qualifying-scene/finish/`.
Source-derived CPU cases cover initial approach, extra-pass cadence and the
earned-grid reset. The native three-lap qualifying→results→earned-grid flow,
one ASan/UBSan component run and the existing ordinary96 regression cover
integration and the shared camera implementation.

### Limits

Multiplayer car-effect masks and matched material/raster comparisons remain with
their respective packages; no original screenshot equality is claimed.
