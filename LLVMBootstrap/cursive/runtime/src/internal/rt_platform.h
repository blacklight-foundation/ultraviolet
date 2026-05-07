#ifndef CURSIVE_RT_PLATFORM_H
#define CURSIVE_RT_PLATFORM_H

#include "rt_platform_types.h"

#if defined(CURSIVE_RT_PLATFORM_WINDOWS)
#include "../platform/windows/rt_platform_windows_api.h"
#endif

/* Neutral runtime surface. The older cursive_platform_* names remain as
 * compatibility aliases for untouched runtime code. */
#define cursive_rt_error_get cursive_rt_last_error_get
#define cursive_rt_error_set cursive_rt_last_error_set
#define cursive_rt_env_query_wide cursive_rt_env_get_wide
#define cursive_rt_env_query_utf8 cursive_rt_env_get_utf8
#define cursive_rt_executable_path_query_utf8 cursive_rt_executable_path_utf8
#define cursive_rt_argument_count_query cursive_rt_argument_count
#define cursive_rt_argument_query_utf8 cursive_rt_argument_utf8
#define cursive_rt_current_directory_query_utf8 cursive_rt_current_directory_utf8
#define cursive_rt_process_spawn cursive_rt_process_create
#define cursive_rt_wait cursive_rt_wait_one
#define cursive_rt_process_exit_status cursive_rt_process_exit_code
#define cursive_rt_handle_release cursive_rt_close_handle
#define cursive_rt_module_open_loaded cursive_rt_module_get
#define cursive_rt_module_open cursive_rt_module_load
#define cursive_rt_module_lookup cursive_rt_module_symbol
#define cursive_rt_thread_spawn cursive_rt_thread_create
#define cursive_rt_thread_exit_status cursive_rt_thread_exit_code
#define cursive_rt_event_open cursive_rt_event_create
#define cursive_rt_std_stream cursive_rt_std_handle
#define cursive_rt_std_stream_set cursive_rt_std_handle_set
#define cursive_rt_file_open_path_wide cursive_rt_file_open_wide
#define cursive_rt_file_sync cursive_rt_handle_flush
#define cursive_rt_file_size cursive_rt_file_size_get
#define cursive_rt_file_seek cursive_rt_file_pointer_set
#define cursive_rt_path_attributes_wide cursive_rt_file_attributes_get_wide
#define cursive_rt_file_remove_wide cursive_rt_file_delete_wide
#define cursive_rt_directory_remove_path_wide cursive_rt_directory_remove_wide
#define cursive_rt_directory_create_path_wide cursive_rt_directory_create_wide
#define cursive_rt_temp_directory_wide cursive_rt_temp_path_get_wide
#define cursive_rt_temp_file_path_wide cursive_rt_temp_file_name_wide
#define cursive_rt_directory_scan_first_wide cursive_rt_find_first_wide
#define cursive_rt_directory_scan_next cursive_rt_find_next
#define cursive_rt_directory_scan_close cursive_rt_find_close
#define cursive_rt_shared_mapping_create cursive_rt_mapping_create
#define cursive_rt_shared_mapping_view cursive_rt_mapping_view
#define cursive_rt_shared_mapping_unview cursive_rt_mapping_unview

#define cursive_platform_error_get cursive_rt_last_error_get
#define cursive_platform_error_set cursive_rt_last_error_set
#define cursive_platform_last_error_get cursive_rt_last_error_get
#define cursive_platform_last_error_set cursive_rt_last_error_set
#define cursive_platform_heap_handle cursive_rt_heap_handle
#define cursive_platform_heap_alloc cursive_rt_heap_alloc
#define cursive_platform_heap_free cursive_rt_heap_free
#define cursive_platform_heap_validate cursive_rt_heap_validate
#define cursive_platform_process_exit cursive_rt_exit_process
#define cursive_platform_exit_process cursive_rt_exit_process
#define cursive_platform_env_get_wide cursive_rt_env_query_wide
#define cursive_platform_env_get_utf8 cursive_rt_env_query_utf8
#define cursive_platform_icu_data_configure cursive_rt_icu_data_configure
#define cursive_platform_utf8_to_wide_chars cursive_rt_utf8_to_wide_chars
#define cursive_platform_wide_to_utf8_chars cursive_rt_wide_to_utf8_chars
#define cursive_platform_process_create cursive_rt_process_spawn
#define cursive_platform_wait_one cursive_rt_wait
#define cursive_platform_process_exit_code cursive_rt_process_exit_status
#define cursive_platform_handle_close cursive_rt_close_handle
#define cursive_platform_close_handle cursive_rt_handle_release
#define cursive_platform_module_get cursive_rt_module_open_loaded
#define cursive_platform_module_load cursive_rt_module_open
#define cursive_platform_module_symbol cursive_rt_module_lookup
#define cursive_platform_once_execute cursive_rt_once_execute
#define cursive_platform_tls_key_create cursive_rt_tls_key_create
#define cursive_platform_tls_get cursive_rt_tls_get
#define cursive_platform_tls_set cursive_rt_tls_set
#define cursive_platform_mutex_init cursive_rt_mutex_init
#define cursive_platform_mutex_destroy cursive_rt_mutex_destroy
#define cursive_platform_mutex_lock cursive_rt_mutex_lock
#define cursive_platform_mutex_unlock cursive_rt_mutex_unlock
#define cursive_platform_condition_init cursive_rt_condition_init
#define cursive_platform_condition_wait cursive_rt_condition_wait
#define cursive_platform_condition_wake_one cursive_rt_condition_wake_one
#define cursive_platform_condition_wake_all cursive_rt_condition_wake_all
#define cursive_platform_rwlock_init cursive_rt_rwlock_init
#define cursive_platform_rwlock_lock_exclusive cursive_rt_rwlock_lock_exclusive
#define cursive_platform_rwlock_unlock_exclusive cursive_rt_rwlock_unlock_exclusive
#define cursive_platform_rwlock_lock_shared cursive_rt_rwlock_lock_shared
#define cursive_platform_rwlock_unlock_shared cursive_rt_rwlock_unlock_shared
#define cursive_platform_atomic_exchange cursive_rt_atomic_exchange
#define cursive_platform_atomic_compare_exchange cursive_rt_atomic_compare_exchange
#define cursive_platform_atomic_increment64 cursive_rt_atomic_increment64
#define cursive_platform_current_thread_id cursive_rt_current_thread_id
#define cursive_platform_current_process_id cursive_rt_current_process_id
#define cursive_platform_current_thread cursive_rt_current_thread
#define cursive_platform_thread_priority_get cursive_rt_thread_priority_get
#define cursive_platform_thread_priority_set cursive_rt_thread_priority_set
#define cursive_platform_thread_affinity_set cursive_rt_thread_affinity_set
#define cursive_platform_thread_create cursive_rt_thread_spawn
#define cursive_platform_thread_exit_code cursive_rt_thread_exit_status
#define cursive_platform_event_create cursive_rt_event_open
#define cursive_platform_event_set cursive_rt_event_set
#define cursive_platform_event_reset cursive_rt_event_reset
#define cursive_platform_std_handle cursive_rt_std_stream
#define cursive_platform_std_handle_set cursive_rt_std_stream_set
#define cursive_platform_console_mode_get cursive_rt_console_mode_get
#define cursive_platform_console_write_utf8 cursive_rt_console_write_utf8
#define cursive_platform_handle_write cursive_rt_handle_write
#define cursive_platform_handle_read cursive_rt_handle_read
#define cursive_platform_file_open_wide cursive_rt_file_open_path_wide
#define cursive_platform_handle_flush cursive_rt_file_sync
#define cursive_platform_file_size_get cursive_rt_file_size
#define cursive_platform_file_pointer_set cursive_rt_file_seek
#define cursive_platform_file_attributes_get_wide cursive_rt_path_attributes_wide
#define cursive_platform_file_delete_wide cursive_rt_file_remove_wide
#define cursive_platform_directory_remove_wide cursive_rt_directory_remove_path_wide
#define cursive_platform_directory_create_wide cursive_rt_directory_create_path_wide
#define cursive_platform_temp_path_get_wide cursive_rt_temp_directory_wide
#define cursive_platform_temp_file_name_wide cursive_rt_temp_file_path_wide
#define cursive_platform_find_first_wide cursive_rt_directory_scan_first_wide
#define cursive_platform_find_next cursive_rt_directory_scan_next
#define cursive_platform_find_close cursive_rt_directory_scan_close
#define cursive_platform_mapping_create cursive_rt_shared_mapping_create
#define cursive_platform_mapping_view cursive_rt_shared_mapping_view
#define cursive_platform_mapping_unview cursive_rt_shared_mapping_unview
#define cursive_platform_debug_break cursive_rt_debug_break
#define cursive_platform_system_time_filetime cursive_rt_system_time_filetime
#define cursive_platform_panic_boundary_active \
  cursive_rt_panic_boundary_active
#define cursive_platform_panic_boundary_run cursive_rt_panic_boundary_run
#define cursive_platform_panic_boundary_raise cursive_rt_panic_boundary_raise

static __inline cursive_rt_dword_t cursive_rt_last_error_get(void) {
  return cursive_platform_backend_error_get();
}

static __inline void cursive_rt_last_error_set(cursive_rt_dword_t error_code) {
  cursive_platform_backend_error_set(error_code);
}

static __inline cursive_rt_handle_t cursive_rt_heap_handle(void) {
  return cursive_platform_backend_heap_handle();
}

static __inline void* cursive_rt_heap_alloc(cursive_rt_handle_t heap,
                                            cursive_rt_dword_t flags,
                                            size_t bytes) {
  return cursive_platform_backend_heap_alloc(heap, flags, bytes);
}

static __inline cursive_rt_bool_t cursive_rt_heap_free(cursive_rt_handle_t heap,
                                                       cursive_rt_dword_t flags,
                                                       void* memory) {
  return cursive_platform_backend_heap_free(heap, flags, memory);
}

static __inline cursive_rt_bool_t cursive_rt_heap_validate(
    cursive_rt_handle_t heap,
    cursive_rt_dword_t flags,
    const void* memory) {
  return cursive_platform_backend_heap_validate(heap, flags, memory);
}

static __inline void cursive_rt_exit_process(cursive_rt_uint_t exit_code) {
  cursive_platform_backend_exit_process(exit_code);
}

static __inline cursive_rt_dword_t cursive_rt_env_get_wide(
    const wchar_t* name,
    wchar_t* buffer,
    cursive_rt_dword_t size) {
  return cursive_platform_backend_env_get_wide(name, buffer, size);
}

static __inline cursive_rt_dword_t cursive_rt_env_get_utf8(
    const char* name,
    char* buffer,
    cursive_rt_dword_t size) {
  return cursive_platform_backend_env_get_utf8(name, buffer, size);
}

static __inline cursive_rt_dword_t cursive_rt_executable_path_utf8(
    char* buffer,
    cursive_rt_dword_t size) {
  return cursive_platform_backend_executable_path_utf8(buffer, size);
}

static __inline cursive_rt_uptr_t cursive_rt_argument_count(void) {
  return cursive_platform_backend_argument_count();
}

static __inline cursive_rt_dword_t cursive_rt_argument_utf8(
    cursive_rt_uptr_t index,
    char* buffer,
    cursive_rt_dword_t size) {
  return cursive_platform_backend_argument_utf8(index, buffer, size);
}

static __inline cursive_rt_dword_t cursive_rt_current_directory_utf8(
    char* buffer,
    cursive_rt_dword_t size) {
  return cursive_platform_backend_current_directory_utf8(buffer, size);
}

static __inline void cursive_rt_icu_data_configure(void) {
  cursive_platform_backend_icu_data_configure();
}

static __inline int cursive_rt_utf8_to_wide_chars(const char* source,
                                                  int source_length,
                                                  wchar_t* destination,
                                                  int destination_length) {
  return cursive_platform_backend_utf8_to_wide_chars(
      source, source_length, destination, destination_length);
}

static __inline int cursive_rt_wide_to_utf8_chars(const wchar_t* source,
                                                  int source_length,
                                                  char* destination,
                                                  int destination_length) {
  return cursive_platform_backend_wide_to_utf8_chars(
      source, source_length, destination, destination_length);
}

static __inline cursive_rt_bool_t cursive_rt_process_create(
    const wchar_t* application_name,
    wchar_t* command_line,
    void* process_attributes,
    void* thread_attributes,
    cursive_rt_bool_t inherit_handles,
    cursive_rt_dword_t creation_flags,
    void* environment,
    const wchar_t* current_directory,
    cursive_rt_startup_info_t* startup_info,
    cursive_rt_process_info_t* process_information) {
  return cursive_platform_backend_process_create(application_name,
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

static __inline cursive_rt_dword_t cursive_rt_wait_one(
    cursive_rt_handle_t handle,
    cursive_rt_dword_t milliseconds) {
  return cursive_platform_backend_wait_one(handle, milliseconds);
}

static __inline cursive_rt_bool_t cursive_rt_process_exit_code(
    cursive_rt_handle_t handle,
    cursive_rt_dword_t* exit_code) {
  return cursive_platform_backend_process_exit_code(handle, exit_code);
}

static __inline cursive_rt_bool_t cursive_rt_close_handle(
    cursive_rt_handle_t handle) {
  return cursive_platform_backend_close_handle(handle);
}

static __inline cursive_rt_module_t cursive_rt_module_get(const char* name) {
  return cursive_platform_backend_module_get(name);
}

static __inline cursive_rt_module_t cursive_rt_module_load(const char* name) {
  return cursive_platform_backend_module_load(name);
}

static __inline void* cursive_rt_module_symbol(cursive_rt_module_t module,
                                               const char* symbol_name) {
  return cursive_platform_backend_module_symbol(module, symbol_name);
}

static __inline cursive_rt_bool_t cursive_rt_once_execute(
    cursive_rt_once_t* init_once,
    cursive_rt_once_callback_t init_fn,
    void* parameter,
    void** context) {
  return cursive_platform_backend_once_execute(init_once, init_fn, parameter,
                                               context);
}

static __inline cursive_rt_tls_key_t cursive_rt_tls_key_create(void) {
  return cursive_platform_backend_tls_key_create();
}

static __inline void* cursive_rt_tls_get(cursive_rt_tls_key_t index) {
  return cursive_platform_backend_tls_get(index);
}

static __inline cursive_rt_bool_t cursive_rt_tls_set(cursive_rt_tls_key_t index,
                                                     void* value) {
  return cursive_platform_backend_tls_set(index, value);
}

static __inline void cursive_rt_mutex_init(cursive_rt_mutex_t* mutex) {
  cursive_platform_backend_mutex_init(mutex);
}

static __inline void cursive_rt_mutex_destroy(cursive_rt_mutex_t* mutex) {
  cursive_platform_backend_mutex_destroy(mutex);
}

static __inline void cursive_rt_mutex_lock(cursive_rt_mutex_t* mutex) {
  cursive_platform_backend_mutex_lock(mutex);
}

static __inline void cursive_rt_mutex_unlock(cursive_rt_mutex_t* mutex) {
  cursive_platform_backend_mutex_unlock(mutex);
}

static __inline void cursive_rt_condition_init(
    cursive_rt_condition_t* condition) {
  cursive_platform_backend_condition_init(condition);
}

static __inline cursive_rt_bool_t cursive_rt_condition_wait(
    cursive_rt_condition_t* condition,
    cursive_rt_mutex_t* mutex,
    cursive_rt_dword_t milliseconds) {
  return cursive_platform_backend_condition_wait(condition, mutex, milliseconds);
}

static __inline void cursive_rt_condition_wake_one(
    cursive_rt_condition_t* condition) {
  cursive_platform_backend_condition_wake_one(condition);
}

static __inline void cursive_rt_condition_wake_all(
    cursive_rt_condition_t* condition) {
  cursive_platform_backend_condition_wake_all(condition);
}

static __inline void cursive_rt_rwlock_init(cursive_rt_rwlock_t* lock) {
  cursive_platform_backend_rwlock_init(lock);
}

static __inline void cursive_rt_rwlock_lock_exclusive(
    cursive_rt_rwlock_t* lock) {
  cursive_platform_backend_rwlock_lock_exclusive(lock);
}

static __inline void cursive_rt_rwlock_unlock_exclusive(
    cursive_rt_rwlock_t* lock) {
  cursive_platform_backend_rwlock_unlock_exclusive(lock);
}

static __inline void cursive_rt_rwlock_lock_shared(cursive_rt_rwlock_t* lock) {
  cursive_platform_backend_rwlock_lock_shared(lock);
}

static __inline void cursive_rt_rwlock_unlock_shared(
    cursive_rt_rwlock_t* lock) {
  cursive_platform_backend_rwlock_unlock_shared(lock);
}

static __inline cursive_rt_long_t cursive_rt_atomic_exchange(
    volatile cursive_rt_long_t* target,
    cursive_rt_long_t value) {
  return cursive_platform_backend_atomic_exchange(target, value);
}

static __inline cursive_rt_long_t cursive_rt_atomic_compare_exchange(
    volatile cursive_rt_long_t* target,
    cursive_rt_long_t exchange,
    cursive_rt_long_t comparand) {
  return cursive_platform_backend_atomic_compare_exchange(target, exchange,
                                                          comparand);
}

static __inline cursive_rt_long64_t cursive_rt_atomic_increment64(
    volatile cursive_rt_long64_t* target) {
  return cursive_platform_backend_atomic_increment64(target);
}

static __inline cursive_rt_thread_id_t cursive_rt_current_thread_id(void) {
  return cursive_platform_backend_current_thread_id();
}

static __inline cursive_rt_dword_t cursive_rt_current_process_id(void) {
  return cursive_platform_backend_current_process_id();
}

static __inline cursive_rt_handle_t cursive_rt_current_thread(void) {
  return cursive_platform_backend_current_thread();
}

static __inline int cursive_rt_thread_priority_get(cursive_rt_handle_t thread) {
  return cursive_platform_backend_thread_priority_get(thread);
}

static __inline cursive_rt_bool_t cursive_rt_thread_priority_set(
    cursive_rt_handle_t thread,
    int priority) {
  return cursive_platform_backend_thread_priority_set(thread, priority);
}

static __inline cursive_rt_dword_ptr_t cursive_rt_thread_affinity_set(
    cursive_rt_handle_t thread,
    cursive_rt_dword_ptr_t mask) {
  return cursive_platform_backend_thread_affinity_set(thread, mask);
}

static __inline cursive_rt_handle_t cursive_rt_thread_create(
    void* attributes,
    size_t stack_size,
    cursive_rt_thread_start_routine_t start_routine,
    void* parameter,
    cursive_rt_dword_t creation_flags,
    cursive_rt_dword_t* thread_id) {
  return cursive_platform_backend_thread_create(attributes, stack_size,
                                                start_routine, parameter,
                                                creation_flags, thread_id);
}

static __inline cursive_rt_bool_t cursive_rt_thread_exit_code(
    cursive_rt_handle_t handle,
    cursive_rt_dword_t* exit_code) {
  return cursive_platform_backend_thread_exit_code(handle, exit_code);
}

static __inline cursive_rt_handle_t cursive_rt_event_create(
    void* attributes,
    cursive_rt_bool_t manual_reset,
    cursive_rt_bool_t initial_state,
    const wchar_t* name) {
  return cursive_platform_backend_event_create(attributes, manual_reset,
                                               initial_state, name);
}

static __inline cursive_rt_bool_t cursive_rt_event_set(
    cursive_rt_handle_t handle) {
  return cursive_platform_backend_event_set(handle);
}

static __inline cursive_rt_bool_t cursive_rt_event_reset(
    cursive_rt_handle_t handle) {
  return cursive_platform_backend_event_reset(handle);
}

static __inline cursive_rt_handle_t cursive_rt_std_handle(
    cursive_rt_dword_t std_handle_id) {
  return cursive_platform_backend_std_handle(std_handle_id);
}

static __inline cursive_rt_bool_t cursive_rt_std_handle_set(
    cursive_rt_dword_t std_handle_id,
    cursive_rt_handle_t handle) {
  return cursive_platform_backend_std_handle_set(std_handle_id, handle);
}

static __inline cursive_rt_bool_t cursive_rt_console_mode_get(
    cursive_rt_handle_t handle,
    cursive_rt_dword_t* mode) {
  return cursive_platform_backend_console_mode_get(handle, mode);
}

static __inline cursive_rt_bool_t cursive_rt_console_write_utf8(
    cursive_rt_handle_t handle,
    const void* buffer,
    cursive_rt_dword_t chars_to_write,
    cursive_rt_dword_t* chars_written) {
  return cursive_platform_backend_console_write_utf8(handle, buffer,
                                                     chars_to_write,
                                                     chars_written);
}

static __inline cursive_rt_bool_t cursive_rt_handle_write(
    cursive_rt_handle_t handle,
    const void* buffer,
    cursive_rt_dword_t bytes_to_write,
    cursive_rt_dword_t* bytes_written) {
  return cursive_platform_backend_handle_write(handle, buffer, bytes_to_write,
                                               bytes_written);
}

static __inline cursive_rt_bool_t cursive_rt_handle_read(
    cursive_rt_handle_t handle,
    void* buffer,
    cursive_rt_dword_t bytes_to_read,
    cursive_rt_dword_t* bytes_read) {
  return cursive_platform_backend_handle_read(handle, buffer, bytes_to_read,
                                              bytes_read);
}

static __inline cursive_rt_handle_t cursive_rt_file_open_wide(
    const wchar_t* path,
    cursive_rt_dword_t desired_access,
    cursive_rt_dword_t share_mode,
    void* security_attributes,
    cursive_rt_dword_t creation_disposition,
    cursive_rt_dword_t flags_and_attributes,
    cursive_rt_handle_t template_file) {
  return cursive_platform_backend_file_open_wide(
      path, desired_access, share_mode, security_attributes,
      creation_disposition, flags_and_attributes, template_file);
}

static __inline cursive_rt_bool_t cursive_rt_handle_flush(
    cursive_rt_handle_t handle) {
  return cursive_platform_backend_handle_flush(handle);
}

static __inline cursive_rt_bool_t cursive_rt_file_size_get(
    cursive_rt_handle_t handle,
    cursive_rt_large_integer_t* size_out) {
  return cursive_platform_backend_file_size_get(handle, size_out);
}

static __inline cursive_rt_bool_t cursive_rt_file_pointer_set(
    cursive_rt_handle_t handle,
    cursive_rt_large_integer_t distance,
    cursive_rt_large_integer_t* new_position,
    cursive_rt_dword_t move_method) {
  return cursive_platform_backend_file_pointer_set(handle, distance,
                                                   new_position, move_method);
}

static __inline cursive_rt_dword_t cursive_rt_file_attributes_get_wide(
    const wchar_t* path) {
  return cursive_platform_backend_file_attributes_get_wide(path);
}

static __inline cursive_rt_bool_t cursive_rt_file_delete_wide(
    const wchar_t* path) {
  return cursive_platform_backend_file_delete_wide(path);
}

static __inline cursive_rt_bool_t cursive_rt_directory_remove_wide(
    const wchar_t* path) {
  return cursive_platform_backend_directory_remove_wide(path);
}

static __inline cursive_rt_bool_t cursive_rt_directory_create_wide(
    const wchar_t* path,
    void* security_attributes) {
  return cursive_platform_backend_directory_create_wide(path,
                                                        security_attributes);
}

static __inline cursive_rt_dword_t cursive_rt_temp_path_get_wide(
    cursive_rt_dword_t buffer_length,
    wchar_t* buffer) {
  return cursive_platform_backend_temp_path_get_wide(buffer_length, buffer);
}

static __inline cursive_rt_uint_t cursive_rt_temp_file_name_wide(
    const wchar_t* path_name,
    const wchar_t* prefix_string,
    cursive_rt_uint_t unique,
    wchar_t* temp_file_name) {
  return cursive_platform_backend_temp_file_name_wide(path_name,
                                                      prefix_string,
                                                      unique,
                                                      temp_file_name);
}

static __inline cursive_rt_handle_t cursive_rt_find_first_wide(
    const wchar_t* pattern,
    cursive_rt_find_data_t* find_data) {
  return cursive_platform_backend_find_first_wide(pattern, find_data);
}

static __inline cursive_rt_bool_t cursive_rt_find_next(
    cursive_rt_handle_t handle,
    cursive_rt_find_data_t* find_data) {
  return cursive_platform_backend_find_next(handle, find_data);
}

static __inline cursive_rt_bool_t cursive_rt_find_close(
    cursive_rt_handle_t handle) {
  return cursive_platform_backend_find_close(handle);
}

static __inline cursive_rt_handle_t cursive_rt_mapping_create(
    cursive_rt_handle_t file,
    void* attributes,
    cursive_rt_dword_t protect,
    cursive_rt_dword_t maximum_size_high,
    cursive_rt_dword_t maximum_size_low,
    const wchar_t* name) {
  return cursive_platform_backend_mapping_create(file, attributes, protect,
                                                 maximum_size_high,
                                                 maximum_size_low, name);
}

static __inline void* cursive_rt_mapping_view(
    cursive_rt_handle_t mapping,
    cursive_rt_dword_t desired_access,
    cursive_rt_dword_t file_offset_high,
    cursive_rt_dword_t file_offset_low,
    size_t number_of_bytes_to_map) {
  return cursive_platform_backend_mapping_view(mapping, desired_access,
                                               file_offset_high,
                                               file_offset_low,
                                               number_of_bytes_to_map);
}

static __inline cursive_rt_bool_t cursive_rt_mapping_unview(
    const void* base_address) {
  return cursive_platform_backend_mapping_unview(base_address);
}

static __inline void cursive_rt_debug_break(void) {
  cursive_platform_backend_debug_break();
}

static __inline void cursive_rt_system_time_filetime(
    cursive_rt_filetime_t* file_time) {
  cursive_platform_backend_system_time_filetime(file_time);
}

static __inline cursive_rt_bool_t cursive_rt_panic_boundary_active(void) {
  return cursive_platform_backend_panic_boundary_active();
}

static __inline cursive_rt_bool_t cursive_rt_panic_boundary_run(
    cursive_rt_panic_boundary_body_t body,
    void* context,
    cursive_rt_dword_t* panic_code) {
  return cursive_platform_backend_panic_boundary_run(body, context, panic_code);
}

static __inline void cursive_rt_panic_boundary_raise(
    cursive_rt_dword_t panic_code) {
  cursive_platform_backend_panic_boundary_raise(panic_code);
}

#endif  // CURSIVE_RT_PLATFORM_H
