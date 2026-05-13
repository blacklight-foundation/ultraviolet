// =============================================================================
// MIGRATION: typecheck.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2: Static Semantics - Type Checking
//   Section 5.3: Declarations
//   - Module-level typechecking entry point
//   - Item typechecking dispatch
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/typecheck.cpp
//
// =============================================================================

#include "04_analysis/typing/typecheck.h"

#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "typecheck_diag_lookup.h"
#include "00_core/diagnostics.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"
#include "01_project/assemblies.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/conformance/conformance.h"
#include "04_analysis/generics/generic_params.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/memory/borrow_bind.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/memory/init_planner.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/expr/call.h"
#include "04_analysis/typing/expr/path.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_decls.h"

namespace cursive::analysis {

namespace {

void LogSemaProgress(const std::string& message) {
  if (!core::IsDebugEnabled("pipeline") && !core::IsDebugEnabled("sema")) {
    return;
  }
  std::cerr << "[cursive] sema " << message << "\n";
}

std::string NormalizeAttrLiteral(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

std::optional<std::string> ExplicitLinkNameOfProcedure(
    const ast::ProcedureDecl& decl) {
  if (!HasAttribute(decl.attrs, attrs::kMangle)) {
    return std::nullopt;
  }
  auto mode_value = GetAttributeValue(decl.attrs, attrs::kMangle, "mode");
  if (!mode_value.has_value()) {
    mode_value = GetAttributeValue(decl.attrs, attrs::kMangle);
  }
  if (!mode_value.has_value()) {
    return std::nullopt;
  }
  const std::string mode = NormalizeAttrLiteral(*mode_value);
  if (mode == "none") {
    return decl.name;
  }
  return mode;
}

ast::ProcedureDecl AsProcedureDecl(const ast::ComptimeProcedureDecl& decl) {
  ast::ProcedureDecl proc;
  proc.attrs = decl.attrs;
  proc.vis = decl.vis;
  proc.name = decl.name;
  proc.generic_params = decl.generic_params;
  proc.params = decl.params;
  proc.return_type_opt = decl.return_type_opt;
  proc.predicate_clause_opt = std::nullopt;
  proc.contract = decl.contract;
  proc.body = decl.body;
  proc.span = decl.span;
  proc.doc = decl.doc;
  return proc;
}

ProcedureDeclResult TypeDeriveTargetDeclBody(const ScopeContext& ctx,
                                             const ast::DeriveTargetDecl& decl,
                                             core::DiagnosticStream& diags) {
  ProcedureDeclResult result;
  result.ok = true;

  if (!decl.body) {
    return result;
  }

  TypeEnv env;
  env.scopes.emplace_back();
  env.scopes.back()[IdKeyOf("target")] =
      TypeBinding{ast::Mutability::Let, MakeTypePath({"Type"})};
  env.scopes.back()[IdKeyOf("emitter")] =
      TypeBinding{ast::Mutability::Let, MakeTypePath({"TypeEmitter"})};
  env.scopes.back()[IdKeyOf("introspect")] =
      TypeBinding{ast::Mutability::Let, MakeTypePath({"Introspect"})};
  env.scopes.back()[IdKeyOf("diagnostics")] =
      TypeBinding{ast::Mutability::Let,
                  MakeTypePath({"ComptimeDiagnostics"})};

  StmtTypeContext type_ctx;
  type_ctx.diags = &diags;
  type_ctx.env_ref = &env;

  ExprTypeFn type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  IdentTypeFn type_ident = [&](std::string_view name) -> ExprTypeResult {
    return expr::TypeIdentifierExprImpl(ctx, ast::IdentifierExpr{std::string(name)},
                                        env);
  };
  PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };

  const auto body_result =
      TypeBlock(ctx, type_ctx, *decl.body, env, type_expr, type_ident,
                type_place, &env);
  if (!body_result.ok) {
    result.ok = false;
    result.diag_id = body_result.diag_id.has_value()
                         ? body_result.diag_id
                         : std::optional<std::string_view>{"E-TYP-1530"};
    result.diag_span = body_result.diag_span;
    result.diag_detail =
        body_result.diag_detail.empty()
            ? "procedure body typing failed without statement-level diagnostic"
            : body_result.diag_detail;
  }
  return result;
}

std::optional<ProcedureDeclResult> ValidateComptimeProcedureSignatureTypes(
    const ScopeContext& ctx,
    const ast::ComptimeProcedureDecl& decl) {
  ProcedureDeclResult result;
  result.ok = true;

  const auto sig = BuildProcedureSignature(ctx, decl.params, decl.return_type_opt);
  if (!sig.ok) {
    result.ok = false;
    result.diag_id = sig.diag_id;
    return result;
  }

  if (const auto* fn = std::get_if<TypeFunc>(&sig.func_type->node)) {
    const std::size_t limit = std::min(fn->params.size(), decl.params.size());
    for (std::size_t i = 0; i < limit; ++i) {
      if (const auto diag =
              ComptimeTypeAvailabilityDiag(ctx, fn->params[i].type,
                                           "E-CTE-0030")) {
        result.ok = false;
        result.diag_id = *diag;
        result.diag_span =
            decl.params[i].type
                ? std::optional<core::Span>(decl.params[i].type->span)
                : std::optional<core::Span>(decl.params[i].span);
        return result;
      }
    }
  }

  if (const auto diag =
          ComptimeTypeAvailabilityDiag(ctx, sig.return_type, "E-CTE-0031")) {
    result.ok = false;
    result.diag_id = *diag;
    result.diag_span =
        decl.return_type_opt
            ? std::optional<core::Span>(decl.return_type_opt->span)
            : std::optional<core::Span>(decl.span);
    return result;
  }

  return std::nullopt;
}

std::optional<std::string> ExplicitLinkNameOfExternProcedure(
    const ast::ExternProcDecl& decl) {
  if (!HasAttribute(decl.attrs, attrs::kMangle)) {
    return std::nullopt;
  }
  auto mode_value = GetAttributeValue(decl.attrs, attrs::kMangle, "mode");
  if (!mode_value.has_value()) {
    mode_value = GetAttributeValue(decl.attrs, attrs::kMangle);
  }
  if (!mode_value.has_value()) {
    return std::nullopt;
  }
  const std::string mode = NormalizeAttrLiteral(*mode_value);
  if (mode == "none") {
    return decl.name;
  }
  return mode;
}

std::size_t CountErrorDiagnostics(const core::DiagnosticStream& diags) {
  return ::cursive::analysis::CountErrorLikeDiagnostics(diags);
}

}  // namespace

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsDeclTyping() {
  SPEC_DEF("DeclJudg", "5.2.14");
  SPEC_DEF("DeclTyping", "5.2.14");
  SPEC_DEF("DeclTypingMod", "5.2.14");
  SPEC_DEF("DeclTypingItem", "5.2.14");
  SPEC_DEF("MainDecls", "5.2.14");
  SPEC_DEF("MainGeneric", "5.2.14");
  SPEC_DEF("MainSigOk", "5.2.14");
  SPEC_DEF("DuplicateErasedOverloadSignaturesForbidden", "15.3.4");
}

// =============================================================================
// DIAGNOSTIC HELPERS
// =============================================================================

static void EmitTypecheckDiag(core::DiagnosticStream& diags,
                              std::string_view diag_id,
                              const std::optional<core::Span>& span,
                              const std::string& detail = {}) {
  EmitResolvedTypecheckDiagnostic(diags, diag_id, span, detail);
}

static bool IsContextTypeSyntax(const ast::Type& type) {
  if (const auto* path_type = std::get_if<ast::TypePathType>(&type.node)) {
    return path_type->generic_args.empty() && IsContextTypePath(path_type->path);
  }
  if (const auto* prim_type = std::get_if<ast::TypePrim>(&type.node)) {
    return IdEq(prim_type->name, "Context");
  }
  return false;
}

static bool IsI32TypeSyntax(const ast::Type& type) {
  const auto* prim_type = std::get_if<ast::TypePrim>(&type.node);
  return prim_type != nullptr && IdEq(prim_type->name, "i32");
}

static std::vector<core::SubDiagnostic> MainSignatureFixIts(
    const ast::ProcedureDecl& decl) {
  if (!IdEq(decl.name, "main") || !decl.attrs.empty() ||
      !ast::TypeParamsOpt(decl.generic_params).empty() ||
      decl.params.size() != 1 || !decl.params[0].type ||
      !decl.return_type_opt) {
    return {};
  }

  const ast::Param& param = decl.params[0];
  if (!IsContextTypeSyntax(*param.type) ||
      !IsI32TypeSyntax(*decl.return_type_opt)) {
    return {};
  }

  core::Span replacement_span = decl.span;
  replacement_span.end_offset = param.type->span.end_offset;
  replacement_span.end_line = param.type->span.end_line;
  replacement_span.end_col = param.type->span.end_col;
  if (replacement_span.end_offset <= replacement_span.start_offset) {
    return {};
  }

  core::SubDiagnostic fix;
  fix.kind = core::SubDiagnosticKind::FixIt;
  fix.message = "Fix main signature";
  fix.span = replacement_span;
  fix.fix_text =
      "public procedure main(move " + param.name + ": Context)";
  return {std::move(fix)};
}

static void EmitDeclDiag(core::DiagnosticStream& diags,
                         const std::optional<std::string_view>& diag_id,
                         const std::optional<core::Span>& span,
                         const std::string& detail = {},
                         std::vector<core::SubDiagnostic> children = {}) {
  if (!diag_id.has_value()) {
    return;
  }
  const auto emit_from_id = [&](std::string_view id,
                                const std::string& d) -> bool {
    auto diag = BuildResolvedTypecheckDiagnostic(id, span);
    if (!diag.has_value()) {
      return false;
    }
    if (!d.empty()) {
      core::SubDiagnostic note;
      note.kind = core::SubDiagnosticKind::Note;
      note.message = d;
      diag->children.push_back(std::move(note));
    }
    diag->children.insert(diag->children.end(),
                          std::make_move_iterator(children.begin()),
                          std::make_move_iterator(children.end()));
    core::Emit(diags, *diag);
    return true;
  };

  if (!emit_from_id(*diag_id, detail)) {
    core::Emit(diags, MakeInternalTypecheckDiagnostic(
                          core::Severity::Error, span,
                          "Internal error: unknown typecheck diagnostic id '" +
                              std::string(*diag_id) + "'"));
    return;
  }

  if (*diag_id == "E-SEM-2534") {
    emit_from_id("E-MOD-2411", {});
  }
}

static void EmitDuplicateSymbolDiags(
    const std::vector<ast::ASTModule>& modules,
    core::DiagnosticStream& diags) {
  std::unordered_map<std::string, core::Span> seen_symbols;
  for (const auto& module : modules) {
    for (const auto& item : module.items) {
      const ast::ProcedureDecl* proc = std::get_if<ast::ProcedureDecl>(&item);
      std::optional<ast::ProcedureDecl> proc_view;
      if (!proc) {
        if (const auto* comptime_proc = std::get_if<ast::ComptimeProcedureDecl>(&item)) {
          proc_view = AsProcedureDecl(*comptime_proc);
          proc = &*proc_view;
        }
      }
      if (!proc) {
        continue;
      }
      const auto link_name = ExplicitLinkNameOfProcedure(*proc);
      if (!link_name.has_value() || link_name->empty()) {
        continue;
      }
      const auto [it, inserted] =
          seen_symbols.emplace(*link_name, proc->span);
      if (!inserted) {
        EmitTypecheckDiag(diags, "E-SYS-3342", std::optional<core::Span>(proc->span));
      }
    }

    for (const auto& item : module.items) {
      const auto* block = std::get_if<ast::ExternBlock>(&item);
      if (!block) {
        continue;
      }
      for (const auto& extern_item : block->items) {
        const auto* proc = std::get_if<ast::ExternProcDecl>(&extern_item);
        if (!proc) {
          continue;
        }
        const auto link_name = ExplicitLinkNameOfExternProcedure(*proc);
        if (!link_name.has_value() || link_name->empty()) {
          continue;
        }
        const auto [it, inserted] =
            seen_symbols.emplace(*link_name, proc->span);
        if (!inserted) {
          EmitTypecheckDiag(diags, "E-SYS-3342",
                            std::optional<core::Span>(proc->span));
        }
      }
    }
  }
}

struct ErasedOverloadParam {
  std::optional<ParamMode> mode;
  TypeKey type_key;
};

struct ErasedOverloadSignature {
  std::string name;
  std::vector<ErasedOverloadParam> params;
};

static bool ParamModeEqual(const std::optional<ParamMode>& lhs,
                           const std::optional<ParamMode>& rhs) {
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  if (!lhs.has_value()) {
    return true;
  }
  return *lhs == *rhs;
}

static bool ErasedOverloadSignatureEqual(
    const ErasedOverloadSignature& lhs,
    const ErasedOverloadSignature& rhs) {
  if (!IdEq(lhs.name, rhs.name) || lhs.params.size() != rhs.params.size()) {
    return false;
  }

  for (std::size_t i = 0; i < lhs.params.size(); ++i) {
    if (!ParamModeEqual(lhs.params[i].mode, rhs.params[i].mode) ||
        !TypeKeyEqual(lhs.params[i].type_key, rhs.params[i].type_key)) {
      return false;
    }
  }
  return true;
}

static TypeSubst BuildGenericErasureSubstitution(
    const std::optional<ast::GenericParams>& generic_params) {
  TypeSubst subst;
  const TypeRef erased_param = MakeTypeVar(0);
  for (const auto& param : ast::TypeParamsOpt(generic_params)) {
    subst[param.name] = erased_param;
  }
  return subst;
}

static std::optional<ErasedOverloadSignature> BuildErasedOverloadSignature(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& proc) {
  ScopeContext proc_ctx = ctx;
  proc_ctx.scopes = BindTypeParams(ctx, proc.generic_params);
  const auto sig =
      BuildProcedureSignature(proc_ctx, proc.params, proc.return_type_opt);
  if (!sig.ok || !sig.func_type) {
    return std::nullopt;
  }

  const auto* func = std::get_if<TypeFunc>(&sig.func_type->node);
  if (!func) {
    return std::nullopt;
  }

  const TypeSubst erasure = BuildGenericErasureSubstitution(proc.generic_params);
  ErasedOverloadSignature signature;
  signature.name = proc.name;
  signature.params.reserve(func->params.size());
  for (const auto& param : func->params) {
    const TypeRef erased_type = InstantiateType(param.type, erasure);
    signature.params.push_back(
        ErasedOverloadParam{param.mode, TypeKeyOf(erased_type)});
  }
  return signature;
}

static void EmitDuplicateErasedOverloadSignatureDiags(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    core::DiagnosticStream& diags) {
  SpecDefsDeclTyping();
  std::vector<ErasedOverloadSignature> seen_signatures;
  for (const auto& item : module.items) {
    const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
    if (proc == nullptr || IdEq(proc->name, "main")) {
      continue;
    }
    const auto signature = BuildErasedOverloadSignature(ctx, *proc);
    if (!signature.has_value()) {
      continue;
    }

    for (const auto& seen : seen_signatures) {
      if (ErasedOverloadSignatureEqual(*signature, seen)) {
        SPEC_RULE("DuplicateErasedOverloadSignaturesForbidden");
        EmitTypecheckDiag(diags, "E-SEM-3032",
                          std::optional<core::Span>(proc->span));
        break;
      }
    }
    seen_signatures.push_back(*signature);
  }
}

// =============================================================================
// MAIN SIGNATURE CHECKING
// =============================================================================

// Check if main procedure has generic parameters
static bool MainGeneric(const ast::ProcedureDecl& decl) {
  SPEC_RULE("MainGeneric");
  return !ast::TypeParamsOpt(decl.generic_params).empty();
}

// Check if main procedure has valid signature: (move ctx: Context) -> i32
static bool MainSigOk(const ast::ProcedureDecl& decl) {
  SPEC_RULE("MainSigOk");

  // Must have exactly one parameter
  if (decl.params.size() != 1) {
    return false;
  }

  // Parameter must be "move ctx: Context" (or just Context type)
  const auto& param = decl.params[0];
  if (!param.type) {
    return false;
  }

  // Check parameter type is "Context" - it could be TypePathType or TypePrim
  bool param_ok = false;
  if (const auto* path_type = std::get_if<ast::TypePathType>(&param.type->node)) {
    if (IsContextTypePath(path_type->path)) {
      param_ok = true;
    }
  } else if (const auto* prim_type = std::get_if<ast::TypePrim>(&param.type->node)) {
    if (IdEq(prim_type->name, "Context")) {
      param_ok = true;
    }
  }
  if (!param_ok) {
    return false;
  }

  // Return type must be i32
  if (!decl.return_type_opt) {
    return false;
  }
  const auto* return_prim = std::get_if<ast::TypePrim>(&decl.return_type_opt->node);
  if (!return_prim) {
    return false;
  }
  if (!IdEq(return_prim->name, "i32")) {
    return false;
  }

  return true;
}

// =============================================================================
// DeclTypingModules - Type check all declarations in modules
// =============================================================================

DeclTypingResult DeclTypingModules(ScopeContext& ctx,
                                   const std::vector<ast::ASTModule>& modules,
                                   const NameMapTable& name_maps) {
  SpecDefsDeclTyping();
  DeclTypingResult result;
  const bool sema_debug_logging =
      core::IsDebugEnabled("pipeline") || core::IsDebugEnabled("sema");
  const auto error_policy = core::MaxErrorsOverride().value_or(
      core::DefaultErrorRecoveryPolicy());
  const std::size_t module_total = modules.size();
  std::size_t module_index = 0;
  if (sema_debug_logging) {
    LogSemaProgress("typecheck decls-start modules=" +
                    std::to_string(module_total));
  }
  EmitDuplicateSymbolDiags(modules, result.diags);
  if (core::AbortOnErrorCount(error_policy,
                              CountErrorDiagnostics(result.diags))) {
    result.ok = false;
    return result;
  }
  const Scope universe_scope = UniverseBindings();

  for (const auto& module : modules) {
    ++module_index;
    std::string module_name = core::StringOfPath(module.path);
    if (module_name.empty()) {
      module_name = "<root>";
    }
    std::size_t module_diag_start = 0;
    if (sema_debug_logging) {
      module_diag_start = result.diags.size();
    }
    if (sema_debug_logging) {
      LogSemaProgress("typecheck module-start index=" +
                      std::to_string(module_index) + "/" +
                      std::to_string(module_total) + " module=" + module_name +
                      " items=" + std::to_string(module.items.size()));
    }
    ctx.current_module = module.path;
    Scope module_scope;
    const auto map_it = name_maps.find(PathKeyOf(module.path));
    if (map_it != name_maps.end()) {
      module_scope = map_it->second;
    }
    ctx.scopes = {Scope{}, std::move(module_scope), universe_scope};

    EmitDuplicateErasedOverloadSignatureDiags(ctx, module, result.diags);
    if (core::AbortOnErrorCount(error_policy,
                                CountErrorDiagnostics(result.diags))) {
      result.ok = false;
      return result;
    }

    // Type check each item in the module
    std::size_t item_index = 0;
    for (const auto& item : module.items) {
      ++item_index;
      std::string item_kind;
      std::size_t item_diag_start = 0;
      if (sema_debug_logging) {
        item_kind = std::visit(
            [](const auto& node) -> std::string {
              using T = std::decay_t<decltype(node)>;
              if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
                return "procedure";
              } else if constexpr (std::is_same_v<T, ast::ComptimeProcedureDecl>) {
                return "comptime procedure";
              } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
                return "derive target";
              } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
                return "record";
              } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
                return "enum";
              } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
                return "modal";
              } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
                return "class";
              } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
                return "type";
              } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
                return "static";
              } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
                return "import";
              } else if constexpr (std::is_same_v<T, ast::UsingDecl>) {
                return "using";
              } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
                return "extern";
              } else {
                return "unknown";
              }
            },
            item);
        item_diag_start = result.diags.size();
      }
      if (sema_debug_logging) {
        LogSemaProgress("typecheck item-start module=" + module_name +
                        " index=" + std::to_string(item_index) + "/" +
                        std::to_string(module.items.size()) + " kind=" +
                        item_kind);
      }
      std::visit(
          [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
              const auto res = ::cursive::analysis::TypeProcedureDecl(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                const auto diag_span =
                    res.diag_span.has_value()
                        ? res.diag_span
                        : std::optional<core::Span>(node.span);
                std::vector<core::SubDiagnostic> children;
                if (res.diag_id.has_value() && *res.diag_id == "E-MOD-2431") {
                  children = MainSignatureFixIts(node);
                }
                EmitDeclDiag(result.diags, res.diag_id,
                             diag_span,
                             res.diag_detail,
                             std::move(children));
              }
            } else if constexpr (std::is_same_v<T, ast::ComptimeProcedureDecl>) {
              if (const auto precheck =
                      ValidateComptimeProcedureSignatureTypes(ctx, node)) {
                const auto diag_span =
                    precheck->diag_span.has_value()
                        ? precheck->diag_span
                        : std::optional<core::Span>(node.span);
                EmitDeclDiag(result.diags, precheck->diag_id, diag_span,
                             precheck->diag_detail);
                return;
              }
              const auto proc = AsProcedureDecl(node);
              const auto res = ::cursive::analysis::TypeProcedureDecl(
                  ctx, proc, module.path, result.diags);
              if (!res.ok) {
                const auto diag_span =
                    res.diag_span.has_value()
                        ? res.diag_span
                        : std::optional<core::Span>(node.span);
                EmitDeclDiag(result.diags, res.diag_id,
                             diag_span,
                             res.diag_detail);
              } else {
                SPEC_RULE_AT("T-CtProc", node.span);
              }
            } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
              const auto res =
                  TypeDeriveTargetDeclBody(ctx, node, result.diags);
              if (!res.ok) {
                const auto diag_span =
                    res.diag_span.has_value()
                        ? res.diag_span
                        : std::optional<core::Span>(node.span);
                EmitDeclDiag(result.diags, res.diag_id, diag_span,
                             res.diag_detail);
              }
            } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
              const auto res = ::cursive::analysis::TypeRecordDecl(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span),
                             res.diag_detail);
              }
            } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
              const auto res = ::cursive::analysis::TypeEnumDecl(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span));
              }
            } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
              const auto res = ::cursive::analysis::TypeModalDecl(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span),
                             res.diag_detail);
              }
            } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
              const auto res = ::cursive::analysis::TypeClassDecl(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span),
                             res.diag_detail);
              }
            } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
              const auto res = ::cursive::analysis::TypeTypeAliasDecl(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span));
              }
            } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
              const auto res = ::cursive::analysis::TypeStaticDecl(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span));
              }
            } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
              const auto res = ::cursive::analysis::TypeImportDecl(
                  ctx, node, module.path);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span));
              }
            } else if constexpr (std::is_same_v<T, ast::UsingDecl>) {
              const auto res = ::cursive::analysis::TypeUsingDecl(
                  ctx, node, module.path);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span));
              }
            } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
              const auto res = ::cursive::analysis::TypeExternBlock(
                  ctx, node, module.path, result.diags);
              if (!res.ok) {
                EmitDeclDiag(result.diags, res.diag_id,
                             std::optional<core::Span>(node.span));
              }
            }
          },
          item);
      if (sema_debug_logging) {
        const std::size_t item_diag_count = result.diags.size() - item_diag_start;
        LogSemaProgress("typecheck item-finish module=" + module_name +
                        " index=" + std::to_string(item_index) + "/" +
                        std::to_string(module.items.size()) + " kind=" +
                        item_kind + " emitted_diags=" +
                        std::to_string(item_diag_count));
      }
      if (core::AbortOnErrorCount(error_policy,
                                  CountErrorDiagnostics(result.diags))) {
        if (sema_debug_logging) {
          LogSemaProgress("typecheck abort-on-error-count module=" + module_name +
                          " index=" + std::to_string(item_index));
        }
        result.ok = false;
        return result;
      }
    }
    if (sema_debug_logging) {
      const std::size_t module_diag_count = result.diags.size() - module_diag_start;
      LogSemaProgress("typecheck module-finish index=" +
                      std::to_string(module_index) + "/" +
                      std::to_string(module_total) + " module=" + module_name +
                      " emitted_diags=" + std::to_string(module_diag_count));
    }
  }

  (void)CheckTypeSystemMetatheoryHooks(ctx);

  result.ok = !core::HasError(result.diags);
  if (sema_debug_logging) {
    LogSemaProgress("typecheck decls-finish ok=" +
                    std::string(result.ok ? "true" : "false") +
                    " emitted_diags=" + std::to_string(result.diags.size()));
  }
  return result;
}

// =============================================================================
// MainCheckProject - Verify exactly one valid main procedure exists
// =============================================================================

DeclTypingResult MainCheckProject(ScopeContext& ctx,
                                  const std::vector<ast::ASTModule>& modules) {
  (void)ctx;
  SpecDefsDeclTyping();
  DeclTypingResult result;

  // Find all main procedures
  std::vector<const ast::ProcedureDecl*> mains;
  for (const auto& module : modules) {
    for (const auto& item : module.items) {
      if (const auto* proc = std::get_if<ast::ProcedureDecl>(&item)) {
        if (IdEq(proc->name, "main")) {
          mains.push_back(proc);
        }
      }
    }
  }

  if (mains.empty()) {
    SPEC_RULE("Main-Missing");
    EmitTypecheckDiag(result.diags, "E-MOD-2434", std::nullopt);
    result.ok = false;
    return result;
  }

  if (mains.size() > 1) {
    SPEC_RULE("Main-Multiple");
    EmitTypecheckDiag(result.diags, "E-MOD-2430", mains.front()->span);
    result.ok = false;
    return result;
  }

  const auto* main_decl = mains.front();

  if (MainGeneric(*main_decl)) {
    SPEC_RULE("Main-Generic-Err");
    EmitTypecheckDiag(result.diags, "E-MOD-2432", main_decl->span);
    result.ok = false;
    return result;
  }

  if (!MainSigOk(*main_decl)) {
    SPEC_RULE("Main-Signature-Err");
    auto diag =
        BuildResolvedTypecheckDiagnostic("E-MOD-2431", main_decl->span);
    if (diag.has_value()) {
      auto fixes = MainSignatureFixIts(*main_decl);
      diag->children.insert(diag->children.end(),
                            std::make_move_iterator(fixes.begin()),
                            std::make_move_iterator(fixes.end()));
      core::Emit(result.diags, *diag);
    } else {
      EmitTypecheckDiag(result.diags, "E-MOD-2431", main_decl->span);
    }
    result.ok = false;
    return result;
  }

  SPEC_RULE("Main-Ok");
  result.ok = true;
  return result;
}

TypecheckResult TypecheckModules(
    ScopeContext& ctx,
    const std::vector<ast::ASTModule>& modules,
    const NameMapTable* precomputed_name_maps) {
  NameMapBuildResult collected_name_maps;
  const NameMapTable* name_maps = precomputed_name_maps;
  TypecheckResult result;
  struct PerfSummaryScope {
    ~PerfSummaryScope() {
      LogProcedureTypePerfSummary();
      expr::LogCallLookupPerfSummary();
      LogClassLookupPerfSummary();
      LogBorrowBindPerfSummary();
      LogProvenancePerfSummary();
    }
  } perf_summary_scope;

  if (!name_maps) {
    collected_name_maps = CollectNameMaps(ctx);
    if (!collected_name_maps.diags.empty()) {
      result.diags.insert(result.diags.end(),
                          collected_name_maps.diags.begin(),
                          collected_name_maps.diags.end());
    }
    if (core::HasError(result.diags)) {
      result.ok = false;
      return result;
    }
    name_maps = &collected_name_maps.name_maps;
  }

  ExprTypeMap* prev_expr_types = ctx.expr_types;
  DynamicRefineExprMap* prev_dynamic_refine_checks = ctx.dynamic_refine_checks;
  GenericCallSubstMap* prev_generic_call_substs = ctx.generic_call_substs;
  ctx.expr_types = &result.expr_types;
  ctx.dynamic_refine_checks = &result.dynamic_refine_checks;
  ctx.generic_call_substs = &result.generic_call_substs;
  struct ExprTypesReset {
    ScopeContext& ctx;
    ExprTypeMap* prev;
    DynamicRefineExprMap* prev_dynamic_refine_checks;
    GenericCallSubstMap* prev_generic_call_substs;
    ~ExprTypesReset() {
      ctx.expr_types = prev;
      ctx.dynamic_refine_checks = prev_dynamic_refine_checks;
      ctx.generic_call_substs = prev_generic_call_substs;
    }
  } expr_types_reset{ctx,
                     prev_expr_types,
                     prev_dynamic_refine_checks,
                     prev_generic_call_substs};

  const auto decls = DeclTypingModules(ctx, modules, *name_maps);
  if (!decls.diags.empty()) {
    result.diags.insert(result.diags.end(),
                        decls.diags.begin(),
                        decls.diags.end());
  }

  if (!core::HasError(result.diags)) {
    const auto init_plan = BuildInitPlan(ctx, *name_maps);
    if (!init_plan.diags.empty()) {
      result.diags.insert(result.diags.end(),
                          init_plan.diags.begin(),
                          init_plan.diags.end());
    }
    if (init_plan.ok) {
      result.init_plan = init_plan.plan;
    }
  }

  if (!core::HasError(result.diags)) {
    const bool require_main =
        !ctx.project || project::IsExecutable(*ctx.project);
    if (require_main) {
      const auto main_check = MainCheckProject(ctx, modules);
      if (!main_check.diags.empty()) {
        result.diags.insert(result.diags.end(),
                            main_check.diags.begin(),
                            main_check.diags.end());
      }
    } else {
      SPEC_RULE("Main-Bypass-Lib");
    }
  }

  result.ok = !core::HasError(result.diags);
  return result;
}

core::DiagnosticStream ValidateComptimeProcedureSignatures(
    ScopeContext& ctx,
    const std::vector<ast::ASTModule>& modules) {
  core::DiagnosticStream diags;
  NameMapBuildResult collected_name_maps = CollectNameMaps(ctx);
  for (const auto& diag : collected_name_maps.diags) {
    core::Emit(diags, diag);
  }
  if (core::HasError(diags)) {
    return diags;
  }

  const Scope universe_scope = UniverseBindings();
  for (const auto& module : modules) {
    ctx.current_module = module.path;
    Scope module_scope;
    const auto map_it =
        collected_name_maps.name_maps.find(PathKeyOf(module.path));
    if (map_it != collected_name_maps.name_maps.end()) {
      module_scope = map_it->second;
    }
    ctx.scopes = {Scope{}, std::move(module_scope), universe_scope};

    for (const auto& item : module.items) {
      const auto* node = std::get_if<ast::ComptimeProcedureDecl>(&item);
      if (node == nullptr) {
        continue;
      }
      if (const auto precheck =
              ValidateComptimeProcedureSignatureTypes(ctx, *node)) {
        const auto diag_span =
            precheck->diag_span.has_value()
                ? precheck->diag_span
                : std::optional<core::Span>(node->span);
        EmitDeclDiag(diags, precheck->diag_id, diag_span,
                     precheck->diag_detail);
      }
    }
  }

  return diags;
}

}  // namespace cursive::analysis
