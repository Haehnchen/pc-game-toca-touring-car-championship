# Playable Single Race

The Single Race menu loads the original 16 driver identities and 15 physical
opponents. Qualifying classification supplies grid ranks; NoQual consumes the
original generated qualifying order and places the player last. Laps, track,
car, difficulty and resolved weather feed the same runtime. Qualifying and Time
Trial keep their own flows. Rivals cross real POINTS/checkpoint planes; the HUD
shows the actual running order.

Finish: measured finishers are kept, unfinished totals derive from actual
progress (see Single Race lap and finish records). Continue opens RACE AGAIN (YES
selected): YES reloads the session setup; NO releases scene/audio and restores
main menu/CD02. Local two-player races open RACE AGAIN directly over state10
([split screen](split-screen.md)). [Championship](championship.md) is separate.
Original-recording comparison and the remaining source branches stay explicit
follow-ups in G05/G06.

## Generated NoQual startup

Ordinary one-player Single Race, normal car, no effect codes, uses the original
two-call initializer: loading and first state9 entry each classify the grid
(`447980→460170`). The live fleet takes the second canonical driver table/grid
and continues its final RNG without another draw. Local driver and opponents
share the initialized POINTS route/traffic cache writer. Previous body modes,
release state and the cold pair reset enter the native fleet through the typed
result, never a raw snapshot.

- Pre-deadline native preview stays grounded. At the first regular scheduled
  update, before input/progression/AI, the fleet adopts generated body poses,
  contact points, support samples, cached normal and material classes. Controls,
  prepared drivers, RNG and inherited cache tails survive. The ordinary physical
  and ground pass runs once before the next presentation capture; no sentinel
  pose is captured, no extra tick inserted.
- Original state9 calls `448790→421aa0` before the secondary render dispatcher
  `448a10→41f520`. Startup64 retains `0x83000000` (-32000 Q16) for actors 0–14
  through the first consumed boundary; the first ground pass corrects all 16
  heights. The typed handoff reproduces that input boundary; the grounded
  preview is a native scheduling adaptation.
- Each new native scene starts this initializer from the current process RNG
  with fresh Single Race points. Ordinary one-player NoQual RACE AGAIN keeps the
  scene and runs one warm grid writer from existing resolved drivers, points,
  ranks and pole context. Qualifying, two-player loading, Championship, Linkup
  and effect/bonus-car setup keep their own paths.
- Actor first-read classification is complete for the process-pristine context
  below. Original warm-process score/entry lifetime, world/resource histories,
  callback RNG ordering and full autonomous race parity remain open.

## Generated qualifying startup

Ordinary one-player QualOn uses two `443f60` calls: loading and the first
State4 wrapper. `initialize_qualifying_start` shares the cold Motion/Controls
writers with NoQual, places all sixteen bodies from rotated POINTS slots and
removes the twelve actors outside the local actor's modulo-four group. Removal
splices the traffic ring and updates running ranks. It neither classifies a
grid nor awards pole points or consumes classification RNG.

All controllers initially belong to AI; the local player's saved modes survive
for the actual start-line handback. Initial velocity derives from the generated
body basis at `0xcccc`, with wheel speeds `0x33330`. Live play keeps grounded
previews and installs the generated fleet poses, velocities and controls at the
first scheduled update. The four active actors share the race Motion/contact/
ground dispatcher; the twelve removed records remain allocated and frozen.

The three qualifying opponents follow their own rotated POINTS cursors and
traffic ring. Qualifying speed/traffic rules retain permanent player identity
after AI handoff. NPC laps never invoke the player's release or finish; global
states7/8 stop new NPC laps while bodies and route cursors continue. Inactive
actors retain the source distance sentinel `-1`. Scene visibility/model/effect
gates and spatial engine/wheel audio consume the same active fleet.

The earned grid replaces qualifying roles with sixteen ordinary race actors.
Warm RACE AGAIN rebuilds the four-actor approach from the retained setup without
classification draws or an extra simulation step, keeping scene resources and
the process world. Ordinary one-player normal-car setup is covered; the full
original warm-process byte lifetime and continuous QPC join remain separate.

The loader binds each actor to its own SGRID strip; the cold camera heading uses
the local actor's retained binding. The first State4
renderer later replaces it with the track heading. First State4 resets timer2
through `44ffe1→421290`, retaining the other creator-clock origins; NoQual also
resets timer3. The source approach
owner queues State6 at the generated start-line event and commits it only on
the next primary, resetting timer0 before the next caller-clock sample.

The separate `--qualifying-startup` capture closes at the first `421aa0` PRE,
with zero physical updates. The native comparator receives authored setup,
initial RNG/points/name and observed creation/caller clocks/divisor; actor
snapshots remain expectations. This proves the initialization boundary, not a
continuous autonomous qualifying lap or QPC/physics join.

The original cold-start capture matches all 7,405 named values across both
initializer returns and first PRE. Evidence:
`build/test-artifacts/qualifying-startup/reference-trace-qzg00bon/` and `compare/`.
CRC32: trace `11ad2c4d`, prefix `dfbf54e5`, 98-byte input `a79cd0ff`.
Focused CPU/sanitizer cases, the existing NoQual96 regression and the native
three-lap qualifying/results/earned-grid/warm-qualifying flow cover integration.
Its `rival_actors` diagnostic column records permanent identity, physical mode,
pose and route/lap progress, proving three moving NPCs and twelve frozen records.

## Pristine physical context

PE `.data`: virtual size `0x188e48`, raw size `0xfe00`, base `0x4ad000`;
file-backed bytes end at `0x4bce00`. Dynamics array `0x4c5da0..0x4c9120` and
body array `0x4cd760..0x4d2b20` lie in process zero-fill. This is cold-process
provenance, not a reset on every race entry; typed native constructors need no
executable or captured state for it.

| Initial group | First consumer / ownership |
| --- | --- |
| Velocity, wheel speed/slip/rumble, visual and rollover defaults | Explicit `40a770`/`412040` writers and shared native construction. |
| Previous position / full-body previous RPM and engine drive | Written before first relevant use by full/distant movement or the full engine pass. Distant previous-RPM/drive slots keep cold zero. |
| Wheel axes, patch offsets/latch, righting, raw axes | Process-pristine zero. Full wheel forces replace axes; the zero patch latch prevents offset reads until the first ground writer publishes them. Righting and distant caches keep the cold context. |
| Upper support flags / inactive index tail | Only the live count/index prefix contributes forces. Initial count 4 uses indices 0–3; ground rebuilds flags/new live indices before later use. Raw byte history is unclaimed, not a startup dependency. |
| Tire-effect gate (`dynamics+2c0`) | Cold zero; only full `40b360` computes it. Distant updates retain it; `432210` consumes it for surface effects. Fresh distant cars emit no ordinary tire particles/spray until a full update has computed it. |

World, audio-slot, model/visibility and unclaimed warm actor storage stay
separate; no blanket zero reset. The retained cold comparison uses the same
Motion/Controls owners through an explicit cold restore: consumed pristine
kinematics, wheel/effect state and raw controls keep constructor values; the grid
supplies authored body, calibration and actor/car/controller identity. Both
initial stream aliases are guarded before driver or physical restoration.
[Cold actor/world boundary](single-race-fleet-reference.md#generated-cold-contact-damage-and-prop-world).

## Warm world reset and resource lifetime

Original RACE AGAIN (`441720` action 10 → `44a370` → `449f50`) resets session
clocks and re-enters the grid without `44f0a3` resource loading; ordinary NoQual
calls `447980→460170` once. Native: scene kept, one 15-draw grid classification,
next 40 ms update scheduled. Simulation step numbers stay monotonic; session
timing restarts at zero.

- `initialize_warm_grid` is shared by native play and retained comparison:
  resolved drivers, bindings, scores and live RNG feed the same
  placement/control/route writers as startup, once. The warm merge retains
  unowned raw axes, filtered throttle/engine drive, previous RPM/position and
  saved controller modes; repaired actors, new qualifying records and reset
  filters come from those writers.
- QualOn: `449f50` selects state4/stage1 and calls `443f60`; first state4 entry
  calls it again (rollout/repair writers, no classification). Native restart
  reconstructs the AI-owned rolling approach at source initial speed `0xcccc`,
  clears earned race results/rivals, retains the original qualifying selection
  even after qualifying led into a race; no classification/RNG draws.
- Local two-player forces NoQual: both human controllers, held-neutral cars,
  pit/finish/boost records, sessions, cameras and contact registration reset
  together, no classification.

| State | Warm grid reset | Full scene reload |
| --- | --- | --- |
| Shared damage cycle and five selection triples | Retain | Retain as typed process state |
| Actor damage, detached allocations, active effect pools | Repair/free/clear | Fresh scene-owned pools |
| Prop transforms and pair contacts | Restore authored poses/clear contacts | New scene initialization |
| Fragment random tables/cursor | Retain; no 111-draw regeneration | Rebuild tables |
| Exhaust cooldown/phase | Retain; clear active bursts | Initialize 900/2 |
| Skid visible counts/anchors | Clear; retain ring storage/cursor | Retain process ring storage/cursor |
| Roughness cursor | Retain | Retain |

- `44ffe1→4469a0` resets live pools; `45c4c0` restores cones, posts, signs.
  True resource initialization (`4467a0→431820/4209d0`) owns fragment-table
  generation and exhaust cadence initialization.
- `419ad0` alone advances the process damage cycle/scratch. Front/rear impacts
  replace only their first three triples; untouched tails survive. Typed
  `DamagePersistentState` carries only this scratch across scene destruction
  (no detached callbacks, allocations or render ownership).
- Old driving queues/voices stop at restart; the owned finish loop fades. Native
  diagnostics stay cumulative within the loaded scene.
- Warm path covers ordinary Single Race, normal cars, no codes, one or two local
  players. Time Trial, Championship, Linkup, bonus/code profiles keep their paths.
- Native two-player placement keeps authored slots 0/1; the source's
  cached-clock-derived slot permutation remains unproved.
- Qualifying writer-call equivalence, unclaimed warm actor/detached record tails,
  visibility/render retirement and complete source race parity remain open.

## Rendering and audio

- `SceneView` loads each CAR archive once, shares textures between team actors,
  registers each rival as a dynamic scene root. Body, steering, wheel phase,
  damage variants and projected shadow feed the terrain/visibility pass.
- Three model members follow `4271d0`/`452376` distance/detail limits: detail 0/1
  uses 0 near + 5 medium cars, detail 2 uses 1/5, detail 3 uses 4/5. Normal
  one-player NoQual View2 uses the local body observer and canonical force1;
  Mega/other native profiles retain their observer/hold policy. Selection and
  effects advance on active simulation ticks.
- Tyre particles, skid marks, exhaust consume the active actor records. Removed
  wrecks release models and motor voices. Original frame-matched fleet/model
  comparisons remain open.
- One `FleetSceneContext` snapshot supplies physical registrations, current sector
  visibility, retained model LOD and separate contact/wheel/exhaust eligibility.
  Motion, Rivals and Linkup adopt it together after ordinary emission, before
  tyre-mark/weather presentation. Single-player Full rivals retain manual car
  effect eligibility outside geometry PVS; the Full/Distant and contact LOD gates
  remain distinct. Presentation cannot migrate registrations or advance LOD
  holds. [Scene/effect contract](scene-rendering.md#draw-and-effect-consumers).
- `4259f0/425c10/425d20` retain the two loudest opponent motor voices: `414d10`
  external car sample, `4141f0` RPM-rate curve, actual camera listener, `424c10`
  distance attenuation. Distance 0.8 truncated before View2 category 2 gain
  `0x5999`; volume ties keep actor order. Voices persist while selected, release
  on distance, pause, finish, scene exit. Rival wall/car contacts feed the
  spatial impact consumer. Finish audio: see Audio under Single Race lap and
  finish records.

## Single Race traffic decisions

`403dd0` runs separate ascending actor passes: observe all POINTS targets, decide
passing for every AI actor (`4036f0`), then closing brakes (`403aa0`). Physical
movement follows: distant bodies integrate ascending; full bodies (including the
local car) descending; then car/prop contacts; the shared ground pass ascending.
Not a complete original fleet/substep recording.

### Ordering and gaps

- `Rivals::set_route_distances` consumes the same wrapped route-distance records
  used for position ordering. Ahead/behind identities come from the circular
  spatial ring, independent of classified finish order.
- `403dd0` forward gap = ahead actor's route distance minus follower's, wrapped
  by total route length. If the ahead actor has a player identity and the gap is
  below 100, `404b07` uses horizontal distance from the last active actor visited
  by the ascending route pass (`404811`), reusing that saved pose. Native rivals
  keep this source quirk.
- Player identity (`game+80`) survives AI handoff; it is distinct from current
  human control (`game+8`).

### Passing and queue

- `4036f0` retains selected passing mode and target identity (`game+2c/+98`)
  across updates. Cancels when the actor stops being adjacent, becomes blocked,
  moves behind, no longer warrants passing, or the corridor clears. An already
  finished actor above 60 cancels an active maneuver.
- `403490` evaluates both front-corner rays against the ahead actor's POINTS
  Best/Over targets and selected body corners; cached ray flags survive outside
  the stopping gap. `4032c0` measures avoidance angles from the follower's two
  front corners. `403690` retains an existing left/right side.
- A slow actor inside 200 units starts a blocked queue when the follower exceeds
  50 (`game+18/+99`). A follower of a blocked actor inherits the identity of the
  actual blocker and, once close, steers around that blocker's body.
- `4051e0` applies the source 60 speed cap after route-speed variation. `403aa0`
  adds queue braking near the player, mode-specific closing brakes and the
  original high-speed short-gap alternate-line choice. `404ea0` large-steering
  brake release stays downstream.
- `414e20` counts existing full bodies that cannot be demoted. `414ef0` ordinary
  `game+10==0` branch promotes a distant behind neighbor first if its forward gap
  is smaller than the local actor's; remaining budget walks the circular ahead
  chain. Total stays four full bodies (player and unsupported full bodies count).
  Both spatial-direction branches share the
  [recording-checked body selector](single-race-fleet-reference.md#body-selection-boundary--2026-10-17).

### Source bounds

- Modes 0/3/6 are HEAD_LEFT/HEAD_RIGHT/HEAD_FOLLOW in `452630`'s diagnostic
  switch. `405530` consumes an endpoint target for 0/3 and half the angle to the
  ahead body for 6. But `401370` initializes mode 1; recovered `4036f0` returns
  only 1/2/4/5; `403aa0` can select 2; other recovered writes reset to 1. No
  recovered Single Race selector activates 0/3/6; no invented triggers. Their
  braking cases stay in the recovered brake dispatcher.
- Ordinary nonplayer AI does not start a pit visit: `403dd0` requires human
  control (`game+8!=0`) before the PITS entry crossing calls `402be0` and sets
  the pit flag (`game+30`); `402be0` clears human control for every rival. An
  active pit interval has its own continuation/exit path. No autonomous rival pit
  planner is missing.
- Rollover requests, player righting, body modes 2/3/4: see Single Race rollover
  and removal. `415240` removes a frozen wreck when its registered sector leaves
  visibility (no timer). Shared prop contacts run for the full fleet.
- The host lifecycle of `game+10`, special route-layout corridor choices,
  complete human wrong-way flags, damage-driven pedal suppression and complete
  process RNG chronology remain open.

Cross-track checks: see Single Race fleet across tracks. Unit coverage:
`test_ai_driver.cpp` (authored POINTS, real body corners).

## Single Race lap and finish records

`RaceProgress` consumes the 16 actual body positions before physics, following
`403dd0`; it keeps the local `Session` and owns 15 rival sessions. Every actor
uses the same directed POINTS planes, six ordered checkpoints and the original
active millisecond clock. Grid release starts lap and total timers together; the
initial grid-to-line passage awards no lap. Race lengths finish after that many
complete ordered circuits; qualifying keeps three measured laps, Time Trial is
unlimited.

### Running order and classification

- `403dd0` supplies cumulative route distance minus the absolute horizontal
  projection to the current POINTS Best target. `402e90` uses it, lap and
  initial-start flags to exchange neighboring positions. Native keeps the source
  ahead/behind ring, spatial ranks and race ranks initialized from the classified
  grid; the ascending actor pass applies one `402e90` exchange per actor with the
  same flags and half-route displacement guard. Qualifying times are not consumed
  during racing; equal positions keep this neighboring chronology (no global sort).
- Full/distant body selection (`414ef0`) follows game fields `+96/+97`
  (`cc5ae/cc5af`), the spatial ahead/behind ring, not race position `cc5a7`.
  `spatial_order()` exposes the circular order (preceding index ahead, next
  behind, with wrap). Finished cars stay in the ring while coasting. Ring and
  route distances continue through states 10/13; result rows, finish order and
  completed-lap records stay fixed.
- `421390` returns immediately in states 10/13, before lap increment or the write
  to game`+54` (`param_1[0x15]`): a driver unfinished at local classification stays
  unfinished after further start-line passages. `finished(actor)` stays valid for
  the AI's live finish/coasting branch; no post-classification finish transition.
- `421390` records finish order and total centiseconds at each final crossing. A
  local finish hands the car to AI and enters state 10. Then `4220c0` appends
  unfinished actors in running order and extrapolates displayed totals from the
  local best whole seconds, actual remaining route fraction and lap deficit
  (classified totals, not invented laps or generated qualifying times).
  `RaceEntry.finished` distinguishes real crossings from extrapolation. Later
  movement cannot change classified records.
- `4220c0` repairs totals preceding the classified winner from nearest valid
  neighbors, or a 30 s step when only one exists, then sorts totals along the
  fixed actor classification. Championship points/standings (`422720`/`422880`)
  are outside ordinary Single Race mode 0.

### Finish states, HUD and result view

- Local final crossing: displayed lap restored to the limit, finish-order record
  set, `41a820(0,player)` hides gauges (state10 hands control to AI).
  `41eea0(case10)` selects GSTRINGS14/15/16 for the first three places, formats
  line 17 for later places. Caption during all of state10, initially with the
  driving camera; at timer0 >= 2 s `448e90` selects the authored finish camera.
- Single-player: `448a20` enters state13 (all 16 results) at timer0 >= 5 s
  (confirmation does not shorten state10). The outer `44a370` commit resets
  timer0 to its previous cached clock before `44a1d0` samples again; `44a020`
  uses that new origin. Table: GSTRINGS56, original car icons, fonts 23/24,
  winner total and positive gaps for others; all 16 rows fit the authored 640
  canvas, uniformly centered at 4:3, 16:9, 16:10. Confirmation or timer0 >= 40 s
  sets `4edab4`. After rendering, `44a090` calls `44a0c0`: requests overlay 3 via
  `441650(3,0,0)`, resets timer0. State13 stays beneath RACE AGAIN?: YES
  preselected, Escape disabled, YES restarts the original qualifying/grid choice,
  NO returns to Main. State15 is the Championship menu, not this continuation.
- Local multiplayer: `448a20` requires timer0 strictly above five quantized
  seconds after every human finished before `441650(3)` requests RACE AGAIN
  directly; state10 stays beneath; no state13 table. At native 40 ms deadlines
  the first eligible update is 5080 ms after the final finisher. Totals stay
  fixed until restart.
- `45ed30(case13)`: 60-call outer selection hold around `426b10`'s 30-call
  cache. Its `26830(...,1)` close branch keeps the look-at basis computed from
  the authored camera, moves the origin 15 units from the local body in that
  direction, clamps at least 3 units above the `42e9f0` HPC height. Renderer,
  visibility observer and listener use this close origin. Native selection
  updates at active simulation deadlines; original presentation-call camera
  cadence is not claimed byte-identical across FPS settings.
- Racing HUD shows the real 1..16 position: `41de90` draws POS artwork (`4ad9e8`)
  at 437,5 and secondary-atlas large rank digits right-aligned at 620,5.
  Qualifying and Time Trial omit it.

### Checkpoint display

`4212d0→41a840` keeps a personal three-split reference per player. First displayed
lap records elapsed checkpoint times; later laps show signed differences; a
faster complete lap replaces all three references. Leader-gap text is a separate
source field. `41e3d0` places SPLIT at 208,60 and the time at 286,60 (centered
together at wider aspects). A leading minus selects the alternate numeral row for
the whole string. Original and Mega artwork include the label in source rectangle
`(1,132,75,11)`. The label stays visible while its number flashes for the first
48 updates; the number then stays until `41ab70` expires it past 200 updates.
Pause freezes the lifetime; finishing hides the display. Rival finish evidence and
remaining handoff/display limits:
[fleet timing](single-race-fleet-reference.md#rival-finish-and-checkpoint-display--2026-10-17).

### Audio

State10 entry calls `425950`, `426400`, `47a305`, then `4058e0`: stop horns,
sample voices and CD, then start owned sample 83 `LOOP2.WAV`, volume 127, pan 64,
infinite. Championship-only `44cb90(9)`/`(10)` do not apply. State13 has no speech
and no fade; the loop continues at original gain beneath RACE AGAIN (`441680`
stops streamed/driving handles, `426400` does not own the separate `4bd94c`
finish-loop handle). RACE AGAIN fades it via `405840(2)` (warm state9); scene exit
releases it. Native focus loss mutes the loop independently of the paused
simulation.

Dispatch-table bounds: base `4ed9b8` + 13×4 selects `44a020`; base `4eda10` +
13×4 selects render callback `44a090`. Nearby `448f00` is state12 (Championship
table); its `44cb90(11)` CTIN speech and `405860` fade must not leak into
ordinary Single Race.

## Single Race rival bodies

`460170` preserves the 16 AIBIN driver identities and the qualifying actor order.
NoQual calls `444270(0)` with the local 60-minute record, then swaps that actor
into slot 15. `Rivals` uses those identities and `401190` SGRID placements; the
selected car's `Motion` stays local. Ford's original pair 8/9 also keeps the
selected bonus-car replacement from `44df6e`. Full-body rivals share HPC,
barriers, car contacts, recovery, the rough-surface sequence and impact effects
with the local car (see the sections of those names).

### Body selection and movement

- `414e20/414ef0 → 415090` selects the full wheel/body path or the original
  `448830` distant body (stable supported actors). The player keeps a full body;
  neighbors fill the source budget of four. The budget counts existing
  unsupported full bodies before promoting distant actors and follows the
  circular spatial neighbor ring, not finished classification order.
- Distant update: `4488b0` gas/brake/drag and gear acceleration, `444ab0`
  direction blending/displacement, `444ee0` HPC height/normal/basis; never moves
  a body to a waypoint. Reverse keeps signed body speed; controls and the
  game-speed record use its magnitude.
- `42e6f0`/`42e9f0` ignore a failed `42eed0` triangle search and use the caller's
  retained valid face. Motion follows this inside the HPC root rectangle: eight
  per-wheel caches for full bodies, the actor's first face cache for the distant
  center query; caches are actor-owned, use the original plane arithmetic, and
  never supply a plane without a valid cache or outside the bounded root. Strict
  placement/parser queries still require a containing face. Known case: seed 1
  X 4523287 / Z -10249364 lies inside Donington's root rectangle
  X[-46706944,53956352), Z[-18810098,31521550); the containing-face search failed
  but the original actor retained its previous face.

### AI inputs

- `403490/4036f0/403aa0` consume the preceding body's corners, speed and POINTS
  normal. Main/alternate corridors select ordinary mode 1/2 with corner
  avoidance 4/5 and closing-distance braking (see Single Race traffic decisions).
- `404ea0`: random prestart gas and per-driver reaction brake. Green consumes each
  AIBIN reaction bound through the shared CRT stream. `4051e0` uses AIBIN signed
  fixed variations at driver+36/+32 for distant main/passing speeds; POINTS+56
  supplies the passing speed. Finished actors coast above that adjusted speed
  with the original 160/170 route caps. Reverse recovery keeps its selected full
  steering axis as `405530` requires.

### Actor storage

Traffic lookups keep all 16 allocated actor records (original `4036f0` indexes
flat gameplay/body arrays). The spatial ring and ascending decision/physical
passes contain active actors. Removal freezes an actor's updates while keeping
its last pose, speed and route state for retained blocked-leader/passing
references. The active ring and full-body dispatch list use 16-slot local ID
buffers with populated spans. An aborted preparation skips the entire full-body
list but keeps completed distant updates.

### Fixtures and limits

- CPU fixture: local car parked, all 15 rivals driven through the application's
  `RaceProgress`/`Rivals` calls; three Donington laps complete by tick 10491
  (419.64 s) with real HPC/barriers/traffic/contacts, no motion boundary.
  Nothing is synthesized. Seeds 2, 7, 896116572 complete one lap and continue
  6500 ticks. Seed 896116572 comes from a failed native acceptance's
  process-start epoch; native scene/audio random draws still differ from this
  CPU fixture's stream.
- Complete original process RNG/substep parity and the host lifecycle of `414ef0`'s alternate
  direction flag remain open. The shared selector implements both ring-walk
  branches and consumes retained `body+b4` before stabilization; ordinary native
  races keep direction 0 ([body-selection comparison](single-race-fleet-reference.md#body-selection-boundary--2026-10-17)).
  Seed/weather coverage: see Single Race fleet across tracks.

### Retired actor traffic lookups

Native failure with initial CRT seed `896387043` (reproduced by fixing the
process-start `time()` sample to `1792774086`): retired rival 14 had left the
active ring; the retained queue 1→15→14 failed because its allocated body was
omitted from the caller's lookup view. The complete 16-slot view now feeds both
ascending traffic passes; neighbor construction and update eligibility keep using
the active ring. The fix draws no extra RNG; the clock shim affects only the
initial seed.

## Single Race car contacts

`CarContacts::resolve` runs after local/rival body updates, before shared prop
contacts and ground correction. `42a6f0` broad phase, both `42ada0` corner
transforms, `42ba80` penetration ray, `42a820` axial fallback, `42b570`
correction of both actors and already-contacted neighbors. Candidate groups and
pair order: see Registered sectors for car contacts. Contact pairs/strengths feed
host audio; barrier/post adapters keep their own normalization.

### Contact lists

- Each tick starts fresh symmetric successful-contact lists (`428820`, actor
  `+529/+52a`). Only confirmed contacts enter; two completely stationary actors
  are excluded.
- Persistent pair flags remember new vs continued contact; only an attempted
  overlap miss clears them (omitted pair ≠ separation).
- Native storage: 16 fixed rows of 15 actor IDs; validated actor bounds and
  symmetric duplicate suppression bound each row; only the populated prefix is
  searched/shifted.
- `42bc70` excludes removed mode 4 bodies; frozen mode 2 cars can still collide.
  A hit returns a frozen wreck to mode 3 for another ground settlement.

### Correction

- `42ada0` runs both corner probes. Each accepted displacement immediately moves
  the actor and its already-contacted neighbors once (`42b570`), before the
  opposite probe or next pair. Direct list walk, not recursive propagation;
  shared neighbors can receive both sides' corrections.
- A body held by a static barrier leaves the full correction to its partner.
  Otherwise each local penetration component is multiplied first, halved second,
  both signs use the original rounded transform. Position and all eight retained
  world corners translate together; previous integration positions stay intact.
- Contact arms transform before signed division, using `428790`'s clamps.
  `42a820` fallback probes use the original one-third half-length offsets,
  restore only the penetrating actor's previous position, retain the source
  endpoint arm. Corrections also update the rendered body position.

### Impact

- `428b50` exchanges normal velocity components and queues delayed linear/local
  angular impulses for `40caf0`, with the original center-of-mass offset and
  moment clamps. First strong-impact moment uses the center-of-mass offset; its
  old accumulated moment controls the angular-direction guard; result clamps to
  ±2 per component.
- The second actor gets the strong branch only when the first actor's retained,
  clamped moment length exceeds `0x5998`; otherwise its ordinary contact-arm
  moment applies, even at high relative speed. Sound strength uses full relative
  velocity length, including glancing motion.
- New contacts call `419ad0` per full-body actor in encountered pair order;
  continued contacts use the retained scrape handle and spark path (see Single
  Race car impact effects).

### Limits

Native startup initializes the uninitialized neighbor shorts to `ffff`; supported
single-player physical roots retain view mask 1 independently of camera-hidden
model children. The CPU fleet uses an all-visible, highest-detail camera policy.
These checks and the fleet acceptance (see Single Race fleet across tracks) give
bounded physical coverage; there is no matched original collision recording
establishing whole-scene bit-for-bit parity.

## Registered sectors for car contacts

`42bc70` builds car-contact sector lists from the car root object's registered
`+120` sector and `+122/+124/+126` neighbor shorts, not from the current physical
position. Registration changes later through `47f500 → 47e3c0`; the collision
pass consumes the caller's retained registration snapshot.

### Registration arithmetic

- `Visibility` and the CPU-only `initial_root_registration` /
  `update_root_registration` share it. `47de00` initially links the root using its
  local translation truncated to Q16. `47e3c0` uses ROUND of world translation ×
  65536 for sector lookup; its neighbor tests truncate world X/Z × **negative**
  65536 and compare strict 4.5-unit margins against the sector center and
  half-size (equality excluded). A fractional Q16 boundary can use a rounded root
  sector and a truncated neighbor comparison in the same update.
- Neighbors are written in X, Z, diagonal order, remaining slots `ffff`;
  recomputed even when the root stays in its sector. Edge arithmetic is not
  clamped to a row or the grid: raw non-`ffff` shorts remain sector-list keys.
  Hidden roots keep registration. Leaving the grid unlinks the root and clears its
  neighbors; `47f500` leaves the outside root unregistered afterwards.
- `47f2c0` initializes `+120` to `ffff` but not the three neighbor shorts;
  `47de00` writes only `+120`. Native startup uses explicit `ffff` neighbors until
  the first ordinary registration update (no undefined allocation contents).

### Physical root

Registered root = `DAT_004c3968[actor]`. `44d91f` attaches three LOD model roots
below that empty parent; `4608b0` publishes the parent's translation directly from
body `+bc/+c0/+c4` as float / 65536, no model offset. `427170` hides the model
children for cockpit/bonnet cameras, `427010/090/100` select LOD; both keep the
parent's `+140` view mask, so camera-hidden meshes keep registration. Single-player
race: active parent mask 1; removal clears it.

`4608b0` publishes poses after physics/contact/ground and the shared `415e10`
detached-part advance; `47f440 → 47f500 → 47e3c0` update registrations for the next
contact pass. Native registration initializes from the first published pose per
actor, resets at a new grid, and retains each snapshot until the next ordinary
registration phase (an unrelated initial origin would leave an outside root
permanently unregistered). The CPU fleet fixture uses the same conversion and
phase with an all-sectors-visible, detail 0 camera policy. Registration helpers
use no graphics API, RNG or simulation time.

### Candidate groups and pairs

- `contact_candidate_groups` ports `42bc70`: 64 groups × 16 actor occurrences.
  Actor slots ascending; append the actor's own sector, then up to three neighbors
  until the first `ffff`. Mode 4 excluded; stationary mode 2 wrecks remain.
  Groups keep first-encounter order, actors append order. An own `ffff` sector is
  itself a grouping key (no fabricated all-pairs fallback).
- `428820` visits groups in encounter order and each group's `i,j>i` pairs. Only a
  confirmed overlap adds both actors to the tick's successful-contact lists
  (`car+529/+52a`). A miss can be retried via another shared sector after other
  contacts moved the bodies; global pair deduplication would lose that. `428aa0`
  clears persistent pair-contact/audio state only for an attempted pair that
  fails overlap; omitted pairs, removed actors and the stationary-pair skip do
  not clear it. Impulses, damage and sound stay in the contact resolver.

## Single Race car impact effects

`428b50` drives the existing mesh damage, detached-part, fragment, spark and
impact-audio consumers for both cars. No health, engine degradation, tire wear
or repair model is added or inferred. Candidate order and pair suppression: see
Registered sectors for car contacts.

### New contact and damage

- `428b50` applies both cars' pending acceleration and moment before effects. On
  a pair's first successful contact it marks both directions touching, clears
  their scrape-handle mapping, then calls `419ad0` for the first car, then the
  second, only where body mode is zero/full (distant and crash bodies collide
  but get no damage call). Input: each car's accumulated pending acceleration
  (incl. earlier pairs in the same update) and its clamped local contact arm.
- Each car keeps its own 17 mesh variants and broken features. `DamageContext`
  shares the `4c2f8c` selection cycle, `4c26b8` scratch selection and ten
  detached records across the scene. An underside hit retains the preceding
  call's selected node/slot even if from another car. A flying/recent part from
  one actor can block another actor's detachment. Each detached record keeps
  actor and car mesh identity. Initial detachment tests the owner's retained
  physical-root sector and model detail, not visibility recomputed from the
  updated body position.
- Host advances the one pool at `415e10`; Motion updates and ground-failure
  rollback never advance/restore it. Grid debris clear preserves the shared
  selection cycle/scratch.
- Sound point = first car's origin + transformed contact arm (not midpoint). If
  the first damage kind is special (2–4) the second becomes 1; otherwise a
  special second kind changes the first to 1. Equal kinds emit once at max
  strength; unequal nonzero kinds emit first then second. Human flag is true if
  either original controller identity is human (also after finish AI handoff).
  No barrier `0xe666` attenuation for car contacts.
- If both high-detail model children (`body+8`, `+140`) have nonzero masks, the
  first contact also takes one shared impact-fragment queue slot: final first
  damage strength (incl. equal-kind max), one quarter of summed car velocities;
  car IDs supply palettes; nonhuman Type2 uses original palette 11. The fragment
  allocator decides whether it yields particles. `427010/090/100` select these
  children independently of the physical parent; `427170` hides local children in
  cockpit view. The native host supplies this gate separately from physical
  sectors and wreck-retirement visibility.

### Continued contact and scrape handles

- Continued contact does not call `419ad0`. A confirmed pair is suppressed in
  later groups of the same update; only an attempted failed overlap clears the
  persistent state and sends event 12 to an allocated scrape handle. Unless the
  overlap set its longitudinal flag, a pair involving a human uses scrape event
  10 when its matrix entry is unallocated, else 11. Strength =
  `min(1, relative_speed * 0x106 * 0x1f40000)` with separate Q16 products.
- Host owns the four shared wall/car scrape slots; `425070` allocates the first
  nonplaying slot; the pair matrix remembers allocated slots independently of
  playing. `4250a0` event 10 consumes its discarded random draw and starts
  sample 50 only if stopped. Event 11 updates an existing playing sample, never
  creates one. Event 12 stops before volume gating. Pause calls
  `426400 → 426490`, stopping the four samples while keeping pair mappings;
  continued event 11 stays silent until a later recontact resets the mapping.
- Visible continued side contact queues sparks when |tangential relative speed|
  exceeds `0x1999`. Direction: normalized horizontal perpendicular to the contact
  normal, oriented against relative velocity; strength capped at 1; velocity =
  half the car-velocity sum. Fragment and spark queues have ten shared slots:
  distant/full integration wall requests precede car-pair requests, `432210`
  body-ground sparks follow. `415e10` suppresses spark consumption in
  rain/snow/storm, keeps fragment emission. Tangent spark-strength tests keep
  source lookup normalization (65522, not an ideal Q16 unit).

### Limits

Complete original/native checkpoint and process-RNG parity remain separate
G04/G05 acceptance; no mechanical-health or repair rules are inferred.

## Single Race rollover and removal

### Stabilization requests

`4102e0` (called by `40f5c0` for continuing ground support) measures the retained
support normal against the three body axes before its stabilization corrections.
Three independent signed-byte counters latch dynamics requests:

| Request | Ground dot | Consecutive supported updates |
| --- | --- | --- |
| Upside-down, `+2a0` / counter `+331` | up below `-f333` | 61 |
| Sideways, `+2a4` / counter `+332` | side outside `[-f333,f333]` | 11 |
| Pitched, `+2a8` / counter `+333` | forward outside `[-e666,e666]` | 26 |

Equality does not count. An interrupted unlatched interval clears its counter; a
latched request stays set until its source consumer clears it. Airborne updates
and the first landing contact do not advance the counters. Unrelated to
linked-barrier recovery flags.

### Wreck mode

- `414e20` keeps full physics for human ownership, player identity or the source
  first-tyre exception. Eligible nonplayers: upside-down/sideways requests select
  mode 3 unless already mode 2; pitch alone does not select the wreck path.
  Neighbors still fill the four-body budget through `414ef0`.
- `415240` calls `445360` for mode 3, then sets mode 2. The correction clears
  velocity, angular rates, delayed force/moment, wheel speeds, steering angle;
  keeps speed record, gear, pedals, engine RPM. After linked-barrier resolution it
  samples the cached HPC face, keeps horizontal position and inverted/side
  orientation: roof/side is placed at terrain height using original
  half-height/half-width, forward axis projected onto the support plane. Side
  wrecks also lower the source parity of four contact points by `cccc` along the
  support normal. Never rights a rival or moves it to POINTS.
- Mode 2 = stationary visible wreck, no removal timer. `415350` rebuilds
  car+4f4 visibility bits from the player renderer; `415240` removes a mode 2
  actor only when those bits are zero. A car contact can request another mode 3
  pass. `401650` unlinks the removed actor from the spatial ring and clears its
  active flag. Removal sets the retirement flag and clears lap/checkpoint only
  when game+54 says the actor has not finished; a finished actor may disappear
  while retaining its finish result. The host owns those lifecycle and
  classification records; results frozen at local finish stay unchanged.
- The settling path has no dedicated original sound call.

### Player righting

Player identity survives AI handoff. `415090` consumes a player's upside-down or
sideways request by enabling dynamics+31c; `40caf0` applies the side-dependent
`±ccc` angular velocity until the up dot reaches `b334`. Upside-down aborts that
update's full-body dispatcher (already visited distant integrations stay valid;
ground processing still runs); sideways continues. Pitched requests give the
side-dependent angular component `±2666`, also for a full nonplayer. Physical
rotations; no upright-pose replacement.

### Native structure

`Motion` separates integration from `finish_ground(BodyMode)`, so car and
shared-prop contacts run before source `415240`. `begin_tick` clears transient
contact/effect queues and deferred/preparation bookkeeping even for frozen,
removed or dispatcher-skipped actors. Default standalone updates still finish
their own ground pass. The host reads registered-sector visibility, omits
removed models/effects, releases their opponent voices.

### Limits

Bounded source recovery, not a matched full-process RNG/physics recording.
Render-sector pair ordering, further car-impact damage/debris consumers and
other G05/G06 branches remain explicitly open.

## Shared rough-surface sequence

`42e6f0` adds a signed Q16 height offset to each sampled contact whose resolved
material class is 2 or 3 and whose car speed record (`body+e0`) is strictly
greater than `0x8000`. It visits all eight contacts in index order (incl. upper
body corners); contact support is tested afterward. Water-marked material words
first keep only their low seven bits; classes outside 0–3 fall back to 0. The gate
uses the retained speed record, not initial qualifying velocity or a wheel-speed
threshold.

Table at `004ae988` (15 entries):

```text
655, -1310, -1966, 655, -1310, 0, -1310, -1966,
655, -1310, 655, 1966, -1310, -655, 1966
```

### Cursor lifetime

- Shared short `004d5634`: only decompiled writes are the increment and wrap to 0
  after entry 14 in `42e6f0`. Executable references: read `42e97e`, increment
  store `42e999`, wrap store `42e9a2`. Initial 0 in the PE `.data` zero-filled
  tail. `42f1c0` reloads HPC pointers/metadata through `004d5630` without
  resetting it; prop records start at `004d5638`. The sequence is process-lifetime:
  survives car construction, grid reset, qualifying-to-race and scene/track change.
- `421980` runs car contacts, shared prop contacts, then `415240` in ascending
  actor-slot order. Mode 0: `40f5c0 → 42e6f0` (consumes entries). Mode 1:
  `444ee0 → 42e9f0` (simpler plane, no roughness). Mode 3 settlement (`445360`)
  uses `42e9f0`; modes 2/4 have no ground update. Full-body integration runs
  earlier in descending order, so the cursor must not advance there.
- Other `42e6f0` call sites: `40a770` construction and two samples inside `412040`
  placement/orientation. Construction zeros `body+e0`; `412040` sets initial
  velocity but never the speed record. These setup samples consume no offsets
  (incl. qualifying's `0xcccc` initial velocity) and do not reset the cursor.

### Native

- `SurfaceRoughness` owns the cursor: `next()` returns the current value and
  advances modulo 15; `current()` returns the next table index. `Motion` requires
  a borrowed context after its `DamageContext` argument, no fallback. The
  application owns one context outside scene sessions, shared by local car and
  rivals; per-car reset preserves the reference. Paused scenes consume no entries.
  No new audio consumer.
- `import_cursor()` (diagnostic) accepts only 0–14. Actor snapshot restoration
  does not restore process state: recorded-snapshot replay imports its cursor
  explicitly, sequential replay imports it once with the initial capture; ordinary
  input updates never reimport. When the native unsupported-HPC boundary restores
  a car's last pose, offsets already consumed stay consumed.
- Establishes the shared sequence and call order; not complete original fleet
  trajectory parity. Complete original/native fleet trajectories and all process
  RNG consumers remain separate G05 acceptance work.

## Shared Single Race props

The scene owns one `PropContacts` world for all 16 registered cars. Rivals and
the local car move the same cones, posts and signs; rendering, fragment requests
and audio consume its single frame and event queue. Resetting a car does not
reset scenery; a new grid resets the shared controller once.

### Original order

- `421aa0` runs `415090` for vehicle movement, then advances registered cones
  (`458ac0`), posts (`45a460`) and signs (`45b010`) once, each in reverse
  registration order. `421980` then dispatches car contacts, calls `45b900`, and
  finishes each car's ground correction through `415240`.
- `45b900` visits registered cars in ascending actor order (ordinary race: full
  count; Time Trial: one). No full/distant/crash/removed body-mode filter. Each
  car visits its primary and neighboring sectors, testing posts, cones, then
  signs in registration order per sector. Candidate sectors stay those stored
  before contacts, also when an earlier car pushes a cone across a sector edge.
- The at-most-four vehicle sectors use a local fixed-size list: primary,
  horizontal, vertical, diagonal order, including repeated indices. The three
  prop-sector buffers are sized once to the registered controllers and fully
  refreshed before each dispatcher call visits its first car.
- The dispatcher clears post `+134` touched flags once before visiting cars. Only
  after the whole fleet is checked does an untouched held post enter its ten-tick
  return wait (per-car clear/finalize could release a post still held by an
  earlier opponent).

### Native API

`PropContacts::advance()` does the one scenery update and clears the previous
events. `contact(span<PropVehicle>)` runs the shared dispatcher and returns one
`PropResponse` per actor. The host supplies ascending identity order, applies
responses with `Motion::apply_prop_response`, then calls `finish_ground` after
deferred Motion updates. Cones get original forces without an invented car
impulse. Posts queue the original drag and body-local moment for the car's next
integration, after inverse mass and before the `0.2 * delta` scaling. Full-lap,
track-lap and replay consumers dispatch their one-car prop world in the same
phase; Motion neither owns nor advances props.

### Sound and fragments

- Human flag = `DAT_004c5d58[actor] != -1`, not current AI control or finish.
  Post and cone events carry it.
- Light-sign events keep `45d4a0`'s order: the first event is emitted before the
  local flag is assigned; the flag then survives later sign candidates for the
  same car and sector. Fast sign contacts hide their shared hierarchy and produce
  one fragment request, so a later car sees the already broken sign. Event
  ordinals preserve actor/sector/kind order when the scene merges sound and
  fragment allocation.

### Limits

Fixtures are controlled contacts; no claim that every fleet route hits every
authored prop or that original/native frame parity is complete. Continued
steering in the Donington cone-row scenario hits the wall first and gives
contacts below the source `0xf5b` audio threshold.

## Single Race fleet across tracks

`toca-rivals-tests canonical-root track-id [weather-choice [seed]]` runs one
ordered lap for each of the 15 opponents with all 16 physical bodies present. The
local car stays at the last grid slot with zero input (contact can still move it).
No placement, route cursor, lap or result is forced; the fixture calls the same
Motion, Rivals and RaceProgress consumers as the application.

- Step 40 ms, including the original held grid start and reaction delays. Bound:
  12000 steps / 480 active seconds. Missing ground or unsupported contact fails
  immediately for any actor. A missed finish prints each actor's route target, lap
  count, last target-change age, speed, gear, support, body mode, recovery state.
- Root-only invocation keeps the repeated three-lap Donington run (transcript/RNG
  comparison) and three one-lap seeds through 260 active seconds; other circuits
  run one lap each.

### Original resources

`scene::track_content` and `resolve_setup` share the application's track mapping.
`44e717/44e673/44e5cf` select each Keep/Free archive family; Keep members 0/1/3
supply HPC, POINTS and the original grid, Free member 1 supplies terrain-sector
candidates for barriers. AIBIN driver banks, cars, difficulty and weather come
from the qualifying data consumer. `4512b0` forces Lavaland dry even when snow is
requested. `403dd0/421390` consume directed checkpoints and final crossings.
Qualifying time generation initializes the grid and never replaces race laps or
physical results.

| Track ID | Circuit | Archive prefix |
| --- | --- | --- |
| 0 | Donington GP | `donl` |
| 1 | Silverstone | `silv` |
| 2 | Thruxton | `thru` |
| 3 | Brands Hatch | `bran` |
| 4 | Oulton Park | `oult` |
| 5 | Donington Short | `dons` |
| 6 | Croft | `crof` |
| 7 | Knockhill | `knoc` |
| 8 | Snetterton | `snet` |
| 9 | Lavaland | `land` |

Weather choices used: snow = 5, rain = 3. The fixture supplies the spatial actor
ring and route distances before every Rivals update and checks Motion boundaries
for the local body and all 15 opponents.

### Limits

Bounded physical fleet completion with canonical resources only. Does not
establish original/native frame parity, nor every seed and weather on each
circuit. Rollover and shared prop integration (see Single Race rollover and
removal) changed the phase order after the original per-track completion ticks
were recorded. The traffic-decision audit (see Source bounds under Single Race
traffic decisions) records why ordinary nonplayer AI has no autonomous pit-entry
selector.
