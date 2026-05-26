// =============================================================================
// resolve_pattern.cpp - Pattern Resolution Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §5.1.2 "Name Introduction and Shadowing" (Lines 6718-6821)
//   Docs/SPECIFICATION.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//
// SOURCE FILE:
//   ultraviolet-bootstrap/src/03_analysis/resolve/resolver_pat.cpp (Lines 1-283)
//
// MIGRATED: 2026-02-01
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <utility>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsResolverPatterns() {
  SPEC_DEF("ResolvePattern", "5.1.7");
  SPEC_DEF("ResolvePatternList", "5.1.7");
  SPEC_DEF("ResolveFieldPatternList", "5.1.7");
  SPEC_DEF("ResolveEnumPayloadPattern", "5.1.7");
  SPEC_DEF("ResolveFieldPatternListOpt", "5.1.7");
  SPEC_DEF("ResolveTypePath", "5.1.7");
}

ast::TypePath FullPath(const ast::TypePath& path,
                            std::string_view name) {
  ast::TypePath out = path;
  out.emplace_back(name);
  return out;
}


const ast::EnumDecl* FindEnumDecl(const ScopeContext& ctx,
                                     const ast::TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::EnumDecl>(&it->second);
}

const ast::RecordDecl* FindRecordDecl(const ScopeContext& ctx,
                                      const ast::TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::RecordDecl>(&it->second);
}

bool HasRecordPayloadVariant(const ast::EnumDecl& decl,
                             std::string_view variant_name) {
  for (const auto& variant : decl.variants) {
    if (variant.name != variant_name || !variant.payload_opt.has_value()) {
      continue;
    }
    return std::holds_alternative<ast::VariantPayloadRecord>(
        *variant.payload_opt);
  }
  return false;
}

ResolveResult<std::vector<ast::PatternPtr>> ResolvePatternList(
    ResolveContext& ctx,
    const std::vector<ast::PatternPtr>& patterns) {
  ResolveResult<std::vector<ast::PatternPtr>> result;
  result.ok = true;
  if (patterns.empty()) {
    SPEC_RULE("ResolvePatternList-Empty");
    return result;
  }
  result.value.reserve(patterns.size());
  for (const auto& pattern : patterns) {
    const auto resolved = ResolvePattern(ctx, pattern);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolvePatternList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::FieldPattern>> ResolveFieldPatternList(
    ResolveContext& ctx,
    const std::vector<ast::FieldPattern>& fields) {
  ResolveResult<std::vector<ast::FieldPattern>> result;
  result.ok = true;
  if (fields.empty()) {
    SPEC_RULE("ResolveFieldPatternList-Empty");
    return result;
  }
  result.value.reserve(fields.size());
  for (const auto& field : fields) {
    ast::FieldPattern out = field;
    if (field.pattern_opt) {
      const auto resolved = ResolvePattern(ctx, field.pattern_opt);
      if (!resolved.ok) {
        return {false, resolved.diag_id, resolved.span, {}};
      }
      out.pattern_opt = resolved.value;
      SPEC_RULE("ResolveFieldPattern-Explicit");
    } else {
      SPEC_RULE("ResolveFieldPattern-Implicit");
    }
    result.value.push_back(std::move(out));
    SPEC_RULE("ResolveFieldPatternList-Cons");
  }
  return result;
}

ResolveResult<std::optional<ast::ModalRecordPayload>>
ResolveModalRecordPayloadOpt(ResolveContext& ctx,
                             const std::optional<ast::ModalRecordPayload>&
                                 fields_opt) {
  ResolveResult<std::optional<ast::ModalRecordPayload>> result;
  result.ok = true;
  if (!fields_opt.has_value()) {
    SPEC_RULE("ResolveFieldPatternListOpt-None");
    return result;
  }
  const auto fields = ResolveFieldPatternList(ctx, fields_opt->fields);
  if (!fields.ok) {
    return {false, fields.diag_id, fields.span, std::nullopt};
  }
  ast::ModalRecordPayload payload;
  payload.fields = fields.value;
  result.value = std::move(payload);
  SPEC_RULE("ResolveFieldPatternListOpt-Some");
  return result;
}

ResolveResult<std::optional<ast::EnumPayloadPattern>>
ResolveEnumPayloadPattern(ResolveContext& ctx,
                          const std::optional<ast::EnumPayloadPattern>&
                              payload_opt) {
  ResolveResult<std::optional<ast::EnumPayloadPattern>> result;
  result.ok = true;
  if (!payload_opt.has_value()) {
    SPEC_RULE("ResolveEnumPayloadPattern-None");
    return result;
  }
  return std::visit(
      [&](const auto& node) -> ResolveResult<std::optional<ast::EnumPayloadPattern>> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TuplePayloadPattern>) {
          const auto elems = ResolvePatternList(ctx, node.elements);
          if (!elems.ok) {
            return {false, elems.diag_id, elems.span, std::nullopt};
          }
          ast::TuplePayloadPattern out;
          out.elements = elems.value;
          result.value = out;
          SPEC_RULE("ResolveEnumPayloadPattern-Tuple");
          return result;
        } else {
          const auto fields = ResolveFieldPatternList(ctx, node.fields);
          if (!fields.ok) {
            return {false, fields.diag_id, fields.span, std::nullopt};
          }
          ast::RecordPayloadPattern out;
          out.fields = fields.value;
          result.value = out;
          SPEC_RULE("ResolveEnumPayloadPattern-Record");
          return result;
        }
      },
      *payload_opt);
}

}  // namespace

ResPatternResult ResolvePattern(ResolveContext& ctx,
                               const ast::PatternPtr& pattern) {
  SpecDefsResolverPatterns();
  ResPatternResult result;
  if (!pattern) {
    return result;
  }
  return std::visit(
      [&](const auto& node) -> ResPatternResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
          SPEC_RULE("ResolvePat-Wild");
          return {true, std::nullopt, std::nullopt, pattern};
        } else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          SPEC_RULE("ResolvePat-Ident");
          return {true, std::nullopt, std::nullopt, pattern};
        } else if constexpr (std::is_same_v<T, ast::LiteralPattern>) {
          SPEC_RULE("ResolvePat-Literal");
          return {true, std::nullopt, std::nullopt, pattern};
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          const auto resolved = ResolveType(ctx, node.type);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {},
                    resolved.diag_detail, resolved.diag_children};
          }
          auto out = *pattern;
          auto& out_node = std::get<ast::TypedPattern>(out.node);
          out_node.type = resolved.value;
          SPEC_RULE("ResolvePat-Typed");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Pattern>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          const auto elems = ResolvePatternList(ctx, node.elements);
          if (!elems.ok) {
            return {false, elems.diag_id, elems.span, {}};
          }
          auto out = *pattern;
          auto& out_node = std::get<ast::TuplePattern>(out.node);
          out_node.elements = elems.value;
          SPEC_RULE("ResolvePat-Tuple");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Pattern>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          const auto resolved_path = ResolveTypePath(ctx, node.path);
          if (!resolved_path.ok) {
            return {false, resolved_path.diag_id,
                    resolved_path.span.has_value()
                        ? resolved_path.span
                        : std::optional<core::Span>(pattern->span),
                    {}, resolved_path.diag_detail,
                    resolved_path.diag_children};
          }
          const auto fields = ResolveFieldPatternList(ctx, node.fields);
          if (!fields.ok) {
            return {false, fields.diag_id, fields.span, {},
                    fields.diag_detail, fields.diag_children};
          }
          auto out = *pattern;
          auto& out_node = std::get<ast::RecordPattern>(out.node);
          out_node.path = resolved_path.value;
          out_node.fields = fields.value;
          SPEC_RULE("ResolvePat-Record");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Pattern>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          const bool record_payload =
              node.payload_opt &&
              std::holds_alternative<ast::RecordPayloadPattern>(*node.payload_opt);
          const auto resolved_path = ResolveTypePath(ctx, node.path);
          if (!resolved_path.ok) {
            if (record_payload) {
              const ast::TypePath joined = FullPath(node.path, node.name);
              const auto resolved_rec = ResolveTypePath(ctx, joined);
              if (resolved_rec.ok && ctx.ctx &&
                  FindRecordDecl(*ctx.ctx, resolved_rec.value)) {
                const auto fields = ResolveFieldPatternList(
                    ctx,
                    std::get<ast::RecordPayloadPattern>(*node.payload_opt).fields);
                if (!fields.ok) {
                  return {false, fields.diag_id, fields.span, {},
                          fields.diag_detail, fields.diag_children};
                }
                ast::RecordPattern rec;
                rec.path = resolved_rec.value;
                rec.fields = fields.value;
                auto out = std::make_shared<ast::Pattern>();
                out->span = pattern->span;
                out->node = std::move(rec);
                SPEC_RULE("ResolvePat-Enum-Record-Fallback");
                return {true, std::nullopt, std::nullopt, out};
              }
            }
            return {false, resolved_path.diag_id, resolved_path.span, {},
                    resolved_path.diag_detail, resolved_path.diag_children};
          }
          const auto* decl = ctx.ctx ? FindEnumDecl(*ctx.ctx, resolved_path.value)
                                     : nullptr;
          if (decl && record_payload &&
              HasRecordPayloadVariant(*decl, node.name)) {
            const ast::TypePath joined = FullPath(node.path, node.name);
            const auto resolved_rec = ResolveTypePath(ctx, joined);
            if (resolved_rec.ok && ctx.ctx &&
                FindRecordDecl(*ctx.ctx, resolved_rec.value)) {
              return {false, "E-MOD-1307",
                      std::optional<core::Span>(pattern->span), {}};
            }
          }
          if (!decl && record_payload) {
            const ast::TypePath joined = FullPath(node.path, node.name);
            const auto resolved_rec = ResolveTypePath(ctx, joined);
            if (!resolved_rec.ok) {
              return {false, resolved_rec.diag_id, resolved_rec.span, {},
                      resolved_rec.diag_detail, resolved_rec.diag_children};
            }
            if (!ctx.ctx || !FindRecordDecl(*ctx.ctx, resolved_rec.value)) {
              return {false, "E-TYP-1501",
                      std::optional<core::Span>(pattern->span), {}};
            }
            const auto fields = ResolveFieldPatternList(
                ctx,
                std::get<ast::RecordPayloadPattern>(*node.payload_opt).fields);
            if (!fields.ok) {
              return {false, fields.diag_id, fields.span, {},
                      fields.diag_detail, fields.diag_children};
            }
            ast::RecordPattern rec;
            rec.path = resolved_rec.value;
            rec.fields = fields.value;
            auto out = std::make_shared<ast::Pattern>();
            out->span = pattern->span;
            out->node = std::move(rec);
            SPEC_RULE("ResolvePat-Enum-Record-Fallback");
            return {true, std::nullopt, std::nullopt, out};
          }
          const auto payload = ResolveEnumPayloadPattern(ctx, node.payload_opt);
          if (!payload.ok) {
            return {false, payload.diag_id, payload.span, {}};
          }
          auto out = *pattern;
          auto& out_node = std::get<ast::EnumPattern>(out.node);
          out_node.path = resolved_path.value;
          out_node.payload_opt = payload.value;
          SPEC_RULE("ResolvePat-Enum");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Pattern>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          const auto fields = ResolveModalRecordPayloadOpt(ctx, node.fields_opt);
          if (!fields.ok) {
            return {false, fields.diag_id, fields.span, {}};
          }
          auto out = *pattern;
          auto& out_node = std::get<ast::ModalPattern>(out.node);
          out_node.fields_opt = fields.value;
          SPEC_RULE("ResolvePat-Modal");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Pattern>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          const auto resolved_lo = ResolvePattern(ctx, node.lo);
          if (!resolved_lo.ok) {
            return {false, resolved_lo.diag_id, resolved_lo.span, {}};
          }
          const auto resolved_hi = ResolvePattern(ctx, node.hi);
          if (!resolved_hi.ok) {
            return {false, resolved_hi.diag_id, resolved_hi.span, {}};
          }
          auto out = *pattern;
          auto& out_node = std::get<ast::RangePattern>(out.node);
          out_node.lo = resolved_lo.value;
          out_node.hi = resolved_hi.value;
          SPEC_RULE("ResolvePat-Range");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Pattern>(std::move(out))};
        } else {
          return {true, std::nullopt, std::nullopt, pattern};
        }
      },
      pattern->node);
}

}  // namespace ultraviolet::analysis
