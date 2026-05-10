#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

using LowerTypeFn =
    std::function<LowerTypeResult(const std::shared_ptr<ast::Type>&)>;

struct RecvTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

RecvTypeResult RecvTypeForReceiver(const ScopeContext& ctx,
                                   const TypeRef& base,
                                   const ast::Receiver& receiver,
                                   const LowerTypeFn& lower_type);

std::optional<ParamMode> RecvModeOf(const ast::Receiver& receiver);

struct RecvBaseTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  Permission perm = Permission::Const;
  TypeRef base;
};

RecvBaseTypeResult RecvBaseType(const ast::ExprPtr& base,
                                const std::optional<ParamMode>& mode,
                                const PlaceTypeFn& type_place,
                                const ExprTypeFn& type_expr);

struct RecvArgOkResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

RecvArgOkResult RecvArgOk(const ast::ExprPtr& base,
                          const std::optional<ParamMode>& mode,
                          const ExprTypeFn& type_expr);

struct ArgsOkResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

std::vector<const ast::MethodDecl*> RecordMethods(
    const ast::RecordDecl& record);

ArgsOkResult ArgsOk(const ScopeContext& ctx,
                    const std::vector<ast::Param>& params,
                    const std::vector<ast::Arg>& args,
                    const ExprTypeFn& type_expr,
                    const PlaceTypeFn* type_place,
                    const LowerTypeFn& lower_type,
                    const ArgCheckFn* check_expr = nullptr);

ArgsOkResult ArgsOkWithSubst(const ScopeContext& ctx,
                             const std::vector<ast::Param>& params,
                             const std::vector<ast::Arg>& args,
                             const ExprTypeFn& type_expr,
                             const PlaceTypeFn* type_place,
                             const LowerTypeFn& lower_type,
                             const TypeSubst& subst,
                             const ArgCheckFn* check_expr = nullptr);

struct StaticMethodLookup {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  const ast::RecordDecl* record_decl = nullptr;
  const ast::MethodDecl* record_method = nullptr;
  const ast::ClassMethodDecl* class_method = nullptr;
  TypePath record_path;
  std::vector<TypeRef> record_generic_args;
  ast::ClassPath owner_class;
};

StaticMethodLookup LookupMethodStatic(const ScopeContext& ctx,
                                      const TypeRef& base,
                                      std::string_view name);

}  // namespace cursive::analysis
