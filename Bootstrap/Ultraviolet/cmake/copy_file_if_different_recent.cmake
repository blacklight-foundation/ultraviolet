foreach(required_var IN ITEMS SRC DST)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR
      "Missing required copy_file_if_different_recent variable: ${required_var}")
  endif()
endforeach()

get_filename_component(DST_DIR "${DST}" DIRECTORY)
file(MAKE_DIRECTORY "${DST_DIR}")

set(COPY_FILE_RETRY_COUNT 15)
set(copy_result "")
set(copy_succeeded FALSE)
set(copy_permission_denied FALSE)
if(NOT DEFINED ALLOW_PERMISSION_DENIED_IF_DST_EXISTS)
  set(ALLOW_PERMISSION_DENIED_IF_DST_EXISTS FALSE)
endif()

foreach(copy_attempt RANGE 1 ${COPY_FILE_RETRY_COUNT})
  file(COPY_FILE "${SRC}" "${DST}"
       RESULT copy_result
       ONLY_IF_DIFFERENT
       INPUT_MAY_BE_RECENT)
  if(copy_result STREQUAL "0")
    set(copy_succeeded TRUE)
    break()
  endif()

  string(FIND "${copy_result}" "Permission denied" permission_denied_pos)
  if(permission_denied_pos EQUAL -1)
    message(FATAL_ERROR
      "file COPY_FILE failed to copy ${SRC} to ${DST}: ${copy_result}")
  endif()
  set(copy_permission_denied TRUE)

  # Launcher-owned Windows packaging can race with an already-running output
  # executable. If the locked destination is already byte-identical to the
  # newly built source, preserve it and let packaging continue.
  if(EXISTS "${DST}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E compare_files "${SRC}" "${DST}"
      RESULT_VARIABLE compare_result
      OUTPUT_QUIET
      ERROR_QUIET)
    if(compare_result EQUAL 0)
      set(copy_succeeded TRUE)
      break()
    endif()
  endif()

  if(copy_attempt LESS COPY_FILE_RETRY_COUNT)
    # Newly linked Windows executables can remain locked longer than the
    # initial recent-input window. Keep early retries responsive, then extend
    # the backoff before giving up on a launcher-owned packaging step.
    set(copy_retry_sleep_seconds 1)
    if(copy_attempt GREATER 5)
      set(copy_retry_sleep_seconds 2)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep
                            "${copy_retry_sleep_seconds}")
  endif()
endforeach()

if(NOT copy_succeeded AND copy_permission_denied AND
   ALLOW_PERMISSION_DENIED_IF_DST_EXISTS AND EXISTS "${DST}")
  message(STATUS
    "Keeping locked existing destination ${DST}; refreshed copy remains at ${SRC}")
  set(copy_succeeded TRUE)
endif()

if(NOT copy_succeeded)
  message(FATAL_ERROR
    "file COPY_FILE failed to copy ${SRC} to ${DST} after ${COPY_FILE_RETRY_COUNT} attempts: ${copy_result}")
endif()
