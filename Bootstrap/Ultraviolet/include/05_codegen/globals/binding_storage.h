#pragma once

// ============================================================================
// §6.12.10 Binding Storage and Validity Tracking
// ============================================================================
// This header declares the binding storage and validity tracking functionality
// for the Ultraviolet codegen phase. This includes:
// - BindSlot: Determining storage location for bindings
// - BindValid: Tracking validity state of bindings
// - UpdateValid: State transitions for binding validity
// - DropOnAssign: Generating cleanup IR for reassignment
//
// BindStorageJudg = {BindSlot(x) => slot, BindValid(x) => v,
//                    UpdateValid(x, op) => v', DropOnAssign(x, slot) => IR}
// ============================================================================

#include <optional>
#include <string>
#include <vector>

#include "04_analysis/typing/types.h"
#include "05_codegen/globals/globals.h"

// Forward declarations
namespace ultraviolet::codegen {
class LLVMEmitter;
}

namespace ultraviolet::codegen {

// ============================================================================
// §6.12.10 Binding Validity States
// ============================================================================

// Validity states for bindings as defined in the spec
enum class BindValidState {
  Valid,           // Binding is valid and usable
  Moved,           // Binding has been moved
  PartiallyMoved,  // Some fields have been moved
  Poisoned,        // Error state
};

// ============================================================================
// §6.12.10 Binding Storage Slot Types
// ============================================================================

// Represents the storage location for a binding
struct BindSlot {
  enum class Kind {
    Alloca,       // Stack allocation (BindSlot-Local, BindSlot-Param-ByValue)
    ParamRef,     // Parameter passed by reference (BindSlot-Param-ByRef)
    RegionSlot,   // Allocation in a region (BindSlot-Region)
    GlobalSym,    // Static/global symbol (BindSlot-Static)
    AsyncFrame,   // Slot within async frame for resume functions
  };

  Kind kind = Kind::Alloca;
  std::string name;           // Binding name
  std::string symbol;         // For GlobalSym: mangled symbol
  std::string region;         // For RegionSlot: region name
  analysis::TypeRef type;     // Type of the binding
};

// ============================================================================
// §6.12.10 Validity Tracking Functions
// ============================================================================

// (BindValid-Sigma): Check validity state of a binding
// Returns the current validity state for the named binding
BindValidState GetBindingValidity(const std::string& name, const LowerCtx& ctx);

// Resolve the lowered storage class for a bind operation.
std::optional<BindSlot> ResolveBindSlot(const IRBindVar& bind,
                                        const LowerCtx& ctx);

// Resolve the concrete region target name for a region-backed binding.
std::optional<std::string> ResolveBindRegionTarget(const IRBindVar& bind,
                                                   const LowerCtx& ctx);

// (UpdateValid-BindVar): Update validity after binding
// Sets the binding to Valid state
void UpdateValidOnBind(const std::string& name, LowerCtx& ctx);

// (UpdateValid-Move): Update validity after move
// Sets the binding to Moved state
void UpdateValidOnMove(const std::string& name, LowerCtx& ctx);

// (UpdateValid-PartialMove): Update validity after partial move
// Sets the binding to PartiallyMoved state with the given fields
void UpdateValidOnPartialMove(const std::string& name,
                               const std::vector<std::string>& moved_fields,
                               LowerCtx& ctx);

// (UpdateValid-Reassign): Update validity on reassignment
// Clears moved state and sets to Valid
void UpdateValidOnReassign(const std::string& name, LowerCtx& ctx);

// ============================================================================
// §6.12.10 Validity Check Emission (LLVM-specific)
// ============================================================================

// (BindValid-Check): Emit runtime validity check
// In bootstrap implementation, this is a no-op (assumes all bindings valid)
void EmitValidityCheck(LLVMEmitter& emitter, const std::string& name);

// (BindValid-Move): Emit IR to mark a binding as moved
void EmitMarkMoved(LLVMEmitter& emitter, const std::string& name);

// (BindValid-PartialMove): Emit IR to mark a binding as partially moved
void EmitMarkPartiallyMoved(LLVMEmitter& emitter, const std::string& name);

// (BindValid-Reassign): Emit IR to clear moved state on reassignment
void EmitClearMoved(LLVMEmitter& emitter, const std::string& name);

// (BindValid-DropIfValid): Emit IR to drop if binding is still valid
// In bootstrap, always emits drop (conservative behavior)
void EmitDropIfValid(LLVMEmitter& emitter,
                     const std::string& name,
                     analysis::TypeRef type);

// ============================================================================
// §6.12.10 Drop On Assignment
// ============================================================================

// (DropOnAssign-Record-Valid): Drop before reassignment of valid record
// (DropOnAssign-Record-Partial): Partial drop for partially moved record
// (DropOnAssign-Record-Moved): No-op for fully moved record
// (DropOnAssign-Aggregate): Drop aggregate types (arrays, tuples, unions, modals)
// (DropOnAssign-Aggregate-Moved): No-op for moved aggregate
IRPtr EmitDropOnAssign(const std::string& name,
                       analysis::TypeRef type,
                       LowerCtx& ctx);

// ============================================================================
// Spec Rule Anchors
// ============================================================================

// Emits SPEC_RULE anchors for §6.12.10 binding storage rules
void AnchorBindingStorageRules();

}  // namespace ultraviolet::codegen
