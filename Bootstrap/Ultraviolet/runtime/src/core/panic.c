#include "../internal/rt_internal.h"

void ultraviolet_x3a_x3aruntime_x3a_x3apanic(const uint32_t* code) {
  const uint32_t panic_code = code ? *code : 1u;
  uv_trace_emit_rule_with_meta("PanicSym", "runtime", "error");
  if (uv_parallel_in_panic_scope()) {
    uv_parallel_raise_panic(panic_code);
    uv_platform_exit_process((uv_platform_uint_t)panic_code);
  }
  uv_platform_exit_process((uv_platform_uint_t)panic_code);
}

void uv_panic(uint32_t code) {
  uv_trace_emit_rule_with_meta("PanicSym", "runtime", "error");
  ultraviolet_x3a_x3aruntime_x3a_x3apanic(&code);
}
