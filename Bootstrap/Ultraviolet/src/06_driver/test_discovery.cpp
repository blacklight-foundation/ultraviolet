#include "06_driver/test_discovery.h"

#include <algorithm>
#include <system_error>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "00_core/symbols.h"
#include "02_source/attributes/attribute_registry.h"

namespace ultraviolet::driver {

namespace {

std::string NormalizeStringLiteral(std::string value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

const ast::Token* TokenArg(const ast::AttributeArg& arg) {
  return std::get_if<ast::Token>(&arg.value);
}

std::string FullyQualifiedProcedurePath(const ast::ModulePath& module_path,
                                        std::string_view procedure_name) {
  std::vector<std::string> components = module_path;
  components.emplace_back(procedure_name);
  return core::StringOfPath(components);
}

std::size_t SourceFileOrderFor(
    const std::string& file,
    std::unordered_map<std::string, std::size_t>& file_order,
    std::size_t& next_file_order) {
  const auto it = file_order.find(file);
  if (it != file_order.end()) {
    return it->second;
  }
  const std::size_t order = next_file_order++;
  file_order.emplace(file, order);
  return order;
}

SourceNativeTestDescriptor TestDescriptorFromProcedure(
    std::string_view assembly_name,
    const ast::ModulePath& module_path,
    const ast::ProcedureDecl& procedure,
    std::size_t source_file_order,
    std::size_t declaration_order) {
  SourceNativeTestDescriptor descriptor;
  descriptor.assembly_name = std::string(assembly_name);
  descriptor.module_path = module_path;
  descriptor.procedure_name = procedure.name;
  descriptor.stable_identity =
      FullyQualifiedProcedurePath(module_path, procedure.name);
  descriptor.display_name = descriptor.stable_identity;
  descriptor.span = procedure.span;
  descriptor.source_file = procedure.span.file;
  descriptor.source_file_order = source_file_order;
  descriptor.requires_context = procedure.params.size() == 1;
  descriptor.declaration_order = declaration_order;

  for (const auto& attr : procedure.attrs) {
    if (attr.name != analysis::attrs::kTest) {
      continue;
    }
    for (const auto& arg : attr.args) {
      if (arg.key.has_value() && *arg.key == "name") {
        if (const auto* token = TokenArg(arg)) {
          descriptor.display_name = NormalizeStringLiteral(token->lexeme);
        }
        continue;
      }
      if (arg.key.has_value() && *arg.key == "covers") {
        const auto* nested =
            std::get_if<std::vector<ast::AttributeArg>>(&arg.value);
        if (nested && nested->size() == 1) {
          if (const auto* token = TokenArg((*nested)[0])) {
            descriptor.coverage_references.push_back(
                NormalizeStringLiteral(token->lexeme));
          }
        }
      }
    }
  }

  return descriptor;
}

bool TestDiscoveryLess(const SourceNativeTestDescriptor& lhs,
                       const SourceNativeTestDescriptor& rhs) {
  if (lhs.module_path != rhs.module_path) {
    return lhs.module_path < rhs.module_path;
  }
  if (lhs.source_file_order != rhs.source_file_order) {
    return lhs.source_file_order < rhs.source_file_order;
  }
  if (lhs.declaration_order != rhs.declaration_order) {
    return lhs.declaration_order < rhs.declaration_order;
  }
  if (lhs.span.start_offset != rhs.span.start_offset) {
    return lhs.span.start_offset < rhs.span.start_offset;
  }
  return lhs.stable_identity < rhs.stable_identity;
}

bool ModulePathStartsWith(const ast::ModulePath& path,
                          const ast::ModulePath& prefix) {
  return path.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), path.begin());
}

bool ModulePathIsTestsSubtree(std::string_view assembly_name,
                              const ast::ModulePath& module_path) {
  return module_path.size() >= 2 && module_path[0] == assembly_name &&
         module_path[1] == "Tests";
}

ast::ModulePath ParseModulePath(std::string_view text) {
  ast::ModulePath path;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t pos = text.find("::", start);
    if (pos == std::string_view::npos) {
      path.emplace_back(text.substr(start));
      break;
    }
    path.emplace_back(text.substr(start, pos - start));
    start = pos + 2;
  }
  return path;
}

bool ModuleListContainsPath(const project::Assembly& assembly,
                            std::string_view module_path) {
  for (const auto& module : assembly.modules) {
    if (module.path == module_path) {
      return true;
    }
  }
  return false;
}

bool PathIsUnderDirectory(const std::filesystem::path& source_file,
                          const std::filesystem::path& directory) {
  std::error_code source_ec;
  std::error_code dir_ec;
  const std::filesystem::path source_abs =
      std::filesystem::weakly_canonical(source_file, source_ec);
  const std::filesystem::path dir_abs =
      std::filesystem::weakly_canonical(directory, dir_ec);
  if (source_ec || dir_ec) {
    return false;
  }
  const auto source_text = source_abs.generic_string();
  const auto dir_text = dir_abs.generic_string();
  if (source_text.size() < dir_text.size()) {
    return false;
  }
  if (source_text.compare(0, dir_text.size(), dir_text) != 0) {
    return false;
  }
  return source_text.size() == dir_text.size() ||
         source_text[dir_text.size()] == '/';
}

std::filesystem::path ResolveTargetPath(
    const std::filesystem::path& current_directory,
    const std::string& target) {
  std::filesystem::path path(target);
  if (path.is_relative()) {
    path = current_directory / path;
  }
  return path.lexically_normal();
}

}  // namespace

SourceNativeTestDiscoveryResult DiscoverSourceNativeTests(
    std::string_view assembly_name,
    const std::vector<ast::ASTModule>& modules) {
  SourceNativeTestDiscoveryResult result;
  std::size_t declaration_order = 0;
  std::size_t next_file_order = 0;
  std::unordered_map<std::string, std::size_t> file_order;

  for (const auto& module : modules) {
    for (const auto& item : module.items) {
      ++declaration_order;
      const auto* procedure = std::get_if<ast::ProcedureDecl>(&item);
      if (!procedure ||
          !analysis::HasAttribute(procedure->attrs, analysis::attrs::kTest)) {
        continue;
      }
      const std::size_t source_file_order = SourceFileOrderFor(
          procedure->span.file, file_order, next_file_order);
      result.tests.push_back(TestDescriptorFromProcedure(
          assembly_name, module.path, *procedure, source_file_order,
          declaration_order));
    }
  }

  std::sort(result.tests.begin(), result.tests.end(), TestDiscoveryLess);
  return result;
}

SourceNativeTestTargetResolution ResolveSourceNativeTestTarget(
    const project::Project& project,
    const std::filesystem::path& current_directory,
    const std::optional<std::string>& target) {
  if (!target.has_value()) {
    return SourceNativeTestTargetResolution{
        SourceNativeTestScope{SourceNativeTestScopeKind::AllTests}, {}};
  }

  const std::filesystem::path target_path =
      ResolveTargetPath(current_directory, *target);
  std::error_code ec;
  const bool exists = std::filesystem::exists(target_path, ec);
  if (exists && !ec) {
    if (std::filesystem::is_regular_file(target_path, ec) && !ec) {
      SourceNativeTestScope scope;
      scope.kind = SourceNativeTestScopeKind::SourceFileTests;
      scope.path = std::filesystem::weakly_canonical(target_path, ec);
      if (ec) {
        scope.path = target_path;
      }
      return SourceNativeTestTargetResolution{std::move(scope), {}};
    }
    ec.clear();
    if (std::filesystem::is_directory(target_path, ec) && !ec) {
      std::error_code target_ec;
      std::error_code root_ec;
      const auto canonical_target =
          std::filesystem::weakly_canonical(target_path, target_ec);
      const auto canonical_root =
          std::filesystem::weakly_canonical(project.root, root_ec);
      if (!target_ec && !root_ec && canonical_target == canonical_root) {
        return SourceNativeTestTargetResolution{
            SourceNativeTestScope{SourceNativeTestScopeKind::AllTests}, {}};
      }
      SourceNativeTestScope scope;
      scope.kind = SourceNativeTestScopeKind::DirectoryTests;
      scope.path = target_ec ? target_path : canonical_target;
      return SourceNativeTestTargetResolution{std::move(scope), {}};
    }
  }

  for (const auto& assembly : project.assemblies) {
    if (assembly.name == *target) {
      SourceNativeTestScope scope;
      scope.kind = SourceNativeTestScopeKind::AssemblyTests;
      scope.assembly_name = assembly.name;
      return SourceNativeTestTargetResolution{std::move(scope), {}};
    }
    if (ModuleListContainsPath(assembly, *target)) {
      SourceNativeTestScope scope;
      scope.kind = SourceNativeTestScopeKind::ModuleTests;
      scope.module_path = ParseModulePath(*target);
      return SourceNativeTestTargetResolution{std::move(scope), {}};
    }
  }

  return SourceNativeTestTargetResolution{std::nullopt, *target};
}

std::vector<SourceNativeTestDescriptor> SelectSourceNativeTests(
    const project::Project& project,
    const SourceNativeTestScope& scope,
    const std::vector<SourceNativeTestDescriptor>& tests) {
  std::vector<SourceNativeTestDescriptor> selected;
  for (const auto& test : tests) {
    if (!ModulePathIsTestsSubtree(test.assembly_name, test.module_path)) {
      continue;
    }

    bool include = false;
    switch (scope.kind) {
      case SourceNativeTestScopeKind::AllTests:
        include = true;
        break;
      case SourceNativeTestScopeKind::AssemblyTests:
        include = test.assembly_name == scope.assembly_name;
        break;
      case SourceNativeTestScopeKind::ModuleTests:
        include = ModulePathStartsWith(test.module_path, scope.module_path);
        break;
      case SourceNativeTestScopeKind::SourceFileTests:
        {
          std::error_code ec;
          const auto source_file =
              std::filesystem::weakly_canonical(test.source_file, ec);
          include = !ec && source_file == scope.path;
        }
        break;
      case SourceNativeTestScopeKind::DirectoryTests:
        include = PathIsUnderDirectory(test.source_file, scope.path);
        break;
    }

    if (include) {
      selected.push_back(test);
    }
  }

  (void)project;
  std::sort(selected.begin(), selected.end(), TestDiscoveryLess);
  return selected;
}

}  // namespace ultraviolet::driver
