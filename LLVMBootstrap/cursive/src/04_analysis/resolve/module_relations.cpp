// =============================================================================
// module_relations.cpp - Module Relation Queries
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §12735-12736
//     ImplModule(T) = p ⇔ T is declared in ASTModule(P, p)
//     ClassModule(Cl) = p ⇔ Cl is declared in ASTModule(P, p)
//
// =============================================================================

#include "04_analysis/resolve/module_relations.h"

#include <type_traits>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsModuleRelations() {
  SPEC_DEF("ImplModule", "12.3.1");
  SPEC_DEF("ClassModule", "12.3.1");
}

}  // namespace

std::optional<ast::ModulePath> ImplModule(const Sigma& sigma,
                                          std::string_view type_name) {
  SpecDefsModuleRelations();
  const auto key = IdKeyOf(type_name);
  for (const auto& mod : sigma.mods) {
    for (const auto& item : mod.items) {
      const bool matches = std::visit(
          [&](const auto& node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::RecordDecl> ||
                          std::is_same_v<T, ast::EnumDecl> ||
                          std::is_same_v<T, ast::ModalDecl> ||
                          std::is_same_v<T, ast::TypeAliasDecl>) {
              return IdEq(node.name, type_name);
            } else {
              return false;
            }
          },
          item);
      if (matches) {
        SPEC_RULE("ImplModule");
        return mod.path;
      }
    }
  }
  return std::nullopt;
}

std::optional<ast::ModulePath> ClassModule(const Sigma& sigma,
                                           std::string_view class_name) {
  SpecDefsModuleRelations();
  for (const auto& mod : sigma.mods) {
    for (const auto& item : mod.items) {
      if (const auto* cls = std::get_if<ast::ClassDecl>(&item)) {
        if (IdEq(cls->name, class_name)) {
          SPEC_RULE("ClassModule");
          return mod.path;
        }
      }
    }
  }
  return std::nullopt;
}

}  // namespace cursive::analysis
