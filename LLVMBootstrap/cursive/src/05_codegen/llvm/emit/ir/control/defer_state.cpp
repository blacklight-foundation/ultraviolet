// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/defer_state.cpp
// Canonical owner for LLVM IR defer and move-state instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRDefer &) const
{}

void IRInstructionVisitor::operator()(const IRMoveState &) const
{}

} // namespace cursive::codegen::emit_detail
