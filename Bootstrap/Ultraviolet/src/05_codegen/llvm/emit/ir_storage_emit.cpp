// =============================================================================
// File: 05_codegen/llvm/emit/ir_storage_emit.cpp
// Canonical owner for LLVM IR storage, flow-state, and bind lowering.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

#include <algorithm>

namespace ultraviolet::codegen {

using namespace emit_detail;

  namespace {

  bool SameStorageObject(llvm::Value *left, llvm::Value *right)
  {
    if (!left || !right)
    {
      return false;
    }
    return left->stripPointerCasts() == right->stripPointerCasts();
  }

  bool MatchesAggregateReturnLocal(const IRBindVar &bind,
                                   const IRAggregateCopyElision &info)
  {
    if (!info.return_local_uses_sret)
    {
      return false;
    }
    return bind.name == info.return_local ||
           (!info.return_local_stable_name.empty() &&
            bind.name == info.return_local_stable_name) ||
           (!bind.stable_name.empty() &&
            (bind.stable_name == info.return_local ||
             bind.stable_name == info.return_local_stable_name));
  }

  llvm::Value *SRetStorageForAggregateReturnLocal(LLVMEmitter &emitter,
                                                   llvm::Function *func,
                                                   const IRBindVar &bind,
                                                   llvm::Type *bind_ty)
  {
    if (!func || !bind_ty || func->arg_size() == 0)
    {
      return nullptr;
    }
    const LowerCtx *ctx = emitter.GetCurrentCtx();
    if (!ctx)
    {
      return nullptr;
    }
    const std::string symbol = std::string(func->getName());
    const LowerCtx::ProcSigInfo *sig = ctx->LookupProcSig(symbol);
    if (!sig || !sig->aggregate_copy_elision.has_value() ||
        !MatchesAggregateReturnLocal(bind, *sig->aggregate_copy_elision))
    {
      return nullptr;
    }
    ABICallResult abi = ComputeProcABI(emitter, symbol, sig->params, sig->ret);
    if (!abi.valid || !abi.has_sret)
    {
      return nullptr;
    }
    llvm::Value *out = func->getArg(0);
    llvm::Type *target_ptr_ty = llvm::PointerType::get(bind_ty, 0);
    if (out->getType() == target_ptr_ty)
    {
      return out;
    }
    auto *builder =
        static_cast<llvm::IRBuilder<> *>(emitter.GetBuilderRaw());
    return builder ? builder->CreateBitCast(out, target_ptr_ty) : nullptr;
  }

  } // namespace

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
      case DerivedValueInfo::Kind::AddrUnionPayload:
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
    auto &bucket = reusable_aggregate_storage_[func][ty];
    if (std::find(bucket.begin(), bucket.end(), alloca) == bucket.end()) {
      bucket.push_back(alloca);
    }
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
    auto is_current_bind_alias = [&](const std::string &name) -> bool
    {
      return name == bind.name ||
             (!bind.stable_name.empty() && name == bind.stable_name);
    };
    auto aliases_live_local_storage = [&](llvm::Value *storage) -> bool
    {
      for (const auto &[name, local_storage] : local_home_storage_)
      {
        if (is_current_bind_alias(name))
        {
          continue;
        }
        if (SameStorageObject(storage, local_storage))
        {
          return true;
        }
      }
      for (const auto &[name, local_storage] : locals_)
      {
        if (is_current_bind_alias(name))
        {
          continue;
        }
        if (SameStorageObject(storage, local_storage))
        {
          return true;
        }
      }
      return false;
    };
    analysis::TypeRef source_type = nullptr;
    if (bind.value.kind == IRValue::Kind::Local)
    {
      const auto it = local_types_.find(bind.value.name);
      if (it != local_types_.end())
      {
        source_type = it->second;
      }
    }
    if (!source_type)
    {
      if (const LowerCtx *ctx = GetCurrentCtx())
      {
        source_type = ctx->LookupValueType(bind.value);
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
    const std::string async_slot_name =
        !bind.stable_name.empty() &&
                async_state_ &&
                async_state_->info &&
                async_state_->info->slots.contains(bind.stable_name)
            ? bind.stable_name
            : bind.name;
    if (async_state_ && async_state_->info &&
        async_state_->info->slots.contains(async_slot_name))
    {
      bind_slot = GetLocal(async_slot_name);
    }

    const bool aggregate_bind_ty = ty && (ty->isStructTy() || ty->isArrayTy());
    const bool use_region_slot =
        bind_slot_info.has_value() &&
        bind_slot_info->kind == BindSlot::Kind::RegionSlot;
    if ((!bind_slot || !bind_slot->getType()->isPointerTy()) &&
        !use_region_slot &&
        aggregate_bind_ty)
    {
      bind_slot =
          SRetStorageForAggregateReturnLocal(*this, func, bind, ty);
    }
    if ((!bind_slot || !bind_slot->getType()->isPointerTy()) &&
        !use_region_slot &&
        aggregate_bind_ty)
    {
      if (bind.value.kind == IRValue::Kind::Opaque)
      {
        if (llvm::Value *existing_storage = GetTempStorage(bind.value))
        {
          bool compatible_storage = (source_llvm_ty == ty);
          auto *alloca_inst =
              llvm::dyn_cast<llvm::AllocaInst>(existing_storage->stripPointerCasts());
          if (!compatible_storage && alloca_inst)
          {
            compatible_storage = (alloca_inst->getAllocatedType() == ty);
          }
          if (compatible_storage && alloca_inst &&
              !aliases_live_local_storage(existing_storage))
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
        if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, bind_slot_info->type))
        {
          alloc_size = *size;
        }
        if (const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, bind_slot_info->type))
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
        const bool runtime_foreign_boundary = RuntimeUsesForeignABI(alloc_sym);
        const bool use_c_abi_aggregate_sret = runtime_foreign_boundary;
        if (!alloc_fn)
        {
          ABICallResult alloc_abi = ComputeCallABI(
              *this,
              alloc_info->params,
              alloc_info->ret,
              use_c_abi_aggregate_sret,
              /*foreign_boundary_mode_independent=*/false);
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
              use_c_abi_aggregate_sret,
              /*ffi_import_boundary=*/false,
              /*ffi_import_catch=*/false,
              std::nullopt,
              nullptr,
              nullptr,
              nullptr,
              /*foreign_boundary_mode_independent=*/false);
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
      if (const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, bind.type); align.has_value())
      {
        if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(bind_slot))
        {
          alloca_inst->setAlignment(llvm::Align(std::max<std::uint64_t>(1, *align)));
        }
      }
    }

    bool copied_from_storage = false;
    if (!adopted_existing_storage)
    {
      llvm::Value *source_storage = GetAddressableStorage(bind.value);
      if (source_storage)
      {
        const BindingState *target_state =
            current_ctx_ ? current_ctx_->GetBindingState(bind.name) : nullptr;
        const bool source_is_temp_storage =
            bind.value.kind == IRValue::Kind::Opaque &&
            GetTempStorage(bind.value) == source_storage;
        copied_from_storage = TryEmitBitcopyAggregateStorageCopy(
            *this,
            builder,
            bind_slot,
            source_storage,
            bind.type ? bind.type : source_type,
            source_type);
        if (!copied_from_storage &&
            ((target_state && !target_state->has_responsibility) ||
             source_is_temp_storage))
        {
          copied_from_storage = TryEmitAggregateStorageTransfer(
              *this,
              builder,
              bind_slot,
              source_storage,
              bind.type ? bind.type : source_type,
              source_type);
        }
      }
      else
      {
        copied_from_storage = TryEmitDerivedAggregateToStorage(
            *this,
            builder,
            bind_slot,
            bind.value,
            bind.type ? bind.type : source_type);
      }
    }

    if (!adopted_existing_storage && !copied_from_storage)
    {
      init_val = EvaluateIRValue(bind.value);
    }

    // Store the initial value
    if (!adopted_existing_storage && !copied_from_storage && !init_val)
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
    else if (!adopted_existing_storage && !copied_from_storage)
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
    if (!adopted_existing_storage && !copied_from_storage)
    {
      llvm::Value *typed_slot = bind_slot;
      llvm::Type *slot_ptr_ty = llvm::PointerType::get(ty, 0);
      if (typed_slot->getType() != slot_ptr_ty)
      {
        typed_slot = builder->CreateBitCast(typed_slot, slot_ptr_ty);
      }
      builder->CreateStore(init_val, typed_slot);
    }
    else if (adopted_existing_storage)
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

} // namespace ultraviolet::codegen
