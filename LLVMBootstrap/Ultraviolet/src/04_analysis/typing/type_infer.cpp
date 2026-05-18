// =============================================================================
// MIGRATION MAPPING: type_infer.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.2.12: Expression Typing (lines 9778-9798)
//   Section 9.4: Type Inference (referenced at line 22083, 22440)
//   - ExprJudg = {Gamma; R; L |- e : T, Gamma; R; L |- e <= T, ...}
//   - Lift-Expr (lines 9789-9792): Lifting simple judgments
//   - Bidirectional type inference: synthesis (=>) and checking (<=)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/types/type_infer.cpp
//   Lines 1-729: Full implementation
//   Key functions:
//   - InferExpr(Env, Expr) -> Type (synthesis mode)
//   - CheckExpr(Env, Expr, Type) -> bool (checking mode)
//   - UnifyTypes(Env, Type, Type) -> Type (constraint solving)
//   - Solve(Constraints) -> Substitution
//
// KEY CONTENT TO MIGRATE:
//   BIDIRECTIONAL TYPE INFERENCE:
//   Synthesis (=>): Derive type from expression
//   Checking (<=): Verify expression against expected type
//
//   SYNTHESIS MODE (InferExpr):
//   - Literals: known types (integers, floats, bools, chars, strings)
//   - Identifiers: lookup in environment
//   - Field access: receiver type determines field type
//   - Method calls: receiver + method signature
//   - Function calls: callee type determines result
//   - Binary/unary ops: operand types determine result
//   - If/if-case: branch type unification
//   - Block: final expression or unit
//
//   CHECKING MODE (CheckExpr):
//   - Verify synthesized type is subtype of expected
//   - Propagate expected type for better inference
//   - Handle type annotation contexts
//
//   CONSTRAINT GENERATION:
//   - Type variables for unknowns
//   - Equality constraints from unification
//   - Subtyping constraints from coercion points
//
//   CONSTRAINT SOLVING (Solve):
//   - Unification algorithm
//   - Substitution application
//   - Error on unsatisfiable constraints
//
// DEPENDENCIES:
//   - TypeEquiv() for type comparison
//   - Subtyping() for subtype checks
//   - LowerType() for type annotation processing
//   - Environment for binding lookups
//   - Constraint representation
//
// SPEC RULES:
//   - Lift-Expr: Context lifting for simple judgments
//   - All T-* expression rules use inference
//   - T-LetStmt-Infer-Err, T-VarStmt-Infer-Err for failures
//
// RESULT:
//   Type (for synthesis) or bool (for checking)
//   Diagnostics for inference failures
//
// NOTES:
//   - Inference flows bidirectionally
//   - Some expressions require checking mode (e.g., Ptr::null())
//   - Type variables resolved by Solve()
//
// =============================================================================

#include "04_analysis/typing/type_infer.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/typing/literals.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/composite/arrays_slices.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/composite/records.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/memory/safe_ptr.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/expr/call.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/memory/calls.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsTypeInfer() {
  SPEC_DEF("TypeInfJudg", "5.2.9");
  SPEC_DEF("Constraint", "5.2.9");
  SPEC_DEF("ConstraintSet", "5.2.9");
  SPEC_DEF("Solve", "5.2.9");
  SPEC_DEF("Syn-Call-Err", "5.2.9");
  SPEC_DEF("StripPerm", "5.2.12");
  SPEC_DEF("UnsafeSpan", "5.2.12");
  SPEC_DEF("PtrNullExpected", "5.2.9");
  SPEC_DEF("NicheCompatible", "5.7");
  SPEC_DEF("GpuPtr-AddrSpace-Err", "20.2.4");
  SPEC_DEF("Chk-Subsumption-Err", "5.2.12");
  SPEC_DEF("ValueUse-NonBitcopyPlace", "5.2.12");
  SpecDefsSafePtr();
}

struct GpuPtrTypeView {
  TypeRef pointee;
  TypeRef address_space;
};

static std::optional<GpuPtrTypeView> AsGpuPtrType(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return std::nullopt;
  }
  const TypePath* path = AppliedTypePath(*stripped);
  const std::vector<TypeRef>* args = AppliedTypeArgs(*stripped);
  if (!path || !args || *path != TypePath{"GpuPtr"} || args->size() != 2) {
    return std::nullopt;
  }
  return GpuPtrTypeView{(*args)[0], (*args)[1]};
}

static bool IsGpuPtrAddrSpaceMismatch(const TypeRef& actual,
                                      const TypeRef& expected) {
  const auto actual_gpu = AsGpuPtrType(actual);
  const auto expected_gpu = AsGpuPtrType(expected);
  if (!actual_gpu.has_value() || !expected_gpu.has_value()) {
    return false;
  }
  const auto pointee_eq =
      TypeEquiv(actual_gpu->pointee, expected_gpu->pointee);
  if (!pointee_eq.ok || !pointee_eq.equiv) {
    return false;
  }
  const auto addr_space_eq =
      TypeEquiv(actual_gpu->address_space, expected_gpu->address_space);
  return addr_space_eq.ok && !addr_space_eq.equiv;
}

static bool HasMemoryOrderAttribute(const ast::AttributeList& attrs_list) {
  return HasAttribute(attrs_list, attrs::kRelaxed) ||
         HasAttribute(attrs_list, attrs::kAcquire) ||
         HasAttribute(attrs_list, attrs::kRelease) ||
         HasAttribute(attrs_list, attrs::kAcqRel) ||
         HasAttribute(attrs_list, attrs::kSeqCst);
}

static bool PtrNullExpected(const TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return PtrNullExpected(perm->base);
  }
  if (const auto* refine = std::get_if<TypeRefine>(&type->node)) {
    return PtrNullExpected(refine->base);
  }
  const auto* ptr = std::get_if<TypePtr>(&type->node);
  if (!ptr) {
    return false;
  }
  if (!ptr->state.has_value()) {
    return true;
  }
  return ptr->state == PtrState::Null;
}

static bool IsExplicitMoveExpr(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::MoveExpr>(expr->node);
}

static bool IsNonBitcopyPlaceValueUse(const ScopeContext& ctx,
                                      const ast::ExprPtr& expr,
                                      const TypeRef& type) {
  return expr && !IsExplicitMoveExpr(expr) && IsPlaceExprForCall(expr) &&
         !BitcopyType(ctx, type);
}

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

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

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

static AliasExpandResult NormalizeAliasType(const ScopeContext& ctx,
                                            const TypeRef& type) {
  AliasExpandResult out;
  out.type = type;
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
    out.type = expanded.type;
    out.expanded = true;
  }
  return out;
}

static bool IsSignedIntOrFloatType(std::string_view name) {
  static constexpr std::array<std::string_view, 6> kSignedIntTypes = {
      "i8", "i16", "i32", "i64", "i128", "isize"};
  static constexpr std::array<std::string_view, 3> kFloatTypes = {
      "f16", "f32", "f64"};
  for (const auto& t : kSignedIntTypes) {
    if (name == t) {
      return true;
    }
  }
  for (const auto& t : kFloatTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

static bool IsGpuIntrinsicCallee(const ast::ExprPtr& callee) {
  if (!callee) {
    return false;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    return IsGpuIntrinsicName(ident->name);
  }
  if (const auto* path = std::get_if<ast::PathExpr>(&callee->node)) {
    return IsGpuIntrinsicName(path->name);
  }
  return false;
}

static ast::ExprPtr MakeExpr(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static ast::ExprPtr SubstituteIdent(const ast::ExprPtr& expr,
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
          out.lhs = SubstituteIdent(node.lhs, name, replacement);
          out.rhs = SubstituteIdent(node.rhs, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdent(node.base, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdent(node.base, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdent(node.base, name, replacement);
          out.index = SubstituteIdent(node.index, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          out.callee = SubstituteIdent(node.callee, name, replacement);
          for (auto& arg : out.args) {
            arg.value = SubstituteIdent(arg.value, name, replacement);
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto out = node;
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            auto paren = std::get<ast::ParenArgs>(node.args);
            for (auto& arg : paren.args) {
              arg.value = SubstituteIdent(arg.value, name, replacement);
            }
            out.args = paren;
          } else {
            auto brace = std::get<ast::BraceArgs>(node.args);
            for (auto& field : brace.fields) {
              field.value = SubstituteIdent(field.value, name, replacement);
            }
            out.args = brace;
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          out.receiver = SubstituteIdent(node.receiver, name, replacement);
          for (auto& arg : out.args) {
            arg.value = SubstituteIdent(arg.value, name, replacement);
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = node;
          out.lhs = SubstituteIdent(node.lhs, name, replacement);
          out.rhs = SubstituteIdent(node.rhs, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          auto out = node;
          out.place = SubstituteIdent(node.place, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          auto out = node;
          out.place = SubstituteIdent(node.place, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          auto out = node;
          for (auto& elem : out.elements) {
            elem = SubstituteIdent(elem, name, replacement);
          }
          return MakeExpr(expr->span, out);
      } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
        auto out = node;
        for (auto& segment : out.elements) {
          std::visit(
              [&](auto& seg) {
                seg.value = SubstituteIdent(seg.value, name, replacement);
                if constexpr (std::is_same_v<std::decay_t<decltype(seg)>,
                                             ast::ArrayRepeatSegment>) {
                  seg.count = SubstituteIdent(seg.count, name, replacement);
                }
              },
              segment);
        }
        return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          out.count = SubstituteIdent(node.count, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          return expr;
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          return expr;
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          auto out = node;
          for (auto& field : out.fields) {
            field.value = SubstituteIdent(field.value, name, replacement);
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          auto out = node;
          if (out.payload_opt.has_value()) {
            if (std::holds_alternative<ast::EnumPayloadParen>(*out.payload_opt)) {
              auto paren = std::get<ast::EnumPayloadParen>(*out.payload_opt);
              for (auto& elem : paren.elements) {
                elem = SubstituteIdent(elem, name, replacement);
              }
              out.payload_opt = paren;
            } else {
              auto brace = std::get<ast::EnumPayloadBrace>(*out.payload_opt);
              for (auto& field : brace.fields) {
                field.value = SubstituteIdent(field.value, name, replacement);
              }
              out.payload_opt = brace;
            }
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = node;
          out.cond = SubstituteIdent(node.cond, name, replacement);
          out.then_expr = SubstituteIdent(node.then_expr, name, replacement);
          out.else_expr = SubstituteIdent(node.else_expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = node;
          out.scrutinee = SubstituteIdent(node.scrutinee, name, replacement);
          for (auto& arm : out.cases) {
            arm.body = SubstituteIdent(arm.body, name, replacement);
          }
          out.else_expr = SubstituteIdent(node.else_expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = node;
          out.scrutinee = SubstituteIdent(node.scrutinee, name, replacement);
          out.then_expr = SubstituteIdent(node.then_expr, name, replacement);
          out.else_expr = SubstituteIdent(node.else_expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          auto out = node;
          out.expr = SubstituteIdent(node.expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else {
          return expr;
        }
      },
      expr->node);
}

static bool ProveRefinePredicate(const ast::ExprPtr& value,
                                 const TypeRefine& refine,
                                 std::optional<std::string_view>& diag_id) {
  if (!refine.predicate) {
    return false;
  }
  const auto substituted =
      SubstituteIdent(refine.predicate, "self", value);
  StaticProofContext proof_ctx;
  const auto proof = StaticProofAt(proof_ctx, value ? value->span : substituted->span,
                                   substituted);
  if (!proof.provable) {
    diag_id = "E-TYP-1953";
    return false;
  }
  return true;
}

static bool TypePathEq(const TypePath& lhs, const TypePath& rhs) {
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

static bool ModalNonNiche(const ScopeContext& ctx,
                          const TypeRef& source,
                          const TypeRef& target) {
  const auto stripped_source = StripPerm(source);
  const auto stripped_target = StripPerm(target);
  if (!stripped_source || !stripped_target) {
    return false;
  }
    const auto* modal = std::get_if<TypeModalState>(&stripped_source->node);
    const auto* target_path = AppliedTypePath(*stripped_target);
    const auto* target_args = AppliedTypeArgs(*stripped_target);
    if (!modal || !target_path || !target_args) {
      return false;
    }
    if (!TypePathEq(modal->path, *target_path)) {
      return false;
    }
    if (modal->generic_args.size() != target_args->size()) {
      return false;
    }
  const auto* decl = LookupModalDecl(ctx, modal->path);
  if (!decl) {
    return false;
  }
  if (!HasState(*decl, modal->state)) {
    return false;
  }
  return !NicheCompatible(ctx, modal->path, modal->state);
}

static TypeRef ApplySubstitutionImpl(const TypeRef& type,
                                     const TypeSubstitution& subst,
                                     std::unordered_set<std::uint32_t>& active);

static bool OccursInType(std::uint32_t id,
                         const TypeRef& type,
                         const TypeSubstitution& subst,
                         std::unordered_set<std::uint32_t>& seen) {
  if (!type) {
    return false;
  }
  if (const auto* var = std::get_if<TypeVar>(&type->node)) {
    if (var->id == id) {
      return true;
    }
    if (!seen.insert(var->id).second) {
      return false;
    }
    const auto it = subst.find(var->id);
    if (it == subst.end()) {
      return false;
    }
    return OccursInType(id, it->second, subst, seen);
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim> ||
                      std::is_same_v<T, TypeString> ||
                      std::is_same_v<T, TypeBytes> ||
                      std::is_same_v<T, TypeDynamic> ||
                      std::is_same_v<T, TypeOpaque> ||
                      std::is_same_v<T, TypeRangeFull>) {
          return false;
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          for (const auto& arg : node.generic_args) {
            if (OccursInType(id, arg, subst, seen)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            if (OccursInType(id, arg, subst, seen)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return OccursInType(id, node.base, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          for (const auto& member : node.members) {
            if (OccursInType(id, member, subst, seen)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          for (const auto& param : node.params) {
            if (OccursInType(id, param.type, subst, seen)) {
              return true;
            }
          }
          return OccursInType(id, node.ret, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (OccursInType(id, elem, subst, seen)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return OccursInType(id, node.element, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return OccursInType(id, node.element, subst, seen);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return OccursInType(id, node.element, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return OccursInType(id, node.element, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return OccursInType(id, node.base, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          return OccursInType(id, node.base, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          return OccursInType(id, node.base, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          return OccursInType(id, node.base, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          return OccursInType(id, node.base, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          return OccursInType(id, node.base, subst, seen);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          for (const auto& [is_move, param] : node.params) {
            (void)is_move;
            if (OccursInType(id, param, subst, seen)) {
              return true;
            }
          }
          if (OccursInType(id, node.ret, subst, seen)) {
            return true;
          }
          if (!node.deps_opt.has_value()) {
            return false;
          }
          for (const auto& dep : *node.deps_opt) {
            if (OccursInType(id, dep.type, subst, seen)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeVar>) {
          return false;
        } else {
          return false;
        }
      },
      type->node);
}

static bool BindTypeVar(std::uint32_t id,
                        const TypeRef& rhs,
                        TypeSubstitution& subst) {
  if (!rhs) {
    return false;
  }
  if (const auto* rhs_var = std::get_if<TypeVar>(&rhs->node)) {
    if (rhs_var->id == id) {
      return true;
    }
  }
  std::unordered_set<std::uint32_t> seen;
  if (OccursInType(id, rhs, subst, seen)) {
    return false;
  }
  subst[id] = rhs;
  return true;
}

static bool RefinePredicatesEqual(const ast::ExprPtr& lhs,
                                  const ast::ExprPtr& rhs) {
  if (!lhs || !rhs) {
    return lhs == rhs;
  }
  return ExprStructEqual(lhs, rhs);
}

static bool UnifyEq(const ScopeContext& ctx,
                    const TypeRef& lhs_raw,
                    const TypeRef& rhs_raw,
                    TypeSubstitution& subst,
                    std::optional<std::string_view>& diag_id) {
  std::unordered_set<std::uint32_t> active;
  const auto lhs = ApplySubstitutionImpl(lhs_raw, subst, active);
  active.clear();
  const auto rhs = ApplySubstitutionImpl(rhs_raw, subst, active);
  if (!lhs || !rhs) {
    diag_id = "Syn-Call-Err";
    return false;
  }

  if (const auto* lvar = std::get_if<TypeVar>(&lhs->node)) {
    if (!BindTypeVar(lvar->id, rhs, subst)) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return true;
  }
  if (const auto* rvar = std::get_if<TypeVar>(&rhs->node)) {
    if (!BindTypeVar(rvar->id, lhs, subst)) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return true;
  }

  if (const auto* lrefine = std::get_if<TypeRefine>(&lhs->node)) {
    const auto* rrefine = std::get_if<TypeRefine>(&rhs->node);
    if (!rrefine) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    if (!RefinePredicatesEqual(lrefine->predicate, rrefine->predicate)) {
      SPEC_RULE("Unify-Refine-Pred-Fail");
      diag_id = "Syn-Call-Err";
      return false;
    }
    SPEC_RULE("Unify-Refine");
    return UnifyEq(ctx, lrefine->base, rrefine->base, subst, diag_id);
  }

  const auto eq = TypeEquiv(lhs, rhs);
  if (!eq.ok) {
    diag_id = eq.diag_id;
    return false;
  }
  if (eq.equiv) {
    return true;
  }

  if (const auto* lperm = std::get_if<TypePerm>(&lhs->node)) {
    const auto* rperm = std::get_if<TypePerm>(&rhs->node);
    if (!rperm || lperm->perm != rperm->perm) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lperm->base, rperm->base, subst, diag_id);
  }
  if (const auto* ltuple = std::get_if<TypeTuple>(&lhs->node)) {
    const auto* rtuple = std::get_if<TypeTuple>(&rhs->node);
    if (!rtuple || ltuple->elements.size() != rtuple->elements.size()) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    for (std::size_t i = 0; i < ltuple->elements.size(); ++i) {
      if (!UnifyEq(ctx, ltuple->elements[i], rtuple->elements[i], subst,
                   diag_id)) {
        return false;
      }
    }
    return true;
  }
  if (const auto* larray = std::get_if<TypeArray>(&lhs->node)) {
    const auto* rarray = std::get_if<TypeArray>(&rhs->node);
    if (!rarray || larray->length != rarray->length) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, larray->element, rarray->element, subst, diag_id);
  }
  if (const auto* lslice = std::get_if<TypeSlice>(&lhs->node)) {
    const auto* rslice = std::get_if<TypeSlice>(&rhs->node);
    if (!rslice) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lslice->element, rslice->element, subst, diag_id);
  }
  if (const auto* lfunc = std::get_if<TypeFunc>(&lhs->node)) {
    const auto* rfunc = std::get_if<TypeFunc>(&rhs->node);
    if (!rfunc || lfunc->params.size() != rfunc->params.size()) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    for (std::size_t i = 0; i < lfunc->params.size(); ++i) {
      if (lfunc->params[i].mode != rfunc->params[i].mode) {
        diag_id = "Syn-Call-Err";
        return false;
      }
      if (!UnifyEq(ctx, lfunc->params[i].type, rfunc->params[i].type, subst,
                   diag_id)) {
        return false;
      }
    }
    return UnifyEq(ctx, lfunc->ret, rfunc->ret, subst, diag_id);
  }
    if (const auto* lpath = std::get_if<TypePathType>(&lhs->node)) {
      const auto* rpath = AppliedTypePath(*rhs);
      const auto* rargs = AppliedTypeArgs(*rhs);
      if (!rpath || !rargs || !TypePathEq(lpath->path, *rpath) ||
          lpath->generic_args.size() != rargs->size()) {
        diag_id = "Syn-Call-Err";
        return false;
      }
      for (std::size_t i = 0; i < lpath->generic_args.size(); ++i) {
        if (!UnifyEq(ctx, lpath->generic_args[i], (*rargs)[i], subst,
                     diag_id)) {
          return false;
        }
      }
      return true;
    }
    if (const auto* lapply = std::get_if<TypeApply>(&lhs->node)) {
      const auto* rpath = AppliedTypePath(*rhs);
      const auto* rargs = AppliedTypeArgs(*rhs);
      if (!rpath || !rargs || !TypePathEq(lapply->path, *rpath) ||
          lapply->args.size() != rargs->size()) {
        diag_id = "Syn-Call-Err";
        return false;
      }
      for (std::size_t i = 0; i < lapply->args.size(); ++i) {
        if (!UnifyEq(ctx, lapply->args[i], (*rargs)[i], subst, diag_id)) {
          return false;
        }
      }
      return true;
    }
  if (const auto* lmodal = std::get_if<TypeModalState>(&lhs->node)) {
    const auto* rmodal = std::get_if<TypeModalState>(&rhs->node);
    if (!rmodal || !TypePathEq(lmodal->path, rmodal->path) ||
        !IdEq(lmodal->state, rmodal->state) ||
        lmodal->generic_args.size() != rmodal->generic_args.size()) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    for (std::size_t i = 0; i < lmodal->generic_args.size(); ++i) {
      if (!UnifyEq(ctx, lmodal->generic_args[i], rmodal->generic_args[i], subst,
                   diag_id)) {
        return false;
      }
    }
    return true;
  }
  if (const auto* lptr = std::get_if<TypePtr>(&lhs->node)) {
    const auto* rptr = std::get_if<TypePtr>(&rhs->node);
    if (!rptr || lptr->state != rptr->state) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lptr->element, rptr->element, subst, diag_id);
  }
  if (const auto* lraw = std::get_if<TypeRawPtr>(&lhs->node)) {
    const auto* rraw = std::get_if<TypeRawPtr>(&rhs->node);
    if (!rraw || lraw->qual != rraw->qual) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lraw->element, rraw->element, subst, diag_id);
  }
  if (const auto* lrange = std::get_if<TypeRange>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRange>(&rhs->node);
    if (!rrange) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lrange->base, rrange->base, subst, diag_id);
  }
  if (const auto* lrange = std::get_if<TypeRangeInclusive>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeInclusive>(&rhs->node);
    if (!rrange) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lrange->base, rrange->base, subst, diag_id);
  }
  if (const auto* lrange = std::get_if<TypeRangeFrom>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeFrom>(&rhs->node);
    if (!rrange) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lrange->base, rrange->base, subst, diag_id);
  }
  if (const auto* lrange = std::get_if<TypeRangeTo>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeTo>(&rhs->node);
    if (!rrange) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lrange->base, rrange->base, subst, diag_id);
  }
  if (const auto* lrange = std::get_if<TypeRangeToInclusive>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeToInclusive>(&rhs->node);
    if (!rrange) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    return UnifyEq(ctx, lrange->base, rrange->base, subst, diag_id);
  }
  if (std::holds_alternative<TypeRangeFull>(lhs->node) &&
      std::holds_alternative<TypeRangeFull>(rhs->node)) {
    return true;
  }
  if (const auto* lclosure = std::get_if<TypeClosure>(&lhs->node)) {
    const auto* rclosure = std::get_if<TypeClosure>(&rhs->node);
    if (!rclosure || lclosure->params.size() != rclosure->params.size()) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    for (std::size_t i = 0; i < lclosure->params.size(); ++i) {
      if (lclosure->params[i].first != rclosure->params[i].first) {
        diag_id = "Syn-Call-Err";
        return false;
      }
      if (!UnifyEq(ctx, lclosure->params[i].second, rclosure->params[i].second,
                   subst, diag_id)) {
        return false;
      }
    }
    if (!UnifyEq(ctx, lclosure->ret, rclosure->ret, subst, diag_id)) {
      return false;
    }
    if (lclosure->deps_opt.has_value() != rclosure->deps_opt.has_value()) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    if (lclosure->deps_opt.has_value()) {
      if (lclosure->deps_opt->size() != rclosure->deps_opt->size()) {
        diag_id = "Syn-Call-Err";
        return false;
      }
      for (std::size_t i = 0; i < lclosure->deps_opt->size(); ++i) {
        if (!IdEq((*lclosure->deps_opt)[i].name, (*rclosure->deps_opt)[i].name)) {
          diag_id = "Syn-Call-Err";
          return false;
        }
        if (!UnifyEq(ctx, (*lclosure->deps_opt)[i].type,
                     (*rclosure->deps_opt)[i].type, subst, diag_id)) {
          return false;
        }
      }
    }
    return true;
  }
  if (const auto* luni = std::get_if<TypeUnion>(&lhs->node)) {
    const auto* runi = std::get_if<TypeUnion>(&rhs->node);
    if (!runi || luni->members.size() != runi->members.size()) {
      diag_id = "Syn-Call-Err";
      return false;
    }
    const auto lhs_sorted = SortUnionMembers(luni->members);
    const auto rhs_sorted = SortUnionMembers(runi->members);
    for (std::size_t i = 0; i < lhs_sorted.size(); ++i) {
      if (!UnifyEq(ctx, lhs_sorted[i], rhs_sorted[i], subst, diag_id)) {
        return false;
      }
    }
    return true;
  }

  diag_id = "Syn-Call-Err";
  return false;
}

static TypeRef ApplySubstitutionImpl(const TypeRef& type,
                                     const TypeSubstitution& subst,
                                     std::unordered_set<std::uint32_t>& active) {
  if (!type) {
    return type;
  }
  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypeVar>) {
          const auto it = subst.find(node.id);
          if (it == subst.end() || !it->second) {
            return type;
          }
          if (!active.insert(node.id).second) {
            return type;
          }
          auto out = ApplySubstitutionImpl(it->second, subst, active);
          active.erase(node.id);
          return out ? out : type;
        } else if constexpr (std::is_same_v<T, TypePrim> ||
                             std::is_same_v<T, TypeString> ||
                             std::is_same_v<T, TypeBytes> ||
                             std::is_same_v<T, TypeDynamic> ||
                             std::is_same_v<T, TypeOpaque> ||
                             std::is_same_v<T, TypeRangeFull>) {
          return type;
          } else if constexpr (std::is_same_v<T, TypePathType>) {
            std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          bool changed = false;
          for (const auto& arg : node.generic_args) {
            const auto applied = ApplySubstitutionImpl(arg, subst, active);
            changed = changed || (applied != arg);
            args.push_back(applied);
            }
            return changed ? MakeTypePath(node.path, std::move(args)) : type;
          } else if constexpr (std::is_same_v<T, TypeApply>) {
            std::vector<TypeRef> args;
            args.reserve(node.args.size());
            bool changed = false;
            for (const auto& arg : node.args) {
              const auto applied = ApplySubstitutionImpl(arg, subst, active);
              changed = changed || (applied != arg);
              args.push_back(applied);
            }
            return changed ? MakeTypeApply(node.path, std::move(args)) : type;
          } else if constexpr (std::is_same_v<T, TypeModalState>) {
          std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          bool changed = false;
          for (const auto& arg : node.generic_args) {
            const auto applied = ApplySubstitutionImpl(arg, subst, active);
            changed = changed || (applied != arg);
            args.push_back(applied);
          }
          return changed
                     ? MakeTypeModalState(node.path, node.state, std::move(args))
                     : type;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          const auto base = ApplySubstitutionImpl(node.base, subst, active);
          return base == node.base ? type : MakeTypePerm(node.perm, base);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          std::vector<TypeRef> members;
          members.reserve(node.members.size());
          bool changed = false;
          for (const auto& member : node.members) {
            const auto applied = ApplySubstitutionImpl(member, subst, active);
            changed = changed || (applied != member);
            members.push_back(applied);
          }
          return changed ? MakeTypeUnion(std::move(members)) : type;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          std::vector<TypeFuncParam> params;
          params.reserve(node.params.size());
          bool changed = false;
          for (const auto& param : node.params) {
            const auto applied = ApplySubstitutionImpl(param.type, subst, active);
            changed = changed || (applied != param.type);
            params.push_back(TypeFuncParam{param.mode, applied});
          }
          const auto ret = ApplySubstitutionImpl(node.ret, subst, active);
          changed = changed || (ret != node.ret);
          return changed ? MakeTypeFunc(std::move(params), ret) : type;
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          std::vector<TypeRef> elements;
          elements.reserve(node.elements.size());
          bool changed = false;
          for (const auto& elem : node.elements) {
            const auto applied = ApplySubstitutionImpl(elem, subst, active);
            changed = changed || (applied != elem);
            elements.push_back(applied);
          }
          return changed ? MakeTypeTuple(std::move(elements)) : type;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          const auto elem = ApplySubstitutionImpl(node.element, subst, active);
          return elem == node.element
                     ? type
                     : MakeTypeArray(elem, node.length, node.length_expr_text);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          const auto elem = ApplySubstitutionImpl(node.element, subst, active);
          return elem == node.element ? type : MakeTypeSlice(elem);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          const auto elem = ApplySubstitutionImpl(node.element, subst, active);
          return elem == node.element ? type : MakeTypePtr(elem, node.state);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          const auto elem = ApplySubstitutionImpl(node.element, subst, active);
          return elem == node.element ? type : MakeTypeRawPtr(node.qual, elem);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          const auto base = ApplySubstitutionImpl(node.base, subst, active);
          return base == node.base ? type : MakeTypeRefine(base, node.predicate);
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          const auto base = ApplySubstitutionImpl(node.base, subst, active);
          return base == node.base ? type : MakeTypeRange(base);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          const auto base = ApplySubstitutionImpl(node.base, subst, active);
          return base == node.base ? type : MakeTypeRangeInclusive(base);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          const auto base = ApplySubstitutionImpl(node.base, subst, active);
          return base == node.base ? type : MakeTypeRangeFrom(base);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          const auto base = ApplySubstitutionImpl(node.base, subst, active);
          return base == node.base ? type : MakeTypeRangeTo(base);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          const auto base = ApplySubstitutionImpl(node.base, subst, active);
          return base == node.base ? type : MakeTypeRangeToInclusive(base);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> params;
          params.reserve(node.params.size());
          bool changed = false;
          for (const auto& [is_move, param_type] : node.params) {
            const auto applied = ApplySubstitutionImpl(param_type, subst, active);
            changed = changed || (applied != param_type);
            params.emplace_back(is_move, applied);
          }
          const auto ret = ApplySubstitutionImpl(node.ret, subst, active);
          changed = changed || (ret != node.ret);
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              const auto applied = ApplySubstitutionImpl(dep.type, subst, active);
              changed = changed || (applied != dep.type);
              deps.push_back(SharedDep{dep.name, applied});
            }
            deps_opt = std::move(deps);
          }
          return changed ? MakeTypeClosure(std::move(params), ret, deps_opt) : type;
        } else {
          return type;
        }
      },
      type->node);
}

}  // namespace

static CheckResult CheckExprImpl(const ScopeContext& ctx,
                      const ast::ExprPtr& expr,
                      const TypeRef& expected,
                      const ExprTypeFn& type_expr,
                      const PlaceTypeFn* type_place,
                      const IdentTypeFn& type_ident,
                      const IfCaseCheckFn* if_case_check);

static ExprTypeResult InferExprImpl(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn* type_place,
                         const IdentTypeFn& type_ident,
                         const IfCaseCheckFn* if_case_check,
                         ConstraintSet* constraints_out) {
  SpecDefsTypeInfer();
  ExprTypeResult result;
  struct ExprTypeRecorder {
    const ScopeContext& ctx;
    const ast::ExprPtr& expr;
    ExprTypeResult& result;
    ~ExprTypeRecorder() {
      if (result.ok && ctx.expr_types && expr) {
        (*ctx.expr_types)[expr.get()] = result.type;
      }
    }
  } recorder{ctx, expr, result};
  if (!expr) {
    return result;
  }

  if (std::holds_alternative<ast::PtrNullExpr>(expr->node)) {
    SPEC_RULE("Syn-PtrNull-Err");
    result.diag_id = "PtrNull-Infer-Err";
    return result;
  }


  if (const auto* literal = std::get_if<ast::LiteralExpr>(&expr->node)) {
    if (literal->literal.kind == lexer::TokenKind::NullLiteral) {
      SPEC_RULE("Syn-Null-Literal-Err");
      result.diag_id = "NullLiteral-Infer-Err";
      return result;
    }
    const auto typed = TypeLiteralExpr(ctx, *literal);
    if (!typed.ok) {
      result.diag_id = typed.diag_id;
      return result;
    }
    SPEC_RULE("Syn-Literal");
    result.ok = true;
    result.type = typed.type;
    return result;
  }

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    const auto ident_type = type_ident(ident->name);
    if (!ident_type.ok) {
      result.diag_id = ident_type.diag_id;
      result.diag_detail = ident_type.diag_detail;
      return result;
    }
    SPEC_RULE("Syn-Ident");
    result.ok = true;
    result.type = ident_type.type;
    return result;
  }

  if (const auto* tuple = std::get_if<ast::TupleExpr>(&expr->node)) {
    if (tuple->elements.empty()) {
      SPEC_RULE("Syn-Unit");
      result.ok = true;
      result.type = MakeTypePrim("()");
      return result;
    }
    std::vector<TypeRef> elements;
    elements.reserve(tuple->elements.size());
    for (const auto& elem : tuple->elements) {
      const auto elem_type =
          InferExprImpl(ctx, elem, type_expr, type_place, type_ident,
                        if_case_check, constraints_out);
      if (!elem_type.ok) {
        result.diag_id = elem_type.diag_id;
        return result;
      }
      elements.push_back(elem_type.type);
    }
    SPEC_RULE("Syn-Tuple");
    result.ok = true;
    result.type = MakeTypeTuple(std::move(elements));
    return result;
  }

  if (std::holds_alternative<ast::CallExpr>(expr->node) ||
      std::holds_alternative<ast::CallTypeArgsExpr>(expr->node)) {
    // Delegate call typing to expression typing so generic inference
    // and call elaboration rules are applied uniformly in synthesis mode.
    const auto typed_call = type_expr(expr);
    if (!typed_call.ok) {
      SPEC_RULE("Syn-Call-Err");
      result.diag_id = typed_call.diag_id;
      result.diag_detail = typed_call.diag_detail;
      return result;
    }
    SPEC_RULE("Syn-Call");
    result.ok = true;
    result.type = typed_call.type;
    return result;
  }

  const auto fallback = type_expr(expr);
  if (!fallback.ok) {
    result.diag_id = fallback.diag_id;
    result.diag_detail = fallback.diag_detail;
    return result;
  }
  SPEC_RULE("Syn-Expr");
  result.ok = true;
  result.type = fallback.type;
  return result;
}

static CheckResult CheckExprImpl(const ScopeContext& ctx,
                      const ast::ExprPtr& expr,
                      const TypeRef& expected,
                      const ExprTypeFn& type_expr,
                      const PlaceTypeFn* type_place,
                      const IdentTypeFn& type_ident,
                      const IfCaseCheckFn* if_case_check) {
  SpecDefsTypeInfer();
  CheckResult result;
  struct CheckExprRecorder {
    const ScopeContext& ctx;
    const ast::ExprPtr& expr;
    const TypeRef& expected;
    CheckResult& result;
    ~CheckExprRecorder() {
      if (result.ok && ctx.expr_types && expr && expected) {
        (*ctx.expr_types)[expr.get()] = expected;
      }
    }
  } recorder{ctx, expr, expected, result};
  if (!expr || !expected) {
    return result;
  }

  if (if_case_check) {
    if (const auto* if_case = std::get_if<ast::IfCaseExpr>(&expr->node)) {
      result = (*if_case_check)(*if_case, expected);
      return result;
    }
  }

  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    const auto attr_validation =
        ValidateAttributes(attributed->attrs, AttributeTarget::Expression);
    if (!attr_validation.ok) {
      result.diag_id = attr_validation.diag_id;
      return result;
    }
    if (HasMemoryOrderAttribute(attributed->attrs)) {
      const auto observed_attr = type_expr(expr);
      if (!observed_attr.ok) {
        result.diag_id = observed_attr.diag_id;
        return result;
      }
    }

    const auto observed = type_expr(expr);
    if (observed.ok) {
      const auto sub = Subtyping(ctx, observed.type, expected);
      if (!sub.ok) {
        result.diag_id = sub.diag_id;
        return result;
      }
      if (sub.subtype) {
        result.ok = true;
        return result;
      }
    }

    const auto checked_inner =
        CheckExprImpl(ctx, attributed->expr, expected, type_expr, type_place,
                      type_ident, if_case_check);
    if (!checked_inner.ok) {
      result.diag_id = checked_inner.diag_id;
      return result;
    }

    result.ok = true;
    return result;
  }

  if (const auto* closure = std::get_if<ast::ClosureExpr>(&expr->node)) {
    TypeRef expected_for_closure = expected;
    const auto normalized_expected = NormalizeAliasType(ctx, expected);
    if (!normalized_expected.ok) {
      result.diag_id = normalized_expected.diag_id;
      return result;
    }
    if (normalized_expected.type) {
      expected_for_closure = normalized_expected.type;
    }
    const auto expected_strip = StripPerm(expected_for_closure);
    const auto* expected_func =
        expected_strip ? std::get_if<TypeFunc>(&expected_strip->node) : nullptr;
    const auto* expected_closure = expected_strip
                                       ? std::get_if<TypeClosure>(&expected_strip->node)
                                       : nullptr;

    if (expected_func || expected_closure) {
      const auto expected_arity =
          expected_func ? expected_func->params.size() : expected_closure->params.size();
      if (closure->params.size() != expected_arity) {
        result.diag_id = "Infer-Closure-Params-Err";
        return result;
      }

      SPEC_RULE("Infer-Closure-Params");
      result.ok = true;
      return result;
    }
  }

  if (std::holds_alternative<ast::PtrNullExpr>(expr->node)) {
    if (PtrNullExpected(expected)) {
      SPEC_RULE("Chk-Null-Ptr");
      result.ok = true;
      return result;
    }
    SPEC_RULE("Chk-PtrNull-Err");
    result.diag_id = "PtrNull-Infer-Err";
    return result;
  }

  if (const auto* literal = std::get_if<ast::LiteralExpr>(&expr->node)) {
    const auto check = CheckLiteralExpr(ctx, *literal, expected);
    if (check.ok) {
      result.ok = true;
      return result;
    }
    if (check.diag_id.has_value()) {
      result.diag_id = check.diag_id;
      return result;
    }
  }

  if (const auto* array_expr = std::get_if<ast::ArrayExpr>(&expr->node)) {
    const auto expected_strip = StripPerm(expected);
    const auto* expected_array = expected_strip
                                     ? std::get_if<TypeArray>(&expected_strip->node)
                                     : nullptr;
    if (expected_array) {
      std::uint64_t total_length = 0;
      for (const auto& segment : array_expr->elements) {
        if (const auto* elem = std::get_if<ast::ArrayElemSegment>(&segment)) {
          const auto elem_check =
              CheckExprImpl(ctx, elem->value, expected_array->element, type_expr,
                            type_place, type_ident, if_case_check);
          if (!elem_check.ok) {
            result.diag_id = elem_check.diag_id;
            return result;
          }
          total_length += 1;
          continue;
        }

        const auto* repeat = std::get_if<ast::ArrayRepeatSegment>(&segment);
        if (!repeat || !repeat->value || !repeat->count) {
          return result;
        }
        const auto count_type = type_expr(repeat->count);
        if (!count_type.ok) {
          result.diag_id = count_type.diag_id;
          return result;
        }
        const auto count_prim = expr::GetPrimName(count_type.type);
        if (!count_prim.has_value() ||
            (!expr::IsIntType(*count_prim) && *count_prim != "usize")) {
          result.diag_id = "E-TYP-1812";
          return result;
        }
        const auto repeat_len = ConstLen(ctx, repeat->count);
        if (!repeat_len.ok || !repeat_len.value.has_value()) {
          result.diag_id = repeat_len.diag_id.value_or("E-TYP-1812");
          return result;
        }
        const auto value_check =
            CheckExprImpl(ctx, repeat->value, expected_array->element,
                          type_expr, type_place, type_ident, if_case_check);
        if (!value_check.ok) {
          result.diag_id = value_check.diag_id;
          return result;
        }
        if (!BitcopyType(ctx, expected_array->element)) {
          result.diag_id = "E-UNS-0107";
          return result;
        }
        total_length += *repeat_len.value;
      }
      if (total_length != expected_array->length) {
        return result;
      }
      SPEC_RULE("T-Array-Literal-Segments");
      result.ok = true;
      return result;
    }

    const auto* expected_slice = expected_strip
                                     ? std::get_if<TypeSlice>(&expected_strip->node)
                                     : nullptr;
    if (expected_slice) {
      for (const auto& segment : array_expr->elements) {
        if (const auto* elem = std::get_if<ast::ArrayElemSegment>(&segment)) {
          const auto elem_check =
              CheckExprImpl(ctx, elem->value, expected_slice->element, type_expr,
                            type_place, type_ident, if_case_check);
          if (!elem_check.ok) {
            result.diag_id = elem_check.diag_id;
            return result;
          }
          continue;
        }

        const auto* repeat = std::get_if<ast::ArrayRepeatSegment>(&segment);
        if (!repeat || !repeat->value || !repeat->count) {
          return result;
        }
        const auto count_type = type_expr(repeat->count);
        if (!count_type.ok) {
          result.diag_id = count_type.diag_id;
          return result;
        }
        const auto count_prim = expr::GetPrimName(count_type.type);
        if (!count_prim.has_value() ||
            (!expr::IsIntType(*count_prim) && *count_prim != "usize")) {
          result.diag_id = "E-TYP-1812";
          return result;
        }
        const auto repeat_len = ConstLen(ctx, repeat->count);
        if (!repeat_len.ok || !repeat_len.value.has_value()) {
          result.diag_id = repeat_len.diag_id.value_or("E-TYP-1812");
          return result;
        }
        (void)repeat_len;
        const auto value_check =
            CheckExprImpl(ctx, repeat->value, expected_slice->element,
                          type_expr, type_place, type_ident, if_case_check);
        if (!value_check.ok) {
          result.diag_id = value_check.diag_id;
          return result;
        }
        if (!BitcopyType(ctx, expected_slice->element)) {
          result.diag_id = "E-UNS-0107";
          return result;
        }
      }
      SPEC_RULE("T-Array-Literal-Segments");
      result.ok = true;
      return result;
    }
  }

  if (const auto* array_repeat = std::get_if<ast::ArrayRepeatExpr>(&expr->node)) {
    const auto expected_strip = StripPerm(expected);
    const auto count_type = type_expr(array_repeat->count);
    if (!count_type.ok) {
      result.diag_id = count_type.diag_id;
      return result;
    }
    const auto count_prim = expr::GetPrimName(count_type.type);
    if (!count_prim.has_value() ||
        (!expr::IsIntType(*count_prim) && *count_prim != "usize")) {
      result.diag_id = "E-TYP-1812";
      return result;
    }
    const auto repeat_len = ConstLen(ctx, array_repeat->count);
    if (!repeat_len.ok || !repeat_len.value.has_value()) {
      result.diag_id = repeat_len.diag_id.value_or("E-TYP-1812");
      return result;
    }

    const auto* expected_array = expected_strip
                                     ? std::get_if<TypeArray>(&expected_strip->node)
                                     : nullptr;
    if (expected_array) {
      if (*repeat_len.value != expected_array->length) {
        return result;
      }
      const auto value_check =
          CheckExprImpl(ctx, array_repeat->value, expected_array->element,
                        type_expr, type_place, type_ident, if_case_check);
      if (!value_check.ok) {
        result.diag_id = value_check.diag_id;
        return result;
      }
      if (!BitcopyType(ctx, expected_array->element)) {
        result.diag_id = "E-UNS-0107";
        return result;
      }
      SPEC_RULE("T-Array-Literal-Segments");
      result.ok = true;
      return result;
    }

    const auto* expected_slice = expected_strip
                                     ? std::get_if<TypeSlice>(&expected_strip->node)
                                     : nullptr;
    if (expected_slice) {
      const auto value_check =
          CheckExprImpl(ctx, array_repeat->value, expected_slice->element,
                        type_expr, type_place, type_ident, if_case_check);
      if (!value_check.ok) {
        result.diag_id = value_check.diag_id;
        return result;
      }
      if (!BitcopyType(ctx, expected_slice->element)) {
        result.diag_id = "E-UNS-0107";
        return result;
      }
      SPEC_RULE("T-Array-Literal-Segments");
      result.ok = true;
      return result;
    }
  }

  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    if (IdEq(unary->op, "-")) {
      const auto expected_strip = StripPerm(expected);
      const auto* prim = expected_strip
                             ? std::get_if<TypePrim>(&expected_strip->node)
                             : nullptr;
      if (prim && IsSignedIntOrFloatType(prim->name)) {
        const auto inner = CheckExprImpl(ctx, unary->value, expected, type_expr,
                                         type_place, type_ident, if_case_check);
        if (inner.ok) {
          SPEC_RULE("T-Neg");
          result.ok = true;
          return result;
        }
        if (inner.diag_id.has_value()) {
          result.diag_id = inner.diag_id;
          return result;
        }
      }
    }
  }

  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    if (IdEq(method->name, "alloc_raw")) {
      const auto expected_strip = StripPerm(expected);
      const auto* expected_raw = expected_strip
                                    ? std::get_if<TypeRawPtr>(&expected_strip->node)
                                    : nullptr;
      if (expected_raw && expected_raw->qual == RawPtrQual::Mut) {
        auto recv_type = type_expr(method->receiver);
        if (!recv_type.ok && recv_type.diag_id.has_value() &&
            *recv_type.diag_id == "ValueUse-NonBitcopyPlace") {
          auto move_expr = std::make_shared<ast::Expr>();
          move_expr->span = method->receiver ? method->receiver->span : core::Span{};
          move_expr->node = ast::MoveExpr{method->receiver};
          recv_type = type_expr(move_expr);
        }
        if (!recv_type.ok) {
          result.diag_id = recv_type.diag_id;
          return result;
        }
        const auto recv_strip = StripPerm(recv_type.type);
        const auto* recv_dyn = recv_strip
                                   ? std::get_if<TypeDynamic>(&recv_strip->node)
                                   : nullptr;
        if (recv_dyn && IsHeapAllocatorClassPath(recv_dyn->path)) {
          if (!IsInUnsafeSpan(ctx, expr->span)) {
            SPEC_RULE("AllocRaw-Unsafe-Err");
            result.diag_id = "E-MEM-3030";
            return result;
          }
          const auto required =
              MakeTypePerm(Permission::Const, MakeTypeDynamic({"HeapAllocator"}));
          const auto recv_sub = Subtyping(ctx, recv_type.type, required);
          if (!recv_sub.ok) {
            result.diag_id = recv_sub.diag_id;
            return result;
          }
          if (!recv_sub.subtype) {
            SPEC_RULE("MethodCall-RecvPerm-Err");
            result.diag_id = "E-TYP-1605";
            return result;
          }
          const auto recv_arg =
              RecvArgOk(method->receiver, std::nullopt, type_expr);
          if (!recv_arg.ok) {
            result.diag_id = recv_arg.diag_id;
            return result;
          }
          if (method->args.size() != 1) {
            SPEC_RULE("Call-ArgCount-Err");
            result.diag_id = "E-SEM-2532";
            return result;
          }
          const auto& arg = method->args[0];
          if (arg.moved) {
            SPEC_RULE("Call-Move-Unexpected");
            result.diag_id = "E-SEM-2535";
            return result;
          }
          if (HasSourceProvenance(arg.value) && !IsPlaceExprForCall(arg.value)) {
            SPEC_RULE("Call-Arg-NotPlace");
            result.diag_id = "E-TYP-1603";
            return result;
          }
          const auto arg_type = type_expr(arg.value);
          if (!arg_type.ok) {
            result.diag_id = arg_type.diag_id;
            return result;
          }
          const auto count_sub =
              Subtyping(ctx, arg_type.type, MakeTypePrim("usize"));
          if (!count_sub.ok) {
            result.diag_id = count_sub.diag_id;
            return result;
          }
          if (!count_sub.subtype) {
            SPEC_RULE("Call-ArgType-Err");
            result.diag_id = "E-SEM-2533";
            return result;
          }
          SPEC_RULE("Chk-Subsumption");
          result.ok = true;
          return result;
        }
      }
    }
  }

  std::optional<ExprTypeResult> inferred_call_with_expected;
  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    if (call->generic_args.empty()) {
      const auto inferred_subst =
          ::ultraviolet::analysis::expr::InferGenericCallSubst(
              ctx, call->callee, call->args, expected, type_expr, type_place);
      if (inferred_subst.ok) {
        const auto typed_call = TypeCallWithSubst(
            ctx, call->callee, call->args, inferred_subst.subst, type_expr,
            type_place, nullptr);
        if (!typed_call.ok) {
          result.diag_id = typed_call.diag_id;
          return result;
        }
        ::ultraviolet::analysis::expr::RecordGenericCallSubst(
            ctx, *call, inferred_subst.subst);
        ExprTypeResult call_result;
        call_result.ok = true;
        call_result.type = typed_call.type;
        call_result.diag_detail = typed_call.diag_detail;
        inferred_call_with_expected = std::move(call_result);
      }
    }
  }

  const auto inferred = inferred_call_with_expected.has_value()
                            ? *inferred_call_with_expected
                            : InferExprImpl(ctx, expr, type_expr, type_place,
                                            type_ident, if_case_check, nullptr);
  if (!inferred.ok) {
    result.diag_id = inferred.diag_id;
    result.diag_detail = inferred.diag_detail;
    result.diag_span = inferred.diag_span;
    return result;
  }
  if (IsNonBitcopyPlaceValueUse(ctx, expr, inferred.type)) {
    SPEC_RULE("ValueUse-NonBitcopyPlace");
    result.diag_id = "ValueUse-NonBitcopyPlace";
    return result;
  }

  TypeRef expected_for_union = expected;
  if (const auto normalized_expected = NormalizeAliasType(ctx, expected);
      normalized_expected.ok && normalized_expected.type) {
    expected_for_union = normalized_expected.type;
  }
  if (const auto* expected_union =
          std::get_if<TypeUnion>(&StripPerm(expected_for_union)->node)) {
    for (const auto& member : expected_union->members) {
      const auto member_sub = Subtyping(ctx, inferred.type, member);
      if (!member_sub.ok) {
        result.diag_id = member_sub.diag_id;
        return result;
      }
      if (member_sub.subtype) {
        SPEC_RULE("Chk-Subsumption");
        result.ok = true;
        return result;
      }
    }
  }

  if (ModalNonNiche(ctx, inferred.type, expected)) {
    SPEC_RULE("Chk-Subsumption-Modal-NonNiche");
    result.diag_id = "Chk-Subsumption-Modal-NonNiche";
    return result;
  }

  auto try_coerce = [&](const TypeRef& source) -> bool {
    const auto coerced = CoerceArrayToSlice(ctx, source);
    if (!coerced.ok) {
      return false;
    }
    const auto sub = Subtyping(ctx, coerced.type, expected);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      return true;
    }
    if (sub.subtype) {
      result.ok = true;
      return true;
    }
    if (sub.diag_id.has_value()) {
      result.diag_id = sub.diag_id;
      return true;
    }
    return false;
  };

  if (try_coerce(inferred.type)) {
    return result;
  }
  if (std::holds_alternative<TypeArray>(inferred.type->node)) {
    const auto perm_wrapped =
        MakeTypePerm(Permission::Const, inferred.type);
    if (try_coerce(perm_wrapped)) {
      return result;
    }
  }

  if (const auto* expected_perm = std::get_if<TypePerm>(&expected->node)) {
    const bool is_aggregate_literal =
        std::holds_alternative<ast::RecordExpr>(expr->node) ||
        std::holds_alternative<ast::EnumLiteralExpr>(expr->node) ||
        std::holds_alternative<ast::TupleExpr>(expr->node) ||
        std::holds_alternative<ast::ArrayExpr>(expr->node) ||
        std::holds_alternative<ast::ArrayRepeatExpr>(expr->node);
    const bool is_async_create =
        (std::holds_alternative<ast::CallExpr>(expr->node) ||
         std::holds_alternative<ast::CallTypeArgsExpr>(expr->node) ||
         std::holds_alternative<ast::MethodCallExpr>(expr->node) ||
         std::holds_alternative<ast::RaceExpr>(expr->node)) &&
        AsyncSigOf(ctx, inferred.type).has_value() &&
        AsyncSigOf(ctx, expected_perm->base).has_value();
    TypeRef expected_base = expected_perm->base;
    if (is_async_create) {
      if (const auto* expected_modal =
              std::get_if<TypeModalState>(&StripPerm(expected_perm->base)->node)) {
        expected_base = ModalRefType(expected_modal->modal_ref);
      }
    }
    if (is_aggregate_literal || is_async_create) {
      const auto base_sub = Subtyping(ctx, inferred.type, expected_base);
      if (!base_sub.ok) {
        result.diag_id = base_sub.diag_id;
        return result;
      }
      if (base_sub.subtype) {
        SPEC_RULE("Chk-Subsumption");
        result.ok = true;
        return result;
      }
      if (base_sub.diag_id.has_value()) {
        result.diag_id = base_sub.diag_id;
        return result;
      }
    }
  }

  const auto equiv = TypeEquiv(inferred.type, expected);
  if (!equiv.ok) {
    result.diag_id = equiv.diag_id;
    return result;
  }
  if (equiv.equiv) {
    SPEC_RULE("Chk-Subsumption");
    result.ok = true;
    return result;
  }

  const auto sub = Subtyping(ctx, inferred.type, expected);
  if (!sub.ok) {
    result.diag_id = sub.diag_id;
    return result;
  }
  if (!sub.subtype) {
    if (expr && std::holds_alternative<ast::MoveExpr>(expr->node) &&
        PermOfType(inferred.type) == Permission::Unique) {
      const auto moved_value_sub = Subtyping(ctx, StripPerm(inferred.type), expected);
      if (!moved_value_sub.ok) {
        result.diag_id = moved_value_sub.diag_id;
        return result;
      }
      if (moved_value_sub.subtype) {
        SPEC_RULE("Chk-Subsumption");
        result.ok = true;
        return result;
      }
    }
    if (sub.diag_id.has_value()) {
      result.diag_id = sub.diag_id;
      return result;
    }
    if (IsGpuPtrAddrSpaceMismatch(inferred.type, expected)) {
      SPEC_RULE("GpuPtr-AddrSpace-Err");
      result.diag_id = "E-TYP-2641";
      return result;
    }
    TypeRef expected_norm = expected;
    const auto norm = NormalizeAliasType(ctx, expected);
    if (!norm.ok) {
      result.diag_id = norm.diag_id;
      return result;
    }
    expected_norm = norm.type;
    if (const auto* refine = std::get_if<TypeRefine>(&expected_norm->node)) {
      if (inferred.type &&
          std::holds_alternative<TypeRefine>(inferred.type->node)) {
        const auto refine_sub = Subtyping(ctx, inferred.type, expected_norm);
        if (!refine_sub.ok) {
          result.diag_id = refine_sub.diag_id;
          return result;
        }
        if (refine_sub.subtype) {
          SPEC_RULE("Chk-Subsumption");
          result.ok = true;
          return result;
        }
      }
      const auto base_check =
          CheckExprImpl(ctx, expr, refine->base, type_expr, type_place,
                        type_ident, if_case_check);
      if (!base_check.ok) {
        result.diag_id = base_check.diag_id;
        return result;
      }
      if (!ProveRefinePredicate(expr, *refine, result.diag_id)) {
        return result;
      }
      SPEC_RULE("T-Refine-Intro");
      result.ok = true;
      return result;
    }
    SPEC_RULE("Chk-Subsumption-Err");
    result.diag_id = "E-SEM-2526";
    result.diag_detail = "expected " + TypeToString(expected) +
                         ", found " + TypeToString(inferred.type);
    return result;
  }

  SPEC_RULE("Chk-Subsumption");
  result.ok = true;
  return result;
}

TypeRef ApplySubstitution(const TypeRef& type, const TypeSubstitution& subst) {
  std::unordered_set<std::uint32_t> active;
  return ApplySubstitutionImpl(type, subst, active);
}

SolveResult Solve(const ScopeContext& ctx, const ConstraintSet& constraints) {
  SpecDefsTypeInfer();
  SPEC_RULE("Solve");

  SolveResult result;
  TypeSubstitution subst;

  for (const auto& constraint : constraints) {
    const auto lhs = ApplySubstitution(constraint.lhs, subst);
    const auto rhs = ApplySubstitution(constraint.rhs, subst);
    if (!lhs || !rhs) {
      result.diag_id = "Syn-Call-Err";
      return result;
    }

    if (constraint.requires_subtyping) {
      if (const auto* lhs_var = std::get_if<TypeVar>(&lhs->node)) {
        if (!BindTypeVar(lhs_var->id, rhs, subst)) {
          result.diag_id = "Syn-Call-Err";
          return result;
        }
        continue;
      }
      if (const auto* rhs_var = std::get_if<TypeVar>(&rhs->node)) {
        if (!BindTypeVar(rhs_var->id, lhs, subst)) {
          result.diag_id = "Syn-Call-Err";
          return result;
        }
        continue;
      }

      const auto sub = Subtyping(ctx, lhs, rhs);
      if (!sub.ok) {
        result.diag_id = sub.diag_id;
        return result;
      }
      if (!sub.subtype) {
        result.diag_id =
            sub.diag_id.has_value() ? sub.diag_id : std::optional<std::string_view>{"Syn-Call-Err"};
        return result;
      }
      continue;
    }

    std::optional<std::string_view> diag_id;
    if (!UnifyEq(ctx, lhs, rhs, subst, diag_id)) {
      result.diag_id =
          diag_id.has_value() ? diag_id : std::optional<std::string_view>{"Syn-Call-Err"};
      return result;
    }
  }

  result.ok = true;
  result.subst = std::move(subst);
  return result;
}

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident) {
  return InferExpr(ctx, expr, type_expr, type_place, type_ident,
                   static_cast<ConstraintSet*>(nullptr));
}

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident,
                         ConstraintSet* constraints_out) {
  if (constraints_out) {
    constraints_out->clear();
  }
  return InferExprImpl(ctx, expr, type_expr, &type_place, type_ident, nullptr,
                       constraints_out);
}

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident,
                         const IfCaseCheckFn& if_case_check) {
  return InferExpr(ctx, expr, type_expr, type_place, type_ident, if_case_check,
                   static_cast<ConstraintSet*>(nullptr));
}

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident,
                         const IfCaseCheckFn& if_case_check,
                         ConstraintSet* constraints_out) {
  if (constraints_out) {
    constraints_out->clear();
  }
  return InferExprImpl(ctx, expr, type_expr, &type_place, type_ident,
                       &if_case_check, constraints_out);
}

CheckResult CheckExpr(const ScopeContext& ctx,
                      const ast::ExprPtr& expr,
                      const TypeRef& expected,
                      const ExprTypeFn& type_expr,
                      const PlaceTypeFn& type_place,
                      const IdentTypeFn& type_ident) {
  return CheckExprImpl(ctx, expr, expected, type_expr, &type_place, type_ident,
                       nullptr);
}

CheckResult CheckExpr(const ScopeContext& ctx,
                      const ast::ExprPtr& expr,
                      const TypeRef& expected,
                      const ExprTypeFn& type_expr,
                      const PlaceTypeFn& type_place,
                      const IdentTypeFn& type_ident,
                      const IfCaseCheckFn& if_case_check) {
  return CheckExprImpl(ctx, expr, expected, type_expr, &type_place, type_ident,
                       &if_case_check);
}

}  // namespace ultraviolet::analysis
