# Original vehicle recordings

Schema-2 16-car Single Race capture and its two physical phase comparisons:
[original fleet comparison](single-race-fleet-reference.md). The schema-1
local-vehicle workflow below is a separate diagnostic.

## Tracing

`tools/trace-reference.py` observes the unchanged TOCA 1.1a executable (CRC32
`8ce56d41`) through the Wine runner and `winedbg`: private Xvfb display, existing
reference lock, muted. Records, debugger logs and comparisons stay under ignored
`build/test-artifacts/reference-trace-*` (not distributable). Attachment requires the
launcher's post-lock marker and one same-user game process with this invocation's exact
private display, runtime directory and Wine prefix.

```sh
make build BUILD_TARGETS=toca-reference-replay
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tools/trace-reference.py --scenario shuttle --updates 500 --timeout 300 \
  --replay build/dev/toca-reference-replay --replay-mode sequential
```

- Software Vulkan override is the tested local setup. `--runner` selects a Wine runner,
  `--home` its cache/prefix location.
- The driver follows the real frontend with Enter and waits for the automatic qualifying
  approach to return player control (recording starts only after `421390→402c80`
  returns player transmission control, also in Time Trial). Header: skipped approach
  update counters, car, track, weather, transmission, steering profile. Requested
  desktop keys are separate from input values the game consumed.
- Scenarios: `coast`, `accel`, `accel-turn-brake` (thirds of the update count);
  `shuttle`, `slalom-shuttle`, `offroad-shuttle`: brake the moving car, then choose
  keyboard inputs from the observed pose; reverse at targets 12 m either side of the
  anchor, release the pedal above speed `0x8000`; steering pulses keep the maneuver near
  its initial area. Test driver, no physics change. Header records parameters, each
  update its decision, footer actual movement/braking/reversing/surface coverage; long
  captures must pass coverage checks (no stationary padding). Off-road variant first
  drives onto grass, confirms grounded grass wheels, then anchors the shuttle there
  (entry phase and new local frame logged; car never repositioned).
- `--weather sunny|cloudy|rain|snow|storm` selects the Single Race weather row by
  keyboard and verifies consumed menu values and resolved scene configuration (default
  `current`). `--session time-trial` navigates RaceMenu and TimeTrial options:
  `44f0a3` allocates three car/ghost records, but `421980` ground-completes only actor 0
  and `460070` disables rival collisions → one active ground actor for isolated
  roughness-cursor comparisons (header records allocated and active counts). Native
  Time Trial: [Time Trial](time-trial.md#time-trial-core).

Re-compare an existing recording without Wine:

```sh
python3 tools/replay-reference.py build/test-artifacts/reference-trace-EXAMPLE/trace.jsonl --mode sequential
python3 tools/replay-reference.py build/test-artifacts/reference-trace-EXAMPLE/trace.jsonl --mode independent
```

## Boundaries and integrity

Each schema-1 JSONL update records three boundaries:

| Boundary | Meaning |
| --- | --- |
| `421aa0-entry` | Before session input/AI mapping. |
| `415090-entry` | After mapping; before `40b360` controls, wheel forces and body integration. |
| `421aa0-return` | After the separate `415240` ground-contact completion. Return address read from that invocation's stack. |

- Snapshots: little-endian body (`0x53c`), dynamics (`0x338`), gameplay (`0x124`)
  records. Decoded inputs: four signed 32-bit Q16 axes + signed 8-bit gear edge. Timing:
  actual delta, reciprocal delta, extra-update flag, QPC sample/divisor/difference,
  source update counter. RNG, limiter state and rough-surface cursor recorded
  separately. Header: configuration blob and executable identity. Footer: update count
  and CRC32 of all preceding JSONL bytes.
- Reader rejects incomplete records, wrong widths/layouts, inconsistent decoded fields,
  changed configuration, missing boundaries, checksum failures. Consumed snapshots are
  converted to a small little-endian binary stream (C++ reader has no JSON dependency).
  `native-MODE.csv` and `comparison-MODE.json` keep both comparisons separate and report
  exact integer differences plus consecutive matching updates before the first
  divergence. Exit 0 = all selected fields match, 1 = completed comparison differs,
  2 = Python reader could not validate/replay.

## What the comparison establishes

- Sequential replay (default) imports the first consumed snapshot once (filtered
  pedals/steering, pending gear, engine/wheels, support flags, tire offsets, queued
  force/moment); later updates supply only recorded four input axes, gear edge, time
  step. Vehicle, filter, limiter, contact and rough-surface state stay native.
  Configuration, ownership, clutch and tire-parameter changes outside the boundary are
  rejected, not copied. Track barriers and canonical map props are active; props
  advance persistently in normal-driving order (initialization assumes no earlier actor
  changed the canonical props before the interval).
- Independent replay imports each snapshot separately (single-update diagnosis), with
  barriers but without prop-controller state. The replay adapter imports the recorded
  global roughness cursor once before the first sequential update, or before every
  independent snapshot; sequential input updates never copy it back
  ([shared surface](single-race.md#shared-rough-surface-sequence)). Motion snapshot import changes
  only the vehicle.
- Both modes call the same native controls/motion consumer as `make run`; already
  consumed keyboard mapping and launch-offset decay are not applied twice; one original
  invocation = one native update (extra-update flag does not duplicate it).
- Sequential equality establishes state propagation for the recorded interval;
  the complete-lap case below has its own coverage. Debugger pauses alter QPC and often trigger the
  capped delta/extra-update path; no controlled timing injection. Rival collisions,
  shared actor effects and complete session state are not imported (consequences
  appear as differences). Reports expose world scope and every original vehicle contact
  (`body+529` count, `+52a` partner IDs) and observable external roughness-cursor
  advances modulo 15 (not fed back). Reader requires local keyboard filters and a player
  transmission mode, not AI ownership. No numerical tolerance hides differences. The
  reader compares 49 values per update (incl. both visual angles).

## Accepted short recordings

Donington, car 7, cloudy, automatic, steering profile 2; 4/32/64-update recordings
match exactly across all compared values (no boundary error). The 64-update capture:
21 gas, 21 gas/right, 21 brake, 1 initial no-input update; 156 asphalt + 100 grass
wheel samples; deltas 64946 once, 98304 otherwise (debugger pauses). The comparisons
exposed the brake-ramp, negative-product projection, x87 transform rounding and
simultaneous-axis stabilization errors in [controls](vehicle-motion.md#local-keyboard-and-drivetrain-consumers) and
[motion](vehicle-motion.md#original-recording-corrections).

## Continuous recording follow-up

Weather dispatch: original `4512b0→42e440→4128d0` selects profile 2 for rain/snow,
profile 1 for storm, profile 0 for sun/cloud (the earlier wrong mapping diverged at
rain update 7).

Three continuous 500-update Donington/car7 sequences match all compared integers with
one state import each; movement/braking/reversing gates pass, the off-road scenarios
also require grounded grass contacts; no rival contact or external roughness advance in
these intervals:

| Weather / session | Scenario |
| --- | --- |
| Sunny / TimeTrial | Grass entry and shuttle |
| Rain / Single Race | Slalom shuttle |
| Snow / Single Race | Grass entry and shuttle |

Rain's footer counts 146 off-road updates; the final grounded-wheel rule excludes two
airborne updates (recording unchanged).

Establishes captured one-car propagation and asphalt/grass transitions. Other cars,
collision-heavy intervals and the complete multi-actor world keep their separate
boundaries. The original debugger timing caveat still applies. Isolated dry route:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  python3 tools/trace-reference.py --session time-trial --weather sunny \
  --scenario offroad-shuttle --updates 500 --timeout 240 \
  --replay build/dev/toca-reference-replay
```

- First dry 500 recording matches 445 updates before a collision with rival 2:
  `428820→428aa0→428b50` writes its partner ID and queued force/moment into the
  captured body. A 500-update snow pilot matches 399 updates before rival 2 contact (its
  off-road coverage gate also fails). Both are diagnostic evidence only, differences
  reported, not accepted as sequence parity.
- Sunny off-road recording: 459 continuous updates match, all 500 independent. At
  updates 393–395 other actors advance the shared roughness cursor by 1/2/1 while the
  local car is below `0x8000` (no advance); its next fast grass contact at 459 exposes
  that. `42e6f0` consumes the cursor for grass/gravel above that speed, `421980`
  ground-completes all active actors. World-state limit of the one-car replay; the
  shared production context fixes native fleet ownership but does not reconstruct
  unrecorded actors. Independent equality diagnoses the isolated update, not
  continuous fleet parity. All reports preserve external advances and mismatches.
- Native control acceptance selects Silverstone through the UI: its start straight
  supports the acceleration/gear-change/braking assertions (straight acceleration at
  Donington's curved handover reached the HPC edge before the F key).

## Matched original full lap

`capture-render-reference.py --tt-lap --tt-save-best` drives a genuine original
Time Trial through the existing keyboard driver and records the same three
boundaries. The original lifecycle trace remains intact. A separately identified
human-only interval excludes the automatic-approach transmission change, retains
source update identities and has its own checksum/footer; no values are repaired.
Renault 7/Donington/sunny: 3,647 continuous native updates, 49 fields per update,
zero differences, all 254 ordered POINTS, best 240,565 ms; trace CRC32 `3a9bb91e`.
Both ghost owners are captured from the original and its own saved best stream.
[Route/timing scope](full-lap.md#scope), [rendered-state comparison](graphics-comparison.md).

## Shared rough-surface follow-up

`42e6f0` consumes one process-wide 15-entry height sequence for grass and gravel when
the speed record is strictly above `0x8000`, visiting all eight sample points
(incl. unsupported upper corners). `415240` full-body dispatch supplies actor order;
distant and crash settlement use `42e9f0` and do not advance it. Native
`SurfaceRoughness` has this shared ownership and survives car, grid and scene
transitions ([source and lifetime](single-race.md#shared-rough-surface-sequence)). A physically
integrated car crossing the HPC domain edge rolls back when its third sample fails; its
two earlier draws stay consumed and the next actor continues after them. The recordings
establish the bounded one-car intervals above; complete original/native fleet replay
remains open.
