# Time Trial

## Time Trial core

Time Trial uses the normal menu route, a real automatic approach and unlimited measured
laps. Completed best laps become a translucent presentation-only ghost: no Motion actor,
collision, damage, tyre effects or engine audio. A saved ghost keeps its own car,
weather and driver independent of the current player. The approach uses the original
`45eb60` Time Trial caption GSTRINGS51 (GET READY / BEREITMACHEN; qualifying uses
GSTRINGS50) with the original target/flash timing, removed at player handoff.

Ghost rendering keeps scene depth testing and writing in original sector submission
order: `489069` enables both after the sky; `495ee0` changes material diffuse/ambient and
alpha 150 without disabling depth writes. Later background guardrails cannot overwrite a
nearer ghost; foreground geometry still occludes it; later background geometry does not
show through an already drawn ghost (source draw order, no sorted transparent pass).
Mega-generated interiors use body-attached shallow positions and alpha 96; Mega Ghost
wheels also alpha 96; exterior and original-model Ghost alpha 150
([Mega model contract](cm3d.md#encoding-and-materials)).

### Source contracts

Evidence: `references/decompiled/tourcars.c` (patched 1.1a only).

- `449f50` enters state4/session-kind 1. First valid ordered start-line crossing
  (`421390`) discards the approach, restores player controls, starts measured lap
  1/session-kind 2. The qualifying three-lap finish does not apply.
- `403dd0` requires six ordered checkpoints. Incomplete start crossing calls `45dd60`:
  restart attempt and ghost cursor, keep the completed best, award no lap. Reverse
  crossings award no lap.
- `45dac0`/`45dcb0` promote only a strictly faster complete lap; equal/slower keep the
  previous best. No-record sentinel 999999 ms.
- `45dee0` stores two basis rows as six signed 16 components (`Q16 >> 2`) and three
  signed 32 world coordinates. `45df60`/`45e120` interpolate these and reconstruct the
  third row by fixed-point cross product without normalizing.
- `45e7a0` (pause actions 7/9, before releasing the scene) writes only an improved
  completed best. Load Best Lap No still protects the disk best; a slower session best
  may play without replacing a faster saved record.

### Native recording and storage

- `TimeTrial::update(active_ms, pose, events, last_lap_ms)` follows the same
  `Session::update` call with the body pose and cumulative active ms. Each normal 40 ms
  simulation deadline records once (presentation rate and pause time do not choose the
  clock). `GhostPose` rows: matrix `[row*4+axis] /65536`, translation `[12+axis] /65536`.
- Recording keeps the complete valid lap. The original 3600-pose limit is replaced by a
  duration bound: longest recordable lap 999998 ms (below the sentinel); at 40 ms
  intervals incl. origin and an off-tick finish at most 25001 poses
  (`ceil(999998/40)+1`). Extra host updates do not append poses < 40 ms apart; identical
  timestamps replace the same pose; the finish is kept between samples. An overlong
  attempt stops growing and cannot become best. A faster lap atomically promotes poses,
  time, car, weather, driver, then restarts recording and playback at zero.
- File: `time-trial/track-N.ghost` under the writable user-state root, little-endian:

| Part | Contents |
| --- | --- |
| 32-byte header (`<4s7I`) | `TCTT`, version 3, track, car, weather, lap milliseconds, sample count, name byte count |
| Name | Name bytes, length from header |
| 28-byte poses | Six signed16 basis components, three signed32 coordinates, unsigned32 elapsed milliseconds |

- `load(root, use_saved_best)` skips unknown magic/version like a missing file (no
  warning/pause); supported files are validated before exposure. Allocation/read limit
  `32+255+25001*28` bytes (700315), exact size, monotonic timestamp and metadata checks;
  no checksum or footer. Playback includes the exact finish pose and ends after it
  without extrapolation.
- Yes supplies the saved pose stream and best time; No keeps only the persistent
  comparison baseline and records fresh. A faster lap marks the record dirty;
  `save(root)` uses the shared atomic state writer, failed writes keep the dirty record
  for retry. Missing/unsupported files leave Time Trial usable without a ghost; the next
  best lap saves the current format. Canonical game data unchanged.
- `records.dat` changes only through completed local lap records. End Time Trial saves
  an improved ghost, shows the two original record pages, returns to Main with CD02;
  pages and track preview use the same record table (see Time Trial frontend).
- `toca-time-trial-tests --fixture CANONICAL-ROOT STATE-ROOT` traverses all 254 Donington
  gates synthetically with HPC-supported Audi poses through the Session/TimeTrial pair
  (101600 ms / 255 samples), saves a `.ghost`, reloads it for a Honda player. Data
  plumbing only; no physically driven lap claimed.

### Accelerated driven acceptance

`native-time-trial-driven` uses the [diagnostic step bridge](full-lap.md#faster-diagnostic-scheduling)
(held actions at every normal 40 ms deadline; simulation, Session, HUD and offline audio
update normally; menus, approach, save and fresh-process playback use the real clock).
It traverses all 254 Donington gates, saves ghost and record table on ordinary exit,
verifies every stored pose/timestamp against the actual body trace, loads and plays the
ghost in a fresh process; an incomplete second attempt preserves both files. The
historical driven result predates the complete-lap recording bound (it used the former
3600-sample prefix limit) and does not validate it; the synthetic 999998 ms lap retains
all 25001 poses, survives save/reload and plays through its off-grid finish. The full
physical X11 run remains separate evidence.

## Time Trial frontend

### Menu and pause

- Flow: Weather, Load Best Lap and Continue lead through Name, Car, Track. Native keeps
  weather, load choice, selected car/track and the previous mode; switching modes restores
  Single Race's own lap/qualifying choices. Pause actions are mode-aware with a localized
  quit confirmation.
- `4691a9` restores mode 2's Time Trial race-menu selection. `4698f7` authors exactly
  three rows at `4b71c0`: Weather (icon 28), Load Best Lap (icon 30), Continue (icon 13);
  initial selection Continue. `469979` chooses mode 2/one player/one lap. `4660c7`
  formats the localized Load Best Lap Yes/No label; `46612a` toggles config+30; fresh
  configuration `46a8bb` sets it to 0 (initial No). It controls loading a saved ghost;
  a faster lap still records and replays the new best. Continue enters Name directly, then
  the garage and Track. Difficulty and qualifying selection belong to Single Race.
- `45e6a0` loads the selected track's record. Its saved car selects the ghost model via
  `443280`/`45e910`; its weather byte is metadata. Loading does not replace the player's
  car/weather. No save-slot or ghost selection screen. Persistence/timing:
  see Time Trial core.
- `440510` mode 2 suppresses End Qualifying and uses GSTRINGS28 END TIME TRIAL. `441720`
  action 1 opens confirmation state2 with selection 2 (No); GSTRINGS123 = QUIT TIME TRIAL?;
  clears the Escape gate. No (action 8) restores the pause root at Continue; Yes (action 7)
  calls `45e7a0` before leaving. Native `end_time_trial` lets the application finish the
  write before resource release; a failed write keeps the run and shows a bounded notice
  on the reopened pause page. Samples 78/79/80 = navigation/confirmation/option cues.
  Sound Done returns to Continue; Escape from root/sound resumes the attempt.

### Ghost presentation evidence

- `4437f0` loads three Time Trial actors; player actor 0 passes 1 and ghost actors 1/2
  pass 0 to `44dc6d`, then `494a40`. The loader passes `(argument3 == 0)` to `495ee0`,
  which creates the ghost material via `499d10`. Hardware path: diffuse RGBA
  150/150/255/150, ambient 100/100/180/150. Software path: diffuse 130/130/255/255,
  ambient 100/100/200/255. Blue tint with translucency on the hardware path. Actor+140
  visibility follows `52b3e4`; only the selected ghost buffer is visible. No wireframe
  branch found in this chain. Full ghost-image parity remains open; material recovery
  alone does not establish complete rendering equivalence.
- `44a6f0` updates local engine/WHIRR layers only for config+ac players (one in Time
  Trial), so ghosts get none. `427xxx` clears `ed968` for ghost actors; `446b50` bypasses
  their slip consumer and queues their tyre voice stop through `425e70`/`425e90`. Ghost
  poses need no collision, damage or audio simulation actor.

### Track record pages

- `46b7a0` initializes ten 44-byte records from executable `4b7be8`: every default time
  9:59.99; names ANDY, LES, RICH, SCOTT, PAT, JON, PHIL, BRYAN, IAIN, FIRELORD; car IDs
  0/1/2/3/4/5/6/7/0/1; verification codes empty. Independent of saved ghost streams; the
  native table receives the persisted record array and does not synthesize high scores
  from a loaded ghost.
- `46b957`/`46bb05`: five tracks per page; localized Track/Time/Player headings from
  `4b1f18`/`4b1f30`/`4b14b0`, abbreviations at `4b7bb0`, manufacturer badges (`470d63`,
  frontend archive member 22). Headers y 78; first record y 124; 49 px steps, original
  font. Fixed-width digit cells; times truncate to hundredths (`46b878`/`46bc37`). A
  nonempty verification code sits 18 px below its time. Improved-track rows use palette 97
  instead of 144. Lavaland's row is omitted unless `46ca6f` permits it; other locked
  tracks keep their records. Track selection reads the same saved time/driver/car.
  Centering uses `64dff`'s separate integer half-width divisions (a combined division is
  off by 1 px).
- `46bd22`/`46bf63` consume Enter/Escape, ignore arrows. Enter: page 0 → page 1; Escape:
  page 1 → page 0, page 0 → Main. Enter after page 1 returns to Main after a Time Trial.
  Main High Scores shares those pages, then opens the nine-row Championship table before
  returning to its retained Main selection; Escape from that table returns to the second
  Time Trial page ([Championship high scores](championship.md#championship-high-scores)).
  Transitions keep original confirmation/back sounds and CD02.
- `426f20` calls `446240` after a measured Time Trial lap and suppresses track record
  updates only for `XBOOSTME` (config+8c). `446160` requires a strictly faster time.
  Stored milliseconds = displayed hundredths × 10. The 10-character visible verification
  code comes from `420670`/`420870` (distinct from the IEEE CRC32 on the native records
  file). Persistence uses the normal state writer outside canonical game data; ghost
  saving has its own source gate.
- Verified against the live original for the record pages: glyph placement matches
  (numeric cells exact, header cores overlap); this establishes table glyph placement,
  not full background-image parity.
