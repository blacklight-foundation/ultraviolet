// =============================================================================
// MIGRATION MAPPING: expr/call.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16143-16151: (Lower-Expr-Call-PanicOut) and (Lower-Expr-Call-NoPanicOut)
//     With panic out: appends PanicOutName to args, adds PanicCheck after
//     Without panic out: direct call
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - CallExpr lowering with argument evaluation
//   - NeedsPanicOut check determines if panic out parameter needed
//
// DEPENDENCIES:
//   - cursive/src/05_codegen/ir/ir_model.h (IRCall, IRValue)
//   - cursive/src/05_codegen/abi/abi.h (NeedsPanicOut, kPanicOutName)
//   - cursive/src/05_codegen/checks/checks.h (PanicCheck)
//
// =============================================================================

#include "05_codegen/lower/expr/call.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/monomorphize.h"
#include "05_codegen/symbols/linkage.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/type_lower.h"
#include "05_codegen/intrinsics/builtins.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_expr.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/checks/panic.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/expr/closure_expr.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_proc.h"
#include "05_codegen/symbols/mangle.h"
namespace cursive::codegen {

namespace {

// =============================================================================
// Helper functions for call lowering
// =============================================================================

// Extract parameter modes from TypeFunc parameters
ParamModeList ParamModesFromFuncParams(const std::vector<analysis::TypeFuncParam>& params) {
  ParamModeList modes;
  modes.reserve(params.size());
  for (const auto& param : params) {
    modes.push_back(param.mode);
  }
  return modes;
}

// Extract parameter modes from AST Param declarations
ParamModeList ParamModesFromParams(const std::vector<ast::Param>& params) {
  ParamModeList modes;
  modes.reserve(params.size());
  for (const auto& param : params) {
    if (param.mode.has_value()) {
      modes.push_back(analysis::ParamMode::Move);
    } else {
      modes.push_back(std::nullopt);
    }
  }
  return modes;
}

IRValue BoolImmediate(bool value) {
  IRValue out;
  out.kind = IRValue::Kind::Immediate;
  out.name = value ? "true" : "false";
  out.bytes = {static_cast<std::uint8_t>(value ? 1 : 0)};
  return out;
}

IRValue NullOpaqueValue() {
  IRValue out;
  out.kind = IRValue::Kind::Opaque;
  out.name = "null";
  return out;
}

std::optional<ast::KeyMode> RequiredKeyModeForParamType(
    const analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  switch (analysis::PermOfType(type)) {
    case analysis::Permission::Unique:
      return ast::KeyMode::Write;
    case analysis::Permission::Shared:
    case analysis::Permission::Const:
      return ast::KeyMode::Read;
  }
  return std::nullopt;
}

bool UsesRawExportAbi(LowerCtx& ctx, const std::string& symbol) {
  return ctx.LookupExportUnwindMode(symbol).has_value();
}

bool ExternAbiUsesRawName(const std::optional<ast::ExternAbi>& abi_opt) {
  if (!abi_opt.has_value()) {
    return true;
  }

  const std::string abi = std::visit(
      [](const auto& abi_node) -> std::string {
        using T = std::decay_t<decltype(abi_node)>;
        if constexpr (std::is_same_v<T, ast::ExternAbiString>) {
          std::string text = abi_node.literal.lexeme;
          if (text.size() >= 2 &&
              ((text.front() == '"' && text.back() == '"') ||
               (text.front() == '\'' && text.back() == '\''))) {
            text = text.substr(1, text.size() - 2);
          }
          return text;
        } else {
          return abi_node.name;
        }
      },
      *abi_opt);
  return abi == "C" || abi == "C-unwind";
}

ast::ExprPtr MakeExprNode(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

const ast::ExprPtr* FindContractBindingReplacement(
    std::string_view ident,
    const std::vector<std::pair<std::string, ast::ExprPtr>>& bindings) {
  for (const auto& [name, value] : bindings) {
    if (analysis::IdEq(name, ident)) {
      return &value;
    }
  }
  return nullptr;
}

ast::ExprPtr SubstituteContractPredicate(
    const ast::ExprPtr& expr,
    const std::vector<std::pair<std::string, ast::ExprPtr>>& bindings) {
  if (!expr) {
    return expr;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    if (const auto* replacement =
            FindContractBindingReplacement(ident->name, bindings)) {
      return *replacement;
    }
    return expr;
  }
  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SubstituteContractPredicate(node.lhs, bindings);
          out.rhs = SubstituteContractPredicate(node.rhs, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteContractPredicate(node.value, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteContractPredicate(node.base, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteContractPredicate(node.base, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteContractPredicate(node.base, bindings);
          out.index = SubstituteContractPredicate(node.index, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          out.callee = SubstituteContractPredicate(node.callee, bindings);
          for (auto& arg : out.args) {
            arg.value = SubstituteContractPredicate(arg.value, bindings);
          }
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto out = node;
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            auto paren = std::get<ast::ParenArgs>(node.args);
            for (auto& arg : paren.args) {
              arg.value = SubstituteContractPredicate(arg.value, bindings);
            }
            out.args = paren;
          } else {
            auto brace = std::get<ast::BraceArgs>(node.args);
            for (auto& field : brace.fields) {
              field.value = SubstituteContractPredicate(field.value, bindings);
            }
            out.args = brace;
          }
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          out.receiver = SubstituteContractPredicate(node.receiver, bindings);
          for (auto& arg : out.args) {
            arg.value = SubstituteContractPredicate(arg.value, bindings);
          }
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteContractPredicate(node.value, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = node;
          out.lhs = SubstituteContractPredicate(node.lhs, bindings);
          out.rhs = SubstituteContractPredicate(node.rhs, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          auto out = node;
          out.expr = SubstituteContractPredicate(node.expr, bindings);
          return MakeExprNode(expr->span, out);
        } else {
          return expr;
        }
      },
      expr->node);
}

IRPtr EmitLocalPreDynamicChecks(const std::string& callee_symbol,
                                const ast::CallExpr& call_expr,
                                const std::vector<IRValue>& arg_values,
                                LowerCtx& ctx) {
  if (!ctx.dynamic_checks) {
    return EmptyIR();
  }
  const auto* local_info = ctx.LookupLocalContractInfo(callee_symbol);
  if (!local_info || !local_info->precondition) {
    return EmptyIR();
  }
  if (local_info->param_names.size() != call_expr.args.size() ||
      call_expr.args.size() != arg_values.size()) {
    return EmptyIR();
  }

  std::vector<std::pair<std::string, ast::ExprPtr>> ast_bindings;
  ast_bindings.reserve(call_expr.args.size());
  for (std::size_t i = 0; i < call_expr.args.size(); ++i) {
    ast_bindings.emplace_back(local_info->param_names[i], call_expr.args[i].value);
  }

  const auto pre_subst =
      SubstituteContractPredicate(local_info->precondition, ast_bindings);
  analysis::StaticProofContext proof_ctx;
  const auto proof = analysis::StaticProof(proof_ctx, pre_subst);
  if (proof.provable) {
    return EmptyIR();
  }

  const auto* sig = ctx.LookupProcSig(callee_symbol);
  if (!sig) {
    return EmptyIR();
  }
  const analysis::TypeRef bool_type = analysis::MakeTypePrim("bool");

  const auto prev_param_entry_values = ctx.contract_param_entry_values;
  const bool prev_lowering_contract_postcondition =
      ctx.lowering_contract_postcondition;
  ctx.lowering_contract_postcondition = true;

  std::vector<IRPtr> parts;
  for (std::size_t i = 0;
       i < local_info->param_names.size() && i < sig->params.size(); ++i) {
    IRValue param_value = arg_values[i];
    const bool param_by_ref = !sig->params[i].mode.has_value();
    if (param_by_ref && arg_values[i].kind != IRValue::Kind::Immediate) {
      IRReadPtr load_param;
      load_param.ptr = arg_values[i];
      load_param.result = ctx.FreshTempValue("local_pre_param");
      ctx.RegisterValueType(load_param.result, sig->params[i].type);
      param_value = load_param.result;
      parts.push_back(MakeIR(std::move(load_param)));
    } else {
      ctx.RegisterValueType(param_value, sig->params[i].type);
    }
    ctx.contract_param_entry_values[local_info->param_names[i]] = param_value;
  }

  auto pred_result = LowerExpr(*local_info->precondition, ctx);
  parts.push_back(pred_result.ir);
  ctx.RegisterValueType(pred_result.value, bool_type);

  IRUnaryOp not_pred;
  not_pred.op = "!";
  not_pred.operand = pred_result.value;
  not_pred.result = ctx.FreshTempValue("local_pre_not");
  ctx.RegisterValueType(not_pred.result, bool_type);
  IRValue not_pred_value = not_pred.result;
  parts.push_back(MakeIR(std::move(not_pred)));

  IRIf check;
  check.cond = not_pred_value;
  check.then_ir = LowerContractViolation(ContractKind::Pre,
                                         ctx,
                                         local_info->precondition.get(),
                                         local_info->precondition->span);
  check.else_ir = nullptr;
  check.result = ctx.FreshTempValue("local_pre_check");
  ctx.RegisterValueType(check.result, bool_type);
  parts.push_back(MakeIR(std::move(check)));

  ctx.contract_param_entry_values = prev_param_entry_values;
  ctx.lowering_contract_postcondition = prev_lowering_contract_postcondition;

  return SeqIR(std::move(parts));
}

IRPtr EmitForeignPreDynamicChecks(const std::string& callee_symbol,
                                  const std::vector<IRValue>& arg_values,
                                  LowerCtx& ctx) {
  const auto* foreign_info = ctx.LookupForeignContractInfo(callee_symbol);
  if (!foreign_info || !foreign_info->dynamic || foreign_info->assumes.empty()) {
    return EmptyIR();
  }
  const auto* sig = ctx.LookupProcSig(callee_symbol);
  if (!sig) {
    return EmptyIR();
  }

  const auto prev_param_entry_values = ctx.contract_param_entry_values;
  const bool prev_lowering_contract_postcondition =
      ctx.lowering_contract_postcondition;
  ctx.lowering_contract_postcondition = true;

  const analysis::TypeRef bool_type = analysis::MakeTypePrim("bool");
  std::vector<IRPtr> parts;
  for (std::size_t i = 0; i < sig->params.size() && i < arg_values.size(); ++i) {
    IRValue param_value = arg_values[i];
    const bool param_by_ref = !sig->params[i].mode.has_value();
    if (param_by_ref && arg_values[i].kind != IRValue::Kind::Immediate) {
      IRReadPtr load_param;
      load_param.ptr = arg_values[i];
      load_param.result = ctx.FreshTempValue("foreign_pre_param");
      ctx.RegisterValueType(load_param.result, sig->params[i].type);
      param_value = load_param.result;
      parts.push_back(MakeIR(std::move(load_param)));
    } else {
      ctx.RegisterValueType(param_value, sig->params[i].type);
    }
    ctx.contract_param_entry_values[sig->params[i].name] = param_value;
  }

  for (const auto& pred : foreign_info->assumes) {
    if (!pred) {
      continue;
    }
    auto pred_result = LowerExpr(*pred, ctx);
    parts.push_back(pred_result.ir);
    ctx.RegisterValueType(pred_result.value, bool_type);

    IRUnaryOp not_pred;
    not_pred.op = "!";
    not_pred.operand = pred_result.value;
    not_pred.result = ctx.FreshTempValue("foreign_pre_not");
    ctx.RegisterValueType(not_pred.result, bool_type);
    IRValue not_pred_value = not_pred.result;
    parts.push_back(MakeIR(std::move(not_pred)));

    IRIf check;
    check.cond = not_pred_value;
    check.then_ir = LowerContractViolation(ContractKind::ForeignPre,
                                           ctx,
                                           pred.get(),
                                           pred->span);
    check.else_ir = nullptr;
    check.result = ctx.FreshTempValue("foreign_pre_check");
    ctx.RegisterValueType(check.result, bool_type);
    parts.push_back(MakeIR(std::move(check)));
  }

  ctx.contract_param_entry_values = prev_param_entry_values;
  ctx.lowering_contract_postcondition = prev_lowering_contract_postcondition;

  if (parts.empty()) {
    return EmptyIR();
  }
  return SeqIR(std::move(parts));
}

IRPtr EmitForeignPostDynamicChecks(const std::string& callee_symbol,
                                   const IRValue& result_value,
                                   LowerCtx& ctx) {
  const auto* foreign_info = ctx.LookupForeignContractInfo(callee_symbol);
  if (!foreign_info || !foreign_info->dynamic) {
    return EmptyIR();
  }
  if (foreign_info->ensures.empty() && foreign_info->ensures_error.empty() &&
      foreign_info->ensures_null_result.empty()) {
    return EmptyIR();
  }

  const analysis::TypeRef bool_type = analysis::MakeTypePrim("bool");
  std::vector<IRPtr> parts;
  const auto prev_result = ctx.contract_result_value;
  ctx.contract_result_value = result_value;

  std::vector<IRValue> err_pred_values;
  err_pred_values.reserve(foreign_info->ensures_error.size());
  for (const auto& pred : foreign_info->ensures_error) {
    if (!pred) {
      continue;
    }
    auto pred_result = LowerExpr(*pred, ctx);
    parts.push_back(pred_result.ir);
    ctx.RegisterValueType(pred_result.value, bool_type);
    err_pred_values.push_back(pred_result.value);
  }

  IRValue err_cond = BoolImmediate(false);
  if (!err_pred_values.empty()) {
    err_cond = err_pred_values.front();
    for (std::size_t i = 1; i < err_pred_values.size(); ++i) {
      IRBinaryOp and_ir;
      and_ir.op = "&&";
      and_ir.lhs = err_cond;
      and_ir.rhs = err_pred_values[i];
      and_ir.result = ctx.FreshTempValue("foreign_err_cond_and");
      IRValue and_result = and_ir.result;
      ctx.RegisterValueType(and_ir.result, bool_type);
      parts.push_back(MakeIR(std::move(and_ir)));
      err_cond = and_result;
    }
  }

  IRUnaryOp success_not;
  success_not.op = "!";
  success_not.operand = err_cond;
  success_not.result = ctx.FreshTempValue("foreign_success_cond");
  ctx.RegisterValueType(success_not.result, bool_type);
  IRValue success_cond = success_not.result;
  parts.push_back(MakeIR(std::move(success_not)));

  std::optional<IRValue> null_cond;
  if (!foreign_info->ensures_null_result.empty()) {
    IRBinaryOp null_cmp;
    null_cmp.op = "==";
    null_cmp.lhs = result_value;
    null_cmp.rhs = NullOpaqueValue();
    null_cmp.result = ctx.FreshTempValue("foreign_null_cond");
    ctx.RegisterValueType(null_cmp.result, bool_type);
    null_cond = null_cmp.result;
    parts.push_back(MakeIR(std::move(null_cmp)));
  }

  auto append_guarded_checks = [&](const std::vector<ast::ExprPtr>& predicates,
                                   const IRValue& guard,
                                   std::string_view label_prefix) {
    for (const auto& pred : predicates) {
      if (!pred) {
        continue;
      }
      auto pred_result = LowerExpr(*pred, ctx);
      parts.push_back(pred_result.ir);
      ctx.RegisterValueType(pred_result.value, bool_type);

      IRUnaryOp not_pred;
      not_pred.op = "!";
      not_pred.operand = pred_result.value;
      not_pred.result = ctx.FreshTempValue(std::string(label_prefix) + "_not");
      ctx.RegisterValueType(not_pred.result, bool_type);
      IRValue not_pred_value = not_pred.result;
      parts.push_back(MakeIR(std::move(not_pred)));

      IRBinaryOp fail_and;
      fail_and.op = "&&";
      fail_and.lhs = guard;
      fail_and.rhs = not_pred_value;
      fail_and.result = ctx.FreshTempValue(std::string(label_prefix) + "_fail");
      ctx.RegisterValueType(fail_and.result, bool_type);
      IRValue fail_value = fail_and.result;
      parts.push_back(MakeIR(std::move(fail_and)));

      IRIf check;
      check.cond = fail_value;
      check.then_ir = LowerContractViolation(ContractKind::ForeignPost,
                                             ctx,
                                             pred.get(),
                                             pred->span);
      check.else_ir = nullptr;
      check.result = ctx.FreshTempValue(std::string(label_prefix) + "_check");
      ctx.RegisterValueType(check.result, bool_type);
      parts.push_back(MakeIR(std::move(check)));
    }
  };

  append_guarded_checks(foreign_info->ensures, success_cond, "foreign_post");
  if (null_cond.has_value()) {
    append_guarded_checks(foreign_info->ensures_null_result, *null_cond,
                          "foreign_null_post");
  }

  ctx.contract_result_value = prev_result;
  if (parts.empty()) {
    return EmptyIR();
  }
  return SeqIR(std::move(parts));
}

// Find the success member type in a union type for propagation
std::optional<analysis::TypeRef> SuccessMemberType(const analysis::ScopeContext& scope,
                                                    const analysis::TypeRef& ret_type,
                                                    const analysis::TypeRef& expr_type) {
  if (!ret_type || !expr_type) {
    return std::nullopt;
  }
  const auto* uni = std::get_if<analysis::TypeUnion>(&expr_type->node);
  if (!uni) {
    return std::nullopt;
  }

  std::optional<analysis::TypeRef> success;
  for (const auto& member : uni->members) {
    const auto sub = analysis::Subtyping(scope, member, ret_type);
    if (!sub.ok) {
      return std::nullopt;
    }
    if (!sub.subtype) {
      if (success.has_value()) {
        return std::nullopt;
      }
      success = member;
    }
  }
  if (!success.has_value()) {
    return std::nullopt;
  }
  for (const auto& member : uni->members) {
    if (member == *success) {
      continue;
    }
    const auto sub = analysis::Subtyping(scope, member, ret_type);
    if (!sub.ok || !sub.subtype) {
      return std::nullopt;
    }
  }
  return success;
}

// Info about a record constructor call
struct RecordCtorInfo {
  ast::Path path;
  const ast::RecordDecl* record = nullptr;
  bool synth_builtin_record_defaults = false;
};

// Attempt to resolve a call as a record constructor
std::optional<RecordCtorInfo> ResolveRecordCtor(const ast::ExprPtr& callee,
                                                 const std::vector<ast::Arg>& args,
                                                 LowerCtx& ctx) {
  if (!callee || !ctx.sigma || !args.empty()) {
    return std::nullopt;
  }

  auto lookup_record = [&](const ast::Path& path) -> const ast::RecordDecl* {
    const auto it = ctx.sigma->types.find(analysis::PathKeyOf(path));
    if (it == ctx.sigma->types.end()) {
      return nullptr;
    }
    return std::get_if<ast::RecordDecl>(&it->second);
  };

  auto make_info = [&](ast::Path path) -> std::optional<RecordCtorInfo> {
    if (const auto* record = lookup_record(path)) {
      return RecordCtorInfo{std::move(path), record};
    }
    return std::nullopt;
  };

  return std::visit(
      [&](const auto& node) -> std::optional<RecordCtorInfo> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (const auto builtin_path =
                  analysis::LookupBuiltinRecordCtorPath(node.name);
              builtin_path.has_value()) {
            if (auto info = make_info(*builtin_path)) {
              return info;
            }
            // Built-in record defaults are synthesized when the declaration
            // has not been materialized in the current lowering scope.
            RecordCtorInfo info;
            info.path = *builtin_path;
            info.synth_builtin_record_defaults = true;
            return info;
          }
          if (ctx.resolve_type_name) {
            if (auto resolved = ctx.resolve_type_name(node.name)) {
              if (!resolved->empty()) {
                return make_info(*resolved);
              }
            }
          }
          ast::Path path = ctx.module_path;
          path.emplace_back(node.name);
          return make_info(std::move(path));
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          ast::Path path = node.path;
          path.emplace_back(node.name);
          return make_info(std::move(path));
        }
        return std::nullopt;
      },
      callee->node);
}

struct GenericProcInfo {
  ast::Path full_path;
  ast::Path module_path;
  const ast::ProcedureDecl* decl = nullptr;
};

struct ProcedureCalleeInfo {
  std::string symbol;
  ast::ModulePath module_path;
  const ast::ProcedureDecl* proc = nullptr;
  const ast::ExternProcDecl* extern_proc = nullptr;
};

bool PathEq(const ast::Path& lhs, const ast::Path& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!analysis::IdEq(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

std::optional<ast::Path> ExtractCalleePath(const ast::ExprPtr& callee, LowerCtx& ctx) {
  if (!callee) {
    return std::nullopt;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<ast::Path> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (ctx.resolve_name) {
            if (auto resolved = ctx.resolve_name(node.name)) {
              if (!resolved->empty()) {
                return *resolved;
              }
            }
          }
          ast::Path path = ctx.module_path;
          path.push_back(node.name);
          return path;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          ast::Path path = node.path;
          path.push_back(node.name);
          return path;
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          ast::Path path = node.path;
          path.push_back(node.name);
          return path;
        }
        return std::nullopt;
      },
      callee->node);
}

std::optional<ProcedureCalleeInfo> ResolveProcedureCalleeInfo(
    const ast::ExprPtr& callee,
    LowerCtx& ctx) {
  if (!ctx.sigma || !callee) {
    return std::nullopt;
  }

  auto full_path_opt = ExtractCalleePath(callee, ctx);
  if (!full_path_opt.has_value() || full_path_opt->empty()) {
    return std::nullopt;
  }

  ast::Path full_path = *full_path_opt;
  ast::Path module_path = full_path;
  const std::string proc_name = module_path.back();
  module_path.pop_back();

  for (const auto& module : ctx.sigma->mods) {
    if (!PathEq(module.path, module_path)) {
      continue;
    }
    for (const auto& item : module.items) {
      const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
      if (!proc || !analysis::IdEq(proc->name, proc_name)) {
        continue;
      }
      return ProcedureCalleeInfo{
          MangleProc(module.path, *proc),
          module.path,
          proc,
          nullptr};
    }
    for (const auto& item : module.items) {
      const auto* ext = std::get_if<ast::ExternBlock>(&item);
      if (!ext) {
        continue;
      }
      for (const auto& ext_item : ext->items) {
        const auto* proc = std::get_if<ast::ExternProcDecl>(&ext_item);
        if (!proc || !analysis::IdEq(proc->name, proc_name)) {
          continue;
        }
        if (auto link_name = LinkName(proc->attrs, proc->name)) {
          return ProcedureCalleeInfo{*link_name, module.path, nullptr, proc};
        }
        if (ExternAbiUsesRawName(ext->abi_opt)) {
          return ProcedureCalleeInfo{proc->name, module.path, nullptr, proc};
        }
        ast::Path path = module.path;
        path.push_back(proc->name);
        return ProcedureCalleeInfo{ScopedSym(path), module.path, nullptr, proc};
      }
    }
  }

  return std::nullopt;
}

std::optional<std::string> ResolveProcedureSymbolForCallee(
    const ast::ExprPtr& callee,
    LowerCtx& ctx) {
  const auto info = ResolveProcedureCalleeInfo(callee, ctx);
  if (!info.has_value()) {
    return std::nullopt;
  }
  return info->symbol;
}

std::optional<GenericProcInfo> ResolveGenericProcedure(const ast::CallExpr& expr,
                                                       LowerCtx& ctx) {
  if (!ctx.sigma || !expr.callee) {
    return std::nullopt;
  }

  auto full_path_opt = ExtractCalleePath(expr.callee, ctx);
  if (!full_path_opt.has_value() || full_path_opt->empty()) {
    return std::nullopt;
  }

  ast::Path full_path = *full_path_opt;
  ast::Path module_path = full_path;
  const std::string proc_name = module_path.back();
  module_path.pop_back();

  for (const auto& module : ctx.sigma->mods) {
    if (!PathEq(module.path, module_path)) {
      continue;
    }
    for (const auto& item : module.items) {
      const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
      if (!proc || !analysis::IdEq(proc->name, proc_name)) {
        continue;
      }
      if (!proc->generic_params.has_value() || proc->generic_params->params.empty()) {
        return std::nullopt;
      }
      return GenericProcInfo{std::move(full_path), std::move(module_path), proc};
    }
    return std::nullopt;
  }

  return std::nullopt;
}

analysis::TypeRef InstantiateActiveGenericType(const analysis::TypeRef& type,
                                               const LowerCtx& ctx) {
  if (!type || !ctx.active_generic_type_subst.has_value() ||
      ctx.active_generic_type_subst->empty()) {
    return type;
  }
  return analysis::InstantiateType(type, *ctx.active_generic_type_subst);
}

analysis::ScopeContext SourceCallScope(const ast::ModulePath& module_path,
                                       const LowerCtx& ctx) {
  analysis::ScopeContext scope;
  if (ctx.sigma) {
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
  }
  scope.current_module = module_path;
  scope.target_profile = ctx.target_profile;
  scope.expr_types = ctx.expr_types;
  scope.dynamic_refine_checks = ctx.dynamic_refine_checks;
  scope.generic_call_substs = ctx.generic_call_substs;
  return scope;
}

analysis::TypeRef LowerSourceCallType(
    const analysis::ScopeContext& scope,
    const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return analysis::MakeTypePrim("()");
  }
  if (const auto lowered =
          ::cursive::analysis::layout::LowerTypeForLayout(scope, type)) {
    return *lowered;
  }
  const auto fallback = analysis::LowerType(scope, type);
  if (fallback.ok && fallback.type) {
    return fallback.type;
  }
  return analysis::MakeTypePrim("()");
}

template <typename ProcDeclT>
bool PopulateSourceCallSignature(const ProcDeclT& decl,
                                 const ast::ModulePath& module_path,
                                 LowerCtx& ctx,
                                 ParamModeList& param_modes,
                                 ParamTypeList& param_types,
                                 analysis::TypeRef& result_type) {
  const analysis::ScopeContext scope = SourceCallScope(module_path, ctx);
  ParamModeList modes;
  ParamTypeList types;
  modes.reserve(decl.params.size());
  types.reserve(decl.params.size());
  for (const auto& param : decl.params) {
    modes.push_back(param.mode.has_value()
                        ? std::optional<analysis::ParamMode>(
                              analysis::ParamMode::Move)
                        : std::nullopt);
    types.push_back(InstantiateActiveGenericType(
        LowerSourceCallType(scope, param.type),
        ctx));
  }
  param_modes = std::move(modes);
  param_types = std::move(types);
  result_type = InstantiateActiveGenericType(
      LowerSourceCallType(scope, decl.return_type_opt),
      ctx);
  return true;
}

void RegisterResolvedSourceSignatureIfMissing(
    const ProcedureCalleeInfo& info,
    LowerCtx& ctx) {
  if (ctx.LookupProcSig(info.symbol)) {
    return;
  }
  if (!info.proc) {
    return;
  }

  ParamModeList param_modes;
  ParamTypeList param_types;
  analysis::TypeRef result_type;
  const bool populated = PopulateSourceCallSignature(*info.proc,
                                                     info.module_path,
                                                     ctx,
                                                     param_modes,
                                                     param_types,
                                                     result_type);
  if (!populated) {
    return;
  }

  ProcIR proc;
  proc.symbol = info.symbol;
  proc.ret = result_type ? result_type : analysis::MakeTypePrim("()");
  proc.params.reserve(param_modes.size() + 1);
  for (std::size_t i = 0; i < param_modes.size(); ++i) {
    IRParam param;
    param.mode = param_modes[i];
    param.name = "arg" + std::to_string(i);
    if (info.proc && i < info.proc->params.size()) {
      param.name = info.proc->params[i].name;
    } else if (info.extern_proc && i < info.extern_proc->params.size()) {
      param.name = info.extern_proc->params[i].name;
    }
    param.type = i < param_types.size() && param_types[i]
                     ? param_types[i]
                     : analysis::MakeTypePrim("()");
    proc.params.push_back(std::move(param));
  }
  if (ctx.NeedsPanicOutForSymbol(info.symbol)) {
    proc.params.push_back(PanicOutParam());
  }
  ctx.RegisterProcSig(proc);
}

std::optional<analysis::TypeSubst> LookupGenericSubstForCall(
    const ast::CallExpr& expr,
    const GenericProcInfo& info,
    LowerCtx& ctx) {
  if (!info.decl || !info.decl->generic_params.has_value() ||
      !ctx.generic_call_substs) {
    return std::nullopt;
  }

  const auto& type_params = info.decl->generic_params->params;
  auto it = ctx.generic_call_substs->find(&expr);
  if (it == ctx.generic_call_substs->end()) {
    return std::nullopt;
  }

  analysis::TypeSubst subst = it->second;
  for (auto& entry : subst) {
    entry.second = InstantiateActiveGenericType(entry.second, ctx);
  }

  for (const auto& param : type_params) {
    const auto subst_it = subst.find(param.name);
    if (subst_it == subst.end() || !subst_it->second) {
      return std::nullopt;
    }
  }

  return subst;
}

std::string MonomorphizedProcSymbol(const GenericProcInfo& info,
                                    const analysis::TypeSubst& subst) {
  std::vector<std::string> inst_path = info.full_path;
  inst_path.push_back("inst");
  if (info.decl && info.decl->generic_params.has_value()) {
    for (const auto& param : info.decl->generic_params->params) {
      const auto it = subst.find(param.name);
      if (it != subst.end() && it->second) {
        inst_path.push_back(analysis::TypeToString(it->second));
      } else {
        inst_path.push_back("_");
      }
    }
  }
  return ScopedSym(inst_path);
}

void RegisterProvisionalGenericProcSig(const GenericProcInfo& info,
                                       const analysis::TypeSubst& subst,
                                       const std::string& symbol,
                                       LowerCtx& ctx) {
  if (ctx.LookupProcSig(symbol) || !info.decl) {
    return;
  }

  ProcIR provisional;
  provisional.symbol = symbol;
  const auto instantiate = [&](const analysis::TypeRef& type) -> analysis::TypeRef {
    return analysis::InstantiateType(type, subst);
  };

  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  for (const auto& param : info.decl->params) {
    IRParam ir_param;
    ir_param.mode = param.mode.has_value()
                        ? std::optional<analysis::ParamMode>(analysis::ParamMode::Move)
                        : std::nullopt;
    ir_param.name = param.name;

    if (param.type) {
      if (const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, param.type)) {
        ir_param.type = instantiate(*lowered);
      } else {
        const auto fallback = analysis::LowerType(scope, param.type);
        if (fallback.ok) {
          ir_param.type = instantiate(fallback.type);
        }
      }
    }
    if (!ir_param.type) {
      ir_param.type = analysis::MakeTypePrim("()");
    }
    provisional.params.push_back(std::move(ir_param));
  }

  provisional.ret = analysis::MakeTypePrim("()");
  if (info.decl->return_type_opt) {
    if (const auto lowered_ret = ::cursive::analysis::layout::LowerTypeForLayout(scope, info.decl->return_type_opt)) {
      provisional.ret = instantiate(*lowered_ret);
    } else {
      const auto fallback_ret = analysis::LowerType(scope, info.decl->return_type_opt);
      if (fallback_ret.ok && fallback_ret.type) {
        provisional.ret = instantiate(fallback_ret.type);
      }
    }
  }

  if (ctx.NeedsPanicOutForSymbol(symbol)) {
    provisional.params.push_back(PanicOutParam());
  }

  ctx.RegisterProcSig(provisional);
}

}  // namespace

// Materialize a call temporary for a non-`move` argument without source provenance
// so lowering can pass an addressable value to the callee.
LowerResult LowerRefArgExprWithTemp(const ast::ExprPtr& expr,
                                    std::string_view temp_prefix,
                                    const analysis::TypeRef& expected_type,
                                    LowerCtx& ctx) {
  if (!expr) {
    return LowerResult{EmptyIR(), IRValue{}};
  }

  if (analysis::HasSourceProvenance(expr)) {
    return LowerAddrOf(*expr, ctx);
  }

  auto prev_suppress = ctx.suppress_temp_at_depth;
  ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
  auto value_result = LowerExpr(*expr, ctx);
  ctx.suppress_temp_at_depth = prev_suppress;
  std::string temp_name = ctx.FreshTempValue(temp_prefix).name;

  analysis::TypeRef temp_type =
      InstantiateActiveGenericType(expected_type, ctx);
  if (!temp_type) {
    temp_type = ctx.LookupValueType(value_result.value);
    if (!temp_type && ctx.expr_type) {
      temp_type = ctx.expr_type(*expr);
    }
    temp_type = InstantiateActiveGenericType(temp_type, ctx);
  }
  if (!temp_type) {
    temp_type = analysis::MakeTypePrim("()");
  }

  IRBindVar bind;
  bind.name = temp_name;
  bind.value = value_result.value;
  bind.type = temp_type;
  bind.prov = analysis::ProvenanceKind::Stack;

  ctx.RegisterVar(temp_name, temp_type, false, false,
                  analysis::ProvenanceKind::Stack, std::nullopt);
  bind.stable_name = ctx.StableBindingName(temp_name);

  IRValue temp_value;
  temp_value.kind = IRValue::Kind::Local;
  temp_value.name = temp_name;
  if (temp_type) {
    ctx.RegisterValueType(temp_value, temp_type);
    ctx.RegisterTempValue(temp_value, temp_type);
  }

  ast::Expr temp_ident;
  temp_ident.span = expr->span;
  temp_ident.node = ast::IdentifierExpr{temp_name};
  auto addr_result = LowerAddrOf(temp_ident, ctx);

  return LowerResult{SeqIR({value_result.ir, MakeIR(std::move(bind)), addr_result.ir}),
                     addr_result.value};
}

LowerResult LowerMoveArgExprWithTemp(const ast::Arg& arg,
                                     std::string_view temp_prefix,
                                     const analysis::TypeRef& expected_type,
                                     LowerCtx& ctx) {
  if (!arg.value) {
    return LowerResult{EmptyIR(), IRValue{}};
  }

  if (!analysis::UsesCallTempForConsuming(
          std::optional<analysis::ParamMode>(analysis::ParamMode::Move), arg)) {
    auto moved_expr = analysis::MovedArgExpr(arg);
    auto prev_suppress = ctx.suppress_temp_at_depth;
    ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    auto result = LowerExpr(*moved_expr, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;
    return result;
  }

  auto prev_suppress = ctx.suppress_temp_at_depth;
  ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
  auto value_result = LowerExpr(*arg.value, ctx);
  ctx.suppress_temp_at_depth = prev_suppress;
  std::string temp_name = ctx.FreshTempValue(temp_prefix).name;

  analysis::TypeRef temp_type =
      InstantiateActiveGenericType(expected_type, ctx);
  if (!temp_type) {
    temp_type = ctx.LookupValueType(value_result.value);
    if (!temp_type && ctx.expr_type) {
      temp_type = ctx.expr_type(*arg.value);
    }
    temp_type = InstantiateActiveGenericType(temp_type, ctx);
  }
  if (!temp_type) {
    temp_type = analysis::MakeTypePrim("()");
  }

  IRBindVar bind;
  bind.name = temp_name;
  bind.value = value_result.value;
  bind.type = temp_type;
  bind.prov = analysis::ProvenanceKind::Stack;

  ctx.RegisterVar(temp_name, temp_type, false, false,
                  analysis::ProvenanceKind::Stack, std::nullopt);
  bind.stable_name = ctx.StableBindingName(temp_name);

  ast::Expr temp_ident;
  temp_ident.span = arg.value->span;
  temp_ident.node = ast::IdentifierExpr{temp_name};
  auto move_result = LowerMovePlace(temp_ident, ctx);

  return LowerResult{
      SeqIR({value_result.ir, MakeIR(std::move(bind)), move_result.ir}),
      move_result.value};
}

// =============================================================================
// Section 6.4 LowerArgs - lower call arguments (LTR order)
// =============================================================================
//
// (Lower-Args-Empty)
// params = []    args = []
// -------------------------
// Gamma |- LowerArgs(params, args) => <epsilon, []>
//
// (Lower-Args-Cons-Move)
// mode_i = Move    Gamma |- LowerExpr(move arg_i) => <IR_i, v_i>
// ---------------------------------------------------------------
// Gamma |- LowerArgs([...], [...]) => <SeqIR(IR_1..IR_n), [v_1..v_n]>
//
// (Lower-Args-Cons-Ref)
// mode_i != Move    Gamma |- LowerAddrOf(arg_i) => <IR_i, v_i>
// -------------------------------------------------------------
// Gamma |- LowerArgs([...], [...]) => <SeqIR(IR_1..IR_n), [v_1..v_n]>

std::pair<IRPtr, std::vector<IRValue>> LowerArgs(
    const ParamModeList& params,
    const std::vector<ast::Arg>& args,
    LowerCtx& ctx,
    const ParamTypeList* param_types) {
  if (params.empty() && args.empty()) {
    SPEC_RULE("Lower-Args-Empty");
    return {EmptyIR(), {}};
  }

  std::vector<IRPtr> ir_parts;
  std::vector<IRValue> values;

  const bool use_params = params.size() == args.size();
  const bool use_types =
      param_types && param_types->size() == args.size();
  if (!use_params || (param_types && !use_types)) {
    ctx.ReportCodegenFailure();
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (!args[i].value) {
      continue;
    }

    analysis::TypeRef expected_type = nullptr;
    if (use_types && i < param_types->size()) {
      expected_type = (*param_types)[i];
    }
    IRPtr key_ir = EmptyIR();
    if (const auto key_mode = RequiredKeyModeForParamType(expected_type)) {
      key_ir = LowerImplicitKeyAccess(*args[i].value, *key_mode, ctx);
    }

    if (!use_params) {
      continue;
    }

    if (params[i].has_value()) {
      SPEC_RULE("Lower-Args-Cons-Move");
      auto result =
          LowerMoveArgExprWithTemp(args[i], "call_move_tmp", expected_type, ctx);
      ir_parts.push_back(SeqIR({key_ir, result.ir}));
      values.push_back(result.value);
      continue;
    }

    SPEC_RULE("Lower-Args-Cons-Ref");
    auto result =
        LowerRefArgExprWithTemp(args[i].value, "call_ref_tmp", expected_type, ctx);
    ir_parts.push_back(SeqIR({key_ir, result.ir}));
    values.push_back(result.value);
  }

  return {SeqIR(std::move(ir_parts)), std::move(values)};
}

// =============================================================================
// Section 6.4 LowerCallExpr - lower call expression
// =============================================================================
//
// (Lower-Expr-Call-PanicOut)
// Gamma |- LowerExpr(callee) => <IR_c, v_c>
// Gamma |- LowerArgs(Params(Call(callee, args)), args) => <IR_a, vec_v>
// NeedsPanicOut(callee)
// -----------------------------------------------------------------------------------------
// Gamma |- LowerExpr(Call(callee, args)) => <SeqIR(IR_c, IR_a, CallIR(v_c, vec_v ++ [PanicOutName]), PanicCheck), v_call>
//
// (Lower-Expr-Call-NoPanicOut)
// Gamma |- LowerExpr(callee) => <IR_c, v_c>
// Gamma |- LowerArgs(Params(Call(callee, args)), args) => <IR_a, vec_v>
// not NeedsPanicOut(callee)
// -----------------------------------------------------------------------------------------
// Gamma |- LowerExpr(Call(callee, args)) => <SeqIR(IR_c, IR_a, CallIR(v_c, vec_v)), v_call>

LowerResult LowerCallExpr(const ast::Expr& expr_wrapper,
                          const ast::CallExpr& expr,
                          LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Call-PanicOut");
  SPEC_RULE("Lower-Expr-Call-NoPanicOut");

  // Check if this is a record constructor call (zero-arg call to record type)
  if (auto record_info = ResolveRecordCtor(expr.callee, expr.args, ctx)) {
    SPEC_RULE("Lower-CallIR-RecordCtor");
    if (!record_info->record && !record_info->synth_builtin_record_defaults) {
      ctx.ReportCodegenFailure();
      IRValue bad = ctx.FreshTempValue("record_ctor_err");
      return LowerResult{EmptyIR(), bad};
    }

    std::vector<ast::FieldInit> field_inits;
    if (record_info->record) {
      for (const auto& member : record_info->record->members) {
        const auto* field = std::get_if<ast::FieldDecl>(&member);
        if (!field) {
          continue;
        }
        if (!field->init_opt) {
          ctx.ReportCodegenFailure();
          IRValue bad = ctx.FreshTempValue("record_ctor_err");
          return LowerResult{EmptyIR(), bad};
        }
        ast::FieldInit init;
        init.name = field->name;
        init.value = field->init_opt;
        init.span = field->span;
        field_inits.push_back(std::move(init));
      }
    }

    if (record_info->synth_builtin_record_defaults && field_inits.empty()) {
      auto make_literal = [](ast::TokenKind kind, std::string lexeme) -> ast::ExprPtr {
        auto expr = std::make_shared<ast::Expr>();
        ast::LiteralExpr lit{};
        lit.literal.kind = kind;
        lit.literal.lexeme = std::move(lexeme);
        lit.literal.span = core::Span{};
        expr->span = core::Span{};
        expr->node = std::move(lit);
        return expr;
      };

      ast::FieldInit stack_size{};
      stack_size.name = "stack_size";
      stack_size.value = make_literal(ast::TokenKind::IntLiteral, "0usize");
      stack_size.span = core::Span{};
      field_inits.push_back(std::move(stack_size));

      ast::FieldInit name{};
      name.name = "name";
      name.value = make_literal(ast::TokenKind::StringLiteral, "\"\"");
      name.span = core::Span{};
      field_inits.push_back(std::move(name));
    }

    std::vector<std::string> saved_module = ctx.module_path;
    if (!record_info->path.empty()) {
      ctx.module_path = record_info->path;
      ctx.module_path.pop_back();
    }

    auto [ir, field_values] = LowerFieldInits(field_inits, ctx, true);

    ctx.module_path = std::move(saved_module);

    IRValue record_value = RegisterLoweredRecordValue(
        std::move(field_values),
        record_info->path.empty()
            ? std::optional<analysis::TypeRef>{}
            : std::optional<analysis::TypeRef>{
                  analysis::MakeTypePath(record_info->path)},
        "record_ctor",
        ctx);
    return LowerResult{ir, record_value};
  }

  if (expr.callee && ctx.expr_type) {
    analysis::TypeRef callee_type =
        analysis::StripPerm(ctx.expr_type(*expr.callee));
    if (!callee_type) {
      callee_type = ctx.expr_type(*expr.callee);
    }
    if (callee_type &&
        std::holds_alternative<analysis::TypeClosure>(callee_type->node)) {
      return LowerClosureCall(*expr.callee, expr.args, ctx);
    }
  }

  const bool debug_call = core::IsDebugEnabled("codegen");
  auto log_call_stage = [&](std::string_view stage) {
    if (!debug_call) {
      return;
    }
    std::cerr << "[lower-call-debug] stage=" << stage
              << " args=" << expr.args.size()
              << " generic_args=" << expr.generic_args.size() << "\n";
  };

  // Generic procedure call: instantiate a monomorphic procedure for this
  // call-site substitution, then lower as a direct symbol call.
  log_call_stage("resolve-generic-start");
  if (auto generic_info = ResolveGenericProcedure(expr, ctx)) {
    log_call_stage("resolve-generic-found");
    log_call_stage("lookup-generic-subst-start");
    if (auto subst = LookupGenericSubstForCall(expr, *generic_info, ctx)) {
      log_call_stage("lookup-generic-subst-finish");
      const std::string inst_symbol = MonomorphizedProcSymbol(*generic_info, *subst);
      auto log_generic = [&](std::string_view stage) {
        if (!debug_call) {
          return;
        }
        std::cerr << "[lower-call-generic-debug] callee=" << inst_symbol
                  << " stage=" << stage << "\n";
      };
      log_generic("start");

      if (!ctx.LookupProcSig(inst_symbol)) {
        RegisterProvisionalGenericProcSig(*generic_info, *subst, inst_symbol, ctx);
      }

      if (ctx.generic_instantiation_in_progress.find(inst_symbol) ==
          ctx.generic_instantiation_in_progress.end()) {
        if (ctx.generic_instantiation_stack.size() >=
            analysis::MonomorphizeContext::kMaxDepth) {
          log_generic("instantiate-depth-limit");
          ctx.ReportCodegenFailure();
        } else if (!ctx.LookupProcModule(inst_symbol)) {
          log_generic("instantiate-start");
          ctx.generic_instantiation_in_progress.insert(inst_symbol);
          ctx.generic_instantiation_stack.push_back(inst_symbol);
          ProcIR inst_proc = LowerProcInstantiated(
              *generic_info->decl, generic_info->module_path, inst_symbol, *subst, ctx);
          ctx.generic_instantiation_stack.pop_back();
          ctx.generic_instantiation_in_progress.erase(inst_symbol);
          if (generic_info->decl->contract.has_value() &&
              generic_info->decl->contract->precondition) {
            LowerCtx::LocalContractInfo local_contract;
            local_contract.precondition =
                generic_info->decl->contract->precondition;
            local_contract.param_names.reserve(generic_info->decl->params.size());
            for (const auto& param : generic_info->decl->params) {
              local_contract.param_names.push_back(param.name);
            }
            ctx.RegisterLocalContractInfo(inst_proc.symbol,
                                          std::move(local_contract));
          }
          ctx.QueueExtraProc(std::move(inst_proc),
                             LinkageOf(*generic_info->decl),
                             &generic_info->module_path);
          log_generic("instantiate-finish");
        } else {
          log_generic("instantiate-skip-module-registered");
        }
      } else {
        log_generic("instantiate-skip-in-progress");
      }

      const bool needs_panic_out = ctx.NeedsPanicOutForSymbol(inst_symbol);

      ParamModeList param_modes;
      ParamTypeList param_types;
      analysis::TypeRef result_type;
      if (const auto* inst_sig = ctx.LookupProcSig(inst_symbol)) {
        param_modes.reserve(inst_sig->params.size());
        param_types.reserve(inst_sig->params.size());
        for (const auto& param : inst_sig->params) {
          param_modes.push_back(param.mode);
          param_types.push_back(param.type);
        }
        // Proc signatures include hidden panic-out when required. It is not
        // part of source-level call arguments and is appended separately below.
        if (!inst_sig->params.empty() &&
            inst_sig->params.back().name == std::string(kPanicOutName) &&
            param_modes.size() == expr.args.size() + 1) {
          param_modes.pop_back();
          param_types.pop_back();
        }
        if (inst_sig->ffi_import || UsesRawExportAbi(ctx, inst_symbol)) {
          for (auto& mode : param_modes) {
            mode = analysis::ParamMode::Move;
          }
        }
        result_type = inst_sig->ret;
      } else {
        param_modes = ParamModesFromParams(generic_info->decl->params);
      }
      auto [args_ir, arg_values] =
          LowerArgs(param_modes,
                    expr.args,
                    ctx,
                    param_types.empty() ? nullptr : &param_types);
      log_generic("args-lowered");

      IRValue result_value = ctx.FreshTempValue("call");
      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = inst_symbol;
      call.args = std::move(arg_values);
      call.result = result_value;
      if (result_type) {
        ctx.RegisterValueType(result_value, result_type);
      }
      IRPtr local_pre_ir =
          EmitLocalPreDynamicChecks(inst_symbol, expr, call.args, ctx);
      auto is_noop = [](const IRPtr& ir) {
        return !ir || std::holds_alternative<IROpaque>(ir->node);
      };

      if (needs_panic_out) {
        IRValue panic_out;
        panic_out.kind = IRValue::Kind::Local;
        panic_out.name = std::string(kPanicOutName);
        call.args.push_back(panic_out);
        std::vector<IRPtr> parts;
        parts.push_back(args_ir);
        if (!is_noop(local_pre_ir)) {
          parts.push_back(local_pre_ir);
        }
        parts.push_back(MakeIR(std::move(call)));
        parts.push_back(PanicFollowup(ctx));
        return LowerResult{SeqIR(std::move(parts)), result_value};
      }

      std::vector<IRPtr> parts;
      parts.push_back(args_ir);
      if (!is_noop(local_pre_ir)) {
        parts.push_back(local_pre_ir);
      }
      parts.push_back(MakeIR(std::move(call)));
      return LowerResult{SeqIR(std::move(parts)), result_value};
    }
    log_call_stage("lookup-generic-subst-miss");
    ctx.ReportCodegenFailure();
    return LowerResult{EmptyIR(), ctx.FreshTempValue("generic_call_subst_err")};
  }
  log_call_stage("resolve-generic-miss");

  // Lower the callee expression
  auto callee_result = LowerExpr(*expr.callee, ctx);
  std::string callee_lookup_symbol = callee_result.value.name;
  std::optional<ProcedureCalleeInfo> resolved_proc_info;
  if (callee_result.value.kind == IRValue::Kind::Symbol) {
    resolved_proc_info = ResolveProcedureCalleeInfo(expr.callee, ctx);
    if (resolved_proc_info.has_value()) {
      callee_lookup_symbol = resolved_proc_info->symbol;
      RegisterResolvedSourceSignatureIfMissing(*resolved_proc_info, ctx);
    }
  }

  analysis::TypeRef contextual_result_type;
  if (ctx.expr_type) {
    contextual_result_type = ctx.expr_type(expr_wrapper);
    contextual_result_type =
        InstantiateActiveGenericType(contextual_result_type, ctx);
  }
  analysis::TypeRef result_type;

  // Extract parameter modes from the callee type if available
  ParamModeList param_modes;
  ParamTypeList param_types;
  analysis::TypeRef callee_type = nullptr;
  if (ctx.expr_type) {
    callee_type = ctx.expr_type(*expr.callee);
  }
  if (!callee_type) {
    callee_type = ctx.LookupValueType(callee_result.value);
  }
  callee_type = InstantiateActiveGenericType(callee_type, ctx);
  if (callee_type) {
    auto stripped = analysis::StripPerm(callee_type);
    if (stripped) {
      if (const auto* func = std::get_if<analysis::TypeFunc>(&stripped->node)) {
        param_modes.reserve(func->params.size());
        param_types.reserve(func->params.size());
        for (const auto& param : func->params) {
          param_modes.push_back(param.mode);
          param_types.push_back(param.type);
        }
        result_type = func->ret;
      } else if (const auto* closure = std::get_if<analysis::TypeClosure>(&stripped->node)) {
        param_modes.reserve(closure->params.size());
        param_types.reserve(closure->params.size());
        for (const auto& param : closure->params) {
          param_modes.push_back(
              param.first ? std::optional<analysis::ParamMode>(analysis::ParamMode::Move)
                          : std::nullopt);
          param_types.push_back(param.second);
        }
        result_type = closure->ret;
      }
    }
  }

  // Concrete procedure symbols have a canonical registered signature in the
  // lowering context. Use it as the source of truth for parameter and return
  // types, excluding the hidden panic-out parameter from source-argument
  // matching.
  if (callee_result.value.kind == IRValue::Kind::Symbol) {
    const LowerCtx::ProcSigInfo* sig = ctx.LookupProcSig(callee_lookup_symbol);
    if (!sig) {
      sig = ctx.LookupProcSig(callee_result.value.name);
    }
    if (sig) {
      ParamModeList sig_param_modes;
      ParamTypeList sig_param_types;
      sig_param_modes.reserve(sig->params.size());
      sig_param_types.reserve(sig->params.size());
      for (const auto& param : sig->params) {
        if (param.name == std::string(kPanicOutName)) {
          continue;
        }
        sig_param_modes.push_back(param.mode);
        sig_param_types.push_back(param.type);
      }
      if (sig_param_modes.size() == expr.args.size() || param_modes.empty()) {
        param_modes = std::move(sig_param_modes);
        param_types = std::move(sig_param_types);
      }
      result_type = sig->ret;
    }
  }

  if (callee_result.value.kind == IRValue::Kind::Symbol) {
    if (const auto* sig = ctx.LookupProcSig(callee_lookup_symbol);
        sig && (sig->ffi_import ||
                UsesRawExportAbi(ctx, callee_lookup_symbol))) {
      ParamModeList sig_param_modes;
      ParamTypeList sig_param_types;
      sig_param_modes.reserve(sig->params.size());
      sig_param_types.reserve(sig->params.size());
      for (const auto& param : sig->params) {
        if (param.name == std::string(kPanicOutName)) {
          continue;
        }
        sig_param_modes.push_back(analysis::ParamMode::Move);
        sig_param_types.push_back(param.type);
      }
      if (sig_param_modes.size() == expr.args.size() || param_modes.empty()) {
        param_modes = std::move(sig_param_modes);
        param_types = std::move(sig_param_types);
      }
      result_type = sig->ret;
    }
  }
  if (!result_type) {
    result_type = contextual_result_type;
  }
  const bool source_params_mismatch =
      param_modes.size() != expr.args.size() ||
      (!param_types.empty() && param_types.size() != expr.args.size());
  if (source_params_mismatch && resolved_proc_info.has_value()) {
    if (resolved_proc_info->proc) {
      PopulateSourceCallSignature(*resolved_proc_info->proc,
                                  resolved_proc_info->module_path,
                                  ctx,
                                  param_modes,
                                  param_types,
                                  result_type);
    } else if (resolved_proc_info->extern_proc) {
      PopulateSourceCallSignature(*resolved_proc_info->extern_proc,
                                  resolved_proc_info->module_path,
                                  ctx,
                                  param_modes,
                                  param_types,
                                  result_type);
    }
  }
  // Lower arguments
  auto [args_ir, arg_values] =
      LowerArgs(param_modes,
                expr.args,
                ctx,
                param_types.empty() ? nullptr : &param_types);

  IRValue result_value = ctx.FreshTempValue("call");

  IRCall call;
  call.callee = callee_result.value;
  if (call.callee.kind == IRValue::Kind::Symbol &&
      !callee_lookup_symbol.empty()) {
    call.callee.name = callee_lookup_symbol;
  }
  call.args = std::move(arg_values);
  call.result = result_value;
  if (result_type) {
    ctx.RegisterValueType(result_value, result_type);
  }

  // Determine if we need to add panic out parameter
  bool needs_panic_out = true;
  if (call.callee.kind == IRValue::Kind::Symbol) {
    needs_panic_out = ctx.NeedsPanicOutForSymbol(callee_lookup_symbol);
    if (needs_panic_out) {
      // Check for builtin symbols that don't need panic out
      auto check_builtin = [&](const std::vector<std::string>& full_path,
                               const std::vector<std::string>& container_path) {
        if (full_path.empty()) {
          return;
        }
        const std::string qualified = core::StringOfPath(full_path);
        const std::string builtin = BuiltinSym(qualified);
        const bool is_string_bytes =
            container_path.size() == 1 &&
            (analysis::IdEq(container_path[0], "string") ||
             analysis::IdEq(container_path[0], "bytes"));
        if (!builtin.empty() || is_string_bytes) {
          needs_panic_out = false;
        }
      };

      if (const auto* path_expr =
              std::get_if<ast::PathExpr>(&expr.callee->node)) {
        std::vector<std::string> full = path_expr->path;
        full.push_back(path_expr->name);
        check_builtin(full, path_expr->path);
      } else if (const auto* qname =
                     std::get_if<ast::QualifiedNameExpr>(&expr.callee->node)) {
        std::vector<std::string> full = qname->path;
        full.push_back(qname->name);
        check_builtin(full, qname->path);
      } else if (const auto* ident =
                     std::get_if<ast::IdentifierExpr>(&expr.callee->node)) {
        if (ctx.resolve_name) {
          if (auto resolved = ctx.resolve_name(ident->name)) {
            std::vector<std::string> container = *resolved;
            if (!container.empty()) {
              container.pop_back();
            }
            check_builtin(*resolved, container);
          }
        }
      }
    }
  }

  IRPtr local_pre_ir = EmptyIR();
  IRPtr foreign_pre_ir = EmptyIR();
  IRPtr foreign_post_ir = EmptyIR();
  if (call.callee.kind == IRValue::Kind::Symbol) {
    local_pre_ir =
        EmitLocalPreDynamicChecks(callee_lookup_symbol, expr, call.args, ctx);
    foreign_pre_ir =
        EmitForeignPreDynamicChecks(callee_lookup_symbol, call.args, ctx);
    foreign_post_ir =
        EmitForeignPostDynamicChecks(callee_lookup_symbol, result_value, ctx);
  }
  auto is_noop = [](const IRPtr& ir) {
    return !ir || std::holds_alternative<IROpaque>(ir->node);
  };

  if (needs_panic_out) {
    IRValue panic_out;
    panic_out.kind = IRValue::Kind::Local;
    panic_out.name = std::string(kPanicOutName);
    call.args.push_back(panic_out);
    std::vector<IRPtr> parts;
    parts.push_back(callee_result.ir);
    parts.push_back(args_ir);
    if (!is_noop(local_pre_ir)) {
      parts.push_back(local_pre_ir);
    }
    if (!is_noop(foreign_pre_ir)) {
      parts.push_back(foreign_pre_ir);
    }
    parts.push_back(MakeIR(std::move(call)));
    parts.push_back(PanicFollowup(ctx));
    if (!is_noop(foreign_post_ir)) {
      parts.push_back(foreign_post_ir);
    }
    return LowerResult{
        SeqIR(std::move(parts)),
        result_value};
  }

  std::vector<IRPtr> parts;
  parts.push_back(callee_result.ir);
  parts.push_back(args_ir);
  if (!is_noop(local_pre_ir)) {
    parts.push_back(local_pre_ir);
  }
  if (!is_noop(foreign_pre_ir)) {
    parts.push_back(foreign_pre_ir);
  }
  parts.push_back(MakeIR(std::move(call)));
  if (!is_noop(foreign_post_ir)) {
    parts.push_back(foreign_post_ir);
  }
  return LowerResult{
      SeqIR(std::move(parts)),
      result_value};
}

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorCallLoweringRules() {
  SPEC_RULE("Lower-Expr-Call-PanicOut");
  SPEC_RULE("Lower-Expr-Call-NoPanicOut");
  SPEC_RULE("Lower-CallIR-RecordCtor");
  SPEC_RULE("Lower-Args-Empty");
  SPEC_RULE("Lower-Args-Cons-Move");
  SPEC_RULE("Lower-Args-Cons-Ref");
  SPEC_RULE("Lower-RecvArg-Move");
  SPEC_RULE("Lower-RecvArg-Ref");
  SPEC_RULE("Lower-Expr-MethodCall");
  SPEC_RULE("Lower-MethodCall-Region-Alloc");
  SPEC_RULE("Lower-MethodCall-ContextBuiltin");
  SPEC_RULE("Lower-MethodCall-Capability");
  SPEC_RULE("Lower-MethodCall-Dynamic");
  SPEC_RULE("Lower-MethodCall-Static-PanicOut");
  SPEC_RULE("Lower-MethodCall-Static-NoPanicOut");
}

}  // namespace cursive::codegen


