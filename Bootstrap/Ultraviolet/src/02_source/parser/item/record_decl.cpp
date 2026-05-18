// =============================================================================
// record_decl.cpp - Record Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.6.6 (Record Declaration Rules)
//
// This file implements record declaration parsing:
//   - ParseRecordFieldInitOpt: Parse optional field default value
//   - ParseRecordFieldDeclAfterVis: Parse field after visibility
//   - ParseRecordFieldDecl: Parse field with visibility
//   - ParseMethodDefAfterVis: Parse method after visibility
//   - ParseRecordMember: Parse field or method
//   - ParseRecordMemberList: Parse member list
//   - ParseRecordBody: Parse record body in braces
//   - ParseClassList: Parse class implementation list
//   - ParseImplementsOpt: Parse optional <: Class list
//   - ParseInvariantOpt: Parse optional type invariant
//   - ParseRecordDecl: Parse complete record declaration
//
// SYNTAX:
//   public record Point { x: i32; y: i32 }
//   public record Counter { value: i32; procedure get(~) -> i32 { ... } }
//   public record MyType <: Comparable { ... }
//   public record Positive where { self.value > 0 } { value: i32 }
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast
{

  // Use lexer types
  using ultraviolet::lexer::Token;
  using ultraviolet::lexer::TokenKind;

  // Forward declarations for helper functions
  bool IsKw(const Parser &parser, std::string_view kw);
  bool IsOp(const Parser &parser, std::string_view op);
  bool IsPunc(const Parser &parser, std::string_view p);
  void SkipNewlines(Parser &parser);
  void ConsumeTerminatorReq(Parser &parser);
  ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);

  // Forward declarations for type and expression parsing
  ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
  ParseElemResult<ExprPtr> ParseExpr(Parser parser);
  ParseElemResult<ExprPtr> ParsePredicateExpr(Parser parser);
  ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
  ParseElemResult<ClassPath> ParseClassPath(Parser parser);
  ParseElemResult<std::optional<ContractClause>> ParseContractClauseOpt(
      Parser parser);

  // Forward declarations for generic params and where clause parsing
  ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
      Parser parser);
  ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
      Parser parser);

  // Forward declaration from signature.cpp
  struct MethodSignatureResult
  {
    Parser parser;
    Receiver receiver;
    std::vector<Param> params;
    TypePtr return_type_opt;
  };

  MethodSignatureResult ParseMethodSignature(Parser parser);

  std::shared_ptr<Block> MakeEmptyBlock(const core::Span &span)
  {
    auto block = std::make_shared<Block>();
    block->stmts.clear();
    block->tail_opt = nullptr;
    block->span = span;
    return block;
  }

  // =============================================================================
  // ParseRecordFieldInitOpt - Parse optional field default value
  // =============================================================================

  struct RecordFieldInitOptResult
  {
    Parser parser;
    std::shared_ptr<Expr> init_opt;
  };

  RecordFieldInitOptResult ParseRecordFieldInitOpt(Parser parser)
  {
    if (!IsOp(parser, "="))
    {
      SPEC_RULE("Parse-RecordFieldInitOpt-None");
      return {parser, nullptr};
    }
    SPEC_RULE("Parse-RecordFieldInitOpt-Yes");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> init = ParseExpr(next);
    return {init.parser, init.elem};
  }

  // =============================================================================
  // ParseRecordFieldDeclAfterVis - Parse field after visibility
  // =============================================================================
  //
  // Field format: [#] name: Type [= default_expr]
  // # prefix marks key boundary for synchronized access

  ParseElemResult<FieldDecl> ParseRecordFieldDeclAfterVis(Parser parser,
                                                          Visibility vis,
                                                          AttributeList attrs)
  {
    SPEC_RULE("Parse-RecordFieldDeclAfterVis");
    Parser start = parser;

    ParseElemResult<bool> boundary = ParseKeyBoundaryOpt(parser);

    // Parse field name
    ParseElemResult<Identifier> name = ParseIdent(boundary.parser);
    if (!IsPunc(name.parser, ":"))
    {
      EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
    }
    else
    {
      Advance(name.parser);
    }

    // Parse field type
    ParseElemResult<std::shared_ptr<Type>> ty = ParseType(name.parser);

    // Parse optional default value
    RecordFieldInitOptResult init = ParseRecordFieldInitOpt(ty.parser);

    FieldDecl field;
    field.attrs = attrs;
    field.vis = vis;
    field.key_boundary = boundary.elem;
    field.name = name.elem;
    field.type = ty.elem;
    field.init_opt = init.init_opt;
    field.span = SpanBetween(start, init.parser);
    field.doc_opt = std::nullopt;

    return {init.parser, field};
  }

  // =============================================================================
  // ParseRecordFieldDecl - Parse field with visibility
  // =============================================================================

  ParseElemResult<FieldDecl> ParseRecordFieldDecl(Parser parser)
  {
    SPEC_RULE("Parse-RecordFieldDecl");
    ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
    Parser after_attrs = attrs.parser;
    SkipNewlines(after_attrs);
    ParseElemResult<Visibility> vis = ParseVis(after_attrs);
    return ParseRecordFieldDeclAfterVis(vis.parser, vis.elem,
                                        attrs.elem.value_or(AttributeList{}));
  }

  // =============================================================================
  // ParseOverrideOpt - Parse optional override keyword
  // =============================================================================

  struct OverrideResult
  {
    Parser parser;
    bool override_flag;
  };

  OverrideResult ParseOverrideOpt(Parser parser)
  {
    if (IsKw(parser, "override"))
    {
      SPEC_RULE("Parse-Override-Yes");
      Parser next = parser;
      Advance(next);
      return {next, true};
    }
    SPEC_RULE("Parse-Override-No");
    return {parser, false};
  }

  // =============================================================================
  // ParseMethodDefAfterVis - Parse method after visibility
  // =============================================================================
  //
  // Format: [override] procedure name(receiver, params) -> ReturnType
  //         [|: contract] { body }

  ParseElemResult<MethodDecl> ParseMethodDefAfterVis(Parser parser,
                                                     Visibility vis,
                                                     AttributeList attrs)
  {
    SPEC_RULE("Parse-MethodDefAfterVis");
    Parser start = parser;

    // Parse optional override keyword
    OverrideResult ov = ParseOverrideOpt(parser);

    // Expect 'procedure' keyword
    if (!IsKw(ov.parser, "procedure"))
    {
      EmitParseSyntaxErr(ov.parser, TokSpan(ov.parser));
    }
    else
    {
      Advance(ov.parser);
    }

    // Parse method name
    ParseElemResult<Identifier> name = ParseIdent(ov.parser);
    ParseElemResult<std::optional<GenericParams>> method_generics =
        ParseGenericParamsOpt(name.parser);

    // Parse method signature with receiver
    MethodSignatureResult sig = ParseMethodSignature(method_generics.parser);

    // Parse optional contract clause
    ParseElemResult<std::optional<ContractClause>> contract =
        ParseContractClauseOpt(sig.parser);

    std::shared_ptr<Block> body_block;
    Parser body_parser = contract.parser;
    Parser body_probe = body_parser;
    SkipNewlines(body_probe);
    if (IsPunc(body_probe, "{"))
    {
      ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(body_parser);
      body_parser = body.parser;
      body_block = body.elem;
    }
    else
    {
      body_block = MakeEmptyBlock(TokSpan(body_parser));
    }

    MethodDecl method;
    method.attrs = attrs;
    method.vis = vis;
    method.override_flag = ov.override_flag;
    method.name = name.elem;
    method.generic_params = method_generics.elem;
    method.receiver = sig.receiver;
    method.params = sig.params;
    method.return_type_opt = sig.return_type_opt;
    method.contract = contract.elem;
    method.body = body_block;
    method.span = SpanBetween(start, body_parser);
    method.doc_opt = std::nullopt;

    return {body_parser, method};
  }

  // =============================================================================
  // ParseAssociatedTypeDeclAfterVis - Parse associated type after visibility
  // =============================================================================

  ParseElemResult<AssociatedTypeDecl> ParseAssociatedTypeDeclAfterVis(
      Parser start, Parser parser, Visibility vis, AttributeList attrs)
  {
    SPEC_RULE("Parse-RecordMember-AssociatedType");

    if (!IsKw(parser, "type"))
    {
      EmitParseSyntaxErr(parser, TokSpan(parser));
    }
    else
    {
      Advance(parser);
    }

    ParseElemResult<Identifier> name = ParseIdent(parser);
    Parser after_name = name.parser;

    std::shared_ptr<Type> default_type = nullptr;
    if (IsOp(after_name, "="))
    {
      SPEC_RULE("Parse-AssocTypeOpt-Yes");
      SPEC_RULE("Parse-AssocTypeDefaultOpt");
      Advance(after_name);
      ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after_name);
      default_type = ty.elem;
      after_name = ty.parser;
    }
    else
    {
      SPEC_RULE("Parse-AssocTypeOpt-None");
      SPEC_RULE("Parse-AssocTypeDefaultOpt");
    }

    AssociatedTypeDecl assoc;
    assoc.attrs = attrs;
    assoc.vis = vis;
    assoc.name = name.elem;
    assoc.default_type = default_type;
    assoc.span = SpanBetween(start, after_name);
    assoc.doc_opt = std::nullopt;

    return {after_name, assoc};
  }

  // =============================================================================
  // ParseRecordMember - Parse field or method
  // =============================================================================

  ParseElemResult<RecordMember> ParseRecordMember(Parser parser)
  {
    ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
    Parser after_attrs = attrs.parser;
    SkipNewlines(after_attrs);
    ParseElemResult<Visibility> vis = ParseVis(after_attrs);
    AttributeList attrs_list = attrs.elem.value_or(AttributeList{});

    // Methods identified by 'procedure' or 'override' keyword
    if (IsKw(vis.parser, "procedure") || IsKw(vis.parser, "override"))
    {
      SPEC_RULE("Parse-RecordMember-Method");
      ParseElemResult<MethodDecl> method =
          ParseMethodDefAfterVis(vis.parser, vis.elem, attrs_list);
      return {method.parser, method.elem};
    }

    if (IsKw(vis.parser, "type"))
    {
      ParseElemResult<AssociatedTypeDecl> assoc =
          ParseAssociatedTypeDeclAfterVis(parser, vis.parser, vis.elem,
                                          attrs_list);
      return {assoc.parser, assoc.elem};
    }

    SPEC_RULE("Parse-RecordMember-Field");
    ParseElemResult<FieldDecl> field =
        ParseRecordFieldDeclAfterVis(vis.parser, vis.elem, attrs_list);
    return {field.parser, field.elem};
  }

  // =============================================================================
  // ParseRecordMemberSep - Parse separator after a record member
  // =============================================================================

  void ParseRecordMemberSep(Parser &parser)
  {
    if (IsPunc(parser, "}"))
    {
      SPEC_RULE("Parse-RecordMemberSep-End");
      return;
    }
    SPEC_RULE("Parse-RecordMemberSep-Terminator");
    ConsumeTerminatorReq(parser);
  }

  // =============================================================================
  // ParseRecordMemberList - Parse member list
  // =============================================================================

ParseElemResult<std::vector<RecordMember>> ParseRecordMemberList(
      Parser parser)
  {
    // Skip leading newlines
    SkipNewlines(parser);

    if (IsPunc(parser, "}"))
    {
      SPEC_RULE("Parse-RecordMemberList-End");
      return {parser, {}};
    }

    SPEC_RULE("Parse-RecordMemberList-Cons");
    std::vector<RecordMember> members;
    Parser cur = parser;

    while (!IsPunc(cur, "}"))
    {
      // Skip newlines between members.
      SkipNewlines(cur);
      if (IsPunc(cur, "}"))
        break;

      Parser before = cur;
      ParseElemResult<RecordMember> mem = ParseRecordMember(cur);
      cur = mem.parser;
      ParseRecordMemberSep(cur);
      members.push_back(std::move(mem.elem));

      // Prevent infinite loops during error recovery.
      if (cur.tokens == before.tokens && cur.index == before.index)
      {
        EmitParseSyntaxErr(cur, TokSpan(cur));
        cur = AdvanceOrEOF(cur);
      }
    }

    return {cur, members};
  }

  // =============================================================================
  // ParseRecordBody - Parse record body in braces
  // =============================================================================

  ParseElemResult<std::vector<RecordMember>> ParseRecordBody(Parser parser)
  {
    SPEC_RULE("Parse-RecordBody");

    // Skip newlines before opening brace
    while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline)
    {
      Advance(parser);
    }

    if (!IsPunc(parser, "{"))
    {
      EmitParseSyntaxErr(parser, TokSpan(parser));
      return {parser, {}};
    }

    Parser next = parser;
    Advance(next);

    ParseElemResult<std::vector<RecordMember>> members =
        ParseRecordMemberList(next);

    if (!IsPunc(members.parser, "}"))
    {
      EmitParseSyntaxErr(members.parser, TokSpan(members.parser));
      return {members.parser, members.elem};
    }

    Advance(members.parser);
    return {members.parser, members.elem};
  }

  // =============================================================================
  // ParseClassListTail - Parse remaining classes after first
  // =============================================================================

  ParseElemResult<std::vector<ClassPath>> ParseClassListTail(
      Parser parser, std::vector<ClassPath> xs)
  {
    SkipNewlines(parser);

    if (!IsPunc(parser, ","))
    {
      SPEC_RULE("Parse-ClassListTail-End");
      return {parser, xs};
    }

    const EndSetToken end_set[] = {EndPunct("{")};
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);

    if (IsPunc(after, "{"))
    {
      EmitTrailingCommaErr(parser, end_set);
      after.diags = parser.diags;
      return {after, xs};
    }

    SPEC_RULE("Parse-ClassListTail-Comma");
    ParseElemResult<ClassPath> cls = ParseClassPath(after);
    xs.push_back(cls.elem);
    return ParseClassListTail(cls.parser, std::move(xs));
  }

  // =============================================================================
  // ParseClassList - Parse comma-separated class list
  // =============================================================================

  ParseElemResult<std::vector<ClassPath>> ParseClassList(Parser parser)
  {
    SPEC_RULE("Parse-ClassList-Cons");
    ParseElemResult<ClassPath> first = ParseClassPath(parser);
    std::vector<ClassPath> xs;
    xs.push_back(first.elem);
    return ParseClassListTail(first.parser, std::move(xs));
  }

  // =============================================================================
  // ParseImplementsOpt - Parse optional <: Class list
  // =============================================================================

  ParseElemResult<std::vector<ClassPath>> ParseImplementsOpt(Parser parser)
  {
    if (!IsOp(parser, "<:"))
    {
      SPEC_RULE("Parse-Implements-None");
      return {parser, {}};
    }
    SPEC_RULE("Parse-Implements-Yes");
    Parser next = parser;
    Advance(next);
    return ParseClassList(next);
  }

  // =============================================================================
  // ParseInvariantOpt - Parse optional type invariant
  // =============================================================================
  //
  // Format: |: { predicate_expr }
  // This is the TYPE invariant, distinct from generic constraint clauses.

  ParseElemResult<std::optional<TypeInvariant>> ParseInvariantOpt(
      Parser parser)
  {
    if (!IsOp(parser, "|:"))
    {
      SPEC_RULE("Parse-InvariantOpt-None");
      return {parser, std::nullopt};
    }

    SPEC_RULE("Parse-InvariantOpt-Yes");
    Parser start = parser;
    Parser next = parser;
    Advance(next); // consume |:

    if (!IsPunc(next, "{"))
    {
      EmitParseSyntaxErr(next, TokSpan(next));
      SyncItem(next);
      return {next, std::nullopt};
    }

    Parser after_l = next;
    Advance(after_l);

    ParseElemResult<ExprPtr> pred = ParsePredicateExpr(after_l);
    Parser after_pred = pred.parser;

    if (!IsPunc(after_pred, "}"))
    {
      EmitParseSyntaxErr(after_pred, TokSpan(after_pred));
      SyncItem(after_pred);
      return {after_pred, std::nullopt};
    }

    Parser after = after_pred;
    Advance(after);

    TypeInvariant inv;
    inv.predicate = pred.elem;
    inv.span = SpanBetween(start, after);

    return {after, inv};
  }

  // =============================================================================
  // ParseRecordDecl - Parse complete record declaration
  // =============================================================================
  //
  // SPEC: Parse-Record
  //   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
  //   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
  //   IsKw(Tok(P_1), `record`)
  //   Γ ⊢ ParseIdent(Advance(P_1)) ⇓ (P_2, name)
  //   Γ ⊢ ParseGenericParamsOpt(P_2) ⇓ (P_3, gen_params_opt)
  //   Γ ⊢ ParseImplementsOpt(P_3) ⇓ (P_4, impls)
  //   Γ ⊢ ParseWhereClauseOpt(P_4) ⇓ (P_5, where_clause_opt)
  //   Γ ⊢ ParseRecordBody(P_5) ⇓ (P_6, members)
  //   Γ ⊢ ParseInvariantOpt(P_6) ⇓ (P_7, invariant_opt)
  //   ────────────────────────────────────────────────────────────────────
  //   Γ ⊢ ParseItem(P) ⇓ (P_7, ⟨RecordDecl, ...⟩)

  ParseItemResult ParseRecordDecl(Parser parser, Visibility vis,
                                  AttributeList attrs)
  {
    SPEC_RULE("Parse-Record");
    Parser start = parser;

    // Already know we're at "record" keyword
    Advance(parser); // consume "record"

    // Parse record name
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

    // Parse record body
    ParseElemResult<std::vector<RecordMember>> members = ParseRecordBody(parser);
    parser = members.parser;

    // Parse optional type invariant
    ParseElemResult<std::optional<TypeInvariant>> invariant =
        ParseInvariantOpt(parser);
    parser = invariant.parser;

    RecordDecl decl;
    decl.attrs = attrs;
    decl.vis = vis;
    decl.name = name.elem;
    decl.generic_params = gen_params.elem;
    decl.predicate_clause_opt = predicate_clause_opt.elem;
    decl.implements = std::move(impls.elem);
    decl.members = std::move(members.elem);
    decl.invariant_opt = invariant.elem;
    decl.span = SpanBetween(start, parser);
    decl.doc = {};

    RecordGenericPredicateOwnerClause("RecordDecl", decl.name,
                                      decl.generic_params,
                                      decl.predicate_clause_opt, decl.span);
    RecordNominalRelationFormOnOwnerDecl("RecordDecl", decl.name,
                                         "implements", decl.implements,
                                         decl.span);

    return {parser, decl};
  }

} // namespace ultraviolet::ast
