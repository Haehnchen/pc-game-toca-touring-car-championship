# Supported track scene resources

Selection applies original fresh locks; all ten tracks and bonus cars 8/9
(original parameters and actor mapping) are reachable through Extras unlocks
([codes and effects](cheats.md)).

`scene::load_scene_resources(store, setup)` implements all ten track branches of
`0x0044e7bb`; validates setup against `resolve_setup` before opening files.
Input: canonical asset store; output owns its storage (no extracted files, Wine
or graphics context).

| Source | Original sequence and native result |
| --- | --- |
| `0x0044e717`, `0x0044e7bb` | `DATA/DONLKEEP.BFP`: 0 `GROUND.HPC`, 1 `POINTS.DAT`, 2 `PITS.DAT`, 3 `SGRID.DAT`. HPC/POINTS keep query bytes; PITS decodes to owned gate/target records. |
| `0x00460160`, `0x00401190` | SGRID: 32 records × 20 bytes: two signed 16.16 X/Z endpoint pairs, signed 16-bit heading, reserved word. First 16 = grid strips; later code overwrites the second group for session starts. Decoding does not choose a player slot or height. |
| `0x0044e673`, `0x0044e7bb` | `DATA/DONLFREE.BFP`: detail 3 skips 0 `LOWPOINT.DAT` and keeps KEEP POINTS; details 0..2 replace it with LOWPOINT (detail at config +0x328, not weather). |
| `0x0044e7bb` | FREE members 1 `GROUND.C3D`, 2 `TREES.C3D`, 3 `FENCE.C3D`, 4 `CAMERAS.C3D`; objects: 5 `OBJECTS.C3D` (detail 3), 6 `LOWOBJ.C3D` (1/2), 7 `ZEROBJ.C3D` (0). Cursor skips keep this order. |
| `0x004962e0`, `0x00496980` | Ground: CM3D100 container; terrain consumer bakes each referenced mesh through that node's complete fixed matrix, no parent hierarchy multiply. Chunks keep node/variant identity, X/Z origin, final-byte face flags. |
| `0x0047c7e0`, `0x0047c900` | Each signed 16.16 product shifts before 32-bit wrapped addition; only positions get translation. `0x00496980` writes transformed normals back to the source mesh: repeated references cumulatively transform decode-local fixed normals. Positions unchanged. |
| `0x00496980` | Terrain UVs: subtract 1 once when U > 1, add 1 to V, clamp both to 0.02..0.99 (no reversal of generic parser UV folding). |
| `0x004510bf` | Cameras: transform-only CM3D nodes (zero mesh/texture offsets); separate bounded decoder keeps 16 raw fixed matrix elements, enforces the 50-node capacity. |
| `0x0044dc6d` | Selected car: ALLCARS cursor skip then three CM3D reads (Honda indices 1,2,3; Audi 4,5,6); recovered local-player LOD and intact variant. |
| `0x0044e5cf`, `0x0043cf60`, `0x0043e2a0`, `0x00431440` | `DATA/DONLHOR.BFP`: members 0/1 for auxiliary actors (kept as bytes), skip the resolved horizon index, read member `2 + index`. Default cloudy Donington `DO023D_1`/`DO02`; storm `DO053D_1`/`DO05`. |

- Donington terrain: 304 mesh references, 15 embedded textures; camera resource
  24 transforms. No palette/material/GPU state invented by the loader.
- Terrain keeps `4962e0`'s signed matrix X/Z translation for every non-root node
  (also nodes without drawable variants), source order; these centers feed the
  [barrier sector lists](barrier-contact.md#original-sector-candidates).
- Counted CM3D texture sections of OBJECTS/LOWOBJ/ZEROBJ end one byte before the
  BFP member ends; the byte is zero. `0x00494a40` returns after its counted nodes,
  `0x00495ee0` loops exactly the declared texture count, no footer consumer.
  Parser keeps it as `Model::trailing_null_byte`; nonzero/multiple trailing bytes
  rejected. Parse errors include archive/member identity.
- Consumers: [placement](scene-rendering.md#start-placement-and-collision-surface), [body/camera](scene-rendering.md#initial-body-and-follow-camera),
  [terrain rendering](scene-rendering.md), [models](scene-models.md).
  Trees/Fence/Objects keep generic model data and the baked static branch
  (root bit 29). Archive/member selection stays here.
- HPC queries, POINTS/SGRID placement, POINTS barrier records, wheel/body motion,
  barriers, start/route/lap records, FREE props and moving horizon actors are
  integrated. Remaining work includes visible weather effects, spatial culling/order and complete race rules. See [scene lifecycle](scene-lifecycle.md). PITS supplies automatic
  local box-lane guidance on nine tracks ([pit lanes](pit-lanes.md)).

## Remaining track integration

Archive selectors `0044e717`/`0044e673`/`0044e5cf`; loading `00439820`;
weather `004512b0`; Single Race music `00426ec0`. Native selection exposes all
tracks for free driving; it does not grant earned profile progress.

| ID | Track | Prefix | Loading member | CD | Default weather / horizon |
| --- | --- | --- | --- | --- | --- |
| 0 | Donington GP | DONL | 3 | 03 | 2 / 1 |
| 1 | Silverstone | SILV | 6 | 04 | 5 / 4 |
| 2 | Thruxton | THRU | 8 | 05 | 2 / 2 |
| 3 | Brands Hatch | BRAN | 1 | 06 | 2 / 1 |
| 4 | Oulton Park | OULT | 5 | 03 | 1 / 0 |
| 5 | Donington Short | DONS | 9 | 04 | 3 / 3 |
| 6 | Croft | CROF | 2 | 05 | 2 / 2 |
| 7 | Knockhill | KNOC | 4 | 06 | 1 / 0 |
| 8 | Snetterton | SNET | 7 | 03 | 2 / 2 |
| 9 | Lavaland | LAND | 10 | 04 | forced 1 / 0 (`LAVA03_1`) |

`scene/catalog.hpp` shares archive family, loading member, light direction and
Single Race CD track across consumers.

| Track | KEEP | FREE | HOR |
| --- | --- | --- | --- |
| Silverstone | `fe37e86c` | `960a335c` | `5bc8cde6` |
| Thruxton | `e3b7a7c3` | `473c8fe8` | `46eab623` |
| Brands Hatch | `f7d9336b` | `ecc577c7` | `7f2628e3` |
| Oulton Park | `06c012d9` | `e6e258cb` | `716aabd8` |
| Donington Short | `f7422df7` | `f07db8b7` | `8771ca20` |
| Croft | `584752e4` | `e4399113` | `f242381f` |
| Knockhill | `8a446217` | `7e00bade` | `2715d7f9` |
| Snetterton | `ed1f9f52` | `4111866a` | `8cad122b` |
| Lavaland | `851f0e1a` | `dba52c51` | `25e04c2f` |

### Silverstone integration

323 terrain chunks, 15 ground textures, 484 sector centers, 241 barrier
segments, 26 cameras; 212 POINTS records, LOWPOINT bytes match (`1026dc23`).
Honda qualifying X/Z `(-25638838,9972735)`, grid slot 15
`(-16697834,23754680)` (16.16). Default weather 5, horizon `SI053D_1`/`SI05`;
explicit choices keep all five source horizons; cloudy uses variant 1 or 2 via
`0049ff60` and `0044cb80(2)`. The application owns and seeds that random state;
unported callers' complete random draw order remains unverified. CD02 → CD04 → CD02.
Full-lap/source comparisons, AI, race completion and remaining visual effects
are still separate work.

### Thruxton integration

464 terrain chunks, 13 ground textures, 567 sectors, 5154 HPC faces, 27 cameras;
highest detail 228 route records/284 barriers, LOWPOINT 278 barriers (detail
switch significant). Honda qualifying X/Z `(-28698342,-28680451)`, grid slot 15
`(-36466633,-24871634)`. Default weather 2 → fixed horizon 2 (`TH033D_1`), no RNG
draw. Light `(75,100,75)`, midpoint reciprocal 449 → `(33675,44900,33675)/65536`
(`0044eda9`). Scene entry starts CD05; return restores CD02. Original progression/Extras remain pending. Full-lap/source parity, rivals,
complete race progression and remaining visual effects retain their existing scope.

### Brands Hatch integration

- Ground 197 chunks, 13 textures, 210 sector centers; HPC 3003 faces; POINTS and
  LOWPOINT identical 148-record routes, 204 barriers; objects full/low/zero
  860/512/248 nodes; 19 cameras.
- Qualifying route index 130, XZ `(-20411506,-7437131)`, heading `-956`; grid
  slot 15 XZ `(-10585580,5233159)`, heading `3756`. Honda body Y `340260`
  (qualifying), `239414` (grid), from HPC contacts.
- Default weather 2, horizon 1 (`BH023D_1`), no draw. Light `(-60,100,-45)`,
  reciprocal 524 → `(-31440,52400,-23580)/65536` (`0044eda9`/`0049da90`).
  CD06 after loading, CD02 on return.

### Remaining tracks

- `0044e7bb` skips Lavaland's empty FREE trees member; shared loader keeps empty
  tree geometry. Lavaland's stored sector rows 2–9 differ from a reconstructed
  grid by one raw Z unit: `0047de00`/`0047db40` clip candidates against stored
  centers in file order, `0047dab0` selects a sector through separate grid
  arithmetic; no exact-center equality is imposed. Croft reduced route 271
  barriers vs 283 at detail 3. POINTS/grid/HPC placement serves all ten circuits.
- Explicit source RNG draws once for cloudy IDs 1/4/5/7, never for other
  weather/defaults or Lavaland. Menu weather labels keep the selected value
  (`0046cb54`); Lavaland's scene forces dry weather.
- Fixed light (`0044eda9`, `0049da90`, no float renormalization), all /65536:
  Oulton `(-27750,37000,-46250)`; Donington Short `(24300,48600,36450)`; Croft
  `(43100,43100,23705)`; Knockhill `(-87325,99800,54890)` (source squared sum
  wraps 32 bits; direction deliberately nonunit); Snetterton
  `(-30675,40900,40900)`. No track 9 light assignment in the original: native
  Lavaland uses the compiled initial `(1,1,1)` light, direction
  `(37834,37834,37834)/65536`, on every entry (no inherited mutated global).
- Qualifying caller mapping correction: rain/snow use group 2, storm group 1;
  `00444730` converts them to u16 offsets 4 and 2 (affects Donington Short rain
  default, Silverstone storm default). Menu table `004b9210`: weather 3=RAIN,
  4=SNOW, 5=STORM. Complete laps, original trace parity, animated weather and race rivals/results remain separate work.

## Track props and starting cameras

- FREE members 8–11: CONES, POSTS, SIGNS, FLAGS. `44d4b3/44d59e/44d668/44d72f`
  keep authored placement and hierarchy. Lavaland skips POSTS/SIGNS; detail 0
  skips FLAGS; Silverstone's empty FLAGS creates no flag actors. Controller
  limits 30 cones, 30 posts, 50 signs, 40 flags (loaded scenegraph geometry not
  discarded).
- Prop decoder mirrors `494a40` actor creation: an unresolved forward parent
  keeps the previously created actor, then overwrites its ID, matrix, mesh set
  (Brands Hatch FLAGS: roots without children, `44d72f` registers no flag
  controllers). Static roots use the exact bake; dynamic roots keep hierarchy.
  `44f0a3→45c4c0` resets registered cones upright at saved positions before
  presentation.
- Flags: `45b880` finds each flag's ID 1 animation target, draws its initial
  frame from the shared original RNG, countdown 3. `45b8c0` advances through the
  three mesh variants every fourth active simulation update; other nodes (poles)
  keep their initial mesh. Drawing advances no state; focus loss pauses. Flags
  continue through qualifying finish and grid restart without another init draw.
  Init/update callers emit no sounds.
- Original `421980→45b900` gathers prop candidates from the vehicle sector and up
  to three neighbors; posts → `45bc50`, cones → `45c6c0`, signs → `45d4a0`;
  `421aa0` advances existing states via `458ac0/45a460/45b010` before new
  contacts. Cones move/tumble, posts bend/return, signs tip or break into
  `417cd0` fragments above the speed threshold. Audio: CONE samples 45/65, SIGN
  sample 66. Only these registered actor kinds collide, not every decorative
  object ([prop contacts](prop-contacts.md)).
- Qualifying initializes the camera with the track-specific `45eb60` angle table
  before the first State 4 follow step; grid entry uses the stored SGRID heading
  ([camera evidence](driving-audio.md#moving-follow-camera-and-local-driving-audio)).
- No single-player far-plane exception found; detail values stay 150/200/250.

## Moving horizon actors

- Loaded from `data/<prefix>hor.bfp`, separate from FREE. Member 0: moving
  aircraft/balloons; member 1: helicopter where enabled; later members: weather
  sky. Controllers: `src/scene/horizon_actors.cpp`.
- Mega builds active groups as `track-N/moving.glb` and `track-N/rotorcraft.glb`
  (eleven paths, six unique payloads) via `mega_aircraft.py`: Donington's two
  aircraft, Thruxton's two plane families, the helicopter, three Croft
  balloon/airship forms. Moving-root IDs, helicopter identity parent, node
  matrices, mesh slots, routes, four rotor/propeller phases kept. Lavaland skips
  both groups. 32 distinct original materials; AI redraws keep livery, symbols,
  UVs, colors; missing replacements embed native-size canonical pixels. The PSP
  side billboard gets an opaque 512×256 WebP80 `MegaPSPSkin` with explicit 3D UV
  regions, appended to the material table before geometry generation (original
  slots incl. PSP1 keep indices).
- Donington GP/Short share member 0 (CRC32 `27aa6ea9`): `AIRMARK_1` 43 v/31 t
  aircraft; `PSP1_1` 4 v/2 t billboard with a jet texture (not airships).
  Helicopter member shared between its tracks (CRC32 `cc604573`). Thruxton: three
  planes 74/74/64 triangles + four alternating propeller phases, six textures
  32–128 px.
- C05b: `43cf60/43d330/43dfc0`, `43e2a0/43e630/43f0c0`; selection by resolved
  weather, independent of detail:

| Tracks | Member 0 | Member 1 |
| --- | --- | --- |
| Donington GP / Short | 3D aircraft and jet billboard, every weather | None |
| Silverstone | Balloon and animated plane, weather 1/2 | Helicopter, weather 1/2 |
| Thruxton | Three animated planes, weather 1/2 | None |
| Brands Hatch / Oulton / Knockhill | None | Helicopter, weather 1/2 |
| Croft | Two hot-air balloons and Audi car-shaped airship, weather 1/2 | None |
| Snetterton | One balloon, weather 1/2 | Helicopter, weather 1/2 |
| Lavaland | None | None |

- Route tables: fixed-point positions, speeds, countdowns, turns. Oriented
  member 0 actors repeat; balloon controllers retire at route end leaving their
  last transform. Helicopter: takeoff, quarter-circle turns, straight flight,
  deceleration, landing, 100-tick waits. Four rotor/propeller nodes switch each
  active update. `47b150` float-input rotation keeps wider intermediates until
  the final integer rounding; other motion uses fixed arithmetic.
- `421aa0→421980→446a80` advances both groups after flags at 25 Hz. Drawing uses
  render-only intermediate node transforms between fixed updates (linear
  translation, rotations keep basis scale/orientation); camera 0 follows the same
  intermediate root. Rotor phases, visibility, controller metadata keep source
  values. Route repeats cut to the authored start. Focus loss pauses; loading
  clears presentation history; qualifying→grid preserves it. No RNG. `4608b0`
  publishes the fixed transforms; original allocation order draws both HOR
  groups after the selected car.
- No audio: the six actor callers and `43fd70/43fe00` have no audio calls;
  `446a80`'s event 19 is a separate `52bc5c` branch.
- HOR palette: reduced BMP palettes have 255 entries but some pixels use index
  255; `483261` leaves that slot undefined (16-bit path). The loader normalizes
  using the complete original `PSP1` palette from `donlhor.bfp` member 0 only
  when every stored prefix byte matches, copying the missing entry into an owned
  decode buffer (reference bytes unchanged). 1020-byte prefix CRC32 `057d15c3`;
  full 1024-byte bank `4613c335` (omitted entry = white). Deterministic
  asset-backed correction, not a reproduction of an uninitialized stack value.
- Mega builder/runtime derive HOR override CRC32 keys from the completed BMP
  bytes (corrected file size, pixel offset, palette count). Unmapped reference
  bytes intact. PNG references and the name/dimension/CRC32/track inventory live
  only under ignored `build/mega-aircraft-sources/`; the builder checks all
  canonical HOR archive CRC32 values first.
