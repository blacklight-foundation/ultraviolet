#include "../internal/rt_internal.h"

static UVHeapState* uv_heap_from_dyn(const UVDynObject* heap) {
  if (!heap) {
    return NULL;
  }
  return (UVHeapState*)heap->data;
}

static void uv_set_alloc_error(UVAllocationError* err,
                               uint64_t size,
                               int quota_exceeded) {
  if (!err) {
    return;
  }
  err->disc = (uint8_t)(quota_exceeded ? UV_ALLOC_QUOTA_EXCEEDED
                                       : UV_ALLOC_OUT_OF_MEMORY);
  err->size = size;
}

static void uv_string_alloc_err(UVUnion_StringManaged_AllocError* out,
                                uint64_t size,
                                int quota_exceeded) {
  if (!out) {
    return;
  }
  out->disc = 1;
  uv_set_alloc_error(&out->payload.alloc_error, size, quota_exceeded);
}

static void uv_bytes_alloc_err(UVUnion_BytesManaged_AllocError* out,
                               uint64_t size,
                               int quota_exceeded) {
  if (!out) {
    return;
  }
  out->disc = 1;
  uv_set_alloc_error(&out->payload.alloc_error, size, quota_exceeded);
}

static void uv_unit_alloc_err(UVUnion_Unit_AllocError* out,
                              uint64_t size,
                              int quota_exceeded) {
  if (!out) {
    return;
  }
  out->disc = 1;
  uv_set_alloc_error(&out->error, size, quota_exceeded);
}

static uint8_t* uv_realloc_managed_bytes(UVHeapState* heap,
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
    return uv_alloc_managed_bytes(heap, new_cap, out_quota_exceeded);
  }
  if (new_cap <= old_cap) {
    return old_data;
  }

  uint64_t extra = new_cap - old_cap;
  if (!uv_heap_can_alloc(heap, extra, out_quota_exceeded)) {
    return NULL;
  }

  uint64_t total = new_cap + (uint64_t)sizeof(UVAllocHeader);
  if (total < new_cap || total > (uint64_t)SIZE_MAX) {
    return NULL;
  }

  UVAllocHeader* header = (UVAllocHeader*)uv_heap_alloc_raw((size_t)total);
  if (!header) {
    return NULL;
  }
  header->heap = heap;
  header->cap = new_cap;
  uv_heap_add_used(heap, extra);

  uint8_t* data = (uint8_t*)(header + 1);
  if (old_len > 0) {
    uv_memcpy(data, old_data, (size_t)old_len);
  }

  UVAllocHeader* old_header = uv_header_from_data(old_data);
  uv_heap_free_raw(old_header);
  return data;
}

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3adrop_x5fmanaged(
    UVStringManaged* value) {
  if (!value) {
    return;
  }
  uv_drop_managed_bytes(&value->data, value->cap);
  value->len = 0;
  value->cap = 0;
}

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3adrop_x5fmanaged(
    UVBytesManaged* value) {
  if (!value) {
    return;
  }
  uv_drop_managed_bytes(&value->data, value->cap);
  value->len = 0;
  value->cap = 0;
}

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3afrom(
    UVUnion_StringManaged_AllocError* out,
    UVStringView source,
    UVDynObject heap) {
  uv_trace_emit_rule("Log-StringFrom-Enter");
  if (!out) {
    uv_trace_emit_rule("Log-StringFrom-NoOut");
    return;
  }
  uint64_t len = source.len;
  if (len == 0) {
    uv_trace_emit_rule("Log-StringFrom-ZeroLen");
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  UVHeapState* heap_state = uv_heap_from_dyn(&heap);
  uv_trace_emit_rule("Log-StringFrom-BeforeAlloc");
  int quota_exceeded = 0;
  uint8_t* data = uv_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!data) {
    uv_trace_emit_rule("Log-StringFrom-AllocErr");
    uv_string_alloc_err(out, len, quota_exceeded);
    return;
  }
  uv_trace_emit_rule("Log-StringFrom-BeforeMemcpy");
  uv_trace_emit_rule("Log-StringFrom-SourceValue");
  if (len == 2) {
    uv_trace_emit_rule("Log-StringFrom-Len-2");
  } else {
    uv_trace_emit_rule("Log-StringFrom-Len-Not2");
  }
  if (len <= 1024) {
    uv_trace_emit_rule("Log-StringFrom-Len-Le1024");
  } else {
    uv_trace_emit_rule("Log-StringFrom-Len-Gt1024");
  }
  if (source.data == NULL) {
    uv_trace_emit_rule("Log-StringFrom-SourceData-Null");
  } else {
    uv_trace_emit_rule("Log-StringFrom-SourceData-NonNull");
  }
  if (source.data) {
    const uintptr_t src_addr = (uintptr_t)source.data;
    if (src_addr < 0x10000u) {
      uv_trace_emit_rule("Log-StringFrom-SourceData-LowAddr");
    } else {
      uv_trace_emit_rule("Log-StringFrom-SourceData-NotLowAddr");
    }
  }
  uv_trace_emit_rule("Log-StringFrom-BeforeReadSrc");
  volatile uint8_t src_probe = source.data[0];
  (void)src_probe;
  uv_trace_emit_rule("Log-StringFrom-AfterReadSrc");
  uv_trace_emit_rule("Log-StringFrom-BeforeWriteDst");
  data[0] = data[0];
  uv_trace_emit_rule("Log-StringFrom-AfterWriteDst");
  uv_memcpy(data, source.data, (size_t)len);
  uv_trace_emit_rule("Log-StringFrom-AfterMemcpy");
  out->disc = 0;
  out->payload.value.data = data;
  out->payload.value.len = len;
  out->payload.value.cap = len;
  uv_trace_emit_rule("Log-StringFrom-ReturnOk");
}

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aas_x5fview(
    const UVStringManaged* self) {
  UVStringView view;
  view.data = self ? self->data : NULL;
  view.len = self ? self->len : 0;
  return view;
}

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aslice(
    UVStringView self,
    uint64_t start,
    uint64_t end) {
  UVStringView view;
  view.data = NULL;
  view.len = 0;

  if (start > end || end > self.len) {
    return view;
  }

  view.data = self.data ? self.data + start : NULL;
  view.len = end - start;
  return view;
}

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3ato_x5fmanaged(
    UVUnion_StringManaged_AllocError* out,
    UVStringView self,
    UVDynObject heap) {
  ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3afrom(out, self, heap);
}

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aclone_x5fwith(
    UVUnion_StringManaged_AllocError* out,
    const UVStringManaged* self,
    const UVDynObject* heap) {
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

  UVHeapState* heap_state = uv_heap_from_dyn(heap);
  int quota_exceeded = 0;
  uint8_t* data = uv_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!data) {
    uv_string_alloc_err(out, len, quota_exceeded);
    return;
  }
  uv_memcpy(data, self->data, (size_t)len);
  out->disc = 0;
  out->payload.value.data = data;
  out->payload.value.len = len;
  out->payload.value.cap = len;
}

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aappend(
    UVUnion_Unit_AllocError* out,
    UVStringManaged* self,
    UVStringView data,
    UVDynObject heap) {
  uv_trace_emit_rule("Debug-String-Append-Enter");
  if (!out) {
    uv_trace_emit_rule("Debug-String-Append-Return-NoOut");
    return;
  }
  if (!self) {
    uv_unit_alloc_err(out, 0, 0);
    uv_trace_emit_rule("Debug-String-Append-Return-NullSelf");
    return;
  }
  const uint64_t add_len = data.len;
  if (add_len == 0) {
    out->disc = 0;
    out->error.disc = 0;
    out->error.size = 0;
    uv_trace_emit_rule("Debug-String-Append-Return-Empty");
    return;
  }

  if (self->len > UINT64_MAX - add_len) {
    uv_unit_alloc_err(out, add_len, 0);
    uv_trace_emit_rule("Debug-String-Append-Return-Overflow");
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
    UVHeapState* heap_state = uv_heap_from_dyn(&heap);
    int quota_exceeded = 0;
    uint8_t* new_data = uv_realloc_managed_bytes(heap_state,
                                                 self->data,
                                                 self->len,
                                                 self->cap,
                                                 new_cap,
                                                 &quota_exceeded);
    if (!new_data) {
      uv_unit_alloc_err(out, new_cap, quota_exceeded);
      uv_trace_emit_rule("Debug-String-Append-Return-AllocError");
      return;
    }
    self->data = new_data;
    self->cap = new_cap;
  }

  uv_memcpy(self->data + self->len, data.data, (size_t)add_len);
  self->len = new_len;

  out->disc = 0;
  out->error.disc = 0;
  out->error.size = 0;
  uv_trace_emit_rule("Debug-String-Append-Return-Ok");
}

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3alength(
    UVStringView self) {
  return self.len;
}

uint8_t ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3ais_x5fempty(
    UVStringView self) {
  return self.len == 0 ? 1 : 0;
}

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3awith_x5fcapacity(
    UVUnion_BytesManaged_AllocError* out,
    uint64_t cap,
    UVDynObject heap) {
  if (!out) {
    return;
  }
  uint64_t capacity = cap;
  if (capacity == 0) {
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  UVHeapState* heap_state = uv_heap_from_dyn(&heap);
  int quota_exceeded = 0;
  uint8_t* data = uv_alloc_managed_bytes(heap_state, capacity, &quota_exceeded);
  if (!data) {
    uv_bytes_alloc_err(out, capacity, quota_exceeded);
    return;
  }
  out->disc = 0;
  out->payload.value.data = data;
  out->payload.value.len = 0;
  out->payload.value.cap = capacity;
}

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3afrom_x5fslice(
    UVUnion_BytesManaged_AllocError* out,
    UVSliceU8 data,
    UVDynObject heap) {
  if (!out) {
    return;
  }
  uint64_t len = data.len;
  if (len == 0) {
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  UVHeapState* heap_state = uv_heap_from_dyn(&heap);
  int quota_exceeded = 0;
  uint8_t* buf = uv_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!buf) {
    uv_bytes_alloc_err(out, len, quota_exceeded);
    return;
  }
  uv_memcpy(buf, data.data, (size_t)len);
  out->disc = 0;
  out->payload.value.data = buf;
  out->payload.value.len = len;
  out->payload.value.cap = len;
}

UVBytesView ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fview(
    const UVBytesManaged* self) {
  UVBytesView view;
  view.data = self ? self->data : NULL;
  view.len = self ? self->len : 0;
  return view;
}

UVSliceU8 ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fslice(
    UVBytesView self) {
  UVSliceU8 slice;
  slice.data = self.data;
  slice.len = self.len;
  return slice;
}

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3ato_x5fmanaged(
    UVUnion_BytesManaged_AllocError* out,
    UVBytesView self,
    UVDynObject heap) {
  if (!out) {
    return;
  }
  uint64_t len = self.len;
  if (len == 0) {
    out->disc = 0;
    out->payload.value.data = NULL;
    out->payload.value.len = 0;
    out->payload.value.cap = 0;
    return;
  }

  UVHeapState* heap_state = uv_heap_from_dyn(&heap);
  int quota_exceeded = 0;
  uint8_t* buf = uv_alloc_managed_bytes(heap_state, len, &quota_exceeded);
  if (!buf) {
    uv_bytes_alloc_err(out, len, quota_exceeded);
    return;
  }
  uv_memcpy(buf, self.data, (size_t)len);
  out->disc = 0;
  out->payload.value.data = buf;
  out->payload.value.len = len;
  out->payload.value.cap = len;
}

UVBytesView ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview(
    UVSliceU8 data) {
  UVBytesView view;
  view.data = data.data;
  view.len = data.len;
  return view;
}

UVBytesView ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview_x5fstring(
    UVStringView data) {
  UVBytesView view;
  view.data = data.data;
  view.len = data.len;
  return view;
}

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aappend(
    UVUnion_Unit_AllocError* out,
    UVBytesManaged* self,
    UVBytesView data,
    UVDynObject heap) {
  if (!out) {
    return;
  }
  if (!self) {
    uv_unit_alloc_err(out, 0, 0);
    return;
  }
  const uint64_t add_len = data.len;
  if (add_len == 0) {
    out->disc = 0;
    out->error.disc = 0;
    out->error.size = 0;
    return;
  }

  if (self->len > UINT64_MAX - add_len) {
    uv_unit_alloc_err(out, add_len, 0);
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
    UVHeapState* heap_state = uv_heap_from_dyn(&heap);
    int quota_exceeded = 0;
    uint8_t* new_data = uv_realloc_managed_bytes(heap_state,
                                                 self->data,
                                                 self->len,
                                                 self->cap,
                                                 new_cap,
                                                 &quota_exceeded);
    if (!new_data) {
      uv_unit_alloc_err(out, new_cap, quota_exceeded);
      return;
    }
    self->data = new_data;
    self->cap = new_cap;
  }

  uv_memcpy(self->data + self->len, data.data, (size_t)add_len);
  self->len = new_len;

  out->disc = 0;
  out->error.disc = 0;
  out->error.size = 0;
}

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3alength(
    UVBytesView self) {
  return self.len;
}

uint8_t ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3ais_x5fempty(
    UVBytesView self) {
  return self.len == 0 ? 1 : 0;
}
