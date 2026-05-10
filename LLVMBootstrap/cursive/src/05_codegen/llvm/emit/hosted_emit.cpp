// =============================================================================
// File: 05_codegen/llvm/emit/hosted_emit.cpp
// Canonical owner for the moved LLVM emitter implementation slice.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

namespace cursive::codegen {

using namespace emit_detail;

  bool LLVMEmitter::IsHostedLibraryBuild() const
  {
    return current_ctx_ && current_ctx_->hosted_library;
  }

  bool LLVMEmitter::RequiresHostedEnvParam(const std::string &symbol) const
  {
    if (!IsHostedLibraryBuild() || symbol.empty() || !current_ctx_)
    {
      return false;
    }
    return current_ctx_->hosted_explicit_env_procs.find(symbol) !=
           current_ctx_->hosted_explicit_env_procs.end();
  }

  bool LLVMEmitter::HasHostedStateSlot(const std::string &symbol) const
  {
    if (!IsHostedLibraryBuild() || symbol.empty())
    {
      return false;
    }
    return hosted_layout_.slots.find(symbol) != hosted_layout_.slots.end();
  }

  llvm::Value *LLVMEmitter::GetHostedCurrentEnvPtr()
  {
    if (!IsHostedLibraryBuild())
    {
      return nullptr;
    }
    if (hosted_env_value_)
    {
      return hosted_env_value_;
    }
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    if (!builder || !builder->GetInsertBlock())
    {
      return nullptr;
    }
    llvm::Type *opaque_ptr_ty = GetOpaquePtr();
    if (!opaque_ptr_ty)
    {
      return nullptr;
    }
    llvm::Function *fn = module_->getFunction(HostRuntimeCurrentEnvSym());
    if (!fn)
    {
      llvm::FunctionType *fn_ty =
          llvm::FunctionType::get(opaque_ptr_ty, {}, false);
      fn = llvm::Function::Create(
          fn_ty,
          llvm::GlobalValue::ExternalLinkage,
          HostRuntimeCurrentEnvSym(),
          module_.get());
      fn->setCallingConv(llvm::CallingConv::C);
    }
    llvm::CallInst *call = builder->CreateCall(fn->getFunctionType(), fn, {});
    call->setCallingConv(fn->getCallingConv());
    return call;
  }

  llvm::Value *LLVMEmitter::GetSharedLibraryImagePanicPtr(
      llvm::Value *fallback_ptr)
  {
    if (!current_ctx_ || !current_ctx_->shared_library_project)
    {
      return fallback_ptr;
    }
    llvm::Type *panic_ty = GetLLVMType(PanicRecordType());
    if (!panic_ty)
    {
      return fallback_ptr;
    }

    llvm::GlobalVariable *panic_gv =
        module_->getNamedGlobal(ImagePanicRecordSym());
    if (!panic_gv)
    {
      panic_gv = new llvm::GlobalVariable(
          *module_,
          panic_ty,
          false,
          llvm::GlobalValue::CommonLinkage,
          llvm::Constant::getNullValue(panic_ty),
          ImagePanicRecordSym());
      panic_gv->setAlignment(llvm::Align(4));
    }
    return panic_gv ? static_cast<llvm::Value *>(panic_gv) : fallback_ptr;
  }

  llvm::Value *LLVMEmitter::GetHostedStatePtr(const std::string &symbol,
                                              llvm::Type *value_ty,
                                              llvm::Value *fallback_ptr)
  {
    if (!IsHostedLibraryBuild() || !value_ty)
    {
      return fallback_ptr;
    }
    const auto it = hosted_layout_.slots.find(symbol);
    if (it == hosted_layout_.slots.end())
    {
      return fallback_ptr;
    }
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    if (!builder || !builder->GetInsertBlock())
    {
      return fallback_ptr;
    }

    llvm::Type *ptr_ty = llvm::PointerType::get(value_ty, 0);
    auto coerce_fallback = [&]() -> llvm::Value * {
      if (!fallback_ptr)
      {
        return nullptr;
      }
      llvm::Value *typed_fallback = CoerceTo(builder, fallback_ptr, ptr_ty);
      if (!typed_fallback && fallback_ptr->getType()->isPointerTy())
      {
        typed_fallback = builder->CreateBitCast(fallback_ptr, ptr_ty);
      }
      return typed_fallback;
    };

    llvm::Value *env = GetHostedCurrentEnvPtr();
    if (!env)
    {
      return coerce_fallback();
    }

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(context_);
    llvm::Value *env_i8 = CoerceTo(builder, env, llvm::PointerType::get(i8_ty, 0));
    if (!env_i8)
    {
      env_i8 = builder->CreateBitCast(env, llvm::PointerType::get(i8_ty, 0));
    }

    auto build_slot_ptr = [&](llvm::Value *session_env_i8) -> llvm::Value * {
      llvm::Value *slot_ptr = session_env_i8;
      if (it->second.offset != 0u)
      {
        slot_ptr = builder->CreateGEP(
            i8_ty,
            session_env_i8,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                   it->second.offset));
      }
      return builder->CreateBitCast(slot_ptr, ptr_ty);
    };

    llvm::Value *typed_fallback = coerce_fallback();
    if (!typed_fallback)
    {
      return build_slot_ptr(env_i8);
    }

    llvm::Function *fn = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock *session_bb =
        llvm::BasicBlock::Create(context_, "hosted.state.session", fn);
    llvm::BasicBlock *fallback_bb =
        llvm::BasicBlock::Create(context_, "hosted.state.fallback", fn);
    llvm::BasicBlock *merge_bb =
        llvm::BasicBlock::Create(context_, "hosted.state.merge", fn);
    llvm::Value *has_env = builder->CreateICmpNE(
        env_i8,
        llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(env_i8->getType())));
    builder->CreateCondBr(has_env, session_bb, fallback_bb);

    builder->SetInsertPoint(session_bb);
    llvm::Value *session_ptr = build_slot_ptr(env_i8);
    builder->CreateBr(merge_bb);
    session_bb = builder->GetInsertBlock();

    builder->SetInsertPoint(fallback_bb);
    builder->CreateBr(merge_bb);
    fallback_bb = builder->GetInsertBlock();

    builder->SetInsertPoint(merge_bb);
    llvm::PHINode *phi = builder->CreatePHI(ptr_ty, 2);
    phi->addIncoming(session_ptr, session_bb);
    phi->addIncoming(typed_fallback, fallback_bb);
    return phi;
  }

  llvm::Value *LLVMEmitter::GetHostedSessionPanicPtr(llvm::Value *fallback_ptr)
  {
    if (!IsHostedLibraryBuild())
    {
      return fallback_ptr;
    }
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    if (!builder || !builder->GetInsertBlock())
    {
      return fallback_ptr;
    }
    llvm::Type *panic_ty = GetLLVMType(PanicRecordType());
    if (!panic_ty)
    {
      return fallback_ptr;
    }
    llvm::Type *panic_ptr_ty = llvm::PointerType::get(panic_ty, 0);
    auto coerce_fallback = [&]() -> llvm::Value * {
      if (!fallback_ptr)
      {
        return nullptr;
      }
      llvm::Value *typed_fallback = CoerceTo(builder, fallback_ptr, panic_ptr_ty);
      if (!typed_fallback && fallback_ptr->getType()->isPointerTy())
      {
        typed_fallback = builder->CreateBitCast(fallback_ptr, panic_ptr_ty);
      }
      return typed_fallback;
    };

    llvm::Value *env = GetHostedCurrentEnvPtr();
    if (!env)
    {
      return coerce_fallback();
    }

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(context_);
    llvm::Value *env_i8 = CoerceTo(builder, env, llvm::PointerType::get(i8_ty, 0));
    if (!env_i8)
    {
      env_i8 = builder->CreateBitCast(env, llvm::PointerType::get(i8_ty, 0));
    }

    auto build_session_panic_ptr = [&](llvm::Value *session_env_i8) -> llvm::Value * {
      llvm::Value *panic_i8 = session_env_i8;
      if (hosted_layout_.panic_offset != 0u)
      {
        panic_i8 = builder->CreateGEP(
            i8_ty,
            session_env_i8,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_),
                                   hosted_layout_.panic_offset));
      }
      return builder->CreateBitCast(panic_i8, panic_ptr_ty);
    };

    llvm::Value *typed_fallback = coerce_fallback();
    if (!typed_fallback)
    {
      return build_session_panic_ptr(env_i8);
    }

    llvm::Function *fn = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock *session_bb =
        llvm::BasicBlock::Create(context_, "hosted.panic.session", fn);
    llvm::BasicBlock *fallback_bb =
        llvm::BasicBlock::Create(context_, "hosted.panic.fallback", fn);
    llvm::BasicBlock *merge_bb =
        llvm::BasicBlock::Create(context_, "hosted.panic.merge", fn);
    llvm::Value *has_env = builder->CreateICmpNE(
        env_i8,
        llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(env_i8->getType())));
    builder->CreateCondBr(has_env, session_bb, fallback_bb);

    builder->SetInsertPoint(session_bb);
    llvm::Value *session_ptr = build_session_panic_ptr(env_i8);
    builder->CreateBr(merge_bb);
    session_bb = builder->GetInsertBlock();

    builder->SetInsertPoint(fallback_bb);
    builder->CreateBr(merge_bb);
    fallback_bb = builder->GetInsertBlock();

    builder->SetInsertPoint(merge_bb);
    llvm::PHINode *phi = builder->CreatePHI(panic_ptr_ty, 2);
    phi->addIncoming(session_ptr, session_bb);
    phi->addIncoming(typed_fallback, fallback_bb);
    return phi;
  }

  void LLVMEmitter::EmitHostedLifecycleExports()
  {
    if (!current_ctx_ || !current_ctx_->hosted_library ||
        current_ctx_->hosted_exports.empty() ||
        !IsProjectEntryModule(current_ctx_))
    {
      return;
    }

    auto *i1_ty = llvm::Type::getInt1Ty(context_);
    auto *i8_ty = llvm::Type::getInt8Ty(context_);
    auto *i32_ty = llvm::Type::getInt32Ty(context_);
    auto *i64_ty = llvm::Type::getInt64Ty(context_);
    llvm::Type *usize_ty = llvm::Type::getInt64Ty(context_);
    llvm::Type *opaque_ptr_ty = GetOpaquePtr();
    auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);
    llvm::Type *panic_record_ty = GetLLVMType(PanicRecordType());
    llvm::Type *context_ty = GetLLVMType(analysis::MakeTypePath({"Context"}));
    llvm::GlobalVariable *owner_token_gv =
        EnsureHostedOwnerTokenGlobal(module_.get(), context_, true);
    if (!owner_token_gv)
    {
      current_ctx_->ReportCodegenFailure();
      return;
    }
    llvm::Value *owner_token =
        llvm::ConstantExpr::getBitCast(owner_token_gv, opaque_ptr_ty);

    auto ensure_runtime_fn = [&](const char *name,
                                 llvm::Type *ret_ty,
                                 std::vector<llvm::Type *> params) -> llvm::Function * {
      llvm::Function *fn = module_->getFunction(name);
      if (!fn)
      {
        llvm::FunctionType *fn_ty = llvm::FunctionType::get(ret_ty, params, false);
        fn = llvm::Function::Create(
            fn_ty,
            llvm::GlobalValue::ExternalLinkage,
            name,
            module_.get());
        fn->setCallingConv(llvm::CallingConv::C);
      }
      return fn;
    };

    auto call_proc_with_panic = [&](llvm::IRBuilder<> &builder,
                                    const std::string &sym,
                                    llvm::Value *panic_ptr,
                                    llvm::Value *env_ptr) {
      const LowerCtx::ProcSigInfo *sig =
          current_ctx_ ? current_ctx_->LookupProcSig(sym) : nullptr;
      if (!sig)
      {
        return;
      }
      std::vector<IRParam> call_params = sig->params;
      if (RequiresHostedEnvParam(sym) && !HasLeadingHostedEnvParam(call_params))
      {
        call_params.insert(call_params.begin(), HostedEnvParam());
      }
      ABICallResult abi = ComputeCallABI(*this, call_params, sig->ret);
      if (!abi.valid || !abi.func_type)
      {
        if (current_ctx_)
        {
          current_ctx_->ReportCodegenFailure();
        }
        return;
      }
      llvm::Function *fn = module_->getFunction(sym);
      if (!fn)
      {
        fn = llvm::Function::Create(
            abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            sym,
            module_.get());
      }
      std::vector<llvm::Value *> args;
      args.reserve(fn->arg_size());
      for (unsigned i = 0; i < fn->arg_size(); ++i)
      {
        llvm::Type *param_ty = fn->getFunctionType()->getParamType(i);
        llvm::Value *arg = nullptr;
        if (!call_params.empty() && call_params[0].name == kHostedEnvParamName &&
            i < abi.param_indices.size() && abi.param_indices[0].has_value() &&
            *abi.param_indices[0] == i)
        {
          arg = CoerceTo(&builder, env_ptr, param_ty);
          if (!arg && env_ptr && env_ptr->getType() == param_ty)
          {
            arg = env_ptr;
          }
        }
        if (!abi.param_indices.empty() && abi.param_indices.back().has_value() &&
            *abi.param_indices.back() == i)
        {
          arg = CoerceTo(&builder, panic_ptr, param_ty);
          if (!arg && panic_ptr && panic_ptr->getType() == param_ty)
          {
            arg = panic_ptr;
          }
        }
        if (!arg)
        {
          if (current_ctx_)
          {
            current_ctx_->ReportCodegenFailure();
          }
          return;
        }
        args.push_back(arg);
      }
      builder.CreateCall(fn->getFunctionType(), fn, args);
    };

    auto panic_offsets = [&]() {
      std::uint64_t flag_offset = 0;
      std::uint64_t code_offset = 4;
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
      const auto layout = ::cursive::analysis::layout::RecordLayoutOf(scope,
                                         {analysis::MakeTypePrim("bool"),
                                          analysis::MakeTypePrim("u32")});
      if (layout.has_value() && layout->offsets.size() >= 2)
      {
        flag_offset = layout->offsets[0];
        code_offset = layout->offsets[1];
      }
      return std::pair<std::uint64_t, std::uint64_t>{flag_offset, code_offset};
    }();

    auto panic_field_ptr = [&](llvm::IRBuilder<> &builder,
                               llvm::Value *panic_ptr,
                               std::uint64_t offset,
                               llvm::Type *field_ty) -> llvm::Value * {
      llvm::Value *panic_i8 =
          CoerceTo(&builder, panic_ptr, llvm::PointerType::get(i8_ty, 0));
      if (!panic_i8)
      {
        panic_i8 =
            builder.CreateBitCast(panic_ptr, llvm::PointerType::get(i8_ty, 0));
      }
      llvm::Value *field_i8 = panic_i8;
      if (offset != 0u)
      {
        field_i8 = builder.CreateGEP(
            i8_ty, panic_i8, llvm::ConstantInt::get(i64_ty, offset));
      }
      return builder.CreateBitCast(field_i8, llvm::PointerType::get(field_ty, 0));
    };

    auto clear_panic_record = [&](llvm::IRBuilder<> &builder, llvm::Value *panic_ptr) {
      if (!panic_ptr)
      {
        return;
      }
      builder.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0),
                          panic_field_ptr(builder,
                                          panic_ptr,
                                          panic_offsets.first,
                                          llvm::Type::getInt8Ty(context_)));
      builder.CreateStore(llvm::ConstantInt::get(i32_ty, 0),
                          panic_field_ptr(builder,
                                          panic_ptr,
                                          panic_offsets.second,
                                          i32_ty));
    };

    auto load_panic_flag = [&](llvm::IRBuilder<> &builder,
                               llvm::Value *panic_ptr) -> llvm::Value * {
      return builder.CreateICmpNE(
          builder.CreateLoad(
              llvm::Type::getInt8Ty(context_),
              panic_field_ptr(builder,
                              panic_ptr,
                              panic_offsets.first,
                              llvm::Type::getInt8Ty(context_))),
          llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0));
    };

    auto load_panic_code = [&](llvm::IRBuilder<> &builder,
                               llvm::Value *panic_ptr) -> llvm::Value * {
      return builder.CreateLoad(
          i32_ty,
          panic_field_ptr(builder, panic_ptr, panic_offsets.second, i32_ty));
    };

    auto build_env_slot_ptr = [&](llvm::IRBuilder<> &builder,
                                  llvm::Value *env_ptr,
                                  std::uint64_t offset,
                                  llvm::Type *target_ty) -> llvm::Value * {
      llvm::Value *env_i8 =
          CoerceTo(&builder, env_ptr, llvm::PointerType::get(i8_ty, 0));
      if (!env_i8)
      {
        env_i8 =
            builder.CreateBitCast(env_ptr, llvm::PointerType::get(i8_ty, 0));
      }
      llvm::Value *slot_i8 = env_i8;
      if (offset != 0u)
      {
        slot_i8 = builder.CreateGEP(
            i8_ty, env_i8, llvm::ConstantInt::get(i64_ty, offset));
      }
      return builder.CreateBitCast(slot_i8, llvm::PointerType::get(target_ty, 0));
    };

    llvm::Function *abi_fn = ensure_runtime_fn(HostAbiVersionSym(), i32_ty, {});
    if (abi_fn->empty())
    {
      llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", abi_fn);
      auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
      builder->SetInsertPoint(entry);
      builder->CreateRet(llvm::ConstantInt::get(i32_ty, kHostAbiVersion));
    }

    llvm::Function *create_fn = ensure_runtime_fn(HostSessionCreateSym(), usize_ty, {});
    if (create_fn->empty())
    {
      llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", create_fn);
      auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
      builder->SetInsertPoint(entry);

      llvm::Function *alloc_fn =
          ensure_runtime_fn(HostRuntimeAllocSym(), opaque_ptr_ty, {usize_ty});
      llvm::Function *free_fn =
          ensure_runtime_fn(HostRuntimeFreeSym(),
                            llvm::Type::getVoidTy(context_),
                            {opaque_ptr_ty});
      llvm::Function *register_fn =
          ensure_runtime_fn(HostRuntimeRegisterSym(),
                            usize_ty,
                            {opaque_ptr_ty, opaque_ptr_ty});
      llvm::Function *try_enter_fn = ensure_runtime_fn(
          HostRuntimeTryEnterSym(),
          i32_ty,
          {usize_ty, opaque_ptr_ty, opaque_ptr_ptr_ty});
      llvm::Function *leave_fn = ensure_runtime_fn(
          HostRuntimeLeaveSym(), i32_ty, {usize_ty, opaque_ptr_ty});
      llvm::Function *retire_fn = ensure_runtime_fn(
          HostRuntimeTryRetireSym(),
          i32_ty,
          {usize_ty, opaque_ptr_ty, opaque_ptr_ptr_ty});
      llvm::Function *abort_live_fn = ensure_runtime_fn(
          HostRuntimeAbortLiveSym(),
          i32_ty,
          {usize_ty, opaque_ptr_ty, opaque_ptr_ptr_ty});

      llvm::Value *env_ptr =
          builder->CreateCall(alloc_fn, {llvm::ConstantInt::get(usize_ty, hosted_layout_.size)});
      llvm::BasicBlock *alloc_ok =
          llvm::BasicBlock::Create(context_, "host.create.alloc.ok", create_fn);
      llvm::BasicBlock *alloc_fail =
          llvm::BasicBlock::Create(context_, "host.create.alloc.fail", create_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(
              env_ptr, llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)),
          alloc_ok,
          alloc_fail);

      builder->SetInsertPoint(alloc_fail);
      builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));

      builder->SetInsertPoint(alloc_ok);
      builder->CreateMemSet(
          env_ptr,
          llvm::ConstantInt::get(i8_ty, 0),
          llvm::ConstantInt::get(i64_ty, hosted_layout_.size),
          llvm::Align(1));

      if (context_ty)
      {
        llvm::Value *ctx_ptr = build_env_slot_ptr(
            *builder, env_ptr, hosted_layout_.context_offset, context_ty);
        const std::string context_init_sym = ContextInitSym();
        if (std::optional<RuntimeFuncInfo> init_info =
                GetRuntimeFuncInfo(context_init_sym))
        {
          ABICallResult init_abi =
              ComputeCallABI(*this, init_info->params, init_info->ret, true);
          if (!init_abi.valid || !init_abi.func_type)
          {
            current_ctx_->ReportCodegenFailure();
            return;
          }
          llvm::Function *context_init_fn = module_->getFunction(context_init_sym);
          if (!context_init_fn)
          {
            context_init_fn = llvm::Function::Create(
                init_abi.func_type,
                llvm::GlobalValue::ExternalLinkage,
                context_init_sym,
                module_.get());
            context_init_fn->setCallingConv(llvm::CallingConv::C);
          }
          std::vector<llvm::Value *> init_args;
          for (unsigned i = 0; i < context_init_fn->arg_size(); ++i)
          {
            llvm::Type *param_ty =
                context_init_fn->getFunctionType()->getParamType(i);
            llvm::Value *arg = nullptr;
            if (init_abi.has_sret && i == 0u)
            {
              arg = CoerceTo(builder, ctx_ptr, param_ty);
            }
            if (!arg)
            {
              current_ctx_->ReportCodegenFailure();
              return;
            }
            init_args.push_back(arg);
          }
          llvm::CallInst *init_call =
              builder->CreateCall(context_init_fn->getFunctionType(),
                                  context_init_fn,
                                  init_args);
          init_call->setCallingConv(context_init_fn->getCallingConv());
          if (!init_abi.has_sret && !init_call->getType()->isVoidTy())
          {
            builder->CreateStore(init_call, ctx_ptr);
          }
        }
      }

      for (const auto &[symbol, slot] : hosted_layout_.slots)
      {
        if (slot.zero_init || slot.bytes.empty() || slot.size == 0u)
        {
          continue;
        }

        // Hosted sessions must start from the static initializer template,
        // not from the current live DLL-global contents.
        std::vector<std::uint8_t> template_bytes(slot.size, 0u);
        const std::size_t copy_size =
            std::min<std::size_t>(template_bytes.size(), slot.bytes.size());
        std::copy(slot.bytes.begin(),
                  slot.bytes.begin() + copy_size,
                  template_bytes.begin());

        const std::string template_name = symbol + "__host_template";
        llvm::GlobalVariable *template_gv = module_->getNamedGlobal(template_name);
        if (!template_gv)
        {
          llvm::ArrayType *template_ty =
              llvm::ArrayType::get(i8_ty, template_bytes.size());
          llvm::Constant *template_init =
              llvm::ConstantDataArray::get(context_, template_bytes);
          template_gv = new llvm::GlobalVariable(
              *module_,
              template_ty,
              true,
              llvm::GlobalValue::InternalLinkage,
              template_init,
              template_name);
          template_gv->setAlignment(llvm::Align(1));
        }

        llvm::Value *slot_ptr =
            build_env_slot_ptr(*builder, env_ptr, slot.offset, llvm::Type::getInt8Ty(context_));
        llvm::Value *src_ptr = builder->CreateBitCast(
            template_gv, llvm::PointerType::get(i8_ty, 0));
        llvm::Value *dst_ptr = builder->CreateBitCast(
            slot_ptr, llvm::PointerType::get(i8_ty, 0));
        builder->CreateMemCpy(dst_ptr,
                              llvm::Align(1),
                              src_ptr,
                              llvm::Align(1),
                              llvm::ConstantInt::get(i64_ty, slot.size));
      }

      llvm::AllocaInst *entered_env =
          builder->CreateAlloca(opaque_ptr_ty, nullptr, "host_env");
      builder->CreateStore(
          llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);
      llvm::Value *handle = builder->CreateCall(register_fn, {owner_token, env_ptr});
      llvm::BasicBlock *registered_ok =
          llvm::BasicBlock::Create(context_, "host.create.register.ok", create_fn);
      llvm::BasicBlock *registered_fail =
          llvm::BasicBlock::Create(context_, "host.create.register.fail", create_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(handle, llvm::ConstantInt::get(usize_ty, 0)),
          registered_ok,
          registered_fail);

      builder->SetInsertPoint(registered_fail);
      builder->CreateCall(free_fn, {env_ptr});
      builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));

      builder->SetInsertPoint(registered_ok);
      llvm::Value *entered_ok = builder->CreateCall(
          try_enter_fn,
          {handle, owner_token, CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
      llvm::BasicBlock *enter_ok =
          llvm::BasicBlock::Create(context_, "host.create.enter.ok", create_fn);
      llvm::BasicBlock *enter_fail =
          llvm::BasicBlock::Create(context_, "host.create.enter.fail", create_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(entered_ok, llvm::ConstantInt::get(i32_ty, 0)),
          enter_ok,
          enter_fail);

      builder->SetInsertPoint(enter_fail);
      {
      builder->CreateStore(
          llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);
      llvm::Value *retired = builder->CreateCall(
          retire_fn,
          {handle, owner_token, CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
      llvm::BasicBlock *retire_ok_bb =
          llvm::BasicBlock::Create(context_, "host.create.retire.ok", create_fn);
      llvm::BasicBlock *retire_fail_bb =
          llvm::BasicBlock::Create(context_, "host.create.retire.fail", create_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(retired, llvm::ConstantInt::get(i32_ty, 0)),
          retire_ok_bb,
          retire_fail_bb);

      builder->SetInsertPoint(retire_ok_bb);
      {
        llvm::Value *cleanup_env = builder->CreateLoad(opaque_ptr_ty, entered_env);
        llvm::Value *use_cleanup_env = builder->CreateSelect(
            builder->CreateICmpNE(
                cleanup_env,
                llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)),
            cleanup_env,
            env_ptr);
        builder->CreateCall(free_fn, {use_cleanup_env});
        builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
      }

      builder->SetInsertPoint(retire_fail_bb);
      builder->CreateStore(
          llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);
      llvm::Value *aborted = builder->CreateCall(
          abort_live_fn,
          {handle, owner_token, CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
      llvm::BasicBlock *abort_ok_bb =
          llvm::BasicBlock::Create(context_, "host.create.abort.ok", create_fn);
      llvm::BasicBlock *abort_fail_bb =
          llvm::BasicBlock::Create(context_, "host.create.abort.fail", create_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(aborted, llvm::ConstantInt::get(i32_ty, 0)),
          abort_ok_bb,
          abort_fail_bb);

      builder->SetInsertPoint(abort_ok_bb);
      {
        llvm::Value *cleanup_env = builder->CreateLoad(opaque_ptr_ty, entered_env);
        llvm::Value *use_cleanup_env = builder->CreateSelect(
            builder->CreateICmpNE(
                cleanup_env,
                llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)),
            cleanup_env,
            env_ptr);
        builder->CreateCall(free_fn, {use_cleanup_env});
        builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
      }

      builder->SetInsertPoint(abort_fail_bb);
      builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
      }

      builder->SetInsertPoint(enter_ok);
      llvm::Value *panic_ptr =
          build_env_slot_ptr(*builder, env_ptr, hosted_layout_.panic_offset, panic_record_ty);
      clear_panic_record(*builder, panic_ptr);
      for (std::size_t module_index = 0;
           module_index < current_ctx_->init_order.size();
           ++module_index)
      {
        const auto &module_path = current_ctx_->init_order[module_index];
        call_proc_with_panic(*builder, InitFn(module_path), panic_ptr, env_ptr);
        llvm::BasicBlock *cont =
            llvm::BasicBlock::Create(context_, "host.create.init.cont", create_fn);
        llvm::BasicBlock *fail =
            llvm::BasicBlock::Create(context_, "host.create.init.fail", create_fn);
        builder->CreateCondBr(load_panic_flag(*builder, panic_ptr), fail, cont);
        builder->SetInsertPoint(fail);
        {
        clear_panic_record(*builder, panic_ptr);
        for (std::size_t deinit_index = module_index; deinit_index > 0; --deinit_index)
        {
          call_proc_with_panic(*builder,
                               DeinitFn(current_ctx_->init_order[deinit_index - 1]),
                               panic_ptr,
                               env_ptr);
          clear_panic_record(*builder, panic_ptr);
        }
        llvm::Value *leave_ok =
            builder->CreateCall(leave_fn, {handle, owner_token});
        llvm::BasicBlock *leave_ok_bb =
            llvm::BasicBlock::Create(context_, "host.create.leave.ok", create_fn);
        llvm::BasicBlock *leave_fail_bb =
            llvm::BasicBlock::Create(context_, "host.create.leave.fail", create_fn);
        builder->CreateCondBr(
            builder->CreateICmpNE(leave_ok, llvm::ConstantInt::get(i32_ty, 0)),
            leave_ok_bb,
            leave_fail_bb);
        builder->SetInsertPoint(leave_fail_bb);
        builder->CreateStore(
            llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);
        llvm::Value *aborted_retry = builder->CreateCall(
            abort_live_fn,
            {handle,
             owner_token,
             CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
        llvm::BasicBlock *abort_ok_bb =
            llvm::BasicBlock::Create(context_, "host.create.abort.ok", create_fn);
        llvm::BasicBlock *abort_fail_bb =
            llvm::BasicBlock::Create(context_, "host.create.abort.fail", create_fn);
        builder->CreateCondBr(
            builder->CreateICmpNE(aborted_retry, llvm::ConstantInt::get(i32_ty, 0)),
            abort_ok_bb,
            abort_fail_bb);
        builder->SetInsertPoint(abort_ok_bb);
        {
          llvm::Value *cleanup_env = builder->CreateLoad(opaque_ptr_ty, entered_env);
          llvm::Value *use_cleanup_env = builder->CreateSelect(
              builder->CreateICmpNE(
                  cleanup_env,
                  llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)),
              cleanup_env,
              env_ptr);
          builder->CreateCall(free_fn, {use_cleanup_env});
          builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
        }
        builder->SetInsertPoint(abort_fail_bb);
        builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
        builder->SetInsertPoint(leave_ok_bb);
        builder->CreateStore(
            llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);
        llvm::Value *retired = builder->CreateCall(
            retire_fn,
            {handle,
             owner_token,
             CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
        llvm::BasicBlock *retire_ok_bb =
            llvm::BasicBlock::Create(context_, "host.create.retire.ok", create_fn);
        llvm::BasicBlock *retire_fail_bb =
            llvm::BasicBlock::Create(context_, "host.create.retire.fail", create_fn);
        builder->CreateCondBr(
            builder->CreateICmpNE(retired, llvm::ConstantInt::get(i32_ty, 0)),
            retire_ok_bb,
            retire_fail_bb);
        builder->SetInsertPoint(retire_ok_bb);
        {
          llvm::Value *cleanup_env = builder->CreateLoad(opaque_ptr_ty, entered_env);
          llvm::Value *use_cleanup_env = builder->CreateSelect(
              builder->CreateICmpNE(
                  cleanup_env,
                  llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)),
              cleanup_env,
              env_ptr);
          builder->CreateCall(free_fn, {use_cleanup_env});
          builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
        }
        builder->SetInsertPoint(retire_fail_bb);
        builder->CreateStore(
            llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);
        llvm::Value *aborted = builder->CreateCall(
            abort_live_fn,
            {handle,
             owner_token,
             CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
        llvm::BasicBlock *abort_ok_bb2 =
            llvm::BasicBlock::Create(context_, "host.create.abort.ok", create_fn);
        llvm::BasicBlock *abort_fail_bb2 =
            llvm::BasicBlock::Create(context_, "host.create.abort.fail", create_fn);
        builder->CreateCondBr(
            builder->CreateICmpNE(aborted, llvm::ConstantInt::get(i32_ty, 0)),
            abort_ok_bb2,
            abort_fail_bb2);
        builder->SetInsertPoint(abort_ok_bb2);
        {
          llvm::Value *cleanup_env = builder->CreateLoad(opaque_ptr_ty, entered_env);
          llvm::Value *use_cleanup_env = builder->CreateSelect(
              builder->CreateICmpNE(
                  cleanup_env,
                  llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)),
              cleanup_env,
              env_ptr);
          builder->CreateCall(free_fn, {use_cleanup_env});
          builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
        }
        builder->SetInsertPoint(abort_fail_bb2);
        builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
        }
        builder->SetInsertPoint(cont);
      }

      llvm::Value *leave_ok = builder->CreateCall(leave_fn, {handle, owner_token});
      llvm::BasicBlock *leave_ok_bb =
          llvm::BasicBlock::Create(context_, "host.create.leave.ok", create_fn);
      llvm::BasicBlock *leave_fail_bb =
          llvm::BasicBlock::Create(context_, "host.create.leave.fail", create_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(leave_ok, llvm::ConstantInt::get(i32_ty, 0)),
          leave_ok_bb,
          leave_fail_bb);
      builder->SetInsertPoint(leave_fail_bb);
      {
      builder->CreateStore(
          llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);
      llvm::Value *aborted = builder->CreateCall(
          abort_live_fn,
          {handle,
           owner_token,
           CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
      llvm::BasicBlock *abort_ok_bb =
          llvm::BasicBlock::Create(context_, "host.create.abort.ok", create_fn);
      llvm::BasicBlock *abort_fail_bb =
          llvm::BasicBlock::Create(context_, "host.create.abort.fail", create_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(aborted, llvm::ConstantInt::get(i32_ty, 0)),
          abort_ok_bb,
          abort_fail_bb);
      builder->SetInsertPoint(abort_ok_bb);
      {
        llvm::Value *cleanup_env = builder->CreateLoad(opaque_ptr_ty, entered_env);
        llvm::Value *use_cleanup_env = builder->CreateSelect(
            builder->CreateICmpNE(
                cleanup_env,
                llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)),
            cleanup_env,
            env_ptr);
        builder->CreateCall(free_fn, {use_cleanup_env});
        builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
      }
      builder->SetInsertPoint(abort_fail_bb);
      builder->CreateRet(llvm::ConstantInt::get(usize_ty, 0));
      }
      builder->SetInsertPoint(leave_ok_bb);
      builder->CreateRet(handle);
    }

    llvm::Function *destroy_fn =
        ensure_runtime_fn(HostSessionDestroySym(), i32_ty, {usize_ty});
    if (destroy_fn->empty())
    {
      llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", destroy_fn);
      auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
      builder->SetInsertPoint(entry);

      llvm::Function *free_fn =
          ensure_runtime_fn(HostRuntimeFreeSym(),
                            llvm::Type::getVoidTy(context_),
                            {opaque_ptr_ty});
      llvm::Function *retire_fn = ensure_runtime_fn(
          HostRuntimeTryRetireSym(),
          i32_ty,
          {usize_ty, opaque_ptr_ty, opaque_ptr_ptr_ty});
      llvm::Function *enter_retired_fn = ensure_runtime_fn(
          HostRuntimeEnterRetiredSym(),
          i32_ty,
          {usize_ty, opaque_ptr_ty, opaque_ptr_ty});
      llvm::Function *leave_retired_fn = ensure_runtime_fn(
          HostRuntimeLeaveRetiredSym(), i32_ty, {usize_ty, opaque_ptr_ty});
      llvm::Function *abort_retired_fn = ensure_runtime_fn(
          HostRuntimeAbortRetiredSym(),
          i32_ty,
          {usize_ty, opaque_ptr_ty, opaque_ptr_ty});

      llvm::Argument *handle_arg = destroy_fn->getArg(0);
      llvm::AllocaInst *retired_env =
          builder->CreateAlloca(opaque_ptr_ty, nullptr, "host_env");
      builder->CreateStore(
          llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), retired_env);
      llvm::Value *retired_ok = builder->CreateCall(
          retire_fn,
          {handle_arg, owner_token, CoerceTo(builder, retired_env, opaque_ptr_ptr_ty)});
      llvm::BasicBlock *retire_ok =
          llvm::BasicBlock::Create(context_, "host.destroy.retire.ok", destroy_fn);
      llvm::BasicBlock *retire_fail =
          llvm::BasicBlock::Create(context_, "host.destroy.retire.fail", destroy_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(retired_ok, llvm::ConstantInt::get(i32_ty, 0)),
          retire_ok,
          retire_fail);

      builder->SetInsertPoint(retire_fail);
      builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));

      builder->SetInsertPoint(retire_ok);
      llvm::Value *env_ptr = builder->CreateLoad(opaque_ptr_ty, retired_env);
      llvm::Value *entered_env = builder->CreateCall(
          enter_retired_fn, {handle_arg, owner_token, env_ptr});
      llvm::BasicBlock *enter_ok =
          llvm::BasicBlock::Create(context_, "host.destroy.enter.ok", destroy_fn);
      llvm::BasicBlock *enter_fail =
          llvm::BasicBlock::Create(context_, "host.destroy.enter.fail", destroy_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(entered_env, llvm::ConstantInt::get(i32_ty, 0)),
          enter_ok,
          enter_fail);

      builder->SetInsertPoint(enter_fail);
      {
        llvm::Value *aborted = builder->CreateCall(
            abort_retired_fn, {handle_arg, owner_token, env_ptr});
        llvm::BasicBlock *abort_ok_bb =
            llvm::BasicBlock::Create(context_, "host.destroy.enter.abort.ok", destroy_fn);
        llvm::BasicBlock *abort_fail_bb =
            llvm::BasicBlock::Create(context_, "host.destroy.enter.abort.fail", destroy_fn);
        builder->CreateCondBr(
            builder->CreateICmpNE(aborted, llvm::ConstantInt::get(i32_ty, 0)),
            abort_ok_bb,
            abort_fail_bb);
        builder->SetInsertPoint(abort_ok_bb);
        builder->CreateCall(free_fn, {env_ptr});
        builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));
        builder->SetInsertPoint(abort_fail_bb);
        builder->CreateCall(free_fn, {env_ptr});
        builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));
      }

      builder->SetInsertPoint(enter_ok);
      llvm::Value *panic_ptr =
          build_env_slot_ptr(*builder, env_ptr, hosted_layout_.panic_offset, panic_record_ty);
      clear_panic_record(*builder, panic_ptr);
      llvm::AllocaInst *had_panic_slot = builder->CreateAlloca(i1_ty);
      builder->CreateStore(llvm::ConstantInt::getFalse(context_), had_panic_slot);
      llvm::BasicBlock *deinit_panic_bb =
          llvm::BasicBlock::Create(context_, "host.destroy.deinit.fail", destroy_fn);
      llvm::BasicBlock *deinit_done_bb =
          llvm::BasicBlock::Create(context_, "host.destroy.deinit.done", destroy_fn);
      for (auto it = current_ctx_->init_order.rbegin();
           it != current_ctx_->init_order.rend();
           ++it)
      {
        call_proc_with_panic(*builder, DeinitFn(*it), panic_ptr, env_ptr);
        llvm::BasicBlock *cont_bb =
            llvm::BasicBlock::Create(context_, "host.destroy.deinit.cont", destroy_fn);
        builder->CreateCondBr(LoadPanicFlag(*this, builder, panic_ptr),
                              deinit_panic_bb,
                              cont_bb);
        builder->SetInsertPoint(cont_bb);
      }
      builder->CreateBr(deinit_done_bb);

      builder->SetInsertPoint(deinit_panic_bb);
      builder->CreateStore(llvm::ConstantInt::getTrue(context_), had_panic_slot);
      builder->CreateBr(deinit_done_bb);

      builder->SetInsertPoint(deinit_done_bb);
      llvm::Value *had_panic = builder->CreateLoad(i1_ty, had_panic_slot);
      llvm::Value *leave_ok =
          builder->CreateCall(leave_retired_fn, {handle_arg, owner_token});
      llvm::BasicBlock *leave_ok_bb =
          llvm::BasicBlock::Create(context_, "host.destroy.leave.ok", destroy_fn);
      llvm::BasicBlock *leave_fail_bb =
          llvm::BasicBlock::Create(context_, "host.destroy.leave.fail", destroy_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(leave_ok, llvm::ConstantInt::get(i32_ty, 0)),
          leave_ok_bb,
          leave_fail_bb);
      builder->SetInsertPoint(leave_fail_bb);
      {
      llvm::Value *aborted = builder->CreateCall(
          abort_retired_fn, {handle_arg, owner_token, env_ptr});
      llvm::BasicBlock *abort_ok_bb =
          llvm::BasicBlock::Create(context_, "host.destroy.abort.ok", destroy_fn);
      llvm::BasicBlock *abort_fail_bb =
          llvm::BasicBlock::Create(context_, "host.destroy.abort.fail", destroy_fn);
      builder->CreateCondBr(
          builder->CreateICmpNE(aborted, llvm::ConstantInt::get(i32_ty, 0)),
          abort_ok_bb,
          abort_fail_bb);
      builder->SetInsertPoint(abort_ok_bb);
      builder->CreateCall(free_fn, {env_ptr});
      builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));
      builder->SetInsertPoint(abort_fail_bb);
      builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));
      }
      builder->SetInsertPoint(leave_ok_bb);
      builder->CreateCall(free_fn, {env_ptr});
      llvm::Value *destroy_ok = builder->CreateSelect(
          had_panic,
          llvm::ConstantInt::get(i32_ty, 0),
          llvm::ConstantInt::get(i32_ty, 1));
      builder->CreateRet(destroy_ok);
    }
  }

  void LLVMEmitter::EmitHostedExportThunks()
  {
    if (!current_ctx_ || !current_ctx_->hosted_library ||
        current_ctx_->hosted_exports.empty())
    {
      return;
    }

    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    auto *i1_ty = llvm::Type::getInt1Ty(context_);
    auto *i8_ty = llvm::Type::getInt8Ty(context_);
    auto *i32_ty = llvm::Type::getInt32Ty(context_);
    auto *i64_ty = llvm::Type::getInt64Ty(context_);
    llvm::Type *usize_ty = llvm::Type::getInt64Ty(context_);
    llvm::Type *opaque_ptr_ty = GetOpaquePtr();
    auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);
    llvm::Type *panic_record_ty = GetLLVMType(PanicRecordType());
    llvm::Type *context_ty = GetLLVMType(analysis::MakeTypePath({"Context"}));
    llvm::GlobalVariable *owner_token_gv = EnsureHostedOwnerTokenGlobal(
        module_.get(), context_, IsProjectEntryModule(current_ctx_));
    if (!owner_token_gv)
    {
      current_ctx_->ReportCodegenFailure();
      return;
    }
    llvm::Value *owner_token =
        llvm::ConstantExpr::getBitCast(owner_token_gv, opaque_ptr_ty);
    const analysis::ScopeContext &scope = BuildScope(current_ctx_);
    auto panic_offsets = [&]() {
      std::uint64_t flag_offset = 0;
      std::uint64_t code_offset = 4;
      const auto layout = ::cursive::analysis::layout::RecordLayoutOf(
          scope,
          {analysis::MakeTypePrim("bool"), analysis::MakeTypePrim("u32")});
      if (layout.has_value() && layout->offsets.size() >= 2)
      {
        flag_offset = layout->offsets[0];
        code_offset = layout->offsets[1];
      }
      return std::pair<std::uint64_t, std::uint64_t>{flag_offset, code_offset};
    }();

    auto ensure_runtime_fn = [&](const char *name,
                                 llvm::Type *ret_ty,
                                 std::vector<llvm::Type *> params) -> llvm::Function * {
      llvm::Function *fn = module_->getFunction(name);
      if (!fn)
      {
        llvm::FunctionType *fn_ty = llvm::FunctionType::get(ret_ty, params, false);
        fn = llvm::Function::Create(
            fn_ty,
            llvm::GlobalValue::ExternalLinkage,
            name,
            module_.get());
        fn->setCallingConv(llvm::CallingConv::C);
      }
      return fn;
    };

    auto runtime_panic_fn = [&]() -> llvm::Function * {
      llvm::Function *fn = module_->getFunction(RuntimePanicSym());
      if (!fn)
      {
        llvm::FunctionType *fn_ty =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {i32_ty}, false);
        fn = llvm::Function::Create(
            fn_ty,
            llvm::GlobalValue::ExternalLinkage,
            RuntimePanicSym(),
            module_.get());
        fn->setCallingConv(llvm::CallingConv::C);
      }
      return fn;
    }();

    auto build_env_slot_ptr = [&](llvm::IRBuilder<> &irb,
                                  llvm::Value *env_ptr,
                                  std::uint64_t offset,
                                  llvm::Type *target_ty) -> llvm::Value * {
      llvm::Value *env_i8 =
          CoerceTo(&irb, env_ptr, llvm::PointerType::get(i8_ty, 0));
      if (!env_i8)
      {
        env_i8 = irb.CreateBitCast(env_ptr, llvm::PointerType::get(i8_ty, 0));
      }
      llvm::Value *slot_i8 = env_i8;
      if (offset != 0u)
      {
        slot_i8 = irb.CreateGEP(
            i8_ty, env_i8, llvm::ConstantInt::get(i64_ty, offset));
      }
      return irb.CreateBitCast(slot_i8, llvm::PointerType::get(target_ty, 0));
    };

    auto panic_field_ptr = [&](llvm::IRBuilder<> &irb,
                               llvm::Value *panic_ptr,
                               std::uint64_t offset,
                               llvm::Type *field_ty) -> llvm::Value * {
      llvm::Value *panic_i8 =
          CoerceTo(&irb, panic_ptr, llvm::PointerType::get(i8_ty, 0));
      if (!panic_i8)
      {
        panic_i8 = irb.CreateBitCast(panic_ptr, llvm::PointerType::get(i8_ty, 0));
      }
      llvm::Value *field_i8 = panic_i8;
      if (offset != 0u)
      {
        field_i8 = irb.CreateGEP(
            i8_ty, panic_i8, llvm::ConstantInt::get(i64_ty, offset));
      }
      return irb.CreateBitCast(field_i8, llvm::PointerType::get(field_ty, 0));
    };

    auto clear_panic_record = [&](llvm::IRBuilder<> &irb, llvm::Value *panic_ptr) {
      irb.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0),
                      panic_field_ptr(irb,
                                      panic_ptr,
                                      panic_offsets.first,
                                      llvm::Type::getInt8Ty(context_)));
      irb.CreateStore(llvm::ConstantInt::get(i32_ty, 0),
                      panic_field_ptr(irb, panic_ptr, panic_offsets.second, i32_ty));
    };

    auto load_panic_flag = [&](llvm::IRBuilder<> &irb,
                               llvm::Value *panic_ptr) -> llvm::Value * {
      return irb.CreateICmpNE(
          irb.CreateLoad(
              llvm::Type::getInt8Ty(context_),
              panic_field_ptr(irb,
                              panic_ptr,
                              panic_offsets.first,
                              llvm::Type::getInt8Ty(context_))),
          llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0));
    };

    auto load_panic_code = [&](llvm::IRBuilder<> &irb,
                               llvm::Value *panic_ptr) -> llvm::Value * {
      return irb.CreateLoad(
          i32_ty,
          panic_field_ptr(irb, panic_ptr, panic_offsets.second, i32_ty));
    };

    auto normalize_type =
        [&](auto &&self, analysis::TypeRef type, std::size_t depth) -> analysis::TypeRef {
      if (!type || depth > 16u)
      {
        return type;
      }
      analysis::TypeRef cur = analysis::StripPerm(type);
      if (!cur)
      {
        cur = type;
      }
      while (cur)
      {
        if (const auto *refine = std::get_if<analysis::TypeRefine>(&cur->node))
        {
          cur = analysis::StripPerm(refine->base);
          if (!cur)
          {
            cur = refine->base;
          }
          continue;
        }
        break;
      }
      if (const auto *path = cur ? std::get_if<analysis::TypePathType>(&cur->node) : nullptr)
      {
        if (path->generic_args.empty())
        {
          ast::Path syntax_path;
          for (const auto &comp : path->path)
          {
            syntax_path.push_back(comp);
          }
          const auto it = scope.sigma.types.find(analysis::PathKeyOf(syntax_path));
          if (it != scope.sigma.types.end())
          {
            if (const auto *alias = std::get_if<ast::TypeAliasDecl>(&it->second))
            {
              const auto lowered = analysis::LowerType(scope, alias->type);
              if (lowered.ok && lowered.type)
              {
                return self(self, lowered.type, depth + 1u);
              }
            }
          }
        }
      }
      return cur;
    };

    auto context_field_value =
        [&](llvm::IRBuilder<> &irb,
            llvm::Value *ctx_value,
            std::string_view field_name) -> llvm::Value * {
      struct ContextFieldInfo {
        const char *name;
        analysis::TypeRef type;
      };
      const std::array<ContextFieldInfo, 5> fields = {{
          {"fs", analysis::MakeTypeDynamic({"FileSystem"})},
          {"net", analysis::MakeTypeDynamic({"Network"})},
          {"heap", analysis::MakeTypeDynamic({"HeapAllocator"})},
          {"sys", analysis::MakeTypePath({"System"})},
          {"reactor", analysis::MakeTypeDynamic({"Reactor"})},
      }};
      std::size_t extract_index = 0u;
      for (const auto &field : fields)
      {
        const auto size = ::cursive::analysis::layout::SizeOf(scope, field.type).value_or(0u);
        if (std::string_view(field.name) == field_name)
        {
          if (size == 0u)
          {
            llvm::Type *field_ty = GetLLVMType(field.type);
            return field_ty && !field_ty->isVoidTy()
                       ? llvm::Constant::getNullValue(field_ty)
                       : nullptr;
          }
          return irb.CreateExtractValue(ctx_value, {static_cast<unsigned>(extract_index)});
        }
        if (size != 0u)
        {
          ++extract_index;
        }
      }
      return nullptr;
    };

    auto build_context_bundle =
        [&](auto &&self,
            llvm::IRBuilder<> &irb,
            analysis::TypeRef target_type,
            std::string_view field_name,
            llvm::Value *root_ctx_ptr,
            llvm::Value *root_ctx_value) -> llvm::Value * {
      analysis::TypeRef cur = normalize_type(normalize_type, target_type, 0u);
      if (!cur)
      {
        return nullptr;
      }

      if (const auto *dyn = std::get_if<analysis::TypeDynamic>(&cur->node))
      {
        if (field_name == "cpu" || field_name == "gpu" || field_name == "inline")
        {
          const analysis::TypeRef expected_context_type =
              analysis::MakeTypePath({"Context"});
          const analysis::TypeRef expected_domain_type =
              analysis::MakeTypeDynamic({"ExecutionDomain"});
          std::string runtime_sym =
              field_name == "cpu" ? BuiltinSymContextCpu()
              : field_name == "gpu" ? BuiltinSymContextGpu()
                                    : BuiltinSymContextInline();
          if (auto runtime_info = GetRuntimeFuncInfo(runtime_sym))
          {
            const auto ctx_eq =
                analysis::TypeEquiv(runtime_info->params.size() == 1u
                                        ? runtime_info->params[0].type
                                        : nullptr,
                                    expected_context_type);
            const auto ret_eq =
                analysis::TypeEquiv(runtime_info->ret, expected_domain_type);
            const auto target_eq = analysis::TypeEquiv(cur, expected_domain_type);
            if (runtime_info->params.size() != 1u || !ctx_eq.ok || !ctx_eq.equiv ||
                !ret_eq.ok || !ret_eq.equiv || !target_eq.ok || !target_eq.equiv)
            {
              current_ctx_->ReportCodegenFailure();
              return nullptr;
            }

            ABICallResult abi =
                ComputeCallABI(*this, runtime_info->params, runtime_info->ret, true);
            if (!abi.valid || !abi.func_type || abi.param_kinds.size() != 1u)
            {
              current_ctx_->ReportCodegenFailure();
              return nullptr;
            }
            llvm::Function *fn = module_->getFunction(runtime_sym);
            if (!fn)
            {
              fn = llvm::Function::Create(
                  abi.func_type,
                  llvm::GlobalValue::ExternalLinkage,
                  runtime_sym,
                  module_.get());
              fn->setCallingConv(llvm::CallingConv::C);
            }

            llvm::Value *context_arg = nullptr;
            if (abi.param_kinds[0] == PassKind::ByRef)
            {
              context_arg = root_ctx_ptr;
            }
            else
            {
              context_arg = root_ctx_value;
            }
            if (!context_arg)
            {
              current_ctx_->ReportCodegenFailure();
              return nullptr;
            }

            return EmitABICall(
                *this,
                &irb,
                fn,
                runtime_info->params,
                runtime_info->ret,
                {context_arg},
                true);
          }
          current_ctx_->ReportCodegenFailure();
          return nullptr;
        }
        return context_field_value(irb, root_ctx_value, field_name);
      }

      if (const auto *path = std::get_if<analysis::TypePathType>(&cur->node))
      {
        if (path->generic_args.empty() && path->path.size() == 1u &&
            path->path.front() == "System")
        {
          llvm::Type *target_ll = GetLLVMType(cur);
          return target_ll && !target_ll->isVoidTy()
                     ? llvm::Constant::getNullValue(target_ll)
                     : nullptr;
        }

        if (const ast::RecordDecl *record =
                analysis::LookupRecordDecl(scope, path->path))
        {
          llvm::Type *target_ll = GetLLVMType(cur);
          if (!target_ll || target_ll->isVoidTy())
          {
            return nullptr;
          }
          llvm::Value *aggregate = llvm::Constant::getNullValue(target_ll);
          unsigned insert_index = 0u;
          for (const auto &member : record->members)
          {
            const auto *field = std::get_if<ast::FieldDecl>(&member);
            if (!field)
            {
              continue;
            }
            auto lowered = analysis::LowerType(scope, field->type);
            if (!lowered.ok || !lowered.type)
            {
              continue;
            }
            llvm::Value *field_value = self(
                self, irb, lowered.type, field->name, root_ctx_ptr, root_ctx_value);
            const auto field_size = ::cursive::analysis::layout::SizeOf(scope, lowered.type).value_or(0u);
            if (field_size == 0u)
            {
              continue;
            }
            if (!field_value)
            {
              llvm::Type *field_ty = GetLLVMType(lowered.type);
              if (!field_ty || field_ty->isVoidTy())
              {
                continue;
              }
              field_value = llvm::Constant::getNullValue(field_ty);
            }
            aggregate = irb.CreateInsertValue(
                aggregate, field_value, {insert_index++});
          }
          return aggregate;
        }
      }

      return context_field_value(irb, root_ctx_value, field_name);
    };

    llvm::Function *try_enter_fn = ensure_runtime_fn(
        HostRuntimeTryEnterSym(),
        i32_ty,
        {usize_ty, opaque_ptr_ty, opaque_ptr_ptr_ty});
    llvm::Function *leave_fn = ensure_runtime_fn(
        HostRuntimeLeaveSym(), i32_ty, {usize_ty, opaque_ptr_ty});

    for (const auto &info : current_ctx_->hosted_exports)
    {
      const auto *proc_module = current_ctx_->LookupProcModule(info.internal_symbol);
      if (!proc_module || *proc_module != current_ctx_->module_path)
      {
        continue;
      }

      std::vector<IRParam> thunk_params;
      const std::string hosted_session_param(
          project::ActiveLanguageProfile().hosted_session_param_name);
      thunk_params.push_back(IRParam{analysis::ParamMode::Move,
                                     hosted_session_param,
                                     hosted_session_param,
                                     analysis::MakeTypePrim("usize")});
      thunk_params.insert(thunk_params.end(),
                          info.visible_params.begin(),
                          info.visible_params.end());
      ABICallResult thunk_abi = ComputeCallABI(
          *this,
          thunk_params,
          info.ret,
          /*use_c_abi_aggregate_sret=*/true,
          /*foreign_boundary_mode_independent=*/true);
      if (!thunk_abi.valid || !thunk_abi.func_type)
      {
        current_ctx_->ReportCodegenFailure();
        continue;
      }
      llvm::Function *thunk = module_->getFunction(info.thunk_symbol);
      if (!thunk)
      {
        thunk = llvm::Function::Create(
            thunk_abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            info.thunk_symbol,
            module_.get());
      }
      thunk->setCallingConv(CallingConvForAbi(info.abi));
      if (!thunk->empty())
      {
        continue;
      }

      llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", thunk);
      builder->SetInsertPoint(entry);
      locals_.clear();
      local_types_.clear();
      values_.clear();

      struct PreparedArg {
        llvm::Value *value = nullptr;
        llvm::Value *storage = nullptr;
        analysis::TypeRef type;
      };
      std::vector<PreparedArg> prepared(thunk_params.size());
      llvm::IRBuilder<> entry_builder(&thunk->getEntryBlock(),
                                      thunk->getEntryBlock().begin());
      auto create_entry_alloca =
          [&](llvm::Type *alloc_ty, llvm::StringRef name) -> llvm::AllocaInst * {
        llvm::IRBuilder<> alloca_builder(&thunk->getEntryBlock(),
                                         thunk->getEntryBlock().begin());
        return alloca_builder.CreateAlloca(alloc_ty, nullptr, name);
      };
      llvm::Type *ret_ll = GetLLVMType(info.ret);
      llvm::Value *thunk_sret_storage = nullptr;
      if (thunk_abi.has_sret && ret_ll && !ret_ll->isVoidTy() &&
          thunk->arg_size() > 0u)
      {
        thunk_sret_storage = thunk->getArg(0);
      }
      auto typed_thunk_sret_storage =
          [&](llvm::IRBuilder<> &irb) -> llvm::Value * {
        if (!thunk_sret_storage || !ret_ll || ret_ll->isVoidTy())
        {
          return nullptr;
        }
        llvm::Value *typed_ptr = thunk_sret_storage;
        llvm::Type *target_ptr_ty = llvm::PointerType::get(ret_ll, 0);
        if (typed_ptr->getType() != target_ptr_ty)
        {
          typed_ptr = irb.CreateBitCast(typed_ptr, target_ptr_ty);
        }
        return typed_ptr;
      };
      auto zero_thunk_sret =
          [&](llvm::IRBuilder<> &irb) {
        llvm::Value *typed_ptr = typed_thunk_sret_storage(irb);
        if (!typed_ptr)
        {
          return;
        }
        const llvm::DataLayout &dl = module_->getDataLayout();
        llvm::Value *dst_ptr = irb.CreateBitCast(
            typed_ptr, llvm::PointerType::get(i8_ty, 0));
        irb.CreateMemSet(dst_ptr,
                         llvm::ConstantInt::get(i8_ty, 0),
                         llvm::ConstantInt::get(i64_ty, dl.getTypeStoreSize(ret_ll)),
                         llvm::Align(1));
      };
      auto store_into_thunk_sret =
          [&](llvm::IRBuilder<> &irb,
              llvm::Value *aggregate_value,
              llvm::Value *aggregate_storage) -> bool {
        llvm::Value *typed_ptr = typed_thunk_sret_storage(irb);
        if (!typed_ptr)
        {
          return false;
        }
        if (aggregate_storage)
        {
          const llvm::DataLayout &dl = module_->getDataLayout();
          llvm::Value *src_ptr = aggregate_storage;
          llvm::Type *target_ptr_ty = llvm::PointerType::get(ret_ll, 0);
          if (src_ptr->getType() != target_ptr_ty)
          {
            src_ptr = irb.CreateBitCast(src_ptr, target_ptr_ty);
          }
          llvm::Value *dst_i8 = irb.CreateBitCast(
              typed_ptr, llvm::PointerType::get(i8_ty, 0));
          llvm::Value *src_i8 = irb.CreateBitCast(
              src_ptr, llvm::PointerType::get(i8_ty, 0));
          irb.CreateMemCpy(dst_i8,
                           llvm::Align(1),
                           src_i8,
                           llvm::Align(1),
                           llvm::ConstantInt::get(
                               i64_ty, dl.getTypeStoreSize(ret_ll)));
          return true;
        }
        if (!aggregate_value)
        {
          return false;
        }
        if (aggregate_value->getType() != ret_ll)
        {
          if (llvm::Value *coerced = CoerceTo(&irb, aggregate_value, ret_ll))
          {
            aggregate_value = coerced;
          }
          else
          {
            return false;
          }
        }
        irb.CreateStore(aggregate_value, typed_ptr);
        return true;
      };
      auto emit_hosted_boundary_failure =
          [&](llvm::IRBuilder<> &irb, llvm::Value *panic_code_value) {
        if (info.unwind_mode == LowerCtx::ExportUnwindMode::Catch)
        {
          zero_thunk_sret(irb);
          if (thunk->getReturnType()->isVoidTy())
          {
            irb.CreateRetVoid();
          }
          else
          {
            irb.CreateRet(llvm::Constant::getNullValue(thunk->getReturnType()));
          }
          return;
        }
        if (!panic_code_value)
        {
          panic_code_value = llvm::ConstantInt::get(
              i32_ty, PanicCode(PanicReason::Other));
        }
        if (panic_code_value->getType() != i32_ty)
        {
          panic_code_value = irb.CreateIntCast(panic_code_value, i32_ty, false);
        }
        irb.CreateCall(runtime_panic_fn, {panic_code_value});
        irb.CreateUnreachable();
      };
      for (std::size_t i = 0; i < thunk_params.size(); ++i)
      {
        if (i >= thunk_abi.param_indices.size() || !thunk_abi.param_indices[i].has_value())
        {
          continue;
        }
        llvm::Argument *arg = thunk->getArg(*thunk_abi.param_indices[i]);
        arg->setName(thunk_params[i].name);
        llvm::Type *param_ll = GetLLVMType(thunk_params[i].type);
        const ABIArgCarrierKind carrier =
            i < thunk_abi.param_carriers.size()
                ? thunk_abi.param_carriers[i]
                : ABIArgCarrierKind::Direct;
        if (thunk_abi.param_kinds[i] == PassKind::ByRef)
        {
          prepared[i].storage = arg;
          prepared[i].type = thunk_params[i].type;
          if (param_ll)
          {
            llvm::Value *typed_ptr = arg;
            if (typed_ptr->getType() != llvm::PointerType::get(param_ll, 0))
            {
              typed_ptr =
                  builder->CreateBitCast(typed_ptr, llvm::PointerType::get(param_ll, 0));
            }
            prepared[i].storage = typed_ptr;
            prepared[i].value = builder->CreateLoad(param_ll, typed_ptr);
          }
        }
        else if (carrier == ABIArgCarrierKind::Indirect)
        {
          prepared[i].type = thunk_params[i].type;
          if (param_ll)
          {
            llvm::Value *typed_ptr = arg;
            llvm::Type *target_ptr_ty = llvm::PointerType::get(param_ll, 0);
            if (typed_ptr->getType() != target_ptr_ty)
            {
              typed_ptr = builder->CreateBitCast(typed_ptr, target_ptr_ty);
            }
            prepared[i].storage = typed_ptr;
            prepared[i].value = builder->CreateLoad(param_ll, typed_ptr);
          }
          else
          {
            prepared[i].value = arg;
          }
        }
        else
        {
          prepared[i].type = thunk_params[i].type;
          llvm::Value *prepared_value = arg;
          if (param_ll && prepared_value && prepared_value->getType() != param_ll)
          {
            if (llvm::Value *coerced = CoerceTo(builder, prepared_value, param_ll))
            {
              prepared_value = coerced;
            }
          }
          prepared[i].value = prepared_value;
          if (param_ll && !param_ll->isVoidTy())
          {
            llvm::AllocaInst *slot =
                create_entry_alloca(param_ll, thunk_params[i].name);
            llvm::Value *stored_value = prepared_value;
            if (!stored_value)
            {
              stored_value = llvm::Constant::getNullValue(param_ll);
            }
            else if (stored_value->getType() != param_ll)
            {
              if (llvm::Value *coerced = CoerceTo(builder, stored_value, param_ll))
              {
                stored_value = coerced;
              }
              else
              {
                stored_value = llvm::Constant::getNullValue(param_ll);
              }
            }
            builder->CreateStore(stored_value, slot);
            prepared[i].storage = slot;
          }
        }
      }

      llvm::Value *handle = prepared[0].value;
      if (!handle)
      {
        handle = llvm::ConstantInt::get(usize_ty, 0);
      }
      if (handle->getType() != usize_ty)
      {
        handle = builder->CreateIntCast(handle, usize_ty, false);
      }

      llvm::AllocaInst *entered_env =
          create_entry_alloca(opaque_ptr_ty, "host_env");
      builder->CreateStore(
          llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty), entered_env);

      llvm::Value *entered_ok = builder->CreateCall(
          try_enter_fn,
          {handle, owner_token, CoerceTo(builder, entered_env, opaque_ptr_ptr_ty)});
      llvm::BasicBlock *entered_bb =
          llvm::BasicBlock::Create(context_, "host.enter.ok", thunk);
      llvm::BasicBlock *rejected_bb =
          llvm::BasicBlock::Create(context_, "host.enter.reject", thunk);
      builder->CreateCondBr(
          builder->CreateICmpNE(entered_ok, llvm::ConstantInt::get(i32_ty, 0)),
          entered_bb,
          rejected_bb);

      builder->SetInsertPoint(rejected_bb);
      emit_hosted_boundary_failure(
          *builder,
          llvm::ConstantInt::get(i32_ty, PanicCode(PanicReason::ForeignPre)));

      builder->SetInsertPoint(entered_bb);
      llvm::Value *env_ptr = builder->CreateLoad(opaque_ptr_ty, entered_env);
      llvm::Value *ctx_ptr =
          build_env_slot_ptr(*builder, env_ptr, hosted_layout_.context_offset, context_ty);
      llvm::Value *ctx_value = builder->CreateLoad(context_ty, ctx_ptr);
      llvm::Value *panic_ptr =
          build_env_slot_ptr(*builder, env_ptr, hosted_layout_.panic_offset, panic_record_ty);
      clear_panic_record(*builder, panic_ptr);

      const LowerCtx::ProcSigInfo *internal_sig =
          current_ctx_->LookupProcSig(info.internal_symbol);
      if (!internal_sig)
      {
        current_ctx_->ReportCodegenFailure();
        llvm::Value *leave_ok =
            builder->CreateCall(leave_fn, {handle, owner_token});
        llvm::BasicBlock *leave_ok_bb =
            llvm::BasicBlock::Create(context_, "host.call.leave.ok", thunk);
        llvm::BasicBlock *leave_fail_bb =
            llvm::BasicBlock::Create(context_, "host.call.leave.fail", thunk);
        builder->CreateCondBr(
            builder->CreateICmpNE(leave_ok, llvm::ConstantInt::get(i32_ty, 0)),
            leave_ok_bb,
            leave_fail_bb);
        builder->SetInsertPoint(leave_fail_bb);
        emit_hosted_boundary_failure(
            *builder,
            llvm::ConstantInt::get(i32_ty, PanicCode(PanicReason::Other)));
        builder->SetInsertPoint(leave_ok_bb);
        emit_hosted_boundary_failure(
            *builder,
            llvm::ConstantInt::get(i32_ty, PanicCode(PanicReason::Other)));
        continue;
      }

      std::vector<PreparedArg> source_args;
      const bool internal_needs_hosted_env =
          RequiresHostedEnvParam(info.internal_symbol);
      std::vector<IRParam> internal_params = internal_sig->params;
      if (internal_needs_hosted_env &&
          !HasLeadingHostedEnvParam(internal_params))
      {
        internal_params.insert(internal_params.begin(), HostedEnvParam());
      }
      source_args.reserve(internal_params.size());
      if (internal_needs_hosted_env)
      {
        PreparedArg env_arg;
        env_arg.type = HostedEnvParamType();
        env_arg.value = CoerceTo(builder, env_ptr, GetLLVMType(env_arg.type));
        if (!env_arg.value)
        {
          env_arg.value = env_ptr;
        }
        source_args.push_back(env_arg);
      }
      PreparedArg ctx_arg;
      ctx_arg.type = info.context_type;
      ctx_arg.value = build_context_bundle(
          build_context_bundle, *builder, info.context_type, "", ctx_ptr, ctx_value);
      if (llvm::Type *ctx_ll = GetLLVMType(info.context_type))
      {
        llvm::AllocaInst *slot = create_entry_alloca(ctx_ll, "host_ctx");
        if (ctx_arg.value)
        {
          builder->CreateStore(ctx_arg.value, slot);
        }
        else
        {
          builder->CreateStore(llvm::Constant::getNullValue(ctx_ll), slot);
        }
        ctx_arg.storage = slot;
      }
      source_args.push_back(ctx_arg);
      for (std::size_t i = 1; i < prepared.size(); ++i)
      {
        source_args.push_back(prepared[i]);
      }
      PreparedArg panic_arg;
      panic_arg.type = PanicOutType();
      panic_arg.value = panic_ptr;
      panic_arg.storage = nullptr;
      source_args.push_back(panic_arg);

      ABICallResult internal_abi =
          ComputeCallABI(*this, internal_params, internal_sig->ret);
      llvm::Function *callee = module_->getFunction(info.internal_symbol);
      if (!callee)
      {
        callee = functions_[info.internal_symbol];
      }
      std::vector<llvm::Value *> call_args;
      llvm::AllocaInst *ret_slot = nullptr;
      if (internal_abi.has_sret && ret_ll && !ret_ll->isVoidTy())
      {
        ret_slot = create_entry_alloca(ret_ll, "host_ret");
        call_args.push_back(CoerceTo(builder, ret_slot, opaque_ptr_ty));
      }
      for (std::size_t i = 0; i < internal_params.size(); ++i)
      {
        const auto kind =
            i < internal_abi.param_kinds.size() ? internal_abi.param_kinds[i]
                                                : PassKind::ByValue;
        llvm::Value *arg = nullptr;
        if (i < source_args.size())
        {
          if (kind == PassKind::ByRef)
          {
            arg = source_args[i].storage;
            if (!arg && source_args[i].value && source_args[i].type)
            {
              if (llvm::Type *arg_ll = GetLLVMType(source_args[i].type))
              {
                llvm::AllocaInst *slot =
                    create_entry_alloca(arg_ll, "host_arg");
                builder->CreateStore(source_args[i].value, slot);
                arg = slot;
              }
            }
          }
          else
          {
            arg = source_args[i].value;
          }
        }
        if (!arg)
        {
          llvm::Type *param_ty =
              callee ? callee->getFunctionType()->getParamType(
                           static_cast<unsigned>(call_args.size()))
                     : nullptr;
          arg = param_ty ? llvm::Constant::getNullValue(param_ty)
                         : llvm::ConstantInt::get(i64_ty, 0);
        }
        else if (callee)
        {
          llvm::Type *param_ty =
              callee->getFunctionType()->getParamType(
                  static_cast<unsigned>(call_args.size()));
          if (arg->getType() != param_ty)
          {
            if (llvm::Value *coerced = CoerceTo(builder, arg, param_ty))
            {
              arg = coerced;
            }
          }
        }
        call_args.push_back(arg);
      }

      llvm::Value *result_value = nullptr;
      if (callee)
      {
        llvm::CallInst *call =
            builder->CreateCall(callee->getFunctionType(), callee, call_args);
        if (internal_abi.has_sret)
        {
          if (ret_slot)
          {
            result_value = builder->CreateLoad(ret_ll, ret_slot);
          }
        }
        else if (!call->getType()->isVoidTy())
        {
          result_value = call;
        }
      }

      llvm::Value *panic_flag = load_panic_flag(*builder, panic_ptr);
      llvm::Value *panic_code = load_panic_code(*builder, panic_ptr);
      llvm::Value *leave_ok = builder->CreateCall(leave_fn, {handle, owner_token});
      llvm::BasicBlock *leave_ok_bb =
          llvm::BasicBlock::Create(context_, "host.call.leave.ok", thunk);
      llvm::BasicBlock *leave_fail_bb =
          llvm::BasicBlock::Create(context_, "host.call.leave.fail", thunk);
      builder->CreateCondBr(
          builder->CreateICmpNE(leave_ok, llvm::ConstantInt::get(i32_ty, 0)),
          leave_ok_bb,
          leave_fail_bb);
      builder->SetInsertPoint(leave_fail_bb);
      emit_hosted_boundary_failure(
          *builder,
          llvm::ConstantInt::get(i32_ty, PanicCode(PanicReason::Other)));
      builder->SetInsertPoint(leave_ok_bb);

      llvm::BasicBlock *panic_bb =
          llvm::BasicBlock::Create(context_, "host.call.panic", thunk);
      llvm::BasicBlock *ok_bb =
          llvm::BasicBlock::Create(context_, "host.call.ok", thunk);
      builder->CreateCondBr(
          panic_flag,
          panic_bb,
          ok_bb);

      builder->SetInsertPoint(panic_bb);
      emit_hosted_boundary_failure(*builder, panic_code);

      builder->SetInsertPoint(ok_bb);
      if (thunk->getReturnType()->isVoidTy())
      {
        if (thunk_abi.has_sret)
        {
          const bool stored = store_into_thunk_sret(
              *builder, result_value, ret_slot);
          if (!stored)
          {
            zero_thunk_sret(*builder);
          }
        }
        builder->CreateRetVoid();
      }
      else
      {
        if (!result_value)
        {
          result_value = llvm::Constant::getNullValue(thunk->getReturnType());
        }
        else if (result_value->getType() != thunk->getReturnType())
        {
          if (llvm::Value *coerced =
                  CoerceTo(builder, result_value, thunk->getReturnType()))
          {
            result_value = coerced;
          }
          else
          {
            result_value = llvm::Constant::getNullValue(thunk->getReturnType());
          }
        }
        builder->CreateRet(result_value);
      }
    }
  }

} // namespace cursive::codegen
