// =============================================================================
// class_decl.cpp - Class Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.6.8 (Class Declaration Rules)
//
// This file implements class (interface/trait) declaration parsing:
//   - ParseClassItem: Parse method, associated type, or field in class body
//   - ParseClassItemList: Parse list of class items
//   - ParseClassBody: Parse class body in braces
//   - ParseSuperclassBounds: Parse superclass bounds (Class1 + Class2)
//   - ParseSuperclassOpt: Parse optional <: superclasses
//   - ParseClassDecl: Parse complete class declaration
//
// SYNTAX:
//   public class Comparable { procedure compare(~, other: const Self) -> i32 }
//   public class Container { type Item; procedure get(~) -> Self::Item }
//   public modal class Lifecycle { ... }
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
void SkipNewlines(Parser& parser);
void ConsumeTerminatorReq(Parser& parser);
void ConsumeTerminatorOpt(Parser& parser, TerminatorPolicy policy);

// Forward declarations for type and expression parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
ParseElemResult<ClassPath> ParseClassPath(Parser parser);
ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);

// Forward declarations for generic params and where clause parsing
ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
    Parser parser);
ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
    Parser parser);
ParseElemResult<std::optional<ContractClause>> ParseContractClauseOpt(
    Parser parser);

// Forward declaration from signature.cpp
struct MethodSignatureResult {
  Parser parser;
  Receiver receiver;
  std::vector<Param> params;
  TypePtr return_type_opt;
};

MethodSignatureResult ParseMethodSignature(Parser parser);

ParseElemResult<TypeParam> ParseAsyncTypeParam(Parser parser) {
  Parser start = parser;
  ParseElemResult<Identifier> name = ParseIdent(parser);
  Parser next = name.parser;

  std::shared_ptr<Type> default_type = nullptr;
  if (IsOp(next, "=")) {
    Advance(next);
    ParseElemResult<std::shared_ptr<Type>> ty = ParseType(next);
    default_type = ty.elem;
    next = ty.parser;
  }

  TypeParam param;
  param.name = name.elem;
  param.bounds = {};
  param.default_type = default_type;
  param.variance = std::nullopt;
  param.span = SpanBetween(start, next);
  return {next, param};
}

ParseElemResult<std::optional<GenericParams>> ParseAsyncTypeParamsOpt(
    Parser parser) {
  if (!IsOp(parser, "<")) {
    return {parser, std::nullopt};
  }

  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume <

  GenericParams params;
  ParseElemResult<TypeParam> first = ParseAsyncTypeParam(next);
  params.params.push_back(first.elem);
  next = first.parser;

  while (IsPunc(next, ";")) {
    Advance(next);
    ParseElemResult<TypeParam> param = ParseAsyncTypeParam(next);
    params.params.push_back(param.elem);
    next = param.parser;
  }

  if (!IsOp(next, ">")) {
    EmitParseSyntaxErr(next, TokSpan(next));
  } else {
    Advance(next);
  }

  params.span = SpanBetween(start, next);
  return {next, params};
}

// =============================================================================
// ParseAbstractFieldDecl / ParseAbstractFieldList
// =============================================================================

static ParseElemResult<AbstractFieldDecl> ParseAbstractFieldDecl(Parser parser) {
  Parser start = parser;

  ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
  Parser after_attrs = attrs.parser;
  SkipNewlines(after_attrs);
  ParseElemResult<Visibility> vis = ParseVis(after_attrs);
  Parser cur = vis.parser;
  AttributeList attrs_list = attrs.elem.value_or(AttributeList{});

  ParseElemResult<bool> boundary = ParseKeyBoundaryOpt(cur);
  ParseElemResult<Identifier> name = ParseIdent(boundary.parser);

  if (!IsPunc(name.parser, ":")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
  } else {
    Advance(name.parser);
  }

  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(name.parser);
  Parser after_type = ty.parser;

  AbstractFieldDecl field;
  field.attrs = attrs_list;
  field.vis = vis.elem;
  field.key_boundary = boundary.elem;
  field.name = name.elem;
  field.type = ty.elem;
  field.span = SpanBetween(start, after_type);
  field.doc_opt = std::nullopt;

  return {after_type, field};
}

static ParseElemResult<std::vector<AbstractFieldDecl>> ParseAbstractFieldList(
    Parser parser) {
  std::vector<AbstractFieldDecl> fields;
  Parser cur = parser;
  SkipNewlines(cur);

  while (!IsPunc(cur, "}")) {
    Parser before = cur;
    ParseElemResult<AbstractFieldDecl> field = ParseAbstractFieldDecl(cur);
    Parser after_field = field.parser;
    if (!IsPunc(after_field, "}")) {
      ConsumeTerminatorReq(after_field);
    }

    field.elem.span = SpanBetween(before, after_field);
    fields.push_back(std::move(field.elem));
    cur = after_field;
    SkipNewlines(cur);

    if (cur.tokens == before.tokens && cur.index == before.index) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      cur = AdvanceOrEOF(cur);
    }
  }

  return {cur, fields};
}

// =============================================================================
// ParseClassItem - Parse method, associated type, or field in class body
// =============================================================================

ParseElemResult<ClassItem> ParseClassItem(Parser parser) {
  Parser start = parser;
  ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
  Parser after_attrs = attrs.parser;
  SkipNewlines(after_attrs);
  ParseElemResult<Visibility> vis = ParseVis(after_attrs);
  Parser cur = vis.parser;
  AttributeList attrs_list = attrs.elem.value_or(AttributeList{});

  // Abstract state declaration: @ State { ... }
  if (IsOp(cur, "@")) {
    SPEC_RULE("Parse-ClassItem-AbstractState");
    Parser after_at = cur;
    Advance(after_at);
    ParseElemResult<Identifier> name = ParseIdent(after_at);
    Parser after_name = name.parser;
    if (!IsPunc(after_name, "{")) {
      EmitParseSyntaxErr(after_name, TokSpan(after_name));
      Parser sync = after_name;
      SyncStmt(sync);
      AbstractStateDecl state;
      state.attrs = attrs_list;
      state.vis = vis.elem;
      state.name = "";
      state.fields = {};
      state.span = SpanBetween(start, sync);
      state.doc_opt = std::nullopt;
      return {sync, state};
    }
    Parser after_l = after_name;
    Advance(after_l);

    ParseElemResult<std::vector<AbstractFieldDecl>> fields =
        ParseAbstractFieldList(after_l);
    Parser field_cur = fields.parser;

    Parser after_r = field_cur;
    if (!IsPunc(after_r, "}")) {
      EmitParseSyntaxErr(after_r, TokSpan(after_r));
      Parser sync = after_r;
      SyncStmt(sync);
      AbstractStateDecl state;
      state.attrs = attrs_list;
      state.vis = vis.elem;
      state.name = name.elem;
      state.fields = std::move(fields.elem);
      state.span = SpanBetween(start, sync);
      state.doc_opt = std::nullopt;
      return {sync, state};
    }
    Advance(after_r);
    ConsumeTerminatorOpt(after_r, TerminatorPolicy::Optional);

    AbstractStateDecl state;
    state.attrs = attrs_list;
    state.vis = vis.elem;
    state.name = name.elem;
    state.fields = std::move(fields.elem);
    state.span = SpanBetween(start, after_r);
    state.doc_opt = std::nullopt;
    return {after_r, state};
  }


  // Check for method (procedure keyword)
  if (IsKw(cur, "procedure")) {
    SPEC_RULE("Parse-ClassItem-Method");
    Parser start_proc = cur;
    Advance(start_proc);  // consume 'procedure'

    ParseElemResult<Identifier> name = ParseIdent(start_proc);
    ParseElemResult<std::optional<GenericParams>> gen_params =
        ParseGenericParamsOpt(name.parser);
    MethodSignatureResult sig = ParseMethodSignature(gen_params.parser);

    // Optional contract clause
    ParseElemResult<std::optional<ContractClause>> contract =
        ParseContractClauseOpt(sig.parser);
    Parser after_contract = contract.parser;

    // Check for body or abstract (no body)
    std::shared_ptr<Block> body = nullptr;
    Parser after_sig = after_contract;

    if (IsPunc(after_sig, "{")) {
      SPEC_RULE("Parse-ClassMethodBody-Concrete");
      ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(after_sig);
      body = block.elem;
      after_sig = block.parser;
    } else {
      SPEC_RULE("Parse-ClassMethodBody-Abstract");
      ConsumeTerminatorReq(after_sig);
    }

    ClassMethodDecl method;
    method.attrs = attrs_list;
    method.vis = vis.elem;
    method.name = name.elem;
    method.generic_params = gen_params.elem;
    method.receiver = sig.receiver;
    method.params = sig.params;
    method.return_type_opt = sig.return_type_opt;
    method.contract = contract.elem;
    method.body_opt = body;
    method.span = SpanBetween(start, after_sig);
    method.doc_opt = std::nullopt;

    return {after_sig, method};
  }

  // Check for associated type
  if (IsKw(cur, "type")) {
    SPEC_RULE("Parse-ClassItem-AssociatedType");
    Parser start_type = cur;
    Advance(start_type);  // consume 'type'

    ParseElemResult<Identifier> name = ParseIdent(start_type);
    Parser after_name = name.parser;

    // Check for optional = Type
    std::shared_ptr<Type> type_opt = nullptr;
    if (IsOp(after_name, "=")) {
      SPEC_RULE("Parse-AssocTypeOpt-Yes");
      Advance(after_name);
      ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after_name);
      type_opt = ty.elem;
      after_name = ty.parser;
    } else {
      SPEC_RULE("Parse-AssocTypeOpt-None");
    }

    ConsumeTerminatorReq(after_name);

    AssociatedTypeDecl assoc;
    assoc.attrs = attrs_list;
    assoc.vis = vis.elem;
    assoc.name = name.elem;
    assoc.default_type = type_opt;
    assoc.span = SpanBetween(start, after_name);
    assoc.doc_opt = std::nullopt;

    return {after_name, assoc};
  }

  // Default: parse field (abstract field decl)
  SPEC_RULE("Parse-ClassItem-Field");
  Parser start_field = cur;

  ParseElemResult<bool> boundary = ParseKeyBoundaryOpt(cur);
  ParseElemResult<Identifier> name = ParseIdent(boundary.parser);

  if (!IsPunc(name.parser, ":")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
  } else {
    Advance(name.parser);
  }

  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(name.parser);
  Parser after_type = ty.parser;

  ConsumeTerminatorReq(after_type);

  ClassFieldDecl field;
  field.attrs = attrs_list;
  field.vis = vis.elem;
  field.key_boundary = boundary.elem;
  field.name = name.elem;
  field.type = ty.elem;
  field.span = SpanBetween(start, after_type);
  field.doc_opt = std::nullopt;

  return {after_type, field};
}

// =============================================================================
// ParseClassItemList - Parse list of class items
// =============================================================================

ParseElemResult<std::vector<ClassItem>> ParseClassItemList(Parser parser) {
  // Skip leading newlines
  SkipNewlines(parser);

  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-ClassItemList-End");
    SPEC_RULE("Parse-ClassItemList-Empty");
    return {parser, {}};
  }

  SPEC_RULE("Parse-ClassItemList-Cons");
  std::vector<ClassItem> items;
  Parser cur = parser;

  while (!IsPunc(cur, "}")) {
    // Skip newlines between items
    SkipNewlines(cur);
    if (IsPunc(cur, "}")) break;

    Parser before = cur;
    ParseElemResult<ClassItem> item = ParseClassItem(cur);
    items.push_back(item.elem);
    cur = item.parser;

    // Skip newlines after item
    SkipNewlines(cur);

    // Prevent infinite loop
    if (cur.tokens == before.tokens && cur.index == before.index) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      cur = AdvanceOrEOF(cur);
    }
  }

  SPEC_RULE("Parse-ClassItemList-End");
  return {cur, items};
}

// =============================================================================
// ParseClassBody - Parse class body in braces
// =============================================================================

ParseElemResult<std::vector<ClassItem>> ParseClassBody(Parser parser) {
  SPEC_RULE("Parse-ClassBody");

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

  ParseElemResult<std::vector<ClassItem>> items = ParseClassItemList(next);

  if (!IsPunc(items.parser, "}")) {
    EmitParseSyntaxErr(items.parser, TokSpan(items.parser));
    return {items.parser, items.elem};
  }

  Advance(items.parser);
  return {items.parser, items.elem};
}

// =============================================================================
// ParseSuperclassBoundsTail - Parse remaining superclasses after first
// =============================================================================

ParseElemResult<ClassPath> ParseClassBound(Parser parser) {
  ParseElemResult<ClassPath> cls = ParseClassPath(parser);
  Parser cur = cls.parser;
  if (IsOp(cur, "<")) {
    Advance(cur);
    if (!IsOp(cur, ">")) {
      ParseElemResult<std::shared_ptr<Type>> arg = ParseType(cur);
      cur = arg.parser;
      while (IsPunc(cur, ",")) {
        Advance(cur);
        ParseElemResult<std::shared_ptr<Type>> next_arg = ParseType(cur);
        cur = next_arg.parser;
      }
    }
    if (!IsOp(cur, ">")) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
    } else {
      Advance(cur);
    }
  }
  return {cur, cls.elem};
}

ParseElemResult<std::vector<ClassPath>> ParseSuperclassBoundsTail(
    Parser parser, std::vector<ClassPath> xs) {
  if (!IsOp(parser, "+")) {
    SPEC_RULE("Parse-SuperclassBoundsTail-End");
    return {parser, xs};
  }
  SPEC_RULE("Parse-SuperclassBoundsTail-Plus");
  Parser next = parser;
  Advance(next);
  ParseElemResult<ClassPath> cls = ParseClassBound(next);
  xs.push_back(cls.elem);
  return ParseSuperclassBoundsTail(cls.parser, std::move(xs));
}

// =============================================================================
// ParseSuperclassBounds - Parse superclass bounds
// =============================================================================
//
// Superclasses separated by + operator: Class1 + Class2 + Class3

ParseElemResult<std::vector<ClassPath>> ParseSuperclassBounds(Parser parser) {
  SPEC_RULE("Parse-SuperclassBounds-Cons");
  ParseElemResult<ClassPath> first = ParseClassBound(parser);
  std::vector<ClassPath> xs;
  xs.push_back(first.elem);
  return ParseSuperclassBoundsTail(first.parser, std::move(xs));
}

// =============================================================================
// ParseSuperclassOpt - Parse optional <: superclasses
// =============================================================================

ParseElemResult<std::vector<ClassPath>> ParseSuperclassOpt(Parser parser) {
  if (!IsOp(parser, "<:")) {
    SPEC_RULE("Parse-Superclass-None");
    return {parser, {}};
  }
  SPEC_RULE("Parse-Superclass-Yes");
  Parser next = parser;
  Advance(next);
  return ParseSuperclassBounds(next);
}

// =============================================================================
// ParseClassDecl - Parse complete class declaration
// =============================================================================
//
// SPEC: Parse-Class
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
//   Γ ⊢ ParseModalOpt(P_1) ⇓ (P_2, modal_opt)
//   IsKw(Tok(P_2), `class`)
//   Γ ⊢ ParseIdent(Advance(P_2)) ⇓ (P_3, name)
//   Γ ⊢ ParseGenericParamsOpt(P_3) ⇓ (P_4, gen_params_opt)
//   Γ ⊢ ParseSuperclassOpt(P_4) ⇓ (P_5, supers)
//   Γ ⊢ ParseWhereClauseOpt(P_5) ⇓ (P_6, where_clause_opt)
//   Γ ⊢ ParseClassBody(P_6) ⇓ (P_7, items)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (P_7, ⟨ClassDecl, ...⟩)
//
// Note: The `is_modal` parameter indicates if "modal" was seen before "class"

ParseItemResult ParseClassDecl(Parser parser, Visibility vis,
                               AttributeList attrs, bool is_modal) {
  SPEC_RULE("Parse-Class");
  Parser start = parser;

  // Already know we're at "class" keyword
  Advance(parser);  // consume "class"

  // Parse class name
  ParseElemResult<Identifier> name = ParseIdent(parser);
  parser = name.parser;

  // Parse optional generic parameters
  ParseElemResult<std::optional<GenericParams>> gen_params;
  if (name.elem == "Async") {
    // Appendix B.11 async_class/type_param grammar constrains Async params
    // to identifier with optional default type (no bounds).
    if (!IsOp(parser, "<")) {
      EmitParseSyntaxErr(parser, TokSpan(parser));
      gen_params = {parser, std::nullopt};
    } else {
      gen_params = ParseAsyncTypeParamsOpt(parser);
      parser = gen_params.parser;
    }
  } else {
    gen_params = ParseGenericParamsOpt(parser);
    parser = gen_params.parser;
  }

  // Parse optional superclasses
  ParseElemResult<std::vector<ClassPath>> supers = ParseSuperclassOpt(parser);
  parser = supers.parser;

  // Parse optional predicate clause
  ParseElemResult<std::optional<PredicateClause>> predicate_clause_opt =
      ParsePredicateClauseOpt(parser);
  parser = predicate_clause_opt.parser;

  // Parse class body
  ParseElemResult<std::vector<ClassItem>> items = ParseClassBody(parser);
  parser = items.parser;

  ClassDecl decl;
  decl.attrs = attrs;
  decl.vis = vis;
  decl.modal = is_modal;
  decl.name = name.elem;
  decl.generic_params = gen_params.elem;
  decl.predicate_clause_opt = predicate_clause_opt.elem;
  decl.supers = std::move(supers.elem);
  decl.items = std::move(items.elem);
  decl.span = SpanBetween(start, parser);
  decl.doc = {};

  RecordGenericPredicateOwnerClause("ClassDecl", decl.name,
                                    decl.generic_params,
                                    decl.predicate_clause_opt, decl.span);
  RecordNominalRelationFormOnOwnerDecl("ClassDecl", decl.name, "supers",
                                       decl.supers, decl.span);

  return {parser, decl};
}

}  // namespace ultraviolet::ast
