# Local qualifying finish

## State, clock and handoff

- `421390` discards the qualifying out-lap, finishes after three measured laps
  (actor lap 4): updates best, calls `402be0`, increments the local completion
  count, requests state7. Unlike a race finish it keeps `game+54` (ordinary AI
  braking). Native `Session` emits `lap_complete`, then `qualifying_finished`, once.
  Root performs the AI handoff; the vehicle continues through the normal
  wheel/body/ground pipeline. Route targets keep advancing for the AI; `421390`
  excludes state7/8 from further lap updates. Race-finish classification is a
  different consumer. The qualifying handback clears raw steering and its angle
  through the shared `421390` caller consumer, keeping filtered steering.
- State7 (`448450`/`448580`): reset timers 0/3, copy local best into qualifying
  records, classify, stop CD and sample voices, start owned sample 83. Before
  timer0 reaches 2 s render `41f520` with the driving camera and HUD; at
  `timer0>=2s` authored camera, gauges hidden. `41eea0(case7)` shows GSTRINGS line
  52 "QUALIFICATION OVER" centered at normalized y `0x370a`. Input accepted only at
  strict `timer0>1s`; automatic continuation at strict `timer0>6s`; both enter
  state8. Native active ms pass through the original 1/16 s quantization; focus
  pauses add nothing; transitions cache at simulation deadlines.
- State8 (`4485c0`/`448750`): AI motion and authored look-at camera. `45ed30(case8)`
  draws the qualifying table: authored 640 canvas shows all 16 entries (the original
  low-resolution paging path is unused). Confirm → state9; without input
  `timer0>=40s`. Root reconstructs the race session and grounded grid pose using the
  classified local rank, resets driving camera and wheel phase, starts the ordinary
  held-clutch start. No frozen car, inspection camera or synthetic rank.
- Pause action `441720` can enter state7 before three laps: keeps the best completed
  lap; if minute/second fields are both zero assigns 60:00.00. Classifier and result
  view accept that sentinel (unmeasured player last); no unfinished lap counted
  ([pause menu](pause-menu.md)).

## Original qualifying records

- `420460` reads CONFIG member 0 AIBIN version 3. Ten fixed 10-byte car names at 4;
  weather parameters at 104/108/112/116/120. Single Race selects one of three
  16×40-byte driver banks at `124+(24+difficulty)*640`: name bytes 0..15, car
  identity byte 16, qualifying ability byte 18 (0 maps to 1). Local actor =
  `car_id*2`; the entered name replaces only that driver's name. Raw 40-byte records
  stay available to the AI. Counts at 17404/17408/17412 bound the three signed-short
  distributions starting at 17416 (`4c3ed8`, `4c3e10`, `4c3fa0`); no zero-jitter
  substitute.
- `444670`/`444730` select CONFIG member 1 QUALPC bounds. Normal Single: u16 index
  `48+60*difficulty+6*track+weather_group` selects min/max seconds; both ×100.
  Weather offsets: 0 for 1/2, 4 for 3/4, 2 for 5 (`00444670`: rain group 2, snow
  group 1; `00444730` adds two shorts per group). All ten tracks, incl. Lavaland's
  forced dry weather; e.g. Thruxton medium/default 6800..8200.
- `444270` per nonlocal actor centiseconds =
  `min + floor(ability_fraction*(max-min)) + random(1..floor(variation*min))` with Q16
  tables `4aee40`/`4aee70`. Local time = measured best truncated to centiseconds.
  `SourceRandom` implements `49ff60`; `44cb80` takes the next 15-bit result modulo a
  positive bound, plus 1. One draw per nonlocal actor in actor order. Stable
  ascending sort keeps actor order on equal records.
- Native uses the ordinary PC branch; the source's config+8==1 time scale is not
  selected. Rankings come from the formula, not simulated laps or invented names.

## Camera, table and audio

- `426b10` chooses the nearest CAMERAS actor by whole-unit horizontal distance, keeps
  the first tie, caches selection for 30 further calls (native: active simulation
  ticks). `4608b0` publishes the body origin to `52b588`, no target-height offset.
  `26830` builds forward/right/up look-at axes with the original fixed normalization;
  `47c520`'s affine inverse preserves the lookup basis's small scale errors.
  `492040` first divides components by 512 toward zero when any magnitude exceeds
  100 units (kept).
- Table: IMAGE640 fonts 23/24, primary HUD car icons, CONFIG GSTRINGS line 53.
  `45ed30`'s normalized rank/icon/name/time columns, font-height+3 row return,
  negative row gap and panel rectangle are applied in an authored 640×480 canvas,
  centered and uniformly scaled. Names uppercased as in `426570`; time `%d:%02d.%02d`.
  `44f0a3` re-enables kerning via `463ff0` after Loading, so title and rows apply
  `46400e` pair spacing. Titles stay single-line through minimal `464e4d`-style
  tracking reduction when kerned width reaches 640 (avoids `464935` word-wrap pushing
  the 16th row out of the canvas; text and font unchanged). Exact old D3D font/raster
  blending is separate from this resource/layout contract.
- State7 stops CD (`47a305`), voices (`426400`), starts sample 83 `LOOP2.WAV` via
  `4058e0` (gain 127, pan 64, total plays 0). State7→8 does not stop/fade it. No
  Championship speech. On entry to 9 `448801` calls `405840(2)`; `405860` fades gain
  by 4 per dispatch, caps 124, calls owned `405960` at zero. After grid
  initialization/physics `4488a7` calls `426ec0` to start the track's CD music. Root
  owns this integration and generation-safe focus/exit cleanup.

## Integrated acceptance

Result fixture uses an authored local best of 123450 ms and the real opponent-time
generator (not a recorded qualifying run). Native three-lap flow: 254 ordered route
points per lap, AI-owned body moves through the finish camera, results, earned
16-car grid with fresh clocks and CD03. Complete original qualifying physics,
classification/RNG progression and PCM equivalence remain separate from this native
integration and the
[bounded original controller calls](single-race-fleet-reference.md#local-controller-call-boundaries--2026-10-17).
