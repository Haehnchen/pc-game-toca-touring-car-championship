# Local Single Race start and lap records

The local session consumer uses POINTS/LOWPOINT route boundaries and active scene
milliseconds. Qualifying and the no-qualifying grid start are different source
paths. No universal countdown or synthetic lap/time values for free driving.

## Clock and grid release

- `44f0a3` selects phase `+5f6=1`, state `+5f8=4` for qualifying, else phase 2,
  state 9. `44a1d0` refreshes game time `4d3840` and timer time `4eda6c` from the
  millisecond clock `4a0580`; `44a170(slot)` stores that time. `44a190` first
  requires unsigned `origin<clock` (else 0); subtraction/multiplication wrap at 32
  bits before division: `((delta_ms*16)/1000)<<12`; threshold comparisons are
  signed. Timer outputs advance in 1/16 s steps. Native host supplies cumulative
  active milliseconds from its 25 Hz deadlines; focus pause adds no time.
- `44ffe1`/`421290` initialize timer2. No-qualifying update `448790` calls `421f40`
  before the physics chain; ordinary local Single Race has no network readiness
  gate. Each strict `timer2>0x30000` advances `4c3a7a` once and resets timer2.
  Phase 1: no sound; phase 2: `425850(0)`; phase 3: `425850(1)` and `448130`
  (records the lap-time origin, selects pending state6). The outer dispatch
  commits it on its next entry; a green extra update still runs state9.
  `SourceTimerBank`/`SourceSessionStart` share this arithmetic with the replay.
  At native 40 ms deadlines transitions occur at 3080, 6160, 9240 ms (not 3000/
  6000/9000); no multi-phase advance in one late call. `41f9xx` hides phase 3 after
  timer1 strictly exceeds `0x18000`; the native session caches this as phase 4
  independent of rendering (green expiry at 10840 ms). The HUD owns lamp artwork/
  layout.
- Grid hold is a drivetrain input, not a frozen world or ignored pedals. `460170`
  ends in `401320`: dynamics `+148=1`, gear 0; `40d5f0` consumes the open clutch.
  State6's first update (`4482d0`, call `448411`) calls `401260(0)` to reconnect.
  Green keeps state9, gear 0 and held clutch; the next deadline enters state6,
  resets timer0, reconnects the clutch for both local players. Launch scalar and lap
  origin are installed at green, before reconnection. `401260` selects gear 1 for
  automatic (`+27c=2`); manual (`=1`) keeps neutral/preselected gear.
- `448130` also initializes the local-player array `4c5d80[player*4]` from RPM and
  starts `4d37b0=360`. The former is consumed by `422f60` as a temporary
  tire-parameter adjustment (not the dynamics throttle ramp); see
  [controls/forces consumer](barrier-contact.md#noqual-clutch-boundary) (also the
  original start wheel-brake force). `4d37b0` stays 360 at green and decrements
  before each following state6 AI/route update.

## Original route and lap consumer

- Local caller of `421390` is `403dd0` (call site `404407`), before new physics.
  `43be20` processes network records and is not used locally.
- `402910` mode 1: directed horizontal boundary from POINTS fields 0..2 and 9..11
  and body `+bc/+c4`: two independently shifted 16.16 products, wrapped 32-bit sum
  selects side 1/2. The session decodes just those two vectors per 64-byte route
  record, bounds the prefix, keeps no spans into temporary archive members.
  Donington KEEP has 254 records.
- `4019f0` builds six ordered checkpoint indices `k*floor(count*0x2b85/65536)`.
  `403dd0` advances at most one target per update, wraps the route index, rewinds
  the previous/current pair for backwards travel, suppresses a backwards start-line
  bounce. Only index 0 at the expected checkpoint calls `421390`. Directed-plane/
  order test, not nearest-point distance. Pit diversion, recovery/teleport records
  and alternate route paths are outside this consumer.
- Qualifying placement `443d30`/`443f60`: previous=`count-18`, next=`count-17`,
  expected checkpoint 0. First valid start-line crossing: `421390` discards the
  out-lap, resets best time, sets measured lap 1, calls `448130` (state6).
  `443f60→402be0` gives AI the approach; `421390→402c80` restores saved player
  controls before the next body update. Elapsed time/confirmation cannot release it;
  no red/go cue.
- No-qualifying reset in `460170` writes current/previous 0 and expected checkpoint
  1 (`4cc5ba..4cc5be`): the first grid-to-line passage completes no lap; race timing
  begins at release.
- Subsequent complete circuits cache lap milliseconds, best/last and the next
  displayed lap number (original displayed short wraps at 100). Completed-lap count
  is a native diagnostic. [Local lap/time HUD](session-hud.md) consumes these
  records. Qualifying finish, classification and result rendering:
  [qualifying finish](session-finish.md); split/rival records remain open.

## Recovered finish contract

- `421390` increments the lap record at an ordered start-line crossing, updates
  best. Qualifying phases 1/3: first crossing discards the out-lap; finish after
  three measured laps (incremented lap 4): `402be0` AI handoff, marks
  `4c39c8[player]`, state7 once every local player finished. No body freeze, no
  fabricated opponent rank.
- State7 `448450`: resets timers 0/3, stops CD via `47a305` when enabled, copies
  local best-time fields, calls `444270(0)` (generates missing opponent qualifying
  times with RNG, sorts the field). `426400` stops sample voices; `4058e0` starts
  sample 83 `LOOP2.WAV`, pan 64, total plays 0, gain `4ad068→4ad064`, owned handle
  `4bd94c`; `405960` stops it. Not a victory one-shot; role not inferred from the
  filename. State7 continues `421aa0` autonomous physics. After 2 s `448580`
  changes the displayed camera actor via `45ed30`; input after strict timer0>1 s or
  automatic timer0>6 s requests state8. A complete consumer needs AI handoff,
  qualifying classification, camera and state8 continuation together.
- Race phases 2/4 finish when the incremented lap exceeds the limit: `421390`
  restores displayed lap to the limit, records total centiseconds, `402be0` AI
  handoff, finish-order fields, `41a820(0,player)` hides HUD, then classification/
  record consumers `4220c0`, `422720`, `422880` before state10 (fields include
  other actors; a single native car must not imply a race position).
- State10 `448a20`: `425950`, `426400`, `47a305`, then the same sample-83 loop via
  `4058e0`. Speech `44cb90(9)`/`(10)` only for Championship mode 1. Continuation
  requests state13 depending on result/classification. Dry/wet sample 83 files:
  338166 bytes, CRC32 `7bb14f00`.
- Qualifying handoff, states 7/8, sample 83 and classified grid restart are
  integrated ([qualifying finish](session-finish.md), [AI handoff](ai-driver.md)).
  Race state10 and its later result consumers still require actual rival records.

## Audio and lifecycle

- `425850` plays SOUND member 69 `STARTER1.WAV` (red) and 70 `STARTER2.WAV` (go),
  once, volume 60, pan 64, single sample handle. `audio::Start` loads spans from the
  resolved full-memory dry/wet archive during Loading; `start()` arms,
  `update(StartCue)` plays; a later event replaces its own preceding cue; the
  playback serial protects a foreign playback reusing a finished slot; `stop()`
  handles focus/exit without replaying on resume. Linear native gain is the SDL_mixer
  adapter, not a Miles PCM-fidelity claim.
- `44a6f0` audio-only State9 branch: RPM = `max(2000,(rawRPM/5)*4)`, sound gear 1,
  sound speed = `min(260,adjustedRPM/100+100)`. `session_audio_inputs` reproduces it
  for motor and WHIRR consumers, preserving throttle; raw actor/RPM/gear/HUD inputs
  unchanged. Explicit original pre-start audio convention, not body motion. State4/6
  keep raw audio inputs.

## API

`simulation::Session(route_bytes, qualifying)` owns the decoded route prefix.
`update(elapsed_ms, position_q16)` returns ordered red/go/released/lap events; call
before Motion with the old body position (as `421f40`/`403dd0`). Getters: state
4/9/6, lamp phase 0..4, release, target point, lap records. Re-entry constructs a
new session; active time must stay monotonic; cumulative ms input avoids rounding
a Q16 delta per tick.

## Integrated acceptance

Start sequence checks: neutral/open clutch with zero wheel drive, rising raw RPM,
State9 WHIRR, cue 69 red, focus pause without timer advance or cue replay, cue 70
green, first-gear acceleration; phase boundaries 3080/6160/9240/10840 ms. Actual
complete-lap driving and matched original gameplay/audio recordings remain
unaccepted. Qualifying finish checks:
[its acceptance](session-finish.md#integrated-acceptance).
