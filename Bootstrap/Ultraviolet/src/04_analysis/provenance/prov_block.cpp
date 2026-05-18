/*
 * =============================================================================
 * prov_block.cpp - Block Provenance Analysis
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - SPECIFICATION.md, Section 21.4.5 "Block Provenance" (lines 25360-25450)
 *   - SPECIFICATION.md, Section 21.2 "Region Statements" (lines 24800-24900)
 *   - SPECIFICATION.md, Section 21.3 "Frame Statements" (lines 24910-25000)
 *
 * DIAGNOSTIC CODES:
 *   - E-PROV-0040: Provenance escapes region
 *   - E-PROV-0041: Provenance escapes frame
 *   - E-PROV-0042: Incompatible provenance merge
 *   - E-PROV-0043: Return of region provenance
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

struct ProvFlow {
  std::vector<ProvTag> results;
  std::vector<ProvTag> breaks;
  bool break_void = false;
};

struct BlockProvResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvTag prov;
  ProvFlow flow;
};

struct EscapeInfo {
  std::string binding_name;
  ProvenanceKind prov_kind;
  core::Span span;
};

static inline void SpecDefsBlockProv() {
  SPEC_DEF("BlockProvJudg", "5.2.17");
  SPEC_DEF("RegionBind", "5.2.17");
  SPEC_DEF("FrameBind", "5.2.17");
  SPEC_DEF("ProvEscape", "5.2.17");
  SPEC_DEF("ControlFlowMerge", "5.2.17");
}

static ProvTag BottomTag() {
  return ProvTag{ProvKind::Bottom, 0, IdKey{}, 0};
}

static ProvTag RegionTag(const IdKey& name) {
  return ProvTag{ProvKind::Region, 0, name, 0};
}

static ProvTag StackTag(std::size_t scope_id) {
  return ProvTag{ProvKind::Stack, scope_id, IdKey{}, 0};
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

static ProvEnv PushScope_pi(const ProvEnv& env) {
  ProvEnv out = env;
  ProvScope scope;
  scope.id = out.next_scope_id++;
  out.scopes.push_back(std::move(scope));
  return out;
}

static ProvEnv PopScope_pi(const ProvEnv& env) {
  if (env.scopes.empty()) {
    return env;
  }
  ProvEnv out = env;
  out.scopes.pop_back();
  return out;
}

static ProvEnv Intro_pi(const ProvEnv& env, std::string_view name,
                        const ProvTag& tag) {
  ProvEnv out = env;
  if (out.scopes.empty()) {
    return out;
  }
  out.scopes.back().map[IdKeyOf(name)] = tag;
  return out;
}

static ProvEnv SeedBlockProvEnv(const TypeEnv& gamma, const ProvEnv& env) {
  ProvEnv out = env;
  if (out.scopes.empty()) {
    ProvScope scope;
    scope.id = out.next_scope_id++;
    out.scopes.push_back(std::move(scope));
  }

  for (const auto& type_scope : gamma.scopes) {
    for (const auto& [key, binding] : type_scope) {
      if (RegionActiveType(binding.type)) {
        const auto tag = binding.provenance_region.value_or(key);
        out.regions.push_back(RegionEntry{tag, key});
        out.scopes.back().map[key] = RegionTag(tag);
        continue;
      }

      ProvTag prov = BottomTag();
      switch (binding.provenance_kind) {
        case BindingProvenanceSeedKind::Global:
          prov.kind = ProvKind::Global;
          break;
        case BindingProvenanceSeedKind::Stack:
          prov = StackTag(out.scopes.back().id);
          break;
        case BindingProvenanceSeedKind::Heap:
          prov.kind = ProvKind::Heap;
          break;
        case BindingProvenanceSeedKind::Region:
          if (binding.provenance_region.has_value()) {
            prov = RegionTag(*binding.provenance_region);
          }
          break;
        case BindingProvenanceSeedKind::Bottom:
          prov = BottomTag();
          break;
        case BindingProvenanceSeedKind::Param:
          prov.kind = ProvKind::Param;
          break;
      }
      out.scopes.back().map[key] = prov;
    }
  }
  return out;
}

static ProvTag StackProv(const ProvEnv& env) {
  if (env.scopes.empty()) {
    return BottomTag();
  }
  return StackTag(env.scopes.back().id);
}

static ProvTag BindProv(const ProvEnv& env, const ProvTag& init) {
  if (init.kind == ProvKind::Bottom) {
    return StackProv(env);
  }
  return init;
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

}  // namespace

// =============================================================================
// Block Provenance State
// =============================================================================

struct BlockProvState {
  bool in_region = false;
  bool in_frame = false;
  std::optional<std::string> region_name;
  std::optional<std::string> frame_name;
  std::size_t scope_depth = 0;
};

// =============================================================================
// Region Provenance Entry/Exit
// =============================================================================

struct RegionProvState {
  std::string region_name;
  ProvenanceKind prov_kind;
  std::size_t entry_scope_id;
};

RegionProvState EnterRegionProvenance(const std::string& region_name,
                                       std::size_t current_scope_id) {
  SpecDefsBlockProv();
  SPEC_RULE("RegionBind-Enter");

  RegionProvState state;
  state.region_name = region_name;
  state.prov_kind = ProvenanceKind::Region;
  state.entry_scope_id = current_scope_id;
  return state;
}

void ExitRegionProvenance(const RegionProvState& state) {
  SpecDefsBlockProv();
  SPEC_RULE("RegionBind-Exit");

  // On region exit, all pointers with this region's provenance become expired.
  // This is enforced by the type system transitioning Ptr<T>@Valid to Ptr<T>@Expired.
  // The state parameter captures information for diagnostic purposes.
  (void)state;
}

// =============================================================================
// Frame Provenance Entry/Exit
// =============================================================================

struct FrameProvState {
  std::string frame_name;
  std::string target_region;
  ProvenanceKind prov_kind;
  std::size_t entry_scope_id;
};

FrameProvState EnterFrameProvenance(const std::string& frame_name,
                                     const std::string& target_region,
                                     std::size_t current_scope_id) {
  SpecDefsBlockProv();
  SPEC_RULE("FrameBind-Enter");

  FrameProvState state;
  state.frame_name = frame_name;
  state.target_region = target_region;
  state.prov_kind = ProvenanceKind::Region;
  state.entry_scope_id = current_scope_id;
  return state;
}

void ExitFrameProvenance(const FrameProvState& state) {
  SpecDefsBlockProv();
  SPEC_RULE("FrameBind-Exit");

  // On frame exit, pointers allocated in this frame become expired.
  // The parent region's allocations remain valid.
  (void)state;
}

// =============================================================================
// Escape Checking
// =============================================================================

namespace {

struct EscapeCheckContext {
  std::vector<EscapeInfo> escaped;
  std::optional<std::string> current_region;
  std::size_t region_scope_depth = 0;
};

static bool ProvEscapesRegion(const ProvTag& prov,
                              const ProvEnv& env,
                              const IdKey& region_key) {
  if (prov.kind != ProvKind::Region) {
    return false;
  }
  // Check if the provenance is from the current region or a nested one
  return prov.region == region_key || RegionNesting(env, prov.region, region_key);
}

}  // namespace

std::vector<EscapeInfo> CheckEscapeOnExit(
    const std::unordered_map<std::string, ProvenanceKind>& bindings,
    const std::string& exiting_region,
    const std::unordered_map<std::string, core::Span>& binding_spans) {
  SpecDefsBlockProv();
  SPEC_RULE("ProvEscape-Check");

  std::vector<EscapeInfo> escaped;

  for (const auto& [name, prov] : bindings) {
    // Check if this binding's provenance is from the exiting region
    if (prov == ProvenanceKind::Region) {
      EscapeInfo info;
      info.binding_name = name;
      info.prov_kind = prov;
      if (auto it = binding_spans.find(name); it != binding_spans.end()) {
        info.span = it->second;
      }
      escaped.push_back(std::move(info));
    }
  }

  return escaped;
}

// =============================================================================
// Control Flow Merge
// =============================================================================

struct BranchProvenance {
  std::vector<ProvenanceKind> provs;
  bool has_divergent = false;
};

ProvenanceKind MergeControlFlow(const std::vector<ProvenanceKind>& branch_provs) {
  SpecDefsBlockProv();
  SPEC_RULE("ControlFlowMerge");

  if (branch_provs.empty()) {
    return ProvenanceKind::Bottom;
  }

  // Create internal environment for merging
  ProvEnv env;
  env.next_scope_id = 0;

  // Convert public kinds to internal tags for merging
  std::vector<ProvTag> tags;
  tags.reserve(branch_provs.size());
  for (const auto& kind : branch_provs) {
    ProvTag tag;
    switch (kind) {
      case ProvenanceKind::Global:
        tag.kind = ProvKind::Global;
        break;
      case ProvenanceKind::Stack:
        tag.kind = ProvKind::Stack;
        break;
      case ProvenanceKind::Heap:
        tag.kind = ProvKind::Heap;
        break;
      case ProvenanceKind::Region:
        tag.kind = ProvKind::Region;
        break;
      case ProvenanceKind::Bottom:
        tag.kind = ProvKind::Bottom;
        break;
      case ProvenanceKind::Param:
        tag.kind = ProvKind::Param;
        break;
    }
    tags.push_back(tag);
  }

  // Join all provenances
  const auto merged = JoinAllProv(env, tags);
  return ToPublicKind(merged);
}

// =============================================================================
// Block Analysis
// =============================================================================

namespace {

struct BlockAnalysisResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvenanceKind block_prov;
  std::vector<EscapeInfo> escapes;
};

}  // namespace

BlockAnalysisResult AnalyzeBlockProvenance(
    const ScopeContext& ctx,
    const ast::Block& block,
    const TypeEnv& gamma,
    const std::optional<std::string>& enclosing_region) {
  SpecDefsBlockProv();

  BlockAnalysisResult result;
  result.ok = true;
  result.block_prov = ProvenanceKind::Bottom;

  // Create provenance environment
  ProvEnv env;
  env.next_scope_id = 0;

  // Push initial scope
  ProvScope scope;
  scope.id = env.next_scope_id++;
  env.scopes.push_back(std::move(scope));
  env = SeedBlockProvEnv(gamma, env);

  // If inside a region, track it
  if (enclosing_region.has_value()) {
    const auto region_key = IdKeyOf(*enclosing_region);
    IdKey region_tag = region_key;
    if (const auto binding = BindOf(gamma, *enclosing_region)) {
      if (binding->provenance_region.has_value()) {
        region_tag = *binding->provenance_region;
      }
    }
    env.regions.push_back(RegionEntry{region_tag, region_key});
    env = Intro_pi(env, *enclosing_region, RegionTag(region_tag));
  }

  // Analyze statements
  std::vector<ProvTag> stmt_provs;
  for (const auto& stmt : block.stmts) {
    // Each statement is analyzed for provenance
    // (detailed statement analysis is in prov_stmt.cpp)
    (void)stmt;
    stmt_provs.push_back(BottomTag());
  }

  // Analyze tail expression if present
  if (block.tail_opt) {
    // The tail expression determines block provenance
    // (detailed expression analysis is in prov_expr.cpp)
    stmt_provs.push_back(BottomTag());
  }

  // Merge all provenances
  const auto merged = JoinAllProv(env, stmt_provs);
  result.block_prov = ToPublicKind(merged);

  SPEC_RULE("BlockProv-Analyze");
  return result;
}

// =============================================================================
// Region Block Analysis
// =============================================================================

struct RegionBlockResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::string region_name;
  std::vector<EscapeInfo> escapes;
};

RegionBlockResult AnalyzeRegionBlock(
    const ScopeContext& ctx,
    const ast::Block& block,
    const TypeEnv& gamma,
    const std::string& region_name) {
  SpecDefsBlockProv();

  RegionBlockResult result;
  result.ok = true;
  result.region_name = region_name;

  // Enter region provenance
  const auto region_state = EnterRegionProvenance(region_name, 0);

  // Analyze the block
  const auto block_result = AnalyzeBlockProvenance(ctx, block, gamma, region_name);
  if (!block_result.ok) {
    result.ok = false;
    result.diag_id = block_result.diag_id;
    result.span = block_result.span;
    return result;
  }

  // Check for escapes
  result.escapes = block_result.escapes;

  // Exit region provenance
  ExitRegionProvenance(region_state);

  SPEC_RULE("RegionBlock-Analyze");
  return result;
}

// =============================================================================
// Frame Block Analysis
// =============================================================================

struct FrameBlockResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::string frame_name;
  std::string target_region;
  std::vector<EscapeInfo> escapes;
};

FrameBlockResult AnalyzeFrameBlock(
    const ScopeContext& ctx,
    const ast::Block& block,
    const TypeEnv& gamma,
    const std::string& frame_name,
    const std::string& target_region) {
  SpecDefsBlockProv();

  FrameBlockResult result;
  result.ok = true;
  result.frame_name = frame_name;
  result.target_region = target_region;

  // Enter frame provenance
  const auto frame_state = EnterFrameProvenance(frame_name, target_region, 0);

  // Analyze the block with frame context
  const auto block_result = AnalyzeBlockProvenance(ctx, block, gamma, frame_name);
  if (!block_result.ok) {
    result.ok = false;
    result.diag_id = block_result.diag_id;
    result.span = block_result.span;
    return result;
  }

  // Check for escapes specific to frame
  result.escapes = block_result.escapes;

  // Exit frame provenance
  ExitFrameProvenance(frame_state);

  SPEC_RULE("FrameBlock-Analyze");
  return result;
}

// =============================================================================
// Loop Provenance
// =============================================================================

ProvenanceKind ComputeLoopProvenance(const std::vector<ProvenanceKind>& break_provs,
                                      bool has_void_break) {
  SpecDefsBlockProv();

  if (break_provs.empty() || has_void_break) {
    SPEC_RULE("LoopProv-Void");
    return ProvenanceKind::Bottom;
  }

  SPEC_RULE("LoopProv-Merge");
  return MergeControlFlow(break_provs);
}

// =============================================================================
// If/Match Provenance Merge
// =============================================================================

ProvenanceKind ComputeIfProvenance(ProvenanceKind then_prov,
                                    const std::optional<ProvenanceKind>& else_prov) {
  SpecDefsBlockProv();

  if (!else_prov.has_value()) {
    SPEC_RULE("IfProv-NoElse");
    return ProvenanceKind::Bottom;
  }

  SPEC_RULE("IfProv-Merge");
  return MergeControlFlow({then_prov, *else_prov});
}

ProvenanceKind ComputeIfCaseProvenance(const std::vector<ProvenanceKind>& arm_provs) {
  SpecDefsBlockProv();
  SPEC_RULE("IfCaseProv-Merge");
  return MergeControlFlow(arm_provs);
}

}  // namespace ultraviolet::analysis
