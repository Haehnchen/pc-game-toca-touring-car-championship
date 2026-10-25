# Local wheel and body motion

`simulation::Motion` owns controls, four wheel angular velocities, body velocity/angular velocity and the changing `scene::BodyPose`. It borrows the owning `HpcSurface` (must outlive it). The host supplies source time steps and timestamped input; no SDL, presentation delta or generic physics library enters the consumer.

## Recovered update order

Source: `references/decompiled/tourcars.c`. `00415090` dispatches `0040b360` (lines 14759–15075); `00415240` dispatches the separate contact update `0040f5c0` (17675–18090). Native tick:

1. Controls/filtering and engine moments before wheel integration: `Controls::begin_update`/`finish_update` keep `0040d3b0` → `0040db10` → `0040caf0` → `0040d5f0` (engine not coupled to the preceding tick's wheel rotation). Support flags, ground plane, surface classes cached from the preceding contact completion.
2. `0040c5f0` corner velocities from local angular velocity and body velocity. `0040be80` distributes normal load using actual support count, per-car mass, surface/weather friction tables.
3. `0040db10`: split brake torque, wheel feedback, front steering and rear body axes, longitudinal/lateral slip, source traction ellipse, surface drag. The squared force-magnitude expression and its lateral-limit test / longitudinal-limit replacement are kept (not a conventional tire model).
4. `0040d830`: sliding body contacts when the normal wheel-contact gate is false or upper corners touch. `0040caf0`: sums forces and local `force × arm` moments, source drag, fixed inverse mass/inertia, `0x3333` acceleration factor. Position uses new velocity minus the half-step velocity increment. Rotations use `00412d10` order Y, Z, X, normalizing the two affected basis rows after each nonzero rotation.
5. `0040d5f0` couples engine to new wheel speeds. Static barriers apply `00409c30` position/velocity correction and queue torque ([barrier contacts](barrier-contact.md)). Then `00421aa0` calls `00421980` → `00415240` → `0040f5c0`: cached HPC queries for all eight body corners, surface classes, `00410110` supports. `0040bbc0` selects the contact plane by the dominant vertical body axis; `00410190` shifts the body by the maximum active corner height difference (never mesh height at the body center).
6. `004102e0`: sign-crossing basis stabilization, angular damping, then normal-velocity projection (source lookup normalization, fixed products). Eight contact offsets refreshed via `0040c280` (same as initial setup). The host receives the already corrected pose.

Initial values:

- `0040a770`: normal-car dimensions, mass, drive/brake split, inverse inertia. Its call `00412890(dynamics, 0x1eb8)` (line 14553) sets all eight raw Q16 tire offsets `+dc..f8` to `0x1eb8` (7864/65536; not a percent, no rescaling). `0040be80` adds front/rear `+dc/+e0` to lateral friction and `+ec/+f0` to longitudinal friction before multiplying normal load; `0040db10` adds `+e4/+e8` to lateral ellipse shape and `+f4/+f8` to longitudinal. Body sliding offsets `+d4/+d8` are zero at init, no tire offset.
- `00412040`: qualifying initial speed `0xcccc`, wheel speed 4× that, zero race-grid velocity. Scalar body-speed record zero until first integration. Initial cached contact count = four lower corners, initial ground normal in body basis Y; no extra pre-physics contact tick; no constant forward acceleration or repeated initial-pose recalculation.
- `simulation/fixed.hpp`: wrapping 32-bit add/sub, shifted 64-bit products, recovered midpoint square-root lookup. Atan ratio keeps source equality index 512 and its adjacent zero table word. Trig tables reconstructed with the truncating table formulas of the controls/body consumers. 15-value rough-surface height cycle at `004ae988` applied by `0042e6f0` for classes2/3 above speed `0x8000`; all actors borrow one process-lifetime cursor across grid resets and scene reloads ([shared surface chronology](single-race.md#shared-rough-surface-sequence)).

## State handed to rendering, camera and audio

- `MotionState`: fixed body velocity, signed forward speed, wheel angular velocities, longitudinal/lateral slip, support flags/classes, body pose. `004193b0` derives signed forward speed from current velocity and basis X (incl. ground projection). Gameplay speed: `00403dd0` (lines 9533–9538): `4cc5e4 = mul16(body_speed, 0x640000)`; integer consumer = signed shift right 16. Record sync precedes physics in `00421aa0`, so `speed_integer` is captured at tick start (initially zero).
- `rumble`/`previous_rumble` = discrete groups at dynamics `+0x280..0x28c` / `+0x290..0x29c`. `0040f5c0` copies current to old, then sets current from surface class1 and previous contact flags; caller `0040fa..0040fb` compares them for actions13/14/15 (RUMBLE1..8 samples; not evidence of a tire-squeal model). `rumble_strength_q16` = body speed × `0x9851e` clamped `[0,65536]`. Skid consumer `0044b690` uses actual longitudinal/lateral slip ([tire sounds](driving-audio.md#moving-follow-camera-and-local-driving-audio)).

## Local-player grip correction

- `00460170` calls `00402c80` for the local player after car construction → `00402b90`, replacing all eight tire offsets with `{0,0,0,0,0xccc,0,0,0}`. Native local controls start with these values (a fresh grid start with `0x1eb8` everywhere gave dry lateral friction `0x28f5 + 0x1eb8` instead of `0x28f5`, ~75% too high). AI handoff, player restoration and temporary RPM-dependent launch adjustment keep separate rules. Qualifying restores player grip at the first start-line crossing.
- Friction profiles: sun/cloud profile0, storm profile1, rain/snow profile2. `4512b0` sets weather flags; `42e440→4128d0` selects profile2 for rain/snow, profile1 when the storm flag is set. Original rain recording update7: lateral limit 1421821 (`10155910 × 0x23d7` Q16); wrong profile gave 1523324. All 500 rain updates match after the fix ([continuous recordings](reference-replay.md)).
- Surface classes: 0/1 road/curb, 2 grass (reduced grip and rolling resistance), 3 gravel (own traction shape, slip scale, stronger drag). Each wheel uses its own sampled class. Original tables, no difficulty multiplier.

## Tire-offset writer audit

No distance-, time-, temperature- or slip-dependent tire wear exists in the recovered grip fields. Canonical executable (CRC32 `8ce56d41`) has eight direct calls to the common `00412890` writer:

| Caller | Meaning |
| --- | --- |
| `00402ac0` (`402aef`, `402aff`) | AI recovery: increase by `0x62` toward its AIBIN target. |
| `00402b10` (`402b5c`, `402b7f`) | AI position adjustment or AIBIN target. |
| `0040a770` (`40ae84`) | Temporary construction value `0x1eb8`. |
| `00422f60` (`422fef`, `423037`) | Launch correction decays by `0x73` toward zero. |
| `00428440` (`428658`) | Optional multiplayer catch-up by leader distance; front longitudinal offset additionally gets `0xccc`. |

Local reset `00402b90` has two direct callers: ownership restoration `402ce9` and completed launch correction `423054`. `00412c90` scales pedal/steering increments by delta; it does not age tires. Same-offset writes in collision/contact functions target the separate `0x53c` body record, not the `0x338` dynamics record. No degradation is added. Local and Linkup catch-up use this writer through the source throttle/tire ramp in `simulation/car_boost.*` ([network contract](network-protocol.md#race-ordering-and-explicit-native-adaptations)).

## Original recording corrections

Found by the [short original recordings](reference-replay.md#accepted-short-recordings):

- `0040db10`/`0040d830` form tangent vectors as `v + mul(normal, -dot(v, normal))`. Subtracting the positive product changes negative fixed-point rounding and can select another square-root lookup bucket. Wheel axes, tangent gravity and body-friction callers keep this order.
- `0047b150`/`0047b250` use x87 products, sum before conversion, one nearest-even `fistp`. `source_transform.hpp` preserves the summed result and tie/overflow behavior for steering-axis, contact-normal, force/moment and stabilization transforms; other fixed-point consumers keep their own rules.
- `004102e0`, branch range `411a55..411ead`, removes an angular component only when exactly one body axis was corrected (not yaw when two axes crossed together).

With the [brake-ramp fix](#input-and-filtering), all 4-, 32- and 64-update recordings match the selected original fields exactly, incl. the asphalt-to-grass transition. This establishes independent update comparisons, not a continuous lap or the full weather/car matrix.

## Grip and surface checks

Behavior verified on native source-consumer runs: lateral slip, reversed yaw under countersteering and post-braking stability on road/curb/grass/gravel and all five weather choices; sun/cloud and rain/snow each identical within their profile, both reduced-grip branches increase asphalt slip; road-to-grass crossings give mixed front/rear contacts; a rear-drive bonus car on snowy grass rotates against travel direction in a forward gear and regains grip after lift-off (no spin animation or forced yaw). Unsteered Donington Short qualifying reaches the real HPC edge at tick 96. These are not matched original recordings. Original trace/lap parity remains open.

## Runtime adaptation and remaining boundary

The native 25 Hz deadline schedule is a documented scheduling adaptation, independent of present FPS. The host consumes queued input at each simulation deadline and pauses/rebases on focus loss. `Motion` takes explicit `TimeStep` values and reads no wall clock. No original fixed 25 Hz loop is claimed.

All eight normal cars and resolved weather 1..5 use their source parameters. Missing HPC coverage preserves the last published pose and reports `missing_surface`; only a degenerate selected contact plane reports `unsupported_contact`. No arbitrary start-distance or ordinary slope stop.

This is not a complete race simulation. Car-to-car collisions,
external explosion effects, opponent actors and the full race lifecycle remain
incomplete. Static damage, wall fragments/sparks, first-contact landing sounds and dry body-contact ground-scrape sparks are connected ([contact effects](contact-effects.md)). Linked barrier recovery and session timing have their own consumers. The source's transient effect-position biases are not a second render/body pose. Driving outside HPC coverage is explicit rather than inventing a ground plane. These limits must not be described as completed collision or race logic.

## Local keyboard and drivetrain consumers

These controls run inside `simulation::Motion` on independent simulation deadlines. `begin_update` supplies filtered controls and drive moments before the wheel-force/body consumer; `finish_update` couples the motor to the resulting wheel velocities.

### Input and filtering

`0046ae6b` player-one keyboard: Up accelerates, Down brakes, Left/Right steer, F shifts up, V down. Z rear view, C changes camera, 1 another session action (outside this package). `0046a800` installs action masks; `00422c40` held actions, `00422c90` press edges, then `00422f60` maps the car fields:

| Original physics field | Native consumer |
| --- | --- |
| `+140`, `+144` | Independent gas/brake held values, zero or 65536 |
| `+138`, `+13c` | Left/Right with normal projection; Left wins a simultaneous pair |
| `+14c` | Gear press; Down wins simultaneous Up/Down |
| `+11c`, `+120` | Filtered throttle and its ramp |
| `+130`, `+134` | Filtered brake and its ramp |
| `+150`, `+274` | Filtered steering and signed steering angle |

- `0040b360` clears steering direction fields below speed `0xccc` before `00418cd0`: steering is suppressed at rest, active with actual body speed; held key reporting is distinct from filtered steering.
- Config `+24/+28` are X/Y projection-mirror flags, not drawable dimensions: `0046a8bb`/`00468d00` initialize/reset both to zero; `0048f5f9` negates its projection scales from them. Equal-flags branch of `00422f60`: action2 (Left) → `+138`, action3 (Right) → `+13c`; Right gives a positive wheel angle and a turn toward body-local −Z. Widescreen dimensions select no other branch.
- `00419630`: throttle ramp attack `fixed_mul(0xccc,delta)`, release 3× faster. `004197a0`: brake ramp attack `fixed_mul(0xccc,delta)`, immediate reset on release. Both use the original sine lookup curve before the per-car maximum multiplier. `0040a770` provides the eight normal cars' maxima and drive split. Dynamics `+3c` is `0xccc` (a former `0x9999` brake increment was a translation error): at a captured 1.5-step, car7's first brake update yields 3811 (raw Q16), not 32359.
- Steering filter `00419230`/`004193b0`/`00419550`: local player's configured flags 3 and original profiles 1–3 from `00418b60` (default config profile 2). Keeps quadratic input increment, minimum deflection, return bias, speed-based angle reduction, signed angle conversion. Products shift separately and wrap at 32 bits. Sine table follows `00478b20`'s formula with host `sin` (not the old CRT).
- Return correction uses dynamics `+44`, recomputed by `00412c90` as `fixed_mul(0x3333,delta)`; scales with the supplied source step like the steering increment. AI steering dispatches `004190f0` directly and leaves keyboard accumulator `+150` untouched; local ownership restoration `00402c80` clears only gear-input byte `+14c`, preserving `+14d..14f` and the accumulator.

### Gears and motor inputs

- `00418cd0` commits a queued gear change on the following update; a new gear edge is not processed during that commit. Manual mode1 (`00418f60`): gears 1–6 and reverse 7; Down from1 → 7; Up from 7 → 1. Neutral 0 is not a normal keyboard selection; no extra Neutral/Clutch key.
- Default transmission mode2 (`00418fd0`) = automatic with reverse: braking near standstill in first queues reverse and switches that pedal to gas; gas in reverse without brake queues first and restores normal pedals. Shift thresholds use the previous engine speed and original up/down tables. Manual/automatic comes from the committed frontend choice.
- `0040d3b0` computes engine torque, drag, per-wheel drive moments. Exposed `wheel_drive_q16` precedes `0040db10`; it is not wheel velocity or complete tire force. `0040d5f0` couples engine speed to the four wheel speeds and selected ratio/front-drive split, or applies the disconnected-engine `5*torque` branch. Overspeed keeps the original limiter state/load and clamp.
- `DrivetrainInputs` supplies upstream wheel speeds, unsigned speed magnitude, signed longitudinal speed and the clutch-disconnected record flag. `Motion` supplies the previous body/wheel fields before controls and new wheel fields after integration. The standalone `update` wrapper serves isolated control/coupling cases.

### Audio and movement

`VehicleState` exposes `engine_speed_q16` (`+124`), `rpm_scale_q16` (`+24`), filtered `throttle_q16` (`+11c`), gear (`+330`) to the audio adapter (`0044a6f0` RPM and gas expressions). Gas product `fixed_mul(throttle,0x640000)` is compared directly with 50/100 without another Q16 division (a normalized percentage would change that caller). Audio RPM floors at the caller's 2000 minimum; with actual wheel speeds RPM evolves with the coupling. Race/session gating remains separate from this accessible native free-drive mode.

Limit: checks are synthetic source-arithmetic cases; there is no original runtime recording or lap-physics acceptance in this batch.
