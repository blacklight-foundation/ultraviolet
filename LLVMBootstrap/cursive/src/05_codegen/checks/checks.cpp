// =============================================================================
// MIGRATION MAPPING: checks.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.11 Runtime Checks (lines 17200-17265)
//   - Check-Index-Ok/Check-Index-Err rules (lines 17235-17250)
//   - Check-Range-Ok/Check-Range-Err rules (lines 17250-17254)
//   - SliceBounds computation (lines 17245-17250)
//   - Lower-Transmute rule (lines 17255-17258)
//   - Lower-RawDeref-* rules (lines 17267-17285)
//   - IndexCheckRequired predicate
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/checks.cpp
//   - Lines 1-95: PoisonSetFor helper, StringImmediate, EmitRuntimeTrace
//   - Lines 97-165: PanicCode mapping, PanicReasonString
//   - Lines 167-250: Immediate parsing helpers
//   - Lines 253-349: CheckIndex, CheckRange, SliceBounds
//   - Lines 351-397: PanicSym, LowerPanic, ClearPanic, PanicCheck, InitPanicHandle
//   - Lines 402-447: LowerRangeExpr
//   - Lines 449-549: LowerTransmute, LowerRawDeref
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/checks/checks.h
//   - cursive/include/05_codegen/ir/ir_model.h (IRLowerPanic, IRPanicCheck)
//   - cursive/include/04_analysis/layout/layout.h (SizeOf)
//   - cursive/include/05_codegen/cleanup/cleanup.h (CleanupPlan)
//   - cursive/include/runtime/runtime_interface.h
//
// REFACTORING NOTES:
//   1. Runtime checks verify safety conditions at runtime
//   2. Index/range checks for array/slice bounds
//   3. Pointer state checks (null, expired)
//   4. Transmute size validation
//   5. SliceBounds computes (start, end) from range
//   6. Checks may be elided if statically proven safe
//
// PANIC CODES:
//   0x0001 - ErrorExpr
//   0x0002 - ErrorStmt
//   0x0003 - DivZero
//   0x0004 - Overflow
//   0x0005 - Shift
//   0x0006 - Bounds
//   0x0007 - Cast
//   0x0008 - NullDeref
//   0x0009 - ExpiredDeref
//   0x000A - InitPanic
//   0x000B - ContractPre
//   0x000C - ContractPost
//   0x000D - AsyncFailed
//   0x00FF - Other
//
// SLICEBOUNDS COMPUTATION:
//   Full: (0, len)
//   From(start): (start, len)
//   To(end): (0, end)
//   ToInclusive(end): (0, end+1)
//   Exclusive(start, end): (start, end)
//   Inclusive(start, end): (start, end+1)
//   Returns nullopt if invalid
//
// RAW DEREF LOWERING:
//   - Valid: emit ReadPtr
//   - Null: emit LowerPanic(NullDeref)
//   - Expired: emit LowerPanic(ExpiredDeref)
//   - Raw (*imm/*mut): emit unchecked ReadPtr
// =============================================================================

#include "05_codegen/checks/checks.h"
#include "05_codegen/checks/poison_instrument.h"
#include "05_codegen/common/runtime_trace_utils.h"
#include "05_codegen/globals/init.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/intrinsics/builtins.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_predicates.h"
#include "00_core/assert_spec.h"
#include "00_core/symbols.h"

#include <cassert>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cursive::codegen {
namespace {

analysis::TypeRef ResolveAliasTypeForCodegen(const analysis::TypeRef& type,
                                             const LowerCtx& ctx,
                                             std::size_t depth = 0) {
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    stripped = type;
  }
  if (!stripped || depth > 16 || !ctx.sigma) {
    return stripped;
  }

  const auto* path = std::get_if<analysis::TypePathType>(&stripped->node);
  if (!path) {
    return stripped;
  }

  ast::Path syntax_path;
  syntax_path.reserve(path->path.size());
  for (const auto& seg : path->path) {
    syntax_path.push_back(seg);
  }
  const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma->types.end()) {
    return stripped;
  }

  const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second);
  if (!alias) {
    return stripped;
  }

  analysis::ScopeContext scope;
  scope.sigma = *ctx.sigma;
  scope.sigma_source = ctx.sigma;
  scope.current_module = ctx.module_path;

  const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, alias->type);
  if (!lowered.has_value()) {
    return stripped;
  }

  analysis::TypeRef inst = *lowered;
  if (alias->generic_params &&
      !alias->generic_params->params.empty() &&
      !path->generic_args.empty()) {
    analysis::TypeSubst subst =
        analysis::BuildSubstitution(alias->generic_params->params,
                                    path->generic_args);
    inst = analysis::InstantiateType(inst, subst);
  }

  return ResolveAliasTypeForCodegen(inst, ctx, depth + 1);
}

std::string StripIntSuffix(std::string text) {
  static const char* suffixes[] = {
      "isize", "usize", "i128", "u128", "i64", "u64", "i32", "u32", "i16", "u16", "i8", "u8"
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

std::optional<std::uint64_t> ParseImmediateU64(const IRValue& value) {
  if (value.kind != IRValue::Kind::Immediate) {
    return std::nullopt;
  }
  if (!value.bytes.empty()) {
    if (value.bytes.size() > sizeof(std::uint64_t)) {
      return std::nullopt;
    }
    std::uint64_t out = 0;
    for (std::uint8_t b : value.bytes) {
      out = (out << 8) | static_cast<std::uint64_t>(b);
    }
    return out;
  }
  std::string text = StripIntSuffix(value.name);
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
    size_t idx = 0;
    std::uint64_t out = std::stoull(text, &idx, 0);
    if (idx != text.size()) {
      return std::nullopt;
    }
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::uint64_t> OptImmediateU64(const std::optional<IRValue>& value) {
  if (!value.has_value()) {
    return std::nullopt;
  }
  return ParseImmediateU64(*value);
}

}  // namespace


// S6.8 PanicCode mapping
std::uint16_t PanicCode(PanicReason reason) {
  SPEC_RULE("PanicCode");
  switch (reason) {
    case PanicReason::ErrorExpr:
      return 0x0001;
    case PanicReason::ErrorStmt:
      return 0x0002;
    case PanicReason::DivZero:
      return 0x0003;
    case PanicReason::Overflow:
      return 0x0004;
    case PanicReason::Shift:
      return 0x0005;
    case PanicReason::Bounds:
      return 0x0006;
    case PanicReason::Cast:
      return 0x0007;
    case PanicReason::NullDeref:
      return 0x0008;
    case PanicReason::ExpiredDeref:
      return 0x0009;
    case PanicReason::InitPanic:
      return 0x000A;
    case PanicReason::ContractPre:
      return 0x000B;
    case PanicReason::ContractPost:
      return 0x000C;
    case PanicReason::AsyncFailed:
      return 0x000D;
    case PanicReason::ForeignPre:
      return 0x000E;
    case PanicReason::ForeignPost:
      return 0x000F;
    case PanicReason::TypeInv:
      return 0x0010;
    case PanicReason::LoopInv:
      return 0x0011;
    case PanicReason::Other:
    default:
      return 0x00FF;
  }
}

std::string PanicReasonString(PanicReason reason) {
  switch (reason) {
    case PanicReason::ErrorExpr:
      return "ErrorExpr";
    case PanicReason::ErrorStmt:
      return "ErrorStmt";
    case PanicReason::DivZero:
      return "DivZero";
    case PanicReason::Overflow:
      return "Overflow";
    case PanicReason::Shift:
      return "Shift";
    case PanicReason::Bounds:
      return "Bounds";
    case PanicReason::Cast:
      return "Cast";
    case PanicReason::NullDeref:
      return "NullDeref";
    case PanicReason::ExpiredDeref:
      return "ExpiredDeref";
    case PanicReason::InitPanic:
      return "InitPanic";
    case PanicReason::ContractPre:
      return "ContractPre";
    case PanicReason::ContractPost:
      return "ContractPost";
    case PanicReason::AsyncFailed:
      return "AsyncFailed";
    case PanicReason::ForeignPre:
      return "ForeignPre";
    case PanicReason::ForeignPost:
      return "ForeignPost";
    case PanicReason::TypeInv:
      return "TypeInv";
    case PanicReason::LoopInv:
      return "LoopInv";
    case PanicReason::Other:
    default:
      return "Other";
  }
}

std::string ContractKindString(ContractKind kind) {
  switch (kind) {
    case ContractKind::Pre:
      return "Pre";
    case ContractKind::Post:
      return "Post";
    case ContractKind::TypeInv:
      return "TypeInv";
    case ContractKind::LoopInv:
      return "LoopInv";
    case ContractKind::ForeignPre:
      return "ForeignPre";
    case ContractKind::ForeignPost:
      return "ForeignPost";
  }
  return "Pre";
}

PanicReason ContractPanicReason(ContractKind kind) {
  switch (kind) {
    case ContractKind::Pre:
      return PanicReason::ContractPre;
    case ContractKind::Post:
      return PanicReason::ContractPost;
    case ContractKind::TypeInv:
      return PanicReason::TypeInv;
    case ContractKind::LoopInv:
      return PanicReason::LoopInv;
    case ContractKind::ForeignPre:
      return PanicReason::ForeignPre;
    case ContractKind::ForeignPost:
      return PanicReason::ForeignPost;
  }
  return PanicReason::ContractPre;
}

std::string SpanSummary(const std::optional<core::Span>& span_opt) {
  if (!span_opt.has_value()) {
    return "unknown";
  }
  const auto& span = *span_opt;
  const std::string file = span.file.empty() ? "-" : span.file;
  return file + ":" + std::to_string(span.start_line) + ":" +
         std::to_string(span.start_col) + "-" + std::to_string(span.end_line) +
         ":" + std::to_string(span.end_col);
}

std::string PredicateSummary(const ast::Expr* predicate) {
  if (!predicate) {
    return "unknown";
  }
  return std::string("span(") +
         SpanSummary(std::optional<core::Span>(predicate->span)) + ")";
}

std::string ContractViolationReason(ContractKind kind,
                                    const ast::Expr* predicate,
                                    const std::optional<core::Span>& site_span) {
  std::string reason = std::string("ContractViolation(") + ContractKindString(kind);
  if (predicate || site_span.has_value()) {
    const auto effective_site_span =
        site_span.has_value() ? site_span : std::optional<core::Span>(predicate->span);
    reason += ",P=" + PredicateSummary(predicate);
    reason += ",s=" + SpanSummary(effective_site_span);
  }
  reason += ")";
  return reason;
}

// S6.11 Check-Index-Ok / Check-Index-Err
bool CheckIndex(std::uint64_t len, std::uint64_t idx) {
  SPEC_RULE("Check-Index-Ok");
  SPEC_RULE("Check-Index-Err");
  return idx < len;
}

// S6.11 Check-Range-Ok / Check-Range-Err
bool CheckRange(std::uint64_t len, const RangeVal& range) {
  SPEC_RULE("Check-Range-Ok");
  SPEC_RULE("Check-Range-Err");
  return SliceBounds(range, len).has_value();
}

// S6.11 SliceBounds computation
std::optional<std::pair<std::uint64_t, std::uint64_t>> SliceBounds(
    const RangeVal& range, std::uint64_t len) {
  std::uint64_t start = 0;
  std::uint64_t end = len;

  switch (range.kind) {
    case ast::RangeKind::Full:
      // Full range: 0..len
      start = 0;
      end = len;
      break;

    case ast::RangeKind::From: {
      // start..
      const auto lo = OptImmediateU64(range.lo);
      if (!lo.has_value()) {
        return std::nullopt;
      }
      start = *lo;
      end = len;
      break;
    }

    case ast::RangeKind::To: {
      // ..end (exclusive)
      const auto hi = OptImmediateU64(range.hi);
      if (!hi.has_value()) {
        return std::nullopt;
      }
      start = 0;
      end = *hi;
      break;
    }

    case ast::RangeKind::ToInclusive: {
      // ..=end (inclusive)
      const auto hi = OptImmediateU64(range.hi);
      if (!hi.has_value()) {
        return std::nullopt;
      }
      start = 0;
      if (*hi >= len) {
        return std::nullopt;
      }
      end = *hi + 1;
      break;
    }

    case ast::RangeKind::Exclusive: {
      // start..end (exclusive)
      const auto lo = OptImmediateU64(range.lo);
      const auto hi = OptImmediateU64(range.hi);
      if (!lo.has_value() || !hi.has_value()) {
        return std::nullopt;
      }
      start = *lo;
      end = *hi;
      break;
    }

    case ast::RangeKind::Inclusive: {
      // start..=end (inclusive)
      const auto lo = OptImmediateU64(range.lo);
      const auto hi = OptImmediateU64(range.hi);
      if (!lo.has_value() || !hi.has_value()) {
        return std::nullopt;
      }
      start = *lo;
      if (*hi == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
      }
      end = *hi + 1;
      break;
    }
  }

  if (start > end || end > len) {
    return std::nullopt;
  }

  return std::make_pair(start, end);
}

// S6.8 PanicSym - runtime panic handler symbol
std::string PanicSym() {
  SPEC_RULE("PanicSym");
  return RuntimePanicSym();
}

static IRPtr LowerInitPanic(const std::string& module_path,
                            LowerCtx& ctx,
                            IRPtr cleanup_ir) {
  IRInitPanicRaise panic_ir;
  panic_ir.module = module_path;
  panic_ir.poison_modules = PoisonSetFor(module_path, ctx);
  panic_ir.cleanup_ir = std::move(cleanup_ir);
  IRPtr trace_ir = EmitRuntimeTrace("InitPanicRaise", ctx);
  IRPtr panic_node = MakeIR(std::move(panic_ir));
  return SeqIR({trace_ir, panic_node});
}

// S6.8 LowerPanic - emit panic IR
IRPtr LowerPanic(PanicReason reason, LowerCtx& ctx) {
  SPEC_RULE("LowerPanic");
  if (ctx.active_static_init_module.has_value()) {
    CleanupPlan cleanup_plan = ActiveStaticInitCleanupPlan(ctx);
    return LowerInitPanic(*ctx.active_static_init_module,
                          ctx,
                          EmitCleanupOnPanic(cleanup_plan, ctx));
  }

  IRLowerPanic panic_ir;
  panic_ir.reason = PanicReasonString(reason);
  CleanupPlan cleanup_plan = ComputeCleanupPlanToFunctionRoot(ctx);
  panic_ir.cleanup_ir = EmitCleanupOnPanic(cleanup_plan, ctx);
  IRPtr trace_ir = EmitRuntimeTrace("LowerPanic", ctx);
  IRPtr panic_node = MakeIR(panic_ir);
  return SeqIR({trace_ir, panic_node});
}

IRPtr LowerContractViolation(ContractKind kind,
                             LowerCtx& ctx,
                             const ast::Expr* predicate,
                             std::optional<core::Span> site_span) {
  SPEC_RULE("LowerPanic");
  if (ctx.active_static_init_module.has_value()) {
    CleanupPlan cleanup_plan = ActiveStaticInitCleanupPlan(ctx);
    return LowerInitPanic(*ctx.active_static_init_module,
                          ctx,
                          EmitCleanupOnPanic(cleanup_plan, ctx));
  }

  IRLowerPanic panic_ir;
  panic_ir.reason = ContractViolationReason(kind, predicate, site_span);
  CleanupPlan cleanup_plan = ComputeCleanupPlanToFunctionRoot(ctx);
  panic_ir.cleanup_ir = EmitCleanupOnPanic(cleanup_plan, ctx);
  IRPtr trace_ir = EmitRuntimeTrace("LowerPanic", ctx);
  IRPtr panic_node = MakeIR(panic_ir);
  return SeqIR({trace_ir, panic_node});
}

// S6.8 ClearPanic - emit IR to clear panic flag
IRPtr ClearPanic(LowerCtx& /*ctx*/) {
  SPEC_RULE("ClearPanic");
  return MakeIR(IRClearPanic{});
}

// S6.8 PanicCheck - emit IR to check panic after call
IRPtr PanicCheck(LowerCtx& ctx) {
  SPEC_RULE("PanicCheck");
  IRPtr trace_ir = EmitRuntimeTrace("PanicCheck", ctx);
  return SeqIR({trace_ir, MakeIR(IRPanicCheck{})});
}

IRPtr PanicFollowup(LowerCtx& ctx) {
  if (ctx.active_static_init_module.has_value()) {
    return InitPanicHandle(*ctx.active_static_init_module, ctx);
  }
  return PanicCheck(ctx);
}

IRPtr CheckPoison(const std::string& module_path, LowerCtx& ctx) {
  SPEC_RULE("CheckPoison-Use");
  IRCheckPoison check;
  check.module = module_path;
  IRPtr trace_ir = EmitRuntimeTrace("CheckPoison", ctx);
  return SeqIR({trace_ir, MakeIR(std::move(check))});
}

// S6.8 InitPanicHandle - module init panic handling
IRPtr InitPanicHandle(const std::string& module_path, LowerCtx& ctx) {
  SPEC_RULE("InitPanicHandle");
  IRInitPanicHandle handle;
  handle.module = module_path;
  handle.poison_modules = PoisonSetFor(module_path, ctx);
  handle.cleanup_ir = EmitCleanupOnPanic(ActiveStaticInitCleanupPlan(ctx), ctx);
  IRPtr trace_ir = EmitRuntimeTrace("InitPanicHandle", ctx);
  IRPtr handle_node = MakeIR(handle);
  return SeqIR({trace_ir, handle_node});
}

// Note: LowerRangeExpr is defined in lower/expr/range.cpp

// S6.11 Lower-Transmute

LowerResult LowerTransmute(analysis::TypeRef from_type,
                           analysis::TypeRef to_type,
                           const ast::Expr& expr,
                           LowerCtx& ctx) {
  SPEC_RULE("Lower-Transmute");
  SPEC_RULE("Lower-Transmute-Err");

  auto expr_result = LowerExpr(expr, ctx);

  if (!from_type && ctx.expr_type) {
    from_type = ctx.expr_type(expr);
  }

  if (!to_type && from_type) {
    to_type = from_type;
  }

  std::optional<std::uint64_t> from_size;
  std::optional<std::uint64_t> to_size;
  if (ctx.sigma && from_type && to_type) {
    analysis::ScopeContext scope;
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
    from_size = ::cursive::analysis::layout::SizeOf(scope, from_type);
    to_size = ::cursive::analysis::layout::SizeOf(scope, to_type);
  }

  if (from_size.has_value() && to_size.has_value() && *from_size != *to_size) {
    IRValue unreachable = ctx.FreshTempValue("unreachable");
    return LowerResult{SeqIR({expr_result.ir, LowerPanic(PanicReason::Cast, ctx)}), unreachable};
  }

  IRValue result = ctx.FreshTempValue("transmute");

  IRTransmute transmute;
  transmute.from = from_type;
  transmute.to = to_type;
  transmute.value = expr_result.value;
  transmute.result = result;

  return LowerResult{SeqIR({expr_result.ir, MakeIR(std::move(transmute))}), result};
}

// S6.11 Lower-RawDeref
LowerResult LowerRawDeref(const IRValue& ptr_value,
                          analysis::TypeRef ptr_type,
                          LowerCtx& ctx) {
  if (!ptr_type) {
    IRValue result = ctx.FreshTempValue("deref");
    return LowerResult{MakeIR(IROpaque{}), result};
  }

  analysis::TypeRef stripped = analysis::StripPerm(ptr_type);
  if (stripped) {
    ptr_type = stripped;
  }
  ptr_type = ResolveAliasTypeForCodegen(ptr_type, ctx);

  // Determine pointer state from type
  auto* ptr = std::get_if<analysis::TypePtr>(&ptr_type->node);
  auto* raw_ptr = std::get_if<analysis::TypeRawPtr>(&ptr_type->node);
  auto* path_ptr = std::get_if<analysis::TypePathType>(&ptr_type->node);

  analysis::TypeRef elem_type;
  if (raw_ptr != nullptr) {
    elem_type = raw_ptr->element;
  } else if (ptr != nullptr) {
    elem_type = ptr->element;
  } else if (path_ptr != nullptr && !path_ptr->generic_args.empty()) {
    elem_type = path_ptr->generic_args.front();
  }

  IRValue result = ctx.FreshTempValue("deref");

  if (raw_ptr != nullptr) {
    // Lower-RawDeref-Raw: raw pointer dereference.
    // Even for raw pointers, null/expired addresses must trap in runtime
    // semantics instead of silently producing UB.
    SPEC_RULE("Lower-RawDeref-Raw");

    std::vector<IRPtr> seq;

    IRCheckOp null_check;
    null_check.op = "nonnull";
    null_check.reason = PanicReasonString(PanicReason::NullDeref);
    null_check.lhs = ptr_value;
    seq.push_back(MakeIR(std::move(null_check)));
    seq.push_back(PanicFollowup(ctx));

    IRCall active_call;
    active_call.callee.kind = IRValue::Kind::Symbol;
    active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
    active_call.args.push_back(ptr_value);
    IRValue active_value = ctx.FreshTempValue("addr_active");
    active_call.result = active_value;
    ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));
    seq.push_back(MakeIR(std::move(active_call)));

    IRCheckOp active_check;
    active_check.op = "addr_active";
    active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
    active_check.lhs = active_value;
    seq.push_back(MakeIR(std::move(active_check)));
    seq.push_back(PanicFollowup(ctx));

    IRReadPtr read;
    read.ptr = ptr_value;
    read.result = result;
    ctx.RegisterValueType(result, elem_type);
    seq.push_back(MakeIR(std::move(read)));

    return LowerResult{SeqIR(std::move(seq)), result};
  }

  if (ptr != nullptr) {
    if (ptr->state.has_value()) {
      switch (*ptr->state) {
        case analysis::PtrState::Valid:
          // Lower-RawDeref-Safe: valid pointer
          SPEC_RULE("Lower-RawDeref-Safe");
          {
            std::vector<IRPtr> seq;

            IRCheckOp null_check;
            null_check.op = "nonnull";
            null_check.reason = PanicReasonString(PanicReason::NullDeref);
            null_check.lhs = ptr_value;
            seq.push_back(MakeIR(std::move(null_check)));
            seq.push_back(PanicFollowup(ctx));

            IRCall active_call;
            active_call.callee.kind = IRValue::Kind::Symbol;
            active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
            active_call.args.push_back(ptr_value);
            IRValue active_value = ctx.FreshTempValue("addr_active");
            active_call.result = active_value;
            ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));
            seq.push_back(MakeIR(std::move(active_call)));

            IRCheckOp active_check;
            active_check.op = "addr_active";
            active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
            active_check.lhs = active_value;
            seq.push_back(MakeIR(std::move(active_check)));
            seq.push_back(PanicFollowup(ctx));

            IRReadPtr read;
            read.ptr = ptr_value;
            read.result = result;
            ctx.RegisterValueType(result, elem_type);
            seq.push_back(MakeIR(std::move(read)));
            return LowerResult{SeqIR(std::move(seq)), result};
          }

        case analysis::PtrState::Null:
          // Lower-RawDeref-Null: emit panic
          SPEC_RULE("Lower-RawDeref-Null");
          {
            result = ctx.FreshTempValue("unreachable");
            return LowerResult{LowerPanic(PanicReason::NullDeref, ctx), result};
          }

        case analysis::PtrState::Expired:
          // Lower-RawDeref-Expired: emit panic
          SPEC_RULE("Lower-RawDeref-Expired");
          {
            result = ctx.FreshTempValue("unreachable");
            return LowerResult{LowerPanic(PanicReason::ExpiredDeref, ctx),
                               result};
          }
      }
    }

    // No state specified: emit runtime null/expired checks before dereference.
    SPEC_RULE("Lower-RawDeref-Safe");
    std::vector<IRPtr> seq;

    IRCheckOp null_check;
    null_check.op = "nonnull";
    null_check.reason = PanicReasonString(PanicReason::NullDeref);
    null_check.lhs = ptr_value;
    seq.push_back(MakeIR(std::move(null_check)));
    seq.push_back(PanicFollowup(ctx));

    IRCall active_call;
    active_call.callee.kind = IRValue::Kind::Symbol;
    active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
    active_call.args.push_back(ptr_value);
    IRValue active_value = ctx.FreshTempValue("addr_active");
    active_call.result = active_value;
    ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));
    seq.push_back(MakeIR(std::move(active_call)));

    IRCheckOp active_check;
    active_check.op = "addr_active";
    active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
    active_check.lhs = active_value;
    seq.push_back(MakeIR(std::move(active_check)));
    seq.push_back(PanicFollowup(ctx));

    IRReadPtr read;
    read.ptr = ptr_value;
    read.result = result;
    ctx.RegisterValueType(result, elem_type);
    seq.push_back(MakeIR(std::move(read)));
    return LowerResult{SeqIR(std::move(seq)), result};
  }

  // Lowered path-shaped pointer forms: treat `Ptr<...>` as safe pointers and
  // `RawPtr<...>` as raw pointers when analysis typing did not normalize.
  if (path_ptr != nullptr && !path_ptr->path.empty()) {
    const std::string& tail = path_ptr->path.back();
    if (tail == "RawPtr") {
      SPEC_RULE("Lower-RawDeref-Raw");
      IRReadPtr read;
      read.ptr = ptr_value;
      read.result = result;
      ctx.RegisterValueType(result, elem_type);
      return LowerResult{MakeIR(read), result};
    }
    if (tail == "Ptr") {
      SPEC_RULE("Lower-RawDeref-Safe");
      std::vector<IRPtr> seq;

      IRCheckOp null_check;
      null_check.op = "nonnull";
      null_check.reason = PanicReasonString(PanicReason::NullDeref);
      null_check.lhs = ptr_value;
      seq.push_back(MakeIR(std::move(null_check)));
      seq.push_back(PanicFollowup(ctx));

      IRCall active_call;
      active_call.callee.kind = IRValue::Kind::Symbol;
      active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
      active_call.args.push_back(ptr_value);
      IRValue active_value = ctx.FreshTempValue("addr_active");
      active_call.result = active_value;
      ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));
      seq.push_back(MakeIR(std::move(active_call)));

      IRCheckOp active_check;
      active_check.op = "addr_active";
      active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
      active_check.lhs = active_value;
      seq.push_back(MakeIR(std::move(active_check)));
      seq.push_back(PanicFollowup(ctx));

      IRReadPtr read;
      read.ptr = ptr_value;
      read.result = result;
      ctx.RegisterValueType(result, elem_type);
      seq.push_back(MakeIR(std::move(read)));
      return LowerResult{SeqIR(std::move(seq)), result};
    }
  }

  // Fallback for other types (shouldn't happen in well-typed code)
  return LowerResult{MakeIR(IROpaque{}), result};
}

// Emit SPEC_RULE anchors for coverage
void AnchorChecksAndPanicRules() {
  // S6.8 Cleanup, Drop, and Unwinding
  SPEC_RULE("PanicCode");
  SPEC_RULE("PanicSym");
  SPEC_RULE("LowerPanic");
  SPEC_RULE("ClearPanic");
  SPEC_RULE("PanicCheck");
  SPEC_RULE("InitPanicHandle");

  // S6.11 Checks and Panic
  SPEC_RULE("Lower-Range-Full");
  SPEC_RULE("Lower-Range-To");
  SPEC_RULE("Lower-Range-ToInclusive");
  SPEC_RULE("Lower-Range-From");
  SPEC_RULE("Lower-Range-Inclusive");
  SPEC_RULE("Lower-Range-Exclusive");
  SPEC_RULE("Check-Index-Ok");
  SPEC_RULE("Check-Index-Err");
  SPEC_RULE("Check-Range-Ok");
  SPEC_RULE("Check-Range-Err");
  SPEC_RULE("Lower-Transmute");
  SPEC_RULE("Lower-Transmute-Err");
  SPEC_RULE("Lower-RawDeref-Safe");
  SPEC_RULE("Lower-RawDeref-Raw");
  SPEC_RULE("Lower-RawDeref-Null");
  SPEC_RULE("Lower-RawDeref-Expired");
}

}  // namespace cursive::codegen


