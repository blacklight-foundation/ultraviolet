#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
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
  out << "root = \"src\"\n";
  out << "out_dir = \"build/" << name << "\"\n";
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

bool RunRejectedFixture(std::string_view name,
                        std::string_view source,
                        std::string_view diagnostic_code,
                        std::string_view phase,
                        std::string_view rule_id) {
  const std::filesystem::path project_root =
      std::filesystem::path(UV_TEST_WORK_ROOT) / std::string(name);
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path compile_log = project_root / "compile.log";
  const std::filesystem::path conformance_log =
      out_root / "logs" / "conformance" /
      (std::string(name) + ".conformance.log");

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
      !WriteFile(source_root / "Main.uv", source)) {
    return false;
  }

  const std::string compile_command =
      CommandInDirectory(
          project_root,
          Quote(UV_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(UV_TEST_TARGET_PROFILE) + " --check " +
              Quote(project_root.generic_string()) +
              " --assembly " + std::string(name) + " --out-dir " +
              Quote(out_root.generic_string()) + " --conformance " +
              std::string(name) + ".conformance.log "
              "--build-progress off --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result == 0) {
    std::cerr << "fixture compiled despite expected diagnostic "
              << diagnostic_code << "; see " << compile_log << "\n";
    return false;
  }

  const auto compile_text = ReadFile(compile_log);
  if (!compile_text.has_value()) {
    return false;
  }
  const std::string diagnostic_needle =
      "error[" + std::string(diagnostic_code) + "]";
  if (compile_text->find(diagnostic_needle) == std::string::npos) {
    std::cerr << "expected diagnostic " << diagnostic_code
              << " was not emitted; see " << compile_log << "\n";
    return false;
  }

  const auto conformance_text = ReadFile(conformance_log);
  if (!conformance_text.has_value()) {
    return false;
  }
  const std::string rule_needle =
      "\t" + std::string(phase) + "\t" + std::string(rule_id) + "\t";
  if (conformance_text->find(rule_needle) == std::string::npos) {
    std::cerr << "conformance trace did not record " << rule_id << "\n";
    return false;
  }

  return true;
}

bool RunAcceptedFixture(std::string_view name,
                        std::string_view source,
                        std::initializer_list<std::string_view> rule_ids) {
  const std::filesystem::path project_root =
      std::filesystem::path(UV_TEST_WORK_ROOT) / std::string(name);
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path compile_log = project_root / "compile.log";
  const std::filesystem::path conformance_log =
      out_root / "logs" / "conformance" /
      (std::string(name) + ".conformance.log");

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
      !WriteFile(source_root / "Main.uv", source)) {
    return false;
  }

  const std::string compile_command =
      CommandInDirectory(
          project_root,
          Quote(UV_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(UV_TEST_TARGET_PROFILE) + " --check " +
              Quote(project_root.generic_string()) +
              " --assembly " + std::string(name) + " --out-dir " +
              Quote(out_root.generic_string()) + " --conformance " +
              std::string(name) + ".conformance.log "
              "--build-progress off --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "fixture failed to compile; see " << compile_log << "\n";
    return false;
  }

  const auto conformance_text = ReadFile(conformance_log);
  if (!conformance_text.has_value()) {
    return false;
  }
  for (std::string_view rule_id : rule_ids) {
    const std::string rule_needle =
        "\ttypecheck\t" + std::string(rule_id) + "\t";
    if (conformance_text->find(rule_needle) == std::string::npos) {
      std::cerr << "conformance trace did not record " << rule_id << "\n";
      return false;
    }
  }

  return true;
}

}  // namespace

int main() {
  constexpr std::string_view bare_type_source = R"uv(public type Number = i32

public procedure classify(value: Number) -> i32 {
    return if value is Number { 1 } else { 0 }
}
)uv";

  constexpr std::string_view incompatible_type_source =
      R"uv(public procedure classify(value: i32) -> i32 {
    return if value is :bool { 1 } else { 0 }
}
)uv";

  constexpr std::string_view returning_branch_source =
      R"uv(public record RowError {
    public row_index: usize
}

public record InvoiceRecord {
    public amount_cents: i64
}

public procedure classify_statement(value: RowError | InvoiceRecord) -> i32 {
    if value is :RowError {
        return 1
    } else if value is : InvoiceRecord {
        return 2
    }

    return 0
}

public procedure classify_expression(value: RowError | InvoiceRecord) -> i32 {
    return if value is : RowError { 1 } else if value is :InvoiceRecord { 2 } else { 0 }
}
)uv";

  if (!RunRejectedFixture("if_is_bare_type", bare_type_source, "E-SEM-2761",
                          "resolve", "IfIs-BareTypePattern-Err")) {
    return 1;
  }
  if (!RunRejectedFixture("if_is_incompatible_type",
                          incompatible_type_source, "E-SEM-2762",
                          "typecheck", "IfIs-TypedPattern-Incompatible")) {
    return 1;
  }
  if (!RunAcceptedFixture("if_is_returning_branch",
                          returning_branch_source,
                          {"T-IfIs", "T-IfIs-No-Else", "Chk-IfIs"})) {
    return 1;
  }

  return 0;
}
