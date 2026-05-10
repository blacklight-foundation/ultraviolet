// =================================================================
// File: 04_analysis/typing/type_lookup.cpp
// Construct: Type Lookup Utilities
// Spec Section: 5.2.12
// Spec Rules: Various field/record/enum lookups
// =================================================================
//
// MIGRATED FROM: cursive-bootstrap/src/03_analysis/types/type_lookup.cpp
//
// =================================================================

#include "04_analysis/typing/type_lookup.h"

#include <mutex>
#include <unordered_map>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/generics/generic_params.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_lower.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsTypeLookup() {
  SPEC_DEF("FieldVisible", "5.2.12");
  SPEC_DEF("FieldExists", "5.2.12");
  SPEC_DEF("FieldType", "5.2.12");
  SPEC_DEF("LookupRecordDecl", "5.2.12");
  SPEC_DEF("LookupEnumDecl", "5.2.12");
  SPEC_DEF("TypeParamsOf", "14.1.3");
  SPEC_DEF("TypePredicateClauseOf", "14.1.3");
}

struct RecordFieldIndex {
  std::unordered_map<std::string, const ast::FieldDecl*> by_name;
};

std::mutex g_record_field_index_mu;
std::unordered_map<const ast::RecordDecl*, RecordFieldIndex> g_record_field_index;

TypePath EntityQualifiedPath(const Entity& entity,
                             std::string_view written_name,
                             const TypePath& suffix) {
  TypePath path;
  if (entity.origin_opt.has_value()) {
    path.insert(path.end(), entity.origin_opt->begin(), entity.origin_opt->end());
  }
  path.push_back(entity.target_opt.value_or(std::string(written_name)));
  path.insert(path.end(), suffix.begin(), suffix.end());
  return path;
}

const TypeDecl* LookupTypeDeclAt(const ScopeContext& ctx,
                                 const TypePath& path,
                                 TypePath* resolved_path) {
  ast::TypePath syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& comp : path) {
    syntax_path.push_back(comp);
  }

  const auto it = ctx.sigma.types.find(PathKeyOf(syntax_path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  if (resolved_path) {
    *resolved_path = path;
  }
  return &it->second;
}

const TypeDecl* LookupScopedTypeDecl(const ScopeContext& ctx,
                                     const TypePath& path,
                                     TypePath* resolved_path) {
  if (path.empty()) {
    return nullptr;
  }

  if (path.size() == 1) {
    const auto entity = ResolveTypeName(ctx, path.front());
    if (!entity.has_value()) {
      return nullptr;
    }
    TypePath resolved;
    if (entity->origin_opt.has_value()) {
      resolved = EntityQualifiedPath(*entity, path.front(), {});
    } else {
      resolved.push_back(entity->target_opt.value_or(path.front()));
    }
    return LookupTypeDeclAt(ctx, resolved, resolved_path);
  }

  const auto module_entity = ResolveModuleName(ctx, path.front());
  if (!module_entity.has_value() || !module_entity->origin_opt.has_value()) {
    return nullptr;
  }

  TypePath suffix(path.begin() + 1, path.end());
  TypePath resolved = *module_entity->origin_opt;
  resolved.insert(resolved.end(), suffix.begin(), suffix.end());
  return LookupTypeDeclAt(ctx, resolved, resolved_path);
}

const TypeDecl* LookupModuleRelativeTypeDecl(const ScopeContext& ctx,
                                             const TypePath& path,
                                             TypePath* resolved_path) {
  if (path.empty() || ctx.current_module.empty()) {
    return nullptr;
  }

  TypePath current_qualified = ctx.current_module;
  current_qualified.insert(current_qualified.end(), path.begin(), path.end());
  if (const TypeDecl* decl =
          LookupTypeDeclAt(ctx, current_qualified, resolved_path)) {
    return decl;
  }

  if (path.size() >= 2) {
    TypePath assembly_qualified;
    assembly_qualified.push_back(ctx.current_module.front());
    assembly_qualified.insert(
        assembly_qualified.end(),
        path.begin(),
        path.end());
    if (const TypeDecl* decl =
            LookupTypeDeclAt(ctx, assembly_qualified, resolved_path)) {
      return decl;
    }
  }

  return nullptr;
}

const ast::FieldDecl* LookupFieldDeclImpl(const ast::RecordDecl& record,
                                          std::string_view field_name) {
  const auto key = IdKeyOf(field_name);
  {
    std::lock_guard<std::mutex> lock(g_record_field_index_mu);
    const auto it = g_record_field_index.find(&record);
    if (it != g_record_field_index.end()) {
      const auto field_it = it->second.by_name.find(key);
      return field_it != it->second.by_name.end() ? field_it->second : nullptr;
    }
  }

  RecordFieldIndex index;
  for (const auto& member : record.members) {
    const auto* field = std::get_if<ast::FieldDecl>(&member);
    if (!field) {
      continue;
    }
    index.by_name.emplace(IdKeyOf(field->name), field);
  }

  std::lock_guard<std::mutex> lock(g_record_field_index_mu);
  auto [it, _inserted] = g_record_field_index.emplace(&record, std::move(index));
  const auto field_it = it->second.by_name.find(key);
  return field_it != it->second.by_name.end() ? field_it->second : nullptr;
}

ScopeContext BindRecordFieldTypeScope(const ScopeContext& ctx,
                                      const ast::RecordDecl& record) {
  ScopeContext field_ctx = ctx;
  field_ctx.scopes = BindTypeParams(ctx, record.generic_params);
  return field_ctx;
}

}  // namespace

const TypeDecl* LookupTypeDecl(const ScopeContext& ctx,
                               const TypePath& path,
                               TypePath* resolved_path) {
  SpecDefsTypeLookup();
  if (const TypeDecl* decl = LookupTypeDeclAt(ctx, path, resolved_path)) {
    return decl;
  }
  if (const TypeDecl* decl = LookupScopedTypeDecl(ctx, path, resolved_path)) {
    return decl;
  }
  return LookupModuleRelativeTypeDecl(ctx, path, resolved_path);
}

const ast::RecordDecl* LookupRecordDecl(const ScopeContext& ctx,
                                        const TypePath& path) {
  SpecDefsTypeLookup();
  const TypeDecl* decl = LookupTypeDecl(ctx, path);
  if (!decl) {
    return nullptr;
  }
  return std::get_if<ast::RecordDecl>(decl);
}

const ast::EnumDecl* LookupEnumDecl(const ScopeContext& ctx,
                                    const TypePath& path) {
  SpecDefsTypeLookup();
  const TypeDecl* decl = LookupTypeDecl(ctx, path);
  if (!decl) {
    return nullptr;
  }
  return std::get_if<ast::EnumDecl>(decl);
}

const std::optional<ast::GenericParams>* TypeParamsOf(const ScopeContext& ctx,
                                                      const TypePath& path) {
  SpecDefsTypeLookup();
  const TypeDecl* decl = LookupTypeDecl(ctx, path);
  if (!decl) {
    return nullptr;
  }

  return std::visit(
      [](const auto& decl) -> const std::optional<ast::GenericParams>* {
        return &decl.generic_params;
      },
      *decl);
}

const std::optional<ast::PredicateClause>* TypePredicateClauseOf(
    const ScopeContext& ctx,
    const TypePath& path) {
  SpecDefsTypeLookup();
  const TypeDecl* decl = LookupTypeDecl(ctx, path);
  if (!decl) {
    return nullptr;
  }

  return std::visit(
      [](const auto& decl) -> const std::optional<ast::PredicateClause>* {
        return &decl.predicate_clause_opt;
      },
      *decl);
}

bool FieldExists(const ast::RecordDecl& record, std::string_view field_name) {
  SpecDefsTypeLookup();
  return LookupFieldDecl(record, field_name) != nullptr;
}

std::vector<const ast::FieldDecl*> RecordFields(const ast::RecordDecl& record) {
  std::vector<const ast::FieldDecl*> fields;
  fields.reserve(record.members.size());
  for (const auto& member : record.members) {
    if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
      fields.push_back(field);
    }
  }
  return fields;
}

const ast::FieldDecl* LookupFieldDecl(const ast::RecordDecl& record,
                                      std::string_view field_name) {
  return LookupFieldDeclImpl(record, field_name);
}

ast::Visibility FieldVis(const ast::RecordDecl& record,
                         std::string_view field_name) {
  SpecDefsTypeLookup();
  const auto* field = LookupFieldDecl(record, field_name);
  return field ? field->vis : ast::Visibility::Private;
}

bool FieldVisible(const ScopeContext& ctx,
                  const ast::RecordDecl& record,
                  std::string_view field_name,
                  const TypePath& record_path) {
  SpecDefsTypeLookup();
  const auto vis = FieldVis(record, field_name);
  if (vis == ast::Visibility::Public || vis == ast::Visibility::Internal) {
    return true;
  }
  // Protected: visible in declaring module and submodules
  // Private: visible only in declaring module
  if (record_path.empty()) {
    return false;
  }
  ast::ModulePath declaring_module = record_path;
  declaring_module.pop_back();  // Remove the record name to get the module
  const auto& current = ctx.current_module;
  if (declaring_module == current) {
    return true;  // Same module: both protected and private are visible
  }
  return false;
}

std::optional<TypeRef> FieldType(const ast::RecordDecl& record,
                                 std::string_view field_name,
                                 const ScopeContext& ctx,
                                 const std::vector<TypeRef>& generic_args) {
  SpecDefsTypeLookup();
  SPEC_RULE("Fields");
  const auto* field = LookupFieldDecl(record, field_name);
  if (!field) {
    return std::nullopt;
  }

  const auto field_ctx = BindRecordFieldTypeScope(ctx, record);
  const auto lowered = LowerType(field_ctx, field->type);
  if (!lowered.ok || !lowered.type) {
    return std::nullopt;
  }

  TypeRef field_type = lowered.type;
  if (record.generic_params.has_value()) {
    const auto& params = record.generic_params->params;
    if (generic_args.size() > params.size()) {
      return std::nullopt;
    }
    const auto subst = BuildSubstitution(params, generic_args);
    field_type = InstantiateType(field_type, subst);
  } else if (!generic_args.empty()) {
    return std::nullopt;
  }

  return field_type;
}

}  // namespace cursive::analysis
