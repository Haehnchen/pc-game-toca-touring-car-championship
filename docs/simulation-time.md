# Simulation time and native scheduling

`simulation/time.hpp` separates the recovered numeric clock from the native host schedule. Unit tests use synthetic counter sequences; the retained startup comparison derives physical steps from actual original QPC observations. Neither independently measures QPF or establishes an autonomous session from authored setup.

## Recovered arithmetic

`00412a80` stores the signed low-32 QPC frequency divided by 25 and sets the first-sample flag. `00412ab0` works on low-32 counter values:

| Condition | Per-update delta | Extra update |
| --- | --- | --- |
| First sample | `65536`, reciprocal `65536` | Existing extra flag retained |
| Raw delta ≤ `0x18000` | Raw delta | No |
| `0x18000` < raw delta ≤ `0x30000` | Raw delta / 2, truncating | Yes |
| Raw delta > `0x30000` | `0x18000` | Yes |

- Raw delta = signed counter difference ×65536 / stored divisor. If the new low counter ≤ the old, the source subtracts one more before the division: equal counters give a negative interval, not zero. A difference above `INT32_MAX` is read as negative and bypasses the positive upper clamp. Reciprocal = signed `2^32 / delta`. Native reports a zero divisor/delta explicitly.
- `XBOOSTME` doubles/caps the final delta at `0x30000` and halves its reciprocal when `DAT_004c5d70==0`; it does not alter the extra-step flag.
- `SourceClock::reset()` follows `00412a80`: resets first-sample ownership, not the retained extra flag. `TimeStep::update_count()` gives one or two update calls; consumers must not halve an already halved delta.
- `0044a370` calls the main update, invokes the state render callback, then samples the next delta via `00412ab0`. The render chain at label `0041f9e5` checks the previous extra flag and calls `0044a1d0` again (polls inputs, dispatches the current state, runs state/audio consumers). This is presentation coupling in the original, not evidence of a fixed 25 Hz physics loop.

## Recorded startup clock transactions

- `--compare-startup-world-reset` takes a required `TOCACLOK` input after its typed startup input. Contracts use the observed signed stored divisor, one explicit initial retained TimeStep/extra latch and external caller-clock samples. `SourceClock::from_stored_divisor` uses the stored divisor directly (no invented QueryPerformanceFrequency).
- Full cold/warm captures: TOCACLOK v3 with TOCACOMB v8 journal. Source `447a77` return supplies one external creation clock `C`; `SourceTimerBank::create(C)` initializes all five origins. The first NoQual wrapper resets slots3/2 with its caller sample `T`, giving `[C,C,T,T,C]`. Snapshot origins are outputs, never inputs. The creator occurs between the actual loader and state9 initializer calls, with source `44a470` return address.
- Ordered primary, external QPC-sample and warm-grid reset operations retain every `412ab0` sample, including result-menu frames with no physics call. Counter = direct external low-32 copy at return `44a516`; pre/post stored steps, differences, divisors and latches stay expectations. A warm `412a80` resets first-sample ownership with its newly observed divisor, keeping extra/delta/reciprocal/previous-counter state; it must not replace the retained step with an observed post-restart value. Each primary frame admits its first physical call and a second only when the generated extra latch permits; menu frames may have none. The physical CSV checks consumed timing and lifecycle outputs.
- v2 recordings keep their explicit five-origin context and bounded sample/extra contract; they cannot supply the missing creator or overwritten menu samples. Both contracts reject imported later timing words.
- The retained timer bank owns countdown/release, deferred state9→6 and later lifecycle transitions. Source green expiry runs after primary POST before an optional extra update; native play evaluates it once per fixed deadline. The state10 wrapper clears the raw phase word `4c3a7a` before every physical pass (`448c33`), including extras; `SourceSessionStart::finish_wrapper` supplies that writer to native play and replay; timer origins, clocks and release latches stay retained. State13 keeps the cleared phase.
- Startup64 (`92f72cfc`): 32 QPC samples, 31 extra reuses; 189 later timing values generated, 3 initial values observed context. Covers first-sample and clamped positive steps; synthetic tests cover split, ordinary, wrap/equality and retained-extra arithmetic. Nine lifecycle fields add 576 unique checks to 192 step checks, covering phases0..4, green release, first state6 entry. This v2 interval keeps its initial caller-step/origin context; it does not prove the new creator/menu sample contract or complete race parity. External callback scheduling remains observed.
- Debugger pauses affect recorded counters, so they are not native deadlines; prefix verification runs in the background (no multi-minute pause in the observed source).
- Full creator/sample profile: 3,744 continuous updates from cold startup through all finishes, results, actual RACE AGAIN and the next green, zero imported initial origins. Closed source prefix `38155171` under `build/test-artifacts/full-race-clock-async/lifecycle-prefix/`; evidence `build/test-artifacts/fleet-startup-lifecycle/`.
- `SourceSceneFrame` reuses the same clock owner: primary physics → generated scene publication → optional extra physics → next external sample. The joined normal NoQual profile verifies three frames with physical counts1/1/2; body states and initial roots remain explicit inputs. [Frame boundary](scene-rendering.md#generated-normal-view2-frames).
- Joined `--compare-startup-scene` uses the generated physical owner instead of recorded bodies/roots. Its v3 clock stream includes the final complete frame sample at sentinel ID=update count; native replay consumes it after final POST without changing that physical update's timestep. Primary/QPC observations have one owner shared with the combined journal. [Joined scene](scene-rendering.md#joined-physical-and-scene-replay).

## Declared native adaptation

- Fixed **25 Hz** deadlines, 40,000,000 ns apart: a native scheduling decision motivated by the clock's 25-unit scale, not a recovered tick-frequency claim. `LiveSchedule::reset(now)` schedules the first tick one interval later (no immediate update). `take_due(now)` returns up to eight ordered deadlines and keeps all outstanding debt. The host drains due batches before mixing the remaining audio interval; `next_deadline_ns()` lets it wait for the earlier of a simulation tick and a presentation deadline.
- Presentation caps and VSync do not select simulation deltas. Timestamped input takes effect at its first eligible deadline (never retroactively across ticks after a stall). Gear edges belong to one update; held pedals/directions continue. If a source delta requests an extra update, held state remains and the first update's native edges are not replayed.
- Focus loss pauses scheduling and clears held actions; resume resets the deadline boundary (the gap is not catch-up time). Genuine presentation stalls keep their deadlines. Offline audio advances to each deadline before new engine controls take effect, then to current host time after due ticks.

## Smooth presentation

- Rendering interpolates the previous and current 40 ms snapshots for local car, opponents, ghost and camera, incl. both split-screen players. Positions linear; rotations normalized quaternion interpolation; wheel angles follow the original 4096-unit wrap. Cameras interpolate their world pose before inversion. Physics, contacts, visibility registration, inputs, audio and lap timing consume only authoritative tick states.
- Tick snapshots contain poses; a scene-owned presentation result keeps weather, surface-effect and skid-mark buffers across draws and warm restarts. Destination-writing effect methods reuse capacity and replace all output, incl. empty/reset frames; skid presentation builds directly into its destination.
- Adds one simulation interval (40 ms) of presentation delay, no physics prediction. Pause/focus, restart, viewport change and camera cuts reset snapshot history; ghost lap wrap snaps to the new attempt. Diagnostic `--step-input` keeps exact tick pictures; trace columns `presentation_alpha`, `presentation_body_x/y/z` expose the rendered car position (world units); the simulation trace is unchanged.
- Tests establish arithmetic and schedule ownership; they do not claim complete original session/physics recovery.
