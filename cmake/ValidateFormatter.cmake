# Formatting is an explicit developer check, never a game-build prerequisite.
set(format_hint "Install clang-format 21.1.8 or configure with -DCLANG_FORMAT_EXECUTABLE=/path/to/clang-format-21.1.8")
if(NOT CLANG_FORMAT_EXECUTABLE)
  message(FATAL_ERROR "clang-format 21.1.8 is required for format-check. ${format_hint}")
endif()
execute_process(COMMAND "${CLANG_FORMAT_EXECUTABLE}" --version
  RESULT_VARIABLE format_result OUTPUT_VARIABLE format_version ERROR_VARIABLE format_error
  TIMEOUT 10 OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT format_result STREQUAL "0" OR NOT format_version MATCHES "version 21\\.1\\.8([^0-9.]|$)")
  message(FATAL_ERROR "clang-format 21.1.8 is required for format-check; got '${format_version}${format_error}'. ${format_hint}")
endif()
