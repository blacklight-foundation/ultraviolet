if(NOT DEFINED UV_MACHO_PATH OR "${UV_MACHO_PATH}" STREQUAL "")
  message(FATAL_ERROR "UV_MACHO_PATH is required")
endif()

if(NOT EXISTS "${UV_MACHO_PATH}")
  message(FATAL_ERROR "Cannot normalize missing Mach-O file: ${UV_MACHO_PATH}")
endif()

execute_process(
  COMMAND otool -l "${UV_MACHO_PATH}"
  RESULT_VARIABLE UV_OTOOL_RESULT
  OUTPUT_VARIABLE UV_OTOOL_OUTPUT
  ERROR_VARIABLE UV_OTOOL_ERROR
)
if(NOT UV_OTOOL_RESULT EQUAL 0)
  message(FATAL_ERROR
    "otool -l failed for ${UV_MACHO_PATH}: ${UV_OTOOL_ERROR}${UV_OTOOL_OUTPUT}")
endif()

set(UV_EXISTING_RPATHS)
string(REPLACE "\n" ";" UV_OTOOL_LINES "${UV_OTOOL_OUTPUT}")
foreach(UV_OTOOL_LINE IN LISTS UV_OTOOL_LINES)
  string(STRIP "${UV_OTOOL_LINE}" UV_LOAD_LINE)
  if("${UV_LOAD_LINE}" MATCHES "^path ([^ ]+) \\(offset [0-9]+\\)$")
    list(APPEND UV_EXISTING_RPATHS "${CMAKE_MATCH_1}")
  endif()
endforeach()

if(DEFINED UV_DELETE_RPATHS AND NOT "${UV_DELETE_RPATHS}" STREQUAL "")
  string(REPLACE "," ";" UV_DELETE_RPATH_LIST "${UV_DELETE_RPATHS}")
  foreach(UV_RPATH IN LISTS UV_DELETE_RPATH_LIST)
    list(FIND UV_EXISTING_RPATHS "${UV_RPATH}" UV_RPATH_INDEX)
    if(NOT UV_RPATH_INDEX EQUAL -1)
      execute_process(
        COMMAND install_name_tool -delete_rpath "${UV_RPATH}" "${UV_MACHO_PATH}"
        RESULT_VARIABLE UV_DELETE_RESULT
        OUTPUT_VARIABLE UV_DELETE_OUTPUT
        ERROR_VARIABLE UV_DELETE_ERROR
      )
      if(NOT UV_DELETE_RESULT EQUAL 0)
        message(FATAL_ERROR
          "install_name_tool -delete_rpath failed for ${UV_MACHO_PATH}: "
          "${UV_DELETE_ERROR}${UV_DELETE_OUTPUT}")
      endif()
      list(REMOVE_ITEM UV_EXISTING_RPATHS "${UV_RPATH}")
    endif()
  endforeach()
endif()

if(DEFINED UV_REQUIRED_RPATHS AND NOT "${UV_REQUIRED_RPATHS}" STREQUAL "")
  string(REPLACE "," ";" UV_REQUIRED_RPATH_LIST "${UV_REQUIRED_RPATHS}")
  foreach(UV_RPATH IN LISTS UV_REQUIRED_RPATH_LIST)
    list(FIND UV_EXISTING_RPATHS "${UV_RPATH}" UV_RPATH_INDEX)
    if(UV_RPATH_INDEX EQUAL -1)
      execute_process(
        COMMAND install_name_tool -add_rpath "${UV_RPATH}" "${UV_MACHO_PATH}"
        RESULT_VARIABLE UV_ADD_RESULT
        OUTPUT_VARIABLE UV_ADD_OUTPUT
        ERROR_VARIABLE UV_ADD_ERROR
      )
      if(NOT UV_ADD_RESULT EQUAL 0)
        message(FATAL_ERROR
          "install_name_tool -add_rpath failed for ${UV_MACHO_PATH}: "
          "${UV_ADD_ERROR}${UV_ADD_OUTPUT}")
      endif()
      list(APPEND UV_EXISTING_RPATHS "${UV_RPATH}")
    endif()
  endforeach()
endif()
