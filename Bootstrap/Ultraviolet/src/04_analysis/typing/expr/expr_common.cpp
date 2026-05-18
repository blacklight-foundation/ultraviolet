// =============================================================================
// File: 04_analysis/typing/expr/expr_common.cpp
// Expression Typing - Common Utilities
// Spec Section: 5.2.12
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.2.12: Expression Typing
//   - ExprJudg (line 9787): Expression judgments
//   - Lift-Expr (lines 9789-9792): Context lifting
//   - Place-Check (lines 9794-9797): Place expression checking
//   - StripPerm (line 9799): Permission stripping
//
// =============================================================================

#include "04_analysis/typing/expr/expr_common.h"

#include <array>
#include <string_view>

#include "00_core/assert_spec.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsExprCommon() {
  SPEC_DEF("ExprJudg", "5.2.12");
  SPEC_DEF("Lift-Expr", "5.2.12");
  SPEC_DEF("Place-Check", "5.2.12");
  SPEC_DEF("StripPerm", "5.2.12");
  SPEC_DEF("IntTypes", "5.2.10");
  SPEC_DEF("FloatTypes", "5.2.10");
  SPEC_DEF("SignedIntTypes", "5.2.10");
  SPEC_DEF("NumericTypes", "5.2.12");
}

}  // namespace

// Type sets for numeric type checking (Section 5.2.12)
const std::array<std::string_view, 12> kIntTypes = {
    "i8", "i16", "i32", "i64", "i128", "isize",
    "u8", "u16", "u32", "u64", "u128", "usize"
};

const std::array<std::string_view, 6> kSignedIntTypes = {
    "i8", "i16", "i32", "i64", "i128", "isize"
};

const std::array<std::string_view, 3> kFloatTypes = {
    "f16", "f32", "f64"
};

bool IsIntType(std::string_view name) {
  SpecDefsExprCommon();
  SPEC_RULE("IntTypes");
  for (const auto& t : kIntTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

bool IsSignedIntType(std::string_view name) {
  SpecDefsExprCommon();
  SPEC_RULE("SignedIntTypes");
  for (const auto& t : kSignedIntTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

bool IsFloatType(std::string_view name) {
  SpecDefsExprCommon();
  SPEC_RULE("FloatTypes");
  for (const auto& t : kFloatTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

bool IsNumericType(std::string_view name) {
  SpecDefsExprCommon();
  SPEC_RULE("NumericTypes");
  return IsIntType(name) || IsFloatType(name);
}

bool IsPrimTypeName(std::string_view name) {
  SpecDefsExprCommon();
  if (name == "bool" || name == "char" || name == "()" || name == "!") {
    return true;
  }
  return IsNumericType(name);
}

// Operator classification (Section 5.2.12)
bool IsArithOp(std::string_view op) {
  return op == "+" || op == "-" || op == "*" || op == "/" ||
         op == "%" || op == "**";
}

bool IsBitOp(std::string_view op) {
  return op == "&" || op == "|" || op == "^";
}

bool IsShiftOp(std::string_view op) {
  return op == "<<" || op == ">>";
}

bool IsEqOp(std::string_view op) {
  return op == "==" || op == "!=";
}

bool IsOrdOp(std::string_view op) {
  return op == "<" || op == "<=" || op == ">" || op == ">=";
}

bool IsLogicOp(std::string_view op) {
  return op == "&&" || op == "||";
}

std::optional<std::string> GetPrimName(const TypeRef& type) {
  SpecDefsExprCommon();
  if (!type) {
    return std::nullopt;
  }
  // Strip permission qualifiers
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
  if (!cur) {
    return std::nullopt;
  }
  if (const auto* prim = std::get_if<TypePrim>(&cur->node)) {
    return prim->name;
  }
  return std::nullopt;
}

}  // namespace ultraviolet::analysis::expr
