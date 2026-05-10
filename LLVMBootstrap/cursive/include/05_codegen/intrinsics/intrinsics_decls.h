#pragma once

// =============================================================================
// Intrinsic Declarations
// =============================================================================
//
// This file declares compiler intrinsics - built-in operations that the
// compiler has special knowledge of and handles specially during lowering.
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Contract intrinsics @result, @entry (lines 3357, 23204-23266)
//   - Type intrinsics: sizeof, alignof, transmute, widen
//   - @result: references return value in postcondition
//   - @entry(expr): captures entry/old value of expression
//
// INTRINSIC CATEGORIES:
//   1. Contract intrinsics: @result, @entry
//   2. Type intrinsics: sizeof, alignof
//   3. Memory intrinsics: transmute
//   4. Modal intrinsics: widen
//
// =============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"

namespace cursive::codegen {

// =============================================================================
// Intrinsic Kind Enumeration
// =============================================================================

/// Identifies the kind of compiler intrinsic.
enum class IntrinsicKind {
  // ===========================================================================
  // Contract Intrinsics (§14)
  // ===========================================================================

  /// @result - reference to return value in postcondition.
  /// Only valid in postcondition context (right of =>).
  /// Type: matches procedure return type.
  Result,

  /// @entry(expr) - captures entry/old value of expression.
  /// Only valid in postcondition context.
  /// Requires: expr type satisfies Bitcopy or Clone.
  Entry,

  // ===========================================================================
  // Type Intrinsics
  // ===========================================================================

  /// sizeof(T) - compile-time size of type T in bytes.
  /// Type: usize
  /// Requires: T must have a defined layout.
  SizeOf,

  /// alignof(T) - compile-time alignment of type T in bytes.
  /// Type: usize
  /// Requires: T must have a defined layout.
  AlignOf,

  // ===========================================================================
  // Memory Intrinsics
  // ===========================================================================

  /// transmute<T, U>(value) - reinterpret bit pattern.
  /// Type: T -> U
  /// Requires: sizeof(T) == sizeof(U), unsafe context.
  Transmute,

  // ===========================================================================
  // Modal Intrinsics
  // ===========================================================================

  /// widen(value) - convert modal state to general modal type.
  /// Type: Modal@State -> Modal
  /// Safe but may warn for large copies.
  Widen,

  // ===========================================================================
  // Count
  // ===========================================================================

  /// Number of intrinsic kinds.
  Count,
};

// =============================================================================
// Intrinsic Metadata
// =============================================================================

/// Metadata describing an intrinsic.
struct IntrinsicInfo {
  /// The kind of intrinsic.
  IntrinsicKind kind = IntrinsicKind::Count;

  /// The name of the intrinsic (e.g., "@result", "sizeof").
  std::string_view name;

  /// Whether this intrinsic requires unsafe context.
  bool requires_unsafe = false;

  /// Whether this intrinsic is a contract intrinsic.
  bool is_contract = false;

  /// Whether this intrinsic is compile-time only.
  bool is_comptime = false;

  /// The number of type parameters (for generics).
  std::size_t type_param_count = 0;

  /// The number of value arguments.
  std::size_t arg_count = 0;
};

// =============================================================================
// Intrinsic Lookup
// =============================================================================

/// Look up an intrinsic by name.
/// Returns nullptr if not a known intrinsic.
const IntrinsicInfo* LookupIntrinsic(std::string_view name);

/// Check if a name is a known intrinsic.
bool IsIntrinsic(std::string_view name);

/// Check if an intrinsic is a contract intrinsic (@result, @entry).
bool IsContractIntrinsic(IntrinsicKind kind);

/// Check if an intrinsic requires unsafe context.
bool RequiresUnsafe(IntrinsicKind kind);

/// Check if an intrinsic is compile-time only.
bool IsComptime(IntrinsicKind kind);

// =============================================================================
// Contract Intrinsic Support
// =============================================================================

/// Context for contract evaluation.
enum class ContractContext {
  /// Precondition (left of =>).
  Precondition,
  /// Postcondition (right of =>).
  Postcondition,
  /// Not in a contract.
  None,
};

/// Check if @result is valid in the given context.
/// @result is only valid in postcondition context.
bool ResultValidInContext(ContractContext ctx);

/// Check if @entry is valid in the given context.
/// @entry is only valid in postcondition context.
bool EntryValidInContext(ContractContext ctx);

// =============================================================================
// @entry Support
// =============================================================================

/// Information about an @entry capture.
struct EntryCaptureInfo {
  /// The expression being captured.
  std::string expr_repr;

  /// The type of the captured value.
  analysis::TypeRef type;

  /// Whether the type is Bitcopy (false = Clone).
  bool is_bitcopy = false;

  /// The generated temporary name for the captured value.
  std::string temp_name;
};

// =============================================================================
// Transmute Support
// =============================================================================

/// Check if transmute is valid between two types.
/// Returns true if sizeof(from) == sizeof(to).
bool TransmuteValid(
    const analysis::TypeRef& from,
    const analysis::TypeRef& to,
    std::uint64_t from_size,
    std::uint64_t to_size);

// =============================================================================
// Widen Support
// =============================================================================

/// Check if widen is valid for a modal type.
/// Returns true if the type is a modal state type.
bool WidenValid(const analysis::TypeRef& type);

// =============================================================================
// Spec Rule Anchors
// =============================================================================

/// Emit SPEC_RULE anchors for intrinsic-related rules.
void AnchorIntrinsicRules();

}  // namespace cursive::codegen
