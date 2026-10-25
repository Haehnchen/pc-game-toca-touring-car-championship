# TOCA 1.1a implementation plan

[Work rules](../plan.md), [setup/checks](porting.md). This plan tracks open work and
integration order. Detailed package checklists remain authoritative; completion records
retain technical results and evidence.

## Decisions and scope

- C++23, SDL3 GPU, SDL_shadercross and GLM for presentation math. Preserve
  recovered 1.1a numeric/game behavior. Reconsider backends only for a proven blocker.
- Linux and Windows share code/dependencies; macOS uses the same backends.
- Optimize measured resource reuse and allocations while preserving numeric,
  update and draw order.
- Every usable feature joins `make run`; settings UI and config/CLI share
  preferences and application services. Presentation supports 4:3/16:9/16:10,
  FPS limits and VSync without changing simulation, input or audio timing.
- Canonical source/media, CRC32, asset quality, audio integration and test
  isolation follow [plan.md](../plan.md) and [AGENTS.md](../AGENTS.md).
  Original codes/unlocks belong in Extras; keep original effects, convenience
  controls and earned progression explicit. Intro movies and a separate
  user-music player remain outside the soundtrack package; see [music](music.md).

## Established baseline

The native application integrates all ten tracks, driving, qualifying, Single Race, Time
Trial, Championship, two-player split screen and Linkup with audio. This baseline does not
complete G04/G05/G06 source parity.

Technical records: [foundation](porting-completed/foundation.md),
[tooling](porting-completed/foundation.md#completed-iteration-tooling-and-settings-integration), [frontend](porting-completed/frontend.md),
[gameplay](porting-completed/gameplay.md), [tracks/rendering](porting-completed/tracks.md),
[vehicles](porting-completed/vehicles.md), [Mega](porting-completed/mega.md),
[ground](porting-completed/ground-materials.md) (S00–S16 on all ten tracks).

## Delivery priorities and remaining scope

### Accepted functional completion

The functional package is accepted: archive verification and the extracted-package
startup/menu-to-race check passed ([release evidence](porting-completed/foundation.md#functional-release)).
Further work addresses concrete defects or explicit requests; no automatic
original/replay proof or acceptance matrix. Artwork and fidelity retain their
separate scope below.

### Separately owned further work

Remaining work grouped by outcome; detailed unchecked steps can include already integrated
subsets and must not be counted as wholly missing features. No completion percentage follows
from checkbox or comparison-value counts.

| Remaining outcome | Current boundary / owning work |
| --- | --- |
| Original-faithful autonomous race | Playable fleet and a complete retained original cold→lap→finish→warm-restart comparison exist. Finish authored world/resource initialization and input/callback ownership below. |
| Mega artwork rollout | All track scenery, ground/sky packages and eleven racing exteriors are accepted. Finish the separately owned T17 transporter, finale drivers and remaining cockpit interiors. |
| Complete visual/audio fidelity | Extend matched scenes/full-lap paths and driving PCM where coverage is missing; batch fixes by consumer and visible/audible defect. |
| Remaining runtime/input/save contracts | Close source timing/recovery and outstanding numeric/save-field behavior. Raw wheel input and original constant/sine FFB are integrated; physical wheel/driver acceptance remains open. |

Asset work keeps its existing ownership/order. Defer unused storage,
speculative resets, extra reports, pixel polishing and additional recording
matrices unless a concrete defect makes them necessary for the functional version.

## Frontend state

<a id="original-random-menu-backgrounds--2026-10-18"></a>

MMB01b menu rolls and image 3 are integrated; image 4 is unreachable in the original phase
cycle ([transition contract](frontend-startup.md#menu-page-turns)). All ten Mega loading images
and original localized statistics are integrated with precomposed bitmaps and a separate
bitmapfont ([loading contract](scene-lifecycle.md#mega-loading-bitmaps-and-original-statistics)).

## Open Mega asset rollout

Ground S00–S16 and track scenery M00–M08/R00–R10 are complete on all ten
tracks. Asset ownership and per-type acceptance remain in the linked plans;
vehicle/cockpit work below is separate.

### Vehicles and cockpits

The [vehicle plan](mega-vehicles-plan.md) owns the roster. Complete and commit one vehicle
before the next. All eleven ALLCARS exteriors V00–V10 are accepted
([models and records](porting-completed/vehicles.md#exteriors)); S01/S02 finale vehicles
([package](../assets/models/mega/finale-generation.json)) and T01–T16 static vehicles (30 HIGH
mesh rows, 27 source types, 15 model names; 117/118 source images replaced, one native constant
retained) are accepted.

- [ ] Complete F0 driver models through the existing finale consumer.
- [ ] Complete [K0 and nine cockpit packages](mega-vehicles-plan.md#k0-and-per-vehicle-interiors):
  dashboards, original driver sides, GPU resource sharing and complete
  animation/fallback/QA for both local players. Shared wheel/wiper/hands are
  integrated; the shared dashboard path and complete Honda K00 are accepted.
  Eight vehicle dashboards remain; exteriors do not accept interiors.
  [Asset/driver-side audit](cockpit.md#asset-and-driver-side-audit).
- [ ] T17 Croft `BH06WT2_1`: newly confirmed transporter with four bindings and
  six source slots; separate vehicle ownership, outside track-prop work.
  [Package scope](mega-vehicles-plan.md#t0-and-static-track-vehicles).

### Roadside objects and textures

The [scenery plan](mega-scenery-plan.md) owns M00–M08; T/F vehicles and finale figures remain
with the vehicle plan.

- [x] M01/M02: all confirmed near-tree/woodland types and local materials accepted;
  simple bodies, source silhouettes and placement retained.
- [x] M03–M06: barriers, people/crew, small objects/animals and flags accepted.
- [x] M07: buildings, stands, towers and bridges accepted per track.
- [x] [MT00–MT05 texture review](mega-scenery-texture-review.md): all track props,
  including existing/shared artwork, checked against source motifs, UVs, masks
  and repeat phase; vehicles/cockpits excluded.
- [x] M08: exact inventory/acceptance reconciled with existing native driving,
  audio and split-screen evidence; concrete remaining defects closed.

### Track integration and final artwork review

The [track plan](mega-tracks-plan.md) owns R00–R10 and consumes S/M/B assets. The
[ground plan](mega-track-materials-plan.md) owns ground materials and optional future ground
geometry. T0 vehicles remain exclusively in the vehicle plan. All R00–R10 packages are
accepted (per-track counts and evidence: [track plan](mega-tracks-plan.md)).

- [x] R06 Croft: all159 scenery rows/155 types/603 raw bindings and83 source
  images accepted;82 mappings plus one native constant. Source-bound local
  artwork, shared RGB variants and original prop bodies/UVs verified.
  [R06 acceptance](mega-tracks-plan.md#r06--croft).
- [x] R10: all1983 pure scenery rows across all ten tracks accepted; Donington's
  existing acceptance added to the registry, local/shared variants and existing
  driving/audio/split evidence reconciled. T17 remains separate.
- [x] Final generated-texture inventory and source-aspect/motif/UV review closed;
  [current format contract](assets.md#image-formats-and-scaling). No remaining track asset task.

## Further source-parity work

### C05j / C02 — Matched original rendering

Completed and accepted. [Shader/material and presentation contracts](graphics-comparison.md),
[backend](graphics-prototype.md), [original-lap validation](full-lap.md#scope).

### G04/G05/G06 — Autonomous Single Race and detached parts

The native game already runs complete 16-car races. The open milestone is exact original
behavior from authored setup, human input and elapsed time, without recorded internal state or
callback scheduling as inputs. Accepted components: retained AI/RNG, controls/drivetrain,
continuous physical feedback, body dispatch, the bounded two-call cold-start handoff (body
fields, calibration, support caches, initial pair touching)
([scope](porting-completed/gameplay.md#step-17-single-race-integration),
[evidence and dependencies](single-race-fleet-reference.md#remaining-work)).

| Accepted package | Contract |
| --- | --- |
| Cold NoQual startup | Shared two-call initializer, first physical handoff, control/support caches, pair touching. [Startup](single-race.md#generated-noqual-startup). |
| Cold qualifying startup and native fleet | Both443f60 calls generate canonical bodies, AI controls, the four-actor ring and initial clock handoff without classification draws. Native local+3 rivals share physical/scene/audio consumers; twelve records stay removed. Earned grid restores sixteen actors; warm restart returns to four. [QualOn startup](single-race.md#generated-qualifying-startup). |
| Cold actors and mutable race world | Production constructors own pristine Motion/Controls; grid supplies actor/car/controller identity; contacts, damage/active-free parts, roughness, canonical props generated; warm reset keeps process scratch and cone caches. [Cold contract](single-race-fleet-reference.md#generated-cold-contact-damage-and-prop-world). |
| Native scene/frame consumers | One owner publishes physical registration, retained LOD and separate geometry/contact/wheel/exhaust gates. Motion, Rivals and Linkup consume one snapshot; Full rivals keep single-player manual effects independently of geometry PVS. Presentation cannot advance this state. [Scene contract](scene-rendering.md#draw-and-effect-consumers). |
| Recorded scene inputs | A separate actual-call journal drives the production CPU owners from camera/frustum/root/body inputs. Bounded startup: 98 operations, 3 publications, 3,411 exact values; normal View2 shares body-observer/forced model dispatch with live play. Camera generation and moving PVS/LOD transitions remain outside that proof. [Journal contract](scene-rendering.md#recorded-scene-inputs). |
| Generated normal scene frames | Authored grid heading, physical pre/post bodies and external QPC samples generate normal View2 camera/projection and primary/extra refresh cadence. Three complete primary frames include one extra physical pass; camera and scene calls stay expectations. Physical states and initial roots remain explicit inputs. [Frame contract](scene-rendering.md#generated-normal-view2-frames). |
| Joined autonomous normal scene | One cold execution generates physical transforms, initial roots, View2 camera/PVS/LOD and primary/extra cadence. A 64-update driving prefix matches all 35,383 scene values, including 23 PVS and 2 LOD changes; source bodies/masks stay expectations. Callback/audio context remains explicit. [Joined contract](scene-rendering.md#joined-physical-and-scene-replay). |
| Ordinary camera controls | Joined views1..5 consume C/Z keys, generate view/projection, head/follow/car ancestry and view-specific model/effect gates. One 96-update prefix matches all 52,756 scene values. [Camera contract](scene-rendering.md#ordinary-camera-controls). |
| Finish/results/warm scene | Generated lifecycle timers, authored camera/PVS/model ownership, menu-only publication and warm grid reset match 1,475,168 consumer values over the existing 3,744-update recording. Matrices/ordered PVS remain unrecorded. [Lifecycle scene contract](scene-rendering.md#finish-results-and-warm-scene-consumers). |
| Qualifying scene consumers | The same owner handles approach, finish/results and the earned race grid. The existing qualifying finish recording checks generated camera selection, roots, PVS and model gates; physical bodies, TimeStep/pass markers and phase timers remain explicit inputs. [Qualifying contract](scene-rendering.md#qualifying-scene-consumers). |
| Source clocks and lifecycle | One creation clock generates five origins; ordered QPC samples generate later TimeSteps; countdown, finish/results, warm restart share one timer bank (incl. raw state10 phase reset). [Clock inputs](simulation-time.md#recorded-startup-clock-transactions). |
| Native complete race and warm restart | Local finish/results, one-/two-player RACE AGAIN, actor repair, retained world/audio ownership; retired AI records stay available to traffic lookups. [World lifetime](single-race.md#warm-world-reset-and-resource-lifetime), [local multiplayer](split-screen.md#finish-and-restart-behavior), [AI lookup](single-race.md#retired-actor-traffic-lookups). |
| Original comparisons | One continuous 3,744-update cold→lap→all finishes→results→RACE AGAIN→warm-green interval is exact in Debug and ASan/UBSan (five initial origins generated, no intermediate driver/physical/timing import). [Continuous proof](single-race-fleet-reference.md#continuous-cold-startup-and-local-race-timing). |

Native fixed 25 Hz scheduling is an explicit adaptation with accepted focus/stall/input
behavior. Accepted component/native checks and unchanged recordings are reused without another
routine track/weather or Debug/Sanitizer matrix.

- [ ] Join qualifying's continuous State4→6 physical/clock producers to the scene
  owner. The four-actor native fleet and cold initializer are integrated;
  existing finish/startup captures lack the full QPC stream. Preserve their
  bounded claims and reuse the native flow/component evidence.
- [ ] Batch manual trackside/actor/cheat camera ownership. Ordinary views1..5 and
  finish/results/warm/qualifying scene consumers are integrated. Numerical authored
  camera matrices and ordered PVS lists remain outside the existing recording;
  broader source input/callback origins and audio listener/mix context remain
  explicit. Reuse accepted full-race lifecycle and component evidence.
- [ ] Complete original damage/effect comparisons: visibility retirement/reuse,
  raw render-owner handles, remaining effect/scrape state and actor-bound type17
  callbacks. Keep recording limits distinct from the implemented runtime
  lifetime/ownership contracts. [Damage](contact-effects.md#original-car-damage-presentation), [exhaust](visual-effects.md#exhaust-flames-and-backfire).

New recordings need a consumed missing input, a named uncovered branch or a concrete defect;
geometric checks run in the background on immutable prefixes. Qualifying, pit/retirement
classification and unsupported callback/device origins remain explicit.

### Remaining foundation, input, saves and audio contracts

- [ ] Complete [F05b startup/state recovery](implementation/01-foundation.md#f05b--startup-and-state-integration),
  including frame/lifecycle contracts with G01/G02.
- [ ] Finish broader original driving-PCM/G08 fidelity comparisons using the
  existing isolated capture path and menu-PCM baseline.
- [ ] Recover remaining numeric/save/race structures in G02/G07;
  [gameplay checklists](implementation/03-gameplay.md) remain authoritative.
- [ ] Accept physical wheel/driver behavior and two-pad feedback feel in
  [G03](implementation/03-gameplay.md#g03--keyboard-controllers-and-local-players).
  Calibrated raw joystick/wheel input and original directional constant/sine
  FFB are integrated alongside gamepad vibration; see the
  [device/output boundary](controller-setup.md#raw-joysticks-and-wheels).

## Network and release

Linux Release ZIP via `make release`: stripped runtime, bundled libraries/shaders/assets and
unpacked versioned 1.1a data. GitHub jobs prepare two x86_64 ZIP artifacts: native Linux and
Windows/MinGW in the same Ubuntu 26.04 Docker image used locally, including full Mega generation,
recursive non-system DLL packaging and ZIP verification; Wine checks run separately on the host.
Constants and
translations are versioned, typed native C++ data; build/runtime need no original EXE.
[Build/package contract](build-and-tests.md#local-release-package).

Established (see [plan.md](../plan.md#assets) and [build contract](build-and-tests.md#remaining-original-data-dependencies)):
clean Git export builds the full Mega Release (isolated/muted frontend and menu-to-scene flow,
extracted ZIP); 48 non-audio BFPs versioned individually as Zstd19 (11.75 MB, unchanged bytes);
no local-reference prerequisites or skip gates in tests (package-relative archive identities;
[isolation](build-and-tests.md#isolated-fixtures)); converted speech/effects as five USTAR+Zstd19
packs (2,045 runtime files, 59,663,767 → 50,739,177 bytes, no re-encoding); ZIP excludes
`THIRD-PARTY-NOTICES.txt`, checksum manifests, replaced WAV/RAW, unused low-memory sound banks,
loose BMP/PAL and unused 320/low/medium frontend archives; release speech Vorbis 32 kbit/s mono,
effects 64 kbit/s, music 96 kbit/s stereo ([audio](music.md#release-speech-and-effects)); startup
screens WebP70/method 6; UI images split Original/Mega with matching filenames and one WebP
loader, 19 base/upgraded garages imported/composed manually ([garage](mega-garage-plan.md)).

ENet LAN/direct IPv4 has an accepted local eight-human baseline, one local driver
per process. Host loss ends the session; unchanged DirectPlay-client interoperation,
automatic migration and internet services remain outside this baseline.

The [local eight-peer capacity](implementation/04-multiplayer.md#n04c--local-eight-peer-capacity)
contract records the existing network checks.

## Detailed work packages and integration order

| Checklist | Packages |
| --- | --- |
| [Foundation/tools](implementation/01-foundation.md) | F01–F06: build/app, evidence, assets, startup and original traces. |
| [Content/rendering](implementation/02-content.md) | C01–C06: frontend, original drawing, cars, tracks and Extras. |
| [Gameplay/audio](implementation/03-gameplay.md) | G01–G09: flow/numerics, input, handling, AI, race/damage, saves, audio and codes. |
| [Multiplayer](implementation/04-multiplayer.md) | N01–N04: protocol, transport, sessions/lobby and replicated race. |
| [Display](implementation/05-display.md) | W01–W03: drawable sizes, projection/HUD and camera coverage. |

Further rendering/gameplay fidelity and Mega work retain their existing ownership.
Audio accompanies each originating feature.

## Delegation and acceptance rules

[plan.md](../plan.md#zusammenarbeit-und-prüfungen) owns collaboration and checks:
two disjoint agents, main integration, isolated/muted runs, affected tests and
regular scoped commits. Record original evidence and the user-visible result;
agree state/ownership contracts before implementation. Review fidelity,
portability, removed code and `git diff --check`; update the owning checklist.
Tests must validate real behavior, not trivial implementation details. Run no
game tests for documentation-only changes; broaden passed checks only for
relevant changes, failures or unresolved concerns.
