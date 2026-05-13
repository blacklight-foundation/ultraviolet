#include "../internal/rt_internal.h"

void cursive_x3a_x3aruntime_x3a_x3acontext_x5finit(C0Context* out) {
  if (!out) {
    return;
  }

  out->fs.data = NULL;
  out->fs.vtable = NULL;
  out->net.data = NULL;
  out->net.vtable = NULL;
  out->heap.data = NULL;
  out->heap.vtable = NULL;
  out->reactor.data = NULL;
  out->reactor.vtable = NULL;
  out->time.data = NULL;
  out->time.vtable = NULL;

  C0FsState* fs = (C0FsState*)c0_heap_alloc_raw(sizeof(C0FsState));
  if (!fs) {
    return;
  }
  fs->base_utf8 = NULL;
  fs->base_len = 0;
  fs->restricted = 0;
  fs->valid = 1;

  C0NetState* net = (C0NetState*)c0_heap_alloc_raw(sizeof(C0NetState));
  if (!net) {
    c0_heap_free_raw(fs);
    return;
  }
  net->parent = NULL;
  net->host_utf8 = NULL;
  net->host_len = 0;
  net->restricted = 0;
  net->valid = 1;

  C0HeapState* heap = (C0HeapState*)c0_heap_alloc_raw(sizeof(C0HeapState));
  if (!heap) {
    c0_heap_free_raw(net);
    c0_heap_free_raw(fs);
    return;
  }
  heap->parent = NULL;
  heap->quota = 0;
  heap->used = 0;

  C0TimeState* time = (C0TimeState*)c0_heap_alloc_raw(sizeof(C0TimeState));
  if (!time) {
    c0_heap_free_raw(heap);
    c0_heap_free_raw(net);
    c0_heap_free_raw(fs);
    return;
  }
  time->parent = NULL;
  time->kind = C0_TIME_STATE_ROOT;
  time->domain = 1;
  time->resolution.lo = 1;
  time->resolution.hi = 0;

  out->fs.data = fs;
  out->fs.vtable = NULL;
  out->net.data = net;
  out->net.vtable = NULL;
  out->heap.data = heap;
  out->heap.vtable = NULL;
  out->time.data = time;
  out->time.vtable = NULL;
}

static C0ExecutionDomain g_cpu_domain = {C0_DOMAIN_CPU, {0}, 4};
static C0ExecutionDomain g_gpu_domain = {C0_DOMAIN_GPU, {0}, 1};
static C0ExecutionDomain g_inline_domain = {C0_DOMAIN_INLINE, {0}, 1};

C0DynObject cursive_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu(
    const C0Context* self) {
  (void)self;
  C0DynObject out;
  out.data = &g_cpu_domain;
  out.vtable = NULL;
  return out;
}

C0DynObject cursive_x3a_x3aruntime_x3a_x3acontext_x3a_x3agpu(
    const C0Context* self) {
  (void)self;
  C0DynObject out;
  out.data = &g_gpu_domain;
  out.vtable = NULL;
  return out;
}

C0DynObject cursive_x3a_x3aruntime_x3a_x3acontext_x3a_x3ainline(
    const C0Context* self) {
  (void)self;
  C0DynObject out;
  out.data = &g_inline_domain;
  out.vtable = NULL;
  return out;
}

C0StringView cursive_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3aname(
    const C0DynObject* self) {
  const C0ExecutionDomain* domain =
      self ? (const C0ExecutionDomain*)self->data : NULL;
  const char* name = "cpu";
  if (domain) {
    switch (domain->kind) {
      case C0_DOMAIN_CPU:
        name = "cpu";
        break;
      case C0_DOMAIN_GPU:
        name = "gpu";
        break;
      case C0_DOMAIN_INLINE:
        name = "inline";
        break;
      default:
        name = "unknown";
        break;
    }
  }
  C0StringView out;
  out.data = (const uint8_t*)name;
  out.len = (uint64_t)c0_cstr_len(name);
  return out;
}

uint64_t cursive_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3amax_x5fconcurrency(
    const C0DynObject* self) {
  const C0ExecutionDomain* domain =
      self ? (const C0ExecutionDomain*)self->data : NULL;
  if (!domain) {
    return 1;
  }
  return domain->max_concurrency;
}

static C0Usize3 c0_gpu_triplet(uint64_t x, uint64_t y, uint64_t z) {
  C0Usize3 out;
  out.x = x;
  out.y = y;
  out.z = z;
  return out;
}

C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fid(void) {
  return c0_gpu_triplet(0, 0, 0);
}

C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3alocal_x5fid(void) {
  return c0_gpu_triplet(0, 0, 0);
}

C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fid(void) {
  return c0_gpu_triplet(0, 0, 0);
}

C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fsize(void) {
  return c0_gpu_triplet(1, 1, 1);
}

C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fsize(void) {
  return c0_gpu_triplet(1, 1, 1);
}

C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3anum_x5fworkgroups(void) {
  return c0_gpu_triplet(1, 1, 1);
}

uint64_t cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3alinear_x5fid(void) {
  return 0;
}

void cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3abarrier(void) {}

void cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3amemory_x5fbarrier(void) {}

void cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fbarrier(void) {}
