#ifndef UV_RT_PLATFORM_H
#define UV_RT_PLATFORM_H

#include "rt_platform_types.h"

#if defined(UV_RT_PLATFORM_WINDOWS)
#include "../platform/windows/rt_platform_windows_api.h"
#endif

/* Neutral runtime surface. The older uv_platform_* names remain as
 * compatibility aliases for untouched runtime code. */
#define uv_rt_error_get uv_rt_last_error_get
#define uv_rt_error_set uv_rt_last_error_set
#define uv_rt_env_query_wide uv_rt_env_get_wide
#define uv_rt_env_query_utf8 uv_rt_env_get_utf8
#define uv_rt_executable_path_query_utf8 uv_rt_executable_path_utf8
#define uv_rt_argument_count_query uv_rt_argument_count
#define uv_rt_argument_query_utf8 uv_rt_argument_utf8
#define uv_rt_current_directory_query_utf8 uv_rt_current_directory_utf8
#define uv_rt_process_spawn uv_rt_process_create
#define uv_rt_wait uv_rt_wait_one
#define uv_rt_process_exit_status uv_rt_process_exit_code
#define uv_rt_handle_release uv_rt_close_handle
#define uv_rt_module_open_loaded uv_rt_module_get
#define uv_rt_module_open uv_rt_module_load
#define uv_rt_module_lookup uv_rt_module_symbol
#define uv_rt_thread_spawn uv_rt_thread_create
#define uv_rt_thread_exit_status uv_rt_thread_exit_code
#define uv_rt_event_open uv_rt_event_create
#define uv_rt_std_stream uv_rt_std_handle
#define uv_rt_std_stream_set uv_rt_std_handle_set
#define uv_rt_file_open_path_wide uv_rt_file_open_wide
#define uv_rt_file_sync uv_rt_handle_flush
#define uv_rt_file_size uv_rt_file_size_get
#define uv_rt_file_seek uv_rt_file_pointer_set
#define uv_rt_path_attributes_wide uv_rt_file_attributes_get_wide
#define uv_rt_file_remove_wide uv_rt_file_delete_wide
#define uv_rt_directory_remove_path_wide uv_rt_directory_remove_wide
#define uv_rt_directory_create_path_wide uv_rt_directory_create_wide
#define uv_rt_temp_directory_wide uv_rt_temp_path_get_wide
#define uv_rt_temp_file_path_wide uv_rt_temp_file_name_wide
#define uv_rt_directory_scan_first_wide uv_rt_find_first_wide
#define uv_rt_directory_scan_next uv_rt_find_next
#define uv_rt_directory_scan_close uv_rt_find_close
#define uv_rt_shared_mapping_create uv_rt_mapping_create
#define uv_rt_shared_mapping_view uv_rt_mapping_view
#define uv_rt_shared_mapping_unview uv_rt_mapping_unview

#define uv_platform_error_get uv_rt_last_error_get
#define uv_platform_error_set uv_rt_last_error_set
#define uv_platform_last_error_get uv_rt_last_error_get
#define uv_platform_last_error_set uv_rt_last_error_set
#define uv_platform_heap_handle uv_rt_heap_handle
#define uv_platform_heap_alloc uv_rt_heap_alloc
#define uv_platform_heap_free uv_rt_heap_free
#define uv_platform_heap_validate uv_rt_heap_validate
#define uv_platform_process_exit uv_rt_exit_process
#define uv_platform_exit_process uv_rt_exit_process
#define uv_platform_env_get_wide uv_rt_env_query_wide
#define uv_platform_env_get_utf8 uv_rt_env_query_utf8
#define uv_platform_icu_data_configure uv_rt_icu_data_configure
#define uv_platform_utf8_to_wide_chars uv_rt_utf8_to_wide_chars
#define uv_platform_wide_to_utf8_chars uv_rt_wide_to_utf8_chars
#define uv_platform_process_create uv_rt_process_spawn
#define uv_platform_wait_one uv_rt_wait
#define uv_platform_process_exit_code uv_rt_process_exit_status
#define uv_platform_handle_close uv_rt_close_handle
#define uv_platform_close_handle uv_rt_handle_release
#define uv_platform_module_get uv_rt_module_open_loaded
#define uv_platform_module_load uv_rt_module_open
#define uv_platform_module_symbol uv_rt_module_lookup
#define uv_platform_once_execute uv_rt_once_execute
#define uv_platform_tls_key_create uv_rt_tls_key_create
#define uv_platform_tls_get uv_rt_tls_get
#define uv_platform_tls_set uv_rt_tls_set
#define uv_platform_mutex_init uv_rt_mutex_init
#define uv_platform_mutex_destroy uv_rt_mutex_destroy
#define uv_platform_mutex_lock uv_rt_mutex_lock
#define uv_platform_mutex_unlock uv_rt_mutex_unlock
#define uv_platform_condition_init uv_rt_condition_init
#define uv_platform_condition_wait uv_rt_condition_wait
#define uv_platform_condition_wake_one uv_rt_condition_wake_one
#define uv_platform_condition_wake_all uv_rt_condition_wake_all
#define uv_platform_rwlock_init uv_rt_rwlock_init
#define uv_platform_rwlock_lock_exclusive uv_rt_rwlock_lock_exclusive
#define uv_platform_rwlock_unlock_exclusive uv_rt_rwlock_unlock_exclusive
#define uv_platform_rwlock_lock_shared uv_rt_rwlock_lock_shared
#define uv_platform_rwlock_unlock_shared uv_rt_rwlock_unlock_shared
#define uv_platform_atomic_exchange uv_rt_atomic_exchange
#define uv_platform_atomic_compare_exchange uv_rt_atomic_compare_exchange
#define uv_platform_atomic_increment64 uv_rt_atomic_increment64
#define uv_platform_current_thread_id uv_rt_current_thread_id
#define uv_platform_current_process_id uv_rt_current_process_id
#define uv_platform_current_thread uv_rt_current_thread
#define uv_platform_thread_priority_get uv_rt_thread_priority_get
#define uv_platform_thread_priority_set uv_rt_thread_priority_set
#define uv_platform_thread_affinity_set uv_rt_thread_affinity_set
#define uv_platform_thread_create uv_rt_thread_spawn
#define uv_platform_thread_exit_code uv_rt_thread_exit_status
#define uv_platform_event_create uv_rt_event_open
#define uv_platform_event_set uv_rt_event_set
#define uv_platform_event_reset uv_rt_event_reset
#define uv_platform_std_handle uv_rt_std_stream
#define uv_platform_std_handle_set uv_rt_std_stream_set
#define uv_platform_console_mode_get uv_rt_console_mode_get
#define uv_platform_console_write_utf8 uv_rt_console_write_utf8
#define uv_platform_handle_write uv_rt_handle_write
#define uv_platform_handle_read uv_rt_handle_read
#define uv_platform_file_open_wide uv_rt_file_open_path_wide
#define uv_platform_handle_flush uv_rt_file_sync
#define uv_platform_file_size_get uv_rt_file_size
#define uv_platform_file_pointer_set uv_rt_file_seek
#define uv_platform_file_attributes_get_wide uv_rt_path_attributes_wide
#define uv_platform_file_delete_wide uv_rt_file_remove_wide
#define uv_platform_directory_remove_wide uv_rt_directory_remove_path_wide
#define uv_platform_directory_create_wide uv_rt_directory_create_path_wide
#define uv_platform_temp_path_get_wide uv_rt_temp_directory_wide
#define uv_platform_temp_file_name_wide uv_rt_temp_file_path_wide
#define uv_platform_find_first_wide uv_rt_directory_scan_first_wide
#define uv_platform_find_next uv_rt_directory_scan_next
#define uv_platform_find_close uv_rt_directory_scan_close
#define uv_platform_mapping_create uv_rt_shared_mapping_create
#define uv_platform_mapping_view uv_rt_shared_mapping_view
#define uv_platform_mapping_unview uv_rt_shared_mapping_unview
#define uv_platform_debug_break uv_rt_debug_break
#define uv_platform_system_time_filetime uv_rt_system_time_filetime
#define uv_platform_panic_boundary_active \
  uv_rt_panic_boundary_active
#define uv_platform_panic_boundary_run uv_rt_panic_boundary_run
#define uv_platform_panic_boundary_raise uv_rt_panic_boundary_raise

static __inline uv_rt_dword_t uv_rt_last_error_get(void) {
  return uv_platform_backend_error_get();
}

static __inline void uv_rt_last_error_set(uv_rt_dword_t error_code) {
  uv_platform_backend_error_set(error_code);
}

static __inline uv_rt_handle_t uv_rt_heap_handle(void) {
  return uv_platform_backend_heap_handle();
}

static __inline void* uv_rt_heap_alloc(uv_rt_handle_t heap,
                                            uv_rt_dword_t flags,
                                            size_t bytes) {
  return uv_platform_backend_heap_alloc(heap, flags, bytes);
}

static __inline uv_rt_bool_t uv_rt_heap_free(uv_rt_handle_t heap,
                                                       uv_rt_dword_t flags,
                                                       void* memory) {
  return uv_platform_backend_heap_free(heap, flags, memory);
}

static __inline uv_rt_bool_t uv_rt_heap_validate(
    uv_rt_handle_t heap,
    uv_rt_dword_t flags,
    const void* memory) {
  return uv_platform_backend_heap_validate(heap, flags, memory);
}

static __inline void uv_rt_exit_process(uv_rt_uint_t exit_code) {
  uv_platform_backend_exit_process(exit_code);
}

static __inline uv_rt_dword_t uv_rt_env_get_wide(
    const wchar_t* name,
    wchar_t* buffer,
    uv_rt_dword_t size) {
  return uv_platform_backend_env_get_wide(name, buffer, size);
}

static __inline uv_rt_dword_t uv_rt_env_get_utf8(
    const char* name,
    char* buffer,
    uv_rt_dword_t size) {
  return uv_platform_backend_env_get_utf8(name, buffer, size);
}

static __inline uv_rt_dword_t uv_rt_executable_path_utf8(
    char* buffer,
    uv_rt_dword_t size) {
  return uv_platform_backend_executable_path_utf8(buffer, size);
}

static __inline uv_rt_uptr_t uv_rt_argument_count(void) {
  return uv_platform_backend_argument_count();
}

static __inline uv_rt_dword_t uv_rt_argument_utf8(
    uv_rt_uptr_t index,
    char* buffer,
    uv_rt_dword_t size) {
  return uv_platform_backend_argument_utf8(index, buffer, size);
}

static __inline uv_rt_dword_t uv_rt_current_directory_utf8(
    char* buffer,
    uv_rt_dword_t size) {
  return uv_platform_backend_current_directory_utf8(buffer, size);
}

static __inline void uv_rt_icu_data_configure(void) {
  uv_platform_backend_icu_data_configure();
}

static __inline int uv_rt_utf8_to_wide_chars(const char* source,
                                                  int source_length,
                                                  wchar_t* destination,
                                                  int destination_length) {
  return uv_platform_backend_utf8_to_wide_chars(
      source, source_length, destination, destination_length);
}

static __inline int uv_rt_wide_to_utf8_chars(const wchar_t* source,
                                                  int source_length,
                                                  char* destination,
                                                  int destination_length) {
  return uv_platform_backend_wide_to_utf8_chars(
      source, source_length, destination, destination_length);
}

static __inline uv_rt_bool_t uv_rt_process_create(
    const wchar_t* application_name,
    wchar_t* command_line,
    void* process_attributes,
    void* thread_attributes,
    uv_rt_bool_t inherit_handles,
    uv_rt_dword_t creation_flags,
    void* environment,
    const wchar_t* current_directory,
    uv_rt_startup_info_t* startup_info,
    uv_rt_process_info_t* process_information) {
  return uv_platform_backend_process_create(application_name,
                                                 command_line,
                                                 process_attributes,
                                                 thread_attributes,
                                                 inherit_handles,
                                                 creation_flags,
                                                 environment,
                                                 current_directory,
                                                 startup_info,
                                                 process_information);
}

static __inline uv_rt_dword_t uv_rt_wait_one(
    uv_rt_handle_t handle,
    uv_rt_dword_t milliseconds) {
  return uv_platform_backend_wait_one(handle, milliseconds);
}

static __inline uv_rt_bool_t uv_rt_process_exit_code(
    uv_rt_handle_t handle,
    uv_rt_dword_t* exit_code) {
  return uv_platform_backend_process_exit_code(handle, exit_code);
}

static __inline uv_rt_bool_t uv_rt_close_handle(
    uv_rt_handle_t handle) {
  return uv_platform_backend_close_handle(handle);
}

static __inline uv_rt_module_t uv_rt_module_get(const char* name) {
  return uv_platform_backend_module_get(name);
}

static __inline uv_rt_module_t uv_rt_module_load(const char* name) {
  return uv_platform_backend_module_load(name);
}

static __inline void* uv_rt_module_symbol(uv_rt_module_t module,
                                               const char* symbol_name) {
  return uv_platform_backend_module_symbol(module, symbol_name);
}

static __inline uv_rt_bool_t uv_rt_once_execute(
    uv_rt_once_t* init_once,
    uv_rt_once_callback_t init_fn,
    void* parameter,
    void** context) {
  return uv_platform_backend_once_execute(init_once, init_fn, parameter,
                                               context);
}

static __inline uv_rt_tls_key_t uv_rt_tls_key_create(void) {
  return uv_platform_backend_tls_key_create();
}

static __inline void* uv_rt_tls_get(uv_rt_tls_key_t index) {
  return uv_platform_backend_tls_get(index);
}

static __inline uv_rt_bool_t uv_rt_tls_set(uv_rt_tls_key_t index,
                                                     void* value) {
  return uv_platform_backend_tls_set(index, value);
}

static __inline void uv_rt_mutex_init(uv_rt_mutex_t* mutex) {
  uv_platform_backend_mutex_init(mutex);
}

static __inline void uv_rt_mutex_destroy(uv_rt_mutex_t* mutex) {
  uv_platform_backend_mutex_destroy(mutex);
}

static __inline void uv_rt_mutex_lock(uv_rt_mutex_t* mutex) {
  uv_platform_backend_mutex_lock(mutex);
}

static __inline void uv_rt_mutex_unlock(uv_rt_mutex_t* mutex) {
  uv_platform_backend_mutex_unlock(mutex);
}

static __inline void uv_rt_condition_init(
    uv_rt_condition_t* condition) {
  uv_platform_backend_condition_init(condition);
}

static __inline uv_rt_bool_t uv_rt_condition_wait(
    uv_rt_condition_t* condition,
    uv_rt_mutex_t* mutex,
    uv_rt_dword_t milliseconds) {
  return uv_platform_backend_condition_wait(condition, mutex, milliseconds);
}

static __inline void uv_rt_condition_wake_one(
    uv_rt_condition_t* condition) {
  uv_platform_backend_condition_wake_one(condition);
}

static __inline void uv_rt_condition_wake_all(
    uv_rt_condition_t* condition) {
  uv_platform_backend_condition_wake_all(condition);
}

static __inline void uv_rt_rwlock_init(uv_rt_rwlock_t* lock) {
  uv_platform_backend_rwlock_init(lock);
}

static __inline void uv_rt_rwlock_lock_exclusive(
    uv_rt_rwlock_t* lock) {
  uv_platform_backend_rwlock_lock_exclusive(lock);
}

static __inline void uv_rt_rwlock_unlock_exclusive(
    uv_rt_rwlock_t* lock) {
  uv_platform_backend_rwlock_unlock_exclusive(lock);
}

static __inline void uv_rt_rwlock_lock_shared(uv_rt_rwlock_t* lock) {
  uv_platform_backend_rwlock_lock_shared(lock);
}

static __inline void uv_rt_rwlock_unlock_shared(
    uv_rt_rwlock_t* lock) {
  uv_platform_backend_rwlock_unlock_shared(lock);
}

static __inline uv_rt_long_t uv_rt_atomic_exchange(
    volatile uv_rt_long_t* target,
    uv_rt_long_t value) {
  return uv_platform_backend_atomic_exchange(target, value);
}

static __inline uv_rt_long_t uv_rt_atomic_compare_exchange(
    volatile uv_rt_long_t* target,
    uv_rt_long_t exchange,
    uv_rt_long_t comparand) {
  return uv_platform_backend_atomic_compare_exchange(target, exchange,
                                                          comparand);
}

static __inline uv_rt_long64_t uv_rt_atomic_increment64(
    volatile uv_rt_long64_t* target) {
  return uv_platform_backend_atomic_increment64(target);
}

static __inline uv_rt_thread_id_t uv_rt_current_thread_id(void) {
  return uv_platform_backend_current_thread_id();
}

static __inline uv_rt_dword_t uv_rt_current_process_id(void) {
  return uv_platform_backend_current_process_id();
}

static __inline uv_rt_handle_t uv_rt_current_thread(void) {
  return uv_platform_backend_current_thread();
}

static __inline int uv_rt_thread_priority_get(uv_rt_handle_t thread) {
  return uv_platform_backend_thread_priority_get(thread);
}

static __inline uv_rt_bool_t uv_rt_thread_priority_set(
    uv_rt_handle_t thread,
    int priority) {
  return uv_platform_backend_thread_priority_set(thread, priority);
}

static __inline uv_rt_dword_ptr_t uv_rt_thread_affinity_set(
    uv_rt_handle_t thread,
    uv_rt_dword_ptr_t mask) {
  return uv_platform_backend_thread_affinity_set(thread, mask);
}

static __inline uv_rt_handle_t uv_rt_thread_create(
    void* attributes,
    size_t stack_size,
    uv_rt_thread_start_routine_t start_routine,
    void* parameter,
    uv_rt_dword_t creation_flags,
    uv_rt_dword_t* thread_id) {
  return uv_platform_backend_thread_create(attributes, stack_size,
                                                start_routine, parameter,
                                                creation_flags, thread_id);
}

static __inline uv_rt_bool_t uv_rt_thread_exit_code(
    uv_rt_handle_t handle,
    uv_rt_dword_t* exit_code) {
  return uv_platform_backend_thread_exit_code(handle, exit_code);
}

static __inline uv_rt_handle_t uv_rt_event_create(
    void* attributes,
    uv_rt_bool_t manual_reset,
    uv_rt_bool_t initial_state,
    const wchar_t* name) {
  return uv_platform_backend_event_create(attributes, manual_reset,
                                               initial_state, name);
}

static __inline uv_rt_bool_t uv_rt_event_set(
    uv_rt_handle_t handle) {
  return uv_platform_backend_event_set(handle);
}

static __inline uv_rt_bool_t uv_rt_event_reset(
    uv_rt_handle_t handle) {
  return uv_platform_backend_event_reset(handle);
}

static __inline uv_rt_handle_t uv_rt_std_handle(
    uv_rt_dword_t std_handle_id) {
  return uv_platform_backend_std_handle(std_handle_id);
}

static __inline uv_rt_bool_t uv_rt_std_handle_set(
    uv_rt_dword_t std_handle_id,
    uv_rt_handle_t handle) {
  return uv_platform_backend_std_handle_set(std_handle_id, handle);
}

static __inline uv_rt_bool_t uv_rt_console_mode_get(
    uv_rt_handle_t handle,
    uv_rt_dword_t* mode) {
  return uv_platform_backend_console_mode_get(handle, mode);
}

static __inline uv_rt_bool_t uv_rt_console_write_utf8(
    uv_rt_handle_t handle,
    const void* buffer,
    uv_rt_dword_t chars_to_write,
    uv_rt_dword_t* chars_written) {
  return uv_platform_backend_console_write_utf8(handle, buffer,
                                                     chars_to_write,
                                                     chars_written);
}

static __inline uv_rt_bool_t uv_rt_handle_write(
    uv_rt_handle_t handle,
    const void* buffer,
    uv_rt_dword_t bytes_to_write,
    uv_rt_dword_t* bytes_written) {
  return uv_platform_backend_handle_write(handle, buffer, bytes_to_write,
                                               bytes_written);
}

static __inline uv_rt_bool_t uv_rt_handle_read(
    uv_rt_handle_t handle,
    void* buffer,
    uv_rt_dword_t bytes_to_read,
    uv_rt_dword_t* bytes_read) {
  return uv_platform_backend_handle_read(handle, buffer, bytes_to_read,
                                              bytes_read);
}

static __inline uv_rt_handle_t uv_rt_file_open_wide(
    const wchar_t* path,
    uv_rt_dword_t desired_access,
    uv_rt_dword_t share_mode,
    void* security_attributes,
    uv_rt_dword_t creation_disposition,
    uv_rt_dword_t flags_and_attributes,
    uv_rt_handle_t template_file) {
  return uv_platform_backend_file_open_wide(
      path, desired_access, share_mode, security_attributes,
      creation_disposition, flags_and_attributes, template_file);
}

static __inline uv_rt_bool_t uv_rt_handle_flush(
    uv_rt_handle_t handle) {
  return uv_platform_backend_handle_flush(handle);
}

static __inline uv_rt_bool_t uv_rt_file_size_get(
    uv_rt_handle_t handle,
    uv_rt_large_integer_t* size_out) {
  return uv_platform_backend_file_size_get(handle, size_out);
}

static __inline uv_rt_bool_t uv_rt_file_pointer_set(
    uv_rt_handle_t handle,
    uv_rt_large_integer_t distance,
    uv_rt_large_integer_t* new_position,
    uv_rt_dword_t move_method) {
  return uv_platform_backend_file_pointer_set(handle, distance,
                                                   new_position, move_method);
}

static __inline uv_rt_dword_t uv_rt_file_attributes_get_wide(
    const wchar_t* path) {
  return uv_platform_backend_file_attributes_get_wide(path);
}

static __inline uv_rt_bool_t uv_rt_file_delete_wide(
    const wchar_t* path) {
  return uv_platform_backend_file_delete_wide(path);
}

static __inline uv_rt_bool_t uv_rt_directory_remove_wide(
    const wchar_t* path) {
  return uv_platform_backend_directory_remove_wide(path);
}

static __inline uv_rt_bool_t uv_rt_directory_create_wide(
    const wchar_t* path,
    void* security_attributes) {
  return uv_platform_backend_directory_create_wide(path,
                                                        security_attributes);
}

static __inline uv_rt_dword_t uv_rt_temp_path_get_wide(
    uv_rt_dword_t buffer_length,
    wchar_t* buffer) {
  return uv_platform_backend_temp_path_get_wide(buffer_length, buffer);
}

static __inline uv_rt_uint_t uv_rt_temp_file_name_wide(
    const wchar_t* path_name,
    const wchar_t* prefix_string,
    uv_rt_uint_t unique,
    wchar_t* temp_file_name) {
  return uv_platform_backend_temp_file_name_wide(path_name,
                                                      prefix_string,
                                                      unique,
                                                      temp_file_name);
}

static __inline uv_rt_handle_t uv_rt_find_first_wide(
    const wchar_t* pattern,
    uv_rt_find_data_t* find_data) {
  return uv_platform_backend_find_first_wide(pattern, find_data);
}

static __inline uv_rt_bool_t uv_rt_find_next(
    uv_rt_handle_t handle,
    uv_rt_find_data_t* find_data) {
  return uv_platform_backend_find_next(handle, find_data);
}

static __inline uv_rt_bool_t uv_rt_find_close(
    uv_rt_handle_t handle) {
  return uv_platform_backend_find_close(handle);
}

static __inline uv_rt_handle_t uv_rt_mapping_create(
    uv_rt_handle_t file,
    void* attributes,
    uv_rt_dword_t protect,
    uv_rt_dword_t maximum_size_high,
    uv_rt_dword_t maximum_size_low,
    const wchar_t* name) {
  return uv_platform_backend_mapping_create(file, attributes, protect,
                                                 maximum_size_high,
                                                 maximum_size_low, name);
}

static __inline void* uv_rt_mapping_view(
    uv_rt_handle_t mapping,
    uv_rt_dword_t desired_access,
    uv_rt_dword_t file_offset_high,
    uv_rt_dword_t file_offset_low,
    size_t number_of_bytes_to_map) {
  return uv_platform_backend_mapping_view(mapping, desired_access,
                                               file_offset_high,
                                               file_offset_low,
                                               number_of_bytes_to_map);
}

static __inline uv_rt_bool_t uv_rt_mapping_unview(
    const void* base_address) {
  return uv_platform_backend_mapping_unview(base_address);
}

static __inline void uv_rt_debug_break(void) {
  uv_platform_backend_debug_break();
}

static __inline void uv_rt_system_time_filetime(
    uv_rt_filetime_t* file_time) {
  uv_platform_backend_system_time_filetime(file_time);
}

static __inline uv_rt_bool_t uv_rt_panic_boundary_active(void) {
  return uv_platform_backend_panic_boundary_active();
}

static __inline uv_rt_bool_t uv_rt_panic_boundary_run(
    uv_rt_panic_boundary_body_t body,
    void* context,
    uv_rt_dword_t* panic_code) {
  return uv_platform_backend_panic_boundary_run(body, context, panic_code);
}

static __inline void uv_rt_panic_boundary_raise(
    uv_rt_dword_t panic_code) {
  uv_platform_backend_panic_boundary_raise(panic_code);
}

#endif  // UV_RT_PLATFORM_H
