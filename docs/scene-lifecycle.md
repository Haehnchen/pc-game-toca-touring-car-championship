# Scene setup and loading boundary

Track confirmation enters the native scene (all ten tracks): selected car and
weather, qualifying/grid start, body contacts, follow camera, terrain, scenery,
horizon. Update/render run at independent native deadlines ([motion](vehicle-motion.md),
[simulation schedule](simulation-time.md)). Complete race progression remains separate work.
Escape opens the [pause menu](pause-menu.md); End Race → Race Again? → No
returns to Main with choices retained.

Public SDL-free `scene/setup.hpp`: `SceneSelection{track_id, car_id,
weather_choice, detail}`, `SceneSetup{selection, weather, horizon_index}`.
Weather choice 0 stays distinguishable from the resolved scene weather.

## Entry and loading

- `0046cb8a` commits Track `+1c`, returns frontend state 7. Car `+dc` committed
  by `00477586`; transmission `+e0` changes while browsing.
- `00478690`: optional config save (`0046ac35`), chosen weather `+a0` kept in
  `+640`, fade/clear, frontend and CD02 teardown via `00468e74` →
  `0047a305`/`0047a353`, calls `0044a6e0`.
- `0044a6e0` installs the scene dispatcher (`0044a560`); `0052c888=1` switches
  the next host dispatch (`00463410`) to `0044a370`, whose first entry calls the
  loader `0044f0a3`.
- Loader inputs: track, car, weather `+a0`, detail `+328`, laps `+18`,
  qualifying-disable `+20`, difficulty `+34`, name `+b0`, transmission `+e0`,
  player count `+ac`, profile/control/render fields. Native parameters: tracks
  0–9, cars 0–7, weather 0–5, detail 0–3, single player; bonus cars 8/9 and
  Extras modifiers also use this path ([runtime coverage](cheats.md)).
- `0044f0a3` order: graphics with `IMAGES/GAMEPAL.PAL`; session state; scene
  weather (`004512b0`); `DATA/CONFIG.BFP`; loading artwork (`00439820`); inputs
  (`0044fe55`); reopen CD audio (`0047a207`, not a playback start); HUD
  resources (`DATA/IMAGE640.BFP` at canonical size); track/cars/horizons
  (`0044e7bb`).

## Weather and resource selection

- `004512b0` normal-track default weather `[2,5,2,2,1,3,2,1,2]`; explicit 1–5
  override. Horizon variants: sunny 0, rain/snow 3, thunder 4; cloudy 1 for
  Donington GP/Brands Hatch, 2 for Thruxton/Croft/Snetterton.
  Silverstone/Oulton/Donington Short/Knockhill call `0044cb80(2)` for cloudy:
  `0049ff60() % 2 + 1`. The native resolver requires an explicitly injected
  result 1 or 2 and rejects missing/invalid.
- Application supplies the four randomized cloudy choices through an owned
  `SourceRandom`: `0049ff60` state `s*0x343fd+0x269ec3`, returns
  `(s>>16)&0x7fff`; startup seed `time(0)>>1` (`0046846b`). The full original call order across unported frontend/AI consumers is pending.
- Default Donington: weather 2, horizon variant 1. Lavaland: `004512b0` forces
  weather 1, horizon 0 (`LAVA03`); stored menu weather unchanged.
- `0044e7bb` → `0044e717`/`0044e673`: Donington GP uses
  `DATA/DONLKEEP.BFP`/`DATA/DONLFREE.BFP`. Detail `+328==3` keeps KEEP POINTS,
  other details use FREE LOWPOINT (condition is detail, not weather). FREE
  object detail members: 5 (detail 3), 6 (detail 1/2), 7 (detail 0).
- `0044df6e` loads eight normal car families, two slots each; `00451ccd` maps
  car to player slot `car_id*2`; allcars triple mapping via `0044dc6d`. Full car assembly,
  physics, materials and actor updates remain separate from resource decoding.

## Loading artwork, scene state and audio

- `00439820`: `DATA/LOADHIGH.BFP` for 640×480 (LOADMED/LOADLOW for smaller
  modes). Member 0 = loading font; track IDs 0–9 → members
  `[3,6,8,1,5,9,2,4,7,10]` (Donington GP = 3). `FE/LSCREENS/*` strings only
  identify text, not loose images. Adds localized length/laps/record text.
- `0043a030` sets the progress-step count; `0043a050` pumps host messages and
  draws the yellow bar at canonical `(32,289)`, max `257×15`. No minimum timer.
- Single Race: qualifying enabled (`+20=0`) → phase `+5f6=1`, state `+5f8=4`;
  disabled → phase 2, state 9 (`0044f0a3`). Tables from `0044a560`: update
  state 4 → `00447f60`, state 9 → `00448790` (first-entry `00426ec0` calls at
  `00448000`/`004488a7`, confirmed by disassembly; labels only in the C export).
  Music selector: enable `+31c`, table `004ae7a8`, Single Race CD tracks
  `[3,4,5,6,3,4,5,6,3,4]` by track ID, started after loading, not during the
  loading image.
- `0044a370` update callbacks at `004ed9b8 + state*4`, render at
  `004eda10 + state*4`. `0044a1d0` dispatches simulation/input/audio and polls
  `0047a3b4(0)` (race music does not use the frontend restart policy).
  Exit-to-frontend: `004638bb`, resets frontend-init and scene-switch flags,
  re-enters frontend init. Native return releases scene resources, reloads
  frontend samples/view, restarts CD02. The [pause-menu dispatch](pause-menu.md)
  and qualifying results are connected; full race-result dispatch remains pending.
- Native order: selected loading image presented before decoding scene
  resources/body state; progress after CPU and GPU stages; events processed
  between stages. Original profiles use canonical LOADHIGH; Mega uses the
  matching WebP with independent BFP fallback when the override is absent.
- The GPU loading stage prepares the actual initial scene frame and its
  graphics pipelines before the completed loading frame, music and simulation
  activation. Pipeline compilation runs on one worker while the main thread
  services events and redraws loading; command buffers and pipeline-cache
  installation stay on the main thread. Cold driver caches cannot strand the
  loading image after race audio has started. `gpu-pipeline-preparation` and
  `native-cold-start` cover loading presentation, cleanup and fresh defaults.
- CD03–CD06 start after the completed loading frame. Motor WAVs/curves load in
  the GPU/audio stage without reserving voices; scene activation starts three
  motor loops. On return, the Engine stops before sample teardown/voice reuse.
- RACE AGAIN (one-player NoQual/qualifying, local two-player) reuses the scene
  without loading screen or sample teardown: resets grid/rollout and active
  world pools, keeps resource-owned effect tables/clocks, new session clock
  after setup. Damage selection scratch stays process-owned even across a true
  reload. [Warm reset contract and limits](single-race.md#warm-world-reset-and-resource-lifetime).

## Widescreen loading presentation

Artwork and progress bar keep the centered, uniformly scaled 640×480 layout.
For 16:9/16:10 a separate background continues the fire/checkered decoration
into the side areas and fills the rest with the artwork's black base. Original
profile derives the decoration at runtime from clean parts of the selected
image; Mega supplies a shared `sides.webp`. Logos, lettering, progress and
miniatures stay central. Uses the shared UI side-background renderer.

## Mega loading bitmaps and original statistics

- Ten 2560×1920 WebP (quality 70, method 6) under `assets/ui/mega/loading/`
  keep LOADHIGH's split: header, blue captions, progress outline and miniature
  precomposed; yellow fill and dynamic text separate. Originals are read from
  the unchanged BFP; builds copy the finished images (also without Mega models).
- Authoring: imagegen produced one shared header, nine gray/brown terrain
  reliefs, transparent Lavaland decorations. `tools/compose-loading-artwork.py`
  places them at original coordinates, rebuilds yellow circuit centerlines and
  red direction markers as smooth geometry, rasterizes captions from continuous
  letter contours at source positions; Lavaland burn opening and red/silver
  motif retained. The nine circuit miniatures register original and authored
  deck corners, center circuit and direction marker on the visible deck with an
  inset from every edge; relief sidewalls excluded; encoded routes checked
  against deck edges.
- `439820` dynamic rows (eight): LOADHIGH member 0 and CONFIG GSTRINGS 77–81:
  length, laps/unlimited, total distance, record driver/time. English keeps the
  1600 m/mile conversion; distance multiplies truncated hundredths per lap.
  Time Trial uses the saved ghost record and `T.T` caption. All five languages
  keep source right alignment and row coordinates.
- Mega font: authored 67-glyph atlas, original cells/metrics at fourfold
  resolution (`tools/loading_font_geometry.py`); no host font.
- Network status uses the separate LIL640 font; its panel occupies the left
  miniature area when statistics are present.

## Integrated initial-scene acceptance

Honda's initial qualifying body heading differs from its grid-based camera
anchor (recovered initialization, before the moving camera update). No matched
original moving frame or complete raster fidelity is claimed.
