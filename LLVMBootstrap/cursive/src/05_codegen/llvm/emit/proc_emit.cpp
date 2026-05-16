// =============================================================================
// File: 05_codegen/llvm/emit/proc_emit.cpp
// Canonical owner for the moved LLVM emitter implementation slice.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

namespace cursive::codegen {

using namespace emit_detail;

  void LLVMEmitter::EmitProc(const ProcIR &proc)
  {
    const bool generated_proc = emit_detail::IsGeneratedProcSymbol(proc.symbol);
    if (generated_proc)
    {
      SPEC_RULE("LowerIRDecl-Proc-Gen");
    }
    else
    {
      SPEC_RULE("LowerIRDecl-Proc-User");
    }

    llvm::Function *func = functions_[proc.symbol];
    if (!func)
    {
      return;
    }

    ProcModuleContextScope proc_module_scope(
        current_ctx_, proc.defining_module_path);

    using Clock = std::chrono::steady_clock;
    const bool perf_enabled = EmitPerfLoggingEnabled();
    const bool log_all_procs = perf_enabled && EmitPerfLogAllProcs();
    const long long slow_proc_threshold_ms =
        perf_enabled ? EmitPerfSlowProcThresholdMs() : 0;
    const std::string perf_module_label =
        (perf_enabled && current_ctx_) ? ModulePerfLabel(*current_ctx_)
                                       : std::string();
    const auto proc_start = perf_enabled ? Clock::now() : Clock::time_point{};
    auto phase_start = proc_start;

    long long state_reset_ms = 0;
    long long prologue_ms = 0;
    long long abi_ms = 0;
    long long bind_params_ms = 0;
    long long panic_slot_ms = 0;
    long long async_setup_ms = 0;
    long long body_emit_ms = 0;
    long long async_clear_ms = 0;
    long long terminator_fix_ms = 0;
    long long final_cleanup_ms = 0;
    long long ir_self_total_ms = 0;

    std::size_t bound_params = 0;
    std::size_t bound_params_by_ref = 0;
    std::size_t bound_params_by_value = 0;
    std::size_t async_slots_typed = 0;
    std::size_t async_slots_restored = 0;
    std::size_t inserted_terminators = 0;
    bool panic_slot_materialized = false;
    bool async_resume_mode = false;
    IRProcPerfContext ir_proc_perf;
    IRProcPerfContext *prior_ir_ctx = nullptr;

    ClearLocals();
    ClearTempValues();
    ClearSymbolAliases();
    active_regions_.clear();
    hosted_env_value_ = nullptr;
    if (perf_enabled)
    {
      const auto now = Clock::now();
      state_reset_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", func);
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    builder->SetInsertPoint(entry);
    if (perf_enabled)
    {
      const auto now = Clock::now();
      prologue_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    const bool has_explicit_hosted_env = HasLeadingHostedEnvParam(proc.params);
    std::vector<IRParam> abi_params =
        BuildProcABIParams(*this, proc.symbol, proc.params);
    const bool has_abi_hosted_env =
        !abi_params.empty() && abi_params.front().name == std::string(kHostedEnvParamName);
    const bool needs_implicit_hosted_env =
        has_abi_hosted_env && !has_explicit_hosted_env;
    ABICallResult abi = ComputeProcABI(*this, proc.symbol, proc.params, proc.ret);
    if (!abi.valid || !abi.func_type)
    {
      if (builder && builder->GetInsertBlock() &&
          !builder->GetInsertBlock()->getTerminator())
      {
        builder->CreateUnreachable();
      }
      return;
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      abi_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    if (needs_implicit_hosted_env && !abi.param_indices.empty() &&
        abi.param_indices[0].has_value())
    {
      const unsigned idx = *abi.param_indices[0];
      if (idx < func->arg_size())
      {
        llvm::Argument *arg = func->getArg(idx);
        arg->setName(kHostedEnvParamName);
        llvm::Value *env_value = arg;
        if (llvm::Type *env_ty = GetLLVMType(HostedEnvParamType()))
        {
          if (env_value->getType() != env_ty)
          {
            if (llvm::Value *coerced = CoerceTo(builder, env_value, env_ty))
            {
              env_value = coerced;
            }
          }
        }
        hosted_env_value_ = env_value;
        RegisterLocalBindStorage(std::string(kHostedEnvParamName), env_value);
        local_types_[std::string(kHostedEnvParamName)] = HostedEnvParamType();
      }
    }

    const auto &param_scope = BuildScope(current_ctx_);

    auto bind_zero_sized_param = [&](const IRParam &param) -> bool
    {
      if (!param.type)
      {
        return false;
      }

      const auto size = ::cursive::analysis::layout::SizeOf(param_scope, param.type);
      if (!size.has_value() || *size != 0)
      {
        return false;
      }

      llvm::Type *llvm_ty = GetLLVMType(param.type);
      if (!llvm_ty || llvm_ty->isVoidTy())
      {
        return false;
      }

      SPEC_RULE("BindSlot-Param-ByValue");
      llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
      llvm::AllocaInst *alloca = entry_builder.CreateAlloca(llvm_ty, nullptr, param.name);
      builder->CreateStore(llvm::Constant::getNullValue(llvm_ty), alloca);
      RegisterLocalBindStorage(param.name, alloca);
      local_types_[param.name] = param.type;
      const std::string stable_name =
          param.stable_name.empty() ? param.name : param.stable_name;
      if (stable_name != param.name) {
        RegisterLocalBindStorage(stable_name, alloca);
        if (param.type) {
          local_types_[stable_name] = param.type;
        }
      }
      ++bound_params;
      ++bound_params_by_value;
      return true;
    };

    SPEC_RULE("ParamInitIR");
    // Map parameters into locals
    for (std::size_t i = 0; i < abi_params.size(); ++i)
    {
      const IRParam &param = abi_params[i];
      const std::size_t abi_index = i;
      if (abi_index >= abi.param_indices.size())
      {
        bind_zero_sized_param(param);
        continue;
      }
      if (!abi.param_indices[abi_index].has_value())
      {
        bind_zero_sized_param(param);
        continue;
      }
      unsigned idx = *abi.param_indices[abi_index];
      if (idx >= func->arg_size())
      {
        bind_zero_sized_param(param);
        continue;
      }
      llvm::Argument *arg = func->getArg(idx);
      arg->setName(param.name);

      auto register_explicit_hosted_env = [&](llvm::Value *env_value) {
        if (param.name != std::string(kHostedEnvParamName) || !env_value)
        {
          return;
        }
        if (llvm::Type *env_ty = GetLLVMType(HostedEnvParamType()))
        {
          if (env_value->getType() != env_ty)
          {
            if (llvm::Value *coerced = CoerceTo(builder, env_value, env_ty))
            {
              env_value = coerced;
            }
          }
        }
        hosted_env_value_ = env_value;
      };

      const ABIArgCarrierKind carrier =
          abi_index < abi.param_carriers.size() ? abi.param_carriers[abi_index]
                                                : ABIArgCarrierKind::Direct;

      if (abi_index < abi.param_kinds.size() &&
          abi.param_kinds[abi_index] == PassKind::ByRef)
      {
        SPEC_RULE("BindSlot-Param-ByRef");
        llvm::Type *llvm_ty = GetLLVMType(param.type);
        llvm::Value *typed_ptr = arg;
        if (typed_ptr && typed_ptr->getType()->isPointerTy() && llvm_ty)
        {
          typed_ptr =
              builder->CreateBitCast(typed_ptr, llvm::PointerType::get(llvm_ty, 0));
        }
        RegisterLocalBindStorage(param.name, typed_ptr);
        if (param.type)
        {
          local_types_[param.name] = param.type;
        }
        const std::string stable_name =
            param.stable_name.empty()
                ? param.name
                : param.stable_name;
        if (stable_name != param.name) {
          RegisterLocalBindStorage(stable_name, typed_ptr);
          if (param.type) {
            local_types_[stable_name] = param.type;
          }
        }
        register_explicit_hosted_env(typed_ptr);
        ++bound_params;
        ++bound_params_by_ref;
        continue;
      }

      SPEC_RULE("BindSlot-Param-ByValue");
      llvm::Type *llvm_ty = GetLLVMType(param.type);
      llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
      llvm::AllocaInst *alloca = entry_builder.CreateAlloca(llvm_ty, nullptr, param.name);
      llvm::Value *stored_value = arg;
      if (carrier == ABIArgCarrierKind::Indirect &&
          arg && arg->getType()->isPointerTy() && llvm_ty)
      {
        llvm::Value *typed_ptr = arg;
        llvm::Type *expected_ptr_ty = llvm::PointerType::get(llvm_ty, 0);
        if (typed_ptr->getType() != expected_ptr_ty)
        {
          typed_ptr = builder->CreateBitCast(typed_ptr, expected_ptr_ty);
        }
        stored_value = builder->CreateLoad(llvm_ty, typed_ptr);
      }
      if (stored_value && llvm_ty && stored_value->getType() != llvm_ty)
      {
        if (llvm::Value *coerced = CoerceTo(builder, stored_value, llvm_ty))
        {
          stored_value = coerced;
        }
      }
      builder->CreateStore(stored_value, alloca);
      RegisterLocalBindStorage(param.name, alloca);
      if (param.type)
      {
        local_types_[param.name] = param.type;
      }
      const std::string stable_name =
          param.stable_name.empty()
              ? param.name
              : param.stable_name;
      if (stable_name != param.name) {
        RegisterLocalBindStorage(stable_name, alloca);
        if (param.type) {
          local_types_[stable_name] = param.type;
        }
      }
      register_explicit_hosted_env(arg);
      ++bound_params;
      ++bound_params_by_value;
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      bind_params_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    // Some procedures (for example exported ABI-entry procedures) intentionally
    // omit the hidden panic out-parameter. Cleanup/panic IR still refers to the
    // canonical local name, so materialize a local panic slot when absent.
    if (!GetLocal(std::string(kPanicOutName)))
    {
      llvm::Type *panic_record_ty = GetLLVMType(PanicRecordType());
      llvm::Type *panic_ptr_ty = GetLLVMType(PanicOutType());
      if (panic_record_ty && panic_ptr_ty && panic_ptr_ty->isPointerTy())
      {
        llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
        llvm::AllocaInst *panic_out_alloca =
            entry_builder.CreateAlloca(panic_ptr_ty, nullptr, std::string(kPanicOutName));
        llvm::Value *panic_record_ptr = nullptr;
        if (current_ctx_ && current_ctx_->shared_library_project)
        {
          panic_record_ptr = GetSharedLibraryImagePanicPtr();
          if (IsHostedLibraryBuild())
          {
            panic_record_ptr = GetHostedSessionPanicPtr(panic_record_ptr);
          }
          if (panic_record_ptr)
          {
            llvm::Value *typed_panic_ptr =
                CoerceTo(builder,
                         panic_record_ptr,
                         llvm::PointerType::get(panic_record_ty, 0));
            if (!typed_panic_ptr && panic_record_ptr->getType()->isPointerTy())
            {
              typed_panic_ptr =
                  builder->CreateBitCast(panic_record_ptr,
                                         llvm::PointerType::get(panic_record_ty, 0));
            }
            if (typed_panic_ptr)
            {
              builder->CreateStore(llvm::Constant::getNullValue(panic_record_ty),
                                   typed_panic_ptr);
              panic_record_ptr = typed_panic_ptr;
            }
          }
        }
        if (!panic_record_ptr)
        {
          llvm::AllocaInst *panic_record_alloca =
              entry_builder.CreateAlloca(panic_record_ty, nullptr, "__c0_panic_record");
          builder->CreateStore(llvm::Constant::getNullValue(panic_record_ty),
                               panic_record_alloca);
          panic_record_ptr =
              IsHostedLibraryBuild()
                  ? GetHostedSessionPanicPtr(panic_record_alloca)
                  : static_cast<llvm::Value *>(panic_record_alloca);
        }
        if (panic_record_ptr->getType() != panic_ptr_ty)
        {
          panic_record_ptr = builder->CreateBitCast(panic_record_ptr, panic_ptr_ty);
        }
        builder->CreateStore(panic_record_ptr, panic_out_alloca);
        RegisterLocalBindStorage(std::string(kPanicOutName), panic_out_alloca);
        local_types_[std::string(kPanicOutName)] = PanicOutType();
        panic_slot_materialized = true;
      }
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      panic_slot_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    if (!generated_proc && !proc.defining_module_path.empty())
    {
      SPEC_RULE("LowerIRInstr-CheckPoison");
      EmitPoisonCheck(core::StringOfPath(proc.defining_module_path));
    }

    const bool needs_entry_panic_clear =
        current_ctx_ ? current_ctx_->NeedsPanicOutForSymbol(proc.symbol)
                     : NeedsPanicOut(proc.symbol);
    if (needs_entry_panic_clear)
    {
      SPEC_RULE("ClearPanic");
      ClearPanicRecord(*this, builder);
    }

    AsyncEmitState async_state_storage;
    bool async_state_active = false;
    const LowerCtx::AsyncProcInfo *async_info =
        current_ctx_ ? current_ctx_->LookupAsyncProc(proc.symbol) : nullptr;
    if (async_info)
    {
      async_state_storage.info = async_info;
      async_state_active = true;
      async_resume_mode = async_info->is_resume;
      async_state_storage.emitting_resume_prelude = async_info->is_resume;

      llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
      for (const auto &slot_name : async_info->slot_order)
      {
        const auto slot_it = async_info->slots.find(slot_name);
        if (slot_it == async_info->slots.end())
        {
          continue;
        }
        const auto &slot = slot_it->second;
        llvm::Type *slot_ty = GetLLVMType(slot.type);
        if (!slot_ty || slot_ty->isVoidTy())
        {
          continue;
        }
        ++async_slots_typed;

        llvm::Value *local_slot = GetLocal(slot_name);
        if (!local_slot)
        {
          local_slot = entry_builder.CreateAlloca(slot_ty, nullptr, slot_name);
          RegisterLocalBindStorage(slot_name, local_slot);
        }
        SetLocalType(slot_name, slot.type);
        if (const auto alias_it = async_info->slot_aliases.find(slot_name);
            alias_it != async_info->slot_aliases.end())
        {
          for (const auto &alias : alias_it->second)
          {
            if (alias.empty() || alias == slot_name)
            {
              continue;
            }
            RegisterLocalBindStorage(alias, local_slot);
            SetLocalType(alias, slot.type);
          }
        }
      }

      if (async_info->is_resume)
      {
        async_state_storage.frame_ptr = LoadLocalValue(*this, builder, "__c0_async_frame");
        async_state_storage.input_ptr = LoadLocalValue(*this, builder, "__c0_async_input");

        if (async_state_storage.frame_ptr)
        {
          for (const auto &slot_name : async_info->slot_order)
          {
            const auto slot_it = async_info->slots.find(slot_name);
            if (slot_it == async_info->slots.end())
            {
              continue;
            }
            const auto &slot = slot_it->second;
            llvm::Value *local_slot = GetLocal(slot_name);
            if (!local_slot || !local_slot->getType()->isPointerTy())
            {
              continue;
            }
            llvm::Type *slot_ty = GetLLVMType(slot.type);
            if (!slot_ty || slot_ty->isVoidTy())
            {
              continue;
            }
            llvm::Value *frame_slot_ptr = AsyncFrameTypedPtr(
                *this,
                builder,
                async_state_storage.frame_ptr,
                slot.offset,
                slot_ty);
            if (!frame_slot_ptr)
            {
              continue;
            }
            llvm::LoadInst *loaded = builder->CreateLoad(slot_ty, frame_slot_ptr);
            loaded->setAlignment(llvm::Align(std::max<std::uint64_t>(1, slot.align)));
            ++async_slots_restored;
            llvm::Value *target_ptr = local_slot;
            llvm::Type *expected_ptr_ty = llvm::PointerType::get(slot_ty, 0);
            if (target_ptr->getType() != expected_ptr_ty)
            {
              target_ptr = builder->CreateBitCast(target_ptr, expected_ptr_ty);
            }
            builder->CreateStore(loaded, target_ptr);
          }

          llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
          llvm::Value *resume_state_ptr = AsyncFrameTypedPtr(
              *this,
              builder,
              async_state_storage.frame_ptr,
              kAsyncFrameResumeStateOffset,
              i64_ty);
          llvm::Value *resume_state = nullptr;
          if (resume_state_ptr)
          {
            resume_state = builder->CreateLoad(i64_ty, resume_state_ptr);
          }
          else
          {
            resume_state = llvm::ConstantInt::get(i64_ty, 0);
          }
          llvm::BasicBlock *invalid_resume_bb =
              llvm::BasicBlock::Create(context_, "async.resume.invalid", func);
          llvm::BasicBlock *start_bb =
              llvm::BasicBlock::Create(context_, "async.resume.start", func);
          async_state_storage.resume_switch =
              builder->CreateSwitch(resume_state, invalid_resume_bb);
          builder->SetInsertPoint(invalid_resume_bb);
          if (func->getReturnType()->isVoidTy())
          {
            builder->CreateRetVoid();
          }
          else
          {
            builder->CreateRet(
                llvm::Constant::getNullValue(func->getReturnType()));
          }
          builder->SetInsertPoint(start_bb);
        }
      }

      SetAsyncState(&async_state_storage);
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      async_setup_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    // Emit procedure body
    if (perf_enabled)
    {
      ir_proc_perf.stack.clear();
      ir_proc_perf.stack.reserve(256);
      prior_ir_ctx = g_ir_proc_perf_ctx;
      g_ir_proc_perf_ctx = &ir_proc_perf;
    }
    EmitIR(proc.body);
    if (perf_enabled)
    {
      g_ir_proc_perf_ctx = prior_ir_ctx;
      ir_self_total_ms = IRProcPerfTotalSelfMs(ir_proc_perf);
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      body_emit_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    if (async_state_active)
    {
      SetAsyncState(nullptr);
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      async_clear_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    // Ensure all blocks are terminated
    llvm::Type *ret_ty = func->getReturnType();
    for (auto &block : *func)
    {
      if (block.getTerminator())
      {
        continue;
      }
      builder->SetInsertPoint(&block);
      if (ret_ty->isVoidTy())
      {
        builder->CreateRetVoid();
      }
      else
      {
        builder->CreateRet(llvm::Constant::getNullValue(ret_ty));
      }
      ++inserted_terminators;
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      terminator_fix_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    ClearLocals();
    ClearTempValues();
    ClearSymbolAliases();
    if (perf_enabled)
    {
      const auto now = Clock::now();
      final_cleanup_ms = ElapsedMs(phase_start, now);
      const long long total_ms = ElapsedMs(proc_start, now);
      const bool log_this_proc = log_all_procs || total_ms >= slow_proc_threshold_ms;
      if (log_this_proc)
      {
        std::string perf_line =
            "module=" + perf_module_label + " stage=emit-proc symbol=" +
            proc.symbol + " total_ms=" + std::to_string(total_ms) +
            " state_reset_ms=" + std::to_string(state_reset_ms) +
            " prologue_ms=" + std::to_string(prologue_ms) + " abi_ms=" +
            std::to_string(abi_ms) + " bind_params_ms=" +
            std::to_string(bind_params_ms) + " panic_slot_ms=" +
            std::to_string(panic_slot_ms) + " async_setup_ms=" +
            std::to_string(async_setup_ms) + " body_emit_ms=" +
            std::to_string(body_emit_ms) + " async_clear_ms=" +
            std::to_string(async_clear_ms) + " terminator_fix_ms=" +
            std::to_string(terminator_fix_ms) + " final_cleanup_ms=" +
            std::to_string(final_cleanup_ms) + " param_total=" +
            std::to_string(proc.params.size()) + " bound_params=" +
            std::to_string(bound_params) + " bound_by_ref=" +
            std::to_string(bound_params_by_ref) + " bound_by_value=" +
            std::to_string(bound_params_by_value) + " panic_slot_materialized=" +
            (panic_slot_materialized ? "1" : "0") + " async_active=" +
            (async_state_active ? "1" : "0") + " async_resume=" +
            (async_resume_mode ? "1" : "0") + " async_slots_typed=" +
            std::to_string(async_slots_typed) + " async_slots_restored=" +
            std::to_string(async_slots_restored) + " inserted_terminators=" +
            std::to_string(inserted_terminators) + " ir_self_ms=" +
            std::to_string(ir_self_total_ms);
        if (perf_enabled)
        {
          AppendTopIRNodePerf(perf_line, ir_proc_perf);
        }
        EmitPerfLogLine(perf_line);
      }
    }
  }

} // namespace cursive::codegen
