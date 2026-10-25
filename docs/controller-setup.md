# Controller Setup

Game Options → Controller Setup opens a three-row player menu (Player 1 Controls, Player 2 Controls, Back). A supported player opens a five-row page: device, Configure selected device, sensitivity, Gamepad/Wheel Options, Back. Title names the player; Back returns to the same row. Keyboard, gamepad and raw wheel configuration share the original Configure row (label/icon follow the device). Changes apply/save immediately; failed saves stay visible and Back retries. Fixed menu controls are independent of driving bindings.

## Necessary native adaptations

- Player 1 may select Auto (keyboard and gamepad together); original and explicit choices stay, defaults unchanged ([Auto](#auto-for-player-one)).
- A disconnected saved pad does not lock its profile: entering it allows Keyboard or another pad; its Configure row needs a connected device.
- The original Windows Joystick Panel is replaced by Gamepad Options (SDL deadzone, vibration, Reset Defaults, Back); raw joysticks/wheels get Wheel Options with directional Force Feedback in the same switch position.
- SDL button/axis capture replaces the separate DirectInput calibration steps; raw axes keep released/centered calibration. Escape cancellation of partial captures is native (`473d59` stages and installs the complete keyboard map without an Escape branch). The original shortens its `OK` display on any held input; native capture uses the next fresh physical input, which also starts the next assignment.
- Wheel effects and dual-motor gamepad vibration use separate outputs.

## Original contracts

| Source | Contract |
| --- | --- |
| `472e2b`, `472f21`, table `4b9240` | Player titles use the canonical localized format. Native shows the two supported profiles. |
| `4730a0`, table `4b93c0` | Five rows: device, selected-device configuration, Low/Medium/High sensitivity, Windows joystick panel, Back. Sensitivity editable for keyboard. |
| `4747c1`, `474873` | Nine actions: accelerate, brake, left, right, gear up/down, rear camera, change camera, horn. Masks `10,80,1,2,1000,2000,8,20,40` hex. |
| `46ae6b` | Player-one defaults Up, Down, Left, Right, F, V, Z, C, 1; all nine configurable; F5 stays an optional fixed trackside shortcut. |
| `473d59` | Stage a full capture, reject duplicates, confirm each accepted input with sample2, install the map only after completion. |
| `475050`, bytes `4b9540` | Reject F1–F24 and NumLock; arrows only for their corresponding first-four action. Native Escape, Return/KP Enter, Backspace reserved for menus/cancel. |
| `473a67`, `473b9c`, `4190f0` | Sensitivity 1/2/3: steering gains 1/1.5/2, speed reduction, 90-angle-unit slew per simulation update. |
| `4749a6`, `474d2e` | Joystick digital/analogue and optional analogue pedals precede button capture; native wizard captures SDL buttons or signed axis halves. |
| `4932b9`, `47e920`, `47eb48` | DirectInput force-feedback capability, exclusive foreground device, constant-force/sine effects (distinct from SDL dual-motor vibration; see [horn.md](horn.md#force-feedback-evidence-and-native-boundary)). |
| `40f5c0`, `47eca3`, `47edb8`, `47ede6`, `47ee7c` | Impact and contact/surface effects; sine stop, pause/exit stop, device/effect release. |

## Capture screen and defaults

- Capture follows `473d59`: no heading, centered input prompt y120, current action y150, localized `OK` y360 after an accepted input. `63410` → `69f10(2,60)` / `69f9b`: nominal 30 Hz cadence; seven feedback updates = at most 233 ms of native time, FPS-independent. A fresh input shortens feedback without losing the next assignment. The ninth accepted action stays visible before the full map commits atomically. Escape or selected-pad disconnect cancels partial assignments. Fresh key/button edges and axis threshold crossings advance capture; held inputs do not repeat; opposite axis halves are distinct; the opening menu confirmation cannot become the first binding. Gamepad East is bindable and does not pause; Start (SDL button6) stays reserved for pause; only keyboard Escape cancels the wizard.
- Standardized gamepad defaults: triggers gas/brake (right/left), left stick steering, shoulders gears (right/left), North rear view, West camera, East horn. Camera/horn bindings replace their default keys (no fixed C/Z/1 shortcut after rebinding).
- Analog gamepad steering: native `sign(x) * x²` after deadzone removal (half travel = quarter input, full lock kept); original sensitivity/speed filtering follows. Pedals, digital steering, capture and menus keep their response.
- Native uniform adjustable 0–30% SDL deadzone, default 2 (original axis deadzones differ: X600/Y200; no original-calibration claim).
- One-player race start: gamepad South selects and saves that pad for driving and vibration, or pins it while keeping Auto; keyboard confirmation keeps the configured device. Two-player races keep both explicit profiles (track confirmation reassigns neither). Navigation, capture, pause do not change it. Identical models share a saved GUID; the confirming pad is pinned for the run. Disconnected saved pads keep their ID.

## Auto for player one

- Player 1 Controls → device Auto enables both saved maps for the same driver. Configure Controls opens Keyboard, Gamepad, Back; each complete map commits separately. Saved as `input_device=auto`; player two keeps explicit Keyboard or gamepad.
- Gas/brake use the stronger input; gears, cameras, horn combine held actions. With an idle analog gas axis a digital brake keeps the original keyboard throttle filter (holding Down accelerates in reverse); an idle analog gas axis does not suppress keyboard reverse gas. Active proportional pedals keep direct response. A held bound keyboard steering key beats the stick (original digital response); releasing restores analog; both keys keep the Left-wins rule.
- Auto picks one connected gamepad, reserving player two's concrete instance first in split screen (incl. identical models). Race confirmation can pin another eligible pad without changing Auto or saving a GUID over it. Keyboard continues when no pad is connected or the pad disconnects; a newly connected eligible pad can resume; an already connected spare does not inherit a disconnected pin. Focus loss clears both sources and stops vibration; held controls must return to neutral.
- Reference: live muted 1.1a Time Trial recording (car7, sunny, held Down) replays 96 updates/4,704 values exactly from one imported state incl. the R commit and backward motion (source shift threshold and physics unchanged); `build/test-artifacts/original-held-down/reference-trace-jg05dmmo/`.

## Local two-player controls

- Single Race → Players:2: two human drivers, no qualifying (`44f0a3`). Order: difficulty, P1 name/car/transmission, P2 name/car/transmission, shared track. Titles identify P1/P2. Track Back reopens player two's garage; Back from player two's name returns to player one's garage; Players back to 1 restores the earlier qualifying choice.
- P1: Up/Down/Left/Right, F/V, Z, C, 1. P2 defaults: W/S/A/D, R/E, Q, Tab, 2 (native adaptation; `46ae6b` gives player two the numeric keypad). Both rebindable independently. F5 = P1 trackside camera, F6 = P2, both need `CMFOLLOW`. Keyboard+gamepad and two gamepads use the same selectors.
- Identical gamepads share a saved SDL model GUID, but each local player owns a distinct connected instance; input and vibration never reach both cars from one pad. Disconnect releases that player's controls without transfer; a newly connected matching pad can resume. Focus loss clears both profiles and stops both pads' vibration.

## Vibration and host boundary

- `Controller::set_input_devices` receives current IDs/names and the runtime's selected concrete pad (Auto availability uses it). `InputSnapshot::physical_binding` carries one fresh capture candidate. `controller_*_bindings` screens expose capture index, staged values, rejection status. Selector and control page use five original icon rows; capture shows one action at a time in the original 640x480 panel. Bindings and `input_rumble=on/off` use the current preferences format; player two saves `p2_input_*`, `p2_key_*`, `p2_pad_*`.
- `InputDevices::rumble_supported()` checks the selected device's SDL capability. `rumble(low,high,duration_ms=80)` replaces that device's effect (max 250 ms). Disabled, unfocused, absent or unsupported devices produce no vibration and never block gameplay. `stop_rumble()` stops owned output; settings change, focus loss, remapping, unplug, destruction stop/discard it; focus regain does not replay. The host supplies physics amplitudes and stops output outside active local driving. Devices with real SDL constant/sine support use the original [wheel effects](horn.md#force-feedback-evidence-and-native-boundary) without simultaneous rumble.
- Local cone/post/sign hits feed the impact envelope via `PropResponse::impact_strength_q16` and `MotionState::prop_impact_q16` (struck vehicle's identity; independent of audio flags and fragment capacity). Opponents hitting the player feed the same envelope; opponent-only contacts do not. Wall impacts, hard landings, wall scraping keep their feedback. Continuous underbody ground scraping is not yet a separate vibration source.
- Rumble tuning: curb feedback = strongest contacting wheel plus a smaller summed contribution; loose surfaces keep a weaker mix. Output curve `y = x * (2 - x)`, then `y + y * (1 - y) / 4` (half strength → ~80%); zero, maximum and the 160 ms impact decay unchanged. Physical-pad QA found working but weak vibration (esp. curbs) before this tuning; physical steering/vibration feel remains hands-on QA.
- Host: `begin_poll()` before collecting events; `select_confirming_gamepad()` only when that frontend update starts scene loading. Only a fresh focused South confirmation selects a pad (keyboard confirmation in the same poll wins). Selection clears held inputs and stops old vibration. Applying the same saved GUID preserves the live instance; unplugging that instance cannot transfer control to an already connected identical pad; a new hotplug of the saved model can reconnect. Focus loss clears pending press/title edges before a same-poll regain.
- SDL 3.4.2: `SDL_RumbleGamepad`, `SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN`, `SDL_VirtualJoystickDesc::Rumble` (virtual callback for tests without hardware). Test fixture admits only its virtual VID/PIDs.
- Physical vibration strength/feel remains a hands-on check.

## Raw joysticks and wheels

- Explicit selection includes SDL raw joysticks/wheels (gamepads appear once). IDs: `joystick:` + SDL model GUID. Auto keeps its keyboard/gamepad contract. Both local profiles own distinct instances (incl. identical wheels); disconnect does not transfer to a connected twin.
- Before Configure: center the wheel, release pedals; capture records those rest values; move each requested axis fully or press its button. Raw steering linear after deadzone removal. Pedals may rest at either endpoint and use full calibrated travel. Nine-action transaction keeps cancellation, duplicate rejection, confirmation, atomic save. Defaults: steering axis0, gas axis1, brake axis2. Keyboard menu navigation works; raw button0/1 confirm/back.
- Binding format: `j4` = raw button4; `r1-:-32768:32767:32767` = raw axis1 negative direction with minimum/rest/maximum. Axis indices 0..31, buttons 0..127. Saved endpoints adjustable for reduced travel; malformed, duplicate, zero-travel bindings rejected. Focus regain needs neutral relative to the instance's saved calibration.
- `input_rumble` switches both output types. The device's actual SDL Haptic capabilities decide constant/sine effects (also mapped gamepads with real haptics); missing haptics never block input. Physical wheel direction, strength
  and driver compatibility remain hands-on acceptance.
- Ownership: device enumeration, gamepad/raw-joystick handles and SDL subsystem references are scoped (incl. incomplete initialization); haptic effects close before their device, devices before the subsystem reference; hotplug, remapping, shutdown keep stop-before-close. `input-device-lifetime` injects constructor allocation failures (handle release, reference balance, reopening).
