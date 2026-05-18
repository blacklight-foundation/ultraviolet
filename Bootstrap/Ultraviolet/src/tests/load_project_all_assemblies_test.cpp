#include "01_project/project.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#ifndef UV_TEST_WORK_ROOT
#error "UV_TEST_WORK_ROOT must be defined"
#endif

namespace {

bool WriteFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    std::cerr << "failed to write " << path << "\n";
    return false;
  }
  out << text;
  return true;
}

bool HasDiagnosticCode(const ultraviolet::core::DiagnosticStream& diags,
                       std::string_view code) {
  for (const auto& diag : diags) {
    if (diag.code == code) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path root =
      fs::path(UV_TEST_WORK_ROOT) / "load_project_all_assemblies";

  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "AlphaSource", ec);
  fs::create_directories(root / "BetaSource", ec);
  if (ec) {
    std::cerr << "failed to create fixture directories: " << ec.message()
              << "\n";
    return 1;
  }

  if (!WriteFile(root / "AlphaSource" / "Alpha.uv", "\n") ||
      !WriteFile(root / "BetaSource" / "Beta.uv", "\n")) {
    return 1;
  }

  if (!WriteFile(root / "Ultraviolet.toml",
                 "[[assembly]]\n"
                 "name = \"AlphaLib\"\n"
                 "kind = \"library\"\n"
                 "link_kind = \"static\"\n"
                 "root = \"AlphaSource\"\n"
                 "\n"
                 "[[assembly]]\n"
                 "name = \"BetaLib\"\n"
                 "kind = \"library\"\n"
                 "link_kind = \"static\"\n"
                 "root = \"BetaSource\"\n")) {
    return 1;
  }

  const auto ordinary = ultraviolet::project::LoadProject(root);
  if (ordinary.project.has_value() ||
      !HasDiagnosticCode(ordinary.diags, "E-PRJ-0205")) {
    std::cerr << "ordinary project load should require explicit assembly "
                 "selection\n";
    return 1;
  }

  const auto all = ultraviolet::project::LoadProjectAllAssemblies(root);
  if (!all.project.has_value()) {
    std::cerr << "all-assemblies project load failed\n";
    return 1;
  }
  if (all.project->assemblies.size() != 2) {
    std::cerr << "all-assemblies project load returned "
              << all.project->assemblies.size() << " assemblies\n";
    return 1;
  }

  const auto selected = ultraviolet::project::LoadProjectAllAssemblies(
      root, ultraviolet::project::AssemblyTarget{std::string("BetaLib")});
  if (!selected.project.has_value() ||
      selected.project->assembly.name != "BetaLib" ||
      selected.project->assemblies.size() != 2) {
    std::cerr << "all-assemblies load should preserve explicit selection "
                 "without dropping other assemblies\n";
    return 1;
  }

  return 0;
}
