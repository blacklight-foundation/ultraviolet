#include "03_comptime/comptime_internal.h"

#include <optional>
#include <string>
#include <string_view>

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
        EvalResult result;
        result.ok = true;
        result.value = CtString{ModulePathText(CtSiteOf(env).module_path)};
        return result;
      }
      if (call.name == "current_span" && call.args.empty()) {
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
          AppendCtCodedDiagnostic(
              env, "E-CTE-0070", core::Severity::Error, msg->value);
          EvalResult result;
          result.ok = true;
          result.value = MakeCtUnit();
          return result;
        }
        if (call.name == "warning") {
          AppendCtCodedDiagnostic(
              env, "W-CTE-0071", core::Severity::Warning, msg->value);
          EvalResult result;
          result.ok = true;
          result.value = MakeCtUnit();
          return result;
        }
        if (call.name == "note") {
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
      auto value = EvalExpr(let_stmt->binding.init, local);
      if (!value.ok) {
        return value;
      }
      if (const auto* pat =
              std::get_if<ast::IdentifierPattern>(&let_stmt->binding.pat->node)) {
        local.values[pat->name] = value.value;
      }
      continue;
    }

    if (const auto* var_stmt = std::get_if<ast::VarStmt>(&stmt)) {
      if (!var_stmt->binding.init) {
        continue;
      }
      auto value = EvalExpr(var_stmt->binding.init, local);
      if (!value.ok) {
        return value;
      }
      if (const auto* pat =
              std::get_if<ast::IdentifierPattern>(&var_stmt->binding.pat->node)) {
        local.values[pat->name] = value.value;
      }
      continue;
    }

    if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
      auto value = EvalExpr(expr_stmt->value, local);
      if (!value.ok) {
        return value;
      }
      continue;
    }

    if (const auto* ret_stmt = std::get_if<ast::ReturnStmt>(&stmt)) {
      auto value = EvalExpr(ret_stmt->value_opt, local);
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
          return {};
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
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return EvalExpr(node.expr, env);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return EvalCall(node, env);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return EvalMethodCall(node, env);
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          EvalResult out;
          out.ok = true;
          out.value = CtType{node.type};
          return out;
        } else if constexpr (std::is_same_v<T, ast::QuoteExpr>) {
          EvalResult out;
          auto parsed_ast = ParseQuotedAst(node, env, *env.diags);
          if (!parsed_ast.has_value()) {
            out.ok = false;
            return out;
          }
          if (!parsed_ast->span.has_value()) {
            parsed_ast->span = expr->span;
          }
          if (!parsed_ast->hygiene.has_value()) {
            parsed_ast->hygiene = CtHygiene{CtSiteOf(env), CtSiteOf(env), 0};
          }
          out.ok = true;
          out.value = std::move(*parsed_ast);
          return out;
        } else {
          return {};
        }
      },
      expr->node);
}

}  // namespace cursive::frontend::comptime_internal
