#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef CURSIVE_TEST_COMPILER_PATH
#error "CURSIVE_TEST_COMPILER_PATH must be defined"
#endif

#ifndef CURSIVE_TEST_RUNTIME_LIB_PATH
#error "CURSIVE_TEST_RUNTIME_LIB_PATH must be defined"
#endif

#ifndef CURSIVE_TEST_TARGET_PROFILE
#error "CURSIVE_TEST_TARGET_PROFILE must be defined"
#endif

#ifndef CURSIVE_TEST_EXECUTABLE_SUFFIX
#error "CURSIVE_TEST_EXECUTABLE_SUFFIX must be defined"
#endif

#ifndef CURSIVE_TEST_WORK_ROOT
#error "CURSIVE_TEST_WORK_ROOT must be defined"
#endif

#ifndef CURSIVE_TEST_LLD_LINK_PATH
#error "CURSIVE_TEST_LLD_LINK_PATH must be defined"
#endif

#ifndef CURSIVE_TEST_LLVM_LIB_PATH
#error "CURSIVE_TEST_LLVM_LIB_PATH must be defined"
#endif

#ifndef CURSIVE_TEST_LLVM_AS_PATH
#error "CURSIVE_TEST_LLVM_AS_PATH must be defined"
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

std::string RunExecutableCommand(const std::filesystem::path& executable,
                                 const std::filesystem::path& run_log) {
#ifdef _WIN32
  return "cd /d " + Quote(executable.parent_path().generic_string()) + " && " +
         Quote(executable.filename().string()) + " > " +
         Quote(run_log.generic_string()) + " 2>&1";
#else
  return Quote(executable.generic_string()) + " > " +
         Quote(run_log.generic_string()) + " 2>&1";
#endif
}

std::string FixtureManifest() {
  std::ostringstream out;
  out << "[toolchain]\n";
  out << "runtime_lib = \"" << CURSIVE_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << CURSIVE_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"sret\"\n";
  out << "kind = \"executable\"\n";
  out << "root = \"src\"\n";
  out << "out_dir = \"build/sret\"\n";
  out << "emit_ir = \"ll\"\n";
  return out.str();
}

std::string FixtureSource() {
  return R"cursive(public record LargeAggregate {
    public v0: i64
    public v1: i64
    public v2: i64
    public v3: i64
    public v4: i64
    public v5: i64
}

public enum LargeResult {
    Ready(LargeAggregate)
    Rejected(i32)
}

procedure makeAggregate(seed: i64) -> LargeAggregate {
    return LargeAggregate {
        v0: seed,
        v1: seed + 1,
        v2: seed + 2,
        v3: seed + 3,
        v4: seed + 4,
        v5: seed + 5
    }
}

procedure nestedAggregate(seed: i64) -> LargeAggregate {
    return makeAggregate(seed)
}

procedure chooseAggregate(seed: i64) -> LargeResult {
    return LargeResult::Ready(nestedAggregate(seed))
}

procedure aggregateExitCode(value: LargeAggregate) -> i32 {
    if ((value.v0 == 10) &&
        (value.v1 == 11) &&
        (value.v2 == 12) &&
        (value.v3 == 13) &&
        (value.v4 == 14) &&
        (value.v5 == 15)) {
        return 0
    }
    return 1
}

procedure consumeAggregate(result: LargeResult) -> i32 {
    return if result is {
        LargeResult::Ready(value) { aggregateExitCode(value) }
        LargeResult::Rejected(code) { code }
    }
}

public modal Gate {
    @Closed {
        public transition open(value: i32) -> @Open {
            return Gate@Open { value: value }
        }
    }

    @Open {
        public value: i32
    }
}

procedure transitionExitCode() -> i32 {
    let closed: unique Gate@Closed = Gate@Closed {}
    let opened: Gate@Open = closed~>open(42)
    if (opened.value == 42) {
        return 0
    }
    return 2
}

public type PrimitiveUnion = i32 | bool

procedure numericUnion() -> PrimitiveUnion {
    return 9
}

procedure booleanUnion() -> PrimitiveUnion {
    return true
}

procedure typedUnionExitCode() -> i32 {
    let numeric: PrimitiveUnion = numericUnion()
    let boolean: PrimitiveUnion = booleanUnion()
    let numeric_ok: bool = if numeric is {
        value: i32 { value == 9 }
        value: bool { value == false }
    }
    let boolean_ok: bool = if boolean is {
        value: i32 { value == 0 }
        value: bool { value == true }
    }
    let numeric_type_test_ok: bool = if numeric is :i32 {
        true
    } else {
        false
    }
    let boolean_else_narrow_ok: bool = if boolean is :i32 {
        false
    } else {
        boolean == true
    }
    if (!numeric_ok) {
        return 30
    }
    if (!boolean_ok) {
        return 31
    }
    if (!numeric_type_test_ok) {
        return 32
    }
    if (!boolean_else_narrow_ok) {
        return 33
    }
    return 0
}

procedure conditionalLoopExitCode() -> i32 {
    let value: i32 = loop true {
        break 5
    }
    if (value == 5) {
        return 0
    }
    return 4
}

procedure nonCapturingClosureExitCode() -> i32 {
    let non_capturing = |value: i32| -> i32 value + 1
    let empty = || 5
    if (non_capturing(4) == 5 && empty() == 5) {
        return 0
    }
    return 5
}

procedure capturedClosureExitCode() -> i32 {
    let base_value: i32 = 3
    let capturing = |value: i32| -> i32 value + base_value
    if (capturing(4) == 7) {
        return 0
    }
    return 6
}

public procedure main(move ctx: Context) -> i32 {
    let _ = ctx
    let aggregate_code: i32 = consumeAggregate(chooseAggregate(10))
    if (aggregate_code != 0) {
        return aggregate_code
    }
    let transition_code: i32 = transitionExitCode()
    if (transition_code != 0) {
        return transition_code
    }
    let union_code: i32 = typedUnionExitCode()
    if (union_code != 0) {
        return union_code
    }
    let loop_code: i32 = conditionalLoopExitCode()
    if (loop_code != 0) {
        return loop_code
    }
    let non_capturing_code: i32 = nonCapturingClosureExitCode()
    if (non_capturing_code != 0) {
        return non_capturing_code
    }
    return capturedClosureExitCode()
}
)cursive";
}

}  // namespace

int main() {
  const std::filesystem::path work_root = CURSIVE_TEST_WORK_ROOT;
  const std::filesystem::path project_root = work_root / "codegen_abi_sret_fixture";
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
    std::cerr << "failed to create fixture directories: " << ec.message()
              << "\n";
    return 1;
  }

  if (!WriteFile(project_root / "Cursive.toml", FixtureManifest()) ||
      !WriteFile(source_root / "Main.cursive", FixtureSource())) {
    return 1;
  }
  if (!InstallTool(tool_root, CURSIVE_TEST_LLD_LINK_PATH, "lld-link") ||
      !InstallTool(tool_root, CURSIVE_TEST_LLVM_LIB_PATH, "llvm-lib") ||
      !InstallTool(tool_root, CURSIVE_TEST_LLVM_AS_PATH, "llvm-as")) {
    return 1;
  }

  const std::string compile_command =
      CommandWithToolPath(
          tool_root,
          Quote(CURSIVE_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(CURSIVE_TEST_TARGET_PROFILE) + " " +
              Quote(project_root.generic_string()) + " --assembly sret --out-dir " +
              Quote(out_root.generic_string()) +
              " --build-progress on --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "fixture compile failed; see " << compile_log << "\n";
    return 1;
  }

  const std::filesystem::path executable =
      out_root / "bin" /
      (std::string("sret") + std::string(CURSIVE_TEST_EXECUTABLE_SUFFIX));
  if (!std::filesystem::exists(executable)) {
    std::cerr << "fixture executable was not produced: " << executable << "\n";
    return 1;
  }

  const std::string run_command = RunExecutableCommand(executable, run_log);
  const int run_result = RunCommand(run_command);
  if (run_result != 0) {
    std::cerr << "fixture executable failed; see " << run_log << "\n";
    return 1;
  }

  return 0;
}
