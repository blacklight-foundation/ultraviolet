// =============================================================================
// resolve_expr.cpp - Expression Resolution Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §5.1.3 "Lookup" (Lines 6822-6899)
//   CursiveSpecification.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//
// SOURCE FILE:
//   cursive-bootstrap/src/03_analysis/resolve/resolver_expr.cpp (Lines 1-1349)
//
// MIGRATED: 2026-02-01
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/language_service/facts.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/resolve_qual.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_intro.h"
#include "04_analysis/resolve/scope_overrides.h"
#include "04_analysis/resolve/visibility.h"

namespace cursive::analysis {

ResolveResult<std::optional<ast::LoopInvariant>> ResolveInvariantOpt(
    ResolveContext& ctx,
    const std::optional<ast::LoopInvariant>& invariant_opt);

namespace {

bool IsComptimeResolutionEnv(const ScopeContext& ctx) {
  const auto has = [&](std::string_view name) {
    return ResolveValueName(ctx, name).has_value();
  };
  return has("diagnostics") || has("introspect") || has("emitter") ||
         has("files") || has("target");
}

Scope BuildComptimeCapabilityScope(const ast::AttributeList* attrs) {
  Scope scope;
  const auto add = [&](std::string_view name) {
    scope.emplace(IdKeyOf(name), Entity{EntityKind::Value, std::nullopt, std::nullopt,
                                        EntitySource::Decl});
  };
  add("introspect");
  add("diagnostics");
  if (attrs && HasAttribute(*attrs, attrs::kFiles)) {
    add("files");
  }
  if (attrs && HasAttribute(*attrs, attrs::kEmit)) {
    add("emitter");
  }
  return scope;
}

ResExprResult ResolveComptimeExprWithAttrs(ResolveContext& ctx,
                                           const ast::ExprPtr& expr,
                                           const ast::ComptimeExpr& node,
                                           const ast::AttributeList* attrs) {
  if (!node.body) {
    return {true, std::nullopt, std::nullopt, expr};
  }
  ast::AttributeList merged_attrs = ast::AttrListOf(node.attrs_opt);
  if (attrs) {
    merged_attrs.insert(merged_attrs.end(), attrs->begin(), attrs->end());
  }
  ScopedLeadingScope comptime_scope(*ctx.ctx);
  comptime_scope.scope() = BuildComptimeCapabilityScope(&merged_attrs);
  const auto body = ResolveExpr(ctx, node.body);
  if (!body.ok) {
    return {false, body.diag_id, body.span, {},
            body.diag_detail, body.diag_children};
  }
  auto out = *expr;
  auto& out_node = std::get<ast::ComptimeExpr>(out.node);
  out_node.body = body.value;
  out_node.attrs_opt = std::move(merged_attrs);
  SPEC_RULE("ResolveExpr-CtExpr");
  return {true, std::nullopt, std::nullopt,
          std::make_shared<ast::Expr>(std::move(out))};
}

// ============================================================================
// Edit distance for "did you mean" suggestions
// ============================================================================

// Compute Levenshtein edit distance between two strings.
// Returns early if distance exceeds max_dist (for performance).
static std::size_t EditDistance(std::string_view a, std::string_view b,
                               std::size_t max_dist = 4) {
  if (a.size() > b.size()) std::swap(a, b);
  if (b.size() - a.size() > max_dist) return max_dist + 1;

  std::vector<std::size_t> row(a.size() + 1);
  for (std::size_t i = 0; i <= a.size(); ++i) row[i] = i;

  for (std::size_t j = 1; j <= b.size(); ++j) {
    std::size_t prev = row[0];
    row[0] = j;
    std::size_t row_min = row[0];
    for (std::size_t i = 1; i <= a.size(); ++i) {
      const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      const std::size_t val = std::min({row[i] + 1,
                                         row[i - 1] + 1,
                                         prev + cost});
      prev = row[i];
      row[i] = val;
      if (val < row_min) row_min = val;
    }
    if (row_min > max_dist) return max_dist + 1;
  }
  return row[a.size()];
}

// Find the closest matching name from visible scopes.
// Returns empty string if no good match found.
static std::string SuggestName(const ScopeContext& ctx,
                               std::string_view name) {
  // Threshold: allow up to 1/3 of the name length in edits, minimum 1, max 3
  const std::size_t threshold =
      std::max<std::size_t>(1, std::min<std::size_t>(3, name.size() / 3));

  std::string best;
  std::size_t best_dist = threshold + 1;

  for (const auto& scope : ctx.scopes) {
    for (const auto& [key, entity] : scope) {
      if (key == name) continue;  // exact match already handled
      const auto dist = EditDistance(name, key, threshold);
      if (dist < best_dist) {
        best_dist = dist;
        best = key;
      }
    }
  }

  return best;
}

static inline void SpecDefsResolverExpr() {
  SPEC_DEF("ResolveExpr", "5.1.7");
  SPEC_DEF("ResolveStmt", "5.1.7");
  SPEC_DEF("ResolveStmtSeq", "5.1.7");
  SPEC_DEF("ResolveExprList", "5.1.7");
  SPEC_DEF("ResolveEnumPayload", "5.1.7");
  SPEC_DEF("ResolveKeySeg", "5.1.7");
  SPEC_DEF("ResolveKeySegs", "5.1.7");
  SPEC_DEF("ResolveKeyPathExpr", "5.1.7");
  SPEC_DEF("ResolveKeyPathList", "5.1.7");
  SPEC_DEF("ResolveKeyClauseOpt", "5.1.7");
  SPEC_DEF("ResolveCallee", "5.1.7");
  SPEC_DEF("ResolveIfCase", "5.1.7");
  SPEC_DEF("BindNames", "5.1.7");
  SPEC_DEF("BindPattern", "5.1.7");
  SPEC_DEF("ResolveExprOpt", "5.1.7");
  SPEC_DEF("ResolveTypeOpt", "5.1.7");
  SPEC_DEF("ResolveTypeRef", "5.1.7");
}

struct ScopeGuard {
  ScopeContext* ctx = nullptr;
  bool active = false;

  explicit ScopeGuard(ScopeContext& ctx_in) : ctx(&ctx_in), active(true) {
    ctx->scopes.insert(ctx->scopes.begin(), Scope{});
  }

  ~ScopeGuard() {
    if (active && ctx && !ctx->scopes.empty()) {
      ctx->scopes.erase(ctx->scopes.begin());
    }
  }

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
};

ast::ExprPtr MakeExpr(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

ast::Path FullPath(const ast::Path& path, std::string_view name) {
  ast::Path out = path;
  out.emplace_back(name);
  return out;
}

ResolveQualContext BuildResolveQualContext(ResolveContext& ctx) {
  ResolveQualContext qual_ctx;
  qual_ctx.ctx = ctx.ctx;
  qual_ctx.name_maps = ctx.name_maps;
  qual_ctx.module_names = ctx.module_names;
  qual_ctx.can_access = ctx.can_access;
  qual_ctx.language_service = ctx.language_service;
  qual_ctx.resolve_expr = [](const ScopeContext& qctx,
                             const NameMapTable& name_maps,
                             const source::ModuleNames& module_names,
                             LanguageServiceIndex* language_service,
                             const ast::ExprPtr& expr) {
    ResolveContext local_ctx;
    local_ctx.ctx = const_cast<ScopeContext*>(&qctx);
    local_ctx.name_maps = &name_maps;
    local_ctx.module_names = &module_names;
    local_ctx.can_access = CanAccess;
    local_ctx.language_service = language_service;
    const auto resolved = ResolveExpr(local_ctx, expr);
    return ResolveExprResult{resolved.ok, resolved.diag_id, resolved.value};
  };
  qual_ctx.resolve_type_path = [](const ScopeContext& qctx,
                                  const NameMapTable& name_maps,
                                  const source::ModuleNames& module_names,
                                  const ast::TypePath& path) {
    ResolveContext local_ctx;
    local_ctx.ctx = const_cast<ScopeContext*>(&qctx);
    local_ctx.name_maps = &name_maps;
    local_ctx.module_names = &module_names;
    local_ctx.can_access = CanAccess;
    const auto resolved = ResolveTypePath(local_ctx, path);
    return ResolveTypePathResult{resolved.ok, resolved.diag_id, resolved.value};
  };
  return qual_ctx;
}

struct BindNamesResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

BindNamesResult BindNames(ResolveContext& ctx,
                          const std::vector<ast::Identifier>& names,
                          const std::optional<core::Span>& span) {
  if (!ctx.ctx) {
    return {false, std::nullopt, span};
  }
  for (const auto& name : names) {
    Entity entity{EntityKind::Value, std::nullopt, std::nullopt,
                  EntitySource::Decl};
    if (span.has_value()) {
      entity = MakeLanguageServiceLocalEntity(
          ctx.language_service, *ctx.ctx, name, *span,
          LanguageSymbolKind::Variable, "local binding");
    }
    const auto res = Intro(*ctx.ctx, name, entity);
    if (!res.ok) {
      return {false, res.diag_id, span};
    }
  }
  SPEC_RULE("BindNames-Cons");
  return {true, std::nullopt, span};
}

BindNamesResult BindPattern(ResolveContext& ctx,
                            const ast::PatternPtr& pat) {
  if (!pat) {
    return {true, std::nullopt, std::nullopt};
  }
  const auto names = PatNames(pat);
  if (names.empty()) {
    SPEC_RULE("BindNames-Empty");
    return {true, std::nullopt, pat->span};
  }
  std::unordered_set<IdKey> seen;
  for (const auto& name : names) {
    const auto key = IdKeyOf(name);
    if (!seen.insert(key).second) {
      SPEC_RULE("Pat-Dup-Err");
      return {false, "Pat-Dup-Err", pat->span};
    }
  }
  const auto result = BindNames(ctx, names, pat->span);
  if (result.ok) {
    SPEC_RULE("BindPattern");
  }
  return result;
}

ResExprResult ResolveExprOpt(ResolveContext& ctx,
                            const ast::ExprPtr& expr_opt) {
  if (!expr_opt) {
    SPEC_RULE("ResolveExprOpt-None");
    return {true, std::nullopt, std::nullopt, expr_opt};
  }
  const auto resolved = ResolveExpr(ctx, expr_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {},
            resolved.diag_detail, resolved.diag_children};
  }
  SPEC_RULE("ResolveExprOpt-Some");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

ResTypeResult ResolveTypeOpt(ResolveContext& ctx,
                             const std::shared_ptr<ast::Type>& type_opt) {
  if (!type_opt) {
    SPEC_RULE("ResolveTypeOpt-None");
    return {true, std::nullopt, std::nullopt, type_opt};
  }
  const auto resolved = ResolveType(ctx, type_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }
  SPEC_RULE("ResolveTypeOpt-Some");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

ResolveResult<std::vector<ast::Arg>> ResolveArgs(ResolveContext& ctx,
                                                    const std::vector<ast::Arg>& args) {
  ResolveResult<std::vector<ast::Arg>> result;
  result.ok = true;
  if (args.empty()) {
    SPEC_RULE("ResolveExprList-Empty");
    return result;
  }
  result.value.reserve(args.size());
  for (const auto& arg : args) {
    const auto resolved = ResolveExpr(ctx, arg.value);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    ast::Arg next = arg;
    next.value = resolved.value;
    result.value.push_back(std::move(next));
    SPEC_RULE("ResolveExprList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::FieldInit>> ResolveFieldInits(
    ResolveContext& ctx,
    const std::vector<ast::FieldInit>& fields) {
  ResolveResult<std::vector<ast::FieldInit>> result;
  result.ok = true;
  result.value.reserve(fields.size());
  for (const auto& field : fields) {
    const auto resolved = ResolveExpr(ctx, field.value);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    ast::FieldInit next = field;
    next.value = resolved.value;
    result.value.push_back(std::move(next));
  }
  return result;
}

ResolveResult<std::optional<ast::EnumPayload>> ResolveEnumPayload(
    ResolveContext& ctx,
    const std::optional<ast::EnumPayload>& payload_opt) {
  ResolveResult<std::optional<ast::EnumPayload>> result;
  result.ok = true;
  if (!payload_opt.has_value()) {
    SPEC_RULE("ResolveEnumPayload-None");
    return result;
  }
  return std::visit(
      [&](const auto& node) -> ResolveResult<std::optional<ast::EnumPayload>> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::EnumPayloadParen>) {
          std::vector<ast::ExprPtr> elems;
          elems.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            const auto resolved = ResolveExpr(ctx, elem);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, std::nullopt,
                      resolved.diag_detail, resolved.diag_children};
            }
            elems.push_back(resolved.value);
          }
          ast::EnumPayloadParen out;
          out.elements = std::move(elems);
          SPEC_RULE("ResolveEnumPayload-Tuple");
          return {true, std::nullopt, std::nullopt, out};
        } else {
          const auto fields = ResolveFieldInits(ctx, node.fields);
          if (!fields.ok) {
            return {false, fields.diag_id, fields.span, std::nullopt,
                    fields.diag_detail, fields.diag_children};
          }
          ast::EnumPayloadBrace out;
          out.fields = fields.value;
          SPEC_RULE("ResolveEnumPayload-Record");
          return {true, std::nullopt, std::nullopt, out};
        }
      },
      *payload_opt);
}

template <typename T>
ResolveResult<T> MakeUnresolvedValueNameResult(const ScopeContext& ctx,
                                               std::string_view name,
                                               const core::Span& span) {
  ResolveResult<T> err;
  err.ok = false;
  err.diag_id = "ResolveExpr-Ident-Err";
  err.span = span;
  err.diag_detail = "unresolved name '" + std::string(name) + "'";

  const auto suggestion = SuggestName(ctx, name);
  if (!suggestion.empty()) {
    err.diag_children.push_back(
        {core::SubDiagnosticKind::Help,
         "did you mean `" + suggestion + "`?",
         span,
         suggestion});
  }

  if (name == "Option" || name == "Some" || name == "None") {
    err.diag_children.push_back(
        {core::SubDiagnosticKind::Help,
         "Cursive uses union types (`T | ()`) instead of Option",
         {},
         {}});
  } else if (name == "Result" || name == "Ok" || name == "Err") {
    err.diag_children.push_back(
        {core::SubDiagnosticKind::Help,
         "Cursive uses union types (`T | E`) instead of Result",
         {},
         {}});
  } else if (name == "Vec") {
    err.diag_children.push_back(
        {core::SubDiagnosticKind::Help,
         "Cursive uses array types `[T; n]` or slice types `[T]`",
         {},
         {}});
  } else if (name == "Box" || name == "Rc" || name == "Arc") {
    err.diag_children.push_back(
        {core::SubDiagnosticKind::Help,
         "Cursive uses `region` and `Ptr<T>` for heap allocation",
         {},
         {}});
  } else if (name == "String") {
    err.diag_children.push_back(
        {core::SubDiagnosticKind::Help,
         "Cursive uses `string@View` and `string@Managed`",
         {},
         {}});
  } else if (name != "self" && name != "Self" &&
             (name == "println" || name == "print" ||
              name == "eprintln" || name == "eprint")) {
    err.diag_children.push_back(
        {core::SubDiagnosticKind::Help,
         "Cursive does not have `" + std::string(name) +
             "!`; use capabilities from Context for I/O",
         {},
         {}});
  }

  return err;
}

ResolveResult<ast::KeySeg> ResolveKeySeg(ResolveContext& ctx,
                                         const ast::KeySeg& seg) {
  return std::visit(
      [&](const auto& node) -> ResolveResult<ast::KeySeg> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::KeySegField>) {
          SPEC_RULE("ResolveKeySeg-Field");
          return {true, std::nullopt, std::nullopt, seg};
        } else {
          const auto resolved_expr = ResolveExpr(ctx, node.expr);
          if (!resolved_expr.ok) {
            return {false, resolved_expr.diag_id, resolved_expr.span, {},
                    resolved_expr.diag_detail, resolved_expr.diag_children};
          }
          auto out = node;
          out.expr = resolved_expr.value;
          SPEC_RULE("ResolveKeySeg-Index");
          return {true, std::nullopt, std::nullopt, out};
        }
      },
      seg);
}

ResolveResult<std::vector<ast::KeySeg>> ResolveKeySegs(
    ResolveContext& ctx,
    const std::vector<ast::KeySeg>& segs) {
  ResolveResult<std::vector<ast::KeySeg>> result;
  result.ok = true;
  if (segs.empty()) {
    SPEC_RULE("ResolveKeySegs-Empty");
    return result;
  }

  result.value.reserve(segs.size());
  for (const auto& seg : segs) {
    const auto resolved_seg = ResolveKeySeg(ctx, seg);
    if (!resolved_seg.ok) {
      return {false, resolved_seg.diag_id, resolved_seg.span, {},
              resolved_seg.diag_detail, resolved_seg.diag_children};
    }
    result.value.push_back(resolved_seg.value);
    SPEC_RULE("ResolveKeySegs-Cons");
  }
  return result;
}

ResolveResult<ast::KeyPathExpr> ResolveKeyPathExpr(ResolveContext& ctx,
                                                   const ast::KeyPathExpr& path) {
  if (!ctx.ctx) {
    return {false, "ResolveExpr-Ident-Err", path.span, {}};
  }

  const auto ent = ResolveValueName(*ctx.ctx, path.root);
  if (!ent.has_value()) {
    SPEC_RULE("ResolveKeyPathExpr-Err");
    return MakeUnresolvedValueNameResult<ast::KeyPathExpr>(*ctx.ctx, path.root,
                                                           path.span);
  }
  RecordLanguageServiceReference(ctx.language_service, *ctx.ctx, path.root,
                                 path.span, *ent);

  const auto resolved_segs = ResolveKeySegs(ctx, path.segs);
  if (!resolved_segs.ok) {
    return {false, resolved_segs.diag_id, resolved_segs.span, {},
            resolved_segs.diag_detail, resolved_segs.diag_children};
  }

  auto out = path;
  out.segs = std::move(resolved_segs.value);
  SPEC_RULE("ResolveKeyPathExpr");
  return {true, std::nullopt, std::nullopt, std::move(out)};
}

ResolveResult<std::vector<ast::KeyPathExpr>> ResolveKeyPathList(
    ResolveContext& ctx,
    const std::vector<ast::KeyPathExpr>& paths) {
  ResolveResult<std::vector<ast::KeyPathExpr>> result;
  result.ok = true;
  if (paths.empty()) {
    SPEC_RULE("ResolveKeyPathList-Empty");
    return result;
  }

  result.value.reserve(paths.size());
  for (const auto& path : paths) {
    const auto resolved_path = ResolveKeyPathExpr(ctx, path);
    if (!resolved_path.ok) {
      return {false, resolved_path.diag_id, resolved_path.span, {},
              resolved_path.diag_detail, resolved_path.diag_children};
    }
    result.value.push_back(resolved_path.value);
    SPEC_RULE("ResolveKeyPathList-Cons");
  }
  return result;
}

ResolveResult<std::optional<ast::DispatchKeyClause>> ResolveKeyClauseOpt(
    ResolveContext& ctx,
    const std::optional<ast::DispatchKeyClause>& key_clause_opt) {
  ResolveResult<std::optional<ast::DispatchKeyClause>> result;
  result.ok = true;
  if (!key_clause_opt.has_value()) {
    SPEC_RULE("ResolveKeyClauseOpt-None");
    return result;
  }

  auto out = *key_clause_opt;
  const auto resolved_path = ResolveKeyPathExpr(ctx, out.key_path);
  if (!resolved_path.ok) {
    return {false, resolved_path.diag_id, resolved_path.span, std::nullopt,
            resolved_path.diag_detail, resolved_path.diag_children};
  }
  out.key_path = std::move(resolved_path.value);
  SPEC_RULE("ResolveKeyClauseOpt-Yes");
  return {true, std::nullopt, std::nullopt, std::move(out)};
}

ResolveResult<ast::SpawnOption> ResolveSpawnOpt(ResolveContext& ctx,
                                                const ast::SpawnOption& opt) {
  if (!opt.value || opt.kind == ast::SpawnOptionKind::Name) {
    SPEC_RULE("ResolveSpawnOpt-Name");
    return {true, std::nullopt, std::nullopt, opt};
  }

  const auto resolved_value = ResolveExpr(ctx, opt.value);
  if (!resolved_value.ok) {
    return {false, resolved_value.diag_id, resolved_value.span, {},
            resolved_value.diag_detail, resolved_value.diag_children};
  }

  auto out = opt;
  out.value = resolved_value.value;
  if (opt.kind == ast::SpawnOptionKind::Affinity) {
    SPEC_RULE("ResolveSpawnOpt-Affinity");
  } else {
    SPEC_RULE("ResolveSpawnOpt-Priority");
  }
  return {true, std::nullopt, std::nullopt, std::move(out)};
}

ResolveResult<std::vector<ast::SpawnOption>> ResolveSpawnOpts(
    ResolveContext& ctx,
    const std::vector<ast::SpawnOption>& opts) {
  ResolveResult<std::vector<ast::SpawnOption>> result;
  result.ok = true;
  result.value.reserve(opts.size());
  if (opts.empty()) {
    SPEC_RULE("ResolveSpawnOpts-Empty");
    return result;
  }

  for (const auto& opt : opts) {
    const auto resolved_opt = ResolveSpawnOpt(ctx, opt);
    if (!resolved_opt.ok) {
      return {false, resolved_opt.diag_id, resolved_opt.span, {},
              resolved_opt.diag_detail, resolved_opt.diag_children};
    }
    result.value.push_back(resolved_opt.value);
    SPEC_RULE("ResolveSpawnOpts-Cons");
  }
  return result;
}

ResolveResult<ast::ParallelOption> ResolveParallelOpt(
    ResolveContext& ctx,
    const ast::ParallelOption& opt) {
  if (opt.kind == ast::ParallelOptionKind::Name) {
    SPEC_RULE("ResolveParallelOpt-Name");
    return {true, std::nullopt, std::nullopt, opt};
  }

  if (!opt.value) {
    if (opt.kind == ast::ParallelOptionKind::Cancel) {
      SPEC_RULE("ResolveParallelOpt-Cancel");
    } else if (opt.kind == ast::ParallelOptionKind::Workgroup) {
      SPEC_RULE("ResolveParallelOpt-Workgroup");
    } else {
      SPEC_RULE("ResolveParallelOpt-Workgroups");
    }
    return {true, std::nullopt, std::nullopt, opt};
  }

  const auto resolved_value = ResolveExpr(ctx, opt.value);
  if (!resolved_value.ok) {
    return {false, resolved_value.diag_id, resolved_value.span, {},
            resolved_value.diag_detail, resolved_value.diag_children};
  }

  auto out = opt;
  out.value = resolved_value.value;
  if (opt.kind == ast::ParallelOptionKind::Cancel) {
    SPEC_RULE("ResolveParallelOpt-Cancel");
  } else if (opt.kind == ast::ParallelOptionKind::Workgroup) {
    SPEC_RULE("ResolveParallelOpt-Workgroup");
  } else {
    SPEC_RULE("ResolveParallelOpt-Workgroups");
  }
  return {true, std::nullopt, std::nullopt, std::move(out)};
}

ResolveResult<std::vector<ast::ParallelOption>> ResolveParallelOpts(
    ResolveContext& ctx,
    const std::vector<ast::ParallelOption>& opts) {
  ResolveResult<std::vector<ast::ParallelOption>> result;
  result.ok = true;
  result.value.reserve(opts.size());
  if (opts.empty()) {
    SPEC_RULE("ResolveParallelOpts-Empty");
    return result;
  }

  for (const auto& opt : opts) {
    const auto resolved_opt = ResolveParallelOpt(ctx, opt);
    if (!resolved_opt.ok) {
      return {false, resolved_opt.diag_id, resolved_opt.span, {},
              resolved_opt.diag_detail, resolved_opt.diag_children};
    }
    result.value.push_back(resolved_opt.value);
    SPEC_RULE("ResolveParallelOpts-Cons");
  }
  return result;
}

ResolveResult<ast::DispatchOption> ResolveDispatchOpt(
    ResolveContext& ctx,
    const ast::DispatchOption& opt) {
  if (opt.kind == ast::DispatchOptionKind::Reduce) {
    SPEC_RULE("ResolveDispatchOpt-Reduce");
    return {true, std::nullopt, std::nullopt, opt};
  }
  if (opt.kind == ast::DispatchOptionKind::Ordered) {
    SPEC_RULE("ResolveDispatchOpt-Ordered");
    return {true, std::nullopt, std::nullopt, opt};
  }

  ast::ExprPtr value =
      opt.kind == ast::DispatchOptionKind::Chunk ? opt.chunk_expr : opt.workgroup_expr;
  if (!value) {
    if (opt.kind == ast::DispatchOptionKind::Chunk) {
      SPEC_RULE("ResolveDispatchOpt-Chunk");
    } else {
      SPEC_RULE("ResolveDispatchOpt-Workgroup");
    }
    return {true, std::nullopt, std::nullopt, opt};
  }

  const auto resolved_value = ResolveExpr(ctx, value);
  if (!resolved_value.ok) {
    return {false, resolved_value.diag_id, resolved_value.span, {},
            resolved_value.diag_detail, resolved_value.diag_children};
  }

  auto out = opt;
  if (opt.kind == ast::DispatchOptionKind::Chunk) {
    out.chunk_expr = resolved_value.value;
    SPEC_RULE("ResolveDispatchOpt-Chunk");
  } else {
    out.workgroup_expr = resolved_value.value;
    SPEC_RULE("ResolveDispatchOpt-Workgroup");
  }
  return {true, std::nullopt, std::nullopt, std::move(out)};
}

ResolveResult<std::vector<ast::DispatchOption>> ResolveDispatchOpts(
    ResolveContext& ctx,
    const std::vector<ast::DispatchOption>& opts) {
  ResolveResult<std::vector<ast::DispatchOption>> result;
  result.ok = true;
  result.value.reserve(opts.size());
  if (opts.empty()) {
    SPEC_RULE("ResolveDispatchOpts-Empty");
    return result;
  }

  for (const auto& opt : opts) {
    const auto resolved_opt = ResolveDispatchOpt(ctx, opt);
    if (!resolved_opt.ok) {
      return {false, resolved_opt.diag_id, resolved_opt.span, {},
              resolved_opt.diag_detail, resolved_opt.diag_children};
    }
    result.value.push_back(resolved_opt.value);
    SPEC_RULE("ResolveDispatchOpts-Cons");
  }
  return result;
}

ResolveResult<std::variant<ast::TypePath, ast::ModalStateRef>>
ResolveTypeRef(ResolveContext& ctx,
               const std::variant<ast::TypePath, ast::ModalStateRef>& target) {
  ResolveResult<std::variant<ast::TypePath, ast::ModalStateRef>> result;
  return std::visit(
      [&](const auto& node)
          -> ResolveResult<std::variant<ast::TypePath, ast::ModalStateRef>> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePath>) {
          const auto resolved = ResolveTypePath(ctx, node);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {}};
          }
          SPEC_RULE("ResolveTypeRef-Path");
          result.ok = true;
          result.value = resolved.value;
          return result;
        } else {
          const auto resolved = ResolveTypePath(ctx, node.path);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {}};
          }
          ast::ModalStateRef out = node;
          out.path = resolved.value;
          // Also resolve generic args to match ResolveType behavior for TypeModalState
          for (auto& arg : out.generic_args) {
            const auto resolved_arg = ResolveType(ctx, arg);
            if (!resolved_arg.ok) {
              return {false, resolved_arg.diag_id, resolved_arg.span, {}};
            }
            arg = resolved_arg.value;
          }
          ast::SyncModalStateRefFromFields(out);
          SPEC_RULE("ResolveTypeRef-ModalState");
          result.ok = true;
          result.value = out;
          return result;
        }
      },
      target);
}

ResExprResult ResolveCallee(ResolveContext& ctx,
                            const ast::ExprPtr& callee,
                            const std::vector<ast::Arg>& args) {
  if (!callee) {
    return {false, "ResolveExpr-Ident-Err", std::nullopt, {}};
  }
  return std::visit(
      [&](const auto& node) -> ResExprResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto ent = ResolveValueName(*ctx.ctx, node.name);
          if (ent.has_value()) {
            RecordLanguageServiceReference(ctx.language_service, *ctx.ctx,
                                           node.name, callee->span, *ent);
            SPEC_RULE("ResolveCallee-Ident-Value");
            return {true, std::nullopt, std::nullopt, callee};
          }
          if (args.empty() &&
              LookupBuiltinRecordCtorPath(node.name).has_value()) {
            SPEC_RULE("ResolveCallee-Ident-Record");
            return {true, std::nullopt, std::nullopt, callee};
          }
          if (args.empty()) {
            const auto type_ent = ResolveTypeName(*ctx.ctx, node.name);
            if (type_ent.has_value()) {
              const auto name = type_ent->target_opt.value_or(node.name);
              ast::ModulePath module;
              if (type_ent->origin_opt.has_value()) {
                module = *type_ent->origin_opt;
              }
              const auto path = FullPath(module, name);
              const auto it = ctx.ctx->sigma.types.find(PathKeyOf(path));
              if (it != ctx.ctx->sigma.types.end() &&
                  std::holds_alternative<ast::RecordDecl>(it->second)) {
                SPEC_RULE("ResolveCallee-Ident-Record");
                return {true, std::nullopt, std::nullopt, callee};
              }
            }
          }
          return ResolveExpr(ctx, callee);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          TypePath modal_path;
          modal_path.reserve(node.path.size());
          for (const auto& seg : node.path) {
            modal_path.push_back(seg);
          }
          if (IsBuiltinModalTypePath(modal_path) &&
              IsBuiltinModalMemberName(modal_path, node.name)) {
            SPEC_RULE("ResolveCallee-Path-Builtin");
            return {true, std::nullopt, std::nullopt, callee};
          }
          const auto value = ResolveQualified(
              *ctx.ctx, *ctx.name_maps, *ctx.module_names, node.path, node.name,
              EntityKind::Value, ctx.can_access);
          if (value.ok) {
            if (value.entity.has_value()) {
              RecordLanguageServiceReference(ctx.language_service, *ctx.ctx,
                                             node.name, callee->span,
                                             *value.entity);
            }
            SPEC_RULE("ResolveCallee-Path-Value");
            return {true, std::nullopt, std::nullopt, callee};
          }
          const ResolveQualContext qual_ctx = BuildResolveQualContext(ctx);
          if (args.empty()) {
            const auto record = ResolveRecordPath(qual_ctx, node.path, node.name);
            if (record.ok) {
              SPEC_RULE("ResolveCallee-Path-Record");
              return {true, std::nullopt, std::nullopt, callee};
            }
          }
          return ResolveExpr(ctx, callee);
        } else {
          SPEC_RULE("ResolveCallee-Other");
          return ResolveExpr(ctx, callee);
        }
      },
      callee->node);
}

ResolveResult<ast::IfCaseClause> ResolveIfCase(ResolveContext& ctx,
                                               const ast::IfCaseClause& arm) {
  if (!ctx.ctx) {
    return {false, std::nullopt, std::nullopt, {}};
  }
  ScopeGuard guard(*ctx.ctx);
  const auto resolved_pat = ResolvePattern(ctx, arm.pattern);
  if (!resolved_pat.ok) {
    std::string detail = resolved_pat.diag_detail;
    if (!resolved_pat.diag_id.has_value() && detail.empty()) {
      detail = "while resolving `if ... is` clause pattern";
    }
    return {false, resolved_pat.diag_id,
            resolved_pat.span.has_value() ? resolved_pat.span : std::optional<core::Span>(arm.pattern ? arm.pattern->span : (arm.body ? arm.body->span : core::Span{})),
            {}, detail, resolved_pat.diag_children};
  }
  const auto bind = BindPattern(ctx, resolved_pat.value);
  if (!bind.ok) {
    return {false, bind.diag_id,
            bind.span.has_value() ? bind.span : std::optional<core::Span>(arm.pattern ? arm.pattern->span : (arm.body ? arm.body->span : core::Span{})),
            {}};
  }
  const auto resolved_body = ResolveExpr(ctx, arm.body);
  if (!resolved_body.ok) {
    std::string detail = resolved_body.diag_detail;
    if (!resolved_body.diag_id.has_value() && detail.empty()) {
      detail = "while resolving `if ... is` clause body";
    }
    return {false, resolved_body.diag_id,
            resolved_body.span.has_value() ? resolved_body.span : std::optional<core::Span>(arm.body ? arm.body->span : (arm.pattern ? arm.pattern->span : core::Span{})), {},
            detail, resolved_body.diag_children};
  }
  ast::IfCaseClause out = arm;
  out.pattern = resolved_pat.value;
  out.body = resolved_body.value;
  SPEC_RULE("ResolveIfCase");
  return {true, std::nullopt, std::nullopt, out};
}

ResolveResult<std::vector<ast::IfCaseClause>> ResolveIfCases(
    ResolveContext& ctx,
    const std::vector<ast::IfCaseClause>& arms) {
  ResolveResult<std::vector<ast::IfCaseClause>> result;
  result.ok = true;
  if (arms.empty()) {
    SPEC_RULE("ResolveIfCases-Empty");
    return result;
  }
  result.value.reserve(arms.size());
  for (const auto& arm : arms) {
    const auto resolved = ResolveIfCase(ctx, arm);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveIfCases-Cons");
  }
  return result;
}

}  // namespace

ResExprResult ResolveExpr(ResolveContext& ctx,
                          const ast::ExprPtr& expr) {
  SpecDefsResolverExpr();
  ResExprResult result;
  if (!expr || !ctx.ctx || !ctx.name_maps || !ctx.module_names) {
    return result;
  }

  ResolveQualContext qual_ctx = BuildResolveQualContext(ctx);

  return std::visit(
      [&](const auto& node) -> ResExprResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto ent = ResolveValueName(*ctx.ctx, node.name);
          if (!ent.has_value()) {
            SPEC_RULE("ResolveExpr-Ident-Err");
            return MakeUnresolvedValueNameResult<ast::ExprPtr>(*ctx.ctx,
                                                               node.name,
                                                               expr->span);
          }
          RecordLanguageServiceReference(ctx.language_service, *ctx.ctx,
                                         node.name, expr->span, *ent);
          SPEC_RULE("ResolveExpr-Ident");
          return {true, std::nullopt, std::nullopt, expr};
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr> ||
                             std::is_same_v<T, ast::QualifiedApplyExpr>) {
          const auto resolved = ResolveQualifiedForm(qual_ctx, *expr);
          if (!resolved.ok) {
            const auto diag = resolved.diag_id.value_or("ResolveExpr-Ident-Err");
            SPEC_RULE("ResolveExpr-Qualified-Err");
            return {false, diag, expr->span, {}};
          }
          SPEC_RULE("ResolveExpr-Qualified");
          return {true, std::nullopt, std::nullopt, resolved.expr};
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return {true, std::nullopt, std::nullopt, expr};
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          if (!node.expr) {
            return {false, "ResolveExpr-Ident-Err", expr->span, {},
                    "unresolved attributed expression"};
          }
          ResExprResult resolved;
          if (const auto* comptime = std::get_if<ast::ComptimeExpr>(&node.expr->node)) {
            resolved = ResolveComptimeExprWithAttrs(ctx, node.expr, *comptime, &node.attrs);
          } else {
            resolved = ResolveExpr(ctx, node.expr);
          }
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::AttributedExpr>(out.node);
          out_node.expr = resolved.value;
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          const auto payload = ResolveEnumPayload(ctx, node.payload_opt);
          if (!payload.ok) {
            return {false, payload.diag_id, payload.span, {},
                    payload.diag_detail, payload.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::EnumLiteralExpr>(out.node);
          out_node.payload_opt = payload.value;
          SPEC_RULE("ResolveExpr-EnumLiteral");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          const auto resolved_args = ResolveArgs(ctx, node.args);
          if (!resolved_args.ok) {
            return {false, resolved_args.diag_id, resolved_args.span, {},
                    resolved_args.diag_detail, resolved_args.diag_children};
          }
          const auto resolved_callee =
              ResolveCallee(ctx, node.callee, resolved_args.value);
          if (!resolved_callee.ok) {
            return {false, resolved_callee.diag_id, resolved_callee.span, {},
                    resolved_callee.diag_detail, resolved_callee.diag_children};
          }

          std::vector<std::shared_ptr<ast::Type>> resolved_type_args;
          resolved_type_args.reserve(node.type_args.size());
          for (const auto& arg : node.type_args) {
            const auto resolved = ResolveType(ctx, arg);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {}};
            }
            resolved_type_args.push_back(resolved.value);
          }

          ast::CallExpr out_call;
          out_call.callee = resolved_callee.value;
          out_call.args = resolved_args.value;
          out_call.generic_args = std::move(resolved_type_args);
          SPEC_RULE("ResolveExpr-CallTypeArgs");
          return {true, std::nullopt, std::nullopt,
                  MakeExpr(expr->span, out_call)};
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          const auto resolved_args = ResolveArgs(ctx, node.args);
          if (!resolved_args.ok) {
            return {false, resolved_args.diag_id, resolved_args.span, {},
                    resolved_args.diag_detail, resolved_args.diag_children};
          }
          const auto resolved_callee = ResolveCallee(ctx, node.callee, resolved_args.value);
          if (!resolved_callee.ok) {
            return {false, resolved_callee.diag_id, resolved_callee.span, {},
                    resolved_callee.diag_detail, resolved_callee.diag_children};
          }
          // Resolve generic type arguments (§13.1.2 T-Generic-Call)
          std::vector<std::shared_ptr<ast::Type>> resolved_generic_args;
          for (const auto& arg : node.generic_args) {
            const auto resolved = ResolveType(ctx, arg);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {}};
            }
            resolved_generic_args.push_back(resolved.value);
          }
          auto out = *expr;
          auto& out_node = std::get<ast::CallExpr>(out.node);
          out_node.callee = resolved_callee.value;
          out_node.args = resolved_args.value;
          out_node.generic_args = std::move(resolved_generic_args);
          SPEC_RULE("ResolveExpr-Call");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          const auto target = ResolveTypeRef(ctx, node.target);
          if (!target.ok) {
            return {false, target.diag_id, target.span, {},
                    target.diag_detail, target.diag_children};
          }
          const auto fields = ResolveFieldInits(ctx, node.fields);
          if (!fields.ok) {
            return {false, fields.diag_id, fields.span, {},
                    fields.diag_detail, fields.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::RecordExpr>(out.node);
          out_node.target = target.value;
          out_node.fields = fields.value;
          SPEC_RULE("ResolveExpr-RecordExpr");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          const auto resolved_scrutinee = ResolveExpr(ctx, node.scrutinee);
          if (!resolved_scrutinee.ok) {
            return {false, resolved_scrutinee.diag_id, resolved_scrutinee.span, {},
                    resolved_scrutinee.diag_detail, resolved_scrutinee.diag_children};
          }
          const auto resolved_pattern = ResolvePattern(ctx, node.pattern);
          if (!resolved_pattern.ok) {
            return {false, resolved_pattern.diag_id, resolved_pattern.span, {}};
          }
          const auto bind = BindPattern(ctx, resolved_pattern.value);
          if (!bind.ok) {
            return {false, bind.diag_id, bind.span, {}};
          }
          const auto resolved_then = ResolveExpr(ctx, node.then_expr);
          if (!resolved_then.ok) {
            return {false, resolved_then.diag_id, resolved_then.span, {},
                    resolved_then.diag_detail, resolved_then.diag_children};
          }
          const auto resolved_else = ResolveExprOpt(ctx, node.else_expr);
          if (!resolved_else.ok) {
            return {false, resolved_else.diag_id, resolved_else.span, {},
                    resolved_else.diag_detail, resolved_else.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::IfIsExpr>(out.node);
          out_node.scrutinee = resolved_scrutinee.value;
          out_node.pattern = resolved_pattern.value;
          out_node.then_expr = resolved_then.value;
          out_node.else_expr = resolved_else.value;
          SPEC_RULE("ResolveExpr-IfIs");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          const auto resolved_scrutinee = ResolveExpr(ctx, node.scrutinee);
          if (!resolved_scrutinee.ok) {
            return {false, resolved_scrutinee.diag_id, resolved_scrutinee.span, {},
                    resolved_scrutinee.diag_detail, resolved_scrutinee.diag_children};
          }
          const auto resolved_arms = ResolveIfCases(ctx, node.cases);
          if (!resolved_arms.ok) {
            return {false, resolved_arms.diag_id, resolved_arms.span, {},
                    resolved_arms.diag_detail, resolved_arms.diag_children};
          }
          const auto resolved_else = ResolveExprOpt(ctx, node.else_expr);
          if (!resolved_else.ok) {
            return {false, resolved_else.diag_id, resolved_else.span, {},
                    resolved_else.diag_detail, resolved_else.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::IfCaseExpr>(out.node);
          out_node.scrutinee = resolved_scrutinee.value;
          out_node.cases = resolved_arms.value;
          out_node.else_expr = resolved_else.value;
          SPEC_RULE("ResolveExpr-IfCase");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          const auto resolved_pat = ResolvePattern(ctx, node.pattern);
          if (!resolved_pat.ok) {
            return {false, resolved_pat.diag_id, resolved_pat.span, {}};
          }
          const auto resolved_ty = ResolveTypeOpt(ctx, node.type_opt);
          if (!resolved_ty.ok) {
            return {false, resolved_ty.diag_id, resolved_ty.span, {}};
          }
          const auto resolved_iter = ResolveExpr(ctx, node.iter);
          if (!resolved_iter.ok) {
            return {false, resolved_iter.diag_id, resolved_iter.span, {},
                    resolved_iter.diag_detail, resolved_iter.diag_children};
          }
          ScopeGuard guard(*ctx.ctx);
          const auto bind = BindPattern(ctx, resolved_pat.value);
          if (!bind.ok) {
            return {false, bind.diag_id, bind.span, {}};
          }
          ast::LoopIterExpr out = node;
          out.pattern = resolved_pat.value;
          out.type_opt = resolved_ty.value;
          out.iter = resolved_iter.value;
          const auto invariant = ResolveInvariantOpt(ctx, node.invariant_opt);
          if (!invariant.ok) {
            return {false, invariant.diag_id, invariant.span, {},
                    invariant.diag_detail, invariant.diag_children};
          }
          out.invariant_opt = invariant.value;
          if (node.body) {
            const auto body = ResolveBlock(ctx, *node.body);
            if (!body.ok) {
              return {false, body.diag_id, body.span, {},
                      body.diag_detail, body.diag_children};
            }
            out.body = std::make_shared<ast::Block>(body.block);
          }
          SPEC_RULE("ResolveExpr-LoopIter");
          return {true, std::nullopt, std::nullopt,
                  MakeExpr(expr->span, out)};
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (!node.block) {
            return {true, std::nullopt, std::nullopt, expr};
          }
          const auto block = ResolveBlock(ctx, *node.block);
          if (!block.ok) {
            return {false, block.diag_id, block.span, {},
                    block.diag_detail, block.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::BlockExpr>(out.node);
          out_node.block = std::make_shared<ast::Block>(block.block);
          SPEC_RULE("ResolveExpr-Block");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (!node.block) {
            return {true, std::nullopt, std::nullopt, expr};
          }
          const auto block = ResolveBlock(ctx, *node.block);
          if (!block.ok) {
            return {false, block.diag_id, block.span, {},
                    block.diag_detail, block.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::UnsafeBlockExpr>(out.node);
          out_node.block = std::make_shared<ast::Block>(block.block);
          SPEC_RULE("ResolveExpr-Block");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          const auto resolved_ty = ResolveType(ctx, node.type);
          if (!resolved_ty.ok) {
            return {false, resolved_ty.diag_id, resolved_ty.span, {},
                    resolved_ty.diag_detail, resolved_ty.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::TypeLiteralExpr>(out.node);
          out_node.type = resolved_ty.value;
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::QuoteExpr>) {
          return {true, std::nullopt, std::nullopt, expr};
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          return ResolveComptimeExprWithAttrs(ctx, expr, node, nullptr);
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          const auto resolved_cond = ResolveExpr(ctx, node.cond);
          if (!resolved_cond.ok) {
            return {false, resolved_cond.diag_id, resolved_cond.span, {},
                    resolved_cond.diag_detail, resolved_cond.diag_children};
          }
          ast::CtIfExpr out = node;
          out.cond = resolved_cond.value;
          if (node.then_block) {
            const auto then_block = ResolveBlock(ctx, *node.then_block);
            if (!then_block.ok) {
              return {false, then_block.diag_id, then_block.span, {},
                      then_block.diag_detail, then_block.diag_children};
            }
            out.then_block = std::make_shared<ast::Block>(then_block.block);
          }
          if (node.else_block_opt) {
            const auto else_block = ResolveBlock(ctx, *node.else_block_opt);
            if (!else_block.ok) {
              return {false, else_block.diag_id, else_block.span, {},
                      else_block.diag_detail, else_block.diag_children};
            }
            out.else_block_opt = std::make_shared<ast::Block>(else_block.block);
          }
          return {true, std::nullopt, std::nullopt, MakeExpr(expr->span, out)};
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          const auto resolved_pat = ResolvePattern(ctx, node.pattern);
          if (!resolved_pat.ok) {
            return {false, resolved_pat.diag_id, resolved_pat.span, {}};
          }
          const auto resolved_ty = ResolveTypeOpt(ctx, node.type_opt);
          if (!resolved_ty.ok) {
            return {false, resolved_ty.diag_id, resolved_ty.span, {}};
          }
          const auto resolved_iter = ResolveExpr(ctx, node.iter);
          if (!resolved_iter.ok) {
            return {false, resolved_iter.diag_id, resolved_iter.span, {},
                    resolved_iter.diag_detail, resolved_iter.diag_children};
          }
          ScopeGuard guard(*ctx.ctx);
          const auto bind = BindPattern(ctx, resolved_pat.value);
          if (!bind.ok) {
            return {false, bind.diag_id, bind.span, {}};
          }
          ast::CtLoopIterExpr out = node;
          out.pattern = resolved_pat.value;
          out.type_opt = resolved_ty.value;
          out.iter = resolved_iter.value;
          if (node.body) {
            const auto body = ResolveBlock(ctx, *node.body);
            if (!body.ok) {
              return {false, body.diag_id, body.span, {},
                      body.diag_detail, body.diag_children};
            }
            out.body = std::make_shared<ast::Block>(body.block);
          }
          return {true, std::nullopt, std::nullopt, MakeExpr(expr->span, out)};
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          if (node.region_opt.has_value()) {
            const auto ent = ResolveValueName(*ctx.ctx, *node.region_opt);
            if (!ent.has_value()) {
              return {false, "ResolveExpr-Ident-Err", expr->span, {},
                      "unresolved name '" + *node.region_opt + "'"};
            }
          }
          const auto resolved = ResolveExpr(ctx, node.value);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::AllocExpr>(out.node);
          out_node.value = resolved.value;
          if (node.region_opt.has_value()) {
            SPEC_RULE("ResolveExpr-Alloc-Explicit");
          } else {
            SPEC_RULE("ResolveExpr-Alloc-Implicit");
          }
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          if (node.op == "^") {
            if (node.lhs && std::holds_alternative<ast::IdentifierExpr>(node.lhs->node)) {
              const auto& ident = std::get<ast::IdentifierExpr>(node.lhs->node).name;
              const auto ent = ResolveValueName(*ctx.ctx, ident);
              if (ent.has_value() && RegionAlias(*ent)) {
                const auto resolved_rhs = ResolveExpr(ctx, node.rhs);
                if (!resolved_rhs.ok) {
                  return {false, resolved_rhs.diag_id, resolved_rhs.span, {},
                          resolved_rhs.diag_detail, resolved_rhs.diag_children};
                }
                ast::AllocExpr alloc;
                alloc.region_opt = ident;
                alloc.value = resolved_rhs.value;
                SPEC_RULE("ResolveExpr-Alloc-Explicit-ByAlias");
                return {true, std::nullopt, std::nullopt,
                        MakeExpr(expr->span, alloc)};
              }
            }
          }
          const auto resolved_lhs = ResolveExpr(ctx, node.lhs);
          if (!resolved_lhs.ok) {
            return {false, resolved_lhs.diag_id, resolved_lhs.span, {},
                    resolved_lhs.diag_detail, resolved_lhs.diag_children};
          }
          const auto resolved_rhs = ResolveExpr(ctx, node.rhs);
          if (!resolved_rhs.ok) {
            return {false, resolved_rhs.diag_id, resolved_rhs.span, {},
                    resolved_rhs.diag_detail, resolved_rhs.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::BinaryExpr>(out.node);
          out_node.lhs = resolved_lhs.value;
          out_node.rhs = resolved_rhs.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          const auto resolved = ResolveExpr(ctx, node.value);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::UnaryExpr>(out.node);
          out_node.value = resolved.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          const auto resolved_val = ResolveExpr(ctx, node.value);
          if (!resolved_val.ok) {
            return {false, resolved_val.diag_id, resolved_val.span, {},
                    resolved_val.diag_detail, resolved_val.diag_children};
          }
          const auto resolved_ty = ResolveType(ctx, node.type);
          if (!resolved_ty.ok) {
            return {false, resolved_ty.diag_id, resolved_ty.span, {}};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::CastExpr>(out.node);
          out_node.value = resolved_val.value;
          out_node.type = resolved_ty.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = *expr;
          auto& out_node = std::get<ast::RangeExpr>(out.node);
          if (node.lhs) {
            const auto resolved = ResolveExpr(ctx, node.lhs);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out_node.lhs = resolved.value;
          }
          if (node.rhs) {
            const auto resolved = ResolveExpr(ctx, node.rhs);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out_node.rhs = resolved.value;
          }
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          const auto resolved = ResolveExpr(ctx, node.value);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::DerefExpr>(out.node);
          out_node.value = resolved.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          const auto resolved = ResolveExpr(ctx, node.place);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::AddressOfExpr>(out.node);
          out_node.place = resolved.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          const auto resolved = ResolveExpr(ctx, node.place);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::MoveExpr>(out.node);
          out_node.place = resolved.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          auto out = *expr;
          auto& out_node = std::get<ast::TupleExpr>(out.node);
          for (auto& elem : out_node.elements) {
            const auto resolved = ResolveExpr(ctx, elem);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            elem = resolved.value;
          }
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          auto out = *expr;
          auto& out_node = std::get<ast::ArrayExpr>(out.node);
          for (auto& segment : out_node.elements) {
            std::optional<ResExprResult> failure;
            std::visit(
                [&](auto& seg) {
                  if (failure.has_value()) {
                    return;
                  }
                  const auto resolved_value = ResolveExpr(ctx, seg.value);
                  if (!resolved_value.ok) {
                    failure = resolved_value;
                    return;
                  }
                  seg.value = resolved_value.value;
                  if constexpr (std::is_same_v<std::decay_t<decltype(seg)>,
                                               ast::ArrayRepeatSegment>) {
                    const auto resolved_count = ResolveExpr(ctx, seg.count);
                    if (!resolved_count.ok) {
                      failure = resolved_count;
                      return;
                    }
                    seg.count = resolved_count.value;
                  }
                },
                segment);
            if (failure.has_value()) {
              return {false, failure->diag_id, failure->span, {},
                      failure->diag_detail, failure->diag_children};
            }
          }
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          const auto resolved_value = ResolveExpr(ctx, node.value);
          if (!resolved_value.ok) {
            return {false, resolved_value.diag_id, resolved_value.span, {},
                    resolved_value.diag_detail, resolved_value.diag_children};
          }
          const auto resolved_count = ResolveExpr(ctx, node.count);
          if (!resolved_count.ok) {
            return {false, resolved_count.diag_id, resolved_count.span, {},
                    resolved_count.diag_detail, resolved_count.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::ArrayRepeatExpr>(out.node);
          out_node.value = resolved_value.value;
          out_node.count = resolved_count.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          if (!node.type) {
            SPEC_RULE("ResolveExpr-Leaf");
            return {true, std::nullopt, std::nullopt, expr};
          }
          const auto resolved_ty = ResolveType(ctx, node.type);
          if (!resolved_ty.ok) {
            return {false, resolved_ty.diag_id, resolved_ty.span, {}};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::SizeofExpr>(out.node);
          out_node.type = resolved_ty.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          if (!node.type) {
            SPEC_RULE("ResolveExpr-Leaf");
            return {true, std::nullopt, std::nullopt, expr};
          }
          const auto resolved_ty = ResolveType(ctx, node.type);
          if (!resolved_ty.ok) {
            return {false, resolved_ty.diag_id, resolved_ty.span, {}};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::AlignofExpr>(out.node);
          out_node.type = resolved_ty.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          const auto resolved_cond = ResolveExpr(ctx, node.cond);
          if (!resolved_cond.ok) {
            return {false, resolved_cond.diag_id, resolved_cond.span, {},
                    resolved_cond.diag_detail, resolved_cond.diag_children};
          }
          const auto resolved_then = ResolveExpr(ctx, node.then_expr);
          if (!resolved_then.ok) {
            return {false, resolved_then.diag_id, resolved_then.span, {},
                    resolved_then.diag_detail, resolved_then.diag_children};
          }
          std::shared_ptr<ast::Expr> resolved_else_expr = nullptr;
          if (node.else_expr) {
            const auto resolved_else = ResolveExpr(ctx, node.else_expr);
            if (!resolved_else.ok) {
              return {false, resolved_else.diag_id, resolved_else.span, {},
                      resolved_else.diag_detail, resolved_else.diag_children};
            }
            resolved_else_expr = resolved_else.value;
          }
          auto out = *expr;
          auto& out_node = std::get<ast::IfExpr>(out.node);
          out_node.cond = resolved_cond.value;
          out_node.then_expr = resolved_then.value;
          out_node.else_expr = resolved_else_expr;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          auto out = *expr;
          auto& out_node = std::get<ast::LoopInfiniteExpr>(out.node);
          const auto invariant = ResolveInvariantOpt(ctx, node.invariant_opt);
          if (!invariant.ok) {
            return {false, invariant.diag_id, invariant.span, {},
                    invariant.diag_detail, invariant.diag_children};
          }
          out_node.invariant_opt = invariant.value;
          if (node.body) {
            const auto body = ResolveBlock(ctx, *node.body);
            if (!body.ok) {
              return {false, body.diag_id, body.span, {},
                      body.diag_detail, body.diag_children};
            }
            out_node.body = std::make_shared<ast::Block>(body.block);
          }
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          const auto cond = ResolveExpr(ctx, node.cond);
          if (!cond.ok) {
            return {false, cond.diag_id, cond.span, {},
                    cond.diag_detail, cond.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::LoopConditionalExpr>(out.node);
          out_node.cond = cond.value;
          const auto invariant = ResolveInvariantOpt(ctx, node.invariant_opt);
          if (!invariant.ok) {
            return {false, invariant.diag_id, invariant.span, {},
                    invariant.diag_detail, invariant.diag_children};
          }
          out_node.invariant_opt = invariant.value;
          if (node.body) {
            const auto body = ResolveBlock(ctx, *node.body);
            if (!body.ok) {
              return {false, body.diag_id, body.span, {},
                      body.diag_detail, body.diag_children};
            }
            out_node.body = std::make_shared<ast::Block>(body.block);
          }
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          const auto val = ResolveExpr(ctx, node.value);
          if (!val.ok) {
            return {false, val.diag_id, val.span, {},
                    val.diag_detail, val.diag_children};
          }
          const auto src = ResolveType(ctx, node.from);
          if (!src.ok) {
            return {false, src.diag_id, src.span, {}};
          }
          const auto dst = ResolveType(ctx, node.to);
          if (!dst.ok) {
            return {false, dst.diag_id, dst.span, {}};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::TransmuteExpr>(out.node);
          out_node.value = val.value;
          out_node.from = src.value;
          out_node.to = dst.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          const auto base = ResolveExpr(ctx, node.base);
          if (!base.ok) {
            return {false, base.diag_id, base.span, {},
                    base.diag_detail, base.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::FieldAccessExpr>(out.node);
          out_node.base = base.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          const auto base = ResolveExpr(ctx, node.base);
          if (!base.ok) {
            return {false, base.diag_id, base.span, {},
                    base.diag_detail, base.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::TupleAccessExpr>(out.node);
          out_node.base = base.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          const auto base = ResolveExpr(ctx, node.base);
          if (!base.ok) {
            return {false, base.diag_id, base.span, {},
                    base.diag_detail, base.diag_children};
          }
          const auto index = ResolveExpr(ctx, node.index);
          if (!index.ok) {
            return {false, index.diag_id, index.span, {},
                    index.diag_detail, index.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::IndexAccessExpr>(out.node);
          out_node.base = base.value;
          out_node.index = index.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (const auto* recv_ident =
                  std::get_if<ast::IdentifierExpr>(&node.receiver->node);
              recv_ident != nullptr && IdEq(recv_ident->name, "emitter") &&
              IdEq(node.name, "emit") && IsComptimeResolutionEnv(*ctx.ctx) &&
              !ResolveValueName(*ctx.ctx, "emitter").has_value()) {
            return {false, std::optional<std::string_view>{"E-CTE-0250"},
                    std::optional<core::Span>(node.receiver->span), {},
                    "emit call requires the `TypeEmitter` capability", {}};
          }
          const auto recv = ResolveExpr(ctx, node.receiver);
          if (!recv.ok) {
            return {false, recv.diag_id, recv.span, {},
                    recv.diag_detail, recv.diag_children};
          }
          const auto args = ResolveArgs(ctx, node.args);
          if (!args.ok) {
            return {false, args.diag_id, args.span, {},
                    args.diag_detail, args.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::MethodCallExpr>(out.node);
          out_node.receiver = recv.value;
          out_node.args = args.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          const auto val = ResolveExpr(ctx, node.value);
          if (!val.ok) {
            return {false, val.diag_id, val.span, {},
                    val.diag_detail, val.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::PropagateExpr>(out.node);
          out_node.value = val.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          const auto val = ResolveExpr(ctx, node.expr);
          if (!val.ok) {
            return {false, val.diag_id, val.span, {},
                    val.diag_detail, val.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::EntryExpr>(out.node);
          out_node.expr = val.value;
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          const auto val = ResolveExpr(ctx, node.value);
          if (!val.ok) {
            return {false, val.diag_id, val.span, {},
                    val.diag_detail, val.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::YieldExpr>(out.node);
          out_node.value = val.value;
          SPEC_RULE("ResolveExpr-Yield");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          const auto val = ResolveExpr(ctx, node.value);
          if (!val.ok) {
            return {false, val.diag_id, val.span, {},
                    val.diag_detail, val.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::YieldFromExpr>(out.node);
          out_node.value = val.value;
          SPEC_RULE("ResolveExpr-YieldFrom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          const auto val = ResolveExpr(ctx, node.value);
          if (!val.ok) {
            return {false, val.diag_id, val.span, {},
                    val.diag_detail, val.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::SyncExpr>(out.node);
          out_node.value = val.value;
          SPEC_RULE("ResolveExpr-Sync");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          if (!ctx.ctx) {
            return {true, std::nullopt, std::nullopt, expr};
          }
          std::vector<ast::RaceArm> arms;
          arms.reserve(node.arms.size());
          for (const auto& arm : node.arms) {
            const auto resolved_expr = ResolveExpr(ctx, arm.expr);
            if (!resolved_expr.ok) {
              return {false, resolved_expr.diag_id, resolved_expr.span, {},
                      resolved_expr.diag_detail, resolved_expr.diag_children};
            }
            ScopeGuard guard(*ctx.ctx);
            const auto resolved_pat = ResolvePattern(ctx, arm.pattern);
            if (!resolved_pat.ok) {
              return {false, resolved_pat.diag_id, resolved_pat.span, {}};
            }
            const auto bind = BindPattern(ctx, resolved_pat.value);
            if (!bind.ok) {
              return {false, bind.diag_id, bind.span, {}};
            }
            const auto resolved_handler = ResolveExpr(ctx, arm.handler.value);
            if (!resolved_handler.ok) {
              return {false, resolved_handler.diag_id, resolved_handler.span, {},
                      resolved_handler.diag_detail, resolved_handler.diag_children};
            }
            ast::RaceArm out_arm = arm;
            out_arm.expr = resolved_expr.value;
            out_arm.pattern = resolved_pat.value;
            out_arm.handler.value = resolved_handler.value;
            arms.push_back(std::move(out_arm));
          }
          auto out = *expr;
          auto& out_node = std::get<ast::RaceExpr>(out.node);
          out_node.arms = std::move(arms);
          SPEC_RULE("ResolveExpr-Race");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          std::vector<ast::ExprPtr> elems;
          elems.reserve(node.exprs.size());
          for (const auto& elem : node.exprs) {
            const auto resolved = ResolveExpr(ctx, elem);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            elems.push_back(resolved.value);
          }
          auto out = *expr;
          auto& out_node = std::get<ast::AllExpr>(out.node);
          out_node.exprs = std::move(elems);
          SPEC_RULE("ResolveExpr-All");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          SPEC_RULE("ResolveExpr-Hom");
          return {true, std::nullopt, std::nullopt, expr};
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          const auto resolved_domain = ResolveExpr(ctx, node.domain);
          if (!resolved_domain.ok) {
            return {false, resolved_domain.diag_id, resolved_domain.span, {},
                    resolved_domain.diag_detail, resolved_domain.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::ParallelExpr>(out.node);
          out_node.domain = resolved_domain.value;
          const auto resolved_opts = ResolveParallelOpts(ctx, node.opts);
          if (!resolved_opts.ok) {
            return {false, resolved_opts.diag_id, resolved_opts.span, {},
                    resolved_opts.diag_detail, resolved_opts.diag_children};
          }
          out_node.opts = resolved_opts.value;
          if (node.body) {
            const auto body = ResolveBlock(ctx, *node.body);
            if (!body.ok) {
              return {false, body.diag_id, body.span, {},
                      body.diag_detail, body.diag_children};
            }
            out_node.body = std::make_shared<ast::Block>(body.block);
          }
          SPEC_RULE("ResolveExpr-Parallel");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          auto out = *expr;
          auto& out_node = std::get<ast::SpawnExpr>(out.node);
          const auto resolved_opts = ResolveSpawnOpts(ctx, node.opts);
          if (!resolved_opts.ok) {
            return {false, resolved_opts.diag_id, resolved_opts.span, {},
                    resolved_opts.diag_detail, resolved_opts.diag_children};
          }
          out_node.opts = resolved_opts.value;
          if (node.body) {
            const auto body = ResolveBlock(ctx, *node.body);
            if (!body.ok) {
              return {false, body.diag_id, body.span, {},
                      body.diag_detail, body.diag_children};
            }
            out_node.body = std::make_shared<ast::Block>(body.block);
          }
          SPEC_RULE("ResolveExpr-Spawn");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          const auto resolved = ResolveExpr(ctx, node.handle);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::WaitExpr>(out.node);
          out_node.handle = resolved.value;
          SPEC_RULE("ResolveExpr-Wait");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::FenceExpr>) {
          SPEC_RULE("ResolveExpr-Leaf");
          return {true, std::nullopt, std::nullopt, expr};
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          const auto resolved_pat = ResolvePattern(ctx, node.pattern);
          if (!resolved_pat.ok) {
            return {false, resolved_pat.diag_id, resolved_pat.span, {}};
          }
          const auto resolved_range = ResolveExpr(ctx, node.range);
          if (!resolved_range.ok) {
            return {false, resolved_range.diag_id, resolved_range.span, {},
                    resolved_range.diag_detail, resolved_range.diag_children};
          }
          const auto resolved_key_clause =
              ResolveKeyClauseOpt(ctx, node.key_clause);
          if (!resolved_key_clause.ok) {
            return {false, resolved_key_clause.diag_id,
                    resolved_key_clause.span, {},
                    resolved_key_clause.diag_detail,
                    resolved_key_clause.diag_children};
          }
          ScopeGuard guard(*ctx.ctx);
          const auto bind = BindPattern(ctx, resolved_pat.value);
          if (!bind.ok) {
            return {false, bind.diag_id, bind.span, {}};
          }
          auto out = *expr;
          auto& out_node = std::get<ast::DispatchExpr>(out.node);
          out_node.range = resolved_range.value;
          out_node.pattern = resolved_pat.value;
          out_node.key_clause = resolved_key_clause.value;
          const auto resolved_opts = ResolveDispatchOpts(ctx, node.opts);
          if (!resolved_opts.ok) {
            return {false, resolved_opts.diag_id, resolved_opts.span, {},
                    resolved_opts.diag_detail, resolved_opts.diag_children};
          }
          out_node.opts = resolved_opts.value;
          if (node.body) {
            const auto body = ResolveBlock(ctx, *node.body);
            if (!body.ok) {
              return {false, body.diag_id, body.span, {},
                      body.diag_detail, body.diag_children};
            }
            out_node.body = std::make_shared<ast::Block>(body.block);
          }
          SPEC_RULE("ResolveExpr-Dispatch");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Expr>(std::move(out))};
        } else {
          return {true, std::nullopt, std::nullopt, expr};
        }
      },
      expr->node);
}

ResolveStmtResult ResolveStmt(ResolveContext& ctx,
                              const ast::Stmt& stmt) {
  SpecDefsResolverExpr();
  return std::visit(
      [&](const auto& node) -> ResolveStmtResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          auto out = node;
          const auto init = ResolveExpr(ctx, node.binding.init);
          if (!init.ok) {
            return {false, init.diag_id, init.span, {},
                    init.diag_detail, init.diag_children};
          }
          out.binding.init = init.value;
          const auto ty = ResolveTypeOpt(ctx, node.binding.type_opt);
          if (!ty.ok) {
            return {false, ty.diag_id, ty.span, {}};
          }
          out.binding.type_opt = ty.value;
          if (node.binding.pat) {
            const auto pat = ResolvePattern(ctx, node.binding.pat);
            if (!pat.ok) {
              return {false, pat.diag_id, pat.span, {}};
            }
            out.binding.pat = pat.value;
            const auto bind = BindPattern(ctx, pat.value);
            if (!bind.ok) {
              return {false, bind.diag_id, bind.span, {}};
            }
          }
          SPEC_RULE("ResolveStmt-Let");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          auto out = node;
          const auto init = ResolveExpr(ctx, node.binding.init);
          if (!init.ok) {
            return {false, init.diag_id, init.span, {},
                    init.diag_detail, init.diag_children};
          }
          out.binding.init = init.value;
          const auto ty = ResolveTypeOpt(ctx, node.binding.type_opt);
          if (!ty.ok) {
            return {false, ty.diag_id, ty.span, {}};
          }
          out.binding.type_opt = ty.value;
          if (node.binding.pat) {
            const auto pat = ResolvePattern(ctx, node.binding.pat);
            if (!pat.ok) {
              return {false, pat.diag_id, pat.span, {}};
            }
            out.binding.pat = pat.value;
            const auto bind = BindPattern(ctx, pat.value);
            if (!bind.ok) {
              return {false, bind.diag_id, bind.span, {}};
            }
          }
          SPEC_RULE("ResolveStmt-Var");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias (§7.2 UsingAlias, §18.3).
          // Resolve `source` as a value name, then bind `alias` into the
          // current scope to the same Entity. No runtime effect.
          auto out = node;
          const auto ent = ResolveValueName(*ctx.ctx, node.source);
          if (!ent.has_value()) {
            return {false, "ResolveExpr-Ident-Err", node.span, {}};
          }
          RecordLanguageServiceReference(ctx.language_service, *ctx.ctx,
                                         node.source, node.span, *ent);
          const auto res = Intro(*ctx.ctx, node.alias, *ent);
          if (!res.ok) {
            return {false, res.diag_id, node.span, {}};
          }
          SPEC_RULE("ResolveStmt-UsingLocal");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          auto out = node;
          if (node.body) {
            const auto resolved = ResolveBlock(ctx, *node.body);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved.block);
          }
          SPEC_RULE("ResolveStmt-Defer");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          auto out = node;
          if (node.target_opt) {
            const auto ent = ResolveValueName(*ctx.ctx, *node.target_opt);
            if (!ent.has_value()) {
              return {false, "ResolveExpr-Ident-Err", node.span, {},
                      "unresolved name '" + *node.target_opt + "'"};
            }
            RecordLanguageServiceReference(ctx.language_service, *ctx.ctx,
                                           *node.target_opt, node.span, *ent);
            SPEC_RULE("ResolveStmt-Frame-Explicit");
          } else {
            SPEC_RULE("ResolveStmt-Frame-Implicit");
          }
          if (node.body) {
            const auto resolved = ResolveBlock(ctx, *node.body);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved.block);
          }
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          auto out = node;
          const auto opts = ResolveExprOpt(ctx, node.opts_opt);
          if (!opts.ok) {
            return {false, opts.diag_id, opts.span, {},
                    opts.diag_detail, opts.diag_children};
          }
          out.opts_opt = opts.value;
          std::optional<Scope> saved_scope;
          if (node.alias_opt) {
            if (!ctx.ctx || ctx.ctx->scopes.empty()) {
              return {false, "ResolveExpr-Ident-Err", node.span, {},
                      "unresolved name '" + *node.alias_opt + "'"};
            }
            saved_scope = ctx.ctx->scopes.front();
            Entity alias_entity = MakeLanguageServiceLocalEntity(
                ctx.language_service, *ctx.ctx, *node.alias_opt, node.span,
                LanguageSymbolKind::Variable, "region alias");
            alias_entity.source = EntitySource::RegionAlias;
            const auto res = Intro(*ctx.ctx, *node.alias_opt, alias_entity);
            if (!res.ok) {
              return {false, res.diag_id, node.span, {}};
            }
          }
          if (node.body) {
            const auto resolved = ResolveBlock(ctx, *node.body);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved.block);
          }
          if (saved_scope.has_value()) {
            ctx.ctx->scopes.front() = *saved_scope;
          }
          SPEC_RULE("ResolveStmt-Region");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          auto out = node;
          const auto place = ResolveExpr(ctx, node.place);
          if (!place.ok) {
            return {false, place.diag_id, place.span, {},
                    place.diag_detail, place.diag_children};
          }
          const auto value = ResolveExpr(ctx, node.value);
          if (!value.ok) {
            return {false, value.diag_id, value.span, {},
                    value.diag_detail, value.diag_children};
          }
          out.place = place.value;
          out.value = value.value;
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          auto out = node;
          const auto place = ResolveExpr(ctx, node.place);
          if (!place.ok) {
            return {false, place.diag_id, place.span, {},
                    place.diag_detail, place.diag_children};
          }
          const auto value = ResolveExpr(ctx, node.value);
          if (!value.ok) {
            return {false, value.diag_id, value.span, {},
                    value.diag_detail, value.diag_children};
          }
          out.place = place.value;
          out.value = value.value;
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          auto out = node;
          const auto value = ResolveExpr(ctx, node.value);
          if (!value.ok) {
            return {false, value.diag_id, value.span, {},
                    value.diag_detail, value.diag_children};
          }
          out.value = value.value;
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          auto out = node;
          const auto value = ResolveExprOpt(ctx, node.value_opt);
          if (!value.ok) {
            return {false, value.diag_id, value.span, {},
                    value.diag_detail, value.diag_children};
          }
          out.value_opt = value.value;
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          auto out = node;
          const auto value = ResolveExprOpt(ctx, node.value_opt);
          if (!value.ok) {
            return {false, value.diag_id, value.span, {},
                    value.diag_detail, value.diag_children};
          }
          out.value_opt = value.value;
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          return {true, std::nullopt, std::nullopt, node};
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          auto out = node;
          if (node.body) {
            const auto resolved = ResolveBlock(ctx, *node.body);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved.block);
          }
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ComptimeStmt>) {
          auto out = node;
          if (node.body) {
            ScopedLeadingScope comptime_scope(*ctx.ctx);
            comptime_scope.scope() = BuildComptimeCapabilityScope(&node.attrs);
            const auto resolved = ResolveBlock(ctx, *node.body);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved.block);
          }
          SPEC_RULE("ResolveStmt-CtStmt");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          auto out = node;
          const auto resolved_paths = ResolveKeyPathList(ctx, node.paths);
          if (!resolved_paths.ok) {
            return {false, resolved_paths.diag_id, resolved_paths.span, {},
                    resolved_paths.diag_detail, resolved_paths.diag_children};
          }
          out.paths = resolved_paths.value;
          if (node.body) {
            const auto resolved = ResolveBlock(ctx, *node.body);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, {},
                      resolved.diag_detail, resolved.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved.block);
          }
          SPEC_RULE("ResolveStmt-KeyBlock");
          return {true, std::nullopt, std::nullopt, out};
        } else {
          return {true, std::nullopt, std::nullopt, node};
        }
      },
      stmt);
}

// ResolveStmtSeq and ResolveBlock are defined in resolve_stmt_seq.cpp
// and declared in resolver.h - no duplicate definitions here

}  // namespace cursive::analysis
