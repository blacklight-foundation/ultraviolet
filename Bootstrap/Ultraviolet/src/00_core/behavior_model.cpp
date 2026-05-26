// =============================================================================
// BEHAVIOR MODEL IMPLEMENTATION
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 1.2 "Behavior Types" (lines 392-445)
//     - StaticJudgSet: union of all static judgment types
//     - StaticRuleSet: rules whose conclusions are static judgments
//     - StaticUndefined(J): premise contains bottom (⊥)
//     - DiagIdOf(J): maps judgment to diagnostic ID
//     - Static-Undefined rule: emits diagnostic code for static undefined
//     - Static-Undefined-NoCode rule: when no code defined
//     - OutsideConformance: no requirements imposed
//     - CheckKind = {PatternExhaustiveness, TypeCompatibility, PermissionViolations,
//                    ProvenanceEscape, ArrayBounds, SafePointerValidity,
//                    IntegerOverflow, SliceBounds, IntDivisionByZero}
//     - StaticCheck vs RuntimeCheck classification
//     - RuntimeBehavior(IntegerOverflow/SliceBounds/IntDivisionByZero) = Panic
//
//   - Section 1.4.4 "Unverifiable Behavior (UVB)" (Ultraviolet.md)
//     - UVB: Operations whose runtime correctness depends on properties
//       external to the language's semantic model
//     - UVB is permitted only within `unsafe` blocks and FFI calls
//
// IMPLEMENTATION NOTES:
//   1. SPEC_DEF traces to "1.2":
//      - "BehaviorClass", "UVBRel", "DynamicUndefined", "Behavior"
//      - "StaticUndefined", "DiagIdOf"
//   2. SPEC_RULE traces:
//      - "Dynamic-Undefined-UVB" in BehaviorOfDynamicUndefined()
//      - "Static-Undefined" and "Static-Undefined-NoCode" in StaticUndefinedCode()
//   3. UVB = Unverifiable Behavior (NOT traditional "undefined behavior")
//   4. RawPtrPermission::Imm means immutable - writes are UVB
//   5. DynamicUndefinedReadPtr accepts perm for API symmetry but doesn't use it:
//      reading from undefined address is UVB regardless of permission
//
// =============================================================================

#include "00_core/behavior_model.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::core {

namespace {

#include "generated/static_rule_registry.inc"

static bool IsUpperAscii(char c) {
  return c >= 'A' && c <= 'Z';
}

static bool IsDigitAscii(char c) {
  return c >= '0' && c <= '9';
}

static bool IsDiagnosticCodeLike(std::string_view id) {
  // DiagCode = DiagPrefix ++ "-" ++ DiagCategory ++ "-" ++ DiagDigits
  // where DiagPrefix in {E, W, I, P}, DiagCategory is 3 uppercase letters,
  // and DiagDigits is 4 decimal digits.
  return id.size() == 10 &&
         (id[0] == 'E' || id[0] == 'W' || id[0] == 'I' || id[0] == 'P') &&
         id[1] == '-' &&
         IsUpperAscii(id[2]) &&
         IsUpperAscii(id[3]) &&
         IsUpperAscii(id[4]) &&
         id[5] == '-' &&
         IsDigitAscii(id[6]) &&
         IsDigitAscii(id[7]) &&
         IsDigitAscii(id[8]) &&
         IsDigitAscii(id[9]);
}

std::vector<std::string_view> SplitPremises(std::string_view premises_text) {
  std::vector<std::string_view> premises;
  std::size_t start = 0;
  while (start <= premises_text.size()) {
    const std::size_t end = premises_text.find('\n', start);
    const std::size_t length =
        (end == std::string_view::npos) ? premises_text.size() - start
                                        : end - start;
    const std::string_view premise = premises_text.substr(start, length);
    if (!premise.empty()) {
      premises.push_back(premise);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return premises;
}

}  // namespace

static inline void SpecDefsBehaviorModel() {
  SPEC_DEF("BehaviorClass", "1.2");
  SPEC_DEF("UVBRel", "1.2");
  SPEC_DEF("DynamicUndefined", "1.2");
  SPEC_DEF("Behavior", "1.2");
  SPEC_DEF("StaticUndefined", "1.2");
  SPEC_DEF("DiagIdOf", "1.2");
  SPEC_DEF("CheckKind", "1.2");
  SPEC_DEF("StaticCheck", "1.2");
  SPEC_DEF("RuntimeCheck", "1.2");
  SPEC_DEF("RuntimeBehavior", "1.2");
  SPEC_DEF("StaticJudgSet", "1.2");
  SPEC_DEF("StaticRuleSet", "1.2");
  SPEC_DEF("Premises", "1.2");
  SPEC_DEF("MaxErrorCount", "1.2");
  SPEC_DEF("AbortOnErrorCount", "1.2");
}

ErrorRecoveryPolicy DefaultErrorRecoveryPolicy() {
  SpecDefsBehaviorModel();
  ErrorRecoveryPolicy policy;
  policy.max_error_count = kSuggestedMaxErrorCount;
  return policy;
}

bool IsStaticCheck(CheckKind kind) {
  SpecDefsBehaviorModel();
  switch (kind) {
    case CheckKind::PatternExhaustiveness:
    case CheckKind::TypeCompatibility:
    case CheckKind::PermissionViolations:
    case CheckKind::ProvenanceEscape:
    case CheckKind::ArrayBounds:
    case CheckKind::SafePointerValidity:
      return true;
    case CheckKind::IntegerOverflow:
    case CheckKind::SliceBounds:
    case CheckKind::IntDivisionByZero:
      return false;
  }
  return false;
}

bool IsRuntimeCheck(CheckKind kind) {
  SpecDefsBehaviorModel();
  return !IsStaticCheck(kind);
}

std::optional<RuntimeBehavior> RuntimeBehaviorOf(CheckKind kind) {
  SpecDefsBehaviorModel();
  if (!IsRuntimeCheck(kind)) {
    return std::nullopt;
  }
  return RuntimeBehavior::Panic;
}

bool AbortOnErrorCount(const ErrorRecoveryPolicy& policy,
                       std::size_t error_count) {
  SpecDefsBehaviorModel();
  if (!policy.max_error_count.has_value()) {
    return false;
  }
  if (error_count >= *policy.max_error_count) {
    SPEC_RULE("Abort-On-ErrorCount");
    return true;
  }
  return false;
}

bool IsStaticJudgmentFamily(std::string_view judgment_family) {
  SpecDefsBehaviorModel();
  for (const auto fam : kStaticJudgmentFamilies) {
    if (fam == judgment_family) {
      return true;
    }
  }
  return false;
}

bool IsStaticRule(std::string_view rule_id) {
  SpecDefsBehaviorModel();
  for (const auto& meta : kStaticRules) {
    if (meta.rule_id == rule_id &&
        IsStaticJudgmentFamily(meta.conclusion_family)) {
      return true;
    }
  }
  return false;
}

std::optional<StaticRuleMeta> LookupStaticRule(std::string_view rule_id) {
  SpecDefsBehaviorModel();
  for (const auto& meta : kStaticRules) {
    if (meta.rule_id == rule_id &&
        IsStaticJudgmentFamily(meta.conclusion_family)) {
      return meta;
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> ConclusionOfRule(std::string_view rule_id) {
  SpecDefsBehaviorModel();
  if (const auto meta = LookupStaticRule(rule_id)) {
    return meta->conclusion_family;
  }
  return std::nullopt;
}

std::optional<std::string_view> ConclusionFamilyOfRule(std::string_view rule_id) {
  return ConclusionOfRule(rule_id);
}

std::optional<std::vector<std::string_view>> PremisesOfRule(
    std::string_view rule_id) {
  SpecDefsBehaviorModel();
  const auto meta = LookupStaticRule(rule_id);
  if (!meta.has_value() || !meta->premises_text.has_value()) {
    return std::nullopt;
  }
  return SplitPremises(*meta->premises_text);
}

std::optional<std::string_view> DiagIdOfJudgment(
    std::string_view judgment_family) {
  SpecDefsBehaviorModel();
  for (const auto& meta : kStaticRules) {
    if (meta.conclusion_family != judgment_family) {
      continue;
    }
    if (meta.diag_id.has_value()) {
      return meta.diag_id;
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> DiagIdOfRule(std::string_view rule_id) {
  SpecDefsBehaviorModel();
  if (const auto meta = LookupStaticRule(rule_id)) {
    return meta->diag_id;
  }
  return std::nullopt;
}

bool StaticUndefined(std::string_view judgment_family) {
  SpecDefsBehaviorModel();
  if (!IsStaticJudgmentFamily(judgment_family)) {
    return false;
  }
  for (const auto& meta : kStaticRules) {
    if (meta.conclusion_family != judgment_family) {
      continue;
    }
    if (meta.has_bottom_premise) {
      return true;
    }
  }
  return false;
}

BehaviorClass BehaviorOfDynamicUndefined(bool dynamic_undefined) {
  SpecDefsBehaviorModel();
  if (dynamic_undefined) {
    SPEC_RULE("Dynamic-Undefined-UVB");
    return BehaviorClass::UVB;
  }
  return BehaviorClass::Specified;
}

bool DynamicUndefinedReadPtr([[maybe_unused]] RawPtrPermission perm,
                             bool read_addr_defined) {
  SpecDefsBehaviorModel();
  // perm is semantically unused: reading from undefined address is UVB
  // regardless of permission (both imm and mut). Parameter kept for symmetry
  // with DynamicUndefinedWritePtr.
  return !read_addr_defined;
}

bool DynamicUndefinedWritePtr(RawPtrPermission perm) {
  SpecDefsBehaviorModel();
  return perm == RawPtrPermission::Imm;
}

std::optional<DiagCode> StaticUndefinedCode(const DiagCodeMap& spec_map,
                                            const DiagCodeMap& uv_map,
                                            const DiagId& id) {
  SpecDefsBehaviorModel();
  const auto code = Code(spec_map, uv_map, id);
  if (code.has_value()) {
    SPEC_RULE("Static-Undefined");
    return code;
  }
  SPEC_RULE("Static-Undefined-NoCode");
  return std::nullopt;
}

std::optional<DiagCode> StaticUndefinedCodeForRule(const DiagCodeMap& spec_map,
                                                   const DiagCodeMap& uv_map,
                                                   std::string_view rule_id) {
  SpecDefsBehaviorModel();
  const auto conclusion = ConclusionOfRule(rule_id);
  if (!conclusion.has_value()) {
    SPEC_RULE("Static-Undefined-NoCode");
    return std::nullopt;
  }
  if (!StaticUndefined(*conclusion)) {
    SPEC_RULE("Static-Undefined-NoCode");
    return std::nullopt;
  }
  auto diag_id = DiagIdOfRule(rule_id);
  if (!diag_id.has_value()) {
    diag_id = DiagIdOfJudgment(*conclusion);
  }
  if (!diag_id.has_value()) {
    SPEC_RULE("Static-Undefined-NoCode");
    return std::nullopt;
  }
  if (IsDiagnosticCodeLike(*diag_id)) {
    SPEC_RULE("Static-Undefined");
    return std::string(*diag_id);
  }
  const auto code = Code(spec_map, uv_map, std::string(*diag_id));
  if (code.has_value()) {
    SPEC_RULE("Static-Undefined");
    return code;
  }
  SPEC_RULE("Static-Undefined-NoCode");
  return std::nullopt;
}

}  // namespace ultraviolet::core
