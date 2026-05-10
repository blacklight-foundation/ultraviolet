#pragma once

#include "02_source/ast/ast.h"

namespace cursive::codegen {

// LinkageKind per §6.3.4:
//   LinkageKind = {internal, external}
enum class LinkageKind {
  Internal,
  External,
};

// =============================================================================
// User Item Linkage (§6.3.4 Linkage-UserItem, Linkage-UserItem-Internal)
// =============================================================================

// (Linkage-UserItem): public/internal visibility -> external linkage
// (Linkage-UserItem-Internal): private visibility -> internal linkage
LinkageKind LinkageOf(const ast::ProcedureDecl& proc);
LinkageKind LinkageOf(const ast::ExternProcDecl& proc);
LinkageKind LinkageOf(const ast::StaticDecl& decl);
LinkageKind LinkageOf(const ast::MethodDecl& method);

// (Linkage-StaticBinding): public/internal -> external
// (Linkage-StaticBinding-Internal): private -> internal
LinkageKind LinkageOfStaticBinding(ast::Visibility vis);

// =============================================================================
// Class Method Linkage (§6.3.4 Linkage-ClassMethod, Linkage-ClassMethod-Internal)
// =============================================================================

// (Linkage-ClassMethod): public/internal/protected with body -> external
// (Linkage-ClassMethod-Internal): private with body -> internal
// Note: Abstract class methods (no body) don't generate symbols.
LinkageKind LinkageOf(const ast::ClassMethodDecl& method);

// =============================================================================
// State Method and Transition Linkage (§6.3.4)
// =============================================================================

// (Linkage-StateMethod): public/internal/protected -> external
// (Linkage-StateMethod-Internal): private -> internal
LinkageKind LinkageOf(const ast::StateMethodDecl& method);

// (Linkage-Transition): public/internal/protected -> external
// (Linkage-Transition-Internal): private -> internal
LinkageKind LinkageOf(const ast::TransitionDecl& trans);

// =============================================================================
// Generated Symbol Linkage (§6.3.4)
// =============================================================================

// (Linkage-VTable): always internal
LinkageKind LinkageOfVTable();

// (Linkage-LiteralData): always internal
LinkageKind LinkageOfLiteral();

// (Linkage-DropGlue): always internal
LinkageKind LinkageOfDropGlue();

// (Linkage-DefaultImpl): follows method visibility
// (Linkage-DefaultImpl-Internal): private method -> internal
LinkageKind LinkageOfDefaultImpl(ast::Visibility method_vis);

// =============================================================================
// Runtime Symbol Linkage (§6.3.4)
// =============================================================================

// (Linkage-InitFn): always internal
LinkageKind LinkageOfInitFn();

// (Linkage-DeinitFn): always internal
LinkageKind LinkageOfDeinitFn();

// (Linkage-PanicSym): always internal
LinkageKind LinkageOfPanicSym();

// (Linkage-BuiltinModalSym): always internal
LinkageKind LinkageOfBuiltinModalSym();

// (Linkage-BuiltinSym): always internal
LinkageKind LinkageOfBuiltinSym();

// (Linkage-EntrySym): always external
LinkageKind LinkageOfEntrySym();

// =============================================================================
// Spec Rule Anchors
// =============================================================================

// Emits SPEC_RULE anchors for §6.3.4 linkage rules.
void AnchorLinkageRules();

}  // namespace cursive::codegen
