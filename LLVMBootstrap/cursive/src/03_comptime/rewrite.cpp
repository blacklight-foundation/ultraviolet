#include "03_comptime/comptime_internal.h"

#include <string>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "02_source/attributes/attribute_registry.h"

namespace cursive::frontend::comptime_internal {

namespace {

ExprPtr RewriteExpr(const ExprPtr& expr, CtEnv& env);
bool BindCtPatternValue(CtEnv& env, const ast::PatternPtr& pattern, const CtValue& value);

bool ValidatePhase2AttributeList(CtEnv& env,
                                 const AttributeList& attrs,
                                 analysis::AttributeTarget target) {
  if (attrs.empty()) {
    return true;
  }

  const auto validation = analysis::ValidateAttributes(attrs, target);
  if (validation.ok) {
    return true;
  }

  if (core::DiagnosticStream* diags = CtDiags(env)) {
    const std::string code =
        validation.diag_id.has_value() ? std::string(*validation.diag_id)
                                       : "E-MOD-2450";
    if (auto diag = core::MakeDiagnosticById(code, validation.span)) {
      if (!validation.message.empty()) {
        diag->message = validation.message;
      }
      core::Emit(*diags, *diag);
    }
  }
  return false;
}

ExprPtr MakeUnitExpr(const core::Span& span) {
  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = ast::TupleExpr{};
  return expr;
}

ExprPtr MakeBlockExpr(const Block& block, const core::Span& span) {
  ast::BlockExpr expr_node;
  expr_node.block = std::make_shared<Block>(block);
  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = std::move(expr_node);
  return expr;
}

Block MakeFallbackEmptyBlock(const core::Span& span) {
  Block block;
  block.span = span;
  block.tail_opt = MakeUnitExpr(span);
  return block;
}

void AppendUnitBlockStmts(const Block& block, std::vector<ast::Stmt>& out) {
  out.insert(out.end(), block.stmts.begin(), block.stmts.end());
  if (block.tail_opt) {
    ast::ExprStmt stmt;
    stmt.value = block.tail_opt;
    stmt.span = block.tail_opt->span;
    out.push_back(std::move(stmt));
  }
}

bool CtValueEqualsLiteral(const CtValue& value, const ast::Token& literal) {
  if (literal.kind == ast::TokenKind::BoolLiteral) {
    bool bool_value = false;
    return TryGetCtBool(value, bool_value) &&
           ((literal.lexeme == "true") == bool_value);
  }
  if (literal.kind == ast::TokenKind::IntLiteral) {
    const auto* i = TryGetCtInt(value);
    if (!i) {
      return false;
    }
    try {
      return i->value == std::stoull(std::string(literal.lexeme));
    } catch (...) {
      return false;
    }
  }
  if (literal.kind == ast::TokenKind::StringLiteral) {
    const auto* s = std::get_if<CtString>(&value);
    if (!s) {
      return false;
    }
    std::string lexeme = literal.lexeme;
    if (lexeme.size() >= 2 && lexeme.front() == '"' && lexeme.back() == '"') {
      lexeme = lexeme.substr(1, lexeme.size() - 2);
    }
    return s->value == lexeme;
  }
  return false;
}

bool BindCtRangePattern(CtEnv& env,
                        const ast::RangePattern& range,
                        const CtValue& value) {
  const auto* int_value = TryGetCtInt(value);
  if (!int_value || !range.lo || !range.hi) {
    return false;
  }
  const auto* lo_pat = std::get_if<ast::LiteralPattern>(&range.lo->node);
  const auto* hi_pat = std::get_if<ast::LiteralPattern>(&range.hi->node);
  if (!lo_pat || !hi_pat) {
    return false;
  }
  try {
    const auto lo = std::stoull(std::string(lo_pat->literal.lexeme));
    const auto hi = std::stoull(std::string(hi_pat->literal.lexeme));
    const bool lower_ok = int_value->value >= lo;
    const bool upper_ok =
        range.kind == ast::RangeKind::Inclusive
            ? int_value->value <= hi
            : int_value->value < hi;
    return lower_ok && upper_ok;
  } catch (...) {
    return false;
  }
}

bool BindCtTuplePattern(CtEnv& env,
                        const ast::TuplePattern& tuple,
                        const CtValue& value) {
  const auto* tuple_value = std::get_if<std::shared_ptr<CtTuple>>(&value);
  if (!tuple_value || !*tuple_value ||
      (*tuple_value)->elements.size() != tuple.elements.size()) {
    return false;
  }
  CtEnv next = env;
  for (std::size_t i = 0; i < tuple.elements.size(); ++i) {
    if (!BindCtPatternValue(next, tuple.elements[i], (*tuple_value)->elements[i])) {
      return false;
    }
  }
  env = std::move(next);
  return true;
}

bool BindCtPatternValue(CtEnv& env,
                        const ast::PatternPtr& pattern,
                        const CtValue& value) {
  if (!pattern) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          env.values[node.name] = value;
          return true;
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          env.values[node.name] = value;
          return true;
        } else if constexpr (std::is_same_v<T, ast::LiteralPattern>) {
          return CtValueEqualsLiteral(value, node.literal);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          return BindCtTuplePattern(env, node, value);
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          return BindCtRangePattern(env, node, value);
        } else {
          return false;
        }
      },
      pattern->node);
}

ast::Stmt RewriteStmt(const ast::Stmt& stmt, CtEnv& env) {
  return std::visit(
      [&](const auto& node) -> ast::Stmt {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          ast::LetStmt out = node;
          out.binding.init = RewriteExpr(node.binding.init, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          ast::VarStmt out = node;
          out.binding.init = RewriteExpr(node.binding.init, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          ast::ExprStmt out = node;
          out.value = RewriteExpr(node.value, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          ast::ReturnStmt out = node;
          out.value_opt = RewriteExpr(node.value_opt, env);
          return out;
        } else {
          return stmt;
        }
      },
      stmt);
}

Block RewriteBlock(const Block& block, CtEnv& env) {
  SPEC_RULE_AT("CtExpandBlock", block.span);
  Block out = block;
  out.stmts.clear();
  out.stmts.reserve(block.stmts.size());
  if (block.stmts.empty()) {
    SPEC_RULE("CtExpandStmtSeq-Empty");
  }
  for (const auto& stmt : block.stmts) {
    SPEC_RULE("CtExpandStmtSeq-Cons");
    if (const auto* comptime = std::get_if<ast::ComptimeStmt>(&stmt)) {
      if (!ValidatePhase2AttributeList(env, comptime->attrs,
                                       analysis::AttributeTarget::Statement)) {
        continue;
      }
      SPEC_RULE_AT("CtExpandStmt-CtStmt", comptime->span);
      std::vector<ASTItem> emitted;
      CtEnv stmt_env = WithCtCaps(env, comptime->attrs);
      stmt_env.pending_emits = &emitted;
      if (EvalBlock(*comptime->body, stmt_env).ok && env.pending_emits) {
        env.pending_emits->insert(env.pending_emits->end(), emitted.begin(),
                                  emitted.end());
      }
      continue;
    }
    out.stmts.push_back(RewriteStmt(stmt, env));
  }
  out.tail_opt = RewriteExpr(out.tail_opt, env);
  return out;
}

ExprPtr RewriteExpr(const ExprPtr& expr, CtEnv& env) {
  if (!expr) {
    return expr;
  }

  return std::visit(
      [&](const auto& node) -> ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          if (node.expr && std::holds_alternative<ast::ComptimeExpr>(node.expr->node)) {
            SPEC_RULE_AT("CtExpandExpr-CtExpr", expr->span);
            if (!ValidatePhase2AttributeList(
                    env, node.attrs, analysis::AttributeTarget::Expression)) {
              return expr;
            }
            CtEnv ct_env = WithCtCaps(env, node.attrs);
            std::vector<ASTItem> emitted;
            ct_env.pending_emits = &emitted;
            auto value = EvalExpr(expr, ct_env);
            if (value.ok) {
              if (env.pending_emits) {
                env.pending_emits->insert(env.pending_emits->end(), emitted.begin(),
                                          emitted.end());
              }
              if (std::holds_alternative<CtAst>(value.value)) {
                auto hygienized = PrepareAstForInsertion(
                    std::get<CtAst>(value.value), CtSiteOf(env), env);
                if (hygienized.has_value() &&
                    hygienized->kind == CtAstKind::Expr) {
                  if (const auto* quoted_expr =
                          std::get_if<ExprPtr>(&hygienized->payload)) {
                    return *quoted_expr;
                  }
                }
                EmitComptimeDiag(env, "E-CTE-0210", expr->span);
              } else if (ExprPtr lit = LiteralizeValue(value.value, expr->span)) {
                return lit;
              }
            }
          }
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::AttributedExpr>(out->node);
          rewritten.expr = RewriteExpr(node.expr, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          SPEC_RULE_AT("CtExpandExpr-CtExpr", expr->span);
          if (!ValidatePhase2AttributeList(
                  env, ast::AttrListOf(node.attrs_opt),
                  analysis::AttributeTarget::Expression)) {
            return expr;
          }
          CtEnv ct_env = WithCtCaps(env, ast::AttrListOf(node.attrs_opt));
          std::vector<ASTItem> emitted;
          ct_env.pending_emits = &emitted;
          auto value = EvalExpr(expr, ct_env);
          if (value.ok) {
            if (env.pending_emits) {
              env.pending_emits->insert(env.pending_emits->end(), emitted.begin(),
                                        emitted.end());
            }
            if (std::holds_alternative<CtAst>(value.value)) {
              auto hygienized = PrepareAstForInsertion(
                  std::get<CtAst>(value.value), CtSiteOf(env), env);
              if (hygienized.has_value() &&
                  hygienized->kind == CtAstKind::Expr) {
                if (const auto* quoted_expr =
                        std::get_if<ExprPtr>(&hygienized->payload)) {
                  return *quoted_expr;
                }
              }
              EmitComptimeDiag(env, "E-CTE-0210", expr->span);
            } else if (ExprPtr lit = LiteralizeValue(value.value, expr->span)) {
              return lit;
            }
          }
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::ComptimeExpr>(out->node);
          rewritten.body = RewriteExpr(node.body, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          CtEnv ct_env = env;
          auto cond = EvalExpr(node.cond, ct_env);
          if (!cond.ok) {
            EmitComptimeDiag(env, "E-CTE-0080", expr->span);
            return expr;
          }
          bool cond_bool = false;
          if (!TryGetCtBool(cond.value, cond_bool)) {
            EmitComptimeDiag(env, "E-CTE-0081", expr->span);
            return expr;
          }
          if (cond_bool) {
            SPEC_RULE_AT("CtExpandExpr-CtIf-True", expr->span);
          } else {
            SPEC_RULE_AT("CtExpandExpr-CtIf-False", expr->span);
          }
          const Block* selected =
              cond_bool ? node.then_block.get() : node.else_block_opt.get();
          Block rewritten =
              selected ? RewriteBlock(*selected, ct_env) : MakeFallbackEmptyBlock(expr->span);
          return MakeBlockExpr(rewritten, expr->span);
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          SPEC_RULE_AT("CtExpandExpr-CtLoopIter", expr->span);
          CtEnv ct_env = env;
          auto iter = EvalExpr(node.iter, ct_env);
          if (!iter.ok) {
            EmitComptimeDiag(env, "E-CTE-0082", expr->span);
            return expr;
          }

          if (!CtIterable(iter.value)) {
            EmitComptimeDiag(env, "E-CTE-0083", expr->span);
            return expr;
          }
          const std::vector<CtValue>* elems = CtElems(iter.value);
          if (!elems) {
            EmitComptimeDiag(env, "E-CTE-0083", expr->span);
            return expr;
          }

          Block unrolled;
          unrolled.span = expr->span;
          unrolled.tail_opt = MakeUnitExpr(expr->span);
          CtEnv loop_env = ct_env;
          if (elems->empty()) {
            SPEC_RULE_AT("CtLoopIterUnroll-Empty", expr->span);
          }
          for (const auto& elem : *elems) {
            SPEC_RULE_AT("CtLoopIterUnroll-Cons", expr->span);
            CtEnv iter_env = loop_env;
            if (!BindCtPatternValue(iter_env, node.pattern, elem)) {
              EmitComptimeDiag(env, "E-CTE-0083", expr->span);
              return expr;
            }
            Block rewritten = RewriteBlock(*node.body, iter_env);
            AppendUnitBlockStmts(rewritten, unrolled.stmts);
            loop_env = std::move(iter_env);
          }
          return MakeBlockExpr(unrolled, expr->span);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::BlockExpr>(out->node);
          rewritten.block = std::make_shared<Block>(RewriteBlock(*node.block, env));
          return out;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::BinaryExpr>(out->node);
          rewritten.lhs = RewriteExpr(node.lhs, env);
          rewritten.rhs = RewriteExpr(node.rhs, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::IfExpr>(out->node);
          rewritten.cond = RewriteExpr(node.cond, env);
          rewritten.then_expr = RewriteExpr(node.then_expr, env);
          rewritten.else_expr = RewriteExpr(node.else_expr, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::IfIsExpr>(out->node);
          rewritten.scrutinee = RewriteExpr(node.scrutinee, env);
          rewritten.then_expr = RewriteExpr(node.then_expr, env);
          rewritten.else_expr = RewriteExpr(node.else_expr, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::IfCaseExpr>(out->node);
          rewritten.scrutinee = RewriteExpr(node.scrutinee, env);
          for (auto& case_clause : rewritten.cases) {
            case_clause.body = RewriteExpr(case_clause.body, env);
          }
          rewritten.else_expr = RewriteExpr(node.else_expr, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::MethodCallExpr>(out->node);
          rewritten.receiver = RewriteExpr(node.receiver, env);
          for (auto& arg : rewritten.args) {
            arg.value = RewriteExpr(arg.value, env);
          }
          return out;
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = std::make_shared<Expr>(*expr);
          auto& rewritten = std::get<ast::CallExpr>(out->node);
          rewritten.callee = RewriteExpr(node.callee, env);
          for (auto& arg : rewritten.args) {
            arg.value = RewriteExpr(arg.value, env);
          }
          return out;
        } else {
          return expr;
        }
      },
      expr->node);
}

ASTItem RewriteItem(const ASTItem& item, CtEnv& env) {
  return std::visit(
      [&](const auto& node) -> ASTItem {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          ast::StaticDecl out = node;
          if (out.attrs_opt.has_value()) {
            out.attrs_opt = StripAttribute(*out.attrs_opt, "derive");
            if (out.attrs_opt->empty()) {
              out.attrs_opt = std::nullopt;
            }
          }
          out.binding.init = RewriteExpr(node.binding.init, env);
          return out;
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          ast::ProcedureDecl out = node;
          out.attrs = StripAttribute(out.attrs, "derive");
          out.body = std::make_shared<Block>(RewriteBlock(*node.body, env));
          return out;
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          auto out = node;
          out.attrs = StripAttribute(out.attrs, "derive");
          return out;
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          auto out = node;
          out.attrs = StripAttribute(out.attrs, "derive");
          return out;
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          auto out = node;
          out.attrs = StripAttribute(out.attrs, "derive");
          return out;
        } else {
          return item;
        }
      },
      item);
}

}  // namespace

std::optional<std::vector<ASTItem>> ExpandModuleItems(
    const std::vector<ASTItem>& items, CtEnv& env) {
  std::vector<ASTItem> queue = items;
  std::vector<ASTItem> visible_current_items = items;
  env.current_module_items = &visible_current_items;
  std::vector<ASTItem> out;
  if (queue.empty()) {
    SPEC_RULE("CtExpandItemSeq-Empty");
  }
  for (std::size_t i = 0; i < queue.size(); ++i) {
    SPEC_RULE("CtExpandItemSeq-Cons");
    env = WithCtSite(std::move(env), i, {});
    const ASTItem item = queue[i];

    if (const auto* proc = std::get_if<ast::ComptimeProcedureDecl>(&item)) {
      if (!ValidatePhase2AttributeList(
              env, proc->attrs, analysis::AttributeTarget::Procedure)) {
        continue;
      }
      SPEC_RULE_AT("CtExpandItem-CtProc", proc->span);
      env = BindCtProc(std::move(env), *proc);
      continue;
    }

    if (const auto* derive = std::get_if<ast::DeriveTargetDecl>(&item)) {
      SPEC_RULE_AT("CtExpandItem-DeriveTargetDecl", derive->span);
      continue;
    }

    std::vector<ASTItem> explicit_emits;
    CtEnv item_env = env;
    item_env.pending_emits = &explicit_emits;
    ASTItem rewritten = RewriteItem(item, item_env);
    out.push_back(rewritten);

    std::vector<ASTItem> emitted;
    if (IsDeriveAnnotatedItem(item)) {
      SPEC_RULE_AT("CtExpandItem-DeriveAnnotatedDecl", SpanOfItem(item));
      auto derive_emits = ExpandDerives(item, env);
      if (!derive_emits.has_value()) {
        return std::nullopt;
      }
      emitted.insert(emitted.end(), derive_emits->begin(), derive_emits->end());
    }
    emitted.insert(emitted.end(), explicit_emits.begin(), explicit_emits.end());

    if (!emitted.empty()) {
      queue.insert(queue.begin() + static_cast<std::ptrdiff_t>(i + 1),
                   emitted.begin(), emitted.end());
      visible_current_items.insert(visible_current_items.end(), emitted.begin(),
                                   emitted.end());
    }
  }
  return out;
}

}  // namespace cursive::frontend::comptime_internal
