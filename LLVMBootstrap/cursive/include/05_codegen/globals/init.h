#pragma once

// ============================================================================
// §6.7 Module Initialization and Deinitialization Ordering
// ============================================================================
// This header declares the module initialization ordering functionality for
// Cursive codegen. This includes:
// - Computing initialization order based on module dependencies
// - Detecting initialization cycles
// - Generating init/deinit function IR
// - Panic handling during initialization
//
// Related spec sections:
// - §6.7 Globals and Initialization (GlobalsJudg)
// - §A7.1 Initialization Order and Poisoning
// - §7.8 Interpreter Entrypoint (Project-Level)
// ============================================================================

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "02_source/ast/ast.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// ============================================================================
// §6.7 Module Dependency Analysis
// ============================================================================

// Type alias for module dependency edges (from_index, to_index)
using ModuleDepEdge = std::pair<std::size_t, std::size_t>;

// (ValueDepsEager): Compute eager value dependencies for a module
// Returns indices into the module list that the given module depends on
std::vector<std::size_t> ValueDepsEager(
    const ast::ModulePath& module_path,
    const ast::ASTModule& module,
    const std::vector<ast::ModulePath>& all_modules,
    const analysis::Sigma& sigma);

// (InitOrder): Compute topological initialization order
// Returns modules in order such that dependencies are initialized first.
// Returns empty vector if a cycle is detected (InitOrder-Err).
std::vector<ast::ModulePath> ComputeInitOrder(
    const std::vector<ast::ModulePath>& modules,
    const std::vector<ModuleDepEdge>& edges);

// (InitOrderFromSigma): Compute initialization order from Sigma
// Extracts modules and dependency edges from Sigma and computes order
std::vector<ast::ModulePath> ComputeInitOrderFromSigma(
    const analysis::Sigma& sigma);

// ============================================================================
// §A7.1 Initialization State Tracking
// ============================================================================

// Module initialization states as defined in the spec
enum class ModuleInitState {
  Pending,       // Not yet initialized
  Initializing,  // Currently being initialized (cycle detection)
  Ready,         // Successfully initialized
  Poisoned,      // Initialization failed (panic)
};

// (Init-Start): Begin module initialization
// (Init-Step): Execute next static initializer
// (Init-Next-Module): Advance to next module in order
// (Init-Panic): Handle panic during initialization
// (Init-Done): Complete initialization sequence
// (Init-Ok): Success state
// (Init-Fail): Failure state (propagate first panic)

// Track initialization state for a module
struct ModuleInitTracker {
  std::vector<ast::ModulePath> order;
  std::vector<ModuleInitState> states;
  std::size_t current_index = 0;
  bool panicked = false;
  std::optional<std::string> panic_module;

  // Advance to next module (Init-Next-Module)
  bool AdvanceToNext();

  // Mark current module as ready (Init-Ok)
  void MarkReady();

  // Mark current module as poisoned and propagate (Init-Panic)
  void MarkPoisoned(const std::string& reason);

  // Check if all modules are ready (Init-Done)
  bool AllReady() const;
};

// ============================================================================
// §6.7 Static Initializer Helpers
// ============================================================================

// (StaticInitExprs): Get all static init expressions from a module
std::vector<const ast::Expr*> StaticInitExprs(const ast::ASTModule& module);

// (StaticHasResponsibility): Check if static decl has cleanup responsibility
// Returns true if the binding owns the value (not a borrowed reference)
bool StaticHasResponsibility(const ast::StaticDecl& decl);

// (InitList): Get initialization expressions in declaration order
std::vector<const ast::Expr*> InitList(const ast::ASTModule& module);

// ============================================================================
// §6.7 Panic Handling During Init/Deinit
// ============================================================================

// (InitPanicHandle): Generate IR to handle panic during init
// Creates IR that checks panic state and marks dependent modules as poisoned
IRPtr InitPanicHandle(const std::string& module_name, LowerCtx& ctx);

// (PanicCheck): Generate IR to check panic flag and return early if set
IRPtr PanicCheck(LowerCtx& ctx);

// Generate context-specific panic followup after an operation that can set panic.
IRPtr PanicFollowup(LowerCtx& ctx);

// Emit cleanup actions for statics that finished initialization earlier in the
// current module-init body.
CleanupPlan ActiveStaticInitCleanupPlan(const LowerCtx& ctx);

// ============================================================================
// §6.7 Poison Checking
// ============================================================================

// (CheckPoison): Generate IR to check if a module is poisoned
// Used to skip deinitialization of poisoned modules
IRPtr EmitCheckPoisonIR(const std::string& module_name);

// ============================================================================
// §6.7 Initialization Order Validation
// ============================================================================

// (ValidateInitOrder): Check that initialization order is valid
// Returns true if the order respects all dependencies
bool ValidateInitOrder(const std::vector<ast::ModulePath>& order,
                       const std::vector<ModuleDepEdge>& edges,
                       const std::vector<ast::ModulePath>& all_modules);

// (DetectInitCycle): Detect cycle in module dependencies
// Returns the cycle path if one exists, empty vector otherwise
std::vector<ast::ModulePath> DetectInitCycle(
    const std::vector<ast::ModulePath>& modules,
    const std::vector<ModuleDepEdge>& edges);

// ============================================================================
// Spec Rule Anchors
// ============================================================================

// Emits SPEC_RULE anchors for §6.7 and §A7.1 init ordering rules
// (Already declared in globals.h as AnchorInitRules)

}  // namespace cursive::codegen
