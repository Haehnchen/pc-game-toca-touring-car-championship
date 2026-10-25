include_guard(GLOBAL)

function(toca_setup_model_dependencies)
  toca_dependency_source(meshoptimizer meshoptimizer_source)
  set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
  set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
  set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${meshoptimizer_source}" "${CMAKE_BINARY_DIR}/dependencies/meshoptimizer" EXCLUDE_FROM_ALL)

  toca_dependency_source(cgltf cgltf_source)
  add_library(toca-cgltf INTERFACE)
  target_include_directories(toca-cgltf SYSTEM INTERFACE "${cgltf_source}")

  toca_dependency_source(libwebp webp_source)
  set(BUILD_SHARED_LIBS OFF)
  foreach(feature IN ITEMS ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP
      WEBPINFO LIBWEBPMUX WEBPMUX EXTRAS WEBP_JS FUZZTEST)
    set(WEBP_BUILD_${feature} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
  add_subdirectory("${webp_source}" "${CMAKE_BINARY_DIR}/dependencies/libwebp" EXCLUDE_FROM_ALL)
  target_include_directories(webpdecoder SYSTEM INTERFACE "$<BUILD_INTERFACE:${webp_source}/src>")
endfunction()
