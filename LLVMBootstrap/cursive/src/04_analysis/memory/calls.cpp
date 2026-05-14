/*
 * =============================================================================
 * MIGRATION MAPPING: calls.cpp
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 6.8 "Procedure Calls" (lines 16500-16700)
 *   - CursiveSpecification.md, Section 6.8.1 "Argument Passing" (lines 16510-16600)
 *   - CursiveSpecification.md, Section 6.8.2 "Move Parameters" (lines 16610-16700)
 *   - CursiveSpecification.md, Section 10.3 "Move Semantics" (lines 22310-22400)
 *
 * SOURCE FILE:
 *   - cursive-bootstrap/src/03_analysis/memory/calls.cpp
 *
 * FUNCTIONS MIGRATED:
 *   - TypeCall() - Type check a procedure call
 *   - TypeCallWithSubst() - Type check a generic procedure call with substitution
 *   - IsRecordCallee() - Check if callee is a record constructor
 *
 * DEPENDENCIES:
 *   - Call, MethodCall AST nodes
 *   - Parameter, Argument matching
 *   - Permission system
 *   - Move semantics
 *
 * REFACTORING NOTES:
 *   1. Non-move parameters: caller retains ownership
 *   2. Move parameters: callee takes ownership (explicit `move` required for
 *      provenance-bearing source places; non-place temporaries may be consumed
 *      without explicit `move`)
 *   3. Receiver shorthands: ~ (const), ~! (unique), ~% (shared)
 *   4. Argument evaluation order is left-to-right
 *   5. Non-consuming permission admissibility is a use-site check; it does
 *      not create a weaker alias or general permission subtype
 *   6. After move argument, binding is Moved state
 *   7. Consider tracking which arguments were moved for error recovery
 *
 * CALL SEMANTICS:
 *   - procedure foo(x: T) -> ()           // x borrowed, caller keeps
 *   - procedure bar(move x: T) -> ()      // x consumed, caller loses
 *   - procedure baz(~) -> ()              // receiver borrowed (const)
 *   - procedure qux(~!) -> ()             // receiver borrowed (unique)
 *   - procedure quux(~%) -> ()            // receiver borrowed (shared)
 *
 * DIAGNOSTIC CODES:
 *   - E-CALL-0001: Argument type mismatch
 *   - E-CALL-0002: Permission insufficient
 *   - E-CALL-0003: Move required for consuming parameter
 *   - E-CALL-0004: Receiver permission mismatch
 *   - E-CALL-0005: Cannot move borrowed value
 *
 * =============================================================================
 */

#include "04_analysis/memory/calls.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "02_source/lexer/token.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsCalls() {
  SPEC_DEF("ArgsOkTJudg", "5.2.4");
  SPEC_DEF("ParamMode", "5.2.4");
  SPEC_DEF("ParamType", "5.2.4");
  SPEC_DEF("ArgMoved", "5.2.4");
  SPEC_DEF("ArgExpr", "5.2.4");
  SPEC_DEF("ArgType", "5.2.4");
  SPEC_DEF("MovedArg", "3.3.2.4");
  SPEC_DEF("IsPlace", "3.3.3");
  SPEC_DEF("Call-Arg-Packed-Unsafe-Err", "5.2.4");
  SPEC_DEF("RecordCallee", "5.2.8");
}

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

TypeRef StripPermAndRefine(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

template <typename TResult>
bool IsExpectedTypeMismatch(const TResult& check) {
  return !check.diag_id.has_value() || *check.diag_id == "E-SEM-2526";
}

const ast::TypeAliasDecl* LookupTypeAliasDecl(const ScopeContext& ctx,
                                              const TypePath& path) {
  if (path.empty()) {
    return nullptr;
  }
  if (path.size() > 1) {
    ast::Path full;
    full.reserve(path.size());
    for (const auto& seg : path) {
      full.push_back(seg);
    }
    const auto it = ctx.sigma.types.find(PathKeyOf(full));
    if (it == ctx.sigma.types.end()) {
      return nullptr;
    }
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  const auto ent = ResolveTypeName(ctx, path[0]);
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return nullptr;
  }

  ast::Path resolved = *ent->origin_opt;
  const std::string resolved_name =
      ent->target_opt.has_value() ? *ent->target_opt : path[0];
  resolved.push_back(resolved_name);
  const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (resolved_it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(&resolved_it->second);
}

AliasExpandResult ExpandTypeAliasApply(const ScopeContext& ctx,
                                       const TypePathType& applied) {
  AliasExpandResult result;
  const auto* alias = LookupTypeAliasDecl(ctx, applied.path);
  if (!alias) {
    return result;
  }

  const auto lowered = LowerType(ctx, alias->type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }

  if (!alias->generic_params.has_value()) {
    if (!applied.generic_args.empty()) {
      return result;
    }
    result.type = lowered.type;
    result.expanded = true;
    return result;
  }

  const auto& params = alias->generic_params->params;
  if (applied.generic_args.size() > params.size()) {
    return result;
  }

  const auto subst = BuildSubstitution(params, applied.generic_args);
  result.type = InstantiateType(lowered.type, subst);
  result.expanded = result.type != nullptr;
  return result;
}

AliasExpandResult NormalizeCalleeType(const ScopeContext& ctx,
                                      const TypeRef& type) {
  AliasExpandResult out;
  out.type = StripPermAndRefine(type);
  for (int i = 0; i < 16; ++i) {
    if (!out.type) {
      return out;
    }
    const auto* path = std::get_if<TypePathType>(&out.type->node);
    if (!path) {
      return out;
    }
    const auto expanded = ExpandTypeAliasApply(ctx, *path);
    if (!expanded.ok) {
      out.ok = false;
      out.diag_id = expanded.diag_id;
      return out;
    }
    if (!expanded.expanded) {
      return out;
    }
    out.type = StripPermAndRefine(expanded.type);
    out.expanded = true;
  }
  return out;
}

struct CallLookupIndex {
  const void* sigma_key = nullptr;
  const void* module_storage_key = nullptr;
  std::size_t module_count = 0;
  PathKey current_module_key;
  std::map<PathKey, const ast::ASTModule*> modules_by_path;
  std::unordered_map<const ast::ASTModule*,
                     std::unordered_map<IdKey, const ast::ProcedureDecl*>>
      procedures_by_module;
  std::unordered_map<const ast::ASTModule*,
                     std::unordered_map<IdKey, const ast::ExternProcDecl*>>
      externs_by_module;
};

const CallLookupIndex& GetCallLookupIndex(const ScopeContext& ctx) {
  thread_local CallLookupIndex index;
  const Sigma* sigma_ptr = ctx.sigma_source ? ctx.sigma_source : &ctx.sigma;
  const void* sigma_key = static_cast<const void*>(sigma_ptr);
  const void* module_storage_key =
      sigma_ptr->mods.empty() ? nullptr : static_cast<const void*>(sigma_ptr->mods.data());
  const PathKey current_module_key = PathKeyOf(ctx.current_module);
  if (index.sigma_key == sigma_key &&
      index.module_storage_key == module_storage_key &&
      index.module_count == sigma_ptr->mods.size() &&
      index.current_module_key == current_module_key) {
    return index;
  }

  index = {};
  index.sigma_key = sigma_key;
  index.module_storage_key = module_storage_key;
  index.module_count = sigma_ptr->mods.size();
  index.current_module_key = current_module_key;
  index.procedures_by_module.reserve(sigma_ptr->mods.size());
  index.externs_by_module.reserve(sigma_ptr->mods.size());

  for (const auto& mod : sigma_ptr->mods) {
    const auto* mod_ptr = &mod;
    index.modules_by_path.emplace(PathKeyOf(mod.path), mod_ptr);

    auto& proc_map = index.procedures_by_module[mod_ptr];
    auto& ext_map = index.externs_by_module[mod_ptr];
    for (const auto& item : mod.items) {
      if (const auto* proc = std::get_if<ast::ProcedureDecl>(&item)) {
        proc_map.emplace(IdKeyOf(proc->name), proc);
        continue;
      }
      if (const auto* block = std::get_if<ast::ExternBlock>(&item)) {
        for (const auto& ext_item : block->items) {
          if (const auto* ext_proc = std::get_if<ast::ExternProcDecl>(&ext_item)) {
            ext_map.emplace(IdKeyOf(ext_proc->name), ext_proc);
          }
        }
      }
    }
  }
  return index;
}

const ast::ASTModule* FindModuleByPathForCall(const ScopeContext& ctx,
                                              const ast::ModulePath& path) {
  const auto& index = GetCallLookupIndex(ctx);
  const auto it = index.modules_by_path.find(PathKeyOf(path));
  if (it != index.modules_by_path.end()) {
    return it->second;
  }
  return nullptr;
}

const ast::ProcedureDecl* FindProcedureInModule(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    std::string_view name) {
  const auto& index = GetCallLookupIndex(ctx);
  const auto mod_it = index.procedures_by_module.find(&module);
  if (mod_it == index.procedures_by_module.end()) {
    return nullptr;
  }
  const auto proc_it = mod_it->second.find(IdKeyOf(name));
  if (proc_it != mod_it->second.end()) {
    return proc_it->second;
  }
  return nullptr;
}

const ast::ExternProcDecl* FindExternProcedureInModule(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    std::string_view name) {
  const auto& index = GetCallLookupIndex(ctx);
  const auto mod_it = index.externs_by_module.find(&module);
  if (mod_it == index.externs_by_module.end()) {
    return nullptr;
  }
  const auto ext_it = mod_it->second.find(IdKeyOf(name));
  if (ext_it != mod_it->second.end()) {
    return ext_it->second;
  }
  return nullptr;
}

bool HasExternProcedureInModule(const ScopeContext& ctx,
                                const ast::ASTModule& module,
                                const std::vector<std::string>& candidate_names) {
  for (const auto& candidate : candidate_names) {
    if (FindExternProcedureInModule(ctx, module, candidate) != nullptr) {
      return true;
    }
  }
  return false;
}

bool HasProcedureInModule(const ScopeContext& ctx,
                          const ast::ASTModule& module,
                          const std::vector<std::string>& candidate_names) {
  for (const auto& candidate : candidate_names) {
    if (FindProcedureInModule(ctx, module, candidate) != nullptr) {
      return true;
    }
  }
  return false;
}

bool IsExternCallee(const ScopeContext& ctx, const ast::ExprPtr& callee) {
  if (!callee) {
    return false;
  }

  std::optional<ast::ModulePath> module_path;
  std::vector<std::string> candidate_names;

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    candidate_names.push_back(std::string(ident->name));
    const auto ent = ResolveValueName(ctx, ident->name);
    if (ent && ent->origin_opt.has_value()) {
      module_path = *ent->origin_opt;
      if (ent->target_opt.has_value() &&
          !IdEq(*ent->target_opt, ident->name)) {
        candidate_names.push_back(*ent->target_opt);
      }
    } else {
      module_path = ctx.current_module;
    }
  } else if (const auto* qualified =
                 std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    module_path = qualified->path;
    candidate_names.push_back(qualified->name);
  } else if (const auto* qualified_apply =
                 std::get_if<ast::QualifiedApplyExpr>(&callee->node)) {
    module_path = qualified_apply->path;
    candidate_names.push_back(qualified_apply->name);
  } else if (const auto* path_expr = std::get_if<ast::PathExpr>(&callee->node)) {
    module_path = path_expr->path.empty() ? ctx.current_module : path_expr->path;
    candidate_names.push_back(path_expr->name);
  } else {
    return false;
  }

  if (candidate_names.empty()) {
    return false;
  }

  if (!module_path.has_value()) {
    const auto* current = FindModuleByPathForCall(ctx, ctx.current_module);
    if (current != nullptr) {
      if (HasExternProcedureInModule(ctx, *current, candidate_names)) {
        return true;
      }
      if (HasProcedureInModule(ctx, *current, candidate_names)) {
        return false;
      }
    }
    for (const auto& mod : ctx.sigma.mods) {
      if (HasExternProcedureInModule(ctx, mod, candidate_names)) {
        return true;
      }
    }
    return false;
  }

  const auto* module = FindModuleByPathForCall(ctx, *module_path);
  if (module != nullptr) {
    if (HasExternProcedureInModule(ctx, *module, candidate_names)) {
      return true;
    }
    if (HasProcedureInModule(ctx, *module, candidate_names)) {
      return false;
    }
  }

  for (const auto& mod : ctx.sigma.mods) {
    if (HasExternProcedureInModule(ctx, mod, candidate_names)) {
      return true;
    }
  }

  return false;
}

ast::ExprPtr MakeExpr(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

bool IsPlaceExprForCallLocal(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (std::holds_alternative<ast::IdentifierExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<ast::FieldAccessExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<ast::TupleAccessExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<ast::IndexAccessExpr>(expr->node)) {
    return true;
  }
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    return IsPlaceExprForCallLocal(deref->value);
  }
  return false;
}

bool BlockHasSourceProvenance(const std::shared_ptr<ast::Block>& block);
bool StmtHasSourceProvenance(const ast::Stmt& stmt);
bool HasSourceProvenanceLocal(const ast::ExprPtr& expr);

bool ExprListHasSourceProvenance(const std::vector<ast::ExprPtr>& exprs) {
  for (const auto& expr : exprs) {
    if (HasSourceProvenanceLocal(expr)) {
      return true;
    }
  }
  return false;
}

bool ArraySegmentsHaveSourceProvenance(
    const std::vector<ast::ArraySegment>& segments) {
  bool has_source_provenance = false;
  for (const auto& segment : segments) {
    std::visit(
        [&](const auto& seg) {
          using S = std::decay_t<decltype(seg)>;
          if constexpr (std::is_same_v<S, ast::ArrayElemSegment>) {
            if (!has_source_provenance &&
                HasSourceProvenanceLocal(seg.value)) {
              has_source_provenance = true;
            }
          } else if constexpr (std::is_same_v<S, ast::ArrayRepeatSegment>) {
            if (!has_source_provenance &&
                (HasSourceProvenanceLocal(seg.value) ||
                 HasSourceProvenanceLocal(seg.count))) {
              has_source_provenance = true;
            }
          }
        },
        segment);
    if (has_source_provenance) {
      return true;
    }
  }
  return false;
}

bool BlockHasSourceProvenance(const std::shared_ptr<ast::Block>& block) {
  if (!block) {
    return false;
  }
  // Call-argument source provenance is about whether the block result itself
  // aliases caller-visible source storage. Intermediate statements may touch
  // source-backed values without making the block result source-provenant.
  return HasSourceProvenanceLocal(block->tail_opt);
}

static bool IsDirectSymbolCalleeLocal(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::holds_alternative<ast::IdentifierExpr>(expr->node) ||
         std::holds_alternative<ast::PathExpr>(expr->node) ||
         std::holds_alternative<ast::QualifiedNameExpr>(expr->node);
}

bool StmtHasSourceProvenance(const ast::Stmt& stmt) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          return HasSourceProvenanceLocal(node.binding.init);
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          return HasSourceProvenanceLocal(node.binding.init);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          return HasSourceProvenanceLocal(node.place) || HasSourceProvenanceLocal(node.value);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          return HasSourceProvenanceLocal(node.place) || HasSourceProvenanceLocal(node.value);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return HasSourceProvenanceLocal(node.value);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          return BlockHasSourceProvenance(node.body);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return HasSourceProvenanceLocal(node.opts_opt) ||
                 BlockHasSourceProvenance(node.body);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return BlockHasSourceProvenance(node.body);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          return HasSourceProvenanceLocal(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          return HasSourceProvenanceLocal(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          return BlockHasSourceProvenance(node.body);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          return BlockHasSourceProvenance(node.body);
        } else {
          return false;
        }
      },
      stmt);
}

bool HasSourceProvenanceLocal(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (IsPlaceExprForCallLocal(expr)) {
    return true;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ErrorExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::PtrNullExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          if (!node.else_expr) {
            return false;
          }
          return HasSourceProvenanceLocal(node.then_expr) &&
                 HasSourceProvenanceLocal(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          for (const auto& arm : node.cases) {
            if (!HasSourceProvenanceLocal(arm.body)) {
              return false;
            }
          }
          return HasSourceProvenanceLocal(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return HasSourceProvenanceLocal(node.then_expr) &&
                 HasSourceProvenanceLocal(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return BlockHasSourceProvenance(node.block);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return BlockHasSourceProvenance(node.block);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return HasSourceProvenanceLocal(node.expr);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          return false;
        } else {
          return false;
        }
      },
      expr->node);
}

bool MissingRequiredMoveForConsumingLocal(const std::optional<ParamMode>& mode,
                                     const ast::Arg& arg) {
  return mode == ParamMode::Move && !arg.moved && HasSourceProvenanceLocal(arg.value);
}

bool UsesCallTempForConsumingLocal(const std::optional<ParamMode>& mode,
                                   const ast::Arg& arg) {
  return mode == ParamMode::Move && !arg.moved &&
         !HasSourceProvenanceLocal(arg.value);
}

ast::ExprPtr MovedArgExprLocal(const ast::Arg& arg) {
  if (!arg.moved || !IsPlaceExprForCallLocal(arg.value)) {
    return arg.value;
  }
  core::Span span = arg.span;
  if (!span.file.empty()) {
    return MakeExpr(span, ast::MoveExpr{arg.value});
  }
  if (arg.value) {
    return MakeExpr(arg.value->span, ast::MoveExpr{arg.value});
  }
  return MakeExpr(core::Span{}, ast::MoveExpr{arg.value});
}
TypeRef StripPermLocal(const TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->base;
  }
  return type;
}

struct AddrOfOkResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

bool HasLayoutPacked(const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name != "layout") {
      continue;
    }
    for (const auto& arg : attr.args) {
      if (const auto* tok = std::get_if<lexer::Token>(&arg.value)) {
        if (tok->lexeme == "packed") {
          return true;
        }
      }
    }
  }
  return false;
}

bool IsPackedRecord(const ScopeContext& ctx, const TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  const auto* record = std::get_if<ast::RecordDecl>(&it->second);
  if (!record) {
    return false;
  }
  return HasLayoutPacked(record->attrs);
}

AddrOfOkResult AddrOfOk(const ScopeContext& ctx,
                        const ast::ExprPtr& expr,
                        const ExprTypeFn& type_expr) {
  if (!IsPlaceExprForCallLocal(expr)) {
    return {false, std::nullopt};
  }

  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    const auto base_type = type_expr(field->base);
    if (!base_type.ok) {
      return {false, base_type.diag_id};
    }
    const auto stripped = StripPermLocal(base_type.type);
    if (const auto* path =
            stripped ? std::get_if<TypePathType>(&stripped->node) : nullptr) {
      if (IsPackedRecord(ctx, path->path) &&
          !IsInUnsafeSpan(ctx, expr->span)) {
        return {false, "E-TYP-2105"};
      }
    }
  }

  const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node);
  if (!index) {
    return {true, std::nullopt};
  }
  const auto idx_type = type_expr(index->index);
  if (!idx_type.ok) {
    return {false, idx_type.diag_id};
  }
  const auto idx_stripped = StripPermLocal(idx_type.type);
  if (IsPrimType(idx_stripped, "usize")) {
    return {true, std::nullopt};
  }

  const auto base_type = type_expr(index->base);
  if (!base_type.ok) {
    return {false, base_type.diag_id};
  }
  const auto stripped = StripPermLocal(base_type.type);
  if (stripped) {
    if (std::holds_alternative<TypeArray>(stripped->node)) {
      return {false, "Index-Array-NonUsize"};
    }
    if (std::holds_alternative<TypeSlice>(stripped->node)) {
      return {false, "Index-Slice-NonUsize"};
    }
  }
  return {false, "Index-NonIndexable"};
}

ast::Path FullPath(const ast::ModulePath& path, std::string_view name) {
  ast::Path out = path;
  out.emplace_back(name);
  return out;
}

const ast::RecordDecl* LookupRecordDecl(const ScopeContext& ctx,
                                        const ast::TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::RecordDecl>(&it->second);
}

const ast::RecordDecl* RecordCalleeDecl(const ScopeContext& ctx,
                                        const ast::ExprPtr& callee,
                                        const std::vector<ast::Arg>& args) {
  if (!callee || !args.empty()) {
    return nullptr;
  }
  return std::visit(
      [&](const auto& node) -> const ast::RecordDecl* {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto ent = ResolveTypeName(ctx, node.name);
          if (!ent.has_value() || !ent->origin_opt.has_value()) {
            return nullptr;
          }
          const auto name = ent->target_opt.value_or(node.name);
          const auto full = FullPath(*ent->origin_opt, name);
          return LookupRecordDecl(ctx, full);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          const auto full = FullPath(node.path, node.name);
          return LookupRecordDecl(ctx, full);
        }
        return nullptr;
      },
      callee->node);
}

std::optional<core::Span> ArgDiagnosticSpan(const ast::Arg& arg) {
  if (!arg.span.file.empty()) {
    return arg.span;
  }
  if (arg.value) {
    return arg.value->span;
  }
  return std::nullopt;
}

static const ast::TypeAliasDecl* LookupTypeAliasDeclForFunctionValue(
    const ScopeContext& ctx,
    const TypePath& path) {
  if (path.empty()) {
    return nullptr;
  }
  if (path.size() > 1) {
    const auto it = ctx.sigma.types.find(PathKeyOf(path));
    if (it == ctx.sigma.types.end()) {
      return nullptr;
    }
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  const auto ent = ResolveTypeName(ctx, path[0]);
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return nullptr;
  }
  ast::Path resolved = *ent->origin_opt;
  resolved.push_back(ent->target_opt.value_or(path[0]));
  const auto it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(&it->second);
}

static bool IsFunctionValueType(const ScopeContext& ctx,
                                const TypeRef& type,
                                std::uint32_t depth = 0) {
  if (depth > 32) {
    return false;
  }
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  if (std::holds_alternative<TypeFunc>(stripped->node)) {
    return true;
  }
  const auto* path_type = std::get_if<TypePathType>(&stripped->node);
  if (!path_type || !path_type->generic_args.empty()) {
    return false;
  }
  const auto* alias =
      LookupTypeAliasDeclForFunctionValue(ctx, path_type->path);
  if (!alias || alias->generic_params.has_value()) {
    return false;
  }
  const auto lowered = LowerType(ctx, alias->type);
  return lowered.ok && IsFunctionValueType(ctx, lowered.type, depth + 1);
}

}  // namespace

bool IsPlaceExprForCall(const ast::ExprPtr& expr) {
  return IsPlaceExprForCallLocal(expr);
}

bool HasSourceProvenance(const ast::ExprPtr& expr) {
  return HasSourceProvenanceLocal(expr);
}

bool MissingRequiredMoveForConsuming(const std::optional<ParamMode>& mode,
                                     const ast::Arg& arg) {
  return MissingRequiredMoveForConsumingLocal(mode, arg);
}

bool UsesCallTempForConsuming(const std::optional<ParamMode>& mode,
                              const ast::Arg& arg) {
  return UsesCallTempForConsumingLocal(mode, arg);
}

ast::ExprPtr MovedArgExpr(const ast::Arg& arg) {
  return MovedArgExprLocal(arg);
}

bool IsRecordCallee(const ScopeContext& ctx,
                    const ast::ExprPtr& callee,
                    const std::vector<ast::Arg>& args) {
  SpecDefsCalls();
  return RecordCalleeDecl(ctx, callee, args) != nullptr;
}

CallTypeResult TypeCall(const ScopeContext& ctx,
                        const ast::ExprPtr& callee,
                        const std::vector<ast::Arg>& args,
                        const ExprTypeFn& type_expr,
                        const PlaceTypeFn* type_place,
                        const ArgCheckFn* check_expr) {
  SpecDefsCalls();
  CallTypeResult result;
  if (!callee) {
    return result;
  }
  if (IsExternCallee(ctx, callee) &&
      !IsInUnsafeSpan(ctx, callee ? callee->span : core::Span{})) {
    SPEC_RULE("Call-Extern-Unsafe-Err");
    result.diag_id = "Call-Extern-Unsafe-Err";
    return result;
  }
  const auto callee_type = type_expr(callee);
  if (!callee_type.ok) {
    result.diag_id = callee_type.diag_id;
    result.diag_span = callee_type.diag_span;
    return result;
  }

  const auto normalized_callee = NormalizeCalleeType(ctx, callee_type.type);
  if (!normalized_callee.ok) {
    result.diag_id = normalized_callee.diag_id;
    return result;
  }

  const auto* func =
      normalized_callee.type ? std::get_if<TypeFunc>(&normalized_callee.type->node) : nullptr;
  const auto* closure =
      normalized_callee.type ? std::get_if<TypeClosure>(&normalized_callee.type->node) : nullptr;
  if (!func && !closure) {
    if (RecordCalleeDecl(ctx, callee, args)) {
      result.record_callee = true;
      return result;
    }
    SPEC_RULE("Call-Callee-NotFunc");
    result.diag_id = "E-SEM-2531";
    return result;
  }

  std::vector<TypeFuncParam> closure_params;
  const std::vector<TypeFuncParam>* params_ptr = nullptr;
  TypeRef ret_type = nullptr;
  if (func) {
    params_ptr = &func->params;
    ret_type = func->ret;
  } else {
    closure_params.reserve(closure->params.size());
    for (const auto& entry : closure->params) {
      TypeFuncParam param;
      param.mode = entry.first ? std::optional<ParamMode>(ParamMode::Move) : std::nullopt;
      param.type = entry.second;
      closure_params.push_back(param);
    }
    params_ptr = &closure_params;
    ret_type = closure->ret;
  }

  const auto& params = *params_ptr;
  if (params.size() != args.size()) {
    SPEC_RULE("Call-ArgCount-Err");
    result.diag_id = "E-SEM-2532";
    result.diag_detail = "expected " + std::to_string(params.size()) +
                         " args, found " + std::to_string(args.size());
    return result;
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (MissingRequiredMoveForConsumingLocal(params[i].mode, args[i])) {
      SPEC_RULE("Call-Move-Missing");
      result.diag_id = "E-SEM-2534";
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (!params[i].mode.has_value() && args[i].moved) {
      SPEC_RULE("Call-Move-Unexpected");
      result.diag_id = "E-SEM-2535";
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  std::vector<TypeRef> arg_types;
  arg_types.reserve(args.size());
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (!params[i].mode.has_value()) {
      const bool has_source_prov = HasSourceProvenanceLocal(arg.value);
      const bool expected_function_value =
          IsFunctionValueType(ctx, params[i].type);
      if (has_source_prov && !expected_function_value &&
          !IsPlaceExprForCallLocal(arg.value)) {
        SPEC_RULE("Call-Arg-NotPlace");
        result.diag_id = "E-TYP-1603";
        result.diag_span = ArgDiagnosticSpan(arg);
        return result;
      }
      if (has_source_prov && !expected_function_value && type_place) {
        const auto place_type = (*type_place)(arg.value);
        if (!place_type.ok) {
          result.diag_id = place_type.diag_id;
          result.diag_span = ArgDiagnosticSpan(arg);
          return result;
        }
        arg_types.push_back(place_type.type);
      } else {
        if ((!has_source_prov || expected_function_value) && check_expr) {
          const auto checked = (*check_expr)(arg.value, params[i].type);
          if (checked.ok) {
            arg_types.push_back(params[i].type);
            continue;
          }
          if (IsExpectedTypeMismatch(checked)) {
            SPEC_RULE("Call-ArgType-Err");
            result.diag_id = "E-SEM-2533";
            result.diag_span = ArgDiagnosticSpan(arg);
            return result;
          }
          result.diag_id = checked.diag_id;
          result.diag_span = ArgDiagnosticSpan(arg);
          return result;
        }
        const auto arg_type = type_expr(arg.value);
        if (!arg_type.ok) {
          result.diag_id = arg_type.diag_id;
          result.diag_span = arg_type.diag_span.has_value()
                                 ? arg_type.diag_span
                                 : ArgDiagnosticSpan(arg);
          return result;
        }
        arg_types.push_back(arg_type.type);
      }
      continue;
    }
    const auto arg_expr = MovedArgExprLocal(arg);
    if (UsesCallTempForConsumingLocal(params[i].mode, arg) && check_expr) {
      const auto checked = (*check_expr)(arg_expr, params[i].type);
      if (checked.ok) {
        arg_types.push_back(params[i].type);
        continue;
      }
      if (IsExpectedTypeMismatch(checked)) {
        SPEC_RULE("Call-ArgType-Err");
        result.diag_id = "E-SEM-2533";
        result.diag_span = ArgDiagnosticSpan(arg);
        return result;
      }
      result.diag_id = checked.diag_id;
      result.diag_span = ArgDiagnosticSpan(arg);
      return result;
    }
    const auto arg_type = type_expr(arg_expr);
    if (!arg_type.ok) {
      result.diag_id = arg_type.diag_id;
      result.diag_span = arg_type.diag_span.has_value()
                             ? arg_type.diag_span
                             : ArgDiagnosticSpan(arg);
      return result;
    }
    arg_types.push_back(arg_type.type);
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto sub =
        ArgumentTypeCompatible(ctx, arg_types[i], params[i].type,
                               params[i].mode);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
    if (!sub.subtype) {
      SPEC_RULE("Call-ArgType-Err");
      result.diag_id = "E-SEM-2533";
      result.diag_detail = "expected type " + TypeToString(params[i].type) +
                           ", found " + TypeToString(arg_types[i]);
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (!params[i].mode.has_value() &&
        !IsFunctionValueType(ctx, params[i].type) &&
        HasSourceProvenanceLocal(args[i].value) &&
        !IsPlaceExprForCallLocal(args[i].value)) {
      SPEC_RULE("Call-Arg-NotPlace");
      result.diag_id = "E-TYP-1603";
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  if (params.empty()) {
    SPEC_RULE("ArgsT-Empty");
  } else {
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (params[i].mode == ParamMode::Move) {
        const auto moved = MovedArgExprLocal(args[i]);
        const auto moved_type = type_expr(moved);
        if (!moved_type.ok) {
          result.diag_id = moved_type.diag_id;
          result.diag_span = moved_type.diag_span.has_value()
                                 ? moved_type.diag_span
                                 : ArgDiagnosticSpan(args[i]);
          return result;
        }
        const auto sub =
            ArgumentTypeCompatible(ctx, moved_type.type, params[i].type,
                                   params[i].mode);
        if (!sub.ok) {
          result.diag_id = sub.diag_id;
          result.diag_span = ArgDiagnosticSpan(args[i]);
          return result;
        }
        if (!sub.subtype) {
          SPEC_RULE("Call-ArgType-Err");
          result.diag_id = "E-SEM-2533";
          result.diag_detail = "expected type " + TypeToString(params[i].type) +
                               ", found " + TypeToString(moved_type.type);
          result.diag_span = ArgDiagnosticSpan(args[i]);
          return result;
        }
        SPEC_RULE("ArgsT-Cons");
        continue;
      }
      if (!IsFunctionValueType(ctx, params[i].type) &&
          HasSourceProvenanceLocal(args[i].value)) {
        const auto addr_ok = AddrOfOk(ctx, args[i].value, type_expr);
        if (!addr_ok.ok) {
          if (addr_ok.diag_id ==
              std::optional<std::string_view>("E-TYP-2105")) {
            SPEC_RULE("Call-Arg-Packed-Unsafe-Err");
          }
          result.diag_id = addr_ok.diag_id;
          result.diag_span = ArgDiagnosticSpan(args[i]);
          return result;
        }
      }
      SPEC_RULE("ArgsT-Cons-Ref");
    }
  }

  SPEC_RULE("T-Call");
  result.ok = true;
  result.type = ret_type;
  return result;
}

// Type check a generic procedure call with type substitution (S13.1.2 T-Generic-Call)
CallTypeResult TypeCallWithSubst(const ScopeContext& ctx,
                                 const ast::ExprPtr& callee,
                                 const std::vector<ast::Arg>& args,
                                 const TypeSubst& subst,
                                 const ExprTypeFn& type_expr,
                                 const PlaceTypeFn* type_place,
                                 const ArgCheckFn* check_expr) {
  SpecDefsCalls();
  SPEC_RULE("T-Generic-Call");
  CallTypeResult result;
  if (!callee) {
    return result;
  }
  if (IsExternCallee(ctx, callee) &&
      !IsInUnsafeSpan(ctx, callee ? callee->span : core::Span{})) {
    SPEC_RULE("Call-Extern-Unsafe-Err");
    result.diag_id = "Call-Extern-Unsafe-Err";
    return result;
  }
  const auto callee_type = type_expr(callee);
  if (!callee_type.ok) {
    result.diag_id = callee_type.diag_id;
    result.diag_span = callee_type.diag_span;
    return result;
  }

  const auto normalized_callee = NormalizeCalleeType(ctx, callee_type.type);
  if (!normalized_callee.ok) {
    result.diag_id = normalized_callee.diag_id;
    return result;
  }

  const auto* func =
      normalized_callee.type ? std::get_if<TypeFunc>(&normalized_callee.type->node) : nullptr;
  if (!func) {
    SPEC_RULE("Call-Callee-NotFunc");
    result.diag_id = "E-SEM-2531";
    return result;
  }

  const auto& params = func->params;
  if (params.size() != args.size()) {
    SPEC_RULE("Call-ArgCount-Err");
    result.diag_id = "E-SEM-2532";
    result.diag_detail = "expected " + std::to_string(params.size()) +
                         " args, found " + std::to_string(args.size());
    return result;
  }

  // Check move annotations
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (MissingRequiredMoveForConsumingLocal(params[i].mode, args[i])) {
      SPEC_RULE("Call-Move-Missing");
      result.diag_id = "E-SEM-2534";
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (!params[i].mode.has_value() && args[i].moved) {
      SPEC_RULE("Call-Move-Unexpected");
      result.diag_id = "E-SEM-2535";
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  // Type arguments with substitution applied to parameter types
  std::vector<TypeRef> arg_types;
  arg_types.reserve(args.size());
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    const TypeRef subst_param_type = InstantiateType(params[i].type, subst);
    if (!params[i].mode.has_value()) {
      const bool has_source_prov = HasSourceProvenanceLocal(arg.value);
      const bool expected_function_value =
          IsFunctionValueType(ctx, subst_param_type);
      if (has_source_prov && !expected_function_value &&
          !IsPlaceExprForCallLocal(arg.value)) {
        SPEC_RULE("Call-Arg-NotPlace");
        result.diag_id = "E-TYP-1603";
        result.diag_span = ArgDiagnosticSpan(arg);
        return result;
      }
      if (has_source_prov && !expected_function_value && type_place) {
        const auto place_type = (*type_place)(arg.value);
        if (!place_type.ok) {
          result.diag_id = place_type.diag_id;
          result.diag_span = ArgDiagnosticSpan(arg);
          return result;
        }
        arg_types.push_back(place_type.type);
      } else {
        if ((!has_source_prov || expected_function_value) && check_expr) {
          const auto checked = (*check_expr)(arg.value, subst_param_type);
          if (checked.ok) {
            arg_types.push_back(subst_param_type);
            continue;
          }
          if (IsExpectedTypeMismatch(checked)) {
            SPEC_RULE("Call-ArgType-Err");
            result.diag_id = "E-SEM-2533";
            result.diag_span = ArgDiagnosticSpan(arg);
            return result;
          }
          result.diag_id = checked.diag_id;
          result.diag_span = ArgDiagnosticSpan(arg);
          return result;
        }
        const auto arg_type = type_expr(arg.value);
        if (!arg_type.ok) {
          result.diag_id = arg_type.diag_id;
          result.diag_span = arg_type.diag_span.has_value()
                                 ? arg_type.diag_span
                                 : ArgDiagnosticSpan(arg);
          return result;
        }
        arg_types.push_back(arg_type.type);
      }
      continue;
    }
    const auto arg_expr = MovedArgExprLocal(arg);
    if (UsesCallTempForConsumingLocal(params[i].mode, arg) && check_expr) {
      const auto checked = (*check_expr)(arg_expr, subst_param_type);
      if (checked.ok) {
        arg_types.push_back(subst_param_type);
        continue;
      }
      if (IsExpectedTypeMismatch(checked)) {
        SPEC_RULE("Call-ArgType-Err");
        result.diag_id = "E-SEM-2533";
        result.diag_span = ArgDiagnosticSpan(arg);
        return result;
      }
      result.diag_id = checked.diag_id;
      result.diag_span = ArgDiagnosticSpan(arg);
      return result;
    }
    const auto arg_type = type_expr(arg_expr);
    if (!arg_type.ok) {
      result.diag_id = arg_type.diag_id;
      result.diag_span = arg_type.diag_span.has_value()
                             ? arg_type.diag_span
                             : ArgDiagnosticSpan(arg);
      return result;
    }
    arg_types.push_back(arg_type.type);
  }

  // Check arg types against substituted parameter types
  for (std::size_t i = 0; i < args.size(); ++i) {
    // Apply substitution to parameter type (T -> concrete type)
    const TypeRef subst_param_type = InstantiateType(params[i].type, subst);
    const auto sub =
        ArgumentTypeCompatible(ctx, arg_types[i], subst_param_type,
                               params[i].mode);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
    if (!sub.subtype) {
      SPEC_RULE("Call-ArgType-Err");
      result.diag_id = "E-SEM-2533";
      result.diag_detail = "expected type " + TypeToString(subst_param_type) +
                           ", found " + TypeToString(arg_types[i]);
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (!params[i].mode.has_value() &&
        !IsFunctionValueType(ctx, InstantiateType(params[i].type, subst)) &&
        HasSourceProvenanceLocal(args[i].value) &&
        !IsPlaceExprForCallLocal(args[i].value)) {
      SPEC_RULE("Call-Arg-NotPlace");
      result.diag_id = "E-TYP-1603";
      result.diag_span = ArgDiagnosticSpan(args[i]);
      return result;
    }
  }

  if (params.empty()) {
    SPEC_RULE("ArgsT-Empty");
  } else {
    for (std::size_t i = 0; i < params.size(); ++i) {
      if (params[i].mode == ParamMode::Move) {
        const auto moved = MovedArgExprLocal(args[i]);
        const auto moved_type = type_expr(moved);
        if (!moved_type.ok) {
          result.diag_id = moved_type.diag_id;
          result.diag_span = moved_type.diag_span.has_value()
                                 ? moved_type.diag_span
                                 : ArgDiagnosticSpan(args[i]);
          return result;
        }
        // Apply substitution to parameter type
        const TypeRef subst_param_type = InstantiateType(params[i].type, subst);
        const auto sub =
            ArgumentTypeCompatible(ctx, moved_type.type, subst_param_type,
                                   params[i].mode);
        if (!sub.ok) {
          result.diag_id = sub.diag_id;
          result.diag_span = ArgDiagnosticSpan(args[i]);
          return result;
        }
        if (!sub.subtype) {
          SPEC_RULE("Call-ArgType-Err");
          result.diag_id = "E-SEM-2533";
          result.diag_detail = "expected type " + TypeToString(subst_param_type) +
                               ", found " + TypeToString(moved_type.type);
          result.diag_span = ArgDiagnosticSpan(args[i]);
          return result;
        }
        SPEC_RULE("ArgsT-Cons");
        continue;
      }
      if (!IsFunctionValueType(ctx, InstantiateType(params[i].type, subst)) &&
          HasSourceProvenanceLocal(args[i].value)) {
        const auto addr_ok = AddrOfOk(ctx, args[i].value, type_expr);
        if (!addr_ok.ok) {
          if (addr_ok.diag_id ==
              std::optional<std::string_view>("E-TYP-2105")) {
            SPEC_RULE("Call-Arg-Packed-Unsafe-Err");
          }
          result.diag_id = addr_ok.diag_id;
          result.diag_span = ArgDiagnosticSpan(args[i]);
          return result;
        }
      }
      SPEC_RULE("ArgsT-Cons-Ref");
    }
  }

  // Return substituted return type
  result.ok = true;
  result.type = InstantiateType(func->ret, subst);
  return result;
}

}  // namespace cursive::analysis
