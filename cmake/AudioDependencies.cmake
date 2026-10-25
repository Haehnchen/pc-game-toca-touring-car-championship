include_guard(GLOBAL)

function(toca_setup_audio_dependency)
  toca_dependency_source(sdl_mixer mixer_source)
  # Canonical WAV/raw samples and bundled Ogg/Vorbis audio; stb_vorbis is included.
  set(BUILD_SHARED_LIBS OFF)
  foreach(feature IN ITEMS INSTALL TESTS EXAMPLES VENDORED WERROR
      AIFF VOC AU FLAC GME MOD MP3 MIDI OPUS VORBIS_VORBISFILE VORBIS_TREMOR WAVPACK)
    set(SDLMIXER_${feature} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
  set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
  add_subdirectory("${mixer_source}" "${CMAKE_BINARY_DIR}/dependencies/sdl_mixer" EXCLUDE_FROM_ALL)
endfunction()
