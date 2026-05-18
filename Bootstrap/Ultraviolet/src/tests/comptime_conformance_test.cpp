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
  if (!file) {
    std::cerr << "failed to write " << path << "\n";
    return false;
  }
  return true;
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

std::string FixtureManifest() {
  std::ostringstream out;
  out << "[toolchain]\n";
  out << "runtime_lib = \"" << UV_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << UV_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"comptime_fixture\"\n";
  out << "kind = \"library\"\n";
  out << "root = \"src\"\n";
  out << "out_dir = \"build/comptime_fixture\"\n";
  return out.str();
}

std::string FixtureSource() {
  return R"uv(public procedure useComptime() -> u64 {
    let direct = comptime { 1 + 2 }
    let then_value = comptime if true { 3 } else { 4 }
    let else_value = comptime if false { 5 } else { 6 }
    comptime loop value in [1, 2] {
    }
    comptime {
        let quoted = quote type { i32 }
    }
    return direct
}
)uv";
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

bool ContainsRule(std::string_view conformance_text, std::string_view rule) {
  const std::string needle = "\t" + std::string(rule) + "\t";
  return conformance_text.find(needle) != std::string_view::npos;
}

}  // namespace

int main() {
  const std::filesystem::path work_root = UV_TEST_WORK_ROOT;
  const std::filesystem::path project_root = work_root / "comptime_fixture";
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path compile_log = project_root / "compile.log";
  const std::filesystem::path conformance_log =
      out_root / "logs" / "conformance" / "comptime_fixture.conformance.log";

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
          Quote(UV_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(UV_TEST_TARGET_PROFILE) + " --check " +
              Quote(project_root.generic_string()) +
              " --assembly comptime_fixture --out-dir " +
              Quote(out_root.generic_string()) +
              " --conformance comptime_fixture.conformance.log "
              "--build-progress off --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "fixture check failed; see " << compile_log << "\n";
    return 1;
  }

  const auto conformance_text = ReadFile(conformance_log);
  if (!conformance_text.has_value()) {
    return 1;
  }

  for (std::string_view rule : {
           "ComptimePass-Cons",
           "CtExecModule",
           "CtExpandItemSeq-Cons",
           "CtExpandBlock",
           "CtExpandStmtSeq-Cons",
           "CtExpandStmt-CtStmt",
           "CtExpandExpr-CtExpr",
           "CtExpandExpr-CtIf-True",
           "CtExpandExpr-CtIf-False",
           "CtExpandExpr-CtLoopIter",
           "CtLoopIterUnroll-Cons",
           "CtEval-Quote",
       }) {
    if (!ContainsRule(*conformance_text, rule)) {
      std::cerr << "conformance trace did not record " << rule << "\n";
      return 1;
    }
  }

  return 0;
}
