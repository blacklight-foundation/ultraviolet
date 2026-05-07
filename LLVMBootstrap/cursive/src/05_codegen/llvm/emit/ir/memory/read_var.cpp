// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/read_var.cpp
// Canonical owner for LLVM IR variable read instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRReadVar &) const
{
  // Read semantics are represented by IRValue::Local in operand positions.
}

} // namespace cursive::codegen::emit_detail
