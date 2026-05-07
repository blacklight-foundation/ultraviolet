#include "../internal/rt_internal.h"

static C0StringView cursive_system_empty_string_view(void) {
  C0StringView out;
  out.data = NULL;
  out.len = 0;
  return out;
}

static C0StringView cursive_system_query_string_view(
    cursive_rt_dword_t (*query)(char*, cursive_rt_dword_t)) {
  cursive_rt_dword_t required = query(NULL, 0u);
  if (required == 0u) {
    return cursive_system_empty_string_view();
  }

  char* text = (char*)c0_heap_alloc_raw(required);
  if (!text) {
    return cursive_system_empty_string_view();
  }

  cursive_rt_dword_t written = query(text, required);
  if (written >= required) {
    c0_heap_free_raw(text);
    return cursive_system_empty_string_view();
  }

  C0StringView out;
  out.data = (const uint8_t*)text;
  out.len = written;
  return out;
}

static C0StringView cursive_system_get_env_none(void) {
  c0_trace_emit_rule("System-GetEnv-None");
  return cursive_system_empty_string_view();
}

void cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexit(int32_t code) {
  c0_trace_emit_rule("System-Exit");
  c0_trace_emit_rule("Prim-System-Exit");
  cursive_rt_exit_process((cursive_rt_uint_t)code);
  for (;;) {
  }
}

C0StringView cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aget_x5fenv(
    const C0StringView* key) {
  if (!key || !key->data || key->len == 0) {
    return cursive_system_get_env_none();
  }

  char* key_utf8 = (char*)c0_heap_alloc_raw((size_t)key->len + 1u);
  if (!key_utf8) {
    c0_trace_emit_rule("Prim-System-GetEnv");
    return cursive_system_empty_string_view();
  }
  c0_memcpy(key_utf8, key->data, key->len);
  key_utf8[key->len] = '\0';

  cursive_rt_last_error_set(CURSIVE_RT_ERROR_SUCCESS);
  cursive_rt_u32_t required = cursive_rt_env_query_utf8(key_utf8, NULL, 0);
  if (required == 0) {
    cursive_rt_u32_t env_error = cursive_rt_last_error_get();
    c0_heap_free_raw(key_utf8);
    if (env_error == CURSIVE_RT_ERROR_ENVVAR_NOT_FOUND) {
      return cursive_system_get_env_none();
    }
    if (env_error == CURSIVE_RT_ERROR_SUCCESS) {
      c0_trace_emit_rule("System-GetEnv-Ok");
      return cursive_system_empty_string_view();
    }
    c0_trace_emit_rule("Prim-System-GetEnv");
    return cursive_system_empty_string_view();
  }

  char* value_utf8 =
      (char*)c0_heap_alloc_raw(((size_t)required) * sizeof(char));
  if (!value_utf8) {
    c0_heap_free_raw(key_utf8);
    return cursive_system_empty_string_view();
  }

  cursive_rt_u32_t written =
      cursive_rt_env_query_utf8(key_utf8, value_utf8, required);
  c0_heap_free_raw(key_utf8);
  if (written == 0 || written >= required) {
    c0_trace_emit_rule("Prim-System-GetEnv");
    c0_heap_free_raw(value_utf8);
    return cursive_system_empty_string_view();
  }

  C0StringView out;
  out.data = (uint8_t*)value_utf8;
  out.len = written;
  c0_trace_emit_rule("System-GetEnv-Ok");
  return out;
}

C0StringView
cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexecutable_x5fpath(void) {
  C0StringView out =
      cursive_system_query_string_view(cursive_rt_executable_path_query_utf8);
  c0_trace_emit_rule("System-ExecutablePath");
  c0_trace_emit_rule("Prim-System-ExecutablePath");
  return out;
}

uint64_t cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument_x5fcount(void) {
  cursive_rt_uptr_t count = cursive_rt_argument_count_query();
  c0_trace_emit_rule("System-ArgumentCount");
  c0_trace_emit_rule("Prim-System-ArgumentCount");
  return (uint64_t)count;
}

C0StringView cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument(
    const uint64_t* index) {
  if (!index || *index > (uint64_t)((cursive_rt_uptr_t)-1)) {
    return cursive_system_empty_string_view();
  }

  cursive_rt_uptr_t argument_index = (cursive_rt_uptr_t)*index;
  cursive_rt_dword_t required =
      cursive_rt_argument_query_utf8(argument_index, NULL, 0u);
  if (required == 0u) {
    return cursive_system_empty_string_view();
  }

  char* text = (char*)c0_heap_alloc_raw(required);
  if (!text) {
    return cursive_system_empty_string_view();
  }

  cursive_rt_dword_t written =
      cursive_rt_argument_query_utf8(argument_index, text, required);
  if (written >= required) {
    c0_heap_free_raw(text);
    return cursive_system_empty_string_view();
  }

  C0StringView out;
  out.data = (const uint8_t*)text;
  out.len = written;
  c0_trace_emit_rule("System-Argument");
  c0_trace_emit_rule("Prim-System-Argument");
  return out;
}

C0StringView
cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3acurrent_x5fdirectory(void) {
  C0StringView out =
      cursive_system_query_string_view(cursive_rt_current_directory_query_utf8);
  c0_trace_emit_rule("System-CurrentDirectory");
  c0_trace_emit_rule("Prim-System-CurrentDirectory");
  return out;
}

int32_t cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3arun(
    const C0StringView* command) {
  if (!command || !command->data || command->len == 0) {
    return -1;
  }

  uint32_t command_wide_len = 0;
  wchar_t* command_wide =
      c0_utf8_to_wide(command->data, command->len, &command_wide_len);
  if (!command_wide) {
    return -1;
  }

  cursive_rt_process_launch_t si;
  cursive_rt_process_t pi;
  c0_memset(&si, 0, sizeof(si));
  c0_memset(&pi, 0, sizeof(pi));
  si.size_bytes = sizeof(si);

  cursive_rt_bool_t ok = cursive_rt_process_spawn(
      NULL,
      command_wide,
      NULL,
      NULL,
      CURSIVE_RT_FALSE,
      0,
      NULL,
      NULL,
      &si,
      &pi);
  c0_heap_free_raw(command_wide);
  if (!ok) {
    return -1;
  }

  cursive_rt_wait_result_t wait_result =
      cursive_rt_wait(pi.process_handle, CURSIVE_RT_WAIT_FOREVER);
  cursive_rt_u32_t exit_code = 0;
  cursive_rt_bool_t got_exit_code =
      cursive_rt_process_exit_status(pi.process_handle, &exit_code);

  cursive_rt_handle_release(pi.thread_handle);
  cursive_rt_handle_release(pi.process_handle);

  if (wait_result != CURSIVE_RT_WAIT_SIGNALED || !got_exit_code) {
    return -1;
  }

  c0_trace_emit_rule("System-Run");
  c0_trace_emit_rule("Prim-System-Run");
  return (int32_t)exit_code;
}
