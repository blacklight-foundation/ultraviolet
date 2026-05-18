// =============================================================================
// Expression Lowering Common Utilities Implementation
// =============================================================================

#include "05_codegen/lower/expr/expr_common.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <variant>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/type_expr.h"
#include "05_codegen/intrinsics/builtins.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/lower/expr/addr_of.h"
#include "05_codegen/lower/expr/all_expr.h"
#include "05_codegen/lower/expr/alloc_expr.h"
#include "05_codegen/lower/expr/array_literal.h"
#include "05_codegen/lower/expr/binary.h"
#include "05_codegen/lower/expr/block_expr.h"
#include "05_codegen/lower/expr/call.h"
#include "05_codegen/lower/expr/cast.h"
#include "05_codegen/lower/expr/closure_expr.h"
#include "05_codegen/lower/expr/contract_entry.h"
#include "05_codegen/lower/expr/contract_result.h"
#include "05_codegen/lower/expr/deref.h"
#include "05_codegen/lower/expr/dispatch_expr.h"
#include "05_codegen/lower/expr/enum_literal.h"
#include "05_codegen/lower/expr/error_expr.h"
#include "05_codegen/lower/expr/field_access.h"
#include "05_codegen/lower/expr/identifier.h"
#include "05_codegen/lower/expr/if_case_expr.h"
#include "05_codegen/lower/expr/if_expr.h"
#include "05_codegen/lower/expr/index_access.h"
#include "05_codegen/lower/expr/literal.h"
#include "05_codegen/lower/expr/loop_conditional.h"
#include "05_codegen/lower/expr/loop_infinite.h"
#include "05_codegen/lower/expr/loop_iter.h"
#include "05_codegen/lower/expr/method_call.h"
#include "05_codegen/lower/expr/move_expr.h"
#include "05_codegen/lower/expr/null_ptr.h"
#include "05_codegen/lower/expr/parallel_expr.h"
#include "05_codegen/lower/expr/path.h"
#include "05_codegen/lower/expr/pipeline_expr.h"
#include "05_codegen/lower/expr/propagate_expr.h"
#include "05_codegen/lower/expr/qualified_apply.h"
#include "05_codegen/lower/expr/qualified_name.h"
#include "05_codegen/lower/expr/race_expr.h"
#include "05_codegen/lower/expr/range.h"
#include "05_codegen/lower/expr/record_literal.h"
#include "05_codegen/lower/expr/spawn_expr.h"
#include "05_codegen/lower/expr/sync_expr.h"
#include "05_codegen/lower/expr/transmute_expr.h"
#include "05_codegen/lower/expr/tuple_access.h"
#include "05_codegen/lower/expr/tuple_literal.h"
#include "05_codegen/lower/expr/unary.h"
#include "05_codegen/lower/expr/unsafe_block_expr.h"
#include "05_codegen/lower/expr/wait_expr.h"
#include "05_codegen/lower/expr/yield_expr.h"
#include "05_codegen/lower/expr/yield_from_expr.h"
#include "05_codegen/lower/pattern/ir_pattern.h"

namespace ultraviolet::codegen {

namespace {

std::vector<std::uint8_t> EncodeU64LE(std::uint64_t value) {
  std::vector<std::uint8_t> bytes;
  if (value == 0) {
    bytes.push_back(0);
    return bytes;
  }
  while (value > 0) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    value >>= 8;
  }
  return bytes;
}

IRValue StringImmediate(std::string_view text) {
  IRValue value;
  value.kind = IRValue::Kind::Immediate;
  value.name = "\"" + std::string(text) + "\"";
  value.bytes.assign(text.begin(), text.end());
  return value;
}

IRValue U8Immediate(std::uint8_t value) {
  IRValue out;
  out.kind = IRValue::Kind::Immediate;
  out.name = std::to_string(value);
  out.bytes = {value};
  return out;
}

std::string FormatRangeBound(const ast::ExprPtr& expr) {
  if (!expr) {
    return "";
  }
  if (const auto* attr = std::get_if<ast::AttributedExpr>(&expr->node)) {
    if (attr->expr) {
      return FormatRangeBound(attr->expr);
    }
  }
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    return lit->literal.lexeme;
  }
  return "?";
}

LowerResult LowerExprImpl(const ast::Expr& expr, LowerCtx& ctx) {
  return std::visit(
      [&ctx, &expr](const auto& node) -> LowerResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::ErrorExpr>) {
          return LowerError(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          const auto prev_order = ctx.current_access_order;
          if (const auto order = MemoryOrderFromAttrs(node.attrs)) {
            ctx.current_access_order = *order;
          }
          auto lowered = node.expr ? LowerExprImpl(*node.expr, ctx) : LowerResult{};
          ctx.current_access_order = prev_order;
          return lowered;
        } else if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return LowerLiteral(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return LowerIdentifier(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return LowerPath(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::PtrNullExpr>) {
          return LowerPtrNull(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          return LowerTuple(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          return LowerArrayLiteral(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return LowerArrayRepeat(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          SPEC_RULE("Lower-Expr-Sizeof");
          if (!ctx.sigma) {
            ctx.ReportCodegenFailure();
            return LowerResult{EmptyIR(), IRValue{}};
          }
          const analysis::ScopeContext& scope = ScopeForLowering(ctx);
          auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, node.type);
          if (!lowered) {
            ctx.ReportCodegenFailure();
            return LowerResult{EmptyIR(), IRValue{}};
          }
          auto layout = ::ultraviolet::analysis::layout::LayoutOf(scope, *lowered);
          if (!layout) {
            ctx.ReportCodegenFailure();
            return LowerResult{EmptyIR(), IRValue{}};
          }
          IRValue value;
          value.kind = IRValue::Kind::Immediate;
          value.name = std::to_string(layout->size);
          value.bytes = EncodeU64LE(layout->size);
          return LowerResult{EmptyIR(), value};
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          SPEC_RULE("Lower-Expr-Alignof");
          if (!ctx.sigma) {
            ctx.ReportCodegenFailure();
            return LowerResult{EmptyIR(), IRValue{}};
          }
          const analysis::ScopeContext& scope = ScopeForLowering(ctx);
          auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, node.type);
          if (!lowered) {
            ctx.ReportCodegenFailure();
            return LowerResult{EmptyIR(), IRValue{}};
          }
          auto layout = ::ultraviolet::analysis::layout::LayoutOf(scope, *lowered);
          if (!layout) {
            ctx.ReportCodegenFailure();
            return LowerResult{EmptyIR(), IRValue{}};
          }
          IRValue value;
          value.kind = IRValue::Kind::Immediate;
          value.name = std::to_string(layout->align);
          value.bytes = EncodeU64LE(layout->align);
          return LowerResult{EmptyIR(), value};
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          return LowerRecord(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          return LowerEnumLiteral(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return LowerReadPlaceFieldAccess(node, expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return LowerTupleAccess(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return LowerIndexAccess(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return LowerCallExpr(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return LowerMethodCall(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return LowerUnaryExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return LowerBinaryExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return LowerCastExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return LowerTransmuteExpr(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          return LowerClosureExpr(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return LowerPipelineExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return LowerPropagateExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return LowerIfExpr(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          analysis::TypeRef result_type = ctx.expr_type ? ctx.expr_type(expr) : nullptr;
          return LowerIfCases(*node.scrutinee, node.cases, node.else_expr,
                              false, ctx, result_type);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          std::vector<ast::IfCaseClause> cases;
          ast::IfCaseClause case_clause;
          case_clause.pattern = node.pattern;
          case_clause.body = node.then_expr;
          cases.push_back(std::move(case_clause));
          analysis::TypeRef result_type = ctx.expr_type ? ctx.expr_type(expr) : nullptr;
          return LowerIfCases(*node.scrutinee, cases, node.else_expr,
                              true, ctx, result_type);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return LowerBlock(*node.block, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          return LowerLoopInfinite(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          return LowerLoopConditional(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          return LowerLoopIter(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return LowerAddrOf(*node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return LowerReadPlaceDeref(node, expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return LowerMovePlace(*node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::CopyExpr>) {
          return LowerCopyExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return LowerRange(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return LowerAllocExpr(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return LowerUnsafeBlockExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          ctx.ReportCodegenFailure();
          IRValue value = ctx.FreshTempValue("comptime_expr_unlowerable");
          return LowerResult{EmptyIR(), value};
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          return LowerParallelExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          return LowerSpawnExpr(expr, node, ctx);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          return LowerDispatchExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return LowerWaitExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::FenceExpr>) {
          IRValue result = ctx.FreshTempValue("fence");
          IRFence fence;
          fence.order = ToIRFenceOrder(node.order);
          fence.result = result;
          return LowerResult{MakeIR(std::move(fence)), result};
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return LowerYieldExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return LowerYieldFromExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return LowerSyncExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          return LowerRaceExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          return LowerAllExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return LowerEntryExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          return LowerResultExpr(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return LowerQualifiedName(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          return LowerQualifiedApply(node, ctx);
        } else {
          ctx.ReportCodegenFailure();
          IRValue value = ctx.FreshTempValue("unhandled_expr_unlowerable");
          return LowerResult{EmptyIR(), value};
        }
      },
      expr.node);
}

}  // namespace

std::string StripIntSuffix(const std::string& text) {
  static const char* suffixes[] = {
      "isize", "usize", "i128", "u128", "i64", "u64",
      "i32", "u32", "i16", "u16", "i8", "u8"
  };
  for (const char* suf : suffixes) {
    const std::string_view sv{suf};
    if (text.size() >= sv.size() &&
        text.compare(text.size() - sv.size(), sv.size(), sv) == 0) {
      return text.substr(0, text.size() - sv.size());
    }
  }
  return text;
}

std::optional<std::uint64_t> ParseIntLiteralLexeme(const std::string& lexeme) {
  std::string text = StripIntSuffix(lexeme);

  if (text.rfind("0b", 0) == 0 || text.rfind("0B", 0) == 0) {
    text.erase(0, 2);
    if (text.empty()) {
      return std::nullopt;
    }
    std::uint64_t out = 0;
    for (char c : text) {
      if (c == '_') {
        continue;
      }
      if (c != '0' && c != '1') {
        return std::nullopt;
      }
      out = (out << 1) | (c == '1');
    }
    return out;
  }

  if (text.rfind("0o", 0) == 0 || text.rfind("0O", 0) == 0) {
    text.erase(0, 2);
    if (text.empty()) {
      return std::nullopt;
    }
    std::uint64_t out = 0;
    for (char c : text) {
      if (c == '_') {
        continue;
      }
      if (c < '0' || c > '7') {
        return std::nullopt;
      }
      out = (out << 3) | static_cast<std::uint64_t>(c - '0');
    }
    return out;
  }

  try {
    std::size_t idx = 0;
    std::uint64_t out = std::stoull(text, &idx, 0);
    if (idx != text.size()) {
      return std::nullopt;
    }
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

bool IsPlaceExpr(const ast::Expr& expr) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? IsPlaceExpr(*node.expr) : false;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return true;
        }
        return false;
      },
      expr.node);
}

bool IsTempValueExpr(const ast::Expr& expr) {
  return !IsPlaceExpr(expr);
}

bool NeedsIndexCheck(const ast::Expr& base, const LowerCtx& ctx) {
  if (!ctx.expr_type) {
    return true;
  }
  analysis::TypeRef base_type = ctx.expr_type(base);
  analysis::TypeRef stripped = analysis::StripPerm(base_type);
  if (stripped && std::holds_alternative<analysis::TypeArray>(stripped->node)) {
    return ctx.dynamic_checks;
  }
  return true;
}

bool IsRangeIndexExpr(const ast::Expr& expr, const LowerCtx& ctx) {
  if (std::holds_alternative<ast::RangeExpr>(expr.node)) {
    return true;
  }
  if (!ctx.expr_type) {
    return false;
  }
  const analysis::TypeRef index_type = ctx.expr_type(expr);
  return analysis::IsRangeType(index_type) &&
         analysis::IsRangeIndexType(index_type);
}

std::optional<ast::RangeKind> RangeIndexKindOf(const ast::Expr& expr,
                                               const LowerCtx& ctx) {
  if (const auto* range = std::get_if<ast::RangeExpr>(&expr.node)) {
    return range->kind;
  }
  if (!ctx.expr_type) {
    return std::nullopt;
  }

  analysis::TypeRef index_type = ctx.expr_type(expr);
  while (index_type) {
    if (const auto* perm = std::get_if<analysis::TypePerm>(&index_type->node)) {
      index_type = perm->base;
      continue;
    }
    if (const auto* refine =
            std::get_if<analysis::TypeRefine>(&index_type->node)) {
      index_type = refine->base;
      continue;
    }
    break;
  }

  if (!index_type) {
    return std::nullopt;
  }
  if (std::holds_alternative<analysis::TypeRange>(index_type->node)) {
    return ast::RangeKind::Exclusive;
  }
  if (std::holds_alternative<analysis::TypeRangeInclusive>(index_type->node)) {
    return ast::RangeKind::Inclusive;
  }
  if (std::holds_alternative<analysis::TypeRangeFrom>(index_type->node)) {
    return ast::RangeKind::From;
  }
  if (std::holds_alternative<analysis::TypeRangeTo>(index_type->node)) {
    return ast::RangeKind::To;
  }
  if (std::holds_alternative<analysis::TypeRangeToInclusive>(index_type->node)) {
    return ast::RangeKind::ToInclusive;
  }
  if (std::holds_alternative<analysis::TypeRangeFull>(index_type->node)) {
    return ast::RangeKind::Full;
  }
  return std::nullopt;
}

bool IsMoveExpr(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::MoveExpr>(expr->node);
}

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

std::string BuildPlaceRepr(const ast::Expr& expr) {
  return std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? BuildPlaceRepr(*node.expr) : "";
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return node.name;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          std::string base = BuildPlaceRepr(*node.base);
          if (base.empty()) {
            return node.name;
          }
          return base + "." + node.name;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          std::string base = BuildPlaceRepr(*node.base);
          std::string idx = ast::FormatTupleIndex(node.index);
          if (base.empty()) {
            return idx;
          }
          return base + "." + idx;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          std::string base = BuildPlaceRepr(*node.base);
          std::string idx = FormatIndexExpr(*node.index);
          if (base.empty()) {
            return "[" + idx + "]";
          }
          return base + "[" + idx + "]";
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return "*" + BuildPlaceRepr(*node.value);
        }
        return "";
      },
      expr.node);
}

bool HasDynamicAttr(const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name == analysis::attrs::kDynamic) {
      return true;
    }
  }
  return false;
}

bool HasMemoryOrderAttr(const ast::AttributeList& attrs) {
  return MemoryOrderFromAttrs(attrs).has_value();
}

std::optional<AccessOrdering> MemoryOrderFromAttrs(const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name == analysis::attrs::kRelaxed) {
      return AccessOrdering::Relaxed;
    }
    if (attr.name == analysis::attrs::kAcquire) {
      return AccessOrdering::Acquire;
    }
    if (attr.name == analysis::attrs::kRelease) {
      return AccessOrdering::Release;
    }
    if (attr.name == analysis::attrs::kAcqRel) {
      return AccessOrdering::AcqRel;
    }
    if (attr.name == analysis::attrs::kSeqCst) {
      return AccessOrdering::SeqCst;
    }
  }
  return std::nullopt;
}

static IRPtr EmitFenceIR(IRFenceOrder order, LowerCtx& ctx) {
  IRFence fence;
  fence.order = order;
  fence.result = ctx.FreshTempValue("ordered_access_fence");
  ctx.RegisterValueType(fence.result, analysis::MakeTypePrim("()"));
  return MakeIR(std::move(fence));
}

bool IsSharedAccessExpr(const ast::Expr& expr, const LowerCtx& ctx) {
  if (!ctx.expr_type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? IsSharedAccessExpr(*node.expr, ctx) : false;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr> ||
                             std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr> ||
                             std::is_same_v<T, ast::IndexAccessExpr> ||
                             std::is_same_v<T, ast::DerefExpr>) {
          const analysis::TypeRef type = ctx.expr_type(expr);
          return type &&
                 analysis::PermOfType(type) == analysis::Permission::Shared;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (!node.receiver) {
            return false;
          }
          const analysis::TypeRef recv_type = ctx.expr_type(*node.receiver);
          return recv_type &&
                 analysis::PermOfType(recv_type) == analysis::Permission::Shared;
        }
        return false;
      },
      expr.node);
}

std::string EncodeLoweredKeyPath(const analysis::KeyPath& path) {
  std::string encoded = path.root;
  for (const auto& seg : path.segs) {
    encoded += ".";
    encoded += seg.is_index ? "i:" : "f:";
    encoded += seg.name;
  }
  return encoded;
}

namespace {

bool RuntimeKeyIndexIsStatic(const ast::ExprPtr& expr) {
  const auto constant = analysis::EvaluateConstant(expr);
  return constant.known;
}

bool AppendRuntimeKeySegments(const ast::Expr& expr, std::string& encoded) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          encoded = node.name;
          return true;
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr ? AppendRuntimeKeySegments(*node.expr, encoded) : false;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          if (!node.base || !AppendRuntimeKeySegments(*node.base, encoded)) {
            return false;
          }
          encoded += ".f:";
          encoded += node.name;
          return true;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          if (!node.base || !AppendRuntimeKeySegments(*node.base, encoded)) {
            return false;
          }
          encoded += ".f:";
          encoded += ast::FormatTupleIndex(node.index);
          return true;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          if (!node.base || !AppendRuntimeKeySegments(*node.base, encoded)) {
            return false;
          }
          if (!RuntimeKeyIndexIsStatic(node.index)) {
            return true;
          }
          encoded += ".i:";
          encoded += FormatIndexExpr(*node.index);
          return true;
        }
        return false;
      },
      expr.node);
}

std::string EncodeRuntimeSharedAccessPath(const ast::Expr& expr) {
  std::string encoded;
  if (!AppendRuntimeKeySegments(expr, encoded)) {
    return "";
  }
  return encoded;
}

}  // namespace

IRPtr EnterSyntheticProcedureRegion(LowerCtx& ctx) {
  ast::ExprPtr region_opts = analysis::MakeDefaultRegionOptionsExpr();
  LowerResult opts_res = LowerExpr(*region_opts, ctx);

  const std::string region_alias = ctx.FreshRegionAlias();
  const analysis::TypeRef region_type = analysis::RegionActiveTypeRef();
  ctx.RegisterVar(region_alias,
                  region_type,
                  /*has_responsibility=*/false,
                  /*is_immovable=*/true,
                  analysis::ProvenanceKind::Region,
                  region_alias,
                  /*preserve_addr_provenance=*/false,
                  region_alias);
  ctx.RegisterRegionRelease(region_alias);
  ctx.active_region_aliases.push_back(region_alias);

  IRValue region_value = ctx.FreshTempValue("synthetic_proc_region");
  ctx.RegisterValueType(region_value, region_type);

  IRCall region_new;
  region_new.callee.kind = IRValue::Kind::Symbol;
  region_new.callee.name = BuiltinModalSymRegionNewScoped();
  region_new.args.push_back(opts_res.value);
  region_new.result = region_value;

  IRBindVar region_bind;
  region_bind.name = region_alias;
  region_bind.value = region_value;
  region_bind.type = region_type;
  region_bind.prov = analysis::ProvenanceKind::Region;
  region_bind.prov_region = region_alias;
  region_bind.prov_region_tag = region_alias;

  std::vector<IRPtr> parts;
  if (opts_res.ir && !std::holds_alternative<IROpaque>(opts_res.ir->node)) {
    parts.push_back(opts_res.ir);
  }
  parts.push_back(MakeIR(std::move(region_new)));
  parts.push_back(MakeIR(std::move(region_bind)));
  return SeqIR(std::move(parts));
}

ast::ExprPtr AliasExprPtr(const ast::Expr& expr) {
  return ast::ExprPtr(const_cast<ast::Expr*>(&expr), [](ast::Expr*) {});
}

bool KeyModeSufficient(std::uint8_t held, ast::KeyMode required) {
  return held == 1u || required == ast::KeyMode::Read;
}

bool HasCoveringActiveKey(const analysis::KeyPath& path,
                          ast::KeyMode required,
                          const LowerCtx& ctx) {
  for (auto scope_it = ctx.active_key_scopes.rbegin();
       scope_it != ctx.active_key_scopes.rend(); ++scope_it) {
    for (const auto& acquired : scope_it->acquired_paths) {
      if (!analysis::IsPrefix(acquired.path, path)) {
        continue;
      }
      if (KeyModeSufficient(acquired.mode, required)) {
        return true;
      }
    }
  }
  return false;
}

std::pair<IRPtr, IRValue> EnsureImplicitKeyScope(LowerCtx& ctx) {
  const auto scope_runtime_id = ctx.CurrentRuntimeScopeId().value_or(0);
  const analysis::TypeRef key_scope_type = analysis::MakeTypeRawPtr(
      analysis::RawPtrQual::Mut, analysis::MakeTypePrim("u8"));
  if (scope_runtime_id == 0) {
    return {EmptyIR(), IRValue{}};
  }

  if (const auto it = ctx.implicit_key_scope_names.find(scope_runtime_id);
      it != ctx.implicit_key_scope_names.end()) {
    IRValue scope_local;
    scope_local.kind = IRValue::Kind::Local;
    scope_local.name = it->second;
    ctx.RegisterValueType(scope_local, key_scope_type);
    return {EmptyIR(), scope_local};
  }

  std::vector<IRPtr> parts;
  IRCall key_scope_enter;
  key_scope_enter.callee.kind = IRValue::Kind::Symbol;
  key_scope_enter.callee.name = ConcurrencySymKeyScopeEnter();
  key_scope_enter.result = ctx.FreshTempValue("implicit_key_scope_enter");
  ctx.RegisterValueType(key_scope_enter.result, key_scope_type);
  IRValue key_scope_value = key_scope_enter.result;
  parts.push_back(MakeIR(std::move(key_scope_enter)));

  const std::string scope_local_name =
      ctx.FreshTempValue("__uv_implicit_key_scope").name;
  IRBindVar scope_bind;
  scope_bind.name = scope_local_name;
  scope_bind.value = key_scope_value;
  scope_bind.type = key_scope_type;
  parts.push_back(MakeIR(std::move(scope_bind)));

  ctx.RegisterVar(scope_local_name,
                  key_scope_type,
                  /*has_responsibility=*/false,
                  /*is_immovable=*/true,
                  analysis::ProvenanceKind::Bottom);
  ctx.RegisterKeyScopeExit(scope_local_name);
  ctx.implicit_key_scope_names.emplace(scope_runtime_id, scope_local_name);
  ctx.active_key_scopes.push_back(
      ActiveKeyScopeInfo{scope_runtime_id, scope_local_name, true, {}});

  IRValue scope_local;
  scope_local.kind = IRValue::Kind::Local;
  scope_local.name = scope_local_name;
  ctx.RegisterValueType(scope_local, key_scope_type);
  return {SeqIR(std::move(parts)), scope_local};
}

IRPtr LowerImplicitKeyAccess(const ast::Expr& expr,
                             ast::KeyMode mode,
                             LowerCtx& ctx) {
  if (!ctx.expr_type) {
    return EmptyIR();
  }

  const auto expr_type = ctx.expr_type(expr);
  if (analysis::PermOfType(expr_type) != analysis::Permission::Shared) {
    return EmptyIR();
  }

  const auto expr_ptr = AliasExprPtr(expr);
  if (!analysis::IsPlaceExpression(expr_ptr)) {
    return EmptyIR();
  }

  const auto built = analysis::BuildKeyPath(expr_ptr);
  if (!built.success) {
    return EmptyIR();
  }

  if (HasCoveringActiveKey(built.path, mode, ctx)) {
    return EmptyIR();
  }

  auto [scope_setup, scope_local] = EnsureImplicitKeyScope(ctx);
  if (scope_local.name.empty()) {
    ctx.ReportCodegenFailure();
    return EmptyIR();
  }

  std::string encoded_path = EncodeRuntimeSharedAccessPath(expr);
  if (encoded_path.empty()) {
    encoded_path = EncodeLoweredKeyPath(built.path);
  }
  const std::uint8_t mode_byte = mode == ast::KeyMode::Write ? 1u : 0u;

  IRCall check;
  check.callee.kind = IRValue::Kind::Symbol;
  check.callee.name = ConcurrencySymKeyCheckConflict();
  check.args.push_back(StringImmediate(encoded_path));
  check.args.push_back(U8Immediate(mode_byte));
  check.result = ctx.FreshTempValue("implicit_key_conflict_check");
  ctx.RegisterValueType(check.result, analysis::MakeTypePrim("()"));

  IRCall acquire;
  acquire.callee.kind = IRValue::Kind::Symbol;
  acquire.callee.name = ConcurrencySymKeyAcquire();
  acquire.args.push_back(scope_local);
  acquire.args.push_back(StringImmediate(encoded_path));
  acquire.args.push_back(U8Immediate(mode_byte));
  acquire.result = ctx.FreshTempValue("implicit_key_acquire");
  ctx.RegisterValueType(acquire.result, analysis::MakeTypePrim("()"));

  for (auto scope_it = ctx.active_key_scopes.rbegin();
       scope_it != ctx.active_key_scopes.rend(); ++scope_it) {
    if (scope_it->scope_name != scope_local.name) {
      continue;
    }
    scope_it->acquired_paths.push_back(
        ActiveKeyPathInfo{built.path, encoded_path, mode_byte});
    break;
  }

  return SeqIR({scope_setup, MakeIR(std::move(check)), MakeIR(std::move(acquire))});
}

LowerResult ApplyEffectiveOrdering(const ast::Expr& expr,
                                   LowerResult result,
                                   LowerCtx& ctx) {
  if (!IsSharedAccessExpr(expr, ctx)) {
    return result;
  }

  const AccessOrdering order =
      ctx.current_access_order.value_or(AccessOrdering::SeqCst);
  switch (order) {
    case AccessOrdering::Relaxed:
      return result;
    case AccessOrdering::Acquire:
      result.ir = SeqIR({result.ir, EmitFenceIR(IRFenceOrder::Acquire, ctx)});
      return result;
    case AccessOrdering::Release:
      result.ir = SeqIR({EmitFenceIR(IRFenceOrder::Release, ctx), result.ir});
      return result;
    case AccessOrdering::AcqRel:
      result.ir = SeqIR({EmitFenceIR(IRFenceOrder::Release, ctx),
                         result.ir,
                         EmitFenceIR(IRFenceOrder::Acquire, ctx)});
      return result;
    case AccessOrdering::SeqCst:
    default:
      result.ir = SeqIR({EmitFenceIR(IRFenceOrder::SeqCst, ctx),
                         result.ir,
                         EmitFenceIR(IRFenceOrder::SeqCst, ctx)});
      return result;
  }
}

std::string FormatRangeExpr(const ast::RangeExpr& expr) {
  const std::string lo = FormatRangeBound(expr.lhs);
  const std::string hi = FormatRangeBound(expr.rhs);
  switch (expr.kind) {
    case ast::RangeKind::Full:
      return "..";
    case ast::RangeKind::From:
      return lo + "..";
    case ast::RangeKind::To:
      return ".." + hi;
    case ast::RangeKind::ToInclusive:
      return "..=" + hi;
    case ast::RangeKind::Exclusive:
      return lo + ".." + hi;
    case ast::RangeKind::Inclusive:
      return lo + "..=" + hi;
  }
  return "..";
}

std::string FormatIndexExpr(const ast::Expr& expr) {
  if (const auto* attr = std::get_if<ast::AttributedExpr>(&expr.node)) {
    if (attr->expr) {
      return FormatIndexExpr(*attr->expr);
    }
  }
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr.node)) {
    return lit->literal.lexeme;
  }
  if (const auto* range = std::get_if<ast::RangeExpr>(&expr.node)) {
    return FormatRangeExpr(*range);
  }
  return "?";
}

std::vector<ast::ExprPtr> ArgsExprs(const std::vector<ast::Arg>& args) {
  if (args.empty()) {
    SPEC_RULE("ArgsExprs-Empty");
    return {};
  }
  SPEC_RULE("ArgsExprs-Cons");
  std::vector<ast::ExprPtr> result;
  result.reserve(args.size());
  for (const auto& arg : args) {
    result.push_back(arg.value);
  }
  return result;
}

std::vector<ast::ExprPtr> FieldExprs(const std::vector<ast::FieldInit>& fields) {
  if (fields.empty()) {
    SPEC_RULE("FieldExprs-Empty");
    return {};
  }
  SPEC_RULE("FieldExprs-Cons");
  std::vector<ast::ExprPtr> result;
  result.reserve(fields.size());
  for (const auto& field : fields) {
    result.push_back(field.value);
  }
  return result;
}

bool DispatchHasReduce(const ast::DispatchExpr& expr) {
  for (const auto& opt : expr.opts) {
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      return true;
    }
  }
  return false;
}

bool IsCollectableParallelExpr(const ast::Expr& expr, bool& needs_wait) {
  if (std::holds_alternative<ast::SpawnExpr>(expr.node)) {
    needs_wait = true;
    return true;
  }
  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&expr.node)) {
    if (DispatchHasReduce(*dispatch)) {
      needs_wait = false;
      return true;
    }
  }
  return false;
}

ast::ExprPtr WrapBlockExpr(const std::shared_ptr<ast::Block>& block) {
  if (!block) {
    return nullptr;
  }
  auto expr = std::make_shared<ast::Expr>();
  expr->span = block->span;
  expr->node = ast::BlockExpr{block};
  return expr;
}

void UpdateBindingAfterFieldAssign(const ast::Expr& place, LowerCtx& ctx) {
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

void AnchorExprCommonRules() {
  SPEC_RULE("LiteralValue-Int");
  SPEC_RULE("LiteralValue-Float");
  SPEC_RULE("LiteralValue-Bool");
  SPEC_RULE("LiteralValue-Char");
  SPEC_RULE("LiteralValue-String");
  SPEC_RULE("LiteralValue-Null");
  SPEC_RULE("IsPlace-Ident");
  SPEC_RULE("IsPlace-Field");
  SPEC_RULE("IsPlace-Tuple");
  SPEC_RULE("IsPlace-Index");
  SPEC_RULE("IsPlace-Deref");
  SPEC_RULE("PlaceRoot-Ident");
  SPEC_RULE("PlaceRoot-Field");
  SPEC_RULE("PlaceRoot-Tuple");
  SPEC_RULE("PlaceRoot-Index");
  SPEC_RULE("ArgsExprs-Empty");
  SPEC_RULE("ArgsExprs-Cons");
  SPEC_RULE("FieldExprs-Empty");
  SPEC_RULE("FieldExprs-Cons");
}

std::pair<IRPtr, std::vector<IRValue>> LowerList(
    const std::vector<ast::ExprPtr>& exprs, LowerCtx& ctx) {
  SPEC_RULE("LowerList-Empty");
  SPEC_RULE("LowerList-Cons");

  if (exprs.empty()) {
    return {EmptyIR(), {}};
  }

  std::vector<IRPtr> ir_parts;
  std::vector<IRValue> values;

  for (const auto& expr : exprs) {
    auto prev_suppress = ctx.suppress_temp_at_depth;
    ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    auto result = LowerExpr(*expr, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;
    ir_parts.push_back(result.ir);
    values.push_back(result.value);
  }

  return {SeqIR(std::move(ir_parts)), std::move(values)};
}

std::pair<IRPtr, std::vector<DerivedArraySegment>> LowerList(
    const std::vector<ast::ArraySegment>& segments, LowerCtx& ctx) {
  SPEC_RULE("LowerList-Empty");
  SPEC_RULE("LowerList-Cons");

  if (segments.empty()) {
    return {EmptyIR(), {}};
  }

  std::vector<IRPtr> ir_parts;
  std::vector<DerivedArraySegment> lowered_segments;
  lowered_segments.reserve(segments.size());

  for (const auto& segment : segments) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ArrayElemSegment>) {
            auto prev_suppress = ctx.suppress_temp_at_depth;
            ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
            auto value_result = LowerExpr(*node.value, ctx);
            ctx.suppress_temp_at_depth = prev_suppress;
            ir_parts.push_back(value_result.ir);

            DerivedArraySegment lowered_segment;
            lowered_segment.kind = DerivedArraySegment::Kind::Element;
            lowered_segment.value = value_result.value;
            lowered_segments.push_back(std::move(lowered_segment));
          } else if constexpr (std::is_same_v<T, ast::ArrayRepeatSegment>) {
            auto prev_suppress = ctx.suppress_temp_at_depth;
            ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
            auto value_result = LowerExpr(*node.value, ctx);
            auto count_result = LowerExpr(*node.count, ctx);
            ctx.suppress_temp_at_depth = prev_suppress;
            ir_parts.push_back(value_result.ir);
            ir_parts.push_back(count_result.ir);

            DerivedArraySegment lowered_segment;
            lowered_segment.kind = DerivedArraySegment::Kind::Repeat;
            lowered_segment.value = value_result.value;
            lowered_segment.count = count_result.value;
            lowered_segments.push_back(std::move(lowered_segment));
          }
        },
        segment);
  }

  return {SeqIR(std::move(ir_parts)), std::move(lowered_segments)};
}

std::pair<IRPtr, std::vector<std::pair<std::string, IRValue>>> LowerFieldInits(
    const std::vector<ast::FieldInit>& fields, LowerCtx& ctx, bool suppress_temps) {
  SPEC_RULE("LowerFieldInits-Empty");
  SPEC_RULE("LowerFieldInits-Cons");

  if (fields.empty()) {
    return {EmptyIR(), {}};
  }

  std::vector<IRPtr> ir_parts;
  std::vector<std::pair<std::string, IRValue>> field_values;

  for (const auto& field : fields) {
    auto prev_suppress = ctx.suppress_temp_at_depth;
    if (suppress_temps) {
      ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    }
    auto result = LowerExpr(*field.value, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;
    ir_parts.push_back(result.ir);
    field_values.emplace_back(field.name, result.value);
  }

  return {SeqIR(std::move(ir_parts)), std::move(field_values)};
}

IRValue RegisterLoweredRecordValue(
    std::vector<std::pair<std::string, IRValue>> field_values,
    std::optional<analysis::TypeRef> record_type,
    std::string_view temp_prefix,
    LowerCtx& ctx) {
  IRValue record_value = ctx.FreshTempValue(std::string(temp_prefix));
  DerivedValueInfo info;
  info.kind = DerivedValueInfo::Kind::RecordLit;
  info.fields = std::move(field_values);
  ctx.RegisterDerivedValue(record_value, info);
  if (record_type.has_value() && *record_type) {
    ctx.RegisterValueType(record_value, *record_type);
  }
  return record_value;
}

std::pair<IRPtr, std::optional<IRValue>> LowerOpt(
    const ast::ExprPtr& expr_opt, LowerCtx& ctx) {
  SPEC_RULE("LowerOpt-None");
  SPEC_RULE("LowerOpt-Some");

  if (!expr_opt) {
    return {EmptyIR(), std::nullopt};
  }

  auto result = LowerExpr(*expr_opt, ctx);
  return {result.ir, result.value};
}

LowerResult LowerExpr(const ast::Expr& expr, LowerCtx& ctx) {
  ctx.temp_depth += 1;
  LowerResult result = LowerExprImpl(expr, ctx);
  const int depth = ctx.temp_depth;
  analysis::TypeRef value_type = ctx.LookupValueType(result.value);

  if (ctx.expr_type) {
    if (!value_type) {
      if (analysis::TypeRef inferred = ctx.expr_type(expr)) {
        ctx.RegisterValueType(result.value, inferred);
        value_type = inferred;
      }
    }
  }
  ctx.temp_depth -= 1;

  bool suppress = ctx.suppress_temp_at_depth.has_value() &&
                  *ctx.suppress_temp_at_depth == depth;
  if (suppress) {
    ctx.suppress_temp_at_depth.reset();
  }

  if (!suppress && ctx.temp_sink && IsTempValueExpr(expr)) {
    ctx.RegisterTempValue(result.value, value_type);
  }

  if (IRPtr refine_check_ir =
          EmitDynamicRefinementChecksForExpr(expr, result.value, value_type, ctx);
      refine_check_ir && !std::holds_alternative<IROpaque>(refine_check_ir->node)) {
    result.ir = SeqIR({result.ir, refine_check_ir});
  }

  result = ApplyEffectiveOrdering(expr, std::move(result), ctx);

  return result;
}

// SplitModulePathString is defined in llvm_ir_panic.cpp (canonical location)

}  // namespace ultraviolet::codegen
