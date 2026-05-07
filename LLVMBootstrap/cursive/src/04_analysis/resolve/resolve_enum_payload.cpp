// =============================================================================
// resolve_enum_payload.cpp - Enum Variant and Payload Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §5.1.6 "Qualified Disambiguation" (Lines 7310-7429)
//   CursiveSpecification.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//
// CONTENT:
//   1. ResolveEnumVariant - Lookup and validate enum variant
//   2. ResolveTuplePayload - Resolve tuple variant payload types
//   3. ResolveRecordPayload - Resolve record variant payload types
//   4. ResolveEnumDiscriminant - Resolve discriminant expression
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <type_traits>
#include <unordered_set>
#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsEnumPayload() {
  SPEC_DEF("ResolveEnumVariant", "5.1.6");
  SPEC_DEF("ResolveTuplePayload", "5.1.7");
  SPEC_DEF("ResolveRecordPayload", "5.1.7");
  SPEC_DEF("ResolveEnumDiscriminant", "5.1.7");
  SPEC_DEF("VariantPayloadKind", "5.1.6");
}

// -----------------------------------------------------------------------------
// Variant Kind Classification
// -----------------------------------------------------------------------------

enum class VariantKind {
  Unit,
  Tuple,
  Record,
};

std::optional<VariantKind> VariantPayloadKind(const ast::VariantDecl& variant) {
  SpecDefsEnumPayload();
  if (!variant.payload_opt) {
    SPEC_RULE("VariantPayloadKind-Unit");
    return VariantKind::Unit;
  }
  if (std::holds_alternative<ast::VariantPayloadTuple>(*variant.payload_opt)) {
    SPEC_RULE("VariantPayloadKind-Tuple");
    return VariantKind::Tuple;
  }
  if (std::holds_alternative<ast::VariantPayloadRecord>(*variant.payload_opt)) {
    SPEC_RULE("VariantPayloadKind-Record");
    return VariantKind::Record;
  }
  return std::nullopt;
}

// -----------------------------------------------------------------------------
// Enum Definition Lookup Helpers
// -----------------------------------------------------------------------------

const ast::EnumDecl* FindEnumDecl(const ScopeContext& ctx,
                                  const ast::TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::EnumDecl>(&it->second);
}

const ast::VariantDecl* FindVariant(const ast::EnumDecl& decl,
                                    std::string_view name) {
  for (const auto& variant : decl.variants) {
    if (IdEq(variant.name, name)) {
      return &variant;
    }
  }
  return nullptr;
}

}  // namespace

// =============================================================================
// Public Interface
// =============================================================================

// -----------------------------------------------------------------------------
// ResolveEnumPayload
// -----------------------------------------------------------------------------
// Resolves an enum payload (tuple or record form) by resolving all contained
// expressions.
//
// Implements (Resolve-Enum-Payload) from §5.1.7:
//   payload = Paren(elems) → ∀ e ∈ elems. ResolveExpr(e)
//   payload = Brace(fields) → ∀ (n, e) ∈ fields. ResolveExpr(e)
// -----------------------------------------------------------------------------

ResolveResult<std::optional<ast::EnumPayload>> ResolveEnumPayload(
    ResolveContext& ctx,
    const std::optional<ast::EnumPayload>& payload_opt) {
  SpecDefsEnumPayload();
  ResolveResult<std::optional<ast::EnumPayload>> result;
  result.ok = true;

  if (!payload_opt.has_value()) {
    SPEC_RULE("ResolveEnumPayload-None");
    result.value = std::nullopt;
    return result;
  }

  return std::visit(
      [&](const auto& node)
          -> ResolveResult<std::optional<ast::EnumPayload>> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::EnumPayloadParen>) {
          // Tuple payload - resolve each element expression
          std::vector<ast::ExprPtr> resolved_elems;
          resolved_elems.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            const auto resolved = ResolveExpr(ctx, elem);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, std::nullopt};
            }
            resolved_elems.push_back(resolved.value);
          }
          ast::EnumPayloadParen out;
          out.elements = std::move(resolved_elems);
          SPEC_RULE("ResolveEnumPayload-Tuple");
          return {true, std::nullopt, std::nullopt, out};
        } else {
          // Record payload - resolve each field value expression
          std::vector<ast::FieldInit> resolved_fields;
          resolved_fields.reserve(node.fields.size());
          for (const auto& field : node.fields) {
            const auto resolved = ResolveExpr(ctx, field.value);
            if (!resolved.ok) {
              return {false, resolved.diag_id, resolved.span, std::nullopt};
            }
            ast::FieldInit out_field = field;
            out_field.value = resolved.value;
            resolved_fields.push_back(std::move(out_field));
          }
          ast::EnumPayloadBrace out;
          out.fields = std::move(resolved_fields);
          SPEC_RULE("ResolveEnumPayload-Record");
          return {true, std::nullopt, std::nullopt, out};
        }
      },
      *payload_opt);
}

// -----------------------------------------------------------------------------
// ResolveTuplePayload
// -----------------------------------------------------------------------------
// Resolves a tuple variant payload by resolving each element type.
//
// Implements (Resolve-Tuple-Payload) from §5.1.7:
//   ∀ ty ∈ fields. Γ ⊢ ResolveType(ty) ⇓ ok
//   → Γ ⊢ ResolveTuplePayload(payload) ⇓ ok
// -----------------------------------------------------------------------------

ResolveResult<ast::VariantPayloadTuple> ResolveTuplePayload(
    ResolveContext& ctx,
    const ast::VariantPayloadTuple& payload) {
  SpecDefsEnumPayload();
  ResolveResult<ast::VariantPayloadTuple> result;
  result.ok = true;

  if (payload.elements.empty()) {
    SPEC_RULE("ResolveTuplePayload-Empty");
    result.value = payload;
    return result;
  }

  std::vector<std::shared_ptr<ast::Type>> resolved_types;
  resolved_types.reserve(payload.elements.size());

  for (const auto& type : payload.elements) {
    const auto resolved = ResolveType(ctx, type);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    resolved_types.push_back(resolved.value);
    SPEC_RULE("ResolveTuplePayload-Cons");
  }

  result.value.elements = std::move(resolved_types);
  return result;
}

// -----------------------------------------------------------------------------
// ResolveRecordPayload
// -----------------------------------------------------------------------------
// Resolves a record variant payload by resolving each field type and
// validating no duplicate field names.
//
// Implements (Resolve-Record-Payload) from §5.1.7:
//   NoDuplicates(field_names) ∧
//   ∀ (n, ty) ∈ fields. Γ ⊢ ResolveType(ty) ⇓ ok
//   → Γ ⊢ ResolveRecordPayload(payload) ⇓ ok
// -----------------------------------------------------------------------------

ResolveResult<ast::VariantPayloadRecord> ResolveRecordPayload(
    ResolveContext& ctx,
    const ast::VariantPayloadRecord& payload) {
  SpecDefsEnumPayload();
  ResolveResult<ast::VariantPayloadRecord> result;
  result.ok = true;

  if (payload.fields.empty()) {
    SPEC_RULE("ResolveRecordPayload-Empty");
    result.value = payload;
    return result;
  }

  // Check for duplicate field names
  std::unordered_set<IdKey> seen_names;
  for (const auto& field : payload.fields) {
    const auto key = IdKeyOf(field.name);
    if (seen_names.find(key) != seen_names.end()) {
      SPEC_RULE("ResolveRecordPayload-DupField");
      return {false, "ResolveRecordPayload-DupField", field.span, {}};
    }
    seen_names.insert(key);
  }

  // Resolve each field type
  std::vector<ast::FieldDecl> resolved_fields;
  resolved_fields.reserve(payload.fields.size());

  for (const auto& field : payload.fields) {
    auto out_field = field;
    const auto resolved_type = ResolveType(ctx, field.type);
    if (!resolved_type.ok) {
      return {false, resolved_type.diag_id, resolved_type.span, {}};
    }
    out_field.type = resolved_type.value;

    // Resolve default initializer if present
    if (field.init_opt) {
      const auto resolved_init = ResolveExpr(ctx, field.init_opt);
      if (!resolved_init.ok) {
        return {false, resolved_init.diag_id, resolved_init.span, {}};
      }
      out_field.init_opt = resolved_init.value;
    }

    resolved_fields.push_back(std::move(out_field));
    SPEC_RULE("ResolveRecordPayload-Cons");
  }

  result.value.fields = std::move(resolved_fields);
  return result;
}

// -----------------------------------------------------------------------------
// ResolveEnumDiscriminant
// -----------------------------------------------------------------------------
// Resolves the discriminant expression of an enum variant.
// The discriminant must be a constant expression (checked in later pass).
// -----------------------------------------------------------------------------

ResExprResult ResolveEnumDiscriminant(
    ResolveContext& ctx,
    const ast::ExprPtr& discrim) {
  SpecDefsEnumPayload();

  if (!discrim) {
    SPEC_RULE("ResolveEnumDiscriminant-None");
    return {true, std::nullopt, std::nullopt, nullptr};
  }

  const auto resolved = ResolveExpr(ctx, discrim);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  SPEC_RULE("ResolveEnumDiscriminant-Ok");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

// -----------------------------------------------------------------------------
// ResolveVariantDecl
// -----------------------------------------------------------------------------
// Resolves a complete variant declaration including payload and discriminant.
// -----------------------------------------------------------------------------

ResolveResult<ast::VariantDecl> ResolveVariantDecl(
    ResolveContext& ctx,
    const ast::VariantDecl& variant) {
  SpecDefsEnumPayload();
  ResolveResult<ast::VariantDecl> result;
  result.ok = true;
  result.value = variant;

  // Resolve payload if present
  if (variant.payload_opt.has_value()) {
    if (std::holds_alternative<ast::VariantPayloadTuple>(*variant.payload_opt)) {
      const auto& payload = std::get<ast::VariantPayloadTuple>(*variant.payload_opt);
      const auto resolved = ResolveTuplePayload(ctx, payload);
      if (!resolved.ok) {
        return {false, resolved.diag_id, resolved.span, {}};
      }
      result.value.payload_opt = resolved.value;
      SPEC_RULE("ResolveVariantDecl-Tuple");
    } else {
      const auto& payload = std::get<ast::VariantPayloadRecord>(*variant.payload_opt);
      const auto resolved = ResolveRecordPayload(ctx, payload);
      if (!resolved.ok) {
        return {false, resolved.diag_id, resolved.span, {}};
      }
      result.value.payload_opt = resolved.value;
      SPEC_RULE("ResolveVariantDecl-Record");
    }
  } else {
    SPEC_RULE("ResolveVariantDecl-Unit");
  }

  // Discriminant is already a Token (simple value like integer literal)
  // No resolution needed - just copy it through
  if (variant.discriminant_opt) {
    result.value.discriminant_opt = variant.discriminant_opt;
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveVariantDeclList
// -----------------------------------------------------------------------------
// Resolves a list of variant declarations.
// -----------------------------------------------------------------------------

ResolveResult<std::vector<ast::VariantDecl>> ResolveVariantDeclList(
    ResolveContext& ctx,
    const std::vector<ast::VariantDecl>& variants) {
  SpecDefsEnumPayload();
  ResolveResult<std::vector<ast::VariantDecl>> result;
  result.ok = true;

  if (variants.empty()) {
    SPEC_RULE("ResolveVariantDeclList-Empty");
    return result;
  }

  result.value.reserve(variants.size());
  for (const auto& variant : variants) {
    const auto resolved = ResolveVariantDecl(ctx, variant);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveVariantDeclList-Cons");
  }

  return result;
}

}  // namespace cursive::analysis
