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

#ifndef CURSIVE_TEST_WORK_ROOT
#error "CURSIVE_TEST_WORK_ROOT must be defined"
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
  out << "runtime_lib = \"" << CURSIVE_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << CURSIVE_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"parser_generics\"\n";
  out << "kind = \"library\"\n";
  out << "root = \"src\"\n";
  out << "out_dir = \"build/parser_generics\"\n";
  return out.str();
}

std::string FixtureSource() {
  return R"cursive(public class EmptyClass {
}

public class Equatable {
    public type Item
    public type Number = i32
}

public record Pair<T = i32; U <: Equatable, Equatable> |: Clone(T); FfiSafe(U)
{
    public type Stored = T
    public first: T
    public second: U
} |: { true }

public type FunctionType = (i32, bool) -> i32
public type EmptyClosure = || -> i32
public type SharedClosure = |i32, (i32 | bool)| -> i32 [shared: { dep: i32 }]
public type TrailingClosure = |i32,
| -> i32

extern "C" {
    public procedure foreignString(value: i32) -> i32
    public procedure foreignEnsuresPlain(value: i32) -> i32 |: @foreign_ensures(true)
    public procedure foreignEnsuresError(value: i32) -> i32 |: @foreign_ensures(@error: true)
    public procedure foreignEnsuresNull(value: i32) -> i32 |: @foreign_ensures(@null_result: true)
}

extern C {
    public procedure foreignIdent(value: i32) -> i32
}

extern {
    public procedure foreignDefault(value: i32) -> i32
}

derive target BuildEquatable(target: Type) |: requires Equatable, emits Generated {
    let reflected = target
}

public procedure takePair(input: Pair<i32, i32>) -> Pair<i32, i32> {
    loop |: { true } {
        break input
    }
}

public procedure takeRefined(value: i32 |: { true }) -> i32 {
    return value
}

public procedure useComptimeForms() -> i32 {
    let reflected_type = Type::<i32>;
    let raw_quote = quote { 1 + 2 };
    let type_quote = quote type { Pair<i32, i32> };
    let pattern_quote = quote pattern { value: i32 };
    let capability = comptime { introspect };
    return 0
}

public procedure useClosureExpr() -> i32 {
    let f = || 1
    let typed = |value: i32| -> i32 value
    let untyped = |value| value
    let moved_typed = |move value: i32| value
    let moved = |move value| value
    let empty_call = foreignString()
    let moved_call = foreignString(move 1, 2)
    let chain = 1 + 2 + 3
    let power = 2 ** 3 ** 4
    let full = ..
    let to_range = ..10
    let from_range = 10..
    let exclusive = 0..1
    let inclusive = 0..=1
    let tuple_value = (1, 2)
    let singleton_tuple = (1;)
    let second = 2
    let record_value = Pair{ first: 1, second }
    let transmuted = transmute<i32, i32>(1)
    let split_transmuted = transmute<Pair<i32, i32>, Pair<i32, i32>>(record_value)
    let matched = if 1 is {
        0 { 0 }
        1 { 1 }
        else { 2 }
    }
    let ended = if 1 is {
        0 { 0 }
    }
    loop value in [1, 2] {
        break 0
    }
    loop true {
        break 0
    }
    return 0
}

public procedure requirePre(value: i32) -> i32 |: true {
    return value
}

public procedure requirePost(value: i32) -> i32 |: => true {
    return value
}

public procedure requireBoth(value: i32) -> i32 |: true => true {
    return value
}
)cursive";
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
  const std::string needle = "\tparse\t" + std::string(rule) + "\t";
  return conformance_text.find(needle) != std::string_view::npos;
}

}  // namespace

int main() {
  const std::filesystem::path work_root = CURSIVE_TEST_WORK_ROOT;
  const std::filesystem::path project_root = work_root / "parser_generics_fixture";
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path compile_log = project_root / "compile.log";
  const std::filesystem::path conformance_log =
      out_root / "logs" / "conformance" / "parser_generics.conformance.log";

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

  const std::string compile_command =
      CommandInDirectory(
          project_root,
          Quote(CURSIVE_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(CURSIVE_TEST_TARGET_PROFILE) + " --phase1-only " +
              Quote(project_root.generic_string()) +
              " --assembly parser_generics --out-dir " +
              Quote(out_root.generic_string()) +
              " --conformance parser_generics.conformance.log "
              "--build-progress off --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "fixture parse phase failed; see " << compile_log << "\n";
    return 1;
  }

  const auto conformance_text = ReadFile(conformance_log);
  if (!conformance_text.has_value()) {
    return 1;
  }

  for (std::string_view rule : {
           "Parse-GenericParams",
           "Parse-GenericParamsOpt-Yes",
           "Parse-TypeBoundsOpt-None",
           "Parse-TypeBoundsOpt-Yes",
           "Parse-TypeParamTail-Cons",
           "Parse-TypeParamTail-End",
           "Parse-TypeDefaultOpt-None",
           "Parse-TypeDefaultOpt-Yes",
           "Parse-ClassBoundList-Cons",
           "Parse-ClassBoundListTail-Cons",
           "Parse-ClassBoundListTail-End",
           "Parse-ClassBound",
           "Parse-ClassItemList-End",
           "Parse-AssocTypeOpt-None",
           "Parse-AssocTypeOpt-Yes",
           "Parse-AssocTypeDefaultOpt",
           "Parse-Func-Type",
           "Parse-ParamTypeListTail-Cons",
           "Parse-Closure-Type",
           "Parse-Closure-Type-Empty",
           "Parse-ClosureParamType-Grouped",
           "Parse-ClosureParamType-Plain",
           "Parse-ClosureParamTypeList-Empty",
           "Parse-ClosureParamTypeList-Cons",
           "Parse-ClosureParamTypeListTail-End",
           "Parse-ClosureParamTypeListTail-TrailingComma",
           "Parse-ClosureParamTypeListTail-Comma",
           "Parse-ClosureDepsOpt-None",
           "Parse-ClosureDepsOpt-Some",
           "Parse-Closure-Expr-Empty",
           "Parse-ClosureParam-MoveTyped",
           "Parse-ClosureParam-MoveUntyped",
           "Parse-ClosureParam-Typed",
           "Parse-ClosureParam-Untyped",
           "Parse-ClosureRetOpt-Some",
           "Parse-ClosureRetOpt-None",
           "Parse-ClosureBody-Expr",
           "ArgumentListParsingFamily",
           "Parse-ArgList-Empty",
           "Parse-ArgList-Cons",
           "Parse-ArgMoveOpt-Yes",
           "ParseRangeFamily",
           "Parse-Range-Full",
           "Parse-Range-To",
           "Parse-RangeTail-From",
           "Parse-RangeTail-Exclusive",
           "Parse-RangeTail-Inclusive",
           "ParseLeftChainFamily",
           "Parse-LeftChain-Cons",
           "ParsePowerFamily",
           "Parse-PowerTail-Cons",
           "ParseTransmuteExprFamily",
           "Parse-Transmute-Expr",
           "ConstructionListAndShorthandParsingFamily",
           "Parse-TupleExprElems-Many",
           "Parse-FieldInit-Explicit",
           "Parse-FieldInit-Shorthand",
           "ControlExpressionParsingRemainderFamily",
           "Parse-IfCases-Cons",
           "Parse-IfCasesTail-End",
           "Parse-IfCasesTail-Else",
           "Parse-IfCasesTail-Cons",
           "TryParsePatternIn-Ok",
           "TryParsePatternIn-Fail",
           "Parse-LoopTail-Iter",
           "Parse-LoopTail-Cond",
           "Parse-ExternBlock",
           "Parse-ExternAbiOpt-String",
           "Parse-ExternAbiOpt-Ident",
           "Parse-ExternAbiOpt-None",
           "Parse-EnsuresPredicate-Error",
           "Parse-EnsuresPredicate-NullResult",
	           "Parse-EnsuresPredicate-Plain",
	           "Parse-Derive-Target",
	           "Parse-DeriveTargetDecl",
	           "Parse-DeriveContractOpt-Yes",
	           "Parse-DeriveClause-Requires",
	           "Parse-DeriveClause-Emits",
	           "Parse-DeriveClauseList-Cons",
	           "Parse-DeriveClauseTail-Comma",
	           "Parse-DeriveClauseTail-End",
	           "Parse-CtExpr",
	           "Parse-CtCapRef",
	           "Parse-TypeLiteral",
	           "Parse-Quote-Raw",
	           "Parse-Quote-Type",
	           "Parse-Quote-Pattern",
	           "Parse-RefinementOpt-Yes",
	           "ParsePredicateExpr",
	           "ParseLoopInvariantOpt",
           "Parse-ContractClauseOpt-None",
           "Parse-ContractClauseOpt-Yes",
           "Parse-ContractBody-PostOnly",
           "Parse-ContractBody-PrePost",
           "Parse-ContractBody-PreOnly",
           "Parse-GenericArgs",
           "Parse-GenericArgsOpt-Yes",
           "Parse-PredicateClauseOpt-None",
           "Parse-PredicateClauseOpt-Yes",
           "Parse-PredicateReqList-Cons",
           "Parse-PredicateReqListTail-Cons",
           "Parse-PredicateReqListTail-TrailingTerminator",
       }) {
    if (!ContainsRule(*conformance_text, rule)) {
      std::cerr << "conformance trace did not record " << rule << "\n";
      return 1;
    }
  }

  return 0;
}
