#include "06_driver/shared_library_exports.h"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <vector>

#include "00_core/symbols.h"
#include "01_project/assemblies.h"
#include "01_project/deterministic_order.h"
#include "01_project/language_profile.h"
#include "01_project/link.h"
#include "05_codegen/globals/globals.h"
#include "06_driver/pipeline.h"

namespace ultraviolet::driver {

namespace {

void SortStringsDeterministically(std::vector<std::string>& values) {
  std::sort(values.begin(), values.end(),
            [](const std::string& lhs, const std::string& rhs) {
              return project::Utf8LexLess(lhs, rhs);
            });
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::vector<std::string> ComputeSharedLibraryExportSymbols(
    const project::Project& project,
    const CodegenCache& cache) {
  if (!project::IsSharedLibrary(project)) {
    return {};
  }

  std::unordered_set<std::string> project_modules;
  project_modules.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    project_modules.insert(module.path);
  }

  std::unordered_set<std::string> exported_symbols;
  std::unordered_set<std::string> hosted_internal_symbols;
  std::vector<codegen::LowerCtx::HostedExportInfo> hosted_exports;
  hosted_exports.reserve(cache.all_hosted_exports.size());
  for (const auto& hosted : cache.all_hosted_exports) {
    const auto* owner = cache.ctx.LookupProcModule(hosted.internal_symbol);
    if (!owner || owner->empty()) {
      continue;
    }
    if (project_modules.find(core::StringOfPath(*owner)) ==
        project_modules.end()) {
      continue;
    }
    hosted_exports.push_back(hosted);
  }

  hosted_internal_symbols.reserve(hosted_exports.size());
  for (const auto& hosted : hosted_exports) {
    hosted_internal_symbols.insert(hosted.internal_symbol);
  }
  for (const auto& [symbol, linkage] : cache.ctx.AllProcLinkages()) {
    if (linkage != codegen::LinkageKind::External ||
        project::IsHiddenSharedLibraryExportSymbol(symbol)) {
      continue;
    }
    if (cache.ctx.LookupProcVisibility(symbol) !=
        std::optional<ast::Visibility>{ast::Visibility::Public}) {
      continue;
    }
    const auto* owner = cache.ctx.LookupProcModule(symbol);
    if (!owner || owner->empty()) {
      continue;
    }
    if (project_modules.find(core::StringOfPath(*owner)) ==
            project_modules.end() ||
        hosted_internal_symbols.find(symbol) != hosted_internal_symbols.end()) {
      continue;
    }
    exported_symbols.insert(symbol);
  }

  for (const auto& module_info : project.modules) {
    const auto module_it = cache.ast_modules.find(module_info.path);
    if (module_it == cache.ast_modules.end() || module_it->second == nullptr) {
      continue;
    }
    const auto& module = *module_it->second;
    for (const auto& item : module.items) {
      const auto* decl = std::get_if<ast::StaticDecl>(&item);
      if (!decl || decl->vis != ast::Visibility::Public) {
        continue;
      }
      for (const auto& name : codegen::StaticBindList(decl->binding)) {
        exported_symbols.insert(
            codegen::StaticSym(*decl, module.path, name));
      }
    }
  }

  for (const auto& hosted : hosted_exports) {
    exported_symbols.insert(hosted.thunk_symbol);
  }

  if (cache.ctx.hosted_library && !hosted_exports.empty()) {
    const auto& language = project::ActiveLanguageProfile();
    exported_symbols.insert(std::string(language.host_abi_version_symbol));
    exported_symbols.insert(std::string(language.host_session_create_symbol));
    exported_symbols.insert(std::string(language.host_session_destroy_symbol));
  }

  std::vector<std::string> out(exported_symbols.begin(), exported_symbols.end());
  SortStringsDeterministically(out);
  return out;
}

std::vector<std::string> ComputeSharedLibraryDataExportSymbols(
    const project::Project& project,
    const CodegenCache& cache) {
  if (!project::IsSharedLibrary(project)) {
    return {};
  }

  std::unordered_set<std::string> exported_symbols;
  const auto& language = project::ActiveLanguageProfile();
  for (const auto& module_info : project.modules) {
    exported_symbols.insert(
        core::Mangle(std::string(language.runtime_root) + "::runtime::poison::" +
                     module_info.path));

    const auto module_it = cache.ast_modules.find(module_info.path);
    if (module_it == cache.ast_modules.end() || module_it->second == nullptr) {
      continue;
    }
    const auto& module = *module_it->second;
    for (const auto& item : module.items) {
      const auto* decl = std::get_if<ast::StaticDecl>(&item);
      if (!decl || decl->vis != ast::Visibility::Public) {
        continue;
      }
      for (const auto& name : codegen::StaticBindList(decl->binding)) {
        exported_symbols.insert(
            codegen::StaticSym(*decl, module.path, name));
      }
    }
  }

  std::vector<std::string> out(exported_symbols.begin(), exported_symbols.end());
  SortStringsDeterministically(out);
  return out;
}

}  // namespace

std::optional<SharedLibraryExports> ResolveSharedLibraryExports(
    const project::Project& project,
    const CodegenCache& cache) {
  if (!project::IsSharedLibrary(project)) {
    return SharedLibraryExports{};
  }

  SharedLibraryExports exports;
  if (cache.ctx.shared_library_export_symbols.empty()) {
    exports.export_symbols = ComputeSharedLibraryExportSymbols(project, cache);
  } else {
    exports.export_symbols = cache.ctx.shared_library_export_symbols;
  }
  exports.data_export_symbols =
      ComputeSharedLibraryDataExportSymbols(project, cache);
  if (!exports.data_export_symbols.empty()) {
    std::unordered_set<std::string> data_symbols(
        exports.data_export_symbols.begin(), exports.data_export_symbols.end());
    exports.export_symbols.erase(
        std::remove_if(
            exports.export_symbols.begin(),
            exports.export_symbols.end(),
            [&](const std::string& symbol) {
              return data_symbols.find(symbol) != data_symbols.end();
            }),
        exports.export_symbols.end());
    SortStringsDeterministically(exports.export_symbols);
  }
  return exports;
}

bool PrepareSharedLibraryCodegenContext(
    const project::Project& project,
    CodegenCache& cache,
    const SharedLibraryExports& exports) {
  ConfigureCodegenContextForProject(cache, project);
  if (!project::IsSharedLibrary(project)) {
    cache.ctx.shared_library_export_symbols.clear();
    return true;
  }
  cache.ctx.shared_library_export_symbols = exports.export_symbols;
  return true;
}

}  // namespace ultraviolet::driver
