#pragma once

#include <optional>
#include <string_view>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct ValuePathTypeResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

struct ModuleStaticLookupResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type;
  bool is_mutable = false;
};

ValuePathTypeResult ValuePathType(const ScopeContext& ctx,
                                  const ast::ModulePath& path,
                                  std::string_view name);

ModuleStaticLookupResult LookupModuleStatic(const ScopeContext& ctx,
                                            const ast::ModulePath& path,
                                            std::string_view name);

ValuePathTypeResult ProcType(const ScopeContext& ctx,
                             const ast::ProcedureDecl& decl);

}  // namespace cursive::analysis
