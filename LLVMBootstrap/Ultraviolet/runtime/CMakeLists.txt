# =============================================================================
# Ultraviolet Runtime Library
# =============================================================================
#
# This builds the Ultraviolet runtime library (UltravioletRT) which provides:
# - Memory management (heap, region allocators)
# - String and bytes operations
# - Context and capability initialization
# - IO operations
# - Structured concurrency (spawn/wait, dispatch, cancellation)
# - Panic handling
#
# The runtime is written in pure C (not C++) for ABI stability.
# Platform-dependent runtime backends live under src/platform.

get_filename_component(UV_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." REALPATH)
set(UV_EXTERN_DIR "${UV_REPO_ROOT}/extern")
set(RUNTIME_PLATFORM_SOURCES)
set(RUNTIME_PLATFORM_LIBS)
set(RUNTIME_COMPAT_SOURCES)
set(RUNTIME_STARTUP_OBJECT_NAME "")
set(RUNTIME_PLATFORM_DEFINITION "")
set(RUNTIME_SUPPORT_TARGETS)

if(WIN32)
  set(RUNTIME_PLATFORM_DEFINITION UV_RT_PLATFORM_WINDOWS=1)
  set(RUNTIME_ICU_ROOT "${UV_EXTERN_DIR}/icu/win64")
  set(RUNTIME_ICU_INCLUDE_DIR "${RUNTIME_ICU_ROOT}/include")
  set(RUNTIME_ICU_LIB_DIR "${RUNTIME_ICU_ROOT}/lib64")
  set(RUNTIME_ICU_BIN_DIR "${RUNTIME_ICU_ROOT}/bin64")
  set(RUNTIME_ICU_LIBS
    "${RUNTIME_ICU_LIB_DIR}/icuuc.lib"
    "${RUNTIME_ICU_LIB_DIR}/icuin.lib"
    "${RUNTIME_ICU_LIB_DIR}/icudt.lib"
  )
  set(RUNTIME_ICU_DLLS
    "${RUNTIME_ICU_BIN_DIR}/icuuc72.dll"
    "${RUNTIME_ICU_BIN_DIR}/icuin72.dll"
    "${RUNTIME_ICU_BIN_DIR}/icudt72.dll"
  )
  set(RUNTIME_COMPAT_SOURCES
    src/compat/rtc_stubs.c
  )
  set(RUNTIME_PLATFORM_SOURCES
    src/platform/windows/rt_platform_windows.c
    src/platform/windows/rt_path_windows.c
  )
else()
  set(RUNTIME_PLATFORM_DEFINITION UV_RT_PLATFORM_LINUX=1)
  if(NOT TARGET uv_linux_icui18n OR
     NOT TARGET uv_linux_icuuc OR
     NOT TARGET uv_linux_icudata)
    message(FATAL_ERROR "Required Linux ICU shared-library sidecar targets are missing")
  endif()
  set(RUNTIME_ICU_ROOT "${UV_LINUX_ICU_SHARED_INSTALL_DIR}")
  set(RUNTIME_ICU_INCLUDE_DIR "${UV_LINUX_ICU_SHARED_INCLUDE_DIR}")
  set(RUNTIME_ICU_LIB_DIR "${UV_LINUX_ICU_SHARED_INSTALL_DIR}/lib")
  set(RUNTIME_ICU_LIBS
    uv_linux_icui18n
    uv_linux_icuuc
    uv_linux_icudata
  )
  set(RUNTIME_ICUI18N_SO_STAGE "${UV_LINUX_ICUI18N_SHARED_STAGE}")
  set(RUNTIME_ICUUC_SO_STAGE "${UV_LINUX_ICUUC_SHARED_STAGE}")
  set(RUNTIME_ICUDATA_SO_STAGE "${UV_LINUX_ICUDATA_SHARED_STAGE}")
  set(RUNTIME_ICUI18N_SO_NAME "${UV_LINUX_ICUI18N_SHARED_SONAME}")
  set(RUNTIME_ICUUC_SO_NAME "${UV_LINUX_ICUUC_SHARED_SONAME}")
  set(RUNTIME_ICUDATA_SO_NAME "${UV_LINUX_ICUDATA_SHARED_SONAME}")
  add_library(uv_rt_linux_support SHARED
    src/platform/common/rt_startup_state.c
    src/platform/linux/rt_platform_linux.c
    src/platform/linux/rt_path_linux.c
  )
  target_include_directories(uv_rt_linux_support PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(uv_rt_linux_support PUBLIC
    ${RUNTIME_PLATFORM_DEFINITION}
  )
  target_link_libraries(uv_rt_linux_support PRIVATE
    ${RUNTIME_ICU_LIBS}
    ${CMAKE_DL_LIBS}
  )
  set_target_properties(uv_rt_linux_support PROPERTIES
    OUTPUT_NAME "UltravioletRTSupport"
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  )
  target_compile_options(uv_rt_linux_support PRIVATE
    -fno-stack-protector
  )
  set(RUNTIME_SUPPORT_TARGETS
    uv_rt_linux_support
  )
  set(RUNTIME_PLATFORM_LIBS
    uv_rt_linux_support
  )
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    add_library(uv_rt_startup_x86_64_sysv OBJECT
      src/platform/linux/rt_startup_x86_64_sysv.c
    )
    set_target_properties(uv_rt_startup_x86_64_sysv PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
    )
    target_compile_definitions(uv_rt_startup_x86_64_sysv PRIVATE
      ${RUNTIME_PLATFORM_DEFINITION}
    )
    target_compile_options(uv_rt_startup_x86_64_sysv PRIVATE
      -fno-stack-protector
    )
    set(RUNTIME_STARTUP_OBJECT_NAME "uv_start_x86_64_sysv.o")
    list(APPEND RUNTIME_PLATFORM_SOURCES
      $<TARGET_OBJECTS:uv_rt_startup_x86_64_sysv>
    )
  endif()
endif()

set(UV_RUNTIME_SOURCES
  # Core
  src/core/mem.c
  src/core/panic.c
  src/core/spec_trace.c

  # Platform common
  src/platform/common/rt_startup_state.c

  # Context
  src/context/context.c
  src/context/host_session.c
  src/context/heap.c

  # Memory
  src/memory/region.c
  src/memory/async.c
  src/memory/string_bytes.c

  # I/O
  src/io/io.c
  src/network/network.c
  src/system/process.c
  src/system/raw_dylib.c
  src/time/time.c

  # Concurrency
  src/concurrency/key.c
  src/concurrency/parallel.c

  # Compatibility
  ${RUNTIME_COMPAT_SOURCES}
  ${RUNTIME_PLATFORM_SOURCES}
)

add_library(uv_rt STATIC
  ${UV_RUNTIME_SOURCES}
)

target_include_directories(uv_rt PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(uv_rt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${RUNTIME_ICU_INCLUDE_DIR}
)

target_compile_definitions(uv_rt PUBLIC
  ${RUNTIME_PLATFORM_DEFINITION}
)

target_link_libraries(uv_rt PUBLIC
  ${RUNTIME_ICU_LIBS}
  ${RUNTIME_PLATFORM_LIBS}
)

set_target_properties(uv_rt PROPERTIES
  OUTPUT_NAME "UltravioletRT"
  PREFIX ""
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
)

if(NOT WIN32)
  target_compile_options(uv_rt PRIVATE
    -fno-stack-protector
  )
endif()

if(NOT WIN32)
  add_dependencies(uv_rt uv_linux_icu_sidecars)
  add_dependencies(uv_rt ${RUNTIME_SUPPORT_TARGETS})
endif()

if(MSVC)
  # /GS- disables buffer security checks.
  # /Zl omits default library name in .obj.
  target_compile_options(uv_rt PRIVATE /GS- /Zl)
  # Leave the runtime archive free of MSVC debug runtime-check helpers.
  set_property(TARGET uv_rt PROPERTY MSVC_RUNTIME_CHECKS "")
endif()

add_executable(uv_rt_task070_test
  tests/task070_region_runtime.c
)

target_link_libraries(uv_rt_task070_test PRIVATE
  uv_rt
)

if(NOT WIN32)
  target_link_libraries(uv_rt_task070_test PRIVATE
    ${RUNTIME_ICU_LIBS}
    ${RUNTIME_PLATFORM_LIBS}
  )
  target_link_options(uv_rt_task070_test PRIVATE
    "LINKER:--no-as-needed"
  )
endif()

target_include_directories(uv_rt_task070_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(uv_rt_task070_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
)

if(NOT WIN32)
  set_target_properties(uv_rt_task070_test PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "\$ORIGIN/lib"
  )
endif()

if(WIN32)
  add_custom_command(TARGET uv_rt_task070_test POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${RUNTIME_ICU_DLLS}
            "$<TARGET_FILE_DIR:uv_rt_task070_test>"
    COMMENT "Copying ICU DLLs for runtime tests"
  )
endif()

add_executable(uv_rt_task075_test
  tests/task075_io_runtime.c
)

target_link_libraries(uv_rt_task075_test PRIVATE
  uv_rt
)

if(NOT WIN32)
  target_link_libraries(uv_rt_task075_test PRIVATE
    ${RUNTIME_ICU_LIBS}
    ${RUNTIME_PLATFORM_LIBS}
  )
  target_link_options(uv_rt_task075_test PRIVATE
    "LINKER:--no-as-needed"
  )
endif()

target_include_directories(uv_rt_task075_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(uv_rt_task075_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
)

if(NOT WIN32)
  set_target_properties(uv_rt_task075_test PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "\$ORIGIN/lib"
  )
endif()

if(WIN32)
  add_custom_command(TARGET uv_rt_task075_test POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${RUNTIME_ICU_DLLS}
            "$<TARGET_FILE_DIR:uv_rt_task075_test>"
    COMMENT "Copying ICU DLLs for runtime tests"
  )
endif()

add_executable(uv_rt_task104_test
  tests/task104_spec_trace_runtime.c
)

target_link_libraries(uv_rt_task104_test PRIVATE
  uv_rt
)

if(NOT WIN32)
  target_link_libraries(uv_rt_task104_test PRIVATE
    ${RUNTIME_ICU_LIBS}
    ${RUNTIME_PLATFORM_LIBS}
  )
  target_link_options(uv_rt_task104_test PRIVATE
    "LINKER:--no-as-needed"
  )
endif()

target_include_directories(uv_rt_task104_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(uv_rt_task104_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
)

if(NOT WIN32)
  set_target_properties(uv_rt_task104_test PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    BUILD_RPATH "\$ORIGIN/lib"
  )
endif()

if(WIN32)
  add_custom_command(TARGET uv_rt_task104_test POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${RUNTIME_ICU_DLLS}
            "$<TARGET_FILE_DIR:uv_rt_task104_test>"
    COMMENT "Copying ICU DLLs for runtime tests"
  )
endif()

# On Windows, stage a final runtime archive that bundles the runtime,
# kernel32, the ICU import libraries, and only the CRT stack-probe objects
# needed for large stack frames. The archive MUST stay CRT-free so raw-dylib
# imports such as msvcrt do not collide with bundled _fltused/_RTC_* symbols.
#
# Note: vcruntime.lib is NOT merged because the runtime is CRT-free:
# - mem.c provides memset/memcpy/memmove/memcmp
# - rtc_stubs.c provides _RTC_* stubs
# - /GS- disables security cookies
# - Pure C code, no C++ exceptions or RTTI
# - only chkstk.obj and chkstk2.obj are extracted from libcmt.lib for
#   __chkstk / __chkstk2 support
#
# The library builds to the configuration-specific archive directory, then gets
# merged/copy-staged into a stable runtime path under the build tree.
if(MSVC)
  set(FINAL_RT_DIR "${CMAKE_BINARY_DIR}/runtime")
  set(FINAL_RT "${FINAL_RT_DIR}/UltravioletRT.lib")
  set(RUNTIME_STAGE_SCRIPT
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stage_windows_runtime.cmake")
  set(RUNTIME_LLVM_ARCH_DIR "llvm-21.1.8-x86_64")
  set(RUNTIME_LLVM_ROOT "${UV_EXTERN_DIR}/llvm/${RUNTIME_LLVM_ARCH_DIR}")
  set(RUNTIME_LLVM_AR "${RUNTIME_LLVM_ROOT}/bin/llvm-ar.exe")

  if(NOT EXISTS "${RUNTIME_STAGE_SCRIPT}")
    message(FATAL_ERROR
      "Windows runtime staging helper is missing: ${RUNTIME_STAGE_SCRIPT}")
  endif()

  if(NOT EXISTS "${RUNTIME_LLVM_AR}")
    message(FATAL_ERROR
      "Required llvm-ar not found for Windows runtime staging: ${RUNTIME_LLVM_AR}")
  endif()


  add_custom_command(TARGET uv_rt POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DRUNTIME_LIB_TOOL=${CMAKE_AR}"
            "-DRUNTIME_LLVM_AR=${RUNTIME_LLVM_AR}"
            "-DRUNTIME_INPUT=$<TARGET_FILE:uv_rt>"
            "-DFINAL_RT_DIR=${FINAL_RT_DIR}"
            "-DFINAL_RT=${FINAL_RT}"
            "-DRUNTIME_LIBCMT_LIBRARY=libcmt.lib"
            "-DRUNTIME_KERNEL32_LIBRARY=kernel32.lib"
            "-DRUNTIME_ICU_LIBS=${RUNTIME_ICU_LIBS}"
            "-DRUNTIME_ICU_DLLS=${RUNTIME_ICU_DLLS}"
            -P "${RUNTIME_STAGE_SCRIPT}"
    COMMENT "Staging CRT-free Windows runtime archive"
  )

else()
  # On non-Windows, stage the archive in the build runtime directory.
  set(FINAL_RT_DIR "${CMAKE_BINARY_DIR}/runtime")
  add_custom_command(TARGET uv_rt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${FINAL_RT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:uv_rt>" "${FINAL_RT_DIR}/UltravioletRT.a"
    COMMENT "Staging Linux Ultraviolet runtime archive"
  )
  add_custom_command(TARGET uv_rt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:uv_rt_linux_support>"
            "${CMAKE_BINARY_DIR}/lib/$<TARGET_FILE_NAME:uv_rt_linux_support>"
    COMMENT "Staging Linux runtime support sidecar"
  )
  if(TARGET uv_rt_startup_x86_64_sysv)
    add_custom_command(TARGET uv_rt POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_OBJECTS:uv_rt_startup_x86_64_sysv>"
              "${FINAL_RT_DIR}/${RUNTIME_STARTUP_OBJECT_NAME}"
      COMMENT "Copying Linux runtime startup object to build runtime directory"
    )
  endif()
endif()
