# Championship

Recovered from the canonical 1.1a export and selective disassembly of its unexported
state-handler labels. `race/championship.*` owns season rules; `settings/championship.*`
stores five named native slots per championship variant.

## Menu evidence

Normal Championship and unlocked TOCA Showdown both enter New/Load (`4698c5`, `469867`
return state `2f`). Showdown prepares the five-row options state, but the canonical New
Game row at `4b0738` targets state `32`, whose handler `469ab1` shows only Short/Full and
Continue; Load targets state `30`. Both submenus keep the CHAMPIONSHIP title (`465e59`,
`469a28`). Name entry skips Single Race difficulty (`4697a1`); garage confirmation routes
mode 1 directly to scene state7 (`477586`), no free track selection.

## Schedule and classification

- `44f0a3`, `44a0c0`, state-name strings around `453028`, table `4af0a0`: twelve
  meetings, each **Qualifying 1 → Race 1 → Qualifying 2 → Race 2** (three measured laps
  per qualifying). Tracks **0,1,2,3,4,5,6,7,8,2,3,1**. No reverse-grid shortcut.
- `44d420` full race laps by track 0..8: **18,20,20,38,27,25,25,35,23**. Short divides by
  3 with integer truncation (`44f0a3`); all 24 races kept. Options expose only Short/Full
  and Continue (`469a28`); no difficulty selector.
- `420460` reads AIBIN driver bank **race index +1** (1..24), not Single Race banks 24..26.
  `444670`/`444730` use each race's own QUALPC min/max pair. `4512b0` meeting weather IDs
  **2,5,2,2,1,3,2,1,2,3,5,2**, horizons **1,4,2,1,0,3,1,0,2,3,4,1** (Croft cloudy horizon
  intentionally differs from the ordinary menu default).
- `444270` awards **one pole point**. `422720` classified top ten
  **15,12,10,8,6,5,4,3,2,1**. The original classification array orders
  finishers/retirements (no second DNF rule). The player's race penalty reduces newly
  earned race points only. `422880` sorts points descending, ascending actor ID on ties.
  Native result ranks are zero based.

## Progress, awards and ending

- `4af0b8` cumulative meeting targets **20,40,…,240**. `4283b0`/`44a0c0` stop an
  impossible campaign after Race 1 when more than 16 points remain to that target; after
  Race 2 the full target is required (`4491b0`). Both exempt the final meeting. `449de0`
  records the result and exits (no retry); a saved earlier checkpoint can be loaded.
- `4492e0` awards one badge per six races: ≥4 wins gold, ≥4 top-two silver, ≥4 podiums
  bronze. The three podium counters reset on entering the next block (`44f0a3`, races
  7/13/19). Four badge slots survive saving.
- Later meetings expose the previous circuit (`44f0a3`, threshold vs current track −1);
  from race 19 threshold 9 is restored. Completing all 24 races enables TOCA Showdown
  (`449790`, config+38). Normal-championship gold in the last block also names LAVALAND
  (`427cb0`). Showdown awards bonus-car flags +40/+3c for gold in blocks 2/4 (no ordinary
  championship bonus-car award). Earned progression merges with Extras independently of
  direct overrides.

## Championship high scores

- `426f20`→`4462d0`/`446350` inserts into one nine-row table shared by normal, Showdown,
  Short, Full. Higher points first; a new equal-point result precedes existing ties.
  24-byte row: 12-byte driver name, points, round, four packed 2-bit medal tiers.
  Defaults at `4b7da8` begin with PATRICK and BRYAN on 225 points, end with RICH on 6.
  The active configuration reader/writer keeps the table at `TOURCARS.CFG+33c`;
  `HI_SC_C.TAB` helpers have no observed callers.
- `449de0` submits on elimination entry alongside its speech, before confirmation.
  `449790` submits after the final honors, before the convoy. Race transitions, manual
  abandon and host shutdown leave the table unchanged. XBOOSTME suppresses submission;
  other original codes stay eligible. Completion stores round 24; elimination stores
  `2*meeting+2` after Race 2 and `2*meeting` after Race 1 (original increment order,
  zero-based native meeting index).
- Main → High Scores: both Time Trial pages, then the Championship table (Enter
  continues, Escape back). Post-Time-Trial path returns to Main after two pages. Page:
  Player, Points, Round, four original medal icons, original first-row color cycle,
  localized headings, confirmation/back sounds, CD02; centered original proportions at
  4:3, 16:9, 16:10.
- Native storage `championship/records.dat`: TCCR/version 1, nine 24-byte rows, IEEE/zlib
  CRC32 over the preceding 224 bytes. The atomic writer persists a prepared table before
  consuming the terminal transition; the prepared table survives write failures (also
  after rename), so a retry inserts once; separate equal results stay eligible. A
  corrupt/unreadable table shows the localized file-error notice and stays untouched
  (reopening after repair reloads). Failed terminal writes stay on the honors/results page.
- Cosmetic storage waits and the honors input lock remain as documented (not reproduced).

## Named saves and honors

- Original saves.dat: ten records, five normal + five Showdown (`465898`). Slot names =
  stored driver names; Short/Full, one-based race number and points in the slot detail
  (`46598f`, `465aac`). Empty slots cannot load. Native paths
  `championship/normal/slot-1.dat`..`slot-5.dat`, separate `championship/showdown/`; a
  corrupt file reports its own error without hiding others.
- `44a0c0` advances Race 1 directly to Qualifying 2. After Race 2's standings and points
  gate `4491b0`/`423bd0` offer **Save Game?** (initially Yes); `423be0` supplies five
  destinations + Cancel. Occupied destination asks **Replace Existing Game?** (initially
  No). Success shows the original saved message 3 s; slot Cancel/declined replacement shows
  Save Cancelled 2 s and returns to Save Game. No at the initial prompt continues
  immediately. Final completion has no ordinary save prompt. Session boundaries keep state
  in memory; only an explicit slot save replaces a file; no migration or autosave. Save
  failures keep current state and permit another destination or retry. Feedback timers
  keep active-time/focus behavior; the original cosmetic 1 s/2 s storage waits and 10 s
  honors input lock are not reproduced.
- `466170` stores name, variant/length, one-based race, track, 16 scores, car/transmission,
  podiums, four badges, two race-condition fields. Native format adds actor roster,
  classified grid and stage with header/version and IEEE/zlib CRC32. `settings/storage`
  shares bounded little-endian fields, exact-size/header/CRC32 checks and atomic
  replacement with championship and lap records; the three version 1 layouts stay
  byte-compatible; field/domain validation stays per format. Stage validation prevents
  duplicate pole/race scoring. A resumed session starts at its beginning (no mid-lap
  physics).
- After races 6/12/18 the save flow precedes the honors page; race 24 goes straight there
  (`423be0`, `449350`). Capture podium counters before advancing resets them. `427cb0`
  shows each block's first/second/third counts, medal tier, earned rewards. 640 px
  backgrounds: `DATA/CSCREENS.BFP` members 0/3/6/9 (Cover, Flame, Hill, Start); widescreen
  keeps the centered composition. Normal awards include all codes up to the earned tier:

| Block | Bronze | Silver | Gold |
| --- | --- | --- | --- |
| 1 | CMSTARS | CMNOHITS | CMMICRO |
| 2 | CMCOPTER | CMHANDY | CMMAYHEM |
| 3 | CMRAINUP | CMTOON | CMFOLLOW |
| 4 | CMCHUN | CMCATDOG | LAVALAND |

Showdown requires four wins for a reward: CMUPSIDE, FLEXMOBILE, CMIMPOSS and the bonus tank
respectively. Codes are revealed for later entry; displaying a code does not enable its
effect. Bonus cars/tracks keep their earned unlocks.

## Final convoy and credits

- After the final honors `449790` runs the Silverstone closing convoy. `DATA/CHAMPFIN.BFP`:
  member 0 Pace Car, 1 transporter, 2..9 team driver figures, 10 credits. The three
  championship leaders use actor/2 as member offset, attached to the transporter at the
  exact source translations; duplicate teams keep separate figures. The original
  archive's team-order mismatch is preserved (no remapping).
- Mega detail 4 loads `finale/pace-car.glb` and `finale/transporter.glb` from the model
  bundle ([Finale registry](../assets/models/mega/finale.json); artwork/geometry
  acceptance: [finale-generation](../assets/models/mega/finale-generation.json)); missing,
  corrupt or incompatible files fall back independently. Original pivots, variants, eight
  driver teams, credits intact. Shared texture cache decodes embedded WebP with explicit
  alpha; original BMPs keep their black key.
- Actor 0 starts at GRID slot 4, actor 1 at slot 1 (first XZ endpoints and headings); both
  follow original POINTS edge midpoints through the shared driving code. State18's speed
  and separation brakes keep the convoy together. Ordinary Silverstone renderer supplies
  scenery/horizon; racing car and HUD hidden. Showdown and upside effects reflect only the
  3D world; credits stay upright at every aspect.
- Convoy/driver geometry and normals stay resident in object space through the shared
  mesh/texture caches. GPU instances carry node matrices and lighting; the shader keeps the
  former CPU consumer's translation-first position additions, then applies the camera
  separately. Original BMP point sampling, black key, material order, depth kept.
  Same-team figures share buffers; consecutive identical ranges can be instanced.
  `frame_into` reuses draw/instance storage. `model_triangles`/`finale_model_triangles`
  count submitted source triangles before GPU culling.
- Camera/credit phases repeat on the original 4/8/12 s timer; 1/16 s quantization and
  strict comparisons give 4080/8080/12080 ms at 25 Hz. The orbit keeps the last
  player-camera heading; other phases use authored track cameras. `419870`/`4198f0`
  parse the literal CRLF/PAGE/END stream: the leading PAGE makes the first switch empty
  (text first appears after 8 s); END rewinds. BIG640/LIL640 fonts, centered headings and
  rows. No automatic ending; keyboard/gamepad input returns to Main after the first 4 s of
  each cycle. Focus loss/minimization freeze the whole scene.
- Audio: `449c60` restarts CD04, `44a1d0` polls it without looping; independent of the
  in-race music switch; volume/mute apply. `44a6f0` keeps local motor at 2000 RPM, neutral,
  no throttle. Spatial rival motors use the two physical actors and real RPM (excluding the
  original local actor). Both live actors keep wheel-contact tyre/rumble audio; frozen
  invisible racers emit no events. State18 keeps speed input zero for motor/WHIRR audio.
  Authored cameras use the external mix; the orbit keeps the player's previous audio view.
  Exit releases the finale and resumes CD02 frontend.

### Finale acceptance

Finale checks require exact CPU/GPU actor pixels, all eight team materials, buffer reuse
and retained mesh lifetime (`championship-reference-finale-models`), source timer/credit
boundaries, both physical actors, duplicate teams, wheel-only audio voices, CD04/motor
voices, upright mirrored credits. Canonical CRC32: `tourcars.exe` `8ce56d41`,
`data/champfin.bfp` `28f9b97c`.

## Showdown and commentary

- Showdown keeps the season schedule and AIBIN/QUALPC banks. `444270` scales generated
  rival times by 960/100 instead of 10 before splitting the centisecond record; player
  times keep measured values. `4019f0` changes tire tuning to `228f` before
  Mayhem/Impossible overrides and adds `ccc` Q16 to POINTS speeds from point 1 (also used
  by Impossible). `44f0a3` sets horizontal projection mirror +24; `48f5f9` negates
  projectionX and changes culling; `422f60` swaps Left/Right for mismatched mirror flags.
  Native applies the mirror and the matching steering change together.
- `audio/championship.*` ports `44cb90` and `479cfb`: language-specific unsigned 8-bit
  mono RAW at 22050 Hz, volume 127, pan 64. Teams and driver abbreviations from tables
  `4af268`/`4af378`; filenames in the source families:
  - Race finish (`448a20`, case 9): first-win ERF, subsequent-win ERN, or winning rival
    ERE/ERC, incl. the source sub-second finish-gap branch.
  - Result entry (case 10): FPI1..7.
  - Standings (`448f00`, cases 11/12): CTIN1..5 then CTIC/CTIS/CTIB/CTPL/CTPR/CTPB chosen
    from actual scores, actor order and original 8/20/26-point thresholds.
  - Elimination (`449de0`, case 14): GO plus player team.
  - Final (`448f00`, cases 13/14): ES plus player team for the champion; GOC for a
    completed campaign without the title.
- Final Showdown honors connect `FINAL.raw` regardless of medal, at the English-only case 16
  trigger in `449350`. This deliberately restores a silent original caller: case 16 builds
  the filename but falls through to return 0 at `44d21c` instead of entering the RAW
  playback chain at `44d0d5`. English speech fallback for Italian/Spanish does not enable
  this extra for those languages. Focus/pause and scene exit stop owned speech without
  replaying a consumed one-shot. No reference audio in versioned assets. All five frontend
  language IDs accepted; the reference has English/French/German commentary only (Italian
  and Spanish menus use English speech when their folder is absent).
- Release builds substitute 32 kbit/s mono Vorbis at 22050 Hz for these RAW files; the
  loader prefers the `.ogg`; cue keys, language fallback and queue order unchanged.
  Converted files stay local build/package output
  ([encoding contract](music.md#release-speech-and-effects)).
