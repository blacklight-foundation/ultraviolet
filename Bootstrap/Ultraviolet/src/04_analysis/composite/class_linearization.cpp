// =============================================================================
// MIGRATION MAPPING: class_linearization.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Section 5.3.1 "Classes (Ultraviolet)" - Superclass Linearization (C3) (lines 11834-11882)
//   - Supers(Cl) (line 11836)
//   - Lin-Base (lines 11838-11841)
//   - Head, Tail (lines 11843-11845)
//   - HeadOk (line 11846)
//   - SelectHead (lines 11847-11848)
//   - PopHead (lines 11849-11850)
//   - PopAll (line 11851)
//   - Merge-Empty (lines 11853-11856)
//   - Merge-Step (lines 11858-11861)
//   - Merge-Fail (lines 11863-11866)
//   - Lin-Ok (lines 11868-11871)
//   - Lin-Fail (lines 11873-11876)
//   - Superclass-Cycle (lines 11878-11881)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/composite/class_linearization.cpp
// - Lines 1-190 (entire file)
//
// Key source functions to migrate:
// - LinearizeClass (lines 182-187): Main entry point for C3 linearization
//
// Supporting helpers:
// - LookupClassDecl (lines 27-34): Look up class declaration
// - ClassList type alias (line 36): Vector of class paths
// - ClassLists type alias (line 37): Vector of class lists
// - HeadOk (lines 39-51): Check if head is ok (not in tail of any list)
// - SelectHead (lines 53-64): Select first ok head from lists
// - PopAll (lines 66-79): Remove head from all lists
// - AllEmpty (lines 81-88): Check if all lists are empty
// - Merge (lines 90-110): C3 merge algorithm
// - LinearizeState struct (lines 112-115): Memoization state
// - LinearizeImpl (lines 117-178): Recursive linearization implementation
//
// DEPENDENCIES:
// - ultraviolet/include/04_analysis/resolve/scopes.h (ScopeContext, PathKeyOf, PathEq)
// - ultraviolet/include/00_core/assert_spec.h (SPEC_DEF, SPEC_RULE)
//
// Diagnostic rules implemented:
// - Merge-Empty (line 97): All lists empty - successful merge
// - Merge-Step (line 106): Found valid head - continue merge
// - Merge-Fail (line 103): No valid head found - linearization fails
// - Lin-Base (line 140): Base case - class with no supers
// - Lin-Fail (line 163): Merge failed
// - Lin-Ok (line 169): Linearization succeeded
// - Superclass-Undefined (line 133): Referenced superclass not found
//
// REFACTORING NOTES:
// 1. C3 linearization (MRO - Method Resolution Order) from Python
// 2. Memoization prevents recomputation for diamond inheritance
// 3. Active set prevents infinite recursion on cycles
// 4. Merge algorithm selects heads that don't appear in tails
// 5. Result includes the class itself as first element
// 6. Cycle detection via active set returns empty result
// =============================================================================

#include "04_analysis/composite/classes.h"

#include <map>
#include <optional>
#include <set>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsClassLinearization() {
  SPEC_DEF("Supers", "5.3.1");
  SPEC_DEF("Linearize", "5.3.1");
  SPEC_DEF("Merge", "5.3.1");
  SPEC_DEF("Head", "5.3.1");
  SPEC_DEF("Tail", "5.3.1");
  SPEC_DEF("HeadOk", "5.3.1");
  SPEC_DEF("SelectHead", "5.3.1");
  SPEC_DEF("PopHead", "5.3.1");
  SPEC_DEF("PopAll", "5.3.1");
  SPEC_DEF("Lin-Fail", "5.3.1");
  SPEC_DEF("Superclass-Cycle", "5.3.1");
}

static const ast::ClassDecl* LookupClassDecl(const ScopeContext& ctx,
                                             const ast::ClassPath& path) {
  const auto it = ctx.sigma.classes.find(PathKeyOf(path));
  if (it == ctx.sigma.classes.end()) {
    return nullptr;
  }
  return &it->second;
}

using ClassList = std::vector<ast::ClassPath>;
using ClassLists = std::vector<ClassList>;

static bool HeadOk(const ast::ClassPath& head, const ClassLists& lists) {
  for (const auto& list : lists) {
    if (list.empty()) {
      continue;
    }
    for (std::size_t i = 1; i < list.size(); ++i) {
      if (PathEq(list[i], head)) {
        return false;
      }
    }
  }
  return true;
}

static std::optional<ast::ClassPath> SelectHead(const ClassLists& lists) {
  for (const auto& list : lists) {
    if (list.empty()) {
      continue;
    }
    const auto& head = list.front();
    if (HeadOk(head, lists)) {
      return head;
    }
  }
  return std::nullopt;
}

static ClassLists PopAll(const ast::ClassPath& head,
                         const ClassLists& lists) {
  ClassLists out;
  out.reserve(lists.size());
  for (const auto& list : lists) {
    if (!list.empty() && PathEq(list.front(), head)) {
      ClassList tail(list.begin() + 1, list.end());
      out.push_back(std::move(tail));
    } else {
      out.push_back(list);
    }
  }
  return out;
}

static bool AllEmpty(const ClassLists& lists) {
  for (const auto& list : lists) {
    if (!list.empty()) {
      return false;
    }
  }
  return true;
}

static LinearizationResult Merge(const ClassLists& lists) {
  SpecDefsClassLinearization();
  LinearizationResult result;

  ClassLists current = lists;
  while (true) {
    if (AllEmpty(current)) {
      SPEC_RULE("Merge-Empty");
      result.ok = true;
      return result;
    }
    const auto head_opt = SelectHead(current);
    if (!head_opt.has_value()) {
      SPEC_RULE("Merge-Fail");
      return result;
    }
    SPEC_RULE("Merge-Step");
    result.order.push_back(*head_opt);
    current = PopAll(*head_opt, current);
  }
}

struct LinearizeState {
  std::map<PathKey, LinearizationResult> memo;
  std::set<PathKey> active;
};

static LinearizationResult LinearizeImpl(const ScopeContext& ctx,
                                         const ast::ClassPath& path,
                                         LinearizeState& state) {
  SpecDefsClassLinearization();
  const auto key = PathKeyOf(path);
  if (const auto it = state.memo.find(key); it != state.memo.end()) {
    return it->second;
  }
  if (state.active.find(key) != state.active.end()) {
    SPEC_RULE("Lin-Fail");
    SPEC_RULE("Superclass-Cycle");
    LinearizationResult cycle;
    cycle.diag_id = "E-TYP-2508";
    return cycle;
  }
  state.active.insert(key);

  LinearizationResult result;
  const auto* decl = LookupClassDecl(ctx, path);
  if (!decl) {
    result.diag_id = "Superclass-Undefined";
    state.active.erase(key);
    state.memo.emplace(key, result);
    return result;
  }

  if (decl->supers.empty()) {
    SPEC_RULE("Lin-Base");
    result.ok = true;
    result.order = {path};
    state.active.erase(key);
    state.memo.emplace(key, result);
    return result;
  }

  ClassLists lists;
  lists.reserve(decl->supers.size() + 1);
  for (const auto& super : decl->supers) {
    const auto linearized = LinearizeImpl(ctx, super, state);
    if (!linearized.ok) {
      LinearizationResult failed = linearized;
      if (!failed.diag_id.has_value()) {
        failed.diag_id = "E-TYP-2508";
      }
      if (*failed.diag_id == "E-TYP-2508") {
        SPEC_RULE("Superclass-Cycle");
      }
      state.active.erase(key);
      state.memo.emplace(key, failed);
      return failed;
    }
    lists.push_back(linearized.order);
  }
  lists.push_back(decl->supers);

  const auto merged = Merge(lists);
  if (!merged.ok) {
    SPEC_RULE("Lin-Fail");
    SPEC_RULE("Superclass-Cycle");
    LinearizationResult failed = merged;
    failed.diag_id = "E-TYP-2508";
    state.active.erase(key);
    state.memo.emplace(key, failed);
    return failed;
  }

  SPEC_RULE("Lin-Ok");
  result.ok = true;
  result.order.reserve(merged.order.size() + 1);
  result.order.push_back(path);
  result.order.insert(result.order.end(), merged.order.begin(), merged.order.end());

  state.active.erase(key);
  state.memo.emplace(key, result);
  return result;
}

}  // namespace

LinearizationResult LinearizeClass(const ScopeContext& ctx,
                                   const ast::ClassPath& path) {
  SpecDefsClassLinearization();
  LinearizeState state;
  return LinearizeImpl(ctx, path, state);
}

}  // namespace ultraviolet::analysis
