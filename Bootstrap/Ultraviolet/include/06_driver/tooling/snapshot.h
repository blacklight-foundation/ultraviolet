#pragma once

#include <optional>
#include <vector>

#include "00_core/diagnostics.h"
#include "01_project/project.h"
#include "02_source/ast/ast.h"
#include "04_analysis/language_service/facts.h"
#include "04_analysis/typing/context.h"
#include "06_driver/tooling/symbol_index.h"

namespace ultraviolet::driver::tooling {

struct AnalysisSnapshot {
  bool project_ok = false;
  bool parse_ok = false;
  bool comptime_ok = false;
  bool resolve_ok = false;
  bool typecheck_ok = false;
  std::optional<project::Project> project;
  std::vector<ast::ASTModule> modules;
  core::DiagnosticStream diagnostics;
  SymbolIndex symbols;
  analysis::LanguageServiceIndex language_service;
  analysis::ExprTypeMap expr_types;
};

}  // namespace ultraviolet::driver::tooling
