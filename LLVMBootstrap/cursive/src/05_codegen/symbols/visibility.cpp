// =============================================================================
// MIGRATION MAPPING: visibility.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 5.1.4 Visibility (not in codegen section, but used by linkage)
//   - Visibility = {public, internal, private, protected}
//   - Linkage rules reference Vis(item) (lines 15556, 15566, etc.)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/linkage.cpp
//   - IsExternalVisibility helper (lines 13-16)
//   - IsExternalVisibilityWithProtected helper (lines 21-26)
//
// DEPENDENCIES:
//   - cursive/include/02_source/ast/ast.h (Visibility enum)
//   - cursive/include/05_codegen/symbols/visibility.h (helpers)
//
// REFACTORING NOTES:
//   1. Visibility determines linkage for user items
//   2. public/internal: External linkage (cross-module)
//   3. private: Internal linkage (module-local)
//   4. protected: External for class/modal items
//   5. Default visibility is Internal (assembly-scope)
//   6. Used by linkage determination, not directly in mangling
//
// VISIBILITY HELPERS:
//   - IsExternalVisibility(vis) -> bool
//     Returns true for public, internal
//   - IsExternalVisibilityWithProtected(vis) -> bool
//     Returns true for public, internal, protected
//
// VISIBILITY TO LINKAGE MAPPING:
//   User items (procedures, statics, methods):
//     public -> External
//     internal -> External
//     private -> Internal
//
//   Class/Modal items (class methods, state methods, transitions):
//     public -> External
//     internal -> External
//     protected -> External
//     private -> Internal
// =============================================================================

#include "05_codegen/symbols/visibility.h"

namespace cursive::codegen {

// Helper to determine if visibility implies external linkage for user items.
// Per Section 6.3.4:
//   Vis(item) in {public, internal} -> external
//   Vis(item) = private -> internal
bool IsExternalVisibility(ast::Visibility vis) {
  return vis == ast::Visibility::Public ||
         vis == ast::Visibility::Internal;
}

bool IsExternalVisibilityWithProtected(ast::Visibility vis) {
  return vis == ast::Visibility::Public ||
         vis == ast::Visibility::Internal;
}

}  // namespace cursive::codegen
