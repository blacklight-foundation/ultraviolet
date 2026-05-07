#pragma once

#include <cstddef>
#include <optional>
#include <type_traits>

#include "02_source/ast/ast.h"
#include "02_source/parser/parser.h"

namespace cursive::ast {

core::Span span_from(const lexer::Token& start, const lexer::Token& end);
core::Span span_between(const Parser& start, const Parser& end);

DocList doc_default();
std::optional<DocList> doc_opt_default();

core::Span extend_span(const core::Span& base, const lexer::Token& end);
core::Span extend_span(const core::Span& base, const core::Span& end);
core::Span span_covering(const core::Span& first, const core::Span& last);
core::Span point_span(const core::SourceFile& source,
                      std::size_t offset,
                      std::size_t line,
                      std::size_t col);

template <typename Node>
inline constexpr bool kHasSpanField = requires(const Node& node) {
  node.span;
};

template <typename Node>
inline constexpr bool kHasDocField = requires(const Node& node) {
  node.doc;
};

template <typename Node>
inline constexpr bool kHasDocOptField = requires(const Node& node) {
  node.doc_opt;
};

template <typename Node>
inline bool SpanMissing(const Node& node) {
  if constexpr (kHasSpanField<Node>) {
    return node.span.file.empty();
  }
  return false;
}

template <typename Node>
inline Node FillSpan(Node node, const Parser& start, const Parser& end) {
  if constexpr (kHasSpanField<Node>) {
    if (SpanMissing(node)) {
      node.span = span_between(start, end);
    }
  }
  return node;
}

template <typename Node>
inline Node FillDoc(Node node) {
  if constexpr (kHasDocField<Node>) {
    if (node.doc.empty()) {
      node.doc = doc_default();
    }
  }
  return node;
}

template <typename Node>
inline Node FillDocOpt(Node node) {
  if constexpr (kHasDocOptField<Node>) {
    if (!node.doc_opt.has_value()) {
      node.doc_opt = doc_opt_default();
    }
  }
  return node;
}

template <typename Node>
inline Node ParseCtor(Node node, const Parser& start, const Parser& end) {
  return FillDocOpt(FillDoc(FillSpan(std::move(node), start, end)));
}

}  // namespace cursive::ast
