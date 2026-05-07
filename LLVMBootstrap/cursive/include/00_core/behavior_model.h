#pragma once

// =============================================================================
// BEHAVIOR MODEL
// =============================================================================
//
// Cursive's behavior classification model. Cursive does NOT have traditional
// "undefined behavior" (UB) as found in C/C++. Instead, Cursive uses:
//
//   - Specified: Normal, well-defined behavior
//   - UVB (Unverifiable Behavior): Operations whose runtime correctness depends
//     on properties external to the language's semantic model. UVB is permitted
//     only within `unsafe` blocks and FFI calls.
//
// See Cursive specification:
//   - Section 1.2 "Behavior Types" (StaticUndefined, CheckKind, RuntimeBehavior)
//   - Section 1.4.4 "Unverifiable Behavior (UVB)"
//
// =============================================================================

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/diagnostic_codes.h"

namespace cursive::core {

// BehaviorClass classifies runtime behavior of operations.
//
// Specified: The operation has well-defined semantics per the spec.
// UVB: Unverifiable Behavior - correctness depends on programmer-asserted
//      invariants (only permitted in unsafe blocks and FFI).
enum class BehaviorClass {
  Specified,
  UVB,
};

// Runtime checks in §1.2 panic when violated.
enum class RuntimeBehavior {
  Panic,
};

// §1.2 check taxonomy.
enum class CheckKind {
  PatternExhaustiveness,
  TypeCompatibility,
  PermissionViolations,
  ProvenanceEscape,
  ArrayBounds,
  SafePointerValidity,
  IntegerOverflow,
  SliceBounds,
  IntDivisionByZero,
};

// §1.2 recovery policy: MaxErrorCount ∈ ℕ ∪ {∞}.
struct ErrorRecoveryPolicy {
  // nullopt represents ∞.
  std::optional<std::size_t> max_error_count = 100;
};

constexpr std::size_t kSuggestedMaxErrorCount = 100;

// RawPtrPermission indicates the permission of a raw pointer for UVB checks.
enum class RawPtrPermission {
  Imm,  // Immutable raw pointer (*imm T)
  Mut,  // Mutable raw pointer (*mut T)
};

// Metadata entry for a static rule in §1.2 StaticRuleSet.
struct StaticRuleMeta {
  std::string_view rule_id;
  std::string_view conclusion_family;
  std::optional<std::string_view> diag_id;
  std::string_view source_path;
  std::optional<std::string_view> premises_text;
  bool has_bottom_premise;
};

ErrorRecoveryPolicy DefaultErrorRecoveryPolicy();

bool IsStaticCheck(CheckKind kind);
bool IsRuntimeCheck(CheckKind kind);
std::optional<RuntimeBehavior> RuntimeBehaviorOf(CheckKind kind);

bool AbortOnErrorCount(const ErrorRecoveryPolicy& policy,
                       std::size_t error_count);

// §1.2 StaticJudgSet membership over judgment-family names.
bool IsStaticJudgmentFamily(std::string_view judgment_family);
bool IsStaticRule(std::string_view rule_id);
std::optional<StaticRuleMeta> LookupStaticRule(std::string_view rule_id);
std::optional<std::string_view> ConclusionOfRule(std::string_view rule_id);
std::optional<std::string_view> ConclusionFamilyOfRule(std::string_view rule_id);
std::optional<std::vector<std::string_view>> PremisesOfRule(
    std::string_view rule_id);
std::optional<std::string_view> DiagIdOfJudgment(
    std::string_view judgment_family);
std::optional<std::string_view> DiagIdOfRule(std::string_view rule_id);
bool StaticUndefined(std::string_view judgment_family);

// Returns the behavior class for a dynamically undefined condition.
// If dynamic_undefined is true, returns UVB; otherwise Specified.
//
// This implements the Dynamic-Undefined-UVB rule from spec section 1.2.
BehaviorClass BehaviorOfDynamicUndefined(bool dynamic_undefined);

// Check if reading from a raw pointer would be dynamically undefined (UVB).
// Returns true if the read address is undefined (UVB regardless of permission).
//
// The perm parameter is accepted for API symmetry with DynamicUndefinedWritePtr
// but is semantically unused: reading from an undefined address is UVB for both
// imm and mut pointers.
bool DynamicUndefinedReadPtr(RawPtrPermission perm, bool read_addr_defined);

// Check if writing through a raw pointer would be dynamically undefined (UVB).
// Returns true if perm is Imm (writing through immutable pointer is UVB).
bool DynamicUndefinedWritePtr(RawPtrPermission perm);

// Implements the Static-Undefined and Static-Undefined-NoCode rules.
// Returns the diagnostic code if one is defined for the given judgment ID,
// or nullopt if no code is defined.
//
// From spec section 1.2:
//   (Static-Undefined)
//   StaticUndefined(J)    Code(DiagIdOf(J)) = c
//   ───────────────────────────────────────
//   Γ ⊢ J ⇑ c
//
//   (Static-Undefined-NoCode)
//   StaticUndefined(J)    Code(DiagIdOf(J)) = ⊥
//   ────────────────────────────────────────
//   Γ ⊢ J ⇑
std::optional<DiagCode> StaticUndefinedCode(const DiagCodeMap& spec_map,
                                            const DiagCodeMap& c0_map,
                                            const DiagId& id);

// Convenience helper for StaticUndefined lookup using RuleId -> DiagId.
std::optional<DiagCode> StaticUndefinedCodeForRule(const DiagCodeMap& spec_map,
                                                   const DiagCodeMap& c0_map,
                                                   std::string_view rule_id);

}  // namespace cursive::core
