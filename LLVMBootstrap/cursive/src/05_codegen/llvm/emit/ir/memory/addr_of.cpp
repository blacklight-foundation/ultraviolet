// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/addr_of.cpp
// Canonical owner for LLVM IR address-of instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRAddrOf &addrof) const
{
  auto cache_if_safe = [&](llvm::Value *value)
  {
    if (!value)
    {
      return;
    }
    if (llvm::isa<llvm::Constant>(value) ||
        llvm::isa<llvm::GlobalValue>(value) ||
        llvm::isa<llvm::Argument>(value) ||
        llvm::isa<llvm::AllocaInst>(value))
    {
      emitter.SetTempValue(addrof.result, value);
    }
  };

  // Prefer derived address materialization (AddrField/AddrTuple/AddrIndex/etc.).
  // Falling back to place-root lookup for these cases collapses addresses to the
  // base binding and loses field/element offsets.
  if (llvm::Value *derived_ptr = emitter.EvaluateIRValue(addrof.result);
      derived_ptr && derived_ptr->getType()->isPointerTy())
  {
    if (llvm::Type *expected = ExpectedLLVMType(addrof.result))
    {
      if (expected->isPointerTy())
      {
        derived_ptr = CoerceTo(&builder, derived_ptr, expected);
      }
    }
    cache_if_safe(derived_ptr);
    return;
  }

  std::string base_name = BasePlaceIdentifier(addrof.place.repr);
  if (base_name.empty())
  {
    base_name = addrof.place.repr;
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const BindingState *local_binding =
      active_ctx ? active_ctx->GetBindingState(base_name) : nullptr;

  llvm::Value *ptr = emitter.GetLocalBindStorage(base_name);
  bool hosted_state_resolution_failed = false;
  auto resolve_hosted_state_ptr = [&](const std::string &symbol_name,
                                      llvm::Type *static_ll) -> llvm::Value * {
    llvm::Value *fallback = nullptr;
    if (llvm::Value *global_value = emitter.GetGlobal(symbol_name))
    {
      fallback = global_value;
    }
    if (!fallback)
    {
      fallback = emitter.GetModule().getNamedGlobal(symbol_name);
    }
    llvm::Value *state_ptr =
        emitter.GetHostedStatePtr(symbol_name, static_ll, fallback);
    if (!state_ptr && emitter.HasHostedStateSlot(symbol_name) && !fallback)
    {
      hosted_state_resolution_failed = true;
    }
    return state_ptr;
  };
  if (!ptr)
  {
    if (auto alias = emitter.LookupSymbolAlias(base_name))
    {
      if (active_ctx && emitter.IsHostedLibraryBuild())
      {
        analysis::TypeRef static_type = active_ctx->LookupStaticType(*alias);
        if (static_type)
        {
          if (llvm::Type *static_ll = emitter.GetLLVMType(static_type))
          {
            ptr = resolve_hosted_state_ptr(*alias, static_ll);
          }
        }
      }
      if (!ptr && !hosted_state_resolution_failed)
      {
        ptr = emitter.GetGlobal(*alias);
      }
      if (!ptr && !hosted_state_resolution_failed)
      {
        ptr = emitter.GetFunction(*alias);
      }
    }
  }
  if (!ptr && !hosted_state_resolution_failed)
  {
    if (active_ctx)
    {
      auto try_hosted_state = [&](const std::string &symbol_name) -> llvm::Value * {
        if (!emitter.IsHostedLibraryBuild())
        {
          return nullptr;
        }
        analysis::TypeRef static_type = active_ctx->LookupStaticType(symbol_name);
        if (!static_type)
        {
          return nullptr;
        }
        llvm::Type *static_ll = emitter.GetLLVMType(static_type);
        if (!static_ll || static_ll->isVoidTy())
        {
          return nullptr;
        }
        return resolve_hosted_state_ptr(symbol_name, static_ll);
      };

      if (auto alias = emitter.LookupSymbolAlias(base_name))
      {
        ptr = try_hosted_state(*alias);
      }
      if (!ptr)
      {
        ptr = try_hosted_state(base_name);
      }
    }
  }
  if (hosted_state_resolution_failed)
  {
    if (active_ctx)
    {
      const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
    }
    emitter.SetTempValue(addrof.result, DefaultFor(addrof.result));
    return;
  }
  if (!ptr && local_binding)
  {
    const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
    emitter.SetTempValue(addrof.result, DefaultFor(addrof.result));
    return;
  }
  if (!ptr)
  {
    ptr = emitter.GetGlobal(base_name);
  }
  if (!ptr)
  {
    ptr = emitter.GetFunction(base_name);
  }

  if (!ptr || !ptr->getType()->isPointerTy())
  {
    if (local_binding && active_ctx)
    {
      const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
    }
    emitter.SetTempValue(addrof.result, DefaultFor(addrof.result));
    return;
  }

  if (llvm::Type *expected = ExpectedLLVMType(addrof.result))
  {
    if (expected->isPointerTy())
    {
      ptr = CoerceTo(&builder, ptr, expected);
    }
  }
  cache_if_safe(ptr ? ptr : DefaultFor(addrof.result));
}

} // namespace cursive::codegen::emit_detail
