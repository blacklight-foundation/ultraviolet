// ===========================================================================
// ast_attributes.h - Attribute AST node definitions
// ===========================================================================
//
// PURPOSE:
//   Attribute AST node definitions for the [[attr]] annotation system used
//   on declarations and expressions. This is a UVX extension.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.2.2 - Attributes (Lines 2671-2678)
//
//   Attribute syntax: [[name]] or [[name(arg, key: value, key: ident)]]
//
//   Standard attributes:
//   - [[inline]], [[inline(always)]], [[inline(never)]], [[inline(default)]]
//   - [[cold]]
//   - [[export]]
//   - [[mangle(none)]], [[mangle("name")]]
//   - [[unwind]]
//   - [[layout(C)]]
//   - [[ffi_pass_by_value]]
//   - [[weak]]
//   - [[dynamic]]
//
// ===========================================================================

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast_common.h"

namespace ultraviolet::ast {

// ===========================================================================
// Attribute Argument
// ===========================================================================
//
// Represents a single argument to an attribute. Arguments can be:
//
//   1. Positional token literals:
//      [[inline(always)]]  -> args[0] = {key: none, value: Token("always")}
//      [[mangle("name")]]  -> args[0] = {key: none, value: Token("name")}
//
//   2. Named token literals:
//      [[foo(bar: "baz")]] -> args[0] = {key: "bar", value: Token("baz")}
//
//   3. Named token identifiers:
//      [[foo(mode: ready)]] -> args[0] = {key: "mode", value: Token("ready")}
//
//   4. Nested named-call arguments:
//      [[layout(align(16))]] -> args[0] = {key: "align", value: [Token("16")]}
//
struct AttributeArg {
  std::optional<Identifier> key;       // Named arg: key:value or key(args) (nullopt if positional)
  std::variant<Token, std::vector<AttributeArg>> value;  // Token literal/identifier or nested args
};

// ===========================================================================
// Attribute Name
// ===========================================================================
//
// Attribute names preserve the normalized spec form while also caching the
// canonical flattened text spelling used by existing lookup code.
//
// Examples:
//   [[inline]]            -> vendor_prefix_opt = null,      leaf_name = "inline"
//   [[vendor::inline]]    -> vendor_prefix_opt = ["vendor"], leaf_name = "inline"
//   [[v1::v2::attr]]      -> vendor_prefix_opt = ["v1", "v2"], leaf_name = "attr"
//
using VendorPrefix = std::vector<Identifier>;

struct AttrName {
  std::optional<VendorPrefix> vendor_prefix_opt;
  Identifier leaf_name;

  // Cached canonical spelling used by string-based attribute lookup.
  Identifier full_name;

  std::size_t rfind(std::string_view needle,
                    std::size_t pos = Identifier::npos) const {
    return full_name.rfind(needle, pos);
  }

  operator std::string_view() const { return full_name; }
  operator const Identifier&() const { return full_name; }

  friend bool operator==(const AttrName& lhs, std::string_view rhs) {
    return lhs.full_name == rhs;
  }
  friend bool operator==(std::string_view lhs, const AttrName& rhs) {
    return rhs == lhs;
  }
  friend bool operator!=(const AttrName& lhs, std::string_view rhs) {
    return !(lhs == rhs);
  }
  friend bool operator!=(std::string_view lhs, const AttrName& rhs) {
    return !(lhs == rhs);
  }
  friend std::string operator+(const char* lhs, const AttrName& rhs) {
    return std::string(lhs) + rhs.full_name;
  }
  friend std::string operator+(std::string lhs, const AttrName& rhs) {
    lhs += rhs.full_name;
    return lhs;
  }
  friend std::string operator+(const AttrName& lhs, const char* rhs) {
    return lhs.full_name + std::string(rhs);
  }
  friend std::string operator+(const AttrName& lhs, std::string_view rhs) {
    return lhs.full_name + std::string(rhs);
  }
};

// ===========================================================================
// Attribute Item
// ===========================================================================
//
// Represents a single attribute: [[name]] or [[name(args)]]
//
// Examples:
//   [[inline]]            -> name: "inline", args: []
//   [[inline(always)]]    -> name: "inline", args: [{key: none, value: "always"}]
//   [[mangle("my_func")]] -> name: "mangle", args: [{key: none, value: "my_func"}]
//   [[layout(C)]]         -> name: "layout", args: [{key: none, value: "C"}]
//
struct AttributeItem {
  AttrName name;
  std::vector<AttributeArg> args;
  ultraviolet::core::Span span;
};

// ===========================================================================
// Attribute List
// ===========================================================================
//
// A list of attributes attached to a declaration or expression.
// Multiple attributes can appear in a single [[...]] or in separate brackets.
//
// Examples:
//   [[inline, cold]]       -> Two attributes in one bracket
//   [[inline]] [[cold]]    -> Two separate attribute brackets (same result)
//
using AttributeList = std::vector<AttributeItem>;
using AttrOpt = std::optional<AttributeList>;

// ===========================================================================
// Helper Functions
// ===========================================================================

inline const AttributeList& EmptyAttributeList() {
  static const AttributeList kEmpty;
  return kEmpty;
}

inline const AttributeList& AttrListOf(const AttrOpt& attrs_opt) {
  if (attrs_opt.has_value()) {
    return *attrs_opt;
  }
  return EmptyAttributeList();
}

// Check if an attribute with the given name exists in the list.
// Returns true if found, false otherwise.
inline bool has_attribute(const AttributeList& attrs, std::string_view name) {
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      return true;
    }
  }
  return false;
}

// Find an attribute by name in the list.
// Returns a pointer to the AttributeItem if found, nullptr otherwise.
inline const AttributeItem* find_attribute(const AttributeList& attrs, std::string_view name) {
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      return &attr;
    }
  }
  return nullptr;
}

// Get a token argument from an attribute by index.
// Returns the token if the argument exists and is a Token, nullopt otherwise.
inline std::optional<Token> get_attr_token_arg(const AttributeItem& attr, size_t index) {
  if (index >= attr.args.size()) {
    return std::nullopt;
  }
  if (auto* token = std::get_if<Token>(&attr.args[index].value)) {
    return *token;
  }
  return std::nullopt;
}

// Get a token argument from an attribute by key name.
// Returns the token if a named argument with that key exists and is a Token, nullopt otherwise.
inline std::optional<Token> get_attr_token_arg(const AttributeItem& attr, std::string_view key) {
  for (const auto& arg : attr.args) {
    if (arg.key.has_value() && *arg.key == key) {
      if (auto* token = std::get_if<Token>(&arg.value)) {
        return *token;
      }
    }
  }
  return std::nullopt;
}

inline AttributeList AttrByName(const AttributeList& attrs, std::string_view name) {
  AttributeList matches;
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      matches.push_back(attr);
    }
  }
  return matches;
}

}  // namespace ultraviolet::ast
