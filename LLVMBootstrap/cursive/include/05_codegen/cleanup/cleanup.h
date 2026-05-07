#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "04_analysis/typing/types.h"

namespace cursive::codegen {

// ============================================================================
// §6.8 Cleanup, Drop, and Unwinding
// ============================================================================

// A CleanupAction represents a single cleanup operation to be performed
// at scope exit (e.g., dropping a variable, releasing a region).
struct CleanupAction {
  enum class Kind {
    DropVar,       // Drop a local variable
    DropStatic,    // Drop an initialized static during init unwind
    DropTemp,      // Drop a temporary value
    DropField,     // Drop a field of a partially-moved record
    ReleaseRegion, // Release a region
    ReleaseKeyScope, // Release all keys held by a key scope
    ReacquireReleasedKey, // Reacquire a previously released outer key
    ParallelJoin,  // Join an active parallel context
    RunDefer,      // Execute a defer block
    RuntimeScopeExit, // Deactivate runtime scope tag
  };

  Kind kind = Kind::DropVar;
  std::string name;                    // Variable or temp name
  analysis::TypeRef type;                  // Type to drop
  std::optional<IRValue> value;        // Value to drop (if already computed)
  std::optional<IRPtr> defer_ir;       // Defer block IR (for Kind::RunDefer)
  ast::ModulePath static_module_path;  // Static owner module (for Kind::DropStatic)
  std::vector<std::string> skip_fields; // Fields to skip (for partial moves)
  std::uint64_t scope_runtime_id = 0;  // Scope id (for RuntimeScopeExit)
};

// A CleanupPlan is an ordered list of cleanup actions for a scope.
using CleanupPlan = std::vector<CleanupAction>;

// ============================================================================
// §6.8 CleanupPlan - Compute cleanup actions for a scope
// ============================================================================

// §6.8 CleanupPlan(scope) - Compute the ordered list of cleanup actions
CleanupPlan ComputeCleanupPlan(const std::vector<CleanupItem>& cleanup_items,
                               LowerCtx& ctx);

// Compute cleanup actions for the current scope
CleanupPlan ComputeCleanupPlanForCurrentScope(LowerCtx& ctx);

// Compute cleanup actions from current scope up to a loop scope
CleanupPlan ComputeCleanupPlanToLoopScope(LowerCtx& ctx);

// Compute cleanup actions from current scope to function root
CleanupPlan ComputeCleanupPlanToFunctionRoot(LowerCtx& ctx);

enum class CleanupTarget {
  CurrentScope,
  ToLoopScope,
  ToFunctionRoot,
};

// Compute cleanup actions for scopes outside the given target.
CleanupPlan ComputeCleanupPlanRemainder(CleanupTarget target, LowerCtx& ctx);

// §6.8 EmitCleanup - Emit IR for a cleanup plan
IRPtr EmitCleanup(const CleanupPlan& plan, LowerCtx& ctx);
IRPtr EmitCleanupOnPanic(const CleanupPlan& plan, LowerCtx& ctx);
IRPtr EmitCleanupWithRemainder(const CleanupPlan& plan,
                               const CleanupPlan& remainder,
                               LowerCtx& ctx);

// ============================================================================
// §6.8 EmitDrop - Emit IR to drop a value of a given type
// ============================================================================

// §6.8 EmitDrop(T, v) - Emit IR that correctly drops a value of type T
IRPtr EmitDrop(const analysis::TypeRef& type, const IRValue& value, LowerCtx& ctx);

// §6.8 EmitDropInline(T, v) - Emit IR that drops a value without routing
// through drop glue indirection. Used for static deinit/unwind paths that must
// materialize concrete cleanup directly in the current procedure body.
IRPtr EmitDropInline(const analysis::TypeRef& type,
                     const IRValue& value,
                     LowerCtx& ctx);

// §6.8 EmitDropFields - Emit IR to drop specific fields of a record
IRPtr EmitDropFields(const analysis::TypeRef& type,
                     const IRValue& value,
                     const std::vector<std::string>& skip_fields,
                     LowerCtx& ctx);

// ============================================================================
// §6.8 Drop Glue
// ============================================================================

// §6.12.13 DropGlueSym(T) - Symbol for drop glue function
std::string DropGlueSym(const analysis::TypeRef& type, LowerCtx& ctx);

// §6.12.13 DropGlueIR(T) - IR for drop glue function body
IRPtr DropGlueIR(const analysis::TypeRef& type, LowerCtx& ctx);

// §6.12.13 EmitDropGlue(T) - Emit drop glue function declaration
ProcIR EmitDropGlue(const analysis::TypeRef& type, LowerCtx& ctx);

// ============================================================================
// §6.8 DropOnAssign - Drop value before assignment
// ============================================================================

// §6.8 DropOnAssign(x, slot) - Emit drop IR before overwriting a binding
IRPtr DropOnAssign(const std::string& name,
                   const IRPlace& slot,
                   LowerCtx& ctx);

// §6.8 DropOnAssignApplicable - Check if drop-on-assign applies
bool DropOnAssignApplicable(const std::string& name, LowerCtx& ctx);

// §6.8 DropOnAssignRoot - Check if drop should occur at assignment root
bool DropOnAssignRoot(const ast::Expr& place, LowerCtx& ctx);

// ============================================================================
// §6.8 Binding Validity State
// ============================================================================

enum class BindValidity {
  Valid,          // Fully valid, not moved
  PartiallyMoved, // Some fields moved
  Moved,          // Fully moved
};

// §6.8 BindValid(x) - Get validity state of a binding
BindValidity GetBindValidity(const std::string& name, LowerCtx& ctx);

// §6.8 Get moved fields for a partially moved binding
std::vector<std::string> GetMovedFields(const std::string& name,
                                        LowerCtx& ctx);

// ============================================================================
// Anchor function for SPEC_RULE markers
// ============================================================================

void AnchorCleanupRules();

}  // namespace cursive::codegen
