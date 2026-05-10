// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/fence.cpp
// Canonical owner for LLVM IR fence instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRFence &fence) const
{
  llvm::AtomicOrdering ordering = llvm::AtomicOrdering::SequentiallyConsistent;
  switch (fence.order)
  {
  case IRFenceOrder::Acquire:
    ordering = llvm::AtomicOrdering::Acquire;
    break;
  case IRFenceOrder::Release:
    ordering = llvm::AtomicOrdering::Release;
    break;
  case IRFenceOrder::SeqCst:
    ordering = llvm::AtomicOrdering::SequentiallyConsistent;
    break;
  }
  builder.CreateFence(ordering);
  emitter.SetTempValue(fence.result, DefaultFor(fence.result));
}

} // namespace cursive::codegen::emit_detail
