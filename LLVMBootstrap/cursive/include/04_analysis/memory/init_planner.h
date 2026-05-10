#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "00_core/diagnostics.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct InitGraph {
  std::vector<ast::ModulePath> modules;
  std::vector<std::pair<std::size_t, std::size_t>> type_edges;
  std::vector<std::pair<std::size_t, std::size_t>> eager_edges;
  std::vector<std::pair<std::size_t, std::size_t>> lazy_edges;
};

struct InitPlan {
  InitGraph graph;
  std::vector<ast::ModulePath> init_order;
  bool topo_ok = false;
};

struct InitPlanResult {
  bool ok = false;
  core::DiagnosticStream diags;
  InitPlan plan;
};

InitPlanResult BuildInitPlan(const ScopeContext& ctx,
                             const NameMapTable& name_maps);

}  // namespace cursive::analysis
