// =================================================================
// File: 04_analysis/typing/expr/addr_of.cpp
// Construct: Address-Of Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-AddrOf, AddrOf-NonPlace, AddrOf-Index-Array-NonUsize,
//             AddrOf-Index-Slice-NonUsize
// =================================================================

#include "04_analysis/typing/expr/addr_of.h"

#include "00_core/assert_spec.h"
#include "02_source/lexer/token.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/typecheck.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsAddrOf() {
  SPEC_DEF("T-AddrOf", "5.2.12");
  SPEC_DEF("AddrOf-NonPlace", "5.2.12");
  SPEC_DEF("AddrOf-Index-Array-NonUsize", "5.2.12");
  SPEC_DEF("AddrOf-Index-Slice-NonUsize", "5.2.12");
  SPEC_DEF("AddrOf-Packed-Unsafe-Err", "5.2.12");
}

static bool HasLayoutPacked(const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name != "layout") {
      continue;
    }
    for (const auto& arg : attr.args) {
      if (const auto* tok = std::get_if<lexer::Token>(&arg.value)) {
        if (tok->lexeme == "packed") {
          return true;
        }
      }
    }
  }
  return false;
}

static bool IsPackedRecord(const ScopeContext& ctx, const TypePath& path) {
  PathKey key;
  for (const auto& seg : path) {
    key.push_back(seg);
  }
  const auto it = ctx.sigma.types.find(key);
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  const auto* record = std::get_if<ast::RecordDecl>(&it->second);
  if (!record) {
    return false;
  }
  return HasLayoutPacked(record->attrs);
}

}  // namespace

// (T-AddrOf)
ExprTypeResult TypeAddressOfExprImpl(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::AddressOfExpr& expr,
                                     const TypeEnv& env) {
  SpecDefsAddrOf();
  ExprTypeResult result;

  // Must be a place expression
  if (!IsPlaceExpr(expr.place)) {
    SPEC_RULE("AddrOf-NonPlace");
    result.diag_id = "AddrOf-NonPlace";
    return result;
  }

  if (expr.place) {
    if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr.place->node)) {
      const auto base_type = TypeExpr(ctx, type_ctx, field->base, env);
      if (base_type.ok) {
        const auto stripped = StripPerm(base_type.type);
        if (const auto* path = stripped ? std::get_if<TypePathType>(&stripped->node) : nullptr) {
          if (IsPackedRecord(ctx, path->path) && !IsInUnsafeSpan(ctx, expr.place->span)) {
            SPEC_RULE("AddrOf-Packed-Unsafe-Err");
            result.diag_id = "E-TYP-2105";
            return result;
          }
        }
      }
    }
  }

  if (expr.place) {
    if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr.place->node)) {
      const auto idx_type = TypeExpr(ctx, type_ctx, index->index, env);
      if (!idx_type.ok) {
        result.diag_id = idx_type.diag_id;
        return result;
      }
      const auto idx_stripped = StripPerm(idx_type.type);
      if (!IsPrimType(idx_stripped, "usize")) {
        const auto base_type = TypePlace(ctx, type_ctx, index->base, env);
        if (!base_type.ok) {
          result.diag_id = base_type.diag_id;
          return result;
        }
        const auto stripped = StripPerm(base_type.type);
        if (stripped && std::holds_alternative<TypeArray>(stripped->node)) {
          SPEC_RULE("AddrOf-Index-Array-NonUsize");
          result.diag_id = "Index-Array-NonUsize";
          return result;
        }
        if (stripped && std::holds_alternative<TypeSlice>(stripped->node)) {
          SPEC_RULE("AddrOf-Index-Slice-NonUsize");
          result.diag_id = "Index-Slice-NonUsize";
          return result;
        }
        result.diag_id = "Index-NonIndexable";
        return result;
      }

      if (!index->index ||
          !std::holds_alternative<ast::RangeExpr>(index->index->node)) {
        const auto base_place = TypePlace(ctx, type_ctx, index->base, env);
        if (!base_place.ok) {
          result.diag_id = base_place.diag_id;
          result.diag_detail = base_place.diag_detail;
          result.diag_span = base_place.diag_span;
          return result;
        }
        const auto stripped = StripPerm(base_place.type);
        if (const auto* array = stripped ? std::get_if<TypeArray>(&stripped->node)
                                         : nullptr) {
          const auto index_const = ConstLen(ctx, index->index);
          const bool has_const_index = index_const.ok && index_const.value.has_value();
          if (!has_const_index) {
            if (!type_ctx.contract_dynamic) {
              result.diag_id = "Index-Array-NonConst-Err";
              result.diag_detail =
                  "fixed-size array index expression is not compile-time constant; "
                  "runtime fixed-array indexing requires [[dynamic]], or use a slice "
                  "for runtime indexing";
              result.diag_span = index->index
                                     ? std::optional<core::Span>(index->index->span)
                                     : std::nullopt;
              return result;
            }
          } else if (*index_const.value >= array->length) {
            result.diag_id = "Index-Array-OOB-Err";
            result.diag_span = index->index
                                   ? std::optional<core::Span>(index->index->span)
                                   : std::nullopt;
            return result;
          }
          TypeRef out_type = array->element;
          if (const auto* perm = std::get_if<TypePerm>(&base_place.type->node)) {
            out_type = MakeTypePerm(perm->perm, out_type);
          }
          SPEC_RULE("T-AddrOf");
          result.ok = true;
          result.type = MakeTypePtr(out_type, PtrState::Valid);
          return result;
        }
      }
    }
  }

  const auto place = TypePlace(ctx, type_ctx, expr.place, env);
  if (!place.ok) {
    result.diag_id = place.diag_id;
    result.diag_detail = place.diag_detail;
    result.diag_span = place.diag_span;
    return result;
  }

  SPEC_RULE("T-AddrOf");
  result.ok = true;
  result.type = MakeTypePtr(place.type, PtrState::Valid);
  return result;
}

}  // namespace cursive::analysis::expr
