// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/result.cpp
// Canonical owner for LLVM IR result instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRResult &result) const
{
  SetForwardedOrMaterializedResult(result.value);
}

} // namespace ultraviolet::codegen::emit_detail
