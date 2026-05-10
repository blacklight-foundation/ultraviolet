// =============================================================================
// MIGRATION MAPPING: expr/addr_of.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16243-16246: (Lower-Expr-AddressOf)
//   - Lines 16428-16477: LowerAddrOf rules for all place forms
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 888-1153: LowerAddrOf
//   - Lines 1159-1185: LowerMovePlace
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/ir/ir_model.h (IRAddrOf, DerivedValueInfo)
//   - cursive/include/05_codegen/lower/lower_expr.h (LowerCtx, LowerResult)
//   - cursive/include/05_codegen/checks/checks.h (PanicCheck, LowerPanic, etc.)
//   - cursive/include/04_analysis/typing/types.h (TypeRef, TypePtr, etc.)
//   - cursive/include/04_analysis/typing/type_predicates.h (StripPerm)
//
// =============================================================================

#include "05_codegen/lower/expr/addr_of.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/pattern/ir_pattern.h"
#include "04_analysis/typing/type_predicates.h"
#include "00_core/assert_spec.h"

#include <algorithm>
#include <cassert>
#include <string_view>
#include <variant>

namespace cursive::codegen {

namespace {

// Convert RangeVal to IRRange
IRRange ToIRRange(const RangeVal& range) {
  IRRange out;
  out.kind = ToIRRangeKind(range.kind);
  out.lo = range.lo;
  out.hi = range.hi;
  return out;
}

// Check if index bounds check is needed
bool AddrOfNeedsIndexCheck(const ast::Expr& base, const LowerCtx& ctx) {
  if (!ctx.expr_type) {
    return true;
  }
  analysis::TypeRef base_type = ctx.expr_type(base);
  analysis::TypeRef stripped = analysis::StripPerm(base_type);
  if (stripped && std::holds_alternative<analysis::TypeArray>(stripped->node)) {
    return true;
  }
  return true;
}

// addr_of dynamic attribute handling now uses shared utilities from expr_common.h

// Join module path components with "::"
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

std::optional<std::string> ExactIdentifierPlace(const ast::Expr& expr) {
  return std::visit(
      [&](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? ExactIdentifierPlace(*node.expr) : std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return node.name;
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

// Runtime symbol for region address tag propagation
// (BuiltinModalSym-AddrTagFrom)
std::string RuntimeBuiltinModalSymRegionAddrTagFrom() {
  return codegen::BuiltinModalSymRegionAddrTagFrom();
}

std::string RuntimeBuiltinModalSymRegionAddrTagScope() {
  return codegen::BuiltinModalSymRegionAddrTagScope();
}

// Runtime symbol for region address validity check
// (BuiltinModalSym-AddrIsActive)
std::string RuntimeBuiltinModalSymRegionAddrIsActive() {
  return codegen::BuiltinModalSymRegionAddrIsActive();
}

void SeedAddrRefSyms(IRAddrOf& addr, std::vector<IRPtr> prereq_ir) {
  if (prereq_ir.empty()) {
    addr.ref_syms.clear();
    return;
  }

  addr.ref_syms = RefSyms(SeqIR(std::move(prereq_ir)));
  std::sort(addr.ref_syms.begin(), addr.ref_syms.end());
  addr.ref_syms.erase(
      std::unique(addr.ref_syms.begin(), addr.ref_syms.end()),
      addr.ref_syms.end());
}

}  // namespace

// ============================================================================
// LowerAddrOf - Lower address-of expression
// ============================================================================
//
// Spec rules:
//   Lower-AddrOf-Ident-Local, Lower-AddrOf-Ident-Path, Lower-AddrOf-Field,
//   Lower-AddrOf-Tuple, Lower-AddrOf-Index, Lower-AddrOf-Index-OOB,
//   Lower-AddrOf-Deref, Lower-AddrOf-Deref-Null, Lower-AddrOf-Deref-Expired,
//   Lower-AddrOf-Deref-Raw
// ============================================================================

LowerResult LowerAddrOf(const ast::Expr& place, LowerCtx& ctx) {
  SPEC_RULE("Lower-AddrOf-Ident-Local");
  SPEC_RULE("Lower-AddrOf-Ident-Path");
  SPEC_RULE("Lower-AddrOf-Field");
  SPEC_RULE("Lower-AddrOf-Tuple");
  SPEC_RULE("Lower-AddrOf-Index");
  SPEC_RULE("Lower-AddrOf-Index-OOB");
  SPEC_RULE("Lower-AddrOf-Deref");
  SPEC_RULE("Lower-AddrOf-Deref-Null");
  SPEC_RULE("Lower-AddrOf-Deref-Expired");
  SPEC_RULE("Lower-AddrOf-Deref-Raw");

  auto register_ptr_type = [&](IRValue& value) {
    analysis::TypeRef pointee_type = nullptr;
    if (auto identifier = ExactIdentifierPlace(place)) {
      if (const BindingState* state = ctx.GetBindingState(*identifier)) {
        pointee_type = state->type;
      }
    }
    if (!pointee_type && ctx.expr_type) {
      pointee_type = ctx.expr_type(place);
    }
    if (!pointee_type) {
      return;
    }
    auto ptr_type = analysis::MakeTypePtr(pointee_type, analysis::PtrState::Valid);
    ctx.RegisterValueType(value, ptr_type);
  };

  auto tag_from = [&](const IRValue& addr, const IRValue& base) -> IRPtr {
    IRCall tag_call;
    tag_call.callee.kind = IRValue::Kind::Symbol;
    tag_call.callee.name = RuntimeBuiltinModalSymRegionAddrTagFrom();
    tag_call.args.push_back(addr);
    tag_call.args.push_back(base);
    IRValue tag_value = ctx.FreshTempValue("addr_tag");
    tag_call.result = tag_value;
    ctx.RegisterValueType(tag_value, analysis::MakeTypePrim("()"));
    return MakeIR(std::move(tag_call));
  };

  return std::visit(
      [&](const auto& node) -> LowerResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          IRAddrOf addr;
          addr.place = LowerPlace(place, ctx);

          IRValue ptr_value = ctx.FreshTempValue("addr_of");
          register_ptr_type(ptr_value);
          addr.result = ptr_value;

          auto lower_local_address = [&](const BindingState& state,
                                         std::string_view local_name,
                                         std::string_view source_name)
              -> LowerResult {
            SPEC_RULE("Lower-AddrOf-Ident-Local");
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::AddrLocal;
            info.name = std::string(local_name);
            ctx.RegisterDerivedValue(ptr_value, info);

            std::vector<IRPtr> seq;
            std::vector<IRPtr> prereq_ir;

            // If this binding was materialized by loading from an address,
            // preserve the source address tag instead of stamping the current
            // lexical scope.
            bool tagged_from_origin = false;
            auto lookup_origin = [&](std::string_view candidate)
                -> const DerivedValueInfo* {
              IRValue local_value;
              local_value.kind = IRValue::Kind::Local;
              local_value.name = std::string(candidate);
              return ctx.LookupDerivedValue(local_value);
            };

            if (const DerivedValueInfo* local_info = lookup_origin(local_name)) {
              if (local_info->kind == DerivedValueInfo::Kind::LoadFromAddr) {
                prereq_ir.push_back(tag_from(ptr_value, local_info->base));
                tagged_from_origin = true;
              }
            }
            if (!tagged_from_origin && local_name != source_name) {
              if (const DerivedValueInfo* local_info = lookup_origin(source_name)) {
                if (local_info->kind == DerivedValueInfo::Kind::LoadFromAddr) {
                  prereq_ir.push_back(tag_from(ptr_value, local_info->base));
                  tagged_from_origin = true;
                }
              }
            }

            const bool should_tag_scope =
                !tagged_from_origin &&
                !state.preserve_addr_provenance &&
                state.scope_runtime_id != 0;
            if (should_tag_scope) {
              IRCall tag_scope;
              tag_scope.callee.kind = IRValue::Kind::Symbol;
              tag_scope.callee.name = RuntimeBuiltinModalSymRegionAddrTagScope();
              tag_scope.args.push_back(ptr_value);
              tag_scope.args.push_back(USizeConstValue(state.scope_runtime_id));
              IRValue tag_value = ctx.FreshTempValue("addr_tag_scope");
              tag_scope.result = tag_value;
              ctx.RegisterValueType(tag_value, analysis::MakeTypePrim("()"));
              prereq_ir.push_back(MakeIR(std::move(tag_scope)));
            }
            SeedAddrRefSyms(addr, prereq_ir);
            seq.push_back(MakeIR(std::move(addr)));
            seq.insert(seq.end(), prereq_ir.begin(), prereq_ir.end());
            return LowerResult{SeqIR(std::move(seq)), ptr_value};
          };

          auto lower_capture_address = [&](const CaptureAccess& capture)
              -> LowerResult {
            SPEC_RULE("Lower-AddrOf-Ident-Capture");
            IRValue field_ptr = ctx.CaptureFieldPtr(capture);
            if (capture.by_ref) {
              IRReadPtr load_ptr;
              load_ptr.ptr = field_ptr;
              load_ptr.result = ptr_value;
              return LowerResult{MakeIR(std::move(load_ptr)), ptr_value};
            }
            return LowerResult{EmptyIR(), field_ptr};
          };

          auto lower_static_address = [&](std::vector<std::string> full,
                                          std::string resolved_name)
              -> LowerResult {
            SPEC_RULE("Lower-AddrOf-Ident-Path");
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::AddrStatic;
            info.static_path = full;
            info.name = resolved_name;
            ctx.RegisterDerivedValue(ptr_value, info);

            IRPtr poison_ir = CheckPoison(ModulePathString(full), ctx);
            if (poison_ir && !std::holds_alternative<IROpaque>(poison_ir->node)) {
              SeedAddrRefSyms(addr, {poison_ir, PanicFollowup(ctx)});
              return LowerResult{SeqIR(std::vector<IRPtr>{poison_ir,
                                                          PanicFollowup(ctx),
                                                          MakeIR(std::move(addr))}),
                                 ptr_value};
            }
            SeedAddrRefSyms(addr, {});
            return LowerResult{MakeIR(std::move(addr)), ptr_value};
          };

          if (auto alias = ctx.LookupLocalAddrAlias(node.name)) {
            switch (alias->kind) {
              case LocalAddrAlias::Kind::Binding: {
                if (const BindingState* state =
                        ctx.GetBindingStateById(alias->binding_name, alias->binding_id)) {
                  return lower_local_address(*state,
                                             alias->stable_name,
                                             alias->binding_name);
                }
                ctx.ReportCodegenFailure();
                return LowerResult{MakeIR(std::move(addr)), ptr_value};
              }
              case LocalAddrAlias::Kind::Capture: {
                if (const auto* capture = ctx.LookupCapture(alias->capture_name)) {
                  return lower_capture_address(*capture);
                }
                ctx.ReportCodegenFailure();
                return LowerResult{MakeIR(std::move(addr)), ptr_value};
              }
              case LocalAddrAlias::Kind::Static:
                return lower_static_address(alias->static_path, alias->static_name);
            }
          }

          if (const BindingState* state = ctx.GetBindingState(node.name)) {
            return lower_local_address(*state, state->stable_name, node.name);
          }
          if (const auto* capture = ctx.LookupCapture(node.name)) {
            return lower_capture_address(*capture);
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

          return lower_static_address(std::move(full), std::move(resolved_name));
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto base_result = LowerAddrOf(*node.base, ctx);
          IRAddrOf addr;
          addr.place = LowerPlace(place, ctx);
          IRValue ptr_value = ctx.FreshTempValue("addr_of");
          register_ptr_type(ptr_value);
          addr.result = ptr_value;

          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::AddrField;
          info.base = base_result.value;
          info.field = node.name;
          ctx.RegisterDerivedValue(ptr_value, info);

          IRPtr tag_ir = tag_from(ptr_value, base_result.value);
          SeedAddrRefSyms(addr, {base_result.ir, tag_ir});
          return LowerResult{SeqIR(std::vector<IRPtr>{base_result.ir,
                                                      MakeIR(std::move(addr)),
                                                      tag_ir}),
                             ptr_value};
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto base_result = LowerAddrOf(*node.base, ctx);
          IRAddrOf addr;
          addr.place = LowerPlace(place, ctx);
          IRValue ptr_value = ctx.FreshTempValue("addr_of");
          register_ptr_type(ptr_value);
          addr.result = ptr_value;

          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::AddrTuple;
          info.base = base_result.value;
          info.tuple_index = ast::TupleIndexToSize(node.index).value();
          ctx.RegisterDerivedValue(ptr_value, info);

          IRPtr tag_ir = tag_from(ptr_value, base_result.value);
          SeedAddrRefSyms(addr, {base_result.ir, tag_ir});
          return LowerResult{SeqIR(std::vector<IRPtr>{base_result.ir,
                                                      MakeIR(std::move(addr)),
                                                      tag_ir}),
                             ptr_value};
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          const bool prev_dynamic = ctx.dynamic_checks;
          if (HasDynamicAttr(node.attrs)) {
            ctx.dynamic_checks = true;
          }
          LowerResult out = node.expr ? LowerAddrOf(*node.expr, ctx)
                                      : LowerResult{EmptyIR(), ctx.FreshTempValue("addr_of_attr")};
          ctx.dynamic_checks = prev_dynamic;
          return out;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto base_result = LowerAddrOf(*node.base, ctx);
          IRValue ptr_value = ctx.FreshTempValue("addr_of");
          register_ptr_type(ptr_value);

          IRAddrOf addr;
          addr.place = LowerPlace(place, ctx);
          addr.result = ptr_value;

          if (std::holds_alternative<ast::RangeExpr>(node.index->node)) {
            const auto& range_node = std::get<ast::RangeExpr>(node.index->node);
            auto range_result = LowerRangeExpr(range_node, ctx);

            IRCheckRange check;
            check.base = base_result.value;
            check.range = ToIRRange(range_result.value);

            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::AddrIndex;
            info.base = base_result.value;
            info.range = ToIRRange(range_result.value);
            ctx.RegisterDerivedValue(ptr_value, info);

            IRPtr tag_ir = tag_from(ptr_value, base_result.value);
            SeedAddrRefSyms(
                addr,
                {base_result.ir, range_result.ir, PanicFollowup(ctx), tag_ir});
            return LowerResult{SeqIR(std::vector<IRPtr>{base_result.ir, range_result.ir,
                                      MakeIR(std::move(check)),
                                      PanicFollowup(ctx),
                                      MakeIR(std::move(addr)),
                                      tag_ir}),
                               ptr_value};
          }

          if (IsRangeIndexExpr(*node.index, ctx)) {
            auto range_result = LowerExpr(*node.index, ctx);
            const auto range_kind = RangeIndexKindOf(*node.index, ctx);

            IRCheckRange check;
            check.base = base_result.value;
            check.range_value = range_result.value;
            if (range_kind.has_value()) {
              check.range.kind = ToIRRangeKind(*range_kind);
            }

            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::AddrIndex;
            info.base = base_result.value;
            info.range_value = range_result.value;
            if (range_kind.has_value()) {
              info.range.kind = ToIRRangeKind(*range_kind);
            }
            ctx.RegisterDerivedValue(ptr_value, info);

            IRPtr tag_ir = tag_from(ptr_value, base_result.value);
            SeedAddrRefSyms(
                addr,
                {base_result.ir, range_result.ir, PanicFollowup(ctx), tag_ir});
            return LowerResult{SeqIR(std::vector<IRPtr>{base_result.ir, range_result.ir,
                                      MakeIR(std::move(check)),
                                      PanicFollowup(ctx),
                                      MakeIR(std::move(addr)),
                                      tag_ir}),
                               ptr_value};
          }

          auto index_result = LowerExpr(*node.index, ctx);
          const bool needs_check = AddrOfNeedsIndexCheck(*node.base, ctx);
          IRCheckIndex check;
          check.base = base_result.value;
          check.index = index_result.value;

          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::AddrIndex;
          info.base = base_result.value;
          info.index = index_result.value;
          ctx.RegisterDerivedValue(ptr_value, info);

          IRPtr tag_ir = tag_from(ptr_value, base_result.value);
          std::vector<IRPtr> seq;
          seq.push_back(base_result.ir);
          seq.push_back(index_result.ir);
          if (needs_check) {
            seq.push_back(MakeIR(std::move(check)));
            seq.push_back(PanicFollowup(ctx));
          }
          {
            std::vector<IRPtr> prereq_ir = seq;
            prereq_ir.push_back(tag_ir);
            SeedAddrRefSyms(addr, std::move(prereq_ir));
          }
          seq.push_back(MakeIR(std::move(addr)));
          seq.push_back(tag_ir);
          return LowerResult{SeqIR(std::move(seq)), ptr_value};
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          // `AddrOf(Deref(e))` must evaluate pointer expression `e` directly.
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
            auto stripped = analysis::StripPerm(ptr_type);
            if (stripped) {
              if (const auto* raw = std::get_if<analysis::TypeRawPtr>(&stripped->node)) {
                (void)raw;
                SPEC_RULE("Lower-AddrOf-Deref-Raw");
                return LowerResult{ptr_result.ir, ptr_result.value};
              }
              if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
                if (ptr->state == analysis::PtrState::Null) {
                  SPEC_RULE("Lower-AddrOf-Deref-Null");
                  IRValue unreachable = ctx.FreshTempValue("unreachable");
                  return LowerResult{SeqIR({ptr_result.ir,
                                            LowerPanic(PanicReason::NullDeref, ctx)}),
                                     unreachable};
                }
                if (ptr->state == analysis::PtrState::Expired) {
                  SPEC_RULE("Lower-AddrOf-Deref-Expired");
                  IRValue unreachable = ctx.FreshTempValue("unreachable");
                  return LowerResult{SeqIR({ptr_result.ir,
                                            LowerPanic(PanicReason::ExpiredDeref, ctx)}),
                                     unreachable};
                }
                SPEC_RULE("Lower-AddrOf-Deref");
                IRCheckOp null_check;
                null_check.op = "nonnull";
                null_check.reason = PanicReasonString(PanicReason::NullDeref);
                null_check.lhs = ptr_result.value;
                IRCall call;
                call.callee.kind = IRValue::Kind::Symbol;
                call.callee.name = RuntimeBuiltinModalSymRegionAddrIsActive();
                call.args.push_back(ptr_result.value);
                IRValue active_value = ctx.FreshTempValue("addr_active");
                call.result = active_value;
                ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));
                IRCheckOp check;
                check.op = "addr_active";
                check.reason = PanicReasonString(PanicReason::ExpiredDeref);
                check.lhs = active_value;
                return LowerResult{SeqIR({ptr_result.ir,
                                          MakeIR(std::move(null_check)),
                                          PanicFollowup(ctx),
                                          MakeIR(std::move(call)),
                                          MakeIR(std::move(check)),
                                          PanicFollowup(ctx)}),
                                   ptr_result.value};
              }
              if (const auto* path = std::get_if<analysis::TypePathType>(&stripped->node)) {
                if (!path->path.empty() && path->path.back() == "RawPtr") {
                  SPEC_RULE("Lower-AddrOf-Deref-Raw");
                  return LowerResult{ptr_result.ir, ptr_result.value};
                }
                if (!path->path.empty() && path->path.back() == "Ptr") {
                  SPEC_RULE("Lower-AddrOf-Deref");
                  IRCheckOp null_check;
                  null_check.op = "nonnull";
                  null_check.reason = PanicReasonString(PanicReason::NullDeref);
                  null_check.lhs = ptr_result.value;
                  IRCall call;
                  call.callee.kind = IRValue::Kind::Symbol;
                  call.callee.name = RuntimeBuiltinModalSymRegionAddrIsActive();
                  call.args.push_back(ptr_result.value);
                  IRValue active_value = ctx.FreshTempValue("addr_active");
                  call.result = active_value;
                  ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));
                  IRCheckOp check;
                  check.op = "addr_active";
                  check.reason = PanicReasonString(PanicReason::ExpiredDeref);
                  check.lhs = active_value;
                  return LowerResult{SeqIR({ptr_result.ir,
                                            MakeIR(std::move(null_check)),
                                            PanicFollowup(ctx),
                                            MakeIR(std::move(call)),
                                            MakeIR(std::move(check)),
                                            PanicFollowup(ctx)}),
                                     ptr_result.value};
                }
              }
            }
          }
          SPEC_RULE("Lower-AddrOf-Deref");
          IRCheckOp null_check;
          null_check.op = "nonnull";
          null_check.reason = PanicReasonString(PanicReason::NullDeref);
          null_check.lhs = ptr_result.value;

          IRCall call;
          call.callee.kind = IRValue::Kind::Symbol;
          call.callee.name = RuntimeBuiltinModalSymRegionAddrIsActive();
          call.args.push_back(ptr_result.value);
          IRValue active_value = ctx.FreshTempValue("addr_active");
          call.result = active_value;
          ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

          IRCheckOp check;
          check.op = "addr_active";
          check.reason = PanicReasonString(PanicReason::ExpiredDeref);
          check.lhs = active_value;

          return LowerResult{SeqIR({ptr_result.ir,
                                    MakeIR(std::move(null_check)),
                                    PanicFollowup(ctx),
                                    MakeIR(std::move(call)),
                                    MakeIR(std::move(check)),
                                    PanicFollowup(ctx)}),
                             ptr_result.value};
        }

        IRAddrOf addr;
        addr.place = LowerPlace(place, ctx);
        IRValue ptr_value = ctx.FreshTempValue("addr_of");
        register_ptr_type(ptr_value);
        addr.result = ptr_value;
        SeedAddrRefSyms(addr, {});
        return LowerResult{MakeIR(std::move(addr)), ptr_value};
      },
      place.node);
}

}  // namespace cursive::codegen
