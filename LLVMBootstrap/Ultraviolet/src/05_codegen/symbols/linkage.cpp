// =============================================================================
// MIGRATION MAPPING: linkage.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.3.4 Linkage for Generated Symbols (lines 15550-15663)
//   - LinkageKind = {internal, external} (line 15552)
//   - Linkage-UserItem rules (lines 15555-15568)
//   - Linkage-ExternProc rule (lines 15560-15563)
//   - Linkage-StaticBinding rules (lines 15570-15578)
//   - Linkage-ClassMethod rules (lines 15580-15588)
//   - Linkage-StateMethod rules (lines 15590-15598)
//   - Linkage-Transition rules (lines 15600-15608)
//   - Linkage-InitFn, Linkage-DeinitFn rules (lines 15610-15618)
//   - Linkage-VTable, Linkage-LiteralData rules (lines 15620-15628)
//   - Linkage-DropGlue, Linkage-DefaultImpl rules (lines 15630-15643)
//   - Linkage-PanicSym through Linkage-EntrySym (lines 15645-15663)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/linkage.cpp
//   - IsExternalVisibility helper (lines 13-16)
//   - IsExternalVisibilityWithProtected helper (lines 21-26)
//   - LinkageOf overloads for various item types (lines 34-116)
//   - LinkageOfVTable, LinkageOfLiteral, etc. (lines 122-178)
//   - AnchorLinkageRules (lines 185-208)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/symbols/linkage.h (LinkageKind enum)
//   - ultraviolet/include/02_source/ast/ast.h (Visibility enum, declaration types)
//
// REFACTORING NOTES:
//   1. LinkageKind determines symbol visibility to linker
//   2. External: visible for linking across modules
//   3. Internal: private to compilation unit
//   4. Visibility mapping:
//      - public/internal -> External linkage
//      - private -> Internal linkage
//      - protected -> External (for class/modal items)
//   5. Generated symbols (vtables, literals, drop glue) are Internal
//   6. Entry symbol (main) is always External
//   7. Runtime symbols are Internal
//
// LINKAGE RULES SUMMARY:
//   - User items: visibility-based
//   - Extern procs: External
//   - Generated: Internal (vtables, literals, drop)
//   - Runtime: Internal (init/deinit, panic, region, builtin)
//   - Entry: External
// =============================================================================

#include "05_codegen/symbols/linkage.h"

#include "00_core/assert_spec.h"
#include "05_codegen/symbols/visibility.h"

#include "02_source/ast/nodes/ast_items.h"

namespace ultraviolet::codegen {

// =============================================================================
// User Item Linkage
// =============================================================================

LinkageKind LinkageOf(const ast::ProcedureDecl& proc) {
  SPEC_DEF("LinkageJudg", "6.3.4");
  SPEC_DEF("Vis", "5.1.4");

  if (IsExternalVisibility(proc.vis)) {
    SPEC_RULE("Linkage-UserItem");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-UserItem-Internal");
  return LinkageKind::Internal;
}

LinkageKind LinkageOf([[maybe_unused]] const ast::ExternProcDecl& proc) {
  SPEC_RULE("Linkage-ExternProc");
  return LinkageKind::External;
}

LinkageKind LinkageOf(const ast::StaticDecl& decl) {
  if (IsExternalVisibility(decl.vis)) {
    SPEC_RULE("Linkage-UserItem");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-UserItem-Internal");
  return LinkageKind::Internal;
}

LinkageKind LinkageOf(const ast::MethodDecl& method) {
  if (IsExternalVisibility(method.vis)) {
    SPEC_RULE("Linkage-UserItem");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-UserItem-Internal");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfStaticBinding(ast::Visibility vis) {
  if (IsExternalVisibility(vis)) {
    SPEC_RULE("Linkage-StaticBinding");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-StaticBinding-Internal");
  return LinkageKind::Internal;
}

// =============================================================================
// Class Method Linkage
// =============================================================================

LinkageKind LinkageOf(const ast::ClassMethodDecl& method) {
  // Note: This function should only be called for methods with bodies.
  // Abstract class methods (body_opt = nullptr) don't generate symbols.

  if (IsExternalVisibilityWithProtected(method.vis)) {
    SPEC_RULE("Linkage-ClassMethod");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-ClassMethod-Internal");
  return LinkageKind::Internal;
}

// =============================================================================
// State Method and Transition Linkage
// =============================================================================

LinkageKind LinkageOf(const ast::StateMethodDecl& method) {
  if (IsExternalVisibilityWithProtected(method.vis)) {
    SPEC_RULE("Linkage-StateMethod");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-StateMethod-Internal");
  return LinkageKind::Internal;
}

LinkageKind LinkageOf(const ast::TransitionDecl& trans) {
  if (IsExternalVisibilityWithProtected(trans.vis)) {
    SPEC_RULE("Linkage-Transition");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-Transition-Internal");
  return LinkageKind::Internal;
}

// =============================================================================
// Generated Symbol Linkage
// =============================================================================

LinkageKind LinkageOfVTable() {
  SPEC_RULE("Linkage-VTable");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfLiteral() {
  SPEC_RULE("Linkage-LiteralData");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfDropGlue() {
  SPEC_RULE("Linkage-DropGlue");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfDefaultImpl(ast::Visibility method_vis) {
  if (IsExternalVisibilityWithProtected(method_vis)) {
    SPEC_RULE("Linkage-DefaultImpl");
    return LinkageKind::External;
  }

  SPEC_RULE("Linkage-DefaultImpl-Internal");
  return LinkageKind::Internal;
}

// =============================================================================
// Runtime Symbol Linkage
// =============================================================================

LinkageKind LinkageOfInitFn() {
  SPEC_RULE("Linkage-InitFn");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfDeinitFn() {
  SPEC_RULE("Linkage-DeinitFn");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfPanicSym() {
  SPEC_RULE("Linkage-PanicSym");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfBuiltinModalSym() {
  SPEC_RULE("Linkage-BuiltinModalSym");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfBuiltinSym() {
  SPEC_RULE("Linkage-BuiltinSym");
  return LinkageKind::Internal;
}

LinkageKind LinkageOfEntrySym() {
  SPEC_RULE("Linkage-EntrySym");
  return LinkageKind::External;
}

// =============================================================================
// Spec Rule Anchors
// =============================================================================

void AnchorLinkageRules() {
  // Section 6.3.4 Linkage Rules
  SPEC_RULE("Linkage-UserItem");
  SPEC_RULE("Linkage-UserItem-Internal");
  SPEC_RULE("Linkage-StaticBinding");
  SPEC_RULE("Linkage-StaticBinding-Internal");
  SPEC_RULE("Linkage-ClassMethod");
  SPEC_RULE("Linkage-ClassMethod-Internal");
  SPEC_RULE("Linkage-StateMethod");
  SPEC_RULE("Linkage-StateMethod-Internal");
  SPEC_RULE("Linkage-Transition");
  SPEC_RULE("Linkage-Transition-Internal");
  SPEC_RULE("Linkage-InitFn");
  SPEC_RULE("Linkage-DeinitFn");
  SPEC_RULE("Linkage-VTable");
  SPEC_RULE("Linkage-LiteralData");
  SPEC_RULE("Linkage-DropGlue");
  SPEC_RULE("Linkage-DefaultImpl");
  SPEC_RULE("Linkage-DefaultImpl-Internal");
  SPEC_RULE("Linkage-PanicSym");
  SPEC_RULE("Linkage-BuiltinModalSym");
  SPEC_RULE("Linkage-BuiltinSym");
  SPEC_RULE("Linkage-EntrySym");
}

}  // namespace ultraviolet::codegen
