# Game Options

Game Options (`00469d0c`, menu `004b7640`): Sound Setup, Graphics Setup,
Controller Setup, Back (icon IDs 33, 58, 20, 16). The native hub uses those
localized labels/help; English/French/German tables are read from the unchanged
`tourcars.exe`.

Sound Setup: frontend menu `00475361`/`0047591d` at `004b9570`, five normal icon
rows (not the pause overlay):

| Row | Original label/value | Label table | Icon |
| --- | --- | --- | --- |
| 0 | Effects Volume 0–100; Effects Off at zero | `004b1210`, `004b1240`, `004b12b8` | 19 |
| 1 | CD Music Volume 0–100; CD Music Off at zero | `004b1228`, `004b1240`, `004b12b8` | 18 |
| 2 | Game Music On/Off | `004b1258`, `004b12a0`, `004b12b8` | 18 |
| 3 | Stereo SFX / Mono SFX | `004b1288`, `004b1270` | 14 |
| 4 | Back | `004b0ad8` | 16 |

- `00475519`/`00475580` build volumes with `%s %s %d` (`004b96f0`) or `%s %s`
  (`004aeb60`) for Off; `004755e5` formats Game Music; `004754f3` selects
  Stereo/Mono. Title `004b11f8`; help tables `004b12d0`, `004b12e8`, `004b1318`,
  `004b1300`, `004b0af0`.
- `00475645`/`004756bc` and `00475768`/`004757df` adjust volumes in ten-point
  steps with integer `volume/10` counters initialized in `0047591d`. Dispatcher
  `0046e6c2` respects arrow bounds for Left/Right; Enter calls the right
  callback directly and cycles 100 to zero. Game Music (`0047588b`) and
  Stereo/Mono (`004758d4`) toggle with either arrow or Enter.
- Stereo flag: config `+0x320` (sound block `+0x14`). `004244f0` supplies the
  mode to `0047939c`, cached in `004d3d20`; `0047939c`/`0047952f` select one or
  two PCM output channels. Spatial effect callers (`00424860`, `00425d20`) use
  pan 64 in mono mode. CD audio separate. Native: mono mode downmixes stereo
  source channels to their average and centers directional pans; switching back
  restores the requested pan; CD music stays stereo. Native master gain/mute
  remain config/CLI preferences.
- Native Sound Setup saves each change immediately and reports persistence
  failures.

Graphics Setup keeps native resolution, fullscreen, VSync, FPS-limit controls,
Apply and Save at the bottom of page 1; page 2: detail presets plus Mega, Split,
Back; detail changes apply/save immediately, display/split edits stay staged
([graphics](frontend-graphics.md)). Graphics
and Controller Back use original icon rows; ≤5 entries per page.

Controller Setup selects one of two local players or Back. Each player's page:
five rows (device, Configure selected device, sensitivity, Gamepad Options,
Back). Gamepad Options holds native deadzone, vibration, defaults in place of
the original external Windows joystick panel. Original capture order and menu
cues use the same immediate save/error path; Escape discards partial binding
sets; fixed menu controls stay available during device changes
([controller source contracts](controller-setup.md)).

## First frontend audio path

Evidence: 1.1a decompiled code plus the sample-name pointer table read from the
unchanged executable.

| Original site | Contract |
| --- | --- |
| `0x004688fc` → `0x004681b1` | First startup: title (`0x004684ac`, five seconds/input), then copyright (`0x00468652`) before frontend device/sample init. Title confirmation has no sample cue. |
| `0x004688fc` after frontend image/font setup | Init audio, open CD, load frontend samples, start CD02 using config `+0x30c`; only then return the initial frontend state (incl. language selection). |
| `0x0046dc17` → `0x004796eb`, table `0x004b8298` | Loose `FE/SOUNDS/` WAVs in index order: UPDOWN, OPTION, FORWBACK, TYPE, CANNOT, CME_E/F/G/I/S, BAD. FRONTEND.BFP supplies visuals, not these WAVs. |
| `0x0046e6c2` → `0x00475220` | Navigation sample 0; confirm/back 2; disabled/unavailable 4. Single Race selection adds option adjustments 1 and name append/delete 3. |
| `0x00475220` → `0x004798cb` | Gate config `+0x318`; one total play, volume 70, pan 64. Integer sample/master volume combined before the [Miles dB conversion](driving-audio.md#original-miles-sample-volume); centered stereo, live effects settings. |
| `0x00475361` | Music config `+0x30c`, sample-master `+0x310`: menu endpoints 0..100. The original persisted default is not established here; the native global gain remains an explicit native setting. |
| `0x0047a258` | Start a finite music track; remember ID, gain, start time, duration + 2000 ms. Separate SDL_mixer track leaves all 12 sample voices available. |
| `0x0047a3b4` | Restart needs elapsed ms strictly > duration+2000, completed status 3, a selected track and the caller's restart flag; disabled restart disarms the finished request. No infinite mixer loop. |
| `0x0047a305`, `0x00468e74` | Explicit stop clears track/restart state; frontend shutdown stops CD playback. |
| `0x00463410` | Frontend checks music after its counter exceeds 60; presentation FPS does not supply the music clock. |

- `audio::Service::load_sample` predecodes caller-owned WAV or Ogg byte spans,
  returns the decoded source rate. The file loader prefers the bundled `.ogg`;
  canonical reference loading keeps the original WAVs. Music opens the
  resource-root Ogg path with deferred decoding (game-data paths and bundled
  music roots separate). Global mute/gain through SDL_mixer, incl. offline
  output. `audio::load_frontend_samples` and `play_frontend_cue` bridge the
  controller route without changing shared game-event structures.
- CD02 continues through name, garage, track selection and Back without
  restarting. Track confirmation stays unavailable until the race loader can
  perform frontend teardown and its CD stop boundary. No Miles waveform-fidelity claim.

## Frontend and car resource contracts

Bounded 1.1a export reads. Consumers: `src/frontend/resources.cpp`; checks:
`tests/frontend/test_resources.cpp`. Shared BMP/WebP decoding and its owning
surface type: `src/image/decode.hpp` (palette restoration, alpha handling, image
bounds keep their contracts).

### Car triples

`0x0044dc6d` opens `DATA/ALLCARS.BFP`, skips the selected starting index, reads
exactly three members, passes each to `0x00494a40`. The native consumer keeps the
cursor sequence and calls `parse_cm3d` while each immutable archive member owns
its bytes.

| Original car ID | Member indices | Catalog identity |
| --- | --- | --- |
| 0 | 1, 2, 3 | Honda |
| 1 | 4, 5, 6 | Audi |
| 2 | 7, 8, 9 | Vectra |
| 3 | 13, 14, 15 | Volvo |
| 4 | 16, 17, 18 | Ford |
| 5 | 19, 20, 21 | Nissan |
| 6 | 22, 23, 24 | Peugeot (`p406`) |
| 7 | 25, 26, 27 | Renault (`ren`) |
| 8 | 28, 29, 30 | Tank |
| 9 | 31, 32, 33 | Flex |
| 10 | 10, 11, 12 | Vectra2 |

Stored names are truncated labels; identity = archive + index. Ordered members
are high/medium/low; the loader selects no detail level, variant, camera or
damage state. `0x0044df6e` loads the triples in the scene path (`0x0044e7bb`,
normal loop selects Honda for the first pair); frontend init `0x004688fc` loads
frontend images/fonts/audio but no 3D cars (no Honda preload at title). The
selected car is consumed by the [initial scene](scene-lifecycle.md).

### Title BMP and external PAL

- `0x004684ac` reads `FE/IMAGES/TITLE.BMP` via the BMP converter `0x00483261`,
  then `FE/IMAGES/TITLE.PAL` via `0x0048293d`, applied by the palette/fade
  routine `0x0049cb40`. Fade/timer/input behavior is outside this loader.
- Native: SDL IO decodes BMPs keeping indexed surfaces/palettes; TITLE requires
  640×480 and applies the external palette before upload; its 256 RGB entries
  match the embedded BMP palette.
- `0x0048293d` copies 256 four-byte records from offset `0x18`, keeping the
  first three bytes as R/G/B (the fourth is not alpha). Native decoding bounds
  this region by the real input span and checks RIFF, PAL/data markers, version
  `0x0300`, entry count 256. The RIFF size field is not trusted (canonical
  TITLE.PAL declares an inconsistent size); no palette correction or rewrite.

### Initial C01 findings

The font and compiled frontend-string consumers below ship in
[startup](frontend-startup.md) and [selection](frontend-selection.md).
CONFIG scene strings remain separate unfinished work.

- FRONTEND initialization `0x004688fc` calls `0x0046ff22`, which consumes
  24 image members (indices 0..23), before four calls to `0x0046418e` consume
  indices 24..27: OPTION3, OPTION3S, HELP3 and TITLE3 fonts. Cursor continuation
  then supplies the cursor and subsequent frontend images through other callees.
- Font parser `0x0046427f` accepts uncompressed eight-bit BMPs. Its appendix
  starts at `0x436 + align_up(width,4)*height`: a 24-byte header followed by
  `header[1]` records of 16 bytes. Glyph lookup `0x00464398` matches the first
  byte of each record. Draw consumer `0x0046473a` reads little-endian x/y/width/
  height at record offsets 2/4/6/8. `0x0046400e` combines signed bytes 10..12
  from one glyph with 13..15 from its neighbor for spacing. Unknown header and
  glyph fields should remain explicit until their consumers are implemented.
- CONFIG consumption in the scene path is ordered: `0x00420460` reads member
  0 (AIBIN); `0x00444670` reads member 1 (QUALPC); `0x0042bee0` skips language
  ID 0..4, then reads member `2 + language_id`, mapping E/F/G/I/S string tables.
  The parser copies bytes and replaces contiguous CR/LF runs with NULs while
  indexing lines. Encoding and all string-ID consumers remain to be confirmed.
- Frontend localization `0x00465610` stores the language and `0x0046561f`
  indexes a compiled pointer table by that language. These frontend strings
  are a separate contract from the CONFIG/GSTRINGS scene strings; they should
  not be conflated merely because both vary by language.
