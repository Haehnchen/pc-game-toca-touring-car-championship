# Track prop contacts

C05c connects the registered CONES, POSTS and SIGNS to the moving local car (geometry from C05a): contact candidates, prop motion, vehicle feedback, sign fragments and their sounds. FLAGS and HOR actors keep separate controllers. Evidence: `references/decompiled/tourcars.c` (C-line numbers below).

## Source order

| Original caller | Behavior |
| --- | --- |
| `421aa0`, C30419–30456 | After car force integration: cones `458ac0`, posts `45a460`, signs `45b010`, then flags; then `421980`. |
| `421980`, C30247 | Dispatch prop contacts via `45b900`, `415240` ground correction, then `446a80` scenery/actor updates. |
| `45b900`, C70154–70301 | Group candidates by current actor sector: primary sector plus up to three boundary neighbors; visit posts, cones, signs in registered order. |
| `47dab0` / `47e3c0`, C92577 / C93082 | 64-unit sector arithmetic, 4.5-unit vehicle margin, authored terrain-sector centers; no whole-map search. |
| `45c4c0`, C70713–70867 | Reset cone/post/sign controllers for a new grid; flags and moving HOR state independent. |

- `PropContacts::advance()` then `contact()` (existing prop motion before the new hit), once per active 25 Hz update. Rendering reads poses; focus loss advances neither vehicle nor props. Single Race dispatches all sixteen vehicles through one shared pass; qualifying/Time Trial one ([fleet contacts](single-race.md#shared-single-race-props)).
- Candidate sector membership is fixed during one dispatch (source actor's stored sector field): a cone pushed across a boundary gives one contact and sound that tick.

## Contact and motion consumers

- Cones: `45c6c0` rejects cones outside the coarse radius and expanded local bounds, derives an outward direction, projects it onto the ground for supported cones, uses `45d270`'s X/Z/Y exit-distance selection to move the cone out. Relative velocity gives force; supported hits add lift and local torque. No fabricated car impulse.
- `458ac0`: gravity, ground damping, force/moment integration, linear/angular speed caps, sleeping state2. `4594f0`/`459fd0`: upright/tipped support and piecewise support-height curve. `458680` constants rebuilt from the original lookup entries (square-root entry1870, atan entries194/389), ordinary and low gravity. Per-product shifts and wrapped additions use shared fixed arithmetic.
- Lavaland's five `GARCON_1` obstacles use the cone controller. Authored mesh center is `41023/131072` above its pivot (≈0.317 visible clearance with ordinary cone support). `prepare_cone_model` centers only the render copy before rotation in every detail profile (settled upright clearance ≈0.00425). Placements, collisions, controller state source-exact. Mega keeps the gargoyle shape and both `GARG01`/`GARG02` slots. Lavaland has no post/sign controllers.
- `494a40` stores actor matrices as binary32 before prop constructors restore Q16. Audit of all ten FREE archives, members8–11: 1503 nodes, 24048 matrix fields round-trip exactly, so `ModelNode` matrices preserve authored prop coordinates/orientations without a raw copy.
- Posts: `45bc50` tests either endpoint against expanded local car bounds. First hit initializes a 25° bend about the horizontal velocity's perpendicular axis. Weak posts return after release and a ten-tick wait; strong hits keep state5. `45a460` owns bend, ground clamp, return rotation, original-matrix restore. Only the first post hit adds `velocity * -0x1999` to car `+1f0..1f8`; local offset cross local drag enters `+1fc..204`. At `40caf0` (C15807–15875) the former is added after force/inverse-mass multiplication, before the `0x3333` and time-step factors; the latter gets mass/inertia scaling. Native Motion queues both for the next integration.
- Signs: `45d4a0` tests either saved endpoint. Below speed `0x8001` the sign enters hinge state3; `45b010` rotates and clamps it to the ground. The source copies its X basis into all three temporary rows before the pivot translation; native keeps that aliasing. Faster hit hides the sign hierarchy, marks it consumed, requests `417cd0` fragments. Grid reset restores sign pose and eligibility.
- Ground: `HpcSurface::prop_sample` = separate `42e9f0` operation: normalize both triangle edges, normalize their cross product, solve fixed plane height (normalizing ordinary HPC plane ratios would change rounding). Missing coverage: `PropFrame::missing_surface`.

## Audio and fragments

`24860` (C32686–32797) supplies event-to-sample assignments and the shared spatial/camera mix.

| Contact | Event kind | Sample member |
| --- | --- | --- |
| Cone hit, `45c6c0`, C71548 | 6 | 45, `CONE.WAV` |
| Weak post/sign hit, `45bc50` / `45d4a0` | 18 (`0x12`) | 65, `CONE.WAV` |
| Post return, `45a460`, C69626 | 19 (`0x13`) | 65, `CONE.WAV` |
| Strong post hit, `45bc50` | 20 (`0x14`) | 65, `CONE.WAV` |
| Sign fragments allocated, `417cd0`, C24374 | 21 (`0x15`) | 66, `SIGN.WAV` |

- Cone strength keeps the signed approach dot product; nonpositive/quiet events follow the original sound threshold. The initial small sign contact calls audio before setting its local-player flag, so the first event in a sector uses the false flag; later small hits in that candidate loop keep the assigned flag. Post return also uses false.
- `PropFragments` implements `417cd0` allocation and `416150` updates: a sign requests at most 37 particles from a 63-entry pool. Event21 only when capacity exists (the contact consumer does not duplicate it). Shared emission ordinals keep source order when a sign break precedes another prop sound in a neighboring sector. Initialization consumes the original random-table draws; contact/prop updates consume none. Triangles use original shapes, colors, velocities, gravity, drag, lifetime.
- Gamepad vibration follows new local prop contacts via `PropResponse::impact_strength_q16`, independent of audio flags and fragment allocation: cone approach strength, first post-contact strength, weak-sign speed (bounded); strong sign = full. `Motion::apply_prop_response` keeps the signal for the current tick through ground correction, then clears it. Adds no vehicle force. Opponent prop hits, returning posts and moving debris do not shake the player.

## Limits

These checks do not establish an original
executable matched replay or contacts for opponents whose simulation is pending.
