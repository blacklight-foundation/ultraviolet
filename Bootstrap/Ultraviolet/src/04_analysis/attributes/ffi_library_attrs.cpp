#include "04_analysis/attributes/ffi_library_attrs.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

#include "00_core/assert_spec.h"

namespace ultraviolet::analysis {

namespace {

std::string NormalizeAttributeStringLiteral(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

void RecordLibraryAttributeConformance(const ast::AttributeItem& attr,
                                       const project::FfiLibrarySpec& spec,
                                       bool used_default_kind) {
  if (!core::Conformance::Enabled()) {
    return;
  }

  std::string payload;
  payload.reserve(spec.name.size() + spec.kind.size() + 160);
  payload += "source=NormalizeLibraryAttribute;link_kinds=dylib,static,"
             "framework,raw-dylib;name=";
  payload += spec.name;
  payload += ";kind=";
  payload += spec.kind;
  payload += ";default_kind=";
  payload += used_default_kind ? "true" : "false";
  payload += ";target=extern_block;manifest_link_kind_independent=true";

  core::Conformance::Record("def.23.LibraryLinkKinds", attr.span, payload);
  core::Conformance::Record(
      "requirement.23.LibraryAttributeSemantics", attr.span, payload);
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

  project::FfiLibrarySpec spec{*name, kind.value_or("dylib")};
  RecordLibraryAttributeConformance(attr, spec, !kind.has_value());
  return spec;
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

}  // namespace ultraviolet::analysis
