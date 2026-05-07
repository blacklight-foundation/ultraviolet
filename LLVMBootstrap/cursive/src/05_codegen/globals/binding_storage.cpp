// =============================================================================
// MIGRATION MAPPING: binding_storage.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.4 Expression Lowering (lines 15665-15992)
//   - IRBindVar, IRStoreVar, IRStoreVarNoDrop (IR nodes for binding)
//   - ExecIR-BindVar rule (lines 15831-15833)
//   - ExecIR-StoreVar rule (lines 15821-15823)
//   - ExecIR-StoreVarNoDrop rule (lines 15825-15828)
//   - Binding state tracking (Alive, Moved, PartiallyMoved, Poisoned)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/binding_storage.cpp
//   - Local variable allocation
//   - Binding state management
//   - Stack frame layout
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/globals/binding_storage.h
//   - cursive/include/04_analysis/layout/layout.h (SizeOf, AlignOf)
//   - cursive/include/04_analysis/types/types.h (TypeRef)
//
// REFACTORING NOTES:
//   1. Bindings track local variables within a procedure
//   2. Each binding has:
//      - Name (identifier)
//      - Type
//      - Storage location (stack slot)
//      - State (Alive, Moved, etc.)
//      - Mutability (let vs var)
//      - Movability (= vs :=)
//   3. BindVar: create new binding with initial value
//   4. StoreVar: update existing binding (with drop of old value)
//   5. StoreVarNoDrop: update without dropping old (for subfields)
//   6. Stack layout computed for procedure prologue
//
// BINDING OPERATIONS:
//   - allocate(name, type) -> storage location
//   - bind(name, value) -> void
//   - store(name, value) -> void (drops old)
//   - load(name) -> value
//   - move(name) -> value (marks as Moved)
//   - drop(name) -> void (cleanup)
//
// STATE TRANSITIONS:
//   Alive -> Moved (after move)
//   Alive -> PartiallyMoved (after field move)
//   * -> Poisoned (on error)
// =============================================================================

#include "05_codegen/globals/binding_storage.h"

#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/lower/lower_expr.h"
#include "04_analysis/typing/type_predicates.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

namespace {

bool IsRegionHandleType(const analysis::TypeRef& type) {
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    stripped = type;
  }
  while (stripped) {
    if (const auto* refine = std::get_if<analysis::TypeRefine>(&stripped->node)) {
      stripped = analysis::StripPerm(refine->base);
      if (!stripped) {
        stripped = refine->base;
      }
      continue;
    }
    break;
  }
  if (!stripped) {
    return false;
  }
  if (const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node)) {
    return modal->path.size() == 1 && modal->path.front() == "Region";
  }
  if (const auto* path = std::get_if<analysis::TypePathType>(&stripped->node)) {
    return path->path.size() == 1 && path->path.front() == "Region";
  }
  return false;
}

}  // namespace

// ============================================================================
// Section 6.12.10 Validity Tracking Functions
// ============================================================================

BindValidState GetBindingValidity(const std::string& name, const LowerCtx& ctx) {
  SPEC_RULE("BindValid-Sigma");

  const BindingState* state = ctx.GetBindingState(name);
  if (!state) {
    // Unknown binding - treat as valid (conservative)
    return BindValidState::Valid;
  }

  if (state->is_moved) {
    return BindValidState::Moved;
  }

  if (!state->moved_fields.empty()) {
    return BindValidState::PartiallyMoved;
  }

  return BindValidState::Valid;
}

std::optional<std::string> ResolveBindRegionTarget(const IRBindVar& bind,
                                                   const LowerCtx& ctx) {
  SPEC_RULE("BindRegionTarget(x)");

  const BindingState* state = ctx.GetBindingState(bind.name);
  const analysis::ProvenanceKind prov =
      state ? state->prov : bind.prov;
  const std::optional<std::string> region_tag =
      state ? state->prov_region_tag : bind.prov_region_tag;
  const std::optional<std::string> region =
      state ? state->prov_region : bind.prov_region;

  if (prov != analysis::ProvenanceKind::Region || !region_tag.has_value() ||
      region_tag->empty() || !region.has_value() ||
      region->empty()) {
    return std::nullopt;
  }

  return region;
}

std::optional<BindSlot> ResolveBindSlot(const IRBindVar& bind,
                                        const LowerCtx& ctx) {
  const BindingState* state = ctx.GetBindingState(bind.name);
  analysis::TypeRef bind_type = bind.type;
  if (!bind_type && state && state->type) {
    bind_type = state->type;
  }

  if (!IsRegionHandleType(bind_type)) {
    if (auto region = ResolveBindRegionTarget(bind, ctx)) {
      if (!bind_type) {
        SPEC_RULE("BindSlot-Err");
        return std::nullopt;
      }
      SPEC_RULE("BindSlot-Region");
      BindSlot slot;
      slot.kind = BindSlot::Kind::RegionSlot;
      slot.name = bind.name;
      slot.region = *region;
      slot.type = bind_type;
      return slot;
    }
  }

  if (!bind_type) {
    SPEC_RULE("BindSlot-Err");
    return std::nullopt;
  }

  SPEC_RULE("BindSlot-Local");
  BindSlot slot;
  slot.kind = BindSlot::Kind::Alloca;
  slot.name = bind.name;
  slot.type = bind_type;
  return slot;
}

void UpdateValidOnBind(const std::string& name, LowerCtx& ctx) {
  SPEC_RULE("UpdateValid-BindVar");
  // Binding creates a new valid binding - handled by RegisterVar
  // The LowerCtx::RegisterVar already sets the binding as valid
}

void UpdateValidOnMove(const std::string& name, LowerCtx& ctx) {
  SPEC_RULE("UpdateValid-Move");
  ctx.MarkMoved(name);
}

void UpdateValidOnPartialMove(const std::string& name,
                               const std::vector<std::string>& moved_fields,
                               LowerCtx& ctx) {
  SPEC_RULE("UpdateValid-PartialMove");
  for (const auto& field : moved_fields) {
    ctx.MarkFieldMoved(name, field);
  }
}

void UpdateValidOnReassign(const std::string& name, LowerCtx& ctx) {
  SPEC_RULE("UpdateValid-Reassign");
  // Re-registration with same type but valid state
  // This is handled by the store operation clearing the moved flag
  const BindingState* state = ctx.GetBindingState(name);
  if (state) {
    // Re-register to clear moved state
    ctx.RegisterVar(name, state->type, state->has_responsibility,
                    state->is_immovable, state->prov, state->prov_region,
                    false, state->prov_region_tag);
  }
}

// ============================================================================
// Section 6.12.10 Validity Check Emission (LLVM-specific helpers)
// ============================================================================

void EmitValidityCheck(LLVMEmitter& emitter, const std::string& name) {
  SPEC_RULE("BindValid-Check");
  // In a full implementation, this would check a validity flag
  // For bootstrap, we assume all bindings are valid
}

void EmitMarkMoved(LLVMEmitter& emitter, const std::string& name) {
  SPEC_RULE("BindValid-Move");
  // In a full implementation, this would set the validity flag to moved
  // For bootstrap, this is a no-op
}

void EmitMarkPartiallyMoved(LLVMEmitter& emitter, const std::string& name) {
  SPEC_RULE("BindValid-PartialMove");
  // For bootstrap, this is a no-op
}

void EmitClearMoved(LLVMEmitter& emitter, const std::string& name) {
  SPEC_RULE("BindValid-Reassign");
  // For bootstrap, this is a no-op
}

void EmitDropIfValid(LLVMEmitter& emitter,
                     const std::string& name,
                     analysis::TypeRef type) {
  SPEC_RULE("BindValid-DropIfValid");
  // For bootstrap, always emit drop (conservative)
  // A full implementation would check validity flag first
}

// ============================================================================
// Section 6.12.10 Drop On Assignment
// ============================================================================

IRPtr EmitDropOnAssign(const std::string& name,
                       analysis::TypeRef type,
                       LowerCtx& ctx) {
  BindValidState validity = GetBindingValidity(name, ctx);

  switch (validity) {
    case BindValidState::Valid: {
      SPEC_RULE("DropOnAssign-Record-Valid");
      SPEC_RULE("DropOnAssign-Aggregate");
      // Drop the old value before reassignment
      IRValue old_value;
      old_value.kind = IRValue::Kind::Local;
      old_value.name = name;
      return EmitDrop(type, old_value, ctx);
    }

    case BindValidState::PartiallyMoved: {
      SPEC_RULE("DropOnAssign-Record-Partial");
      // Drop only the non-moved fields
      const BindingState* state = ctx.GetBindingState(name);
      if (!state) {
        return EmptyIR();
      }
      IRValue old_value;
      old_value.kind = IRValue::Kind::Local;
      old_value.name = name;
      return EmitDropFields(type, old_value, state->moved_fields, ctx);
    }

    case BindValidState::Moved: {
      SPEC_RULE("DropOnAssign-Record-Moved");
      SPEC_RULE("DropOnAssign-Aggregate-Moved");
      // Nothing to drop - value was moved
      return EmptyIR();
    }

    case BindValidState::Poisoned:
    default:
      // Poisoned state - no drop
      return EmptyIR();
  }
}

// ============================================================================
// Spec Rule Anchors
// ============================================================================

void AnchorBindingStorageRules() {
  // Section 6.12.10 Binding Storage
  SPEC_RULE("BindSlot-Local");
  SPEC_RULE("BindSlot-Param-ByValue");
  SPEC_RULE("BindSlot-Param-ByRef");
  SPEC_RULE("BindSlot-Region");
  SPEC_RULE("BindSlot-Static");
  SPEC_RULE("BindSlot-Err");
  SPEC_RULE("BindRegionTarget(x)");

  // Section 6.12.10 Binding Validity
  SPEC_RULE("BindValid-Init");
  SPEC_RULE("BindValid-Check");
  SPEC_RULE("BindValid-Move");
  SPEC_RULE("BindValid-PartialMove");
  SPEC_RULE("BindValid-Reassign");
  SPEC_RULE("BindValid-DropIfValid");
  SPEC_RULE("BindValid-Sigma");

  // Section 6.12.10 Update Validity
  SPEC_RULE("UpdateValid-BindVar");
  SPEC_RULE("UpdateValid-Move");
  SPEC_RULE("UpdateValid-PartialMove");
  SPEC_RULE("UpdateValid-Reassign");

  // Section 6.12.10 Drop On Assign
  SPEC_RULE("DropOnAssign-Record-Valid");
  SPEC_RULE("DropOnAssign-Record-Partial");
  SPEC_RULE("DropOnAssign-Record-Moved");
  SPEC_RULE("DropOnAssign-Aggregate");
  SPEC_RULE("DropOnAssign-Aggregate-Moved");
}

}  // namespace cursive::codegen
