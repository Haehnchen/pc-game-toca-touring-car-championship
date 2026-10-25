# Original fleet comparison

One-car recordings cannot establish fleet parity (no other 15 bodies, inputs, shared
contacts, global state). The fleet recorder enters the real Single Race menu and records
all 16 actors together. Default disables qualifying via the original option handler;
`--qualifying` selects QualOn for bounded local handoff recordings. The canonical
executable stays unchanged; acquisition uses the serialized Wine launcher, private Xvfb,
muted audio. Traces and reports stay outside Git (`build/test-artifacts/`). Unless stated,
recordings are Donington / sunny / Car 7 / NoQual; debugger pauses perturb QPC, so all
totals are arithmetic evidence, never performance measurements.

## Shared boundaries

| Boundary | State observed |
| --- | --- |
| `421aa0-entry` | Before input mapping, body-mode selection and AI decisions. |
| `415090-entry` | Mapped controls and dispatch modes, before body integration. |
| `421980-entry` | Bodies integrated and props advanced; before car/prop contacts. |
| `446a80-entry` | All ascending `415240` ground updates complete; before effects. |
| `421aa0-return` | Effects and player suspension presentation complete. |

Each boundary holds the same 16 body/dynamics/gameplay records and shared state; CRC32
protects the completed trace. Later snapshots are comparison evidence, never fed into a
claimed continuous simulation. `--ai-state` also records `403dd0-entry/-return` plus
initialized driver/distribution data, live POINTS and start-line tables once (CRC32,
bounded pointer/count checks); disk POINTS cannot replace these bytes (setup has already
changed speed/lateral fields). `421980` calls `446a80` after its ground loop, then
`42f950` per local player; `446a80` dispatches contact/effect pools, wheel effects and
presentation consumers. Ordinary `421aa0` does no further physics after `421980` (the
following branch is network-only). Comparing the ground boundary separately keeps effect
writes/RNG consumers from being misreported as integration errors.

## Comparison scope

- Independent physics: imports a captured fleet per update. Continuous physics: imports
  initial vehicle state once, then supplies observed mapped inputs and body dispatch per
  actor; keeps native vehicle/contact/roughness state. Supplying observed AI decisions
  does not establish native AI decision parity. `continuous-derived-body-selection` also
  keeps its own selected body modes (ordinary rival selector) but takes observed
  pre-update race metadata and mapped driver controls.
- `--compare-ai-route` adds independent route-progression/target comparison: imports each
  `403dd0-entry`, runs the production `AiDriver` route consumer in ascending actor order,
  compares outputs and shared RNG at `403dd0-return`. Not continuous AI, traffic,
  controls or race-clock parity.
- Replay also supplies observed AI tire offsets, start-clutch state, controller
  ownership, contact-pass enablement, physical-root registration/detail/effect
  visibility; rejects changed drivetrain/filter/tire configuration and unsupported low
  gravity. Cone/post/sign controller state is imported (incl. each sign's live model
  transform; untouched props can already differ from their reset state).
- Warm actor damage imports 17 mesh-level bytes + four broken-feature words. Detached-part
  physics imports all ten source slots, free flags and the changing count (the pool does
  not compact freed records). Source type determines the part slot; the pool stores no
  owning car/actor, so imported parts have an explicit unknown owner (cannot establish
  detached-part rendering parity; ordinary native detachment keeps actual identity).
- A complete autonomous comparison additionally needs initial AI/race progress, session
  lifecycle and all shared RNG consumers, then only local human input and time; it must
  report unsupported state and unexplained differences instead of repeated imports or
  tolerances.

## Reproduce and recorder options

```sh
make build BUILD_TARGETS=toca-fleet-reference-replay
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tools/trace-reference.py --scope fleet --updates 128 \
  --scenario fleet-pursuit --weather sunny --timeout 600
python3 tools/replay-fleet-reference.py PATH/trace.jsonl \
  --replay build/dev/toca-fleet-reference-replay --mode independent-physics
# --mode continuous-physics-inputs | continuous-derived-body-selection
```

CLI exit: 0 exact named outputs; 1 differences or incomplete requested AI interval; 2
unsupported/invalid recording. Five raw boundaries preserved; 161 integer fields per actor
at integration and ground completion, plus 34 numeric fields for each of ten detached
slots (before contacts, after ground, after effects). First difference per actor/phase and
max absolute differences, no tolerances. `--updates N` compares a prefix of the validated
recording (report keeps the full count). Failed reruns replace stale reports; native
execution errors stay distinct from unsupported source-state transitions. Reports separate
exact supported updates from complete interval coverage and physical parity from exact
prefix.

| Option (acquisition → replay) | Purpose |
| --- | --- |
| `--ai-state` → `--compare-ai-route` | Initialized AI/route boundary; sidecars `.ai-route.csv`, `.ai-route-unsupported.csv`. |
| `--checkpoint-timing` → `--compare-ai-timing` | Lap clocks, ordinary non-finishing laps, shared split table (`.ai-timing.csv`, `.checkpoint.csv`). |
| `--finish-timing` → `--compare-ai-finish` | Rival finishes and finish/player-display fields; needs route + timing comparisons. v8 PRE block = 372 bytes; POST = outputs only; sidecars `.finish.csv`, `.split-display.csv`. |
| `--local-handoffs` (+`--ai-state`) → `--compare-local-handoffs --local-handoffs-only` | Local controller calls; v9 PRE transaction 108 bytes; `.local-handoffs.csv`; verifies callee entry/return, stack return address, local actor argument; 22 scalar fields + unchanged RNG; all other gameplay/dynamics bytes retention-checked. No event → no parity pass; skips physical replay (physical/whole-session parity `null`). |
| `--session-boundaries` (needs `--finish-timing`, `--local-handoffs`) → `--compare-session-boundaries` | `403dd0-entry` → `403dd0-return` session outputs. |
| `--traffic-controls` → `--compare-traffic-controls` | `404c1a-entry` traffic/mapped-control decisions. |
| `--nearest-recovery` → `--compare-nearest-recovery` | `4046a3-entry`..`404731-exit` human recovery. |
| `--joined-ai` (+ai-state, traffic-controls) → `--compare-joined-ai` | `403dd0-entry`..`415090-entry` joined AI random sequence. |
| `--skid-dispatch` → `--compare-skid-dispatch` | `44c540`/`424d50` tire-audio dispatcher. |
| `--wheel-queue` → `--compare-wheel-queue` | `44b690`/`446b50` wheel-event preparation. |
| `--whine-rng --audio-null-sink` → `--compare-whine-rng` | Inter-update WHINE/shift state. |
| `--combined-rng`, `--contact-weather-rng` → `--compare-combined-rng` | Joint AI/tire/WHINE/contact/thunder RNG. |
| `--compare-driver-retention`, `-driver-physics`, `-drivetrain-feedback`, `-rpm-feedback`, `-limiter-feedback`, `-pedal-feedback`, `-control-feedback`, `-physical-feedback`, `-dispatch-feedback` | Successive retained-state comparisons (below). |
| `--compare-startup-join`, `-startup-first-selection`, `-startup-physical-body`, `-startup-control-calibration`, `-startup-contact-cache`, `-startup-world-reset` | Cold-start chain (below). |
| `--cold-grid-sequence` | Records both `460170` entry/return pairs + first PRE. |
| `--local-handoffs --session-boundaries --warmup-until-local-finish --scenario local-lap --laps 1`; `--warm-restart` | Local finish/results and warm restart provenance. |

Audio recordings (`--audio-null-sink`): explicit private `PULSE_SERVER=unix:/private/socket`,
dedicated PipeWire null sink, policy-only routing, no hardware devices registered, Wine
ALSA/OSS fallback disabled; backend config/graph retained with the capture. Plain `--mute`
disables audio drivers and cannot prove positive playback commands.

Scenarios and warmups (unrecorded warmup establishes no continuity from the grid):

- `--warmup-until-ai-point 86`: observes real rival driving until one reaches Donington's
  first timed checkpoint (≤3000 updates); point `0` also requires all ordered checkpoints
  and a pending non-finishing lap; combine with `--warmup-step 1000:` to pass the grid.
  `--warmup-step 550:Up --warmup-step 30:Up,Left` etc. verify source-counter advance.
- True rival finish: `--laps 1 --warmup-step 1000: --warmup-until-ai-finish`. Only explicit
  source evidence of the first rival finish permits the shared update counter reset.
- `--qualifying --warmup-until-local-rollout` (QualOn handback); `--scenario local-lap
  --warmup-until-local-finish` drives real keys to the final ordered start line (`--laps 1`
  race or `--qualifying` third lap; default bound 6000 updates, `--local-lap-max-updates
  18000`); driver uses live POINTS/heading/speed (acquisition input, not game AI; each key
  decision logged with CRC32).
- `fleet-pursuit`: follow the nearest forward rival; gate requires confirmed contacts.
  Debugger batches read-only memory commands while paused; cached bytes invalid after any
  execution/new stop.
- `fleet-detachment`: 50 released updates accelerating, then accelerator + right steering
  toward the outer barrier; at forward/negative-initial-side dot ≥ `b333` Q16 it releases
  steering and goes straight (needs race state6 and cleared clutch); after a live detached
  record it releases all keys. Coverage gate needs allocation + subsequent movement;
  failure = exit 2 (valid acquisition, insufficient coverage). `--warmup-until-detached-expiry`
  uses actual horizontal progress/speed (PRE62 of the earlier recording).
- Others: `accel`, `coast`, `brake-lamps` (Up → Down → Up → coast after `--warmup-step
  200:Up`), `--weather storm --warmup-until-thunder-ready`, `--warmup-detachment-approach`.

## Corrections recovered from the fleet

- `412040` initializes support count 4 independently of the eight support bytes; `40f5c0`
  consumes the stored count, also after distant driving. `40be80/40caf0` consume the
  separate `body+521` index list in reverse; stale upper support flags must not add four
  extra force contributors on distant→full promotion.
- `444830` filters distant controls through `418cd0` (ramp increments from init or last
  full update); skips `412c90`, the full-body low-speed steering clear and `40d3b0` torque.
  `4448b0` clamps/couples RPM directly (no `40d5f0` disconnected-engine/limiter tail).
- `444ab0` translates the retained eight contact points (a barrier miss must not rebuild
  them). `444ee0` updates the center triangle/material and aliased `body+1d8` height slot;
  other sampled heights/support flags stay retained.
- `40f5c0` publishes lower tire-contact points when ground dot Y exceeds `0xe666`, with
  offsets `0x13333/0x3333/0xe666`.
- Physical registration comes from `body+0`; `428b50` gates impact particles on `body+8`'s
  high-detail child (host supplies that separate LOD/cockpit gate).
- `402b10` changes all eight AI tire offsets during contact avoidance; `402ac0` returns
  them toward the baseline `4c3b88`. Observed AI decisions before `415090`: continuous
  physics receives them explicitly, validated against those transitions. Human tire
  configuration stays checked.
- `419ad0` writes feedback side/strength only when signed `body+53b != -1` (local player
  identity alone does not enable it).
- `40caf0` scales the whole retained contact-moment vector `body+1fc..204` by `body+3c`;
  the car-contact contribution must join barrier/prop moments before that multiplication.
- `4095b0` transforms barrier impulse and lever arm through `47b250`, rounding each
  complete product sum once to nearest-even (not truncating component products). `416320`
  (launch pivot) and `417890` (basis rows before normalization) call `47b150`, likewise
  rounding the complete dot product once, keeping separately truncated launch-velocity
  products and the `47fc00` rotation-matrix construction. Rotation rows use 16-byte
  strides, physical basis rows 12-byte strides.
- Detached allocation keeps its separate free table; `415d50` clears allocation without
  erasing flight fields; `416320` keeps timer/sector on reuse. Ground processing consumes
  the captured hardware-feedback side at its original boundary.
- `405530` nearby-target steering threshold at speed ≥150 reads traffic brake `game+1c`,
  not obstacle `game+40`.
- Route mode is byte `game+81` (separate from active flag `+4`); negative source speed
  selects one-point lookahead and its published integer keeps arithmetic-shift rounding.
- Distant barrier hits keep `4095b0` torque and dispatch `409fa0→419ad0` damage (native
  `update_distant` had dropped them).
- Directed contact history: original `428b50/428aa0` sets/clears both matrix directions;
  native stores both and keeps them until an attempted miss.

## Recorded coverage (facts per interval type)

- Initial no-contact (64 updates, grid state9 → race state6): 256 full + 768 distant actor
  updates; observed time steps two at `65536` and the rest `98304` Q16 (not an unperturbed
  40 ms baseline). No collision/roughness claim.
- Contact + roughness (128, `fleet-pursuit`): contacts pairs 8/11 and 13/14; roughness
  draws verified against full-body support gates and cursor. Empty-pool equality does not
  establish detached-flight parity.
- Prop/warm-world recordings (grid acceleration 96, turn into cone/barrier 64, warm wall
  32, damaged grass 16): cars 13/14 contact; cone 27 contact; barrier damage; roughness
  every update on damaged grass. Cone interval begins before launch completion (see launch).
  Autonomous AI needs ascending `402e90` rank/ring updates before contact grip: actor 13's
  grid contact uses rank 15 in `402b10` while its later consumed rank is 16; the replay
  validates exact contact outputs for source ranks 1–16 plus unchanged/base/recovery
  outputs rather than assuming the final rank was the rank at the unobserved call.
  Original live-part parity and imported mesh ownership remain open.

### Local launch boundary — 2026-10-17

`448130` initializes local correction `4c5d80` from retained engine RPM while the clutch is
held (before update 5 in the cone recording). `422f60` then moves `-5242` → `-5127` (decay
`0x73` toward zero) and writes all eight tire offsets. The clutch reconnects later (update
7 pre-boundary); the player-control reset restores `402b90` defaults before `422f60`
reapplies the next decayed correction. Initializing from the later clutch transition would
use a different RPM. Replay separates launch init, clutch state, player tire reset and the
local input-mapping step: imports the initial correction once, runs the shared controls
calculation from retained native RPM; later correction/RPM/tire values are outputs. Stream
`TOCAFLET4` + `.launch.csv` (eleven local control values at `415090`; release epoch
`4d3844` identifies `448130` in newer recordings; playback gate `4dcd9e==0` is an
assumption). The reset event comes from session/controller transitions, never expected
tire values. State9 WHIRR uses `44a6f0` audio-only speed; `414240`'s split branch returns
before that consumer.

### Body-selection boundary — 2026-10-17

`414e20/414ef0` runs before `403dd0` updates route/traffic records. Inputs present at
`421aa0-entry`: previous body mode, support count, retained ground-up dot `body+b4`,
rollover requests, first tire offset, human ownership, permanent player identity, spatial
neighbor ring/gaps. `414d20` sets the full-body budget to four. No RNG, no new driver
settings. The ground dot is the value from before stabilization, not a new dot product
with the final basis. Selector first counts bodies that must remain full, then promotes
distant neighbors in source direction order. Stream `TOCAFLET5` (600-byte pre-selection
block), `.selection.csv` (16 selected modes per update). Continuous mode imports
pre-selection physical fields only for the first decision, then uses retained native mode,
rollover, tire, support count, ground dot. All recorded direction flags are zero; the
alternate branch has component checks only.

### Initialized AI route boundary — 2026-10-17

Optional profile captures resolved drivers/distributions `4c3b90/540`, live POINTS
descriptor `54c120`, release counter `4d37b0`, start bounds and both 33-vector start-line
tables. Packed POINTS pointer ends in `...32`; `479182` keeps archive bytes unaligned
(debugger reads round outward, then keep the exact bytes). `TOCAFLET6` carries this setup
and `403dd0-entry` inputs only. Shared `AiDriver` handles initialized lateral high words,
release-table swaps, full/distant random draws, cached targets; mode 2 uses the
alternate-line distance. The independent boundary advances signed route counter `game+86`
against resolved driver byte 19 before deriving the refresh request (using the previous
`game+3c` request gave a one-update heading error); ordinary host cadence unchanged.
Only route mode 1 occurs in recordings; alternate-line behavior has component coverage.

### Route progression and recovery — 2026-10-17

`Session`/`AiDriver` share the `402910` boundary predicate and one-step `403dd0` cursor
transition (separately rounded Q16 products, equality on the passed side, forward
priority, reverse wrap). `4019f0` initializes each nearest-map entry to zero, then scans
candidates ascending outside `401860`'s cyclic 23-point window; wrapped coordinate
differences are shifted to integer XZ before squaring; only a strict improvement replaces
the candidate. Human refresh in `403dd0` likewise requires a strictly closer candidate;
its distance/lookahead work precedes recovery (target consumer keeps the intermediate
cursor, final cursor can change). Observed human nearest-route checks left the cursor
unchanged in that recording; a recovery jump has component coverage only.

### Human nearest-route recovery — 2026-10-17

`4046a3-entry`..`404731-exit` isolates the decision after gate progression; position,
checkpoints, laps and RNG untouched. `NearestRouteMap` serves AI progression, replay and
player `Session`. Native takes the refresh period from resolved driver byte 19, keeps
cadence across handoffs, applies same-tick qualifying release/finish ownership before
recovery. A 32-update recording had four checks on actor 14 (cursor 204→228, 229→203
twice); all match with all 254 map entries.

### Live detached-part interval — 2026-10-17

Recording: at update 104 local actor 14/car 7 loses slot 0/type 0 during integration; part
moves right after effects, lands at 110, rests through 159 (timer 651); no retirement/
expiry. Both `416320` and `417890` use `47b150` (see corrections).

#### Visible timer expiry — 2026-10-23

Warm 8-update recording: slot 0/type 0 starts resting at timer 2, advances 2→1→0 while
active, becomes inactive/free on the next update with count 1→0; its sector stays visible.
Comparator requires the chosen slot's zero checkpoint followed by visible expiry/free/count
decrease (an exact shorter prefix is insufficient); the focused expiry gate replaces the
former broad allocation/flight gate for future expiry captures. Original visibility-based
retirement, reuse and render-owner handles remain unmeasured by this interval.

### Checkpoint and lap timing — 2026-10-17

`race_timing.hpp` shares lap arithmetic and the `4212d0` 48-slot checkpoint writer with
`Session`/`RaceProgress`. Diagnostic derives the `403dd0` clock pass, unsigned best-time
comparison, lap increment and sector-0 write in source order. Observations: table `4cc2d8`,
race flags `4dcdb0`, session stage. `TOCAFLET7` imports a 600-byte PRE timing block per
independently compared update. Player split-display globals, finish/qualifying
transitions, pits and autonomous clocks/RNG stay outside this timing comparison.

### Distant barrier response — 2026-10-17

Full and distant Motion share the barrier damage/impact consumer. `444830` clears old
pending force/moment after movement and before `409c30`; new raw `4095b0` moments survive
`444ee0`; a following distant update discards them, a following full `40caf0` consumes them
once. `419ad0` also advances the shared damage cycle below its damage threshold. Owned
impact events feed race audio; distant ground counts each event once. Autonomous AI,
unobserved clocks/RNG and visual/audio parity remain outside the recorded comparison.

### Rival finish and checkpoint display — 2026-10-17

Shared `421390` arithmetic covers qualifying lap decisions and rival finish records.
`4212d0→41a840` supplies personal checkpoint references, signed differences and separate
leader-gap strings; `41ab70` display lifetime and `41e3d0` flashing feed the HUD (incl.
Mega SPLIT label); `425950` stops owned horns. One-lap capture: first rival finish
(actor 15) sets count 0→1, timer3/update counter reset (`2444→0`) and total 18,542
hundredths; actor 2 finishes later (count 1→2, timer2=timer3=36, total 33,020). Both
advance lap 1→2 and write the shared split table. No positive player-display mutation or
qualifying transition in that interval (event-parity fields `null`); source arithmetic,
component cases and the native race establish the implemented player display, not an
original event comparison. One cursor change during the actor pass does not isolate
recovery from an ordinary route crossing, so actual recovery remains unproven there.

### Local controller call boundaries — 2026-10-17

`Controls` and diagnostic replay share the exact `402be0/402c80` transaction. AI handoff
saves modes only from human ownership, keeps reverse recovery unless gear 7 sets it, sets
modes 1/1/0/0, resets all eight tire fields, sets countdown 25. Handback restores the four
saved modes and player tires, clearing only the queued gear-input byte `+14c` (assembly at
`402ce2` confirms the byte write; `+14d..14f` and steering accumulator `+150` untouched).
Mode order steering/gas/brake/transmission, network bits 26/24/25 for the first three.
The production wrapper preserves and pauses the separate launch correction while AI owns
the car; repeated handoffs use the same consumer.

Observed transactions (unchanged RNG, all 23 named values + raw records match): QualOn
rollout at update 11 (`402c80→421827`, actor 14: human 0→1, modes 1/1/0/0 → 0/0/0/2);
qualifying finish at update 23 (`402be0→4217aa`, human 1→0, modes 1/1/0/0, countdown 25,
resolved tire base); local race finish at update 22 (`402be0→42160b`). QualOn activates
only four moving cars (generic all-16-moving activity gate correctly fails). Three-event
controller parity only, not physical/session parity. Asymmetric analog flags, repeated
handoffs and retained `+14c` bytes have source-based component coverage; the original
events use default keyboard/automatic controls. See
[qualifying finish](session-finish.md#integrated-acceptance).

### Local session caller outputs — 2026-10-17

`--compare-session-boundaries` derives checkpoint/lap crossings from PRE geometry, then
shares the production clock, controller, finish and classification consumers; no physical
motion, route targets, traffic or RNG; unsupported branches reject the whole update. The
manifest adds classification order, split-enable flags, route length, release marker/flags
and the configuration tail (points, podium counters); v10 stream also carries local PRE
dynamics and launch/tire inputs. Normal play shares `4220c0` finish totals, `422720` awards
and `422880` standings with the replay; RNG-based retirement fallback remains unsupported.
Qualifying completion keeps `game+54=0`; `421390` on handback clears raw steering
`+138/+13c` and angle `+274`, keeps filtered `+150`; pending state and race-stop writes
keep WORD width. Observed positive events: qualifying finish at update 23 (local
completion 1, pending state 7, race-stop 1, player-0 split display active 0→1, ticks
201→0, sector 23,836→30,416, text `-00:01.88`→`+01:05.80`); one-lap finish (finish count
15→16, local count 0→1, finish position 16, pending 6→10, stop 0→1, split-enable cleared,
ten actors receive 66 points total, standings change; podium bytes zero because the
local actor is 16th — podium increments and total extrapolation are component-tested only);
QualOn rollout (`402c80→421827` returns actor 14 to human control; release globals, lap
clock, caller steering writes).

### Result-state comparison completion — 2026-10-17

In state13 `403dd0` still decrements the AI handoff countdown, updates active actor clocks
and formats local time components; its display guard excludes states 10/7, not 13.
`421390` returns early in 13, keeping laps, finish order, awards.

### Traffic decisions and mapped AI controls — 2026-10-17

`--traffic-controls` adds `404c1a-entry`: route/rank/neighbor ring/gap preparation done;
`4051a0` stopping, ascending `4036f0` passing, `403aa0` braking, reverse selection not
started. Retains all 16 raw records, RNG, weather/tuning, release globals, configuration
force-acceleration and live brake-bypass table. `--compare-traffic-controls` compares 16
traffic outputs at `403dd0-return`, then maps eligible AI controls in source descending
order through `415090-entry` (steering, throttle, brake, gear/reverse, reaction counters,
brake lights, both RNG boundaries). Production corrections: source route angle, all four
obstacle corridor flags, ascending passing/brake dependencies, release gates, headlight
cadence, reverse axes, reaction RNG, 16-tick brake-light latch. `game+10` (direction flag)
is separate from reverse recovery `game+28`; only the former selects the opposing-speed
stopping calculation. Low-speed obstacle steering returns before refreshing cached
angles. Signed stopping-table indices during reverse: the recorder captures the 1212-byte
window at `4bcce0` (incl. neighboring nearest-route map, pit notice and bound before the
table); no captured row is repaired. Presentation: AI headlight/brake requests; `434f00`
lamp coordinates and `439720/439500/4392d0` polygons keep car/detail variants, mirrored
winding, broken-light masks, alternate Vectra; weather enables base lamps, an AI flash
may turn headlights off; `402d00` is a headlight op with no audio call. Human brake
sampling and pixel-identical rendering are outside the comparison.

### Release state and joined AI random sequence — 2026-10-18

`460170` initializes `4d37b0` to 0; `448130` resets it, then installs 360 inside its
local-player loop for stages 2/4 outside Time Trial. Active wrapper `4482d0` decrements a
positive counter before `421aa0`; countdown, qualifying and result wrappers do not.
Native `Session` shares the counter with replay and the rival target/traffic consumers.
`401260(0)` clears reaction bytes without RNG draws (earlier native random reaction delays
were wrong for this caller). Native fleet observes every active target ascending
(incl. the human's cached target), then maps AI controls descending before any body
integration; physical dispatch consumes them once. Joined comparison (`403dd0-entry` →
`415090-entry`) starts each update from one observed PRE world and RNG seed, preserves AI
objects and RNG across targets, distance/rank/ring/gaps, traffic and controls; the
initialized cumulative route-distance table is retained explicitly (terrain projection
changes live POINTS, recomputing from projected coordinates would change input).
Observed: `448130` returns to `4220a6` before update 4, installs 360 without RNG; update 6
is the first `448434` return caller and consumes 359; target selection draws RNG in 27
updates, prestart controls consume fifteen draws each in updates 0–5. `game+6c` is saved
transmission; the distance delta is `game+f4` (`4049a4`). Clocks, initial AI/world data
and draws outside the joined pass remain observed (no scene-initialization or whole-process
RNG parity).

## Queued tire-audio consumer — 2026-10-18

`44c540` and its `424d50` calls: ascending actors/wheels; consumes queued start/continue/
stop events, updates channel masks and restart flags, selects tire samples from the shared
random stream. Surface 0 and 2 draw before spatial/gain rejection; surface 2 also draws
before rejecting an unowned actor. Visual effects use initialized cyclic tables. Input
boundary: actual `44c540` entry (queued events/strengths, dominant surfaces, masks, restart
flags, audio enable, ownership, one RNG seed); playback allocation and mixed PCM are outside
the numerical comparison. Native `Driving` shares the event consumer. Observed (asphalt
only): 1,008 calls (30 start, 378 continuation, 600 stop events, 7 restart decisions, 4
last-wheel stops, asphalt samples 39–44), 385 draws account for the complete
ground-to-update-return RNG gap. Grass/gravel ownership branches: source/component
coverage. Recorder arms the return breakpoint only during an actual callee. Engine WHINE
can draw between updates (next section).

## Retained wheel-event preparation — 2026-10-18

`44b690` copies current slip flags to previous, derives current slip from the signed32
longitudinal/lateral sum and airborne byte, prepares event 7/8/9 plus strength. `446b50`
clears event slots at view 0, prepares eligible actors ascending, resets hidden rivals'
slip state and requests deferred audio muting; in local multiplayer each view prepares its
own human. Callbacks between updates are variable (33 callbacks and 30 empty intervals in
63 intervals; one callback per update would be wrong). Replay imports initial queues and
both slip-flag arrays once, feeds derived queues into every following `44c540`. Native play
consumes the previous queue ascending and prepares the next at the 25 Hz deadline;
opponent channels share loaded samples and dispatch skid events without WHIRR/rolling/
rumble voices; visibility admits all rendered LODs for full-body actors; unowned actors keep
initialized dominant surface 0 (`425400` returns before updating their rolling material);
hidden actors settle pending muting after frame audio; focus loss clears queues/slip flags
with voices; pause freezes counters; Race Again resets them. Autonomous callback timing and
visibility, original skid-mark geometry, intervening mute/audio lifetime and WHINE draws
remain outside this comparison; whole-process random continuity not claimed.

## Inter-update WHINE/shift state — 2026-10-18

Shared engine consumer derives `44a6f0` physical/session conversion, `414240` normalized
gear, shift decrement and car-throttle ramp, then `414780` modulation, WHINE ramp,
remembered volume and playback commands. Seven numeric state fields imported once and
retained; raw caller inputs, callback timing, initialized curve/offset tables, camera mix
and voice-playing gates are observations. Each interval derives the next update's entry
seed from the prior POST RNG. Observed: 63 intervals, one call each; rates 11120–20595 Hz,
volumes 0–19; upward audio-gear changes −1→1 and 1→2 draw 8796 and 1930, both select phase
26/offset 0 (the first from State9's audio gear 1 despite physical gear 0). Nonzero shift
offsets, alternate gates, bonus cars, multiple locals, zero/multiple callbacks: component
coverage only. Native order: contact/material audio, ascending queued tire draws,
Engine/WHINE, WHIRR (updated shift counter). Not connected through intervening AI/physical
updates. Whole-process scene initialization, remembered volume across scenes and the
original multi-local caller remain separate; native split-screen owns one Engine per
player ([engine lifecycle](driving-audio.md#recorded-inter-update-state)).

## Combined AI, tire and WHINE random pass — 2026-10-18

One initial `403dd0` seed drives every selected AI, tire and WHINE call through the final
`421aa0` return; wheel queues/slip flags, dominant/per-wheel materials, restart/mask/
pending-mute state, camera mix and seven engine fields initialized once; shared consumers
derive `425400` material changes, `4257d0` camera policy, `425e90` muting in actual callback
order. Intermediate fleet RNG fields are zero and rejected if supplied. Observed events:
448 phase boundaries, 64 AI passes, 256 material calls, 64 tire dispatches, 63 motor calls,
63 mute calls, 33 wheel callbacks, one release; each intervening journal holds 2–4
callbacks (order recorded); 873 advances = 492 route + 60 descending-control + 319 tire +
2 WHINE; 22 calls with nonzero shift offsets. Native `Driving` shares the source material
transaction: wet external, View0/3 and showcase transitions from asphalt require all four
wheels to agree (former camera predicate was reversed); showcase playback uses the same
effective mix; unowned material/rumble calls keep their source gate. No random
impact/scrape/thunder calls in this interval; autonomous racing not proven.

## Contact and weather random consumers — 2026-10-18

Shared production contact consumer: impact 3/4/5 variation after spatial admission;
valid-handle scrape 10's discarded draw even when playback is rejected; thunder 25's early
draw without spatial query. Raw camera floats, initialized gains and strongest-listener
selection decide spatial/sample outcomes; the last queried listener leaves the shared camera
mix. Native contact playback order: wall integration, car pairs, props, ascending
landings, queued tires, pending thunder, Engine/WHINE. Each recording imports one initial
seed, queue/material/mute/camera/engine state; a warm interval imports its release counter
once. Observed: impact 4→38, landing 5→55, two valid scrape 10→50 draws; storm thunder 25
draws select 76/77 after tires; one thunder 77 request returns allocation success with a
nonzero handle. Impact 3 is covered only by source-backed component checks. Internal
`4798cb` requests are compared before its 127 clamp; observed wrapper success is
allocation evidence, not derived backend voice lifetime. Offline PCM order:
impact3→scrape10→tires→thunder25→WHINE→WHIRR on one stream. Recording options in the table
above (`--contact-weather-rng`, `--scenario fleet-detachment --warmup-detachment-approach`
or `--scenario accel --weather storm --warmup-until-thunder-ready`).

## Retained driver and feedback state — 2026-10-18

Each `--compare-*` mode below initializes its state from the first `421aa0` PRE only; later
copies are zeroed in the input stream and rejected by native replay; physical results feed
the following PRE and every checkpoint; expected-state mutations cannot alter replay inputs.

| Mode | Retained state / rule |
| --- | --- |
| `driver-retention` | 16 `AiDriver` objects, full gameplay records, route/rank, braking history, control/lamp latches; one shared seed for AI/tire/contact/thunder/WHINE. Native route observation keeps source heading cadence; original short-player gap reuses the last active route actor's pose (`404811→404b07`); `PitLane::source_zone` supplies the PITS entry-side gate. Human mapper `421d24→422f60→421d29` is an external transaction. Audio ownership from `4c5d58`, identity from body+515; WHINE speed cache derived from retained `game+cc`. |
| human brake lamp | `4608b0` publishes raw brake `dynamics+144` → `body+4e8`. `446b50`'s first actor loop updates `body+508` only for view 0 and `gameplay+8 != 0`: published brake ≥ `0x1999` and `dynamics+304 == 0`; before render bypass/visibility checks. AI actors and later views keep lamps. Both controller handoff directions preserve the lamp output. TOCACOMB v4 supplies callback PRE brake/reverse pedal. |
| brake publication | `4608b0` copies `dynamics+144` → `body+4e8` for every allocated actor in descending slot order (`4610ce`), independent of ownership/visibility/body mode; precedes the lamp callback. Recorder observes entry, completed loop (`4610ed`), return; calls happen between wheel callbacks independently. |
| `driver-physics` | Generated gameplay, raw axes, tire offsets, mapped gear byte, lights, published pedals replace later observed copies before Motion restore; gear mapping starts from the observed incoming gear `330` and replaces only that byte (adjacent rollover counters kept); selected reverse = `gameplay+28`; physical reverse pedal `304`, queued gear edge `14c`, clutch `148` stay observed; mapped `SourceInput` avoids a second `422f60` call and launch-tire decay. Launch scalar: `448130` from physical RPM/max; `422f60` advances by `0x73` toward zero, publishes all eight tire values; `4483da→402c80` resets tires before the first active wrapper without clearing the scalar. |
| `drivetrain-feedback` | gear `330`, pending `334`, pending flag `30c`, reverse pedal `304`. `403dd0` recovery store changes only byte 330 and on exit clears 304; keeps pending 334/30c, so the following `418cd0` commit can override. `418e70` mode-0 gear-edge toggle and `401260`'s signed-player/manual release condition preserved. |
| `rpm-feedback` | RPM `124`, previous RPM `128`. Full `40b360` copies `128=124` before integration; distant `444830/4448b0` and frozen modes retain 128. `420be0` uses `124-128` to gate type 17 emission with constant strength `0x10000`; recorded contact calls lack actor identity (schedule observed; no generated exhaust eligibility). |
| `limiter-feedback` | per-actor limiter flag `4be150`, load `4be108`. `40d3b0` consumes the previous active flag/load; full `40d5f0` resets the flag; only overspeed above `0xcccc` refreshes load and activates the flag; other full branches retain the load; distant retains both. |
| `pedal-feedback` | cached steps `14/38`, ramps `120/134`, outputs `11c/130`. Full `412c90` refreshes steps; distant `418cd0` advances filters using retained steps; `44a6f0` consumes retained `11c` throttle. Direct-brake ramp retention and frozen-mode positive captures absent. |
| `control-feedback` | cached return `44`, accumulator `150`, signed int16 angle `274`; adjacent configuration `276` preserved. Full `412c90` refreshes 44, distant retains it, direct steering retains 150. Ownership/reset/frozen events and human direct slew have no positive capture. |
| `physical-feedback` | 16 persistent Motion actors and roughness/damage/detached/contact/prop state initialized once; Motion publication keeps stale tire-patch offsets and unused support-list entries; cached integration/ground views keep contact/ascending-ground order. Dispatch, registration/visibility/ownership, human mapping/clocks and material/contact-audio arguments stay observed. |
| `dispatch-feedback` | `414e20/414ef0` runs once at the pre-AI boundary from continuous Motion (previous modes, rollover/support/first-tire, ground dot) and retained gameplay (player identity, ring, gaps). Covers ordinary direction, forced full, supported-full demotion, unsupported/tilted retention; not alternate-ring, closer-behind, frozen/removed, reset or ownership/allocation-change branches. |

Observed-only boundaries across these modes: callback timing, human mapping, body/wheel/
contact context, clocks, allocation/visibility/lifecycle, material/contact-audio
arguments. Cold-grid construction and full-feedback AI recovery: see below. Recorded
pedal values 0/65536 and view 0; threshold boundaries, other views, brake-plus-reverse
suppression, pending-commit/recovery combinations: synthetic source-backed coverage. Physical
recovery changes (`e51dcb2c`, updates 33/37/45) exercise generated recovery in the AI unit
check with route angle/speed/barrier observations supplied; no complete retained-driver/RNG
recovery interval is claimed. Older incomplete traffic manifests are rejected, not upgraded.

## Cold startup writers — 2026-10-19

Ordinary one-player NoQual calls `447980→460170` during `44f0a3` loading (parent return
`44fcff`), then `447a70` sets the state9 entry flag and the first `448790` callback calls
`447980` again before `44889c→421aa0` (parent return `4487d2`); both initializer calls
return at `4479e5`, first PRE returns at `4488a1`. Each call consumes 15 CRT draws; no
RNG draw between the second return and first PRE. `--cold-grid-sequence` keeps the hook
until first PRE and records both fixed raw entry/return pairs (47,526 bytes per boundary,
16 slots, per-block CRC32), actual callers and the first-PRE endpoint. Entry→return also
changes CONFIG `+60c` and the local driver name. The second call changes 13 ranks/
positions, all 16 rings and seven body bases; its return equals the independently captured
first PRE.

- Factory input: 82 bytes (resolved track/car/weather, ordinary difficulty, manual/steering
  settings, player name, sixteen entry pole counters, entry RNG). No initialized actors,
  return values, resolved driver tables or first-PRE state. Scope guards require fresh
  driver bindings, zero inherited raw controls and matching saved control modes. The
  repeated call uses only generated driver identities, canonical driver data, pole
  counters and RNG from the first result.
- Source order: `444270` classifies through still-zero gameplay `+8d` bindings before
  `401370` publishes identities (the second call sees the first call's identities), and
  increments the pole counter before the local-last swap. `40a770` leaves the global
  current-body on actor 15 and each loop re-establishes canonical loader supports;
  `412040` does not rebind it for actors 0..14. `4019f0` recomputes authored POINTS heights
  with the collision surface; the shared height loader and `401370` AI cache initializer
  also serve live rivals. Launcher may reuse a saved TOURCARS.CFG and `460070` clears
  counters conditionally, so entry score provenance must be recorded, not inferred.
- Startup join (`--compare-startup-join`): consumer is `403dd0` PRE after `414e20`;
  116 named actor definitions except body `4f0` survive unchanged from first `421aa0` PRE.
  The five inherited raw control words stay guarded observations. Stream guards reject
  owned fields in both initial snapshots, compact route duplicates, status prefix, local
  strcpy prefix, selector aliases, release state. Journal seed must be zero.
- First selection (`--compare-startup-first-selection`): `414e20/414ef0` once before first
  `403dd0`; previous modes, identities, poses, supports, tire state, normal, rings, gaps,
  budget generated; `40a770` supplies both rollover flags, `401370` direction, canonical
  qualifying data the tire threshold. Observed: all 16 previous modes distant; actors
  8, 9, 13, 14 become full. No RNG.
- Consumed physical initialization inventory: accepted consumed body intersection = 52
  field definitions / 175 bytes per actor (incl. previous mode); overlay only that subset
  at the initial handoff (`--compare-startup-physical-body`), reject every imported alias;
  initial dynamics/gameplay from the phase-correct retained-driver state; selector runs
  once at the journal's pre-AI boundary. Counterexample against copying the full factory
  snapshot: original first PRE has body `268` previous-position words zero for all actors
  while Motion publication carries the placed position (full/distant updates replace it
  before the first contact consumer). `game28` reverse fallback and saved controller modes
  `game60..6c` stay inherited; the initializer's human `dyn14c` writer owns only the low
  byte. Use the cold recording for the first physical handoff; the moving contact
  recording (starts in state6, no cold-entry provenance) is a separate regression — never
  apply cold resets to it or join the recordings.
- Calibration (`--compare-startup-control-calibration`): eight `40a770` words dynamics
  `00c/010/018/01c/024/030/03c/048` from a const Controls publisher sharing the car
  constructor values/filter bases. `010` = base throttle (`428440` reads it, changes
  current `00c`); `412c90` reads filter bases `018/03c/048`, updates retained increments
  `014/038/044`. Physical configuration caching happens after restoration.
- Contact cache (`--compare-startup-contact-cache`): 27 definitions / 68 bytes per actor:
  eight support heights (`body1c4`), three normal components (`body1e4`), eight triangle
  IDs (`dyn264`), eight material-class bytes (`dyn328`) from canonical SGRID/HPC.
  `42e6f0` writes all eight entries, `40bbc0` the support normal; actors 0–14 keep loader
  support while placement operates with actor 15 current; actor 15 uses the current-body
  path. Initial material classes all 0 (other classes/cold roughness have no positive
  coverage).
- Not generated (no claim): QPC frequency, name tails, padding, retained RPM/gear/lamp/
  support tails, rendering ownership, upper support tails, other wheel/body state, loader/
  render ownership, full first-PRE physical storage.

<a id="generated-startup-pair-touching-reset--2026-10-19"></a>

## Generated cold contact, damage and prop world

`--compare-startup-world-reset` constructs its cold mutable world from production owners,
not the first captured world copy; both FLET streams reject owned world inputs from update 0.
The same mode keeps fresh Motion/Controls values for the consumed cold actor context
(kinematics, wheel speed/slip/rumble, visual/righting/rollover, patch latch, tire-effect
gate, pending prop forces, raw controls/throttle); the shared cold contract supplies the
first driver and physical restores; authored body, calibration and filter writers keep
their chronology; initial actor/car/controller identity comes from the generated grid. Cold
path is separate from observed/warm restoration (no live race reset).

- Previous position and full-body previous RPM/engine drive are overwritten before first
  read. Wheel axes/patch payloads with latch 0, inactive support tails, pending-gear
  storage: no consumed-state claim.
- `RaceWorldProcessContext` owns roughness and persistent damage cycle/selection across
  scene reloads. `reset_grid_world`: contacts `4287f0`, active/free detached parts
  `415d50`, registered props `45c4c0`; preserves process scratch, roughness, unused dormant
  state; used for native cold entry and grid restart; actor repair keeps its RNG
  chronology. Cone triangle hints start at pristine 0 and survive grid reset (`45c4d0`
  does not clear the cached triangle).
- World CSV compares named constructor state before/after the cold physical restore (18
  consumed ranges, 183 bytes per actor): identity/handoff, touching, damage/features/
  feedback, barrier-active/contact counts, process scratch, active/free parts, complete
  registered cone/post/sign geometry, transforms, mutable state. Resources come from
  canonical loading; serialized counts are registration declarations only. Pair handles,
  unread partner bytes, inactive barrier vectors and dormant/free payloads establish no
  consumed-state coverage.
- Later controller ownership, root registration, visibility/model detail, contact enable
  and external input/callback timing remain explicit (scene/frame/lifecycle boundaries; no
  render-callback simulation). [Clock contract](simulation-time.md#recorded-startup-clock-transactions).

The native scene publishes registration, LOD and separate effect gates through
one shared snapshot. Historical Fleet recordings retain observed scene outputs;
sparse listeners/view IDs cannot reconstruct missing camera/refresh inputs.
A separate actual-call scene journal now executes initialization, migration,
PVS and model dispatch through the production owners. Its bounded startup
comparison is 3,411 values exact. A joined normal View2 frame replay additionally
generates camera/projection and primary/extra refresh cadence from physical
boundaries and external QPC samples: 3,216 values exact. Connecting those supplied
physical states to the autonomous replay and driving PVS/LOD transitions remain
open for that standalone boundary. `--compare-startup-scene` now closes the normal
cold driving join: generated physical states and authored roots feed the scene
in one execution, with 35,383 exact scene values across 64 updates/33 publications,
including active PVS/LOD changes. Ordinary C/Z views1..5 additionally match
52,756 values. `--compare-startup-scene-lifecycle` consumes the existing full-race
recording through finish/results/warm restart: 1,475,168 physical-PRE root/PVS
and wheel-mask values match with no scene-state imports. Authored camera matrices
and ordered PVS lists were not recorded. Other mode entries and observed audio
listener/callback origins remain explicit. [Scene contracts](scene-rendering.md#finish-results-and-warm-scene-consumers).

## Continuous cold startup and local race timing

`--compare-startup-world-reset` matches one continuous NoQual cold start, all 16 finishes,
local results, RACE AGAIN confirmation and the next warm green over 3,744 updates; driver,
physical and timing state retained across the interval, no intermediate world import.

- One timer bank owns countdown/release, checkpoint displays, finish/results, restart. One
  observed creation clock generates all five initial origins; every later QPC sample
  (incl. menu-only frames) feeds the same clock owner. Initial stored divisor/retained step
  and external clocks stay explicit ([clock contract](simulation-time.md#recorded-startup-clock-transactions)).
- The state10 wrapper clears the raw start phase before every physical call (`448c33`),
  keeping clocks, timer origins, release latches. Its pending 13 write follows physical
  POST; the next primary can commit 13 before the next physical PRE (accept that observed
  adjacent-record transition; no artificial state10/pending13 snapshot).
- The earlier 1,248-update checkpoint capture and cold64/256 recordings keep their bounded
  clock context (missing creator/menu samples cannot be reconstructed). Initial unowned
  tables/text/cache, mapped human input and actual callback scheduling remain explicit
  inputs; authored autonomous race initialization is still open.

## Retained laps, finishes, local results and warm restart — 2026-10-23

- AI laps/finishes: shared lap tables, finish order/counters, release epoch and display
  state initialized once from captured checkpoint/finish provenance; later values derived
  by production lap/finish consumers; later copies erased from input and rejected. External
  caller clock observed through the AI journal; no intermediate RNG import. First AI finish
  resets the retained update counter, later updates increment once; synchronizing the AI
  finish latch preserves gearbox/reverse state (no synthesized handoff). Supports ordinary
  state6 before local finish; active/new local split-display lifetimes, local controller/
  classification handoff, qualifying, retirement unsupported in this profile. Prefix
  before the chosen finish is insufficient (recording must include it).
- Local finish/results: actual final-line crossing drives the shared handoff, finish
  classification and score consumers, then generated AI controls feed the physical actor.
  Neutral `44a370` journal entries mark primary dispatch boundaries and verify the previous
  cached clock; they commit generated pending states (an extra update does not). First
  state10 wrapper resets timers 0/3 from its new caller clock; state13 entry resets timer0
  from the preceding cached clock. State13's input wrapper writes temporary local device
  axes before the AI mapper replaces them (device-pointer contents not captured): only
  those four raw axes at the pre-AI checkpoint and the two steering aliases in each
  AI-return session/traffic output are outside the comparison. Native adapter rejects
  state13 reverse steering (that branch can consume the missing scratch). Initial active
  split displays and new local split production unsupported. Result table timeout (40 s)
  uses the result-entry timer. Observed: local actor 14 finishes at update 23 in place 16;
  state10 at 24, state13 at 28.
- Warm restart: `441b33` records the external RACE AGAIN YES input; `449f50` consumes the
  separately observed `447a77` tick sample and creates all five timer origins. Overlay
  primary clocks can advance without a physical update. The following
  `448790→447980→460170` runs the shared warm grid writer once from retained identities,
  scores, bindings, actors and live RNG; actor/world repair resets feed the same fleet;
  later source grid snapshots cannot initialize it. Update counter, release epoch,
  roughness, damage scratch, shared effect histories keep their lifetimes; release counter
  resets and the new countdown owns its green epoch ([warm world contract](single-race.md#warm-world-reset-and-resource-lifetime)).
  Observed: finish at 23, state10 at 25, state13 at 29, YES/reset/grid at 39, four state9
  updates then state6 at 43; grid consumes 15 draws and retains counter 2100. 80
  overwritten-before-use raw-axis values stay unclaimed. One disabled wheel-audio callback
  during the state13 confirmation overlay is accepted only in this interstitial phase
  (camera state retains, wheel queues clear). Initial warm state and observed clocks/
  callbacks remain inputs; not one autonomous race from authored setup.

## Remaining work

- Compare positive detached visibility retirement/reuse and other part types.
  Visible timer expiry is measured above. Runtime lifetime/owner rules are
  source-audited and component-tested;
  the cached-plane landing fix is covered in [damage](contact-effects.md#original-car-damage-presentation).
  Add track/weather intervals only for a named uncovered transition or branch.
- Compare remaining raw-only effect pools and scrape-handle state. Resolve
  detached render ownership before making a visual parity claim.
- Complete autonomous racing from authored setup, human input and elapsed time.
  Reuse the accepted continuous NoQual lap/finish/results/warm-restart path;
  qualifying, pits, RNG-based retirement classification and other mode entries
  keep their explicit unsupported boundaries.
- Derive consumed remaining world/lifecycle initialization and frame/callback
  scheduling. Initial warm context, external clocks and actual callback journals
  remain observed; actorless type17 callbacks still need original actor binding.
  Reuse the accepted startup body/control/support/pair initialization and retained
  driver/physical/RNG path. Preserve body selection → route/traffic → descending
  controls → physics/contacts/ascending ground → effects. Captured live POINTS,
  start-line targets and initialized cumulative distances must not be replaced
  with a guessed disk-table reconstruction.
