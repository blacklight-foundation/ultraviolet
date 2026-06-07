#include "04_analysis/typing/ffi_by_value.h"

#include <optional>
#include <string>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

void RecordFfiByValueConformance(bool requires_drop, bool has_explicit_attr) {
  if (!core::Conformance::Enabled()) {
    return;
  }

  std::string payload = "source=FfiByValueOk;requires_drop=";
  payload += requires_drop ? "true" : "false";
  payload += ";ffi_pass_by_value_attr=";
  payload += has_explicit_attr ? "true" : "false";
  payload += ";allowed=";
  payload += (!requires_drop || has_explicit_attr) ? "true" : "false";
  core::Conformance::Record(
      "requirement.23.FfiSafeRaiiByValueRule", std::nullopt, payload);
  core::Conformance::Record(
      "requirement.23.FfiPassByValueAttributeSemantics", std::nullopt, payload);
}

bool HasFfiPassByValueNominalAttr(const ScopeContext& ctx,
                                  const TypePath& type_path) {
  ast::Path ast_path;
  ast_path.reserve(type_path.size());
  for (const auto& seg : type_path) {
    ast_path.push_back(seg);
  }
  const auto it = ctx.sigma.types.find(PathKeyOf(ast_path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  return std::visit(
      [](const auto& decl) {
        using D = std::decay_t<decltype(decl)>;
        if constexpr (std::is_same_v<D, ast::RecordDecl> ||
                      std::is_same_v<D, ast::EnumDecl>) {
          return HasAttribute(decl.attrs, attrs::kFfiPassByValue);
        }
        return false;
      },
      it->second);
}

bool HasFfiPassByValueTypeAttr(const ScopeContext& ctx, const TypeRef& type) {
  if (!type) {
    return false;
  }

  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return HasFfiPassByValueTypeAttr(ctx, perm->base);
  }
  if (const auto* refine = std::get_if<TypeRefine>(&type->node)) {
    return HasFfiPassByValueTypeAttr(ctx, refine->base);
  }
  if (const auto* apply = std::get_if<TypeApply>(&type->node)) {
    return HasFfiPassByValueNominalAttr(ctx, apply->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&type->node)) {
    return HasFfiPassByValueNominalAttr(ctx, path->path);
  }

  return false;
}

bool TypeRequiresDropForFfi(const ScopeContext& ctx, const TypeRef& type) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          return TypeRequiresDropForFfi(ctx, node.base);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return TypeRequiresDropForFfi(ctx, node.base);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (TypeRequiresDropForFfi(ctx, elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return TypeRequiresDropForFfi(ctx, node.element);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          return node.state == StringState::Managed;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          return node.state == BytesState::Managed;
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          ast::Path ast_path;
          ast_path.reserve(node.path.size());
          for (const auto& seg : node.path) {
            ast_path.push_back(seg);
          }
          const auto it = ctx.sigma.types.find(PathKeyOf(ast_path));
          if (it == ctx.sigma.types.end()) {
            return DropType(ctx, type);
          }
          return std::visit(
              [&](const auto& decl) -> bool {
                using D = std::decay_t<decltype(decl)>;
                if constexpr (std::is_same_v<D, ast::RecordDecl>) {
                  for (const auto& member : decl.members) {
                    const auto* field = std::get_if<ast::FieldDecl>(&member);
                    if (!field || !field->type) {
                      continue;
                    }
                    const auto lowered = LowerType(ctx, field->type);
                    if (lowered.ok && TypeRequiresDropForFfi(ctx, lowered.type)) {
                      return true;
                    }
                  }
                  return DropType(ctx, type);
                } else if constexpr (std::is_same_v<D, ast::EnumDecl>) {
                  for (const auto& variant : decl.variants) {
                    if (!variant.payload_opt.has_value()) {
                      continue;
                    }
                    bool needs_drop = false;
                    std::visit(
                        [&](const auto& payload) {
                          using P = std::decay_t<decltype(payload)>;
                          if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
                            for (const auto& elem : payload.elements) {
                              const auto lowered = LowerType(ctx, elem);
                              if (lowered.ok &&
                                  TypeRequiresDropForFfi(ctx, lowered.type)) {
                                needs_drop = true;
                                return;
                              }
                            }
                          } else if constexpr (std::is_same_v<
                                                   P, ast::VariantPayloadRecord>) {
                            for (const auto& field : payload.fields) {
                              const auto lowered = LowerType(ctx, field.type);
                              if (lowered.ok &&
                                  TypeRequiresDropForFfi(ctx, lowered.type)) {
                                needs_drop = true;
                                return;
                              }
                            }
                          }
                        },
                        *variant.payload_opt);
                    if (needs_drop) {
                      return true;
                    }
                  }
                  return DropType(ctx, type);
                }
                return DropType(ctx, type);
              },
              it->second);
        } else {
          return DropType(ctx, type);
        }
      },
      type->node);
}

}  // namespace

bool FfiByValueOk(const ScopeContext& ctx, const TypeRef& type) {
  if (!type) {
    return false;
  }
  const bool requires_drop = TypeRequiresDropForFfi(ctx, type);
  if (!requires_drop) {
    RecordFfiByValueConformance(false, false);
    return true;
  }
  const bool has_explicit_attr = HasFfiPassByValueTypeAttr(ctx, type);
  RecordFfiByValueConformance(true, has_explicit_attr);
  return has_explicit_attr;
}

}  // namespace ultraviolet::analysis
