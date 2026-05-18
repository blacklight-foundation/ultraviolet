// =================================================================
// File: 04_analysis/typing/expr/enum_literal.cpp
// Construct: Enum Literal Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Enum-Lit-Unit, T-Enum-Lit-Tuple, T-Enum-Lit-Record
// =================================================================

#include "04_analysis/typing/expr/enum_literal.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/enums.h"
#include "04_analysis/generics/generic_params.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/expr/path.h"
#include "04_analysis/typing/if_case_check.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/deprecation_warnings.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/typecheck.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsEnumLiteral() {
  SPEC_DEF("T-Enum-Lit-Unit", "5.2.12");
  SPEC_DEF("Enum-Lit-Tuple-Arity-Err", "12.7.4");
  SPEC_DEF("Enum-Lit-Record-MissingField", "12.7.4");
  SPEC_DEF("T-Enum-Lit-Tuple", "5.2.12");
  SPEC_DEF("T-Enum-Lit-Record", "5.2.12");
}

struct EnumLiteralTarget {
  TypePath enum_path;
  std::string variant_name;
  const ast::EnumDecl* enum_decl = nullptr;
  const ast::VariantDecl* variant = nullptr;
};

struct EnumGenericContext {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  ScopeContext payload_ctx;
  TypeSubst subst;
};

bool TypePathEqLocal(const TypePath& lhs, const TypePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (IdKeyOf(lhs[i]) != IdKeyOf(rhs[i])) {
      return false;
    }
  }
  return true;
}

std::optional<core::Span> EnumLiteralRefSpan(
    const ast::EnumLiteralExpr& expr) {
  if (!expr.payload_opt.has_value()) {
    return std::nullopt;
  }
  if (std::holds_alternative<ast::EnumPayloadParen>(*expr.payload_opt)) {
    const auto& payload = std::get<ast::EnumPayloadParen>(*expr.payload_opt);
    if (!payload.elements.empty() && payload.elements.front()) {
      return payload.elements.front()->span;
    }
    return std::nullopt;
  }
  const auto& payload = std::get<ast::EnumPayloadBrace>(*expr.payload_opt);
  if (!payload.fields.empty() && payload.fields.front().value) {
    return payload.fields.front().value->span;
  }
  return std::nullopt;
}

std::optional<EnumLiteralTarget> ResolveEnumLiteralTarget(
    const ScopeContext& ctx,
    const ast::EnumLiteralExpr& expr) {
  if (expr.path.size() <= 1) {
    return std::nullopt;
  }

  EnumLiteralTarget target;
  target.enum_path.assign(expr.path.begin(), expr.path.end() - 1);
  target.variant_name = expr.path.back();

  target.enum_decl = LookupEnumDecl(ctx, target.enum_path);
  if (!target.enum_decl) {
    return std::nullopt;
  }

  for (const auto& variant : target.enum_decl->variants) {
    if (IdEq(variant.name, target.variant_name)) {
      target.variant = &variant;
      break;
    }
  }
  if (!target.variant) {
    return std::nullopt;
  }

  return target;
}

EnumGenericContext BuildEnumGenericContext(
    const ScopeContext& ctx,
    const ast::EnumDecl& decl,
    const std::vector<TypeRef>& generic_args) {
  EnumGenericContext result;
  result.payload_ctx = ctx;
  result.payload_ctx.scopes = BindTypeParams(ctx, decl.generic_params);

  if (decl.generic_params.has_value()) {
    const auto provided = generic_args.size();
    const auto required = RequiredParamCount(decl.generic_params);
    const auto total = TotalParamCount(decl.generic_params);
    if (provided < required || provided > total) {
      result.ok = false;
      result.diag_id = "E-TYP-2303";
      return result;
    }
    result.subst = BuildSubstitution(decl.generic_params->params, generic_args);
  } else if (!generic_args.empty()) {
    result.ok = false;
    result.diag_id = "E-TYP-2303";
  }

  return result;
}

CheckResult CheckEnumLiteralPayloadAgainst(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::EnumLiteralExpr& expr,
    const EnumLiteralTarget& target,
    const ScopeContext& payload_type_ctx,
    const TypeSubst& subst,
    const TypeEnv& env) {
  CheckResult result;
  const auto& variant = *target.variant;

  if (!variant.payload_opt.has_value()) {
    if (expr.payload_opt.has_value()) {
      return result;
    }
    SPEC_RULE("T-Enum-Lit-Unit");
    result.ok = true;
    return result;
  }

  if (!expr.payload_opt.has_value()) {
    return result;
  }

  const auto& decl_payload = *variant.payload_opt;
  const auto& expr_payload = *expr.payload_opt;

  if (std::holds_alternative<ast::VariantPayloadTuple>(decl_payload)) {
    const auto& tuple_payload =
        std::get<ast::VariantPayloadTuple>(decl_payload);
    if (!std::holds_alternative<ast::EnumPayloadParen>(expr_payload)) {
      return result;
    }
    const auto& paren = std::get<ast::EnumPayloadParen>(expr_payload);
    if (paren.elements.size() != tuple_payload.elements.size()) {
      SPEC_RULE("Enum-Lit-Tuple-Arity-Err");
      result.diag_id = "E-TYP-2008";
      return result;
    }

    for (std::size_t i = 0; i < paren.elements.size(); ++i) {
      const auto elem_type_lowered =
          LowerType(payload_type_ctx, tuple_payload.elements[i]);
      if (!elem_type_lowered.ok) {
        result.diag_id = elem_type_lowered.diag_id;
        return result;
      }
      const TypeRef elem_type = InstantiateType(elem_type_lowered.type, subst);
      const auto check =
          CheckExprAgainst(ctx, type_ctx, paren.elements[i], elem_type, env);
      if (!check.ok) {
        return check;
      }
    }

    SPEC_RULE("T-Enum-Lit-Tuple");
    result.ok = true;
    return result;
  }

  const auto& record_payload =
      std::get<ast::VariantPayloadRecord>(decl_payload);
  if (!std::holds_alternative<ast::EnumPayloadBrace>(expr_payload)) {
    return result;
  }
  const auto& brace = std::get<ast::EnumPayloadBrace>(expr_payload);

  std::unordered_set<IdKey> seen;
  for (const auto& field_init : brace.fields) {
    const auto key = IdKeyOf(field_init.name);
    if (!seen.insert(key).second) {
      return result;
    }
  }

  std::unordered_map<IdKey, TypeRef> field_types;
  for (const auto& field_decl : record_payload.fields) {
    const auto lowered = LowerType(payload_type_ctx, field_decl.type);
    if (!lowered.ok) {
      result.diag_id = lowered.diag_id;
      return result;
    }
    field_types.emplace(IdKeyOf(field_decl.name),
                        InstantiateType(lowered.type, subst));
  }

  if (field_types.size() != seen.size()) {
    SPEC_RULE("Enum-Lit-Record-MissingField");
    result.diag_id = "E-TYP-2009";
    return result;
  }

  for (const auto& field_init : brace.fields) {
    const auto it = field_types.find(IdKeyOf(field_init.name));
    if (it == field_types.end()) {
      SPEC_RULE("Enum-Lit-Record-MissingField");
      result.diag_id = "E-TYP-2009";
      if (field_init.value) {
        result.diag_span = field_init.value->span;
      }
      return result;
    }
    const auto check =
        CheckExprAgainst(ctx, type_ctx, field_init.value, it->second, env);
    if (!check.ok) {
      return check;
    }
  }

  SPEC_RULE("T-Enum-Lit-Record");
  result.ok = true;
  return result;
}

void CopyCheckFailure(const CheckResult& check, ExprTypeResult& result) {
  result.diag_id = check.diag_id;
  result.diag_detail = check.diag_detail;
  result.diag_span = check.diag_span;
}

}  // namespace

ExprTypeResult TypeEnumLiteralExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::EnumLiteralExpr& expr,
                                       const TypeEnv& env) {
  SpecDefsEnumLiteral();
  ExprTypeResult result;

  const auto target = ResolveEnumLiteralTarget(ctx, expr);
  if (!target.has_value()) {
    return result;
  }

  EmitDeprecatedReferenceWarningFromAttrs(
      target->enum_decl->attrs, type_ctx, EnumLiteralRefSpan(expr));

  // Generic enum constructors require a contextual TypeApply so payload type
  // parameters can be substituted before checking constructor arguments.
  if (TotalParamCount(target->enum_decl->generic_params) > 0) {
    return result;
  }

  const TypeSubst subst;
  const auto check =
      CheckEnumLiteralPayloadAgainst(ctx, type_ctx, expr, *target, ctx, subst, env);
  if (!check.ok) {
    CopyCheckFailure(check, result);
    return result;
  }

  result.ok = true;
  result.type = MakeTypePath(target->enum_path);
  return result;
}

CheckResult CheckEnumLiteralExprAgainstImpl(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::EnumLiteralExpr& expr,
                                            const TypeRef& expected,
                                            const TypeEnv& env) {
  SpecDefsEnumLiteral();
  CheckResult result;

  const auto target = ResolveEnumLiteralTarget(ctx, expr);
  if (!target.has_value()) {
    return result;
  }

  const TypeRef expected_base = StripPerm(expected);
  if (!expected_base) {
    return result;
  }
  const auto* expected_path = AppliedTypePath(*expected_base);
  const auto* expected_args = AppliedTypeArgs(*expected_base);
  if (!expected_path || !expected_args ||
      !TypePathEqLocal(*expected_path, target->enum_path)) {
    return result;
  }

  EmitDeprecatedReferenceWarningFromAttrs(
      target->enum_decl->attrs, type_ctx, EnumLiteralRefSpan(expr));

  const auto generic_ctx =
      BuildEnumGenericContext(ctx, *target->enum_decl, *expected_args);
  if (!generic_ctx.ok) {
    result.diag_id = generic_ctx.diag_id;
    return result;
  }

  return CheckEnumLiteralPayloadAgainst(ctx, type_ctx, expr, *target,
                                        generic_ctx.payload_ctx,
                                        generic_ctx.subst, env);
}

}  // namespace ultraviolet::analysis::expr
