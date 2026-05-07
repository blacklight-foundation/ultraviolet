#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "00_core/diagnostics.h"
#include "01_project/project.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct AssemblyImportGraph {
  std::unordered_map<std::string, const project::Assembly*> assemblies;
  std::unordered_map<std::string, std::vector<std::string>> imports;
};

AssemblyImportGraph BuildAssemblyImportGraph(
    const project::Project& project,
    const std::vector<ast::ASTModule>& modules);

bool ValidateAssemblyImportGraphStructure(
    const project::Project& project,
    const AssemblyImportGraph& graph,
    core::DiagnosticStream& diags);

bool ValidateHostedLibraryImportGraph(
    const project::Project& project,
    const AssemblyImportGraph& graph,
    const std::vector<ast::ASTModule>& modules,
    core::DiagnosticStream& diags);

std::vector<project::ModuleInfo> ComputeEmitModules(
    std::string_view assembly_name,
    const AssemblyImportGraph& graph);

std::vector<std::string> ComputeLibraryClosure(
    std::string_view assembly_name,
    const AssemblyImportGraph& graph);
std::vector<std::string> ImportedLibraries(
    std::string_view assembly_name,
    const AssemblyImportGraph& graph);

std::optional<project::Project> BuildOutputProjectForAssembly(
    const project::Project& base_project,
    const AssemblyImportGraph& graph,
    std::string_view assembly_name);

}  // namespace cursive::analysis
