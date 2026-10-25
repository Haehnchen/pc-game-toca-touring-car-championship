include_guard(GLOBAL)

# Only versioned, human-readable native data participates. The original EXE is
# neither an input nor a dependency; newline normalization keeps OS peers equal.
set(native_data_sources
  src/frontend/builtin_data.hpp
  src/frontend/builtin_data.cpp
  src/audio/audio_tables.hpp
  src/scene/hud_sprites.hpp
  src/scene/qualifying_tables.hpp
  src/scene/presentation_tables.hpp
  src/settings/championship_defaults.hpp)
foreach(source IN LISTS native_data_sources)
  list(APPEND native_data_dependencies "${PROJECT_SOURCE_DIR}/${source}")
endforeach()
set(native_data_identity "${CMAKE_CURRENT_BINARY_DIR}/generated/native_data_identity.hpp")
add_custom_command(OUTPUT "${native_data_identity}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/build-native-data-identity.py"
    --source-root "${PROJECT_SOURCE_DIR}" --output "${native_data_identity}" ${native_data_sources}
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/build-native-data-identity.py" ${native_data_dependencies}
  VERBATIM)
target_sources(toca-network PRIVATE "${native_data_identity}")
target_include_directories(toca-network PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
