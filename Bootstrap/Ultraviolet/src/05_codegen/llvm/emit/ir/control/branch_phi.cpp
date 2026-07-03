// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/branch_phi.cpp
// Canonical owner for LLVM IR branch and phi instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "00_core/spec_trace.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"

namespace ultraviolet::codegen::emit_detail {

namespace {

void ReportBranchPhiCodegenFailure(LLVMEmitter &emitter)
{
  if (const LowerCtx *ctx = emitter.GetCurrentCtx())
  {
    const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
  }
}

llvm::Function *CurrentFunction(llvm::IRBuilder<> &builder)
{
  llvm::BasicBlock *block = builder.GetInsertBlock();
  return block ? block->getParent() : nullptr;
}

llvm::BasicBlock *FindBlockByLabel(llvm::Function &function, const std::string &label)
{
  for (llvm::BasicBlock &block : function)
  {
    if (block.getName() == llvm::StringRef(label))
    {
      return &block;
    }
  }
  return nullptr;
}

llvm::BasicBlock *GetOrCreateBlockByLabel(LLVMEmitter &emitter,
                                          llvm::Function &function,
                                          const std::string &label)
{
  if (label.empty())
  {
    ReportBranchPhiCodegenFailure(emitter);
    return nullptr;
  }

  if (llvm::BasicBlock *block = FindBlockByLabel(function, label))
  {
    return block;
  }
  return llvm::BasicBlock::Create(emitter.GetContext(), label, &function);
}

llvm::PHINode *CreatePhiAtBlockHeader(LLVMEmitter &emitter,
                                      llvm::BasicBlock &block,
                                      llvm::Type *type,
                                      unsigned incoming_count,
                                      const std::string &name)
{
  llvm::IRBuilder<> phi_builder(emitter.GetContext());
  llvm::Instruction *first_non_phi = block.getFirstNonPHI();
  if (first_non_phi)
  {
    phi_builder.SetInsertPoint(first_non_phi);
  }
  else
  {
    phi_builder.SetInsertPoint(&block);
  }
  return phi_builder.CreatePHI(type, incoming_count, name);
}

bool BuilderHasOpenInsertionBlock(LLVMEmitter &emitter, llvm::IRBuilder<> &builder)
{
  llvm::BasicBlock *block = builder.GetInsertBlock();
  if (!block || block->getTerminator())
  {
    ReportBranchPhiCodegenFailure(emitter);
    return false;
  }
  return true;
}

std::string IRValueDisplayName(const IRValue &value)
{
  if (!value.name.empty())
  {
    return value.name;
  }
  switch (value.kind)
  {
  case IRValue::Kind::Opaque:
    return "<opaque>";
  case IRValue::Kind::Local:
    return "<local>";
  case IRValue::Kind::Symbol:
    return "<symbol>";
  case IRValue::Kind::Immediate:
    return "<immediate>";
  }
  return "<value>";
}

std::string LLVMTypeDisplayName(llvm::Type *type)
{
  if (!type)
  {
    return "<unknown>";
  }
  std::string type_name;
  llvm::raw_string_ostream stream(type_name);
  type->print(stream);
  stream.flush();
  return type_name;
}

std::string PhiIncomingPayload(const IRPhi &phi)
{
  std::string payload;
  for (std::size_t index = 0; index < phi.incoming.size(); ++index)
  {
    if (index != 0)
    {
      payload += ",";
    }
    payload += phi.incoming[index].label;
    payload += ":";
    payload += IRValueDisplayName(phi.incoming[index].value);
  }
  return payload;
}

void RecordBranchLowering(const IRBranch &branch)
{
  if (!core::Conformance::Enabled())
  {
    return;
  }

  if (branch.cond.has_value())
  {
    std::string form_payload =
        "source=IRBranchEmission;form=conditional;llvm=BrCond;true_label=";
    form_payload += branch.true_label;
    form_payload += ";false_label=";
    form_payload += branch.false_label;
    form_payload += ";condition=";
    form_payload += IRValueDisplayName(*branch.cond);
    form_payload += ";result=bottom";
    core::Conformance::Record("def.24.BranchLowerForms", std::nullopt, form_payload);

    std::string rule_payload =
        "obligation=rule.24.Lower-BranchIR-Conditional;ir_form=BranchIR(v_c,t,f);"
        "lower_form=BrCond(v_c,t,f);true_label=";
    rule_payload += branch.true_label;
    rule_payload += ";false_label=";
    rule_payload += branch.false_label;
    rule_payload += ";condition=";
    rule_payload += IRValueDisplayName(*branch.cond);
    rule_payload += ";result=bottom";
    core::Conformance::Record(
        "rule.24.Lower-BranchIR-Conditional", std::nullopt, rule_payload);
    return;
  }

  std::string form_payload =
      "source=IRBranchEmission;form=unconditional;llvm=Br;target=";
  form_payload += branch.true_label;
  form_payload += ";result=bottom";
  core::Conformance::Record("def.24.BranchLowerForms", std::nullopt, form_payload);

  std::string rule_payload =
      "obligation=rule.24.Lower-BranchIR-Unconditional;ir_form=BranchIR(target);"
      "lower_form=Br(target);target=";
  rule_payload += branch.true_label;
  rule_payload += ";result=bottom";
  core::Conformance::Record(
      "rule.24.Lower-BranchIR-Unconditional", std::nullopt, rule_payload);
}

llvm::Value *CoerceIncomingForPhi(LLVMEmitter &emitter,
                                  llvm::IRBuilder<> &builder,
                                  llvm::BasicBlock &predecessor,
                                  llvm::Value *incoming,
                                  llvm::Type *target_type,
                                  const analysis::TypeRef &source_type,
                                  const analysis::TypeRef &target_uv_type)
{
  if (!incoming || !target_type)
  {
    return nullptr;
  }
  if (incoming->getType() == target_type)
  {
    return incoming;
  }

  llvm::Instruction *terminator = predecessor.getTerminator();
  if (terminator)
  {
    llvm::IRBuilder<> predecessor_builder(terminator);
    llvm::Value *coerced = CoerceToTyped(
        emitter,
        &predecessor_builder,
        incoming,
        target_type,
        source_type,
        target_uv_type);
    if (!coerced)
    {
      coerced = CoerceTo(&predecessor_builder, incoming, target_type);
    }
    return coerced;
  }

  llvm::Value *coerced = CoerceToTyped(
      emitter,
      &builder,
      incoming,
      target_type,
      source_type,
      target_uv_type);
  if (!coerced)
  {
    coerced = CoerceTo(&builder, incoming, target_type);
  }
  return coerced;
}

void RecordPhiLowering(const IRPhi &phi, llvm::Type *phi_type)
{
  if (!core::Conformance::Enabled())
  {
    return;
  }

  std::string result_name = phi.value.name.empty() ? "<unnamed>" : phi.value.name;
  std::string uv_type = phi.type ? analysis::TypeToString(phi.type) : "<unknown>";
  std::string incoming = PhiIncomingPayload(phi);
  std::string form_payload =
      "source=IRPhiEmission;llvm=Phi;uv_type=" + uv_type +
      ";llvm_type=" + LLVMTypeDisplayName(phi_type) +
      ";incoming_count=" + std::to_string(phi.incoming.size()) +
      ";incoming=" + incoming +
      ";result=" + result_name;
  core::Conformance::Record("def.24.PhiLowerForm", std::nullopt, form_payload);

  std::string rule_payload =
      "obligation=rule.24.Lower-PhiIR;ir_form=PhiIR(T,inc,v);"
      "lower_form=Phi(tau,inc,v);uv_type=" +
      uv_type + ";llvm_type=" + LLVMTypeDisplayName(phi_type) +
      ";incoming_count=" + std::to_string(phi.incoming.size()) +
      ";incoming=" + incoming +
      ";result=" + result_name;
  core::Conformance::Record("rule.24.Lower-PhiIR", std::nullopt, rule_payload);
}

} // namespace

void IRInstructionVisitor::operator()(const IRBranch &branch) const
{
  if (!BuilderHasOpenInsertionBlock(emitter, builder))
  {
    return;
  }

  llvm::Function *function = CurrentFunction(builder);
  if (!function)
  {
    ReportBranchPhiCodegenFailure(emitter);
    return;
  }

  if (branch.cond.has_value())
  {
    if (branch.true_label.empty() || branch.false_label.empty())
    {
      ReportBranchPhiCodegenFailure(emitter);
      return;
    }

    llvm::Value *condition = emitter.EvaluateIRValue(*branch.cond);
    if (!condition)
    {
      ReportBranchPhiCodegenFailure(emitter);
      return;
    }

    llvm::BasicBlock *true_block =
        GetOrCreateBlockByLabel(emitter, *function, branch.true_label);
    llvm::BasicBlock *false_block =
        GetOrCreateBlockByLabel(emitter, *function, branch.false_label);
    if (!true_block || !false_block)
    {
      return;
    }

    builder.CreateCondBr(AsBool(&builder, condition), true_block, false_block);
    RecordBranchLowering(branch);
    return;
  }

  llvm::BasicBlock *target =
      GetOrCreateBlockByLabel(emitter, *function, branch.true_label);
  if (!target)
  {
    return;
  }

  builder.CreateBr(target);
  RecordBranchLowering(branch);
}

void IRInstructionVisitor::operator()(const IRPhi &phi) const
{
  if (!BuilderHasOpenInsertionBlock(emitter, builder))
  {
    return;
  }
  if (!phi.type || phi.value.kind != IRValue::Kind::Opaque || phi.value.name.empty())
  {
    ReportBranchPhiCodegenFailure(emitter);
    return;
  }

  llvm::Function *function = CurrentFunction(builder);
  llvm::Type *phi_type = emitter.GetLLVMType(phi.type);
  llvm::BasicBlock *phi_block = builder.GetInsertBlock();
  if (!function || !phi_type || phi_type->isVoidTy() || !phi_block)
  {
    ReportBranchPhiCodegenFailure(emitter);
    return;
  }
  if (IsZeroSizedLLVMType(emitter, phi_type))
  {
    emitter.SetTempValue(phi.value, llvm::Constant::getNullValue(phi_type));
    return;
  }

  llvm::PHINode *node = CreatePhiAtBlockHeader(
      emitter,
      *phi_block,
      phi_type,
      static_cast<unsigned>(phi.incoming.size()),
      phi.value.name);
  if (!node)
  {
    ReportBranchPhiCodegenFailure(emitter);
    return;
  }

  auto fail_phi = [&]()
  {
    node->eraseFromParent();
    ReportBranchPhiCodegenFailure(emitter);
  };

  std::vector<std::pair<llvm::Value *, llvm::BasicBlock *>> incoming_values;
  incoming_values.reserve(phi.incoming.size());
  for (const IRIncoming &incoming : phi.incoming)
  {
    llvm::BasicBlock *predecessor = FindBlockByLabel(*function, incoming.label);
    if (!predecessor || !predecessor->getTerminator())
    {
      fail_phi();
      return;
    }

    llvm::IRBuilderBase::InsertPoint saved_insert_point = builder.saveIP();
    builder.SetInsertPoint(predecessor->getTerminator());
    llvm::Value *incoming_value = emitter.EvaluateIRValue(incoming.value);
    builder.restoreIP(saved_insert_point);
    if (!incoming_value)
    {
      fail_phi();
      return;
    }

    llvm::Value *coerced = CoerceIncomingForPhi(
        emitter,
        builder,
        *predecessor,
        incoming_value,
        phi_type,
        LookupValueType(incoming.value),
        phi.type);
    if (!coerced || coerced->getType() != phi_type)
    {
      fail_phi();
      return;
    }
    incoming_values.push_back({coerced, predecessor});
  }

  for (const auto &[incoming_value, predecessor] : incoming_values)
  {
    node->addIncoming(incoming_value, predecessor);
  }

  emitter.SetTempValue(phi.value, node);
  RecordPhiLowering(phi, phi_type);
}

} // namespace ultraviolet::codegen::emit_detail
