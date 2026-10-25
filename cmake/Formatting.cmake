include_guard(GLOBAL)

function(toca_setup_formatting)
  find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format-21 clang-format
    DOC "Path to clang-format 21.1.8")
  add_custom_target(toca-format-tool-check
    COMMAND "${CMAKE_COMMAND}" "-DCLANG_FORMAT_EXECUTABLE:FILEPATH=${CLANG_FORMAT_EXECUTABLE}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ValidateFormatter.cmake"
    VERBATIM)
  set(format_command "${CLANG_FORMAT_EXECUTABLE}")
  if(NOT format_command)
    # The prerequisite reports the missing tool before any batch can run.
    set(format_command clang-format-21)
  endif()
  file(GLOB_RECURSE format_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp" "${PROJECT_SOURCE_DIR}/tests/*.hpp")
  # Balance whole files across a few processes; keep every source in the check.
  foreach(batch RANGE 0 5)
    set(format_batch_${batch} "")
    set(format_size_${batch} 0)
  endforeach()
  foreach(source IN LISTS format_sources)
    set(smallest 0)
    foreach(batch RANGE 1 5)
      if(format_size_${batch} LESS format_size_${smallest})
        set(smallest ${batch})
      endif()
    endforeach()
    file(SIZE "${source}" source_size)
    list(APPEND format_batch_${smallest} "${source}")
    math(EXPR format_size_${smallest} "${format_size_${smallest}} + ${source_size}")
  endforeach()
  add_custom_target(format-check)
  add_dependencies(format-check toca-format-tool-check)
  foreach(batch RANGE 0 5)
    if(format_batch_${batch})
      add_custom_target(toca-format-check-${batch}
        COMMAND "${format_command}" --dry-run --Werror ${format_batch_${batch}}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM
        COMMENT "Checking C++ formatting batch ${batch} with clang-format 21.1.8")
      add_dependencies(format-check toca-format-check-${batch})
      add_dependencies(toca-format-check-${batch} toca-format-tool-check)
    endif()
  endforeach()
endfunction()
