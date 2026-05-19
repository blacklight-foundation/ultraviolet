#include "06_driver/test_discovery.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "02_source/ast/ast.h"
#include "02_source/lexer/token.h"

namespace {

ultraviolet::core::Span Span(std::string file, std::size_t start) {
  ultraviolet::core::Span span;
  span.file = std::move(file);
  span.start_offset = start;
  span.end_offset = start + 1;
  return span;
}

ultraviolet::ast::AttrName AttrName(std::string name) {
  ultraviolet::ast::AttrName attr_name;
  attr_name.leaf_name = name;
  attr_name.full_name = name;
  return attr_name;
}

ultraviolet::ast::Token StringToken(std::string value) {
  ultraviolet::ast::Token token;
  token.kind = ultraviolet::lexer::TokenKind::StringLiteral;
  token.lexeme = std::move(value);
  return token;
}

ultraviolet::ast::AttributeArg NamedTokenArg(std::string name,
                                             std::string value) {
  ultraviolet::ast::AttributeArg arg;
  arg.key = std::move(name);
  arg.value = StringToken(std::move(value));
  return arg;
}

ultraviolet::ast::AttributeArg CoversArg(std::string value) {
  ultraviolet::ast::AttributeArg nested;
  nested.value = StringToken(std::move(value));

  ultraviolet::ast::AttributeArg arg;
  arg.key = std::string("covers");
  arg.value = std::vector<ultraviolet::ast::AttributeArg>{std::move(nested)};
  return arg;
}

ultraviolet::ast::AttributeItem TestAttribute(
    std::vector<ultraviolet::ast::AttributeArg> args = {}) {
  ultraviolet::ast::AttributeItem attr;
  attr.name = AttrName("test");
  attr.args = std::move(args);
  return attr;
}

ultraviolet::ast::ProcedureDecl Procedure(
    std::string name,
    ultraviolet::core::Span span,
    std::vector<ultraviolet::ast::AttributeArg> test_args = {},
    bool requires_context = false) {
  ultraviolet::ast::ProcedureDecl procedure;
  procedure.attrs = {TestAttribute(std::move(test_args))};
  procedure.name = std::move(name);
  procedure.span = std::move(span);
  if (requires_context) {
    ultraviolet::ast::Param param;
    param.name = "authority";
    procedure.params.push_back(std::move(param));
  }
  return procedure;
}

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    return false;
  }
  return true;
}

std::filesystem::path FreshTempDir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("uv_test_discovery_conformance_" + std::to_string(stamp));
}

}  // namespace

int main() {
  const std::filesystem::path root = FreshTempDir();
  const std::filesystem::path source_dir = root / "Source";
  std::filesystem::create_directories(source_dir);
  const std::filesystem::path alpha_file = source_dir / "Alpha.uv";
  const std::filesystem::path beta_file = source_dir / "Beta.uv";
  {
    std::ofstream(alpha_file) << "// alpha\n";
    std::ofstream(beta_file) << "// beta\n";
  }

  ultraviolet::ast::ASTModule alpha;
  alpha.path = {"Example", "Tests", "Alpha"};
  alpha.items.push_back(Procedure(
      "secondInFile",
      Span(alpha_file.string(), 30),
      {NamedTokenArg("name", "\"display label\""),
       CoversArg("\"req.9.SourceNativeTestAttributes@L100604\"")}));
  alpha.items.push_back(Procedure("thirdInFile", Span(alpha_file.string(), 60)));

  ultraviolet::ast::ASTModule beta;
  beta.path = {"Example", "Tests", "Beta"};
  beta.items.push_back(Procedure(
      "betaTest", Span(beta_file.string(), 10), {}, true));

  const auto discovery = ultraviolet::driver::DiscoverSourceNativeTests(
      "Example", {beta, alpha});

  ultraviolet::project::Assembly assembly;
  assembly.name = "Example";
  assembly.source_root = source_dir;
  assembly.modules = {
      ultraviolet::project::ModuleInfo{"Example::Tests::Alpha", source_dir},
      ultraviolet::project::ModuleInfo{"Example::Tests::Beta", source_dir},
  };
  ultraviolet::project::Project project;
  project.root = root;
  project.assemblies = {assembly};

  bool ok = true;
  ok &= Expect(discovery.tests.size() == 3,
               "expected all source-native tests to be discovered");
  ok &= Expect(discovery.tests[0].stable_identity ==
                   "Example::Tests::Alpha::secondInFile",
               "expected module-path discovery order before input order");
  ok &= Expect(discovery.tests[1].stable_identity ==
                   "Example::Tests::Alpha::thirdInFile",
               "expected declaration order inside one module");
  ok &= Expect(discovery.tests[2].stable_identity ==
                   "Example::Tests::Beta::betaTest",
               "expected later module path after Alpha");
  ok &= Expect(discovery.tests[0].display_name == "display label",
               "expected name argument to be a display label");
  ok &= Expect(discovery.tests[0].coverage_references.size() == 1,
               "expected coverage reference extraction");
  ok &= Expect(discovery.tests[0].coverage_references[0] ==
                   "req.9.SourceNativeTestAttributes@L100604",
               "expected normalized coverage reference text");
  ok &= Expect(discovery.tests[0].procedure_name == "secondInFile",
               "expected procedure name to stay separate from display label");
  ok &= Expect(!discovery.tests[0].requires_context,
               "expected parameterless test to require no TestAuthority");
  ok &= Expect(discovery.tests[2].requires_context,
               "expected one-parameter test to require TestAuthority");

  const auto absent_resolution =
      ultraviolet::driver::ResolveSourceNativeTestTarget(
          project, root, std::nullopt);
  ok &= Expect(absent_resolution.scope.has_value(),
               "expected absent uv test target to resolve");
  ok &= Expect(absent_resolution.scope->kind ==
                   ultraviolet::driver::SourceNativeTestScopeKind::AllTests,
               "expected absent target to select all tests");
  ok &= Expect(ultraviolet::driver::SelectSourceNativeTests(
                   project, *absent_resolution.scope, discovery.tests).size() == 3,
               "expected all target to select every tests-subtree procedure");

  const auto assembly_resolution =
      ultraviolet::driver::ResolveSourceNativeTestTarget(
          project, root, std::string("Example"));
  ok &= Expect(assembly_resolution.scope.has_value(),
               "expected assembly uv test target to resolve");
  ok &= Expect(ultraviolet::driver::SelectSourceNativeTests(
                   project, *assembly_resolution.scope, discovery.tests).size() == 3,
               "expected assembly target to select assembly tests");

  const auto module_resolution =
      ultraviolet::driver::ResolveSourceNativeTestTarget(
          project, root, std::string("Example::Tests::Alpha"));
  ok &= Expect(module_resolution.scope.has_value(),
               "expected module uv test target to resolve");
  ok &= Expect(ultraviolet::driver::SelectSourceNativeTests(
                   project, *module_resolution.scope, discovery.tests).size() == 2,
               "expected module target to select matching module tests");

  const auto file_resolution =
      ultraviolet::driver::ResolveSourceNativeTestTarget(
          project, root, alpha_file.string());
  ok &= Expect(file_resolution.scope.has_value(),
               "expected source-file uv test target to resolve");
  ok &= Expect(ultraviolet::driver::SelectSourceNativeTests(
                   project, *file_resolution.scope, discovery.tests).size() == 2,
               "expected source-file target to select exact-file tests");

  const auto dir_resolution =
      ultraviolet::driver::ResolveSourceNativeTestTarget(
          project, root, source_dir.string());
  ok &= Expect(dir_resolution.scope.has_value(),
               "expected directory uv test target to resolve");
  ok &= Expect(ultraviolet::driver::SelectSourceNativeTests(
                   project, *dir_resolution.scope, discovery.tests).size() == 3,
               "expected directory target to select tests under that directory");

  ultraviolet::driver::SourceNativeTestFilter procedure_filter;
  procedure_filter.test_name = "thirdInFile";
  const auto procedure_selected =
      ultraviolet::driver::FilterSourceNativeTests(
          discovery.tests, procedure_filter);
  ok &= Expect(procedure_selected.size() == 1 &&
                   procedure_selected[0].procedure_name == "thirdInFile",
               "expected procedure-name test filter to select one test");

  ultraviolet::driver::SourceNativeTestFilter display_filter;
  display_filter.test_name = "display label";
  const auto display_selected =
      ultraviolet::driver::FilterSourceNativeTests(
          discovery.tests, display_filter);
  ok &= Expect(display_selected.size() == 1 &&
                   display_selected[0].display_name == "display label",
               "expected display-name test filter to select one test");

  ultraviolet::driver::SourceNativeTestFilter coverage_filter;
  coverage_filter.coverage_reference =
      "req.9.SourceNativeTestAttributes@L100604";
  const auto coverage_selected =
      ultraviolet::driver::FilterSourceNativeTests(
          discovery.tests, coverage_filter);
  ok &= Expect(coverage_selected.size() == 1 &&
                   coverage_selected[0].procedure_name == "secondInFile",
               "expected coverage filter to select one covered test");

  const auto unknown_resolution =
      ultraviolet::driver::ResolveSourceNativeTestTarget(
          project, root, std::string("DoesNotExist"));
  ok &= Expect(!unknown_resolution.scope.has_value(),
               "expected unknown uv test target to reject");
  ok &= Expect(unknown_resolution.unknown_target == "DoesNotExist",
               "expected unknown target text to be preserved");

  std::error_code cleanup_ec;
  std::filesystem::remove_all(root, cleanup_ec);
  return ok ? 0 : 1;
}
