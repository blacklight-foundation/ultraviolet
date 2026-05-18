#include "../internal/rt_internal.h"

#define SPEC_RULE(id) ((void)0)

static UVNetState* uv_net_state(const UVDynObject* net) {
  if (!net || !net->data) {
    return NULL;
  }
  return (UVNetState*)net->data;
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3anet_x3a_x3arestrict_x5fto_x5fhost(
    const UVDynObject* self,
    const UVStringView* host) {
  SPEC_RULE("Prim-Network-RestrictHost");
  uv_trace_emit_rule("Prim-Network-RestrictHost");

  UVDynObject out;
  out.data = NULL;
  out.vtable = self ? self->vtable : NULL;

  UVNetState* state = (UVNetState*)uv_heap_alloc_raw(sizeof(UVNetState));
  if (!state) {
    return out;
  }

  state->parent = uv_net_state(self);
  state->host_utf8 = NULL;
  state->host_len = 0;
  state->restricted = 1;
  state->valid = 1;

  if (!host || (!host->data && host->len != 0)) {
    state->valid = 0;
    out.data = state;
    return out;
  }

  if (host->len != 0) {
    uint8_t* host_copy = (uint8_t*)uv_heap_alloc_raw((size_t)host->len);
    if (!host_copy) {
      uv_heap_free_raw(state);
      return out;
    }
    uv_memcpy(host_copy, host->data, (size_t)host->len);
    state->host_utf8 = host_copy;
    state->host_len = (uint32_t)host->len;
  }

  out.data = state;
  return out;
}
