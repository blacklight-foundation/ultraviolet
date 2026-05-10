// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/region.cpp
// Canonical owner for LLVM IR region instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRRegion &region) const
{
  llvm::Value *opts_value = EvaluateOrDefault(region.owner);
  if (!opts_value)
  {
    emitter.EmitIR(region.body);
    SetForwardedOrMaterializedResult(region.value);
    return;
  }

  analysis::TypePath region_path;
  region_path.push_back("Region");
  analysis::TypeRef region_active_type =
      analysis::MakeTypeModalState(std::move(region_path), "Active");
  llvm::Type *region_llvm_ty = emitter.GetLLVMType(region_active_type);
  if (!region_llvm_ty || region_llvm_ty->isVoidTy())
  {
    emitter.EmitIR(region.body);
    SetForwardedOrMaterializedResult(region.value);
    return;
  }

  llvm::Function *current_fn =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!current_fn)
  {
    emitter.EmitIR(region.body);
    SetForwardedOrMaterializedResult(region.value);
    return;
  }

  llvm::IRBuilder<> entry_builder(
      &current_fn->getEntryBlock(),
      current_fn->getEntryBlock().begin());
  llvm::Value *region_value = nullptr;
  const auto new_scoped_sym = analysis::LookupBuiltinModalRuntimeSymbol(
      {"Region"}, std::nullopt, "new_scoped");
  if (new_scoped_sym.has_value())
  {
    if (std::optional<RuntimeFuncInfo> new_scoped_info =
            GetRuntimeFuncInfo(*new_scoped_sym))
    {
      llvm::Function *new_scoped_fn =
          emitter.GetModule().getFunction(*new_scoped_sym);
      const bool use_c_abi_aggregate_sret = true;
      if (!new_scoped_fn)
      {
        ABICallResult new_scoped_abi = ComputeCallABI(
            emitter,
            new_scoped_info->params,
            new_scoped_info->ret,
            use_c_abi_aggregate_sret);
        if (new_scoped_abi.func_type)
        {
          new_scoped_fn = llvm::Function::Create(
              new_scoped_abi.func_type,
              llvm::GlobalValue::ExternalLinkage,
              *new_scoped_sym,
              &emitter.GetModule());
          new_scoped_fn->setCallingConv(llvm::CallingConv::C);
        }
      }
      if (new_scoped_fn)
      {
        std::vector<llvm::Value *> new_scoped_args;
        new_scoped_args.reserve(1);
        new_scoped_args.push_back(opts_value);
        region_value = EmitABICall(
            emitter,
            &builder,
            new_scoped_fn,
            new_scoped_info->params,
            new_scoped_info->ret,
            new_scoped_args,
            use_c_abi_aggregate_sret);
      }
    }
  }
  if (!region_value)
  {
    emitter.EmitIR(region.body);
    SetForwardedOrMaterializedResult(region.value);
    return;
  }

  std::optional<std::string> alias = region.alias;
  if (!alias.has_value() || alias->empty())
  {
    alias = std::string(project::ActiveLanguageProfile().region_active_alias);
  }

  llvm::Value *previous_local = emitter.GetLocal(*alias);
  analysis::TypeRef previous_type = emitter.LookupLocalType(*alias);
  const bool had_previous_local = previous_local != nullptr;

  llvm::AllocaInst *region_slot = entry_builder.CreateAlloca(
      region_llvm_ty,
      nullptr,
      *alias);
  builder.CreateStore(region_value, region_slot);
  emitter.SetLocal(*alias, region_slot);
  emitter.SetLocalType(*alias, region_active_type);

  IRValue active_region;
  active_region.kind = IRValue::Kind::Local;
  active_region.name = *alias;

  emitter.PushActiveRegion(active_region);
  emitter.EmitIR(region.body);
  emitter.PopActiveRegion();

  if (had_previous_local)
  {
    emitter.SetLocal(*alias, previous_local);
    if (previous_type)
    {
      emitter.SetLocalType(*alias, previous_type);
    }
    else
    {
      emitter.RemoveLocal(*alias);
      emitter.SetLocal(*alias, previous_local);
    }
  }
  else
  {
    emitter.RemoveLocal(*alias);
  }

  SetForwardedOrMaterializedResult(region.value);
}

} // namespace cursive::codegen::emit_detail
