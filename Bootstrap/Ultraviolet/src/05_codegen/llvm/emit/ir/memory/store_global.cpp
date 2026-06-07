// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/store_global.cpp
// Canonical owner for LLVM IR global store instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include "00_core/spec_trace.h"
#include "04_analysis/layout/layout.h"

#include <optional>
#include <string>

namespace ultraviolet::codegen::emit_detail {

namespace {

void RecordGlobalStateRef(const char *operation)
{
  SPEC_RULE("def.24.StateRefJudg");
  SPEC_RULE("rule.24.StateRef-Global");
  if (!core::Conformance::Enabled())
  {
    return;
  }

  std::string payload = "source=IRStoreGlobal;operation=";
  payload += operation;
  payload += ";state_ref=global;slot=llvm_global_address;hosted_state=false";
  core::Conformance::Record("def.24.StateRefJudg", std::nullopt, payload);
  core::Conformance::Record("rule.24.StateRef-Global", std::nullopt, payload);
}

void RecordGlobalStoreMemoryHelper()
{
  SPEC_RULE("def.24.MemoryInstructionHelpers");
  if (!core::Conformance::Enabled())
  {
    return;
  }

  core::Conformance::Record(
      "def.24.MemoryInstructionHelpers",
      std::nullopt,
      "source=IRStoreGlobal;helper=Store;lower_form=llvm.store");
}

void RecordLowerStoreGlobal(bool has_static_type, bool has_hosted_state_slot)
{
  SPEC_RULE("rule.24.Lower-StoreGlobal");
  if (!core::Conformance::Enabled())
  {
    return;
  }

  std::string payload =
      "source=IRStoreGlobal;ir_form=StoreGlobal;lower_form=llvm.store";
  payload += ";static_type=";
  payload += has_static_type ? "true" : "false";
  payload += ";state_ref=";
  payload += has_hosted_state_slot ? "hosted_session" : "global";
  core::Conformance::Record("rule.24.Lower-StoreGlobal", std::nullopt, payload);
}

} // namespace

void IRInstructionVisitor::operator()(const IRStoreGlobal &store) const
{
  std::string symbol = store.symbol;
  if (auto alias = emitter.LookupSymbolAlias(symbol))
  {
    symbol = *alias;
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef target_type =
      active_ctx ? active_ctx->LookupStaticType(symbol) : nullptr;
  llvm::GlobalVariable *global_var = nullptr;
  if (llvm::Value *global_value = emitter.GetGlobal(symbol))
  {
    global_var = llvm::dyn_cast<llvm::GlobalVariable>(global_value);
  }
  if (!global_var)
  {
    global_var = emitter.GetModule().getNamedGlobal(symbol);
  }
  const bool has_hosted_state_slot = emitter.HasHostedStateSlot(symbol);
  if (global_var && global_var->isConstant() && !has_hosted_state_slot)
  {
    return;
  }

  llvm::Value *value = EvaluateOrDefault(store.value);
  llvm::Value *target_ptr = nullptr;
  llvm::Type *target_ty = nullptr;
  analysis::TypeRef source_type =
      active_ctx ? active_ctx->LookupValueType(store.value) : nullptr;
  if (!source_type && store.value.kind == IRValue::Kind::Local)
  {
    source_type = emitter.LookupLocalType(store.value.name);
  }

  if (target_type)
  {
    if (llvm::Type *typed_target_ty = emitter.GetLLVMType(target_type))
    {
      target_ty = typed_target_ty;
      target_ptr =
          emitter.GetHostedStatePtr(symbol, typed_target_ty, global_var);
      if (!target_ptr && emitter.HasHostedStateSlot(symbol) && !global_var)
      {
        return;
      }
      if (!target_ptr && global_var)
      {
        target_ptr = builder.CreateBitCast(
            global_var, llvm::PointerType::get(typed_target_ty, 0));
      }
      llvm::Value *coerced = CoerceToTyped(
          emitter,
          &builder,
          value,
          typed_target_ty,
          source_type,
          target_type);
      if (coerced)
      {
        value = coerced;
      }
    }
  }

  if (!target_ptr)
  {
    if (!global_var)
    {
      return;
    }

    target_ptr = global_var;
    target_ty = global_var->getValueType();
  }

  if (value && value->getType() != target_ty)
  {
    if (llvm::Value *coerced = CoerceTo(&builder, value, target_ty))
    {
      value = coerced;
    }
  }

  if (!value)
  {
    value = llvm::Constant::getNullValue(target_ty);
  }

  if (!has_hosted_state_slot)
  {
    RecordGlobalStateRef("store");
  }
  RecordGlobalStoreMemoryHelper();
  llvm::StoreInst *stored = builder.CreateStore(value, target_ptr);
  RecordLowerStoreGlobal(target_type != nullptr, has_hosted_state_slot);
  llvm::Align store_align =
      global_var ? global_var->getAlign().valueOrOne() : llvm::Align(1);
  if (target_type && active_ctx)
  {
    const analysis::ScopeContext &scope = BuildScope(active_ctx);
    if (const auto align =
            ::ultraviolet::analysis::layout::AlignOf(scope, target_type))
    {
      store_align = llvm::Align(*align);
    }
  }
  stored->setAlignment(store_align);
}

} // namespace ultraviolet::codegen::emit_detail
