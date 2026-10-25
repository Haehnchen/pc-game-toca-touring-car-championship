# Original cockpit rendering

Original profiles render view1's dashboard, gloves, steering wheel and wiper directly from the selected car's DASX archive; Mega overrides below. The body transform is their parent; the separately simulated cockpit camera supplies the view. Wider targets keep the original vertical projection and show more horizontally.

## Asset and driver-side audit

[Cockpit inventory](../tools/inventory-cockpits.py): nine archives/45 model slots, 29 complete payloads, source node matrices, material/UV identities, alpha and geometry groups (compared directly against the unchanged canonical reference; CRC32 IEEE/zlib). Left/right = physical driver position. Source flags come from `0042c0e0`/`004508ce`; physical meaning follows camera right=`−body Z`, forward=`+body X` and the center-lever shift hand. Honda/Audi GPU captures confirm opposite window/A-pillar placement.

| Car ID | Car | Archive under `data/` | CRC32 | Driver side |
| --- | --- | --- | --- | --- |
| 0 | Honda Accord | `honddasx.bfp` | `6c68f4a3` | Right |
| 1 | Audi A4 | `audidasx.bfp` | `f3f7ab71` | Left |
| 2 | Vauxhall Vectra | `vectdasx.bfp` | `2949934a` | Left |
| 3 | Volvo S40 | `volvdasx.bfp` | `30f85a03` | Right |
| 4 | Ford Mondeo | `forddasx.bfp` | `505d1bfb` | Left |
| 5 | Nissan Primera | `nissdasx.bfp` | `08de1330` | Right |
| 6 | Peugeot 406 | `p406dasx.bfp` | `9a0f77d6` | Right |
| 7 | Renault Laguna | `rendasx.bfp` | `fd3542e3` | Left |
| 9 | Flexmobile | `flexdasx.bfp` | `038cfaae` | Right |

- Flag0/negative Z = right-hand drive; flag1/positive Z = left-hand drive. `cockpit_left_hand_drive()` returns true for flag1. Evidence: `src/scene/follow_camera.hpp` initial `head_camera_`, `cockpit_world_to_view()`, shift route `0042de80` → `0042e1d0` → `0042d850`. Captures: `build/test-artifacts/scene-reference-cockpit-render-46d4yy3i/work/cockpit-car{0,1}-640x480.png`.
- Tank8 has no cockpit branch and skips view1. Render ID10 is Warwick's exterior Vectra resource, not a selectable car/cockpit archive; Vectra ID2 owns its interior. No interior production tasks for these two.

| Member | Role | Reuse / differences |
| --- | --- | --- |
| 0 | Left hand + forearm | Six `LEFT_1..6` meshes; geometry matches across eight cars, Audi distinct. Complete payloads differ for all nine. |
| 1 | Right hand + forearm | Six `RIGHT_1..6`; same reuse and Audi exception. |
| 2 | Dashboard | Four `DASH_1..4`, 32 vertices/21 faces. Geometry matches Honda/Volvo/Peugeot/Flex and separately Audi/Vectra/Ford/Renault; Nissan has another UV layout. All nine payloads differ. |
| 3 | Wiper | Byte-identical in all nine: `a958a105`. |
| 4 | Steering wheel | Byte-identical in all nine: `bb153bcc`. |

- 29 distinct complete CM3D payloads. A car's left/right hand texture sections match each other; the nine cars differ. Hand slots: `ARM`, `GLOVE03`, `GLOVE01`, `GLOVE02`. Dashboard slots: `dashc` 32×32, `dash` 128×32, `dasha` 32×32, `dashb` 32×32. Reuse needs section/content checks and preserved bindings, not shared-name matching.
- Hands include forearms; no separate arm, gear-lever or gauge model slot. Left/right also changes grip/shoulder transforms and the shifting hand: do not mirror the complete cockpit image.
- Dashboard is static: `0042de80` loads/places its root, `0042bff0` toggles visibility. Hands/wheel update via `0042c280`, wiper via `0042db50`. Live speed/RPM/gear are the separate [HUD](session-hud.md) (Mega artwork). No live 3D dashboard needle/display consumer exists; adding one is a separate feature decision.

## Recovered runtime behavior

| Source | Consumer |
| --- | --- |
| `0042de80` | Five DASX members: left glove, right glove, dashboard, wiper, wheel; each car selects its own archive. Tank 8 and Vectra2 ID 10 have no branch. |
| `0042c0e0`, `0042e1d0` | Seat/shoulder/grip records. Audi, Vectra, Ford, Renault: opposite driver side. Dashboard rotates a quarter turn about X, translates `(-1999,ccc,0)` Q16; wheel keeps its source pivot. |
| `0042c220`, `0042c280` | Steering ratio `0x4000000 / 398`, signed 16-bit half-error smoothing, forearm axes shoulder→grip, tangent projection/cross product, fixed `18000` glove scale. |
| `00418cd0` → `0042d850` | Local player's pending gear commits handling `+334` → `+330`, clears `+30c`, starts shift hand (also automatic changes and commits outside cockpit view). Input edges and AI forced-gear assignments do not trigger it. |
| `0042d850`, `0042c280` | Hand phases: 0 wheel, 1 reach, 2 return, 3 hold. Reach: cached fixed `1999` step, truncated `distance*10` count, snaps to lever `(6666,-6666,0)`. Hold counts 5→0, then return recomputes its target against current steering and moves by `1999` until within that distance. Reach/hold keep the glove basis; return recomputes forearm/tangent basis. |
| `0042e1d0`, `0042c280`, `0042d850` | Flex 9 parks the left glove at the lever with its original unit basis; only the right hand follows the wheel. Both gesture functions skip Flex/Tank. The parked glove bypasses CMHANDY scaling. |
| `0042bff0` | CMCHUN keeps both gloves and wheel, hides dashboard and wiper in cockpit 1. |
| `0042c280`, `0042db50`, `0042e440` | CMHANDY: fixed `1b333` glove scale, doubles the wiper's animated plane; dashboard/wheel unchanged. |
| `0042db50`, `0042e440` | Dry wiper parks at angle `71`; rain sweeps to `800` and back with acceleration `2d`; snow uses `22` and a 30-tick pause. Updates only on active cockpit ticks. |
| `004271d0` → `00427170` | Cockpit 1 suppresses the player's three body LOD roots. Bonnet 4 keeps hood/body, suppresses front-wheel nodes 1/2 in high/medium models. Rivals/ghosts keep their own visibility. |

- `SceneView::update_visibility` advances cockpit animation only when view 1 is selected; frame generation is read-only, so FPS and pause do not change steering/wiper timing. `SceneView::gear_changed()` forwards the local pending-gear commit; `cockpit_shift_phase()` exposes the phase index. A new commit during a gesture restarts reach/hold.
- Checks: `scene-cockpit`, `scene-reference-cockpit`, `scene-reference-cockpit-render` (Honda/Audi/Flex at 4:3, 16:9, 16:10; CMCHUN/CMHANDY/steering pixels).

### No live rear-view mirror

Bounded to the normal race render route:

- `0041fa40` calls `0041ec20` once per local player (selected camera or authored camera); `00460030` uses the same single-camera wrapper for finish.
- `0041ec20` → `004202b0` → `004871b2` selects the existing viewport, sets the camera, submits once via `00489069`. Trailing `00415350` updates effect visibility bits only.
- `0041faf0` creates one full viewport for single player; `004508ce` creates driving cameras 0–5 and authored cameras, no mirror camera/target.
- `00423198` held rear-camera action saves the view, selects full view 5, restores on release (`004747c1`: “REAR CAMERA”); not an inset.
- `0042de80` loads five static CM3D members with embedded textures; no dynamic scene texture. `MIRROR.WAV` is sound member 46 (damage event 2, `00424860`), unrelated.

The port keeps the full rear-camera view. An inset live mirror would be a new feature.

## Mega coverage

Mega replaces shared wheel/wiper and both vehicle-specific glove/forearm models via `load_cockpit_resources()` for both local players. Honda also loads its four-material dashboard; the other eight dashboards retain their own original models until their individual package is accepted. The generic scenery material pass skips the embedded authored WebPs and does not produce cockpit models.

- `assets/models/mega/cockpits.json` registers nine canonical consumers (each with original side flag and physical driver side), two shared roles, 18 per-car hand models and the Honda dashboard. `cmake/CockpitModels.cmake` and `tools/build-cockpit-models.py` verify canonical archive/member CRC32, prove shared-role identity across all nine archives, build each GLB once.
- Generated shared roles: `build/<config>/generated/mega/cockpit/shared/`; bundled: `assets/models/mega/cockpit/shared/`, hands `cockpit/<key>/{left-hand,right-hand}.glb`. `TOCA_MEGA_MODELS=OFF` removes generated and bundled role files, known dashboard paths when unregistered, and the completion marker; other packages stay.
- Only detail4 passes the Mega directory. Each role validates original nodes, transforms, mesh/material identities and decoded images before replacing the whole original role; missing/invalid models fall back independently.
- Hands: original six mesh slots and four material slots exact; nine texture sets keep distinct colors, sleeve patterns, glove branding (Flex: different square swatches, unbranded burgundy leather). Left/right hands share one car's material set. Audi offset stays separate; right-hand fits cover both shoulder/driver-side groups and Flex's spread; six source-geometry/fit groups cached. Static mesh fits with smooth wrist transition; runtime animation unchanged.
- Each glove/forearm: 7,720 triangles (original 138), curled fingers, padded knuckles, cuff; thumb with gently rising shaft, broad palm transition, continuous taper; distal continuation wraps outside the tube, rim hides its tip from the driver. Audi fit keeps source palm/forearm offset. 18 Meshopt GLBs 3,199,464 bytes; 36 WebP80 textures 649,216 bytes under `assets/models/cockpits/hands/`. [Geometry parameters](../assets/models/cockpits/hands/definition.json), [material import recipe](../assets/models/cockpits/hands/generation.json) (source members, bindings, crops, alpha ops).
- Wheel: rounded leather rim, three drilled metal spokes, hub, six bolts; 2,788 triangles (original two-triangle cutout). Wiper: open frame windows, rails, bridges; 181 triangles (original 26), within the original envelope, planar animation unchanged. UV orientation, grip positions, driver side, weather timing and Extras masks original. Imagegen WebP80: `wheel01.webp` 512×512, `wiper.webp` 512×32. Source identities, crops and import parameters: `assets/models/cockpits/shared/generation.json`; raw originals stay local.
- The [K0/K00–K09 backlog](mega-vehicles-plan.md#k0-and-per-vehicle-interiors) tracks completed shared parts, hand package K0-H and remaining dashboard work.
- `SessionModels` shares each car ID's loaded cockpit between local players; its role cache reuses wheel/wiper parsing across car IDs when complete source member bytes and replacement path match. The selected original/Mega role is a session snapshot; a new cache observes changed files. Hands and dashboards are vehicle-specific; each cockpit keeps its own role model values. `ModelTextures` shares identical GPU images across roles, cars, split views (palette, encoding, upload policy are part of image identity). Each view keeps independent model preparation, animation, visibility.

### Honda dashboard

K00: four WebP70/method6 source-specific images; original four-material skin closed with 8 mm hidden backing surfaces. All 21 front triangles, UVs, normals, five static nodes, material slots exact. Back points shared across real edge joins; the two point-touching source fans stay separate. Wheel/wiper/hands reused. [Definition](../assets/models/cockpits/honda/definition.json), [bindings](../assets/models/mega/cockpits.json).

The original dashboard U direction mirrors its instrument atlas; only the reconstructed RPM glyphs are pre-oriented in their field; atlas, UVs, motif positions intact. Fixed 0/0 instrument art stays static; live speed/RPM/gear use the Mega HUD. GPU fixture `--dashboard-review` compares original/prior/new Honda at 4:3, 16:9, 16:10 and 1920×1080 detail, plus Audi/Flex references.

### Hand geometry inspection

The native render fixture exports `Cockpit::frame`/node transforms, vertices, normals and original/Mega materials for Honda, Audi, Flex. Optional Blender tool: close front/side/back views, fingertip occlusion against real rim triangles, same framing for before/after. Exports stay under ignored `build/`; Blender is not needed by normal builds.

```sh
make check BUILD_DIR=build/cockpit-dev CMAKE_ARGS='-DTOCA_MEGA_MODELS=ON' TESTS='^scene-reference-mega-cockpit-render$'
build/cockpit-dev/toca-mega-cockpit-render-tests references/toca-1.1a build/cockpit-dev/assets/models/mega --export-grip build/cockpit-hands/runtime
python3 tools/render-cockpit-hands.py build/cockpit-hands/runtime --output build/cockpit-hands/inspection --profiles mega --material clay
python3 tools/render-cockpit-hands.py build/cockpit-hands/runtime --output build/cockpit-hands/inspection --profiles mega --material textured
```

`--poses left,right,shift`, `--cars`, `--hand` select views; `--definition` selects a geometry definition; `--framing-frames` reuses one export's cameras; `--report-only` needs only Python. Images WebP70/method6. Proportion reference: Blender Studio CC0 [Human Base Meshes](https://www.blender.org/download/demo-files/#assets), archive CRC32 `519a950c`, object `Hand  - Realistic`; no external mesh is copied.
