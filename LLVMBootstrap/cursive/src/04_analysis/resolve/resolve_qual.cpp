// =============================================================================
// resolve_qual.cpp - Qualified Name Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §5.1.6 "Qualified Disambiguation" (Lines 7310-7429)
//   CursiveSpecification.md §5.1.3 "Lookup" (Lines 6822-6899)
//
// SOURCE FILE:
//   Migrated from cursive-bootstrap/src/03_analysis/resolve/resolve_qual.cpp
//
// =============================================================================

#include "04_analysis/resolve/resolve_qual.h"

#include <utility>

#include "00_core/assert_spec.h"
#include "01_project/language_profile.h"
#include "00_core/symbols.h"
#include "04_analysis/language_service/facts.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/memory/string_bytes.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsResolveQual() {
  SPEC_DEF("ResolveQualifiedForm", "5.1.6");
  SPEC_DEF("ResolveArgs", "5.1.6");
  SPEC_DEF("ResolveFieldInits", "5.1.6");
  SPEC_DEF("ResolveRecordPath", "5.1.6");
  SPEC_DEF("ResolveEnumUnit", "5.1.6");
  SPEC_DEF("ResolveEnumTuple", "5.1.6");
  SPEC_DEF("ResolveEnumRecord", "5.1.6");
  SPEC_DEF("ResolvePathJudg", "5.1.6");
  SPEC_DEF("PathOfModule", "3.4.1");
  SPEC_DEF("SplitLast", "5.1.5");
  SPEC_DEF("FullPath", "5.12");
  SPEC_DEF("ArgsExprs", "9.3");
}

bool IsRuntimePanicPath(const ast::Path& path, std::string_view name) {
  return path.size() == 2 &&
         IdEq(path[0], project::ActiveLanguageProfile().runtime_root) &&
         IdEq(path[1], "runtime") && name == "panic";
}

ast::ExprPtr MakeExpr(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

std::optional<std::pair<ast::ModulePath, ast::Identifier>> SplitLast(
    const ast::Path& path) {
  SpecDefsResolveQual();
  if (path.size() < 2) {
    return std::nullopt;
  }
  ast::ModulePath prefix(path.begin(), path.end() - 1);
  return std::make_pair(prefix, path.back());
}

ast::Path FullPath(const ast::Path& path, std::string_view name) {
  SpecDefsResolveQual();
  ast::Path out = path;
  out.emplace_back(name);
  return out;
}

std::vector<ast::ExprPtr> ArgsExprs(const std::vector<ast::Arg>& args) {
  SpecDefsResolveQual();
  std::vector<ast::ExprPtr> out;
  out.reserve(args.size());
  for (const auto& arg : args) {
    out.push_back(arg.value);
  }
  return out;
}

const ast::RecordDecl* FindRecordDecl(const ScopeContext& ctx,
                                      const ast::TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::RecordDecl>(&it->second);
}

const ast::EnumDecl* FindEnumDecl(const ScopeContext& ctx,
                                  const ast::TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::EnumDecl>(&it->second);
}

const ast::VariantDecl* FindVariant(const ast::EnumDecl& decl,
                                    std::string_view name) {
  for (const auto& variant : decl.variants) {
    if (IdEq(variant.name, name)) {
      return &variant;
    }
  }
  return nullptr;
}

enum class VariantKind {
  Unit,
  Tuple,
  Record,
};

std::optional<VariantKind> VariantPayloadKind(const ast::VariantDecl& variant) {
  if (!variant.payload_opt) {
    return VariantKind::Unit;
  }
  if (std::holds_alternative<ast::VariantPayloadTuple>(*variant.payload_opt)) {
    return VariantKind::Tuple;
  }
  if (std::holds_alternative<ast::VariantPayloadRecord>(*variant.payload_opt)) {
    return VariantKind::Record;
  }
  return std::nullopt;
}

void RecordQualifiedValueReference(const ResolveQualContext& ctx,
                                   const ast::Expr& expr,
                                   std::string_view fallback_name,
                                   const Entity& entity) {
  if (ctx.ctx == nullptr) {
    return;
  }
  RecordLanguageServiceReference(ctx.language_service, *ctx.ctx, fallback_name,
                                 expr.span, entity);
}

void RecordQualifiedTypeReference(const ResolveQualContext& ctx,
                                  const ast::Expr& expr,
                                  const ast::TypePath& path) {
  if (ctx.ctx == nullptr) {
    return;
  }
  RecordLanguageServiceTypePathReference(ctx.language_service, *ctx.ctx, path,
                                         expr.span);
}

void RecordQualifiedMemberReference(const ResolveQualContext& ctx,
                                    const ast::Expr& expr,
                                    const ast::TypePath& owner_path,
                                    std::string_view member_name) {
  RecordLanguageServiceMemberReference(ctx.language_service, owner_path,
                                       member_name, expr.span);
}

}  // namespace

ResolveArgsResult ResolveArgs(const ResolveQualContext& ctx,
                              const std::vector<ast::Arg>& args) {
  SpecDefsResolveQual();
  if (args.empty()) {
    SPEC_RULE("ResolveArgs-Empty");
    return {true, std::nullopt, {}};
  }
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names || !ctx.resolve_expr) {
    return {false, std::nullopt, {}};
  }
  std::vector<ast::Arg> out;
  out.reserve(args.size());
  for (const auto& arg : args) {
    const auto resolved =
        ctx.resolve_expr(*ctx.ctx, *ctx.name_maps, *ctx.module_names,
                         ctx.language_service, arg.value);
    if (!resolved.ok) {
      return {false, resolved.diag_id, {}};
    }
    ast::Arg next = arg;
    next.value = resolved.expr;
    out.push_back(std::move(next));
    SPEC_RULE("ResolveArgs-Cons");
  }
  return {true, std::nullopt, std::move(out)};
}

ResolveFieldInitsResult ResolveFieldInits(
    const ResolveQualContext& ctx,
    const std::vector<ast::FieldInit>& fields) {
  SpecDefsResolveQual();
  if (fields.empty()) {
    SPEC_RULE("ResolveFieldInits-Empty");
    return {true, std::nullopt, {}};
  }
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names || !ctx.resolve_expr) {
    return {false, std::nullopt, {}};
  }
  std::vector<ast::FieldInit> out;
  out.reserve(fields.size());
  for (const auto& field : fields) {
    const auto resolved =
        ctx.resolve_expr(*ctx.ctx, *ctx.name_maps, *ctx.module_names,
                         ctx.language_service, field.value);
    if (!resolved.ok) {
      return {false, resolved.diag_id, {}};
    }
    ast::FieldInit next = field;
    next.value = resolved.expr;
    out.push_back(std::move(next));
    SPEC_RULE("ResolveFieldInits-Cons");
  }
  return {true, std::nullopt, std::move(out)};
}

ResolveRecordPathResult ResolveRecordPath(const ResolveQualContext& ctx,
                                          const ast::ModulePath& path,
                                          std::string_view name) {
  SpecDefsResolveQual();
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names || !ctx.resolve_type_path) {
    return {false, std::nullopt, {}};
  }
  const auto full = FullPath(path, name);
  const auto resolved =
      ctx.resolve_type_path(*ctx.ctx, *ctx.name_maps, *ctx.module_names, full);
  if (!resolved.ok) {
    return {false, std::nullopt, {}};
  }
  if (!FindRecordDecl(*ctx.ctx, resolved.path)) {
    return {false, std::nullopt, {}};
  }
  SPEC_RULE("Resolve-RecordPath");
  return {true, std::nullopt, resolved.path};
}

ResolveEnumPathResult ResolveEnumUnit(const ResolveQualContext& ctx,
                                      const ast::ModulePath& path,
                                      std::string_view name) {
  SpecDefsResolveQual();
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names || !ctx.resolve_type_path) {
    return {false, std::nullopt, {}};
  }
  const auto resolved =
      ctx.resolve_type_path(*ctx.ctx, *ctx.name_maps, *ctx.module_names, path);
  if (!resolved.ok) {
    return {false, std::nullopt, {}};
  }
  const auto* decl = FindEnumDecl(*ctx.ctx, resolved.path);
  if (!decl) {
    return {false, std::nullopt, {}};
  }
  const auto* variant = FindVariant(*decl, name);
  if (!variant) {
    return {false, std::nullopt, {}};
  }
  const auto kind = VariantPayloadKind(*variant);
  if (!kind || *kind != VariantKind::Unit) {
    return {false, std::nullopt, {}};
  }
  SPEC_RULE("Resolve-EnumUnit");
  return {true, std::nullopt, resolved.path};
}

ResolveEnumPathResult ResolveEnumTuple(const ResolveQualContext& ctx,
                                       const ast::ModulePath& path,
                                       std::string_view name) {
  SpecDefsResolveQual();
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names || !ctx.resolve_type_path) {
    return {false, std::nullopt, {}};
  }
  const auto resolved =
      ctx.resolve_type_path(*ctx.ctx, *ctx.name_maps, *ctx.module_names, path);
  if (!resolved.ok) {
    return {false, std::nullopt, {}};
  }
  const auto* decl = FindEnumDecl(*ctx.ctx, resolved.path);
  if (!decl) {
    return {false, std::nullopt, {}};
  }
  const auto* variant = FindVariant(*decl, name);
  if (!variant) {
    return {false, std::nullopt, {}};
  }
  const auto kind = VariantPayloadKind(*variant);
  if (!kind || *kind != VariantKind::Tuple) {
    return {false, std::nullopt, {}};
  }
  SPEC_RULE("Resolve-EnumTuple");
  return {true, std::nullopt, resolved.path};
}

ResolveEnumPathResult ResolveEnumRecord(const ResolveQualContext& ctx,
                                        const ast::ModulePath& path,
                                        std::string_view name) {
  SpecDefsResolveQual();
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names || !ctx.resolve_type_path) {
    return {false, std::nullopt, {}};
  }
  const auto resolved =
      ctx.resolve_type_path(*ctx.ctx, *ctx.name_maps, *ctx.module_names, path);
  if (!resolved.ok) {
    return {false, std::nullopt, {}};
  }
  const auto* decl = FindEnumDecl(*ctx.ctx, resolved.path);
  if (!decl) {
    return {false, std::nullopt, {}};
  }
  const auto* variant = FindVariant(*decl, name);
  if (!variant) {
    return {false, std::nullopt, {}};
  }
  const auto kind = VariantPayloadKind(*variant);
  if (!kind || *kind != VariantKind::Record) {
    return {false, std::nullopt, {}};
  }
  SPEC_RULE("Resolve-EnumRecord");
  return {true, std::nullopt, resolved.path};
}

ResolveQualifiedFormResult ResolveQualifiedForm(const ResolveQualContext& ctx,
                                                const ast::Expr& expr) {
  SpecDefsResolveQual();
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names) {
    return {false, std::nullopt, {}};
  }
  return std::visit(
      [&](const auto& node) -> ResolveQualifiedFormResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          if (IsStringBytesBuiltinPath(node.path)) {
            const bool is_string = node.path.size() == 1 &&
                                   IdEq(node.path[0], "string");
            const bool is_bytes = node.path.size() == 1 &&
                                  IdEq(node.path[0], "bytes");
            if ((IsStringBuiltinName(node.name) && is_string) ||
                (IsBytesBuiltinName(node.name) && is_bytes)) {
              ast::PathExpr path;
              path.path = node.path;
              path.name = node.name;
              SPEC_RULE("ResolveQual-Name-Value");
              return {true, std::nullopt, MakeExpr(expr.span, path)};
            }
            SPEC_RULE("ResolveQual-Name-Err");
            return {false, "ResolveExpr-Ident-Err", {}};
          }
          if (IsRuntimePanicPath(node.path, node.name)) {
            ast::PathExpr path;
            path.path = node.path;
            path.name = node.name;
            SPEC_RULE("ResolveQual-Name-Value");
            return {true, std::nullopt, MakeExpr(expr.span, path)};
          }
          TypePath modal_path;
          modal_path.reserve(node.path.size());
          for (const auto& seg : node.path) {
            modal_path.push_back(seg);
          }
          if (IsBuiltinModalTypePath(modal_path) &&
              IsBuiltinModalMemberName(modal_path, node.name)) {
            ast::PathExpr path;
            path.path = node.path;
            path.name = node.name;
            SPEC_RULE("ResolveQual-Name-Value");
            return {true, std::nullopt, MakeExpr(expr.span, path)};
          }
          const auto value = ResolveQualified(
              *ctx.ctx, *ctx.name_maps, *ctx.module_names, node.path, node.name,
              EntityKind::Value, ctx.can_access);
          if (value.ok && value.entity && value.entity->origin_opt) {
            const auto& ent = *value.entity;
            RecordQualifiedValueReference(ctx, expr, node.name, ent);
            const ast::Identifier name =
                ent.target_opt ? *ent.target_opt
                               : ast::Identifier(node.name);
            ast::PathExpr path;
            path.path = *ent.origin_opt;
            path.name = name;
            SPEC_RULE("ResolveQual-Name-Value");
            return {true, std::nullopt, MakeExpr(expr.span, path)};
          }
          const auto record = ResolveRecordPath(ctx, node.path, node.name);
          if (record.ok) {
            RecordQualifiedTypeReference(ctx, expr, record.path);
            const auto split = SplitLast(record.path);
            if (!split) {
              return {false, std::nullopt, {}};
            }
            ast::PathExpr path;
            path.path = split->first;
            path.name = split->second;
            SPEC_RULE("ResolveQual-Name-Record");
            return {true, std::nullopt, MakeExpr(expr.span, path)};
          }
          const auto unit = ResolveEnumUnit(ctx, node.path, node.name);
          if (unit.ok) {
            RecordQualifiedMemberReference(ctx, expr, unit.path, node.name);
            ast::EnumLiteralExpr literal;
            literal.path = FullPath(unit.path, node.name);
            literal.payload_opt = std::nullopt;
            SPEC_RULE("ResolveQual-Name-Enum");
            return {true, std::nullopt, MakeExpr(expr.span, literal)};
          }
          SPEC_RULE("ResolveQual-Name-Err");
          return {false, "ResolveExpr-Ident-Err", {}};
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            const auto resolved_args =
                ResolveArgs(ctx, std::get<ast::ParenArgs>(node.args).args);
            if (!resolved_args.ok) {
              return {false, resolved_args.diag_id, {}};
            }
            if (IsStringBytesBuiltinPath(node.path)) {
              const bool is_string = node.path.size() == 1 &&
                                     IdEq(node.path[0], "string");
              const bool is_bytes = node.path.size() == 1 &&
                                    IdEq(node.path[0], "bytes");
              if ((IsStringBuiltinName(node.name) && is_string) ||
                  (IsBytesBuiltinName(node.name) && is_bytes)) {
                ast::PathExpr path;
                path.path = node.path;
                path.name = node.name;
                ast::CallExpr call;
                call.callee = MakeExpr(expr.span, path);
                call.args = resolved_args.args;
                SPEC_RULE("ResolveQual-Apply-Value");
                return {true, std::nullopt, MakeExpr(expr.span, call)};
              }
              SPEC_RULE("ResolveQual-Apply-Err");
              return {false, "ResolveExpr-Ident-Err", {}};
            }
            if (IsRuntimePanicPath(node.path, node.name)) {
              ast::PathExpr path;
              path.path = node.path;
              path.name = node.name;
              ast::CallExpr call;
              call.callee = MakeExpr(expr.span, path);
              call.args = resolved_args.args;
              SPEC_RULE("ResolveQual-Apply-Value");
              return {true, std::nullopt, MakeExpr(expr.span, call)};
            }
            TypePath modal_path;
            modal_path.reserve(node.path.size());
            for (const auto& seg : node.path) {
              modal_path.push_back(seg);
            }
            if (IsBuiltinModalTypePath(modal_path) &&
                IsBuiltinModalMemberName(modal_path, node.name)) {
              ast::PathExpr path;
              path.path = node.path;
              path.name = node.name;
              ast::CallExpr call;
              call.callee = MakeExpr(expr.span, path);
              call.args = resolved_args.args;
              SPEC_RULE("ResolveQual-Apply-Value");
              return {true, std::nullopt, MakeExpr(expr.span, call)};
            }
            const auto value = ResolveQualified(
                *ctx.ctx, *ctx.name_maps, *ctx.module_names, node.path,
                node.name, EntityKind::Value, ctx.can_access);
            if (value.ok && value.entity && value.entity->origin_opt) {
              const auto& ent = *value.entity;
              RecordQualifiedValueReference(ctx, expr, node.name, ent);
              const ast::Identifier name =
                  ent.target_opt ? *ent.target_opt
                                 : ast::Identifier(node.name);
              ast::PathExpr path;
              path.path = *ent.origin_opt;
              path.name = name;
              ast::CallExpr call;
              call.callee = MakeExpr(expr.span, path);
              call.args = resolved_args.args;
              SPEC_RULE("ResolveQual-Apply-Value");
              return {true, std::nullopt, MakeExpr(expr.span, call)};
            }
            const auto record = ResolveRecordPath(ctx, node.path, node.name);
            if (record.ok) {
              RecordQualifiedTypeReference(ctx, expr, record.path);
              const auto split = SplitLast(record.path);
              if (!split) {
                return {false, std::nullopt, {}};
              }
              ast::PathExpr path;
              path.path = split->first;
              path.name = split->second;
              ast::CallExpr call;
              call.callee = MakeExpr(expr.span, path);
              call.args = resolved_args.args;
              SPEC_RULE("ResolveQual-Apply-Record");
              return {true, std::nullopt, MakeExpr(expr.span, call)};
            }
            const auto tuple = ResolveEnumTuple(ctx, node.path, node.name);
            if (tuple.ok) {
              RecordQualifiedMemberReference(ctx, expr, tuple.path, node.name);
              ast::EnumLiteralExpr literal;
              literal.path = FullPath(tuple.path, node.name);
              ast::EnumPayloadParen payload;
              payload.elements = ArgsExprs(resolved_args.args);
              literal.payload_opt = ast::EnumPayload{payload};
              SPEC_RULE("ResolveQual-Apply-Enum-Tuple");
              return {true, std::nullopt, MakeExpr(expr.span, literal)};
            }
            SPEC_RULE("ResolveQual-Apply-Err");
            return {false, "ResolveExpr-Ident-Err", {}};
          }
          if (std::holds_alternative<ast::BraceArgs>(node.args)) {
            const auto resolved_fields = ResolveFieldInits(
                ctx, std::get<ast::BraceArgs>(node.args).fields);
            if (!resolved_fields.ok) {
              return {false, resolved_fields.diag_id, {}};
            }
            const auto record = ResolveRecordPath(ctx, node.path, node.name);
            if (record.ok) {
              RecordQualifiedTypeReference(ctx, expr, record.path);
              ast::RecordExpr rec;
              rec.target = record.path;
              rec.fields = resolved_fields.fields;
              SPEC_RULE("ResolveQual-Apply-RecordLit");
              return {true, std::nullopt, MakeExpr(expr.span, rec)};
            }
            const auto record_enum = ResolveEnumRecord(ctx, node.path, node.name);
            if (record_enum.ok) {
              RecordQualifiedMemberReference(ctx, expr, record_enum.path,
                                             node.name);
              ast::EnumLiteralExpr literal;
              literal.path = FullPath(record_enum.path, node.name);
              ast::EnumPayloadBrace payload;
              payload.fields = resolved_fields.fields;
              literal.payload_opt = ast::EnumPayload{payload};
              SPEC_RULE("ResolveQual-Apply-Enum-Record");
              return {true, std::nullopt, MakeExpr(expr.span, literal)};
            }
            SPEC_RULE("ResolveQual-Apply-Brace-Err");
            return {false, "ResolveExpr-Ident-Err", {}};
          }
          return {false, std::nullopt, {}};
        } else {
          return {false, std::nullopt, {}};
        }
      },
      expr.node);
}

}  // namespace cursive::analysis
