#include "../internal/rt_internal.h"

static C0HeapState* c0_heap_from_dyn(const C0DynObject* heap) {
  if (!heap) {
    return NULL;
  }
  return (C0HeapState*)heap->data;
}

static void c0_set_alloc_error(C0AllocationError* err,
                               uint64_t size,
                               int quota_exceeded) {
  if (!err) {
    return;
  }
  err->disc = (uint8_t)(quota_exceeded ? C0_ALLOC_QUOTA_EXCEEDED
                                       : C0_ALLOC_OUT_OF_MEMORY);
  err->size = size;
}

static void c0_string_alloc_err(C0Union_StringManaged_AllocError* out,
                                uint64_t size,
                                int quota_exceeded) {
  if (!out) {
    return;
  }
  out->disc = 1;
  c0_set_alloc_error(&out->payload.alloc_error, size, quota_exceeded);
}

static void c0_bytes_alloc_err(C0Union_BytesManaged_AllocError* out,
                               uint64_t size,
                               int quota_exceeded) {
  if (!out) {
    return;
  }
  out->disc = 1;
  c0_set_alloc_error(&out->payload.alloc_error, size, quota_exceeded);
}

static void c0_unit_alloc_err(C0Union_Unit_AllocError* out,
                              uint64_t size,
                              int quota_exceeded) {
  if (!out) {
    return;
  }
  out->disc = 1;
  c0_set_alloc_error(&out->error, size, quota_exceeded);
}

static uint8_t* c0_realloc_managed_bytes(C0HeapState* heap,
                                         uint8_t* old_data,
                                         uint64_t old_len,
                                         uint64_t old_cap,
                                         uint64_t new_cap,
                                         int* out_quota_exceeded) {
  if (out_quota_exceeded) {
    *out_quota_exceeded = 0;
  }
  if (new_cap == 0) {
    return NULL;
  }
  if (!old_data || old_cap == 0) {
    return c0_alloc_managed_bytes(heap, new_cap, out_quota_exceeded);
  }
  if (new_cap <= old_cap) {
    return old_data;
  }

  uint64_t extra = new_cap - old_cap;
  if (!c0_heap_can_alloc(heap, extra, out_quota_exceeded)) {
    return NULL;
  }

  uint64_t total = new_cap + (uint64_t)sizeof(C0AllocHeader);
  if (total < new_cap || total > (uint64_t)SIZE_MAX) {
    return NULL;
  }

  C0AllocHeader* header = (C0AllocHeader*)c0_heap_alloc_raw((size_t)total);
  if (!header) {
    return NULL;
  }
  header->heap = heap;
  header->cap = new_cap;
  c0_heap_add_used(heap, extra);

  uint8_t* data = (uint8_t*)(header + 1);
  if (old_len > 0) {
    c0_memcpy(data, old_data, (size_t)old_len);
  }

  C0AllocHeader* old_header = c0_header_from_data(old_data);
  c0_heap_free_raw(old_header);
  return data;
}

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3adrop_x5fmanaged(
    C0StringManaged* value) {
  if (!value) {
    return;
  }
  c0_drop_managed_bytes(&value->data, value->cap);
  value->len = 0;
  value->cap = 0;
}

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3adrop_x5fmanaged(
    C0BytesManaged* value) {
  if (!value) {
    return;
  }
  c0_drop_managed_bytes(&value->data, value->cap);
  value->len = 0;
  value->cap = 0;
}

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3afrom(
    C0Union_StringManaged_AllocError* out,
    const C0StringView* source,
    const C0DynObject* heap) {
  c0_trace_emit_rule("Log-StringFrom-Enter");
  if (!out) {
    c0_trace_emit_rule("Log-StringFrom-NoOut");
    return;
  }
  uint64_t len = source ? source->len : 0;
  if (len == 0) {
    c0_trace_emit_rule("Log-StringFrom-ZeroLen");
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  C0HeapState* heap_state = c0_heap_from_dyn(heap);
  c0_trace_emit_rule("Log-StringFrom-BeforeAlloc");
  int quota_exceeded = 0;
  uint8_t* data = c0_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!data) {
    c0_trace_emit_rule("Log-StringFrom-AllocErr");
    c0_string_alloc_err(out, len, quota_exceeded);
    return;
  }
  c0_trace_emit_rule("Log-StringFrom-BeforeMemcpy");
  if (source == NULL) {
    c0_trace_emit_rule("Log-StringFrom-SourcePtr-Null");
  } else {
    c0_trace_emit_rule("Log-StringFrom-SourcePtr-NonNull");
    if (((uintptr_t)source) < 0x10000u) {
      c0_trace_emit_rule("Log-StringFrom-SourcePtr-LowAddr");
    } else {
      c0_trace_emit_rule("Log-StringFrom-SourcePtr-NotLowAddr");
    }
  }
  if (len == 2) {
    c0_trace_emit_rule("Log-StringFrom-Len-2");
  } else {
    c0_trace_emit_rule("Log-StringFrom-Len-Not2");
  }
  if (len <= 1024) {
    c0_trace_emit_rule("Log-StringFrom-Len-Le1024");
  } else {
    c0_trace_emit_rule("Log-StringFrom-Len-Gt1024");
  }
  if (source && source->data == NULL) {
    c0_trace_emit_rule("Log-StringFrom-SourceData-Null");
  } else {
    c0_trace_emit_rule("Log-StringFrom-SourceData-NonNull");
  }
  if (source && source->data) {
    const uintptr_t src_addr = (uintptr_t)source->data;
    if (src_addr < 0x10000u) {
      c0_trace_emit_rule("Log-StringFrom-SourceData-LowAddr");
    } else {
      c0_trace_emit_rule("Log-StringFrom-SourceData-NotLowAddr");
    }
  }
  c0_trace_emit_rule("Log-StringFrom-BeforeReadSrc");
  volatile uint8_t src_probe = source->data[0];
  (void)src_probe;
  c0_trace_emit_rule("Log-StringFrom-AfterReadSrc");
  c0_trace_emit_rule("Log-StringFrom-BeforeWriteDst");
  data[0] = data[0];
  c0_trace_emit_rule("Log-StringFrom-AfterWriteDst");
  c0_memcpy(data, source->data, (size_t)len);
  c0_trace_emit_rule("Log-StringFrom-AfterMemcpy");
  out->disc = 0;
  out->payload.value.data = data;
  out->payload.value.len = len;
  out->payload.value.cap = len;
  c0_trace_emit_rule("Log-StringFrom-ReturnOk");
}

C0StringView cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aas_x5fview(
    const C0StringManaged* self) {
  C0StringView view;
  view.data = self ? self->data : NULL;
  view.len = self ? self->len : 0;
  return view;
}

C0StringView cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aslice(
    const C0StringView* self,
    const uint64_t* start,
    const uint64_t* end) {
  C0StringView view;
  view.data = NULL;
  view.len = 0;

  if (!self || !start || !end) {
    return view;
  }
  if (*start > *end || *end > self->len) {
    return view;
  }

  view.data = self->data ? self->data + *start : NULL;
  view.len = *end - *start;
  return view;
}

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3ato_x5fmanaged(
    C0Union_StringManaged_AllocError* out,
    const C0StringView* self,
    const C0DynObject* heap) {
  cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3afrom(out, self, heap);
}

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aclone_x5fwith(
    C0Union_StringManaged_AllocError* out,
    const C0StringManaged* self,
    const C0DynObject* heap) {
  if (!out) {
    return;
  }
  uint64_t len = self ? self->len : 0;
  if (len == 0) {
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  C0HeapState* heap_state = c0_heap_from_dyn(heap);
  int quota_exceeded = 0;
  uint8_t* data = c0_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!data) {
    c0_string_alloc_err(out, len, quota_exceeded);
    return;
  }
  c0_memcpy(data, self->data, (size_t)len);
  out->disc = 0;
  out->payload.value.data = data;
  out->payload.value.len = len;
  out->payload.value.cap = len;
}

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aappend(
    C0Union_Unit_AllocError* out,
    C0StringManaged* self,
    const C0StringView* data,
    const C0DynObject* heap) {
  c0_trace_emit_rule("Debug-String-Append-Enter");
  if (!out) {
    c0_trace_emit_rule("Debug-String-Append-Return-NoOut");
    return;
  }
  if (!self) {
    c0_unit_alloc_err(out, 0, 0);
    c0_trace_emit_rule("Debug-String-Append-Return-NullSelf");
    return;
  }
  const uint64_t add_len = data ? data->len : 0;
  if (add_len == 0) {
    out->disc = 0;
    out->error.disc = 0;
    out->error.size = 0;
    c0_trace_emit_rule("Debug-String-Append-Return-Empty");
    return;
  }

  if (self->len > UINT64_MAX - add_len) {
    c0_unit_alloc_err(out, add_len, 0);
    c0_trace_emit_rule("Debug-String-Append-Return-Overflow");
    return;
  }

  uint64_t new_len = self->len + add_len;
  uint64_t new_cap = self->cap;
  if (new_cap < new_len) {
    uint64_t doubled = new_cap == 0 ? new_len : new_cap * 2;
    if (doubled < new_cap) {
      doubled = new_len;
    }
    new_cap = doubled < new_len ? new_len : doubled;
  }

  if (self->data == NULL || self->cap < new_len) {
    C0HeapState* heap_state = c0_heap_from_dyn(heap);
    int quota_exceeded = 0;
    uint8_t* new_data = c0_realloc_managed_bytes(heap_state,
                                                 self->data,
                                                 self->len,
                                                 self->cap,
                                                 new_cap,
                                                 &quota_exceeded);
    if (!new_data) {
      c0_unit_alloc_err(out, new_cap, quota_exceeded);
      c0_trace_emit_rule("Debug-String-Append-Return-AllocError");
      return;
    }
    self->data = new_data;
    self->cap = new_cap;
  }

  c0_memcpy(self->data + self->len, data->data, (size_t)add_len);
  self->len = new_len;

  out->disc = 0;
  out->error.disc = 0;
  out->error.size = 0;
  c0_trace_emit_rule("Debug-String-Append-Return-Ok");
}

uint64_t cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3alength(
    const C0StringView* self) {
  return self ? self->len : 0;
}

uint8_t cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3ais_x5fempty(
    const C0StringView* self) {
  return (self && self->len == 0) ? 1 : 0;
}

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3awith_x5fcapacity(
    C0Union_BytesManaged_AllocError* out,
    const uint64_t* cap,
    const C0DynObject* heap) {
  if (!out) {
    return;
  }
  uint64_t capacity = cap ? *cap : 0;
  if (capacity == 0) {
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  C0HeapState* heap_state = c0_heap_from_dyn(heap);
  int quota_exceeded = 0;
  uint8_t* data = c0_alloc_managed_bytes(heap_state, capacity, &quota_exceeded);
  if (!data) {
    c0_bytes_alloc_err(out, capacity, quota_exceeded);
    return;
  }
  out->disc = 0;
  out->payload.value.data = data;
  out->payload.value.len = 0;
  out->payload.value.cap = capacity;
}

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3afrom_x5fslice(
    C0Union_BytesManaged_AllocError* out,
    const C0SliceU8* data,
    const C0DynObject* heap) {
  if (!out) {
    return;
  }
  uint64_t len = data ? data->len : 0;
  if (len == 0) {
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  C0HeapState* heap_state = c0_heap_from_dyn(heap);
  int quota_exceeded = 0;
  uint8_t* buf = c0_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!buf) {
    c0_bytes_alloc_err(out, len, quota_exceeded);
    return;
  }
  c0_memcpy(buf, data->data, (size_t)len);
  out->disc = 0;
  out->payload.value.data = buf;
  out->payload.value.len = len;
  out->payload.value.cap = len;
}

C0BytesView cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fview(
    const C0BytesManaged* self) {
  C0BytesView view;
  view.data = self ? self->data : NULL;
  view.len = self ? self->len : 0;
  return view;
}

C0SliceU8 cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fslice(
    const C0BytesView* self) {
  C0SliceU8 slice;
  slice.data = self ? self->data : NULL;
  slice.len = self ? self->len : 0;
  return slice;
}

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3ato_x5fmanaged(
    C0Union_BytesManaged_AllocError* out,
    const C0BytesView* self,
    const C0DynObject* heap) {
  if (!out) {
    return;
  }
  uint64_t len = self ? self->len : 0;
  if (len == 0) {
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  C0HeapState* heap_state = c0_heap_from_dyn(heap);
  int quota_exceeded = 0;
  uint8_t* buf = c0_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!buf) {
    c0_bytes_alloc_err(out, len, quota_exceeded);
    return;
  }
  c0_memcpy(buf, self->data, (size_t)len);
  out->disc = 0;
  out->payload.value.data = buf;
  out->payload.value.len = len;
  out->payload.value.cap = len;
}

C0BytesView cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview(
    const C0SliceU8* data) {
  C0BytesView view;
  view.data = data ? data->data : NULL;
  view.len = data ? data->len : 0;
  return view;
}

C0BytesView cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview_x5fstring(
    const C0StringView* data) {
  C0BytesView view;
  view.data = data ? data->data : NULL;
  view.len = data ? data->len : 0;
  return view;
}

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aappend(
    C0Union_Unit_AllocError* out,
    C0BytesManaged* self,
    const C0BytesView* data,
    const C0DynObject* heap) {
  if (!out) {
    return;
  }
  if (!self) {
    c0_unit_alloc_err(out, 0, 0);
    return;
  }
  const uint64_t add_len = data ? data->len : 0;
  if (add_len == 0) {
    out->disc = 0;
    out->error.disc = 0;
    out->error.size = 0;
    return;
  }

  if (self->len > UINT64_MAX - add_len) {
    c0_unit_alloc_err(out, add_len, 0);
    return;
  }

  uint64_t new_len = self->len + add_len;
  uint64_t new_cap = self->cap;
  if (new_cap < new_len) {
    uint64_t doubled = new_cap == 0 ? new_len : new_cap * 2;
    if (doubled < new_cap) {
      doubled = new_len;
    }
    new_cap = doubled < new_len ? new_len : doubled;
  }

  if (self->data == NULL || self->cap < new_len) {
    C0HeapState* heap_state = c0_heap_from_dyn(heap);
    int quota_exceeded = 0;
    uint8_t* new_data = c0_realloc_managed_bytes(heap_state,
                                                 self->data,
                                                 self->len,
                                                 self->cap,
                                                 new_cap,
                                                 &quota_exceeded);
    if (!new_data) {
      c0_unit_alloc_err(out, new_cap, quota_exceeded);
      return;
    }
    self->data = new_data;
    self->cap = new_cap;
  }

  c0_memcpy(self->data + self->len, data->data, (size_t)add_len);
  self->len = new_len;

  out->disc = 0;
  out->error.disc = 0;
  out->error.size = 0;
}

uint64_t cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3alength(
    const C0BytesView* self) {
  return self ? self->len : 0;
}

uint8_t cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3ais_x5fempty(
    const C0BytesView* self) {
  return (self && self->len == 0) ? 1 : 0;
}
