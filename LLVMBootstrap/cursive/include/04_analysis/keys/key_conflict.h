#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "04_analysis/keys/key_context.h"
#include "00_core/span.h"

namespace cursive::analysis {

// C0X Extension: Key System - Conflict Detection
// Implements conflict detection rules for concurrent access

// Conflict detection result
struct ConflictResult {
  bool conflict = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  
  // Conflicting paths (if conflict detected)
  std::optional<KeyPath> path1;
  std::optional<KeyPath> path2;
};

// Check for conflict between two keys
// Conflict((P1, M1), (P2, M2)) = Overlap(P1, P2) ∧ (M1 = Write ∨ M2 = Write)
bool KeyModeCompatible(KeyAccessMode lhs, KeyAccessMode rhs);
bool SegmentLess(const KeyPathSeg& lhs, const KeyPathSeg& rhs);
bool LexLess(const std::vector<KeyPathSeg>& lhs,
             const std::vector<KeyPathSeg>& rhs);
bool PathsDisjoint(const KeyPath& p1, const KeyPath& p2);
bool KeysCompatible(const HeldKey& lhs, const HeldKey& rhs);
bool KeysOverlap(const KeyPath& p1, const KeyPath& p2);
bool KeysConflict(const KeyPath& p1, KeyAccessMode m1,
                  const KeyPath& p2, KeyAccessMode m2);

// Check if a new key acquisition would conflict with held keys
ConflictResult CheckAcquisitionConflict(const KeyContext& ctx,
                                        const KeyPath& path,
                                        KeyAccessMode mode,
                                        const core::Span& span);

// Check if two key blocks have conflicting acquisitions
ConflictResult CheckBlockConflict(const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys1,
                                  const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys2,
                                  const core::Span& span);

// Validate canonical acquisition order
// Keys should be acquired in lexicographic order to avoid deadlock
struct OrderValidation {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::vector<KeyPath> suggested_order;
};

enum class StaticSafetyCondition {
  NoEscape,
  DisjointPaths,
  SequentialContext,
  UniqueOrigin,
  DispatchIndexed,
  SpeculativeOnly,
};

struct StaticSafetyClassification {
  std::set<StaticSafetyCondition> conditions;

  bool Has(StaticSafetyCondition condition) const {
    return conditions.find(condition) != conditions.end();
  }

  bool IsStaticallySafe() const {
    return Has(StaticSafetyCondition::SequentialContext) ||
           Has(StaticSafetyCondition::DisjointPaths) ||
           Has(StaticSafetyCondition::DispatchIndexed) ||
           Has(StaticSafetyCondition::SpeculativeOnly);
  }
};

OrderValidation ValidateAcquisitionOrder(
    const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys);

// Compute canonical acquisition order
std::vector<KeyPath> CanonicalSort(const std::vector<KeyPath>& paths);
std::vector<KeyPath> CanonicalOrder(const std::vector<KeyPath>& paths);
std::vector<std::pair<KeyPath, KeyAccessMode>> CanonicalOrder(
    const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys);

// Static disjointness proof for index equivalence
// Two index expressions are statically disjoint if they can be proven different
bool StaticallyDisjoint(const ast::ExprPtr& idx1, const ast::ExprPtr& idx2);

}  // namespace cursive::analysis
