// =============================================================================
// File: 05_codegen/llvm/emit/entry_emit.cpp
// Canonical owner for the moved LLVM emitter implementation slice.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

namespace cursive::codegen {

using namespace emit_detail;

  void LLVMEmitter::EmitEntryPoint()
  {
    if (!current_ctx_)
    {
      return;
    }

    const auto entry_decl = EntryStubDecl(*current_ctx_);
    if (!entry_decl.has_value())
    {
      return;
    }

    SPEC_RULE("LowerIRDecl-EntryPoint");

    const bool returns_exit_code =
        target_profile_ == project::TargetProfile::X86_64SysV;
    // Create the generated entry stub symbol. Windows uses it as the process
    // entrypoint directly; x86_64 SysV links a runtime-provided _start shim
    // that calls this stub and exits with its return code.
    llvm::FunctionType *main_ty =
        llvm::FunctionType::get(
            returns_exit_code ? llvm::Type::getInt32Ty(context_)
                              : llvm::Type::getVoidTy(context_),
            {},
            false);

    llvm::Function *main_fn = llvm::Function::Create(
        main_ty,
        llvm::GlobalValue::ExternalLinkage,
        entry_decl->symbol,
        module_.get());

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", main_fn);
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    builder->SetInsertPoint(entry);

    // Configure runtime log/trace sink when --log, --log-file, or --trace is used.
    if (current_ctx_->log_enabled)
    {
      const std::string set_sink_sym = RuntimeConformanceSetSinkSym();
      llvm::Function *set_sink_fn = module_->getFunction(set_sink_sym);
      llvm::Type *void_ty = llvm::Type::getVoidTy(context_);
      llvm::Type *i8_ty = llvm::Type::getInt8Ty(context_);
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
      if (!set_sink_fn)
      {
        llvm::FunctionType *set_sink_ty = llvm::FunctionType::get(
            void_ty, {i8_ty, GetOpaquePtr(), i64_ty}, false);
        set_sink_fn = llvm::Function::Create(set_sink_ty,
                                             llvm::GlobalValue::ExternalLinkage,
                                             set_sink_sym,
                                             module_.get());
      }

      if (set_sink_fn)
      {
        const bool to_file =
            current_ctx_->log_to_file &&
            !current_ctx_->log_file_path.empty();
        const bool to_console = current_ctx_->log_to_console || !to_file;
        std::uint8_t sink_mode = 0u;
        if (to_file && to_console)
        {
          sink_mode = 2u;
        }
        else if (to_file)
        {
          sink_mode = 1u;
        }
        llvm::Value *sink_kind =
            llvm::ConstantInt::get(i8_ty, sink_mode);
        llvm::Value *path_ptr =
            llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(GetOpaquePtr()));
        llvm::Value *path_len = llvm::ConstantInt::get(i64_ty, 0u);
        if (to_file)
        {
          llvm::Value *raw_path =
              builder->CreateGlobalStringPtr(current_ctx_->log_file_path);
          path_ptr = CoerceTo(builder, raw_path, GetOpaquePtr());
          if (!path_ptr)
          {
            path_ptr = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(GetOpaquePtr()));
          }
          path_len = llvm::ConstantInt::get(
              i64_ty, static_cast<uint64_t>(current_ctx_->log_file_path.size()));
        }

        llvm::FunctionType *set_sink_ty = set_sink_fn->getFunctionType();
        std::vector<llvm::Value *> set_sink_args;
        set_sink_args.reserve(set_sink_ty->getNumParams());
        for (unsigned i = 0; i < set_sink_ty->getNumParams(); ++i)
        {
          llvm::Type *param_ty = set_sink_ty->getParamType(i);
          llvm::Value *arg = nullptr;
          if (i == 0)
          {
            arg = CoerceTo(builder, sink_kind, param_ty);
          }
          else if (i == 1)
          {
            arg = CoerceTo(builder, path_ptr, param_ty);
          }
          else if (i == 2)
          {
            arg = CoerceTo(builder, path_len, param_ty);
          }
          if (!arg)
          {
            current_ctx_->ReportCodegenFailure();
            return;
          }
          set_sink_args.push_back(arg);
        }

        llvm::CallInst *set_sink_call =
            builder->CreateCall(set_sink_ty, set_sink_fn, set_sink_args);
        set_sink_call->setCallingConv(set_sink_fn->getCallingConv());
      }

      if (!current_ctx_->trace_root.empty())
      {
        const std::string set_root_sym = RuntimeConformanceSetRootSym();
        llvm::Function *set_root_fn = module_->getFunction(set_root_sym);
        if (!set_root_fn)
        {
          llvm::FunctionType *set_root_ty = llvm::FunctionType::get(
              void_ty, {GetOpaquePtr(), i64_ty}, false);
          set_root_fn = llvm::Function::Create(set_root_ty,
                                               llvm::GlobalValue::ExternalLinkage,
                                               set_root_sym,
                                               module_.get());
        }
        if (set_root_fn)
        {
          llvm::Value *raw_root =
              builder->CreateGlobalStringPtr(current_ctx_->trace_root);
          llvm::Value *root_ptr = CoerceTo(builder, raw_root, GetOpaquePtr());
          if (!root_ptr)
          {
            root_ptr = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(GetOpaquePtr()));
          }
          llvm::Value *root_len = llvm::ConstantInt::get(
              i64_ty, static_cast<uint64_t>(current_ctx_->trace_root.size()));
          llvm::FunctionType *set_root_ty = set_root_fn->getFunctionType();
          llvm::Value *root_arg0 = CoerceTo(builder, root_ptr, set_root_ty->getParamType(0));
          llvm::Value *root_arg1 = CoerceTo(builder, root_len, set_root_ty->getParamType(1));
          if (!root_arg0)
          {
            root_arg0 = llvm::Constant::getNullValue(set_root_ty->getParamType(0));
          }
          if (!root_arg1)
          {
            root_arg1 = llvm::Constant::getNullValue(set_root_ty->getParamType(1));
          }
          llvm::CallInst *set_root_call =
              builder->CreateCall(set_root_ty, set_root_fn, {root_arg0, root_arg1});
          set_root_call->setCallingConv(set_root_fn->getCallingConv());
        }
      }

      // Configure runtime category filtering as an explicit bitmask:
      // bit0=log, bit1=diagnostic, bit2=runtime.
      const std::string log_filter_sym = RuntimeConformanceSetLogFilterSym();
      llvm::Function *log_filter_fn = module_->getFunction(log_filter_sym);
      if (!log_filter_fn)
      {
        llvm::FunctionType *log_filter_ty = llvm::FunctionType::get(
            void_ty, {i8_ty}, false);
        log_filter_fn = llvm::Function::Create(log_filter_ty,
                                               llvm::GlobalValue::ExternalLinkage,
                                               log_filter_sym,
                                               module_.get());
      }
      if (log_filter_fn)
      {
        std::uint8_t filter_mode = current_ctx_->trace ? 0x7u : 0x1u;
        if (current_ctx_->trace_filter_mask.has_value())
        {
          filter_mode = *current_ctx_->trace_filter_mask;
        }
        llvm::Value *enabled = llvm::ConstantInt::get(i8_ty, filter_mode);
        llvm::FunctionType *log_filter_ty = log_filter_fn->getFunctionType();
        llvm::Value *arg = CoerceTo(builder, enabled, log_filter_ty->getParamType(0));
        if (!arg)
        {
          arg = llvm::Constant::getNullValue(log_filter_ty->getParamType(0));
        }
        llvm::CallInst *log_filter_call =
            builder->CreateCall(log_filter_ty, log_filter_fn, {arg});
        log_filter_call->setCallingConv(log_filter_fn->getCallingConv());
      }

      if (current_ctx_->trace_min_level.has_value())
      {
        const std::string min_level_sym = RuntimeConformanceSetMinLevelSym();
        llvm::Function *min_level_fn = module_->getFunction(min_level_sym);
        if (!min_level_fn)
        {
          llvm::FunctionType *min_level_ty = llvm::FunctionType::get(
              void_ty, {i8_ty}, false);
          min_level_fn = llvm::Function::Create(min_level_ty,
                                                llvm::GlobalValue::ExternalLinkage,
                                                min_level_sym,
                                                module_.get());
        }
        if (min_level_fn)
        {
          llvm::Value *level = llvm::ConstantInt::get(i8_ty, *current_ctx_->trace_min_level);
          llvm::FunctionType *min_level_ty = min_level_fn->getFunctionType();
          llvm::Value *arg = CoerceTo(builder, level, min_level_ty->getParamType(0));
          if (!arg)
          {
            arg = llvm::Constant::getNullValue(min_level_ty->getParamType(0));
          }
          llvm::CallInst *min_level_call =
              builder->CreateCall(min_level_ty, min_level_fn, {arg});
          min_level_call->setCallingConv(min_level_fn->getCallingConv());
        }
      }
    }

    // Call the Cursive main function
    llvm::Function *cursive_main = nullptr;
    if (const auto it = functions_.find(*current_ctx_->main_symbol);
        it != functions_.end())
    {
      cursive_main = it->second;
    }
    if (!cursive_main)
    {
      if (returns_exit_code)
      {
        builder->CreateRet(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1));
      }
      else
      {
        llvm::Function *runtime_exit_fn = module_->getFunction(BuiltinSymSystemExit());
        if (!runtime_exit_fn)
        {
          llvm::FunctionType *exit_ty =
              llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                      {llvm::Type::getInt32Ty(context_)},
                                      false);
          runtime_exit_fn = llvm::Function::Create(
              exit_ty,
              llvm::GlobalValue::ExternalLinkage,
              BuiltinSymSystemExit(),
              module_.get());
          runtime_exit_fn->setCallingConv(llvm::CallingConv::C);
        }
        builder->CreateCall(
            runtime_exit_fn->getFunctionType(),
            runtime_exit_fn,
            {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)});
        builder->CreateUnreachable();
      }
      return;
    }

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(context_);
    llvm::Type *i32_ty = llvm::Type::getInt32Ty(context_);
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
    llvm::Type *i8_ptr_ty = llvm::PointerType::get(i8_ty, 0);

    llvm::Type *panic_storage_ty = GetLLVMType(PanicRecordType());
    if (!panic_storage_ty || panic_storage_ty->isVoidTy())
    {
      panic_storage_ty = llvm::ArrayType::get(i8_ty, 8);
    }
    llvm::AllocaInst *panic_storage =
        builder->CreateAlloca(panic_storage_ty, nullptr, "entry_panic");
    builder->CreateStore(llvm::Constant::getNullValue(panic_storage_ty), panic_storage);

    llvm::Value *panic_ptr = CoerceTo(builder, panic_storage, GetOpaquePtr());
    if (!panic_ptr)
    {
      panic_ptr = builder->CreatePointerCast(panic_storage, GetOpaquePtr());
    }

    std::uint64_t panic_flag_offset = 0;
    std::uint64_t panic_code_offset = 4;
    {
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
      const auto layout = ::cursive::analysis::layout::RecordLayoutOf(scope, {
                                                    analysis::MakeTypePrim("bool"),
                                                    analysis::MakeTypePrim("u32"),
                                                });
      if (layout.has_value() && layout->offsets.size() >= 2)
      {
        panic_flag_offset = layout->offsets[0];
        panic_code_offset = layout->offsets[1];
      }
    }

    auto load_panic_flag = [&]() -> llvm::Value *
    {
      return LoadPanicFlag(*this, builder, panic_ptr);
    };

    auto load_panic_code = [&]() -> llvm::Value *
    {
      llvm::Value *code = LoadPanicCodeValue(*this, builder, panic_ptr);
      if (!code)
      {
        return llvm::ConstantInt::get(i32_ty, 1);
      }
      return code;
    };

    auto store_entry_panic_record = [&](llvm::Value *flag,
                                        llvm::Value *code)
    {
      llvm::Value *flag_value = CoerceTo(builder, flag, i8_ty);
      if (!flag_value)
      {
        flag_value = llvm::ConstantInt::get(i8_ty, 0);
      }
      llvm::Value *code_value = CoerceTo(builder, code, i32_ty);
      if (!code_value)
      {
        code_value = llvm::ConstantInt::get(i32_ty, 0);
      }
      EmitStoreAtOffset(*this, builder, panic_ptr, panic_flag_offset, flag_value);
      EmitStoreAtOffset(*this, builder, panic_ptr, panic_code_offset, code_value);
    };

    auto clear_entry_panic_record = [&]()
    {
      store_entry_panic_record(llvm::ConstantInt::get(i8_ty, 0),
                               llvm::ConstantInt::get(i32_ty, 0));
    };

    auto restore_entry_panic_record = [&](llvm::Value *code)
    {
      if (!code)
      {
        code = llvm::ConstantInt::get(i32_ty, 1);
      }
      store_entry_panic_record(llvm::ConstantInt::get(i8_ty, 1), code);
    };

    const std::string runtime_panic_sym = RuntimePanicSym();
    llvm::Function *runtime_panic_fn = module_->getFunction(runtime_panic_sym);
    if (!runtime_panic_fn)
    {
      llvm::FunctionType *panic_ty =
          llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {i32_ty}, false);
      runtime_panic_fn = llvm::Function::Create(
          panic_ty,
          llvm::GlobalValue::ExternalLinkage,
          runtime_panic_sym,
          module_.get());
      runtime_panic_fn->setCallingConv(llvm::CallingConv::C);
    }

    llvm::BasicBlock *panic_bb =
        llvm::BasicBlock::Create(context_, "entry.panic", main_fn);

    auto get_lifecycle_fn = [&](const std::string &symbol) -> llvm::Function *
    {
      if (const auto fn_it = functions_.find(symbol); fn_it != functions_.end())
      {
        return fn_it->second;
      }
      if (llvm::Function *fn = module_->getFunction(symbol))
      {
        return fn;
      }
      llvm::FunctionType *fn_ty = llvm::FunctionType::get(
          llvm::Type::getVoidTy(context_),
          {GetOpaquePtr()},
          false);
      return llvm::Function::Create(fn_ty,
                                    llvm::GlobalValue::ExternalLinkage,
                                    symbol,
                                    module_.get());
    };

    auto call_lifecycle_fn = [&](const std::string &symbol) -> bool
    {
      llvm::Function *fn = get_lifecycle_fn(symbol);
      if (!fn)
      {
        if (current_ctx_)
        {
          current_ctx_->ReportCodegenFailure();
        }
        return false;
      }

      llvm::FunctionType *fn_ty = fn->getFunctionType();
      std::vector<llvm::Value *> args;
      args.reserve(fn_ty->getNumParams());
      for (unsigned i = 0; i < fn_ty->getNumParams(); ++i)
      {
        llvm::Type *param_ty = fn_ty->getParamType(i);
        llvm::Value *arg = nullptr;
        if (i == 0)
        {
          arg = CoerceTo(builder, panic_ptr, param_ty);
        }
        if (!arg)
        {
          arg = llvm::Constant::getNullValue(param_ty);
        }
        args.push_back(arg);
      }

      llvm::CallInst *call = builder->CreateCall(fn_ty, fn, args);
      call->setCallingConv(fn->getCallingConv());
      return true;
    };

    std::vector<llvm::Value *> call_args;
    llvm::FunctionType *callee_ty = cursive_main->getFunctionType();
    call_args.reserve(callee_ty->getNumParams());
    for (unsigned i = 0; i < callee_ty->getNumParams(); ++i)
    {
      llvm::Type *param_ty = callee_ty->getParamType(i);
      call_args.push_back(llvm::Constant::getNullValue(param_ty));
    }

    analysis::TypeRef ctx_type = analysis::MakeTypePath({"Context"});
    llvm::Type *ctx_storage_ty = GetLLVMType(ctx_type);
    if (!ctx_storage_ty || ctx_storage_ty->isVoidTy())
    {
      ctx_storage_ty = llvm::ArrayType::get(i8_ty, 64);
    }
    llvm::AllocaInst *ctx_storage =
        builder->CreateAlloca(ctx_storage_ty, nullptr, "entry_ctx");
    builder->CreateStore(llvm::Constant::getNullValue(ctx_storage_ty), ctx_storage);

    const std::string context_init_sym = ContextInitSym();
    bool context_initialized = false;
    if (std::optional<RuntimeFuncInfo> init_info = GetRuntimeFuncInfo(context_init_sym))
    {
      const bool use_c_abi_aggregate_sret = true;
      ABICallResult init_abi =
          ComputeCallABI(*this, init_info->params, init_info->ret, use_c_abi_aggregate_sret);
      if (!init_abi.valid || !init_abi.func_type)
      {
        current_ctx_->ReportCodegenFailure();
        return;
      }
      llvm::FunctionType *init_ty = init_abi.func_type;
      llvm::Function *context_init_fn = module_->getFunction(context_init_sym);
      if (!context_init_fn)
      {
        context_init_fn = llvm::Function::Create(
            init_ty,
            llvm::GlobalValue::ExternalLinkage,
            context_init_sym,
            module_.get());
      }

      if (context_init_fn)
      {
        std::vector<llvm::Value *> init_args;
        init_args.reserve(init_ty->getNumParams());
        for (unsigned i = 0; i < init_ty->getNumParams(); ++i)
        {
          llvm::Type *param_ty = init_ty->getParamType(i);
          llvm::Value *arg = nullptr;
          if (init_abi.has_sret && i == 0)
          {
            arg = CoerceTo(builder, ctx_storage, param_ty);
          }
          if (!arg)
          {
            current_ctx_->ReportCodegenFailure();
            return;
          }
          init_args.push_back(arg);
        }
        llvm::CallInst *init_call =
            builder->CreateCall(init_ty, context_init_fn, init_args);
        init_call->setCallingConv(context_init_fn->getCallingConv());
        context_initialized = true;

        if (!init_abi.has_sret && !init_call->getType()->isVoidTy())
        {
          llvm::Value *out_ptr = CoerceTo(
              builder,
              ctx_storage,
              llvm::PointerType::get(init_call->getType(), 0));
          if (out_ptr && out_ptr->getType()->isPointerTy())
          {
            builder->CreateStore(init_call, out_ptr);
          }
        }
      }
    }

    if (!context_initialized)
    {
      llvm::Function *context_init_fn = module_->getFunction(context_init_sym);
      if (!context_init_fn)
      {
        llvm::FunctionType *init_ty =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                    {GetOpaquePtr()},
                                    false);
        context_init_fn = llvm::Function::Create(
            init_ty,
            llvm::GlobalValue::ExternalLinkage,
            context_init_sym,
            module_.get());
      }
      if (context_init_fn)
      {
        llvm::FunctionType *init_ty = context_init_fn->getFunctionType();
        std::vector<llvm::Value *> init_args;
        init_args.reserve(init_ty->getNumParams());
        for (unsigned i = 0; i < init_ty->getNumParams(); ++i)
        {
          llvm::Type *param_ty = init_ty->getParamType(i);
          llvm::Value *arg = nullptr;
          if (i == 0)
          {
            arg = CoerceTo(builder, ctx_storage, param_ty);
          }
          if (!arg)
          {
            arg = llvm::Constant::getNullValue(param_ty);
          }
          init_args.push_back(arg);
        }
        llvm::CallInst *init_call =
            builder->CreateCall(init_ty, context_init_fn, init_args);
        init_call->setCallingConv(context_init_fn->getCallingConv());
      }
    }

    const std::vector<ast::ModulePath> init_order =
        ComputeEntryInitOrder(*current_ctx_);
    for (std::size_t module_index = 0;
         module_index < init_order.size();
         ++module_index)
    {
      if (!call_lifecycle_fn(InitFn(init_order[module_index])))
      {
        return;
      }

      llvm::Value *has_panic = load_panic_flag();
      if (!has_panic)
      {
        continue;
      }

      llvm::BasicBlock *init_fail_bb =
          llvm::BasicBlock::Create(context_, "entry.init.fail", main_fn);
      llvm::BasicBlock *init_cont_bb =
          llvm::BasicBlock::Create(context_, "entry.init.cont", main_fn);
      builder->CreateCondBr(has_panic, init_fail_bb, init_cont_bb);

      builder->SetInsertPoint(init_fail_bb);
      builder->CreateBr(panic_bb);

      builder->SetInsertPoint(init_cont_bb);
    }

    const LowerCtx::ProcSigInfo *main_sig =
        current_ctx_ ? current_ctx_->LookupProcSig(*current_ctx_->main_symbol) : nullptr;
    llvm::Value *root_ctx_value = nullptr;
    if (ctx_storage_ty && !ctx_storage_ty->isVoidTy())
    {
      root_ctx_value = builder->CreateLoad(ctx_storage_ty, ctx_storage);
    }

    const analysis::ScopeContext &entry_scope = BuildScope(current_ctx_);
    auto normalize_context_type =
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
          const auto it = entry_scope.sigma.types.find(analysis::PathKeyOf(syntax_path));
          if (it != entry_scope.sigma.types.end())
          {
            if (const auto *alias = std::get_if<ast::TypeAliasDecl>(&it->second))
            {
              const auto lowered = analysis::LowerType(entry_scope, alias->type);
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

    auto entry_context_field_value =
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
        const auto size = ::cursive::analysis::layout::SizeOf(entry_scope, field.type).value_or(0u);
        if (std::string_view(field.name) == field_name)
        {
          if (size == 0u)
          {
            llvm::Type *field_ty = GetLLVMType(field.type);
            return field_ty && !field_ty->isVoidTy()
                       ? llvm::Constant::getNullValue(field_ty)
                       : nullptr;
          }
          return ctx_value
                     ? irb.CreateExtractValue(
                           ctx_value, {static_cast<unsigned>(extract_index)})
                     : nullptr;
        }
        if (size != 0u)
        {
          ++extract_index;
        }
      }
      return nullptr;
    };

    auto build_entry_context_bundle =
        [&](auto &&self,
            llvm::IRBuilder<> &irb,
            analysis::TypeRef target_type,
            std::string_view field_name,
            llvm::Value *root_ctx_ptr,
            llvm::Value *root_ctx_loaded) -> llvm::Value * {
      analysis::TypeRef cur = normalize_context_type(normalize_context_type, target_type, 0u);
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

            llvm::Value *context_arg =
                abi.param_kinds[0] == PassKind::ByRef ? root_ctx_ptr : root_ctx_loaded;
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
        return entry_context_field_value(irb, root_ctx_loaded, field_name);
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
                analysis::LookupRecordDecl(entry_scope, path->path))
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
            auto lowered = analysis::LowerType(entry_scope, field->type);
            if (!lowered.ok || !lowered.type)
            {
              continue;
            }
            llvm::Value *field_value = self(
                self, irb, lowered.type, field->name, root_ctx_ptr, root_ctx_loaded);
            const auto field_size = ::cursive::analysis::layout::SizeOf(entry_scope, lowered.type).value_or(0u);
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
            aggregate = irb.CreateInsertValue(aggregate, field_value, {insert_index++});
          }
          return aggregate;
        }
      }

      return entry_context_field_value(irb, root_ctx_loaded, field_name);
    };

    if (callee_ty->getNumParams() >= 1)
    {
      llvm::Type *param_ty = callee_ty->getParamType(0);
      llvm::Value *context_arg_value = nullptr;
      if (main_sig && !main_sig->params.empty() && main_sig->params[0].type)
      {
        context_arg_value = build_entry_context_bundle(
            build_entry_context_bundle,
            *builder,
            main_sig->params[0].type,
            "",
            ctx_storage,
            root_ctx_value);
      }
      if (!context_arg_value)
      {
        context_arg_value = root_ctx_value;
      }

      if (param_ty->isPointerTy())
      {
        llvm::Value *arg = nullptr;
        if (main_sig && !main_sig->params.empty() && main_sig->params[0].type)
        {
          analysis::TypeRef normalized_main_ctx =
              normalize_context_type(
                  normalize_context_type, main_sig->params[0].type, 0u);
          llvm::Type *main_ctx_ll = normalized_main_ctx
                                        ? GetLLVMType(normalized_main_ctx)
                                        : nullptr;
          if (main_ctx_ll && !main_ctx_ll->isVoidTy() &&
              normalized_main_ctx &&
              !analysis::TypeEquiv(
                   normalized_main_ctx, analysis::MakeTypePath({"Context"}))
                   .equiv)
          {
            llvm::AllocaInst *bundle_storage =
                builder->CreateAlloca(main_ctx_ll, nullptr, "entry_ctx_bundle");
            if (context_arg_value && context_arg_value->getType() == main_ctx_ll)
            {
              builder->CreateStore(context_arg_value, bundle_storage);
              arg = CoerceTo(builder, bundle_storage, param_ty);
            }
          }
        }
        if (!arg)
        {
          arg = CoerceTo(builder, ctx_storage, param_ty);
        }
        if (!arg)
        {
          current_ctx_->ReportCodegenFailure();
          return;
        }
        call_args[0] = arg;
      }
      else if (context_arg_value)
      {
        llvm::Value *arg = CoerceTo(builder, context_arg_value, param_ty);
        if (!arg && context_arg_value->getType() == param_ty)
        {
          arg = context_arg_value;
        }
        if (!arg)
        {
          current_ctx_->ReportCodegenFailure();
          return;
        }
        call_args[0] = arg;
      }
    }
    if (callee_ty->getNumParams() >= 2)
    {
      llvm::Type *param_ty = callee_ty->getParamType(1);
      if (param_ty->isPointerTy())
      {
        llvm::Value *arg = CoerceTo(builder, panic_ptr, param_ty);
        if (!arg)
        {
          current_ctx_->ReportCodegenFailure();
          return;
        }
        call_args[1] = arg;
      }
    }

    for (llvm::Value *arg : call_args)
    {
      if (!arg)
      {
        current_ctx_->ReportCodegenFailure();
        return;
      }
    }

    llvm::CallInst *call = builder->CreateCall(callee_ty, cursive_main, call_args);
    call->setCallingConv(cursive_main->getCallingConv());

    llvm::Value *exit_code = nullptr;
    if (call->getType()->isVoidTy())
    {
      exit_code = llvm::ConstantInt::get(i32_ty, 0);
    }
    else
    {
      exit_code = CoerceTo(builder, call, i32_ty);
      if (!exit_code)
      {
        exit_code = llvm::ConstantInt::get(i32_ty, 0);
      }
    }

    llvm::AllocaInst *deinit_panic_seen =
        builder->CreateAlloca(i8_ty, nullptr, "entry_deinit_panic_seen");
    llvm::AllocaInst *deinit_panic_code =
        builder->CreateAlloca(i32_ty, nullptr, "entry_deinit_panic_code");
    builder->CreateStore(llvm::ConstantInt::get(i8_ty, 0), deinit_panic_seen);
    builder->CreateStore(llvm::ConstantInt::get(i32_ty, 0), deinit_panic_code);

    llvm::BasicBlock *deinit_bb =
        llvm::BasicBlock::Create(context_, "entry.deinit", main_fn);
    if (llvm::Value *has_panic = load_panic_flag())
    {
      builder->CreateCondBr(has_panic, panic_bb, deinit_bb);
    }
    else
    {
      builder->CreateBr(deinit_bb);
    }

    builder->SetInsertPoint(panic_bb);
    llvm::Value *panic_code = load_panic_code();
    panic_code = CoerceTo(builder, panic_code, i32_ty);
    if (!panic_code)
    {
      panic_code = llvm::ConstantInt::get(i32_ty, 1);
    }
    if (runtime_panic_fn)
    {
      llvm::FunctionType *panic_ty = runtime_panic_fn->getFunctionType();
      llvm::Value *panic_arg = CoerceTo(builder, panic_code, panic_ty->getParamType(0));
      if (!panic_arg)
      {
        panic_arg = llvm::Constant::getNullValue(panic_ty->getParamType(0));
      }
      llvm::CallInst *panic_call =
          builder->CreateCall(panic_ty, runtime_panic_fn, {panic_arg});
      panic_call->setCallingConv(runtime_panic_fn->getCallingConv());
      builder->CreateUnreachable();
    }
    else
    {
      llvm::Function *runtime_exit_fn = module_->getFunction(BuiltinSymSystemExit());
      if (!runtime_exit_fn)
      {
        llvm::FunctionType *exit_ty =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                    {i32_ty},
                                    false);
        runtime_exit_fn = llvm::Function::Create(
            exit_ty,
            llvm::GlobalValue::ExternalLinkage,
            BuiltinSymSystemExit(),
            module_.get());
        runtime_exit_fn->setCallingConv(llvm::CallingConv::C);
      }
      builder->CreateCall(runtime_exit_fn->getFunctionType(),
                          runtime_exit_fn,
                          {panic_code});
      builder->CreateUnreachable();
    }

    builder->SetInsertPoint(deinit_bb);
    auto capture_deinit_panic = [&]()
    {
      llvm::Value *has_panic = load_panic_flag();
      if (!has_panic)
      {
        return;
      }

      llvm::BasicBlock *capture_bb =
          llvm::BasicBlock::Create(context_, "entry.deinit.panic.capture", main_fn);
      llvm::BasicBlock *cont_bb =
          llvm::BasicBlock::Create(context_, "entry.deinit.panic.cont", main_fn);
      builder->CreateCondBr(has_panic, capture_bb, cont_bb);

      builder->SetInsertPoint(capture_bb);
      llvm::Value *seen = builder->CreateLoad(i8_ty, deinit_panic_seen);
      llvm::Value *already_seen =
          builder->CreateICmpNE(seen, llvm::ConstantInt::get(i8_ty, 0));
      llvm::Value *code = load_panic_code();
      if (!code)
      {
        code = llvm::ConstantInt::get(i32_ty, 1);
      }

      llvm::BasicBlock *store_bb =
          llvm::BasicBlock::Create(context_, "entry.deinit.panic.store", main_fn);
      llvm::BasicBlock *clear_bb =
          llvm::BasicBlock::Create(context_, "entry.deinit.panic.clear", main_fn);
      builder->CreateCondBr(already_seen, clear_bb, store_bb);

      builder->SetInsertPoint(store_bb);
      builder->CreateStore(llvm::ConstantInt::get(i8_ty, 1), deinit_panic_seen);
      llvm::Value *stored_code = CoerceTo(builder, code, i32_ty);
      if (!stored_code)
      {
        stored_code = llvm::ConstantInt::get(i32_ty, 1);
      }
      builder->CreateStore(stored_code, deinit_panic_code);
      builder->CreateBr(clear_bb);

      builder->SetInsertPoint(clear_bb);
      clear_entry_panic_record();
      builder->CreateBr(cont_bb);

      builder->SetInsertPoint(cont_bb);
    };

    for (auto it = init_order.rbegin(); it != init_order.rend(); ++it)
    {
      const std::string deinit_sym = DeinitFn(*it);
      llvm::Function *deinit_fn = nullptr;
      if (const auto fn_it = functions_.find(deinit_sym); fn_it != functions_.end())
      {
        deinit_fn = fn_it->second;
      }
      else
      {
        deinit_fn = module_->getFunction(deinit_sym);
      }
      if (!deinit_fn)
      {
        llvm::FunctionType *deinit_ty = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context_),
            {GetOpaquePtr()},
            false);
        deinit_fn = llvm::Function::Create(
            deinit_ty,
            llvm::GlobalValue::ExternalLinkage,
            deinit_sym,
            module_.get());
      }

      if (deinit_fn)
      {
        llvm::FunctionType *deinit_ty = deinit_fn->getFunctionType();
        std::vector<llvm::Value *> deinit_args;
        deinit_args.reserve(deinit_ty->getNumParams());
        for (unsigned i = 0; i < deinit_ty->getNumParams(); ++i)
        {
          llvm::Type *param_ty = deinit_ty->getParamType(i);
          llvm::Value *arg = nullptr;
          if (i == 0)
          {
            arg = CoerceTo(builder, panic_ptr, param_ty);
          }
          if (!arg)
          {
            arg = llvm::Constant::getNullValue(param_ty);
          }
          deinit_args.push_back(arg);
        }
        llvm::CallInst *deinit_call =
            builder->CreateCall(deinit_ty, deinit_fn, deinit_args);
        deinit_call->setCallingConv(deinit_fn->getCallingConv());
        capture_deinit_panic();
      }
      else if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
    }

    llvm::Value *deinit_seen =
        builder->CreateLoad(i8_ty, deinit_panic_seen);
    llvm::Value *deinit_had_panic =
        builder->CreateICmpNE(deinit_seen, llvm::ConstantInt::get(i8_ty, 0));
    llvm::BasicBlock *deinit_restore_bb =
        llvm::BasicBlock::Create(context_, "entry.deinit.panic.restore", main_fn);
    llvm::BasicBlock *ret_bb =
        llvm::BasicBlock::Create(context_, "entry.ret", main_fn);
    builder->CreateCondBr(deinit_had_panic, deinit_restore_bb, ret_bb);

    builder->SetInsertPoint(deinit_restore_bb);
    llvm::Value *captured_deinit_code =
        builder->CreateLoad(i32_ty, deinit_panic_code);
    restore_entry_panic_record(captured_deinit_code);
    builder->CreateBr(panic_bb);

    builder->SetInsertPoint(ret_bb);

    if (returns_exit_code)
    {
      builder->CreateRet(exit_code);
    }
    else
    {
      llvm::Function *runtime_exit_fn = module_->getFunction(BuiltinSymSystemExit());
      if (!runtime_exit_fn)
      {
        llvm::FunctionType *exit_ty =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                    {i32_ty},
                                    false);
        runtime_exit_fn = llvm::Function::Create(
            exit_ty,
            llvm::GlobalValue::ExternalLinkage,
            BuiltinSymSystemExit(),
            module_.get());
        runtime_exit_fn->setCallingConv(llvm::CallingConv::C);
      }
      builder->CreateCall(runtime_exit_fn->getFunctionType(),
                          runtime_exit_fn,
                          {exit_code});
      builder->CreateUnreachable();
    }
  }

  void LLVMEmitter::EmitLibraryEntryPoint()
  {
    if (!current_ctx_ || !current_ctx_->shared_library_project)
    {
      return;
    }
    if (!IsProjectEntryModule(current_ctx_))
    {
      return;
    }

    auto *i1_ty = llvm::Type::getInt1Ty(context_);
    auto *i8_ty = llvm::Type::getInt8Ty(context_);
    auto *i32_ty = llvm::Type::getInt32Ty(context_);
    auto *i64_ty = llvm::Type::getInt64Ty(context_);
    llvm::Type *opaque_ptr_ty = GetOpaquePtr();
    llvm::Type *panic_record_ty = GetLLVMType(PanicRecordType());
    if (!opaque_ptr_ty || !panic_record_ty)
    {
      if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
      return;
    }

    llvm::Function *entry_fn = module_->getFunction(LibraryEntrySym());
    if (!entry_fn)
    {
      llvm::FunctionType *entry_ty = llvm::FunctionType::get(
          i32_ty, {opaque_ptr_ty, i32_ty, opaque_ptr_ty}, false);
      entry_fn = llvm::Function::Create(entry_ty,
                                        llvm::GlobalValue::ExternalLinkage,
                                        LibraryEntrySym(),
                                        module_.get());
      entry_fn->setCallingConv(llvm::CallingConv::C);
    }
    if (!entry_fn->empty())
    {
      return;
    }

    const auto attached_symbol =
        std::string(project::ActiveLanguageProfile().library_attached_symbol);
    llvm::GlobalVariable *attached_gv = module_->getNamedGlobal(attached_symbol);
    if (!attached_gv)
    {
      attached_gv = new llvm::GlobalVariable(*module_,
                                             i1_ty,
                                             false,
                                             llvm::GlobalValue::InternalLinkage,
                                             llvm::ConstantInt::getFalse(context_),
                                             attached_symbol);
    }

    llvm::BasicBlock *entry_bb = llvm::BasicBlock::Create(context_, "entry", entry_fn);
    llvm::BasicBlock *attach_bb =
        llvm::BasicBlock::Create(context_, "dll.attach", entry_fn);
    llvm::BasicBlock *detach_bb =
        llvm::BasicBlock::Create(context_, "dll.detach", entry_fn);
    llvm::BasicBlock *other_bb =
        llvm::BasicBlock::Create(context_, "dll.other", entry_fn);
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    builder->SetInsertPoint(entry_bb);

    llvm::Argument *reason_arg = entry_fn->getArg(1);
    reason_arg->setName("fdwReason");

    llvm::SwitchInst *dispatch =
        builder->CreateSwitch(reason_arg, other_bb, 2);
    dispatch->addCase(llvm::ConstantInt::get(i32_ty, 1), attach_bb);
    dispatch->addCase(llvm::ConstantInt::get(i32_ty, 0), detach_bb);

    auto panic_offsets = [&]() {
      std::uint64_t flag_offset = 0;
      std::uint64_t code_offset = 4;
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
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

    auto panic_field_ptr = [&](llvm::IRBuilder<> &irb,
                               llvm::Value *panic_ptr,
                               std::uint64_t offset,
                               llvm::Type *field_ty) -> llvm::Value * {
      llvm::Value *panic_i8 =
          CoerceTo(&irb, panic_ptr, llvm::PointerType::get(i8_ty, 0));
      if (!panic_i8)
      {
        panic_i8 =
            irb.CreateBitCast(panic_ptr, llvm::PointerType::get(i8_ty, 0));
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
      irb.CreateStore(llvm::ConstantInt::get(i8_ty, 0),
                      panic_field_ptr(irb, panic_ptr, panic_offsets.first, i8_ty));
      irb.CreateStore(llvm::ConstantInt::get(i32_ty, 0),
                      panic_field_ptr(irb, panic_ptr, panic_offsets.second, i32_ty));
    };

    auto load_panic_flag = [&](llvm::IRBuilder<> &irb,
                               llvm::Value *panic_ptr) -> llvm::Value * {
      return irb.CreateICmpNE(
          irb.CreateLoad(
              i8_ty,
              panic_field_ptr(irb, panic_ptr, panic_offsets.first, i8_ty)),
          llvm::ConstantInt::get(i8_ty, 0));
    };

    auto load_panic_code = [&](llvm::IRBuilder<> &irb,
                               llvm::Value *panic_ptr) -> llvm::Value * {
      return irb.CreateLoad(
          i32_ty,
          panic_field_ptr(irb, panic_ptr, panic_offsets.second, i32_ty));
    };

    auto store_panic_record = [&](llvm::IRBuilder<> &irb,
                                  llvm::Value *panic_ptr,
                                  llvm::Value *code) {
      llvm::Value *stored_code = CoerceTo(&irb, code, i32_ty);
      if (!stored_code)
      {
        stored_code = llvm::ConstantInt::get(i32_ty, 1);
      }
      irb.CreateStore(llvm::ConstantInt::get(i8_ty, 1),
                      panic_field_ptr(irb, panic_ptr, panic_offsets.first, i8_ty));
      irb.CreateStore(stored_code,
                      panic_field_ptr(irb, panic_ptr, panic_offsets.second, i32_ty));
    };

    auto ensure_proc_fn = [&](const std::string &sym) -> llvm::Function * {
      const LowerCtx::ProcSigInfo *sig =
          current_ctx_ ? current_ctx_->LookupProcSig(sym) : nullptr;
      if (!sig)
      {
        if (current_ctx_)
        {
          current_ctx_->ReportCodegenFailure();
        }
        return nullptr;
      }
      ABICallResult abi = ComputeCallABI(*this, sig->params, sig->ret);
      if (!abi.valid || !abi.func_type)
      {
        if (current_ctx_)
        {
          current_ctx_->ReportCodegenFailure();
        }
        return nullptr;
      }
      llvm::Function *fn = module_->getFunction(sym);
      if (!fn)
      {
        fn = llvm::Function::Create(abi.func_type,
                                    llvm::GlobalValue::ExternalLinkage,
                                    sym,
                                    module_.get());
      }
      fn->setCallingConv(CallingConvForAbi(sig->abi));
      return fn;
    };

    auto call_proc_with_panic = [&](llvm::IRBuilder<> &irb,
                                    const std::string &sym,
                                    llvm::Value *panic_ptr) {
      const LowerCtx::ProcSigInfo *sig =
          current_ctx_ ? current_ctx_->LookupProcSig(sym) : nullptr;
      llvm::Function *fn = ensure_proc_fn(sym);
      if (!sig || !fn)
      {
        return;
      }
      ABICallResult abi = ComputeCallABI(*this, sig->params, sig->ret);
      std::vector<llvm::Value *> args;
      args.reserve(fn->arg_size());
      for (unsigned i = 0; i < fn->arg_size(); ++i)
      {
        llvm::Value *arg = nullptr;
        if (!abi.param_indices.empty() && abi.param_indices.back().has_value() &&
            *abi.param_indices.back() == i)
        {
          arg = CoerceTo(&irb, panic_ptr, fn->getFunctionType()->getParamType(i));
          if (!arg && panic_ptr &&
              panic_ptr->getType() == fn->getFunctionType()->getParamType(i))
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
      llvm::CallInst *call =
          irb.CreateCall(fn->getFunctionType(), fn, args);
      call->setCallingConv(fn->getCallingConv());
    };

    builder->SetInsertPoint(attach_bb);
    llvm::LoadInst *attached_before = builder->CreateLoad(i1_ty, attached_gv);
    llvm::BasicBlock *attach_work_bb =
        llvm::BasicBlock::Create(context_, "dll.attach.work", entry_fn);
    llvm::BasicBlock *attach_done_bb =
        llvm::BasicBlock::Create(context_, "dll.attach.done", entry_fn);
    builder->CreateCondBr(attached_before, attach_done_bb, attach_work_bb);

    builder->SetInsertPoint(attach_done_bb);
    builder->CreateRet(llvm::ConstantInt::get(i32_ty, 1));

    builder->SetInsertPoint(attach_work_bb);
    llvm::Value *panic_record_ptr = GetSharedLibraryImagePanicPtr();
    if (!panic_record_ptr)
    {
      if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
      builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));
      return;
    }
    clear_panic_record(*builder, panic_record_ptr);

    for (std::size_t module_index = 0;
         module_index < current_ctx_->init_order.size();
         ++module_index)
    {
      const auto &module_path = current_ctx_->init_order[module_index];
      call_proc_with_panic(*builder, InitFn(module_path), panic_record_ptr);
      llvm::BasicBlock *cont_bb =
          llvm::BasicBlock::Create(context_, "dll.attach.cont", entry_fn);
      llvm::BasicBlock *fail_bb =
          llvm::BasicBlock::Create(context_, "dll.attach.fail", entry_fn);
      builder->CreateCondBr(load_panic_flag(*builder, panic_record_ptr),
                            fail_bb,
                            cont_bb);
      builder->SetInsertPoint(fail_bb);
      clear_panic_record(*builder, panic_record_ptr);
      for (std::size_t deinit_index = module_index; deinit_index > 0; --deinit_index)
      {
        const auto &deinit_path = current_ctx_->init_order[deinit_index - 1];
        call_proc_with_panic(*builder, DeinitFn(deinit_path), panic_record_ptr);
        clear_panic_record(*builder, panic_record_ptr);
      }
      builder->CreateStore(llvm::ConstantInt::getFalse(context_), attached_gv);
      builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));
      builder->SetInsertPoint(cont_bb);
    }

    builder->CreateStore(llvm::ConstantInt::getTrue(context_), attached_gv);
    builder->CreateRet(llvm::ConstantInt::get(i32_ty, 1));

    builder->SetInsertPoint(detach_bb);
    llvm::LoadInst *attached_now = builder->CreateLoad(i1_ty, attached_gv);
    llvm::BasicBlock *detach_work_bb =
        llvm::BasicBlock::Create(context_, "dll.detach.work", entry_fn);
    llvm::BasicBlock *detach_done_bb =
        llvm::BasicBlock::Create(context_, "dll.detach.done", entry_fn);
    builder->CreateCondBr(attached_now, detach_work_bb, detach_done_bb);

    builder->SetInsertPoint(detach_work_bb);
    clear_panic_record(*builder, panic_record_ptr);
    llvm::AllocaInst *detach_panic_seen =
        builder->CreateAlloca(i1_ty, nullptr, "dll_detach_panic_seen");
    llvm::AllocaInst *detach_panic_code =
        builder->CreateAlloca(i32_ty, nullptr, "dll_detach_panic_code");
    builder->CreateStore(llvm::ConstantInt::getFalse(context_), detach_panic_seen);
    builder->CreateStore(llvm::ConstantInt::get(i32_ty, 0), detach_panic_code);

    auto capture_detach_panic = [&](llvm::IRBuilder<> &irb) {
      llvm::BasicBlock *capture_bb =
          llvm::BasicBlock::Create(context_, "dll.detach.panic.capture", entry_fn);
      llvm::BasicBlock *cont_bb =
          llvm::BasicBlock::Create(context_, "dll.detach.cont", entry_fn);
      irb.CreateCondBr(load_panic_flag(irb, panic_record_ptr),
                       capture_bb,
                       cont_bb);

      irb.SetInsertPoint(capture_bb);
      llvm::Value *already_seen =
          irb.CreateLoad(i1_ty, detach_panic_seen);
      llvm::Value *panic_code = load_panic_code(irb, panic_record_ptr);
      llvm::BasicBlock *store_bb =
          llvm::BasicBlock::Create(context_, "dll.detach.panic.store", entry_fn);
      llvm::BasicBlock *clear_bb =
          llvm::BasicBlock::Create(context_, "dll.detach.panic.clear", entry_fn);
      irb.CreateCondBr(already_seen, clear_bb, store_bb);

      irb.SetInsertPoint(store_bb);
      irb.CreateStore(llvm::ConstantInt::getTrue(context_), detach_panic_seen);
      irb.CreateStore(panic_code ? panic_code : llvm::ConstantInt::get(i32_ty, 1),
                      detach_panic_code);
      irb.CreateBr(clear_bb);

      irb.SetInsertPoint(clear_bb);
      clear_panic_record(irb, panic_record_ptr);
      irb.CreateBr(cont_bb);

      irb.SetInsertPoint(cont_bb);
    };

    for (auto it = current_ctx_->init_order.rbegin();
         it != current_ctx_->init_order.rend();
         ++it)
    {
      const auto &module_path = *it;
      call_proc_with_panic(*builder, DeinitFn(module_path), panic_record_ptr);
      capture_detach_panic(*builder);
    }
    builder->CreateStore(llvm::ConstantInt::getFalse(context_), attached_gv);
    llvm::BasicBlock *detach_panic_bb =
        llvm::BasicBlock::Create(context_, "dll.detach.fail", entry_fn);
    llvm::BasicBlock *detach_success_bb =
        llvm::BasicBlock::Create(context_, "dll.detach.success", entry_fn);
    builder->CreateCondBr(builder->CreateLoad(i1_ty, detach_panic_seen),
                          detach_panic_bb,
                          detach_success_bb);

    builder->SetInsertPoint(detach_panic_bb);
    store_panic_record(*builder,
                       panic_record_ptr,
                       builder->CreateLoad(i32_ty, detach_panic_code));
    builder->CreateRet(llvm::ConstantInt::get(i32_ty, 0));

    builder->SetInsertPoint(detach_success_bb);
    builder->CreateRet(llvm::ConstantInt::get(i32_ty, 1));

    builder->SetInsertPoint(detach_done_bb);
    builder->CreateRet(llvm::ConstantInt::get(i32_ty, 1));

    builder->SetInsertPoint(other_bb);
    builder->CreateRet(llvm::ConstantInt::get(i32_ty, 1));
  }

  void LLVMEmitter::EmitPosixLibraryLifecycleHooks()
  {
    if (!current_ctx_ || !current_ctx_->shared_library_project ||
        project::ObjectFormatOf(target_profile_) != project::ObjectFormat::Elf ||
        !IsProjectEntryModule(current_ctx_))
    {
      return;
    }

    llvm::Function *entry_fn = module_->getFunction(LibraryEntrySym());
    llvm::Type *opaque_ptr_ty = GetOpaquePtr();
    if (!entry_fn || !opaque_ptr_ty)
    {
      if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
      return;
    }

    auto *i32_ty = llvm::Type::getInt32Ty(context_);
    auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);
    auto ensure_runtime_panic = [&]() -> llvm::Function * {
      llvm::Function *fn = module_->getFunction(RuntimePanicSym());
      if (!fn)
      {
        llvm::FunctionType *fn_ty =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                    {i32_ty},
                                    false);
        fn = llvm::Function::Create(fn_ty,
                                    llvm::GlobalValue::ExternalLinkage,
                                    RuntimePanicSym(),
                                    module_.get());
        fn->setCallingConv(llvm::CallingConv::C);
      }
      return fn;
    };

    auto ensure_hook = [&](const char *name) -> llvm::Function * {
      llvm::Function *fn = module_->getFunction(name);
      if (!fn)
      {
        llvm::FunctionType *fn_ty =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {}, false);
        fn = llvm::Function::Create(fn_ty,
                                    llvm::GlobalValue::InternalLinkage,
                                    name,
                                    module_.get());
        fn->setCallingConv(llvm::CallingConv::C);
      }
      return fn;
    };

    llvm::Function *ctor_fn = ensure_hook(LibraryCtorSym());
    if (ctor_fn && ctor_fn->empty())
    {
      llvm::BasicBlock *entry_bb =
          llvm::BasicBlock::Create(context_, "entry", ctor_fn);
      llvm::BasicBlock *ok_bb =
          llvm::BasicBlock::Create(context_, "ctor.ok", ctor_fn);
      llvm::BasicBlock *fail_bb =
          llvm::BasicBlock::Create(context_, "ctor.fail", ctor_fn);
      auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
      builder->SetInsertPoint(entry_bb);
      llvm::CallInst *attach_call =
          builder->CreateCall(entry_fn->getFunctionType(),
                              entry_fn,
                              {llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty),
                               llvm::ConstantInt::get(i32_ty, 1),
                               llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)});
      attach_call->setCallingConv(entry_fn->getCallingConv());
      builder->CreateCondBr(
          builder->CreateICmpNE(attach_call, llvm::ConstantInt::get(i32_ty, 0)),
          ok_bb,
          fail_bb);

      builder->SetInsertPoint(fail_bb);
      llvm::Function *runtime_panic_fn = ensure_runtime_panic();
      builder->CreateCall(runtime_panic_fn,
                          {llvm::ConstantInt::get(
                              i32_ty,
                              PanicCode(PanicReason::ForeignPre))});
      builder->CreateUnreachable();

      builder->SetInsertPoint(ok_bb);
      builder->CreateRetVoid();
    }

    llvm::Function *dtor_fn = ensure_hook(LibraryDtorSym());
    if (dtor_fn && dtor_fn->empty())
    {
      llvm::BasicBlock *entry_bb =
          llvm::BasicBlock::Create(context_, "entry", dtor_fn);
      llvm::BasicBlock *ok_bb =
          llvm::BasicBlock::Create(context_, "dtor.ok", dtor_fn);
      llvm::BasicBlock *fail_bb =
          llvm::BasicBlock::Create(context_, "dtor.fail", dtor_fn);
      auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
      builder->SetInsertPoint(entry_bb);
      llvm::CallInst *detach_call =
          builder->CreateCall(entry_fn->getFunctionType(),
                              entry_fn,
                              {llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty),
                               llvm::ConstantInt::get(i32_ty, 0),
                               llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty)});
      detach_call->setCallingConv(entry_fn->getCallingConv());
      builder->CreateCondBr(
          builder->CreateICmpNE(detach_call, llvm::ConstantInt::get(i32_ty, 0)),
          ok_bb,
          fail_bb);

      builder->SetInsertPoint(fail_bb);
      llvm::Function *runtime_panic_fn = ensure_runtime_panic();
      builder->CreateCall(runtime_panic_fn,
                          {llvm::ConstantInt::get(
                              i32_ty,
                              PanicCode(PanicReason::ForeignPost))});
      builder->CreateUnreachable();

      builder->SetInsertPoint(ok_bb);
      builder->CreateRetVoid();
    }

    if (ctor_fn)
    {
      llvm::appendToGlobalCtors(*module_, ctor_fn, 65535);
    }
    if (dtor_fn)
    {
      llvm::appendToGlobalDtors(*module_, dtor_fn, 65535);
    }
  }

  void LLVMEmitter::ApplySharedLibraryDefinitionVisibility()
  {
    if (!current_ctx_ || !current_ctx_->shared_library_project ||
        current_ctx_->shared_library_export_symbols.empty() ||
        project::ObjectFormatOf(target_profile_) != project::ObjectFormat::Elf)
    {
      return;
    }

    std::unordered_set<std::string> exported(
        current_ctx_->shared_library_export_symbols.begin(),
        current_ctx_->shared_library_export_symbols.end());

    auto apply_visibility = [&](llvm::GlobalValue &value) {
      if (value.isDeclaration() || value.hasLocalLinkage())
      {
        return;
      }
      if (exported.find(value.getName().str()) != exported.end())
      {
        value.setVisibility(llvm::GlobalValue::DefaultVisibility);
        return;
      }
      value.setVisibility(llvm::GlobalValue::HiddenVisibility);
    };

    for (auto &fn : *module_)
    {
      apply_visibility(fn);
    }
    for (auto &global : module_->globals())
    {
      apply_visibility(global);
    }
  }

} // namespace cursive::codegen
