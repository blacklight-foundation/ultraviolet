// =============================================================================
// import_decl.cpp - Import Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.3 (Import Declaration Rules)
//
// This file implements import declaration parsing:
//   - ParseImportDecl: Parse complete import declaration
//
// SYNTAX:
//   import mylib::networking            -- basic import
//   import mylib::networking as net     -- import with alias
//
// NOTE: Import brings a module into scope as a namespace.
//       This is different from "using" which brings items directly into scope.
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <utility>

#include "00_core/assert_spec.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);

// =============================================================================
// ParseImportDecl - Parse complete import declaration
// =============================================================================
//
// SPEC: Parse-Import
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
//   IsKw(Tok(P_1), `import`)
//   Γ ⊢ ParseModulePath(Advance(P_1)) ⇓ (P_2, path)
//   Γ ⊢ ParseAliasOpt(P_2) ⇓ (P_3, alias_opt)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (P_3, ⟨ImportDecl, attrs_opt, vis, path, alias_opt,
//                           SpanBetween(P, P_3), []⟩)

ParseItemResult ParseImportDecl(Parser parser, Visibility vis,
                                AttrOpt attrs_opt) {
  SPEC_RULE("Parse-Import");
  Parser start = parser;

  // Already know we're at "import" keyword
  Advance(parser);  // consume "import"

  // Parse module path
  ParseElemResult<ModulePath> path = ParseModulePath(parser);
  parser = path.parser;

  // Parse optional alias
  ParseElemResult<std::optional<Identifier>> alias = ParseAliasOpt(parser);
  parser = alias.parser;

  ImportDecl decl;
  decl.attrs_opt = std::move(attrs_opt);
  decl.vis = vis;
  decl.path = path.elem;  // ModulePath is Path (same type)
  decl.alias_opt = alias.elem;
  decl.span = SpanBetween(start, parser);
  decl.doc = {};

  return {parser, decl};
}

}  // namespace cursive::ast
