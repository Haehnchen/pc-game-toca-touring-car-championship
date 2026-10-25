- Read `docs/porting-plan.md` for task order and `docs/porting.md` for build/test setup.
- Use CRC32 (IEEE/zlib, eight hexadecimal digits); see `tools/checksum.py`.
- Replace track textures 1:1; preserve UVs, slots, alpha, motifs and repeat phase.
- Batch related track props/materials with shared provenance and representative checks.
  Add reports or fixtures only for concrete ambiguities or integration defects.
- Do not benchmark track assets or measure frame times.
- Run GUI tests muted on a separate Xvfb display with `SDL_VIDEODRIVER=x11` and
  `WAYLAND_DISPLAY` unset. Capture only the game window; no desktop windows or remote desktop.
- Use `make check TESTS='...'` for affected checks. `make check` is quick;
  `make check-full` runs full acceptance. No routine full track/weather matrices.
- Run one CTest invocation per build tree, at most six jobs.
