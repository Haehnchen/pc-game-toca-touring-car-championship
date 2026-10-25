# Qualifying rollout and local actor AI handoff

`443f60` calls `402be0` during qualifying setup, including for the local actor.
The actor follows POINTS under AI control until `421390` calls `402c80` at its
first valid start-line crossing; `448130` then starts measured lap 1 in state6.
After the third measured lap `402be0` takes control again for the finish
continuation. The qualifying branch keeps `game+54=0` (ordinary AI braking); only
a race finish selects the finished coasting regime. The local actor stays in the
full wheel/contact/body path: `415090` selects `40b360` while player byte
`game+80` is valid; the distant AI body `448830` does not replace it.

Handoff: `dynamics+310/+314/+318` = 1/1/0 (direct steering, direct gas, filtered
brake). Transmission `+27c` = mode 0 (automatic, no human pedal-to-reverse).
`game+8=0`; `402b10→412890`'s weather-resolved value goes to all eight tire
parameters. Body, velocity, RPM and pending gear changes stay intact.

## Route and analog controls

- `AiDriver` owns the POINTS prefix and decoded AIBIN driver values. The host
  supplies the local driver record, the three source signed-short distributions,
  weather speed penalty and tire offset from `420460`, and a shared host-owned
  `SourceRandom` for the original `49ff60` CRT sequence. The native seed and this
  subset do not reproduce a complete original process's global random-call history.
- `403dd0` refreshes route targets before controls, also for human-owned actors.
  `Motion::prepare_ai` installs the route consumer on scene entry; the host
  supplies `Session::next_point()` before each motion update. Target cache
  continues in states 7/8. It picks one or two points ahead using source speed 70
  and projected distance 20, with the original refresh condition; route-heading
  updates follow the `403dd0` cadence/refresh gate (no fresh angle every tick).
  Distance projection uses target angle minus `4218e0`'s route-boundary normal
  angle (the boundary before this tick's route advance).
- `4024c0` supplies target coordinates, route speed, sampled speed variation and
  lateral jitter. Half-width normal is rebuilt from the two POINTS endpoints as
  `4019f0` does, without mutating game bytes. Both target-jitter sample groups
  consume RNG even though this actor uses the first route line.
- Fleet diagnostic may import initialized live POINTS, driver rows/distributions
  and the retained target cache: same target/projection consumer, packed lateral
  high words read directly, original 33-vector start-line tables while release is
  active; mode 2 measures distance to the alternate route line. Independent
  route-step comparisons consume one shared RNG in ascending actor order; they do
  not reconstruct traffic, controls or session lifecycle
  ([fleet comparison](single-race-fleet-reference.md)).
- Source replay derives one forward/backward route step and the periodic human
  nearest-route candidate from that data; gate predicate and cursor transition are
  shared with `Session`. Integer-distance candidate scan with strict comparison
  retains ties; distance/lookahead uses the cursor from before nearest-route
  recovery. With checkpoint-timing observations replay also consumes the source
  lap clock, ordinary non-finishing laps and the shared split-time table; `Session`
  and `RaceProgress` use the same lap arithmetic/checkpoint writer. Finish/
  qualifying transitions and autonomous clocks stay outside this comparison.
- One-actor no-pit `4036f0` keeps steering mode 1. `4265b0` normalizes the
  horizontal target delta, uses the 4096-entry quantized asin mapping from
  `478b20`, sign from the XZ cross product. `405530` maps the angle via full-body
  small/large gains `17d/4a7`, speed/distance thresholds, clamp 1. `4051e0`
  supplies the driver's gas while below route speed adjusted by sampled variation
  and weather. Initial approach (`game+54=0`): `4050e0` piecewise braking-distance
  table and `4051a0` decide full brake; `404ea0` releases it above 70 when
  steering exceeds 20 percent. Finished local actor (`game+54!=0`) coasts above
  the desired speed. Wheel forces, engine, barriers, HPC consume the raw analog
  fields unchanged.
- `403dd0` low-speed reverse recovery selects gear 7 when the close target is more
  than a quarter turn behind, gear 1 once aligned. Direct steering/gas: `4190f0`,
  `418cd0`; brake keeps `4197a0` filtering. The 1/1/0 flags are not emulated by
  binary key presses.

## Handoff and handback

- `Motion::handoff_to_ai(false)` starts rollout and the measured qualifying finish
  continuation; default `true` selects race-finished coasting. Both need prepared
  route data.
- First valid line: `restore_player_control()` restores saved manual/automatic
  selection and human filters, clears only the queued gear-input byte `+14c`,
  applies `402b90`'s tire reset. Bytes `+14d..14f`, steering accumulator `+150`,
  body, velocity, gear, pending gear commits and RPM stay. Direct AI gas does not
  advance the unused human gas ramp. Confirmation and elapsed time cannot end
  rollout.
- `apply_controller_handoff` saves steering/gas/brake/transmission modes only when
  the actor is human-owned. Repeated AI handoffs keep the saved modes and the
  reverse-recovery flag, reapply all eight tires, restart the 25-update counter.
  The player launch correction pauses while AI owns controls; decay resumes on
  handback. Source modes `+310/+314/+318` map to network flags 26/24/25
  ([bounded original calls](single-race-fleet-reference.md#local-controller-call-boundaries--2026-10-17)).
- Focus handling is host-owned: no ticks, no AI updates or draws.
  `reset_for_race(actual_grid_body)` reconstructs the original local race
  initialization (car, manual/automatic, weather, borrowed collision resources),
  clears AI ownership, opens the start clutch; it does not reset or move the
  shared RNG.

## Bounds and focused checks

- Qualifying approach (all ten tracks) measured 181–278 updates (7.24–11.12 s),
  18–20 route points, body turns both directions; handover preserves pose,
  velocity, gear, RPM.
- Not claimed: autonomous complete laps and traffic from this approach check;
  traffic avoidance/overtaking modes 0/2..6, opponent pit decisions, remote
  actors, damage-driven pedal suppression, the distant simplified body, post-finish
  race results. Local PITS guidance/control return: [pit lanes](pit-lanes.md). No
  synthetic rivals, body freeze or waypoint teleport. Finish-tick target jitter is
  preserved before classification consumes the shared RNG
  ([integrated acceptance](session-finish.md#integrated-acceptance)).
- Single Race adds nonplayer bodies/traffic via [`Rivals`](single-race.md#single-race-rival-bodies).
