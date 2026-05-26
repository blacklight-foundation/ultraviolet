// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/opaque.cpp
// Canonical owner for LLVM IR opaque instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IROpaque &) const
{}

} // namespace ultraviolet::codegen::emit_detail
