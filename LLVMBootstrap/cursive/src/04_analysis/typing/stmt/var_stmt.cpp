// =============================================================================
// var_stmt.cpp - Var statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.11: Statement Typing (lines 9389-9407)
//   - T-VarStmt-Ann (lines 9389-9392): Var with type annotation
//   - T-VarStmt-Ann-Mismatch (lines 9394-9397): Type mismatch error
//   - T-VarStmt-Infer (lines 9399-9402): Var with inference
//   - T-VarStmt-Infer-Err (lines 9404-9407): Inference failure
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "02_source/ast/ast.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/types.h"

#include <cstdio>

namespace cursive::analysis {

// TypePattern, IntroResult, IntroAll, CollectPatNames, and DistinctNames
// are declared in type_stmt.h

namespace {

static inline void SpecDefsVarStmt() {
  SPEC_DEF("T-VarStmt-Ann", "5.2.11");
  SPEC_DEF("T-VarStmt-Ann-Mismatch", "5.2.11");
  SPEC_DEF("T-VarStmt-Infer", "5.2.11");
  SPEC_DEF("T-VarStmt-Infer-Err", "5.2.11");
  SPEC_DEF("Pat-Dup-Err", "5.2.11");
}

std::string PatternName(const ast::PatternPtr& pat) {
  if (!pat) return {};
  if (const auto* ident = std::get_if<ast::IdentifierPattern>(&pat->node)) {
    return ident->name;
  }
  if (const auto* typed = std::get_if<ast::TypedPattern>(&pat->node)) {
    if (typed->name == "_") {
      return {};
    }
    return typed->name;
  }
  return {};
}

bool ContainsTransmuteWarningCandidate(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ContainsTransmuteWarningCandidate(node.expr);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr> ||
                             std::is_same_v<T, ast::BlockExpr>) {
          return node.block &&
                 ContainsTransmuteWarningCandidate(node.block->tail_opt);
        } else {
          return false;
        }
      },
      expr->node);
}

bool IsUniqueMoveInitCompatible(const TypeRef& annotated,
                                const ast::ExprPtr& init,
                                const PlaceTypeFn& type_place) {
  if (!init) {
    return false;
  }

  if (PermOfType(annotated) == Permission::Const) {
    const auto place = type_place(init);
    if (!place.ok || PermOfType(place.type) != Permission::Unique) {
      return false;
    }
    const auto eq = TypeEquiv(StripPerm(place.type), StripPerm(annotated));
    return eq.ok && eq.equiv;
  }

  if (PermOfType(annotated) == Permission::Unique) {
    const auto* move_expr = std::get_if<ast::MoveExpr>(&init->node);
    if (!move_expr || !move_expr->place) {
      return false;
    }
    const auto place = type_place(move_expr->place);
    if (!place.ok) {
      return false;
    }
    const auto eq = TypeEquiv(StripPerm(place.type), StripPerm(annotated));
    return eq.ok && eq.equiv;
  }

  return false;
}

std::optional<std::string_view> ValidateBindingAttributes(
    const ast::Binding& binding,
    std::string* message_out = nullptr) {
  const auto validation =
      ValidateAttributes(binding.attrs, AttributeTarget::Binding);
  if (!validation.ok) {
    if (message_out) {
      *message_out = validation.message;
    }
    if (validation.diag_id.has_value()) {
      return validation.diag_id;
    }
    return std::optional<std::string_view>{"Attr-Target-Err"};
  }
  return std::nullopt;
}

std::optional<std::string> NormalizeDeprecatedMessage(
    const ast::AttributeList& attrs) {
  auto message =
      GetAttributeValue(attrs, ::cursive::analysis::attrs::kDeprecated);
  if (!message.has_value()) {
    return std::nullopt;
  }
  if (message->size() >= 2 &&
      ((message->front() == '"' && message->back() == '"') ||
       (message->front() == '\'' && message->back() == '\''))) {
    return message->substr(1, message->size() - 2);
  }
  return message;
}

void ApplyBindingMetadata(TypeEnv& env,
                          const std::vector<IdKey>& names,
                          const std::optional<TypeBinding::ClosureCaptureInfo>& closure_info,
                          const std::optional<ProvStmtTrackResult>& provenance,
                          const std::optional<ParallelContextKind>& parallel_context_kind,
                          bool derived_from_shared,
                          bool stale_ok,
                          bool deprecated,
                          const std::optional<std::string>& deprecated_message) {
  if (env.scopes.empty()) {
    return;
  }
  auto& scope = env.scopes.back();
  for (const auto& name : names) {
    const auto it = scope.find(name);
    if (it == scope.end()) {
      continue;
    }
    it->second.deprecated = deprecated;
    it->second.deprecated_message = deprecated_message;
    it->second.derived_from_shared = derived_from_shared;
    it->second.stale_ok = stale_ok;
    it->second.stale_after_release = false;
    if (parallel_context_kind.has_value() && names.size() == 1) {
      it->second.parallel_context_kind = *parallel_context_kind;
    }
    if (closure_info.has_value() && names.size() == 1) {
      it->second.closure_capture_info = *closure_info;
    }
    if (provenance.has_value()) {
      ApplyBindingProvenanceSeed(it->second, provenance->kind,
                                 provenance->region);
    }
  }
}

std::optional<ParallelContextKind> BindingParallelContextKind(
    const ast::ExprPtr& expr,
    const TypeEnv& env) {
  const ast::Expr* current = expr.get();
  while (current) {
    if (const auto* attributed = std::get_if<ast::AttributedExpr>(&current->node)) {
      current = attributed->expr.get();
      continue;
    }
    if (const auto* method = std::get_if<ast::MethodCallExpr>(&current->node)) {
      if (method->name == "cpu") {
        return ParallelContextKind::Cpu;
      }
      if (method->name == "gpu") {
        return ParallelContextKind::Gpu;
      }
      if (method->name == "inline") {
        return ParallelContextKind::Inline;
      }
    }
    if (const auto* ident = std::get_if<ast::IdentifierExpr>(&current->node)) {
      if (const auto binding = BindOf(env, ident->name)) {
        return binding->parallel_context_kind;
      }
    }
    return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

StmtTypeResult TypeVarStmt(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::VarStmt& node,
                           const TypeEnv& env,
                           const ExprTypeFn& type_expr,
                           const IdentTypeFn& type_ident,
                           const PlaceTypeFn& type_place) {
  SpecDefsVarStmt();

  const auto& binding = node.binding;
  std::string attr_message;
  if (const auto attr_diag = ValidateBindingAttributes(binding, &attr_message)) {
    return {false, attr_diag, {}, {}, std::move(attr_message)};
  }
  const bool binding_deprecated =
      HasAttribute(binding.attrs, ::cursive::analysis::attrs::kDeprecated);
  const auto deprecated_message = NormalizeDeprecatedMessage(binding.attrs);
  const bool stale_ok =
      HasAttribute(binding.attrs, ::cursive::analysis::attrs::kStaleOk);
  const auto ann_type = ast::BindingAnnotationTypeOpt(binding);
  const StmtTypeContext read_ctx =
      WithSharedAccessMode(type_ctx, ast::KeyMode::Read);
  const ExprTypeFn read_type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, read_ctx, inner, env);
  };
  const PlaceTypeFn read_type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, read_ctx, inner, env);
  };
  const IdentTypeFn read_type_ident = [&](std::string_view name) {
    if (const auto binding = BindOf(env, name)) {
      ExprTypeResult local;
      local.ok = true;
      local.type = binding->type;
      return local;
    }
    return type_ident(name);
  };

  // Case 1: Type annotation provided
  if (ann_type) {
    // Lower the type annotation
    const auto ann = LowerType(ctx, ann_type);
    if (!ann.ok) {
      return {false, ann.diag_id, {}, {}};
    }

    // Check init expression against annotated type
    const auto check =
        CheckExprAgainst(ctx, read_ctx, binding.init, ann.type, env);
    const bool unique_move_ok =
        IsUniqueMoveInitCompatible(ann.type, binding.init, type_place);
    if (!check.ok && !unique_move_ok) {
      if (core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline")) {
        const auto inferred_dbg =
            InferExpr(ctx, binding.init, read_type_expr, read_type_place,
                      read_type_ident);
        if (inferred_dbg.ok) {
          std::fprintf(stderr,
                       "[var-ann-check-fail] %s:%zu:%zu expected=%s inferred=%s diag=%s\n",
                       node.span.file.c_str(),
                       node.span.start_line,
                       node.span.start_col,
                       TypeToString(ann.type).c_str(),
                       TypeToString(inferred_dbg.type).c_str(),
                       check.diag_id.has_value() ? std::string(*check.diag_id).c_str() : "<none>");
        } else {
          std::fprintf(stderr,
                       "[var-ann-check-fail] %s:%zu:%zu expected=%s inferred=<infer-failed> diag=%s\n",
                       node.span.file.c_str(),
                       node.span.start_line,
                       node.span.start_col,
                       TypeToString(ann.type).c_str(),
                       check.diag_id.has_value() ? std::string(*check.diag_id).c_str() : "<none>");
        }
      }
      if (!check.diag_id.has_value() || *check.diag_id == "E-SEM-2526") {
        SPEC_RULE("T-VarStmt-Ann-Mismatch");
        return {false, "E-MOD-2402", {}, {}};
      }
      return {false, check.diag_id, {}, {}};
    }

    if (ContainsTransmuteWarningCandidate(binding.init)) {
      (void)read_type_expr(binding.init);
    }

    // Type the pattern against annotated type
    const auto pat = TypePattern(ctx, binding.pat, ann.type);
    if (!pat.ok) {
      return {false, pat.diag_id, {}, {}};
    }

    // Check pattern names are distinct
    std::vector<IdKey> names;
    CollectPatNames(*binding.pat, names);
    if (!DistinctNames(names)) {
      SPEC_RULE("Pat-Dup-Err");
      return {false, "Pat-Dup-Err", {}, {}};
    }

    const auto closure_info =
        AnalyzeClosureCaptureInfo(binding.init, env, ann.type);

    // Introduce bindings with 'var' mutability
    const auto intro = IntroAll(env, pat.bindings, ast::Mutability::Var, false);
    if (!intro.ok) {
      if (!intro.diag_id.has_value()) {
        SPEC_RULE("Pat-Dup-Err");
        return {false, "Pat-Dup-Err", {}, {}};
      }
      return {false, intro.diag_id, {}, {}};
    }

    TypeEnv out_env = std::move(intro.env);
    std::optional<ProvStmtTrackResult> binding_provenance;
    const auto tracked = TrackBindingProvenance(ctx, binding, env);
    if (tracked.ok) {
      binding_provenance = tracked;
    }
    const bool derived_from_shared =
        ExprNeedsKeyAccess(ctx, read_ctx, binding.init, env);
    const auto parallel_context_kind =
        BindingParallelContextKind(binding.init, env);
    ApplyBindingMetadata(out_env, names, closure_info, binding_provenance,
                         parallel_context_kind,
                         derived_from_shared, stale_ok,
                         binding_deprecated, deprecated_message);

    SPEC_RULE("T-VarStmt-Ann");
    return {true, std::nullopt, std::move(out_env), {}};
  }

  // Case 2: Type inference
  ConstraintSet constraints;
  const auto inferred = InferExpr(ctx, binding.init, read_type_expr,
                                  read_type_place, read_type_ident,
                                  &constraints);
  if (!inferred.ok) {
    if (inferred.diag_id.has_value()) {
      return {false, inferred.diag_id, {}, {}, inferred.diag_detail};
    }
    SPEC_RULE("T-VarStmt-Infer-Err");
    {
      std::string detail;
      const auto name = PatternName(binding.pat);
      if (!name.empty()) {
        detail = "binding '" + name + "'";
      }
      return {false, "T-VarStmt-Infer-Err", {}, {}, std::move(detail)};
    }
  }
  const auto solved = Solve(ctx, constraints);
  if (!solved.ok) {
    if (solved.diag_id.has_value()) {
      return {false, solved.diag_id, {}, {}, inferred.diag_detail};
    }
    SPEC_RULE("T-VarStmt-Infer-Err");
    return {false, "T-VarStmt-Infer-Err", {}, {}, inferred.diag_detail};
  }
  const auto inferred_type = ApplySubstitution(inferred.type, solved.subst);
  if (!inferred_type) {
    SPEC_RULE("T-VarStmt-Infer-Err");
    return {false, "T-VarStmt-Infer-Err", {}, {}, inferred.diag_detail};
  }

  // Type the pattern against inferred type
  const auto pat = TypePattern(ctx, binding.pat, inferred_type);
  if (!pat.ok) {
    return {false, pat.diag_id, {}, {}};
  }

  // Check pattern names are distinct
  std::vector<IdKey> names;
  CollectPatNames(*binding.pat, names);
  if (!DistinctNames(names)) {
    SPEC_RULE("Pat-Dup-Err");
    return {false, "Pat-Dup-Err", {}, {}};
  }

  const auto closure_info =
      AnalyzeClosureCaptureInfo(binding.init, env, inferred_type);

  // Introduce bindings with 'var' mutability
  const auto intro = IntroAll(env, pat.bindings, ast::Mutability::Var, false);
  if (!intro.ok) {
    if (!intro.diag_id.has_value()) {
      SPEC_RULE("Pat-Dup-Err");
      return {false, "Pat-Dup-Err", {}, {}};
    }
    return {false, intro.diag_id, {}, {}};
  }

  TypeEnv out_env = std::move(intro.env);
    std::optional<ProvStmtTrackResult> binding_provenance;
    const auto tracked = TrackBindingProvenance(ctx, binding, env);
    if (tracked.ok) {
      binding_provenance = tracked;
    }
    const bool derived_from_shared =
        ExprNeedsKeyAccess(ctx, read_ctx, binding.init, env);
    const auto parallel_context_kind =
        BindingParallelContextKind(binding.init, env);
    ApplyBindingMetadata(out_env, names, closure_info, binding_provenance,
                         parallel_context_kind,
                         derived_from_shared, stale_ok,
                         binding_deprecated, deprecated_message);

  SPEC_RULE("T-VarStmt-Infer");
  return {true, std::nullopt, std::move(out_env), {}};
}

}  // namespace cursive::analysis
