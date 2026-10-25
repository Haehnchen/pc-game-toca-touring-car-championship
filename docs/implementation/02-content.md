# Original content and rendering integration

Data semantics are recovered from the decompiled 1.1a consumers, then connected
to the common application. [Foundation](01-foundation.md): startup, asset
ownership, comparisons; [display](05-display.md): viewport/projection policy.
[Shared batch rules](../porting-plan.md#delegation-and-acceptance-rules).

## Existing evidence and useful entry points

- [Asset catalog](../assets.md): 1047 members from 59 BFPs plus loose-file
  provenance; all archives reconstructed byte-identically.
- [CM3D](../cm3d.md) covers all 33 car files / 696 meshes; runtime materials,
  hierarchy and variants are described in C02/C03 below.
- GPU upload checks preserve decoded title/palette RGB; scene projection and
  material acceptance use the separate contracts below.
- Title BMP/PAL and all 33 car models load from the canonical asset store.
  [Resource evidence](../frontend-options.md#frontend-and-car-resource-contracts): original car-ID map, font/
  localization findings.

| Export/corpus evidence | Bounded recovery lead |
| --- | --- |
| `0x004688fc` opens `data/frontend.bfp` (69 members); menu pacing at `0x00469f10`/`54`/`9b`. | Trace menu resource reads, state dispatch, input and timing before reproducing the flow. |
| `data/config.bfp`: `AIBIN.DAT`, `QUALPC.BIN`, five `GSTRINGS` language members. | Decode localized string consumers separately from AI/configuration tables. |
| `0x0044dc6d` opens `data/allcars.bfp`, selects an index, loads three consecutive CM3D members via `0x00494a40`. | Recover car/detail/variant selection from caller and data; names alone are insufficient. |
| `0x0044e7bb` loads KEEP, FREE and horizon content; `0x0044e673`/`0x0044e717` map FREE/KEEP archives; `0x0044e5cf` maps `*HOR.BFP`. | `0x004512b0` selects horizon BMP/C3D names, environment variants, visibility/projection parameters. |
| `0x00495ee0` consumes named texture/material blocks; `0x00483261` decodes BMP data into surfaces (palette/pixel conversion). | Trace node/variant flags in CM3D and draw consumers separately from embedded images. |
| `data/donlkeep.bfp`: `GROUND.HPC`, `POINTS.DAT`, `PITS.DAT`, `SGRID.DAT`; first member reaches `0x0042f1c0`. | HPC is a distinct runtime structure; collision/surface/query meanings are proven via callers. |
| `data/donlfree.bfp`: `LOWPOINT.DAT`, `GROUND.C3D`, then trees, fence, cameras, objects, other CM3D members. `GROUND.C3D` reaches `0x004962e0`. | Terrain rendering is separate from HPC; neither a `.C3D` suffix nor shared magic proves identical runtime use. |
| Code lookup `0x0047739f`, table `0x004b9728`, mode policy `0x00477409`; G09 records handlers/effects. | Bonus-car and all-track effects confirmed; a single original all-content opcode is not established. |

These are observed identities and call relationships, not complete format
claims. HPC consumers beyond the Donington height-query subset still need
bounded recovery/validation.

## C01 — Frontend, fonts and localized selection flow

Frontend content/state code; gameplay owns race behavior.

Integrated route: title → copyright/loading → three-language selection →
localized main menu → Single Race (options, difficulty, driver-name editing,
2D car-selection garage, transmission) → Car → Track → Back → scene in Donington
and back. Original fonts/icons, keyboard actions, retained choices.
[Startup scope/evidence](../frontend-startup.md), [selection contracts](../frontend-selection.md),
[track contracts](../frontend-selection.md#track-selection-and-frontend-commit-boundaries), [preview rendering](../frontend-selection.md#track-selection-resources-and-preview).

- Cue IDs 0/2/4 (startup/menu), 1/3 (selection); CD02 continuous while
  browsing; track entry CD02→CD03→CD02. Original CD track 02 starts at
  `0x004688fc`'s frontend music call (finite playback, restart/stop, native
  global gain/mute via G08, `assets/music/track02.ogg`; [music](../music.md)).
- Track selection: original fresh availability, stored selections, localized
  track/lap data, rotating `LILTRAX.BFP` miniatures; all ten tracks enter scenes.
- Loading uses original artwork/bar, selected weather, qualifying/grid body
  placement, default camera. Held-repeat, pause menu, live race state are separate tasks.
- Music enable/volume fields feed the shared settings UI; exact Miles gain/pan
  stays with G08/F06a comparisons.
- Settings UI over F02/W01/G07 config/CLI service; effective values and
  apply/save errors shown via the same preference contract.
  [Native settings](../frontend-startup.md#native-settings): resolution/
  fullscreen/VSync, FPS caps behind Graphics Setup. Original Sound Setup:
  effects/CD volume, game music, stereo/mono effects; Controller Setup prepared.
  [Original options](../frontend-options.md).
- Graphics Setup: four detail presets via the shared preference service and next
  scene's resource/projection consumers; selectable/saved split-orientation row
  for two-player mode; adapter selection retained for a future backend.
  [Graphics options](../frontend-graphics.md).
- UI drawn in the original logical baseline through W01's viewport mapping;
  widescreen menus extend only the background
  ([menu background](../frontend-startup.md#widescreen-menu-background)); five
  icon rows per graphics page, equal-size resolution/aspect text; complete
  generated widescreen title artwork, canonical 4:3 kept
  ([title](../frontend-startup.md#widescreen-title)).

- [ ] Trace `0x004688fc` and its bounded resource/state callees; map language,
  main menu, car/track selection, race setup, loading, back/cancel and return paths.
- [ ] Decode FRONTEND BMP/FON resources, palettes, clipping, glyph metrics,
  localized GSTRINGS IDs/encoding and shared screen data from their consumers.
- [ ] Compare one reproducible language/menu route before extending remaining
  screens; add the navigation/localization hook for C06 and meaningful state/glyph checks.

## C02 — Actual materials, transforms and draw order

Completed shared SDL GPU adapter: recovered materials, transforms, lighting,
clipping, source order and resource lifetimes. Camera/game state remains with
its gameplay owner. [C05j results](../graphics-comparison.md#full-lap-and-raster-scope).

- Resident terrain, baked scenery, race/cockpit/finale models share immutable
  geometry and per-instance matrices. CPU owns hierarchy, LOD/damage, visibility
  and fixed-point queue construction; the shader owns projection/lighting and
  recovered PC53 stores. Finale retains its separate arithmetic order.
  [Model consumers](../scene-models.md#original-model-consumers).
- Prelit billboards, shadows, skid/lamp/fragment geometry and rain/spark/grid lines
  retain admission, topology, clipping, depth, blend and source order. The
  reference profile supplies the measured TL/P8 and rectangular-line convention;
  DASH cockpit depth and original HUD TL/UV stores are covered.
  [Material/raster contract](../graphics-comparison.md#shader-color-and-sampling).
- Scene-local mesh/texture caches, fenced uploads, reusable frames and pass-local
  bindings preserve queued ownership. Two color targets retain A/B/A history;
  MSAA, overlays, UI and resize have separate checks. Original mode/palette
  lifetimes map to SDL/RAII. [Resource contract](../graphics-prototype.md#resource-ownership-and-reuse).
- `TOCA_RENDER_12` rebuilds canonical actor worlds from bounded presentation
  inputs, including both ghost owners, HIGH/MEDIUM/LOW and actual camera switches;
  prior source pixels are never imported. GPU/native, cockpit/HUD, reader and
  representative Mesa ASan/UBSan checks pass. [Comparisons](../graphics-comparison.md).
- Frontend miniature keeps its explicit native coordinate correction and source
  initial ambient illumination. [Preview contract](../frontend-selection.md#track-selection-resources-and-preview).

## C03 — Car assembly, detail and visible variants

CM3D decoding and visual assembly; simulation owns vehicle state. Wheels/damage
coordinated with G04/G06/G08.

- ALLCARS member mapping, CM3D/material decoding, hierarchy, runtime wheel/
  steering transforms, damage variants integrated. Local HIGH/MEDIUM families
  and all four original presets have draw-boundary comparisons.
  [Model/detail](../graphics-comparison.md#matched-vehicle-and-detail-comparison), [damage bindings](../contact-effects.md#original-car-damage-presentation),
  [CM3D/GLB](../cm3d.md).
- Car shadows `437770`/`498430`: original support-plane projection, dry
  directional silhouettes for all ten cars, weather gates, airborne/landing
  support. [Car shadows](../visual-effects.md#original-vehicle-shadows).
- All eleven Mega exteriors V00–V10 bundled; pivots/artwork/HIGH overrides/
  fallback in [vehicles](../porting-completed/vehicles.md#exteriors).
- Immutable decoded data/GPU resources are separate from per-car instance state
  (transforms, variant choices).
- Runtime player-name decals: `44df6e` → `464423`/`49ae57` and `4437f0` →
  `49b1f8`; independent local/rival/Ghost names, original/Mega material
  bindings, 2× Mega WIN3 font, original font metrics/bonus/320-pixel gates.
  [Name rendering](../porting-completed/vehicles.md#runtime-player-names).

- [ ] Complete the separate cockpit/dashboard resource work in
  [K0/K00–K09](../mega-vehicles-plan.md#k0-and-per-vehicle-interiors).
- [ ] Validate remaining damaged/cockpit/LOW families against the existing corpus;
  compare wheel/steering/variant changes and engine/damage audio as gameplay states land.

## C04 — Donington terrain, objects and spatial data

Track decoding, scene assembly, query data; gameplay owns response/rules.

- Donington subset: terrain node transforms, UV rules, start-grid records,
  scene camera matrices, selected object detail, car triple, weather-selected
  horizon. HPC, route, pit, horizon auxiliary bytes preserved without runtime
  semantics. [Scene resources](../scene-resources.md), [lifecycle](../scene-lifecycle.md).
- HPC height-query tree and plane arithmetic decoded; qualifying/grid XZ/heading
  placement separate from body height/tilt ([placement](../scene-rendering.md#start-placement-and-collision-surface)).
  HPC-supported initial body height/tilt and separate View-2 follow anchor
  ([body/camera](../scene-rendering.md#initial-body-and-follow-camera)).
- POINTS/LOWPOINT, PITS, SGRID consumers distinguished (render, racing line,
  pit, grid, timing). POINTS route/lap and SGRID starts integrated; all ten PITS
  members decoded, automatic local entry/guidance/exit on nine supported tracks
  (authored gates, actual vehicle motion, lap progress, temporary barrier
  suppression, original captions/audio, grid/focus lifecycle);
  [pit lanes](../pit-lanes.md). Full-grid pit decisions stay with G05/G06.
- C04a: original sector spiral, static bounds/registration, ordered terrain/
  scenery/root draws, root migration/LOD, effect masks; camera-ancestor sectors
  preserved; actual widescreen projection; effect visibility advances with
  simulation, independent of render FPS. [Spatial visibility](../scene-rendering.md#scene-visibility-and-sector-order).

- [ ] Follow `0x0044e7bb`'s actual Donington GP KEEP/FREE/`*HOR.BFP` member order,
  horizon archive map `0x0044e5cf` and environment selection `0x004512b0`;
  recover terrain origin/scale/axes/units through specialized `GROUND.C3D` consumer `0x004962e0`.
- [ ] Decode visible terrain, horizon, object placement, detail selection,
  cameras and draw/culling order; validate non-car CM3D variants before expanding the parser.
- [ ] Decode HPC beginning at `0x0042f1c0` and its query callers independently;
  establish topology, height/surface and collision meanings with small local byte observations.
- [ ] Expose owned immutable scene/query data to collision, AI and race code;
  integrate menu-loaded Donington, any observed track ambience and G04/G08 surface/contact events together.
- [ ] Compare known bends, elevation changes, starts/pits and object placement;
  add structural bounds tests and targeted query checks rather than a new editor/export pipeline.

## C05 — Remaining tracks, dashboards and race content

Per track family; supported tracks are selectable and drivable through the menu
even when AI/results or later visual details are missing.

| Family | Source / music | Reference |
| --- | --- | --- |
| Silverstone | SILVKEEP/FREE/HOR, HPC, starts, scenery, lighting, weather; CD04 | [scene-resources](../scene-resources.md#silverstone-integration) |
| Thruxton | THRUKEEP/FREE/HOR, distinct reduced route data; CD05 | [scene-resources](../scene-resources.md#thruxton-integration) |
| Brands Hatch | BRANKEEP/FREE/HOR; CD06 | [scene-resources](../scene-resources.md#brands-hatch-integration) |
| Oulton Park, Donington Short, Croft, Knockhill, Snetterton, Lavaland | recovered loaders incl. source schema exceptions | [track matrix](../scene-resources.md#remaining-track-integration) |

Sub-packages (all integrated):

- C05a — FREE members 8–11 (`CONES`, `POSTS`, `SIGNS`, `FLAGS`) via original
  consumers: authored placement/basis, detail gates, Lavaland POST/SIGN skips;
  original three-frame flag animation independent of render FPS. Qualifying
  camera start angles from `45eb60` before State4 follow update; SGRID init kept
  for ordinary races. [Objects/cameras](../scene-resources.md#track-props-and-starting-cameras).
- C05b — HOR members 0/1 are the two moving background-actor groups
  (`43cf60/43d330`, `43e2a0/43e630`); track/weather selection, original motion,
  Lavaland excluded; the actor functions emit no sounds.
  [HOR](../scene-resources.md#moving-horizon-actors). Separate from opponent gameplay.
- C05c — prop contacts: `421980→45b900` and per-kind dispatch; state updates
  `458ac0`, `45a460`, `45b010` (cone motion, post bending/return, sign tipping),
  high-speed `45d4a0→417cd0` sign-fragment path, CONE/SIGN sound events;
  registration limits and track/detail gates preserved. Vehicle response/damage
  state shared with G04/G06 (no second vehicle simulation). C05a only loads/draws
  objects; contacts come from C05c. [Prop contacts](../prop-contacts.md).
- C05d — pits: forward entry, automatic local guidance, ordered exit, temporary
  barrier gate on nine tracks; Lavaland excluded; ordinary lap progress intact;
  captions/sample75 coupled to focus/grid lifecycle. [Pit lanes](../pit-lanes.md).
- C05e — atmosphere: rain, snow, wind, storm flashes, delayed thunder 76/77,
  CMRAINUP/CMCATDOG particles; shared effect-table state, camera-relative
  wrapping, source texture selection and draw states.
  [Weather effects](../visual-effects.md#atmospheric-weather).
- C05f — wheel effects: smoke, grass/mud/gravel, continuous plumes, water
  trails; `431b20/432210/432d10` and trail consumers; shared effect-table order,
  pool limits, lifetime, track/weather texture choices; sprite batches and spray
  blend state. [Surface effects](../visual-effects.md#wheel-and-surface-effects).
- C05g — tyre marks: `44b480/44b690` builder, shared 100-segment ring, running
  segments, wet fading, sector assignment, reset/persistence; `441d10`
  untextured ground quads with source blend/depth/order. [Skid marks](../visual-effects.md#tyre-marks).
- C05h — exhaust: BANG2 flames, engine-speed gates, three-update burst/cooldown,
  Time Trial car8 branch, event17/sound64, shared sprite queue. The inspected
  damage consumers establish no separate engine smoke/fire emitter.
  [Engine effects](../visual-effects.md#exhaust-flames-and-backfire).
- C05i — dry Honda laps on all ten tracks through CPU and normal-menu player
  tests ([track acceptance](../track-acceptance.md)); all ten tracks have data/
  render/driving coverage. Matched original recordings, wet/snow laps and
  remaining content/detail combinations stay open.
- Loading: all ten Mega loading bitmaps plus original five-language dynamic
  statistics with separate source-metric bitmapfonts; original image/progress
  division and BFP fallback. [Loading contract](../scene-lifecycle.md#mega-loading-bitmaps-and-original-statistics).
- Weather-selected skies, lighting and grip exist. No fog activation was found
  in the audited precipitation or scene draw paths; a proven original caller is
  required before adding one.

- [ ] Validate all remaining CM3D/HPC members and original car/track/detail
  combinations; fix proven variants in the shared loader rather than special-case alternate paths.
- [ ] Recover dashboards, HUD images, skies/weather/effects and results
  resources with their consumers; identify accompanying sounds from those callers.
  Weather-selected skies, lighting and grip already exist; atmospheric
  effects are tracked in C05e, wheel/surface consumers in C05f, tyre marks in
  C05g and exhaust flames in C05h. The inspected damage consumers establish no
  separate engine smoke/fire emitter. No fog activation was found in the audited
  precipitation or scene draw paths; require a proven original caller before
  adding one. Remaining material/raster comparisons stay with C02.
- [ ] Complete frontend routes/settings for the implemented original content;
  coordinate championship/unlocks/results with G06/G07 state and save ownership.

## C06 — Native Extras controls

Depends on C01 and G09's effect/state interface; persistence follows G07.

- Localized Extras entry before Quit with icon, four effect categories, Yes/No
  switches, ordinary parent/back navigation; controller hardware navigation via
  the shared input adapter (fixed keyboard/gamepad menu actions also drive Extras).
- Separate All tracks (includes Lavaland) and Bonus cars switches; disabling
  either preserves earned progress. All 19 reversible switches bound to
  persistent flags and runtime effects; ordinary menu cues.
- Typed-name redemption keeps original action codes and activation speech;
  `CMLOCK`/`CMGARAGE` keep original action semantics, `CMDISCO` its original
  rejection. No global clearing or destructive reset actions.
- Original mode restrictions enforced in typed redemption, car/track selection,
  shared-session effects. [Per-code catalog](../cheats.md).
