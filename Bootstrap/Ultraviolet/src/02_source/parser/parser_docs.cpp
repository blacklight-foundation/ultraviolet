// =============================================================================
// parser_docs.cpp - Documentation Comment Association
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.11 (Lines 6433-6450)
//
// This file implements documentation comment handling:
//   - ModuleDocs: Extract module documentation (//!) comments
//   - AttachLineDocs: Associate line documentation (///) with items
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <utility>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::DocComment;
using ultraviolet::lexer::DocKind;

namespace {

// =============================================================================
// ItemStartOffset - Get the start offset of an ASTItem
// =============================================================================

std::size_t ItemStartOffset(const ASTItem& item) {
  return std::visit([](const auto& node) { return node.span.start_offset; },
                    item);
}

// =============================================================================
// SetItemDoc - Attach doc list to an item
// =============================================================================
//
// Type-specific visitor to set the doc field on each item type.
// ErrorItem and ImportDecl do not receive documentation.

void SetItemDoc(ASTItem& item, DocList docs) {
  if (auto* decl = std::get_if<UsingDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<ImportDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<ExternBlock>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<StaticDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<ProcedureDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<RecordDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<EnumDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<ModalDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<ClassDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<TypeAliasDecl>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
  if (auto* decl = std::get_if<ErrorItem>(&item)) {
    decl->doc = std::move(docs);
    return;
  }
}

}  // namespace

// =============================================================================
// DocSeq - Identity projection for the parser doc stream
// =============================================================================
//
// SPEC: Section 5.7 line 2950
//   DocSeq(D) = D

const std::vector<DocComment>& DocSeq(const std::vector<DocComment>& docs) {
  SPEC_RULE("DocSeq");
  return docs;
}

// =============================================================================
// ItemSeq - Identity projection for the parser item stream
// =============================================================================
//
// SPEC: Section 5.7 line 2989
//   ItemSeq(Items) = Items

std::vector<ASTItem> ItemSeq(std::vector<ASTItem> items) {
  SPEC_RULE("ItemSeq(Items)");
  return items;
}

// =============================================================================
// ModuleDocs - Extract module documentation comments
// =============================================================================
//
// SPEC: Attach-Doc-Module (Section 3.3.11 lines 6445-6448)
//   d.kind = ModuleDoc
//   ----------------------------------------
//   AttachModuleDoc(d)
//
//   ModuleDocs(D) = [d in D | d.kind = ModuleDoc]

std::vector<DocComment> ModuleDocs(const std::vector<DocComment>& docs) {
  std::vector<DocComment> out;
  for (const auto& doc : docs) {
    if (doc.kind == DocKind::ModuleDoc) {
      SPEC_RULE("Attach-Doc-Module");
      out.push_back(doc);
    }
  }
  return out;
}

// =============================================================================
// AttachLineDocs - Associate line docs with items
// =============================================================================
//
// SPEC: Attach-Doc-Line (Section 3.3.11 lines 6438-6443)
//   d.kind = LineDoc    Items = [i_1, ..., i_k]
//   j = min{ t | d.span.end_offset <= i_t.span.start_offset }
//   ----------------------------------------
//   AttachDoc(d, i_j)
//
// Algorithm is O(n + m) where n = items, m = docs:
// - Scans docs in order
// - For each LineDoc, finds first item starting after doc ends
// - Associates each line doc to the first item that begins at or after the doc

void AttachLineDocs(std::vector<ASTItem>& items,
                    const std::vector<DocComment>& docs) {
  if (items.empty()) {
    return;
  }

  std::vector<DocList> item_docs(items.size());
  std::size_t item_index = 0;

  for (const auto& doc : docs) {
    if (doc.kind != DocKind::LineDoc) {
      continue;
    }
    while (item_index < items.size() &&
           ItemStartOffset(items[item_index]) < doc.span.end_offset) {
      item_index++;
    }
    if (item_index >= items.size()) {
      continue;
    }
    SPEC_RULE("Attach-Doc-Line");
    item_docs[item_index].push_back(doc);
  }

  for (std::size_t i = 0; i < items.size(); ++i) {
    if (item_docs[i].empty()) {
      continue;
    }
    SetItemDoc(items[i], std::move(item_docs[i]));
  }
}

}  // namespace ultraviolet::ast
