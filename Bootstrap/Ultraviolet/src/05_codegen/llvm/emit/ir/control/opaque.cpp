// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/opaque.cpp
// Canonical owner for LLVM IR opaque instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include "00_core/spec_trace.h"

#include <optional>

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IROpaque &) const
{
  SPEC_RULE("rule.24.LowerIRInstr-Empty");

  if (core::Conformance::Enabled())
  {
    core::Conformance::Record(
        "rule.24.LowerIRInstr-Empty",
        std::nullopt,
        "obligation=rule.24.LowerIRInstr-Empty;"
        "ir_form=EmptyIR;"
        "lower_form=LLVMInstrList.empty;"
        "result=bottom;"
        "emits_instruction=false");
  }
}

} // namespace ultraviolet::codegen::emit_detail
