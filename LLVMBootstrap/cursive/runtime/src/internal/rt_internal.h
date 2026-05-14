#ifndef CURSIVE_RT_INTERNAL_H
#define CURSIVE_RT_INTERNAL_H

#include "../../include/cursive_rt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rt_platform.h"

// -----------------------------------------------------------------------------
// Internal runtime state
// -----------------------------------------------------------------------------

typedef struct C0HeapState {
  struct C0HeapState* parent;
  uint64_t quota;
  uint64_t used;
} C0HeapState;

typedef struct C0AllocHeader {
  C0HeapState* heap;
  uint64_t cap;
} C0AllocHeader;

typedef struct C0RawAllocHeader {
  C0HeapState* heap;
  uint64_t size;
} C0RawAllocHeader;

_Static_assert(sizeof(C0AllocHeader) == sizeof(C0RawAllocHeader),
               "managed/raw allocation headers must stay layout-compatible");
_Static_assert(offsetof(C0AllocHeader, heap) == offsetof(C0RawAllocHeader, heap),
               "managed/raw allocation headers must agree on heap offset");
_Static_assert(offsetof(C0AllocHeader, cap) == offsetof(C0RawAllocHeader, size),
               "managed/raw allocation headers must agree on size offset");

typedef struct C0FsState {
  uint8_t* base_utf8;
  uint32_t base_len;
  int restricted;
  int valid;
} C0FsState;

typedef struct C0NetState {
  struct C0NetState* parent;
  uint8_t* host_utf8;
  uint32_t host_len;
  int restricted;
  int valid;
} C0NetState;

typedef enum C0TimeStateKind {
  C0_TIME_STATE_ROOT = 0,
  C0_TIME_STATE_MONOTONIC = 1,
  C0_TIME_STATE_WALL = 2,
} C0TimeStateKind;

typedef struct C0TimeState {
  struct C0TimeState* parent;
  uint8_t kind;
  uint8_t _pad[7];
  uint64_t domain;
  C0U128 resolution;
} C0TimeState;

typedef struct C0DirIterState {
  wchar_t* base_path;
  uint32_t base_len;
  uint8_t* base_utf8;
  uint32_t base_utf8_len;
  wchar_t** names;
  uint32_t* name_lens;
  uint8_t* kinds;
  uint32_t count;
  uint32_t index;
} C0DirIterState;

// -----------------------------------------------------------------------------
// Hosted library session support
// -----------------------------------------------------------------------------

uint64_t cursive_host_session_register(const void* owner, void* env);
int cursive_host_session_try_enter(uint64_t handle,
                                   const void* owner,
                                   void** out_env);
int cursive_host_session_leave(uint64_t handle, const void* owner);
int cursive_host_session_try_retire(uint64_t handle,
                                    const void* owner,
                                    void** out_env);
int cursive_host_session_abort_live(uint64_t handle,
                                    const void* owner,
                                    void** out_env);
void* cursive_host_session_current_env(void);
int cursive_host_session_enter_retired(uint64_t handle,
                                       const void* owner,
                                       void* env);
int cursive_host_session_leave_retired(uint64_t handle, const void* owner);
int cursive_host_session_abort_retired(uint64_t handle,
                                       const void* owner,
                                       void* env);
void* cursive_host_alloc(size_t size);
void cursive_host_free(void* ptr);

// -----------------------------------------------------------------------------
// Utility helpers (no CRT)
// -----------------------------------------------------------------------------

static __inline uint64_t c0_min_u64(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

static __inline uint64_t c0_align_up(uint64_t value, uint64_t align) {
  if (align == 0) {
    return value;
  }
  const uint64_t rem = value % align;
  if (rem == 0) {
    return value;
  }
  return value + (align - rem);
}

static __inline cursive_rt_handle_t c0_process_heap(void) {
  static cursive_rt_handle_t heap = NULL;
  if (!heap) {
    heap = cursive_rt_heap_handle();
  }
  return heap;
}

static __inline void c0_trace_emit_rule(const char* rule_id);
static __inline void c0_trace_emit_rule_with_meta(const char* rule_id,
                                                  const char* category,
                                                  const char* level);

static __inline void* c0_heap_alloc_raw(size_t size) {
  return cursive_rt_heap_alloc(c0_process_heap(), 0, size);
}

static __inline void c0_heap_free_raw(void* ptr) {
  if (ptr) {
    cursive_rt_dword_t saved_error = cursive_rt_last_error_get();
    cursive_rt_handle_t heap = c0_process_heap();
    if (cursive_rt_heap_validate(heap, 0, ptr) == 0) {
      c0_trace_emit_rule("Log-HeapFreeRaw-InvalidPointer");
      cursive_rt_last_error_set(saved_error);
      return;
    }
    if (cursive_rt_heap_free(heap, 0, ptr) != 0) {
      cursive_rt_last_error_set(saved_error);
    }
  }
}

static __inline void* c0_memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

static __inline void* c0_memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  if (d == s || n == 0) {
    return dst;
  }
  if (d < s) {
    for (size_t i = 0; i < n; ++i) {
      d[i] = s[i];
    }
  } else {
    for (size_t i = n; i > 0; --i) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

static __inline void* c0_memset(void* dst, int c, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char v = (unsigned char)c;
  for (size_t i = 0; i < n; ++i) {
    d[i] = v;
  }
  return dst;
}

static __inline size_t c0_wcslen(const wchar_t* s) {
  size_t n = 0;
  if (!s) {
    return 0;
  }
  while (s[n] != 0) {
    ++n;
  }
  return n;
}

static __inline uint64_t c0_cstr_len(const char* s) {
  uint64_t n = 0;
  if (!s) {
    return 0;
  }
  while (s[n] != 0) {
    ++n;
  }
  return n;
}

static __inline void c0_trace_emit_rule_with_meta(const char* rule_id,
                                                  const char* category,
                                                  const char* level) {
  if (!rule_id) {
    return;
  }
  C0StringView rule_view;
  rule_view.data = (const uint8_t*)rule_id;
  rule_view.len = c0_cstr_len(rule_id);

  char payload_buf[96];
  uint64_t payload_len = 0u;
  const char* category_text = category ? category : "runtime";
  const char* level_text = level ? level : "trace";

  const char* category_key = "category=";
  for (uint64_t i = 0u; category_key[i] != 0 && payload_len + 1u < (uint64_t)sizeof(payload_buf); ++i) {
    payload_buf[payload_len++] = category_key[i];
  }
  for (uint64_t i = 0u; category_text[i] != 0 && payload_len + 1u < (uint64_t)sizeof(payload_buf); ++i) {
    payload_buf[payload_len++] = category_text[i];
  }
  if (payload_len + 1u < (uint64_t)sizeof(payload_buf)) {
    payload_buf[payload_len++] = ';';
  }
  const char* level_key = "level=";
  for (uint64_t i = 0u; level_key[i] != 0 && payload_len + 1u < (uint64_t)sizeof(payload_buf); ++i) {
    payload_buf[payload_len++] = level_key[i];
  }
  for (uint64_t i = 0u; level_text[i] != 0 && payload_len + 1u < (uint64_t)sizeof(payload_buf); ++i) {
    payload_buf[payload_len++] = level_text[i];
  }

  C0StringView payload;
  payload.data = (const uint8_t*)payload_buf;
  payload.len = payload_len;
  cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
      &rule_view,
      NULL,
      0u,
      0u,
      0u,
      0u,
      &payload);
}

static __inline void c0_trace_emit_rule(const char* rule_id) {
  c0_trace_emit_rule_with_meta(rule_id, "runtime", "trace");
}

static __inline int c0_utf8_has_null(const uint8_t* data, uint64_t len) {
  for (uint64_t i = 0; i < len; ++i) {
    if (data[i] == 0) {
      return 1;
    }
  }
  return 0;
}

static __inline int c0_is_abs_path(const wchar_t* path, uint32_t len) {
  if (!path || len == 0) {
    return 0;
  }
  if (len >= 2 && path[1] == L':') {
    return 1;
  }
  if (len >= 2 && path[0] == L'\\' && path[1] == L'\\') {
    return 1;
  }
  if (path[0] == L'\\' || path[0] == L'/') {
    return 1;
  }
  return 0;
}

static __inline int c0_heap_can_alloc(C0HeapState* heap,
                                      uint64_t size,
                                      int* out_quota_exceeded) {
  if (out_quota_exceeded) {
    *out_quota_exceeded = 0;
  }
  if (!heap || size == 0) {
    return 1;
  }
  uint32_t guard = 0;
  for (C0HeapState* cur = heap; cur; cur = cur->parent) {
    if (cursive_rt_heap_validate(c0_process_heap(), 0, cur) == 0) {
      c0_trace_emit_rule("Log-HeapCanAlloc-InvalidState");
      return 0;
    }
    ++guard;
    if (guard > 1024u) {
      c0_trace_emit_rule("Log-HeapCanAlloc-ParentCycle");
      return 0;
    }
    if (cur->quota != 0) {
      if (cur->used >= cur->quota) {
        if (out_quota_exceeded) {
          *out_quota_exceeded = 1;
        }
        return 0;
      }
      if (size > cur->quota - cur->used) {
        if (out_quota_exceeded) {
          *out_quota_exceeded = 1;
        }
        return 0;
      }
    }
  }
  return 1;
}

static __inline void c0_heap_add_used(C0HeapState* heap, uint64_t size) {
  if (!heap || size == 0) {
    return;
  }
  for (C0HeapState* cur = heap; cur; cur = cur->parent) {
    cur->used += size;
  }
}

static __inline void c0_heap_sub_used(C0HeapState* heap, uint64_t size) {
  if (!heap || size == 0) {
    return;
  }
  uint32_t guard = 0;
  for (C0HeapState* cur = heap; cur; cur = cur->parent) {
    if (cur->used >= size) {
      cur->used -= size;
    } else {
      cur->used = 0;
    }
    ++guard;
    if (guard > 1024u) {
      c0_trace_emit_rule("HeapSubUsed-ParentCycle");
      break;
    }
  }
}

// -----------------------------------------------------------------------------
// Allocation helpers for managed string/bytes
// -----------------------------------------------------------------------------

static __inline C0AllocHeader* c0_header_from_data(uint8_t* data) {
  return data ? ((C0AllocHeader*)data) - 1 : NULL;
}

static __inline C0RawAllocHeader* c0_raw_header_from_data(void* data) {
  return data ? ((C0RawAllocHeader*)data) - 1 : NULL;
}

static __inline C0HeapState* c0_recorded_owner_heap(void* data) {
  C0RawAllocHeader* header = c0_raw_header_from_data(data);
  return header ? header->heap : NULL;
}

static __inline uint64_t c0_recorded_alloc_size(void* data) {
  C0RawAllocHeader* header = c0_raw_header_from_data(data);
  return header ? header->size : 0;
}

static __inline void c0_heap_dealloc_recorded(C0HeapState* heap,
                                              void** ptr,
                                              uint64_t count) {
  (void)heap;
  if (!ptr || !*ptr) {
    return;
  }

  C0RawAllocHeader* header = c0_raw_header_from_data(*ptr);
  if (!header) {
    return;
  }
  if (cursive_rt_heap_validate(c0_process_heap(), 0, header) == 0) {
    c0_trace_emit_rule("Log-HeapRawDealloc-InvalidPointer");
    return;
  }
  if (count != header->size) {
    c0_trace_emit_rule("Log-HeapRawDealloc-CountMismatch");
  }

  c0_heap_sub_used(header->heap, header->size);
  c0_heap_free_raw(header);
  *ptr = NULL;
}

static __inline void c0_drop_managed_bytes(uint8_t** data, uint64_t count) {
  if (!data || !*data) {
    return;
  }

  c0_trace_emit_rule("ManagedFree-Enter");
  C0HeapState* owner = c0_recorded_owner_heap(*data);
  c0_heap_dealloc_recorded(owner, (void**)data, count);
  c0_trace_emit_rule("ManagedFree-Exit");
}

static __inline uint8_t* c0_alloc_managed_bytes(
    C0HeapState* heap,
    uint64_t cap,
    int* out_quota_exceeded) {
  if (cap == 0) {
    return NULL;
  }
  if (!c0_heap_can_alloc(heap, cap, out_quota_exceeded)) {
    return NULL;
  }
  const uint64_t total = cap + (uint64_t)sizeof(C0AllocHeader);
  if (total < cap || total > (uint64_t)SIZE_MAX) {
    return NULL;
  }
  C0AllocHeader* header = (C0AllocHeader*)c0_heap_alloc_raw((size_t)total);
  if (!header) {
    return NULL;
  }
  header->heap = heap;
  header->cap = cap;
  c0_heap_add_used(heap, cap);
  return (uint8_t*)(header + 1);
}

static __inline void c0_free_managed_bytes(uint8_t* data) {
  if (!data) {
    return;
  }
  uint8_t* local = data;
  c0_drop_managed_bytes(&local, c0_recorded_alloc_size(data));
}

// -----------------------------------------------------------------------------
// UTF-8 helpers
// -----------------------------------------------------------------------------

static __inline int c0_utf8_valid(const uint8_t* data, uint64_t len) {
  uint64_t i = 0;
  while (i < len) {
    uint8_t c = data[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    if (c >= 0xC2 && c <= 0xDF) {
      if (i + 1 >= len) return 0;
      if ((data[i + 1] & 0xC0) != 0x80) return 0;
      i += 2;
      continue;
    }
    if (c == 0xE0) {
      if (i + 2 >= len) return 0;
      if (data[i + 1] < 0xA0 || data[i + 1] > 0xBF) return 0;
      if ((data[i + 2] & 0xC0) != 0x80) return 0;
      i += 3;
      continue;
    }
    if (c >= 0xE1 && c <= 0xEC) {
      if (i + 2 >= len) return 0;
      if ((data[i + 1] & 0xC0) != 0x80) return 0;
      if ((data[i + 2] & 0xC0) != 0x80) return 0;
      i += 3;
      continue;
    }
    if (c == 0xED) {
      if (i + 2 >= len) return 0;
      if (data[i + 1] < 0x80 || data[i + 1] > 0x9F) return 0;
      if ((data[i + 2] & 0xC0) != 0x80) return 0;
      i += 3;
      continue;
    }
    if (c >= 0xEE && c <= 0xEF) {
      if (i + 2 >= len) return 0;
      if ((data[i + 1] & 0xC0) != 0x80) return 0;
      if ((data[i + 2] & 0xC0) != 0x80) return 0;
      i += 3;
      continue;
    }
    if (c == 0xF0) {
      if (i + 3 >= len) return 0;
      if (data[i + 1] < 0x90 || data[i + 1] > 0xBF) return 0;
      if ((data[i + 2] & 0xC0) != 0x80) return 0;
      if ((data[i + 3] & 0xC0) != 0x80) return 0;
      i += 4;
      continue;
    }
    if (c >= 0xF1 && c <= 0xF3) {
      if (i + 3 >= len) return 0;
      if ((data[i + 1] & 0xC0) != 0x80) return 0;
      if ((data[i + 2] & 0xC0) != 0x80) return 0;
      if ((data[i + 3] & 0xC0) != 0x80) return 0;
      i += 4;
      continue;
    }
    if (c == 0xF4) {
      if (i + 3 >= len) return 0;
      if (data[i + 1] < 0x80 || data[i + 1] > 0x8F) return 0;
      if ((data[i + 2] & 0xC0) != 0x80) return 0;
      if ((data[i + 3] & 0xC0) != 0x80) return 0;
      i += 4;
      continue;
    }
    return 0;
  }
  return 1;
}

// Convert UTF-8 bytes to wide string (allocates with process heap)
static __inline wchar_t* c0_utf8_to_wide(
    const uint8_t* data,
    uint64_t len,
    uint32_t* out_len) {
  if (out_len) {
    *out_len = 0;
  }
  if (!data && len != 0) {
    return NULL;
  }
  if (len == 0) {
    return NULL;
  }
  if (len > (uint64_t)INT32_MAX) {
    return NULL;
  }
  if (c0_utf8_has_null(data, len)) {
    return NULL;
  }
  int wide_len = cursive_rt_utf8_to_wide_chars((const char*)data,
                                               (int)len,
                                               NULL,
                                               0);
  if (wide_len <= 0) {
    return NULL;
  }
  const size_t bytes = (size_t)(wide_len + 1) * sizeof(wchar_t);
  wchar_t* out = (wchar_t*)c0_heap_alloc_raw(bytes);
  if (!out) {
    return NULL;
  }
  int written = cursive_rt_utf8_to_wide_chars((const char*)data,
                                              (int)len,
                                              out,
                                              wide_len);
  if (written != wide_len) {
    c0_heap_free_raw(out);
    return NULL;
  }
  out[wide_len] = 0;
  if (out_len) {
    *out_len = (uint32_t)wide_len;
  }
  return out;
}

// Convert wide string to UTF-8 (allocates with process heap)
static __inline uint8_t* c0_wide_to_utf8(
    const wchar_t* data,
    uint32_t len,
    uint32_t* out_len) {
  if (out_len) {
    *out_len = 0;
  }
  if (!data || len == 0) {
    return NULL;
  }
  if (len > (uint32_t)INT32_MAX) {
    return NULL;
  }
  int byte_len = cursive_rt_wide_to_utf8_chars(data,
                                               (int)len,
                                               NULL,
                                               0);
  if (byte_len <= 0) {
    return NULL;
  }
  uint8_t* out = (uint8_t*)c0_heap_alloc_raw((size_t)byte_len);
  if (!out) {
    return NULL;
  }
  int written = cursive_rt_wide_to_utf8_chars(data,
                                              (int)len,
                                              (char*)out,
                                              byte_len);
  if (written != byte_len) {
    c0_heap_free_raw(out);
    return NULL;
  }
  if (out_len) {
    *out_len = (uint32_t)byte_len;
  }
  return out;
}

// -----------------------------------------------------------------------------
// Parallel panic integration (see parallel.c / panic.c)
// -----------------------------------------------------------------------------
int c0_parallel_in_panic_scope(void);
void c0_parallel_raise_panic(uint32_t code);

// -----------------------------------------------------------------------------
// Size checks (Win64 expectations)
// -----------------------------------------------------------------------------

_Static_assert(sizeof(void*) == 8, "runtime expects 64-bit pointers");
_Static_assert(sizeof(C0StringView) == 16, "string@View layout");
_Static_assert(sizeof(C0StringManaged) == 24, "string@Managed layout");
_Static_assert(sizeof(C0BytesView) == 16, "bytes@View layout");
_Static_assert(sizeof(C0BytesManaged) == 24, "bytes@Managed layout");
_Static_assert(sizeof(C0DynObject) == 16, "dynamic object layout");
_Static_assert(sizeof(C0Context) == 80, "Context layout");
_Static_assert(sizeof(C0ExecutionDomain) == 24, "ExecutionDomain layout");
_Static_assert(sizeof(C0Duration) == 16, "Duration layout");
_Static_assert(sizeof(C0MonotonicInstant) == 32, "MonotonicInstant layout");
_Static_assert(sizeof(C0UtcInstant) == 16, "UtcInstant layout");
_Static_assert(sizeof(C0Union_Duration_TimeError) == 32,
               "Outcome<Duration, TimeError> layout");
_Static_assert(sizeof(C0Union_DynObject_TimeError) == 24,
               "Outcome<dyn, TimeError> layout");
_Static_assert(sizeof(C0Union_UtcInstant_TimeError) == 32,
               "Outcome<UtcInstant, TimeError> layout");
_Static_assert(sizeof(C0StringModal) == 32, "string modal layout");
_Static_assert(sizeof(C0RegionOptions) == 40, "RegionOptions layout");
_Static_assert(sizeof(C0Range) == 24, "Range layout");
_Static_assert(sizeof(C0AllocationError) == 16, "AllocationError layout");
_Static_assert(sizeof(C0DirEntry) == 56, "DirEntry layout");
_Static_assert(sizeof(C0Union_StringManaged_AllocError) == 32, "string|AllocError layout");
_Static_assert(sizeof(C0Union_BytesManaged_AllocError) == 32, "bytes|AllocError layout");
_Static_assert(sizeof(C0Union_Unit_AllocError) == 24, "unit|AllocError layout");
_Static_assert(sizeof(C0Union_StringManaged_IoError) == 32, "string|IoError layout");
_Static_assert(sizeof(C0Union_BytesManaged_IoError) == 32, "bytes|IoError layout");
_Static_assert(sizeof(C0Union_File_IoError) == 16, "file|IoError layout");
_Static_assert(sizeof(C0Union_DirIter_IoError) == 16, "diriter|IoError layout");
_Static_assert(sizeof(C0Union_Unit_IoError) == 2, "unit|IoError layout");
_Static_assert(sizeof(C0Union_FileKind_IoError) == 2, "filekind|IoError layout");
_Static_assert(sizeof(C0Union_DirEntry_Unit_IoError) == 72, "Outcome<DirEntry|(), IoError> layout");

#endif  // CURSIVE0_RT_INTERNAL_H

