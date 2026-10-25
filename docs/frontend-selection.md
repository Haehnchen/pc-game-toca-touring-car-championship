# Single-race selection subset

Route: Main (state `2`) → Race Menu (`0b`) → Single initialization (`0c`) →
Single Options (`0d`) → Difficulty (`1b`) → Name (`5`) → original 2D garage
(`4`) → Track (`6`) → loading (`7`) → [initial scene](scene-lifecycle.md). Escape
follows these screens back to Main; initial Main is its own back-state. Other
race modes and the Players option remain visible but unavailable.

- `FUN_004691a9`: Race Menu's four visible rows Single, Championship, Time
  Trial, Linkup (compiled Showdown slot hidden). Icon tables `004b6d40`,
  `004b6ec0`, `004b7040`; labels/help/option formats read from the canonical
  executable by the bounded PE reader (no original text bytes in source).
  `FUN_004694f0`, `004695f8`, `00469713`, `004697a1`, `00469825`: Single/
  Difficulty route, defaults/selections. `FUN_0046a8bb` initializes one player,
  lap choice 2 (three laps), qualifying enabled, default weather, Medium
  difficulty, Honda, automatic transmission.
- Setup fields mirror original offsets: lap choice `+14`, derived laps `+18`,
  inverted qualifying flag `+20` (native: explicit boolean), difficulty `+34`,
  weather `+a0`, player count `+ac`, name `+b0`, car `+dc`, transmission `+e0`.
  Lap choices wrap 1, 2, 3, 5, 10, 15, 20 (`004728b0`, `004729f2`, `00472ad6`);
  six weather choices wrap (`004727ed`, `0047284c`); qualifying changes
  `0042be90`. Enter on an option invokes its right callback; Left/Right changes
  emit sample 1. Continue defaults to row 4. Difficulty is committed on entering
  Car (`00477586` → `004696c5`).
- Name editor (`004780ab`, `00478354`, `004783c1`, `00478449`): uppercase ASCII
  A–Z, digits, spaces, max nine characters; native text input uppercases
  lowercase ASCII. First accepted text replaces the prior name; first Backspace
  removes only the final character. Delete is not implemented. Escape precedes
  Enter and discards unconfirmed edits. Empty confirmation: localized PLAYER +
  one-based index (`0047755c`). Append/delete sample 3; confirm/back sample 2.
- CURSOR3 draw is separate from its blink flag (`00477dbd`, `00477f84`,
  `004780ab`, `004782dd`): name entry resets the counter without drawing; append/
  Backspace draws immediately; blink every ten active frontend ticks (exactly
  10/30 s, retained fractional time, independent of presentation); only the
  first accepted replacement character resets the counter; unfocused dispatch
  pauses the clock. Name-triggered code redemption shares the Extras dispatcher
  and localized activation speech ([code scope](cheats.md)). Profanity
  substitution remains pending.
- Garage input (`00477586`, `00476339`, `00476536`): Left/Right IDs 0–9 bounded,
  Up/Down toggles manual/automatic. IDs 8/9 locked under original fresh defaults
  (`004761bc`), unavailable display kept. Navigation/transmission sample 0;
  available Car confirmation commits and enters Track with sample 2; locked cars
  sample 4; Back returns to Name. Unconfirmed browsing is discarded on
  re-entering Car (separate selected/stored car fields). The garage is a 2D
  strip composite, not CM3D (see Single-race selection resources).
  Native changes are immediate: original roll/slide animation and held key repeat remain open.
- CD02 continues throughout this route; its original stop boundary is frontend
  teardown `00468e74`, not Car entry.

## Single-race selection resources

Car selection uses committed 2D garage WebPs, not a live CM3D scene.
`assets/ui/original/garage/` holds 19 base views; `assets/ui/mega/garage/` equal
logical filenames for upgraded views. One loader selects Original, then an
available Mega override at detail 4 (missing Mega keeps Original). The archive
layout is provenance only; no strip loader runs.

| Original site | Native resource/display contract |
| --- | --- |
| `0x00475b3f`, `0x00475bb5` | Original images: nine consecutive 64×360 BMP strips. Native loads a finished 573×357 Original or 1146×714 Mega `garage/garN-carN.webp`. |
| `0x004b9700`, `0x00477586` | IDs 0–6 select variant 1; IDs 7–9 variants 1–4 from the two independent bonus-car flags. Normal availability 0–7; locked 8/9 visible with unknown labels and blocked confirmation. |
| `0x00476363` | Finished image already contains the original crop; drawn at logical (34,63,573,357); centered uniform 640×480 transform handles aspects. |
| `0x00472540`, `0x00472568`..`0x0047272e` | Car names/technical info: 36-byte records at `0x004b9078` plus compiled localized strings; drive kind, valve byte, power-text pointer, RPM read without inventing missing Honda figures. |
| `0x00476547`, `0x00477b6e`, `0x00476280` | Selection heading, technical rows, transmission, help; locked cars use compiled unknown/blocked labels. |
| `0x00476404`, `0x00470714` | Selection icon 23 and arrow sprites; car changes bounded 0..9; transmission upward arrow. |
| `0x0046e1b0`, `0x0046dee5` | Adjustable rows: unselected arrows at atlas offset 90, selected at 0. Unsupported Players changes stay disabled. |
| `0x00476a7a`, `0x00476b9c`, `0x00477c60` | Name heading y24; prompt centered on 320 at y161; OPTION3 name centered within 0..600 (x300) at y219 with shadow (+2,+2); help y440. |
| `0x00477d0a`, `0x00477d73`, `0x004782dd`, `0x004780ab` | CURSOR3 = frontend member 28, 11×25 indexed sprite, transparent at centered text end +1, y215; visibility from the ten-step 30 Hz blink counter and immediate edit redraws. |

- The original garage used the active Audi frontend palette. GAR1's first 156
  embedded colors differ only at unused index 0; two used index-255 pixels also
  differ from Audi; GAR2–4 match indices 1–155. Reference exports apply the
  active frontend palette instead of each BMP's.
- [Mega garage plan](mega-garage-plan.md): all 19 views, background/vehicle
  artwork workflow. Cars and shadows were baked into the strips (no independent
  car sprite). New source layers composed manually; 19 finished WebP70/method 6
  views committed. Cars rendered locally from Mega/original HIGH models by a
  manually invoked script (new exports WebP70/method 6; existing WebP80 layers
  unchanged; only empty backgrounds use AI generation). The build copies
  finished images and never renders/composes. Compact recipes keep layers, crop,
  placements.
- Labels/vehicle info use shared versioned native tables. The view caches WebP
  textures and canvases; invalidates on dynamic option labels, edited names,
  cursor visibility, selected car, transmission changes.
- Stable garage frames only: original strip-scroll transitions, arrow
  animation remains open; bonus unlocks join Extras.

## Track selection and frontend commit boundaries

Car (state `4`, `FUN_00477586`) ↔ Track (`6`, `FUN_0046cb8a`). Car
confirmation commits the car to config `+dc`, then enters Track; a locked bonus
car stays in Car with sample 4. Transmission `+e0` changes while browsing,
independent of car confirmation. Returning through Name restores the last
committed car, not an unconfirmed preview.

### Unlocks and browsing

- Track's preview (`DAT_0052edc0`) is separate from stored config `+1c`. Fresh
  defaults (`0046a8bb`): stored track 0, progression `+44` = 1, unlock override
  `+90`/copter restriction `+88` = 0 → Donington GP (0) and Silverstone (1)
  available; Thruxton (2), Brands Hatch (3), Oulton Park (4), Donington Short
  (5), Croft (6), Knockhill (7), Snetterton (8) browsable but locked; Lavaland
  (9) outside fresh browsing. `0046ca6f` checks progression/override, then
  `0046caae`'s copter restrictions. Native applies these fresh rules and stores
  code overrides separately from earned progress (Main Menu → Extras → Unlocks:
  track and bonus-car switches; "All tracks" includes Lavaland;
  [code details](cheats.md)). No original saved profile is imported.
- On entry the source bounds browsing to `max(progression, 8)` and restores
  stored `+1c` (out-of-range → 0). Left/Right bounded (no wrap); Up/Down no
  effect; navigation sample 0. Back (sample 2) writes the current preview to `+1c` even
  when locked, returns to Car (one player) or Name (multiple players); the
  original defers this one dispatch via `DAT_0052edf0`, native completes it in
  the same update. Re-entering restores the stored preview. All ten tracks
  commit `+1c` and open their scene through the loading screen; locked tracks
  emit sample 4 without committing.

### Confirmation

Track confirmation checks availability, commits `+1c`, emits sample 2, returns
state `7` in Single Race (`0046cb8a`). `00478690`: writes the original config
(`0046ac35`) unless returning from a session, keeps chosen weather `+a0` in
`+640`, fades/clears frontend output, tears down frontend/CD music (`00468e74`),
resets rendering resources, calls `0044a6e0` (installs the scene dispatcher via
`0044a560`; not a miniature viewer). CD02 continues during native browsing/Back
incl. unavailable confirmation. Scene entry releases frontend resources/samples,
loads scene resources and body/camera placement, starts the track-specific
CD03–06. Controller retains name, transmission, laps, difficulty for later
simulation consumers; car, track, weather, qualifying already select the
initial scene. Escape returns to Main with choices retained and CD02 restarted
([scene lifecycle](scene-lifecycle.md)). Track-text shadows and the texture
color-key state were recovered after visual/source review; for the dark-preview
light correction see Native light repair. Miniature, geometry, projection and
materials: see Track-selection resources and preview.

## Track-selection resources and preview

State 6 uses a rotating CM3D miniature (not a bitmap or the driving scene); the
native view reads model and embedded textures directly from the original archive
through the SDL GPU triangle/RHW path.

| Original site | Consumer contract |
| --- | --- |
| `0x0046c947` | Ten CM3D miniatures from `DATA/LILTRAX.BFP`; member index = track ID (0..8 circuits, 9 Lavaland); identity matches the ten 40-byte records below. |
| `0x0046ca42`, `0x0046cb54` | Ten 40-byte records at `0x004b8098`: name[16], location[16], length in metres, default weather ID; strings/ranges checked before use. |
| `0x0046b7a0`, `0x0046d56b` | Fresh state copies ten 44-byte compiled lap records from `0x004b7be8` (time, driver, car); no blank records, no reference saves. |
| `0x0046dafc`, `0x0046d56b`, `0x0046de59` | Title, track name, location, distance, weather, lap record, help positions; localized strings/fonts. Name/information use `0x00464c36`'s palette-1 shadow (+2,+2). Weather default 2 shows Overcast on tracks 0/8, Cloudy otherwise. |
| `0x0046da56` | English miles use integer divisor 1600, other languages kilometres; both truncate to two decimals. |
| `0x0046c917` | Camera `(0,0,-100)`, zero rotation; miniature `(0,12,0)`, initial Euler `(3584,0,2048)` (4096-unit turns), tables `0x004b8240/50/60`. |
| `0x0047fe20`, `0x0046d433`, `0x004809e0` | Initial rotation `Rz(pi)*Rx(-pi/4)`; each active frontend dispatch appends local Z rotation -34/4096 turns. The SDL-free controller exposes the 12-bit Z angle at exactly 30 ticks per active second with retained fractional time. Track changes preserve the angle; leaving and re-entering Track resets it; presentation FPS/VSync does not advance it; unfocused dispatch pauses it. |
| `0x00468449` → `0x0048f5f9` | Projection X/Y scales `2*(0xb333/65536)`, `2.5*(0xb333/65536)`; near 0.25, far 200; viewport 640×480. The 320×200 rectangle in `0x0046d433` restores background, not the viewport. |
| `0x0046c860`, `0x0046c9eb`, `0x0046d1dd` | Two point-light frames at `(0,32,10)` and `(0,-1.5,0)`, Euler `(0x241,0xf01,0x87)`, constant attenuation 1, range 5000; positions updated before draw (below). RGB 2 for available tracks, `0x3333/65536` locked. Ambient RGB zero (the DWORD high byte is not grey RGB). |
| `0x00494a40`, `0x00495ee0`, `0x00499d10` | Textured materials white diffuse (face RGB 167 is no texture multiplier); untextured faces original RGB. Native: transformed-normal point-light diffuse per vertex. |
| `0x00495ee0` → `0x0048402e` → `0x00484111`, `0x00489069` → `0x0048fae2(1)` | Texture loading sets color key zero; mesh pass enables color key (or alpha fallback). Native indexed preview textures key palette index 0 independent of material opacity. |
| `0x0046cb8a`, `0x0046d234` | Browse range 0..8, disabled right arrow at 8; locked tracks previewable with original help and darker lighting. |

### Native light repair

- Frontend `488f49(0)` selects D3D normal-bearing vertices (`48e837`), not the
  CPU TL branch. The original light constructor is insufficient: `4871b2` →
  `480a80` → `480ab0(case1)` → `47b9e0` overwrites point-light positions before
  drawing; disassembly shows uninitialized local X/Y reads (`47b9f8`, `47ba09`),
  unscaled raw integer Z, overwritten intermediates: a binary defect with no
  deterministic output to port.
- Native helper initializes all Q16 coordinates, computes separate affine
  outputs. `47fe20`'s local Y/Z/X Euler convention gives column matrix
  `Ry(0xf01)*Rz(0x87)*Rx(0x241)`; the caller supplies its frame translation, so
  the operation is `R*Pframe + Pframe` → approximately `(-14.2368,44.2548,37.7680)`
  and `(0.6231,-2.4295,-0.9988)`.
- For readable previews native illumination keeps `46c860`'s initial ambient RGB
  128/255; locked previews scale it by `0x3333/0x20000`; point diffuse
  unchanged. Deliberate presentation correction: original `46c9eb` / `46d1dd`
  overwrite ambient with RGB 0 (packed DWORD `c0000000` / `40000000`, high byte
  alpha). Not a claim to reproduce final original lighting or stack-dependent output.
- No parser normal flip or specular workaround: `494a40` copies signed Q16
  normals at offsets 20/24/28 unchanged; Donington's three node matrices are
  identity, normal lengths differ from 1 by <0.000021. `486fbf` disables D3D
  specular (render state 29). The original black-top capture and native
  dark-side capture are not brightness targets.
- Exact D3D raster fidelity, arrow animation, save-backed lap records and unlock configuration remain open.
