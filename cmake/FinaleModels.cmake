# The convoy keeps its own two models; it is not an ALLCARS render-ID family.
set(finale_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/mega")
set(finale_registry "${PROJECT_SOURCE_DIR}/assets/models/mega/finale.json")
set(finale_builder "${PROJECT_SOURCE_DIR}/tools/build-finale-models.py")
set(finale_bundle "$<TARGET_FILE_DIR:toca-game>/assets/models/mega")
if(TOCA_MEGA_MODELS)
  file(GLOB finale_textures CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/models/finale/textures/*.webp")
  set(finale_manifest "${finale_generated}/finale-build.json")
  add_custom_command(OUTPUT "${finale_manifest}"
    COMMAND "${Python3_EXECUTABLE}" "${finale_builder}"
      --data-root "${original_data_directory}"
      --output "${finale_generated}" --meshopt-encoder "$<TARGET_FILE:toca-meshopt-encode>"
    BYPRODUCTS "${finale_generated}/finale/pace-car.glb" "${finale_generated}/finale/transporter.glb"
    DEPENDS "${finale_builder}" "${finale_registry}" ${finale_textures} toca-meshopt-encode
      "${PROJECT_SOURCE_DIR}/tools/mega_finale.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_model.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_manifest.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_glb.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_geometry.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_wheel_wells.py"
      "${PROJECT_SOURCE_DIR}/tools/vehicle_textures.py"
      "${original_data_directory}/data/champfin.bfp"
    VERBATIM)
  add_custom_target(toca-mega-finale-resources
    COMMAND "${Python3_EXECUTABLE}" "${finale_builder}"
      --output "${finale_generated}" --bundle "${finale_bundle}" --bundle-only
    DEPENDS "${finale_manifest}" VERBATIM)
else()
  add_custom_target(toca-mega-finale-resources
    COMMAND "${Python3_EXECUTABLE}" "${finale_builder}"
      --output "${finale_generated}" --bundle "${finale_bundle}" --disabled
    VERBATIM)
endif()
add_dependencies(toca-model-resources toca-mega-finale-resources)
add_dependencies(toca-mega-finale-resources toca-original-data)
