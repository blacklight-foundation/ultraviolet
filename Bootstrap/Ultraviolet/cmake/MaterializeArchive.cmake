if(NOT DEFINED AR_TOOL OR AR_TOOL STREQUAL "")
  message(FATAL_ERROR "AR_TOOL is required")
endif()

if(NOT DEFINED INPUT_ARCHIVE OR INPUT_ARCHIVE STREQUAL "")
  message(FATAL_ERROR "INPUT_ARCHIVE is required")
endif()

if(NOT DEFINED OUTPUT_ARCHIVE OR OUTPUT_ARCHIVE STREQUAL "")
  message(FATAL_ERROR "OUTPUT_ARCHIVE is required")
endif()

if(NOT DEFINED WORKING_DIRECTORY OR WORKING_DIRECTORY STREQUAL "")
  message(FATAL_ERROR "WORKING_DIRECTORY is required")
endif()

execute_process(
  COMMAND "${AR_TOOL}" t "${INPUT_ARCHIVE}"
  WORKING_DIRECTORY "${WORKING_DIRECTORY}"
  OUTPUT_VARIABLE archive_members
  ERROR_VARIABLE archive_list_error
  RESULT_VARIABLE archive_list_result
)

if(NOT archive_list_result EQUAL 0)
  message(FATAL_ERROR
    "failed to list archive members for ${INPUT_ARCHIVE}: ${archive_list_error}")
endif()

string(REPLACE "\r\n" "\n" archive_members "${archive_members}")
string(REPLACE "\r" "\n" archive_members "${archive_members}")
string(REGEX REPLACE "\n$" "" archive_members "${archive_members}")
string(REPLACE "\n" ";" archive_member_list "${archive_members}")

if(archive_member_list STREQUAL "")
  message(FATAL_ERROR "archive has no materializable members: ${INPUT_ARCHIVE}")
endif()

get_filename_component(output_dir "${OUTPUT_ARCHIVE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(REMOVE "${OUTPUT_ARCHIVE}")

execute_process(
  COMMAND "${AR_TOOL}" rcs "${OUTPUT_ARCHIVE}" ${archive_member_list}
  WORKING_DIRECTORY "${WORKING_DIRECTORY}"
  ERROR_VARIABLE archive_create_error
  RESULT_VARIABLE archive_create_result
)

if(NOT archive_create_result EQUAL 0)
  message(FATAL_ERROR
    "failed to materialize archive ${OUTPUT_ARCHIVE}: ${archive_create_error}")
endif()
