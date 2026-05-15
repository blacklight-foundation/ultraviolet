// =============================================================================
// return_stmt.cpp - Return statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.11: Statement Typing
//   - return expr: Return with value
//   - return: Return unit (procedure with () return)
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <memory>
#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/memory/safe_ptr.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_pattern.h"
#include "04_analysis/typing/type_predicates.h"

namespace cursive::analysis {

// Forward declaration - defined in verification.cpp or equivalent
bool TypeImplementsClass(const ScopeContext& ctx, const TypeRef& type,
                         const TypePath& class_path);

namespace {

static inline void SpecDefsReturnStmt() {
  SPEC_DEF("T-Return-Value", "5.2.11");
  SPEC_DEF("T-Return-Unit", "5.2.11");
  SPEC_DEF("Return-Type-Err", "5.2.11");
  SPEC_DEF("Return-Async-Type-Err", "5.2.11");
  SPEC_DEF("Return-Async-Unit-Err", "5.2.11");
  SPEC_DEF("Return-Unit-Err", "5.2.11");
  SPEC_DEF("T-Opaque-Return", "5.2.11");
  SPEC_DEF("FFI-Return-RegionLocalRawPtr-Err", "23.5.4");
}

static ast::ExprPtr MakeUnitExpr(const core::Span& span) {
  ast::TupleExpr tuple;
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(tuple);
  return expr;
}

static ast::ExprPtr MakeExprNodeLocal(const core::Span& span,
                                      ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static ast::ExprPtr SubstituteResultEntry(const ast::ExprPtr& expr,
                                          const ast::ExprPtr& result_expr,
                                          const TypeEnv* env);

static ExprTypeResult TypeExprWithCurrentEnv(const ScopeContext& ctx,
                                             const StmtTypeContext& type_ctx,
                                             const TypeEnv& env,
                                             const ExprTypeFn& type_expr,
                                             const ast::ExprPtr& expr) {
  if (!expr) {
    return {};
  }
  const auto via_env = TypeExpr(ctx, type_ctx, expr, env);
  if (via_env.ok || via_env.diag_id.has_value()) {
    return via_env;
  }
  const auto via_callback = type_expr(expr);
  if (via_callback.ok) {
    return via_callback;
  }
  return via_callback;
}

static PlaceTypeResult TypePlaceWithCurrentEnv(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const TypeEnv& env,
                                               const PlaceTypeFn& type_place,
                                               const ast::ExprPtr& expr) {
  if (!expr) {
    return {};
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    const auto binding = BindOf(env, ident->name);
    if (binding.has_value()) {
      return {true, std::nullopt, binding->type};
    }
  }
  const auto via_env = TypePlace(ctx, type_ctx, expr, env);
  if (via_env.ok || via_env.diag_id.has_value()) {
    return via_env;
  }
  const auto via_callback = type_place(expr);
  if (via_callback.ok) {
    return via_callback;
  }
  return via_callback;
}

static IdentTypeFn IdentTypeWithCurrentEnv(const TypeEnv& env,
                                           const IdentTypeFn& type_ident) {
  return [&](std::string_view name) -> ExprTypeResult {
    const auto binding = BindOf(env, name);
    if (binding.has_value()) {
      ExprTypeResult local;
      local.ok = true;
      local.type = binding->type;
      return local;
    }
    return type_ident(name);
  };
}

static bool PatternBindsName(const ast::PatternPtr& pattern,
                             std::string_view name) {
  if (!pattern) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          return IdEq(node.name, name);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          return IdEq(node.name, name);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& element : node.elements) {
            if (PatternBindsName(element, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            if (field.pattern_opt) {
              if (PatternBindsName(field.pattern_opt, name)) {
                return true;
              }
            } else if (IdEq(field.name, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          return std::visit(
              [&](const auto& payload) -> bool {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& element : payload.elements) {
                    if (PatternBindsName(element, name)) {
                      return true;
                    }
                  }
                  return false;
                } else if constexpr (std::is_same_v<P,
                                                    ast::RecordPayloadPattern>) {
                  for (const auto& field : payload.fields) {
                    if (field.pattern_opt) {
                      if (PatternBindsName(field.pattern_opt, name)) {
                        return true;
                      }
                    } else if (IdEq(field.name, name)) {
                      return true;
                    }
                  }
                  return false;
                } else {
                  return false;
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt.has_value()) {
            return false;
          }
          for (const auto& field : node.fields_opt->fields) {
            if (field.pattern_opt) {
              if (PatternBindsName(field.pattern_opt, name)) {
                return true;
              }
            } else if (IdEq(field.name, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          return PatternBindsName(node.lo, name) ||
                 PatternBindsName(node.hi, name);
        } else {
          return false;
        }
      },
      pattern->node);
}

static bool PatternBindsWholeValue(const ast::PatternPtr& pattern) {
  if (!pattern) {
    return false;
  }
  return std::holds_alternative<ast::IdentifierPattern>(pattern->node) ||
         std::holds_alternative<ast::TypedPattern>(pattern->node) ||
         std::holds_alternative<ast::WildcardPattern>(pattern->node);
}

static ast::ExprPtr SubstituteIdentifierExpr(const ast::ExprPtr& expr,
                                             std::string_view name,
                                             const ast::ExprPtr& replacement) {
  if (!expr) {
    return expr;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    if (IdEq(ident->name, name)) {
      return replacement;
    }
    return expr;
  }

  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SubstituteIdentifierExpr(node.lhs, name, replacement);
          out.rhs = SubstituteIdentifierExpr(node.rhs, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierExpr(node.value, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdentifierExpr(node.base, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdentifierExpr(node.base, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdentifierExpr(node.base, name, replacement);
          out.index = SubstituteIdentifierExpr(node.index, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          out.callee = SubstituteIdentifierExpr(node.callee, name, replacement);
          for (auto& arg : out.args) {
            arg.value = SubstituteIdentifierExpr(arg.value, name, replacement);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto out = node;
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            auto paren = std::get<ast::ParenArgs>(node.args);
            for (auto& arg : paren.args) {
              arg.value = SubstituteIdentifierExpr(arg.value, name, replacement);
            }
            out.args = std::move(paren);
          } else {
            auto brace = std::get<ast::BraceArgs>(node.args);
            for (auto& field : brace.fields) {
              field.value =
                  SubstituteIdentifierExpr(field.value, name, replacement);
            }
            out.args = std::move(brace);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          out.receiver =
              SubstituteIdentifierExpr(node.receiver, name, replacement);
          for (auto& arg : out.args) {
            arg.value = SubstituteIdentifierExpr(arg.value, name, replacement);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = node;
          out.cond = SubstituteIdentifierExpr(node.cond, name, replacement);
          out.then_expr =
              SubstituteIdentifierExpr(node.then_expr, name, replacement);
          out.else_expr =
              SubstituteIdentifierExpr(node.else_expr, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = node;
          out.scrutinee =
              SubstituteIdentifierExpr(node.scrutinee, name, replacement);
          if (!PatternBindsName(node.pattern, name)) {
            out.then_expr =
                SubstituteIdentifierExpr(node.then_expr, name, replacement);
          }
          out.else_expr =
              SubstituteIdentifierExpr(node.else_expr, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = node;
          out.scrutinee =
              SubstituteIdentifierExpr(node.scrutinee, name, replacement);
          for (auto& arm : out.cases) {
            if (!PatternBindsName(arm.pattern, name)) {
              arm.body = SubstituteIdentifierExpr(arm.body, name, replacement);
            }
          }
          out.else_expr =
              SubstituteIdentifierExpr(node.else_expr, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierExpr(node.value, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = node;
          out.lhs = SubstituteIdentifierExpr(node.lhs, name, replacement);
          out.rhs = SubstituteIdentifierExpr(node.rhs, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierExpr(node.value, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          auto out = node;
          out.place = SubstituteIdentifierExpr(node.place, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          auto out = node;
          out.place = SubstituteIdentifierExpr(node.place, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierExpr(node.value, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          auto out = node;
          for (auto& element : out.elements) {
            element = SubstituteIdentifierExpr(element, name, replacement);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          auto out = node;
          for (auto& segment : out.elements) {
            std::visit(
                [&](auto& typed_segment) {
                  typed_segment.value =
                      SubstituteIdentifierExpr(typed_segment.value,
                                               name,
                                               replacement);
                  if constexpr (std::is_same_v<
                                    std::decay_t<decltype(typed_segment)>,
                                    ast::ArrayRepeatSegment>) {
                    typed_segment.count =
                        SubstituteIdentifierExpr(typed_segment.count,
                                                 name,
                                                 replacement);
                  }
                },
                segment);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierExpr(node.value, name, replacement);
          out.count = SubstituteIdentifierExpr(node.count, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          auto out = node;
          for (auto& field : out.fields) {
            field.value = SubstituteIdentifierExpr(field.value,
                                                   name,
                                                   replacement);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          auto out = node;
          if (out.block) {
            out.block = std::make_shared<ast::Block>(*out.block);
            out.block->tail_opt =
                SubstituteIdentifierExpr(out.block->tail_opt,
                                         name,
                                         replacement);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierExpr(node.value, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          auto out = node;
          out.expr = SubstituteIdentifierExpr(node.expr, name, replacement);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else {
          return expr;
        }
      },
      expr->node);
}

static TypeRef StripPermDeepLocal(const TypeRef& type) {
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

static bool IsRawPointerTypeLocal(const TypeRef& type) {
  const auto stripped = StripPermDeepLocal(type);
  return stripped && std::holds_alternative<TypeRawPtr>(stripped->node);
}

static bool HasLocalPointerProvenance(ProvenanceKind kind) {
  return kind == ProvenanceKind::Stack || kind == ProvenanceKind::Region;
}

static bool TypeContainsSafePointer(const TypeRef& type) {
  if (!type) {
    return false;
  }
  const TypeRef stripped = StripPermDeepLocal(type);
  if (!stripped) {
    return false;
  }
  if (IsSafePtrType(stripped)) {
    return true;
  }
  if (const auto* union_type = std::get_if<TypeUnion>(&stripped->node)) {
    for (const auto& member : union_type->members) {
      if (TypeContainsSafePointer(member)) {
        return true;
      }
    }
  }
  return false;
}

static std::optional<std::string_view>
CheckReturnedSafePointerProvenance(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const TypeEnv& env,
                                   const ExprTypeFn& type_expr,
                                   const ast::ExprPtr& ret_expr,
                                   const TypeRef& expected_return_type) {
  if (!ret_expr || !TypeContainsSafePointer(expected_return_type)) {
    return std::nullopt;
  }
  if (std::holds_alternative<ast::PtrNullExpr>(ret_expr->node)) {
    return std::nullopt;
  }

  const auto typed =
      TypeExprWithCurrentEnv(ctx, type_ctx, env, type_expr, ret_expr);
  if (!typed.ok) {
    return typed.diag_id;
  }
  if (!IsSafePtrType(typed.type)) {
    return std::nullopt;
  }

  const auto prov = TrackReturnProvenance(ctx, ret_expr, env);
  if (!prov.ok) {
    return prov.diag_id;
  }
  if (HasLocalPointerProvenance(prov.kind)) {
    SPEC_RULE("Prov-Return-Local-Warning");
    return "E-MEM-3020";
  }

  return std::nullopt;
}

static bool BindingProvenanceIsLocalFfi(const TypeBinding& binding) {
  return binding.provenance_kind == BindingProvenanceSeedKind::Region;
}

static std::optional<TypeBinding> BindingForFfiBoundaryExpr(
    const TypeEnv& env,
    const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  return std::visit(
      [&](const auto& node) -> std::optional<TypeBinding> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return BindOf(env, node.name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return BindingForFfiBoundaryExpr(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return BindingForFfiBoundaryExpr(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return BindingForFfiBoundaryExpr(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return BindingForFfiBoundaryExpr(env, node.value);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return BindingForFfiBoundaryExpr(env, node.place);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return BindingForFfiBoundaryExpr(env, node.expr);
        } else {
          return std::nullopt;
        }
      },
      expr->node);
}

static std::optional<std::string_view>
CheckFfiBoundaryRegionLocalRawPointerReturn(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const TypeEnv& env,
                                            const ExprTypeFn& type_expr,
                                            const ast::ExprPtr& ret_expr) {
  if (!type_ctx.ffi_export_boundary || !ret_expr) {
    return std::nullopt;
  }

  const auto typed =
      TypeExprWithCurrentEnv(ctx, type_ctx, env, type_expr, ret_expr);
  if (!typed.ok) {
    return typed.diag_id;
  }
  if (!IsRawPointerTypeLocal(typed.type)) {
    return std::nullopt;
  }

  if (const auto binding = BindingForFfiBoundaryExpr(env, ret_expr);
      binding.has_value() && BindingProvenanceIsLocalFfi(*binding)) {
    SPEC_RULE("FFI-Return-RegionLocalRawPtr-Err");
    return "E-SYS-3360";
  }

  const auto prov = TrackReturnProvenance(ctx, ret_expr, env);
  if (!prov.ok) {
    return prov.diag_id;
  }
  if (prov.kind != ProvenanceKind::Region) {
    return std::nullopt;
  }

  SPEC_RULE("FFI-Return-RegionLocalRawPtr-Err");
  return "E-SYS-3360";
}

static std::optional<TypeBinding::ClosureCaptureInfo> ReturnedClosureCaptureInfo(
    const ast::ExprPtr& ret_expr,
    const TypeEnv& env,
    const TypeRef& expected_closure_type) {
  if (!ret_expr) {
    return std::nullopt;
  }

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&ret_expr->node)) {
    const auto binding = BindOf(env, ident->name);
    if (!binding.has_value() || !binding->closure_capture_info.has_value()) {
      return std::nullopt;
    }
    auto info = *binding->closure_capture_info;
    info.has_shared_deps = info.has_shared_deps ||
                           ClosureTypeHasSharedDeps(binding->type) ||
                           ClosureTypeHasSharedDeps(expected_closure_type);
    return info;
  }

  auto info = AnalyzeClosureCaptureInfo(ret_expr, env, expected_closure_type);
  if (!info.has_value()) {
    return std::nullopt;
  }
  info->has_shared_deps =
      info->has_shared_deps || ClosureTypeHasSharedDeps(expected_closure_type);
  return info;
}

static std::optional<std::string_view> CheckEscapingClosureReturn(
    const ast::ExprPtr& ret_expr,
    const TypeEnv& env,
    const TypeRef& expected_closure_type) {
  const auto info = ReturnedClosureCaptureInfo(ret_expr, env, expected_closure_type);
  if (!info.has_value() || !info->captures_any) {
    return std::nullopt;
  }
  if (ClosureTypeHasSharedDeps(expected_closure_type) && info->contains_spawn) {
    SPEC_RULE("Parallel-Escaping-Closure-Spawn-Err");
    return "E-CON-0131";
  }
  if (info->captures_shared && !info->has_shared_deps) {
    SPEC_RULE("K-Closure-Missing-SharedDeps-Err");
    return "E-CON-0085";
  }
  return std::nullopt;
}

static void CollectContractFacts(const ast::ExprPtr& expr,
                                 StaticProofContext& ctx) {
  AddPredicateFacts(ctx, expr);
}

struct KnownPatternMatch {
  bool decidable = false;
  bool matches = false;
  PatternTypeResult pattern;
};

static bool TypeCanSelectPatternBranch(const TypeRef& type) {
  const TypeRef base = StripPermDeepLocal(type);
  if (!base) {
    return false;
  }
  if (std::holds_alternative<TypeUnion>(base->node) ||
      std::holds_alternative<TypeVar>(base->node) ||
      std::holds_alternative<TypeDynamic>(base->node) ||
      std::holds_alternative<TypeOpaque>(base->node)) {
    return false;
  }
  return true;
}

static KnownPatternMatch MatchPatternForKnownScrutinee(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const ExprTypeFn& type_expr,
    const ast::ExprPtr& scrutinee,
    const ast::PatternPtr& pattern) {
  KnownPatternMatch result;
  if (!scrutinee || !pattern) {
    return result;
  }

  const auto typed =
      TypeExprWithCurrentEnv(ctx, type_ctx, env, type_expr, scrutinee);
  if (!typed.ok || !TypeCanSelectPatternBranch(typed.type)) {
    return result;
  }

  const auto pattern_result =
      TypePatternAgainstType(ctx, pattern, StripPermDeepLocal(typed.type));
  if (pattern_result.ok) {
    result.decidable = true;
    result.matches = true;
    result.pattern = pattern_result;
    return result;
  }
  if (!pattern_result.diag_id.has_value()) {
    result.decidable = true;
    result.matches = false;
  }
  return result;
}

static ast::ExprPtr SubstituteWholePatternBindings(
    const ast::ExprPtr& body,
    const ast::PatternPtr& pattern,
    const ast::ExprPtr& scrutinee,
    const PatternTypeResult& pattern_result) {
  if (!PatternBindsWholeValue(pattern)) {
    return body;
  }

  ast::ExprPtr out = body;
  for (const auto& [name, _] : pattern_result.bindings) {
    out = SubstituteIdentifierExpr(out, name, scrutinee);
  }
  return out;
}

static ast::ExprPtr SimplifyReturnPostcondition(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const ExprTypeFn& type_expr,
    const ast::ExprPtr& expr);

static ast::ExprPtr SimplifyMatchedPatternBody(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const ExprTypeFn& type_expr,
    const ast::PatternPtr& pattern,
    const ast::ExprPtr& scrutinee,
    const ast::ExprPtr& body,
    const PatternTypeResult& pattern_result) {
  const ast::ExprPtr bound =
      SubstituteWholePatternBindings(body, pattern, scrutinee, pattern_result);
  return SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr, bound);
}

static ast::ExprPtr SimplifyReturnPostcondition(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const ExprTypeFn& type_expr,
    const ast::ExprPtr& expr) {
  if (!expr) {
    return expr;
  }

  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                node.lhs);
          out.rhs = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                node.rhs);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                  node.value);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                 node.base);
          if (const auto* record =
                  out.base ? std::get_if<ast::RecordExpr>(&out.base->node)
                           : nullptr) {
            for (const auto& field : record->fields) {
              if (IdEq(field.name, node.name)) {
                return SimplifyReturnPostcondition(ctx,
                                                   type_ctx,
                                                   env,
                                                   type_expr,
                                                   field.value);
              }
            }
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                 node.base);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                 node.base);
          out.index = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                  node.index);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block && node.block->stmts.empty() &&
              node.block->tail_opt) {
            return SimplifyReturnPostcondition(ctx,
                                               type_ctx,
                                               env,
                                               type_expr,
                                               node.block->tail_opt);
          }
          auto out = node;
          if (out.block) {
            out.block = std::make_shared<ast::Block>(*out.block);
            out.block->tail_opt = SimplifyReturnPostcondition(ctx,
                                                              type_ctx,
                                                              env,
                                                              type_expr,
                                                              out.block->tail_opt);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = node;
          out.cond = SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr,
                                                 node.cond);
          const auto cond_value = EvaluateConstant(out.cond);
          if (cond_value.known && cond_value.is_bool) {
            return SimplifyReturnPostcondition(
                ctx,
                type_ctx,
                env,
                type_expr,
                cond_value.bool_value ? node.then_expr : node.else_expr);
          }
          out.then_expr = SimplifyReturnPostcondition(ctx,
                                                      type_ctx,
                                                      env,
                                                      type_expr,
                                                      node.then_expr);
          out.else_expr = SimplifyReturnPostcondition(ctx,
                                                      type_ctx,
                                                      env,
                                                      type_expr,
                                                      node.else_expr);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = node;
          out.scrutinee = SimplifyReturnPostcondition(ctx,
                                                      type_ctx,
                                                      env,
                                                      type_expr,
                                                      node.scrutinee);
          const auto matched = MatchPatternForKnownScrutinee(
              ctx, type_ctx, env, type_expr, out.scrutinee, node.pattern);
          if (matched.decidable && matched.matches) {
            return SimplifyMatchedPatternBody(ctx,
                                             type_ctx,
                                             env,
                                             type_expr,
                                             node.pattern,
                                             out.scrutinee,
                                             node.then_expr,
                                             matched.pattern);
          }
          if (matched.decidable && !matched.matches && node.else_expr) {
            return SimplifyReturnPostcondition(ctx,
                                               type_ctx,
                                               env,
                                               type_expr,
                                               node.else_expr);
          }
          out.then_expr = SimplifyReturnPostcondition(ctx,
                                                      type_ctx,
                                                      env,
                                                      type_expr,
                                                      node.then_expr);
          out.else_expr = SimplifyReturnPostcondition(ctx,
                                                      type_ctx,
                                                      env,
                                                      type_expr,
                                                      node.else_expr);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = node;
          out.scrutinee = SimplifyReturnPostcondition(ctx,
                                                      type_ctx,
                                                      env,
                                                      type_expr,
                                                      node.scrutinee);
          bool all_arms_decidable_false = true;
          for (const auto& arm : node.cases) {
            const auto matched = MatchPatternForKnownScrutinee(
                ctx, type_ctx, env, type_expr, out.scrutinee, arm.pattern);
            if (!matched.decidable) {
              all_arms_decidable_false = false;
              break;
            }
            if (matched.matches) {
              return SimplifyMatchedPatternBody(ctx,
                                               type_ctx,
                                               env,
                                               type_expr,
                                               arm.pattern,
                                               out.scrutinee,
                                               arm.body,
                                               matched.pattern);
            }
          }
          if (all_arms_decidable_false && node.else_expr) {
            return SimplifyReturnPostcondition(ctx,
                                               type_ctx,
                                               env,
                                               type_expr,
                                               node.else_expr);
          }
          for (auto& arm : out.cases) {
            arm.body = SimplifyReturnPostcondition(ctx,
                                                   type_ctx,
                                                   env,
                                                   type_expr,
                                                   arm.body);
          }
          out.else_expr = SimplifyReturnPostcondition(ctx,
                                                      type_ctx,
                                                      env,
                                                      type_expr,
                                                      node.else_expr);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else {
          return expr;
        }
      },
      expr->node);
}

static std::optional<std::string_view> VerifyPostconditionAtReturn(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const ExprTypeFn& type_expr,
    const ast::ExprPtr& return_value) {
  if (!type_ctx.contract || !type_ctx.contract->postcondition) {
    return std::nullopt;
  }
  if (type_ctx.test_postcondition_runtime) {
    return std::nullopt;
  }
  const auto result_expr = return_value ? return_value
      : MakeUnitExpr(type_ctx.contract->postcondition->span);
  const auto pred =
      SubstituteResultEntry(type_ctx.contract->postcondition, result_expr,
                            type_ctx.env_ref);
  const auto simplified =
      SimplifyReturnPostcondition(ctx, type_ctx, env, type_expr, pred);
  StaticProofContext proof_ctx;
  if (type_ctx.proof_ctx) {
    proof_ctx = *type_ctx.proof_ctx;
  }
  if (type_ctx.contract->precondition) {
    CollectContractFacts(type_ctx.contract->precondition, proof_ctx);
  }
  const auto proof = StaticProofAt(proof_ctx, return_value ? return_value->span
                                                           : simplified->span,
                                   simplified);
  if (!proof.provable && !type_ctx.contract_dynamic) {
    return "E-SEM-2801";
  }
  return std::nullopt;
}

// Helper to substitute @result references in postcondition expression
static bool EntryExprCurrentValueStable(const ast::ExprPtr& expr,
                                        const TypeEnv* env) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LiteralExpr> ||
                      std::is_same_v<T, ast::PtrNullExpr> ||
                      std::is_same_v<T, ast::ResultExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (!env) {
            return false;
          }
          const auto mut = MutOf(*env, node.name);
          return mut.has_value() && *mut == ast::Mutability::Let;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return EntryExprCurrentValueStable(node.base, env);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return EntryExprCurrentValueStable(node.base, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return EntryExprCurrentValueStable(node.base, env) &&
                 EntryExprCurrentValueStable(node.index, env);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return EntryExprCurrentValueStable(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return EntryExprCurrentValueStable(node.lhs, env) &&
                 EntryExprCurrentValueStable(node.rhs, env);
        } else {
          return false;
        }
      },
      expr->node);
}

static ast::ExprPtr SubstituteResultEntry(const ast::ExprPtr& expr,
                                          const ast::ExprPtr& result_expr,
                                          const TypeEnv* env) {
  if (!expr) {
    return expr;
  }
  if (std::holds_alternative<ast::ResultExpr>(expr->node)) {
    return result_expr;
  }
  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SubstituteResultEntry(node.lhs, result_expr, env);
          out.rhs = SubstituteResultEntry(node.rhs, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteResultEntry(node.value, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          if (EntryExprCurrentValueStable(node.expr, env)) {
            return SubstituteResultEntry(node.expr, result_expr, env);
          }
          return expr;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteResultEntry(node.base, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteResultEntry(node.base, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteResultEntry(node.base, result_expr, env);
          out.index = SubstituteResultEntry(node.index, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = node;
          out.cond = SubstituteResultEntry(node.cond, result_expr, env);
          out.then_expr =
              SubstituteResultEntry(node.then_expr, result_expr, env);
          out.else_expr =
              SubstituteResultEntry(node.else_expr, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = node;
          out.scrutinee =
              SubstituteResultEntry(node.scrutinee, result_expr, env);
          out.then_expr =
              SubstituteResultEntry(node.then_expr, result_expr, env);
          out.else_expr =
              SubstituteResultEntry(node.else_expr, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = node;
          out.scrutinee =
              SubstituteResultEntry(node.scrutinee, result_expr, env);
          for (auto& arm : out.cases) {
            arm.body = SubstituteResultEntry(arm.body, result_expr, env);
          }
          out.else_expr =
              SubstituteResultEntry(node.else_expr, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          out.callee = SubstituteResultEntry(node.callee, result_expr, env);
          for (auto& arg : out.args) {
            arg.value = SubstituteResultEntry(arg.value, result_expr, env);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto out = node;
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            auto paren = std::get<ast::ParenArgs>(node.args);
            for (auto& arg : paren.args) {
              arg.value = SubstituteResultEntry(arg.value, result_expr, env);
            }
            out.args = std::move(paren);
          } else {
            auto brace = std::get<ast::BraceArgs>(node.args);
            for (auto& field : brace.fields) {
              field.value = SubstituteResultEntry(field.value, result_expr, env);
            }
            out.args = std::move(brace);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          out.receiver = SubstituteResultEntry(node.receiver, result_expr, env);
          for (auto& arg : out.args) {
            arg.value = SubstituteResultEntry(arg.value, result_expr, env);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteResultEntry(node.value, result_expr, env);
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          auto out = node;
          for (auto& field : out.fields) {
            field.value = SubstituteResultEntry(field.value, result_expr, env);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          auto out = node;
          if (out.block) {
            out.block = std::make_shared<ast::Block>(*out.block);
            out.block->tail_opt =
                SubstituteResultEntry(out.block->tail_opt, result_expr, env);
          }
          return MakeExprNodeLocal(expr->span, std::move(out));
        } else {
          return expr;
        }
      },
      expr->node);
}

}  // namespace

StmtTypeResult TypeReturnStmt(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::ReturnStmt& node,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place) {
  SpecDefsReturnStmt();

  auto type_expr_current = [&](const ast::ExprPtr& inner) {
    return TypeExprWithCurrentEnv(ctx, type_ctx, env, type_expr, inner);
  };

  const auto verify_post = [&](const ast::ExprPtr& value)
      -> std::optional<std::string_view> {
    return VerifyPostconditionAtReturn(ctx, type_ctx, env, type_expr_current,
                                       value);
  };

  const ast::ExprPtr ret_value =
      node.value_opt ? node.value_opt : ast::ExprPtr{};

  // Check if return type is async
  const auto async_sig = AsyncSigOf(ctx, type_ctx.return_type);
  if (async_sig.has_value()) {
    if (node.value_opt) {
      const auto check =
          CheckExprAgainst(ctx, type_ctx, node.value_opt, async_sig->result, env);
      if (!check.ok) {
        if (!check.diag_id.has_value() || *check.diag_id == "E-SEM-2526") {
          SPEC_RULE("Return-Async-Type-Err");
          return {false, "E-CON-0203", {}, {}};
        }
        return {false, check.diag_id, {}, {}};
      }
      if (const auto diag =
              CheckEscapingClosureReturn(node.value_opt, env, async_sig->result);
          diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      if (const auto diag = CheckFfiBoundaryRegionLocalRawPointerReturn(
              ctx, type_ctx, env, type_expr_current, node.value_opt);
          diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      if (const auto diag = CheckReturnedSafePointerProvenance(
              ctx, type_ctx, env, type_expr_current, node.value_opt,
              async_sig->result);
          diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      if (const auto diag = verify_post(ret_value); diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      SPEC_RULE("T-Return-Value");
      return {true, std::nullopt, env, {}};
    }
    if (!IsPrimType(async_sig->result, "()")) {
      SPEC_RULE("Return-Async-Unit-Err");
      return {false, "E-CON-0203", {}, {}};
    }
    if (const auto diag = verify_post(ret_value); diag.has_value()) {
      return {false, *diag, {}, {}};
    }
    SPEC_RULE("T-Return-Unit");
    return {true, std::nullopt, env, {}};
  }

  // Non-async return with value
  if (node.value_opt) {
    // Handle opaque return types
    if (type_ctx.opaque_return) {
      const auto typed = type_expr_current(node.value_opt);
      if (!typed.ok) {
        return {false, typed.diag_id, {}, {}, typed.diag_detail};
      }
      if (!TypeImplementsClass(ctx, typed.type,
                               type_ctx.opaque_return->class_path)) {
        SPEC_RULE("T-Opaque-Return");
        return {false, "E-TYP-2511", {}, {}};
      }
      if (type_ctx.opaque_return->underlying) {
        const auto equiv =
            TypeEquiv(type_ctx.opaque_return->underlying, typed.type);
        if (!equiv.ok) {
          return {false, equiv.diag_id, {}, {}};
        }
        if (!equiv.equiv) {
          SPEC_RULE("T-Opaque-Return");
          return {false, "E-TYP-2512", {}, {}};
        }
      } else {
        type_ctx.opaque_return->underlying = typed.type;
      }
      if (const auto diag =
              CheckEscapingClosureReturn(node.value_opt, env, typed.type);
          diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      if (const auto diag = CheckFfiBoundaryRegionLocalRawPointerReturn(
              ctx, type_ctx, env, type_expr_current, node.value_opt);
          diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      if (const auto diag = CheckReturnedSafePointerProvenance(
              ctx, type_ctx, env, type_expr_current, node.value_opt, typed.type);
          diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      if (const auto diag = verify_post(ret_value); diag.has_value()) {
        return {false, *diag, {}, {}};
      }
      SPEC_RULE("T-Opaque-Return");
      SPEC_RULE("T-Return-Value");
      return {true, std::nullopt, env, {}};
    }

    // Normal return with value
    const auto check = CheckExprAgainst(ctx, type_ctx, node.value_opt,
                                        type_ctx.return_type, env);
    if (!check.ok) {
      if (!check.diag_id.has_value() || *check.diag_id == "E-SEM-2526") {
        SPEC_RULE("Return-Type-Err");
        return {false, "E-SEM-3161", {}, {}};
      }
      return {false, check.diag_id, {}, {}};
    }
    if (const auto diag =
            CheckEscapingClosureReturn(node.value_opt, env, type_ctx.return_type);
        diag.has_value()) {
      return {false, *diag, {}, {}};
    }
    if (const auto diag = CheckFfiBoundaryRegionLocalRawPointerReturn(
            ctx, type_ctx, env, type_expr_current, node.value_opt);
        diag.has_value()) {
      return {false, *diag, {}, {}};
    }
    if (const auto diag = CheckReturnedSafePointerProvenance(
            ctx, type_ctx, env, type_expr_current, node.value_opt,
            type_ctx.return_type);
        diag.has_value()) {
      return {false, *diag, {}, {}};
    }
    if (const auto diag = verify_post(ret_value); diag.has_value()) {
      return {false, *diag, {}, {}};
    }
    SPEC_RULE("T-Return-Value");
    return {true, std::nullopt, env, {}};
  }

  // Return without value - procedure must have () return type
  if (!IsPrimType(type_ctx.return_type, "()")) {
    SPEC_RULE("Return-Unit-Err");
    return {false, "E-SEM-3161", {}, {}};
  }
  if (const auto diag = verify_post(ret_value); diag.has_value()) {
    return {false, *diag, {}, {}};
  }
  SPEC_RULE("T-Return-Unit");
  return {true, std::nullopt, env, {}};
}

}  // namespace cursive::analysis
