#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "01_project/project.h"
#include "02_source/ast/ast.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/typecheck.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::driver {

// Lowered module state cached across per-module object/IR emission.
struct ModuleCodegen {
  ast::ModulePath path;
  std::string path_key;
  codegen::IRDecls decls;
  codegen::LowerValueState values;
  std::unordered_map<std::string, codegen::LowerCtx::ProcSigInfo> proc_sigs;
  std::unordered_map<std::string, codegen::LinkageKind> proc_linkages;
  std::unordered_map<std::string, codegen::LowerCtx::AsyncProcInfo> async_procs;
  std::uint64_t temp_counter = 0;
  std::optional<std::string> main_symbol;
};

// Driver-owned codegen state. LowerCtx remains the codegen context; this cache
// owns cross-module orchestration, output filtering, and concurrency state.
struct CodegenCache {
  enum class ModuleState : std::uint8_t {
    Pending,
    InProgress,
    Done,
    Failed,
  };

  codegen::LowerCtx ctx;
  const analysis::NameMapBuildResult* name_maps = nullptr;
  std::unordered_map<std::string, codegen::LowerCtx::HostedStateTemplate>
      hosted_state_templates;
  std::vector<codegen::LowerCtx::HostedExportInfo> all_hosted_exports;
  std::optional<analysis::InitPlan> full_init_plan;
  std::vector<std::string> hosted_project_modules;
  std::vector<std::shared_ptr<ModuleCodegen>> modules;
  std::unordered_map<std::string, std::size_t> index;
  std::unordered_map<std::string, const ast::ASTModule*> ast_modules;
  std::unordered_map<std::string, std::size_t> module_order;
  std::unordered_map<std::string, std::shared_ptr<ModuleCodegen>>
      module_entries;
  std::unordered_map<std::string, ModuleState> module_states;
  std::unordered_set<std::string> lowered_proc_symbols;
  std::string active_project_context_key;
  std::uint64_t emit_context_epoch = 0;
  mutable std::mutex module_mu;
  std::condition_variable module_cv;
  std::atomic<bool> ok{true};
};

}  // namespace ultraviolet::driver
