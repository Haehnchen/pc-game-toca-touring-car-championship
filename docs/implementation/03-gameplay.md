# Gameplay, timing and audio

Dependencies use the IDs in the [implementation roadmap](../porting-plan.md);
they name usable recovered contracts, not completion of later acceptance
scenarios. Evidence is from 1.1a, read-only. Shared audio/execution rules:
[roadmap](../porting-plan.md#delegation-and-acceptance-rules). **G08 starts with F02.**

Integrated: local Donington motion, sector-selected ordinary/linked static
barriers, grid/qualifying start and lap records, gauges/start lamps/lap timing,
moving camera/wheels, driving audio, qualifying finish/results. Full
collision/race acceptance remains below.

## Evidence and limits

| Address | Confirmed observation | Still to recover |
| --- | --- | --- |
| `0x0049a240`, `0x0046333f`, `0x00463410` | Window/event entry, startup arguments and frontend state dispatch. | Complete startup/shutdown and state ownership; F05b owns that recovery. |
| `0x00463410`, `0x00469f10`, `0x00469f9b` | Frontend initialization calls the limiter once with `(2,60)`; subsequent iterations wait against that interval and pump messages. | Other limiter changes and call contexts. The `2/60`-second interval is not proof of 60 Hz physics. |
| `0x00412a80`, `0x00412ab0` | Simulation time is normalized against QPC frequency / 25 and stored as Q16. First delta is `0x10000`; large deltas are halved/capped and set `DAT_004be19c`. `XBOOSTME` (`+0x8c`) doubles the delta, capped at `0x30000`, and halves its reciprocal only when `DAT_004c5d70 == 0`. | Exact update order, first-frame flag state, wrap behavior and the gate's complete lifecycle. |
| `0x0044a1d0`, `0x0044a370`, label `LAB_0041f9e5` | Input/state updates, race dispatcher and end-of-frame timing; the render path at the label checks the extra-step flag and calls the update again. The label is not a separate function. | All callers and whether each subsystem runs once or twice. |
| `0x00463910`, `0x004a0000` | Signed 64-bit multiply then shift by 16; x87 `__ftol` explicitly selects truncation toward zero before `fistp`. | Other rounding sites and intermediate precision; decompiler `ROUND` is insufficient. |
| `0x0049ff50`, `0x0049ff60` | RNG seed and 32-bit recurrence `s = s*0x343fd + 0x269ec3`, returning `(s>>16)&0x7fff`; the RNG callee reads no arguments. | Caller signatures/stack setup, subsystem call order and any additional RNGs. Apparent RNG arguments in pseudocode are not evidence of a bounded RNG API. |
| `0x004923e8`, `0x004926cd`, `0x004929cd`, `0x0049355f` | Device dispatch; joystick deadzone rescaling; keyboard action masks; held/press/release query modes. | Game action bindings, steering filters, wheel/force-feedback use and local multiplayer mappings. |
| `0x00415090`, `0x00415240` | Per-car dispatch uses separate runtime records and branches into `0x0040f5c0`, `0x00444ee0`, `0x00445360`; many products shift by 16. | Meanings of car states and contact/handling structures; these are starting points, not established function names. |
| `0x00465711`, `0x004657b9`, `0x004657da`, `0x0046ac35`, `0x0046ac56` | `saves.dat` is `0xf78` bytes; `0x004657da` initializes ten `0x18c` slots. `TOURCARS.CFG` is `0x654` bytes. | Field layouts, time-trial files and validations before claiming save compatibility. |
| `0x0047939c`, `0x0047992c`, `0x00479afc`–`0x00479b3d` | Up to 12 Miles sample handles; file samples, loop count, volume, dynamic pan and playback rate. Pan outside 0..127 becomes 64. | Miles gain/pan curves, channel arbitration and loop-count semantics. |
| `0x00479cfb`, `0x0047a099`, `0x0047a1a3`, `0x00479c92` | Raw playback uses 22050 Hz, sample type `(0,0)`, three stream slots and buffer-ready/refill calls; the last address is cleanup. | Confirm type flags in the existing DLL before treating speech as unsigned 8-bit mono. |
| `0x0047a207`–`0x0047a3b4` | Redbook/CD playback calls exist; the supplied CD ZIP contains audio tracks 02–06. | Recover native track selection, volume, stop/status and delayed restart using the bundled Ogg assets; retain unavailable-media behavior for an actually missing pack. |

## G01 — Session flow and simulation time

Lifecycle in `src/app/`, timing in `src/simulation/`.

Ported: `00412a80`/`00412ab0` arithmetic and the extra-step contract; native
deadlines for controls, vehicle motion, timestamped input, focus resume,
driving audio. See [time consumer and limits](../simulation-time.md). Original
counter/input recordings and complete session-update equivalence remain unaccepted.

- [ ] Extend F05b's call map through race entry/exit, pause/focus, restart and return to menus; name states from callers and original strings.
  Single Race pause, sound options, qualifying skip, restart and return now
  use the recovered dispatch; see [pause menu](../pause-menu.md).
- [ ] Recover every limiter change and QPC delta consumer, including the extra update and `XBOOSTME` timing branch; document update order and gates in one short table.
- [ ] Define the live simulation schedule before G04: update deadlines, the source
  of supplied deltas, input-edge assignment and stall/catch-up policy. The original
  update/render/QPC loop couples elapsed time to presentation; document the required
  scheduling adaptation explicitly while preserving recovered per-update arithmetic
  and extra-step rules. Cap/VSync waits must not select deltas or update frequency.
- [ ] Implement SDL timing and event pumping with explicit original integer conversions; inject recorded deltas into the same simulation entry for replay. Keep W01 presentation limits/VSync separate from game updates, input sampling and audio scheduling.
- [ ] Integrate the recovered session transitions and original pacing into `make run`; keep rendering dimensions under W01/W02.

Initial acceptance: recorded 25/30/60 Hz and long-frame delta sequences produce
the original Q16 delta, extra-step flag and update count.
Replaying those deltas with different presentation caps leaves state and audio event
traces unchanged; rendering extra frames must not advance game state.
Also run the live scheduler with the same timestamped action sequence at
30/60/120/uncapped and supported VSync modes, including an induced presentation
stall. Compare simulation time, update counts, state and audio events at shared
simulation checkpoints under a declared scheduling tolerance. Fixed-delta replay
alone does not prove that the live clock is independent of presentation.
Joint F05b/C01/G08 acceptance: `make run` enters and leaves the recovered race
session state three times and pauses/resumes its clock; transition sounds follow
recovered events. Driving joins G04.

### G01b — Pause menus for the remaining modes

Source `440510` reads mode config+4:1 Championship,2 Time Trial,3 Linkup.
`441720` dispatches the actions; CONFIG GSTRINGS supplies all labels. Network
variants need N01/N03/N04; local multiplayer needs G03 and W02.

- Championship: END CHAMPIONSHIP (GSTRINGS29) replaces END RACE, QUIT
  CHAMPIONSHIP? (125) with NO selected initially; source Yes/No and Escape gates;
  END QUALIFYING SESSION stays in the source session-kind/phase branches.
  Continuation retains the last stage checkpoint; confirmed abandon does not
  save in-lap physics. See [Championship](../championship.md).
- Time Trial: END TIME TRIAL (28), QUIT TIME TRIAL? (123), initial NO; no
  qualifying skip. On confirmed exit `45e7a0` conditionally writes best-run/
  ghost; the write completes before resource release. NO returns to the pause
  root; resume retains timing and current attempt.
  [Evidence](../time-trial.md#time-trial-core).
- Local two-player: shared pause/focus freeze, independent player controls and
  audio state, both-player finish gate, two-row results, RACE AGAIN
  ([split screen](../split-screen.md)).

- [ ] Linkup (N01/N03/N04): implement `441680→43cba0(1)` pause notification,
  `441720→43bed0` receive/timeout processing while the menu is open, and
  resume/leave notifications. Recover host/client authority, peer timeout
  extension and peer simulation rules before applying the local freeze policy.
  END RACE follows its network branch; do not reuse Single Race's restart
  confirmation without a matching caller.
- [ ] Linkup presentation (N04): recover the network player-count/layout branches
  in `440510`, including result rows (player, wins, total time), owning player's
  controls and shared/local sound changes.
- [ ] For remaining Linkup work, A owns mode/record transitions and controller
  actions; B owns localized layouts, per-player input and original audio consumers.
  Main integrates the relevant mode through `make run` and accepts the package.

Acceptance per mode: compare the live original's root, sound and exit/restart
pages and initial selection; exercise Continue, each confirmation choice and
its Escape rule. Verify session time/records and actual save effects, cues and
music transitions, with 4:3/16:9/16:10 captures on muted private displays.
For Linkup, two peers must preserve the recovered pause/leave state, receive
traffic and pause-adjusted timeouts. The [native network scope](04-multiplayer.md#reuse-and-scope-decisions)
ends the baseline session on host loss; original automatic host migration is
deferred. Menu entry must not duplicate cues or disconnect a healthy peer.
Linkup remains open.

## G02 — Numeric behavior and repeatable state

Arithmetic/RNG and state-trace helpers. State-trace integration uses
F05a/G01 contracts and F06b's capture tool; it does not block standalone
arithmetic/RNG work.

- [ ] Define explicit signed/unsigned widths, wrapping, arithmetic shifts, Q16 products/divisions and fixed table layouts from actual uses; avoid C++ signed-overflow assumptions.
- [ ] Inspect x87 conversion/precision sites in bounded assembly; preserve truncation, store boundaries and operation order. Keep fast-math disabled.
- [ ] Port the confirmed RNG and seed/call order; retain original lookup tables instead of replacing them with host `sin`, random engines or GLM physics. Verify callee signatures and calling conventions before translating apparent arguments: at `0x004635c2`/`0x00463607`, pushed `10`/`6` survive the no-argument RNG call and become the second factor of `0x00463910`. Preserve that subsequent scaling.
- [ ] Add synthetic boundary cases and a state recorder with frame/update number,
  supplied delta, RNG and named car fields. Use F06b's proven short capture from
  unchanged 1.1a: record actual input snapshots, Q16 deltas, extra-step flags and
  state at named update boundaries, then feed those recorded inputs/deltas to the
  native recorder. Use the actual computed deltas and record debugger timing
  effects; controlled in-memory time injection is optional and separately identified.
  Keep captures outside Git; do not infer original state from screenshots or
  wall-clock keypress times.

Initial acceptance: signed boundary arithmetic and RNG vectors agree exactly with
original instructions. Joint G01/G04 acceptance: the same supplied input/delta
sequence run three times gives equal native state traces. Declare any necessary
floating-point tolerance before accepting a discrepancy; no unexplained drift.

## G03 — Keyboard, controllers and local players

`src/app/input.cpp`, `src/app/input_devices.cpp`, game action mapping. Depends on
F05a contract and G02 arithmetic; F02 supplies SDL devices.

Integrated:

- Player one arrows and F/V bindings, keyboard profile 2, pedal/steering
  filters, delayed manual shifts, automatic reverse via the timestamped queue
  ([local controls](../vehicle-motion.md#local-keyboard-and-drivetrain-consumers), [motion](../vehicle-motion.md)).
- Local keyboard rebinding and SDL gamepad selection/capture, original analog
  steering/pedal branches, sensitivity, native deadzone settings, timestamped
  delivery, focus/hotplug release, persistent preferences via Game Options
  ([Controller Setup](../controller-setup.md)).
- Both local player profiles: separate keyboard/gamepad bindings, distinct
  instances for identical pads, independent steering/pedals/cameras/feedback,
  focus/pause/unplug cleanup ([split screen](../split-screen.md)). Physical
  two-pad feel is hands-on QA.
- Local Car Boost in two-player options and physical update, sharing `428440`
  with Linkup ([Car Boost](../split-screen.md#local-car-boost)).
- Raw joystick/wheel axes and buttons, centered/end-rest pedal calibration,
  linear wheel steering, saved bindings for both players; virtual twins, focus
  rearm, hotplug share the device ownership boundary
  ([raw device contract](../controller-setup.md#raw-joysticks-and-wheels)).
- `40f5c0` constant/sine FFB consumer through SDL Haptic: original directions,
  magnitudes, periods, cooldown, stop order; capability-aware output; pause/
  focus/device/scene cleanup ([effects](../horn.md#force-feedback-evidence-and-native-boundary)).

- [ ] Recover keyboard precedence, held/edge masks, axis ranges, deadzones, steering/braking filters and per-player mappings from input callers.
- [ ] Accept physical wheel direction/strength and driver compatibility, plus
  the remaining physical two-pad driving/feedback feel.
- [ ] Feed replayed actions through the same mapping boundary. The two-player
  layout is integrated with W02; see [local split screen](../split-screen.md).

Initial acceptance: input snapshots produce recovered steering extremes, deadzone
values, simultaneous actions and gear edges; per-player action values stay
independent. Joint F02/C01/G01 acceptance: keyboard and one SDL controller navigate
`make run` menus, handle focus/device changes and play observed binding/menu sounds.
Joint G04 acceptance adds actual driving and local player layouts; joint G07
acceptance adds saved bindings after restart.

## G04 — One car on Donington: handling and collision

`src/simulation/` vehicle/contact functions.

Integrated:

- Motor moments, wheel/contact forces, body integration, ground correction in
  recovered order; all eight normal cars use recovered initial parameters
  ([motion](../vehicle-motion.md)). Three continuous 500-update
  [original recordings](../reference-replay.md) match all 49 selected fields in
  dry, rain, snow (incl. grass). One sixteen-car NoQual cold→lap→finish→
  warm-restart comparison matches 3,744 updates (Debug, ASan/UBSan); authored
  setup and other mode/context ownership remain open.
  [Continuous scope](../single-race-fleet-reference.md#continuous-cold-startup-and-local-race-timing).
- [Tire-offset writer audit](../vehicle-motion.md#tire-offset-writer-audit):
  writers are initialization, AI, launch, catch-up; no tire-wear consumer, no
  invented degradation.
- POINTS static barrier kinds 0/1, endpoint/body correction, normal rebound,
  delayed torque, base impact sound ([barrier contacts](../barrier-contact.md)).
  Car-to-car impulses/effects: see G06; the retained NoQual world keeps explicit
  initial resource/ownership boundaries.
- Sector candidates from every non-root ground node with clipping, lookup,
  reverse contact order; grid clutch/brake hold and temporary launch tire
  offsets with real engine input ([sector/start](../barrier-contact.md)).
- Linked kind 2/3 entry, persistent actor recovery, secondary projection,
  velocity/basis update, exit; ordinary wall handling outside recovery, distinct audio.
- `460170→402c80→402b90` local-player tire initialization (no allocation grip
  boost); slip/countersteering, dry/wet/snow, road-to-grass mixed contacts,
  powered spin/recovery ([motion](../vehicle-motion.md#grip-and-surface-checks),
  [audio](../driving-audio.md#slip-and-changing-surfaces)).

- [ ] Recover the car/runtime record fields and update callers; separate rendering transforms from authoritative position, velocity, wheels and drivetrain state.
- [ ] Port suspension, tire/road response, steering, engine/gears, braking and integration in original order, constants and numeric formats.
- [ ] Decode track contact queries with C04; port broad/narrow contact, barriers, off-road surfaces and car-to-car impulses from the actual routines.
- [ ] Add one-car qualifying with engine/tire/contact sounds to `make run`; compare acceleration, braking, a turn, curb/off-road and wall-contact sequences against matched original input/deltas.

Acceptance: a car can complete a Donington lap, restart and return to menus.
For at least three 500-update recorded sequences, integer states agree exactly;
any float fields meet their predeclared G02 tolerance. Contact side, normal,
surface classification and impulse agree in the chosen curb/barrier/car cases.
Audio event traces match those contacts and engine changes through G08.

## G05 — Opponents and remaining handling variants

`src/simulation/ai_driver.cpp` and fleet handling in `src/simulation/`.

Integrated:

- Qualifying approach `443f60→402be0` (route steering, normal braking); player
  controls restored at first valid line via `421390→402c80`; finished coasting separate.
- Post-qualifying AI handoff: route speed/jitter, analog controls, weather
  parameters through the full wheel/body path; original RNG shared with scene
  setup and classification ([AI handoff](../ai-driver.md), [Single Race](../single-race.md)).
- 16-driver grid, opponent motor/contact audio, selected difficulty; three
  physical Donington laps repeat deterministically; exact source parity remains G05.
- Passing identities, blocked-leader chains, mode-aware braking, behind-neighbor
  body priority; dormant 0/3/6 selectors and nonplayer pit-entry guards audited
  ([AI recovery](../single-race.md#single-race-traffic-decisions)).
- `42e6f0` rough-height sequence shared by all full-body actors in ascending
  ground order, process lifetime kept through grid resets and scene changes;
  distant/crash-settlement ground does not consume it
  ([shared surface](../single-race.md#shared-rough-surface-sequence)).
- Original evidence: all sixteen actors at five common update boundaries,
  CRC32-protected raw state; per-update imports versus one initial import plus
  observed AI controls/dispatch (neither proves autonomous AI parity); cone/
  barrier contacts, full/distant transitions, damaged grass interval, barrier
  torque rounding, detached reset/reuse ([fleet comparison](../single-race-fleet-reference.md),
  [warm-world](../single-race-fleet-reference.md#recorded-coverage-facts-per-interval-type)).
  Full race/RNG parity remains open.
- Local launch correction from retained RPM and lifecycle events, independent
  of clutch release; cone64 recording matches through decay/reset
  ([launch](../single-race-fleet-reference.md#local-launch-boundary--2026-10-17)).
- `414e20/414ef0` body selection shared with native rivals; dispatch derived
  from retained physical state; route/control inputs remain observed
  ([body selection](../single-race-fleet-reference.md#body-selection-boundary--2026-10-17)).
- 128-update interval with vehicle contacts, mesh damage, rough-surface draws in
  both physical modes; warm damage and numeric detached slots imported; delayed
  contact-moment scaling verified. The recorded pool stayed empty, so original
  detached-flight parity remains open.
- One physical fleet lap on all ten tracks, selected rain/snow and Lavaland
  weather resolution ([fleet acceptance](../single-race.md#single-race-fleet-across-tracks)).

- [ ] Recover AI route/waypoint data, target speed, steering/braking, overtaking/recovery and difficulty parameters from track data and callers.
- [ ] Port AI decisions into the common vehicle update; preserve RNG draws and iteration order, including race starts and blocked-car recovery.
- [ ] Apply recovered car/setup/weather parameters without copying inspector camera or lighting assumptions into gameplay.

Acceptance: a full grid completes a Donington race without stuck cars in the
baseline sequence; the recorded seed/input/delta trace repeats. Compare original
AI decisions, route progress and lap splits at named checkpoints; record remaining
differences by address rather than retuning them to hide a recovery error.
Opponent voice allocation and distance/pan behavior follow the recovered calls.

## G06 — Race rules, damage and championship

`src/race/`, `src/simulation/`, scene lifecycle in `src/app/`.

Integrated:

- Single Race: distinct grid/qualifying starts, quantized source timers,
  directed route checkpoints, lap records, red/green lamps, starter cues;
  qualifying approaches automatically and hands over at the geometric first-line
  crossing ([session start](../session-start.md)). Lap/limit and current/best
  times with original sprites, centisecond formatting, unknown-best markers
  ([HUD](../session-hud.md), [finish contract](../session-start.md#recovered-finish-contract)).
- Three measured qualifying laps → AI handoff, states 7/8, authored camera,
  generated opponent times, result table, classified state 9 grid restart
  ([qualifying finish](../session-finish.md)). Finite laps, neighbor position
  records, finish order, states 10/13, RACE AGAIN ([rules](../single-race.md#single-race-lap-and-finish-records)).
- Damage and contact: static-barrier visual damage (side/severity/part
  variants, detached panels, landing/lifetime, sounds, reset;
  [car damage](../contact-effects.md#original-car-damage-presentation)); static-impact fragments, scrape sparks/audio,
  first-contact landing sound with retained pending acceleration
  ([contact effects](../contact-effects.md)); dry body-contact sparks from the
  `432210` ground branch (side/roof origins, shared queue/pool; the source has
  no separate ground-scrape audio caller); pile-up correction propagation,
  asymmetric secondary strong-contact torque, glancing-contact audio strength
  ([car contacts](../single-race.md#single-race-car-contacts)); rollover detection, player
  righting, frozen rival bodies, visibility-driven removal/retirement, shared
  fleet prop contacts before ground correction ([recovery](../single-race.md#single-race-rollover-and-removal),
  [props](../single-race.md#shared-single-race-props)); registered-sector pair order, first-contact
  damage for both cars, shared detached parts/fragments/sparks, persistent
  wall/car scrape handles ([sector order](../single-race.md#registered-sectors-for-car-contacts),
  [impact effects](../single-race.md#single-race-car-impact-effects)). [Source audit](../contact-effects.md#damagecontact-source-audit):
  no mechanical-health or repair consumer; do not invent engine/tire
  degradation from the visual mesh levels.
- Championship: twelve meetings / twenty-four races, qualifying pole points,
  classification, point thresholds, four awards, earned unlocks; checkpoints
  resume the qualifying/classified grid; TOCA Showdown uses its source
  qualifying/AI bonuses and mirrored view/controls; finish/results/standings/end
  speech, CD transitions, speech completion timing; honors backgrounds/counts/
  code labels, final English Showdown announcement, five named slots per variant
  with Save/Replace/Cancel and independent file errors; finale from
  CHAMPFIN.BFP (`449790`): Pace Car/transporter convoy, top-three driver
  figures, authored cameras, looping credits, CD04/motor/contact audio
  ([Championship](../championship.md), [finale](../championship.md#finale-acceptance)).

- [ ] Complete the remaining practice/race variants, penalties and retirements.

Acceptance: complete qualifying → both races → results and a championship round;
compare laps, times, positions, points and damage with matching reference scenarios.
Incorrect-direction crossings and confirmed penalties obey the recovered rules.
Required speech language, event ordering and interruption behavior pass with G08.

### G06a — Time Trial and best-lap ghost

Source contracts: [session/records](../time-trial.md#time-trial-core),
[frontend/pause/material/audio](../time-trial.md#time-trial-frontend).

- Mode 2: automatic approach, unlimited measured laps, ordered checkpoints,
  incomplete-attempt resets. Body poses recorded, only faster complete laps
  promoted, saved stream interpolated, car/weather metadata per track.
- Route Weather / Load Best Lap / Continue bypasses Single Race
  difficulty/qualifying and retains previous menu choices; localized End Time
  Trial with No-default quit confirmation, original cues, Sound Options, error-
  retaining pause. Ghost drawing, HUD without finite lap limit, active-time
  pause, track music, save-before-release.
- Two-page post-exit record table, Main High Scores entry, saved records in
  track selection; compiled defaults, strict improvement, XBOOSTME exclusion,
  original result codes.
- Physically driven lap through save/quit, restart and ghost replay: Donington
  254 ordered gates, 209520 ms lap, 3600 actual poses through 143960 ms; source
  buffer cap remains ([acceptance](../time-trial.md#time-trial-core)).

- [ ] Compare a complete physically driven lap and ghost trajectory with the
  original. Canonical synthetic gate traversal does not establish that parity.

## G07 — Settings, saves and records

Explicit serialization and user data paths. Native video/audio preferences
ship with F02/W01/G08; G03 bindings and G09 overrides join as contracts land; G06
is required only for championship/time-trial save completion.

Implemented: version-1 explicit text values, CLI precedence, deliberate durable
saves for display/audio. Startup save/restore, transient overrides, strict
malformed-value handling and read-only path discovery work; error messages
use startup output (menu presentation via C01). Original profiles, network
settings, bindings and progression await their owning packages.

- Per-track best runs: atomic writes with IEEE CRC32, existing disk record read
  even with Load Best Lap disabled, failed write retained for retry; one native
  `.ghost` schema with fixed millisecond timestamps; original-file exchange is
  outside the user-approved scope.
- Independent ten-track record table: source defaults, original visible result
  codes, CRC32; shared by result pages, Main High Scores, track selection.
  Championship records separate.
- Shared nine-row Championship table: original defaults, point ties, four medals,
  terminal submission callers; after the two Time Trial pages in Main High Scores;
  menu cues/CD02; retryable storage errors ([Championship](../championship.md)).

- [ ] Map original config fields, markers and encodings with F03/F04/C01. Distinguish disk values from runtime-only fields.
- [ ] Implement explicit endian/width config readers/writers and native display/audio/network preference fields; publish the early settings/override contract used by C01 and G09.
- [ ] Use SDL's portable user-data location and durable replacement writes; surface actual load/save errors in the common menu.
- [ ] Integrate settings at native startup and through C01; add G03 bindings and G09 override persistence with their feature batches and observed audio cues.
- [ ] With G06, recover save-slot/time-trial layouts and checksums if present; integrate championship continuation and records into `make run`, using user-data copies for validation.

Acceptance: `make run` saves and restores native display/audio preferences.
Joint F04/C01 acceptance adds menu settings and a known 1.1a config copy's logical
fields. Joint G03/G09 acceptance
adds restart persistence of bindings/overrides. With G06, load a known 1.1a save
copy, save/reload without field loss and resume its championship/records.
Native writes preserve reference assets and Wine user data. Observed save/load
cues ship through G08 in the same feature batch.

## G08 — Early Miles semantics and SDL_mixer service

`src/audio/`. F06a supplies the silent original-audio capture before Miles
comparison acceptance; the reference launcher disables audio devices and cannot
supply that baseline.

Service: SDL_mixer 3.2.4 locked after probing initialization, offline/device
PCM, 12-voice exhaustion/reuse, finite/infinite loops, live rate, stereo gains,
volume/mute, raw EOF, stream refill. 44.1 kHz stereo output, first-free shared
voices, ordered lifecycle/play events, Miles total-plays → SDL-repeat translation
(SDL AudioStream loops are unsupported: seekable audio/IO sources where looping
is needed). Mechanisms: [frequency ratio](https://wiki.libsdl.org/SDL3_mixer/MIX_SetTrackFrequencyRatio),
[stereo gains](https://wiki.libsdl.org/SDL3_mixer/MIX_SetTrackStereo),
[raw PCM/streams](https://wiki.libsdl.org/SDL3_mixer/MIX_LoadRawAudio),
[loop options](https://wiki.libsdl.org/SDL3_mixer/MIX_PlayTrack). Native linear
gains are raw service controls; recovered callers use the original Miles
master/sample volume and pan curves ([volume evidence](../driving-audio.md#original-miles-sample-volume)).
Full driving PCM and priority semantics remain open. Pause cues have a separate
captured comparison ([source evidence](../recovery.csv)).

Integrated callers:

- Engine: three source loops, compiled RPM/rate/volume curves, gas ramp, shift
  attenuation, wheel-coupled RPM; WHIRR (speed/shift-dependent); WHINE samples
  3/15, speed curve, shift modulation, shared RNG ordering after motor controls,
  bonus cars 8/9 bypass ([engine audio](../driving-audio.md#local-engine-audio)).
- Four-wheel shared RUMBLE voice with source contact events and camera distance;
  wet/surface layers; rumble/skid/rolling/tail pan ([driving audio](../driving-audio.md#moving-follow-camera-and-local-driving-audio)).
  Other actors and captured full-game driving PCM comparisons remain open.
- Miles integer master/sample volume and DirectSound dB conversion for all
  implemented frontend/driving callers, incl. live settings, pans, fades.
- Barrier impact caller, severity-selected SWIPE/COLLIS one-shots,
  generation-safe cleanup through the shared 12-voice service; damage/prop/
  landing variants consume the original RNG ([impact audio/HUD](../driving-audio.md#barrier-audio-and-local-driving-hud)).
  Camera-relative contact pan and Miles DirectSound channel gains incl. wall
  scraping, Mono/Stereo ([spatial contacts](../contact-effects.md#spatial-contact-audio)).
- Starter members 69/70 and State9 motor/WHIRR controls with session
  transitions, owned voice cleanup, focus pause ([session audio](../session-start.md#audio-and-lifecycle)).
  Sample 83 with qualifying state 7/8, CD/driving stop, focus ownership, source
  gain fade into classified grid restart ([finish audio](../session-finish.md#camera-table-and-audio)).

- [ ] Recover Miles handle/status/loop conventions, volume/pan curves, rate changes, stream lifecycle and engine/speech priority from callers and the existing DLL.
- [ ] Implement the imported-operation map below, preserving game-side arbitration, handle/status values, gain curves and stream completion timing.
- [ ] Compare offline PCM for finite/infinite loops, live rate/pan/gain changes, occupied voices and stream refill/EOF. Translate Miles total-play counts to SDL repeat counts; SDL AudioStream loops are unsupported, so use seekable audio/IO sources where looping is required. Use SDL_mixer, with no custom mixer.
- [ ] Integrate each feature's sounds when its callers land: C01 menu cues and CD track 02, G04/G05 engines/contacts/opponents, G06 announcements, G07 save cues, C06 confirmed Extras cues and N03/N04 session/race events. CD music ships with those callers; it is no longer excluded for missing source media.
- [ ] Use the five bundled Ogg/Vorbis 96 kbit/s tracks through SDL_mixer's built-in
  stb_vorbis decoder. Keep a separate music track outside the 12 sample voices,
  with deferred decoding, original CD track IDs and independent music volume/enable.
  C01 integrates `0x004688fc`'s track 02; G01/G06 integrate `0x00426ec0`'s race
  selection after resolving its tables. Preserve `0x0047a305` stop and
  `0x0047a3b4`'s duration-plus-2000ms/status/restart gate; do not substitute an
  automatic gapless loop. See [music provenance and acceptance](../music.md).

| Imported Miles operation (`AIL_` prefix) | SDL_mixer 3 mapping to verify |
| --- | --- |
| `startup`, `shutdown`, `waveOutOpen`, `waveOutClose`, `set_preference` | `MIX_Init`/`MIX_Quit`, [device/offline mixer](https://wiki.libsdl.org/SDL3_mixer/MIX_CreateMixerDevice); translate confirmed preferences into format/voice/buffer policy. |
| `allocate_sample_handle`, `release_sample_handle`, `init_sample` | `MIX_CreateTrack`/`MIX_DestroyTrack`, reset assigned input and playback properties; retain original 12-voice allocator. |
| `set_sample_file`, `set_sample_type` | Load WAV through mixer audio objects; explicit PCM spec for verified raw type flags. |
| `start_sample`, `end_sample`, `sample_status`, `set_sample_loop_count` | `MIX_PlayTrack`, `MIX_StopTrack`, `MIX_TrackPlaying` plus game lifecycle state; translate loop counts rather than copying their numeric values. |
| `set_sample_playback_rate`, `set_sample_pan` | Live frequency ratio = requested/source Hz; live stereo gains using the recovered pan curve. |
| `set_sample_volume`, `set_digital_master_volume` | [Track gain](https://wiki.libsdl.org/SDL3_mixer/MIX_SetTrackGain) / [mixer gain](https://wiki.libsdl.org/SDL3_mixer/MIX_SetMixerGain), using measured Miles scaling. |
| `minimum_sample_buffer_size`, `sample_buffer_ready`, `load_sample_buffer` | SDL_mixer owns buffering; [raw IOStream](https://wiki.libsdl.org/SDL3_mixer/MIX_SetTrackRawIOStream) or `MIX_SetTrackAudioStream` preserves source format, read order and EOF. |
| `redbook_open`, `close`, `play`, `stop`, `status`, `track_info`, `set_volume` | Original tracks 02–06 map to bundled Ogg files on a separate mixer track; preserve CD IDs, duration/status, volume and caller-controlled restart. Recover no-disc behavior for missing media. |

Acceptance: silent/offline checks verify event timing, sample count, pan endpoints,
loop completion, shared voice allocation and refill continuity; compare spectral
rate and channel energy with a captured Miles baseline under the same events. Exact resampler waveform
identity is not assumed.

## G09 — Original codes, unlocks and direct effect controls

Early state contract for C06; G07 completes persistence; each effect lives in
its owning simulation/content package. 22-entry code table at `0x004b9728`,
decoder `0x004759b0`, lookup `0x0047739f`, toggles `0x00476bfb`, mode gate
`0x00477409`. Entry 12 (`CMDISCO`) is always rejected by the original gate.
[Codes](../cheats.md).

| Confirmed code field | Observed consumer/effect |
| --- | --- |
| `GONGOGO`, index 15, `+0x84` | Permits car IDs 8/9 via `0x00475a3f`/`0x004761bc`. |
| `JHAMMO`, index 18, `+0x90` | Bypasses the progression limit in `0x0046ca6f`; typed JHAMMO keeps the original separate browsing limit (native All tracks switch also exposes Lavaland). |
| `CMCOPTER`, index 16, `+0x88` | When set, `0x0046caae` permits only track IDs 1, 3, 4, 7 and 8. This filter also applies with `JHAMMO`. Native C adds the original automatic helicopter camera on its five dry-weather circuits; no separate flight controls exist. |
| `XBOOSTME`, index 17, `+0x8c` | `0x00412ab0` doubles/caps the simulation delta and halves its reciprocal when `DAT_004c5d70 == 0`; `0x00426f20` also bypasses record-writing paths while set. Native simulation and record exclusion consume it; ghost saving is independent. |
| `PATSCREEM`, index 21, flag `+0x9c`, side effect `+0x38` | In addition to the flag toggle, `0x00476bfb` sets `+0x38 = 1` and calls `0x00469e25(1)`, which writes `DAT_004b6e20`. The target is Race Menu slot2: ToCA Showdown. Native typed permanent unlock and reversible menu override both reach the complete Championship/Showdown route. |
| `CMGARAGE` / `CMLOCK` (typed) | `CMGARAGE` writes earned bonus cars at `477381`; `CMLOCK` clears Showdown/bonus cars and restores threshold 1 at `47734a`. Menu switches never perform these irreversible writes. |
| `CODIES` | Matches the ordinary human-name setup; does not rename AI. |

- Mode gate `0x00477409`: original mode 1 permits indices 0, 1, 10, 19; mode 3
  permits 0, 1, 10, 15, 18; other modes permit all except 12. Track/car
  overrides still pass independent content/selection restrictions. Linkup
  excludes physics modifiers using the mode 3 gate; permitted cameras remain local.
- Saved code flags are separate from earned progress. Original exit
  `469e6e→46ac35` saves both in `TOURCARS.CFG`; after `46ac56` reloads, normal
  startup `468ccd` clears the 22 code flags but keeps earned unlocks. Native
  switches deliberately persist across restart and save immediately.
- Enforced in name redemption, garage/track selection, race/finale effects,
  record eligibility. Typed codes keep original activation speech; direct
  switches use menu cues. All 21 accepted code consumers and 19 reversible
  Extras controls implemented (incl. no-car-contact dispatch, reversible
  Showdown access). Only named, traced effects appear; multiplayer participants
  agree on allowed modifiers; unsupported mode combinations stay unavailable.
