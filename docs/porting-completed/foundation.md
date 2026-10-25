# Completed: foundation and scene startup

Current work: [porting plan](../porting-plan.md). Source contracts and evidence live in the
linked documents.

## Baseline

- Canonical patched 1.1a, isolated Wine launcher, Ghidra export; BFP extraction/catalog and
  CM3D triple reader verified; canonical files unchanged.
- SDL GPU feasibility (all 33 cars, title/state readback, shader translation):
  [graphics backend contract](../graphics-prototype.md).
- Native application: all ten tracks, driving/contact/damage, qualifying, Single Race, Time
  Trial, Championship, two-player split screen, eight-human Linkup, each with its recovered
  menu, session and audio consumers; display/settings, original-profile rendering and Mega
  replacements integrated. Records: [frontend](frontend.md), [tracks](tracks.md),
  [gameplay](gameplay.md), [Mega](mega.md).

## Functional release

Snapshot `4c851969` builds the complete Linux Release with bundled runtime data,
libraries and existing Mega assets. Package:
`dist/functional-4c851969/toca-Linux-x86_64.zip` (151,507,087 bytes), CRC32 `67e49300`;
contained stripped executable CRC32 `923d2ed5`.

`verify-release.py --platform Linux` passed. The existing native `scene` fixture
ran from the extracted ZIP and its own `game-data`: menu → loading → qualifying
at three aspects → menu → race re-entry, CD02/03 handoff, window-only PNGs and
clean exit. Evidence: `build/test-artifacts/functional-release-menu-scene-t9yai4zg/`.
Accepted gameplay/CPU/sanitizer evidence is reused; functional completion has no
remaining blocker from this closing check. Further artwork/original fidelity
stays separately scoped in the porting plan.

## Common application

F01/F02/W01, initial G08 audio, G07 preferences, F03 evidence, F05a input/time/event/settings
contracts live in `toca-game` through `make run`; the former viewer entry point is retired,
synthetic graphics fixtures remain tests. F01 specifies build, formatting, isolation independent
of other projects ([foundation contracts](../implementation/01-foundation.md)). Covered: 4:3,
16:9, 16:10, input, resize, exit, config/CLI display and audio settings, dummy/offline audio on
private Xvfb.

## Canonical data and frontend route

F04 reads loose files and BFP members directly with original case-insensitive paths, title
BMP/PAL and car-ID/triple selection (no extracted runtime tree). C01: language → Main → Single
Race options → difficulty → name → 2D garage → rotating track preview → scene; original fonts,
localization, unlock rules, retained Back choices, sample cues, CD02. Menu-loaded scene resources
initialize at scene entry as in the source ([startup](../frontend-startup.md),
[track preview](../frontend-selection.md#track-selection-resources-and-preview)).

## Scene integration

| Step | Implemented behavior | Contract |
| --- | --- | --- |
| 1 | Donington resource chain `478690→44a6e0/44a560→44a370→44f0a3`, setup/weather, lifecycle/audio handoff. | [resources](../scene-resources.md) |
| 2 | HPC height queries (separate from mesh loading), qualifying/grid XZ/heading, aspect-aware projection, bounded terrain TL pass. | [placement](../scene-rendering.md#start-placement-and-collision-surface), [terrain](../scene-rendering.md) |
| 3 | Selected body/scenery, initial follow camera, Track confirmation; CD02/frontend samples stop, finite CD03 starts; scene exit restores frontend resources/audio. | [lifecycle](../scene-lifecycle.md#integrated-initial-scene-acceptance), [body](../scene-rendering.md#initial-body-and-follow-camera) |
| 4 | Keyboard filters, gears/automatic reverse, motor/drive moments, three engine layers at independent 25 Hz deadlines; original QPC arithmetic kept for replay. | [timing](../simulation-time.md), [controls](../vehicle-motion.md#local-keyboard-and-drivetrain-consumers), [audio](../driving-audio.md#local-engine-audio) |
| 5 | Wheel/contact forces and body integration `40b360/40d830/40caf0→40f5c0`, moving wheels/follow camera, WHIRR, contact rumble. | [motion](../vehicle-motion.md), [camera/audio](../driving-audio.md#moving-follow-camera-and-local-driving-audio) |
| 6 | POINTS/LOWPOINT static barriers, `409c30` impulses, impact audio, speed/gear/RPM HUD. | [barriers](../barrier-contact.md), [HUD/audio](../driving-audio.md#barrier-audio-and-local-driving-hud) |
| 7 | Sector candidate lists, NoQual clutch/brake hold, launch tire adjustment, ordered route/lap records, start lamps/cues 69/70, State 9 audio. | [session](../session-start.md#integrated-acceptance) |
| 8 | Linked kind 2/3 recovery (entry, saved state, motion, exit); lap/time HUD, corrected gear atlas row, diagnostic recovery state. | [recovery](../barrier-contact.md), [HUD](../session-hud.md#integrated-acceptance) |

Track-preview correction: defined light-frame coordinates keep the source initialization
ambient light, reduced for locked tracks ([preview acceptance](../frontend-selection.md#native-light-repair)).

## Completed: iteration tooling and settings integration

### Focused builds and trace readers

Make derives required targets/dependencies from CTest's selected cases; explicit overrides stay;
listing builds nothing. Native fixtures share incremental CSV reads, keeping complete records and
partial writes across polls, re-entry and file restart. Usage: [porting setup](../porting.md);
[F01](../implementation/01-foundation.md).

### Diagnostic stepping

Synchronous opt-in clock/action protocol keeps normal 40 ms simulation, player input,
body/contact, audio and HUD. External feedback supplies one input per deadline; physical-keyboard
real-time mode remains for pacing work. Presentation samples every eight steps and at lap
transitions. A full native lap runs in about 32 s (baseline about 221 s); Time Trial lap/save/
fresh-process ghost playback matches all 3600 saved poses against the body trace.
[Protocol](../full-lap.md#faster-diagnostic-scheduling).

### Settings and original menu integration

C01 connects ordinary language/menu/selection/scene loading directly to canonical data (no
extraction or Wine; menu samples and CD02 included). Settings use the shared display/audio
services with effective values, Apply/Save/Cancel, write-error recovery
([native settings](../frontend-startup.md#native-settings)). Original Game Options hub and
five-row Sound Setup apply/save immediately; native graphics controls use their submenu
([options](../frontend-options.md)).

### Setup prerequisites

Direct archive loading, live simulation scheduling, short original input/time/state
recording/replay, two-profile input/preferences, N01 network recovery shipped with their owning
packages. Real Miles menu PCM uses a private null sink; inter-update WHINE recording uses the same
isolated audio principle. [controls/storage](frontend.md),
[replay/audio/network](gameplay.md), [Miles PCM](../pause-menu.md). F05b startup/frame recovery,
remaining numeric/save structures, broader driving-PCM fidelity stay with the implementation plans.
