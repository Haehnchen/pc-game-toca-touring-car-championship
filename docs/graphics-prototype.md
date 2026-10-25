# Native graphics backend and resource contract

The application uses SDL3 GPU with one HLSL shader set (Vulkan on Linux,
D3D12/Vulkan on Windows, Metal on macOS). The adapter translates recovered TOCA
draw states; it implements neither DirectDraw nor the D3D COM interfaces. Reconsider the
backend only for a demonstrated blocker. The Wine reference is a separate
comparison workflow.

`make build`, `make run`, `make check TESTS='^gpu-'`. Setup, options, profiles:
[porting.md](porting.md#native-build-and-checks). GPU/native checks use muted
private Xvfb, `SDL_VIDEODRIVER=x11`, no `WAYLAND_DISPLAY`; captures stay inside
the game window; artifacts under `build/test-artifacts/`. Automated coverage
uses versioned game data or synthetic fixtures.

## Dependencies and shader generation

- [Dependency lock](../cmake/dependencies.lock.json) pins SDL3, GLM,
  SDL_shadercross, SPIRV-Cross, host DXC archives (CRC32). CMake can build the
  locked shadercross; an existing tool via
  `make build CMAKE_ARGS='-DTOCA_SHADERCROSS=/path/to/shadercross'` or
  `-DTOCA_BUILD_SHADERCROSS=ON`. A packaged tool also needs
  `TOCA_SHADERCROSS_REVISION` matching the lock; cross-compiles and hosts
  without a locked DXC archive need an installed host shadercross
  ([dependency selection](build-and-tests.md#dependency-selection)).
- `legacy.vert`, `world.vert`, `world-reference.vert`, `legacy.frag` → SPIR-V, DXIL, MSL
  under the build's `shaders/`; runtime picks an available SDL backend format;
  compiled shaders ship beside the executable (no shadercross/DXC/DXVK/Wine at
  runtime). The two world modules compile from shared HLSL with an explicit
  reference define. DXIL/MSL generation on Linux establishes translation only.

## Adapter scope and remaining game work

- Screen-space UI/HUD keeps the 32-byte `D3DTLVERTEX` layout. `legacy.vert`
  rebuilds clip coordinates from screen X/Y, depth, reciprocal W incl. the
  integer-to-half-integer pixel-center conversion; `legacy.frag` modulates
  texture × diffuse, applies the cutout threshold and optional vertex fog. SDL
  handles sampler, depth, blend, transfer, synchronization.
- Resident terrain/baked scenery: immutable world-space corners. Resident
  race/finale models: object-space corners, material colors, normals, ghost-
  interior positions. `world.vert` applies node/world and camera transforms,
  table lighting, clipping, culling, TL projection. Lighting changes replace
  the affected chunk without touching resources of queued frames. Hierarchy,
  damage/LOD, visibility, steering, wheel animation, sector order stay CPU.
  Model frame uploads carry matrices, lighting, personal-name rows. Finale keeps
  its translation-first X/Y/Z multiply/add order. Ghost HIGH/MEDIUM/LOW follows
  the published selection or retained LOD owner, with its own texture class.
- Prelit billboards, shadows, skid marks, lamp geometry/reflections, prop/impact
  fragments stream world corners through the same shader; rain/contact lines and
  the opaque SGRID/POINTS queue use an endpoint stream and hardware-line branch.
  CPU keeps fixed-point construction, queue admission, topology, source order. Triangle/line ranges
  rebase independently per split pane; projection IDs, reflections, MSAA
  overlays keep pass boundaries. Near-plane, color, winding, depth contracts:
  [scene rendering](scene-rendering.md); hierarchy/lighting/variants:
  [model consumers](scene-models.md#original-model-consumers).
- Resident projection follows `48741e/47e730/487ee9`: compose WV, store binary32
  coefficients, compose/store WVP, reproduce the original 53-bit x87 sums and
  explicit clip/RHW/TL stores. Integer shader arithmetic provides this precision
  without float64/64-bit integer hardware. Near intersections keep the source's
  distinct one-/two-outside formulas, UV stores, topology. Streamed construction,
  lighting and the finale consumer keep their own operation order; contraction
  disabled where required. GPU division and packed-color normalization keep
  correctly rounded binary32; final TL-to-clip is backend arithmetic. CPU-double
  vs GPU-integer checks require exact pixels and source numeric vectors (no
  raster tolerances). Complete original-scene equality is a separate boundary.

### Resource ownership and reuse

- Scene-local CRC32 + exact-byte mesh caches share identical immutable
  corners/attributes across families and split players. Original textures retain
  `496250` first-name ownership in actor load order, independently for ordinary
  and ghost materials; Mega textures share by exact encoded bytes.
  Queued frames keep their mesh references and dynamic corner data. Resources
  outlive views; textures/meshes destroyed before the renderer. Visibility
  copies share immutable scene/LOD data with independent ordering state.
  `SceneView::frame_into`, finale and live composition reuse frame capacity;
  transforms/visibility recomputed before reused hierarchy slots activate.
- Original surfaces: `481930/482448` presentation surfaces + attached 8-bit
  palette; `4829d8` releases them keeping the DirectDraw device across ordinary
  mode changes (`481f7b` replaces it on adapter change; `482b81` requests
  shutdown). Texture surfaces: `4902d5/49078e`; `484111` releases its temporary
  upload surface unless retention is requested. Palette banks:
  `485ea0/486035/485f76`. Native startup/mode changes preserve these lifetimes
  through the application-owned renderer and views; session data outlives its
  views, warm restart retains them. Scene-local caches include exact image and
  palette bytes. Frames borrow textures (submit while the view lives); SDL
  defers released GPU resources; renderer shutdown waits before destroying the
  device. No COM ownership layer. Device-loss recovery remains outside this coverage.
- Three fenced upload slots (texture, mesh, lighting): a slot waits for its
  previous copy before reuse, grows only when required, maps without cycling.
  Transfer mappings always unmap. Error unwinding closes active passes;
  upload/offscreen commands cancel, commands with an acquired swapchain image
  submit. Two retained world color targets rotate A/B/A; depth clears each world
  frame, while detail0/1 and Lavaland clear color. MSAA seeding, single-sample
  overlays, UI clearing and resize initialization retain separate checked contracts.
  Errors name resource and byte size.
- Renderer-owned vertex, index, instance, dynamic-triangle, dynamic-line,
  staging buffers keep capacity with overflow-safe growth (≤50% spare, minimum
  256-byte allocation); cyclic uploads preserve queued frames; UI background and
  cover scratch frames keep storage. Local 16-bit TL indices with GPU vertex
  offsets, new batch when the range fills (>65536 vertices per frame).
- Per render pass cached: viewport/scissor, pipeline, uniform bytes/length,
  storage-buffer tuple, texture/sampler pair; a new pass (incl. MSAA overlays)
  resets all. Consecutive primitives merge only with identical state/texture
  and contiguous ranges; adjacent identical model ranges can instance;
  different materials, panes, overlay phases stop merging; no material sorting.
- Indexed uploads keep palette RGB and an index-zero key mask; the fragment
  shader discards on it independently of diffuse opacity; nonindexed
  alpha-format conversion uses exact RGB black. Scene samplers inherit CLAMP,
  point/bilinear per pass ([color/sampling contract](graphics-comparison.md#shader-color-and-sampling)).
  Opaque Mega model/scenery WebPs: mipmaps, trilinear, 2× anisotropy; alpha
  images keep cutout sampling.

## Acceptance boundaries

NVIDIA Vulkan GPU/native Time Trial and representative Mesa ASan/UBSan checks pass;
Mesa software Vulkan is not AMD hardware acceptance. SPIR-V/DXIL/MSL compile from
shared source. Separate compile-time world modules and exact normal binary32
decoding resolve the Vulkan pipeline-start blocker without changing PC53 precision.
The explicit `reference_dgvoodoo263` profile retains the measured TL/P8 convention;
the native profile keeps its backend convention.
C05j/C02 is complete; [matched rendering](graphics-comparison.md) records the accepted
scene comparisons.
The host-CRT asin-table limitation remains explicit in the scene contracts.
