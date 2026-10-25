# Visual effects

## Atmospheric weather

The canonical 1.1a precipitation code runs through normal Single Race weather
selection: weather 3 rain (90 lines), 4 snow (25 sprites), 5 storm (10 rain
lines, lightning, delayed thunder). Sunny/cloudy: no precipitation; Lavaland and
special-sky precedence stay in `4512b0`. CMRAINUP reverses falling velocity;
CMCATDOG uses the two animal textures (both have Extras switches).

### Original consumers

| Source | Native behavior |
| --- | --- |
| `4463e0` | IMAGE640 members 13/14/15 = Snow, Dogs, Cats, loaded from the archive. |
| `4467a0`, `431820`, `4153c0` | Precipitation and fragments share the original 37-entry effect table and cursor. Particle placement uses the shared CRT stream in X,Z,Y draw order. |
| `462000/4628a0`, `4623b0/462d20` | Falling velocities, low-gravity/rain-up modifiers, twelve-unit camera-relative cube, wrapping, sprite variants, line alpha. |
| `446a80→461bb0` | Wind state advances after fragments, before moving HOR actors. |
| `446d30→463170`, `4632c0` | Six-step flash pattern, delayed thunder, randomized quiet interval. |
| `446a80→424860(event25)` | Thunder: one shared CRT draw, sample 76/77, played once centered; threshold strength > `0xf5b`. |
| `446f20→497180`, `47ca40→47cb80` | Camera-facing textured sprites test depth without writing; deferred rain lines keep depth writes and fade their second endpoint to zero alpha. |

- Thunder volume: `4248d5..4248f9` multiplies raw Q16 strength by 100 without a
  shift; `4798cb` clamps to 127 (confirmed by disassembly): every audible event
  has the same saturated gain. The shared contact consumer derives this gate/
  selection for native weather audio and recorded replay; pending thunder is
  delivered after queued tire audio and before Engine/WHINE (shared RNG order).
  Recorded comparisons check raw playback requests; native audio applies the
  127 clamp. Backend voice lifetime remains an observed boundary.
- Texture CRC32: Snow `01768568`, Dogs `37f2860e`, Cats `b653adb6`.

### Native timing and rendering

- The source advances precipitation/lightning in its view callback and wind in
  its simulation update; native schedules both once per active 25 Hz deadline.
  Drawing reads immutable state, no random values. Camera changes recenter
  particles by the original wrapping rule. Qualifying-to-grid reset keeps
  weather/table state (`4469a0`); a new scene creates a new pool. Wet-to-dry
  keeps the source wind state; a new wet scene resets wind via original setup.
  Delayed/pending thunder survives scene ownership changes, pauses its countdown
  outside storm weather, is delivered once. Pause/focus loss freezes all weather
  and stops current thunder without replay.
- Draw order: weather sprites, wheel effects and sign fragments, deferred rain
  lines, white lightning overlay after the 3D passes before the HUD. GPU state
  separates depth test from depth write and supports native line lists. Uses
  the current scene projection and full drawable lightning area.
- The inspected precipitation and lighting consumers do not enable fog; a
  renderer fog shader is no evidence for a scene fog curve. The audited effect
  and scene paths establish no additional fog activation. Fog-state recovery and
  broader transparency ordering/culling remain open. Wheel spray, dust, gravel,
  tyre smoke: see Wheel and surface effects.

## Original sun and lens consumer

- Seven IMAGE640 lens images, members 40–46 (IMAGE320 copies identical), loaded
  by `44f0a3` when `4512b0` resolves sunny weather (incl. Lavaland, CMSTARS,
  CMTOON). Local split screen and mode 2 Time Trial disable the effect; no
  graphics-detail gate.
- `49491f` reports the ONE/INVSRCALPHA blend-capability pair. With it `44f0a3`
  combines member 37 WHITE2 RGB and member 38 SUNMASK indices via `484bc2`;
  without it member 39 SUN is the sun image. The hardware call
  `434d70 → 434b30 → 490b90` passes 1 and `490b90` queues only calls passing 0,
  so the sun bitmap is loaded but undrawn.
- `491950` projects `44eda9`'s light point after multiplying X/Z by 32 and Y by
  10; W must exceed 1, no far rejection. `434b30` tests whether the 64 px sun
  rectangle intersects the viewport; that gates `434c70`'s seven lens sprites; no
  scene-depth occlusion query.
- First two lens centers advance beyond the sun by one and two integer steps;
  the remaining five advance beyond the viewport center in the opposite
  direction; step = `(sun-center)*0x6666 >> 16`. Sizes 32/32/64/16/64/64/128
  logical pixels; native scales the 480-line layout uniformly (circles stay
  circular at 4:3, 16:9, 16:10). Lavaland has no `44eda9` case: its compiled
  initial point is used deterministically (no inherited mutable state).
- `434b10` supplies white RGB, alpha 80. `490cb0`: ONE/INVSRCCOLOR screen blend
  for ordinary devices (device 4 branch: additive ONE/ONE). Native screen path
  keeps opaque RGB images (black neutral). Sprites neither test nor write world
  depth, follow the 3D view before the HUD, consume no RNG, ticks or audio.
  Mega resolves sampled world geometry before drawing these single-sample
  overlays. SUNMASK indices are not reinterpreted as lens alpha. Mega: authored
  WebP80 at twice source size, ≤256 px, same slots and draw rectangles.
- IMAGE640 members 17 BGLOW and 18 HGLOW (see Vehicle lamps, Glow) are
  vehicle/effect images (`437640`, `447230` draw car/fragment sprites), excluded
  from background generation.

## Wheel and surface effects

Original wheel emitters run in normal Single Race driving on all ten tracks:
dry road slip → tyre smoke; gravel → dust and loose particles; grass → grass or
wet mud; rain/snow/storm enable water plumes and wheel trails. Emission is gated
by live contacts, retained tyre axes, slip, gear direction, source orientation.

### Source consumers

| Source | Native behavior |
| --- | --- |
| `431820/431ab0/431b20` | One 80-slot pool, original group order, lifetimes, smoke growth/fade, debris gravity, reset; shared 37-entry table/cursor with fragments and weather. |
| `432210/432d10/4333d0` | Per-wheel height gate, driven-axle/direction rules, mud/gravel/grass/smoke/dust dispatch. |
| `433b90/434030/434430` | Wet wheel trails, dry grass plume state, wet base plumes. Dry continuous plume state consumes its random values but is not drawn by `446f20`. |
| `432090/4347d0` | Sprite bounds, shade bytes, visible actor masks; local View 2 keeps one base plume even during AI handoff. |
| `4463e0` | IMAGE640 Smoke 0, Dust 1..4, Spray2 at 5, Mud 7, Gravel 8..11, Grass 12. Track groups 2,1,2,0,0,2,3,2,0,3. |
| `446a80` | Particle update, sign-fragment update, weather wind, vehicle emission, then HOR actors. No wind force on wheel particles. |
| `446f20/497070/497180` | Weather sprites, wet spray, mud/gravel/grass, smoke/dust, sign fragments, then weather lines. Sprite batches rejected whole when they would reach 128 queued sprites. |

- Smoke/dust/debris: original alpha blend. Spray: source flags 6,
  ONE/ONE_MINUS_SRC_COLOR. Both test depth without writing. Camera-facing
  geometry uses the scene projection at every aspect. Rendering/trace queries
  consume no random values.
- The physics adapter keeps `40db10` wheel tangents and `40b360` controls
  `+0x2c0` before body integration and passes those recorded values to the
  emitters (recomputing from the later body pose would shift effects during
  rapid rotation). Vehicle `+0x38` is the source lookup length of the three
  half-extents, not a longitudinal extent.
- Texture CRC32: Smoke `5e4adea6`, Spray2 `f44ac585`, Mud `e45cf274`, Grass
  `67a4c1d6`; Dust groups 0..3 `7c5ca251/950c9627/24d323f0/7a85f0ec`; Gravel
  groups 0..3 `66930bc7/1e026c9b/149dca20/66930bc7`.

### Timing, audio and lifecycle

Effects advance once per active 25 Hz deadline; menu/focus pause freezes state.
Qualifying-to-grid reset clears particles/emitters without resetting the shared
table cursor; Race Again creates a fresh scene. Low gravity removes the debris
gravity term. The particle callers contain no sound event; `44c540` tyre/surface
audio stays coupled to the same contacts: skid samples 39–49, grass 67, gravel
68, wet road 81. Grass/gravel rolling gain is zero in the current source View 2;
rain/storm use SOUNDWET, snow keeps SOUNDDRY despite wet particles. No synthetic
splash or gravel cue.

See Exhaust flames and backfire (BANG2, backfire sound), Tyre marks (own
consumer) and [spatial visibility](scene-rendering.md#scene-visibility-and-sector-order)
(sector selection/draw order). Remaining transparent-material and raster
comparisons do not yet establish complete-lap geometry or original/native visual
equivalence.

## Tyre marks

Original wheel-slip marks run in normal driving on all ten tracks. Each grounded
wheel uses actual longitudinal/lateral slip, body velocity, support plane,
surface class; the same slip state drives the original skid audio (no extra
sound or bitmap).

### Source consumers

| Source | Native behavior |
| --- | --- |
| `441c90/44ab90` | Scene setup, original track/weather colors, persistent sector invalidation. |
| `44b690` | Slip threshold `0x4ccc`, wheel contact flag, four quad corners, half-width `0x1999`, HPC height plus `0xa3d`; splits on surface/normal/direction changes and travel beyond three units. |
| `44b480` | 100 physical ring slots plus up to 99 running segments; original write order, per-view mask, `47e370` sector of the first corner. |
| `44c3a0` | Rain/storm road and curb marks wait 50 updates (counting the commit tick), then lose one alpha unit per update. Snow uses wet colors without fade. Transparent entries stay in their slots. |
| `44c3f0` | Grid reset clears counts and wheel slip state, preserving ring storage, aging and write cursor. |
| `446b50/44c430` | Invisible nonhuman actors close their last slipping segment with mask zero and clear slip before distant-body coordinates can extend it; returning actors start a new segment. |
| `441d10/442500` | Untextured vertex-colored triangles `(0,2,1)` and `(0,3,2)`, after terrain, before car shadows; source alpha blend, depth test and writes. |
| `446b50/44c540/424d50` | View callback builds marks and publishes skid-audio state; tyre samples 39–44/47–49 stay attached to actual slip. |

- Colors distinguish road, curb, grass, each track's gravel palette; Silverstone
  and Croft keep their wet color exceptions; Lavaland keeps its resolved dry
  weather. Geometry uses travelled wheel positions and the support plane (no
  projected steering input, no texture trail).
- Current projection for 4:3/16:9/16:10; per-view masks and original sector
  selection ([visibility](scene-rendering.md#scene-visibility-and-sector-order)); invalid sectors (incl. source
  scene-reset slots) rejected, no out-of-range lookup.

### Timing and persistence

- Original `420420→446b50` builds marks from the view callback; native
  generation and fading advance once per active 25 Hz deadline; drawing and
  diagnostics read-only; pause/focus freeze both queues and fade timers; no
  random values.
- The ring cursor survives grid reset and scene changes. Source drawing visits
  physical slots `0..count-1` (not chronological order); this quirk is kept,
  incl. cursor 100 wrapping immediately before the next write. New scenes
  invalidate old sectors and reset fading metadata, preserving ring bytes/cursor;
  counts start at zero (restart shows no old marks). Qualifying-to-grid reset
  follows `44c3f0` and keeps old ring sectors.

### Distant-opponent correction — 2026-10-18

The scene passed every opponent to mark generation with visibility 1, including
distant bodies whose four wheel heights remain from their last full-body update
(`444ee0`): moving XZ plus old heights produced floating trails. Marks now share
the audio queue's human/full-body/visibility gate after scene selection; `44c430`
closes hidden segments once; reappearing opponents cannot bridge unseen travel.
Effect (Donington NoQual, point 59 after 1600 ticks): identical body positions,
speeds, route progress; emitted segments over 800 ticks 1009 → 394.

## Original vehicle shadows

`scene/car_shadow.cpp` ports the `437770` geometry producer. Output keeps the
original `498400` record: eleven signed Q16 positions and a `498430` type 0, 1
or 2. No camera-derived silhouette, no car-mesh change, original texture cutouts
kept.

### Inputs and arithmetic

`4608b0` publishes the body/contact snapshot consumed by `437770`:

| Original offset | Native input |
| --- | --- |
| `44c`, `458` | Cached ground normal and body position |
| `464..4c0` | Eight body contact-box points |
| `4c4`, `4d0` | First body axis and body up (incl. near-vertical fallback) |
| `4a0`, `440`, `4a8` | Contact 5 X/Z with sampled ground height |
| `1e4`, `15c` | Current ground normal and current body up |
| `515`, `516` | Actor index and car profile index |

- The first axis is projected onto the cached support plane; at absolute
  alignment ≥ `fd71`, body up is projected instead. Positive extents of all
  eight contact points give the rectangle. Car 9 scales its second extent by
  `11999`; actor 0 in original state 18 scales the two extents by `21999` and
  `16666` (state 18 ≠ the Micro Machines cheat). Ground sample = projected
  center. Every product shifts before wrapping addition; normalization uses the
  source midpoint-sqrt table.
- `44eda9` creates the scene light with `49da90`, then scales stored X/Z left by
  five; `434f00` normalizes those scaled horizontal coordinates and negates the
  result for shadow casting. Squared Q16 products wrap (float normalization
  would change the result). Same for all ten tracks.
- Weather 3/4/5 → rectangular type 0 record. Dry: sunlight projected against
  the current ground normal, normalized, extended by `c000`. The four sign cases
  keep original base and translated edge orders. Ground-normal/body-up
  alignment < `4ccd` → type 1; otherwise the car's complete four-point side
  profile is added → type 2. All ten profiles from `434f00`, table `4dadd0`,
  stride `30`; silhouette height, lateral taper, fixed-width adjustment,
  reversal, shared endpoints keep the original thresholds/arithmetic.
- Multiplier `1 - min(mul16(distance, -ccc), 9999)` from `437f3f` retained. The
  distance global `4dbe74` begins at zero and has no recovered producer (only
  its read at that site); the adapter supplies zero. Jumping invents no
  shrinking/fading: raised body/contact points project onto the sampled support
  plane; airborne motion keeps the last valid ground normal; ground sampling
  and the normal refresh at landing. The scalar branch is tested independently
  without an original runtime caller for nonzero values.

### Rendering

- `498130` loads LOWIMAGE members 0/1, SHADPC RGB and SHADAM raw indexed alpha.
  `498430` emits the type 0 strip, type 1 side strip/fan, type 2 complete
  silhouette/fan; core ARGB `c8000000`, extension ARGB `aa000000`, source UVs,
  fixed midpoints. Capability branch with depth test/write disabled and no Y
  lift; drawn after terrain, before every scenery/car model; separate
  color/alpha images and source blend unchanged.
- `484bc2` reduces SHADAM indices to the matched original's A4R4G4B4 alpha
  before bilinear sampling: `(index >> 4) * 17` in the native RGBA8 upload; all
  4096 packed original upload texels match, CRC32 `95b0f62e`
  ([material comparison](graphics-comparison.md#vehicle-shadow-alpha)).
  `486fbf` keeps CLAMP; the shadow draw selects the bilinear clamp sampler so
  UV 0/1 cannot blend opposite image edges.
- Original type 2 submission matches all 15 positions/colors/UVs, 36 indices and
  world/view/projection/viewport state (capture CRC32 `1fdcbd5b`). The apparent
  larger remaining shadow came from a stale original screenshot: pausing WineDbg
  also paused the graphics workers; the schema 2 presentation barrier drains
  them while the game thread stays suspended. Schema 1 images are not matched
  raster proof; no shadow-geometry or draw-order workaround is justified.
- Goldens came from an independently compiled local harness of the `437770`
  decompilation (untracked); a seeded comparison matched all consumed Q16
  coordinates and record types in 500 cases (ten profiles, rotated boxes, sloped
  normals, near-vertical fallback, four light quadrants, three types, threshold
  sides, state 18, dormant distance branch). Reference CRC32: `tourcars.exe`
  `8ce56d41`, ALLCARS `92273ec9`, LOWIMAGE `0f4c1a48`.

## Vehicle lamps

Evidence: `references/decompiled/tourcars.c` (unchanged ToCA 1.1a).

- `004367f0` selects front/rear from the physical root's forward axis and the
  normalized root-to-camera vector, using the quantized `53d840` asin table.
  HIGH selects dynamics[1] (animated body), MEDIUM dynamics[0] (physical root).
  Broken slots 0/1 suppress front lamps; slots 2/3 suppress positive/negative
  rear emitters. `00446d30` excludes Type8 and cars absent from the current view.
  Alternate Vectra model 10 uses car 2.
- `004392d0`/`00439500`/`00439720` emit the untextured brake/tail/head polygons
  through `00499580`; ARGB `ffff0000`, `ffc80000`, `ffffffe6`. Detail 0/1 keep
  adjusted rear coordinates; detail 2/3 add the authored extra rear sections.
  Original profiles keep these silhouettes; Mega clips textured intact glass
  against these contours (rebuilt depth, animated node, interpolated atlas UVs).
- `4608b0` publishes raw brake `dynamics+144` to `body+4e8` before the view
  callbacks. `446b50` writes `body+508` only for `view == 0` and human
  `gameplay+8 != 0`: published pedal ≥ `1999` and `dynamics+304` zero; the write
  precedes render bypass and visibility gates. Other views and AI keep their
  lamp output. Native frame publication and lamp callback are separate; control
  changes and read-only lamp queries cannot update the retained lamp;
  controller handoffs preserve it. Pedal publication covers every actor in
  descending slot order (AI, hidden, distant) and is kept until the next
  publication.

### Glow

`00436c80` → `00437640`: two camera-facing billboards per illuminated end,
separate from the solid polygons. `00434da0` loads (identical in
IMAGE320/IMAGE640):

| End | Member | Original image | CRC32 |
| --- | --- | --- | --- |
| Rear | 17 | `IMAGES\BGLOW.BMP` | `3ac4f26c` |
| Front | 18 | `IMAGES\HGLOW.BMP` | `e3bed0e7` |

- `00434f00`: model-specific emitter centers incl. final longitudinal ±`3333`.
  Front rectangles ±`8000` Q16 (one unit across), rear ±`6666` (~0.8 units). The
  second center adds the separately shifted body-Z product `-2*localZ`; both
  follow body tilt. Two emitters even for Patriot; Type8 none.

| Weather | Headlight | Tail | Brake |
| --- | ---: | ---: | ---: |
| Sunny/cloudy 1/2 | 100 | 30 | 60 |
| Rain/storm 3/4 | 200 | 70 | 100 |
| Snow 5 | 150 | 60 | 100 |

- Pre-fade intensities. `004367f0`: `fade = angleQ16*02d0>>16` from the angle to
  the selected end (0–90°); `00436c80` publishes
  `(intensityQ16 - intensityQ16*fade>>16)>>16`. The lookup leaves a small fade
  even directly ahead/behind (ten-unit axial camera: fade 126, sunny brake 59);
  side-on rear branch fade 64800.
- Hardware draw `00497070` flags 6: opaque alpha, attenuated intensity in RGB,
  depth test without writes, camera-facing, ONE/INVSRCCOLOR; shared strict
  127-sprite queue limit. Availability checks texture success and rendering
  capabilities; no graphics-detail gate (every native detail uses it).

### Separate wet-ground effect

- `00436790` enables rain/storm/snow reflections per wheel whose retained
  surface class is asphalt 0 (contact flags do not gate). `4608b0` publishes
  wheel positions and sampled plane heights to render-body `464`/`42c`; the
  native physics adapter supplies the same inputs for local, rival, Linkup
  actors; presentation interpolates positions/heights while surface eligibility
  and body mode keep the current tick.
- `00436c80` mirrors emitter Y around each wheel's sampled height. Rear records
  add `3333` and clamp their offset to ≤ `-4ccc`. Positive wheel-to-plane
  distance attenuates brightness by `min(distance - authoredLampLocalY, 10000)`
  (Q16); no lower clamp; stored as a byte (flight fades via actual sampled
  distances). Body mode 1 (`4f0`) = distant-body branch: emitter Y offset
  `-2*(authoredLampLocalY + bodyHalfHeight)`, `3333` added at the rear;
  independent of airborne contact state.
- Wet intensities head/brake/tail 150/150/70; snow 90/80/60, before
  angle/distance attenuation. `00437640` sends the displaced rectangles to
  `00442210` flags 1 with HGLOW/BGLOW, separate from solid lamps and halos.
  Front extents ±`b328`/`3ffc`, rear ±`8f56`/`3331`. Source 32-record queue,
  ONE/INVSRCCOLOR, ALWAYS depth comparison, depth writes enabled. All profiles
  incl. Mega use this path, with broken-side and Type8 suppression.

### Mega glass

Mega keeps the existing glass artwork, creates red-only rear emission masks at
upload, draws them on clipped animated lens surfaces with screen blending. Clear
indicators, ribs, gaskets, untextured housings stay distinct. Glass sampling
keeps the model's mirrored UV wrapping. Patriot's body-integrated lamps use extra
depth/face clipping; Type8 keeps its absence of lamps. Distant MEDIUM rivals keep
the source polygon overlay. Hidden or broken mesh variants cannot draw intact
Mega glass.

## Exhaust flames and backfire

The BANG2 consumer produces exhaust flames; its generic trigger follows an
increase in engine speed; the inspected consumer does not read engine damage, so
no damage-triggered smoke/fire rule exists.

### Original consumers

| Source | Recovered behavior |
| --- | --- |
| `4209d0` | Initialize 16 burst slots, cooldown 900, phase 2, ten car-specific exhaust anchors. |
| `420bb0`, first call in `446a80` | Decrement a positive cooldown; cycle phase 2→1→0→2. |
| `4608b0→420be0` | Clear the output list, then emit for actors with a nonzero original view mask; hidden actors keep burst age. |
| `40b360` | Preserve controls+0x124 engine speed in +0x128 before `40d3b0/40d5f0` (not throttle inputs). |
| `4469a0→420b80` | Clear active bursts and output sprites at grid reset, keeping cooldown and phase. |
| `4463e0`, `446f20` | Load BANG2; queue its sprites after wet spray, before wheel mud/gravel/grass/smoke/dust. |
| `424860` | Event 17 selects sample 64 with the original strength/local-or-remote spatial path; no random sample selection. |

- Generic trigger needs all of: engine speed delta (current − previous) strictly
  > `0x9999`, gear nonzero (reverse eligible), shared table cursor 0, phase 0,
  cooldown 0, inactive actor slot. Emits event 17 at strength 65536, starts
  cooldown 900, creates the first sprite. Fresh setup with constant eligible
  inputs and cursor 0 first triggers at active tick 902. The sound carries the
  permanent actor/human identity (finish AI handoff does not make a local human
  remote); rivals use the source remote gain; Linkup treats only this peer's
  actor as local.
- Each generic burst has three visible emissions:

| Burst age | Backward offset along current body-forward axis | Half-size | Shade |
| --- | --- | --- | --- |
| 0 | 0 | 6553 | 200 |
| 1 | -13107 | 9829 | 160 |
| 2 | -26214 | 13105 | 120 |

The next eligible emission call clears the burst. Sprites follow the current
vehicle pose. Q16 local exhaust anchors from `4209d0` by car type 0..9:

```text
0 (-148307,-27656, 23789)   1 (-145424,-27131,-18022)
2 (-144113,-22020,-18022)   3 (-140771,-31588, 38076)
4 (-149815,-25624,-15400)   5 (-142934,-28049,-15859)
6 (-142213,-24903,-40173)   7 (-144965,-30015,-16121)
8 (-149356,  6553,     0)   9 (-176947,-28573,-19660)
```

Anchor transformation keeps `47b150` rounding; forward offsets use the original
separate fixed products.

### Type8 Time Trial branch

Car type 8 in mode 2 (Time Trial): `420be0` emits two sprites on every visible
emission call regardless of RPM, gear, cooldown, phase. Centers: current exhaust
anchor and that anchor minus `0x1999` × body-forward axis. Half-sizes `0x3333` and
`0x3d70`, each reduced by the fixed product of its shared positive-table value
and `0xccc`. Each sprite advances the 37-entry cursor once. No sound, no
cooldown restart. Brightness expression kept as in the source: subtract
`fixed_mul(table_value, 0x960000)` from integer 200, divide by 65536 toward zero,
keep the low byte (confirmed at `420cf9/420ea0`; not a clamped formula).

### Resources, scheduling and reset

- `data/image640.bfp` member 16 = 16×16 8bpp BANG2 bitmap: 1334 bytes, CRC32
  `293546a6`. Source flags 6: screen blending, depth test without writes, shade 0
  as RGB. Actor view masks filter sprites before the shared whole-batch
  127-sprite queue limit.
- Original `4608b0` emission runs before `446d30` weather view updates. A setup
  call in `44ffe1` follows `4469a0` (actors get view bit 1): type 8 Time Trial
  emits two sprites and consumes two table values before the first tick.
- Native updates/emission run once per active 25 Hz deadline; pause/focus freeze;
  drawing consumes no RNG. Emission consumes the completed previous car-effect
  mask; the following view selection publishes masks for the next deadline;
  hidden/distant actors keep burst age while shared cooldown/phase continues.
  Grid reset reinstalls the initial mask before setup emission. Local effect
  bit 1 is independent of geometry visibility (see
  [visibility](scene-rendering.md#scene-visibility-and-sector-order)).
  Generic bursts consume no RNG or table steps; only type 8 advances the shared
  table (order relative to weather matters).
- A new scene initializes cooldown 900/phase 2 (grid reset keeps the shared
  cursor as well). Event 17 → sample 64 once per generic burst; type 8 silent.
  `424b50` admits one sample 64 voice across active local views, selecting the
  loudest spatial listener (later listener on ties); camera gain selection keeps
  the source's final-listener transaction; a second view neither duplicates the
  sound nor requires it to be audible to player 1.
- NUSMOKE3 is tyre smoke (`4333d0`). The inspected damage/light geometry
  consumers establish no engine smoke/fire emitter; mechanical damage behavior is
  separate. Native contact runs supply no positive generic actor-bound type17 event, so original
  actorless capture callbacks remain explicit comparison inputs. Not established: damage-triggered motor fire or full original screenshot equality.

## Anti-Aliasing: 2× MSAA in the Mega Profile

The 3D scene uses **2× MSAA in the Mega profile only**, without an extra switch; Low,
Medium, High and Very High stay at one sample. The profile of the loaded scene
decides (also split screen and Championship finale; split screen with separate
viewport coordinates). HUD, pause, results, finale credits and menus stay on the
single-sample path. If a device does not support exactly two samples for RGBA8 and
D16, it stays at one sample and logs this once (no fallback to 4×).

SMAA (shaders/lookups/comparison selection) is removed; the comparison remains
reproducible with tool and sources in commit `216c1ea`. Against ground shimmer:
mipmaps with trilinear filtering for opaque ground textures
([Mega material plan](mega-track-materials-plan.md#contract-for-further-work)).

### Decision

Measurement: RTX 5060 Ti, Vulkan, Release, Donington sunny, Mega, Renault;
host time (upload, render commands, GPU completion; without swapchain/PNG readback;
no desktop FPS claim). Median 2560×1440: off 1.507 ms, 2× MSAA 1.594 ms,
SMAA 1x Medium 1.770 ms (1920×1080: 1.550 / 1.608 / 1.782 ms).

- 2× MSAA smooths vehicle/object contours for about +0.09 ms at 1440p and
  preserves texture sharpness. SMAA improves some logo interior details, looks
  softer, and shows no advantage on the road lines in question. Both
  keep distant patterns and visible line steps. P99 outliers are no
  evidence that AA removes stutter. No long driving test across all
  track/weather situations was performed.
- Additional memory at 1440p: about 42.2 MiB (2× RGBA8 + 2× D16, computed from the
  formats, without driver alignment).
- D3D12/Metal were not run on hardware.
