// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/block.cpp
// Canonical owner for LLVM IR block instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRBlock &block) const
{
  emitter.EmitIR(block.setup);
  emitter.EmitIR(block.body);
  SetForwardedOrMaterializedResult(block.value);
}

} // namespace cursive::codegen::emit_detail
