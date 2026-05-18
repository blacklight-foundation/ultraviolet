#ifndef UV_RT_PLATFORM_LINUX_H
#define UV_RT_PLATFORM_LINUX_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct uv_rt_process_launch_t uv_rt_process_launch_t;
typedef struct uv_rt_process_t uv_rt_process_t;
typedef struct uv_rt_file_offset_t uv_rt_file_offset_t;
typedef struct uv_rt_timestamp_t uv_rt_timestamp_t;
typedef struct uv_rt_directory_scan_entry_t
    uv_rt_directory_scan_entry_t;
typedef struct uv_rt_process_launch_t uv_platform_process_startup_t;
typedef struct uv_rt_process_t uv_platform_process_info_t;
typedef struct uv_rt_file_offset_t uv_platform_large_integer_t;
typedef struct uv_rt_timestamp_t uv_platform_filetime_t;
typedef struct uv_rt_directory_scan_entry_t uv_platform_find_data_t;

typedef struct uv_rt_backend_handle_impl* uv_rt_backend_handle_t;
typedef uint32_t uv_rt_backend_u32_t;
typedef uintptr_t uv_rt_backend_uptr_t;
typedef int32_t uv_rt_backend_i32_t;
typedef int64_t uv_rt_backend_i64_t;
typedef int64_t uv_rt_backend_signed_size64_t;
typedef unsigned int uv_rt_backend_uint_t;
typedef int uv_rt_backend_bool_t;
typedef void* uv_rt_backend_module_t;

typedef uv_rt_backend_handle_t uv_platform_handle_t;
typedef uv_rt_backend_u32_t uv_platform_u32_t;
typedef uv_rt_backend_uptr_t uv_platform_uptr_t;
typedef uv_rt_backend_i32_t uv_platform_i32_t;
typedef uv_rt_backend_i64_t uv_platform_i64_t;
typedef uv_rt_backend_signed_size64_t uv_platform_signed_size64_t;
typedef uv_rt_backend_uint_t uv_platform_uint_t;
typedef uv_rt_backend_bool_t uv_platform_bool_t;
typedef uv_rt_backend_module_t uv_platform_module_t;

typedef struct uv_rt_mutex_t {
  volatile uint32_t state;
} uv_rt_mutex_t;
typedef uv_rt_mutex_t uv_platform_mutex_t;

typedef struct uv_rt_condition_t {
  volatile uint32_t sequence;
} uv_rt_condition_t;
typedef uv_rt_condition_t uv_platform_condition_t;

typedef struct uv_rt_rwlock_t {
  uv_rt_mutex_t mutex;
  uv_rt_condition_t condition;
  volatile uint32_t readers;
  volatile uint32_t writer;
  volatile uint32_t waiting_writers;
} uv_rt_rwlock_t;
typedef uv_rt_rwlock_t uv_platform_rwlock_t;

typedef struct uv_rt_once_t {
  volatile uv_rt_backend_i32_t state;
  void* context;
} uv_rt_once_t;
typedef uv_rt_once_t uv_platform_once_t;

typedef uv_rt_backend_bool_t (*uv_rt_once_callback_t)(
    uv_rt_once_t* once_state,
    void* parameter,
    void** context);
typedef uv_rt_once_callback_t uv_platform_once_callback_t;
typedef uv_rt_backend_u32_t uv_rt_tls_key_t;
typedef uv_rt_backend_u32_t uv_rt_thread_id_t;
typedef uv_rt_backend_u32_t (*uv_rt_thread_start_routine_t)(
    void* parameter);
typedef uv_rt_tls_key_t uv_platform_tls_key_t;
typedef uv_rt_thread_id_t uv_platform_thread_id_t;
typedef uv_rt_thread_start_routine_t uv_platform_thread_start_routine_t;

#define UV_RT_BACKEND_ONCE_INIT { 0, NULL }
#define UV_RT_BACKEND_RWLOCK_INIT { { 0u }, { 0u }, 0u, 0u, 0u }
#define UV_RT_BACKEND_TLS_KEY_INVALID ((uv_rt_backend_u32_t)0xFFFFFFFFu)
#define UV_RT_BACKEND_INVALID_HANDLE \
  ((uv_rt_backend_handle_t)(intptr_t)-1)
#define UV_RT_BACKEND_TRUE 1
#define UV_RT_BACKEND_FALSE 0
#define UV_RT_BACKEND_WAIT_FOREVER 0xFFFFFFFFu
#define UV_RT_BACKEND_WAIT_SIGNALED 0u
#define UV_RT_BACKEND_WAIT_ERROR 0xFFFFFFFFu
#define UV_RT_BACKEND_WAIT_TIMED_OUT 258u
#define UV_RT_BACKEND_PROCESS_EXIT_RUNNING 259u
#define UV_RT_BACKEND_STD_STREAM_INPUT ((uv_rt_backend_u32_t)-10)
#define UV_RT_BACKEND_STD_STREAM_OUTPUT ((uv_rt_backend_u32_t)-11)
#define UV_RT_BACKEND_STD_STREAM_ERROR ((uv_rt_backend_u32_t)-12)
#define UV_RT_BACKEND_FILE_ATTRIBUTES_INVALID 0xFFFFFFFFu
#define UV_RT_BACKEND_PAGE_ACCESS_READ_WRITE 0x00000004u
#define UV_RT_BACKEND_MAP_ACCESS_ALL 0x000F001Fu
#define UV_RT_BACKEND_THREAD_PRIORITY_LOW (-1)
#define UV_RT_BACKEND_THREAD_PRIORITY_NORMAL 0
#define UV_RT_BACKEND_THREAD_PRIORITY_HIGH 1
#define UV_RT_BACKEND_THREAD_PRIORITY_INVALID 0x7FFFFFFF
#define UV_RT_BACKEND_FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define UV_RT_BACKEND_FILE_SHARE_READ 0x00000001u
#define UV_RT_BACKEND_FILE_SHARE_WRITE 0x00000002u
#define UV_RT_BACKEND_FILE_SHARE_DELETE 0x00000004u
#define UV_RT_BACKEND_FILE_OPEN_CREATE_NEW 1u
#define UV_RT_BACKEND_FILE_OPEN_REPLACE_ALWAYS 2u
#define UV_RT_BACKEND_FILE_OPEN_EXISTING 3u
#define UV_RT_BACKEND_FILE_OPEN_OR_CREATE 4u
#define UV_RT_BACKEND_FILE_OPEN_TRUNCATE_EXISTING 5u
#define UV_RT_BACKEND_FILE_ATTRIBUTE_NORMAL 0x00000080u
#define UV_RT_BACKEND_FILE_ACCESS_READ 0x80000000u
#define UV_RT_BACKEND_FILE_ACCESS_WRITE 0x40000000u
#define UV_RT_BACKEND_FILE_ACCESS_APPEND 0x00000004u
#define UV_RT_BACKEND_FILE_SEEK_START 0u
#define UV_RT_BACKEND_FILE_SEEK_CURRENT 1u
#define UV_RT_BACKEND_FILE_SEEK_END 2u
#define UV_RT_BACKEND_ERROR_SUCCESS 0u
#define UV_RT_BACKEND_ERROR_FILE_NOT_FOUND 2u
#define UV_RT_BACKEND_ERROR_PATH_NOT_FOUND 3u
#define UV_RT_BACKEND_ERROR_ACCESS_DENIED 5u
#define UV_RT_BACKEND_ERROR_INVALID_HANDLE 6u
#define UV_RT_BACKEND_ERROR_NOT_ENOUGH_MEMORY 8u
#define UV_RT_BACKEND_ERROR_INVALID_DRIVE 15u
#define UV_RT_BACKEND_ERROR_NO_MORE_FILES 18u
#define UV_RT_BACKEND_ERROR_WRITE_PROTECT 19u
#define UV_RT_BACKEND_ERROR_BAD_PATHNAME 161u
#define UV_RT_BACKEND_ERROR_ALREADY_EXISTS 183u
#define UV_RT_BACKEND_ERROR_ENVVAR_NOT_FOUND 203u
#define UV_RT_BACKEND_ERROR_FILENAME_EXCED_RANGE 206u
#define UV_RT_BACKEND_ERROR_DIRECTORY 267u
#define UV_RT_BACKEND_ERROR_PRIVILEGE_NOT_HELD 1314u
#define UV_RT_BACKEND_ERROR_FILE_EXISTS 80u
#define UV_RT_BACKEND_ERROR_BUSY 170u
#define UV_RT_BACKEND_ERROR_SHARING_VIOLATION 32u
#define UV_RT_BACKEND_ERROR_LOCK_VIOLATION 33u
#define UV_RT_BACKEND_ERROR_PIPE_BUSY 231u
#define UV_RT_BACKEND_ERROR_INVALID_NAME 123u
#define UV_RT_BACKEND_ERROR_INVALID_PARAMETER 87u
#define UV_RT_BACKEND_ERROR_INSUFFICIENT_BUFFER 122u

#define UV_PLATFORM_BACKEND_ONCE_INIT UV_RT_BACKEND_ONCE_INIT
#define UV_PLATFORM_BACKEND_RWLOCK_INIT UV_RT_BACKEND_RWLOCK_INIT
#define UV_PLATFORM_BACKEND_TLS_KEY_INVALID UV_RT_BACKEND_TLS_KEY_INVALID
#define UV_PLATFORM_BACKEND_INVALID_HANDLE \
  UV_RT_BACKEND_INVALID_HANDLE
#define UV_PLATFORM_BACKEND_TRUE UV_RT_BACKEND_TRUE
#define UV_PLATFORM_BACKEND_FALSE UV_RT_BACKEND_FALSE
#define UV_PLATFORM_BACKEND_INFINITE UV_RT_BACKEND_WAIT_FOREVER
#define UV_PLATFORM_BACKEND_WAIT_OBJECT_0 UV_RT_BACKEND_WAIT_SIGNALED
#define UV_PLATFORM_BACKEND_WAIT_FAILED UV_RT_BACKEND_WAIT_ERROR
#define UV_PLATFORM_BACKEND_WAIT_TIMEOUT UV_RT_BACKEND_WAIT_TIMED_OUT
#define UV_PLATFORM_BACKEND_STILL_ACTIVE UV_RT_BACKEND_PROCESS_EXIT_RUNNING
#define UV_PLATFORM_BACKEND_STD_INPUT_HANDLE UV_RT_BACKEND_STD_STREAM_INPUT
#define UV_PLATFORM_BACKEND_STD_OUTPUT_HANDLE UV_RT_BACKEND_STD_STREAM_OUTPUT
#define UV_PLATFORM_BACKEND_STD_ERROR_HANDLE UV_RT_BACKEND_STD_STREAM_ERROR
#define UV_PLATFORM_BACKEND_INVALID_FILE_ATTRIBUTES \
  UV_RT_BACKEND_FILE_ATTRIBUTES_INVALID
#define UV_PLATFORM_BACKEND_PAGE_READWRITE \
  UV_RT_BACKEND_PAGE_ACCESS_READ_WRITE
#define UV_PLATFORM_BACKEND_FILE_MAP_ALL_ACCESS \
  UV_RT_BACKEND_MAP_ACCESS_ALL
#define UV_PLATFORM_BACKEND_THREAD_PRIORITY_BELOW_NORMAL (-1)
#define UV_PLATFORM_BACKEND_THREAD_PRIORITY_NORMAL \
  UV_RT_BACKEND_THREAD_PRIORITY_NORMAL
#define UV_PLATFORM_BACKEND_THREAD_PRIORITY_ABOVE_NORMAL \
  UV_RT_BACKEND_THREAD_PRIORITY_HIGH
#define UV_PLATFORM_BACKEND_THREAD_PRIORITY_ERROR_RETURN \
  UV_RT_BACKEND_THREAD_PRIORITY_INVALID
#define UV_PLATFORM_BACKEND_FILE_ATTRIBUTE_DIRECTORY \
  UV_RT_BACKEND_FILE_ATTRIBUTE_DIRECTORY
#define UV_PLATFORM_BACKEND_FILE_SHARE_READ UV_RT_BACKEND_FILE_SHARE_READ
#define UV_PLATFORM_BACKEND_FILE_SHARE_WRITE \
  UV_RT_BACKEND_FILE_SHARE_WRITE
#define UV_PLATFORM_BACKEND_FILE_SHARE_DELETE \
  UV_RT_BACKEND_FILE_SHARE_DELETE
#define UV_PLATFORM_BACKEND_CREATE_NEW UV_RT_BACKEND_FILE_OPEN_CREATE_NEW
#define UV_PLATFORM_BACKEND_CREATE_ALWAYS \
  UV_RT_BACKEND_FILE_OPEN_REPLACE_ALWAYS
#define UV_PLATFORM_BACKEND_OPEN_EXISTING \
  UV_RT_BACKEND_FILE_OPEN_EXISTING
#define UV_PLATFORM_BACKEND_OPEN_ALWAYS UV_RT_BACKEND_FILE_OPEN_OR_CREATE
#define UV_PLATFORM_BACKEND_TRUNCATE_EXISTING \
  UV_RT_BACKEND_FILE_OPEN_TRUNCATE_EXISTING
#define UV_PLATFORM_BACKEND_FILE_ATTRIBUTE_NORMAL \
  UV_RT_BACKEND_FILE_ATTRIBUTE_NORMAL
#define UV_PLATFORM_BACKEND_GENERIC_READ UV_RT_BACKEND_FILE_ACCESS_READ
#define UV_PLATFORM_BACKEND_GENERIC_WRITE UV_RT_BACKEND_FILE_ACCESS_WRITE
#define UV_PLATFORM_BACKEND_FILE_APPEND_DATA \
  UV_RT_BACKEND_FILE_ACCESS_APPEND
#define UV_PLATFORM_BACKEND_FILE_BEGIN UV_RT_BACKEND_FILE_SEEK_START
#define UV_PLATFORM_BACKEND_FILE_CURRENT UV_RT_BACKEND_FILE_SEEK_CURRENT
#define UV_PLATFORM_BACKEND_FILE_END UV_RT_BACKEND_FILE_SEEK_END
#define UV_PLATFORM_BACKEND_ERROR_SUCCESS UV_RT_BACKEND_ERROR_SUCCESS
#define UV_PLATFORM_BACKEND_ERROR_FILE_NOT_FOUND \
  UV_RT_BACKEND_ERROR_FILE_NOT_FOUND
#define UV_PLATFORM_BACKEND_ERROR_PATH_NOT_FOUND \
  UV_RT_BACKEND_ERROR_PATH_NOT_FOUND
#define UV_PLATFORM_BACKEND_ERROR_ACCESS_DENIED \
  UV_RT_BACKEND_ERROR_ACCESS_DENIED
#define UV_PLATFORM_BACKEND_ERROR_INVALID_HANDLE \
  UV_RT_BACKEND_ERROR_INVALID_HANDLE
#define UV_PLATFORM_BACKEND_ERROR_NOT_ENOUGH_MEMORY \
  UV_RT_BACKEND_ERROR_NOT_ENOUGH_MEMORY
#define UV_PLATFORM_BACKEND_ERROR_INVALID_DRIVE \
  UV_RT_BACKEND_ERROR_INVALID_DRIVE
#define UV_PLATFORM_BACKEND_ERROR_NO_MORE_FILES \
  UV_RT_BACKEND_ERROR_NO_MORE_FILES
#define UV_PLATFORM_BACKEND_ERROR_WRITE_PROTECT \
  UV_RT_BACKEND_ERROR_WRITE_PROTECT
#define UV_PLATFORM_BACKEND_ERROR_BAD_PATHNAME \
  UV_RT_BACKEND_ERROR_BAD_PATHNAME
#define UV_PLATFORM_BACKEND_ERROR_ALREADY_EXISTS \
  UV_RT_BACKEND_ERROR_ALREADY_EXISTS
#define UV_PLATFORM_BACKEND_ERROR_ENVVAR_NOT_FOUND \
  UV_RT_BACKEND_ERROR_ENVVAR_NOT_FOUND
#define UV_PLATFORM_BACKEND_ERROR_FILENAME_EXCED_RANGE \
  UV_RT_BACKEND_ERROR_FILENAME_EXCED_RANGE
#define UV_PLATFORM_BACKEND_ERROR_DIRECTORY UV_RT_BACKEND_ERROR_DIRECTORY
#define UV_PLATFORM_BACKEND_ERROR_PRIVILEGE_NOT_HELD \
  UV_RT_BACKEND_ERROR_PRIVILEGE_NOT_HELD
#define UV_PLATFORM_BACKEND_ERROR_FILE_EXISTS UV_RT_BACKEND_ERROR_FILE_EXISTS
#define UV_PLATFORM_BACKEND_ERROR_BUSY UV_RT_BACKEND_ERROR_BUSY
#define UV_PLATFORM_BACKEND_ERROR_SHARING_VIOLATION \
  UV_RT_BACKEND_ERROR_SHARING_VIOLATION
#define UV_PLATFORM_BACKEND_ERROR_LOCK_VIOLATION \
  UV_RT_BACKEND_ERROR_LOCK_VIOLATION
#define UV_PLATFORM_BACKEND_ERROR_PIPE_BUSY UV_RT_BACKEND_ERROR_PIPE_BUSY
#define UV_PLATFORM_BACKEND_ERROR_INVALID_NAME \
  UV_RT_BACKEND_ERROR_INVALID_NAME
#define UV_PLATFORM_BACKEND_ERROR_INVALID_PARAMETER \
  UV_RT_BACKEND_ERROR_INVALID_PARAMETER
#define UV_PLATFORM_BACKEND_ERROR_INSUFFICIENT_BUFFER \
  UV_RT_BACKEND_ERROR_INSUFFICIENT_BUFFER

uv_platform_u32_t uv_platform_backend_error_get(void);
void uv_platform_backend_error_set(uv_platform_u32_t error_code);
uv_platform_handle_t uv_platform_backend_heap_handle(void);
void* uv_platform_backend_heap_alloc(uv_platform_handle_t heap,
                                          uv_platform_u32_t flags,
                                          size_t bytes);
uv_platform_bool_t uv_platform_backend_heap_free(
    uv_platform_handle_t heap,
    uv_platform_u32_t flags,
    void* memory);
uv_platform_bool_t uv_platform_backend_heap_validate(
    uv_platform_handle_t heap,
    uv_platform_u32_t flags,
    const void* memory);
void uv_platform_backend_exit_process(uv_platform_uint_t exit_code);
uv_platform_u32_t uv_platform_backend_env_get_wide(
    const wchar_t* name,
    wchar_t* buffer,
    uv_platform_u32_t size);
uv_platform_u32_t uv_platform_backend_env_get_utf8(
    const char* name,
    char* buffer,
    uv_platform_u32_t size);
uv_platform_u32_t uv_platform_backend_executable_path_utf8(
    char* buffer,
    uv_platform_u32_t size);
uv_platform_uptr_t uv_platform_backend_argument_count(void);
uv_platform_u32_t uv_platform_backend_argument_utf8(
    uv_platform_uptr_t index,
    char* buffer,
    uv_platform_u32_t size);
uv_platform_u32_t uv_platform_backend_current_directory_utf8(
    char* buffer,
    uv_platform_u32_t size);
void uv_platform_backend_icu_data_configure(void);
int uv_platform_backend_utf8_to_wide_chars(const char* source,
                                                int source_length,
                                                wchar_t* destination,
                                                int destination_length);
int uv_platform_backend_wide_to_utf8_chars(const wchar_t* source,
                                                int source_length,
                                                char* destination,
                                                int destination_length);
uv_platform_bool_t uv_platform_backend_process_create(
    const wchar_t* application_name,
    wchar_t* command_line,
    void* process_attributes,
    void* thread_attributes,
    uv_platform_bool_t inherit_handles,
    uv_platform_u32_t creation_flags,
    void* environment,
    const wchar_t* current_directory,
    uv_platform_process_startup_t* startup_info,
    uv_platform_process_info_t* process_information);
uv_platform_u32_t uv_platform_backend_wait_one(
    uv_platform_handle_t handle,
    uv_platform_u32_t milliseconds);
uv_platform_bool_t uv_platform_backend_process_exit_code(
    uv_platform_handle_t handle,
    uv_platform_u32_t* exit_code);
uv_platform_bool_t uv_platform_backend_close_handle(
    uv_platform_handle_t handle);
uv_platform_module_t uv_platform_backend_module_get(
    const char* name);
uv_platform_module_t uv_platform_backend_module_load(
    const char* name);
void* uv_platform_backend_module_symbol(uv_platform_module_t module,
                                             const char* symbol_name);
uv_platform_bool_t uv_platform_backend_once_execute(
    uv_platform_once_t* init_once,
    uv_platform_once_callback_t init_fn,
    void* parameter,
    void** context);
uv_platform_tls_key_t uv_platform_backend_tls_key_create(void);
void* uv_platform_backend_tls_get(uv_platform_tls_key_t index);
uv_platform_bool_t uv_platform_backend_tls_set(
    uv_platform_tls_key_t index,
    void* value);
void uv_platform_backend_mutex_init(uv_platform_mutex_t* mutex);
void uv_platform_backend_mutex_destroy(uv_platform_mutex_t* mutex);
void uv_platform_backend_mutex_lock(uv_platform_mutex_t* mutex);
void uv_platform_backend_mutex_unlock(uv_platform_mutex_t* mutex);
void uv_platform_backend_condition_init(
    uv_platform_condition_t* condition);
uv_platform_bool_t uv_platform_backend_condition_wait(
    uv_platform_condition_t* condition,
    uv_platform_mutex_t* mutex,
    uv_platform_u32_t milliseconds);
void uv_platform_backend_condition_wake_one(
    uv_platform_condition_t* condition);
void uv_platform_backend_condition_wake_all(
    uv_platform_condition_t* condition);
void uv_platform_backend_rwlock_init(uv_platform_rwlock_t* lock);
void uv_platform_backend_rwlock_lock_exclusive(
    uv_platform_rwlock_t* lock);
void uv_platform_backend_rwlock_unlock_exclusive(
    uv_platform_rwlock_t* lock);
void uv_platform_backend_rwlock_lock_shared(uv_platform_rwlock_t* lock);
void uv_platform_backend_rwlock_unlock_shared(
    uv_platform_rwlock_t* lock);
uv_platform_i32_t uv_platform_backend_atomic_exchange(
    volatile uv_platform_i32_t* target,
    uv_platform_i32_t value);
uv_platform_i32_t uv_platform_backend_atomic_compare_exchange(
    volatile uv_platform_i32_t* target,
    uv_platform_i32_t exchange,
    uv_platform_i32_t comparand);
uv_platform_i64_t uv_platform_backend_atomic_increment64(
    volatile uv_platform_i64_t* target);
uv_platform_thread_id_t uv_platform_backend_current_thread_id(void);
uv_platform_u32_t uv_platform_backend_current_process_id(void);
uv_platform_handle_t uv_platform_backend_current_thread(void);
int uv_platform_backend_thread_priority_get(
    uv_platform_handle_t thread);
uv_platform_bool_t uv_platform_backend_thread_priority_set(
    uv_platform_handle_t thread,
    int priority);
uv_platform_uptr_t uv_platform_backend_thread_affinity_set(
    uv_platform_handle_t thread,
    uv_platform_uptr_t mask);
uv_platform_handle_t uv_platform_backend_thread_create(
    void* attributes,
    size_t stack_size,
    uv_platform_thread_start_routine_t start_routine,
    void* parameter,
    uv_platform_u32_t creation_flags,
    uv_platform_u32_t* thread_id);
uv_platform_bool_t uv_platform_backend_thread_exit_code(
    uv_platform_handle_t handle,
    uv_platform_u32_t* exit_code);
uv_platform_handle_t uv_platform_backend_event_create(
    void* attributes,
    uv_platform_bool_t manual_reset,
    uv_platform_bool_t initial_state,
    const wchar_t* name);
uv_platform_bool_t uv_platform_backend_event_set(
    uv_platform_handle_t handle);
uv_platform_bool_t uv_platform_backend_event_reset(
    uv_platform_handle_t handle);
uv_platform_handle_t uv_platform_backend_std_handle(
    uv_platform_u32_t std_handle_id);
uv_platform_bool_t uv_platform_backend_std_handle_set(
    uv_platform_u32_t std_handle_id,
    uv_platform_handle_t handle);
uv_platform_bool_t uv_platform_backend_console_mode_get(
    uv_platform_handle_t handle,
    uv_platform_u32_t* mode);
uv_platform_bool_t uv_platform_backend_console_write_utf8(
    uv_platform_handle_t handle,
    const void* buffer,
    uv_platform_u32_t chars_to_write,
    uv_platform_u32_t* chars_written);
uv_platform_bool_t uv_platform_backend_handle_write(
    uv_platform_handle_t handle,
    const void* buffer,
    uv_platform_u32_t bytes_to_write,
    uv_platform_u32_t* bytes_written);
uv_platform_bool_t uv_platform_backend_handle_read(
    uv_platform_handle_t handle,
    void* buffer,
    uv_platform_u32_t bytes_to_read,
    uv_platform_u32_t* bytes_read);
uv_platform_handle_t uv_platform_backend_file_open_wide(
    const wchar_t* path,
    uv_platform_u32_t desired_access,
    uv_platform_u32_t share_mode,
    void* security_attributes,
    uv_platform_u32_t creation_disposition,
    uv_platform_u32_t flags_and_attributes,
    uv_platform_handle_t template_file);
uv_platform_bool_t uv_platform_backend_handle_flush(
    uv_platform_handle_t handle);
uv_platform_bool_t uv_platform_backend_file_size_get(
    uv_platform_handle_t handle,
    uv_platform_large_integer_t* size_out);
uv_platform_bool_t uv_platform_backend_file_pointer_set(
    uv_platform_handle_t handle,
    uv_platform_large_integer_t distance,
    uv_platform_large_integer_t* new_position,
    uv_platform_u32_t move_method);
uv_platform_u32_t uv_platform_backend_file_attributes_get_wide(
    const wchar_t* path);
uv_platform_bool_t uv_platform_backend_file_delete_wide(
    const wchar_t* path);
uv_platform_bool_t uv_platform_backend_directory_remove_wide(
    const wchar_t* path);
uv_platform_bool_t uv_platform_backend_directory_create_wide(
    const wchar_t* path,
    void* security_attributes);
uv_platform_u32_t uv_platform_backend_temp_path_get_wide(
    uv_platform_u32_t buffer_length,
    wchar_t* buffer);
uv_platform_uint_t uv_platform_backend_temp_file_name_wide(
    const wchar_t* path_name,
    const wchar_t* prefix_string,
    uv_platform_uint_t unique,
    wchar_t* temp_file_name);
uv_platform_handle_t uv_platform_backend_find_first_wide(
    const wchar_t* pattern,
    uv_platform_find_data_t* find_data);
uv_platform_bool_t uv_platform_backend_find_next(
    uv_platform_handle_t handle,
    uv_platform_find_data_t* find_data);
uv_platform_bool_t uv_platform_backend_find_close(
    uv_platform_handle_t handle);
uv_platform_handle_t uv_platform_backend_mapping_create(
    uv_platform_handle_t file,
    void* attributes,
    uv_platform_u32_t protect,
    uv_platform_u32_t maximum_size_high,
    uv_platform_u32_t maximum_size_low,
    const wchar_t* name);
void* uv_platform_backend_mapping_view(
    uv_platform_handle_t mapping,
    uv_platform_u32_t desired_access,
    uv_platform_u32_t file_offset_high,
    uv_platform_u32_t file_offset_low,
    size_t number_of_bytes_to_map);
uv_platform_bool_t uv_platform_backend_mapping_unview(
    const void* base_address);
void uv_platform_backend_debug_break(void);
void uv_platform_backend_system_time_filetime(
    uv_platform_filetime_t* file_time);
uv_platform_bool_t uv_platform_backend_panic_boundary_active(void);
uv_platform_bool_t uv_platform_backend_panic_boundary_run(
    uv_rt_panic_boundary_body_t body,
    void* context,
    uv_platform_u32_t* panic_code);
void uv_platform_backend_panic_boundary_raise(
    uv_platform_u32_t panic_code);

#ifdef __cplusplus
}
#endif

#endif  // UV_RT_PLATFORM_LINUX_H
