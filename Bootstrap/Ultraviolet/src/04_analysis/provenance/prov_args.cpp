/*
 * =============================================================================
 * prov_args.cpp - Argument Provenance Tracking
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - Docs/SPECIFICATION.md, Section 21.4.4 "Argument Provenance" (lines 25270-25350)
 *   - Docs/SPECIFICATION.md, Section 6.8 "Procedure Calls" (lines 16500-16700)
 *   - Docs/SPECIFICATION.md, Section 10.3 "Move Semantics" (lines 22310-22400)
 *
 * DIAGNOSTIC CODES:
 *   - E-MEM-3020: Argument provenance expired
 *   - E-MEM-3020: Return provenance escapes
 *   - E-PROV-0032: Move of borrowed provenance
 *
 * =============================================================================
 */

#include "04_analysis/memory/regions.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_expr.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// Forward declarations for internal types
enum class ProvKind {
  Global,
  Stack,
  Heap,
  Region,
  Bottom,
  Param,
};

struct ProvTag {
  ProvKind kind = ProvKind::Bottom;
  std::size_t scope_id = 0;
  IdKey region;
  std::size_t param_index = 0;
};

struct ProvScope {
  std::size_t id = 0;
  std::unordered_map<IdKey, ProvTag> map;
};

struct RegionEntry {
  IdKey tag;
  IdKey target;
};

struct ProvEnv {
  std::vector<ProvScope> scopes;
  std::vector<RegionEntry> regions;
  std::size_t next_scope_id = 0;
};

static inline void SpecDefsArgProv() {
  SPEC_DEF("ArgProvJudg", "5.2.17");
  SPEC_DEF("MoveArgProv", "5.2.17");
  SPEC_DEF("BorrowArgProv", "5.2.17");
  SPEC_DEF("ReturnProv", "5.2.17");
  SPEC_DEF("ProvLifetime", "5.2.17");
}

static ProvTag BottomTag() {
  return ProvTag{ProvKind::Bottom, 0, IdKey{}, 0};
}

static ProvTag ParamTag(std::size_t idx) {
  return ProvTag{ProvKind::Param, 0, IdKey{}, idx};
}

static ProvTag HeapTag() {
  return ProvTag{ProvKind::Heap, 0, IdKey{}, 0};
}

static ProvTag GlobalTag() {
  return ProvTag{ProvKind::Global, 0, IdKey{}, 0};
}

static ProvTag StackTag(std::size_t scope_id) {
  return ProvTag{ProvKind::Stack, scope_id, IdKey{}, 0};
}

static ProvTag RegionTag(const IdKey& name) {
  return ProvTag{ProvKind::Region, 0, name, 0};
}

static bool ProvEq(const ProvTag& lhs, const ProvTag& rhs) {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  switch (lhs.kind) {
    case ProvKind::Stack:
      return lhs.scope_id == rhs.scope_id;
    case ProvKind::Region:
      return lhs.region == rhs.region;
    case ProvKind::Param:
      return lhs.param_index == rhs.param_index;
    default:
      return true;
  }
}

static std::optional<std::size_t> RegionIndex(const ProvEnv& env,
                                              const IdKey& name) {
  for (std::size_t i = 0; i < env.regions.size(); ++i) {
    if (env.regions[i].tag == name) {
      return i;
    }
  }
  return std::nullopt;
}

static bool RegionNesting(const ProvEnv& env,
                          const IdKey& inner,
                          const IdKey& outer) {
  const auto inner_idx = RegionIndex(env, inner);
  const auto outer_idx = RegionIndex(env, outer);
  if (!inner_idx.has_value() || !outer_idx.has_value()) {
    return false;
  }
  return *inner_idx > *outer_idx;
}

static int ProvRank(const ProvTag& tag) {
  switch (tag.kind) {
    case ProvKind::Region:
      return 0;
    case ProvKind::Stack:
      return 1;
    case ProvKind::Heap:
      return 2;
    case ProvKind::Global:
      return 3;
    case ProvKind::Bottom:
      return 4;
    case ProvKind::Param:
      return -1;
  }
  return -1;
}

static bool ProvLeq(const ProvEnv& env, const ProvTag& lhs, const ProvTag& rhs) {
  if (ProvEq(lhs, rhs)) {
    return true;
  }
  if (lhs.kind == ProvKind::Param || rhs.kind == ProvKind::Param) {
    return false;
  }
  if (lhs.kind == ProvKind::Region && rhs.kind == ProvKind::Region) {
    return RegionNesting(env, lhs.region, rhs.region);
  }
  const int lhs_rank = ProvRank(lhs);
  const int rhs_rank = ProvRank(rhs);
  if (lhs_rank < 0 || rhs_rank < 0) {
    return false;
  }
  return lhs_rank < rhs_rank;
}

static bool ProvLess(const ProvEnv& env, const ProvTag& lhs, const ProvTag& rhs) {
  if (lhs.kind == ProvKind::Param || rhs.kind == ProvKind::Param) {
    return false;
  }
  if (lhs.kind == ProvKind::Region && rhs.kind == ProvKind::Region) {
    return RegionNesting(env, lhs.region, rhs.region);
  }
  if (lhs.kind == ProvKind::Region && rhs.kind == ProvKind::Stack) {
    return true;
  }
  if (lhs.kind == ProvKind::Stack && rhs.kind == ProvKind::Heap) {
    return true;
  }
  if (lhs.kind == ProvKind::Heap && rhs.kind == ProvKind::Global) {
    return true;
  }
  if (lhs.kind == ProvKind::Global && rhs.kind == ProvKind::Bottom) {
    return true;
  }
  return false;
}

static ProvTag JoinProv(const ProvEnv& env, const ProvTag& lhs,
                        const ProvTag& rhs) {
  if (ProvLeq(env, lhs, rhs)) {
    return lhs;
  }
  if (ProvLeq(env, rhs, lhs)) {
    return rhs;
  }
  return BottomTag();
}

static ProvTag JoinAllProv(const ProvEnv& env, const std::vector<ProvTag>& tags) {
  if (tags.empty()) {
    return BottomTag();
  }
  ProvTag current = tags.front();
  for (std::size_t i = 1; i < tags.size(); ++i) {
    current = JoinProv(env, current, tags[i]);
  }
  return current;
}

static ProvenanceKind ToPublicKind(const ProvTag& tag) {
  switch (tag.kind) {
    case ProvKind::Global:
      return ProvenanceKind::Global;
    case ProvKind::Stack:
      return ProvenanceKind::Stack;
    case ProvKind::Heap:
      return ProvenanceKind::Heap;
    case ProvKind::Region:
      return ProvenanceKind::Region;
    case ProvKind::Bottom:
      return ProvenanceKind::Bottom;
    case ProvKind::Param:
      return ProvenanceKind::Param;
  }
  return ProvenanceKind::Bottom;
}

static ProvTag FromPublicKind(ProvenanceKind kind) {
  switch (kind) {
    case ProvenanceKind::Global:
      return GlobalTag();
    case ProvenanceKind::Stack:
      return StackTag(0);
    case ProvenanceKind::Heap:
      return HeapTag();
    case ProvenanceKind::Region:
      return RegionTag(IdKey{});
    case ProvenanceKind::Bottom:
      return BottomTag();
    case ProvenanceKind::Param:
      return ParamTag(0);
  }
  return BottomTag();
}

}  // namespace

// =============================================================================
// Argument Provenance Types
// =============================================================================

struct ArgProvenance {
  std::size_t arg_index = 0;
  ProvenanceKind prov = ProvenanceKind::Bottom;
  bool is_move = false;
  bool is_borrowed = false;
};

struct CallProvenanceEffect {
  std::vector<ArgProvenance> arg_provs;
  ProvenanceKind return_prov = ProvenanceKind::Bottom;
  std::vector<std::size_t> consumed_args;  // Indices of move arguments
  bool may_escape = false;
};

// =============================================================================
// Analyze Call Provenance
// =============================================================================

CallProvenanceEffect AnalyzeCallProvenance(
    const ScopeContext& ctx,
    const std::vector<ProvenanceKind>& arg_provs,
    const std::vector<bool>& arg_is_move,
    const TypeEnv& gamma) {
  SpecDefsArgProv();

  CallProvenanceEffect effect;

  // Track each argument's provenance
  for (std::size_t i = 0; i < arg_provs.size(); ++i) {
    ArgProvenance arg;
    arg.arg_index = i;
    arg.prov = arg_provs[i];
    arg.is_move = i < arg_is_move.size() && arg_is_move[i];
    arg.is_borrowed = !arg.is_move;

    if (arg.is_move) {
      effect.consumed_args.push_back(i);
    }

    effect.arg_provs.push_back(std::move(arg));
  }

  // Infer return provenance (conservative: could come from any argument)
  ProvEnv env;
  env.next_scope_id = 0;

  std::vector<ProvTag> tags;
  tags.reserve(arg_provs.size());
  for (const auto& prov : arg_provs) {
    tags.push_back(FromPublicKind(prov));
  }

  if (!tags.empty()) {
    const auto merged = JoinAllProv(env, tags);
    effect.return_prov = ToPublicKind(merged);
  }

  SPEC_RULE("CallProv-Analyze");
  return effect;
}

// =============================================================================
// Track Argument Provenance
// =============================================================================

struct ArgProvResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvenanceKind prov;
};

ArgProvResult TrackArgumentProvenance(
    ProvenanceKind arg_prov,
    ProvenanceKind param_prov,
    bool is_move) {
  SpecDefsArgProv();

  ArgProvResult result;
  result.ok = true;
  result.prov = arg_prov;

  // For non-move arguments, the provenance is borrowed for the call duration
  // The caller retains the original provenance
  if (!is_move) {
    SPEC_RULE("BorrowArgProv");
    result.prov = arg_prov;
    return result;
  }

  // For move arguments, the provenance is transferred to the callee
  SPEC_RULE("MoveArgProv");
  result.prov = arg_prov;
  return result;
}

// =============================================================================
// Track Move Argument Provenance
// =============================================================================

struct MoveArgProvResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvenanceKind transferred_prov;
};

MoveArgProvResult TrackMoveArgProvenance(
    ProvenanceKind arg_prov,
    const core::Span& arg_span) {
  SpecDefsArgProv();

  MoveArgProvResult result;
  result.ok = true;
  result.transferred_prov = arg_prov;

  // Move transfers ownership and provenance to the callee
  // The caller loses access to the moved value
  SPEC_RULE("MoveArgProv-Transfer");
  return result;
}

// =============================================================================
// Validate Provenance Lifetime
// =============================================================================

struct LifetimeValidation {
  bool valid = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

LifetimeValidation ValidateProvenanceLifetime(
    ProvenanceKind arg_prov,
    ProvenanceKind call_site_prov,
    const core::Span& arg_span) {
  SpecDefsArgProv();

  LifetimeValidation result;

  // Create minimal environment for comparison
  ProvEnv env;
  env.next_scope_id = 0;

  const auto arg_tag = FromPublicKind(arg_prov);
  const auto site_tag = FromPublicKind(call_site_prov);

  // Argument provenance must outlive the call
  // i.e., arg_prov must be >= call_site_prov in the provenance lattice
  if (ProvLess(env, arg_tag, site_tag)) {
    SPEC_RULE("ProvLifetime-Err");
    result.valid = false;
    result.diag_id = "E-MEM-3020";
    result.span = arg_span;
    return result;
  }

  SPEC_RULE("ProvLifetime-Ok");
  result.valid = true;
  return result;
}

// =============================================================================
// Infer Return Provenance
// =============================================================================

struct ReturnProvInference {
  ProvenanceKind prov = ProvenanceKind::Bottom;
  std::optional<std::size_t> from_arg;  // If return prov comes from an argument
  bool is_fresh = false;                 // If return prov is from new allocation
};

ReturnProvInference InferReturnProvenance(
    const std::vector<ProvenanceKind>& arg_provs,
    const std::vector<bool>& arg_is_move,
    bool returns_heap_allocated) {
  SpecDefsArgProv();

  ReturnProvInference result;

  // If the procedure allocates on the heap and returns that
  if (returns_heap_allocated) {
    SPEC_RULE("ReturnProv-Heap");
    result.prov = ProvenanceKind::Heap;
    result.is_fresh = true;
    return result;
  }

  // Otherwise, return provenance is the join of all argument provenances
  if (arg_provs.empty()) {
    SPEC_RULE("ReturnProv-NoArgs");
    result.prov = ProvenanceKind::Bottom;
    return result;
  }

  // Find the most restrictive provenance among arguments
  ProvEnv env;
  env.next_scope_id = 0;

  std::vector<ProvTag> tags;
  tags.reserve(arg_provs.size());
  for (std::size_t i = 0; i < arg_provs.size(); ++i) {
    tags.push_back(FromPublicKind(arg_provs[i]));
  }

  const auto merged = JoinAllProv(env, tags);
  result.prov = ToPublicKind(merged);

  // Try to identify which argument the return might come from
  for (std::size_t i = 0; i < arg_provs.size(); ++i) {
    if (arg_provs[i] == result.prov) {
      result.from_arg = i;
      break;
    }
  }

  SPEC_RULE("ReturnProv-FromArgs");
  return result;
}

// =============================================================================
// Check Provenance Escape
// =============================================================================

struct EscapeCheck {
  bool may_escape = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::string reason;
};

EscapeCheck CheckProvenanceEscape(
    ProvenanceKind arg_prov,
    ProvenanceKind call_result_prov,
    ProvenanceKind call_site_prov,
    const core::Span& call_span) {
  SpecDefsArgProv();

  EscapeCheck result;

  ProvEnv env;
  env.next_scope_id = 0;

  const auto arg_tag = FromPublicKind(arg_prov);
  const auto res_tag = FromPublicKind(call_result_prov);
  const auto site_tag = FromPublicKind(call_site_prov);

  // Check if the call result's provenance escapes through an argument
  // This happens when the result provenance is more restrictive than
  // the call site provenance
  if (ProvLess(env, res_tag, site_tag)) {
    SPEC_RULE("ProvEscape-Result");
    result.may_escape = true;
    result.diag_id = "E-MEM-3020";
    result.span = call_span;
    result.reason = "Return provenance escapes call site scope";
    return result;
  }

  // Check if argument provenance is borrowed but the callee might store it
  if (arg_prov == ProvenanceKind::Region) {
    // Region provenance passed to a procedure could escape if stored
    // This is a conservative check - actual escape depends on procedure body
    SPEC_RULE("ProvEscape-RegionArg");
    result.may_escape = true;
    result.reason = "Region provenance may escape through procedure call";
    // This is a warning, not an error
    return result;
  }

  SPEC_RULE("ProvEscape-Ok");
  result.may_escape = false;
  return result;
}

// =============================================================================
// Method Call Provenance
// =============================================================================

struct MethodCallProvenance {
  ProvenanceKind receiver_prov;
  std::vector<ArgProvenance> arg_provs;
  ProvenanceKind return_prov;
};

MethodCallProvenance AnalyzeMethodCallProvenance(
    ProvenanceKind receiver_prov,
    const std::vector<ProvenanceKind>& arg_provs,
    const std::vector<bool>& arg_is_move,
    bool receiver_is_move) {
  SpecDefsArgProv();

  MethodCallProvenance result;
  result.receiver_prov = receiver_prov;

  // Combine receiver with arguments
  std::vector<ProvenanceKind> all_provs;
  all_provs.push_back(receiver_prov);
  all_provs.insert(all_provs.end(), arg_provs.begin(), arg_provs.end());

  std::vector<bool> all_moves;
  all_moves.push_back(receiver_is_move);
  all_moves.insert(all_moves.end(), arg_is_move.begin(), arg_is_move.end());

  // Track each argument
  for (std::size_t i = 0; i < arg_provs.size(); ++i) {
    ArgProvenance arg;
    arg.arg_index = i;
    arg.prov = arg_provs[i];
    arg.is_move = i < arg_is_move.size() && arg_is_move[i];
    arg.is_borrowed = !arg.is_move;
    result.arg_provs.push_back(std::move(arg));
  }

  // Infer return provenance from all inputs
  ProvEnv env;
  env.next_scope_id = 0;

  std::vector<ProvTag> tags;
  tags.reserve(all_provs.size());
  for (const auto& prov : all_provs) {
    tags.push_back(FromPublicKind(prov));
  }

  if (!tags.empty()) {
    const auto merged = JoinAllProv(env, tags);
    result.return_prov = ToPublicKind(merged);
  }

  SPEC_RULE("MethodCallProv-Analyze");
  return result;
}

// =============================================================================
// Async Call Provenance
// =============================================================================

struct AsyncCallProvenance {
  std::vector<ArgProvenance> captured_provs;
  ProvenanceKind future_prov;
  bool captures_region_prov = false;
};

AsyncCallProvenance AnalyzeAsyncCallProvenance(
    const std::vector<ProvenanceKind>& arg_provs,
    ProvenanceKind frame_prov) {
  SpecDefsArgProv();

  AsyncCallProvenance result;

  ProvEnv env;
  env.next_scope_id = 0;

  const auto frame_tag = FromPublicKind(frame_prov);

  // Check each captured argument
  for (std::size_t i = 0; i < arg_provs.size(); ++i) {
    const auto arg_tag = FromPublicKind(arg_provs[i]);

    ArgProvenance cap;
    cap.arg_index = i;
    cap.prov = arg_provs[i];

    // Check if capture would escape frame
    if (ProvLess(env, arg_tag, frame_tag)) {
      // This capture would escape - async captures must outlive the frame
      result.captures_region_prov = true;
    }

    result.captured_provs.push_back(std::move(cap));
  }

  // Future provenance is the frame provenance (async is tied to its creation scope)
  result.future_prov = frame_prov;

  SPEC_RULE("AsyncCallProv-Analyze");
  return result;
}

}  // namespace ultraviolet::analysis

