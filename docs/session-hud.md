# Local lap and timing HUD

## Mega artwork

- Mega loads newly authored 4× WebP atlases from `assets/ui/mega/hud/`: wide italic
  digits, colors, flat labels and the curved 12-segment tachometer redrawn as clean
  contours with bevel shading; start housing and red/green lenses generated from the
  original reference with Imagegen. Lossless source atlases keep alignment in the
  original sprite rectangles. No runtime pixel upscaler. Other graphics presets keep
  canonical artwork and nearest sampling.
- The twelve HUD files keep WebP quality 80 and transparency; new exports use
  quality 70/method 6. Only the primary atlas varies between KpH and MpH; five atlases
  per HUD. Six more font atlases serve scene and frontend text. The existing WebP
  decoder is reused.
- `tools/build-hud-assets.py` authors the font/gauge atlases (needs Python + Pillow,
  no game-data input). Light atlases are committed; source/output identities in
  `assets/sources/hud-lights/` and `lights.json`. Original reference data stays
  read-only. Geometry: `tools/hud_font_geometry.py` (both font sizes),
  `hud_caption_geometry.py`/`hud_small_caption_geometry.py` (game fonts),
  `hud_menu_font_geometry.py` (frontend), `hud_art_exports.py` (sprite export).

```sh
python3 tools/build-hud-assets.py --sprites build/hud-art/sprites --preview build/hud-art/overview.png
```

Writes atlases, all 370 named WebP70 cutouts, a source-rectangle/CRC32 manifest and a
visual overview; `--output DIR` selects another atlas destination.

- Font/gauge generator: saved Bezier contours for large digits, R/N, labels, units.
  Small red/yellow glyphs: independent upright masters with shared rounded bowls,
  stroke widths and one common italic shear; punctuation stays upright. RPM outer
  edge = one analytic ellipse, inner edge = constant 12-unit inward normal offset
  (no spline inflections); 12 adjoining sectors with uniform dividers; both lit
  sprites and the dim background are cut from the same band into the original atlas
  rectangles.
- Small red/yellow digits/punctuation keep a one-pixel gray8 edge (times, speed, lap
  limits, splits). Canonical IMAGE640 members 32/33 use exactly this square dilation
  of each colored face, clipped to its original cell (CRC32 `95b86263`/`8ee805b8`).
  Large digits/labels already carry dark edges; Gear and MpH/KpH share the original
  gray8 backing.
- Start-light masters: lossless WebP `assets/sources/hud-lights/start-{housing,red,green}.webp`
  (512×512, 256×256, 256×256), exact encoding (RGB under transparency kept);
  `provenance.json` records generation and source CRC32. Runtime WebPs: quality
  80/method 6, exact RGBA; no crop/mesh/positional transform. Source package stays
  outside the runtime HUD directory (build copies only runtime artwork). Lamp sockets
  use the original 49-pixel spacing, red/green lenses centered in each ring (housing
  artwork centers all three sockets).
- Mega uses clamped linear sampling (lap 1's top edge must not wrap into the black POS
  strip at the opposite atlas edge). The lap-limit slash has a visible pale stroke.
  Small numerals use their own original proportions; large numerals keep full source
  height. Gear and MpH/KpH follow the original contours incl. lowercase p's descender.
- Values, digit advances, draw rectangles, anchors, RPM thresholds and start phases
  use the same recovered logic as the original HUD (GPU fixture compares every
  vertex/UV/index/draw count at several aspects).

### Complete Mega font set

Mega replaces six HUD/frontend fonts and the vehicle-name font with independently
authored contours:

| Original source | Artwork | Glyphs | Style |
| --- | --- | --- | --- |
| IMAGE640 member23 BIG640 | `caption-big.webp` | 46 | Bold italic red captions |
| IMAGE640 member24 LIL640 | `caption-small.webp` | 61 | Upright white pause/result rows, accented letters |
| FRONTEND member24 OPTION3 | `menufont-0.webp` | 44 | Wide upright menu text |
| FRONTEND member25 OPTION3S | `menufont-1.webp` | 44 | Narrower menu text |
| FRONTEND member26 HELP3 | `menufont-2.webp` | 48 | Compact help text |
| FRONTEND member27 TITLE3 | `menufont-3.webp` | 38 | Italic metallic headings |
| ALLCARS member0 WIN3 | `car-name.webp` | 38 | Upright white vehicle names, 2× |

- HUD/frontend atlases keep 4× resolution. WIN3: 512×512 atlas, 256×32 runtime name
  bitmaps (exactly 2× original); source cells, ink bounds, spacing, centering fixed;
  continuous alpha + clamped linear sampling ([vehicle name contract](cm3d.md#encoding-and-materials)).
- OPTION3/OPTION3S include the two parentheses the native frontend adds (same cells and
  advances); other glyph inventories unchanged. BIG640/LIL640 cover qualifying/Time
  Trial preparation, pit entry/exit, finish messages, result tables, ESC menus,
  Championship dialogs/honors, finale credits. The four frontend fonts cover menus,
  options, names, selections, records, help. Callers keep original colors (selection
  dimming, shadows). Honors uses the original white-face/black-edge canvas override;
  credits white/black; scene text gray8 edges.
- Original glyph rectangles, pair spacing, advances, strings, wrapping, positions and
  flash deadlines drive every caller. Frontend text uses GPU quads above the indexed
  canvas; later canvas sprites keep original draw order (badges over long names).
  Menu highlight colors tint shared textures; game-font color caches keep only the
  current canvas's colors. Each glyph is antialiased independently before packing, with
  a transparent output-texel border. Mega menu contours use straight C/G inner lips and
  stroke-bounded curve handles (no hooks). Changing graphics detail invalidates the
  text cache; detail 0..3 use the original fonts, also after returning from Mega.
  Spanish-specific punctuation is part of the export.

## Original value and sprite contracts

`41a760` caches actor values for `41de90`; native HUD receives `Session` records via
`Hud::set_session(HudSession)` (no tachometer smoothing rerun). Initial records during
Loading, then once per simulation deadline; drawing/focus resume only read the cache.

Original single-player 640×480 layout:

| Value | Atlas / source | Destination |
| --- | --- | --- |
| Lap panel | primary, rectangle `4ad9b8` `(0,212,186,28)` | `(11,9)` |
| Current lap | primary large digits; `41e480` flags 3 | right edge 132, y 5 |
| Lap limit | primary small digits | x 146, y 27 |
| Current lap time | primary small digits | x 286, y 11 |
| Best lap time | primary small digits | x 286, y 26 |
| TIME/BEST labels | primary, rectangle `4ad9a0` `(178,186,72,25)` | `(208,12)` |

- Primary atlas = IMAGE640 member 32 (KM) or 33 (MILES); secondary large glyphs =
  member 36. `41e480` flags 3 uses the primary large-digit table with no vertical
  offset; gear flags 8 uses the secondary table with y+106. Every rectangle (incl.
  punctuation/unknown-time rows) is checked against its decoded atlas. Uniform scaling
  keeps the exact layout at 640×480; explicit anchors place groups on wider drawables.
- `44a6f0` caps the displayed lap at the race limit, or 3 in qualifying; `41de90`
  prints total 3 for qualifying regardless of menu lap choice. Race position display:
  [Single Race rules](single-race.md#single-race-lap-and-finish-records).
- `403dd0` derives minutes/seconds/centiseconds from elapsed ms; `41de90` formats
  `%02d:%02d.%02d`. Native truncates the final centisecond and caps at `59:59.99`.
  Actor reset zeros current time (`00:00.00` before the measured start; no unknown-
  current marker). Best stays `--:--.--` until a measured lap gives a nonzero best
  (`44a6f0` / `4c3310=-1`). `41e5f0` selects primary y 106 for numeric time and y 118
  for the whole unknown string (colon and dot included); original sprite glyphs,
  individual widths, two-pixel advances.

## Widescreen layout

640×480 positions above and the gauge/start rectangles in
[the resource contract](driving-audio.md#barrier-audio-and-local-driving-hud) are the baseline. Native scale =
`min(width/640, height/480)` on both axes:

| Group | Anchor |
| --- | --- |
| Lap panel, current lap and limit | Top left |
| Current and best lap times | Top center |
| Speed, gear and RPM | Bottom right |
| Start housing and lamps | Top center |

Margins scale with the sprites. Resizing changes only presentation (not cached values,
deadlines or start cues). The start support repeats its original strip from the centered
housing to the right edge, clipping the final repeat; lamps and support lattice are not
widened. Qualifying results and finish text keep their centered layout. Linearly
filtered Mega support gradients may differ by one 8-bit RGB step after translate/clip
(77/155); original nearest-sampled pixels stay exact.

## Qualifying preparation caption

- CONFIG GSTRINGS line 50 (recovered `45eb60`'s missing `4d5200` text): `PREPARE TO
  QUALIFY` / `QUALIFICATIONS` / `AUF QUALIFIKTN VORBEREITEN` (EN/FR/DE). IMAGE640
  member 23 BIG640 font, centered at `y=(0x370a*480)>>16=103`; `464935` wraps words
  against the 640-wide canvas (German: two centered lines, 39 px apart). The port reads
  the original localized strings/font, keeps wrap and uniform scaling at 4:3/16:9/16:10.
- `45eb60` reads actor target `cc5ba` (next target advanced by `403dd0`): targets 0..4
  hide the caption, 5..count-4 show it continuously, the final three targets use
  `41eca0`'s strict 660 ms-visible/1000 ms flash cycle. The application gates it to the
  automatic approach, removing it at the first ordered start crossing. Time Trial uses
  GSTRINGS51 (GET READY / BEREITMACHEN), same placement/timing. No audio call.
- TIME/BEST labels: `41de90`'s first single-player blit uses the combined primary
  rectangle above; `41ad80` selects it via `4ad990+2*8`, positions it at 208,12. The
  English atlas labels also appear in French/German sessions; they share the top-center
  anchor with both time values.
- Source CRC32: CONFIG.BFP `22f90f1d`, IMAGE640.BFP `1060ace3`; executable `8ce56d41`.

## Integrated acceptance

Checks cover selected lap limit, initial zero time, time unchanged during focus pause,
cached HUD/session agreement at every deadline, qualifying three-lap display. Full-lap
original trace comparison and finish/results remain open.

## Complete-lap follow-up

[One-car lap acceptance](full-lap.md). `403dd0`'s incomplete qualifying start crossing
restarts the time origin without awarding a lap, keeping the raw elapsed value already
computed for that update.
