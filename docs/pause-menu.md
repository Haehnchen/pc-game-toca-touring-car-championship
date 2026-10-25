# In-game pause menu

Canonical TOCA 1.1a only (`tourcars.exe` CRC32 `8ce56d41`); existing decompilation and
read-only CONFIG/IMAGE640 members.

## Original menu and actions

`00441680` enters the overlay and saves the simulation/session state; `00440510` draws it;
`00441720` handles input. Single Race root:

| GSTRINGS line | English label | Action |
| --- | --- | --- |
| 24 | GAME PAUSED | Title |
| 25 | CONTINUE | Restore the saved session state |
| 26 | SOUND OPTIONS | Open the four-row sound submenu |
| 27 | END QUALIFYING SESSION | Finish qualifying immediately; qualifying only |
| 30 | END RACE | Open RACE AGAIN? |

- Line 27 condition: session kind 1/3, excluding phase 18 and Time Trial. The native Single
  Race caller supplies whether qualifying can still end. No Championship/Time Trial actions
  and no restart row in the controller (Time Trial rows: [frontend](time-trial.md#time-trial-frontend)).
- `00441720` action 3 ends qualifying without confirmation; keeps an existing best, fills
  an absent record with minutes 60, seconds 0, centiseconds 0 (`004cc5c2/4/6`): a
  **60-minute** sentinel (`0045e470` stores minutes as seconds/60 in `004cc5c2`). The
  continuation sets saved phase 7 → existing finish/classification flow.
- END RACE opens GSTRINGS38 RACE AGAIN?, YES (36) selected, then NO (37). YES sets
  `004eda8c`; `00449f50` restarts qualifying or the grid per the original NoQual setting,
  independent of the active phase. NO sets `004dcd8c` (ends session). Escape resumes
  directly from root and Sound Options; RACE AGAIN? clears `004dcdbc` in draw case 3, so
  Escape leaves that page open until YES/NO. Up/Down wraps; dispatch priority Down, Up,
  Left, Right, Enter, Escape.

## Sound options

- `00440510` case 1: GSTRINGS31/32 (effects volume or OFF), 33 (music volume), 34/82
  (in-game music OFF/ON), 35 (DONE). `004402d0`, `00440310`, `00440350`, `00440390`
  change effects/CD volume independently by 10 within 0..100. Left or Right toggles
  music via `00441c30`: OFF stops CD, ON restarts the authored track via `00426ec0`.
  Enter on DONE returns to Continue; Enter on an adjustment row plays its confirmation cue
  without changing the value (source switch).
- `00426320` maps navigation, confirmation, successful adjustment to race samples
  78, 79, 80 (race resources, distinct from frontend menu samples). Escape: no cue.

## Presentation and implementation

- `0042bee0` reads CONFIG members 2..6, splits CR/LF runs into GSTRINGS; native reads the
  same lines for English, French, German, Italian, Spanish. IMAGE640 members 23/24 =
  BIG640/LIL640. `004652ae`: RGB 200 title/selected row, RGB 50 otherwise. Translucent
  panel RGBA `0a0a0a90` from `004adeb0`.
- Layout: title at normalized `0x4ccc` (y 143 at 640×480); rows start from the title's
  returned font baseline minus `0xa3d`, advance `0xf5c` (~29 px). Panel starts at y 186,
  +40 px around the widest text, width capped at 90%. Native keeps this centered 640×480
  canvas at 4:3, 16:9, 16:10.
- `scene::PauseController`: menu state, emits actions, changed audio settings, source
  sample IDs. `scene::PauseView`: appends original fonts and panel to the scene frame.
  Application: pausing simulation deadlines, focus/input clearing, audio playback,
  preference saving, session transitions. Source captions are hidden while the pause page
  is visible.

## Integrated acceptance

`native-pause` covers ESC/Continue, frozen car/lap/flag/HOR/prop state, focus loss while
paused, cleared held inputs, independent saved audio settings, CD toggle, unmeasured
qualifying end, all 16 result rows, classified grid start, Race Again using the original
qualifying preference, return to Main/CD02, RACE AGAIN Escape gate.

## Running-original visual comparison

Unchanged 1.1a on a private muted Xvfb (640×480 window only): English qualifying, Escape →
root, Sound Options, End Race → RACE AGAIN?. Original profile effects 100 / music 40.
Holding Escape 150 ms on RACE AGAIN? left the window byte-identical (native ignores Escape
there).

Panel rounding: `00440510` converts an even pixel width to Q16 and `0046557f` converts it
back; `0049d0c0` draws 4×4 equal tiles, truncating each quarter to whole pixels. Native
panel (original = corrected):

| English screen | Panel x,y,width,height |
| --- | --- |
| Pause root | 116,186,404,128 |
| Sound Options | 88,186,460,128 |
| Race Again | 122,186,392,68 |

German panels: 140,186,356,128 / 86,186,464,128 / 32,186,572,68. All compared English
(51,968) and German (20,416/25,750/15,184) glyph pixels, incl. font shadows, match the
original position and RGB exactly (title placement, baselines, kerning, selected/
unselected colors); panel RGB 10/alpha 144 agrees within one channel level. English/German
need no wrapping or font changes. Establishes the overlay only; underlying 3D scene and
HUD are outside the pixel comparison.

## Running-original sound comparison

Measured with the unchanged game (`8ce56d41`) and supplied Miles DLL (`1012e9cb`): temporary
Wine prefix, private Xvfb, `winepulse` only, private PulseAudio with
`module-native-protocol-unix` + `module-null-sink` (one stereo 44100 Hz sink and monitor);
captures contain the game's output. Cues 78/79/80 recorded at effects 0/50/100 via keyboard.
Effects 0 is exactly silent; both channels match. WineDbg: digital master 100, Miles
DirectSound backend. Music 40 is a saved profile value, not an original default.

Omitted stage found: Miles `2000de00` combines `floor(master*sample_volume/127)`; `2000de70`
converts that integer to truncated hundredths of a dB with `-2000*log10(127/value)`. For
pause sample volume 100: effects 100 → 78 (−4.23 dB); effects 50 → 39 (−10.25 dB); 0 → silence.
`Service::play_original_volume` composes this path for pause cues, incl. live master
changes; reused generic voices keep the linear-gain contract. The same curve serves all
implemented frontend/driving sample callers ([volume integration](driving-audio.md#original-miles-sample-volume)).

Corrected native vs original at effects 100: RMS ratios 0.991234 / 0.988897 / 0.984416
(cues 78/79/80), waveform correlation 0.999205 / 0.998519 / 0.997818; across effects 50/100
RMS ratios 0.982071..0.991234. Original active signal spans 16..25 additional 44100 Hz frames
(0.36..0.57 ms); alignment needs 2..7 frames. Small resampling/filter/quantization
differences remain: establishes cue identity, rate, gain behavior and centered pan, not
bit-identical PCM. `tests/audio/test_pause_audio.cpp` supports
`--dump CANONICAL_ROOT EXISTING_OUTPUT_DIRECTORY` for the native service.

CD playback was not armed in this folder-mapped reference run (track −1), so there is no
original CD PCM comparison; pause/no-stop and music-toggle behavior rest on the recovered
callers and native integration checks.
