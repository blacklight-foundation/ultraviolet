#include "../internal/rt_internal.h"

static UVHeapState* uv_heap_state(const UVDynObject* heap) {
  if (!heap) {
    return NULL;
  }
  return (UVHeapState*)heap->data;
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3aheap_x3a_x3awith_x5fquota(
    UVDynObject self,
    uint64_t size) {
  uv_trace_emit_rule("BuiltinSym-HeapAllocator-WithQuota");
  UVDynObject out;
  out.data = NULL;
  out.vtable = self.vtable;

  UVHeapState* parent = uv_heap_state(&self);
  UVHeapState* heap = (UVHeapState*)uv_heap_alloc_raw(sizeof(UVHeapState));
  if (!heap) {
    return out;
  }
  heap->parent = parent;
  heap->quota = size;
  heap->used = 0;

  out.data = heap;
  return out;
}

void* ultraviolet_x3a_x3aruntime_x3a_x3aheap_x3a_x3aalloc_x5fraw(
    UVDynObject self,
    uint64_t count) {
  uv_trace_emit_rule("BuiltinSym-HeapAllocator-AllocRaw");
  const uint64_t size = count;
  if (size == 0) {
    return NULL;
  }
  if (size > (uint64_t)SIZE_MAX) {
    return NULL;
  }

  UVHeapState* heap = uv_heap_state(&self);
  int quota_exceeded = 0;
  if (!uv_heap_can_alloc(heap, size, &quota_exceeded)) {
    return NULL;
  }

  const uint64_t total = size + (uint64_t)sizeof(UVRawAllocHeader);
  if (total < size || total > (uint64_t)SIZE_MAX) {
    return NULL;
  }
  UVRawAllocHeader* header = (UVRawAllocHeader*)uv_heap_alloc_raw((size_t)total);
  if (!header) {
    return NULL;
  }
  header->heap = heap;
  header->size = size;

  uv_heap_add_used(heap, size);
  return (void*)(header + 1);
}

void ultraviolet_x3a_x3aruntime_x3a_x3aheap_x3a_x3adealloc_x5fraw(
    UVDynObject self,
    void* ptr,
    uint64_t count) {
  uv_trace_emit_rule("BuiltinSym-HeapAllocator-DeallocRaw");
  void* raw = ptr;
  uv_heap_dealloc_recorded(uv_heap_state(&self), &raw, count);
}
