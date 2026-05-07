// =============================================================================
// MIGRATION MAPPING: assemblies.cpp (NEW FILE)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.2
//   - Projects, manifests, and assemblies
//   - AssemblyKind definition
//   - Assembly record validation
//
// SOURCE FILES: Content extracted from multiple sources
//   - cursive-bootstrap/src/01_project/load_project.cpp (Assembly handling)
//   - cursive-bootstrap/src/01_project/project_validate.cpp (ValidatedAssembly)
//
// =============================================================================
// RATIONALE FOR NEW FILE
// =============================================================================
//
// This file consolidates assembly-related functionality that is currently
// scattered across load_project.cpp and project_validate.cpp. The spec
// defines assemblies as a distinct concept in Section 2.2, warranting
// a dedicated implementation file.
//
// =============================================================================
// CONTENT TO IMPLEMENT
// =============================================================================
//
// 1. AssemblyKind validation
//    - SPEC RULE: AssemblyKind = {`executable`, `library`, `dependency`}
//    - SPEC RULE: A_0.kind in AssemblyKind => A_0 : Assembly
//
// 2. Assembly record accessors
//    - SPEC RULE: Assemblies(P) = P.assemblies
//    - SPEC RULE: Assembly(P) = P.assembly
//    - SPEC RULE: AsmNames(P) = [A.name | A in Assemblies(P)]
//    - SPEC RULE: AsmByName(P, n) = A iff A in Assemblies(P) and A.name = n
//
// 3. Assembly record structure
//    - SPEC: Assembly = <name, kind, link_kind, root, out_dir, emit_ir,
//      source_root, outputs, modules>
//
// =============================================================================
// CONTENT TO EXTRACT FROM EXISTING FILES
// =============================================================================
//
// From load_project.cpp:
//   - Assembly struct definition (currently in project.h)
//   - BuildAssembly() function creates Assembly instances (lines 40-96)
//   - Module list handling for assembly.modules field
//
// From project_validate.cpp:
//   - ValidatedAssembly struct (intermediate validation result)
//   - AssemblyKind validation (lines 215-219)
//
// =============================================================================
// PROPOSED FUNCTIONS
// =============================================================================
//
// 1. bool IsValidAssemblyKind(std::string_view kind)
//    - PURPOSE: Check if kind is "executable", "library", or "dependency"
//    - SPEC RULE: k in AssemblyKind
//
// 2. bool IsExecutable(const Assembly& assembly)
//    - PURPOSE: Check if assembly is an executable
//    - SPEC RULE: Executable(P) <=> P.assembly.kind = `executable`
//
// 3. bool IsLibrary(const Assembly& assembly)
//    - PURPOSE: Check if assembly is a library
//
// 4. std::vector<std::string> GetAssemblyNames(const Project& project)
//    - PURPOSE: Get list of assembly names
//    - SPEC RULE: AsmNames(P) = [A.name | A in Assemblies(P)]
//
// 5. std::optional<Assembly> GetAssemblyByName(const Project& project, std::string_view name)
//    - PURPOSE: Find assembly by name
//    - SPEC RULE: AsmByName(P, n) = A iff A.name = n
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
//
// Headers required:
//   - "cursive0/01_project/assemblies.h" (to be created)
//   - "cursive0/01_project/project.h" (Project, Assembly types)
//   - <string>
//   - <string_view>
//   - <vector>
//   - <optional>
//
// Types needed:
//   - Assembly struct (from project.h)
//   - Project struct (from project.h)
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
//
// 1. This file consolidates assembly handling from multiple places
//    RECOMMENDATION: Move related code here during migration
//
// 2. The spec treats assemblies as first-class entities
//    RECOMMENDATION: Ensure type definitions match spec structure
//
// 3. Consider adding validation functions for Assembly records
//
// 4. Assembly and project kind helpers are canonical here. Driver/output code
//    should call these helpers rather than comparing kind strings directly.
//
// =============================================================================
// SPEC RULE ANNOTATIONS (to be added during implementation)
// =============================================================================
//
// No existing source to annotate - this is a new file.
// Implement with appropriate SPEC_RULE annotations.
//
// =============================================================================

#include "01_project/assemblies.h"

#include "00_core/assert_spec.h"

namespace cursive::project {

namespace {

void SpecDefsAssemblyModel() {
  SPEC_DEF("AssemblyKind", "2");
  SPEC_DEF("Assembly", "2");
  SPEC_DEF("Project", "2");
  SPEC_DEF("Assemblies", "2");
  SPEC_DEF("AsmNames", "2");
  SPEC_DEF("AsmByName", "2");
}

void SpecDefsExecutable() {
  SPEC_DEF("Executable", "0.3.2");
}

}  // namespace

bool IsValidAssemblyKind(std::string_view kind) {
  SpecDefsAssemblyModel();
  return kind == "executable" || kind == "library" || kind == "dependency";
}

bool IsValidLinkKind(std::string_view link_kind) {
  SpecDefsAssemblyModel();
  return link_kind == "shared" || link_kind == "static";
}

bool IsExecutable(const Assembly& assembly) {
  SpecDefsAssemblyModel();
  SpecDefsExecutable();
  return assembly.kind == "executable";
}

bool IsLibrary(const Assembly& assembly) {
  SpecDefsAssemblyModel();
  return assembly.kind == "library";
}

bool IsDependency(const Assembly& assembly) {
  SpecDefsAssemblyModel();
  return assembly.kind == "dependency";
}

bool IsLinkable(const Assembly& assembly) {
  SpecDefsAssemblyModel();
  return IsExecutable(assembly) || IsLibrary(assembly);
}

bool IsSharedLibrary(const Assembly& assembly) {
  SpecDefsAssemblyModel();
  return IsLibrary(assembly) && assembly.link_kind == "shared";
}

bool IsStaticLibrary(const Assembly& assembly) {
  SpecDefsAssemblyModel();
  return IsLibrary(assembly) && assembly.link_kind == "static";
}

bool IsExecutable(const Project& project) {
  return IsExecutable(project.assembly);
}

bool IsLibrary(const Project& project) {
  return IsLibrary(project.assembly);
}

bool IsDependency(const Project& project) {
  return IsDependency(project.assembly);
}

bool IsLinkable(const Project& project) {
  return IsLinkable(project.assembly);
}

bool IsSharedLibrary(const Project& project) {
  return IsSharedLibrary(project.assembly);
}

bool IsStaticLibrary(const Project& project) {
  return IsStaticLibrary(project.assembly);
}

std::vector<std::string> GetAssemblyNames(const Project& project) {
  SpecDefsAssemblyModel();
  std::vector<std::string> names;
  names.reserve(project.assemblies.size());
  for (const auto& assembly : project.assemblies) {
    names.push_back(assembly.name);
  }
  return names;
}

std::optional<Assembly> GetAssemblyByName(const Project& project,
                                          std::string_view name) {
  SpecDefsAssemblyModel();
  const Assembly* match = nullptr;
  for (const auto& assembly : project.assemblies) {
    if (assembly.name == name) {
      if (match != nullptr) {
        return std::nullopt;
      }
      match = &assembly;
    }
  }
  if (!match) {
    return std::nullopt;
  }
  return *match;
}

}  // namespace cursive::project
