#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "00_core/span.h"
#include "04_analysis/memory/borrow_bind.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct ProvCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

enum class ProvenanceKind {
  Global,
  Stack,
  Heap,
  Region,
  Bottom,
  Param,
};

struct ExprProvMapResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ExprProvenanceMap expr_prov;
  ExprRegionMap expr_region_tags;
  ExprRegionMap expr_region_targets;
};

struct BodyMemoryCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::uint64_t borrow_ms = 0;
  std::uint64_t provenance_ms = 0;
};

struct ProvExprTrackResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvenanceKind kind = ProvenanceKind::Bottom;
  std::optional<std::string> region;
  std::optional<std::string> region_target;
  bool fresh_region = false;
};

struct ProvStmtTrackResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvenanceKind kind = ProvenanceKind::Bottom;
  std::optional<std::string> region;
  std::optional<std::string> region_target;
  bool fresh_region = false;
};

struct ProvAssignCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvenanceKind place_kind = ProvenanceKind::Bottom;
  ProvenanceKind value_kind = ProvenanceKind::Bottom;
  bool escapes = false;
};

struct ProvReturnCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvenanceKind kind = ProvenanceKind::Bottom;
  std::optional<std::string> region;
  std::optional<std::string> region_target;
};

TypeRef RegionActiveTypeRef();
TypeRef RegionOptionsTypeRef();
ast::ExprPtr MakeDefaultRegionOptionsExpr();
bool RegionActiveType(const TypeRef& type);
std::optional<std::string> InnermostActiveRegion(const TypeEnv& env);
std::string FreshRegionName(const TypeEnv& env);

ProvCheckResult ProvBindCheck(const ScopeContext& ctx,
                              const ast::ModulePath& module_path,
                              const std::vector<ast::Param>& params,
                              const std::shared_ptr<ast::Block>& body,
                              const std::optional<BindSelfParam>& self_param,
                              core::DiagnosticStream* diags = nullptr);

BodyMemoryCheckResult CheckBodyMemory(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Block>& body,
    const std::optional<BindSelfParam>& self_param,
    core::DiagnosticStream* diags = nullptr,
    bool collect_timing = false);

// Debug-only profiling summary for provenance checks.
void LogProvenancePerfSummary();

ExprProvMapResult ComputeExprProvenanceMap(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Block>& body,
    const std::optional<BindSelfParam>& self_param);

// Expression provenance tracking (prov_expr.cpp)
ProvExprTrackResult TrackExprProvenance(const ScopeContext& ctx,
                                         const ast::ExprPtr& expr,
                                         const TypeEnv& gamma);

ProvenanceKind MergeProvenance(ProvenanceKind a, ProvenanceKind b);

ProvenanceKind AddressOfProvenance(const ScopeContext& ctx,
                                    const ast::ExprPtr& place,
                                    const TypeEnv& gamma);

ProvenanceKind DerefProvenance(const ScopeContext& ctx,
                                const ast::ExprPtr& ptr,
                                const TypeEnv& gamma);

ProvenanceKind AllocProvenance(const std::optional<std::string>& region_name,
                                const TypeEnv& gamma);

// Place expression provenance tracking (prov_place.cpp)
bool IsPlaceExpr(const ast::ExprPtr& expr);

std::optional<std::string> GetPlaceRoot(const ast::ExprPtr& place);

bool ValidatePlaceAccess(const ast::ExprPtr& place, Permission perm);

bool PlaceOverlaps(const ast::ExprPtr& a, const ast::ExprPtr& b);

ProvenanceKind ComputePlaceProvenance(const ScopeContext& ctx,
                                       const ast::ExprPtr& place,
                                       const TypeEnv& gamma);

// Statement provenance tracking (prov_stmt.cpp)
ProvStmtTrackResult TrackBindingProvenance(const ScopeContext& ctx,
                                            const ast::Binding& binding,
                                            const TypeEnv& gamma);

ProvAssignCheckResult TrackAssignmentProvenance(const ScopeContext& ctx,
                                                 const ast::ExprPtr& place,
                                                 const ast::ExprPtr& value,
                                                 const TypeEnv& gamma);

ProvReturnCheckResult TrackReturnProvenance(const ScopeContext& ctx,
                                             const ast::ExprPtr& value,
                                             const TypeEnv& gamma);

}  // namespace ultraviolet::analysis
