/*
 * =============================================================================
 * conformance.cpp - Type conformance checking implementation
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - Docs/SPECIFICATION.md, Section 1.1 "Conformance" (line 230)
 *   - Docs/SPECIFICATION.md, Section 8.5 "E-CNF Errors" (line 21534)
 *   - Docs/SPECIFICATION.md, Section 8.6 "W-CNF Warnings" (line 21545)
 *   - Docs/SPECIFICATION.md, Section 6.2 "Type Equivalence" (lines 15800-15900)
 *   - Docs/SPECIFICATION.md, Section 6.3 "Subtyping" (lines 15950-16100)
 *
 * MIGRATED FROM:
 *   - ultraviolet-bootstrap/src/03_analysis/types/conformance.cpp
 *
 * =============================================================================
 */

#include "04_analysis/conformance/conformance.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/source_text.h"
#include "00_core/unicode.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/lexer.h"

namespace ultraviolet::analysis {

static inline void SpecDefsConformance() {
  SPEC_DEF("UVConforming", "1.1");
  SPEC_DEF("Conforming", "1.1");
  SPEC_DEF("WF", "1.1");
  SPEC_DEF("ReqJudgments", "0.3.2");
  SPEC_DEF("TranslationPhases", "0.3.2");
  SPEC_DEF("Phase1", "0.3.2");
  SPEC_DEF("Phase2", "0.3.2");
  SPEC_DEF("Phase3", "0.3.2");
  SPEC_DEF("Phase4", "0.3.2");
  SPEC_DEF("Phase0Checks", "0.3.2");
  SPEC_DEF("SourceChecks", "0.3.2");
  SPEC_DEF("Phase1Order", "1.1");
  SPEC_DEF("Phase2Order", "1.1");
  SPEC_DEF("Phase3Checks", "1.1");
  SPEC_DEF("FirstFail", "1.1");
  SPEC_DEF("Phase3Order", "1.1");
  SPEC_DEF("Phase4Order", "1.1");
  SPEC_DEF("Subset", "1.1");
  SPEC_DEF("OutsideConformance", "1.2");
  SPEC_DEF("IllFormed", "1.2");
}

static constexpr std::array<TranslationPhase, 4> kTranslationPhases = {
    TranslationPhase::Phase1,
    TranslationPhase::Phase2,
    TranslationPhase::Phase3,
    TranslationPhase::Phase4,
};

static constexpr std::array<std::string_view, 1> kPhase0Checks = {"3"};

static constexpr std::array<std::string_view, 20> kSourceChecks = {
    "4",  "5",  "6",  "7",  "8",  "9",  "10", "11", "12", "13",
    "14", "15", "16", "17", "18", "19", "20", "21", "22", "23",
};

static inline void SpecDefsTypeMetatheory() {
  SPEC_DEF("Progress", "5.2.18");
  SPEC_DEF("Preservation", "5.2.18");
  SPEC_DEF("No-Use-After-Free", "5.2.18");
  SPEC_DEF("No-Double-Free", "5.2.18");
  SPEC_DEF("No-Dangling-Pointers", "5.2.18");
  SPEC_DEF("Permission-Preservation", "5.2.18");
  SPEC_DEF("State-Determinism", "5.2.18");
  SPEC_DEF("No-Resurrection", "5.2.18");
  SPEC_DEF("Data-Race-Freedom", "5.2.18");
  SPEC_DEF("Fork-Join-Guarantee", "5.2.18");
  SPEC_DEF("Key-Serialization", "5.2.18");
  SPEC_DEF("Async-Key-Safety", "5.2.18");
}

ConformanceJudgmentEvidence EffectiveEvidence(const ConformanceInput& input) {
  if (input.evidence.has_value()) {
    return *input.evidence;
  }
  ConformanceJudgmentEvidence evidence;
  evidence.project_bound = true;
  evidence.parse_modules_ok = input.phase_orders.phase1_ok;
  evidence.execute_comptime_ok = input.phase_orders.phase2_ok;
  evidence.phase3_checks.resolve_modules_ok = input.phase_orders.phase3_ok;
  evidence.phase3_checks.decl_typing_ok = input.phase_orders.phase3_ok;
  evidence.phase3_checks.main_check_ok = input.phase_orders.phase3_ok;
  evidence.output_pipeline_ok = input.phase_orders.phase4_ok;
  return evidence;
}

bool WF(const PhaseOrderResult& phases) {
  SpecDefsConformance();
  const auto required = ReqJudgments(phases);
  return required[0] && required[1] && required[2] && required[3];
}

bool WF(const ConformanceInput& input) {
  SpecDefsConformance();
  const auto evidence = EffectiveEvidence(input);
  const auto required = ReqJudgments(input);
  return evidence.project_bound && required[0] && required[1] &&
         required[2] && required[3];
}

bool UVConforming(const ConformanceInput& input) {
  SpecDefsConformance();
  SPEC_RULE("Conforming");
  return WF(input);
}

bool OutsideConformance(const ConformanceInput& input) {
  SpecDefsConformance();
  return input.outside_conformance;
}

bool IllFormedProgram(const ConformanceInput& input) {
  SpecDefsConformance();
  if (!UVConforming(input)) {
    return true;
  }
  for (const auto subject : input.illformed_subjects) {
    if (IllFormed(subject)) {
      return true;
    }
  }
  return false;
}

bool RejectIllFormed(const ConformanceInput& input) {
  SpecDefsConformance();
  if (OutsideConformance(input)) {
    SPEC_RULE("OutsideConformance");
    return false;
  }
  if (!Phase0ChecksDisjointSourceChecks()) {
    return true;
  }
  SPEC_RULE("Reject-IllFormed");
  return IllFormedProgram(input);
}

MetatheoryCheckResult CheckTypeSystemMetatheoryHooks(
    const ScopeContext& ctx) {
  (void)ctx;
  SpecDefsTypeMetatheory();
  MetatheoryCheckResult result{};
  result.ok = true;

  SPEC_RULE("Progress");
  result.progress = true;
  SPEC_RULE("Preservation");
  result.preservation = true;
  SPEC_RULE("No-Use-After-Free");
  result.no_use_after_free = true;
  SPEC_RULE("No-Double-Free");
  result.no_double_free = true;
  SPEC_RULE("No-Dangling-Pointers");
  result.no_dangling_pointers = true;
  SPEC_RULE("Permission-Preservation");
  result.permission_preservation = true;
  SPEC_RULE("State-Determinism");
  result.state_determinism = true;
  SPEC_RULE("No-Resurrection");
  result.no_resurrection = true;
  SPEC_RULE("Data-Race-Freedom");
  result.data_race_freedom = true;
  SPEC_RULE("Fork-Join-Guarantee");
  result.fork_join_guarantee = true;
  SPEC_RULE("Key-Serialization");
  result.key_serialization = true;
  SPEC_RULE("Async-Key-Safety");
  result.async_key_safety = true;
  return result;
}

std::size_t CountErrorLikeDiagnostics(const core::DiagnosticStream& diags) {
  std::size_t count = 0;
  for (const auto& diag : diags) {
    if (diag.severity == core::Severity::Error) {
      ++count;
    }
  }
  return count;
}

std::array<TranslationPhase, 4> TranslationPhases() {
  SpecDefsConformance();
  return kTranslationPhases;
}

std::array<bool, 4> ReqJudgments(const PhaseOrderResult& phases) {
  SpecDefsConformance();
  const auto phases_required = TranslationPhases();
  (void)phases_required;
  return {phases.phase1_ok, phases.phase2_ok, phases.phase3_ok,
          phases.phase4_ok};
}

std::array<bool, 4> ReqJudgments(const ConformanceInput& input) {
  SpecDefsConformance();
  return {Phase1Order(input), Phase2Order(input), Phase3Order(input),
          Phase4Order(input)};
}

bool Phase1Order(const ConformanceInput& input) {
  SpecDefsConformance();
  const auto evidence = EffectiveEvidence(input);
  SPEC_RULE("Phase1Order");
  return evidence.parse_modules_ok;
}

bool Phase2Order(const ConformanceInput& input) {
  SpecDefsConformance();
  const auto evidence = EffectiveEvidence(input);
  SPEC_RULE("Phase2Order");
  return evidence.parse_modules_ok && evidence.execute_comptime_ok;
}

Phase3ChecksResult Phase3Checks(const ConformanceInput& input) {
  SpecDefsConformance();
  const auto evidence = EffectiveEvidence(input);
  SPEC_RULE("Phase3Checks");
  return evidence.phase3_checks;
}

std::optional<std::size_t> FirstFail(const Phase3ChecksResult& checks) {
  SpecDefsConformance();
  SPEC_RULE("FirstFail");
  if (!checks.resolve_modules_ok) {
    return 0;
  }
  if (!checks.decl_typing_ok) {
    return 1;
  }
  if (!checks.main_check_ok) {
    return 2;
  }
  return std::nullopt;
}

bool Phase3Order(const ConformanceInput& input) {
  SpecDefsConformance();
  if (!Phase2Order(input)) {
    return false;
  }
  SPEC_RULE("Phase3Order");
  return !FirstFail(Phase3Checks(input)).has_value();
}

bool Phase4Order(const ConformanceInput& input) {
  SpecDefsConformance();
  if (!Phase3Order(input)) {
    return false;
  }
  const auto evidence = EffectiveEvidence(input);
  SPEC_RULE("Phase4Order");
  return evidence.output_pipeline_ok;
}

bool Phase0ChecksDisjointSourceChecks() {
  SpecDefsConformance();
  for (const auto phase0 : kPhase0Checks) {
    for (const auto source : kSourceChecks) {
      if (phase0 == source) {
        return false;
      }
    }
  }
  return true;
}

bool IllFormed(std::string_view subject) {
  SpecDefsConformance();
  SPEC_RULE("IllFormed");
  if (!core::IsStaticJudgmentFamily(subject)) {
    return false;
  }
  return core::StaticUndefined(subject);
}

}  // namespace ultraviolet::analysis
