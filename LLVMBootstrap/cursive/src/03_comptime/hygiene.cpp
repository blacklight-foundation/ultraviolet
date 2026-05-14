#include "03_comptime/comptime_internal.h"

#include <cctype>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cursive::frontend::comptime_internal {

namespace {

struct HygieneContext {
  CtEnv& env;
  CtSite quote_site;
  CtSite emit_site;
  std::size_t next_seed = 0;
  bool ok = true;
  std::unordered_set<std::string> all_names;
  std::unordered_set<std::string> unhygienic_names;
  std::vector<std::unordered_map<std::string, std::string>> scopes;
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

std::uint64_t Fnv1aAppend(std::uint64_t hash, std::string_view text) {
  for (unsigned char ch : text) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::uint64_t Fnv1aAppendNumber(std::uint64_t hash, std::size_t value) {
  return Fnv1aAppend(hash, std::to_string(value));
}

std::uint64_t SiteHash(const CtSite& quote_site, const CtSite& emit_site) {
  std::uint64_t hash = 1469598103934665603ull;
  hash = Fnv1aAppend(hash, ModulePathText(quote_site.module_path));
  hash = Fnv1aAppendNumber(hash, quote_site.ordinal);
  hash = Fnv1aAppend(hash, quote_site.span.file);
  hash = Fnv1aAppendNumber(hash, quote_site.span.start_line);
  hash = Fnv1aAppendNumber(hash, quote_site.span.start_col);
  hash = Fnv1aAppendNumber(hash, quote_site.span.end_line);
  hash = Fnv1aAppendNumber(hash, quote_site.span.end_col);
  hash = Fnv1aAppend(hash, ModulePathText(emit_site.module_path));
  hash = Fnv1aAppendNumber(hash, emit_site.ordinal);
  hash = Fnv1aAppend(hash, emit_site.span.file);
  hash = Fnv1aAppendNumber(hash, emit_site.span.start_line);
  hash = Fnv1aAppendNumber(hash, emit_site.span.start_col);
  hash = Fnv1aAppendNumber(hash, emit_site.span.end_line);
  hash = Fnv1aAppendNumber(hash, emit_site.span.end_col);
  return hash;
}

std::string SanitizeName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char ch : name) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9')) {
      out.push_back(static_cast<char>(ch));
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    out = "id";
  }
  return out;
}

core::Span AstDiagSpan(const CtAst& ast, const CtSite& fallback_site) {
  if (ast.span.has_value()) {
    return *ast.span;
  }
  return fallback_site.span;
}

void EmitHygieneDiag(CtEnv& env,
                     std::string_view diag_id,
                     const CtAst& ast,
                     const CtSite& fallback_site) {
  EmitComptimeDiag(env, diag_id, AstDiagSpan(ast, fallback_site));
}

void PushScope(HygieneContext& ctx) {
  ctx.scopes.emplace_back();
}

void PopScope(HygieneContext& ctx) {
  if (!ctx.scopes.empty()) {
    ctx.scopes.pop_back();
  }
}

std::optional<std::string> LookupName(const HygieneContext& ctx,
                                      std::string_view name) {
  for (auto it = ctx.scopes.rbegin(); it != ctx.scopes.rend(); ++it) {
    auto found = it->find(std::string(name));
    if (found != it->end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

void BindPreservedName(HygieneContext& ctx, std::string_view name) {
  if (ctx.scopes.empty()) {
    PushScope(ctx);
  }
  (*ctx.scopes.rbegin())[std::string(name)] = std::string(name);
}

std::optional<std::string> FreshHygienicName(HygieneContext& ctx,
                                             std::string_view base,
                                             const CtAst& ast) {
  const std::uint64_t site_hash = SiteHash(ctx.quote_site, ctx.emit_site);
  const std::string sanitized = SanitizeName(base);
  for (;;) {
    std::ostringstream buffer;
    buffer << "hyg_" << std::hex << std::nouppercase << site_hash << "_"
           << std::dec << ctx.next_seed++ << "_" << sanitized;
    std::string candidate = buffer.str();
    if (ctx.unhygienic_names.contains(candidate)) {
      EmitHygieneDiag(ctx.env, "E-CTE-0241", ast, ctx.emit_site);
      ctx.ok = false;
      return std::nullopt;
    }
    if (ctx.all_names.insert(candidate).second) {
      return candidate;
    }
  }
}

void ReserveName(HygieneContext& ctx, std::string_view name, bool unhygienic) {
  if (name.empty()) {
    return;
  }
  ctx.all_names.insert(std::string(name));
  if (unhygienic) {
    ctx.unhygienic_names.insert(std::string(name));
  }
}

void CollectExprNames(const ExprPtr& expr, HygieneContext& ctx);
void CollectTypeNames(const TypePtr& type, HygieneContext& ctx);
void CollectPatternNames(const PatternPtr& pattern, HygieneContext& ctx);
void CollectStmtNames(const Stmt& stmt, HygieneContext& ctx);
void CollectBlockNames(const Block& block, HygieneContext& ctx);
void CollectItemNames(const ASTItem& item, HygieneContext& ctx);

void RenameExpr(const ExprPtr& expr, HygieneContext& ctx);
void RenameType(const TypePtr& type, HygieneContext& ctx);
void RenamePattern(const PatternPtr& pattern, HygieneContext& ctx, const CtAst& ast);
void RenameStmt(Stmt& stmt, HygieneContext& ctx, const CtAst& ast);
void RenameBlock(Block& block, HygieneContext& ctx, const CtAst& ast);
void RenameItem(ASTItem& item, HygieneContext& ctx, const CtAst& ast);
void CollectUsingAliasNames(const ast::UsingClause& clause, HygieneContext& ctx);
void RenameUsingAliases(ast::UsingClause& clause,
                        HygieneContext& ctx,
                        const CtAst& ast);

void CollectGenericParamsNames(const std::optional<ast::GenericParams>& params_opt,
                               HygieneContext& ctx) {
  if (!params_opt.has_value()) {
    return;
  }
  for (const auto& param : params_opt->params) {
    ReserveName(ctx, param.name, false);
    CollectTypeNames(param.default_type, ctx);
  }
}

void CollectWhereClauseNames(const std::optional<ast::PredicateClause>& clause_opt,
                             HygieneContext& ctx) {
  if (!clause_opt.has_value()) {
    return;
  }
  for (const auto& predicate : *clause_opt) {
    CollectTypeNames(predicate.type, ctx);
  }
}

void CollectTypeInvariantNames(
    const std::optional<ast::TypeInvariant>& invariant_opt,
    HygieneContext& ctx) {
  if (!invariant_opt.has_value()) {
    return;
  }
  CollectExprNames(invariant_opt->predicate, ctx);
}

void CollectExprNames(const ExprPtr& expr, HygieneContext& ctx) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          ReserveName(ctx, node.name, node.from_splice);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          CollectExprNames(node.lhs, ctx);
          CollectExprNames(node.rhs, ctx);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          std::visit(
              [&](const auto& args) {
                using A = std::decay_t<decltype(args)>;
                if constexpr (std::is_same_v<A, ast::ParenArgs>) {
                  for (const auto& arg : args.args) {
                    CollectExprNames(arg.value, ctx);
                  }
                } else {
                  for (const auto& field : args.fields) {
                    CollectExprNames(field.value, ctx);
                  }
                }
              },
              node.args);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          CollectExprNames(node.callee, ctx);
          for (const auto& arg : node.args) {
            CollectExprNames(arg.value, ctx);
          }
          for (const auto& type_arg : node.generic_args) {
            CollectTypeNames(type_arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          CollectExprNames(node.receiver, ctx);
          for (const auto& arg : node.args) {
            CollectExprNames(arg.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          CollectExprNames(node.lhs, ctx);
          CollectExprNames(node.rhs, ctx);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          CollectExprNames(node.value, ctx);
          CollectTypeNames(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          CollectExprNames(node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          CollectExprNames(node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          if (node.region_opt.has_value()) {
            ReserveName(ctx, *node.region_opt, false);
          }
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr> ||
                             std::is_same_v<T, ast::ArrayExpr>) {
          if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              CollectExprNames(elem, ctx);
            }
          } else {
            ast::ForEachArrayExprSubexpr(node, [&](const auto& elem) {
              CollectExprNames(elem, ctx);
            });
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          CollectExprNames(node.value, ctx);
          CollectExprNames(node.count, ctx);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          CollectTypeNames(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          CollectTypeNames(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            CollectExprNames(field.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      CollectExprNames(elem, ctx);
                    }
                  } else {
                    for (const auto& field : payload.fields) {
                      CollectExprNames(field.value, ctx);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          CollectExprNames(node.cond, ctx);
          CollectExprNames(node.then_expr, ctx);
          CollectExprNames(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          CollectExprNames(node.scrutinee, ctx);
          CollectPatternNames(node.pattern, ctx);
          CollectExprNames(node.then_expr, ctx);
          CollectExprNames(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          CollectExprNames(node.scrutinee, ctx);
          for (const auto& clause : node.cases) {
            CollectPatternNames(clause.pattern, ctx);
            CollectExprNames(clause.body, ctx);
          }
          CollectExprNames(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.invariant_opt.has_value()) {
            CollectExprNames(node.invariant_opt->predicate, ctx);
          }
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          CollectExprNames(node.cond, ctx);
          if (node.invariant_opt.has_value()) {
            CollectExprNames(node.invariant_opt->predicate, ctx);
          }
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          CollectPatternNames(node.pattern, ctx);
          CollectTypeNames(node.type_opt, ctx);
          CollectExprNames(node.iter, ctx);
          if (node.invariant_opt.has_value()) {
            CollectExprNames(node.invariant_opt->predicate, ctx);
          }
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          CollectBlockNames(*node.block, ctx);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          CollectBlockNames(*node.block, ctx);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          CollectExprNames(node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          CollectExprNames(node.cond, ctx);
          CollectBlockNames(*node.then_block, ctx);
          if (node.else_block_opt) {
            CollectBlockNames(*node.else_block_opt, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          CollectPatternNames(node.pattern, ctx);
          CollectTypeNames(node.type_opt, ctx);
          CollectExprNames(node.iter, ctx);
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          CollectExprNames(node.expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          CollectTypeNames(node.from, ctx);
          CollectTypeNames(node.to, ctx);
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          for (const auto& param : node.params) {
            ReserveName(ctx, param.name, false);
            CollectTypeNames(param.type_opt, ctx);
          }
          CollectTypeNames(node.ret_type_opt, ctx);
          CollectExprNames(node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          CollectExprNames(node.lhs, ctx);
          CollectExprNames(node.rhs, ctx);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          CollectExprNames(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          CollectExprNames(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          CollectExprNames(node.base, ctx);
          CollectExprNames(node.index, ctx);
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          CollectExprNames(node.callee, ctx);
          for (const auto& arg : node.type_args) {
            CollectTypeNames(arg, ctx);
          }
          for (const auto& arg : node.args) {
            CollectExprNames(arg.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            CollectPatternNames(arm.pattern, ctx);
            CollectExprNames(arm.expr, ctx);
            CollectExprNames(arm.handler.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& elem : node.exprs) {
            CollectExprNames(elem, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          CollectExprNames(node.domain, ctx);
          for (const auto& opt : node.opts) {
            CollectExprNames(opt.value, ctx);
          }
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            CollectExprNames(opt.value, ctx);
          }
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          CollectExprNames(node.handle, ctx);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          CollectPatternNames(node.pattern, ctx);
          CollectExprNames(node.range, ctx);
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          CollectTypeNames(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          CollectExprNames(node.expr, ctx);
        }
      },
      expr->node);
}

void CollectTypeNames(const TypePtr& type, HygieneContext& ctx) {
  if (!type) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          CollectTypeNames(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& elem : node.types) {
            CollectTypeNames(elem, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            CollectTypeNames(param.type, ctx);
          }
          CollectTypeNames(node.ret, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            CollectTypeNames(param.type, ctx);
          }
          CollectTypeNames(node.ret, ctx);
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              CollectTypeNames(dep.type, ctx);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            CollectTypeNames(elem, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          CollectTypeNames(node.element, ctx);
          CollectExprNames(node.length, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          CollectTypeNames(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          CollectTypeNames(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          CollectTypeNames(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            CollectTypeNames(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          for (const auto& arg : node.generic_args) {
            CollectTypeNames(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          for (const auto& arg : node.args) {
            CollectTypeNames(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          CollectTypeNames(node.base, ctx);
          CollectExprNames(node.predicate, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive>) {
          CollectTypeNames(node.base, ctx);
        }
      },
      type->node);
}

void CollectPatternNames(const PatternPtr& pattern, HygieneContext& ctx) {
  if (!pattern) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          ReserveName(ctx, node.name, node.name_splice_opt.has_value());
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          ReserveName(ctx, node.name, node.name_splice_opt.has_value());
          CollectTypeNames(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            CollectPatternNames(elem, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            CollectPatternNames(field.pattern_opt, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                    for (const auto& elem : payload.elements) {
                      CollectPatternNames(elem, ctx);
                    }
                  } else {
                    for (const auto& field : payload.fields) {
                      CollectPatternNames(field.pattern_opt, ctx);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (node.fields_opt.has_value()) {
            for (const auto& field : node.fields_opt->fields) {
              CollectPatternNames(field.pattern_opt, ctx);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          CollectPatternNames(node.lo, ctx);
          CollectPatternNames(node.hi, ctx);
        }
      },
      pattern->node);
}

void CollectStmtNames(const Stmt& stmt, HygieneContext& ctx) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          CollectPatternNames(node.binding.pat, ctx);
          CollectTypeNames(node.binding.type_opt, ctx);
          CollectExprNames(node.binding.init, ctx);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          ReserveName(ctx, node.alias, node.alias_splice_opt.has_value());
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          CollectExprNames(node.place, ctx);
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          CollectExprNames(node.place, ctx);
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          CollectExprNames(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::ComptimeStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (node.alias_opt.has_value()) {
            ReserveName(ctx, *node.alias_opt, node.alias_splice_opt.has_value());
          }
          CollectExprNames(node.opts_opt, ctx);
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.target_opt.has_value()) {
            ReserveName(ctx, *node.target_opt, false);
          }
          CollectBlockNames(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          CollectExprNames(node.value_opt, ctx);
        }
      },
      stmt);
}

void CollectContractNames(const std::optional<ast::ContractClause>& contract,
                          HygieneContext& ctx) {
  if (!contract.has_value()) {
    return;
  }
  CollectExprNames(contract->precondition, ctx);
  CollectExprNames(contract->postcondition, ctx);
}

void CollectBlockNames(const Block& block, HygieneContext& ctx) {
  for (const auto& stmt : block.stmts) {
    CollectStmtNames(stmt, ctx);
  }
  CollectExprNames(block.tail_opt, ctx);
}

void CollectMethodLikeNames(const std::optional<ast::GenericParams>& generic_params,
                            const std::optional<ast::PredicateClause>* where_clause_opt,
                            const std::vector<ast::Param>& params,
                            const TypePtr& return_type_opt,
                            const std::optional<ast::ContractClause>& contract,
                            const BlockPtr& body_opt,
                            HygieneContext& ctx) {
  CollectGenericParamsNames(generic_params, ctx);
  if (where_clause_opt) {
    CollectWhereClauseNames(*where_clause_opt, ctx);
  }
  for (const auto& param : params) {
    ReserveName(ctx, param.name, param.name_splice_opt.has_value());
    CollectTypeNames(param.type, ctx);
  }
  CollectTypeNames(return_type_opt, ctx);
  CollectContractNames(contract, ctx);
  if (body_opt) {
    CollectBlockNames(*body_opt, ctx);
  }
}

void CollectItemNames(const ASTItem& item, HygieneContext& ctx) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          CollectUsingAliasNames(node.clause, ctx);
        } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
          if (node.alias_opt.has_value()) {
            ReserveName(ctx, *node.alias_opt, false);
          }
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                      std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          ReserveName(ctx, node.name, false);
          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            CollectMethodLikeNames(node.generic_params,
                                   &node.predicate_clause_opt,
                                   node.params, node.return_type_opt,
                                   node.contract, node.body, ctx);
          } else {
            CollectMethodLikeNames(node.generic_params, nullptr,
                                   node.params, node.return_type_opt,
                                   node.contract, node.body, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (const auto& ext : node.items) {
            std::visit(
                [&](const auto& ext_item) {
                  using E = std::decay_t<decltype(ext_item)>;
                  if constexpr (std::is_same_v<E, ast::ExternProcDecl>) {
                    ReserveName(ctx, ext_item.name, false);
                    CollectMethodLikeNames(ext_item.generic_params,
                                           &ext_item.where_clause,
                                           ext_item.params,
                                           ext_item.return_type_opt,
                                           ext_item.contract, nullptr, ctx);
                  }
                },
                ext);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          ReserveName(ctx, node.name, false);
          CollectGenericParamsNames(node.generic_params, ctx);
          CollectWhereClauseNames(node.predicate_clause_opt, ctx);
          CollectTypeInvariantNames(node.invariant_opt, ctx);
          for (const auto& member : node.members) {
            std::visit(
                [&](const auto& rec_member) {
                  using M = std::decay_t<decltype(rec_member)>;
                  if constexpr (std::is_same_v<M, ast::FieldDecl>) {
                    CollectTypeNames(rec_member.type, ctx);
                    CollectExprNames(rec_member.init_opt, ctx);
                  } else if constexpr (std::is_same_v<M, ast::MethodDecl>) {
                    CollectMethodLikeNames(std::optional<ast::GenericParams>{},
                                           nullptr,
                                           rec_member.params,
                                           rec_member.return_type_opt,
                                           rec_member.contract,
                                           rec_member.body, ctx);
                  } else if constexpr (std::is_same_v<M, ast::AssociatedTypeDecl>) {
                    CollectTypeNames(rec_member.default_type, ctx);
                  }
                },
                member);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          ReserveName(ctx, node.name, false);
          CollectGenericParamsNames(node.generic_params, ctx);
          CollectWhereClauseNames(node.predicate_clause_opt, ctx);
          CollectTypeInvariantNames(node.invariant_opt, ctx);
          for (const auto& variant : node.variants) {
            if (variant.payload_opt.has_value()) {
              std::visit(
                  [&](const auto& payload) {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
                      for (const auto& elem : payload.elements) {
                        CollectTypeNames(elem, ctx);
                      }
                    } else {
                      for (const auto& field : payload.fields) {
                        CollectTypeNames(field.type, ctx);
                      }
                    }
                  },
                  *variant.payload_opt);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          ReserveName(ctx, node.name, false);
          CollectGenericParamsNames(node.generic_params, ctx);
          CollectWhereClauseNames(node.predicate_clause_opt, ctx);
          CollectTypeInvariantNames(node.invariant_opt, ctx);
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              std::visit(
                  [&](const auto& state_member) {
                    using M = std::decay_t<decltype(state_member)>;
                    if constexpr (std::is_same_v<M, ast::StateFieldDecl>) {
                      CollectTypeNames(state_member.type, ctx);
                    } else if constexpr (std::is_same_v<M, ast::StateMethodDecl>) {
                      CollectMethodLikeNames(state_member.generic_params,
                                             nullptr,
                                             state_member.params,
                                             state_member.return_type_opt,
                                             state_member.contract,
                                             state_member.body, ctx);
                    } else if constexpr (std::is_same_v<M, ast::TransitionDecl>) {
                      for (const auto& param : state_member.params) {
                        ReserveName(ctx, param.name,
                                    param.name_splice_opt.has_value());
                        CollectTypeNames(param.type, ctx);
                      }
                      CollectBlockNames(*state_member.body, ctx);
                    }
                },
                member);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          ReserveName(ctx, node.name, false);
          CollectGenericParamsNames(node.generic_params, ctx);
          CollectWhereClauseNames(node.predicate_clause_opt, ctx);
          for (const auto& member : node.items) {
            std::visit(
                [&](const auto& class_item) {
                  using M = std::decay_t<decltype(class_item)>;
                  if constexpr (std::is_same_v<M, ast::ClassFieldDecl> ||
                                std::is_same_v<M, ast::AbstractFieldDecl>) {
                    CollectTypeNames(class_item.type, ctx);
                  } else if constexpr (std::is_same_v<M, ast::ClassMethodDecl>) {
                    CollectMethodLikeNames(class_item.generic_params,
                                           nullptr,
                                           class_item.params,
                                           class_item.return_type_opt,
                                           class_item.contract,
                                           class_item.body_opt, ctx);
                  } else if constexpr (std::is_same_v<M, ast::AssociatedTypeDecl>) {
                    CollectTypeNames(class_item.default_type, ctx);
                  } else if constexpr (std::is_same_v<M, ast::AbstractStateDecl>) {
                    for (const auto& field : class_item.fields) {
                      CollectTypeNames(field.type, ctx);
                    }
                  }
                },
                member);
          }
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          CollectPatternNames(node.binding.pat, ctx);
          CollectTypeNames(node.binding.type_opt, ctx);
          CollectExprNames(node.binding.init, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          ReserveName(ctx, node.name, false);
          CollectGenericParamsNames(node.generic_params, ctx);
          CollectWhereClauseNames(node.predicate_clause_opt, ctx);
          CollectTypeNames(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          ReserveName(ctx, node.name, false);
          CollectBlockNames(*node.body, ctx);
        }
      },
      item);
}

void RenameLocalRef(std::optional<Identifier>& name_opt, const HygieneContext& ctx) {
  if (!name_opt.has_value()) {
    return;
  }
  if (const auto mapped = LookupName(ctx, *name_opt)) {
    *name_opt = *mapped;
  }
}

void RenamePathHead(Path& path, const HygieneContext& ctx) {
  if (path.empty()) {
    return;
  }
  if (const auto mapped = LookupName(ctx, path.front())) {
    path.front() = *mapped;
  }
}

void BindPatternName(Identifier& name,
                     bool unhygienic,
                     HygieneContext& ctx,
                     const CtAst& ast) {
  if (unhygienic) {
    BindPreservedName(ctx, name);
    return;
  }
  auto fresh = FreshHygienicName(ctx, name, ast);
  if (!fresh.has_value()) {
    return;
  }
  if (ctx.scopes.empty()) {
    PushScope(ctx);
  }
  (*ctx.scopes.rbegin())[name] = *fresh;
  name = *fresh;
}

void BindHygienicItemName(Identifier& name,
                          HygieneContext& ctx,
                          const CtAst& ast) {
  BindPatternName(name, false, ctx, ast);
}

void CollectUsingAliasNames(const ast::UsingClause& clause, HygieneContext& ctx) {
  std::visit(
      [&](const auto& entry) {
        using T = std::decay_t<decltype(entry)>;
        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          if (entry.alias_opt.has_value()) {
            ReserveName(ctx, *entry.alias_opt, false);
          }
        } else if constexpr (std::is_same_v<T, ast::UsingList>) {
          for (const auto& spec : entry.specs) {
            if (spec.alias_opt.has_value()) {
              ReserveName(ctx, *spec.alias_opt, false);
            }
          }
        }
      },
      clause);
}

void RenameUsingAliases(ast::UsingClause& clause,
                        HygieneContext& ctx,
                        const CtAst& ast) {
  std::visit(
      [&](auto& entry) {
        using T = std::decay_t<decltype(entry)>;
        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          if (entry.alias_opt.has_value()) {
            BindHygienicItemName(*entry.alias_opt, ctx, ast);
          }
        } else if constexpr (std::is_same_v<T, ast::UsingList>) {
          for (auto& spec : entry.specs) {
            if (spec.alias_opt.has_value()) {
              BindHygienicItemName(*spec.alias_opt, ctx, ast);
            }
          }
        }
      },
      clause);
}

void RenamePattern(const PatternPtr& pattern, HygieneContext& ctx, const CtAst& ast) {
  if (!pattern || !ctx.ok) {
    return;
  }
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          BindPatternName(node.name, node.name_splice_opt.has_value(), ctx, ast);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          RenameType(node.type, ctx);
          BindPatternName(node.name, node.name_splice_opt.has_value(), ctx, ast);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (auto& elem : node.elements) {
            RenamePattern(elem, ctx, ast);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          RenamePathHead(node.path, ctx);
          for (auto& field : node.fields) {
            RenamePattern(field.pattern_opt, ctx, ast);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          RenamePathHead(node.path, ctx);
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                    for (auto& elem : payload.elements) {
                      RenamePattern(elem, ctx, ast);
                    }
                  } else {
                    for (auto& field : payload.fields) {
                      RenamePattern(field.pattern_opt, ctx, ast);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (node.fields_opt.has_value()) {
            for (auto& field : node.fields_opt->fields) {
              RenamePattern(field.pattern_opt, ctx, ast);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          RenamePattern(node.lo, ctx, ast);
          RenamePattern(node.hi, ctx, ast);
        }
      },
      pattern->node);
}

void RenameType(const TypePtr& type, HygieneContext& ctx) {
  if (!type || !ctx.ok) {
    return;
  }
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          RenameType(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (auto& elem : node.types) {
            RenameType(elem, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (auto& param : node.params) {
            RenameType(param.type, ctx);
          }
          RenameType(node.ret, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (auto& param : node.params) {
            RenameType(param.type, ctx);
          }
          RenameType(node.ret, ctx);
          if (node.deps_opt.has_value()) {
            for (auto& dep : *node.deps_opt) {
              RenameType(dep.type, ctx);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (auto& elem : node.elements) {
            RenameType(elem, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          RenameType(node.element, ctx);
          RenameExpr(node.length, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          RenameType(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          RenameType(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          RenameType(node.element, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          RenamePathHead(node.path, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          RenamePathHead(node.path, ctx);
          for (auto& arg : node.generic_args) {
            RenameType(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          RenamePathHead(node.path, ctx);
          for (auto& arg : node.generic_args) {
            RenameType(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          RenamePathHead(node.path, ctx);
          for (auto& arg : node.args) {
            RenameType(arg, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          RenamePathHead(node.path, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          RenameType(node.base, ctx);
          RenameExpr(node.predicate, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive>) {
          RenameType(node.base, ctx);
        }
      },
      type->node);
}

void RenameExpr(const ExprPtr& expr, HygieneContext& ctx) {
  if (!expr || !ctx.ok) {
    return;
  }
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (!node.from_splice) {
            if (const auto mapped = LookupName(ctx, node.name)) {
              node.name = *mapped;
            }
          }
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          RenamePathHead(node.path, ctx);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          RenamePathHead(node.path, ctx);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          RenameExpr(node.lhs, ctx);
          RenameExpr(node.rhs, ctx);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          RenamePathHead(node.path, ctx);
          std::visit(
              [&](auto& args) {
                using A = std::decay_t<decltype(args)>;
                if constexpr (std::is_same_v<A, ast::ParenArgs>) {
                  for (auto& arg : args.args) {
                    RenameExpr(arg.value, ctx);
                  }
                } else {
                  for (auto& field : args.fields) {
                    RenameExpr(field.value, ctx);
                  }
                }
              },
              node.args);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          RenameExpr(node.callee, ctx);
          for (auto& arg : node.generic_args) {
            RenameType(arg, ctx);
          }
          for (auto& arg : node.args) {
            RenameExpr(arg.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          RenameExpr(node.receiver, ctx);
          for (auto& arg : node.args) {
            RenameExpr(arg.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          RenameExpr(node.lhs, ctx);
          RenameExpr(node.rhs, ctx);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          RenameExpr(node.value, ctx);
          RenameType(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          RenameExpr(node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          RenameExpr(node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          RenameLocalRef(node.region_opt, ctx);
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr> ||
                             std::is_same_v<T, ast::ArrayExpr>) {
          if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (auto& elem : node.elements) {
              RenameExpr(elem, ctx);
            }
          } else {
            ast::ForEachArrayExprSubexpr(node, [&](auto& elem) {
              RenameExpr(elem, ctx);
            });
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          RenameExpr(node.value, ctx);
          RenameExpr(node.count, ctx);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          RenameType(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          RenameType(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          std::visit(
              [&](auto& target) {
                using R = std::decay_t<decltype(target)>;
                if constexpr (std::is_same_v<R, ast::TypePath>) {
                  RenamePathHead(target, ctx);
                } else if constexpr (std::is_same_v<R, ast::ModalStateRef>) {
                  RenamePathHead(target.path, ctx);
                  for (auto& arg : target.generic_args) {
                    RenameType(arg, ctx);
                  }
                }
              },
              node.target);
          for (auto& field : node.fields) {
            RenameExpr(field.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          RenamePathHead(node.path, ctx);
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (auto& elem : payload.elements) {
                      RenameExpr(elem, ctx);
                    }
                  } else {
                    for (auto& field : payload.fields) {
                      RenameExpr(field.value, ctx);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          RenameExpr(node.cond, ctx);
          RenameExpr(node.then_expr, ctx);
          RenameExpr(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          RenameExpr(node.scrutinee, ctx);
          PushScope(ctx);
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenamePattern(node.pattern, ctx, expr_ast);
          RenameExpr(node.then_expr, ctx);
          PopScope(ctx);
          RenameExpr(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          RenameExpr(node.scrutinee, ctx);
          for (auto& clause : node.cases) {
            PushScope(ctx);
            CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
            RenamePattern(clause.pattern, ctx, expr_ast);
            RenameExpr(clause.body, ctx);
            PopScope(ctx);
          }
          RenameExpr(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.invariant_opt.has_value()) {
            RenameExpr(node.invariant_opt->predicate, ctx);
          }
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.body, ctx, expr_ast);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          RenameExpr(node.cond, ctx);
          if (node.invariant_opt.has_value()) {
            RenameExpr(node.invariant_opt->predicate, ctx);
          }
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.body, ctx, expr_ast);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          RenameType(node.type_opt, ctx);
          RenameExpr(node.iter, ctx);
          if (node.invariant_opt.has_value()) {
            RenameExpr(node.invariant_opt->predicate, ctx);
          }
          PushScope(ctx);
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenamePattern(node.pattern, ctx, expr_ast);
          RenameBlock(*node.body, ctx, expr_ast);
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.block, ctx, expr_ast);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.block, ctx, expr_ast);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          RenameExpr(node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          RenameExpr(node.cond, ctx);
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.then_block, ctx, expr_ast);
          if (node.else_block_opt) {
            RenameBlock(*node.else_block_opt, ctx, expr_ast);
          }
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          RenameType(node.type_opt, ctx);
          RenameExpr(node.iter, ctx);
          PushScope(ctx);
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenamePattern(node.pattern, ctx, expr_ast);
          RenameBlock(*node.body, ctx, expr_ast);
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          RenameExpr(node.expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          RenameExpr(node.expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          RenameType(node.type, ctx);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          RenameType(node.from, ctx);
          RenameType(node.to, ctx);
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          for (auto& param : node.params) {
            RenameType(param.type_opt, ctx);
          }
          RenameType(node.ret_type_opt, ctx);
          PushScope(ctx);
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          for (auto& param : node.params) {
            BindPatternName(param.name, false, ctx, expr_ast);
          }
          RenameExpr(node.body, ctx);
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          RenameExpr(node.lhs, ctx);
          RenameExpr(node.rhs, ctx);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          RenameExpr(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          RenameExpr(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          RenameExpr(node.base, ctx);
          RenameExpr(node.index, ctx);
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          RenameExpr(node.callee, ctx);
          for (auto& arg : node.type_args) {
            RenameType(arg, ctx);
          }
          for (auto& arg : node.args) {
            RenameExpr(arg.value, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (auto& arm : node.arms) {
            RenameExpr(arm.expr, ctx);
            PushScope(ctx);
            CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
            RenamePattern(arm.pattern, ctx, expr_ast);
            RenameExpr(arm.handler.value, ctx);
            PopScope(ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (auto& elem : node.exprs) {
            RenameExpr(elem, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          RenameExpr(node.domain, ctx);
          for (auto& opt : node.opts) {
            RenameExpr(opt.value, ctx);
          }
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.body, ctx, expr_ast);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (auto& opt : node.opts) {
            RenameExpr(opt.value, ctx);
          }
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.body, ctx, expr_ast);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          RenameExpr(node.handle, ctx);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          RenamePattern(node.pattern, ctx, AstOf(CtAstKind::Expr, expr));
          RenameExpr(node.range, ctx);
          CtAst expr_ast = AstOf(CtAstKind::Expr, expr);
          RenameBlock(*node.body, ctx, expr_ast);
        }
      },
      expr->node);
}

void RenameStmt(Stmt& stmt, HygieneContext& ctx, const CtAst& ast) {
  if (!ctx.ok) {
    return;
  }
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          RenameType(node.binding.type_opt, ctx);
          RenameExpr(node.binding.init, ctx);
          RenamePattern(node.binding.pat, ctx, ast);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          BindPatternName(node.alias, node.alias_splice_opt.has_value(), ctx, ast);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          RenameExpr(node.place, ctx);
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          RenameExpr(node.place, ctx);
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          RenameExpr(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::ComptimeStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          RenameBlock(*node.body, ctx, ast);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          RenameExpr(node.opts_opt, ctx);
          PushScope(ctx);
          if (node.alias_opt.has_value()) {
            BindPatternName(*node.alias_opt, node.alias_splice_opt.has_value(), ctx,
                            ast);
          }
          RenameBlock(*node.body, ctx, ast);
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          RenameLocalRef(node.target_opt, ctx);
          RenameBlock(*node.body, ctx, ast);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          RenameExpr(node.value_opt, ctx);
        }
      },
      stmt);
}

void RenameBlock(Block& block, HygieneContext& ctx, const CtAst& ast) {
  if (!ctx.ok) {
    return;
  }
  PushScope(ctx);
  for (auto& stmt : block.stmts) {
    RenameStmt(stmt, ctx, ast);
  }
  RenameExpr(block.tail_opt, ctx);
  PopScope(ctx);
}

void RenameContract(std::optional<ast::ContractClause>& contract,
                    HygieneContext& ctx) {
  if (!contract.has_value() || !ctx.ok) {
    return;
  }
  RenameExpr(contract->precondition, ctx);
  RenameExpr(contract->postcondition, ctx);
}

void RenameGenericParams(std::optional<ast::GenericParams>& params_opt,
                         HygieneContext& ctx,
                         const CtAst& ast) {
  if (!params_opt.has_value() || !ctx.ok) {
    return;
  }
  for (auto& param : params_opt->params) {
    RenameType(param.default_type, ctx);
    BindPatternName(param.name, false, ctx, ast);
  }
}

void RenameWhereClause(std::optional<ast::PredicateClause>& clause_opt,
                       HygieneContext& ctx) {
  if (!clause_opt.has_value() || !ctx.ok) {
    return;
  }
  for (auto& predicate : *clause_opt) {
    RenameType(predicate.type, ctx);
  }
}

void RenameTypeInvariant(std::optional<ast::TypeInvariant>& invariant_opt,
                         HygieneContext& ctx) {
  if (!invariant_opt.has_value() || !ctx.ok) {
    return;
  }
  RenameExpr(invariant_opt->predicate, ctx);
}

void BindParams(std::vector<ast::Param>& params,
                HygieneContext& ctx,
                const CtAst& ast) {
  for (auto& param : params) {
    RenameType(param.type, ctx);
  }
  for (auto& param : params) {
    BindPatternName(param.name, param.name_splice_opt.has_value(), ctx, ast);
  }
}

void RenameMethodLike(std::optional<ast::GenericParams>& generic_params,
                      std::optional<ast::PredicateClause>* where_clause_opt,
                      std::vector<ast::Param>& params,
                      TypePtr& return_type_opt,
                      std::optional<ast::ContractClause>& contract,
                      BlockPtr* body_opt,
                      HygieneContext& ctx,
                      const CtAst& ast) {
  PushScope(ctx);
  RenameGenericParams(generic_params, ctx, ast);
  if (where_clause_opt) {
    RenameWhereClause(*where_clause_opt, ctx);
  }
  RenameType(return_type_opt, ctx);
  BindParams(params, ctx, ast);
  RenameContract(contract, ctx);
  if (body_opt && *body_opt) {
    RenameBlock(**body_opt, ctx, ast);
  }
  PopScope(ctx);
}

void RenameItem(ASTItem& item, HygieneContext& ctx, const CtAst& ast) {
  if (!ctx.ok) {
    return;
  }
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          RenameUsingAliases(node.clause, ctx, ast);
        } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
          if (node.alias_opt.has_value()) {
            BindPreservedName(ctx, *node.alias_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                      std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          BindPreservedName(ctx, node.name);
          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            RenameMethodLike(node.generic_params, &node.predicate_clause_opt,
                             node.params, node.return_type_opt,
                             node.contract, &node.body, ctx, ast);
          } else {
            RenameMethodLike(node.generic_params, nullptr,
                             node.params, node.return_type_opt,
                             node.contract, &node.body, ctx, ast);
          }
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (auto& ext : node.items) {
            std::visit(
                [&](auto& ext_item) {
                  using E = std::decay_t<decltype(ext_item)>;
                  if constexpr (std::is_same_v<E, ast::ExternProcDecl>) {
                    BindPreservedName(ctx, ext_item.name);
                    RenameMethodLike(ext_item.generic_params,
                                     &ext_item.where_clause,
                                     ext_item.params,
                                     ext_item.return_type_opt,
                                     ext_item.contract,
                                     nullptr, ctx, ast);
                  }
                },
                ext);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          BindPreservedName(ctx, node.name);
          PushScope(ctx);
          RenameGenericParams(node.generic_params, ctx, ast);
          RenameWhereClause(node.predicate_clause_opt, ctx);
          RenameTypeInvariant(node.invariant_opt, ctx);
          for (auto& member : node.members) {
            std::visit(
                [&](auto& rec_member) {
                  using M = std::decay_t<decltype(rec_member)>;
                  if constexpr (std::is_same_v<M, ast::FieldDecl>) {
                    RenameType(rec_member.type, ctx);
                    RenameExpr(rec_member.init_opt, ctx);
                  } else if constexpr (std::is_same_v<M, ast::MethodDecl>) {
                    std::optional<ast::GenericParams> no_generic_params;
                    RenameMethodLike(no_generic_params, nullptr,
                                     rec_member.params, rec_member.return_type_opt,
                                     rec_member.contract, &rec_member.body, ctx, ast);
                  } else if constexpr (std::is_same_v<M, ast::AssociatedTypeDecl>) {
                    RenameType(rec_member.default_type, ctx);
                  }
                },
                member);
          }
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          BindPreservedName(ctx, node.name);
          PushScope(ctx);
          RenameGenericParams(node.generic_params, ctx, ast);
          RenameWhereClause(node.predicate_clause_opt, ctx);
          RenameTypeInvariant(node.invariant_opt, ctx);
          for (auto& variant : node.variants) {
            if (variant.payload_opt.has_value()) {
              std::visit(
                  [&](auto& payload) {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
                      for (auto& elem : payload.elements) {
                        RenameType(elem, ctx);
                      }
                    } else {
                      for (auto& field : payload.fields) {
                        RenameType(field.type, ctx);
                      }
                    }
                  },
                  *variant.payload_opt);
            }
          }
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          BindPreservedName(ctx, node.name);
          PushScope(ctx);
          RenameGenericParams(node.generic_params, ctx, ast);
          RenameWhereClause(node.predicate_clause_opt, ctx);
          RenameTypeInvariant(node.invariant_opt, ctx);
          for (auto& state : node.states) {
            for (auto& member : state.members) {
              std::visit(
                  [&](auto& state_member) {
                    using M = std::decay_t<decltype(state_member)>;
                    if constexpr (std::is_same_v<M, ast::StateFieldDecl>) {
                      RenameType(state_member.type, ctx);
                    } else if constexpr (std::is_same_v<M, ast::StateMethodDecl>) {
                      RenameMethodLike(state_member.generic_params, nullptr,
                                       state_member.params,
                                       state_member.return_type_opt,
                                       state_member.contract,
                                       &state_member.body, ctx, ast);
                    } else if constexpr (std::is_same_v<M, ast::TransitionDecl>) {
                      PushScope(ctx);
                      BindParams(state_member.params, ctx, ast);
                      RenameBlock(*state_member.body, ctx, ast);
                      PopScope(ctx);
                    }
                  },
                  member);
            }
          }
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          BindPreservedName(ctx, node.name);
          PushScope(ctx);
          RenameGenericParams(node.generic_params, ctx, ast);
          RenameWhereClause(node.predicate_clause_opt, ctx);
          for (auto& class_item : node.items) {
            std::visit(
                [&](auto& entry) {
                  using M = std::decay_t<decltype(entry)>;
                  if constexpr (std::is_same_v<M, ast::ClassFieldDecl> ||
                                std::is_same_v<M, ast::AbstractFieldDecl>) {
                    RenameType(entry.type, ctx);
                  } else if constexpr (std::is_same_v<M, ast::ClassMethodDecl>) {
                    RenameMethodLike(entry.generic_params, nullptr,
                                     entry.params, entry.return_type_opt,
                                     entry.contract, &entry.body_opt, ctx, ast);
                  } else if constexpr (std::is_same_v<M, ast::AssociatedTypeDecl>) {
                    RenameType(entry.default_type, ctx);
                  } else if constexpr (std::is_same_v<M, ast::AbstractStateDecl>) {
                    for (auto& field : entry.fields) {
                      RenameType(field.type, ctx);
                    }
                  }
                },
                class_item);
          }
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          RenameType(node.binding.type_opt, ctx);
          RenameExpr(node.binding.init, ctx);
          PushScope(ctx);
          RenamePattern(node.binding.pat, ctx, ast);
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          BindPreservedName(ctx, node.name);
          PushScope(ctx);
          RenameGenericParams(node.generic_params, ctx, ast);
          RenameWhereClause(node.predicate_clause_opt, ctx);
          RenameType(node.type, ctx);
          PopScope(ctx);
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          BindPreservedName(ctx, node.name);
          RenameBlock(*node.body, ctx, ast);
        }
      },
      item);
}

}  // namespace

std::optional<CtAst> HygienizeAst(const CtAst& ast,
                                  const CtSite& quote_site,
                                  const CtSite& emit_site,
                                  std::size_t seed,
                                  std::size_t& next_seed,
                                  CtEnv& env) {
  HygieneContext ctx{env, quote_site, emit_site, seed};
  switch (ast.kind) {
    case CtAstKind::Expr:
      if (const auto* expr = std::get_if<ExprPtr>(&ast.payload)) {
        CollectExprNames(*expr, ctx);
      }
      break;
    case CtAstKind::Stmt:
      if (const auto* stmt = std::get_if<Stmt>(&ast.payload)) {
        CollectStmtNames(*stmt, ctx);
      }
      break;
    case CtAstKind::Item:
      if (const auto* item = std::get_if<ASTItem>(&ast.payload)) {
        CollectItemNames(*item, ctx);
      }
      break;
    case CtAstKind::Type:
      if (const auto* type = std::get_if<TypePtr>(&ast.payload)) {
        CollectTypeNames(*type, ctx);
      }
      break;
    case CtAstKind::Pattern:
      if (const auto* pattern = std::get_if<PatternPtr>(&ast.payload)) {
        CollectPatternNames(*pattern, ctx);
      }
      break;
  }

  CtAst out = ast;
  out.hygiene = CtHygiene{quote_site, emit_site, seed};

  PushScope(ctx);
  switch (out.kind) {
    case CtAstKind::Expr:
      if (auto* expr = std::get_if<ExprPtr>(&out.payload)) {
        RenameExpr(*expr, ctx);
      }
      break;
    case CtAstKind::Stmt:
      if (auto* stmt = std::get_if<Stmt>(&out.payload)) {
        RenameStmt(*stmt, ctx, out);
      }
      break;
    case CtAstKind::Item:
      if (auto* item = std::get_if<ASTItem>(&out.payload)) {
        RenameItem(*item, ctx, out);
      }
      break;
    case CtAstKind::Type:
      if (auto* type = std::get_if<TypePtr>(&out.payload)) {
        RenameType(*type, ctx);
      }
      break;
    case CtAstKind::Pattern:
      if (auto* pattern = std::get_if<PatternPtr>(&out.payload)) {
        RenamePattern(*pattern, ctx, out);
      }
      break;
  }
  PopScope(ctx);

  next_seed = ctx.next_seed;
  if (!ctx.ok) {
    return std::nullopt;
  }
  return out;
}

std::optional<CtAst> PrepareAstForInsertion(const CtAst& ast,
                                            const CtSite& emit_site,
                                            CtEnv& env) {
  const CtSite quote_site =
      ast.hygiene.has_value() ? ast.hygiene->quote_site : CtSiteOf(env);
  std::size_t next_seed = CtFreshSeed(env);
  auto hygienized =
      HygienizeAst(ast, quote_site, emit_site, next_seed, next_seed, env);
  env.next_hygiene = next_seed;
  return hygienized;
}

}  // namespace cursive::frontend::comptime_internal
