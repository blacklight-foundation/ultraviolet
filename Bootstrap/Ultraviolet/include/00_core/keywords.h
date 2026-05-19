#pragma once

#include <array>
#include <string_view>
#include <unordered_set>

namespace ultraviolet::core
{

  namespace detail
  {
    // Helper to get a static hash set for O(1) keyword lookup
    template <std::size_t N>
    inline const std::unordered_set<std::string_view> &
    GetKeywordSet(const std::array<std::string_view, N> &keywords)
    {
      static const std::unordered_set<std::string_view> set(keywords.begin(),
                                                            keywords.end());
      return set;
    }
  } // namespace detail

  inline constexpr std::array<std::string_view, 55> kUltravioletKeywords = {
      "all",
      "as",
      "break",
      "class",
      "comptime",
      "continue",
      "copy",
      "derive",
      "dispatch", // UVX Extension: structured concurrency
      "else",
      "enum",
      "extern",
      "false",
      "defer",
      "frame",
      "from",
      "if",
      "is",
      "imm",
      "import",
      "internal",
      "let",
      "loop",
      "modal",
      "move",
      "mut",
      "null",
      "parallel", // UVX Extension: structured concurrency
      "private",
      "procedure",
      "public",
      "quote",
      "race",
      "record",
      "region",
      "return",
      "shared",
      "spawn", // UVX Extension: structured concurrency
      "sync",
      "transition",
      "transmute",
      "true",
      "type",
      "unique",
      "unsafe",
      "var",
      "where", // UVX Extension: generics
      "widen",
      "using",
      "yield",
      "const",
      "override",
  };

  // §3.3.4 Fixed identifiers - these are identifiers, not keywords
  // They have special meaning in specific contexts only
  inline constexpr std::array<std::string_view, 22> kUltravioletFixedIdentifiers = {
      "read",
      "write",
      "dynamic",
      "speculative",
      "release",
      "cancel",
      "name",
      "workgroup",
      "workgroups",
      "affinity",
      "priority",
      "reduce",
      "ordered",
      "chunk",
      "min",
      "max",
      "and",
      "or",
      "pattern",
      "target",
      "requires",
      "emits",
  };

  inline bool IsFixedIdentifier(std::string_view s)
  {
    const auto &set = detail::GetKeywordSet(kUltravioletFixedIdentifiers);
    return set.find(s) != set.end();
  }

  inline constexpr std::array<std::string_view, 47> kUltravioletOperators = {
      "+",
      "-",
      "*",
      "/",
      "%",
      "**",
      "==",
      "!=",
      "<",
      "<=",
      ">",
      ">=",
      "&&",
      "||",
      "!",
      "&",
      "|",
      "^",
      "<<",
      ">>",
      "=",
      "+=",
      "-=",
      "*=",
      "/=",
      "%=",
      "&=",
      "|:",
      "|=",
      "^=",
      "<<=",
      ">>=",
      ":=",
      "<:",
      "..",
      "..=",
      "=>",
      "->",
      "::",
      "~",
      "~>",
      "~!",
      "~%",
      "?",
      "#",
      "@",
      "$",
  };

  inline constexpr std::array<std::string_view, 10> kUltravioletPunctuators = {
      "(",
      ")",
      "[",
      "]",
      "{",
      "}",
      ",",
      ":",
      ";",
      ".",
  };

  inline bool IsKeyword(std::string_view s)
  {
    const auto &set = detail::GetKeywordSet(kUltravioletKeywords);
    return set.find(s) != set.end();
  }

  // Reserved identifiers that are not valid user-defined names.
  inline constexpr std::array<std::string_view, 24> kUnsupportedLexemes = {
      "attribute",
      "opaque_type",
      "refinement_type",
      "closure",
      "pipeline",
      "async",
      "metaprogramming",
      "Network",
      "Reactor",
      "GPUFactory",
      "CPUFactory",
      "AsyncRuntime",
      "key_system",
      "extern_block",
      "foreign_decl",
      "class_generics",
      "class_where_clause",
      "associated_type",
      "modal_class",
      "class_contract",
      "type_item",
      "abstract_state",
      "override_in_class",
  };

  inline bool IsUnsupportedLexeme(std::string_view s)
  {
    const auto &set = detail::GetKeywordSet(kUnsupportedLexemes);
    return set.find(s) != set.end();
  }

} // namespace ultraviolet::core
