#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifndef UV_TEST_COMPILER_PATH
#error "UV_TEST_COMPILER_PATH must be defined"
#endif

#ifndef UV_TEST_RUNTIME_LIB_PATH
#error "UV_TEST_RUNTIME_LIB_PATH must be defined"
#endif

#ifndef UV_TEST_TARGET_PROFILE
#error "UV_TEST_TARGET_PROFILE must be defined"
#endif

#ifndef UV_TEST_WORK_ROOT
#error "UV_TEST_WORK_ROOT must be defined"
#endif

namespace {

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
  return static_cast<bool>(file);
}

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

std::string FixtureManifest(std::string_view name) {
  std::ostringstream out;
  out << "[toolchain]\n";
  out << "runtime_lib = \"" << UV_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << UV_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"" << name << "\"\n";
  out << "kind = \"library\"\n";
  out << "root = \"Source\"\n";
  out << "out_dir = \"Build/" << name << "\"\n";
  return out.str();
}

std::string CommandInDirectory(const std::filesystem::path& directory,
                               const std::string& command) {
#ifdef _WIN32
  return "cd /d " + Quote(directory.generic_string()) + " && " + command;
#else
  return "cd " + Quote(directory.generic_string()) + " && " + command;
#endif
}

int RunCommand(const std::string& command) {
  std::cerr << command << "\n";
  return std::system(command.c_str());
}

bool RunReservedIdentifierFixture(std::string_view name,
                                  std::string_view source) {
  const std::filesystem::path project_root =
      std::filesystem::path(UV_TEST_WORK_ROOT) / std::string(name);
  const std::filesystem::path source_root = project_root / "Source";
  const std::filesystem::path source_file = source_root / "Main.uv";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path stdout_file = project_root / "stdout.json";
  const std::filesystem::path stderr_file = project_root / "stderr.txt";

  std::error_code ec;
  std::filesystem::remove_all(project_root, ec);
  if (ec) {
    std::cerr << "failed to remove old fixture: " << ec.message() << "\n";
    return false;
  }
  std::filesystem::create_directories(source_root, ec);
  if (ec) {
    std::cerr << "failed to create fixture directories: " << ec.message()
              << "\n";
    return false;
  }

  if (!WriteFile(project_root / "Ultraviolet.toml", FixtureManifest(name)) ||
      !WriteFile(source_file, source)) {
    return false;
  }

  const std::string command =
      CommandInDirectory(
          project_root,
          Quote(UV_TEST_COMPILER_PATH) + " build " +
              Quote(source_file.generic_string()) + " --check --diag-json " +
              "--assembly " + std::string(name) + " --out-dir " +
              Quote(out_root.generic_string()) +
              " --build-progress off --incremental off > " +
              Quote(stdout_file.generic_string()) + " 2> " +
              Quote(stderr_file.generic_string()));
  const int result = RunCommand(command);
  if (result == 0) {
    std::cerr << "fixture compiled despite reserved identifier: " << name
              << "\n";
    return false;
  }

  const auto stdout_text = ReadFile(stdout_file);
  const auto stderr_text = ReadFile(stderr_file);
  if (!stdout_text.has_value() || !stderr_text.has_value()) {
    return false;
  }
  const std::string combined = *stdout_text + "\n" + *stderr_text;
  if (combined.find("\"E-CNF-0401\"") == std::string::npos &&
      combined.find("E-CNF-0401") == std::string::npos) {
    std::cerr << "expected E-CNF-0401 for " << name << "\n";
    std::cerr << combined << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  constexpr std::string_view reserved_param_no_use =
      R"uv(public procedure helper(record: i32) -> i32 {
    return 1
}
)uv";

  constexpr std::string_view reserved_local_binding =
      R"uv(public procedure helper() -> i32 {
    let record = 1
    return 1
}
)uv";

  constexpr std::string_view reserved_closure_param =
      R"uv(public procedure helper() -> i32 {
    let f = |record: i32| -> i32 1
    return 1
}
)uv";

  if (!RunReservedIdentifierFixture("reserved_param_no_use",
                                    reserved_param_no_use)) {
    return 1;
  }
  if (!RunReservedIdentifierFixture("reserved_local_binding",
                                    reserved_local_binding)) {
    return 1;
  }
  if (!RunReservedIdentifierFixture("reserved_closure_param",
                                    reserved_closure_param)) {
    return 1;
  }

  return 0;
}
