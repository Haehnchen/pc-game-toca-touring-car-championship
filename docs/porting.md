# Porting TOCA 1.1a

Rules: [plan.md](../plan.md). Tasks: [porting plan](porting-plan.md).
Details: [runtime/controls/config](native-runtime.md), [build/test](build-and-tests.md),
[original-game setup](reference-setup.md), [assets](assets.md), [CM3D](cm3d.md),
[graphics backend](graphics-prototype.md).

## Native build and checks

Requires CMake 3.25+, Ninja, a C++23 compiler, Python 3.11+ (standard library) and the
`zstd` CLI (build tool only). GNU Make is an optional wrapper; clang-format 21.1.8 is
needed only for `format-check`. Mega generation also needs ImageMagick with WebP;
garage rendering/composition are manual (Pillow/WebP), builds copy the committed
WebP70 images (also with `TOCA_MEGA_MODELS=OFF`). Linux GPU tests need Vulkan, Xvfb
and `xdpyinfo`; interaction fixtures also Openbox, `xprop`, `xdotool`, ImageMagick
`import`. Locked SDL3 3.4.2, SDL_mixer 3.2.4, GLM 1.0.1 and shadercross revision:
[`dependencies.lock.json`](../cmake/dependencies.lock.json); shader generation needs host
DXC/SPIRV-Cross ([dependency selection](build-and-tests.md#dependency-selection)).

```sh
make build
make run RUN_ARGS='--resolution 1280x720 --fps-limit 60 --vsync on'
make check TESTS='^cm3d-'
make check-sanitizers TESTS='^cm3d-'
make check CTEST_ARGS='-L tooling -N'
make format-check
make check-full        # explicit complete acceptance
make check-network     # local lobby/driving/loading recovery
make release           # dist/toca-Linux-x86_64.zip
```

- `make run` builds/launches `toca-game` once (Release, `build/release`); build/check
  uses Debug in `build/dev`; sanitizers `build/sanitized`; six jobs.
  [Overrides and presets](build-and-tests.md#presets-and-overrides).
- Windows packaging (native MSYS2 UCRT64 or the shared Docker cross-build) and
  the GitHub workflow: [Windows builds](build-and-tests.md#native-windows).
- `make check`/`make test` select CTest cases first and build only their targets.
  `TESTS` is a name regex, `CTEST_ARGS` other selectors; an empty selection fails.

| `TEST_PROFILE` | Selection |
| --- | --- |
| `auto` (default) | Quick without selector; explicit name/label/index/file/rerun selection has no exclusions. |
| `quick` | Excludes `integration` and `extended`; keeps cheap CPU and component GPU/audio cases. |
| `full` | No exclusions (`make check-full`). |

`--rerun-failed`/`--union` need auto/full. Debug/sanitized test presets are quick;
`debug-full`/`sanitized-full` remove exclusions.

Integration changes need one affected native flow/track/weather after component
checks. Complete lap/race, pit and fleet cases are `extended`. Tooling needs its own
checks; documentation needs no game run. One CTest invocation at six jobs, never
parallel CTest in one build tree. Fixtures own state/output under
`build/test-artifacts`, separate Xvfb displays and dummy/offline audio; capture only
game windows; no host desktop/audio; Wine stays serialized.
[Fixtures](build-and-tests.md#isolated-fixtures).

CTest supplies `TOCA_DATA_ROOT` (unpacked Git BFPs) to Python readers; manual asset
authoring uses `TOCA_DATA_ROOT="$PWD/build/dev/generated/original-data"`. Asset
provenance uses package-relative archive names, member identities and CRC32, never
local reference paths.

## Runtime

The build unpacks the 48 BFPs (`assets/data/original/*.bfp.zst`) and five Vorbis packs
(`assets/audio/`) into `assets/original-data/` beside the executable; this bundle wins
over `--data-root` and rejects writable paths. Release packaging puts them in
`game-data/` without Zstd runtime dependency; the build copies the committed music
from `assets/music/`. Defaults: 1280×720 fullscreen (16:9), VSync on,
120 FPS limit, Mega detail, gain 1. CLI overrides saved settings (`--save-settings`
persists); `--config FILE`/`--state-dir DIR` move writable roots.
[Arguments, paths, trace fields](native-runtime.md#arguments-and-persistence).
The ZIP contains no original EXE or Miles DLL.

## Run the reference

```sh
xvfb-run -a -s '-screen 0 640x480x24 -nolisten tcp' ./tools/run-reference.sh --mute
```

`make run-original` opens a visible Wine desktop with sound (manual use). Mutable Wine
data lives outside the reference. Wine/dgVoodoo versions, Ghidra, capture and audio
acquisition: [reference-setup.md](reference-setup.md).

## Analysis

Search `references/decompiled/tourcars.c`/`tourcars.h` first (`rg`). Ghidra project
`references/ghidra/TOCA-1.1a.gpr`, one writer only; for concrete ambiguities use
`./tools/ghidra.sh import|open|decompile` (close the GUI before export).
[Ghidra/debugger](reference-setup.md#ghidra-and-debugger).

## Reference

Only `references/toca-1.1a/` supplies game code/data: patched 1.1a, unchanged
`tourcars.exe` (790016 bytes, CRC32 `8ce56d41`, equal to the official patch payload),
`mss32.dll`, `data/`, `fe/`, `speech/` (2120 files). References and generated analysis
stay out of Git.

```sh
python3 tools/checksum.py --check references/CRC32SUMS --quiet
```

Widescreen ZIP: analysis only. CD ZIP: music provenance only. Approved converted
media in Git: [media boundary](reference-setup.md#media-boundary), [music](music.md).
