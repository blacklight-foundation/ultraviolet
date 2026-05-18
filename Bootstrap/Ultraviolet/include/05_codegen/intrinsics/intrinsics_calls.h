#pragma once

// =============================================================================
// Intrinsic Call Lowering
// =============================================================================
//
// This file provides functions to lower intrinsic calls to IR.
// Intrinsics are compiler-known operations with special lowering behavior.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.4 Expression Lowering (lines 15665-15992)
//   - Lower-Transmute rule (lines 17255-17258)
//   - widen operator lowering (lines 12814-12845)
//   - Contract intrinsics @result, @entry (lines 23204-23266)
//
// =============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/intrinsics/intrinsics_decls.h"

namespace ultraviolet::codegen {

// Forward declarations
struct LowerCtx;
struct LowerResult;

// =============================================================================
// Transmute Lowering
// =============================================================================

/// Result of transmute lowering.
struct TransmuteLowerResult {
  /// The generated IR.
  IRPtr ir;

  /// The result value (transmuted value).
  IRValue value;

  /// Whether the operation was successful.
  bool success = true;

  /// Error message if not successful.
  std::string error;
};

/// Lower a transmute expression.
///
/// Transmute performs bit-level reinterpretation between types of the same size.
/// This is an unsafe operation and must only be called from unsafe context.
///
/// SPEC: §6.11 Lower-Transmute
///   - Verify ::ultraviolet::analysis::layout::SizeOf(from) == ::ultraviolet::analysis::layout::SizeOf(to)
///   - Generate IRTransmute node
///   - No runtime checks (caller verified unsafe context)
///
/// Parameters:
///   - from_type: The source type
///   - to_type: The target type
///   - value: The value to transmute
///   - from_size: Size of source type in bytes
///   - to_size: Size of target type in bytes
///   - ctx: Lowering context
///
/// Returns:
///   TransmuteLowerResult with IR and result value, or error.
TransmuteLowerResult LowerTransmuteIntrinsic(
    const analysis::TypeRef& from_type,
    const analysis::TypeRef& to_type,
    const IRValue& value,
    std::uint64_t from_size,
    std::uint64_t to_size,
    LowerCtx& ctx);

// =============================================================================
// Widen Lowering
// =============================================================================

/// Result of widen lowering.
struct WidenLowerResult {
  /// The generated IR.
  IRPtr ir;

  /// The result value (widened value).
  IRValue value;

  /// Whether the operation was successful.
  bool success = true;

  /// Error message if not successful.
  std::string error;
};

/// Lower a widen expression.
///
/// Widen converts a modal state type to the general modal type.
/// For example: Connection@Open -> Connection
///
/// SPEC: §7.4 Modal Widening
///   - For niche-optimized modals: extract value directly
///   - For tagged modals: construct tagged representation
///
/// Parameters:
///   - state_type: The specific modal state type (e.g., Modal@State)
///   - general_type: The general modal type (e.g., Modal)
///   - value: The value to widen
///   - ctx: Lowering context
///
/// Returns:
///   WidenLowerResult with IR and result value, or error.
WidenLowerResult LowerWidenIntrinsic(
    const analysis::TypeRef& state_type,
    const analysis::TypeRef& general_type,
    const IRValue& value,
    LowerCtx& ctx);

// =============================================================================
// String/Bytes Widening
// =============================================================================

/// Lower a string widening expression.
///
/// Widens string@View or string@Managed to general string type.
/// Packs: discriminant (u8) + ptr + len + cap
///
/// Parameters:
///   - state: The string state (View or Managed)
///   - value: The string value to widen
///   - ctx: Lowering context
///
/// Returns:
///   WidenLowerResult with IR and result value.
WidenLowerResult LowerStringWiden(
    analysis::StringState state,
    const IRValue& value,
    LowerCtx& ctx);

/// Lower a bytes widening expression.
///
/// Widens bytes@View or bytes@Managed to general bytes type.
/// Packs: discriminant (u8) + ptr + len + cap
///
/// Parameters:
///   - state: The bytes state (View or Managed)
///   - value: The bytes value to widen
///   - ctx: Lowering context
///
/// Returns:
///   WidenLowerResult with IR and result value.
WidenLowerResult LowerBytesWiden(
    analysis::BytesState state,
    const IRValue& value,
    LowerCtx& ctx);

// =============================================================================
// SizeOf / AlignOf Lowering
// =============================================================================

/// Lower a sizeof expression.
///
/// Returns the compile-time size of a type in bytes as a usize value.
///
/// Parameters:
///   - type: The type to get the size of
///   - size: The computed size in bytes
///   - ctx: Lowering context
///
/// Returns:
///   LowerResult with IR and immediate usize value.
LowerResult LowerSizeOfIntrinsic(
    const analysis::TypeRef& type,
    std::uint64_t size,
    LowerCtx& ctx);

/// Lower an alignof expression.
///
/// Returns the compile-time alignment of a type in bytes as a usize value.
///
/// Parameters:
///   - type: The type to get the alignment of
///   - align: The computed alignment in bytes
///   - ctx: Lowering context
///
/// Returns:
///   LowerResult with IR and immediate usize value.
LowerResult LowerAlignOfIntrinsic(
    const analysis::TypeRef& type,
    std::uint64_t align,
    LowerCtx& ctx);

// =============================================================================
// Contract Intrinsic Lowering
// =============================================================================

/// Lower an @result intrinsic reference.
///
/// @result references the return value in a postcondition.
/// This is lowered to a read of the result temporary.
///
/// Parameters:
///   - return_type: The procedure's return type
///   - result_temp: The temporary holding the result value
///   - ctx: Lowering context
///
/// Returns:
///   LowerResult with IR and result value.
LowerResult LowerResultIntrinsic(
    const analysis::TypeRef& return_type,
    const std::string& result_temp,
    LowerCtx& ctx);

/// Lower an @entry(expr) intrinsic.
///
/// @entry captures the entry/old value of an expression.
/// The expression is evaluated at procedure entry and stored in a temporary.
/// At the point of @entry evaluation, we read the temporary.
///
/// Parameters:
///   - capture: Information about the @entry capture
///   - ctx: Lowering context
///
/// Returns:
///   LowerResult with IR and captured value.
LowerResult LowerEntryIntrinsic(
    const EntryCaptureInfo& capture,
    LowerCtx& ctx);

// =============================================================================
// Null Pointer Creation
// =============================================================================

/// Create a null pointer of the given type.
///
/// Used internally for initializing optional pointers and Ptr@Null.
///
/// Parameters:
///   - ptr_type: The pointer type (e.g., Ptr<T>@Null, *imm T)
///   - ctx: Lowering context
///
/// Returns:
///   LowerResult with IR and null pointer value.
LowerResult LowerNullPtr(
    const analysis::TypeRef& ptr_type,
    LowerCtx& ctx);

// =============================================================================
// Spec Rule Anchors
// =============================================================================

/// Emit SPEC_RULE anchors for intrinsic lowering rules.
void AnchorIntrinsicLoweringRules();

}  // namespace ultraviolet::codegen
