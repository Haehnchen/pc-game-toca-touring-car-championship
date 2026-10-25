# Foundation and recovery workflow

C++23 native game on SDL3 GPU. Linux and Windows have equal design priority;
macOS uses the same SDL backends. `make run` is the common entry point (F02/W01).
Shared execution/audio rules: [roadmap](../porting-plan.md#delegation-and-acceptance-rules).

## Evidence base

- Only reference: unchanged `references/toca-1.1a/`; generated analysis and
  extracted assets are Git-ignored. See [setup](../porting.md).
- `src/formats/cm3d.cpp`, `src/render/` and two HLSL shaders: native feasibility
  work; limits in [results](../graphics-prototype.md).
- `tools/ghidra.sh` exports the 2018-function analysis. The export is
  pseudocode, not buildable source.

## F01 — Build, formatting and focused checks

### Build and command contract

Requires CMake 3.25+, Ninja, a C++23 compiler, Python 3.11+ (standard library
only). GNU Make is an optional wrapper; CMake/CTest presets work directly,
including native Windows with an initialized compiler environment. C++
extensions off; `compile_commands.json` exported. Interface target
`toca-project-options` applies only to project libraries/executables/tests;
third-party sources keep their own warning policy.

| Setting | Behavior |
| --- | --- |
| GCC/Clang warnings | `-Wall -Wextra -Wpedantic -Werror`. |
| MSVC | `/W4 /WX /permissive- /Zc:__cplusplus`. |
| Sanitizers | `TOCA_SANITIZERS=ON`: `-fsanitize=address,undefined -fno-omit-frame-pointer` (compile), `-fsanitize=address,undefined` (link), GCC/Clang only; other compilers fail clearly. |
| Numeric code | No fast-math; recovered arithmetic is kept separate from presentation math; contraction/precision evaluated under G02. |
| Presets | CMake schema 6, Ninja: `debug` → `build/dev` (Debug, tests on); `release` → `build/release` (Release, tests off); `sanitized` → `build/sanitized` (Debug, tests, sanitizers); `distribution` → `build/dist` (Release, tests off, locked SDL/GLM, host shader tooling). |
| Parallel defaults | 6 build jobs, 6 test jobs in Make, check tool and presets; overridable. |
| Compiler cache | Ninja/Makefile generators use installed `ccache` automatically; explicit launcher settings preserved; plain compile when absent. |
| Make defaults | `build`; `CMAKE=cmake`, `CTEST=ctest`, `BUILD_DIR=build/dev`, `BUILD_TYPE=Debug`, `GENERATOR=Ninja`, `PYTHON=python3`. |
| Make overrides | `CMAKE_ARGS`, `BUILD_JOBS`, `BUILD_TARGETS`, `RUN_ARGS` (application arguments), `TEST_JOBS`, `TEST_PROFILE=auto`, `TESTS` (CTest name regex), `CTEST_ARGS`, `SANITIZER_BUILD_DIR=build/sanitized`, `RELEASE_BUILD_DIR=build/release`. |
| `make build` | Incremental configure+build (`--config`, `--parallel`, optional `--target`); reuses the build tree. |
| `make run` | Release in `build/release` unless `BUILD_DIR`/`BUILD_TYPE` given. Invokes CMake `run` target once (depends on the game target regardless of test filters); launches `$<TARGET_FILE:...>` with `RUN_ARGS`. |
| `make test` / `make check` | Quick profile (excludes `integration`/`extended`) unless an explicit CTest selector is given. `TEST_PROFILE=quick` keeps exclusions with selectors; `full` removes them. Build targets are derived from CTest's selection (`tools/check.py`, generated `test-targets.json`), built once, then `--output-on-failure --no-tests=error --parallel`. `BUILD_TARGETS` overrides; `-N` only lists. A disabled `BUILD_TESTING` cache is rejected. |
| `make check-full` | `TEST_PROFILE=full`; user selectors still honored. CTest presets `debug`/`sanitized` are quick; `debug-full`/`sanitized-full` have no profile exclusions. |
| `make check-sanitizers` | Same checks in `SANITIZER_BUILD_DIR` with `TOCA_SANITIZERS=ON`. |
| `make format-check` | Configures and runs only the formatter target. |
| `make release` | Optimized game only in `DIST_BUILD_DIR=build/dist` (tests/sanitizers off, locked SDL source), then stripped Linux ZIP under `RELEASE_OUTPUT_DIR=dist`; `PACKAGE_ARGS` passes packaging options. [Package contract](../build-and-tests.md#local-release-package). |

Code rules: RAII, small functions; gameplay is SDL-free with explicit time,
input and random state; module targets only at real ownership/dependency
boundaries. Check IPO support before enabling LTO; re-verify recovered numeric
behavior when changing optimization. Build-time shadercross/DXC are not runtime DLLs.

### Formatting and lint contract

clang-format **21.1.8** is required only for `format-check` (`CLANG_FORMAT_EXECUTABLE`);
configuration and builds work without it. Checks `src/` and `tests/` `.cpp`/`.hpp`
with `--dry-run --Werror`; a missing/different formatter fails the target with a
hint. Files are distributed over up to six native build targets balanced by size;
no result cache. HLSL is checked by shader compilation. Gates: compiler
warnings, formatting; Python tools use the standard library plus unit checks.

`.clang-format`:

| Options | Values |
| --- | --- |
| Base/indentation | `BasedOnStyle: LLVM`; `IndentWidth: 4`; `ContinuationIndentWidth: 4`; `ColumnLimit: 180`; `NamespaceIndentation: None`; `AccessModifierOffset: -4`; `IndentCaseLabels: true`. |
| Alignment | `PointerAlignment: Left`; `ReferenceAlignment: Left`; `AlignAfterOpenBracket: DontAlign`; `AlignTrailingComments.Kind: Never`. |
| Braces/operators | `BreakBeforeBraces: Attach`; `BreakBeforeBinaryOperators: NonAssignment`; `InsertBraces: true`. |
| Short forms | `AllowShortFunctionsOnASingleLine: Empty`; `AllowShortLambdasOnASingleLine: Empty`; `AllowShortBlocksOnASingleLine: Never`; `AllowShortIfStatementsOnASingleLine: Never`; `AllowShortEnumsOnASingleLine: false`; `AllowShortLoopsOnASingleLine: false`; `AllowShortCaseLabelsOnASingleLine: false`. |
| Spacing/comments | `SeparateDefinitionBlocks: Always`; `MaxEmptyLinesToKeep: 1`; `EmptyLineBeforeAccessModifier: Always`; `ReflowComments: Never`. |
| Includes | `IncludeBlocks: Regroup`; quoted project includes priority 1, `<SDL3/...>` 2, other angle-bracket 3; extend the SDL group only for a library actually adopted. |

### Dependencies and shared cache

`cmake/dependencies.lock.json`: schema version, name, version/revision, URL,
archive name/root, CRC32, license per source/tool archive. Seeded versions:
SDL3 3.4.2, GLM 1.0.1, shadercross `1ff05bec573988a98ef9e0260b4da44f512b8367`
(DXC/SPIRV-Cross inputs and host-tool requirements in
[the build report](../graphics-prototype.md)).

`cmake/Dependencies.cmake`: source downloads shared under `build/dependencies/`
across presets (`TOCA_DEPENDENCY_CACHE` overrides); cache lock with 120 s
timeout; CRC32 verified through `tools/checksum.py`; TLS verification required;
downloads published only after checks; extraction into name/CRC32-specific
directories with a completion marker; compiled outputs stay per build directory.
Unused dependency tests/examples/install targets disabled; their warnings stay
outside `toca-project-options`. Installed-library/tool overrides (including
`TOCA_SHADERCROSS`) are version-validated at configure time. Runtime asset paths
are separate from dependency caching.

### Focused tests and private runtime

- One CTest helper takes target/name/source, libraries, labels, arguments,
  environment, timeout; rejects unknown/missing arguments. Tests mirror source
  modules under `tests/`; cross-module checks in `tests/integration/`. Python
  checks use the configured interpreter. Labels: unit, parser, GPU, audio,
  bundled-data. All checks use committed inputs or synthetic fixtures (no local
  original installation, no missing-reference skips). Inputs are read-only;
  saves/caches/logs use private writable directories.
- Long native lap/race/pit and multi-car fleet acceptance is `extended`;
  inexpensive single-car CPU lap checks are quick. Full native track/weather
  matrices are for broad changes or milestones. Never run CTest concurrently in
  one tree.
- ASan/UBSan for parser or cross-module changes with `ASAN_OPTIONS=detect_leaks=1`,
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; no global suppressions.
  Timeouts: GPU checks 30 s, short native flows a declared deadline, corpus
  checks a measured allowance; CTest timeout above the owned-process cleanup budget.
- Linux runtime helper: unique ignored case directory under
  `build/test-artifacts/` with private XDG data/config/cache/state/runtime,
  temporary/working directories, logs, captures. Xvfb and applications run as
  owned process groups: readiness required, startup/tool/app deadlines, TERM then
  bounded KILL. Never attach to an occupied display or clean up foreign X11
  endpoints. 24-bit Xvfb screen sized for the case, `-nolisten tcp`; desktop/
  session/audio environment variables isolated. Artifacts retained after exit/failure.
- Native checks use SDL's X11 video path and dummy/offline audio that still
  exercises G08's event/mix service. SDL GPU needs a Vulkan device or the
  software Vulkan driver (select explicitly). Tools per case: Xvfb/xdpyinfo
  (readiness), xdotool (input/window lookup), Openbox/xprop (window-manager cases only).
- Opt-in `--step-input` diagnostic clock advances one unchanged simulation
  deadline per input command; normal motion/audio/HUD updates and presentation
  sampling continue; real-time pacing checks are separate. See
  [the protocol](../full-lap.md#faster-diagnostic-scheduling).
- External captures: shared `capture_window`, ImageMagick
  `import -window <game-window-id>` to PNG24 with unfiltered scanlines, 5 s
  timeout, command failure status propagated; `convert` for conversion. GPU
  readback is used for native pixel checks (no ImageMagick). Original Wine runs
  keep their serialization lock.
- `native_support.py` holds shared native helpers and one incremental CSV reader
  per trace (partial writes, UTF-8, scene re-entry, truncation/replacement,
  snapshots, malformed complete records). PNG reader selects reconstruction once
  per row (filters 0–4, RGB/RGBA).
- Formatter: six native CMake batches over all source files.

Dependency/cache and sanitizer limits: SDL source configuration was verified;
application builds used installed SDL3 3.4.2. Windows/macOS execution is not claimed.

## F02 — First common native application

Ships with [W01](05-display.md) and G08's early audio service.

- `src/app/` owns initialization, event loop, active screen, shutdown, startup
  errors; recovered logic stays outside SDL services.
- Renderer and title loading live in the application; startup shows the original title.
- One read-only game-data root; separate writable preferences/save roots via
  SDL/standard C++; explicit paths passed to loaders and checks.
- W01 dimensions, resize, input mapping: 4:3, 16:9, 16:10, FPS limits, VSync;
  640×480 is the UI baseline, independent of simulation and audio pacing.
- Display/audio preferences via config and CLI (`RUN_ARGS`); precedence CLI >
  saved > defaults; effective values and errors logged; only deliberate
  preference changes persist (not diagnostic overrides).
- G08 audio service initialized in the application (SDL_mixer); original startup
  events where observed; silent test sink for tests.
- `make wip` and the viewer entry point are retired.

Behavior verified: exact original 4:3 title pixels at 640×480, centered wide
artwork at 1280×720/1280×800, config save/restore, transient overrides, keyboard
edges, focus, live resize, minimize/restore, pointer mapping, shutdown.

Not claimed: original simulation, Miles PCM fidelity, physical high-DPI,
other-OS execution. [Contracts](../runtime-contract.md), [evidence](../recovery.csv).

## F03 — Recovery ledger and selective source lookup

Decision: existing function header plus `rg`; no index service, MCP, database
or full decompiled dumps in Git.

- Tracked CSV ledger: address, name, subsystem, evidence, confidence,
  implementation, validation (callers/data ownership when useful).
- Workflow: `rg` over `tourcars.h`/`tourcars.c`, read the bounded function and
  callers, follow constants/imports/asset identities. Inferred meanings stay
  visibly unconfirmed; each recovered behavior exposed by `make run` links to a
  bounded source finding and a comparison.
- [ ] Add replayable names/types/structure annotations to the existing Ghidra
  scripts; use Ghidra or assembly for a specific ambiguity, and the game for observation.
- [ ] Measure whether repeated lookup costs justify a local generated function
  address/name/line index under ignored `references/`; add it only if `rg` is insufficient.

## F04 — Native archive and asset loading

- BFP byte-span reading: open/read `0x00486080`, `0x004862a2`, skip
  `0x00486338`, close `0x004863a7`; archive/member index identity preserved.
- Caller-visible sequential reads, sizes, ownership matched without the original
  global scratch allocator; layouts decoded explicitly little-endian.
- BMP/palette decoding and `parse_cm3d` run on archive members; original
  case-insensitive names resolved consistently on case-sensitive filesystems.
- Extractor/CSV kept for provenance; runtime loads canonical data directly
  (ignored generated caches, no symlinks).
- Comparisons against the approved 48 Git BFPs: manifest identities verified,
  members reconstructed without a local catalog. Catalog: 1047 members in 59
  archives; 33 car models / 696 meshes.
- Car loader belongs to scene setup, not title initialization; menu-to-Honda
  route: [integrated scene](../scene-lifecycle.md#integrated-initial-scene-acceptance).

## F05 — Recovered startup and frame contracts

### F05a — Early input, time and event contract

Published: `src/core/contracts.hpp` and [ownership rules](../runtime-contract.md).

- SDL-free `InputSnapshot`, `ClockStep`, `GameEvent`, settings data: explicit
  widths, held/edge ownership, timestamp/delta units, event ordering,
  preference ownership. Unproven field meanings are named as such.
- Host event polling is separate from consuming game actions; menu, live
  simulation and replay share input/time entry points. No simulation frequency
  imposed (G01/G03 supply recovered scheduling/action semantics).
- Small value types and calls; no generic event bus or scheduler framework.

### F05b — Startup and state integration

Depends on F02, F04, F05a; joint checks with C01 (frontend), G01 (session
timing), G03 (actions).

- [ ] Trace CRT entry `0x004a0b70` into app startup `0x0049a240`, active dispatch
  `0x00463410` and shutdown; distinguish runtime helpers from game state transitions.
- [ ] Recover initialization/state ownership and focus/pause transitions before
  translating them; replace platform effects through narrow SDL adapters.
- [ ] Bind the F05a data boundary to recovered startup/frame state. G01/G02
  own update order, numeric conversions and RNG; G03 owns action semantics.
- [ ] Inspect menu pacing `0x00469f10`/`0x00469f54`/`0x00469f9b` separately from
  simulation timing; do not infer a simulation frequency from the menu's 60 divisor.

Output: startup/state diagram and recovered lifecycle integration.
Acceptance with C01/G01: screen transitions and focus/quit behavior follow the
original through the shared input/time boundary.

## F06 — Isolated iteration and reference comparisons

### F06a — Native checks and reference audio

- Private runtime (unique Xvfb displays, private storage, bounded execution,
  cleanup); game-window capture only; dummy/offline audio sinks exercising
  event/mix paths.
- [ ] Before G08 audio-fidelity acceptance, add an explicit opt-in reference
  capture mode: a private Wine audio driver connected to a private null/file
  sink, with no audible host output. Keep the ordinary muted launcher unchanged,
  the canonical executable/CRC32 and the existing Wine lock. Its disabled audio
  drivers currently let `0x0047939c` return before Miles initialization when
  `waveOutGetNumDevs()` is zero; verify actual Miles initialization/sample output
  in capture mode and retain the short PCM baseline locally. Native SDL dummy
  output alone cannot supply the original baseline.
- [ ] Preserve GPU state/readback checks and source CRC32 verification; compare
  original/native frames only with matching content, camera, state and dimensions.

Evidence bundles (commands, source CRC32, logs, window captures) stay outside Git.

### F06b — Original input/time/state trace probe

Short trace proven before G04's long comparisons; car-field coverage grows with G04/G05.

- `tools/trace-reference.py`: drives the runner's `winedbg`, launches/attaches
  only the owned reference process, bounded scripted breakpoints/memory reads,
  existing Wine lock. Scripts in Git; generated debugger commands and captures
  in the private artifact directory.
- Records: executable CRC32, scenario/config/initial state, input snapshots,
  actual Q16 deltas, inverse delta/extra-step flag, RNG, decoded state fields at
  named pre/post-update boundaries. Actions captured where the game consumes
  them (desktop keypress timestamps do not establish game input).
- Captured deltas are replayed into the native entry point. Debugger pauses
  affect QPC: such traces are not an unperturbed wall-clock baseline. Controlled
  reference deltas would need a post-calculation, pre-consumer injection point
  with all coupled timing fields updated in memory; entry `0x00412ab0` alone is
  insufficient because its body computes those fields. Interventions logged
  separately; the canonical executable is never patched on disk.
- Machine-readable trace with update IDs and known field widths; missed
  boundaries or missing values fail. Short input/delta traces replay through G02.
- Replays import each consumed snapshot and diagnose individual updates; full
  sequential state and lap parity remain unaccepted. A failed trace probe blocks
  state-parity claims, not asset/frontend work. Commands, boundaries, integrity
  checks: [original recordings](../reference-replay.md).
