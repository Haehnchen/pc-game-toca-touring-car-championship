# Local split screen

The canonical 1.1a two-player branch uses two human cars, no single-player AI field. `44f0a3` sets the actor count from config `+ac` and forces `+20=1` (No Qualifying). `421390` hands each finisher to AI and waits for every local human before global state10. Native: shared track, contacts and simulation deadlines; separate input, motion, camera, HUD and audio state. After the last finish `448a20` waits for timer0 strictly greater than five quantized seconds, then opens RACE AGAIN directly over state10; local multiplayer does not enter the single-player state13 result table. The owned finish loop continues beneath the overlay and fades on warm restart; focus loss mutes it.

## Local Car Boost

- `4694f0` replaces the Qualifying option with Car Boost when Single Race has more than one local player; `469386/4693f5` toggle config `+2c`. Native two-player menu uses the original localized option/help and ordinary option cue; Left/Right/Enter toggle; original default On. Returning to one player restores the previous Qualifying choice independently of the boost option.
- `415090` calls `428440` after grid release when `+2c` is enabled and the local count exceeds one or the mode is Linkup; `426e60` supplies the human count; both modes share the throttle/tire catch-up calculation. `simulation/car_boost.*` serves local play and Linkup. Both local slots keep their own smoothing state and car-specific throttle baseline, incl. pit/finish AI handoffs. The prepass snapshots the race leader before either body update; tyre writes follow launch adjustment. Pause freezes the state; Race Again keeps the option and initializes both cars' boost state afresh. XBOOSTME's doubled simulation speed is separate.
- Trace: `car_boost`, each driver's `base_throttle`, `maximum_throttle`, first `tire_offset` (P2 prefix `p2_`). Reference result: On reaches the trailing Audi's source limits (+6553 throttle, +7864 tyre offset, Q16); Off keeps both throttle baselines.

## Integration state

- Done: local actor count, qualifying restriction, both-player finish gate; Players:2 with separate driver/car/transmission selection; Car Boost menu/physics/start/pause/restart; one collision/prop world with individual lap records; each measured finish time retained while waiting; saved horizontal/vertical projection, clipping, per-view HUD; keyboard and separate gamepad assignments, focus, pause, restart; original audio layers for both players; Mega HUD lamps with independent values and unobstructed reduced/odd panes; full two-player race with mutual contacts, both pit traversals, independent finishes and shared result/restart.
- [ ] Complete hands-on driving and vibration QA with two physical gamepads.

## Controls and presentation

- Start Race → Single Race → Players:2 selects local play; each driver chooses name, car, gearbox before the shared track.
- Game Options → Graphics Setup → Split: **Horizontal** (player1 above player2) or **Vertical** (player1 left, player2 right); persisted as `split_vertical`. Camera offsets follow `4508ce`; split projection uses horizontal `0x10000` and vertical `0x1383a` with per-pane aspect correction. Pane sizes derive from the live drawable; an odd spare pixel belongs to the second pane. Pause/RACE AGAIN use the whole window.

| Action | Player1 | Player2 |
| --- | --- | --- |
| Gas/brake | Up/Down | W/S |
| Steering | Left/Right | A/D |
| Manual gears | F/V | R/E |
| Rear view | Z | Q |
| Next camera | C | Tab |
| Horn | 1 | 2 |
| Trackside camera (CMFOLLOW) | F5 | F6 |

- The second keyboard uses WASD as a native adaptation; source defaults at `46ae6b` used the numeric keypad.
- Controller Setup keeps a separate binding/device profile per player. Two pads of the same model get distinct connected instances. Escape or either pad's Start pauses both cars. Confirming the track in split mode keeps the explicit assignments (does not reassign the confirming gamepad). Shared objects advance once per simulation deadline; rendering a pane never advances physics or audio.
- Audio: twelve voices per local player, original engine/contact curves, shared music. Each voice owns its track and playback state. Pool changes require stopped sample voices; expansion prepares every new track before committing slots; creation/initialization failure releases staged tracks and leaves the pool usable.

## Finish and restart behavior

- Group timer starts at the last finish; confirmation cannot bypass it; strict quantized gate: 5000/5062/5063 ms distinguish it. State10 through 5040 ms, automatic RACE AGAIN at 5080 ms with retained totals/finish audio. The one-player flow keeps state13; focus muting/resume works while the overlay is open; retained-voice fade on warm restart.
- Race progress: a player2 win keeps player1 control, waits beyond the ordinary results timeout, shows a two-row classification, finish times immutable until restart. Pit: all 31 pit gates per car, entry speech, barrier restoration, human control at each exit; each pane shows its own flashing pit caption and control-return hint.
- Mega HUD: red/green lamps in each pane, separate cached gear/lap/time values, original 3080/6160/9240/10840 ms start deadlines; 640x480, 960x540, 961x601 (spare pixel in the second pane).
- `native-split-race --step-input` commands (`step P1_MASK [P2_MASK]`) supply only driving actions at 40 ms deadlines.

## Physical controller QA still open

The 2026-10-17 host inventory exposes one Xbox360 controller; two-pad hands-on QA
has not been performed. Automated SDL device/input checks do not establish
physical driving or vibration feel. With two pads attached, verify independent
steering/pedals/cameras and feedback, pause from either Start button, disconnect
and reconnect without swapping drivers, then Race Again with both assignments
retained. Network Linkup has its own [pending package](implementation/04-multiplayer.md).
