# Display, widescreen and HUD

W01 ships with F02 (resolution, FPS limits, VSync via config/CLI; C01 supplies
the menu UI). W02/W03 join the relevant content and camera packages.
[Shared rules](../porting-plan.md#delegation-and-acceptance-rules).

Scope: 4:3, 16:9, 16:10. Out of scope: ultrawide, user-adjustable HUD size,
monitor/refresh-rate selection. HiDPI and monitor-move acceptance has low
priority; the existing drawable-pixel handling stays.

## Evidence from the supplied fix

`references/ToCA_WidescreenV3.zip` (CRC32 `a7b54397`, readme credits
AuToNaNiAk005) was read in memory; nothing extracted, run or installed. It is
analysis material, not an authoritative reference; no mod code is copied.

| Finding | Consequence |
| --- | --- |
| Basic changes field of view; Advanced also adjusts HUD and resolution constants. | Projection, output dimensions and 2D layout are separate responsibilities. |
| Advanced modes 768x480, 854x480, 1138x480. | Derive dimensions/aspect from SDL; no hardcoded modes or driver custom resolutions. |
| Readme: stretched post-race camera, left-aligned menus, limited cockpit geometry on ultrawide. | Check every camera, center the frontend, inspect cockpit asset coverage; do not reproduce these defects. |
| EN/DE/FR EXEs are 788480 bytes, entry `0x004a0520`; canonical 1.1a is 790016 bytes, entry `0x004a0b70`. | The mod's English assembly addresses do not map to our executable. |
| PL Basic has matching size/entry but differs from canonical in 4440 bytes. | Matching entry does not prove an identical base version. |
| PL Basic redirects writes at `0x00460b87` and `0x004805f9` to code near `0x004ace1c`; canonical code writes object offset `0x64` there. | Recovery leads in `0x004608b0` / `0x0047fe20` (confirmed by short disassembly); camera semantics need caller/data tracing. Not a binary patch to transplant. |
| Basic constants ≈1.2 (16:10) and 1.333333 (16:9). | Equal target aspect ÷ 4:3. Derive native projection from the recovered camera, not by float-bit edits. |

Sources: ZIP readmes and `EN, DE, FR/*/Assembly.txt`. The assembly notes'
manual HUD offsets and inconsistent cave labels are clues only.

## Display contract

- Output size comes from SDL drawable pixels, separate from window/input units
  ([SDL_GetWindowSizeInPixels](https://wiki.libsdl.org/SDL3/SDL_GetWindowSizeInPixels)
  and pixel-size changes); the acquired swapchain extent is authoritative.
- 3D viewport fills the chosen aspect without stretching a 640x480 image.
  Vertical FOV preserved; horizontal projection coefficient = original 4:3
  coefficient × `(4/3) / (viewport_width / viewport_height)`, keeping the
  original fixed-point rounding. Each camera's parameters are recovered; the
  viewer's 40° is not original evidence.
- Original menus/art keep authored logical dimensions (baseline 640x480), scale
  uniformly and center. Interactive menus extend the original background into
  left/right margins; frame, text, previews keep original proportions. The title
  uses a complete generated widescreen image at non-4:3 and the original at 4:3;
  copyright stays centered. Race HUD items use explicit edge/center anchors and
  uniform asset scaling.
- Input mapping is the inverse of the displayed UI transform. Gameplay
  coordinates, collision, timing are independent of output resolution.
- No per-aspect patch tables, custom monitor modes or second renderer; SDL GPU
  coordinate convention and tested TL/pixel-center mapping retained.

## Frame pacing contract

- Resolution/window mode, FPS limit and VSync are native video settings: VSync
  on and cap off by default; off/30/60/120 and valid custom cap. Config/CLI
  precedence from F02, G07 storage; C01 adds menu controls over the same service.
- [SDL_SetGPUSwapchainParameters](https://wiki.libsdl.org/SDL3/SDL_SetGPUSwapchainParameters)
  with [supported present modes](https://wiki.libsdl.org/SDL3/SDL_WindowSupportsGPUPresentMode);
  VSync/SDR supported; immediate/VSync-off only when supported. Effective mode
  and failed changes reported at startup, then in C01's UI.
- A cap is a presentation ceiling, not a simulation timestep. G01's recovered
  update/delta behavior is preserved; repeated presentation never runs another
  physics, input-edge, RNG or audio-event update; no 60 Hz display assumption.
  G01 defines live update scheduling and checks it under cap/VSync changes.
- Account for swapchain blocking (no full cap delay after VSync waited); limit
  background/minimized work while keeping recovered pause and audio behavior.
  SDL timing, no scheduler framework.
- Native interval tolerance: larger of 2 ms or 20% of the requested period
  (host scheduling under Xvfb). Physical high-DPI displays and monitor cadence
  are outside this fixture's coverage.

## W04 — Presentation timing audit

Applies to every graphics profile. A 25 Hz world pose repeats across several
60/120/144 Hz images unless continuous fields have presentation samples. The
native 40 ms gameplay/audio deadlines and source event ordering are retained;
simulation frequency is not raised to repair rendering. The fixed schedule is an
adaptation, not a claim that the original always ran at 25 Hz.

| Consumer | Presentation behavior |
| --- | --- |
| Player, rivals, network actors, ghost, race cameras | Shared body, wheel, steering, shadow and camera interpolation; camera/lap/reset cuts remain explicit. |
| HOR planes, helicopters, balloons and attached camera | Interpolated in every profile; route-repeat cuts and discrete rotor/propeller siblings retained. |
| Rain, snow and cats/dogs | Surviving particles and rain endpoints interpolate; wrapping, birth and camera jumps cut history. Lightning/thunder/RNG remain on source ticks. |
| Smoke, dust, spray and exhaust flames | Persistent particles interpolate position, radius, opacity. New particles and attached emitters follow the displayed car pose. Burst phases, visibility, sounds discrete. |
| Cones, posts and signs | Transforms interpolate after movement/contact correction; scene/grid/import boundaries reset history. Breaks/contact responses discrete. |
| Detached car parts, sign/impact fragments, sparks | Previous geometry belongs to each live record; birth, retirement, slot reuse never blend different objects; pivots and lifetimes intact. |
| Skid marks | Only the active endpoint follows the displayed wheel on its existing support plane; saved world quads, ring order, fade unchanged. |
| Cockpit hands, steering wheel, wipers | Local poses interpolate before attachment to the displayed car; wipers rebuild their original singular matrix from the interpolated angle; hand scale and shift phases intact. |
| Championship finale | Cars, wheels, cameras, HOR use shared render history/alpha; camera phase and authored camera selection cut history; credit pages and exit gates discrete. |
| Track-selection miniature | Existing fractional remainder supplies the render angle (speed, phase, wrap, focus pause preserved). |
| HUD, flags, damage variants, menu blinking | Digits/RPM segments/start lamps, mesh variants, blink phases discrete. Rendering never advances gameplay state. |

No frame-count-driven gameplay speed change was found in the host schedule,
audio timeline or frontend clocks. `LiveSchedule` preserves overdue deadlines;
the host wakes for simulation even below 25 FPS. The finale keeps its
elapsed-tick catch-up loop: bounded batches would also require coordinating the
host audio deadline, otherwise sound could advance ahead of deferred events
(a scheduler change separate from interpolation). The pacing fixture
selects 640×480 explicitly (a 1280×720 title run was GPU-submit-bound at about
20 ms); larger sizes are covered by the aspect checks; physical-monitor cadence
and full-size GPU throughput are not claimed.

## W01 — Drawable sizes and pacing in the first native application

Targets: `src/platform/`, `src/render/`, SDL GPU shaders; lifecycle/entry point
belong to the application (F02).

- Drawable extent, viewport and UI transform values replace global renderer
  width/height (projection, uniforms, depth/color targets, readback).
- Common application starts at 640x480, 1280x720, 1280x800; arbitrary valid
  drawable sizes use the same math.
- Video settings: supported VSync modes, frame caps, preference persistence,
  without changing game/audio clocks.
- Resize, high DPI, minimize/restore through SDL; only size-dependent resources
  are rebuilt; unavailable swapchain frames are skipped.
- Input maps into the uniform UI canvas and resizes with the viewport.
- Verified: three aspects, arbitrary live resize, minimize/restore, inverse
  pointer mapping, VSync/immediate switches, 30/60/120 caps.

## W02 — Recovered camera projection and native HUD layout

Camera setup consumer: `004508ce`. Normal single-player view 2: local
fixed-point position `(-0x5668e, 0x1800f, 0)`, Euler angles `(0x6e, 0x400, 0)`,
attached to the follow node; `+48==0` selects view 2, otherwise view 3 (view 3:
`(-7,2,0)`, pitch 56; table indexed from `004c3478`). Both pass projection
factor `0xa3d7` and near plane `0x4000` to `0048f5f9`. These are local inputs,
combined with the initial body position and independent anchor heading
([body/camera](../scene-rendering.md#initial-body-and-follow-camera), [FollowCamera](../driving-audio.md#moving-follow-camera-and-local-driving-audio)).
`0041ec20` → `004202b0` → `004871b2` supplies the per-player viewport and camera.

`src/scene/camera.cpp`: `0048f5f9` multiplies the factor by fixed `0x20000` /
`0x28000` for X/Y and computes fixed depth scale/offset before converting to
floats. `00452376` far limits: 150 (detail 0/1), 200 (2), 250 (3). The original
X/Y ratio is not an idealized 4:3 perspective: preserved at 640×480, then
horizontal coverage widens by the drawable aspect. Near/far depth keeps the
original fixed rounding, so endpoints need not be exactly 0/1. Weather's third
argument to `0048f5f9` is unused; multiplayer and the special fog far-limit
override are outside this API. These coefficients do not establish a complete
player-camera state.

HUD/menu layout (all uniform scaling, original 640×480 placement intact):
frontend artwork centered with background extended into margins
([menu background](../frontend-startup.md#widescreen-menu-background));
track loading screen background extended, centered image and progress bar kept
([loading](../scene-lifecycle.md#widescreen-loading-presentation)); two-car
title with generated side artwork ([title](../frontend-startup.md#widescreen-title));
lap panel, timing, speed/gear/RPM, start lamps, driving/pause notice anchored to
drawable edges or center ([HUD layout](../session-hud.md#widescreen-layout));
IMAGE640 speed/gear/RPM sprites and compiled layout, gauge cluster follows the
bottom-right edge ([HUD evidence](../driving-audio.md#barrier-audio-and-local-driving-hud)); settings and Extras
(four effect categories) follow the centered menu policy.

- [ ] Trace the two patch leads through the canonical export; identify their
  object/camera roles and original projection setup before replacing inspection math.
- [ ] Build one aspect-aware projection from recovered camera parameters. Apply it
  to the original transform/clip path without changing simulation-space values.
- [ ] Apply the same policy to future rival-position panels and
  complete race messages as their consumers land.
- [ ] Compare a matched 4:3 reference view and geometric proportions at wider
  aspects; inspect culling near the newly visible left/right edges.

Accept: menu → race has correct proportions, readable HUD and aligned controls
at all three aspects; wider views reveal scenery horizontally.

## W03 — Camera and presentation coverage

Inventory of normal race/cockpit, alternate/rear and authored trackside modes:
[cameras](../race-cameras.md). Ordinary live views, rear and helicopter checked at
4:3/16:9/16:10 (HUD, focus/pause, cockpit/hand visibility); the recovered normal
race path uses full rear view without a separate live mirror pass
([cockpit](../cockpit.md)). Cockpit coverage from each original DASX archive and
camera setup, source geometry at three aspects, no substituted tank interior
([cockpit checks](../cockpit.md)).

- [ ] Complete the separate split-screen camera inventory.
- [ ] Extend the aspect matrix to authored trackside, overhead and split
  views, including scene transitions.
- [ ] Check 640x480 and 1024x768, 1280x720 and 1920x1080, 1280x800 and 1920x1200
  at milestone acceptance; daily work uses one representative size per aspect.
  Pair representative scenes with 30/60/120/uncapped and supported VSync modes;
  compare game-time/input/audio traces without multiplying every matrix dimension.
- [ ] Retain window-only captures and short measurements locally; update the
  shared acceptance record and commit the completed camera/layout work.

Accept: all implemented modes preserve aspect and UI layout; the mod's reported
post-race stretch and menu alignment problems are explicitly checked. Xvfb
verifies mode selection and cap logic; physical tearing and monitor cadence are
outside its coverage.
