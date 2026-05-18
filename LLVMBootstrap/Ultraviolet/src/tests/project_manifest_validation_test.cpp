#include "00_core/spec_trace.h"
#include "01_project/project.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifndef UV_TEST_WORK_ROOT
#error "UV_TEST_WORK_ROOT must be defined"
#endif

namespace {

using ultraviolet::core::Conformance;
using ultraviolet::project::ValidateManifest;

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "failed to open " << path << " for reading\n";
    return std::nullopt;
  }

  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool ContainsRule(std::string_view conformance_text, std::string_view rule) {
  const std::string needle = "\tproject-system\t" + std::string(rule) + "\t";
  return conformance_text.find(needle) != std::string_view::npos;
}

toml::table MakeManifestWithOptionalAssemblyFields() {
  toml::table assembly;
  assembly.insert("name", "manifest_trace");
  assembly.insert("kind", "library");
  assembly.insert("root", "src");
  assembly.insert("emit_ir", "ll");
  assembly.insert("link_kind", "static");

  toml::table manifest;
  manifest.insert("assembly", std::move(assembly));
  return manifest;
}

}  // namespace

int main() {
  const std::filesystem::path work_root =
      std::filesystem::path(UV_TEST_WORK_ROOT) /
      "project_manifest_validation";
  std::error_code ec;
  std::filesystem::create_directories(work_root, ec);
  if (ec) {
    std::cerr << "failed to create fixture directory: " << ec.message() << "\n";
    return 1;
  }

  const std::filesystem::path conformance_log =
      work_root / "project_manifest_validation.conformance.log";
  const std::filesystem::path closed_log =
      work_root / "project_manifest_validation_closed.conformance.log";
  std::filesystem::remove(conformance_log, ec);
  std::filesystem::remove(closed_log, ec);

  Conformance::Init(conformance_log.string(), "compile");
  Conformance::SetRoot(work_root.string());
  Conformance::SetPhase("project-system");

  const toml::table manifest = MakeManifestWithOptionalAssemblyFields();
  const auto result = ValidateManifest(work_root, manifest);
  if (!result.diags.empty()) {
    std::cerr << "manifest validation emitted " << result.diags.size()
              << " diagnostics\n";
    return 1;
  }
  if (result.assemblies.size() != 1) {
    std::cerr << "manifest validation returned " << result.assemblies.size()
              << " assemblies\n";
    return 1;
  }

  const auto& assembly = result.assemblies.front();
  if (assembly.emit_ir != "ll") {
    std::cerr << "manifest validation did not preserve emit_ir\n";
    return 1;
  }
  if (assembly.link_kind != "static") {
    std::cerr << "manifest validation did not preserve link_kind\n";
    return 1;
  }

  Conformance::Init(closed_log.string(), "compile");

  const auto conformance_text = ReadFile(conformance_log);
  if (!conformance_text.has_value()) {
    return 1;
  }
  for (const std::string_view rule :
       {"WF-Assembly-EmitIRType", "WF-Assembly-LinkKindType"}) {
    if (!ContainsRule(*conformance_text, rule)) {
      std::cerr << "conformance trace did not record " << rule << "\n";
      return 1;
    }
  }

  return 0;
}
