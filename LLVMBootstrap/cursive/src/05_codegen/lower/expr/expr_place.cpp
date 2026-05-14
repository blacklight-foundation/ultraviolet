// =============================================================================
// Expression Lowering Place Operations
// =============================================================================

#include "05_codegen/lower/expr/expr_common.h"

#include <functional>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lookup.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/globals/globals.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/lower/expr/identifier.h"
#include "05_codegen/lower/expr/range.h"
#include "05_codegen/lower/pattern/ir_pattern.h"

namespace cursive::codegen {

namespace {

IRRange ToIRRange(const RangeVal& range) {
  IRRange out;
  out.kind = ToIRRangeKind(range.kind);
  out.lo = range.lo;
  out.hi = range.hi;
  return out;
}

std::string ModulePathString(const std::vector<std::string>& path) {
  std::string out;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i) {
      out += "::";
    }
    out += path[i];
  }
  return out;
}

}  // namespace

LowerResult LowerReadPlace(const ast::Expr& place, LowerCtx& ctx) {
  return std::visit(
      [&](const auto& node) -> LowerResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (ctx.LookupLocalAddrAlias(node.name)) {
            return LowerIdentifier(place, node, ctx);
          }

          if (const BindingState* binding = ctx.GetBindingState(node.name)) {
            SPEC_RULE("Lower-ReadPlace-Ident-Local");
            IRReadVar read;
            read.name = node.name;
            IRValue value;
            value.kind = IRValue::Kind::Local;
            value.name = node.name;
            if (binding->type) {
              ctx.RegisterValueType(value, binding->type);
            } else if (ctx.expr_type) {
              ctx.RegisterValueType(value, ctx.expr_type(place));
            }
            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
            return LowerResult{SeqIR({key_ir, MakeIR(std::move(read))}), value};
          }
          if (const auto* capture = ctx.LookupCapture(node.name)) {
            SPEC_RULE("Lower-ReadPlace-Ident-Capture");
            IRPtr ir = EmptyIR();
            IRValue field_ptr = ctx.CaptureFieldPtr(*capture);
            IRValue value = ctx.FreshTempValue("capture_val");
            if (capture->by_ref) {
              IRValue captured_ptr = ctx.FreshTempValue("capture_ptr");
              IRReadPtr load_ptr;
              load_ptr.ptr = field_ptr;
              load_ptr.result = captured_ptr;
              ctx.RegisterValueType(captured_ptr, capture->field_type);
              IRReadPtr load_val;
              load_val.ptr = captured_ptr;
              load_val.result = value;
              ir = SeqIR({MakeIR(std::move(load_ptr)), MakeIR(std::move(load_val))});
            } else {
              IRReadPtr load_val;
              load_val.ptr = field_ptr;
              load_val.result = value;
              ir = MakeIR(std::move(load_val));
            }
            if (ctx.expr_type) {
              ctx.RegisterValueType(value, ctx.expr_type(place));
            } else {
              ctx.RegisterValueType(value, capture->value_type);
            }
            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
            return LowerResult{SeqIR({key_ir, ir}), value};
          }

          std::vector<std::string> full;
          std::string resolved_name = node.name;
          if (!ctx.resolve_name) {
            ctx.ReportResolveFailure(node.name);
            full = ctx.module_path;
          } else {
            auto resolved = ctx.resolve_name(node.name);
            if (!resolved.has_value() || resolved->empty()) {
              ctx.ReportResolveFailure(node.name);
              full = ctx.module_path;
            } else {
              full = *resolved;
              resolved_name = full.back();
              full.pop_back();
            }
          }

          SPEC_RULE("Lower-ReadPlace-Ident-Path");
          IRReadPath read;
          read.path = std::move(full);
          read.name = resolved_name;
          IRValue value;
          value.kind = IRValue::Kind::Symbol;
          value.name = resolved_name;
          if (ctx.expr_type) {
            ctx.RegisterValueType(value, ctx.expr_type(place));
          }
          IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
          return LowerResult{SeqIR({key_ir, MakeIR(std::move(read))}), value};
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          SPEC_RULE("Lower-ReadPlace-Field");
          auto base_result = LowerReadPlace(*node.base, ctx);
          IRValue field_value = ctx.FreshTempValue("place_field");
          analysis::TypeRef field_type = ctx.expr_type ? ctx.expr_type(place) : nullptr;
          if (!field_type) {
            analysis::TypeRef base_type = ctx.LookupValueType(base_result.value);
            while (base_type) {
              if (const auto* perm = std::get_if<analysis::TypePerm>(&base_type->node)) {
                base_type = perm->base;
                continue;
              }
              if (const auto* refine = std::get_if<analysis::TypeRefine>(&base_type->node)) {
                base_type = refine->base;
                continue;
              }
              break;
            }
            if (base_type) {
              const auto* path = analysis::AppliedTypePath(*base_type);
              const auto* args = analysis::AppliedTypeArgs(*base_type);
              if (path) {
                const auto& scope = ScopeForLowering(ctx);
                if (const ast::RecordDecl* record = analysis::LookupRecordDecl(scope, *path)) {
                  const auto inferred = analysis::FieldType(
                      *record,
                      node.name,
                      scope,
                      args ? *args : std::vector<analysis::TypeRef>{});
                  if (inferred.has_value()) {
                    field_type = *inferred;
                  }
                }
              }
            }
          }
          if (field_type) {
            ctx.RegisterValueType(field_value, field_type);
          }
          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::Field;
          info.base = base_result.value;
          info.field = node.name;
          ctx.RegisterDerivedValue(field_value, info);
          IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
          return LowerResult{SeqIR({base_result.ir, key_ir}), field_value};
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          SPEC_RULE("Lower-ReadPlace-Tuple");
          auto base_result = LowerReadPlace(*node.base, ctx);
          IRValue elem_value = ctx.FreshTempValue("place_tuple_elem");
          if (ctx.expr_type) {
            ctx.RegisterValueType(elem_value, ctx.expr_type(place));
          }
          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::Tuple;
          info.base = base_result.value;
          info.tuple_index = ast::TupleIndexToSize(node.index).value();
          ctx.RegisterDerivedValue(elem_value, info);
          IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
          return LowerResult{SeqIR({base_result.ir, key_ir}), elem_value};
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? LowerReadPlace(*node.expr, ctx)
                           : LowerResult{EmptyIR(), ctx.FreshTempValue("place_attr")};
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto base_result = LowerReadPlace(*node.base, ctx);

          if (std::holds_alternative<ast::RangeExpr>(node.index->node)) {
            SPEC_RULE("Lower-ReadPlace-Index-Range");
            const auto& range_node = std::get<ast::RangeExpr>(node.index->node);
            auto range_result = LowerRangeExpr(range_node, ctx);

            IRCheckRange check;
            check.base = base_result.value;
            check.range = ToIRRange(range_result.value);

            IRValue slice_value = ctx.FreshTempValue("place_slice");
            if (ctx.expr_type) {
              ctx.RegisterValueType(slice_value, ctx.expr_type(place));
            }
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::Slice;
            info.base = base_result.value;
            info.range = ToIRRange(range_result.value);
            ctx.RegisterDerivedValue(slice_value, info);

            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
            return LowerResult{SeqIR({base_result.ir, range_result.ir, key_ir,
                                      MakeIR(std::move(check)),
                                      PanicFollowup(ctx)}),
                               slice_value};
          }

          if (IsRangeIndexExpr(*node.index, ctx)) {
            SPEC_RULE("Lower-ReadPlace-Index-Range");
            auto range_result = LowerExpr(*node.index, ctx);
            const auto range_kind = RangeIndexKindOf(*node.index, ctx);

            IRCheckRange check;
            check.base = base_result.value;
            check.range_value = range_result.value;
            if (range_kind.has_value()) {
              check.range.kind = ToIRRangeKind(*range_kind);
            }

            IRValue slice_value = ctx.FreshTempValue("place_slice");
            if (ctx.expr_type) {
              ctx.RegisterValueType(slice_value, ctx.expr_type(place));
            }
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::Slice;
            info.base = base_result.value;
            info.range_value = range_result.value;
            if (range_kind.has_value()) {
              info.range.kind = ToIRRangeKind(*range_kind);
            }
            ctx.RegisterDerivedValue(slice_value, info);

            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx);
            return LowerResult{SeqIR({base_result.ir, range_result.ir, key_ir,
                                      MakeIR(std::move(check)),
                                      PanicFollowup(ctx)}),
                               slice_value};
          }

          SPEC_RULE("Lower-ReadPlace-Index-Scalar");
          auto index_result = LowerExpr(*node.index, ctx);
          const bool needs_check = NeedsIndexCheck(*node.base, ctx);
          IRCheckIndex check;
          check.base = base_result.value;
          check.index = index_result.value;

          IRValue elem_value = ctx.FreshTempValue("place_index_elem");
          if (ctx.expr_type) {
            ctx.RegisterValueType(elem_value, ctx.expr_type(place));
          }
          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::Index;
          info.base = base_result.value;
          info.index = index_result.value;
          ctx.RegisterDerivedValue(elem_value, info);

          std::vector<IRPtr> seq;
          seq.push_back(base_result.ir);
          seq.push_back(index_result.ir);
          seq.push_back(LowerImplicitKeyAccess(place, ast::KeyMode::Read, ctx));
          if (needs_check) {
            seq.push_back(MakeIR(std::move(check)));
            seq.push_back(PanicFollowup(ctx));
          }
          return LowerResult{SeqIR(std::move(seq)), elem_value};
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          SPEC_RULE("Lower-ReadPlace-Deref");
          auto ptr_result = LowerExpr(*node.value, ctx);
          analysis::TypeRef ptr_type = ctx.LookupValueType(ptr_result.value);
          if (ctx.expr_type) {
            analysis::TypeRef expr_ptr_type = ctx.expr_type(*node.value);
            auto pointer_specificity = [](const analysis::TypeRef& type) -> int {
              if (!type) {
                return 0;
              }
              analysis::TypeRef stripped = analysis::StripPerm(type);
              if (!stripped) {
                stripped = type;
              }
              if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
                return ptr->state.has_value() ? 3 : 2;
              }
              if (std::holds_alternative<analysis::TypeRawPtr>(stripped->node)) {
                return 2;
              }
              return 1;
            };
            if (pointer_specificity(expr_ptr_type) > pointer_specificity(ptr_type)) {
              ptr_type = expr_ptr_type;
            }
          }
          if (ptr_type) {
            auto deref_result = LowerRawDeref(ptr_result.value, ptr_type, ctx);
            return LowerResult{SeqIR({ptr_result.ir, deref_result.ir}),
                               deref_result.value};
          }
          IRReadPtr read;
          read.ptr = ptr_result.value;
          IRValue value = ctx.FreshTempValue("deref");
          read.result = value;
          if (ctx.expr_type) {
            ctx.RegisterValueType(value, ctx.expr_type(place));
          }

          IRCheckOp null_check;
          null_check.op = "nonnull";
          null_check.reason = PanicReasonString(PanicReason::NullDeref);
          null_check.lhs = ptr_result.value;

          IRCall active_call;
          active_call.callee.kind = IRValue::Kind::Symbol;
          active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
          active_call.args.push_back(ptr_result.value);
          IRValue active_value = ctx.FreshTempValue("addr_active");
          active_call.result = active_value;
          ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

          IRCheckOp active_check;
          active_check.op = "addr_active";
          active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
          active_check.lhs = active_value;

          return LowerResult{SeqIR({
                                 ptr_result.ir,
                                 MakeIR(std::move(null_check)),
                                 PanicFollowup(ctx),
                                 MakeIR(std::move(active_call)),
                                 MakeIR(std::move(active_check)),
                                 PanicFollowup(ctx),
                                 MakeIR(std::move(read)),
                             }),
                             value};
        }

        IRValue value = ctx.FreshTempValue("place_unknown");
        value.name = "place_read";
        return LowerResult{EmptyIR(), value};
      },
      place.node);
}

namespace {

IRPtr LowerWritePlaceImpl(const ast::Expr& place,
                          const IRValue& value,
                          LowerCtx& ctx,
                          bool allow_drop);

struct StaticBindFlags {
  bool has_responsibility = false;
  bool immovable = false;
};

bool IsPlaceExprLite(const ast::ExprPtr& expr);
bool IsMoveExprLite(const ast::ExprPtr& expr);

bool IsMoveExprLite(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::MoveExpr>(expr->node);
}

bool IsPlaceExprLite(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? IsPlaceExprLite(node.expr) : false;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return IsPlaceExprLite(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return IsPlaceExprLite(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return IsPlaceExprLite(node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return IsPlaceExprLite(node.value);
        }
        return false;
      },
      expr->node);
}

IRPtr LowerWritePlaceImpl(const ast::Expr& place,
                          const IRValue& value,
                          LowerCtx& ctx,
                          bool allow_drop) {
  auto register_ptr_type = [&](IRValue& ptr_value,
                               const analysis::TypeRef& elem_type) {
    if (!elem_type) {
      return;
    }
    auto ptr_type = analysis::MakeTypePtr(elem_type, analysis::PtrState::Valid);
    ctx.RegisterValueType(ptr_value, ptr_type);
  };

  std::function<analysis::TypeRef(const analysis::TypeRef&)> unwrap_elem_type =
      [&](const analysis::TypeRef& type) -> analysis::TypeRef {
        if (!type) {
          return nullptr;
        }
        if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
          return unwrap_elem_type(perm->base);
        }
        if (const auto* slice = std::get_if<analysis::TypeSlice>(&type->node)) {
          return slice->element;
        }
        if (const auto* arr = std::get_if<analysis::TypeArray>(&type->node)) {
          return arr->element;
        }
        return nullptr;
      };

  auto lower_static_write = [&](std::vector<std::string> full,
                                const std::string& resolved_name) -> IRPtr {
    IRPtr poison_ir = CheckPoison(ModulePathString(full), ctx);

    IRPtr drop_ir = EmptyIR();
    if (allow_drop) {
      if (ctx.sigma) {
        if (auto bind_info = StaticBindInfo(*ctx.sigma, full, resolved_name)) {
          if (bind_info->has_responsibility) {
            analysis::TypeRef static_type;
            if (ctx.expr_type) {
              static_type = ctx.expr_type(place);
            }
            IRReadPath read;
            read.path = full;
            read.name = resolved_name;
            IRValue current_value;
            current_value.kind = IRValue::Kind::Symbol;
            current_value.name = resolved_name;
            drop_ir = SeqIR({MakeIR(std::move(read)),
                             EmitDrop(static_type, current_value, ctx)});
          }
        }
      }
    }

    IRStoreGlobal store;
    store.symbol =
        ctx.sigma ? StaticSymPath(*ctx.sigma, full, resolved_name)
                  : StaticSymPath(full, resolved_name);
    store.value = value;

    auto is_noop = [](const IRPtr& ir) {
      return !ir || std::holds_alternative<IROpaque>(ir->node);
    };

    std::vector<IRPtr> parts;
    if (!is_noop(poison_ir)) {
      parts.push_back(poison_ir);
      parts.push_back(PanicFollowup(ctx));
    }
    if (!is_noop(drop_ir)) {
      parts.push_back(drop_ir);
    }
    parts.push_back(LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx));
    parts.push_back(MakeIR(std::move(store)));

    if (parts.size() == 1) {
      return parts.front();
    }
    return SeqIR(std::move(parts));
  };

  return std::visit(
      [&](const auto& node) -> IRPtr {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (auto alias = ctx.LookupLocalAddrAlias(node.name)) {
            switch (alias->kind) {
              case LocalAddrAlias::Kind::Binding: {
                const BindingState* state =
                    ctx.GetBindingStateById(alias->binding_name,
                                            alias->binding_id);
                if (!state) {
                  ctx.ReportCodegenFailure();
                  return EmptyIR();
                }
                const std::string store_name =
                    state->stable_name.empty() ? alias->binding_name
                                               : state->stable_name;
                if (allow_drop) {
                  auto it = ctx.binding_states.find(alias->binding_name);
                  if (it != ctx.binding_states.end()) {
                    for (auto& binding_state : it->second) {
                      if (binding_state.binding_id == alias->binding_id) {
                        binding_state.is_moved = false;
                        binding_state.moved_fields.clear();
                        break;
                      }
                    }
                  }
                  IRStoreVar store;
                  store.name = store_name;
                  store.value = value;
                  IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
                  return SeqIR({key_ir, MakeIR(std::move(store))});
                }
                IRStoreVarNoDrop store_nodrop;
                store_nodrop.name = store_name;
                store_nodrop.value = value;
                IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
                return SeqIR({key_ir, MakeIR(std::move(store_nodrop))});
              }
              case LocalAddrAlias::Kind::Capture: {
                const auto* capture = ctx.LookupCapture(alias->capture_name);
                if (!capture) {
                  ctx.ReportCodegenFailure();
                  return EmptyIR();
                }
                IRValue field_ptr = ctx.CaptureFieldPtr(*capture);
                if (capture->by_ref) {
                  IRValue captured_ptr = ctx.FreshTempValue("capture_ptr");
                  IRReadPtr load_ptr;
                  load_ptr.ptr = field_ptr;
                  load_ptr.result = captured_ptr;
                  register_ptr_type(captured_ptr, capture->value_type);
                  IRWritePtr write;
                  write.ptr = captured_ptr;
                  write.value = value;
                  IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
                  return SeqIR({MakeIR(std::move(load_ptr)), key_ir,
                                MakeIR(std::move(write))});
                }
                IRWritePtr write;
                write.ptr = field_ptr;
                write.value = value;
                register_ptr_type(field_ptr, capture->value_type);
                IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
                return SeqIR({key_ir, MakeIR(std::move(write))});
              }
              case LocalAddrAlias::Kind::Static:
                return lower_static_write(alias->static_path, alias->static_name);
            }
          }

          if (ctx.GetBindingState(node.name)) {
            SPEC_RULE(allow_drop ? "Lower-WritePlace-Ident-Local"
                                 : "LowerWriteSub-Ident-Local");
            if (allow_drop) {
              SPEC_RULE("UpdateValid-StoreVar");
              auto it = ctx.binding_states.find(node.name);
              if (it != ctx.binding_states.end() && !it->second.empty()) {
                it->second.back().is_moved = false;
                it->second.back().moved_fields.clear();
              }
            }
            IRStoreVar store;
            IRStoreVarNoDrop store_nodrop;
            if (allow_drop) {
              store.name = node.name;
              store.value = value;
              IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
              return SeqIR({key_ir, MakeIR(std::move(store))});
            }
            SPEC_RULE("UpdateValid-StoreVarNoDrop");
            store_nodrop.name = node.name;
            store_nodrop.value = value;
            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
            return SeqIR({key_ir, MakeIR(std::move(store_nodrop))});
          }
          if (const auto* capture = ctx.LookupCapture(node.name)) {
            SPEC_RULE(allow_drop ? "Lower-WritePlace-Ident-Capture"
                                 : "LowerWriteSub-Ident-Capture");
            IRValue field_ptr = ctx.CaptureFieldPtr(*capture);
            if (capture->by_ref) {
              IRValue captured_ptr = ctx.FreshTempValue("capture_ptr");
              IRReadPtr load_ptr;
              load_ptr.ptr = field_ptr;
              load_ptr.result = captured_ptr;
              register_ptr_type(captured_ptr, capture->value_type);
              IRWritePtr write;
              write.ptr = captured_ptr;
              write.value = value;
              IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
              return SeqIR({MakeIR(std::move(load_ptr)), key_ir,
                            MakeIR(std::move(write))});
            }
            IRWritePtr write;
            write.ptr = field_ptr;
            write.value = value;
            register_ptr_type(field_ptr, capture->value_type);
            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
            return SeqIR({key_ir, MakeIR(std::move(write))});
          }

          std::vector<std::string> full;
          std::string resolved_name = node.name;
          if (!ctx.resolve_name) {
            ctx.ReportResolveFailure(node.name);
            full = ctx.module_path;
          } else {
            auto resolved = ctx.resolve_name(node.name);
            if (!resolved.has_value() || resolved->empty()) {
              ctx.ReportResolveFailure(node.name);
              full = ctx.module_path;
            } else {
              full = *resolved;
              resolved_name = full.back();
              full.pop_back();
            }
          }

          SPEC_RULE(allow_drop ? "Lower-WritePlace-Ident-Path"
                               : "LowerWriteSub-Ident-Path");
          IRPtr poison_ir = CheckPoison(ModulePathString(full), ctx);

          IRPtr drop_ir = EmptyIR();
          if (allow_drop) {
            if (ctx.sigma) {
              if (auto bind_info = StaticBindInfo(*ctx.sigma, full, resolved_name)) {
                if (bind_info->has_responsibility) {
                analysis::TypeRef static_type;
                if (ctx.expr_type) {
                  static_type = ctx.expr_type(place);
                }
                IRReadPath read;
                read.path = full;
                read.name = resolved_name;
                IRValue current_value;
                current_value.kind = IRValue::Kind::Symbol;
                current_value.name = resolved_name;
                drop_ir = SeqIR({MakeIR(std::move(read)),
                                 EmitDrop(static_type, current_value, ctx)});
              }
              }
            }
          }

          IRStoreGlobal store;
          store.symbol =
              ctx.sigma ? StaticSymPath(*ctx.sigma, full, resolved_name)
                        : StaticSymPath(full, resolved_name);
          store.value = value;

          auto is_noop = [](const IRPtr& ir) {
            return !ir || std::holds_alternative<IROpaque>(ir->node);
          };

          std::vector<IRPtr> parts;
          if (!is_noop(poison_ir)) {
            parts.push_back(poison_ir);
            parts.push_back(PanicFollowup(ctx));
          }
          if (!is_noop(drop_ir)) {
            parts.push_back(drop_ir);
          }
          parts.push_back(LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx));
          parts.push_back(MakeIR(std::move(store)));

          if (parts.size() == 1) {
            return parts.front();
          }
          return SeqIR(std::move(parts));
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          SPEC_RULE(allow_drop ? "Lower-WritePlace-Field"
                               : "LowerWriteSub-Field");
          auto base_addr =
              LowerAddrOf(*node.base, ctx, AddressUseKind::TransientNoEscape);

          IRValue ptr_value = ctx.FreshTempValue("addr_of_field");
          if (ctx.expr_type) {
            register_ptr_type(ptr_value, ctx.expr_type(place));
          }
          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::AddrField;
          info.base = base_addr.value;
          info.field = node.name;
          ctx.RegisterDerivedValue(ptr_value, info);

          IRPtr drop_ir = EmptyIR();
          if (allow_drop && DropOnAssignRoot(*node.base, ctx)) {
            analysis::TypeRef field_type;
            if (ctx.expr_type) {
              field_type = ctx.expr_type(place);
            }
            IRValue field_value = ctx.FreshTempValue("place_field_old");
            ctx.RegisterValueType(field_value, field_type);
            IRReadPtr read;
            read.ptr = ptr_value;
            read.result = field_value;
            drop_ir = SeqIR({MakeIR(std::move(read)),
                             EmitDrop(field_type, field_value, ctx)});
          }

          IRAddrOf addr_marker;
          addr_marker.place = LowerPlace(place, ctx);
          addr_marker.result = ptr_value;

          IRWritePtr write;
          write.ptr = ptr_value;
          write.value = value;

          if (allow_drop) {
            UpdateBindingAfterFieldAssign(place, ctx);
          }

          IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
          return SeqIR({base_addr.ir,
                        key_ir,
                        MakeIR(std::move(addr_marker)),
                        drop_ir,
                        MakeIR(std::move(write))});
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          SPEC_RULE(allow_drop ? "Lower-WritePlace-Tuple"
                               : "LowerWriteSub-Tuple");
          auto base_addr =
              LowerAddrOf(*node.base, ctx, AddressUseKind::TransientNoEscape);

          IRValue ptr_value = ctx.FreshTempValue("addr_of_tuple");
          if (ctx.expr_type) {
            register_ptr_type(ptr_value, ctx.expr_type(place));
          }
          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::AddrTuple;
          info.base = base_addr.value;
          info.tuple_index = ast::TupleIndexToSize(node.index).value();
          ctx.RegisterDerivedValue(ptr_value, info);

          IRPtr drop_ir = EmptyIR();
          if (allow_drop && DropOnAssignRoot(*node.base, ctx)) {
            analysis::TypeRef elem_type;
            if (ctx.expr_type) {
              elem_type = ctx.expr_type(place);
            }
            IRValue elem_value = ctx.FreshTempValue("place_tuple_old");
            ctx.RegisterValueType(elem_value, elem_type);
            IRReadPtr read;
            read.ptr = ptr_value;
            read.result = elem_value;
            drop_ir = SeqIR({MakeIR(std::move(read)),
                             EmitDrop(elem_type, elem_value, ctx)});
          }

          IRAddrOf addr_marker;
          addr_marker.place = LowerPlace(place, ctx);
          addr_marker.result = ptr_value;

          IRWritePtr write;
          write.ptr = ptr_value;
          write.value = value;

          IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
          return SeqIR({base_addr.ir,
                        key_ir,
                        MakeIR(std::move(addr_marker)),
                        drop_ir,
                        MakeIR(std::move(write))});
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? LowerWritePlaceImpl(*node.expr, value, ctx, allow_drop)
                           : EmptyIR();
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto base_addr =
              LowerAddrOf(*node.base, ctx, AddressUseKind::TransientNoEscape);

          analysis::TypeRef base_type;
          if (ctx.expr_type) {
            base_type = ctx.expr_type(*node.base);
          }
          IRValue base_value = ctx.FreshTempValue("place_index_base");
          ctx.RegisterValueType(base_value, base_type);
          IRReadPtr read_base;
          read_base.ptr = base_addr.value;
          read_base.result = base_value;
          IRPtr base_read_ir = MakeIR(std::move(read_base));

          if (std::holds_alternative<ast::RangeExpr>(node.index->node)) {
            SPEC_RULE(allow_drop ? "Lower-WritePlace-Index-Range"
                                 : "LowerWriteSub-Index-Range");
            const auto& range_node = std::get<ast::RangeExpr>(node.index->node);
            auto range_result = LowerRangeExpr(range_node, ctx);

            IRCheckRange check;
            check.base = base_value;
            check.range = ToIRRange(range_result.value);

            IRCheckSliceLen len_check;
            len_check.base = base_value;
            len_check.range = ToIRRange(range_result.value);
            len_check.value = value;

            IRValue ptr_value = ctx.FreshTempValue("addr_of_range");
            if (ctx.expr_type) {
              auto elem_type = unwrap_elem_type(ctx.expr_type(place));
              register_ptr_type(ptr_value, elem_type);
            }
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::AddrIndex;
            info.base = base_addr.value;
            info.range = ToIRRange(range_result.value);
            ctx.RegisterDerivedValue(ptr_value, info);

            IRWritePtr write;
            write.ptr = ptr_value;
            write.value = value;

            IRAddrOf addr_marker;
            addr_marker.place = LowerPlace(place, ctx);
            addr_marker.result = ptr_value;

            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
            return SeqIR({base_addr.ir, base_read_ir, range_result.ir, key_ir,
                          MakeIR(std::move(check)),
                          PanicFollowup(ctx),
                          MakeIR(std::move(len_check)),
                          PanicFollowup(ctx),
                          MakeIR(std::move(addr_marker)),
                          MakeIR(std::move(write))});
          }

          if (IsRangeIndexExpr(*node.index, ctx)) {
            SPEC_RULE(allow_drop ? "Lower-WritePlace-Index-Range"
                                 : "LowerWriteSub-Index-Range");
            auto range_result = LowerExpr(*node.index, ctx);
            const auto range_kind = RangeIndexKindOf(*node.index, ctx);

            IRCheckRange check;
            check.base = base_value;
            check.range_value = range_result.value;
            if (range_kind.has_value()) {
              check.range.kind = ToIRRangeKind(*range_kind);
            }

            IRCheckSliceLen len_check;
            len_check.base = base_value;
            len_check.range_value = range_result.value;
            if (range_kind.has_value()) {
              len_check.range.kind = ToIRRangeKind(*range_kind);
            }
            len_check.value = value;

            IRValue ptr_value = ctx.FreshTempValue("addr_of_range");
            if (ctx.expr_type) {
              auto elem_type = unwrap_elem_type(ctx.expr_type(place));
              register_ptr_type(ptr_value, elem_type);
            }
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::AddrIndex;
            info.base = base_addr.value;
            info.range_value = range_result.value;
            if (range_kind.has_value()) {
              info.range.kind = ToIRRangeKind(*range_kind);
            }
            ctx.RegisterDerivedValue(ptr_value, info);

            IRWritePtr write;
            write.ptr = ptr_value;
            write.value = value;

            IRAddrOf addr_marker;
            addr_marker.place = LowerPlace(place, ctx);
            addr_marker.result = ptr_value;

            IRPtr key_ir = LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx);
            return SeqIR({base_addr.ir, base_read_ir, range_result.ir, key_ir,
                          MakeIR(std::move(check)),
                          PanicFollowup(ctx),
                          MakeIR(std::move(len_check)),
                          PanicFollowup(ctx),
                          MakeIR(std::move(addr_marker)),
                          MakeIR(std::move(write))});
          }

          SPEC_RULE(allow_drop ? "Lower-WritePlace-Index-Scalar"
                               : "LowerWriteSub-Index-Scalar");
          auto index_result = LowerExpr(*node.index, ctx);

          const bool needs_check = NeedsIndexCheck(*node.base, ctx);
          IRCheckIndex check;
          check.base = base_value;
          check.index = index_result.value;

          IRValue ptr_value = ctx.FreshTempValue("addr_of_index");
          if (ctx.expr_type) {
            register_ptr_type(ptr_value, ctx.expr_type(place));
          }
          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::AddrIndex;
          info.base = base_addr.value;
          info.index = index_result.value;
          ctx.RegisterDerivedValue(ptr_value, info);

          IRPtr drop_ir = EmptyIR();
          if (allow_drop && DropOnAssignRoot(*node.base, ctx)) {
            analysis::TypeRef elem_type;
            if (ctx.expr_type) {
              elem_type = ctx.expr_type(place);
            }
            IRValue elem_value = ctx.FreshTempValue("place_index_old");
            ctx.RegisterValueType(elem_value, elem_type);
            IRReadPtr read;
            read.ptr = ptr_value;
            read.result = elem_value;
            drop_ir = SeqIR({MakeIR(std::move(read)),
                             EmitDrop(elem_type, elem_value, ctx)});
          }

          IRAddrOf addr_marker;
          addr_marker.place = LowerPlace(place, ctx);
          addr_marker.result = ptr_value;

          IRWritePtr write;
          write.ptr = ptr_value;
          write.value = value;

          std::vector<IRPtr> seq;
          seq.push_back(base_addr.ir);
          seq.push_back(base_read_ir);
          seq.push_back(index_result.ir);
          seq.push_back(LowerImplicitKeyAccess(place, ast::KeyMode::Write, ctx));
          if (needs_check) {
            seq.push_back(MakeIR(std::move(check)));
            seq.push_back(PanicFollowup(ctx));
          }
          seq.push_back(MakeIR(std::move(addr_marker)));
          seq.push_back(drop_ir);
          seq.push_back(MakeIR(std::move(write)));
          return SeqIR(std::move(seq));
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          SPEC_RULE(allow_drop ? "Lower-WritePlace-Deref"
                               : "LowerWriteSub-Deref");
          auto ptr_result = LowerExpr(*node.value, ctx);
          analysis::TypeRef ptr_type = ctx.LookupValueType(ptr_result.value);
          if (ctx.expr_type) {
            analysis::TypeRef expr_ptr_type = ctx.expr_type(*node.value);
            auto pointer_specificity = [](const analysis::TypeRef& type) -> int {
              if (!type) {
                return 0;
              }
              analysis::TypeRef stripped = analysis::StripPerm(type);
              if (!stripped) {
                stripped = type;
              }
              if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
                return ptr->state.has_value() ? 3 : 2;
              }
              if (std::holds_alternative<analysis::TypeRawPtr>(stripped->node)) {
                return 2;
              }
              return 1;
            };
            if (pointer_specificity(expr_ptr_type) > pointer_specificity(ptr_type)) {
              ptr_type = expr_ptr_type;
            }
          }
          ptr_type = analysis::StripPerm(ptr_type);
          if (ptr_type) {
            if (const auto* ptr = std::get_if<analysis::TypePtr>(&ptr_type->node)) {
              if (ptr->state.has_value()) {
                if (*ptr->state == analysis::PtrState::Null) {
                  return SeqIR({ptr_result.ir, LowerPanic(PanicReason::NullDeref, ctx)});
                }
                if (*ptr->state == analysis::PtrState::Expired) {
                  return SeqIR({ptr_result.ir, LowerPanic(PanicReason::ExpiredDeref, ctx)});
                }
                if (*ptr->state == analysis::PtrState::Valid) {
                  IRCheckOp null_check;
                  null_check.op = "nonnull";
                  null_check.reason = PanicReasonString(PanicReason::NullDeref);
                  null_check.lhs = ptr_result.value;

                  IRCall active_call;
                  active_call.callee.kind = IRValue::Kind::Symbol;
                  active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
                  active_call.args.push_back(ptr_result.value);
                  IRValue active_value = ctx.FreshTempValue("addr_active");
                  active_call.result = active_value;
                  ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

                  IRCheckOp active_check;
                  active_check.op = "addr_active";
                  active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
                  active_check.lhs = active_value;

                  IRWritePtr write;
                  write.ptr = ptr_result.value;
                  write.value = value;
                  return SeqIR({
                      ptr_result.ir,
                      MakeIR(std::move(null_check)),
                      PanicFollowup(ctx),
                      MakeIR(std::move(active_call)),
                      MakeIR(std::move(active_check)),
                      PanicFollowup(ctx),
                      MakeIR(std::move(write)),
                  });
                }
              } else {
                IRCheckOp null_check;
                null_check.op = "nonnull";
                null_check.reason = PanicReasonString(PanicReason::NullDeref);
                null_check.lhs = ptr_result.value;

                IRCall active_call;
                active_call.callee.kind = IRValue::Kind::Symbol;
                active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
                active_call.args.push_back(ptr_result.value);
                IRValue active_value = ctx.FreshTempValue("addr_active");
                active_call.result = active_value;
                ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

                IRCheckOp active_check;
                active_check.op = "addr_active";
                active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
                active_check.lhs = active_value;

                IRWritePtr write;
                write.ptr = ptr_result.value;
                write.value = value;
                return SeqIR({
                    ptr_result.ir,
                    MakeIR(std::move(null_check)),
                    PanicFollowup(ctx),
                    MakeIR(std::move(active_call)),
                    MakeIR(std::move(active_check)),
                    PanicFollowup(ctx),
                    MakeIR(std::move(write)),
                });
              }
            } else if (const auto* raw = std::get_if<analysis::TypeRawPtr>(&ptr_type->node)) {
              if (raw->qual == analysis::RawPtrQual::Imm) {
                return SeqIR({ptr_result.ir, LowerPanic(PanicReason::Other, ctx)});
              }
            } else if (const auto* path = std::get_if<analysis::TypePathType>(&ptr_type->node)) {
              if (!path->path.empty() && path->path.back() == "Ptr") {
                IRCheckOp null_check;
                null_check.op = "nonnull";
                null_check.reason = PanicReasonString(PanicReason::NullDeref);
                null_check.lhs = ptr_result.value;

                IRCall active_call;
                active_call.callee.kind = IRValue::Kind::Symbol;
                active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
                active_call.args.push_back(ptr_result.value);
                IRValue active_value = ctx.FreshTempValue("addr_active");
                active_call.result = active_value;
                ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

                IRCheckOp active_check;
                active_check.op = "addr_active";
                active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
                active_check.lhs = active_value;

                IRWritePtr write;
                write.ptr = ptr_result.value;
                write.value = value;
                return SeqIR({
                    ptr_result.ir,
                    MakeIR(std::move(null_check)),
                    PanicFollowup(ctx),
                    MakeIR(std::move(active_call)),
                    MakeIR(std::move(active_check)),
                    PanicFollowup(ctx),
                    MakeIR(std::move(write)),
                });
              }
            }
          }
          IRCheckOp null_check;
          null_check.op = "nonnull";
          null_check.reason = PanicReasonString(PanicReason::NullDeref);
          null_check.lhs = ptr_result.value;

          IRCall active_call;
          active_call.callee.kind = IRValue::Kind::Symbol;
          active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
          active_call.args.push_back(ptr_result.value);
          IRValue active_value = ctx.FreshTempValue("addr_active");
          active_call.result = active_value;
          ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

          IRCheckOp active_check;
          active_check.op = "addr_active";
          active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
          active_check.lhs = active_value;

          IRWritePtr write;
          write.ptr = ptr_result.value;
          write.value = value;
          return SeqIR({
              ptr_result.ir,
              MakeIR(std::move(null_check)),
              PanicFollowup(ctx),
              MakeIR(std::move(active_call)),
              MakeIR(std::move(active_check)),
              PanicFollowup(ctx),
              MakeIR(std::move(write)),
          });
        }

        return EmptyIR();
      },
      place.node);
}

}  // namespace

IRPtr LowerWritePlace(const ast::Expr& place,
                      const IRValue& value,
                      LowerCtx& ctx) {
  return LowerWritePlaceImpl(place, value, ctx, true);
}

IRPtr LowerWritePlaceSub(const ast::Expr& place,
                         const IRValue& value,
                         LowerCtx& ctx) {
  return LowerWritePlaceImpl(place, value, ctx, false);
}

IRPlace LowerPlace(const ast::Expr& place, LowerCtx& /*ctx*/) {
  SPEC_RULE("Lower-Place-Ident");
  SPEC_RULE("Lower-Place-Field");
  SPEC_RULE("Lower-Place-Tuple");
  SPEC_RULE("Lower-Place-Index");
  SPEC_RULE("Lower-Place-Deref");

  IRPlace result;
  result.repr = BuildPlaceRepr(place);
  return result;
}

}  // namespace cursive::codegen
