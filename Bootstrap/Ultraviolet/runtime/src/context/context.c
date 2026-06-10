#include "../internal/rt_internal.h"

void ultraviolet_x3a_x3aruntime_x3a_x3acontext_x5finit(UVContext* out) {
  if (!out) {
    return;
  }

  out->io.data = NULL;
  out->io.vtable = NULL;
  out->net.data = NULL;
  out->net.vtable = NULL;
  out->heap.data = NULL;
  out->heap.vtable = NULL;
  out->sys.data = NULL;
  out->sys.vtable = NULL;
  out->reactor.data = NULL;
  out->reactor.vtable = NULL;
  out->time.data = NULL;
  out->time.vtable = NULL;

  UVFsState* io = (UVFsState*)uv_heap_alloc_raw(sizeof(UVFsState));
  if (!io) {
    return;
  }
  io->base_utf8 = NULL;
  io->base_len = 0;
  io->restricted = 0;
  io->valid = 1;

  UVNetState* net = (UVNetState*)uv_heap_alloc_raw(sizeof(UVNetState));
  if (!net) {
    uv_heap_free_raw(io);
    return;
  }
  net->parent = NULL;
  net->host_utf8 = NULL;
  net->host_len = 0;
  net->restricted = 0;
  net->valid = 1;

  UVHeapState* heap = (UVHeapState*)uv_heap_alloc_raw(sizeof(UVHeapState));
  if (!heap) {
    uv_heap_free_raw(net);
    uv_heap_free_raw(io);
    return;
  }
  heap->parent = NULL;
  heap->quota = 0;
  heap->used = 0;

  UVSystemState* sys = (UVSystemState*)uv_heap_alloc_raw(sizeof(UVSystemState));
  if (!sys) {
    uv_heap_free_raw(heap);
    uv_heap_free_raw(net);
    uv_heap_free_raw(io);
    return;
  }
  sys->valid = 1;

  UVTimeState* time = (UVTimeState*)uv_heap_alloc_raw(sizeof(UVTimeState));
  if (!time) {
    uv_heap_free_raw(sys);
    uv_heap_free_raw(heap);
    uv_heap_free_raw(net);
    uv_heap_free_raw(io);
    return;
  }
  time->parent = NULL;
  time->kind = UV_TIME_STATE_ROOT;
  time->domain = 1;
  time->resolution.lo = 1;
  time->resolution.hi = 0;

  out->io.data = io;
  out->io.vtable = NULL;
  out->net.data = net;
  out->net.vtable = NULL;
  out->heap.data = heap;
  out->heap.vtable = NULL;
  out->sys.data = sys;
  out->sys.vtable = NULL;
  out->time.data = time;
  out->time.vtable = NULL;
}

static UVExecutionDomain g_cpu_domain = {UV_DOMAIN_CPU, {0}, 1, 4, 0};
static UVExecutionDomain g_gpu_domain = {UV_DOMAIN_GPU, {0}, 1, 1, 0};
static UVExecutionDomain g_inline_domain = {UV_DOMAIN_INLINE, {0}, 1, 1, 0};

static int32_t uv_context_priority_rank(int32_t priority_hint) {
  if (priority_hint <= 0) {
    return 0;
  }
  if (priority_hint == 1) {
    return 1;
  }
  return 2;
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu(
    const UVContext* self) {
  (void)self;
  UVDynObject out;
  out.data = &g_cpu_domain;
  out.vtable = NULL;
  return out;
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu_x5fconfigured(
    const UVContext* self,
    const uint64_t* affinity_mask,
    const int32_t* priority_hint) {
  (void)self;
  const uint64_t affinity_mask_value = affinity_mask ? *affinity_mask : 0;
  const int32_t priority_hint_value = priority_hint ? *priority_hint : 0;
  const int32_t priority_rank = uv_context_priority_rank(priority_hint_value);
  if (affinity_mask_value == 0 && priority_rank == 1) {
    return ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu(self);
  }

  UVExecutionDomain* domain =
      (UVExecutionDomain*)uv_heap_alloc_raw(sizeof(UVExecutionDomain));
  UVDynObject out;
  out.data = NULL;
  out.vtable = NULL;
  if (!domain) {
    return out;
  }

  domain->kind = UV_DOMAIN_CPU;
  domain->_pad[0] = 0;
  domain->_pad[1] = 0;
  domain->_pad[2] = 0;
  domain->priority_hint = priority_rank;
  domain->max_concurrency = 4;
  domain->affinity_mask = affinity_mask_value;

  out.data = domain;
  return out;
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3agpu(
    const UVContext* self) {
  (void)self;
  UVDynObject out;
  out.data = &g_gpu_domain;
  out.vtable = NULL;
  return out;
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3ainline(
    const UVContext* self) {
  (void)self;
  UVDynObject out;
  out.data = &g_inline_domain;
  out.vtable = NULL;
  return out;
}

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3aname(
    const UVDynObject* self) {
  const UVExecutionDomain* domain =
      (const UVExecutionDomain*)(self ? self->data : NULL);
  const char* name = "cpu";
  if (domain) {
    switch (domain->kind) {
      case UV_DOMAIN_CPU:
        name = "cpu";
        break;
      case UV_DOMAIN_GPU:
        name = "gpu";
        break;
      case UV_DOMAIN_INLINE:
        name = "inline";
        break;
      default:
        name = "unknown";
        break;
    }
  }
  UVStringView out;
  out.data = (const uint8_t*)name;
  out.len = (uint64_t)uv_cstr_len(name);
  return out;
}

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3amax_x5fconcurrency(
    const UVDynObject* self) {
  const UVExecutionDomain* domain =
      (const UVExecutionDomain*)(self ? self->data : NULL);
  if (!domain) {
    return 1;
  }
  return domain->max_concurrency;
}
