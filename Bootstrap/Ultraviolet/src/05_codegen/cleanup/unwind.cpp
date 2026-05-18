// =============================================================================
// MIGRATION MAPPING: unwind.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.4 Expression Lowering - Panic and Unwind (lines 15950-15992)
//   - Panic out-parameter handling (lines 15950-15965)
//   - Unwind path generation (lines 15966-15980)
//   - Cleanup on panic (lines 15981-15992)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/cleanup.cpp
//   - Lines 1401-1500: Unwind path generation
//   - Lines 1501-1599: Panic cleanup integration
//
// ADDITIONAL SOURCE: ultraviolet-bootstrap/src/04_codegen/abi/abi_params.cpp
//   - Lines 200-284: PanicRecordType, NeedsPanicOut, PanicOutParams
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/cleanup/unwind.h
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRBranch, IRLabel)
//   - ultraviolet/include/05_codegen/abi/abi_params.h (PanicOutParam)
//   - ultraviolet/include/05_codegen/cleanup/cleanup.h (CleanupPlan)
//
// REFACTORING NOTES:
//   1. Ultraviolet uses out-parameter panic handling, not exceptions
//   2. Procedures that can panic have hidden PanicRecord* parameter
//   3. On panic, set panic record and jump to cleanup
//   4. Cleanup runs in reverse scope order
//   5. After cleanup, propagate panic to caller
//   6. Unwind paths share cleanup code with normal paths where possible
//
// PANIC RECORD STRUCTURE:
//   PanicRecord = {
//     panicked: bool,
//     message: *const u8,
//     message_len: usize,
//     file: *const u8,
//     file_len: usize,
//     line: u32,
//     column: u32
//   }
//
// UNWIND PATH GENERATION:
//   1. At each call site that can panic:
//      - Check panic flag after call
//      - If set, branch to unwind label
//   2. Unwind label:
//      - Execute cleanup for current scope
//      - Jump to next outer unwind label or return
//   3. Entry point catches outermost panic
//
// CLEANUP-ON-PANIC RULES:
//   - Same cleanup as normal scope exit
//   - Must handle partially-constructed values
//   - Must not panic during cleanup (abort if so)
// =============================================================================

#include "05_codegen/cleanup/unwind.h"

#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/lower/lower_expr.h"
#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ultraviolet::codegen {

// ============================================================================
// Section 6.8 Panic Record Access
// ============================================================================
// Note: PanicOutType() is defined in abi_panic.cpp (see abi.h)

PanicAccess BuildPanicAccess(LowerCtx& ctx) {
  SPEC_RULE("BuildPanicAccess");

  IRValue panic_ptr = ctx.FreshTempValue("panic_ptr");
  DerivedValueInfo base_info;
  base_info.kind = DerivedValueInfo::Kind::AddrDeref;
  base_info.base.kind = IRValue::Kind::Local;
  base_info.base.name = std::string(kPanicOutName);
  ctx.RegisterDerivedValue(panic_ptr, base_info);
  ctx.RegisterValueType(panic_ptr, PanicOutType());

  IRValue flag_ptr = ctx.FreshTempValue("panic_flag_ptr");
  DerivedValueInfo flag_info;
  flag_info.kind = DerivedValueInfo::Kind::AddrField;
  flag_info.base = panic_ptr;
  flag_info.field = "panic";
  ctx.RegisterDerivedValue(flag_ptr, flag_info);
  ctx.RegisterValueType(flag_ptr,
                        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut,
                                             analysis::MakeTypePrim("bool")));

  IRValue code_ptr = ctx.FreshTempValue("panic_code_ptr");
  DerivedValueInfo code_info;
  code_info.kind = DerivedValueInfo::Kind::AddrField;
  code_info.base = panic_ptr;
  code_info.field = "code";
  ctx.RegisterDerivedValue(code_ptr, code_info);
  ctx.RegisterValueType(code_ptr,
                        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut,
                                             analysis::MakeTypePrim("u32")));

  return PanicAccess{panic_ptr, flag_ptr, code_ptr};
}

PanicSnapshot ReadPanicRecord(const PanicAccess& access, LowerCtx& ctx) {
  SPEC_RULE("ReadPanicRecord");

  IRValue flag = ctx.FreshTempValue("panic_flag");
  ctx.RegisterValueType(flag, analysis::MakeTypePrim("bool"));
  IRReadPtr read_flag;
  read_flag.ptr = access.flag_ptr;
  read_flag.result = flag;

  IRValue code = ctx.FreshTempValue("panic_code");
  ctx.RegisterValueType(code, analysis::MakeTypePrim("u32"));
  IRReadPtr read_code;
  read_code.ptr = access.code_ptr;
  read_code.result = code;

  IRPtr ir = SeqIR({MakeIR(std::move(read_flag)), MakeIR(std::move(read_code))});
  return PanicSnapshot{ir, flag, code};
}

IRPtr WritePanicRecord(const PanicAccess& access,
                       const IRValue& flag,
                       const IRValue& code) {
  SPEC_RULE("WritePanicRecord");

  IRWritePtr write_flag;
  write_flag.ptr = access.flag_ptr;
  write_flag.value = flag;

  IRWritePtr write_code;
  write_code.ptr = access.code_ptr;
  write_code.value = code;

  return SeqIR({MakeIR(std::move(write_flag)), MakeIR(std::move(write_code))});
}

// ============================================================================
// Section 7.4 Unwind State
// ============================================================================

UnwindContext CreateUnwindContext(bool start_panicking, LowerCtx& ctx) {
  SPEC_RULE("CreateUnwindContext");

  UnwindContext unwind;
  unwind.status = start_panicking ? UnwindStatus::Panic : UnwindStatus::Ok;
  unwind.panicking = BoolImmediate(start_panicking);
  unwind.panic_code = U32Immediate(0);
  unwind.cleanup_depth = 0;

  return unwind;
}

// ============================================================================
// Section 7.4 Double Panic Detection
// ============================================================================

IRPtr EmitDoublePanicCheck(const IRValue& panicking,
                           const IRValue& new_panic_flag,
                           const IRValue& new_panic_code,
                           LowerCtx& ctx) {
  SPEC_RULE("EmitDoublePanicCheck");

  // double_cond = panicking && new_panic_flag
  IRValue double_cond = ctx.FreshTempValue("double_panic");
  IRBinaryOp double_and;
  double_and.op = "&&";
  double_and.lhs = panicking;
  double_and.rhs = new_panic_flag;
  double_and.result = double_cond;

  // If double panic, call panic (abort)
  IRCall panic_call;
  panic_call.callee.kind = IRValue::Kind::Symbol;
  panic_call.callee.name = RuntimePanicSym();
  panic_call.args.push_back(new_panic_code);
  panic_call.result = ctx.FreshTempValue("panic_abort");

  IRIf if_double;
  if_double.cond = double_cond;
  if_double.then_ir = MakeIR(std::move(panic_call));
  if_double.then_value = UnitValue();
  if_double.else_ir = EmptyIR();
  if_double.else_value = UnitValue();
  if_double.result = ctx.FreshTempValue("double_panic_if");

  return SeqIR({MakeIR(std::move(double_and)), MakeIR(std::move(if_double))});
}

// ============================================================================
// Section 7.4 Cleanup Loop Emission
// ============================================================================

IRPtr EmitCleanupActionWithUnwind(IRPtr action_ir,
                                  UnwindContext& unwind,
                                  LowerCtx& ctx) {
  SPEC_RULE("EmitCleanupActionWithUnwind");

  if (!action_ir || IsNoopIR(action_ir)) {
    return EmptyIR();
  }

  const PanicAccess access = BuildPanicAccess(ctx);

  std::vector<IRPtr> parts;

  // 1. Clear panic before the action
  parts.push_back(MakeIR(IRClearPanic{}));

  // 2. Execute the action
  parts.push_back(action_ir);

  // 3. Read panic state after action
  auto snapshot = ReadPanicRecord(access, ctx);
  parts.push_back(snapshot.ir);
  IRValue flag = snapshot.flag;
  IRValue code = snapshot.code;

  // 4. Check for double panic
  parts.push_back(EmitDoublePanicCheck(unwind.panicking, flag, code, ctx));

  // 5. Update panicking state: panicking = panicking || flag
  IRValue new_panicking = ctx.FreshTempValue("panicking");
  IRBinaryOp or_op;
  or_op.op = "||";
  or_op.lhs = unwind.panicking;
  or_op.rhs = flag;
  or_op.result = new_panicking;
  parts.push_back(MakeIR(std::move(or_op)));

  // 6. Update panic code if new panic: code_sel = flag ? code : panic_code
  IRValue new_code = ctx.FreshTempValue("panic_code_sel");
  IRIf code_if;
  code_if.cond = flag;
  code_if.then_ir = EmptyIR();
  code_if.then_value = code;
  code_if.else_ir = EmptyIR();
  code_if.else_value = unwind.panic_code;
  code_if.result = new_code;
  parts.push_back(MakeIR(std::move(code_if)));

  // Update context
  unwind.panicking = new_panicking;
  unwind.panic_code = new_code;

  return SeqIR(std::move(parts));
}

// ============================================================================
// Section 2.7 FFI Boundary Unwinding
// ============================================================================

UnwindMode GetUnwindMode(const ast::AttributeList& attrs) {
  SPEC_RULE("GetUnwindMode");

  for (const auto& attr : attrs) {
    if (attr.name != analysis::attrs::kUnwind) {
      continue;
    }

    if (attr.args.size() != 1 || attr.args.front().key.has_value()) {
      return UnwindMode::Abort;
    }
    auto mode_tok = ast::get_attr_token_arg(attr, 0);
    if (!mode_tok.has_value() ||
        mode_tok->kind != lexer::TokenKind::StringLiteral) {
      return UnwindMode::Abort;
    }

    std::string mode = mode_tok->lexeme;
    if (mode.size() >= 2 &&
        ((mode.front() == '"' && mode.back() == '"') ||
         (mode.front() == '\'' && mode.back() == '\''))) {
      mode = mode.substr(1, mode.size() - 2);
    }

    if (mode == "catch") {
      return UnwindMode::Catch;
    }
    return UnwindMode::Abort;
  }

  return UnwindMode::Abort;
}

static IRValue ZeroValueForType(const analysis::TypeRef& return_type) {
  if (!return_type) {
    return UnitValue();
  }

  analysis::TypeRef cur = return_type;
  while (cur) {
    if (const auto* perm = std::get_if<analysis::TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<analysis::TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }

  if (!cur) {
    return UnitValue();
  }

  if (const auto* prim = std::get_if<analysis::TypePrim>(&cur->node)) {
    if (prim->name == "()") {
      return UnitValue();
    }

    std::size_t byte_count = 0;
    if (prim->name == "bool" || prim->name == "i8" || prim->name == "u8") {
      byte_count = 1;
    } else if (prim->name == "i16" || prim->name == "u16" ||
               prim->name == "f16") {
      byte_count = 2;
    } else if (prim->name == "i32" || prim->name == "u32" ||
               prim->name == "f32" || prim->name == "char") {
      byte_count = 4;
    } else if (prim->name == "i64" || prim->name == "u64" ||
               prim->name == "isize" || prim->name == "usize" ||
               prim->name == "f64") {
      byte_count = 8;
    } else if (prim->name == "i128" || prim->name == "u128") {
      byte_count = 16;
    }

    if (byte_count > 0) {
      IRValue zero;
      zero.kind = IRValue::Kind::Immediate;
      zero.name = "0";
      zero.bytes.assign(byte_count, 0u);
      return zero;
    }
  }

  if (std::holds_alternative<analysis::TypePtr>(cur->node) ||
      std::holds_alternative<analysis::TypeRawPtr>(cur->node) ||
      std::holds_alternative<analysis::TypeFunc>(cur->node) ||
      std::holds_alternative<analysis::TypeClosure>(cur->node)) {
    IRValue zero;
    zero.kind = IRValue::Kind::Immediate;
    zero.name = "0";
    zero.bytes.assign(8, 0u);
    return zero;
  }

  IRValue zero;
  zero.kind = IRValue::Kind::Opaque;
  zero.name = "ffi_zero";
  return zero;
}

IRPtr EmitFFIBoundaryCheck(UnwindMode mode,
                           const IRValue& return_value,
                           analysis::TypeRef return_type,
                           LowerCtx& ctx) {
  SPEC_RULE("EmitFFIBoundaryCheck");

  if (mode == UnwindMode::Abort) {
    // For abort mode, no special handling at boundary
    // Panic would already have aborted
    return EmptyIR();
  }

  // For catch mode, convert panic to the return type's zero-value indicator.

  const PanicAccess access = BuildPanicAccess(ctx);
  auto snapshot = ReadPanicRecord(access, ctx);
  IRValue caught_zero = ZeroValueForType(return_type);
  IRValue clear_flag;
  clear_flag.kind = IRValue::Kind::Immediate;
  clear_flag.name = "false";
  clear_flag.bytes = {0u};

  IRIf if_panic;
  if_panic.cond = snapshot.flag;
  if_panic.then_ir = WritePanicRecord(access, clear_flag, U32Immediate(0));
  if_panic.then_value = caught_zero;
  if_panic.else_ir = EmptyIR();
  if_panic.else_value = return_value;
  if_panic.result = ctx.FreshTempValue("ffi_boundary_result");
  if (return_type) {
    ctx.RegisterValueType(if_panic.result, return_type);
  }

  return SeqIR({snapshot.ir, MakeIR(std::move(if_panic))});
}

// ============================================================================
// Section 7.4 Unwind Step Emission
// ============================================================================

IRPtr EmitUnwindStep(IRPtr cleanup_ir, LowerCtx& ctx) {
  SPEC_RULE("EmitUnwindStep");

  // Execute cleanup, then check if we should continue unwinding
  // or if a new panic occurred (which would abort)

  if (!cleanup_ir || IsNoopIR(cleanup_ir)) {
    return EmptyIR();
  }

  // The cleanup_ir already handles panic checking internally
  return cleanup_ir;
}

IRPtr EmitUnwindAbort(const IRValue& panic_code, LowerCtx& ctx) {
  SPEC_RULE("EmitUnwindAbort");

  IRCall panic_call;
  panic_call.callee.kind = IRValue::Kind::Symbol;
  panic_call.callee.name = RuntimePanicSym();
  panic_call.args.push_back(panic_code);
  panic_call.result = ctx.FreshTempValue("abort_result");

  return MakeIR(std::move(panic_call));
}

// ============================================================================
// Section 6.8 Panic Propagation Helpers
// ============================================================================

bool NeedsPanicOut(const std::string& symbol) {
  SPEC_RULE("NeedsPanicOut");
  // Delegate to the canonical ABI predicate to keep a single source of truth.
  return ::ultraviolet::codegen::NeedsPanicOut(std::string_view(symbol));
}

void RegisterPanicOutProcedure(const std::string& symbol, LowerCtx& ctx) {
  SPEC_RULE("RegisterPanicOutProcedure");

  // This would register the symbol as needing panic out
  // For now, we rely on the NeedsPanicOut check
  (void)symbol;
  (void)ctx;
}

IRPtr EmitPanicPropagate(LowerCtx& ctx) {
  SPEC_RULE("EmitPanicPropagate");

  // After cleanup completes, if we were panicking, we just return
  // The panic state is already set in the out parameter

  // Read the panic state
  const PanicAccess access = BuildPanicAccess(ctx);
  auto snapshot = ReadPanicRecord(access, ctx);

  // Nothing else to do - caller will check the panic state
  return snapshot.ir;
}

// ============================================================================
// Helper: Create IR values for panic state
// ============================================================================

IRValue BoolImmediate(bool value) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = value ? "true" : "false";
  v.bytes = {static_cast<std::uint8_t>(value ? 1 : 0)};
  return v;
}

IRValue U32Immediate(std::uint32_t value) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = std::to_string(value);
  v.bytes = {
      static_cast<std::uint8_t>(value & 0xFFu),
      static_cast<std::uint8_t>((value >> 8) & 0xFFu),
      static_cast<std::uint8_t>((value >> 16) & 0xFFu),
      static_cast<std::uint8_t>((value >> 24) & 0xFFu),
  };
  return v;
}

IRValue USizeImmediate(std::size_t value) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = std::to_string(value);
  const std::uint64_t encoded = static_cast<std::uint64_t>(value);
  v.bytes = {
      static_cast<std::uint8_t>(encoded & 0xFFu),
      static_cast<std::uint8_t>((encoded >> 8) & 0xFFu),
      static_cast<std::uint8_t>((encoded >> 16) & 0xFFu),
      static_cast<std::uint8_t>((encoded >> 24) & 0xFFu),
      static_cast<std::uint8_t>((encoded >> 32) & 0xFFu),
      static_cast<std::uint8_t>((encoded >> 40) & 0xFFu),
      static_cast<std::uint8_t>((encoded >> 48) & 0xFFu),
      static_cast<std::uint8_t>((encoded >> 56) & 0xFFu),
  };
  return v;
}

IRValue UnitValue() {
  IRValue v;
  v.kind = IRValue::Kind::Opaque;
  v.name = "unit";
  return v;
}

bool IsNoopIR(const IRPtr& ir) {
  return !ir || std::holds_alternative<IROpaque>(ir->node);
}

// ============================================================================
// Anchor function for SPEC_RULE markers
// ============================================================================

void AnchorUnwindRules() {
  SPEC_RULE("PanicOutType");
  SPEC_RULE("BuildPanicAccess");
  SPEC_RULE("ReadPanicRecord");
  SPEC_RULE("WritePanicRecord");
  SPEC_RULE("CreateUnwindContext");
  SPEC_RULE("EmitDoublePanicCheck");
  SPEC_RULE("EmitCleanupActionWithUnwind");
  SPEC_RULE("GetUnwindMode");
  SPEC_RULE("EmitFFIBoundaryCheck");
  SPEC_RULE("EmitUnwindStep");
  SPEC_RULE("EmitUnwindAbort");
  SPEC_RULE("NeedsPanicOut");
  SPEC_RULE("RegisterPanicOutProcedure");
  SPEC_RULE("EmitPanicPropagate");
}

}  // namespace ultraviolet::codegen
