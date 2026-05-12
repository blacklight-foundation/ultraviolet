// =============================================================================
// MIGRATION MAPPING: project_validate.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 2.1 (lines 806-1027)
//   - 2.1. Project Root and Manifest
//   - Manifest Schema (Cursive0)
//   - Manifest Validation rules (WF-*)
//   - Path Resolution
//
// SOURCE FILE: cursive-bootstrap/src/01_project/project_validate.cpp
//   - Lines 1-285 (entire file)
//
// =============================================================================
// CONTENT TO MIGRATE
// =============================================================================
//
// 1. Helper functions (lines 16-68)
//    - MakeExternalDiag() / EmitExternal() (lines 18-30)
//
//    - HasOnlyAssemblyKey() (lines 32-40)
//      PURPOSE: Check that only "assembly" key exists at top level
//      SPEC RULE: Keys(T) subseteq {`assembly`}
//      SPEC REF: Lines 873-876
//
//    - IsKnownAssemblyKey() (lines 42-45)
//      PURPOSE: Check if key is valid assembly table key
//      SPEC: Req = {`name`, `kind`, `root`}, Opt = {`out_dir`, `emit_ir`}
//      SPEC REF: Lines 913-914
//
//    - RelPathStatus enum and CheckRelPath() (lines 47-68)
//      PURPOSE: Validate relative path within project root
//      SPEC RULES:
//        * WF-RelPath (spec lines 1018-1021)
//        * WF-RelPath-Err (spec lines 1023-1026)
//
// 2. AsmTablesResult and AsmTables() (lines 70-111)
//    - PURPOSE: Extract assembly tables from manifest
//    - SPEC RULE: AsmTables(T) extraction logic
//    - SPEC REF: Lines 867-871
//    - Handles both single table and array of tables
//
// 3. ValidateManifest() (lines 115-269)
//    - PURPOSE: Validate parsed manifest against schema
//    - SPEC RULES (in order of checking):
//      * WF-TopKeys / WF-TopKeys-Err (spec lines 873-881)
//      * WF-Assembly-Table / WF-Assembly-Table-Err (spec lines 883-891)
//      * WF-Assembly-Count / WF-Assembly-Count-Err (spec lines 893-901)
//      * WF-Assembly-Keys / WF-Assembly-Keys-Err (spec lines 916-924)
//      * WF-Assembly-Required-Types / WF-Assembly-Required-Types-Err (spec lines 926-934)
//      * WF-Assembly-Optional-Types (spec lines 936-948)
//      * WF-Assembly-OutDirType-Err (spec lines 941-943)
//      * WF-Assembly-EmitIRType-Err (spec lines 950-953)
//      * WF-Assembly-Name / WF-Assembly-Name-Err (spec lines 814-822)
//      * WF-Assembly-Kind / WF-Assembly-Kind-Err (spec lines 824-832)
//      * WF-Assembly-EmitIR / WF-Assembly-EmitIR-Err (spec lines 854-862)
//      * WF-Assembly-Root-Path / WF-Assembly-Root-Path-Err (spec lines 834-842)
//      * WF-Assembly-OutDir-Path / WF-Assembly-OutDir-Path-Err (spec lines 844-852)
//      * WF-Assembly-Name-Dup / WF-Assembly-Name-Dup-Err (spec lines 903-911)
//      * ValidateManifest-Ok / ValidateManifest-Err (spec lines 1068-1076)
//    - DIAGNOSTICS:
//      * E-PRJ-0103: Missing or invalid assembly table
//      * E-PRJ-0104: Unknown top-level key
//      * E-PRJ-0201: Invalid assembly kind
//      * E-PRJ-0202: Duplicate assembly name
//      * E-PRJ-0203: Invalid assembly name
//      * E-PRJ-0204: Invalid emit_ir value
//      * E-PRJ-0301: Invalid path (out_dir or root)
//      * E-PRJ-0304: Path resolution error
//    - DEPENDENCIES: IsName() from ident.h
//
// 4. IsProjectRoot() (lines 271-282)
//    - PURPOSE: Check if directory contains Cursive.toml
//    - SPEC RULE: WF-Project-Root (spec lines 1156-1159)
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
//
// Headers required:
//   - "cursive0/01_project/project.h" (types)
//   - "cursive0/00_core/assert_spec.h" (SPEC_RULE macro)
//   - "cursive0/00_core/diagnostic_messages.h" (MakeDiagnostic)
//   - "cursive0/00_core/diagnostics.h" (DiagnosticStream, Emit)
//   - "cursive0/00_core/host_primitives.h" (HostPrimFail)
//   - "cursive0/00_core/path.h" (IsRelative, Resolve, Prefix)
//   - "cursive0/01_project/ident.h" (IsName, IsIdentifier, IsKeyword)
//   - <string_view>
//   - <system_error>
//   - <unordered_set>
//   - <filesystem>
//   - toml++ library (toml::table, toml::node, toml::array)
//
// Types from header (project.h):
//   - ValidatedAssembly { name, kind, root, out_dir, emit_ir }
//   - ManifestValidationResult { std::vector<ValidatedAssembly> assemblies; DiagnosticStream diags; }
//   - TOMLTable = toml::table
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
//
// 1. The validation follows a deterministic order as specified:
//    ChecksAsm(t) = [KnownKeys, ReqTypes, OutDirType, EmitIRType, Name, Kind, EmitIR, RootPath, OutDirPath]
//    SPEC REF: Lines 1057-1062
//
// 2. The implementation uses early return on first error (FirstFail pattern)
//    SPEC RULE: FirstFail([J::Js]) = c if J fails, else FirstFail(Js)
//    SPEC REF: Lines 1064-1066
//
// 3. The CheckRelPath helper handles three cases:
//    - Ok: path is relative and resolves within root
//    - RelPathErr: path is absolute or escapes root
//    - ResolveErr: path resolution itself failed
//
// 4. TOML handling uses toml++ library conventions:
//    - is_table(), is_array(), is_array_of_tables()
//    - as_table(), as_array()
//    - value<T>() for extracting values
//
// =============================================================================
// SPEC RULE ANNOTATIONS (use SPEC_RULE macro)
// =============================================================================
//
// Line 55: SPEC_RULE("WF-RelPath-Err");
// Line 64: SPEC_RULE("WF-RelPath-Err");
// Line 66: SPEC_RULE("WF-RelPath");
// Line 120: SPEC_RULE("ValidateManifest-Err");
// Line 126: SPEC_RULE("WF-TopKeys-Err");
// Line 129: SPEC_RULE("WF-TopKeys");
// Line 133: SPEC_RULE("WF-Assembly-Table-Err");
// Line 139: SPEC_RULE("WF-Assembly-Table-Err");
// Line 142: SPEC_RULE("WF-Assembly-Table");
// Line 145: SPEC_RULE("WF-Assembly-Count-Err");
// Line 148: SPEC_RULE("WF-Assembly-Count");
// Line 157: SPEC_RULE("WF-Assembly-Table-Err");
// Line 164: SPEC_RULE("WF-Assembly-Keys-Err");
// Line 168: SPEC_RULE("WF-Assembly-Keys");
// Line 176: SPEC_RULE("WF-Assembly-Required-Types-Err");
// Line 179: SPEC_RULE("WF-Assembly-Required-Types");
// Line 185: SPEC_RULE("WF-Assembly-Required-Types-Err");
// Line 192: SPEC_RULE("WF-Assembly-OutDirType-Err");
// Line 202: SPEC_RULE("WF-Assembly-EmitIRType-Err");
// Line 207: SPEC_RULE("WF-Assembly-Optional-Types");
// Line 210: SPEC_RULE("WF-Assembly-Name-Err");
// Line 213: SPEC_RULE("WF-Assembly-Name");
// Line 216: SPEC_RULE("WF-Assembly-Kind-Err");
// Line 219: SPEC_RULE("WF-Assembly-Kind");
// Line 224: SPEC_RULE("WF-Assembly-EmitIR-Err");
// Line 228: SPEC_RULE("WF-Assembly-EmitIR");
// Line 231: SPEC_RULE("WF-Assembly-Root-Path-Err");
// Line 238: SPEC_RULE("WF-Assembly-Root-Path");
// Line 244: SPEC_RULE("WF-Assembly-OutDir-Path-Err");
// Line 251: SPEC_RULE("WF-Assembly-OutDir-Path");
// Line 254: SPEC_RULE("WF-Assembly-Name-Dup-Err");
// Line 265: SPEC_RULE("WF-Assembly-Name-Dup");
// Line 267: SPEC_RULE("ValidateManifest-Ok");
// Line 279: SPEC_RULE("WF-Project-Root");
//
// =============================================================================

#include "01_project/project.h"
#include "01_project/language_profile.h"

#include <string_view>
#include <system_error>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/host_primitives.h"
#include "00_core/ident.h"
#include "00_core/path.h"

namespace cursive::project {

namespace {

void EmitExternal(core::DiagnosticStream& diags, std::string_view code) {
  core::EmitExternalDiagnostic(diags, code);
}

bool IsValidTopLevelKey(std::string_view key) {
  return key == "assembly" || key == "toolchain" || key == "build";
}

bool HasOnlyValidTopLevelKeys(const TOMLTable& table) {
  for (const auto& [key, value] : table) {
    (void)value;
    if (!IsValidTopLevelKey(key)) {
      return false;
    }
  }
  return true;
}

bool IsKnownToolchainKey(std::string_view key) {
  return key == "llvm_bin" || key == "runtime_lib" ||
         key == "target_profile";
}

bool IsKnownBuildKey(std::string_view key) {
  return key == "incremental" || key == "progress";
}

ToolchainConfig ToolchainConfigOf(const TOMLTable& table) {
  ToolchainConfig config;
  const toml::node* node = table.get("toolchain");
  if (!node || !node->is_table()) {
    return config;
  }
  const auto* toolchain = node->as_table();
  if (!toolchain) {
    return config;
  }
  if (const toml::node* llvm_node = toolchain->get("llvm_bin");
      llvm_node && llvm_node->is_string()) {
    config.llvm_bin = llvm_node->value<std::string>();
  }
  if (const toml::node* runtime_node = toolchain->get("runtime_lib");
      runtime_node && runtime_node->is_string()) {
    config.runtime_lib = runtime_node->value<std::string>();
  }
  if (const toml::node* target_profile_node = toolchain->get("target_profile");
      target_profile_node && target_profile_node->is_string()) {
    const auto target_profile_value =
        target_profile_node->value<std::string>();
    if (target_profile_value.has_value()) {
      config.target_profile = ParseTargetProfile(*target_profile_value);
    }
  }
  return config;
}

BuildConfig BuildConfigOf(const TOMLTable& table) {
  BuildConfig config;
  const toml::node* node = table.get("build");
  if (!node || !node->is_table()) {
    return config;
  }
  const auto* build = node->as_table();
  if (!build) {
    return config;
  }

  // BuildConfig(T):
  // incremental defaults to false when omitted.
  // progress defaults to true when omitted.
  if (const toml::node* incremental = build->get("incremental");
      incremental && incremental->is_boolean()) {
    if (const auto value = incremental->value<bool>(); value.has_value()) {
      config.incremental = *value;
    }
  }
  if (const toml::node* progress = build->get("progress");
      progress && progress->is_boolean()) {
    if (const auto value = progress->value<bool>(); value.has_value()) {
      config.progress = *value;
    }
  }
  return config;
}

std::optional<std::string> AsmLinkKind(std::string_view kind,
                                       const std::optional<std::string>& link_kind) {
  if (kind != "library") {
    return std::nullopt;
  }
  if (!link_kind.has_value()) {
    return std::string("shared");
  }
  if (*link_kind == "shared" || *link_kind == "static") {
    return *link_kind;
  }
  return std::nullopt;
}

bool IsKnownAssemblyKey(std::string_view key) {
  return key == "name" || key == "kind" || key == "root" ||
         key == "out_dir" || key == "emit_ir" || key == "link_kind";
}

enum class RelPathStatus {
  Ok,
  RelPathErr,
  ResolveErr,
};

RelPathStatus CheckRelPath(std::string_view p, std::string_view root) {
  if (!core::IsRelative(p)) {
    SPEC_RULE("WF-RelPath-Err");
    return RelPathStatus::RelPathErr;
  }
  const auto resolved = core::Resolve(root, p);
  if (!resolved.has_value()) {
    return RelPathStatus::ResolveErr;
  }
  if (!core::Prefix(resolved->root, resolved->path)) {
    SPEC_RULE("WF-RelPath-Err");
    return RelPathStatus::RelPathErr;
  }
  SPEC_RULE("WF-RelPath");
  return RelPathStatus::Ok;
}

struct AsmTablesResult {
  bool ok = false;
  std::vector<const toml::table*> tables;
};

AsmTablesResult AsmTables(const toml::node* assembly_node) {
  AsmTablesResult result;
  if (!assembly_node) {
    return result;
  }
  if (assembly_node->is_table()) {
    const auto* table = assembly_node->as_table();
    if (!table) {
      return result;
    }
    result.ok = true;
    result.tables.push_back(table);
    return result;
  }
  if (assembly_node->is_array_of_tables()) {
    const auto* array = assembly_node->as_array();
    if (!array) {
      return result;
    }
    for (const auto& entry : *array) {
      if (!entry.is_table()) {
        return result;
      }
      result.tables.push_back(entry.as_table());
    }
    result.ok = true;
    return result;
  }
  if (assembly_node->is_array()) {
    const auto* array = assembly_node->as_array();
    if (array && array->empty()) {
      result.ok = true;
      return result;
    }
  }
  return result;
}

}  // namespace

ManifestValidationResult ValidateManifest(const std::filesystem::path& project_root,
                                         const TOMLTable& table) {
  ManifestValidationResult result;
  result.toolchain = ToolchainConfigOf(table);
  result.build = BuildConfigOf(table);

  auto fail = [&](std::string_view code) {
    SPEC_RULE("ValidateManifest-Err");
    EmitExternal(result.diags, code);
    return result;
  };

  if (!HasOnlyValidTopLevelKeys(table)) {
    SPEC_RULE("WF-TopKeys-Err");
    return fail("E-PRJ-0104");
  }
  SPEC_RULE("WF-TopKeys");

  if (const toml::node* toolchain_node = table.get("toolchain")) {
    if (!toolchain_node->is_table()) {
      SPEC_RULE("WF-Toolchain-Err");
      return fail("E-PRJ-0110");
    }
    const auto* toolchain = toolchain_node->as_table();
    if (!toolchain) {
      SPEC_RULE("WF-Toolchain-Err");
      return fail("E-PRJ-0110");
    }
    for (const auto& [key, value] : *toolchain) {
      if (!IsKnownToolchainKey(key)) {
        (void)value;
        SPEC_RULE("WF-Toolchain-Err");
        return fail("E-PRJ-0110");
      }
    }
    if (const toml::node* llvm_node = toolchain->get("llvm_bin");
        llvm_node && !llvm_node->is_string()) {
      SPEC_RULE("WF-Toolchain-Err");
      return fail("E-PRJ-0110");
    }
    if (const toml::node* runtime_node = toolchain->get("runtime_lib");
        runtime_node && !runtime_node->is_string()) {
      SPEC_RULE("WF-Toolchain-Err");
      return fail("E-PRJ-0110");
    }
    if (const toml::node* target_profile_node =
            toolchain->get("target_profile");
        target_profile_node) {
      if (!target_profile_node->is_string()) {
        SPEC_RULE("WF-Toolchain-Err");
        return fail("E-PRJ-0110");
      }
      const auto target_profile_value =
          target_profile_node->value<std::string>();
      if (!target_profile_value.has_value() ||
          !ParseTargetProfile(*target_profile_value).has_value()) {
        SPEC_RULE("WF-Toolchain-Err");
        return fail("E-PRJ-0110");
      }
    }
  }
  SPEC_RULE("WF-Toolchain");

  if (const toml::node* build_node = table.get("build")) {
    if (!build_node->is_table()) {
      SPEC_RULE("WF-Build-Err");
      return fail("E-PRJ-0111");
    }
    const auto* build = build_node->as_table();
    if (!build) {
      SPEC_RULE("WF-Build-Err");
      return fail("E-PRJ-0111");
    }
    for (const auto& [key, value] : *build) {
      if (!IsKnownBuildKey(key)) {
        (void)value;
        SPEC_RULE("WF-Build-Err");
        return fail("E-PRJ-0111");
      }
    }
    if (const toml::node* incremental_node = build->get("incremental");
        incremental_node && !incremental_node->is_boolean()) {
      SPEC_RULE("WF-Build-Err");
      return fail("E-PRJ-0111");
    }
    if (const toml::node* progress_node = build->get("progress");
        progress_node && !progress_node->is_boolean()) {
      SPEC_RULE("WF-Build-Err");
      return fail("E-PRJ-0111");
    }
  }
  SPEC_RULE("WF-Build");

  const toml::node* assembly_node = table.get("assembly");
  if (!assembly_node) {
    SPEC_RULE("WF-Assembly-Table-Err");
    return fail("E-PRJ-0103");
  }

  const AsmTablesResult asm_tables = AsmTables(assembly_node);
  if (!asm_tables.ok) {
    SPEC_RULE("WF-Assembly-Table-Err");
    return fail("E-PRJ-0103");
  }
  SPEC_RULE("WF-Assembly-Table");

  if (asm_tables.tables.empty()) {
    SPEC_RULE("WF-Assembly-Count-Err");
    return fail("E-PRJ-0103");
  }
  SPEC_RULE("WF-Assembly-Count");

  {
    std::unordered_set<std::string> names;
    names.reserve(asm_tables.tables.size());
    for (const auto* assembly : asm_tables.tables) {
      if (!assembly) {
        SPEC_RULE("WF-Assembly-Table-Err");
        return fail("E-PRJ-0103");
      }
      const toml::node* name_node = assembly->get("name");
      if (!name_node || !name_node->is_string()) {
        continue;
      }
      const auto name_value = name_node->value<std::string>();
      if (!name_value.has_value()) {
        continue;
      }
      if (!names.insert(*name_value).second) {
        SPEC_RULE("WF-Assembly-Name-Dup-Err");
        return fail("E-PRJ-0202");
      }
    }
  }
  SPEC_RULE("WF-Assembly-Name-Dup");

  const std::string root_str = project_root.generic_string();

  for (const auto* assembly : asm_tables.tables) {
    if (!assembly) {
      SPEC_RULE("WF-Assembly-Table-Err");
      return fail("E-PRJ-0103");
    }

    for (const auto& [key, value] : *assembly) {
      (void)value;
      if (!IsKnownAssemblyKey(key)) {
        SPEC_RULE("WF-Assembly-Keys-Err");
        return fail("E-PRJ-0104");
      }
    }
    SPEC_RULE("WF-Assembly-Keys");

    const toml::node* name_node = assembly->get("name");
    const toml::node* kind_node = assembly->get("kind");
    const toml::node* root_node = assembly->get("root");
    if (!name_node || !kind_node || !root_node ||
        !name_node->is_string() || !kind_node->is_string() ||
        !root_node->is_string()) {
      SPEC_RULE("WF-Assembly-Required-Types-Err");
      return fail("E-PRJ-0103");
    }
    SPEC_RULE("WF-Assembly-Required-Types");

    const auto name_value = name_node->value<std::string>();
    const auto kind_value = kind_node->value<std::string>();
    const auto root_value = root_node->value<std::string>();
    if (!name_value || !kind_value || !root_value) {
      SPEC_RULE("WF-Assembly-Required-Types-Err");
      return fail("E-PRJ-0103");
    }

    std::optional<std::string> out_dir_value;
    if (const toml::node* out_dir_node = assembly->get("out_dir")) {
      if (!out_dir_node->is_string()) {
        SPEC_RULE("WF-Assembly-OutDirType-Err");
        return fail("E-PRJ-0301");
      }
      out_dir_value = out_dir_node->value<std::string>();
    }

    std::optional<std::string> emit_ir_value;
    if (const toml::node* emit_ir_node = assembly->get("emit_ir")) {
      if (!emit_ir_node->is_string()) {
        SPEC_RULE("WF-Assembly-EmitIRType-Err");
        return fail("E-PRJ-0204");
      }
      emit_ir_value = emit_ir_node->value<std::string>();
    }
    SPEC_RULE("WF-Assembly-EmitIRType");

    std::optional<std::string> link_kind_value;
    if (const toml::node* link_kind_node = assembly->get("link_kind")) {
      if (!link_kind_node->is_string()) {
        SPEC_RULE("WF-Assembly-LinkKindType-Err");
        return fail("E-PRJ-0207");
      }
      link_kind_value = link_kind_node->value<std::string>();
    }
    SPEC_RULE("WF-Assembly-LinkKindType");

    SPEC_RULE("WF-Assembly-Optional-Types");

    if (!core::IsName(*name_value)) {
      SPEC_RULE("WF-Assembly-Name-Err");
      return fail("E-PRJ-0203");
    }
    SPEC_RULE("WF-Assembly-Name");

    if (!(*kind_value == "executable" || *kind_value == "library" ||
          *kind_value == "dependency")) {
      SPEC_RULE("WF-Assembly-Kind-Err");
      return fail("E-PRJ-0201");
    }
    SPEC_RULE("WF-Assembly-Kind");

    if (*kind_value != "library" && link_kind_value.has_value()) {
      SPEC_RULE("WF-Assembly-LinkKind-Use-Err");
      return fail("E-PRJ-0208");
    }
    if (*kind_value == "library" && link_kind_value.has_value() &&
        !(*link_kind_value == "shared" || *link_kind_value == "static")) {
      SPEC_RULE("WF-Assembly-LinkKind-Err");
      return fail("E-PRJ-0207");
    }
    SPEC_RULE("WF-Assembly-LinkKind");

    if (emit_ir_value.has_value()) {
      const auto& emit_ir = *emit_ir_value;
      if (!(emit_ir == "none" || emit_ir == "ll" || emit_ir == "bc")) {
        SPEC_RULE("WF-Assembly-EmitIR-Err");
        return fail("E-PRJ-0204");
      }
    }
    SPEC_RULE("WF-Assembly-EmitIR");

    const RelPathStatus root_status = CheckRelPath(*root_value, root_str);
    if (root_status == RelPathStatus::RelPathErr) {
      SPEC_RULE("WF-Assembly-Root-Path-Err");
      return fail("E-PRJ-0301");
    }
    if (root_status == RelPathStatus::ResolveErr) {
      return fail("E-PRJ-0304");
    }
    SPEC_RULE("WF-Assembly-Root-Path");

    if (out_dir_value.has_value()) {
      const RelPathStatus out_dir_status =
          CheckRelPath(*out_dir_value, root_str);
      if (out_dir_status == RelPathStatus::RelPathErr) {
        SPEC_RULE("WF-Assembly-OutDir-Path-Err");
        return fail("E-PRJ-0301");
      }
      if (out_dir_status == RelPathStatus::ResolveErr) {
        return fail("E-PRJ-0304");
      }
    }
    SPEC_RULE("WF-Assembly-OutDir-Path");

    const std::optional<std::string> asm_link_kind =
        AsmLinkKind(*kind_value, link_kind_value);

    ValidatedAssembly validated{*name_value,
                                *kind_value,
                                asm_link_kind,
                                *root_value,
                                out_dir_value,
                                emit_ir_value};
    result.assemblies.push_back(std::move(validated));
    SPEC_RULE("WF-Assembly");
  }

  SPEC_RULE("ValidateManifest-Ok");
  return result;
}

bool IsProjectRoot(const std::filesystem::path& root) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(
      root / std::string(ActiveLanguageProfile().manifest_name), ec);
  if (ec) {
    core::HostPrimFail(core::HostPrim::FSExists, true);
    return false;
  }
  if (exists) {
    SPEC_RULE("WF-Project-Root");
  }
  return exists;
}

}  // namespace cursive::project
