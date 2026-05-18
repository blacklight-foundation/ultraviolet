// =============================================================================
// MIGRATION MAPPING: manifest.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 2.1 (lines 779-1027)
//   - 2.1. Project Root and Manifest
//   - Manifest Parsing (Big-Step): ParseTOML, Parse-Manifest-Ok/Missing/Err
//   - Manifest Required (No Single-File Fallback)
//   - Manifest Path Resolution
//
// SOURCE FILE: ultraviolet-bootstrap/src/01_project/manifest.cpp
//   - Lines 1-109 (entire file)
//
// =============================================================================
// CONTENT TO MIGRATE
// =============================================================================
//
// 1. StartDirForInput() helper (lines 16-32)
//    - PURPOSE: Determine starting directory for project root search
//    - REFACTORING: Consider moving to 00_core/path module
//
// 2. MakeExternalDiag() helper (lines 34-40)
//    - PURPOSE: Create diagnostic without source span
//    - REFACTORING: Common helper - consolidate into core diagnostics module
//
// 3. FindProjectRoot() (lines 44-66)
//    - PURPOSE: Search upward from input path to find Ultraviolet.toml
//    - SPEC RULE: WF-Project-Root (line 1154-1159 in spec)
//    - DEPENDENCIES: std::filesystem, core diagnostics
//
// 4. ParseManifest() (lines 68-107)
//    - PURPOSE: Parse Ultraviolet.toml manifest file
//    - SPEC RULES:
//      * Parse-Manifest-Ok (spec lines 785-788)
//      * Parse-Manifest-Missing (spec lines 790-793)
//      * Parse-Manifest-Err (spec lines 795-798)
//    - DIAGNOSTICS:
//      * E-PRJ-0101: Manifest file not found
//      * E-PRJ-0102: Manifest parse error
//    - DEPENDENCIES:
//      * toml++ library (toml::parse_file)
//      * core::HostPrimFail for host primitive failure tracking
//      * core::MakeDiagnostic, core::Emit
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
//
// Headers required:
//   - "uv/01_project/manifest.h" (types: ManifestParseResult)
//   - "uv/00_core/assert_spec.h" (SPEC_RULE macro)
//   - "uv/00_core/diagnostic_messages.h" (MakeDiagnostic)
//   - "uv/00_core/diagnostics.h" (DiagnosticStream, Emit, HasError)
//   - "uv/00_core/host_primitives.h" (HostPrimFail)
//   - <filesystem>
//   - <exception>
//   - <system_error>
//   - toml++ (toml::parse_file, toml::parse_error)
//
// Types from header (manifest.h):
//   - ManifestParseResult { std::optional<toml::table> table; DiagnosticStream diags; }
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
//
// 1. MakeExternalDiag helper is duplicated across multiple files:
//    - manifest.cpp
//    - load_project.cpp
//    - module_discovery.cpp
//    - outputs.cpp
//    - link.cpp
//    - project_validate.cpp
//    - deterministic_order.cpp
//    RECOMMENDATION: Consolidate into core::MakeExternalDiag() in diagnostics module
//
// 2. Consider adding structured error context for ParseManifest:
//    - Capture TOML parse error details (line, column, message)
//    - Include file path in diagnostic
//
// 3. The spec defines:
//    - ParseTOML : Path -> TOMLTable (host primitive)
//    - The implementation wraps this with diagnostic emission
//
// =============================================================================
// SPEC RULE ANNOTATIONS (use SPEC_RULE macro)
// =============================================================================
//
// Line 75: SPEC_RULE("Parse-Manifest-Err");
// Line 84: SPEC_RULE("Parse-Manifest-Missing");
// Line 93: SPEC_RULE("Parse-Manifest-Ok");
// Line 101: SPEC_RULE("Parse-Manifest-Err");
//
// =============================================================================

#include "01_project/manifest.h"

#include <exception>
#include <filesystem>
#include <system_error>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/host_primitives.h"
#include "01_project/language_profile.h"

namespace ultraviolet::project {

namespace {

std::filesystem::path StartDirForInput(const std::filesystem::path& input_path) {
  std::filesystem::path dir = input_path;
  if (dir.empty()) {
    dir = ".";
  }
  std::error_code ec;
  if (dir.is_relative()) {
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec && !cwd.empty()) {
      dir = cwd / dir;
    }
  }
  dir = dir.lexically_normal();
  ec.clear();
  const bool exists = std::filesystem::exists(dir, ec);
  if (!ec && exists) {
    if (!std::filesystem::is_directory(dir, ec) && dir.has_filename()) {
      dir = dir.parent_path();
    }
  }
  if (dir.empty()) {
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
      dir = cwd;
    }
  }
  if (dir.empty()) {
    dir = ".";
  }
  return dir;
}

void EmitManifestDiagnostic(core::DiagnosticStream& diags,
                            std::string_view code,
                            const LanguageProfile& language) {
  auto diag = core::MakeExternalDiagnostic(code);
  if (!diag.has_value()) {
    core::EmitExternalDiagnostic(diags, code);
    return;
  }
  if (language.language == SourceLanguage::Ultraviolet) {
    if (code == "E-PRJ-0101") {
      diag->message = "`Ultraviolet.toml` not found at project root";
    } else if (code == "E-PRJ-0102") {
      diag->message = "`Ultraviolet.toml` is not valid TOML";
    }
  }
  core::Emit(diags, *diag);
}

}  // namespace

std::filesystem::path FindProjectRoot(const std::filesystem::path& input_path) {
  return FindProjectRoot(input_path, ActiveLanguageProfile());
}

std::filesystem::path FindProjectRoot(const std::filesystem::path& input_path,
                                      const LanguageProfile& language) {
  const std::filesystem::path start = StartDirForInput(input_path);
  std::filesystem::path current = start;

  for (;;) {
    std::error_code ec;
    const auto manifest_path = current / std::string(language.manifest_name);
    const bool exists = std::filesystem::exists(manifest_path, ec);
    if (ec) {
      return current;
    }
    if (exists) {
      return current;
    }
    const auto parent = current.parent_path();
    if (parent.empty() || parent == current) {
      break;
    }
    current = parent;
  }

  return start;
}

ManifestParseResult ParseManifest(const std::filesystem::path& project_root) {
  return ParseManifest(project_root, ActiveLanguageProfile());
}

ManifestParseResult ParseManifest(const std::filesystem::path& project_root,
                                  const LanguageProfile& language) {
  ManifestParseResult result;
  const auto manifest_path = project_root / std::string(language.manifest_name);

  std::error_code ec;
  const bool exists = std::filesystem::exists(manifest_path, ec);
	  if (ec) {
	    SPEC_RULE("Parse-Manifest-Err");
	    EmitManifestDiagnostic(result.diags, "E-PRJ-0102", language);
    core::HostPrimFail(core::HostPrim::ParseTOML, true);
    return result;
  }

	  if (!exists) {
	    SPEC_RULE("Parse-Manifest-Missing");
	    EmitManifestDiagnostic(result.diags, "E-PRJ-0101", language);
    return result;
  }

  try {
    result.table = toml::parse_file(manifest_path.string());
    SPEC_RULE("Parse-Manifest-Ok");
    return result;
  } catch (const toml::parse_error&) {
    // Fall through to error handling.
  } catch (const std::exception&) {
    // Fall through to error handling.
  }

	  SPEC_RULE("Parse-Manifest-Err");
	  EmitManifestDiagnostic(result.diags, "E-PRJ-0102", language);
  core::HostPrimFail(core::HostPrim::ParseTOML, true);
  return result;
}

}  // namespace ultraviolet::project
