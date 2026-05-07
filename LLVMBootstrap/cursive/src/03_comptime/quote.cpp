#include "03_comptime/comptime_internal.h"

#include <optional>
#include <type_traits>
#include <utility>

#include "00_core/diagnostic_messages.h"
#include "00_core/ident.h"

namespace cursive::frontend::comptime_internal {

namespace {

ast::Parser MakeTokenParser(const std::vector<ast::Token>& tokens,
                            bool quote_mode) {
  static const std::vector<ast::DocComment> kNoDocs;

  ast::Parser parser;
  parser.owned_tokens = std::make_shared<std::vector<ast::Token>>(tokens);
  parser.tokens = parser.owned_tokens.get();
  parser.docs = &kNoDocs;
  parser.quote_mode = quote_mode;
  return parser;
}

template <typename Node>
ExprPtr MakeExpr(const core::Span& span, Node node) {
  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

template <typename Node>
TypePtr MakeType(const core::Span& span, Node node) {
  auto type = std::make_shared<ast::Type>();
  type->span = span;
  type->node = std::move(node);
  return type;
}

template <typename Node>
PatternPtr MakePattern(const core::Span& span, Node node) {
  auto pattern = std::make_shared<ast::Pattern>();
  pattern->span = span;
  pattern->node = std::move(node);
  return pattern;
}

struct ScopedQuoteCtx {
  CtEnv& env;
  std::optional<CtQuoteCtx> saved;

  ScopedQuoteCtx(CtEnv& env, ast::QuoteKind kind) : env(env), saved(env.quote_ctx) {
    env.quote_ctx = CtQuoteCtx{kind, CtSiteOf(env)};
  }

  ~ScopedQuoteCtx() {
    env.quote_ctx = saved;
  }
};

bool IsQuotedStatementForm(const ast::Stmt& stmt) {
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        return std::is_same_v<T, ast::LetStmt> ||
               std::is_same_v<T, ast::VarStmt> ||
               std::is_same_v<T, ast::UsingLocalStmt> ||
               std::is_same_v<T, ast::AssignStmt> ||
               std::is_same_v<T, ast::CompoundAssignStmt> ||
               std::is_same_v<T, ast::ExprStmt> ||
               std::is_same_v<T, ast::ReturnStmt> ||
               std::is_same_v<T, ast::BreakStmt> ||
               std::is_same_v<T, ast::ContinueStmt> ||
               std::is_same_v<T, ast::DeferStmt> ||
               std::is_same_v<T, ast::RegionStmt> ||
               std::is_same_v<T, ast::FrameStmt> ||
               std::is_same_v<T, ast::UnsafeBlockStmt> ||
               std::is_same_v<T, ast::KeyBlockStmt> ||
               std::is_same_v<T, ast::ComptimeStmt>;
      },
      stmt);
}

std::optional<ExprPtr> ParseWholeBodySpliceExpr(const QuoteExpr& quote) {
  ast::Parser parser = MakeTokenParser(quote.tokens, true);
  const std::size_t start_index = parser.index;
  ast::ParseElemResult<ExprPtr> parsed = ast::ParseExpr(parser);
  if (parsed.parser.index == start_index || !parsed.elem ||
      !ast::AtEof(parsed.parser)) {
    return std::nullopt;
  }
  if (const auto* splice = std::get_if<ast::SpliceExprNode>(&parsed.elem->node)) {
    return splice->expr;
  }
  return std::nullopt;
}

bool QuoteParsesAsKind(const QuoteExpr& quote, ast::QuoteKind kind) {
  if (kind == ast::QuoteKind::Item && ParseWholeBodySpliceExpr(quote).has_value()) {
    return true;
  }

  ast::Parser parser = MakeTokenParser(quote.tokens, true);
  const std::size_t start_index = parser.index;
  switch (kind) {
    case ast::QuoteKind::Expr: {
      ast::ParseElemResult<ExprPtr> parsed = ast::ParseExpr(parser);
      return parsed.parser.index != start_index && parsed.elem &&
             ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Stmt: {
      ast::ParseElemResult<Stmt> parsed = ast::ParseStmt(parser);
      return parsed.parser.index != start_index && IsQuotedStatementForm(parsed.elem) &&
             ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Unspecified:
      return false;
    case ast::QuoteKind::Item: {
      ast::ParseItemResult parsed = ast::ParseItem(parser);
      return parsed.parser.index != start_index &&
             !std::holds_alternative<ast::ErrorItem>(parsed.item) &&
             ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Type: {
      ast::ParseElemResult<TypePtr> parsed = ast::ParseType(parser);
      return parsed.parser.index != start_index && parsed.elem &&
             ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Pattern: {
      ast::ParseElemResult<PatternPtr> parsed = ast::ParsePattern(parser);
      return parsed.parser.index != start_index && parsed.elem &&
             ast::AtEof(parsed.parser);
    }
  }
  return false;
}

std::optional<CtValue> EvalSpliceValue(const ExprPtr& expr, CtEnv& env,
                                       const core::Span& span) {
  const EvalResult value = EvalExpr(expr, env);
  if (!value.ok) {
    EmitComptimeDiag(env, "E-CTE-0231", span);
    return std::nullopt;
  }
  return value.value;
}

bool RenderExprSplice(const CtValue& value, const core::Span& span,
                      ExprPtr& out) {
  if (const auto* ast_value = std::get_if<CtAst>(&value)) {
    if (ast_value->kind != CtAstKind::Expr) {
      return false;
    }
    if (const auto* expr = std::get_if<ExprPtr>(&ast_value->payload)) {
      out = *expr;
      return static_cast<bool>(out);
    }
    return false;
  }

  out = LiteralizeValue(value, span);
  return static_cast<bool>(out);
}

bool RenderStmtSplice(const CtValue& value, Stmt& out) {
  if (const auto* ast_value = std::get_if<CtAst>(&value)) {
    if (ast_value->kind == CtAstKind::Stmt) {
      if (const auto* stmt = std::get_if<Stmt>(&ast_value->payload)) {
        out = *stmt;
        return true;
      }
      return false;
    }
    if (ast_value->kind == CtAstKind::Expr) {
      const auto* expr = std::get_if<ExprPtr>(&ast_value->payload);
      if (!expr || !*expr) {
        return false;
      }
      ast::ExprStmt stmt;
      stmt.value = *expr;
      stmt.span = (*expr)->span;
      out = std::move(stmt);
      return true;
    }
  }
  return false;
}

bool RenderItemSplice(const CtValue& value, ASTItem& out) {
  if (const auto* ast_value = std::get_if<CtAst>(&value)) {
    if (ast_value->kind != CtAstKind::Item) {
      return false;
    }
    if (const auto* item = std::get_if<ASTItem>(&ast_value->payload)) {
      out = *item;
      return true;
    }
  }
  return false;
}

bool RenderTypeSplice(const CtValue& value, TypePtr& out) {
  if (const auto* ast_value = std::get_if<CtAst>(&value)) {
    if (ast_value->kind != CtAstKind::Type) {
      return false;
    }
    if (const auto* type = std::get_if<TypePtr>(&ast_value->payload)) {
      out = *type;
      return static_cast<bool>(out);
    }
    return false;
  }
  if (const auto* ct_type = std::get_if<CtType>(&value)) {
    out = ct_type->type;
    return static_cast<bool>(out);
  }
  return false;
}

bool RenderPatternSplice(const CtValue& value, PatternPtr& out) {
  if (const auto* ast_value = std::get_if<CtAst>(&value)) {
    if (ast_value->kind != CtAstKind::Pattern) {
      return false;
    }
    if (const auto* pattern = std::get_if<PatternPtr>(&ast_value->payload)) {
      out = *pattern;
      return static_cast<bool>(out);
    }
  }
  return false;
}

enum class RenderIdentifierStatus {
  Ok,
  Incompatible,
  Invalid,
};

RenderIdentifierStatus RenderIdentifierSplice(const CtValue& value,
                                              Identifier& out) {
  const auto* text = std::get_if<CtString>(&value);
  if (!text) {
    return RenderIdentifierStatus::Incompatible;
  }
  if (!core::IsName(text->value)) {
    return RenderIdentifierStatus::Invalid;
  }
  out = text->value;
  return RenderIdentifierStatus::Ok;
}

bool BuildSplicedIdentifierInPlace(std::optional<ast::SpliceIdentNode>& splice_opt,
                                   Identifier& name,
                                   CtEnv& env) {
  if (!splice_opt.has_value()) {
    return true;
  }
  auto value = EvalSpliceValue(splice_opt->name_expr, env, splice_opt->span);
  if (!value.has_value()) {
    return false;
  }
  switch (RenderIdentifierSplice(*value, name)) {
    case RenderIdentifierStatus::Ok:
      return true;
    case RenderIdentifierStatus::Incompatible:
      EmitComptimeDiag(env, "E-CTE-0230", splice_opt->span);
      return false;
    case RenderIdentifierStatus::Invalid:
      EmitComptimeDiag(env, "E-CTE-0232", splice_opt->span);
      return false;
  }
  return false;
}

std::optional<ExprPtr> BuildExpr(const ExprPtr& expr, CtEnv& env);
std::optional<TypePtr> BuildType(const TypePtr& type, CtEnv& env);
std::optional<PatternPtr> BuildPattern(const PatternPtr& pattern, CtEnv& env);
std::optional<Stmt> BuildStmt(const Stmt& stmt, CtEnv& env);
std::optional<Block> BuildBlock(const Block& block, CtEnv& env);
std::optional<ASTItem> BuildItem(const ASTItem& item, CtEnv& env);

bool BuildExprInPlace(ExprPtr& expr, CtEnv& env);
bool BuildTypeInPlace(TypePtr& type, CtEnv& env);
bool BuildPatternInPlace(PatternPtr& pattern, CtEnv& env);

bool BuildExprInPlace(ExprPtr& expr, CtEnv& env) {
  if (!expr) {
    return true;
  }
  auto built = BuildExpr(expr, env);
  if (!built.has_value()) {
    return false;
  }
  expr = std::move(*built);
  return true;
}

bool BuildTypeInPlace(TypePtr& type, CtEnv& env) {
  if (!type) {
    return true;
  }
  auto built = BuildType(type, env);
  if (!built.has_value()) {
    return false;
  }
  type = std::move(*built);
  return true;
}

bool BuildPatternInPlace(PatternPtr& pattern, CtEnv& env) {
  if (!pattern) {
    return true;
  }
  auto built = BuildPattern(pattern, env);
  if (!built.has_value()) {
    return false;
  }
  pattern = std::move(*built);
  return true;
}

bool BuildArgInPlace(ast::Arg& arg, CtEnv& env) {
  return BuildExprInPlace(arg.value, env);
}

bool BuildFieldInitInPlace(ast::FieldInit& field, CtEnv& env) {
  return BuildExprInPlace(field.value, env);
}

bool BuildApplyArgsInPlace(ast::ApplyArgs& args, CtEnv& env) {
  return std::visit(
      [&](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ParenArgs>) {
          for (auto& arg : node.args) {
            if (!BuildArgInPlace(arg, env)) {
              return false;
            }
          }
          return true;
        } else {
          for (auto& field : node.fields) {
            if (!BuildFieldInitInPlace(field, env)) {
              return false;
            }
          }
          return true;
        }
      },
      args);
}

bool BuildEnumPayloadInPlace(std::optional<ast::EnumPayload>& payload_opt,
                             CtEnv& env) {
  if (!payload_opt.has_value()) {
    return true;
  }
  return std::visit(
      [&](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::EnumPayloadParen>) {
          for (auto& elem : node.elements) {
            if (!BuildExprInPlace(elem, env)) {
              return false;
            }
          }
          return true;
        } else {
          for (auto& field : node.fields) {
            if (!BuildFieldInitInPlace(field, env)) {
              return false;
            }
          }
          return true;
        }
      },
      *payload_opt);
}

bool BuildLoopInvariantInPlace(std::optional<ast::LoopInvariant>& invariant_opt,
                               CtEnv& env) {
  if (!invariant_opt.has_value()) {
    return true;
  }
  return BuildExprInPlace(invariant_opt->predicate, env);
}

bool BuildRaceHandlerInPlace(ast::RaceHandler& handler, CtEnv& env) {
  return BuildExprInPlace(handler.value, env);
}

bool BuildRaceArmInPlace(ast::RaceArm& arm, CtEnv& env) {
  return BuildExprInPlace(arm.expr, env) &&
         BuildPatternInPlace(arm.pattern, env) &&
         BuildRaceHandlerInPlace(arm.handler, env);
}

bool BuildParallelOptionInPlace(ast::ParallelOption& opt, CtEnv& env) {
  return BuildExprInPlace(opt.value, env);
}

bool BuildSpawnOptionInPlace(ast::SpawnOption& opt, CtEnv& env) {
  return BuildExprInPlace(opt.value, env);
}

bool BuildDispatchOptionInPlace(ast::DispatchOption& opt, CtEnv& env) {
  return BuildExprInPlace(opt.chunk_expr, env) &&
         BuildExprInPlace(opt.workgroup_expr, env);
}

bool BuildIfCaseClauseInPlace(ast::IfCaseClause& clause, CtEnv& env) {
  return BuildPatternInPlace(clause.pattern, env) &&
         BuildExprInPlace(clause.body, env);
}

bool BuildKeyPathExprInPlace(ast::KeyPathExpr& path, CtEnv& env) {
  for (auto& seg : path.segs) {
    if (auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
      if (!BuildExprInPlace(index->expr, env)) {
        return false;
      }
    }
  }
  return true;
}

std::optional<TypePtr> BuildType(const TypePtr& type, CtEnv& env) {
  if (!type) {
    return type;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<TypePtr> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          auto value = EvalSpliceValue(node.expr, env, node.span);
          if (!value.has_value()) {
            return std::nullopt;
          }
          TypePtr rendered;
          if (!RenderTypeSplice(*value, rendered)) {
            EmitComptimeDiag(env, "E-CTE-0230", node.span);
            return std::nullopt;
          }
          return rendered;
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          auto out = node;
          if (!BuildTypeInPlace(out.base, env)) {
            return std::nullopt;
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          auto out = node;
          for (auto& elem : out.types) {
            if (!BuildTypeInPlace(elem, env)) {
              return std::nullopt;
            }
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          auto out = node;
          for (auto& param : out.params) {
            if (!BuildTypeInPlace(param.type, env) ||
                !BuildTypeInPlace(out.ret, env)) {
              return std::nullopt;
            }
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          auto out = node;
          for (auto& param : out.params) {
            if (!BuildTypeInPlace(param.type, env)) {
              return std::nullopt;
            }
          }
          if (!BuildTypeInPlace(out.ret, env)) {
            return std::nullopt;
          }
          if (out.deps_opt.has_value()) {
            for (auto& dep : *out.deps_opt) {
              if (!BuildTypeInPlace(dep.type, env)) {
                return std::nullopt;
              }
            }
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          auto out = node;
          for (auto& elem : out.elements) {
            if (!BuildTypeInPlace(elem, env)) {
              return std::nullopt;
            }
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          auto out = node;
          if (!BuildTypeInPlace(out.element, env) ||
              !BuildExprInPlace(out.length, env)) {
            return std::nullopt;
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          auto out = node;
          if (!BuildTypeInPlace(out.element, env)) {
            return std::nullopt;
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          auto out = node;
          if (!BuildTypeInPlace(out.element, env)) {
            return std::nullopt;
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          auto out = node;
          if (!BuildTypeInPlace(out.element, env)) {
            return std::nullopt;
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          auto out = node;
          for (auto& arg : out.generic_args) {
            if (!BuildTypeInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          auto out = node;
          for (auto& arg : out.generic_args) {
            if (!BuildTypeInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          auto out = node;
          for (auto& arg : out.args) {
            if (!BuildTypeInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          auto out = node;
          if (!BuildTypeInPlace(out.base, env) ||
              !BuildExprInPlace(out.predicate, env)) {
            return std::nullopt;
          }
          return MakeType(type->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive>) {
          auto out = node;
          if (!BuildTypeInPlace(out.base, env)) {
            return std::nullopt;
          }
          return MakeType(type->span, std::move(out));
        } else {
          return type;
        }
      },
      type->node);
}

std::optional<PatternPtr> BuildPattern(const PatternPtr& pattern, CtEnv& env) {
  if (!pattern) {
    return pattern;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<PatternPtr> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          auto value = EvalSpliceValue(node.expr, env, node.span);
          if (!value.has_value()) {
            return std::nullopt;
          }
          PatternPtr rendered;
          if (!RenderPatternSplice(*value, rendered)) {
            EmitComptimeDiag(env, "E-CTE-0230", node.span);
            return std::nullopt;
          }
          return rendered;
        } else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          auto out = node;
          if (!BuildSplicedIdentifierInPlace(out.name_splice_opt, out.name, env)) {
            return std::nullopt;
          }
          return MakePattern(pattern->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          auto out = node;
          if (!BuildSplicedIdentifierInPlace(out.name_splice_opt, out.name, env) ||
              !BuildTypeInPlace(out.type, env)) {
            return std::nullopt;
          }
          return MakePattern(pattern->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          auto out = node;
          for (auto& elem : out.elements) {
            if (!BuildPatternInPlace(elem, env)) {
              return std::nullopt;
            }
          }
          return MakePattern(pattern->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          auto out = node;
          for (auto& field : out.fields) {
            if (!BuildPatternInPlace(field.pattern_opt, env)) {
              return std::nullopt;
            }
          }
          return MakePattern(pattern->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          auto out = node;
          if (out.payload_opt.has_value()) {
            bool ok = std::visit(
                [&](auto& payload) -> bool {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                    for (auto& elem : payload.elements) {
                      if (!BuildPatternInPlace(elem, env)) {
                        return false;
                      }
                    }
                    return true;
                  } else {
                    for (auto& field : payload.fields) {
                      if (!BuildPatternInPlace(field.pattern_opt, env)) {
                        return false;
                      }
                    }
                    return true;
                  }
                },
                *out.payload_opt);
            if (!ok) {
              return std::nullopt;
            }
          }
          return MakePattern(pattern->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          auto out = node;
          if (out.fields_opt.has_value()) {
            for (auto& field : out.fields_opt->fields) {
              if (!BuildPatternInPlace(field.pattern_opt, env)) {
                return std::nullopt;
              }
            }
          }
          return MakePattern(pattern->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          auto out = node;
          if (!BuildPatternInPlace(out.lo, env) ||
              !BuildPatternInPlace(out.hi, env)) {
            return std::nullopt;
          }
          return MakePattern(pattern->span, std::move(out));
        } else {
          return pattern;
        }
      },
      pattern->node);
}

std::optional<Stmt> BuildStmt(const Stmt& stmt, CtEnv& env) {
  return std::visit(
      [&](const auto& node) -> std::optional<Stmt> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          auto out = node;
          if (!BuildPatternInPlace(out.binding.pat, env) ||
              !BuildTypeInPlace(out.binding.type_opt, env) ||
              !BuildExprInPlace(out.binding.init, env)) {
            return std::nullopt;
          }
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          auto out = node;
          if (!BuildPatternInPlace(out.binding.pat, env) ||
              !BuildTypeInPlace(out.binding.type_opt, env) ||
              !BuildExprInPlace(out.binding.init, env)) {
            return std::nullopt;
          }
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          auto out = node;
          if (!BuildSplicedIdentifierInPlace(out.source_splice_opt, out.source, env) ||
              !BuildSplicedIdentifierInPlace(out.alias_splice_opt, out.alias, env)) {
            return std::nullopt;
          }
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          auto out = node;
          if (!BuildExprInPlace(out.place, env) ||
              !BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          auto out = node;
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          auto out = node;
          if (!BuildExprInPlace(out.opts_opt, env)) {
            return std::nullopt;
          }
          if (out.alias_splice_opt.has_value()) {
            out.alias_opt.emplace("_");
            if (!BuildSplicedIdentifierInPlace(out.alias_splice_opt,
                                               *out.alias_opt, env)) {
              out.alias_opt.reset();
              return std::nullopt;
            }
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          auto out = node;
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          auto out = node;
          if (!BuildExprInPlace(out.value_opt, env)) {
            return std::nullopt;
          }
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          return stmt;
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          auto out = node;
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ComptimeStmt>) {
          auto out = node;
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          auto out = node;
          for (auto& path : out.paths) {
            if (!BuildKeyPathExprInPlace(path, env)) {
              return std::nullopt;
            }
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return Stmt{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ErrorStmt>) {
          return stmt;
        } else {
          return std::nullopt;
        }
      },
      stmt);
}

std::optional<Block> BuildBlock(const Block& block, CtEnv& env) {
  Block out = block;
  for (auto& stmt : out.stmts) {
    auto built = BuildStmt(stmt, env);
    if (!built.has_value()) {
      return std::nullopt;
    }
    stmt = std::move(*built);
  }
  if (!BuildExprInPlace(out.tail_opt, env)) {
    return std::nullopt;
  }
  return out;
}

bool BuildParamInPlace(ast::Param& param, CtEnv& env) {
  return BuildSplicedIdentifierInPlace(param.name_splice_opt, param.name, env) &&
         BuildTypeInPlace(param.type, env);
}

bool BuildGenericParamsInPlace(std::optional<ast::GenericParams>& params_opt,
                               CtEnv& env) {
  if (!params_opt.has_value()) {
    return true;
  }
  for (auto& param : params_opt->params) {
    if (!BuildTypeInPlace(param.default_type, env)) {
      return false;
    }
  }
  return true;
}

bool BuildWhereClauseInPlace(std::optional<ast::PredicateClause>& clause_opt,
                             CtEnv& env) {
  if (!clause_opt.has_value()) {
    return true;
  }
  for (auto& predicate : *clause_opt) {
    if (!BuildTypeInPlace(predicate.type, env)) {
      return false;
    }
  }
  return true;
}

bool BuildContractClauseInPlace(std::optional<ast::ContractClause>& clause_opt,
                                CtEnv& env) {
  if (!clause_opt.has_value()) {
    return true;
  }
  return BuildExprInPlace(clause_opt->precondition, env) &&
         BuildExprInPlace(clause_opt->postcondition, env);
}

bool BuildTypeInvariantInPlace(std::optional<ast::TypeInvariant>& invariant_opt,
                               CtEnv& env) {
  if (!invariant_opt.has_value()) {
    return true;
  }
  return BuildExprInPlace(invariant_opt->predicate, env);
}

bool BuildReceiverInPlace(ast::Receiver& receiver, CtEnv& env) {
  if (auto* explicit_recv = std::get_if<ast::ReceiverExplicit>(&receiver)) {
    return BuildTypeInPlace(explicit_recv->type, env);
  }
  return true;
}

bool BuildFieldDeclInPlace(ast::FieldDecl& field, CtEnv& env) {
  return BuildTypeInPlace(field.type, env) &&
         BuildExprInPlace(field.init_opt, env);
}

bool BuildMethodDeclInPlace(ast::MethodDecl& method, CtEnv& env) {
  if (!BuildGenericParamsInPlace(method.generic_params, env) ||
      !BuildReceiverInPlace(method.receiver, env)) {
    return false;
  }
  for (auto& param : method.params) {
    if (!BuildParamInPlace(param, env)) {
      return false;
    }
  }
  if (!BuildTypeInPlace(method.return_type_opt, env) ||
      !BuildContractClauseInPlace(method.contract, env)) {
    return false;
  }
  auto body = BuildBlock(*method.body, env);
  if (!body.has_value()) {
    return false;
  }
  method.body = std::make_shared<Block>(std::move(*body));
  return true;
}

bool BuildAssociatedTypeDeclInPlace(ast::AssociatedTypeDecl& assoc, CtEnv& env) {
  return BuildTypeInPlace(assoc.default_type, env);
}

bool BuildRecordMemberInPlace(ast::RecordMember& member, CtEnv& env) {
  return std::visit(
      [&](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::FieldDecl>) {
          return BuildFieldDeclInPlace(node, env);
        } else if constexpr (std::is_same_v<T, ast::MethodDecl>) {
          return BuildMethodDeclInPlace(node, env);
        } else {
          return BuildAssociatedTypeDeclInPlace(node, env);
        }
      },
      member);
}

bool BuildStateMemberInPlace(ast::StateMember& member, CtEnv& env) {
  return std::visit(
      [&](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::StateFieldDecl>) {
          return BuildTypeInPlace(node.type, env);
        } else if constexpr (std::is_same_v<T, ast::StateMethodDecl>) {
          if (!BuildGenericParamsInPlace(node.generic_params, env) ||
              !BuildReceiverInPlace(node.receiver, env)) {
            return false;
          }
          for (auto& param : node.params) {
            if (!BuildParamInPlace(param, env)) {
              return false;
            }
          }
          if (!BuildTypeInPlace(node.return_type_opt, env) ||
              !BuildContractClauseInPlace(node.contract, env)) {
            return false;
          }
          auto body = BuildBlock(*node.body, env);
          if (!body.has_value()) {
            return false;
          }
          node.body = std::make_shared<Block>(std::move(*body));
          return true;
        } else {
          for (auto& param : node.params) {
            if (!BuildParamInPlace(param, env)) {
              return false;
            }
          }
          auto body = BuildBlock(*node.body, env);
          if (!body.has_value()) {
            return false;
          }
          node.body = std::make_shared<Block>(std::move(*body));
          return true;
        }
      },
      member);
}

bool BuildClassItemInPlace(ast::ClassItem& item, CtEnv& env) {
  return std::visit(
      [&](auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ClassFieldDecl>) {
          return BuildTypeInPlace(node.type, env);
        } else if constexpr (std::is_same_v<T, ast::ClassMethodDecl>) {
          if (!BuildGenericParamsInPlace(node.generic_params, env) ||
              !BuildReceiverInPlace(node.receiver, env)) {
            return false;
          }
          for (auto& param : node.params) {
            if (!BuildParamInPlace(param, env)) {
              return false;
            }
          }
          if (!BuildTypeInPlace(node.return_type_opt, env) ||
              !BuildContractClauseInPlace(node.contract, env)) {
            return false;
          }
          if (!node.body_opt) {
            return true;
          }
          auto body = BuildBlock(*node.body_opt, env);
          if (!body.has_value()) {
            return false;
          }
          node.body_opt = std::make_shared<Block>(std::move(*body));
          return true;
        } else if constexpr (std::is_same_v<T, ast::AssociatedTypeDecl>) {
          return BuildAssociatedTypeDeclInPlace(node, env);
        } else if constexpr (std::is_same_v<T, ast::AbstractFieldDecl>) {
          return BuildTypeInPlace(node.type, env);
        } else {
          for (auto& field : node.fields) {
            if (!BuildTypeInPlace(field.type, env)) {
              return false;
            }
          }
          return true;
        }
      },
      item);
}

std::optional<ExprPtr> BuildExpr(const ExprPtr& expr, CtEnv& env);

std::optional<ASTItem> BuildItem(const ASTItem& item, CtEnv& env) {
  return std::visit(
      [&](const auto& node) -> std::optional<ASTItem> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl> ||
                      std::is_same_v<T, ast::ImportDecl> ||
                      std::is_same_v<T, ast::ErrorItem>) {
          return item;
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          auto out = node;
          for (auto& extern_item : out.items) {
            if (auto* proc = std::get_if<ast::ExternProcDecl>(&extern_item)) {
              if (!BuildGenericParamsInPlace(proc->generic_params, env) ||
                  !BuildWhereClauseInPlace(proc->where_clause, env)) {
                return std::nullopt;
              }
              for (auto& param : proc->params) {
                if (!BuildParamInPlace(param, env)) {
                  return std::nullopt;
                }
              }
              if (!BuildTypeInPlace(proc->return_type_opt, env) ||
                  !BuildContractClauseInPlace(proc->contract, env)) {
                return std::nullopt;
              }
              if (proc->foreign_contracts_opt.has_value()) {
                for (auto& clause : *proc->foreign_contracts_opt) {
                  for (auto& predicate : clause.predicates) {
                    if (!BuildExprInPlace(predicate, env)) {
                      return std::nullopt;
                    }
                  }
                }
              }
            }
          }
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          auto out = node;
          if (!BuildPatternInPlace(out.binding.pat, env) ||
              !BuildTypeInPlace(out.binding.type_opt, env) ||
              !BuildExprInPlace(out.binding.init, env)) {
            return std::nullopt;
          }
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          auto out = node;
          if (!BuildGenericParamsInPlace(out.generic_params, env)) {
            return std::nullopt;
          }
          for (auto& param : out.params) {
            if (!BuildParamInPlace(param, env)) {
              return std::nullopt;
            }
          }
          if (!BuildTypeInPlace(out.return_type_opt, env) ||
              !BuildWhereClauseInPlace(out.predicate_clause_opt, env) ||
              !BuildContractClauseInPlace(out.contract, env)) {
            return std::nullopt;
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          auto out = node;
          if (!BuildGenericParamsInPlace(out.generic_params, env)) {
            return std::nullopt;
          }
          for (auto& param : out.params) {
            if (!BuildParamInPlace(param, env)) {
              return std::nullopt;
            }
          }
          if (!BuildTypeInPlace(out.return_type_opt, env) ||
              !BuildContractClauseInPlace(out.contract, env)) {
            return std::nullopt;
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          auto out = node;
          if (!BuildGenericParamsInPlace(out.generic_params, env) ||
              !BuildWhereClauseInPlace(out.predicate_clause_opt, env) ||
              !BuildTypeInvariantInPlace(out.invariant_opt, env)) {
            return std::nullopt;
          }
          for (auto& member : out.members) {
            if (!BuildRecordMemberInPlace(member, env)) {
              return std::nullopt;
            }
          }
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          auto out = node;
          if (!BuildGenericParamsInPlace(out.generic_params, env) ||
              !BuildWhereClauseInPlace(out.predicate_clause_opt, env) ||
              !BuildTypeInvariantInPlace(out.invariant_opt, env)) {
            return std::nullopt;
          }
          for (auto& variant : out.variants) {
            if (variant.payload_opt.has_value()) {
              const bool ok = std::visit(
                  [&](auto& payload) -> bool {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
                      for (auto& elem : payload.elements) {
                        if (!BuildTypeInPlace(elem, env)) {
                          return false;
                        }
                      }
                      return true;
                    } else {
                      for (auto& field : payload.fields) {
                        if (!BuildFieldDeclInPlace(field, env)) {
                          return false;
                        }
                      }
                      return true;
                    }
                  },
                  *variant.payload_opt);
              if (!ok) {
                return std::nullopt;
              }
            }
          }
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          auto out = node;
          if (!BuildGenericParamsInPlace(out.generic_params, env) ||
              !BuildWhereClauseInPlace(out.predicate_clause_opt, env) ||
              !BuildTypeInvariantInPlace(out.invariant_opt, env)) {
            return std::nullopt;
          }
          for (auto& state : out.states) {
            for (auto& member : state.members) {
              if (!BuildStateMemberInPlace(member, env)) {
                return std::nullopt;
              }
            }
          }
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          auto out = node;
          if (!BuildGenericParamsInPlace(out.generic_params, env) ||
              !BuildWhereClauseInPlace(out.predicate_clause_opt, env)) {
            return std::nullopt;
          }
          for (auto& class_item : out.items) {
            if (!BuildClassItemInPlace(class_item, env)) {
              return std::nullopt;
            }
          }
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          auto out = node;
          if (!BuildGenericParamsInPlace(out.generic_params, env) ||
              !BuildTypeInPlace(out.type, env) ||
              !BuildWhereClauseInPlace(out.predicate_clause_opt, env)) {
            return std::nullopt;
          }
          return ASTItem{std::move(out)};
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          auto out = node;
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return ASTItem{std::move(out)};
        } else {
          return item;
        }
      },
      item);
}

std::optional<ExprPtr> BuildExpr(const ExprPtr& expr, CtEnv& env) {
  if (!expr) {
    return expr;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<ExprPtr> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          auto value = EvalSpliceValue(node.expr, env, node.span);
          if (!value.has_value()) {
            return std::nullopt;
          }
          ExprPtr rendered;
          if (!RenderExprSplice(*value, node.span, rendered)) {
            EmitComptimeDiag(env, "E-CTE-0230", node.span);
            return std::nullopt;
          }
          return rendered;
        } else if constexpr (std::is_same_v<T, ast::SpliceIdentNode>) {
          auto value = EvalSpliceValue(node.name_expr, env, node.span);
          if (!value.has_value()) {
            return std::nullopt;
          }
          Identifier rendered;
          switch (RenderIdentifierSplice(*value, rendered)) {
            case RenderIdentifierStatus::Ok: {
              ast::IdentifierExpr ident;
              ident.name = std::move(rendered);
              ident.from_splice = true;
              return MakeExpr(expr->span, std::move(ident));
            }
            case RenderIdentifierStatus::Incompatible:
              EmitComptimeDiag(env, "E-CTE-0230", node.span);
              return std::nullopt;
            case RenderIdentifierStatus::Invalid:
              EmitComptimeDiag(env, "E-CTE-0232", node.span);
              return std::nullopt;
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.lhs, env) ||
              !BuildExprInPlace(out.rhs, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto out = node;
          if (!BuildApplyArgsInPlace(out.args, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.callee, env)) {
            return std::nullopt;
          }
          for (auto& arg : out.generic_args) {
            if (!BuildTypeInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          for (auto& arg : out.args) {
            if (!BuildArgInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.receiver, env)) {
            return std::nullopt;
          }
          for (auto& arg : out.args) {
            if (!BuildArgInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.lhs, env) ||
              !BuildExprInPlace(out.rhs, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env) ||
              !BuildTypeInPlace(out.type, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.place, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.place, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TupleExpr> ||
                             std::is_same_v<T, ast::ArrayExpr>) {
          auto out = node;
          if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (auto& elem : out.elements) {
              if (!BuildExprInPlace(elem, env)) {
                return std::nullopt;
              }
            }
          } else {
            bool ok = true;
            ast::ForEachArrayExprSubexpr(out, [&](auto& elem) {
              if (!ok || !BuildExprInPlace(elem, env)) {
                ok = false;
              }
            });
            if (!ok) {
              return std::nullopt;
            }
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env) ||
              !BuildExprInPlace(out.count, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          auto out = node;
          for (auto& field : out.fields) {
            if (!BuildFieldInitInPlace(field, env)) {
              return std::nullopt;
            }
          }
          const bool target_ok = std::visit(
              [&](auto& target) -> bool {
                using Target = std::decay_t<decltype(target)>;
                if constexpr (std::is_same_v<Target, ast::ModalStateRef>) {
                  for (auto& arg : target.generic_args) {
                    if (!BuildTypeInPlace(arg, env)) {
                      return false;
                    }
                  }
                }
                return true;
              },
              out.target);
          if (!target_ok) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          auto out = node;
          if (!BuildEnumPayloadInPlace(out.payload_opt, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          auto out = node;
          if (!BuildTypeInPlace(out.type, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          auto out = node;
          if (!BuildTypeInPlace(out.type, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.cond, env) ||
              !BuildExprInPlace(out.then_expr, env) ||
              !BuildExprInPlace(out.else_expr, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.scrutinee, env) ||
              !BuildPatternInPlace(out.pattern, env) ||
              !BuildExprInPlace(out.then_expr, env) ||
              !BuildExprInPlace(out.else_expr, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.scrutinee, env)) {
            return std::nullopt;
          }
          for (auto& clause : out.cases) {
            if (!BuildIfCaseClauseInPlace(clause, env)) {
              return std::nullopt;
            }
          }
          if (!BuildExprInPlace(out.else_expr, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          auto out = node;
          if (!BuildLoopInvariantInPlace(out.invariant_opt, env)) {
            return std::nullopt;
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.cond, env) ||
              !BuildLoopInvariantInPlace(out.invariant_opt, env)) {
            return std::nullopt;
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          auto out = node;
          if (!BuildPatternInPlace(out.pattern, env) ||
              !BuildTypeInPlace(out.type_opt, env) ||
              !BuildExprInPlace(out.iter, env) ||
              !BuildLoopInvariantInPlace(out.invariant_opt, env)) {
            return std::nullopt;
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          auto out = node;
          auto block = BuildBlock(*out.block, env);
          if (!block.has_value()) {
            return std::nullopt;
          }
          out.block = std::make_shared<Block>(std::move(*block));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          auto out = node;
          auto block = BuildBlock(*out.block, env);
          if (!block.has_value()) {
            return std::nullopt;
          }
          out.block = std::make_shared<Block>(std::move(*block));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.body, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.cond, env)) {
            return std::nullopt;
          }
          auto then_block = BuildBlock(*out.then_block, env);
          if (!then_block.has_value()) {
            return std::nullopt;
          }
          out.then_block = std::make_shared<Block>(std::move(*then_block));
          if (out.else_block_opt) {
            auto else_block = BuildBlock(*out.else_block_opt, env);
            if (!else_block.has_value()) {
              return std::nullopt;
            }
            out.else_block_opt = std::make_shared<Block>(std::move(*else_block));
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          auto out = node;
          if (!BuildPatternInPlace(out.pattern, env) ||
              !BuildTypeInPlace(out.type_opt, env) ||
              !BuildExprInPlace(out.iter, env)) {
            return std::nullopt;
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.expr, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.expr, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          auto out = node;
          if (!BuildTypeInPlace(out.type, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          auto out = node;
          if (!BuildTypeInPlace(out.from, env) ||
              !BuildTypeInPlace(out.to, env) ||
              !BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          auto out = node;
          for (auto& param : out.params) {
            if (!BuildTypeInPlace(param.type_opt, env)) {
              return std::nullopt;
            }
          }
          if (!BuildTypeInPlace(out.ret_type_opt, env) ||
              !BuildExprInPlace(out.body, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.lhs, env) ||
              !BuildExprInPlace(out.rhs, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.base, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.base, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.base, env) ||
              !BuildExprInPlace(out.index, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.callee, env)) {
            return std::nullopt;
          }
          for (auto& arg : out.type_args) {
            if (!BuildTypeInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          for (auto& arg : out.args) {
            if (!BuildArgInPlace(arg, env)) {
              return std::nullopt;
            }
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.value, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          auto out = node;
          for (auto& arm : out.arms) {
            if (!BuildRaceArmInPlace(arm, env)) {
              return std::nullopt;
            }
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          auto out = node;
          for (auto& elem : out.exprs) {
            if (!BuildExprInPlace(elem, env)) {
              return std::nullopt;
            }
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.domain, env)) {
            return std::nullopt;
          }
          for (auto& opt : out.opts) {
            if (!BuildParallelOptionInPlace(opt, env)) {
              return std::nullopt;
            }
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          auto out = node;
          for (auto& opt : out.opts) {
            if (!BuildSpawnOptionInPlace(opt, env)) {
              return std::nullopt;
            }
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          auto out = node;
          if (!BuildExprInPlace(out.handle, env)) {
            return std::nullopt;
          }
          return MakeExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          auto out = node;
          if (!BuildPatternInPlace(out.pattern, env) ||
              !BuildExprInPlace(out.range, env)) {
            return std::nullopt;
          }
          if (out.key_clause.has_value() &&
              !BuildKeyPathExprInPlace(out.key_clause->key_path, env)) {
            return std::nullopt;
          }
          for (auto& opt : out.opts) {
            if (!BuildDispatchOptionInPlace(opt, env)) {
              return std::nullopt;
            }
          }
          auto body = BuildBlock(*out.body, env);
          if (!body.has_value()) {
            return std::nullopt;
          }
          out.body = std::make_shared<Block>(std::move(*body));
          return MakeExpr(expr->span, std::move(out));
        } else {
          return expr;
        }
      },
      expr->node);
}

std::optional<CtAst> ParseQuotedAstAsKind(const QuoteExpr& quote,
                                          ast::QuoteKind kind,
                                          CtEnv& env,
                                          core::DiagnosticStream& diags,
                                          bool& parse_failed) {
  parse_failed = false;
  ScopedQuoteCtx quote_ctx(env, kind);

  if (auto whole_splice = ParseWholeBodySpliceExpr(quote)) {
    const core::Span splice_span =
        quote.tokens.empty() ? core::Span{} : quote.tokens.front().span;
    auto value = EvalSpliceValue(*whole_splice, env, splice_span);
    if (!value.has_value()) {
      return std::nullopt;
    }
    switch (kind) {
      case ast::QuoteKind::Expr: {
        ExprPtr rendered;
        if (!RenderExprSplice(*value, splice_span, rendered)) {
          EmitComptimeDiag(env, "E-CTE-0230", splice_span);
          return std::nullopt;
        }
        return AstOf(CtAstKind::Expr, rendered);
      }
      case ast::QuoteKind::Stmt: {
        Stmt rendered;
        if (!RenderStmtSplice(*value, rendered)) {
          EmitComptimeDiag(env, "E-CTE-0230", splice_span);
          return std::nullopt;
        }
        return AstOf(CtAstKind::Stmt, rendered);
      }
      case ast::QuoteKind::Unspecified:
        return std::nullopt;
      case ast::QuoteKind::Item: {
        ASTItem rendered;
        if (!RenderItemSplice(*value, rendered)) {
          EmitComptimeDiag(env, "E-CTE-0230", splice_span);
          return std::nullopt;
        }
        return AstOf(CtAstKind::Item, rendered);
      }
      case ast::QuoteKind::Type: {
        TypePtr rendered;
        if (!RenderTypeSplice(*value, rendered)) {
          EmitComptimeDiag(env, "E-CTE-0230", splice_span);
          return std::nullopt;
        }
        return AstOf(CtAstKind::Type, rendered);
      }
      case ast::QuoteKind::Pattern: {
        PatternPtr rendered;
        if (!RenderPatternSplice(*value, rendered)) {
          EmitComptimeDiag(env, "E-CTE-0230", splice_span);
          return std::nullopt;
        }
        return AstOf(CtAstKind::Pattern, rendered);
      }
    }
  }

  ast::Parser parser = MakeTokenParser(quote.tokens, true);
  const std::size_t start_index = parser.index;
  switch (kind) {
    case ast::QuoteKind::Expr: {
      auto parsed = ast::ParseExpr(parser);
      AppendDiags(diags, parsed.parser.diags);
      if (parsed.parser.index == start_index || !parsed.elem ||
          !ast::AtEof(parsed.parser)) {
        parse_failed = true;
        return std::nullopt;
      }
      auto payload = BuildExpr(parsed.elem, env);
      if (!payload.has_value()) {
        return std::nullopt;
      }
      return AstOf(CtAstKind::Expr, std::move(*payload));
    }
    case ast::QuoteKind::Stmt: {
      auto parsed = ast::ParseStmt(parser);
      AppendDiags(diags, parsed.parser.diags);
      if (parsed.parser.index == start_index || !IsQuotedStatementForm(parsed.elem) ||
          !ast::AtEof(parsed.parser)) {
        parse_failed = true;
        return std::nullopt;
      }
      auto payload = BuildStmt(parsed.elem, env);
      if (!payload.has_value()) {
        return std::nullopt;
      }
      return AstOf(CtAstKind::Stmt, std::move(*payload));
    }
    case ast::QuoteKind::Unspecified:
      parse_failed = true;
      return std::nullopt;
    case ast::QuoteKind::Item: {
      auto parsed = ast::ParseItem(parser);
      AppendDiags(diags, parsed.parser.diags);
      if (parsed.parser.index == start_index ||
          std::holds_alternative<ast::ErrorItem>(parsed.item) ||
          !ast::AtEof(parsed.parser)) {
        parse_failed = true;
        return std::nullopt;
      }
      auto payload = BuildItem(parsed.item, env);
      if (!payload.has_value()) {
        return std::nullopt;
      }
      return AstOf(CtAstKind::Item, std::move(*payload));
    }
    case ast::QuoteKind::Type: {
      auto parsed = ast::ParseType(parser);
      AppendDiags(diags, parsed.parser.diags);
      if (parsed.parser.index == start_index || !parsed.elem ||
          !ast::AtEof(parsed.parser)) {
        parse_failed = true;
        return std::nullopt;
      }
      auto payload = BuildType(parsed.elem, env);
      if (!payload.has_value()) {
        return std::nullopt;
      }
      return AstOf(CtAstKind::Type, std::move(*payload));
    }
    case ast::QuoteKind::Pattern: {
      auto parsed = ast::ParsePattern(parser);
      AppendDiags(diags, parsed.parser.diags);
      if (parsed.parser.index == start_index || !parsed.elem ||
          !ast::AtEof(parsed.parser)) {
        parse_failed = true;
        return std::nullopt;
      }
      auto payload = BuildPattern(parsed.elem, env);
      if (!payload.has_value()) {
        return std::nullopt;
      }
      return AstOf(CtAstKind::Pattern, std::move(*payload));
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<CtAst> ParseQuotedAst(const QuoteExpr& quote,
                                    CtEnv& env,
                                    core::DiagnosticStream& diags) {
  auto emit_invalid_quote = [&]() -> std::optional<CtAst> {
    if (auto diag = core::MakeDiagnosticById(
            "E-CTE-0220",
            quote.tokens.empty() ? std::nullopt
                                 : std::optional<core::Span>(quote.tokens.front().span))) {
      core::Emit(diags, *diag);
    }
    return std::nullopt;
  };

  if (quote.kind != ast::QuoteKind::Unspecified) {
    bool parse_failed = false;
    if (auto parsed = ParseQuotedAstAsKind(quote, quote.kind, env, diags, parse_failed)) {
      return parsed;
    }
    return parse_failed ? emit_invalid_quote() : std::nullopt;
  }

  std::optional<ast::QuoteKind> resolved_kind;
  std::size_t match_count = 0;
  for (ast::QuoteKind kind :
       {ast::QuoteKind::Expr, ast::QuoteKind::Stmt, ast::QuoteKind::Item}) {
    if (!QuoteParsesAsKind(quote, kind)) {
      continue;
    }
    ++match_count;
    if (!resolved_kind.has_value()) {
      resolved_kind = kind;
    }
  }

  if (match_count != 1 || !resolved_kind.has_value()) {
    return emit_invalid_quote();
  }

  bool parse_failed = false;
  if (auto parsed =
          ParseQuotedAstAsKind(quote, *resolved_kind, env, diags, parse_failed)) {
    return parsed;
  }
  return parse_failed ? emit_invalid_quote() : std::nullopt;
}

}  // namespace cursive::frontend::comptime_internal
