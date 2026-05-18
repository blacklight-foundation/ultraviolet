// =============================================================================
// MIGRATION MAPPING: qualified_name.cpp
// =============================================================================
// This file implements parsing for qualified name expressions
// (module::path::name without call parentheses or record braces).
// These are references to items in other modules/namespaces.
//
// =============================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md
// =============================================================================
// Lines 5189-5192:
//
// **(Parse-Qualified-Name)**
// Γ ⊢ ParseQualifiedHead(P) ⇓ (P_1, path, name)
//     Tok(P_1) ∉ {Punctuator("("), Punctuator("{")}
//     ¬ IsOp(Tok(P_1), "@")
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePrimary(P) ⇓ (P_1, QualifiedName(path, name))
//
// SEMANTICS:
// - Parse qualified path via ParseQualifiedHead
// - After head, token must NOT be "(" (would be Parse-Qualified-Apply-Paren)
// - After head, token must NOT be "{" (would be Parse-Qualified-Apply-Brace)
// - After head, token must NOT be "@" (would be modal state reference)
// - Returns QualifiedNameExpr with the module path and final identifier name
//
// PREREQUISITE RULE (Lines 4078-4081):
//
// **(Parse-QualifiedHead)**
// Γ ⊢ ParseIdent(P) ⇓ (P_1, id_0)
//     IsOp(Tok(P_1), "::")
//     Γ ⊢ ParseModulePathTail(P_1, [id_0]) ⇓ (P_2, xs)
//     xs = ys ++ [name]
//     |xs| ≥ 2
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseQualifiedHead(P) ⇓ (P_2, ys, name)
//
// SEMANTICS:
// - Parse an identifier followed by "::" and more path segments
// - Result path must have at least 2 segments total
// - Returns (parser, module_path_without_last, last_segment_as_name)
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_paths.cpp
// =============================================================================
//
// ParseQualifiedHead function
// Source lines: 102-120
// -------------------------------------------------------------------------
// ParseQualifiedHeadResult ParseQualifiedHead(Parser parser) {
//   SPEC_RULE("Parse-QualifiedHead");
//   ParseElemResult<Identifier> head = ParseIdent(parser);
//   const Token* tok = Tok(head.parser);
//   if (!tok || !IsOpTok(*tok, "::")) {
//     EmitParseSyntaxErr(parser, TokSpan(parser));
//     return {parser, {}, "_"};
//   }
//
//   ParseElemResult<ModulePath> rest = ParseModulePathTail(head.parser, {head.elem});
//   ModulePath full = std::move(rest.elem);
//   if (full.size() < 2) {
//     EmitParseSyntaxErr(parser, TokSpan(parser));
//     return {rest.parser, full, "_"};
//   }
//   Identifier name = full.back();
//   full.pop_back();
//   return {rest.parser, full, name};
// }
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// Qualified name/apply entry and QualifiedName construction
// Source lines: 1142-1202
// -------------------------------------------------------------------------
//   if (tok && IsIdentTok(*tok)) {
//     Parser next = parser;
//     Advance(next);
//     const Token* look = Tok(next);
//     if (look && IsOpTok(*look, "::")) {
//       ParseQualifiedHeadResult head = ParseQualifiedHead(parser);
//       const Token* after_head = Tok(head.parser);
//       if (after_head && IsOpTok(*after_head, "<")) {
//         SPEC_RULE("Parse-Qualified-Generic-Unsupported");
//         // ... error handling for generics ...
//       }
//       if (after_head && IsPuncTok(*after_head, "(")) {
//         // ... Parse-Qualified-Apply-Paren (see qualified_apply.cpp) ...
//       }
//       if (allow_brace && after_head && IsPuncTok(*after_head, "{")) {
//         // ... Parse-Qualified-Apply-Brace (see qualified_apply.cpp) ...
//       }
//       if (after_head && !IsPuncTok(*after_head, "(") &&
//           !IsPuncTok(*after_head, "{") &&
//           !(after_head->kind == TokenKind::Operator && after_head->lexeme == "@")) {
//         SPEC_RULE("Parse-Qualified-Name");
//         QualifiedNameExpr qname;
//         qname.path = head.module_path;
//         qname.name = head.name;
//         return {head.parser, MakeExpr(SpanBetween(parser, head.parser), qname)};
//       }
//     }
//   }
//
// Specific QualifiedName construction lines: 1194-1202
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
// FROM expr_common.cpp:
// - MakeExpr(Span, ExprNode) -> ExprPtr
// - SpanBetween(Parser, Parser) -> Span
//
// FROM parser_paths.cpp (or shared utility):
// - ParseQualifiedHead(Parser) -> ParseQualifiedHeadResult
// - ParseQualifiedHeadResult struct { Parser parser; ModulePath module_path; Identifier name; }
// - ParseModulePathTail(Parser, ModulePath) -> ParseElemResult<ModulePath>
// - ParseIdent(Parser) -> ParseElemResult<Identifier>
//
// FROM parser_common.cpp:
// - Parser state type
// - Advance(Parser&) -> void
// - Tok(Parser) -> const Token*
//
// FROM lexer/token helpers:
// - IsIdentTok(Token) -> bool
// - IsOpTok(Token, const char*) -> bool
// - IsPuncTok(Token, const char*) -> bool
// - TokenKind::Operator
//
// FROM AST types:
// - QualifiedNameExpr struct { ModulePath path; Identifier name; }
// - ModulePath = std::vector<Identifier>
// - Identifier = std::string
// - ExprPtr = std::shared_ptr<Expr>
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
// 1. ParseQualifiedHead should remain in parser_paths.cpp as it's shared
//    between QualifiedName and QualifiedApply parsing.
//
// 2. Export functions:
//    a) For use after ParseQualifiedHead already called:
//       std::optional<ParseElemResult<ExprPtr>> TryBuildQualifiedNameExpr(
//           Parser original_parser,
//           const ParseQualifiedHeadResult& head);
//       Returns std::nullopt if followed by "(", "{", or "@".
//
//    b) Full standalone version:
//       std::optional<ParseElemResult<ExprPtr>> TryParseQualifiedNameExpr(
//           Parser parser);
//       Returns std::nullopt if not a qualified name expression.
//
// 3. The check for "<" (generics) emits an unsupported construct error.
//    This should be handled before falling into QualifiedName logic.
//
// 4. The condition for QualifiedName is the "else" case after checking for
//    paren/brace/modal. Consider making this more explicit:
//    if (!IsPunc && !IsOp("@")) -> QualifiedName
//
// 5. Note: The implementation in parser_expr.cpp checks `allow_brace` only
//    for the brace case, not for the final QualifiedName case. This means
//    QualifiedName can be parsed even when allow_brace=false.
// =============================================================================
