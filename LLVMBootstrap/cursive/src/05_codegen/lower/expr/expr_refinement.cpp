// =============================================================================
// Expression Lowering Refinement Helpers
// =============================================================================

#include "05_codegen/lower/expr/expr_common.h"

#include <variant>

#include "04_analysis/typing/type_expr.h"
#include "05_codegen/checks/checks.h"

namespace cursive::codegen {

namespace {

bool DynamicChecksEnabledForExpr(const ast::Expr& expr, bool inherited_dynamic) {
  (void)expr;
  return inherited_dynamic;
}

ast::ExprPtr MakeLoweringExpr(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

ast::ExprPtr SubstituteRefinementSelfWithResult(const ast::ExprPtr& expr) {
  if (!expr) {
    return expr;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    if (analysis::IdEq(ident->name, "self")) {
      return MakeLoweringExpr(expr->span, ast::ResultExpr{});
    }
    return expr;
  }

  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SubstituteRefinementSelfWithResult(node.lhs);
          out.rhs = SubstituteRefinementSelfWithResult(node.rhs);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteRefinementSelfWithResult(node.value);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteRefinementSelfWithResult(node.base);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteRefinementSelfWithResult(node.base);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteRefinementSelfWithResult(node.base);
          out.index = SubstituteRefinementSelfWithResult(node.index);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          out.callee = SubstituteRefinementSelfWithResult(node.callee);
          for (auto& arg : out.args) {
            arg.value = SubstituteRefinementSelfWithResult(arg.value);
          }
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto out = node;
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            auto paren = std::get<ast::ParenArgs>(node.args);
            for (auto& arg : paren.args) {
              arg.value = SubstituteRefinementSelfWithResult(arg.value);
            }
            out.args = std::move(paren);
          } else {
            auto brace = std::get<ast::BraceArgs>(node.args);
            for (auto& field : brace.fields) {
              field.value = SubstituteRefinementSelfWithResult(field.value);
            }
            out.args = std::move(brace);
          }
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          out.receiver = SubstituteRefinementSelfWithResult(node.receiver);
          for (auto& arg : out.args) {
            arg.value = SubstituteRefinementSelfWithResult(arg.value);
          }
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteRefinementSelfWithResult(node.value);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = node;
          out.lhs = SubstituteRefinementSelfWithResult(node.lhs);
          out.rhs = SubstituteRefinementSelfWithResult(node.rhs);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          auto out = node;
          out.expr = SubstituteRefinementSelfWithResult(node.expr);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = node;
          out.cond = SubstituteRefinementSelfWithResult(node.cond);
          out.then_expr = SubstituteRefinementSelfWithResult(node.then_expr);
          out.else_expr = SubstituteRefinementSelfWithResult(node.else_expr);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = node;
          out.scrutinee = SubstituteRefinementSelfWithResult(node.scrutinee);
          for (auto& case_clause : out.cases) {
            case_clause.body =
                SubstituteRefinementSelfWithResult(case_clause.body);
          }
          out.else_expr = SubstituteRefinementSelfWithResult(node.else_expr);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = node;
          out.scrutinee = SubstituteRefinementSelfWithResult(node.scrutinee);
          out.then_expr = SubstituteRefinementSelfWithResult(node.then_expr);
          out.else_expr = SubstituteRefinementSelfWithResult(node.else_expr);
          return MakeLoweringExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          auto out = node;
          out.expr = SubstituteRefinementSelfWithResult(node.expr);
          return MakeLoweringExpr(expr->span, out);
        } else {
          return expr;
        }
      },
      expr->node);
}

IRPtr EmitDynamicRefinementChecksImpl(const ast::Expr& expr,
                                      const IRValue& value,
                                      analysis::TypeRef value_type,
                                      LowerCtx& ctx) {
  const bool dynamic_checks_enabled =
      DynamicChecksEnabledForExpr(expr, ctx.dynamic_checks);
  if (!dynamic_checks_enabled || !ctx.dynamic_refine_checks) {
    return EmptyIR();
  }

  const auto it = ctx.dynamic_refine_checks->find(&expr);
  if (it == ctx.dynamic_refine_checks->end() || it->second.empty()) {
    return EmptyIR();
  }

  const analysis::TypeRef bool_type = analysis::MakeTypePrim("bool");
  std::vector<IRPtr> parts;
  for (const auto& refine_type : it->second) {
    if (!refine_type) {
      continue;
    }
    const auto* refine = std::get_if<analysis::TypeRefine>(&refine_type->node);
    if (!refine || !refine->predicate) {
      continue;
    }

    const auto predicate = SubstituteRefinementSelfWithResult(refine->predicate);
    if (!predicate) {
      continue;
    }

    const auto prev_result = ctx.contract_result_value;
    const auto prev_proc_ret_type = ctx.proc_ret_type;
    ctx.contract_result_value = value;
    ctx.proc_ret_type = value_type ? value_type : refine->base;
    auto pred_result = LowerExpr(*predicate, ctx);
    ctx.proc_ret_type = prev_proc_ret_type;
    ctx.contract_result_value = prev_result;

    ctx.RegisterValueType(pred_result.value, bool_type);

    IRIf check;
    check.cond = pred_result.value;
    check.then_ir = EmptyIR();
    check.else_ir = LowerContractViolation(ContractKind::TypeInv,
                                           ctx,
                                           refine->predicate.get(),
                                           expr.span);
    check.result = ctx.FreshTempValue("refine_check");
    ctx.RegisterValueType(check.result, bool_type);

    parts.push_back(pred_result.ir);
    parts.push_back(MakeIR(std::move(check)));
  }

  if (parts.empty()) {
    return EmptyIR();
  }
  return SeqIR(std::move(parts));
}

}  // namespace

IRPtr EmitDynamicRefinementChecksForExpr(const ast::Expr& expr,
                                        const IRValue& value,
                                        analysis::TypeRef value_type,
                                        LowerCtx& ctx) {
  return EmitDynamicRefinementChecksImpl(expr, value, value_type, ctx);
}

}  // namespace cursive::codegen
