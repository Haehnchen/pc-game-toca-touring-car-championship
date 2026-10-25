# Original PITS routes and local box-lane guidance

KEEP member 2 decodes during ordinary scene loading. `401790` reads a positive signed 16-bit count, skips the next header word, keeps 64-byte records. `453f20` names their four signed Q16 XYZ vectors **Left, Best, Over, Right**. The final four 32-bit words are preserved without borrowing POINTS speed meanings. The decoder owns every field and checks the exact payload length before allocation.

| Track | Records | Member CRC32 | Automatic guidance |
| --- | ---: | --- | --- |
| Donington GP | 34 | `c8e3231d` | Yes |
| Silverstone | 52 | `9f01ef85` | Yes |
| Thruxton | 21 | `9cde1ed4` | Yes |
| Brands Hatch | 50 | `c4c0d355` | Yes |
| Oulton Park | 40 | `b6a90fc8` | Yes |
| Donington Short | 26 | `8f3102cd` | Yes |
| Croft | 25 | `b80cd63f` | Yes |
| Knockhill | 20 | `891980c7` | Yes |
| Snetterton | 38 | `f17ec791` | Yes |
| Lavaland | 30 | `8385343a` | Disabled by original track9 branch |

CRC32 IEEE/zlib of the unchanged BFP member bytes. All canonical reserved header words are zero. Snetterton's first metadata words are 70/73/70/70 (kept distinct).

## Entry, driving and return

- `403dd0` processes PITS before ordinary POINTS progression. Entry needs a forward side change across gate0 (`402910`, selector2), both endpoint strip checks (`4017b0`) and the strict whole-unit squared-distance limit prepared by `4019f0`; being near the box lane is not enough.
- Human entry calls `402be0`: keeps body, velocity, RPM, gear while switching to original AI controls; resets POINTS next/previous to 0/count-1, keeps lap/checkpoint record, starts PITS at index1. An already AI-owned approach keeps ownership; a valid entry crossing still resets POINTS. Ordinary POINTS/lap updates continue during guidance.
- `403030` aims at PITS[index+1].Best; `AiDriver` steering and wheel/body physics consume the target; POINTS cache and RNG draws continue. `4051e0`: original 100/120 speed governor, minimum `0xccc` throttle, strong-steering brake release. No waypoint teleport, separate integration or stationary service operation.
- The cursor advances at most one authored gate per update. At count-4, vehicle+510 disables `409c30` static-barrier processing (ground support and prop contacts stay). At count-2 barriers return and `402c80` restores the player's transmission and controls. Finished actors keep AI ownership; a finish during guidance still updates the AI finished-coasting regime. Grid reconstruction prepares the driver and resets pit state; focus loss stops simulation without consuming gates or RNG.
- `41ed00`: GSTRINGS3 during the lane, GSTRINGS51 strictly after count-8. Entry flashes 660 ms each second; exit hint stays visible. The scene shows these localized captions and hides manual driving hints while guided. `425970` plays SOUND member75 once, volume60, pan64; wet/dry archives follow the weather. Focus loss, scene exit, grid restart stop the owned cue without replay. The preceding `425950` stops eight `425880` warning loops; those loops have no native caller yet, so unrelated motor/contact/music voices are not stopped.

## Limits

- Knockhill's first Best lies outside its entry strip; physical fixtures approach through the authored Left/Right midpoint instead (no runtime exception). All nine supported circuits drive the full lane with Honda wheel/body/HPC/barrier integration despite conflicting human inputs during takeover; Donington's nearby hairpin divider prevents a direct crossing from the grid (31 active gate indices).
- Full-grid opponent pit decisions, multiplayer, and matched
  original-executable recordings remain outside this local actor package. No
  pit-crew, repair or tire-change operation was found in these PITS callers.
