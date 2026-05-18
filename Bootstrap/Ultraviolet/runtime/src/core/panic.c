#include "../internal/rt_internal.h"

void ultraviolet_x3a_x3aruntime_x3a_x3apanic(uint32_t code) {
  uv_trace_emit_rule_with_meta("PanicSym", "runtime", "error");
  if (uv_parallel_in_panic_scope()) {
    uv_parallel_raise_panic(code);
    uv_platform_exit_process((uv_platform_uint_t)code);
  }
  uv_platform_exit_process((uv_platform_uint_t)code);
}

void uv_panic(uint32_t code) {
  uv_trace_emit_rule_with_meta("PanicSym", "runtime", "error");
  ultraviolet_x3a_x3aruntime_x3a_x3apanic(code);
}
