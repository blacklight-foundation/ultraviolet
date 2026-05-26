#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/typing/context.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

class LanguageServiceIndex;

struct ResolveExprResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  ast::ExprPtr expr;
};

struct ResolveArgsResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<ast::Arg> args;
};

struct ResolveFieldInitsResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<ast::FieldInit> fields;
};

struct ResolveTypePathResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  ast::TypePath path;
};

struct ResolveRecordPathResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  ast::TypePath path;
};

struct ResolveEnumPathResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  ast::TypePath path;
};

struct ResolveQualifiedFormResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  ast::ExprPtr expr;
};

using ResolveExprFn =
    ResolveExprResult (*)(const ScopeContext& ctx,
                          const NameMapTable& name_maps,
                          const source::ModuleNames& module_names,
                          LanguageServiceIndex* language_service,
                          const ast::ExprPtr& expr);

using ResolveTypePathFn =
    ResolveTypePathResult (*)(const ScopeContext& ctx,
                              const NameMapTable& name_maps,
                              const source::ModuleNames& module_names,
                              const ast::TypePath& path);

struct ResolveQualContext {
  const ScopeContext* ctx = nullptr;
  const NameMapTable* name_maps = nullptr;
  const source::ModuleNames* module_names = nullptr;
  ResolveExprFn resolve_expr = nullptr;
  ResolveTypePathFn resolve_type_path = nullptr;
  CanAccessFn can_access = nullptr;
  LanguageServiceIndex* language_service = nullptr;
};

ResolveArgsResult ResolveArgs(const ResolveQualContext& ctx,
                              const std::vector<ast::Arg>& args);

ResolveFieldInitsResult ResolveFieldInits(
    const ResolveQualContext& ctx,
    const std::vector<ast::FieldInit>& fields);

ResolveRecordPathResult ResolveRecordPath(const ResolveQualContext& ctx,
                                          const ast::ModulePath& path,
                                          std::string_view name);

ResolveEnumPathResult ResolveEnumUnit(const ResolveQualContext& ctx,
                                      const ast::ModulePath& path,
                                      std::string_view name);

ResolveEnumPathResult ResolveEnumTuple(const ResolveQualContext& ctx,
                                       const ast::ModulePath& path,
                                       std::string_view name);

ResolveEnumPathResult ResolveEnumRecord(const ResolveQualContext& ctx,
                                        const ast::ModulePath& path,
                                        std::string_view name);

ResolveQualifiedFormResult ResolveQualifiedForm(const ResolveQualContext& ctx,
                                                const ast::Expr& expr);

}  // namespace ultraviolet::analysis
