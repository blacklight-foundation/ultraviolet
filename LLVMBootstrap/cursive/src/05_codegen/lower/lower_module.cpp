// =============================================================================
// MIGRATION MAPPING: lower_module.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6 (Code Generation)
//   - Lines 14196-14349: Codegen Model and Judgments
//   - Lines 15394-15665: Symbols, Mangling, and Linkage (for symbol generation)
//   - Lines 17161-17202: Dynamic Dispatch (for vtable emission)
//   - Module lowering orchestrates item-level code generation
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_module.cpp
//   - Lines 1-20: Includes and namespace
//   - Lines 21-50: Helper functions (IsUnitType, BlockEndsWithReturn, BuildScope)
//   - Lines 52-128: Type lowering helpers (LowerTypeForMethod, SubstSelfType, LowerReturnType)
//   - Lines 130-152: Parameter lowering (LowerParam, HasDynamicAttr)
//   - Lines 154-207: LowerProcLike - core procedure lowering logic
//   - Lines 209-256: SortedImplementations, DefaultUserList - class method helpers
//   - Lines 258-339: Method lowering (LowerRecordMethod, LowerStateMethod, LowerTransition)
//   - Lines 341-382: LowerClassMethodBody - class default implementations
//   - Lines 386-549: LowerModule - main module lowering entry point
//
// DEPENDENCIES:
//   - cursive/src/05_codegen/lower/lower_proc.h (LowerProc)
//   - cursive/src/05_codegen/lower/lower_stmt.h (LowerBlock, LowerStmtList)
//   - cursive/src/05_codegen/abi/abi.h (NeedsPanicOut, PanicOutType)
//   - cursive/src/05_codegen/cleanup.h (ComputeCleanupPlanForCurrentScope, EmitCleanup)
//   - cursive/src/05_codegen/dyn_dispatch.h (EmitVTable)
//   - cursive/src/05_codegen/globals.h (EmitGlobal, EmitModuleInitFn, EmitModuleDeinitFn)
//   - cursive/src/05_codegen/mangle.h (MangleProc, MangleMethod, MangleStateMethod, etc.)
//   - cursive/src/04_analysis/layout/layout.h (LowerTypeForLayout)
//   - cursive/src/04_analysis/types/types.h (TypeRef, MakeTypePrim, etc.)
//   - cursive/src/04_analysis/composite/classes.h (TypeImplementsClass)
//   - cursive/src/04_analysis/composite/record_methods.h (RecvTypeForReceiver, RecvModeOf)
//
// REFACTORING NOTES:
//   1. Module lowering is the top-level entry point for code generation
//   2. Key function LowerModule iterates over all module items and dispatches to:
//      - LowerProc for procedures
//      - LowerRecordMethod for record methods
//      - LowerStateMethod for modal state methods
//      - LowerTransition for modal transitions
//      - LowerClassMethodBody for class default implementations
//      - EmitVTable for class vtables
//      - EmitGlobal for static declarations
//   3. Self-type substitution (SubstSelfType) handles replacing 'Self' in method signatures
//   4. Module init/deinit functions handle static initialization order
//   5. Consider splitting method lowering into separate files for maintainability
//
// SPEC RULES IMPLEMENTED:
//   - Lower-Module: Main module lowering judgment
//   - CG-Item-Procedure, CG-Item-Procedure-Main: Procedure items
//   - CG-Item-Record, CG-Item-Method: Record and method items
//   - CG-Item-Modal, CG-Item-StateMethod, CG-Item-Transition: Modal items
//   - CG-Item-Class, CG-Item-ClassMethod-Abstract, CG-Item-ClassMethod-Body: Class items
//   - CG-Item-Static: Static declarations
//   - CG-Item-ExternBlock, CG-Item-ExternProc: FFI declarations
//   - CG-Item-Using, CG-Item-TypeAlias, CG-Item-Import: Non-codegen items
//
// =============================================================================

#include "05_codegen/lower/lower_module.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "05_codegen/abi/abi.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "05_codegen/dyn_dispatch/vtable_emit.h"
#include "05_codegen/globals/globals.h"
#include "05_codegen/globals/literal_emit.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_proc.h"
#include "05_codegen/lower/lower_stmt.h"
#include "05_codegen/symbols/linkage.h"
#include "05_codegen/symbols/mangle.h"
#include "01_project/ffi_library.h"
#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/attributes/ffi_library_attrs.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_control_flow.h"

namespace cursive::codegen {

namespace {

bool IsUnitType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  return false;
}

const analysis::ScopeContext& BuildScope(const ast::ModulePath& module_path,
                                         LowerCtx& ctx) {
  static const analysis::ScopeContext kEmptyScope{};

  struct ScopeCache {
    LowerCtx* ctx = nullptr;
    const analysis::Sigma* sigma = nullptr;
    ast::ModulePath module_path;
    std::optional<project::TargetProfile> target_profile;
    analysis::ExprTypeMap* expr_types = nullptr;
    analysis::DynamicRefineExprMap* dynamic_refine_checks = nullptr;
    analysis::GenericCallSubstMap* generic_call_substs = nullptr;
    analysis::SelectedCallTargetMap* selected_call_targets = nullptr;
    analysis::ScopeContext scope;
  };

  thread_local ScopeCache cache;
  if (!ctx.sigma) {
    return kEmptyScope;
  }

  if (cache.ctx != &ctx || cache.sigma != ctx.sigma ||
      cache.module_path != module_path ||
      cache.target_profile != ctx.target_profile ||
      cache.expr_types != ctx.expr_types ||
      cache.dynamic_refine_checks != ctx.dynamic_refine_checks ||
      cache.generic_call_substs != ctx.generic_call_substs ||
      cache.selected_call_targets != ctx.selected_call_targets) {
    cache.ctx = &ctx;
    cache.sigma = ctx.sigma;
    cache.module_path = module_path;
    cache.target_profile = ctx.target_profile;
    cache.expr_types = ctx.expr_types;
    cache.dynamic_refine_checks = ctx.dynamic_refine_checks;
    cache.generic_call_substs = ctx.generic_call_substs;
    cache.selected_call_targets = ctx.selected_call_targets;
    cache.scope = analysis::ScopeContext{};
    cache.scope.sigma = *ctx.sigma;
    cache.scope.sigma_source = ctx.sigma;
    cache.scope.current_module = module_path;
    cache.scope.target_profile = ctx.target_profile;
    cache.scope.expr_types = ctx.expr_types;
    cache.scope.dynamic_refine_checks = ctx.dynamic_refine_checks;
    cache.scope.generic_call_substs = ctx.generic_call_substs;
    cache.scope.selected_call_targets = ctx.selected_call_targets;
  }
  return cache.scope;
}

std::string NormalizeExternAbi(std::string_view abi_raw) {
  if (abi_raw.size() >= 2 &&
      ((abi_raw.front() == '"' && abi_raw.back() == '"') ||
       (abi_raw.front() == '\'' && abi_raw.back() == '\''))) {
    return std::string(abi_raw.substr(1, abi_raw.size() - 2));
  }
  return std::string(abi_raw);
}

std::optional<std::string> ExportAbiFor(const ast::AttributeList& attrs) {
  const auto abi = analysis::GetAttributeValue(attrs, analysis::attrs::kExport);
  if (!abi.has_value()) {
    return std::nullopt;
  }
  return NormalizeExternAbi(*abi);
}

std::string ExternAbiFor(const std::optional<ast::ExternAbi>& abi_opt) {
  if (!abi_opt.has_value()) {
    return "C";
  }
  return std::visit(
      [](const auto& abi_node) -> std::string {
        using T = std::decay_t<decltype(abi_node)>;
        if constexpr (std::is_same_v<T, ast::ExternAbiString>) {
          return NormalizeExternAbi(abi_node.literal.lexeme);
        } else {
          return abi_node.name;
        }
      },
      *abi_opt);
}

IRInlineMode InlineModeFor(const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name != analysis::attrs::kInline) {
      continue;
    }
    if (attr.args.empty()) {
      return IRInlineMode::Default;
    }
    for (const auto& arg : attr.args) {
      if (arg.key.has_value() && *arg.key != "kind") {
        continue;
      }
      const auto* token = std::get_if<ast::Token>(&arg.value);
      if (!token) {
        continue;
      }
      const std::string mode = NormalizeExternAbi(token->lexeme);
      if (mode == "always") {
        return IRInlineMode::Always;
      }
      if (mode == "never") {
        return IRInlineMode::Never;
      }
      return IRInlineMode::Default;
    }
  }
  return IRInlineMode::Default;
}

void ApplyProcAttrs(const ast::AttributeList& attrs, ProcIR& proc) {
  proc.inline_mode = InlineModeFor(attrs);
  proc.cold = analysis::HasAttribute(attrs, analysis::attrs::kCold);
}

bool ExternAbiUsesRawName(const std::optional<ast::ExternAbi>& abi_opt) {
  // Extern ABI defaults to "C" when omitted.
  if (!abi_opt.has_value()) {
    return true;
  }

  const std::string abi = std::visit(
      [](const auto& abi_node) -> std::string {
        using T = std::decay_t<decltype(abi_node)>;
        if constexpr (std::is_same_v<T, ast::ExternAbiString>) {
          return NormalizeExternAbi(abi_node.literal.lexeme);
        } else {
          return abi_node.name;
        }
      },
      *abi_opt);
  return abi == "C" || abi == "C-unwind";
}

std::optional<project::FfiLibrarySpec> ExternLibrarySpecFor(
    const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    const auto spec = analysis::NormalizeLibraryAttribute(attr);
    if (spec.has_value()) {
      return spec;
    }
  }
  return std::nullopt;
}

std::string ForeignExternProcSymbol(const ast::ModulePath& module_path,
                                    const std::optional<ast::ExternAbi>& abi_opt,
                                    const ast::ExternProcDecl& proc) {
  if (auto link_name = LinkName(proc.attrs, proc.name)) {
    return *link_name;
  }
  if (ExternAbiUsesRawName(abi_opt)) {
    return proc.name;
  }
  std::vector<std::string> path = module_path;
  path.push_back(proc.name);
  return ScopedSym(path);
}

std::string ExternProcSymbol(const ast::ModulePath& module_path,
                             const std::optional<ast::ExternAbi>& abi_opt,
                             const ast::ExternProcDecl& proc) {
  return ForeignExternProcSymbol(module_path, abi_opt, proc);
}

LowerCtx::FfiImportUnwindMode ParseExternUnwindMode(
    const ast::AttributeList& attrs,
    LowerCtx::FfiImportUnwindMode fallback) {
  for (const auto& attr : attrs) {
    if (attr.name != analysis::attrs::kUnwind) {
      continue;
    }
    if (attr.args.size() != 1 || attr.args.front().key.has_value()) {
      return LowerCtx::FfiImportUnwindMode::Abort;
    }
    const auto mode_tok = ast::get_attr_token_arg(attr, 0);
    if (!mode_tok.has_value() ||
        mode_tok->kind != lexer::TokenKind::StringLiteral) {
      return LowerCtx::FfiImportUnwindMode::Abort;
    }
    std::string mode = mode_tok->lexeme;
    if (mode.size() >= 2 &&
        ((mode.front() == '"' && mode.back() == '"') ||
         (mode.front() == '\'' && mode.back() == '\''))) {
      mode = mode.substr(1, mode.size() - 2);
    }
    if (mode == "catch") {
      return LowerCtx::FfiImportUnwindMode::Catch;
    }
    return LowerCtx::FfiImportUnwindMode::Abort;
  }
  return fallback;
}

ast::TypePath ResolveLoweringTypePath(const ast::TypePath& path,
                                      const LowerCtx& ctx) {
  if (path.size() != 1 || !ctx.resolve_type_name) {
    return path;
  }
  const auto resolved = ctx.resolve_type_name(path.front());
  if (!resolved.has_value() || resolved->empty()) {
    return path;
  }
  return *resolved;
}

std::shared_ptr<ast::Type> ResolveTypeNamesForLowering(
    const std::shared_ptr<ast::Type>& type,
    const LowerCtx& ctx) {
  if (!type) {
    return type;
  }

  auto out = std::make_shared<ast::Type>(*type);
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          node.base = ResolveTypeNamesForLowering(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (auto& member : node.types) {
            member = ResolveTypeNamesForLowering(member, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (auto& param : node.params) {
            param.type = ResolveTypeNamesForLowering(param.type, ctx);
          }
          node.ret = ResolveTypeNamesForLowering(node.ret, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (auto& param : node.params) {
            param.type = ResolveTypeNamesForLowering(param.type, ctx);
          }
          node.ret = ResolveTypeNamesForLowering(node.ret, ctx);
          if (node.deps_opt.has_value()) {
            for (auto& dep : *node.deps_opt) {
              dep.type = ResolveTypeNamesForLowering(dep.type, ctx);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (auto& element : node.elements) {
            element = ResolveTypeNamesForLowering(element, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          node.element = ResolveTypeNamesForLowering(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          node.element = ResolveTypeNamesForLowering(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          node.element = ResolveTypeNamesForLowering(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          node.element = ResolveTypeNamesForLowering(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          node.path = ResolveLoweringTypePath(node.path, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          node.path = ResolveLoweringTypePath(node.path, ctx);
          for (auto& arg : node.generic_args) {
            arg = ResolveTypeNamesForLowering(arg, ctx);
          }
          ast::SyncTypeModalStateFromFields(node);
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          node.path = ResolveLoweringTypePath(node.path, ctx);
          for (auto& arg : node.generic_args) {
            arg = ResolveTypeNamesForLowering(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          node.path = ResolveLoweringTypePath(node.path, ctx);
          for (auto& arg : node.args) {
            arg = ResolveTypeNamesForLowering(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          node.path = ResolveLoweringTypePath(node.path, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          node.base = ResolveTypeNamesForLowering(node.base, ctx);
        }
      },
      out->node);
  return out;
}

analysis::LowerTypeResult LowerTypeForMethod(const analysis::ScopeContext& scope,
                                             const std::shared_ptr<ast::Type>& type,
                                             const LowerCtx& ctx) {
  if (!type) {
    return {false, std::nullopt, nullptr};
  }
  const auto resolved_type = ResolveTypeNamesForLowering(type, ctx);
  if (auto lowered =
          ::cursive::analysis::layout::LowerTypeForLayout(scope, resolved_type)) {
    return {true, std::nullopt, *lowered};
  }
  return {false, std::nullopt, nullptr};
}

analysis::TypeRef SubstSelfType(const analysis::TypeRef& self,
                            const analysis::TypeRef& type) {
  if (!type || !self) {
    return type;
  }
  return std::visit(
      [&](const auto& node) -> analysis::TypeRef {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, analysis::TypePathType>) {
          if (analysis::IsSelfVarPath(node.path)) {
            return self;
          }
          return type;
        } else if constexpr (std::is_same_v<T, analysis::TypePerm>) {
          return analysis::MakeTypePerm(node.perm, SubstSelfType(self, node.base));
        } else if constexpr (std::is_same_v<T, analysis::TypeTuple>) {
          std::vector<analysis::TypeRef> elems;
          elems.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            elems.push_back(SubstSelfType(self, elem));
          }
          return analysis::MakeTypeTuple(std::move(elems));
        } else if constexpr (std::is_same_v<T, analysis::TypeArray>) {
          return analysis::MakeTypeArray(SubstSelfType(self, node.element), node.length);
        } else if constexpr (std::is_same_v<T, analysis::TypeSlice>) {
          return analysis::MakeTypeSlice(SubstSelfType(self, node.element));
        } else if constexpr (std::is_same_v<T, analysis::TypeUnion>) {
          std::vector<analysis::TypeRef> members;
          members.reserve(node.members.size());
          for (const auto& member : node.members) {
            members.push_back(SubstSelfType(self, member));
          }
          return analysis::MakeTypeUnion(std::move(members));
        } else if constexpr (std::is_same_v<T, analysis::TypeFunc>) {
          std::vector<analysis::TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            params.push_back(analysis::TypeFuncParam{param.mode,
                                                 SubstSelfType(self, param.type)});
          }
          return analysis::MakeTypeFunc(std::move(params), SubstSelfType(self, node.ret));
        } else if constexpr (std::is_same_v<T, analysis::TypeClosure>) {
          std::vector<std::pair<bool, analysis::TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            params.emplace_back(param.first, SubstSelfType(self, param.second));
          }
          std::optional<std::vector<analysis::SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<analysis::SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              deps.push_back(analysis::SharedDep{dep.name, SubstSelfType(self, dep.type)});
            }
            deps_opt = std::move(deps);
          }
          return analysis::MakeTypeClosure(std::move(params), SubstSelfType(self, node.ret), std::move(deps_opt));
        } else if constexpr (std::is_same_v<T, analysis::TypePtr>) {
          return analysis::MakeTypePtr(SubstSelfType(self, node.element), node.state);
        } else if constexpr (std::is_same_v<T, analysis::TypeRawPtr>) {
          return analysis::MakeTypeRawPtr(node.qual, SubstSelfType(self, node.element));
        } else if constexpr (std::is_same_v<T, analysis::TypeRefine>) {
          return analysis::MakeTypeRefine(SubstSelfType(self, node.base),
                                          node.predicate);
        } else {
          return type;
        }
      },
      type->node);
}

analysis::TypeRef LowerReturnType(const analysis::ScopeContext& scope,
                                  const std::shared_ptr<ast::Type>& ret_opt,
                                  const analysis::TypeRef& self_type,
                                  const LowerCtx& ctx) {
  if (!ret_opt) {
    return analysis::MakeTypePrim("()");
  }
  const auto resolved_ret = ResolveTypeNamesForLowering(ret_opt, ctx);
  const auto lowered = analysis::LowerType(scope, resolved_ret);
  if (lowered.ok && lowered.type) {
    return SubstSelfType(self_type, lowered.type);
  }
  return nullptr;
}

IRParam LowerParam(const ast::Param& param,
                   const analysis::ScopeContext& scope,
                   const analysis::TypeRef& self_type,
                   const LowerCtx& ctx) {
  IRParam out;
  out.mode = param.mode.has_value() ? std::optional<analysis::ParamMode>(analysis::ParamMode::Move)
                                    : std::nullopt;
  out.name = param.name;
  out.stable_name = out.name;
  if (param.type) {
    const auto resolved_type = ResolveTypeNamesForLowering(param.type, ctx);
    const auto lowered = analysis::LowerType(scope, resolved_type);
    if (lowered.ok && lowered.type) {
      out.type = SubstSelfType(self_type, lowered.type);
    }
  }
  return out;
}

analysis::TypeRef ApplyTypeSubst(
    const analysis::TypeRef& type,
    const std::optional<analysis::TypeSubst>& type_subst) {
  if (!type || !type_subst.has_value() || type_subst->empty()) {
    return type;
  }
  return analysis::InstantiateType(type, *type_subst);
}

IRParam LowerParamWithSubst(
    const ast::Param& param,
    const analysis::ScopeContext& scope,
    const analysis::TypeRef& self_type,
    const LowerCtx& ctx,
    const std::optional<analysis::TypeSubst>& type_subst) {
  IRParam out = LowerParam(param, scope, self_type, ctx);
  out.type = ApplyTypeSubst(out.type, type_subst);
  return out;
}

std::vector<analysis::TypeRef> ModalGenericArgsForSubst(
    const ast::ModalDecl& modal,
    const std::optional<analysis::TypeSubst>& type_subst) {
  std::vector<analysis::TypeRef> args;
  if (!modal.generic_params.has_value() ||
      modal.generic_params->params.empty()) {
    return args;
  }

  args.reserve(modal.generic_params->params.size());
  for (const auto& param : modal.generic_params->params) {
    analysis::TypeRef arg;
    if (type_subst.has_value()) {
      const auto it = type_subst->find(param.name);
      if (it != type_subst->end()) {
        arg = it->second;
      }
    }
    if (!arg) {
      arg = analysis::MakeTypePath({param.name});
    }
    args.push_back(arg);
  }
  return args;
}

analysis::VerificationModeAttribute ResolveForeignVerificationMode(
    const ast::AttributeList& proc_attrs) {
  if (const auto proc_mode = analysis::ResolveVerificationModeAttribute(proc_attrs);
      proc_mode.has_value()) {
    return *proc_mode;
  }

  return analysis::VerificationModeAttribute::Static;
}

bool ForeignDynamicChecksEnabled(const ast::AttributeList& proc_attrs) {
  switch (ResolveForeignVerificationMode(proc_attrs)) {
    case analysis::VerificationModeAttribute::Dynamic:
      return true;
    case analysis::VerificationModeAttribute::Static:
      return false;
  }
  return false;
}

IRParam PanicOutParam() {
  return ::cursive::codegen::PanicOutParam();
}

void AppendPanicOutParamIfNeeded(ProcIR& proc, LowerCtx& ctx) {
  if (ctx.NeedsPanicOutForSymbol(proc.symbol)) {
    proc.params.push_back(::cursive::codegen::PanicOutParam());
  }
}

bool HasExportAttr(const ast::AttributeList& attrs) {
  return analysis::HasAttribute(attrs, analysis::attrs::kExport);
}

bool HasHostExportAttr(const ast::AttributeList& attrs) {
  return analysis::HasAttribute(attrs, analysis::attrs::kHostExport);
}

std::optional<std::string> HostExportAbiFor(const ast::AttributeList& attrs) {
  const auto abi =
      analysis::GetAttributeValue(attrs, analysis::attrs::kHostExport);
  if (!abi.has_value()) {
    return std::nullopt;
  }
  return NormalizeExternAbi(*abi);
}

std::string HostedThunkSymbol(const ast::ModulePath& module_path,
                              const ast::ProcedureDecl& decl) {
  return HostThunkLinkName(module_path, decl);
}

std::string InternalProcSymbol(const ast::ModulePath& module_path,
                               const ast::ProcedureDecl& decl) {
  return MangleProc(module_path, decl);
}

std::string InternalProcSymbol(const ast::ASTModule& module,
                               const ast::ProcedureDecl& decl) {
  return MangleProcInModule(module, decl);
}

LowerCtx::ExportUnwindMode ExportUnwindModeFor(
    const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name != "unwind") {
      continue;
    }
    const auto mode_tok = ast::get_attr_token_arg(attr, 0);
    if (!mode_tok.has_value()) {
      return LowerCtx::ExportUnwindMode::Abort;
    }
    std::string mode = mode_tok->lexeme;
    if (mode.size() >= 2 &&
        ((mode.front() == '"' && mode.back() == '"') ||
         (mode.front() == '\'' && mode.back() == '\''))) {
      mode = mode.substr(1, mode.size() - 2);
    }
    if (mode == "catch") {
      return LowerCtx::ExportUnwindMode::Catch;
    }
    return LowerCtx::ExportUnwindMode::Abort;
  }
  return LowerCtx::ExportUnwindMode::Abort;
}

ProcIR LowerProcLike(const std::string& symbol,
                     const std::vector<IRParam>& params,
                     const analysis::TypeRef& ret_type,
                     const ast::Block& body,
                     const ast::ModulePath& module_path,
                     LowerCtx& ctx) {
  ProcIR ir;
  ir.symbol = symbol;
  ctx.module_path = module_path;
  const auto prev_proc_ret_type = ctx.proc_ret_type;
  const auto prev_current_proc_symbol = ctx.current_proc_symbol;
  const std::uint64_t prev_current_closure_counter =
      ctx.current_closure_counter;
  const auto prev_expr_prov = ctx.expr_prov;
  const auto prev_expr_region = ctx.expr_region;
  const auto prev_expr_region_tags = ctx.expr_region_tags;
  const auto prev_active_contract_postcondition =
      ctx.active_contract_postcondition;
  const auto prev_contract_result_value = ctx.contract_result_value;
  const auto prev_contract_entry_values = ctx.contract_entry_values;
  const auto prev_contract_param_entry_values =
      ctx.contract_param_entry_values;
  const bool prev_lowering_contract_postcondition =
      ctx.lowering_contract_postcondition;

  ctx.current_proc_symbol = symbol;
  ctx.current_closure_counter = 0;
  ctx.expr_prov.reset();
  ctx.expr_region.reset();
  ctx.expr_region_tags.reset();
  ctx.active_contract_postcondition = nullptr;
  ctx.contract_result_value.reset();
  ctx.contract_entry_values.clear();
  ctx.contract_param_entry_values.clear();
  ctx.lowering_contract_postcondition = false;

  ctx.PushScope(false, false);

  for (const auto& param : params) {
    IRParam lowered_param = param;
    const bool has_resp = param.mode.has_value();
    ctx.RegisterVar(param.name, param.type, has_resp, false);
    lowered_param.stable_name = ctx.StableBindingName(param.name);
    ir.params.push_back(std::move(lowered_param));
  }

  ir.ret = ret_type ? ret_type : analysis::MakeTypePrim("()");
  ctx.proc_ret_type = ir.ret;

  if (ctx.NeedsPanicOutForSymbol(ir.symbol)) {
    ir.params.push_back(PanicOutParam());
  }

  auto body_res = LowerBlock(body, ctx);

  IRPtr scope_enter_ir = EmptyIR();
  ctx.RegisterRuntimeScopeExitIfRequired();
  if (ctx.CurrentScopeRequiresRuntime()) {
    if (const auto scope_id = ctx.CurrentRuntimeScopeId()) {
      scope_enter_ir = EmitRuntimeScopeEnter(*scope_id, ctx);
    }
  }
  CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
  IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);
  ctx.PopScope();

  std::vector<IRPtr> body_seq;
  body_seq.push_back(scope_enter_ir);
  if (body_res.ir) {
    body_seq.push_back(body_res.ir);
  }
  const bool body_may_fallthrough = IRFlowMayFallThrough(body_res.ir);
  if (cleanup_ir && body_may_fallthrough) {
    body_seq.push_back(cleanup_ir);
  }

  const bool has_tail = body.tail_opt != nullptr;
  const bool ret_is_unit = IsUnitType(ir.ret);
  if (has_tail || (body_may_fallthrough && ret_is_unit)) {
    IRReturn ret;
    ret.value = body_res.value;
    body_seq.push_back(MakeIR(std::move(ret)));
  }

  ir.body = SeqIR(std::move(body_seq));
  ctx.proc_ret_type = prev_proc_ret_type;
  ctx.current_proc_symbol = prev_current_proc_symbol;
  ctx.current_closure_counter = prev_current_closure_counter;
  ctx.expr_prov = prev_expr_prov;
  ctx.expr_region = prev_expr_region;
  ctx.expr_region_tags = prev_expr_region_tags;
  ctx.active_contract_postcondition = prev_active_contract_postcondition;
  ctx.contract_result_value = prev_contract_result_value;
  ctx.contract_entry_values = prev_contract_entry_values;
  ctx.contract_param_entry_values = prev_contract_param_entry_values;
  ctx.lowering_contract_postcondition =
      prev_lowering_contract_postcondition;
  return ir;
}

std::vector<analysis::TypeRef> SortedImplementations(
    const analysis::ScopeContext& scope,
    const ast::ClassPath& class_path) {
  std::vector<std::pair<analysis::TypeRef, analysis::TypeKey>> types;
  for (const auto& [path_key, decl] : scope.sigma.types) {
    if (!std::holds_alternative<ast::RecordDecl>(decl) &&
        !std::holds_alternative<ast::EnumDecl>(decl) &&
        !std::holds_alternative<ast::ModalDecl>(decl)) {
      continue;
    }
    analysis::TypePath path;
    path.reserve(path_key.size());
    for (const auto& comp : path_key) {
      path.push_back(comp);
    }
    auto type = analysis::MakeTypePath(path);
    if (analysis::TypeImplementsClass(scope, type, class_path)) {
      types.push_back({type, analysis::TypeKeyOf(type)});
    }
  }

  std::sort(types.begin(), types.end(),
            [](const auto& lhs, const auto& rhs) {
              return analysis::TypeKeyLess(lhs.second, rhs.second);
            });

  std::vector<analysis::TypeRef> out;
  out.reserve(types.size());
  for (const auto& entry : types) {
    out.push_back(entry.first);
  }
  return out;
}

std::vector<analysis::TypeRef> DefaultUserList(
    const analysis::ScopeContext& scope,
    const ast::ClassPath& class_path,
    const ast::ClassMethodDecl& method) {
  std::vector<analysis::TypeRef> out;
  const auto types = SortedImplementations(scope, class_path);
  for (const auto& type : types) {
    const auto lookup = analysis::LookupMethodStatic(scope, type, method.name);
    if (lookup.ok && lookup.class_method && lookup.owner_class == class_path) {
      out.push_back(type);
    }
  }
  return out;
}

ProcIR LowerRecordMethod(const ast::RecordDecl& record,
                         const ast::MethodDecl& method,
                         const ast::ModulePath& module_path,
                         LowerCtx& ctx) {
  const auto& scope = BuildScope(module_path, ctx);

  analysis::TypePath record_path = module_path;
  record_path.push_back(record.name);
  auto self_type = analysis::MakeTypePath(record_path);

  const auto recv_type = analysis::RecvTypeForReceiver(
      scope, self_type, method.receiver,
      [&](const std::shared_ptr<ast::Type>& type) {
        return LowerTypeForMethod(scope, type, ctx);
      });
  const auto recv_mode = analysis::RecvModeOf(method.receiver);

  std::vector<IRParam> params;
  IRParam self_param;
  self_param.mode = recv_mode;
  self_param.name = "self";
  self_param.stable_name = self_param.name;
  self_param.type = recv_type.ok ? recv_type.type : self_type;
  params.push_back(self_param);

  for (const auto& param : method.params) {
    params.push_back(LowerParam(param, scope, self_type, ctx));
  }

  const auto ret_type =
      LowerReturnType(scope, method.return_type_opt, self_type, ctx);
  const std::string sym = MangleMethod(record_path, method);

  const bool prev_dynamic_checks = ctx.dynamic_checks;
  const bool record_method_dynamic =
      analysis::IsDynamicDecl(method) || analysis::IsDynamicDecl(record);
  ctx.dynamic_checks = record_method_dynamic;
  auto proc = LowerProcLike(sym, params, ret_type, *method.body, module_path, ctx);
  ctx.dynamic_checks = prev_dynamic_checks;
  ApplyProcAttrs(method.attrs, proc);
  return proc;
}

ProcIR LowerStateMethodWithSymbol(
    const ast::ModalDecl& modal,
    const ast::StateBlock& state,
    const ast::StateMethodDecl& method,
    const ast::ModulePath& module_path,
    const std::string& symbol,
    const std::optional<analysis::TypeSubst>& type_subst,
    LowerCtx& ctx) {
  const auto& scope = BuildScope(module_path, ctx);

  analysis::TypePath modal_path = module_path;
  modal_path.push_back(modal.name);
  auto state_type = analysis::MakeTypeModalState(
      modal_path, state.name, ModalGenericArgsForSubst(modal, type_subst));
  const auto recv_type = analysis::RecvTypeForReceiver(
      scope, state_type, method.receiver,
      [&](const std::shared_ptr<ast::Type>& type) {
        analysis::LowerTypeResult lowered = LowerTypeForMethod(scope, type, ctx);
        lowered.type = ApplyTypeSubst(lowered.type, type_subst);
        return lowered;
      });
  const auto recv_mode = analysis::RecvModeOf(method.receiver);

  std::vector<IRParam> params;
  params.push_back(IRParam{
      recv_mode,
      "self",
      "self",
      recv_type.ok ? recv_type.type
                   : analysis::MakeTypePerm(analysis::Permission::Const,
                                            state_type)});
  for (const auto& param : method.params) {
    params.push_back(
        LowerParamWithSubst(param, scope, state_type, ctx, type_subst));
  }

  auto ret_type = LowerReturnType(scope, method.return_type_opt, state_type, ctx);
  ret_type = ApplyTypeSubst(ret_type, type_subst);
  const bool prev_dynamic_checks = ctx.dynamic_checks;
  ctx.dynamic_checks =
      analysis::IsDynamicDecl(method) || analysis::IsDynamicDecl(modal);
  auto proc =
      LowerProcLike(symbol, params, ret_type, *method.body, module_path, ctx);
  ctx.dynamic_checks = prev_dynamic_checks;
  ApplyProcAttrs(method.attrs, proc);
  return proc;
}

ProcIR LowerStateMethod(const ast::ModalDecl& modal,
                        const ast::StateBlock& state,
                        const ast::StateMethodDecl& method,
                        const ast::ModulePath& module_path,
                        LowerCtx& ctx) {
  analysis::TypePath modal_path = module_path;
  modal_path.push_back(modal.name);
  return LowerStateMethodWithSymbol(
      modal,
      state,
      method,
      module_path,
      MangleStateMethod(modal_path, state.name, method),
      std::nullopt,
      ctx);
}

ProcIR LowerTransition(const ast::ModalDecl& modal,
                       const ast::StateBlock& state,
                       const ast::TransitionDecl& trans,
                       const ast::ModulePath& module_path,
                       LowerCtx& ctx) {
  const auto& scope = BuildScope(module_path, ctx);

  analysis::TypePath modal_path = module_path;
  modal_path.push_back(modal.name);
  auto state_type = analysis::MakeTypeModalState(modal_path, state.name);
  auto recv_type = analysis::MakeTypePerm(analysis::Permission::Unique, state_type);

  std::vector<IRParam> params;
  params.push_back(IRParam{analysis::ParamMode::Move, "self", "self", recv_type});
  for (const auto& param : trans.params) {
    params.push_back(LowerParam(param, scope, nullptr, ctx));
  }

  auto ret_type = analysis::MakeTypeModalState(modal_path, trans.target_state);
  const std::string sym = MangleTransition(modal_path, state.name, trans);
  const bool prev_dynamic_checks = ctx.dynamic_checks;
  ctx.dynamic_checks =
      analysis::IsDynamicDecl(trans) || analysis::IsDynamicDecl(modal);
  auto proc = LowerProcLike(sym, params, ret_type, *trans.body, module_path, ctx);
  ctx.dynamic_checks = prev_dynamic_checks;
  ApplyProcAttrs(trans.attrs, proc);
  return proc;
}

std::vector<ProcIR> LowerClassMethodBody(const ast::ClassDecl& class_decl,
                                         const ast::ClassMethodDecl& method,
                                         const ast::ModulePath& module_path,
                                         LowerCtx& ctx) {
  if (!method.body_opt) {
    SPEC_RULE("CG-Item-ClassMethod-Abstract");
    return {};
  }
  SPEC_RULE("CG-Item-ClassMethod-Body");

  const auto& scope = BuildScope(module_path, ctx);
  analysis::TypePath class_path = module_path;
  class_path.push_back(class_decl.name);

  std::vector<ProcIR> procs;
  const bool inherited_dynamic_checks =
      analysis::HasAttribute(method.attrs, analysis::attrs::kDynamic) ||
      analysis::HasAttribute(class_decl.attrs, analysis::attrs::kDynamic);
  const bool prev_dynamic_checks = ctx.dynamic_checks;
  ctx.dynamic_checks = inherited_dynamic_checks;
  const auto users = DefaultUserList(scope, class_path, method);
  for (const auto& self_type : users) {
    const auto recv_type = analysis::RecvTypeForReceiver(
        scope, self_type, method.receiver,
        [&](const std::shared_ptr<ast::Type>& type) {
          return LowerTypeForMethod(scope, type, ctx);
        });
    const auto recv_mode = analysis::RecvModeOf(method.receiver);

    std::vector<IRParam> params;
    IRParam self_param;
    self_param.mode = recv_mode;
    self_param.name = "self";
    self_param.stable_name = self_param.name;
    self_param.type = recv_type.ok ? recv_type.type : self_type;
    params.push_back(self_param);

    for (const auto& param : method.params) {
      params.push_back(LowerParam(param, scope, self_type, ctx));
    }

    const auto ret_type =
        LowerReturnType(scope, method.return_type_opt, self_type, ctx);
    const std::string sym = MangleDefaultImpl(self_type, class_path, method.name);
    auto proc = LowerProcLike(sym, params, ret_type, *method.body_opt, module_path, ctx);
    ApplyProcAttrs(method.attrs, proc);
    procs.push_back(std::move(proc));
  }
  ctx.dynamic_checks = prev_dynamic_checks;

  return procs;
}

ProcIR BuildProcedureSignature(const ast::ProcedureDecl& decl,
                               const ast::ModulePath& module_path,
                               LowerCtx& ctx,
                               std::optional<std::string> symbol_override =
                                   std::nullopt) {
  ProcIR ir;
  ir.symbol = symbol_override.value_or(InternalProcSymbol(module_path, decl));

  const auto& scope = BuildScope(module_path, ctx);
  for (const auto& param : decl.params) {
    ir.params.push_back(LowerParam(param, scope, nullptr, ctx));
  }
  ir.ret = LowerReturnType(scope, decl.return_type_opt, nullptr, ctx);
  ApplyProcAttrs(decl.attrs, ir);

  const bool is_export_proc = HasExportAttr(decl.attrs);
  if (is_export_proc) {
    for (auto& param : ir.params) {
      param.mode = analysis::ParamMode::Move;
    }
    ir.abi = ExportAbiFor(decl.attrs);
    ctx.RegisterExportUnwindMode(ir.symbol, ExportUnwindModeFor(decl.attrs));
  } else {
    AppendPanicOutParamIfNeeded(ir, ctx);
  }
  return ir;
}

ProcIR BuildProcedureSignature(const ast::ProcedureDecl& decl,
                               const ast::ASTModule& module,
                               LowerCtx& ctx) {
  return BuildProcedureSignature(
      decl, module.path, ctx, InternalProcSymbol(module, decl));
}

std::optional<LowerCtx::LocalContractInfo> BuildLocalContractInfo(
    const ast::ProcedureDecl& decl) {
  if (!decl.contract.has_value() || !decl.contract->precondition) {
    return std::nullopt;
  }
  LowerCtx::LocalContractInfo info;
  info.precondition = decl.contract->precondition;
  info.param_names.reserve(decl.params.size());
  for (const auto& param : decl.params) {
    info.param_names.push_back(param.name);
  }
  return info;
}

ProcIR BuildRecordMethodSignature(const ast::RecordDecl& record,
                                  const ast::MethodDecl& method,
                                  const ast::ModulePath& module_path,
                                  LowerCtx& ctx) {
  const auto& scope = BuildScope(module_path, ctx);
  analysis::TypePath record_path = module_path;
  record_path.push_back(record.name);
  auto self_type = analysis::MakeTypePath(record_path);

  const auto recv_type = analysis::RecvTypeForReceiver(
      scope, self_type, method.receiver,
      [&](const std::shared_ptr<ast::Type>& type) {
        return LowerTypeForMethod(scope, type, ctx);
      });
  const auto recv_mode = analysis::RecvModeOf(method.receiver);

  ProcIR ir;
  ir.symbol = MangleMethod(record_path, method);

  IRParam self_param;
  self_param.mode = recv_mode;
  self_param.name = "self";
  self_param.stable_name = self_param.name;
  self_param.type = recv_type.ok ? recv_type.type : self_type;
  ir.params.push_back(std::move(self_param));

  for (const auto& param : method.params) {
    ir.params.push_back(LowerParam(param, scope, self_type, ctx));
  }
  ir.ret = LowerReturnType(scope, method.return_type_opt, self_type, ctx);
  AppendPanicOutParamIfNeeded(ir, ctx);
  return ir;
}

ProcIR BuildStateMethodSignature(const ast::ModalDecl& modal,
                                 const ast::StateBlock& state,
                                 const ast::StateMethodDecl& method,
                                 const ast::ModulePath& module_path,
                                 LowerCtx& ctx) {
  const auto& scope = BuildScope(module_path, ctx);
  analysis::TypePath modal_path = module_path;
  modal_path.push_back(modal.name);
  auto state_type = analysis::MakeTypeModalState(modal_path, state.name);
  const auto recv_type = analysis::RecvTypeForReceiver(
      scope, state_type, method.receiver,
      [&](const std::shared_ptr<ast::Type>& type) {
        return LowerTypeForMethod(scope, type, ctx);
      });
  const auto recv_mode = analysis::RecvModeOf(method.receiver);

  ProcIR ir;
  ir.symbol = MangleStateMethod(modal_path, state.name, method);
  ir.params.push_back(IRParam{
      recv_mode,
      "self",
      "self",
      recv_type.ok ? recv_type.type
                   : analysis::MakeTypePerm(analysis::Permission::Const,
                                            state_type)});
  for (const auto& param : method.params) {
    ir.params.push_back(LowerParam(param, scope, state_type, ctx));
  }
  ir.ret = LowerReturnType(scope, method.return_type_opt, state_type, ctx);
  AppendPanicOutParamIfNeeded(ir, ctx);
  return ir;
}

ProcIR BuildTransitionSignature(const ast::ModalDecl& modal,
                                const ast::StateBlock& state,
                                const ast::TransitionDecl& trans,
                                const ast::ModulePath& module_path,
                                LowerCtx& ctx) {
  const auto& scope = BuildScope(module_path, ctx);
  analysis::TypePath modal_path = module_path;
  modal_path.push_back(modal.name);
  auto state_type = analysis::MakeTypeModalState(modal_path, state.name);
  auto recv_type = analysis::MakeTypePerm(analysis::Permission::Unique, state_type);

  ProcIR ir;
  ir.symbol = MangleTransition(modal_path, state.name, trans);
  ir.params.push_back(IRParam{analysis::ParamMode::Move, "self", "self", recv_type});
  for (const auto& param : trans.params) {
    ir.params.push_back(LowerParam(param, scope, nullptr, ctx));
  }
  ir.ret = analysis::MakeTypeModalState(modal_path, trans.target_state);
  AppendPanicOutParamIfNeeded(ir, ctx);
  return ir;
}

std::vector<ProcIR> BuildClassMethodSignatures(const ast::ClassDecl& class_decl,
                                               const ast::ClassMethodDecl& method,
                                               const ast::ModulePath& module_path,
                                               LowerCtx& ctx) {
  if (!method.body_opt) {
    return {};
  }

  const auto& scope = BuildScope(module_path, ctx);
  analysis::TypePath class_path = module_path;
  class_path.push_back(class_decl.name);

  std::vector<ProcIR> out;
  const auto users = DefaultUserList(scope, class_path, method);
  out.reserve(users.size());

  for (const auto& self_type : users) {
    const auto recv_type = analysis::RecvTypeForReceiver(
        scope, self_type, method.receiver,
        [&](const std::shared_ptr<ast::Type>& type) {
          return LowerTypeForMethod(scope, type, ctx);
        });
    const auto recv_mode = analysis::RecvModeOf(method.receiver);

    ProcIR ir;
    ir.symbol = MangleDefaultImpl(self_type, class_path, method.name);

    IRParam self_param;
    self_param.mode = recv_mode;
    self_param.name = "self";
    self_param.stable_name = self_param.name;
    self_param.type = recv_type.ok ? recv_type.type : self_type;
    ir.params.push_back(std::move(self_param));

    for (const auto& param : method.params) {
      ir.params.push_back(LowerParam(param, scope, self_type, ctx));
    }
    ir.ret = LowerReturnType(scope, method.return_type_opt, self_type, ctx);
    AppendPanicOutParamIfNeeded(ir, ctx);
    out.push_back(std::move(ir));
  }

  return out;
}

}  // namespace

ProcIR LowerStateMethodInstantiated(
    const ast::ModalDecl& modal,
    const ast::StateBlock& state,
    const ast::StateMethodDecl& method,
    const ast::ModulePath& module_path,
    const std::string& symbol_override,
    const std::map<std::string, analysis::TypeRef>& type_subst,
    LowerCtx& ctx) {
  struct InstantiationCtxSnapshot {
    std::vector<std::string> module_path;
    std::vector<ScopeInfo> scope_stack;
    std::unordered_map<std::string, std::vector<BindingState>> binding_states;
    std::unordered_map<std::string, DerivedValueInfo> derived_values;
    std::vector<TempValue>* temp_sink = nullptr;
    int temp_depth = 0;
    std::optional<int> suppress_temp_at_depth;
    std::vector<ParallelCollectItem>* parallel_collect = nullptr;
    int parallel_collect_depth = 0;
    std::optional<CaptureEnvInfo> capture_env;
    analysis::TypeRef proc_ret_type;
    std::optional<std::string> current_proc_symbol;
    std::uint64_t current_closure_counter = 0;
    std::shared_ptr<const std::unordered_map<const ast::Expr*,
                                             analysis::ProvenanceKind>>
        expr_prov;
    std::shared_ptr<const std::unordered_map<const ast::Expr*, std::string>>
        expr_region;
    std::shared_ptr<const std::unordered_map<const ast::Expr*, std::string>>
        expr_region_tags;
    std::vector<std::string> active_region_aliases;
    bool dynamic_checks = false;
    const ast::Expr* active_contract_postcondition = nullptr;
    std::optional<IRValue> contract_result_value;
    std::unordered_map<const ast::EntryExpr*, IRValue> contract_entry_values;
    std::unordered_map<std::string, IRValue> contract_param_entry_values;
    bool lowering_contract_postcondition = false;
    std::optional<analysis::TypeSubst> active_generic_type_subst;

    explicit InstantiationCtxSnapshot(const LowerCtx& source)
        : module_path(source.module_path),
          scope_stack(source.scope_stack),
          binding_states(source.binding_states),
          derived_values(source.values.derived_values),
          temp_sink(source.temp_sink),
          temp_depth(source.temp_depth),
          suppress_temp_at_depth(source.suppress_temp_at_depth),
          parallel_collect(source.parallel_collect),
          parallel_collect_depth(source.parallel_collect_depth),
          capture_env(source.capture_env),
          proc_ret_type(source.proc_ret_type),
          current_proc_symbol(source.current_proc_symbol),
          current_closure_counter(source.current_closure_counter),
          expr_prov(source.expr_prov),
          expr_region(source.expr_region),
          expr_region_tags(source.expr_region_tags),
          active_region_aliases(source.active_region_aliases),
          dynamic_checks(source.dynamic_checks),
          active_contract_postcondition(source.active_contract_postcondition),
          contract_result_value(source.contract_result_value),
          contract_entry_values(source.contract_entry_values),
          contract_param_entry_values(source.contract_param_entry_values),
          lowering_contract_postcondition(source.lowering_contract_postcondition),
          active_generic_type_subst(source.active_generic_type_subst) {}

    void Restore(LowerCtx& target) const {
      target.module_path = module_path;
      target.scope_stack = scope_stack;
      target.binding_states = binding_states;
      auto preserved_derived = std::move(target.values.derived_values);
      target.values.derived_values = derived_values;
      for (auto& [name, info] : preserved_derived) {
        target.values.derived_values.emplace(std::move(name), std::move(info));
      }
      target.temp_sink = temp_sink;
      target.temp_depth = temp_depth;
      target.suppress_temp_at_depth = suppress_temp_at_depth;
      target.parallel_collect = parallel_collect;
      target.parallel_collect_depth = parallel_collect_depth;
      target.capture_env = capture_env;
      target.proc_ret_type = proc_ret_type;
      target.current_proc_symbol = current_proc_symbol;
      target.current_closure_counter = current_closure_counter;
      target.expr_prov = expr_prov;
      target.expr_region = expr_region;
      target.expr_region_tags = expr_region_tags;
      target.active_region_aliases = active_region_aliases;
      target.dynamic_checks = dynamic_checks;
      target.active_contract_postcondition = active_contract_postcondition;
      target.contract_result_value = contract_result_value;
      target.contract_entry_values = contract_entry_values;
      target.contract_param_entry_values = contract_param_entry_values;
      target.lowering_contract_postcondition = lowering_contract_postcondition;
      target.active_generic_type_subst = active_generic_type_subst;
    }
  };

  InstantiationCtxSnapshot snapshot(ctx);
  ctx.module_path.clear();
  ctx.scope_stack.clear();
  ctx.binding_states.clear();
  ctx.values.derived_values.clear();
  ctx.temp_sink = nullptr;
  ctx.temp_depth = 0;
  ctx.suppress_temp_at_depth.reset();
  ctx.parallel_collect = nullptr;
  ctx.parallel_collect_depth = 0;
  ctx.capture_env.reset();
  ctx.proc_ret_type = nullptr;
  ctx.current_proc_symbol = symbol_override;
  ctx.current_closure_counter = 0;
  ctx.expr_prov.reset();
  ctx.expr_region.reset();
  ctx.expr_region_tags.reset();
  ctx.active_region_aliases.clear();
  ctx.active_contract_postcondition = nullptr;
  ctx.contract_result_value.reset();
  ctx.contract_entry_values.clear();
  ctx.contract_param_entry_values.clear();
  ctx.lowering_contract_postcondition = false;
  ctx.active_generic_type_subst = type_subst;

  std::vector<std::string> inserted_value_type_keys;
  auto* prev_value_type_insert_sink = ctx.values.value_type_insert_sink;
  ctx.values.value_type_insert_sink = &inserted_value_type_keys;

  ProcIR ir = LowerStateMethodWithSymbol(
      modal, state, method, module_path, symbol_override, type_subst, ctx);
  ctx.values.value_type_insert_sink = prev_value_type_insert_sink;
  snapshot.Restore(ctx);
  if (prev_value_type_insert_sink && !inserted_value_type_keys.empty()) {
    prev_value_type_insert_sink->insert(prev_value_type_insert_sink->end(),
                                        inserted_value_type_keys.begin(),
                                        inserted_value_type_keys.end());
  }

  return ir;
}

bool RegisterModuleSignatures(const ast::ASTModule& module, LowerCtx& ctx) {
  ctx.module_path = module.path;

  auto register_user_proc = [&](const ProcIR& proc,
                                LinkageKind linkage,
                                ast::Visibility visibility) {
    ctx.RegisterProcSig(proc);
    ctx.RegisterProcLinkage(proc.symbol, linkage);
    ctx.RegisterProcVisibility(proc.symbol, visibility);
    ctx.RegisterProcModule(proc.symbol, module.path);
  };

  auto register_internal_proc = [&](const ProcIR& proc) {
    ctx.RegisterProcSig(proc);
    ctx.RegisterProcLinkage(proc.symbol, LinkageKind::Internal);
  };

  for (const auto& item : module.items) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::StaticDecl>) {
            // Static symbol metadata is still registered during full lowering.
            // Signature pre-registration focuses on procedure call metadata.
            (void)node;
          } else if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            if (node.generic_params.has_value() &&
                !node.generic_params->params.empty()) {
              return;
            }
            auto sig = BuildProcedureSignature(node, module, ctx);
            const LinkageKind proc_linkage =
                LinkageOf(node);
            register_user_proc(sig, proc_linkage, node.vis);
            if (HasHostExportAttr(node.attrs)) {
              LowerCtx::HostedExportInfo info;
              info.internal_symbol = sig.symbol;
              info.thunk_symbol = HostedThunkSymbol(module.path, node);
              info.abi = HostExportAbiFor(node.attrs);
              info.context_type = sig.params.empty() ? nullptr : sig.params.front().type;
              info.ret = sig.ret;
              info.unwind_mode = ExportUnwindModeFor(node.attrs);
              if (sig.params.size() > 1) {
                info.visible_params.assign(sig.params.begin() + 1, sig.params.end());
                if (!info.visible_params.empty() &&
                    info.visible_params.back().name ==
                        std::string(kPanicOutName)) {
                  info.visible_params.pop_back();
                }
              }
              ctx.hosted_exports.push_back(std::move(info));
            }
            if (const auto contract = BuildLocalContractInfo(node);
                contract.has_value()) {
              ctx.RegisterLocalContractInfo(sig.symbol, *contract);
            }
          } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
            std::vector<std::string> record_path = module.path;
            record_path.push_back(node.name);
            ctx.RegisterRecordCtor(ScopedSym(record_path), record_path);
            for (const auto& member : node.members) {
              if (const auto* method = std::get_if<ast::MethodDecl>(&member)) {
                register_user_proc(
                    BuildRecordMethodSignature(node, *method, module.path, ctx),
                    LinkageOf(*method),
                    method->vis);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
            const bool generic_modal =
                node.generic_params.has_value() &&
                !node.generic_params->params.empty();
            for (const auto& state : node.states) {
              for (const auto& member : state.members) {
                if (const auto* method =
                        std::get_if<ast::StateMethodDecl>(&member)) {
                  if (generic_modal) {
                    continue;
                  }
                  register_user_proc(
                      BuildStateMethodSignature(node, state, *method, module.path, ctx),
                      LinkageOf(*method),
                      method->vis);
                }
              }
              for (const auto& member : state.members) {
                if (const auto* trans = std::get_if<ast::TransitionDecl>(&member)) {
                  if (generic_modal) {
                    continue;
                  }
                  register_user_proc(
                      BuildTransitionSignature(node, state, *trans, module.path, ctx),
                      LinkageOf(*trans),
                      trans->vis);
                }
              }
            }
          } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
            for (const auto& class_item : node.items) {
              if (const auto* method =
                      std::get_if<ast::ClassMethodDecl>(&class_item)) {
                auto sigs =
                    BuildClassMethodSignatures(node, *method, module.path, ctx);
                for (const auto& proc : sigs) {
                  register_user_proc(proc, LinkageOf(*method), method->vis);
                }
              }
            }
          } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            const auto& scope = BuildScope(module.path, ctx);
            const auto& block_attrs = ast::AttrListOf(node);
            const auto block_unwind_mode = ParseExternUnwindMode(
                block_attrs,
                LowerCtx::FfiImportUnwindMode::Abort);
            for (const auto& ext_item : node.items) {
              std::visit(
                [&](const auto& proc) {
                  using PT = std::decay_t<decltype(proc)>;
                  if constexpr (std::is_same_v<PT, ast::ExternProcDecl>) {
                    const std::string symbol =
                        ExternProcSymbol(module.path,
                                         node.abi_opt,
                                         proc);

                    ProcIR sig;
                    sig.symbol = symbol;
                    for (const auto& param : proc.params) {
                      sig.params.push_back(LowerParam(param, scope, nullptr, ctx));
                    }
                    sig.ret =
                        LowerReturnType(scope, proc.return_type_opt, nullptr, ctx);
                    sig.abi = ExternAbiFor(node.abi_opt);
                    register_internal_proc(sig);
                    ctx.RegisterProcFfiImport(
                        symbol,
                        ParseExternUnwindMode(proc.attrs, block_unwind_mode));
                  }
                },
                ext_item);
            }
          }
        },
        item);
    if (ctx.resolve_failed || ctx.codegen_failed) {
      return false;
    }
  }

  ProcIR init_sig;
  init_sig.symbol = InitFn(module.path);
  init_sig.params.push_back(::cursive::codegen::PanicOutParam());
  init_sig.ret = analysis::MakeTypePrim("()");
  register_internal_proc(init_sig);

  ProcIR deinit_sig;
  deinit_sig.symbol = DeinitFn(module.path);
  deinit_sig.params.push_back(::cursive::codegen::PanicOutParam());
  deinit_sig.ret = analysis::MakeTypePrim("()");
  register_internal_proc(deinit_sig);

  return !(ctx.resolve_failed || ctx.codegen_failed);
}

IRDecls LowerModule(const ast::ASTModule& module, LowerCtx& ctx) {
  SPEC_RULE("Lower-Module");

  IRDecls decls;
  ctx.module_path = module.path;
  // Synthetic procedures are generated while lowering expressions (closures,
  // spawn/dispatch wrappers, async resume helpers). Ensure each module starts
  // with a clean queue and drain generated procedures into IR decls.
  ctx.extra_procs.clear();

  // Keep module-level metadata but avoid letting per-item lowering carry the
  // full accumulated maps. This keeps branch-local LowerCtx copies cheaper.
  std::unordered_map<std::string, analysis::TypeRef> module_value_types;
  std::unordered_map<std::string, DerivedValueInfo> module_derived_values;
  std::unordered_map<std::string, analysis::TypeRef> module_drop_glue_types;

  auto flush_item_maps = [&]() {
    if (!ctx.values.value_types.empty()) {
      for (auto& [name, type] : ctx.values.value_types) {
        module_value_types[name] = type;
      }
      ctx.values.value_types.clear();
    }
    if (!ctx.values.derived_values.empty()) {
      for (auto& [name, info] : ctx.values.derived_values) {
        module_derived_values[name] = std::move(info);
      }
      ctx.values.derived_values.clear();
    }
    if (!ctx.values.drop_glue_types.empty()) {
      for (auto& [name, type] : ctx.values.drop_glue_types) {
        module_drop_glue_types[name] = type;
      }
      ctx.values.drop_glue_types.clear();
    }
  };

  auto register_proc = [&](const ProcIR& proc,
                           bool user,
                           std::optional<LinkageKind> linkage = std::nullopt) {
    ctx.RegisterProcSig(proc);
    if (linkage.has_value()) {
      ctx.RegisterProcLinkage(proc.symbol, *linkage);
    }
    if (user) {
      ctx.RegisterProcModule(proc.symbol, module.path);
    }
  };
  auto drain_extra_procs = [&]() {
    if (ctx.extra_procs.empty()) {
      return;
    }
    std::vector<ProcIR> synthesized;
    synthesized.swap(ctx.extra_procs);
    for (auto& proc : synthesized) {
      const auto linkage = ctx.LookupProcLinkage(proc.symbol).value_or(LinkageKind::Internal);
      register_proc(proc, false, linkage);
      decls.push_back(std::move(proc));
    }
  };

  const bool debug_lower_module = core::IsDebugEnabled("codegen");
  const std::string module_key = core::StringOfPath(module.path);
  std::size_t item_index = 0;
  for (const auto& item : module.items) {
    ++item_index;
    if (debug_lower_module) {
      std::cerr << "[lower-module-debug] module=" << module_key
                << " item_index=" << item_index
                << " variant=" << item.index()
                << " stage=item-start\n";
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::UsingDecl>) {
            SPEC_RULE("CG-Item-Using");
            return;
          } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
            SPEC_RULE("CG-Item-TypeAlias");
            return;
          } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
            SPEC_RULE("CG-Item-Static");
            auto res = EmitGlobal(node, module.path, ctx);
            decls.insert(decls.end(), res.decls.begin(), res.decls.end());
            return;
          } else if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            if (node.generic_params.has_value() &&
                !node.generic_params->params.empty()) {
              // Generic procedures are instantiated at concrete call sites.
              // Do not emit a monomorphic body from the generic declaration.
              SPEC_RULE("CG-Item-Procedure");
              return;
            }
            if (ctx.executable_project && node.name == "main") {
              SPEC_RULE("CG-Item-Procedure-Main");
            } else {
              SPEC_RULE("CG-Item-Procedure");
            }
            auto proc = LowerProc(node, module.path, ctx,
                                  InternalProcSymbol(module, node));
            if (ctx.resolve_failed || ctx.codegen_failed) {
              return;
            }
            const LinkageKind proc_linkage =
                LinkageOf(node);
            register_proc(proc, true, proc_linkage);
            if (const auto contract = BuildLocalContractInfo(node);
                contract.has_value()) {
              ctx.RegisterLocalContractInfo(proc.symbol, *contract);
            }
            if (ctx.executable_project && node.name == "main") {
              ctx.main_symbol = proc.symbol;
            }
            decls.push_back(std::move(proc));
            return;
          } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
            SPEC_RULE("CG-Item-Record");
            std::vector<std::string> record_path = module.path;
            record_path.push_back(node.name);
            ctx.RegisterRecordCtor(ScopedSym(record_path), record_path);
            for (const auto& member : node.members) {
              if (const auto* method = std::get_if<ast::MethodDecl>(&member)) {
                SPEC_RULE("CG-Item-Method");
                auto proc = LowerRecordMethod(node, *method, module.path, ctx);
                register_proc(proc, true, LinkageOf(*method));
                decls.push_back(std::move(proc));
              }
            }
            return;
          } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
            SPEC_RULE("CG-Item-Modal");
            const bool generic_modal =
                node.generic_params.has_value() &&
                !node.generic_params->params.empty();
            for (const auto& state : node.states) {
              for (const auto& member : state.members) {
                if (const auto* method = std::get_if<ast::StateMethodDecl>(&member)) {
                  SPEC_RULE("CG-Item-StateMethod");
                  if (generic_modal) {
                    continue;
                  }
                  auto proc = LowerStateMethod(node, state, *method, module.path, ctx);
                  register_proc(proc, true, LinkageOf(*method));
                  decls.push_back(std::move(proc));
                }
              }
              for (const auto& member : state.members) {
                if (const auto* trans = std::get_if<ast::TransitionDecl>(&member)) {
                  SPEC_RULE("CG-Item-Transition");
                  if (generic_modal) {
                    continue;
                  }
                  auto proc = LowerTransition(node, state, *trans, module.path, ctx);
                  register_proc(proc, true, LinkageOf(*trans));
                  decls.push_back(std::move(proc));
                }
              }
            }
            return;
          } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
            SPEC_RULE("CG-Item-Class");
            for (const auto& item : node.items) {
              if (const auto* method = std::get_if<ast::ClassMethodDecl>(&item)) {
                auto procs = LowerClassMethodBody(node, *method, module.path, ctx);
                for (auto& proc : procs) {
                  register_proc(proc, true, LinkageOf(*method));
                  decls.push_back(std::move(proc));
                }
              }
            }
            return;
          } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
            SPEC_RULE("CG-Item-Enum");
            return;
          } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            SPEC_RULE("CG-Item-ExternBlock");
            // Emit external declarations for each procedure in the extern block
            const auto& scope = BuildScope(module.path, ctx);
            const auto& block_attrs = ast::AttrListOf(node);
            const auto block_unwind_mode = ParseExternUnwindMode(
                block_attrs,
                LowerCtx::FfiImportUnwindMode::Abort);
            for (const auto& ext_item : node.items) {
              std::visit(
                  [&](const auto& proc) {
                    using PT = std::decay_t<decltype(proc)>;
                    if constexpr (std::is_same_v<PT, ast::ExternProcDecl>) {
                      SPEC_RULE("CG-Item-ExternProc");
                      const auto library_spec = ExternLibrarySpecFor(block_attrs);
                      std::string symbol =
                          ExternProcSymbol(module.path,
                                           node.abi_opt,
                                           proc);
                      if (library_spec.has_value() &&
                          library_spec->kind == "raw-dylib") {
                        ast::Path internal_path = module.path;
                        internal_path.push_back(proc.name);
                        symbol = ScopedSym(internal_path);
                      }
                      const auto unwind_mode =
                          ParseExternUnwindMode(proc.attrs, block_unwind_mode);

                      // Build parameter list
                      std::vector<IRParam> params;
                      for (const auto& param : proc.params) {
                        params.push_back(LowerParam(param, scope, nullptr, ctx));
                      }
                      // Extern declarations describe a foreign ABI surface.
                      // Their visible parameters are lowered as by-value ABI
                      // parameters, not ordinary aliased Cursive parameters.
                      for (auto& param : params) {
                        param.mode = analysis::ParamMode::Move;
                      }

                      // Determine return type
                      auto ret_type =
                          LowerReturnType(scope, proc.return_type_opt, nullptr, ctx);

                      // Create extern proc IR
                      ExternProcIR extern_proc;
                      extern_proc.symbol = symbol;
                      extern_proc.params = std::move(params);
                      extern_proc.ret = ret_type;
                      extern_proc.abi = ExternAbiFor(node.abi_opt);
                      if (library_spec.has_value() &&
                          library_spec->kind == "raw-dylib") {
                        const auto dll_name =
                            project::ResolveLibraryNameForCurrentTarget(
                                library_spec->name,
                                library_spec->kind,
                                project::TargetProfile::X86_64Win64);
                        if (dll_name.has_value()) {
                          extern_proc.raw_dylib_library_name = *dll_name;
                        } else {
                          extern_proc.raw_dylib_library_name =
                              std::string(library_spec->name);
                        }
                        extern_proc.raw_dylib_foreign_symbol =
                            LinkName(proc.attrs, proc.name).value_or(proc.name);
                        extern_proc.raw_dylib_catch_unwind =
                            unwind_mode == LowerCtx::FfiImportUnwindMode::Catch;
                      }

                      // Register signature for call resolution
                      ProcIR sig;
                      sig.symbol = extern_proc.symbol;
                      sig.params = extern_proc.params;
                      sig.ret = extern_proc.ret;
                      sig.abi = extern_proc.abi;
                      ctx.RegisterProcSig(sig);
                      ctx.RegisterProcLinkage(extern_proc.symbol, LinkageOf(proc));
                      ctx.RegisterProcFfiImport(extern_proc.symbol,
                                                unwind_mode);

                      LowerCtx::ForeignContractInfo foreign_info;
                      foreign_info.dynamic =
                          ForeignDynamicChecksEnabled(proc.attrs);
                      if (proc.foreign_contracts_opt.has_value()) {
                        for (const auto& clause : *proc.foreign_contracts_opt) {
                          switch (clause.kind) {
                            case ast::ForeignContractKind::Assumes:
                              foreign_info.assumes.insert(foreign_info.assumes.end(),
                                                          clause.predicates.begin(),
                                                          clause.predicates.end());
                              break;
                            case ast::ForeignContractKind::Ensures:
                              foreign_info.ensures.insert(foreign_info.ensures.end(),
                                                          clause.predicates.begin(),
                                                          clause.predicates.end());
                              break;
                            case ast::ForeignContractKind::EnsuresError:
                              foreign_info.ensures_error.insert(
                                  foreign_info.ensures_error.end(),
                                  clause.predicates.begin(),
                                  clause.predicates.end());
                              break;
                            case ast::ForeignContractKind::EnsuresNullResult:
                              foreign_info.ensures_null_result.insert(
                                  foreign_info.ensures_null_result.end(),
                                  clause.predicates.begin(),
                                  clause.predicates.end());
                              break;
                          }
                        }
                      }
                      if (!foreign_info.assumes.empty() || !foreign_info.ensures.empty() ||
                          !foreign_info.ensures_error.empty() ||
                          !foreign_info.ensures_null_result.empty()) {
                        ctx.RegisterForeignContractInfo(extern_proc.symbol,
                                                        std::move(foreign_info));
                      }

                      decls.push_back(std::move(extern_proc));
                    }
                  },
                  ext_item);
            }
            return;
          } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
            SPEC_RULE("CG-Item-Import");
            // Imports are handled during module loading
            return;
          } else if constexpr (std::is_same_v<T, ast::ErrorItem>) {
            SPEC_RULE("CG-Item-ErrorItem");
            return;
          }
        },
        item);
    if (debug_lower_module) {
      std::cerr << "[lower-module-debug] module=" << module_key
                << " item_index=" << item_index
                << " stage=item-visit-done\n";
    }
    drain_extra_procs();
    if (debug_lower_module) {
      std::cerr << "[lower-module-debug] module=" << module_key
                << " item_index=" << item_index
                << " stage=item-drain-done\n";
    }
    flush_item_maps();
    if (debug_lower_module) {
      std::cerr << "[lower-module-debug] module=" << module_key
                << " item_index=" << item_index
                << " stage=item-flush-done\n";
    }
  }

  auto init_fn = EmitModuleInitFn(module.path, module, ctx);
  register_proc(init_fn, false, LinkageKind::Internal);
  decls.push_back(init_fn);
  drain_extra_procs();
  flush_item_maps();

  auto deinit_fn = EmitModuleDeinitFn(module.path, module, ctx);
  register_proc(deinit_fn, false, LinkageKind::Internal);
  decls.push_back(deinit_fn);
  drain_extra_procs();
  flush_item_maps();

  std::unordered_set<std::string> emitted_drop_glue_syms;
  for (;;) {
    if (!ctx.values.drop_glue_types.empty()) {
      for (auto& [name, type] : ctx.values.drop_glue_types) {
        module_drop_glue_types[name] = type;
      }
      ctx.values.drop_glue_types.clear();
    }

    std::vector<std::string> pending_drop_syms;
    pending_drop_syms.reserve(module_drop_glue_types.size());
    for (const auto& [sym, _type] : module_drop_glue_types) {
      if (emitted_drop_glue_syms.count(sym) == 0) {
        pending_drop_syms.push_back(sym);
      }
    }
    if (pending_drop_syms.empty()) {
      break;
    }

    std::sort(pending_drop_syms.begin(), pending_drop_syms.end());
    for (const auto& sym : pending_drop_syms) {
      const auto type_it = module_drop_glue_types.find(sym);
      if (type_it == module_drop_glue_types.end()) {
        continue;
      }
      emitted_drop_glue_syms.insert(sym);
      ProcIR glue = EmitDropGlue(type_it->second, ctx);
      flush_item_maps();
      register_proc(glue, false, LinkageKind::Internal);
      decls.push_back(std::move(glue));
    }
  }

  ctx.values.value_types = std::move(module_value_types);
  ctx.values.derived_values = std::move(module_derived_values);
  ctx.values.drop_glue_types = std::move(module_drop_glue_types);

  return decls;
}

IRDecls ExpandIR(const IRDecls& decls, LowerCtx& ctx) {
  SPEC_RULE("ExpandIR");

  IRDecls expanded_decls = decls;
  std::unordered_set<std::string> seen_symbols;
  seen_symbols.reserve(expanded_decls.size());
  for (const auto& decl : expanded_decls) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ProcIR> ||
                        std::is_same_v<T, GlobalConst> ||
                        std::is_same_v<T, GlobalZero> ||
                        std::is_same_v<T, GlobalVTable> ||
                        std::is_same_v<T, ExternProcIR>) {
            seen_symbols.insert(node.symbol);
          }
        },
        decl);
  }

  for (auto& lit_decl : UniqueLiterals(LiteralRefs(decls))) {
    const auto* global = std::get_if<GlobalConst>(&lit_decl);
    if (!global) {
      continue;
    }
    if (seen_symbols.insert(global->symbol).second) {
      expanded_decls.push_back(std::move(lit_decl));
    }
  }

  if (ctx.sigma != nullptr) {
    for (const auto& symbol : CollectVTableRefs(expanded_decls, ctx)) {
      const auto* info = ctx.LookupRequiredVTable(symbol);
      if (!info || info->class_path.empty()) {
        continue;
      }

      ast::ModulePath owner_module(info->class_path.begin(), info->class_path.end());
      owner_module.pop_back();
      if (owner_module != ctx.module_path) {
        continue;
      }

      ast::Path class_ast_path(info->class_path.begin(), info->class_path.end());
      const auto class_it = ctx.sigma->classes.find(analysis::PathKeyOf(class_ast_path));
      if (class_it == ctx.sigma->classes.end()) {
        ctx.ReportCodegenFailure();
        continue;
      }

      ProcIR glue = ::cursive::codegen::EmitDropGlue(info->type, ctx);
      if (seen_symbols.insert(glue.symbol).second) {
        expanded_decls.push_back(std::move(glue));
      }

      GlobalVTable vtable =
          ::cursive::codegen::EmitVTable(info->type, info->class_path, class_it->second, ctx);
      if (seen_symbols.insert(vtable.symbol).second) {
        expanded_decls.push_back(std::move(vtable));
      }
    }
  }

  return expanded_decls;
}

}  // namespace cursive::codegen
