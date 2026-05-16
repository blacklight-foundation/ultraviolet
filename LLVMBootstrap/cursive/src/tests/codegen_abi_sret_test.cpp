#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

std::optional<std::string_view> FunctionBody(
    std::string_view module_ir,
    std::string_view signature) {
  std::size_t start = std::string_view::npos;
  for (std::size_t pos = module_ir.find(signature);
       pos != std::string_view::npos;
       pos = module_ir.find(signature, pos + signature.size())) {
    const std::size_t line_start =
        module_ir.rfind('\n', pos) == std::string_view::npos
            ? 0
            : module_ir.rfind('\n', pos) + 1;
    if (module_ir.substr(line_start, 7) == "define ") {
      start = line_start;
      break;
    }
  }
  if (start == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t next =
      module_ir.find("\ndefine ", start + signature.size());
  const std::size_t end =
      next == std::string_view::npos ? module_ir.size() : next;
  return module_ir.substr(start, end - start);
}

std::size_t CountOccurrences(std::string_view body, std::string_view needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while (true) {
    pos = body.find(needle, pos);
    if (pos == std::string_view::npos) {
      return count;
    }
    ++count;
    pos += needle.size();
  }
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

public record LargeError {
    public row: usize
    public code0: i64
    public code1: i64
    public code2: i64
    public code3: i64
}

public type LargeUnion = LargeError | LargeAggregate

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

procedure makeLargeError(row: usize) -> LargeError {
    return LargeError {
        row: row,
        code0: 40,
        code1: 41,
        code2: 42,
        code3: 43
    }
}

procedure chooseAggregate(seed: i64) -> LargeResult {
    return LargeResult::Ready(nestedAggregate(seed))
}

procedure chooseLargeError() -> LargeUnion {
    return makeLargeError(7usize)
}

procedure chooseLargeAggregate() -> LargeUnion {
    return makeAggregate(10)
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

procedure largeErrorExitCode(value: LargeError) -> i32 {
    if ((value.row == 7usize) &&
        (value.code0 == 40) &&
        (value.code1 == 41) &&
        (value.code2 == 42) &&
        (value.code3 == 43)) {
        return 0
    }
    return 7
}

procedure concreteLargeUnionReturnExitCode() -> i32 {
    let error_or_aggregate: LargeUnion = chooseLargeError()
    let error_ok: bool = if error_or_aggregate is :LargeError {
        largeErrorExitCode(error_or_aggregate) == 0
    } else {
        false
    }
    if (!error_ok) {
        return 8
    }

    let aggregate_or_error: LargeUnion = chooseLargeAggregate()
    let aggregate_ok: bool = if aggregate_or_error is :LargeAggregate {
        aggregateExitCode(aggregate_or_error) == 0
    } else {
        false
    }
    if (!aggregate_ok) {
        return 9
    }
    return 0
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

procedure aggregateLocalCopyExitCode() -> i32 {
    var first: LargeAggregate = makeAggregate(0)
    var second: LargeAggregate = first
    var third: LargeAggregate = first
    var fourth: LargeAggregate = first

    first = makeAggregate(10)
    second = makeAggregate(20)
    third = makeAggregate(30)
    fourth = makeAggregate(40)

    if (first.v0 != 10) {
        return 40
    }
    if (second.v0 != 20) {
        return 41
    }
    if (third.v0 != 30) {
        return 42
    }
    if (fourth.v0 != 40) {
        return 43
    }
    return 0
}

record MutablePair {
    value: usize
    other: usize
}

record SelfAssignBag {
    count: usize
    values: [usize; 8]
}

procedure makeMutatedPair(seed: usize) -> MutablePair {
    var pair: MutablePair = MutablePair { value: 0usize, other: 7usize }
    pair.value = seed + 1usize
    pair.other = seed + 2usize
    return pair
}

procedure appendSelfAssignValue(bag: SelfAssignBag, value: usize) -> SelfAssignBag {
    var out: SelfAssignBag = bag
    [[dynamic]] {
        out.values[out.count] = value
    }
    out.count = out.count + 1usize
    return out
}

procedure aggregateFieldMutationExitCode() -> i32 {
    let pair: MutablePair = makeMutatedPair(10usize)
    if (pair.value != 11usize) {
        return 50
    }
    if (pair.other != 12usize) {
        return 51
    }
    return 0
}

procedure aggregateSelfAssignmentReturnExitCode() -> i32 {
    var bag: SelfAssignBag = SelfAssignBag {
        count: 0usize,
        values: [0usize; 8]
    }
    var index: usize = 0usize
    loop index < 6usize {
        bag = appendSelfAssignValue(bag, index)
        index = index + 1usize
    }
    if (bag.count != 6usize) {
        return 60
    }
    if (bag.values[0usize] != 0usize) {
        return 61
    }
    if (bag.values[1usize] != 1usize) {
        return 62
    }
    if (bag.values[2usize] != 2usize) {
        return 63
    }
    if (bag.values[3usize] != 3usize) {
        return 64
    }
    if (bag.values[4usize] != 4usize) {
        return 65
    }
    if (bag.values[5usize] != 5usize) {
        return 66
    }
    return 0
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

procedure timeCapabilityExitCode(time: $Time) -> i32 {
    let monotonic: $MonotonicTime = time~>monotonic()
    let start: MonotonicInstant = monotonic~>now()
    let end: MonotonicInstant = monotonic~>now()
    let resolution: Duration = monotonic~>resolution()
    let elapsed_result = monotonic~>elapsed(start, end)
    let monotonic_coarse = monotonic~>coarsen(resolution)
    let wall: $WallTime = time~>wall()
    let utc_result = wall~>now_utc()
    let wall_resolution = wall~>resolution()
    let wall_coarse = wall~>coarsen(resolution)
    let _ = elapsed_result
    let _ = monotonic_coarse
    let _ = utc_result
    let _ = wall_resolution
    let _ = wall_coarse
    return 0
}

procedure timeElapsedLoopExitCode(time: $Time) -> i32 {
    let monotonic: $MonotonicTime = time~>monotonic()
    var index: usize = 0usize
    loop index < 1usize {
        let start: MonotonicInstant = monotonic~>now()
        let end: MonotonicInstant = monotonic~>now()
        let elapsed: Outcome<Duration, TimeError> = monotonic~>elapsed(start, end)
        let _ = elapsed
        index = index + 1usize
    }
    return 0
}

public procedure main(move ctx: Context) -> i32 {
    let time_code: i32 = timeCapabilityExitCode(ctx.time)
    if (time_code != 0) {
        return time_code
    }
    let time_loop_code: i32 = timeElapsedLoopExitCode(ctx.time)
    if (time_loop_code != 0) {
        return time_loop_code
    }
    let aggregate_code: i32 = consumeAggregate(chooseAggregate(10))
    if (aggregate_code != 0) {
        return aggregate_code
    }
    let concrete_union_code: i32 = concreteLargeUnionReturnExitCode()
    if (concrete_union_code != 0) {
        return concrete_union_code
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
    let aggregate_local_copy_code: i32 = aggregateLocalCopyExitCode()
    if (aggregate_local_copy_code != 0) {
        return aggregate_local_copy_code
    }
    let aggregate_field_mutation_code: i32 = aggregateFieldMutationExitCode()
    if (aggregate_field_mutation_code != 0) {
        return aggregate_field_mutation_code
    }
    let aggregate_self_assignment_code: i32 = aggregateSelfAssignmentReturnExitCode()
    if (aggregate_self_assignment_code != 0) {
        return aggregate_self_assignment_code
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
              " --build-progress on --incremental off --opt-level O2 > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "fixture compile failed; see " << compile_log << "\n";
    return 1;
  }

  const std::filesystem::path ir_file = out_root / "ir" / "sret.ll";
  const auto ir_text = ReadFile(ir_file);
  if (!ir_text.has_value()) {
    return 1;
  }
  const auto append_body =
      FunctionBody(*ir_text, "@sret_x3a_x3aappendSelfAssignValue");
  if (!append_body.has_value()) {
    std::cerr << "appendSelfAssignValue was not emitted in LLVM IR\n";
    return 1;
  }
  const std::size_t aggregate_memcpys =
      CountOccurrences(*append_body, "llvm.memcpy");
  if (aggregate_memcpys > 1) {
    std::cerr << "aggregate self-update return still emitted "
              << aggregate_memcpys << " memcpy calls\n";
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
