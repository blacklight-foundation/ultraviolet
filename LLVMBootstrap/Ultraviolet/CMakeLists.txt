cmake_minimum_required(VERSION 4.0.1)
project(ultraviolet LANGUAGES C CXX)

set(UV_BUILD_DIR "${CMAKE_BINARY_DIR}")
file(MAKE_DIRECTORY "${UV_BUILD_DIR}")
get_filename_component(UV_SHARED_BUILD_DIR "${UV_BUILD_DIR}/.." REALPATH)
file(MAKE_DIRECTORY "${UV_SHARED_BUILD_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${UV_BUILD_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${UV_BUILD_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${UV_BUILD_DIR}")

if(UNIX AND NOT APPLE)
  set(CMAKE_C_ARCHIVE_CREATE
      "<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_C_ARCHIVE_APPEND
      "<CMAKE_AR> qT <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_CXX_ARCHIVE_CREATE
      "<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_CXX_ARCHIVE_APPEND
      "<CMAKE_AR> qT <TARGET> <LINK_FLAGS> <OBJECTS>")
endif()

if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${cfg}" cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${UV_BUILD_DIR}/${cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${UV_BUILD_DIR}/${cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${UV_BUILD_DIR}/${cfg}")
  endforeach()
endif()

if(NOT WIN32)
  get_filename_component(UV_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." REALPATH)
  set(UV_EXTERN_DIR "${UV_REPO_ROOT}/extern")
  set(UV_LINUX_ICU_ROOT "${UV_EXTERN_DIR}/icu/linux")
  set(UV_LINUX_ICU_SHARED_INSTALL_DIR "${UV_LINUX_ICU_ROOT}")
  set(UV_LINUX_ICU_SHARED_INCLUDE_DIR
      "${UV_LINUX_ICU_ROOT}/include")
  set(UV_LINUX_ICU_LIB_DIR
      "${UV_LINUX_ICU_ROOT}/lib")
  set(UV_LINUX_ICUUC_SHARED_REAL
      "${UV_LINUX_ICU_LIB_DIR}/libicuuc.so.72")
  set(UV_LINUX_ICUI18N_SHARED_REAL
      "${UV_LINUX_ICU_LIB_DIR}/libicui18n.so.72")
  set(UV_LINUX_ICUDATA_SHARED_REAL
      "${UV_LINUX_ICU_LIB_DIR}/libicudata.so.72")
  set(UV_LINUX_ICUDATA_DATA_REAL
      "${UV_LINUX_ICU_LIB_DIR}/icudt72l.dat")
  set(UV_LINUX_ICUUC_SHARED_SONAME "libicuuc.so.72")
  set(UV_LINUX_ICUI18N_SHARED_SONAME "libicui18n.so.72")
  set(UV_LINUX_ICUDATA_SHARED_SONAME "libicudata.so.72")
  set(UV_LINUX_ICUDATA_DATA_NAME "icudt72l.dat")
  set(UV_LINUX_ICU_STAGE_DIR "${CMAKE_BINARY_DIR}/lib")
  set(UV_LINUX_ICUUC_SHARED_STAGE
      "${UV_LINUX_ICU_STAGE_DIR}/${UV_LINUX_ICUUC_SHARED_SONAME}")
  set(UV_LINUX_ICUI18N_SHARED_STAGE
      "${UV_LINUX_ICU_STAGE_DIR}/${UV_LINUX_ICUI18N_SHARED_SONAME}")
  set(UV_LINUX_ICUDATA_SHARED_STAGE
      "${UV_LINUX_ICU_STAGE_DIR}/${UV_LINUX_ICUDATA_SHARED_SONAME}")
  set(UV_LINUX_ICUDATA_DATA_STAGE
      "${UV_LINUX_ICU_STAGE_DIR}/${UV_LINUX_ICUDATA_DATA_NAME}")

  foreach(required_icu_path IN ITEMS
      "${UV_LINUX_ICU_SHARED_INCLUDE_DIR}"
      "${UV_LINUX_ICUI18N_SHARED_REAL}"
      "${UV_LINUX_ICUUC_SHARED_REAL}"
      "${UV_LINUX_ICUDATA_SHARED_REAL}"
      "${UV_LINUX_ICUDATA_DATA_REAL}")
    if(NOT EXISTS "${required_icu_path}")
      message(FATAL_ERROR
        "Required prebuilt Linux ICU payload is missing: ${required_icu_path}\n"
        "Run scripts/bootstrap_linux_icu.sh to refresh extern/icu/linux.")
    endif()
  endforeach()

  if(NOT EXISTS "${UV_LINUX_ICU_SHARED_INCLUDE_DIR}")
    message(FATAL_ERROR
      "Required Linux ICU include directory not found: ${UV_LINUX_ICU_SHARED_INCLUDE_DIR}")
  endif()

  add_custom_command(
    OUTPUT
      "${UV_LINUX_ICUI18N_SHARED_STAGE}"
      "${UV_LINUX_ICUUC_SHARED_STAGE}"
      "${UV_LINUX_ICUDATA_SHARED_STAGE}"
      "${UV_LINUX_ICUDATA_DATA_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${UV_LINUX_ICU_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f
            "${UV_LINUX_ICU_STAGE_DIR}/libicudata.a"
            "${UV_LINUX_ICU_STAGE_DIR}/libicui18n.a"
            "${UV_LINUX_ICU_STAGE_DIR}/libicutest.a"
            "${UV_LINUX_ICU_STAGE_DIR}/libicutu.a"
            "${UV_LINUX_ICU_STAGE_DIR}/libicuuc.a"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${UV_LINUX_ICU_STAGE_DIR}/icu"
            "${UV_LINUX_ICU_STAGE_DIR}/pkgconfig"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${UV_LINUX_ICUI18N_SHARED_REAL}"
            "${UV_LINUX_ICUI18N_SHARED_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${UV_LINUX_ICUUC_SHARED_REAL}"
            "${UV_LINUX_ICUUC_SHARED_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${UV_LINUX_ICUDATA_SHARED_REAL}"
            "${UV_LINUX_ICUDATA_SHARED_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${UV_LINUX_ICUDATA_DATA_REAL}"
            "${UV_LINUX_ICUDATA_DATA_STAGE}"
    DEPENDS
      "${UV_LINUX_ICUI18N_SHARED_REAL}"
      "${UV_LINUX_ICUUC_SHARED_REAL}"
      "${UV_LINUX_ICUDATA_SHARED_REAL}"
      "${UV_LINUX_ICUDATA_DATA_REAL}"
    VERBATIM
  )
  add_custom_target(uv_linux_icu_sidecars
    DEPENDS
      "${UV_LINUX_ICUI18N_SHARED_STAGE}"
      "${UV_LINUX_ICUUC_SHARED_STAGE}"
      "${UV_LINUX_ICUDATA_SHARED_STAGE}"
      "${UV_LINUX_ICUDATA_DATA_STAGE}"
  )

  add_library(uv_linux_icuuc SHARED IMPORTED GLOBAL)
  set_target_properties(uv_linux_icuuc PROPERTIES
    IMPORTED_LOCATION "${UV_LINUX_ICUUC_SHARED_STAGE}"
    IMPORTED_SONAME "${UV_LINUX_ICUUC_SHARED_SONAME}"
    INTERFACE_INCLUDE_DIRECTORIES "${UV_LINUX_ICU_SHARED_INCLUDE_DIR}"
  )
  add_dependencies(uv_linux_icuuc uv_linux_icu_sidecars)

  add_library(uv_linux_icui18n SHARED IMPORTED GLOBAL)
  set_target_properties(uv_linux_icui18n PROPERTIES
    IMPORTED_LOCATION "${UV_LINUX_ICUI18N_SHARED_STAGE}"
    IMPORTED_SONAME "${UV_LINUX_ICUI18N_SHARED_SONAME}"
    INTERFACE_INCLUDE_DIRECTORIES "${UV_LINUX_ICU_SHARED_INCLUDE_DIR}"
  )
  add_dependencies(uv_linux_icui18n uv_linux_icu_sidecars)

  add_library(uv_linux_icudata SHARED IMPORTED GLOBAL)
  set_target_properties(uv_linux_icudata PROPERTIES
    IMPORTED_LOCATION "${UV_LINUX_ICUDATA_SHARED_STAGE}"
    IMPORTED_SONAME "${UV_LINUX_ICUDATA_SHARED_SONAME}"
    INTERFACE_INCLUDE_DIRECTORIES "${UV_LINUX_ICU_SHARED_INCLUDE_DIR}"
  )
  add_dependencies(uv_linux_icudata uv_linux_icu_sidecars)
endif()

add_subdirectory(src)
add_subdirectory(runtime)
