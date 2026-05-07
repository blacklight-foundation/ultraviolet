/*
 * =============================================================================
 * subset_checks.cpp - Type subset checking implementation
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 5.2.2 "Subtyping" (lines 8594-8696)
 *   - CursiveSpecification.md, Section 10.4 "Permission Admissibility"
 *   - CursiveSpecification.md, Section 5.3 "CastValid" predicate
 *
 * MIGRATED FROM:
 *   - cursive-bootstrap/src/03_analysis/types/conformance.cpp
 *     (subset checking logic integrated into conformance.cpp)
 *
 * KEY RULES:
 *   - Permission-qualified subtyping requires permission equality
 *   - No implicit numeric coercion (i32 -> i64 requires explicit cast)
 *   - Union subset: every variant of A|B must appear in C|D for subset
 *   - Safe pointer states: @Valid <: @Null (valid can be used where null expected)
 *   - Modal widened type is supertype of state-specific types
 *
 * DIAGNOSTIC CODES:
 *   - E-SEM-2533: Subset check failed
 *   - E-SEM-2533: Coercion not allowed
 *   - E-SEM-2533: Explicit cast required
 *
 * =============================================================================
 */

#include "04_analysis/conformance/conformance.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsSubsetChecks() {
  SPEC_DEF("SubtypingJudg", "5.2.2");
  SPEC_DEF("Member", "5.2.2");
  SPEC_DEF("Sub-Union-Width", "5.2.2");
  SPEC_DEF("CastValid", "5.3");
  SPEC_DEF("Coerce-Array-Slice", "5.3.1");
}

// Numeric type categories for cast validation
static constexpr std::array<std::string_view, 12> kIntTypes = {
    "i8",  "i16", "i32",  "i64",   "i128",  "u8",
    "u16", "u32", "u64",  "u128",  "isize", "usize"};

static constexpr std::array<std::string_view, 3> kFloatTypes = {"f16", "f32",
                                                                 "f64"};

static constexpr std::array<std::string_view, 15> kNumericTypes = {
    "i8",  "i16", "i32",  "i64", "i128", "u8",    "u16", "u32",
    "u64", "u128", "isize", "usize", "f16", "f32", "f64"};

static bool IsIntType(std::string_view name) {
  for (const auto& t : kIntTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

[[maybe_unused]]
static bool IsFloatType(std::string_view name) {
  for (const auto& t : kFloatTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

static bool IsNumericType(std::string_view name) {
  for (const auto& t : kNumericTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

/// Strip permission wrapper from a type to get the base type.
static TypeRef StripPerm(const TypeRef& type) {
  if (!type) {
    return type;
  }
  const auto* perm = std::get_if<TypePerm>(&type->node);
  if (perm) {
    return perm->base;
  }
  return type;
}

/// Get the primitive type name if the type is a primitive.
static std::optional<std::string_view> GetPrimName(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  const auto stripped = StripPerm(type);
  const auto* prim = std::get_if<TypePrim>(&stripped->node);
  if (prim) {
    return prim->name;
  }
  return std::nullopt;
}

/// Check if two type paths are equal using NFC-normalized comparison.
[[maybe_unused]]
static bool TypePathEq(const TypePath& lhs, const TypePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

/// Check if a single type T is a member of union U.
/// Member(T, U) iff U = TypeUnion([U_1, ..., U_n]) and exists i. T equiv U_i
static bool IsMember(const TypeRef& type, const TypeUnion& uni) {
  SpecDefsSubsetChecks();
  for (const auto& member : uni.members) {
    const auto res = TypeEquiv(type, member);
    if (res.ok && res.equiv) {
      return true;
    }
  }
  return false;
}

/// CastValid(S, T) predicate from §5.3.
/// Determines if a cast from S to T is valid.
/// Cast is valid for:
/// - Numeric to numeric (any combination of int/float types)
/// - bool to int type
/// - int type to bool
/// - char to u32
/// - u32 to char
static bool CastValid(const TypeRef& source, const TypeRef& target) {
  SpecDefsSubsetChecks();
  SPEC_RULE("CastValid");

  const auto s_name = GetPrimName(source);
  const auto t_name = GetPrimName(target);

  if (!s_name.has_value() || !t_name.has_value()) {
    return false;
  }

  const auto s = *s_name;
  const auto t = *t_name;

  // Numeric to numeric
  if (IsNumericType(s) && IsNumericType(t)) {
    return true;
  }

  // bool to int type
  if (s == "bool" && IsIntType(t)) {
    return true;
  }

  // int type to bool
  if (IsIntType(s) && t == "bool") {
    return true;
  }

  // char to u32
  if (s == "char" && t == "u32") {
    return true;
  }

  // u32 to char
  if (s == "u32" && t == "char") {
    return true;
  }

  return false;
}

/// Check if array-to-slice coercion applies.
/// Coerce-Array-Slice: TypePerm(p, TypeArray(T, n)) can coerce to TypePerm(p, TypeSlice(T))
static bool ArrayToSliceCoercion(const TypeRef& from, const TypeRef& to) {
  SpecDefsSubsetChecks();

  const auto* from_perm = std::get_if<TypePerm>(&from->node);
  const auto* to_perm = std::get_if<TypePerm>(&to->node);
  const Permission from_effective_perm =
      from_perm ? from_perm->perm : Permission::Const;
  const Permission to_effective_perm =
      to_perm ? to_perm->perm : Permission::Const;

  TypeRef from_base = from_perm ? from_perm->base : from;
  TypeRef to_base = to_perm ? to_perm->base : to;

  if (from_effective_perm != to_effective_perm) {
    return false;
  }

  // Check array -> slice
  const auto* from_array = std::get_if<TypeArray>(&from_base->node);
  const auto* to_slice = std::get_if<TypeSlice>(&to_base->node);

  if (!from_array || !to_slice) {
    return false;
  }

  // Element types must be equivalent
  const auto elem_eq = TypeEquiv(from_array->element, to_slice->element);
  if (!elem_eq.ok || !elem_eq.equiv) {
    return false;
  }

  SPEC_RULE("Coerce-Array-Slice");
  return true;
}

}  // namespace

bool PermissionSubset(Permission sub, Permission super) {
  SpecDefsSubsetChecks();

  if (sub == super) {
    if (sub == Permission::Const) {
      SPEC_RULE("Perm-Const");
    } else if (sub == Permission::Unique) {
      SPEC_RULE("Perm-Unique");
    } else if (sub == Permission::Shared) {
      SPEC_RULE("Perm-Shared");
    }
    return true;
  }

  return false;
}

TypeSubsetResult IsSubsetOf(const ScopeContext& ctx,
                            const TypeRef& sub,
                            const TypeRef& super) {
  SpecDefsSubsetChecks();

  if (!sub || !super) {
    return {true, std::nullopt, false};
  }

  // Use the existing Subtyping implementation which implements §5.2.2
  const auto result = Subtyping(ctx, sub, super);

  return {result.ok, result.diag_id, result.subtype};
}

TypeSubsetResult UnionSubset(const ScopeContext& ctx,
                             const TypeRef& union_sub,
                             const TypeRef& union_super) {
  SpecDefsSubsetChecks();

  if (!union_sub || !union_super) {
    return {true, std::nullopt, false};
  }

  // Extract union types
  const auto* sub_union = std::get_if<TypeUnion>(&union_sub->node);
  const auto* super_union = std::get_if<TypeUnion>(&union_super->node);

  if (!sub_union || !super_union) {
    // If not both unions, fall back to general subset check
    return IsSubsetOf(ctx, union_sub, union_super);
  }

  SPEC_RULE("Sub-Union-Width");

  // Every member of sub_union must be a member of super_union
  for (const auto& member : sub_union->members) {
    if (!IsMember(member, *super_union)) {
      return {true, std::optional<std::string_view>{"E-SEM-2533"}, false};
    }
  }

  return {true, std::nullopt, true};
}

CoercionResult CanCoerceTo(const ScopeContext& ctx,
                           const TypeRef& from,
                           const TypeRef& to) {
  SpecDefsSubsetChecks();

  if (!from || !to) {
    return {true, std::nullopt, false};
  }

  // Check if subtype relation holds (implicit coercion via subtyping)
  const auto subset = IsSubsetOf(ctx, from, to);
  if (!subset.ok) {
    return {false, subset.diag_id, false};
  }
  if (subset.is_subset) {
    return {true, std::nullopt, true};
  }

  // Check special coercion rules

  // Array to slice coercion (§5.3.1 Coerce-Array-Slice)
  if (ArrayToSliceCoercion(from, to)) {
    return {true, std::nullopt, true};
  }

  const auto* from_perm = std::get_if<TypePerm>(&from->node);
  const auto* to_perm = std::get_if<TypePerm>(&to->node);

  if (from_perm && to_perm && from_perm->perm == to_perm->perm) {
    const auto base_eq = TypeEquiv(from_perm->base, to_perm->base);
    if (base_eq.ok && base_eq.equiv) {
      return {true, std::nullopt, true};
    }
  }

  if (from_perm && !to_perm && from_perm->perm == Permission::Const) {
    const auto base_eq = TypeEquiv(from_perm->base, to);
    if (base_eq.ok && base_eq.equiv) {
      return {true, std::nullopt, true};
    }
  }

  // No implicit coercion allowed
  return {true, std::optional<std::string_view>{"E-SEM-2533"}, false};
}

CastResult RequiresExplicitCast(const ScopeContext& ctx,
                                const TypeRef& from,
                                const TypeRef& to) {
  SpecDefsSubsetChecks();

  if (!from || !to) {
    return {true, std::nullopt, false, false};
  }

  // First check if implicit coercion is possible
  const auto coerce = CanCoerceTo(ctx, from, to);
  if (!coerce.ok) {
    return {false, coerce.diag_id, false, false};
  }
  if (coerce.can_coerce) {
    // No explicit cast required - implicit coercion works
    return {true, std::nullopt, false, true};
  }

  // Check type equivalence - if types are equivalent, no cast needed
  const auto equiv = TypeEquiv(from, to);
  if (equiv.ok && equiv.equiv) {
    return {true, std::nullopt, false, true};
  }

  // Check if explicit cast is valid
  const bool cast_valid = CastValid(from, to);

  if (cast_valid) {
    // Explicit cast is required and valid
    SPEC_RULE("T-Cast");
    return {true, std::nullopt, true, true};
  }

  // Check pointer casts in unsafe contexts
  // Raw pointer casts may be valid but require unsafe
  const auto from_stripped = StripPerm(from);
  const auto to_stripped = StripPerm(to);

  const auto* from_raw = std::get_if<TypeRawPtr>(&from_stripped->node);
  const auto* to_raw = std::get_if<TypeRawPtr>(&to_stripped->node);

  if (from_raw && to_raw) {
    // Raw pointer cast - requires unsafe but is valid
    return {true, std::nullopt, true, true};
  }

  // Check usize <-> raw pointer casts
  const auto from_name = GetPrimName(from);
  if (from_name.has_value() && (*from_name == "usize" || *from_name == "isize") && to_raw) {
    return {true, std::nullopt, true, true};
  }

  const auto to_name = GetPrimName(to);
  if (from_raw && to_name.has_value() && (*to_name == "usize" || *to_name == "isize")) {
    return {true, std::nullopt, true, true};
  }

  // Cast not valid
  return {true, std::optional<std::string_view>{"E-SEM-2533"}, true, false};
}

}  // namespace cursive::analysis

