// =============================================================================
// MIGRATION MAPPING: unions.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Section 5.2.7 "Union Types" (lines 9056-9070)
//   - Members (line 9058)
//   - DistinctMembers (line 9059)
//   - SetMembers (line 9060)
//   - T-Union-Intro (lines 9062-9065)
//   - Union-DirectAccess-Err (lines 9067-9070)
// - Section 5.2.2 "Subtyping" (lines 8594-8696)
//   - Member (line 8684)
//   - Sub-Member-Union (lines 8686-8689)
//   - Sub-Union-Width (lines 8691-8694)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/composite/unions.cpp
// - Lines 1-135 (entire file)
//
// Key source functions to migrate:
// - TypeUnionIntro (lines 54-108): Union introduction rule
// - CheckUnionDirectAccess (lines 110-132): Direct access error check
//
// Supporting helpers:
// - UnionMemberResult: Result type for membership check
// - UnionMember: Check if type is a union member
//
// DEPENDENCIES:
// - ultraviolet/include/04_analysis/typing/type_equiv.h (TypeEquiv)
// - ultraviolet/include/00_core/assert_spec.h (SPEC_DEF, SPEC_RULE)
//
// REFACTORING NOTES:
// 1. Union types are unordered: A|B is equivalent to B|A (spec line 8512 MembersEq)
// 2. The UnionMember check uses TypeEquiv for each member comparison
// 3. Union-DirectAccess-Err prevents field access on union types directly
// 4. Consider caching union member equivalence checks for performance
// 5. The union introduction rule checks permission compatibility when both
//    the value and target have permissions
// =============================================================================

#include "04_analysis/composite/unions.h"

#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_predicates.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsUnions() {
  SPEC_DEF("Members", "5.2.7");
  SPEC_DEF("DistinctMembers", "5.2.7");
  SPEC_DEF("SetMembers", "5.2.7");
  SPEC_DEF("Member", "5.2.2");
}

struct UnionMemberResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  bool member = false;
};

static UnionMemberResult UnionMember(const TypeRef& type,
                                     const TypeUnion& uni) {
  SpecDefsUnions();
  for (const auto& member : uni.members) {
    const auto equiv = TypeEquiv(type, member);
    if (!equiv.ok) {
      return {false, equiv.diag_id, false};
    }
    if (equiv.equiv) {
      return {true, std::nullopt, true};
    }
  }
  return {true, std::nullopt, false};
}

}  // namespace

ExprTypeResult TypeUnionIntro(const ScopeContext& ctx,
                              const TypeRef& value_type,
                              const TypeRef& union_type) {
  (void)ctx;
  SpecDefsUnions();
  ExprTypeResult result;
  if (!value_type || !union_type) {
    return result;
  }

  if (const auto* perm_union = std::get_if<TypePerm>(&union_type->node)) {
    if (!perm_union->base) {
      return result;
    }
    const auto* base_union = std::get_if<TypeUnion>(&perm_union->base->node);
    if (!base_union) {
      return result;
    }
    const auto* perm_value = std::get_if<TypePerm>(&value_type->node);
    if (!perm_value || perm_value->perm != perm_union->perm) {
      return result;
    }
    const auto member = UnionMember(perm_value->base, *base_union);
    if (!member.ok) {
      result.diag_id = member.diag_id;
      return result;
    }
    if (!member.member) {
      return result;
    }
    SPEC_RULE("T-Union-Intro");
    result.ok = true;
    result.type = union_type;
    return result;
  }

  const auto* base_union = std::get_if<TypeUnion>(&union_type->node);
  if (!base_union) {
    return result;
  }

  const auto member = UnionMember(value_type, *base_union);
  if (!member.ok) {
    result.diag_id = member.diag_id;
    return result;
  }
  if (!member.member) {
    return result;
  }

  SPEC_RULE("T-Union-Intro");
  result.ok = true;
  result.type = union_type;
  return result;
}

UnionAccessResult CheckUnionDirectAccess(const ScopeContext& ctx,
                                         const ast::FieldAccessExpr& expr,
                                         const ExprTypeFn& type_expr) {
  (void)ctx;
  SpecDefsUnions();
  UnionAccessResult result;
  if (!expr.base) {
    return result;
  }
  const auto base_type = type_expr(expr.base);
  if (!base_type.ok) {
    result.diag_id = base_type.diag_id;
    return result;
  }
  const auto stripped = StripPerm(base_type.type);
  if (stripped && std::holds_alternative<TypeUnion>(stripped->node)) {
    SPEC_RULE("Union-DirectAccess-Err");
    result.diag_id = "Union-DirectAccess-Err";
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace ultraviolet::analysis
