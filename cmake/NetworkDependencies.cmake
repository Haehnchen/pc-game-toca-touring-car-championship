include_guard(GLOBAL)

function(toca_setup_network_dependency)
  enable_language(C)
  toca_dependency_source(enet enet_source)
  # ENet's configuration checks are C checks; its warnings remain private to
  # the upstream directory and never inherit toca-project-options.
  include(CheckCSourceCompiles)
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory("${enet_source}" "${CMAKE_BINARY_DIR}/dependencies/enet" EXCLUDE_FROM_ALL)
  target_include_directories(enet SYSTEM INTERFACE "${enet_source}/include")
  add_library(ENet::ENet ALIAS enet)
endfunction()
