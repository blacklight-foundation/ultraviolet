// =============================================================================
// parse_modules.cpp - Module-Level Parsing Orchestration
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.4.1-3.4.2 (Lines 6522-6582)
//
// This file implements module-level parsing:
//   - ReadBytesDefault: Read file bytes from filesystem
//   - ParseModuleWithDeps: Parse a single module with dependency injection
//   - ParseModulesWithDeps: Parse multiple modules in sequence
//   - ParseModule: Public entry point for single module
//   - ParseModules: Public entry point for multiple modules
//
// =============================================================================

#include "02_source/parser/parse_modules.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/process_config.h"
#include "00_core/diagnostics.h"
#include "00_core/host/services.h"
#include "00_core/host_primitives.h"
#include "02_source/lexer/keyword_policy.h"


namespace cursive::frontend {

// =============================================================================
// ReadBytesDefault - Default file reading implementation
// =============================================================================
//
// SPEC: ReadBytes-Ok (lines 6554-6557)
//   read_ok(f) = B
//   ----------------------------------------
//   ReadBytes(f) => B
//
// SPEC: ReadBytes-Err (lines 6559-6562)
//   read_ok(f) error    c = Code(ReadBytes-Err)
//   ----------------------------------------
//   ReadBytes(f) error c

ReadBytesResult ReadBytesDefault(const std::filesystem::path& path) {
  ReadBytesResult result;
  if (const auto force = core::HostGetEnvUtf8("CURSIVE_TEST_READ_BYTES_FAIL");
      force.has_value() && !force->empty()) {
    SPEC_RULE("ReadBytes-Err");
    core::HostPrimFail(core::HostPrim::ReadBytes, true);
    core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
    return result;
  }
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    SPEC_RULE("ReadBytes-Err");
    core::HostPrimFail(core::HostPrim::ReadBytes, true);
    core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
    return result;
  }

  const std::streamoff size_off = in.tellg();
  if (size_off < 0) {
    SPEC_RULE("ReadBytes-Err");
    core::HostPrimFail(core::HostPrim::ReadBytes, true);
    core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
    return result;
  }

  const std::size_t size = static_cast<std::size_t>(size_off);
  std::vector<std::uint8_t> bytes(size);
  in.seekg(0, std::ios::beg);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    if (!in) {
      SPEC_RULE("ReadBytes-Err");
      core::HostPrimFail(core::HostPrim::ReadBytes, true);
      core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
      return result;
    }
  }

  SPEC_RULE("ReadBytes-Ok");
  result.bytes = std::move(bytes);
  return result;
}

namespace {

// =============================================================================
// AppendDiags - Merge diagnostic streams
// =============================================================================

void AppendDiags(core::DiagnosticStream& out,
                 const core::DiagnosticStream& add) {
  for (const auto& diag : add) {
    core::Emit(out, diag);
  }
}

// Enforce MethodContextOk at parse/module aggregation time for top-level
// procedures: a top-level procedure parameter named `self` is method-only.
void CheckMethodContext(const ast::ASTFile& file, core::DiagnosticStream& diags) {
  for (const auto& item : file.items) {
    const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
    if (!proc) {
      continue;
    }
    for (const auto& param : proc->params) {
      if (param.name != "self") {
        continue;
      }
      if (auto diag = core::MakeDiagnosticById(
              "E-SEM-3011", std::optional<core::Span>(param.span))) {
        core::Emit(diags, *diag);
      }
      break;
    }
  }
}

// =============================================================================
// SplitModulePath - Split "a::b::c" into ["a", "b", "c"]
// =============================================================================

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

// =============================================================================
// DirOf - Compute directory path for a module
// =============================================================================
//
// SPEC: DirOf-Root (lines 6537-6540)
//   p = A
//   ----------------------------------------
//   DirOf(p, S) = S
//
// SPEC: DirOf-Rel (lines 6542-6545)
//   p = c_1 :: ... :: c_n    n >= 1
//   ----------------------------------------
//   DirOf(p, S) = S / c_1 / ... / c_n

std::filesystem::path DirOf(std::string_view module_path,
                            const std::filesystem::path& source_root,
                            std::string_view assembly_name) {
  if (module_path == assembly_name) {
    SPEC_RULE("DirOf-Root");
    return source_root;
  }
  SPEC_RULE("DirOf-Rel");
  const auto comps = SplitModulePath(module_path);
  std::filesystem::path dir = source_root;
  std::size_t start = 0;
  // Module paths may be assembly-qualified for cross-assembly uniqueness.
  // Directory layout under source_root remains relative to the assembly root.
  if (!comps.empty() && comps.front() == assembly_name) {
    start = 1;
  }
  for (std::size_t i = start; i < comps.size(); ++i) {
    dir /= comps[i];
  }
  return dir;
}

// =============================================================================
// DefaultDeps - Create default ParseModuleDeps
// =============================================================================

ParseModuleDeps DefaultDeps() {
	  ParseModuleDeps deps;
	  deps.compilation_unit = static_cast<project::CompilationUnitResult (*)(
	      const std::filesystem::path&)>(project::CompilationUnit);
  deps.read_bytes = ReadBytesDefault;
  deps.load_source = core::LoadSource;
  deps.parse_file = ast::ParseFile;
  return deps;
}

}  // namespace

// =============================================================================
// ParseModuleWithDeps - Parse a single module with dependency injection
// =============================================================================
//
// SPEC: ParseModule-Ok (lines 6566-6569)
//   forall i, ReadBytes(f_i) => B_i    LoadSource(f_i, B_i) => S_i    ParseFile(S_i) => F_i
//   ----------------------------------------
//   ParseModule(p, S) => <p, F_1.items ++ ... ++ F_n.items, F_1.module_doc ++ ... ++ F_n.module_doc>
//
// SPEC: ParseModule-Err-Read (lines 6571-6574)
//   exists i, ReadBytes(f_i) error c
//   ----------------------------------------
//   ParseModule(p, S) error c
//
// SPEC: ParseModule-Err-Load (lines 6576-6579)
//   exists i, ReadBytes(f_i) => B_i    LoadSource(f_i, B_i) error c
//   ----------------------------------------
//   ParseModule(p, S) error c

ParseModuleResult ParseModuleWithDeps(std::string_view module_path,
                                      const std::filesystem::path& source_root,
                                      std::string_view assembly_name,
                                      const ParseModuleDeps& deps) {
  ParseModuleResult result;
  const bool debug_phases = core::IsDebugEnabled("phases");
  const auto log_phase = [&](const char* label,
                             const std::filesystem::path& path) {
    if (debug_phases) {
      std::cerr << "[cursive] parse: " << label << " " << path.string()
                << "\n";
    }
  };

  SPEC_RULE("Mod-Start");
  const std::filesystem::path module_dir =
      DirOf(module_path, source_root, assembly_name);

  const project::CompilationUnitResult unit = deps.compilation_unit(module_dir);
  AppendDiags(result.diags, unit.diags);
  if (core::HasError(unit.diags)) {
    SPEC_RULE("Mod-Start-Err-Unit");
    SPEC_RULE("ParseModule-Err-Unit");
    return result;
  }

  std::vector<ast::ASTItem> items;
  std::vector<lexer::DocComment> docs;
  for (const auto& file : unit.files) {
    log_phase("read", file);
    const ReadBytesResult bytes = deps.read_bytes(file);
    AppendDiags(result.diags, bytes.diags);
    if (!bytes.bytes.has_value()) {
      SPEC_RULE("Mod-Scan-Err-Read");
      SPEC_RULE("ParseModule-Err-Read");
      return result;
    }
    const core::SourceLoadResult load =
        deps.load_source(file.generic_string(), *bytes.bytes);
    AppendDiags(result.diags, load.diags);
    if (!load.source.has_value()) {
      SPEC_RULE("Mod-Scan-Err-Load");
      SPEC_RULE("ParseModule-Err-Load");
      return result;
    }

    core::DiagnosticStream inspect_diags;
    if (deps.inspect_source) {
      log_phase("inspect", file);
      inspect_diags = deps.inspect_source(*load.source);
    }

    log_phase("parse", file);
    const ast::ParseFileResult parsed = deps.parse_file(*load.source);
    if (core::IsDebugEnabled("parse")) {
      std::cerr << "[cursive] parse: file=" << file.string()
                << " diags=" << parsed.diags.size()
                << " ok=" << (parsed.file.has_value() ? "yes" : "no") << "\n";
    }
    AppendDiags(result.diags, parsed.diags);
    AppendDiags(result.diags, inspect_diags);
    if (!parsed.file.has_value()) {
      SPEC_RULE("Mod-Scan-Err-Parse");
      SPEC_RULE("ParseModule-Err-Parse");
      return result;
    }

    CheckMethodContext(*parsed.file, result.diags);

    SPEC_RULE("Mod-Scan");
    items.insert(items.end(), parsed.file->items.begin(),
                 parsed.file->items.end());
    docs.insert(docs.end(), parsed.file->module_doc.begin(),
                parsed.file->module_doc.end());
    result.unsafe_spans_by_file[load.source->path] =
        std::move(parsed.unsafe_spans);
  }

  SPEC_RULE("Mod-Done");
  ast::ASTModule module;
  module.path = SplitModulePath(module_path);
  module.items = std::move(items);
  module.module_doc = std::move(docs);
  result.module = std::move(module);
  SPEC_RULE("ParseModule-Ok");
  return result;
}

// =============================================================================
// ParseModulesWithDeps - Parse multiple modules in sequence
// =============================================================================
//
// SPEC: ParseModules-Ok (line 257), ParseModules-Err (line 251)

ParseModulesResult ParseModulesWithDeps(
    const std::vector<project::ModuleInfo>& modules,
    const std::filesystem::path& source_root,
    std::string_view assembly_name,
    const ParseModuleDeps& deps) {
  ParseModulesResult result;

  std::vector<ast::ASTModule> parsed_modules;
  parsed_modules.reserve(modules.size());
  for (const auto& module : modules) {
    ParseModuleResult parsed =
        ParseModuleWithDeps(module.path, source_root, assembly_name, deps);
    AppendDiags(result.diags, parsed.diags);
    for (auto& [path, spans] : parsed.unsafe_spans_by_file) {
      result.unsafe_spans_by_file.insert_or_assign(std::move(path),
                                                   std::move(spans));
    }
    if (!parsed.module.has_value()) {
      SPEC_RULE("ParseModules-Err");
      return result;
    }
    parsed_modules.push_back(*parsed.module);
  }

  SPEC_RULE("ParseModules-Ok");
  SPEC_RULE("Phase1-Complete");
  SPEC_RULE("Phase1-Declarations");
  SPEC_RULE("Phase1-Forward-Refs");
  result.modules = std::move(parsed_modules);
  return result;
}

// =============================================================================
// ParseModule - Public entry point for single module
// =============================================================================

ParseModuleResult ParseModule(std::string_view module_path,
                              const std::filesystem::path& source_root,
                              std::string_view assembly_name) {
  return ParseModuleWithDeps(module_path, source_root, assembly_name,
                             DefaultDeps());
}

// =============================================================================
// ParseModules - Public entry point for multiple modules
// =============================================================================

ParseModulesResult ParseModules(const std::vector<project::ModuleInfo>& modules,
                                const std::filesystem::path& source_root,
                                std::string_view assembly_name) {
  return ParseModulesWithDeps(modules, source_root, assembly_name,
                              DefaultDeps());
}

}  // namespace cursive::frontend
