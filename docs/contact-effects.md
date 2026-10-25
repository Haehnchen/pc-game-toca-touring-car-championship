# Collision fragments, scrape sparks and landing sound

Strong static impacts emit original fragments; sliding along solid barriers emits sparks with the SCRAPE loop. Dry body contacts with the car on its side or roof emit ground sparks. A hard first landing plays one of five ROLL sounds. Pause freezes particle state and stops contact voices; resume waits for fresh contacts. A new race clears both particle pools and damage state.

## Spatial contact audio

- Impact, break, prop-contact, landing and wall-scrape sounds use the contact position and active camera direction. `4313d0/426190` derive listener heading from camera world axis `+bc/c0/c4` rounded to Q16; `424070` reads position `+cc/d0/d4` (full affine camera adapter, incl. CMMICRO local pitch/yaw). `423fb0` quantizes horizontal distance before the square root; direction keeps sub-unit precision through the original normalization/asin tables. 40-unit fade and RNG order intact.
- `424ac0` folds the relative 12-bit angle into the asymmetric pan curve: angles0/1024/2048/3072 → 68/122/68/5. Coincident stereo sources keep 68.
- Miles pan (`mss32.dll` CRC32 `1012e9cb`, `2000def0/2000de70`): below64 attenuate right by truncated `-2000*log10(63/pan)` hundredths of dB; above64 attenuate left using `127-pan`; centers63/64 keep both channels; endpoints silence the opposite channel ([DirectSound pan contract](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/mt708938(v=vs.85))). Gains are set before playback starts, including reused voices. Mono centers effects; returning to Stereo restores each voice's requested pan. Music keeps its separate stereo path. Volume curve: [Miles sample volume](driving-audio.md#original-miles-sample-volume).
- `contact_state.hpp` shares impact/scrape random gates, raw camera conversion, strongest-listener selection and sample/gain decisions with recorded replay. Impact3/4/5 draws follow spatial admission; valid-handle scrape10 draws even when spatial/gain rejects playback. Native contact audio precedes queued tires, then thunder and Engine/WHINE. Voice availability is an explicit replay input; native split-screen keeps its per-player adaptation.
- Goldens: CRC32 over all 4,096 angles `8b790799`; 1,024 camera/source records `132d051a`.

## Original consumers

- `409fa0`: selected contact after barrier response. Absolute normal speed >`8000` requests fragments with intensity `min(4*(speed-8000),10000)`. Solid kind1 also uses tangential speed >`4000` for scrape sound and sparks. Spark direction divides by `speed-4000` then points against travel (deliberately not a unit vector here). Source square-root lookup keeps its threshold rounding; `47b150/47b250` keep x87 sum-then-round via the source-transform helper.
- `415e70`: shares the 63-slot triangle pool with sign fragments; five shapes, twelve car/sign palettes, weather gain, count table, 25-tick lifetime, normal/Low Gravity acceleration. Init keeps 185 RNG draws; bursts use only the shared 37-entry cyclic table. The last Vectra2/AI palette lies beyond the weather-scaling loop and keeps its brightness.
- `418570`: separate 64-record spark pool; `418430` advances six-tick streaks; `418920` exports them; `446f20/47ca40` submits yellow world-space lines after double-sided fragments. Full-ring export quirk (head equals tail → no lines) kept.
- `446a80` updates existing fragments/sparks before HOR actors and queued `415e10` bursts: a new sign fragment moves immediately, new impact fragments/sparks first move next tick. Presentation does not advance simulation or the shared cursor.
- `409c30` stops/clears the prior local wall sound handle each tick; `409fa0/4250a0` starts member50 SCRAPE at 4× tangential strength and half local camera gain. Event10 consumes one discarded RNG draw even when distance/gain prevents playback. Native local caller uses the shared audio service without stealing voices; pause/exit releases only its own voice.
- `40f5c0` first-contact landing: event5, `51+rng()%5` by `424860`. Fixed impulse is below the visual-damage threshold (no dents); one-tick pending acceleration kept. Gate/order: see Damage/contact source audit.

## Ground contact

- `432210` queues mode1 sparks for dry asphalt when the saved pre-integration tyre-orientation gate (`40b360`, dynamics+2c0) is false. Uses the eight post-ground support flags and surface classes, current velocity and support plane. Positive separating normal speed removed; descending speed kept. Intensity `min((source_length(v)-3333)*8000,10000)`, must exceed `1999`. Rain, storm, snow, invisible roots and nonzero actor ownership state suppress it. Linked-barrier recovery is a separate state.
- Lower contact origins use sampled ground height. Upper contacts use authored side/roof positions from `415b00` via the quantized asin lookup, transformed with `47b150`, projected onto the support plane. Requests keep ascending contact order and share the ten-entry queue with wall sparks; `415e10/418570` consume them after the queued wall request; the 64-slot pool, six-tick movement, drawing, pause and reset are shared.
- The ground branch has no audio call. `4250a0` belongs to wall and car/car scraping; wheel/surface and landing sounds come from their own callers. Ground sparks consume the cyclic effect table, no sound-variation RNG draw. The shared surface cursor changes the contact burst count, as in the original.

## Limits

Opponent collisions remain open. This
package adds no invented engine or tire degradation: the audited damage and pit
paths contain no mechanical-health or repair consumer (see Damage/contact source
audit). Tire/surface trails and external explosion effects have separate producers.

Verification basis: extracted-consumer comparisons: 1,500 `409fa0` effect/contact records, 500 `415e70/416150/418570/418430/418920/418ac0` particle records, 2,000 `432210` request cases + 8,192 asin-table boundary cases (original `478b20` tables), 2,000 spatial records; all bit-exact. Direct consumer comparisons, not full-game recordings.

## Original car damage presentation

Static-barrier contacts change the local car: contact side and strength select dented panels, broken glass, headlights and mirrors. Strong contacts remove supported panels and launch the original detached meshes. A new scene or qualifying-to-grid restart restores the car and clears detached parts; focus pauses freeze their simulation.

### Source contract

| Address | Function |
|---|---|
| `00409fa0` | post-response normal impulse and barrier-plane contact in body coordinates |
| `00419ad0` | clamps Q16 length to 1; severity thresholds `4ccc`, `b333`, `f333`; one of six sides from normalized local coordinates with X bias `ccc` |
| `0041a280` | maps 17 slots to nodes `5,6,8,9,12,15,16,17,21,22,23,13,20,24,25,10,11` |
| `00480c90` | selects variants directly in HIGH/MEDIUM; LOW node709 shares slot3 |
| `00416320` | launches slots0..4 with source pivots, rotation matrices, velocity |
| `00417890` | flight/drag/gravity step |
| `00416870` | aligns landed parts to the sampled HPC plane (signed-axis rules, clearances) |
| `42e9f0` | keeps the cached plane when face lookup fails inside the bounded HPC root; detached landing uses the same consumer |
| `00417830` | frees a slot, hides both cloned faces, no pool compaction |
| `00424860` | sounds: event2 MIRROR member46, event3 HLIGHT members31/32, event4 SCREEN members37/38 from the resolved dry/wet archive |

- Severity3 is governed by per-part rules, a six-step cycle and a ten-record detached-parts pool. Bonus cars8/9 keep the original immunity. Model triples are detail families, not damage levels.
- Parts rest 700 ticks: zero stays active until the next update; hidden saved sectors retire them first. Recent/flying parts block another detachment for the original interval. Low Gravity uses the original reduced acceleration. Rendering uses HIGH variant2, including the paired/reversed back face.
- Variant draws consume the shared source RNG after spatial rejection and before voice allocation. The caller keeps the ordinary impact sound alongside the damage cue.
- Actor fields `+4ec/+514` feed `0040f5c0`/`0047eca3` force feedback ([horn.md](horn.md)); they are not a mesh-deformation input.
- The changing-count `417890` traversal can leave a higher active slot unadvanced. Reset/reuse keeps old timer/sector bytes until the next landing; a new launch cuts presentation history and supplies its actual actor/model owner. Native bindings keep that owner's model/livery after the car is removed. Shared selection scratch: [process ownership](single-race.md#warm-world-reset-and-resource-lifetime).
- Detached parts use the recovered [sector visibility](scene-rendering.md#scene-visibility-and-sector-order): creation and landed-part removal consume the retained previous selection before the current tick publishes its next view.
- Rival-car contacts share the damage and audio consumers; [recorded fleet comparisons](single-race-fleet-reference.md) track numeric agreement.

### Verification basis

- Extracted-function comparisons: 2,000 damage-selection cases and 1,200 signed/sloped landing records match exactly.
- Cached-plane landing in an uncovered HPC cell has a `DamageContext` regression. [Expiry interval](single-race-fleet-reference.md#visible-timer-expiry--2026-10-23): timer zero then slot release.

### Open

Original visibility retirement/reuse and imported detached render handles remain
separate comparison work.

## Damage/contact source audit

Bounded read of the canonical 1.1a decompilation: no mechanical degradation or repair consumer exists in the audited damage paths; no health, engine-loss or tire-wear model is invented.

### Damage state and consumers

- `419ad0` reads/writes seventeen bytes at `4c25a8 + slot*16 + actor`. Other readers: network sender `43ca70`; receiver `43bb60` applies received mesh variants. `41a280` clears bytes/variants/broken-feature flags; its only direct caller is actor/grid construction `44d91f` (reconstruction, not pit repair). PITS audit: no crew, repair or tire-change consumer ([pit lanes](pit-lanes.md)).
- Broken flags `4db8c8/cc/d0/d4` suppress light/indicator geometry in `4367f0` (caller of `439720`, `439500`, `4392d0`); no drivetrain/grip reader.
- Actor `+4ec/+514` = force-feedback strength/direction (`40f5c0`, `47eca3`). `+4f0` = ownership/recovery state (global actor0 `4cdc50`), not health. `+1f0..1f8`, `+1fc..204` accumulate external acceleration/moment for `40caf0`, cleared by `40b360`.
- `419ad0` callers: static barrier `409fa0`, car/car `428b50`, first ground contact `40f5c0`, expanding external blast `405ca0` (impulse25, range/position gate; not a rollover rule). Car/car uses the same consumer per eligible full body, persistent first-contact state, combined fracture sounds: [Single Race impact effects](single-race.md#single-race-car-impact-effects).
- `4c2f8c` selection cycle and `4c26b8` selection scratch are global; the 17 damage bytes are per actor. `416320/417890` own one ten-record detached-part pool (`4be220`) per scene; records keep the initiating actor/car mesh. Native: shared `DamageContext` per scene. Actor reconstruction resets that actor's visual variants and pending forces; grid debris clear empties the shared pool, keeping the global selection cycle/scratch. Ground failure cannot roll back records from another actor.

### Local-car landing slice

- `421980` dispatches contacts, then `415240` calls the local actor's `40f5c0`: samples HPC (`42e6f0`), rebuilds support (`410110`), computes plane (`40bbc0`), corrects height (`410190`), rebuilds support. If previous support count was zero, new count nonzero and incoming normal velocity strictly below `-3333`: averages the active local bounds corners, writes acceleration `{3333,0,0}`, calls `419ad0`, overrides returned kind with5, emits at the world-transformed average position; only then projects velocity onto the plane.
- Impulse Q16 length `3333` < visual threshold `4ccc`: advances the selection cycle, no dents/missing panels. Unlike the barrier caller, acceleration `{3333,0,0}` stays in the actor; the next `40caf0` consumes it before the clear, replacing any previous pending acceleration; moment unchanged. Native keeps this side effect without changing contact-correction arithmetic.
- Audio `424860` event5: member `51 + rng()%5`, after spatial rejection, before voice allocation; no barrier-caller scaling. Continuous support and gentle landings emit nothing. Motion impact list carries kind5 with segment `UINT32_MAX`; actor/grid reconstruction clears events, pending acceleration and that actor's visual variants, not the global selection cycle.
