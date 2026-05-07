#ifndef CURSIVE_RT_PLATFORM_LINUX_H
#define CURSIVE_RT_PLATFORM_LINUX_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cursive_rt_process_launch_t cursive_rt_process_launch_t;
typedef struct cursive_rt_process_t cursive_rt_process_t;
typedef struct cursive_rt_file_offset_t cursive_rt_file_offset_t;
typedef struct cursive_rt_timestamp_t cursive_rt_timestamp_t;
typedef struct cursive_rt_directory_scan_entry_t
    cursive_rt_directory_scan_entry_t;
typedef struct cursive_rt_process_launch_t cursive_platform_process_startup_t;
typedef struct cursive_rt_process_t cursive_platform_process_info_t;
typedef struct cursive_rt_file_offset_t cursive_platform_large_integer_t;
typedef struct cursive_rt_timestamp_t cursive_platform_filetime_t;
typedef struct cursive_rt_directory_scan_entry_t cursive_platform_find_data_t;

typedef struct cursive_rt_backend_handle_impl* cursive_rt_backend_handle_t;
typedef uint32_t cursive_rt_backend_u32_t;
typedef uintptr_t cursive_rt_backend_uptr_t;
typedef int32_t cursive_rt_backend_i32_t;
typedef int64_t cursive_rt_backend_i64_t;
typedef int64_t cursive_rt_backend_signed_size64_t;
typedef unsigned int cursive_rt_backend_uint_t;
typedef int cursive_rt_backend_bool_t;
typedef void* cursive_rt_backend_module_t;

typedef cursive_rt_backend_handle_t cursive_platform_handle_t;
typedef cursive_rt_backend_u32_t cursive_platform_u32_t;
typedef cursive_rt_backend_uptr_t cursive_platform_uptr_t;
typedef cursive_rt_backend_i32_t cursive_platform_i32_t;
typedef cursive_rt_backend_i64_t cursive_platform_i64_t;
typedef cursive_rt_backend_signed_size64_t cursive_platform_signed_size64_t;
typedef cursive_rt_backend_uint_t cursive_platform_uint_t;
typedef cursive_rt_backend_bool_t cursive_platform_bool_t;
typedef cursive_rt_backend_module_t cursive_platform_module_t;

typedef struct cursive_rt_mutex_t {
  volatile uint32_t state;
} cursive_rt_mutex_t;
typedef cursive_rt_mutex_t cursive_platform_mutex_t;

typedef struct cursive_rt_condition_t {
  volatile uint32_t sequence;
} cursive_rt_condition_t;
typedef cursive_rt_condition_t cursive_platform_condition_t;

typedef struct cursive_rt_rwlock_t {
  cursive_rt_mutex_t mutex;
  cursive_rt_condition_t condition;
  volatile uint32_t readers;
  volatile uint32_t writer;
  volatile uint32_t waiting_writers;
} cursive_rt_rwlock_t;
typedef cursive_rt_rwlock_t cursive_platform_rwlock_t;

typedef struct cursive_rt_once_t {
  volatile cursive_rt_backend_i32_t state;
  void* context;
} cursive_rt_once_t;
typedef cursive_rt_once_t cursive_platform_once_t;

typedef cursive_rt_backend_bool_t (*cursive_rt_once_callback_t)(
    cursive_rt_once_t* once_state,
    void* parameter,
    void** context);
typedef cursive_rt_once_callback_t cursive_platform_once_callback_t;
typedef cursive_rt_backend_u32_t cursive_rt_tls_key_t;
typedef cursive_rt_backend_u32_t cursive_rt_thread_id_t;
typedef cursive_rt_backend_u32_t (*cursive_rt_thread_start_routine_t)(
    void* parameter);
typedef cursive_rt_tls_key_t cursive_platform_tls_key_t;
typedef cursive_rt_thread_id_t cursive_platform_thread_id_t;
typedef cursive_rt_thread_start_routine_t cursive_platform_thread_start_routine_t;

#define CURSIVE_RT_BACKEND_ONCE_INIT { 0, NULL }
#define CURSIVE_RT_BACKEND_RWLOCK_INIT { { 0u }, { 0u }, 0u, 0u, 0u }
#define CURSIVE_RT_BACKEND_TLS_KEY_INVALID ((cursive_rt_backend_u32_t)0xFFFFFFFFu)
#define CURSIVE_RT_BACKEND_INVALID_HANDLE \
  ((cursive_rt_backend_handle_t)(intptr_t)-1)
#define CURSIVE_RT_BACKEND_TRUE 1
#define CURSIVE_RT_BACKEND_FALSE 0
#define CURSIVE_RT_BACKEND_WAIT_FOREVER 0xFFFFFFFFu
#define CURSIVE_RT_BACKEND_WAIT_SIGNALED 0u
#define CURSIVE_RT_BACKEND_WAIT_ERROR 0xFFFFFFFFu
#define CURSIVE_RT_BACKEND_WAIT_TIMED_OUT 258u
#define CURSIVE_RT_BACKEND_PROCESS_EXIT_RUNNING 259u
#define CURSIVE_RT_BACKEND_STD_STREAM_INPUT ((cursive_rt_backend_u32_t)-10)
#define CURSIVE_RT_BACKEND_STD_STREAM_OUTPUT ((cursive_rt_backend_u32_t)-11)
#define CURSIVE_RT_BACKEND_STD_STREAM_ERROR ((cursive_rt_backend_u32_t)-12)
#define CURSIVE_RT_BACKEND_FILE_ATTRIBUTES_INVALID 0xFFFFFFFFu
#define CURSIVE_RT_BACKEND_PAGE_ACCESS_READ_WRITE 0x00000004u
#define CURSIVE_RT_BACKEND_MAP_ACCESS_ALL 0x000F001Fu
#define CURSIVE_RT_BACKEND_THREAD_PRIORITY_LOW (-1)
#define CURSIVE_RT_BACKEND_THREAD_PRIORITY_NORMAL 0
#define CURSIVE_RT_BACKEND_THREAD_PRIORITY_HIGH 1
#define CURSIVE_RT_BACKEND_THREAD_PRIORITY_INVALID 0x7FFFFFFF
#define CURSIVE_RT_BACKEND_FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define CURSIVE_RT_BACKEND_FILE_SHARE_READ 0x00000001u
#define CURSIVE_RT_BACKEND_FILE_SHARE_WRITE 0x00000002u
#define CURSIVE_RT_BACKEND_FILE_SHARE_DELETE 0x00000004u
#define CURSIVE_RT_BACKEND_FILE_OPEN_CREATE_NEW 1u
#define CURSIVE_RT_BACKEND_FILE_OPEN_REPLACE_ALWAYS 2u
#define CURSIVE_RT_BACKEND_FILE_OPEN_EXISTING 3u
#define CURSIVE_RT_BACKEND_FILE_OPEN_OR_CREATE 4u
#define CURSIVE_RT_BACKEND_FILE_OPEN_TRUNCATE_EXISTING 5u
#define CURSIVE_RT_BACKEND_FILE_ATTRIBUTE_NORMAL 0x00000080u
#define CURSIVE_RT_BACKEND_FILE_ACCESS_READ 0x80000000u
#define CURSIVE_RT_BACKEND_FILE_ACCESS_WRITE 0x40000000u
#define CURSIVE_RT_BACKEND_FILE_ACCESS_APPEND 0x00000004u
#define CURSIVE_RT_BACKEND_FILE_SEEK_START 0u
#define CURSIVE_RT_BACKEND_FILE_SEEK_CURRENT 1u
#define CURSIVE_RT_BACKEND_FILE_SEEK_END 2u
#define CURSIVE_RT_BACKEND_ERROR_SUCCESS 0u
#define CURSIVE_RT_BACKEND_ERROR_FILE_NOT_FOUND 2u
#define CURSIVE_RT_BACKEND_ERROR_PATH_NOT_FOUND 3u
#define CURSIVE_RT_BACKEND_ERROR_ACCESS_DENIED 5u
#define CURSIVE_RT_BACKEND_ERROR_INVALID_HANDLE 6u
#define CURSIVE_RT_BACKEND_ERROR_NOT_ENOUGH_MEMORY 8u
#define CURSIVE_RT_BACKEND_ERROR_INVALID_DRIVE 15u
#define CURSIVE_RT_BACKEND_ERROR_NO_MORE_FILES 18u
#define CURSIVE_RT_BACKEND_ERROR_WRITE_PROTECT 19u
#define CURSIVE_RT_BACKEND_ERROR_BAD_PATHNAME 161u
#define CURSIVE_RT_BACKEND_ERROR_ALREADY_EXISTS 183u
#define CURSIVE_RT_BACKEND_ERROR_ENVVAR_NOT_FOUND 203u
#define CURSIVE_RT_BACKEND_ERROR_FILENAME_EXCED_RANGE 206u
#define CURSIVE_RT_BACKEND_ERROR_DIRECTORY 267u
#define CURSIVE_RT_BACKEND_ERROR_PRIVILEGE_NOT_HELD 1314u
#define CURSIVE_RT_BACKEND_ERROR_FILE_EXISTS 80u
#define CURSIVE_RT_BACKEND_ERROR_BUSY 170u
#define CURSIVE_RT_BACKEND_ERROR_SHARING_VIOLATION 32u
#define CURSIVE_RT_BACKEND_ERROR_LOCK_VIOLATION 33u
#define CURSIVE_RT_BACKEND_ERROR_PIPE_BUSY 231u
#define CURSIVE_RT_BACKEND_ERROR_INVALID_NAME 123u
#define CURSIVE_RT_BACKEND_ERROR_INVALID_PARAMETER 87u
#define CURSIVE_RT_BACKEND_ERROR_INSUFFICIENT_BUFFER 122u

#define CURSIVE_PLATFORM_BACKEND_ONCE_INIT CURSIVE_RT_BACKEND_ONCE_INIT
#define CURSIVE_PLATFORM_BACKEND_RWLOCK_INIT CURSIVE_RT_BACKEND_RWLOCK_INIT
#define CURSIVE_PLATFORM_BACKEND_TLS_KEY_INVALID CURSIVE_RT_BACKEND_TLS_KEY_INVALID
#define CURSIVE_PLATFORM_BACKEND_INVALID_HANDLE \
  CURSIVE_RT_BACKEND_INVALID_HANDLE
#define CURSIVE_PLATFORM_BACKEND_TRUE CURSIVE_RT_BACKEND_TRUE
#define CURSIVE_PLATFORM_BACKEND_FALSE CURSIVE_RT_BACKEND_FALSE
#define CURSIVE_PLATFORM_BACKEND_INFINITE CURSIVE_RT_BACKEND_WAIT_FOREVER
#define CURSIVE_PLATFORM_BACKEND_WAIT_OBJECT_0 CURSIVE_RT_BACKEND_WAIT_SIGNALED
#define CURSIVE_PLATFORM_BACKEND_WAIT_FAILED CURSIVE_RT_BACKEND_WAIT_ERROR
#define CURSIVE_PLATFORM_BACKEND_WAIT_TIMEOUT CURSIVE_RT_BACKEND_WAIT_TIMED_OUT
#define CURSIVE_PLATFORM_BACKEND_STILL_ACTIVE CURSIVE_RT_BACKEND_PROCESS_EXIT_RUNNING
#define CURSIVE_PLATFORM_BACKEND_STD_INPUT_HANDLE CURSIVE_RT_BACKEND_STD_STREAM_INPUT
#define CURSIVE_PLATFORM_BACKEND_STD_OUTPUT_HANDLE CURSIVE_RT_BACKEND_STD_STREAM_OUTPUT
#define CURSIVE_PLATFORM_BACKEND_STD_ERROR_HANDLE CURSIVE_RT_BACKEND_STD_STREAM_ERROR
#define CURSIVE_PLATFORM_BACKEND_INVALID_FILE_ATTRIBUTES \
  CURSIVE_RT_BACKEND_FILE_ATTRIBUTES_INVALID
#define CURSIVE_PLATFORM_BACKEND_PAGE_READWRITE \
  CURSIVE_RT_BACKEND_PAGE_ACCESS_READ_WRITE
#define CURSIVE_PLATFORM_BACKEND_FILE_MAP_ALL_ACCESS \
  CURSIVE_RT_BACKEND_MAP_ACCESS_ALL
#define CURSIVE_PLATFORM_BACKEND_THREAD_PRIORITY_BELOW_NORMAL (-1)
#define CURSIVE_PLATFORM_BACKEND_THREAD_PRIORITY_NORMAL \
  CURSIVE_RT_BACKEND_THREAD_PRIORITY_NORMAL
#define CURSIVE_PLATFORM_BACKEND_THREAD_PRIORITY_ABOVE_NORMAL \
  CURSIVE_RT_BACKEND_THREAD_PRIORITY_HIGH
#define CURSIVE_PLATFORM_BACKEND_THREAD_PRIORITY_ERROR_RETURN \
  CURSIVE_RT_BACKEND_THREAD_PRIORITY_INVALID
#define CURSIVE_PLATFORM_BACKEND_FILE_ATTRIBUTE_DIRECTORY \
  CURSIVE_RT_BACKEND_FILE_ATTRIBUTE_DIRECTORY
#define CURSIVE_PLATFORM_BACKEND_FILE_SHARE_READ CURSIVE_RT_BACKEND_FILE_SHARE_READ
#define CURSIVE_PLATFORM_BACKEND_FILE_SHARE_WRITE \
  CURSIVE_RT_BACKEND_FILE_SHARE_WRITE
#define CURSIVE_PLATFORM_BACKEND_FILE_SHARE_DELETE \
  CURSIVE_RT_BACKEND_FILE_SHARE_DELETE
#define CURSIVE_PLATFORM_BACKEND_CREATE_NEW CURSIVE_RT_BACKEND_FILE_OPEN_CREATE_NEW
#define CURSIVE_PLATFORM_BACKEND_CREATE_ALWAYS \
  CURSIVE_RT_BACKEND_FILE_OPEN_REPLACE_ALWAYS
#define CURSIVE_PLATFORM_BACKEND_OPEN_EXISTING \
  CURSIVE_RT_BACKEND_FILE_OPEN_EXISTING
#define CURSIVE_PLATFORM_BACKEND_OPEN_ALWAYS CURSIVE_RT_BACKEND_FILE_OPEN_OR_CREATE
#define CURSIVE_PLATFORM_BACKEND_TRUNCATE_EXISTING \
  CURSIVE_RT_BACKEND_FILE_OPEN_TRUNCATE_EXISTING
#define CURSIVE_PLATFORM_BACKEND_FILE_ATTRIBUTE_NORMAL \
  CURSIVE_RT_BACKEND_FILE_ATTRIBUTE_NORMAL
#define CURSIVE_PLATFORM_BACKEND_GENERIC_READ CURSIVE_RT_BACKEND_FILE_ACCESS_READ
#define CURSIVE_PLATFORM_BACKEND_GENERIC_WRITE CURSIVE_RT_BACKEND_FILE_ACCESS_WRITE
#define CURSIVE_PLATFORM_BACKEND_FILE_APPEND_DATA \
  CURSIVE_RT_BACKEND_FILE_ACCESS_APPEND
#define CURSIVE_PLATFORM_BACKEND_FILE_BEGIN CURSIVE_RT_BACKEND_FILE_SEEK_START
#define CURSIVE_PLATFORM_BACKEND_FILE_CURRENT CURSIVE_RT_BACKEND_FILE_SEEK_CURRENT
#define CURSIVE_PLATFORM_BACKEND_FILE_END CURSIVE_RT_BACKEND_FILE_SEEK_END
#define CURSIVE_PLATFORM_BACKEND_ERROR_SUCCESS CURSIVE_RT_BACKEND_ERROR_SUCCESS
#define CURSIVE_PLATFORM_BACKEND_ERROR_FILE_NOT_FOUND \
  CURSIVE_RT_BACKEND_ERROR_FILE_NOT_FOUND
#define CURSIVE_PLATFORM_BACKEND_ERROR_PATH_NOT_FOUND \
  CURSIVE_RT_BACKEND_ERROR_PATH_NOT_FOUND
#define CURSIVE_PLATFORM_BACKEND_ERROR_ACCESS_DENIED \
  CURSIVE_RT_BACKEND_ERROR_ACCESS_DENIED
#define CURSIVE_PLATFORM_BACKEND_ERROR_INVALID_HANDLE \
  CURSIVE_RT_BACKEND_ERROR_INVALID_HANDLE
#define CURSIVE_PLATFORM_BACKEND_ERROR_NOT_ENOUGH_MEMORY \
  CURSIVE_RT_BACKEND_ERROR_NOT_ENOUGH_MEMORY
#define CURSIVE_PLATFORM_BACKEND_ERROR_INVALID_DRIVE \
  CURSIVE_RT_BACKEND_ERROR_INVALID_DRIVE
#define CURSIVE_PLATFORM_BACKEND_ERROR_NO_MORE_FILES \
  CURSIVE_RT_BACKEND_ERROR_NO_MORE_FILES
#define CURSIVE_PLATFORM_BACKEND_ERROR_WRITE_PROTECT \
  CURSIVE_RT_BACKEND_ERROR_WRITE_PROTECT
#define CURSIVE_PLATFORM_BACKEND_ERROR_BAD_PATHNAME \
  CURSIVE_RT_BACKEND_ERROR_BAD_PATHNAME
#define CURSIVE_PLATFORM_BACKEND_ERROR_ALREADY_EXISTS \
  CURSIVE_RT_BACKEND_ERROR_ALREADY_EXISTS
#define CURSIVE_PLATFORM_BACKEND_ERROR_ENVVAR_NOT_FOUND \
  CURSIVE_RT_BACKEND_ERROR_ENVVAR_NOT_FOUND
#define CURSIVE_PLATFORM_BACKEND_ERROR_FILENAME_EXCED_RANGE \
  CURSIVE_RT_BACKEND_ERROR_FILENAME_EXCED_RANGE
#define CURSIVE_PLATFORM_BACKEND_ERROR_DIRECTORY CURSIVE_RT_BACKEND_ERROR_DIRECTORY
#define CURSIVE_PLATFORM_BACKEND_ERROR_PRIVILEGE_NOT_HELD \
  CURSIVE_RT_BACKEND_ERROR_PRIVILEGE_NOT_HELD
#define CURSIVE_PLATFORM_BACKEND_ERROR_FILE_EXISTS CURSIVE_RT_BACKEND_ERROR_FILE_EXISTS
#define CURSIVE_PLATFORM_BACKEND_ERROR_BUSY CURSIVE_RT_BACKEND_ERROR_BUSY
#define CURSIVE_PLATFORM_BACKEND_ERROR_SHARING_VIOLATION \
  CURSIVE_RT_BACKEND_ERROR_SHARING_VIOLATION
#define CURSIVE_PLATFORM_BACKEND_ERROR_LOCK_VIOLATION \
  CURSIVE_RT_BACKEND_ERROR_LOCK_VIOLATION
#define CURSIVE_PLATFORM_BACKEND_ERROR_PIPE_BUSY CURSIVE_RT_BACKEND_ERROR_PIPE_BUSY
#define CURSIVE_PLATFORM_BACKEND_ERROR_INVALID_NAME \
  CURSIVE_RT_BACKEND_ERROR_INVALID_NAME
#define CURSIVE_PLATFORM_BACKEND_ERROR_INVALID_PARAMETER \
  CURSIVE_RT_BACKEND_ERROR_INVALID_PARAMETER
#define CURSIVE_PLATFORM_BACKEND_ERROR_INSUFFICIENT_BUFFER \
  CURSIVE_RT_BACKEND_ERROR_INSUFFICIENT_BUFFER

cursive_platform_u32_t cursive_platform_backend_error_get(void);
void cursive_platform_backend_error_set(cursive_platform_u32_t error_code);
cursive_platform_handle_t cursive_platform_backend_heap_handle(void);
void* cursive_platform_backend_heap_alloc(cursive_platform_handle_t heap,
                                          cursive_platform_u32_t flags,
                                          size_t bytes);
cursive_platform_bool_t cursive_platform_backend_heap_free(
    cursive_platform_handle_t heap,
    cursive_platform_u32_t flags,
    void* memory);
cursive_platform_bool_t cursive_platform_backend_heap_validate(
    cursive_platform_handle_t heap,
    cursive_platform_u32_t flags,
    const void* memory);
void cursive_platform_backend_exit_process(cursive_platform_uint_t exit_code);
cursive_platform_u32_t cursive_platform_backend_env_get_wide(
    const wchar_t* name,
    wchar_t* buffer,
    cursive_platform_u32_t size);
cursive_platform_u32_t cursive_platform_backend_env_get_utf8(
    const char* name,
    char* buffer,
    cursive_platform_u32_t size);
cursive_platform_u32_t cursive_platform_backend_executable_path_utf8(
    char* buffer,
    cursive_platform_u32_t size);
cursive_platform_uptr_t cursive_platform_backend_argument_count(void);
cursive_platform_u32_t cursive_platform_backend_argument_utf8(
    cursive_platform_uptr_t index,
    char* buffer,
    cursive_platform_u32_t size);
cursive_platform_u32_t cursive_platform_backend_current_directory_utf8(
    char* buffer,
    cursive_platform_u32_t size);
void cursive_platform_backend_icu_data_configure(void);
int cursive_platform_backend_utf8_to_wide_chars(const char* source,
                                                int source_length,
                                                wchar_t* destination,
                                                int destination_length);
int cursive_platform_backend_wide_to_utf8_chars(const wchar_t* source,
                                                int source_length,
                                                char* destination,
                                                int destination_length);
cursive_platform_bool_t cursive_platform_backend_process_create(
    const wchar_t* application_name,
    wchar_t* command_line,
    void* process_attributes,
    void* thread_attributes,
    cursive_platform_bool_t inherit_handles,
    cursive_platform_u32_t creation_flags,
    void* environment,
    const wchar_t* current_directory,
    cursive_platform_process_startup_t* startup_info,
    cursive_platform_process_info_t* process_information);
cursive_platform_u32_t cursive_platform_backend_wait_one(
    cursive_platform_handle_t handle,
    cursive_platform_u32_t milliseconds);
cursive_platform_bool_t cursive_platform_backend_process_exit_code(
    cursive_platform_handle_t handle,
    cursive_platform_u32_t* exit_code);
cursive_platform_bool_t cursive_platform_backend_close_handle(
    cursive_platform_handle_t handle);
cursive_platform_module_t cursive_platform_backend_module_get(
    const char* name);
cursive_platform_module_t cursive_platform_backend_module_load(
    const char* name);
void* cursive_platform_backend_module_symbol(cursive_platform_module_t module,
                                             const char* symbol_name);
cursive_platform_bool_t cursive_platform_backend_once_execute(
    cursive_platform_once_t* init_once,
    cursive_platform_once_callback_t init_fn,
    void* parameter,
    void** context);
cursive_platform_tls_key_t cursive_platform_backend_tls_key_create(void);
void* cursive_platform_backend_tls_get(cursive_platform_tls_key_t index);
cursive_platform_bool_t cursive_platform_backend_tls_set(
    cursive_platform_tls_key_t index,
    void* value);
void cursive_platform_backend_mutex_init(cursive_platform_mutex_t* mutex);
void cursive_platform_backend_mutex_destroy(cursive_platform_mutex_t* mutex);
void cursive_platform_backend_mutex_lock(cursive_platform_mutex_t* mutex);
void cursive_platform_backend_mutex_unlock(cursive_platform_mutex_t* mutex);
void cursive_platform_backend_condition_init(
    cursive_platform_condition_t* condition);
cursive_platform_bool_t cursive_platform_backend_condition_wait(
    cursive_platform_condition_t* condition,
    cursive_platform_mutex_t* mutex,
    cursive_platform_u32_t milliseconds);
void cursive_platform_backend_condition_wake_one(
    cursive_platform_condition_t* condition);
void cursive_platform_backend_condition_wake_all(
    cursive_platform_condition_t* condition);
void cursive_platform_backend_rwlock_init(cursive_platform_rwlock_t* lock);
void cursive_platform_backend_rwlock_lock_exclusive(
    cursive_platform_rwlock_t* lock);
void cursive_platform_backend_rwlock_unlock_exclusive(
    cursive_platform_rwlock_t* lock);
void cursive_platform_backend_rwlock_lock_shared(cursive_platform_rwlock_t* lock);
void cursive_platform_backend_rwlock_unlock_shared(
    cursive_platform_rwlock_t* lock);
cursive_platform_i32_t cursive_platform_backend_atomic_exchange(
    volatile cursive_platform_i32_t* target,
    cursive_platform_i32_t value);
cursive_platform_i32_t cursive_platform_backend_atomic_compare_exchange(
    volatile cursive_platform_i32_t* target,
    cursive_platform_i32_t exchange,
    cursive_platform_i32_t comparand);
cursive_platform_i64_t cursive_platform_backend_atomic_increment64(
    volatile cursive_platform_i64_t* target);
cursive_platform_thread_id_t cursive_platform_backend_current_thread_id(void);
cursive_platform_u32_t cursive_platform_backend_current_process_id(void);
cursive_platform_handle_t cursive_platform_backend_current_thread(void);
int cursive_platform_backend_thread_priority_get(
    cursive_platform_handle_t thread);
cursive_platform_bool_t cursive_platform_backend_thread_priority_set(
    cursive_platform_handle_t thread,
    int priority);
cursive_platform_uptr_t cursive_platform_backend_thread_affinity_set(
    cursive_platform_handle_t thread,
    cursive_platform_uptr_t mask);
cursive_platform_handle_t cursive_platform_backend_thread_create(
    void* attributes,
    size_t stack_size,
    cursive_platform_thread_start_routine_t start_routine,
    void* parameter,
    cursive_platform_u32_t creation_flags,
    cursive_platform_u32_t* thread_id);
cursive_platform_bool_t cursive_platform_backend_thread_exit_code(
    cursive_platform_handle_t handle,
    cursive_platform_u32_t* exit_code);
cursive_platform_handle_t cursive_platform_backend_event_create(
    void* attributes,
    cursive_platform_bool_t manual_reset,
    cursive_platform_bool_t initial_state,
    const wchar_t* name);
cursive_platform_bool_t cursive_platform_backend_event_set(
    cursive_platform_handle_t handle);
cursive_platform_bool_t cursive_platform_backend_event_reset(
    cursive_platform_handle_t handle);
cursive_platform_handle_t cursive_platform_backend_std_handle(
    cursive_platform_u32_t std_handle_id);
cursive_platform_bool_t cursive_platform_backend_std_handle_set(
    cursive_platform_u32_t std_handle_id,
    cursive_platform_handle_t handle);
cursive_platform_bool_t cursive_platform_backend_console_mode_get(
    cursive_platform_handle_t handle,
    cursive_platform_u32_t* mode);
cursive_platform_bool_t cursive_platform_backend_console_write_utf8(
    cursive_platform_handle_t handle,
    const void* buffer,
    cursive_platform_u32_t chars_to_write,
    cursive_platform_u32_t* chars_written);
cursive_platform_bool_t cursive_platform_backend_handle_write(
    cursive_platform_handle_t handle,
    const void* buffer,
    cursive_platform_u32_t bytes_to_write,
    cursive_platform_u32_t* bytes_written);
cursive_platform_bool_t cursive_platform_backend_handle_read(
    cursive_platform_handle_t handle,
    void* buffer,
    cursive_platform_u32_t bytes_to_read,
    cursive_platform_u32_t* bytes_read);
cursive_platform_handle_t cursive_platform_backend_file_open_wide(
    const wchar_t* path,
    cursive_platform_u32_t desired_access,
    cursive_platform_u32_t share_mode,
    void* security_attributes,
    cursive_platform_u32_t creation_disposition,
    cursive_platform_u32_t flags_and_attributes,
    cursive_platform_handle_t template_file);
cursive_platform_bool_t cursive_platform_backend_handle_flush(
    cursive_platform_handle_t handle);
cursive_platform_bool_t cursive_platform_backend_file_size_get(
    cursive_platform_handle_t handle,
    cursive_platform_large_integer_t* size_out);
cursive_platform_bool_t cursive_platform_backend_file_pointer_set(
    cursive_platform_handle_t handle,
    cursive_platform_large_integer_t distance,
    cursive_platform_large_integer_t* new_position,
    cursive_platform_u32_t move_method);
cursive_platform_u32_t cursive_platform_backend_file_attributes_get_wide(
    const wchar_t* path);
cursive_platform_bool_t cursive_platform_backend_file_delete_wide(
    const wchar_t* path);
cursive_platform_bool_t cursive_platform_backend_directory_remove_wide(
    const wchar_t* path);
cursive_platform_bool_t cursive_platform_backend_directory_create_wide(
    const wchar_t* path,
    void* security_attributes);
cursive_platform_u32_t cursive_platform_backend_temp_path_get_wide(
    cursive_platform_u32_t buffer_length,
    wchar_t* buffer);
cursive_platform_uint_t cursive_platform_backend_temp_file_name_wide(
    const wchar_t* path_name,
    const wchar_t* prefix_string,
    cursive_platform_uint_t unique,
    wchar_t* temp_file_name);
cursive_platform_handle_t cursive_platform_backend_find_first_wide(
    const wchar_t* pattern,
    cursive_platform_find_data_t* find_data);
cursive_platform_bool_t cursive_platform_backend_find_next(
    cursive_platform_handle_t handle,
    cursive_platform_find_data_t* find_data);
cursive_platform_bool_t cursive_platform_backend_find_close(
    cursive_platform_handle_t handle);
cursive_platform_handle_t cursive_platform_backend_mapping_create(
    cursive_platform_handle_t file,
    void* attributes,
    cursive_platform_u32_t protect,
    cursive_platform_u32_t maximum_size_high,
    cursive_platform_u32_t maximum_size_low,
    const wchar_t* name);
void* cursive_platform_backend_mapping_view(
    cursive_platform_handle_t mapping,
    cursive_platform_u32_t desired_access,
    cursive_platform_u32_t file_offset_high,
    cursive_platform_u32_t file_offset_low,
    size_t number_of_bytes_to_map);
cursive_platform_bool_t cursive_platform_backend_mapping_unview(
    const void* base_address);
void cursive_platform_backend_debug_break(void);
void cursive_platform_backend_system_time_filetime(
    cursive_platform_filetime_t* file_time);
cursive_platform_bool_t cursive_platform_backend_panic_boundary_active(void);
cursive_platform_bool_t cursive_platform_backend_panic_boundary_run(
    cursive_rt_panic_boundary_body_t body,
    void* context,
    cursive_platform_u32_t* panic_code);
void cursive_platform_backend_panic_boundary_raise(
    cursive_platform_u32_t panic_code);

#ifdef __cplusplus
}
#endif

#endif  // CURSIVE_RT_PLATFORM_LINUX_H
