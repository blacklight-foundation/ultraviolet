#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

struct FixtureResult {
  int exit_code = 1;
  std::filesystem::path project_root;
  std::filesystem::path compile_log;
  std::filesystem::path conformance_log;
};

FixtureResult RunPhase1Fixture(std::string_view name, std::string_view source) {
  const std::filesystem::path project_root =
      std::filesystem::path(UV_TEST_WORK_ROOT) / std::string(name);
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path compile_log = project_root / "compile.log";
  const std::filesystem::path conformance_log =
      out_root / "logs" / "conformance" / (std::string(name) + ".log");

  std::error_code ec;
  std::filesystem::remove_all(project_root, ec);
  if (ec) {
    std::cerr << "failed to remove old fixture: " << ec.message() << "\n";
    return {1, project_root, compile_log, conformance_log};
  }
  std::filesystem::create_directories(source_root, ec);
  if (ec) {
    std::cerr << "failed to create fixture directories: " << ec.message()
              << "\n";
    return {1, project_root, compile_log, conformance_log};
  }

  if (!WriteFile(project_root / "Ultraviolet.toml", FixtureManifest(name)) ||
      !WriteFile(source_root / "Main.uv", source)) {
    return {1, project_root, compile_log, conformance_log};
  }

  const std::string command =
      CommandInDirectory(
          project_root,
          Quote(UV_TEST_COMPILER_PATH) + " --target-profile " +
              Quote(UV_TEST_TARGET_PROFILE) + " --phase1-only " +
              Quote(project_root.generic_string()) + " --assembly " +
              std::string(name) + " --out-dir " +
              Quote(out_root.generic_string()) + " --conformance " +
              std::string(name) + ".log --build-progress off "
              "--incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  return {RunCommand(command), project_root, compile_log, conformance_log};
}

bool ContainsRule(std::string_view conformance_text, std::string_view rule) {
  const std::string needle = "\tparse\t" + std::string(rule) + "\t";
  return conformance_text.find(needle) != std::string_view::npos;
}

bool RequireRules(std::string_view conformance_text,
                  const std::vector<std::string_view>& rules) {
  for (std::string_view rule : rules) {
    if (!ContainsRule(conformance_text, rule)) {
      std::cerr << "conformance trace did not record " << rule << "\n";
      return false;
    }
  }
  return true;
}

bool ExpectAccepted(std::string_view name,
                    std::string_view source,
                    const std::vector<std::string_view>& rules) {
  const FixtureResult result = RunPhase1Fixture(name, source);
  if (result.exit_code != 0) {
    std::cerr << "accepted grammar fixture failed; see " << result.compile_log
              << "\n";
    return false;
  }

  const auto conformance_text = ReadFile(result.conformance_log);
  if (!conformance_text.has_value()) {
    return false;
  }
  return RequireRules(*conformance_text, rules);
}

bool ExpectRejected(std::string_view name, std::string_view source) {
  const FixtureResult result = RunPhase1Fixture(name, source);
  if (result.exit_code == 0) {
    std::cerr << "rejected grammar fixture compiled: " << name << "\n";
    std::cerr << "see " << result.compile_log << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  constexpr std::string_view accepted_source =
      R"uv(#layout(C)
public record GrammarCorrectionRecord {
    public value: i32
}

#grammar::empty()
public procedure emptyAttributeSyntax() -> i32 {
    return 0
}

#test(name: "grammar corrections", covers("grammar.AttributeSyntax@L1"))
public procedure testedProcedure() -> bool |: |= @result {
    return true
}

public procedure identity(value: i32) -> i32 {
    return value
}

public procedure contractPreOnly(value: i32) -> i32 |: value >= 0 {
    return value
}

public procedure contractPrePost(value: i32) -> i32 |: value >= 0 |= true {
    return value
}

public procedure correctedKeyBlocks(value: i32, other: i32, index: usize) -> i32 {
    var total = value
    #dynamic
    #seqcst
    %write value, other [ordered] {
        total = total + 1
    }
    %read value [ordered] {
        total = total + 1
    }
    %release read value {
        total = total + 1
    }
    %release write other {
        total = total + 1
    }
    %speculative write other {
        total = total + 1
    }
    let piped = total => identity
    return piped
}

public procedure attributedComptime() -> i32 {
    let projected = #files comptime { 1 }
    #emit
    comptime {
        let generated = projected
    }
    return projected
}
)uv";

  constexpr std::string_view old_attribute_source =
      R"uv([[dynamic]]
public procedure oldAttribute() -> i32 {
    return 0
}
)uv";

  constexpr std::string_view old_grouped_attribute_source =
      R"uv([[inline(default), cold]]
public procedure groupedAttribute() -> i32 {
    return 0
}
)uv";

  constexpr std::string_view misplaced_ordered_source =
      R"uv(public procedure badOrdered(value: i32) -> i32 {
    %write [ordered] value {
        return value
    }
}
)uv";

  constexpr std::string_view speculative_read_source =
      R"uv(public procedure badSpeculative(value: i32) -> i32 {
    %speculative read value {
        return value
    }
}
)uv";

  if (!ExpectAccepted(
          "grammar_corrections_accepted",
          accepted_source,
          {
              "Parse-AttrListOpt-Yes",
              "Parse-Attribute",
              "Parse-AttrArgsOpt-Empty",
              "Parse-AttrArgsOpt-Yes",
              "Parse-ContractBody-PostOnly",
              "Parse-ContractBody-PreOnly",
              "Parse-ContractBody-PrePost",
              "Parse-KeyBlockHead-Read",
              "Parse-KeyBlockHead-Write",
              "Parse-KeyBlockHead-Release",
              "Parse-KeyBlockHead-SpeculativeWrite",
              "Parse-KeyOption-Ordered",
              "Parse-PipelineTail-Cons",
          })) {
    return 1;
  }

  if (!ExpectRejected("grammar_corrections_old_attribute",
                      old_attribute_source)) {
    return 1;
  }
  if (!ExpectRejected("grammar_corrections_old_grouped_attribute",
                      old_grouped_attribute_source)) {
    return 1;
  }
  if (!ExpectRejected("grammar_corrections_misplaced_ordered",
                      misplaced_ordered_source)) {
    return 1;
  }
  if (!ExpectRejected("grammar_corrections_speculative_read",
                      speculative_read_source)) {
    return 1;
  }

  return 0;
}
