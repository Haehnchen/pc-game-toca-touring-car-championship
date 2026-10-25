# Build and test details

Common commands/profiles: [porting setup](porting.md#native-build-and-checks).
Build contract: [F01](implementation/01-foundation.md).

## Presets and overrides

- Directories: Debug `build/dev`, Release `build/release`, ASan/UBSan
  `build/sanitized`, network `build/network`, package `build/dist`. Six build/test
  jobs by default. CMake schema 6 presets use the same directories; Release has
  tests off. GNU Make only wraps CMake/CTest; the compiler environment must
  already be initialized.
- Test presets `debug`/`sanitized` are quick; `debug-full`/`sanitized-full` remove
  profile exclusions. `-R` still runs inside the preset filters; plain
  `ctest --test-dir build/dev -R ...` has no profile filter.
- Make variables: `CMAKE`, `CTEST`, `PYTHON`, `GENERATOR`, `BUILD_DIR`, `BUILD_TYPE`,
  `CMAKE_ARGS`, `BUILD_JOBS`, `BUILD_TARGETS`, `TEST_JOBS`, `TEST_PROFILE`, `TESTS`
  (CTest name regex), `CTEST_ARGS`, `SANITIZER_BUILD_DIR`, `NETWORK_BUILD_DIR`,
  `RELEASE_BUILD_DIR`, `DIST_BUILD_DIR`, `RELEASE_OUTPUT_DIR`, `PACKAGE_ARGS`.
- `TESTS`/`CTEST_ARGS` (name/label/index/file/rerun) narrow the built targets as well;
  `BUILD_TARGETS` overrides that mapping. An empty selection fails before build;
  `CTEST_ARGS='-N'` only lists. Never run two CTest processes in one build tree.
- Unchanged Ninja configurations refresh metadata only; changed flags/cache,
  missing metadata and other generators do a full configure. `ccache` is used when
  present and no explicit compiler launcher is set (an empty launcher disables it).
- `format-check`: clang-format 21.1.8 in up to six size-balanced batches, no compile.
  Only that target checks the formatter; override with `-DCLANG_FORMAT_EXECUTABLE`.
- Mega scene generation verifies input CRC32 and every manifest-listed GLB/WebP before
  reusing its cache; missing or corrupt outputs regenerate; unchanged output keeps
  its timestamp.

## Native Windows

MSYS2 UCRT64 with GCC, CMake 3.25+, Ninja, Python 3.11+, Zstd and ImageMagick (WebP).
The `distribution` preset serves both hosts (Release, tests/sanitizers off,
`build/dist`, locked SDL/GLM and host shadercross/DXC/SPIRV-Cross):

```sh
cmake --preset distribution
cmake --build --preset distribution
python3 tools/package_release.py --build-dir build/dist
cmake --build --preset distribution --target format-check   # optional
```

The game uses the Windows GUI subsystem, so opening it does not create a console
window. SDL supplies the Windows entry point and command-line handling.
Packaging resolves recursive PE imports with the toolchain's `objdump`, copies
non-system DLLs (SDL3, GCC runtime) beside `toca.exe` and strips only staged copies;
system/graphics-driver DLLs stay external. The
[workflow](../.github/workflows/nightly.yml) builds Linux natively with Ubuntu
24.04/GCC 14 and Windows with MinGW inside an Ubuntu 26.04 Docker image. On the
default branch, both verified x86_64 ZIPs update the `nightly` prerelease.

The Windows ZIP includes `toca-vulkan.bat` and `toca-direct3d12.bat` beside
`toca.exe`. Double-click either launcher to select Vulkan or Direct3D 12 for
that run; command-line arguments are forwarded to the game. Launching `toca.exe`
directly defaults to Vulkan. Use `toca.exe --gpu-driver direct3d12` or
`toca.exe --gpu-driver vulkan` to select a backend explicitly. The CLI option
overrides `SDL_GPU_DRIVER` from the environment, which overrides the Windows
Vulkan default. Other operating systems retain SDL's automatic default selection.

The Windows job and local builds use the same image and script, with full Mega
generation enabled:

```sh
docker build -f tools/ci/windows.Dockerfile -t toca-windows-build tools/ci
docker run --rm --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$PWD,target=/work" --workdir /work \
  toca-windows-build python3 tools/build-windows.py
```

The image contains build tools; source enters through the bind mount. The script
builds locked Linux host shader tools and the meshopt encoder, cross-builds the
Windows game and Mega assets, then verifies `dist/windows/toca-Windows-x86_64.zip`.
The container performs no Wine execution. CI caches only dependency archives and
sources. Native MSYS2 builds above remain supported separately.

Linux cross-builds keep Mega generation enabled: supply a Windows toolchain,
`TOCA_BUILD_SHADERCROSS=OFF`, the locked host `TOCA_SHADERCROSS` and
`TOCA_HOST_MESHOPT_ENCODER` from a native build of `toca-meshopt-encode`.
Windows ZIP packaging uses the cross-toolchain's `objdump`/`strip` and discovers
MinGW runtime DLL directories through its compiler. This checks generation,
compilation and packaging.

## Source boundaries and focused tests

- `src/app/application.cpp` orchestrates; scene lifecycle, ticks, interpolation,
  audio, finale and CSV tracing are separate units under `src/app/`.
  `SceneSession::prepare_frame` composes views, ghosts, reflections, HUD/results and
  pause overlays into retained buffers; the application owns presentation
  scheduling, `renderer.draw`, FPS deadlines and frame tracing.
- `scene/model_pose.*` and `scene/lighting.*` form the GPU-free
  `toca-scene-preparation` target (strict floating-point contraction rules).
  `scene/terrain_view.*` owns ground uploads, cached lighting, UVs and material
  layers; terrain and static scenery share `scene/render_geometry.*`.
  `frontend/canvas.*` owns indexed blits, glyph caching, overlays and canvas uploads.
- `app/driving_input.*` evaluates the nine sampled driving bindings without SDL;
  InputDevices owns device/focus/capture.
- Selectable test suites: `toca-input-device-tests --suite keyboard-pad|local-players|wheel|feedback`;
  vehicle-motion `--list/--suite/--case`; `network-session --suite lobby|protocol|loading|link`;
  `mega-geometry`/`reference-fleet-reader`/`reference-trace-reader`/`mega-vegetation`
  Python runners accept repeated `--suite NAME`, `Class.test_method` and unittest
  discovery (`tests/tools/`, `tests/integration/reference_trace_tests/`).
  Frontend menu family: `make check TESTS='^frontend-menu($|-settings$|-bindings$|-modes$|-fonts$)'`.
- Fleet replay parsing/clock/state/comparison code lives in
  `tests/simulation/fleet_reference/`; the CLI is `replay_fleet_reference.cpp`;
  synthetic fixtures need no recordings or game launch.
- Standalone native selectors run through `tools/run-isolated.py`, as CTest does.

## Dependency selection

Archives are CRC32-checked and extracted once into the shared `build/dependencies`
(`-DTOCA_DEPENDENCY_CACHE=/path` moves it). Installed SDL3/GLM via `SDL3_DIR`, `glm_DIR`,
`CMAKE_PREFIX_PATH`; disable system lookup with `TOCA_USE_SYSTEM_SDL3=OFF`/
`TOCA_USE_SYSTEM_GLM=OFF`. `TOCA_SHADERCROSS` selects the host shader tool (a
packaged tool needs a matching `TOCA_SHADERCROSS_REVISION`); `TOCA_BUILD_SHADERCROSS=ON`
builds the locked tool; unsupported hosts and cross-compiles need an installed one.
Configure verifies shader translation.

```sh
make build CMAKE_ARGS='-DTOCA_SHADERCROSS=/path/shadercross'
```

## Local release package

```sh
make release
make release RELEASE_OUTPUT_DIR=/path/to/output
make release DIST_BUILD_DIR=build/package
```

- Builds only `toca-game` in Release (no tests/sanitizers/LTO/CPU-specific flags) in
  `build/dist`; output `dist/toca-<OS>-<arch>.zip`. Linux needs `strip` and `ldd`,
  Windows MinGW `strip`/`objdump`. Linked non-glibc libraries must resolve inside
  the package; the host supplies glibc and Vulkan (a newer build host gives no
  older-glibc compatibility). Documented packages cover Linux and Windows.
- Contents: stripped executable, non-system libraries, compiled shaders, runtime
  UI/models/music, `game-data/` (48 non-audio BFPs and 2,045 Vorbis files copied
  unpacked), the unchanged `README.md`. Audio is accepted only from `data/`,
  `fe/sounds/` and `speech/`. No reference directory, EXE, Miles DLLs, checksum
  files, `THIRD-PARTY-NOTICES.txt`, reference tools, symbols or authoring reports.
- Audio: speech Vorbis 32 kbit/s mono, frontend cues and the two full sound banks
  64 kbit/s with original rate/channels, soundtrack 96 kbit/s stereo, copied
  unchanged ([encoding](music.md#release-speech-and-effects)). Omitted: WAV/RAW,
  low-memory sound banks, loose `fe/images/*.bmp|*.pal`, `data/image320.bfp`,
  `loadlow.bfp`, `loadmed.bfp`, speech `filelen.dat`, garages GAR1–4. Required
  indexed BFP/CM3D images stay (indices, font tails, shadow alpha are runtime inputs).
- UI images are committed WebP70/method 6 under `assets/ui/original/` and
  `assets/ui/mega/` (4 startup images, 19 base + 19 upgraded garage views, ten Mega
  loading images with font/margin art); build/package copy them; the import scripts
  are manual. The loader takes Original and applies a same-name Mega file when enabled.
- Run: extract, start `./toca`. Resources resolve beside the executable; saves go to
  user storage; `--data-root` selects external data but the bundled archives win.
- Menu translations, cars/tracks, records, HUD rectangles, qualifying parameters and
  audio curves are named native C++ data; neither build nor runtime needs the EXE.

<a id="remaining-original-data-dependencies"></a>

## Versioned runtime data

All 48 required non-audio BFPs are stored one by one as single-thread Zstd19
(62,620,798 → 11,754,788 bytes, round-trip identical to canonical 1.1a):

| Input | Archives | Content |
| --- | ---: | --- |
| Track `*keep.bfp` | 10 | HPC collision, routes, pits, grids. |
| Track `*free.bfp` | 10 | Terrain, cameras, scenery/detail families, prop placements. |
| Track `*hor.bfp` | 10 | Moving actors, weather/special skies/horizons. |
| `allcars.bfp` | 1 | Eleven HIGH/MEDIUM/LOW car families, name font. |
| Nine `*dasx.bfp` | 9 | Hands, dashboards, wipers, steering wheels. |
| `champfin.bfp` | 1 | Finale vehicles, eight team figures, credits. |
| UI/config/HUD/effect BFPs | 7 | Fonts, menus, loading/HUD/effect images, shadows, rotating previews. |

`toca-original-data` unpacks `assets/data/original/*.bfp.zst` to
`generated/original-data/data/` (suffix removed); the resource target copies them to
`<executable>/assets/original-data/data/`. Generators and parser tests receive this
root, keep package-relative archive names and verify archive/member CRC32. Mega loads
the original world/models before applying GLB/WebP overrides, so all inputs stay needed.

Five audio packs under `assets/audio/` (deterministic USTAR + Zstd19) hold all 2,045
release files unchanged (50.7 MB packed): English speech 678, French 677, German 677,
full dry/wet sound banks 2 (84 members each, Ogg payloads at the original
indices/names), frontend cues 11. `toca-audio-resources` unpacks them with their TAR
member paths. Build dependencies and stamps skip unchanged inputs; the soundtrack
is copied from the committed Ogg files under `assets/music/`. Manual unpack:

```sh
python3 tools/unpack-game-data.py --source-dir assets/data/original --output-dir build/original-data
python3 tools/unpack-audio-assets.py --source-dir assets/audio --output-dir build/audio
```

Runtime AssetStore prefers the bundle, keeping case-insensitive path, symlink and
writable-path protection. Capturing real Miles output stays a manual research workflow.
Boundaries: [media](reference-setup.md#media-boundary).

## Isolated fixtures

- Labels: `unit`, `parser`, `gpu`, `audio`, `bundled-data`, `integration`, `extended`.
  Every test uses committed data or synthetic fixtures; missing Git data is an error,
  never a skip. CTest supplies unpacked BFP/audio inputs and builds their producers.
- Each fixture owns state/config/output/logs below `build/test-artifacts`, its own
  Xvfb display, SDL X11/dummy or offline audio, and captures only the game window.
  Real audio event/mix paths are never disabled to silence tests. Interaction
  fixtures also use Openbox, `xprop`, `xdotool`; captures use ImageMagick `import`.
  Software Vulkan: `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`.
- Display fixtures reserve two CTest slots (up to three GPU cases in six slots);
  explicit reservations win. `LP_NUM_THREADS` defaults to 2 in the isolated runtime so
  parallel llvmpipe instances do not each spawn a CPU-sized pool.
- Fixtures share an incremental CSV reader (partial rows, per-trace restart); PNG24
  captures use unfiltered scanlines, the reader handles all five filters.
- Manual original (Wine) work stays serialized.

### GPU sanitizer leaks

LeakSanitizer reports from real GPU drivers are driver lifetime artifacts, not game
frees: with NVIDIA 610.57.04 a bare SDL3.4.2 GPU device create/claim/release/destroy
leaks ~10 kB, each window resize ~8 kB, each swapchain creation 5,584 bytes inside
`libnvidia-glcore`; a bare `vkCreateInstance`/`vkDestroyInstance` leaks 2,178 bytes
via D-Bus. Lavapipe with `detect_leaks=1` is clean. `SDL_SHUTDOWN_DBUS_ON_QUIT=1` does
not help and is debugging-only; keep it out of the application. For leak acceptance
select Lavapipe explicitly; hardware rendering acceptance is separate:

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json make check-sanitizers TESTS='^scene-reference-loading-view$'
```

## Network selection

`make check-network` (`build/network`, source graphics) selects components, lobby,
two-game driving at 100 ms RTT/2% loss (both cars, remote movement/audio, pause/focus,
End Race cleanup) and loading recovery on separate muted displays.
`native-network-capacity` starts eight instances (Ready, movement/contact/audio,
ninth-player rejection, client/host loss, fresh lobby). `native-network-soak` (ten
minutes, two races) and `native-network-race` are `extended`. Local runs do not
establish another machine's firewall or platform build.
[Network checks](implementation/04-multiplayer.md).
