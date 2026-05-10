// =============================================================================
// MIGRATION MAPPING: poison_instrument.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 7.1 Initialization Order and Poisoning
//   - SetPoison judgment (referenced in InitPanicHandle)
//   - Module poison propagation
//   - InitPanicHandle rule (line 16989)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/checks.cpp
//   - Lines 18-72: PoisonSetFor helper
//   - Computes transitive closure of dependent modules
//   - Uses init_modules and init_eager_edges from context
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/poison_instrument.cpp
//   - LLVM emission of poison flags (GetOrCreatePoisonFlag)
//   - EmitSetPoison
//   - LLVMEmitter::EmitPoisonCheck
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/checks/poison_instrument.h
//   - cursive/include/05_codegen/ir/ir_model.h (IRSetPoison)
//   - cursive/include/00_core/symbols.h (StringOfPath)
//   - cursive/include/05_codegen/llvm/llvm_emit.h
//   - cursive/include/05_codegen/llvm/llvm_ir_panic.h
//   - cursive/include/04_analysis/layout/layout.h (RecordLayoutOf)
//
// REFACTORING NOTES:
//   1. Poisoning marks modules as failed during init
//   2. PoisonSetFor computes which modules to poison
//   3. Uses graph reachability from module node
//   4. Prevents use of partially-initialized modules
//   5. Set includes module itself + transitive dependents
//
// POISON SET COMPUTATION:
//   PoisonSetFor(module, ctx):
//     1. Build module index map
//     2. Build reverse edge graph from init_eager_edges
//     3. DFS from target module
//     4. Collect all reachable modules
//     5. Return module paths as strings
//
// GRAPH TRAVERSAL:
//   - init_modules: ordered list of all modules
//   - init_eager_edges: (from, to) dependency pairs
//   - Traverse reverse eager dependency edges from module
//   - Mark visited, collect reachable set
//
// USAGE IN INIT:
//   If module A's init panics:
//     1. Compute PoisonSetFor(A)
//     2. Mark all modules in set as poisoned
//     3. Any access to poisoned module -> panic
//
// LLVM EMISSION (from poison_instrument.cpp):
//   - GetOrCreatePoisonFlag: creates global bool for module poison state
//   - EmitSetPoison: stores value to poison flag
//   - EmitPoisonCheck: branch on poison flag, panic if poisoned
// =============================================================================

#include "05_codegen/checks/poison_instrument.h"
#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/llvm_ir_panic.h"
#include "05_codegen/checks/checks.h"
#include "04_analysis/typing/types.h"
#include "00_core/assert_spec.h"
#include "00_core/symbols.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"

#include <unordered_map>
#include <vector>

namespace cursive::codegen {

// Compute the set of modules to poison on initialization failure
// Returns module paths that should be poisoned if the given module's init fails
std::vector<std::string> PoisonSetFor(const std::string& module_path,
                                      const LowerCtx& ctx) {
  SPEC_RULE("PoisonSetFor");

  std::vector<std::string> out;
  if (ctx.init_modules.empty()) {
    out.push_back(module_path);
    return out;
  }

  // Build module index map
  std::unordered_map<std::string, std::size_t> index;
  index.reserve(ctx.init_modules.size());
  for (std::size_t i = 0; i < ctx.init_modules.size(); ++i) {
    index.emplace(core::StringOfPath(ctx.init_modules[i]), i);
  }

  const auto it = index.find(module_path);
  if (it == index.end()) {
    out.push_back(module_path);
    return out;
  }

  const std::size_t target = it->second;
  const std::size_t n = ctx.init_modules.size();

  // Build reverse edge graph from init_eager_edges. Edges are (dependent,
  // dependency), and PoisonSet(m) contains modules that can reach m.
  std::vector<std::vector<std::size_t>> dependents(n);
  for (const auto& edge : ctx.init_eager_edges) {
    if (edge.first < n && edge.second < n) {
      dependents[edge.second].push_back(edge.first);
    }
  }

  // DFS from target module to find all reverse-reachable dependents.
  std::vector<char> visited(n, false);
  std::vector<std::size_t> stack;
  visited[target] = true;
  stack.push_back(target);
  while (!stack.empty()) {
    const std::size_t cur = stack.back();
    stack.pop_back();
    for (const auto succ : dependents[cur]) {
      if (!visited[succ]) {
        visited[succ] = true;
        stack.push_back(succ);
      }
    }
  }

  // Collect all reachable module paths
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (visited[i]) {
      out.push_back(core::StringOfPath(ctx.init_modules[i]));
    }
  }
  if (out.empty()) {
    out.push_back(module_path);
  }
  return out;
}

// T-LLVM-016: Poisoning Instrumentation
// Implements module poison tracking for failed static initialization.

// Set poison flag for a binding
void EmitSetPoison(LLVMEmitter& emitter, const std::string& module_name, bool value) {
  SPEC_RULE("PoisonFlag-Set");

  std::vector<std::string> module_path = SplitModulePathString(module_name);
  llvm::Value* flag = GetPoisonFlagPtr(emitter, module_path);

  auto* builder = static_cast<llvm::IRBuilder<>*>(emitter.GetBuilderRaw());
  llvm::Type* bool_ty = emitter.GetLLVMType(analysis::MakeTypePrim("bool"));
  llvm::Value* val = llvm::ConstantInt::get(bool_ty, value ? 1 : 0);
  if (!flag) {
    if (LowerCtx* ctx = emitter.GetCurrentCtx()) {
      ctx->ReportCodegenFailure();
    }
    return;
  }
  builder->CreateStore(val, flag);
}

// LLVMEmitter::EmitPoisonCheck is implemented in llvm_emit.cpp as it's a method
// of LLVMEmitter. The supporting utilities are in llvm_ir_panic.cpp.

}  // namespace cursive::codegen
