// =============================================================================
// stmt_common.cpp - Common statement typing utilities
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2: Static Semantics - Statements
//   - Statement typing judgments
//   - StmtResult type
//   - Statement list typing
//   - Block typing
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//   Common statement typing utilities
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/if_case_check.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/memory/regions.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

// Forward declarations for expression typing
bool BitcopyType(const ScopeContext& ctx, const TypeRef& type);
ExprTypeResult TypeExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::ExprPtr& expr,
                        const TypeEnv& env);
ExprTypeResult TypeIdentifierExpr(const ScopeContext& ctx,
                                  const ast::IdentifierExpr& expr,
                                  const TypeEnv& env);
PlaceTypeResult TypePlace(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::ExprPtr& expr,
                          const TypeEnv& env);

// IntroResult is declared in type_stmt.h

namespace {

static inline void SpecDefsTypeStmt() {
  SPEC_DEF("MutKind", "5.2.11");
  SPEC_DEF("Bind", "5.2.11");
  SPEC_DEF("BindOf", "5.2.11");
  SPEC_DEF("MutOf", "5.2.11");
  SPEC_DEF("StmtJudg", "5.2.11");
  SPEC_DEF("LoopFlag", "5.2.11");
  SPEC_DEF("PushScope", "5.2.11");
  SPEC_DEF("PopScope", "5.2.11");
  SPEC_DEF("IntroAll", "5.2.11");
  SPEC_DEF("IntroAllVar", "5.2.11");
  SPEC_DEF("ShadowAll", "5.2.11");
  SPEC_DEF("ShadowAllVar", "5.2.11");
  SPEC_DEF("ResType", "5.2.11");
  SPEC_DEF("LoopTypeInf", "5.2.11");
  SPEC_DEF("LoopTypeFin", "5.2.11");
  SPEC_DEF("LastStmt", "5.2.11");
  SPEC_DEF("HasNonLocalCtrl", "5.2.11");
  SPEC_DEF("WarnResultUnreachable", "5.2.11");
  SPEC_DEF("DeferSafe", "5.2.11");
  SPEC_DEF("RegionActiveType", "5.2.17");
  SPEC_DEF("RegionOptsExpr", "5.2.17");
  SPEC_DEF("RegionBind", "5.2.17");
  SPEC_DEF("InnermostActiveRegion", "5.2.17");
  SPEC_DEF("FrameBind", "5.2.17");
  SPEC_DEF("CaptureSet", "16.9.4");
  SPEC_DEF("ConstCaptures", "16.9.4");
  SPEC_DEF("SharedCaptures", "16.9.4");
  SPEC_DEF("UniqueCaptures", "16.9.4");
  SPEC_DEF("GpuDomainJudg", "20.2.3");
  SPEC_DEF("GpuContext", "20.2.3");
  SPEC_DEF("GpuCaptureJudg", "20.3.3");
  SPEC_DEF("HasHeapProvenance", "20.3.3");
  SPEC_DEF("GpuCaptureOk-Const", "20.3.4");
  SPEC_DEF("GpuCaptureOk-Unique-Move", "20.3.4");
  SPEC_DEF("GpuCapture-Shared-Err", "20.3.4");
  SPEC_DEF("GpuCapture-HeapProv-Err", "20.3.4");
  SPEC_DEF("GpuCapture-NonGpuSafe-Err", "20.3.4");
  SPEC_DEF("StripPerm", "5.2.12");
  SPEC_DEF("NumericTypes", "5.2.12");
  SPEC_DEF("T-CtStmt", "22.1.2");
}

static TypeRef StripPermOnce(const TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->base;
  }
  return type;
}

static TypeRef StripPermRefine(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

static bool IsGpuDomainType(const TypeRef& type) {
  const auto stripped = StripPermRefine(type);
  if (!stripped) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    return IsGpuDomainTypePath(dyn->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    return IsGpuDomainTypePath(path->path);
  }
  return false;
}

static bool IsUsizeType(const TypeRef& type) {
  const auto stripped = StripPermRefine(type);
  if (!stripped) {
    return false;
  }
  if (const auto* prim = std::get_if<TypePrim>(&stripped->node)) {
    return prim->name == "usize";
  }
  return false;
}

static constexpr std::array<std::string_view, 12> kIntTypes = {
    "i8", "i16", "i32", "i64", "i128", "isize",
    "u8", "u16", "u32", "u64", "u128", "usize"};
static constexpr std::array<std::string_view, 3> kFloatTypes = {"f16", "f32",
                                                                "f64"};

static bool IsNumericType(const TypeRef& type) {
  const auto stripped = StripPermOnce(type);
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<TypePrim>(&stripped->node);
  if (!prim) {
    return false;
  }
  for (const auto& t : kIntTypes) {
    if (prim->name == t) {
      return true;
    }
  }
  for (const auto& t : kFloatTypes) {
    if (prim->name == t) {
      return true;
    }
  }
  return false;
}

static bool InScope(const TypeEnv& env, const IdKey& key) {
  if (env.scopes.empty()) {
    return false;
  }
  return env.scopes.back().find(key) != env.scopes.back().end();
}

static bool InOuter(const TypeEnv& env, const IdKey& key) {
  if (env.scopes.size() < 2) {
    return false;
  }
  for (std::size_t i = 0; i + 1 < env.scopes.size(); ++i) {
    if (env.scopes[i].find(key) != env.scopes[i].end()) {
      return true;
    }
  }
  return false;
}

static IntroResult IntroBinding(const TypeEnv& env,
                                std::string_view name,
                                const TypeBinding& binding) {
  SpecDefsTypeStmt();
  if (ReservedGen(name)) {
    SPEC_RULE("Intro-Reserved-Gen-Err");
    return {false, "Intro-Reserved-Gen-Err", env};
  }

  const auto key = IdKeyOf(name);
  if (env.scopes.empty()) {
    return {false, std::nullopt, env};
  }

  if (InScope(env, key)) {
    SPEC_RULE("Intro-Dup");
    return {false, std::nullopt, env};
  }
  if (InOuter(env, key)) {
    SPEC_RULE("Intro-Outer-Err");
    return {false, "Intro-Outer-Err", env};
  }

  TypeEnv out = env;
  out.scopes.back().emplace(key, binding);
  SPEC_RULE("Intro-Ok");
  return {true, std::nullopt, std::move(out)};
}

static IntroResult ShadowIntroBinding(const TypeEnv& env,
                                      std::string_view name,
                                      const TypeBinding& binding) {
  SpecDefsTypeStmt();
  if (ReservedGen(name)) {
    SPEC_RULE("Shadow-Reserved-Gen-Err");
    return {false, "Shadow-Reserved-Gen-Err", env};
  }

  const auto key = IdKeyOf(name);
  if (env.scopes.empty()) {
    return {false, std::nullopt, env};
  }
  if (InScope(env, key)) {
    return {false, std::nullopt, env};
  }
  if (!InOuter(env, key)) {
    SPEC_RULE("Shadow-Unnecessary");
    return {false, "Shadow-Unnecessary", env};
  }

  TypeEnv out = env;
  out.scopes.back().emplace(key, binding);
  SPEC_RULE("Shadow-Ok");
  return {true, std::nullopt, std::move(out)};
}

static core::Span SpanOfStmt(const ast::Stmt& stmt) {
  return std::visit(
      [](const auto& node) -> core::Span { return node.span; },
      stmt);
}

static TypeRef StmtProjectFilesType() { return MakeTypePath({"ProjectFiles"}); }
static TypeRef StmtTypeEmitterType() { return MakeTypePath({"TypeEmitter"}); }
static TypeRef StmtIntrospectType() { return MakeTypePath({"Introspect"}); }
static TypeRef StmtComptimeDiagnosticsType() {
  return MakeTypePath({"ComptimeDiagnostics"});
}

static TypeEnv ExtendStmtComptimeEnv(const TypeEnv& env,
                                     const ast::AttributeList& attrs) {
  TypeEnv out = PushScope(env);
  auto& scope = out.scopes.back();
  scope[IdKeyOf("introspect")] =
      TypeBinding{ast::Mutability::Let, StmtIntrospectType()};
  scope[IdKeyOf("diagnostics")] =
      TypeBinding{ast::Mutability::Let, StmtComptimeDiagnosticsType()};
  if (HasAttribute(attrs, ::cursive::analysis::attrs::kFiles)) {
    scope[IdKeyOf("files")] =
        TypeBinding{ast::Mutability::Let, StmtProjectFilesType()};
  }
  if (HasAttribute(attrs, ::cursive::analysis::attrs::kEmit)) {
    scope[IdKeyOf("emitter")] =
        TypeBinding{ast::Mutability::Let, StmtTypeEmitterType()};
  }
  return out;
}

static StmtTypeResult TypeCtStmt(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::CtStmt& stmt,
                                 const TypeEnv& env,
                                 const ExprTypeFn& type_expr,
                                 const IdentTypeFn& type_ident,
                                 const PlaceTypeFn& type_place) {
  if (!stmt.body) {
    return {false, std::nullopt, env, {}};
  }

  const TypeEnv comptime_env = ExtendStmtComptimeEnv(env, stmt.attrs);
  const auto check =
      CheckBlock(ctx, type_ctx, *stmt.body, comptime_env, MakeTypePrim("()"),
                 type_expr, type_ident, type_place, type_ctx.env_ref);
  if (!check.ok) {
    return {false, check.diag_id, env, {}, check.diag_detail, check.diag_span};
  }

  SPEC_RULE_AT("T-CtStmt", stmt.span);
  return {true, std::nullopt, env, {}};
}

static bool WarnResultUnreachable(const std::vector<ast::Stmt>& stmts,
                                  const StmtTypeContext& type_ctx) {
  SpecDefsTypeStmt();
  (void)type_ctx;
  bool seen_non_local = false;
  for (const auto& stmt : stmts) {
    const bool is_non_local =
        std::holds_alternative<ast::ReturnStmt>(stmt) ||
        std::holds_alternative<ast::BreakStmt>(stmt) ||
        std::holds_alternative<ast::ContinueStmt>(stmt);
    if (seen_non_local) {
      break;
    }
    seen_non_local = seen_non_local || is_non_local;
  }
  SPEC_RULE("WarnResultUnreachable");
  return true;
}

}  // namespace

BindingProvenanceSeedKind NormalizeBindingProvenanceSeed(
    ProvenanceKind kind) {
  switch (kind) {
    case ProvenanceKind::Global:
      return BindingProvenanceSeedKind::Global;
    case ProvenanceKind::Stack:
      return BindingProvenanceSeedKind::Stack;
    case ProvenanceKind::Heap:
      return BindingProvenanceSeedKind::Heap;
    case ProvenanceKind::Region:
      return BindingProvenanceSeedKind::Region;
    case ProvenanceKind::Bottom:
      return BindingProvenanceSeedKind::Stack;
    case ProvenanceKind::Param:
      return BindingProvenanceSeedKind::Param;
  }
  return BindingProvenanceSeedKind::Stack;
}

void ApplyBindingProvenanceSeed(TypeBinding& binding,
                                ProvenanceKind kind,
                                const std::optional<std::string>& region) {
  binding.provenance_kind = NormalizeBindingProvenanceSeed(kind);
  if (kind == ProvenanceKind::Region && region.has_value()) {
    binding.provenance_region = IdKeyOf(*region);
    return;
  }
  binding.provenance_region.reset();
}

// =============================================================================
// CollectPatNames - Collect all identifier names from a pattern
// =============================================================================
//
// Used to check for duplicate pattern bindings. This function traverses
// the pattern structure and collects all identifiers that would be bound.
//
// =============================================================================

// Forward declaration for mutual recursion
static void CollectFieldPatNamesImpl(const ast::FieldPattern& field,
                                     std::vector<IdKey>& out);

void CollectPatNames(const ast::Pattern& pat, std::vector<IdKey>& out) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          out.push_back(IdKeyOf(node.name));
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (node.name == "_") {
            return;
          }
          out.push_back(IdKeyOf(node.name));
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            if (elem) {
              CollectPatNames(*elem, out);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            CollectFieldPatNamesImpl(field, out);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt.has_value()) {
            return;
          }
          std::visit(
              [&](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& elem : payload.elements) {
                    if (elem) {
                      CollectPatNames(*elem, out);
                    }
                  }
                } else {
                  for (const auto& field : payload.fields) {
                    CollectFieldPatNamesImpl(field, out);
                  }
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt.has_value()) {
            return;
          }
          for (const auto& field : node.fields_opt->fields) {
            CollectFieldPatNamesImpl(field, out);
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          if (node.lo) {
            CollectPatNames(*node.lo, out);
          }
          if (node.hi) {
            CollectPatNames(*node.hi, out);
          }
        } else {
          // WildcardPattern and LiteralPattern don't bind names
          return;
        }
      },
      pat.node);
}

static void CollectFieldPatNamesImpl(const ast::FieldPattern& field,
                                     std::vector<IdKey>& out) {
  if (field.pattern_opt) {
    CollectPatNames(*field.pattern_opt, out);
    return;
  }
  // Shorthand: field name itself becomes binding
  out.push_back(IdKeyOf(field.name));
}

// =============================================================================
// DistinctNames - Check if all names in a collection are distinct
// =============================================================================

bool DistinctNames(const std::vector<IdKey>& names) {
  std::unordered_set<IdKey> seen;
  for (const auto& name : names) {
    if (!seen.insert(name).second) {
      return false;
    }
  }
  return true;
}

// =============================================================================
// IntroAll - Introduce multiple bindings into the environment
// =============================================================================

// Forward declarations for static helpers used by IntroAll
namespace {
static IntroResult IntroBinding(const TypeEnv& env,
                                std::string_view name,
                                const TypeBinding& binding);
static IntroResult ShadowIntroBinding(const TypeEnv& env,
                                      std::string_view name,
                                      const TypeBinding& binding);
static inline void SpecDefsTypeStmt();
}  // namespace

IntroResult IntroAll(const TypeEnv& env,
                     const std::vector<std::pair<std::string, TypeRef>>& binds,
                     ast::Mutability mut,
                     bool shadow) {
  SpecDefsTypeStmt();
  if (binds.empty()) {
    if (shadow) {
      if (mut == ast::Mutability::Var) {
        SPEC_RULE("ShadowAllVar-Empty");
      } else {
        SPEC_RULE("ShadowAll-Empty");
      }
    } else {
      if (mut == ast::Mutability::Var) {
        SPEC_RULE("IntroAllVar-Empty");
      } else {
        SPEC_RULE("IntroAll-Empty");
      }
    }
    return {true, std::nullopt, env};
  }

  TypeEnv current = env;
  for (const auto& [name, type] : binds) {
    TypeBinding binding{mut, type};
    binding.storage_type = type;
    IntroResult res = shadow ? ShadowIntroBinding(current, name, binding)
                             : IntroBinding(current, name, binding);
    if (!res.ok) {
      return res;
    }
    current = std::move(res.env);
    if (shadow) {
      if (mut == ast::Mutability::Var) {
        SPEC_RULE("ShadowAllVar-Cons");
      } else {
        SPEC_RULE("ShadowAll-Cons");
      }
    } else {
      if (mut == ast::Mutability::Var) {
        SPEC_RULE("IntroAllVar-Cons");
      } else {
        SPEC_RULE("IntroAll-Cons");
      }
    }
  }

  return {true, std::nullopt, std::move(current)};
}

namespace {

static std::optional<TypeRef> ResType(const ScopeContext& ctx,
                                      const std::vector<TypeRef>& types) {
  SpecDefsTypeStmt();
  if (types.empty()) {
    return std::nullopt;
  }
  TypeRef base = types.front();
  for (std::size_t i = 1; i < types.size(); ++i) {
    const auto equiv = TypeEquiv(base, types[i]);
    if (!equiv.ok || !equiv.equiv) {
      return std::nullopt;
    }
  }
  return base;
}

static std::optional<TypeRef> LoopTypeInf(const ScopeContext& ctx,
                                          const std::vector<TypeRef>& breaks,
                                          bool break_void) {
  SpecDefsTypeStmt();
  if (breaks.empty() && !break_void) {
    return MakeTypePrim("!");
  }
  if (breaks.empty() && break_void) {
    return MakeTypePrim("()");
  }
  if (!break_void) {
    const auto res = ResType(ctx, breaks);
    if (res.has_value()) {
      return res;
    }
  }
  return std::nullopt;
}

static std::optional<TypeRef> LoopTypeFin(const ScopeContext& ctx,
                                          const std::vector<TypeRef>& breaks,
                                          bool break_void) {
  SpecDefsTypeStmt();
  if (breaks.empty()) {
    return MakeTypePrim("()");
  }
  if (!break_void) {
    const auto res = ResType(ctx, breaks);
    if (res.has_value()) {
      return res;
    }
  }
  return std::nullopt;
}

static std::optional<std::reference_wrapper<const ast::Stmt>> LastStmt(
    const std::vector<ast::Stmt>& stmts) {
  SpecDefsTypeStmt();
  if (stmts.empty()) {
    return std::nullopt;
  }
  return std::cref(stmts.back());
}

static std::optional<std::string_view> PlaceRootName(
    const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return ident->name;
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return PlaceRootName(field->base);
  }
  if (const auto* tup = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return PlaceRootName(tup->base);
  }
  if (const auto* idx = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return PlaceRootName(idx->base);
  }
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    return PlaceRootName(deref->value);
  }
  return std::nullopt;
}

static ExprTypeResult TypeIdentExpr(const ScopeContext& ctx,
                                    const TypeEnv& env,
                                    std::string_view name) {
  const auto binding = BindOf(env, name);
  if (!binding.has_value()) {
    return {false, "ResolveExpr-Ident-Err", {}};
  }
  if (!BitcopyType(ctx, binding->type)) {
    SPEC_RULE("ValueUse-NonBitcopyPlace");
    return {false, "ValueUse-NonBitcopyPlace", {}};
  }
  return {true, std::nullopt, binding->type};
}

static ExprTypeResult TypeExprWithEnv(const ScopeContext& ctx,
                                      const StmtTypeContext& type_ctx,
                                      const TypeEnv& env,
                                      const ExprTypeFn& type_expr,
                                      const ast::ExprPtr& expr) {
  if (!expr) {
    return {false, std::nullopt, {}};
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    const auto binding = BindOf(env, ident->name);
    if (binding.has_value()) {
      return TypeIdentExpr(ctx, env, ident->name);
    }
  }
  const auto via_env = TypeExpr(ctx, type_ctx, expr, env);
  if (via_env.ok || via_env.diag_id.has_value()) {
    return via_env;
  }
  const auto via_callback = type_expr(expr);
  if (via_callback.ok) {
    return via_callback;
  }
  return via_callback;
}

static PlaceTypeResult TypePlaceWithEnv(const TypeEnv& env,
                                        const PlaceTypeFn& type_place,
                                        const ast::ExprPtr& expr) {
  if (!expr) {
    return {false, std::nullopt, {}};
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    const auto binding = BindOf(env, ident->name);
    if (binding.has_value()) {
      return {true, std::nullopt, binding->type};
    }
  }
  return type_place(expr);
}

struct FlowTypingFns {
  ExprTypeFn type_expr;
  IdentTypeFn type_ident;
  PlaceTypeFn type_place;
};

static FlowTypingFns MakeFlowTypingFns(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const TypeEnv& env) {
  FlowTypingFns fns;
  fns.type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  fns.type_ident = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)}, env);
  };
  fns.type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };
  return fns;
}

static void MergeBreakFlow(FlowInfo& dst, const FlowInfo& src) {
  dst.breaks.insert(dst.breaks.end(), src.breaks.begin(), src.breaks.end());
  dst.break_void = dst.break_void || src.break_void;
}

static FlowInfo CollectNestedBreakFlowFromExpr(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const TypeEnv& env,
                                               const ExprTypeFn& type_expr,
                                               const ast::ExprPtr& expr);

static FlowInfo CollectNestedBreakFlowFromStmt(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const TypeEnv& env,
                                               const ExprTypeFn& type_expr,
                                               const ast::Stmt& stmt);

static FlowInfo CollectNestedBreakFlowFromBlock(const ScopeContext& ctx,
                                                const StmtTypeContext& type_ctx,
                                                const TypeEnv& env,
                                                const ExprTypeFn& type_expr,
                                                const ast::Block& block) {
  FlowInfo flow;
  for (const auto& stmt : block.stmts) {
    MergeBreakFlow(flow, CollectNestedBreakFlowFromStmt(
                             ctx, type_ctx, env, type_expr, stmt));
  }
  MergeBreakFlow(flow, CollectNestedBreakFlowFromExpr(
                           ctx, type_ctx, env, type_expr, block.tail_opt));
  return flow;
}

static FlowInfo CollectNestedBreakFlowFromBlock(const ScopeContext& ctx,
                                                const StmtTypeContext& type_ctx,
                                                const TypeEnv& env,
                                                const ExprTypeFn& type_expr,
                                                const ast::BlockPtr& block) {
  if (!block) {
    return {};
  }
  return CollectNestedBreakFlowFromBlock(ctx, type_ctx, env, type_expr, *block);
}

static FlowInfo CollectNestedBreakFlowFromBreak(const ScopeContext& ctx,
                                                const StmtTypeContext& type_ctx,
                                                const TypeEnv& env,
                                                const ExprTypeFn& type_expr,
                                                const ast::BreakStmt& stmt) {
  FlowInfo flow;
  if (!stmt.value_opt) {
    flow.break_void = true;
    return flow;
  }
  const auto typed =
      TypeExprWithEnv(ctx, type_ctx, env, type_expr, stmt.value_opt);
  if (typed.ok) {
    flow.breaks.push_back(typed.type);
  }
  return flow;
}

static FlowInfo CollectNestedBreakFlowFromStmt(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const TypeEnv& env,
                                               const ExprTypeFn& type_expr,
                                               const ast::Stmt& stmt) {
  return std::visit(
      [&](const auto& node) -> FlowInfo {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          return CollectNestedBreakFlowFromBreak(ctx, type_ctx, env, type_expr, node);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return CollectNestedBreakFlowFromExpr(ctx, type_ctx, env, type_expr,
                                                node.value);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::RegionStmt> ||
                             std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          return CollectNestedBreakFlowFromBlock(ctx, type_ctx, env, type_expr,
                                                 node.body);
        } else {
          return {};
        }
      },
      stmt);
}

static FlowInfo CollectNestedBreakFlowFromExpr(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const TypeEnv& env,
                                               const ExprTypeFn& type_expr,
                                               const ast::ExprPtr& expr) {
  if (!expr) {
    return {};
  }
  return std::visit(
      [&](const auto& node) -> FlowInfo {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return CollectNestedBreakFlowFromExpr(ctx, type_ctx, env, type_expr,
                                                node.expr);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return CollectNestedBreakFlowFromBlock(ctx, type_ctx, env, type_expr,
                                                 node.block);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          FlowInfo out;
          MergeBreakFlow(out, CollectNestedBreakFlowFromExpr(
                                  ctx, type_ctx, env, type_expr, node.then_expr));
          MergeBreakFlow(out, CollectNestedBreakFlowFromExpr(
                                  ctx, type_ctx, env, type_expr, node.else_expr));
          return out;
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          FlowInfo out;
          MergeBreakFlow(out, CollectNestedBreakFlowFromExpr(
                                  ctx, type_ctx, env, type_expr, node.then_expr));
          MergeBreakFlow(out, CollectNestedBreakFlowFromExpr(
                                  ctx, type_ctx, env, type_expr, node.else_expr));
          return out;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          FlowInfo out;
          for (const auto& clause : node.cases) {
            MergeBreakFlow(out, CollectNestedBreakFlowFromExpr(
                                    ctx, type_ctx, env, type_expr, clause.body));
          }
          MergeBreakFlow(out, CollectNestedBreakFlowFromExpr(
                                  ctx, type_ctx, env, type_expr, node.else_expr));
          return out;
        } else {
          return {};
        }
      },
      expr->node);
}

static IdentTypeFn IdentTypeFnWithEnv(const ScopeContext& ctx,
                                      const TypeEnv& env,
                                      const IdentTypeFn& type_ident) {
  return [&](std::string_view name) -> ExprTypeResult {
    const auto binding = BindOf(env, name);
    if (binding.has_value()) {
      return TypeIdentExpr(ctx, env, name);
    }
    return type_ident(name);
  };
}

static IfCaseCheckFn MakeIfCaseCheck(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const TypeEnv& env) {
  return [&](const ast::IfCaseExpr& if_case,
             const TypeRef& expected) -> CheckResult {
    return CheckIfCaseExpr(ctx, type_ctx, if_case, env, expected);
  };
}

// Forward declarations for non-local control flow checking
static bool HasNonLocalCtrlExpr(const ast::ExprPtr& expr, bool in_loop);
static bool HasNonLocalCtrlBlock(const ast::Block& block, bool in_loop);

static ast::ExprPtr StripAttributedExpr(const ast::ExprPtr& expr) {
  ast::ExprPtr current = expr;
  while (current) {
    const auto* attributed = std::get_if<ast::AttributedExpr>(&current->node);
    if (!attributed) {
      break;
    }
    current = attributed->expr;
  }
  return current;
}

static ast::ExprPtr MakeProofExpr(const core::Span& span,
                                  ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static std::optional<std::string> SimpleBindingName(const ast::PatternPtr& pat) {
  if (!pat) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierPattern>(&pat->node)) {
    return ident->name;
  }
  if (const auto* typed = std::get_if<ast::TypedPattern>(&pat->node)) {
    return typed->name;
  }
  return std::nullopt;
}

static ast::ExprPtr BindingEqualityFact(const std::string& name,
                                        const ast::ExprPtr& init,
                                        const core::Span& span) {
  ast::IdentifierExpr ident;
  ident.name = name;
  ast::BinaryExpr equality;
  equality.op = "==";
  equality.lhs = MakeProofExpr(span, std::move(ident));
  equality.rhs = init;
  return MakeProofExpr(span, std::move(equality));
}

static ast::ExprPtr ZeroLiteralExpr(const core::Span& span) {
  ast::LiteralExpr literal;
  literal.literal.kind = ast::TokenKind::IntLiteral;
  literal.literal.lexeme = "0";
  literal.literal.span = span;
  return MakeProofExpr(span, std::move(literal));
}

static bool IsUnsignedPrimitiveName(std::string_view name) {
  return name == "usize" ||
         name == "u8" ||
         name == "u16" ||
         name == "u32" ||
         name == "u64";
}

static bool BindingHasUnsignedAnnotation(const ast::Binding& binding) {
  const auto type = ast::BindingAnnotationTypeOpt(binding);
  if (!type) {
    return false;
  }
  const auto* prim = std::get_if<ast::TypePrim>(&type->node);
  return prim && IsUnsignedPrimitiveName(prim->name);
}

static ast::ExprPtr BindingNonNegativeFact(const std::string& name,
                                           const core::Span& span) {
  ast::IdentifierExpr ident;
  ident.name = name;
  ast::BinaryExpr comparison;
  comparison.op = "<=";
  comparison.lhs = ZeroLiteralExpr(span);
  comparison.rhs = MakeProofExpr(span, std::move(ident));
  return MakeProofExpr(span, std::move(comparison));
}

static std::shared_ptr<StaticProofContext> LetBindingProofContextForStmt(
    const ScopeContext& ctx,
    const std::shared_ptr<StaticProofContext>& current_proof_ctx,
    const ast::Stmt& stmt) {
  const auto* let_stmt = std::get_if<ast::LetStmt>(&stmt);
  if (!let_stmt || !let_stmt->binding.init) {
    return current_proof_ctx;
  }

  const auto binding_name = SimpleBindingName(let_stmt->binding.pat);
  if (!binding_name.has_value()) {
    return current_proof_ctx;
  }

  ContractContext contract_ctx;
  contract_ctx.scope_ctx = &ctx;
  const auto purity = CheckPurity(contract_ctx, let_stmt->binding.init);
  if (!purity.ok) {
    return current_proof_ctx;
  }

  std::shared_ptr<StaticProofContext> proof_ctx =
      ExtendProofContextWithPredicate(
      current_proof_ctx,
      BindingEqualityFact(*binding_name, let_stmt->binding.init,
                          let_stmt->span));
  if (BindingHasUnsignedAnnotation(let_stmt->binding)) {
    proof_ctx = ExtendProofContextWithPredicate(
        proof_ctx, BindingNonNegativeFact(*binding_name, let_stmt->span));
  }
  return proof_ctx;
}

static std::shared_ptr<StaticProofContext> FallthroughProofContextForStmt(
    const ScopeContext& ctx,
    const std::shared_ptr<StaticProofContext>& current_proof_ctx,
    const ast::Stmt& stmt) {
  const auto with_binding_fact =
      LetBindingProofContextForStmt(ctx, current_proof_ctx, stmt);

  const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt);
  if (!expr_stmt || !expr_stmt->value) {
    return with_binding_fact;
  }

  const auto stripped = StripAttributedExpr(expr_stmt->value);
  if (!stripped) {
    return with_binding_fact;
  }
  const auto* if_expr = std::get_if<ast::IfExpr>(&stripped->node);
  if (!if_expr || if_expr->else_expr || !if_expr->then_expr) {
    return with_binding_fact;
  }
  if (!HasNonLocalCtrlExpr(if_expr->then_expr, false)) {
    return with_binding_fact;
  }

  ContractContext contract_ctx;
  contract_ctx.scope_ctx = &ctx;
  const auto purity = CheckPurity(contract_ctx, if_expr->cond);
  if (!purity.ok) {
    return with_binding_fact;
  }

  const auto negated = NegatedPredicate(if_expr->cond);
  if (!negated.has_value()) {
    return with_binding_fact;
  }
  return ExtendProofContextWithPredicate(with_binding_fact, *negated);
}

static bool HasNonLocalCtrlStmt(const ast::Stmt& stmt, bool in_loop) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          SPEC_RULE("HasNonLocalCtrl-Return");
          return true;
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          if (!in_loop) {
            SPEC_RULE("HasNonLocalCtrl-Break");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          if (!in_loop) {
            SPEC_RULE("HasNonLocalCtrl-Continue");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::LetStmt>) {
          if (HasNonLocalCtrlExpr(node.binding.init, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          if (HasNonLocalCtrlExpr(node.binding.init, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          if (HasNonLocalCtrlExpr(node.place, in_loop) ||
              HasNonLocalCtrlExpr(node.value, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          if (HasNonLocalCtrlExpr(node.place, in_loop) ||
              HasNonLocalCtrlExpr(node.value, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          if (HasNonLocalCtrlExpr(node.value, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (node.body && HasNonLocalCtrlBlock(*node.body, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (HasNonLocalCtrlExpr(node.opts_opt, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          if (node.body && HasNonLocalCtrlBlock(*node.body, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.body && HasNonLocalCtrlBlock(*node.body, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (node.body && HasNonLocalCtrlBlock(*node.body, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body && HasNonLocalCtrlBlock(*node.body, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else {
          return false;
        }
      },
      stmt);
}

static bool HasNonLocalCtrlBlock(const ast::Block& block, bool in_loop) {
  for (const auto& stmt : block.stmts) {
    if (HasNonLocalCtrlStmt(stmt, in_loop)) {
      return true;
    }
  }
  if (block.tail_opt && HasNonLocalCtrlExpr(block.tail_opt, in_loop)) {
    SPEC_RULE("HasNonLocalCtrl-Child");
    return true;
  }
  return false;
}

static bool HasNonLocalCtrlExpr(const ast::ExprPtr& expr, bool in_loop) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.body && HasNonLocalCtrlBlock(*node.body, true)) {
            SPEC_RULE("HasNonLocalCtrl-LoopInfinite");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          if (HasNonLocalCtrlExpr(node.cond, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          if (node.body && HasNonLocalCtrlBlock(*node.body, true)) {
            SPEC_RULE("HasNonLocalCtrl-LoopConditional");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          if (HasNonLocalCtrlExpr(node.iter, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          if (node.body && HasNonLocalCtrlBlock(*node.body, true)) {
            SPEC_RULE("HasNonLocalCtrl-LoopIter");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block && HasNonLocalCtrlBlock(*node.block, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block && HasNonLocalCtrlBlock(*node.block, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          if (HasNonLocalCtrlExpr(node.cond, in_loop) ||
              HasNonLocalCtrlExpr(node.then_expr, in_loop) ||
              HasNonLocalCtrlExpr(node.else_expr, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (HasNonLocalCtrlExpr(node.scrutinee, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          for (const auto& case_clause : node.cases) {
            if (HasNonLocalCtrlExpr(case_clause.body, in_loop)) {
              SPEC_RULE("HasNonLocalCtrl-Child");
              return true;
            }
          }
          return HasNonLocalCtrlExpr(node.else_expr, in_loop);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          if (HasNonLocalCtrlExpr(node.scrutinee, in_loop) ||
              HasNonLocalCtrlExpr(node.then_expr, in_loop) ||
              HasNonLocalCtrlExpr(node.else_expr, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          if (HasNonLocalCtrlExpr(node.lhs, in_loop) ||
              HasNonLocalCtrlExpr(node.rhs, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          if (HasNonLocalCtrlExpr(node.value, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (HasNonLocalCtrlExpr(node.callee, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          for (const auto& arg : node.args) {
            if (HasNonLocalCtrlExpr(arg.value, in_loop)) {
              SPEC_RULE("HasNonLocalCtrl-Child");
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (HasNonLocalCtrlExpr(node.receiver, in_loop)) {
            SPEC_RULE("HasNonLocalCtrl-Child");
            return true;
          }
          for (const auto& arg : node.args) {
            if (HasNonLocalCtrlExpr(arg.value, in_loop)) {
              SPEC_RULE("HasNonLocalCtrl-Child");
              return true;
            }
          }
          return false;
        } else {
          return false;
        }
      },
      expr->node);
}

class OuterCaptureCollector {
 public:
  explicit OuterCaptureCollector(const TypeEnv& env) : env_(env) {
    local_scopes_.emplace_back();
  }

  std::unordered_set<IdKey> CollectClosure(const ast::ClosureExpr& closure) {
    PushScope();
    for (const auto& param : closure.params) {
      DeclareName(param.name);
    }
    VisitExpr(closure.body);
    PopScope();
    return captures_;
  }

  std::unordered_set<IdKey> CollectBlock(const ast::Block& block) {
    VisitBlock(block);
    return captures_;
  }

 private:
  void PushScope() { local_scopes_.emplace_back(); }

  void PopScope() {
    if (!local_scopes_.empty()) {
      local_scopes_.pop_back();
    }
  }

  bool IsLocal(std::string_view name) const {
    const IdKey key = IdKeyOf(name);
    for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
      if (it->find(key) != it->end()) {
        return true;
      }
    }
    return false;
  }

  void DeclareName(std::string_view name) {
    if (local_scopes_.empty()) {
      local_scopes_.emplace_back();
    }
    local_scopes_.back().insert(IdKeyOf(name));
  }

  void DeclarePattern(const ast::PatternPtr& pattern) {
    if (!pattern) {
      return;
    }
    std::vector<IdKey> names;
    CollectPatNames(*pattern, names);
    for (const auto& name : names) {
      DeclareName(name);
    }
  }

  void CaptureIfOuter(std::string_view name) {
    if (IsLocal(name)) {
      return;
    }
    if (BindOf(env_, name).has_value()) {
      captures_.insert(IdKeyOf(name));
    }
  }

  void VisitKeyPath(const ast::KeyPathExpr& path) {
    CaptureIfOuter(path.root);
    for (const auto& seg : path.segs) {
      if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
        VisitExpr(idx->expr);
      }
    }
  }

  void VisitBlock(const ast::Block& block) {
    PushScope();
    for (const auto& stmt : block.stmts) {
      VisitStmt(stmt);
    }
    VisitExpr(block.tail_opt);
    PopScope();
  }

  void VisitStmt(const ast::Stmt& stmt) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            VisitExpr(node.binding.init);
            DeclarePattern(node.binding.pat);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression,
            // but the alias name still enters the surrounding scope.
            DeclareName(node.alias);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            VisitExpr(node.opts_opt);
            if (node.body) {
              PushScope();
              if (node.alias_opt.has_value()) {
                DeclareName(*node.alias_opt);
              }
              for (const auto& inner : node.body->stmts) {
                VisitStmt(inner);
              }
              VisitExpr(node.body->tail_opt);
              PopScope();
            }
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            if (node.target_opt.has_value()) {
              CaptureIfOuter(*node.target_opt);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            for (const auto& path : node.paths) {
              VisitKeyPath(path);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else {
            // ContinueStmt / ErrorStmt carry no expressions.
          }
        },
        stmt);
  }

  void VisitExpr(const ast::ExprPtr& expr) {
    if (!expr) {
      return;
    }

    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
            CaptureIfOuter(node.name);
          } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            if (std::holds_alternative<ast::ParenArgs>(node.args)) {
              const auto& args = std::get<ast::ParenArgs>(node.args).args;
              for (const auto& arg : args) {
                VisitExpr(arg.value);
              }
            } else {
              const auto& fields = std::get<ast::BraceArgs>(node.args).fields;
              for (const auto& field : fields) {
                VisitExpr(field.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
            VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            VisitExpr(elem);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            VisitExpr(elem);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          VisitExpr(node.value);
          VisitExpr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (!node.payload_opt.has_value()) {
              return;
            }
            if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
              const auto& payload = std::get<ast::EnumPayloadParen>(*node.payload_opt);
              for (const auto& elem : payload.elements) {
                VisitExpr(elem);
              }
            } else {
              const auto& payload = std::get<ast::EnumPayloadBrace>(*node.payload_opt);
              for (const auto& field : payload.fields) {
                VisitExpr(field.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee);
            for (const auto& case_clause : node.cases) {
              PushScope();
              DeclarePattern(case_clause.pattern);
              VisitExpr(case_clause.body);
              PopScope();
            }
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee);
            PushScope();
            DeclarePattern(node.pattern);
            VisitExpr(node.then_expr);
            PopScope();
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter);
            PushScope();
            DeclarePattern(node.pattern);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              for (const auto& stmt : node.body->stmts) {
                VisitStmt(stmt);
              }
              VisitExpr(node.body->tail_opt);
            }
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr>) {
            if (node.block) {
              VisitBlock(*node.block);
            }
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            // Nested closure captures are resolved within the nested closure.
            PushScope();
            for (const auto& param : node.params) {
              DeclareName(param.name);
            }
            VisitExpr(node.body);
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base);
            VisitExpr(node.index);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            VisitExpr(node.receiver);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              VisitExpr(arm.expr);
              if (arm.pattern) {
                PushScope();
                DeclarePattern(arm.pattern);
                VisitExpr(arm.handler.value);
                PopScope();
              } else {
                VisitExpr(arm.handler.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& sub : node.exprs) {
              VisitExpr(sub);
            }
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            VisitExpr(node.domain);
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            VisitExpr(node.handle);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            VisitExpr(node.range);
            if (node.key_clause.has_value()) {
              VisitKeyPath(node.key_clause->key_path);
            }
            for (const auto& opt : node.opts) {
              VisitExpr(opt.chunk_expr);
              VisitExpr(opt.workgroup_expr);
            }
            PushScope();
            DeclarePattern(node.pattern);
            if (node.body) {
              for (const auto& stmt : node.body->stmts) {
                VisitStmt(stmt);
              }
              VisitExpr(node.body->tail_opt);
            }
            PopScope();
          } else {
            // ErrorExpr, LiteralExpr, QualifiedNameExpr, PathExpr,
            // PtrNullExpr, SizeofExpr, AlignofExpr, ResultExpr have no children.
          }
        },
        expr->node);
  }

  const TypeEnv& env_;
  std::vector<std::unordered_set<IdKey>> local_scopes_;
  std::unordered_set<IdKey> captures_;
};

class SpawnExprPresenceFinder {
 public:
  bool Contains(const ast::ExprPtr& expr) {
    found_ = false;
    VisitExpr(expr);
    return found_;
  }

 private:
  void VisitKeyPath(const ast::KeyPathExpr& path) {
    if (found_) {
      return;
    }
    for (const auto& seg : path.segs) {
      if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
        VisitExpr(idx->expr);
      }
    }
  }

  void VisitBlock(const ast::Block& block) {
    if (found_) {
      return;
    }
    for (const auto& stmt : block.stmts) {
      VisitStmt(stmt);
      if (found_) {
        return;
      }
    }
    VisitExpr(block.tail_opt);
  }

  void VisitStmt(const ast::Stmt& stmt) {
    if (found_) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            VisitExpr(node.binding.init);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression.
            (void)node;
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt> ||
                               std::is_same_v<T, ast::CtStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            VisitExpr(node.opts_opt);
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            for (const auto& path : node.paths) {
              VisitKeyPath(path);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else {
            // ContinueStmt / ErrorStmt carry no nested expressions.
          }
        },
        stmt);
  }

  void VisitExpr(const ast::ExprPtr& expr) {
    if (!expr || found_) {
      return;
    }

    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            found_ = true;
          } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            if (std::holds_alternative<ast::ParenArgs>(node.args)) {
              for (const auto& arg : std::get<ast::ParenArgs>(node.args).args) {
                VisitExpr(arg.value);
              }
            } else {
              for (const auto& field : std::get<ast::BraceArgs>(node.args).fields) {
                VisitExpr(field.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::RangeExpr> ||
                               std::is_same_v<T, ast::BinaryExpr> ||
                               std::is_same_v<T, ast::PipelineExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::CastExpr> ||
                               std::is_same_v<T, ast::UnaryExpr> ||
                               std::is_same_v<T, ast::DerefExpr> ||
                               std::is_same_v<T, ast::AllocExpr> ||
                               std::is_same_v<T, ast::TransmuteExpr> ||
                               std::is_same_v<T, ast::PropagateExpr> ||
                               std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::EntryExpr> ||
                               std::is_same_v<T, ast::AttributedExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                               std::is_same_v<T, ast::MoveExpr>) {
            VisitExpr(node.place);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            VisitExpr(elem);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            VisitExpr(elem);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          VisitExpr(node.value);
          VisitExpr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (node.payload_opt.has_value()) {
              if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
                for (const auto& elem :
                     std::get<ast::EnumPayloadParen>(*node.payload_opt).elements) {
                  VisitExpr(elem);
                }
              } else {
                for (const auto& field :
                     std::get<ast::EnumPayloadBrace>(*node.payload_opt).fields) {
                  VisitExpr(field.value);
                }
              }
            }
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee);
            for (const auto& arm : node.cases) {
              VisitExpr(arm.body);
            }
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr>) {
            if (node.block) {
              VisitBlock(*node.block);
            }
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            VisitExpr(node.body);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                               std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base);
            VisitExpr(node.index);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            VisitExpr(node.receiver);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              VisitExpr(arm.expr);
              VisitExpr(arm.handler.value);
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& sub : node.exprs) {
              VisitExpr(sub);
            }
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            VisitExpr(node.domain);
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            VisitExpr(node.handle);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            VisitExpr(node.range);
            if (node.key_clause.has_value()) {
              VisitKeyPath(node.key_clause->key_path);
            }
            for (const auto& opt : node.opts) {
              VisitExpr(opt.chunk_expr);
              VisitExpr(opt.workgroup_expr);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else {
            // Leaf forms carry no nested expressions.
          }
        },
        expr->node);
  }

  bool found_ = false;
};

static bool ContainsSpawnExpr(const ast::ExprPtr& expr) {
  return SpawnExprPresenceFinder{}.Contains(expr);
}

const ast::ExprPtr* StripClosureValueShells(const ast::ExprPtr& expr) {
  if (!expr) {
    return nullptr;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return StripClosureValueShells(attributed->expr);
  }
  if (const auto* moved = std::get_if<ast::MoveExpr>(&expr->node)) {
    return StripClosureValueShells(moved->place);
  }
  return &expr;
}

std::optional<TypeBinding::ClosureCaptureInfo> ClosureCaptureInfoOfValue(
    const ast::ExprPtr& expr,
    const TypeEnv& env,
    const TypeRef& closure_type_hint) {
  const auto* stripped = StripClosureValueShells(expr);
  if (!stripped || !*stripped) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&(*stripped)->node)) {
    const auto binding = BindOf(env, ident->name);
    if (binding.has_value() && binding->closure_capture_info.has_value()) {
      auto info = *binding->closure_capture_info;
      info.has_shared_deps =
          info.has_shared_deps || ClosureTypeHasSharedDeps(binding->type) ||
          ClosureTypeHasSharedDeps(closure_type_hint);
      return info;
    }
    return std::nullopt;
  }
  return AnalyzeClosureCaptureInfo(*stripped, env, closure_type_hint);
}

}  // namespace

bool ClosureTypeHasSharedDeps(const TypeRef& hint) {
  const auto stripped = StripPerm(hint);
  if (!stripped) {
    return false;
  }
  const auto* closure = std::get_if<TypeClosure>(&stripped->node);
  if (!closure) {
    return false;
  }
  return closure->deps_opt.has_value();
}

namespace {

bool ExprNeedsKeyAccessImpl(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::ExprPtr& expr,
                            const TypeEnv& env);
bool BlockNeedsKeyAccessImpl(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::Block& block,
                             const TypeEnv& env);

bool ExprHasSharedPermissionImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::ExprPtr& expr,
                                 const TypeEnv& env) {
  if (!expr) {
    return false;
  }
  const auto place = TypePlace(ctx, type_ctx, expr, env);
  if (place.ok && place.type &&
      PermOfType(place.type) == Permission::Shared) {
    return true;
  }
  const auto value = TypeExpr(ctx, type_ctx, expr, env);
  return value.ok && value.type &&
         PermOfType(value.type) == Permission::Shared;
}

bool StmtNeedsKeyAccessImpl(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::Stmt& stmt,
                            const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.binding.init, env);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          return ExprHasSharedPermissionImpl(ctx, type_ctx, node.place, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.place, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt>) {
          return node.body && BlockNeedsKeyAccessImpl(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.opts_opt, env) ||
                 (node.body && BlockNeedsKeyAccessImpl(ctx, type_ctx, *node.body, env));
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return node.body && BlockNeedsKeyAccessImpl(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.value_opt, env);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          return true;
        }
        return false;
      },
      stmt);
}

bool BlockNeedsKeyAccessImpl(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::Block& block,
                             const TypeEnv& env) {
  for (const auto& stmt : block.stmts) {
    if (StmtNeedsKeyAccessImpl(ctx, type_ctx, stmt, env)) {
      return true;
    }
  }
  return ExprNeedsKeyAccessImpl(ctx, type_ctx, block.tail_opt, env);
}

bool ExprNeedsKeyAccessImpl(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::ExprPtr& expr,
                            const TypeEnv& env) {
  if (!expr) {
    return false;
  }
  if (ExprHasSharedPermissionImpl(ctx, type_ctx, expr, env)) {
    return true;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprNeedsKeyAccessImpl(ctx, type_ctx, node.callee, env)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprNeedsKeyAccessImpl(ctx, type_ctx, arg.value, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprNeedsKeyAccessImpl(ctx, type_ctx, node.receiver, env)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprNeedsKeyAccessImpl(ctx, type_ctx, arg.value, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr> ||
                             std::is_same_v<T, ast::PipelineExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.lhs, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.place, env);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.base, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.base, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.index, env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprNeedsKeyAccessImpl(ctx, type_ctx, elem, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool found = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            found = found || ExprNeedsKeyAccessImpl(ctx, type_ctx, elem, env);
          });
          return found;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.value, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.count, env);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprNeedsKeyAccessImpl(ctx, type_ctx, field.value, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.cond, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.then_expr, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprNeedsKeyAccessImpl(ctx, type_ctx, node.scrutinee, env)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (ExprNeedsKeyAccessImpl(ctx, type_ctx, arm.body, env)) {
              return true;
            }
          }
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprNeedsKeyAccessImpl(ctx, type_ctx, node.scrutinee, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.then_expr, env) ||
                 ExprNeedsKeyAccessImpl(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return node.block && BlockNeedsKeyAccessImpl(ctx, type_ctx, *node.block, env);
        }
        return false;
      },
      expr->node);
}

}  // namespace

bool ExprNeedsKeyAccess(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::ExprPtr& expr,
                        const TypeEnv& env) {
  return ExprNeedsKeyAccessImpl(ctx, type_ctx, expr, env);
}

static bool DeferSafe(const ast::Block& block) {
  return !HasNonLocalCtrlBlock(block, false);
}

std::optional<std::string_view> CheckEscapingClosureSpawn(
    const ast::ExprPtr& expr,
    const TypeEnv& env,
    const TypeRef& expected_closure_type) {
  if (!ClosureTypeHasSharedDeps(expected_closure_type)) {
    return std::nullopt;
  }
  const auto info = ClosureCaptureInfoOfValue(expr, env, expected_closure_type);
  if (!info.has_value() || !info->contains_spawn) {
    return std::nullopt;
  }
  SPEC_RULE("Parallel-Escaping-Closure-Spawn-Err");
  return "E-CON-0131";
}

std::optional<Dim3ConstValue> ExtractDim3Const(const ScopeContext& ctx,
                                               const ast::ExprPtr& expr,
                                               const ExprTypeFn& type_expr) {
  SpecDefsTypeStmt();
  if (!expr) {
    return std::nullopt;
  }
  const auto* tuple = std::get_if<ast::TupleExpr>(&expr->node);
  if (!tuple || tuple->elements.size() != 3) {
    return std::nullopt;
  }

  Dim3ConstValue dims;
  std::uint64_t* out[3] = {&dims.x, &dims.y, &dims.z};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto elem_type = type_expr(tuple->elements[i]);
    if (!elem_type.ok || !IsUsizeType(elem_type.type)) {
      return std::nullopt;
    }
    const auto const_len = ConstLen(ctx, tuple->elements[i]);
    if (!const_len.ok || !const_len.value.has_value() ||
        *const_len.value == 0) {
      return std::nullopt;
    }
    *out[i] = *const_len.value;
  }

  return dims;
}

bool ExceedsMaxWorkgroupSize(const Dim3ConstValue& dims) {
  constexpr std::uint64_t kMaxWorkgroupSize = 1024;
  if (dims.x == 0 || dims.y == 0 || dims.z == 0) {
    return false;
  }
  if (dims.x > kMaxWorkgroupSize) {
    return true;
  }
  const std::uint64_t xy_limit = kMaxWorkgroupSize / dims.x;
  if (dims.y > xy_limit) {
    return true;
  }
  const std::uint64_t xyz_limit = xy_limit / dims.y;
  return dims.z > xyz_limit;
}

TypeEnv PushScope(const TypeEnv& env) {
  SpecDefsTypeStmt();
  TypeEnv out = env;
  out.scopes.emplace_back();
  return out;
}

TypeEnv PopScope(const TypeEnv& env) {
  SpecDefsTypeStmt();
  if (env.scopes.empty()) {
    return env;
  }
  TypeEnv out = env;
  out.scopes.pop_back();
  return out;
}

TypeEnv ProjectTypeEnvToDepth(const TypeEnv& env, std::size_t depth) {
  SpecDefsTypeStmt();
  if (env.scopes.size() <= depth) {
    return env;
  }
  TypeEnv out = env;
  out.scopes.resize(depth);
  return out;
}

StmtTypeResult TypeScopedStmtBody(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::Block& body,
                                  const TypeEnv& scoped_env,
                                  std::size_t outer_scope_depth,
                                  const ExprTypeFn& type_expr,
                                  const IdentTypeFn& type_ident,
                                  const PlaceTypeFn& type_place) {
  SpecDefsTypeStmt();
  TypeEnv body_env;
  const auto info = TypeBlockInfo(ctx, type_ctx, body, scoped_env, type_expr,
                                  type_ident, type_place, &body_env);
  if (!info.ok) {
    return {false, info.diag_id, {}, {}, info.diag_detail, info.diag_span};
  }

  const TypeEnv projected_env =
      ProjectTypeEnvToDepth(body_env, outer_scope_depth);
  if (type_ctx.env_ref) {
    *type_ctx.env_ref = projected_env;
  }
  FlowInfo flow;
  if (IsPrimType(info.type, "!")) {
    flow.results.push_back(info.type);
  }
  flow.breaks = info.breaks;
  flow.break_void = info.break_void;
  return {true, std::nullopt, projected_env, std::move(flow)};
}

std::optional<TypeBinding> BindOf(const TypeEnv& env, std::string_view name) {
  SpecDefsTypeStmt();
  const auto key = IdKeyOf(name);
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    const auto found = it->find(key);
    if (found != it->end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

TypeRef StableBindingType(const TypeBinding& binding) {
  return binding.storage_type ? binding.storage_type : binding.type;
}

std::optional<ast::Mutability> MutOf(const TypeEnv& env,
                                        std::string_view name) {
  SpecDefsTypeStmt();
  const auto binding = BindOf(env, name);
  if (!binding.has_value()) {
    return std::nullopt;
  }
  return binding->mut;
}

std::optional<ParallelContextKind> ParallelContext(const TypeEnv& env) {
  SpecDefsTypeStmt();
  return env.parallel_context;
}

bool GpuContext(const TypeEnv& env) {
  SpecDefsTypeStmt();
  SPEC_RULE("GpuContext");
  return env.parallel_context.has_value() &&
         *env.parallel_context == ParallelContextKind::Gpu;
}

bool HasHeapProvenance(const TypeEnv& env, std::string_view name) {
  SpecDefsTypeStmt();
  SPEC_RULE("HasHeapProvenance");
  const auto binding = BindOf(env, name);
  return binding.has_value() &&
         binding->provenance_kind == BindingProvenanceSeedKind::Heap;
}

bool IsOuterParallelBinding(const StmtTypeContext& type_ctx, const IdKey& name) {
  if (type_ctx.parallel_capture_scopes) {
    for (auto it = type_ctx.parallel_capture_scopes->rbegin();
         it != type_ctx.parallel_capture_scopes->rend();
         ++it) {
      if (it->bindings && it->bindings->find(name) != it->bindings->end()) {
        return true;
      }
    }
    return false;
  }

  if (type_ctx.parallel_bindings &&
      type_ctx.parallel_bindings->find(name) != type_ctx.parallel_bindings->end()) {
    return true;
  }
  return type_ctx.parallel_ancestor_bindings &&
         type_ctx.parallel_ancestor_bindings->find(name) !=
             type_ctx.parallel_ancestor_bindings->end();
}

bool ClaimFirstChildMove(const StmtTypeContext& type_ctx, const IdKey& name) {
  if (type_ctx.parallel_capture_scopes) {
    for (auto it = type_ctx.parallel_capture_scopes->rbegin();
         it != type_ctx.parallel_capture_scopes->rend();
         ++it) {
      if (!it->bindings || !it->first_child_moves) {
        continue;
      }
      if (it->bindings->find(name) == it->bindings->end()) {
        continue;
      }
      return it->first_child_moves->insert(name).second;
    }
    return false;
  }

  if (!type_ctx.parallel_first_child_moves) {
    return false;
  }
  return type_ctx.parallel_first_child_moves->insert(name).second;
}

GpuCaptureCheckResult CheckGpuCapture(const ScopeContext& ctx,
                                      const TypeEnv& env,
                                      std::string_view name,
                                      bool explicit_move) {
  SpecDefsTypeStmt();
  GpuCaptureCheckResult result;

  const auto binding = BindOf(env, name);
  if (!binding.has_value()) {
    return result;
  }

  if (PermOfType(binding->type) == Permission::Shared) {
    SPEC_RULE("GpuCapture-Shared-Err");
    result.diag_id = "E-CON-0151";
    return result;
  }

  if (HasHeapProvenance(env, name)) {
    SPEC_RULE("GpuCapture-HeapProv-Err");
    result.diag_id = "E-CON-0150";
    return result;
  }

  if (const auto gpu_diag = GpuSafeDiagForType(ctx, binding->type);
      gpu_diag.has_value()) {
    SPEC_RULE("GpuCapture-NonGpuSafe-Err");
    result.diag_id = "E-CON-0153";
    result.supplemental_diag_id = *gpu_diag;
    return result;
  }

  if (PermOfType(binding->type) == Permission::Const) {
    SPEC_RULE("GpuCaptureOk-Const");
  } else if (PermOfType(binding->type) == Permission::Unique &&
             explicit_move) {
    SPEC_RULE("GpuCaptureOk-Unique-Move");
  }

  result.ok = true;
  return result;
}

std::optional<TypeBinding::ClosureCaptureInfo> AnalyzeClosureCaptureInfo(
    const ast::ExprPtr& expr,
    const TypeEnv& env,
    const TypeRef& closure_type_hint) {
  const auto capture_sets = AnalyzeClosureCaptureSets(expr, env);
  if (!capture_sets.has_value()) {
    return std::nullopt;
  }

  const auto* closure = std::get_if<ast::ClosureExpr>(&expr->node);
  if (!closure) {
    return std::nullopt;
  }

  TypeBinding::ClosureCaptureInfo info;
  info.captures_any = !capture_sets->captures.empty();
  info.captures_shared = !capture_sets->shared_captures.empty();
  info.has_shared_deps = ClosureTypeHasSharedDeps(closure_type_hint);
  info.contains_spawn = ContainsSpawnExpr(closure->body);
  return info;
}

std::optional<ClosureCaptureSets> AnalyzeClosureCaptureSets(
    const ast::ExprPtr& expr,
    const TypeEnv& env) {
  if (!expr) {
    return std::nullopt;
  }
  const auto* closure = std::get_if<ast::ClosureExpr>(&expr->node);
  if (!closure) {
    return std::nullopt;
  }

  ClosureCaptureSets sets;
  SPEC_RULE("CaptureSet");
  sets.captures = OuterCaptureCollector(env).CollectClosure(*closure);

  for (const auto& captured : sets.captures) {
    const auto binding = BindOf(env, captured);
    if (!binding.has_value()) {
      continue;
    }
    switch (PermOfType(binding->type)) {
      case Permission::Const:
        sets.const_captures.insert(captured);
        break;
      case Permission::Shared:
        sets.shared_captures.insert(captured);
        break;
      case Permission::Unique:
        sets.unique_captures.insert(captured);
        break;
    }
  }

  SPEC_RULE("ConstCaptures");
  SPEC_RULE("SharedCaptures");
  SPEC_RULE("UniqueCaptures");
  return sets;
}

std::optional<ClosureCaptureSets> AnalyzeBlockCaptureSets(
    const ast::Block& block,
    const TypeEnv& env) {
  ClosureCaptureSets sets;
  SPEC_RULE("CaptureSet");
  sets.captures = OuterCaptureCollector(env).CollectBlock(block);

  for (const auto& captured : sets.captures) {
    const auto binding = BindOf(env, captured);
    if (!binding.has_value()) {
      continue;
    }
    switch (PermOfType(binding->type)) {
      case Permission::Const:
        sets.const_captures.insert(captured);
        break;
      case Permission::Shared:
        sets.shared_captures.insert(captured);
        break;
      case Permission::Unique:
        sets.unique_captures.insert(captured);
        break;
    }
  }

  SPEC_RULE("ConstCaptures");
  SPEC_RULE("SharedCaptures");
  SPEC_RULE("UniqueCaptures");
  return sets;
}

static void RecordParallelStmtBindings(const StmtTypeContext& type_ctx,
                                       const ast::Stmt& stmt) {
  if (!type_ctx.parallel_bindings) {
    return;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          if (!node.binding.pat) {
            return;
          }
          std::vector<IdKey> names;
          CollectPatNames(*node.binding.pat, names);
          type_ctx.parallel_bindings->insert(names.begin(), names.end());
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; the alias name still
          // enters the parallel-binding set so references can be resolved.
          type_ctx.parallel_bindings->insert(IdKeyOf(node.alias));
        }
      },
      stmt);
}

StmtSeqResult TypeStmtSeq(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const std::vector<ast::Stmt>& stmts,
                          const TypeEnv& env,
                          const ExprTypeFn& type_expr,
                          const IdentTypeFn& type_ident,
                          const PlaceTypeFn& type_place,
                          TypeEnv* env_ref) {
  SpecDefsTypeStmt();
  StmtSeqResult result;
  if (stmts.empty()) {
    SPEC_RULE("StmtSeq-Empty");
    result.ok = true;
    result.env = env;
    result.flow = {};
    result.proof_ctx = type_ctx.proof_ctx;
    if (type_ctx.env_ref) {
      *type_ctx.env_ref = env;
    }
    if (env_ref) {
      *env_ref = env;
    }
    return result;
  }

  TypeEnv current = env;
  std::shared_ptr<StaticProofContext> current_proof_ctx = type_ctx.proof_ctx;
  if (type_ctx.env_ref) {
    *type_ctx.env_ref = current;
  }
  if (env_ref) {
    *env_ref = current;
  }
  std::vector<TypeRef> res;
  std::vector<TypeRef> brk;
  bool brk_void = false;

  for (const auto& stmt : stmts) {
    StmtTypeContext stmt_ctx = type_ctx;
    stmt_ctx.proof_ctx = current_proof_ctx;
    const auto stmt_fns = MakeFlowTypingFns(ctx, stmt_ctx, current);
    if (type_ctx.env_ref) {
      *type_ctx.env_ref = current;
    }
    if (env_ref) {
      *env_ref = current;
    }
    const auto typed =
        TypeStmt(ctx, stmt_ctx, stmt, current, stmt_fns.type_expr,
                 stmt_fns.type_ident, stmt_fns.type_place, env_ref);
    if (!typed.ok) {
      result.diag_id = typed.diag_id;
      result.diag_detail = typed.diag_detail;
      if (typed.diag_span.has_value()) {
        result.diag_span = typed.diag_span;
      } else {
        result.diag_span = SpanOfStmt(stmt);
      }
      if (env_ref) {
        *env_ref = current;
      }
      return result;
    }
    current = typed.env;
    current_proof_ctx = FallthroughProofContextForStmt(ctx, current_proof_ctx, stmt);
    RecordParallelStmtBindings(type_ctx, stmt);
    if (type_ctx.env_ref) {
      *type_ctx.env_ref = current;
    }
    if (env_ref) {
      *env_ref = current;
    }
    res.insert(res.end(), typed.flow.results.begin(), typed.flow.results.end());
    brk.insert(brk.end(), typed.flow.breaks.begin(), typed.flow.breaks.end());
    brk_void = brk_void || typed.flow.break_void;
    SPEC_RULE("StmtSeq-Cons");
  }

    result.ok = true;
    result.env = std::move(current);
    result.proof_ctx = std::move(current_proof_ctx);
    result.flow.results = std::move(res);
  result.flow.breaks = std::move(brk);
  result.flow.break_void = brk_void;
  if (type_ctx.env_ref) {
    *type_ctx.env_ref = result.env;
  }
  if (env_ref) {
    *env_ref = result.env;
  }
  return result;
}

BlockInfoResult TypeBlockInfo(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::Block& block,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place,
                              TypeEnv* env_ref) {
  SpecDefsTypeStmt();
  BlockInfoResult result;
  const TypeEnv pushed = PushScope(env);
  const auto stmts_typed =
      TypeStmtSeq(ctx, type_ctx, block.stmts, pushed, type_expr,
                  type_ident, type_place, env_ref);
  if (!stmts_typed.ok) {
    result.diag_id = stmts_typed.diag_id;
    result.diag_detail = stmts_typed.diag_detail;
    result.diag_span = stmts_typed.diag_span;
    return result;
  }
  if (!WarnResultUnreachable(block.stmts, type_ctx)) {
    result.diag_id = "BlockInfo-Res-Err";
    return result;
  }

  FlowInfo block_break_flow;
  block_break_flow.breaks = stmts_typed.flow.breaks;
  block_break_flow.break_void = stmts_typed.flow.break_void;
  MergeBreakFlow(block_break_flow,
                 CollectNestedBreakFlowFromBlock(ctx, type_ctx, stmts_typed.env,
                                                 type_expr, block));
  auto apply_break_flow = [&]() {
    result.breaks = block_break_flow.breaks;
    result.break_void = block_break_flow.break_void;
  };

  const auto res_type = ResType(ctx, stmts_typed.flow.results);
  std::optional<ExprTypeResult> tail_type;
  if (block.tail_opt) {
    StmtTypeContext tail_ctx = type_ctx;
    tail_ctx.proof_ctx = stmts_typed.proof_ctx;
    const auto tail_fns = MakeFlowTypingFns(ctx, tail_ctx, stmts_typed.env);
    if (type_ctx.env_ref) {
      *type_ctx.env_ref = stmts_typed.env;
    }
    if (env_ref) {
      *env_ref = stmts_typed.env;
    }
    const auto typed = TypeExprWithEnv(ctx, tail_ctx, stmts_typed.env,
                                       tail_fns.type_expr, block.tail_opt);
    if (!typed.ok) {
      result.diag_id = typed.diag_id;
      result.diag_detail = typed.diag_detail;
      result.diag_span =
          typed.diag_span.has_value() ? typed.diag_span
                                      : std::optional<core::Span>(block.tail_opt->span);
      return result;
    }
    tail_type = typed;
  }

  if (res_type.has_value()) {
    SPEC_RULE("BlockInfo-Res");
    result.ok = true;
    result.type = *res_type;
    apply_break_flow();
    return result;
  }

  if (!stmts_typed.flow.results.empty()) {
    SPEC_RULE("BlockInfo-Res-Err");
    result.diag_id = "BlockInfo-Res-Err";
    return result;
  }

  if (block.tail_opt) {
    if (!tail_type.has_value()) {
      StmtTypeContext tail_ctx = type_ctx;
      tail_ctx.proof_ctx = stmts_typed.proof_ctx;
      const auto tail_fns = MakeFlowTypingFns(ctx, tail_ctx, stmts_typed.env);
      const auto typed = TypeExprWithEnv(ctx, tail_ctx, stmts_typed.env,
                                         tail_fns.type_expr, block.tail_opt);
      if (!typed.ok) {
        result.diag_id = typed.diag_id;
        result.diag_span =
            typed.diag_span.has_value() ? typed.diag_span
                                        : std::optional<core::Span>(block.tail_opt->span);
        return result;
      }
      tail_type = typed;
    }
    SPEC_RULE("BlockInfo-Tail");
    result.ok = true;
    result.type = tail_type->type;
    apply_break_flow();
    return result;
  }

  if (const auto last = LastStmt(block.stmts);
      last.has_value() &&
      std::holds_alternative<ast::ReturnStmt>(last->get())) {
    SPEC_RULE("BlockInfo-ReturnTail");
    result.ok = true;
    result.type = MakeTypePrim("!");
    apply_break_flow();
    return result;
  }

  SPEC_RULE("BlockInfo-Unit");
  result.ok = true;
  result.type = MakeTypePrim("()");
  apply_break_flow();
  return result;
}

ExprTypeResult TypeBlock(const ScopeContext& ctx,
                         const StmtTypeContext& type_ctx,
                         const ast::Block& block,
                         const TypeEnv& env,
                         const ExprTypeFn& type_expr,
                         const IdentTypeFn& type_ident,
                         const PlaceTypeFn& type_place,
                         TypeEnv* env_ref) {
  SpecDefsTypeStmt();
  ExprTypeResult result;
  const auto info = TypeBlockInfo(ctx, type_ctx, block, env, type_expr,
                                  type_ident, type_place, env_ref);
  if (!info.ok) {
    result.diag_id = info.diag_id;
    result.diag_detail = info.diag_detail;
    result.diag_span = info.diag_span;
    return result;
  }
  SPEC_RULE("T-Block");
  result.ok = true;
  result.type = info.type;
  return result;
}

CheckResult CheckBlock(const ScopeContext& ctx,
                       const StmtTypeContext& type_ctx,
                       const ast::Block& block,
                       const TypeEnv& env,
                       const TypeRef& expected,
                       const ExprTypeFn& type_expr,
                       const IdentTypeFn& type_ident,
                       const PlaceTypeFn& type_place,
                       TypeEnv* env_ref) {
  SpecDefsTypeStmt();
  CheckResult result;
  if (!expected) {
    return result;
  }

  const TypeEnv pushed = PushScope(env);
  const auto stmts_typed =
      TypeStmtSeq(ctx, type_ctx, block.stmts, pushed, type_expr,
                  type_ident, type_place, env_ref);
  if (!stmts_typed.ok) {
    result.diag_id = stmts_typed.diag_id;
    result.diag_span = stmts_typed.diag_span;
    return result;
  }
  if (!WarnResultUnreachable(block.stmts, type_ctx)) {
    return result;
  }

  const auto res_type = ResType(ctx, stmts_typed.flow.results);
  if (res_type.has_value()) {
    return result;
  }

  if (block.tail_opt) {
    StmtTypeContext tail_ctx = type_ctx;
    tail_ctx.proof_ctx = stmts_typed.proof_ctx;
    if (type_ctx.env_ref) {
      *type_ctx.env_ref = stmts_typed.env;
    }
    if (env_ref) {
      *env_ref = stmts_typed.env;
    }
    const auto check = CheckExprAgainst(ctx, tail_ctx, block.tail_opt, expected,
                                        stmts_typed.env);
    if (check.ok) {
      SPEC_RULE("Chk-Block-Tail");
      result.ok = true;
      return result;
    }
    result.diag_id = check.diag_id;
    result.diag_span =
        check.diag_span.has_value() ? check.diag_span
                                    : std::optional<core::Span>(block.tail_opt->span);
    return result;
  }

  if (const auto last = LastStmt(block.stmts);
      last.has_value() &&
      std::holds_alternative<ast::ReturnStmt>(last->get())) {
    SPEC_RULE("Chk-Block-Return");
    result.ok = true;
    return result;
  }

  if (IsPrimType(expected, "()")) {
    SPEC_RULE("Chk-Block-Unit");
    result.ok = true;
    return result;
  }
  return result;
}

// =============================================================================
// TypePattern - Irrefutable pattern typing for let/var bindings
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 5.4 Pattern Matching
//   - Irrefutable patterns are used in let/var bindings
//   - They must cover all possible values of the expected type
//
// =============================================================================

PatternTypeResult TypePattern(const ScopeContext& ctx,
                              const ast::PatternPtr& pattern,
                              const TypeRef& expected) {
  SpecDefsTypeStmt();
  PatternTypeResult result;
  if (!pattern || !expected) {
    return result;
  }

  return std::visit(
      [&](const auto& node) -> PatternTypeResult {
        using T = std::decay_t<decltype(node)>;

        // Wildcard Pattern - irrefutable
        if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
          SPEC_RULE("Pat-Wildcard");
          return {true, std::nullopt, {}};
        }

        // Identifier Pattern - irrefutable, binds expected type
        else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          SPEC_RULE("Pat-Ident");
          return {true, std::nullopt, {{std::string(node.name), expected}}};
        }

        // Typed Pattern - may be used for type narrowing
        else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          const auto lowered = LowerType(ctx, node.type);
          if (!lowered.ok) {
            return {false, lowered.diag_id, {}};
          }
          // Check equivalence with expected
          const auto equiv = TypeEquiv(lowered.type, expected);
          if (!equiv.ok) {
            return {false, equiv.diag_id, {}};
          }
          if (!equiv.equiv) {
            // Allow subtyping check
            const auto sub = Subtyping(ctx, lowered.type, expected);
            if (!sub.ok || !sub.subtype) {
              SPEC_RULE("Pat-Typed-Err");
              return {false, "Pat-Typed-Err", {}};
            }
          }
          SPEC_RULE("Pat-Typed");
          if (node.name == "_") {
            return {true, std::nullopt, {}};
          }
          return {true, std::nullopt, {{std::string(node.name), lowered.type}}};
        }

        // Tuple Pattern - irrefutable if all elements are
        else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          if (node.elements.empty()) {
            if (IsPrimType(expected, "()")) {
              SPEC_RULE("Pat-Unit");
              return {true, std::nullopt, {}};
            }
            return {false, std::nullopt, {}};
          }
          const auto* tuple = std::get_if<TypeTuple>(&expected->node);
          if (!tuple) {
            return {false, std::nullopt, {}};
          }
          if (tuple->elements.size() != node.elements.size()) {
            SPEC_RULE("Pat-Tuple-Arity-Err");
            return {false, "Pat-Tuple-Arity-Err", {}};
          }
          std::vector<std::pair<std::string, TypeRef>> binds;
          for (std::size_t i = 0; i < node.elements.size(); ++i) {
            const auto sub = TypePattern(ctx, node.elements[i], tuple->elements[i]);
            if (!sub.ok) {
              return sub;
            }
            binds.insert(binds.end(), sub.bindings.begin(), sub.bindings.end());
          }
          SPEC_RULE("Pat-Tuple");
          return {true, std::nullopt, std::move(binds)};
        }

        // Record Pattern - irrefutable for record types
        else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          const auto* path_type = std::get_if<TypePathType>(&expected->node);
          if (!path_type) {
            return {false, std::nullopt, {}};
          }
          // Look up the record declaration
          const auto decl_it = ctx.sigma.types.find(PathKeyOf(node.path));
          if (decl_it == ctx.sigma.types.end()) {
            return {false, std::nullopt, {}};
          }
          const auto* record = std::get_if<ast::RecordDecl>(&decl_it->second);
          if (!record) {
            return {false, std::nullopt, {}};
          }
          std::vector<std::pair<std::string, TypeRef>> binds;
          for (const auto& field : node.fields) {
            // Find field in record
            const ast::FieldDecl* field_decl = nullptr;
            for (const auto& member : record->members) {
              const auto* f = std::get_if<ast::FieldDecl>(&member);
              if (f && IdEq(f->name, field.name)) {
                field_decl = f;
                break;
              }
            }
            if (!field_decl) {
              return {false, "RecordPattern-UnknownField", {}};
            }
            const auto field_type = LowerType(ctx, field_decl->type);
            if (!field_type.ok) {
              return {false, field_type.diag_id, {}};
            }
            ast::PatternPtr pat = field.pattern_opt;
            if (!pat) {
              // Shorthand: field name becomes binding
              auto implicit = std::make_shared<ast::Pattern>();
              implicit->node = ast::IdentifierPattern{field.name};
              pat = implicit;
            }
            const auto sub = TypePattern(ctx, pat, field_type.type);
            if (!sub.ok) {
              return sub;
            }
            binds.insert(binds.end(), sub.bindings.begin(), sub.bindings.end());
          }
          SPEC_RULE("Pat-Record");
          return {true, std::nullopt, std::move(binds)};
        }

        else {
          // Other patterns (Literal, Enum, Modal, Range) are refutable
          // and cannot be used in let/var bindings
          SPEC_RULE("Let-Refutable-Pattern-Err");
          return {false, "Let-Refutable-Pattern-Err", {}};
        }
      },
      pattern->node);
}

// =============================================================================
// Forward declarations for individual statement typing functions
// =============================================================================

// Each individual stmt file implements its own TypeXxxStmt function.
// These are declared here for the dispatcher.

StmtTypeResult TypeLetStmt(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::LetStmt& stmt,
                           const TypeEnv& env,
                           const ExprTypeFn& type_expr,
                           const IdentTypeFn& type_ident,
                           const PlaceTypeFn& type_place);

StmtTypeResult TypeVarStmt(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::VarStmt& stmt,
                           const TypeEnv& env,
                           const ExprTypeFn& type_expr,
                           const IdentTypeFn& type_ident,
                           const PlaceTypeFn& type_place);

StmtTypeResult TypeUsingLocalStmt(const ast::UsingLocalStmt& stmt,
                                  const TypeEnv& env);

StmtTypeResult TypeAssignStmt(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::AssignStmt& stmt,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place);

StmtTypeResult TypeCompoundAssignStmt(const ScopeContext& ctx,
                                      const StmtTypeContext& type_ctx,
                                      const ast::CompoundAssignStmt& stmt,
                                      const TypeEnv& env,
                                      const ExprTypeFn& type_expr,
                                      const IdentTypeFn& type_ident,
                                      const PlaceTypeFn& type_place);

StmtTypeResult TypeReturnStmt(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::ReturnStmt& stmt,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place);

StmtTypeResult TypeBreakStmt(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::BreakStmt& stmt,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr);

StmtTypeResult TypeContinueStmt(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::ContinueStmt& stmt,
                                const TypeEnv& env);

StmtTypeResult TypeExprStmt(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::ExprStmt& stmt,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr);

StmtTypeResult TypeDeferStmt(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::DeferStmt& stmt,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place);

StmtTypeResult TypeRegionStmt(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::RegionStmt& stmt,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place);

StmtTypeResult TypeFrameStmt(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::FrameStmt& stmt,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place);

StmtTypeResult TypeUnsafeBlockStmt(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::UnsafeBlockStmt& stmt,
                                   const TypeEnv& env,
                                   const ExprTypeFn& type_expr,
                                   const IdentTypeFn& type_ident,
                                   const PlaceTypeFn& type_place);

StmtTypeResult TypeKeyBlockStmt(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::KeyBlockStmt& stmt,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const IdentTypeFn& type_ident,
                                const PlaceTypeFn& type_place);

StmtTypeResult TypeErrorStmt(const ScopeContext& ctx,
                             const ast::ErrorStmt& stmt,
                             const TypeEnv& env);

// =============================================================================
// TypeStmt - Statement typing dispatcher
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 5.2.11 Statement Typing
//   - Dispatches to appropriate statement-specific typing function
//
// =============================================================================

StmtTypeResult TypeStmt(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::Stmt& stmt,
                        const TypeEnv& env,
                        const ExprTypeFn& type_expr,
                        const IdentTypeFn& type_ident,
                        const PlaceTypeFn& type_place,
                        TypeEnv* env_ref) {
  SpecDefsTypeStmt();

  return std::visit(
      [&](const auto& node) -> StmtTypeResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          SPEC_RULE("T-LetStmt");
          return TypeLetStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          SPEC_RULE("T-VarStmt");
          return TypeVarStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          SPEC_RULE("T-UsingLocalStmt");
          return TypeUsingLocalStmt(node, env);
        }
        else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          SPEC_RULE("T-AssignStmt");
          return TypeAssignStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          SPEC_RULE("T-CompoundAssignStmt");
          return TypeCompoundAssignStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          SPEC_RULE("T-ReturnStmt");
          return TypeReturnStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          SPEC_RULE("T-BreakStmt");
          return TypeBreakStmt(ctx, type_ctx, node, env, type_expr);
        }
        else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          SPEC_RULE("T-ContinueStmt");
          return TypeContinueStmt(ctx, type_ctx, node, env);
        }
        else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          SPEC_RULE("T-ExprStmt");
          return TypeExprStmt(ctx, type_ctx, node, env, type_expr);
        }
        else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          SPEC_RULE("T-DeferStmt");
          return TypeDeferStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          SPEC_RULE("T-RegionStmt");
          return TypeRegionStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          SPEC_RULE("T-FrameStmt");
          return TypeFrameStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          SPEC_RULE("T-UnsafeBlockStmt");
          return TypeUnsafeBlockStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::CtStmt>) {
          return TypeCtStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          SPEC_RULE("T-KeyBlockStmt");
          return TypeKeyBlockStmt(ctx, type_ctx, node, env, type_expr, type_ident, type_place);
        }
        else if constexpr (std::is_same_v<T, ast::ErrorStmt>) {
          SPEC_RULE("T-ErrorStmt");
          return TypeErrorStmt(ctx, node, env);
        }
        else {
          // Unknown statement type
          return {false, "Unknown-Stmt-Type", env, {}};
        }
      },
      stmt);
}

}  // namespace cursive::analysis
