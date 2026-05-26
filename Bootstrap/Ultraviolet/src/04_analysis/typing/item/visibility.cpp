// =============================================================================
// MIGRATION: item/visibility.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 4: Module System - Visibility
//   - Visibility modifiers: public, internal, private, protected
//   - Visibility inheritance
//   - Field visibility
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/resolve/visibility.cpp
//
// =============================================================================

#include "04_analysis/resolve/visibility.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_decls.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsVisibility() {
  SPEC_DEF("Vis-Public", "5.4");
  SPEC_DEF("Vis-Internal", "5.4");
  SPEC_DEF("Vis-Private", "5.4");
  SPEC_DEF("Vis-Protected", "5.4");
  SPEC_DEF("Vis-Rank", "5.4");
  SPEC_DEF("Vis-Check", "5.4");
  SPEC_DEF("Vis-Field", "5.4");
  SPEC_DEF("Vis-Coercion", "5.4");
}

// =============================================================================
// VISIBILITY RANK
// =============================================================================

static int VisibilityRank(ast::Visibility vis) {
  SPEC_RULE("Vis-Rank");
  switch (vis) {
    case ast::Visibility::Public:
      return 4;
    case ast::Visibility::Internal:
      return 3;
    case ast::Visibility::Private:
      return 1;
  }
  return 1;
}

// =============================================================================
// MODULE PATH UTILITIES
// =============================================================================

static bool SameModule(const ast::ModulePath& a, const ast::ModulePath& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

static bool IsSubmodule(const ast::ModulePath& parent,
                        const ast::ModulePath& child) {
  if (child.size() <= parent.size()) {
    return false;
  }
  for (std::size_t i = 0; i < parent.size(); ++i) {
    if (parent[i] != child[i]) {
      return false;
    }
  }
  return true;
}

static bool SameModuleOrSubmodule(const ast::ModulePath& parent,
                                   const ast::ModulePath& child) {
  return SameModule(parent, child) || IsSubmodule(parent, child);
}

}  // namespace

// =============================================================================
// EXPORTED: VisRank
// =============================================================================

int VisRank(ast::Visibility vis) {
  SpecDefsVisibility();
  return VisibilityRank(vis);
}

// =============================================================================
// EXPORTED: IsVisible
// =============================================================================

bool IsVisible(ast::Visibility item_vis,
               const ast::ModulePath& item_module,
               const ast::ModulePath& from_module,
               const std::string& assembly_name,
               const std::string& from_assembly) {
  SpecDefsVisibility();

  switch (item_vis) {
    case ast::Visibility::Public:
      SPEC_RULE("Vis-Public");
      return true;

    case ast::Visibility::Internal:
      SPEC_RULE("Vis-Internal");
      // Visible within the same assembly
      return assembly_name == from_assembly;

    case ast::Visibility::Private:
      SPEC_RULE("Vis-Private");
      // Visible only in declaring module
      return SameModule(item_module, from_module);
  }

  return false;
}

// =============================================================================
// EXPORTED: CheckFieldVisibility
// =============================================================================

VisibilityCheckResult CheckFieldVisibility(
    ast::Visibility field_vis,
    ast::Visibility type_vis) {
  SpecDefsVisibility();
  VisibilityCheckResult result;
  result.ok = true;

  // Field visibility cannot exceed containing type visibility
  if (VisibilityRank(field_vis) > VisibilityRank(type_vis)) {
    SPEC_RULE("Vis-Field-Err");
    result.ok = false;
    result.diag_id = "E-TYP-1906";
  } else {
    SPEC_RULE("Vis-Field-Ok");
  }

  return result;
}

// =============================================================================
// EXPORTED: CheckVisibilityCoercion
// =============================================================================

VisibilityCheckResult CheckVisibilityCoercion(
    ast::Visibility exposed_vis,
    ast::Visibility type_vis) {
  SpecDefsVisibility();
  VisibilityCheckResult result;
  result.ok = true;

  // Cannot expose a private type in a more visible signature
  // e.g., public procedure cannot return a private type
  if (VisibilityRank(exposed_vis) > VisibilityRank(type_vis)) {
    SPEC_RULE("Vis-Coercion-Err");
    result.ok = false;
    result.diag_id = "E-MOD-1207";
  } else {
    SPEC_RULE("Vis-Coercion-Ok");
  }

  return result;
}

// =============================================================================
// EXPORTED: DefaultVisibility
// =============================================================================

ast::Visibility DefaultVisibility() {
  SpecDefsVisibility();
  // Default visibility is internal (assembly-wide)
  return ast::Visibility::Internal;
}

// =============================================================================
// EXPORTED: VisibilityToString
// =============================================================================

std::string_view VisibilityToString(ast::Visibility vis) {
  SpecDefsVisibility();
  switch (vis) {
    case ast::Visibility::Public:
      return "public";
    case ast::Visibility::Internal:
      return "internal";
    case ast::Visibility::Private:
      return "private";
  }
  return "unknown";
}

}  // namespace ultraviolet::analysis

