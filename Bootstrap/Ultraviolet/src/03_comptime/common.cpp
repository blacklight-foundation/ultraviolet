#include "03_comptime/comptime_internal.h"

#include <string>
#include <string_view>

#include "00_core/diagnostic_messages.h"

namespace ultraviolet::frontend::comptime_internal {

void AppendDiags(core::DiagnosticStream& out, const core::DiagnosticStream& add) {
  for (const auto& diag : add) {
    core::Emit(out, diag);
  }
}

void EmitComptimeDiag(CtEnv& env,
                      std::string_view diag_id,
                      const core::Span& span) {
  if (!CtDiags(env)) {
    return;
  }
  if (auto diag = core::MakeDiagnosticById(diag_id, span)) {
    core::Emit(*CtDiags(env), *diag);
  }
}

bool HasAttribute(const AttributeList& attrs, std::string_view name) {
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      return true;
    }
  }
  return false;
}

const AttributeItem* FindAttribute(const AttributeList& attrs,
                                   std::string_view name) {
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      return &attr;
    }
  }
  return nullptr;
}

AttributeList StripAttribute(const AttributeList& attrs,
                             std::string_view name) {
  AttributeList out;
  out.reserve(attrs.size());
  for (const auto& attr : attrs) {
    if (attr.name != name) {
      out.push_back(attr);
    }
  }
  return out;
}

CtEnv CtEmptyEnv(const ast::ASTModule& module) {
  CtEnv env;
  env.current_module = module.path;
  env.site.module_path = module.path;
  env.site.ordinal = 0;
  env.site.span = {};
  env.current_item_index = 0;
  env.current_span = {};
  env.quote_ctx.reset();
  env.return_quote_kind.reset();
  return env;
}

CtEnv WithCtCaps(CtEnv env, const AttributeList& attrs, bool derive_body) {
  env.caps.clear();
  env.values["introspect"] = MakeCtUnit();
  env.values["diagnostics"] = MakeCtUnit();
  env.caps.push_back("Introspect");
  env.caps.push_back("ComptimeDiagnostics");
  if (derive_body || HasAttribute(attrs, "emit")) {
    env.values["emitter"] = MakeCtUnit();
    env.caps.push_back("TypeEmitter");
  }
  if (HasAttribute(attrs, "files")) {
    if (!env.files) {
      EmitComptimeDiag(env, "E-CTE-0060", env.current_span);
      return env;
    }
    env.values["files"] = MakeCtUnit();
    env.caps.push_back("ProjectFiles");
  }
  return env;
}

CtEnv WithCtSite(CtEnv env, std::size_t ord, const core::Span& sp) {
  env.site.ordinal = ord;
  env.site.span = sp;
  env.current_item_index = ord;
  env.current_span = sp;
  return env;
}

CtEnv BindCtProc(CtEnv env, const ComptimeProcedureDecl& proc) {
  env.procs[proc.name] = proc;
  return env;
}

const std::unordered_map<std::string, CtValue>& CtVals(const CtEnv& env) {
  return env.values;
}

const std::unordered_map<std::string, ComptimeProcedureDecl>& CtProcs(
    const CtEnv& env) {
  return env.procs;
}

const std::vector<std::string>& CtCaps(const CtEnv& env) {
  return env.caps;
}

const CtSite& CtSiteOf(const CtEnv& env) {
  return env.site;
}

const std::optional<CtQuoteCtx>& CtQuoteCtxOf(const CtEnv& env) {
  return env.quote_ctx;
}

std::shared_ptr<const ProjectFileSnapshot> CtFiles(const CtEnv& env) {
  return env.files;
}

const std::filesystem::path& CtProjectRoot(const CtEnv& env) {
  return env.project_root;
}

core::DiagnosticStream* CtDiags(const CtEnv& env) {
  return env.diags;
}

std::vector<ASTItem>* CtPendingEmits(const CtEnv& env) {
  return env.pending_emits;
}

std::size_t CtFreshSeed(const CtEnv& env) {
  return env.next_hygiene;
}

std::size_t TakeFreshSeed(CtEnv& env) {
  return env.next_hygiene++;
}

CtAstKind AstKindOf(const CtAst& ast) {
  return ast.kind;
}

const std::variant<ExprPtr, Stmt, ASTItem, TypePtr, PatternPtr>& AstPayloadOf(
    const CtAst& ast) {
  return ast.payload;
}

std::optional<core::Span> AstSpanOf(const CtAst& ast) {
  return ast.span;
}

const std::optional<CtHygiene>& AstHygieneOf(const CtAst& ast) {
  return ast.hygiene;
}

CtAst AstOf(CtAstKind kind,
            std::variant<ExprPtr, Stmt, ASTItem, TypePtr, PatternPtr> payload) {
  CtAst ast;
  ast.kind = kind;
  ast.payload = std::move(payload);
  ast.span.reset();
  ast.hygiene.reset();
  return ast;
}

CtValue MakeCtUnit() {
  return CtPrim{CtPrimKind::Unit, std::monostate{}};
}

CtValue MakeCtBool(bool value) {
  return CtPrim{CtPrimKind::Bool, value};
}

CtValue MakeCtInt(unsigned long long value,
                  std::string_view suffix,
                  std::string_view lexeme) {
  CtPrimInt int_value;
  int_value.value = value;
  int_value.suffix = std::string(suffix);
  if (lexeme.empty()) {
    int_value.lexeme = std::to_string(value);
    int_value.lexeme.append(int_value.suffix);
  } else {
    int_value.lexeme = std::string(lexeme);
  }
  return CtPrim{CtPrimKind::Int, std::move(int_value)};
}

CtValue MakeCtFloat(std::string_view lexeme) {
  return CtPrim{CtPrimKind::Float, CtPrimFloat{std::string(lexeme)}};
}

CtValue MakeCtChar(std::string_view lexeme) {
  return CtPrim{CtPrimKind::Char, CtPrimChar{std::string(lexeme)}};
}

const CtPrim* AsCtPrim(const CtValue& value) {
  return std::get_if<CtPrim>(&value);
}

bool TryGetCtBool(const CtValue& value, bool& out) {
  const auto* prim = AsCtPrim(value);
  if (!prim || prim->kind != CtPrimKind::Bool) {
    return false;
  }
  const auto* bool_value = std::get_if<bool>(&prim->value);
  if (!bool_value) {
    return false;
  }
  out = *bool_value;
  return true;
}

const CtPrimInt* TryGetCtInt(const CtValue& value) {
  const auto* prim = AsCtPrim(value);
  if (!prim || prim->kind != CtPrimKind::Int) {
    return nullptr;
  }
  return std::get_if<CtPrimInt>(&prim->value);
}

bool CtIterable(const CtValue& value) {
  return std::holds_alternative<std::shared_ptr<CtArray>>(value) ||
         std::holds_alternative<std::shared_ptr<CtSlice>>(value);
}

const std::vector<CtValue>* CtElems(const CtValue& value) {
  if (const auto* array = std::get_if<std::shared_ptr<CtArray>>(&value)) {
    return (array && *array) ? &(*array)->elements : nullptr;
  }
  if (const auto* slice = std::get_if<std::shared_ptr<CtSlice>>(&value)) {
    return (slice && *slice) ? &(*slice)->elements : nullptr;
  }
  return nullptr;
}

namespace {

ExprPtr MakeBoolLiteralExpr(const core::Span& span, bool value) {
  ast::Token tok;
  tok.kind = ast::TokenKind::BoolLiteral;
  tok.lexeme = value ? "true" : "false";
  tok.span = span;

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = ast::LiteralExpr{tok};
  return expr;
}

ExprPtr MakeU64LiteralExpr(const core::Span& span,
                           std::string_view lexeme) {
  ast::Token tok;
  tok.kind = ast::TokenKind::IntLiteral;
  tok.lexeme = std::string(lexeme);
  tok.span = span;

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = ast::LiteralExpr{tok};
  return expr;
}

ExprPtr MakeFloatLiteralExpr(const core::Span& span, std::string_view lexeme) {
  ast::Token tok;
  tok.kind = ast::TokenKind::FloatLiteral;
  tok.lexeme = std::string(lexeme);
  tok.span = span;

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = ast::LiteralExpr{tok};
  return expr;
}

ExprPtr MakeCharLiteralExpr(const core::Span& span, std::string_view lexeme) {
  ast::Token tok;
  tok.kind = ast::TokenKind::CharLiteral;
  tok.lexeme = std::string(lexeme);
  tok.span = span;

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = ast::LiteralExpr{tok};
  return expr;
}

ExprPtr MakeStringLiteralExpr(const core::Span& span, std::string_view value) {
  ast::Token tok;
  tok.kind = ast::TokenKind::StringLiteral;
  tok.lexeme.reserve(value.size() + 2);
  tok.lexeme.push_back('"');
  tok.lexeme.append(value);
  tok.lexeme.push_back('"');
  tok.span = span;

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = ast::LiteralExpr{tok};
  return expr;
}

ExprPtr MakeUnitExpr(const core::Span& span) {
  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = ast::TupleExpr{};
  return expr;
}

ExprPtr MakeRecordLiteralExpr(const core::Span& span, const CtRecord& record) {
  ast::RecordExpr expr_node;
  expr_node.target = TypePath(record.path.begin(), record.path.end());
  expr_node.fields.reserve(record.fields.size());
  for (const auto& field : record.fields) {
    ExprPtr lit = LiteralizeValue(field.second, span);
    if (!lit) {
      return nullptr;
    }
    ast::FieldInit init;
    init.name = field.first;
    init.value = lit;
    init.span = span;
    expr_node.fields.push_back(std::move(init));
  }

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = std::move(expr_node);
  return expr;
}

ExprPtr MakeEnumLiteralExpr(const core::Span& span, const CtEnum& value) {
  ast::EnumLiteralExpr expr_node;
  expr_node.path = value.path;
  expr_node.path.push_back(value.variant);

  if (const auto* tuple_payload = std::get_if<CtTuplePayload>(&value.payload)) {
    ast::EnumPayloadParen paren;
    paren.elements.reserve(tuple_payload->elements.size());
    for (const auto& elem : tuple_payload->elements) {
      ExprPtr lit = LiteralizeValue(elem, span);
      if (!lit) {
        return nullptr;
      }
      paren.elements.push_back(lit);
    }
    expr_node.payload_opt = std::move(paren);
  } else if (const auto* record_payload =
                 std::get_if<CtRecordPayload>(&value.payload)) {
    ast::EnumPayloadBrace brace;
    brace.fields.reserve(record_payload->fields.size());
    for (const auto& field : record_payload->fields) {
      ExprPtr lit = LiteralizeValue(field.second, span);
      if (!lit) {
        return nullptr;
      }
      ast::FieldInit init;
      init.name = field.first;
      init.value = lit;
      init.span = span;
      brace.fields.push_back(std::move(init));
    }
    expr_node.payload_opt = std::move(brace);
  }

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = std::move(expr_node);
  return expr;
}

ExprPtr MakeModalStateLiteralExpr(const core::Span& span,
                                  const CtModalState& state) {
  ast::RecordExpr expr_node;
  expr_node.target = state.target;
  expr_node.fields.reserve(state.fields.size());
  for (const auto& field : state.fields) {
    ExprPtr lit = LiteralizeValue(field.second, span);
    if (!lit) {
      return nullptr;
    }
    ast::FieldInit init;
    init.name = field.first;
    init.value = lit;
    init.span = span;
    expr_node.fields.push_back(std::move(init));
  }

  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = std::move(expr_node);
  return expr;
}

}  // namespace

CtValue MakeSpanValue(const core::Span& span) {
  auto record = std::make_shared<CtRecord>();
  record->path = {"SourceSpan"};
  record->fields = {
      {"file", CtString{span.file}},
      {"start_line", MakeCtInt(span.start_line, "usize")},
      {"start_col", MakeCtInt(span.start_col, "usize")},
      {"end_line", MakeCtInt(span.end_line, "usize")},
      {"end_col", MakeCtInt(span.end_col, "usize")},
  };
  return record;
}

ExprPtr LiteralizeValue(const CtValue& value, const core::Span& span) {
  if (const auto* prim = std::get_if<CtPrim>(&value)) {
    switch (prim->kind) {
      case CtPrimKind::Unit:
        return MakeUnitExpr(span);
      case CtPrimKind::Bool: {
        if (const auto* b = std::get_if<bool>(&prim->value)) {
          return MakeBoolLiteralExpr(span, *b);
        }
        return nullptr;
      }
      case CtPrimKind::Int: {
        if (const auto* i = std::get_if<CtPrimInt>(&prim->value)) {
          return MakeU64LiteralExpr(span, i->lexeme);
        }
        return nullptr;
      }
      case CtPrimKind::Float: {
        if (const auto* f = std::get_if<CtPrimFloat>(&prim->value)) {
          return MakeFloatLiteralExpr(span, f->lexeme);
        }
        return nullptr;
      }
      case CtPrimKind::Char: {
        if (const auto* c = std::get_if<CtPrimChar>(&prim->value)) {
          return MakeCharLiteralExpr(span, c->lexeme);
        }
        return nullptr;
      }
    }
  }
  if (const auto* s = std::get_if<CtString>(&value)) {
    return MakeStringLiteralExpr(span, s->value);
  }
  if (std::holds_alternative<CtBytes>(value)) {
    return nullptr;
  }
  if (const auto* tuple = std::get_if<std::shared_ptr<CtTuple>>(&value)) {
    if (!*tuple) {
      return nullptr;
    }
    ast::TupleExpr expr_node;
    expr_node.elements.reserve((*tuple)->elements.size());
    for (const auto& elem : (*tuple)->elements) {
      ExprPtr lit = LiteralizeValue(elem, span);
      if (!lit) {
        return nullptr;
      }
      expr_node.elements.push_back(lit);
    }
    auto expr = std::make_shared<Expr>();
    expr->span = span;
    expr->node = std::move(expr_node);
    return expr;
  }
  if (const auto* array = std::get_if<std::shared_ptr<CtArray>>(&value)) {
    if (!*array) {
      return nullptr;
    }
    ast::ArrayExpr expr_node;
    expr_node.elements.reserve((*array)->elements.size());
    for (const auto& elem : (*array)->elements) {
      ExprPtr lit = LiteralizeValue(elem, span);
      if (!lit) {
        return nullptr;
      }
      expr_node.elements.push_back(lit);
    }
    auto expr = std::make_shared<Expr>();
    expr->span = span;
    expr->node = std::move(expr_node);
    return expr;
  }
  if (const auto* record = std::get_if<std::shared_ptr<CtRecord>>(&value)) {
    if (!*record) {
      return nullptr;
    }
    return MakeRecordLiteralExpr(span, **record);
  }
  if (const auto* modal_state =
          std::get_if<std::shared_ptr<CtModalState>>(&value)) {
    if (!*modal_state) {
      return nullptr;
    }
    return MakeModalStateLiteralExpr(span, **modal_state);
  }
  if (const auto* enum_value = std::get_if<std::shared_ptr<CtEnum>>(&value)) {
    if (!*enum_value) {
      return nullptr;
    }
    return MakeEnumLiteralExpr(span, **enum_value);
  }
  if (const auto* ast_value = std::get_if<CtAst>(&value)) {
    if (ast_value->kind != CtAstKind::Expr) {
      return nullptr;
    }
    if (const auto* expr = std::get_if<ExprPtr>(&ast_value->payload)) {
      return *expr;
    }
    return nullptr;
  }
  return nullptr;
}

core::Span SpanOfItem(const ASTItem& item) {
  return std::visit([](const auto& node) -> core::Span { return node.span; }, item);
}

}  // namespace ultraviolet::frontend::comptime_internal
