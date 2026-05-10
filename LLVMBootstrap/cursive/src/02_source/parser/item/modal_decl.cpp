// =============================================================================
// modal_decl.cpp - Modal Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.8 (Modal Declaration Rules)
//
// This file implements modal type declaration parsing:
//   - ParseStateMember: Parse field, method, or transition in state block
//   - ParseStateMemberList: Parse list of state members
//   - ParseStateBlock: Parse @StateName { members }
//   - ParseStateBlockList: Parse multiple state blocks
//   - ParseModalBody: Parse modal body in braces
//   - ParseModalDecl: Parse complete modal declaration
//
// SYNTAX:
//   public modal Connection {
//       @Disconnected { host: string@View, transition connect() -> @Connected { ... } }
//       @Connected { socket: i32, procedure send(~!) -> i32 { ... } }
//   }
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
void SkipNewlines(Parser& parser);

// Forward declarations for type and expression parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// Forward declarations for generic params and where clause parsing
ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
    Parser parser);
ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
    Parser parser);
ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);
ParseElemResult<std::optional<ContractClause>> ParseContractClauseOpt(
    Parser parser);

// Forward declarations from record_decl.cpp
ParseElemResult<std::vector<ClassPath>> ParseImplementsOpt(Parser parser);
ParseElemResult<std::optional<TypeInvariant>> ParseInvariantOpt(Parser parser);

// Forward declaration from signature.cpp
struct SignatureResult {
  Parser parser;
  std::vector<Param> params;
  TypePtr return_type_opt;
};

ParseElemResult<Param> ParseParam(Parser parser);
ParseElemResult<std::vector<Param>> ParseParamTail(Parser parser,
                                                   std::vector<Param> xs);
ParseElemResult<std::vector<Param>> ParseParamList(Parser parser);
SignatureResult ParseSignature(Parser parser);
struct MethodSignatureResult {
  Parser parser;
  Receiver receiver;
  std::vector<Param> params;
  TypePtr return_type_opt;
};
MethodSignatureResult ParseStateMethodSignature(Parser parser);

namespace {

ParseElemResult<std::vector<Param>> ParseTransitionParamList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    return {parser, {}};
  }
  ParseElemResult<Param> param = ParseParam(parser);
  std::vector<Param> params;
  params.push_back(param.elem);
  return ParseParamTail(param.parser, std::move(params));
}

ParseElemResult<StateMethodDecl> ParseStateMethodDef(Parser start,
                                                     Parser parser,
                                                     Visibility vis,
                                                     AttributeList attrs_list) {
  SPEC_RULE("Parse-StateMember-Method");
  Advance(parser);  // consume 'procedure'

  ParseElemResult<Identifier> name = ParseIdent(parser);
  ParseElemResult<std::optional<GenericParams>> gen_params =
      ParseGenericParamsOpt(name.parser);
  MethodSignatureResult sig = ParseStateMethodSignature(gen_params.parser);
  ParseElemResult<std::optional<ContractClause>> contract =
      ParseContractClauseOpt(sig.parser);
  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(contract.parser);

  StateMethodDecl method;
  method.attrs = attrs_list;
  method.vis = vis;
  method.name = name.elem;
  method.generic_params = gen_params.elem;
  method.receiver = sig.receiver;
  method.params = sig.params;
  method.return_type_opt = sig.return_type_opt;
  method.contract = contract.elem;
  method.body = body.elem;
  method.span = SpanBetween(start, body.parser);
  method.doc_opt = std::nullopt;

  return {body.parser, method};
}

ParseElemResult<TransitionDecl> ParseTransitionDecl(Parser start, Parser parser,
                                                    Visibility vis,
                                                    AttributeList attrs_list) {
  SPEC_RULE("Parse-StateMember-Transition");
  Advance(parser);  // consume 'transition'

  ParseElemResult<Identifier> name = ParseIdent(parser);
  Parser cur = name.parser;

  if (!IsPunc(cur, "(")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
  } else {
    Advance(cur);
  }

  ParseElemResult<std::vector<Param>> params = ParseTransitionParamList(cur);
  cur = params.parser;

  if (!IsPunc(cur, ")")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
  } else {
    Advance(cur);
  }

  if (!IsOp(cur, "->")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
  } else {
    Advance(cur);
  }

  if (!IsOp(cur, "@")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
  } else {
    Advance(cur);
  }

  ParseElemResult<Identifier> target = ParseIdent(cur);
  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(target.parser);

  TransitionDecl trans;
  trans.attrs = attrs_list;
  trans.vis = vis;
  trans.name = name.elem;
  trans.params = params.elem;
  trans.target_state = target.elem;
  trans.body = body.elem;
  trans.span = SpanBetween(start, body.parser);
  trans.doc_opt = std::nullopt;

  return {body.parser, trans};
}

ParseElemResult<StateFieldDecl> ParseStateFieldDecl(Parser start, Parser parser,
                                                    Visibility vis,
                                                    AttributeList attrs_list) {
  SPEC_RULE("Parse-StateMember-Field");

  ParseElemResult<bool> boundary = ParseKeyBoundaryOpt(parser);
  ParseElemResult<Identifier> name = ParseIdent(boundary.parser);

  if (!IsPunc(name.parser, ":")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
  } else {
    Advance(name.parser);
  }

  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(name.parser);

  StateFieldDecl field;
  field.attrs = attrs_list;
  field.vis = vis;
  field.key_boundary = boundary.elem;
  field.name = name.elem;
  field.type = ty.elem;
  field.span = SpanBetween(start, ty.parser);
  field.doc_opt = std::nullopt;

  return {ty.parser, field};
}

}  // namespace

// =============================================================================
// ParseStateMember - Parse field, method, or transition in state block
// =============================================================================

ParseElemResult<StateMember> ParseStateMember(Parser parser) {
  ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
  Parser after_attrs = attrs.parser;
  SkipNewlines(after_attrs);
  ParseElemResult<Visibility> vis = ParseVis(after_attrs);
  Parser cur = vis.parser;
  AttributeList attrs_list = attrs.elem.value_or(AttributeList{});

  // Check for method (procedure keyword)
  if (IsKw(cur, "procedure")) {
    auto method = ParseStateMethodDef(parser, cur, vis.elem, attrs_list);
    return {method.parser, StateMember{std::move(method.elem)}};
  }

  // Check for transition
  if (IsKw(cur, "transition")) {
    auto transition = ParseTransitionDecl(parser, cur, vis.elem, attrs_list);
    return {transition.parser, StateMember{std::move(transition.elem)}};
  }

  // Default: parse field
  auto field = ParseStateFieldDecl(parser, cur, vis.elem, attrs_list);
  return {field.parser, StateMember{std::move(field.elem)}};
}

// =============================================================================
// ParseStateMemberList - Parse list of state members
// =============================================================================

ParseElemResult<std::vector<StateMember>> ParseStateMemberList(Parser parser) {
  // Skip leading newlines
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-StateMemberList-End");
    return {parser, {}};
  }

  SPEC_RULE("Parse-StateMemberList-Cons");
  std::vector<StateMember> members;
  Parser cur = parser;

  while (!IsPunc(cur, "}")) {
    // Skip newlines between members
    while (Tok(cur) && Tok(cur)->kind == TokenKind::Newline) {
      Advance(cur);
    }
    if (IsPunc(cur, "}")) break;

    if (IsPunc(cur, ",")) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      Advance(cur);
      continue;
    }

    Parser before = cur;
    ParseElemResult<StateMember> mem = ParseStateMember(cur);
    members.push_back(mem.elem);
    cur = mem.parser;

    // Skip newlines after member
    while (Tok(cur) && Tok(cur)->kind == TokenKind::Newline) {
      Advance(cur);
    }

    // Prevent infinite loop
    if (cur.tokens == before.tokens && cur.index == before.index) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      cur = AdvanceOrEOF(cur);
    }
  }

  return {cur, members};
}

// =============================================================================
// ParseStateBlock - Parse @StateName { members }
// =============================================================================

ParseElemResult<StateBlock> ParseStateBlock(Parser parser) {
  SPEC_RULE("Parse-StateBlock");
  Parser start = parser;

  if (!IsOp(parser, "@")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = AdvanceOrEOF(parser);
    StateBlock blk;
    blk.name = "_";
    blk.span = SpanBetween(start, next);
    blk.doc_opt = std::nullopt;
    return {next, blk};
  }

  Parser next = parser;
  Advance(next);  // consume @

  // Parse state name
  ParseElemResult<Identifier> name = ParseIdent(next);

  // Expect {
  if (!IsPunc(name.parser, "{")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
  } else {
    Advance(name.parser);
  }

  // Parse members
  ParseElemResult<std::vector<StateMember>> members =
      ParseStateMemberList(name.parser);

  // Expect }
  Parser block_end = members.parser;
  if (!IsPunc(members.parser, "}")) {
    EmitParseSyntaxErr(members.parser, TokSpan(members.parser));
  } else {
    Advance(members.parser);
  }

  StateBlock blk;
  blk.name = name.elem;
  blk.members = std::move(members.elem);
  blk.span = SpanBetween(start, block_end);
  blk.doc_opt = std::nullopt;

  return {members.parser, blk};
}

// =============================================================================
// ParseStateBlockList - Parse multiple state blocks
// =============================================================================

ParseElemResult<std::vector<StateBlock>> ParseStateBlockList(Parser parser) {
  // Skip leading newlines
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-StateBlockList-Empty");
    return {parser, {}};
  }

  SPEC_RULE("Parse-StateBlockList-Cons");
  ParseElemResult<StateBlock> block = ParseStateBlock(parser);
  std::vector<StateBlock> blocks;
  blocks.push_back(block.elem);
  Parser cur = block.parser;

  // Skip newlines between state blocks
  while (Tok(cur) && Tok(cur)->kind == TokenKind::Newline) {
    Advance(cur);
  }

  while (!IsPunc(cur, "}")) {
    ParseElemResult<StateBlock> next_blk = ParseStateBlock(cur);
    blocks.push_back(next_blk.elem);
    cur = next_blk.parser;

    // Skip newlines after state block
    while (Tok(cur) && Tok(cur)->kind == TokenKind::Newline) {
      Advance(cur);
    }
  }

  return {cur, blocks};
}

// =============================================================================
// ParseModalBody - Parse modal body in braces
// =============================================================================

ParseElemResult<std::vector<StateBlock>> ParseModalBody(Parser parser) {
  SPEC_RULE("Parse-ModalBody");

  // Skip newlines before opening brace
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  if (!IsPunc(parser, "{")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  Parser next = parser;
  Advance(next);

  ParseElemResult<std::vector<StateBlock>> blocks = ParseStateBlockList(next);
  if (!IsPunc(blocks.parser, "}")) {
    EmitParseSyntaxErr(blocks.parser, TokSpan(blocks.parser));
    return {blocks.parser, blocks.elem};
  }

  Advance(blocks.parser);
  return {blocks.parser, blocks.elem};
}

// =============================================================================
// ParseModalDecl - Parse complete modal declaration
// =============================================================================
//
// SPEC: Parse-Modal
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
//   IsKw(Tok(P_1), `modal`)
//   Γ ⊢ ParseIdent(Advance(P_1)) ⇓ (P_2, name)
//   Γ ⊢ ParseGenericParamsOpt(P_2) ⇓ (P_3, gen_params_opt)
//   Γ ⊢ ParseImplementsOpt(P_3) ⇓ (P_4, impls)
//   Γ ⊢ ParseWhereClauseOpt(P_4) ⇓ (P_5, where_clause_opt)
//   Γ ⊢ ParseModalBody(P_5) ⇓ (P_6, states)
//   Γ ⊢ ParseInvariantOpt(P_6) ⇓ (P_7, invariant_opt)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (P_7, ⟨ModalDecl, ...⟩)

ParseItemResult ParseModalDecl(Parser parser, Visibility vis,
                               AttributeList attrs) {
  SPEC_RULE("Parse-Modal");
  Parser start = parser;

  // Already know we're at "modal" keyword
  Advance(parser);  // consume "modal"

  // Parse modal name
  ParseElemResult<Identifier> name = ParseIdent(parser);
  parser = name.parser;

  // Parse optional generic parameters
  ParseElemResult<std::optional<GenericParams>> gen_params =
      ParseGenericParamsOpt(parser);
  parser = gen_params.parser;

  // Parse optional implements list
  ParseElemResult<std::vector<ClassPath>> impls = ParseImplementsOpt(parser);
  parser = impls.parser;

  // Parse optional predicate clause
  ParseElemResult<std::optional<PredicateClause>> predicate_clause_opt =
      ParsePredicateClauseOpt(parser);
  parser = predicate_clause_opt.parser;

  // Parse modal body
  ParseElemResult<std::vector<StateBlock>> states = ParseModalBody(parser);
  parser = states.parser;

  // Parse optional type invariant
  ParseElemResult<std::optional<TypeInvariant>> invariant =
      ParseInvariantOpt(parser);
  parser = invariant.parser;

  ModalDecl decl;
  decl.attrs = std::move(attrs);
  decl.vis = vis;
  decl.name = name.elem;
  decl.generic_params = gen_params.elem;
  decl.predicate_clause_opt = predicate_clause_opt.elem;
  decl.implements = std::move(impls.elem);
  decl.states = std::move(states.elem);
  decl.invariant_opt = invariant.elem;
  decl.span = SpanBetween(start, parser);
  decl.doc = {};

  RecordGenericPredicateOwnerClause("ModalDecl", decl.name,
                                    decl.generic_params,
                                    decl.predicate_clause_opt, decl.span);
  RecordNominalRelationFormOnOwnerDecl("ModalDecl", decl.name, "implements",
                                       decl.implements, decl.span);

  return {parser, decl};
}

}  // namespace cursive::ast
