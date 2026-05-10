// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/opaque.cpp
// Canonical owner for LLVM IR opaque instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IROpaque &) const
{}

} // namespace cursive::codegen::emit_detail
