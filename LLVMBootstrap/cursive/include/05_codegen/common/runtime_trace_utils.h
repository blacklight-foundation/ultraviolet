#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

inline IRValue TraceStringImmediate(std::string_view text) {
  IRValue value;
  value.kind = IRValue::Kind::Immediate;
  value.name = "\"" + std::string(text) + "\"";
  value.bytes.assign(text.begin(), text.end());
  return value;
}

inline IRValue TraceU64Immediate(std::uint64_t value) {
  IRValue out;
  out.kind = IRValue::Kind::Immediate;
  out.name = std::to_string(value);
  out.bytes.resize(8);
  for (std::size_t i = 0; i < 8; ++i) {
    out.bytes[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
  }
  return out;
}

inline IRPtr EmitRuntimeTrace(std::string_view rule_id, const LowerCtx& ctx) {
  if (rule_id.empty() || !ctx.log_enabled) {
    return EmptyIR();
  }
  IRCall call;
  call.callee.kind = IRValue::Kind::Symbol;
  call.callee.name = RuntimeConformanceEmitSym();
  call.args.push_back(TraceStringImmediate(rule_id));
  call.args.push_back(TraceStringImmediate("-"));
  call.args.push_back(TraceU64Immediate(0));
  call.args.push_back(TraceU64Immediate(0));
  call.args.push_back(TraceU64Immediate(0));
  call.args.push_back(TraceU64Immediate(0));
  call.args.push_back(TraceStringImmediate(""));
  return MakeIR(std::move(call));
}

}  // namespace cursive::codegen
