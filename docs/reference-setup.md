# Original reference and analysis setup

Canonical identity/checksum: [porting reference](porting.md#reference). Needed only for
original-game observation, debugger or audio acquisition.

## Wine launcher

`make run-original` runs `./tools/run-reference.sh --sound` (visible 640×480 Wine
desktop, with sound). The script alone defaults to muted (`--mute`, `--sound`, `--help`).
Isolated observation:

```sh
xvfb-run -a -s '-screen 0 640x480x24 -nolisten tcp' ./tools/run-reference.sh --mute
```

The executable is copied unchanged, assets link to the canonical tree; settings,
saves and runtime DLLs live in `~/.cache/toca-1.1a/wine`. Intro playback is omitted.
Wine maps the folder to CD-ROM `D:` (no ISO needed, no CD audio; the native Ogg
soundtrack is not played). [Music provenance](music.md).

## Dependencies

| Component | Working setup |
| --- | --- |
| Wine | Classic 32-bit 10.0 (`~/.local/share/lutris/runners/wine/ge-proton/files`, with DXVK/vkd3d); override with `TOCA_WINE_RUNNER`. |
| dgVoodoo | [2.63](https://github.com/dege-diosg/dgVoodoo2/releases/tag/v2.63): `MS/x86/DDraw.dll`, `MS/x86/D3DImm.dll` in `~/.cache/toca-tools/dgVoodoo-2.63/`. |
| Host | Vulkan driver, Bash/`flock`, Xvfb. |

Wine reports Windows 98 (VGA-port emulation); rendering is dgVoodoo → D3D11 → DXVK.
System WoW64 Wine with dgVoodoo 2.87.5 crashed. Dependencies stay outside Git
(`XDG_CACHE_HOME` is respected).

## Silent original audio acquisition

`--audio-null-sink` needs an explicit absolute `PULSE_SERVER=unix:/private/socket`: a
private Pulse/PipeWire server with only a null sink, Wine streams routed there, ALSA/OSS
fallback off. This keeps Miles initialized and voices active without speaker output
(disabled drivers do not). `trace-reference.py --audio-null-sink` records the route.
The WHINE capture used minimal PipeWire plus WirePlumber's policy-only profile. Default
recordings keep drivers disabled; native dummy output is not original PCM evidence.

## Ghidra and debugger

Use the existing export first; only one writer may open the project.

```sh
./tools/ghidra.sh open
./tools/ghidra.sh decompile    # close the GUI before export
```

[Ghidra 12.1.4](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build)
at `~/.cache/toca-tools/ghidra_12.1.4_PUBLIC` (override `GHIDRA_HOME`), Java 21+.
`./tools/ghidra.sh import` analyzes headless and keeps existing analysis. Project
`references/ghidra/TOCA-1.1a.gpr`, export `references/decompiled/tourcars.c`/`.h`
(2018 functions, pseudocode, language `x86:LE:32:default`, compiler `windows`).
Debugger: the runner's `winedbg` with `WINEPREFIX=~/.cache/toca-1.1a/wine`; image base
`0x00400000`, entry `0x004a0b70`. Never patch the canonical executable.

### Original frame capture

`tools/capture-render-reference.py` owns a private muted Xvfb and the reference lock.
Schema 2 captures need the presentation barrier: after the source present returns, the
recorder suspends only the verified Windows game thread and detaches WineDbg so
dgVoodoo/DXVK workers finish; it captures the stable game window and verifies unchanged
body, camera, projection and update-counter bytes, releasing exactly its own suspend
count. Stopping at an attached debugger leaves workers frozen and can capture an older
frame, so schema 1 images cannot prove raster fidelity. The helper is the project's
CRT-free 32-bit `tools/reference-thread-control.c` (Clang Windows target, GNU `ld`
`i386pe`, linked against the runner's `kernel32.dll`; `--thread-helper PATH` selects a
prebuilt one; strict process/thread ownership check before suspend/resume).
Manual observation only, not a build/test dependency. The native comparator consumes
`TOCA_RENDER_12` (actors, both ghost owners and five cockpit roles included) and
rejects captures without the schema 2 barrier. `--session single-race --no-qualifying`,
`--view 1/4`, `--damage`, `--tt-lap --tt-save-best` and `--load-best --tt-wait-ghost`
follow actual menu/keyboard paths; no recorded pose or ghost injection.
[Frame identity and reproduction](graphics-comparison.md).
The bounded history retains up to 64 preceding numerical world/present snapshots,
including actual C-key camera switches. The native fixture replays canonical
geometry through two retained color targets A/B/A; depth clears each world frame,
with color clearing at detail0/1 and Track9. Only the final source image uses the
completed-presentation barrier; no original pixels enter the native history.

### Fleet trace capture

- `trace-reference.py --memory-reader proc` (Linux): after proving the single private
  process and matching executable/actor bytes against WineDbg, reads memory through a
  read-only descriptor at confirmed debugger stops; default reader stays WineDbg.
- `--cold-grid-sequence --warm-restart` with the full timing/local-session profile also
  observes the `447a77` timer creation clock and every `412ab0` counter sample (menu
  frames without physics included); comparison uses one creation sample and ordered
  clock operations. [Clock contract](simulation-time.md#recorded-startup-clock-transactions).
- `--verify-replay PATH` compares every recorded update at prefixes 64, 128, 256, 512 and
  doubling, then at lifecycle completion/limit, using immutable CRC32 prefixes in a
  background verifier; a difference stops at the next closed clock transaction.
  An unfinished extra-update transaction defers a checkpoint. Long paused checks can
  overflow the original signed QPC difference (214.748 s at 10 MHz). A source
  exception leaves an aborted prefix, never a lifecycle PASS.
- `replay-fleet-reference.py --native-timeout SECONDS` (default 120; 600 for the dense
  sanitizer comparison) limits each native subprocess; capture `--timeout` is separate.
- `--stop-after-local-checkpoint` (needs the cold local-session profile and
  `--verify-replay`) ends after a real local split-table write plus three display-counter
  advances at a complete clock transaction. Open: complete finish/restart coverage.

### Scene input journal

The optional journal observes actual `47de00` initialization, `47e3c0` migration,
`47d3d0` selection, `4271d0` model dispatch and the next geometry publication.
It has its own header, strict operation order, CRC32 and complete/aborted footer;
the historical FLET format and its coverage keep their separate meaning.
Scope is one normal single-player NoQual View2, sixteen physical car parents
and the follow anchor. Camera/near/far/root/body inputs are separate from source
PVS, registrations and active model/effect expectations. Pointer provenance and
expected outputs never enter the native input binary.

```sh
python3 tools/trace-reference.py --scope fleet --scene-journal --memory-reader proc \
  --updates 128 --scenario accel --weather sunny --timeout 1800
cmake --build build/dev --target toca-scene-journal-replay
python3 tools/replay-scene-journal.py PATH/scene-journal.jsonl
```

The capture owns a muted private Xvfb and stops once its named scene coverage is
closed, or honestly at the update limit. The comparator loads Git resources and
requires both exact values and source coverage for PASS. The accepted startup
closes after 4 updates/98 operations/3 publications: 12 grid-placement registration
changes, camera-Y settling and 3,411 exact values. It establishes neither moving
PVS/LOD transitions nor autonomous camera/physics/scheduling.
[Owner and evidence](scene-rendering.md#recorded-scene-inputs).

Add `--scene-frames` to capture complete `44a370` primary transactions, nested
physical bodies and the external `412ab0` counter after publication/extra physics.
It implies `--scene-journal` and also writes `scene-frames.jsonl`; both streams
must close completely and satisfy their named coverage. Generate the camera and
refresh cadence with:

```sh
python3 tools/replay-scene-journal.py PATH/scene-journal.jsonl \
  --frames PATH/scene-frames.jsonl
```

This path omits observed camera/projection/scene calls from native inputs and
checks their generated outputs exactly. Physical states and initial roots remain
explicit boundary inputs; [scope](scene-rendering.md#generated-normal-view2-frames).

For a single joined cold/physical/scene capture, use `--scene-autonomous` with the
existing complete cold-local provenance flags and at least64 updates. It keeps
one primary/QPC observer and drains the final callback/sample tail:

```sh
python3 tools/trace-reference.py --scope fleet --scenario local-lap --scene-autonomous \
  --contact-weather-rng --cold-grid-init --cold-grid-sequence --checkpoint-timing \
  --finish-timing --local-handoffs --session-boundaries --updates 64 --memory-reader proc
python3 tools/replay-fleet-reference.py PATH/trace.jsonl --compare-startup-scene \
  --replay build/dev/toca-fleet-reference-replay --data-root build/dev/generated/original-data
```

The scene sidecars bind capture identity, physical IDs and the parent prefix CRC32.
They supply expectations only; no `TOCASFR1` body/root input is written in this
profile. Build `toca-fleet-reference-replay` first. The bounded scene coverage is
separate from the parent trace's full-finish gate. Contact-audio listener/mix
context stays observed. [Contract and limits](scene-rendering.md#joined-physical-and-scene-replay).

## Media boundary

Only patched 1.1a supplies game code/data. `references/ToCA_WidescreenV3.zip` is
read-only analysis material. `references/TOCA-Championship-Racing_Win_EN-FR-DE_CD.zip`
is read-only CD-music provenance (tracks 02–06 only). User-approved media in Git:
converted `assets/music/`, startup WebPs `assets/ui/original/title.webp`/`cpyright.webp`,
original garage WebPs `assets/ui/original/garage/`, the unchanged original 32x32
executable icon `assets/ui/original/icon.ico` ([identity](../assets/ui/original/icon.txt)),
the 48 non-audio BFPs as individual
Zstd19 under `assets/data/original/`, and five converted Ogg/Vorbis packs under
`assets/audio/` (existing release bytes in lossless Zstd19). Original BMP/PAL stay
untracked and unchanged; canonical reference copies stay unchanged. Unpatched
installations were removed; an inspected desktop ZIP held a modified unpatched
executable with reduced speech and was not imported.
