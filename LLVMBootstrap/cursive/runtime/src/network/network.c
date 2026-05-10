#include "../internal/rt_internal.h"

#define SPEC_RULE(id) ((void)0)

static C0NetState* c0_net_state(const C0DynObject* net) {
  if (!net || !net->data) {
    return NULL;
  }
  return (C0NetState*)net->data;
}

C0DynObject cursive_x3a_x3aruntime_x3a_x3anet_x3a_x3arestrict_x5fto_x5fhost(
    const C0DynObject* self,
    const C0StringView* host) {
  SPEC_RULE("Prim-Network-RestrictHost");
  c0_trace_emit_rule("Prim-Network-RestrictHost");

  C0DynObject out;
  out.data = NULL;
  out.vtable = self ? self->vtable : NULL;

  C0NetState* state = (C0NetState*)c0_heap_alloc_raw(sizeof(C0NetState));
  if (!state) {
    return out;
  }

  state->parent = c0_net_state(self);
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
    uint8_t* host_copy = (uint8_t*)c0_heap_alloc_raw((size_t)host->len);
    if (!host_copy) {
      c0_heap_free_raw(state);
      return out;
    }
    c0_memcpy(host_copy, host->data, (size_t)host->len);
    state->host_utf8 = host_copy;
    state->host_len = (uint32_t)host->len;
  }

  out.data = state;
  return out;
}
