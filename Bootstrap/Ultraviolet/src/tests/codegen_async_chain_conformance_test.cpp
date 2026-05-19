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
#ifdef _WIN32
  std::string out = "\"";
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  out.push_back('"');
  return out;
#else
  std::string out = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('\'');
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
  const std::size_t next = module_ir.find("\ndefine ", start + signature.size());
  const std::size_t end = next == std::string_view::npos ? module_ir.size() : next;
  return module_ir.substr(start, end - start);
}

std::optional<std::string_view> SegmentBetween(
    std::string_view text,
    std::string_view first,
    std::string_view second) {
  const std::size_t first_pos = text.find(first);
  if (first_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t second_pos = text.find(second, first_pos + first.size());
  if (second_pos == std::string_view::npos) {
    return std::nullopt;
  }
  return text.substr(first_pos, second_pos - first_pos);
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
  return "set \"PATH=" + tool_root.generic_string() + ";%PATH%\" && " + command;
#else
  return "PATH=" + Quote(tool_root.generic_string()) + ":$PATH " + command;
#endif
}

std::string FixtureManifest() {
  std::ostringstream out;
  out << "[toolchain]\n";
  out << "runtime_lib = \"" << UV_TEST_RUNTIME_LIB_PATH << "\"\n";
  out << "target_profile = \"" << UV_TEST_TARGET_PROFILE << "\"\n\n";
  out << "[[assembly]]\n";
  out << "name = \"async_chain\"\n";
  out << "kind = \"executable\"\n";
  out << "root = \"src\"\n";
  out << "out_dir = \"build/async_chain\"\n";
  out << "emit_ir = \"ll\"\n";
  return out.str();
}

std::string FixtureSource() {
  return R"uv(internal procedure complete(value: i32) -> Async<(), (), i32, bool> {
    return value
}

internal procedure i32BoolUnionIsValue(value: i32 | bool, expected: i32) -> bool {
    return if value is {
        actual: i32 {
            actual == expected
        }
        failed: bool {
            false
        }
    }
}

public procedure chainReference() -> bool {
    let chained: Async<(), (), i32, bool> =
        complete(7)~>chain(
            |value: i32| -> Async<(), (), i32, bool> complete(value + 1)
        )
    let result: i32 | bool = sync chained
    return i32BoolUnionIsValue(result, 8)
}

public procedure main(move ctx: Context) -> i32 {
    let _ = ctx
    if chainReference() {
        return 0
    }
    return 1
}
)uv";
}

}  // namespace

int main() {
  const std::filesystem::path work_root = UV_TEST_WORK_ROOT;
  const std::filesystem::path project_root = work_root / "async_chain_fixture";
  const std::filesystem::path source_root = project_root / "src";
  const std::filesystem::path out_root = project_root / "out";
  const std::filesystem::path tool_root = project_root / "tools";
  const std::filesystem::path compile_log = project_root / "compile.log";

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
              " --assembly async_chain --out-dir " +
              Quote(out_root.generic_string()) +
              " --build-progress off --incremental off > " +
              Quote(compile_log.generic_string()) + " 2>&1");
  const int compile_result = RunCommand(compile_command);
  if (compile_result != 0) {
    std::cerr << "async chain fixture compile failed; see "
              << compile_log << "\n";
    return 1;
  }

  const auto ir_text = ReadFile(out_root / "ir" / "async_x5fchain.ll");
  if (!ir_text.has_value()) {
    return 1;
  }
  const auto chain_body =
      FunctionBody(*ir_text, "@async_x5fchain_x3a_x3achainReference");
  if (!chain_body.has_value()) {
    std::cerr << "chainReference was not emitted in LLVM IR\n";
    return 1;
  }
  const auto completed_segment =
      SegmentBetween(*chain_body, "ac.chain.completed:", "ac.chain.merge:");
  if (!completed_segment.has_value()) {
    std::cerr << "async chain completed branch was not emitted\n";
    return 1;
  }
  if (completed_segment->find("x5fclosure0") == std::string_view::npos) {
    std::cerr << "async chain completed branch did not invoke the continuation\n";
    return 1;
  }

  return 0;
}
