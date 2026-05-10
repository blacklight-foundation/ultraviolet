#include "../internal/rt_internal.h"

void cursive_x3a_x3aruntime_x3a_x3apanic(uint32_t code) {
  c0_trace_emit_rule_with_meta("PanicSym", "runtime", "error");
  if (c0_parallel_in_panic_scope()) {
    c0_parallel_raise_panic(code);
    return;
  }
  cursive_platform_exit_process((cursive_platform_uint_t)code);
}

void cursive_panic(uint32_t code) {
  c0_trace_emit_rule_with_meta("PanicSym", "runtime", "error");
  cursive_x3a_x3aruntime_x3a_x3apanic(code);
}
