# Original codes and native Extras

Evidence: `references/decompiled/tourcars.c`; canonical `references/toca-1.1a/tourcars.exe` CRC32 `8ce56d41`. The 22 code pointers at `004b9728` decode with `4759b0`: subtract successive letters of `COPYRIGHT` minus 64, wrap values below `A` by 26. `47739f` compares decoded codes exactly; the native name editor supplies uppercase characters.

## Native Extras menu

- Main Menu → Extras, before Quit. Category page: five original-sized icon rows, left-aligned large text: Unlocks, Camera, Graphics and Effects, Racing, Back. Icons: Effects = Fx icon19, Racing = Start icon3, Back = icon16. Open-padlock and camera symbols reuse the native metal badge (no matching original symbol).
- Controls: localized effect names and Yes/No; Left = No, Right = Yes, Enter toggles, Up/Down rows, Back/Escape to parent. Values save automatically. Switches use ordinary menu sounds; activation speech belongs only to typed original codes. Inactive option change plays `cannot.wav` (sample4) without changing state; locked cars/tracks keep their own progression feedback.
- All 19 menu switches have runtime consumers. Driver-name entry accepts all 21 allowed codes. `CMLOCK` and `CMGARAGE` are typed actions, not switches. `CMDISCO` has no row, placeholder or saved switch (index12 records the original rejected entry); typing it just sets the name, no speech.
- State: `<state-dir>/extras.cfg`, same atomic writer as display preferences; separate original code flags, native overrides, earned fields. Failed write keeps session state, shows a localized error, retries when leaving a category/Extras. No original saves imported or changed.

## Code table

| Index / field | Code | Original consumer and native scope |
| --- | --- | --- |
| 0 / `+48` | `CMMICRO` | `42f280` chooses camera3 instead of 2; `4807f0` also checks camera3 when displaying models. Native camera3: height 40, pitch `03dd`, yaw `fc23`, retained anchor heading. |
| 1 / `+4c` | `CMCHUN` | `4271d0` forces `42bff0(0)`; `42bff0` selects cockpit visibility at camera1. Native hides dashboard/wiper, keeps gloves and wheel. |
| 2 / `+50` | `CMSTARS` | `4512b0`: SILYLAN geometry / sillylan bitmap, horizon5, weather1, short scene range. |
| 3 / `+54` | `CMTOON` | `4512b0`: CARTOON geometry/bitmap, horizon6, weather1. Stars wins over Toon; Lavaland wins over both. |
| 4 / `+58` | `CMMAYHEM` | `4019f0` sets AI tire offset `004c3b88=0`, overriding weather and Impossible. Native qualifying, finish and all 15 rivals consume it via `402b10→412890`; wins over Impossible. |
| 5 / `+5c` | `CMLOGRAV` | `4153c0`, `431820`, `458400`, `462000`, `4628a0` alter particle gravity/parameters. Car setup still writes gravity `-8000` unconditionally in `40a770`. Native: precipitation, detached parts, sign/impact fragments, cone gravity/impulses, all three ballistic surface pools. Smoke, dust, spray, exhaust, sparks have no flag consumer. |
| 6 / `+60` | `CMRAINUP` | `4512b0` forces rainy weather3 unless a special sky wins; `462000/4628a0` reverse particle vertical velocity (`9999`, or `f5c` with low gravity). See [weather](visual-effects.md#atmospheric-weather). |
| 7 / `+64` | `CMCATDOG` | `4512b0` forces weather3; Cats/Dogs sprite variants. See [weather](visual-effects.md#atmospheric-weather). |
| 8 / `+68` | `CMNOHITS` | `412040/45ea50` clear `004adf48`; `421980` skips `428820` car collision dispatch. Native: rival and local split-screen car contacts disabled incl. collision damage and impact/scrape events. Barriers, props, ground contacts stay active. |
| 9 / `+6c` | `CMUPSIDE` | `44f0a3` sets projection flag `+28=1`; `48f5f9` consumes it; `422f60` reverses steering when `+24!=+28`. HUD stays upright. |
| 10 / `+70` | `CMHANDY` | `42c280/42db50/42e440` change cockpit hand/steering transforms, extra fixed scale `1b333`. |
| 11 / `+74` | `CODIES` | `420610` copies entered names into human actors' AIBIN name records; `443f60` (qualifying) and `460170` (race) copy human names unconditionally. No additional visible effect natively. Nonhuman opponents keep their names. |
| 12 / `+78` | `CMDISCO` | Dormant horizon material variant via `431440→496f10→494a40→495ee0`; `477409` always rejects it. See audit below. |
| 13 / `+7c` | `CMFOLLOW` | Camera selection sets view0 and `004c3a58=1`; key `74` toggles. Native C cycling and F5 connect the authored trackside camera and its audio listener. |
| 14 / `+80` | `CMLOCK` | `47734a` clears earned `+38/+3c/+40`, sets track threshold `+44=1`. Native destructive progress reset; other code flags stay set. |
| 15 / `+84` | `GONGOGO` | `475a3f/4761bc` permit bonus cars8/9 in addition to earned flags. Native reversible override, incl. driving. |
| 16 / `+88` | `CMCOPTER` | `46caae` permits only tracks1/3/4/7/8, even with JHAMMO. Native C adds view0 on the automatic helicopter route in sunny/cloudy weather; priority over CMFOLLOW; missing wet-weather actors leave the ordinary cycle usable. No flight keys in the original. |
| 17 / `+8c` | `XBOOSTME` | `412ab0` doubles/caps simulation delta at `30000`, halves its input reciprocal outside special mode. `426f20` suppresses record writes. Native: shared boost arithmetic, Time Trial record exclusion; ghost saving independent. |
| 18 / `+90` | `JHAMMO` | `46ca6f` bypasses earned track threshold. Separate original browsing limit: threshold below9 shows tracks0..8. Native typed code keeps both rules. |
| 19 / `+94` | `CMIMPOSS` | `4019f0` sets AI tire offset `3333` unless Mayhem; adds fixed `ccc` (~5%) to integer POINTS `+34/+38` from point1 (point0 unchanged). Native AI consumes primary/alternate speeds plus tire override. |
| 20 / `+98` | `CMGARAGE` | `477381` writes earned bonus-car flags `+3c/+40=1`. Native permanent unlock action, distinct from GONGOGO. |
| 21 / `+9c` | `PATSCREEM` | `476bfb` always sets earned `+38=1`, calls `469e25(1)`: enables Race Menu slot2 (`4691a9`, TOCA SHOWDOWN `4b0b98`, help THE FINAL SHOWDOWN `4b0c40`). Typed redemption permanently earns the native Showdown season; the menu switch gives reversible access to the same New/Load, qualifying/race, saves, finale route. |

## CMDISCO source audit

- `431440` passes config `+78` to the horizon loader. `496f10→494a40` selects separate texture/material data and skips mesh reuse; `495ee0→499d10` supplies static blue-tinted Direct3D materials: diffuse RGBA `(130,130,255,255)` or `(150,150,255,150)` depending on renderer branch; 8-bit color bypasses the tint. No color-cycling timer or animation in the traced chain.
- 1.1a cannot enable it: `477409` rejects index12 and `477bfc` clears disallowed flags before driving. Intended appearance not verified against the running reference. Not a porting TODO.

## Modes, activation, persistence

- `477409` permits only indices0/1/10/19 in mode1 and 0/1/10/15/18 in mode3; other modes permit all except12. Native: Single Race mode0, Championship/Showdown mode1, Time Trial mode2, Linkup mode3. Name redemption, garage availability and session effects enforce the gates. Stored Extras survive mode changes; disallowed modifiers do not enter that session, its ending or record eligibility. Linkup permits only local camera effects and independent car/track unlocks. Track/car restrictions still apply. Showdown rejects restricted name codes and filters saved physics modifiers, keeping allowed Impossible.
- `476bfb` toggles the selected flag. On enable, `4774da` plays sample5/6/7/8/9 (English/French/German/Italian/Spanish) via frontend sample player `475220`, volume70, pan64, native master gain. Disable: no speech. Accepted name codes clear the driver name and stay in the editor. CMLOCK, CMGARAGE and PATSCREEM execute their writes on every transition; action flags are kept separately to repeat typed-code speech.
- Original persistence: exit `469e6e` → `46ac35` writes the whole `0x654`-byte config to `TOURCARS.CFG`; `46ac56` reloads it, but startup `4688fc` calls `468ccd` clearing all 22 code fields `+48..+9c`; Championship/Showdown entry `4698c5`/`469867` also reset. GONGOGO/JHAMMO do not survive restart. Earned fields `+38..+44` stay: CMGARAGE's unlocks persist, never revoked. Native saved switches deliberately survive restart and save immediately (convenience extension).
- Fresh `46a8bb` state: threshold1 (Donington GP, Silverstone open; other base tracks browsable but locked). Lavaland threshold9 is written by championship award `428410` at round state `+c=19` hex with badge tier3 (displays LAVALAND); championship setup restores it after enough rounds; unrelated to PATSCREEM.
- Native switches: **All circuits** also sets the bonus-track visibility override (Yes includes Lavaland; No removes both overrides, keeps earned progress). **Bonus cars** controls only GONGOGO's override (No keeps earned cars). **TOCA Showdown** controls access via its saved flag (No hides an unearned mode, keeps earned access). No combined unlock, permanent-car action or progress reset in the menu; typed CMLOCK still performs its reset, CMGARAGE its earned-car writes.

## Bonus cars

- Cars8/9 use `40a770`'s original dimensions, pedal limits, drive/brake split and mass (2000/1000 plus120). `44df6e` replaces Ford's actor pair8/9 with the selected bonus car; qualifying and AI keep that mapping and the original driver bank. `41e850` remaps both icons to bonus-car rectangles (`4ad970/4ad988` at640). `414c10→414b00` still selects local actor8's Ford motor sample4/16 (not indexed by bonus selection ID). Medium wheel offsets fall through `44d7f9`'s ordinary `7ae` branch.
