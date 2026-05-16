// =============================================================================
// resolve_items.cpp - Item Resolution Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//   CursiveSpecification.md §5.1.2 "Name Introduction and Shadowing" (Lines 6718-6821)
//
// SOURCE FILE:
//   cursive-bootstrap/src/03_analysis/resolve/resolver_items.cpp (Lines 1-868)
//
// MIGRATED: 2026-02-01
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <utility>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "04_analysis/language_service/facts.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_intro.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/resolve/scope_overrides.h"
#include "04_analysis/resolve/visibility.h"

namespace cursive::analysis {

ResolveResult<std::optional<ast::GenericParams>> ResolveGenericParamsOpt(
    ResolveContext& ctx,
    const std::optional<ast::GenericParams>& params_opt);
ResolveResult<std::optional<ast::ContractClause>> ResolveContractOpt(
    ResolveContext& ctx,
    const std::optional<ast::ContractClause>& contract_opt);
ResolveResult<std::optional<ast::PredicateClause>> ResolveWhereClauseOpt(
    ResolveContext& ctx,
    const std::optional<ast::PredicateClause>& where_opt);
ResolveResult<std::optional<ast::TypeInvariant>> ResolveInvariantOpt(
    ResolveContext& ctx,
    const std::optional<ast::TypeInvariant>& invariant_opt);

namespace {

static inline void SpecDefsResolverItems() {
  SPEC_DEF("ResolveItem", "5.1.7");
  SPEC_DEF("ResolveParams", "5.1.7");
  SPEC_DEF("ResolveParam", "5.1.7");
  SPEC_DEF("ResolveTypeList", "5.1.7");
  SPEC_DEF("ResolveFieldDecl", "5.1.7");
  SPEC_DEF("ResolveFieldDeclList", "5.1.7");
  SPEC_DEF("ResolveRecordMember", "5.1.7");
  SPEC_DEF("ResolveRecordMemberList", "5.1.7");
  SPEC_DEF("ResolveClassItem", "5.1.7");
  SPEC_DEF("ResolveClassItemList", "5.1.7");
  SPEC_DEF("ResolveVariant", "5.1.7");
  SPEC_DEF("ResolveVariantList", "5.1.7");
  SPEC_DEF("ResolveStateMember", "5.1.7");
  SPEC_DEF("ResolveStateMemberList", "5.1.7");
  SPEC_DEF("ResolveStateBlockList", "5.1.7");
  SPEC_DEF("BindSelfRecord", "5.1.7");
  SPEC_DEF("BindSelfClass", "5.1.7");
}

Scope BuildDeriveTargetCapabilityScope() {
  Scope scope;
  const auto add = [&](std::string_view name) {
    scope.emplace(IdKeyOf(name),
                  Entity{EntityKind::Value, std::nullopt, std::nullopt,
                         EntitySource::Decl});
  };
  add("target");
  add("emitter");
  add("introspect");
  add("diagnostics");
  return scope;
}

Entity LocalLanguageEntity(ResolveContext& ctx,
                           std::string_view name,
                           const core::Span& span,
                           LanguageSymbolKind kind,
                           std::string detail) {
  if (ctx.ctx == nullptr) {
    return Entity{EntityKind::Value, std::nullopt, std::nullopt,
                  EntitySource::Decl};
  }
  return MakeLanguageServiceLocalEntity(ctx.language_service, *ctx.ctx, name,
                                        span, kind, std::move(detail));
}

void AddParamToScope(ResolveContext& ctx, Scope& scope, const ast::Param& param) {
  scope.emplace(IdKeyOf(param.name),
                LocalLanguageEntity(ctx, param.name, param.span,
                                    LanguageSymbolKind::Parameter,
                                    "parameter"));
}

std::optional<core::Span> SpanOfItem(const ast::ASTItem& item) {
  return std::visit(
      [](const auto& it) -> std::optional<core::Span> { return it.span; },
      item);
}

ResTypeResult ResolveTypeOpt(ResolveContext& ctx,
                             const std::shared_ptr<ast::Type>& type_opt) {
  ResTypeResult result;
  result.ok = true;
  result.value = type_opt;
  if (!type_opt) {
    SPEC_RULE("ResolveTypeOpt-None");
    return result;
  }
  const auto resolved = ResolveType(ctx, type_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {},
            resolved.diag_detail, resolved.diag_children};
  }
  SPEC_RULE("ResolveTypeOpt-Some");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

ResExprResult ResolveExprOpt(ResolveContext& ctx,
                            const ast::ExprPtr& expr_opt) {
  ResExprResult result;
  result.ok = true;
  result.value = expr_opt;
  if (!expr_opt) {
    SPEC_RULE("ResolveExprOpt-None");
    return result;
  }
  const auto resolved = ResolveExpr(ctx, expr_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {},
            resolved.diag_detail, resolved.diag_children};
  }
  SPEC_RULE("ResolveExprOpt-Some");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

ResolveResult<ast::Receiver> ResolveReceiver(ResolveContext& ctx,
                                                const ast::Receiver& recv) {
  ResolveResult<ast::Receiver> result;
  return std::visit(
      [&](const auto& node) -> ResolveResult<ast::Receiver> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ReceiverShorthand>) {
          SPEC_RULE("ResolveReceiver-Shorthand");
          result.ok = true;
          result.value = recv;
          return result;
        } else {
          const auto resolved = ResolveType(ctx, node.type);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {}};
          }
          ast::ReceiverExplicit out = node;
          out.type = resolved.value;
          SPEC_RULE("ResolveReceiver-Explicit");
          result.ok = true;
          result.value = out;
          return result;
        }
      },
      recv);
}

ResolveResult<std::vector<ast::Param>> ResolveParams(
    ResolveContext& ctx,
    const std::vector<ast::Param>& params) {
  ResolveResult<std::vector<ast::Param>> result;
  result.ok = true;
  if (params.empty()) {
    SPEC_RULE("ResolveParams-Empty");
    return result;
  }
  result.value.reserve(params.size());
  for (const auto& param : params) {
    auto resolved_param = param;
    const auto resolved_type = ResolveType(ctx, param.type);
    if (!resolved_type.ok) {
      return {false, resolved_type.diag_id, resolved_type.span, {}};
    }
    resolved_param.type = resolved_type.value;
    SPEC_RULE("ResolveParam");
    result.value.push_back(std::move(resolved_param));
    SPEC_RULE("ResolveParams-Cons");
  }
  return result;
}

ResolveResult<std::vector<std::shared_ptr<ast::Type>>> ResolveTypeList(
    ResolveContext& ctx,
    const std::vector<std::shared_ptr<ast::Type>>& types) {
  ResolveResult<std::vector<std::shared_ptr<ast::Type>>> result;
  result.ok = true;
  if (types.empty()) {
    SPEC_RULE("ResolveTypeList-Empty");
    return result;
  }
  result.value.reserve(types.size());
  for (const auto& type : types) {
    const auto resolved = ResolveType(ctx, type);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveTypeList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::ClassPath>> ResolveClassPathList(
    ResolveContext& ctx,
    const std::vector<ast::ClassPath>& paths) {
  ResolveResult<std::vector<ast::ClassPath>> result;
  result.ok = true;
  result.value.reserve(paths.size());
  for (const auto& path : paths) {
    const auto resolved = ResolveClassPath(ctx, path);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveClassPathList-Cons");
  }
  if (paths.empty()) {
    SPEC_RULE("ResolveClassPathList-Empty");
  }
  return result;
}

ResolveResult<std::vector<ast::FieldDecl>> ResolveFieldDeclList(
    ResolveContext& ctx,
    const std::vector<ast::FieldDecl>& fields) {
  ResolveResult<std::vector<ast::FieldDecl>> result;
  result.ok = true;
  if (fields.empty()) {
    SPEC_RULE("ResolveFieldDeclList-Empty");
    return result;
  }
  result.value.reserve(fields.size());
  for (const auto& field : fields) {
    auto out = field;
    const auto resolved_type = ResolveType(ctx, field.type);
    if (!resolved_type.ok) {
      return {false, resolved_type.diag_id, resolved_type.span, {}};
    }
    out.type = resolved_type.value;
    const auto resolved_init = ResolveExprOpt(ctx, field.init_opt);
    if (!resolved_init.ok) {
      return {false, resolved_init.diag_id, resolved_init.span, {},
              resolved_init.diag_detail, resolved_init.diag_children};
    }
    out.init_opt = resolved_init.value;
    SPEC_RULE("ResolveFieldDecl");
    result.value.push_back(std::move(out));
    SPEC_RULE("ResolveFieldDeclList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::VariantDecl>> ResolveVariantList(
    ResolveContext& ctx,
    const std::vector<ast::VariantDecl>& vars) {
  ResolveResult<std::vector<ast::VariantDecl>> result;
  result.ok = true;
  if (vars.empty()) {
    SPEC_RULE("ResolveVariantList-Empty");
    return result;
  }
  result.value.reserve(vars.size());
  for (const auto& var : vars) {
    auto out = var;
    if (var.payload_opt.has_value()) {
      if (std::holds_alternative<ast::VariantPayloadTuple>(*var.payload_opt)) {
        auto payload = std::get<ast::VariantPayloadTuple>(*var.payload_opt);
        const auto types = ResolveTypeList(ctx, payload.elements);
        if (!types.ok) {
          return {false, types.diag_id, types.span, {}};
        }
        payload.elements = types.value;
        out.payload_opt = payload;
        SPEC_RULE("ResolveVariantPayloadOpt-Tuple");
      } else {
        auto payload = std::get<ast::VariantPayloadRecord>(*var.payload_opt);
        const auto fields = ResolveFieldDeclList(ctx, payload.fields);
        if (!fields.ok) {
          return {false, fields.diag_id, fields.span, {}};
        }
        payload.fields = fields.value;
        out.payload_opt = payload;
        SPEC_RULE("ResolveVariantPayloadOpt-Record");
      }
    } else {
      SPEC_RULE("ResolveVariantPayloadOpt-None");
    }
    SPEC_RULE("ResolveVariant");
    result.value.push_back(std::move(out));
    SPEC_RULE("ResolveVariantList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::RecordMember>> ResolveRecordMemberList(
    ResolveContext& ctx,
    const ast::RecordDecl& record,
    const std::vector<ast::RecordMember>& members) {
  ResolveResult<std::vector<ast::RecordMember>> result;
  result.ok = true;
  if (members.empty()) {
    SPEC_RULE("ResolveRecordMemberList-Empty");
    return result;
  }
  result.value.reserve(members.size());
  for (const auto& member : members) {
    auto resolved = std::visit(
        [&](const auto& node) -> ResolveResult<ast::RecordMember> {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::FieldDecl>) {
            auto out = node;
            const auto resolved_type = ResolveType(ctx, node.type);
            if (!resolved_type.ok) {
              return {false, resolved_type.diag_id, resolved_type.span, {}};
            }
            out.type = resolved_type.value;
            const auto resolved_init = ResolveExprOpt(ctx, node.init_opt);
            if (!resolved_init.ok) {
              return {false, resolved_init.diag_id, resolved_init.span, {},
                      resolved_init.diag_detail, resolved_init.diag_children};
            }
            out.init_opt = resolved_init.value;
            SPEC_RULE("ResolveRecordMember-Field");
            return {true, std::nullopt, std::nullopt, out};
          } else if constexpr (std::is_same_v<T, ast::MethodDecl>) {
            auto out = node;
            Scope proc_scope;
            for (const auto& param : node.params) {
              AddParamToScope(ctx, proc_scope, param);
            }
            proc_scope.emplace(
                IdKeyOf("self"),
                LocalLanguageEntity(ctx, "self", node.span,
                                    LanguageSymbolKind::Variable, "receiver"));
            proc_scope.emplace(
                IdKeyOf("Self"),
                Entity{EntityKind::Type, ctx.ctx->current_module, record.name,
                       EntitySource::Decl});
            SPEC_RULE("BindSelf-Record");
            ScopedScopesOverride proc_scopes(
                *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
            ResolveContext method_ctx = ctx;

            const auto resolved_recv = ResolveReceiver(method_ctx, node.receiver);
            if (!resolved_recv.ok) {
              return {false, resolved_recv.diag_id, resolved_recv.span, {}};
            }
            out.receiver = resolved_recv.value;
            const auto resolved_params = ResolveParams(method_ctx, node.params);
            if (!resolved_params.ok) {
              return {false, resolved_params.diag_id, resolved_params.span, {}};
            }
            out.params = resolved_params.value;
            const auto resolved_ret = ResolveTypeOpt(method_ctx, node.return_type_opt);
            if (!resolved_ret.ok) {
              return {false, resolved_ret.diag_id, resolved_ret.span, {}};
            }
            out.return_type_opt = resolved_ret.value;
            if (node.body) {
              const auto resolved_body = ResolveBlock(method_ctx, *node.body);
              if (!resolved_body.ok) {
                return {false, resolved_body.diag_id, resolved_body.span, {},
                        resolved_body.diag_detail, resolved_body.diag_children};
              }
              out.body = std::make_shared<ast::Block>(resolved_body.block);
            }
            SPEC_RULE("ResolveRecordMember-Method");
            return {true, std::nullopt, std::nullopt, out};
          } else {
            // AssociatedTypeDecl
            auto out = node;
            if (node.default_type) {
              const auto resolved_default = ResolveType(ctx, node.default_type);
              if (!resolved_default.ok) {
                return {false, resolved_default.diag_id, resolved_default.span, {}};
              }
              out.default_type = resolved_default.value;
            }
            SPEC_RULE("ResolveRecordMember-AssociatedType");
            return {true, std::nullopt, std::nullopt, out};
          }
        },
        member);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    result.value.push_back(std::move(resolved.value));
    SPEC_RULE("ResolveRecordMemberList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::ClassItem>> ResolveClassItemList(
    ResolveContext& ctx,
    const std::vector<ast::ClassItem>& items) {
  ResolveResult<std::vector<ast::ClassItem>> result;
  result.ok = true;
  if (items.empty()) {
    SPEC_RULE("ResolveClassItemList-Empty");
    return result;
  }
  result.value.reserve(items.size());
  for (const auto& item : items) {
    auto resolved = std::visit(
        [&](const auto& node) -> ResolveResult<ast::ClassItem> {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ClassFieldDecl>) {
            auto out = node;
            const auto resolved_type = ResolveType(ctx, node.type);
            if (!resolved_type.ok) {
              return {false, resolved_type.diag_id, resolved_type.span, {}};
            }
            out.type = resolved_type.value;
            SPEC_RULE("ResolveClassItem-Field");
            return {true, std::nullopt, std::nullopt, out};
          } else if constexpr (std::is_same_v<T, ast::AssociatedTypeDecl>) {
            auto out = node;
            if (node.default_type) {
              const auto resolved_default = ResolveType(ctx, node.default_type);
              if (!resolved_default.ok) {
                return {false, resolved_default.diag_id, resolved_default.span, {}};
              }
              out.default_type = resolved_default.value;
            }
            SPEC_RULE("ResolveClassItem-AssociatedType");
            return {true, std::nullopt, std::nullopt, out};
          } else if constexpr (std::is_same_v<T, ast::AbstractFieldDecl>) {
            auto out = node;
            const auto resolved_type = ResolveType(ctx, node.type);
            if (!resolved_type.ok) {
              return {false, resolved_type.diag_id, resolved_type.span, {}};
            }
            out.type = resolved_type.value;
            SPEC_RULE("ResolveClassItem-AbstractField");
            return {true, std::nullopt, std::nullopt, out};
          } else if constexpr (std::is_same_v<T, ast::AbstractStateDecl>) {
            auto out = node;
            for (auto& field : out.fields) {
              const auto resolved_type = ResolveType(ctx, field.type);
              if (!resolved_type.ok) {
                return {false, resolved_type.diag_id, resolved_type.span, {}};
              }
              field.type = resolved_type.value;
            }
            SPEC_RULE("ResolveClassItem-AbstractState");
            return {true, std::nullopt, std::nullopt, out};
          } else {
            // ClassMethodDecl
            auto out = node;
            Scope proc_scope;
            if (node.generic_params.has_value()) {
              for (const auto& type_param : node.generic_params->params) {
                proc_scope.emplace(IdKeyOf(type_param.name),
                                   Entity{EntityKind::Type, std::nullopt,
                                          std::nullopt, EntitySource::Decl});
              }
            }
            for (const auto& param : node.params) {
              AddParamToScope(ctx, proc_scope, param);
            }
            proc_scope.emplace(
                IdKeyOf("self"),
                LocalLanguageEntity(ctx, "self", node.span,
                                    LanguageSymbolKind::Variable, "receiver"));
            proc_scope.emplace(
                IdKeyOf("Self"),
                Entity{EntityKind::Type, std::nullopt, std::nullopt,
                       EntitySource::Decl});
            SPEC_RULE("BindSelf-Class");
            ScopedScopesOverride proc_scopes(
                *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
            ResolveContext method_ctx = ctx;

            const auto resolved_recv = ResolveReceiver(method_ctx, node.receiver);
            if (!resolved_recv.ok) {
              return {false, resolved_recv.diag_id, resolved_recv.span, {}};
            }
            out.receiver = resolved_recv.value;
            const auto resolved_params = ResolveParams(method_ctx, node.params);
            if (!resolved_params.ok) {
              return {false, resolved_params.diag_id, resolved_params.span, {}};
            }
            out.params = resolved_params.value;
            const auto resolved_ret = ResolveTypeOpt(method_ctx, node.return_type_opt);
            if (!resolved_ret.ok) {
              return {false, resolved_ret.diag_id, resolved_ret.span, {}};
            }
            out.return_type_opt = resolved_ret.value;
            if (node.body_opt) {
              const auto resolved_body = ResolveBlock(method_ctx, *node.body_opt);
              if (!resolved_body.ok) {
                return {false, resolved_body.diag_id, resolved_body.span, {},
                        resolved_body.diag_detail, resolved_body.diag_children};
              }
              out.body_opt = std::make_shared<ast::Block>(resolved_body.block);
              SPEC_RULE("ResolveClassItem-Method-Concrete");
            } else {
              SPEC_RULE("ResolveClassItem-Method-Abstract");
            }
            return {true, std::nullopt, std::nullopt, out};
          }
        },
        item);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    result.value.push_back(std::move(resolved.value));
    SPEC_RULE("ResolveClassItemList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::StateMember>> ResolveStateMemberList(
    ResolveContext& ctx,
    const std::vector<ast::StateMember>& members) {
  ResolveResult<std::vector<ast::StateMember>> result;
  result.ok = true;
  if (members.empty()) {
    SPEC_RULE("ResolveStateMemberList-Empty");
    return result;
  }
  result.value.reserve(members.size());
  for (const auto& member : members) {
    auto resolved = std::visit(
        [&](const auto& node) -> ResolveResult<ast::StateMember> {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::StateFieldDecl>) {
            auto out = node;
            const auto resolved_type = ResolveType(ctx, node.type);
            if (!resolved_type.ok) {
              return {false, resolved_type.diag_id, resolved_type.span, {}};
            }
            out.type = resolved_type.value;
            SPEC_RULE("ResolveStateMember-Field");
            return {true, std::nullopt, std::nullopt, out};
          } else if constexpr (std::is_same_v<T, ast::StateMethodDecl>) {
            auto out = node;
            Scope proc_scope;
            for (const auto& param : node.params) {
              AddParamToScope(ctx, proc_scope, param);
            }
            proc_scope.emplace(
                IdKeyOf("self"),
                LocalLanguageEntity(ctx, "self", node.span,
                                    LanguageSymbolKind::Variable, "receiver"));
            ScopedScopesOverride proc_scopes(
                *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
            ResolveContext method_ctx = ctx;
            const auto resolved_params = ResolveParams(method_ctx, node.params);
            if (!resolved_params.ok) {
              return {false, resolved_params.diag_id, resolved_params.span, {}};
            }
            out.params = resolved_params.value;
            const auto resolved_ret = ResolveTypeOpt(method_ctx, node.return_type_opt);
            if (!resolved_ret.ok) {
              return {false, resolved_ret.diag_id, resolved_ret.span, {}};
            }
            out.return_type_opt = resolved_ret.value;
            if (node.body) {
              const auto resolved_body = ResolveBlock(method_ctx, *node.body);
              if (!resolved_body.ok) {
                return {false, resolved_body.diag_id, resolved_body.span, {},
                        resolved_body.diag_detail, resolved_body.diag_children};
              }
              out.body = std::make_shared<ast::Block>(resolved_body.block);
            }
            SPEC_RULE("ResolveStateMember-Method");
            return {true, std::nullopt, std::nullopt, out};
          } else {
            // TransitionDecl
            auto out = node;
            Scope proc_scope;
            for (const auto& param : node.params) {
              AddParamToScope(ctx, proc_scope, param);
            }
            proc_scope.emplace(
                IdKeyOf("self"),
                LocalLanguageEntity(ctx, "self", node.span,
                                    LanguageSymbolKind::Variable, "receiver"));
            ScopedScopesOverride proc_scopes(
                *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
            ResolveContext method_ctx = ctx;
            const auto resolved_params = ResolveParams(method_ctx, node.params);
            if (!resolved_params.ok) {
              return {false, resolved_params.diag_id, resolved_params.span, {}};
            }
            out.params = resolved_params.value;
            if (node.body) {
              const auto resolved_body = ResolveBlock(method_ctx, *node.body);
              if (!resolved_body.ok) {
                return {false, resolved_body.diag_id, resolved_body.span, {},
                        resolved_body.diag_detail, resolved_body.diag_children};
              }
              out.body = std::make_shared<ast::Block>(resolved_body.block);
            }
            SPEC_RULE("ResolveStateMember-Transition");
            return {true, std::nullopt, std::nullopt, out};
          }
        },
        member);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    result.value.push_back(std::move(resolved.value));
    SPEC_RULE("ResolveStateMemberList-Cons");
  }
  return result;
}

ResolveResult<std::vector<ast::StateBlock>> ResolveStateBlockList(
    ResolveContext& ctx,
    const std::vector<ast::StateBlock>& states) {
  ResolveResult<std::vector<ast::StateBlock>> result;
  result.ok = true;
  if (states.empty()) {
    SPEC_RULE("ResolveStateBlockList-Empty");
    return result;
  }
  result.value.reserve(states.size());
  for (const auto& state : states) {
    auto out = state;
    const auto members = ResolveStateMemberList(ctx, state.members);
    if (!members.ok) {
      return {false, members.diag_id, members.span, {},
              members.diag_detail, members.diag_children};
    }
    out.members = members.value;
    SPEC_RULE("ResolveStateBlock");
    result.value.push_back(std::move(out));
    SPEC_RULE("ResolveStateBlockList-Cons");
  }
  return result;
}

}  // namespace

ResolveResult<ast::ASTItem> ResolveItem(ResolveContext& ctx,
                                           const ast::ASTItem& item) {
  SpecDefsResolverItems();
  return std::visit(
      [&](const auto& node) -> ResolveResult<ast::ASTItem> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          auto out = node;
          if (node.binding.pat) {
            const auto resolved_pat = ResolvePattern(ctx, node.binding.pat);
            if (!resolved_pat.ok) {
              return {false, resolved_pat.diag_id, resolved_pat.span, {}};
            }
            out.binding.pat = resolved_pat.value;
          }
          const auto resolved_ty = ResolveTypeOpt(ctx, node.binding.type_opt);
          if (!resolved_ty.ok) {
            return {false, resolved_ty.diag_id, resolved_ty.span, {}};
          }
          out.binding.type_opt = resolved_ty.value;
          const auto resolved_init = ResolveExpr(ctx, node.binding.init);
          if (!resolved_init.ok) {
            return {false, resolved_init.diag_id, resolved_init.span, {},
                    resolved_init.diag_detail, resolved_init.diag_children};
          }
          out.binding.init = resolved_init.value;
          SPEC_RULE("ResolveItem-Static");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          auto out = node;
          Scope proc_scope;
          for (const auto& param : node.params) {
            AddParamToScope(ctx, proc_scope, param);
          }
          ScopedScopesOverride proc_scopes(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
          ResolveContext proc_res = ctx;

          const auto resolved_gen =
              ResolveGenericParamsOpt(proc_res, node.generic_params);
          if (!resolved_gen.ok) {
            return {false, resolved_gen.diag_id, resolved_gen.span, {}};
          }
          out.generic_params = resolved_gen.value;
          const auto resolved_where =
              ResolveWhereClauseOpt(proc_res, node.predicate_clause_opt);
          if (!resolved_where.ok) {
            return {false, resolved_where.diag_id, resolved_where.span, {}};
          }
          out.predicate_clause_opt = resolved_where.value;

          const auto resolved_params = ResolveParams(proc_res, node.params);
          if (!resolved_params.ok) {
            return {false, resolved_params.diag_id, resolved_params.span, {}};
          }
          out.params = resolved_params.value;
          const auto resolved_ret = ResolveTypeOpt(proc_res, node.return_type_opt);
          if (!resolved_ret.ok) {
            return {false, resolved_ret.diag_id, resolved_ret.span, {},
                    resolved_ret.diag_detail, resolved_ret.diag_children};
          }
          out.return_type_opt = resolved_ret.value;
          if (node.body) {
            const auto resolved_body = ResolveBlock(proc_res, *node.body);
            if (!resolved_body.ok) {
              return {false, resolved_body.diag_id, resolved_body.span, {},
                      resolved_body.diag_detail, resolved_body.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved_body.block);
          }
          SPEC_RULE("ResolveItem-Procedure");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          auto out = node;
          Scope proc_scope;
          for (const auto& param : node.params) {
            AddParamToScope(ctx, proc_scope, param);
          }
          ScopedScopesOverride proc_scopes(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
          ResolveContext proc_res = ctx;

          const auto resolved_gen =
              ResolveGenericParamsOpt(proc_res, node.generic_params);
          if (!resolved_gen.ok) {
            return {false, resolved_gen.diag_id, resolved_gen.span, {}};
          }
          out.generic_params = resolved_gen.value;

          const auto resolved_params = ResolveParams(proc_res, node.params);
          if (!resolved_params.ok) {
            return {false, resolved_params.diag_id, resolved_params.span, {}};
          }
          out.params = resolved_params.value;

          const auto resolved_ret = ResolveTypeOpt(proc_res, node.return_type_opt);
          if (!resolved_ret.ok) {
            return {false, resolved_ret.diag_id, resolved_ret.span, {}};
          }
          out.return_type_opt = resolved_ret.value;

          const auto resolved_contract =
              ResolveContractOpt(proc_res, node.contract);
          if (!resolved_contract.ok) {
            return {false, resolved_contract.diag_id, resolved_contract.span, {},
                    resolved_contract.diag_detail,
                    resolved_contract.diag_children};
          }
          out.contract = resolved_contract.value;

          if (node.body) {
            const auto resolved_body = ResolveBlock(proc_res, *node.body);
            if (!resolved_body.ok) {
              return {false, resolved_body.diag_id, resolved_body.span, {},
                      resolved_body.diag_detail, resolved_body.diag_children};
            }
            out.body = std::make_shared<ast::Block>(resolved_body.block);
          }
          SPEC_RULE("ResolveItem-Procedure");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          auto out = node;
          Scope derive_scope = BuildDeriveTargetCapabilityScope();
          ScopedScopesOverride derive_scopes(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(derive_scope)));
          ResolveContext derive_res = ctx;
          const auto resolved_body = ResolveBlock(derive_res, *node.body);
          if (!resolved_body.ok) {
            return {false, resolved_body.diag_id, resolved_body.span, {},
                    resolved_body.diag_detail, resolved_body.diag_children};
          }
          out.body = std::make_shared<ast::Block>(resolved_body.block);
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          SPEC_RULE("ResolveItem-Using");
          return {true, std::nullopt, std::nullopt, node};
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          auto out = node;
          ResolveContext alias_ctx = ctx;
          ScopedScopesOverride alias_scope_ctx(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, Scope{}));
          const auto resolved_gen =
              ResolveGenericParamsOpt(alias_ctx, node.generic_params);
          if (!resolved_gen.ok) {
            return {false, resolved_gen.diag_id, resolved_gen.span, {}};
          }
          out.generic_params = resolved_gen.value;
          const auto resolved_where =
              ResolveWhereClauseOpt(alias_ctx, node.predicate_clause_opt);
          if (!resolved_where.ok) {
            return {false, resolved_where.diag_id, resolved_where.span, {}};
          }
          out.predicate_clause_opt = resolved_where.value;
          const auto resolved = ResolveType(alias_ctx, node.type);
          if (!resolved.ok) {
            return {false, resolved.diag_id, resolved.span, {}};
          }
          out.type = resolved.value;
          SPEC_RULE("ResolveItem-TypeAlias");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          auto out = node;
          ResolveContext record_ctx = ctx;
          ScopedScopesOverride record_scope_ctx(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, Scope{}));
          const auto resolved_gen =
              ResolveGenericParamsOpt(record_ctx, node.generic_params);
          if (!resolved_gen.ok) {
            return {false, resolved_gen.diag_id, resolved_gen.span, {}};
          }
          out.generic_params = resolved_gen.value;
          const auto resolved_where =
              ResolveWhereClauseOpt(record_ctx, node.predicate_clause_opt);
          if (!resolved_where.ok) {
            return {false, resolved_where.diag_id, resolved_where.span, {}};
          }
          out.predicate_clause_opt = resolved_where.value;
          const auto impls = ResolveClassPathList(record_ctx, node.implements);
          if (!impls.ok) {
            return {false, impls.diag_id, impls.span, {}};
          }
          out.implements = impls.value;
          const auto invariant =
              ResolveInvariantOpt(record_ctx, node.invariant_opt);
          if (!invariant.ok) {
            return {false, invariant.diag_id, invariant.span, {}};
          }
          out.invariant_opt = invariant.value;
          const auto members = ResolveRecordMemberList(record_ctx, node, node.members);
          if (!members.ok) {
            return {false, members.diag_id, members.span, {},
                    members.diag_detail, members.diag_children};
          }
          out.members = members.value;
          SPEC_RULE("ResolveItem-Record");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          auto out = node;
          ResolveContext enum_ctx = ctx;
          ScopedScopesOverride enum_scope_ctx(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, Scope{}));
          const auto resolved_gen =
              ResolveGenericParamsOpt(enum_ctx, node.generic_params);
          if (!resolved_gen.ok) {
            return {false, resolved_gen.diag_id, resolved_gen.span, {}};
          }
          out.generic_params = resolved_gen.value;
          const auto resolved_where =
              ResolveWhereClauseOpt(enum_ctx, node.predicate_clause_opt);
          if (!resolved_where.ok) {
            return {false, resolved_where.diag_id, resolved_where.span, {}};
          }
          out.predicate_clause_opt = resolved_where.value;
          const auto impls = ResolveClassPathList(enum_ctx, node.implements);
          if (!impls.ok) {
            return {false, impls.diag_id, impls.span, {}};
          }
          out.implements = impls.value;
          const auto invariant =
              ResolveInvariantOpt(enum_ctx, node.invariant_opt);
          if (!invariant.ok) {
            return {false, invariant.diag_id, invariant.span, {}};
          }
          out.invariant_opt = invariant.value;
          const auto vars = ResolveVariantList(enum_ctx, node.variants);
          if (!vars.ok) {
            return {false, vars.diag_id, vars.span, {}};
          }
          out.variants = vars.value;
          SPEC_RULE("ResolveItem-Enum");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          auto out = node;
          ResolveContext modal_ctx = ctx;
          ScopedScopesOverride modal_scope_ctx(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, Scope{}));
          const auto resolved_gen =
              ResolveGenericParamsOpt(modal_ctx, node.generic_params);
          if (!resolved_gen.ok) {
            return {false, resolved_gen.diag_id, resolved_gen.span, {}};
          }
          out.generic_params = resolved_gen.value;
          const auto resolved_where =
              ResolveWhereClauseOpt(modal_ctx, node.predicate_clause_opt);
          if (!resolved_where.ok) {
            return {false, resolved_where.diag_id, resolved_where.span, {}};
          }
          out.predicate_clause_opt = resolved_where.value;
          const auto impls = ResolveClassPathList(modal_ctx, node.implements);
          if (!impls.ok) {
            return {false, impls.diag_id, impls.span, {}};
          }
          out.implements = impls.value;
          const auto invariant =
              ResolveInvariantOpt(modal_ctx, node.invariant_opt);
          if (!invariant.ok) {
            return {false, invariant.diag_id, invariant.span, {}};
          }
          out.invariant_opt = invariant.value;
          const auto states = ResolveStateBlockList(modal_ctx, node.states);
          if (!states.ok) {
            return {false, states.diag_id, states.span, {},
                    states.diag_detail, states.diag_children};
          }
          out.states = states.value;
          SPEC_RULE("ResolveItem-Modal");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          auto out = node;
          ResolveContext class_ctx = ctx;
          ScopedScopesOverride class_scope_ctx(
              *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, Scope{}));
          const auto resolved_gen =
              ResolveGenericParamsOpt(class_ctx, node.generic_params);
          if (!resolved_gen.ok) {
            return {false, resolved_gen.diag_id, resolved_gen.span, {}};
          }
          out.generic_params = resolved_gen.value;
          const auto resolved_where =
              ResolveWhereClauseOpt(class_ctx, node.predicate_clause_opt);
          if (!resolved_where.ok) {
            return {false, resolved_where.diag_id, resolved_where.span, {}};
          }
          out.predicate_clause_opt = resolved_where.value;
          const auto supers = ResolveClassPathList(class_ctx, node.supers);
          if (!supers.ok) {
            return {false, supers.diag_id, supers.span, {}};
          }
          out.supers = supers.value;
          const auto items = ResolveClassItemList(class_ctx, node.items);
          if (!items.ok) {
            return {false, items.diag_id, items.span, {},
                    items.diag_detail, items.diag_children};
          }
          out.items = items.value;
          SPEC_RULE("ResolveItem-Class");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          auto out = node;
          // Resolve each extern procedure declaration
          std::vector<ast::ExternItem> resolved_items;
          for (const auto& ext_item : node.items) {
            bool ext_ok = true;
            std::optional<std::string_view> ext_diag_id;
            std::optional<core::Span> ext_span;
            std::visit(
                [&](const auto& ext) {
                  using IT = std::decay_t<decltype(ext)>;
                  if constexpr (std::is_same_v<IT, ast::ExternProcDecl>) {
                    auto proc_out = ext;
                    Scope proc_scope;
                    for (const auto& param : ext.params) {
                      AddParamToScope(ctx, proc_scope, param);
                    }
                    ScopedScopesOverride proc_scopes(
                        *ctx.ctx,
                        MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
                    ResolveContext proc_ctx = ctx;

                    const auto resolved_gen =
                        ResolveGenericParamsOpt(proc_ctx, ext.generic_params);
                    if (!resolved_gen.ok) {
                      ext_ok = false;
                      ext_diag_id = resolved_gen.diag_id;
                      ext_span = resolved_gen.span;
                      return;
                    }
                    proc_out.generic_params = resolved_gen.value;

                    const auto resolved_where =
                        ResolveWhereClauseOpt(proc_ctx, ext.where_clause);
                    if (!resolved_where.ok) {
                      ext_ok = false;
                      ext_diag_id = resolved_where.diag_id;
                      ext_span = resolved_where.span;
                      return;
                    }
                    proc_out.where_clause = resolved_where.value;

                    const auto resolved_params = ResolveParams(proc_ctx, ext.params);
                    if (!resolved_params.ok) {
                      ext_ok = false;
                      ext_diag_id = resolved_params.diag_id;
                      ext_span = resolved_params.span;
                      return;
                    }
                    proc_out.params = resolved_params.value;

                    const auto resolved_ret =
                        ResolveTypeOpt(proc_ctx, ext.return_type_opt);
                    if (!resolved_ret.ok) {
                      ext_ok = false;
                      ext_diag_id = resolved_ret.diag_id;
                      ext_span = resolved_ret.span;
                      return;
                    }
                    if (resolved_ret.value) {
                      proc_out.return_type_opt = resolved_ret.value;
                    }
                    resolved_items.push_back(proc_out);
                  }
                },
                ext_item);
            if (!ext_ok) {
              return {false, ext_diag_id, ext_span, {}};
            }
          }
          out.items = resolved_items;
          SPEC_RULE("ResolveItem-ExternBlock");
          return {true, std::nullopt, std::nullopt, out};
        } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
          // Import declarations are resolved during module loading
          SPEC_RULE("ResolveItem-Import");
          return {true, std::nullopt, std::nullopt, node};
        } else {
          SPEC_RULE("ResolveItem-Error");
          return {true, std::nullopt, std::nullopt, node};
        }
      },
      item);
}

}  // namespace cursive::analysis
