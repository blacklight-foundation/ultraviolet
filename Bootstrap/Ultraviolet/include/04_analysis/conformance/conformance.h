#pragma once

#include <array>
#include <optional>
#include <cstddef>
#include <string_view>
#include <vector>

#include "00_core/behavior_model.h"
#include "00_core/diagnostics.h"
#include "00_core/source_text.h"
#include "02_source/ast/ast.h"
#include "02_source/token.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

struct ScopeContext;  // Forward declaration

struct PhaseOrderResult {
  bool phase1_ok = false;
  bool phase2_ok = false;
  bool phase3_ok = false;
  bool phase4_ok = false;
};

struct Phase3ChecksResult {
  bool resolve_modules_ok = false;
  bool decl_typing_ok = false;
  bool main_check_ok = false;
};

struct ConformanceJudgmentEvidence {
  bool project_bound = false;
  bool parse_modules_ok = false;
  bool execute_comptime_ok = false;
  Phase3ChecksResult phase3_checks;
  bool output_pipeline_ok = false;
};

enum class TranslationPhase {
  Phase1,
  Phase2,
  Phase3,
  Phase4,
};

struct ConformanceInput {
  PhaseOrderResult phase_orders;
  std::optional<ConformanceJudgmentEvidence> evidence;
  std::vector<std::string_view> illformed_subjects;
  std::size_t error_count = 0;
  core::ErrorRecoveryPolicy error_policy = core::DefaultErrorRecoveryPolicy();
  bool outside_conformance = false;
};

bool WF(const PhaseOrderResult& phases);
bool WF(const ConformanceInput& input);

bool UVConforming(const ConformanceInput& input);

bool OutsideConformance(const ConformanceInput& input);

bool IllFormedProgram(const ConformanceInput& input);

bool RejectIllFormed(const ConformanceInput& input);

std::size_t CountErrorLikeDiagnostics(const core::DiagnosticStream& diags);

std::array<TranslationPhase, 4> TranslationPhases();
std::array<bool, 4> ReqJudgments(const PhaseOrderResult& phases);
std::array<bool, 4> ReqJudgments(const ConformanceInput& input);
bool Phase1Order(const ConformanceInput& input);
bool Phase2Order(const ConformanceInput& input);
Phase3ChecksResult Phase3Checks(const ConformanceInput& input);
std::optional<std::size_t> FirstFail(const Phase3ChecksResult& checks);
bool Phase3Order(const ConformanceInput& input);
bool Phase4Order(const ConformanceInput& input);
bool Phase0ChecksDisjointSourceChecks();
bool IllFormed(std::string_view subject);

// =============================================================================
// Type Subset Checking (§5.2.2 Subtyping, §10.3 Permission Coercion)
// =============================================================================

/// Result of a type subset check.
struct TypeSubsetResult {
  bool ok = true;                              // Operation succeeded
  std::optional<std::string_view> diag_id;     // Diagnostic code if error
  bool is_subset = false;                      // True if sub <: super
};

/// Result of a coercion check.
struct CoercionResult {
  bool ok = true;                              // Operation succeeded
  std::optional<std::string_view> diag_id;     // Diagnostic code if error
  bool can_coerce = false;                     // True if implicit coercion allowed
};

/// Result of an explicit cast check.
struct CastResult {
  bool ok = true;                              // Operation succeeded
  std::optional<std::string_view> diag_id;     // Diagnostic code if error
  bool requires_cast = false;                  // True if explicit `as` cast required
  bool cast_valid = false;                     // True if the cast is valid
};

/// Check if sub is a subtype of super (sub <: super).
/// This is the primary subtyping judgment from §5.2.2.
/// Returns is_subset=true if sub <: super.
TypeSubsetResult IsSubsetOf(const ScopeContext& ctx,
                            const TypeRef& sub,
                            const TypeRef& super);

/// Check if implicit coercion from `from` to `to` is allowed.
/// Coercion is allowed when:
/// - from <: to (subtype relation holds), OR
/// - Specific coercion rules apply (e.g., array to slice coercion)
CoercionResult CanCoerceTo(const ScopeContext& ctx,
                           const TypeRef& from,
                           const TypeRef& to);

/// Determine if an explicit `as` cast is required to convert from `from` to `to`.
/// Returns requires_cast=true if no implicit coercion is allowed but cast is valid.
/// Returns cast_valid=true if the explicit cast would be well-formed.
/// See CastValid predicate in §5.3.
CastResult RequiresExplicitCast(const ScopeContext& ctx,
                                const TypeRef& from,
                                const TypeRef& to);

/// Check whether two permission qualifiers match for general subtyping.
/// Permission admissibility at receiver and non-consuming argument sites is
/// separate from general type subtyping.
bool PermissionSubset(Permission sub, Permission super);

/// Check if union_sub is a subset of union_super.
/// Union subset holds when every variant in union_sub is a member of union_super.
/// See §5.2.2 Sub-Union-Width rule.
TypeSubsetResult UnionSubset(const ScopeContext& ctx,
                             const TypeRef& union_sub,
                             const TypeRef& union_super);

// =============================================================================
// Type-System Metatheory Hook Surface (§5.2.18)
// =============================================================================

struct MetatheoryCheckResult {
  bool ok = false;
  bool progress = false;
  bool preservation = false;
  bool no_use_after_free = false;
  bool no_double_free = false;
  bool no_dangling_pointers = false;
  bool permission_preservation = false;
  bool state_determinism = false;
  bool no_resurrection = false;
  bool data_race_freedom = false;
  bool fork_join_guarantee = false;
  bool key_serialization = false;
  bool async_key_safety = false;
};

MetatheoryCheckResult CheckTypeSystemMetatheoryHooks(const ScopeContext& ctx);

}  // namespace ultraviolet::analysis
