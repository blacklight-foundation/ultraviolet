// =============================================================================
// File: 05_codegen/llvm/emit/ir/value/evaluate.cpp
// Canonical owner for LLVM IR value materialization.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

#include "04_analysis/layout/layout.h"
#include "05_codegen/symbols/mangle.h"

namespace cursive::codegen {

using namespace emit_detail;

namespace {

std::optional<std::string> ProcedureSymbolForPath(
    const LowerCtx &ctx,
    const std::vector<std::string> &module_path,
    const std::string &name)
{
  if (!ctx.sigma)
  {
    return std::nullopt;
  }
  for (const auto &module : ctx.sigma->mods)
  {
    if (module.path != module_path)
    {
      continue;
    }
    for (const auto &item : module.items)
    {
      if (const auto *proc = std::get_if<ast::ProcedureDecl>(&item))
      {
        if (proc->name == name)
        {
          return MangleProc(module.path, *proc);
        }
      }
    }
    break;
  }
  return std::nullopt;
}

} // namespace

  // Evaluate an IRValue to an llvm::Value*
  llvm::Value *LLVMEmitter::EvaluateIRValue(const IRValue &val)
  {
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());

    auto lookup_value_type = [&](const IRValue &value) -> analysis::TypeRef
    {
      if (const LowerCtx *ctx = GetCurrentCtx())
      {
        if (analysis::TypeRef type = ctx->LookupValueType(value))
        {
          return type;
        }
      }
      if (value.kind == IRValue::Kind::Local)
      {
        const auto it = local_types_.find(value.name);
        if (it != local_types_.end())
        {
          return it->second;
        }
      }
      return nullptr;
    };

    auto default_for = [&](const IRValue &value) -> llvm::Value *
    {
      if (analysis::TypeRef type = lookup_value_type(value))
      {
        if (llvm::Type *llvm_ty = GetLLVMType(type))
        {
          return llvm::Constant::getNullValue(llvm_ty);
        }
      }
      return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0);
    };

    if (val.kind == IRValue::Kind::Opaque)
    {
      if (llvm::Value *cached = GetTempValue(val))
      {
        return cached;
      }
      if (llvm::Value *storage = GetTempStorage(val))
      {
        analysis::TypeRef storage_type = lookup_value_type(val);
        llvm::Type *storage_llvm_ty = storage_type ? GetLLVMType(storage_type) : nullptr;
        if (storage_llvm_ty && !storage_llvm_ty->isVoidTy())
        {
          llvm::Value *typed_ptr = storage;
          llvm::Type *expected_ptr_ty = llvm::PointerType::get(storage_llvm_ty, 0);
          if (typed_ptr->getType() != expected_ptr_ty)
          {
            typed_ptr = builder->CreateBitCast(typed_ptr, expected_ptr_ty);
          }
          return builder->CreateLoad(storage_llvm_ty, typed_ptr);
        }
      }
    }

    auto eval_key_for = [](const IRValue &value) -> std::string
    {
      std::string key;
      key.reserve(64 + value.name.size() + (value.bytes.size() * 2));
      key += std::to_string(static_cast<int>(value.kind));
      key.push_back(':');
      key += value.name;
      key.push_back(':');
      for (std::uint8_t byte : value.bytes)
      {
        constexpr char kHex[] = "0123456789abcdef";
        key.push_back(kHex[(byte >> 4) & 0x0f]);
        key.push_back(kHex[byte & 0x0f]);
      }
      return key;
    };

    thread_local std::vector<std::string> eval_stack;
    const bool track_eval_cycle = (val.kind == IRValue::Kind::Opaque);
    std::optional<std::string> eval_key;
    if (track_eval_cycle)
    {
      eval_key = eval_key_for(val);
      if (std::find(eval_stack.begin(), eval_stack.end(), *eval_key) != eval_stack.end())
      {
        if (core::IsDebugEnabled("obj"))
        {
          std::cerr << "[cursive] recursive EvaluateIRValue cycle for key=" << *eval_key;
          if (!eval_stack.empty())
          {
            std::cerr << " stack=[";
            for (std::size_t i = 0; i < eval_stack.size(); ++i)
            {
              if (i != 0)
              {
                std::cerr << " -> ";
              }
              std::cerr << eval_stack[i];
            }
            std::cerr << "]";
          }
          std::cerr << "\n";
        }
        return default_for(val);
      }

      eval_stack.push_back(*eval_key);
    }

    struct EvalStackPopGuard
    {
      std::vector<std::string> *stack = nullptr;
      bool active = false;
      ~EvalStackPopGuard()
      {
        if (active && stack && !stack->empty())
        {
          stack->pop_back();
        }
      }
    } eval_stack_pop_guard{&eval_stack, track_eval_cycle};

    auto resolve_symbol = [&](const std::string &name) -> std::string
    {
      if (GetFunction(name) || GetGlobal(name) ||
          module_->getFunction(name) || module_->getNamedGlobal(name))
      {
        return name;
      }
      if (auto alias = LookupSymbolAlias(name))
      {
        if (GetFunction(*alias) || GetGlobal(*alias) ||
            module_->getFunction(*alias) || module_->getNamedGlobal(*alias))
        {
          return *alias;
        }
        if (HasHostedStateSlot(*alias))
        {
          return *alias;
        }
        if (const LowerCtx *active_ctx = GetCurrentCtx())
        {
          if (active_ctx->LookupStaticType(*alias) ||
              active_ctx->LookupProcSig(*alias))
          {
            return *alias;
          }
        }
      }
      return name;
    };

    switch (val.kind)
    {
    case IRValue::Kind::Local:
    {
      llvm::Value *local = GetLocalBindStorage(val.name);
      if (core::IsDebugEnabled("obj") &&
          (val.name == "unit_value" || val.name == "tuple_value" || val.name == "record_value"))
      {
        std::cerr << "[enum-local] name=" << val.name
                  << " found=" << (local ? "yes" : "no");
        if (local)
        {
          std::cerr << " local_ty=" << LLVMValueRepr(local);
        }
        std::cerr << "\n";
      }
      if (!local)
      {
        return nullptr;
      }
      analysis::TypeRef desired_type = nullptr;
      if (const LowerCtx *ctx = GetCurrentCtx())
      {
        desired_type = ctx->LookupValueType(val);
      }
      analysis::TypeRef storage_type = nullptr;
      const auto storage_type_it = local_types_.find(val.name);
      if (storage_type_it != local_types_.end())
      {
        storage_type = storage_type_it->second;
      }
      auto coerce_refined_local = [&](llvm::Value *loaded) -> llvm::Value *
      {
        if (!loaded || !desired_type || !storage_type)
        {
          return loaded;
        }
        llvm::Type *desired_llvm_ty = GetLLVMType(desired_type);
        if (!desired_llvm_ty || desired_llvm_ty->isVoidTy())
        {
          return loaded;
        }
        if (llvm::Value *coerced = CoerceToTyped(
                *this,
                builder,
                loaded,
                desired_llvm_ty,
                storage_type,
                desired_type))
        {
          return coerced;
        }
        return loaded;
      };
      if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(local))
      {
        llvm::Value *loaded = builder->CreateLoad(alloca->getAllocatedType(), alloca);
        return coerce_refined_local(loaded);
      }
      if (local->getType()->isPointerTy())
      {
        if (storage_type)
        {
          if (llvm::Type *local_ty = GetLLVMType(storage_type))
          {
            llvm::Value *typed_ptr = local;
            llvm::Type *expected_ptr_ty = llvm::PointerType::get(local_ty, 0);
            if (typed_ptr->getType() != expected_ptr_ty)
            {
              typed_ptr = builder->CreateBitCast(typed_ptr, expected_ptr_ty);
            }
            llvm::Value *loaded = builder->CreateLoad(local_ty, typed_ptr);
            return coerce_refined_local(loaded);
          }
        }
      }
      return local;
    }

    case IRValue::Kind::Symbol:
    {
      const std::string symbol = resolve_symbol(val.name);
      auto configure_imported_static_decl =
          [&](llvm::GlobalVariable *decl) -> llvm::GlobalVariable * {
        if (!decl) {
          return nullptr;
        }
        const LowerCtx *active_ctx = GetCurrentCtx();
        if (!active_ctx || active_ctx->module_path.empty()) {
          return decl;
        }
        if (project::ObjectFormatOf(target_profile_) != project::ObjectFormat::Coff) {
          return decl;
        }
        const auto *owner_module = active_ctx->LookupStaticModule(symbol);
        if (!owner_module || owner_module->empty()) {
          return decl;
        }
        const std::string &current_root = active_ctx->module_path.front();
        const std::string &owner_root = owner_module->front();
        const bool imported_shared_library_data =
            owner_root != current_root &&
            active_ctx->shared_library_assembly_names.contains(owner_root);
        if (!imported_shared_library_data) {
          return decl;
        }
        decl->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
        return decl;
      };
      auto load_global_value = [&](llvm::GlobalVariable *global_var) -> llvm::Value *
      {
        if (!global_var)
        {
          return nullptr;
        }
        global_var = configure_imported_static_decl(global_var);
        if (!builder->GetInsertBlock())
        {
          return global_var;
        }

        analysis::TypeRef symbol_type = analysis::StripPerm(lookup_value_type(val));
        if (!symbol_type)
        {
          symbol_type = lookup_value_type(val);
        }
        if (!symbol_type)
        {
          if (const LowerCtx *active_ctx = GetCurrentCtx())
          {
            symbol_type = active_ctx->LookupStaticType(symbol);
          }
        }
        if (symbol_type)
        {
          if (llvm::Type *symbol_ll = GetLLVMType(symbol_type))
          {
            llvm::Align load_align(1);
            if (const LowerCtx *ctx_for_align = GetCurrentCtx())
            {
              const analysis::ScopeContext &scope = BuildScope(ctx_for_align);
              if (const auto align =
                      ::cursive::analysis::layout::AlignOf(scope, symbol_type))
              {
                load_align = llvm::Align(*align);
              }
            }
            llvm::Value *typed_ptr =
                GetHostedStatePtr(symbol, symbol_ll, global_var);
            if (!typed_ptr && HasHostedStateSlot(symbol) && !global_var)
            {
              return nullptr;
            }
            if (!typed_ptr)
            {
              typed_ptr = builder->CreateBitCast(
                  global_var, llvm::PointerType::get(symbol_ll, 0));
            }
            llvm::LoadInst *loaded = builder->CreateLoad(symbol_ll, typed_ptr);
            loaded->setAlignment(load_align);
            return loaded;
          }
        }

        if (llvm::Value *hosted_ptr =
                GetHostedStatePtr(symbol, global_var->getValueType(), global_var))
        {
          llvm::LoadInst *loaded =
              builder->CreateLoad(global_var->getValueType(), hosted_ptr);
          loaded->setAlignment(global_var->getAlign().valueOrOne());
          return loaded;
        }
        if (HasHostedStateSlot(symbol) && !global_var)
        {
          return nullptr;
        }

        llvm::LoadInst *loaded =
            builder->CreateLoad(global_var->getValueType(), global_var);
        loaded->setAlignment(global_var->getAlign().valueOrOne());
        return loaded;
      };

      // Symbol can be a global variable or a function
      if (llvm::Function *func = GetFunction(symbol))
      {
        return func;
      }
      if (llvm::Function *func = module_->getFunction(symbol))
      {
        return func;
      }
      if (const LowerCtx *active_ctx = GetCurrentCtx())
      {
        if (const auto *sig = active_ctx->LookupProcSig(symbol))
        {
          const bool use_c_abi_aggregate_sret =
              sig->ffi_import ||
              active_ctx->LookupExportUnwindMode(symbol).has_value();
          ABICallResult abi = ComputeCallABI(
              *this,
              sig->params,
              sig->ret,
              use_c_abi_aggregate_sret);
          if (abi.func_type)
          {
            llvm::Function *declared = llvm::Function::Create(
                abi.func_type,
                llvm::GlobalValue::ExternalLinkage,
                symbol,
                module_.get());
            declared->setCallingConv(CallingConvForAbi(sig->abi));
            return declared;
          }
        }
      }
      if (llvm::Value *global = GetGlobal(symbol))
      {
        if (auto *global_var = llvm::dyn_cast<llvm::GlobalVariable>(global))
        {
          return load_global_value(global_var);
        }
        return global;
      }
      if (llvm::GlobalVariable *global = module_->getNamedGlobal(symbol))
      {
        return load_global_value(global);
      }
      if (const LowerCtx *active_ctx = GetCurrentCtx())
      {
        analysis::TypeRef static_type = active_ctx->LookupStaticType(symbol);
        static_type = analysis::StripPerm(static_type);
        if (!static_type)
        {
          static_type = active_ctx->LookupStaticType(symbol);
        }
        if (static_type)
        {
          if (llvm::Type *static_ll = GetLLVMType(static_type))
          {
            llvm::GlobalVariable *decl = module_->getNamedGlobal(symbol);
            if (!decl)
            {
              decl = new llvm::GlobalVariable(
                  *module_,
                  static_ll,
                  false,
                  llvm::GlobalValue::ExternalLinkage,
                  nullptr,
                  symbol);
            }
            decl = configure_imported_static_decl(decl);
            return load_global_value(decl);
          }
        }
      }
      return nullptr;
    }

    case IRValue::Kind::Immediate:
    {
      analysis::TypeRef immediate_type;
      immediate_type = analysis::StripPerm(lookup_value_type(val));
      if (!immediate_type)
      {
        immediate_type = lookup_value_type(val);
      }

      auto build_view_literal = [&](llvm::Type *view_ty, LiteralKind literal_kind) -> llvm::Value *
      {
        auto *struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(view_ty);
        if (!struct_ty || struct_ty->getNumElements() < 2)
        {
          return nullptr;
        }

        llvm::Type *ptr_elem_ty = struct_ty->getElementType(0);
        llvm::Type *len_elem_ty = struct_ty->getElementType(1);

        llvm::Constant *data_ptr = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(GetOpaquePtr()));

        if (!val.bytes.empty())
        {
          llvm::ArrayType *arr_ty =
              llvm::ArrayType::get(llvm::Type::getInt8Ty(context_), val.bytes.size());
          const std::string literal_sym = LiteralSym(literal_kind, val.bytes);
          llvm::GlobalVariable *gv = module_->getNamedGlobal(literal_sym);
          if (!gv)
          {
            llvm::Constant *arr_init =
                llvm::ConstantDataArray::get(context_, llvm::ArrayRef<std::uint8_t>(val.bytes));
            gv = new llvm::GlobalVariable(
                *module_,
                arr_ty,
                true,
                llvm::GlobalValue::InternalLinkage,
                arr_init,
                literal_sym);
            gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            gv->setAlignment(llvm::Align(1));
          }

          llvm::Constant *zero = llvm::ConstantInt::get(
              llvm::Type::getInt64Ty(context_), 0);
          llvm::Constant *indices[] = {zero, zero};
          llvm::Constant *gep = llvm::ConstantExpr::getInBoundsGetElementPtr(
              arr_ty, gv, indices);
          data_ptr = llvm::ConstantExpr::getPointerCast(gep, GetOpaquePtr());
        }

        if (ptr_elem_ty->isPointerTy() && data_ptr->getType() != ptr_elem_ty)
        {
          data_ptr = llvm::ConstantExpr::getPointerCast(data_ptr, ptr_elem_ty);
        }

        llvm::Constant *len = llvm::ConstantInt::get(
            len_elem_ty, static_cast<std::uint64_t>(val.bytes.size()));
        return llvm::ConstantStruct::get(struct_ty, {data_ptr, len});
      };
      auto build_int_literal = [&](unsigned bit_width) -> llvm::APInt
      {
        if (bit_width == 0)
        {
          bit_width = 64;
        }
        std::vector<std::uint64_t> words((bit_width + 63u) / 64u, 0u);
        const std::size_t max_bytes = words.size() * sizeof(std::uint64_t);
        const std::size_t byte_count = std::min(val.bytes.size(), max_bytes);
        for (std::size_t i = 0; i < byte_count; ++i)
        {
          const std::size_t word_index = i / sizeof(std::uint64_t);
          const std::size_t byte_index = i % sizeof(std::uint64_t);
          words[word_index] |= (static_cast<std::uint64_t>(val.bytes[i]) << (byte_index * 8u));
        }
        return llvm::APInt(bit_width, llvm::ArrayRef<std::uint64_t>(words));
      };

      const bool looks_like_string_literal =
          val.name.size() >= 2 && val.name.front() == '"' && val.name.back() == '"';
      if (looks_like_string_literal)
      {
        if (core::IsDebugEnabled("obj"))
        {
          std::fprintf(stderr,
                       "[llvm-immediate-string] name=%s bytes=%zu\n",
                       val.name.c_str(),
                       val.bytes.size());
        }
        auto *default_view_ty = llvm::StructType::get(
            context_, {GetOpaquePtr(), llvm::Type::getInt64Ty(context_)});
        if (llvm::Value *view = build_view_literal(default_view_ty, LiteralKind::String))
        {
          return view;
        }
      }

      if (immediate_type)
      {
        if (const auto *str_ty = std::get_if<analysis::TypeString>(&immediate_type->node))
        {
          if (str_ty->state.has_value() && *str_ty->state == analysis::StringState::View)
          {
            if (llvm::Type *view_ty = GetLLVMType(immediate_type))
            {
              if (llvm::Value *view = build_view_literal(view_ty, LiteralKind::String))
              {
                return view;
              }
            }
          }
        }

        if (const auto *bytes_ty = std::get_if<analysis::TypeBytes>(&immediate_type->node))
        {
          if (bytes_ty->state.has_value() && *bytes_ty->state == analysis::BytesState::View)
          {
            if (llvm::Type *view_ty = GetLLVMType(immediate_type))
            {
              if (llvm::Value *view = build_view_literal(view_ty, LiteralKind::Bytes))
              {
                return view;
              }
            }
          }
        }
      }

      if (val.name == "true")
      {
        return llvm::ConstantInt::getTrue(context_);
      }
      if (val.name == "false")
      {
        return llvm::ConstantInt::getFalse(context_);
      }
      if (immediate_type)
      {
        if (const auto *prim = std::get_if<analysis::TypePrim>(&immediate_type->node))
        {
          (void)prim;
          if (llvm::Type *prim_ty = GetLLVMType(immediate_type))
          {
            if (prim_ty->isFloatingPointTy())
            {
              std::uint64_t raw = 0;
              for (std::size_t i = 0; i < val.bytes.size() && i < 8; ++i)
              {
                raw |= static_cast<std::uint64_t>(val.bytes[i]) << (8 * i);
              }

              if (prim_ty->isHalfTy())
              {
                const std::uint16_t bits16 = static_cast<std::uint16_t>(raw & 0xFFFFu);
                llvm::APFloat fp(llvm::APFloat::IEEEhalf(),
                                 llvm::APInt(16, bits16));
                return llvm::ConstantFP::get(context_, fp);
              }
              if (prim_ty->isFloatTy())
              {
                const std::uint32_t bits32 = static_cast<std::uint32_t>(raw & 0xFFFFFFFFu);
                float native = 0.0f;
                std::memcpy(&native, &bits32, sizeof(bits32));
                return llvm::ConstantFP::get(prim_ty, static_cast<double>(native));
              }
              if (prim_ty->isDoubleTy())
              {
                double native = 0.0;
                std::memcpy(&native, &raw, sizeof(raw));
                return llvm::ConstantFP::get(prim_ty, native);
              }

              if (!val.name.empty())
              {
                auto strip_float_suffix = [](std::string text) -> std::string
                {
                  constexpr const char *kSuffixes[] = {"f16", "f32", "f64", "f"};
                  for (const char *suffix : kSuffixes)
                  {
                    const std::size_t slen = std::char_traits<char>::length(suffix);
                    if (text.size() >= slen &&
                        text.compare(text.size() - slen, slen, suffix) == 0)
                    {
                      text.resize(text.size() - slen);
                      break;
                    }
                  }
                  return text;
                };
                const std::string core = strip_float_suffix(val.name);
                try
                {
                  const double parsed = std::stod(core);
                  return llvm::ConstantFP::get(prim_ty, parsed);
                }
                catch (...)
                {
                }
              }
            }
            if (auto *int_ty = llvm::dyn_cast<llvm::IntegerType>(prim_ty))
            {
              return llvm::ConstantInt::get(int_ty, build_int_literal(int_ty->getBitWidth()));
            }
          }
        }
      }
      // Create constant from the bytes
      if (val.bytes.empty())
      {
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0);
      }
      // Interpret bytes as an integer value
      std::uint64_t word = 0;
      for (std::size_t i = 0; i < val.bytes.size() && i < 8; ++i)
      {
        word |= static_cast<std::uint64_t>(val.bytes[i]) << (8 * i);
      }
      unsigned bit_width = static_cast<unsigned>(val.bytes.size() * 8);
      if (bit_width == 0)
        bit_width = 64;
      return llvm::ConstantInt::get(
          llvm::Type::getIntNTy(context_, bit_width), word);
    }

    case IRValue::Kind::Opaque:
    {
      const LowerCtx *ctx = GetCurrentCtx();
      if (!ctx)
      {
        return nullptr;
      }
      const DerivedValueInfo *derived = ctx->LookupDerivedValue(val);
      if (!derived)
      {
        if (core::IsDebugEnabled("obj"))
        {
          llvm::Function *fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          std::cerr << "[cursive] missing derived value in "
                    << (fn ? fn->getName().str() : std::string("<no-func>"))
                    << " opaque=" << val.name
                    << " type="
                    << (lookup_value_type(val)
                            ? analysis::TypeToString(lookup_value_type(val))
                            : std::string("<null>"))
                    << "\n";
        }
        if (val.name == "null")
        {
          return llvm::ConstantPointerNull::get(
              llvm::cast<llvm::PointerType>(GetOpaquePtr()));
        }
        return nullptr;
      }

      llvm::Value *materialized = nullptr;
      const analysis::ScopeContext &scope = BuildScope(ctx);
      auto pointer_from_value = [&](llvm::Value *value) -> llvm::Value *
      {
        if (!value)
        {
          return nullptr;
        }
        if (value->getType()->isPointerTy())
        {
          return value;
        }
        if (value->getType()->isIntegerTy())
        {
          return builder->CreateIntToPtr(value, GetOpaquePtr());
        }
        return nullptr;
      };
      auto pointee_from_type = [&](analysis::TypeRef type) -> analysis::TypeRef
      {
        analysis::TypeRef current = analysis::StripPerm(type);
        if (!current)
        {
          return nullptr;
        }
        if (const auto *raw = std::get_if<analysis::TypeRawPtr>(&current->node))
        {
          if (analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, raw->element))
          {
            if (analysis::TypeRef stripped = analysis::StripPerm(resolved))
            {
              return stripped;
            }
            return resolved;
          }
          return analysis::StripPerm(raw->element);
        }
        if (const auto *ptr = std::get_if<analysis::TypePtr>(&current->node))
        {
          if (analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, ptr->element))
          {
            if (analysis::TypeRef stripped = analysis::StripPerm(resolved))
            {
              return stripped;
            }
            return resolved;
          }
          return analysis::StripPerm(ptr->element);
        }
        return ResolveAliasTypeInScope(scope, current);
      };
      auto storage_matches_semantic_type = [&](analysis::TypeRef storage_type,
                                               analysis::TypeRef semantic_type) -> bool
      {
        if (!storage_type || !semantic_type)
        {
          return true;
        }
        analysis::TypeRef lhs = analysis::StripPerm(storage_type);
        if (!lhs)
        {
          lhs = storage_type;
        }
        analysis::TypeRef rhs = analysis::StripPerm(semantic_type);
        if (!rhs)
        {
          rhs = semantic_type;
        }
        if (analysis::TypeRef resolved_lhs = ResolveAliasTypeInScope(scope, lhs))
        {
          lhs = analysis::StripPerm(resolved_lhs);
          if (!lhs)
          {
            lhs = resolved_lhs;
          }
        }
        if (analysis::TypeRef resolved_rhs = ResolveAliasTypeInScope(scope, rhs))
        {
          rhs = analysis::StripPerm(resolved_rhs);
          if (!rhs)
          {
            rhs = resolved_rhs;
          }
        }
        const auto equiv = analysis::TypeEquiv(lhs, rhs);
        return equiv.ok && equiv.equiv;
      };
      auto can_use_addressable_storage_for_field = [&](const IRValue &base_value) -> bool
      {
        if (base_value.kind != IRValue::Kind::Local)
        {
          return true;
        }
        const auto storage_type_it = local_types_.find(base_value.name);
        if (storage_type_it == local_types_.end())
        {
          return true;
        }
        return storage_matches_semantic_type(
            storage_type_it->second,
            lookup_value_type(base_value));
      };
      auto field_meta_for = [&](const IRValue &base_value,
                                std::string_view field_name) -> std::optional<FieldAccessMeta>
      {
        auto resolve_from_type = [&](analysis::TypeRef type) -> std::optional<FieldAccessMeta>
        {
          if (!type)
          {
            return std::nullopt;
          }
          analysis::TypeRef candidate = pointee_from_type(type);
          if (!candidate)
          {
            candidate = type;
          }
          return ResolveFieldAccessMeta(scope, candidate, field_name);
        };

        if (analysis::TypeRef base_type = lookup_value_type(base_value))
        {
          if (auto meta = resolve_from_type(base_type))
          {
            return meta;
          }
        }

        const DerivedValueInfo *base_derived = ctx->LookupDerivedValue(base_value);
        while (base_derived)
        {
          if (base_derived->kind == DerivedValueInfo::Kind::AddrLocal)
          {
            if (const BindingState *state = ctx->GetBindingState(base_derived->name))
            {
              if (auto meta = resolve_from_type(state->type))
              {
                return meta;
              }
            }
            break;
          }
          if (base_derived->kind == DerivedValueInfo::Kind::AddrField ||
              base_derived->kind == DerivedValueInfo::Kind::AddrTuple ||
              base_derived->kind == DerivedValueInfo::Kind::AddrIndex ||
              base_derived->kind == DerivedValueInfo::Kind::AddrDeref)
          {
            base_derived = ctx->LookupDerivedValue(base_derived->base);
            continue;
          }
          break;
        }

        return std::nullopt;
      };
      auto strip_perm = [](analysis::TypeRef type) -> analysis::TypeRef
      {
        if (!type)
        {
          return nullptr;
        }
        if (analysis::TypeRef stripped = analysis::StripPerm(type))
        {
          return stripped;
        }
        return type;
      };
      auto tuple_type_for_value = [&](const IRValue &value) -> analysis::TypeRef
      {
        analysis::TypeRef type = lookup_value_type(value);
        if (!type)
        {
          return nullptr;
        }
        if (analysis::TypeRef stripped = analysis::StripPerm(type))
        {
          type = stripped;
        }
        if (const auto *raw = std::get_if<analysis::TypeRawPtr>(&type->node))
        {
          type = analysis::StripPerm(raw->element);
        }
        else if (const auto *ptr = std::get_if<analysis::TypePtr>(&type->node))
        {
          type = analysis::StripPerm(ptr->element);
        }
        if (!type)
        {
          return nullptr;
        }
        if (analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, type))
        {
          type = analysis::StripPerm(resolved);
          if (!type)
          {
            type = resolved;
          }
        }
        return type && std::holds_alternative<analysis::TypeTuple>(type->node)
                   ? type
                   : nullptr;
      };
      auto addressable_aggregate_value = [&](llvm::Value *value) -> llvm::Value *
      {
        if (!value)
        {
          return nullptr;
        }
        if (llvm::Value *ptr = pointer_from_value(value))
        {
          return ptr;
        }
        llvm::Function *current_fn =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
        if (!current_fn)
        {
          return nullptr;
        }
        llvm::IRBuilder<> entry_builder(
            &current_fn->getEntryBlock(),
            current_fn->getEntryBlock().begin());
        llvm::AllocaInst *slot = entry_builder.CreateAlloca(value->getType());
        builder->CreateStore(value, slot);
        return slot;
      };
      auto load_tuple_element_by_offset =
          [&](llvm::Value *base,
              const analysis::TypeTuple &tuple,
              std::size_t tuple_index) -> llvm::Value *
      {
        if (tuple_index >= tuple.elements.size())
        {
          return nullptr;
        }
        const auto layout =
            ::cursive::analysis::layout::RecordLayoutOf(scope, tuple.elements);
        if (!layout.has_value() || tuple_index >= layout->offsets.size())
        {
          return nullptr;
        }
        llvm::Type *elem_ll = GetLLVMType(tuple.elements[tuple_index]);
        if (!elem_ll || elem_ll->isVoidTy())
        {
          return nullptr;
        }
        llvm::Value *base_ptr = addressable_aggregate_value(base);
        if (!base_ptr)
        {
          return nullptr;
        }
        llvm::Value *base_i8 = builder->CreateBitCast(
            base_ptr,
            llvm::PointerType::get(llvm::Type::getInt8Ty(context_), 0));
        llvm::Value *field_i8 = builder->CreateGEP(
            llvm::Type::getInt8Ty(context_),
            base_i8,
            llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(context_),
                layout->offsets[tuple_index]));
        llvm::Value *field_ptr = builder->CreateBitCast(
            field_i8,
            llvm::PointerType::get(elem_ll, 0));
        llvm::LoadInst *load = builder->CreateLoad(elem_ll, field_ptr);
        load->setAlignment(llvm::Align(1));
        return load;
      };
      struct MaterializedRangeValue
      {
        IRRangeKind kind = IRRangeKind::Full;
        llvm::Value *lo = nullptr;
        llvm::Value *hi = nullptr;
      };
      auto materialize_range_value = [&](const IRValue &range_value,
                                        llvm::Type *bound_ty,
                                        std::optional<IRRangeKind> fallback_kind = std::nullopt)
          -> std::optional<MaterializedRangeValue>
      {
        auto normalize_range_type = [&](analysis::TypeRef type) -> analysis::TypeRef
        {
          analysis::TypeRef current = strip_perm(type);
          if (!current)
          {
            current = type;
          }
          for (int depth = 0; current && depth < 4; ++depth)
          {
            if (analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, current))
            {
              current = strip_perm(resolved);
              if (!current)
              {
                current = resolved;
              }
              continue;
            }
            break;
          }
          return strip_perm(current);
        };

        analysis::TypeRef range_type =
            normalize_range_type(lookup_value_type(range_value));

        MaterializedRangeValue out;
        std::optional<unsigned> lo_index;
        std::optional<unsigned> hi_index;
        auto configure_for_kind = [&](IRRangeKind kind) -> bool
        {
          out.kind = kind;
          lo_index.reset();
          hi_index.reset();
          switch (kind)
          {
          case IRRangeKind::Full:
            return true;
          case IRRangeKind::From:
            lo_index = 0u;
            return true;
          case IRRangeKind::To:
          case IRRangeKind::ToInclusive:
            hi_index = 0u;
            return true;
          case IRRangeKind::Exclusive:
          case IRRangeKind::Inclusive:
            lo_index = 0u;
            hi_index = 1u;
            return true;
          }
          return false;
        };

        if (range_type && analysis::IsRangeType(range_type))
        {
          if (std::holds_alternative<analysis::TypeRange>(range_type->node))
          {
            if (!configure_for_kind(IRRangeKind::Exclusive))
            {
              return std::nullopt;
            }
          }
          else if (std::holds_alternative<analysis::TypeRangeInclusive>(
                       range_type->node))
          {
            if (!configure_for_kind(IRRangeKind::Inclusive))
            {
              return std::nullopt;
            }
          }
          else if (std::holds_alternative<analysis::TypeRangeFrom>(
                       range_type->node))
          {
            if (!configure_for_kind(IRRangeKind::From))
            {
              return std::nullopt;
            }
          }
          else if (std::holds_alternative<analysis::TypeRangeTo>(
                       range_type->node))
          {
            if (!configure_for_kind(IRRangeKind::To))
            {
              return std::nullopt;
            }
          }
          else if (std::holds_alternative<analysis::TypeRangeToInclusive>(
                       range_type->node))
          {
            if (!configure_for_kind(IRRangeKind::ToInclusive))
            {
              return std::nullopt;
            }
          }
          else if (std::holds_alternative<analysis::TypeRangeFull>(
                       range_type->node))
          {
            if (!configure_for_kind(IRRangeKind::Full))
            {
              return std::nullopt;
            }
          }
          else
          {
            return std::nullopt;
          }
        }
        else if (fallback_kind.has_value())
        {
          if (!configure_for_kind(*fallback_kind))
          {
            return std::nullopt;
          }
        }
        else
        {
          return std::nullopt;
        }

        if (!lo_index.has_value() && !hi_index.has_value())
        {
          return out;
        }

        llvm::Value *raw = EvaluateIRValue(range_value);
        if (!raw)
        {
          return std::nullopt;
        }
        llvm::Type *range_ll = range_type ? GetLLVMType(range_type) : nullptr;
        if (raw->getType()->isPointerTy())
        {
          if (!range_ll)
          {
            return std::nullopt;
          }
          llvm::Value *typed_ptr = raw;
          llvm::Type *expected_ptr_ty = llvm::PointerType::get(range_ll, 0);
          if (typed_ptr->getType() != expected_ptr_ty)
          {
            typed_ptr = builder->CreateBitCast(typed_ptr, expected_ptr_ty);
          }
          raw = builder->CreateLoad(range_ll, typed_ptr);
        }
        else if (range_ll && raw->getType() != range_ll)
        {
          raw = CoerceTo(builder, raw, range_ll);
        }
        if (!raw)
        {
          return std::nullopt;
        }

        auto extract_bound = [&](unsigned index) -> llvm::Value *
        {
          auto *struct_ty = llvm::dyn_cast<llvm::StructType>(raw->getType());
          if (!struct_ty || index >= struct_ty->getNumElements())
          {
            return nullptr;
          }
          llvm::Value *bound = builder->CreateExtractValue(raw, {index});
          if (!bound || !bound->getType()->isIntegerTy() || !bound_ty ||
              !bound_ty->isIntegerTy())
          {
            return nullptr;
          }
          if (bound->getType() != bound_ty)
          {
            bound = builder->CreateIntCast(bound, bound_ty, false);
          }
          return bound;
        };

        if (lo_index.has_value())
        {
          out.lo = extract_bound(*lo_index);
          if (!out.lo)
          {
            return std::nullopt;
          }
        }
        if (hi_index.has_value())
        {
          out.hi = extract_bound(*hi_index);
          if (!out.hi)
          {
            return std::nullopt;
          }
        }
        return out;
      };
      auto enum_decl_for_type = [&](analysis::TypeRef type,
                                    analysis::TypePath *out_path) -> const ast::EnumDecl *
      {
        type = strip_perm(type);
        const auto *path = type ? analysis::AppliedTypePath(*type) : nullptr;
        if (!path)
        {
          return nullptr;
        }
        if (out_path)
        {
          *out_path = *path;
        }
        if (const ast::EnumDecl* decl = analysis::LookupEnumDecl(scope, *path))
        {
          return decl;
        }
        if (!scope.current_module.empty() && path->size() == 1u)
        {
          analysis::TypePath qualified = scope.current_module;
          qualified.insert(qualified.end(), path->begin(), path->end());
          if (out_path)
          {
            *out_path = qualified;
          }
          return analysis::LookupEnumDecl(scope, qualified);
        }
        return nullptr;
      };
      auto enum_decl_for_value = [&](const IRValue &value,
                                     analysis::TypePath *out_path) -> const ast::EnumDecl *
      {
        return enum_decl_for_type(lookup_value_type(value), out_path);
      };
      auto enum_generic_args_for_type = [&](analysis::TypeRef type)
          -> std::vector<analysis::TypeRef>
      {
        type = strip_perm(type);
        if (!type)
        {
          return {};
        }
        const auto *args = analysis::AppliedTypeArgs(*type);
        if (!args)
        {
          return {};
        }
        return *args;
      };
      auto enum_generic_args_for_value = [&](const IRValue &value)
          -> std::vector<analysis::TypeRef>
      {
        return enum_generic_args_for_type(lookup_value_type(value));
      };
      auto enum_decl_for_static_path = [&](const std::vector<std::string> &path,
                                           analysis::TypePath *out_path)
          -> const ast::EnumDecl *
      {
        if (path.empty())
        {
          return nullptr;
        }
        analysis::TypePath resolved_path;
        resolved_path.reserve(path.size());
        for (const auto &seg : path)
        {
          resolved_path.push_back(seg);
        }
        if (out_path)
        {
          *out_path = resolved_path;
        }
        if (const ast::EnumDecl* decl = analysis::LookupEnumDecl(scope, resolved_path))
        {
          return decl;
        }
        if (!scope.current_module.empty() && resolved_path.size() == 1u)
        {
          analysis::TypePath qualified = scope.current_module;
          qualified.insert(qualified.end(), resolved_path.begin(), resolved_path.end());
          if (out_path)
          {
            *out_path = qualified;
          }
          return analysis::LookupEnumDecl(scope, qualified);
        }
        return nullptr;
      };
      auto enum_decl_for_payload_value = [&](const DerivedValueInfo &info,
                                             analysis::TypePath *out_path)
          -> const ast::EnumDecl *
      {
        if (const ast::EnumDecl *decl = enum_decl_for_value(info.base, out_path))
        {
          return decl;
        }
        if (!info.static_path.empty())
        {
          if (const ast::EnumDecl *decl =
                  enum_decl_for_static_path(info.static_path, out_path))
          {
            return decl;
          }
        }
        if (const DerivedValueInfo *base_derived = ctx->LookupDerivedValue(info.base))
        {
          if (!base_derived->static_path.empty())
          {
            if (const ast::EnumDecl *decl =
                    enum_decl_for_static_path(base_derived->static_path, out_path))
            {
              return decl;
            }
          }
        }
        return nullptr;
      };
      auto enum_generic_args_for_payload_value = [&](const DerivedValueInfo &info)
          -> std::vector<analysis::TypeRef>
      {
        return enum_generic_args_for_value(info.base);
      };
      auto find_enum_variant = [](const ast::EnumDecl &decl,
                                  std::string_view variant_name) -> const ast::VariantDecl *
      {
        for (const auto &variant : decl.variants)
        {
          if (analysis::IdEq(variant.name, std::string(variant_name)))
          {
            return &variant;
          }
        }
        return nullptr;
      };
      auto enum_variant_disc = [&](const ast::EnumDecl &decl,
                                   std::string_view variant_name) -> std::optional<std::uint64_t>
      {
        const auto discs = analysis::EnumDiscriminants(decl);
        if (!discs.ok || discs.discs.size() != decl.variants.size())
        {
          return std::nullopt;
        }
        for (std::size_t i = 0; i < decl.variants.size(); ++i)
        {
          if (analysis::IdEq(decl.variants[i].name, std::string(variant_name)))
          {
            return discs.discs[i];
          }
        }
        return std::nullopt;
      };
      struct EnumPayloadMemberInfo
      {
        analysis::TypeRef type;
        std::uint64_t offset = 0;
        std::uint64_t payload_size = 0;
        std::uint64_t payload_align = 1;
        bool ok = false;
      };
      auto enum_payload_member_by_index = [&](const ast::EnumDecl &enum_decl,
                                              const ast::VariantDecl &variant,
                                              const std::vector<analysis::TypeRef> &generic_args,
                                              std::size_t index) -> EnumPayloadMemberInfo
      {
        EnumPayloadMemberInfo out;
        const auto member = ::cursive::analysis::layout::EnumTuplePayloadMemberLayout(
            scope,
            enum_decl,
            variant,
            generic_args,
            index);
        if (!member.has_value())
        {
          return out;
        }
        out.type = member->type;
        out.offset = member->offset;
        out.payload_size = member->payload_size;
        out.payload_align = member->payload_align;
        out.ok = true;
        return out;
      };
      auto enum_payload_member_by_field = [&](const ast::EnumDecl &enum_decl,
                                              const ast::VariantDecl &variant,
                                              const std::vector<analysis::TypeRef> &generic_args,
                                              std::string_view field_name)
          -> EnumPayloadMemberInfo
      {
        EnumPayloadMemberInfo out;
        const auto member = ::cursive::analysis::layout::EnumRecordPayloadMemberLayout(
            scope,
            enum_decl,
            variant,
            generic_args,
            field_name);
        if (!member.has_value())
        {
          return out;
        }
        out.type = member->type;
        out.offset = member->offset;
        out.payload_size = member->payload_size;
        out.payload_align = member->payload_align;
        out.ok = true;
        return out;
      };
      auto load_enum_payload_member = [&](llvm::Value *enum_value,
                                          const EnumPayloadMemberInfo &member) -> llvm::Value *
      {
        if (!enum_value || !member.ok || !member.type)
        {
          return nullptr;
        }
        llvm::Type *member_ty = GetLLVMType(member.type);
        auto *enum_ty = llvm::dyn_cast<llvm::StructType>(enum_value->getType());
        if (!member_ty || !enum_ty || enum_ty->getNumElements() < 2)
        {
          return nullptr;
        }
        llvm::Function *current_fn =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
        if (!current_fn)
        {
          return nullptr;
        }
        llvm::IRBuilder<> entry_builder(
            &current_fn->getEntryBlock(),
            current_fn->getEntryBlock().begin());
        llvm::AllocaInst *enum_slot = entry_builder.CreateAlloca(enum_ty);
        builder->CreateStore(enum_value, enum_slot);
        llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
            *this,
            builder,
            enum_ty,
            enum_slot,
            member.payload_align);
        if (!payload_i8)
        {
          return nullptr;
        }
        llvm::Type *i8_ty = llvm::Type::getInt8Ty(context_);
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
        llvm::Value *field_i8 = builder->CreateGEP(
            i8_ty,
            payload_i8,
            llvm::ConstantInt::get(i64_ty, member.offset));
        llvm::Value *field_ptr = builder->CreateBitCast(
            field_i8,
            llvm::PointerType::get(member_ty, 0));
        llvm::LoadInst *load = builder->CreateLoad(member_ty, field_ptr);
        load->setAlignment(llvm::Align(1));
        return load;
      };
      auto modal_decl_for_type = [&](analysis::TypeRef type,
                                     analysis::TypePath *out_path) -> const ast::ModalDecl *
      {
        type = strip_perm(type);
        if (analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, type))
        {
          type = strip_perm(resolved);
          if (!type)
          {
            type = resolved;
          }
        }
        if (!type)
        {
          return nullptr;
        }
        if (const auto *state = std::get_if<analysis::TypeModalState>(&type->node))
        {
          if (out_path)
          {
            *out_path = state->path;
          }
          return analysis::LookupModalDecl(scope, state->path);
        }
        const auto *path = analysis::AppliedTypePath(*type);
        if (!path)
        {
          return nullptr;
        }
        if (out_path)
        {
          *out_path = *path;
        }
        return analysis::LookupModalDecl(scope, *path);
      };
      auto modal_decl_for_value = [&](const IRValue &value,
                                      analysis::TypePath *out_path) -> const ast::ModalDecl *
      {
        return modal_decl_for_type(lookup_value_type(value), out_path);
      };
      auto modal_decl_for_static_path = [&](const std::vector<std::string> &path,
                                            analysis::TypePath *out_path)
          -> const ast::ModalDecl *
      {
        if (path.empty())
        {
          return nullptr;
        }
        analysis::TypePath resolved_path;
        resolved_path.reserve(path.size());
        for (const auto &seg : path)
        {
          resolved_path.push_back(seg);
        }
        if (out_path)
        {
          *out_path = resolved_path;
        }
        return analysis::LookupModalDecl(scope, resolved_path);
      };
      auto modal_decl_for_payload_value = [&](const DerivedValueInfo &info,
                                              analysis::TypePath *out_path)
          -> const ast::ModalDecl *
      {
        if (const ast::ModalDecl *decl = modal_decl_for_value(info.base, out_path))
        {
          return decl;
        }
        if (!info.static_path.empty())
        {
          if (const ast::ModalDecl *decl =
                  modal_decl_for_static_path(info.static_path, out_path))
          {
            return decl;
          }
        }
        if (const DerivedValueInfo *base_derived = ctx->LookupDerivedValue(info.base))
        {
          if (!base_derived->static_path.empty())
          {
            if (const ast::ModalDecl *decl =
                    modal_decl_for_static_path(base_derived->static_path, out_path))
            {
              return decl;
            }
          }
        }
        return nullptr;
      };
      auto find_modal_state = [](const ast::ModalDecl &decl,
                                 std::string_view state_name) -> const ast::StateBlock *
      {
        for (const auto &state : decl.states)
        {
          if (analysis::IdEq(state.name, std::string(state_name)))
          {
            return &state;
          }
        }
        return nullptr;
      };
      struct ModalPayloadMemberInfo
      {
        analysis::TypeRef type;
        llvm::Type *storage_type = nullptr;
        std::uint64_t offset = 0;
        std::uint64_t payload_size = 0;
        std::uint64_t payload_align = 1;
        bool tagged = true;
        bool recursive_indirect = false;
        bool ok = false;
      };
      auto modal_payload_member_by_field = [&](const ast::ModalDecl &modal_decl,
                                               const analysis::TypePath &modal_path,
                                               const std::vector<analysis::TypeRef> &modal_args,
                                               std::string_view state_name,
                                               std::string_view field_name)
          -> ModalPayloadMemberInfo
      {
        ModalPayloadMemberInfo out;
        analysis::TypeSubst modal_subst;
        if (modal_decl.generic_params && !modal_decl.generic_params->params.empty())
        {
          if (modal_args.size() > modal_decl.generic_params->params.size())
          {
            return out;
          }
          modal_subst = analysis::BuildSubstitution(
              modal_decl.generic_params->params,
              modal_args);
        }
        const auto modal_layout = ::cursive::analysis::layout::ModalLayoutOf(scope, modal_decl, modal_args);
        if (!modal_layout.has_value())
        {
          return out;
        }
        out.payload_size = modal_layout->payload_size;
        out.payload_align = modal_layout->payload_align;
        out.tagged = modal_layout->disc_type.has_value();

        const ast::StateBlock *state = find_modal_state(modal_decl, state_name);
        if (!state)
        {
          return out;
        }

        std::vector<analysis::TypeRef> field_types;
        std::vector<std::string> field_names;
        for (const auto &member : state->members)
        {
          const auto *field = std::get_if<ast::StateFieldDecl>(&member);
          if (!field)
          {
            continue;
          }
          const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, field->type);
          if (!lowered.has_value())
          {
            return out;
          }
          analysis::TypeRef field_type = *lowered;
          if (!modal_subst.empty())
          {
            field_type = analysis::InstantiateType(field_type, modal_subst);
          }
          field_types.push_back(field_type);
          field_names.push_back(field->name);
        }

        analysis::TypeRef aggregate_type = analysis::MakeTypeModalState(
            modal_path,
            std::string(state_name),
            modal_args);
        const auto layout = ComputeLayoutLLVMRecord(
            *this,
            scope,
            aggregate_type,
            field_types);
        if (!layout.has_value())
        {
          return out;
        }
        for (std::size_t i = 0; i < field_names.size() && i < layout->fields.size(); ++i)
        {
          if (analysis::IdEq(field_names[i], std::string(field_name)))
          {
            out.type = field_types[i];
            out.storage_type = layout->fields[i].llvm_type;
            out.offset = layout->fields[i].offset;
            out.recursive_indirect = layout->fields[i].recursive_indirect;
            out.ok = true;
            break;
          }
        }
        return out;
      };
      auto load_modal_payload_member = [&](llvm::Value *modal_value,
                                           const ModalPayloadMemberInfo &member) -> llvm::Value *
      {
        if (!modal_value || !member.ok || !member.type)
        {
          return nullptr;
        }
        llvm::Type *member_ty = GetLLVMType(member.type);
        llvm::Type *storage_ty = member.storage_type ? member.storage_type : member_ty;
        if (!member_ty || !storage_ty)
        {
          return nullptr;
        }
        llvm::Function *current_fn =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
        if (!current_fn)
        {
          return nullptr;
        }
        llvm::IRBuilder<> entry_builder(
            &current_fn->getEntryBlock(),
            current_fn->getEntryBlock().begin());
        llvm::AllocaInst *modal_slot = entry_builder.CreateAlloca(modal_value->getType());
        builder->CreateStore(modal_value, modal_slot);

        llvm::Type *i8_ty = llvm::Type::getInt8Ty(context_);
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
        llvm::Value *payload_i8 = nullptr;
        if (member.tagged)
        {
          auto *modal_ty = llvm::dyn_cast<llvm::StructType>(modal_value->getType());
          if (!modal_ty || modal_ty->getNumElements() < 2)
          {
            return nullptr;
          }
          payload_i8 = CreateTaggedPayloadI8Ptr(
              *this,
              builder,
              modal_ty,
              modal_slot,
              member.payload_align);
        }
        else
        {
          payload_i8 = builder->CreateBitCast(
              modal_slot,
              llvm::PointerType::get(i8_ty, 0));
        }
        if (!payload_i8)
        {
          return nullptr;
        }
        llvm::Value *field_i8 = builder->CreateGEP(
            i8_ty,
            payload_i8,
            llvm::ConstantInt::get(i64_ty, member.offset));
        llvm::Value *field_ptr = builder->CreateBitCast(
            field_i8,
            llvm::PointerType::get(storage_ty, 0));
        llvm::LoadInst *load = builder->CreateLoad(storage_ty, field_ptr);
        load->setAlignment(llvm::Align(1));
        if (member.recursive_indirect)
        {
          llvm::Value *target_ptr =
              builder->CreateBitCast(load, llvm::PointerType::get(member_ty, 0));
          llvm::LoadInst *target_load =
              builder->CreateLoad(member_ty, target_ptr);
          target_load->setAlignment(llvm::Align(1));
          return target_load;
        }
        return load;
      };
      switch (derived->kind)
      {
      case DerivedValueInfo::Kind::Field:
      {
        auto meta = field_meta_for(derived->base, derived->field);
        auto load_field_from_ptr = [&](llvm::Value *base_ptr,
                                       const FieldAccessMeta &field_meta)
            -> llvm::Value *
        {
          if (!base_ptr || !base_ptr->getType()->isPointerTy() ||
              !field_meta.field_type ||
              field_meta.index >= field_meta.aggregate_fields.size())
          {
            return nullptr;
          }
          const auto layout = ComputeLayoutLLVMRecord(
              *this,
              scope,
              field_meta.aggregate_type,
              field_meta.aggregate_fields,
              field_meta.layout_options);
          if (!layout.has_value() || field_meta.index >= layout->fields.size())
          {
            return nullptr;
          }
          const LayoutLLVMField &layout_field = layout->fields[field_meta.index];
          llvm::Type *storage_ll = layout_field.llvm_type;
          llvm::Type *field_ll = GetLLVMType(field_meta.field_type);
          if (!storage_ll || storage_ll->isVoidTy() ||
              !field_ll || field_ll->isVoidTy())
          {
            return nullptr;
          }

          llvm::Value *base_i8 = builder->CreateBitCast(
              base_ptr, llvm::PointerType::get(llvm::Type::getInt8Ty(context_), 0));
          llvm::Value *field_i8 = builder->CreateGEP(
              llvm::Type::getInt8Ty(context_),
              base_i8,
              llvm::ConstantInt::get(
                  llvm::Type::getInt64Ty(context_),
                  layout_field.offset));
          llvm::Value *field_ptr = builder->CreateBitCast(
              field_i8, llvm::PointerType::get(storage_ll, 0));
          llvm::LoadInst *load = builder->CreateLoad(storage_ll, field_ptr);
          load->setAlignment(llvm::Align(1));
          if (layout_field.recursive_indirect)
          {
            llvm::Value *target_ptr =
                builder->CreateBitCast(load, llvm::PointerType::get(field_ll, 0));
            llvm::LoadInst *target_load =
                builder->CreateLoad(field_ll, target_ptr);
            target_load->setAlignment(llvm::Align(1));
            return target_load;
          }
          return load;
        };
        auto load_field_by_offset = [&](llvm::Value *base_value,
                                        const FieldAccessMeta &field_meta)
            -> llvm::Value *
        {
          if (!base_value)
          {
            return nullptr;
          }
          if (base_value->getType()->isPointerTy())
          {
            return load_field_from_ptr(base_value, field_meta);
          }

          llvm::Function *current_fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          if (!current_fn)
          {
            return nullptr;
          }
          llvm::IRBuilder<> entry_builder(
              &current_fn->getEntryBlock(),
              current_fn->getEntryBlock().begin());
          llvm::AllocaInst *base_slot = entry_builder.CreateAlloca(base_value->getType());
          builder->CreateStore(base_value, base_slot);
          return load_field_from_ptr(base_slot, field_meta);
        };

        // Field metadata provides semantic field order (excluding ABI padding).
        // Materialize by byte offset instead of aggregate index to avoid reading
        // synthetic padding members from LLVM struct representations.
        if (meta.has_value() && can_use_addressable_storage_for_field(derived->base))
        {
          if (llvm::Value *base_storage = GetAddressableStorage(derived->base))
          {
            if (llvm::Value *by_offset = load_field_from_ptr(base_storage, *meta))
            {
              materialized = by_offset;
              break;
            }
          }
        }

        llvm::Value *base = EvaluateIRValue(derived->base);
        if (!base)
        {
          break;
        }

        if (meta.has_value())
        {
          if (llvm::Value *by_offset = load_field_by_offset(base, *meta))
          {
            materialized = by_offset;
            break;
          }
        }

        if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(base->getType()))
        {
          std::optional<std::size_t> index =
              meta.has_value() ? std::optional<std::size_t>(meta->index) : std::nullopt;
          if (!index.has_value())
          {
            if (auto parsed = ParseTupleFieldIndex(derived->field))
            {
              if (*parsed < struct_ty->getNumElements())
              {
                index = *parsed;
              }
            }
            else if (struct_ty->getNumElements() == 1)
            {
              index = 0;
            }
          }
          if (index.has_value() && *index < struct_ty->getNumElements())
          {
            materialized =
                builder->CreateExtractValue(base, {static_cast<unsigned>(*index)});
          }
        }
        else if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(base->getType()))
        {
          std::optional<std::size_t> index =
              meta.has_value() ? std::optional<std::size_t>(meta->index) : std::nullopt;
          if (!index.has_value())
          {
            if (auto parsed = ParseTupleFieldIndex(derived->field))
            {
              if (*parsed < arr_ty->getNumElements())
              {
                index = *parsed;
              }
            }
            else if (arr_ty->getNumElements() == 1)
            {
              index = 0;
            }
          }
          if (index.has_value() && *index < arr_ty->getNumElements())
          {
            materialized =
                builder->CreateExtractValue(base, {static_cast<unsigned>(*index)});
          }
        }
        break;
      }
      case DerivedValueInfo::Kind::AddrStatic:
      {
        auto configure_imported_static_decl =
            [&](llvm::GlobalVariable *decl,
                const std::string &symbol_name) -> llvm::GlobalVariable * {
          if (!decl) {
            return nullptr;
          }
          if (!ctx || ctx->module_path.empty()) {
            return decl;
          }
          if (project::ObjectFormatOf(target_profile_) != project::ObjectFormat::Coff) {
            return decl;
          }
          const auto *owner_module = ctx->LookupStaticModule(symbol_name);
          if (!owner_module || owner_module->empty()) {
            return decl;
          }
          const std::string &current_root = ctx->module_path.front();
          const std::string &owner_root = owner_module->front();
          const bool imported_shared_library_data =
              owner_root != current_root &&
              ctx->shared_library_assembly_names.contains(owner_root);
          if (!imported_shared_library_data) {
            return decl;
          }
          decl->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
          return decl;
        };
        std::vector<std::string> symbol_candidates;
        symbol_candidates.reserve(4);
        if (!derived->static_path.empty() && !derived->name.empty())
        {
          if (auto* lower_ctx = current_ctx_;
              lower_ctx && lower_ctx->sigma) {
            if (auto addr =
                    StaticAddr(*lower_ctx->sigma,
                               derived->static_path,
                               derived->name)) {
              symbol_candidates.push_back(addr->name);
            } else {
              symbol_candidates.push_back(
                  StaticSymPath(derived->static_path, derived->name));
            }
          } else {
            symbol_candidates.push_back(
                StaticSymPath(derived->static_path, derived->name));
          }
          if (auto proc_symbol =
                  ProcedureSymbolForPath(*ctx,
                                         derived->static_path,
                                         derived->name))
          {
            symbol_candidates.push_back(*proc_symbol);
          }
        }
        if (!derived->name.empty())
        {
          symbol_candidates.push_back(derived->name);
          if (auto alias = LookupSymbolAlias(derived->name))
          {
            symbol_candidates.push_back(*alias);
          }
        }

        std::set<std::string> seen_symbols;
        bool hosted_slot_symbol = false;
        for (const auto &symbol_name : symbol_candidates)
        {
          if (symbol_name.empty() || !seen_symbols.insert(symbol_name).second)
          {
            continue;
          }

          if (ctx)
          {
            if (const auto *sig = ctx->LookupProcSig(symbol_name))
            {
              llvm::Function *fn = GetFunction(symbol_name);
              if (!fn)
              {
                fn = module_->getFunction(symbol_name);
              }
              if (!fn)
              {
                const bool use_c_abi_aggregate_sret =
                    sig->ffi_import ||
                    ctx->LookupExportUnwindMode(symbol_name).has_value();
                ABICallResult abi = ComputeCallABI(
                    *this,
                    sig->params,
                    sig->ret,
                    use_c_abi_aggregate_sret);
                if (abi.func_type)
                {
                  fn = llvm::Function::Create(
                      abi.func_type,
                      llvm::GlobalValue::ExternalLinkage,
                      symbol_name,
                      module_.get());
                  fn->setCallingConv(CallingConvForAbi(sig->abi));
                }
              }
              if (fn)
              {
                materialized = fn;
                break;
              }
            }
          }

          analysis::TypeRef static_type = nullptr;
          if (ctx)
          {
            static_type = analysis::StripPerm(ctx->LookupStaticType(symbol_name));
            if (!static_type)
            {
              static_type = ctx->LookupStaticType(symbol_name);
            }
          }

          llvm::Type *static_ll = static_type ? GetLLVMType(static_type) : nullptr;
          if (!static_ll || static_ll->isVoidTy())
          {
            continue;
          }

          llvm::Value *fallback = GetGlobal(symbol_name);
          if (!fallback)
          {
            fallback = module_->getNamedGlobal(symbol_name);
          }
          if (!fallback && static_ll)
          {
            auto *decl = new llvm::GlobalVariable(
                *module_,
                static_ll,
                false,
                llvm::GlobalValue::ExternalLinkage,
                nullptr,
                symbol_name);
            fallback = configure_imported_static_decl(decl, symbol_name);
          }
          else if (auto *global_decl = llvm::dyn_cast<llvm::GlobalVariable>(fallback))
          {
            fallback = configure_imported_static_decl(global_decl, symbol_name);
          }

          llvm::Value *ptr = GetHostedStatePtr(symbol_name, static_ll, fallback);
          if (!ptr && fallback)
          {
            ptr = CoerceTo(builder, fallback, llvm::PointerType::get(static_ll, 0));
            if (!ptr && fallback->getType()->isPointerTy())
            {
              ptr = builder->CreateBitCast(fallback, llvm::PointerType::get(static_ll, 0));
            }
          }
          if (!ptr && HasHostedStateSlot(symbol_name))
          {
            hosted_slot_symbol = true;
          }
          if (ptr)
          {
            materialized = ptr;
            break;
          }
        }

        if (!materialized && hosted_slot_symbol && current_ctx_)
        {
          current_ctx_->ReportCodegenFailure();
        }
        break;
      }
      case DerivedValueInfo::Kind::AddrLocal:
      {
        llvm::Value *local = GetLocalBindStorage(derived->name);
        if (!local)
        {
          if (async_state_ && async_state_->info &&
              async_state_->info->is_resume &&
              async_state_->emitting_resume_prelude)
          {
            analysis::TypeRef storage_type =
                pointee_from_type(lookup_value_type(val));
            llvm::Type *storage_ll =
                storage_type ? GetLLVMType(storage_type) : nullptr;
            llvm::Function *func =
                builder && builder->GetInsertBlock()
                    ? builder->GetInsertBlock()->getParent()
                    : nullptr;
            if (storage_ll && !storage_ll->isVoidTy() && func)
            {
              llvm::IRBuilder<> entry_builder(
                  &func->getEntryBlock(),
                  func->getEntryBlock().begin());
              local = entry_builder.CreateAlloca(
                  storage_ll,
                  nullptr,
                  derived->name + ".resume_prelude");
              if (builder && builder->GetInsertBlock() &&
                  !builder->GetInsertBlock()->getTerminator())
              {
                builder->CreateStore(
                    llvm::Constant::getNullValue(storage_ll),
                    local);
              }
            }
          }
        }
        if (!local)
        {
          if (current_ctx_)
          {
            std::cerr << "[cursive] missing local address storage"
                      << " name=" << derived->name;
            if (module_)
            {
              std::cerr << " module=" << module_->getModuleIdentifier();
            }
            if (builder && builder->GetInsertBlock() &&
                builder->GetInsertBlock()->getParent())
            {
              std::cerr << " function="
                        << builder->GetInsertBlock()->getParent()->getName().str();
            }
            std::cerr << "\n";
            current_ctx_->ReportCodegenFailure();
          }
          break;
        }
        if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(local))
        {
          // Panic-out is already a pointer value; load it from the local slot.
          if (derived->name == std::string(kPanicOutName))
          {
            materialized = builder->CreateLoad(alloca->getAllocatedType(), alloca);
            break;
          }
        }

        analysis::TypeRef desired_storage_type =
            pointee_from_type(lookup_value_type(val));
        analysis::TypeRef current_storage_type = nullptr;
        const auto local_type_it = local_types_.find(derived->name);
        if (local_type_it != local_types_.end())
        {
          current_storage_type = local_type_it->second;
        }
        if (desired_storage_type && current_storage_type)
        {
          llvm::Type *desired_storage_llvm = GetLLVMType(desired_storage_type);
          llvm::Type *current_storage_llvm = GetLLVMType(current_storage_type);
          if (desired_storage_llvm && current_storage_llvm &&
              !desired_storage_llvm->isVoidTy() &&
              desired_storage_llvm != current_storage_llvm)
          {
            llvm::Value *typed_local = local;
            llvm::Type *current_ptr_ty =
                llvm::PointerType::get(current_storage_llvm, 0);
            if (typed_local->getType() != current_ptr_ty)
            {
              typed_local = builder->CreateBitCast(typed_local, current_ptr_ty);
            }
            llvm::Value *loaded =
                builder->CreateLoad(current_storage_llvm, typed_local);
            llvm::Value *coerced =
                CoerceToTyped(*this,
                              builder,
                              loaded,
                              desired_storage_llvm,
                              current_storage_type,
                              desired_storage_type);
            if (coerced)
            {
              llvm::Function *current_fn =
                  builder->GetInsertBlock()
                      ? builder->GetInsertBlock()->getParent()
                      : nullptr;
              if (current_fn)
              {
                llvm::IRBuilder<> entry_builder(
                    &current_fn->getEntryBlock(),
                    current_fn->getEntryBlock().begin());
                llvm::AllocaInst *refined_slot =
                    entry_builder.CreateAlloca(
                        desired_storage_llvm,
                        nullptr,
                        derived->name + ".addr.refined");
                builder->CreateStore(coerced, refined_slot);
                materialized = refined_slot;
                break;
              }
            }
          }
        }

        IRValue local_value;
        local_value.kind = IRValue::Kind::Local;
        local_value.name = derived->name;
        if (llvm::Value *storage = GetAddressableStorage(local_value))
        {
          materialized = storage;
          break;
        }
        materialized = local;
        break;
      }
      case DerivedValueInfo::Kind::AddrTuple:
      {
        analysis::TypeRef base_value_type = lookup_value_type(derived->base);
        analysis::TypeRef base_type = pointee_from_type(base_value_type);
        llvm::Value *base = nullptr;
        base = pointer_from_value(EvaluateIRValue(derived->base));
        if (!base)
        {
          base = GetAddressableStorage(derived->base);
        }
        if (!base)
        {
          break;
        }
        analysis::TypeRef elem_type = nullptr;
        std::optional<std::uint64_t> field_offset = derived->byte_offset;
        if (base_type)
        {
          if (const auto *tup = std::get_if<analysis::TypeTuple>(&base_type->node))
          {
            if (derived->tuple_index < tup->elements.size())
            {
              elem_type = tup->elements[derived->tuple_index];
              if (!field_offset.has_value())
              {
                const analysis::ScopeContext &scope = BuildScope(ctx);
                if (const auto layout = ::cursive::analysis::layout::RecordLayoutOf(scope, tup->elements))
                {
                  if (derived->tuple_index < layout->offsets.size())
                  {
                    field_offset = layout->offsets[derived->tuple_index];
                  }
                }
              }
            }
          }
        }
        if (!elem_type)
        {
          elem_type = pointee_from_type(lookup_value_type(val));
        }
        if (!field_offset.has_value())
        {
          break;
        }
        llvm::Value *base_i8 = builder->CreateBitCast(
            base,
            llvm::PointerType::get(llvm::Type::getInt8Ty(context_), 0));
        llvm::Value *field_ptr = builder->CreateGEP(
            llvm::Type::getInt8Ty(context_),
            base_i8,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), *field_offset));
        if (elem_type)
        {
          if (llvm::Type *elem_ll = GetLLVMType(elem_type))
          {
            field_ptr = builder->CreateBitCast(
                field_ptr,
                llvm::PointerType::get(elem_ll, 0));
          }
        }
        materialized = field_ptr;
        break;
      }
      case DerivedValueInfo::Kind::AddrIndex:
      {
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
        const bool debug_addr_index = core::IsDebugEnabled("obj");
        auto runtime_range = derived->range_value.has_value()
                                 ? materialize_range_value(*derived->range_value,
                                                           i64_ty,
                                                           derived->range.kind)
                                 : std::nullopt;

        llvm::Value *base_storage = GetAddressableStorage(derived->base);
        llvm::Value *base_value = nullptr;
        llvm::Value *base_ptr = base_storage;
        if (!base_ptr)
        {
          base_value = EvaluateIRValue(derived->base);
          base_ptr = pointer_from_value(base_value);
        }
        if (!base_ptr)
        {
          break;
        }

        llvm::Value *index = EvaluateIRValue(derived->index);
        if ((!index || !index->getType()->isIntegerTy()) &&
            runtime_range.has_value() && runtime_range->lo)
        {
          index = runtime_range->lo;
        }
        if ((!index || !index->getType()->isIntegerTy()) &&
            derived->range.lo.has_value())
        {
          index = EvaluateIRValue(*derived->range.lo);
        }
        if (!index || !index->getType()->isIntegerTy())
        {
          index = llvm::ConstantInt::get(i64_ty, 0);
        }
        if (index->getType()->getIntegerBitWidth() != 64)
        {
          index = builder->CreateIntCast(index, i64_ty, false);
        }

        analysis::TypeRef base_type = pointee_from_type(lookup_value_type(derived->base));
        analysis::TypeRef elem_type = nullptr;
        if (base_type)
        {
          if (const auto *arr = std::get_if<analysis::TypeArray>(&base_type->node))
          {
            elem_type = arr->element;
          }
          else if (const auto *slice = std::get_if<analysis::TypeSlice>(&base_type->node))
          {
            elem_type = slice->element;
          }
        }
        if (!elem_type)
        {
          elem_type = lookup_value_type(val);
        }
        if (!elem_type)
        {
          break;
        }

        llvm::Type *elem_ll = GetLLVMType(elem_type);
        if (!elem_ll)
        {
          break;
        }

        llvm::Value *elem_base_ptr = base_ptr;
        bool used_slice_data_ptr = false;
        if (base_type && std::holds_alternative<analysis::TypeSlice>(base_type->node))
        {
          if (!base_value)
          {
            base_value = EvaluateIRValue(derived->base);
          }
          llvm::Value *data_ptr = nullptr;

          if (base_value && base_value->getType()->isStructTy())
          {
            data_ptr = builder->CreateExtractValue(base_value, {0u});
          }
          else
          {
            llvm::Type *slice_ll = GetLLVMType(base_type);
            if (slice_ll && base_ptr->getType()->isPointerTy())
            {
              llvm::Value *typed_slice_ptr =
                  builder->CreateBitCast(base_ptr, llvm::PointerType::get(slice_ll, 0));
              llvm::Value *loaded_slice = builder->CreateLoad(slice_ll, typed_slice_ptr);
              if (loaded_slice && loaded_slice->getType()->isStructTy())
              {
                data_ptr = builder->CreateExtractValue(loaded_slice, {0u});
              }
            }
          }

          llvm::Value *coerced = pointer_from_value(data_ptr);
          if (coerced)
          {
            elem_base_ptr = coerced;
            used_slice_data_ptr = true;
          }
        }

        if (debug_addr_index)
        {
          llvm::Function *fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          std::cerr << "[cursive] AddrIndex materialize in "
                    << (fn ? fn->getName().str() : std::string("<no-func>"))
                    << " result=" << val.name
                    << " base="
                    << (lookup_value_type(derived->base)
                            ? analysis::TypeToString(lookup_value_type(derived->base))
                            : std::string("<null>"))
                    << " base_pointee="
                    << (base_type ? analysis::TypeToString(base_type) : std::string("<null>"))
                    << " elem="
                    << (elem_type ? analysis::TypeToString(elem_type) : std::string("<null>"))
                    << " index_kind="
                    << (derived->index.kind == IRValue::Kind::Immediate ? "imm"
                                                                        : "non-imm")
                    << " used_slice_data_ptr=" << (used_slice_data_ptr ? "true" : "false");
          if (auto *idx_const = llvm::dyn_cast<llvm::ConstantInt>(index))
          {
            std::cerr << " index=" << idx_const->getZExtValue();
          }
          std::cerr << "\n";
        }

        llvm::Value *elem_ptr = builder->CreateBitCast(
            elem_base_ptr, llvm::PointerType::get(elem_ll, 0));
        materialized = builder->CreateGEP(elem_ll, elem_ptr, index);
        break;
      }
      case DerivedValueInfo::Kind::AddrField:
      {
        llvm::Value *base = GetAddressableStorage(derived->base);
        if (!base)
        {
          base = pointer_from_value(EvaluateIRValue(derived->base));
        }
        if (!base)
        {
          break;
        }
        auto meta = field_meta_for(derived->base, derived->field);
        std::optional<std::uint64_t> field_offset;
        analysis::TypeRef field_type = nullptr;
        llvm::Type *field_storage_type = nullptr;
        bool field_recursive_indirect = false;
        if (meta.has_value() && meta->index < meta->aggregate_fields.size())
        {
          if (auto layout = ComputeLayoutLLVMRecord(
                  *this,
                  scope,
                  meta->aggregate_type,
                  meta->aggregate_fields,
                  meta->layout_options))
          {
            if (meta->index < layout->fields.size())
            {
              field_offset = layout->fields[meta->index].offset;
              field_type = meta->field_type;
              field_storage_type = layout->fields[meta->index].llvm_type;
              field_recursive_indirect =
                  layout->fields[meta->index].recursive_indirect;
            }
          }
        }
        if (!field_offset.has_value())
        {
          // Fallback for unresolved record metadata: single-field aggregate at offset 0.
          if (auto parsed = ParseTupleFieldIndex(derived->field))
          {
            if (*parsed == 0)
            {
              field_offset = 0;
            }
          }
          else
          {
            if (current_ctx_)
            {
              current_ctx_->ReportCodegenFailure();
            }
            break;
          }
        }
        if (!field_offset.has_value())
        {
          break;
        }

        llvm::Value *base_i8 = builder->CreateBitCast(
            base, llvm::PointerType::get(llvm::Type::getInt8Ty(context_), 0));
        llvm::Value *field_ptr = builder->CreateGEP(
            llvm::Type::getInt8Ty(context_),
            base_i8,
            llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(context_), *field_offset));
        if (field_recursive_indirect)
        {
          llvm::Type *slot_ll = field_storage_type ? field_storage_type : GetOpaquePtr();
          llvm::Value *slot_ptr =
              builder->CreateBitCast(field_ptr, llvm::PointerType::get(slot_ll, 0));
          llvm::Value *target_ptr = builder->CreateLoad(slot_ll, slot_ptr);
          if (field_type)
          {
            if (llvm::Type *elem_ll = GetLLVMType(field_type))
            {
              target_ptr = builder->CreateBitCast(
                  target_ptr, llvm::PointerType::get(elem_ll, 0));
            }
          }
          materialized = target_ptr;
          break;
        }
        if (field_type)
        {
          if (llvm::Type *elem_ll = GetLLVMType(field_type))
          {
            field_ptr = builder->CreateBitCast(
                field_ptr, llvm::PointerType::get(elem_ll, 0));
          }
        }
        materialized = field_ptr;
        break;
      }
      case DerivedValueInfo::Kind::AddrDeref:
      {
        materialized = pointer_from_value(EvaluateIRValue(derived->base));
        break;
      }
      case DerivedValueInfo::Kind::LoadFromAddr:
      {
        llvm::Value *base_ptr = pointer_from_value(EvaluateIRValue(derived->base));
        if (!base_ptr)
        {
          break;
        }

        analysis::TypeRef load_type = lookup_value_type(val);
        llvm::Type *load_llvm_ty = load_type ? GetLLVMType(load_type) : nullptr;
        if (!load_llvm_ty || load_llvm_ty->isVoidTy())
        {
          break;
        }

        llvm::Value *typed_ptr = builder->CreateBitCast(
            base_ptr,
            llvm::PointerType::get(load_llvm_ty, 0));
        materialized = builder->CreateLoad(load_llvm_ty, typed_ptr);
        break;
      }
      case DerivedValueInfo::Kind::Tuple:
      {
        llvm::Value *base = EvaluateIRValue(derived->base);
        const bool debug_tuple = core::IsDebugEnabled("obj");
        analysis::TypeRef tuple_type = tuple_type_for_value(derived->base);
        const auto *tuple =
            tuple_type ? std::get_if<analysis::TypeTuple>(&tuple_type->node) : nullptr;
        if (tuple)
        {
          materialized =
              load_tuple_element_by_offset(base, *tuple, derived->tuple_index);
          if (!materialized && current_ctx_)
          {
            current_ctx_->ReportCodegenFailure();
          }
        }
        else if (auto *struct_ty = base ? llvm::dyn_cast<llvm::StructType>(base->getType()) : nullptr)
        {
          if (derived->tuple_index < struct_ty->getNumElements())
          {
            materialized = builder->CreateExtractValue(base, {static_cast<unsigned>(derived->tuple_index)});
          }
        }
        else if (llvm::Value *base_ptr = pointer_from_value(base))
        {
          analysis::TypeRef base_type = pointee_from_type(lookup_value_type(derived->base));
          const auto *tup = base_type ? std::get_if<analysis::TypeTuple>(&base_type->node) : nullptr;
          if (tup && derived->tuple_index < tup->elements.size())
          {
            analysis::TypeRef elem_type = tup->elements[derived->tuple_index];
            if (const auto layout = ::cursive::analysis::layout::RecordLayoutOf(scope, tup->elements))
            {
              if (derived->tuple_index < layout->offsets.size())
              {
                const std::uint64_t field_offset = layout->offsets[derived->tuple_index];
                llvm::Value *base_i8 = builder->CreateBitCast(
                    base_ptr,
                    llvm::PointerType::get(llvm::Type::getInt8Ty(context_), 0));
                llvm::Value *field_i8 = builder->CreateGEP(
                    llvm::Type::getInt8Ty(context_),
                    base_i8,
                    llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(context_),
                        field_offset));
                if (llvm::Type *elem_ll = GetLLVMType(elem_type))
                {
                  llvm::Value *field_ptr = builder->CreateBitCast(
                      field_i8,
                      llvm::PointerType::get(elem_ll, 0));
                  materialized = builder->CreateLoad(elem_ll, field_ptr);
                }
              }
            }
          }
        }
        if (materialized && debug_tuple)
        {
          llvm::Function *fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          std::cerr << "[cursive] tuple materialized in "
                    << (fn ? fn->getName().str() : std::string("<no-func>"))
                    << " tuple_index=" << derived->tuple_index
                    << " base_kind="
                    << (base ? (base->getType()->isPointerTy()
                                    ? std::string("ptr")
                                    : (base->getType()->isStructTy() ? std::string("struct")
                                                                     : std::string("non-ptr")))
                             : std::string("<null>"))
                    << " result_kind="
                    << (materialized->getType()->isPointerTy()
                            ? std::string("ptr")
                            : (materialized->getType()->isStructTy()
                                   ? std::string("struct")
                                   : std::string("non-ptr")))
                    << "\n";
        }
        if (!materialized && core::IsDebugEnabled("obj"))
        {
          llvm::Function *fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          analysis::TypeRef raw_base_ty = lookup_value_type(derived->base);
          analysis::TypeRef norm_base_ty = pointee_from_type(raw_base_ty);
          std::cerr << "[cursive] tuple materialization failed in "
                    << (fn ? fn->getName().str() : std::string("<no-func>"))
                    << " tuple_index=" << derived->tuple_index
                    << " base_type="
                    << (raw_base_ty ? analysis::TypeToString(raw_base_ty) : std::string("<null>"))
                    << " normalized_base_type="
                    << (norm_base_ty ? analysis::TypeToString(norm_base_ty) : std::string("<null>"))
                    << " base_value="
                    << (base ? (base->getType()->isPointerTy()
                                    ? std::string("ptr")
                                    : (base->getType()->isStructTy() ? std::string("struct")
                                                                     : std::string("non-ptr")))
                             : std::string("<null>"))
                    << "\n";
        }
        break;
      }
      case DerivedValueInfo::Kind::Slice:
      {
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
        llvm::Value *base = EvaluateIRValue(derived->base);
        if (!base)
        {
          break;
        }

        analysis::TypeRef base_type = strip_perm(lookup_value_type(derived->base));
        analysis::TypeRef slice_type = strip_perm(lookup_value_type(val));
        analysis::TypeRef elem_type = nullptr;
        if (const auto *slice = slice_type ? std::get_if<analysis::TypeSlice>(&slice_type->node)
                                           : nullptr)
        {
          elem_type = slice->element;
        }
        if (!elem_type && base_type)
        {
          if (const auto *arr = std::get_if<analysis::TypeArray>(&base_type->node))
          {
            elem_type = arr->element;
          }
          else if (const auto *slice = std::get_if<analysis::TypeSlice>(&base_type->node))
          {
            elem_type = slice->element;
          }
        }
        if (!elem_type)
        {
          break;
        }

        if (!slice_type)
        {
          slice_type = analysis::MakeTypeSlice(elem_type);
        }
        llvm::Type *slice_ll = slice_type ? GetLLVMType(slice_type) : nullptr;
        auto *slice_struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(slice_ll);
        if (!slice_struct_ty || slice_struct_ty->getNumElements() < 2)
        {
          break;
        }

        llvm::Value *base_len = nullptr;
        if (const auto *arr = base_type ? std::get_if<analysis::TypeArray>(&base_type->node)
                                        : nullptr)
        {
          base_len = llvm::ConstantInt::get(i64_ty, static_cast<std::uint64_t>(arr->length));
        }
        else if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(base->getType()))
        {
          base_len =
              llvm::ConstantInt::get(i64_ty, static_cast<std::uint64_t>(arr_ty->getNumElements()));
        }
        else if (base_type && std::holds_alternative<analysis::TypeSlice>(base_type->node))
        {
          if (base->getType()->isStructTy())
          {
            base_len = builder->CreateExtractValue(base, {1u});
          }
          else if (base->getType()->isPointerTy())
          {
            base_len = EmitIndexLenFromAddr(*this, *builder, base_type, base);
          }
        }
        if (!base_len || !base_len->getType()->isIntegerTy())
        {
          break;
        }
        if (base_len->getType()->getIntegerBitWidth() != 64)
        {
          base_len = builder->CreateIntCast(base_len, i64_ty, false);
        }

        auto bound_or = [&](const std::optional<IRValue> &bound_opt,
                            std::uint64_t default_value) -> llvm::Value *
        {
          if (!bound_opt.has_value())
          {
            return llvm::ConstantInt::get(i64_ty, default_value);
          }
          llvm::Value *bound = EvaluateIRValue(*bound_opt);
          if (!bound || !bound->getType()->isIntegerTy())
          {
            return nullptr;
          }
          if (bound->getType()->getIntegerBitWidth() != 64)
          {
            bound = builder->CreateIntCast(bound, i64_ty, false);
          }
          return bound;
        };

        llvm::Value *start = nullptr;
        llvm::Value *end = nullptr;
        auto runtime_range = derived->range_value.has_value()
                                 ? materialize_range_value(*derived->range_value,
                                                           i64_ty,
                                                           derived->range.kind)
                                 : std::nullopt;
        if (runtime_range.has_value())
        {
          switch (runtime_range->kind)
          {
          case IRRangeKind::Full:
            start = llvm::ConstantInt::get(i64_ty, 0);
            end = base_len;
            break;
          case IRRangeKind::From:
            start = runtime_range->lo
                        ? runtime_range->lo
                        : llvm::ConstantInt::get(i64_ty, 0);
            end = base_len;
            break;
          case IRRangeKind::To:
            start = llvm::ConstantInt::get(i64_ty, 0);
            end = runtime_range->hi
                      ? runtime_range->hi
                      : llvm::ConstantInt::get(i64_ty, 0);
            break;
          case IRRangeKind::ToInclusive:
          {
            start = llvm::ConstantInt::get(i64_ty, 0);
            llvm::Value *hi = runtime_range->hi
                                  ? runtime_range->hi
                                  : llvm::ConstantInt::get(i64_ty, 0);
            end = hi ? builder->CreateAdd(hi, llvm::ConstantInt::get(i64_ty, 1)) : nullptr;
            break;
          }
          case IRRangeKind::Exclusive:
            start = runtime_range->lo
                        ? runtime_range->lo
                        : llvm::ConstantInt::get(i64_ty, 0);
            end = runtime_range->hi
                      ? runtime_range->hi
                      : llvm::ConstantInt::get(i64_ty, 0);
            break;
          case IRRangeKind::Inclusive:
          {
            start = runtime_range->lo
                        ? runtime_range->lo
                        : llvm::ConstantInt::get(i64_ty, 0);
            llvm::Value *hi = runtime_range->hi
                                  ? runtime_range->hi
                                  : llvm::ConstantInt::get(i64_ty, 0);
            end = hi ? builder->CreateAdd(hi, llvm::ConstantInt::get(i64_ty, 1)) : nullptr;
            break;
          }
          }
        }
        else
        {
          switch (derived->range.kind)
          {
          case IRRangeKind::Full:
            start = llvm::ConstantInt::get(i64_ty, 0);
            end = base_len;
            break;
          case IRRangeKind::From:
            start = bound_or(derived->range.lo, 0);
            end = base_len;
            break;
          case IRRangeKind::To:
            start = llvm::ConstantInt::get(i64_ty, 0);
            end = bound_or(derived->range.hi, 0);
            break;
          case IRRangeKind::ToInclusive:
          {
            start = llvm::ConstantInt::get(i64_ty, 0);
            llvm::Value *hi = bound_or(derived->range.hi, 0);
            end = hi ? builder->CreateAdd(hi, llvm::ConstantInt::get(i64_ty, 1)) : nullptr;
            break;
          }
          case IRRangeKind::Exclusive:
            start = bound_or(derived->range.lo, 0);
            end = bound_or(derived->range.hi, 0);
            break;
          case IRRangeKind::Inclusive:
          {
            start = bound_or(derived->range.lo, 0);
            llvm::Value *hi = bound_or(derived->range.hi, 0);
            end = hi ? builder->CreateAdd(hi, llvm::ConstantInt::get(i64_ty, 1)) : nullptr;
            break;
          }
          }
        }
        if (!start || !end)
        {
          break;
        }

        llvm::Type *elem_ll = GetLLVMType(elem_type);
        if (!elem_ll)
        {
          break;
        }

        llvm::Value *base_data_ptr = nullptr;
        if (base_type && std::holds_alternative<analysis::TypeSlice>(base_type->node))
        {
          if (base->getType()->isStructTy())
          {
            base_data_ptr = builder->CreateExtractValue(base, {0u});
          }
          else if (base->getType()->isPointerTy())
          {
            llvm::Type *base_slice_ll = GetLLVMType(base_type);
            if (base_slice_ll)
            {
              llvm::Value *typed_slice_ptr = builder->CreateBitCast(
                  base, llvm::PointerType::get(base_slice_ll, 0));
              llvm::Value *loaded_slice = builder->CreateLoad(base_slice_ll, typed_slice_ptr);
              if (loaded_slice && loaded_slice->getType()->isStructTy())
              {
                base_data_ptr = builder->CreateExtractValue(loaded_slice, {0u});
              }
            }
          }
        }

        if (!base_data_ptr)
        {
          llvm::Value *base_ptr = pointer_from_value(base);
          if (!base_ptr)
          {
            if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(base->getType()))
            {
              llvm::Function *current_fn =
                  builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
              if (!current_fn)
              {
                break;
              }
              llvm::IRBuilder<> entry_builder(
                  &current_fn->getEntryBlock(),
                  current_fn->getEntryBlock().begin());
              llvm::AllocaInst *array_slot = entry_builder.CreateAlloca(arr_ty);
              builder->CreateStore(base, array_slot);
              base_ptr = array_slot;
            }
          }
          if (!base_ptr)
          {
            break;
          }
          llvm::Value *elem_base_ptr = builder->CreateBitCast(
              base_ptr, llvm::PointerType::get(elem_ll, 0));
          base_data_ptr = builder->CreateGEP(elem_ll, elem_base_ptr, start);
        }
        else
        {
          llvm::Value *coerced_ptr = pointer_from_value(base_data_ptr);
          if (!coerced_ptr)
          {
            break;
          }
          llvm::Value *elem_base_ptr = builder->CreateBitCast(
              coerced_ptr, llvm::PointerType::get(elem_ll, 0));
          base_data_ptr = builder->CreateGEP(elem_ll, elem_base_ptr, start);
        }

        llvm::Value *slice_ptr = pointer_from_value(base_data_ptr);
        if (!slice_ptr)
        {
          break;
        }
        llvm::Type *slice_ptr_ty = slice_struct_ty->getElementType(0);
        if (slice_ptr->getType() != slice_ptr_ty)
        {
          if (!slice_ptr->getType()->isPointerTy() || !slice_ptr_ty->isPointerTy())
          {
            break;
          }
          slice_ptr = builder->CreateBitCast(slice_ptr, slice_ptr_ty);
        }

        llvm::Value *slice_len = builder->CreateSub(end, start);
        llvm::Type *slice_len_ty = slice_struct_ty->getElementType(1);
        if (slice_len->getType() != slice_len_ty)
        {
          if (!slice_len_ty->isIntegerTy())
          {
            break;
          }
          slice_len = builder->CreateIntCast(slice_len, slice_len_ty, false);
        }

        llvm::Value *slice_value = llvm::UndefValue::get(slice_struct_ty);
        slice_value = builder->CreateInsertValue(slice_value, slice_ptr, {0u});
        slice_value = builder->CreateInsertValue(slice_value, slice_len, {1u});
        materialized = slice_value;
        break;
      }
      case DerivedValueInfo::Kind::Index:
      {
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
        llvm::Value *index = EvaluateIRValue(derived->index);
        if (!index || !index->getType()->isIntegerTy())
        {
          break;
        }
        if (index->getType()->getIntegerBitWidth() != 64)
        {
          index = builder->CreateIntCast(index, i64_ty, false);
        }

        analysis::TypeRef base_type = strip_perm(lookup_value_type(derived->base));
        analysis::TypeRef elem_type = lookup_value_type(val);
        if (!elem_type && base_type)
        {
          if (const auto *arr = std::get_if<analysis::TypeArray>(&base_type->node))
          {
            elem_type = arr->element;
          }
          else if (const auto *slice = std::get_if<analysis::TypeSlice>(&base_type->node))
          {
            elem_type = slice->element;
          }
        }
        llvm::Type *elem_ll = elem_type ? GetLLVMType(elem_type) : nullptr;
        if (!elem_ll)
        {
          break;
        }

        if (llvm::Value *base_storage = GetAddressableStorage(derived->base))
        {
          if (base_type && std::holds_alternative<analysis::TypeArray>(base_type->node))
          {
            if (llvm::Type *array_ll = GetLLVMType(base_type))
            {
              llvm::Value *array_ptr = builder->CreateBitCast(
                  base_storage, llvm::PointerType::get(array_ll, 0));
              llvm::Value *elem_ptr = builder->CreateGEP(
                  array_ll,
                  array_ptr,
                  {llvm::ConstantInt::get(i64_ty, 0), index});
              materialized = builder->CreateLoad(elem_ll, elem_ptr);
              break;
            }
          }

          if (base_type && std::holds_alternative<analysis::TypeSlice>(base_type->node))
          {
            if (llvm::Type *slice_ll = GetLLVMType(base_type))
            {
              llvm::Value *slice_ptr = builder->CreateBitCast(
                  base_storage, llvm::PointerType::get(slice_ll, 0));
              llvm::Value *slice_value = builder->CreateLoad(slice_ll, slice_ptr);
              if (slice_value && slice_value->getType()->isStructTy())
              {
                llvm::Value *data_ptr = builder->CreateExtractValue(slice_value, {0u});
                llvm::Value *coerced = pointer_from_value(data_ptr);
                if (coerced)
                {
                  llvm::Value *elem_base_ptr = builder->CreateBitCast(
                      coerced, llvm::PointerType::get(elem_ll, 0));
                  llvm::Value *elem_ptr = builder->CreateGEP(
                      elem_ll, elem_base_ptr, index);
                  materialized = builder->CreateLoad(elem_ll, elem_ptr);
                  break;
                }
              }
            }
          }
        }

        llvm::Value *base = EvaluateIRValue(derived->base);
        if (!base)
        {
          break;
        }

        if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(base->getType()))
        {
          if (auto *idx_const = llvm::dyn_cast<llvm::ConstantInt>(index))
          {
            const std::uint64_t idx = idx_const->getZExtValue();
            if (idx < arr_ty->getNumElements())
            {
              materialized = builder->CreateExtractValue(
                  base, {static_cast<unsigned>(idx)});
              break;
            }
          }
          llvm::Function *current_fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          if (!current_fn)
          {
            break;
          }
          llvm::IRBuilder<> entry_builder(
              &current_fn->getEntryBlock(),
              current_fn->getEntryBlock().begin());
          llvm::AllocaInst *array_slot = entry_builder.CreateAlloca(arr_ty);
          builder->CreateStore(base, array_slot);
          llvm::Value *elem_ptr = builder->CreateGEP(
              arr_ty,
              array_slot,
              {llvm::ConstantInt::get(i64_ty, 0), index});
          materialized = builder->CreateLoad(elem_ll, elem_ptr);
          break;
        }

        if (base_type && std::holds_alternative<analysis::TypeSlice>(base_type->node) &&
            base->getType()->isStructTy())
        {
          llvm::Value *data_ptr = builder->CreateExtractValue(base, {0u});
          llvm::Value *coerced = pointer_from_value(data_ptr);
          if (!coerced)
          {
            break;
          }
          llvm::Value *elem_base_ptr = builder->CreateBitCast(
              coerced, llvm::PointerType::get(elem_ll, 0));
          llvm::Value *elem_ptr = builder->CreateGEP(
              elem_ll, elem_base_ptr, index);
          materialized = builder->CreateLoad(elem_ll, elem_ptr);
          break;
        }

        llvm::Value *base_ptr = pointer_from_value(base);
        if (!base_ptr)
        {
          break;
        }
        llvm::Value *elem_base_ptr = builder->CreateBitCast(
            base_ptr, llvm::PointerType::get(elem_ll, 0));
        llvm::Value *elem_ptr = builder->CreateGEP(
            elem_ll, elem_base_ptr, index);
        materialized = builder->CreateLoad(elem_ll, elem_ptr);
        break;
      }
      case DerivedValueInfo::Kind::UnionPayload:
      {
        analysis::TypeRef union_type = strip_perm(lookup_value_type(derived->base));
        if (analysis::TypeRef resolved =
                ResolveAliasTypeInScope(scope, union_type))
        {
          union_type = strip_perm(resolved);
          if (!union_type)
          {
            union_type = resolved;
          }
        }
        const auto *uni = union_type ? std::get_if<analysis::TypeUnion>(&union_type->node) : nullptr;
        if (!uni)
        {
          break;
        }
        const auto layout = ::cursive::analysis::layout::UnionLayoutOf(scope, *uni);
        if (!layout.has_value())
        {
          break;
        }
        if (derived->union_index >= layout->member_list.size())
        {
          break;
        }

        analysis::TypeRef member_type = layout->member_list[derived->union_index];
        if (!member_type)
        {
          member_type = lookup_value_type(val);
        }

        llvm::Value *base = EvaluateIRValue(derived->base);
        if (!base || !member_type)
        {
          break;
        }

        analysis::TypeRef stripped_member = analysis::StripPerm(member_type);
        if (stripped_member &&
            std::holds_alternative<analysis::TypePrim>(stripped_member->node) &&
            std::get<analysis::TypePrim>(stripped_member->node).name == "()")
        {
          if (llvm::Type *unit_ty = GetLLVMType(member_type))
          {
            materialized = llvm::Constant::getNullValue(unit_ty);
          }
          break;
        }

        if (layout->niche)
        {
          if (llvm::Type *member_ty = GetLLVMType(member_type))
          {
            materialized = CoerceTo(builder, base, member_ty);
          }
          else
          {
            materialized = base;
          }
          break;
        }

        auto *union_ty = llvm::dyn_cast<llvm::StructType>(base->getType());
        if (!union_ty || union_ty->getNumElements() < 2)
        {
          break;
        }
        llvm::Type *member_ty = GetLLVMType(member_type);
        if (!member_ty)
        {
          break;
        }
        llvm::Function *current_fn =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
        if (!current_fn)
        {
          break;
        }
        llvm::IRBuilder<> entry_builder(
            &current_fn->getEntryBlock(),
            current_fn->getEntryBlock().begin());
        llvm::AllocaInst *union_slot = entry_builder.CreateAlloca(union_ty);
        builder->CreateStore(base, union_slot);
        llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
            *this,
            builder,
            union_ty,
            union_slot,
            layout->payload_align);
        if (!payload_i8)
        {
          break;
        }
        llvm::Value *field_ptr = builder->CreateBitCast(
            payload_i8, llvm::PointerType::get(member_ty, 0));
        llvm::LoadInst *load = builder->CreateLoad(member_ty, field_ptr);
        load->setAlignment(llvm::Align(1));
        materialized = load;
        break;
      }
      case DerivedValueInfo::Kind::EnumPayloadIndex:
      {
        analysis::TypePath enum_path;
        const ast::EnumDecl *enum_decl =
            enum_decl_for_payload_value(*derived, &enum_path);
        const std::vector<analysis::TypeRef> enum_generic_args =
            enum_generic_args_for_payload_value(*derived);
        EnumPayloadMemberInfo member;
        if (enum_decl)
        {
          if (const ast::VariantDecl *variant = find_enum_variant(*enum_decl, derived->variant))
          {
            member = enum_payload_member_by_index(
                *enum_decl,
                *variant,
                enum_generic_args,
                derived->tuple_index);
          }
        }
        if (!member.ok)
        {
          member.type = lookup_value_type(val);
          member.offset = 0;
          member.ok = member.type != nullptr;
          if (enum_decl)
          {
            if (const auto enum_layout = ::cursive::analysis::layout::EnumLayoutOf(
                    scope,
                    *enum_decl,
                    enum_generic_args,
                    ::cursive::analysis::layout::ResolveEnumLayoutOptions(enum_decl->attrs)))
            {
              member.payload_size = enum_layout->payload_size;
              member.payload_align = enum_layout->payload_align;
            }
          }
        }
        llvm::Value *base = EvaluateIRValue(derived->base);
        materialized = load_enum_payload_member(base, member);
        break;
      }
      case DerivedValueInfo::Kind::EnumPayloadField:
      {
        analysis::TypePath enum_path;
        const ast::EnumDecl *enum_decl =
            enum_decl_for_payload_value(*derived, &enum_path);
        const std::vector<analysis::TypeRef> enum_generic_args =
            enum_generic_args_for_payload_value(*derived);
        EnumPayloadMemberInfo member;
        if (enum_decl)
        {
          if (const ast::VariantDecl *variant = find_enum_variant(*enum_decl, derived->variant))
          {
            member = enum_payload_member_by_field(
                *enum_decl,
                *variant,
                enum_generic_args,
                derived->field);
          }
        }
        if (!member.ok)
        {
          member.type = lookup_value_type(val);
          member.offset = 0;
          member.ok = member.type != nullptr;
          if (enum_decl)
          {
            if (const auto enum_layout = ::cursive::analysis::layout::EnumLayoutOf(
                    scope,
                    *enum_decl,
                    enum_generic_args,
                    ::cursive::analysis::layout::ResolveEnumLayoutOptions(enum_decl->attrs)))
            {
              member.payload_size = enum_layout->payload_size;
              member.payload_align = enum_layout->payload_align;
            }
          }
        }
        llvm::Value *base = EvaluateIRValue(derived->base);
        materialized = load_enum_payload_member(base, member);
        break;
      }
      case DerivedValueInfo::Kind::ModalField:
      {
        analysis::TypeRef base_modal_type = strip_perm(lookup_value_type(derived->base));
        if (!base_modal_type)
        {
          if (const DerivedValueInfo *base_derived =
                  ctx->LookupDerivedValue(derived->base))
          {
            if (base_derived->kind == DerivedValueInfo::Kind::UnionPayload)
            {
              analysis::TypeRef union_type =
                  strip_perm(lookup_value_type(base_derived->base));
              if (analysis::TypeRef resolved =
                      ResolveAliasTypeInScope(scope, union_type))
              {
                union_type = strip_perm(resolved);
                if (!union_type)
                {
                  union_type = resolved;
                }
              }
              if (union_type &&
                  std::holds_alternative<analysis::TypeUnion>(union_type->node))
              {
                const auto &uni = std::get<analysis::TypeUnion>(union_type->node);
                std::vector<analysis::TypeRef> members = uni.members;
                if (const auto layout =
                        ::cursive::analysis::layout::UnionLayoutOf(scope, uni))
                {
                  members = layout->member_list;
                }
                if (base_derived->union_index < members.size())
                {
                  base_modal_type = strip_perm(members[base_derived->union_index]);
                }
              }
            }
          }
        }
        if (analysis::TypeRef resolved =
                ResolveAliasTypeInScope(scope, base_modal_type))
        {
          base_modal_type = strip_perm(resolved);
          if (!base_modal_type)
          {
            base_modal_type = resolved;
          }
        }
        analysis::TypePath modal_path;
        const ast::ModalDecl *modal_decl =
            modal_decl_for_type(base_modal_type, &modal_path);
        if (!modal_decl)
        {
          modal_decl = modal_decl_for_payload_value(*derived, &modal_path);
        }
        const auto *base_modal_state =
            base_modal_type
                ? std::get_if<analysis::TypeModalState>(&base_modal_type->node)
                : nullptr;
        const bool base_is_modal_state = (base_modal_state != nullptr);
        const bool base_is_async_modal_state =
            base_modal_state && analysis::IsAsyncType(base_modal_type);
        std::vector<analysis::TypeRef> base_modal_args;
        if (base_modal_state)
        {
          base_modal_args = base_modal_state->generic_args;
        }
        else if (base_modal_type)
        {
          if (const auto *base_applied_args = analysis::AppliedTypeArgs(*base_modal_type))
          {
            base_modal_args = *base_applied_args;
          }
        }
        ModalPayloadMemberInfo member;
        if (modal_decl)
        {
          member = modal_payload_member_by_field(
              *modal_decl,
              modal_path,
              base_modal_args,
              derived->modal_state,
              derived->field);
          if (base_is_modal_state && !base_is_async_modal_state)
          {
            member.tagged = false;
          }
        }
        if (!member.ok)
        {
          member.type = lookup_value_type(val);
          member.offset = 0;
          member.ok = member.type != nullptr;
          if (modal_decl)
          {
            if (const auto modal_layout = ::cursive::analysis::layout::ModalLayoutOf(scope, *modal_decl, base_modal_args))
            {
              member.payload_size = modal_layout->payload_size;
              member.payload_align = modal_layout->payload_align;
              member.tagged = modal_layout->disc_type.has_value();
            }
          }
          if (base_is_modal_state && !base_is_async_modal_state)
          {
            member.tagged = false;
          }
        }
        llvm::Value *base = EvaluateIRValue(derived->base);
        materialized = load_modal_payload_member(base, member);
        break;
      }
      case DerivedValueInfo::Kind::EnumLit:
      {
        analysis::TypePath enum_path;
        analysis::TypeRef enum_type = lookup_value_type(val);
        const ast::EnumDecl *enum_decl = enum_decl_for_type(enum_type, &enum_path);
        const std::vector<analysis::TypeRef> enum_generic_args =
            enum_generic_args_for_type(enum_type);
        if (!enum_decl && !derived->static_path.empty())
        {
          enum_decl = enum_decl_for_static_path(derived->static_path, &enum_path);
        }
        if (!enum_decl)
        {
          break;
        }
        const ast::VariantDecl *variant = find_enum_variant(*enum_decl, derived->variant);
        const auto disc = enum_variant_disc(*enum_decl, derived->variant);
        const auto enum_layout = ::cursive::analysis::layout::EnumLayoutOf(
            scope,
            *enum_decl,
            enum_generic_args,
            ::cursive::analysis::layout::ResolveEnumLayoutOptions(enum_decl->attrs));
        if (!variant || !disc.has_value() || !enum_layout.has_value())
        {
          break;
        }

        llvm::Type *enum_ty = enum_type ? GetLLVMType(enum_type) : nullptr;
        if (enum_layout->payload_size == 0)
        {
          if (!enum_ty)
          {
            break;
          }
          llvm::Value *disc_value = nullptr;
          if (auto *disc_int_ty = llvm::dyn_cast<llvm::IntegerType>(enum_ty))
          {
            disc_value = llvm::ConstantInt::get(disc_int_ty, *disc);
          }
          else
          {
            disc_value = CoerceTo(
                builder,
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), *disc),
                enum_ty);
          }
          materialized = disc_value;
          break;
        }
        auto *enum_struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(enum_ty);
        if (!enum_struct_ty || enum_struct_ty->getNumElements() < 2)
        {
          break;
        }
        llvm::Function *current_fn =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
        if (!current_fn)
        {
          break;
        }
        llvm::IRBuilder<> entry_builder(
            &current_fn->getEntryBlock(),
            current_fn->getEntryBlock().begin());
        llvm::AllocaInst *enum_slot = entry_builder.CreateAlloca(enum_struct_ty);
        builder->CreateStore(llvm::Constant::getNullValue(enum_struct_ty), enum_slot);

        llvm::Value *disc_ptr = builder->CreateStructGEP(enum_struct_ty, enum_slot, 0);
        llvm::Type *disc_ty = enum_struct_ty->getElementType(0);
        llvm::Value *disc_value = nullptr;
        if (auto *disc_int_ty = llvm::dyn_cast<llvm::IntegerType>(disc_ty))
        {
          disc_value = llvm::ConstantInt::get(disc_int_ty, *disc);
        }
        else
        {
          disc_value = CoerceTo(
              builder,
              llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), *disc),
              disc_ty);
        }
        if (disc_value)
        {
          builder->CreateStore(disc_value, disc_ptr);
        }

        llvm::Value *payload_base_i8 = CreateTaggedPayloadI8Ptr(
            *this,
            builder,
            enum_struct_ty,
            enum_slot,
            enum_layout->payload_align);

        auto store_payload_value = [&](const EnumPayloadMemberInfo &member, llvm::Value *value)
        {
          if (!payload_base_i8 || !member.ok || !member.type || !value)
          {
            return;
          }
          llvm::Type *member_ty = GetLLVMType(member.type);
          if (!member_ty)
          {
            return;
          }
          value = CoerceTo(builder, value, member_ty);
          if (!value)
          {
            value = llvm::Constant::getNullValue(member_ty);
          }
          llvm::Type *i8_ty = llvm::Type::getInt8Ty(context_);
          llvm::Type *i64_ty = llvm::Type::getInt64Ty(context_);
          llvm::Value *field_i8 = builder->CreateGEP(
              i8_ty,
              payload_base_i8,
              llvm::ConstantInt::get(i64_ty, member.offset));
          llvm::Value *field_ptr = builder->CreateBitCast(
              field_i8,
              llvm::PointerType::get(member_ty, 0));
          llvm::StoreInst *store = builder->CreateStore(value, field_ptr);
          store->setAlignment(llvm::Align(1));
        };

        if (const auto *tuple_payload =
                variant->payload_opt.has_value()
                    ? std::get_if<ast::VariantPayloadTuple>(&*variant->payload_opt)
                    : nullptr)
        {
          const std::size_t count =
              std::min(tuple_payload->elements.size(), derived->payload_elems.size());
          for (std::size_t i = 0; i < count; ++i)
          {
            const auto member = enum_payload_member_by_index(
                *enum_decl,
                *variant,
                enum_generic_args,
                i);
            store_payload_value(member, EvaluateIRValue(derived->payload_elems[i]));
          }
        }
        else if (const auto *record_payload =
                     variant->payload_opt.has_value()
                         ? std::get_if<ast::VariantPayloadRecord>(&*variant->payload_opt)
                         : nullptr)
        {
          (void)record_payload;
          for (const auto &[field_name, field_value] : derived->payload_fields)
          {
            const auto member = enum_payload_member_by_field(
                *enum_decl,
                *variant,
                enum_generic_args,
                field_name);
            store_payload_value(member, EvaluateIRValue(field_value));
          }
        }

        materialized = builder->CreateLoad(enum_struct_ty, enum_slot);
        break;
      }
      case DerivedValueInfo::Kind::RangeLit:
      {
        analysis::TypeRef range_type = lookup_value_type(val);
        if (!analysis::IsRangeType(range_type))
        {
          break;
        }
        llvm::Type *range_ty = GetLLVMType(range_type);
        auto *range_struct_ty = llvm::dyn_cast<llvm::StructType>(range_ty);
        if (!range_struct_ty)
        {
          break;
        }

        analysis::TypeRef stripped = range_type;
        while (stripped)
        {
          if (const auto *perm = std::get_if<analysis::TypePerm>(&stripped->node))
          {
            stripped = perm->base;
            continue;
          }
          if (const auto *refine = std::get_if<analysis::TypeRefine>(&stripped->node))
          {
            stripped = refine->base;
            continue;
          }
          break;
        }
        if (!stripped)
        {
          break;
        }

        enum class RangeStructShape
        {
          Full,
          OneLower,
          OneUpper,
          TwoBounds
        };
        RangeStructShape shape = RangeStructShape::Full;
        if (std::holds_alternative<analysis::TypeRange>(stripped->node) ||
            std::holds_alternative<analysis::TypeRangeInclusive>(stripped->node))
        {
          shape = RangeStructShape::TwoBounds;
        }
        else if (std::holds_alternative<analysis::TypeRangeFrom>(stripped->node))
        {
          shape = RangeStructShape::OneLower;
        }
        else if (std::holds_alternative<analysis::TypeRangeTo>(stripped->node) ||
                 std::holds_alternative<analysis::TypeRangeToInclusive>(
                     stripped->node))
        {
          shape = RangeStructShape::OneUpper;
        }
        else if (std::holds_alternative<analysis::TypeRangeFull>(stripped->node))
        {
          shape = RangeStructShape::Full;
        }
        else
        {
          break;
        }

        auto eval_bound = [&](const std::optional<IRValue> &bound_opt,
                              llvm::Type *target_ty) -> llvm::Value *
        {
          if (!target_ty)
          {
            return nullptr;
          }
          if (!bound_opt.has_value())
          {
            return llvm::Constant::getNullValue(target_ty);
          }
          llvm::Value *value = EvaluateIRValue(*bound_opt);
          if (!value)
          {
            return llvm::Constant::getNullValue(target_ty);
          }
          if (value->getType()->isPointerTy() && target_ty->isIntegerTy())
          {
            llvm::Type *load_ty = target_ty;
            if (analysis::TypeRef bound_type = lookup_value_type(*bound_opt))
            {
              if (llvm::Type *bound_ll = GetLLVMType(bound_type))
              {
                if (bound_ll->isIntegerTy())
                {
                  load_ty = bound_ll;
                }
              }
            }
            llvm::Value *typed_ptr = value;
            llvm::Type *ptr_to_load_ty = llvm::PointerType::get(load_ty, 0);
            if (typed_ptr->getType() != ptr_to_load_ty)
            {
              typed_ptr = builder->CreateBitCast(typed_ptr, ptr_to_load_ty);
            }
            value = builder->CreateLoad(load_ty, typed_ptr);
          }
          if (value->getType() != target_ty)
          {
            value = CoerceTo(builder, value, target_ty);
          }
          if (!value)
          {
            return llvm::Constant::getNullValue(target_ty);
          }
          return value;
        };

        llvm::Value *out = llvm::Constant::getNullValue(range_struct_ty);
        switch (shape)
        {
        case RangeStructShape::Full:
          materialized = out;
          break;
        case RangeStructShape::OneLower:
        {
          if (range_struct_ty->getNumElements() < 1)
          {
            break;
          }
          llvm::Type *lo_ty = range_struct_ty->getElementType(0);
          llvm::Value *lo = eval_bound(derived->range.lo, lo_ty);
          out = builder->CreateInsertValue(out, lo, {0u});
          materialized = out;
          break;
        }
        case RangeStructShape::OneUpper:
        {
          if (range_struct_ty->getNumElements() < 1)
          {
            break;
          }
          llvm::Type *hi_ty = range_struct_ty->getElementType(0);
          llvm::Value *hi = eval_bound(derived->range.hi, hi_ty);
          out = builder->CreateInsertValue(out, hi, {0u});
          materialized = out;
          break;
        }
        case RangeStructShape::TwoBounds:
        {
          if (range_struct_ty->getNumElements() < 2)
          {
            break;
          }
          llvm::Type *lo_ty = range_struct_ty->getElementType(0);
          llvm::Type *hi_ty = range_struct_ty->getElementType(1);
          llvm::Value *lo = eval_bound(derived->range.lo, lo_ty);
          llvm::Value *hi = eval_bound(derived->range.hi, hi_ty);
          out = builder->CreateInsertValue(out, lo, {0u});
          out = builder->CreateInsertValue(out, hi, {1u});
          materialized = out;
          break;
        }
        }
        break;
      }
      case DerivedValueInfo::Kind::RecordLit:
      {
        llvm::Type *agg_ty = nullptr;
        analysis::TypeRef record_type = lookup_value_type(val);
        if (record_type)
        {
          agg_ty = GetLLVMType(record_type);
        }
        if (!agg_ty || !agg_ty->isStructTy())
        {
          std::vector<llvm::Type *> inferred_field_tys;
          inferred_field_tys.reserve(derived->fields.size());
          for (const auto &[field_name, field_value] : derived->fields)
          {
            (void)field_name;
            llvm::Value *elem = EvaluateIRValue(field_value);
            inferred_field_tys.push_back(
                elem ? elem->getType() : llvm::Type::getInt64Ty(context_));
          }
          agg_ty = llvm::StructType::get(context_, inferred_field_tys);
        }

        auto *struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(agg_ty);
        if (!struct_ty)
        {
          break;
        }

        struct RecordFieldStore
        {
          std::uint64_t offset = 0;
          analysis::TypeRef field_type;
          llvm::Type *field_llvm_type = nullptr;
          bool recursive_indirect = false;
          IRValue value;
        };
        std::vector<RecordFieldStore> offset_fields;
        bool can_use_offset_mode = record_type != nullptr;
        if (can_use_offset_mode)
        {
          for (const auto &[field_name, field_value] : derived->fields)
          {
            auto meta = ResolveFieldAccessMeta(scope, record_type, field_name);
            if (!meta.has_value() || !meta->field_type ||
                meta->index >= meta->aggregate_fields.size())
            {
              can_use_offset_mode = false;
              break;
            }
            const auto layout = ComputeLayoutLLVMRecord(
                *this,
                scope,
                meta->aggregate_type,
                meta->aggregate_fields,
                meta->layout_options);
            if (!layout.has_value() || meta->index >= layout->fields.size())
            {
              can_use_offset_mode = false;
              break;
            }
            RecordFieldStore store;
            store.offset = layout->fields[meta->index].offset;
            store.field_type = meta->field_type;
            store.field_llvm_type = layout->fields[meta->index].llvm_type;
            store.recursive_indirect =
                layout->fields[meta->index].recursive_indirect;
            store.value = field_value;
            offset_fields.push_back(std::move(store));
          }
        }
        if (can_use_offset_mode)
        {
          llvm::Function *current_fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          if (!current_fn)
          {
            break;
          }
          llvm::IRBuilder<> entry_builder(
              &current_fn->getEntryBlock(),
              current_fn->getEntryBlock().begin());
          llvm::AllocaInst *agg_slot = entry_builder.CreateAlloca(struct_ty);
          builder->CreateStore(llvm::Constant::getNullValue(struct_ty), agg_slot);
          llvm::Value *base_i8 = builder->CreateBitCast(
              agg_slot, llvm::PointerType::get(llvm::Type::getInt8Ty(context_), 0));
          for (const auto &field : offset_fields)
          {
            llvm::Type *field_ll = field.field_llvm_type
                                       ? field.field_llvm_type
                                       : GetLLVMType(field.field_type);
            if (!field_ll || field_ll->isVoidTy())
            {
              continue;
            }
            llvm::Value *elem = nullptr;
            analysis::TypeRef source_type = lookup_value_type(field.value);
            if (field.recursive_indirect)
            {
              elem = GetAddressableStorage(field.value);
              if (!elem)
              {
                llvm::Value *materialized = EvaluateIRValue(field.value);
                llvm::Type *source_ll = GetLLVMType(field.field_type);
                if (materialized && source_ll && !source_ll->isVoidTy())
                {
                  llvm::AllocaInst *field_slot =
                      entry_builder.CreateAlloca(source_ll);
                  llvm::Value *coerced = CoerceToTyped(*this,
                                                       builder,
                                                       materialized,
                                                       source_ll,
                                                       source_type,
                                                       field.field_type);
                  if (!coerced)
                  {
                    coerced = CoerceTo(builder, materialized, source_ll);
                  }
                  if (!coerced)
                  {
                    coerced = llvm::Constant::getNullValue(source_ll);
                  }
                  builder->CreateStore(coerced, field_slot);
                  elem = field_slot;
                }
              }
              elem = CoerceTo(builder, elem, field_ll);
            }
            else
            {
              elem = EvaluateIRValue(field.value);
              elem = CoerceToTyped(*this,
                                   builder,
                                   elem,
                                   field_ll,
                                   source_type,
                                   field.field_type);
            }
            if (!elem)
            {
              elem = llvm::Constant::getNullValue(field_ll);
            }
            llvm::Value *field_i8 = builder->CreateGEP(
                llvm::Type::getInt8Ty(context_),
                base_i8,
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), field.offset));
            llvm::Value *field_ptr = builder->CreateBitCast(
                field_i8, llvm::PointerType::get(field_ll, 0));
            llvm::StoreInst *store = builder->CreateStore(elem, field_ptr);
            store->setAlignment(llvm::Align(1));
          }
          materialized = builder->CreateLoad(struct_ty, agg_slot);
          break;
        }

        llvm::Value *agg = llvm::Constant::getNullValue(struct_ty);
        std::vector<bool> field_set(struct_ty->getNumElements(), false);

        auto insert_field = [&](std::size_t index, const IRValue &field_value)
        {
          if (index >= struct_ty->getNumElements())
          {
            return;
          }
          llvm::Type *elem_ty = struct_ty->getElementType(static_cast<unsigned>(index));
          llvm::Value *elem = EvaluateIRValue(field_value);
          elem = CoerceTo(builder, elem, elem_ty);
          if (!elem)
          {
            elem = llvm::Constant::getNullValue(elem_ty);
          }
          agg = builder->CreateInsertValue(agg, elem, {static_cast<unsigned>(index)});
          field_set[index] = true;
        };

        for (const auto &[field_name, field_value] : derived->fields)
        {
          std::optional<std::size_t> index;
          if (record_type)
          {
            if (auto meta = ResolveFieldAccessMeta(scope, record_type, field_name))
            {
              index = meta->index;
            }
          }
          if (!index.has_value())
          {
            for (std::size_t i = 0; i < struct_ty->getNumElements(); ++i)
            {
              if (!field_set[i])
              {
                index = i;
                break;
              }
            }
          }
          if (index.has_value())
          {
            insert_field(*index, field_value);
          }
        }

        materialized = agg;
        break;
      }
      case DerivedValueInfo::Kind::DynLit:
      {
        // Materialize the fat pointer struct {data_ptr, vtable_ptr}
        llvm::Value *data_ptr = EvaluateIRValue(derived->base);
        if (data_ptr && !data_ptr->getType()->isPointerTy())
        {
          data_ptr = builder->CreateIntToPtr(data_ptr, GetOpaquePtr());
        }
        if (!data_ptr)
        {
          data_ptr = llvm::ConstantPointerNull::get(
              llvm::cast<llvm::PointerType>(GetOpaquePtr()));
        }

        llvm::Constant *vtable_ptr = nullptr;
        if (!derived->vtable_sym.empty())
        {
          if (llvm::Value *gv = GetGlobal(derived->vtable_sym))
          {
            vtable_ptr = llvm::dyn_cast<llvm::Constant>(gv);
          }
          if (!vtable_ptr)
          {
            if (auto *gv = module_->getNamedGlobal(derived->vtable_sym))
            {
              vtable_ptr = gv;
            }
          }
          if (!vtable_ptr && current_ctx_ && current_ctx_->sigma)
          {
            analysis::TypeRef lazy_vtable_type = derived->dyn_impl_type;
            analysis::TypePath lazy_class_path = derived->dyn_class_path;
            if ((!lazy_vtable_type || lazy_class_path.empty()))
            {
              if (const auto *info =
                      current_ctx_->LookupRequiredVTable(derived->vtable_sym))
              {
                lazy_vtable_type = info->type;
                lazy_class_path = info->class_path;
              }
            }

            auto find_class_decl =
                [&](const analysis::TypePath &class_path)
                    -> const ast::ClassDecl * {
              if (class_path.empty())
              {
                return nullptr;
              }

              auto lookup_decl =
                  [&](const analysis::TypePath &candidate)
                      -> const ast::ClassDecl * {
                ast::Path class_ast_path(candidate.begin(), candidate.end());
                const auto class_it =
                    current_ctx_->sigma->classes.find(analysis::PathKeyOf(class_ast_path));
                if (class_it == current_ctx_->sigma->classes.end())
                {
                  return nullptr;
                }
                return &class_it->second;
              };

              if (const ast::ClassDecl *decl = lookup_decl(class_path))
              {
                return decl;
              }

              analysis::TypePath module_qualified = current_ctx_->module_path;
              module_qualified.insert(
                  module_qualified.end(), class_path.begin(), class_path.end());
              return lookup_decl(module_qualified);
            };

            if (lazy_vtable_type && !lazy_class_path.empty())
            {
              if (const ast::ClassDecl *class_decl = find_class_decl(lazy_class_path))
              {
                GlobalVTable lazy_vtable = ::cursive::codegen::EmitVTable(
                    lazy_vtable_type, lazy_class_path, *class_decl, *current_ctx_);
                EmitVTable(lazy_vtable);
                if (auto *gv = module_->getNamedGlobal(derived->vtable_sym))
                {
                  vtable_ptr = gv;
                }
              }
            }
          }
        }
        if (!vtable_ptr)
        {
          vtable_ptr = llvm::ConstantPointerNull::get(
              llvm::cast<llvm::PointerType>(GetOpaquePtr()));
        }

        // Fat pointer: {ptr, ptr}
        llvm::Type *ptr_ty = GetOpaquePtr();
        llvm::StructType *fat_ptr_ty = llvm::StructType::get(context_, {ptr_ty, ptr_ty});
        llvm::Value *fat = llvm::Constant::getNullValue(fat_ptr_ty);

        llvm::Value *data_as_ptr = data_ptr;
        if (data_as_ptr->getType() != ptr_ty)
        {
          data_as_ptr = builder->CreateBitCast(data_as_ptr, ptr_ty);
        }
        fat = builder->CreateInsertValue(fat, data_as_ptr, {0});

        llvm::Value *vtable_as_ptr = vtable_ptr;
        if (vtable_as_ptr->getType() != ptr_ty)
        {
          vtable_as_ptr = builder->CreateBitCast(vtable_as_ptr, ptr_ty);
        }
        fat = builder->CreateInsertValue(fat, vtable_as_ptr, {1});

        materialized = fat;
        break;
      }
      case DerivedValueInfo::Kind::TupleLit:
      {
        analysis::TypeRef tuple_type = tuple_type_for_value(val);
        const auto *tuple =
            tuple_type ? std::get_if<analysis::TypeTuple>(&tuple_type->node) : nullptr;
        llvm::Type *agg_ty = tuple_type ? GetLLVMType(tuple_type) : nullptr;
        auto *struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(agg_ty);
        if (tuple && struct_ty)
        {
          const auto layout =
              ::cursive::analysis::layout::RecordLayoutOf(scope, tuple->elements);
          llvm::Function *current_fn =
              builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
          if (layout.has_value() && current_fn &&
              layout->offsets.size() >= tuple->elements.size())
          {
            llvm::IRBuilder<> entry_builder(
                &current_fn->getEntryBlock(),
                current_fn->getEntryBlock().begin());
            llvm::AllocaInst *agg_slot = entry_builder.CreateAlloca(struct_ty);
            builder->CreateStore(llvm::Constant::getNullValue(struct_ty), agg_slot);
            llvm::Value *base_i8 = builder->CreateBitCast(
                agg_slot,
                llvm::PointerType::get(llvm::Type::getInt8Ty(context_), 0));
            const std::size_t count =
                std::min(derived->elements.size(), tuple->elements.size());
            for (std::size_t i = 0; i < count; ++i)
            {
              llvm::Type *elem_ll = GetLLVMType(tuple->elements[i]);
              if (!elem_ll || elem_ll->isVoidTy())
              {
                continue;
              }
              llvm::Value *elem = EvaluateIRValue(derived->elements[i]);
              analysis::TypeRef source_type =
                  lookup_value_type(derived->elements[i]);
              elem = CoerceToTyped(*this,
                                   builder,
                                   elem,
                                   elem_ll,
                                   source_type,
                                   tuple->elements[i]);
              if (!elem)
              {
                elem = llvm::Constant::getNullValue(elem_ll);
              }
              llvm::Value *field_i8 = builder->CreateGEP(
                  llvm::Type::getInt8Ty(context_),
                  base_i8,
                  llvm::ConstantInt::get(
                      llvm::Type::getInt64Ty(context_),
                      layout->offsets[i]));
              llvm::Value *field_ptr = builder->CreateBitCast(
                  field_i8,
                  llvm::PointerType::get(elem_ll, 0));
              llvm::StoreInst *store = builder->CreateStore(elem, field_ptr);
              store->setAlignment(llvm::Align(1));
            }
            materialized = builder->CreateLoad(struct_ty, agg_slot);
          }
          if (!materialized && current_ctx_)
          {
            current_ctx_->ReportCodegenFailure();
          }
          break;
        }

        std::vector<llvm::Type *> inferred_elem_tys;
        inferred_elem_tys.reserve(derived->elements.size());
        for (const auto &elem_value : derived->elements)
        {
          llvm::Value *elem = EvaluateIRValue(elem_value);
          inferred_elem_tys.push_back(
              elem ? elem->getType() : llvm::Type::getInt64Ty(context_));
        }
        agg_ty = llvm::StructType::get(context_, inferred_elem_tys);
        if (auto *fallback_struct = llvm::dyn_cast_or_null<llvm::StructType>(agg_ty))
        {
          llvm::Value *agg = llvm::Constant::getNullValue(fallback_struct);
          for (std::size_t i = 0; i < derived->elements.size(); ++i)
          {
            if (i >= fallback_struct->getNumElements())
            {
              break;
            }
            llvm::Type *elem_ty =
                fallback_struct->getElementType(static_cast<unsigned>(i));
            llvm::Value *elem = EvaluateIRValue(derived->elements[i]);
            elem = CoerceTo(builder, elem, elem_ty);
            if (!elem)
            {
              elem = llvm::Constant::getNullValue(elem_ty);
            }
            agg = builder->CreateInsertValue(
                agg, elem, {static_cast<unsigned>(i)});
          }
          materialized = agg;
        }
        break;
      }
      case DerivedValueInfo::Kind::ArrayLit:
      case DerivedValueInfo::Kind::ArrayRepeat:
      case DerivedValueInfo::Kind::ArraySegments:
      {
        llvm::Type *agg_ty = nullptr;
        if (analysis::TypeRef ty = lookup_value_type(val))
        {
          agg_ty = GetLLVMType(ty);
        }
        if (!agg_ty || (!agg_ty->isArrayTy() && !agg_ty->isStructTy()))
        {
          std::vector<llvm::Type *> inferred_elem_tys;
          if (derived->kind == DerivedValueInfo::Kind::ArrayRepeat)
          {
            llvm::Value *elem = EvaluateIRValue(derived->repeat_value);
            llvm::Type *elem_ty =
                elem ? elem->getType() : llvm::Type::getInt64Ty(context_);
            llvm::Value *count_value = EvaluateIRValue(derived->repeat_count);
            auto *count_int = llvm::dyn_cast_or_null<llvm::ConstantInt>(count_value);
            if (!count_int)
            {
              agg_ty = llvm::StructType::get(context_, inferred_elem_tys);
            }
            else
            {
              for (std::uint64_t i = 0; i < count_int->getZExtValue(); ++i)
              {
                inferred_elem_tys.push_back(elem_ty);
              }
            }
          }
          else if (derived->kind == DerivedValueInfo::Kind::ArraySegments)
          {
            for (const auto &segment : derived->array_segments)
            {
              llvm::Value *elem = EvaluateIRValue(segment.value);
              llvm::Type *elem_ty =
                  elem ? elem->getType() : llvm::Type::getInt64Ty(context_);
              if (segment.kind == DerivedArraySegment::Kind::Element)
              {
                inferred_elem_tys.push_back(elem_ty);
                continue;
              }
              if (!segment.count.has_value())
              {
                agg_ty = llvm::StructType::get(context_, inferred_elem_tys);
                break;
              }
              llvm::Value *count_value = EvaluateIRValue(*segment.count);
              auto *count_int = llvm::dyn_cast_or_null<llvm::ConstantInt>(count_value);
              if (!count_int)
              {
                agg_ty = llvm::StructType::get(context_, inferred_elem_tys);
                break;
              }
              for (std::uint64_t i = 0; i < count_int->getZExtValue(); ++i)
              {
                inferred_elem_tys.push_back(elem_ty);
              }
            }
          }
          else
          {
            inferred_elem_tys.reserve(derived->elements.size());
            for (const auto &elem_value : derived->elements)
            {
              llvm::Value *elem = EvaluateIRValue(elem_value);
              llvm::Type *elem_ty =
                  elem ? elem->getType() : llvm::Type::getInt64Ty(context_);
              inferred_elem_tys.push_back(elem_ty);
            }
          }
          llvm::Type *first_elem_ty = nullptr;
          bool all_same = true;
          for (const auto &elem_ty : inferred_elem_tys)
          {
            if (!first_elem_ty)
            {
              first_elem_ty = elem_ty;
            }
            else if (elem_ty != first_elem_ty)
            {
              all_same = false;
            }
          }
          if ((derived->kind == DerivedValueInfo::Kind::ArrayLit ||
               derived->kind == DerivedValueInfo::Kind::ArrayRepeat ||
               derived->kind == DerivedValueInfo::Kind::ArraySegments) &&
              all_same &&
              first_elem_ty)
          {
            agg_ty = llvm::ArrayType::get(
                first_elem_ty,
                static_cast<std::uint64_t>(inferred_elem_tys.size()));
          }
          else
          {
            agg_ty = llvm::StructType::get(context_, inferred_elem_tys);
          }
        }
        if (agg_ty && (agg_ty->isArrayTy() || agg_ty->isStructTy()))
        {
          llvm::Value *agg = llvm::Constant::getNullValue(agg_ty);
          std::vector<IRValue> materialized_elems;
          if (derived->kind == DerivedValueInfo::Kind::ArrayRepeat)
          {
            llvm::Value *count_value = EvaluateIRValue(derived->repeat_count);
            auto *count_int = llvm::dyn_cast_or_null<llvm::ConstantInt>(count_value);
            if (count_int)
            {
              for (std::uint64_t j = 0; j < count_int->getZExtValue(); ++j)
              {
                materialized_elems.push_back(derived->repeat_value);
              }
            }
          }
          else if (derived->kind == DerivedValueInfo::Kind::ArraySegments)
          {
            for (const auto &segment : derived->array_segments)
            {
              if (segment.kind == DerivedArraySegment::Kind::Element)
              {
                materialized_elems.push_back(segment.value);
                continue;
              }
              if (!segment.count.has_value())
              {
                continue;
              }
              llvm::Value *count_value = EvaluateIRValue(*segment.count);
              auto *count_int = llvm::dyn_cast_or_null<llvm::ConstantInt>(count_value);
              if (!count_int)
              {
                continue;
              }
              for (std::uint64_t j = 0; j < count_int->getZExtValue(); ++j)
              {
                materialized_elems.push_back(segment.value);
              }
            }
          }
          else
          {
            materialized_elems = derived->elements;
          }
          const std::size_t count = materialized_elems.size();
          for (std::size_t i = 0; i < count; ++i)
          {
            llvm::Value *elem = EvaluateIRValue(materialized_elems[i]);
            if (!elem)
            {
              continue;
            }
            llvm::Type *elem_ty = nullptr;
            if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(agg_ty))
            {
              elem_ty = arr_ty->getElementType();
            }
            else if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(agg_ty))
            {
              if (i < struct_ty->getNumElements())
              {
                elem_ty = struct_ty->getElementType(static_cast<unsigned>(i));
              }
            }
            if (!elem_ty)
            {
              continue;
            }
            elem = CoerceTo(builder, elem, elem_ty);
            if (!elem)
            {
              elem = llvm::Constant::getNullValue(elem_ty);
            }
            agg = builder->CreateInsertValue(agg, elem, {static_cast<unsigned>(i)});
          }
          materialized = agg;
        }
        break;
      }
      default:
        break;
      }

      if (!materialized)
      {
        materialized = default_for(val);
      }
      // Do not memoize block-local instruction values across control-flow joins.
      // Reusing branch-local GEP/load/extract instructions from another block can
      // violate LLVM dominance (observed in nested if/cleanup paths).
      if (llvm::isa<llvm::Constant>(materialized) ||
          llvm::isa<llvm::GlobalValue>(materialized) ||
          llvm::isa<llvm::Argument>(materialized) ||
          llvm::isa<llvm::AllocaInst>(materialized))
      {
        SetTempValue(val, materialized);
      }
      return materialized;
    }

    default:
      return nullptr;
    }
  }

} // namespace cursive::codegen
