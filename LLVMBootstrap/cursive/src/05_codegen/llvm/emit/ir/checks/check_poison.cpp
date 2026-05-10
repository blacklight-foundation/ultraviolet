// =============================================================================
// File: 05_codegen/llvm/emit/ir/checks/check_poison.cpp
// Canonical owner for LLVM IR poison check instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCheckPoison &check) const
{
  emitter.EmitPoisonCheck(check.module);
}

} // namespace cursive::codegen::emit_detail
