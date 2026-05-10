// =============================================================================
// Intrinsic Call Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.4 Expression Lowering (lines 15665-15992)
//   - Lower-Transmute rule (lines 17255-17258)
//   - widen operator lowering (lines 12814-12845)
//   - Contract intrinsics @result, @entry (lines 23204-23266)
//
// =============================================================================

#include "05_codegen/intrinsics/intrinsics_calls.h"

#include <algorithm>
#include <vector>

#include "00_core/assert_spec.h"
#include "05_codegen/ir/ir_builder.h"

namespace cursive::codegen {

// LowerResult is defined in lower_expr.h, but to avoid circular includes,
// we use a local definition that matches the expected interface.
// This must be kept in sync with the definition in lower_expr.h.
struct LowerResult {
  IRPtr ir;
  IRValue value;
};

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

/// Create little-endian bytes for a 64-bit value.
std::vector<std::uint8_t> LEBytesU64(std::uint64_t value, std::size_t n) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
  }
  return bytes;
}

/// Platform pointer size in bytes.
constexpr std::size_t kPtrSize = 8;

/// Create a usize immediate value.
IRValue USizeImmediate(std::uint64_t value) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = std::to_string(value);
  v.bytes = LEBytesU64(value, kPtrSize);
  return v;
}

/// Generate a fresh temporary value name.
std::uint64_t g_temp_counter = 0;

IRValue FreshTemp(std::string_view prefix) {
  IRValue v;
  v.kind = IRValue::Kind::Local;
  v.name = std::string(prefix) + "_" + std::to_string(++g_temp_counter);
  return v;
}

}  // namespace

// =============================================================================
// Transmute Lowering Implementation
// =============================================================================

TransmuteLowerResult LowerTransmuteIntrinsic(
    const analysis::TypeRef& from_type,
    const analysis::TypeRef& to_type,
    const IRValue& value,
    std::uint64_t from_size,
    std::uint64_t to_size,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-Transmute");

  TransmuteLowerResult result;

  // Verify size compatibility
  if (from_size != to_size) {
    SPEC_RULE("Lower-Transmute-Err");
    result.success = false;
    result.error = "transmute requires source and target types to have the same size";
    return result;
  }

  // Generate the transmute IR
  IRValue transmuted = FreshTemp("transmute");

  IRTransmute trans;
  trans.from = from_type;
  trans.to = to_type;
  trans.value = value;
  trans.result = transmuted;

  result.ir = MakeIR(std::move(trans));
  result.value = transmuted;
  result.success = true;
  return result;
}

// =============================================================================
// Widen Lowering Implementation
// =============================================================================

WidenLowerResult LowerWidenIntrinsic(
    const analysis::TypeRef& state_type,
    const analysis::TypeRef& general_type,
    const IRValue& value,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-Widen-Modal");

  WidenLowerResult result;

  // Validate that we have a modal state type
  if (!state_type) {
    result.success = false;
    result.error = "widen requires a modal state type";
    return result;
  }

  const auto* modal_state = std::get_if<analysis::TypeModalState>(&state_type->node);
  if (!modal_state) {
    result.success = false;
    result.error = "widen requires a modal state type (e.g., Modal@State)";
    return result;
  }

  // Generate the widened result
  // The IR for widen is handled by the general unary operator lowering
  // Here we just create an opaque IR with the value
  IRValue widened = FreshTemp("widened");

  // Create a unary operation IR for widen
  IRUnaryOp op;
  op.op = "widen";
  op.operand = value;
  op.result = widened;
  op.operand_type = state_type;
  op.result_type = general_type;

  result.ir = MakeIR(std::move(op));
  result.value = widened;
  result.success = true;

  // Register the result type as the general modal type
  // (This would normally be done by ctx, but we don't have access here)

  return result;
}

WidenLowerResult LowerStringWiden(
    analysis::StringState state,
    const IRValue& value,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-Widen-String");

  WidenLowerResult result;
  IRValue widened = FreshTemp("string_widened");

  // String widening packs: discriminant + ptr + len + cap
  // For View: cap = 0
  // For Managed: cap from the value

  IRUnaryOp op;
  op.op = (state == analysis::StringState::View) ? "widen_string_view" : "widen_string_managed";
  op.operand = value;
  op.result = widened;

  result.ir = MakeIR(std::move(op));
  result.value = widened;
  result.success = true;
  return result;
}

WidenLowerResult LowerBytesWiden(
    analysis::BytesState state,
    const IRValue& value,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-Widen-Bytes");

  WidenLowerResult result;
  IRValue widened = FreshTemp("bytes_widened");

  // Bytes widening packs: discriminant + ptr + len + cap
  // For View: cap = 0
  // For Managed: cap from the value

  IRUnaryOp op;
  op.op = (state == analysis::BytesState::View) ? "widen_bytes_view" : "widen_bytes_managed";
  op.operand = value;
  op.result = widened;

  result.ir = MakeIR(std::move(op));
  result.value = widened;
  result.success = true;
  return result;
}

// =============================================================================
// SizeOf / AlignOf Lowering Implementation
// =============================================================================

LowerResult LowerSizeOfIntrinsic(
    [[maybe_unused]] const analysis::TypeRef& type,
    std::uint64_t size,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-SizeOf");

  // sizeof is compile-time, so we generate an immediate value
  LowerResult result;
  result.value = USizeImmediate(size);
  result.ir = EmptyIR();
  return result;
}

LowerResult LowerAlignOfIntrinsic(
    [[maybe_unused]] const analysis::TypeRef& type,
    std::uint64_t align,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-AlignOf");

  // alignof is compile-time, so we generate an immediate value
  LowerResult result;
  result.value = USizeImmediate(align);
  result.ir = EmptyIR();
  return result;
}

// =============================================================================
// Contract Intrinsic Lowering Implementation
// =============================================================================

LowerResult LowerResultIntrinsic(
    [[maybe_unused]] const analysis::TypeRef& return_type,
    const std::string& result_temp,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-Result");

  // @result is a reference to the return value temporary
  // We generate a read of the result binding
  LowerResult result;

  IRReadVar read;
  read.name = result_temp;

  result.ir = MakeIR(std::move(read));
  result.value = MakeLocalValue(result_temp);
  return result;
}

LowerResult LowerEntryIntrinsic(
    const EntryCaptureInfo& capture,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-Entry");

  // @entry(expr) captures the value of expr at procedure entry
  // The capture has already been stored in a temporary at entry
  // We just read the temporary here
  LowerResult result;

  IRReadVar read;
  read.name = capture.temp_name;

  result.ir = MakeIR(std::move(read));
  result.value = MakeLocalValue(capture.temp_name);
  return result;
}

// =============================================================================
// Null Pointer Creation Implementation
// =============================================================================

LowerResult LowerNullPtr(
    const analysis::TypeRef& ptr_type,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("Lower-NullPtr");

  // Create a null pointer by transmuting 0usize to the pointer type
  LowerResult result;

  IRValue zero = USizeImmediate(0);
  IRValue null_ptr = FreshTemp("null_ptr");

  IRTransmute trans;
  trans.from = analysis::MakeTypePrim("usize");
  trans.to = ptr_type;
  trans.value = zero;
  trans.result = null_ptr;

  result.ir = MakeIR(std::move(trans));
  result.value = null_ptr;
  return result;
}

// =============================================================================
// Spec Rule Anchors
// =============================================================================

void AnchorIntrinsicLoweringRules() {
  // Transmute
  SPEC_RULE("Lower-Transmute");
  SPEC_RULE("Lower-Transmute-Err");
  SPEC_RULE("LowerIR-Transmute");

  // Widen
  SPEC_RULE("Lower-Widen-Modal");
  SPEC_RULE("Lower-Widen-String");
  SPEC_RULE("Lower-Widen-Bytes");

  // SizeOf/AlignOf
  SPEC_RULE("Lower-SizeOf");
  SPEC_RULE("Lower-AlignOf");

  // Contract intrinsics
  SPEC_RULE("Lower-Result");
  SPEC_RULE("Lower-Entry");

  // Null pointer
  SPEC_RULE("Lower-NullPtr");
}

}  // namespace cursive::codegen
