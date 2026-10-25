# Driving audio

## Local engine audio

`audio::Engine` loads the selected car's three original motor WAVs and WHINE during scene loading, reserving no voices until `start()`. Scene activation calls start and applies the current source state; fixed simulation deadlines call `update(inputs, shared_random)`. The application mixes offline audio up to each deadline before applying the next controls. It does not create RPM from accelerator input and does not advance the simulation.

| Original source | Native contract |
| --- | --- |
| `0044f0a3` → `004244f0`, `00424790`, `00413ed0` | Load race sounds, establish mixes, init local voices silently. `00424790`: SOUNDWET for weather3/5, else SOUNDDRY. Native uses the full-memory archive branch; `004949fb` picks low-memory archives only below 17 returned memory units (`(totalPhys>>20)+1`). |
| `00414b00`, `00414c10` | Camera0/1/4/5 → internal samples1/2; camera2/3 → external13/14. Car-specific internal indices for IDs0..7: `[5,6,10,11,4,7,8,9]`; external indices add 12. WHINE uses3 inside, 15 outside. Members decoded directly from BFP byte spans. |
| `00413f70`, `004798cb`, `0047992c` | Start low, high, car-specific layers in order, volume0, pan64, Miles loop count0 (infinite). First-free shared 12-voice Service owns SDL_mixer loops. Failed allocation = zero handle, no extra pool. WHINE starts next; the Driving consumer starts WHIRR afterward. |
| `0044a6f0` → `00414240` | RPM=`clamp(2000 + (mul16(mul16(engine_speed_q16,rpm_scale_q16),0x19640000)>>16),2000,8500)`. Raw fields dynamics+124, dynamics+24, filtered throttle+11c (globals `4c5ec4/4c5dc4/4c5ebc`). Throttle `mul16(rawThrottle,0x640000)` compared **directly** with 50/100: below50 → 0, above100 → 100; no extra shift/percentage conversion. Gear from signed byte `4c60d0`; outside 0..6 → 1. |
| `00414240`, compiled `004ad0f8`, `004ad1f0`, `004ad2f0` | Low curve 2000..5800 over 76 intervals; high 4500..8500 over 80; car-specific 2000..8500 over 130. Index `(rpm-minimum)*intervals/(maximum-minimum)`, inclusive at both ends. Absolute-Hz ushort rates and byte volumes read from the canonical executable (bounded PE reader). Outside a curve: rate11025, volume0. No interpolated curve. |
| `004244f0`, `00424410`, `004145c0` | Inside gains 0x5999/0x5999. External camera2: 0x5999 low/high, 0x4000 car-specific; camera3: 0x4ccc/0x3333 (camera-selection assembly audit; [race cameras](race-cameras.md#audio-mapping)). Fixed products shift16, truncating. |
| `00414740`, `00414240`, `00414780` | Car-layer throttle state rises ≤20, falls ≤10 per caller. Byte gain × percentage /100, then fixed camera gain. Shift counter decremented before motor controls; car layer minus 20 while nonzero, clamped0. A changed gear sets the counter to 2 **after** those controls. WHINE then uses the updated counter and the shared RNG; bonus cars8/9 bypass `14780` incl. gear/shift updates. |
| `00479b21`, `00479b3d` | AIL receives absolute playback Hz and sample volume. SDL_mixer frequency ratio = originalHz / the WAV's own source rate (canonical engine WAVs 22050 Hz), not output 44100 Hz. Original integer master/sample volume and Miles dB curve (see Original Miles sample volume); centered stereo keeps both channel gains equal. Captured-original full driving PCM remains open. |
| `00413dc0`, `00413e90`, `004795cf` | Scene exit/pause mute local layers and clear the update gate; sample end clears a handle. Native `stop()` ends motor/WHINE loops and releases slots. Restart keeps throttle/gear history; a new scene Engine initializes it. Stop/destroy Engine before Service clears/reuses slots. |

### WHINE

- `13f70` starts member3 (internal) or 15 (external2/3) after the three motor loops, before WHIRR24. `14120` excludes bonus cars8/9 incl. gear tracking and RNG work. `14780` reads the 193-interval curve at `4ad488`, rates `4ad494`, gains `4ad618`, 24 shift offsets at `4ad820`. Upper speed boundary exclusive: 193 or higher, or neutral → volume0/rate11025. `session_audio_inputs` gives WHINE and WHIRR the same speed, incl. the original State9 grid override.
- After motor controls a changed gear sets the shared shift counter to 2. Only an upward transition draws the shared RNG: values above `0x5ffe` enable modulation from phase0, otherwise phase25 bypasses it. Gears1..4 increment the phase before reading offsets1..23; gears5/6 bypass the lookup. Keep signed index arithmetic, negative-gain fallback, final fixed camera mix. Full throttle100 raises its separate ramp by 10 per call; other throttle resets it. Nonzero shift counter subtracts 20 from mixed volume, clamped0. `44a6f0` supplies zero for the two additional caller flags.
- Stop/resume keeps ramp and gear history; new Engine resets. Failed allocation does not make a replacement voice or skip the gear/RNG update. Diagnostics: `whine_hz`, `whine_volume`, `whine_active`.
- Focus loss stops native loops, regain starts them at the current simulation state (native adaptation; not a claim about Win32 focus handlers). Unfocused wall time generates no catch-up. CD03 stays on the separate music track.
- Moving integration supplies wheel-coupled RPM; WHIRR and contact rumble: see Moving follow camera and local driving audio. Whole-game RNG continuity and captured full driving PCM remain open. WHINE shares the source random stream; opponent and camera audio belong to their native consumers.

### WHINE source comparison

Extracted canonical `14780` vs native Engine: 32000 updates, 160 scenarios (all 10 cars, 6 views, gear changes, throttle resets, speed 0..260 incl. 193, failed voice gates, final shared RNG state); local caller flags 0/0 (weather3 suppresses WHIRR). Stub service; not a captured-original soundtrack comparison. Evidence under `build/whine-miles-source/build/reference-whine/`: `compare-whine.py` CRC32 `cff3bd01`, raw records `44e8ef30`, canonical curve slice `149cba52`. Canonical executable `8ce56d41`, Miles DLL `1012e9cb`.

### Recorded inter-update state

- `audio/engine_state.hpp` derives the `44a6f0` caller values, `414240` gear normalization/shift decrement/car-throttle ramp, and `414780` offset, WHINE ramp, remembered volume, playback controls. `Engine` uses the same numeric transaction as bounded replay. Retained state: previous gear, modulation phase, shift ticks, WHINE throttle, car throttle and its mirror, remembered WHINE volume.
- Original recording: 63 intervals between 64 updates; numeric state imported once, each prior update's RNG used once per interval, every callback executed, no intermediate state/RNG imports. 4,355 values and 126 Hz/volume commands match (59 positive volume writes). Inactive-voice recording: 4,103 values. See [fleet evidence](single-race-fleet-reference.md#inter-update-whineshift-state--2026-10-18).
- Native active-tick order: local wheel/material audio, ascending queued tire dispatch, Engine/WHINE, WHIRR (with the updated shift counter); tire draw precedes the upward-gear draw.
- Callback timing, physics, mix and voice-playing gates remain observed. Coverage: one local player, normal enabled WHINE; both upshift draws choose phase26/offset0. Other branches: source/component coverage. Source `13ed0` resets six state fields but keeps the process-initialized remembered volume; a fresh native Engine starts that volume at zero. Native split-screen has one Engine per player, while the original shared caller returns early for multiple locals. These lifecycle/multiplayer boundaries are not covered by the recording.

## Moving follow camera and local driving audio

The free-driving scene consumes source-derived body/wheel states; camera and audio consumers share the simulation deadlines, scene lifecycle and focus handling of the normal `make run` path.

### Follow anchor

- `FollowCamera` starts with the independently selected SGRID heading. Yaw is stateful; presentation reads `world_to_view(body)` without advancing it. Call `update(previous_body, false)` once before the active physics tick, then present the new body position (order of `00421aa0`: `0045ea90` precedes controls/physics). `004608b0` publishes separate anchor yaw and current body translation, not roll/pitch. View2 local matrix and full affine inversion stay in `scene/body.cpp`. `position(body)` exposes camera translation as rounded Q16 for audio (not the horizon transform, no unit-basis assumption).
- `0045e920` derives the signed angle from body basis-X's X/Z components. Runtime `00478b20` table: 512 entries, truncate `atan(float(i)/511)*65536`, then ×651 >>16. Index = absolute smaller/larger fixed ratio >>7. Equal nonzero components reach index512, overlapping the low word of the adjacent original cosine[0] (=65536) → 0; retained, not clamped. Exact original CRT bit fidelity across hosts remains open.
- `0045ea90` wraps angle+anchorYaw into a signed 12-bit error. Normal active updates subtract integer error/8 (toward zero). State4 branch: subtract22 beyond error+284, add22 below −284, else subtract error/16. Integer deadband and 16-bit stored-heading wrapping kept.
- State4: `4480e0` calls `45eb60` → `45ea90` with the State4 slew rule; State4 continues `421aa0` physics until the ordered start-line handover. Entry starts track music via `426ec0` and fades an existing sample83 loop; no starter red/go cues. Native: State4 slew during automatic qualifying rollout, then normal /8 after control returns to the player. `45eb60` initial qualifying camera track degrees `{105,350,-150,0,0,0,180,-45,0,0}`: `degrees*65536/5760` truncated, +1024, heading not normalized. First State4 follow step applies before the initial frame. Grid entry and classified race restart use SGRID.

### Driving sounds

`Driving` uses the shared Service and the full-memory SOUNDDRY/SOUNDWET archives. Loading allocates no voices; start after Engine starts, stop before sample clearing/reuse; focus follows the Engine policy. Sample IDs `0x200+nativeIndex` avoid frontend/engine ID collisions; no extra voice pool or mixer.

| Source | Consumer |
| --- | --- |
| `00413f70`, `00414780` | WHIRR member24: silent infinite loop except weather3/5 (no handle). Speed published before physics by `00403dd0` (export lines 9431/9533–9538): ushort-pointer target `4cc5ba+0x15` words = gameplay `4cc5e4`, source `4cd81c+0x12` words = body+e0; stores `mul16(body.speed,0x640000)`; `0044a6f0` shifts16, caps260. Motion publishes this previous-step value at tick start. Curve `004ad6e0`: 90 intervals over 80..260, rates `004ad6ec`, gains `004ad7a2`; below80 volume0/rate11025; above260 last entry; at80 gain0; default View2 audible from 82. |
| `00414780` | WHIRR uses the Engine shift counter (read-only after Engine.update): counter2 → camera mix 80%, 1 → 90%, 0 → 100%. View2 car-layer camera mix `0x4000` (View3 `0x3333`). Rate divided by the WAV's source rate for SDL_mixer. Driving keeps no second shift state. |
| `0040fa..fb`, `004251f0` | Four old/new discrete contact-activity states emit start13/update14/stop15, wheel order 3→0. A local player shares one contact-rumble handle and a four-bit wheel mask; extra starting wheels join it; only the last stopped bit ends the loop. Caller compares previous dynamics+290..29c with current +280..28c. Contact positions body+f0/fc/108/114 = contacts[0..3]. |
| `0040fa..fb`, `004251f0` | Strength `clamp(mul16(body speed,dynamics+28),0,65536)` (car parameter `0x9851e`). First active wheel: sample `64−(adjustedStrength*8>>16)`; below `0x199a` use `0x2666`, else clamp `mul16(strength,0x19999)` to 1 → members56..63 (RUMBLE1..8.WAV). Contact/rolling-rumble events, not a slip-to-squeal model. Deterministic, no RNG. Update14 keeps the loop/sample. |
| `00423fb0`, asm `423fb0..423fec` | Subtract fixed listener/contact X/Z, shift each difference16 **before** squaring, square-root, truncate, restore Q16; height ignored; reject distance>40. |
| `00424c70`, `00424c10`, `004251f0` | Fixed distance fade `0x666` and 100; camera tire mix (View2 `0x9999`), strength×2, cap100. Tiny-strength bit threshold `(strength*25 &0x3fffc000)*4 <65537` kept. Failed spatial query keeps prior controls. |
| `004257d0`, `00424410` | View2 grass/gravel rolling gains (mix1 indices7/9) are both 0: no audible grass/gravel replacement loop for this camera. |

- WAV decode, loop, resampling, volume/mute, output: SDL_mixer via Service. Rumble and skid voices follow each wheel's camera-relative angle via `424b50/424ac0` on start and successful updates (same camera axis and Miles pan as [spatial contacts](contact-effects.md#spatial-contact-audio)). Out-of-range queries and queries whose spatial volume truncates to zero keep existing controls (incl. the 40-unit boundary). WHIRR stays centered. WHINE and its shift/RNG modulation: see Local engine audio. All layers use the Miles volume curve (see Original Miles sample volume). Captured full-game driving PCM comparisons remain open.

### Slip and changing surfaces

- `0044b690` prepares retained wheel events; `0044c540 → 00424d50` consumes them with current wheel positions and the shared RNG. Sum of absolute slip > `0x4ccc` starts the effect; strength `min((sum−0x4ccc)*6,65536)` with source signed32 wrapping. Airborne byte1 suppresses slip. One skid voice and four wheel-mask bits per car. Asphalt members 39–44, grass 47/48, gravel 49; original camera gains and shared `SourceRandom` draw order. Spatial fade stays Q16 until strength×1.2. A previously prepared event can play after the wheel stops slipping.
- 25 Hz tick: consume previous queues in ascending actor/wheel order, then prepare next queues after audio. Single-player preparation admits the human and visible full-body rivals; each split-screen callback prepares its own human. Invisible rivals reset slip flags and request deferred muting. Rival wheel channels share loaded samples, allocate no WHIRR, dispatch only skid events (`425400`/`4251f0` exclude unowned actors from rolling/rumble; their initial dominant surface0 stays). Skid-mark visuals share the slip gate.
- Focus loss stops voices and clears queues/slip flags; next active preparation creates fresh starts; pause freezes queue counters; scene restart clears them. Original callback timing remains observed; one recorded RNG connects AI, retained tire and WHINE phases ([combined comparison](single-race-fleet-reference.md#combined-ai-tire-and-whine-random-pass--2026-10-18)).
- `00425400` keeps the previous rolling class for curbs and airborne wheels and arbitrates surface changes in wheel order 3→0. Grass 67/gravel 68 keep View2's zero rolling gain (skid sounds audible). Rain3/storm5: wet rolling member81 and SOUNDWET; snow4: SOUNDDRY. `00425ef0/00426110` keep one displaced sound tail per car, volume ×`0xe666` per update, released below 2. Pause, exit, scene change release all driving voices incl. the tail.
- `material_state.hpp` shares the material/restart transaction with replay. Wet weather, leaving asphalt: View2, View0/3 or showcase require all four retained wheel materials to agree; internal views1/4/5 use the rising-material rule. Dominant material and restart flag determine the next skid sample and RNG draw. Disabled/unowned calls keep material and effective camera state.
- Rolling pan follows `425400`'s material-side mask, independent of camera direction/distance: contacts0/2 left, 1/3 right. Dominant material left only → 32, both → 64, right only → 96; empty-mask branch → 96. Curbs/airborne wheels keep previous material first. A displaced tail keeps its last pan through the decay (`426110` changes volume only). Mono centers all; Stereo restores requested direction. Leaving gravel: all four contacts in one update keep tail64; leaving contact0 for one extra update gives rolling32 then tail32 (original early dry-asphalt return before pan calculation).
- Simulation trace includes wheel surface/slip, body velocity/forward axis, yaw rate, skid sample/volume, rumble/skid/rolling/tail pan; no tire slip is inferred from steering or camera rotation.
- Original-game PCM and matched motion traces remain open.

## Barrier audio and local driving HUD

Related consumers: [damage](contact-effects.md#original-car-damage-presentation) (dents, detached panels, break sounds), [contact effects](contact-effects.md) (fragments, scraping, landing), [session](session-start.md) (start sequence, lap records). Opponent collisions remain open.

### Impact caller

- `00409c30` calls `00409fa0` after forces/integration/engine, before ground completion. `409fa0` gives `419ad0` an impact vector with X = absolute normal speed ×16 (barrier type1) or ×5. `419ad0` computes its fixed lookup length, caps strength at 1, returns kind1 then damage-dependent kinds2/3/4; the simulation adapter consumes these and the node mutations.
- `409fa0` multiplies strength by `0xe666` and calls `424860` at the selected contact position; `424860` applies another `0xe666` for kind1 (both intentional). Values ≤ `0xf5b` are silent. `424d10`: SWIPE1..4 (members33..36) below `0x6666`, else COLLIS1..6 (members25..30), two piecewise integer formulas; no RNG for kind1. Kinds3/4 consume the break-sample RNG draw; landing kind5 selects ROLL1..5 (members51..55) with its own draw.
- `424070`/`423fb0` quantize horizontal deltas to whole world units, ignore height, cutoff 40 units. `424c70` keeps fixed-point precision through the fade; `424c10` multiplies by event strength <<4, clamps to 100. `424410` applies the camera mix's local/remote gain: View2 `0xa666`/`0x7333`, initialized in `4244f0` (not in the executable data section). Master/sample volume: see Original Miles sample volume; pan: [spatial contacts](contact-effects.md#spatial-contact-audio).
- Loading decodes original WAV spans from the selected full-memory SOUNDDRY/SOUNDWET archive, reserving no voices. Each contact plays once through the shared 12-voice pool with first-free allocation (`424cd0`), never stealing. Focus, scene exit and contact-limit stop owned playback; resume waits for new events. The service's monotone playback serial distinguishes an old finished sample from a foreign sample reusing the slot.

### HUD resource and drawing contract

- `44f0a3` opens IMAGE640.BFP. `4463e0` consumes 23 particle members; two fonts and three images move the cursor to 28. `41ad80`: 640-wide HUD atlas member32 (KM) / 33 (MILES); large glyph atlas member36. `465631`: MILES for language0, KM for other original languages. Real archive indices, not cursor offsets.
- `41de90` draws speed background at `(470,448)`, tachometer background `(452,427)`, gear `(566,424)`, speed `(560,459)`. Background rectangles: tables `4ada18`/`4ada48`; 12 tachometer segments `4ada68`; absolute positions `4adb88`. First segment threshold 2800 RPM, 500 RPM steps to the twelfth at 8300. `41e480`: large gear glyphs, R/N at indices10/11; flag8 selects the secondary atlas and adds 106 to source Y in the normal 640 branch. `41e5f0`: small speed digits, 2-pixel spacing. Glyph rectangles bounded against the two decoded atlases.
- `44a6f0` caches speed, RPM, gear via `41a760` kinds0/1/5. HUD RPM slews ≤500 per simulation update from 2000, independent of motor audio. Speed capped at 260; MILES shows integer `speed*100/161`, suppressing <2; KM suppresses <3. Host supplies source speed record and engine/gear values once per simulation deadline; presentation only reads the cache (FPS-independent).
- Native layout scales uniformly by `min(drawable_width/640, drawable_height/480)`: gauge cluster bottom right, lap panel top left, timing top center; 640×480 matches the recovered layout. Free-drive/ESC notice bottom left. See [widescreen layout](session-hud.md#widescreen-layout). Rival positions remain unavailable. Original D3D/DirectDraw palette/material and final sprite raster fidelity are not claimed by this bounded consumer.

### Original start lamps

- `44f0a3` consumes IMAGE640 members25/26/27 (128×128 housing, 64×64 red/green lamps). `450067` initializes the normal 640 layout: housing source `(0,0,128,64)` → `(229,50,256,128)`; right strip `(0,64,77,64)` → `(485,50,155,128)`. `490c00/490cb0` set XYWH source/destination rectangles. Lamps start at x240, 49-pixel spacing; red y52, green y113. `41f7ab`: housing in phases1..3, red phase2, green phase3. The session owns the cached phase; rendering does not advance timers.
- Native widescreen: housing and lamps centered at the top with uniform scaling; only the support extends to the drawable's right edge, repeating its strip and clipping the last repeat. 640×480 placement/strip intact.
- Qualifying and no-qualifying use distinct release paths ([session evidence](session-start.md)). HUD speed/gear/RPM stay actual vehicle values while State9 applies its separate pre-start audio controls.

## Original Miles sample volume

All recovered frontend and driving sample callers use integer Miles volume through `Service::play_original_volume` and `controls_original_volume` (the latter keeps volume and pan for live Sound Options changes). Music keeps its separate CD track and volume path; the native master/mute control still applies to the whole output. Generic linear controls remain for raw PCM service consumers.

### Recovered curve

- Sources: canonical `mss32.dll` (CRC32 `1012e9cb`) and the 1.1a decompilation. Frontend `475260` calls `47950c` with the effects value unchanged: UI100 = Miles master100, not127. Miles `2000de00`: `floor(master * sample_volume / 127)`. `2000de70`: DirectSound hundredths of dB = truncation toward zero of `-2000*log10(127/value)`. SDL_mixer amplitude = `10^(dB/2000)`. Zero → exact silence natively (DirectSound minimum -10000). Pan: [contact audio](contact-effects.md#spatial-contact-audio).

| Effects master | Sample volume | Combined integer | DirectSound hundredths of dB |
| --- | --- | --- | --- |
|100|127|100|-207|
|100|100|78|-423|
|100|70|55|-726|
|50|70|27|-1344|
|100|60|47|-863|
|50|123|48|-845|

- Live changes follow Miles `2000e8e0`: recompute each active sample from its retained integer volume; never multiply an already converted gain by a second linear master factor. Initial controls (incl. pan) are set before playback; Mono/Stereo switches keep the requested values for restoration.

### Callers

Shared path: three motor layers, WHINE, WHIRR, rumble, skid, rolling surfaces, fading tails; impact/damage/prop/landing, wall scrape; frontend and cheat cues; start, pit, pause, finish, thunder. Source integer curves, sample choices, rates and allocation order stay in their callers. Finish/tire fades update integer volume before the shared conversion. The running-original [pause recording](pause-menu.md#running-original-sound-comparison) demonstrated this master stage. Full-game driving PCM and resampler waveform comparison remain separate acceptance; a curve/source-byte check does not claim those recordings.

### Curve evidence

Freestanding 32-bit probe executes canonical sample setter `2000e5a0`, live master setter `2000e8e0`, combination `2000de00` and x87 conversion `2000de70` / `200166b8` (only critical sections and the DirectSound callback stubbed). Two active samples across all 128×128 sample/master pairs give 32768 captured SetVolume arguments, raw output CRC32 `7844102a`; all agree with the integer/dB rule. The native Service test uses the 128-entry golden table for all 12928 UI0..100/sample0..127 combinations. Local reproducer `build/whine-miles-source/build/reference-miles-volume/`: `compare-miles-volume.py` CRC32 `7d5e0617`, `decibels.txt` CRC32 `c6ef7b32` (untracked).

### Effect/audio ordering

- Engine updates WHINE before Driving, with the same RNG and shift counter. Generic exhaust bursts consume no RNG; Type8 advances only the existing particle-table cursor and bypasses WHINE. Backfire (sample64) uses the shared Impact voice ownership and pause/exit cleanup.
- The rare generic backfire coinciding with WHINE has component PCM/source and
  integration-code coverage, rather than a dedicated driven native burst capture.
  Full original driving PCM and screenshot equality remain outside this acceptance.
