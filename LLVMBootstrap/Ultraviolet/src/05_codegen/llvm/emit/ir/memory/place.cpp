// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/place.cpp
// Canonical owner for LLVM IR place read and write instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRReadPlace &) const
{
  if (const LowerCtx *ctx = emitter.GetCurrentCtx())
  {
    const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
  }
}

void IRInstructionVisitor::operator()(const IRWritePlace &) const
{
  if (const LowerCtx *ctx = emitter.GetCurrentCtx())
  {
    const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
  }
}

} // namespace ultraviolet::codegen::emit_detail
