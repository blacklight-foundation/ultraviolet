// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/frame.cpp
// Canonical owner for LLVM IR frame instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRFrame &frame) const
{
  emitter.EmitIR(frame.body);
  SetForwardedOrMaterializedResult(frame.value);
}

} // namespace ultraviolet::codegen::emit_detail
