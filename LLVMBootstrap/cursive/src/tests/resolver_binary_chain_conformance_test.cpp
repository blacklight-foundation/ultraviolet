#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef CURSIVE_TEST_COMPILER_PATH
#error "CURSIVE_TEST_COMPILER_PATH must be defined"
#endif

#ifndef CURSIVE_TEST_RUNTIME_LIB_PATH
#error "CURSIVE_TEST_RUNTIME_LIB_PATH must be defined"
#endif

#ifndef CURSIVE_TEST_TARGET_PROFILE
#error "CURSIVE_TEST_TARGET_PROFILE must be defined"
#endif

#ifndef CURSIVE_TEST_WORK_ROOT
#error "CURSIVE_TEST_WORK_ROOT must be defined"
#endif

namespace {

constexpr std::size_t kCatalogScaleEntryCount = 6045;

std::string Quote(std::string_view value) {
#ifdef _WIN32
  std::string out = "\"";
  for (char c : value) {
    if (c == '"') {
      out += "\\\"";
    } else {
      out += c;
    }
  }
  out += "\"";
  return out;
#else
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
#endif
}

bool WriteFile(const std::filesystem::path& path, std::string_view contents) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "failed to open " << path << " for writing\n";
    return false;
  }
  file << contents;
  if (!file) {
    std::cerr << "failed to write " << path << "\n";
    return false;
  }
  return true;
}

std::string FixtureManifest() {
  std::ostringstream out;
  out << "[toolchain]\n";
  out << "runtime_lib = \"" << CURSIVE_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << CURSIVE_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"resolver_binary_chain\"\n";
  out << "kind = \"library\"\n";
  out << "root = \"src\"\n";
  out << "out_dir = \"build/resolver_binary_chain\"\n";
  return out.str();
}

std::string FixtureSource() {
  std::ostringstream out;
  out << "internal procedure catalogKeyMatches(\n";
  out << "    csv_line: usize,\n";
  out << "    catalog_line: usize\n";
  out << ") -> bool {\n";
  out << "    return csv_line == catalog_line\n";
  out << "}\n\n";
  out << "public procedure catalogBinaryChainConforms() -> bool {\n";
  for (std::size_t index = 0; index < kCatalogScaleEntryCount; ++index) {
    const char* prefix = index == 0 ? "    return " : "        ";
    const char* suffix = index + 1 == kCatalogScaleEntryCount ? "" : " &&";
    out << prefix << "catalogKeyMatches(\n";
    out << "            " << index << "usize,\n";
    out << "            " << index << "usize\n";
    out << "        )" << suffix << "\n";
  }
  out << "}\n";
  return out.str();
}

int RunCommand(const std::string& command) {
  std::cerr << command << "\n";
  return std::system(command.c_str());
}

std::string CommandInDirectory(const std::filesystem::path& directory,
                               const std::string& command) {
#ifdef _WIN32
  return "cd /d " + Quote(directory.generic_string()) + " && " + command;
#else
  return "cd " + Quote(directory.generic_string()) + " && " + command;
#endif
}

}  // namespace

int main() {
  const std::filesystem::path work_root = CURSIVE_TEST_WORK_ROOT;
  const std::filesystem::path project_root =
      work_root / "resolver_binary_chain_fixture";
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path compile_log = project_root / "compile.log";

  std::error_code ec;
  std::filesystem::remove_all(project_root, ec);
  if (ec) {
    std::cerr << "failed to remove old fixture: " << ec.message() << "\n";
    return 1;
  }
  std::filesystem::create_directories(source_root, ec);
  if (ec) {
    std::cerr << "failed to create fixture directories: " << ec.message()
              << "\n";
    return 1;
  }

  if (!WriteFile(project_root / "Ultraviolet.toml", FixtureManifest()) ||
      !WriteFile(source_root / "Main.uv", FixtureSource())) {
    return 1;
  }

  const std::string compile_command =
      CommandInDirectory(
          project_root,
          Quote(CURSIVE_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(CURSIVE_TEST_TARGET_PROFILE) + " --check " +
              Quote(project_root.generic_string()) +
              " --assembly resolver_binary_chain --out-dir " +
              Quote(out_root.generic_string()) +
              " --build-progress off --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "catalog-scale binary-chain fixture failed; see "
              << compile_log << "\n";
    return 1;
  }

  return 0;
}
