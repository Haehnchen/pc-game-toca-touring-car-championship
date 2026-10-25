# One-car full-lap acceptance

Source: unchanged TOCA1.1a `tourcars.exe`, CRC32 `8ce56d41`; canonical Donington
POINTS/HPC, Honda 0, automatic transmission. CPU case: sun; native player case:
Donington default cloudy (both dry). All-ten-track extension:
[all-track acceptance](track-acceptance.md).

## Route and timing contract

- `4019f0` reads 64-byte POINTS records and assigns six ordered checkpoints.
  `402910` tests the directed XZ plane through each record's two endpoints.
  `403dd0` consumes at most one route boundary per update, also in reverse travel
  (no swept checkpoint loop).
- `421390` discards the qualifying approach, restores player control, starts lap 1.
  Only a subsequent complete checkpoint circuit awards a lap and updates last/best.
  The application checks the pre-Motion body position, so the session event can
  follow the physical crossing by one update.
- Incomplete qualifying lap: at an unqualified start crossing `4044b9..4044c8`
  restarts actor `+c8` while `DAT_004bd934` is set, without awarding a lap. Raw
  current time is written at `404031..40403c`, so it survives that update; the next
  update uses the new origin (600 ms at the crossing, 40 ms next update, 2400 ms
  for the following full circuit, not 3000). Race release timer unchanged.

## Reference laps

- CPU `vehicle-full-lap`: the original unfinished-rollout AI is an explicitly
  labelled test driver after the first start crossing, supplying analog controls
  through normal Motion with its original filters/tire offsets. Body, wheels, props,
  barrier state and Session persist; no teleports, restores or retuning.
  Result: 254/254 ordered points, 2499 updates × 40 ms = 99,960 ms, last==best,
  lap 2/current 0, no barrier contact or motion boundary.
- Native `native-full-lap`: ordinary application, Single Race qualifying, original
  approach handover. A read-only test driver follows authored POINTS `+12` targets
  (test input, never writes vehicle/session state). Default: diagnostic step bridge
  supplies held arrow actions through InputState/TimedActions; `--realtime` keeps
  physical X11 arrow presses. Requires every ordered route transition, uninterrupted
  40 ms updates, player ownership during the measured lap, one completed lap, exact
  best time, matching HUD. Lap duration varies with keyboard scheduling and the
  rollout seed (~209.3–209.9 s, 254 points, no barrier/motion boundary); the check
  asserts its relationship to simulation time, not a fixed target.

## Integrated acceptance

Combined runtime keeps the same player lap (254 ordered points, zero barrier
contacts/motion boundaries, screenshot lap 2/3 with best time). Actual keyboard
scheduling and the original rollout seed vary the lap duration.

## Faster diagnostic scheduling

- `--step-input`: opt-in, requires `--audio-offline` and `--trace`. Menus and the
  automatic approach run normally. At player handover stdout publishes
  `TOCA_STEP N` after the completed update and flushed traces; the test replies on
  stdin with `step MASK` (held arrow and gear actions only, allowed mask `0x30f`).
  Host queues edges through InputState/TimedActions at exactly the next LiveSchedule
  deadline. TimeStep, Session, HUD cache and offline mixer unchanged; no update
  skipped or enlarged. Python helper rejects missing/repeated/skipped acks and
  verifies the consumed mask.
- Bridge also accepts qualifying finish/result states 7/8 (third lap through real AI
  handoff, camera, result timers; `native-qualifying-finish` covers the earned grid
  restart).
- Diagnostic clock waits for input without counting host delay as game time. Every
  eighth step is presented, plus the first step and lap transitions; other frame
  traces mark `presented=0`; physics, HUD, audio always run (presenting every step
  stayed GPU-bound). `live` releases driving keys and reanchors host and SDL-event
  time to the logical epoch; ordinary runs keep the real clock.
- `native-full-lap` default uses this bridge (90 s bound). `--realtime` keeps the
  physical X11-key flow (use the isolated runner with 470 s timeout).
  `native-controls` checks real keyboard, focus, stall catch-up, normal deadlines.
- `native-time-trial-driven` uses the same bridge: [Time Trial acceptance](time-trial.md#accelerated-driven-acceptance).

## Scope

Matched original Time Trial: Renault 7, Donington, sunny, genuine keyboard inputs.
The complete measured lap keeps native Motion state across 3,647 updates: all 49
compared fields match exactly, with one initial import, no vehicle contacts or
external roughness advances. All 254 POINTS are crossed in order; last/best time is
240,565 ms. The retained native Session also matches route, lap, best time and
cached display components over all 3,793 approach/lap updates. Its approach poses
and observed clock are inputs; measured-lap poses come from the native replay.
Trace CRC32 `3a9bb91e`; [recording/replay contract](reference-replay.md).

This establishes the recorded lap and supplied timing, not an autonomous race or
an unperturbed wall-clock baseline. Multi-actor ownership remains in
[the fleet contract](single-race-fleet-reference.md).
