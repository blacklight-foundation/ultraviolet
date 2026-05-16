#include "03_comptime/comptime_internal.h"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"

namespace cursive::frontend::comptime_internal {

namespace {

std::optional<unsigned long long> ParseU64Literal(std::string_view lexeme) {
  std::size_t i = 0;
  while (i < lexeme.size() && lexeme[i] >= '0' && lexeme[i] <= '9') {
    ++i;
  }
  if (i == 0) {
    return std::nullopt;
  }
  try {
    return std::stoull(std::string(lexeme.substr(0, i)));
  } catch (...) {
    return std::nullopt;
  }
}

std::string LiteralSuffix(std::string_view lexeme) {
  std::size_t i = 0;
  while (i < lexeme.size() &&
         ((lexeme[i] >= '0' && lexeme[i] <= '9') || lexeme[i] == '_')) {
    ++i;
  }
  if (i >= lexeme.size()) {
    return "u64";
  }
  return std::string(lexeme.substr(i));
}

struct ScopedCtSpan {
  CtEnv& env;
  core::Span saved;

  ScopedCtSpan(CtEnv& env, const core::Span& span)
      : env(env), saved(env.current_span) {
    env.current_span = span;
    env.site.span = span;
  }

  ~ScopedCtSpan() {
    env.current_span = saved;
    env.site.span = saved;
  }
};

std::string ModulePathText(const ast::ModulePath& path) {
  std::string text;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i != 0) {
      text.append("::");
    }
    text.append(path[i]);
  }
  return text;
}

std::optional<ast::QuoteKind> ExpectedQuoteKindOfType(const ast::TypePtr& type) {
  if (!type) {
    return std::nullopt;
  }

  const ast::TypePtr stripped = std::visit(
      [&](const auto& node) -> ast::TypePtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType> ||
                      std::is_same_v<T, ast::TypeRefine>) {
          return node.base;
        } else {
          return type;
        }
      },
      type->node);
  if (stripped && stripped.get() != type.get()) {
    return ExpectedQuoteKindOfType(stripped);
  }

  const ast::TypePathType* path =
      stripped ? std::get_if<ast::TypePathType>(&stripped->node) : nullptr;
  if (!path || !path->generic_args.empty()) {
    return std::nullopt;
  }
  if (path->path == ast::TypePath{"Ast", "Expr"}) {
    return ast::QuoteKind::Expr;
  }
  if (path->path == ast::TypePath{"Ast", "Stmt"}) {
    return ast::QuoteKind::Stmt;
  }
  if (path->path == ast::TypePath{"Ast", "Item"}) {
    return ast::QuoteKind::Item;
  }
  if (path->path == ast::TypePath{"Ast", "Type"}) {
    return ast::QuoteKind::Type;
  }
  if (path->path == ast::TypePath{"Ast", "Pattern"}) {
    return ast::QuoteKind::Pattern;
  }
  return std::nullopt;
}

EvalResult EvalQuoteExpr(const ast::QuoteExpr& quote,
                         CtEnv& env,
                         const core::Span& span,
                         std::optional<ast::QuoteKind> expected_kind) {
  SPEC_RULE_AT("CtEval-Quote", span);
  EvalResult out;
  auto parsed_ast = ParseQuotedAst(quote, env, *env.diags, expected_kind);
  if (!parsed_ast.has_value()) {
    out.ok = false;
    return out;
  }
  if (!parsed_ast->span.has_value()) {
    parsed_ast->span = span;
  }
  if (!parsed_ast->hygiene.has_value()) {
    parsed_ast->hygiene = CtHygiene{CtSiteOf(env), CtSiteOf(env), 0};
  }
  out.ok = true;
  out.value = std::move(*parsed_ast);
  return out;
}

EvalResult EvalExprWithExpectedQuoteKind(
    const ExprPtr& expr,
    CtEnv& env,
    std::optional<ast::QuoteKind> expected_kind) {
  if (!expected_kind.has_value() || !expr) {
    return EvalExpr(expr, env);
  }
  if (const auto* quote = std::get_if<ast::QuoteExpr>(&expr->node)) {
    return EvalQuoteExpr(*quote, env, expr->span, expected_kind);
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return EvalExprWithExpectedQuoteKind(attributed->expr, env, expected_kind);
  }
  return EvalExpr(expr, env);
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

bool BindEvalPatternValue(CtEnv& env,
                          const ast::PatternPtr& pattern,
                          const CtValue& value);

bool CtEnumEquals(const CtEnum& lhs, const CtEnum& rhs) {
  return lhs.path == rhs.path &&
         lhs.variant == rhs.variant &&
         std::holds_alternative<std::monostate>(lhs.payload) &&
         std::holds_alternative<std::monostate>(rhs.payload);
}

bool BindEvalTuplePattern(CtEnv& env,
                          const ast::TuplePattern& tuple,
                          const CtValue& value) {
  const auto* tuple_value = std::get_if<std::shared_ptr<CtTuple>>(&value);
  if (!tuple_value || !*tuple_value ||
      (*tuple_value)->elements.size() != tuple.elements.size()) {
    return false;
  }
  CtEnv next = env;
  for (std::size_t i = 0; i < tuple.elements.size(); ++i) {
    if (!BindEvalPatternValue(next, tuple.elements[i],
                              (*tuple_value)->elements[i])) {
      return false;
    }
  }
  env = std::move(next);
  return true;
}

bool BindEvalRangePattern(CtEnv& env,
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

bool BindEvalPatternValue(CtEnv& env,
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
          return BindEvalTuplePattern(env, node, value);
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          return BindEvalRangePattern(env, node, value);
        } else {
          return false;
        }
      },
      pattern->node);
}

void AppendCtDiagnostic(CtEnv& env, const core::Diagnostic& diag) {
  if (!CtDiags(env)) {
    return;
  }
  core::Emit(*CtDiags(env), diag);
}

void AppendCtCodedDiagnostic(CtEnv& env,
                             std::string_view diag_id,
                             core::Severity fallback_severity,
                             std::string_view message) {
  if (auto diag = core::MakeDiagnosticById(diag_id, env.current_span)) {
    diag->message = std::string(message);
    AppendCtDiagnostic(env, *diag);
    return;
  }

  core::Diagnostic fallback;
  fallback.severity = fallback_severity;
  fallback.message =
      "Internal error: unresolved diagnostic id '" + std::string(diag_id) +
      "': " + std::string(message);
  fallback.span = env.current_span;
  AppendCtDiagnostic(env, fallback);
}

void AppendCtUserNoteDiagnostic(CtEnv& env, std::string_view message) {
  core::Diagnostic diag;
  diag.severity = core::Severity::Note;
  diag.message = std::string(message);
  diag.span = env.current_span;
  AppendCtDiagnostic(env, diag);
}

EvalResult EvalCall(const ast::CallExpr& call, CtEnv& env) {
  if (!call.callee || !std::holds_alternative<ast::IdentifierExpr>(call.callee->node)) {
    return {};
  }

  const auto& name = std::get<ast::IdentifierExpr>(call.callee->node).name;
  auto it = env.procs.find(name);
  if (it == env.procs.end()) {
    return {};
  }

  if (call.args.size() != it->second.params.size()) {
    return {};
  }

  CtEnv proc_env = env;
  proc_env.pending_emits = env.pending_emits;
  proc_env.contract_result_value.reset();
  proc_env.return_quote_kind = ExpectedQuoteKindOfType(it->second.return_type_opt);
  std::vector<bool> bound(it->second.params.size(), false);
  for (std::size_t i = 0; i < call.args.size(); ++i) {
    const auto value = EvalExpr(call.args[i].value, env);
    if (!value.ok) {
      return value;
    }
    proc_env.values[it->second.params[i].name] = value.value;
    bound[i] = true;
  }

  proc_env.contract_entry_values =
      std::make_shared<const std::unordered_map<std::string, CtValue>>(
          proc_env.values);

  if (it->second.contract && it->second.contract->precondition) {
    const auto pred = EvalExpr(it->second.contract->precondition, proc_env);
    if (!pred.ok) {
      return pred;
    }
    bool pred_bool = false;
    if (!TryGetCtBool(pred.value, pred_bool)) {
      return {};
    }
    if (!pred_bool) {
      EmitComptimeDiag(env, "E-CTE-0033",
                      call.callee ? call.callee->span : core::Span{});
      return {};
    }
  }

  const auto body = EvalBlock(*it->second.body, proc_env);
  if (!body.ok) {
    return body;
  }

  if (it->second.contract && it->second.contract->postcondition) {
    CtEnv post_env = proc_env;
    post_env.contract_result_value = body.value;
    const auto pred = EvalExpr(it->second.contract->postcondition, post_env);
    if (!pred.ok) {
      return pred;
    }
    bool pred_bool = false;
    if (!TryGetCtBool(pred.value, pred_bool)) {
      return {};
    }
    if (!pred_bool) {
      EmitComptimeDiag(env, "E-CTE-0033",
                      call.callee ? call.callee->span : core::Span{});
      return {};
    }
  }

  return body;
}

EvalResult EvalMethodCall(const ast::MethodCallExpr& call, CtEnv& env) {
  if (!call.receiver) {
    return {};
  }

  if (const auto files_result = EvalProjectFilesMethod(call, env)) {
    return *files_result;
  }
  if (const auto reflect_result = EvalIntrospectMethod(call, env)) {
    return *reflect_result;
  }

  if (std::holds_alternative<ast::IdentifierExpr>(call.receiver->node)) {
    const auto& recv = std::get<ast::IdentifierExpr>(call.receiver->node).name;
    if (recv == "diagnostics") {
      if (call.name == "current_module" && call.args.empty()) {
        SPEC_RULE("CtBuiltin-Diagnostics-CurrentModule");
        EvalResult result;
        result.ok = true;
        result.value = CtString{ModulePathText(CtSiteOf(env).module_path)};
        return result;
      }
      if (call.name == "current_span" && call.args.empty()) {
        SPEC_RULE("CtBuiltin-Diagnostics-CurrentSpan");
        EvalResult result;
        result.ok = true;
        result.value = MakeSpanValue(CtSiteOf(env).span);
        return result;
      }
      if (call.args.size() == 1) {
        const auto value = EvalExpr(call.args[0].value, env);
        if (!value.ok) {
          return value;
        }
        const auto* msg = std::get_if<CtString>(&value.value);
        if (!msg) {
          return {};
        }
        if (call.name == "error") {
          SPEC_RULE("CtBuiltin-Diagnostics-Error");
          AppendCtCodedDiagnostic(
              env, "E-CTE-0070", core::Severity::Error, msg->value);
          EvalResult result;
          result.ok = true;
          result.value = MakeCtUnit();
          return result;
        }
        if (call.name == "warning") {
          SPEC_RULE("CtBuiltin-Diagnostics-Warning");
          AppendCtCodedDiagnostic(
              env, "W-CTE-0071", core::Severity::Warning, msg->value);
          EvalResult result;
          result.ok = true;
          result.value = MakeCtUnit();
          return result;
        }
        if (call.name == "note") {
          SPEC_RULE("CtBuiltin-Diagnostics-Note");
          AppendCtUserNoteDiagnostic(env, msg->value);
          EvalResult result;
          result.ok = true;
          result.value = MakeCtUnit();
          return result;
        }
      }
    }
  }

  if (call.name == "emit" && call.args.size() == 1) {
    const auto recv_value = EvalExpr(call.receiver, env);
    if (!recv_value.ok) {
      return recv_value;
    }

    const auto value = EvalExpr(call.args[0].value, env);
    if (!value.ok) {
      return value;
    }

    if (const auto* ast_value = std::get_if<CtAst>(&value.value)) {
      if (ast_value->kind != CtAstKind::Item) {
        EmitComptimeDiag(env, "E-CTE-0251",
                         call.args[0].value
                             ? call.args[0].value->span
                             : (call.receiver ? call.receiver->span
                                              : core::Span{}));
        return {};
      }
      auto hygienized = PrepareAstForInsertion(*ast_value, CtSiteOf(env), env);
      if (!hygienized.has_value()) {
        return {};
      }
      if (const auto* item = std::get_if<ASTItem>(&hygienized->payload)) {
        if (auto* pending = CtPendingEmits(env)) {
          pending->push_back(*item);
        }
        SPEC_RULE("CtBuiltin-Emit");
        EvalResult result;
        result.ok = true;
        result.value = MakeCtUnit();
        return result;
      }
      return {};
    }
  }

  return {};
}

}  // namespace

EvalResult EvalBlock(const Block& block, CtEnv& env) {
  CtEnv local = env;
  for (const auto& stmt : block.stmts) {
    local.current_span = std::visit(
        [](const auto& node) -> core::Span { return node.span; }, stmt);
    if (const auto* let_stmt = std::get_if<ast::LetStmt>(&stmt)) {
      if (!let_stmt->binding.init) {
        continue;
      }
      auto value = EvalExprWithExpectedQuoteKind(
          let_stmt->binding.init,
          local,
          ExpectedQuoteKindOfType(ast::BindingAnnotationTypeOpt(let_stmt->binding)));
      if (!value.ok) {
        return value;
      }
      if (!BindEvalPatternValue(local, let_stmt->binding.pat, value.value)) {
        return {};
      }
      continue;
    }

    if (const auto* var_stmt = std::get_if<ast::VarStmt>(&stmt)) {
      if (!var_stmt->binding.init) {
        continue;
      }
      auto value = EvalExprWithExpectedQuoteKind(
          var_stmt->binding.init,
          local,
          ExpectedQuoteKindOfType(ast::BindingAnnotationTypeOpt(var_stmt->binding)));
      if (!value.ok) {
        return value;
      }
      if (!BindEvalPatternValue(local, var_stmt->binding.pat, value.value)) {
        return {};
      }
      continue;
    }

    if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
      auto value = EvalExpr(expr_stmt->value, local);
      if (!value.ok) {
        return value;
      }
      if (value.returned) {
        return value;
      }
      continue;
    }

    if (const auto* ret_stmt = std::get_if<ast::ReturnStmt>(&stmt)) {
      auto value =
          EvalExprWithExpectedQuoteKind(ret_stmt->value_opt, local,
                                        local.return_quote_kind);
      value.returned = true;
      return value;
    }
  }

  if (block.tail_opt) {
    return EvalExpr(block.tail_opt, local);
  }

  EvalResult result;
  result.ok = true;
  result.value = MakeCtUnit();
  return result;
}

EvalResult EvalExpr(const ExprPtr& expr, CtEnv& env) {
  EvalResult result;
  if (!expr) {
    return result;
  }

  ScopedCtSpan span_scope(env, expr->span);

  return std::visit(
      [&](const auto& node) -> EvalResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          EvalResult out;
          out.ok = true;
          if (node.literal.kind == ast::TokenKind::BoolLiteral) {
            out.value = MakeCtBool(node.literal.lexeme == "true");
            return out;
          }
          if (node.literal.kind == ast::TokenKind::StringLiteral) {
            std::string v = node.literal.lexeme;
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
              v = v.substr(1, v.size() - 2);
            }
            out.value = CtString{v};
            return out;
          }
          if (node.literal.kind == ast::TokenKind::CharLiteral) {
            out.value = MakeCtChar(node.literal.lexeme);
            return out;
          }
          if (node.literal.kind == ast::TokenKind::IntLiteral) {
            const auto parsed = ParseU64Literal(node.literal.lexeme);
            if (!parsed.has_value()) {
              return {};
            }
            out.value = MakeCtInt(*parsed, LiteralSuffix(node.literal.lexeme),
                                  node.literal.lexeme);
            return out;
          }
          if (node.literal.kind == ast::TokenKind::FloatLiteral) {
            out.value = MakeCtFloat(node.literal.lexeme);
            return out;
          }
          return {};
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          auto it = env.values.find(node.name);
          if (it == env.values.end()) {
            return {};
          }
          EvalResult out;
          out.ok = true;
          out.value = it->second;
          return out;
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          auto enum_value = std::make_shared<CtEnum>();
          enum_value->path = node.path;
          enum_value->variant = node.name;
          EvalResult out;
          out.ok = true;
          out.value = enum_value;
          return out;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto enum_value = std::make_shared<CtEnum>();
          enum_value->path = node.path;
          enum_value->variant = node.name;

          bool ok = true;
          std::visit(
              [&](const auto& args) {
                using A = std::decay_t<decltype(args)>;
                if constexpr (std::is_same_v<A, ast::ParenArgs>) {
                  CtTuplePayload tuple;
                  tuple.elements.reserve(args.args.size());
                  for (const auto& arg : args.args) {
                    auto value = EvalExpr(arg.value, env);
                    if (!value.ok) {
                      ok = false;
                      return;
                    }
                    tuple.elements.push_back(value.value);
                  }
                  enum_value->payload = std::move(tuple);
                } else if constexpr (std::is_same_v<A, ast::BraceArgs>) {
                  CtRecordPayload record;
                  record.fields.reserve(args.fields.size());
                  for (const auto& field : args.fields) {
                    auto value = EvalExpr(field.value, env);
                    if (!value.ok) {
                      ok = false;
                      return;
                    }
                    record.fields.push_back({field.name, value.value});
                  }
                  enum_value->payload = std::move(record);
                }
              },
              node.args);
          if (!ok) {
            return {};
          }

          EvalResult out;
          out.ok = true;
          out.value = enum_value;
          return out;
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          if (!env.contract_result_value.has_value()) {
            return {};
          }
          EvalResult out;
          out.ok = true;
          out.value = *env.contract_result_value;
          return out;
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          if (!env.contract_entry_values) {
            return {};
          }
          CtEnv entry_env = env;
          entry_env.values = *env.contract_entry_values;
          entry_env.contract_result_value.reset();
          return EvalExpr(node.expr, entry_env);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto lhs = EvalExpr(node.lhs, env);
          if (!lhs.ok) {
            return lhs;
          }
          auto rhs = EvalExpr(node.rhs, env);
          if (!rhs.ok) {
            return rhs;
          }
          bool lb = false;
          bool rb = false;
          if (TryGetCtBool(lhs.value, lb) && TryGetCtBool(rhs.value, rb)) {
            EvalResult out;
            out.ok = true;
            if (node.op == "&&") {
              out.value = MakeCtBool(lb && rb);
              return out;
            }
            if (node.op == "||") {
              out.value = MakeCtBool(lb || rb);
              return out;
            }
            if (node.op == "==") {
              out.value = MakeCtBool(lb == rb);
              return out;
            }
            if (node.op == "!=") {
              out.value = MakeCtBool(lb != rb);
              return out;
            }
            return {};
          }
          const auto* lhs_string = std::get_if<CtString>(&lhs.value);
          const auto* rhs_string = std::get_if<CtString>(&rhs.value);
          if (lhs_string && rhs_string) {
            EvalResult out;
            out.ok = true;
            if (node.op == "==") {
              out.value = MakeCtBool(lhs_string->value == rhs_string->value);
              return out;
            }
            if (node.op == "!=") {
              out.value = MakeCtBool(lhs_string->value != rhs_string->value);
              return out;
            }
            return {};
          }
          const auto* lhs_enum =
              std::get_if<std::shared_ptr<CtEnum>>(&lhs.value);
          const auto* rhs_enum =
              std::get_if<std::shared_ptr<CtEnum>>(&rhs.value);
          if (lhs_enum && rhs_enum && *lhs_enum && *rhs_enum) {
            EvalResult out;
            out.ok = true;
            if (node.op == "==") {
              out.value = MakeCtBool(CtEnumEquals(**lhs_enum, **rhs_enum));
              return out;
            }
            if (node.op == "!=") {
              out.value = MakeCtBool(!CtEnumEquals(**lhs_enum, **rhs_enum));
              return out;
            }
            return {};
          }
          const auto* li = TryGetCtInt(lhs.value);
          const auto* ri = TryGetCtInt(rhs.value);
          if (!li || !ri) {
            return {};
          }
          EvalResult out;
          out.ok = true;
          if (node.op == "+") {
            out.value = MakeCtInt(li->value + ri->value, li->suffix);
            return out;
          }
          if (node.op == "*") {
            out.value = MakeCtInt(li->value * ri->value, li->suffix);
            return out;
          }
          if (node.op == "==") {
            out.value = MakeCtBool(li->value == ri->value);
            return out;
          }
          if (node.op == "!=") {
            out.value = MakeCtBool(li->value != ri->value);
            return out;
          }
          if (node.op == ">") {
            out.value = MakeCtBool(li->value > ri->value);
            return out;
          }
          if (node.op == ">=") {
            out.value = MakeCtBool(li->value >= ri->value);
            return out;
          }
          if (node.op == "<") {
            out.value = MakeCtBool(li->value < ri->value);
            return out;
          }
          if (node.op == "<=") {
            out.value = MakeCtBool(li->value <= ri->value);
            return out;
          }
          return {};
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto cond = EvalExpr(node.cond, env);
          if (!cond.ok) {
            return cond;
          }
          bool cond_bool = false;
          if (!TryGetCtBool(cond.value, cond_bool)) {
            return {};
          }
          if (cond_bool) {
            return EvalExpr(node.then_expr, env);
          }
          if (node.else_expr) {
            return EvalExpr(node.else_expr, env);
          }
          EvalResult out;
          out.ok = true;
          out.value = MakeCtUnit();
          return out;
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return EvalBlock(*node.block, env);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          return EvalExpr(node.body, env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          auto tuple = std::make_shared<CtTuple>();
          tuple->elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            auto value = EvalExpr(elem, env);
            if (!value.ok) {
              return value;
            }
            tuple->elements.push_back(value.value);
          }
          EvalResult out;
          out.ok = true;
          out.value = tuple;
          return out;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          auto array = std::make_shared<CtArray>();
          for (const auto& segment : node.elements) {
            if (const auto* elem = std::get_if<ast::ArrayElemSegment>(&segment)) {
              auto value = EvalExpr(elem->value, env);
              if (!value.ok) {
                return value;
              }
              array->elements.push_back(value.value);
              continue;
            }
            const auto* repeat = std::get_if<ast::ArrayRepeatSegment>(&segment);
            if (!repeat) {
              return {.ok = false};
            }
            auto value = EvalExpr(repeat->value, env);
            if (!value.ok) {
              return value;
            }
            auto count_value = EvalExpr(repeat->count, env);
            if (!count_value.ok) {
              return count_value;
            }
            const auto* prim = std::get_if<CtPrim>(&count_value.value);
            if (!prim || prim->kind != CtPrimKind::Int) {
              return {.ok = false};
            }
            const auto* int_value = std::get_if<CtPrimInt>(&prim->value);
            if (!int_value) {
              return {.ok = false};
            }
            for (unsigned long long i = 0; i < int_value->value; ++i) {
              array->elements.push_back(value.value);
            }
          }
          EvalResult out;
          out.ok = true;
          out.value = array;
          return out;
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          auto fields = std::vector<std::pair<Identifier, CtValue>>{};
          fields.reserve(node.fields.size());
          for (const auto& field : node.fields) {
            auto value = EvalExpr(field.value, env);
            if (!value.ok) {
              return value;
            }
            fields.push_back({field.name, value.value});
          }

          EvalResult out;
          out.ok = true;
          if (const auto* target = std::get_if<TypePath>(&node.target)) {
            auto record = std::make_shared<CtRecord>();
            record->path = *target;
            record->fields = std::move(fields);
            out.value = record;
            return out;
          }
          if (const auto* target =
                  std::get_if<ast::ModalStateRef>(&node.target)) {
            auto state = std::make_shared<CtModalState>();
            state->target = *target;
            state->fields = std::move(fields);
            out.value = state;
            return out;
          }
          return {};
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.path.empty()) {
            return {};
          }
          auto enum_value = std::make_shared<CtEnum>();
          enum_value->path.assign(node.path.begin(), node.path.end() - 1);
          enum_value->variant = node.path.back();
          if (node.payload_opt.has_value()) {
            bool ok = true;
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    CtTuplePayload tuple;
                    tuple.elements.reserve(payload.elements.size());
                    for (const auto& elem : payload.elements) {
                      auto value = EvalExpr(elem, env);
                      if (!value.ok) {
                        ok = false;
                        return;
                      }
                      tuple.elements.push_back(value.value);
                    }
                    enum_value->payload = std::move(tuple);
                  } else if constexpr (std::is_same_v<P,
                                                       ast::EnumPayloadBrace>) {
                    CtRecordPayload record;
                    record.fields.reserve(payload.fields.size());
                    for (const auto& field : payload.fields) {
                      auto value = EvalExpr(field.value, env);
                      if (!value.ok) {
                        ok = false;
                        return;
                      }
                      record.fields.push_back({field.name, value.value});
                    }
                    enum_value->payload = std::move(record);
                  }
                },
                *node.payload_opt);
            if (!ok) {
              return {};
            }
          }
          EvalResult out;
          out.ok = true;
          out.value = enum_value;
          return out;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto base = EvalExpr(node.base, env);
          if (!base.ok) {
            return base;
          }
          const auto* record =
              std::get_if<std::shared_ptr<CtRecord>>(&base.value);
          if (record && *record) {
            for (const auto& field : (*record)->fields) {
              if (field.first == node.name) {
                EvalResult out;
                out.ok = true;
                out.value = field.second;
                return out;
              }
            }
            return {};
          }
          const auto* modal_state =
              std::get_if<std::shared_ptr<CtModalState>>(&base.value);
          if (!modal_state || !*modal_state) {
            return {};
          }
          for (const auto& field : (*modal_state)->fields) {
            if (field.first == node.name) {
              EvalResult out;
              out.ok = true;
              out.value = field.second;
              return out;
            }
          }
          return {};
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return EvalExpr(node.expr, env);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return EvalCall(node, env);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return EvalMethodCall(node, env);
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          SPEC_RULE_AT("CtEval-TypeLiteral", expr->span);
          EvalResult out;
          out.ok = true;
          out.value = CtType{node.type};
          return out;
        } else if constexpr (std::is_same_v<T, ast::QuoteExpr>) {
          return EvalQuoteExpr(node, env, expr->span, std::nullopt);
        } else {
          return {};
        }
      },
      expr->node);
}

}  // namespace cursive::frontend::comptime_internal
