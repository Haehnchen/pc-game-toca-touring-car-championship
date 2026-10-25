# Shared parts, per-car hands and dashboards have one managed build producer.
set(cockpit_manifest "${PROJECT_SOURCE_DIR}/assets/models/mega/cockpits.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cockpit_manifest}")
set(cockpit_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/mega")
set(cockpit_models)
set(cockpit_sync_options)
if(TOCA_MEGA_MODELS)
  file(READ "${cockpit_manifest}" cockpit_document)
  string(JSON cockpit_model_count LENGTH "${cockpit_document}" models)
  math(EXPR cockpit_model_last "${cockpit_model_count} - 1")
  foreach(index RANGE 0 ${cockpit_model_last})
    string(JSON cockpit_model GET "${cockpit_document}" models ${index} output)
    list(APPEND cockpit_models "${cockpit_generated}/${cockpit_model}")
    string(JSON cockpit_definition ERROR_VARIABLE cockpit_definition_error GET "${cockpit_document}" models ${index} definition)
    if(cockpit_definition_error)
      string(JSON cockpit_definition GET "${cockpit_document}" definition)
    endif()
    string(JSON cockpit_texture_directory ERROR_VARIABLE cockpit_texture_error GET "${cockpit_document}" models ${index} texture_directory)
    if(cockpit_texture_error)
      string(JSON cockpit_texture_directory GET "${cockpit_document}" texture_directory)
    endif()
    file(GLOB cockpit_artwork CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${cockpit_texture_directory}/*.webp")
    list(APPEND cockpit_inputs "${PROJECT_SOURCE_DIR}/${cockpit_definition}" ${cockpit_artwork})
  endforeach()
  string(JSON cockpit_count LENGTH "${cockpit_document}" consumers)
  math(EXPR cockpit_last "${cockpit_count} - 1")
  foreach(index RANGE 0 ${cockpit_last})
    string(JSON cockpit_archive GET "${cockpit_document}" consumers ${index} archive)
    list(APPEND cockpit_inputs "${original_data_directory}/${cockpit_archive}")
  endforeach()
  list(REMOVE_DUPLICATES cockpit_inputs)
  add_custom_command(OUTPUT ${cockpit_models} "${cockpit_generated}/cockpit-manifest.json"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/build-cockpit-models.py"
      --data-root "${original_data_directory}"
      --output "${cockpit_generated}" --meshopt-encoder "$<TARGET_FILE:toca-meshopt-encode>"
    DEPENDS "${cockpit_manifest}" ${cockpit_inputs}
      "${PROJECT_SOURCE_DIR}/tools/build-cockpit-models.py"
      "${PROJECT_SOURCE_DIR}/tools/cockpit_geometry.py" "${PROJECT_SOURCE_DIR}/tools/cockpit_textures.py"
      "${PROJECT_SOURCE_DIR}/tools/cockpit_hand_geometry.py" "${PROJECT_SOURCE_DIR}/tools/cockpit_hand_textures.py"
      "${PROJECT_SOURCE_DIR}/tools/cockpit_dashboard_geometry.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_model.py" "${PROJECT_SOURCE_DIR}/tools/vehicle_glb.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_textures.py" "${PROJECT_SOURCE_DIR}/tools/vehicle_manifest.py" toca-meshopt-encode
    VERBATIM)
else()
  list(APPEND cockpit_sync_options --disabled)
endif()
add_custom_target(toca-cockpit-resources
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/build-cockpit-models.py"
    --output "${cockpit_generated}" --bundle "$<TARGET_FILE_DIR:toca-game>/assets/models/mega"
    --bundle-only ${cockpit_sync_options}
  DEPENDS ${cockpit_models} "${cockpit_manifest}" "${PROJECT_SOURCE_DIR}/tools/build-cockpit-models.py"
  VERBATIM)
add_dependencies(toca-game toca-cockpit-resources)
add_dependencies(toca-cockpit-resources toca-original-data)
