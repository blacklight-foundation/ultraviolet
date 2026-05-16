// =================================================================
// File: 04_analysis/typing/expr/binary.cpp
// Construct: Binary Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Arith, T-Bitwise, T-Shift, T-Compare-Eq, T-Compare-Ord, T-Logical
// =================================================================

#include "04_analysis/typing/expr/binary.h"

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/typecheck.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast_utils.h"

#include <vector>

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsBinary() {
  SPEC_DEF("T-Arith", "5.2.12");
  SPEC_DEF("T-Bitwise", "5.2.12");
  SPEC_DEF("T-Shift", "5.2.12");
  SPEC_DEF("T-Compare-Eq", "5.2.12");
  SPEC_DEF("T-Compare-Ord", "5.2.12");
  SPEC_DEF("T-Logical", "5.2.12");
  SPEC_DEF("Binary-Operand-Type-Err", "16.4.7");
}

struct PrimResolveResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<std::string> prim;
};

struct BinaryOperandInfo {
  ExprTypeResult typed;
  TypeRef core = nullptr;
  std::optional<std::string> prim_name;
};

constexpr std::string_view kBinaryOperandTypeMismatchDiag = "E-SEM-2525";

static PrimResolveResult ResolveAliasTransparentPrim(const ScopeContext& ctx,
                                                     const TypeRef& type) {
  PrimResolveResult result;
  if (!type) {
    return result;
  }

  if (const auto direct = GetPrimName(type); direct.has_value()) {
    result.prim = *direct;
    return result;
  }

  static constexpr std::array<std::string_view, 18> kPrimCandidates = {
      "i8",   "i16",   "i32",   "i64",   "i128", "isize",
      "u8",   "u16",   "u32",   "u64",   "u128", "usize",
      "f16",  "f32",   "f64",   "bool",  "char", "()"};

  for (const auto candidate : kPrimCandidates) {
    const auto prim = MakeTypePrim(std::string(candidate));
    const auto l2r = Subtyping(ctx, type, prim);
    if (!l2r.ok) {
      result.ok = false;
      result.diag_id = l2r.diag_id;
      return result;
    }
    if (!l2r.subtype) {
      continue;
    }

    const auto r2l = Subtyping(ctx, prim, type);
    if (!r2l.ok) {
      result.ok = false;
      result.diag_id = r2l.diag_id;
      return result;
    }
    if (r2l.subtype) {
      result.prim = std::string(candidate);
      return result;
    }
  }

  return result;
}

static SubtypingResult AliasTransparentEquiv(const ScopeContext& ctx,
                                             const TypeRef& lhs,
                                             const TypeRef& rhs) {
  const auto l2r = Subtyping(ctx, lhs, rhs);
  if (!l2r.ok) {
    return {false, l2r.diag_id, false};
  }
  if (!l2r.subtype) {
    return {true, std::nullopt, false};
  }

  const auto r2l = Subtyping(ctx, rhs, lhs);
  if (!r2l.ok) {
    return {false, r2l.diag_id, false};
  }
  if (!r2l.subtype) {
    return {true, std::nullopt, false};
  }

  return {true, std::nullopt, true};
}

static SubtypingResult AliasTransparentPrimEq(const ScopeContext& ctx,
                                              const TypeRef& type,
                                              std::string_view prim_name) {
  return AliasTransparentEquiv(ctx, type, MakeTypePrim(std::string(prim_name)));
}

static bool IsOrdPrim(std::string_view prim_name) {
  return prim_name == "i8" || prim_name == "i16" ||
         prim_name == "i32" || prim_name == "i64" ||
         prim_name == "i128" || prim_name == "isize" ||
         prim_name == "u8" || prim_name == "u16" ||
         prim_name == "u32" || prim_name == "u64" ||
         prim_name == "u128" || prim_name == "usize" ||
         prim_name == "f16" || prim_name == "f32" ||
         prim_name == "f64" || prim_name == "char";
}

static bool IsNullLiteralExpr(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  const auto* literal = std::get_if<ast::LiteralExpr>(&expr->node);
  return literal && literal->literal.kind == lexer::TokenKind::NullLiteral;
}

static TypeRef StripPermAndRefine(const TypeRef& type) {
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
  return cur;
}

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

static const ast::TypeAliasDecl* LookupTypeAliasDecl(const ScopeContext& ctx,
                                                     const TypePath& path) {
  if (path.empty()) {
    return nullptr;
  }
  if (path.size() > 1) {
    ast::Path full;
    full.reserve(path.size());
    for (const auto& seg : path) {
      full.push_back(seg);
    }
    const auto it = ctx.sigma.types.find(PathKeyOf(full));
    if (it == ctx.sigma.types.end()) {
      return nullptr;
    }
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  const auto ent = ResolveTypeName(ctx, path[0]);
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return nullptr;
  }

  ast::Path resolved = *ent->origin_opt;
  const std::string resolved_name =
      ent->target_opt.has_value() ? *ent->target_opt : path[0];
  resolved.push_back(resolved_name);
  const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (resolved_it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(&resolved_it->second);
}

static AliasExpandResult ExpandTypeAliasApply(const ScopeContext& ctx,
                                              const TypePathType& applied) {
  AliasExpandResult result;
  const auto* alias = LookupTypeAliasDecl(ctx, applied.path);
  if (!alias) {
    return result;
  }

  const auto lowered = LowerType(ctx, alias->type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }

  if (!alias->generic_params.has_value()) {
    if (!applied.generic_args.empty()) {
      return result;
    }
    result.type = lowered.type;
    result.expanded = true;
    return result;
  }

  const auto& params = alias->generic_params->params;
  if (applied.generic_args.size() > params.size()) {
    return result;
  }

  const auto subst = BuildSubstitution(params, applied.generic_args);
  result.type = InstantiateType(lowered.type, subst);
  result.expanded = result.type != nullptr;
  return result;
}

static AliasExpandResult NormalizeBinaryCoreType(const ScopeContext& ctx,
                                                 const TypeRef& type) {
  AliasExpandResult out;
  out.type = StripPermAndRefine(type);
  for (int i = 0; i < 16; ++i) {
    if (!out.type) {
      return out;
    }
    const auto* path = std::get_if<TypePathType>(&out.type->node);
    if (!path) {
      return out;
    }
    const auto expanded = ExpandTypeAliasApply(ctx, *path);
    if (!expanded.ok) {
      out.ok = false;
      out.diag_id = expanded.diag_id;
      return out;
    }
    if (!expanded.expanded) {
      return out;
    }
    out.type = StripPermAndRefine(expanded.type);
    out.expanded = true;
  }
  return out;
}

static bool IsRawPtrLikeType(const TypeRef& type) {
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
  return cur && std::holds_alternative<TypeRawPtr>(cur->node);
}

static bool LoadBinaryOperandInfo(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::ExprPtr& expr,
                                  const TypeEnv& env,
                                  BinaryOperandInfo& out,
                                  std::optional<std::string_view>& diag_id,
                                  std::string* diag_detail,
                                  std::optional<core::Span>* diag_span) {
  out.typed =
      TypeExpr(ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr,
               env);
  if (!out.typed.ok) {
    diag_id = out.typed.diag_id;
    if (diag_detail) {
      *diag_detail = std::string("logical operand ") +
                     (expr ? ast::node_kind(*expr) : "<missing>") +
                     " failed to type";
      if (!out.typed.diag_detail.empty()) {
        *diag_detail += ": " + out.typed.diag_detail;
      }
    }
    if (diag_span) {
      *diag_span = out.typed.diag_span.has_value()
                       ? out.typed.diag_span
                       : (expr ? std::optional<core::Span>(expr->span)
                               : std::nullopt);
    }
    return false;
  }

  const auto core_norm = NormalizeBinaryCoreType(ctx, out.typed.type);
  if (!core_norm.ok) {
    diag_id = core_norm.diag_id;
    return false;
  }

  out.core = core_norm.type;

  const auto prim = ResolveAliasTransparentPrim(ctx, out.core);
  if (!prim.ok) {
    diag_id = prim.diag_id;
    return false;
  }

  out.prim_name = prim.prim;
  return true;
}

static void SetBinaryOperandTypeMismatch(ExprTypeResult& result,
                                         std::string_view op) {
  result.diag_id = kBinaryOperandTypeMismatchDiag;
  result.diag_detail =
      "operator '" + std::string(op) +
      "' requires operands compatible with the operator's type rules";
}

static bool TryCheckOperandAgainst(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::ExprPtr& expr,
                                   const TypeRef& expected,
                                   const TypeEnv& env) {
  if (!expected) {
    return false;
  }

  const auto checked = CheckExprAgainst(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr, expected,
      env);
  return checked.ok;
}

static bool TryAliasTransparentEquiv(const ScopeContext& ctx,
                                     const TypeRef& lhs,
                                     const TypeRef& rhs,
                                     ExprTypeResult& result,
                                     bool& equiv) {
  const auto rel = AliasTransparentEquiv(ctx, lhs, rhs);
  if (!rel.ok) {
    result.diag_id = rel.diag_id;
    return false;
  }

  equiv = rel.subtype;
  return true;
}

static void CollectLogicalBinaryOperands(const ast::ExprPtr& expr,
                                         std::string_view op,
                                         std::vector<ast::ExprPtr>& operands) {
  std::vector<ast::ExprPtr> stack;
  stack.push_back(expr);

  while (!stack.empty()) {
    ast::ExprPtr current = std::move(stack.back());
    stack.pop_back();
    if (!current) {
      continue;
    }

    const auto* binary = std::get_if<ast::BinaryExpr>(&current->node);
    if (binary && binary->op == op && binary->lhs && binary->rhs) {
      stack.push_back(binary->rhs);
      stack.push_back(binary->lhs);
      continue;
    }

    operands.push_back(std::move(current));
  }
}

static bool CheckLogicalOperand(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::ExprPtr& expr,
                                const TypeEnv& env,
                                ExprTypeResult& result) {
  BinaryOperandInfo operand;
  std::optional<std::string_view> diag_id;
  if (!LoadBinaryOperandInfo(ctx, type_ctx, expr, env, operand, diag_id,
                             &result.diag_detail, &result.diag_span)) {
    result.diag_id = diag_id;
    return false;
  }

  const auto operand_bool = AliasTransparentPrimEq(ctx, operand.core, "bool");
  if (!operand_bool.ok) {
    result.diag_id = operand_bool.diag_id;
    return false;
  }

  if (operand_bool.subtype ||
      TryCheckOperandAgainst(ctx, type_ctx, expr, MakeTypePrim("bool"), env)) {
    return true;
  }

  result.diag_detail =
      std::string("logical operand ") +
      (expr ? ast::node_kind(*expr) : "<missing>") +
      " has type " + TypeToString(operand.typed.type);
  result.diag_span = expr ? std::optional<core::Span>(expr->span) : std::nullopt;
  return false;
}

static ExprTypeResult TypeLogicalBinaryChain(const ScopeContext& ctx,
                                             const StmtTypeContext& type_ctx,
                                             const ast::BinaryExpr& expr,
                                             const TypeEnv& env) {
  ExprTypeResult result;
  std::vector<ast::ExprPtr> operands;
  CollectLogicalBinaryOperands(expr.lhs, expr.op, operands);
  CollectLogicalBinaryOperands(expr.rhs, expr.op, operands);

  for (const auto& operand : operands) {
    if (!CheckLogicalOperand(ctx, type_ctx, operand, env, result)) {
      if (!result.diag_id.has_value()) {
        SPEC_RULE("Binary-Operand-Type-Err");
        const std::string detail = result.diag_detail;
        const std::optional<core::Span> span = result.diag_span;
        SetBinaryOperandTypeMismatch(result, expr.op);
        if (!detail.empty()) {
          result.diag_detail = detail;
        }
        if (span.has_value()) {
          result.diag_span = span;
        }
      }
      return result;
    }
  }

  SPEC_RULE("T-Logical");
  result.ok = true;
  result.type = MakeTypePrim("bool");
  return result;
}

}  // namespace

// (T-Arith), (T-Bitwise), (T-Shift), (T-Compare-Eq), (T-Compare-Ord), (T-Logical)
ExprTypeResult TypeBinaryExprImpl(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::BinaryExpr& expr,
                                  const TypeEnv& env) {
  SpecDefsBinary();
  ExprTypeResult result;
  const std::string_view op = expr.op;

  // Null literals are check-only by default (Chk-Null-Literal), but equality
  // needs contextual typing from the opposite operand.
  if (IsEqOp(op)) {
    const bool lhs_null = IsNullLiteralExpr(expr.lhs);
    const bool rhs_null = IsNullLiteralExpr(expr.rhs);
    if (lhs_null != rhs_null) {
      const auto typed_non_null =
          TypeExpr(ctx, type_ctx, lhs_null ? expr.rhs : expr.lhs, env);
      if (!typed_non_null.ok) {
        result.diag_id = typed_non_null.diag_id;
        return result;
      }
      if (!IsRawPtrLikeType(typed_non_null.type)) {
        return result;
      }
      SPEC_RULE("T-Compare-Eq");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }
  }

  if (IsLogicOp(op)) {
    return TypeLogicalBinaryChain(ctx, type_ctx, expr, env);
  }

  BinaryOperandInfo lhs;
  std::optional<std::string_view> lhs_diag_id;
  if (!LoadBinaryOperandInfo(ctx, type_ctx, expr.lhs, env, lhs, lhs_diag_id,
                             nullptr, nullptr)) {
    result.diag_id = lhs_diag_id;
    return result;
  }

  BinaryOperandInfo rhs;
  std::optional<std::string_view> rhs_diag_id;
  if (!LoadBinaryOperandInfo(ctx, type_ctx, expr.rhs, env, rhs, rhs_diag_id,
                             nullptr, nullptr)) {
    result.diag_id = rhs_diag_id;
    return result;
  }

  // Arithmetic operators: +, -, *, /, %, **
  if (IsArithOp(op)) {
    bool equiv = false;
    if (!TryAliasTransparentEquiv(ctx, lhs.core, rhs.core, result, equiv)) {
      return result;
    }

    if (equiv && lhs.prim_name.has_value() && IsNumericType(*lhs.prim_name)) {
      SPEC_RULE("T-Arith");
      result.ok = true;
      result.type = MakeTypePrim(*lhs.prim_name);
      return result;
    }

    if (lhs.prim_name.has_value() && IsNumericType(*lhs.prim_name) &&
        TryCheckOperandAgainst(ctx, type_ctx, expr.rhs, lhs.core, env)) {
      SPEC_RULE("T-Arith");
      result.ok = true;
      result.type = MakeTypePrim(*lhs.prim_name);
      return result;
    }

    if (rhs.prim_name.has_value() && IsNumericType(*rhs.prim_name) &&
        TryCheckOperandAgainst(ctx, type_ctx, expr.lhs, rhs.core, env)) {
      SPEC_RULE("T-Arith");
      result.ok = true;
      result.type = MakeTypePrim(*rhs.prim_name);
      return result;
    }

    SPEC_RULE("Binary-Operand-Type-Err");
    SetBinaryOperandTypeMismatch(result, op);
    return result;
  }

  // Bitwise operators: &, |, ^
  if (IsBitOp(op)) {
    bool equiv = false;
    if (!TryAliasTransparentEquiv(ctx, lhs.core, rhs.core, result, equiv)) {
      return result;
    }

    if (equiv && lhs.prim_name.has_value() && IsIntType(*lhs.prim_name)) {
      SPEC_RULE("T-Bitwise");
      result.ok = true;
      result.type = MakeTypePrim(*lhs.prim_name);
      return result;
    }

    if (lhs.prim_name.has_value() && IsIntType(*lhs.prim_name) &&
        TryCheckOperandAgainst(ctx, type_ctx, expr.rhs, lhs.core, env)) {
      SPEC_RULE("T-Bitwise");
      result.ok = true;
      result.type = MakeTypePrim(*lhs.prim_name);
      return result;
    }

    if (rhs.prim_name.has_value() && IsIntType(*rhs.prim_name) &&
        TryCheckOperandAgainst(ctx, type_ctx, expr.lhs, rhs.core, env)) {
      SPEC_RULE("T-Bitwise");
      result.ok = true;
      result.type = MakeTypePrim(*rhs.prim_name);
      return result;
    }

    SPEC_RULE("Binary-Operand-Type-Err");
    SetBinaryOperandTypeMismatch(result, op);
    return result;
  }

  // Shift operators: <<, >>
  if (IsShiftOp(op)) {
    if (!lhs.prim_name.has_value() || !IsIntType(*lhs.prim_name)) {
      SPEC_RULE("Binary-Operand-Type-Err");
      SetBinaryOperandTypeMismatch(result, op);
      return result;
    }

    const auto rhs_u32 = AliasTransparentPrimEq(ctx, rhs.core, "u32");
    if (!rhs_u32.ok) {
      result.diag_id = rhs_u32.diag_id;
      return result;
    }

    if (rhs_u32.subtype ||
        TryCheckOperandAgainst(ctx, type_ctx, expr.rhs, MakeTypePrim("u32"), env)) {
      SPEC_RULE("T-Shift");
      result.ok = true;
      result.type = MakeTypePrim(*lhs.prim_name);
      return result;
    }

    SPEC_RULE("Binary-Operand-Type-Err");
    SetBinaryOperandTypeMismatch(result, op);
    return result;
  }

  // Equality operators: ==, !=
  if (IsEqOp(op)) {
    bool equiv = false;
    if (!TryAliasTransparentEquiv(ctx, lhs.core, rhs.core, result, equiv)) {
      return result;
    }

    if (equiv && EqType(lhs.core)) {
      SPEC_RULE("T-Compare-Eq");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }

    if (EqType(lhs.core) &&
        TryCheckOperandAgainst(ctx, type_ctx, expr.rhs, lhs.core, env)) {
      SPEC_RULE("T-Compare-Eq");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }

    if (EqType(rhs.core) &&
        TryCheckOperandAgainst(ctx, type_ctx, expr.lhs, rhs.core, env)) {
      SPEC_RULE("T-Compare-Eq");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }

    SPEC_RULE("Binary-Operand-Type-Err");
    SetBinaryOperandTypeMismatch(result, op);
    return result;
  }

  // Ordering operators: <, <=, >, >=
  if (IsOrdOp(op)) {
    bool equiv = false;
    if (!TryAliasTransparentEquiv(ctx, lhs.core, rhs.core, result, equiv)) {
      return result;
    }

    const bool lhs_ord = OrdType(lhs.core) ||
                         (lhs.prim_name.has_value() && IsOrdPrim(*lhs.prim_name));
    const bool rhs_ord = OrdType(rhs.core) ||
                         (rhs.prim_name.has_value() && IsOrdPrim(*rhs.prim_name));

    if (equiv && lhs_ord) {
      SPEC_RULE("T-Compare-Ord");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }

    if (lhs_ord && TryCheckOperandAgainst(ctx, type_ctx, expr.rhs, lhs.core, env)) {
      SPEC_RULE("T-Compare-Ord");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }

    if (rhs_ord && TryCheckOperandAgainst(ctx, type_ctx, expr.lhs, rhs.core, env)) {
      SPEC_RULE("T-Compare-Ord");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }

    SPEC_RULE("Binary-Operand-Type-Err");
    SetBinaryOperandTypeMismatch(result, op);
    return result;
  }

  return result;
}

}  // namespace cursive::analysis::expr
