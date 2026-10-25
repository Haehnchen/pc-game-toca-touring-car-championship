include_guard(GLOBAL)

# These packs contain the already shipped Ogg/Vorbis bytes, without re-encoding.
set(package_audio_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/audio")
set(audio_source_directory "${PROJECT_SOURCE_DIR}/assets/audio")
file(GLOB audio_packs CONFIGURE_DEPENDS "${audio_source_directory}/*.tar.zst")
set(audio_stamp "${package_audio_directory}.unpacked-$<CONFIG>")
add_custom_command(OUTPUT "${audio_stamp}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/unpack-audio-assets.py"
    --source-dir "${audio_source_directory}" --output-dir "${package_audio_directory}"
    --bundle-dir "$<TARGET_FILE_DIR:toca-game>/assets/original-data" --zstd "${TOCA_ZSTD}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${audio_stamp}"
  DEPENDS ${audio_packs} "${PROJECT_SOURCE_DIR}/tools/unpack-audio-assets.py"
  VERBATIM)
add_custom_target(toca-audio-resources DEPENDS "${audio_stamp}")
add_dependencies(toca-game toca-audio-resources)
