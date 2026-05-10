// =============================================================================
// MIGRATION MAPPING: module_discovery.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 2.4 (lines 1209-1327)
//   - 2.4. Module Discovery
//   - Module Discovery (Big-Step): Modules-Ok/Err
//   - Module Discovery (Small-Step): Disc-Start, Disc-Skip, Disc-Add, etc.
//   - Module Path computation
//   - Compilation Unit ordering
//
// SOURCE FILE: cursive-bootstrap/src/01_project/module_discovery.cpp
//   - Lines 1-337 (entire file)
//
// =============================================================================
// CONTENT TO MIGRATE
// =============================================================================
//
// 1. MakeExternalDiag() / EmitExternal() helpers (lines 23-35)
//    - PURPOSE: Emit diagnostics without source span
//    - REFACTORING: Consolidate with other files
//
// 2. HasCode() helper (lines 37-44)
//    - PURPOSE: Check if diagnostic stream contains specific code
//
// 3. DirListResult struct and CollectDirsRecursive() (lines 46-80)
//    - PURPOSE: Recursively collect all directories under root
//    - SPEC RULE: Dirs(S) = { d | is_dir(d) and relative(d, S) ok }
//    - SPEC REF: Lines 1211-1213
//    - DIAGNOSTIC: E-PRJ-0305 (DirSeq-Read-Err)
//
// 4. ModuleDirCheck struct and CheckModuleDir() (lines 82-123)
//    - PURPOSE: Check if directory contains .cursive files
//    - SPEC RULE: Module-Dir: exists f in Files(d) : FileExt(f) = ".cursive"
//    - SPEC REF: Lines 1225-1228
//    - DIAGNOSTIC: E-PRJ-0305
//
// 5. SplitModulePath() / JoinModulePath() helpers (lines 125-150)
//    - PURPOSE: Split/join module path on "::" delimiter
//    - REFACTORING: Duplicate with deterministic_order.cpp
//
// 6. ModulePathResult and ModulePathFor() (lines 152-184)
//    - PURPOSE: Compute module path from directory path
//    - SPEC RULES:
//      * Module-Path-Root (spec lines 1256-1259)
//      * Module-Path-Rel (spec lines 1261-1264)
//      * Module-Path-Rel-Fail (spec lines 1266-1269)
//    - DIAGNOSTIC: E-PRJ-0304 (Disc-Rel-Fail)
//
// 7. ValidateModulePath() (lines 186-202)
//    - PURPOSE: Validate module path components
//    - SPEC RULES:
//      * WF-Module-Path-Ok (spec lines 1273-1276)
//      * WF-Module-Path-Reserved (spec lines 1278-1281)
//      * WF-Module-Path-Ident-Err (spec lines 1283-1286)
//    - DIAGNOSTICS:
//      * E-MOD-1105: Reserved keyword in module path
//      * E-MOD-1106: Invalid identifier in module path
//    - DEPENDENCIES: IsIdentifier(), IsKeyword() from ident.h
//
// 8. CompilationUnit() (lines 206-261)
//    - PURPOSE: Get sorted list of .cursive files in module directory
//    - SPEC RULE: CompilationUnit(d) = sort_{<_file}(Files(d))
//    - SPEC REF: Line 1246
//    - SPEC RULE: CompilationUnit-Rel-Fail (spec lines 1248-1251)
//    - DIAGNOSTIC: E-PRJ-0305, E-PRJ-0303
//
// 9. Modules() (lines 263-334)
//    - PURPOSE: Main module discovery entry point
//    - SPEC RULES:
//      * Disc-Start (spec lines 1296-1298)
//      * Disc-Skip (spec lines 1300-1303)
//      * Disc-Add (spec lines 1305-1308)
//      * Disc-Collision (spec lines 1310-1313)
//      * Disc-Invalid-Component (spec lines 1315-1318)
//      * Disc-Rel-Fail (spec lines 1320-1323)
//      * Disc-Done (spec lines 1325-1327)
//      * Modules-Ok / Modules-Err (spec lines 1234-1242)
//      * WF-Module-Path-Collision (spec lines 1288-1291)
//    - DIAGNOSTICS:
//      * E-MOD-1104: Module path collision
//      * W-MOD-1101: Case-insensitive collision warning
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
//
// Headers required:
//   - "cursive0/01_project/module_discovery.h" (types)
//   - "cursive0/00_core/assert_spec.h" (SPEC_RULE macro)
//   - "cursive0/00_core/diagnostic_messages.h" (MakeDiagnostic)
//   - "cursive0/00_core/diagnostics.h" (DiagnosticStream, Emit, HasError)
//   - "cursive0/00_core/path.h" (FileExt, Relative, PathComps)
//   - "cursive0/01_project/deterministic_order.h" (FileKey, KeyLess, Fold, DirSeqFrom)
//   - "cursive0/01_project/ident.h" (IsIdentifier, IsKeyword)
//   - <algorithm>
//   - <filesystem>
//   - <optional>
//   - <string>
//   - <string_view>
//   - <system_error>
//   - <unordered_map>
//   - <vector>
//
// Types from header (module_discovery.h):
//   - ModuleInfo { std::string path; std::filesystem::path dir; }
//   - ModulesResult { std::vector<ModuleInfo> modules; DiagnosticStream diags; }
//   - CompilationUnitResult { std::vector<std::filesystem::path> files; DiagnosticStream diags; }
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
//
// 1. SplitModulePath/JoinModulePath are duplicated in deterministic_order.cpp
//    RECOMMENDATION: Move to shared module_path utility
//
// 2. The discovery algorithm uses std::unordered_map<string, string> for
//    collision detection (folded path -> original path)
//
// 3. The spec defines both small-step and big-step semantics:
//    - Small-step: DiscState machine transitions
//    - Big-step: Modules-Ok/Err
//    The implementation follows the big-step pattern
//
// 4. Consider parallel directory enumeration for large projects
//
// =============================================================================
// SPEC RULE ANNOTATIONS (use SPEC_RULE macro)
// =============================================================================
//
// Lines 58, 65, 72, 93, 101, 109: SPEC_RULE("DirSeq-Read-Err");
// Line 167-168: SPEC_RULE("Module-Path-Rel-Fail"); SPEC_RULE("Disc-Rel-Fail");
// Line 173: SPEC_RULE("Module-Path-Root");
// Line 179: SPEC_RULE("Module-Path-Rel");
// Line 190: SPEC_RULE("WF-Module-Path-Ident-Err");
// Line 195: SPEC_RULE("WF-Module-Path-Reserved");
// Line 200: SPEC_RULE("WF-Module-Path-Ok");
// Line 211, 225, 233: SPEC_RULE("DirSeq-Read-Err");
// Line 245: SPEC_RULE("CompilationUnit-Rel-Fail");
// Line 267: SPEC_RULE("Disc-Start");
// Line 274, 288, 301, 306-307, 317, 327: SPEC_RULE("Modules-Err");
// Line 292: SPEC_RULE("Disc-Skip");
// Line 295: SPEC_RULE("Module-Dir");
// Line 305: SPEC_RULE("Disc-Invalid-Component");
// Line 313-314: SPEC_RULE("Disc-Collision"); SPEC_RULE("WF-Module-Path-Collision");
// Line 323: SPEC_RULE("Disc-Add");
// Line 331-332: SPEC_RULE("Disc-Done"); SPEC_RULE("Modules-Ok");
//
// =============================================================================

#include "01_project/module_discovery.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/host/services.h"
#include "00_core/ident.h"
#include "00_core/path.h"
#include "01_project/deterministic_order.h"
#include "01_project/language_profile.h"

namespace cursive::project {

namespace {

void EmitExternal(core::DiagnosticStream& diags, std::string_view code) {
  core::EmitExternalDiagnostic(diags, code);
}

bool HasCode(const core::DiagnosticStream& diags, std::string_view code) {
  for (const auto& diag : diags) {
    if (diag.code == code) {
      return true;
    }
  }
  return false;
}

struct DirListResult {
  std::vector<std::filesystem::path> dirs;
  core::DiagnosticStream diags;
};

DirListResult CollectDirsRecursive(const std::filesystem::path& root) {
  DirListResult result;
  result.dirs.push_back(root);

  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(root, ec);
  if (ec) {
    SPEC_RULE("DirSeq-Read-Err");
    EmitExternal(result.diags, "E-PRJ-0305");
    return result;
  }
  const std::filesystem::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      SPEC_RULE("DirSeq-Read-Err");
      EmitExternal(result.diags, "E-PRJ-0305");
      return result;
    }
    std::error_code type_ec;
    if (it->is_directory(type_ec)) {
      if (type_ec) {
        SPEC_RULE("DirSeq-Read-Err");
        EmitExternal(result.diags, "E-PRJ-0305");
        return result;
      }
      result.dirs.push_back(it->path());
    }
  }
  return result;
}

struct ModuleDirCheck {
  bool ok = true;
  bool is_module = false;
};

ModuleDirCheck CheckModuleDir(const std::filesystem::path& dir,
                              const LanguageProfile& language,
                              core::DiagnosticStream& diags) {
  ModuleDirCheck result;
  std::error_code ec;
  std::filesystem::directory_iterator it(dir, ec);
  if (ec) {
    SPEC_RULE("DirSeq-Read-Err");
    EmitExternal(diags, "E-PRJ-0305");
    result.ok = false;
    return result;
  }
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      SPEC_RULE("DirSeq-Read-Err");
      EmitExternal(diags, "E-PRJ-0305");
      result.ok = false;
      return result;
    }
    std::error_code type_ec;
    if (!it->is_regular_file(type_ec)) {
      if (type_ec) {
        SPEC_RULE("DirSeq-Read-Err");
        EmitExternal(diags, "E-PRJ-0305");
        result.ok = false;
        return result;
      }
      continue;
    }
    const std::string file_str = it->path().generic_string();
    if (core::FileExt(file_str) == language.source_extension) {
      result.is_module = true;
      break;
    }
  }
  return result;
}

std::vector<std::string> SplitModulePath(std::string_view path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t pos = path.find("::", start);
    if (pos == std::string_view::npos) {
      parts.emplace_back(path.substr(start));
      break;
    }
    parts.emplace_back(path.substr(start, pos - start));
    start = pos + 2;
  }
  return parts;
}

std::string JoinModulePath(const std::vector<std::string>& comps) {
  if (comps.empty()) {
    return "";
  }
  std::string out = comps[0];
  for (std::size_t i = 1; i < comps.size(); ++i) {
    out.append("::");
    out.append(comps[i]);
  }
  return out;
}

struct ModulePathResult {
  std::string path;
  std::vector<std::string> components;
  bool ok = false;
};

ModulePathResult ModulePathFor(const std::filesystem::path& dir,
                               const std::filesystem::path& source_root,
                               std::string_view assembly_name,
                               core::DiagnosticStream& diags) {
  ModulePathResult result;
  const std::string dir_str = dir.generic_string();
  const std::string root_str = source_root.generic_string();
  const std::optional<std::string> rel = core::Relative(dir_str, root_str);
  if (!rel.has_value()) {
    SPEC_RULE("Module-Path-Rel-Fail");
    SPEC_RULE("Disc-Rel-Fail");
    EmitExternal(diags, "E-PRJ-0304");
    return result;
  }
  if (rel->empty()) {
    SPEC_RULE("Module-Path-Root");
    result.path = std::string(assembly_name);
    result.components = SplitModulePath(result.path);
    result.ok = true;
    return result;
  }
  SPEC_RULE("Module-Path-Rel");
  result.components = core::PathComps(*rel);
  // Keep module identity unique across assemblies by qualifying non-root
  // modules with their owning assembly name.
  result.components.insert(result.components.begin(),
                           std::string(assembly_name));
  result.path = JoinModulePath(result.components);
  result.ok = true;
  return result;
}

bool ValidateModulePath(const std::vector<std::string>& components,
                        core::DiagnosticStream& diags) {
  for (const auto& comp : components) {
    if (!core::IsIdentifier(comp)) {
      SPEC_RULE("WF-Module-Path-Ident-Err");
      EmitExternal(diags, "E-MOD-1106");
      return false;
    }
    if (core::IsKeyword(comp)) {
      SPEC_RULE("WF-Module-Path-Reserved");
      // Validate-Module-Keyword-Err and WF-Module-Path-Reserved both denote
      // reserved-keyword misuse in module naming contexts.
      EmitExternal(diags, "E-CNF-0401");
      EmitExternal(diags, "E-MOD-1105");
      return false;
    }
  }
  SPEC_RULE("WF-Module-Path-Ok");
  return true;
}

}  // namespace

CompilationUnitResult CompilationUnit(const std::filesystem::path& module_dir) {
  return CompilationUnit(module_dir, ActiveLanguageProfile());
}

CompilationUnitResult CompilationUnit(const std::filesystem::path& module_dir,
                                      const LanguageProfile& language) {
  CompilationUnitResult result;
  if (const auto force = core::HostGetEnvUtf8("CURSIVE_TEST_COMPILATION_UNIT_FAIL");
      force.has_value() && !force->empty()) {
    SPEC_RULE("CompilationUnit-Rel-Fail");
    EmitExternal(result.diags, "E-PRJ-0303");
    return result;
  }
  std::error_code ec;
  std::filesystem::directory_iterator it(module_dir, ec);
  if (ec) {
    SPEC_RULE("DirSeq-Read-Err");
    EmitExternal(result.diags, "E-PRJ-0305");
    return result;
  }

  struct FileEntry {
    std::filesystem::path path;
    OrderKey key;
  };

  std::vector<FileEntry> entries;
  const std::filesystem::directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      SPEC_RULE("DirSeq-Read-Err");
      EmitExternal(result.diags, "E-PRJ-0305");
      return result;
    }
    std::error_code type_ec;
    if (!it->is_regular_file(type_ec)) {
      if (type_ec) {
        SPEC_RULE("DirSeq-Read-Err");
        EmitExternal(result.diags, "E-PRJ-0305");
        return result;
      }
      continue;
    }
    const std::string file_str = it->path().generic_string();
    if (core::FileExt(file_str) != language.source_extension) {
      continue;
    }

    const OrderKey key = FileKey(it->path(), module_dir, result.diags);
    if (HasCode(result.diags, "E-PRJ-0303")) {
      SPEC_RULE("CompilationUnit-Rel-Fail");
      return result;
    }
    entries.push_back(FileEntry{it->path(), key});
  }

  std::stable_sort(entries.begin(), entries.end(),
                   [](const FileEntry& lhs, const FileEntry& rhs) {
                     return KeyLess(lhs.key, rhs.key);
                   });

  result.files.reserve(entries.size());
  for (const auto& entry : entries) {
    result.files.push_back(entry.path);
  }
  return result;
}

ModulesResult Modules(const std::filesystem::path& source_root,
                      std::string_view assembly_name) {
  return Modules(source_root, assembly_name, ActiveLanguageProfile());
}

ModulesResult Modules(const std::filesystem::path& source_root,
                      std::string_view assembly_name,
                      const LanguageProfile& language) {
  ModulesResult result;

  SPEC_RULE("Disc-Start");

  const DirListResult dir_list = CollectDirsRecursive(source_root);
  for (const auto& diag : dir_list.diags) {
    core::Emit(result.diags, diag);
  }
  if (core::HasError(dir_list.diags)) {
    SPEC_RULE("Modules-Err");
    return result;
  }

  const DirSeqResult dir_seq = DirSeqFrom(source_root, dir_list.dirs);
  for (const auto& diag : dir_seq.diags) {
    core::Emit(result.diags, diag);
  }
  const bool dir_seq_error = core::HasError(dir_seq.diags);

  std::unordered_map<std::string, std::string> seen;
  for (const auto& dir : dir_seq.dirs) {
    const ModuleDirCheck module_check =
        CheckModuleDir(dir, language, result.diags);
    if (!module_check.ok) {
      SPEC_RULE("Modules-Err");
      return result;
    }
    if (!module_check.is_module) {
      SPEC_RULE("Disc-Skip");
      continue;
    }
    SPEC_RULE("Module-Dir");

    const ModulePathResult path_result =
        ModulePathFor(dir, source_root, assembly_name, result.diags);
    if (!path_result.ok) {
      SPEC_RULE("Modules-Err");
      return result;
    }

    if (!ValidateModulePath(path_result.components, result.diags)) {
      SPEC_RULE("Disc-Invalid-Component");
      SPEC_RULE("Modules-Err");
      return result;
    }

    const std::string folded = Fold(path_result.path);
    const auto it = seen.find(folded);
    if (it != seen.end() && it->second != path_result.path) {
      SPEC_RULE("Disc-Collision");
      SPEC_RULE("WF-Module-Path-Collision");
      EmitExternal(result.diags, "E-MOD-1104");
      EmitExternal(result.diags, "W-MOD-1101");
      SPEC_RULE("Modules-Err");
      return result;
    }

    seen.emplace(folded, path_result.path);
    result.modules.push_back(ModuleInfo{path_result.path, dir});
    SPEC_RULE("Disc-Add");
  }

  if (dir_seq_error) {
    SPEC_RULE("Modules-Err");
    return result;
  }

  SPEC_RULE("Disc-Done");
  SPEC_RULE("Modules-Ok");
  return result;
}

}  // namespace cursive::project
