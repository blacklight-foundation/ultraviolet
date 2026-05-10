#ifndef CURSIVE_RT_PLATFORM_WINDOWS_API_H
#define CURSIVE_RT_PLATFORM_WINDOWS_API_H

static __inline cursive_platform_u32_t cursive_platform_backend_error_get(void) {
  return GetLastError();
}

static __inline void cursive_platform_backend_error_set(
    cursive_platform_u32_t error_code) {
  SetLastError(error_code);
}

static __inline cursive_platform_handle_t cursive_platform_backend_heap_handle(
    void) {
  return GetProcessHeap();
}

static __inline void* cursive_platform_backend_heap_alloc(
    cursive_platform_handle_t heap,
    cursive_platform_u32_t flags,
    size_t bytes) {
  return HeapAlloc(heap, flags, bytes);
}

static __inline cursive_platform_bool_t cursive_platform_backend_heap_free(
    cursive_platform_handle_t heap,
    cursive_platform_u32_t flags,
    void* memory) {
  return HeapFree(heap, flags, memory);
}

static __inline cursive_platform_bool_t cursive_platform_backend_heap_validate(
    cursive_platform_handle_t heap,
    cursive_platform_u32_t flags,
    const void* memory) {
  return HeapValidate(heap, flags, memory);
}

static __inline void cursive_platform_backend_exit_process(
    cursive_platform_uint_t exit_code) {
  ExitProcess(exit_code);
}

static __inline cursive_platform_u32_t cursive_platform_backend_env_get_wide(
    const wchar_t* name,
    wchar_t* buffer,
    cursive_platform_u32_t size) {
  return GetEnvironmentVariableW(name, buffer, size);
}

static __inline cursive_platform_u32_t cursive_platform_backend_env_get_utf8(
    const char* name,
    char* buffer,
    cursive_platform_u32_t size) {
  return GetEnvironmentVariableA(name, buffer, size);
}

static __inline cursive_platform_u32_t c0_windows_utf8_query_from_wide(
    const wchar_t* text,
    cursive_platform_u32_t text_len,
    char* buffer,
    cursive_platform_u32_t size) {
  if (!text && text_len != 0u) {
    return 0u;
  }
  if (text_len > (cursive_platform_u32_t)INT_MAX) {
    return 0u;
  }

  int bytes = WideCharToMultiByte(CP_UTF8,
                                  0,
                                  text,
                                  (int)text_len,
                                  NULL,
                                  0,
                                  NULL,
                                  NULL);
  if (bytes < 0) {
    return 0u;
  }

  cursive_platform_u32_t required = (cursive_platform_u32_t)bytes + 1u;
  if (!buffer || size < required) {
    return required;
  }

  if (bytes > 0) {
    int written = WideCharToMultiByte(CP_UTF8,
                                      0,
                                      text,
                                      (int)text_len,
                                      buffer,
                                      (int)(required - 1u),
                                      NULL,
                                      NULL);
    if (written != bytes) {
      return 0u;
    }
  }
  buffer[bytes] = '\0';
  return (cursive_platform_u32_t)bytes;
}

static __inline cursive_platform_u32_t
cursive_platform_backend_executable_path_utf8(
    char* buffer,
    cursive_platform_u32_t size) {
  cursive_platform_u32_t capacity = 260u;
  for (;;) {
    wchar_t* wide = (wchar_t*)HeapAlloc(GetProcessHeap(),
                                        0u,
                                        sizeof(wchar_t) * capacity);
    if (!wide) {
      return 0u;
    }

    cursive_platform_u32_t written =
        GetModuleFileNameW(NULL, wide, capacity);
    if (written == 0u) {
      HeapFree(GetProcessHeap(), 0u, wide);
      return 0u;
    }

    if (written < capacity - 1u) {
      cursive_platform_u32_t result =
          c0_windows_utf8_query_from_wide(wide, written, buffer, size);
      HeapFree(GetProcessHeap(), 0u, wide);
      return result;
    }

    HeapFree(GetProcessHeap(), 0u, wide);
    if (capacity >= 32768u) {
      return 0u;
    }
    capacity *= 2u;
  }
}

static __inline const wchar_t* c0_windows_skip_argument_space(
    const wchar_t* cursor) {
  while (cursor && (*cursor == L' ' || *cursor == L'\t')) {
    ++cursor;
  }
  return cursor;
}

static __inline void c0_windows_emit_arg_char(
    wchar_t ch,
    wchar_t* out,
    cursive_platform_u32_t out_capacity,
    cursive_platform_u32_t* out_len) {
  if (out && *out_len < out_capacity) {
    out[*out_len] = ch;
  }
  *out_len += 1u;
}

static __inline const wchar_t* c0_windows_parse_argument(
    const wchar_t* cursor,
    wchar_t* out,
    cursive_platform_u32_t out_capacity,
    cursive_platform_u32_t* out_len) {
  int in_quotes = 0;
  cursive_platform_u32_t produced = 0u;

  while (cursor && *cursor) {
    cursive_platform_u32_t slashes = 0u;
    if (!in_quotes && (*cursor == L' ' || *cursor == L'\t')) {
      break;
    }

    while (*cursor == L'\\') {
      ++slashes;
      ++cursor;
    }

    if (*cursor == L'"') {
      for (cursive_platform_u32_t i = 0u; i < slashes / 2u; ++i) {
        c0_windows_emit_arg_char(L'\\', out, out_capacity, &produced);
      }
      if ((slashes % 2u) == 0u) {
        if (in_quotes && cursor[1] == L'"') {
          c0_windows_emit_arg_char(L'"', out, out_capacity, &produced);
          ++cursor;
        } else {
          in_quotes = !in_quotes;
        }
      } else {
        c0_windows_emit_arg_char(L'"', out, out_capacity, &produced);
      }
      ++cursor;
      continue;
    }

    for (cursive_platform_u32_t i = 0u; i < slashes; ++i) {
      c0_windows_emit_arg_char(L'\\', out, out_capacity, &produced);
    }
    if (!*cursor || (!in_quotes && (*cursor == L' ' || *cursor == L'\t'))) {
      break;
    }
    c0_windows_emit_arg_char(*cursor, out, out_capacity, &produced);
    ++cursor;
  }

  if (out_len) {
    *out_len = produced;
  }
  return cursor;
}

static __inline cursive_platform_uptr_t
cursive_platform_backend_argument_count(void) {
  const wchar_t* cursor = c0_windows_skip_argument_space(GetCommandLineW());
  cursive_platform_uptr_t count = 0u;

  while (cursor && *cursor) {
    cursor = c0_windows_parse_argument(cursor, NULL, 0u, NULL);
    count += 1u;
    cursor = c0_windows_skip_argument_space(cursor);
  }

  return count > 0u ? count - 1u : 0u;
}

static __inline cursive_platform_u32_t
cursive_platform_backend_argument_utf8(
    cursive_platform_uptr_t index,
    char* buffer,
    cursive_platform_u32_t size) {
  const wchar_t* cursor = c0_windows_skip_argument_space(GetCommandLineW());
  cursive_platform_uptr_t ordinal = 0u;

  while (cursor && *cursor) {
    cursive_platform_u32_t arg_len = 0u;
    const wchar_t* next =
        c0_windows_parse_argument(cursor, NULL, 0u, &arg_len);
    if (ordinal == index + 1u) {
      wchar_t* wide = (wchar_t*)HeapAlloc(GetProcessHeap(),
                                          0u,
                                          sizeof(wchar_t) * (arg_len + 1u));
      if (!wide) {
        return 0u;
      }
      cursive_platform_u32_t written_len = 0u;
      c0_windows_parse_argument(cursor, wide, arg_len, &written_len);
      wide[written_len] = 0;
      cursive_platform_u32_t result =
          c0_windows_utf8_query_from_wide(wide, written_len, buffer, size);
      HeapFree(GetProcessHeap(), 0u, wide);
      return result;
    }
    ordinal += 1u;
    cursor = c0_windows_skip_argument_space(next);
  }

  return 0u;
}

static __inline cursive_platform_u32_t
cursive_platform_backend_current_directory_utf8(
    char* buffer,
    cursive_platform_u32_t size) {
  cursive_platform_u32_t required = GetCurrentDirectoryW(0u, NULL);
  if (required == 0u) {
    return 0u;
  }

  wchar_t* wide =
      (wchar_t*)HeapAlloc(GetProcessHeap(), 0u, sizeof(wchar_t) * required);
  if (!wide) {
    return 0u;
  }

  cursive_platform_u32_t written = GetCurrentDirectoryW(required, wide);
  if (written == 0u || written >= required) {
    HeapFree(GetProcessHeap(), 0u, wide);
    return 0u;
  }

  cursive_platform_u32_t result =
      c0_windows_utf8_query_from_wide(wide, written, buffer, size);
  HeapFree(GetProcessHeap(), 0u, wide);
  return result;
}

static __inline void cursive_platform_backend_icu_data_configure(void) {}

static __inline int cursive_platform_backend_utf8_to_wide_chars(
    const char* source,
    int source_length,
    wchar_t* destination,
    int destination_length) {
  return MultiByteToWideChar(CP_UTF8,
                             MB_ERR_INVALID_CHARS,
                             source,
                             source_length,
                             destination,
                             destination_length);
}

static __inline int cursive_platform_backend_wide_to_utf8_chars(
    const wchar_t* source,
    int source_length,
    char* destination,
    int destination_length) {
  return WideCharToMultiByte(CP_UTF8,
                             0,
                             source,
                             source_length,
                             destination,
                             destination_length,
                             NULL,
                             NULL);
}

static __inline cursive_platform_bool_t cursive_platform_backend_process_create(
    const wchar_t* application_name,
    wchar_t* command_line,
    void* process_attributes,
    void* thread_attributes,
    cursive_platform_bool_t inherit_handles,
    cursive_platform_u32_t creation_flags,
    void* environment,
    const wchar_t* current_directory,
    cursive_platform_process_startup_t* startup_info,
    cursive_platform_process_info_t* process_information) {
  STARTUPINFOW native_startup = {0};
  PROCESS_INFORMATION native_process_info = {0};
  STARTUPINFOW* native_startup_ptr = NULL;
  PROCESS_INFORMATION* native_process_info_ptr = NULL;

  if (startup_info) {
    native_startup.cb = startup_info->size_bytes;
    native_startup.dwFlags = startup_info->flags;
    native_startup.hStdInput = startup_info->stdin_handle;
    native_startup.hStdOutput = startup_info->stdout_handle;
    native_startup.hStdError = startup_info->stderr_handle;
    native_startup_ptr = &native_startup;
  }

  if (process_information) {
    native_process_info.hProcess = CURSIVE_PLATFORM_BACKEND_INVALID_HANDLE;
    native_process_info.hThread = CURSIVE_PLATFORM_BACKEND_INVALID_HANDLE;
    native_process_info.dwProcessId = 0;
    native_process_info.dwThreadId = 0;
    native_process_info_ptr = &native_process_info;
  }

  cursive_platform_bool_t ok = CreateProcessW(application_name,
                                              command_line,
                                              process_attributes,
                                              thread_attributes,
                                              inherit_handles,
                                              creation_flags,
                                              environment,
                                              current_directory,
                                              native_startup_ptr,
                                              native_process_info_ptr);

  if (ok && process_information) {
    process_information->process_handle = native_process_info.hProcess;
    process_information->thread_handle = native_process_info.hThread;
    process_information->process_id = native_process_info.dwProcessId;
    process_information->thread_id = native_process_info.dwThreadId;
  }
  return ok;
}

static __inline cursive_platform_u32_t cursive_platform_backend_wait_one(
    cursive_platform_handle_t handle,
    cursive_platform_u32_t milliseconds) {
  return WaitForSingleObject(handle, milliseconds);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_process_exit_code(cursive_platform_handle_t handle,
                                           cursive_platform_u32_t* exit_code) {
  return GetExitCodeProcess(handle, exit_code);
}

static __inline cursive_platform_bool_t cursive_platform_backend_close_handle(
    cursive_platform_handle_t handle) {
  return CloseHandle(handle);
}

static __inline cursive_platform_module_t cursive_platform_backend_module_get(
    const char* name) {
  return GetModuleHandleA(name);
}

static __inline cursive_platform_module_t cursive_platform_backend_module_load(
    const char* name) {
  return LoadLibraryA(name);
}

static __inline void* cursive_platform_backend_module_symbol(
    cursive_platform_module_t module,
    const char* symbol_name) {
  return GetProcAddress(module, symbol_name);
}

static __inline cursive_platform_bool_t cursive_platform_backend_once_execute(
    cursive_platform_once_t* init_once,
    cursive_platform_once_callback_t init_fn,
    void* parameter,
    void** context) {
  return InitOnceExecuteOnce(init_once, init_fn, parameter, context);
}

static __inline cursive_platform_tls_key_t
cursive_platform_backend_tls_key_create(void) {
  return TlsAlloc();
}

static __inline void* cursive_platform_backend_tls_get(
    cursive_platform_tls_key_t index) {
  return TlsGetValue(index);
}

static __inline cursive_platform_bool_t cursive_platform_backend_tls_set(
    cursive_platform_tls_key_t index,
    void* value) {
  return TlsSetValue(index, value);
}

static __inline void cursive_platform_backend_mutex_init(
    cursive_platform_mutex_t* mutex) {
  InitializeCriticalSection(mutex);
}

static __inline void cursive_platform_backend_mutex_destroy(
    cursive_platform_mutex_t* mutex) {
  DeleteCriticalSection(mutex);
}

static __inline void cursive_platform_backend_mutex_lock(
    cursive_platform_mutex_t* mutex) {
  EnterCriticalSection(mutex);
}

static __inline void cursive_platform_backend_mutex_unlock(
    cursive_platform_mutex_t* mutex) {
  LeaveCriticalSection(mutex);
}

static __inline void cursive_platform_backend_condition_init(
    cursive_platform_condition_t* condition) {
  InitializeConditionVariable(condition);
}

static __inline cursive_platform_bool_t cursive_platform_backend_condition_wait(
    cursive_platform_condition_t* condition,
    cursive_platform_mutex_t* mutex,
    cursive_platform_u32_t milliseconds) {
  return SleepConditionVariableCS(condition, mutex, milliseconds);
}

static __inline void cursive_platform_backend_condition_wake_one(
    cursive_platform_condition_t* condition) {
  WakeConditionVariable(condition);
}

static __inline void cursive_platform_backend_condition_wake_all(
    cursive_platform_condition_t* condition) {
  WakeAllConditionVariable(condition);
}

static __inline void cursive_platform_backend_rwlock_init(
    cursive_platform_rwlock_t* lock) {
  InitializeSRWLock(lock);
}

static __inline void cursive_platform_backend_rwlock_lock_exclusive(
    cursive_platform_rwlock_t* lock) {
  AcquireSRWLockExclusive(lock);
}

static __inline void cursive_platform_backend_rwlock_unlock_exclusive(
    cursive_platform_rwlock_t* lock) {
  ReleaseSRWLockExclusive(lock);
}

static __inline void cursive_platform_backend_rwlock_lock_shared(
    cursive_platform_rwlock_t* lock) {
  AcquireSRWLockShared(lock);
}

static __inline void cursive_platform_backend_rwlock_unlock_shared(
    cursive_platform_rwlock_t* lock) {
  ReleaseSRWLockShared(lock);
}

static __inline cursive_platform_i32_t cursive_platform_backend_atomic_exchange(
    volatile cursive_platform_i32_t* target,
    cursive_platform_i32_t value) {
  return InterlockedExchange(target, value);
}

static __inline cursive_platform_i32_t
cursive_platform_backend_atomic_compare_exchange(
    volatile cursive_platform_i32_t* target,
    cursive_platform_i32_t exchange,
    cursive_platform_i32_t comparand) {
  return InterlockedCompareExchange(target, exchange, comparand);
}

static __inline cursive_platform_i64_t
cursive_platform_backend_atomic_increment64(
    volatile cursive_platform_i64_t* target) {
  return InterlockedIncrement64(target);
}

static __inline cursive_platform_thread_id_t
cursive_platform_backend_current_thread_id(void) {
  return GetCurrentThreadId();
}

static __inline cursive_platform_u32_t
cursive_platform_backend_current_process_id(void) {
  return GetCurrentProcessId();
}

static __inline cursive_platform_handle_t
cursive_platform_backend_current_thread(void) {
  return GetCurrentThread();
}

static __inline int cursive_platform_backend_thread_priority_get(
    cursive_platform_handle_t thread) {
  return GetThreadPriority(thread);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_thread_priority_set(cursive_platform_handle_t thread,
                                             int priority) {
  return SetThreadPriority(thread, priority);
}

static __inline cursive_platform_uptr_t
cursive_platform_backend_thread_affinity_set(cursive_platform_handle_t thread,
                                             cursive_platform_uptr_t mask) {
  return SetThreadAffinityMask(thread, mask);
}

static __inline cursive_platform_handle_t
cursive_platform_backend_thread_create(
    void* attributes,
    size_t stack_size,
    cursive_platform_thread_start_routine_t start_routine,
    void* parameter,
    cursive_platform_u32_t creation_flags,
    cursive_platform_u32_t* thread_id) {
  return CreateThread(attributes,
                      stack_size,
                      start_routine,
                      parameter,
                      creation_flags,
                      thread_id);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_thread_exit_code(cursive_platform_handle_t handle,
                                          cursive_platform_u32_t* exit_code) {
  return GetExitCodeThread(handle, exit_code);
}

static __inline cursive_platform_handle_t cursive_platform_backend_event_create(
    void* attributes,
    cursive_platform_bool_t manual_reset,
    cursive_platform_bool_t initial_state,
    const wchar_t* name) {
  return CreateEventW(attributes, manual_reset, initial_state, name);
}

static __inline cursive_platform_bool_t cursive_platform_backend_event_set(
    cursive_platform_handle_t handle) {
  return SetEvent(handle);
}

static __inline cursive_platform_bool_t cursive_platform_backend_event_reset(
    cursive_platform_handle_t handle) {
  return ResetEvent(handle);
}

static __inline cursive_platform_handle_t cursive_platform_backend_std_handle(
    cursive_platform_u32_t std_handle_id) {
  return GetStdHandle(std_handle_id);
}

static __inline cursive_platform_bool_t cursive_platform_backend_std_handle_set(
    cursive_platform_u32_t std_handle_id,
    cursive_platform_handle_t handle) {
  return SetStdHandle(std_handle_id, handle);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_console_mode_get(cursive_platform_handle_t handle,
                                          cursive_platform_u32_t* mode) {
  return GetConsoleMode(handle, mode);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_console_write_utf8(
    cursive_platform_handle_t handle,
    const void* buffer,
    cursive_platform_u32_t chars_to_write,
    cursive_platform_u32_t* chars_written) {
  return WriteConsoleA(handle, buffer, chars_to_write, chars_written, NULL);
}

static __inline cursive_platform_bool_t cursive_platform_backend_handle_write(
    cursive_platform_handle_t handle,
    const void* buffer,
    cursive_platform_u32_t bytes_to_write,
    cursive_platform_u32_t* bytes_written) {
  return WriteFile(handle, buffer, bytes_to_write, bytes_written, NULL);
}

static __inline cursive_platform_bool_t cursive_platform_backend_handle_read(
    cursive_platform_handle_t handle,
    void* buffer,
    cursive_platform_u32_t bytes_to_read,
    cursive_platform_u32_t* bytes_read) {
  return ReadFile(handle, buffer, bytes_to_read, bytes_read, NULL);
}

static __inline cursive_platform_handle_t
cursive_platform_backend_file_open_wide(
    const wchar_t* path,
    cursive_platform_u32_t desired_access,
    cursive_platform_u32_t share_mode,
    void* security_attributes,
    cursive_platform_u32_t creation_disposition,
    cursive_platform_u32_t flags_and_attributes,
    cursive_platform_handle_t template_file) {
  return CreateFileW(path,
                     desired_access,
                     share_mode,
                     security_attributes,
                     creation_disposition,
                     flags_and_attributes,
                     template_file);
}

static __inline cursive_platform_bool_t cursive_platform_backend_handle_flush(
    cursive_platform_handle_t handle) {
  return FlushFileBuffers(handle);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_file_size_get(cursive_platform_handle_t handle,
                                       cursive_platform_large_integer_t* size) {
  LARGE_INTEGER native_size;
  cursive_platform_bool_t ok = GetFileSizeEx(handle, &native_size);
  if (ok && size) {
    size->quad_part = native_size.QuadPart;
  }
  return ok;
}

static __inline cursive_platform_bool_t
cursive_platform_backend_file_pointer_set(
    cursive_platform_handle_t handle,
    cursive_platform_large_integer_t distance,
    cursive_platform_large_integer_t* new_position,
    cursive_platform_u32_t move_method) {
  LARGE_INTEGER native_distance;
  LARGE_INTEGER native_new_position;
  native_distance.QuadPart = distance.quad_part;
  cursive_platform_bool_t ok =
      SetFilePointerEx(handle, native_distance,
                       new_position ? &native_new_position : NULL, move_method);
  if (ok && new_position) {
    new_position->quad_part = native_new_position.QuadPart;
  }
  return ok;
}

static __inline cursive_platform_u32_t
cursive_platform_backend_file_attributes_get_wide(const wchar_t* path) {
  return GetFileAttributesW(path);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_file_delete_wide(const wchar_t* path) {
  return DeleteFileW(path);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_directory_remove_wide(const wchar_t* path) {
  return RemoveDirectoryW(path);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_directory_create_wide(const wchar_t* path,
                                               void* security_attributes) {
  return CreateDirectoryW(path, security_attributes);
}

static __inline cursive_platform_u32_t
cursive_platform_backend_temp_path_get_wide(cursive_platform_u32_t buffer_length,
                                            wchar_t* buffer) {
  return GetTempPathW(buffer_length, buffer);
}

static __inline cursive_platform_uint_t
cursive_platform_backend_temp_file_name_wide(const wchar_t* path_name,
                                             const wchar_t* prefix_string,
                                             cursive_platform_uint_t unique,
                                             wchar_t* temp_file_name) {
  return GetTempFileNameW(path_name, prefix_string, unique, temp_file_name);
}

static __inline cursive_platform_handle_t
cursive_platform_backend_find_first_wide(const wchar_t* pattern,
                                         cursive_platform_find_data_t* find) {
  WIN32_FIND_DATAW native_find_data;
  cursive_platform_handle_t handle =
      FindFirstFileW(pattern, find ? &native_find_data : NULL);
  if (handle != CURSIVE_PLATFORM_BACKEND_INVALID_HANDLE && find) {
    find->file_attributes = native_find_data.dwFileAttributes;
    for (size_t i = 0;
         i < (sizeof(find->file_name) / sizeof(find->file_name[0]));
         ++i) {
      find->file_name[i] = native_find_data.cFileName[i];
      if (native_find_data.cFileName[i] == 0) {
        break;
      }
    }
  }
  return handle;
}

static __inline cursive_platform_bool_t cursive_platform_backend_find_next(
    cursive_platform_handle_t handle,
    cursive_platform_find_data_t* find) {
  WIN32_FIND_DATAW native_find_data;
  cursive_platform_bool_t ok =
      FindNextFileW(handle, find ? &native_find_data : NULL);
  if (ok && find) {
    find->file_attributes = native_find_data.dwFileAttributes;
    for (size_t i = 0;
         i < (sizeof(find->file_name) / sizeof(find->file_name[0]));
         ++i) {
      find->file_name[i] = native_find_data.cFileName[i];
      if (native_find_data.cFileName[i] == 0) {
        break;
      }
    }
  }
  return ok;
}

static __inline cursive_platform_bool_t cursive_platform_backend_find_close(
    cursive_platform_handle_t handle) {
  return FindClose(handle);
}

static __inline cursive_platform_handle_t
cursive_platform_backend_mapping_create(
    cursive_platform_handle_t file,
    void* attributes,
    cursive_platform_u32_t protect,
    cursive_platform_u32_t maximum_size_high,
    cursive_platform_u32_t maximum_size_low,
    const wchar_t* name) {
  return CreateFileMappingW(file,
                            attributes,
                            protect,
                            maximum_size_high,
                            maximum_size_low,
                            name);
}

static __inline void* cursive_platform_backend_mapping_view(
    cursive_platform_handle_t mapping,
    cursive_platform_u32_t desired_access,
    cursive_platform_u32_t file_offset_high,
    cursive_platform_u32_t file_offset_low,
    size_t number_of_bytes_to_map) {
  return MapViewOfFile(mapping,
                       desired_access,
                       file_offset_high,
                       file_offset_low,
                       number_of_bytes_to_map);
}

static __inline cursive_platform_bool_t
cursive_platform_backend_mapping_unview(const void* base_address) {
  return UnmapViewOfFile(base_address);
}

static __inline void cursive_platform_backend_debug_break(void) {
  DebugBreak();
}

static __inline void cursive_platform_backend_system_time_filetime(
    cursive_platform_filetime_t* file_time) {
  FILETIME native_file_time;
  GetSystemTimeAsFileTime(&native_file_time);
  if (file_time) {
    file_time->low_date_time = native_file_time.dwLowDateTime;
    file_time->high_date_time = native_file_time.dwHighDateTime;
  }
}

cursive_platform_bool_t cursive_platform_backend_panic_boundary_active(void);

static __inline cursive_platform_u32_t
cursive_platform_windows_panic_code_from_exception(
    const EXCEPTION_POINTERS* exception_info) {
  if (!exception_info || !exception_info->ExceptionRecord) {
    return 0u;
  }
  if (exception_info->ExceptionRecord->NumberParameters < 1u) {
    return 0u;
  }
  return (cursive_platform_u32_t)
      exception_info->ExceptionRecord->ExceptionInformation[0];
}

static __inline int cursive_platform_windows_handle_panic_filter(
    cursive_platform_u32_t* panic_code,
    const EXCEPTION_POINTERS* exception_info) {
  if (panic_code) {
    *panic_code =
        cursive_platform_windows_panic_code_from_exception(exception_info);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

cursive_platform_bool_t cursive_platform_backend_panic_boundary_run(
    cursive_rt_panic_boundary_body_t body,
    void* context,
    cursive_platform_u32_t* panic_code);

void cursive_platform_backend_panic_boundary_raise(
    cursive_platform_u32_t panic_code);

#endif  // CURSIVE_RT_PLATFORM_WINDOWS_API_H
