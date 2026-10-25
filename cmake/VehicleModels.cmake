# One registered vehicle per build command; reference-only entries are QA inputs.
set(vehicle_manifest "${PROJECT_SOURCE_DIR}/assets/models/mega/vehicles.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${vehicle_manifest}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/sync-vehicle-models.py"
  --manifest "${vehicle_manifest}" --validate COMMAND_ERROR_IS_FATAL ANY)
file(READ "${vehicle_manifest}" vehicle_document)
string(JSON vehicle_count LENGTH "${vehicle_document}" vehicles)
set(vehicle_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/mega")
set(vehicle_models)
set(vehicle_test_keys)
set(vehicle_sync_options)
if(NOT TOCA_MEGA_MODELS)
  list(APPEND vehicle_sync_options --disabled)
endif()
if(vehicle_count GREATER 0)
  math(EXPR last_vehicle "${vehicle_count} - 1")
  foreach(index RANGE 0 ${last_vehicle})
    string(JSON vehicle GET "${vehicle_document}" vehicles ${index})
    foreach(field IN ITEMS key mode definition output render_id)
      string(JSON vehicle_${field} GET "${vehicle}" ${field})
    endforeach()
    string(JSON vehicle_archive GET "${vehicle}" source archive)
    if(NOT TOCA_MEGA_MODELS)
      continue()
    endif()
    set(vehicle_source_archive "${original_data_directory}/${vehicle_archive}")
    get_filename_component(vehicle_filename "${vehicle_output}" NAME)
    if(vehicle_mode STREQUAL "mega")
      set(vehicle_model "${vehicle_generated}/${vehicle_filename}")
      set(vehicle_options)
      list(APPEND vehicle_models "${vehicle_model}")
    else()
      set(vehicle_model "${CMAKE_CURRENT_BINARY_DIR}/generated/vehicle-reference/${vehicle_filename}")
      set(vehicle_options --reference)
    endif()
    set(vehicle_${vehicle_key}_model "${vehicle_model}")
    set(vehicle_${vehicle_key}_render_id "${vehicle_render_id}")
    set(vehicle_${vehicle_key}_mode "${vehicle_mode}")
    list(APPEND vehicle_test_keys "${vehicle_key}")
    get_filename_component(vehicle_package "${PROJECT_SOURCE_DIR}/${vehicle_definition}" DIRECTORY)
    file(READ "${PROJECT_SOURCE_DIR}/${vehicle_definition}" vehicle_definition_document)
    string(JSON vehicle_geometry GET "${vehicle_definition_document}" geometry)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${vehicle_definition}")
    file(GLOB_RECURSE vehicle_inputs CONFIGURE_DEPENDS
      "${vehicle_package}/*.json" "${vehicle_package}/*.webp" "${vehicle_package}/*.png" "${vehicle_package}/*.bmp")
    foreach(implementation IN ITEMS "${vehicle_key}" "${vehicle_geometry}")
      foreach(module IN ITEMS geometry textures)
        if(EXISTS "${PROJECT_SOURCE_DIR}/tools/${implementation}_${module}.py")
          list(APPEND vehicle_inputs "${PROJECT_SOURCE_DIR}/tools/${implementation}_${module}.py")
        endif()
      endforeach()
    endforeach()
    list(REMOVE_DUPLICATES vehicle_inputs)
    add_custom_command(OUTPUT "${vehicle_model}"
      COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/build-vehicle-models.py"
        --manifest "${vehicle_manifest}" --key "${vehicle_key}" ${vehicle_options}
        --data-root "${original_data_directory}"
        --output "${vehicle_model}" --meshopt-encoder "$<TARGET_FILE:toca-meshopt-encode>"
      DEPENDS "${vehicle_manifest}" "${vehicle_source_archive}" ${vehicle_inputs}
        "${PROJECT_SOURCE_DIR}/tools/build-vehicle-models.py" "${PROJECT_SOURCE_DIR}/tools/vehicle_manifest.py"
        "${PROJECT_SOURCE_DIR}/tools/vehicle_model.py" "${PROJECT_SOURCE_DIR}/tools/vehicle_glb.py"
        "${PROJECT_SOURCE_DIR}/tools/vehicle_textures.py" "${PROJECT_SOURCE_DIR}/tools/vehicle_geometry.py"
        "${PROJECT_SOURCE_DIR}/tools/vehicle_wheel_wells.py" "${PROJECT_SOURCE_DIR}/tools/vehicle_wheel_stance.py"
        toca-meshopt-encode
      VERBATIM)
    if(vehicle_mode STREQUAL "reference")
      add_custom_target(toca-${vehicle_key}-reference-model DEPENDS "${vehicle_model}")
      add_dependencies(toca-${vehicle_key}-reference-model toca-original-data)
    endif()
  endforeach()
endif()
add_custom_target(toca-model-resources
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/sync-vehicle-models.py"
    --manifest "${vehicle_manifest}" --generated "${vehicle_generated}"
    --bundle "$<TARGET_FILE_DIR:toca-game>/assets/models/mega" ${vehicle_sync_options}
  DEPENDS ${vehicle_models} VERBATIM)
add_dependencies(toca-game toca-model-resources)
add_dependencies(toca-model-resources toca-original-data)
