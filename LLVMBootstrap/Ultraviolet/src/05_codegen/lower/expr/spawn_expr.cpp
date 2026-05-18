// =============================================================================
// MIGRATION MAPPING: expr/spawn_expr.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 10 (Structured Parallelism)
//   - Spawn expressions within parallel blocks
//   - Capture semantics for spawn closures
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - SpawnExpr visitor produces IRSpawn (lines 1790-2044)
//   - Capture collection helpers
//   - CaptureCollector for capture analysis
//   - LowerCtxSnapshot for context save/restore
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir/ir_model.h (IRSpawn, IRBindVar, IRWritePtr, etc.)
//   - ultraviolet/src/05_codegen/lower/lower_expr.h (LowerResult, LowerCtx, etc.)
//   - ultraviolet/src/05_codegen/cleanup/cleanup.h (CleanupPlan, EmitCleanup)
//   - ultraviolet/src/05_codegen/cleanup/unwind.h (USizeImmediate, kPanicOutName, PanicOutType)
//   - ultraviolet/src/04_analysis/layout/layout.h (SizeOf)
//   - ultraviolet/src/05_codegen/abi/abi.h (PanicOutType)
//
// =============================================================================

#include "05_codegen/lower/expr/spawn_expr.h"

#include <cctype>
#include <cstdio>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_proc.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/cleanup/unwind.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/types.h"
#include "00_core/assert_spec.h"
#include "00_core/process_config.h"

namespace ultraviolet::codegen {

namespace {

// =============================================================================
// Helper Types and Functions
// =============================================================================

using CaptureBinding = ParallelCaptureBinding;

// ScopedNames tracks locally-defined names within nested scopes to avoid
// capturing them
struct ScopedNames {
  std::vector<std::unordered_set<std::string>> scopes;

  void Push() { scopes.emplace_back(); }
  void Pop() {
    if (!scopes.empty()) {
      scopes.pop_back();
    }
  }
  bool IsLocal(const std::string& name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      if (it->find(name) != it->end()) {
        return true;
      }
    }
    return false;
  }
  void Add(const std::string& name) {
    if (!scopes.empty()) {
      scopes.back().insert(name);
    }
  }
  void AddAll(const std::vector<std::string>& names) {
    for (const auto& name : names) {
      Add(name);
    }
  }
};

// Get permission from a type
analysis::Permission PermissionOfType(const analysis::TypeRef& type) {
  if (!type) {
    return analysis::Permission::Const;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    return perm->perm;
  }
  return analysis::Permission::Const;
}

// Check if a type is the unit type ()
bool IsUnitType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  return false;
}

std::string SpawnModuleSuffix(const LowerCtx& ctx) {
  if (ctx.module_path.empty()) {
    return "root";
  }
  std::string out;
  for (std::size_t i = 0; i < ctx.module_path.size(); ++i) {
    if (i != 0) {
      out += "__";
    }
    for (unsigned char ch : ctx.module_path[i]) {
      out.push_back(static_cast<char>(std::isalnum(ch) ? ch : '_'));
    }
  }
  return out;
}

std::string NextSpawnSynthSymbol(LowerCtx& ctx, std::string_view role) {
  return "__cx_spawn_" + std::string(role) + "_" +
         SpawnModuleSuffix(ctx) + "_" +
         std::to_string(ctx.synth_proc_counter++);
}

// Get the root name from a place expression
std::optional<std::string> PlaceRootName(const ast::ExprPtr& expr);

// Forward declarations for pattern name collection
void CollectFieldPatNames(const ast::FieldPattern& field,
                          std::vector<std::string>& out);
void CollectPatternNames(const ast::Pattern& pat,
                         std::vector<std::string>& out);

void CollectFieldPatNames(const ast::FieldPattern& field,
                          std::vector<std::string>& out) {
  if (field.pattern_opt) {
    CollectPatternNames(*field.pattern_opt, out);
    return;
  }
  out.push_back(field.name);
}

void CollectPatternNames(const ast::Pattern& pat,
                         std::vector<std::string>& out) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          out.push_back(node.name);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (node.name == "_") {
            return;
          }
          out.push_back(node.name);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            if (elem) {
              CollectPatternNames(*elem, out);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            CollectFieldPatNames(field, out);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (node.payload_opt) {
            std::visit(
                [&out](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      if (elem) {
                        CollectPatternNames(*elem, out);
                      }
                    }
                  } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                    for (const auto& field : payload.fields) {
                      CollectFieldPatNames(field, out);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt.has_value()) {
            return;
          }
          for (const auto& field : node.fields_opt->fields) {
            CollectFieldPatNames(field, out);
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          if (node.lo) {
            CollectPatternNames(*node.lo, out);
          }
          if (node.hi) {
            CollectPatternNames(*node.hi, out);
          }
        }
      },
      pat.node);
}

std::optional<std::string> PlaceRootName(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  return std::visit(
      [&](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return node.name;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return PlaceRootName(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return PlaceRootName(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return PlaceRootName(node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return PlaceRootName(node.value);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return PlaceRootName(node.place);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return PlaceRootName(node.place);
        }
        return std::nullopt;
      },
      expr->node);
}

// =============================================================================
// CaptureCollector - Collects all captured variables from a block
// =============================================================================

struct CaptureCollector {
  LowerCtx& ctx;
  std::unordered_map<std::string, CaptureBinding> captures;
  std::vector<std::string> order;
  ScopedNames locals;
  std::unordered_set<std::string> explicit_moves;

  void MaybeCaptureImplicitRegion() {
    if (ctx.active_region_aliases.empty()) {
      return;
    }
    RecordCapture(ctx.active_region_aliases.back());
  }

  void RecordCapture(std::string_view name) {
    const std::string key(name);
    if (locals.IsLocal(key)) {
      return;
    }
    const auto* binding = ctx.GetBindingState(key);
    if (!binding || !binding->type) {
      return;
    }
    if (captures.find(key) != captures.end()) {
      return;
    }
    CaptureBinding entry;
    entry.name = key;
    entry.type = binding->type;
    entry.explicit_move = explicit_moves.find(key) != explicit_moves.end();
    captures.emplace(key, entry);
    order.push_back(key);
  }

  void MarkExplicitMove(const ast::ExprPtr& place) {
    const auto root = PlaceRootName(place);
    if (!root.has_value()) {
      return;
    }
    if (locals.IsLocal(*root)) {
      return;
    }
    const auto* binding = ctx.GetBindingState(*root);
    if (!binding || !binding->type) {
      return;
    }
    explicit_moves.insert(*root);
    const auto found = captures.find(*root);
    if (found != captures.end()) {
      found->second.explicit_move = true;
    }
  }

  void VisitExpr(const ast::ExprPtr& expr);
  void VisitStmt(const ast::Stmt& stmt);
  void VisitBlock(const ast::Block& block);
};

void CaptureCollector::VisitExpr(const ast::ExprPtr& expr) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          RecordCapture(node.name);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          VisitExpr(node.expr);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            const auto& args = std::get<ast::ParenArgs>(node.args).args;
            for (const auto& arg : args) {
              VisitExpr(arg.value);
            }
          } else {
            const auto& fields = std::get<ast::BraceArgs>(node.args).fields;
            for (const auto& field : fields) {
              VisitExpr(field.value);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          // Path expressions don't capture
        } else if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          // Literals don't capture
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          VisitExpr(node.lhs);
          VisitExpr(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            VisitExpr(elem);
          }
    } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
      ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& subexpr) {
        VisitExpr(subexpr);
      });
    } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
      VisitExpr(node.value);
      VisitExpr(node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            VisitExpr(field.value);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.payload_opt) {
            std::visit(
                [this](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      VisitExpr(elem);
                    }
                  } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                    for (const auto& field : payload.fields) {
                      VisitExpr(field.value);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          VisitExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          VisitExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          VisitExpr(node.base);
          VisitExpr(node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          VisitExpr(node.callee);
          for (const auto& arg : node.args) {
            VisitExpr(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          VisitExpr(node.receiver);
          for (const auto& arg : node.args) {
            VisitExpr(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          VisitExpr(node.cond);
          if (node.then_expr) {
            VisitExpr(node.then_expr);
          }
          if (node.else_expr) {
            VisitExpr(node.else_expr);
          }
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          VisitExpr(node.scrutinee);
          for (const auto& case_clause : node.cases) {
            locals.Push();
            std::vector<std::string> names;
            if (case_clause.pattern) {
              CollectPatternNames(*case_clause.pattern, names);
            }
            locals.AddAll(names);
            if (case_clause.body) {
              VisitExpr(case_clause.body);
            }
            locals.Pop();
          }
          VisitExpr(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          VisitExpr(node.scrutinee);
          locals.Push();
          std::vector<std::string> names;
          if (node.pattern) {
            CollectPatternNames(*node.pattern, names);
          }
          locals.AddAll(names);
          VisitExpr(node.then_expr);
          locals.Pop();
          VisitExpr(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          VisitExpr(node.cond);
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          VisitExpr(node.iter);
          locals.Push();
          std::vector<std::string> names;
          if (node.pattern) {
            CollectPatternNames(*node.pattern, names);
          }
          locals.AddAll(names);
          if (node.body) {
            VisitBlock(*node.body);
          }
          locals.Pop();
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            VisitBlock(*node.block);
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            VisitBlock(*node.block);
          }
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          VisitExpr(node.place);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          MarkExplicitMove(node.place);
          VisitExpr(node.place);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          if (node.lhs) {
            VisitExpr(node.lhs);
          }
          if (node.rhs) {
            VisitExpr(node.rhs);
          }
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          VisitExpr(node.value);
          if (node.region_opt) {
            RecordCapture(*node.region_opt);
          } else {
            MaybeCaptureImplicitRegion();
          }
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          // Type, no capture
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          // Type, no capture
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          VisitExpr(node.domain);
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          VisitExpr(node.handle);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          VisitExpr(node.range);
          locals.Push();
          std::vector<std::string> names;
          if (node.pattern) {
            CollectPatternNames(*node.pattern, names);
          }
          locals.AddAll(names);
          if (node.body) {
            VisitBlock(*node.body);
          }
          locals.Pop();
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            VisitExpr(arm.expr);
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& e : node.exprs) {
            VisitExpr(e);
          }
        }
      },
      expr->node);
}

void CaptureCollector::VisitStmt(const ast::Stmt& stmt) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          VisitExpr(node.binding.init);
          std::vector<std::string> names;
          if (node.binding.pat) {
            CollectPatternNames(*node.binding.pat, names);
          }
          locals.AddAll(names);
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          if (node.binding.init) {
            VisitExpr(node.binding.init);
          }
          std::vector<std::string> names;
          if (node.binding.pat) {
            CollectPatternNames(*node.binding.pat, names);
          }
          locals.AddAll(names);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression,
          // but the alias name still enters the surrounding scope.
          locals.Add(node.alias);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          VisitExpr(node.place);
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          VisitExpr(node.place);
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          VisitExpr(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          VisitExpr(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        }
      },
      stmt);
}

void CaptureCollector::VisitBlock(const ast::Block& block) {
  locals.Push();
  for (const auto& stmt : block.stmts) {
    VisitStmt(stmt);
  }
  VisitExpr(block.tail_opt);
  locals.Pop();
}

// Collect captures from a block
std::vector<CaptureBinding> CollectCaptures(
    const ast::Block& body, LowerCtx& ctx) {
  CaptureCollector collector{ctx, {}, {}, {}, {}};
  collector.VisitBlock(body);
  std::vector<CaptureBinding> result;
  result.reserve(collector.order.size());
  for (const auto& key : collector.order) {
    result.push_back(collector.captures.at(key));
  }
  return result;
}

// =============================================================================
// LowerCtxSnapshot - Save and restore LowerCtx state
// =============================================================================

struct LowerCtxSnapshot {
  std::vector<ScopeInfo> scope_stack;
  std::unordered_map<std::string, std::vector<BindingState>> binding_states;
  std::unordered_map<std::string, DerivedValueInfo> derived_values;
  std::vector<TempValue>* temp_sink = nullptr;
  int temp_depth = 0;
  std::optional<int> suppress_temp_at_depth;
  std::vector<ParallelCollectItem>* parallel_collect = nullptr;
  int parallel_collect_depth = 0;
  std::optional<CaptureEnvInfo> capture_env;
  analysis::TypeRef proc_ret_type;
  std::vector<std::string> active_region_aliases;

  explicit LowerCtxSnapshot(const LowerCtx& ctx)
      : scope_stack(ctx.scope_stack),
        binding_states(ctx.binding_states),
        derived_values(ctx.values.derived_values),
        temp_sink(ctx.temp_sink),
        temp_depth(ctx.temp_depth),
        suppress_temp_at_depth(ctx.suppress_temp_at_depth),
        parallel_collect(ctx.parallel_collect),
        parallel_collect_depth(ctx.parallel_collect_depth),
        capture_env(ctx.capture_env),
        proc_ret_type(ctx.proc_ret_type),
        active_region_aliases(ctx.active_region_aliases) {}

  void Restore(LowerCtx& ctx) const {
    ctx.scope_stack = scope_stack;
    ctx.binding_states = binding_states;
    // Merge derived_values: preserve new values created during the nested
    // lowering (e.g., capture_ptr_N) while restoring values from the snapshot.
    // New derived values are referenced from generated IR and must be preserved.
    for (const auto& [key, value] : derived_values) {
      ctx.values.derived_values[key] = value;
    }
    ctx.temp_sink = temp_sink;
    ctx.temp_depth = temp_depth;
    ctx.suppress_temp_at_depth = suppress_temp_at_depth;
    ctx.parallel_collect = parallel_collect;
    ctx.parallel_collect_depth = parallel_collect_depth;
    ctx.capture_env = capture_env;
    ctx.proc_ret_type = proc_ret_type;
    ctx.active_region_aliases = active_region_aliases;
  }
};

}  // namespace

// =============================================================================
// LowerSpawnExpr - Lower a spawn expression
// =============================================================================
//
// A spawn expression creates a parallel task that:
//   1. Captures variables from the enclosing scope
//   2. Runs in parallel within a parallel block
//   3. Returns a Spawned<T> handle that can be waited on
//
// Lowering steps:
//   1. Collect all captures from the spawn body (including explicit move captures)
//   2. Build capture environment tuple with proper field types:
//      - Pointer for const/shared (by_ref)
//      - Value for move captures
//   3. Create environment variable and get its pointer
//   4. Generate wrapper procedure with params: env ptr, result ptr, panic out
//   5. Inside wrapper:
//      a. Restore capture environment
//      b. Lower body
//      c. Store result
//      d. Cleanup
//      e. Return unit
//   6. Create IRSpawn with captured_env, env_ptr, body_fn symbol, etc.
//
LowerResult LowerSpawnExpr(const ast::Expr& expr,
                           const ast::SpawnExpr& node,
                           LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Spawn");

  // Step 1: Collect all captures from the spawn body
  std::vector<CaptureBinding> captures;
  if (node.body) {
    captures = CollectCaptures(*node.body, ctx);
  }

  auto lowered_env = ctx.LowerParallelCaptureEnv(captures, "spawn");
  auto env_parts = std::move(lowered_env.ir_parts);
  auto env_info = std::move(lowered_env.env_info);
  const analysis::TypeRef env_type = env_info.env_type;
  const IRValue env_ptr = env_info.env_param;

  // Compute environment size
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  std::uint64_t env_size_val = 0;
  if (ctx.sigma) {
    if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, env_type)) {
      env_size_val = *size;
    } else {
      ctx.ReportCodegenFailure();
    }
  }
  IRValue env_size = USizeImmediate(env_size_val);

  // Compute body result type and size.
  // Spec-conformant source of truth for spawn result type is the type of the
  // spawn expression itself (Spawned<T> -> T), not a best-effort tail lookup.
  analysis::TypeRef body_type = analysis::MakeTypePrim("()");
  analysis::TypeRef body_type_from_spawn;
  analysis::TypeRef body_type_from_tail;
  if (ctx.expr_type) {
    analysis::TypeRef spawn_expr_type = ctx.expr_type(expr);
    analysis::TypeRef stripped_spawn_type = analysis::StripPerm(spawn_expr_type);
    if (!stripped_spawn_type) {
      stripped_spawn_type = spawn_expr_type;
    }
    if (const auto inner = analysis::ExtractSpawnedInner(stripped_spawn_type)) {
      body_type_from_spawn = *inner;
    }

    if (node.body && node.body->tail_opt) {
      body_type_from_tail = ctx.expr_type(*node.body->tail_opt);
      analysis::TypeRef stripped_tail_type = analysis::StripPerm(body_type_from_tail);
      if (stripped_tail_type) {
        body_type_from_tail = stripped_tail_type;
      }
    }
  }

  if (body_type_from_spawn) {
    body_type = body_type_from_spawn;
  } else if (body_type_from_tail) {
    body_type = body_type_from_tail;
  }

  if (body_type_from_spawn && body_type_from_tail) {
    const auto equiv = analysis::TypeEquiv(body_type_from_spawn, body_type_from_tail);
    if (!equiv.equiv) {
      const std::string spawn_type_text = analysis::TypeToString(body_type_from_spawn);
      const std::string tail_type_text = analysis::TypeToString(body_type_from_tail);
      std::fprintf(stderr,
                   "[uv] codegen failure: spawn result type mismatch; "
                   "spawn=%s tail=%s\n",
                   spawn_type_text.c_str(),
                   tail_type_text.c_str());
      ctx.ReportCodegenFailure();
    }
  }
  if (!body_type) {
    body_type = analysis::MakeTypePrim("()");
  }
  std::uint64_t result_size_val = 0;
  if (ctx.sigma) {
    if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, body_type)) {
      result_size_val = *size;
    } else {
      ctx.ReportCodegenFailure();
    }
  }
  if (core::IsDebugEnabled("spawn")) {
    const int has_tail = (node.body && node.body->tail_opt) ? 1 : 0;
    const std::string body_type_text = analysis::TypeToString(body_type);
    std::fprintf(stderr,
                 "[uv] spawn lower: tail=%d body_type=%s result_size=%llu\n",
                 has_tail,
                 body_type_text.c_str(),
                 static_cast<unsigned long long>(result_size_val));
  }
  IRValue result_size = USizeImmediate(result_size_val);

  // Step 5: Generate wrapper procedure
  std::string wrapper_sym = NextSpawnSynthSymbol(ctx, "body");
  ProcIR proc;
  proc.symbol = wrapper_sym;
  proc.ret = analysis::MakeTypePrim("()");

  proc.params.push_back(HostedEnvParam());
  ctx.hosted_explicit_env_procs.insert(wrapper_sym);

  analysis::TypeRef env_ptr_type =
      analysis::MakeTypePtr(env_type, analysis::PtrState::Valid);
  analysis::TypeRef result_ptr_type =
      analysis::MakeTypePtr(body_type, analysis::PtrState::Valid);

  // Note: env and result are passed by value (Move mode) because the runtime
  // callback ABI passes hosted_env/env/result/panic directly, not by
  // reference. Using nullopt mode would generate ByRef ABI which expects
  // pointer-to-pointer semantics.
  IRParam env_param;
  env_param.mode = analysis::ParamMode::Move;
  env_param.name = "env";
  env_param.type = env_ptr_type;
  proc.params.push_back(env_param);

  IRParam result_param;
  result_param.mode = analysis::ParamMode::Move;
  result_param.name = "result";
  result_param.type = result_ptr_type;
  proc.params.push_back(result_param);

  IRParam panic_param = PanicOutParam();
  proc.params.push_back(panic_param);

  // Step 6: Generate wrapper body with saved/restored context
  {
    LowerCtxSnapshot snapshot(ctx);
    ctx.scope_stack.clear();
    ctx.binding_states.clear();
    ctx.values.derived_values.clear();
    ctx.temp_sink = nullptr;
    ctx.temp_depth = 0;
    ctx.suppress_temp_at_depth.reset();
    ctx.parallel_collect = nullptr;
    ctx.parallel_collect_depth = 0;
    ctx.capture_env.reset();
    ctx.proc_ret_type = analysis::MakeTypePrim("()");

    ctx.PushScope(false, false);
    ctx.RegisterVar(env_param.name, env_ptr_type, false, false);
    proc.params[1].stable_name = ctx.StableBindingName(env_param.name);
    ctx.RegisterVar(result_param.name, result_ptr_type, false, false);
    proc.params[2].stable_name = ctx.StableBindingName(result_param.name);
    ctx.RegisterVar(panic_param.name, panic_param.type, true, false);
    proc.params[3].stable_name = ctx.StableBindingName(panic_param.name);

    IRPtr proc_region_ir = EnterSyntheticProcedureRegion(ctx);

    IRValue env_param_val;
    env_param_val.kind = IRValue::Kind::Local;
    env_param_val.name = env_param.name;
    ctx.BindAll(ctx.LoadEnv(env_param_val, env_info.env_type, env_info.captures));

    // Lower the spawn body
    LowerResult body_result;
    if (node.body) {
      body_result = LowerBlock(*node.body, ctx);
    } else {
      IRValue unit = ctx.FreshTempValue("unit");
      body_result = LowerResult{EmptyIR(), unit};
    }
    ctx.active_region_aliases.pop_back();

    // Store result if not unit type
    IRPtr store_ir = EmptyIR();
    if (!IsUnitType(body_type)) {
      IRWritePtr write;
      IRValue result_ptr_val;
      result_ptr_val.kind = IRValue::Kind::Local;
      result_ptr_val.name = result_param.name;
      write.ptr = result_ptr_val;
      write.value = body_result.value;
      store_ir = MakeIR(std::move(write));
    }

    // Cleanup
    CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
    IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);
    ctx.PopScope();

    // Return unit
    IRValue unit_ret = ctx.FreshTempValue("unit");
    IRReturn ret;
    ret.value = unit_ret;

    std::vector<IRPtr> parts;
    parts.push_back(MakeIR(IRCancelSuppress{}));
    if (proc_region_ir && !std::holds_alternative<IROpaque>(proc_region_ir->node)) {
      parts.push_back(proc_region_ir);
    }
    if (body_result.ir) {
      parts.push_back(body_result.ir);
    }
    if (store_ir && !std::holds_alternative<IROpaque>(store_ir->node)) {
      parts.push_back(store_ir);
    }
    if (cleanup_ir && !std::holds_alternative<IROpaque>(cleanup_ir->node)) {
      parts.push_back(cleanup_ir);
    }
    parts.push_back(MakeIR(std::move(ret)));
    proc.body = SeqIR(std::move(parts));

    snapshot.Restore(ctx);
  }

  ctx.QueueExtraProc(std::move(proc), LinkageKind::Internal);

  // Step 7: Create IRSpawn
  IRSpawn spawn;
  spawn.body = EmptyIR();
  IRValue unit_body = ctx.FreshTempValue("unit");
  spawn.body_result = unit_body;
  spawn.env_ptr = env_ptr;
  spawn.env_size = env_size;
  spawn.body_fn.kind = IRValue::Kind::Symbol;
  spawn.body_fn.name = wrapper_sym;
  spawn.result_size = result_size;
  spawn.result = ctx.FreshTempValue("spawn_handle");
  IRValue spawn_result = spawn.result;
  ctx.RegisterValueType(
      spawn_result,
      analysis::MakeSpawnedTypeWithState(body_type, "Pending"));

  std::vector<IRPtr> option_parts;
  option_parts.reserve(node.opts.size());

  // Handle options (name, affinity, priority) and lower option expressions so
  // their evaluation is not dropped.
  for (const auto& opt : node.opts) {
    if (!opt.value) {
      continue;
    }
    auto opt_result = LowerExpr(*opt.value, ctx);
    if (opt_result.ir && !std::holds_alternative<IROpaque>(opt_result.ir->node)) {
      option_parts.push_back(opt_result.ir);
    }

    switch (opt.kind) {
      case ast::SpawnOptionKind::Name:
        if (const auto* lit = std::get_if<ast::LiteralExpr>(&opt.value->node)) {
          spawn.name = lit->literal.lexeme;
        }
        break;
      case ast::SpawnOptionKind::Affinity:
        spawn.affinity_mask = opt_result.value;
        break;
      case ast::SpawnOptionKind::Priority:
        spawn.priority = opt_result.value;
        break;
    }
  }

  env_parts.insert(env_parts.end(), option_parts.begin(), option_parts.end());
  spawn.captured_env = SeqIR(std::move(env_parts));

  return LowerResult{MakeIR(std::move(spawn)), spawn_result};
}

}  // namespace ultraviolet::codegen
