// =============================================================================
// MIGRATION MAPPING: expr/field_access.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16104-16107: (Lower-Expr-FieldAccess)
//     Gamma |- LowerExpr(base) => <IR_b, v_b>    FieldValue(v_b, f) = v_f
//     Gamma |- LowerExpr(FieldAccess(base, f)) => <IR_b, v_f>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 379-391: LowerReadPlace for FieldAccessExpr
//   - Lines 647-692: LowerWritePlace for FieldAccessExpr
//   - Lines 990-1008: LowerAddrOf for FieldAccessExpr (handled in addr_of.cpp)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (DerivedValueInfo::Kind::Field)
//   - ultraviolet/include/04_analysis/layout/layout.h (field offset calculation)
//   - ultraviolet/include/05_codegen/cleanup/cleanup.h (EmitDrop, DropOnAssignRoot)
//
// =============================================================================

#include "05_codegen/lower/expr/field_access.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/cleanup/cleanup.h"
#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/typing/type_lookup.h"

#include <algorithm>
#include <cassert>
#include <variant>
#include <vector>

namespace ultraviolet::codegen {

namespace {

// Check if attribute list contains #dynamic
bool HasDynamicAttr(const ast::AttributeList& attrs) {
  return analysis::HasAttribute(attrs, analysis::attrs::kDynamic);
}

// Extract the root identifier of a place expression
std::optional<std::string> PlaceRoot(const ast::Expr& expr) {
  return std::visit(
      [&](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? PlaceRoot(*node.expr) : std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return node.name;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return PlaceRoot(*node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return PlaceRoot(*node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return PlaceRoot(*node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return PlaceRoot(*node.value);
        }
        return std::nullopt;
      },
      expr.node);
}

// Extract the first field access from a place chain
std::optional<std::string> FieldHead(const ast::Expr& expr) {
  return std::visit(
      [&](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? FieldHead(*node.expr) : std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto head = FieldHead(*node.base);
          if (head.has_value()) {
            return head;
          }
          return node.name;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return FieldHead(*node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return FieldHead(*node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return std::nullopt;
        }
        return std::nullopt;
      },
      expr.node);
}

// Update binding state after field assignment (clears field from moved_fields)
void UpdateBindingAfterFieldAssignLocal(const ast::Expr& place, LowerCtx& ctx) {
  auto root = PlaceRoot(place);
  auto head = FieldHead(place);
  if (!root.has_value() || !head.has_value()) {
    return;
  }
  auto it = ctx.binding_states.find(*root);
  if (it == ctx.binding_states.end() || it->second.empty()) {
    return;
  }
  auto& state = it->second.back();
  if (state.is_moved) {
    return;
  }
  auto& fields = state.moved_fields;
  fields.erase(std::remove(fields.begin(), fields.end(), *head), fields.end());
}

analysis::TypeRef StripFieldBaseType(analysis::TypeRef type) {
  while (type) {
    if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
      type = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<analysis::TypeRefine>(&type->node)) {
      type = refine->base;
      continue;
    }
    break;
  }
  return type;
}

analysis::TypeRef InferRecordFieldTypeFromBase(
    const IRValue& base_value,
    std::string_view field_name,
    LowerCtx& ctx) {
  analysis::TypeRef base_type = StripFieldBaseType(ctx.LookupValueType(base_value));
  if (!base_type) {
    return nullptr;
  }
  const auto* path = analysis::AppliedTypePath(*base_type);
  if (!path) {
    return nullptr;
  }
  const auto* args = analysis::AppliedTypeArgs(*base_type);
  const auto& scope = ScopeForLowering(ctx);
  const ast::RecordDecl* record = analysis::LookupRecordDecl(scope, *path);
  if (!record) {
    return nullptr;
  }
  const auto field_type = analysis::FieldType(
      *record,
      field_name,
      scope,
      args ? *args : std::vector<analysis::TypeRef>{});
  return field_type.has_value() ? *field_type : nullptr;
}

}  // namespace

// ============================================================================
// LowerReadPlaceFieldAccess - Lower field access expression for reading
// ============================================================================
//
// Implements spec rule: Lower-ReadPlace-Field (Lower-Expr-FieldAccess)
//
// Gamma |- LowerExpr(base) => <IR_b, v_b>    FieldValue(v_b, f) = v_f
// ------------------------------------------------------------------
// Gamma |- LowerExpr(FieldAccess(base, f)) => <IR_b, v_f>
//
// The field value is represented as a DerivedValueInfo with Kind::Field,
// which will be materialized to an actual load during LLVM emission.
// ============================================================================

LowerResult LowerReadPlaceFieldAccess(const ast::FieldAccessExpr& node,
                                       const ast::Expr& place,
                                       LowerCtx& ctx) {
  SPEC_RULE("Lower-ReadPlace-Field");

  // Field access in expressions is defined over arbitrary base expressions
  // (e.g. @result.value in postconditions), not just l-value places.
  auto base_result = LowerExpr(*node.base, ctx);

  // Create a fresh temporary for the field value
  IRValue field_value = ctx.FreshTempValue("place_field");

  // Register the type of the field value if type information is available.
  analysis::TypeRef field_type = ctx.expr_type ? ctx.expr_type(place) : nullptr;
  if (!field_type) {
    field_type = InferRecordFieldTypeFromBase(base_result.value, node.name, ctx);
  }
  if (field_type) {
    ctx.RegisterValueType(field_value, field_type);
  }

  // Register derived value info for field access
  // This allows LLVM emission to compute the actual field offset and load
  DerivedValueInfo info;
  info.kind = DerivedValueInfo::Kind::Field;
  info.base = base_result.value;
  info.field = node.name;
  ctx.RegisterDerivedValue(field_value, info);

  IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
  return LowerResult{SeqIR({base_result.ir, key_ir}), field_value};
}

// ============================================================================
// LowerWritePlaceFieldAccess - Lower field access expression for writing
// ============================================================================
//
// Implements spec rules: Lower-WritePlace-Field, LowerWriteSub-Field
//
// For writing to a field:
// 1. Get address of the base
// 2. Compute field offset
// 3. If allow_drop and root is responsible, drop the old field value
// 4. Write the new value to the field
// 5. Update binding state to clear moved_fields for this field
// ============================================================================

IRPtr LowerWritePlaceFieldAccess(const ast::FieldAccessExpr& node,
                                  const ast::Expr& place,
                                  const IRValue& value,
                                  bool allow_drop,
                                  LowerCtx& ctx) {
  SPEC_RULE(allow_drop ? "Lower-WritePlace-Field" : "LowerWriteSub-Field");

  // Get address of the base
  auto base_addr =
      LowerAddrOf(*node.base, ctx, AddressUseKind::TransientNoEscape);

  // Create a pointer to the field
  IRValue ptr_value = ctx.FreshTempValue("addr_of_field");

  // Register pointer type
  if (ctx.expr_type) {
    analysis::TypeRef field_type = ctx.expr_type(place);
    if (field_type) {
      auto ptr_type = analysis::MakeTypePtr(field_type, analysis::PtrState::Valid);
      ctx.RegisterValueType(ptr_value, ptr_type);
    }
  }

  // Register derived value info for field address
  DerivedValueInfo info;
  info.kind = DerivedValueInfo::Kind::AddrField;
  info.base = base_addr.value;
  info.field = node.name;
  ctx.RegisterDerivedValue(ptr_value, info);

  // Emit drop IR for old field value if needed
  IRPtr drop_ir = EmptyIR();
  if (allow_drop && DropOnAssignRoot(*node.base, ctx)) {
    analysis::TypeRef field_type;
    if (ctx.expr_type) {
      field_type = ctx.expr_type(place);
    }
    if (field_type) {
      // Read the old field value
      IRValue field_value_old = ctx.FreshTempValue("place_field_old");
      ctx.RegisterValueType(field_value_old, field_type);

      IRReadPtr read;
      read.ptr = ptr_value;
      read.result = field_value_old;

      // Emit drop for the old value
      drop_ir = SeqIR(std::vector<IRPtr>{
          MakeIR(std::move(read)),
          EmitDrop(field_type, field_value_old, ctx)
      });
    }
  }

  // Create IR to mark the address computation
  IRAddrOf addr_marker;
  addr_marker.place = LowerPlace(place, ctx);
  addr_marker.result = ptr_value;

  // Write the new value through the pointer
  IRWritePtr write;
  write.ptr = ptr_value;
  write.value = value;

  // Update binding state after field assignment
  if (allow_drop) {
    UpdateBindingAfterFieldAssignLocal(place, ctx);
  }

  return SeqIR(std::vector<IRPtr>{
      base_addr.ir,
      MakeIR(std::move(addr_marker)),
      drop_ir,
      MakeIR(std::move(write))
  });
}

// ============================================================================
// IsFieldAccessPlace - Check if expression is a valid field access place
// ============================================================================

bool IsFieldAccessPlace(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? IsFieldAccessPlace(node.expr) : false;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return true;
        }
        return false;
      },
      expr->node);
}

// ============================================================================
// BuildFieldAccessPlaceRepr - Build string representation of field access
// ============================================================================

std::string BuildFieldAccessPlaceRepr(const ast::FieldAccessExpr& node,
                                       const ast::Expr& base_expr) {
  std::string base_repr;

  std::visit(
      [&](const auto& base_node) {
        using T = std::decay_t<decltype(base_node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          if (base_node.expr) {
            // Recurse into attributed expression
            std::visit(
                [&](const auto& inner) {
                  using U = std::decay_t<decltype(inner)>;
                  if constexpr (std::is_same_v<U, ast::IdentifierExpr>) {
                    base_repr = inner.name;
                  } else if constexpr (std::is_same_v<U, ast::FieldAccessExpr>) {
                    base_repr = BuildFieldAccessPlaceRepr(inner, *node.base);
                  }
                },
                base_node.expr->node);
          }
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          base_repr = base_node.name;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          base_repr = BuildFieldAccessPlaceRepr(base_node, base_expr);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          // Handle tuple base
          base_repr = ""; // Simplified - full implementation would recurse
        }
      },
      node.base->node);

  if (base_repr.empty()) {
    return node.name;
  }
  return base_repr + "." + node.name;
}

}  // namespace ultraviolet::codegen
