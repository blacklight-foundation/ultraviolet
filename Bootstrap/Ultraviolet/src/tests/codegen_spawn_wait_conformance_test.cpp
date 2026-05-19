#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef UV_TEST_COMPILER_PATH
#error "UV_TEST_COMPILER_PATH must be defined"
#endif

#ifndef UV_TEST_RUNTIME_LIB_PATH
#error "UV_TEST_RUNTIME_LIB_PATH must be defined"
#endif

#ifndef UV_TEST_TARGET_PROFILE
#error "UV_TEST_TARGET_PROFILE must be defined"
#endif

#ifndef UV_TEST_EXECUTABLE_SUFFIX
#error "UV_TEST_EXECUTABLE_SUFFIX must be defined"
#endif

#ifndef UV_TEST_WORK_ROOT
#error "UV_TEST_WORK_ROOT must be defined"
#endif

#ifndef UV_TEST_LLD_LINK_PATH
#error "UV_TEST_LLD_LINK_PATH must be defined"
#endif

#ifndef UV_TEST_LLVM_LIB_PATH
#error "UV_TEST_LLVM_LIB_PATH must be defined"
#endif

#ifndef UV_TEST_LLVM_AS_PATH
#error "UV_TEST_LLVM_AS_PATH must be defined"
#endif

namespace {

std::string Quote(std::string_view value) {
  std::string out = "\"";
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  out.push_back('"');
  return out;
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
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "failed to open " << path << " for reading\n";
    return std::nullopt;
  }
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

int RunCommand(const std::string& command) {
  std::cerr << command << "\n";
  return std::system(command.c_str());
}

bool InstallTool(const std::filesystem::path& tool_root,
                 const std::filesystem::path& source,
                 std::string_view name) {
  std::error_code ec;
  std::filesystem::create_directories(tool_root, ec);
  if (ec) {
    std::cerr << "failed to create tool directory " << tool_root << ": "
              << ec.message() << "\n";
    return false;
  }
  if (!std::filesystem::exists(source, ec) || ec) {
    std::cerr << "required test tool is missing: " << source << "\n";
    return false;
  }

  const std::filesystem::path target = tool_root / std::filesystem::path(name);
  std::filesystem::remove(target, ec);
  ec.clear();
  std::filesystem::create_symlink(source, target, ec);
  if (!ec) {
    return true;
  }

  ec.clear();
  std::filesystem::copy_file(
      source,
      target,
      std::filesystem::copy_options::overwrite_existing,
      ec);
  if (ec) {
    std::cerr << "failed to install test tool " << source << " as " << target
              << ": " << ec.message() << "\n";
    return false;
  }
  return true;
}

std::string CommandWithToolPath(const std::filesystem::path& tool_root,
                                const std::string& command) {
#ifdef _WIN32
  return "set \"PATH=" + tool_root.generic_string() + ";%PATH%\" && " +
         command;
#else
  return "PATH=" + Quote(tool_root.generic_string()) + ":$PATH " + command;
#endif
}

int RunExecutableWithTimeout(const std::filesystem::path& executable,
                             const std::filesystem::path& run_log) {
#ifdef _WIN32
  std::string command = "cmd.exe /c \"\"" + executable.filename().string() +
                        "\" > \"" + run_log.generic_string() + "\" 2>&1\"";
  std::vector<char> mutable_command(command.begin(), command.end());
  mutable_command.push_back('\0');

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};
  const std::string working_dir = executable.parent_path().string();
  if (!CreateProcessA(
          nullptr,
          mutable_command.data(),
          nullptr,
          nullptr,
          FALSE,
          0,
          nullptr,
          working_dir.c_str(),
          &startup_info,
          &process_info)) {
    std::cerr << "failed to start fixture executable\n";
    return 1;
  }

  const DWORD wait_result = WaitForSingleObject(process_info.hProcess, 20000);
  DWORD exit_code = 1;
  if (wait_result == WAIT_TIMEOUT) {
    TerminateProcess(process_info.hProcess, 124);
    exit_code = 124;
  } else {
    GetExitCodeProcess(process_info.hProcess, &exit_code);
  }
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return static_cast<int>(exit_code);
#else
  const std::string command =
      "timeout 20s " + Quote(executable.generic_string()) + " > " +
      Quote(run_log.generic_string()) + " 2>&1";
  return RunCommand(command);
#endif
}

std::string FixtureManifest() {
  std::ostringstream out;
  out << "[toolchain]\n";
  out << "runtime_lib = \"" << UV_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << UV_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"spawn_wait\"\n";
  out << "kind = \"executable\"\n";
  out << "root = \"src\"\n";
  out << "out_dir = \"build/spawn_wait\"\n";
  out << "emit_ir = \"ll\"\n";
  return out.str();
}

std::string FixtureSource() {
  return R"uv(internal procedure inlineSpawnWait(context: Context) -> i32 {
    return parallel context~>inline() {
        let handle: Spawned<i32> = spawn {
            23
        }
        wait handle
    }
}

internal procedure cpuSpawnWait(context: Context) -> i32 {
    return parallel context~>cpu() {
        let handle: Spawned<i32> = spawn {
            24
        }
        wait handle
    }
}

public procedure main(context: Context) -> i32 {
    if inlineSpawnWait(context) != 23 {
        return 1
    }
    if cpuSpawnWait(context) != 24 {
        return 2
    }
    return 0
}
)uv";
}

}  // namespace

int main() {
  const std::filesystem::path work_root = UV_TEST_WORK_ROOT;
  const std::filesystem::path project_root = work_root / "spawn_wait_fixture";
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path tool_root = project_root / "tools";
  const std::filesystem::path compile_log = project_root / "compile.log";
  const std::filesystem::path run_log = project_root / "run.log";

  std::error_code ec;
  std::filesystem::remove_all(project_root, ec);
  if (ec) {
    std::cerr << "failed to remove old fixture: " << ec.message() << "\n";
    return 1;
  }
  std::filesystem::create_directories(source_root, ec);
  if (ec) {
    std::cerr << "failed to create fixture directories: " << ec.message() << "\n";
    return 1;
  }

  if (!WriteFile(project_root / "Ultraviolet.toml", FixtureManifest()) ||
      !WriteFile(source_root / "Main.uv", FixtureSource())) {
    return 1;
  }
  if (!InstallTool(tool_root, UV_TEST_LLD_LINK_PATH, "lld-link") ||
      !InstallTool(tool_root, UV_TEST_LLVM_LIB_PATH, "llvm-lib") ||
      !InstallTool(tool_root, UV_TEST_LLVM_AS_PATH, "llvm-as")) {
    return 1;
  }

  const std::string compile_command =
      CommandWithToolPath(
          tool_root,
          Quote(UV_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(UV_TEST_TARGET_PROFILE) + " " +
              Quote(project_root.generic_string()) +
              " --assembly spawn_wait --out-dir " +
              Quote(out_root.generic_string()) +
              " --build-progress off --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "spawn wait fixture compile failed; see "
              << compile_log << "\n";
    return 1;
  }

  const auto ir_text = ReadFile(out_root / "ir" / "spawn_x5fwait.ll");
  if (!ir_text.has_value()) {
    return 1;
  }
  if (ir_text->find("define internal void @__cx_spawn_body_spawn_wait") ==
      std::string_view::npos) {
    std::cerr << "spawn body wrapper was not emitted\n";
    return 1;
  }
  if (ir_text->find("load ptr, ptr %result, align 8") !=
      std::string_view::npos) {
    std::cerr << "spawn body result pointer used source-level by-ref ABI\n";
    return 1;
  }

  const std::filesystem::path executable =
      out_root / "bin" / ("spawn_wait" UV_TEST_EXECUTABLE_SUFFIX);
  const int run_result = RunExecutableWithTimeout(executable, run_log);
  if (run_result != 0) {
    std::cerr << "spawn wait fixture exited with " << run_result
              << "; see " << run_log << "\n";
    return 1;
  }

  return 0;
}
