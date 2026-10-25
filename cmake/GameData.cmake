include_guard(GLOBAL)

# Git stores each approved BFP losslessly compressed; the game still reads BFP.
find_program(TOCA_ZSTD NAMES zstd REQUIRED)
set(original_data_source_directory "${PROJECT_SOURCE_DIR}/assets/data/original")
file(GLOB original_data_sources CONFIGURE_DEPENDS "${original_data_source_directory}/*.bfp.zst")
set(original_data_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/original-data")
set(original_data_files)
foreach(source IN LISTS original_data_sources)
  get_filename_component(filename "${source}" NAME_WLE)
  list(APPEND original_data_files "${original_data_directory}/data/${filename}")
endforeach()
add_custom_command(OUTPUT ${original_data_files}
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/unpack-game-data.py"
    --source-dir "${original_data_source_directory}" --output-dir "${original_data_directory}"
    --zstd "${TOCA_ZSTD}"
  DEPENDS ${original_data_sources} "${PROJECT_SOURCE_DIR}/tools/unpack-game-data.py"
  VERBATIM)
add_custom_target(toca-original-data DEPENDS ${original_data_files})
add_custom_target(toca-game-data-resources
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:toca-game>/assets/original-data/data"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${original_data_files}
    "$<TARGET_FILE_DIR:toca-game>/assets/original-data/data"
  DEPENDS toca-original-data VERBATIM)
add_dependencies(toca-game toca-game-data-resources)
