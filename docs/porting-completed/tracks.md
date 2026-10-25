# Completed: tracks, effects and rendering comparisons

Current work: [porting plan](../porting-plan.md).

## Selectable tracks

All ten canonical tracks load original geometry, HPC contacts, qualifying/grid starts, scenery,
weather, lighting, loading artwork through the shared catalog. Normal Track confirmation enters
driving; Back/re-entry retains selections.

| Package | Track-specific result | Contract |
| --- | --- | --- |
| Silverstone | Shared scene, CD02→CD04→CD02, qualifying/grid re-entry. | [Integration](../scene-resources.md#silverstone-integration) |
| Thruxton | Reduced-detail route differences, source weather/light, CD05. | [Integration](../scene-resources.md#thruxton-integration) |
| Brands Hatch | BRANKEEP/FREE/HOR, original starts, CD06. | [Integration](../scene-resources.md#brands-hatch-integration) |
| Remaining roster | Oulton Park, Donington Short, Croft, Knockhill, Snetterton, Lavaland; track music and cloudy RNG consumption for all four source callers. | [Content matrix](../scene-resources.md#remaining-track-integration) |

Lavaland keeps forced weather, authored horizon and skipped empty TREES member. Qualifying data is
validated for every track/difficulty incl. rain/snow. Earned unlocks stay distinct from content
integration.

## C05a: props and starting cameras

CONES/POSTS/SIGNS/FLAGS keep original placement, detail gates, map exceptions; flags advance in
active simulation time. Qualifying uses `45eb60`'s camera-angle table; ordinary race views keep
grid heading. [Source/acceptance](../scene-resources.md#track-props-and-starting-cameras).

## C05b: moving horizon actors

HOR members 0/1: source fixed-point motion, hierarchies, mesh visibility, scene order, rotor
switching, track/weather gates; state persists through qualifying→race, resets on a newly loaded
scene; actors with no source sound caller stay silent. [HOR](../scene-resources.md#moving-horizon-actors).

## C05c: prop contacts

Cones move/tumble, posts bend, signs tip/break with fragments and source sounds; post impulses
enter the next vehicle update; grid restart restores objects, focus loss freezes them; all ten
tracks construct their source controller sets (Lavaland omissions). [Prop contacts](../prop-contacts.md),
[C05 tasks](../implementation/02-content.md#c05--remaining-tracks-dashboards-and-race-content).

## C05d: pit-lane guidance

All ten KEEP PITS members decode owned Left/Best/Over/Right records with original metadata/
precision. Forward entry hands steering/speed to AI, temporarily suppresses static barriers,
returns controls at ordered exit; route/lap progress continues. Original captions/sample 75 join
pause and classified grid restart; Lavaland keeps its exclusion; no stationary pit service in the
recovered source; opponent pit decisions stay with G05/G06. [PITS](../pit-lanes.md).

## C05f: wheel and surface effects

Original 80-slot smoke/debris pool, contact/slip gates, wet trails, shared-table order use
retained physics-step inputs; track texture groups, sprite order, spray blend, bounded GPU queue
join normal driving, pause/reset and tire/surface audio. [Surface effects](../visual-effects.md#wheel-and-surface-effects).

## C05g: tyre marks

Original slip/contact gates, quad geometry, track/weather colors, 100-slot ring, rain fading,
cursor/reset persistence; the untextured pass draws after terrain/before car shadows with source
blend/depth state. [Tyre marks](../visual-effects.md#tyre-marks).

## C05h/C04a: exhaust and spatial visibility

`4209d0/420bb0/420be0` gates BANG2 by engine speed, three-tick burst/cooldown/reset and Time Trial
car 8; shared sprite order and event 17→sound 64 keep pause timing; no separate engine smoke/fire
trigger in the damage code ([exhaust](../visual-effects.md#exhaust-flames-and-backfire)). `47cf40/47d3d0/47db00/47de00` supply
ordered terrain sectors, scenery bounds, root registration/migration, detail and visibility masks;
terrain/static/dynamic/effect drawing use source order with actual 4:3/16:9/16:10 projection
([visibility](../scene-rendering.md#scene-visibility-and-sector-order)).

## C05i: complete dry laps

All ten CPU and normal-menu player-input laps traverse every ordered route point with continuous
wheel/body/HPC/barrier state, exact native timing/HUD, zero barrier contacts/motion boundaries;
nine PITS traversals. [Track acceptance](../track-acceptance.md). Native driving coverage; a matched original Renault/Donington lap now also passes
[retained motion and route/timing comparison](../full-lap.md#scope).

## C05j/C02: weather laps and matched scenes

All 20 normal-menu rain/snow laps pass incl. Lavaland's forced-dry variants (precipitation,
surface trails, tire audio, route order, timing/HUD, supported ground, scene exit); Snetterton snow
uses source cached-ground-face behavior from `ce3416b` ([weather matrix](../track-acceptance.md)).

Original/native numeric coverage includes all ten tracks and Donington rain/snow;
the preserved 22 car/detail cases match all 424 node worlds. Schema2 verifies completed
presentation. `TOCA_RENDER_12` replays up to 64 preceding numerical presentations,
including actual C-key switches, through two retained color targets A/B/A; no original
pixels are imported. Canonical ghost HIGH/MEDIUM/LOW transitions, the opaque
SGRID/start-line pool and source-ordered effects use the shared native consumers.
[Contracts and raw raster scope](../graphics-comparison.md).

Separate compile-time world/reference modules and exact normal binary32 decoding
resolve Vulkan pipeline startup. GPU/native Time Trial, representative Mesa ASan/UBSan
and reader checks pass. C05j/C02 is complete and accepted. Original HUD TL/UV stores
and DASH depth/order are integrated and covered by cockpit/HUD checks.

One original full lap matches 3647×49 motion fields and 3793 route/session timing
updates, best 240565ms, with explicit source input boundaries. [Full lap](../full-lap.md#scope).

## C05k/C03: vehicle and detail comparisons

22 matched cases cover ten selectable car families at local HIGH/MEDIUM and all four graphics
presets: camera/root bits agree; all 25/19 normal-car or 7 bonus-car nodes accounted for; all 424 rebuilt node worlds now exact; full raster agreement is reported separately. Shared wheel-pivot preparation includes
rivals, ghosts, fixture; actor 5 gets its second Vectra livery; rival animation follows visible
LOD; detail 0/1 omits static sky and uses source weather/Lavaland background colors; the material
audit adds no unproven fog/window/depth behavior.
[Comparison](../graphics-comparison.md#matched-vehicle-and-detail-comparison),
[model consumers](../scene-models.md#vehicle-families-and-rival-detail).
