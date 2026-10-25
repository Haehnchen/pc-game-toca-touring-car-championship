# Complete track driving acceptance

All ten canonical tracks complete a measured lap with Honda 0, automatic transmission, in
dry, rain (menu weather 3) and snow (menu weather 4); native cases use original detail 3,
640×480, fixed 25 Hz simulation, private Xvfb, dummy/offline audio. Original archives
read-only; native reports record archive and POINTS CRC32.

## Method

- CPU case: the original unfinished-rollout AI as an explicitly labelled test input driver.
  Native cases select tracks through Extras and the ordinary menus, complete the automatic
  approach, then supply arrow actions through the diagnostic step bridge. Both keep actual
  Motion, wheel/HPC/barrier state and Session continuous: no teleports, state restores or
  physics/AI changes. Each lap time must equal its own uninterrupted 40 ms updates.
- Each measured lap visits all POINTS in forward order, awards exactly one lap, keeps
  supported ground coverage, zero barrier contacts, zero motion boundaries. Native checks add:
  player ownership, exact live HUD timing, active engine voices (offline sink), original
  track-specific CD selection, pause/capture, clean menu/music exit.
- Weather runs additionally check one weather/surface update per 40 ms step: rain keeps 90
  particles, snow 25, positions change throughout; both exercise the original wheel trails.
  Rain plays wet rolling sample 81 with nonzero volume; snow keeps SOUNDDRY and must not
  produce the wet voice. Tyre slip is checked against the source `0x4ccc` threshold with audible
  skid playback. Rain and snow share source friction profile 2 and AI weather tables (CPU
  trajectories match apart from weather columns). Lavaland has no precipitation: source
  `4512b0` keeps resolved weather 1 for both requests.
- Reports/trajectories carry requested and resolved weather; artifact names keep the weather
  suffix. `native-full-lap` keeps its default cloudy Donington path.

| Track | Ordered POINTS | CPU lap dry (s) | Native lap dry (s) | CPU rain/snow lap (ms) |
| --- | ---: | ---: | ---: | ---: |
| Donington GP | 254 | 99.96 | 209.44 | 101040 |
| Silverstone | 212 | 88.00 | 187.64 | 89280 |
| Thruxton | 228 | 75.08 | 194.44 | 75160 |
| Brands Hatch | 148 | 47.20 | 99.56 | 48040 |
| Oulton Park | 158 | 61.44 | 137.52 | 62040 |
| Donington Short | 201 | 73.12 | 166.60 | 84120 |
| Croft | 228 | 83.88 | 175.16 | 85480 |
| Knockhill | 140 | 57.68 | 106.84 | 58640 |
| Snetterton | 200 | 75.36 | 161.20 | 75480 |
| Lavaland | 145 | 60.28 | 81.72 | 60280 |

Native rain/snow laps stay within about ±0.3 s of the dry native lap on every track
(e.g. Donington GP 209440/209240 ms, Lavaland 81800/81400 ms).

## Source facts found during acceptance

- Oulton Park, Donington Short, Croft, Snetterton, Lavaland: the authored qualifying Best
  lies behind the previous route plane, so the CPU run records one initial reverse cursor
  adjustment. `403dd0` handles reverse boundary crossings explicitly; only the discarded
  approach may use it, measured laps stay strictly forward. A test assertion requiring
  forward-only cursor movement in the approach was wrong and removed.
- Lavaland's CPU traversal has 90 updates without wheel contact across approach/lap (airborne
  travel is not a missing-ground failure); Donington Short wet runs show 9 airborne updates and
  visit grass/gravel more often around gates 91–93, no barrier, no map defect.
- Snetterton snow exposed a real consumer mismatch: a body-corner lookup stopped with
  `missing_surface` while the car stayed on the road. Original `42e6f0` keeps its
  caller-owned cached face when `42eed0` finds no containing replacement; the bounded
  cached-plane fallback (HPC/body-corner subset, commit `ce3416b`) lets the unchanged snow
  driver complete 200 gates/4030 updates. The exact corner/cache identity of the failing trace
  was not retained; no original full-lap replay is claimed.
- `pit-reference-motion`: complete lane traversal on the nine real circuits; Lavaland keeps the
  original PITS exclusion. Resource/placement/visibility checks:
  [scene resources](scene-resources.md), [placement](scene-rendering.md#start-placement-and-collision-surface),
  [visibility](scene-rendering.md#scene-visibility-and-sector-order).
- After the HUD resumes normal scheduling before the final pause/capture it may show a small
  current-lap time; the exact crossing row stays zero and the displayed best equals the
  measured lap.

## Reproduction

```sh
make check TESTS='^(vehicle-track-laps|native-track-lap-[0-9])$'
make check TESTS='^native-track-lap-7$'
make check-sanitizers TESTS='^vehicle-track-laps$'
make check TESTS='^(vehicle-track-laps|native-track-lap-[0-9])-(rain|snow)$'
make check TESTS='^native-track-lap-1-snow$'
make check-sanitizers TESTS='^vehicle-track-laps-(rain|snow)$'
```

Native cases choose weather 1 explicitly and unlock every track through Extras; CTest builds the
game once and runs at most two private cases concurrently, 120 s runner bound each. The CPU test
can select one track with `CANONICAL_ROOT OUTPUT_DIRECTORY TRACK_ID`.

## Remaining comparisons

Exact selected-frame comparison covers all ten scenes plus rain/snow
([graphics comparison](graphics-comparison.md)). Remaining car/detail combinations, matched
original full-lap trajectories and complete geometry/material/raster equality remain separate
checks. Opponent behavior, vehicle collisions and race results belong to the gameplay package.
