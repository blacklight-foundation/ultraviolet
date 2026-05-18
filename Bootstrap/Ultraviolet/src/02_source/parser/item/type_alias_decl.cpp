// =============================================================================
// type_alias_decl.cpp - Type Alias Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.6.11 (Type Alias Rules)
//
// This file implements type alias declaration parsing:
//   - ParseTypeAliasDecl: Parse complete type alias declaration
//
// SYNTAX:
//   public type Result = i32 | AllocationError
//   public type Callback<T> = (T) -> ()
//   public type Option<T> = T | ()
//
// NOTE: Type aliases are distinct from associated types in classes.
//       Type aliases always have = Type; associated types can be abstract.
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Forward declarations for helper functions
bool IsOp(const Parser& parser, std::string_view op);

// Forward declaration for type parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);

// Forward declarations for generic params and where clause parsing
ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
    Parser parser);
ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
    Parser parser);

// =============================================================================
// ParseTypeAliasDecl - Parse complete type alias declaration
// =============================================================================
//
// SPEC: Parse-Type-Alias
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
//   IsKw(Tok(P_1), `type`)
//   Γ ⊢ ParseIdent(Advance(P_1)) ⇓ (P_2, name)
//   Γ ⊢ ParseGenericParamsOpt(P_2) ⇓ (P_3, gen_params_opt)
//   IsOp(Tok(P_3), "=")
//   Γ ⊢ ParseType(Advance(P_3)) ⇓ (P_4, ty)
//   Γ ⊢ ParseWhereClauseOpt(P_4) ⇓ (P_5, where_clause_opt)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (P_5, ⟨TypeAliasDecl, ...⟩)

ParseItemResult ParseTypeAliasDecl(Parser parser, Visibility vis,
                                   AttributeList attrs) {
  SPEC_RULE("Parse-Type-Alias");
  Parser start = parser;

  // Already know we're at "type" keyword
  Advance(parser);  // consume "type"

  // Parse type name
  ParseElemResult<Identifier> name = ParseIdent(parser);
  parser = name.parser;

  // Parse optional generic parameters
  ParseElemResult<std::optional<GenericParams>> gen_params =
      ParseGenericParamsOpt(parser);
  parser = gen_params.parser;

  // Parse optional predicate clause (constraints on generic parameters)
  ParseElemResult<std::optional<PredicateClause>> predicate_clause_opt =
      ParsePredicateClauseOpt(parser);
  parser = predicate_clause_opt.parser;

  // Expect = operator
  if (!IsOp(parser, "=")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
  } else {
    Advance(parser);
  }

  // Parse the aliased type
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(parser);
  parser = ty.parser;

  TypeAliasDecl decl;
  decl.attrs = attrs;
  decl.vis = vis;
  decl.name = name.elem;
  decl.generic_params = gen_params.elem;
  decl.predicate_clause_opt = predicate_clause_opt.elem;
  decl.type = ty.elem;
  decl.span = SpanBetween(start, parser);
  decl.doc = {};

  RecordGenericPredicateOwnerClause("TypeAliasDecl", decl.name,
                                    decl.generic_params,
                                    decl.predicate_clause_opt, decl.span);

  return {parser, decl};
}

}  // namespace ultraviolet::ast
