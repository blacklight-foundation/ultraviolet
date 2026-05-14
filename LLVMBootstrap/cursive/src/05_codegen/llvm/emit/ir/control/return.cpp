// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/return.cpp
// Canonical owner for LLVM IR return instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

namespace {

bool IsResolvedUnion(const analysis::TypeRef &type)
{
  return type && std::holds_alternative<analysis::TypeUnion>(type->node);
}

bool CanSkipAliasedSRetStore(const LowerCtx *ctx,
                             const analysis::TypeRef &source_type,
                             const analysis::TypeRef &target_type)
{
  analysis::TypeRef resolved_source = ResolveAliasType(ctx, source_type);
  analysis::TypeRef resolved_target = ResolveAliasType(ctx, target_type);
  if (!resolved_source || !resolved_target)
  {
    return false;
  }

  if (IsResolvedUnion(resolved_target) && !IsResolvedUnion(resolved_source))
  {
    return false;
  }

  const auto equiv = analysis::TypeEquiv(resolved_source, resolved_target);
  return equiv.ok && equiv.equiv;
}

} // namespace

void IRInstructionVisitor::operator()(const IRReturn &ret) const
{
  llvm::Function *func = builder.GetInsertBlock()->getParent();
  llvm::Type *ret_ty = func->getReturnType();
  const LowerCtx *ctx = emitter.GetCurrentCtx();
  const std::string sym = std::string(func->getName());
  const LowerCtx::ProcSigInfo *sig = ctx ? ctx->LookupProcSig(sym) : nullptr;
  analysis::TypeRef source_type = LookupValueType(ret.value);
  const bool debug_return = core::IsDebugEnabled("return") &&
                            sym.find("PropagationMaybeDouble") != std::string::npos;
  if (debug_return)
  {
    std::string ret_ty_text;
    if (ret_ty)
    {
      llvm::raw_string_ostream os(ret_ty_text);
      ret_ty->print(os);
      os.flush();
    }
    else
    {
      ret_ty_text = "<null>";
    }
    std::cerr << "[return-debug] fn=" << sym
              << " ret_kind=" << static_cast<int>(ret.value.kind)
              << " ret_name=" << ret.value.name
              << " source_type="
              << (source_type ? analysis::TypeToString(source_type) : std::string("<null>"))
              << " sig_ret="
              << (sig && sig->ret ? analysis::TypeToString(sig->ret) : std::string("<null>"))
              << " llvm_ret=" << ret_ty_text << "\n";
  }

  if (ret_ty->isVoidTy())
  {
    if (sig && sig->ret)
    {
      llvm::Value *out_ptr =
          ResolveProcedureOutPtr(emitter, &builder, func, sym, sig);
      if (out_ptr &&
          TryEmitDerivedAggregateToStorage(
              emitter, &builder, out_ptr, ret.value, sig->ret))
      {
        builder.CreateRetVoid();
        return;
      }
      llvm::Value *source_storage = emitter.GetAddressableStorage(ret.value);
      if (out_ptr && source_storage)
      {
        llvm::Value *normalized_source = source_storage;
        llvm::Type *out_ty = emitter.GetLLVMType(sig->ret);
        llvm::Type *target_ptr_ty =
            out_ty ? llvm::PointerType::get(out_ty, 0) : nullptr;
        if (target_ptr_ty)
        {
          if (normalized_source->getType()->isIntegerTy())
          {
            normalized_source =
                builder.CreateIntToPtr(normalized_source, target_ptr_ty);
          }
          else
          {
            llvm::Value *coerced =
                CoerceTo(&builder, normalized_source, target_ptr_ty);
            if (coerced)
            {
              normalized_source = coerced;
            }
            else if (normalized_source->getType()->isPointerTy())
            {
              normalized_source =
                  builder.CreateBitCast(normalized_source, target_ptr_ty);
            }
          }
        }

        if (normalized_source && normalized_source->getType()->isPointerTy() &&
            out_ptr->stripPointerCasts() ==
                normalized_source->stripPointerCasts() &&
            CanSkipAliasedSRetStore(ctx, source_type, sig->ret))
        {
          builder.CreateRetVoid();
          return;
        }

        if (TryEmitBitcopyAggregateStorageCopy(
                emitter,
                &builder,
                out_ptr,
                source_storage,
                sig->ret,
                source_type))
        {
          builder.CreateRetVoid();
          return;
        }
      }
    }

    llvm::Value *value = EvaluateOrDefault(ret.value);
    if (debug_return && value)
    {
      std::string value_ty_text;
      llvm::raw_string_ostream os(value_ty_text);
      value->getType()->print(os);
      os.flush();
      std::cerr << "[return-debug] fn=" << sym
                << " pre-coerce llvm_value_ty=" << value_ty_text << "\n";
    }
    (void)StoreProcedureOutValue(
        emitter,
        &builder,
        func,
        sym,
        sig,
        value,
        source_type);
    builder.CreateRetVoid();
    return;
  }
  llvm::Value *value = EvaluateOrDefault(ret.value);
  if (debug_return && value)
  {
    std::string value_ty_text;
    llvm::raw_string_ostream os(value_ty_text);
    value->getType()->print(os);
    os.flush();
    std::cerr << "[return-debug] fn=" << sym
              << " pre-coerce llvm_value_ty=" << value_ty_text << "\n";
  }
  value = CoerceToTyped(
      emitter,
      &builder,
      value,
      ret_ty,
      source_type,
      sig ? sig->ret : nullptr);
  if (!value)
  {
    value = llvm::Constant::getNullValue(ret_ty);
  }
  if (debug_return && value)
  {
    std::string value_ty_text;
    llvm::raw_string_ostream os(value_ty_text);
    value->getType()->print(os);
    os.flush();
    std::cerr << "[return-debug] fn=" << sym
              << " post-coerce llvm_value_ty=" << value_ty_text << "\n";
  }
  builder.CreateRet(value);
}

} // namespace cursive::codegen::emit_detail
