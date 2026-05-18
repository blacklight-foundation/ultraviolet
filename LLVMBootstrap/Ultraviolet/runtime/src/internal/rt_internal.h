#ifndef UV_RT_INTERNAL_H
#define UV_RT_INTERNAL_H

#include "../../include/uv_rt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rt_platform.h"

// -----------------------------------------------------------------------------
// Internal runtime state
// -----------------------------------------------------------------------------

typedef struct UVHeapState {
  struct UVHeapState* parent;
  uint64_t quota;
  uint64_t used;
} UVHeapState;

typedef struct UVAllocHeader {
  UVHeapState* heap;
  uint64_t cap;
} UVAllocHeader;

typedef struct UVRawAllocHeader {
  UVHeapState* heap;
  uint64_t size;
} UVRawAllocHeader;

_Static_assert(sizeof(UVAllocHeader) == sizeof(UVRawAllocHeader),
               "managed/raw allocation headers must stay layout-compatible");
_Static_assert(offsetof(UVAllocHeader, heap) == offsetof(UVRawAllocHeader, heap),
               "managed/raw allocation headers must agree on heap offset");
_Static_assert(offsetof(UVAllocHeader, cap) == offsetof(UVRawAllocHeader, size),
               "managed/raw allocation headers must agree on size offset");

typedef struct UVFsState {
  uint8_t* base_utf8;
  uint32_t base_len;
  int restricted;
  int valid;
} UVFsState;

typedef struct UVNetState {
  struct UVNetState* parent;
  uint8_t* host_utf8;
  uint32_t host_len;
  int restricted;
  int valid;
} UVNetState;

typedef enum UVTimeStateKind {
  UV_TIME_STATE_ROOT = 0,
  UV_TIME_STATE_MONOTONIC = 1,
  UV_TIME_STATE_WALL = 2,
} UVTimeStateKind;

typedef struct UVTimeState {
  struct UVTimeState* parent;
  uint8_t kind;
  uint8_t _pad[7];
  uint64_t domain;
  UVU128 resolution;
} UVTimeState;

typedef struct UVDirIterState {
  wchar_t* base_path;
  uint32_t base_len;
  uint8_t* base_utf8;
  uint32_t base_utf8_len;
  wchar_t** names;
  uint32_t* name_lens;
  uint8_t* kinds;
  uint32_t count;
  uint32_t index;
} UVDirIterState;

// -----------------------------------------------------------------------------
// Hosted library session support
// -----------------------------------------------------------------------------

uint64_t uv_host_session_register(const void* owner, void* env);
int uv_host_session_try_enter(uint64_t handle,
                                   const void* owner,
                                   void** out_env);
int uv_host_session_leave(uint64_t handle, const void* owner);
int uv_host_session_try_retire(uint64_t handle,
                                    const void* owner,
                                    void** out_env);
int uv_host_session_abort_live(uint64_t handle,
                                    const void* owner,
                                    void** out_env);
void* uv_host_session_current_env(void);
int uv_host_session_enter_retired(uint64_t handle,
                                       const void* owner,
                                       void* env);
int uv_host_session_leave_retired(uint64_t handle, const void* owner);
int uv_host_session_abort_retired(uint64_t handle,
                                       const void* owner,
                                       void* env);
void* uv_host_alloc(size_t size);
void uv_host_free(void* ptr);

// -----------------------------------------------------------------------------
// Utility helpers (no CRT)
// -----------------------------------------------------------------------------

static __inline uint64_t uv_min_u64(uint64_t a, uint64_t b) {
  return a < b ? a : b;
}

static __inline uint64_t uv_align_up(uint64_t value, uint64_t align) {
  if (align == 0) {
    return value;
  }
  const uint64_t rem = value % align;
  if (rem == 0) {
    return value;
  }
  return value + (align - rem);
}

static __inline uv_rt_handle_t uv_process_heap(void) {
  static uv_rt_handle_t heap = NULL;
  if (!heap) {
    heap = uv_rt_heap_handle();
  }
  return heap;
}

static __inline void uv_trace_emit_rule(const char* rule_id);
static __inline void uv_trace_emit_rule_with_meta(const char* rule_id,
                                                  const char* category,
                                                  const char* level);

static __inline void* uv_heap_alloc_raw(size_t size) {
  return uv_rt_heap_alloc(uv_process_heap(), 0, size);
}

static __inline void uv_heap_free_raw(void* ptr) {
  if (ptr) {
    uv_rt_dword_t saved_error = uv_rt_last_error_get();
    uv_rt_handle_t heap = uv_process_heap();
    if (uv_rt_heap_validate(heap, 0, ptr) == 0) {
      uv_trace_emit_rule("Log-HeapFreeRaw-InvalidPointer");
      uv_rt_last_error_set(saved_error);
      return;
    }
    if (uv_rt_heap_free(heap, 0, ptr) != 0) {
      uv_rt_last_error_set(saved_error);
    }
  }
}

static __inline void* uv_memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

static __inline void* uv_memmove(void* dst, const void* src, size_t n) {
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

static __inline void* uv_memset(void* dst, int c, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char v = (unsigned char)c;
  for (size_t i = 0; i < n; ++i) {
    d[i] = v;
  }
  return dst;
}

static __inline size_t uv_wcslen(const wchar_t* s) {
  size_t n = 0;
  if (!s) {
    return 0;
  }
  while (s[n] != 0) {
    ++n;
  }
  return n;
}

static __inline uint64_t uv_cstr_len(const char* s) {
  uint64_t n = 0;
  if (!s) {
    return 0;
  }
  while (s[n] != 0) {
    ++n;
  }
  return n;
}

static __inline void uv_trace_emit_rule_with_meta(const char* rule_id,
                                                  const char* category,
                                                  const char* level) {
  if (!rule_id) {
    return;
  }
  UVStringView rule_view;
  rule_view.data = (const uint8_t*)rule_id;
  rule_view.len = uv_cstr_len(rule_id);

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

  UVStringView payload;
  payload.data = (const uint8_t*)payload_buf;
  payload.len = payload_len;
  ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
      &rule_view,
      NULL,
      0u,
      0u,
      0u,
      0u,
      &payload);
}

static __inline void uv_trace_emit_rule(const char* rule_id) {
  uv_trace_emit_rule_with_meta(rule_id, "runtime", "trace");
}

static __inline int uv_utf8_has_null(const uint8_t* data, uint64_t len) {
  for (uint64_t i = 0; i < len; ++i) {
    if (data[i] == 0) {
      return 1;
    }
  }
  return 0;
}

static __inline int uv_is_abs_path(const wchar_t* path, uint32_t len) {
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

static __inline int uv_heap_can_alloc(UVHeapState* heap,
                                      uint64_t size,
                                      int* out_quota_exceeded) {
  if (out_quota_exceeded) {
    *out_quota_exceeded = 0;
  }
  if (!heap || size == 0) {
    return 1;
  }
  uint32_t guard = 0;
  for (UVHeapState* cur = heap; cur; cur = cur->parent) {
    if (uv_rt_heap_validate(uv_process_heap(), 0, cur) == 0) {
      uv_trace_emit_rule("Log-HeapCanAlloc-InvalidState");
      return 0;
    }
    ++guard;
    if (guard > 1024u) {
      uv_trace_emit_rule("Log-HeapCanAlloc-ParentCycle");
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

static __inline void uv_heap_add_used(UVHeapState* heap, uint64_t size) {
  if (!heap || size == 0) {
    return;
  }
  for (UVHeapState* cur = heap; cur; cur = cur->parent) {
    cur->used += size;
  }
}

static __inline void uv_heap_sub_used(UVHeapState* heap, uint64_t size) {
  if (!heap || size == 0) {
    return;
  }
  uint32_t guard = 0;
  for (UVHeapState* cur = heap; cur; cur = cur->parent) {
    if (cur->used >= size) {
      cur->used -= size;
    } else {
      cur->used = 0;
    }
    ++guard;
    if (guard > 1024u) {
      uv_trace_emit_rule("HeapSubUsed-ParentCycle");
      break;
    }
  }
}

// -----------------------------------------------------------------------------
// Allocation helpers for managed string/bytes
// -----------------------------------------------------------------------------

static __inline UVAllocHeader* uv_header_from_data(uint8_t* data) {
  return data ? ((UVAllocHeader*)data) - 1 : NULL;
}

static __inline UVRawAllocHeader* uv_raw_header_from_data(void* data) {
  return data ? ((UVRawAllocHeader*)data) - 1 : NULL;
}

static __inline UVHeapState* uv_recorded_owner_heap(void* data) {
  UVRawAllocHeader* header = uv_raw_header_from_data(data);
  return header ? header->heap : NULL;
}

static __inline uint64_t uv_recorded_alloc_size(void* data) {
  UVRawAllocHeader* header = uv_raw_header_from_data(data);
  return header ? header->size : 0;
}

static __inline void uv_heap_dealloc_recorded(UVHeapState* heap,
                                              void** ptr,
                                              uint64_t count) {
  (void)heap;
  if (!ptr || !*ptr) {
    return;
  }

  UVRawAllocHeader* header = uv_raw_header_from_data(*ptr);
  if (!header) {
    return;
  }
  if (uv_rt_heap_validate(uv_process_heap(), 0, header) == 0) {
    uv_trace_emit_rule("Log-HeapRawDealloc-InvalidPointer");
    return;
  }
  if (count != header->size) {
    uv_trace_emit_rule("Log-HeapRawDealloc-CountMismatch");
  }

  uv_heap_sub_used(header->heap, header->size);
  uv_heap_free_raw(header);
  *ptr = NULL;
}

static __inline void uv_drop_managed_bytes(uint8_t** data, uint64_t count) {
  if (!data || !*data) {
    return;
  }

  uv_trace_emit_rule("ManagedFree-Enter");
  UVHeapState* owner = uv_recorded_owner_heap(*data);
  uv_heap_dealloc_recorded(owner, (void**)data, count);
  uv_trace_emit_rule("ManagedFree-Exit");
}

static __inline uint8_t* uv_alloc_managed_bytes(
    UVHeapState* heap,
    uint64_t cap,
    int* out_quota_exceeded) {
  if (cap == 0) {
    return NULL;
  }
  if (!uv_heap_can_alloc(heap, cap, out_quota_exceeded)) {
    return NULL;
  }
  const uint64_t total = cap + (uint64_t)sizeof(UVAllocHeader);
  if (total < cap || total > (uint64_t)SIZE_MAX) {
    return NULL;
  }
  UVAllocHeader* header = (UVAllocHeader*)uv_heap_alloc_raw((size_t)total);
  if (!header) {
    return NULL;
  }
  header->heap = heap;
  header->cap = cap;
  uv_heap_add_used(heap, cap);
  return (uint8_t*)(header + 1);
}

static __inline void uv_free_managed_bytes(uint8_t* data) {
  if (!data) {
    return;
  }
  uint8_t* local = data;
  uv_drop_managed_bytes(&local, uv_recorded_alloc_size(data));
}

// -----------------------------------------------------------------------------
// UTF-8 helpers
// -----------------------------------------------------------------------------

static __inline int uv_utf8_valid(const uint8_t* data, uint64_t len) {
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
static __inline wchar_t* uv_utf8_to_wide(
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
  if (uv_utf8_has_null(data, len)) {
    return NULL;
  }
  int wide_len = uv_rt_utf8_to_wide_chars((const char*)data,
                                               (int)len,
                                               NULL,
                                               0);
  if (wide_len <= 0) {
    return NULL;
  }
  const size_t bytes = (size_t)(wide_len + 1) * sizeof(wchar_t);
  wchar_t* out = (wchar_t*)uv_heap_alloc_raw(bytes);
  if (!out) {
    return NULL;
  }
  int written = uv_rt_utf8_to_wide_chars((const char*)data,
                                              (int)len,
                                              out,
                                              wide_len);
  if (written != wide_len) {
    uv_heap_free_raw(out);
    return NULL;
  }
  out[wide_len] = 0;
  if (out_len) {
    *out_len = (uint32_t)wide_len;
  }
  return out;
}

// Convert wide string to UTF-8 (allocates with process heap)
static __inline uint8_t* uv_wide_to_utf8(
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
  int byte_len = uv_rt_wide_to_utf8_chars(data,
                                               (int)len,
                                               NULL,
                                               0);
  if (byte_len <= 0) {
    return NULL;
  }
  uint8_t* out = (uint8_t*)uv_heap_alloc_raw((size_t)byte_len);
  if (!out) {
    return NULL;
  }
  int written = uv_rt_wide_to_utf8_chars(data,
                                              (int)len,
                                              (char*)out,
                                              byte_len);
  if (written != byte_len) {
    uv_heap_free_raw(out);
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
int uv_parallel_in_panic_scope(void);
void uv_parallel_raise_panic(uint32_t code);

// -----------------------------------------------------------------------------
// Size checks (Win64 expectations)
// -----------------------------------------------------------------------------

_Static_assert(sizeof(void*) == 8, "runtime expects 64-bit pointers");
_Static_assert(sizeof(UVStringView) == 16, "string@View layout");
_Static_assert(sizeof(UVStringManaged) == 24, "string@Managed layout");
_Static_assert(sizeof(UVBytesView) == 16, "bytes@View layout");
_Static_assert(sizeof(UVBytesManaged) == 24, "bytes@Managed layout");
_Static_assert(sizeof(UVDynObject) == 16, "dynamic object layout");
_Static_assert(sizeof(UVContext) == 80, "Context layout");
_Static_assert(sizeof(UVExecutionDomain) == 24, "ExecutionDomain layout");
_Static_assert(sizeof(UVDuration) == 16, "Duration layout");
_Static_assert(sizeof(UVMonotonicInstant) == 32, "MonotonicInstant layout");
_Static_assert(sizeof(UVUtcInstant) == 16, "UtcInstant layout");
_Static_assert(sizeof(UVUnion_Duration_TimeError) == 32,
               "Outcome<Duration, TimeError> layout");
_Static_assert(sizeof(UVUnion_DynObject_TimeError) == 24,
               "Outcome<dyn, TimeError> layout");
_Static_assert(sizeof(UVUnion_UtcInstant_TimeError) == 32,
               "Outcome<UtcInstant, TimeError> layout");
_Static_assert(sizeof(UVStringModal) == 32, "string modal layout");
_Static_assert(sizeof(UVRegionOptions) == 40, "RegionOptions layout");
_Static_assert(sizeof(UVRange) == 24, "Range layout");
_Static_assert(sizeof(UVAllocationError) == 16, "AllocationError layout");
_Static_assert(sizeof(UVDirEntry) == 56, "DirEntry layout");
_Static_assert(sizeof(UVUnion_StringManaged_AllocError) == 32, "string|AllocError layout");
_Static_assert(sizeof(UVUnion_BytesManaged_AllocError) == 32, "bytes|AllocError layout");
_Static_assert(sizeof(UVUnion_Unit_AllocError) == 24, "unit|AllocError layout");
_Static_assert(sizeof(UVUnion_StringManaged_IoError) == 32, "string|IoError layout");
_Static_assert(sizeof(UVUnion_BytesManaged_IoError) == 32, "bytes|IoError layout");
_Static_assert(sizeof(UVUnion_File_IoError) == 16, "file|IoError layout");
_Static_assert(sizeof(UVUnion_DirIter_IoError) == 16, "diriter|IoError layout");
_Static_assert(sizeof(UVUnion_Unit_IoError) == 2, "unit|IoError layout");
_Static_assert(sizeof(UVUnion_FileKind_IoError) == 2, "filekind|IoError layout");
_Static_assert(sizeof(UVUnion_DirEntry_Unit_IoError) == 72, "Outcome<DirEntry|(), IoError> layout");

#endif  // UV_RT_INTERNAL_H

