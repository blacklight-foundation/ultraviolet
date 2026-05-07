#include "04_analysis/resolve/assembly_import_graph.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/symbols.h"
#include "01_project/assemblies.h"
#include "01_project/deterministic_order.h"
#include "01_project/outputs.h"
#include "02_source/module_paths.h"
#include "02_source/attributes/attribute_registry.h"

namespace cursive::analysis {

using project::Assembly;
using project::AssemblyProject;
using project::Fold;
using project::IsDependency;
using project::IsExecutable;
using project::IsLibrary;
using project::IsLinkable;
using project::ModuleInfo;
using project::Project;
using project::Utf8LexLess;

namespace {

void SortStringsDeterministically(std::vector<std::string>& values) {
  std::stable_sort(values.begin(), values.end(),
                   [](const std::string& lhs, const std::string& rhs) {
                     return Utf8LexLess(lhs, rhs);
                   });
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

void SortModulesDeterministically(std::vector<ModuleInfo>& modules) {
  std::stable_sort(modules.begin(), modules.end(),
                   [](const ModuleInfo& lhs, const ModuleInfo& rhs) {
                     const std::string lhs_fold = Fold(lhs.path);
                     const std::string rhs_fold = Fold(rhs.path);
                     if (lhs_fold == rhs_fold) {
                       return Utf8LexLess(lhs.path, rhs.path);
                     }
                     return Utf8LexLess(lhs_fold, rhs_fold);
                   });
}

source::ModuleNames ModuleNamesForAssemblies(
    const std::vector<Assembly>& assemblies) {
  source::ModuleNames names;
  std::size_t total = 0;
  for (const auto& assembly : assemblies) {
    total += assembly.modules.size();
  }
  names.reserve(total);
  for (const auto& assembly : assemblies) {
    for (const auto& module : assembly.modules) {
      names.insert(module.path);
    }
  }
  return names;
}

std::unordered_map<std::string, std::string> ModuleOwnerMapForAssemblies(
    const std::vector<Assembly>& assemblies) {
  std::unordered_map<std::string, std::string> owners;
  for (const auto& assembly : assemblies) {
    for (const auto& module : assembly.modules) {
      owners.emplace(module.path, assembly.name);
    }
  }
  return owners;
}

std::optional<std::string> ResolveImportedAssemblyName(
    const ast::ImportDecl& import,
    const ast::ModulePath& current_module,
    const source::ModuleNames& module_names,
    const std::unordered_map<std::string, std::string>& module_owner) {
  const auto resolved =
      source::ResolveImportModulePath(current_module, module_names, import.path);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  const auto owner_it = module_owner.find(core::StringOfPath(*resolved));
  if (owner_it == module_owner.end()) {
    return std::nullopt;
  }
  return owner_it->second;
}

bool EmitAssemblyGraphDiag(core::DiagnosticStream& diags,
                           const std::string& note) {
  if (auto diag = core::MakeExternalDiagnostic("E-PRJ-0209")) {
    if (!note.empty()) {
      core::SubDiagnostic sub;
      sub.kind = core::SubDiagnosticKind::Note;
      sub.message = note;
      diag->children.push_back(std::move(sub));
    }
    core::Emit(diags, *diag);
  } else {
    core::EmitExternalDiagnostic(diags, "E-PRJ-0209");
  }
  return false;
}

std::vector<std::string> ComputeEmitAssemblyNames(
    std::string_view assembly_name,
    const AssemblyImportGraph& graph) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;

  std::function<void(const std::string&)> visit =
      [&](const std::string& current) {
        if (!seen.insert(current).second) {
          return;
        }
        out.push_back(current);

        const auto imports_it = graph.imports.find(current);
        if (imports_it == graph.imports.end()) {
          return;
        }
        for (const auto& dep_name : imports_it->second) {
          const auto dep_it = graph.assemblies.find(dep_name);
          if (dep_it == graph.assemblies.end()) {
            continue;
          }
          if (!IsDependency(*dep_it->second)) {
            continue;
          }
          visit(dep_name);
        }
      };

  visit(std::string(assembly_name));
  return out;
}

std::unordered_set<std::string> ReachableAssemblies(
    std::string_view root_assembly,
    const AssemblyImportGraph& graph) {
  std::unordered_set<std::string> reachable;
  std::vector<std::string> queue;
  queue.push_back(std::string(root_assembly));
  for (std::size_t i = 0; i < queue.size(); ++i) {
    const std::string& current = queue[i];
    if (!reachable.insert(current).second) {
      continue;
    }
    const auto imports_it = graph.imports.find(current);
    if (imports_it == graph.imports.end()) {
      continue;
    }
    for (const auto& dep_name : imports_it->second) {
      if (graph.assemblies.find(dep_name) == graph.assemblies.end()) {
        continue;
      }
      queue.push_back(dep_name);
    }
  }
  return reachable;
}

std::unordered_map<std::string, std::vector<std::string>> ReverseImports(
    const AssemblyImportGraph& graph) {
  std::unordered_map<std::string, std::vector<std::string>> reverse;
  reverse.reserve(graph.imports.size());
  for (const auto& [assembly_name, deps] : graph.imports) {
    for (const auto& dep_name : deps) {
      reverse[dep_name].push_back(assembly_name);
    }
  }
  for (auto& [_, importers] : reverse) {
    SortStringsDeterministically(importers);
  }
  return reverse;
}

std::optional<std::string> NearestImportingLinkableAssembly(
    std::string_view root_assembly,
    std::string_view dependency_assembly,
    const AssemblyImportGraph& graph) {
  const auto reachable = ReachableAssemblies(root_assembly, graph);
  if (reachable.find(std::string(dependency_assembly)) == reachable.end()) {
    return std::nullopt;
  }

  const auto reverse = ReverseImports(graph);
  std::queue<std::pair<std::string, std::size_t>> pending;
  std::unordered_set<std::string> visited;
  pending.push({std::string(dependency_assembly), 0});
  visited.insert(std::string(dependency_assembly));

  std::size_t best_distance = std::numeric_limits<std::size_t>::max();
  std::optional<std::string> best_owner;

  while (!pending.empty()) {
    auto [current, distance] = pending.front();
    pending.pop();

    const auto importers_it = reverse.find(current);
    if (importers_it == reverse.end()) {
      continue;
    }

    for (const auto& importer_name : importers_it->second) {
      if (reachable.find(importer_name) == reachable.end()) {
        continue;
      }
      const auto importer_it = graph.assemblies.find(importer_name);
      if (importer_it == graph.assemblies.end()) {
        continue;
      }
      const std::size_t next_distance = distance + 1;
      if (IsLinkable(*importer_it->second)) {
        if (next_distance < best_distance) {
          best_distance = next_distance;
          best_owner = importer_name;
        } else if (next_distance == best_distance && best_owner.has_value() &&
                   Utf8LexLess(importer_name, *best_owner)) {
          best_owner = importer_name;
        }
        continue;
      }

      if (!visited.insert(importer_name).second) {
        continue;
      }
      pending.push({importer_name, next_distance});
    }
  }

  return best_owner;
}

std::unordered_map<std::string, std::vector<std::string>> ComputeDependencyOwners(
    std::string_view root_assembly,
    const AssemblyImportGraph& graph) {
  std::unordered_map<std::string, std::vector<std::string>> owners;
  const auto reachable = ReachableAssemblies(root_assembly, graph);
  for (const auto& assembly_name : reachable) {
    const auto assembly_it = graph.assemblies.find(assembly_name);
    if (assembly_it == graph.assemblies.end() ||
        !IsDependency(*assembly_it->second)) {
      continue;
    }
    const auto owner = NearestImportingLinkableAssembly(
        root_assembly, assembly_name, graph);
    if (!owner.has_value()) {
      continue;
    }
    owners[*owner].push_back(assembly_name);
  }

  for (auto& [_, deps] : owners) {
    SortStringsDeterministically(deps);
  }
  return owners;
}

std::vector<std::string> ComputeDirectLibraryImports(
    std::string_view assembly_name,
    const AssemblyImportGraph& graph) {
  std::vector<std::string> libraries;
  std::unordered_set<std::string> seen;
  const auto emit_assemblies = ComputeEmitAssemblyNames(assembly_name, graph);
  for (const auto& emit_name : emit_assemblies) {
    const auto imports_it = graph.imports.find(emit_name);
    if (imports_it == graph.imports.end()) {
      continue;
    }
    for (const auto& dep_name : imports_it->second) {
      const auto dep_it = graph.assemblies.find(dep_name);
      if (dep_it == graph.assemblies.end()) {
        continue;
      }
      if (!IsLibrary(*dep_it->second) ||
          dep_name == assembly_name ||
          !seen.insert(dep_name).second) {
        continue;
      }
      libraries.push_back(dep_name);
    }
  }
  SortStringsDeterministically(libraries);
  return libraries;
}

}  // namespace

AssemblyImportGraph BuildAssemblyImportGraph(
    const Project& project,
    const std::vector<ast::ASTModule>& modules) {
  AssemblyImportGraph graph;
  graph.assemblies.reserve(project.assemblies.size());
  graph.imports.reserve(project.assemblies.size());

  const auto module_names = ModuleNamesForAssemblies(project.assemblies);
  const auto module_owner = ModuleOwnerMapForAssemblies(project.assemblies);
  for (const auto& assembly : project.assemblies) {
    graph.assemblies.emplace(assembly.name, &assembly);
    graph.imports.emplace(assembly.name, std::vector<std::string>{});
  }

  for (const auto& module : modules) {
    const std::string module_path = core::StringOfPath(module.path);
    const auto owner_it = module_owner.find(module_path);
    if (owner_it == module_owner.end()) {
      continue;
    }
    auto& assembly_imports = graph.imports[owner_it->second];
    for (const auto& item : module.items) {
      const auto* import = std::get_if<ast::ImportDecl>(&item);
      if (!import) {
        continue;
      }
      const auto imported_assembly = ResolveImportedAssemblyName(
          *import, module.path, module_names, module_owner);
      if (!imported_assembly.has_value()) {
        continue;
      }
      if (*imported_assembly == owner_it->second) {
        continue;
      }
      if (graph.assemblies.find(*imported_assembly) == graph.assemblies.end()) {
        continue;
      }
      assembly_imports.push_back(*imported_assembly);
    }
    SortStringsDeterministically(assembly_imports);
  }

  return graph;
}

bool ValidateAssemblyImportGraphStructure(const Project& project,
                                          const AssemblyImportGraph& graph,
                                          core::DiagnosticStream& diags) {
  const auto selected_it = graph.assemblies.find(project.assembly.name);
  if (selected_it == graph.assemblies.end()) {
    return true;
  }
  const auto reachable = ReachableAssemblies(project.assembly.name, graph);

  for (const auto& [assembly_name, deps] : graph.imports) {
    if (reachable.find(assembly_name) == reachable.end()) {
      continue;
    }
    for (const auto& dep_name : deps) {
      if (reachable.find(dep_name) == reachable.end()) {
        continue;
      }
      const auto dep_it = graph.assemblies.find(dep_name);
      if (dep_it == graph.assemblies.end()) {
        continue;
      }
      if (IsExecutable(*dep_it->second)) {
        return EmitAssemblyGraphDiag(
            diags, "imported executable assembly: " + dep_name);
      }
    }
  }

  enum class VisitState { Unseen, Visiting, Done };

  std::unordered_map<std::string, VisitState> state;
  state.reserve(graph.assemblies.size());
  std::vector<std::string> stack;

  std::function<bool(const std::string&)> dfs =
      [&](const std::string& assembly_name) -> bool {
        state[assembly_name] = VisitState::Visiting;
        stack.push_back(assembly_name);

        const auto imports_it = graph.imports.find(assembly_name);
        if (imports_it != graph.imports.end()) {
          for (const auto& dep_name : imports_it->second) {
            const auto dep_it = graph.assemblies.find(dep_name);
            if (dep_it == graph.assemblies.end()) {
              continue;
            }
            const auto* dep_assembly = dep_it->second;
            if (!IsLibrary(*dep_assembly)) {
              continue;
            }

            const VisitState dep_state = state[dep_name];
            if (dep_state == VisitState::Unseen) {
              if (!dfs(dep_name)) {
                return false;
              }
              continue;
            }
            if (dep_state != VisitState::Visiting) {
              continue;
            }

            auto cycle_start = std::find(stack.begin(), stack.end(), dep_name);
            bool has_library = false;
            std::ostringstream cycle;
            if (cycle_start != stack.end()) {
              for (auto it = cycle_start; it != stack.end(); ++it) {
                has_library = true;
                if (it != cycle_start) {
                  cycle << " -> ";
                }
                cycle << *it;
              }
              cycle << " -> " << dep_name;
            } else {
              cycle << assembly_name << " -> " << dep_name;
            }

            if (has_library) {
              return EmitAssemblyGraphDiag(
                  diags, "linked-library cycle: " + cycle.str());
            }
          }
        }

        stack.pop_back();
        state[assembly_name] = VisitState::Done;
        return true;
      };

  for (const auto& [assembly_name, assembly] : graph.assemblies) {
    if (reachable.find(assembly_name) == reachable.end()) {
      continue;
    }
    if (!IsLibrary(*assembly)) {
      continue;
    }
    if (state[assembly_name] != VisitState::Unseen) {
      continue;
    }
    if (!dfs(assembly_name)) {
      return false;
    }
  }
  return true;
}

bool ValidateHostedLibraryImportGraph(const Project& project,
                                      const AssemblyImportGraph& graph,
                                      const std::vector<ast::ASTModule>& modules,
                                      core::DiagnosticStream& diags) {
  const auto selected_it = graph.assemblies.find(project.assembly.name);
  if (selected_it == graph.assemblies.end()) {
    return true;
  }

  if (!IsLibrary(*selected_it->second)) {
    return true;
  }

  std::unordered_set<std::string> selected_module_paths;
  selected_module_paths.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    selected_module_paths.insert(module.path);
  }

  bool selected_is_hosted_library = false;
  for (const auto& module : modules) {
    const std::string module_path = core::StringOfPath(module.path);
    if (selected_module_paths.find(module_path) == selected_module_paths.end()) {
      continue;
    }
    for (const auto& item : module.items) {
      const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
      if (proc &&
          analysis::HasAttribute(proc->attrs, analysis::attrs::kHostExport)) {
        selected_is_hosted_library = true;
        break;
      }
    }
    if (selected_is_hosted_library) {
      break;
    }
  }

  if (!selected_is_hosted_library) {
    return true;
  }

  const auto imported_libraries =
      ComputeLibraryClosure(project.assembly.name, graph);
  if (imported_libraries.empty()) {
    return true;
  }

  if (auto diag = core::MakeExternalDiagnostic("E-PRJ-0210")) {
    core::SubDiagnostic note;
    note.kind = core::SubDiagnosticKind::Note;
    note.message = "hosted library `" + project.assembly.name +
                   "` imports linked library assembly `" +
                   imported_libraries.front() + "`";
    diag->children.push_back(std::move(note));
    core::Emit(diags, *diag);
  } else {
    core::EmitExternalDiagnostic(diags, "E-PRJ-0210");
  }
  return false;
}

std::vector<ModuleInfo> ComputeEmitModules(std::string_view assembly_name,
                                           const AssemblyImportGraph& graph) {
  std::vector<ModuleInfo> modules;
  const auto assembly_it = graph.assemblies.find(std::string(assembly_name));
  if (assembly_it == graph.assemblies.end()) {
    return modules;
  }

  modules.insert(modules.end(),
                 assembly_it->second->modules.begin(),
                 assembly_it->second->modules.end());

  if (IsLinkable(*assembly_it->second)) {
    const auto owners = ComputeDependencyOwners(assembly_name, graph);
    const auto deps_it = owners.find(std::string(assembly_name));
    if (deps_it != owners.end()) {
      for (const auto& dep_name : deps_it->second) {
        const auto dep_it = graph.assemblies.find(dep_name);
        if (dep_it == graph.assemblies.end()) {
          continue;
        }
        modules.insert(modules.end(), dep_it->second->modules.begin(),
                       dep_it->second->modules.end());
      }
    }
  }

  SortModulesDeterministically(modules);
  return modules;
}

std::vector<std::string> ComputeLibraryClosure(
    std::string_view assembly_name,
    const AssemblyImportGraph& graph) {
  std::vector<std::string> libraries;
  std::unordered_set<std::string> discovered;
  std::vector<std::string> pending =
      ComputeDirectLibraryImports(assembly_name, graph);

  for (std::size_t i = 0; i < pending.size(); ++i) {
    const std::string& current = pending[i];
    if (!discovered.insert(current).second) {
      continue;
    }
    libraries.push_back(current);
    const auto nested = ComputeDirectLibraryImports(current, graph);
    for (const auto& lib_name : nested) {
      if (discovered.find(lib_name) == discovered.end()) {
        pending.push_back(lib_name);
      }
    }
  }

  SortStringsDeterministically(libraries);
  std::unordered_set<std::string> library_set(libraries.begin(), libraries.end());
  std::vector<std::string> order;
  std::unordered_set<std::string> emitted;
  order.reserve(libraries.size());

  while (order.size() < libraries.size()) {
    bool progress = false;
    for (const auto& candidate : libraries) {
      if (emitted.find(candidate) != emitted.end()) {
        continue;
      }
      bool ready = true;
      for (const auto& dep_name : ComputeDirectLibraryImports(candidate, graph)) {
        if (library_set.find(dep_name) != library_set.end() &&
            emitted.find(dep_name) == emitted.end()) {
          ready = false;
          break;
        }
      }
      if (!ready) {
        continue;
      }
      emitted.insert(candidate);
      order.push_back(candidate);
      progress = true;
      break;
    }
    if (!progress) {
      break;
    }
  }

  return order;
}

std::vector<std::string> ImportedLibraries(
    std::string_view assembly_name,
    const AssemblyImportGraph& graph) {
  return ComputeLibraryClosure(assembly_name, graph);
}

std::optional<Project> BuildOutputProjectForAssembly(
    const Project& base_project,
    const AssemblyImportGraph& graph,
    std::string_view assembly_name) {
  const auto assembly_it = graph.assemblies.find(std::string(assembly_name));
  if (assembly_it == graph.assemblies.end()) {
    return std::nullopt;
  }

  Project output_project = AssemblyProject(base_project, *assembly_it->second);
  output_project.modules = ComputeEmitModules(output_project.assembly.name, graph);
  return output_project;
}

}  // namespace cursive::analysis
