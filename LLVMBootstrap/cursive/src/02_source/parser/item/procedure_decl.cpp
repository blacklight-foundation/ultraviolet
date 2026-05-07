// =============================================================================
// procedure_decl.cpp - Procedure Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.5 (Procedure Declaration Rules)
//
// This file implements procedure declaration parsing:
//   - ParseProcedureDecl: Parse complete procedure declaration
//
// SYNTAX:
//   public procedure add(a: i32, b: i32) -> i32 { return a + b }
//   procedure swap<T; U>(a: T, b: U) -> (U, T) { ... }
//   procedure divide(a: i32, b: i32) -> i32 |: b != 0 { ... }
//
// MAIN SIGNATURE:
//   public procedure main(move ctx: Context) -> i32
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace cursive::ast
{

    // Use lexer types
    using cursive::lexer::Token;
    using cursive::lexer::TokenKind;

    // Forward declarations for helper functions
    bool IsKw(const Parser &parser, std::string_view kw);
    bool IsOp(const Parser &parser, std::string_view op);
    bool IsPunc(const Parser &parser, std::string_view p);

    // Forward declarations for signature and parsing functions
    struct SignatureResult
    {
        Parser parser;
        std::vector<Param> params;
        TypePtr return_type_opt;
    };

    SignatureResult ParseSignature(Parser parser);
    ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
        Parser parser);
    ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
        Parser parser);
    ParseElemResult<std::optional<ContractClause>> ParseContractClauseOpt(
        Parser parser);

    ParseItemResult ParseProcedureLikeDeclImpl(Parser parser, Visibility vis,
                                               AttributeList attrs,
                                               bool visibility_explicit,
                                               bool comptime_prefix)
    {
        Parser start = parser;

        Advance(parser); // consume "procedure"

        ParseElemResult<Identifier> name = ParseIdent(parser);
        parser = name.parser;

        ParseElemResult<std::optional<GenericParams>> gen_params =
            ParseGenericParamsOpt(parser);
        parser = gen_params.parser;

        SignatureResult sig = ParseSignature(parser);
        parser = sig.parser;

        std::optional<PredicateClause> predicate_clause_opt = std::nullopt;
        std::optional<ContractClause> contract_opt = std::nullopt;
        if (!comptime_prefix)
        {
            ParseElemResult<std::optional<PredicateClause>> predicate_clause =
                ParsePredicateClauseOpt(parser);
            parser = predicate_clause.parser;
            predicate_clause_opt = predicate_clause.elem;
        }

        ParseElemResult<std::optional<ContractClause>> contract =
            ParseContractClauseOpt(parser);
        parser = contract.parser;
        contract_opt = contract.elem;

        ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(parser);
        parser = body.parser;
        std::shared_ptr<Block> body_block = body.elem;

        if (comptime_prefix)
        {
            ComptimeProcedureDecl decl;
            decl.attrs = attrs;
            decl.vis = vis;
            decl.name = name.elem;
            decl.generic_params = gen_params.elem;
            decl.params = sig.params;
            decl.return_type_opt = sig.return_type_opt;
            decl.contract = contract_opt;
            decl.body = body_block;
            decl.span = SpanBetween(start, parser);
            decl.doc = {};
            return {parser, decl};
        }

        ProcedureDecl decl;
        decl.attrs = attrs;
        decl.vis = vis;
        decl.visibility_explicit = visibility_explicit;
        decl.name = name.elem;
        decl.generic_params = gen_params.elem;
        decl.predicate_clause_opt = predicate_clause_opt;
        decl.params = sig.params;
        decl.return_type_opt = sig.return_type_opt;
        decl.contract = contract_opt;
        decl.body = body_block;
        decl.span = SpanBetween(start, parser);
        decl.doc = {};
        return {parser, decl};
    }

    // =============================================================================
    // ParseProcedureDecl - Parse complete procedure declaration
    // =============================================================================
    //
    // SPEC: Parse-Procedure
    //   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
    //   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
    //   IsKw(Tok(P_1), `procedure`)
    //   Γ ⊢ ParseIdent(Advance(P_1)) ⇓ (P_2, name)
    //   Γ ⊢ ParseGenericParamsOpt(P_2) ⇓ (P_3, gen_params_opt)
    //   Γ ⊢ ParseSignature(P_3) ⇓ (P_4, params, ret_opt)
    //   Γ ⊢ ParsePredicateClauseOpt(P_4) ⇓ (P_5, predicate_clause_opt)
    //   Γ ⊢ ParseContractClauseOpt(P_5) ⇓ (P_6, contract_opt)
    //   Γ ⊢ ParseBlock(P_6) ⇓ (P_7, body)
    //   ────────────────────────────────────────────────────────────────────
    //   Γ ⊢ ParseItem(P) ⇓ (P_7, ⟨ProcedureDecl, attrs_opt, vis, name,
    //       gen_params_opt, predicate_clause_opt, params, ret_opt, contract_opt,
    //       body, SpanBetween(P, P_7), []⟩)

    ParseItemResult ParseProcedureDecl(Parser parser, Visibility vis,
                                       AttributeList attrs,
                                       bool visibility_explicit)
    {
        SPEC_RULE("Parse-Procedure");
        return ParseProcedureLikeDeclImpl(parser, vis, attrs,
                                          visibility_explicit, false);
    }

} // namespace cursive::ast
