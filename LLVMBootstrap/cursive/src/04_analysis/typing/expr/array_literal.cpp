// =================================================================
// File: 04_analysis/typing/expr/array_literal.cpp
// Construct: Array Literal Expression Type Checking
// Spec Section: 5.2.6
// Spec Rules: T-Array-Literal-Segments
// =================================================================

#include "04_analysis/typing/expr/array_literal.h"

#include <cstdint>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsArrayLiteral() {
  SPEC_DEF("T-Array-Literal-Segments", "5.2.6");
}

}  // namespace

// T-Array-Literal-Segments: Array from explicit and repeated segments
ExprTypeResult TypeArrayExprImpl(const ScopeContext& ctx,
                                 const ast::ArrayExpr& expr,
                                 const TypeExprFn& type_expr) {
  SpecDefsArrayLiteral();
  ExprTypeResult result;

  // Empty array literal requires type annotation
  if (expr.elements.empty()) {
    return result;
  }

  analysis::TypeRef element_type;
  std::uint64_t total_length = 0;

  for (const auto& segment : expr.elements) {
    const auto segment_ok = std::visit(
        [&](const auto& node) -> ExprTypeResult {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ArrayElemSegment>) {
            if (!node.value) {
              return {};
            }
            return type_expr(node.value);
          } else {
            if (!node.value || !node.count) {
              return {};
            }
            const auto value_type = type_expr(node.value);
            if (!value_type.ok) {
              return value_type;
            }
            if (!BitcopyType(ctx, value_type.type)) {
              ExprTypeResult repeat_result;
              repeat_result.diag_id = "E-UNS-0107";
              return repeat_result;
            }
            const auto count_type = type_expr(node.count);
            if (!count_type.ok) {
              return count_type;
            }
            const auto count_prim = expr::GetPrimName(count_type.type);
            if (!count_prim.has_value() ||
                (!expr::IsIntType(*count_prim) && *count_prim != "usize")) {
              ExprTypeResult repeat_result;
              repeat_result.diag_id = "E-TYP-1812";
              return repeat_result;
            }
            const auto len = ConstLen(ctx, node.count);
            if (!len.ok || !len.value.has_value()) {
              ExprTypeResult repeat_result;
              repeat_result.diag_id = len.diag_id.value_or("E-TYP-1812");
              return repeat_result;
            }
            total_length += *len.value;
            return value_type;
          }
        },
        segment);

    if (!segment_ok.ok) {
      result.diag_id = segment_ok.diag_id;
      return result;
    }

    if (!element_type) {
      element_type = segment_ok.type;
    } else {
      const auto equiv = TypeEquiv(element_type, segment_ok.type);
      if (!equiv.ok) {
        result.diag_id = equiv.diag_id;
        return result;
      }
      if (!equiv.equiv) {
        return result;
      }
    }

    if (std::holds_alternative<ast::ArrayElemSegment>(segment)) {
      total_length += 1;
    }
  }

  SPEC_RULE("T-Array-Literal-Segments");
  result.ok = true;
  result.type = MakeTypeArray(element_type, total_length);
  return result;
}

}  // namespace cursive::analysis::expr
