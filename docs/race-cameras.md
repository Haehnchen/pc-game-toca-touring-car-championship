# Race cameras

`C` cycles the original single-player views, `Z` holds rear view and release restores the prior selection. With CMFOLLOW enabled in Extras, `F5` toggles an authored trackside camera; cycling past the bonnet also enters that mode. CMMICRO replaces the far chase view with its original overhead camera. CMCOPTER adds the camera attached to the automatically flying horizon helicopter. Controls run in the normal scene on independent simulation ticks.

## Selection and transforms

Evidence: canonical 1.1a `references/decompiled/tourcars.c`. `0046ae6b` binds action7 to VK`43` (`C`), held action6 to VK`5a` (`Z`); VK`74` = F5 (needs CMFOLLOW, config `+7c`). `00422c90` press edges, `00422c40` held. `00422f60` accepts camera changes in states4/6/9, blocks cycling while rear is held, saves/restores the rear selection.

| Original view | Transform from `004508ce` | Projection factor |
| --- | --- | --- |
| 1, cockpit | Independent head anchor; offset `(0,6666,±6666)`, yaw `0400` | `b333` |
| 2, near chase | Independent follow yaw; offset `(-5668e,1800f,0)`, pitch `006e`, yaw `0400` | `a3d7` |
| 3, far chase | Independent follow yaw; offset `(-70000,20000,0)`, pitch `0038`, yaw `0400` | `a3d7` |
| 4, bonnet | Body anchor; per-car X table `004af628`, Y `b333`, yaw `0400` | `b333` |
| 5, held rear | Body anchor; per-car X table `004af650`, Y `c000`, yaw `fc00` | keeps prior view |
| 0, actor | Child of first moving HOR actor / helicopter root; offset `(10,1,-4)`, angles `(0071,fce4,0400)` | `b333` after cycling |
| CMFOLLOW override | Nearest authored camera pointed at local car (`0041f520` → `00426b10` / `00426830`) | keeps stored view |

- Offsets are signed Q16 hex. Cycle 1→2→3→4→1, starting at 2; tank8 skips absent cockpit1. CMFOLLOW changes the wrap from 4 to 0 with the authored-camera override, then 1 on the next press. CMCOPTER selects actual actor-camera0 and wins when both flags are set. F5 changes the authored override without changing the stored view/projection; rear hold preserves it too.
- `0042c0e0` cockpit side flags `{0,1,1,0,1,0,0,1,0,0}` (0 = negative Z, 1 = positive Z). Bonnet X `{19660,20971,21626,19660,22937,19660,26214,20971,19660,62259}` (decimal Q16); tank8 adds `3333` only at Low/Medium detail (config `+328=0/1`). Rear X `{-78643,-72089,-65536,-65536,-85196,-65536,-65536,-78643,-52428,-45875}`. Tables read from the canonical PE sections.
- `0048f5f9` multiplies the selected factor by 2 and 2.5 with fixed truncation. Native projection keeps vertical coverage, extends horizontal at 16:9/16:10. Near 0.25; detail-specific far/depth coefficients unchanged. Rear press/release never calls this projection setter, so its saved projection differs from its selected view5.

## Head movement and overhead view

- `00421980` updates the cockpit after body integration via `0042f950` / `0042fac0`. Anchor node: body-up smoothed with `1999` base blend and `428f` threshold, then body-side axis orthogonalized. Local camera responds to old minus new velocity projected along body-forward (`191eb`) and body-side (`9999`); `0290` force gate, `01ca` damping, `1999` velocity cap. `0042f4c0` rebuilds local axes against the fixed half-unit reference. Yaw angular velocity drives extra roll with a three-angle-unit slew.
- Driver offset at source state `+b4/b8/bc` is immutable; each translated frame publishes that base plus the current spring velocity (no accumulation). Fixed square-root deadband can leave a tiny residual. Leaving cockpit resets orientation/velocity via `0042f950`, keeping the base offset. Presentation and audio invert the same final affine matrix (original non-unit lookup bases).
- CMMICRO starts in view3: height 40, pitch `03dd`, yaw `fc23`. `004608b0` keeps its anchor yaw only while view3 is selected. State4/normal follow slews: [camera and driving audio](driving-audio.md#moving-follow-camera-and-local-driving-audio).

## Audio mapping

- `004145c0`: inside engine samples for views0/1/4/5; view2 outside mix1, view3 outside mix2; authored override outside mix2. `00424070` spatial effects: views0/3 outside mix2, view2 outside mix1, 1/4/5 inside.
- Assembly `00424410..0042444e`: mix1 → `004d3be0` (engine gains `5999/4000`), mix2 → `004d3ba8` (`4ccc/3333`), inside → `004d3c18` (`5999/5999`) (corrected a former native near/far gain reversal). Camera changes reselect the engine sample family and keep the actual listener for spatial effects; music keeps its scene selection. `0043cf60`, `0043e2a0`, `0043f0c0` issue no audio voice calls.
- Rival motors use source category2 gains: near2 `6666`, far3/trackside0 `5999`, inside1/4/5 `4ccc`; inside/outside bank changes keep source `426360`'s rival-voice restart before the next spatial update.

## Actual horizon actor camera0

- `0043e2a0` adds the camera to the extra identity parent of the helicopter, not its banked/pitched model child. `0043cf60` also creates camera0 on the first moving HOR root on tracks0/2/5/6 when loaded. Shared local offset `(10,1,-4)`, pitch `0071`, yaw `fce4`, roll `0400`. `0047fe20` applies local Y, Z, then X using float Rodrigues rotations and the fixed sine lookup; matrices not renormalized. Native composes the local node with the actor root every horizon update and inverts the full affine basis; translation feeds the audio/weather listener; visibility inherits the parent actor's registered sector (`0047da47` / `0047dd90`), not the camera offset or player sector.
- `00446a80` calls the automatic route consumer `0043f0c0` (takeoff, climb, corners, approach, landing, rotor frames, waits). Construction/updates/camera reads consume no RNG. The camera follows the actor regardless of the shown car view. No flight keys (`00422f60` still consumes C/Z and car controls; the helicopter update has no player-input branch). Pause freezes the shared horizon update; scene loading resets it; qualifying-to-grid handoff retains it.
- `0046caae` restricts CMCOPTER track selection to 1/3/4/7/8. Their helicopter loads only in sunny/cloudy weather (`0043e2a0`); rain/storm/snow omit it. Tracks0/5 keep their moving-actor camera in every weather; 2/6 need dry weather; track9 has no actor-camera0. CMFOLLOW can expose an existing actor-camera0 without CMCOPTER: cycle 4→0 authored, F5 releases the authored override, next C selects cockpit1.
- Native availability is explicit (`HorizonFrame.camera_zero`). Where source weather/track loading omitted the node, CMCOPTER skips it and keeps the ordinary cycle; with CMFOLLOW the authored override stays available. F5 cannot disable authored mode while the saved view0 has no actor. Rear hold uses the same availability rule and restores selection/projection on release. No forced dry weather or substituted camera.

## Split-screen boundary

Local two-player viewport composition: [split screen](split-screen.md). `004508ce` distinguishes one/two players and horizontal/vertical split (`configuration+d8`), creating separate per-player camera nodes with different offsets/pitches. Split projection factors `10000` (horizontal), `1383a` (vertical); `0048f2ea` applies viewport scales `(1,.5)` or `(.5,1)`; `0041ec20` selects the per-player viewport/camera. W03's broader
camera inventory remains a separate acceptance item. Split-screen acceptance
remains separate work. Mirror investigation: [cockpit](cockpit.md).

Input: C/Z remain reserved during drive-key rebinding; weather, culling and spatial sound use the same selected view, preserving the race-finish camera.
