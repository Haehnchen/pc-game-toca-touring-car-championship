# Completed: gameplay and bounded original comparisons

Open work: [porting plan](../porting-plan.md). The integrated modes are playable; the recorded
comparisons establish bounded source parity, not a complete autonomous original race. Source
traces/acquisition stay separate from automated checks and Git assets.

## Qualifying, handling and Time Trial

Original `402be0` controls the qualifying approach; `421390→402c80` returns control at the first
ordered start line. Three measured qualifying laps produce classification, finish camera/audio,
the earned grid. Native driving keeps source grip, steering return, surface/weather classes,
rollover, shared roughness. Dry/rain/snow 500-update recordings match 47 fields continuously from
one initial import plus captured inputs/deltas ([AI bounds](../ai-driver.md),
[handling](../vehicle-motion.md), [replay](../reference-replay.md)).

Complete Donington laps traverse 254 ordered points without resets/altered physics. Time Trial:
menus, unlimited laps, strict best replacement, independent ghost, pause, record pages,
save/restart/playback; the diagnostic schedule keeps 40 ms deadlines. [Full lap](../full-lap.md),
[Time Trial](../time-trial.md#time-trial-core), [ghost storage](frontend.md#native-ghost-storage),
[diagnostic stepping](foundation.md#diagnostic-stepping).

## Step 17: Single Race integration

Single Race: all 16 source identities, original grids, full/distant bodies, AI/start delays,
traffic, car/prop contacts, per-actor progress. Finite laps produce ranks, state10/13, localized
results; menu→grid→race→results→RACE AGAIN with spatial motors, effects, pause, restart. All 15
rivals finish an ordered lap on every track; selected rain/snow and deterministic three-lap
Donington paths covered ([race](../single-race.md), [AI](../single-race.md#single-race-traffic-decisions),
[tracks](../single-race.md#single-race-fleet-across-tracks)).

Pristine one-player NoQual initialization is generated from canonical assets and typed entry
configuration: 2,422 source-owned values, 15 RNG draws, 116 actor definitions, first full/distant
selection match at measured boundaries. The physical handoff supplies 52 body fields per actor,
eight calibration words, 27 contact-cache values; remaining consumed pristine actor values keep
their Motion/Controls constructors; initial actor/car/controller identity comes from the grid.
Cold contact/damage/part/roughness/prop state comes from production owners and canonical
resources (both aliases reject owned imports). Grid reset keeps process scratch and cone triangle
caches. One retained NoQual cold start → complete lap → all finishes → results → actual RACE AGAIN
→ next warm green matches 3,744 updates in Debug and ASan/UBSan; initial timer origins and later
steps are generated. Original scene/frame inputs and external input/callback context, other modes and
full autonomous race parity remain open.
[Startup/warm reset](../single-race.md#generated-noqual-startup),
[fleet evidence](../single-race-fleet-reference.md#continuous-cold-startup-and-local-race-timing).

Native Motion/Rivals/Linkup now consume one scene snapshot for registration,
retained LOD, retirement and separate effect gates. Full rivals retain manual
single-player wheel/exhaust eligibility outside geometry PVS. CPU/GPU visibility
and scoped fleet-consumer checks cover this contract. A separate actual-call
camera/scene journal now matches 3,411 startup values; normal View2 shares the
source body observer and forced model dispatch with live play. Camera generation
and moving PVS/LOD transitions remain outside that proof. A joined physical-frame
profile generates normal View2 camera/projection and primary/extra cadence with
3,216 exact values; physical bodies and initial root translations remain explicit
inputs in that standalone profile. The joined cold driving replay now derives
those states and roots from the accepted physical/cold owners in one execution:
64 updates, 33 publications, 35,383 exact scene values with active PVS/LOD changes.
Audio listener/callback context and remaining modes stay explicit.
[Joined scene](../scene-rendering.md#joined-physical-and-scene-replay).

## Fleet physics, AI and audio

The 16-actor recorder checks five phase boundaries. Independent physics imports each measured
update; continuous physical mode imports once and keeps actor/world state with observed external
inputs. Body selection, route recovery, traffic, checkpoint/lap timing, rival finish, local
controller calls share production consumers. Driver outputs feed independent physics before
contacts/ground; contact64 matches 1,648,590 named values and 4,128,768 owned bytes (44
full/distant transitions). Bounded: observed caller clocks, some initial world context and raw
device scratch do not prove input-only autonomy
([scope](../single-race-fleet-reference.md#comparison-scope),
[physical feedback](../single-race-fleet-reference.md#retained-driver-and-feedback-state--2026-10-18),
[limits](../single-race-fleet-reference.md#remaining-work)).

The same retained RNG stream covers AI, tire queues, WHINE/shift, impact/scrape, weather;
contact64 and other selected 64-update recordings have zero numeric differences through supported
consumers; initial callback context stays observed. Native playback uses source Miles gain/pan
rules and actual camera, material, wheel state
([random/audio](../single-race-fleet-reference.md#contact-and-weather-random-consumers--2026-10-18),
[driving audio](../driving-audio.md#moving-follow-camera-and-local-driving-audio), [engine audio](../driving-audio.md#local-engine-audio)).

Damage variants, detached parts, fragments, sparks, landing sound are live; one keys-only barrier
interval covers allocation, flight, landing, 50 resting updates (timer expiry measured).
Visibility retirement/reuse and render-owner reconstruction remain open.
[Damage](../contact-effects.md#original-car-damage-presentation), [detached interval](../single-race-fleet-reference.md#live-detached-part-interval--2026-10-17).

## Championship and high scores

12 meetings/24 races, qualifying stages, Short/Full options, points, weather, standings, five
named save slots, earned unlocks, Showdown. Finale: moving Pace Car/transporter convoy, top-three
figures, cameras, credits, CD04, motor/contact audio. Nine original high-score rows, eligibility,
CRC32-backed atomic persistence with Main High Scores and Time Trial records.
[Championship and records](../championship.md).

## Linkup baseline

ENet direct IPv4, one local driver per process, eight humans. Ownership, cadence, correction,
contacts, results, pause, loading recovery, committed-driver start, host-loss cleanup integrated.
Automatic host migration and internet services remain deferred. Local eight-peer capacity
is recorded in the [protocol](../network-protocol.md#local-eight-peer-capacity) and
[implementation](../implementation/04-multiplayer.md#n04c--local-eight-peer-capacity).
