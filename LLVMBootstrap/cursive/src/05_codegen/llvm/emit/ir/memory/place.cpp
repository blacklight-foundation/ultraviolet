// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/place.cpp
// Canonical owner for LLVM IR place read and write instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRReadPlace &) const
{}

void IRInstructionVisitor::operator()(const IRWritePlace &) const
{}

} // namespace cursive::codegen::emit_detail
