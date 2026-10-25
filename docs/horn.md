# Local horn and original force-feedback callers

## Horn

- Input: action8, original mask `0x40` (`4747c1` / `474873`). Uses the actual held action, independent of render FPS.
- `4234fc` calls held-input helper `422c40`, stores state at `4d3920`. Starts on the first eligible held tick, stops on release, no retrigger per tick.
- Eligible: state6 and (qualifying flag `4bd934!=0` or start-lamp phase `4c3a7a!=3`). Phase3 = green, ends after 1.5 s (`421f40`); qualifying/Time Trial use the qualifying flag.
- Native pause, focus loss and scene teardown stop the owned voice.

| Item | Original |
|---|---|
| Start | `425880`: sound `player_index+0x47`, totalplays0 (loop), no rate override, pan64 (one local player) |
| Player0 | SOUND member71 `CARHORN.WAV`, independent of selected normal car |
| Players1..3 | members72..74 `CARHORN2/3/4.WAV` (not per-car) |
| Archive | weather3/5 `soundwet.bfp`; other weather `sounddry.bfp` |
| Gain | `mix[13]*100 >>16` (`4257d0`, `424410`, `4244f0`): view2 `0x6666`→39, view0/3 `0x4ccc`→29, others `0x9999`→59 |
| Stop | `425930` one player, `425950` all (also audio cleanup `426400`) |

- Gain is captured at start; the original does not update it on camera change. `Horn::set_camera` affects the next press.
- Bonus car8 exception: body constructor `40a770` stores the native car ID at `body+516`; `4234fc` tests it against8 and calls `406a40` (special action/projectile, outside this module) instead of the horn. Native emits no horn for car8; car9 uses the ordinary horn.
- `audio::Horn` owns a voice serial, so cleanup never stops a reused slot. Stop keeps the input latch: unchanged held input does not re-press; release then press starts again. Host gates scene/control eligibility and clears input on focus loss. The service outlives the horn.

## Force-feedback evidence and native boundary

The original has DirectInput wheel feedback, not two-motor rumble.

- `4932b9` queries capabilities, tests `dwFlags &0x100`.
- `47e920` registers at most two feedback devices, foreground-exclusive, sets properties, acquires, calls `47eb48`. Creates constant-force effect (GUID at `4ac400` = `13541c20-8e33-11d0-9ad0-00a0c9a06e35`) and sine effect (`4ac430` = `13541c23-8e33-11d0-9ad0-00a0c9a06e35`).
- `47e920` property9 = 1 (AUTOCENTER ON); property5 sets the axis deadzone.
- `47ede6(0)` stops both effects (pause/exit); `47ee7c` releases effects, device interfaces and DirectInput.

End of ground consumer `40f5c0` (export lines 17999..18084):

| Condition | Effect |
|---|---|
| `body+53b==-1` | no feedback device, return |
| impact direction `body+514` codes1/2/5/6 | direction `(-100,0)`, `(100,0)`, `(0,100)`, `(0,-100)`; `47eca3` with magnitude `body+4ec`, duration 50000 µs. A 15-call cooldown suppresses sine updates (including the impact call); further impacts still run and restart the cooldown |
| rumble fields `dynamics+280..28c` and speed `body+e0 >0x3333` | sine magnitude `trunc(speed/8)`, period `160000-speed` (`47ed33`) |
| surface2/3, wheel-contact byte zero, same speed gate | sine magnitude `trunc(speed/32)`, period 100000 |
| conditions cease | `47edb8` stops the sine effect |

## Native wheel feedback

- `WheelFeedback` follows `40f5c0` once per 40 ms physical update. Motion publishes the signed impact magnitude/direction before clearing `body+514`; both local actors produce output fields independent of hardware.
- `body+53b` is the FFB-device slot (`422cf0 → 422e80 → 47eb08`), separate from actor identity. Constant impulses last 50 ms. Curb/loose-surface flags keep the original shared-sine stop/start order and cooldown.
- SDL disables autocenter when opening haptics; native active driving restores it via `SDL_HAPTIC_AUTOCENTER` where supported. SDL's 100 setting maps to ON under Windows; physical strength on other drivers remains hardware QA. Pause/focus/scene stop disables the owned autocenter, resume restores it. No spring/damper effect is synthesized; native input deadzone stays a separate setting.
- `WheelHaptics`: nominal DirectInput magnitude 10000 → SDL 32767 (unsupported magnitudes bounded), µs → ms. Nonpositive or sub-ms sine periods use 1 ms; infinite source sine duration stays infinite. Multi-axis constant directions keep XY; one-axis wheel gets signed X only (pure Y impulses give no steering torque). Sine uses SDL's steering axis. Constant/sine capabilities independent; unsupported devices still drive.
- Effects live in owned slots; stopped/released on pause, focus loss, device/settings change, scene exit, teardown. Output failure stops the stale effect. Haptics handles close before their joystick.
- SDL gamepad rumble is a separate presentation adaptation of physics events. A real haptic device uses the original wheel effects instead of dual-motor rumble. Physical wheel/driver parity remains unaccepted.
- Physical gamepad feel and directional wheel feedback are not claimed by these checks.

## Native gamepad rumble (`GamepadFeedback`)

- Consumes local Motion state once per 40 ms simulation deadline.
- Rumble-strip wheels give the stronger road texture; surface2/3 wheels half that. Static/landing and local car-to-car impacts feed a low-motor pulse decaying within 160 ms; barrier scraping adds sustained texture. Amplitudes saturate to SDL 0..65535.
- No ghost or remote-only collision supplies feedback. Weights are native tuning, not reconstructed DirectInput magnitudes/directions.
- Each request expires after 80 ms unless refreshed. Pause, focus loss, AI handoff, finish, scene exit and unsupported motion boundaries clear it. Missing rumble support leaves driving available.
- Tests: `tests/audio/test_horn.cpp` (optional canonical-root argument checks member71 in wet/dry archives), `gamepad-feedback`, `native-input-devices` (SDL callback delivery).
