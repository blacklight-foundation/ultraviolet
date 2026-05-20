// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/context_bundle.cpp
// Canonical owner for LLVM IR context bundle construction instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRContextBundleBuild &build) const
{
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  if (!active_ctx || !build.target_type)
  {
    emitter.SetTempValue(build.result, DefaultFor(build.result));
    return;
  }

  llvm::Value *root_ctx_value = EvaluateOrDefault(build.root_ctx);
  if (!root_ctx_value)
  {
    emitter.SetTempValue(build.result, DefaultFor(build.result));
    return;
  }

  llvm::Value *root_ctx_ptr = emitter.GetAddressableStorage(build.root_ctx);
  if (!root_ctx_ptr)
  {
    llvm::Function *func =
        builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
    if (func)
    {
      llvm::IRBuilder<> entry_builder(&func->getEntryBlock(),
                                      func->getEntryBlock().begin());
      llvm::AllocaInst *ctx_slot =
          entry_builder.CreateAlloca(root_ctx_value->getType(), nullptr, "ctx.bundle.root");
      builder.CreateStore(root_ctx_value, ctx_slot);
      root_ctx_ptr = ctx_slot;
    }
  }

  const analysis::ScopeContext &scope = BuildScope(active_ctx);
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
        {"io", analysis::MakeTypeDynamic({"IO"})},
        {"net", analysis::MakeTypeDynamic({"Network"})},
        {"heap", analysis::MakeTypeDynamic({"HeapAllocator"})},
        {"sys", analysis::MakeTypePath({"System"})},
        {"reactor", analysis::MakeTypeDynamic({"Reactor"})},
    }};
    std::size_t extract_index = 0u;
    for (const auto &field : fields)
    {
      const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, field.type).value_or(0u);
      if (std::string_view(field.name) == field_name)
      {
        if (size == 0u)
        {
          llvm::Type *field_ty = emitter.GetLLVMType(field.type);
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
          llvm::Value *ctx_ptr,
          llvm::Value *ctx_loaded) -> llvm::Value * {
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
            const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
            return nullptr;
          }

          const bool runtime_c_aggregate_boundary =
              RuntimeUsesCAggregateABI(runtime_sym);
          const bool runtime_foreign_boundary = RuntimeUsesForeignABI(runtime_sym);
          ABICallResult abi =
              ComputeCallABI(emitter,
                             runtime_info->params,
                             runtime_info->ret,
                             runtime_c_aggregate_boundary,
                             /*foreign_boundary_mode_independent=*/
                             runtime_foreign_boundary,
                             RuntimeUsesExplicitOutResultABI(runtime_sym));
          if (!abi.valid || !abi.func_type || abi.param_kinds.size() != 1u)
          {
            const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
            return nullptr;
          }
          llvm::Function *fn = emitter.GetModule().getFunction(runtime_sym);
          if (!fn)
          {
            fn = llvm::Function::Create(
                abi.func_type,
                llvm::GlobalValue::ExternalLinkage,
                runtime_sym,
                &emitter.GetModule());
            fn->setCallingConv(llvm::CallingConv::C);
          }

          llvm::Value *context_arg =
              abi.param_kinds[0] == PassKind::ByRef ? ctx_ptr : ctx_loaded;
          if (!context_arg)
          {
            const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
            return nullptr;
          }

          return EmitABICall(
              emitter,
              &irb,
              fn,
              runtime_info->params,
              runtime_info->ret,
              {context_arg},
              runtime_c_aggregate_boundary,
              /*ffi_import_boundary=*/false,
              /*ffi_import_catch=*/false,
              std::nullopt,
              nullptr,
              nullptr,
              nullptr,
              runtime_foreign_boundary);
        }
        const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
        return nullptr;
      }
      return context_field_value(irb, ctx_loaded, field_name);
    }

    if (const auto *path = std::get_if<analysis::TypePathType>(&cur->node))
    {
      if (path->generic_args.empty() && path->path.size() == 1u &&
          path->path.front() == "System")
      {
        llvm::Type *target_ll = emitter.GetLLVMType(cur);
        return target_ll && !target_ll->isVoidTy()
                   ? llvm::Constant::getNullValue(target_ll)
                   : nullptr;
      }

      if (const ast::RecordDecl *record = analysis::LookupRecordDecl(scope, path->path))
      {
        llvm::Type *target_ll = emitter.GetLLVMType(cur);
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
              self, irb, lowered.type, field->name, ctx_ptr, ctx_loaded);
          const auto field_size = ::ultraviolet::analysis::layout::SizeOf(scope, lowered.type).value_or(0u);
          if (field_size == 0u)
          {
            continue;
          }
          if (!field_value)
          {
            llvm::Type *field_ty = emitter.GetLLVMType(lowered.type);
            if (!field_ty || field_ty->isVoidTy())
            {
              continue;
            }
            field_value = llvm::Constant::getNullValue(field_ty);
          }
          aggregate =
              irb.CreateInsertValue(aggregate, field_value, {insert_index++});
        }
        return aggregate;
      }
    }

    return context_field_value(irb, ctx_loaded, field_name);
  };

  llvm::Value *bundle = build_context_bundle(
      build_context_bundle, builder, build.target_type, "", root_ctx_ptr, root_ctx_value);
  emitter.SetTempValue(build.result, bundle ? bundle : DefaultFor(build.result));
}

} // namespace ultraviolet::codegen::emit_detail
