if(NOT DEFINED UV_DYLIB_PATH OR "${UV_DYLIB_PATH}" STREQUAL "")
  message(FATAL_ERROR "UV_DYLIB_PATH is required")
endif()

if(NOT EXISTS "${UV_DYLIB_PATH}")
  message(FATAL_ERROR "Cannot normalize missing Mach-O dylib: ${UV_DYLIB_PATH}")
endif()

if(DEFINED UV_DYLIB_ID AND NOT "${UV_DYLIB_ID}" STREQUAL "")
  execute_process(
    COMMAND install_name_tool -id "${UV_DYLIB_ID}" "${UV_DYLIB_PATH}"
    RESULT_VARIABLE UV_INSTALL_NAME_RESULT
    OUTPUT_VARIABLE UV_INSTALL_NAME_OUTPUT
    ERROR_VARIABLE UV_INSTALL_NAME_ERROR
  )
  if(NOT UV_INSTALL_NAME_RESULT EQUAL 0)
    message(FATAL_ERROR
      "install_name_tool -id failed for ${UV_DYLIB_PATH}: "
      "${UV_INSTALL_NAME_ERROR}${UV_INSTALL_NAME_OUTPUT}")
  endif()
endif()

if(NOT DEFINED UV_DYLIB_DEPENDENCY_NAMES OR
   "${UV_DYLIB_DEPENDENCY_NAMES}" STREQUAL "")
  return()
endif()

string(REPLACE "," ";" UV_DYLIB_DEPENDENCY_LIST "${UV_DYLIB_DEPENDENCY_NAMES}")

execute_process(
  COMMAND otool -L "${UV_DYLIB_PATH}"
  RESULT_VARIABLE UV_OTOOL_RESULT
  OUTPUT_VARIABLE UV_OTOOL_OUTPUT
  ERROR_VARIABLE UV_OTOOL_ERROR
)
if(NOT UV_OTOOL_RESULT EQUAL 0)
  message(FATAL_ERROR
    "otool -L failed for ${UV_DYLIB_PATH}: ${UV_OTOOL_ERROR}${UV_OTOOL_OUTPUT}")
endif()

string(REPLACE "\n" ";" UV_OTOOL_LINES "${UV_OTOOL_OUTPUT}")
foreach(UV_OTOOL_LINE IN LISTS UV_OTOOL_LINES)
  string(STRIP "${UV_OTOOL_LINE}" UV_LOAD_COMMAND)
  if("${UV_LOAD_COMMAND}" STREQUAL "" OR "${UV_LOAD_COMMAND}" MATCHES ":$")
    continue()
  endif()

  string(REGEX REPLACE " \\(.*\\)$" "" UV_LOAD_NAME "${UV_LOAD_COMMAND}")
  get_filename_component(UV_LOAD_BASENAME "${UV_LOAD_NAME}" NAME)

  foreach(UV_DEPENDENCY_NAME IN LISTS UV_DYLIB_DEPENDENCY_LIST)
    if("${UV_LOAD_BASENAME}" STREQUAL "${UV_DEPENDENCY_NAME}")
      set(UV_RPATH_LOAD_NAME "@rpath/${UV_DEPENDENCY_NAME}")
      if(NOT "${UV_LOAD_NAME}" STREQUAL "${UV_RPATH_LOAD_NAME}")
        execute_process(
          COMMAND install_name_tool -change
                  "${UV_LOAD_NAME}"
                  "${UV_RPATH_LOAD_NAME}"
                  "${UV_DYLIB_PATH}"
          RESULT_VARIABLE UV_CHANGE_RESULT
          OUTPUT_VARIABLE UV_CHANGE_OUTPUT
          ERROR_VARIABLE UV_CHANGE_ERROR
        )
        if(NOT UV_CHANGE_RESULT EQUAL 0)
          message(FATAL_ERROR
            "install_name_tool -change failed for ${UV_DYLIB_PATH}: "
            "${UV_CHANGE_ERROR}${UV_CHANGE_OUTPUT}")
        endif()
      endif()
    endif()
  endforeach()
endforeach()
