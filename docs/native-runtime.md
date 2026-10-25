# Native runtime

Build/test setup: [porting](porting.md). Source contracts: the linked feature documents.

## Menus and driving

Scope: ten tracks, Single Race with 15 physical rivals, qualifying, Time Trial/ghosts,
Championship/Showdown, local two-player split screen, up to eight Linkup humans.

Menu controls: arrows/Enter/Escape; gamepad stick or D-pad/South/East.
Left/Right changes options, cars and tracks; Up/Down selects transmission in
the garage. Names accept nine letters, digits or spaces; Backspace deletes.
Fresh state unlocks Donington GP and Silverstone. Extras → Unlocks exposes
the track/bonus-car switches, including Lavaland; effects retain original mode gates.

| Action | Default keyboard | Default gamepad |
| --- | --- | --- |
| Gas / brake | Up / Down | Right / left trigger |
| Steering | Left / Right | Left stick |
| Gear up / down | F / V | Right / left shoulder |
| Camera / rear view | C / hold Z | West / hold North |
| Horn / pause | Hold 1 / Escape | Hold East / Start |

Controller Setup configures nine actions per local profile. Player 1 Auto combines
keyboard with one eligible pad: keyboard steering takes priority, pedals use the
stronger input. South confirmation selects/saves the driving pad; Auto pins that
pad while retaining Auto. Pause/focus/unplug clears held input and rumble.
Raw joysticks/wheels support calibrated axes and original constant/sine FFB through
SDL Haptic. Select the device in Controller Setup, center the wheel/release pedals
before Configure, and use Wheel Options for feedback. Physical wheel/driver
acceptance remains open. See [controllers](controller-setup.md#raw-joysticks-and-wheels).

Qualifying starts with an automatic approach, hands control back at the first
ordered start crossing and measures three laps before classification/grid entry.
NoQual holds the clutch until green; automatic selects first, manual retains its
selected gear. In automatic mode, braking at rest selects reverse; gas returns
to first. Escape opens mode-specific pause/end/restart actions.

## Feature references

| Area | Contracts and behavior |
| --- | --- |
| Frontend | [Startup/artwork](frontend-startup.md), [track confirmation](frontend-selection.md#track-selection-and-frontend-commit-boundaries), [Extras/codes](cheats.md). |
| Settings | [Game Options](frontend-options.md), [graphics/profile Apply/Cancel](frontend-graphics.md), [controls](controller-setup.md). |
| Racing | [Session start](session-start.md), [qualifying finish](session-finish.md), [AI handoff](ai-driver.md), [Single Race](single-race.md), [HUD](session-hud.md). |
| Modes | [Time Trial](time-trial.md#time-trial-core), [its menus/records](time-trial.md#time-trial-frontend), [Championship](championship.md), [split screen](split-screen.md), [Linkup](implementation/04-multiplayer.md). |
| Vehicle | [Controls](vehicle-motion.md#local-keyboard-and-drivetrain-consumers), [motion/grip](vehicle-motion.md), [barriers](barrier-contact.md), [damage](contact-effects.md#original-car-damage-presentation), [contact effects](contact-effects.md), [original replay](reference-replay.md). |
| Scene | [Resources/weather/music](scene-resources.md), [placement](scene-rendering.md#start-placement-and-collision-surface), [terrain](scene-rendering.md), [body/camera setup](scene-rendering.md#initial-body-and-follow-camera), [models](scene-models.md), [visibility](scene-rendering.md#scene-visibility-and-sector-order). |
| Effects | [Shadows](visual-effects.md#original-vehicle-shadows), [prop contacts](prop-contacts.md), [pits](pit-lanes.md), [weather](visual-effects.md#atmospheric-weather), [wheel effects](visual-effects.md#wheel-and-surface-effects), [tyre marks](visual-effects.md#tyre-marks), [exhaust](visual-effects.md#exhaust-flames-and-backfire). |
| Camera/audio | [Views](race-cameras.md), [cockpit](cockpit.md), [engine](driving-audio.md#local-engine-audio), [driving/spatial audio](driving-audio.md#moving-follow-camera-and-local-driving-audio), [impact/HUD audio](driving-audio.md#barrier-audio-and-local-driving-hud), [pause](pause-menu.md), [CD music](music.md). |

Time Trial offers Weather and Load Best Lap before name/car/track selection.
No still records new laps and protects a faster disk record; Yes additionally
loads that track's saved ghost with its recorded car/weather/driver. End Time
Trial saves an improved complete lap and displays the original record pages.
Championship saves restart the selected session, rather than restoring in-lap physics.

Local Single Race Players: 2 has separate names/cars/controls/HUDs, shared pause,
Car Boost and results/restart. Graphics selects horizontal or vertical split.
Linkup Host/Join uses IPv4/port (default 27015), one human per process. Entering
the Waiting Room commits that driver; Escape withdraws. Host Start includes only
committed drivers. Loading accepts Escape, names missing peers and has a 120 s
deadline; results close the session.
[Local eight-peer capacity](implementation/04-multiplayer.md#n04c--local-eight-peer-capacity)
records the existing process-based evidence.

## Arguments and persistence

Pass arguments through `make run RUN_ARGS='...'`; CMake stores `TOCA_RUN_ARGS`.
CLI overrides saved settings, then defaults. `--save-settings` persists deliberate
overrides; diagnostics are never preferences. `--help` lists all options.

| Arguments | Meaning/default |
| --- | --- |
| `--resolution WIDTHxHEIGHT`, `--fullscreen on\|off` | 1280×720 (16:9), fullscreen. |
| `--fps-limit N`, `--vsync on\|off` | Default 120; 0 uncapped, otherwise 1–1000; VSync on. |
| `--graphics-detail 0..4` | Low/Medium/High/Very High/Mega; default 4 (Mega). |
| `--gpu-driver vulkan\|direct3d12\|metal` | Select an SDL GPU backend for this run; not saved. Windows defaults to Vulkan; other systems use SDL's automatic selection. |
| `--volume 0..1`, `--mute on\|off` | Master gain 1, unmuted. |
| `--data-root DIR` | Optional external data root; the complete versioned BFP/Vorbis bundle takes precedence. |
| `--config FILE`, `--state-dir DIR` | Override writable preferences/state roots. |
| `--menu-background 0..7` | Fixed startup motif for diagnostics; not saved. |
| `--network-host PORT`, `--network-join IP:PORT`, `--network-name NAME` | Diagnostic entry through the ordinary network service; not saved. |
| `--frames N`, `--screenshot FILE`, `--trace FILE`, `--audio-offline` | Bounded run, PNG capture, CSV traces and silent real audio mixing. |

GPU backend selection uses `--gpu-driver` first, then the `SDL_GPU_DRIVER`
environment variable, then the platform default. A selected backend must be
available on the host; unsupported selections fail at renderer creation.

The default portable root is `$XDG_DATA_HOME/toca/toca` or
`~/.local/share/toca/toca` on Linux and `%APPDATA%\toca\toca` on Windows.
Preferences use `preferences.cfg`; Extras
and earned unlocks use `saves/extras.cfg`. Time Trial uses
`time-trial/track-N.ghost` and independent `time-trial/records.dat`.
Both writes are atomic; only the records table has CRC32. Ghosts use native
millisecond timestamps, retain complete valid laps and silently skip unsupported
formats. See [storage](time-trial.md#native-recording-and-storage).

Version 1 text config keys: `width`, `height`, `fullscreen`, `vsync`, `fps_limit`,
`volume`, `mute`, `effects_volume`, `music_volume`, `game_music`, `stereo_effects`,
`graphics_detail`, `split_vertical`. Input stores `input_device` (SDL model GUID,
empty keyboard, `auto` for player 1, `joystick:GUID` for raw devices), `input_sensitivity`, `input_deadzone`,
`input_rumble`, nine `key_*`/`pad_*` bindings and player 2's `p2_*` equivalents.
Bindings encode scancode `k26`, gamepad button `b10`/axis half `a0-`, raw button
`j4` or calibrated raw axis `r1-:-32768:32767:32767` (minimum/rest/maximum).
Invalid values and write failures are errors. Sound/controller/detail edits save
immediately; display/split edits use Apply/Save/Cancel.
State writes create an exclusive temporary beside the destination, flush it and
replace the destination. Windows uses `_wopen` with `_O_EXCL` for both legacy
MSVCRT and UCRT; Unicode paths, replacement and failed-save cleanup are covered
by `native-preferences`.

## Assets and display

The build unpacks the 48 BFP archives and five Vorbis packs into the generated
`assets/original-data/` beside the executable (release ZIPs: `game-data/`); no
original installation is needed ([details](build-and-tests.md#versioned-runtime-data)).
`--data-root` supplies optional additional canonical data. The read-only store resolves
paths ASCII-insensitively in loose files/BFP members without runtime extraction and
rejects writable paths under both roots. Constant tables and translations are typed
native C++ data; `tourcars.exe` is not needed. The soundtrack resolves under
`assets/music/` relative to the executable, independent of working directory and
`--data-root`.

Mega uses optional per-car GLB/WebP with original Very High fallback, 500-unit
projection and extended object/rival ranges. Its 3D scene uses 2× MSAA when the
device supports exact color/depth samples; other profiles, HUD/menus and fallback
use one. Opaque Mega ground uses mipmaps/trilinear and 2× anisotropy; source
selection/physics remain canonical. [AA](visual-effects.md#anti-aliasing-2-msaa-in-the-mega-profile), [materials](mega-track-materials-plan.md).

UI retains source proportions, centered menu content and edge/center HUD anchors.
Widescreen extends decorative menu/loading sides; non-4:3 title uses a full wide
composition. Startup selects one of eight original car motifs, with a matching
blurred Mega panorama. [Presentation](frontend-startup.md), [HUD](session-hud.md).

## Trace fields

`--trace FILE` writes host CSV plus `FILE.simulation.csv`. Simulation rows retain
deadline/input/controls, gear/body speed and position, contact/barrier/recovery,
camera heading, cached HUD/start/lap values, front-wheel drive and audio layers.
Qualifying includes AI ownership, finish-loop gain, camera selection and rank.
Focus/re-entry boundaries stay visible; these are native diagnostics.

Host counters: `frame_prepare_ns`, `gpu_wait_ns`, `gpu_upload_ns`, `gpu_encode_ns`,
`gpu_submit_ns` are CPU/host-call times, not GPU timestamps. `gpu_draw_calls` counts
after consecutive-state merging; `gpu_buffer_growths` capacity changes;
`gpu_world_draw_calls` resident world/model draws; `gpu_model_draw_calls`,
`gpu_model_instances`, `gpu_instanced_draw_calls` model submissions, instances and
multi-instance submissions after ordered merging (zero for non-presented polls);
`finale_model_triangles` submitted convoy/driver source triangles before clipping.
