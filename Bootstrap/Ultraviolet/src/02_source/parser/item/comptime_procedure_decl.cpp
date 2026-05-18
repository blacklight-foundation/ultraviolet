// =============================================================================
// comptime_procedure_decl.cpp - Compile-Time Procedure Declaration Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

ParseItemResult ParseProcedureLikeDeclImpl(Parser parser, Visibility vis,
                                           AttributeList attrs,
                                           bool visibility_explicit,
                                           bool comptime_prefix);

bool IsKw(const Parser& parser, std::string_view kw);
void EmitParseSyntaxErr(Parser& parser, const core::Span& span);
void SyncItem(Parser& parser);
ParseElemResult<Visibility> ParseVis(Parser parser);

ParseItemResult ParseComptimeProcedureDecl(Parser parser, AttributeList attrs) {
  SPEC_RULE("Parse-CtProc");
  Parser start = parser;
  Advance(parser);  // consume "comptime"
  ParseElemResult<Visibility> vis = ParseVis(parser);
  const bool visibility_explicit = vis.parser.index != parser.index;
  Parser cur = vis.parser;
  if (!IsKw(cur, "procedure")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    Parser next = cur;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }
  ParseItemResult parsed =
      ParseProcedureLikeDeclImpl(cur, vis.elem, attrs, visibility_explicit, true);
  if (auto* decl = std::get_if<ComptimeProcedureDecl>(&parsed.item)) {
    decl->span = SpanBetween(start, parsed.parser);
  }
  return parsed;
}

}  // namespace ultraviolet::ast
