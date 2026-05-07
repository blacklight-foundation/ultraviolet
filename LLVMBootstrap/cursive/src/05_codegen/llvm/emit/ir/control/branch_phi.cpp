// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/branch_phi.cpp
// Canonical owner for LLVM IR branch and phi instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRBranch &) const
{}

void IRInstructionVisitor::operator()(const IRPhi &) const
{}

} // namespace cursive::codegen::emit_detail
