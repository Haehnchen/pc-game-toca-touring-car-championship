# First native frontend route

Route: title → copyright/loading → language → main menu → Quit. Extended by
[selection](frontend-selection.md) (Single Race options, difficulty, name, 2D
garage), [track selection](frontend-selection.md#track-selection-and-frontend-commit-boundaries) and
[Game Options](#native-settings). High scores remain unavailable, using the
original button overlay and sample 4.

## Evidence and ownership

| Source | Implemented behavior |
| --- | --- |
| `0x004681b1`, `0x004684ac`, `0x00468652` | Silent title until any keyboard key or five seconds; copyright shown while frontend resources/audio initialize. Native presentation waits three visible seconds by default; a new keypress skips. |
| `0x004688fc`, `0x0046ff22`, `0x00471ca3`, `0x0047206f` | Frontend BFP member order: icon sheets 0..3, stable ROLL1 artwork 4, unavailable overlay 23, fonts 24..27; eight car groups begin with Audi1 backdrop 29, Audi2 inset 30, alias table 33. Stable menu uses roll index 0; index 5/ROLL6 is only the transition start frame. Selected car's image2 overlays image1 within the translated quad from `0x004b8880`, then ROLL1 overlays both with index-zero transparency; its own 256-color palette and alias active. |
| `0x0046eeef` | English/French/German visible; Italian and Spanish disabled via `0x004b8474`/`0x004b84b8` despite compiled five-language tables. |
| `0x0046561f`, `0x00468fc0` | Frontend strings come from the executable's PE32 pointer tables; native: versioned translations with symbolic text IDs (no original EXE needed). Differ from CONFIG/GSTRINGS scene strings. |
| `0x0046ee06`, `0x0046e427`, `0x0046ddc5` | 640×480 layout, offset (33,15), row spacing 68, original icon IDs, heading/help positions; display code scales and centers the canvas. |
| `0x0046427f`, `0x00464398`, `0x0046400e`, `0x0046473a`, `0x00464e4d` | Font BMP appendices (24-byte header, 16-byte glyph records), glyph rectangles, spacing, smaller-font/spacing fallback. Startup disables kerning; the signed three-band pair calculation is kept for future callers. |
| `0x0046f99c`, `0x0046f526` | Indexed glyph colors/shadows and icon-edge aliasing use the 256×256 palette alias table; palette entries 0 and 155 become black as in frontend palette init. |
| `0x0046e6c2`, `0x0046ee40`, `0x004b6bc0` | Up/down wrap, confirmation, unavailable actions, direct Quit. Initial language and main menus are their own Back states: no parent screen or Quit dialog on Escape. |
| `0x0046dee5`, `0x00463410`, `0x00469f10` | Selected text alternates palette indices 97/144 every eight nominal 30 Hz menu updates; native uses elapsed time. |

- `Controller` (SDL-free) consumes one input/time snapshot per host poll; held
  keys do not repeat menu actions without new pressed edges (original repeat-delay policy
  remains separate). Pointer coordinates stay with the host; no mouse selection.
  Window close = host lifecycle.
- `View` loads title/copyright first; after the copyright frame is presented,
  `load_frontend()` loads indexed images/fonts and embedded constant tables; the
  host loads frontend audio once. Copyright advances after three monotonic
  seconds from its first successful presentation (incl. load time) or a new
  focused keyboard/non-pointer press; loading must finish first; the key that
  dismissed the title cannot dismiss copyright; the dismissing press is consumed
  and does not confirm the language menu. At handoff the host starts bundled CD
  02 and acknowledges `frontend_ready()`. Title dismissal: no sample. Cue IDs: 0
  navigation, 2 confirmation/back, 4 unavailable ([music](music.md)). Return and
  keypad Enter both confirm.
- The indexed canvas is uploaded only when screen, selection, language or
  highlight phase changes; drawn by the SDL GPU renderer.

## Native settings

- Main → Game Options: original Sound Setup, Graphics Setup, Controller Setup,
  Back hub (title `00468fc0`/`004b0bb0`; `00469d0c`; graphics `0046a11f`/
  `0046a2eb`; sound `00475361`; effects volume steps `00475645`/`004756bc`).
  Evidence: [options](frontend-options.md), [graphics](frontend-graphics.md);
  submenus return to their selected parent entry. Cues: 1 changes, 0 navigation,
  2 confirmation/back.
- Fresh settings 1280×720 (16:9); labels show aspect ratio. Choices combine
  4:3/16:9/16:10 presets with SDL display modes and the current size (incl.
  manual window resizing). Fullscreen resolves the closest supported mode;
  unsupported immediate presentation keeps VSync. Menu and saved config show the
  effective values; custom dimensions/FPS caps stay selectable. CD02 continues
  without restart; sound-only changes leave the display untouched. Master
  gain/mute stay config/CLI; simulation/audio scheduling unchanged.

## Widescreen menu background

Original profiles keep the 640×480 composition scaled uniformly and centered;
the extra horizontal area shows the selected original car background photo,
reflected outward from each edge at the same scale (language, Main, Game
Options, Single Race route). Text, frame, icons, garage, track previews keep
positions. The title's extension is separate; copyright and scene unchanged.
`0047206f` backdrop composition and `0047024c(0)` selection: index 0 =
FRONTEND.BFP member 29 (Audi1), member 30 inset, member 4 keyed ROLL1. Only the
unlabelled photo supplies side artwork (corrected menu palette). One retained
texture serves both margins; geometry uses the current swapchain size (incl.
first frame after resize).

### Random menu backgrounds

`FUN_0046846b` (before frontend loading) selects one of eight car motifs; the
port draws it once from the shared source random stream and keeps it through
race returns (one index for every View recreation). Diagnostic
`--menu-background 0..7` override (not saved). `FUN_0047024c` advances five
archive members per index (four images + the car's alias table). `frontend.bfp`
members: Audi 29–33, Renault 34–38, Honda 39–43, Volvo 44–48, Ford 49–53,
Nissan 54–58, `PEAUGOT` 59–63, Vectra 64–68. Original profiles use each
selected `1`/`2` pair, palette and alias; Mega the matching panorama. Image 3
joins the page turn; image 4 is unreachable in the original phase cycle.

### Menu page turns

- `0046e6c2` starts a roll on page confirmation or Back (Up/Down selection and
  editable values: ordinary cues, no turn). `00463410`/`00469f10` clock 14
  phases at 30 Hz: forward 0→1…13→0, Back in reverse, 466,666,667 ns total.
  Startup begins at 5 and takes 300 ms. Confirming the original Back icon 16
  also reverses the roll. Native input accepts new edges during presentation; a
  new page replaces the pending visual target.
- `00471ca3` draws old/new snapshots through independent title, content and help
  scan windows. Native loader reads the three 14×8 signed 32-bit tables at
  `004b8b00`, `004b8880`, `004b8d80`, applies `00472281`/`004722fb` and
  `00470e74`'s Y correction; scan is affine per row using the left Y endpoints.
  Phase 6 omits all dynamic content. Forward phases 1–5 keep the old page, 6–13
  the new; reverse phases 6–13 keep the old, 1–5 the new.
- Original profiles use all 14 ROLL images, FRONTEND.BFP members 4–17.
  `0047206f`/`00470e74` keep image 1 full-screen, clip image 2 or 3 at its
  original coordinates to the moving content opening, overlay ROLL. Image 3:
  phases 3–5 and 9–11; image 2 the others. The counter resets at 2, so the
  image 4 branch is unreachable (checked against `4721dd`–`472241`).
- Mega keeps its panorama; authored frame, title, help, content share the
  content-plane motion (lettering fixed on the metal throughout); the thin
  frame has no thick ROLL side faces. Fonts, icons, garage layers, track
  geometry move with the page. Stable frames keep exact artwork/proportions.
  With a full-window Mega panorama the UI clip area covers the drawable so the
  rails can enter the side margins at 16:9/19:9; original scale/origin and text
  attachment fixed. Original profiles keep their source clip area.
- Snapshots own outgoing canvas/sprite resources through the turn; an explicit
  write mask preserves keyed text and opaque garage pixels whose palette
  indices equal the background. Active frontend time pauses on focus loss; trace
  fields `menu_animation_ns` / `menu_entered_ns` let tests wait for settling.
- Row clipping reuses three temporary vertex buffers per menu region:
  horizontal clipping preserves output across rows; vertical clipping clears
  and refills separate scratch/output buffers; interpolation, depth/UV
  attributes, triangle order, draw merging unchanged.

### Mega menu background

- Newly generated blurred panorama of the startup-selected car across the whole
  window incl. 4:3 (blur baked in, no runtime blur). Audi index 0:
  `assets/ui/mega/menu-background-wide.webp`, 1672×941 WebP80, 29520 bytes,
  CRC32 `05ba84ef` (recreates FRONTEND.BFP member 29 composition). Seven more
  (Renault/Honda/Volvo/Ford/Nissan/Peugeot/Vectra): 1672×941 opaque WebP80,
  uniform `0x16` ImageMagick blur, 177324 bytes total, canonical image1 as motif
  reference, Audi panorama as wide/blur reference.
- Frame: `menu-frame.webp` 1280×960 WebP80, 72438 bytes, CRC32 `1a84b7fd`;
  rounded shape and yellow help bar of member 4/ROLL1, transparent outer margins
  and center; heights corrected and yellow bar ~3 logical pixels lower
  (2026-10-18 regeneration). Registration embedded; common help origin stays
  y 440 for all profiles.
- The GPU texture scales uniformly to cover the drawable, cropping around its
  center. Frame, icons, text, garage, track previews keep centered positions.
  Mega omits ROLL/car image layers and reflected strips. Graphics Setup
  applies and saves detail changes immediately; leaving retains the selected
  profile. The four original detail profiles keep original artwork. Title,
  copyright, championship honors separate.

## Mega menu icons

- Mega replaces all 39 reachable menu symbols in both selection states, five
  navigation arrows and the unavailable cross: 84 generated transparent WebP80
  sprites. Motifs from FRONTEND.BFP members 0–3 and 23; member 29 supplies the
  active Audi palette (black entries 0/155, transparent index 0). The six native
  Extras/Fullscreen/VSync/FPS/Camera/Unlocks badges use their geometry as
  references. Final artwork totals 1,494,876 bytes.
- `tools/menu_icon_assets.py`: `export` writes reference sheets under ignored
  `build/menu-icons/references/`; `inventory` prints canonical cells/provenance;
  `import --batch N --input IMAGE --cells RECTS.json` crops a reviewed sheet
  (explicit `[x,y,width,height]` crops, ordered normal/selected per row) into
  352×224 badges, fitting each crop proportionally into a transparent target
  canvas; auxiliary import `--aux-spec` with named crops and original logical
  dimensions; validates every crop, preserves alpha, never enlarges source
  artwork. Only generated final WebPs and metadata tracked.
- CMake bundles `assets/ui/mega/menu-icons/` beside the executable; the
  frontend caches each GPU texture and draws it in the ordered overlay with Mega
  fonts, keeping 88×56 logical badges and original arrow dimensions/positions
  (nothing reduced to the 640×480 indexed canvas). Preview switches immediately
  in Graphics Setup; High/Very High use original icons; missing optional artwork
  keeps the source sprite.

## Widescreen title

- At exactly 4:3 `assets/ui/original/title.webp` (converted once from TITLE.BMP
  with TITLE.PAL baked into opaque RGB). Other aspects:
  `assets/ui/mega/title-wide.webp` (1672×941 WebP70/method 6, CRC32 `c40a7422`),
  a complete generated composition (two-car scene, stormy sky, flame flag,
  title lettering from a window-only reference; requested 2026-10-17 replacing
  side strips). Renderer chooses by the actual drawable size (also after
  resize), scales uniformly to cover and crops around the center (16:10 trims
  sides, wider aspects top/bottom). Title timing/silence unchanged.
- CMake copies the four committed startup WebPs beside frontend executables
  under `assets/ui/` (found via SDL base path); no BMP/PAL fallback or build
  conversion. Importer and source/output CRC32:
  [`startup-images.txt`](../assets/ui/original/startup-images.txt).

## Copyright splash

`assets/ui/mega/cpyright.webp`: 1448×1086 reconstruction of the 640×480 splash
(WebP70/method 6, CRC32 `d7e9a5e5`) with title, Codemasters logo, legal text and
full 4:3 composition. All profiles select this same-name override through the
shared artwork loader, fallback `original/cpyright.webp`. Drawn at native
resolution into the centered 4:3 viewport; 16:9/16:10 black side bars (no
stretch/crop).
