// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/speculation.cpp
// Canonical owner for LLVM IR async speculation instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRSpecSnapshot &spec) const
{
  emitter.SetTempValue(spec.result, DefaultFor(spec.result));
}

void IRInstructionVisitor::operator()(const IRSpecValidate &spec) const
{
  emitter.SetTempValue(spec.result, DefaultFor(spec.result));
}

void IRInstructionVisitor::operator()(const IRSpecCommit &spec) const
{
  llvm::Value *value = EvaluateOrDefault(spec.value);
  if (!value)
  {
    value = DefaultFor(spec.result);
  }
  emitter.SetTempValue(spec.result, value);
}

void IRInstructionVisitor::operator()(const IRSpecRetry &spec) const
{
  emitter.SetTempValue(spec.result, DefaultFor(spec.result));
}

void IRInstructionVisitor::operator()(const IRSpecFallback &spec) const
{
  emitter.EmitIR(spec.body);
  if (!emitter.GetTempValue(spec.result))
  {
    emitter.SetTempValue(spec.result, DefaultFor(spec.result));
  }
}

void IRInstructionVisitor::operator()(const IRSpecLoop &spec) const
{
  emitter.EmitIR(spec.fallback_ir);
  if (!emitter.GetTempValue(spec.result))
  {
    emitter.SetTempValue(spec.result, DefaultFor(spec.result));
  }
}

} // namespace ultraviolet::codegen::emit_detail
