// =============================================================================
// File: 05_codegen/llvm/emit/ir_storage_emit.cpp
// Canonical owner for LLVM IR storage, flow-state, and bind lowering.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

namespace cursive::codegen {

using namespace emit_detail;
  llvm::Value *LLVMEmitter::GetAddressableStorage(const IRValue &value)
  {
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    if (!builder)
    {
      return nullptr;
    }

    switch (value.kind)
    {
    case IRValue::Kind::Local:
    {
      llvm::Value *local = GetLocalBindStorage(value.name);
      return (local && local->getType()->isPointerTy()) ? local : nullptr;
    }
    case IRValue::Kind::Symbol:
    {
      if (llvm::Value *global = GetGlobal(value.name))
      {
        return global->getType()->isPointerTy() ? global : nullptr;
      }
      if (llvm::GlobalVariable *global = module_->getNamedGlobal(value.name))
      {
        return global;
      }
      return nullptr;
    }
    case IRValue::Kind::Opaque:
    {
      if (llvm::Value *storage = GetTempStorage(value))
      {
        return storage;
      }
      const LowerCtx *ctx = GetCurrentCtx();
      if (!ctx)
      {
        return nullptr;
      }
      const DerivedValueInfo *derived = ctx->LookupDerivedValue(value);
      if (!derived)
      {
        return nullptr;
      }
      switch (derived->kind)
      {
      case DerivedValueInfo::Kind::AddrLocal:
      case DerivedValueInfo::Kind::AddrStatic:
      case DerivedValueInfo::Kind::AddrField:
      case DerivedValueInfo::Kind::AddrTuple:
      case DerivedValueInfo::Kind::AddrIndex:
      case DerivedValueInfo::Kind::AddrDeref:
      {
        llvm::Value *addr = EvaluateIRValue(value);
        return (addr && addr->getType()->isPointerTy()) ? addr : nullptr;
      }
      case DerivedValueInfo::Kind::LoadFromAddr:
      {
        llvm::Value *base = EvaluateIRValue(derived->base);
        if (!base)
        {
          return nullptr;
        }
        analysis::TypeRef value_type =
            ctx ? ctx->LookupValueType(value) : nullptr;
        llvm::Type *value_llvm_ty =
            value_type ? GetLLVMType(value_type) : nullptr;
        llvm::Type *target_ptr_ty =
            value_llvm_ty ? llvm::PointerType::get(value_llvm_ty, 0) : nullptr;
        if (base->getType()->isIntegerTy() && target_ptr_ty)
        {
          return builder->CreateIntToPtr(base, target_ptr_ty);
        }
        if (!base->getType()->isPointerTy())
        {
          return nullptr;
        }
        if (target_ptr_ty && base->getType() != target_ptr_ty)
        {
          return builder->CreateBitCast(base, target_ptr_ty);
        }
        return base;
      }
      default:
        return nullptr;
      }
    }
    default:
      return nullptr;
    }
  }

  LLVMEmitter::FlowStateSnapshot LLVMEmitter::SaveFlowState() const
  {
    FlowStateSnapshot snapshot;
    snapshot.locals = locals_;
    snapshot.local_home_storage = local_home_storage_;
    snapshot.local_types = local_types_;
    snapshot.values = values_;
    snapshot.storage_values = storage_values_;
    snapshot.preferred_result_storage = preferred_result_storage_;
    snapshot.reusable_aggregate_storage = reusable_aggregate_storage_;
    return snapshot;
  }

  void LLVMEmitter::RestoreFlowState(const FlowStateSnapshot &snapshot)
  {
    const auto persistent_home_storage = local_home_storage_;
    const auto persistent_local_types = local_types_;
    locals_ = snapshot.locals;
    local_home_storage_ = snapshot.local_home_storage;
    local_types_ = snapshot.local_types;
    values_ = snapshot.values;
    storage_values_ = snapshot.storage_values;
    preferred_result_storage_ = snapshot.preferred_result_storage;
    reusable_aggregate_storage_ = snapshot.reusable_aggregate_storage;
    for (const auto &[name, storage] : persistent_home_storage)
    {
      if (storage && !local_home_storage_.contains(name))
      {
        local_home_storage_[name] = storage;
      }
    }
    for (const auto &[name, type] : persistent_local_types)
    {
      if (type && !local_types_.contains(name))
      {
        local_types_[name] = type;
      }
    }
  }

  llvm::AllocaInst *LLVMEmitter::AcquireReusableAggregateStorage(
      llvm::Function *func,
      llvm::Type *ty,
      std::string_view name)
  {
    if (!func || !ty) {
      return nullptr;
    }
    if (!ty->isStructTy() && !ty->isArrayTy()) {
      return nullptr;
    }

    auto func_it = reusable_aggregate_storage_.find(func);
    if (func_it != reusable_aggregate_storage_.end()) {
      auto type_it = func_it->second.find(ty);
      if (type_it != func_it->second.end() && !type_it->second.empty()) {
        llvm::AllocaInst *slot = type_it->second.back();
        type_it->second.pop_back();
        return slot;
      }
    }

    return CreateEntryAlloca(func, ty, std::string(name));
  }

  void LLVMEmitter::ReleaseReusableAggregateStorage(llvm::Value *storage)
  {
    auto *alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(storage);
    if (!alloca) {
      return;
    }
    llvm::Type *ty = alloca->getAllocatedType();
    if (!ty || (!ty->isStructTy() && !ty->isArrayTy())) {
      return;
    }
    llvm::Function *func = alloca->getFunction();
    if (!func) {
      return;
    }
    reusable_aggregate_storage_[func][ty].push_back(alloca);
  }

  void LLVMEmitter::ForgetTempStorage(const IRValue &value)
  {
    if (value.kind != IRValue::Kind::Opaque) {
      return;
    }
    storage_values_.erase(value.name);
    values_.erase(value.name);
  }

  void LLVMEmitter::ReleaseTempStorage(const IRValue &value)
  {
    if (value.kind != IRValue::Kind::Opaque) {
      return;
    }
    auto it = storage_values_.find(value.name);
    if (it == storage_values_.end()) {
      return;
    }
    ReleaseReusableAggregateStorage(it->second);
    storage_values_.erase(it);
    values_.erase(value.name);
  }

  void LLVMEmitter::ReleaseMoveConsumedStorage(const IRValue &value)
  {
    if (value.kind == IRValue::Kind::Opaque)
    {
      ReleaseTempStorage(value);
      return;
    }

    if (value.kind != IRValue::Kind::Local)
    {
      return;
    }

    auto local_it = locals_.find(value.name);
    if (local_it == locals_.end())
    {
      return;
    }

    llvm::Value *storage = local_it->second;
    auto *alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(storage);
    if (!alloca)
    {
      return;
    }

    llvm::Type *ty = alloca->getAllocatedType();
    if (!ty || (!ty->isStructTy() && !ty->isArrayTy()))
    {
      return;
    }

    if (GetLocalHomeStorage(value.name) != storage)
    {
      SetLocalHomeStorage(value.name, storage);
    }
    locals_.erase(local_it);
  }

  void LLVMEmitter::RegisterLocalBindStorage(const std::string &name, llvm::Value *val)
  {
    SetLocal(name, val);
    if (val && val->getType()->isPointerTy())
    {
      SetLocalHomeStorage(name, val);
    }
  }

  llvm::Value *LLVMEmitter::GetLocalBindStorage(const std::string &name)
  {
    llvm::Value *local = GetLocal(name);
    if (local && local->getType()->isPointerTy())
    {
      if (GetLocalHomeStorage(name) != local)
      {
        SetLocalHomeStorage(name, local);
      }
      return local;
    }
    return GetLocalHomeStorage(name);
  }

  // T-LLVM-010: Bind local variable
  void LLVMEmitter::EmitBindVar(const IRBindVar &bind)
  {
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    const bool debug_parallel_bind =
        core::IsDebugEnabled("parallel");
    const bool log_this_bind =
        debug_parallel_bind &&
        (bind.name.find("parallel_") != std::string::npos ||
         bind.name.find("spawn_") != std::string::npos ||
         bind.name.find("wait_result") != std::string::npos);
    llvm::Value *init_val = nullptr;
    llvm::Type *ty = nullptr;
    if (bind.type)
    {
      ty = GetLLVMType(bind.type);
    }
    if ((!ty || ty->isVoidTy()) && init_val)
    {
      ty = init_val->getType();
    }
    if (bind.type && init_val)
    {
      analysis::TypeRef stripped = analysis::StripPerm(bind.type);
      if (!stripped)
      {
        stripped = bind.type;
      }
      if (stripped &&
          std::holds_alternative<analysis::TypeFunc>(stripped->node) &&
          IsClosurePairLLVMType(init_val->getType()))
      {
        // Non-capturing closures are represented as (env_ptr, code_ptr) pairs
        // during lowering. Preserve the concrete closure value representation
        // even when the source-level binding is annotated as TypeFunc.
        ty = init_val->getType();
      }
    }
    if (!ty || ty->isVoidTy())
    {
      ty = llvm::Type::getInt64Ty(context_);
    }

    llvm::Function *func = builder->GetInsertBlock()->getParent();
    llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
    llvm::Value *bind_slot = nullptr;
    bool adopted_existing_storage = false;
    analysis::TypeRef source_type = nullptr;
    if (const LowerCtx *ctx = GetCurrentCtx())
    {
      source_type = ctx->LookupValueType(bind.value);
    }
    if (!source_type && bind.value.kind == IRValue::Kind::Local)
    {
      const auto it = local_types_.find(bind.value.name);
      if (it != local_types_.end())
      {
        source_type = it->second;
      }
    }
    llvm::Type *source_llvm_ty = source_type ? GetLLVMType(source_type) : nullptr;
    IRBindVar bind_for_slot = bind;
    if (!bind_for_slot.type && source_type)
    {
      bind_for_slot.type = source_type;
    }
    std::optional<BindSlot> bind_slot_info;
    if (current_ctx_)
    {
      bind_slot_info = ResolveBindSlot(bind_for_slot, *current_ctx_);
      if (!bind_slot_info.has_value())
      {
        if (!ty || ty->isVoidTy())
        {
          current_ctx_->ReportCodegenFailure();
          return;
        }
        BindSlot fallback_slot;
        fallback_slot.kind = BindSlot::Kind::Alloca;
        fallback_slot.name = bind.name;
        fallback_slot.type = bind_for_slot.type;
        bind_slot_info = std::move(fallback_slot);
      }
    }
    if (async_state_ && async_state_->info &&
        async_state_->info->slots.contains(bind.name))
    {
      bind_slot = GetLocal(bind.name);
    }

    const bool aggregate_bind_ty = ty && (ty->isStructTy() || ty->isArrayTy());
    const bool use_region_slot =
        bind_slot_info.has_value() &&
        bind_slot_info->kind == BindSlot::Kind::RegionSlot;
    if ((!bind_slot || !bind_slot->getType()->isPointerTy()) &&
        !use_region_slot &&
        aggregate_bind_ty)
    {
      if (llvm::Value *existing_storage = GetAddressableStorage(bind.value))
      {
        bool compatible_storage = (source_llvm_ty == ty);
        if (!compatible_storage)
        {
          if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(existing_storage))
          {
            compatible_storage = (alloca_inst->getAllocatedType() == ty);
          }
        }
        if (compatible_storage)
        {
          llvm::Type *slot_ptr_ty = llvm::PointerType::get(ty, 0);
          if (existing_storage->getType() != slot_ptr_ty)
          {
            existing_storage = builder->CreateBitCast(existing_storage, slot_ptr_ty);
          }
          bind_slot = existing_storage;
          adopted_existing_storage = true;
        }
      }
    }
    if ((!bind_slot || !bind_slot->getType()->isPointerTy()) && use_region_slot)
    {
      IRValue region_local;
      region_local.kind = IRValue::Kind::Local;
      region_local.name = bind_slot_info->region;
      llvm::Value *region_value = EvaluateIRValue(region_local);
      if (!region_value)
      {
        if (current_ctx_)
        {
          SPEC_RULE("BindSlot-Err");
          current_ctx_->ReportCodegenFailure();
        }
        return;
      }

      std::uint64_t alloc_size = 0;
      std::uint64_t alloc_align = 1;
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
      if (bind_slot_info->type)
      {
        if (const auto size = ::cursive::analysis::layout::SizeOf(scope, bind_slot_info->type))
        {
          alloc_size = *size;
        }
        if (const auto align = ::cursive::analysis::layout::AlignOf(scope, bind_slot_info->type))
        {
          alloc_align = *align;
        }
      }
      const llvm::DataLayout &dl = GetModule().getDataLayout();
      if (alloc_size == 0 && !ty->isVoidTy())
      {
        alloc_size = static_cast<std::uint64_t>(dl.getTypeAllocSize(ty));
      }
      if (alloc_align == 0)
      {
        alloc_align = 1;
      }
      if (alloc_align == 1 && !ty->isVoidTy())
      {
        alloc_align = std::max<std::uint64_t>(
            alloc_align,
            static_cast<std::uint64_t>(dl.getABITypeAlign(ty).value()));
      }

      llvm::Value *raw_ptr = nullptr;
      const std::string alloc_sym = BuiltinModalSymRegionAlloc();
      if (std::optional<RuntimeFuncInfo> alloc_info = GetRuntimeFuncInfo(alloc_sym))
      {
        llvm::Function *alloc_fn = GetModule().getFunction(alloc_sym);
        const bool use_c_abi_aggregate_sret = true;
        if (!alloc_fn)
        {
          ABICallResult alloc_abi = ComputeCallABI(
              *this,
              alloc_info->params,
              alloc_info->ret,
              use_c_abi_aggregate_sret);
          if (alloc_abi.func_type)
          {
            alloc_fn = llvm::Function::Create(
                alloc_abi.func_type,
                llvm::GlobalValue::ExternalLinkage,
                alloc_sym,
                &GetModule());
            alloc_fn->setCallingConv(llvm::CallingConv::C);
          }
        }
        if (alloc_fn)
        {
          llvm::Type *usize_ty = llvm::Type::getInt64Ty(GetContext());
          std::vector<llvm::Value *> alloc_args;
          alloc_args.reserve(3);
          alloc_args.push_back(region_value);
          alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_size));
          alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_align));
          raw_ptr = EmitABICall(
              *this,
              builder,
              alloc_fn,
              alloc_info->params,
              alloc_info->ret,
              alloc_args,
              use_c_abi_aggregate_sret);
        }
      }

      if (!raw_ptr)
      {
        if (current_ctx_)
        {
          SPEC_RULE("BindSlot-Err");
          current_ctx_->ReportCodegenFailure();
        }
        return;
      }

      bind_slot = builder->CreateBitCast(raw_ptr, llvm::PointerType::get(ty, 0));
    }
    if (!bind_slot || !bind_slot->getType()->isPointerTy())
    {
      bind_slot = entry_builder.CreateAlloca(ty, nullptr, bind.name);
    }
    if (bind.type)
    {
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
      if (const auto align = ::cursive::analysis::layout::AlignOf(scope, bind.type); align.has_value())
      {
        if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(bind_slot))
        {
          alloca_inst->setAlignment(llvm::Align(std::max<std::uint64_t>(1, *align)));
        }
      }
    }

    if (!adopted_existing_storage)
    {
      init_val = EvaluateIRValue(bind.value);
    }

    // Store the initial value
    if (!adopted_existing_storage && !init_val)
    {
      init_val = llvm::Constant::getNullValue(ty);
      if (log_this_bind)
      {
        std::fprintf(stderr,
                     "[bind-debug] name=%s init=default-null target=%s\n",
                     bind.name.c_str(),
                     ty->isIntegerTy() ? "int" : ty->isPointerTy() ? "ptr"
                                             : ty->isStructTy()    ? "struct"
                                             : ty->isArrayTy()     ? "array"
                                                                   : "other");
      }
    }
    else if (!adopted_existing_storage)
    {
      if (log_this_bind)
      {
        std::fprintf(stderr,
                     "[bind-debug] name=%s before-coerce source=%s init=%s target=%s\n",
                     bind.name.c_str(),
                     source_type ? analysis::TypeToString(source_type).c_str() : "<null>",
                     init_val->getType()->isIntegerTy() ? "int" : init_val->getType()->isPointerTy() ? "ptr"
                                                              : init_val->getType()->isStructTy()    ? "struct"
                                                              : init_val->getType()->isArrayTy()     ? "array"
                                                                                                     : "other",
                     ty->isIntegerTy() ? "int" : ty->isPointerTy() ? "ptr"
                                             : ty->isStructTy()    ? "struct"
                                             : ty->isArrayTy()     ? "array"
                                                                   : "other");
      }
      if (UnionDebugEnabled())
      {
        analysis::TypeRef target_type = StripPermType(bind.type);
        const bool target_is_union =
            target_type && std::holds_alternative<analysis::TypeUnion>(target_type->node);
        if (target_is_union)
        {
          std::cerr << "[union-debug-bind] name=" << bind.name
                    << " source_kind=" << static_cast<int>(bind.value.kind)
                    << " source_type=" << (source_type ? "known" : "unknown")
                    << " source_llvm="
                    << (init_val->getType()->isIntegerTy() ? "int" : init_val->getType()->isStructTy() ? "struct"
                                                                 : init_val->getType()->isPointerTy()  ? "ptr"
                                                                                                       : "other")
                    << "\n";
        }
      }
      llvm::Value *coerced_init =
          CoerceToTyped(*this, builder, init_val, ty, source_type, bind.type);
      if (!coerced_init)
      {
        if (init_val->getType() == ty)
        {
          coerced_init = init_val;
        }
        else if (llvm::Value *plain = CoerceTo(builder, init_val, ty))
        {
          coerced_init = plain;
        }
      }
      init_val = coerced_init;
      if (!init_val)
      {
        init_val = llvm::Constant::getNullValue(ty);
        if (log_this_bind)
        {
          std::fprintf(stderr,
                       "[bind-debug] name=%s coerce=null-fallback\n",
                       bind.name.c_str());
        }
      }
      else if (log_this_bind)
      {
        std::fprintf(stderr,
                     "[bind-debug] name=%s after-coerce out=%s\n",
                     bind.name.c_str(),
                     init_val->getType()->isIntegerTy() ? "int" : init_val->getType()->isPointerTy() ? "ptr"
                                                              : init_val->getType()->isStructTy()    ? "struct"
                                                              : init_val->getType()->isArrayTy()     ? "array"
                                                                                                     : "other");
      }
    }
    if (!adopted_existing_storage)
    {
      llvm::Value *typed_slot = bind_slot;
      llvm::Type *slot_ptr_ty = llvm::PointerType::get(ty, 0);
      if (typed_slot->getType() != slot_ptr_ty)
      {
        typed_slot = builder->CreateBitCast(typed_slot, slot_ptr_ty);
      }
      builder->CreateStore(init_val, typed_slot);
    }
    else
    {
      storage_values_.erase(bind.value.name);
      values_.erase(bind.value.name);
    }

    if (!adopted_existing_storage)
    {
      ReleaseTempStorage(bind.value);
    }

    RegisterLocalBindStorage(bind.name, bind_slot);
    if (bind.type)
    {
      local_types_[bind.name] = bind.type;
    }
    if (!bind.stable_name.empty() && bind.stable_name != bind.name)
    {
      RegisterLocalBindStorage(bind.stable_name, bind_slot);
      if (bind.type)
      {
        local_types_[bind.stable_name] = bind.type;
      }
    }
  }

} // namespace cursive::codegen
