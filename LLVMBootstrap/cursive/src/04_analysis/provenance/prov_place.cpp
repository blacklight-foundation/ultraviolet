/*
 * =============================================================================
 * prov_place.cpp - Place Expression Provenance Tracking
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 21.4 "Provenance Tracking" (lines 25010-25200)
 *   - CursiveSpecification.md, Section 21.4.1 "Place Expressions" (lines 25020-25100)
 *   - CursiveSpecification.md, Section 10.5 "Memory Provenance" (lines 22510-22600)
 *
 * DIAGNOSTIC CODES:
 *   - E-PROV-0001: Invalid place expression
 *   - E-PROV-0002: Place root not found
 *   - E-PROV-0003: Incompatible place access
 *
 * =============================================================================
 */

#include "04_analysis/memory/regions.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_expr.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// Forward declarations for internal types
enum class ProvKind {
  Global,
  Stack,
  Heap,
  Region,
  Bottom,
  Param,
};

struct ProvTag {
  ProvKind kind = ProvKind::Bottom;
  std::size_t scope_id = 0;
  IdKey region;
  std::size_t param_index = 0;
};

struct ProvScope {
  std::size_t id = 0;
  std::unordered_map<IdKey, ProvTag> map;
};

struct ProvEnv {
  std::vector<ProvScope> scopes;
  std::vector<std::pair<IdKey, IdKey>> regions;  // (tag, target)
  std::size_t next_scope_id = 0;
};

static inline void SpecDefsPlaceProv() {
  SPEC_DEF("ProvPlaceJudg", "5.2.17");
  SPEC_DEF("PlaceExprDef", "6.3");
  SPEC_DEF("PlaceRootDef", "6.3.1");
  SPEC_DEF("PlaceProjectionDef", "6.3.2");
}

static ProvTag BottomTag() {
  return ProvTag{ProvKind::Bottom, 0, IdKey{}, 0};
}

static std::optional<ProvTag> Lookup_pi(const ProvEnv& env, std::string_view name) {
  const auto key = IdKeyOf(name);
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    const auto found = it->map.find(key);
    if (found != it->map.end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

}  // namespace

// =============================================================================
// Place Expression Classification
// =============================================================================

bool IsPlaceExpr(const ast::ExprPtr& expr) {
  SpecDefsPlaceProv();
  if (!expr) {
    return false;
  }

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        // Identifier is a place expression
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          SPEC_RULE("Place-Ident");
          return true;
        }
        // Field access on a place is a place
        else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          SPEC_RULE("Place-Field");
          return IsPlaceExpr(node.base);
        }
        // Tuple access on a place is a place
        else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          SPEC_RULE("Place-Tuple");
          return IsPlaceExpr(node.base);
        }
        // Index access on a place is a place
        else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          SPEC_RULE("Place-Index");
          return IsPlaceExpr(node.base);
        }
        // Dereference of any pointer is a place
        else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          SPEC_RULE("Place-Deref");
          return true;
        }
        // Everything else is not a place
        else {
          return false;
        }
      },
      expr->node);
}

// =============================================================================
// Place Root Computation
// =============================================================================

namespace {

struct PlaceRoot {
  std::optional<std::string> binding_name;
  bool is_deref = false;
};

static PlaceRoot GetPlaceRootImpl(const ast::ExprPtr& place) {
  if (!place) {
    return PlaceRoot{};
  }

  return std::visit(
      [](const auto& node) -> PlaceRoot {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return PlaceRoot{node.name, false};
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return GetPlaceRootImpl(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return GetPlaceRootImpl(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return GetPlaceRootImpl(node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return PlaceRoot{std::nullopt, true};
        } else {
          return PlaceRoot{};
        }
      },
      place->node);
}

}  // namespace

std::optional<std::string> GetPlaceRoot(const ast::ExprPtr& place) {
  SpecDefsPlaceProv();
  SPEC_RULE("PlaceRoot");
  const auto root = GetPlaceRootImpl(place);
  return root.binding_name;
}

// =============================================================================
// Place Projection
// =============================================================================

enum class ProjectionKind {
  Field,
  TupleIndex,
  ArrayIndex,
  Deref,
};

struct Projection {
  ProjectionKind kind;
  std::string field_name;
  std::size_t tuple_index = 0;
  ast::ExprPtr index_expr;
};

std::vector<Projection> PlaceProjection(const ast::ExprPtr& place) {
  SpecDefsPlaceProv();
  std::vector<Projection> projections;

  if (!place) {
    return projections;
  }

  // Build projection list from place expression
  std::function<void(const ast::ExprPtr&)> collect = [&](const ast::ExprPtr& expr) {
    if (!expr) return;

    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
            // Root - no projection
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
            collect(node.base);
            Projection proj;
            proj.kind = ProjectionKind::Field;
            proj.field_name = node.name;
            projections.push_back(proj);
          } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
            collect(node.base);
            Projection proj;
            proj.kind = ProjectionKind::TupleIndex;
            if (const auto index = ast::TupleIndexToSize(node.index);
                index.has_value()) {
              proj.tuple_index = *index;
            }
            projections.push_back(proj);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            collect(node.base);
            Projection proj;
            proj.kind = ProjectionKind::ArrayIndex;
            proj.index_expr = node.index;
            projections.push_back(proj);
          } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
            collect(node.value);
            Projection proj;
            proj.kind = ProjectionKind::Deref;
            projections.push_back(proj);
          }
        },
        expr->node);
  };

  SPEC_RULE("PlaceProjection");
  collect(place);
  return projections;
}

// =============================================================================
// Place Access Validation
// =============================================================================

bool ValidatePlaceAccess(const ast::ExprPtr& place, Permission perm) {
  SpecDefsPlaceProv();

  if (!place) {
    return false;
  }

  // For now, simply check that the expression is a valid place
  // More sophisticated permission checking would require type information
  if (!IsPlaceExpr(place)) {
    SPEC_RULE("PlaceAccess-InvalidPlace");
    return false;
  }

  // Permission-based access rules:
  // - const: read-only access allowed
  // - unique: read and write allowed
  // - shared: requires key acquisition (checked elsewhere)

  SPEC_RULE("PlaceAccess-Valid");
  (void)perm;  // Permission checking delegated to borrow checker
  return true;
}

// =============================================================================
// Place Overlap Detection
// =============================================================================

bool PlaceOverlaps(const ast::ExprPtr& a, const ast::ExprPtr& b) {
  SpecDefsPlaceProv();

  if (!a || !b) {
    return false;
  }

  // Two places overlap if:
  // 1. They have the same root
  // 2. One is a prefix of the other (or they are the same)

  const auto root_a = GetPlaceRoot(a);
  const auto root_b = GetPlaceRoot(b);

  // Different roots - no overlap possible
  if (!root_a.has_value() || !root_b.has_value()) {
    // One of them is a deref - conservative: assume may overlap
    SPEC_RULE("PlaceOverlap-Deref-Conservative");
    return true;
  }

  if (*root_a != *root_b) {
    SPEC_RULE("PlaceOverlap-DifferentRoots");
    return false;
  }

  // Same root - check projections
  const auto proj_a = PlaceProjection(a);
  const auto proj_b = PlaceProjection(b);

  // Check if one is a prefix of the other
  const std::size_t min_len = std::min(proj_a.size(), proj_b.size());

  for (std::size_t i = 0; i < min_len; ++i) {
    const auto& pa = proj_a[i];
    const auto& pb = proj_b[i];

    if (pa.kind != pb.kind) {
      SPEC_RULE("PlaceOverlap-DifferentProjectionKinds");
      return false;
    }

    switch (pa.kind) {
      case ProjectionKind::Field:
        if (pa.field_name != pb.field_name) {
          SPEC_RULE("PlaceOverlap-DifferentFields");
          return false;
        }
        break;
      case ProjectionKind::TupleIndex:
        if (pa.tuple_index != pb.tuple_index) {
          SPEC_RULE("PlaceOverlap-DifferentTupleIndices");
          return false;
        }
        break;
      case ProjectionKind::ArrayIndex:
        // Conservative: array indices may overlap unless we can prove otherwise
        SPEC_RULE("PlaceOverlap-ArrayIndex-Conservative");
        break;
      case ProjectionKind::Deref:
        // Deref projections match
        break;
    }
  }

  // One is a prefix of the other, or they are the same
  SPEC_RULE("PlaceOverlap-Prefix");
  return true;
}

// =============================================================================
// Compute Place Provenance
// =============================================================================

namespace {

struct PlaceProvResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvTag prov;
};

// Forward declaration for mutual recursion
static PlaceProvResult ComputePlaceProvImpl(
    const ScopeContext& ctx,
    const ast::ExprPtr& place,
    const ProvEnv& env,
    const TypeEnv& gamma);

static PlaceProvResult ComputeExprProvForPlace(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr,
    const ProvEnv& env,
    const TypeEnv& gamma);

static PlaceProvResult ComputePlaceProvImpl(
    const ScopeContext& ctx,
    const ast::ExprPtr& place,
    const ProvEnv& env,
    const TypeEnv& gamma) {
  (void)ctx;
  (void)gamma;

  PlaceProvResult result;
  if (!place) {
    result.ok = true;
    result.prov = BottomTag();
    return result;
  }

  return std::visit(
      [&](const auto& node) -> PlaceProvResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          // Look up the binding's provenance
          const auto lookup = Lookup_pi(env, node.name);
          result.ok = true;
          result.prov = lookup.value_or(BottomTag());
          SPEC_RULE("P-Ident");
          return result;
        }
        else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          // Field access inherits base provenance
          auto base = ComputePlaceProvImpl(ctx, node.base, env, gamma);
          if (!base.ok) {
            return base;
          }
          SPEC_RULE("P-Field");
          return base;
        }
        else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          // Tuple access inherits base provenance
          auto base = ComputePlaceProvImpl(ctx, node.base, env, gamma);
          if (!base.ok) {
            return base;
          }
          SPEC_RULE("P-Tuple");
          return base;
        }
        else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          // Index access inherits base provenance
          auto base = ComputePlaceProvImpl(ctx, node.base, env, gamma);
          if (!base.ok) {
            return base;
          }
          SPEC_RULE("P-Index");
          return base;
        }
        else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          // Dereference inherits pointer expression provenance
          auto inner = ComputeExprProvForPlace(ctx, node.value, env, gamma);
          if (!inner.ok) {
            return inner;
          }
          SPEC_RULE("P-Deref");
          return inner;
        }
        else {
          // Not a place expression
          PlaceProvResult out;
          out.ok = true;
          out.prov = BottomTag();
          return out;
        }
      },
      place->node);
}

static PlaceProvResult ComputeExprProvForPlace(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr,
    const ProvEnv& env,
    const TypeEnv& gamma) {
  // For expressions that are places, use place provenance
  if (IsPlaceExpr(expr)) {
    return ComputePlaceProvImpl(ctx, expr, env, gamma);
  }

  // For other expressions, return bottom (conservative)
  PlaceProvResult result;
  result.ok = true;
  result.prov = BottomTag();
  return result;
}

}  // namespace

ProvenanceKind ComputePlaceProvenance(const ScopeContext& ctx,
                                       const ast::ExprPtr& place,
                                       const TypeEnv& gamma) {
  SpecDefsPlaceProv();

  // Create minimal provenance environment
  ProvEnv env;
  env.next_scope_id = 0;

  // Initialize with bindings from type environment
  ProvScope scope;
  scope.id = env.next_scope_id++;
  for (const auto& type_scope : gamma.scopes) {
    for (const auto& [key, binding] : type_scope) {
      // Default to stack provenance for all bindings
      scope.map[key] = ProvTag{ProvKind::Stack, scope.id, IdKey{}, 0};
    }
  }
  env.scopes.push_back(std::move(scope));

  const auto result = ComputePlaceProvImpl(ctx, place, env, gamma);
  if (!result.ok) {
    return ProvenanceKind::Bottom;
  }

  // Convert internal provenance kind to public enum
  switch (result.prov.kind) {
    case ProvKind::Global: return ProvenanceKind::Global;
    case ProvKind::Stack: return ProvenanceKind::Stack;
    case ProvKind::Heap: return ProvenanceKind::Heap;
    case ProvKind::Region: return ProvenanceKind::Region;
    case ProvKind::Bottom: return ProvenanceKind::Bottom;
    case ProvKind::Param: return ProvenanceKind::Param;
  }
  return ProvenanceKind::Bottom;
}

}  // namespace cursive::analysis
