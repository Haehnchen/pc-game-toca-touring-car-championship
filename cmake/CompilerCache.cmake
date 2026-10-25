include_guard(GLOBAL)

if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  return()
endif()

find_program(_toca_ccache NAMES ccache NO_CACHE)
if(_toca_ccache)
  foreach(language IN ITEMS C CXX)
    # C may be enabled later by a dependency. Preserve its environment override
    # before CMake has initialized the corresponding launcher variable.
    if(NOT DEFINED CMAKE_${language}_COMPILER_LAUNCHER AND
       NOT DEFINED ENV{CMAKE_${language}_COMPILER_LAUNCHER})
      set(CMAKE_${language}_COMPILER_LAUNCHER "${_toca_ccache}")
    endif()
  endforeach()
endif()
unset(_toca_ccache)
