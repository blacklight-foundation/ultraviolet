#include "../internal/rt_internal.h"

static UVStringView uv_system_empty_string_view(void) {
  UVStringView out;
  out.data = NULL;
  out.len = 0;
  return out;
}

static UVStringView uv_system_query_string_view(
    uv_rt_dword_t (*query)(char*, uv_rt_dword_t)) {
  uv_rt_dword_t required = query(NULL, 0u);
  if (required == 0u) {
    return uv_system_empty_string_view();
  }

  char* text = (char*)uv_heap_alloc_raw(required);
  if (!text) {
    return uv_system_empty_string_view();
  }

  uv_rt_dword_t written = query(text, required);
  if (written >= required) {
    uv_heap_free_raw(text);
    return uv_system_empty_string_view();
  }

  UVStringView out;
  out.data = (const uint8_t*)text;
  out.len = written;
  return out;
}

static UVStringView uv_system_get_env_none(void) {
  uv_trace_emit_rule("System-GetEnv-None");
  return uv_system_empty_string_view();
}

void ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexit(int32_t code) {
  uv_trace_emit_rule("System-Exit");
  uv_trace_emit_rule("Prim-System-Exit");
  uv_rt_exit_process((uv_rt_uint_t)code);
  for (;;) {
  }
}

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aget_x5fenv(
    const UVStringView* key) {
  if (!key || !key->data || key->len == 0) {
    return uv_system_get_env_none();
  }

  char* key_utf8 = (char*)uv_heap_alloc_raw((size_t)key->len + 1u);
  if (!key_utf8) {
    uv_trace_emit_rule("Prim-System-GetEnv");
    return uv_system_empty_string_view();
  }
  uv_memcpy(key_utf8, key->data, key->len);
  key_utf8[key->len] = '\0';

  uv_rt_last_error_set(UV_RT_ERROR_SUCCESS);
  uv_rt_u32_t required = uv_rt_env_query_utf8(key_utf8, NULL, 0);
  if (required == 0) {
    uv_rt_u32_t env_error = uv_rt_last_error_get();
    uv_heap_free_raw(key_utf8);
    if (env_error == UV_RT_ERROR_ENVVAR_NOT_FOUND) {
      return uv_system_get_env_none();
    }
    if (env_error == UV_RT_ERROR_SUCCESS) {
      uv_trace_emit_rule("System-GetEnv-Ok");
      return uv_system_empty_string_view();
    }
    uv_trace_emit_rule("Prim-System-GetEnv");
    return uv_system_empty_string_view();
  }

  char* value_utf8 =
      (char*)uv_heap_alloc_raw(((size_t)required) * sizeof(char));
  if (!value_utf8) {
    uv_heap_free_raw(key_utf8);
    return uv_system_empty_string_view();
  }

  uv_rt_u32_t written =
      uv_rt_env_query_utf8(key_utf8, value_utf8, required);
  uv_heap_free_raw(key_utf8);
  if (written == 0 || written >= required) {
    uv_trace_emit_rule("Prim-System-GetEnv");
    uv_heap_free_raw(value_utf8);
    return uv_system_empty_string_view();
  }

  UVStringView out;
  out.data = (uint8_t*)value_utf8;
  out.len = written;
  uv_trace_emit_rule("System-GetEnv-Ok");
  return out;
}

UVStringView
ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexecutable_x5fpath(void) {
  UVStringView out =
      uv_system_query_string_view(uv_rt_executable_path_query_utf8);
  uv_trace_emit_rule("System-ExecutablePath");
  uv_trace_emit_rule("Prim-System-ExecutablePath");
  return out;
}

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument_x5fcount(void) {
  uv_rt_uptr_t count = uv_rt_argument_count_query();
  uv_trace_emit_rule("System-ArgumentCount");
  uv_trace_emit_rule("Prim-System-ArgumentCount");
  return (uint64_t)count;
}

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument(
    const uint64_t* index) {
  if (!index || *index > (uint64_t)((uv_rt_uptr_t)-1)) {
    return uv_system_empty_string_view();
  }

  uv_rt_uptr_t argument_index = (uv_rt_uptr_t)*index;
  uv_rt_dword_t required =
      uv_rt_argument_query_utf8(argument_index, NULL, 0u);
  if (required == 0u) {
    return uv_system_empty_string_view();
  }

  char* text = (char*)uv_heap_alloc_raw(required);
  if (!text) {
    return uv_system_empty_string_view();
  }

  uv_rt_dword_t written =
      uv_rt_argument_query_utf8(argument_index, text, required);
  if (written >= required) {
    uv_heap_free_raw(text);
    return uv_system_empty_string_view();
  }

  UVStringView out;
  out.data = (const uint8_t*)text;
  out.len = written;
  uv_trace_emit_rule("System-Argument");
  uv_trace_emit_rule("Prim-System-Argument");
  return out;
}

UVStringView
ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3acurrent_x5fdirectory(void) {
  UVStringView out =
      uv_system_query_string_view(uv_rt_current_directory_query_utf8);
  uv_trace_emit_rule("System-CurrentDirectory");
  uv_trace_emit_rule("Prim-System-CurrentDirectory");
  return out;
}

int32_t ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3arun(
    const UVStringView* command) {
  if (!command || !command->data || command->len == 0) {
    return -1;
  }

  uint32_t command_wide_len = 0;
  wchar_t* command_wide =
      uv_utf8_to_wide(command->data, command->len, &command_wide_len);
  if (!command_wide) {
    return -1;
  }

  uv_rt_process_launch_t si;
  uv_rt_process_t pi;
  uv_memset(&si, 0, sizeof(si));
  uv_memset(&pi, 0, sizeof(pi));
  si.size_bytes = sizeof(si);

  uv_rt_bool_t ok = uv_rt_process_spawn(
      NULL,
      command_wide,
      NULL,
      NULL,
      UV_RT_FALSE,
      0,
      NULL,
      NULL,
      &si,
      &pi);
  uv_heap_free_raw(command_wide);
  if (!ok) {
    return -1;
  }

  uv_rt_wait_result_t wait_result =
      uv_rt_wait(pi.process_handle, UV_RT_WAIT_FOREVER);
  uv_rt_u32_t exit_code = 0;
  uv_rt_bool_t got_exit_code =
      uv_rt_process_exit_status(pi.process_handle, &exit_code);

  uv_rt_handle_release(pi.thread_handle);
  uv_rt_handle_release(pi.process_handle);

  if (wait_result != UV_RT_WAIT_SIGNALED || !got_exit_code) {
    return -1;
  }

  uv_trace_emit_rule("System-Run");
  uv_trace_emit_rule("Prim-System-Run");
  return (int32_t)exit_code;
}
