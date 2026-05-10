#include "../internal/rt_internal.h"

#define C0_TRACE_SINK_CONSOLE 0u
#define C0_TRACE_SINK_FILE 1u
#define C0_TRACE_SINK_BOTH 2u
#define C0_TRACE_FLUSH_INTERVAL 8192u
#define C0_TRACE_FILTER_LOG 0x1u
#define C0_TRACE_FILTER_DIAGNOSTIC 0x2u
#define C0_TRACE_FILTER_RUNTIME 0x4u
#define C0_TRACE_FILTER_ALL \
  (C0_TRACE_FILTER_LOG | C0_TRACE_FILTER_DIAGNOSTIC | C0_TRACE_FILTER_RUNTIME)
#define C0_TRACE_LEVEL_TRACE 0u
#define C0_TRACE_LEVEL_INFO 1u
#define C0_TRACE_LEVEL_WARNING 2u
#define C0_TRACE_LEVEL_ERROR 3u

static cursive_platform_handle_t c0_trace_file_handle = NULL;
static cursive_platform_handle_t c0_trace_console_handle = NULL;
static cursive_platform_i32_t c0_trace_init = 0;
static volatile cursive_platform_i32_t c0_trace_enabled = 0;
static int c0_trace_file_sink = 0;
static int c0_trace_console_sink = 0;
static int c0_trace_policy_configured = 0;
static uint8_t c0_trace_policy_sink_kind = C0_TRACE_SINK_CONSOLE;
static uint8_t *c0_trace_policy_path_utf8 = NULL;
static uint64_t c0_trace_policy_path_len = 0;
static uint8_t *c0_trace_root_utf8 = NULL;
static uint64_t c0_trace_root_len = 0;
static volatile cursive_platform_i32_t c0_trace_filter_mask =
    (cursive_platform_i32_t)C0_TRACE_FILTER_ALL;
static volatile cursive_platform_i32_t c0_trace_min_level = 0;
static uint8_t *c0_trace_rule_filter_utf8 = NULL;
static uint64_t c0_trace_rule_filter_len = 0u;
static uint8_t *c0_trace_file_filter_utf8 = NULL;
static uint64_t c0_trace_file_filter_len = 0u;
static uint8_t *c0_trace_label_filter_utf8 = NULL;
static uint64_t c0_trace_label_filter_len = 0u;
static volatile cursive_platform_i32_t c0_trace_fail_only = 0;
static volatile cursive_platform_i32_t c0_trace_break_on_fail = 0;
static volatile cursive_platform_i32_t c0_trace_break_latched = 0;
static volatile cursive_platform_i32_t c0_trace_env_loaded = 0;
static uint32_t c0_trace_records_since_flush = 0;
static uint64_t c0_trace_next_sequence = 0u;
static cursive_platform_rwlock_t c0_trace_lock = CURSIVE_PLATFORM_RWLOCK_INIT;

static void c0_conformance_init(void);
static void c0_trace_close_file_sink(void);
static void c0_trace_refresh_console_sink(void);
static int c0_trace_open_file_sink_utf8(const uint8_t *path_utf8,
                                        uint64_t path_len,
                                        cursive_platform_handle_t *out_handle);
static void c0_trace_apply_sink_policy_locked(void);
static void c0_trace_apply_env_overrides_locked(void);

static void c0_trace_close_file_sink(void)
{
  if (c0_trace_file_sink != 0 && c0_trace_file_handle)
  {
    cursive_platform_close_handle(c0_trace_file_handle);
  }
  c0_trace_file_handle = NULL;
  c0_trace_file_sink = 0;
}

static void c0_trace_refresh_console_sink(void)
{
  c0_trace_console_handle = cursive_platform_std_handle(CURSIVE_PLATFORM_STD_ERROR_HANDLE);
  if (c0_trace_console_handle == CURSIVE_PLATFORM_INVALID_HANDLE)
  {
    c0_trace_console_handle = cursive_platform_std_handle(CURSIVE_PLATFORM_STD_OUTPUT_HANDLE);
    if (c0_trace_console_handle == CURSIVE_PLATFORM_INVALID_HANDLE)
    {
      c0_trace_console_handle = NULL;
    }
  }
  c0_trace_console_sink = c0_trace_console_handle != NULL;
}

static int c0_trace_open_file_sink_utf8(const uint8_t *path_utf8,
                                        uint64_t path_len,
                                        cursive_platform_handle_t *out_handle)
{
  if (!out_handle)
  {
    return 0;
  }
  *out_handle = NULL;
  if (!path_utf8 || path_len == 0 || path_len > 0x7FFFFFFFllu)
  {
    return 0;
  }
  if (c0_utf8_has_null(path_utf8, path_len))
  {
    return 0;
  }

  int needed =
      cursive_platform_utf8_to_wide_chars((const char *)path_utf8, (int)path_len,
                                    NULL, 0);
  if (needed <= 0)
  {
    return 0;
  }
  const uint64_t chars = (uint64_t)needed + 1u;
  if (chars > (uint64_t)SIZE_MAX / sizeof(wchar_t))
  {
    return 0;
  }
  wchar_t *wide_path = (wchar_t *)c0_heap_alloc_raw((size_t)(chars * sizeof(wchar_t)));
  if (!wide_path)
  {
    return 0;
  }
  int written = cursive_platform_utf8_to_wide_chars((const char *)path_utf8,
                                              (int)path_len,
                                              wide_path,
                                              needed);
  if (written <= 0 || written != needed)
  {
    c0_heap_free_raw(wide_path);
    return 0;
  }
  wide_path[needed] = L'\0';

  cursive_platform_handle_t file = cursive_platform_file_open_wide(
      wide_path, CURSIVE_PLATFORM_GENERIC_WRITE, CURSIVE_PLATFORM_FILE_SHARE_READ, NULL,
      CURSIVE_PLATFORM_CREATE_ALWAYS, CURSIVE_PLATFORM_FILE_ATTRIBUTE_NORMAL, NULL);
  c0_heap_free_raw(wide_path);
  if (file == CURSIVE_PLATFORM_INVALID_HANDLE)
  {
    return 0;
  }
  *out_handle = file;
  return 1;
}

static void c0_trace_apply_sink_policy_locked(void)
{
  c0_trace_close_file_sink();
  c0_trace_console_sink = 0;
  c0_trace_console_handle = NULL;
  c0_trace_records_since_flush = 0;

  if (c0_trace_policy_configured == 0)
  {
    return;
  }

  int want_file = 0;
  int want_console = 0;
  if (c0_trace_policy_sink_kind == C0_TRACE_SINK_FILE)
  {
    want_file = 1;
  }
  else if (c0_trace_policy_sink_kind == C0_TRACE_SINK_BOTH)
  {
    want_file = 1;
    want_console = 1;
  }
  else
  {
    want_console = 1;
  }

  if (want_file != 0)
  {
    cursive_platform_handle_t sink = NULL;
    if (c0_trace_open_file_sink_utf8(c0_trace_policy_path_utf8,
                                     c0_trace_policy_path_len,
                                     &sink) != 0)
    {
      c0_trace_file_handle = sink;
      c0_trace_file_sink = 1;
    }
    else if (want_console == 0)
    {
      want_console = 1;
    }
  }
  if (want_console != 0)
  {
    c0_trace_refresh_console_sink();
  }
}

static uint8_t c0_ascii_lower(uint8_t c)
{
  if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
  {
    return (uint8_t)(c + ((uint8_t)'a' - (uint8_t)'A'));
  }
  return c;
}

static int c0_ascii_is_space(uint8_t c)
{
  return c == (uint8_t)' ' || c == (uint8_t)'\t' ||
         c == (uint8_t)'\r' || c == (uint8_t)'\n';
}

static int c0_sv_equals_ci_literal(const uint8_t *data,
                                   uint64_t len,
                                   const char *literal)
{
  if (!data || !literal)
  {
    return 0;
  }
  const uint64_t literal_len = c0_cstr_len(literal);
  if (len != literal_len)
  {
    return 0;
  }
  for (uint64_t i = 0u; i < len; ++i)
  {
    if (c0_ascii_lower(data[i]) != c0_ascii_lower((uint8_t)literal[i]))
    {
      return 0;
    }
  }
  return 1;
}

static int c0_sv_contains_ci(const uint8_t *haystack,
                             uint64_t haystack_len,
                             const uint8_t *needle,
                             uint64_t needle_len)
{
  if (!needle || needle_len == 0u)
  {
    return 1;
  }
  if (!haystack || haystack_len < needle_len)
  {
    return 0;
  }
  const uint8_t first = c0_ascii_lower(needle[0]);
  const uint64_t last_start = haystack_len - needle_len;
  for (uint64_t i = 0u; i <= last_start; ++i)
  {
    if (c0_ascii_lower(haystack[i]) != first)
    {
      continue;
    }
    int match = 1;
    for (uint64_t j = 1u; j < needle_len; ++j)
    {
      if (c0_ascii_lower(haystack[i + j]) != c0_ascii_lower(needle[j]))
      {
        match = 0;
        break;
      }
    }
    if (match != 0)
    {
      return 1;
    }
  }
  return 0;
}

static int c0_trace_read_env_utf8(const char *name,
                                  uint8_t **out_data,
                                  uint64_t *out_len)
{
  if (out_data)
  {
    *out_data = NULL;
  }
  if (out_len)
  {
    *out_len = 0u;
  }
  if (!name || !out_data || !out_len)
  {
    return 0;
  }
  const cursive_platform_u32_t needed = cursive_platform_env_get_utf8(name, NULL, 0u);
  if (needed == 0u)
  {
    return 0;
  }
  char *raw = (char *)c0_heap_alloc_raw((size_t)needed);
  if (!raw)
  {
    return 0;
  }
  const cursive_platform_u32_t copied = cursive_platform_env_get_utf8(name, raw, needed);
  if (copied == 0u || copied >= needed)
  {
    c0_heap_free_raw(raw);
    return 0;
  }

  uint64_t start = 0u;
  uint64_t end = (uint64_t)copied;
  while (start < end && c0_ascii_is_space((uint8_t)raw[start]))
  {
    ++start;
  }
  while (end > start && c0_ascii_is_space((uint8_t)raw[end - 1u]))
  {
    --end;
  }
  if (end <= start)
  {
    c0_heap_free_raw(raw);
    return 0;
  }
  const uint64_t trimmed_len = end - start;
  uint8_t *trimmed = (uint8_t *)c0_heap_alloc_raw((size_t)trimmed_len);
  if (!trimmed)
  {
    c0_heap_free_raw(raw);
    return 0;
  }
  c0_memcpy(trimmed, raw + start, (size_t)trimmed_len);
  c0_heap_free_raw(raw);
  *out_data = trimmed;
  *out_len = trimmed_len;
  return 1;
}

static void c0_trace_replace_filter_locked(uint8_t **slot_data,
                                           uint64_t *slot_len,
                                           const uint8_t *value,
                                           uint64_t value_len)
{
  if (!slot_data || !slot_len)
  {
    return;
  }
  if (*slot_data)
  {
    c0_heap_free_raw(*slot_data);
    *slot_data = NULL;
  }
  *slot_len = 0u;
  if (!value || value_len == 0u || value_len > (uint64_t)SIZE_MAX)
  {
    return;
  }
  uint8_t *copy = (uint8_t *)c0_heap_alloc_raw((size_t)value_len);
  if (!copy)
  {
    return;
  }
  c0_memcpy(copy, value, (size_t)value_len);
  *slot_data = copy;
  *slot_len = value_len;
}

static int c0_trace_parse_bool(const uint8_t *data,
                               uint64_t len,
                               uint8_t *out_value)
{
  if (!data || len == 0u || !out_value)
  {
    return 0;
  }
  if (c0_sv_equals_ci_literal(data, len, "1") ||
      c0_sv_equals_ci_literal(data, len, "true") ||
      c0_sv_equals_ci_literal(data, len, "yes") ||
      c0_sv_equals_ci_literal(data, len, "on"))
  {
    *out_value = 1u;
    return 1;
  }
  if (c0_sv_equals_ci_literal(data, len, "0") ||
      c0_sv_equals_ci_literal(data, len, "false") ||
      c0_sv_equals_ci_literal(data, len, "no") ||
      c0_sv_equals_ci_literal(data, len, "off"))
  {
    *out_value = 0u;
    return 1;
  }
  return 0;
}

static int c0_trace_parse_level(const uint8_t *data,
                                uint64_t len,
                                uint8_t *out_level)
{
  if (!data || len == 0u || !out_level)
  {
    return 0;
  }
  if (c0_sv_equals_ci_literal(data, len, "trace"))
  {
    *out_level = C0_TRACE_LEVEL_TRACE;
    return 1;
  }
  if (c0_sv_equals_ci_literal(data, len, "info"))
  {
    *out_level = C0_TRACE_LEVEL_INFO;
    return 1;
  }
  if (c0_sv_equals_ci_literal(data, len, "warning") ||
      c0_sv_equals_ci_literal(data, len, "warn"))
  {
    *out_level = C0_TRACE_LEVEL_WARNING;
    return 1;
  }
  if (c0_sv_equals_ci_literal(data, len, "error"))
  {
    *out_level = C0_TRACE_LEVEL_ERROR;
    return 1;
  }
  return 0;
}

static int c0_trace_parse_sink_kind(const uint8_t *data,
                                    uint64_t len,
                                    uint8_t *out_sink_kind)
{
  if (!data || len == 0u || !out_sink_kind)
  {
    return 0;
  }
  if (c0_sv_equals_ci_literal(data, len, "console"))
  {
    *out_sink_kind = C0_TRACE_SINK_CONSOLE;
    return 1;
  }
  if (c0_sv_equals_ci_literal(data, len, "file"))
  {
    *out_sink_kind = C0_TRACE_SINK_FILE;
    return 1;
  }
  if (c0_sv_equals_ci_literal(data, len, "both"))
  {
    *out_sink_kind = C0_TRACE_SINK_BOTH;
    return 1;
  }
  return 0;
}

static int c0_trace_parse_filter_mask_csv(const uint8_t *data,
                                          uint64_t len,
                                          uint32_t *out_mask)
{
  if (!data || len == 0u || !out_mask)
  {
    return 0;
  }
  uint64_t start = 0u;
  uint32_t mask = 0u;
  int has_token = 0;
  while (start <= len)
  {
    uint64_t end = start;
    while (end < len && data[end] != (uint8_t)',' && data[end] != (uint8_t)';')
    {
      ++end;
    }
    uint64_t token_start = start;
    uint64_t token_end = end;
    while (token_start < token_end && c0_ascii_is_space(data[token_start]))
    {
      ++token_start;
    }
    while (token_end > token_start && c0_ascii_is_space(data[token_end - 1u]))
    {
      --token_end;
    }
    const uint64_t token_len = token_end - token_start;
    if (token_len != 0u)
    {
      has_token = 1;
      if (c0_sv_equals_ci_literal(data + token_start, token_len, "all"))
      {
        mask |= C0_TRACE_FILTER_ALL;
      }
      else if (c0_sv_equals_ci_literal(data + token_start, token_len, "log"))
      {
        mask |= C0_TRACE_FILTER_LOG;
      }
      else if (c0_sv_equals_ci_literal(data + token_start, token_len, "diagnostic") ||
               c0_sv_equals_ci_literal(data + token_start, token_len, "diag"))
      {
        mask |= C0_TRACE_FILTER_DIAGNOSTIC;
      }
      else if (c0_sv_equals_ci_literal(data + token_start, token_len, "runtime"))
      {
        mask |= C0_TRACE_FILTER_RUNTIME;
      }
      else
      {
        return 0;
      }
    }
    if (end == len)
    {
      break;
    }
    start = end + 1u;
  }
  if (!has_token)
  {
    return 0;
  }
  *out_mask = (mask == 0u) ? C0_TRACE_FILTER_ALL : mask;
  return 1;
}

static void c0_trace_apply_env_overrides_locked(void)
{
  if (cursive_platform_atomic_compare_exchange(&c0_trace_env_loaded, 1, 0) != 0)
  {
    return;
  }

  uint8_t *env_data = NULL;
  uint64_t env_len = 0u;
  uint8_t parsed_u8 = 0u;
  uint32_t parsed_u32 = 0u;
  uint8_t sink_kind = C0_TRACE_SINK_CONSOLE;
  int has_sink_override = 0;
  int has_any_override = 0;

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_SINK", &env_data, &env_len))
  {
    if (c0_trace_parse_sink_kind(env_data, env_len, &sink_kind))
    {
      has_sink_override = 1;
      has_any_override = 1;
    }
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }
  if (has_sink_override)
  {
    c0_trace_policy_sink_kind = sink_kind;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_PATH", &env_data, &env_len))
  {
    c0_trace_replace_filter_locked(
        &c0_trace_policy_path_utf8, &c0_trace_policy_path_len, env_data, env_len);
    has_any_override = 1;
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_ROOT", &env_data, &env_len))
  {
    c0_trace_replace_filter_locked(
        &c0_trace_root_utf8, &c0_trace_root_len, env_data, env_len);
    has_any_override = 1;
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_FILTER", &env_data, &env_len))
  {
    if (c0_trace_parse_filter_mask_csv(env_data, env_len, &parsed_u32))
    {
      cursive_platform_atomic_exchange(&c0_trace_filter_mask,
                                 (cursive_platform_i32_t)parsed_u32);
      has_any_override = 1;
    }
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_LEVEL", &env_data, &env_len))
  {
    if (c0_trace_parse_level(env_data, env_len, &parsed_u8))
    {
      cursive_platform_atomic_exchange(&c0_trace_min_level,
                                 (cursive_platform_i32_t)parsed_u8);
      has_any_override = 1;
    }
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_RULE", &env_data, &env_len))
  {
    c0_trace_replace_filter_locked(
        &c0_trace_rule_filter_utf8, &c0_trace_rule_filter_len, env_data, env_len);
    has_any_override = 1;
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_FILE", &env_data, &env_len))
  {
    c0_trace_replace_filter_locked(
        &c0_trace_file_filter_utf8, &c0_trace_file_filter_len, env_data, env_len);
    has_any_override = 1;
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_LABEL", &env_data, &env_len))
  {
    c0_trace_replace_filter_locked(
        &c0_trace_label_filter_utf8, &c0_trace_label_filter_len, env_data, env_len);
    has_any_override = 1;
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_FAIL_ONLY", &env_data, &env_len))
  {
    if (c0_trace_parse_bool(env_data, env_len, &parsed_u8))
    {
      cursive_platform_atomic_exchange(&c0_trace_fail_only, parsed_u8 ? 1 : 0);
      has_any_override = 1;
    }
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (c0_trace_read_env_utf8("CURSIVE_RUNTIME_BREAK_ON_FAIL", &env_data, &env_len))
  {
    if (c0_trace_parse_bool(env_data, env_len, &parsed_u8))
    {
      cursive_platform_atomic_exchange(&c0_trace_break_on_fail, parsed_u8 ? 1 : 0);
      has_any_override = 1;
    }
    c0_heap_free_raw(env_data);
    env_data = NULL;
    env_len = 0u;
  }

  if (has_any_override != 0)
  {
    c0_trace_policy_configured = 1;
  }
}

static void c0_trace_write_to_handle(cursive_platform_handle_t handle,
                                     int allow_console_fallback,
                                     const uint8_t *data,
                                     uint64_t len)
{
  if (!handle || !data || len == 0)
  {
    return;
  }
  uint64_t offset = 0;
  while (offset < len)
  {
    uint64_t remaining = len - offset;
    cursive_platform_u32_t chunk =
        remaining > 0xFFFFFFFFu ? 0xFFFFFFFFu : (cursive_platform_u32_t)remaining;
    cursive_platform_u32_t written = 0;
    if (!cursive_platform_handle_write(handle, data + offset, chunk, &written))
    {
      if (allow_console_fallback != 0)
      {
        cursive_platform_u32_t mode = 0;
        if (cursive_platform_console_mode_get(handle, &mode) &&
            cursive_platform_console_write_utf8(handle, data + offset, chunk, &written))
        {
          if (written != 0)
          {
            offset += written;
            continue;
          }
        }
      }
      if (written == 0)
      {
        break;
      }
    }
    if (written == 0)
    {
      break;
    }
    offset += written;
  }
}

static void c0_trace_write_file_bytes(const uint8_t *data, uint64_t len)
{
  if (!data || len == 0)
  {
    return;
  }
  if (c0_trace_file_sink == 0)
  {
    return;
  }
  c0_trace_write_to_handle(c0_trace_file_handle, 0, data, len);
}

static void c0_trace_write_console_bytes(const uint8_t *data, uint64_t len)
{
  if (!data || len == 0)
  {
    return;
  }
  if (c0_trace_console_sink == 0)
  {
    return;
  }
  c0_trace_write_to_handle(c0_trace_console_handle, 1, data, len);
}

static void c0_trace_write_cstr(const char *text)
{
  if (!text)
  {
    return;
  }
  c0_trace_write_file_bytes((const uint8_t *)text, c0_cstr_len(text));
}

static void c0_trace_write_console_cstr(const char *text)
{
  if (!text)
  {
    return;
  }
  c0_trace_write_console_bytes((const uint8_t *)text, c0_cstr_len(text));
}

static void c0_trace_write_encoded(const uint8_t *data, uint64_t len)
{
  static const char hex[] = "0123456789ABCDEF";
  uint8_t chunk[512];
  uint32_t chunk_len = 0u;
  if (!data || len == 0u)
  {
    return;
  }

  for (uint64_t i = 0; i < len; ++i)
  {
    const uint8_t c = data[i];
    const uint32_t needed =
        (c == '\t' || c == '\n' || c == '%' || c == ';' || c == '=') ? 3u : 1u;
    if (chunk_len + needed > (uint32_t)sizeof(chunk))
    {
      c0_trace_write_file_bytes(chunk, chunk_len);
      chunk_len = 0u;
    }
    if (c == '\t' || c == '\n' || c == '%' || c == ';' || c == '=')
    {
      chunk[chunk_len++] = '%';
      chunk[chunk_len++] = (uint8_t)hex[(c >> 4) & 0xF];
      chunk[chunk_len++] = (uint8_t)hex[c & 0xF];
    }
    else
    {
      chunk[chunk_len++] = c;
    }
  }

  if (chunk_len != 0u)
  {
    c0_trace_write_file_bytes(chunk, chunk_len);
  }
}

/* Percent-encode payload bytes that can break row or key/value parsing. */
static void c0_trace_write_payload_readable(const uint8_t *data, uint64_t len)
{
  static const char hex[] = "0123456789ABCDEF";
  uint8_t chunk[512];
  uint32_t chunk_len = 0u;
  if (!data || len == 0u)
  {
    return;
  }

  for (uint64_t i = 0; i < len; ++i)
  {
    const uint8_t c = data[i];
    const uint32_t needed =
        (c == '\t' || c == '\n' || c == '\r' || c == '%' || c == ';' || c == '=')
            ? 3u
            : 1u;
    if (chunk_len + needed > (uint32_t)sizeof(chunk))
    {
      c0_trace_write_file_bytes(chunk, chunk_len);
      chunk_len = 0u;
    }
    if (c == '\t' || c == '\n' || c == '\r' || c == '%' || c == ';' || c == '=')
    {
      chunk[chunk_len++] = '%';
      chunk[chunk_len++] = (uint8_t)hex[(c >> 4) & 0xF];
      chunk[chunk_len++] = (uint8_t)hex[c & 0xF];
    }
    else
    {
      chunk[chunk_len++] = c;
    }
  }

  if (chunk_len != 0u)
  {
    c0_trace_write_file_bytes(chunk, chunk_len);
  }
}

static int c0_hex_nibble(uint8_t c, uint8_t *out)
{
  if (!out)
  {
    return 0;
  }
  if (c >= (uint8_t)'0' && c <= (uint8_t)'9')
  {
    *out = (uint8_t)(c - (uint8_t)'0');
    return 1;
  }
  if (c >= (uint8_t)'a' && c <= (uint8_t)'f')
  {
    *out = (uint8_t)(10u + (c - (uint8_t)'a'));
    return 1;
  }
  if (c >= (uint8_t)'A' && c <= (uint8_t)'F')
  {
    *out = (uint8_t)(10u + (c - (uint8_t)'A'));
    return 1;
  }
  return 0;
}

static void c0_trace_write_console_decoded(const uint8_t *data, uint64_t len)
{
  uint8_t chunk[512];
  uint32_t chunk_len = 0u;
  if (!data || len == 0u || c0_trace_console_sink == 0)
  {
    return;
  }

  for (uint64_t i = 0u; i < len; ++i)
  {
    uint8_t out = data[i];
    if (out == (uint8_t)'%' && i + 2u < len)
    {
      uint8_t hi = 0u;
      uint8_t lo = 0u;
      if (c0_hex_nibble(data[i + 1u], &hi) != 0 &&
          c0_hex_nibble(data[i + 2u], &lo) != 0)
      {
        out = (uint8_t)((hi << 4u) | lo);
        i += 2u;
      }
    }
    if (out == (uint8_t)'\r' || out == (uint8_t)'\n' ||
        out == (uint8_t)'\t')
    {
      out = (uint8_t)' ';
    }
    if (chunk_len + 1u > (uint32_t)sizeof(chunk))
    {
      c0_trace_write_console_bytes(chunk, chunk_len);
      chunk_len = 0u;
    }
    chunk[chunk_len++] = out;
  }

  if (chunk_len != 0u)
  {
    c0_trace_write_console_bytes(chunk, chunk_len);
  }
}

static int c0_payload_lookup_field(const uint8_t *payload,
                                   uint64_t payload_len,
                                   const char *key,
                                   const uint8_t **out_value,
                                   uint64_t *out_value_len)
{
  if (!payload || payload_len == 0u || !key)
  {
    return 0;
  }

  const uint64_t key_len = c0_cstr_len(key);
  uint64_t pos = 0u;
  while (pos <= payload_len)
  {
    uint64_t seg_end = pos;
    while (seg_end < payload_len && payload[seg_end] != (uint8_t)';')
    {
      ++seg_end;
    }

    uint64_t eq = pos;
    while (eq < seg_end && payload[eq] != (uint8_t)'=')
    {
      ++eq;
    }

    if (eq > pos && (eq - pos) == key_len)
    {
      int match = 1;
      for (uint64_t i = 0u; i < key_len; ++i)
      {
        if (payload[pos + i] != (uint8_t)key[i])
        {
          match = 0;
          break;
        }
      }
      if (match != 0)
      {
        const uint64_t value_start = (eq < seg_end) ? (eq + 1u) : seg_end;
        if (out_value)
        {
          *out_value = payload + value_start;
        }
        if (out_value_len)
        {
          *out_value_len = seg_end - value_start;
        }
        return 1;
      }
    }

    if (seg_end == payload_len)
    {
      break;
    }
    pos = seg_end + 1u;
  }

  return 0;
}

static uint32_t c0_u64_to_dec(uint64_t value, char *out)
{
  char rev[32];
  uint32_t count = 0;
  do
  {
    rev[count++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value != 0u && count < (uint32_t)sizeof(rev));
  for (uint32_t i = 0; i < count; ++i)
  {
    out[i] = rev[count - 1u - i];
  }
  return count;
}

static uint32_t c0_i64_to_dec(int64_t value, char *out)
{
  uint32_t pos = 0;
  uint64_t magnitude = 0;
  if (value < 0)
  {
    out[pos++] = '-';
    magnitude = (uint64_t)(-(value + 1)) + 1u;
  }
  else
  {
    magnitude = (uint64_t)value;
  }
  return pos + c0_u64_to_dec(magnitude, out + pos);
}

static uint32_t c0_u64_to_hex(uint64_t value, char *out, uint8_t min_digits)
{
  static const char hex[] = "0123456789ABCDEF";
  char rev[16];
  uint32_t count = 0;
  do
  {
    rev[count++] = hex[value & 0xFu];
    value >>= 4u;
  } while (value != 0u && count < (uint32_t)sizeof(rev));
  while (count < min_digits && count < (uint32_t)sizeof(rev))
  {
    rev[count++] = '0';
  }
  for (uint32_t i = 0; i < count; ++i)
  {
    out[i] = rev[count - 1u - i];
  }
  return count;
}

static uint32_t c0_trim_fraction(char *text, uint32_t len)
{
  if (!text || len == 0u)
  {
    return 0u;
  }

  uint32_t dot = len;
  uint32_t exp = len;
  for (uint32_t i = 0; i < len; ++i)
  {
    if (text[i] == '.')
    {
      dot = i;
    }
    else if (text[i] == 'e' || text[i] == 'E')
    {
      exp = i;
      break;
    }
  }

  if (dot == len)
  {
    return len;
  }

  uint32_t frac_end = (exp == len) ? (len - 1u) : (exp - 1u);
  while (frac_end > dot + 1u && text[frac_end] == '0')
  {
    --frac_end;
  }
  if (frac_end == dot)
  {
    text[++frac_end] = '0';
  }

  const uint32_t head_len = frac_end + 1u;
  if (exp != len)
  {
    const uint32_t tail_len = len - exp;
    c0_memmove(text + head_len, text + exp, tail_len);
    return head_len + tail_len;
  }
  return head_len;
}

static uint32_t c0_format_float_text(double input,
                                     uint8_t bits,
                                     char *out,
                                     uint32_t cap)
{
  if (!out || cap == 0u)
  {
    return 0u;
  }

  double value = input;
  if (bits == 16u || bits == 32u)
  {
    float narrowed = (float)value;
    value = (double)narrowed;
  }

  uint64_t raw = 0u;
  c0_memcpy(&raw, &value, sizeof(raw));
  const uint64_t abs_raw = raw & 0x7FFFFFFFFFFFFFFFull;
  const int negative = (raw >> 63u) != 0u;
  const uint16_t exp_bits = (uint16_t)((raw >> 52u) & 0x7FFu);
  const uint64_t frac_bits = raw & 0x000FFFFFFFFFFFFFull;

  if (exp_bits == 0x7FFu)
  {
    if (frac_bits != 0u)
    {
      if (cap < 4u)
      {
        return 0u;
      }
      out[0] = 'n';
      out[1] = 'a';
      out[2] = 'n';
      return 3u;
    }
    if (negative)
    {
      if (cap < 5u)
      {
        return 0u;
      }
      out[0] = '-';
      out[1] = 'i';
      out[2] = 'n';
      out[3] = 'f';
      return 4u;
    }
    if (cap < 4u)
    {
      return 0u;
    }
    out[0] = 'i';
    out[1] = 'n';
    out[2] = 'f';
    return 3u;
  }

  if (abs_raw == 0u)
  {
    if (negative)
    {
      if (cap < 5u)
      {
        return 0u;
      }
      out[0] = '-';
      out[1] = '0';
      out[2] = '.';
      out[3] = '0';
      return 4u;
    }
    if (cap < 4u)
    {
      return 0u;
    }
    out[0] = '0';
    out[1] = '.';
    out[2] = '0';
    return 3u;
  }

  uint32_t pos = 0u;
  if (negative)
  {
    out[pos++] = '-';
  }

  double norm = negative ? -value : value;
  int exp10 = 0;
  while (norm >= 10.0 && exp10 < 400)
  {
    norm /= 10.0;
    ++exp10;
  }
  while (norm < 1.0 && exp10 > -400)
  {
    norm *= 10.0;
    --exp10;
  }

  const int sig_digits = (bits == 64u) ? 17 : 9;
  int digits[18];
  c0_memset(digits, 0, sizeof(digits));
  for (int i = 0; i < sig_digits; ++i)
  {
    int digit = (int)norm;
    if (digit < 0)
    {
      digit = 0;
    }
    else if (digit > 9)
    {
      digit = 9;
    }
    digits[i] = digit;
    norm = (norm - (double)digit) * 10.0;
    if (norm < 0.0)
    {
      norm = 0.0;
    }
  }

  if (norm >= 5.0)
  {
    int idx = sig_digits - 1;
    while (idx >= 0)
    {
      if (digits[idx] < 9)
      {
        ++digits[idx];
        break;
      }
      digits[idx] = 0;
      --idx;
    }
    if (idx < 0)
    {
      digits[0] = 1;
      for (int i = 1; i < sig_digits; ++i)
      {
        digits[i] = 0;
      }
      ++exp10;
    }
  }

  const int use_scientific = (exp10 < -4 || exp10 >= sig_digits);
  if (!use_scientific)
  {
    const int int_digits = exp10 + 1;
    if (int_digits <= 0)
    {
      if (pos + 2u >= cap)
      {
        return 0u;
      }
      out[pos++] = '0';
      out[pos++] = '.';
      for (int i = 0; i < -int_digits; ++i)
      {
        if (pos + 1u >= cap)
        {
          return 0u;
        }
        out[pos++] = '0';
      }
      for (int i = 0; i < sig_digits; ++i)
      {
        if (pos + 1u >= cap)
        {
          return 0u;
        }
        out[pos++] = (char)('0' + digits[i]);
      }
    }
    else
    {
      for (int i = 0; i < int_digits; ++i)
      {
        if (pos + 1u >= cap)
        {
          return 0u;
        }
        const int digit = (i < sig_digits) ? digits[i] : 0;
        out[pos++] = (char)('0' + digit);
      }
      if (int_digits < sig_digits)
      {
        if (pos + 1u >= cap)
        {
          return 0u;
        }
        out[pos++] = '.';
        for (int i = int_digits; i < sig_digits; ++i)
        {
          if (pos + 1u >= cap)
          {
            return 0u;
          }
          out[pos++] = (char)('0' + digits[i]);
        }
      }
      else
      {
        if (pos + 2u >= cap)
        {
          return 0u;
        }
        out[pos++] = '.';
        out[pos++] = '0';
      }
    }
    return c0_trim_fraction(out, pos);
  }

  if (pos + 2u >= cap)
  {
    return 0u;
  }
  out[pos++] = (char)('0' + digits[0]);
  out[pos++] = '.';
  for (int i = 1; i < sig_digits; ++i)
  {
    if (pos + 1u >= cap)
    {
      return 0u;
    }
    out[pos++] = (char)('0' + digits[i]);
  }
  out[pos++] = 'e';
  if (exp10 < 0)
  {
    if (pos + 1u >= cap)
    {
      return 0u;
    }
    out[pos++] = '-';
  }
  else
  {
    if (pos + 1u >= cap)
    {
      return 0u;
    }
    out[pos++] = '+';
  }

  uint32_t exp_abs = (exp10 < 0) ? (uint32_t)(-exp10) : (uint32_t)exp10;
  char exp_text[16];
  uint32_t exp_len = c0_u64_to_dec((uint64_t)exp_abs, exp_text);
  if (pos + exp_len >= cap)
  {
    return 0u;
  }
  c0_memcpy(out + pos, exp_text, exp_len);
  pos += exp_len;
  return c0_trim_fraction(out, pos);
}

static int c0_rule_id_has_prefix(const C0StringView *rule_id,
                                 const char *prefix)
{
  if (!rule_id || !rule_id->data || !prefix)
  {
    return 0;
  }
  const uint64_t prefix_len = c0_cstr_len(prefix);
  if (rule_id->len < prefix_len)
  {
    return 0;
  }
  for (uint64_t i = 0; i < prefix_len; ++i)
  {
    if (rule_id->data[i] != (uint8_t)prefix[i])
    {
      return 0;
    }
  }
  return 1;
}

static int c0_rule_id_has_log_prefix(const C0StringView *rule_id)
{
  return c0_rule_id_has_prefix(rule_id, "Log-");
}

static const char *c0_trace_level_name(uint8_t level)
{
  switch (level)
  {
  case C0_TRACE_LEVEL_INFO:
    return "info";
  case C0_TRACE_LEVEL_WARNING:
    return "warning";
  case C0_TRACE_LEVEL_ERROR:
    return "error";
  case C0_TRACE_LEVEL_TRACE:
  default:
    return "trace";
  }
}

typedef struct C0TraceRecordInfo
{
  uint32_t required_filter;
  uint8_t level;
  const char *category_name;
  uint8_t cmp_fail;
} C0TraceRecordInfo;

typedef struct C0TraceRecordMeta
{
  uint64_t thread_id;
  uint64_t sequence;
} C0TraceRecordMeta;

static uint32_t c0_trace_filter_from_category(const uint8_t *value,
                                              uint64_t value_len)
{
  if (c0_sv_equals_ci_literal(value, value_len, "log"))
  {
    return C0_TRACE_FILTER_LOG;
  }
  if (c0_sv_equals_ci_literal(value, value_len, "diagnostic") ||
      c0_sv_equals_ci_literal(value, value_len, "diag"))
  {
    return C0_TRACE_FILTER_DIAGNOSTIC;
  }
  return C0_TRACE_FILTER_RUNTIME;
}

static const char *c0_trace_category_name_from_filter(uint32_t filter)
{
  if (filter == C0_TRACE_FILTER_LOG)
  {
    return "log";
  }
  if (filter == C0_TRACE_FILTER_DIAGNOSTIC)
  {
    return "diagnostic";
  }
  return "runtime";
}

static uint8_t c0_trace_level_from_payload(const uint8_t *value,
                                           uint64_t value_len)
{
  uint8_t parsed = C0_TRACE_LEVEL_TRACE;
  if (!c0_trace_parse_level(value, value_len, &parsed))
  {
    return C0_TRACE_LEVEL_TRACE;
  }
  return parsed;
}

static uint8_t c0_trace_payload_cmp_is_fail(const C0StringView *payload)
{
  if (!payload || !payload->data || payload->len == 0u)
  {
    return 0u;
  }
  const uint8_t *cmp_value = NULL;
  uint64_t cmp_len = 0u;
  if (!c0_payload_lookup_field(
          payload->data, payload->len, "cmp", &cmp_value, &cmp_len))
  {
    return 0u;
  }
  return c0_sv_equals_ci_literal(cmp_value, cmp_len, "fail") ? 1u : 0u;
}

static void c0_trace_compute_record_info(const C0StringView *payload,
                                         C0TraceRecordInfo *out_info)
{
  if (!out_info)
  {
    return;
  }
  out_info->required_filter = C0_TRACE_FILTER_RUNTIME;
  out_info->level = C0_TRACE_LEVEL_TRACE;
  out_info->category_name = "runtime";
  out_info->cmp_fail = 0u;

  if (!payload || !payload->data || payload->len == 0u)
  {
    return;
  }
  const uint8_t *category_value = NULL;
  uint64_t category_len = 0u;
  if (c0_payload_lookup_field(payload->data, payload->len, "category",
                              &category_value, &category_len))
  {
    out_info->required_filter =
        c0_trace_filter_from_category(category_value, category_len);
    out_info->category_name =
        c0_trace_category_name_from_filter(out_info->required_filter);
  }

  const uint8_t *level_value = NULL;
  uint64_t level_len = 0u;
  if (c0_payload_lookup_field(payload->data, payload->len, "level",
                              &level_value, &level_len))
  {
    out_info->level = c0_trace_level_from_payload(level_value, level_len);
  }
  out_info->cmp_fail = c0_trace_payload_cmp_is_fail(payload);
}

static uint8_t c0_norm_path_char(uint8_t c)
{
  if (c == (uint8_t)'\\')
  {
    return (uint8_t)'/';
  }
  if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
  {
    return (uint8_t)(c + ((uint8_t)'a' - (uint8_t)'A'));
  }
  return c;
}

static int c0_trace_path_starts_with_root(const C0StringView *file,
                                          uint64_t *out_rel_start)
{
  if (!file || !file->data || file->len == 0u ||
      !c0_trace_root_utf8 || c0_trace_root_len == 0u)
  {
    return 0;
  }
  uint64_t root_len = c0_trace_root_len;
  while (root_len > 0u &&
         (c0_trace_root_utf8[root_len - 1u] == (uint8_t)'/' ||
          c0_trace_root_utf8[root_len - 1u] == (uint8_t)'\\'))
  {
    --root_len;
  }
  if (root_len == 0u || file->len < root_len)
  {
    return 0;
  }
  for (uint64_t i = 0u; i < root_len; ++i)
  {
    if (c0_norm_path_char(file->data[i]) != c0_norm_path_char(c0_trace_root_utf8[i]))
    {
      return 0;
    }
  }
  if (file->len == root_len)
  {
    if (out_rel_start)
    {
      *out_rel_start = root_len;
    }
    return 1;
  }
  if (file->data[root_len] != (uint8_t)'/' &&
      file->data[root_len] != (uint8_t)'\\')
  {
    return 0;
  }
  if (out_rel_start)
  {
    *out_rel_start = root_len + 1u;
  }
  return 1;
}

static uint64_t c0_trace_timestamp_ms(void)
{
  cursive_platform_filetime_t ft;
  cursive_platform_ularge_integer_t value;
  const uint64_t epoch_delta_100ns = 116444736000000000ULL;
  cursive_platform_system_time_filetime(&ft);
  value.low_part = ft.low_date_time;
  value.high_part = ft.high_date_time;
  if (value.quad_part <= epoch_delta_100ns)
  {
    return 0u;
  }
  return (value.quad_part - epoch_delta_100ns) / 10000ULL;
}

static void c0_trace_write_u64_field(uint64_t value)
{
  char text[32];
  uint32_t len = c0_u64_to_dec(value, text);
  c0_trace_write_file_bytes((const uint8_t *)text, len);
}

static void c0_trace_write_source_file_field(const C0StringView *file)
{
  if (file && file->data && file->len != 0u)
  {
    uint64_t rel_start = 0u;
    if (c0_trace_path_starts_with_root(file, &rel_start) != 0u)
    {
      const uint8_t *rel_data = file->data + rel_start;
      uint64_t rel_len = file->len - rel_start;
      if (rel_len != 0u)
      {
        c0_trace_write_encoded(rel_data, rel_len);
        return;
      }
    }
    c0_trace_write_encoded(file->data, file->len);
    return;
  }
  c0_trace_write_cstr("-");
}

static void c0_trace_write_console_u64(uint64_t value)
{
  char text[32];
  uint32_t len = c0_u64_to_dec(value, text);
  c0_trace_write_console_bytes((const uint8_t *)text, len);
}

static void c0_trace_write_console_source_file(const C0StringView *file)
{
  if (file && file->data && file->len != 0u)
  {
    uint64_t rel_start = 0u;
    if (c0_trace_path_starts_with_root(file, &rel_start) != 0u)
    {
      const uint8_t *rel_data = file->data + rel_start;
      const uint64_t rel_len = file->len - rel_start;
      if (rel_len != 0u)
      {
        c0_trace_write_console_decoded(rel_data, rel_len);
        return;
      }
    }
    c0_trace_write_console_decoded(file->data, file->len);
    return;
  }
  c0_trace_write_console_cstr("-");
}

static void c0_trace_write_console_rule_label(const C0StringView *rule_id)
{
  if (!rule_id || !rule_id->data || rule_id->len == 0u)
  {
    c0_trace_write_console_cstr("-");
    return;
  }
  if (c0_rule_id_has_log_prefix(rule_id) && rule_id->len > 4u)
  {
    c0_trace_write_console_decoded(rule_id->data + 4u, rule_id->len - 4u);
    return;
  }
  c0_trace_write_console_decoded(rule_id->data, rule_id->len);
}

static void c0_trace_write_console_pretty_locked(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    const C0StringView *payload,
    const C0TraceRecordMeta *meta,
    const C0TraceRecordInfo *info,
    const uint8_t *actual_override,
    uint64_t actual_override_len)
{
  const uint8_t *label_value = NULL;
  uint64_t label_len = 0u;
  const uint8_t *expected_value = NULL;
  uint64_t expected_len = 0u;
  const uint8_t *actual_value = NULL;
  uint64_t actual_len = 0u;
  const uint8_t *cmp_value = NULL;
  uint64_t cmp_len = 0u;

  if (c0_trace_console_sink == 0 ||
      !rule_id || !rule_id->data || rule_id->len == 0u)
  {
    return;
  }

  c0_trace_write_console_cstr("[");
  c0_trace_write_console_cstr(
      c0_trace_level_name(info ? info->level : C0_TRACE_LEVEL_TRACE));
  c0_trace_write_console_cstr("][");
  c0_trace_write_console_cstr(info ? info->category_name : "runtime");
  c0_trace_write_console_cstr("]");
  if (meta)
  {
    c0_trace_write_console_cstr(" seq=");
    c0_trace_write_console_u64(meta->sequence);
    c0_trace_write_console_cstr(" tid=");
    c0_trace_write_console_u64(meta->thread_id);
  }
  c0_trace_write_console_cstr(" ");
  c0_trace_write_console_source_file(file);
  if (start_line != 0u)
  {
    c0_trace_write_console_cstr(":");
    c0_trace_write_console_u64(start_line);
    if (start_col != 0u)
    {
      c0_trace_write_console_cstr(":");
      c0_trace_write_console_u64(start_col);
    }
  }
  c0_trace_write_console_cstr(" ");

  if (payload && payload->data && payload->len != 0u)
  {
    c0_payload_lookup_field(
        payload->data, payload->len, "label", &label_value, &label_len);
    c0_payload_lookup_field(
        payload->data, payload->len, "expected", &expected_value, &expected_len);
    c0_payload_lookup_field(
        payload->data, payload->len, "actual", &actual_value, &actual_len);
    c0_payload_lookup_field(
        payload->data, payload->len, "cmp", &cmp_value, &cmp_len);
  }

  if (label_value && label_len != 0u)
  {
    c0_trace_write_console_decoded(label_value, label_len);
  }
  else
  {
    c0_trace_write_console_rule_label(rule_id);
  }

  if (expected_value && expected_len != 0u)
  {
    c0_trace_write_console_cstr(" expected=");
    c0_trace_write_console_decoded(expected_value, expected_len);
  }

  if (actual_override && actual_override_len != 0u)
  {
    c0_trace_write_console_cstr(" actual=");
    c0_trace_write_console_decoded(actual_override, actual_override_len);
  }
  else if (actual_value && actual_len != 0u)
  {
    c0_trace_write_console_cstr(" actual=");
    c0_trace_write_console_decoded(actual_value, actual_len);
  }

  if (cmp_value && cmp_len != 0u)
  {
    c0_trace_write_console_cstr(" cmp=");
    c0_trace_write_console_decoded(cmp_value, cmp_len);
  }

  if ((!label_value || label_len == 0u) &&
      (!expected_value || expected_len == 0u) &&
      (!actual_override || actual_override_len == 0u) &&
      (!actual_value || actual_len == 0u) &&
      (!cmp_value || cmp_len == 0u) &&
      payload && payload->data && payload->len != 0u)
  {
    c0_trace_write_console_cstr(" payload=");
    c0_trace_write_console_decoded(payload->data, payload->len);
  }

  c0_trace_write_console_cstr("\n");
}

static uint8_t c0_trace_rule_matches_filter(const C0StringView *rule_id)
{
  if (!c0_trace_rule_filter_utf8 || c0_trace_rule_filter_len == 0u)
  {
    return 1u;
  }
  if (!rule_id || !rule_id->data || rule_id->len == 0u)
  {
    return 0u;
  }
  return c0_sv_contains_ci(
             rule_id->data, rule_id->len, c0_trace_rule_filter_utf8, c0_trace_rule_filter_len)
             ? 1u
             : 0u;
}

static uint8_t c0_trace_file_matches_filter(const C0StringView *file)
{
  if (!c0_trace_file_filter_utf8 || c0_trace_file_filter_len == 0u)
  {
    return 1u;
  }
  if (!file || !file->data || file->len == 0u)
  {
    return 0u;
  }
  return c0_sv_contains_ci(
             file->data, file->len, c0_trace_file_filter_utf8, c0_trace_file_filter_len)
             ? 1u
             : 0u;
}

static uint8_t c0_trace_label_matches_filter(const C0StringView *payload)
{
  if (!c0_trace_label_filter_utf8 || c0_trace_label_filter_len == 0u)
  {
    return 1u;
  }
  if (!payload || !payload->data || payload->len == 0u)
  {
    return 0u;
  }
  const uint8_t *label_value = NULL;
  uint64_t label_len = 0u;
  if (!c0_payload_lookup_field(
          payload->data, payload->len, "label", &label_value, &label_len) ||
      !label_value || label_len == 0u)
  {
    return 0u;
  }
  return c0_sv_contains_ci(
             label_value, label_len, c0_trace_label_filter_utf8, c0_trace_label_filter_len)
             ? 1u
             : 0u;
}

static uint8_t c0_trace_allow_record(const C0StringView *rule_id,
                                     const C0StringView *file,
                                     const C0StringView *payload,
                                     const C0TraceRecordInfo *info)
{
  if (!rule_id || !rule_id->data || rule_id->len == 0u || !info)
  {
    return 0u;
  }
  const uint32_t active_filter =
      (uint32_t)cursive_platform_atomic_compare_exchange(&c0_trace_filter_mask, 0, 0);
  const uint8_t min_level =
      (uint8_t)cursive_platform_atomic_compare_exchange(&c0_trace_min_level, 0, 0);
  if ((info->required_filter & active_filter) == 0u)
  {
    return 0u;
  }
  if (info->level < min_level)
  {
    return 0u;
  }
  if (c0_trace_rule_matches_filter(rule_id) == 0u)
  {
    return 0u;
  }
  if (c0_trace_file_matches_filter(file) == 0u)
  {
    return 0u;
  }
  if (c0_trace_label_matches_filter(payload) == 0u)
  {
    return 0u;
  }
  if (cursive_platform_atomic_compare_exchange(&c0_trace_fail_only, 0, 0) != 0 &&
      info->cmp_fail == 0u)
  {
    return 0u;
  }
  return 1u;
}

static int c0_trace_begin_locked(const C0StringView *rule_id,
                                 const C0StringView *file,
                                 uint64_t start_line,
                                 uint64_t start_col,
                                 uint64_t end_line,
                                 uint64_t end_col,
                                 const C0StringView *payload,
                                 const C0TraceRecordInfo *info,
                                 C0TraceRecordMeta *out_meta)
{
  c0_conformance_init();
  if (cursive_platform_atomic_compare_exchange(&c0_trace_enabled, 0, 0) == 0 ||
      c0_trace_allow_record(rule_id, file, payload, info) == 0u)
  {
    return 0;
  }
  cursive_platform_rwlock_lock_exclusive(&c0_trace_lock);
  if (cursive_platform_atomic_compare_exchange(&c0_trace_enabled, 0, 0) == 0 ||
      c0_trace_allow_record(rule_id, file, payload, info) == 0u)
  {
    cursive_platform_rwlock_unlock_exclusive(&c0_trace_lock);
    return 0;
  }
  C0TraceRecordMeta meta;
  meta.thread_id = (uint64_t)cursive_platform_current_thread_id();
  meta.sequence = ++c0_trace_next_sequence;
  if (out_meta)
  {
    *out_meta = meta;
  }
  c0_trace_write_cstr("runtime\truntime\t");
  c0_trace_write_file_bytes(rule_id->data, rule_id->len);
  c0_trace_write_cstr("\t");
  c0_trace_write_source_file_field(file);
  if ((file && file->data && file->len != 0u) ||
      start_line != 0u || start_col != 0u || end_line != 0u || end_col != 0u)
  {
    c0_trace_write_cstr("\t");
    c0_trace_write_u64_field(start_line);
    c0_trace_write_cstr("\t");
    c0_trace_write_u64_field(start_col);
    c0_trace_write_cstr("\t");
    c0_trace_write_u64_field(end_line);
    c0_trace_write_cstr("\t");
    c0_trace_write_u64_field(end_col);
  }
  else
  {
    c0_trace_write_cstr("\t-\t-\t-\t-");
  }
  c0_trace_write_cstr("\t");
  c0_trace_write_cstr("ts_ms=");
  c0_trace_write_u64_field(c0_trace_timestamp_ms());
  c0_trace_write_cstr(";pid=");
  c0_trace_write_u64_field((uint64_t)cursive_platform_current_process_id());
  c0_trace_write_cstr(";tid=");
  c0_trace_write_u64_field(meta.thread_id);
  c0_trace_write_cstr(";seq=");
  c0_trace_write_u64_field(meta.sequence);
  c0_trace_write_cstr(";level=");
  c0_trace_write_cstr(c0_trace_level_name(info ? info->level : C0_TRACE_LEVEL_TRACE));
  c0_trace_write_cstr(";category=");
  c0_trace_write_cstr(info ? info->category_name : "runtime");
  c0_trace_write_cstr(";");
  return 1;
}

static void c0_trace_end_locked(void)
{
  c0_trace_write_cstr("\n");
  if (c0_trace_file_sink != 0 && c0_trace_file_handle)
  {
    ++c0_trace_records_since_flush;
    if (c0_trace_records_since_flush >= C0_TRACE_FLUSH_INTERVAL)
    {
      cursive_platform_handle_flush(c0_trace_file_handle);
      c0_trace_records_since_flush = 0;
    }
  }
  cursive_platform_rwlock_unlock_exclusive(&c0_trace_lock);
}

static uint8_t c0_trace_should_break_on_fail(const C0TraceRecordInfo *info)
{
  if (!info || info->cmp_fail == 0u)
  {
    return 0u;
  }
  return (uint8_t)(cursive_platform_atomic_compare_exchange(&c0_trace_break_on_fail, 0, 0) != 0);
}

static void c0_trace_maybe_trigger_break(const C0TraceRecordInfo *info)
{
  if (c0_trace_should_break_on_fail(info) == 0u)
  {
    return;
  }
  if (cursive_platform_atomic_compare_exchange(&c0_trace_break_latched, 1, 0) != 0)
  {
    return;
  }
  c0_trace_write_console_cstr(
      "[error][log] cmp=fail break triggered (CURSIVE_RUNTIME_BREAK_ON_FAIL=1)\n");
  cursive_platform_debug_break();
}

static void c0_trace_write_payload_prefix(const C0StringView *payload_prefix)
{
  if (!payload_prefix || !payload_prefix->data || payload_prefix->len == 0)
  {
    return;
  }
  c0_trace_write_payload_readable(payload_prefix->data, payload_prefix->len);
}

static void c0_trace_write_hex_bytes(const uint8_t *data, uint64_t len)
{
  static const char hex[] = "0123456789ABCDEF";
  uint8_t chunk[512];
  uint32_t chunk_len = 0u;
  c0_trace_write_cstr("0x");
  if (!data || len == 0)
  {
    return;
  }
  for (uint64_t i = 0; i < len; ++i)
  {
    if (chunk_len + 2u > (uint32_t)sizeof(chunk))
    {
      c0_trace_write_file_bytes(chunk, chunk_len);
      chunk_len = 0u;
    }
    chunk[chunk_len++] = (uint8_t)hex[(data[i] >> 4) & 0xF];
    chunk[chunk_len++] = (uint8_t)hex[data[i] & 0xF];
  }
  if (chunk_len != 0u)
  {
    c0_trace_write_file_bytes(chunk, chunk_len);
  }
}

static void c0_conformance_init(void)
{
  if (cursive_platform_atomic_compare_exchange(&c0_trace_init, 1, 0) != 0)
  {
    return;
  }
  cursive_platform_rwlock_lock_exclusive(&c0_trace_lock);
  c0_trace_apply_env_overrides_locked();
  if (c0_trace_policy_configured == 0)
  {
    cursive_platform_atomic_exchange(&c0_trace_enabled, 0);
    cursive_platform_rwlock_unlock_exclusive(&c0_trace_lock);
    return;
  }
  c0_trace_apply_sink_policy_locked();
  if (c0_trace_file_sink != 0 || c0_trace_console_sink != 0)
  {
    cursive_platform_atomic_exchange(&c0_trace_enabled, 1);
    c0_trace_write_cstr("runtime_trace_v1\n");
  }
  else
  {
    cursive_platform_atomic_exchange(&c0_trace_enabled, 0);
  }
  cursive_platform_rwlock_unlock_exclusive(&c0_trace_lock);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5fsink(
    uint8_t sink_kind,
    const uint8_t *path_utf8,
    uint64_t path_len)
{
  cursive_platform_rwlock_lock_exclusive(&c0_trace_lock);

  c0_trace_policy_configured = 1;
  if (sink_kind == C0_TRACE_SINK_FILE)
  {
    c0_trace_policy_sink_kind = C0_TRACE_SINK_FILE;
  }
  else if (sink_kind == C0_TRACE_SINK_BOTH)
  {
    c0_trace_policy_sink_kind = C0_TRACE_SINK_BOTH;
  }
  else
  {
    c0_trace_policy_sink_kind = C0_TRACE_SINK_CONSOLE;
  }

  c0_trace_replace_filter_locked(
      &c0_trace_policy_path_utf8, &c0_trace_policy_path_len, NULL, 0u);
  if ((c0_trace_policy_sink_kind == C0_TRACE_SINK_FILE ||
       c0_trace_policy_sink_kind == C0_TRACE_SINK_BOTH) &&
      path_utf8 && path_len != 0u && !c0_utf8_has_null(path_utf8, path_len))
  {
    c0_trace_replace_filter_locked(
        &c0_trace_policy_path_utf8, &c0_trace_policy_path_len, path_utf8, path_len);
    if (!c0_trace_policy_path_utf8 || c0_trace_policy_path_len == 0u)
    {
      c0_trace_policy_sink_kind = C0_TRACE_SINK_CONSOLE;
    }
  }

  if (c0_trace_init != 0)
  {
    c0_trace_apply_sink_policy_locked();
    if (c0_trace_file_sink != 0 || c0_trace_console_sink != 0)
    {
      cursive_platform_atomic_exchange(&c0_trace_enabled, 1);
      c0_trace_write_cstr("runtime_trace_v1\n");
    }
    else
    {
      cursive_platform_atomic_exchange(&c0_trace_enabled, 0);
    }
  }

  cursive_platform_rwlock_unlock_exclusive(&c0_trace_lock);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5froot(
    const uint8_t *path_utf8,
    uint64_t path_len)
{
  cursive_platform_rwlock_lock_exclusive(&c0_trace_lock);
  if (c0_trace_root_utf8)
  {
    c0_heap_free_raw(c0_trace_root_utf8);
    c0_trace_root_utf8 = NULL;
  }
  c0_trace_root_len = 0u;
  if (path_utf8 && path_len != 0u && !c0_utf8_has_null(path_utf8, path_len) &&
      path_len <= (uint64_t)SIZE_MAX)
  {
    uint8_t *copy = (uint8_t *)c0_heap_alloc_raw((size_t)path_len);
    if (copy)
    {
      c0_memcpy(copy, path_utf8, (size_t)path_len);
      c0_trace_root_utf8 = copy;
      c0_trace_root_len = path_len;
    }
  }
  cursive_platform_rwlock_unlock_exclusive(&c0_trace_lock);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5flog_x5ffilter(
    uint8_t mask_bits)
{
  uint32_t mask = ((uint32_t)mask_bits) & C0_TRACE_FILTER_ALL;
  if (mask == 0u)
  {
    mask = C0_TRACE_FILTER_ALL;
  }
  cursive_platform_atomic_exchange(&c0_trace_filter_mask, (cursive_platform_i32_t)mask);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5fmin_x5flevel(
    uint8_t level)
{
  uint8_t clamped = level;
  if (clamped > C0_TRACE_LEVEL_ERROR)
  {
    clamped = C0_TRACE_LEVEL_ERROR;
  }
  cursive_platform_atomic_exchange(&c0_trace_min_level, (cursive_platform_i32_t)clamped);
}

static uint64_t c0_format_string_preview(const uint8_t *data,
                                         uint64_t len,
                                         uint8_t *out,
                                         uint64_t cap)
{
  static const char hex[] = "0123456789ABCDEF";
  uint64_t pos = 0u;
  if (!out || cap < 2u)
  {
    return 0u;
  }

  out[pos++] = (uint8_t)'"';
  if (data && len != 0u)
  {
    const uint64_t reserve = (cap > 8u) ? 4u : 1u;
    for (uint64_t i = 0u; i < len; ++i)
    {
      if (pos + reserve >= cap)
      {
        if (pos + 4u < cap)
        {
          out[pos++] = (uint8_t)'.';
          out[pos++] = (uint8_t)'.';
          out[pos++] = (uint8_t)'.';
        }
        break;
      }
      const uint8_t c = data[i];
      if (c == (uint8_t)'\\' || c == (uint8_t)'"')
      {
        if (pos + 2u >= cap)
        {
          break;
        }
        out[pos++] = (uint8_t)'\\';
        out[pos++] = c;
      }
      else if (c == (uint8_t)'\n')
      {
        if (pos + 2u >= cap)
        {
          break;
        }
        out[pos++] = (uint8_t)'\\';
        out[pos++] = (uint8_t)'n';
      }
      else if (c == (uint8_t)'\r')
      {
        if (pos + 2u >= cap)
        {
          break;
        }
        out[pos++] = (uint8_t)'\\';
        out[pos++] = (uint8_t)'r';
      }
      else if (c == (uint8_t)'\t')
      {
        if (pos + 2u >= cap)
        {
          break;
        }
        out[pos++] = (uint8_t)'\\';
        out[pos++] = (uint8_t)'t';
      }
      else if (c < 32u || c > 126u)
      {
        if (pos + 4u >= cap)
        {
          break;
        }
        out[pos++] = (uint8_t)'\\';
        out[pos++] = (uint8_t)'x';
        out[pos++] = (uint8_t)hex[(c >> 4u) & 0xFu];
        out[pos++] = (uint8_t)hex[c & 0xFu];
      }
      else
      {
        out[pos++] = c;
      }
    }
  }

  if (pos < cap)
  {
    out[pos++] = (uint8_t)'"';
  }
  return pos;
}

static uint64_t c0_format_bytes_preview(const uint8_t *data,
                                        uint64_t len,
                                        uint8_t *out,
                                        uint64_t cap)
{
  static const char hex[] = "0123456789ABCDEF";
  uint64_t pos = 0u;
  if (!out || cap < 3u)
  {
    return 0u;
  }

  out[pos++] = (uint8_t)'0';
  out[pos++] = (uint8_t)'x';
  if (!data || len == 0u)
  {
    return pos;
  }

  const uint64_t max_bytes = (cap > 8u) ? ((cap - 5u) / 2u) : 0u;
  const uint64_t emit_len = (len < max_bytes) ? len : max_bytes;
  for (uint64_t i = 0u; i < emit_len; ++i)
  {
    out[pos++] = (uint8_t)hex[(data[i] >> 4u) & 0xFu];
    out[pos++] = (uint8_t)hex[data[i] & 0xFu];
  }
  if (emit_len < len && pos + 3u < cap)
  {
    out[pos++] = (uint8_t)'.';
    out[pos++] = (uint8_t)'.';
    out[pos++] = (uint8_t)'.';
  }
  return pos;
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload, &record_info, &record_meta))
  {
    return;
  }
  if (payload && payload->data && payload->len != 0)
  {
    c0_trace_write_payload_readable(payload->data, payload->len);
    c0_trace_write_console_pretty_locked(
        rule_id, file, start_line, start_col, payload, &record_meta,
        &record_info, NULL, 0u);
  }
  else if (c0_rule_id_has_log_prefix(rule_id))
  {
    const uint8_t *label_data = rule_id->data;
    uint64_t label_len = rule_id->len;
    static const uint8_t unit_actual[] = {'(', ')'};
    if (label_len > 4u)
    {
      label_data += 4u;
      label_len -= 4u;
    }
    c0_trace_write_cstr("label=");
    c0_trace_write_payload_readable(label_data, label_len);
    c0_trace_write_cstr(";actual=()");
    c0_trace_write_console_pretty_locked(
        rule_id, file, start_line, start_col, NULL, &record_meta,
        &record_info, unit_actual, 2u);
  }
  else
  {
    c0_trace_write_console_pretty_locked(
        rule_id, file, start_line, start_col, NULL, &record_meta,
        &record_info, NULL, 0u);
  }
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fint(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    uint64_t raw,
    uint8_t bits,
    uint8_t is_signed)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);

  char text[64];
  uint32_t len = 0;
  uint8_t width = bits;
  if (width == 0u || width > 64u)
  {
    width = 64u;
  }

  if (width < 64u)
  {
    uint64_t mask = (1ull << width) - 1ull;
    raw &= mask;
  }

  if (is_signed != 0u)
  {
    int64_t signed_value = 0;
    if (width == 64u)
    {
      signed_value = (int64_t)raw;
    }
    else
    {
      const uint64_t sign_bit = 1ull << (width - 1u);
      uint64_t extended = raw;
      if ((extended & sign_bit) != 0u)
      {
        const uint64_t top_mask = ~((1ull << width) - 1ull);
        extended |= top_mask;
      }
      signed_value = (int64_t)extended;
    }
    len = c0_i64_to_dec(signed_value, text);
  }
  else
  {
    len = c0_u64_to_dec(raw, text);
  }
  c0_trace_write_encoded((const uint8_t *)text, len);
  c0_trace_write_console_pretty_locked(
      rule_id, file, start_line, start_col, payload_prefix, &record_meta,
      &record_info,
      (const uint8_t *)text, len);
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbool(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    uint8_t actual)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);
  if (actual != 0u)
  {
    c0_trace_write_encoded((const uint8_t *)"true", 4u);
    c0_trace_write_console_pretty_locked(
        rule_id, file, start_line, start_col, payload_prefix, &record_meta,
        &record_info,
        (const uint8_t *)"true", 4u);
  }
  else
  {
    c0_trace_write_encoded((const uint8_t *)"false", 5u);
    c0_trace_write_console_pretty_locked(
        rule_id, file, start_line, start_col, payload_prefix, &record_meta,
        &record_info,
        (const uint8_t *)"false", 5u);
  }
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5ffloat(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    double actual,
    uint8_t bits)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);
  char text[80];
  uint32_t len = c0_format_float_text(actual, bits, text, (uint32_t)sizeof(text));
  if (len == 0u)
  {
    const char *fallback = "0.0";
    len = 3u;
    c0_memcpy(text, fallback, (size_t)len);
  }
  c0_trace_write_encoded((const uint8_t *)text, len);
  c0_trace_write_console_pretty_locked(
      rule_id, file, start_line, start_col, payload_prefix, &record_meta,
      &record_info,
      (const uint8_t *)text, len);
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fptr(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    const void *actual)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);

  uint64_t raw = (uint64_t)(uintptr_t)actual;
  char text[32];
  text[0] = '0';
  text[1] = 'x';
  uint32_t len = 2u + c0_u64_to_hex(raw, text + 2u, 16u);
  c0_trace_write_encoded((const uint8_t *)text, len);
  c0_trace_write_console_pretty_locked(
      rule_id, file, start_line, start_col, payload_prefix, &record_meta,
      &record_info,
      (const uint8_t *)text, len);
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fstring(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    const C0StringView *actual)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);
  uint8_t preview[512];
  uint64_t preview_len = 0u;
  if (actual && actual->data && actual->len != 0u)
  {
    preview_len = c0_format_string_preview(
        actual->data, actual->len, preview, (uint64_t)sizeof(preview));
  }
  else
  {
    preview_len = c0_format_string_preview(NULL, 0u, preview, (uint64_t)sizeof(preview));
  }
  c0_trace_write_file_bytes((const uint8_t *)"\"", 1u);
  if (actual && actual->data && actual->len != 0u)
  {
    c0_trace_write_encoded(actual->data, actual->len);
  }
  c0_trace_write_file_bytes((const uint8_t *)"\"", 1u);
  c0_trace_write_console_pretty_locked(
      rule_id, file, start_line, start_col, payload_prefix, &record_meta,
      &record_info,
      preview, preview_len);
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fstring_x5fmanaged(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    const C0StringManaged *actual)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);
  uint8_t preview[512];
  uint64_t preview_len = 0u;
  if (actual && actual->data && actual->len != 0u)
  {
    preview_len = c0_format_string_preview(
        actual->data, actual->len, preview, (uint64_t)sizeof(preview));
  }
  else
  {
    preview_len = c0_format_string_preview(NULL, 0u, preview, (uint64_t)sizeof(preview));
  }
  c0_trace_write_file_bytes((const uint8_t *)"\"", 1u);
  if (actual && actual->data && actual->len != 0u)
  {
    c0_trace_write_encoded(actual->data, actual->len);
  }
  c0_trace_write_file_bytes((const uint8_t *)"\"", 1u);
  c0_trace_write_console_pretty_locked(
      rule_id, file, start_line, start_col, payload_prefix, &record_meta,
      &record_info,
      preview, preview_len);
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbytes(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    const C0BytesView *actual)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);
  uint8_t preview[192];
  uint64_t preview_len = 0u;
  if (actual)
  {
    preview_len = c0_format_bytes_preview(
        actual->data, actual->len, preview, (uint64_t)sizeof(preview));
    c0_trace_write_hex_bytes(actual->data, actual->len);
  }
  else
  {
    preview_len = c0_format_bytes_preview(NULL, 0u, preview, (uint64_t)sizeof(preview));
    c0_trace_write_hex_bytes(NULL, 0u);
  }
  c0_trace_write_console_pretty_locked(
      rule_id, file, start_line, start_col, payload_prefix, &record_meta,
      &record_info,
      preview, preview_len);
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}

void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbytes_x5fmanaged(
    const C0StringView *rule_id,
    const C0StringView *file,
    uint64_t start_line,
    uint64_t start_col,
    uint64_t end_line,
    uint64_t end_col,
    const C0StringView *payload_prefix,
    const C0BytesManaged *actual)
{
  C0TraceRecordInfo record_info;
  C0TraceRecordMeta record_meta;
  c0_trace_compute_record_info(payload_prefix, &record_info);
  if (!c0_trace_begin_locked(
          rule_id, file, start_line, start_col, end_line, end_col,
          payload_prefix, &record_info, &record_meta))
  {
    return;
  }
  c0_trace_write_payload_prefix(payload_prefix);
  uint8_t preview[192];
  uint64_t preview_len = 0u;
  if (actual)
  {
    preview_len = c0_format_bytes_preview(
        actual->data, actual->len, preview, (uint64_t)sizeof(preview));
    c0_trace_write_hex_bytes(actual->data, actual->len);
  }
  else
  {
    preview_len = c0_format_bytes_preview(NULL, 0u, preview, (uint64_t)sizeof(preview));
    c0_trace_write_hex_bytes(NULL, 0u);
  }
  c0_trace_write_console_pretty_locked(
      rule_id, file, start_line, start_col, payload_prefix, &record_meta,
      &record_info,
      preview, preview_len);
  c0_trace_end_locked();
  c0_trace_maybe_trigger_break(&record_info);
}
