// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/frame.cpp
// Canonical owner for LLVM IR frame instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRFrame &frame) const
{
  emitter.EmitIR(frame.body);
  SetForwardedOrMaterializedResult(frame.value);
  if (core::Conformance::Enabled())
  {
    core::Conformance::Record(
        "def.24.StructuredIRLoweringForms",
        std::nullopt,
        "obligation=def.24.StructuredIRLoweringForms;"
        "structured_form=FrameIRForm;"
        "structured_lower_form=FrameLowerForm;"
        "source=IRFrameEmission");
    core::Conformance::Record(
        "rule.24.Lower-FrameIR",
        std::nullopt,
        "obligation=rule.24.Lower-FrameIR;"
        "ir_form=FrameIRForm;"
        "lower_form=FrameLowerForm;"
        "source=IRFrameEmission;"
        "body_emitted=true;"
        "result=value");
  }
}

} // namespace ultraviolet::codegen::emit_detail
