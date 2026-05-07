// =============================================================================
// static_decl.cpp - Static Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.4 (Static Declaration Rules)
//
// This file implements static declaration parsing:
//   - ParseBindingAfterLetVar: Parse binding after let/var keyword
//   - ParseStaticDecl: Parse complete module-level static declaration
//
// SYNTAX:
//   public let MAX_SIZE: usize = 1024      -- immutable static
//   public var counter: i32 = 0            -- mutable static
//   let (x, y): (i32, i32) = (10, 20)      -- with destructuring
//   let config := load_config()            -- immovable binding
//
// BINDING OPERATORS:
//   =   movable binding (value can be moved)
//   :=  immovable binding (value cannot be moved)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <utility>

#include "00_core/assert_spec.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);

// Forward declaration for pattern and type parsing
ParseElemResult<std::shared_ptr<Pattern>> ParsePattern(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseTypeAnnotOpt(Parser parser);

// Forward declaration for expression parsing
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// ParseBindingAfterLetVar - Parse binding after let/var keyword
// =============================================================================
//
// SPEC: Parse-BindingAfterLetVar
//   Tok(P) ∈ {`let`, `var`}
//   Γ ⊢ ParsePattern(Advance(P)) ⇓ (P_1, pat)
//   Γ ⊢ ParseTypeAnnotOpt(P_1) ⇓ (P_2, ty_opt)
//   NormalizeBindingPattern(pat, ty_opt)
//   Tok(P_2) ∈ {`=`, `:=`}    op = Tok(P_2)
//   Γ ⊢ ParseExpr(Advance(P_2)) ⇓ (P_3, init)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseBindingAfterLetVar(P) ⇓ (P_3, ⟨pat, ty_opt, op, init,
//                                       SpanBetween(P, P_3)⟩)
//
// Parses: let/var pattern [: Type] (= | :=) expr

ParseElemResult<Binding> ParseBindingAfterLetVar(Parser parser) {
  SPEC_RULE("Parse-BindingAfterLetVar");
  Parser start = parser;
  Parser after_kw = parser;
  Advance(after_kw);  // consume let/var

  // Parse pattern
  ParseElemResult<std::shared_ptr<Pattern>> pat = ParsePattern(after_kw);

  // Parse optional type annotation
  ParseElemResult<std::shared_ptr<Type>> ty = ParseTypeAnnotOpt(pat.parser);

  // Expect = or :=
  const Token* tok = Tok(ty.parser);
  Token op;
  if (tok && tok->kind == TokenKind::Operator &&
      (tok->lexeme == "=" || tok->lexeme == ":=")) {
    op = *tok;
    Advance(ty.parser);
  } else {
    EmitParseSyntaxErr(ty.parser, TokSpan(ty.parser));
  }

  // Parse initializer expression
  ParseElemResult<ExprPtr> init = ParseExpr(ty.parser);

  Binding binding;
  binding.pat = pat.elem;
  binding.type_opt = ty.elem;
  binding.op = op;
  binding.init = init.elem;
  binding.span = SpanBetween(start, init.parser);

  return {init.parser, binding};
}

// =============================================================================
// ParseStaticDecl - Parse complete static declaration
// =============================================================================
//
// SPEC: Parse-Static-Decl
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
//   Tok(P_1) = Keyword(kw)    kw ∈ {`let`, `var`}    mut = kw
//   Γ ⊢ ParseBindingAfterLetVar(P_1) ⇓ (P_2, bind)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (P_2, ⟨StaticDecl, attrs_opt, vis, mut, bind,
//                           SpanBetween(P, P_2), []⟩)

ParseItemResult ParseStaticDecl(Parser parser, Visibility vis,
                                AttrOpt attrs_opt) {
  SPEC_RULE("Parse-Static-Decl");
  Parser start = parser;

  // Determine mutability from keyword
  Mutability mut;
  if (IsKw(parser, "let")) {
    mut = Mutability::Let;
  } else {
    mut = Mutability::Var;
  }

  // Parse binding (consumes let/var and parses pattern = expr)
  ParseElemResult<Binding> binding = ParseBindingAfterLetVar(parser);
  parser = binding.parser;

  StaticDecl decl;
  decl.attrs_opt = std::move(attrs_opt);
  decl.vis = vis;
  decl.mut = mut;
  decl.binding = binding.elem;
  decl.span = SpanBetween(start, parser);
  decl.doc = {};

  return {parser, decl};
}

}  // namespace cursive::ast
