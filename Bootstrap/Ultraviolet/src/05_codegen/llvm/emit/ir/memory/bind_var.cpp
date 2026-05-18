// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/bind_var.cpp
// Canonical owner for LLVM IR variable binding instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRBindVar &bind) const
{
  emitter.EmitBindVar(bind);
}

} // namespace ultraviolet::codegen::emit_detail
