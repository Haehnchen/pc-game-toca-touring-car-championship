# Completed: frontend, controls and cameras

Current work: [porting plan](../porting-plan.md).

## Display, HUD and loading

- Widescreen: generated full title composition outside 4:3; menus keep centered source
  proportions with extended backgrounds; Mega has eight blurred panoramas and a sharp frame
  ([startup](../frontend-startup.md)).
- HUD anchors keep original proportions, generated Mega artwork, cached gameplay values
  ([HUD](../session-hud.md)). Loading keeps the original image/progress layout with extended
  decorative sides; ten Mega images generated and precomposed; localized length/laps/distance/
  record rows use original bitmapfont metrics and an authored Mega atlas
  ([loading](../scene-lifecycle.md#mega-loading-bitmaps-and-original-statistics)).
- Game Options connects Sound/Graphics/Controller; display Apply/Cancel, four source detail
  levels plus Mega use shared preferences; Mega GLB/WebP overrides fall back per car to Very High
  ([graphics settings](../frontend-graphics.md)).

## Codes and Extras

C06/G09: 22 original table entries, 21 accepted codes; CMDISCO is rejected by 1.1a. All 19
reversible menu switches have runtime consumers, original activation audio, Championship/Linkup
gates; disabling preserves earned progress. Car-contact suppression, Showdown, light particles,
both AI speed fields, CODIES human-name ownership connected. [Code catalog](../cheats.md).

## Random menu backgrounds

One startup draw selects index 0–7 for the application lifetime (also across view recreation and
race returns). Original image/palette/alias pairs and matching Mega panoramas keep all eight car
motifs; builds copy finished WebPs. Fourteen-phase forward/reverse rolls use the recovered 30 Hz
clock and image 3; image 4 is unreachable. [Member mapping](../frontend-startup.md#random-menu-backgrounds),
[transition contract](../frontend-startup.md#menu-page-turns).

## Mega menu icons

39 reachable symbols with normal/selected artwork, unavailable states, navigation arrows. Ordered
overlays keep source logical cells, row positions, localization, font layering; Mega preview/
Apply/Cancel with original fallback. Crops and CRC32: [icon contract](../frontend-startup.md#mega-menu-icons).

## Mega selection garages

GR00–GR05 cover all 19 views from four archives: 13 generated empty backgrounds and 19 manually
rendered vehicle layers (ten models, cropped neighbors, interiors, Renault debris, destroyed Tank
bay, Flex graffiti/damage); Mega models, otherwise original HIGH, keep source pose, framing,
livery; layers are cropped WebP80. Manual native renderer publishes source layers; manual
composition publishes 19 committed opaque 1146×714 WebP70/method 6 images; 19 original 573×357
views live in `original/garage/`, same filenames under `mega/garage/`. One loader picks base,
then an available Mega override at detail 4, with the original unlock mapping and UI layering; no
runtime GAR strips or BFP fallback; ZIP excludes GAR1–4. [Garage plan](../mega-garage-plan.md).

## Pause menu

Localized overlays freeze simulation/scene time, clear driving input, keep CD playback running.
Qualifying skip keeps a measured best or the original 60-minute no-time value, then shows
classification and the earned grid. Race Again keeps the qualifying choice; No returns to Main.
Effects/music volume and in-game music toggle persist independently. Live English/German
comparison and Miles PCM at effects 0/50/100 corrected panel rounding, Escape gating and
integer/DirectSound cue gain; measured SRC differences stay in [pause evidence](../pause-menu.md).

| Mode | Lifecycle | Contract |
| --- | --- | --- |
| Championship | END CHAMPIONSHIP, NO-default confirmation, qualifying rules, stage checkpoint continuation. | [Championship](../championship.md) |
| Time Trial | END TIME TRIAL confirmation, conditional best-run/ghost save via `45e7a0`. | [Time Trial](../time-trial.md#time-trial-core) |
| Local two-player | Shared pause/focus freeze, independent input/sound, both-human finish, two-row results, restart; mutual contacts, both pits, independent times. | [Split screen](../split-screen.md) |
| Linkup | Pause ownership, receive processing during overlays/focus, clock freeze, leave/restart permissions, results. | [N04b](../implementation/04-multiplayer.md#n04b--complete-race-pause-and-results) |

## Controller Setup

Original player selector and five-row pages configure selected devices, keyboard/SDL gamepad/raw
wheel bindings, three sensitivity levels; nine actions incl. rear view, camera change, horn;
timed OK feedback, single-action/full capture, cancel/Back, fixed menu controls, persistent device
selection. Deadzone, vibration/force feedback, defaults on a separate device options page; two
local profiles with independent maps/devices. Player 1 Auto combines keyboard and one eligible pad,
reserves player 2's device, keeps both maps; held keyboard steering wins, pedals use stronger
input; focus/hotplug clears held input; South confirm selects the driving pad.
[Controller contracts](../controller-setup.md). Raw wheels: calibrated axes, endpoint pedals,
directed constant/sine SDL Haptic output; physical wheel/driver and two-pad feel acceptance remain
open ([hardware boundary](../controller-setup.md#raw-joysticks-and-wheels), [horn/feedback](../horn.md)).

## Native ghost storage

One atomic `.ghost` schema with fixed millisecond timestamps (user decision); original-file
exchange, frequency conversion, duplicate save targets and export-only split state removed.
Ghosts have no checksum, unsupported formats are silently skipped; the independent records table
keeps CRC32. The source 3600-pose limit is removed: complete valid laps are retained with bounded
loading, incl. laps > 144 s. The approach uses the original Time Trial caption.
[Recording/storage](../time-trial.md#time-trial-core).

## Race cameras and cockpit

C cycles cockpit, two chase distances, bonnet, source-enabled trackside views; held Z = rear view;
CMFOLLOW/F5 controls trackside selection. Per-car offsets, projection, camera-relative sound,
steering/wipers, CMCHUN/CMHANDY/CMMICRO connected. Camera 0 follows the actual HOR helicopter with
original flight, CMCOPTER/CMFOLLOW priority, rear restoration, track/weather gates. Committed
shifts trigger hand/regrip motion; Flex keeps its parked hand. The original race renderer has no
live mirror pass. [Cameras](../race-cameras.md), [cockpit](../cockpit.md).

Config `+24/+28` are projection-mirror flags (reversed steering correction;
[evidence](../vehicle-motion.md#local-keyboard-and-drivetrain-consumers)).
