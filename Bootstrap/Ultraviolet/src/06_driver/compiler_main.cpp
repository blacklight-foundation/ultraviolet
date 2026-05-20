// =============================================================================
// main.cpp - Compiler entry point
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md section 1.1 - Conformance and phase order
//   Docs/SPECIFICATION.md section 3 - Project and compilation model
//   Docs/SPECIFICATION.md section 15 - Program entry point and contracts
//   Docs/SPECIFICATION.md section 24 - Lowering, lifecycle, and backend
//
// Phase Orchestration:
//   Phase 0: Build/project validation
//   Phase 1: Parse and aggregate modules
//   Phase 2: Compile-time execution
//   Phase 3: Name resolution and type checking
//   Phase 4: Lowering and output pipeline
//
// Exit Code Semantics:
//   0 = Compilation succeeded (no errors)
//   1 = Compilation failed (at least one error)
//   2 = CLI parse error (invalid arguments)
//
// =============================================================================

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "06_driver/cli.h"
#include "06_driver/comptime_options.h"
#include "06_driver/compiler_main.h"
#include "06_driver/fingerprints.h"
#include "06_driver/incremental.h"
#include "06_driver/output_pipeline.h"
#include "06_driver/pipeline.h"
#include "06_driver/shared_library_exports.h"
#include "06_driver/test_discovery.h"
#include "06_driver/version.h"

#include "00_core/assert_spec.h"
#include "00_core/build_log_policy.h"
#include "00_core/crash_debug.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostic_render.h"
#include "00_core/diagnostics.h"
#include "00_core/hash.h"
#include "00_core/host/services.h"
#include "00_core/ident.h"
#include "00_core/process_config.h"
#include "00_core/source_load.h"
#include "00_core/spec_trace.h"
#include "00_core/symbols.h"
#include "00_core/terminal.h"
#include "00_core/unicode.h"
#include "00_core/compiler_support.h"
#include "01_project/ir_assembly.h"
#include "01_project/assemblies.h"
#include "01_project/ffi_library.h"
#include "01_project/language_profile.h"
#include "01_project/link.h"
#include "01_project/manifest.h"
#include "01_project/deterministic_order.h"
#include "01_project/module_discovery.h"
#include "01_project/outputs.h"
#include "01_project/project.h"
#include "01_project/tool_resolution.h"
#include "02_source/ast/ast_dump.h"
#include "03_comptime/comptime.h"
#include "02_source/parser/parse_modules.h"
#include "02_source/parser/parser.h"
#include "02_source/module_paths.h"
#include "04_analysis/attributes/ffi_library_attrs.h"
#include "04_analysis/caps/authority_model.h"
#include "04_analysis/caps/callgraph_caps.h"
#include "05_codegen/globals/globals.h"
#include "04_analysis/conformance/conformance.h"
#include "04_analysis/ffi/unwind_surface.h"
#include "04_analysis/resolve/assembly_import_graph.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/resolver.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/resolve/visibility.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/typecheck.h"
#include "05_codegen/ir/ir_dump.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_module.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/llvm/llvm_passes.h"

namespace {

using ultraviolet::driver::BuildIncrementalBuildData;
using ultraviolet::driver::BuildIncrementalBuildKey;
using ultraviolet::driver::ComputeLinkFingerprint;
using ultraviolet::driver::ComputeModuleSourceHash;
using ultraviolet::driver::HashFields;
using ultraviolet::driver::IncrementalEnabled;
using ultraviolet::driver::IncrementalManifestPath;
using ultraviolet::driver::IsExternalDependencyMarker;
using ultraviolet::driver::LoadIncrementalManifest;

std::vector<std::uint8_t> PathFilenameUtf8Bytes(
    const std::filesystem::path& path) {
  const auto utf8 = path.filename().u8string();
  std::vector<std::uint8_t> bytes;
  bytes.reserve(utf8.size());
  for (auto ch : utf8) {
    bytes.push_back(static_cast<std::uint8_t>(ch));
  }
  return bytes;
}

std::string DeriveAssemblyName(const std::filesystem::path& project_dir) {
  std::vector<ultraviolet::core::UnicodeScalar> out;
  const ultraviolet::core::DecodeResult decoded =
      ultraviolet::core::Decode(PathFilenameUtf8Bytes(project_dir));
  bool pending_separator = false;

  auto append_separator = [&]() {
    if (pending_separator) {
      return;
    }
    out.push_back('_');
    pending_separator = true;
  };

  if (decoded.ok) {
    out.reserve(decoded.scalars.size() + 1);
    for (ultraviolet::core::UnicodeScalar scalar : decoded.scalars) {
      if (out.empty()) {
        if (ultraviolet::core::IsIdentStart(scalar)) {
          out.push_back(scalar);
          pending_separator = false;
          continue;
        }
        if (ultraviolet::core::IsIdentContinue(scalar)) {
          out.push_back('_');
          out.push_back(scalar);
          pending_separator = false;
          continue;
        }
        append_separator();
        continue;
      }

      if (ultraviolet::core::IsIdentContinue(scalar)) {
        out.push_back(scalar);
        pending_separator = false;
      } else {
        append_separator();
      }
    }
  }

  std::string name = ultraviolet::core::EncodeUtf8(out);
  if (name.empty()) {
    name = "my_project";
  }
  if (ultraviolet::core::IsKeyword(name)) {
    name.push_back('_');
  }
  if (!ultraviolet::core::IsName(name)) {
    name = "my_project";
  }
  return name;
}

bool HasBlockingErrorsForSema(const ultraviolet::core::DiagnosticStream& diags) {
  for (const auto& diag : diags) {
    if (diag.severity != ultraviolet::core::Severity::Error) {
      continue;
    }
    return true;
  }
  return false;
}

std::size_t CountErrorDiagnostics(const ultraviolet::core::DiagnosticStream& diags) {
  return ultraviolet::analysis::CountErrorLikeDiagnostics(diags);
}

void EmitInternalDiagnostic(ultraviolet::core::DiagnosticStream& diags,
                            ultraviolet::core::Severity severity,
                            const std::optional<ultraviolet::core::Span>& span,
                            const std::string& message) {
  ultraviolet::core::Diagnostic diag;
  diag.severity = severity;
  diag.span = span;
  diag.message = message;
  ultraviolet::core::Emit(diags, diag);
}

void TruncateDiagnosticsToErrorCap(
    ultraviolet::core::DiagnosticStream& diags,
    const ultraviolet::core::ErrorRecoveryPolicy& policy) {
  if (!policy.max_error_count.has_value()) {
    return;
  }

  const std::size_t cap = *policy.max_error_count;
  std::size_t error_count = 0;

  ultraviolet::core::DiagnosticStream truncated;
  truncated.reserve(diags.size());

  for (const auto& diag : diags) {
    const bool is_error_like =
        diag.severity == ultraviolet::core::Severity::Error;
    if (is_error_like) {
      if (error_count >= cap) {
        break;
      }
      ++error_count;
      truncated.push_back(diag);
      if (error_count >= cap) {
        break;
      }
      continue;
    }
    truncated.push_back(diag);
  }

  diags = std::move(truncated);
}

void EmitAuthorityValidationDiagnostic(
    ultraviolet::core::DiagnosticStream& diags,
    const ultraviolet::analysis::AuthorityValidationResult& err) {
  const std::string code = err.error_code.empty() ? "E-CON-0020"
                                                   : err.error_code;
  const std::optional<ultraviolet::core::Span> span =
      err.span.file.empty() ? std::nullopt
                            : std::optional<ultraviolet::core::Span>(err.span);

  if (auto diag = ultraviolet::core::MakeDiagnosticById(code, span)) {
    if (!err.error_message.empty()) {
      diag->message = err.error_message;
    }
    ultraviolet::core::Emit(diags, *diag);
    return;
  }
  EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error, span,
                         err.error_message.empty()
                             ? "Authority validation failed."
                             : err.error_message);
}

void EmitCapabilityChainErrorDiagnostic(
    ultraviolet::core::DiagnosticStream& diags,
    const ultraviolet::analysis::CapabilityChainError& err) {
  const std::string code = err.code.empty() ? "E-CON-0020" : err.code;

  if (auto diag = ultraviolet::core::MakeDiagnosticById(code, err.span)) {
    if (!err.message.empty()) {
      diag->message = err.message;
    }
    ultraviolet::core::Emit(diags, *diag);
    return;
  }
  EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error, err.span,
                         err.message.empty()
                             ? "Capability chain validation failed."
                             : err.message);
}

void EmitCapabilityLeakDiagnostic(
    ultraviolet::core::DiagnosticStream& diags,
    const ultraviolet::analysis::CapabilityLeak& leak) {
  const std::string code = leak.code.empty() ? "E-TYP-2623" : leak.code;
  const std::optional<ultraviolet::core::Span> span =
      leak.leak_span.file.empty() ? std::nullopt
                                  : std::optional<ultraviolet::core::Span>(
                                        leak.leak_span);

  if (auto diag = ultraviolet::core::MakeDiagnosticById(code, span)) {
    if (!leak.message.empty()) {
      diag->message = leak.message;
    }
    ultraviolet::core::Emit(diags, *diag);
    return;
  }
  EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error, span,
                         leak.message.empty()
                             ? "Capability leaked to extern procedure."
                             : leak.message);
}

void EmitAttributeValidationDiagnostic(
    ultraviolet::core::DiagnosticStream& diags,
    const ultraviolet::analysis::AttributeValidationResult& err) {
  const std::string code =
      err.diag_id.has_value() ? std::string(*err.diag_id) : "E-MOD-2450";

  if (auto diag = ultraviolet::core::MakeDiagnosticById(code, err.span)) {
    if (!err.message.empty()) {
      diag->message = err.message;
    }
    ultraviolet::core::Emit(diags, *diag);
    return;
  }
  EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error, err.span,
                         err.message.empty()
                             ? "Attribute validation failed."
                             : err.message);
}

void EmitUnknownTestTargetDiagnostic(
    ultraviolet::core::DiagnosticStream& diags,
    const std::string& target) {
  if (auto diag = ultraviolet::core::MakeDiagnosticById("E-TST-0108")) {
    if (!target.empty()) {
      ultraviolet::core::SubDiagnostic note;
      note.kind = ultraviolet::core::SubDiagnosticKind::Note;
      note.message = "target: " + target;
      diag->children.push_back(std::move(note));
    }
    ultraviolet::core::Emit(diags, *diag);
    return;
  }
  EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error,
                         std::nullopt, "unknown uv test target");
}

void RenderDriverDiagnostics(
    const ultraviolet::core::DiagnosticStream& diags,
    const ultraviolet::driver::CliOptions& opts,
    ultraviolet::core::ColorOverride color_override) {
  if (opts.diag_json) {
    std::cout << ultraviolet::driver::DiagnosticStreamToJson(
        ultraviolet::core::Order(diags)) << "\n";
    return;
  }
  (void)color_override;
  for (const auto& diag : ultraviolet::core::Order(diags)) {
    std::cerr << ultraviolet::core::Render(diag) << "\n";
  }
}

bool ValidateParsedTypeAttributeLists(
    const std::vector<ultraviolet::ast::ASTModule>& modules,
    ultraviolet::core::DiagnosticStream& diags) {
  auto validate = [&](const auto& attrs, ultraviolet::analysis::AttributeTarget target)
      -> bool {
    if (!ultraviolet::analysis::HasAttribute(attrs,
                                         ultraviolet::analysis::attrs::kDerive)) {
      return true;
    }
    const auto result = ultraviolet::analysis::ValidateAttributes(attrs, target);
    if (result.ok) {
      return true;
    }
    EmitAttributeValidationDiagnostic(diags, result);
    return false;
  };

  for (const auto& module : modules) {
    for (const auto& item : module.items) {
      if (const auto* record = std::get_if<ultraviolet::ast::RecordDecl>(&item)) {
        if (!validate(record->attrs, ultraviolet::analysis::AttributeTarget::Record)) {
          return false;
        }
        continue;
      }
      if (const auto* enum_decl = std::get_if<ultraviolet::ast::EnumDecl>(&item)) {
        if (!validate(enum_decl->attrs, ultraviolet::analysis::AttributeTarget::Enum)) {
          return false;
        }
        continue;
      }
      if (const auto* modal = std::get_if<ultraviolet::ast::ModalDecl>(&item)) {
        if (!validate(modal->attrs, ultraviolet::analysis::AttributeTarget::Modal)) {
          return false;
        }
      }
    }
  }

  return true;
}

bool BuildProgressEnabled() {
  // Priority: CLI --build-progress > manifest [build] progress > default(true)
  const std::optional<bool> override = ultraviolet::core::BuildProgressOverride();
  if (override.has_value()) {
    return *override;
  }
  const std::optional<bool> manifest = ultraviolet::core::ManifestBuildProgress();
  if (manifest.has_value()) {
    return *manifest;
  }
  return true;
}

unsigned long CurrentProcessId() {
  return ultraviolet::core::CurrentHostProcessId();
}

std::filesystem::path g_compiler_executable_path;

std::filesystem::path ResolveCurrentExecutablePath(const char* argv0) {
  const auto current = ultraviolet::core::CurrentExecutablePath();
  if (!current.empty()) {
    return current;
  }

  if (argv0 && argv0[0] != '\0') {
    const std::filesystem::path raw(argv0);
    std::error_code ec;
    if (raw.is_absolute()) {
      return raw;
    }
    const auto abs = std::filesystem::absolute(raw, ec);
    if (!ec) {
      return abs;
    }
    return raw;
  }

  return {};
}

std::string ResolveCommandName(const char* argv0) {
  std::filesystem::path candidate = g_compiler_executable_path;
  if (candidate.empty() && argv0 && argv0[0] != '\0') {
    candidate = std::filesystem::path(argv0);
  }

  std::string name = candidate.filename().string();
  if (name.empty()) {
    return "uv";
  }
  return name;
}

bool HasTestHarnessBuildOptions(const ultraviolet::driver::CliOptions& opts) {
  return opts.test_harness_assembly.has_value() ||
         opts.test_harness_module.has_value() ||
         opts.test_harness_dir.has_value();
}

ultraviolet::project::OutputPaths OutputPathsUnder(
    const std::filesystem::path& root) {
  return ultraviolet::project::OutputPathsForRoot(root);
}

bool ModulePathStartsWithAssembly(std::string_view module_path,
                                  std::string_view assembly_name) {
  return module_path == assembly_name ||
         (module_path.size() > assembly_name.size() + 1 &&
          module_path.compare(0, assembly_name.size(), assembly_name) == 0 &&
          module_path[assembly_name.size()] == ':' &&
          module_path[assembly_name.size() + 1] == ':');
}

bool ApplyTestHarnessBuildOptions(ultraviolet::project::Project& project,
                                  const ultraviolet::driver::CliOptions& opts,
                                  ultraviolet::core::DiagnosticStream& diags) {
  if (!HasTestHarnessBuildOptions(opts)) {
    return true;
  }
  if (!opts.test_harness_assembly.has_value() ||
      !opts.test_harness_module.has_value() ||
      !opts.test_harness_dir.has_value()) {
    EmitInternalDiagnostic(
        diags, ultraviolet::core::Severity::Error, std::nullopt,
        "incomplete source-native test harness build options");
    return false;
  }
  if (!ModulePathStartsWithAssembly(*opts.test_harness_module,
                                    *opts.test_harness_assembly)) {
    EmitInternalDiagnostic(
        diags, ultraviolet::core::Severity::Error, std::nullopt,
        "source-native test harness module is outside the selected assembly");
    return false;
  }

  auto assembly_it = std::find_if(
      project.assemblies.begin(), project.assemblies.end(),
      [&](const ultraviolet::project::Assembly& assembly) {
        return assembly.name == *opts.test_harness_assembly;
      });
  if (assembly_it == project.assemblies.end()) {
    EmitInternalDiagnostic(
        diags, ultraviolet::core::Severity::Error, std::nullopt,
        "source-native test harness assembly was not found");
    return false;
  }

  ultraviolet::project::ModuleInfo harness_module;
  harness_module.path = *opts.test_harness_module;
  harness_module.dir = std::filesystem::path(*opts.test_harness_dir);

  auto existing_module = std::find_if(
      assembly_it->modules.begin(), assembly_it->modules.end(),
      [&](const ultraviolet::project::ModuleInfo& module) {
        return module.path == harness_module.path;
      });
  if (existing_module == assembly_it->modules.end()) {
    assembly_it->modules.push_back(std::move(harness_module));
  } else {
    existing_module->dir = harness_module.dir;
  }
  assembly_it->kind = "executable";
  assembly_it->link_kind.reset();

  if (project.assembly.name == assembly_it->name) {
    project.assembly = *assembly_it;
    project.source_root = assembly_it->source_root;
    project.outputs = assembly_it->outputs;
    project.modules = assembly_it->modules;
    project.lifecycle_modules = assembly_it->modules;
    project.test_harness_entry_module = *opts.test_harness_module;
  }

  return true;
}

std::string EscapeUvString(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::string HarnessModulePath(std::string_view assembly_name) {
  std::string path(assembly_name);
  path += "::Tests::GeneratedHarness";
  return path;
}

std::filesystem::path HarnessRootForAssembly(
    const ultraviolet::project::Assembly& assembly) {
  return assembly.outputs.root / "test-harness" / assembly.name;
}

std::string GenerateSourceNativeTestHarness(
    const std::vector<ultraviolet::driver::SourceNativeTestDescriptor>& tests,
    const std::filesystem::path& temporary_directory,
    ultraviolet::project::TargetProfile target_profile,
    const std::filesystem::path& current_directory) {
  std::ostringstream out;
  out << "//! Generated source-native test harness.\n\n";
  out << "public procedure main(context: Context) -> i32 {\n";
  out << "    var failures: i32 = 0\n";

  const bool needs_authority =
      std::any_of(tests.begin(), tests.end(), [](const auto& test) {
        return test.requires_context;
      });
  if (needs_authority) {
    out << "    let authority: TestAuthority = TestAuthority {\n";
    out << "        io: context.io,\n";
    out << "        sys: context.sys,\n";
    out << "        heap: context.heap,\n";
    out << "        temporary_directory: \""
        << EscapeUvString(temporary_directory.string()) << "\",\n";
    out << "        target_profile: \""
        << EscapeUvString(std::string(
               ultraviolet::project::TargetProfileName(target_profile)))
        << "\",\n";
    out << "        compiler_executable_path: \""
        << EscapeUvString(g_compiler_executable_path.string()) << "\",\n";
    out << "        compiler_current_directory: \""
        << EscapeUvString(current_directory.string()) << "\"\n";
    out << "    }\n";
  }

  for (const auto& test : tests) {
    out << "    if !" << test.stable_identity << "(";
    if (test.requires_context) {
      out << "authority";
    }
    out << ") {\n";
    out << "        failures += 1\n";
    out << "    }\n";
  }

  out << "    return failures\n";
  out << "}\n";
  return out.str();
}

bool WriteTextFile(const std::filesystem::path& path,
                   const std::string& contents,
                   ultraviolet::core::DiagnosticStream& diags) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error,
                           std::nullopt,
                           "failed to create test harness directory: " +
                               ec.message());
    return false;
  }

  std::ofstream file(path, std::ios::binary);
  if (!file) {
    EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error,
                           std::nullopt,
                           "failed to write test harness source: " +
                               path.string());
    return false;
  }
  file << contents;
  if (!file) {
    EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error,
                           std::nullopt,
                           "failed to flush test harness source: " +
                               path.string());
    return false;
  }
  return true;
}

std::optional<int> BuildAndRunSourceNativeTestHarness(
    const ultraviolet::project::Project& project,
    const ultraviolet::project::Assembly& assembly,
    const std::vector<ultraviolet::driver::SourceNativeTestDescriptor>& tests,
    ultraviolet::project::TargetProfile target_profile,
    const ultraviolet::driver::CliOptions& opts,
    const std::filesystem::path& current_directory,
    ultraviolet::core::DiagnosticStream& diags) {
  const std::filesystem::path harness_root = HarnessRootForAssembly(assembly);
  const std::filesystem::path harness_source_dir = harness_root / "Source";
  const std::filesystem::path harness_build_dir = harness_root / "Build";
  const std::string harness_module_path = HarnessModulePath(assembly.name);
  const std::filesystem::path harness_source =
      harness_source_dir / "Main.uv";

  const std::string source = GenerateSourceNativeTestHarness(
      tests, harness_root / "tmp", target_profile, current_directory);
  if (!WriteTextFile(harness_source, source, diags)) {
    return std::nullopt;
  }

  std::vector<std::string> build_args;
  build_args.push_back("build");
  build_args.push_back(project.root.string());
  build_args.push_back("--assembly");
  build_args.push_back(assembly.name);
  build_args.push_back("--target-profile");
  build_args.push_back(std::string(
      ultraviolet::project::TargetProfileName(target_profile)));
  build_args.push_back("--out-dir");
  build_args.push_back(harness_build_dir.string());
  build_args.push_back("--test-harness-assembly");
  build_args.push_back(assembly.name);
  build_args.push_back("--test-harness-module");
  build_args.push_back(harness_module_path);
  build_args.push_back("--test-harness-dir");
  build_args.push_back(harness_source_dir.string());
  build_args.push_back("--incremental");
  build_args.push_back("off");
  if (opts.build_progress.has_value()) {
    build_args.push_back("--build-progress");
    build_args.push_back(*opts.build_progress ? "on" : "off");
  }
  if (opts.runtime_lib_path.has_value()) {
    build_args.push_back("--runtime-lib");
    build_args.push_back(*opts.runtime_lib_path);
  }
  if (opts.link_debug.has_value()) {
    build_args.push_back("--link-debug");
    build_args.push_back(*opts.link_debug ? "on" : "off");
  }
  if (opts.max_errors_override.has_value()) {
    build_args.push_back("--max-errors");
    if (opts.max_errors_override->max_error_count.has_value()) {
      build_args.push_back(
          std::to_string(*opts.max_errors_override->max_error_count));
    } else {
      build_args.push_back("inf");
    }
  }
  if (opts.color_mode == ultraviolet::driver::ColorMode::Always) {
    build_args.push_back("--color=always");
  } else if (opts.color_mode == ultraviolet::driver::ColorMode::Never) {
    build_args.push_back("--color=never");
  }

  ultraviolet::core::HostProcessSpec build_spec;
  build_spec.program = g_compiler_executable_path;
  build_spec.arguments = std::move(build_args);
  build_spec.working_directory = project.root;
  build_spec.output_mode =
      ultraviolet::core::HostProcessOutputMode::Inherit;
  const auto build_result = ultraviolet::core::RunHostProcess(build_spec);
  if (!build_result.launched) {
    EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error,
                           std::nullopt,
                           "failed to launch source-native test harness build: " +
                               build_result.error_message);
    return std::nullopt;
  }
  if (build_result.exit_code != 0) {
    return build_result.exit_code;
  }

  ultraviolet::project::Assembly harness_assembly = assembly;
  harness_assembly.kind = "executable";
  harness_assembly.link_kind.reset();
  harness_assembly.outputs = OutputPathsUnder(harness_build_dir);
  ultraviolet::project::ModuleInfo harness_module;
  harness_module.path = harness_module_path;
  harness_module.dir = harness_source_dir;
  harness_assembly.modules.push_back(std::move(harness_module));
  const auto harness_project =
      ultraviolet::project::AssemblyProject(project, harness_assembly);
  const std::filesystem::path executable =
      ultraviolet::project::ExePath(harness_project, target_profile);

  ultraviolet::core::HostProcessSpec run_spec;
  run_spec.program = executable;
  run_spec.working_directory = project.root;
  run_spec.output_mode = ultraviolet::core::HostProcessOutputMode::Inherit;
  const auto run_result = ultraviolet::core::RunHostProcess(run_spec);
  if (!run_result.launched) {
    EmitInternalDiagnostic(diags, ultraviolet::core::Severity::Error,
                           std::nullopt,
                           "failed to launch source-native test harness: " +
                               run_result.error_message);
    return std::nullopt;
  }
  return run_result.exit_code;
}

const char* VisibilitySignature(ultraviolet::ast::Visibility vis) {
  switch (vis) {
    case ultraviolet::ast::Visibility::Public:
      return "public";
    case ultraviolet::ast::Visibility::Internal:
      return "internal";
    case ultraviolet::ast::Visibility::Private:
      return "private";
  }
  return "unknown";
}

const char* MutabilitySignature(ultraviolet::ast::Mutability mut) {
  switch (mut) {
    case ultraviolet::ast::Mutability::Let:
      return "let";
    case ultraviolet::ast::Mutability::Var:
      return "var";
  }
  return "unknown";
}

const char* ParamModeSignature(ultraviolet::ast::ParamMode mode) {
  switch (mode) {
    case ultraviolet::ast::ParamMode::Move:
      return "move";
  }
  return "unknown";
}

const char* ReceiverPermSignature(ultraviolet::ast::ReceiverPerm perm) {
  switch (perm) {
    case ultraviolet::ast::ReceiverPerm::Const:
      return "const";
    case ultraviolet::ast::ReceiverPerm::Unique:
      return "unique";
    case ultraviolet::ast::ReceiverPerm::Shared:
      return "shared";
  }
  return "unknown";
}

const char* TypePermSignature(ultraviolet::ast::TypePerm perm) {
  switch (perm) {
    case ultraviolet::ast::TypePerm::Const:
      return "const";
    case ultraviolet::ast::TypePerm::Unique:
      return "unique";
    case ultraviolet::ast::TypePerm::Shared:
      return "shared";
  }
  return "unknown";
}

const char* RawPtrQualSignature(ultraviolet::ast::RawPtrQual qual) {
  switch (qual) {
    case ultraviolet::ast::RawPtrQual::Imm:
      return "imm";
    case ultraviolet::ast::RawPtrQual::Mut:
      return "mut";
  }
  return "unknown";
}

const char* PtrStateSignature(ultraviolet::ast::PtrState state) {
  switch (state) {
    case ultraviolet::ast::PtrState::Valid:
      return "Valid";
    case ultraviolet::ast::PtrState::Null:
      return "Null";
    case ultraviolet::ast::PtrState::Expired:
      return "Expired";
  }
  return "unknown";
}

const char* StringStateSignature(ultraviolet::ast::StringState state) {
  switch (state) {
    case ultraviolet::ast::StringState::Managed:
      return "Managed";
    case ultraviolet::ast::StringState::View:
      return "View";
  }
  return "unknown";
}

const char* BytesStateSignature(ultraviolet::ast::BytesState state) {
  switch (state) {
    case ultraviolet::ast::BytesState::Managed:
      return "Managed";
    case ultraviolet::ast::BytesState::View:
      return "View";
  }
  return "unknown";
}

const char* VarianceSignature(ultraviolet::ast::Variance variance) {
  switch (variance) {
    case ultraviolet::ast::Variance::Covariant:
      return "covariant";
    case ultraviolet::ast::Variance::Contravariant:
      return "contravariant";
    case ultraviolet::ast::Variance::Invariant:
      return "invariant";
    case ultraviolet::ast::Variance::Bivariant:
      return "bivariant";
  }
  return "unknown";
}

const char* ForeignContractKindSignature(ultraviolet::ast::ForeignContractKind kind) {
  switch (kind) {
    case ultraviolet::ast::ForeignContractKind::Assumes:
      return "assumes";
    case ultraviolet::ast::ForeignContractKind::Ensures:
      return "ensures";
    case ultraviolet::ast::ForeignContractKind::EnsuresError:
      return "ensures_error";
    case ultraviolet::ast::ForeignContractKind::EnsuresNullResult:
      return "ensures_null_result";
  }
  return "unknown";
}

const char* TokenKindSignature(ultraviolet::lexer::TokenKind kind) {
  switch (kind) {
    case ultraviolet::lexer::TokenKind::Identifier:
      return "identifier";
    case ultraviolet::lexer::TokenKind::Keyword:
      return "keyword";
    case ultraviolet::lexer::TokenKind::IntLiteral:
      return "int";
    case ultraviolet::lexer::TokenKind::FloatLiteral:
      return "float";
    case ultraviolet::lexer::TokenKind::StringLiteral:
      return "string";
    case ultraviolet::lexer::TokenKind::CharLiteral:
      return "char";
    case ultraviolet::lexer::TokenKind::BoolLiteral:
      return "bool";
    case ultraviolet::lexer::TokenKind::NullLiteral:
      return "null";
    case ultraviolet::lexer::TokenKind::Operator:
      return "operator";
    case ultraviolet::lexer::TokenKind::Punctuator:
      return "punctuator";
    case ultraviolet::lexer::TokenKind::Newline:
      return "newline";
    case ultraviolet::lexer::TokenKind::Eof:
      return "eof";
    case ultraviolet::lexer::TokenKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string PathSignature(const ultraviolet::ast::Path& path) {
  return ultraviolet::core::StringOfPath(path);
}

void AppendSignatureAtom(std::string& out, std::string_view value) {
  out.append(std::to_string(value.size()));
  out.push_back(':');
  out.append(value);
  out.push_back(';');
}

void AppendSignatureBool(std::string& out, bool value) {
  AppendSignatureAtom(out, value ? "1" : "0");
}

void AppendSignatureCount(std::string& out, std::size_t value) {
  AppendSignatureAtom(out, std::to_string(value));
}

struct SourceTextCache {
  explicit SourceTextCache(ultraviolet::core::DiagnosticStream& diags)
      : diags(diags) {}

  const ultraviolet::core::SourceFile* Load(std::string_view path) {
    if (path.empty()) {
      return nullptr;
    }
    const std::string key(path);
    if (failed.find(key) != failed.end()) {
      return nullptr;
    }
    if (const auto it = loaded.find(key); it != loaded.end()) {
      return &it->second;
    }

    const auto bytes = ultraviolet::frontend::ReadBytesDefault(key);
    for (const auto& diag : bytes.diags) {
      ultraviolet::core::Emit(diags, diag);
    }
    if (!bytes.bytes.has_value()) {
      failed.insert(key);
      return nullptr;
    }

    auto source = ultraviolet::core::LoadSource(key, *bytes.bytes);
    for (const auto& diag : source.diags) {
      ultraviolet::core::Emit(diags, diag);
    }
    if (!source.source.has_value()) {
      failed.insert(key);
      return nullptr;
    }

    const auto [it, _inserted] = loaded.emplace(key, std::move(*source.source));
    return &it->second;
  }

  ultraviolet::core::DiagnosticStream& diags;
  std::unordered_map<std::string, ultraviolet::core::SourceFile> loaded;
  std::unordered_set<std::string> failed;
};

std::string SpanText(SourceTextCache& sources, const ultraviolet::core::Span& span) {
  const auto* source = sources.Load(span.file);
  if (source != nullptr && span.start_offset <= span.end_offset &&
      span.end_offset <= source->text.size()) {
    return source->text.substr(span.start_offset,
                               span.end_offset - span.start_offset);
  }
  std::vector<std::string> fields;
  fields.push_back("span");
  fields.push_back(span.file);
  fields.push_back(std::to_string(span.start_offset));
  fields.push_back(std::to_string(span.end_offset));
  return HashFields(fields);
}

std::string ExprSignature(SourceTextCache& sources,
                          const ultraviolet::ast::ExprPtr& expr) {
  if (!expr) {
    return "none";
  }
  return SpanText(sources, expr->span);
}

std::string PatternSignature(SourceTextCache& sources,
                             const ultraviolet::ast::PatternPtr& pattern) {
  if (!pattern) {
    return "none";
  }
  return SpanText(sources, pattern->span);
}

std::string TypeSignature(SourceTextCache& sources,
                          const ultraviolet::ast::TypePtr& type);

void AppendTypeVectorSignature(SourceTextCache& sources,
                               std::string& out,
                               const std::vector<ultraviolet::ast::TypePtr>& types) {
  AppendSignatureCount(out, types.size());
  for (const auto& type : types) {
    AppendSignatureAtom(out, TypeSignature(sources, type));
  }
}

std::string ModalRefSignature(SourceTextCache& sources,
                              const ultraviolet::ast::TypeModalRef& modal_ref) {
  std::string out;
  AppendSignatureAtom(out, PathSignature(ultraviolet::ast::TypeModalRefPath(modal_ref)));
  AppendTypeVectorSignature(sources, out, ultraviolet::ast::TypeModalRefArgs(modal_ref));
  return out;
}

std::string TypeSignature(SourceTextCache& sources,
                          const ultraviolet::ast::TypePtr& type) {
  if (!type) {
    return "none";
  }

  std::string out;
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::TypePrim>) {
          AppendSignatureAtom(out, "prim");
          AppendSignatureAtom(out, node.name);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypePermType>) {
          AppendSignatureAtom(out, "perm");
          AppendSignatureAtom(out, TypePermSignature(node.perm));
          AppendSignatureAtom(out, TypeSignature(sources, node.base));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeUnion>) {
          AppendSignatureAtom(out, "union");
          std::vector<std::string> members;
          members.reserve(node.types.size());
          for (const auto& member : node.types) {
            members.push_back(TypeSignature(sources, member));
          }
          std::sort(members.begin(), members.end());
          AppendSignatureCount(out, members.size());
          for (const auto& member : members) {
            AppendSignatureAtom(out, member);
          }
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeFunc>) {
          AppendSignatureAtom(out, "func");
          AppendSignatureCount(out, node.params.size());
          for (const auto& param : node.params) {
            AppendSignatureBool(out, param.mode.has_value());
            if (param.mode.has_value()) {
              AppendSignatureAtom(out, ParamModeSignature(*param.mode));
            }
            AppendSignatureAtom(out, TypeSignature(sources, param.type));
          }
          AppendSignatureAtom(out, TypeSignature(sources, node.ret));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeClosure>) {
          AppendSignatureAtom(out, "closure");
          AppendSignatureCount(out, node.params.size());
          for (const auto& param : node.params) {
            AppendSignatureBool(out, param.mode.has_value());
            if (param.mode.has_value()) {
              AppendSignatureAtom(out, ParamModeSignature(*param.mode));
            }
            AppendSignatureAtom(out, TypeSignature(sources, param.type));
          }
          AppendSignatureAtom(out, TypeSignature(sources, node.ret));
          AppendSignatureBool(out, node.deps_opt.has_value());
          if (node.deps_opt.has_value()) {
            AppendSignatureCount(out, node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              AppendSignatureAtom(out, dep.name);
              AppendSignatureAtom(out, TypeSignature(sources, dep.type));
            }
          }
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeTuple>) {
          AppendSignatureAtom(out, "tuple");
          AppendTypeVectorSignature(sources, out, node.elements);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeArray>) {
          AppendSignatureAtom(out, "array");
          AppendSignatureAtom(out, TypeSignature(sources, node.element));
          AppendSignatureAtom(out, ExprSignature(sources, node.length));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeSlice>) {
          AppendSignatureAtom(out, "slice");
          AppendSignatureAtom(out, TypeSignature(sources, node.element));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeSafePtr>) {
          AppendSignatureAtom(out, "ptr");
          AppendSignatureAtom(out, TypeSignature(sources, node.element));
          AppendSignatureBool(out, node.state.has_value());
          if (node.state.has_value()) {
            AppendSignatureAtom(out, PtrStateSignature(*node.state));
          }
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeRawPtr>) {
          AppendSignatureAtom(out, "rawptr");
          AppendSignatureAtom(out, RawPtrQualSignature(node.qual));
          AppendSignatureAtom(out, TypeSignature(sources, node.element));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeString>) {
          AppendSignatureAtom(out, "string");
          AppendSignatureBool(out, node.state.has_value());
          if (node.state.has_value()) {
            AppendSignatureAtom(out, StringStateSignature(*node.state));
          }
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeBytes>) {
          AppendSignatureAtom(out, "bytes");
          AppendSignatureBool(out, node.state.has_value());
          if (node.state.has_value()) {
            AppendSignatureAtom(out, BytesStateSignature(*node.state));
          }
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeDynamic>) {
          AppendSignatureAtom(out, "dynamic");
          AppendSignatureAtom(out, PathSignature(node.path));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeModalState>) {
          AppendSignatureAtom(out, "modal-state");
          AppendSignatureAtom(out, ModalRefSignature(sources, node.modal_ref));
          AppendSignatureAtom(out, node.state);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypePathType>) {
          AppendSignatureAtom(out, "path");
          AppendSignatureAtom(out, PathSignature(node.path));
          AppendTypeVectorSignature(sources, out, node.generic_args);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeApply>) {
          AppendSignatureAtom(out, "apply");
          AppendSignatureAtom(out, PathSignature(node.path));
          AppendTypeVectorSignature(sources, out, node.args);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::SpliceExprNode>) {
          AppendSignatureAtom(out, "splice");
          AppendSignatureAtom(out, ExprSignature(sources, node.expr));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeOpaque>) {
          AppendSignatureAtom(out, "opaque");
          AppendSignatureAtom(out, PathSignature(node.path));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeRefine>) {
          AppendSignatureAtom(out, "refine");
          AppendSignatureAtom(out, TypeSignature(sources, node.base));
          AppendSignatureAtom(out, ExprSignature(sources, node.predicate));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeRange>) {
          AppendSignatureAtom(out, "range");
          AppendSignatureAtom(out, TypeSignature(sources, node.base));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeRangeInclusive>) {
          AppendSignatureAtom(out, "range-inclusive");
          AppendSignatureAtom(out, TypeSignature(sources, node.base));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeRangeFrom>) {
          AppendSignatureAtom(out, "range-from");
          AppendSignatureAtom(out, TypeSignature(sources, node.base));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeRangeTo>) {
          AppendSignatureAtom(out, "range-to");
          AppendSignatureAtom(out, TypeSignature(sources, node.base));
        } else if constexpr (std::is_same_v<T,
                                            ultraviolet::ast::TypeRangeToInclusive>) {
          AppendSignatureAtom(out, "range-to-inclusive");
          AppendSignatureAtom(out, TypeSignature(sources, node.base));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeRangeFull>) {
          AppendSignatureAtom(out, "range-full");
        }
      },
      type->node);
  return out;
}

std::string AttributeArgSignature(const ultraviolet::ast::AttributeArg& arg) {
  std::string out;
  AppendSignatureBool(out, arg.key.has_value());
  if (arg.key.has_value()) {
    AppendSignatureAtom(out, *arg.key);
  }
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ultraviolet::lexer::Token>) {
          AppendSignatureAtom(out, "token");
          AppendSignatureAtom(out, TokenKindSignature(value.kind));
          AppendSignatureAtom(out, value.lexeme);
        } else {
          AppendSignatureAtom(out, "args");
          AppendSignatureCount(out, value.size());
          for (const auto& nested : value) {
            AppendSignatureAtom(out, AttributeArgSignature(nested));
          }
        }
      },
      arg.value);
  return out;
}

void AppendAttributesSignature(std::vector<std::string>& fields,
                               const ultraviolet::ast::AttributeList& attrs) {
  fields.push_back("attrs=" + std::to_string(attrs.size()));
  for (const auto& attr : attrs) {
    std::string out;
    AppendSignatureAtom(out, attr.name.full_name);
    AppendSignatureCount(out, attr.args.size());
    for (const auto& arg : attr.args) {
      AppendSignatureAtom(out, AttributeArgSignature(arg));
    }
    fields.push_back("attr=" + out);
  }
}

void AppendAttributesSignature(std::vector<std::string>& fields,
                               const ultraviolet::ast::AttrOpt& attrs_opt) {
  AppendAttributesSignature(fields, ultraviolet::ast::AttrListOf(attrs_opt));
}

void AppendGenericSignature(
    SourceTextCache& sources,
    std::vector<std::string>& fields,
    const std::optional<ultraviolet::ast::GenericParams>& params_opt) {
  fields.push_back(std::string("generic=") +
                   (params_opt.has_value() ? "1" : "0"));
  if (!params_opt.has_value()) {
    return;
  }
  fields.push_back("generic-count=" + std::to_string(params_opt->params.size()));
  for (const auto& param : params_opt->params) {
    std::string out;
    AppendSignatureAtom(out, param.name);
    AppendSignatureBool(out, param.variance.has_value());
    if (param.variance.has_value()) {
      AppendSignatureAtom(out, VarianceSignature(*param.variance));
    }
    AppendSignatureCount(out, param.bounds.size());
    for (const auto& bound : param.bounds) {
      AppendSignatureAtom(out, PathSignature(bound.class_path));
      AppendTypeVectorSignature(sources, out, bound.generic_args);
    }
    AppendSignatureAtom(out, TypeSignature(sources, param.default_type));
    fields.push_back("generic-param=" + out);
  }
}

void AppendPredicateSignature(
    SourceTextCache& sources,
    std::vector<std::string>& fields,
    const std::optional<ultraviolet::ast::PredicateClause>& predicates_opt) {
  fields.push_back(std::string("predicates=") +
                   (predicates_opt.has_value() ? "1" : "0"));
  if (!predicates_opt.has_value()) {
    return;
  }
  fields.push_back("predicate-count=" + std::to_string(predicates_opt->size()));
  for (const auto& predicate : *predicates_opt) {
    std::string out;
    AppendSignatureAtom(out, predicate.pred);
    AppendSignatureAtom(out, TypeSignature(sources, predicate.type));
    fields.push_back("predicate=" + out);
  }
}

std::string ParamSignature(SourceTextCache& sources,
                           const ultraviolet::ast::Param& param) {
  std::string out;
  AppendSignatureAtom(out, param.name);
  AppendSignatureBool(out, param.mode.has_value());
  if (param.mode.has_value()) {
    AppendSignatureAtom(out, ParamModeSignature(*param.mode));
  }
  AppendSignatureAtom(out, TypeSignature(sources, param.type));
  return out;
}

void AppendParamsSignature(SourceTextCache& sources,
                           std::vector<std::string>& fields,
                           const std::vector<ultraviolet::ast::Param>& params) {
  fields.push_back("params=" + std::to_string(params.size()));
  for (const auto& param : params) {
    fields.push_back("param=" + ParamSignature(sources, param));
  }
}

std::string ReceiverSignature(SourceTextCache& sources,
                              const ultraviolet::ast::Receiver& receiver) {
  std::string out;
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::ReceiverShorthand>) {
          AppendSignatureAtom(out, "shorthand");
          AppendSignatureAtom(out, ReceiverPermSignature(node.perm));
        } else {
          AppendSignatureAtom(out, "explicit");
          AppendSignatureBool(out, node.mode_opt.has_value());
          if (node.mode_opt.has_value()) {
            AppendSignatureAtom(out, ParamModeSignature(*node.mode_opt));
          }
          AppendSignatureAtom(out, TypeSignature(sources, node.type));
        }
      },
      receiver);
  return out;
}

void AppendContractSignature(
    SourceTextCache& sources,
    std::vector<std::string>& fields,
    const std::optional<ultraviolet::ast::ContractClause>& contract_opt) {
  fields.push_back(std::string("contract=") +
                   (contract_opt.has_value() ? "1" : "0"));
  if (!contract_opt.has_value()) {
    return;
  }
  fields.push_back("contract-pre=" +
                   ExprSignature(sources, contract_opt->precondition));
  fields.push_back("contract-post=" +
                   ExprSignature(sources, contract_opt->postcondition));
}

void AppendForeignContractsSignature(
    SourceTextCache& sources,
    std::vector<std::string>& fields,
    const std::optional<std::vector<ultraviolet::ast::ForeignContractClause>>&
        contracts_opt) {
  fields.push_back(std::string("foreign-contracts=") +
                   (contracts_opt.has_value() ? "1" : "0"));
  if (!contracts_opt.has_value()) {
    return;
  }
  fields.push_back("foreign-contract-count=" +
                   std::to_string(contracts_opt->size()));
  for (const auto& contract : *contracts_opt) {
    std::string out;
    AppendSignatureAtom(out, ForeignContractKindSignature(contract.kind));
    AppendSignatureCount(out, contract.predicates.size());
    for (const auto& predicate : contract.predicates) {
      AppendSignatureAtom(out, ExprSignature(sources, predicate));
    }
    fields.push_back("foreign-contract=" + out);
  }
}

void AppendInvariantSignature(
    SourceTextCache& sources,
    std::vector<std::string>& fields,
    const std::optional<ultraviolet::ast::TypeInvariant>& invariant_opt) {
  fields.push_back(std::string("invariant=") +
                   (invariant_opt.has_value() ? "1" : "0"));
  if (invariant_opt.has_value()) {
    fields.push_back("invariant-predicate=" +
                     ExprSignature(sources, invariant_opt->predicate));
  }
}

std::string ExternAbiSignature(const std::optional<ultraviolet::ast::ExternAbi>& abi) {
  if (!abi.has_value()) {
    return "none";
  }
  std::string out;
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::ExternAbiString>) {
          AppendSignatureAtom(out, "string");
          AppendSignatureAtom(out, node.literal.lexeme);
        } else {
          AppendSignatureAtom(out, "ident");
          AppendSignatureAtom(out, node.name);
        }
      },
      *abi);
  return out;
}

void AppendCallableSignature(
    SourceTextCache& sources,
    std::vector<std::string>& fields,
    std::string_view kind,
    const ultraviolet::ast::AttributeList& attrs,
    ultraviolet::ast::Visibility vis,
    std::string_view name,
    const std::optional<ultraviolet::ast::GenericParams>& generic_params,
    const std::optional<ultraviolet::ast::PredicateClause>& predicate_clause,
    const std::vector<ultraviolet::ast::Param>& params,
    const ultraviolet::ast::TypePtr& return_type,
    const std::optional<ultraviolet::ast::ContractClause>& contract) {
  fields.push_back("item=" + std::string(kind));
  AppendAttributesSignature(fields, attrs);
  fields.push_back("vis=" + std::string(VisibilitySignature(vis)));
  fields.push_back("name=" + std::string(name));
  AppendGenericSignature(sources, fields, generic_params);
  AppendPredicateSignature(sources, fields, predicate_clause);
  AppendParamsSignature(sources, fields, params);
  fields.push_back("return=" + TypeSignature(sources, return_type));
  AppendContractSignature(sources, fields, contract);
}

void AppendFieldSignature(SourceTextCache& sources,
                          std::vector<std::string>& fields,
                          const ultraviolet::ast::FieldDecl& field,
                          std::string_view kind) {
  fields.push_back("member=" + std::string(kind));
  AppendAttributesSignature(fields, field.attrs);
  fields.push_back("vis=" + std::string(VisibilitySignature(field.vis)));
  fields.push_back(std::string("key-boundary=") +
                   (field.key_boundary ? "1" : "0"));
  fields.push_back("name=" + field.name);
  fields.push_back("type=" + TypeSignature(sources, field.type));
  fields.push_back("init=" + ExprSignature(sources, field.init_opt));
}

void AppendMethodSignature(SourceTextCache& sources,
                           std::vector<std::string>& fields,
                           const ultraviolet::ast::MethodDecl& method,
                           std::string_view kind) {
  fields.push_back("member=" + std::string(kind));
  AppendAttributesSignature(fields, method.attrs);
  fields.push_back("vis=" + std::string(VisibilitySignature(method.vis)));
  fields.push_back(std::string("override=") +
                   (method.override_flag ? "1" : "0"));
  fields.push_back("name=" + method.name);
  AppendGenericSignature(sources, fields, method.generic_params);
  fields.push_back("receiver=" + ReceiverSignature(sources, method.receiver));
  AppendParamsSignature(sources, fields, method.params);
  fields.push_back("return=" + TypeSignature(sources, method.return_type_opt));
  AppendContractSignature(sources, fields, method.contract);
}

std::string VariantPayloadSignature(
    SourceTextCache& sources,
    const std::optional<ultraviolet::ast::VariantPayload>& payload_opt) {
  if (!payload_opt.has_value()) {
    return "none";
  }
  std::string out;
  std::visit(
      [&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::VariantPayloadTuple>) {
          AppendSignatureAtom(out, "tuple");
          AppendTypeVectorSignature(sources, out, payload.elements);
        } else {
          AppendSignatureAtom(out, "record");
          AppendSignatureCount(out, payload.fields.size());
          for (const auto& field : payload.fields) {
            std::vector<std::string> field_fields;
            AppendFieldSignature(sources, field_fields, field, "variant-field");
            AppendSignatureAtom(out, HashFields(field_fields));
          }
        }
      },
      *payload_opt);
  return out;
}

std::string ClassItemSignature(SourceTextCache& sources,
                               const ultraviolet::ast::ClassItem& item) {
  std::vector<std::string> fields;
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::ClassFieldDecl>) {
          fields.push_back("class-member=field");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back(std::string("key-boundary=") +
                           (node.key_boundary ? "1" : "0"));
          fields.push_back("name=" + node.name);
          fields.push_back("type=" + TypeSignature(sources, node.type));
        } else if constexpr (std::is_same_v<T,
                                            ultraviolet::ast::ClassMethodDecl>) {
          fields.push_back("class-member=method");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("name=" + node.name);
          AppendGenericSignature(sources, fields, node.generic_params);
          fields.push_back("receiver=" +
                           ReceiverSignature(sources, node.receiver));
          AppendParamsSignature(sources, fields, node.params);
          fields.push_back("return=" +
                           TypeSignature(sources, node.return_type_opt));
          AppendContractSignature(sources, fields, node.contract);
          fields.push_back(std::string("default-body=") +
                           (node.body_opt ? "1" : "0"));
        } else if constexpr (std::is_same_v<T,
                                            ultraviolet::ast::AssociatedTypeDecl>) {
          fields.push_back("class-member=assoc-type");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("name=" + node.name);
          fields.push_back("default=" +
                           TypeSignature(sources, node.default_type));
        } else if constexpr (std::is_same_v<T,
                                            ultraviolet::ast::AbstractFieldDecl>) {
          fields.push_back("class-member=abstract-field");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back(std::string("key-boundary=") +
                           (node.key_boundary ? "1" : "0"));
          fields.push_back("name=" + node.name);
          fields.push_back("type=" + TypeSignature(sources, node.type));
        } else if constexpr (std::is_same_v<T,
                                            ultraviolet::ast::AbstractStateDecl>) {
          fields.push_back("class-member=abstract-state");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("name=" + node.name);
          fields.push_back("fields=" + std::to_string(node.fields.size()));
          for (const auto& field : node.fields) {
            fields.push_back("field=" + ClassItemSignature(
                                            sources,
                                            ultraviolet::ast::ClassItem{field}));
          }
        }
      },
      item);
  return HashFields(fields);
}

std::string ItemInterfaceHash(SourceTextCache& sources,
                              const ultraviolet::ast::ASTItem& item) {
  std::vector<std::string> fields;
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::UsingDecl>) {
          fields.push_back("item=using");
          AppendAttributesSignature(fields, node.attrs_opt);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("source=" + SpanText(sources, node.span));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::ImportDecl>) {
          fields.push_back("item=import");
          AppendAttributesSignature(fields, node.attrs_opt);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("path=" + PathSignature(node.path));
          fields.push_back("alias=" + node.alias_opt.value_or(""));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::ExternBlock>) {
          fields.push_back("item=extern");
          AppendAttributesSignature(fields, node.attrs_opt);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("abi=" + ExternAbiSignature(node.abi_opt));
          fields.push_back("extern-items=" + std::to_string(node.items.size()));
          for (const auto& extern_item : node.items) {
            std::visit(
                [&](const auto& proc) {
                  std::vector<std::string> proc_fields;
                  AppendCallableSignature(sources,
                                          proc_fields,
                                          "extern-proc",
                                          proc.attrs,
                                          proc.vis,
                                          proc.name,
                                          proc.generic_params,
                                          proc.where_clause,
                                          proc.params,
                                          proc.return_type_opt,
                                          proc.contract);
                  AppendForeignContractsSignature(
                      sources, proc_fields, proc.foreign_contracts_opt);
                  fields.push_back("extern-item=" + HashFields(proc_fields));
                },
                extern_item);
          }
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::StaticDecl>) {
          fields.push_back("item=static");
          AppendAttributesSignature(fields, node.attrs_opt);
          AppendAttributesSignature(fields, node.binding.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("mut=" + std::string(MutabilitySignature(node.mut)));
          fields.push_back("pattern=" + PatternSignature(sources, node.binding.pat));
          fields.push_back("type=" + TypeSignature(
                                         sources,
                                         ultraviolet::ast::BindingAnnotationTypeOpt(
                                             node.binding)));
          fields.push_back("op=" + node.binding.op.lexeme);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::ProcedureDecl>) {
          AppendCallableSignature(sources,
                                  fields,
                                  "procedure",
                                  node.attrs,
                                  node.vis,
                                  node.name,
                                  node.generic_params,
                                  node.predicate_clause_opt,
                                  node.params,
                                  node.return_type_opt,
                                  node.contract);
        } else if constexpr (std::is_same_v<T,
                                            ultraviolet::ast::ComptimeProcedureDecl>) {
          AppendCallableSignature(sources,
                                  fields,
                                  "comptime-procedure",
                                  node.attrs,
                                  node.vis,
                                  node.name,
                                  node.generic_params,
                                  std::nullopt,
                                  node.params,
                                  node.return_type_opt,
                                  node.contract);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::RecordDecl>) {
          fields.push_back("item=record");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("name=" + node.name);
          AppendGenericSignature(sources, fields, node.generic_params);
          AppendPredicateSignature(sources, fields, node.predicate_clause_opt);
          fields.push_back("implements=" + std::to_string(node.implements.size()));
          for (const auto& impl : node.implements) {
            fields.push_back("implements-path=" + PathSignature(impl));
          }
          fields.push_back("members=" + std::to_string(node.members.size()));
          for (const auto& member : node.members) {
            std::vector<std::string> member_fields;
            std::visit(
                [&](const auto& member_node) {
                  using M = std::decay_t<decltype(member_node)>;
                  if constexpr (std::is_same_v<M, ultraviolet::ast::FieldDecl>) {
                    AppendFieldSignature(
                        sources, member_fields, member_node, "record-field");
                  } else if constexpr (std::is_same_v<M,
                                                      ultraviolet::ast::MethodDecl>) {
                    AppendMethodSignature(
                        sources, member_fields, member_node, "record-method");
                  } else {
                    member_fields.push_back("member=associated-type");
                    AppendAttributesSignature(member_fields, member_node.attrs);
                    member_fields.push_back(
                        "vis=" + std::string(VisibilitySignature(member_node.vis)));
                    member_fields.push_back("name=" + member_node.name);
                    member_fields.push_back(
                        "default=" +
                        TypeSignature(sources, member_node.default_type));
                  }
                },
                member);
            fields.push_back("member=" + HashFields(member_fields));
          }
          AppendInvariantSignature(sources, fields, node.invariant_opt);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::EnumDecl>) {
          fields.push_back("item=enum");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("name=" + node.name);
          AppendGenericSignature(sources, fields, node.generic_params);
          AppendPredicateSignature(sources, fields, node.predicate_clause_opt);
          fields.push_back("implements=" + std::to_string(node.implements.size()));
          for (const auto& impl : node.implements) {
            fields.push_back("implements-path=" + PathSignature(impl));
          }
          fields.push_back("variants=" + std::to_string(node.variants.size()));
          for (const auto& variant : node.variants) {
            std::string out;
            AppendSignatureAtom(out, variant.name);
            AppendSignatureAtom(out,
                                VariantPayloadSignature(sources,
                                                        variant.payload_opt));
            AppendSignatureBool(out, variant.discriminant_opt.has_value());
            if (variant.discriminant_opt.has_value()) {
              AppendSignatureAtom(out, variant.discriminant_opt->lexeme);
            }
            fields.push_back("variant=" + out);
          }
          AppendInvariantSignature(sources, fields, node.invariant_opt);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::ModalDecl>) {
          fields.push_back("item=modal");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("name=" + node.name);
          AppendGenericSignature(sources, fields, node.generic_params);
          AppendPredicateSignature(sources, fields, node.predicate_clause_opt);
          fields.push_back("implements=" + std::to_string(node.implements.size()));
          for (const auto& impl : node.implements) {
            fields.push_back("implements-path=" + PathSignature(impl));
          }
          fields.push_back("states=" + std::to_string(node.states.size()));
          for (const auto& state : node.states) {
            std::vector<std::string> state_fields;
            state_fields.push_back("state=" + state.name);
            state_fields.push_back("members=" + std::to_string(state.members.size()));
            for (const auto& member : state.members) {
              std::vector<std::string> member_fields;
              std::visit(
                  [&](const auto& member_node) {
                    using M = std::decay_t<decltype(member_node)>;
                    if constexpr (std::is_same_v<M,
                                                  ultraviolet::ast::StateFieldDecl>) {
                      member_fields.push_back("member=state-field");
                      AppendAttributesSignature(member_fields, member_node.attrs);
                      member_fields.push_back(
                          "vis=" +
                          std::string(VisibilitySignature(member_node.vis)));
                      member_fields.push_back(
                          std::string("key-boundary=") +
                          (member_node.key_boundary ? "1" : "0"));
                      member_fields.push_back("name=" + member_node.name);
                      member_fields.push_back(
                          "type=" + TypeSignature(sources, member_node.type));
                    } else if constexpr (std::is_same_v<
                                             M, ultraviolet::ast::StateMethodDecl>) {
                      member_fields.push_back("member=state-method");
                      AppendAttributesSignature(member_fields, member_node.attrs);
                      member_fields.push_back(
                          "vis=" +
                          std::string(VisibilitySignature(member_node.vis)));
                      member_fields.push_back("name=" + member_node.name);
                      AppendGenericSignature(
                          sources, member_fields, member_node.generic_params);
                      member_fields.push_back(
                          "receiver=" +
                          ReceiverSignature(sources, member_node.receiver));
                      AppendParamsSignature(
                          sources, member_fields, member_node.params);
                      member_fields.push_back(
                          "return=" +
                          TypeSignature(sources, member_node.return_type_opt));
                      AppendContractSignature(
                          sources, member_fields, member_node.contract);
                    } else {
                      member_fields.push_back("member=transition");
                      AppendAttributesSignature(member_fields, member_node.attrs);
                      member_fields.push_back(
                          "vis=" +
                          std::string(VisibilitySignature(member_node.vis)));
                      member_fields.push_back("name=" + member_node.name);
                      AppendParamsSignature(
                          sources, member_fields, member_node.params);
                      member_fields.push_back("target=" + member_node.target_state);
                    }
                  },
                  member);
              state_fields.push_back("member=" + HashFields(member_fields));
            }
            fields.push_back("state=" + HashFields(state_fields));
          }
          AppendInvariantSignature(sources, fields, node.invariant_opt);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::ClassDecl>) {
          fields.push_back("item=class");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back(std::string("modal=") + (node.modal ? "1" : "0"));
          fields.push_back("name=" + node.name);
          AppendGenericSignature(sources, fields, node.generic_params);
          AppendPredicateSignature(sources, fields, node.predicate_clause_opt);
          fields.push_back("supers=" + std::to_string(node.supers.size()));
          for (const auto& super : node.supers) {
            fields.push_back("super=" + PathSignature(super));
          }
          fields.push_back("items=" + std::to_string(node.items.size()));
          for (const auto& class_item : node.items) {
            fields.push_back("class-item=" +
                             ClassItemSignature(sources, class_item));
          }
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::TypeAliasDecl>) {
          fields.push_back("item=type-alias");
          AppendAttributesSignature(fields, node.attrs);
          fields.push_back("vis=" + std::string(VisibilitySignature(node.vis)));
          fields.push_back("name=" + node.name);
          AppendGenericSignature(sources, fields, node.generic_params);
          AppendPredicateSignature(sources, fields, node.predicate_clause_opt);
          fields.push_back("type=" + TypeSignature(sources, node.type));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::DeriveTargetDecl>) {
          fields.push_back("item=derive-target");
          fields.push_back("name=" + node.name);
          fields.push_back("source=" + SpanText(sources, node.span));
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::ErrorItem>) {
          fields.push_back("item=error");
        }
      },
      item);
  return HashFields(fields);
}

bool InterfaceVisible(const ultraviolet::ast::ASTItem& item) {
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::ErrorItem>) {
          return false;
        } else if constexpr (std::is_same_v<T,
                                            ultraviolet::ast::DeriveTargetDecl>) {
          return true;
        } else {
          return node.vis != ultraviolet::ast::Visibility::Private;
        }
      },
      item);
}

std::string ComputeModuleInterfaceHash(
    const ultraviolet::ast::ASTModule& module,
    SourceTextCache& sources) {
  std::vector<std::string> fields;
  fields.reserve(module.items.size() + 4);
  fields.push_back("interface-v1");
  fields.push_back(ultraviolet::core::StringOfPath(module.path));
  for (const auto& item : module.items) {
    if (!InterfaceVisible(item)) {
      continue;
    }
    fields.push_back("item=" + ItemInterfaceHash(sources, item));
  }
  return HashFields(fields);
}

ultraviolet::source::ModuleNames ModuleNamesForAssemblies(
    const std::vector<ultraviolet::project::Assembly>& assemblies) {
  ultraviolet::source::ModuleNames names;
  for (const auto& assembly : assemblies) {
    for (const auto& module : assembly.modules) {
      names.insert(module.path);
    }
  }
  return names;
}

std::unordered_map<std::string, std::string> ModuleOwnerMapForAssemblies(
    const std::vector<ultraviolet::project::Assembly>& assemblies) {
  std::unordered_map<std::string, std::string> owners;
  for (const auto& assembly : assemblies) {
    for (const auto& module : assembly.modules) {
      owners.emplace(module.path, assembly.name);
    }
  }
  return owners;
}

std::optional<std::string> ResolveImportedAssemblyName(
    const ultraviolet::ast::ImportDecl& import,
    const ultraviolet::ast::ModulePath& current_module,
    const ultraviolet::source::ModuleNames& module_names,
    const std::unordered_map<std::string, std::string>& module_owner) {
  const auto resolved =
      ultraviolet::source::ResolveImportModulePath(current_module,
                                              module_names,
                                              import.path);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  const auto owner_it = module_owner.find(ultraviolet::core::StringOfPath(*resolved));
  if (owner_it == module_owner.end()) {
    return std::nullopt;
  }
  return owner_it->second;
}

std::optional<std::string> ResolveModuleAssemblyName(
    const ultraviolet::ast::ModulePath& module_path,
    const std::unordered_map<std::string, std::string>& module_owner) {
  const auto owner_it =
      module_owner.find(ultraviolet::core::StringOfPath(module_path));
  if (owner_it == module_owner.end()) {
    return std::nullopt;
  }
  return owner_it->second;
}

std::optional<ultraviolet::ast::ModulePath> ResolveUsingModulePath(
    const ultraviolet::ast::UsingClause& clause,
    const ultraviolet::ast::ModulePath& current_module,
    const ultraviolet::source::ModuleNames& module_names) {
  return std::visit(
      [&](const auto& node) -> std::optional<ultraviolet::ast::ModulePath> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ultraviolet::ast::UsingItem>) {
          return ultraviolet::source::ResolveImportModulePath(current_module,
                                                          module_names,
                                                          node.module_path);
        } else if constexpr (std::is_same_v<T, ultraviolet::ast::UsingWildcard>) {
          return ultraviolet::source::ResolveImportModulePath(current_module,
                                                          module_names,
                                                          node.module_path);
        } else {
          return ultraviolet::source::ResolveImportModulePath(current_module,
                                                          module_names,
                                                          node.module_path);
        }
      },
      clause);
}

std::optional<std::string> ResolveUsingAssemblyName(
    const ultraviolet::ast::UsingDecl& using_decl,
    const ultraviolet::ast::ModulePath& current_module,
    const ultraviolet::source::ModuleNames& module_names,
    const std::unordered_map<std::string, std::string>& module_owner) {
  const auto resolved_module =
      ResolveUsingModulePath(using_decl.clause, current_module, module_names);
  if (!resolved_module.has_value()) {
    return std::nullopt;
  }
  return ResolveModuleAssemblyName(*resolved_module, module_owner);
}

std::filesystem::path RuntimeLogsDir(const ultraviolet::project::Project& project) {
  // Keep runtime logs inside the active output root to avoid stray build dirs.
  return project.outputs.logs_dir / "Runtime";
}

std::filesystem::path ConformanceLogsDir(
    const std::filesystem::path& output_root) {
  return ultraviolet::project::OutputPathsForRoot(output_root).logs_dir /
         "Conformance";
}

std::filesystem::path FallbackConformanceLogsDir(
    const std::filesystem::path& project_root) {
  return project_root / "Build" / "Logs" / "Conformance";
}

std::filesystem::path ResolveLogFileName(
    const std::optional<std::string>& requested_path,
    std::string_view fallback_name) {
  std::filesystem::path file_name;
  if (requested_path.has_value()) {
    file_name = std::filesystem::path(*requested_path).filename();
  }
  if (file_name.empty()) {
    file_name = std::filesystem::path(std::string(fallback_name));
  }
  return file_name;
}

std::string EffectiveRuntimeLogFilePath(
    const ultraviolet::project::Project& project,
    const ultraviolet::driver::CliOptions& opts) {
  if (!opts.log_to_file) {
    return {};
  }

  if (opts.log_file_path.has_value() && !opts.log_file_path->empty()) {
    return std::filesystem::path(*opts.log_file_path).generic_string();
  }

  const std::filesystem::path file_name =
      ResolveLogFileName(std::nullopt, project.assembly.name + ".runtime.log");

  return (RuntimeLogsDir(project) / file_name).generic_string();
}

std::string EffectiveConformancePath(
    const std::filesystem::path& conformance_logs_dir,
    const ultraviolet::driver::CliOptions& opts) {
  const std::filesystem::path file_name =
      ResolveLogFileName(opts.conformance_path, "compile.conformance.log");
  return (conformance_logs_dir / file_name).generic_string();
}

std::vector<ultraviolet::ast::ASTModule> FilterAstModulesForProject(
    const std::vector<ultraviolet::ast::ASTModule>& modules,
    const ultraviolet::project::Project& project) {
  std::unordered_set<std::string> wanted;
  wanted.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    wanted.insert(module.path);
  }

  std::vector<ultraviolet::ast::ASTModule> filtered;
  filtered.reserve(project.modules.size());
  for (const auto& module : modules) {
    const std::string module_path = ultraviolet::core::StringOfPath(module.path);
    if (wanted.find(module_path) == wanted.end()) {
      continue;
    }
    filtered.push_back(module);
  }
  return filtered;
}

std::vector<ultraviolet::ast::ASTModule> FilterAstModulesByModuleInfo(
    const std::vector<ultraviolet::ast::ASTModule>& modules,
    const std::vector<ultraviolet::project::ModuleInfo>& selected) {
  std::unordered_set<std::string> wanted;
  wanted.reserve(selected.size());
  for (const auto& module : selected) {
    wanted.insert(module.path);
  }

  std::vector<ultraviolet::ast::ASTModule> filtered;
  filtered.reserve(selected.size());
  for (const auto& module : modules) {
    const std::string module_path = ultraviolet::core::StringOfPath(module.path);
    if (wanted.find(module_path) == wanted.end()) {
      continue;
    }
    filtered.push_back(module);
  }
  return filtered;
}

void EmitExternalCode(ultraviolet::core::DiagnosticStream& diags,
                      std::string_view code,
                      const std::optional<std::string>& note = std::nullopt) {
  if (auto diag = ultraviolet::core::MakeExternalDiagnostic(code)) {
    if (note.has_value() && !note->empty()) {
      ultraviolet::core::SubDiagnostic child;
      child.kind = ultraviolet::core::SubDiagnosticKind::Note;
      child.message = *note;
      diag->children.push_back(std::move(child));
    }
    ultraviolet::core::Emit(diags, *diag);
    return;
  }
  ultraviolet::core::EmitExternalDiagnostic(diags, code);
}

void EnsureCompilerLogDirectory(const std::filesystem::path& logs_root) {
  std::error_code ec;
  std::filesystem::create_directories(logs_root, ec);
}

void EnsureRuntimeLogDirectory(const ultraviolet::project::Project& project,
                               const ultraviolet::driver::CliOptions& opts) {
  if (!opts.log_to_file) {
    return;
  }
  std::error_code ec;
  std::filesystem::path logs_root = RuntimeLogsDir(project);
  if (opts.log_file_path.has_value() && !opts.log_file_path->empty()) {
    logs_root = std::filesystem::path(*opts.log_file_path).parent_path();
  }
  if (!logs_root.empty()) {
    std::filesystem::create_directories(logs_root, ec);
  }
}

std::optional<ultraviolet::project::TargetProfile> ResolveSelectedTargetProfile(
    const ultraviolet::driver::CliOptions& opts,
    const ultraviolet::project::Project& project,
    ultraviolet::core::DiagnosticStream& diags) {
  if (opts.target_profile_override.has_value()) {
    return *opts.target_profile_override;
  }
  if (project.toolchain.target_profile.has_value()) {
    return *project.toolchain.target_profile;
  }
  ultraviolet::core::EmitExternalDiagnostic(diags, "E-PRJ-0112");
  return std::nullopt;
}

std::string EffectiveEmitIR(const ultraviolet::project::Project& project) {
  if (project.assembly.emit_ir.has_value()) {
    return *project.assembly.emit_ir;
  }
  return ultraviolet::project::IsExecutable(project) ? "none" : "ll";
}

ultraviolet::codegen::OptLevel EffectiveOptLevel(
    const ultraviolet::driver::CliOptions& opts) {
  if (!opts.opt_level.has_value()) {
    return ultraviolet::codegen::OptLevel::O0;
  }
  if (*opts.opt_level == "O1") return ultraviolet::codegen::OptLevel::O1;
  if (*opts.opt_level == "O2") return ultraviolet::codegen::OptLevel::O2;
  if (*opts.opt_level == "O3") return ultraviolet::codegen::OptLevel::O3;
  if (*opts.opt_level == "Os") return ultraviolet::codegen::OptLevel::Os;
  if (*opts.opt_level == "Oz") return ultraviolet::codegen::OptLevel::Oz;
  return ultraviolet::codegen::OptLevel::O0;
}

struct IncrementalNoopCheckResult {
  bool reusable = false;
  std::size_t modules = 0;
  std::string reason;
};

struct FullProjectNoopCheckResult {
  bool reusable = false;
  std::size_t assemblies = 0;
  std::size_t modules = 0;
  std::string reason;
};

std::unordered_map<std::string, ultraviolet::project::ModuleInfo>
BuildModuleInfoMapForAssemblies(
    const std::vector<ultraviolet::project::Assembly>& assemblies) {
  std::unordered_map<std::string, ultraviolet::project::ModuleInfo> modules;
  for (const auto& assembly : assemblies) {
    for (const auto& module : assembly.modules) {
      modules.emplace(module.path, module);
    }
  }
  return modules;
}

FullProjectNoopCheckResult CheckWholeProjectNoopReuse(
    const ultraviolet::project::Project& project,
    ultraviolet::project::TargetProfile target_profile,
    const ultraviolet::driver::CliOptions& opts,
    ultraviolet::core::DiagnosticStream& diags) {
  FullProjectNoopCheckResult result;

  if (!IncrementalEnabled()) {
    result.reason = "disabled";
    return result;
  }
  if (opts.phase1_only) {
    result.reason = "phase1-only";
    return result;
  }
  if (opts.check_only) {
    result.reason = "check-only";
    return result;
  }
  if (opts.emit_ir) {
    result.reason = "emit-ir-cli";
    return result;
  }
  if (opts.no_output) {
    result.reason = "no-output";
    return result;
  }

  const auto module_info_by_path =
      BuildModuleInfoMapForAssemblies(project.assemblies);

  for (const auto& assembly : project.assemblies) {
    const auto assembly_project = ultraviolet::project::AssemblyProject(project, assembly);
    // Link fingerprints depend on parsed extern specs and output link planning.
    if (ultraviolet::project::IsLinkable(assembly_project.assembly)) {
      result.reason = "linkable-requires-link-fingerprint:" + assembly.name;
      return result;
    }

    const auto manifest_path = IncrementalManifestPath(assembly_project);
    const auto manifest = LoadIncrementalManifest(manifest_path);
    if (!manifest.has_value()) {
      result.reason = "manifest-missing:" + assembly.name;
      return result;
    }

    const std::string build_key =
        BuildIncrementalBuildKey(
            assembly_project,
            target_profile,
            opts,
            g_compiler_executable_path,
            EffectiveRuntimeLogFilePath(assembly_project, opts));
    const std::string emit_ir = EffectiveEmitIR(assembly_project);
    const bool compatible =
        manifest->format == "1" &&
        manifest->assembly == assembly_project.assembly.name &&
        manifest->kind == assembly_project.assembly.kind &&
        manifest->emit_ir == emit_ir &&
        manifest->build_key == build_key;
    if (!compatible) {
      result.reason = "manifest-incompatible:" + assembly.name;
      return result;
    }

    for (const auto& [module_path, module_state] : manifest->modules) {
      const auto module_it = module_info_by_path.find(module_path);
      if (module_it == module_info_by_path.end()) {
        result.reason = "module-missing:" + module_path;
        return result;
      }
      const auto current_hash =
          ComputeModuleSourceHash(module_it->second, diags);
      if (!current_hash.has_value()) {
        result.reason = "source-hash-failed:" + module_path;
        return result;
      }
      if (*current_hash != module_state.info.source_hash) {
        result.reason = "source-changed:" + module_path;
        return result;
      }
      result.modules += 1;
    }

    result.assemblies += 1;
  }

  result.reusable = true;
  result.reason = "full-project-unchanged";
  return result;
}

IncrementalNoopCheckResult CheckIncrementalNoopReuse(
    const ultraviolet::project::Project& project,
    ultraviolet::project::TargetProfile target_profile,
    const std::vector<ultraviolet::ast::ASTModule>& parsed_modules,
    const ultraviolet::driver::CliOptions& opts,
    ultraviolet::core::DiagnosticStream& diags) {
  IncrementalNoopCheckResult result;

  if (!IncrementalEnabled()) {
    result.reason = "disabled";
    return result;
  }
  if (!ultraviolet::project::IsExecutable(project)) {
    result.reason = "non-executable";
    return result;
  }
  if (project.modules.empty()) {
    result.reason = "empty-module-set";
    return result;
  }
  const auto manifest_path = IncrementalManifestPath(project);
  const auto manifest = LoadIncrementalManifest(manifest_path);
  if (!manifest.has_value()) {
    result.reason = "manifest-missing";
    return result;
  }

  const std::string build_key =
      BuildIncrementalBuildKey(
          project,
          target_profile,
          opts,
          g_compiler_executable_path,
          EffectiveRuntimeLogFilePath(project, opts));
  const std::string emit_ir = EffectiveEmitIR(project);
  const bool compatible = manifest->format == "1" &&
                          manifest->assembly == project.assembly.name &&
                          manifest->kind == project.assembly.kind &&
                          manifest->emit_ir == emit_ir &&
                          manifest->build_key == build_key;
  if (!compatible) {
    result.reason = "manifest-incompatible";
    return result;
  }

  if (manifest->modules.size() != project.modules.size()) {
    result.reason = "module-count-mismatch";
    return result;
  }

  std::error_code exe_ec;
  const auto exe_path = ultraviolet::project::ExePath(project, target_profile);
  if (!std::filesystem::exists(exe_path, exe_ec) || exe_ec) {
    result.reason = "exe-missing";
    return result;
  }
  if (const auto map_path = ultraviolet::project::MapPath(project, target_profile);
      map_path.has_value()) {
    std::error_code map_ec;
    if (!std::filesystem::exists(*map_path, map_ec) || map_ec) {
      result.reason = "map-missing";
      return result;
    }
  }

  SourceTextCache source_text_cache(diags);
  const auto module_interface_hash =
      [&source_text_cache](const ultraviolet::ast::ASTModule& module) {
        return ComputeModuleInterfaceHash(module, source_text_cache);
      };
  auto current_incremental =
      BuildIncrementalBuildData(project,
                                parsed_modules,
                                build_key,
                                module_interface_hash,
                                diags);
  if (!current_incremental.ok) {
    result.reason = "fingerprint-failed";
    return result;
  }
  if (current_incremental.modules.size() != project.modules.size()) {
    result.reason = "fingerprint-module-count-mismatch";
    return result;
  }

  for (const auto& [module_path, info] : current_incremental.modules) {
    for (const auto& dep : info.dependencies) {
      if (IsExternalDependencyMarker(dep)) {
        continue;
      }
      if (current_incremental.modules.find(dep) ==
          current_incremental.modules.end()) {
        result.reason = "external-dependency:" + dep;
        return result;
      }
    }
  }

  for (const auto& module : project.modules) {
    const auto curr_it = current_incremental.modules.find(module.path);
    if (curr_it == current_incremental.modules.end()) {
      result.reason = "fingerprint-module-missing";
      return result;
    }
    const auto prev_it = manifest->modules.find(module.path);
    if (prev_it == manifest->modules.end()) {
      result.reason = "manifest-module-missing";
      return result;
    }
    if (prev_it->second.info.full_hash != curr_it->second.full_hash) {
      result.reason = "module-changed:" + module.path;
      return result;
    }
  }

  if (manifest->link_fingerprint.empty()) {
    result.reason = "link-fingerprint-missing";
    return result;
  }

  const auto extern_libraries =
      ultraviolet::analysis::CollectExternLibrarySpecs(parsed_modules);
  const auto link_inputs =
      ultraviolet::project::ResolveExternLibraryInputs(extern_libraries,
                                                   target_profile);
  const auto runtime_lib =
      ultraviolet::project::ResolveRuntimeLib(project, target_profile);
  ultraviolet::project::LinkPlan link_plan;
  link_plan.target_profile = target_profile;
  if (const auto link_mode = ultraviolet::project::LinkMode(project);
      link_mode.has_value()) {
    link_plan.output_kind = *link_mode;
  }
  const std::string link_fingerprint =
      ComputeLinkFingerprint(project, target_profile, build_key,
                             manifest->modules, link_inputs,
                             runtime_lib, link_plan, emit_ir);
  if (link_fingerprint != manifest->link_fingerprint) {
    result.reason = "link-fingerprint-mismatch";
    return result;
  }

  result.reusable = true;
  result.modules = project.modules.size();
  result.reason = "hit";
  return result;
}

}  // namespace

// ============================================================================
// Main Entry Point
// ============================================================================

int ultraviolet::driver::RunCompiler(int argc, char** argv) {
  using namespace ultraviolet;
  using namespace ultraviolet::driver;

  g_compiler_executable_path =
      ResolveCurrentExecutablePath((argc > 0) ? argv[0] : nullptr);

  {
    core::CrashRuntimeOptions crash_options;
    crash_options.tool_name = "Ultraviolet";
    crash_options.tool_version = GetVersionString();
    crash_options.executable_path = g_compiler_executable_path;
    crash_options.arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1)
                                             : 0u);
    for (int i = 1; i < argc; ++i) {
      crash_options.arguments.push_back(argv[i]);
    }
    std::error_code ec;
    crash_options.working_directory =
        std::filesystem::current_path(ec).generic_string();
    core::ConfigureCrashRuntime(crash_options);
    core::InstallCrashHandlers();
  }

  {
    std::string bundle_error;
    if (!core::EnsureBundledHostCompilerSupport(&bundle_error)) {
      if (bundle_error.empty()) {
        bundle_error = "Failed to initialize compiler sidecar support.";
      }
      std::cerr << "error: " << bundle_error << "\n";
      return 1;
    }
  }

  SpecDefsDriver();

  const auto parse_result = ParseArgs(argc, argv);
  const auto command_name = ResolveCommandName((argc > 0) ? argv[0] : nullptr);
  if (!parse_result.options.has_value()) {
    if (!parse_result.error_message.empty()) {
      std::cerr << "error: " << parse_result.error_message << "\n";
    }
    PrintUsage(command_name);
    return 2;
  }
  const auto& opts = parse_result.options;
  const auto detected_language =
      project::DetectSourceLanguageForInput(opts->input_path);
  project::SetActiveLanguageProfile(
      detected_language.value_or(project::SourceLanguage::Ultraviolet));
  core::SetCrashEnabled(!opts->no_crash_report);
  core::SetCrashJsonStdout(opts->diag_json);
  core::MaybeTriggerCrashFixtureFromEnv();

  core::SetBuildProgressOverride(opts->build_progress);
  core::SetIncrementalOverride(opts->incremental);
  core::SetRuntimeLibOverride(opts->runtime_lib_path);
  core::SetLinkDebugOverride(opts->link_debug);
  core::SetMaxErrorsOverride(opts->max_errors_override);
  core::SetOutDirOverride(opts->out_dir);
  if (!opts->debug_subsystems.empty()) {
    core::SetDebugSubsystems(opts->debug_subsystems);
  }
  const auto effective_error_policy =
      core::MaxErrorsOverride().value_or(core::DefaultErrorRecoveryPolicy());

  // Map CLI Verbosity to core Verbosity and store in process config.
  {
    core::Verbosity core_verbosity = core::Verbosity::Normal;
    switch (opts->verbosity) {
      case Verbosity::Normal: core_verbosity = core::Verbosity::Normal; break;
      case Verbosity::Verbose: core_verbosity = core::Verbosity::Verbose; break;
    }
    core::SetVerbosity(core_verbosity);
  }

  // Compute color override early so init/clean subcommands can use it.
  const auto color_override = [&]() -> core::ColorOverride {
    switch (opts->color_mode) {
      case ColorMode::Always: return core::ColorOverride::ForceOn;
      case ColorMode::Never: return core::ColorOverride::ForceOff;
      case ColorMode::Auto: return core::ColorOverride::Auto;
    }
    return core::ColorOverride::Auto;
  }();

  if (opts->show_help) {
    PrintHelp(command_name);
    return 0;
  }
  if (opts->show_debug_help) {
    PrintDebugHelp();
    return 0;
  }
  if (opts->show_version) {
    PrintVersion();
    return 0;
  }

  // ========================================================================
  // test subcommand
  // ========================================================================
  if (opts->do_test) {
    core::DiagnosticStream test_diags;

    if (opts->test_target_rejected) {
      EmitUnknownTestTargetDiagnostic(test_diags, opts->test_target.value_or(""));
      RenderDriverDiagnostics(test_diags, *opts, color_override);
      return 1;
    }

    std::error_code current_dir_ec;
    const auto current_directory =
        std::filesystem::current_path(current_dir_ec);
    const std::filesystem::path test_root_input = [&]() {
      if (!opts->test_target.has_value()) {
        return current_dir_ec ? std::filesystem::path(".") : current_directory;
      }
      std::filesystem::path candidate(*opts->test_target);
      if (candidate.is_relative() && !current_dir_ec) {
        candidate = current_directory / candidate;
      }
      std::error_code exists_ec;
      if (std::filesystem::exists(candidate, exists_ec) && !exists_ec) {
        return candidate;
      }
      return current_dir_ec ? std::filesystem::path(".") : current_directory;
    }();

    const auto project_root = project::FindProjectRoot(test_root_input);
    const auto assembly_target =
        project::ParseAssemblyTarget(opts->assembly_target);
    if (!assembly_target.has_value()) {
      core::EmitExternalDiagnostic(test_diags, "E-PRJ-0205");
      RenderDriverDiagnostics(test_diags, *opts, color_override);
      return 1;
    }

    const auto project_result =
        project::LoadProjectAllAssemblies(project_root, *assembly_target);
    for (const auto& diag : project_result.diags) {
      core::Emit(test_diags, diag);
    }
    if (!project_result.project.has_value() || core::HasError(test_diags)) {
      RenderDriverDiagnostics(test_diags, *opts, color_override);
      return 1;
    }

    const auto target_resolution = ResolveSourceNativeTestTarget(
        *project_result.project,
        current_dir_ec ? std::filesystem::path(".") : current_directory,
        opts->test_target);
    if (!target_resolution.scope.has_value()) {
      EmitUnknownTestTargetDiagnostic(test_diags,
                                      target_resolution.unknown_target);
      RenderDriverDiagnostics(test_diags, *opts, color_override);
      return 1;
    }

    const auto selected_target_profile = ResolveSelectedTargetProfile(
        *opts, *project_result.project, test_diags);
    if (!selected_target_profile.has_value()) {
      RenderDriverDiagnostics(test_diags, *opts, color_override);
      return 1;
    }

    frontend::ParseModuleDeps deps;
    deps.compilation_unit = static_cast<project::CompilationUnitResult (*)(
        const std::filesystem::path&)>(project::CompilationUnit);
    deps.read_bytes = frontend::ReadBytesDefault;
    deps.load_source = core::LoadSource;
    deps.parse_file = ast::ParseFile;
    deps.inspect_source = [](const core::SourceFile& source) {
      return InspectSource(source);
    };

    std::vector<SourceNativeTestDescriptor> discovered_tests;
    for (const auto& assembly : project_result.project->assemblies) {
      auto parsed = frontend::ParseModulesWithDeps(
          assembly.modules, assembly.source_root, assembly.name, deps);
      for (const auto& diag : parsed.diags) {
        core::Emit(test_diags, diag);
      }
      if (!parsed.modules.has_value() || core::HasError(parsed.diags)) {
        continue;
      }
      if (!ValidateParsedTypeAttributeLists(*parsed.modules, test_diags)) {
        continue;
      }
      auto discovery =
          DiscoverSourceNativeTests(assembly.name, *parsed.modules);
      discovered_tests.insert(discovered_tests.end(),
                              discovery.tests.begin(),
                              discovery.tests.end());
    }

    if (core::HasError(test_diags)) {
      RenderDriverDiagnostics(test_diags, *opts, color_override);
      return 1;
    }

    SourceNativeTestFilter test_filter;
    test_filter.test_name = opts->test_name_filter;
    test_filter.coverage_reference = opts->test_coverage_filter;
    const auto selected_tests = FilterSourceNativeTests(
        SelectSourceNativeTests(
            *project_result.project, *target_resolution.scope, discovered_tests),
        test_filter);
    if (selected_tests.empty()) {
      return 0;
    }

    std::vector<std::string> assembly_order;
    std::unordered_map<std::string, std::vector<SourceNativeTestDescriptor>>
        tests_by_assembly;
    for (const auto& test : selected_tests) {
      auto [it, inserted] =
          tests_by_assembly.try_emplace(test.assembly_name);
      if (inserted) {
        assembly_order.push_back(test.assembly_name);
      }
      it->second.push_back(test);
    }

    int harness_status = 0;
    for (const auto& assembly_name : assembly_order) {
      const auto assembly_it = std::find_if(
          project_result.project->assemblies.begin(),
          project_result.project->assemblies.end(),
          [&](const project::Assembly& assembly) {
            return assembly.name == assembly_name;
          });
      if (assembly_it == project_result.project->assemblies.end()) {
        EmitInternalDiagnostic(
            test_diags, core::Severity::Error, std::nullopt,
            "selected source-native test assembly was not loaded");
        RenderDriverDiagnostics(test_diags, *opts, color_override);
        return 1;
      }
      const auto result = BuildAndRunSourceNativeTestHarness(
          *project_result.project, *assembly_it, tests_by_assembly[assembly_name],
          *selected_target_profile,
          *opts,
          current_dir_ec ? std::filesystem::path(".") : current_directory,
          test_diags);
      if (!result.has_value()) {
        RenderDriverDiagnostics(test_diags, *opts, color_override);
        return 1;
      }
      if (*result != 0 && harness_status == 0) {
        harness_status = *result;
      }
    }

    if (core::HasError(test_diags)) {
      RenderDriverDiagnostics(test_diags, *opts, color_override);
      return 1;
    }
    return harness_status == 0 ? 0 : 1;
  }

  // ========================================================================
  // init subcommand
  // ========================================================================
  if (opts->do_init) {
    namespace io = std::filesystem;
    const auto& language = project::ActiveLanguageProfile();
    const io::path project_dir = io::absolute(io::path(opts->input_path));
    const auto toml_path = project_dir / std::string(language.manifest_name);

    std::error_code ec;
    if (io::exists(toml_path, ec) && !ec) {
      std::cerr << "error: " << language.manifest_name << " already exists in "
                << project_dir.string() << "\n";
      return 1;
    }

    // Derive project name from directory name
    std::string project_label = project_dir.filename().string();
    if (project_label.empty() || project_label == "." || project_label == "..") {
      project_label = "my_project";
    }
    const std::string project_name = DeriveAssemblyName(project_dir);

    // Create project directory if it doesn't exist
    if (!io::exists(project_dir, ec)) {
      io::create_directories(project_dir, ec);
      if (ec) {
        std::cerr << "error: could not create directory: "
                  << project_dir.string() << "\n";
        return 1;
      }
    }

    // Create manifest
    {
      std::ofstream out(toml_path, std::ios::binary);
      if (!out) {
        std::cerr << "error: could not create " << language.manifest_name
                  << "\n";
        return 1;
      }
      out << "[assembly]\n"
          << "name = \"" << project_name << "\"\n"
          << "kind = \"executable\"\n"
          << "root = \"src\"\n";
    }

    // Create src/ directory
    const auto src_dir = project_dir / "src";
    io::create_directories(src_dir, ec);
    if (ec) {
      std::cerr << "error: could not create src directory\n";
      return 1;
    }

    // Create source entry file
    {
      const std::filesystem::path main_path =
          src_dir / ("main" + std::string(language.source_extension));
      std::ofstream out(main_path, std::ios::binary);
      if (!out) {
        std::cerr << "error: could not create "
                  << main_path.lexically_relative(project_dir).generic_string()
                  << "\n";
        return 1;
      }
      out << "public procedure main(move ctx: Context) -> i32 {\n"
          << "    return 0\n"
          << "}\n";
    }

    const bool init_color = core::IsColorEnabledWithOverride(stderr, color_override);
    constexpr std::size_t kLabelWidth = 12;
    const std::string_view label = "Created";
    const std::size_t pad = kLabelWidth - label.size();
    std::cerr << std::string(pad, ' ')
              << core::Colorize(label, core::Color::BoldGreen, init_color)
              << "  " << project_label;
    if (project_name != project_label) {
      std::cerr << " (" << language.lower_name << " project, assembly "
                << project_name << ")\n";
    } else {
      std::cerr << " (" << language.lower_name << " project)\n";
    }
    return 0;
  }

  // ========================================================================
  // clean subcommand
  // ========================================================================
  if (opts->do_clean) {
    namespace io = std::filesystem;
    const io::path input_path_fs = opts->input_path;
    const auto project_root = project::FindProjectRoot(input_path_fs);
    const auto assembly_target =
        project::ParseAssemblyTarget(opts->assembly_target);
    if (!assembly_target.has_value()) {
      core::DiagnosticStream target_diags;
      core::EmitExternalDiagnostic(target_diags, "E-PRJ-0205");
      for (const auto& diag : target_diags) {
        std::cerr << core::Render(diag) << "\n";
      }
      return 1;
    }
    const auto project_result =
        project::LoadProject(project_root, *assembly_target);

    if (!project_result.project.has_value()) {
      for (const auto& diag : project_result.diags) {
        std::cerr << core::Render(diag) << "\n";
      }
      return 1;
    }

    const auto& proj = *project_result.project;
    const auto& out_root = proj.outputs.root;
    const std::string project_name = proj.assembly.name;
    const bool clean_color = core::IsColorEnabledWithOverride(stderr, color_override);
    constexpr std::size_t kLabelWidth = 12;
    const std::string_view label = "Cleaned";
    const std::size_t pad = kLabelWidth - label.size();

    std::error_code ec;
    if (io::exists(out_root, ec) && !ec) {
      io::remove_all(out_root, ec);
      if (ec) {
        std::cerr << "error: could not remove " << out_root.string() << ": "
                  << ec.message() << "\n";
        return 1;
      }
      std::cerr << std::string(pad, ' ')
                << core::Colorize(label, core::Color::BoldGreen, clean_color)
                << "  " << project_name << " (removed "
                << out_root.string() << ")\n";
    } else {
      std::cerr << std::string(pad, ' ')
                << core::Colorize(label, core::Color::BoldGreen, clean_color)
                << "  " << project_name << " (nothing to clean)\n";
    }
    return 0;
  }

  const std::filesystem::path input_path = opts->input_path;
  const auto project_root = project::FindProjectRoot(input_path);

  core::DiagnosticStream diags;
  const bool is_verbose = opts->verbosity == Verbosity::Verbose;
  const bool show_build_progress =
      is_verbose ? true : BuildProgressEnabled();
  const bool use_color = core::IsColorEnabledWithOverride(stderr, color_override);
  const auto build_start = std::chrono::steady_clock::now();

  // Human-friendly progress: right-aligned colored label + detail.
  const auto progress = [&](const char* label, const std::string& detail,
                            core::Color color = core::Color::BoldGreen) {
    if (!show_build_progress) return;
    constexpr std::size_t kLabelWidth = 12;
    const std::size_t label_len = std::strlen(label);
    const std::size_t pad =
        (kLabelWidth > label_len) ? (kLabelWidth - label_len) : 0;
    std::cerr << std::string(pad, ' ')
              << core::Colorize(label, color, use_color) << "  " << detail
              << "\n";
    std::cerr.flush();
  };

  // Machine-format logging preserved under --debug pipeline.
  const bool debug_pipeline = core::IsDebugEnabled("pipeline");
  core::BuildLogResolveOptions phase_log_options;
  phase_log_options.channel_enabled = show_build_progress;
  phase_log_options.debug_enabled = debug_pipeline;
  phase_log_options.default_enabled = true;
  const core::BuildLogMode phase_log_mode =
      core::ResolveBuildLogMode(phase_log_options);
  const auto log_machine = [&](const std::string& message) {
    if (debug_pipeline) {
      std::cerr << "[trace][build] pid=" << CurrentProcessId() << " "
                << message << "\n";
      std::cerr.flush();
      return;
    }
    if (phase_log_mode == core::BuildLogMode::None) return;
    if (phase_log_mode == core::BuildLogMode::Summary &&
        !core::ShouldEmitSummaryBuildLog(core::BuildLogChannel::Phase,
                                         message)) {
      return;
    }
    std::cerr << "[info][build] " << message << "\n";
    std::cerr.flush();
  };

  log_machine("event=start input=" + opts->input_path +
              (opts->assembly_target.has_value()
                   ? " assembly=" + *opts->assembly_target
                   : ""));

  bool phase1_ok = false;
  bool phase2_ok = false;
  bool resolve_ok = false;
  bool typecheck_ok = false;
  bool phase4_ok = false;
  bool incremental_noop_reused = false;

  // Per-phase timing (for --verbose)
  long long parse_ms = 0;
  long long comptime_ms = 0;
  long long check_ms = 0;
  long long codegen_ms = 0;
  long long link_ms = 0;

  log_machine("phase=project-load");
  const auto assembly_target =
      project::ParseAssemblyTarget(opts->assembly_target);
  if (!assembly_target.has_value()) {
    core::EmitExternalDiagnostic(diags, "E-PRJ-0205");
    for (const auto& diag : diags) {
      std::cerr << core::Render(diag) << "\n";
    }
    return 1;
  }
  auto project_result =
      project::LoadProject(project_root, *assembly_target);
  if (project_result.project.has_value()) {
    core::UpdateCrashReportRoot(
        core::DefaultCrashReportRoot(project_result.project->outputs.root));
  }

  if (opts->conformance_path.has_value()) {
    std::filesystem::path conformance_logs_dir =
        FallbackConformanceLogsDir(project_root);
    if (project_result.project.has_value()) {
      conformance_logs_dir =
          ConformanceLogsDir(project_result.project->outputs.root);
    }
    EnsureCompilerLogDirectory(conformance_logs_dir);
    core::Conformance::Init(
        EffectiveConformancePath(conformance_logs_dir, *opts), "compile");
    core::Conformance::SetRoot(project_root.string());
    core::Conformance::SetPhase("project-load");
  }

  for (const auto& diag : project_result.diags) {
    core::Emit(diags, diag);
  }

  if (!core::HasError(diags) && project_result.project.has_value()) {
    ApplyTestHarnessBuildOptions(*project_result.project, *opts, diags);
  }

  std::optional<project::TargetProfile> selected_target_profile;
  if (!core::HasError(diags) && project_result.project.has_value()) {
    selected_target_profile = ResolveSelectedTargetProfile(
        *opts, *project_result.project, diags);
  }

  if (!core::HasError(diags) && project_result.project.has_value() &&
      selected_target_profile.has_value()) {
    const auto& proj = *project_result.project;
    const auto target_profile = *selected_target_profile;
    const auto opt_level = EffectiveOptLevel(*opts);
    if (!opts->phase1_only && !opts->check_only && !opts->emit_ir &&
        !opts->no_output) {
      const auto global_noop =
          CheckWholeProjectNoopReuse(proj, target_profile, *opts, diags);
      log_machine("phase=incremental-global reusable=" +
                  std::string(global_noop.reusable ? "true" : "false") +
                  " reason=" + global_noop.reason +
                  " assemblies=" + std::to_string(global_noop.assemblies) +
                  " modules=" + std::to_string(global_noop.modules));
      if (is_verbose) {
        if (global_noop.reusable) {
          std::cerr << "  incremental: full-project cache hit (no changes)\n";
        } else {
          std::cerr << "  incremental: full-project rebuild required ("
                    << global_noop.reason << ")\n";
        }
        std::cerr.flush();
      }
      if (global_noop.reusable) {
        phase1_ok = true;
        phase2_ok = true;
        resolve_ok = true;
        typecheck_ok = true;
        phase4_ok = true;
        incremental_noop_reused = true;
      }
    }

    if (!incremental_noop_reused) {
      progress("Loading",
               project_root.filename().string() + " (" +
                   std::to_string(proj.modules.size()) + " modules)");
      if (is_verbose) {
        for (const auto& module : proj.modules) {
          std::cerr << "       module: " << module.path << "\n";
        }
        std::cerr.flush();
      }
      EnsureRuntimeLogDirectory(proj, *opts);

    frontend::ParseModuleDeps deps;
    deps.compilation_unit = static_cast<project::CompilationUnitResult (*)(
        const std::filesystem::path&)>(project::CompilationUnit);
    deps.read_bytes = frontend::ReadBytesDefault;
    deps.load_source = core::LoadSource;
    deps.parse_file = ast::ParseFile;
    deps.inspect_source = [](const core::SourceFile& source) {
      return InspectSource(source);
    };

    log_machine("phase=parse-modules");
    core::Conformance::SetPhase("parse");
    const auto parse_start = std::chrono::steady_clock::now();
    std::unordered_map<std::string, const project::Assembly*> assembly_by_name;
    assembly_by_name.reserve(proj.assemblies.size());
    for (const auto& assembly : proj.assemblies) {
      assembly_by_name.emplace(assembly.name, &assembly);
    }
    const auto all_module_names = ModuleNamesForAssemblies(proj.assemblies);
    const auto module_owner = ModuleOwnerMapForAssemblies(proj.assemblies);

    std::vector<std::string> pending_assemblies = {proj.assembly.name};
    std::unordered_set<std::string> seen_assemblies = {proj.assembly.name};
    std::vector<project::ModuleInfo> reachable_modules;
    std::vector<ast::ASTModule> parsed_modules;
    std::optional<std::vector<ast::ASTModule>> parsed_project_module_set;
    frontend::UnsafeSpanMap parsed_unsafe_spans_by_file;
    core::DiagnosticStream parse_phase_diags;
    core::DiagnosticStream comptime_phase_diags;
    bool parse_ok = true;
    bool comptime_ok = true;

    for (std::size_t i = 0; i < pending_assemblies.size() && parse_ok && comptime_ok;
         ++i) {
      const auto asm_it = assembly_by_name.find(pending_assemblies[i]);
      if (asm_it == assembly_by_name.end()) {
        continue;
      }
      const auto& assembly = *asm_it->second;
      progress("Parsing",
               assembly.name + " (" +
                   std::to_string(assembly.modules.size()) + " modules)");
      log_machine("phase=parse-modules assembly-start name=" + assembly.name +
                  " modules=" + std::to_string(assembly.modules.size()) +
                  " source_root=" + assembly.source_root.generic_string());

      auto parsed_chunk = frontend::ParseModulesWithDeps(
          assembly.modules, assembly.source_root, assembly.name, deps);
      for (const auto& diag : parsed_chunk.diags) {
        core::Emit(parse_phase_diags, diag);
      }
      const bool parse_chunk_has_errors = core::HasError(parsed_chunk.diags);
      if (!parsed_chunk.modules.has_value() || parse_chunk_has_errors) {
        log_machine("phase=parse-modules assembly-finish name=" + assembly.name +
                    " ok=false parsed_modules=" +
                    std::to_string(parsed_chunk.modules.has_value()
                                       ? parsed_chunk.modules->size()
                                       : 0) +
                    " emitted_diags=" +
                    std::to_string(parsed_chunk.diags.size()));
        parse_ok = false;
        break;
      }
      reachable_modules.insert(reachable_modules.end(), assembly.modules.begin(),
                               assembly.modules.end());

      std::vector<ast::ASTModule> stage_modules = std::move(*parsed_chunk.modules);
      for (auto& [path, spans] : parsed_chunk.unsafe_spans_by_file) {
        parsed_unsafe_spans_by_file.insert_or_assign(std::move(path),
                                                     std::move(spans));
      }
      if (!ValidateParsedTypeAttributeLists(stage_modules, parse_phase_diags)) {
        log_machine("phase=parse-modules assembly-finish name=" + assembly.name +
                    " ok=false attr-validation=true parsed_modules=" +
                    std::to_string(stage_modules.size()) +
                    " emitted_diags=" +
                    std::to_string(parsed_chunk.diags.size()));
        parse_ok = false;
        break;
      }
      log_machine("phase=parse-modules assembly-finish name=" + assembly.name +
                  " ok=true parsed_modules=" +
                  std::to_string(stage_modules.size()) +
                  " emitted_diags=" +
                  std::to_string(parsed_chunk.diags.size()));
      for (const auto& module : stage_modules) {
        for (const auto& item : module.items) {
          const auto* import = std::get_if<ast::ImportDecl>(&item);
          if (import) {
            const auto imported_assembly = ResolveImportedAssemblyName(
                *import, module.path, all_module_names, module_owner);
            if (!imported_assembly.has_value() ||
                assembly_by_name.find(*imported_assembly) ==
                    assembly_by_name.end()) {
              continue;
            }
            if (seen_assemblies.insert(*imported_assembly).second) {
              pending_assemblies.push_back(*imported_assembly);
            }
            continue;
          }
          const auto* using_decl = std::get_if<ast::UsingDecl>(&item);
          if (!using_decl) {
            continue;
          }
          const auto using_assembly = ResolveUsingAssemblyName(
              *using_decl, module.path, all_module_names, module_owner);
          if (!using_assembly.has_value() ||
              assembly_by_name.find(*using_assembly) == assembly_by_name.end()) {
            continue;
          }
          if (seen_assemblies.insert(*using_assembly).second) {
            pending_assemblies.push_back(*using_assembly);
          }
        }
      }

      for (auto& module : stage_modules) {
        parsed_modules.push_back(std::move(module));
      }
    }

    if (parse_ok && comptime_ok) {
      auto project_modules = parsed_modules;
      for (const auto& assembly : proj.assemblies) {
        if (seen_assemblies.find(assembly.name) != seen_assemblies.end()) {
          continue;
        }
        progress("Parsing",
                 assembly.name + " (" +
                     std::to_string(assembly.modules.size()) + " modules)");
        log_machine("phase=parse-modules assembly-start name=" + assembly.name +
                    " modules=" + std::to_string(assembly.modules.size()) +
                    " source_root=" + assembly.source_root.generic_string());

        auto parsed_chunk = frontend::ParseModulesWithDeps(
            assembly.modules, assembly.source_root, assembly.name, deps);
        for (const auto& diag : parsed_chunk.diags) {
          core::Emit(parse_phase_diags, diag);
        }
        const bool parse_chunk_has_errors = core::HasError(parsed_chunk.diags);
        if (!parsed_chunk.modules.has_value() || parse_chunk_has_errors) {
          log_machine("phase=parse-modules assembly-finish name=" +
                      assembly.name + " ok=false parsed_modules=" +
                      std::to_string(parsed_chunk.modules.has_value()
                                         ? parsed_chunk.modules->size()
                                         : 0) +
                      " emitted_diags=" +
                      std::to_string(parsed_chunk.diags.size()));
          parse_ok = false;
          break;
        }
        std::vector<ast::ASTModule> stage_modules = std::move(*parsed_chunk.modules);
        for (auto& [path, spans] : parsed_chunk.unsafe_spans_by_file) {
          parsed_unsafe_spans_by_file.insert_or_assign(std::move(path),
                                                       std::move(spans));
        }
        if (!ValidateParsedTypeAttributeLists(stage_modules, parse_phase_diags)) {
          log_machine("phase=parse-modules assembly-finish name=" +
                      assembly.name + " ok=false attr-validation=true parsed_modules=" +
                      std::to_string(stage_modules.size()) +
                      " emitted_diags=" +
                      std::to_string(parsed_chunk.diags.size()));
          parse_ok = false;
          break;
        }
        log_machine("phase=parse-modules assembly-finish name=" + assembly.name +
                    " ok=true parsed_modules=" +
                    std::to_string(stage_modules.size()) +
                    " emitted_diags=" +
                    std::to_string(parsed_chunk.diags.size()));
        for (auto& module : stage_modules) {
          project_modules.push_back(std::move(module));
        }
      }

      parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - parse_start).count();

      if (parse_ok && !opts->phase1_only) {
        core::Conformance::SetPhase("comptime");
        const auto comptime_start = std::chrono::steady_clock::now();
        log_machine("phase=comptime project-start modules=" +
                    std::to_string(project_modules.size()));
        analysis::ScopeContext comptime_signature_ctx;
        comptime_signature_ctx.project = &proj;
        comptime_signature_ctx.target_profile = target_profile;
        comptime_signature_ctx.sigma.mods = project_modules;
        comptime_signature_ctx.sigma.unsafe_spans_by_file =
            parsed_unsafe_spans_by_file;
        comptime_signature_ctx.scopes =
            {analysis::Scope{}, analysis::Scope{}, analysis::Scope{}};
        log_machine("phase=comptime signatures-start");
        const auto comptime_signature_diags =
            analysis::ValidateComptimeProcedureSignatures(
                comptime_signature_ctx, project_modules);
        log_machine("phase=comptime signatures-finish emitted_diags=" +
                    std::to_string(comptime_signature_diags.size()));
        for (const auto& diag : comptime_signature_diags) {
          core::Emit(comptime_phase_diags, diag);
        }
        const bool comptime_signature_has_errors =
            core::HasError(comptime_signature_diags);
        std::optional<frontend::ComptimeResult> expanded_project;
        if (!comptime_signature_has_errors) {
          log_machine("phase=comptime execute-start");
          expanded_project = frontend::ExecuteComptime(
              project_modules, BuildComptimeOptions(proj));
          log_machine("phase=comptime execute-finish emitted_diags=" +
                      std::to_string(expanded_project->diags.size()));
          for (const auto& diag : expanded_project->diags) {
            core::Emit(comptime_phase_diags, diag);
          }
        }
        const bool comptime_has_errors =
            comptime_signature_has_errors ||
            (expanded_project.has_value() &&
             core::HasError(expanded_project->diags));
        if (comptime_signature_has_errors || !expanded_project.has_value() ||
            !expanded_project->modules.has_value() || comptime_has_errors) {
          log_machine("phase=comptime project-finish ok=false expanded_modules=" +
                      std::to_string(expanded_project.has_value() &&
                                             expanded_project->modules.has_value()
                                         ? expanded_project->modules->size()
                                         : 0) +
                      " emitted_diags=" +
                      std::to_string(comptime_phase_diags.size()));
          comptime_ok = false;
        } else {
          project_modules = std::move(*expanded_project->modules);
          parsed_modules =
              FilterAstModulesByModuleInfo(project_modules, reachable_modules);
          log_machine("phase=comptime project-finish ok=true expanded_modules=" +
                      std::to_string(project_modules.size()) +
                      " emitted_diags=" +
                      std::to_string(comptime_phase_diags.size()));
        }
        comptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - comptime_start).count();
        core::Conformance::SetPhase("parse");
      }
      if (parse_ok && comptime_ok) {
        parsed_project_module_set = std::move(project_modules);
      }
    }

    if (parse_ms == 0) {
      parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - parse_start).count();
    }
    phase1_ok = parse_ok;
    phase2_ok = parse_ok && comptime_ok;
    const std::size_t parse_phase_error_count =
        CountErrorDiagnostics(diags) + CountErrorDiagnostics(parse_phase_diags);
    if (core::AbortOnErrorCount(effective_error_policy,
                                parse_phase_error_count)) {
      log_machine("phase=parse-modules abort-on-error-count errors=" +
                  std::to_string(parse_phase_error_count));
      phase1_ok = false;
      parse_ok = false;
    }
    std::optional<std::vector<ast::ASTModule>> parsed_module_set;
    project::Project sema_project = proj;
    if (phase1_ok) {
      for (const auto& diag : parse_phase_diags) {
        core::Emit(diags, diag);
      }
      for (const auto& diag : comptime_phase_diags) {
        core::Emit(diags, diag);
      }
      sema_project.modules = std::move(reachable_modules);
    } else {
      analysis::ResolveContext parse_fail_res_ctx;
      parse_fail_res_ctx.parse_ok = false;
      parse_fail_res_ctx.parse_diags = &parse_phase_diags;
      const auto parse_failed_resolution =
          analysis::ResolveModules(parse_fail_res_ctx);
      resolve_ok = parse_failed_resolution.ok;
      for (const auto& diag : parse_failed_resolution.diags) {
        core::Emit(diags, diag);
      }
    }

    if (phase2_ok) {
      parsed_module_set = std::move(parsed_modules);
    }

    if (parsed_module_set.has_value()) {
      std::size_t ffi_import_count = 0;
      std::size_t ffi_export_count = 0;
      for (const auto& module : *parsed_module_set) {
        const auto surface = analysis::CollectFfiSurface(module);
        ffi_import_count += surface.imports.size();
        ffi_export_count += surface.exports.size();
      }
      log_machine("phase=parse-modules step=ffi-surface imports=" +
                  std::to_string(ffi_import_count) +
                  " exports=" + std::to_string(ffi_export_count));
    }

    if (opts->dump_ast && parsed_module_set.has_value()) {
      ast::DumpOptions dump_opts;
      dump_opts.include_spans = true;
      for (const auto& module :
           FilterAstModulesForProject(*parsed_module_set, sema_project)) {
        std::cout << "module " << core::StringOfPath(module.path) << "\n";
        for (const auto& item : module.items) {
          std::cout << "  " << ast::to_string(item, dump_opts) << "\n";
        }
      }
    }

    if (!HasBlockingErrorsForSema(diags) && parsed_module_set.has_value() &&
        !opts->phase1_only && !opts->check_only &&
        !opts->emit_ir && !opts->no_output) {
      const auto noop_check =
          CheckIncrementalNoopReuse(sema_project, target_profile,
                                    *parsed_module_set, *opts, diags);
      log_machine("phase=incremental-fastpath reusable=" +
                  std::string(noop_check.reusable ? "true" : "false") +
                  " reason=" + noop_check.reason + " modules=" +
                  std::to_string(noop_check.modules));
      if (is_verbose) {
        if (noop_check.reusable) {
          std::cerr << "  incremental: cache hit (no changes)\n";
        } else {
          std::cerr << "  incremental: rebuild required (" << noop_check.reason << ")\n";
        }
        std::cerr.flush();
      }
      if (noop_check.reusable) {
        resolve_ok = true;
        typecheck_ok = true;
        phase4_ok = true;
        incremental_noop_reused = true;
      }
    }

    if (!HasBlockingErrorsForSema(diags) && parsed_module_set.has_value() &&
        !opts->phase1_only && !incremental_noop_reused) {
      const auto sema_start = std::chrono::steady_clock::now();
      progress("Checking", proj.assembly.name);
      log_machine("phase=sema");
      core::Conformance::SetPhase("resolve");

      analysis::ScopeContext ctx;
      ctx.project = &sema_project;
      ctx.target_profile = target_profile;
      ctx.sigma.mods = *parsed_module_set;
      ctx.sigma.unsafe_spans_by_file = parsed_unsafe_spans_by_file;
      ctx.scopes = {analysis::Scope{}, analysis::Scope{}, analysis::Scope{}};
      log_machine("phase=sema step=context-init modules=" +
                  std::to_string(parsed_module_set->size()));

      std::size_t visibility_index = 0;
      for (const auto& module : *parsed_module_set) {
        ++visibility_index;
        ctx.current_module = module.path;
        const std::string module_name = core::StringOfPath(module.path);
        log_machine("phase=sema step=visibility-check-start index=" +
                    std::to_string(visibility_index) + "/" +
                    std::to_string(parsed_module_set->size()) + " module=" +
                    module_name);
        const auto vis_diags = analysis::CheckModuleVisibility(ctx, module);
        for (const auto& diag : vis_diags) {
          core::Emit(diags, diag);
        }
        log_machine("phase=sema step=visibility-check-finish index=" +
                    std::to_string(visibility_index) + "/" +
                    std::to_string(parsed_module_set->size()) + " module=" +
                    module_name + " emitted_diags=" +
                    std::to_string(vis_diags.size()));
      }

      log_machine("phase=sema step=name-map-collect-start modules=" +
                  std::to_string(ctx.sigma.mods.size()));
      const auto name_maps = analysis::CollectNameMaps(ctx);
      for (const auto& diag : name_maps.diags) {
        core::Emit(diags, diag);
      }
      log_machine("phase=sema step=name-map-collect-finish emitted_diags=" +
                  std::to_string(name_maps.diags.size()));

      if (!HasBlockingErrorsForSema(diags)) {
        log_machine("phase=sema step=populate-sigma-start modules=" +
                    std::to_string(ctx.sigma.mods.size()));
        analysis::PopulateSigma(ctx);
        log_machine("phase=sema step=populate-sigma-finish");
        const auto module_names = analysis::ModuleNamesOf(sema_project);

        analysis::ResolveContext res_ctx;
        res_ctx.ctx = &ctx;
        res_ctx.name_maps = &name_maps.name_maps;
        res_ctx.module_names = &module_names;
        res_ctx.can_access = analysis::CanAccess;
        res_ctx.parse_ok = phase1_ok;
        res_ctx.parse_diags = &parse_phase_diags;

        log_machine("phase=sema step=resolve-start modules=" +
                    std::to_string(ctx.sigma.mods.size()));
        const auto resolved = analysis::ResolveModules(res_ctx);
        resolve_ok = resolved.ok;
        for (const auto& diag : resolved.diags) {
          core::Emit(diags, diag);
        }
        log_machine("phase=sema step=resolve-finish ok=" +
                    std::string(resolved.ok ? "true" : "false") +
                    " emitted_diags=" + std::to_string(resolved.diags.size()) +
                    " resolved_modules=" +
                    std::to_string(resolved.modules.size()));

        if (resolved.ok) {
          ctx.sigma.mods = resolved.modules;
          log_machine("phase=sema step=resolve-apply-start modules=" +
                      std::to_string(ctx.sigma.mods.size()));
          analysis::PopulateSigma(ctx);
          log_machine("phase=sema step=resolve-apply-finish");
        }

        std::optional<analysis::AssemblyImportGraph> assembly_graph;
        bool assembly_graph_ok = false;
        if (!HasBlockingErrorsForSema(diags) && resolve_ok) {
          typecheck_ok = true;
          const auto& graph_modules = parsed_project_module_set.has_value()
                                          ? *parsed_project_module_set
                                          : ctx.sigma.mods;
          assembly_graph =
              analysis::BuildAssemblyImportGraph(sema_project, graph_modules);
          assembly_graph_ok =
              analysis::ValidateAssemblyImportGraphStructure(sema_project,
                                                           *assembly_graph,
                                                           diags);
        }

        if (!HasBlockingErrorsForSema(diags) && resolve_ok &&
            assembly_graph_ok) {
          const auto& graph_modules = parsed_project_module_set.has_value()
                                          ? *parsed_project_module_set
                                          : ctx.sigma.mods;
          if (!analysis::ValidateHostedLibraryImportGraph(sema_project,
                                                         *assembly_graph,
                                                         graph_modules,
                                                         diags)) {
            typecheck_ok = false;
          }
        }

        if (!HasBlockingErrorsForSema(diags) && resolve_ok &&
            assembly_graph_ok && typecheck_ok) {
          if (opts->dump_project) {
            auto output_project = analysis::BuildOutputProjectForAssembly(
                sema_project, *assembly_graph, sema_project.assembly.name);
            if (!output_project.has_value()) {
              EmitInternalDiagnostic(
                  diags, ultraviolet::core::Severity::Error, std::nullopt,
                  "Failed to construct output project for dump-project: " +
                      sema_project.assembly.name);
              typecheck_ok = false;
            } else {
              const auto lines =
                  project::DumpProject(*output_project, target_profile,
                                       true);
              for (const auto& line : lines) {
                std::cout << line << "\n";
              }
              return 0;
            }
          }

          core::Conformance::SetPhase("typecheck");
          const auto typecheck_start = std::chrono::steady_clock::now();
          log_machine("phase=sema step=typecheck-start modules=" +
                      std::to_string(ctx.sigma.mods.size()));
          const auto typechecked =
              analysis::TypecheckModules(ctx, ctx.sigma.mods,
                                         &name_maps.name_maps);
          for (const auto& diag : typechecked.diags) {
            core::Emit(diags, diag);
          }
          typecheck_ok = typechecked.ok;

          if (typecheck_ok) {
            std::vector<const ast::ASTModule*> cap_modules;
            cap_modules.reserve(ctx.sigma.mods.size());
            for (const auto& module : ctx.sigma.mods) {
              cap_modules.push_back(&module);
            }

            auto call_graph = analysis::BuildCallGraph(ctx, cap_modules);
            analysis::PropagateCapabilityRequirements(call_graph);
            analysis::AnnotateCapabilityFlow(call_graph);
            const auto cap_chain = analysis::ValidateCapabilityChain(
                ctx, call_graph, &typechecked.expr_types);
            if (!cap_chain.valid) {
              typecheck_ok = false;
              std::size_t emitted_cap_diags = 0;
              for (const auto& err : cap_chain.errors) {
                EmitCapabilityChainErrorDiagnostic(diags, err);
                ++emitted_cap_diags;
              }
              for (const auto& leak : cap_chain.leaks) {
                EmitCapabilityLeakDiagnostic(diags, leak);
                ++emitted_cap_diags;
              }
              log_machine("phase=sema step=capability-chain-finish ok=false errors=" +
                          std::to_string(cap_chain.errors.size()) +
                          " leaks=" + std::to_string(cap_chain.leaks.size()) +
                          " emitted_diags=" + std::to_string(emitted_cap_diags));
            } else {
              log_machine("phase=sema step=capability-chain-finish ok=true");
            }

            log_machine("phase=sema step=authority-model-start modules=" +
                        std::to_string(cap_modules.size()));
            const auto authority = analysis::ValidateModuleAuthority(
                ctx, cap_modules, &typechecked.expr_types);
            if (!authority.valid) {
              typecheck_ok = false;
              std::size_t emitted_authority_diags = 0;
              for (const auto& err : authority.errors) {
                EmitAuthorityValidationDiagnostic(diags, err);
                ++emitted_authority_diags;
              }
              log_machine(
                  "phase=sema step=authority-model-finish ok=false errors=" +
                  std::to_string(authority.errors.size()) +
                  " emitted_diags=" +
                  std::to_string(emitted_authority_diags));
            } else {
              log_machine("phase=sema step=authority-model-finish ok=true");
            }
          }

          const auto typecheck_elapsed =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - typecheck_start)
                  .count();
          log_machine("phase=sema step=typecheck-finish ok=" +
                      std::string(typecheck_ok ? "true" : "false") +
                      " emitted_diags=" +
                      std::to_string(typechecked.diags.size()) +
                      " elapsed_ms=" + std::to_string(typecheck_elapsed));

          check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - sema_start).count();
          if (typecheck_ok && opts->check_only) {
            const auto lowerability_start = std::chrono::steady_clock::now();
            progress("Validating",
                     sema_project.assembly.name + " lowerability");
            log_machine("phase=lowerability");
            core::Conformance::SetPhase("lowerability");

            if (!assembly_graph.has_value()) {
              EmitInternalDiagnostic(
                  diags, ultraviolet::core::Severity::Error, std::nullopt,
                  "Failed to construct assembly import graph for lowerability validation: " +
                      sema_project.assembly.name);
              phase4_ok = false;
            } else {
              auto output_project = analysis::BuildOutputProjectForAssembly(
                  sema_project, *assembly_graph, sema_project.assembly.name);
              if (!output_project.has_value()) {
                if (const auto diag = core::MakeDiagnosticById("E-OUT-0417")) {
                  core::Emit(diags, *diag);
                }
                phase4_ok = false;
              } else {
                phase4_ok = driver::ValidateLowerability(sema_project,
                                                         *output_project,
                                                         ctx,
                                                         name_maps,
                                                         typechecked,
                                                         diags);
              }
            }

            codegen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - lowerability_start).count();
            log_machine("phase=lowerability finish ok=" +
                        std::string(phase4_ok ? "true" : "false") +
                        " emitted_diags=" + std::to_string(diags.size()) +
                        " elapsed_ms=" + std::to_string(codegen_ms));
          } else if (typecheck_ok) {
            const auto codegen_start = std::chrono::steady_clock::now();
            progress("Compiling",
                     sema_project.assembly.name + " (" +
                         std::to_string(ctx.sigma.mods.size()) + " modules)");
            log_machine("phase=codegen");
            core::Conformance::SetPhase("codegen");

            codegen::LowerCtx lower_ctx;
            lower_ctx.sigma = &ctx.sigma;
            lower_ctx.target_profile = target_profile;
            lower_ctx.executable_project =
                ultraviolet::project::IsExecutable(sema_project);
            lower_ctx.shared_library_project =
                ultraviolet::project::IsSharedLibrary(sema_project);
            lower_ctx.log_enabled = opts->log_enabled;
            lower_ctx.log_to_console = opts->log_to_console;
            lower_ctx.log_to_file = opts->log_to_file;
            lower_ctx.log_file_path = EffectiveRuntimeLogFilePath(sema_project, *opts);
            lower_ctx.trace = opts->trace;
            lower_ctx.trace_filter_mask = opts->trace_filter_mask;
            lower_ctx.trace_min_level = opts->trace_min_level;
            lower_ctx.trace_root = sema_project.root.generic_string();

            const auto* expr_types = &typechecked.expr_types;
            const auto* dynamic_refine_checks =
                &typechecked.dynamic_refine_checks;
            const auto* generic_call_substs =
                &typechecked.generic_call_substs;
            const auto* selected_call_targets =
                &typechecked.selected_call_targets;
            lower_ctx.expr_types =
                const_cast<analysis::ExprTypeMap*>(expr_types);
            lower_ctx.dynamic_refine_checks =
                const_cast<analysis::DynamicRefineExprMap*>(
                    dynamic_refine_checks);
            lower_ctx.generic_call_substs =
                const_cast<analysis::GenericCallSubstMap*>(
                    generic_call_substs);
            lower_ctx.selected_call_targets =
                const_cast<analysis::SelectedCallTargetMap*>(
                    selected_call_targets);
            lower_ctx.expr_type =
                [expr_types](const ast::Expr& expr) -> analysis::TypeRef {
              if (!expr_types) {
                return nullptr;
              }
              const auto it = expr_types->find(&expr);
              if (it == expr_types->end()) {
                return nullptr;
              }
              return it->second;
            };

            lower_ctx.resolve_name =
                [&](const std::string& name)
                    -> std::optional<std::vector<std::string>> {
              const auto module_key =
                  analysis::PathKeyOf(lower_ctx.module_path);
              const auto map_it = name_maps.name_maps.find(module_key);
              if (map_it == name_maps.name_maps.end()) {
                if (!codegen::BuiltinSym(name).empty()) {
                  return std::vector<std::string>{name};
                }
                return std::nullopt;
              }
              const auto ent_it =
                  map_it->second.find(analysis::IdKeyOf(name));
              if (ent_it == map_it->second.end()) {
                if (!codegen::BuiltinSym(name).empty()) {
                  return std::vector<std::string>{name};
                }
                return std::nullopt;
              }
              const auto& ent = ent_it->second;
              if (ent.kind != analysis::EntityKind::Value) {
                if (codegen::BuiltinSym(name).empty()) {
                  return std::nullopt;
                }
                return std::vector<std::string>{name};
              }
              const std::string resolved_name = ent.target_opt.value_or(name);
              if (!ent.origin_opt.has_value()) {
                if (codegen::BuiltinSym(resolved_name).empty()) {
                  return std::nullopt;
                }
                return std::vector<std::string>{resolved_name};
              }
              std::vector<std::string> full = *ent.origin_opt;
              full.push_back(resolved_name);
              return full;
            };

            lower_ctx.resolve_type_name =
                [&](const std::string& name)
                    -> std::optional<std::vector<std::string>> {
              const auto module_key =
                  analysis::PathKeyOf(lower_ctx.module_path);
              const auto map_it = name_maps.name_maps.find(module_key);
              if (map_it == name_maps.name_maps.end()) {
                return std::nullopt;
              }
              const auto ent_it =
                  map_it->second.find(analysis::IdKeyOf(name));
              if (ent_it == map_it->second.end()) {
                return std::nullopt;
              }
              const auto& ent = ent_it->second;
              if (ent.kind != analysis::EntityKind::Type ||
                  !ent.origin_opt.has_value()) {
                return std::nullopt;
              }
              std::vector<std::string> full = *ent.origin_opt;
              const std::string resolved_name = ent.target_opt.value_or(name);
              full.push_back(resolved_name);
              return full;
            };

            lower_ctx.resolve_type_name_in_module =
                [&name_maps](const std::vector<std::string>& module_path,
                             const std::string& name)
                    -> std::optional<std::vector<std::string>> {
              const auto module_key = analysis::PathKeyOf(module_path);
              const auto map_it = name_maps.name_maps.find(module_key);
              if (map_it == name_maps.name_maps.end()) {
                return std::nullopt;
              }
              const auto ent_it =
                  map_it->second.find(analysis::IdKeyOf(name));
              if (ent_it == map_it->second.end()) {
                return std::nullopt;
              }
              const auto& ent = ent_it->second;
              if (ent.kind != analysis::EntityKind::Type ||
                  !ent.origin_opt.has_value()) {
                return std::nullopt;
              }
              std::vector<std::string> full = *ent.origin_opt;
              const std::string resolved_name = ent.target_opt.value_or(name);
              full.push_back(resolved_name);
              return full;
            };

            if (typechecked.init_plan.has_value()) {
              lower_ctx.init_order = typechecked.init_plan->init_order;
              lower_ctx.init_modules = typechecked.init_plan->graph.modules;
              lower_ctx.init_eager_edges =
                  typechecked.init_plan->graph.eager_edges;
            }

            if (opts->emit_ir) {
              for (const ast::ASTModule& module : ctx.sigma.mods) {
                lower_ctx.module_path = module.path;
                lower_ctx.resolve_failed = false;
                lower_ctx.codegen_failed = false;
                lower_ctx.resolve_failures.clear();
                auto decls = codegen::LowerModule(module, lower_ctx);
                if (lower_ctx.resolve_failed || lower_ctx.codegen_failed) {
                  if (const auto diag = core::MakeDiagnosticById("E-OUT-0403")) {
                    core::Emit(diags, *diag);
                  }
                  phase4_ok = false;
                  break;
                }
                std::cout << codegen::DumpIR(decls) << "\n";
              }
              if (!core::HasError(diags)) {
                phase4_ok = true;
              }
            } else if (!opts->no_output) {
              if (!assembly_graph.has_value()) {
                phase4_ok = false;
              } else {
                const auto* ctx_ptr = &ctx;
                const auto* name_maps_ptr = &name_maps;
                const auto* typechecked_ptr = &typechecked;
                const bool log_enabled = lower_ctx.log_enabled;
                const bool log_to_console = lower_ctx.log_to_console;
                const bool log_to_file = lower_ctx.log_to_file;
                const bool trace = lower_ctx.trace;
                const auto trace_filter_mask = lower_ctx.trace_filter_mask;
                const auto trace_min_level = lower_ctx.trace_min_level;
                const std::string trace_root = lower_ctx.trace_root;
                const std::string log_file_path = lower_ctx.log_file_path;
                auto shared_cache = std::make_shared<std::shared_ptr<CodegenCache>>();
                auto shared_cache_mu = std::make_shared<std::mutex>();
                auto ensure_cache =
                    [shared_cache,
                     shared_cache_mu,
                     ctx_ptr,
                     name_maps_ptr,
                     typechecked_ptr,
                     log_enabled,
                     log_to_console,
                     log_to_file,
                     trace,
                     trace_filter_mask,
                     trace_min_level,
                     trace_root,
                     log_file_path,
                     &sema_project,
                     &log_machine](
                        const project::Project& p)
                        -> std::shared_ptr<CodegenCache> {
                  {
                    std::lock_guard<std::mutex> lock(*shared_cache_mu);
                    if (!*shared_cache) {
                      const auto cache_start = std::chrono::steady_clock::now();
                      const std::string cache_key =
                          "global|" + sema_project.assembly.name;
                      log_machine("phase=codegen cache=build-start key=" +
                                  cache_key + " modules=" +
                                  std::to_string(sema_project.modules.size()));
                      auto cache = BuildCodegenCache(sema_project,
                                                     *ctx_ptr,
                                                     *name_maps_ptr,
                                                     *typechecked_ptr);
                      if (cache) {
                        ConfigureCodegenContextForProject(*cache, p);
                        cache->ctx.log_enabled = log_enabled;
                        cache->ctx.log_to_console = log_to_console;
                        cache->ctx.log_to_file = log_to_file;
                        cache->ctx.trace = trace;
                        cache->ctx.trace_filter_mask = trace_filter_mask;
                        cache->ctx.trace_min_level = trace_min_level;
                        cache->ctx.trace_root = trace_root;
                        cache->ctx.log_file_path = log_file_path;
                      }
                      *shared_cache = cache;
                      const auto elapsed =
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - cache_start)
                              .count();
                      const bool cache_ok = cache && cache->ok.load();
                      const std::size_t lowered_count =
                          cache ? cache->modules.size() : 0;
                      log_machine("phase=codegen cache=build-finish key=" +
                                  cache_key + " ok=" +
                                  std::string(cache_ok ? "true" : "false") +
                                  " lowered=" +
                                  std::to_string(lowered_count) +
                                  " elapsed_ms=" + std::to_string(elapsed));
                    }
                  }
                  return *shared_cache;
                };

                if (IncrementalEnabled()) {
                  log_machine("phase=codegen incremental=deferred-per-artifact");
                }
                auto output_project_opt =
                    ultraviolet::analysis::BuildOutputProjectForAssembly(
                        sema_project, *assembly_graph, sema_project.assembly.name);
                if (!output_project_opt.has_value()) {
                  EmitInternalDiagnostic(
                      diags, core::Severity::Error, std::nullopt,
                      "Failed to construct output project for assembly: " +
                          sema_project.assembly.name);
                  phase4_ok = false;
                } else {
                  auto output_project = std::move(*output_project_opt);
                  auto incremental_cache = std::make_shared<std::unordered_map<
                      std::string, IncrementalBuildDataResult>>();
                  auto incremental_mu = std::make_shared<std::mutex>();
                  auto ensure_incremental =
                      [incremental_cache,
                       incremental_mu,
                       &diags,
                       &log_machine,
                       opts,
                       &ctx,
                       target_profile](const project::Project& p)
                          -> std::optional<IncrementalBuildDataResult> {
                    if (!IncrementalEnabled()) {
                      return std::nullopt;
                    }
                    const std::string cache_key =
                        p.assembly.name + "|" + p.assembly.kind + "|" +
                        p.assembly.link_kind.value_or("none");
                    {
                      std::lock_guard<std::mutex> lock(*incremental_mu);
                      const auto it = incremental_cache->find(cache_key);
                      if (it != incremental_cache->end()) {
                        return it->second;
                      }
                    }

                    const auto build_ast_modules =
                        FilterAstModulesForProject(ctx.sigma.mods, p);
                    const auto fingerprint_start =
                        std::chrono::steady_clock::now();
                    log_machine(
                        "phase=codegen incremental=fingerprint-start assembly=" +
                        p.assembly.name + " modules=" +
                        std::to_string(p.modules.size()));
                    const std::string build_key =
                        BuildIncrementalBuildKey(
                            p,
                            target_profile,
                            *opts,
                            g_compiler_executable_path,
                            EffectiveRuntimeLogFilePath(p, *opts));
                    SourceTextCache source_text_cache(diags);
                    const auto module_interface_hash =
                        [&source_text_cache](const ast::ASTModule& module) {
                          return ComputeModuleInterfaceHash(module,
                                                            source_text_cache);
                        };
                    auto incremental = BuildIncrementalBuildData(
                        p,
                        build_ast_modules,
                        build_key,
                        module_interface_hash,
                        diags);
                    const auto elapsed =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - fingerprint_start)
                            .count();
                    log_machine(
                        "phase=codegen incremental=fingerprint-finish assembly=" +
                        p.assembly.name + " ok=" +
                        std::string(incremental.ok ? "true" : "false") +
                        " modules=" +
                        std::to_string(incremental.ok
                                           ? incremental.modules.size()
                                           : 0) +
                        " elapsed_ms=" + std::to_string(elapsed));
                    if (!incremental.ok) {
                      return std::nullopt;
                    }

                    std::lock_guard<std::mutex> lock(*incremental_mu);
                    (*incremental_cache)[cache_key] = incremental;
                    return incremental;
                  };

                  progress("Compiling",
                           output_project.assembly.name + " (" +
                               std::to_string(output_project.modules.size()) +
                               " modules)");

                  driver::OutputPipelineDeps out_deps;
                  out_deps.ensure_dir = EnsureDir;
                  out_deps.codegen_obj =
                      [ensure_cache, target_profile, opt_level](const project::ModuleInfo& module,
                                             const project::Project& p)
                          -> std::optional<std::string> {
                    const auto cache = ensure_cache(p);
                    if (!cache || !cache->ok.load()) {
                      return std::nullopt;
                    }
                    return CodegenObj(*cache, module, p, target_profile, opt_level);
                  };
                  out_deps.codegen_obj_and_ir =
                      [ensure_cache, target_profile, opt_level](const project::ModuleInfo& module,
                                             const project::Project& p,
                                             std::string_view emit_ir)
                          -> std::optional<driver::CodegenObjectAndIR> {
                    const auto cache = ensure_cache(p);
                    if (!cache || !cache->ok.load()) {
                      return std::nullopt;
                    }
                    return CodegenObjAndIR(*cache,
                                           module,
                                           p,
                                           target_profile,
                                           opt_level,
                                           emit_ir);
                  };
                  out_deps.codegen_ir =
                      [ensure_cache, target_profile](const project::ModuleInfo& module,
                                             const project::Project& p,
                                            std::string_view emit_ir)
                          -> std::optional<std::string> {
                    const auto cache = ensure_cache(p);
                    if (!cache || !cache->ok.load()) {
                      return std::nullopt;
                    }
                    return CodegenIR(*cache, module, p, target_profile, emit_ir);
                  };
                  out_deps.write_file = WriteFile;
                  out_deps.resolve_tool =
                      static_cast<std::optional<std::filesystem::path> (*)(
                          const project::Project&,
                          project::TargetProfile,
                          std::string_view)>(
                          project::ResolveTool);
                  out_deps.assemble_ir = project::AssembleIR;
                  out_deps.resolve_runtime_lib = project::ResolveRuntimeLib;
                  out_deps.invoke_linker = project::InvokeLinker;
                  out_deps.linker_syms = project::LinkerSyms;
                  out_deps.archive_members = project::ArchiveMembers;
                  out_deps.invoke_archiver = project::InvokeArchiver;
                  const auto* front_end_ast_modules =
                      parsed_project_module_set.has_value()
                          ? &*parsed_project_module_set
                          : &ctx.sigma.mods;
                  out_deps.resolve_project_ast_modules =
                      [front_end_ast_modules](const project::Project&)
                          -> std::optional<std::reference_wrapper<
                              const std::vector<ast::ASTModule>>> {
                    return std::cref(*front_end_ast_modules);
                  };
                  out_deps.resolve_shared_library_exports =
                      [ensure_cache](const project::Project& p)
                          -> std::optional<driver::SharedLibraryExports> {
                    const auto cache = ensure_cache(p);
                    if (!cache || !cache->ok.load()) {
                      return std::nullopt;
                    }
                    ConfigureCodegenContextForProject(*cache, p);
                    if (!PopulateCodegenModules(*cache, p)) {
                      return std::nullopt;
                    }
                    return ResolveSharedLibraryExports(p, *cache);
                  };
                  out_deps.prepare_codegen_context =
                      [ensure_cache](const project::Project& p,
                                     const driver::SharedLibraryExports& exports)
                          -> bool {
                    const auto cache = ensure_cache(p);
                    if (!cache || !cache->ok.load()) {
                      return false;
                    }
                    if (!PrepareSharedLibraryCodegenContext(p, *cache, exports)) {
                      return false;
                    }
                    return PopulateCodegenModules(*cache, p);
                  };
                  out_deps.incremental_module =
                      [ensure_incremental](const project::ModuleInfo& module,
                                           const project::Project& p)
                          -> std::optional<driver::IncrementalModuleInfo> {
                    const auto incremental = ensure_incremental(p);
                    if (!incremental.has_value()) {
                      return std::nullopt;
                    }
                    const auto it = incremental->modules.find(module.path);
                    if (it == incremental->modules.end()) {
                      return std::nullopt;
                    }
                    return it->second;
                  };
                  out_deps.incremental_build_key =
                      [ensure_incremental](const project::Project& p)
                          -> std::optional<std::string> {
                    const auto incremental = ensure_incremental(p);
                    if (!incremental.has_value()) {
                      return std::nullopt;
                    }
                    return incremental->build_key;
                  };
                  const auto codegen_cache = ensure_cache(output_project);
                  if (!codegen_cache || !codegen_cache->ok.load()) {
                    EmitInternalDiagnostic(
                        diags, core::Severity::Error, std::nullopt,
                        "Failed to build codegen cache for assembly: " +
                            output_project.assembly.name);
                    phase4_ok = false;
                  } else {
                    auto output =
                        driver::OutputPipeline(output_project,
                                                target_profile,
                                                out_deps);
                    AppendDiags(diags, output.diags);
                    phase4_ok = output.artifacts.has_value();
                  }
                }
              }
            } else {
              phase4_ok = true;
            }
            codegen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - codegen_start).count();
          }
        }
      }
    }
  }
  }

  // ========================================================================
  // Conformance Checking
  // ========================================================================

  TruncateDiagnosticsToErrorCap(diags, effective_error_policy);

  analysis::PhaseOrderResult phases;
  phases.phase1_ok = phase1_ok;
  phases.phase2_ok = phase2_ok;
  phases.phase3_ok = opts->phase1_only
                         ? phase1_ok
                         : (phase1_ok && resolve_ok && typecheck_ok);
  phases.phase4_ok = opts->phase1_only ? false : phase4_ok;

  analysis::ConformanceInput input;
  input.phase_orders = phases;
  analysis::ConformanceJudgmentEvidence evidence;
  evidence.project_bound = project_result.project.has_value();
  evidence.parse_modules_ok = phase1_ok;
  evidence.execute_comptime_ok = phase2_ok;
  evidence.phase3_checks.resolve_modules_ok = resolve_ok;
  evidence.phase3_checks.decl_typing_ok = typecheck_ok;
  evidence.phase3_checks.main_check_ok = typecheck_ok;
  evidence.output_pipeline_ok = phase4_ok;
  input.evidence = evidence;
  input.error_count = CountErrorDiagnostics(diags);
  input.error_policy = effective_error_policy;

  const bool rejected =
      opts->phase1_only ? false : analysis::RejectIllFormed(input);

  // ========================================================================
  // Output Diagnostics
  // ========================================================================

  // Lazy source file registry: caches file contents for diagnostic rendering.
  std::unordered_map<std::string, std::optional<std::string>> source_cache;
  core::SourceRegistry source_registry =
      [&source_cache](const std::string& path)
          -> std::optional<std::string> {
    auto it = source_cache.find(path);
    if (it != source_cache.end()) {
      return it->second;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      source_cache[path] = std::nullopt;
      return std::nullopt;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    source_cache[path] = content;
    return content;
  };

  if (opts->diag_json) {
    const auto ordered_json = core::Order(diags);
    std::cout << DiagnosticStreamToJson(ordered_json, source_registry) << "\n";
  } else {
    const auto ordered = core::Order(diags);

    // Separate progress output from diagnostics.
    if (!ordered.empty() && show_build_progress) {
      std::cerr << "\n";
    }

    core::RenderOptions render_opts;
    render_opts.color = use_color;
    render_opts.terminal_width = core::TerminalWidth();
    render_opts.context_lines = 1;
    for (const auto& diag : ordered) {
      std::cerr << core::RenderRich(diag, source_registry, render_opts)
                << "\n";
    }

    const auto summary = core::DiagnosticSummary(ordered, use_color);
    if (!summary.empty()) {
      std::cerr << "\n" << summary << "\n";
    }
  }

  // ========================================================================
  // Exit Code Determination
  // ========================================================================

  const core::CompileStatusResult status = core::CompileStatus(diags);
  const bool ok = status == core::CompileStatusResult::Ok && !rejected;

  if (core::IsDebugEnabled("pipeline")) {
    std::cerr << "[trace][build] pipeline phase1_ok=" << phase1_ok
              << " phase2_ok=" << phase2_ok
              << " resolve_ok=" << resolve_ok
              << " typecheck_ok=" << typecheck_ok << " phase4_ok=" << phase4_ok
              << " diags=" << diags.size() << " rejected=" << rejected
              << " status=" << static_cast<int>(status) << "\n";
  }

  // Machine-format finish event (debug only).
  if (debug_pipeline) {
    std::size_t error_count = 0;
    for (const auto& diag : diags) {
      if (diag.severity == core::Severity::Error) {
        ++error_count;
      }
    }
    std::cerr << "[trace][build] pid=" << CurrentProcessId()
              << " event=finish ok=" << (ok ? "true" : "false")
              << " phase1_ok=" << phase1_ok
              << " phase2_ok=" << phase2_ok
              << " resolve_ok=" << resolve_ok
              << " typecheck_ok=" << typecheck_ok
              << " phase4_ok=" << phase4_ok << " errors=" << error_count
              << " rejected=" << rejected
              << " status=" << static_cast<int>(status) << "\n";
  }

  // Verbose per-phase timing breakdown.
  if (is_verbose && !opts->diag_json) {
    std::cerr << "\n  Phase timing:\n";
    std::cerr << "    parse:   " << parse_ms << "ms\n";
    std::cerr << "    comptime:" << comptime_ms << "ms\n";
    std::cerr << "    check:   " << check_ms << "ms\n";
    std::cerr << "    codegen: " << codegen_ms << "ms\n";
    std::cerr.flush();
  }

  // Human-friendly build result line.
  if (!opts->diag_json && show_build_progress) {
    const auto build_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - build_start);
    const auto ms = build_elapsed.count();
    std::string elapsed_str;
    if (ms < 1000) {
      elapsed_str = std::to_string(ms) + "ms";
    } else {
      const auto secs = ms / 1000;
      const auto frac = (ms % 1000) / 10;
      elapsed_str = std::to_string(secs) + "." +
                    (frac < 10 ? "0" : "") + std::to_string(frac) + "s";
    }

    if (ok) {
      std::string detail = "build succeeded";
      if (incremental_noop_reused) {
        detail += " (no changes)";
      }
      detail += " in " + elapsed_str;
      progress("Finished", detail);
    } else {
      progress("Finished", "build failed in " + elapsed_str,
               core::Color::BoldRed);
    }
  }

  return ok ? 0 : 1;
}
