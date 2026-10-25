# Original CD music

`references/TOCA-Championship-Racing_Win_EN-FR-DE_CD.zip` holds one BIN/CUE image: track 01 is game data, tracks 02–06 are CD audio. The ZIP is read-only music provenance; its game-data track is not extracted or used as another game reference. `references/toca-1.1a/` stays unchanged.

## Assets and encoding

- By explicit owner request, `assets/music/track02.ogg` … `track06.ogg` are versioned runtime assets: **Ogg/Vorbis, 96 kbit/s nominal, stereo, 44.1 kHz**. SDL_mixer 3.2.4 includes [`stb_vorbis`](https://github.com/libsdl-org/SDL_mixer/blob/release-3.2.4/src/decoder_stb_vorbis.c) (no external codec; configured build enables `wave` and `vorbis_stb`). Opus files under `references/music/` are local working copies. Runtime files were encoded with libvorbis directly from stereo 16-bit LE CD PCM at the CUE INDEX 01 sector boundaries (no transcoding loss). Builds copy these committed files; playback uses the five Ogg filenames directly. Do not renumber, trim or normalize.

| CD track | Runtime file | Duration |
| --- | --- | ---: |
| 02 | `track02.ogg` | 194.720 s |
| 03 | `track03.ogg` | 318.947 s |
| 04 | `track04.ogg` | 300.973 s |
| 05 | `track05.ogg` | 337.613 s |
| 06 | `track06.ogg` | 314.387 s |

## Release speech and effects

- Five user-approved packs under `assets/audio/` hold the release Ogg/Vorbis bytes: English/French/German speech, frontend cues, dry/wet sound banks. Speech **Ogg/Vorbis 32 kbit/s, mono, 22050 Hz**; frontend cues and the full `sounddry.bfp` / `soundwet.bfp` banks **64 kbit/s** with original rate/channels; CD soundtrack unchanged at 96 kbit/s stereo. No normalization, trimming or sample-rate conversion.
- Deterministic USTAR+Zstd19 packing: **59,663,767 → 50,739,177 bytes** (14.96%), no further lossy step. `tools/unpack-audio-assets.py` unpacks `*.tar.zst` by TAR member paths; build dependencies trigger extraction on input change; unchanged builds reuse `build/<config>/generated/audio/`. The audio target copies files beside the executable under `assets/original-data/`; release packaging copies it to `game-data/`.
- Loose cues/speech are `.ogg`; the two BFP banks hold Vorbis payloads and keep all 84 member indices/names per bank.
- Native loading accepts the canonical source files and the converted layout. Engine/wind pitch uses the decoded original sample rate. Speech selection, language fallback and sequential playback keep their rules.

## Integration with original callers

- C01 frontend: `0x004688fc` opens CD audio and starts track **02**, music volume from config `+0x30c` (frontend initialization; not evidence for a sound in the isolated `0x004684ac` title routine).
- G01/G06 race entry: `0x00426ec0` checks music enable at `+0x31c`, selects a CD track through tables `0x004ae798`/`0x004ae7a8`, calls `0x0047a258`. Single Race uses the second table: track IDs 0–9 → `[3,4,5,6,3,4,5,6,3,4]` (all ten native tracks). Qualifying/race state entries call it after loading. CD02 stops at frontend teardown; reopening CD audio during loading does not start playback ([scene lifecycle evidence](scene-lifecycle.md)). Other mode-table semantics and
  their further runtime consumers remain open. All native tracks stop CD02 during loading, start finite CD03/CD04/CD05/CD06 on entry, restart CD02 after returning to Main. `0044a1d0` calls `0047a3b4(0)`, so race music does not inherit the frontend restart policy. Frontend sample voices and decoded samples are released at handoff and reloaded on return.
- `0x0047a258` gets start/end times and starts playback; `0x0047a305` stops; `0x0047a391` sets music volume; `0x0047a3b4` restarts the current track only after elapsed duration plus 2000 ms, status3 and the caller's restart flag (a permanently looping mixer track would change this).
- Music uses a separate track, leaving all 12 sample voices free; compressed audio loaded with deferred decoding via SDL_mixer. Bundled music resolves from the application resource root, separately from `--data-root`; release packaging includes the same files; each owning frontend/race feature bundles its music.
- Native frontend starts track02 after copyright/resource loading, before language selection; playback is finite; the active frontend polls restart by elapsed time at the recovered 61/30-second interval. Global volume/mute via the settings service; shutdown stops music. Music-specific settings UI
  and race music integration remain open. See [frontend audio](frontend-options.md#first-frontend-audio-path). Original Miles
  amplitude/pan/timing fidelity still requires the matching silent reference capture.
