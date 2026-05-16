#include "05_codegen/lower/pattern/ir_pattern.h"

#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

namespace {

IRLiteralKind ToIRLiteralKind(lexer::TokenKind kind) {
  switch (kind) {
    case lexer::TokenKind::IntLiteral:
      return IRLiteralKind::Int;
    case lexer::TokenKind::FloatLiteral:
      return IRLiteralKind::Float;
    case lexer::TokenKind::StringLiteral:
      return IRLiteralKind::String;
    case lexer::TokenKind::CharLiteral:
      return IRLiteralKind::Char;
    case lexer::TokenKind::BoolLiteral:
      return IRLiteralKind::Bool;
    case lexer::TokenKind::NullLiteral:
      return IRLiteralKind::Null;
    case lexer::TokenKind::Identifier:
    case lexer::TokenKind::Keyword:
    case lexer::TokenKind::Operator:
    case lexer::TokenKind::Punctuator:
    case lexer::TokenKind::Newline:
    case lexer::TokenKind::Eof:
    case lexer::TokenKind::Unknown:
      return IRLiteralKind::Unknown;
  }
  return IRLiteralKind::Unknown;
}

IRPatternPtr LowerIRPatternPtr(const ast::PatternPtr& pattern, LowerCtx& ctx) {
  if (!pattern) {
    return nullptr;
  }
  return LowerIRPattern(*pattern, ctx);
}

IRFieldPattern LowerIRFieldPattern(const ast::FieldPattern& field,
                                   LowerCtx& ctx) {
  IRFieldPattern out;
  out.name = field.name;
  out.pattern = LowerIRPatternPtr(field.pattern_opt, ctx);
  return out;
}

std::vector<IRFieldPattern> LowerIRFieldPatterns(
    const std::vector<ast::FieldPattern>& fields,
    LowerCtx& ctx) {
  std::vector<IRFieldPattern> out;
  out.reserve(fields.size());
  for (const auto& field : fields) {
    out.push_back(LowerIRFieldPattern(field, ctx));
  }
  return out;
}

std::vector<IRPatternPtr> LowerIRPatternList(
    const std::vector<ast::PatternPtr>& patterns,
    LowerCtx& ctx) {
  std::vector<IRPatternPtr> out;
  out.reserve(patterns.size());
  for (const auto& pattern : patterns) {
    out.push_back(LowerIRPatternPtr(pattern, ctx));
  }
  return out;
}

}  // namespace

IRRangeKind ToIRRangeKind(ast::RangeKind kind) {
  switch (kind) {
    case ast::RangeKind::To:
      return IRRangeKind::To;
    case ast::RangeKind::ToInclusive:
      return IRRangeKind::ToInclusive;
    case ast::RangeKind::Full:
      return IRRangeKind::Full;
    case ast::RangeKind::From:
      return IRRangeKind::From;
    case ast::RangeKind::Exclusive:
      return IRRangeKind::Exclusive;
    case ast::RangeKind::Inclusive:
      return IRRangeKind::Inclusive;
  }
  return IRRangeKind::Full;
}

IRFenceOrder ToIRFenceOrder(ast::FenceOrder order) {
  switch (order) {
    case ast::FenceOrder::Acquire:
      return IRFenceOrder::Acquire;
    case ast::FenceOrder::Release:
      return IRFenceOrder::Release;
    case ast::FenceOrder::SeqCst:
      return IRFenceOrder::SeqCst;
  }
  return IRFenceOrder::SeqCst;
}

IRPatternPtr LowerIRPattern(const ast::Pattern& pattern, LowerCtx& ctx) {
  auto out = std::make_shared<IRPattern>();
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LiteralPattern>) {
          out->node = IRLiteralPattern{
              IRLiteral{ToIRLiteralKind(node.literal.kind), node.literal.lexeme}};
        } else if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
          out->node = IRWildcardPattern{};
        } else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          const std::string stable_name = ctx.StableBindingName(node.name);
          out->node = IRIdentifierPattern{
              stable_name.empty() ? node.name : stable_name};
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          analysis::TypeRef type;
          if (node.type) {
            if (const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(ScopeForLowering(ctx),
                                                        node.type)) {
              type = *lowered;
            }
          }
          const std::string stable_name = ctx.StableBindingName(node.name);
          out->node = IRTypedPattern{
              stable_name.empty() ? node.name : stable_name,
              type};
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          out->node = IRTuplePattern{LowerIRPatternList(node.elements, ctx)};
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          out->node = IRRecordPattern{node.path,
                                      LowerIRFieldPatterns(node.fields, ctx)};
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          IREnumPattern enum_pattern;
          enum_pattern.path = node.path;
          enum_pattern.name = node.name;
          if (node.payload_opt.has_value()) {
            enum_pattern.payload = std::visit(
                [&](const auto& payload) -> IREnumPayloadPattern {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                    return IRTuplePayloadPattern{
                        LowerIRPatternList(payload.elements, ctx)};
                  } else {
                    return IRRecordPayloadPattern{
                        LowerIRFieldPatterns(payload.fields, ctx)};
                  }
                },
                *node.payload_opt);
          }
          out->node = std::move(enum_pattern);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          IRModalPattern modal;
          modal.state = node.state;
          if (node.fields_opt.has_value()) {
            modal.fields =
                IRModalRecordPayload{LowerIRFieldPatterns(node.fields_opt->fields,
                                                          ctx)};
          }
          out->node = std::move(modal);
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          out->node = IRRangePattern{ToIRRangeKind(node.kind),
                                     LowerIRPatternPtr(node.lo, ctx),
                                     LowerIRPatternPtr(node.hi, ctx)};
        } else {
          out->node = IROpaquePattern{};
        }
      },
      pattern.node);
  return out;
}

}  // namespace cursive::codegen
