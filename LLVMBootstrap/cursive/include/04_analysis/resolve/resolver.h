#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

class LanguageServiceIndex;

struct ResolveContext {
  ScopeContext* ctx = nullptr;
  const NameMapTable* name_maps = nullptr;
  const source::ModuleNames* module_names = nullptr;
  CanAccessFn can_access = nullptr;
  bool parse_ok = true;
  const core::DiagnosticStream* parse_diags = nullptr;
  LanguageServiceIndex* language_service = nullptr;
};

template <typename T>
struct ResolveResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  T value{};
  std::string diag_detail;
  std::vector<core::SubDiagnostic> diag_children;
};

using ResExprResult = ResolveResult<ast::ExprPtr>;
using ResTypeResult = ResolveResult<std::shared_ptr<ast::Type>>;
using ResTypePathResult = ResolveResult<ast::TypePath>;
using ResClassPathResult = ResolveResult<ast::ClassPath>;
using ResPatternResult = ResolveResult<ast::PatternPtr>;
using ResParamsResult = ResolveResult<std::vector<ast::Param>>;
using ResItemsResult = ResolveResult<std::vector<ast::ASTItem>>;

struct ResolveStmtResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ast::Stmt stmt{};
  std::string diag_detail;
  std::vector<core::SubDiagnostic> diag_children;
};

struct ResolveStmtSeqResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::vector<ast::Stmt> stmts;
  std::string diag_detail;
  std::vector<core::SubDiagnostic> diag_children;
};

struct ResolveBlockResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ast::Block block;
  std::string diag_detail;
  std::vector<core::SubDiagnostic> diag_children;
};

struct ResolveModuleResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ast::ASTModule module;
  std::string diag_detail;
  std::vector<core::SubDiagnostic> diag_children;
};

struct ResolveModulesResult {
  bool ok = false;
  core::DiagnosticStream diags;
  std::vector<ast::ASTModule> modules;
};

void PopulateSigma(ScopeContext& ctx);

ResolveModulesResult ResolveModules(ResolveContext& ctx);
ResolveModuleResult ResolveModule(ResolveContext& ctx,
                                  const ast::ASTModule& module);

ResItemsResult ResolveItems(ResolveContext& ctx,
                                const std::vector<ast::ASTItem>& items);
ResolveResult<ast::ASTItem> ResolveItem(ResolveContext& ctx,
                                           const ast::ASTItem& item);

ResExprResult ResolveExpr(ResolveContext& ctx,
                              const ast::ExprPtr& expr);
ResPatternResult ResolvePattern(ResolveContext& ctx,
                                    const ast::PatternPtr& pattern);
ResTypeResult ResolveType(ResolveContext& ctx,
                              const std::shared_ptr<ast::Type>& type);
ResTypePathResult ResolveTypePath(ResolveContext& ctx,
                                      const ast::TypePath& path);
ResClassPathResult ResolveClassPath(ResolveContext& ctx,
                                        const ast::ClassPath& path);

ResolveStmtResult ResolveStmt(ResolveContext& ctx,
                              const ast::Stmt& stmt);
ResolveStmtSeqResult ResolveStmtSeq(ResolveContext& ctx,
                                    const std::vector<ast::Stmt>& stmts);
ResolveBlockResult ResolveBlock(ResolveContext& ctx,
                                const ast::Block& block);

}  // namespace cursive::analysis
