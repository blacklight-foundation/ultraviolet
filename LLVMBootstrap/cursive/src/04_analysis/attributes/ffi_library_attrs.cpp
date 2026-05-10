#include "04_analysis/attributes/ffi_library_attrs.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace cursive::analysis {

namespace {

std::string NormalizeAttributeStringLiteral(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

}  // namespace

std::optional<project::FfiLibrarySpec> NormalizeLibraryAttribute(
    const ast::AttributeItem& attr) {
  if (attr.name != "library") {
    return std::nullopt;
  }

  std::optional<std::string> name;
  std::optional<std::string> kind;

  for (std::size_t i = 0; i < attr.args.size(); ++i) {
    const auto& arg = attr.args[i];
    if (!arg.key.has_value()) {
      return std::nullopt;
    }
    if (*arg.key != "name" && *arg.key != "kind") {
      return std::nullopt;
    }
    const auto* token = std::get_if<ast::Token>(&arg.value);
    if (!token) {
      return std::nullopt;
    }
    const std::string normalized = NormalizeAttributeStringLiteral(token->lexeme);
    if (*arg.key == "name") {
      if (name.has_value() || normalized.empty() || i != 0) {
        return std::nullopt;
      }
      name = normalized;
    } else {
      if (kind.has_value() || normalized.empty() || !name.has_value() ||
          i != 1) {
        return std::nullopt;
      }
      kind = normalized;
    }
  }
  if (!name.has_value() || name->empty()) {
    return std::nullopt;
  }

  return project::FfiLibrarySpec{*name, kind.value_or("dylib")};
}

std::vector<project::FfiLibrarySpec> CollectExternLibrarySpecs(
    const std::vector<ast::ASTModule>& modules) {
  std::vector<project::FfiLibrarySpec> out;
  std::unordered_set<std::string> seen;
  for (const auto& module : modules) {
    for (const auto& item : module.items) {
      const auto* block = std::get_if<ast::ExternBlock>(&item);
      if (!block) {
        continue;
      }
      for (const auto& attr : ast::AttrListOf(*block)) {
        const auto spec = NormalizeLibraryAttribute(attr);
        if (!spec.has_value()) {
          continue;
        }
        const std::string key = spec->kind + "|" + spec->name;
        if (!seen.insert(key).second) {
          continue;
        }
        out.push_back(*spec);
      }
    }
  }
  return out;
}

}  // namespace cursive::analysis
